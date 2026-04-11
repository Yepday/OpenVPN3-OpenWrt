// uci_config.hpp — Read OpenWrt UCI config and .ovpn profile files
//
// UCI config location: /etc/config/openvpn3
//
// UCI schema (per instance):
//   config vpn '<name>'
//       option enabled    '1'
//       option config     '/etc/openvpn3/<name>.ovpn'
//       option dev        'tun0'           # optional, auto-assigned if absent
//       option log_level  '3'
//
// .ovpn import: read raw file, pass content string to
//   ClientAPI::OpenVPNClient::eval_config()  (openvpn3 parses it natively)

#pragma once

#include <string>
#include <vector>

namespace ovpn3_openwrt {

struct VpnInstance {
    std::string name;
    bool enabled{ false };
    std::string config_path;   // path to .ovpn file
    std::string dev;           // tun device name, empty = auto
    int log_level{ 3 };
};

class UciConfig
{
public:
    // Load all instances from /etc/config/openvpn3
    bool load();

    // Load a single .ovpn file and return its content as a string
    // (passed directly to ClientAPI::OpenVPNClient::eval_config)
    static std::string read_ovpn(const std::string& path);

    const std::vector<VpnInstance>& instances() const { return instances_; }

private:
    std::vector<VpnInstance> instances_;
    // TODO: uses libuci C API
};

} // namespace ovpn3_openwrt
