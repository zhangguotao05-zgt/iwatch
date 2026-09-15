import fs from 'node:fs/promises';
import path from 'node:path';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

const outDir = 'hardware';
const supportDir = 'tools/bom-selection';
const fileName = '自研智能手表_整机选型评估与BOM_v0.2.xlsx';
const S = {
  soc:'https://wiki.sifli.com/hardware/SF32LB52B-E-G-J-HW-Application.html',
  socSpec:'https://downloads.sifli.com/user%20manual/DS5202-SF32LB52X-%E8%8A%AF%E7%89%87%E6%8A%80%E6%9C%AF%E8%A7%84%E6%A0%BC%E4%B9%A6%20V0p2p5.pdf',
  chips:'https://www.sifli.com/en/xinpian',
  kit:'https://docs.sifli.com/projects/sdk/latest/sf32lb52x/supported_boards/boards/sf32lb52-lcd_n16r8/doc/index.html',
  flash:'https://www.winbond.com/export/sites/winbond/product-selection-guide/file/2025-Product-Selection-Guide-Winbond-Code-Storage-Flash-Memory.pdf',
  charger:'https://www.ti.com/product/BQ25180/part-details/BQ25180YBGR',
  chargerSpec:'https://www.ti.com/product/BQ25180',
  buckboost:'https://www.ti.com/product/TPS63031/part-details/TPS63031DSKR',
  buck:'https://www.ti.com/product/TPS62840/part-details/TPS62840DLCR',
  imu:'https://www.st.com/en/mems-and-sensors/lis2dw12.html',
  imuSpec:'https://www.st.com/resource/en/datasheet/lis2dw12.pdf',
  haptics:'https://www.ti.com/product/DRV2605L/part-details/DRV2605LYZFR',
  gauge:'https://www.analog.com/en/products/max17055.html',
  esd:'https://www.ti.com/product/TPD4E05U06/part-details/TPD4E05U06DQAR',
  lvgl:'https://lvgl.io/docs/open/9.0/porting/display.html'
};

// This is a selection BOM. Passive counts, connector MPNs and board dimensions
// must be expanded against the measured donor and the completed schematic.
const bom = [
 ['B01','主控 / PSRAM','SF32LB52JUD6',1,'3.3 V；LCD / I²C / SPI / BLE','QFN68L；7 × 7 × 0.85 mm；0.35 mm 间距','SiFli；240 MHz 应用核、2D/2.5D 图形加速，合封 16 MB PSRAM。固件建议采用 SiFli SDK + LVGL。','先确认现有屏幕接口、分辨率、时序及 7 mm 芯片区域可放入；不直接支持所有屏。','核对屏幕后再采购',null,null,S.socSpec],
 ['B02','程序 / 图形存储','W25Q128JVPIQ',1,'2.7–3.6 V；SPI / Dual / Quad','WSON-8；6 × 5 mm','Winbond；128 Mbit = 16 MB，用于程序、字体、图标与升级分区。','核对 MPI 电平、启动方式、SDK Flash 支持及 6 × 5 mm 占位；若面积不足再选小封装等效料。','主控与布局确定后采购',null,null,S.flash],
 ['B03','主晶体','E1SB48E001G00E',1,'48 MHz；CL 8.8 pF','2016 公制；2.0 × 1.6 mm','Hosonic；取自 SiFli 硬件指南推荐清单。','负载电容及布局采用所选芯片版本的推荐设计；具体尾码与供货核对。','可随主控少量备料',null,null,S.soc],
 ['B04','RTC 晶体','ETST00327000LE',1,'32.768 kHz；CL 7 pF','3215 公制；3.2 × 1.5 mm','Hosonic；低速计时与休眠时钟候选，来自 SiFli 推荐清单。','核对高度、ESR、负载与实际板上校准；缺料时按原规格替代。','可随主控少量备料',null,null,S.soc],
 ['B05','充电 / Power Path','BQ25180YBGR',1,'1 节锂电；I²C；首轮使用受控 5 V 输入','DSBGA-8；1.783 × 0.983 mm','TI；充电终止电压可配置，带电源路径、温度管理和关机模式。','必须匹配实物电池终止电压、充电电流、NTC 与保护板；不得按芯片 1 A 上限直接给小电池充电。','电池参数确定后采购',null,null,S.chargerSpec],
 ['B06','主 3.3 V 电源','TPS63031DSKR',1,'输入 1.8–5.5 V；固定输出 3.3 V','WSON-10；DSK 封装图为准','TI；升降压，用于 B05 SYS 到主控和 3.3 V 外设供电。','按最低电池电压下的实际负载选电感和电容；1 A 是开关电流等级，不是保证的输出电流。','电源预算确定后采购',null,null,S.buckboost],
 ['B07','可选 1.8 V 电源','TPS62840DLCR',null,'降压；拟输出 1.8 V','VSON-HR-8；1.5 × 2.0 mm','TI；仅在实物屏幕 / 触摸需要独立 1.8 V 电源时装配。','先核对器件电压域；评估主控已有电源输出是否满足负载，避免重复增加电源。','选配，暂不采购',null,null,S.buck],
 ['B08','抬腕 / 运动检测','LIS2DW12TR',1,'I²C 或 SPI；建议接 INT 唤醒','LGA-12；2.0 × 2.0 × 0.7 mm','ST；低功耗三轴加速度计，支持抬腕检测、运动唤醒和计步算法输入。','固定器件坐标方向、确认 I²C 地址和中断脚；抬腕和计步仍需软件算法调试。','可先买评估模块验证',null,null,S.imuSpec],
 ['B09','振动驱动','DRV2605LYZFR',1,'2–5.2 V；I²C；LRA / ERM','DSBGA-9；YZF 封装图为准','TI；闭环振动驱动候选，支持启动和制动控制。','先辨认现有马达类型、阻抗、额定电压和谐振参数；使用现有 ERM 时无法保证 LRA 式清脆触感。','马达参数确定后采购',null,null,S.haptics],
 ['B10','电量计','MAX17055ETB+T',null,'单节电池；I²C；配电流采样电阻','TDFN；以 ETB 封装图为准','ADI；首版可预留，后续改善电量百分比显示；不作为 UI 点亮的必要物料。','需核对电池电压范围、容量、终止电流和模型参数；省略时仅做 ADC 电压估计。','选配，暂不采购',null,null,S.gauge],
 ['B11','电量计采样电阻','10 mΩ；1%；额定功率按峰值电流确定',null,'与 B10 配套；Kelvin 取样','封装待电流 / 高度预算','低阻采样电阻，仅装配 B10 时需要。','阻值与量程按 MAX17055 配置匹配；考虑走线电阻和充放电峰值。','随电量计定型',null,null,S.gauge],
 ['B12','接口 ESD','TPD4E05U06DQAR',null,'4 通道；低电容保护','USON-10；DQA 封装图为准','TI；用于选定的外露信号接口，最终颗数按信号数量确定。','核对工作电压、钳位与被保护 IC 耐压；不将信号 ESD 管当成电池过压保护。','接口数量确定后采购',null,null,S.esd],
 ['B13','屏幕与触摸总成','优先复用现有整套总成',1,'显示总线、触摸总线及电源均待识别','保留当前安装边框和排线位置','先保留现有屏幕、盖板和触摸的装配关系，以减少结构修改。','必须拿到驱动 IC、实际分辨率、FPC 定义、电压和初始化序列；不能由“S11 46 mm”推断。','优先复用，暂不买新屏',null,null,null],
 ['B14','屏幕电源 / 背光','由 B13 确认后选型',null,'OLED 偏压或 TFT 背光方案待定','待屏幕需求与布局','依据现有屏幕实际类型选择供电；不预设它是 AMOLED 或原装 Apple 屏。','区分总成内已有电源与原主板上的电源；补齐上电时序、复位及调光要求。','识别屏幕后定型',null,null,null],
 ['B15','触摸电平适配','仅必要时增加电平转换',null,'可能是 I²C + INT + RESET，待实测','按接口电平和空间选择','优先直接使用总成内触摸控制器，避免另做触摸板。','核对逻辑电压、上拉电源及断电反灌；不能向未知接口直接施加 3.3 V。','识别接口后定型',null,null,null],
 ['B16','电池','优先复用现有带保护电池',1,'额定容量、标称电压和终止电压待核实','尺寸、厚度和插头按实物','保留既有电池仓及安装位置；电池健康状态合格后使用。','记录标签与引线定义，核查保护电路及 NTC；不根据商品名推断容量和充电电压。','参数 / 状态合格后复用',null,null,null],
 ['B17','电池温度检测','匹配现有 NTC 或新增贴近电池的 NTC',null,'与 B05 TS 网络匹配','阻值 / B 值 / 封装待核实','为充电温度检测提供实物温度反馈。','已有温度线优先核对复用；新增 NTC 需匹配 BQ25180 设置与充电电路参考设计。','随电池和充电设计定型',null,null,S.chargerSpec],
 ['B18','表冠与侧键','优先复用机构及可独立复用的电气件',1,'旋转传感器 / 按键接口待识别','轴心、按压行程、固定位置按实物','机械表冠和侧键位置固定，主板应适配它们的位置。','若编码器焊在旧主板，需转接或同尺寸替代；不能假定一定输出 A/B 两相。','拆机确认后复用 / 替代',null,null,null],
 ['B19','振动器','优先复用现有马达',1,'ERM / LRA 待识别','保留现有固定槽及接触方式','先保留可装入现有马达仓的器件，配 B09 调试。','若仅有 ERM，接受首版触感限制；需要换 LRA 时重新核对空间和固定刚度。','测量后决定复用',null,null,S.haptics],
 ['B20','充电接入组件','优先复用现有触点或独立接收组件',1,'输入方式与输出电压待测','保持现有底盖和充电位置','先辨认磁吸触点还是无线接收。台架阶段可从外部受控 5 V 给 B05 供电。','若无线接收控制在旧主板内，只保留线圈不能完成充电；需另选完整接收电路。','确认独立供电能力后复用',null,null,null],
 ['B21','BLE 天线','现有可分离天线或新 FPC 天线',1,'2.4 GHz；匹配网络待实测','与外壳金属和电池共同评估','在保留整机空间的前提下匹配射频通路。','旧 PCB 天线会随旧主板一起移除；完整封壳后重新测匹配和实际连接距离。','拆机 / 射频布局后定型',null,null,S.soc],
 ['B22','显示 / 按键连接器','与实物排线匹配的连接器',null,'针数、间距、触点朝向待测','高度、锁扣与插入方向待测','保留原排线折弯和装配路径；能够减少外壳修改。','不能按“常见 0.5 mm FPC 座”直接买；确认每个针脚后再绘制原理图符号。','暂不采购',null,null,null],
 ['B23','转接 FPC / 小板','按原排线位置定制',null,'短距离信号与供电转接','厚度 / 弯曲半径 / 局部高度待测','用于旧器件位置与新主板布局不一致的地方。','总线速度、电源压降、装配高度决定能否转接；不预先承诺只需一块平板。','需要时打样',null,null,null],
 ['B24','主板 PCB','按现有主板包络定制',1,'层数由布线、阻抗和装配确定','轮廓 / 板厚 / 禁布区均待实测','先做 1:1 轮廓和厚度样件试装，再制作电路板。','初步评估 4–6 层；这不是已定制板参数，不能默认采用通用 1.6 mm 厚度。','试装通过后打样',null,null,null],
 ['B25','电源电感 / 功率电容','跟随 B05 / B06 / B07 的参考电路',null,'容量、感量、纹波和饱和电流待计算','按高度预算选择','覆盖电源环路和各电源输入 / 输出旁路。','需按真实负载展开到独立位号、MPN 和数量；当前条目不是可投产的阻容清单。','原理图完成后展开',null,null,S.buckboost],
 ['B26','去耦 / 上拉 / 串联电阻','按 MCU / Flash / 接口参考设计展开',null,'工作电压和总线速度决定数值','优先可装配的小封装','按各电源域和引脚要求放置；包含晶体预留电容位置。','不要将一组“通用阻容包”视为最终 BOM；需核对启动脚、I²C 上拉及屏幕信号完整性。','原理图完成后展开',null,null,S.soc],
 ['B27','Flash / 屏幕电源开关','按低功耗与启动要求预留',null,'负载电流、漏电和使能电平待定','按电源树和空间选择','控制可关闭外设，降低关机漏电；避免外设反灌。','Flash 断电控制需符合 SiFli Boot ROM 要求；料号和装配数量随电源树冻结。','电源树完成后定型',null,null,S.soc],
 ['B28','射频匹配无源器件','按参考设计预留可调 π 网络',null,'2.4 GHz；50 Ω 通路设计','以射频参考布局为准','匹配天线、馈线和外壳环境。','元件值必须在完整外壳和电池状态下测定；不能由开发板元件值直接保证性能。','射频验证后定值',null,null,S.soc],
 ['B29','下载 / 调试触点','板上测试点，配外部治具',1,'GND / 参考电压 / SWD 或 UART / RESET','测试点位置、间距随板确定','首版保留可恢复下载路径和关键电源测试点。','避免仅依赖蓝牙升级；触点可达性需在装配前确认。','随 PCB 实现',null,null,S.soc],
 ['B30','绝缘 / 固定 / 密封','匹配现有机壳的胶、垫片和螺钉',1,'机械与绝缘附件，一套','厚度和压缩量按实物','保持电池绝缘、板子固定和屏幕装配。','复装后的防水性需要另行测试，不能沿用商品宣传；可能需要薄支架或垫片。','试装后确定用量',null,null,null]
];

const reuse = [
 ['目标实物','华强北“Apple Watch Series 11 GPS 46mm Smartwatch”','名称按现有商品记录。其内部规格没有经拆机核实。','使用实物主板和总成作为定位基准，不套用 Apple 原装主板尺寸或接线。','用户提供；2026-09-11'],
 ['外壳 / 表带','优先全部保留','需要旧板轮廓、孔位、屏幕窗口和主板可用高度。','可省外观设计；内部布置、板厚和避让仍要核对。','实物测量待补'],
 ['显示 + 触摸 + 盖板','优先保留整套','记录排线正反面丝印、触摸 IC、针脚、工作电压、初始化和像素尺寸。','这是主控冻结前置条件。驱动不可得时，才评估相同总成的可驱动替代。','实物测量待补'],
 ['屏幕性能','先测现有屏幕上限','测刷新率、总线时钟、触摸采样和黑色显示效果。','现有屏若是低刷新 TFT，保壳方案可能限制界面观感；GPU 无法改变面板本身的上限。','工程判断，待实测'],
 ['表冠 / 按键','保留外部机构，电气件按条件复用','确认是旋转编码器、光学 / 磁传感器或仅按键；量轴高与按压行程。','只要旋转检测可用，就可映射滚动；旧板焊接器件需考虑转接与机械固定。','实物测量待补'],
 ['电池','参数与状态合格后复用','标签、尺寸、实际电压、保护板、NTC 和引脚定义。','据此设定充电终止电压和电流；换充电 IC 不代表电池参数已知。',S.chargerSpec],
 ['振动器','先识别再决定','型号、引线、DC 电阻、工作电压；若为 LRA，需谐振参数。','尽量复用其固定槽；高质量触感取决于驱动、马达和机械耦合。',S.haptics],
 ['充电底座 / 接收件','能独立提供合适直流输出时复用','辨认有触点的磁吸充电与无线感应；检查接收电路位于何处。','接收控制若在旧板上，换板必须补上；线圈不等于完整充电模块。','实物测量待补'],
 ['天线','可分离时优先复用','辨认 FPC / 弹片 / PCB 天线，记录馈点与接地位置。','外壳、主板地和电池变化会影响匹配，需要重新测量。',S.soc],
 ['心率 / 血氧','首版 UI 阶段暂不新增','保留后盖窗口；原器件需确认真实芯片、光学结构和可用驱动。','后续做真实测量时独立选型验证，不能直接沿用原表显示的数字作为准确性证据。','工程范围；未做传感器验证'],
 ['麦克风 / 扬声器','首版可保留位置并不装新音频电路','尺寸、阻抗、偏置、声孔结构及接口。','界面验证先完成触摸 / 表冠 / 显示；通话会增加固件、电源和声学工作量。','工程范围；后续选配'],
 ['“GPS” 标称','暂不据此选 GNSS 芯片','该商品名不能证明实物有独立 GNSS 芯片或天线。','首版可经手机同步位置；若要求独立定位，再做天线和 GNSS 专项选型。','用户提供的商品名；能力未核实'],
 ['第一轮装配样件','先做无电路的 1:1 轮廓 / 厚度模型','测原板厚度、最高器件、连接器高度、线缆折弯和电池净空。','合盖不压屏、不压电池、不顶按键后，再确定正式 PCB 厚度和布局。','工程建议'],
 ['备选主控条件','屏幕接口不匹配时改选','若为 MIPI-DSI 或超出选定 SF32LB52 型号显示能力，重新评估 SF32LB58x。','这是一条替代分支；当前不同时采购两种主控。',S.chips]
];

const procure = [
 ['P01','独立 UI 验证','SF32LB52-DevKit-LCD，N16R8 配置',1,'先买 1 套','官方 SDK 已有对应 BSP；16 MB Flash / 8 MB PSRAM 配置适合开始 UI 工作。','采购时核对套装确含匹配显示屏及转接件；开发板仅在台架使用，不装进表壳。',S.kit],
 ['P02','开发板配套屏','LCD_1P85_390*450_DevKit_Adapter_V1.0.0',null,'随 P01 核对，不重复买','官方 P01 文档默认的 1.85 英寸 390 × 450 QSPI 显示组合。','这是已知驱动的开发屏，不保证能装入现有手表；P01 已含则不另买。',S.kit],
 ['P03','现有手表','用户已有华强北 S11 46 mm 手表',1,'已有，先保留完整记录','作为外壳、屏幕、排线、电池、按键和马达的实物来源。','拆开前记录现有工作状态；拆后以照片、标尺和实测建立连接表。','用户已购实物'],
 ['P04','接口识别 / 转接','与实物排线匹配的转接板',null,'暂不采购','识别屏幕后用于从开发板驱动现有屏幕。','针数、间距、接触面和电源未确认前，不购买通用 FPC 座。','实物信息待补'],
 ['P05','焊接板上核心 IC','B01–B12 的适用项',null,'屏幕和电源确认后少量采购','将已验证的芯片与实际屏幕、马达、电池组合到自研板。','不应把本页开发板与装机 BOM 加总成一块手表成本。','对应 BOM 官方来源'],
 ['P06','机械适配样件','1:1 主板轮廓 / 厚度模型',1,'量出尺寸后制作','先验证螺钉、连接器、电池净空与合盖干涉。','可采用纸板 / 薄板或打印样件；材料厚度须匹配测量目标。','工程建议'],
 ['P07','测量与调试工具','优先复用现有仪器',null,'先检查已有工具','万用表、显微观察、受控电源、卡尺；低速总线可用逻辑分析仪。','高速度差分总线不能默认由普通逻辑分析仪解析；按实际总线选择测量方法。','工程建议']
];

// v0.2: all electronics and enclosure are newly selected or designed.
// Keep the preceding v0.1 data as a migration baseline, then replace affected rows.
Object.assign(S,{
 screen:'https://wiki.sifli.com/en/board/sf32lb52x/SF-DevKit-LCM-Adapter.html',
 driver:'https://github.com/OpenSiFli/SiFli-SDK/blob/main/customer/peripherals/display/co5300/co5300.c',
 upgrade:'https://www.sifli.com/en/sf32lb58x',
 hall:'https://www.ti.com/lit/ds/symlink/tmag5273.pdf',
 button:'https://tech.alpsalpine.com/e/products/category/tact-switch/sub/02/series/sksc/',
 battery:'https://www.eemb.com/product-166',
 motor:'https://www.precisionmicrodrives.com/datasheets/C10-100%20-%20datasheet-006.pdf',
 antenna:'https://www.johansontechnology.com/products/antennas/rf-antennas/2450at18a0100001e/',
 ppg:'https://www.analog.com/en/products/max30101.html',
 gnss:'https://www.u-blox.com/en/product/mia-m10-series',
 hfp:'https://docs.sifli.com/projects/sdk/latest/en/sf32lb52x/example/bt/hfp/README.html',
 audio:'https://www.ti.com/product/TPA2011D1'
});
function select(id,module,part,qty,iface,size,why,check,order,source){bom[Number(id.slice(1))-1]=[id,module,part,qty,iface,size,why,check,order,null,null,source];}
bom[0][4]='3.3 V；QSPI / I²C / SPI / 双模蓝牙';
bom[0][7]='先与 B13 测真实 UI。复杂全屏动画未达标时，整组升级 SF32LB58x + MIPI-DSI 屏；两种主控不是原位替换。';
bom[0][8]='先买开发套装验证';
bom[4][7]='按 B16 成品电池包正式规格配置终止电压、电流、NTC 和保护。芯片的 1 A 上限不是小电池的允许充电电流。';
bom[6][6]='仅当新屏总成或扩展外设确需独立 1.8 V 电源时装配；先核对总成供电，避免重复稳压。';
bom[6][7]='与主控电压域和供电能力一起确认；PPG 扩展也可使用其配套 PMIC 的 1.8 V 输出。';
bom[6][4]='降压；拟输出 1.8 V，选配';
bom[8][7]='与 B19 LRA 配套，按额定 RMS、过驱动和制动参数配置，在实际壳体中校准。';
bom[8][8]='与 LRA 一起备样';
bom[9][3]=1;
bom[9][6]='可佩戴版建议装配，用完整充放电校验电量显示。';
bom[9][8]='随电池包备样';
bom[10][3]=1;
select('B13','显示 / 触摸总成','ZC-A1D85W-010',1,'1.85 英寸 AMOLED；390 × 450；QSPI + I²C','圆角矩形；盖板、FPC 与厚度按正式图纸','SiFli 官方配套屏；CO5300AF-01 显示、FT6146-M00 触摸、BV6802W 电源。','先核对交付版本、初始化、最大时钟、扫描频率、TE 和电源。获取完整外形图再设计壳体，不套用其他 1.85 英寸屏尺寸。','先买配套屏样品',S.screen);
select('B14','屏幕电源 / 触摸配套','BV6802W / FT6146-M00 等随 B13 核对',null,'按总成的 VCI / IOVCC / TP 供电要求','优先由屏幕总成包含','核对总成内已集成器件、外部去耦和上电控制。AMOLED 没有 TFT 背光，但仍需偏压。','不重复购买已包含的芯片；根据实物交付电路补齐电源和接口适配。','总成包含项不单独下单',S.screen);
select('B15','表冠角度传感器','TMAG5273A1QDBVR',1,'1.7–3.6 V；I²C + INT','SOT-23-6；含引脚约 2.9 × 2.8 mm','3D Hall 与角度计算，配径向磁化磁铁实现连续旋转检测。','磁铁、间距、旋转平面和按压位移需标定。检查 LRA、充电磁铁干扰；低功耗电流随采样方式变化。','先做表冠样件',S.hall);
select('B16','保护电池包','以 EEMB LP372435TB 为电芯的成品包',1,'3.7 V 标称；300 mAh；要求 PCM + NTC','裸电芯约 24.5 × 36 × 4.0 mm；成品包更大','由供应商组包，包含保护电路、绝缘、引线和温度检测。','官网型号是裸电芯。成品包订单号、充电参数、峰值电流及最终尺寸须确认；不要按裸电芯尺寸封死电池仓。','询带保护电池包样品',S.battery);
select('B17','电池温度检测','随 B16 的 NTC 与 B05 TS 网络',null,'阻值与 B 值按电池包规格','贴近电芯；引线绝缘','成品包已包含 NTC 时不重复采购，主板补齐匹配网络。','按电池允许充电温度设置并测试，不能用 MCU 芯片温度替代电芯温度。','随电池包定型',S.chargerSpec);
select('B18','表冠按压 / 侧键','SKSCLDE010',2,'GPIO 按键；侧向触发','3.5 × 3.55 × 1.25 mm；行程 0.2 mm','ALPS Alpine 1.6 N 侧按开关，分别用于表冠按压与侧键。','设计轴向导向、机械限位和防尘密封，避免开关承担过载；固件需消抖。','结合机构样件备样',S.button);
select('B19','线性振动器','Precision Microdrives C10-100',1,'LRA；约 175 Hz；配 B09','直径 10 mm；厚度约 3.7 mm','有正式规格的 LRA，评估点击、表冠滚动与通知振动。','按批次规格限制 RMS 和过驱动，在实际壳体质量与固定方式下校准；不能仅凭器件保证 Taptic Engine 触感。','与驱动一起备样',S.motor);
select('B20','充电接入 / 底座','新设计 5 V 两触点磁吸底座',1,'表体触点 + 底座 Pogo Pin；限流 5 V','触点行程、磁铁与排水按结构设计','首版选接触充电，减少接收线圈、控制芯片和隔磁片占用。','不是 Apple 磁充协议。检查短路、反接、腐蚀及磁铁对 B15 的干扰；Pogo Pin 料号随行程确定。','结构后制作',null);
select('B21','2.4 GHz 天线','Johanson 2450AT18A0100001E',1,'2.4–2.5 GHz；匹配到 RF 口','1206；地与净空按厂商布局','用于双模蓝牙，作为独立器件选型基线。','芯片本体小不代表净空小；保留非金属射频区域，完整封壳和佩戴后重新匹配与测连接。','RF 布局后备样',S.antenna);
select('B22','显示 / 小板连接器','按新屏总成和新小板接口选座',null,'针数、间距、接触面由图纸决定','高度与 FPC 弯折共同核算','显示使用总成适配的连接器，小板按新设计选连接方式。','开发板 22p 接口不是裸屏排线定义，不能直接照抄作为装机屏座。','总成图纸后定 MPN',S.screen);
select('B23','内部 FPC / 表冠小板','按新壳结构定制',null,'I²C / GPIO / 供电；显示链路另核对','厚度、弯曲半径、插拔方向待 CAD','连接表冠、主板和屏幕，保持组件可装配。','先短线与机构样件验证，再做薄 FPC；投产前展开独立图号和用量。','机构验证后打样',null);
select('B24','主板 PCB','自研 4–6 层；板厚初估 0.8 mm',1,'最终层叠 / 阻抗由布线和加工确定','轮廓随屏、电池和机构布局设计','首轮保留物理下载、电源测试点和分路电流测量位置。','0.8 mm 是预算而非冻结参数；QFN 0.35 mm 与 DSBGA 需核实贴片厂工艺和过孔方案。','UI 与结构验证后投板',null);
select('B30','结构与装配附件','自研壳、后盖、支架、表冠轴与磁铁；新购表带',1,'机械图号、材料和数量待 CAD','表体初估 46–48 × 40–42 × 13–15 mm','先打印试装，围绕新屏、电池包设计。表带初选标准 22 mm 快拆接口。','尺寸是包络假设，不是已验证 CAD；不含表冠和表带。需预留电池膨胀间隙、密封、螺柱、LRA 和天线净空。','试装后定图纸与用量',null);
reuse.splice(0,reuse.length,
 ['整机范围','电子器件与外壳全部重新选型 / 设计','本版覆盖界面核心整机；心率、GNSS 和通话为扩展候选。','第一阶段实现显示、触摸、表冠、振动、抬腕、蓝牙与电池系统。','需求范围；2026-09-11'],
 ['主控选择','先验证 SF32LB52JUD6；必要时升级 SF32LB58x','SF52 有图形加速、PSRAM 与双模蓝牙；SF58 增加 MIPI-DSI 等显示接口。','复杂全屏效果要求稳定 60 fps 时，优先评估 SF58 + 确认过的 MIPI 屏组合；不是原位替换。',S.upgrade],
 ['屏幕总成','ZC-A1D85W-010，标准盖板优先','官方已给出驱动、触摸与电源芯片，开发 BSP 有对应组合。','先获取同版本外形图和完整电路。开发转接板不装入表体；定制曲面玻璃会增加贴合和打样工作。',S.screen],
 ['界面性能','用真实 UI 帧时间作选型门槛','390 × 450 RGB565 单帧 351000 B；60 fps 需要 21.06 MB/s。官方默认 QSPI 50 MHz，理论 25 MB/s。','需求占理论 84.24%。假设有效系数 70%，传输上限约 49.86 fps；局部更新有帮助，不能保证重转场全屏 60 fps。',S.driver],
 ['表冠体验','TMAG5273 + 径向磁铁 + 侧按开关','低摩擦轴、导向、按压限位及磁铁间距需要联合设计。','无机械档位，滚动刻度由 LRA 反馈。测慢转、快转、换向、边按边转与充电底座磁干扰。',S.hall],
 ['电池与续航','300 mAh 保护电池包为初始容量','LP372435TB 是裸电芯型号，成品包额外占据保护板、引线和绝缘空间。','按 80% 可用容量，24 h 要平均电流 ≤10 mA；48 h 要 ≤5 mA。必须按电池侧测量并计入亮屏占空比。',S.battery],
 ['触感','DRV2605L + C10-100','马达直径约 10 mm、厚 3.7 mm，先考虑与电池并排避免叠高。','闭环驱动、固定方式和整机重量共同决定触感，需佩戴样机评价。',S.motor],
 ['充电','首版用新设计 5 V 磁吸触点','Pogo Pin 和磁铁配自制底座，BQ25180 管理充电与电源路径。','无线充电后续需另加接收电路、线圈、隔磁片与热测试；当前不假设兼容 Apple 充电器。',S.chargerSpec],
 ['结构包络','初估 46–48 × 40–42 × 13–15 mm','不含表冠和表带，是布局起点，不是已验证的最终尺寸。','屏总成决定窗口，电池包和主板决定层叠。先做实际厚度的 1:1 试装，再精修外观与密封。','工程假设；后续 CAD 与试装确认'],
 ['天线与材料','首版非金属壳或明确的 RF 窗口','芯片天线需要厂商规定的净空和地；金属壳会影响辐射与匹配。','完整屏、电池、后盖及佩戴状态下测匹配和连接；射频窗口需在结构初期预留。',S.antenna],
 ['心率 / 血氧扩展','MAX30101 + MAX14750A，先做原始 PPG','集成光学模块，参考电路提供独立 1.8 V 与 LED 电源。','另做贴肤小板、遮光、光学窗、运动伪影和算法验证；不是接上 I²C 就能得到可靠腕部读数。',S.ppg],
 ['独立 GNSS 扩展','u-blox MIA-M10Q-00B','约 4.5 × 4.5 × 1.0 mm；UART / I²C，仍需独立 GNSS 天线与供电设计。','按运动记录时段开启，单独测耗电和佩戴接收效果；芯片功耗不能代表整机定位模式。',S.gnss],
 ['音频 / 通话扩展','片上 CODEC + TPA2011D1；MIC / 喇叭后定','SF52 是双模蓝牙平台，官方有 HFP HF 示例，具备继续做蓝牙通话的基础。','需要音频路由、回声抑制、声孔、声腔与手机兼容验证。当前是扩展评估，不是完整音频采购 BOM。',S.hfp+'\n'+S.audio],
 ['固件与边界','SiFli SDK 对应 RTOS + LVGL + 自研 UI','实现表盘、应用网格、通知、卡片、滚动动效；需接硬件加速并管理资源缓存。','可重现主要界面和交互风格；运行自研固件，不能安装 watchOS。手机通知与数据同步仍需手机侧适配。',S.chips]
);
procure.splice(0,procure.length,
 ['P01','UI 性能验证','SF32LB52-DevKit-LCD N16R8 配套屏套装',1,'先验证','官方 BSP 支持对应显示组合，建立渲染、传输和触摸性能基线。','开发板只有 8 MB PSRAM，目标主板为 16 MB；记录缓存差别，开发板耗电不能直接当作整机耗电。',S.kit],
 ['P02','装机屏样品','ZC-A1D85W-010 总成 + 正式图纸',1,'核对套装后购样','拿到盖板、FPC、连接器、电源和驱动资料，作为壳体设计输入。','P01 已含同版本总成则不重复买；不把开发转接板尺寸当作屏总成尺寸。',S.screen],
 ['P03','表冠验证','TMAG5273 评估小板 + 轴机构与磁铁',1,'与 UI 并行','用真实输入调滚动速度、方向、按压及休眠唤醒。','这一项是一套研发工装；磁铁尺寸和间距需要机械容差试验，不能直接视为成品表冠。',S.hall],
 ['P04','触感验证','DRV2605L 评估板 + C10-100',1,'与表冠一起验证','测点击、刻度、通知和制动。','按马达规格限幅，在接近实际整机质量和固定方式的样件上测试。',S.motor],
 ['P05','电源验证','300 mAh 保护电池包 + 充电 / 稳压测试板',1,'电池规格确认后','测白屏、无线收发、振动同时工作的峰值与充电温升。','匹配 NTC 和充电参数，以电池侧电流积分评估亮屏、关屏与通知等模式。',S.chargerSpec],
 ['P06','结构试装','打印壳、支架、表冠轴套与 PCB 厚度模型',1,'图纸和样品齐后','验证屏、电池包、FPC、LRA、按钮与连接器的装配关系。','先做 1:1 干涉检查和实装，再冻结外壳；尚未开模或采购定制玻璃。','工程建议'],
 ['P07','自研 PCBA','B01–B30 适用项；SF58 + MIPI 为升级分支',null,'验证后投板','界面帧时间、表冠、触感和结构通过后，展开原理图与逐位号 BOM。','若复杂动画不达标，先升级显示链路再投板。4–6 层、0.8 mm 仍为初步 PCB 参数。',S.upgrade]
);

const wb = Workbook.create();
const bomSheet = wb.worksheets.add('BOM初选');
const reuseSheet = wb.worksheets.add('整机评估');
const procureSheet = wb.worksheets.add('先行验证');
const budgetSheet = wb.worksheets.add('资源预算');
const palette = {text:'#18202B',muted:'#606B78',head:'#263445',line:'#CED5DD',input:'#FFF2CC',stripe:'#F4F6F8',calc:'#EDF3F8'};
const font = 'Arial'; // Arial verified in the Windows font directory. Chinese falls back to the system CJK font.
function c(index){let s='';for(let i=index+1;i;i=Math.floor((i-1)/26))s=String.fromCharCode(65+(i-1)%26)+s;return s;}
function setup(sheet,title,sub,note,headers,widths,rows,tableName){
  const last=c(headers.length-1),end=5+rows.length;
  sheet.showGridLines=false;
  const used=sheet.getRange(`A1:${last}${end}`);
  used.format.font={name:font,size:11,color:palette.text};
  used.format.verticalAlignment='top';used.format.wrapText=true;
  widths.forEach((w,i)=>sheet.getRange(`${c(i)}1:${c(i)}${end}`).format.columnWidthPx=w);
  sheet.getRange('A1').values=[[title]];
  sheet.getRange('A1').format.font={name:font,size:17,bold:true,color:palette.text};
  sheet.getRange(`A1:${last}1`).format.rowHeightPx=34;
  sheet.getRange('A1').format.wrapText=false;
  sheet.getRange(`A1:${last}1`).format.borders={bottom:{style:'thin',color:palette.line}};
  sheet.getRange('A2').values=[[sub]];sheet.getRange('A3').values=[[note]];
  sheet.getRange('A2:A3').format.wrapText=false;
  sheet.getRange(`A2:${last}3`).format.font={name:font,size:11,color:palette.muted};
  sheet.getRange(`A2:${last}3`).format.rowHeightPx=24;
  sheet.getRange(`A4:${last}4`).format.rowHeightPx=10;
  sheet.getRange(`A5:${last}5`).values=[headers];
  if(rows.length)sheet.getRange(`A6:${last}${end}`).values=rows;
  sheet.getRange(`A5:${last}5`).format={fill:palette.head,font:{name:font,size:11,bold:true,color:'#FFFFFF'},wrapText:true,rowHeightPx:40,verticalAlignment:'center'};
  sheet.getRange(`A5:${last}5`).format.borders={insideVertical:{style:'thin',color:'#FFFFFF'}};
  if(rows.length){sheet.getRange(`A6:${last}${end}`).format.rowHeightPx=92;for(let row=6;row<=end;row+=2)sheet.getRange(`A${row}:${last}${row}`).format.fill=palette.stripe;}
  const table=sheet.tables.add(`A5:${last}${end}`,true,tableName);table.showFilterButton=true;
  sheet.freezePanes.freezeRows(5);
  return {end,last};
}
const bomMeta=setup(bomSheet,'自研智能手表整机 BOM 初选','v0.2，2026-09-11。屏幕、电子器件和外壳全部重新选型；先完成界面核心整机。','价格空白表示未询价；组合件、无源器件与机构尚需展开，不能直接用于贴片采购。',
 ['编号','模块','候选型号 / 规格','单机用量','接口 / 电源','封装 / 外形','用途与选型依据','采用前必须确认','采购安排','含税单价\n元','小计\n元','官方来源 / 依据'],
 [62,120,225,80,190,195,280,330,175,105,105,440],bom,'WatchBOM');
bomSheet.getRange(`D6:D${bomMeta.end}`).format.numberFormat='0';
bomSheet.getRange(`D6:D${bomMeta.end}`).format.horizontalAlignment='center';
bomSheet.getRange(`J6:K${bomMeta.end}`).format.numberFormat='"¥"#,##0.00';
bomSheet.getRange(`J6:J${bomMeta.end}`).format.fill=palette.input;
bomSheet.getRange(`K6:K${bomMeta.end}`).format.fill=palette.calc;
bomSheet.getRange(`L6:L${bomMeta.end}`).format.font={name:font,size:10,color:'#355F81'};
bomSheet.getRange('K6').formulas=[['=IF(AND(ISNUMBER(D6),ISNUMBER(J6)),D6*J6,"")']];
bomSheet.getRange(`K6:K${bomMeta.end}`).fillDown();
const sumRow=bomMeta.end+2;
bomSheet.getRange(`G${sumRow}`).values=[['已填单价项目小计']];
bomSheet.getRange(`H${sumRow}`).values=[['未报价、选配和待定项目未计入；不是整机总价。']];
bomSheet.getRange(`K${sumRow}`).formulas=[[`=IF(COUNT(K6:K${bomMeta.end})=0,"",SUM(K6:K${bomMeta.end}))`]];
bomSheet.getRange(`G${sumRow}:L${sumRow}`).format={font:{name:font,size:11,bold:true,color:palette.text},wrapText:true,rowHeightPx:45};
bomSheet.getRange(`K${sumRow}`).format.numberFormat='"¥"#,##0.00';
bomSheet.getRange(`A${sumRow+2}`).values=[['库存和价格未确认；候选料号需经过屏幕兼容、供电和装配验证后才能冻结。']];
bomSheet.getRange(`A${sumRow+2}`).format.wrapText=false;

setup(reuseSheet,'整机可行性与扩展选型','围绕显示、交互、续航和装配联合设计电子与结构。','尺寸、效率与续航条件为设计假设，尚未完成实机测试。',
 ['项目','建议','依据与边界','对方案的影响 / 验证要求','来源'],[150,245,350,440,430],reuse,'SystemAssessment');
setup(procureSheet,'先行验证清单','先验证真实显示、表冠、振动、电源和结构，再冻结主板。','研发物料不与单机 BOM 相加；目前没有下单或联系供应商。',
 ['编号','用途','型号 / 项目','数量','安排','选择理由','核对事项','来源'],[65,150,290,65,160,335,380,440],procure,'PrototypeList');
procureSheet.getRange('D6:D12').format.numberFormat='0';
procureSheet.getRange('D6:D12').format.horizontalAlignment='center';

const budgetRows = [
 ['显示宽度',390,'px','B13 候选屏的已知像素宽度。'],
 ['显示高度',450,'px','B13 候选屏的已知像素高度。'],
 ['每像素字节数',2,'B/px','示例 RGB565；ARGB8888 使用 4。'],
 ['帧缓冲数量',2,'个','用于双缓冲容量预算，具体渲染模式由驱动决定。'],
 ['目标刷新率',60,'fps','设计目标，尚未在实物验证。'],
 ['显示总线时钟',50,'MHz','官方 co5300.c 默认 50 MHz，并注明 RGB565 限制。需锁定 SDK 与面板版本后核对。'],
 ['每拍数据位数',4,'bit/clock','四数据线、单沿 QSPI，未假定 DDR 或超频。'],
 ['总线有效利用系数',0.7,'比例','工程计算假设，包含命令与传输间隙；不是实测值。'],
 ['单帧数据量',null,'B','宽 × 高 × 每像素字节数。'],
 ['帧缓冲总容量',null,'KiB','1 KiB = 1024 B。'],
 ['全屏刷新的数据量',null,'MB/s','十进制 MB/s；不包含额外渲染读写。'],
 ['总线理论吞吐',null,'MB/s','时钟 × 数据线位数 / 8。'],
 ['估算有效吞吐',null,'MB/s','只用于前期预算，不能据此保证 60 fps。'],
 ['刷新占估算吞吐',null,'比例','超过 100% 表示按当前效率假设不能达到全屏目标帧率；真实效率与渲染仍需实测。'],
 ['单帧时间预算',null,'ms','1 秒 / 目标帧率。'],
 ['实际触摸采样率',null,'Hz','待实测；不能用渲染帧率代替触摸输入速率。'],
 ['实测触摸到显示延迟',null,'ms','用同步触发或高速视频评估典型与 P95；初始交互目标 P95 ≤80 ms，尚未验证。'],
 ['设计用 PSRAM',16,'MiB','B01 合封容量。P01 为 8 MB，试验时需改成 8 并相应缩减下方缓存预算。'],
 ['双缓冲占用',null,'MiB','由本页帧缓冲公式计算。'],
 ['图像资源缓存预算',4,'MiB','工程初始预算，可调整。'],
 ['字体缓存预算',1,'MiB','工程初始预算，可调整。'],
 ['临时合成 / 解码预算',2,'MiB','工程初始预算，可调整。'],
 ['其他 PSRAM 预算',1,'MiB','不代表所有 DMA 或任务栈都可放在 PSRAM。'],
 ['已分配 PSRAM 预算',null,'MiB','各项预算合计。'],
 ['PSRAM 剩余预算',null,'MiB','不足时应调整图形资源或架构。'],
 ['PSRAM 剩余比例',null,'比例','预留余量供缓存峰值和后续功能。'],
 ['片内 SRAM',576,'KiB','芯片资料值；RTOS、协议栈、DMA 与双核分配须看 BSP 链接布局。'],
 ['设计电池容量',300,'mAh','按 B16 候选电芯容量预算，成品电池包规格仍待确认。'],
 ['电池可用容量系数',0.8,'比例','工程估算；受温度、老化与关机阈值影响。'],
 ['目标运行时间',24,'h','用于倒推平均电流上限。'],
 ['电池侧平均电流上限',null,'mA','要求在电池侧测量，以纳入各电源转换损耗。'],
 ['实测电池侧平均电流',null,'mA','留空直到完成指定亮屏、蓝牙与振动占空比测试。'],
 ['按实测电流估算续航',null,'h','估算值，仍需完整放电验证。']
];
setup(budgetSheet,'图形与电源资源预算','黄色为设计输入或待测数据，蓝灰色为公式结果；不是实测性能。','QSPI 50 MHz 为官方驱动默认值，效率 70% 为计算假设；帧率还受绘制、同步和面板限制。',
 ['参数 / 结果','值','单位','说明'],[260,140,100,690],budgetRows,'ResourceBudget');
budgetSheet.getRange('A6:D38').format.rowHeightPx=49;
budgetSheet.getRange('B6:B38').format.numberFormat='0.00';
budgetSheet.getRange('B6:B38').format.horizontalAlignment='center';
const inputRows=[6,7,8,9,10,11,12,13,21,22,23,25,26,27,28,33,34,35,37];
for(const row of inputRows)budgetSheet.getRange(`B${row}`).format.fill=palette.input;
const formulaMap={
 B14:'=B6*B7*B8',B15:'=B14*B9/1024',B16:'=B14*B10/1000000',B17:'=B11*B12/8',B18:'=B17*B13',B19:'=IF(B18>0,B16/B18,"")',B20:'=IF(B10>0,1000/B10,"")',
 B24:'=B15/1024',B29:'=SUM(B24:B28)',B30:'=B23-B29',B31:'=IF(B23>0,B30/B23,"")',B36:'=IF(B35>0,B33*B34/B35,"")',B38:'=IF(ISNUMBER(B37),IF(B37>0,B33*B34/B37,""),"")'
};
for(const [cell,f] of Object.entries(formulaMap)){budgetSheet.getRange(cell).formulas=[[f]];budgetSheet.getRange(cell).format.fill=palette.calc;}
for(const row of [13,19,31,34])budgetSheet.getRange(`B${row}`).format.numberFormat='0.0%';
for(const row of [6,7,8,9,10,11,12,14,21,23,32,33])budgetSheet.getRange(`B${row}`).format.numberFormat='#,##0';
budgetSheet.getRange('A40').values=[['软件与测试依据']];
budgetSheet.getRange('A40').format.font={name:font,size:12,bold:true};
budgetSheet.getRange('A41:D44').values=[
 ['LVGL 缓冲模式',null,null,S.lvgl],
 ['SiFli 电路和时钟',null,null,S.soc],
 ['单机软件架构',null,null,'SiFli SDK 对应 BSP + RTOS + LVGL。界面线程不阻塞采样；表冠 / 触摸事件排队处理，短中断，保留看门狗与恢复下载。'],
 ['实机验证项目',null,null,'测全屏滑动、图标缩放、卡片转场的帧时间；同时测 BLE 重连、任务栈余量、关屏电流、充电温升及完整外壳下的射频表现。']
];
budgetSheet.getRange('A41:D44').format={font:{name:font,size:11,color:palette.text},wrapText:true,rowHeightPx:64,verticalAlignment:'top'};
budgetSheet.getRange('A46:D49').values=[
 ['所需最低有效利用率',null,'比例','全屏目标数据量 / 理论带宽。还需绘制与 DMA 并发，不能只满足算术门槛。'],
 ['按假设效率的传输上限',null,'fps','仅为传输上限，实际还受面板扫描、渲染和同步限制。'],
 ['官方显示驱动',null,null,S.driver],
 ['屏幕总成资料',null,null,S.screen]
];
budgetSheet.getRange('A46:D49').format={font:{name:font,size:11,color:palette.text},wrapText:true,rowHeightPx:62,verticalAlignment:'top'};
budgetSheet.getRange('B46').formulas=[['=IF(B17>0,B16/B17,"")']];
budgetSheet.getRange('B47').formulas=[['=IF(B14>0,B18*1000000/B14,"")']];
budgetSheet.getRange('B46').format.numberFormat='0.00%';budgetSheet.getRange('B47').format.numberFormat='0.00';
budgetSheet.getRange('B46:B47').format.fill=palette.calc;budgetSheet.getRange('B46:B47').format.horizontalAlignment='center';

// Meaningful spot checks, then restore all user-facing prices and measured inputs.
function assertClose(label,actual,expected){if(Math.abs(Number(actual)-expected)>1e-7)throw new Error(`${label}: ${actual} != ${expected}`);}
assertClose('frame bytes',budgetSheet.getRange('B14').values[0][0],351000);
assertClose('double buffer KiB',budgetSheet.getRange('B15').values[0][0],685.546875);
assertClose('display MB/s',budgetSheet.getRange('B16').values[0][0],21.06);
assertClose('minimum efficiency',budgetSheet.getRange('B46').values[0][0],0.8424);
assertClose('transfer limit',budgetSheet.getRange('B47').values[0][0],49.85754985755);
assertClose('battery mA budget',budgetSheet.getRange('B36').values[0][0],10);
bomSheet.getRange('J6').values=[[42.5]];
assertClose('BOM line amount',bomSheet.getRange('K6').values[0][0],42.5);
bomSheet.getRange('J7').values=[[12.25]];
assertClose('BOM subtotal',bomSheet.getRange(`K${sumRow}`).values[0][0],54.75);
bomSheet.getRange('J6:J7').clear({applyTo:'contents'});
budgetSheet.getRange('B37').values=[[8]];
assertClose('runtime from measured input',budgetSheet.getRange('B38').values[0][0],30);
budgetSheet.getRange('B37').clear({applyTo:'contents'});
const inspection=await wb.inspect({kind:'table',range:'资源预算!A14:D20',include:'values,formulas',tableMaxRows:8,tableMaxCols:4,maxChars:3500});
console.log(inspection.ndjson);
const errors=await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:30},summary:'Final formula error scan'});
console.log(errors.ndjson);
await fs.mkdir(outDir,{recursive:true});
const previews=[
 ['BOM初选','A1:I14','v02-bom-top'],
 ['BOM初选','B18:I28','v02-bom-new'],
 ['BOM初选','I28:L37','v02-bom-cost'],
 ['整机评估','A1:D12','v02-assessment'],
 ['整机评估','A13:D19','v02-expansion'],
 ['先行验证','A1:G12','v02-prototype'],
 ['资源预算','A1:D20','v02-resources'],
 ['资源预算','A21:D49','v02-resources-lower']
];
for(const [sheetName,range,name] of previews){const blob=await wb.render({sheetName,range,scale:1,format:'png'});await fs.writeFile(path.join(supportDir,`${name}.png`),new Uint8Array(await blob.arrayBuffer()));console.log(`PREVIEW ${name}.png`);}
const result=await SpreadsheetFile.exportXlsx(wb);
await result.save(path.join(outDir,fileName));
await fs.writeFile(path.join(supportDir,'verification-v02.txt'),'Formula checks: frame 351000 B; buffer 685.546875 KiB; rate 21.06 MB/s; minimum efficiency 84.24%; transmission limit at 70% efficiency 49.85755 fps; battery budget 10 mA; BOM subtotal 54.75; runtime 30 h at 8 mA. Temporary inputs cleared.\n'+errors.ndjson,'utf8');
console.log(`OUTPUT ${path.join(outDir,fileName)}`);
