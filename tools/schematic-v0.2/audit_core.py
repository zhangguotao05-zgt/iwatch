"""Independent checks for the bare-chip conversion and native archive."""
from pathlib import Path
import json, csv, zipfile, re

BASE=Path(__file__).resolve().parent
OUT=BASE.parents[1]/'hardware'/'原理图_v0.2_裸芯片'
m=json.loads((OUT/'circuit-model.json').read_text(encoding='utf8'))
p={(x['ref'],x['pin']):x['net'] for x in m['pins']}
def on(ref,pin,net): assert p[(ref,pin)]==net,(ref,pin,p.get((ref,pin)),net)
def two(ref,a,b): assert {p[(ref,'1')],p[(ref,'2')]}=={a,b},ref

# Ball inventory is checked against a different native reference: PCB decal.
asc=(BASE.parent/'schematic-v0.1/references/SF32LB58-MOD-PCB_V1.0.1.asc').read_text(errors='replace')
decal=asc.split('\nBGA_256_P040_8500X6500_H094_B025 M ')[1].split('\nPAD ')[0]
balls=re.findall(r'^T[^\n]*\s([A-Z]+\d+)\s*$',decal,re.M)[:256]
assert len(balls)==len(set(balls))==256
assert {pin for ref,pin in p if ref=='U1'}==set(balls)

# Core regulator loops, independent capacitor-only nodes, reset and boot domains.
for ball,net in {'B2':'+1V8','B3':'+1V8','C1':'CORE_BUCK1_SW','D1':'CORE_BUCK1_FB','A2':'CORE_BUCK2_SW','B1':'CORE_BUCK2_FB','D2':'CORE_LDO_HP','C4':'CORE_LDO_RET','C5':'CORE_LDO_RTC','D4':'RESET_N','E6':'BOOT_MODE','H6':'VIOA','E17':'+1V8','P12':'+1V8','M2':'VDD_SIP','G1':'VDD_SIP','L14':'+1V8','B4':None,'B5':None,'A3':'GND','A4':'GND'}.items():on('U1',ball,net)
two('L101','CORE_BUCK1_SW','CORE_BUCK1_FB');two('L102','CORE_BUCK2_SW','CORE_BUCK2_FB')
two('R101','+1V8','RESET_N');two('C144','RESET_N','GND');two('R102','BOOT_MODE','GND');two('SW102','VIOA','BOOT_MODE')
two('R103','+3V3','VIOA')
for ref,node in [('C105','CORE_LDO_HP'),('C106','CORE_LDO_RET'),('C107','CORE_LDO_RTC')]:
    two(ref,node,'GND')
    assert sorted(a for a,b in p.items() if b==node)==sorted([('U1',{'C105':'D2','C106':'C4','C107':'C5'}[ref]),(ref,'1')])

# External NOR wiring must agree with the official PADS electrical connectivity.
refnets=json.loads((BASE.parent/'schematic-v0.1/references/module-reference-nets.json').read_text())
for fp,net in [('1','FLASH_CS_N'),('6','FLASH_CLK'),('5','FLASH_D0'),('2','FLASH_D1'),('3','FLASH_D2'),('7','FLASH_D3')]:
    r=[members for members in refnets.values() if ['U0102',fp] in members]
    assert len(r)==1
    cpu=[ball for ref,ball in r[0] if ref=='U0103-A']
    assert len(cpu)==1
    on('U1',cpu[0],net);on('U104',fp,net)
for q,gate,source,drain in [('Q101','SIP_GATE','+1V8','VDD_SIP'),('Q102','SIP_PWR_EN','GND','SIP_GATE'),('Q103','FLASH_GATE','VIOA','FLASH_VDD'),('Q104','FLASH_PWR_EN','GND','FLASH_GATE')]:
    for pn,net in [('1',gate),('2',source),('3',drain)]:on(q,pn,net)

# No component outside U1 has a changed endpoint from v0.1, except removed core items.
old=json.loads((OUT.parent/'原理图_v0.1'/'circuit-model.json').read_text(encoding='utf8'))
peripheral_count=0
for entry in old['pins']:
    ref=entry['ref']
    if ref=='U1' or int(re.search(r'\d+',ref)[0])<200:continue
    on(ref,entry['pin'],entry['net']);peripheral_count+=1
two('R701','LED1_DRV','GREEN1_K')

# Verify all native symbol/device/footprint references actually exist.
with zipfile.ZipFile(OUT/(m['title']+'.epro')) as z:
    assert z.testzip() is None
    pr=json.loads(z.read('project.json'))
    assert not any('SF32LB58-MOD' in d['title'] for d in pr['devices'].values())
    for did,d in pr['devices'].items():
        sid=d['attributes']['Symbol'];assert sid in pr['symbols'];assert 'SYMBOL/'+sid+'.esym' in z.namelist()
        fp=d['attributes'].get('Footprint')
        if fp: assert fp in pr['footprints'] and 'FOOTPRINT/'+fp+'.efoo' in z.namelist()
    for sch in pr['schematics'].values():assert len(sch['sheets'])==12

report={'status':'PASS: model/reference checks only','bga_balls':len(balls),'unchanged_peripheral_pins':peripheral_count,
 'checks':['BGA256 ball set equals official PCB decal ball set','Internal BUCK loops and capacitor-only LDO nodes checked','Reset is PVDD1 domain; BOOT is VIOA domain','SiP power domains and transistor terminal connections checked','Six NOR signals match official PADS netlist','All retained peripheral physical pin/net assignments unchanged','LED1 remains connected to GREEN1_K','Native library references resolve; no SF32LB58-MOD device remains'],
 'limitations':['Not an EDA DRC pass or a hardware bring-up result','Does not validate footprint geometry, component procurement or PCB layout','Display FPC, battery, LRA and optical mechanics remain pending','Firmware BSP, startup, power sequencing and memory tests are not yet run']}
(OUT/'validation-reference.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(report,ensure_ascii=False,indent=2))
