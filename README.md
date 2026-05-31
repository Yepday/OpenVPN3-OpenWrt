# openvpn3-openwrt

适用于 OpenWrt 的 OpenVPN 3 客户端守护进程，提供 procd 服务管理、ubus 控制接口和基于 UCI 的配置。

已在 OpenWrt 23.05.x（x86_64 QEMU）和 rk3399（aarch64）实体机上验证通过。

## 目录结构

```
openvpn3-openwrt/
├── openvpn3/          # OpenWrt 软件包（放入 SDK 的 package/ 目录）
│   ├── Makefile
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── ubus_interface.cpp / .hpp
│   │   └── uci_config.cpp / .hpp
│   └── files/
│       ├── openvpn3.init    # procd 初始化脚本
│       └── openvpn3.config  # UCI 默认配置
└── libasio/           # libasio OpenWrt 软件包（编译依赖）
    └── Makefile
```

## 编译

### 前置条件

- OpenWrt SDK 23.05.x（x86_64 或 aarch64）
- git（编译时用于克隆 openvpn3-core）

> **rk3399 / Orange Pi 4 用户**：可直接使用作者维护的 OpenWrt 构建树，已针对 RK3399 平台适配：
> https://github.com/Yepday/openwrt-orangepi4

### 步骤

```bash
# 1. 在任意位置 clone 本项目（libasio 已包含在项目内）
git clone https://github.com/Yepday/OpenVPN3-OpenWrt
cd OpenVPN3-OpenWrt
REPO_DIR=$(pwd)

# 2. 将软件包软链接到 SDK（不要直接在 SDK 内 clone，避免被 feeds 污染）
cd <SDK_DIR>
ln -s $REPO_DIR/libasio  package/libasio
ln -s $REPO_DIR/openvpn3 package/openvpn3

# 2. 更新 feeds 并安装依赖
./scripts/feeds update -a
./scripts/feeds install -a

# 3. 先编译 libasio（编译依赖）
make package/libasio/compile V=s

# 4. 编译软件包
make package/openvpn3/compile V=s

# 输出：bin/packages/<arch>/base/openvpn3-openwrt_*.ipk
```

首次运行时，编译过程会自动从 https://github.com/OpenVPN/openvpn3（master 分支）克隆 openvpn3-core 到 SDK 编译目录。

### 安装到设备

```bash
# 传输到设备
scp bin/packages/x86_64/base/openvpn3-openwrt_*.ipk root@<device>:/tmp/

# 安装
opkg install /tmp/openvpn3-openwrt_*.ipk
```

QEMU 测试时不使用 opkg（手动解包）：

> **注意**：x86_64 SDK 编译的包在官方预编译镜像上可能因 libc/内核版本差异导致 `opkg` 失败，手动解包可绕过此问题。rk3399（aarch64 full tree 编译）上 `opkg install` 正常工作。

```bash
cd /tmp
tar xzf openvpn3-openwrt_*.ipk
tar xzf data.tar.gz -C /
```

## 使用方法

### 通过 UCI + procd（推荐）

```bash
# 将 .ovpn 文件放到 /etc/openvpn3/
cp client.ovpn /etc/openvpn3/client.ovpn

# 编辑 /etc/config/openvpn3 — 指定 .ovpn 文件路径并启用
uci set openvpn3.client=vpn
uci set openvpn3.client.enabled='1'
uci set openvpn3.client.config='/etc/openvpn3/client.ovpn'
uci commit openvpn3

# 启动
/etc/init.d/openvpn3 enable
/etc/init.d/openvpn3 start
```

### 通过 ubus

```bash
# 导入 .ovpn 配置文件
ubus call openvpn3 import '{"path":"/tmp/client.ovpn","name":"client"}'

# 启动 / 停止 / 查看状态
ubus call openvpn3 start  '{"instance":"client"}'
ubus call openvpn3 status '{"instance":"client"}'
ubus call openvpn3 stop   '{"instance":"client"}'

# 列出所有实例
ubus call openvpn3 list
```

### 直接运行（调试）

```bash
openvpn3d /tmp/client.ovpn
```

## UCI 配置结构

`/etc/config/openvpn3`：

```
config vpn 'client'
    option enabled    '1'          # 1=开机自动启动，0=禁用
    option config     '/etc/openvpn3/client.ovpn'  # .ovpn 文件路径（必填）
    option dev        'tun0'       # 可选：强制指定 tun 接口名
    option log_level  '3'          # 可选：0=fatal 1=error 2=warn 3=info 4=debug
```

server、port、proto 等连接参数在 `.ovpn` 文件内配置，UCI 不管理这些字段。

## 已知问题

- 重连时会出现无害的 `sitnl_send: rtnl: generic error: File exists (-17)` 警告——绕过路由在上次连接时已存在。连接仍可正常建立。

## 许可证

AGPL-3.0-only。参见 [openvpn3-core 许可证](https://github.com/OpenVPN/openvpn3/blob/master/LICENSE.md)。

---

# openvpn3-openwrt

OpenVPN 3 client daemon for OpenWrt. Provides procd service management, ubus control interface, and UCI-based configuration.

Tested on OpenWrt 23.05.x (x86_64 QEMU) and rk3399 (aarch64) hardware.

## Directory structure

```
openvpn3-openwrt/
├── openvpn3/          # OpenWrt package (put this in package/ of your SDK)
│   ├── Makefile
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
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

> **rk3399 / Orange Pi 4 users**: You can use the author's OpenWrt build tree, already adapted for the RK3399 platform:
> https://github.com/Yepday/openwrt-orangepi4

### Steps

```bash
# 1. Clone this repo anywhere (libasio is included inside)
git clone https://github.com/Yepday/OpenVPN3-OpenWrt
cd OpenVPN3-OpenWrt
REPO_DIR=$(pwd)

# 2. Symlink packages into the SDK (don't clone directly inside SDK — feeds may overwrite it)
cd <SDK_DIR>
ln -s $REPO_DIR/libasio  package/libasio
ln -s $REPO_DIR/openvpn3 package/openvpn3

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

> **Note**: x86_64 SDK packages may fail `opkg install` on pre-built official images due to libc/kernel version mismatch. Manual unpack works around this. On rk3399 (aarch64 full tree build), `opkg install` works normally.

```bash
cd /tmp
tar xzf openvpn3-openwrt_*.ipk
tar xzf data.tar.gz -C /
```

## Usage

### Via UCI + procd (recommended)

```bash
# Place the .ovpn file in /etc/openvpn3/
cp client.ovpn /etc/openvpn3/client.ovpn

# Edit /etc/config/openvpn3 — point to the .ovpn file and enable
uci set openvpn3.client=vpn
uci set openvpn3.client.enabled='1'
uci set openvpn3.client.config='/etc/openvpn3/client.ovpn'
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
    option enabled    '1'          # 1=auto-start on boot, 0=disabled
    option config     '/etc/openvpn3/client.ovpn'  # path to .ovpn file (required)
    option dev        'tun0'       # optional: force tun interface name
    option log_level  '3'          # optional: 0=fatal 1=error 2=warn 3=info 4=debug
```

Connection parameters (server, port, proto, etc.) are configured inside the `.ovpn` file — UCI does not manage these fields.

## Known issues

- On reconnect, a harmless `sitnl_send: rtnl: generic error: File exists (-17)` warning appears — the bypass route already exists from the previous connection. The connection still establishes successfully.

## License

AGPL-3.0-only. See [openvpn3-core license](https://github.com/OpenVPN/openvpn3/blob/master/LICENSE.md).
