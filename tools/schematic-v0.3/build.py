"""Continue the existing schematic without changing previous revision files."""
from pathlib import Path
import json,re
from footprints import install

BASE=Path(__file__).resolve().parent
CFG=json.loads((BASE/'config.json').read_text(encoding='utf8'))
source=(BASE/CFG['source_builder']).read_text(encoding='utf8')
source=source.replace('原理图_v0.2_裸芯片','原理图_v0.3_封装与接口完善').replace('HealthWatch-SF58-BareChip-v0.2',CFG['project_title']).replace('healthwatch-sf58-v02/','healthwatch-sf58-v03/').replace('REV 0.2','REV 0.3')
source=source.replace('BOM_v0.2.csv','BOM_v0.3.csv').replace('pin-net-map_v0.2.csv','pin-net-map_v0.3.csv')
source=source.replace("'name':name,'type':typ}","'name':name,'type':typ,'id':pid}")
source=source.replace("pid,'Pin Type',typ,", "pid,'Pin Type',{'Input':'IN','Output':'OUT','Bidirectional':'BI'}.get(typ,typ),")
source=source.replace("self.text(px-25 if side=='L' else px+8,py,'NC','small')", "self.attr(c+'-'+p['id'],'NO_CONNECT','yes',px,py)\n                self.poly([px-4,py-4,px+4,py+4]); self.poly([px-4,py+4,px+4,py-4])")
source=source.replace('All labels are electrical nets. NC text = intentional open pin.','Named nets connect across sheets. Cross = EDA no-connect flag.')
source=source.replace('NC text marks deliberate unused pins.','EDA no-connect flags mark deliberate unused pins.')
source=source.replace('Footprints and PCB fabrication data are not released. Module footprint has been removed, not reused for the BGA.','BGA pad geometry is taken from the official chip footprint. PCB placement/routing and fabrication release remain pending.')
source=source.replace('BGA footprint, stackup, fanout, crystal layout and assembly process require a separate PCB review.','BGA footprint is bound; stackup, fanout, crystal layout and assembly process still require PCB review.')
source=source.replace('Green symbols use A/K logical pads. Physical pad mapping is intentionally not invented.','Green LED: physical pad 1 = anode; pads 2 and 3 = cathode. Both cathode pads are wired.')
source=source.replace("[pin('A','ANODE','L','Passive'),pin('K','CATHODE','R','Passive')]", "[pin('1','ANODE','L','Passive'),pin('2','CATHODE','R','Passive'),pin('3','CATHODE_THERMAL','R','Passive')]")
source=source.replace("{'A':'+5V_SW','K':'GREEN1_K'}", "{'1':'+5V_SW','2':'GREEN1_K','3':'GREEN1_K'}").replace("{'A':'+5V_SW','K':'GREEN2_K'}", "{'1':'+5V_SW','2':'GREEN2_K','3':'GREEN2_K'}")
source=source.replace("'Optical LED; A/K mapping to selected footprint requires verification'", "'OSRAM 2218, 3 physical pads; ADI reference land pattern'")
start=source.index('usb_pins=');end=source.index("two(s,'R201'",start)
source=source[:start]+'''usb_names={1:'A1B12_GND',2:'A4B9_VBUS',3:'A8_SBU1',4:'A5_CC1',5:'B7_DM2',6:'A6_DP1',7:'A7_DM1',8:'B6_DP2',9:'B8_SBU2',10:'B5_CC2',11:'B4A9_VBUS',12:'B1A12_GND',13:'SHELL1',14:'SHELL2',15:'SHELL3',16:'SHELL4',17:'MOUNT_NC1',18:'MOUNT_NC2'}
usb_pins=[pin(n,name,'L' if n in [1,4,10,12,13,14,15,16] else 'R','Passive') for n,name in usb_names.items()]
usb=ic('1012-16FGG0201R3',usb_pins,'https://wiki.sifli.com/board/sf32lb58x/SF32LB58-DevKit-LCD.html','SiFli official USB-C connector pin numbering',width=200)
usbmap={1:'GND',2:'USB_VBUS_RAW',4:'CC1',10:'CC2',11:'USB_VBUS_RAW',12:'GND',13:'GND',14:'GND',15:'GND',16:'GND'}
s.place('J201',usb,'A',310,1400,{str(n):usbmap.get(n) for n in usb_names})
''' + source[end:]
source=source.replace("'USB-C_POWER_ONLY'","'1012-16FGG0201R3'")
source=source.replace('PTC 0.5A / >=12V','1206L050/15YR 0.5A 15V')
source=source.replace('Select a real resettable fuse and audit its hold/trip ratings and footprint before PCB','0.5A hold is at room temperature. Derate input current at elevated ambient; validate fuse self-heating.')
source=source.replace("'32.768kHz CL6pF'","'32.768kHz CL6pF CM1610H32768DZB'")
source=source.replace("'WSON8+EP 6x5mm; verify selected suffix'","'WSON8+EP 6x5mm, WX suffix verified against Puya V2.3'")
source=source.replace('2.2uH Isat>=1.2A','2.2uH XFL3012-222MEC').replace('2.2uH Isat>=1.5A','2.2uH XFL3012-222MEC').replace('1.5uH Isat>=1.5A','1.5uH XFL3012-152MEC')
source=source.replace('Logical connector; encoder mechanics TBD','Defined 5-pad wire interface; final rotary encoder mechanics pending')
source=source.replace('Keyed battery connector TBD','Defined 3-pad wire interface: +, -, NTC; verify pack polarity')
source=source.replace('LRA resonance / voltage / model TBD','Defined 2-pad wire interface; LRA voltage/resonance pending')
source=source.replace('Connector TBD','Defined 1.27mm pitch pogo / wire pads')
source=source.replace('NC text denotes open pins; EDA no-connect markers still require ERC review','EDA native NO_CONNECT attributes are applied; UI DRC validation recorded separately')
source=source.replace('BGA and most IC footprints are not yet bound; no PCB release','Footprints and pin sets are bound; PCB placement/routing, power integrity and manufacturing review are not complete')
source=source.replace('External adapter required; confirm its revision / orientation before powering.','Purchased 1.85in QSPI AMOLED kit confirmed. Use its adapter; check orientation before powering.')
# All four tactile switches use an existing original-reference part with a ground housing pin.
needle="def two(s,ref,typ,value,a,b,x,y,pkg='',note=''):\n"
source=source.replace(needle,needle+'''    if typ=='SW':
        did=ic('TC-1109DE-C-C',[pin(1,'CONTACT','L','Passive'),pin(2,'CONTACT','R','Passive'),pin(3,'CASE','R','Ground')],'https://atta.szlcsc.com/upload/public/pdf/source/20211011/9F1885726FDDBACA9292254052EEAAAD.pdf','TC1109 4.5mm tactile with grounded case',width=150)
        s.place(ref,did,'A',x,y,{'1':a,'2':b,'3':'GND'},value+' / TC1109',note)
        return
''')
start=source.index('schid=uid(\'schematic\')')
pre,post=source[:start],source[start:]
ns={'__file__':str(BASE/'build_schematic.py')}
exec(compile(pre,str(BASE/'build_schematic.py'),'exec'),ns)

# Add accessible test pads without interrupting feedback loops or clock nets.
s=ns['Sheet']('TEST POINTS AND BRINGUP','1.0 mm probe pads | begin with a current-limited bench supply | no probes on crystal nodes')
td=ns['ic']('PCB_TEST_POINT',[ns['pin'](1,'TEST','L','Passive')],'','1.0mm PCB pad',width=100)
testnets=['GND','VSYS','+1V8','+3V3','+5V_SW','VDD_SIP','FLASH_VDD','RESET_N','BOOT_MODE','SENS_SCL','SENS_SDA','PPG_MFIO','PPG_RST_N','HAPTIC_EN','BOOST5_EN','VBAT_ADC']
for i,net in enumerate(testnets):s.place('TP'+str(i+1).zfill(3),td,'A',320+(i%4)*550,1390-(i//4)*210,{'1':net},net)
s.note(130,430,[
 'Power-up order: isolate external loads -> confirm VSYS / 1.8 V / 3.3 V -> reset and debug -> SiP / external Flash.',
 'Then enable display and touch, IMU, LRA and PPG individually. Start PPG LED currents low and measure 5 V droop.',
 'J101 is a 1.8 V debug interface. J202 / J402 / J501 are defined solder-wire pad banks, not purchased plug connectors.',
 'Full system supply current includes charging and transient loads. The regulator current rating is not a rail budget.',
 'A BOM/model pass or schematic DRC pass does not validate startup, sleep current, optics or PCB manufacturing.',
 'Read the revision notes and bring-up checklist before ordering boards. No routed PCB or Gerber is included.'
])
catalog,mapping=install(ns)
(BASE/'build_schematic.py').write_text(pre+'\n# Footprint bindings applied by build.py before serialization.\n'+post,encoding='utf8')
exec(compile(post,str(BASE/'build_schematic.py'),'exec'),ns)
print('Footprints bound:',len(mapping),'Physical BGA balls:',len(catalog['U1']['pads']))
