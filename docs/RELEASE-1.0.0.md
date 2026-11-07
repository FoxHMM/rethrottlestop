# ReThrottleStop 1.0.0

Linux 下的 Intel FIVR 电压与频率控制工具。

## 主要特性

**电压调节（FIVR）**
- 六个电压域独立偏移：CPU Core、Intel GPU、CPU P Cache、iGPU Unslice、System Agent、CPU E Cache
- 偏移编码经 `intel-undervolt` 与 `undervolt-go-pro` 交叉验证
- 数据面板读取硬件返回值

**频率控制**
- Turbo Groups：8 档阶梯、All-Core 快捷设置、P/E 核视图切换
- 目标频率换算：输入目标频率，内部按标定表换算为寄存器值
- Speed Shift EPP：按 `(EPP << 8) | EPP` 写入 MSR `0x772`

**监控**
- 逐核数据表：FID、C0%、调制占空比、温度、逐核峰值
- VID 电压：由 MSR `0x198[47:32]` 解码
- 封装功耗：RAPL via `powercap` sysfs
- 限流原因：13 行面板，带累积位图

**系统集成**
- 守护进程位于 `system.slice`，不随图形会话终止
- GUI 单实例
- 控件状态完整持久化
- 托盘菜单（`com.canonical.dbusmenu`）

## 安装

```bash
git clone https://github.com/FoxHMM/rethrottlestop.git
cd rethrottlestop
make
sudo make install-staging
sudo modprobe msr
sudo systemctl enable --now rethrottlestop-daemon.service
```

或从本 Release 下载预编译包：

```bash
sudo pacman -U rethrottlestop-1.0.0-1-x86_64.pkg.tar.zst
sudo modprobe msr
sudo systemctl enable --now rethrottlestop-daemon.service
```

## 未实现与存疑功能

| 功能 | 状态 |
|---|---|
| AVX Offset 写入 | 未实现。部分写入会破坏 MSR `0x1AD` 中的倍频表，故禁止写入 |
| PROCHOT 温度编辑 | 未实现（MSR `0x1B1`） |
| TPL 面板 | 部分实现（MSR `0x770`、`0x017`、`0x194`） |
| Thermal Velocity Boost | 依赖 CPU 型号白名单，不匹配时不写入 |
| V-Max Stress | MSR `0x150` 返回位，只读 |
| Ring Down Bin | MSR `0x150` 返回位，只读 |
| mV Boost | 无对应 MSR 字段 |
| E 核倍频上限 | 受硬件限制，超过上限的值不生效 |
| 频率写入生效条件 | 部分平台需电源模式为 `performance` |
| HWP_REQUEST 字段写入 | 部分平台返回 `EIO`，固件锁定 |
| 其他 CPU 型号 / 芯片组 | 未测试 |
| 休眠唤醒后状态 | 未测试 |
| 多路 CPU | 未考虑 |
| KDE / GNOME 托盘菜单 | 未测试 |

详见 [README](README.md) 与 [TODO.md](TODO.md)。

## 兼容性

功能验证在单一配置上完成：Intel 混合架构处理器（P 核 + E 核）、
Linux 内核 6.x 以上、Wayland 合成器、Arch Linux。

FIVR 邮箱行为、倍频寄存器布局与 HWP 行为均依赖固件实现，
其他平台可能存在差异。

## 参考项目

- [ThrottleStop](https://www.techpowerup.com/download/techpowerup-throttlestop/) — 行为参考，本项目为独立实现，不包含也不分发其代码
- [intel-undervolt](https://github.com/kitsunyan/intel-undervolt) — FIVR 偏移编码交叉验证
- [undervolt](https://github.com/georgewhewell/undervolt) — FIVR 偏移协议交叉验证

## 许可证

MIT
