# iwatch

基于 SF32LB58、RT-Thread 和 LVGL 9.4 的智能手表项目。当前硬件验证平台为 SF32LB58-DevKit-LCD A128R32N1，显示模组为 390×450 QSPI AMOLED（CO5300 + FT6146）；自研原理图选型为 SF32LB586VDD36。

工程说明统一采用 HTML：

- [文档首页](index.html)
- [架构复审与开发基线 v1.1](docs/ui/架构复审与开发基线_v1.1.html)
- [功能逻辑与页面契约](docs/ui/功能逻辑与页面契约_v1.html)
- [模块接口、开发任务与测试矩阵](docs/ui/模块接口与开发任务_v1.html)
- [系统架构 v1.1](docs/ui/系统架构_v1.html)
- [视觉与交互规范 v1](docs/ui/视觉与交互规范_v1.html)
- [实施与验收记录](docs/ui/实施与验收_v1.html)
- [Series 11 界面覆盖计划](docs/UI复刻计划_watchOS26.html)
- [固件构建与调试](firmware/README.html)
- [硬件原理图 v0.3](hardware/原理图_v0.3_封装与接口完善/阅读说明.html)

部分 M0 基础代码已通过主机测试和 GCC 构建。复审发现的页面生命周期问题仍待修复；下一批先执行 D01–D03，再建立服务与界面闭环。新的 NAND 布局与输入队列尚未烧录，首次上板前需完成备份和引导验证。仓库目前没有可直接投产的佩戴版 PCB 或完整 watchOS 界面。
