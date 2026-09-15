"""Read the saved v0.3 footprints and report size constraints; no EDA edits."""
from pathlib import Path
import csv
import hashlib
import json
import math
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'hardware'
SRC = BASE / '原理图_v0.3_封装与接口完善'
OUT = BASE / '佩戴版封装尺寸审查'
OUT.mkdir(parents=True, exist_ok=True)
EPRO = SRC / 'HealthWatch-SF58-BareChip-v0.3.epro'
bom = list(csv.DictReader((SRC / 'BOM_v0.3.csv').open(encoding='utf-8-sig')))


def extent(points):
    return (min(x for x, y in points), min(y for x, y in points),
            max(x for x, y in points), max(y for x, y in points))


def review_action(r):
    ref = r['ref']
    if ref == 'J401':
        return '必须改接口', '大排针移至外部调试转接板；表内按实物屏幕排线重新选 FPC 座并核对针序、电源和机械尺寸。'
    if ref == 'J201':
        return '整机接口重选', '优先将 USB-C 移至充电座，表内用充电触点；需配套保留输入限流及适当 ESD 保护。'
    if ref in ['SW101', 'SW102']:
        return '移出表内', '复位/下载按键由夹具触发或小型测试焊盘实现；不保留此直插按键。'
    if ref in ['SW401', 'SW402']:
        return '机械件重选', '根据侧键、表冠的受力及密封结构选择小型侧按器件或软板，不能沿用调试按键。'
    if ref == 'U104':
        return '优先缩小', '优先评估 PY25Q128HA 的 3×3 mm USON；核对可采购订货码、电压、SF32LB58 驱动、QE/复位/休眠时序，不仅改封装名称。'
    if ref == 'U502':
        return '优先缩小', '可评估 DRV2605LYZFR 的 9 球 DSBGA；与现有 10 引脚 DGS 要重新映射管脚并核对贴装能力。'
    if r['package'] == 'SC0603-S':
        if r['value'] == '1uF/10V':
            return '缩小候选', '优先评估 0402；按实际电压、温度、容差和有效容量确定料号，不能先直接缩焊盘。'
        return '电气条件优先', '4.7/10 uF 需审查直流偏压下有效容量及电源稳定性；合格才缩小，必要时保留 0603。'
    if ref in ['C702', 'C703']:
        return '电气条件优先', '22 uF/10 V、0805 用于 LED 脉冲供电；依据偏压有效容量、纹波及压降核算，不统一缩成 0201。'
    if ref.startswith('L') or ref == 'F201':
        return '电流与温升审查', '按峰值电流、饱和电流、直流电阻、温升及高度评估小封装；不得只看标称电感或电流。'
    if ref == 'U1':
        return '保留并做扇出验证', '8.5×6.5 mm BGA256，0.4 mm 球距；先试摆和扇出，层数/微孔方案由制造能力与走线验证确定。'
    if ref in ['U601', 'D701', 'D702', 'D703']:
        return '受光学布局约束', '预留底部皮肤接触、LED/PD 间距、遮光墙及窗口；单颗器件面积不代表传感器组件总面积。'
    if ref.startswith('TP') or ref.startswith('J'):
        return '集中与减量', '按测试夹具和结构集中布置，尽量复用测试点；空间需包含探针可达范围。'
    if r['package'] in ['SC0402', 'SR0402']:
        return '可保留0402', '现有英制 0402 本体约 1.0×0.5 mm；仅局部高密度区按电气参数及贴装能力评估 0201。'
    return '试摆评估', '依据实际焊盘、器件高度、布线和装配间距纳入试摆；未承诺可装入表壳。'


records = []
with zipfile.ZipFile(EPRO) as z:
    for r in bom:
        rows = [json.loads(line) for line in z.read('FOOTPRINT/' + r['footprint'] + '.efoo').decode().splitlines()]
        body_points, pad_points = [], []
        for row in rows:
            if row[0] == 'POLY' and row[4] == 48:
                path = row[6]
                assert all(isinstance(t, (int, float)) or t == 'L' for t in path)
                xy = [t for t in path if isinstance(t, (int, float))]
                assert len(xy) % 2 == 0
                body_points += [(xy[i] * .0254, xy[i+1] * .0254) for i in range(0, len(xy), 2)]
            if row[0] == 'PAD':
                x, y, angle = row[6] * .0254, row[7] * .0254, math.radians(row[8])
                shape = row[10]
                assert shape[0] in ['RECT', 'ELLIPSE', 'OVAL']
                w, h = shape[1] * .0254, shape[2] * .0254
                for sx in [-1, 1]:
                    for sy in [-1, 1]:
                        dx, dy = sx * w / 2, sy * h / 2
                        pad_points.append((x + dx * math.cos(angle) - dy * math.sin(angle),
                                           y + dx * math.sin(angle) + dy * math.cos(angle)))
        assert body_points and pad_points, r['ref']
        b = extent(body_points)
        e = extent(body_points + pad_points)
        bw, bh, ew, eh = b[2]-b[0], b[3]-b[1], e[2]-e[0], e[3]-e[1]
        status, action = review_action(r)
        records.append(dict(ref=r['ref'], value=r['value'], package=r['package'], assembly=r['assembly'],
                            body_outline_w_mm=round(bw, 3), body_outline_h_mm=round(bh, 3),
                            body_and_pad_envelope_w_mm=round(ew, 3), body_and_pad_envelope_h_mm=round(eh, 3),
                            body_and_pad_envelope_mm2=round(ew*eh, 3), priority=status, action=action))

with (OUT / '逐位号尺寸与整改.csv').open('w', encoding='utf-8-sig', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(records[0]))
    w.writeheader()
    w.writerows(records)

byref = {r['ref']: r for r in records}
total = sum(r['body_and_pad_envelope_mm2'] for r in records)
interfaces = sum(byref[ref]['body_and_pad_envelope_mm2'] for ref in ['J401','J201','SW101','SW102','SW401','SW402'])
summary = dict(source=str(EPRO), source_sha256=hashlib.sha256(EPRO.read_bytes()).hexdigest(),
               reviewed_positions=len(records), simple_envelope_sum_mm2=round(total, 2),
               large_header_usb_four_switches_sum_mm2=round(interfaces, 2),
               caveat='Sum of per-footprint body-plus-pad rectangles, not PCB area, not a packing/routing/3D validation. Includes DNP and PCB pad features. No board outline is frozen.')
(OUT / '审查依据.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf8')

def size(ref):
    r = byref[ref]
    return f"{r['body_and_pad_envelope_w_mm']:g}×{r['body_and_pad_envelope_h_mm']:g}"

text = f'''佩戴版封装尺寸审查 · 2026-09-11

结论：不能确认 v0.3 能放进目标手表。现有版本完成的是原理图与封装绑定，未完成按表壳约束的布局、布线与高度验证。尺寸审查已覆盖保存版本的 {len(records)} 个位号；本次没有修改在线 EDA 或现有生产文件。

下表为保存工程中“本体外形图与铜焊盘”的联合外接矩形，单位 mm，不含走线、插拔空间、元件高度和装配间距。本体外形图也可能含按键执行部位。不能把这些矩形简单相加或除以两面，作为可制造 PCB 的面积。

| 器件 | 当前尺寸/数量 | 处理方向 |
|---|---|---|
| J401 屏幕排针 | {size('J401')} | 必须移出表内；最终按实物裸屏资料选 FPC 接口 |
| J201 USB-C | {size('J201')} | 优先移至充电座，表内充电触点与输入保护另做设计 |
| SW101/102/401/402 | 各 {size('SW101')}，4 个直插件 | 调试键改夹具；侧键、表冠按结构重选 |
| U104 外部 NOR Flash | {size('U104')}；本体 6×5 | 优先评估同系列 3×3 mm USON，供电、供货和固件兼容性一并核对 |
| U502 触觉驱动 | {size('U502')} | 评估同系列 DSBGA，重新映射管脚和审核焊盘 |
| 电容 | 41 个英制 0603，其中 18 个 1 µF | 1 µF 优先评估 0402；其余按实际有效容量决定 |
| 电阻/小电容 | 37 个 0402 电阻、21 个 0402 电容 | 已是小封装；局部按需求评估 0201 |
| C702/C703 | 2 个 0805、22 µF/10 V | 先验证 LED 脉冲负载与偏压容量，不能统一缩小 |
| U1 主控 | {size('U1')}，0.4 mm 球距 | 保留，先做局部去耦与 BGA 扇出验证 |
| L301/L302/L303 | 各 {size('L301')} | 缩小前审查饱和电流、DCR、温升和纹波 |

全部位置的上述矩形简单相加为 {total:.1f} mm²；仅 J401、USB-C 和四个直插按键就占其中 {interfaces:.1f} mm²。这个数只用于指出优化优先级，不是单面/双面板的最小面积结论；穿孔器件还会占用另一侧的空间。DNP 位置保留焊盘时仍占布局空间。

替换依据：TI 官方资料列有 DRV2605L 的 YZF 9 球 DSBGA 与 DGS 10 引脚 VSSOP 两种封装；因此缩小方向可行，但并非原封装直接替换。[TI 产品资料](https://www.ti.com/product/DRV2605L)。普冉官方 PY25Q128HA 产品页列有 USON8 3×3×0.55 mm，可作为 Flash 的优先缩小方向；仍需锁定可采购订货码并重新核对焊盘和供电。[普冉产品资料](https://www.puyasemi.com/en/h_series653/3187.html)。本次没有把候选写成已完成采购验证的替代料。

下一轮落地顺序：

1. 用屏幕总成、FPC 弯折区、电池、表冠、LRA、PPG 后盖组件建立壳内空间与禁布区；46 mm 是整表外形目标，不是 PCB 可用边长。
2. 拿掉表内的大排针和调试按键，确定充电接口，再形成小型化 BOM。电源芯片本身多数已为约 1.5×2 至 3×3 mm 封装，不是所有器件都要换。
3. 在实际可用板框内试摆双面器件，分别核对每一面的可用高度。PPG 必须满足光路和遮光结构，不能只紧密堆放。
4. 局部验证 BGA256 的 0.4 mm 扇出、电源热回路、QSPI 和回流路径，据此与板厂确定层叠、孔型和工艺。不能用“默认两层”或“上六层就一定能放下”作为结论。
5. 完成器件外形/高度模型、结构干涉检查和关键布线可行性后，才确认板尺寸并进入正式布板。

目前可明确排除的是“把现有验证板按原封装直接作为佩戴版”；还不能给出已验证的最小 PCB 长宽。缺少的是空间输入和实际布局验证，不能用面积估算替代。

逐位号细节见同目录 CSV；审查来源与工程 SHA256 见 JSON。当前电路不因本次审查而更改。
'''
(OUT / '封装尺寸审查.md').write_text(text, encoding='utf8')
print(json.dumps(summary, ensure_ascii=False, indent=2))
print('KEY_ENVELOPES_MM', {ref: size(ref) for ref in ['J401','J201','SW101','U104','U502','U1']})
print('REPORT', str(OUT / '封装尺寸审查.md'))
