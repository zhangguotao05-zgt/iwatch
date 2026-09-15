# iwatch SF32LB58 固件

这里保存本项目可修改、可提交的固件源码。当前基线来自 SiFli-SDK v2.5.1 的 `example/multimedia/lvgl/watch_v9`，已经使用 **SF32LB58-DevKit-LCD、A128R32N1、QSPI 屏接口、A1 芯片修订版**完成无屏启动验证。

## 目录

```text
firmware/
├─ iwatch/
│  ├─ src/                    手表 UI 与资源源码
│  │  ├─ app_utils/           程序入口
│  │  ├─ gui_apps/clock/      表盘
│  │  ├─ gui_apps/main/       蜂窝菜单
│  │  └─ resource/            图片、字体和语言资源
│  └─ project/                SCons 配置、6 MB 项目分区表和 Keil 工程
├─ boards/
│  ├─ iwatch_sf32lb58_a128_qspi/     本项目 A128 QSPI/A1 板级配置
│  └─ sf32lb58-lcd_base/             板级公共实现
├─ build-gcc.ps1              GCC 完整构建
├─ generate-keil.ps1          生成 Keil MDK5 工程
└─ third_party/               上游许可证
```

## 依赖

- SiFli-SDK v2.5.1，默认本机路径：`C:\OpenSiFli\SiFli-SDK-v2.5.1`
- GCC 构建环境由 SDK 的 `export.ps1` 提供
- Keil MDK5/Arm Compiler 6；本机已检测到 `D:\keil\MDK5.36`

SDK 作为外部依赖使用，构建缓存和整个 SDK 不提交到本仓库。项目自己的应用代码和板级配置均在本目录内。

SiFli SDK v2.5.1 的 Windows Kconfig 工具不能直接处理含中文的工程路径。如果仓库所在路径含中文，构建脚本会自动创建纯英文目录联接 `C:\OpenSiFli\_workspaces\iwatch` 并从该入口构建；它仍指向本仓库中的同一份文件，不会复制源码。仓库本身处于纯英文路径时会直接构建。

## GCC 构建

在仓库根目录执行：

```powershell
.\firmware\build-gcc.ps1
```

使用其他 SDK 路径时：

```powershell
.\firmware\build-gcc.ps1 -SdkPath D:\path\to\SiFli-SDK-v2.5.1
```

输出位于 `firmware/iwatch/project/build_iwatch_sf32lb58_a128_qspi_hcpu/`。其中包含主程序、Bootloader、Flash Table 和 `sftool_param.json`；烧录前应按开发板串口下载流程使用 SiFli 工具，不要把 Keil 的 Download 按钮当成 CH342 串口下载。

## Keil

首次打开或新增/删除源码后，先生成工程：

```powershell
.\firmware\generate-keil.ps1
```

随后使用 Keil 打开：

```text
firmware/iwatch/project/project.uvprojx
```

生成脚本会把当前 `-SdkPath` 写入 Keil 工程，补充 SF32LB58 的 CDE 编译参数，并检查全部源文件引用。本机生成结果包含 924 个唯一且有效的引用；换电脑或改变 SDK 位置后重新运行脚本即可。

Keil 工程用于编辑和编译 HCPU 应用。当前开发板的 CH342 USB 串口下载仍使用 SiFli 的串口烧录脚本；Keil 的 Download 按钮需要另接 J-Link/CMSIS-DAP 并配置对应 Flash 算法。

## 当前板级修正

HCPU 和 LCPU 的 `board.conf` 均启用了 `CONFIG_LCPU_CONFIG_AUTO=y`，解决实物 A1 芯片使用原始 V2 配置时的启动断言。构建生成的最终编译宏可能显示 `LCPU_CONFIG_V1`，这是 AUTO 针对当前芯片生成的正常结果。

当前启动日志中的三个 `flash1 is not found` 来自 SDK 默认 FAL 分区表。它尚未阻止 NAND、文件系统、RT-Thread、LVGL 和主界面启动；在实现 OTA、蓝牙配置保存和偏好数据库前，需要补充本板专用 FAL 分区定义。

## 上游许可

`iwatch/src`、项目构建文件和板级基线来自 OpenSiFli/SiFli-SDK v2.5.1，遵循 Apache License 2.0。许可证副本见 [SiFli-SDK-LICENSE.txt](third_party/SiFli-SDK-LICENSE.txt)。后续新增的产品界面资源应保留清晰的来源和许可记录。
