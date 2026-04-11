// tun_openwrt.cpp — TunBuilderBase implementation for OpenWrt/Linux
//
// All tun_builder_set_*/add_* methods are handled by TunBuilderCapture
// (the base class), which records them as a configuration snapshot.
//
// tun_builder_establish() feeds that snapshot into TunLinuxSetup::Setup,
// which opens /dev/net/tun, runs ioctl TUNSETIFF, and applies addresses
// and routes via SITNL (rtnetlink).  No iproute2 external process needed.
//
// tun_builder_teardown() calls setup_->destroy() which reverses every
// add_cmds action that establish() applied.

// Log macro setup: must come before any openvpn/ headers that use OPENVPN_LOG.
// This translation unit does not include ovpncli.cpp, so we use the simple
// stdout-based macros.  main.cpp uses the thread-safe LogBaseSimple instead.
#include <openvpn/log/logsimple.hpp>

#include "tun_openwrt.hpp"

#include <iostream>
#include <sstream>

namespace ovpn3_openwrt {

OpenWrtTunBuilder::OpenWrtTunBuilder()
    : setup_(new TunSetup())
{}

OpenWrtTunBuilder::~OpenWrtTunBuilder()
{
    if (tun_fd_ >= 0) {
        ::close(tun_fd_);
        tun_fd_ = -1;
    }
}

int OpenWrtTunBuilder::tun_builder_establish()
{
    // Config for the setup object:
    //   dev_name = "tun%d" lets the kernel assign the next free tun index.
    //   layer    = OSI_LAYER_3 (standard TUN, not TAP).
    TunSetup::Config conf;
    conf.dev_name = "tun%d";
    conf.layer    = openvpn::Layer(openvpn::Layer::OSI_LAYER_3);

    std::ostringstream os;
    try {
        // 'this' IS a TunBuilderCapture — pass it directly.
        tun_fd_ = setup_->establish(*this, &conf, nullptr, os);
    }
    catch (const std::exception& e) {
        std::cerr << "[tun] establish failed: " << e.what() << '\n'
                  << os.str();
        tun_fd_ = -1;
        return -1;
    }

    const std::string log = os.str();
    if (!log.empty())
        std::cout << "[tun] " << log;

    std::cout << "[tun] established fd=" << tun_fd_ << '\n';
    return tun_fd_;
}

void OpenWrtTunBuilder::tun_builder_teardown(bool /*disconnect*/)
{
    std::ostringstream os;
    if (setup_)
        setup_->destroy(os);

    const std::string log = os.str();
    if (!log.empty())
        std::cout << "[tun] teardown: " << log;

    if (tun_fd_ >= 0) {
        ::close(tun_fd_);
        tun_fd_ = -1;
    }
}

bool OpenWrtTunBuilder::add_bypass_route(const std::string& address, bool ipv6)
{
    std::ostringstream os;
    const bool ok = setup_->add_bypass_route(address, ipv6, os);
    const std::string log = os.str();
    if (!log.empty())
        std::cout << "[tun] bypass: " << log;
    return ok;
}

} // namespace ovpn3_openwrt
