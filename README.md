# iwatch

基于 SF32LB58、RT-Thread 和 LVGL 9.4 的智能手表项目。当前硬件验证平台为 SF32LB58-DevKit-LCD A128R32N1，显示模组为 390×450 QSPI AMOLED（CO5300 + FT6146）；自研原理图选型为 SF32LB586VDD36。

工程说明统一采用 HTML：

- [文档首页](index.html)
- [架构复审与开发基线 v1.1](docs/ui/架构复审与开发基线_v1.1.html)
- [功能逻辑与页面契约](docs/ui/功能逻辑与页面契约_v1.html)
- [模块接口、开发任务与测试矩阵](docs/ui/模块接口与开发任务_v1.html)
- [系统架构 v1.1](docs/ui/系统架构_v1.1.html)
- [视觉与交互规范 v1](docs/ui/视觉与交互规范_v1.html)
- [Apple Watch 设计指南研究与项目适配](docs/ui/Apple_Watch设计指南研究与项目适配_v1.html)
- [Apple Watch 界面复刻定稿 v2（首批七页施工入口）](docs/ui/Apple_Watch界面复刻定稿_v2.html)
- [实施与验收记录](docs/ui/实施与验收_v1.html)
- [D01–D03 板上验收记录](docs/ui/D01-D03_实施记录.html)
- [D04–D05 时间与最小服务实施记录](docs/ui/D04-D05_实施记录.html)
- [D06 显示目标与会话亮度实施记录](docs/ui/D06_显示目标与会话亮度实施记录.html)
- [D07 资源预算与共享组件设计方案 v2](docs/ui/D07_资源预算与共享组件设计方案_v2.html)
- [D07-A0 工具与安全链实施记录](docs/ui/D07-A0_工具与安全链实施记录.html)
- [D07-A1 字体替换与渲染修复记录](docs/ui/D07-A1_字体替换与渲染修复记录.html)
- [D07-B 字体服务与主题实施记录](docs/ui/D07-B_字体服务与主题实施记录.html)
- [D07-C 共享组件与页面回收实施记录](docs/ui/D07-C_共享组件与页面回收实施记录.html)
- [D07-D 组件展示与综合验收记录](docs/ui/D07-D_组件展示与综合验收记录.html)
- [D08 页面作用域与路由实施记录](docs/ui/D08_页面作用域与路由实施记录.html)
- [D09 双键语义与输入仲裁实施记录](docs/ui/D09_双键语义与输入仲裁实施记录.html)
- [D10 首批七页实施记录](docs/ui/D10_首批七页实施记录.html)
- [Series 11 界面覆盖计划](docs/UI复刻计划_watchOS26.html)
- [固件构建与调试](firmware/README.html)
- [硬件原理图 v0.3](hardware/原理图_v0.3_封装与接口完善/阅读说明.html)

开发任务使用 [GitHub Issues](https://github.com/zhangguotao05-zgt/iwatch/issues) 跟踪。任务实际开始时才创建对应的 Dxx Issue；完成后补齐提交、主机测试、双工具链和开发板证据，再以 `completed` 关闭。D01–D05 已按此规则补录为 [#1–#5](https://github.com/zhangguotao05-zgt/iwatch/issues?q=is%3Aissue+is%3Aclosed+label%3Atask)；[D06 · #6](https://github.com/zhangguotao05-zgt/iwatch/issues/6) 已完成；[D07 · #7](https://github.com/zhangguotao05-zgt/iwatch/issues/7) 已通过并关闭。性能遗留项由 [#8](https://github.com/zhangguotao05-zgt/iwatch/issues/8) 独立跟踪，[D08 · #9](https://github.com/zhangguotao05-zgt/iwatch/issues/9) 已按“接受已知风险”批准收口；[显示异常 #10](https://github.com/zhangguotao05-zgt/iwatch/issues/10) 保持打开，允许接续 D09。

D08 按“接受已知风险”收口，验收基线 `5c51f54`、固件源码 `82ab706`：基础实现及已验证功能通过，负责人同意将显示异常独立跟踪并继续 D09。首轮半黑半白转入 [#10](https://github.com/zhangguotao05-zgt/iwatch/issues/10)，仍未定位、后续未复现；性能遗留继续跟踪 [#8](https://github.com/zhangguotao05-zgt/iwatch/issues/8)。首次异常、采集缺口和原始证据保持封存，此决定不代表显示异常已修复或产品发布验收通过。

D01–D03 已完成页面生命周期、BSP/分区/双工具链来源、GUI 等待/唤醒和输入取消实现，并在开发板上完成备份、三件套写入校验、冷启动、输入压力、显示超时恢复和黑屏触摸唤醒验收。D04/D05 已完成时间、启动协调、32 B 命令、48 B 结果、固定账本与快照，并通过主机、双工具链及开发板校时、软件复位和断电冷启动验收；断电后 RTC 正确失效，服务使用 TRNG 生成非零 31 位随机 session，该值只降低碰撞概率，不作为唯一性或安全保证。DEV_A128_NAND 的 main 位于 `0x69000000`，必须与同一归档的 Bootloader、FTab 成套烧录。

D06 已实现 `SET_BRIGHTNESS`、目标/实际/持久状态、最新目标 mailbox 和 GUI owner 驱动确认；只有 LCD 状态、busy、写入后状态及亮度回读全部通过才返回 `OK_APPLIED`。首次显示、触摸 reassert 与故障恢复均读取当前目标，亮度 revision 饱和时失败无副作用。持久值仍明确为会话状态，真实 NAND 保存留 D15。最终 GCC main 为 5,745,568 B，双工具链身份、三件套写后校验、20%/80% 实屏变化、故障结果、软件复位和输入压力均已通过开发板复验。

D07 v2 已以 `967483d` 为批准基线。D07-A1 已获架构审批通过，收口提交为 e064686。D07-B 已获架构阶段验收通过（审批基线 7dfd90d）。D07-C 已获架构阶段验收通过（审批基线 c8392a9）。D07 已获最终审批（基线 9088f52，固件源码 5d66e58），Issue #7 已关闭。触摸溢出取消整改通过；长文延迟与溢出频率转入独立性能待办 #8，历史最大 215 ms 保留，不宣称全面性能达标。D08 已按“接受已知风险”批准收口（验收基线 5c51f54，固件源码 82ab706）；显示异常转入 Issue #10，未定位、后续未复现，性能遗留继续跟踪 Issue #8，允许开始 D09 输入，随后推进 D10 正式界面。 D07 验收时字体为 336 字符、56,804 B；D10 本地开发已扩展为 389 字符、65,752 B，尚未上板。A1 最终整改镜像在 20%/80% 下文字与操作正常，8 次进出后的主堆和字形池均回到启动值；这条是 A1 历史证据。详见 [安全整改记录](docs/ui/D07-A1_安全整改与复验记录.html)。`flash1` 的 FAL 持久化、低功耗和长期测试仍未关闭。

D09 已获架构审批通过（审批基线 `042ba6f`、固件源码 `05fb667`），[Issue #11](https://github.com/zhangguotao05-zgt/iwatch/issues/11) 按 completed 收口。已修复 Home 动画停住与水锁触摸穿透，主机、双工具链、当前镜像实体双键及 20%/80% 通知回归通过。详见 [D09 实施记录](docs/ui/D09_双键语义与输入仲裁实施记录.html)。主堆小幅波动、最大 133 ms 队列延迟、六次触摸溢出及 #8/#10 边界保留，允许进入 D10；本次收口不重新烧录。

D10 已创建 [#12](https://github.com/zhangguotao05-zgt/iwatch/issues/12)，按视觉定稿 v2 开始本地实现。七页 View、路由、校时与亮度服务接入已完成本地主机回归，53 个缺字已补齐；正在准备最终双工具链和实机验证，动效差异及 D10 验收仍未关闭，详见 [D10 实施记录](docs/ui/D10_首批七页实施记录.html)。
