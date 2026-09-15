"""Build a native EasyEDA Pro 1.8 footprint and a self-contained review project.

Dimensional values in config.json are millimetres. The legacy EasyEDA Pro
serialization uses mil; SLOT/OVAL records follow the native SF58 reference
project in this workspace. No polarity or unmarked mechanical tolerance is
assumed. Source drawing geometry is used only for explicitly marked estimates.
"""
from pathlib import Path
import json, math, uuid, zipfile, hashlib, csv

BASE = Path(__file__).resolve().parent
C = json.loads((BASE / 'config.json').read_text(encoding='utf-8'))
D = C['design_mm']
O = C['documented_mm']
I = C['inferred_mm']
OUT = Path(C['output_directory'])
OUT.mkdir(parents=True, exist_ok=True)
NAME = C['name']


def mil(mm):
    return round(mm / 0.0254, 6)


def uid(key):
    return uuid.uuid5(uuid.NAMESPACE_URL, 'healthwatch/magnetic-4mm/r01/' + key).hex


def dump(rows):
    return '\n'.join(json.dumps(r, ensure_ascii=False, separators=(',', ':')) for r in rows) + '\n'


def attr(ident, key, value, layer=13, x=None, y=None, visible=0, height=.65, parent=''):
    return ['ATTR', ident, 0, parent, layer,
            mil(x) if x is not None else None, mil(y) if y is not None else None,
            key, str(value), 0, visible, 'default', mil(height), mil(.12),
            0, 0, 4, 0, 0, 0, 0, 0]


def poly(ident, layer, width, path):
    return ['POLY', ident, 0, '', layer, mil(width), path, 0]


def circle(ident, layer, diameter, stroke=.1):
    return poly(ident, layer, stroke, ['CIRCLE', 0, 0, mil(diameter / 2)])


def xy_path(points):
    return [mil(points[0][0]), mil(points[0][1]), 'L'] + [mil(v) for p in points[1:] for v in p]


# Reuse only layer definitions and primitive visibility settings, never geometry.
with zipfile.ZipFile(BASE.parent / 'schematic-v0.1/references/SF58-official.epro') as z:
    template = [json.loads(l) for l in z.read('FOOTPRINT/12dc0c7586c648cbbbd35df935293e88.efoo').decode('utf-8').splitlines() if l.strip()]
layers = [r for r in template if r and r[0] == 'LAYER' and (r[1] <= 14 or r[1] == 47)]
layers += [['LAYER', 71, 'CUSTOM', 'Courtyard_REF', 3, '#00cccc', 1, '#006666', 1]]
settings = [r for r in template if r and r[0] == 'PRIMITIVE']


def header(kind):
    return [
        ['DOCTYPE', kind, '1.8'],
        ['HEAD', {'editorVersion': '2.2.40.3', 'importFlag': 0}],
        ['CANVAS', 0, 0, 'mm', mil(.5), mil(.5), mil(.05), mil(.05), mil(.01), mil(.01), 1, 0, 5],
        *layers, ['ACTIVE_LAYER', 1], ['NET', '', None, None, 1, None, 0, None],
        *settings,
    ]


def pad(ident, number, x, hole, copper, angle=0):
    # The last two expansion values are inoperative on through-hole pads;
    # no custom paste shapes are added. Multi-layer pads have plated barrels.
    return ['PAD', ident, 0, '', 12, str(number), mil(x), 0, angle,
            hole, copper, [], 0, 0, 0, 1, 0,
            mil(D['solder_mask_expansion_each_side']), mil(D['solder_mask_expansion_each_side']),
            0, 0, 0, None, None, None, None, []]


footprint = header('FOOTPRINT')
footprint += [
    pad('pad1', 1, 0, ['ROUND', mil(D['center_hole_diameter']), mil(D['center_hole_diameter'])],
        ['ELLIPSE', mil(D['center_pad_diameter']), mil(D['center_pad_diameter'])]),
]
for number, sign in [(2, -1), (3, 1)]:
    # Native SLOT length is the full end-to-end length, NOT the milling travel.
    # Local length is X; rotate both the slot and OVAL pad by 90 degrees.
    footprint.append(pad('pad' + str(number), number, sign * D['side_slot_center_x'],
                         ['SLOT', mil(D['side_slot_length_y']), mil(D['side_slot_width_x'])],
                         ['OVAL', mil(D['side_pad_length_y']), mil(D['side_pad_width_x'])], 90))

footprint += [
    circle('silk', 3, D['silkscreen_diameter'], D['silkscreen_stroke']),
    circle('flange_ref', 9, I['flange_diameter']),
    circle('boss_ref', 9, O['mating_boss_diameter']),
]
cx, cy = D['reference_courtyard_width'] / 2, D['reference_courtyard_height'] / 2
footprint.append(poly('courtyard_ref', 71, .05, xy_path([(-cx,-cy),(cx,-cy),(cx,cy),(-cx,cy),(-cx,-cy)])))
# Pin envelopes in the document layer are intentionally reference geometry.
footprint.append(circle('center_pin_ref', 13, O['center_pin_diameter'], .04))
for number, sign in [(2, -1), (3, 1)]:
    half = O['side_tab_width_y'] / 2
    ys = [-half + i * 2 * half / 48 for i in range(49)]
    pts = [(sign * math.sqrt(O['side_tab_outer_radius'] ** 2 - y*y), y) for y in ys]
    pts += [(sign * math.sqrt(I['side_tab_inner_radius'] ** 2 - y*y), y) for y in reversed(ys)]
    footprint.append(poly('tab_ref_' + str(number), 13, .04, xy_path(pts + [pts[0]])))
footprint += [
    attr('fp_name', 'Footprint', NAME),
    attr('reference', 'Designator', 'J?', 3, 0, 3.75, 1, .8),
    attr('draft', 'Review status', 'DRAFT - VERIFY FIT', 13, 0, -3.8, 1, .6),
    attr('body_height', 'Body height (mm)', O['body_height']),
    attr('unknown', 'Unverified dimensions', 'Side-tab inner radius/thickness and flange OD inferred; slot pitch is a design choice.'),
    attr('mapping', 'Pad mapping', '1=center; 2=left tab; 3=right tab; polarity/common nets not specified.'),
    attr('datum', 'Datum', 'Top PCB view; origin at center pin; mm.'),
    attr('source', 'Source drawing', Path(C['source_pdf']).name),
]
for number, x in [(1, 0), (2, -D['side_slot_center_x']), (3, D['side_slot_center_x'])]:
    footprint.append(attr('pinlabel'+str(number), 'Pad label', number, 13, x, 1.6, 1, .45))

raw = dump(footprint)
fp_file = OUT / (NAME + '.efoo')
fp_file.write_text(raw, encoding='utf-8')
fid, pid = uid('footprint'), uid('preview-pcb')
meta = {
    'schematics': {}, 'pcbs': {pid: '4mm磁吸母头_封装预览_非生产PCB'}, 'panels': {}, 'symbols': {},
    'footprints': {fid: {'title': NAME, 'source': '', 'version': 1, 'type': 4,
        'desc': '封装草稿；侧脚几何含比例推算，槽孔中心距为设计取值，须实物复核。',
        'tags': {'parent_tag': [], 'child_tag': []}, 'custom_tags': ''}},
    'devices': {}, 'boards': {},
    'config': {'title': '4mm磁吸母头_封装草稿_R01', 'cbbProject': False, 'defaultSheet': pid, 'editorVersion': '2.2.40.3'},
}
pcb = header('PCB') + [
    ['COMPONENT', 'j1', 0, 1, 0, 0, 0, {'Unique ID': uid('j1')}, 0],
    attr('j1_fp', 'Footprint', fid, 3, parent='j1'),
    attr('j1_ref', 'Designator', 'J1', 3, 0, 3.75, 1, .8, 'j1'),
]
project_file = OUT / '4mm磁吸母头_嘉立创专业版_封装草稿.epro'
with zipfile.ZipFile(project_file, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('project.json', json.dumps(meta, ensure_ascii=False, indent=2))
    z.writestr('FOOTPRINT/' + fid + '.efoo', raw)
    z.writestr('PCB/' + pid + '.epcb', dump(pcb))

with (OUT / '焊盘尺寸_mm.csv').open('w', encoding='utf-8-sig', newline='') as f:
    w = csv.writer(f)
    w.writerow(['焊盘号', '物理位置', 'X_mm', 'Y_mm', '铜宽X_mm', '铜长Y_mm', '孔宽X_mm', '孔长Y_mm', '孔类型', '注意'])
    w.writerow([1, '中心针', 0, 0, D['center_pad_diameter'], D['center_pad_diameter'], D['center_hole_diameter'], D['center_hole_diameter'], '镀铜圆孔', '未指定正负极'])
    for num, sign in [(2,-1),(3,1)]:
        w.writerow([num, '左侧脚' if sign<0 else '右侧脚', sign*D['side_slot_center_x'], 0,
                    D['side_pad_width_x'], D['side_pad_length_y'], D['side_slot_width_x'], D['side_slot_length_y'], '镀铜长圆槽孔', '孔距与尺寸须实物试装；两侧未预设共网'])

# Validate the serialized pads, including their effective rotated dimensions.
parsed = [json.loads(l) for l in fp_file.read_text(encoding='utf-8').splitlines()]
pads = [r for r in parsed if r[0] == 'PAD']
assert {p[5] for p in pads} == {'1','2','3'}
assert all(p[4] == 12 and p[15] == 1 for p in pads)
ids = [r[1] for r in parsed if r[0] in ('PAD','POLY','ATTR')]
assert len(set(ids)) == len(ids)
def effective_dims(values, angle):
    a = math.radians(angle)
    w,h = values[1]*.0254, values[2]*.0254
    return abs(w*math.cos(a))+abs(h*math.sin(a)), abs(w*math.sin(a))+abs(h*math.cos(a))
for p in pads[1:]:
    assert p[9][0] == 'SLOT' and p[10][0] == 'OVAL'
    assert math.isclose(abs(p[6])*.0254, D['side_slot_center_x'], abs_tol=1e-7)
    hx,hy=effective_dims(p[9],p[8]+p[14])
    px,py=effective_dims(p[10],p[8])
    assert math.isclose(hx,D['side_slot_width_x'],abs_tol=1e-7)
    assert math.isclose(hy,D['side_slot_length_y'],abs_tol=1e-7)
    assert math.isclose(px,D['side_pad_width_x'],abs_tol=1e-7)
    assert math.isclose(py,D['side_pad_length_y'],abs_tol=1e-7)
ring = min((D['center_pad_diameter']-D['center_hole_diameter'])/2,
           (D['side_pad_width_x']-D['side_slot_width_x'])/2,
           (D['side_pad_length_y']-D['side_slot_length_y'])/2)
gap = D['side_slot_center_x']-D['side_pad_width_x']/2-D['center_pad_diameter']/2
mask_gap = gap-2*D['solder_mask_expansion_each_side']
side_cu_radius = math.hypot(D['side_slot_center_x'],(D['side_pad_length_y']-D['side_pad_width_x'])/2)+D['side_pad_width_x']/2
silk_gap=D['silkscreen_diameter']/2-D['silkscreen_stroke']/2-side_cu_radius-D['solder_mask_expansion_each_side']
clearances=[]
for n in range(1001):
    y=-O['side_tab_width_y']/2+n*O['side_tab_width_y']/1000
    for r in (O['side_tab_outer_radius'],I['side_tab_inner_radius']):
        x=math.sqrt(r*r-y*y)
        dy=max(0,abs(y)-(D['side_slot_length_y']-D['side_slot_width_x'])/2)
        clearances.append(D['side_slot_width_x']/2-math.hypot(x-D['side_slot_center_x'],dy))
fit=min(clearances)
assert ring>=.2999 and gap>=.2499 and mask_gap>=.1499 and silk_gap>=.1499 and fit>=.08
with zipfile.ZipFile(project_file) as z:
    assert z.testzip() is None
    assert z.read('FOOTPRINT/'+fid+'.efoo').decode('utf-8')==raw
    assert json.loads(z.read('project.json'))['footprints'][fid]['title']==NAME
report = {
    'footprint': NAME, 'format': 'EasyEDA Pro legacy 1.8, based on native reference records',
    'pad_count': len(pads), 'pad_numbers': [p[5] for p in pads],
    'minimum_annular_ring_mm': ring, 'minimum_copper_clearance_mm': gap,
    'minimum_solder_mask_web_mm': mask_gap, 'silkscreen_to_mask_minimum_mm': silk_gap,
    'nominal_inferred_tab_to_slot_minimum_mm': fit,
    'project_zip_integrity': 'pass', 'serialized_pad_rotation_and_mm_roundtrip': 'pass',
    'native_editor_import_and_DRC': 'not performed', 'physical_fit': 'not verified',
    'source_sha256': hashlib.sha256(Path(C['source_pdf']).read_bytes()).hexdigest(),
    'limitations': ['No mechanical tolerances or electrical pinout in source drawing.',
                    'Side tab inner radius/thickness and flange diameter are estimates.',
                    'Slot pitch is a proposed design value, not a dimension quoted from the supplier.'],
}
(OUT / '校核记录.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT / '封装参数.json').write_text(json.dumps(C,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=True,indent=2))
print('CREATED',fp_file)
print('CREATED',project_file)
