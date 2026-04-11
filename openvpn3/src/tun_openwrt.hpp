// tun_openwrt.hpp — TunBuilderBase implementation for OpenWrt/Linux
//
// Inherits TunBuilderCapture to accumulate all tun_builder_* calls, then
// delegates the actual kernel setup to TunLinuxSetup::Setup<TunNetlink::TunMethods>.
// This reuses the SDK's proven ioctl + rtnetlink code path (SITNL backend).
//
// socket_protect() uses the HOST_ROUTE strategy: add a /32 (or /128) host
// route to the VPN server via the original gateway so the control socket
// never loops through tun0.
//
// Reference: openvpn3/openvpn/tun/builder/capture.hpp → TunBuilderCapture
// Reference: openvpn3/openvpn/tun/linux/client/tunsetup.hpp → TunLinuxSetup::Setup

#pragma once

// tunnetlink.hpp defines TunNetlink::TunMethods (SITNL-backed route/address ops).
// tunsetup.hpp defines TunLinuxSetup::Setup<TUNMETHODS> and pulls in TunBuilderCapture.
#include <openvpn/tun/linux/client/tunnetlink.hpp>
#include <openvpn/tun/linux/client/tunsetup.hpp>
#include <openvpn/tun/builder/capture.hpp>
#include <openvpn/common/rc.hpp>

#include <sstream>
#include <string>

namespace ovpn3_openwrt {

// OpenWrtTunBuilder accumulates tun_builder_* config via TunBuilderCapture,
// then executes it using TunLinuxSetup::Setup on tun_builder_establish().
class OpenWrtTunBuilder : public openvpn::TunBuilderCapture
{
public:
    OpenWrtTunBuilder();
    ~OpenWrtTunBuilder() override;

    // Override establish() — all other tun_builder_* methods are inherited
    // from TunBuilderCapture which records them into *this.
    int tun_builder_establish() override;

    // teardown: destroy routes / addresses added by establish()
    void tun_builder_teardown(bool disconnect) override;

    // socket_protect: add host route to VPN server via original gateway
    // (prevents the control socket from routing through tun0)
    bool add_bypass_route(const std::string& address, bool ipv6);

private:
    using TunSetup = openvpn::TunLinuxSetup::Setup<openvpn::TunNetlink::TunMethods>;

    TunSetup::Ptr setup_;   // owns the ActionList for teardown
    int tun_fd_{ -1 };
};

} // namespace ovpn3_openwrt
