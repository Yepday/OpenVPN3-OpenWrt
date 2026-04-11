# openvpn3-openwrt 源码全解

本文逐文件讲解 `openvpn3-openwrt/` 下的每一个源文件，说明它是什么、为什么这样写、和其他文件的关系。

---

## 目录结构总览

```
openvpn3-openwrt/
├── libasio/
│   └── Makefile              # 把 Asio 头文件打成 OpenWrt 包
└── openvpn3/
    ├── Makefile              # 主包：编译 + 打包 openvpn3d
    ├── CMakeLists.txt        # CMake 构建脚本（被 OpenWrt Makefile 调用）
    ├── files/
    │   ├── openvpn3.init     # procd 启动脚本（开机自启）
    │   └── openvpn3.config   # UCI 配置模板（装到 /etc/config/openvpn3）
    └── src/
        ├── main.cpp          # 程序入口，daemon 模式 + legacy 测试模式
        ├── tun_openwrt.hpp   # TUN 设备管理：接口声明
        ├── tun_openwrt.cpp   # TUN 设备管理：实现
        ├── ubus_interface.hpp # ubus 控制接口：接口声明
        ├── ubus_interface.cpp # ubus 控制接口：实现（含 VpnManager）
        ├── uci_config.hpp    # UCI 配置读取：接口声明
        └── uci_config.cpp    # UCI 配置读取：实现
```

---

## 一、构建系统

### 1.1 libasio/Makefile

**它是什么**

Asio 是一个 C++ 异步 I/O 库，OpenVPN3 Core SDK 用它驱动整个事件循环（网络收发、定时器、信号）。Asio 是纯头文件库，不需要编译，只需要把 `.hpp` 文件放到编译器能找到的地方。

这个 Makefile 的唯一工作：从网上下载 Asio 源码包，把头文件安装到 OpenWrt SDK 的 `staging_dir/usr/include/`，让后续编译 openvpn3d 时能 `#include <asio.hpp>`。

**关键字段解读**

```makefile
PKG_SOURCE_URL:=https://sourceforge.net/projects/asio/files/...
PKG_HASH:=9f12cef0...        # SHA256，防止下载到损坏或被篡改的包
```

```makefile
define Build/Compile
endef                        # 空的！头文件库不需要编译
```

```makefile
define Build/InstallDev      # InstallDev = 安装到 staging_dir（给其他包编译时用）
    $(CP) $(PKG_BUILD_DIR)/include/asio.hpp $(1)/usr/include/
    $(CP) -r $(PKG_BUILD_DIR)/include/asio  $(1)/usr/include/
endef

define Package/libasio/install  # install = 安装到目标根文件系统（装到路由器上）
    # 同上
endef
```

`InstallDev` 和 `install` 的区别：
- `InstallDev` 装到宿主机的 `staging_dir`，编译其他包时用
- `install` 装到目标设备的根文件系统，路由器运行时用

---

### 1.2 openvpn3/Makefile

**它是什么**

OpenWrt 的包管理系统（opkg）要求每个包都有一个标准格式的 `Makefile`。这个文件告诉 SDK：
1. 包叫什么名字、版本号是多少
2. 编译时依赖哪些库的头文件（`PKG_BUILD_DEPENDS`）
3. 运行时依赖哪些库的 `.so` 文件（`DEPENDS`）
4. 怎么准备源码（`Build/Prepare`）
5. 怎么编译（委托给 CMake）
6. 编译完把哪些文件装到哪里（`Package/install`）

**逐段解读**

```makefile
include $(TOPDIR)/rules.mk          # OpenWrt 构建系统的基础规则
include $(INCLUDE_DIR)/package.mk   # 提供 define Package/... 等宏
include $(INCLUDE_DIR)/cmake.mk     # 提供 CMake 集成（自动调用 cmake + make）
```

```makefile
PKG_BUILD_DEPENDS:=libasio openssl jsoncpp libfmt liblz4 xxhash
```
编译时依赖——只需要头文件，不需要 `.so`。`libasio` 就是上面那个包。

```makefile
DEPENDS:=+libopenssl +liblz4 +jsoncpp +xxhash +libfmt +libstdcpp +kmod-tun \
         +libubus +libubox +libuci
```
运行时依赖——路由器上必须装这些包，openvpn3d 才能跑。`+` 前缀表示强依赖（opkg 会自动安装）。`kmod-tun` 是 TUN 内核模块，没它就没法创建 tun 设备。

```makefile
define Build/Prepare
    mkdir -p $(PKG_BUILD_DIR)
    ln -sfn $(CURDIR)/src $(PKG_BUILD_DIR)/src
    ln -sfn $(CURDIR)/CMakeLists.txt $(PKG_BUILD_DIR)/CMakeLists.txt
endef
```
OpenWrt 通常从网上下载源码包解压到 `PKG_BUILD_DIR`。我们用本地源码，所以用符号链接把 `src/` 和 `CMakeLists.txt` 链接过去，让 CMake 能找到它们。

```makefile
CMAKE_OPTIONS += \
    -DOPENVPN3_CORE_DIR=$(CURDIR)/../../openvpn3
```
告诉 CMake：OpenVPN3 Core SDK 的头文件在哪里（相对于这个 Makefile 的位置往上两级）。

```makefile
define Package/openvpn3-openwrt/install
    $(INSTALL_DIR) $(1)/usr/sbin
    $(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/sbin/openvpn3d $(1)/usr/sbin/
    $(INSTALL_DIR) $(1)/etc/init.d
    $(INSTALL_BIN) $(CURDIR)/files/openvpn3.init $(1)/etc/init.d/openvpn3
    $(INSTALL_DIR) $(1)/etc/config
    $(INSTALL_CONF) $(CURDIR)/files/openvpn3.config $(1)/etc/config/openvpn3
    $(INSTALL_DIR) $(1)/etc/openvpn3
endef
```
`$(1)` 是目标根文件系统的路径。这段把四样东西装进去：
- `openvpn3d` 二进制 → `/usr/sbin/`
- init 脚本 → `/etc/init.d/openvpn3`（开机自启用）
- UCI 配置模板 → `/etc/config/openvpn3`
- 空目录 `/etc/openvpn3/`（存放 .ovpn 文件）

---

### 1.3 openvpn3/CMakeLists.txt

**它是什么**

CMake 是实际的编译配置。OpenWrt 的 `cmake.mk` 会自动调用 `cmake` 然后 `make`，所以这个文件就是标准 CMake 写法。

**关键部分**

```cmake
set(CMAKE_CXX_STANDARD 20)   # OpenVPN3 Core SDK 要求 C++20
```

```cmake
pkg_check_modules(UBUS REQUIRED ubus)
pkg_check_modules(UBOX REQUIRED libubox)
pkg_check_modules(UCI  REQUIRED uci)
```
用 `pkg-config` 找 ubus/ubox/uci 的头文件和库路径。OpenWrt SDK 的 `staging_dir` 里有这些包的 `.pc` 文件。

```cmake
target_compile_definitions(openvpn3d PRIVATE
    ASIO_STANDALONE      # 不用 Boost，用独立版 Asio
    USE_OPENSSL          # SSL 后端选 OpenSSL（sslchoose.hpp 根据这个宏选择）
    OPENVPN_PLATFORM_LINUX
    OPENVPN_USE_SITNL    # 路由管理用 rtnetlink（不用 iproute2 外部命令）
)
```
这几个宏控制 OpenVPN3 Core SDK 的编译行为，必须和实际环境匹配。

---

## 二、配置文件

### 2.1 files/openvpn3.config

OpenWrt 的配置系统叫 UCI（Unified Configuration Interface）。所有配置都存在 `/etc/config/` 下的文本文件里，格式统一。这个文件是装到路由器上的配置模板。

```
config vpn 'example'
    option enabled    '0'
    option config     '/etc/openvpn3/example.ovpn'
    # option dev      'tun0'
    # option log_level '3'
```

每个 `config vpn '<名字>'` 块代表一个 VPN 实例。字段含义：
- `enabled`：`1` 开机自启，`0` 不启动
- `config`：`.ovpn` 文件路径
- `dev`：可选，强制指定 tun 设备名（不填则内核自动分配 tun0/tun1...）
- `log_level`：0=只报致命错误，4=调试输出

用户要加一个 VPN，就在这个文件里加一个 `config vpn` 块，把 `.ovpn` 文件放到 `/etc/openvpn3/`，然后 `/etc/init.d/openvpn3 restart`。

---

### 2.2 files/openvpn3.init

procd 是 OpenWrt 的进程管理器（类似 systemd）。init 脚本告诉 procd 怎么启动、停止、监控 openvpn3d 进程。

```sh
#!/bin/sh /etc/rc.common
USE_PROCD=1
START=90    # 启动顺序：90（网络 START=20 之后）
STOP=10     # 停止顺序：10（最先停）
```

`/etc/rc.common` 是 OpenWrt 的 init 脚本框架，提供 `start`/`stop`/`restart` 等命令。`USE_PROCD=1` 表示用 procd API。

```sh
start_instance() {
    local cfg="$1"          # UCI section 名，如 "example"

    config_get_bool enabled "$cfg" enabled 0
    [ "$enabled" = "1" ] || return 0    # 没启用就跳过

    config_get config_path "$cfg" config ""
    [ -f "$config_path" ] || return 1   # .ovpn 文件不存在就报错

    procd_open_instance "$cfg"
    procd_set_param command "$PROG" \
        --instance "$cfg" \
        --config "$config_path" \
        --log-level "$log_level" \
        ${dev:+--dev "$dev"}            # dev 非空才加 --dev 参数
    procd_set_param respawn 3600 5 3    # 崩溃后自动重启：间隔3600s，超时5s，最多3次
    procd_set_param stderr 1            # stderr 输出到系统日志
    procd_set_param file "$config_path" # 监视这个文件，变化时触发重启
    procd_close_instance
}

start_service() {
    config_load "$CONFIG"               # 加载 /etc/config/openvpn3
    config_foreach start_instance vpn   # 对每个 'config vpn' 块调用 start_instance
}

service_triggers() {
    procd_add_reload_trigger "$CONFIG"  # UCI 配置变化时自动 reload
}
```

`respawn 3600 5 3` 的含义：如果进程在 3600 秒内崩溃超过 3 次，且每次存活不足 5 秒，则停止重启（认为配置有问题）。

---

## 三、C++ 源码

### 3.1 tun_openwrt.hpp / tun_openwrt.cpp

**它解决什么问题**

VPN 连接建立后，需要在系统里创建一个虚拟网卡（tun 设备），配置 IP 地址、路由，让流量走进 VPN 隧道。OpenVPN3 Core SDK 把这件事抽象成 `TunBuilderBase` 接口——SDK 负责协商出"应该配什么"，具体怎么配交给平台实现。

`OpenWrtTunBuilder` 就是这个平台实现。

**继承关系**

```
TunBuilderBase              <- SDK 定义的纯虚接口
    └── TunBuilderCapture   <- SDK 提供的"记录器"：把所有 tun_builder_* 调用存起来
            └── OpenWrtTunBuilder   <- 我们实现的：在 establish() 时把记录的配置执行出来
```

`TunBuilderCapture` 已经实现了所有 `tun_builder_set_*` / `tun_builder_add_*` 方法，把参数存到自己的字段里。我们只需要重写 `tun_builder_establish()`（真正创建设备）和 `tun_builder_teardown()`（清理）。

**tun_builder_establish() 做了什么**

```cpp
TunSetup::Config conf;
conf.dev_name = "tun%d";          // 让内核分配下一个空闲编号
conf.layer    = OSI_LAYER_3;      // TUN（三层），不是 TAP（二层）

tun_fd_ = setup_->establish(*this, &conf, nullptr, os);
```

`setup_->establish()` 内部：
1. 打开 `/dev/net/tun`
2. `ioctl(TUNSETIFF)` 创建 tun 设备
3. 通过 rtnetlink（SITNL）配置 IP 地址
4. 通过 rtnetlink 添加路由
5. 返回 tun 设备的文件描述符

返回的 fd 交给 SDK，SDK 用它读写 VPN 隧道里的 IP 包。

**add_bypass_route() 解决什么问题**

VPN 连接建立后，默认路由会走 tun0。但 VPN 控制连接（到服务器的 UDP/TCP socket）本身也要走网络——如果它也走 tun0，就会形成死循环。

解决方案：给 VPN 服务器的 IP 地址加一条精确的 /32 主机路由，强制走原来的物理网卡，绕过 tun0。

```cpp
bool OpenWrtTunBuilder::add_bypass_route(const std::string& address, bool ipv6)
{
    return setup_->add_bypass_route(address, ipv6, os);
    // 内部效果：ip route add <server_ip>/32 via <original_gw> dev eth0
}
```

---

### 3.2 uci_config.hpp / uci_config.cpp

**它是什么**

读取 `/etc/config/openvpn3`，把 UCI 配置转换成 C++ 结构体。

```cpp
struct VpnInstance {
    std::string name;         // UCI section 名
    bool enabled;
    std::string config_path;  // .ovpn 文件路径
    std::string dev;          // tun 设备名（可选）
    int log_level;
};
```

**libuci C API 用法**

```cpp
struct uci_context* ctx = uci_alloc_context();
uci_load(ctx, "openvpn3", &pkg);          // 加载 /etc/config/openvpn3

uci_foreach_element(&pkg->sections, e) {
    struct uci_section* s = uci_to_section(e);
    if (strcmp(s->type, "vpn") != 0) continue;  // 只处理 'config vpn' 块

    const char* val = uci_lookup_option(..., s, "enabled");
}

uci_unload(ctx, pkg);
uci_free_context(ctx);
```

`read_ovpn()` 是个工具函数，把 `.ovpn` 文件读成字符串，直接传给 `eval_config()`——OpenVPN3 SDK 原生解析 .ovpn 格式，不需要我们自己解析。

---

### 3.3 ubus_interface.hpp / ubus_interface.cpp

**它是什么**

ubus 是 OpenWrt 的进程间通信总线（类似 D-Bus，但更轻量）。这个模块把 openvpn3d 的控制接口暴露到 ubus 上，让用户可以用 `ubus call` 命令操作 VPN。

**架构**

```
uloop（主线程事件循环）
    └── UbusInterface（注册 "openvpn3" 对象）
            └── VpnManager（管理所有 VPN 实例）
                    ├── VpnRuntime "vpn1"  <- worker thread
                    ├── VpnRuntime "vpn2"  <- worker thread
                    └── ...
```

ubus 回调在主线程（uloop）里执行，VPN 连接在 worker thread 里跑（因为 `connect()` 是阻塞的）。`VpnManager::mu_` 保护所有共享状态。

**VpnManager**

```cpp
class VpnManager {
    std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<VpnRuntime>> instances_;
};
```

`start()` 的流程：
1. 检查实例是否已在运行
2. 读取 .ovpn 文件内容
3. 创建 `VpnRuntime`，状态设为 `CONNECTING`
4. 启动 worker thread，在里面跑 `eval_config` → `provide_creds` → `connect()`
5. worker thread detach（主线程不等它）

`stop()` 的流程：
1. 找到实例，状态设为 `DISCONNECTING`
2. 调用 `client->stop()`（线程安全，SDK 内部通过 ASIO post 到事件循环）

**ubus 方法注册**

```cpp
static const struct blobmsg_policy start_policy[] = {
    { "instance", BLOBMSG_TYPE_STRING },
    { "config",   BLOBMSG_TYPE_STRING },
    { "username", BLOBMSG_TYPE_STRING },
    { "password", BLOBMSG_TYPE_STRING },
};

struct ubus_method s_methods_[] = {
    UBUS_METHOD("start",  handle_start,  start_policy),
    UBUS_METHOD("stop",   handle_stop,   stop_policy),
    UBUS_METHOD("status", handle_status, status_policy),
    UBUS_METHOD_NOARG("list", handle_list),
    UBUS_METHOD("import", handle_import, import_policy),
};
```

`blobmsg_policy` 定义每个方法接受的参数名和类型。ubus 框架在调用 handler 前自动做参数解析和类型检查。

**使用示例**

```sh
# 启动一个 VPN 实例
ubus call openvpn3 start '{"instance":"work","config":"/etc/openvpn3/work.ovpn"}'

# 查看状态
ubus call openvpn3 status '{"instance":"work"}'
# 返回：{"instance":"work","state":"connected","uptime_sec":120,"bytes_in":4096,"bytes_out":1024}

# 列出所有实例
ubus call openvpn3 list

# 停止
ubus call openvpn3 stop '{"instance":"work"}'

# 导入 .ovpn 文件（从 /tmp 复制到 /etc/openvpn3/）
ubus call openvpn3 import '{"path":"/tmp/my.ovpn","name":"work"}'
```

**ManagedClient**

`ManagedClient` 继承 `OpenVPNClient`，是 SDK 和 `VpnManager` 之间的桥梁：
- 所有 `tun_builder_*` 调用委托给 `OpenWrtTunBuilder`
- `event()` 回调把 SDK 事件（CONNECTED/DISCONNECTED 等）同步到 `VpnManager` 的状态
- `socket_protect()` 调用 `add_bypass_route()` 防止路由死循环

---

### 3.4 main.cpp

**两种运行模式**

```
openvpn3d                          -> daemon 模式（正式用法）
openvpn3d <config.ovpn> [u p]     -> legacy 模式（手动测试用）
```

**daemon 模式**

```cpp
uloop_init();               // 初始化 libubox 事件循环
install_daemon_signals();   // SIGTERM/SIGINT -> uloop_end()

VpnManager mgr;
UbusInterface iface(mgr);
iface.init();               // 连接 ubusd，注册 "openvpn3" 对象

uloop_run();                // 阻塞，等待 ubus 调用 / 信号

mgr.stop_all();             // 停止所有 VPN 实例
iface.shutdown();           // 断开 ubus
uloop_done();
```

`uloop_run()` 是 libubox 的事件循环，和 ASIO 的 `io_context::run()` 类似，但专门为 OpenWrt 设计，集成了 ubus、uloop_fd、uloop_timeout 等机制。

**legacy 模式**

直接调用 `VpnManager::start("legacy", config_path)`，然后 poll `is_idle()` 等待连接结束。这样 legacy 模式和 daemon 模式走完全相同的代码路径（`ManagedClient`），测试结果可信。

**ovpncli.cpp 只能 include 一次**

OpenVPN3 Core SDK 的入口是 `client/ovpncli.cpp`（注意是 `.cpp` 不是 `.hpp`），它包含了大量实现代码，必须且只能被 include 一次。我们把它放在 `ubus_interface.cpp` 里，`main.cpp` 只 include `ovpncli.hpp`（只有类型声明）。

---

## 四、数据流全景

```
用户: ubus call openvpn3 start '{"instance":"work","config":"/etc/openvpn3/work.ovpn"}'
    |
    v
uloop（主线程）收到 ubus 消息
    |
    v
UbusInterface::handle_start()
    |  解析参数
    v
VpnManager::start("work", "/etc/openvpn3/work.ovpn")
    |  读取 .ovpn 文件
    |  创建 VpnRuntime，state = CONNECTING
    |  启动 worker thread
    v
worker thread:
    ManagedClient::eval_config()    <- 解析 .ovpn，检查配置合法性
    ManagedClient::provide_creds()  <- 提交用户名密码（如果需要）
    ManagedClient::connect()        <- 阻塞！ASIO 事件循环在这里跑
        |
        +-- TLS 握手
        |       socket_protect() -> add_bypass_route()  <- 防路由死循环
        |
        +-- VPN 隧道建立
        |       tun_builder_new()
        |       tun_builder_add_address()
        |       tun_builder_add_route()
        |       tun_builder_establish()  -> TunLinuxSetup::Setup::establish()
        |           +-- open /dev/net/tun
        |           +-- ioctl TUNSETIFF  -> 创建 tun0
        |           +-- rtnetlink        -> 配置 IP + 路由
        |
        +-- event("CONNECTED") -> VpnManager::set_state(CONNECTED)
        |
        +-- 数据转发（直到 stop() 被调用）

用户: ubus call openvpn3 stop '{"instance":"work"}'
    |
    v
VpnManager::stop("work")
    |  state = DISCONNECTING
    |  client->stop()  <- 线程安全，通过 ASIO post 到 worker thread
    v
worker thread:
    connect() 返回
    tun_builder_teardown()  -> TunLinuxSetup::Setup::destroy()
        +-- rtnetlink 删除路由和地址，关闭 tun fd
    state = IDLE
```

---

## 五、依赖关系图

```
main.cpp
    +-- ubus_interface.hpp/cpp
    |       +-- ovpncli.cpp (OpenVPN3 Core SDK 入口，include 一次)
    |       +-- tun_openwrt.hpp/cpp
    |       |       +-- tunnetlink.hpp  (SDK: rtnetlink 操作)
    |       |       +-- tunsetup.hpp    (SDK: TunLinuxSetup)
    |       +-- uci_config.hpp/cpp
    |               +-- libuci (读 /etc/config/openvpn3)
    +-- libubox/uloop.h (事件循环)
```

---

## 六、阶段进度

| 阶段 | 文件 | 状态 |
|------|------|------|
| 1 | CMakeLists.txt, Makefile, main.cpp (骨架) | 完成 |
| 2 | main.cpp (connect + 信号处理) | 完成 |
| 3 | tun_openwrt.hpp/cpp | 完成 |
| 4 | ubus_interface.hpp/cpp, main.cpp (daemon 模式) | 完成 |
| 5 | uci_config.cpp (libuci 实现) | 完成 |
| 6 | openvpn3.init 参数对齐 + netifd 集成 | 待做 |
| 7 | aarch64 SDK 重编 + rk3399 验证 | 待做 |

**阶段 6 的遗留问题**：init 脚本传了 `--instance`/`--config`/`--log-level`/`--dev` 参数，但 main.cpp 的 daemon 模式目前不解析命令行参数（靠 ubus 驱动）。需要在 main.cpp 里加 `getopt` 解析，或者改 init 脚本改成不传参数、靠 UCI 自动加载。
