# ReThrottleStop 1.0.0

[English](#english) | [简体中文](#简体中文)

---

## 简体中文

Linux 下的 Intel FIVR 电压与频率控制工具。

### 主要特性

**电压调节（FIVR）**
- 六个电压域独立偏移：CPU Core、Intel GPU、CPU P Cache、iGPU Unslice、System Agent、CPU E Cache
- 偏移编码经 `intel-undervolt` 与 `undervolt-go-pro` 交叉验证
- 数据面板读取硬件返回值，不回显输入值
- 偏移值可持久化到配置文件

**频率控制**
- Turbo Groups：8 档倍频阶梯，含 All-Core 快捷设置与 P/E 核视图切换
- 目标频率换算：界面输入目标频率（如 48 表示 4.8 GHz），内部按标定表换算为寄存器值
- Speed Shift EPP：按 `(EPP << 8) | EPP` 写入 MSR `0x772`

**监控**
- 逐核数据表：FID、C0%、调制占空比、温度、逐核峰值
- VID 电压：由 MSR `0x198[47:32]` 解码，单位 1/8192 V
- 封装功耗：通过 `powercap` sysfs 读取 RAPL，64 位计数器差分
- 限流原因：13 行面板，带累积位图

**系统集成**
- 守护进程位于 `system.slice`，不随图形会话结束而终止
- GUI 单实例（D-Bus 应用 ID）
- 控件状态完整持久化
- 托盘菜单（`com.canonical.dbusmenu`）：配置切换、退出

### 安装

从源码：

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

依赖：`gtk4`、`glib2`、`systemd`、`gcc`、`pkgconf`

### 未实现与存疑功能

| 功能 | 状态 |
|---|---|
| AVX Offset 写入 | 未实现。部分写入会破坏 MSR `0x1AD` 中的倍频表，故禁止写入 |
| PROCHOT 温度编辑 | 未实现（MSR `0x1B1`） |
| TPL 面板 | 部分实现（MSR `0x770`、`0x017`、`0x194`） |
| Thermal Velocity Boost | 依赖 CPU 型号白名单，型号不匹配时不写入，复选框置灰 |
| V-Max Stress | MSR `0x150` 返回位，硬件只读，界面置灰 |
| Ring Down Bin | MSR `0x150` 返回位，硬件只读，界面置灰 |
| mV Boost | 无对应 MSR 字段 |
| E 核倍频上限 | 受硬件限制，写入成功但超过上限的值不生效 |
| 频率写入生效条件 | 部分平台需系统电源模式为 `performance` 才被硬件采纳 |
| HWP_REQUEST 字段写入 | 部分平台返回 `EIO`，字段被固件锁定 |
| 其他 CPU 型号 / 芯片组 | 未测试 |
| 休眠 / 唤醒后状态 | 未测试 |
| 多路 CPU | 未考虑 |
| KDE / GNOME 托盘菜单 | 未测试 |

详见 [README](https://github.com/FoxHMM/rethrottlestop#readme)
与 [TODO](https://github.com/FoxHMM/rethrottlestop/blob/main/TODO.md)。

### 兼容性

已在单一配置上完成功能验证：Intel 混合架构处理器（P 核 + E 核）、
Linux 内核 6.x 以上、Wayland 合成器、Arch Linux。

FIVR 邮箱行为、倍频寄存器布局与 HWP 行为均依赖固件实现，
在其他平台上可能存在差异。

### 参考项目

- [ThrottleStop](https://www.techpowerup.com/download/techpowerup-throttlestop/)
  — 行为参考。本项目为独立实现，不包含也不分发其任何代码
- [intel-undervolt](https://github.com/kitsunyan/intel-undervolt)
  — FIVR 偏移编码交叉验证；守护进程设计参考
- [undervolt](https://github.com/georgewhewell/undervolt)
  — FIVR 偏移协议交叉验证

### 许可证

MIT

---

## English

Intel FIVR voltage and frequency control for Linux.

### Features

**Voltage (FIVR)**
- Six independently adjustable domains: CPU Core, Intel GPU, CPU P Cache,
  iGPU Unslice, System Agent, CPU E Cache
- Offset encoding cross-verified against `intel-undervolt` and `undervolt-go-pro`
- The data panel reads hardware return values; it does not echo user input
- Offsets persist to a configuration file

**Frequency**
- Turbo Groups: 8-ratio ladder with All-Core shortcuts and P/E core views
- Target-frequency conversion: enter a target ratio (48 means 4.8 GHz); the
  value is translated to the register value via a calibration table
- Speed Shift EPP: written as `(EPP << 8) | EPP` to MSR `0x772`

**Monitoring**
- Per-core table: FID, C0%, modulation duty, temperature, per-core peak
- VID voltage: decoded from MSR `0x198[47:32]`, unit 1/8192 V
- Package power: RAPL via `powercap` sysfs, 64-bit counter differencing
- Limit reasons: 13-row panel with accumulated bit mask

**System integration**
- The daemon runs in `system.slice` and is not terminated with the graphical session
- Single-instance GUI (D-Bus application ID)
- Full control-state persistence
- Tray menu (`com.canonical.dbusmenu`): profile switching, quit

### Installation

From source:

```bash
git clone https://github.com/FoxHMM/rethrottlestop.git
cd rethrottlestop
make
sudo make install-staging
sudo modprobe msr
sudo systemctl enable --now rethrottlestop-daemon.service
```

Or install the prebuilt package from this release:

```bash
sudo pacman -U rethrottlestop-1.0.0-1-x86_64.pkg.tar.zst
sudo modprobe msr
sudo systemctl enable --now rethrottlestop-daemon.service
```

Dependencies: `gtk4`, `glib2`, `systemd`, `gcc`, `pkgconf`

### Unimplemented and unverified

| Item | Status |
|---|---|
| AVX Offset write | Not implemented. A partial write corrupts the ratio table held in MSR `0x1AD`, so writing is refused |
| PROCHOT temperature edit | Not implemented (MSR `0x1B1`) |
| TPL panel | Partially implemented (MSR `0x770`, `0x017`, `0x194`) |
| Thermal Velocity Boost | Gated by a CPU-model whitelist; not written when the model does not match, checkbox greyed out |
| V-Max Stress | Return bit of MSR `0x150`, read-only, greyed out |
| Ring Down Bin | Return bit of MSR `0x150`, read-only, greyed out |
| mV Boost | No corresponding MSR field |
| E-core ratio limit | Hardware-limited; values above the ceiling do not take effect |
| Frequency write precondition | On some platforms the value is only honoured when the system power profile is `performance` |
| HWP_REQUEST field writes | Return `EIO` on some platforms; fields are firmware-locked |
| Other CPU models / chipsets | Untested |
| State after suspend/resume | Untested |
| Multi-socket systems | Not considered |
| KDE / GNOME tray menu | Untested |

See [README](https://github.com/FoxHMM/rethrottlestop#readme)
and [TODO](https://github.com/FoxHMM/rethrottlestop/blob/main/TODO.md) for details.

### Compatibility

Functionality has been verified on a single configuration: an Intel hybrid
processor (P-cores + E-cores), Linux kernel 6.x or later, a Wayland compositor,
and Arch Linux.

FIVR mailbox behaviour, ratio register layout and HWP behaviour all depend on
firmware implementation and may differ on other platforms.

### References

- [ThrottleStop](https://www.techpowerup.com/download/techpowerup-throttlestop/)
  — Behavioural reference. This project is an independent implementation;
  no code from it is included or redistributed
- [intel-undervolt](https://github.com/kitsunyan/intel-undervolt)
  — FIVR offset encoding cross-check; daemon design reference
- [undervolt](https://github.com/georgewhewell/undervolt)
  — FIVR offset protocol cross-check

### License

MIT
