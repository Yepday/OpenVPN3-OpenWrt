// ubus_interface.hpp — ubus object exposing openvpn3d control API
//
// Registers a ubus object "openvpn3" with methods:
//   start   { instance: "name" }        → start a VPN instance
//   stop    { instance: "name" }        → disconnect
//   status  { instance: "name" }        → connection state, bytes, uptime
//   list    {}                          → all configured instances + state
//   import  { path: "/tmp/my.ovpn" }   → import .ovpn into UCI config
//
// Architecture:
//   - UbusInterface owns a VpnManager (multi-instance lifecycle)
//   - ubus callbacks run on the main thread (uloop)
//   - Each VPN connection runs in its own worker thread
//   - Thread-safe state access via mutex in VpnManager
//
// Usage:
//   UbusInterface iface;
//   iface.init();          // connect to ubusd, register object
//   uloop_run();           // blocks; ubus calls arrive as callbacks
//   iface.shutdown();

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

extern "C" {
#include <libubus.h>
#include <libubox/blobmsg_json.h>
}

namespace ovpn3_openwrt {

// Per-instance runtime state
enum class VpnState {
    IDLE,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    ERROR,
};

const char* vpn_state_str(VpnState s);

struct VpnRuntime {
    std::string name;
    std::string config_path;
    VpnState    state{ VpnState::IDLE };
    std::string last_event;       // last SDK event name
    uint64_t    bytes_in{ 0 };
    uint64_t    bytes_out{ 0 };
    time_t      connected_at{ 0 };

    std::thread          worker;
    std::atomic<bool>    stop_requested{ false };

    // Pointer to the live OpenVPNClient (set by worker, cleared on exit)
    // Access only under VpnManager::mu_
    void* client_ptr{ nullptr };  // typed as void* to avoid pulling in ovpncli.hpp here
};

// ============================================================================
// VpnManager — owns all VpnRuntime instances
// ============================================================================

class VpnManager {
public:
    // Start a VPN instance by name (config_path = path to .ovpn file).
    // Returns error string, empty on success.
    std::string start(const std::string& name, const std::string& config_path,
                      const std::string& username = {}, const std::string& password = {});

    // Stop a running instance. Returns error string, empty on success.
    std::string stop(const std::string& name);

    // Fill blob with status of one instance. Returns false if not found.
    bool status(const std::string& name, struct blob_buf* b) const;

    // Fill blob with list of all instances.
    void list(struct blob_buf* b) const;

    // Returns true if the named instance is in IDLE or ERROR state (or not found).
    bool is_idle(const std::string& name) const;

    // Stop all instances (called on daemon shutdown).
    void stop_all();

    // Update state from worker thread (thread-safe).
    void set_state(const std::string& name, VpnState s, const std::string& event = {});
    void set_client(const std::string& name, void* client_ptr);
    void set_stats(const std::string& name, uint64_t in, uint64_t out);

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<VpnRuntime>> instances_;

    std::shared_ptr<VpnRuntime> get_locked(const std::string& name) const;
};

// ============================================================================
// UbusInterface — registers "openvpn3" ubus object
// ============================================================================

class UbusInterface {
public:
    explicit UbusInterface(VpnManager& mgr) : mgr_(mgr) {}
    ~UbusInterface();

    // Connect to ubusd and register the "openvpn3" object.
    // Must be called before uloop_run().
    bool init(const std::string& socket_path = {});

    void shutdown();

private:
    VpnManager&           mgr_;
    struct ubus_context*  ctx_{ nullptr };

    // ubus object + method table (static, C linkage)
    static struct ubus_object        s_obj_;
    static struct ubus_object_type   s_obj_type_;
    static struct ubus_method        s_methods_[];

    // Method handlers
    static int handle_start(struct ubus_context*, struct ubus_object*,
                             struct ubus_request_data*, const char*, struct blob_attr*);
    static int handle_stop(struct ubus_context*, struct ubus_object*,
                            struct ubus_request_data*, const char*, struct blob_attr*);
    static int handle_status(struct ubus_context*, struct ubus_object*,
                              struct ubus_request_data*, const char*, struct blob_attr*);
    static int handle_list(struct ubus_context*, struct ubus_object*,
                            struct ubus_request_data*, const char*, struct blob_attr*);
    static int handle_import(struct ubus_context*, struct ubus_object*,
                              struct ubus_request_data*, const char*, struct blob_attr*);

    // Back-pointer so static handlers can reach the manager
    static UbusInterface* s_instance_;
};

} // namespace ovpn3_openwrt
