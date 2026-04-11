# 架构设计

> 本文档记录 openvpn3-openwrt 的整体设计决策，持续更新。

---

## 项目定位

用 openvpn3-core（C++ header-only 库）作为 SDK，在其上构建一个符合 OpenWrt 工程规范的 VPN 客户端守护进程。

```
openvpn3-core (SDK，只读引用)
        │
        ▼
openvpn3d (我们的守护进程)
        ├── TunBuilderBase 实现  → 管理 TUN 设备
        ├── ubus 接口            → 控制 & 状态查询
        ├── UCI 配置读取          → /etc/config/openvpn3
        └── procd 集成           → 生命周期管理
```

---

## 目录结构（Package Feed）

```
openvpn3-openwrt/              ← git repo 根，同时是 OpenWrt package feed 根
├── docs/                      ← 项目文档（本目录）
├── libasio/                   ← OpenWrt 包：libasio（依赖，需先 PR 到 openwrt/packages）
│   └── Makefile
└── openvpn3/                  ← OpenWrt 包：主体
    ├── Makefile               ← OpenWrt 包描述（编译、安装规则）
    ├── CMakeLists.txt         ← 我们代码的构建系统
    ├── src/                   ← 我们编写的 C++ 源码
    │   ├── main.cpp
    │   ├── tun_openwrt.hpp/cpp
    │   ├── ubus_interface.hpp/cpp
    │   ├── uci_config.hpp/cpp
    │   └── client.hpp/cpp     ← OpenVPNClient 子类（TODO）
    └── files/                 ← OpenWrt 安装附带文件
        ├── openvpn3.init      ← procd init 脚本
        └── openvpn3.config    ← UCI 默认配置
```

---

## 核心组件设计

### 1. TunBuilderBase 实现（`tun_openwrt`）

openvpn3-core 在建立 VPN 连接时回调 `TunBuilderBase` 的虚函数，告知 IP 地址、路由、DNS 等配置，最后调用 `tun_builder_establish()` 获取 TUN 文件描述符。

**我们的实现方式**：
- 用标准 Linux ioctl（`TUNSETIFF`）打开 `/dev/net/tun`
- 用 rtnetlink（或 `ip` 命令）设置地址和路由
- 不依赖 NetworkManager 或 netifd proto handler（初版）
- DNS：写入 `/tmp/resolv.conf.d/openvpn3-<instance>`，dnsmasq 自动读取

**后续增强**：接入 netifd proto handler，让 VPN 接口在 `ip link` / LuCI 中正确显示。

### 2. procd 集成（`openvpn3.init`）

- 每个 UCI `config vpn` 块对应一个 procd instance
- `respawn` 自动重启（断线重连由 openvpn3-core 内部处理，procd 作为最外层兜底）
- `procd_set_param file` 监听 .ovpn 文件变化自动重载

### 3. ubus 接口（`ubus_interface`）

注册 ubus 对象 `openvpn3`，提供以下方法：

| 方法 | 参数 | 返回 |
|------|------|------|
| `list` | — | 所有实例名称及运行状态 |
| `start` | `instance: string` | ok / error |
| `stop` | `instance: string` | ok / error |
| `status` | `instance: string` | state, bytes_in, bytes_out, uptime, server_ip |
| `import` | `path: string` | ok / error（将 .ovpn 写入 UCI） |

主线程运行 `uloop_run()`，VPN 连接在独立线程中运行（openvpn3-core 的 `client.connect()` 阻塞调用）。

### 4. UCI 配置（`uci_config`）

用 `libuci` C API 读取 `/etc/config/openvpn3`。配置内容仅是元数据（启用开关、.ovpn 文件路径），VPN 参数本身保留在 .ovpn 文件中（openvpn3-core 的 `options/` 模块直接解析标准 .ovpn 格式）。

---

## 关键技术决策记录

| 决策 | 选择 | 备选 | 理由 |
|------|------|------|------|
| IPC 机制 | ubus | D-Bus, unix socket | OpenWrt 标准，无额外依赖 |
| 进程管理 | procd | 手写 daemon | OpenWrt 标准，自动 respawn |
| TUN 配置 | 直接 ioctl/rtnetlink | netifd proto handler | 初版简单可靠；proto handler 作后续增强 |
| DNS 处理 | resolv.conf.d | 调 dnsmasq ubus API | 简单，dnsmasq 默认监听此目录 |
| 加密后端 | OpenSSL | mbedTLS | OpenWrt 已有，体积更小可后续考虑 mbedTLS |
| DCO 内核模块 | 不引入（初版） | 引入 | 减少复杂度；DCO 需单独内核模块打包 |

---

## 与 openvpn3-linux 的对比

| 功能 | openvpn3-linux | 本项目 |
|------|---------------|--------|
| IPC | D-Bus | ubus |
| 进程管理 | systemd | procd |
| 网络配置 | NetworkManager/networkd | 直接 ioctl，可选 netifd |
| 配置格式 | 自定义 JSON/DBus | UCI + .ovpn |
| 多实例 | 多进程 + session manager | procd multi-instance |
| Python 前端 | 有 | 无（ubus call 即可） |
