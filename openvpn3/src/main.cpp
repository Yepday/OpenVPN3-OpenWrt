// main.cpp — openvpn3d daemon  (Stage 4)
//
// Stage 1: eval_config smoke test
// Stage 2: full connect() with thread, signal handling
// Stage 3: daemon mode — uloop + ubus, multi-instance via VpnManager
//
// Usage:
//   openvpn3d                          # daemon mode (ubus-driven)
//   openvpn3d <config.ovpn> [u p]     # legacy single-shot mode (for testing)
//
// Daemon mode:
//   uloop_init()
//   UbusInterface::init()   → registers "openvpn3" ubus object
//   uloop_run()             → blocks; ubus calls drive VPN lifecycle
//
// Signal handling (daemon mode):
//   SIGTERM / SIGINT  → uloop_end()  (clean shutdown)
//   SIGHUP            → ignored (reconnect via ubus stop+start)

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <time.h>

#include <openvpn/common/platform.hpp>

#if defined(OPENVPN_PLATFORM_LINUX) && !defined(OPENVPN_USE_IPROUTE2) && !defined(OPENVPN_USE_SITNL)
#define OPENVPN_USE_SITNL
#endif

#define OPENVPN_LOG_GLOBAL
#include <openvpn/log/logbasesimple.hpp>

// ovpncli.cpp is included exactly once — in ubus_interface.cpp.
// main.cpp only needs the ClientAPI types via the header.
#include <client/ovpncli.hpp>

#include <openvpn/common/exception.hpp>
#include <openvpn/ssl/sslchoose.hpp>

extern "C" {
#include <libubox/uloop.h>
}

#include "ubus_interface.hpp"
#include "uci_config.hpp"

using namespace openvpn;

// ============================================================================
// Daemon mode signal handling
// ============================================================================

static void daemon_signal_handler(int /*sig*/)
{
    uloop_end();
}

static void install_daemon_signals()
{
    struct sigaction sa{};
    sa.sa_handler = daemon_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);
    // SIGHUP: ignore (reconnect via ubus stop+start)
    sa.sa_handler = SIG_IGN;
    sigaction(SIGHUP, &sa, nullptr);
}

// ============================================================================
// Legacy single-shot mode — delegates to VpnManager (same code path as daemon)
// ============================================================================

static int run_legacy(int argc, char* argv[])
{
    std::string config_path = argv[1];
    std::string username    = (argc >= 3) ? argv[2] : "";
    std::string password    = (argc >= 4) ? argv[3] : "";

    if (!username.empty() && password.empty()) {
        std::cout << "Password: ";
        std::cin >> password;
    }

    // Use VpnManager so the same ManagedClient code path is exercised.
    ovpn3_openwrt::VpnManager mgr;
    std::string err = mgr.start("legacy", config_path, username, password);
    if (!err.empty()) {
        std::cerr << "start failed: " << err << '\n';
        return 1;
    }

    // Install signals to stop the instance
    static ovpn3_openwrt::VpnManager* g_mgr = &mgr;
    struct sigaction sa{};
    sa.sa_handler = [](int) { g_mgr->stop("legacy"); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    // Wait until the worker thread finishes (state goes IDLE or ERROR)
    while (!mgr.is_idle("legacy")) {
        struct timespec ts{ 0, 200'000'000 };
        nanosleep(&ts, nullptr);
    }
    return 0;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[])
{
    std::cout << "openvpn3d\n"
              << "  platform:  " << ClientAPI::OpenVPNClientHelper::platform()  << '\n'
              << "  copyright: " << ClientAPI::OpenVPNClientHelper::copyright() << '\n';

    // Legacy single-shot mode: openvpn3d <config.ovpn> [user pass]
    if (argc >= 2)
        return run_legacy(argc, argv);

    // Daemon mode
    std::cout << "Starting daemon mode (ubus-driven)\n";

    uloop_init();
    install_daemon_signals();

    ovpn3_openwrt::VpnManager mgr;
    ovpn3_openwrt::UbusInterface iface(mgr);

    if (!iface.init()) {
        std::cerr << "Failed to connect to ubusd — is ubus running?\n";
        uloop_done();
        return 1;
    }

    // Auto-start all enabled UCI instances
    {
        ovpn3_openwrt::UciConfig uci;
        if (uci.load()) {
            std::cout << "UCI: loaded " << uci.instances().size() << " instance(s)\n";
            for (const auto& inst : uci.instances()) {
                std::cout << "UCI: instance '" << inst.name
                          << "' enabled=" << inst.enabled
                          << " config='" << inst.config_path << "'\n";
                if (!inst.enabled) continue;
                std::cout << "UCI: auto-starting instance '" << inst.name << "'\n";
                std::string err = mgr.start(inst.name, inst.config_path, "", "");
                if (!err.empty())
                    std::cerr << "UCI: start '" << inst.name << "' failed: " << err << '\n';
            }
        } else {
            std::cerr << "UCI: load failed\n";
        }
    }

    uloop_run();   // blocks until SIGTERM/SIGINT or uloop_end()

    std::cout << "Shutting down...\n";
    mgr.stop_all();
    iface.shutdown();
    uloop_done();

    return 0;
}
