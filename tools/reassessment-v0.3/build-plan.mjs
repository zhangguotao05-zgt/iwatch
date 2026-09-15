import fs from 'node:fs/promises';
import path from 'node:path';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

const root = '.';
const outputDir = `${root}/hardware`;
const workDir = `${root}/tools/reassessment-v0.3`;
const date = '2026-09-11';
const workbookPath = `${outputDir}/自研智能手表_完整方案与选型BOM_v0.3.xlsx`;
const reportPath = `${outputDir}/自研智能手表_方案重新评估_v0.3.md`;

const sources = [
 ['S01','Apple Watch Series 11 技术规格','https://support.apple.com/en-la/125093','用于产品参照；不据此推断内部显示总线或自研产品性能。'],
 ['S02','Qualcomm W5/W5+ Gen 1 产品简报','https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-W5-Plus-Gen-1-Wearable-Platforms-product-brief.pdf','SW5100、QCC5100、PMW5100，主核 DSI / 协处理器 QSPI；AOSP/Wear OS 是平台支持范围。'],
 ['S03','Thundercomm TurboX W5+ 开发套件','https://www.thundercomm.com/zh/product/w5-development-kit/','公开配置与页面标价 USD 1,999；页面仍写 Android R/S (TBD)/Go；私有资料交付须确认。'],
 ['S04','UNISOC W377E 产品页','https://www.unisoc.com/en/product/SmartWearablesUS/W377E','4×A53 1.4 GHz、MIPI、LTE Cat.4；页面软件段含 W337/Android 8.1 文案，存在版本核实需求。'],
 ['S05','SiFli SDK','https://github.com/OpenSiFli/SiFli-SDK','RT-Thread 路线；不提供直接运行 Android APK 或 watchOS 应用的证明。'],
 ['S06','OPPO Watch X 功能与脚注','https://www.oppo.com/cn/accessories/oppo-watch-x/','官方微信独立使用案例；脚注要求 A.59+ 系统、手机微信 8.0.48+，仅全智能模式。仅对该产品有效。'],
 ['S07','OPPO Watch X 参数','https://www.oppo.com/cn/accessories/oppo-watch-x/specs/','2 GB + 32 GB 商业产品参照；不能证明自研板兼容其应用。'],
 ['S08','Google Mobile Services','https://www.android.com/gms/','GMS 不属于 AOSP；有 Android BSP 不等于取得 Google Play / Wear OS 商业分发条件。'],
 ['S09','Apple Developer Program 协议','https://developer.apple.com/support/terms/apple-developer-program-license-agreement/','Apple SDK 与系统面向 Apple 设备；本方案不以移植原版 watchOS 为实现路径。'],
 ['S10','荣耀手表微信支付说明','https://www.honor.com/cn/support/content/zh-cn15846628/','绑定穿戴设备后显示支付码；消费者手表支付与商户收款 API 不同。'],
 ['S11','vivo WATCH 2 功能说明','https://www.vivo.com.cn/vivo/vivowatch2/','不同产品微信能力有差异；该产品微信手表版要求 Android 手机并保持蓝牙连接。'],
 ['S12','奥视特 ET020AM03-HT','https://www.aoshite.net/productView1_1975.html','410×502、31 pin MIPI、800 nit、33.07×41.05 mm 为厂商标称；尚缺正式时序/初始化/供货确认。'],
 ['S13','ADI MAXM86146','https://www.analog.com/en/products/maxm86146.html','集成 MAX86141、MAX32664C、双光电二极管；仍需外部 LED、加速度数据和算法固件。'],
 ['S14','ADI MAXM86146EVSYS','https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/maxm86146evsys.html','提供评估板、设计文件、GUI 与固件条目；库存及实际下载资格未核实。'],
 ['S15','MAXM86146EVSYS Rev.1 手册','https://www.analog.com/media/en/technical-documentation/data-sheets/maxm86146evsys.pdf','第 13、25 页：SFH 7015、两颗绿光 LED、MAX14689、参考光学/电路连接。'],
 ['S16','ADI 加速度计兼容性 FAQ','https://ez.analog.com/other-products/a/documents/do21669/what-accelerometers-are-supported-by-the-max32664c-firmware','直接驱动 KX122/LIS2DS12，或由主机送入加速度样本；新 IMU 不能假设即插即用。'],
 ['S17','ST LSM6DSO','https://www.st.com/en/mems-and-sensors/lsm6dso.html','在产六轴 IMU，2.5×3×0.83 mm；功耗须按实际采样模式测量。'],
 ['S18','ST LIS2DS12 生命周期','https://www.st.com/en/mems-and-sensors/lis2ds12.html','官方列为 Obsolete；其推荐替代品不代表兼容 ADI 固件。'],
 ['S19','ADI MAX32664','https://www.analog.com/en/products/max32664.html','C 版本腕式心率/血氧；不同固件版本需匹配传感器组合。'],
 ['S20','ADI MAXREFDES105','https://www.analog.com/cn/resources/reference-designs/maxrefdes105.html','MAX86174A + MAX32664C 备选；页面标价 USD 326.08；软件/资料需 NDA 审核，申请不保证获批。'],
 ['S21','TI DRV2605L','https://www.ti.com/product/DRV2605L','LRA/ERM 闭环触觉驱动；最终触感取决于马达、结构及调校。'],
 ['S22','ams OSRAM SFH 7015','https://ams-osram.com/products/leds/multi-color-leds/osram-multi-chip-led-sfh-7015','655 nm 红光 + 940 nm 红外，2×0.8×0.6 mm，页面列为 Full production。'],
 ['S23','ams OSRAM CT DBLP31.12 数据手册','https://look.ams-osram.com/m/7a84e13abf7bd695/original/CT-DBLP31-12.pdf','绿光 LED 系列；参考板使用的完整分档尾码需再确认供货与光学一致性。'],
 ['S24','ADI MAX30003','https://www.analog.com/en/products/max30003.html','单通道 ECG AFE；不直接提供 Apple 的诊断/预警算法。'],
 ['S25','TI TMP117','https://www.ti.com/product/TMP117','温度传感器；封装温度精度不能等同腕部核心体温测量精度。'],
 ['S26','Bosch BMP390','https://www.bosch-sensortec.com/en/products/environmental-sensors/pressure-sensors/bmp390','气压/相对高度测量候选，需气孔和环境补偿。'],
 ['S27','Apple ANCS 规范','https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html','授权后读取 iOS 通知；不能据此实现完整微信聊天。'],
 ['S28','Android NotificationListenerService','https://developer.android.com/reference/android/service/notification/NotificationListenerService','手机伴随 App 的通知访问能力；需用户授权。'],
 ['S29','TI OPT3001','https://www.ti.com/product/OPT3001','环境光传感器，用于自动亮度候选。'],
 ['S30','Bosch BMM350','https://www.bosch-sensortec.com/en/products/motion-sensors/magnetometers/bmm350','磁力计候选；磁吸充电、扬声器和 LRA 都会影响校准。'],
 ['S31','NXP PN7160','https://www.nxp.com/products/PN7160','说明通用 NFC 控制器并不自动具备银行卡支付能力；不将此器件列为支付方案。'],
];
const sourceMap = Object.fromEntries(sources.map(s=>[s[0],s]));
const link = (id,label) => `[${label || sourceMap[id][1]}](${sourceMap[id][2]})`;

const routes = [
 ['A','方案厂穿戴参考设计','优先寻找已能演示官方微信和支付、可交付 BSP 的方案；芯片可为 W5 系列或展锐平台。','最接近完整功能目标；自研主板、外壳和系统界面继续保留。','当前尚未找到对本项目已承诺交付的方案厂或正式报价。','必须交付适用自研设备的应用接入路径、源码范围、参考电路和可运行样机。','优先落地方向；尚不能冻结采购','S02/S04/S06'],
 ['B','TurboX W5+ / SW5100','有公开开发套件；SW5100 + QCC5100，可开发 Android 与低功耗子系统。','适合建立主系统、显示、音频、联网及休眠验证环境。','官方应用接入未证明；公开套件软件文案版本较旧；关键资料有私有部分。','确认实际 Android/BSP、64 位 ABI、源码/刷机权限及应用版本后再采购。','首选具体工程候选','S02/S03'],
 ['C','UNISOC W377E','四核 A53 1.4 GHz、MIPI、LTE Cat.4，面向穿戴。','可询价的国内穿戴平台备选。','没有核实小批量套件和资料交付；公开 Android 文案不能作为当前应用兼容证明。','由方案厂交付当前可用 BSP 与微信/支付演示；与 W5 做同一负载功耗比较。','并行询价备选；不因品牌推断低价','S04'],
 ['D','SF32LB58x 单主控','RT-Thread + 图形框架，支持自研应用、传感器和蓝牙。','UI、表冠、触觉与健康数据原型仍有价值。','没有已核实的官方微信完整应用及支付接入组合；不能直接运行 Android/watchOS 安装包。','仅当方案厂提供符合全部验收项的定制接入证据时重新考虑。','退出当前完整整机主线','S05'],
];

const features = [
 ['F01','界面与交互','本机自研','蜂窝图标、列表、表盘、通知、控制中心、表冠缩放/滚动。','目标 60 fps；连续滚动 95% 帧≤16.7 ms；触控到画面 P95≤100 ms。','需在最终屏幕/功耗状态测量；这些是自定目标，不是 Apple 实测。','S01/S02'],
 ['F02','AOD 与抬腕','本机自研 + 平台框架','息屏/常亮/抬腕转换；主处理器休眠时保留时钟与采样。','循环切换无白屏/花屏；测试显示归属和唤醒时延，并记录平均功耗。','双接口屏与 BSP 协作是关键；仅 MIPI 点亮不足以验收。','S02/S12'],
 ['F03','官方微信','外部应用接入','登录、文字/图片显示、语音收发、快捷回复；网络重连和后台收信。','在自研设备身份及计划交付系统上演示；屏幕关闭后持续接收，重启/升级后仍可用。','未核实；不得把通知转发、手机 APK 拉伸显示或改设备身份当成已通过。','S06/S11'],
 ['F04','支付宝与微信付款','外部支付接入','账号绑定、付款码更新、支付结果与解绑；明确是否支持离线使用。','取得适用本设备的接入方式，使用本人账号由用户完成实际小额验证。','未核实；商户收款 API、二维码图片和通用 NFC 都不等同消费者腕上付款。','S10/S11/S31'],
 ['F05','应用安装与更新','系统 + 应用服务','官方来源的兼容应用安装、启动、卸载和更新。','固定包版本/ABI/系统依赖，更新后复测；明确可获得的应用目录。','AOSP 不含 GMS；Wear OS/应用商店服务不能因买芯片自动取得。','S08'],
 ['F06','联网与同步','通信硬件 + 软件','Wi-Fi、蓝牙、天气/时间同步、云端上传；独立联网保留 LTE 目标。','无手机时分别验证 Wi-Fi/LTE；弱网重连、时间证书、后台待机。','蓝牙连接本身不等同互联网连接。','S02/S03'],
 ['F07','蜂窝通话与 eSIM','平台 + 运营商','VoLTE、短信和独立号码；一号双终端另行验证。','先在台架用适用频段 Nano-SIM；eSIM 须用实际 EID/IMEI 确认运营商可开通。','焊接 eUICC 不能保证运营商接受自研设备或开通一号双终端。','S03/S06'],
 ['F08','手机配对','自研伴随 App','iPhone/Android 的设备绑定、通知、设置、健康记录同步和 OTA。','两类手机分别验收；iOS 授权/后台限制必须实测。','不能借 Android 的验证结果推定 iPhone 同等可用；不依赖 Apple Watch App 原生配对。','S27/S28'],
 ['F09','心率与血氧','传感器 + 算法 + 光学','真实 PPG，HR/SpO2 数值、信号质量、佩戴状态。','开发期同步留原始数据；静止/运动/不同松紧分别测试，无效数据不显示成正常值。','血氧先验证静止测量；参考血氧仪对照只作工程一致性检查，不能证明全量程临床精度。','S13/S14/S16'],
 ['F10','运动与睡眠','本机算法','IMU 计步、抬腕、GNSS 轨迹、活动时长；睡眠记录/分期单独评估。','场景数据集与对照记录；区分原始传感器可用与算法有效。','不能因加入 IMU/PPG 就承诺 Apple 级睡眠、跌倒或疾病预警。','S17/S26'],
 ['F11','音频与触觉','本机硬件 + 平台','麦克风、扬声器、蓝牙耳机、语音消息、LRA 表冠反馈。','验证音频路由、回声、录音权限及各模式切换；触觉做封壳实测。','驱动芯片能运行不代表达到 Taptic Engine 的主观触感。','S03/S21'],
 ['F12','导航与环境','本机硬件 + 服务','GNSS、指南针、气压高度、自动亮度；地图服务独立接入。','佩戴状态测定位/天线，封壳后校准磁力计，验证地图来源与离线策略。','不默认已有地图或语音服务授权。','S02/S26/S29/S30'],
 ['F13','ECG 与腕温','后续集成','ECG 电极/AFE、腕温传感器、趋势记录；结构阶段预留。','ECG 先用模拟器和隔离测试链验证；腕温排查主板发热与环境变化。','保留在最终功能路线；不能直接宣称房颤诊断、核心体温或 Apple 算法等效。','S24/S25'],
 ['F14','NFC/公交/门禁','专项接入','按实际卡服务与安全架构选择控制器/SE/线圈。','逐一验证卡发行/绑定/扣费/解绑；区分普通 NFC 标签读写。','不把 NFC 器件参数当成支付和公交开卡资格。','S31'],
 ['F15','OTA 与恢复','本机系统','主系统、协处理器、健康固件和资源包的版本配套、回滚与恢复。','模拟升级掉电和版本不一致；始终保留可恢复下载路径。','第三方应用升级后要做回归测试；各芯片固件不能独立随意升级。','S02/S14'],
 ['F16','外壳与续航','自研结构 + 实测','约 46 mm 外观尺度，全部新设计；电池、天线、声孔、光学窗同步布局。','先 3D 体积样件，再带电封壳；正常使用 24 h 为第一轮设计目标。','厚度、电池容量、防水等级和最终续航尚未验证；不沿用 Apple 参数作承诺。','S01'],
 ['F17','Apple 专有生态','当前无可复现路径','watchOS、Apple Watch App 原生配对、Apple Pay、Siri/专有服务。','不计入本方案可交付承诺；替代服务按各自能力验收。','保持尽量接近的目标，同时明确不能实现原版软件生态的一比一兼容。','S09'],
];

// Rows describe one eventual watch. They are a selection-level BOM, not a production netlist.
const bom = [
 ['B01','应用处理器','SW5100（W5 Gen 1）',1,'BGA；封装/球图以交付资料为准','4×A53，最高 1.7 GHz；DSI/内存/音频等','主系统、UI、官方应用、LTE；优先从可交付的参考设计起步。','参考架构候选','确认 BSP、应用和芯片供货；不是裸芯片立即下单项。','S02/S03'],
 ['B02','低功耗协处理器','QCC5100（W5+ 配套）',1,'封装待参考设计','最高 250 MHz；FreeRTOS；显示/传感器接口','沿用平台休眠与双系统协作；不再叠加 SF58。','随参考设计','确认 SDK、显示归属切换和健康数据桥接；不要作为普通蓝牙音频芯片购买。','S02/S03'],
 ['B03','主电源管理','PMW5100 + 平台参考电源器件',1,'以供应商原理图和布局为准','多路电源、充电、电量/温度管理','整体复制经验证的上电时序和峰值供电设计。','随参考设计','旧 MCU 方案的 BQ25180/TPS63031 不能直接替代整个电源树。','S02'],
 ['B04','主内存','2 GB LPDDR4',1,'颗粒/封装待平台 AVL','内存总线及时序由 BSP 决定','与内存训练和 PCB 约束一并冻结；2 GB 为首轮配置。','容量选定；MPN 未定','取得合格物料表和 DDR 参数后选具体 MPN；不按通用品牌随意替换。','S02/S03'],
 ['B05','系统存储','32 GB eMMC 5.1',1,'颗粒/封装待平台 AVL','eMMC；分区由 BSP 决定','系统、应用、用户数据与升级空间。','容量选定；MPN 未定','确认启动配置、寿命和分区；不要误买 32 Gb（4 GB）。','S03'],
 ['B06','协处理器存储','32 MB Flash + 8 MB PSRAM',1,'按 W5+ 套件配置作参考','容量/总线按 QCC 子系统 BSP','作为一组计数；颗粒数和 MPN 由参考设计展开。','随参考设计','不能与主系统 2 GB/32 GB 混淆；不重复计算合封内存。','S03'],
 ['B07','时钟与射频配套','平台 TCXO/RTC/RF/WCN 配套',1,'按供应商推荐布局','频率、负载、供电、射频口以参考资料为准','包括晶体/滤波/PA 等实际所需器件；此处为一组。','MPN 未定','不沿用 SF58 的 48 MHz 晶体假设；取得 BOM 后逐项展开。','S02'],
 ['B08','显示与触摸','优先平台验证的方形 AMOLED；ET020AM03-HT 为备选',1,'候选面板标称 33.07×41.05×0.8 mm；总成另核对','410×502，31 pin MIPI；触摸协议/电压待手册','先用套件原屏；最终屏需验证主核 DSI 与低功耗模式配合。','面板候选，未冻结','ET020 尚未证明 QSPI/AOD 双系统兼容、60 Hz 时序和现货；连接器不能按 pin 数直接配。','S02/S03/S12'],
 ['B09','盖板与排线','自定义盖板 + FPC + 屏连接器',1,'依屏厂二维/三维图定制','触摸、显示、电源、TE/RESET 等','圆角、边框、排线弯折和触摸有效区与新壳联动。','结构待定','面板 0.8 mm 不能视为整套屏含盖板厚度；先做结构堆叠。','S12'],
 ['B10','光学健康模块','MAXM86146CFU+',1,'38 pin OLGA；4.5×4.1×0.88 mm','1.8 V 主电源；LED 电源另核对；主机 I2C','内含 MAX86141 + MAX32664C + 两个 PD；不重复外加这三类器件。','优先评估候选','先拿到匹配固件/GUI并跑通；封壳光学与静止血氧验证后定型。','S13/S14'],
 ['B11','红光与红外 LED','SFH 7015 / Q65113A4161',1,'2×0.8×0.6 mm','655 nm 红光 + 940 nm IR','与参考板一致的发光器件系列；波段和电流须匹配算法。','参考设计选型','正式分档、光学距离、遮光和脉冲电流需验证。','S15/S22'],
 ['B12','绿光 LED','CT DBLP31.12-6C5D-56-J6U6',2,'封装尺寸查完整订货号图纸','绿光；按参考板电路驱动','沿用 EVSYS 的两颗绿光布局作为起点。','参考料号，供货待确认','缺货时先验证替代品波长、光功率和光学布局，不能只按外形替换。','S15/S23'],
 ['B13','LED 切换电路','MAX14689EWL+',1,'WLP，按参考板封装','LED 驱动通路切换','四个发光芯片与三个驱动通道的参考拓扑配套。','参考设计选型','保留 GPIO 控制及固件配置；改变 LED 数量须同步改电路与算法设置。','S15'],
 ['B14','健康板时钟/电源','32.768 kHz 时钟及参考去耦/LED 供电',1,'按健康模块参考设计','1.8 V 域 + LED 域；禁止按平台电压直接接线','完整供电、复位、MFIO、时钟和测试点。','原理图待展开','RTC 晶体 CM1610H32768DZB 为参考板用料；替代需 ESR/CL/启动验证。','S15'],
 ['B15','运动 IMU','LSM6DSOTR',1,'LGA；2.5×3×0.83 mm','I2C/SPI + INT；建议 1.8 V IO 域','抬腕、计步、运动输入；主机/低功耗核向健康模块喂入加速度数据。','在产候选','ADI 内置驱动不直接支持该 IMU；时间戳、量纲、方向、采样率和主机供数模式必须验证。','S16/S17/S18'],
 ['B16','PPG 光学结构','黑色隔光墙 + 光学窗 + 皮肤接触支撑',1,'按传感器/LED 间距和后盖定制','红/IR 透过率与串光控制','保持贴肤、减少直达光；结构是健康测量链的一部分。','必需定制','不能用透明后盖直接覆盖后就认定血氧可用；替换窗口后重新校准/验证。','S13/S15'],
 ['B17','触觉驱动','DRV2605L（如平台驱动不足）',1,'最终封装按高度选','I2C；LRA 闭环驱动','先评估平台已有驱动能力，再决定是否新增此器件。','条件装配','与平台自带触觉驱动二选一，不重复装；按最终 LRA 调整电压和波形。','S02/S21'],
 ['B18','线性马达','小型 LRA，具体 MPN 待机械样件',1,'先分配独立刚性安装腔','额定电压/谐振频率/制动性能待选','表冠刻度、通知和触觉反馈。','结构后选定','用加速度响应和主观佩戴测试选型，不保证与 Apple 触感相同。','S21'],
 ['B19','表冠和侧键','可按压旋转编码机构 + 侧键',1,'新结构设计；预留 ECG 触点','GPIO/编码器/光学或磁检测择一','旋转、按压、长按和防误触；模块化便于替换。','机构候选待定','磁编码方案须评估与磁力计冲突；密封、轴向窜动和触感一起验证。','工程设计'],
 ['B20','音频链','平台 Codec + MEMS MIC + Speaker/PA',1,'按平台参考设计与声腔空间','DMIC/PDM/I2S/模拟路径以 BSP 为准','通话、语音回复、提示音和回声处理；作为一组。','随参考设计','先沿用套件音频；不能仅换扬声器就承诺防水声学与回声表现。','S03'],
 ['B21','无线与天线','LTE、GNSS、Wi-Fi/BT 天线及匹配网络',1,'按最终壳体/电池共同设计','50 Ω 路径按实际栈叠控制','SoC 能力仍需完整 RF/WCN 配套；天线数量依共用拓扑确定。','射频方案待定','首轮外置天线；封壳后 OTA/匹配/佩戴失谐测试，不能照搬开发板匹配值。','S02/S03'],
 ['B22','SIM / eSIM','台架 Nano-SIM；整机 eUICC 条件集成',1,'卡座或焊接芯片依运营商方案','SIM 电气接口按平台手册','LTE/VoLTE 先验证；eSIM 一机一号/一号双终端分别确认。','台架明确；整机待确认','开发套件 eSIM 为预留，不代表交付即能开通；纳入 PCB 前确认服务路径。','S03'],
 ['B23','电池','单节 LiPo，500 mAh 为容量假设',1,'尺寸、倍率、终止电压待堆叠选型','保护/NTC；充电器与电芯一致','用 3.85 V 标称值仅做能量计算；不是已选电芯。','未冻结','按 LTE 发射峰值、内阻、低电量压降和厚度选择；最终容量不以此假设强塞。','工程假设'],
 ['B24','充电与电量','平台充电/电量计 + 磁吸底座接入',1,'线圈/触点依外壳和热设计','先用受控台架电源；目标保留磁吸充电体验','无线接收与有线触点方案分别评估，优先采用平台已有参考方案。','整机方式待验证','不假定兼容 Apple 充电盘；线圈/温升/磁场影响和电量模型均需封壳验证。','S02/S03'],
 ['B25','环境光','OPT3001',1,'封装依高度和光路选择','I2C + INT','自动调节屏幕亮度。','候选','查明屏幕总成是否已含 ALS，避免重复；盖板油墨需透光校准。','S29'],
 ['B26','气压高度','BMP390',1,'LGA；2×2×0.75 mm','I2C/SPI','相对高度和爬楼等输入。','后续集成候选','设计通气及防水膜；气压高度需校准和天气补偿。','S26'],
 ['B27','指南针','BMM350',1,'按当前料号封装图','接口与平台 Sensor HAL 核对','姿态/方向输入，结合 IMU。','后续集成候选','远离马达、扬声器和充电磁铁；硬铁/软铁及佩戴校准。','S30'],
 ['B28','腕温','TMP117',1,'按后盖热路径选封装','I2C','记录腕部温度及趋势。','后续集成候选','热隔离主处理器和充电线圈；不把芯片精度直接当作体温精度。','S25'],
 ['B29','ECG','MAX30003 + 后盖/表冠电极',1,'按模拟前端和电极空间定','SPI + 32 kHz 时钟；按完整参考电路','单导联波形/R-R；本条为一组，包含保护和滤波待展开。','后续集成候选','先用 ECG 模拟器；人体连接测试须建立适当隔离和电池供电条件。','S24'],
 ['B30','NFC/安全器件','平台支持的 NFC/SE/线圈组合',1,'按服务商参考方案','I2C/SPI/NCI 等由方案决定','保留公交/门禁/支付扩展目标，实际服务逐项验证。','接入后选定','不预选 PN7160 作为银行卡支付方案；不将 NFC 能力当成支付资格。','S31'],
 ['B31','PCB/FPC/屏蔽','参考布局派生的 HDI 主板 + 传感板/FPC',1,'约 46 mm 外观约束；轮廓/层数待布线','高速、RF、敏感模拟区域协同设计','先转接/扩展板，再紧凑主板；首轮保留调试接入口。','原理图后展开','不预先保证 4/6 层可布；DDR/DSI/RF 和 HDI 加工能力决定栈叠。','工程设计'],
 ['B32','整机外壳','自研中框/后盖/表带连接/密封件',1,'原型 3D 打印，后续 CNC/模具','全新结构，不复用华强北零件','屏幕、电池、光学、声腔、天线和表冠共同堆叠。','结构待设计','第一轮不承诺 Apple 的厚度、防水和重量；逐项实测收敛。','用户目标/工程设计'],
 ['B33','无源器件与连接','按原理图展开完整 MPN/位号/数量',null,'去耦、滤波、ESD、连接器、测试点','平台器件必须遵循推荐取值和布局','本选型表不能替代原理图导出的生产 BOM。','待设计展开','未知数量留空；实际采购须补齐位号、容差、封装、耐压和替代规则。','工程设计'],
];

// USD prices are public page references only, not quotations. Unknowns stay blank.
const purchases = [
 ['P01','TurboX W5+ Development Kit',1,1999,null,'当前不要直接购买','先确认实际 BSP、文档/源码权限及官方应用验证方案。','S03'],
 ['P02','MAXM86146EVSYS',1,null,null,'健康评估优先候选','先确认库存、固件/GUI 可取得及加速度供数模式。','S14'],
 ['P03','最终方形屏样品 + 转接板',2,null,null,'屏手册与驱动确认后','优先平台已验证的低功耗显示方案；不按通用 FPC 直接插接。','S12'],
 ['P04','表冠 + LRA + 驱动评估件',1,null,null,'机构样件阶段','若已有 SF58 板，可用于此独立实验；不为整机主线再买 SF58。','S21'],
 ['P05','台架电源/测流治具/连接材料',1,null,null,'按已有仪器补缺','用现有示波器/逻辑分析仪；确认量程和动态响应。','工程配置'],
 ['P06','首轮扩展板及装配',1,null,null,'应用与外设验证后','供应商参考接口确定后报价；不含最终 HDI 主板。','工程配置'],
 ['P07','3D 结构与光学样件',1,null,null,'与布局同步','分批验证屏幕、电池、PPG 接触和表冠行程。','工程配置'],
 ['P08','RF/声学外部测试服务',1,null,null,'紧凑样机阶段','范围确定后单独询价，不计入已知器件价格。','工程配置'],
];

const gates = [
 ['G0','完整功能定义','当前完成','1 个版本清单','本表 F01–F17；约 46 mm，新硬件/新外壳；保留网络、官方应用和健康目标。','Apple 专有服务与未验证能力有明确边界。','不随 UI 原型结果自动缩减目标。'],
 ['G1','软件与服务可取得','采购主平台前','供应商回复 + 实机演示','官方微信包来源/版本/ABI、当前 BSP、支付绑定、个人/小批项目接入、源码交付。','计划交付镜像和自研设备条件下可用，保留应用更新路径。','无法提供则更换平台/方案厂；不靠盲买硬件推进。'],
 ['G2','主系统台架','G1 后，估计 2–4 周','可刷机的开发套件','Wi-Fi/LTE、VoLTE、音频、显示、应用安装与系统定制、断电恢复。','日志完整；手机缺席时按要求联网；官方应用实测并记录限制。','版本问题优先修 BSP；不用 GUI 截图替代运行证据。'],
 ['G3','UI 与健康原型','G1 后，可与 G2 部分并行，估计 4–8 周','交互 Demo + 原始 PPG/IMU 数据','表冠、60 fps 目标、AOD、健康固件、光学窗和运动补偿。','帧时延/功耗/信号质量均有记录；确认最终屏候选。','界面资源可复用，LVGL 代码不能直接当成 Android SystemUI。'],
 ['G4','扩展板与结构样件','G2/G3 后，估计 3–6 周','载板/传感板 + 1:1 外壳样件','接口、电压域、天线/电池/声腔/光学/排线尺寸。','可合壳、无挤压；关键电路实测；电源峰值与热路径有余量。','发现体积不足先调整堆叠，不直接压缩电池安全间隙。'],
 ['G5','紧凑整机 EVT','G4 后，估计 5–10 周','自研主板 + 新壳工程样机','HDI PCB、供电、RF、显示切换、按键/音频/健康集成。','可恢复刷机；完成全功能回归与封壳电流/温度记录。','预留返板；不能仅凭裸板工作就宣布可佩戴交付。'],
 ['G6','日常使用验证','G5 后，估计 6–12 周','连续使用记录与问题修订','后台收信、付款、弱网、GNSS、充电、佩戴舒适、防水与耐久。','目标使用负载下达到 24 h；以实测替换预算。','功能、续航、厚度取舍须据测试数据处理，不预先承诺 Apple 等效。'],
 ['G7','后续健康/服务','与结构预留同步','ECG、腕温、NFC 等专项记录','逐项完成 F13/F14；睡眠/跌倒/疾病预警作为独立算法项目。','每项都有硬件、算法、验证及服务条件；不以芯片功能替代整机验收。','未通过的功能在版本清单中明确说明，不输出假正常数据。'],
];

const supplierQuestions = [
 ['Q01','应用与设备身份','能否在拟交付系统和我方自研设备身份上演示官方微信？请提供来源、版本、支持功能、登录条件及后续升级方式。'],
 ['Q02','支付','微信/支付宝付款是官方手表应用、设备 SDK 还是其他方案？个人或小批量项目能否接入？是否有真实绑定、付款、解绑演示？'],
 ['Q03','BSP 交付','实际 Android 版本、API level、64 位 ABI 和安全补丁日期是什么？是否包含 kernel/device tree/显示/Sensor HAL/音频/Launcher/SystemUI 源码或可修改接口？'],
 ['Q04','刷机与维护','能否编译并刷入定制镜像？有哪些封闭库、签名限制、调试口与恢复流程？BSP 更新、应用更新和技术支持范围是什么？'],
 ['Q05','显示与低功耗','推荐哪块方形 AMOLED？主核 DSI 和协处理器 QSPI 如何切换？是否交付面板手册、初始化、低功耗演示及最终可采购料号？'],
 ['Q06','联网与手机','目标地区频段、VoLTE 配置、eSIM/一号双终端条件是什么？微信在 iPhone/Android 配对与手机离线时分别支持哪些功能？'],
 ['Q07','硬件设计资料','是否交付原理图、PCB/叠层参考、完整 BOM/AVL、射频调试指南、内存配置与电源时序？自研紧凑板是否在支持范围内？'],
 ['Q08','商务与交期','请分列开发套件、核心板/芯片 MOQ、BSP/授权、定制 NRE、FAE 支持及小批样机价格和交期；避免只给整机打包价。'],
 ['Q09','健康链路','MAXM86146 对应固件/GUI 是否可下载并允许项目使用？当前是否支持主机输入新 IMU 数据？是否提供样本格式、定时要求和光学验证指导？'],
];

const mdTable = (headers, rows) => `| ${headers.join(' | ')} |\n| ${headers.map(()=> '---').join(' | ')} |\n${rows.map(r=>'| '+r.map(v=>String(v ?? '待定').replaceAll('|','／').replaceAll('\n','<br>')).join(' | ')+' |').join('\n')}`;

const report = `# 自研智能手表：方案重新评估 v0.3

评估日期：${date}。目标：从零自研电子硬件与外壳，操作体验尽量接近 Apple Watch，并保留联网、官方微信/支付宝及真实心率血氧能力。

## 1. 结论与本版决策

**推荐以“能够交付官方应用接入与可定制系统的穿戴参考设计”为主线。具体工程候选优先评估 W5+ / SW5100，W377E 作为方案厂询价备选。SF58 不再是当前完整整机方案的唯一主控。**

目前没有一个已经核实可供本个人项目直接采购、并保证官方微信与支付宝全部可用的组合。W5+ 能运行 Android，并不等于购买其开发套件就获得了这些应用和支付能力。因此本版给出的是有明确验证顺序的选型方案，不是“所有功能已打通”的承诺，也不是可投产 BOM。

完整目标保持不变。阶段样机用于减少返工，不把网络、应用或健康测量删成 UI 演示。旧 v0.1/v0.2 仅保留历史参考，不能继续作为本目标的采购依据。

工作假设：首轮 1–5 台工程样机、主要在中国大陆使用、外观尺度约 46 mm。手机型号、预算和数量尚未指定，本版分别保留 iPhone 与 Android 兼容验收项，不据此停止评估。

## 2. 苹果参照与可实现边界

Series 11 的 46 mm 版本采用 416×496、LTPO3 OLED 屏幕。它可以作为像素密度、暗色界面、交互与尺寸的产品参照；本项目屏幕、续航和健康算法需独立验证。苹果官方公开规格不能证明其内部使用哪一种显示总线。${link('S01')}

自研界面、表冠、触觉、联网、真实传感器采样都存在可实施路径。官方微信、支付宝、地图、在线音乐等必须按具体应用和设备服务验证。原版 watchOS、Apple Watch App 原生配对、Apple Pay 等专有生态不作为本方案可交付能力。${link('S09')}

**不要用“支持微信”一个宣传词作为验收条件。**例如 OPPO 的公开说明包含独立微信及特定系统版本条件，而 vivo WATCH 2 的说明要求 Android 手机并保持蓝牙连接。两者使用条件不同，也不能推导到我们的自研硬件。${link('S06')}，${link('S11')}

## 3. 平台比较

${mdTable(['路线','当前决策','最关键的缺口'], routes.map(r=>[r[1],r[6],r[4]]))}

W5+ 的价值是已有具体开发平台和双系统架构，不在于它是最新芯片。W377E 也有穿戴平台官方资料，但公开页面的软件描述存在版本/型号文案混用，不能把旧 Android 说明当作今天的微信、支付宝兼容性依据。${link('S02')}，${link('S04')}

SF58 可继续用于已有的表冠、LRA 或传感器台架。其公开 SDK 属于 RT-Thread 路线，不能直接运行 Android/watchOS 安装包。商业 MCU 手表可能通过厂商定制接入应用，但我们尚未取得 SF58 满足本目标的可验证组合。${link('S05')}

## 4. 建议架构

\`\`\`mermaid
flowchart TD
  APP["官方兼容应用与支付服务<br/>接入条件先验证"] --> AP["Android 应用处理器<br/>SW5100 工程候选"]
  AP --> MEM["2 GB LPDDR4<br/>32 GB eMMC"]
  AP --> NET["平台 Wi-Fi / 蓝牙 / LTE / GNSS<br/>射频与天线配套"]
  AP --> AUDIO["平台音频链<br/>麦克风与扬声器"]
  AP <--> AON["QCC5100 低功耗系统<br/>调度、唤醒、健康数据桥接"]
  AP -->|MIPI-DSI| DISP["方形 AMOLED 与触摸<br/>显示归属切换待平台验证"]
  AON -->|QSPI 低功耗路径| DISP
  AON <--> HEALTH["MAXM86146 与外部 LED<br/>PPG 光学与腕部算法"]
  AON <--> IMU["LSM6DSO<br/>主机同步加速度样本"]
  AON <--> INPUT[表冠、侧键、LRA]
  PMIC[平台 PMIC、电池、充电] --> AP
  PMIC --> AON
\`\`\`

图中的显示路径表示功能分工，不是可直接照画的电气连接。总线归属、复位、显示内容衔接和电源顺序必须采用供应商验证过的方案，不能把两个主机直接并接。

W5+ 主处理器支持 MIPI-DSI，QCC5100 支持 QSPI DDR；官方简报给出 640×640@60 Hz 的平台显示能力。这不代表任何面板、任何 Android 动画都能达到该性能。${link('S02')}

首轮软件采用供应商当前可交付的 Android BSP，界面在 Launcher/SystemUI/自研应用层开发；低功耗部分沿用平台框架。现有网页原型可作为视觉规范，LVGL 原型的图形布局与素材可迁移，但源码不能直接替代 Android 界面实现。AOSP、Wear OS 与 Google Play/GMS 不是同一套交付内容。${link('S08')}

## 5. 屏幕与结构

先使用开发套件配套屏验证软件。最终优先选供应商已验证的方形 AMOLED，尤其确认 AOD 与双系统切换。奥视特 ET020AM03-HT 可作为小屏候选：厂商列出 410×502、MIPI 31 pin、800 nit 和 33.07×41.05 mm 外观尺寸；正式接口电压、lane 数、初始化、低功耗刷新、触摸协议、盖板/FPC 总成尺寸、样品供货仍需取得。它尚不是已确认可用于 W5+ 双系统的屏幕。${link('S12')}

屏幕规格中的 LTPS 不能等同 Apple 的 LTPO3。不要通过接口或分辨率推断常亮功耗、可视角度或户外观感。最终尺寸必须以屏、主板、500 mAh 级电池候选、天线、光学窗和声腔的 3D 堆叠结果确定；本版不强行承诺 46 mm 外观下的具体厚度。

## 6. 心率、血氧与其他健康功能

主候选调整为 **MAXM86146CFU+ + 外部红/红外/绿光 LED + LSM6DSO**。MAXM86146 集成光学 AFE、算法 MCU 与两个光电二极管，可减少分立集成工作；LED、运动样本、固件与光学结构仍然必需。其算法支持腕式 HR/SpO2，实际准确性要由整机验证决定。${link('S13')}

首轮健康验证候选为 MAXM86146EVSYS。参考电路含 SFH 7015、两颗 CT DBLP31.12 绿光 LED 和 LED 切换器件。先取得 GUI/固件并记录原始 PPG、加速度、算法输出与质量标志，再设计自有传感板。${link('S14')}，${link('S15')}

**新板不直接复制旧参考板的 LIS2DS12/KX122 选型。**ST 已将 LIS2DS12 列为停产；ADI 固件直接控制的加速度型号有限。采用在产 LSM6DSO 时，计划由低功耗主机采集并向健康算法供数，必须验证固件版本、采样率、坐标、单位、同步和休眠缓存。不能把 LSM6DSO 接上去就认为补偿算法可用。${link('S16')}，${link('S17')}，${link('S18')}

MAXREFDES105 是另一条腕部参考方案，使用 MAX86174A + MAX32664C。其资料/软件申请涉及 NDA 审核，因此仅作为备选，不同时购买，也不认为一定可以取得全部文件。旧 MAXREFDES103 不再作为默认采购项；历史设计资料仍有参考价值，供货延续性未确认。${link('S20')}

ECG（MAX30003）和腕温（TMP117）保留为后续集成功能，外壳阶段预留电极及热路径。ECG 波形、腕温趋势、睡眠分期、跌倒检测和疾病预警分别验收；加入传感器不等于获得 Apple 的算法或诊断表现。${link('S24')}，${link('S25')}

## 7. 资源与功耗预算

以下是工程目标和可修改假设，尚无本机实测数据。

| 项目 | 初始值与用途 |
| --- | --- |
| 主处理器 | SW5100，4×A53，最高 1.7 GHz；实际频率由负载与电源策略决定 |
| 低功耗处理器 | QCC5100，最高 250 MHz；沿用平台调度/休眠框架 |
| 主内存 / 存储 | 2 GB LPDDR4 + 32 GB eMMC；正式分区与内存占用由 BSP 实测 |
| UI 内存控制 | 自研前台 UI 先以 128 MiB 峰值预算控制，系统和第三方应用另统计 |
| 图形目标 | 410×502、60 fps；ARGB8888 三帧缓冲约 2.36 MiB，仅为帧缓冲，不含纹理/字体/系统 |
| 显示原始负载 | RGB888 全屏 60 Hz 约 296.4 Mbit/s；加 20% 规划余量约 355.7 Mbit/s，仍需检查面板/DSI 协议时序 |
| 电池假设 | 500 mAh、标称 3.85 V、可用能量系数 85%，约 1.64 Wh；不等于已选电芯 |
| 24 小时目标 | 平均电池侧功耗需约 68.2 mW 或更低 |
| 假设功耗敏感性 | 50/100/300 mW 平均功耗分别对应约 32.7/16.4/5.5 小时；不是续航预测 |

平台处理器和内存配置见 ${link('S02')}、${link('S03')}。Excel“资源预算”提供公式；先测后台微信、LTE 弱网、AOD 和充电温升，再替换假设。所有功耗统一以电池端统计，避免重复扣除电源损耗。

PPG/IMU 中断只记录状态并唤醒任务；采样通过 FIFO/批处理减少主处理器唤醒。硬件时钟、电压域、上电顺序和 RF 配置沿用实际 BSP 参考，不能沿用先前 SF58 的晶体/电源设计。

## 8. 开发采购与费用

**现在不建议按上一版购买 SF58 作为最终主控，也不建议立即购买 W5+ 套件。先完成 G1 的供应与应用验证。**如果已有 SF58，可继续用于独立交互台架。

TurboX W5+ 页面标价为 **USD 1,999**，属于开发套件参考价，不是整机成本，也不保证含本项目所需的全部软件权限、税运和服务。MAXM86146EVSYS 的当前价格未核实。MAXREFDES105 页面标价 USD 326.08，仅作为互斥备选信息，不计入主路线采购合计。${link('S03')}，${link('S14')}，${link('S20')}

Excel 将未取得报价的项目留空，并只计算已知价格小计；不把缺失金额当作零。主芯片小批量价格、BSP/NRE、应用/支付服务、HDI 打样、结构与 RF 测试均缺正式报价，因此不能负责任地给出“整机几百元”的总价。

## 9. 阶段与验收

${mdTable(['阶段','主要产出','时间说明'],gates.map(g=>[`${g[0]} ${g[1]}`,g[3],g[2]]))}

时间为资料齐备、具备嵌入式经验并获得 BSP/射频等协作支持后的工程估计，阶段有重叠。G1 通过后，完整可佩戴工程样机可按约 5–9 个月作规划；个人独自完成全部环节可能更久。第三方服务无法接入时，延长编程时间不能保证完成对应功能。

验收重点是实际应用与付款、后台功耗、低功耗显示切换、真实健康测量以及封壳 RF/热/结构。完整逐项标准见 Excel“功能验收”。

## 10. 可直接用于供应商确认的问题

以下内容仅为沟通清单，尚未发送，也没有作出采购或签约承诺。

${supplierQuestions.map(q=>`- **${q[0]} ${q[1]}：**${q[2]}`).join('\n')}

## 11. 文件使用说明

配套 Excel 包含方案比较、功能验收、整机 BOM、开发采购、资源预算、推进计划和资料来源。整机 BOM 中的条件装配与后续集成项不应一次性全部采购。平台配套料以供应商交付的完整参考 BOM/AVL 为准，原理图完成后才能展开位号、封装尾码、无源器件和生产数量。

本版完成的是技术与供应条件重评，没有进行硬件测试、官方应用登录、支付交易、运营商开通或供应商交付确认。所有来源的网页核查日期为 ${date}。
`;

await fs.mkdir(outputDir,{recursive:true});
await fs.writeFile(reportPath, report, 'utf8');
const wb = Workbook.create();
const color = {ink:'#1D2733',blue:'#284665',line:'#D7DEE7',pale:'#F4F6F9',amber:'#FFF3CC',gray:'#5D6A79'};
const font = 'Arial'; // Windows arial.ttf verified; CJK target rendering uses the installed Microsoft YaHei fallback.
const meta = [];
function col(n){let s='';for(n++;n;n=Math.floor((n-1)/26))s=String.fromCharCode(65+(n-1)%26)+s;return s;}
function tableSheet(name,headers,rows,widths,subtitle){
 const sh=wb.worksheets.add(name); const end=col(headers.length-1); const last=rows.length+4;
 sh.showGridLines=false;
 sh.getRange(`A1:${end}${last}`).format.font={name:font,size:11,color:color.ink};
 sh.getRange(`A1:${end}${last}`).format.verticalAlignment='top';
 sh.getRange(`A4:${end}${last}`).format.wrapText=true;
 sh.getRange('A1').values=[[name]];
 sh.getRange('A1').format.font={name:font,size:17,bold:true,color:color.ink};
 sh.getRange(`A1:${end}1`).format.rowHeight=29;
 sh.getRange(`A2:${end}2`).format.borders={bottom:{style:'thin',color:color.line}};
 sh.getRange('A2').values=[[subtitle]]; sh.getRange('A2').format.font={name:font,size:10,color:color.gray};
 sh.getRange('A2').format.rowHeight=22;
 sh.getRange(`A4:${end}4`).values=[headers];
 sh.getRange(`A5:${end}${last}`).values=rows;
 widths.forEach((w,i)=>sh.getRange(`${col(i)}1:${col(i)}${last}`).format.columnWidthPx=w);
 const tab=sh.tables.add(`A4:${end}${last}`,true,`T${meta.length+1}`);
 tab.style='TableStyleMedium2'; tab.showFilterButton=true;
 sh.getRange(`A4:${end}4`).format={fill:color.blue,font:{name:font,size:11,bold:true,color:'#FFFFFF'},rowHeight:32,wrapText:true,verticalAlignment:'center'};
 sh.getRange(`A4:${end}4`).format.borders={insideVertical:{style:'thin',color:'#FFFFFF'}};
 rows.forEach((r,i)=>{
  const lines=Math.max(...r.map((v,c)=>{
   const text=v instanceof Date?date:String(v??''); let px=0; for(const x of text)px+=x.charCodeAt(0)>255?15.3:8.2;
   return Math.max(1,Math.ceil(px/(widths[c]-18)));
  }));
  sh.getRange(`A${i+5}:${end}${i+5}`).format.rowHeightPx=Math.max(64,(lines+1)*21+18);
  sh.getRange(`A${i+5}:${end}${i+5}`).format.fill=i%2===0?'#F1F4F8':'#FFFFFF';
  sh.getRange(`A${i+5}:${end}${i+5}`).format.borders={bottom:{style:'thin',color:color.line}};
 });
 if(rows.length>7)sh.freezePanes.freezeRows(4);
 meta.push({name,range:`A1:${end}${last}`,preview:`A1:${end}${Math.min(last,11)}`});
 return sh;
}

tableSheet('方案比较',['编号','平台/路线','已核实的基础','适合本目标的价值','当前缺口','进入采购的条件','本版决定','来源'],routes,[55,195,285,240,285,280,195,95],'v0.3  ·  2026-09-11  ·  完整功能目标：先验证应用接入，再冻结芯片');
tableSheet('功能验收',['编号','功能','实现归属','目标行为','验收方法','当前限制','来源'],features,[55,145,150,300,330,320,105],'自定验收目标；尚未进行实机测试。第三方应用在计划交付系统上验证。');
const bs=tableSheet('整机BOM',['编号','功能块','候选料号/方案','数量','封装/结构','接口/资源','选型理由与分工','选型进度','采购/设计前要确认','来源'],bom,[55,140,290,55,245,255,285,150,310,110],'选型级 BOM；数量以一台整机为基准，组项须展开。条件装配与后续项勿一次全买。');
bs.getRange(`D5:D${bom.length+4}`).setNumberFormat('0');
bs.getRange(`D5:D${bom.length+4}`).format.horizontalAlignment='right';

const ps=tableSheet('开发采购',['编号','项目','数量','参考单价 USD','小计 USD','采购时点','约束/包含范围','来源'],purchases,[55,260,55,120,120,180,400,100],'仅 USD 页面参考价；非报价，税运/授权/服务另计。空白表示待报价。');
ps.getRange('C5:C12').setNumberFormat('0');
ps.getRange('D5:E12').setNumberFormat('"$"#,##0.00');
ps.getRange('D5:D12').format.fill=color.amber;
ps.getRange('E5').formulas=[['=IF(ISNUMBER(D5),C5*D5,"")']];
ps.getRange('E5:E12').fillDown();
ps.getRange('B15').values=[['已知价格小计（非预算总额）']];
ps.getRange('E15').formulas=[['=SUM(E5:E12)']]; ps.getRange('E15').setNumberFormat('"$"#,##0.00');
ps.getRange('B16').values=[['尚无单价的项目数']]; ps.getRange('E16').formulas=[['=ROWS(D5:D12)-COUNT(D5:D12)']];
ps.getRange('B17').values=[['全部项目报价合计']]; ps.getRange('E17').formulas=[['=IF(COUNT(D5:D12)=ROWS(D5:D12),SUM(E5:E12),"待报价")']];
ps.getRange('E17').setNumberFormat('"$"#,##0.00');
ps.getRange('B19').values=[['备选不计入合计：MAXREFDES105#，页面 USD 326.08；资料涉及 NDA。']];
ps.getRange('B20').values=[['最终整机单价与总研发费用尚不可计算；不得把开发套件价格当作单台手表 BOM。']];
ps.getRange('B15:B17').format.columnWidthPx=260;
ps.getRange('B15:B17').format.wrapText=true;
ps.getRange('A15:H17').format.rowHeight=34;
ps.getRange('A15:H20').format.font={name:font,size:11,color:color.ink};
meta.find(m=>m.name==='开发采购').range='A1:H20';
meta.find(m=>m.name==='开发采购').preview='A1:H20';

const resource=wb.worksheets.add('资源预算');
resource.showGridLines=false;
resource.getRange('A1:F42').format.font={name:font,size:11,color:color.ink};
resource.getRange('A1:F42').format.rowHeight=24;
resource.getRange('A1:F42').format.verticalAlignment='center';
[255,125,100,300,160,130].forEach((v,i)=>resource.getRange(`${col(i)}1:${col(i)}42`).format.columnWidthPx=v);
resource.getRange('A1').values=[['资源预算']]; resource.getRange('A1').format.font={name:font,size:17,bold:true,color:color.ink};
resource.getRange('A2').values=[['黄色为可修改假设；计算结果不是实测性能或续航承诺。']];
resource.getRange('A2:F2').format.borders={bottom:{style:'thin',color:color.line}};
const data=[
 [4,'参数','输入/结果','单位','含义'],
 [5,'屏幕宽',410,'pixel','ET020 候选像素尺寸；S12'],
 [6,'屏幕高',502,'pixel','ET020 候选像素尺寸；S12'],
 [7,'目标刷新率',60,'Hz','工程目标，须最终屏幕实测'],
 [8,'帧缓冲每像素字节',4,'byte/pixel','ARGB8888 假设'],
 [9,'帧缓冲数量',3,'frame','仅帧缓冲；不含字体/纹理/系统'],
 [10,'单帧字节数',null,'byte','宽 × 高 × 每像素字节'],
 [11,'三帧缓冲占用',null,'MiB','按输入帧数计算；1 MiB = 2^20 byte'],
 [12,'线上每像素位数',24,'bit/pixel','RGB888 传输假设'],
 [13,'原始全屏传输负载',null,'Mbit/s','不含命令、消隐等开销'],
 [14,'规划余量',0.2,'比例','简单带宽规划系数；不是实际 DSI 效率'],
 [15,'含余量的带宽需求',null,'Mbit/s','不能据此选 lane/时序，须检查手册'],
 [17,'电池容量假设',500,'mAh','最终电芯尺寸/容量未定'],
 [18,'电池标称电压假设',3.85,'V','不是充电终止电压'],
 [19,'可用能量系数',0.85,'比例','余量/截止/老化等简化假设'],
 [20,'可用电池能量',null,'mWh','所有系统功耗均以电池端口径计'],
 [21,'目标工作时长',24,'h','第一轮设计目标'],
 [22,'目标允许平均功耗',null,'mW','可用能量 ÷ 目标工作时长'],
 [24,'情景平均功耗 (mW)','计算时长','单位','假设敏感性，非预测'],
 [25,50,null,'h','低平均负载假设'],
 [26,100,null,'h','中等平均负载假设'],
 [27,300,null,'h','高平均负载假设'],
 [29,'资源项','首轮预算','单位','说明'],
 [30,'主 RAM',2048,'MiB 近似配置','2 GB 套件配置；实际可用容量由系统报告'],
 [31,'自研 UI 峰值预算',128,'MiB','自定约束，不包括系统/第三方应用'],
 [32,'主 eMMC',32,'GB 标称','沿用套件；分区由实际 BSP 冻结'],
 [33,'应用包/资源验证集',2,'GB 预算','自定初值，量测后更新'],
 [34,'协处理器 Flash',32,'MB 配置','套件参考；S03'],
 [35,'协处理器 PSRAM',8,'MB 配置','套件参考；S03'],
 [37,'验证项',null,null,'执行方法'],
 [38,'时钟/电源',null,null,'核对供应商时序；启动、休眠、弱网发射期间用示波器记录。'],
 [39,'采样/中断',null,null,'测 FIFO、时间戳、丢样率；中断只通知任务，主机供数按固件规范。'],
 [40,'内存/帧时延',null,null,'Android 工具记录 RSS/PSS、掉帧和温升；协处理器量测栈/堆高水位。'],
 [41,'整机功耗',null,null,'测电池端波形；AOD、后台微信、GNSS、LTE 弱网和充电分场景复测。'],
];
for(const [r,...vals] of data) resource.getRange(`A${r}:D${r}`).values=[vals];
for(const r of [4,24,29,37])resource.getRange(`A${r}:D${r}`).format={fill:color.blue,font:{name:font,size:11,bold:true,color:'#FFFFFF'},rowHeight:28};
for(const r of [5,6,7,8,9,12,14,17,18,19,21,31,33])resource.getRange(`B${r}`).format.fill=color.amber;
resource.getRange('A25:A27').format.fill=color.amber;
resource.getRange('B10').formulas=[['=B5*B6*B8']];
resource.getRange('B11').formulas=[['=B10*B9/1048576']];
resource.getRange('B13').formulas=[['=B5*B6*B7*B12/1000000']];
resource.getRange('B15').formulas=[['=B13*(1+B14)']];
resource.getRange('B20').formulas=[['=B17*B18*B19']];
resource.getRange('B22').formulas=[['=B20/B21']];
resource.getRange('B25').formulas=[['=$B$20/A25']];resource.getRange('B25:B27').fillDown();
resource.getRange('B5:B35').setNumberFormat('#,##0.0');
for(const r of [5,6,7,8,9,10,12,17,21,30,31,32,33,34,35])resource.getRange(`B${r}`).setNumberFormat('#,##0');
resource.getRange('B11').setNumberFormat('0.00');resource.getRange('B18').setNumberFormat('0.00');
resource.getRange('B14').setNumberFormat('0%');resource.getRange('B19').setNumberFormat('0%');
resource.getRange('A4:D41').format.wrapText=true;
resource.getRange('A5:D41').format.rowHeight=36;
resource.getRange('A38:D41').format.rowHeight=65;
resource.getRange('B5:B9').dataValidation={rule:{type:'whole',operator:'greaterThan',formula1:0}};
for(const r of [17,18,21])resource.getRange(`B${r}`).dataValidation={rule:{type:'decimal',operator:'greaterThan',formula1:0}};
resource.getRange('B19').dataValidation={rule:{type:'decimal',operator:'between',formula1:0.01,formula2:1}};
resource.getRange('A25:A27').dataValidation={rule:{type:'decimal',operator:'greaterThan',formula1:0}};
meta.push({name:'资源预算',range:'A1:D41',preview:'A1:D35'});

tableSheet('推进计划',['编号','阶段','开始条件/时间估计','产出','主要工作','完成标准','失败后的处理'],gates,[55,170,215,185,360,310,270],'时间为工程估计，前提是资料和服务可取得；后续健康功能保留在路线中。');
tableSheet('资料来源',['编号','资料','URL','支持的内容与限制','核查日期'],sources.map(s=>[...s,new Date(`${date}T00:00:00Z`)]),[55,265,540,425,110],'官方资料优先；产品页参数、工程假设与本机实测分开。当前无本机实测结果。');
wb.worksheets.getItem('资料来源').getRange(`E5:E${sources.length+4}`).setNumberFormat('yyyy-mm-dd');

const inspect = await wb.inspect({kind:'table',range:'资源预算!A17:D27',include:'values,formulas',tableMaxRows:11,tableMaxCols:4,maxChars:5000});
const pcheck = await wb.inspect({kind:'table',range:'开发采购!B15:E17',include:'values,formulas',tableMaxRows:3,tableMaxCols:4,maxChars:2200});
const errors = await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:50},summary:'formula error scan'});
console.log(inspect.ndjson);console.log(pcheck.ndjson);console.log(errors.ndjson);
await fs.writeFile(`${workDir}/verification.txt`,[inspect.ndjson,pcheck.ndjson,errors.ndjson].join('\n'));
const renderOnly = process.argv[2];
for (const m of meta.filter(m=>!renderOnly || m.name===renderOnly)){
 const preview=await wb.render({sheetName:m.name,range:m.preview,scale:1,format:'png'});
 await fs.writeFile(`${workDir}/preview-${m.name}.png`,new Uint8Array(await preview.arrayBuffer()));
}
const file=await SpreadsheetFile.exportXlsx(wb);await file.save(workbookPath);
await fs.writeFile(`${workDir}/manifest.json`,JSON.stringify({date,workbookPath,reportPath,sheets:meta},null,2));
console.log(JSON.stringify({workbookPath,reportPath,sheetCount:meta.length}));
