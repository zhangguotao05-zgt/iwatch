"""Expand the SF58 carrier into a bare-chip schematic revision.

Pin inventory is extracted from SiFli's original PADS schematic. Peripheral
pages are retained from v0.1, and the module symbol is completely removed.
This is an editable engineering draft, not a fabrication release.
"""
from pathlib import Path
import re, json

BASE = Path(__file__).resolve().parent
OLD = BASE.parent / 'schematic-v0.1'
REFS = OLD / 'references'
source = (OLD / 'build_schematic.py').read_text(encoding='utf8')
pads = (REFS / 'SF32LB58-MOD-SCH-V1.0.1.txt').read_text(errors='replace')
part = pads.split('\nSF32LB58X_BGA256_P040_RD_V3 UND')[1].split('\nMEM-FLASH-')[0]
ballmap = dict(re.findall(r'^([A-Z]+\d+) 0 U (.+)$', part, re.M))
assert len(ballmap) == 256

prefix = source.split('# Module complete pad inventory.')[0]
prefix = prefix.replace("BASE / 'references/SF58-official.epro'", "BASE.parent / 'schematic-v0.1/references/SF58-official.epro'")
prefix = prefix.replace('原理图_v0.1', '原理图_v0.2_裸芯片').replace('HealthWatch-SF58-Offline-v0.1', 'HealthWatch-SF58-BareChip-v0.2').replace('healthwatch-sf58-v01/', 'healthwatch-sf58-v02/').replace('REV 0.1', 'REV 0.2')
prefix = prefix.replace("FP_MOD=footprint('9a3a065e0a944ecdb754546140ed3eec')\n", '')
oldmaptext = source[source.index('modmap='):source.index('groups={')]
scope = {}
exec(oldmaptext, scope)
modmap, oldnets = scope['modmap'], scope['module_nets']
gpio_to_ball = {}
shortnames = {}
for b, name in ballmap.items():
    m = re.search(r'(?:^|/)(P[AB]\d+)(?:/|$)', name)
    if m:
        short = m.group(1)
        short = short[:2] + str(int(short[2:])).zfill(2)
        gpio_to_ball[short] = b
        shortnames[b] = short
    else:
        shortnames[b] = name
chipnets = {gpio_to_ball[modmap[n]]: net for n, net in oldnets.items() if modmap[n] in gpio_to_ball}
chipnets.update({
    'B2': '+1V8', 'B3': '+1V8', 'C1': 'CORE_BUCK1_SW', 'D1': 'CORE_BUCK1_FB',
    'A2': 'CORE_BUCK2_SW', 'B1': 'CORE_BUCK2_FB', 'D2': 'CORE_LDO_HP',
    'C4': 'CORE_LDO_RET', 'C5': 'CORE_LDO_RTC', 'E20': '+1V8',
    'K18': '+3V3', 'J18': '+3V3', 'P5': '+3V3', 'P3': '+1V8',
    'H6': 'VIOA', 'P12': '+1V8', 'E17': '+1V8', 'M2': 'VDD_SIP', 'G1': 'VDD_SIP', 'L14': '+1V8',
    'F20': 'CORE_MIC_BIAS', 'F21': 'CORE_AUD_VREF', 'L21': 'CORE_ADC_VREF', 'K19': 'CORE_SDM_VREF',
    'B7': 'CORE_X32_IN', 'A6': 'CORE_X32_OUT', 'A17': 'CORE_X48_IN', 'A18': 'CORE_X48_OUT',
    'D4': 'RESET_N', 'E6': 'BOOT_MODE', 'A15': 'SIP_PWR_EN', 'A9': 'FLASH_PWR_EN',
    'M5': 'FLASH_CS_N', 'N1': 'FLASH_D2', 'L5': 'FLASH_D1', 'K5': 'FLASH_D3',
    'M1': 'FLASH_CLK', 'K4': 'FLASH_D0', 'P4': 'CORE_DSI_REXT', 'T5': 'CORE_USB_REXT',
})
groundballs = [b for b, n in ballmap.items() if 'VSS' in n or n in ['GPADC_VREFN', 'AUD_VREF_GND']]
chipnets.update({b: 'GND' for b in groundballs})
groups = {
    'A_PMU': ['B2','B3','C1','D1','A2','B1','D2','C4','C5','B4','B5'],
    'B_RAILS': ['E20','K18','J18','P5','P3','H6','P12','E17','M2','G1','L14','F20','F21','L21','K19'],
    'C_CLOCK_DEBUG': ['B7','A6','A17','A18','D4','E6'] + [gpio_to_ball[n] for n in ['PB36','PB37','PB07','PB11','PB54']],
    'D_MEMORY': ['A15','A9','M5','N1','L5','K5','M1','K4'],
    'B_DISPLAY': [gpio_to_ball[modmap[n]] for n in [38,79,80,81,82,83,84,85,86,89,90,94,95]],
    'C_SENSORS': [gpio_to_ball[modmap[n]] for n in [118,119,116,117,120,121,122,123,124,125,126,127,128,73,74]],
    'G_GROUND': groundballs,
}
used = [b for group in groups.values() for b in group]
assert len(used) == len(set(used))
sparegpio = [b for b in ballmap if b not in used and (b in shortnames and (shortnames[b].startswith(('PA','PB'))))]
groups['E_GPIO_SPARE'] = sparegpio[:(len(sparegpio)+1)//2]
groups['F_GPIO_SPARE'] = sparegpio[(len(sparegpio)+1)//2:]
used += sparegpio
groups['H_ANALOG_SPARE'] = [b for b in ballmap if b not in used]
assert sorted(b for group in groups.values() for b in group) == sorted(ballmap)

core = f'\n# Exact BGA ball inventory from SiFli original schematic; cross-checked with DS5801.\nballmap={ballmap!r}\nchipnets={chipnets!r}\ngroups={groups!r}\nshortnames={shortnames!r}\n'
core += r'''
units=[]
for name, balls in groups.items():
    pins=[]
    for i,b in enumerate(balls):
        n=shortnames[b]
        typ='Ground' if chipnets.get(b)=='GND' else 'Power' if name in ['A_PMU','B_RAILS'] and b not in ['C1','A2'] else 'Bidirectional'
        pins.append(pin(b,n,'L' if i<(len(balls)+1)//2 else 'R',typ))
    units.append((name,pins))
CHIPSID=symbol('SF32LB586VDD36',units,440)
CHIP=device('SF32LB586VDD36',CHIPSID,'','BGA256 8.5x6.5mm P0.4mm',
 'https://wiki.sifli.com/en/hardware/SF32LB58x-HW-Application.html')
def module(s,part,x,y):
    # Retains the old peripheral call sites, but now places the actual BGA SoC.
    s.place('U1',CHIP,part,x,y,{b:chipnets.get(b) for b in groups[part]},
        note='Bare SF32LB586VDD36, not SF32LB58-MOD. All units share this physical IC.')

# 01. Internal PMU external loop components.
s=Sheet('BARE CHIP PMU','U1 = SF32LB586VDD36 BGA256 | internal buck/LDO support | never apply battery voltage to the 1.8 V rails')
module(s,'A_PMU',400,1390)
for i,(a,b) in enumerate([('CORE_BUCK1_SW','CORE_BUCK1_FB'),('CORE_BUCK2_SW','CORE_BUCK2_FB')]):
    two(s,'L'+str(101+i),'L','4.7uH DCR<=0.4R Isat>=0.5A',a,b,1510,1370-i*290,
        pkg='2016 metric; reference WPN201610U4R7MT',note='Internal PMU buck loop; follow SiFli placement and return-current guidance')
    two(s,'C'+str(101+i*2),'C','4.7uF/10V',b,'GND',1360,1240-i*290)
    two(s,'C'+str(102+i*2),'C','100nF/10V',b,'GND',1970,1240-i*290)
for i,(v,net) in enumerate([('4.7uF/10V','CORE_LDO_HP'),('470nF/10V','CORE_LDO_RET'),('100nF/10V','CORE_LDO_RTC')]):
    two(s,'C'+str(105+i),'C',v,net,'GND',350+i*700,830)
for i,ball in enumerate(['B2','B3']):
    two(s,'C'+str(108+i*2),'C','10uF/10V','+1V8','GND',350+i*1050,640,note='Place at U1.'+ball+' PVDD input')
    two(s,'C'+str(109+i*2),'C','100nF/10V','+1V8','GND',820+i*1050,640,note='Place at U1.'+ball+' PVDD input')
s.note(120,450,[
 'L101/C101/C102: BUCK1 VSW -> inductor -> FB. L102/C103/C104: BUCK2 VSW -> inductor -> FB.',
 'The internal LDO nodes only take bypass capacitors. Do not connect them to +1V8 or +3V3.',
 'B4/B5 VDD_EXT remain open as in the official internal-PMU reference. Recheck for any external-core-supply mode.',
 'All 44 ground/reference-return balls are listed on the final sheet; connect to the common ground plane.',
 'Input power comes from the POWER RAILS sheet. Clock/reset and SiP power switching are shown on subsequent sheets.',
 'BGA footprint, stackup, fanout, crystal layout and assembly process require a separate PCB review.',
 'U1 is a purchased packaged IC soldered directly onto the custom mainboard; there is no core module in this BOM.'
])

# 02. All external power domains and analog bypass points.
s=Sheet('CHIP POWER DOMAINS','PA12-93 bank fixed at 3.3 V for the selected NOR + kit display adapter | PB / PA0-11 = 1.8 V')
module(s,'B_RAILS',400,1390)
caps=[('1uF/10V','+1V8','E20'),('1uF/10V','+3V3','K18'),('4.7uF/10V','+3V3','J18'),
 ('1uF/10V','+3V3','P5'),('4.7uF/10V','+1V8','P3'),('1uF/10V','VIOA','H6'),
 ('1uF/10V','+1V8','P12'),('1uF/10V','+1V8','E17'),('1uF/10V','VDD_SIP','M2'),
 ('1uF/10V','VDD_SIP','G1'),('1uF/10V','+1V8','L14'),('1uF/10V','CORE_MIC_BIAS','F20'),
 ('1uF/10V','CORE_AUD_VREF','F21'),('4.7uF/10V','CORE_ADC_VREF','L21'),('4.7uF/10V','CORE_SDM_VREF','K19')]
for i,(v,net,ball) in enumerate(caps):
    two(s,'C'+str(120+i),'C',v,net,'GND',1370+(i%2)*580,1400-(i//2)*125,note='Local bypass for U1.'+ball)
two(s,'R103','R','0R VIOA=3V3','+3V3','VIOA',350,820)
s.note(130,690,[
 'VDDIOSA/B = switched 1.8 V for the two SiP PSRAMs; VDDIOSC = always-on 1.8 V for SiP NOR.',
 'VIOA feeds both the external NOR bus and display/control PA12-93 signals. Do not change to 1.8 V with PY25Q128HA.',
 'A future 1.8 V-only panel requires level translation or a compatible Flash/bank remap; its exact FPC is still pending.',
 'USB/audio/DSI power bypass follows the reference, although those functions are disabled in this offline revision.',
 'Each capacitor has an associated BGA ball in its engineering note; place locally, not as one remote bulk capacitor bank.',
 'GPADC/AUD/SDM reference nodes are capacitor-only; they are not external power inputs.'
])

# 03. Real clock, reset, boot, SWD and UART circuit.
s=Sheet('CLOCK RESET AND DEBUG','48 MHz + 32.768 kHz crystals | 1.8 V SWD/UART | BOOT_MODE at VIOA = 3.3 V')
module(s,'C_CLOCK_DEBUG',420,1400)
xtal=ic('E1SB48E001G00E',[pin(1,'X1','L','Passive'),pin(2,'GND','L','Ground'),pin(3,'X3','R','Passive'),pin(4,'GND','R','Ground')],
 'https://wiki.sifli.com/en/hardware/SF32LB58x-HW-Application.html','Crystal 2016 metric 4pad',width=190)
s.place('Y101',xtal,'A',1530,1390,{'1':'CORE_X48_IN','2':'GND','3':'CORE_X48_OUT','4':'GND'},'48MHz CL8.8pF E1SB48E001G00E')
two(s,'Y102','Y','32.768kHz CL7pF ETST00327000LE','CORE_X32_IN','CORE_X32_OUT',1480,1160,pkg='Crystal 3215 metric 2pad')
for i,net in enumerate(['CORE_X48_IN','CORE_X48_OUT','CORE_X32_IN','CORE_X32_OUT']):
    two(s,'C'+str(140+i),'C','DNP load trim',net,'GND',1380+(i%2)*600,970-(i//2)*120,note='Reserve tuning pads; do not fit arbitrary 12pF/22pF values')
two(s,'R101','R','10k 1%','+1V8','RESET_N',1380,660)
two(s,'C144','C','100nF/10V','RESET_N','GND',1980,660)
two(s,'SW101','SW','RESET','RESET_N','GND',1380,500)
two(s,'R102','R','10k 1%','BOOT_MODE','GND',1980,500)
two(s,'SW102','SW','BOOT / HOLD TO DOWNLOAD','VIOA','BOOT_MODE',1380,330)
j=ic('DEBUG_1x07',[(str(i),n,'Passive','L') for i,n in enumerate(['VTREF_1V8','GND','SWDIO','SWCLK','RESET_N','MCU_TX','MCU_RX'],1)],'','Connector TBD',width=240)
s.place('J101',j,'A',430,860,{str(i):n for i,n in enumerate(['+1V8','GND','SWDIO_1V8','SWCLK_1V8','RESET_N','UART4_TX_1V8','UART4_RX_1V8'],1)})
s.note(150,460,[
 'J101 is for a true 1.8 V debugger/UART adapter. VTREF is a reference output, not an input supply.',
 'RESET_N must be pulled up to PVDD1 (+1V8). BOOT_MODE belongs to VIOA (+3V3).',
 'BOOT low = normal boot, high at reset = download. HOME/PB54 is on DISPLAY AND CONTROLS.',
 'Start with external crystal trim capacitors unpopulated for the selected CL values, then measure/calibrate.',
 'HCPU target up to 240 MHz; first bring-up uses conservative clock and Flash timing.',
 'Budget: 32 MB SiP PSRAM total, 1 MB SiP NOR, plus the external 16 MB NOR on the next sheet.'
])

# 04. Official SiP switch and external QSPI NOR topology.
s=Sheet('SIP AND RESOURCE FLASH','SF32LB586VDD36 contains 16+16 MB PSRAM + 1 MB NOR | U104 adds 16 MB 3.3 V QSPI NOR')
module(s,'D_MEMORY',430,1400)
flash=ic('PY25Q128HA-WXH-IR',[pin(8,'VCC','L','Power'),pin(1,'CS_N'),pin(6,'CLK'),pin(5,'D0','L','Bidirectional'),pin(2,'D1','R','Bidirectional'),pin(3,'D2_WP_N','R','Bidirectional'),pin(7,'D3_HOLD_N','R','Bidirectional'),pin(4,'GND','R','Ground'),pin(9,'EP','R','Ground')],
 'https://www.puyasemi.com/en/h_series653/3187.html','WSON8+EP 6x5mm; verify selected suffix',width=230)
s.place('U104',flash,'A',1580,1400,{'8':'FLASH_VDD','1':'FLASH_CS_N','6':'FLASH_CLK','5':'FLASH_D0','2':'FLASH_D1','3':'FLASH_D2','7':'FLASH_D3','4':'GND','9':'GND'})
pmos=ic('CJ3139K',[pin(1,'G','L','Input'),pin(2,'S','L','Passive'),pin(3,'D','R','Passive')],
 'https://downloads.sifli.com/hardware/files/documentation/SF32LB58-MOD-V1.0.1.zip','SOT723 P-MOS; official reference pinout',width=160)
nmos=ic('CJ3134K',[pin(1,'G','L','Input'),pin(2,'S','L','Passive'),pin(3,'D','R','Passive')],
 'https://downloads.sifli.com/hardware/files/documentation/SF32LB58-MOD-V1.0.1.zip','SOT723 N-MOS; official reference pinout',width=160)
for col,(v_in,v_out,en,gate) in enumerate([('+1V8','VDD_SIP','SIP_PWR_EN','SIP_GATE'),('VIOA','FLASH_VDD','FLASH_PWR_EN','FLASH_GATE')]):
    x=400+col*1100
    s.place('Q'+str(101+col*2),pmos,'A',x,990,{'1':gate,'2':v_in,'3':v_out})
    s.place('Q'+str(102+col*2),nmos,'A',x,770,{'1':en,'2':'GND','3':gate})
    two(s,'R'+str(110+col*3),'R','1M',v_in,gate,x-80,580)
    two(s,'R'+str(111+col*3),'R','1M',en,'GND',x+430,580)
    two(s,'R'+str(112+col*3),'R','0R DNP BYPASS',v_in,v_out,x-80,420,
        note='Bring-up bypass option only; normally DNP to retain the reference power switch')
    two(s,'C'+str(150+col),'C','1uF/10V',v_out,'GND',x+430,420)
two(s,'C152','C','100nF/10V','FLASH_VDD','GND',1940,990)
two(s,'R116','R','10k','FLASH_VDD','FLASH_CS_N',1400,1120)
two(s,'R117','R','10k','FLASH_VDD','FLASH_D3',2000,1120)
s.note(130,290,[
 'SiP PSRAM bus wiring is inside the package. PBR0 controls its 1.8 V switch; the 1 MB SiP NOR stays powered via VDDIOSC.',
 'PA74 controls external Flash power. MPI4 = PA30 CS, PA39 CLK, PA40 D0, PA37 D1, PA36 D2, PA38 D3.',
 'This bank is fixed at 3.3 V. Enable and settle FLASH_VDD before access; place bus pins Hi-Z before power-off.',
 'Pin map/power switch follows the SiFli N16R32N1 reference. Firmware BSP must retain correct SiP and Flash startup control.',
 'Module BOM removed. The final board purchases U1, U104, crystals and passives individually; no 24x24 mm core module.'
])
'''
peripheral = source[source.index('# 02. Protected'):source.index('# 08. Every physical module')]
peripheral = peripheral.replace('module(s,', 'module(s,')
peripheral = peripheral.replace("module(s,'B_DISPLAY',400,1400)", "module(s,'B_DISPLAY',340,1400)")
peripheral = peripheral.replace("module(s,'C_SENSORS',370,1400)", "module(s,'C_SENSORS',310,1400)")
peripheral = peripheral.replace('sheet 05', 'sheet 08')
end = r'''
# 11-12. Every physical BGA ball appears once, including grounds and unused I/O.
s=Sheet('SPARE GPIO BALLS','Complete SF32LB586VDD36 pin inventory | spare pins not routed in this offline revision')
module(s,'E_GPIO_SPARE',370,1400);module(s,'F_GPIO_SPARE',1500,1400)
s.note(140,420,[
 'U1 parts on every sheet are units of ONE physical BGA256 IC, with a shared unique ID and a single BOM entry.',
 'NC text marks deliberate unused pins. Configure unused digital inputs to avoid floating input current.',
 'Ball names come from the official SiFli PADS symbol; full alternate-function names are in chip-ball-audit.csv.',
 'Only actual device I/O is shown here. No module castellated-pad numbers remain in this project.'
])
s=Sheet('GROUND AND UNUSED ANALOG','All BGA ground/reference-return balls | disabled RF, USB, audio and MIPI functions')
module(s,'G_GROUND',370,1400);module(s,'H_ANALOG_SPARE',1470,1400)
two(s,'R118','R','10k 1%','CORE_DSI_REXT','GND',1460,920)
two(s,'R119','R','200R 1%','CORE_USB_REXT','GND',2050,920)
s.note(130,470,[
 'All VSS/AVSS/PVSS balls are explicitly grounded, including A3/A4 listed as VSS in DS5801.',
 'GPADC_VREFN, AUD_VREF_GND and SDMADC_VSS_VREF also return to ground; keep analog return paths quiet.',
 'DSI_REXT 10k and USB2_REXT 200R follow the official circuit. Their buses are not connected in this offline revision.',
 'BRF_ANT is deliberately open. Firmware must leave Bluetooth/RF disabled until an antenna network is designed.',
 'B4/B5 external-core supply pins are open on BARE CHIP PMU, following the selected internal-PMU topology.',
 'Footprints and PCB fabrication data are not released. Module footprint has been removed, not reused for the BGA.'
])
'''
tail = source[source.index('# Serialize project'):]
tail = tail.replace('BOM_v0.1.csv','BOM_v0.2.csv').replace('pin-net-map_v0.1.csv','pin-net-map_v0.2.csv')
tail = tail.replace("assert {int(p['pin']) for p in PINS if p['ref']=='U1'}==set(range(1,139))", "assert {p['pin'] for p in PINS if p['ref']=='U1'}==set(ballmap)")
tail = tail.replace("len(module_nets)","len(chipnets)").replace('module units','BGA units').replace('All 138 SF58 module pads represented','All 256 SF32LB586VDD36 BGA balls represented')
tail = tail.replace('IC footprints except U1 are not yet bound; no PCB release','BGA and most IC footprints are not yet bound; no PCB release')
tail += r'''
with (OUT/'chip-ball-audit.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.writer(f);writer.writerow(['ball','original_sifli_pin_name','schematic_short_name','net','unit'])
    for unit,balls in groups.items():
        for b in balls:writer.writerow([b,ballmap[b],shortnames[b],chipnets.get(b),unit])
'''
generated = prefix + core + peripheral + end + tail
(BASE/'build_schematic.py').write_text(generated,encoding='utf8')
exec(compile(generated, str(BASE/'build_schematic.py'), 'exec'), {'__file__': str(BASE/'build_schematic.py')})
