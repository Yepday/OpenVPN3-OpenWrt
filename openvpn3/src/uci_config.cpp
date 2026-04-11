// uci_config.cpp — Stage 5: libuci integration
//
// Reads /etc/config/openvpn3 using the libuci C API.
//
// UCI schema:
//   config vpn 'myvpn'
//       option enabled    '1'
//       option config     '/etc/openvpn3/myvpn.ovpn'
//       option dev        'tun0'      # optional
//       option log_level  '3'         # optional, default 3

#include "uci_config.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>

extern "C" {
#include <uci.h>
}

namespace ovpn3_openwrt {

// ============================================================================
// Helpers
// ============================================================================

static const char* uci_option_str(struct uci_context* ctx, struct uci_section* s, const char* key)
{
    struct uci_option* o = uci_lookup_option(ctx, s, key);
    if (!o || o->type != UCI_TYPE_STRING)
        return nullptr;
    return o->v.string;
}

// ============================================================================
// UciConfig::load
// ============================================================================

bool UciConfig::load()
{
    instances_.clear();

    struct uci_context* ctx = uci_alloc_context();
    if (!ctx) {
        std::cerr << "uci_alloc_context failed\n";
        return false;
    }

    struct uci_package* pkg = nullptr;
    if (uci_load(ctx, "openvpn3", &pkg) != UCI_OK) {
        std::cerr << "uci_load(openvpn3) failed — /etc/config/openvpn3 missing?\n";
        uci_free_context(ctx);
        return false;
    }

    struct uci_element* e;
    uci_foreach_element(&pkg->sections, e) {
        struct uci_section* s = uci_to_section(e);
        if (strcmp(s->type, "vpn") != 0)
            continue;

        VpnInstance inst;
        inst.name = s->e.name ? s->e.name : "";

        const char* enabled = uci_option_str(ctx, s, "enabled");
        inst.enabled = enabled && (strcmp(enabled, "1") == 0 || strcmp(enabled, "true") == 0);

        const char* config = uci_option_str(ctx, s, "config");
        inst.config_path = config ? config : "";

        const char* dev = uci_option_str(ctx, s, "dev");
        inst.dev = dev ? dev : "";

        const char* log_level = uci_option_str(ctx, s, "log_level");
        if (log_level)
            inst.log_level = std::stoi(log_level);

        instances_.push_back(std::move(inst));
    }

    uci_unload(ctx, pkg);
    uci_free_context(ctx);
    return true;
}

// ============================================================================
// UciConfig::read_ovpn
// ============================================================================

std::string UciConfig::read_ovpn(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace ovpn3_openwrt
