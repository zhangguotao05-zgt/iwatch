# iwatch

基于 SF32LB586VDD36、RT-Thread 和 LVGL 9.4 的智能手表项目。当前硬件验证平台为 SF32LB58-DevKit-LCD A128R32N1，显示模组为 390×450 QSPI AMOLED（CO5300 + FT6146）。

工程说明统一采用 HTML：

- [文档首页](index.html)
- [系统架构 v1](docs/ui/系统架构_v1.html)
- [视觉与交互规范 v1](docs/ui/视觉与交互规范_v1.html)
- [实施与验收记录](docs/ui/实施与验收_v1.html)
- [Series 11 界面覆盖计划](docs/UI复刻计划_watchOS26.html)
- [固件构建与调试](firmware/README.html)
- [硬件原理图 v0.3](hardware/原理图_v0.3_封装与接口完善/阅读说明.html)

M0 可靠性基础已完成主机测试和 GCC 构建。新的 NAND 分区布局与输入队列尚未烧录到开发板，首次上板前需按架构文档完成备份和引导验证。仓库目前没有可直接投产的佩戴版 PCB 或完整 watchOS 界面。
