# openvpn3-openwrt

OpenVPN 3 client daemon for OpenWrt. Provides procd service management, ubus control interface, and UCI-based configuration.

Tested on OpenWrt 23.05.x (x86_64 QEMU). Target hardware: rk3399 (aarch64).

## Directory structure

```
openvpn3-openwrt/
├── openvpn3/          # OpenWrt package (put this in package/ of your SDK)
│   ├── Makefile
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── tun_openwrt.cpp / .hpp
│   │   ├── ubus_interface.cpp / .hpp
│   │   └── uci_config.cpp / .hpp
│   └── files/
│       ├── openvpn3.init    # procd init script
│       └── openvpn3.config  # UCI default config
└── libasio/           # libasio OpenWrt package (build dependency)
    └── Makefile
```

## Build

### Prerequisites

- OpenWrt SDK 23.05.x (x86_64 or aarch64)
- git (to clone openvpn3-core at build time)

### Steps

```bash
# 1. Link packages into SDK
cd <SDK_DIR>
ln -s /path/to/openvpn3-openwrt/libasio  package/libasio
ln -s /path/to/openvpn3-openwrt/openvpn3 package/openvpn3

# 2. Update feeds and install dependencies
./scripts/feeds update -a
./scripts/feeds install -a

# 3. Build libasio first (build dependency)
make package/libasio/compile V=s

# 4. Build the package
make package/openvpn3/compile V=s

# Output: bin/packages/<arch>/base/openvpn3-openwrt_*.ipk
```

The build will automatically clone openvpn3-core from https://github.com/OpenVPN/openvpn3 (master branch) into the SDK build directory on first run.

### Install on device

```bash
# Transfer to device
scp bin/packages/x86_64/base/openvpn3-openwrt_*.ipk root@<device>:/tmp/

# Install
opkg install /tmp/openvpn3-openwrt_*.ipk
```

For QEMU testing without opkg (manual unpack):

```bash
cd /tmp
tar xzf openvpn3-openwrt_*.ipk
tar xzf data.tar.gz -C /
```

## Usage

### Via UCI + procd (recommended)

```bash
# Edit /etc/config/openvpn3 — set server/port/proto for your VPN
uci set openvpn3.client.server='your.vpn.server'
uci set openvpn3.client.port='1194'
uci set openvpn3.client.proto='udp'
uci set openvpn3.client.enabled='1'
uci commit openvpn3

# Start
/etc/init.d/openvpn3 enable
/etc/init.d/openvpn3 start
```

### Via ubus

```bash
# Import a .ovpn profile
ubus call openvpn3 import '{"path":"/tmp/client.ovpn","name":"client"}'

# Start / stop / status
ubus call openvpn3 start  '{"instance":"client"}'
ubus call openvpn3 status '{"instance":"client"}'
ubus call openvpn3 stop   '{"instance":"client"}'

# List all instances
ubus call openvpn3 list
```

### Direct (debug)

```bash
openvpn3d /tmp/client.ovpn
```

## UCI config schema

`/etc/config/openvpn3`:

```
config vpn 'client'
    option enabled    '0'
    option server     ''
    option port       '1194'
    option proto      'udp'
    option config     ''        # path to .ovpn file (optional)
    option log_level  '3'
```

## Known issues

- On reconnect, a harmless `sitnl_send: rtnl: generic error: File exists (-17)` warning appears — the bypass route already exists from the previous connection. The connection still establishes successfully.

## License

AGPL-3.0-only. See [openvpn3-core license](https://github.com/OpenVPN/openvpn3/blob/master/LICENSE.md).
