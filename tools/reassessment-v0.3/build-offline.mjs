import fs from 'node:fs/promises';
import {Workbook, SpreadsheetFile} from '@oai/artifact-tool';

const work='tools/reassessment-v0.3';
const out='hardware';
const bookPath=`${out}/自研智能手表_离线阶段方案与BOM_v0.4.xlsx`;
const reportPath=`${out}/自研智能手表_离线阶段方案_v0.4.md`;
const date='2026-09-11';
const sources=[
 ['S01','SF58 开发板官方指南','https://wiki.sifli.com/board/sf32lb58x/SF32LB58-DevKit-LCD.html','N16R32N1、USB 转 UART、屏幕接口和板级设计文件。'],
 ['S02','SF58 硬件设计指南','https://wiki.sifli.com/en/hardware/SF32LB58x-HW-Application.html','48 MHz/32.768 kHz、供电域、SF30147C、表冠/马达、PCB 指导。'],
 ['S03','SiFli SDK','https://github.com/OpenSiFli/SiFli-SDK','RT-Thread 与图形框架；采用匹配开发板和屏幕的稳定工程。'],
 ['S04','SiFli 模组型号指南','https://wiki.sifli.com/silicon/%E6%A8%A1%E7%BB%84%E5%9E%8B%E5%8F%B7%E6%8C%87%E5%8D%97.html','SF32LB58-MOD-N16R32N1 为 24×24 mm；16+16 MB PSRAM 与 16 MB 外部 NOR。'],
 ['S05','SF32LB58X 产品页','https://www.sifli.com/zh-hans/node/29','主系统最高 240 MHz；本版不假设所有核心/存储均归 UI 独占。'],
 ['S06','用户提供的开发板商品截图','用户提供截图，2026-09-11','1.85 英寸、390×450、QSPI 显示/I2C 触摸；截图选中 A128R32N1 和板卡项，不能把截图价格当带屏套装报价。'],
 ['S07','MAXM86146 产品页','https://www.analog.com/en/products/maxm86146.html','集成 AFE、算法 MCU、双 PD；还需 LED、运动样本、固件与光学结构。'],
 ['S08','MAXM86146EVSYS','https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/maxm86146evsys.html','评估板和固件/GUI 入口；具体价格、库存与下载权限未确认。'],
 ['S09','MAXM86146EVSYS 电路与用料','https://www.analog.com/media/en/technical-documentation/data-sheets/maxm86146evsys.pdf','SFH 7015、两颗绿光 LED、LED 切换电路和供电/时钟参考。'],
 ['S10','ADI 加速度兼容性 FAQ','https://ez.analog.com/other-products/a/documents/do21669/what-accelerometers-are-supported-by-the-max32664c-firmware','新 IMU 需采用主机供数等受支持模式，不是直接替换内置驱动型号。'],
 ['S11','ST LSM6DSO','https://www.st.com/en/mems-and-sensors/lsm6dso.html','在产六轴 IMU，用于运动检测及健康算法的加速度输入候选。'],
 ['S12','ST LIS2DS12','https://www.st.com/en/mems-and-sensors/lis2ds12.html','官方已列为 Obsolete；不作为新板默认用料。'],
 ['S13','TI DRV2605L','https://www.ti.com/product/DRV2605L','支持 LRA/ERM 触觉驱动，最终手感须机械和波形调校。'],
 ['S14','TI BQ25180','https://www.ti.com/product/BQ25180','单节电池充电与电源路径候选，充电参数按实际电芯确定。'],
 ['S15','奥视特 MIPI 屏候选','https://www.aoshite.net/productView1_1975.html','ET020AM03-HT 可作为之后的屏幕评估分支，时序/驱动/现货仍待确认。'],
];
const url=id=>sources.find(s=>s[0]===id)?.[2];
const cite=id=>`[${sources.find(s=>s[0]===id)[1]}](${url(id)})`;

const scope=[
 ['01','主线','本阶段实现','SF32LB58 + RT-Thread/LVGL；先开发板，再自研主板/外壳。','联网和官方应用不再是本阶段前置条件。'],
 ['02','界面','本阶段实现','表盘、蜂窝图标、列表、控制中心、设置、健康/运动页面；暗色与动画。','60 fps 为目标；同时测触摸、表冠响应和掉帧。'],
 ['03','基础功能','本阶段实现','本地时间、日期、闹钟、秒表、计时器；手动或有线校时。','离线掉电后重启、RTC 计时与闹钟必须实测。'],
 ['04','交互','本阶段实现','触摸、可按压表冠、侧键、LRA 反馈、抬腕亮屏与休眠。','电机与机械安装共同决定触感；AOD 在基础休眠后优化。'],
 ['05','心率血氧','本阶段实现','接入真实 PPG 数据和算法输出；本地显示与保存摘要。','开发板本身不测 HR/SpO2；外接健康模块，先验证静止血氧。'],
 ['06','运动记录','本阶段实现','IMU 计步、活动时长及简单运动页面；本地保存。','GNSS 轨迹暂不集成；运动/睡眠算法不因离线而自动具备。'],
 ['07','主板和外壳','本阶段后段','完全新设计；初版可用 SF58 模组 + 自研载板，紧凑版再选裸芯片。','模组 24×24 mm；整机厚度与电池容量仍需 3D 堆叠。'],
 ['08','联网/同步','后续阶段','暂不开发 Wi-Fi、蜂窝网络、云服务、蓝牙数据同步和 OTA。','SF58 内置蓝牙保留；本阶段功能在无线关闭时验收，有线烧录/日志。'],
 ['09','微信/支付宝','后续阶段','不开发官方应用安装、聊天和真实支付，也不制作容易误认的假付款功能。','未来需要单独重评应用生态，不能承诺 SF58 加网络后可直接安装。'],
 ['10','其他扩展','后续阶段','ECG、腕温、NFC、GNSS、在线音乐、通话以及复杂睡眠/预警算法。','保持在最终方向中，暂不放入本轮采购和 PCB 必装清单。'],
];
const buy=[
 ['P01','SF32LB58-DevKit-LCD，N16R32N1 + 配套 AMOLED',1,'第一批','按截图选择含 AMOLED 的套餐；取得能编译的屏幕/触摸例程及转接板。','本版建议；实际套装价格未核实','S01/S06'],
 ['P02','USB 数据线',1,'第一批','接 USB 转 UART 调试口；第二根线仅在同时使用芯片 USB 时需要。','优先使用已有数据线','S01'],
 ['P03','表冠原型编码器 + 按键',1,'第二批','先用可按压编码器模块验证操作；最终机构在外壳阶段确定。','GPIO 电平及去抖需确认','S02'],
 ['P04','DRV2605L 评估模块 + 小型 LRA',1,'第二批','用于表冠刻度/通知反馈；先原型调校再选具体马达尺寸。','核对模块逻辑电平与 LRA 参数','S13'],
 ['P05','LSM6DSO 评估模块',1,'第二批','抬腕、计步和健康算法加速度输入。','核对供电/IO、数据率和时间戳','S10/S11'],
 ['P06','MAXM86146EVSYS',1,'健康验证批次','先确认算法固件、GUI、供货及主机供数模式；接入独立健康界面。','有官方评估路线，价格/库存未确认','S07/S08'],
 ['P07','健康/主控转接线板',1,'接口明确后','按参考图对照 VIO、GND、I2C、RESET/MFIO；不能按排针间距盲插。','电气检查后制作','S02/S09'],
 ['P08','自研载板、传感板、3D 外壳',1,'开发板功能验证后','先做可调试版本，再收紧体积；样件数量由返板计划确定。','此项按一轮服务计，价格待图纸','工程设计'],
];
const bom=[
 ['B01','主控','SF32LB58-MOD-N16R32N1；紧凑版 SF32LB586VDD36',1,'初版优先 24×24 mm 模组；避免一开始处理裸芯片密集布线。','与分立方案互斥；模块内存/晶体不再重复采购。','S01/S04'],
 ['B02','主存储','模组内 16+16 MB PSRAM、16 MB NOR 与合封启动 Flash',1,'按模组既有配置；图形、字体、固件和历史摘要先做预算。','按两个 PSRAM 区域配置，不默认单一连续 32 MB 堆。','S01/S04'],
 ['B03','时钟','48 MHz + 32.768 kHz',1,'模块已含；裸芯片版按官方 CL/ESR 与布局要求选型。','不直接沿用 W5+ 平台晶体/电源设计。','S02'],
 ['B04','显示触摸','配套 1.85 英寸 390×450 AMOLED',1,'截图标注 QSPI 显示 + I2C 触摸；先使用随板转接与驱动。','CO5300AF-01/FT6146-M00 来自截图；RGB565 总线模式须核实。','S06'],
 ['B05','屏接口与电源','跟随配套屏参考设计',1,'连接器、供电、电平、初始化与 TE/RESET 一并匹配。','QSPI 面板不能靠改软件变成 MIPI；换屏需转接与驱动。','S01/S02'],
 ['B06','光学健康模块','MAXM86146CFU+',1,'内含 AFE、算法 MCU 与双 PD；主机 I2C 与控制脚。','外部 LED、算法固件和佩戴光学仍然必需。','S07'],
 ['B07','红/红外 LED','SFH 7015',1,'按 EVSYS 参考选择红光/红外组件。','完整分档和当前供货随健康板验证确认。','S09'],
 ['B08','绿光 LED','CT DBLP31.12-6C5D-56-J6U6',2,'按 EVSYS 参考；替代需验证光学和算法设置。','保留遮光和皮肤接触设计。','S09'],
 ['B09','LED 切换','MAX14689EWL+',1,'与参考板的四发光芯片/三驱动通道拓扑配套。','不能只接三个 LED 驱动通道就忽略切换配置。','S09'],
 ['B10','健康板时钟/电源','参考时钟、1.8 V 域、LED 电源与去耦',1,'根据健康模块手册展开；电源和地布局控制噪声。','不把芯片电源直接接电池，不凭外观判断 IO 电平。','S09'],
 ['B11','运动 IMU','LSM6DSOTR',1,'抬腕/计步；SF58 采集并通过支持的协议供给健康算法。','LSM6DSO 不是算法内部驱动的即插即用替代；验证同步和采样率。','S10/S11/S12'],
 ['B12','健康光学结构','遮光墙、光学窗、贴肤支撑',1,'先可调整光学样件，后固定进新后盖。','无效信号应显示未测得，不能显示成正常数值。','S07/S09'],
 ['B13','振动驱动','DRV2605L',1,'I2C 控制 LRA；原型阶段用评估模块。','核对 1.8 V 逻辑兼容及马达额定参数。','S13'],
 ['B14','马达','小型 LRA，MPN 随外壳确定',1,'同一马达在不同安装刚度下响应不同。','预留刚性支撑与独立调校空间。','S13'],
 ['B15','表冠/按键','可按压旋转机构 + 侧键',1,'GPIO/编码器输入；先模块验证后选最终机构。','端口中断、去抖、按压/旋转同时操作实测。','S02'],
 ['B16','主电源','SF30147C + SF58 参考电源树',1,'作为可穿戴板候选；模块方案遵循模块供电要求。','开发板已含供电；自研板重新核对所有电压域和峰值电流。','S02'],
 ['B17','电池充电','BQ25180，或完整官方充电参考方案',1,'单节电池充电/电源路径；二选一并与 B16 做电源树核对。','实际电芯终止电压、电流、NTC、保护与散热确定后冻结。','S14'],
 ['B18','电池','单节 LiPo；300–500 mAh 仅初期体积评估范围',1,'优先满足尺寸、内阻、保护及温度要求。','本版资源表以 400 mAh 做假设；不保证最终容量/续航。','工程假设'],
 ['B19','充电接入','磁吸触点/底座；无线接收后续评估',1,'原型用 USB；优先简化第一次整机充电调试。','不假定兼容 Apple 原装无线充电盘。','工程选择'],
 ['B20','PCB/FPC/调试','自研载板 + 健康板/FPC + UART/SWD/BOOT 测试点',1,'开发板不放入表壳；载板先保留测试点，再收紧结构。','层数/板厚由实际布局决定；保留离线恢复刷机路径。','S01/S02'],
 ['B21','外壳/表带连接','完全新设计中框、后盖、盖板与连接机构',1,'3D 样件核对屏幕、电池、马达、光学和表冠堆叠。','不直接承诺 Apple 尺寸、厚度、防水或重量。','用户目标'],
 ['B22','无源/连接器','按原理图展开',null,'电感、电容、电阻、ESD、排线座及封装尾码。','当前是选型级 BOM，不能直接作为生产贴片清单。','工程设计'],
];

const report=`# 自研智能手表：离线第一阶段 v0.4

更新日期：${date}。本阶段以本版为准，v0.3 的联网/官方应用整机路线保留为后续参考。

**推荐：SF32LB58 + 配套 AMOLED + RT-Thread/LVGL + 真实心率血氧模块。先开发板验证，再做自研主板和新外壳。**

## 当前做什么

- Apple Watch 风格的表盘、蜂窝图标、列表、控制中心、设置和动画。
- 表冠旋转/按压、触摸、侧键、振动、抬腕和休眠。
- 本地时间、闹钟、计时器、秒表；手动或有线校时。
- 真实心率、静止血氧、计步与活动记录，保存在本机。
- 后段设计自己的主板、健康传感板和外壳。

Wi-Fi、4G/eSIM、云服务、官方微信/支付宝、通话和在线应用不纳入本阶段。蓝牙硬件随 SF58 保留，第一阶段先不开发同步功能，整机按无线关闭条件验收。ECG、腕温、NFC、GNSS 和复杂健康算法后续再集成。

## 主控与第一批采购

选择 **SF32LB58-DevKit-LCD，N16R32N1，配套 AMOLED 套餐**。这块板适合本阶段的显示、触摸和传感器开发；官方提供资料与 USB 转 UART 烧录/调试方式。${cite('S01')}

N16R32N1 使用 16 MB NOR，A128R32N1 使用 128 MB NAND，PSRAM 配置同为 16+16 MB。本阶段优先 NOR 版本，减少首轮文件系统与存储调试工作。现有 SDK 工程应匹配实际板卡版本和屏幕。${cite('S04')}，${cite('S03')}

你截图里应选择包含“DevKit+AMOLED屏”的套餐，并确认配套转接板、排线、触摸驱动、可编译示例和烧录文件。截图当前的价格与选项不能当成这一组合的有效报价。第一批先买这套板屏和一根数据线，先跑通原厂触摸/显示例程；健康模块可以随后接入。

开发板本身不具备心率/血氧测量能力，需要外接健康硬件。

## 屏幕与流畅度

截图中的小屏为 **1.85 英寸、390×450、QSPI 显示、I2C 触摸**。开发板支持 MIPI-DSI，但这不代表配套小屏就是 MIPI。先用配套屏把 UI 做起来，目标仍是 60 fps，然后测真实帧率、触摸延迟与功耗。

QSPI 50 MHz、4 bit、SDR 的理论吞吐量为 25 MB/s。390×450 一帧 RGB888 为 526,500 byte，单纯传输约 21.06 ms，已超过 60 fps 的 16.67 ms 周期；RGB565 若能在线上传输 16 bit/pixel，理论传输约 14.04 ms，实际仍需开销与渲染时间。**只有屏幕控制器和驱动支持对应总线像素格式，16 位传输预算才成立；内部用 RGB565 渲染不等于线上一定传 16 位。**

先测试 DMA、图形加速、局部刷新、帧缓冲布局和动画复杂度。若配套屏无法满足关键全屏动画，再评估 MIPI 面板与驱动。MIPI 升级是换屏/转接/驱动工作，不是把原 QSPI 屏的软件设置改一下。显示接口能力见 ${cite('S02')}。

## 健康链路保留

采用上一版已经复核的 **MAXM86146 + 外部 LED + LSM6DSO** 候选。先评估 MAXM86146EVSYS 的固件/GUI 和实际供货，再连接 SF58。MAXM86146 内含光学 AFE、算法 MCU 和双 PD，仍需要发光器件、运动样本和光学隔离。${cite('S07')}，${cite('S08')}

LSM6DSO 由 SF58 采集，再按算法支持的主机供数协议送入健康模块，必须核对固件版本、采样率、单位、坐标和同步。不要复制已停产的 LIS2DS12；也不要把新 IMU 当成算法内部驱动的直接替代。${cite('S10')}，${cite('S11')}，${cite('S12')}

健康输出分清原始波形、算法数值和质量标志。先验证静止测量、佩戴松紧及无效状态，再优化运动心率。参考血氧仪的对照仅作工程一致性检查，不把它当作完整临床精度验证。UI 演示阶段的数据须标为示例，实测界面只显示有效测量。

## 自研板怎么落地

第一块自研板建议采用 **SF32LB58-MOD-N16R32N1 模组 + 自研载板**，模组为 24×24 mm；载板连接屏幕、健康板、表冠、马达、电池与充电电路。待接口、功耗和体积明确后，再决定紧凑版是否使用 SF32LB586VDD36 裸芯片。模组已经包含的内存和晶体不应重复装配。${cite('S04')}

主系统先按最高 240 MHz 运行验证，后续按负载降频。外部时钟为 48 MHz 与 32.768 kHz；供电按 SF58/模组参考设计处理。SF30147C 为可穿戴电源候选，充电器/电芯参数另外匹配，不直接把电池接到任意 1.8 V/3.3 V 电源脚。${cite('S05')}，${cite('S02')}

资源初值：两块 RGB565 全屏缓冲约 0.67 MiB；暂为图形资源/缓存留 8 MiB、应用堆 2 MiB，实际按分散的 PSRAM 区域与 SDK map 文件分配。内部 SRAM 保留给栈、DMA/实时工作区，不能把总 PSRAM 当成一块可随意分配的连续堆。16 MB 外部 NOR 暂按固件 6 MiB、界面资源 6 MiB、摘要/配置 2 MiB、余量 2 MiB 规划，最终以链接与文件系统结果调整；原始 PPG 调试数据优先导出 PC，不长期堆积在 NOR。

续航以实测为准。400 mAh、3.85 V、85% 可用系数仅作为预算假设；24 h 目标对应约 54.5 mW 平均电池侧功耗。没有联网仍需优化显示、PPG、唤醒和漏电；开发板电流不能直接当作最终手表电流。

## 顺序与完成标准

| 阶段 | 工作 | 完成标准 |
| --- | --- | --- |
| 1 | 板屏例程、烧录与日志 | 可从源码编译，屏幕/触摸稳定，有恢复下载路径 |
| 2 | 自研 UI、表冠、LRA | 蜂窝/列表/表盘可操作，记录帧时延，验证休眠唤醒 |
| 3 | IMU 与健康评估板 | 有真实数据及质量标志，供数同步稳定，保存本地摘要 |
| 4 | 载板、健康板、外壳样件 | 完成电压域检查、3D 堆叠、封壳功耗和佩戴测试 |

调试时用 UART 日志、逻辑分析仪检查 I2C/QSPI/中断，结合 DMA/FIFO 计数检查丢样和撕裂。中断只通知任务；UI、传感器采集和存储分任务处理。测试栈/堆高水位、掉电记录恢复、闹钟与看门狗，保留 BOOT/RESET/UART/SWD 调试点。

后续如只增加天气/健康同步，可继续评估 SF58 的通信扩展；如重新要求官方微信和支付宝应用，需再次核实系统与平台接入，不能承诺只加网络硬件就能实现。

本版完成范围与选型更新，尚未采购或进行硬件测试。未核实的价格保持空缺；配套 Excel 为选型 BOM，原理图完成后再展开生产位号和无源器件。
`;

await fs.writeFile(reportPath,report,'utf8');
const wb=Workbook.create();const meta=[];
function col(n){let t='';for(n++;n;n=Math.floor((n-1)/26))t=String.fromCharCode(65+(n-1)%26)+t;return t;}
function sheet(name,heads,rows,widths,note){
 const s=wb.worksheets.add(name);const last=rows.length+4;const end=col(heads.length-1);s.showGridLines=false;
 s.getRange(`A1:${end}${last}`).format.font={name:'Arial',size:11,color:'#1D2733'};
 s.getRange(`A4:${end}${last}`).format.wrapText=true;s.getRange(`A1:${end}${last}`).format.verticalAlignment='top';
 s.getRange('A1').values=[[name]];s.getRange('A1').format.font={name:'Arial',size:17,bold:true};s.getRange('A1').format.rowHeight=29;
 s.getRange('A2').values=[[note]];s.getRange('A2').format.font={name:'Arial',size:10,color:'#5D6A79'};s.getRange(`A2:${end}2`).format.borders={bottom:{style:'thin',color:'#D7DEE7'}};
 widths.forEach((w,i)=>s.getRange(`${col(i)}1:${col(i)}${last}`).format.columnWidthPx=w);
 s.getRange(`A4:${end}4`).values=[heads];s.getRange(`A5:${end}${last}`).values=rows;
 const t=s.tables.add(`A4:${end}${last}`,true,`Offline${meta.length+1}`);t.style='TableStyleMedium2';
 s.getRange(`A4:${end}4`).format={fill:'#284665',font:{name:'Arial',size:11,bold:true,color:'#FFFFFF'},rowHeight:32,wrapText:true,verticalAlignment:'center'};
 rows.forEach((r,i)=>{const lines=Math.max(...r.map((v,c)=>{let px=0;for(const ch of String(v??''))px+=ch.charCodeAt(0)>255?15.3:8.2;return Math.ceil(px/(widths[c]-18));}));const rr=s.getRange(`A${i+5}:${end}${i+5}`);rr.format.rowHeightPx=Math.max(64,(lines+1)*21+18);rr.format.fill=i%2?'#FFFFFF':'#F1F4F8';});
 if(rows.length>7)s.freezePanes.freezeRows(4);
 meta.push({name,range:`A1:${end}${Math.min(last,11)}`});return s;
}
sheet('阶段方案',['编号','模块','本阶段安排','实现内容','边界与验证'],scope,[55,125,135,390,390],'v0.4 · 离线第一阶段；完整网络/应用路线推迟，健康测量仍保留。');
const p=sheet('开发采购',['编号','项目','数量','批次','采购前确认','价格与说明','来源'],buy,[55,305,55,140,390,250,100],'第一批先买板屏套装与数据线；不把开发套件当成最终可佩戴主板。');p.getRange('C5:C12').setNumberFormat('0');
const b=sheet('整机候选',['编号','功能','候选器件/方案','数量','设计选择','验证/装配条件','来源'],bom,[55,135,295,55,350,350,100],'初版模组方案与裸芯片方案互斥；数量 1 的组项需在原理图阶段展开。');b.getRange(`D5:D${bom.length+4}`).setNumberFormat('0');
const r=wb.worksheets.add('资源预算');r.showGridLines=false;r.getRange('A1:D41').format.font={name:'Arial',size:11,color:'#1D2733'};r.getRange('A1:D41').format.rowHeight=32;r.getRange('A1:D41').format.verticalAlignment='center';
[255,135,105,395].forEach((w,i)=>r.getRange(`${col(i)}1:${col(i)}41`).format.columnWidthPx=w);
r.getRange('A1').values=[['资源预算']];r.getRange('A1').format.font={name:'Arial',size:17,bold:true};r.getRange('A2').values=[['黄色为假设；显示、RAM、存储和续航均需实际测量替换。']];
const vals=[
 [4,'参数','值','单位','说明'],[5,'屏幕宽',390,'pixel','来自商品截图；以实际屏为准'],[6,'屏幕高',450,'pixel','来自商品截图；以实际屏为准'],[7,'目标刷新率',60,'Hz','UI 目标，非开发板已测帧率'],[8,'QSPI 时钟假设',50,'MHz','演算值；须查面板/板级驱动可用时钟'],[9,'QSPI 数据线数',4,'bit/cycle','SDR 假设；不套用 DDR 倍率'],[10,'理论吞吐量',null,'MB/s','不含协议、DMA 调度和屏幕等待'],[11,'RGB888 一帧',null,'byte','线传 24 bit/pixel 假设'],[12,'RGB888 传输时间',null,'ms','只含有效像素字节'],[13,'RGB565 一帧',null,'byte','仅当驱动和屏幕实际支持线传 16 位'],[14,'RGB565 传输时间',null,'ms','渲染 16 位不代表总线也传 16 位'],[15,'目标帧周期',null,'ms','渲染和传输可重叠程度需测量'],[16,'RGB565 双帧缓冲',null,'MiB','只是帧缓冲，不含图形资源与中间缓冲'],[18,'图形资源/缓存预算',8,'MiB','自定初值，按 SDK 内存区域实际分配'],[19,'应用堆预算',2,'MiB','自定初值，量测堆/栈高水位后调整'],[21,'外部 NOR 规划','容量','MiB','初始规划；合封启动 Flash 另按 BSP 管理'],[22,'固件',6,'MiB','以链接产物为准；不预留联网 OTA 双镜像'],[23,'字体/图标/表盘',6,'MiB','先压缩资源并裁剪字体'],[24,'摘要/设置',2,'MiB','原始 PPG 调试数据输出 PC'],[25,'余量',2,'MiB','按文件系统开销调整'],[26,'合计',null,'MiB','与 16 MB 外部 NOR 实际分区核对'],[28,'电池容量假设',400,'mAh','体积/电芯未选定'],[29,'标称电压假设',3.85,'V','不是充电终止电压'],[30,'可用能量系数',0.85,'比例','简化余量/截止假设'],[31,'可用能量',null,'mWh','按电池端能量计算'],[32,'工作时长目标',24,'h','整机设计目标，非实测续航'],[33,'允许平均功耗',null,'mW','电池端口径；不再次扣电源效率'],[35,'重点验证','方法',null,null],[36,'内存/时钟',null,null,'主系统先按最高 240 MHz 验证；PSRAM 16+16 MB 不是默认连续单堆。'],[37,'中断/采样',null,null,'用逻辑分析仪和 FIFO 计数检查采样丢失、I2C 时序、数据单位和时间戳。'],[38,'显示链路',null,null,'测 QSPI/TE/DMA、帧耗时和触摸响应，确认实际线上像素格式。'],[39,'离线稳定性',null,null,'测试 RTC、闹钟、掉电记录恢复、看门狗与有线恢复下载。'],[40,'整机功耗',null,null,'测最终板电池端电流、休眠唤醒及 LED 脉冲；开发板电流不能直接推算整机。'],
];
for(const [n,...v]of vals)r.getRange(`A${n}:D${n}`).values=[v];
r.getRange('A4:D40').format.wrapText=true;
for(const n of [4,21,35])r.getRange(`A${n}:D${n}`).format={fill:'#284665',font:{name:'Arial',size:11,bold:true,color:'#FFFFFF'},rowHeight:32};
for(const n of [5,6,7,8,9,18,19,22,23,24,25,28,29,30,32])r.getRange(`B${n}`).format.fill='#FFF3CC';
const f={10:'=B8*B9/8',11:'=B5*B6*3',12:'=B11/(B10*1000000)*1000',13:'=B5*B6*2',14:'=B13/(B10*1000000)*1000',15:'=1000/B7',16:'=B13*2/1048576',26:'=SUM(B22:B25)',31:'=B28*B29*B30',33:'=B31/B32'};
for(const [n,v]of Object.entries(f))r.getRange(`B${n}`).formulas=[[v]];
r.getRange('B5:B33').setNumberFormat('#,##0.00');for(const n of [5,6,7,8,9,11,13,18,19,22,23,24,25,26,28,32])r.getRange(`B${n}`).setNumberFormat('#,##0');r.getRange('B30').setNumberFormat('0%');r.getRange('A36:D40').format.rowHeight=52;
meta.push({name:'资源预算',range:'A1:D33'});
sheet('资料来源',['编号','资料','链接/出处','核实范围'],sources,[55,275,550,420],'网页核查：2026-09-11；价格未确认；截图参数仅作为原型候选。');
const a=await wb.inspect({kind:'table',range:'资源预算!A10:D16',include:'values,formulas',tableMaxRows:7,tableMaxCols:4,maxChars:3500});
const c=await wb.inspect({kind:'table',range:'资源预算!A28:D33',include:'values,formulas',tableMaxRows:6,tableMaxCols:4,maxChars:2500});
const e=await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:30},summary:'offline budget formula check'});console.log(a.ndjson);console.log(c.ndjson);console.log(e.ndjson);
for(const m of meta){const img=await wb.render({sheetName:m.name,range:m.range,scale:1,format:'png'});await fs.writeFile(`${work}/offline-${m.name}.png`,new Uint8Array(await img.arrayBuffer()));}
await (await SpreadsheetFile.exportXlsx(wb)).save(bookPath);console.log(JSON.stringify({bookPath,reportPath,sheets:meta.length}));
