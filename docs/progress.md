# 项目进度

> 目标：OpenVPN3 on OpenWrt — 可交叉编译、可 opkg 安装、procd/ubus/netifd 集成
> 硬件：NanoPi R5C（RK3568，aarch64）
> 系统：OpenWrt 23.05.x

---

## 当前状态

**阶段：M0 框架搭建完成，准备进入 M1**

---

## 里程碑

### M0 — 项目框架 ✅

- [x] 确定项目定位和工程目标
- [x] 完成可行性调查（工具链、依赖库、历史先例）
- [x] 搭建代码目录结构（package feed 布局）
- [x] 建立框架文件（CMakeLists.txt、OpenWrt Makefile、源文件骨架）
- [x] 编写架构设计文档

---

### M1 — 交叉编译验证 🔲

目标：能用 OpenWrt SDK 交叉编译出 aarch64 二进制（哪怕功能不完整）

- [ ] 搭建 OpenWrt 23.05 SDK 环境（aarch64）
- [ ] 打包 libasio（header-only），验证能被 SDK 正确安装
- [ ] 验证 openvpn3-core 能在 musl + aarch64 下编译通过
- [ ] 解决编译期错误（预期有 musl 兼容性问题需排查）
- [ ] 产出：能 `make package/openvpn3/compile` 不报错

---

### M2 — 最小可运行版本 🔲

目标：在 NanoPi R5C 上能建立 VPN 连接（命令行触发）

- [ ] 实现 `TunBuilderBase`（tun_openwrt.cpp）
  - [ ] 打开 TUN 设备
  - [ ] 设置 IP 地址（ioctl）
  - [ ] 添加路由（rtnetlink 或 ip 命令）
  - [ ] teardown 清理
- [ ] 实现 `ClientAPI::OpenVPNClient` 子类（事件 & 日志回调）
- [ ] 实现 `main.cpp`（读配置 → connect → 阻塞）
- [ ] 用真实 .ovpn 文件测试连接
- [ ] 产出：`openvpn3d --config test.ovpn` 能建立 VPN 隧道

---

### M3 — procd 集成 🔲

目标：`/etc/init.d/openvpn3 start` 能启动和管理 VPN 实例

- [ ] 实现 UCI 配置读取（uci_config.cpp，libuci API）
- [ ] 完善 procd init 脚本（多实例、respawn、file 触发）
- [ ] 验证 `service openvpn3 start/stop/restart`
- [ ] 验证重启后自动启动

---

### M4 — ubus 接口 🔲

目标：`ubus call openvpn3 status` 能返回连接状态

- [ ] 实现 ubus_interface.cpp（libubus API）
- [ ] 实现 list / start / stop / status 方法
- [ ] 主循环改为 `uloop_run()`，VPN 移到 worker thread
- [ ] 验证 ubus call 各方法

---

### M5 — 打包与 opkg 安装 🔲

目标：生成 .ipk 文件，能在设备上 `opkg install` 并正常工作

- [ ] 完善 OpenWrt package Makefile（依赖声明、安装路径）
- [ ] 验证 ipk 安装后配置文件正确部署
- [ ] 验证卸载干净
- [ ] 基本功能回归测试

---

### M6 — netifd 集成（增强）🔲

目标：VPN 接口在 `ip link`、LuCI 网络页面正确显示，路由由 netifd 管理

- [ ] 研究 netifd proto handler 机制
- [ ] 实现 netifd proto 脚本或 C proto handler
- [ ] 在 UCI network config 中声明 VPN 接口
- [ ] 防火墙 zone 集成（fw4/nftables）

---

### M7 — 上游 PR 准备 🔲

目标：代码质量达到 OpenWrt packages 仓库要求

- [ ] 阅读 OpenWrt package 贡献规范
- [ ] 代码审查（符合 OpenWrt 风格）
- [ ] 完善 README 和文档
- [ ] 先提交 libasio 包（独立 PR，铺路）
- [ ] 提交 openvpn3 包 PR
- [ ] 建立独立 package feed 仓库（作为 PR 合并前的替代方案）

---

## 问题与决策日志

| 日期 | 问题 | 决策 |
|------|------|------|
| 2026-03-25 | libasio 不在 OpenWrt feeds | 自行打包，顺带作为首个 PR 贡献 |
| 2026-03-25 | DCO 内核模块是否支持 | 初版不引入，M5 后评估 |
| 2026-03-25 | netifd 集成复杂度 | 推迟到 M6，初版直接 ioctl |
