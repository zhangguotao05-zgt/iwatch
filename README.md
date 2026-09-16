# iwatch

基于 SF32LB58、RT-Thread 和 LVGL 9.4 的智能手表项目。当前硬件验证平台为 SF32LB58-DevKit-LCD A128R32N1，显示模组为 390×450 QSPI AMOLED（CO5300 + FT6146）；自研原理图选型为 SF32LB586VDD36。

工程说明统一采用 HTML：

- [文档首页](index.html)
- [架构复审与开发基线 v1.1](docs/ui/架构复审与开发基线_v1.1.html)
- [功能逻辑与页面契约](docs/ui/功能逻辑与页面契约_v1.html)
- [模块接口、开发任务与测试矩阵](docs/ui/模块接口与开发任务_v1.html)
- [系统架构 v1.1](docs/ui/系统架构_v1.1.html)
- [视觉与交互规范 v1](docs/ui/视觉与交互规范_v1.html)
- [实施与验收记录](docs/ui/实施与验收_v1.html)
- [D01–D03 板上验收记录](docs/ui/D01-D03_实施记录.html)
- [D04–D05 时间与最小服务实施记录](docs/ui/D04-D05_实施记录.html)
- [Series 11 界面覆盖计划](docs/UI复刻计划_watchOS26.html)
- [固件构建与调试](firmware/README.html)
- [硬件原理图 v0.3](hardware/原理图_v0.3_封装与接口完善/阅读说明.html)

开发任务使用 [GitHub Issues](https://github.com/zhangguotao05-zgt/iwatch/issues) 跟踪。任务实际开始时才创建对应的 Dxx Issue；完成后补齐提交、主机测试、双工具链和开发板证据，再以 `completed` 关闭。D01–D05 已按此规则补录为 [#1–#5](https://github.com/zhangguotao05-zgt/iwatch/issues?q=is%3Aissue+is%3Aclosed+label%3Atask)，D06 将在开始开发时创建。

D01–D03 已完成页面生命周期、BSP/分区/双工具链来源、GUI 等待/唤醒和输入取消实现，并在开发板上完成备份、三件套写入校验、冷启动、输入压力、显示超时恢复和黑屏触摸唤醒验收。D04/D05 已完成时间、启动协调、32 B 命令、48 B 结果、固定账本与快照，并通过主机、双工具链及开发板校时、软件复位和断电冷启动验收；断电后 RTC 正确失效，服务 session 由 TRNG 重建。DEV_A128_NAND 的 main 位于 `0x69000000`，必须与同一归档的 Bootloader、FTab 成套烧录。

当前 main 已占 6 MiB 槽约 91%，完整中文字体与图片必须在继续扩展界面前迁移或裁剪；`flash1` 的 FAL 持久化、低功耗和长期测试尚未关闭。仓库目前没有可直接投产的佩戴版 PCB 或完整 watchOS 界面。
