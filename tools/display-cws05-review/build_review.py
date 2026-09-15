"""Record the user-proposed CWS05 display and wearable revision requirements."""
from pathlib import Path
import csv
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'hardware'
SRC = BASE / '原理图_v0.3_封装与接口完善'
OUT = BASE / 'CWS05屏幕与佩戴版修订评估'
OUT.mkdir(parents=True, exist_ok=True)
PDF = Path('references/vendor/C50396528_OLED显示屏_CWS05_规格书_WJ1760329.PDF')
bom = list(csv.DictReader((SRC / 'BOM_v0.3.csv').open(encoding='utf-8-sig')))

symbols = ['LCD_RST','GND','LCD_TE','LCD_CS','LCD_CLK','LCD_SIO1','LCD_SIO0','ID',
           'IM1_NC','IM0_NC','VBAT','VBAT','GND','GND','VDD','TP_VDD','TP_INT','TP_RST',
           'TP_SDA','TP_SCL','GND','LCD_SIO3','LCD_SIO2','VCI_EN']
unclear = {
    8: 'P8 名称为 ID，P5 图示说明不清晰；需确认悬空/上下拉要求。',
    10: 'P5 带 MTP 字样，P8 为 NC；按供应商最终接口定义确认，不作编程引脚使用。',
    11: 'P8 标注 PMIC 电源 3.7V；实际 VBAT 允许范围及满充 4.2V 适用性待确认。',
    12: 'P8 标注 PMIC 电源 3.7V；不可沿用原转接板的 +5V。',
    15: 'P5 图示标注 IO 电平 1.8V；P6 VDDIO 正常 1.7~1.9V，需确认该脚与内部供电关系。',
    16: 'P8 标 TP_VDD，P5 标 VDD；确认触摸供电及内部连接，不能仅按 CST820 芯片范围定模组电压。',
    18: 'P8 引脚表漏列；P5 机械图明确标 TP_RST，需在最终供应商针表中补齐。',
    24: '名称 VCI_EN 与 P8 描述模拟电源不一致；须确认其为使能输入或电源输入、有效电平及时序。',
}
pins = []
for number, symbol in enumerate(symbols, 1):
    pins.append(dict(pin=number, symbol=symbol, source='CWS05 v1.0 P5/P8',
                     status='需确认' if number in unclear else '文件中已列出，待与实物核对',
                     note=unclear.get(number, '数字接口按 1.8V 设计域评估；本表不是已完成连接的生产网表。' if symbol!='GND' else '地。')))
assert [p['pin'] for p in pins] == list(range(1, 25))

passives = []
for r in bom:
    ref = r['ref']
    if not ref.startswith(('R','C')):
        continue
    if ref.startswith('R'):
        if r['value'].startswith('0R') or ref in ['R603','R119']:
            target = '0402或更大，先算电流/功耗'
            reason = '电源跳线、LED 电流通路或串联电阻，核对 I²R、额定电流及浪涌。'
        else:
            target = '0201优先候选'
            reason = '核对精度、额定电压、温升、功耗和料号供货；不是仅替换图库封装。'
    elif r['package'] == 'SC0402':
        target = '0201优先候选'
        reason = '小容量去耦/滤波；按有效容量、ESR、介质、温漂及实际电压复核。DNP 调谐位保留合理焊盘。'
    elif r['value'] == '1uF/10V':
        target = '0201/0402按有效容量确定'
        reason = '优先核算小封装可用性；必须对照对应稳压器或芯片去耦要求。'
    else:
        target = '0402/0603/0805按电气要求确定'
        reason = '4.7~22uF 等储能电容不强制 0201；按工作电压下有效容量、纹波及电源稳定性选择。'
    passives.append(dict(ref=ref, value=r['value'], current_package=r['package'], assembly=r['assembly'],
                         target=target, condition=reason, implementation='候选规则，原理图/封装尚未变更'))
assert len(passives) == 101

for name, records in [('CWS05_24针接口核查.csv', pins), ('阻容0201选型方向.csv', passives)]:
    with (OUT / name).open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(records[0])); w.writeheader(); w.writerows(records)

state = {
    'date': '2026-09-11', 'status': '用户计划选用；接口待确认；尚未实现到 EDA',
    'display': 'YTL CWS05 / C50396528', 'source_pdf': str(PDF),
    'source_sha256': hashlib.sha256(PDF.read_bytes()).hexdigest(),
    'resolution': [390, 450], 'interface': 'QSPI', 'connector_pin_count': 24,
    'display_ic': 'CO5300AF', 'touch_ic': 'CST820', 'power_ic_drawing_label': 'BV6802',
    'digital_interface_voltage_v': 1.8, 'outline_with_coverglass_mm': [35.22,42.50,2.46],
    'dimension_caveat': 'P4 总体尺寸与 P5 CG/叠层图对照；不含展开 FPC，名称存在混用；最终按供应商受控图与实物。',
    'passive_policy': '普通低功耗电阻与小电容优先评估英制0201；电源与储能位置按电气要求例外。',
    'button_proposal': {'SW101':'表内不装直插键，保留RESET测试接点',
                        'SW102':'表内不装直插键，保留BOOT测试接点',
                        'SW401':'实际侧键按结构选型',
                        'SW402':'Home/唤醒并入表冠按压；复核唤醒GPIO和固件后移除单独键'},
    'schematic_changed': False, 'layout_fit_verified': False,
}
(OUT / '修订输入.json').write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf8')

report = '''CWS05 屏幕与佩戴版修订评估 · 2026-09-11

这份文件记录新的设计输入与选型方向，不是已修改完成的 EDA 原理图。当前在线电路仍是 v0.3，原有屏幕接口不能直接用于 CWS05。

**屏幕：适合作为接下来的选型候选，不能按现有接线直接替换。**

依据用户提供的 CWS05 v1.0、2024-06-13 规格书，已核查 P4~P12 及 P15 的相关尺寸、供电、针表、时序、光学参数和寿命说明；P5 的原始机械图也单独提取放大核查。

| 项目 | 文件内容 | 对设计的影响 |
|---|---|---|
| 面板 | 1.85英寸、390×450、QSPI | 保留 SF32LB58；RGB565 单帧351000字节，双帧702000字节，不含UI资源及其它内存 |
| 外形 | 含盖板总体约35.22×42.50×2.46mm，P5 CG标注与P4表对应 | FPC弯折、接头与其上器件仍需额外空间；不能把屏幕外形当可用板框 |
| 显示/触摸 | CO5300AF + CST820 | 触摸不能沿用 FT6146 选项 |
| 接口 | 24针，P5有QSPI四根数据线 | 取消表内2×20调试排针；须锁定实际配对连接器，不能只按针数选FPC座 |
| IO | P5注明1.8V；P6数字电源正常1.7~1.9V | v0.3 LCD/TP所在PA电源域为3.3V，必须验证1.8V引脚映射方案或加入合适的电平转换 |
| PMIC | P5标BV6802；P8的11/12脚VBAT典型3.7V | 要核实外部供电范围；不能把原转接板+5V接到VBAT |

官方 SiFli SDK 中已有 [CO5300 显示驱动](https://github.com/OpenSiFli/SiFli-SDK/tree/main/customer/peripherals/display/co5300) 与 [CST820 触摸驱动](https://github.com/OpenSiFli/SiFli-SDK/tree/main/customer/peripherals/touch_panel/cst820)。这是移植起点，不等同于已验证 CWS05 实物。图纸中的60Hz是面板测试帧率，不是我们的UI全屏刷新率保证。

需要补齐的直接设计输入：

- 配对连接器完整料号、朝向、高度及可读机械图。当前图中的局部连接器型号不足以可靠选型。
- VBAT允许范围、15脚VDD与16脚TP_VDD的外部供电要求、24脚VCI_EN究竟是使能还是电源输入。
- P8漏列18脚；P5明确列TP_RST。8脚ID、9/10脚接口模式/NC的处理按最终确认针表执行。
- CWS05专用的初始化序列及QSPI上/下电时序。P9/P10的显示时序图带MIPI标注，不能把它当作完整的该模组QSPI时序资料。

P6的ELVDD/ELVSS、VDD/VEE等包含面板内部工作电源，不能直接推导为主板要各产生一条电源。P12在600nit全白条件下列了200h寿命项；P15以亮度下降到某比例（举例95%）定义寿命，不能解释为200小时后损坏。具体衰减阈值、长期常亮指标和整模组VBAT输入电流仍需供应商确认。

**四个直插按键：佩戴版不应保留四个原型号。**

| 位号 | 现有网络/用途 | 佩戴版处理 |
|---|---|---|
| SW101 | RESET_N，复位主控 | 保留探针可达的复位接点，调试夹具操作 |
| SW102 | BOOT_MODE，配合复位进入下载模式 | 保留启动模式接点和正确上下拉，调试夹具操作 |
| SW401 | SIDE_SW_N / PB52，侧键 | 按表壳选小型侧按器件或侧键软板 |
| SW402 | HOME_N / PB54，Home/唤醒输入 | 计划并入J402的表冠按压；需核对PB51的低功耗唤醒能力或调整GPIO，然后改固件与电路 |

J402已单独预留CROWN_A、CROWN_B和CROWN_SW_N（PB51）。因此表冠按压加一个侧键可以作为外部交互方案，SW402无需再做一个独立实体键。复位/下载功能保留，但不占四个直插按键的体积。

**封装：改为0201优先评估，按电气需求允许较大封装。**

本文0402/0201均为英制：0402本体约1.0×0.5mm，0201约0.6×0.3mm。仅比较本体面积为0.50和0.18mm²，后者小64%；焊盘、间距、过孔和走线占用不会按同一比例下降。

- 低功耗上拉/下拉、分压、信号电阻：0201优先，核对功率、电压、精度与可采购料号。
- 100nF等小电容：0201优先，核对实际偏压下有效容量、介质与芯片去耦要求。
- 1µF：在0201与0402之间逐项核算；不能只按标称容量和耐压判断。
- 4.7~22µF储能、电源环路电容：按有效容量、稳定性和纹波选0402/0603/0805。
- 电源0Ω跳线、LED脉冲电流路径、串联电阻：按额定电流、I²R及温升评估，不能自动缩小。

村田说明高介电常数MLCC在直流偏压下有效容量可能下降，应用必须按实际条件验证：[官方说明](https://www.murata.com/en-us/support/faqs/capacitor/ceramiccapacitor/char/0005)。0201的焊盘、钢网、贴装和返修也必须与工厂匹配；本设计已有0.4mm球距BGA，不能再以便于普通手焊作为全板选型的主要标准。

已将现有101个阻容位号按上述规则整理成候选清单，尚未赋予未经验证的新料号。0402并非必然放不下，但最终板框、双面高度、FPC弯折和BGA扇出没有验证，不能承诺仅换0201就能装入表壳。

实施顺序：确认屏幕电源与接头资料 → 更新1.8V显示/触摸接口 → 清理调试实体按键 → 锁定0201及例外料号 → 在真实板框和高度限制下试摆、扇出和验证。
'''
(OUT / '评估与修订方向.md').write_text(report, encoding='utf8')
print(json.dumps({'screen_pins':len(pins), 'passives_reviewed':len(passives), 'output':str(OUT)}, ensure_ascii=False))
