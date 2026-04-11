// ubus_interface.cpp — Stage 4: ubus object + VpnManager implementation
//
// ubus method signatures (for `ubus call openvpn3 <method>`):
//   start   { "instance": "myvpn", "config": "/etc/openvpn3/myvpn.ovpn",
//              "username": "u", "password": "p" }
//   stop    { "instance": "myvpn" }
//   status  { "instance": "myvpn" }
//   list    {}
//   import  { "path": "/tmp/my.ovpn", "name": "myvpn" }
//
// Threading model:
//   - ubus callbacks run on main thread (uloop)
//   - Each VPN connection runs in a detached worker thread
//   - VpnManager::mu_ protects all shared state

// OpenVPN3 headers must come before libubus/libubox to avoid
// __has_cpp_attribute detection being broken by C headers.
#include <openvpn/common/platform.hpp>
#if defined(OPENVPN_PLATFORM_LINUX) && !defined(OPENVPN_USE_SITNL)
#define OPENVPN_USE_SITNL
#endif
#define OPENVPN_LOG_GLOBAL
#include <openvpn/log/logbasesimple.hpp>
#include <client/ovpncli.cpp>
#include <openvpn/common/exception.hpp>
#include <openvpn/ssl/sslchoose.hpp>
#include "tun_openwrt.hpp"

#include "ubus_interface.hpp"
#include "uci_config.hpp"

#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>

using namespace openvpn;

namespace ovpn3_openwrt {

// ============================================================================
// Helpers
// ============================================================================

const char* vpn_state_str(VpnState s)
{
    switch (s) {
    case VpnState::IDLE:          return "idle";
    case VpnState::CONNECTING:    return "connecting";
    case VpnState::CONNECTED:     return "connected";
    case VpnState::DISCONNECTING: return "disconnecting";
    case VpnState::ERROR:         return "error";
    }
    return "unknown";
}

// ============================================================================
// ManagedClient — OpenVPNClient wired to VpnManager callbacks
// ============================================================================

class ManagedClient : public ClientAPI::OpenVPNClient
{
public:
    ManagedClient(VpnManager& mgr, const std::string& name)
        : mgr_(mgr), name_(name) {}

    // TunBuilderBase — delegate to tun_
    bool tun_builder_new() override
        { return tun_.tun_builder_new(); }
    bool tun_builder_set_remote_address(const std::string& a, bool ipv6) override
        { return tun_.tun_builder_set_remote_address(a, ipv6); }
    bool tun_builder_add_address(const std::string& a, int pl,
                                 const std::string& gw, bool ipv6, bool net30) override
        { return tun_.tun_builder_add_address(a, pl, gw, ipv6, net30); }
    bool tun_builder_reroute_gw(bool ipv4, bool ipv6, unsigned int flags) override
        { return tun_.tun_builder_reroute_gw(ipv4, ipv6, flags); }
    bool tun_builder_add_route(const std::string& a, int pl, int metric, bool ipv6) override
        { return tun_.tun_builder_add_route(a, pl, metric, ipv6); }
    bool tun_builder_exclude_route(const std::string& a, int pl, int metric, bool ipv6) override
        { return tun_.tun_builder_exclude_route(a, pl, metric, ipv6); }
    bool tun_builder_set_dns_options(const DnsOptions& dns) override
        { return tun_.tun_builder_set_dns_options(dns); }
    bool tun_builder_set_mtu(int mtu) override
        { return tun_.tun_builder_set_mtu(mtu); }
    bool tun_builder_set_session_name(const std::string& n) override
        { return tun_.tun_builder_set_session_name(n); }
    int tun_builder_establish() override
        { return tun_.tun_builder_establish(); }
    bool tun_builder_persist() override
        { return false; }
    void tun_builder_teardown(bool d) override
        { tun_.tun_builder_teardown(d); }

    bool socket_protect(openvpn_io::detail::socket_type, std::string remote, bool ipv6) override
        { return tun_.add_bypass_route(remote, ipv6); }

    void event(const ClientAPI::Event& ev) override
    {
        std::cout << "[" << name_ << "] EVENT: " << ev.name;
        if (!ev.info.empty()) std::cout << ' ' << ev.info;
        if (ev.fatal)  std::cout << " [FATAL]";
        if (ev.error)  std::cout << " [ERR]";
        std::cout << '\n' << std::flush;

        if (ev.name == "CONNECTED")
            mgr_.set_state(name_, VpnState::CONNECTED, ev.name);
        else if (ev.name == "DISCONNECTED" || ev.fatal)
            mgr_.set_state(name_, VpnState::IDLE, ev.name);
        else
            mgr_.set_state(name_, VpnState::CONNECTING, ev.name);
    }

    void log(const ClientAPI::LogInfo& li) override
    {
        std::cout << "[" << name_ << "] " << li.text << std::flush;
    }

    void acc_event(const ClientAPI::AppCustomControlMessageEvent&) override {}
    void external_pki_cert_request(ClientAPI::ExternalPKICertRequest& r) override
        { r.error = true; r.errorText = "external PKI not implemented"; }
    void external_pki_sign_request(ClientAPI::ExternalPKISignRequest& r) override
        { r.error = true; r.errorText = "external PKI not implemented"; }
    bool pause_on_connection_timeout() override { return false; }

private:
    VpnManager&          mgr_;
    std::string          name_;
    OpenWrtTunBuilder    tun_;
};

// ============================================================================
// VpnManager
// ============================================================================

std::shared_ptr<VpnRuntime> VpnManager::get_locked(const std::string& name) const
{
    auto it = instances_.find(name);
    return (it != instances_.end()) ? it->second : nullptr;
}

std::string VpnManager::start(const std::string& name, const std::string& config_path,
                               const std::string& username, const std::string& password)
{
    std::lock_guard<std::mutex> lock(mu_);

    auto it = instances_.find(name);
    if (it != instances_.end()) {
        auto& rt = it->second;
        if (rt->state == VpnState::CONNECTING || rt->state == VpnState::CONNECTED)
            return "instance already running";
        // Clean up finished thread
        if (rt->worker.joinable())
            rt->worker.detach();
    }

    std::string content = UciConfig::read_ovpn(config_path);
    if (content.empty())
        return "cannot read config: " + config_path;

    auto rt = std::make_shared<VpnRuntime>();
    rt->name        = name;
    rt->config_path = config_path;
    rt->state       = VpnState::CONNECTING;
    instances_[name] = rt;

    // Launch worker thread
    rt->worker = std::thread([this, rt, content, username, password]() {
        auto client = std::make_unique<ManagedClient>(*this, rt->name);

        set_client(rt->name, client.get());

        ClientAPI::Config cfg;
        cfg.content              = content;
        cfg.compressionMode      = "no";
        cfg.tlsVersionMinOverride = "tls_1_2";

        const ClientAPI::EvalConfig eval = client->eval_config(cfg);
        if (eval.error) {
            std::cerr << "[" << rt->name << "] eval_config: " << eval.message << '\n';
            set_state(rt->name, VpnState::ERROR, "eval_config failed");
            set_client(rt->name, nullptr);
            return;
        }

        if (!eval.autologin && !username.empty()) {
            ClientAPI::ProvideCreds creds;
            creds.username = username;
            creds.password = password;
            const ClientAPI::Status cs = client->provide_creds(creds);
            if (cs.error) {
                std::cerr << "[" << rt->name << "] provide_creds: " << cs.message << '\n';
                set_state(rt->name, VpnState::ERROR, "provide_creds failed");
                set_client(rt->name, nullptr);
                return;
            }
        }

        const ClientAPI::Status st = client->connect();
        if (st.error)
            std::cerr << "[" << rt->name << "] connect: " << st.message << '\n';

        set_state(rt->name, VpnState::IDLE, "disconnected");
        set_client(rt->name, nullptr);
    });
    rt->worker.detach();

    return {};
}

std::string VpnManager::stop(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto rt = get_locked(name);
    if (!rt)
        return "instance not found: " + name;
    if (rt->state == VpnState::IDLE)
        return "instance not running";

    rt->state = VpnState::DISCONNECTING;
    if (rt->client_ptr) {
        auto* c = static_cast<ManagedClient*>(rt->client_ptr);
        c->stop();
    }
    return {};
}

bool VpnManager::status(const std::string& name, struct blob_buf* b) const
{
    std::lock_guard<std::mutex> lock(mu_);
    auto rt = get_locked(name);
    if (!rt) return false;

    blobmsg_add_string(b, "instance", rt->name.c_str());
    blobmsg_add_string(b, "state",    vpn_state_str(rt->state));
    blobmsg_add_string(b, "config",   rt->config_path.c_str());
    if (!rt->last_event.empty())
        blobmsg_add_string(b, "last_event", rt->last_event.c_str());
    if (rt->state == VpnState::CONNECTED && rt->connected_at) {
        time_t uptime = time(nullptr) - rt->connected_at;
        blobmsg_add_u32(b, "uptime_sec", static_cast<uint32_t>(uptime));
    }
    blobmsg_add_u64(b, "bytes_in",  rt->bytes_in);
    blobmsg_add_u64(b, "bytes_out", rt->bytes_out);
    return true;
}

void VpnManager::list(struct blob_buf* b) const
{
    std::lock_guard<std::mutex> lock(mu_);
    void* arr = blobmsg_open_array(b, "instances");
    for (const auto& [name, rt] : instances_) {
        void* tbl = blobmsg_open_table(b, nullptr);
        blobmsg_add_string(b, "instance", rt->name.c_str());
        blobmsg_add_string(b, "state",    vpn_state_str(rt->state));
        blobmsg_add_string(b, "config",   rt->config_path.c_str());
        blobmsg_close_table(b, tbl);
    }
    blobmsg_close_array(b, arr);
}

void VpnManager::stop_all()
{
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& [n, _] : instances_)
            names.push_back(n);
    }
    for (const auto& n : names)
        stop(n);
}

bool VpnManager::is_idle(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mu_);
    auto rt = get_locked(name);
    if (!rt) return true;
    return rt->state == VpnState::IDLE || rt->state == VpnState::ERROR;
}

void VpnManager::set_state(const std::string& name, VpnState s, const std::string& event)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto rt = get_locked(name);
    if (!rt) return;
    rt->state = s;
    if (!event.empty()) rt->last_event = event;
    if (s == VpnState::CONNECTED) rt->connected_at = time(nullptr);
}

void VpnManager::set_client(const std::string& name, void* ptr)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto rt = get_locked(name);
    if (rt) rt->client_ptr = ptr;
}

void VpnManager::set_stats(const std::string& name, uint64_t in, uint64_t out)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto rt = get_locked(name);
    if (!rt) return;
    rt->bytes_in  = in;
    rt->bytes_out = out;
}

// ============================================================================
// UbusInterface — static data
// ============================================================================

UbusInterface* UbusInterface::s_instance_ = nullptr;

// blobmsg policy tables for each method
static const struct blobmsg_policy start_policy[] = {
    { "instance", BLOBMSG_TYPE_STRING },
    { "config",   BLOBMSG_TYPE_STRING },
    { "username", BLOBMSG_TYPE_STRING },
    { "password", BLOBMSG_TYPE_STRING },
};
static const struct blobmsg_policy stop_policy[] = {
    { "instance", BLOBMSG_TYPE_STRING },
};
static const struct blobmsg_policy status_policy[] = {
    { "instance", BLOBMSG_TYPE_STRING },
};
static const struct blobmsg_policy import_policy[] = {
    { "path", BLOBMSG_TYPE_STRING },
    { "name", BLOBMSG_TYPE_STRING },
};

struct ubus_method UbusInterface::s_methods_[] = {
    UBUS_METHOD("start",  UbusInterface::handle_start,  start_policy),
    UBUS_METHOD("stop",   UbusInterface::handle_stop,   stop_policy),
    UBUS_METHOD("status", UbusInterface::handle_status, status_policy),
    UBUS_METHOD_NOARG("list", UbusInterface::handle_list),
    UBUS_METHOD("import", UbusInterface::handle_import, import_policy),
};

struct ubus_object_type UbusInterface::s_obj_type_ = {
    .name     = "openvpn3",
    .id       = 0,
    .methods  = UbusInterface::s_methods_,
    .n_methods = ARRAY_SIZE(UbusInterface::s_methods_),
};

struct ubus_object UbusInterface::s_obj_ = {
    .name      = "openvpn3",
    .type      = &UbusInterface::s_obj_type_,
    .methods   = UbusInterface::s_methods_,
    .n_methods = ARRAY_SIZE(UbusInterface::s_methods_),
};

// ============================================================================
// UbusInterface — init / shutdown
// ============================================================================

UbusInterface::~UbusInterface()
{
    shutdown();
}

bool UbusInterface::init(const std::string& socket_path)
{
    s_instance_ = this;

    const char* sock = socket_path.empty() ? nullptr : socket_path.c_str();
    ctx_ = ubus_connect(sock);
    if (!ctx_) {
        std::cerr << "ubus_connect failed\n";
        return false;
    }

    ubus_add_uloop(ctx_);

    int ret = ubus_add_object(ctx_, &s_obj_);
    if (ret) {
        std::cerr << "ubus_add_object failed: " << ubus_strerror(ret) << '\n';
        ubus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    std::cout << "ubus: registered object 'openvpn3'\n";
    return true;
}

void UbusInterface::shutdown()
{
    if (ctx_) {
        ubus_free(ctx_);
        ctx_ = nullptr;
    }
    s_instance_ = nullptr;
}

// ============================================================================
// Method handlers
// ============================================================================

int UbusInterface::handle_start(struct ubus_context* ctx, struct ubus_object*,
                                 struct ubus_request_data* req,
                                 const char*, struct blob_attr* msg)
{
    enum { INSTANCE, CONFIG, USERNAME, PASSWORD, __MAX };
    struct blob_attr* tb[__MAX];
    blobmsg_parse(start_policy, __MAX, tb, blob_data(msg), blob_len(msg));

    if (!tb[INSTANCE] || !tb[CONFIG]) {
        struct blob_buf b = {};
        blob_buf_init(&b, 0);
        blobmsg_add_string(&b, "error", "missing 'instance' or 'config'");
        ubus_send_reply(ctx, req, b.head);
        blob_buf_free(&b);
        return UBUS_STATUS_INVALID_ARGUMENT;
    }

    std::string name   = blobmsg_get_string(tb[INSTANCE]);
    std::string config = blobmsg_get_string(tb[CONFIG]);
    std::string user   = tb[USERNAME] ? blobmsg_get_string(tb[USERNAME]) : "";
    std::string pass   = tb[PASSWORD] ? blobmsg_get_string(tb[PASSWORD]) : "";

    std::string err = s_instance_->mgr_.start(name, config, user, pass);

    struct blob_buf b = {};
    blob_buf_init(&b, 0);
    if (err.empty()) {
        blobmsg_add_string(&b, "result", "ok");
        blobmsg_add_string(&b, "instance", name.c_str());
    } else {
        blobmsg_add_string(&b, "error", err.c_str());
    }
    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
    return UBUS_STATUS_OK;
}

int UbusInterface::handle_stop(struct ubus_context* ctx, struct ubus_object*,
                                struct ubus_request_data* req,
                                const char*, struct blob_attr* msg)
{
    enum { INSTANCE, __MAX };
    struct blob_attr* tb[__MAX];
    blobmsg_parse(stop_policy, __MAX, tb, blob_data(msg), blob_len(msg));

    if (!tb[INSTANCE]) {
        struct blob_buf b = {};
        blob_buf_init(&b, 0);
        blobmsg_add_string(&b, "error", "missing 'instance'");
        ubus_send_reply(ctx, req, b.head);
        blob_buf_free(&b);
        return UBUS_STATUS_INVALID_ARGUMENT;
    }

    std::string name = blobmsg_get_string(tb[INSTANCE]);
    std::string err  = s_instance_->mgr_.stop(name);

    struct blob_buf b = {};
    blob_buf_init(&b, 0);
    if (err.empty())
        blobmsg_add_string(&b, "result", "ok");
    else
        blobmsg_add_string(&b, "error", err.c_str());
    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
    return UBUS_STATUS_OK;
}

int UbusInterface::handle_status(struct ubus_context* ctx, struct ubus_object*,
                                  struct ubus_request_data* req,
                                  const char*, struct blob_attr* msg)
{
    enum { INSTANCE, __MAX };
    struct blob_attr* tb[__MAX];
    blobmsg_parse(status_policy, __MAX, tb, blob_data(msg), blob_len(msg));

    if (!tb[INSTANCE]) {
        struct blob_buf b = {};
        blob_buf_init(&b, 0);
        blobmsg_add_string(&b, "error", "missing 'instance'");
        ubus_send_reply(ctx, req, b.head);
        blob_buf_free(&b);
        return UBUS_STATUS_INVALID_ARGUMENT;
    }

    std::string name = blobmsg_get_string(tb[INSTANCE]);
    struct blob_buf b = {};
    blob_buf_init(&b, 0);
    if (!s_instance_->mgr_.status(name, &b))
        blobmsg_add_string(&b, "error", ("instance not found: " + name).c_str());
    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
    return UBUS_STATUS_OK;
}

int UbusInterface::handle_list(struct ubus_context* ctx, struct ubus_object*,
                                struct ubus_request_data* req,
                                const char*, struct blob_attr*)
{
    struct blob_buf b = {};
    blob_buf_init(&b, 0);
    s_instance_->mgr_.list(&b);
    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
    return UBUS_STATUS_OK;
}

int UbusInterface::handle_import(struct ubus_context* ctx, struct ubus_object*,
                                  struct ubus_request_data* req,
                                  const char*, struct blob_attr* msg)
{
    enum { PATH, NAME, __MAX };
    struct blob_attr* tb[__MAX];
    blobmsg_parse(import_policy, __MAX, tb, blob_data(msg), blob_len(msg));

    if (!tb[PATH] || !tb[NAME]) {
        struct blob_buf b = {};
        blob_buf_init(&b, 0);
        blobmsg_add_string(&b, "error", "missing 'path' or 'name'");
        ubus_send_reply(ctx, req, b.head);
        blob_buf_free(&b);
        return UBUS_STATUS_INVALID_ARGUMENT;
    }

    std::string src  = blobmsg_get_string(tb[PATH]);
    std::string name = blobmsg_get_string(tb[NAME]);
    std::string dst  = "/etc/openvpn3/" + name + ".ovpn";

    struct blob_buf b = {};
    blob_buf_init(&b, 0);

    // Copy file
    try {
        std::filesystem::create_directories("/etc/openvpn3");
        std::filesystem::copy_file(src, dst,
            std::filesystem::copy_options::overwrite_existing);
        blobmsg_add_string(&b, "result", "ok");
        blobmsg_add_string(&b, "stored", dst.c_str());
    } catch (const std::exception& e) {
        blobmsg_add_string(&b, "error", e.what());
    }

    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
    return UBUS_STATUS_OK;
}

} // namespace ovpn3_openwrt
