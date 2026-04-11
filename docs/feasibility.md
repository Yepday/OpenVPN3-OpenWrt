# 可行性调查报告

> 调查日期：2026-03-25
> 目标：确认 OpenVPN3 移植到 OpenWrt 23.05.x (aarch64) 的技术可行性

---

## 结论

**可行，唯一缺口是 libasio，工作量可控。**

---

## 1. 工具链：GCC 版本与 C++20 支持

| 项目 | 详情 |
|------|------|
| OpenWrt 23.05 aarch64 工具链 | `toolchain-aarch64_cortex-a53_gcc-12.3.0_musl` |
| GCC 版本 | **12.3.0** |
| C++20 支持 | ✅ 完整支持（`-std=c++20`），GCC 12 已覆盖 OpenVPN3 所需特性 |
| C 库 | musl libc（非 glibc，需注意兼容性） |
| NanoPi R5C 核心 | Cortex-A55（aarch64），与 a53 工具链兼容 |

**结论**：C++20 无障碍，不需要特殊处理。

---

## 2. 依赖库现状

| 依赖 | OpenWrt packages feed | 版本 | 结论 |
|------|-----------------------|------|------|
| `libasio` | ❌ 不存在 | — | **需自己打包**（纯 header-only，工作量极小） |
| `libfmt` | ✅ 存在 | 12.1.0 | 直接使用 |
| `libjsoncpp` | ✅ 存在 | 1.9.5 | 直接使用 |
| `libxxhash` | ✅ 存在 | 0.8.1 | 直接使用 |
| `libopenssl` | ✅ 基础包 | — | 直接使用 |
| `liblz4` | ✅ 基础包 | — | 直接使用 |
| `libstdcpp` | ✅ 基础包 | — | DEPENDS 中声明即可 |
| `kmod-tun` | ✅ 基础包 | — | 直接使用 |

### libasio 打包评估

Asio 是纯 header-only 库（BSL-1.0 协议）。OpenWrt package Makefile 中：
- `Build/Compile` 段留空（无需编译）
- `Package/install` 只安装 `include/asio.hpp` 和 `include/asio/`
- 打包复杂度极低，且有助于建立"向 openwrt/packages 贡献"的记录

---

## 3. 历史先例

2021 年有人在 OpenWrt 论坛尝试编译 OpenVPN3，卡在 autotools bootstrap 阶段（`autoreconf` 缺失）。
**本项目采用 CMake + OpenWrt `cmake.mk`，完全绕开 autotools，不存在此问题。**

此外 OpenWrt 官方已有 Boost（大型 C++ header-only 库）打包先例，说明生态对此类依赖有成熟路径。

---

## 4. 潜在风险点

| 风险 | 级别 | 处置 |
|------|------|------|
| musl libc 兼容性 | 中 | 交叉编译阶段测试，OpenVPN3 已知在 Alpine (musl) 上运行 |
| 二进制体积 | 中 | 编译时 strip，评估是否需要裁剪 C++ STL 功能 |
| C++20 模板编译时间 | 低 | 仅影响构建速度，不影响运行时 |
| DCO 内核模块 | 低 | 初版禁用（`-DCLI_OVPNDCO=OFF`），属于可选特性 |
| OpenWrt upstream 审核 | 中 | 需遵循 openwrt/packages 贡献规范，代码风格和文档完整性要求较高 |

---

## 5. 不引入的组件（与 openvpn3-linux 的刻意区别）

| openvpn3-linux 组件 | 本项目决策 | 原因 |
|--------------------|-----------|------|
| D-Bus | ❌ 不引入 | OpenWrt 无 D-Bus，用 ubus 替代 |
| systemd | ❌ 不引入 | OpenWrt 用 procd |
| NetworkManager | ❌ 不引入 | OpenWrt 用 netifd |
| Python 前端 | ❌ 不引入 | 嵌入式环境不适合 |
| DCO 内核模块 | 暂缓 | 初版不引入，稳定后可评估 |
