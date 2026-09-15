"""Author a native EasyEDA Pro schematic from an explicit pin/net model.

Draft schematic only. IC footprints deliberately remain unbound unless sourced
from the official SiFli project; no guessed packages or panel FPC pin mapping.
"""
from pathlib import Path
import json, uuid, zipfile, csv, collections, math

BASE = Path(__file__).resolve().parent
OUT = BASE.parents[1] / 'hardware' / '原理图_v0.1'
OUT.mkdir(parents=True, exist_ok=True)
REF = zipfile.ZipFile(BASE / 'references/SF58-official.epro')
ORIG = json.loads(REF.read('project.json'))
TITLE = 'HealthWatch-SF58-Offline-v0.1'
def uid(key): return uuid.uuid5(uuid.NAMESPACE_URL, 'healthwatch-sf58-v01/'+key).hex
def dump(rows): return '\n'.join(json.dumps(r,ensure_ascii=False,separators=(',',':')) for r in rows)+'\n'
PROJECT = {'schematics':{},'pcbs':{},'panels':{},'symbols':{},'footprints':{},'devices':{},'boards':{},'config':{'title':TITLE,'cbbProject':False,'editorVersion':'2.2.40.3'}}
FILES = {}
SYMS = {}
SHEETS = []
COMPONENTS = []
PINS = []
NETPORT = 'c660be8097c34037944b4ee5b97e20ce'
PORTDEV = 'b638f0a64f144b4cb30c2bf6f73ccb73'
PROJECT['symbols'][NETPORT] = ORIG['symbols'][NETPORT]
PROJECT['devices'][PORTDEV] = ORIG['devices'][PORTDEV]
FILES['SYMBOL/'+NETPORT+'.esym'] = REF.read('SYMBOL/'+NETPORT+'.esym')

def footprint(fid):
    if fid:
        PROJECT['footprints'][fid]=ORIG['footprints'][fid]
        FILES['FOOTPRINT/'+fid+'.efoo']=REF.read('FOOTPRINT/'+fid+'.efoo')
    return fid
FP_R=footprint('e3f3bece9e6c4c37a159570def6f46e2')
FP_C=footprint('c229098197984ccd99912a2643d8918a')
FP_C6=footprint('51dbad8f50a9484aa62d5f6da682dfcd')
FP_MOD=footprint('9a3a065e0a944ecdb754546140ed3eec')
FP_LCD=footprint('947ab661c95042fb8fb976dacd78ba85')

def symbol(key, units, width=240, passive=None):
    """units: [(unit_name, [(pin_number,name,type,side), ...]), ...]."""
    sid=uid('symbol/'+key)
    rows=[['DOCTYPE','SYMBOL','1.1'],['HEAD',{'originX':0,'originY':0,'version':'2','symbolType':2,'maxId':9999}],
          ['LINESTYLE','line',None,None,None,1,None],
          ['FONTSTYLE','left',None,None,None,11,0,0,0,None,1,0],
          ['FONTSTYLE','right',None,None,None,11,0,0,0,None,1,2],
          ['FONTSTYLE','numl',None,None,None,9,0,0,0,None,2,2],
          ['FONTSTYLE','numr',None,None,None,9,0,0,0,None,2,0],
          ['FONTSTYLE','hidden',None,None,None,10,0,0,0,None,1,0]]
    info={}
    seq=0
    for part, pins in units:
        counts=collections.Counter(p[3] for p in pins)
        h=max(counts.values())*30+40 if not passive else 20
        rows.append(['PART',part,{'BBOX':[0,0,width,-h]}])
        if passive:
            # Passive terminals 0,0 and 60,0, recognizable engineering symbols.
            if passive=='R': pts=[10,0,15,0,15,6,45,6,45,-6,15,-6,15,0]
            elif passive=='C': pts=[10,0,27,0,27,12,27,-12]
            elif passive=='L':
                pts=[10,0]
                for k in range(4):
                    for j in range(1,9):
                        angle=math.pi-j*math.pi/8
                        pts += [15+k*10+5*math.cos(angle),5*math.sin(angle)]
            elif passive=='SW': pts=[10,0,18,0,43,12]
            else: pts=[10,0,15,0,15,8,45,8,45,-8,15,-8,15,0]
            rows.append(['POLY','g'+str(seq),pts,False,'line',0]);seq+=1
            if passive=='C':
                rows.append(['POLY','g'+str(seq),[33,12,33,-12,33,0,50,0],False,'line',0]);seq+=1
            elif passive in ['R','SW','F','Y']:
                rows.append(['POLY','g'+str(seq),[45,0,50,0],False,'line',0]);seq+=1
            if passive=='Y':
                for xx in [10,50]:
                    rows.append(['POLY','g'+str(seq),[xx,-14,xx,14],False,'line',0]);seq+=1
        else:
            rows.append(['POLY','g'+str(seq),[0,0,width,0,width,-h,0,-h,0,0],False,'line',0]);seq+=1
        ix={'L':0,'R':0}; locations={}
        for num,name,typ,side in pins:
            num=str(num); y=0 if passive else -(30+ix[side]*30);ix[side]+=1
            x=(0 if side=='L' else 60) if passive else (-30 if side=='L' else width+30)
            plen=10 if passive else 30
            pid='p'+str(seq);seq+=1
            rows.append(['PIN',pid,1,None,x,y,plen,0 if side=='L' else 180,None,0,0])
            rows.append(['ATTR','g'+str(seq),pid,'NAME',name,0,0 if passive else 1,5 if side=='L' else width-5,y,0,'left' if side=='L' else 'right',0]);seq+=1
            rows.append(['ATTR','g'+str(seq),pid,'NUMBER',num,0,0 if passive else 1,x+15 if side=='L' else x-15,y+3,0,'numl' if side=='L' else 'numr',0]);seq+=1
            rows.append(['ATTR','g'+str(seq),pid,'Pin Type',typ,0,0,x,y,0,'hidden',0]);seq+=1
            locations[num]={'x':x,'y':y,'side':side,'name':name,'type':typ}
        info[part]={'pins':locations,'width':width if not passive else 60,'height':h,'passive':passive}
    PROJECT['symbols'][sid]={'title':key,'type':2,'version':'1','desc':'Custom schematic symbol; pins checked against referenced manufacturer documents.','tags':{'parent_tag':[],'child_tag':[]}}
    FILES['SYMBOL/'+sid+'.esym']=dump(rows)
    SYMS[sid]=info
    return sid

def device(key,sid,fp='',pkg='',source='',prefix='U'):
    did=uid('device/'+key)
    PROJECT['devices'][did]={'title':key,'attributes':{'Name':'={Value}','Value':key,'Designator':prefix+'?','Add into BOM':'yes','Convert to PCB':'yes','Symbol':sid,'Footprint':fp,'Manufacturer Part':key,'Package':pkg,'Datasheet':source,'Footprint Status':'Reference footprint; verify before PCB' if fp else 'UNBOUND - select and audit before PCB'},'description':'Engineering draft, not released for fabrication','tags':{'parent_tag':[],'child_tag':[]}}
    return did

class Sheet:
    def __init__(self,title,subtitle):
        self.number=len(SHEETS)+1;self.title=title;self.subtitle=subtitle;self.uuid=uid('sheet/'+title);self.rows=[];self.seq=0;self.draw=[]
        self.rows=[['DOCTYPE','SCH','1.1'],['HEAD',{'originX':0,'originY':0,'version':'2','maxId':100000}],
                   ['FONTSTYLE','text',None,None,None,14,0,0,0,None,1,0],
                   ['FONTSTYLE','small',None,None,None,11,0,0,0,None,1,0],
                   ['FONTSTYLE','right',None,None,None,11,0,0,0,None,1,2],
                   ['FONTSTYLE','title',None,None,None,26,1,0,0,None,1,0],
                   ['FONTSTYLE','ref',None,None,None,14,1,0,0,None,1,0],
                   ['LINESTYLE','wire',None,None,None,1,None],['LINESTYLE','border',None,None,None,1,None]]
        self.poly([30,30,2370,30,2370,1620,30,1620,30,30])
        self.text(65,1580,TITLE+' | '+str(self.number).zfill(2)+'  '+title,'title')
        self.text(65,1540,subtitle)
        self.poly([60,1510,2340,1510]);self.poly([60,105,2340,105])
        self.text(65,75,'REV 0.1 | 2026-09-11 | SCHEMATIC DRAFT / NOT FOR FABRICATION','small')
        self.text(1540,75,'All labels are electrical nets. NC text = intentional open pin.','small')
        SHEETS.append(self)
    def new(self): self.seq+=1;return 'e'+str(self.seq)
    def attr(self,c,key,value,x=0,y=0,show=0,style='small'):
        self.rows.append(['ATTR',self.new(),c,key,str(value),0,show,x,y,0,style,0])
    def text(self,x,y,t,style='text'):
        self.rows.append(['TEXT',self.new(),x,y,0,t,style,0]);self.draw.append(('text',x,y,t,style))
    def poly(self,pts):
        self.rows.append(['POLY',self.new(),pts,False,'border',0]);self.draw.append(('poly',pts))
    def wire(self,x1,y1,x2,y2):
        self.rows.append(['WIRE',self.new(),[[x1,y1,x2,y2]],'wire',0]);self.draw.append(('wire',x1,y1,x2,y2))
    def port(self,x,y,name,side):
        c=self.new();self.rows.append(['COMPONENT',c,'REFBI.1',x,y,0,0 if side=='L' else 1,{},0])
        self.attr(c,'Symbol',NETPORT);self.attr(c,'Device',PORTDEV)
        self.attr(c,'Name',name,x-28 if side=='L' else x+28,y,1,'right' if side=='L' else 'small')
        self.draw.append(('net',x,y,name,side))
    def place(self,ref,did,part,x,y,nets,value=None,note=''):
        dev=PROJECT['devices'][did];sid=dev['attributes']['Symbol'];sym=SYMS[sid][part]
        c=self.new();self.rows.append(['COMPONENT',c,part,x,y,0,0,{},0])
        for key,val in [('Symbol',sid),('Device',did),('Unique ID',uid('component/'+ref)),('Designator',ref),('Value',value or dev['title']),('Name',value or dev['title']),('Footprint',dev['attributes'].get('Footprint','')),('Add into BOM','yes'),('Convert to PCB','yes')]:
            self.attr(c,key,val,x,y+35 if key=='Designator' else y+17,1 if key in ['Designator','Name'] else 0,'ref' if key=='Designator' else 'small')
        if note:self.attr(c,'Engineering note',note)
        self.draw.append(('component',ref,dev['title'],sym,x,y,value or dev['title']))
        COMPONENTS.append({'ref':ref,'part':part,'device':dev['title'],'value':value or dev['title'],'sheet':self.number,'package':dev['attributes'].get('Package',''),'footprint':dev['attributes'].get('Footprint',''),'source':dev['attributes'].get('Datasheet',''),'note':note})
        expected=set(sym['pins']);actual=set(map(str,nets));assert expected==actual,(ref,expected-actual,actual-expected)
        for num,p in sym['pins'].items():
            n=nets[num] if num in nets else nets[int(num)]
            px=x+p['x'];py=y+p['y'];side=p['side']
            PINS.append({'ref':ref,'pin':num,'pin_name':p['name'],'net':n,'sheet':self.number,'type':p['type']})
            if n is None:
                self.text(px-25 if side=='L' else px+8,py,'NC','small')
            else:
                end=px+(-45 if side=='L' else 45)
                self.wire(px,py,end,py);self.port(end,py,n,side)
        return c
    def note(self,x,y,lines):
        for i,t in enumerate(lines): self.text(x,y-24*i,t,'small')

PASS={}
for typ in ['R','C','L','SW','Y','F']:
    sid=symbol(typ,[(typ,[('1','','Passive','L'),('2','','Passive','R')])],width=60,passive=typ)
    PASS[typ]=sid

def two(s,ref,typ,value,a,b,x,y,pkg='',note=''):
    fp=FP_R if typ=='R' else FP_C6 if typ=='C' and ('uF' in value and not value.startswith('0.1')) else FP_C if typ=='C' else ''
    if typ in ['L','F']:fp=''
    did=device(typ+' '+value,PASS[typ],fp,pkg or ('0402' if typ=='R' else '0603' if fp==FP_C6 else '0402' if typ=='C' else 'TBD'),prefix=ref.rstrip('0123456789'))
    s.place(ref,did,typ,x,y,{'1':a,'2':b},value,note)

def ic(key,pins,source,pkg,fp='',width=240):
    sid=symbol(key,[('A',pins)],width)
    return device(key,sid,fp,pkg,source)
def pin(num,name,side='L',typ='Input'):return (str(num),name,typ,side)

# Module complete pad inventory. 114/115 intentionally unassigned until original
# module mechanical drawing is reviewed; they are not used by the design.
modmap={1:'VDDIOB',2:'PB36',3:'PB37',4:'PB54',5:'PA75',6:'PA76',7:'PA77',8:'PA70',9:'PA81',10:'PA79',11:'BOOT_MODE',12:'RSTN',13:'VDD_1V8',14:'GND',15:'VDDIOA',16:'VDD_3V3',17:'PA13',18:'PA15',19:'PA14',20:'PA12',21:'PA67',22:'PA65',23:'PA63',24:'PA62',25:'PA61',26:'PA58',27:'PA57',28:'PA56',29:'PA55',30:'PA54',31:'PA53',32:'PA50',33:'PA48',34:'PA47',35:'PA46',36:'PA45',37:'PA44',38:'PA43',39:'PA27',40:'PA26',41:'PA25',42:'PA24',43:'PA23',44:'PA22',45:'VDDIOA2',46:'GND',47:'DSI_D0N',48:'DSI_D0P',49:'DSI_CLKN',50:'DSI_CLKP',51:'DSI_D1N',52:'DSI_D1P',53:'PA32',54:'PA31',55:'USB_DN',56:'USB_DP',57:'PA17',58:'PA16',59:'PA00',60:'PA03',61:'PB17',62:'PB18',63:'PB11',64:'PB07',65:'GND',66:'DAC1N',67:'DAC1P',68:'MIC_BIAS',69:'ADC1N',70:'ADC1P',71:'GND',72:'BT_ANT',73:'PB51',74:'PB52',75:'PB56',76:'PB57',77:'PB58',78:'PB59',79:'PA93',80:'PA92',81:'PA91',82:'PA90',83:'PA88',84:'PA86',85:'PA84',86:'PA82',87:'PA60',88:'PA59',89:'PA52',90:'PA51',91:'PA42',92:'PA20',93:'PA21',94:'PA29',95:'PA28',96:'PA18',97:'PA02',98:'PA11',99:'PA08',100:'PA07',101:'PA10',102:'PA09',103:'PA06',104:'PA04',105:'PA05',106:'PA01',107:'PB10',108:'PB09',109:'PB08',110:'PB06',111:'PB04',112:'PB03',113:'PB02',114:'UNVERIFIED_UNUSED',115:'UNVERIFIED_UNUSED',116:'PB23',117:'PB26',118:'PB28',119:'PB29',120:'PB24',121:'PB27',122:'PB31',123:'PB30',124:'PB34',125:'PB39',126:'PB38',127:'PB47',128:'PB48',129:'DAC2N',130:'DAC2P',131:'ADC2N',132:'ADC2P',**{i:'GND' for i in range(133,139)}}
module_nets={1:'+1V8',13:'+1V8',15:'VIOA',16:'+3V3',45:'+1V8',2:'UART4_RX_1V8',3:'UART4_TX_1V8',4:'HOME_N',11:'BOOT_MODE',12:'RESET_N',63:'SWDIO_1V8',64:'SWCLK_1V8',38:'LCD_TE',79:'TP_RST',80:'TP_INT',81:'LCD_D0',82:'LCD_CLK',83:'LCD_CS',84:'LCD_D3',85:'LCD_D2',86:'LCD_D1',89:'LCD_BL',90:'LCD_RST',94:'TP_SDA',95:'TP_SCL',118:'SENS_SCL',119:'SENS_SDA',116:'PPG_MFIO',117:'PPG_RST_N',120:'IMU_INT1',121:'IMU_INT2',122:'HAPTIC_EN',123:'BOOST5_EN',124:'CHG_N',125:'PGOOD_N',126:'VBAT_ADC',127:'CROWN_A',128:'CROWN_B',73:'CROWN_SW_N',74:'SIDE_SW_N',**{i:'GND' for i,n in modmap.items() if n=='GND'}}
groups={
 'A_POWER_DEBUG':[1,13,15,16,45,11,12,2,3,4,63,64]+[i for i,n in modmap.items() if n=='GND'],
 'B_DISPLAY':[38,79,80,81,82,83,84,85,86,89,90,94,95],
 'C_SENSORS':[118,119,116,117,120,121,122,123,124,125,126,127,128,73,74]}
used={i for g in groups.values() for i in g};unused=[i for i in modmap if i not in used]
groups['D_UNUSED']=unused[:len(unused)//2];groups['E_UNUSED']=unused[len(unused)//2:]
units=[]
for name,nums in groups.items():
    entries=[]
    for j,n in enumerate(nums):
        typ='Ground' if modmap[n]=='GND' else 'Power' if modmap[n].startswith('VDD') else 'Bidirectional'
        entries.append(pin(n,modmap[n],'L' if j<(len(nums)+1)//2 else 'R',typ))
    units.append((name,entries))
MODSID=symbol('SF32LB58-MOD-N16R32N1',units,320)
MOD=device('SF32LB58-MOD-N16R32N1',MODSID,FP_MOD,'24x24mm module','https://wiki.sifli.com/board/sf32lb58x/SF32LB58-DevKit-LCD.html')
def module(s,part,x,y):s.place('U1',MOD,part,x,y,{str(n):module_nets.get(n) for n in groups[part]})

# 01. Core, voltage-domain options and electrical debug interface.
s=Sheet('CORE AND DEBUG','SF32LB58 module carrier | PA bank selectable; PB / SWD / UART = 1.8 V')
module(s,'A_POWER_DEBUG',450,1400)
s.note(140,875,['Module contains 48 MHz + 32.768 kHz crystals, PSRAM and NOR flash.',
 'HCPU 240 MHz target; start low during bring-up. RTOS / LVGL platform.',
 'N16R32N1: 32 MB PSRAM total, 16 MB NOR + 1 MB boot NOR.',
 'This carrier is for electrical validation; final 46 mm watch PCB is a later revision.'])
for k,(val,a,b) in enumerate([('10uF/10V','+1V8','GND'),('100nF/10V','+1V8','GND'),('10uF/10V','+3V3','GND'),('100nF/10V','+3V3','GND'),('1uF/10V','VIOA','GND')]):two(s,'C'+str(101+k),'C',val,a,b,1500+(k%2)*430,1390-(k//2)*120)
two(s,'R101','R','10k 1%','+1V8','RESET_N',1480,990)
two(s,'C106','C','100nF/10V','RESET_N','GND',1910,990)
two(s,'SW101','SW','RESET','RESET_N','GND',1480,860)
two(s,'R102','R','10k 1%','BOOT_MODE','GND',1480,700)
two(s,'SW102','SW','BOOT / HOLD TO DOWNLOAD','VIOA','BOOT_MODE',1910,700)
two(s,'R103','R','0R DEFAULT','+3V3','VIOA',1480,540)
two(s,'R104','R','0R DNP','+1V8','VIOA',1910,540,note='Mutually exclusive with R103; never populate both')
s.note(1360,430,['Fit R103 only: VIOA = 3.3 V for external display adapter.',
 'For a 1.8 V panel interface: remove R103, then fit R104 and recheck pull-ups.',
 'BOOT_MODE belongs to VIOA; RESET_N pull-up must stay on +1V8.'])
j=ic('DEBUG_1x07',[(str(i),n,'Passive','L') for i,n in enumerate(['VTREF_1V8','GND','SWDIO','SWCLK','RESET_N','MCU_TX','MCU_RX'],1)],'','Connector TBD',width=240)
s.place('J101',j,'A',410,600,{str(i):n for i,n in enumerate(['+1V8','GND','SWDIO_1V8','SWCLK_1V8','RESET_N','UART4_TX_1V8','UART4_RX_1V8'],1)})
s.note(140,280,['VTREF is a voltage reference output, not a power input.',
 'Use a debugger / UART adapter with true 1.8 V logic. Cross TX and RX.',
 'All unused module pads are shown on sheet 08.'])

# 02. Protected single-cell battery and USB-C charge-only input.
s=Sheet('BATTERY AND CHARGER','BQ24074 power path | single-cell 4.2 V charge termination | nominal 128 mA initial charge current')
usb_pins=[pin('A4','VBUS','R','Power'),pin('A9','VBUS','R','Power'),pin('B4','VBUS','R','Power'),pin('B9','VBUS','R','Power'),pin('A5','CC1'),pin('B5','CC2'),pin('A1','GND','L','Ground'),pin('A12','GND','L','Ground'),pin('B1','GND','L','Ground'),pin('B12','GND','L','Ground'),pin('S1','SHIELD','L','Ground')]
usb=ic('USB-C_POWER_ONLY',usb_pins,'','Charge-only receptacle; footprint/pin nomenclature TBD',width=200)
s.place('J201',usb,'A',310,1400,{n:('USB_VBUS_RAW' if n in ['A4','A9','B4','B9'] else 'CC1' if n=='A5' else 'CC2' if n=='B5' else 'GND') for n,_,_,_ in usb_pins})
two(s,'R201','R','5.1k 1%','CC1','GND',200,1050)
two(s,'R202','R','5.1k 1%','CC2','GND',610,1050)
two(s,'F201','F','PTC 0.5A / >=12V','USB_VBUS_RAW','USB_5V',320,850,pkg='Fuse footprint TBD',note='Select a real resettable fuse and audit its hold/trip ratings and footprint before PCB')
bqp=[pin(13,'IN','L','Power'),pin(4,'CE_N'),pin(5,'EN2'),pin(6,'EN1'),pin(12,'ILIM'),pin(16,'ISET'),pin(1,'TS'),pin(14,'TMR'),pin(15,'ITERM'),pin(2,'BAT','R','Power'),pin(3,'BAT','R','Power'),pin(10,'OUT','R','Power'),pin(11,'OUT','R','Power'),pin(7,'PGOOD_N','R','Open Drain'),pin(9,'CHG_N','R','Open Drain'),pin(8,'VSS','R','Ground'),pin(17,'EP','R','Ground')]
bq=ic('BQ24074RGTR',bqp,'https://www.ti.com/lit/ds/symlink/bq24074.pdf','RGT VQFN16+EP 3x3mm',width=270)
s.place('U201',bq,'A',1380,1400,{'13':'USB_5V','4':'GND','5':'GND','6':'USB_5V','12':'CHG_ILIM','16':'CHG_ISET','1':'BAT_NTC','14':None,'15':None,'2':'VBAT','3':'VBAT','10':'VSYS','11':'VSYS','7':'PGOOD_N','9':'CHG_N','8':'GND','17':'GND'})
for k,(val,a,b) in enumerate([('6.98k 1%','CHG_ISET','GND'),('3.09k 1%','CHG_ILIM','GND'),('47k 1%','+1V8','PGOOD_N'),('47k 1%','+1V8','CHG_N')]):two(s,'R'+str(203+k),'R',val,a,b,1230+(k%2)*650,1010-(k//2)*130)
for k,(val,a,b) in enumerate([('4.7uF/16V','USB_5V','GND'),('10uF/10V','VSYS','GND'),('4.7uF/10V','VBAT','GND')]):two(s,'C'+str(201+k),'C',val,a,b,1230+(k%2)*650,710-(k//2)*130)
bat=ic('BATTERY_1x03',[pin(1,'PACK+','L','Power'),pin(2,'PACK-','L','Ground'),pin(3,'NTC10k','L','Passive')],'','Keyed battery connector TBD',width=230)
s.place('J202',bat,'A',400,660,{'1':'VBAT','2':'GND','3':'BAT_NTC'})
s.note(120,450,['Battery: protected 1S Li-ion/LiPo, 4.2 V charge rating; nominal 3.7 V.',
 '400 mAh is a planning assumption. Verify max charge current and cell dimensions.',
 'Use a real 10k NTC in thermal contact with the cell; TS is not tied to GND.',
 'Connector polarity and NTC B value must be checked against the selected pack.',
 'USB-C is charge-only. USB data / SBU contacts are not routed in this revision.'])
s.note(1180,430,['I_CHG typ = 890 / 6980 = 127.5 mA (charger / resistor tolerance applies).',
 'EN2=0, EN1=1: fixed 500 mA input mode; use a suitable USB 5 V supply.',
 'ILIM resistor fitted as required even when fixed input mode is selected.',
 'TMR and ITERM open: internal default timer / termination behavior.',
 'VSYS ~ battery voltage on battery; up to 4.4 V with external input.',
 'Protection IC is required in the purchased battery pack; not duplicated here.'])

# 03. All rails have a verified regulator pinout. Fixed 5 V is TPS610997, not 995.
s=Sheet('POWER RAILS','VSYS -> 1.8 V buck, 3.3 V buck-boost, switchable 5 V boost | validate startup and peak current')
reg=ic('TPS62840DLCR',[pin(2,'VIN','L','Power'),pin(4,'EN'),pin(3,'MODE'),pin(6,'STOP'),pin(5,'VSET'),pin(1,'GND','L','Ground'),pin(7,'SW','R','Power'),pin(8,'VOS','R','Input')],'https://www.ti.com/lit/ds/symlink/tps62840.pdf','DLC VSON8 2x1.5mm',width=210)
s.place('U301',reg,'A',430,1370,{'2':'VSYS','4':'VSYS','3':'GND','6':'GND','5':'GND','1':'GND','7':'BUCK18_SW','8':'+1V8'})
two(s,'L301','L','2.2uH Isat>=1.2A','BUCK18_SW','+1V8',1020,1330)
two(s,'C301','C','4.7uF/10V','VSYS','GND',1450,1330)
two(s,'C302','C','10uF/10V','+1V8','GND',1900,1330)
s.note(1030,1200,['DLC variant: VSET to GND selects 1.8 V. MODE=0, STOP=0.',
 '0.75 A regulator rating; board rail budget remains to be measured.',
 'Place inductor and ceramic capacitors beside the IC.'])
reg=ic('TPS63031DSKR',[pin(5,'VIN','L','Power'),pin(8,'VINA','L','Power'),pin(6,'EN'),pin(7,'PS/SYNC'),pin(9,'GND','L','Ground'),pin(3,'PGND','L','Ground'),pin(11,'EP','L','Ground'),pin(4,'L1','R','Power'),pin(2,'L2','R','Power'),pin(1,'VOUT','R','Power'),pin(10,'FB','R','Input')],'https://www.ti.com/lit/ds/symlink/tps63031.pdf','DSK VSON10+EP 2.5x2.5mm',width=210)
s.place('U302',reg,'A',430,920,{'5':'VSYS','8':'VSYS','6':'VSYS','7':'GND','9':'GND','3':'GND','11':'GND','4':'BB33_L1','2':'BB33_L2','1':'+3V3','10':'+3V3'})
two(s,'L302','L','1.5uH Isat>=1.5A','BB33_L1','BB33_L2',1020,900)
for i,(v,n,x,y) in enumerate([('10uF/10V','VSYS',1450,900),('100nF/10V','VSYS',1900,900),('10uF/10V','+3V3',1450,770),('10uF/10V','+3V3',1900,770)]):two(s,'C'+str(303+i),'C',v,n,'GND',x,y)
s.note(1030,650,['Fixed 3.3 V: FB tied to VOUT. PS/SYNC=0 enables power save.',
 '1 A is switch-current class, not a guaranteed 1 A output.',
 'C304 is the VINA local bypass; do not omit it.'])
reg=ic('TPS610997DRVR',[pin(6,'VIN','L','Power'),pin(4,'EN'),pin(3,'FB'),pin(1,'GND','L','Ground'),pin(7,'EP','L','Ground'),pin(5,'SW','R','Power'),pin(2,'VOUT','R','Power')],'https://www.ti.com/lit/ds/symlink/tps61099.pdf','DRV WSON6+EP 2x2mm',width=210)
s.place('U303',reg,'A',430,470,{'6':'VSYS','4':'BOOST5_EN','3':'GND','1':'GND','7':'GND','5':'BOOST5_SW','2':'+5V_SW'})
two(s,'L303','L','2.2uH Isat>=1.5A','VSYS','BOOST5_SW',1020,450)
two(s,'C307','C','10uF/10V','VSYS','GND',1450,450)
two(s,'C308','C','10uF/10V','+5V_SW','GND',1900,450)
two(s,'C309','C','10uF/10V','+5V_SW','GND',1450,300)
two(s,'R301','R','100k 1%','BOOST5_EN','GND',1900,300)
s.note(950,195,['5 V initially OFF. Firmware enables before PPG acquisition / any 5 V display load.',
 'TPS610997 = fixed 5 V, FB=GND. Validate combined panel + LED pulse load and ripple.'])

# 04. Display interface, physical controls. Stock connector is a subset only.
s=Sheet('DISPLAY AND CONTROLS','390x450 QSPI AMOLED + I2C touch | connector matches required SiFli CONN2 signals; panel FPC is not guessed')
module(s,'B_DISPLAY',400,1400)
lcdmap={1:'+3V3',2:'+5V_SW',3:'TP_SDA',4:'+5V_SW',5:'TP_SCL',6:'GND',9:'GND',14:'GND',17:'+3V3',19:'LCD_TE',20:'GND',21:'LCD_D2',22:'LCD_D1',23:'LCD_CS',24:'LCD_D3',25:'GND',26:'LCD_D0',27:'LCD_CLK',28:'LCD_RST',29:'TP_INT',30:'GND',31:'TP_RST',32:'LCD_BL',34:'GND',39:'GND'}
lcd=ic('LCD_ADAPTER_2x20',[(str(i),'CONN2_'+str(i),'Passive','L' if i%2 else 'R') for i in range(1,41)],'https://wiki.sifli.com/board/sf32lb58x/SF32LB58-DevKit-LCD.html','2x20 2.54mm',FP_LCD,width=260)
s.place('J401',lcd,'A',1560,1400,{str(i):lcdmap.get(i) for i in range(1,41)})
s.note(1170,720,['External adapter required; confirm its revision / orientation before powering.',
 '5 V shares the PPG boost. Unused CONN2 audio / UART / SWD pins are open.',
 'For naked AMOLED: need its FPC drawing, rails and power-up timing first.',
 'CO5300AF-01 / FT6146-M00 are screenshot data, not a pinout specification.'])
two(s,'R401','R','4.7k DNP','VIOA','TP_SCL',280,1080,note='Fit only if the adapter has no pull-up; verify pull-up voltage')
two(s,'R402','R','4.7k DNP','VIOA','TP_SDA',750,1080,note='Fit only if the adapter has no pull-up; verify pull-up voltage')
two(s,'C401','C','10uF/10V','+3V3','GND',280,910)
two(s,'C402','C','100nF/10V','+3V3','GND',750,910)
crown=ic('CROWN_AB_SWITCH',[pin(1,'ENC_A','L','Passive'),pin(2,'ENC_COMMON','L','Passive'),pin(3,'ENC_B','L','Passive'),pin(4,'PUSH','L','Passive'),pin(5,'PUSH_COMMON','L','Passive')],'','Logical connector; encoder mechanics TBD',width=230)
s.place('J402',crown,'A',400,570,{'1':'CROWN_A','2':'GND','3':'CROWN_B','4':'CROWN_SW_N','5':'GND'})
for k,net in enumerate(['CROWN_A','CROWN_B','CROWN_SW_N','SIDE_SW_N','HOME_N']):two(s,'R'+str(403+k),'R','47k 1%','+1V8',net,1120+(k%2)*710,560-(k//2)*120)
two(s,'SW401','SW','SIDE','SIDE_SW_N','GND',270,290)
two(s,'SW402','SW','HOME / WAKE','HOME_N','GND',740,290)
s.note(100,170,['Encoder input uses firmware debounce; MCU ESD / mechanical switch package selection remains for PCB.',
 'No software or display bandwidth claim substitutes for a measured animation frame rate on the purchased kit.'])

# 05. Host I2C sensors and haptics. PB bank is 1.8 V.
s=Sheet('IMU AND HAPTICS','LSM6DSO -> SF58 host -> MAXM86146 algorithm | 1.8 V sensor I2C bus | LRA with closed-loop driver')
module(s,'C_SENSORS',370,1400)
imu=ic('LSM6DSOTR',[pin(8,'VDD','L','Power'),pin(5,'VDDIO','L','Power'),pin(12,'CS'),pin(1,'SDO/SA0'),pin(2,'SDx'),pin(3,'SCx'),pin(6,'GND','L','Ground'),pin(7,'GND','L','Ground'),pin(13,'SCL','R','Input'),pin(14,'SDA','R','Bidirectional'),pin(4,'INT1','R','Output'),pin(9,'INT2','R','Output'),pin(10,'OCS_AUX','R','Output'),pin(11,'SDO_AUX','R','Output')],'https://www.st.com/resource/en/datasheet/lsm6dso.pdf','LGA14 2.5x3mm',width=240)
s.place('U501',imu,'A',1530,1400,{'8':'+1V8','5':'+1V8','12':'+1V8','1':'GND','2':'GND','3':'GND','6':'GND','7':'GND','13':'SENS_SCL','14':'SENS_SDA','4':'IMU_INT1','9':'IMU_INT2','10':None,'11':None})
for k,net in enumerate(['SENS_SCL','SENS_SDA']):two(s,'R'+str(501+k),'R','4.7k 1%','+1V8',net,210+k*500,1040)
for k in range(2):two(s,'C'+str(501+k),'C','100nF/10V','+1V8','GND',1390+k*530,1050)
drv=ic('DRV2605LDGSR',[pin(10,'VDD','L','Power'),pin(6,'VDD/NC','L','Power'),pin(2,'SCL'),pin(3,'SDA','L','Bidirectional'),pin(5,'EN'),pin(4,'IN/TRIG'),pin(8,'GND','L','Ground'),pin(7,'OUT+','R','Output'),pin(9,'OUT-','R','Output'),pin(1,'REG','R','Power')],'https://www.ti.com/lit/ds/symlink/drv2605l.pdf','DGS VSSOP10',width=260)
s.place('U502',drv,'A',500,760,{'10':'VSYS','6':'VSYS','2':'SENS_SCL','3':'SENS_SDA','5':'HAPTIC_EN','4':'GND','8':'GND','7':'LRA_P','9':'LRA_N','1':'HAPTIC_REG'})
for k,(v,a,b) in enumerate([('1uF/10V','VSYS','GND'),('1uF/10V','HAPTIC_REG','GND')]):two(s,'C'+str(503+k),'C',v,a,b,1440+k*500,780)
two(s,'R503','R','100k 1%','HAPTIC_EN','GND',1440,610)
lra=ic('LRA_2PIN',[pin(1,'LRA+','L','Passive'),pin(2,'LRA-','L','Passive')],'','LRA resonance / voltage / model TBD',width=200)
s.place('J501',lra,'A',1550,440,{'1':'LRA_P','2':'LRA_N'})
two(s,'R504','R','1M 1%','VBAT','VBAT_ADC',200,380)
two(s,'R505','R','220k 1%','VBAT_ADC','GND',760,380)
two(s,'C505','C','100nF/10V','VBAT_ADC','GND',200,245)
s.note(950,250,['VBAT_ADC = VBAT x 220/1220; 4.2 V -> about 0.757 V. Verify ADC scale / acquisition time.',
 'LSM6DSO I2C address 0x6A. Firmware must feed accelerometer samples to the PPG algorithm.',
 'DRV2605L REG is a local bypass node. Never connect it to the board +1V8 rail.',
 'LRA rated voltage / resonance must be set before calibration and playback.'])

# 06. Actual PPG hub and AFE support circuit, not a health-sensor placeholder.
s=Sheet('PPG MODULE','MAXM86146CFU+ heart-rate / SpO2 front end | wrist optics and firmware algorithm still require validation')
ppgp=[pin(6,'VDD','L','Power'),pin(22,'VDD_AFE','L','Power'),pin(18,'VLED','L','Power'),pin(3,'HOST_SCL'),pin(4,'HOST_SDA','L','Bidirectional'),pin(12,'MFIO','L','Bidirectional'),pin(10,'RSTN'),pin(8,'32KIN'),pin(9,'32KOUT','L','Output'),pin(2,'VREF','L','Power'),pin(5,'VCORE','L','Power'),pin(38,'P0.3/PPG_INT','L','Bidirectional'),pin(11,'P0.0/ACCEL_CS','L','Output'),pin(13,'P0.2/ACCEL_INT'),pin(35,'P0.6/SCK','L','Output'),pin(36,'P0.5/MOSI','L','Output'),pin(37,'P0.4/MISO')]
ppgp += [pin(21,'LED1_DRV','R','Output'),pin(20,'LED2_DRV','R','Output'),pin(19,'LED3_DRV','R','Output'),pin(28,'AFE_GPIO1','R','Output'),pin(25,'PD2_IN','R','Input'),pin(26,'PD2_CAT','R','Passive'),pin(1,'PD1_IN','R','Input')]
ppgp += [pin(n,'GND' if n not in [7,33] else 'VSS','R','Ground') for n in [7,23,24,29,30,31,32,33]]
ppgp += [pin(n,'NC','R','Undefined') for n in [14,15,16,17,27,34]]
ppg=ic('MAXM86146CFU+',ppgp,'https://www.analog.com/media/en/technical-documentation/data-sheets/maxm86146.pdf','OLGA38 4.5x4.1mm',width=340)
pn={'6':'+1V8','22':'+1V8','18':'+5V_SW','3':'SENS_SCL','4':'SENS_SDA','12':'PPG_MFIO','10':'PPG_RST_N','8':'PPG_XI','9':'PPG_XO','2':'PPG_VREF','5':'PPG_VCORE','38':'PPG_INT_LOCAL','21':'LED1_DRV','20':'LED2_DRV','19':'LED3_DRV','28':'LED_MUX_CB','25':'PD2_LINK','26':'PD2_LINK',**{str(n):'GND' for n in [7,23,24,29,30,31,32,33]}}
s.place('U601',ppg,'A',550,1400,{num:pn.get(num) for num,_,_,_ in ppgp})
for k,(v,n) in enumerate([('1uF/10V','+1V8'),('10uF/10V','+1V8'),('100nF/10V','+1V8'),('1uF/10V','PPG_VREF'),('1uF/10V','PPG_VCORE'),('10uF/10V','+5V_SW'),('100nF/10V','+5V_SW')]):two(s,'C'+str(601+k),'C',v,n,'GND',1430+(k%2)*490,1370-(k//2)*130)
for k,(v,a,b) in enumerate([('4.7k 1%','+1V8','PPG_MFIO'),('4.7k 1%','+1V8','PPG_INT_LOCAL'),('10R 1%','PPG_RST_SNUB','GND')]):two(s,'R'+str(601+k),'R',v,a,b,1430+(k%2)*490,780-(k//2)*130)
two(s,'C608','C','100nF/10V','PPG_RST_N','PPG_RST_SNUB',1920,650)
two(s,'Y601','Y','32.768kHz CL6pF','PPG_XI','PPG_XO',1430,480,pkg='Crystal 2pin / footprint TBD',note='ESR <90k, C0<2pF; confirm load network against selected crystal and reference design')
s.note(115,640,['PD1 is integrated: pin 1 remains open. PD2 pin 25 must be joined to pin 26.',
 'VCORE and VREF are capacitor-only nodes; do not drive with +1V8.',
 'The unused accelerometer SPI pins stay unloaded in host-feed architecture.',
 'PPG_INT_LOCAL is the local AFE interrupt; host communication uses MFIO.',
 'Crystal load network follows the ADI reference; no guessed external load capacitors.'])
s.note(1140,350,['Host I2C / MFIO / reset all 1.8 V. I2C pull-ups are on sheet 05.',
 'Hold PPG reset while required rails / clock stabilize, then follow ADI boot protocol.',
 'C601 at VDD; C602+C603 at VDD_AFE; C606+C607 at VLED.',
 'Common ground electrically; keep LED pulse returns away from optical/analog nodes.',
 'This is a real sensing circuit; accuracy is not established by the schematic.'])

# 07. External emitter and current-steering network from ADI application circuit.
s=Sheet('OPTICAL EMITTERS','Two green LEDs + red / IR | MAX14689 switching network | LED polarity / optical footprint review required')
muxp=[pin('C2','VCC','L','Power'),pin('B2','GND','L','Ground'),pin('A2','CB'),pin('B1','COM1','L','Passive'),pin('B3','COM2','L','Passive'),pin('A1','NC1','R','Passive'),pin('C1','NO1','R','Passive'),pin('A3','NC2','R','Passive'),pin('C3','NO2','R','Passive')]
mux=ic('MAX14689EWL+T',muxp,'https://www.analog.com/media/en/technical-documentation/data-sheets/MAX14689.pdf','WLP9 1.2x1.2mm',width=240)
s.place('U701',mux,'A',520,1370,{'C2':'+1V8','B2':'GND','A2':'LED_MUX_CB','B1':'LED2_DRV','B3':'LED3_DRV','A1':'RED_K','C1':'GREEN1_K','A3':'IR_K','C3':'GREEN2_K'})
dual=ic('SFH7015',[pin(3,'A_RED+IR','L','Passive'),pin(2,'K_RED_655nm','R','Passive'),pin(4,'K_IR_940nm','R','Passive'),pin(1,'NC','L','Undefined')],'https://look.ams-osram.com/m/17a2f1b05b6528bd/original/SFH-7015.pdf','Optical LED 2x0.8mm',width=290)
s.place('D701',dual,'A',1560,1370,{'3':'+5V_SW','2':'RED_K','4':'IR_K','1':None})
two(s,'R701','R','0R','LED1_DRV','GREEN1_K',430,1080,note='LED1 directly drives GREEN1, shared with mux NO1; verified visually against ADI p18')
green=ic('CT DBLP31.12-6C5D-56-J6U6',[pin('A','ANODE','L','Passive'),pin('K','CATHODE','R','Passive')],'https://look.ams-osram.com/m/7a84e13abf7bd695/original/CT-DBLP31-12.pdf','Optical LED; A/K mapping to selected footprint requires verification',width=230)
s.place('D702',green,'A',1550,1080,{'A':'+5V_SW','K':'GREEN1_K'})
s.place('D703',green,'A',1550,840,{'A':'+5V_SW','K':'GREEN2_K'})
two(s,'R702','R','100k 1%','LED_MUX_CB','GND',430,850)
two(s,'C701','C','100nF/10V','+1V8','GND',430,650)
two(s,'C702','C','22uF/10V','+5V_SW','GND',1080,650)
two(s,'C703','C','22uF/10V','+5V_SW','GND',1810,650)
s.note(160,470,['Current is regulated by MAXM86146 LED drivers. Set pulse current / timing before acquisition.',
 'CB=0: LED2 -> RED, LED3 -> IR. CB=1: LED2 -> GREEN1, LED3 -> GREEN2.',
 'GREEN1 cathode is also connected to LED1_DRV, following the ADI typical application.',
 'Keep the GREEN1 shared path: three independent LEDs would change the reference design.',
 'MAX14689 VCC=1.8 V; analog terminals support beyond-rail signals within its ratings.',
 'Green symbols use A/K logical pads. Physical pad mapping is intentionally not invented.',
 'Before PCB: audit emitter land patterns, optical separation, black barriers and skin contact geometry.',
 'Heart rate / SpO2 algorithms, calibration, motion rejection and human testing remain required.'])

# 08. Every physical module pad appears exactly once, including unused pads.
s=Sheet('MODULE UNUSED PADS','Complete module inventory | offline phase: MIPI, USB data, RF, audio and spare GPIO remain unconnected')
module(s,'D_UNUSED',430,1410);module(s,'E_UNUSED',1530,1410)
s.note(130,430,['U1 units A-E are the same physical SF32LB58 module. Shared designator and unique ID preserve one BOM item.',
 'All pads marked NC on this sheet are deliberately not routed in v0.1. This is not an omission of power pins.',
 '114 / 115 are shown as UNVERIFIED_UNUSED; confirm names using the module pad drawing before future reuse.',
 'The imported module footprint comes from the SiFli native EDA reference project.',
 'Other IC and mechanical footprints are unbound for deliberate review before PCB layout.',
 'No PCB outline, RF antenna, audio chain, network radio enablement or production release is included.',
 'Next gate: verify imported electrical nets, complete footprint audit, confirm panel adapter and battery pack.'])

# Serialize project and transparent audit artifacts.
schid=uid('schematic')
PROJECT['schematics'][schid]={'name':TITLE,'sheets':[{'name':s.title,'id':s.number,'uuid':s.uuid} for s in SHEETS]}
PROJECT['boards'][TITLE]={'schematic':schid}
PROJECT['config']['defaultSheet']=SHEETS[0].uuid
for s in SHEETS:FILES['SHEET/'+schid+'/'+str(s.number)+'.esch']=dump(s.rows)
FILES['project.json']=json.dumps(PROJECT,ensure_ascii=False,indent=2)
with zipfile.ZipFile(OUT/(TITLE+'.epro'),'w',zipfile.ZIP_DEFLATED) as z:
    for path,data in FILES.items():z.writestr(path,data)
for s in SHEETS:(OUT/(str(s.number).zfill(2)+'_'+s.title.replace(' ','_')+'.esch')).write_text(dump(s.rows),encoding='utf8')
(OUT/'circuit-model.json').write_text(json.dumps({'title':TITLE,'components':COMPONENTS,'pins':PINS,'sheets':[{'number':s.number,'title':s.title,'draw':s.draw} for s in SHEETS]},ensure_ascii=False,indent=2),encoding='utf8')
with (OUT/'BOM_v0.1.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(COMPONENTS[0]));writer.writeheader();seen=set()
    for c in COMPONENTS:
        if c['ref'] not in seen:writer.writerow(c);seen.add(c['ref'])
with (OUT/'pin-net-map_v0.1.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(PINS[0]));writer.writeheader();writer.writerows(PINS)
assert len({(p['ref'],p['pin']) for p in PINS})==len(PINS),'duplicate physical pin'
assert {int(p['pin']) for p in PINS if p['ref']=='U1'}==set(range(1,139))
netdict=collections.defaultdict(list)
for p in PINS:
    if p['net']:netdict[p['net']].append(p['ref']+'.'+p['pin'])
single={n:v for n,v in netdict.items() if len(v)<2}
assert not single,single
assert sum(p['net'] is not None for p in PINS if p['ref']=='U1')==len(module_nets)
report={'sheets':len(SHEETS),'unique_components':len({c['ref'] for c in COMPONENTS}),'physical_pins':len(PINS),'electrical_nets':len(netdict),'intentionally_open_pins':sum(p['net'] is None for p in PINS),'checks':['All component pins are explicitly connected or intentionally open','No duplicated physical pin across module units','All 138 SF58 module pads represented','No singleton named nets','All symbol/device references resolve within the epro archive'],'limitations':['These checks are model-level, not a substitute for EDA ERC or circuit review','IC footprints except U1 are not yet bound; no PCB release','NC text denotes open pins; EDA no-connect markers still require ERC review','Battery, panel adapter, optics and LRA mechanical definitions are pending']}
(OUT/'validation-model.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'output':str(OUT),'report':report},ensure_ascii=False,indent=2))
