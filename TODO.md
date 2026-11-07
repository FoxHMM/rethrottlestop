# ReThrottleStop 待办

## 后端未实现项

- [ ] **PROCHOT 温度编辑**

      读 MSR `0x1B1`（IA32_ENERGY_PERF_BIAS），
      若低 2 位等于 `0b10` 则清 bit1 后回写。
      这是主界面上唯一直接改 MSR 的输入框。

- [ ] **TPL 面板完整实现**

      涉及 MSR `0x770`（HWP_REQUEST）、`0x017`（P-State 定义）、
      `0x194`（PERF_CTL）。
      当前只实现了部分字段，Power Limit Controls 分组尚未接通。

- [ ] **AVX Offset 写入**

      参考实现写 MSR `0x1AD` 时用的是**完整的 64 位值**
      （来自它自己的配置缓冲），不是单字节修补。

      本平台的 `0x1AD` 存放 BIOS 倍频阶梯，部分写入会破坏它。
      需要实现一套影子缓冲（shadow buffer）机制才能安全写入。

      当前策略：**禁止写 `0x1AD`**，避免损坏倍频表。

## 待验证项

- [ ] 其他 CPU 型号上的 FIVR 邮箱行为
- [ ] 其他芯片组的 `0x774` 位布局
- [ ] 休眠/唤醒后的寄存器状态恢复
- [ ] KDE / GNOME 下的托盘菜单兼容性
- [ ] 多路 CPU 场景

## 打包

- [x] AUR 打包（PKGBUILD + .SRCINFO + systemd 单元 + udev 规则）

## 已确认无需实现（平台限制）

| 项 | 原因 |
|---|---|
| TVB | 本机 CPUID 型号不在参考实现的白名单里，参考实现在本机同样跳过 |
| V-Max Stress | MSR `0x150` 返回位，硬件只读 |
| Ring Down Bin | MSR `0x150` 返回位，硬件只读 |
| mV Boost | 无对应 MSR 字段 |
| E 核倍频 > 37 | 硬件天花板，写入成功但不执行 |
| `EDP OTHER` | 本机 `0x64F` bit8 从不触发（50 次采样含满载全为 0）|
