"""Author a native EasyEDA Pro schematic from an explicit pin/net model.

Draft schematic only. IC footprints deliberately remain unbound unless sourced
from the official SiFli project; no guessed packages or panel FPC pin mapping.
"""
from pathlib import Path
import json, uuid, zipfile, csv, collections, math

BASE = Path(__file__).resolve().parent
OUT = BASE.parents[1] / 'hardware' / '原理图_v0.2_裸芯片'
OUT.mkdir(parents=True, exist_ok=True)
REF = zipfile.ZipFile(BASE.parent / 'schematic-v0.1/references/SF58-official.epro')
ORIG = json.loads(REF.read('project.json'))
TITLE = 'HealthWatch-SF58-BareChip-v0.2'
def uid(key): return uuid.uuid5(uuid.NAMESPACE_URL, 'healthwatch-sf58-v02/'+key).hex
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
        self.text(65,75,'REV 0.2 | 2026-09-11 | SCHEMATIC DRAFT / NOT FOR FABRICATION','small')
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


# Exact BGA ball inventory from SiFli original schematic; cross-checked with DS5801.
ballmap={'N13': 'SD1_D7/CAN1_TX/I2C1_SCL/UART3_RX/PA0', 'R13': 'SD1_D2/MPI4_D2/PA1', 'L12': 'SD1_CLKIN/CAN2_RX/UART3_RX/PA2', 'M12': 'SD1_D5/CAN1_RX/I2C1_SDA/UART3_TX/PA3', 'R12': 'SD1_D1/MPI4_D1/PA4', 'T12': 'SD1_D0/MPI4_D0/PA5', 'R11': 'SD1_D3/MPI4_D3/PA6', 'T11': 'SD1_D4/SCI_CLK/UART2_TX/PA7', 'N12': 'SD1_D6/SCI_DIO/UART2_RX/PA8', 'L11': 'SD1_CLK/MPI4_CLK/PA9', 'M11': 'SD1_CMD/MPI4_CS/PA10', 'L10': 'SCI_RST/CAN2_TX/I2C1_SDA/UART3_TX/PA11', 'M10': 'PA12/GPTIM1_CH1', 'N10': 'PA13/GPTIM1_CH2', 'N9': 'PA14/I2S1_LRCK', 'R10': 'PA15/GPTIM1_CH3', 'T9': 'PA16/I2C1_SDA/UART2_TX', 'R9': 'PA17/I2C1_SCL/UART2_RX', 'P9': 'PA18/PDM1_D/I2S1_DI/UART2_RTS/I2C2_SCL', 'M9': 'PA19/SC1_RST/GPTIM1_CH4', 'L9': 'PA20/UART3_RX/SPI1_DI/UART2_CTS', 'L8': 'PA21/UART3_TX/SPI1_DO/UART2_RTS', 'N8': 'PA22/PDM2_D/GPTIM1_ETR', 'T8': 'PA23/PDM1_C/I2S1_BCK/UART2_CTS/I2C2_SDA', 'R8': 'PA24/LCDC1_SPI_RSTB', 'M8': 'PA25/PDM2_C/SD1_CLKIN/GPTIM2_CH1', 'M7': 'PA26/SCI_CLK/GPTIM2_CH2', 'L7': 'PA27/SCI_DIO/GPTIM2_CH3', 'M6': 'PA28/UART2_TX/SPI1_CLK/I2C2_SCL', 'N6': 'PA29/UART2_RX/SPI1_CS/I2C2_SDA', 'M5': 'PA30/SPI2_CS/SD1_D1/MPI4_CS', 'N5': 'PA31/UART1_TX/I2C3_SCL', 'N4': 'PA32/UART1_RX/I2C3_SDA', 'N3': 'PA33/SD1_D7/UART2_CTS', 'L6': 'PA34/SD1_CMD/UART2_RTS', 'M4': 'PA35/SD1_D6/UART3_CTS', 'N1': 'PA36/SD1_D2/MPI4_D2', 'L5': 'PA37/SPI2_DO/SD1_D5/MPI4_D1/SPI2_DIO', 'K5': 'PA38/SD1_D4/MPI4_D3', 'M1': 'PA39/SPI2_CLK/SD1_CLK/MPI4_CLK', 'K4': 'PA40/SPI2_DI/SD1_D3/MPI4_D0', 'L2': 'PA41/SD1_D0/UART3_RTS', 'K6': 'PA42/GPTIM2_CH4', 'J6': 'PA43/LCDC1_SPI_TE', 'K3': 'PA44/MPI3_CS/LCDC1_SPI_CS', 'K2': 'PA45/MPI3_D3/LCDC1_SPI_D3', 'K1': 'PA46/MPI3_CLK/LCDC1_SPI_CLK', 'J5': 'PA47/MPI3_D2/LCDC1_SPI_D2', 'J4': 'PA48/MPI3_D1/LCDC1_SPI_D1', 'J2': 'PA49/GPTIM1_CH1', 'J1': 'PA50/MPI3_D0/LCDC1_SPI_D0', 'G6': 'SWCLK/GPTIM1_CH2/PA51', 'F6': 'SWDIO/GPTIM1_CH3/PA52', 'G5': 'PA53', 'G4': 'PA54/SPI1_DO/SPI1_DIO/UART2_RTS', 'H2': 'PA55/GPTIM1_CH4', 'G2': 'PA56/SPI1_CLK/UART2_CTS', 'F4': 'PA57/SPI1_DI/UART2_RXD', 'E5': 'PA58/LPTIM1_ETR/GPTIM2_CH1', 'E4': 'PA59/I2C4_SDA/UART1_TX/UART3_RTS', 'E3': 'PA60/I2C4_SCL/UART1_RX/UART3_CTS', 'F1': 'PA61/SPI1_CS/UART2_TX', 'F2': 'PA62/I2C2_SCL/UART1_RTS', 'E2': 'PA63/I2C2_SDA/UART1_CTS', 'D5': 'PA64/WKUP_6', 'D6': 'PA65/WKUP_7/GPTIM2_CH2', 'C7': 'PA66/WKUP_8', 'D7': 'PA67/WKUP_9/GPTIM2_CH3', 'E7': 'PA68/WKUP_10', 'E8': 'PA69/WKUP_11', 'F9': 'PA70/SD2_CMD/SPI2_CS', 'A8': 'PA71/BT_ACTIVE', 'B8': 'PA72/BT_PRIORITY', 'B9': 'PA73/BT_COLLISION/UART1_RTS/GPTIM2_CH4', 'A9': 'PA74/WLAN_ACTIVE/UART1_CTS/LCDC1_SPI_RST', 'D9': 'PA75/SD2_D1/SPI2_DI', 'E9': 'PA76/SD2_D0/SPI2_DO', 'F10': 'PA77/SD2_CLK/SPI2_CLK', 'E10': 'PA78/SPI2_DI/SD2_CLKIN/GPTIM2_ETR', 'D10': 'PA79/SD2_D2', 'B10': 'PA80/LCDC1_SPI_TE', 'C10': 'PA81/SD2_D3', 'F12': 'PA82/I2S2_DO/GPTIM1_CH1/LCDC1_SPI_D1', 'E11': 'PA83/SPI2_CLK', 'E12': 'PA84/I2S2_LRCK/GPTIM1_CH2/LCDC1_SPI_D2', 'B11': 'PA85', 'F13': 'PA86/I2S2_DI/GPTIM1_CH3/LCDC1_SPI_D3', 'A11': 'PA87/SPI2_CS', 'G14': 'PA88/LCDC1_SPI_CS', 'C11': 'PA89/SPI2_DO/SPI2_DIO', 'E13': 'PA90/I2S2_MCLK/GPTIM1_CH4/LCDC1_SPI_CLK', 'D12': 'PA91/I2S2_BCK/GPTIM1_ETR/LCDC1_SPI_D0', 'F15': 'PA92/I2C3_SCL/UART3_RX/UART3_CTS', 'E14': 'PA93/I2C3_SDA/UART3_TX/UART3_RTS', 'M13': 'I2C7_SDA/UART6_RX/GPTIM3_CH1/PB0', 'L13': 'I2C7_SCL/UART6_TX/GPTIM3_CH2/PB1', 'M14': 'LCDC2_SPI_TE/GPTIM3_CH3/PB2', 'R14': 'LCDC2_SPI_D1/GPTIM3_CH4/UART4_RTS/PB3', 'T14': 'LCDC2_SPI_D2/GPTIM4_CH1/UART4_CTS/PB4', 'R15': 'LCDC2_SPI_RST/GPTIM4_CH2/UART6_RTS/PB5', 'T15': 'LCDC2_SPI_D3/GPTIM4_CH3/UART6_CTS/PB6', 'L15': 'UART4_RX/GPTIM4_CH4/PB7/SWCLK', 'M15': 'LCDC2_SPI_CS/GPTIM5_CH1/BT_ACTIVE/PB8', 'P15': 'LCDC2_SPI_D0/UART6_TX/GPTIM5_CH2/PB9', 'N15': 'LCDC2_SPI_CLK/UART6_RX/GPTIM5_CH3/PB10', 'M16': 'UART4_TX/GPTIM5_CH4/PB11/SWDIO', 'N16': 'SPI4_CLK/I2C7_SCL/PB12', 'L16': 'UART6_RX/UART4_CTS/GPTIM3_CH1/PB13', 'K16': 'UART6_TX/UART4_RTS/GPTIM3_CH2/PB14', 'P17': 'SPI4_DI/UART6_CTS/PB15', 'R16': 'SPI4_DO/I2C7_SDA/SPI4_DIO/PB16', 'T17': 'UART5_RX/SPI3_CLK/PB17', 'R17': 'UART5_TX/SPI3_DI/PB18', 'N17': 'SPI4_CS/UART6_RTS/PB19', 'J16': 'GPTIM3_CH3/PB20', 'N18': 'SPI3_CLK/GPTIM3_CH4/PB21', 'P18': 'SPI3_DO/SPI3_DIO/GPTIM4_CH1/PB22', 'R18': 'UART5_CTS/SPI3_DO/GPTIM4_CH2/PB23', 'P19': 'I2S3_DO/GPTIM4_CH3/PB24', 'T19': 'I2S3_DI/GPTIM4_CH4/PB25', 'R19': 'UART5_RTS/SPI3_CS/GPTIM5_CH1/PB26', 'R20': 'SPI4_CLK/I2S3_DI/GPTIM5_CH2/PB27', 'R21': 'I2C6_SCL/UART6_RX/PB28', 'T20': 'I2C6_SDA/UART6_TX/PB29', 'P20': 'SPI4_DO/I2S3_BCK/GPTIM5_CH3/PB30', 'P21': 'SPI4_DI/I2S3_LRCK/AU_CKO/GPTIM5_CH4/PB31', 'N19': 'GPADC_CH0/PB32', 'H16': 'LPTIM3_ETR/GPADC_CH1/PB33', 'N20': 'SPI4_CS/I2S3_MCLK/GPADC_CH2/PB34', 'L17': 'GPADC_CH3/PB35', 'G16': 'UART4_RX/GPTIM3_CH1/GPADC_CH4/PB36', 'F16': 'UART4_TX/GPTIM3_CH2/GPADC_CH5/PB37', 'K17': 'UART6_CTS/GPTIM3_CH3/GPADC_CH6/PB38', 'M18': 'UART6_RTS/GPTIM3_CH4/GPADC_CH7/PB39', 'L18': 'SPI3_CS/GPTIM3_ETR/PB40', 'H17': 'WLAN_ACTIVE/I2S3_DO/GPTIM4_CH1/PB41', 'M20': 'I2S3_DI/GPTIM4_CH2/PB42', 'M21': 'UART4&5_CTS/I2S3_BCK/GPTIM4_CH3/PB43', 'A19': 'UART4&5_RTS/I2S3_LRCK/GPTIM4_CH4/PB44', 'A20': 'BT_ACTIVE/GPTIM4_ETR/PB45', 'B19': 'AU_CKO/I2S3_MCLK/GPTIM5_CH1/PB46', 'B16': 'I2C5_SDA/UART6_RX/GPTIM5_CH2/PB47', 'C17': 'I2C5_SCL/UART6_TX/GPTIM5_CH3/PB48', 'C19': 'UART6_CTS/UART6_RX/GPTIM5_CH4/PB49', 'D19': 'UART6_RTS/UART6_TX/GPTIM5_ETR/PB50', 'D18': 'TWI_DAT/UART4&5_CTS/PB51', 'D17': 'TWI_CLK/UART4&5_RTS/PB52', 'B15': 'PB53', 'C14': 'WKUP_0/LONGTIME_PRESS_RST/PB54', 'C15': 'WKUP_1/PB55', 'D16': 'WKUP_2/UART4_CTS/SPI3_CLK/PB56', 'D14': 'WKUP_3/UART4_RTS/SPI3_DO/PB57', 'D15': 'WKUP_4/UART6_RX/SPI3_DI/PB58', 'E15': 'WKUP_5/UART6_TX/SPI3_CS/PB59', 'A15': 'PBR0', 'B14': 'PBR1', 'A14': 'PBR2', 'B13': 'PBR3', 'B12': 'PBR4', 'A12': 'PBR5', 'B2': 'PVDD_PMU1', 'B3': 'PVDD_PMU2', 'C2': 'PVSS_PMU1', 'C3': 'PVSS_PMU2', 'C1': 'PMU_BUCK1_VSW', 'D1': 'PMU_BUCK1_VOUT', 'B1': 'PMU_BUCK2_VOUT', 'A2': 'PMU_BUCK2_VSW', 'A3': 'VSS', 'A4': 'VSS', 'B4': 'VDD_EXT1', 'B5': 'VDD_EXT2', 'D2': 'HPSYS_LDO_VOUT', 'C4': 'LPMU_VDD07_RET', 'C5': 'LPMU_VDD11_RTC', 'B7': 'XTAL32K_XI', 'A6': 'XTAL32K_XO', 'A17': 'XTAL48M_XI', 'A18': 'XTAL48M_XO', 'C21': 'BRF_ANT', 'E20': 'AVDD_BRF', 'B20': 'AVSS_RRF', 'B21': 'AVSS_RRF', 'D20': 'AVSS_TRF', 'D21': 'AVSS_TRF', 'C20': 'AVSS_VCO', 'E21': 'AVSS_TRF2', 'K18': 'AVDD33_ANA', 'J18': 'AVDD33_AUD', 'P5': 'AVDD33_USB', 'P3': 'AVDD18_DSI', 'H6': 'VDDIOA', 'P12': 'VDDIOA2', 'E17': 'VDDIOB', 'M2': 'VDDIOSA', 'G1': 'VDDIOSB', 'F20': 'MIC_BIAS', 'R6': 'USB2_DN', 'T6': 'USB2_DP', 'T5': 'USB2_REXT', 'D4': 'RSTN', 'E6': 'BOOT_MODE', 'G18': 'ADC1N', 'G19': 'ADC1P', 'F18': 'ADC2N', 'F19': 'ADC2P', 'J21': 'DAC1N', 'J20': 'DAC1P', 'H20': 'DAC2N', 'H21': 'DAC2P', 'L20': 'GPADC_VREFN', 'L21': 'GPADC_VREFP', 'L19': 'SDMADC_INPUT', 'K19': 'SDMADC_VREF', 'K20': 'SDMADC_VSS_VREF', 'B18': 'AVSS_BB', 'B17': 'AVSS_CAU', 'P2': 'AVSS_DSI', 'J17': 'AVSS33_ANA', 'G20': 'AVSS33_AUD', 'F21': 'AUD_VREF', 'H19': 'AUD_VREF_GND', 'T2': 'DSI_CLKN', 'T3': 'DSI_CLKP', 'R1': 'DSI_D0N', 'R2': 'DSI_D0P', 'R3': 'DSI_D1N', 'R4': 'DSI_D1P', 'P4': 'DSI_REXT', 'A1': 'VSS', 'A21': 'VSS', 'B6': 'VSS', 'E19': 'VSS', 'G3': 'VSS', 'G8': 'VSS', 'G17': 'VSS', 'H8': 'VSS', 'H9': 'VSS', 'H10': 'VSS', 'H11': 'VSS', 'H12': 'VSS', 'H13': 'VSS', 'H14': 'VSS', 'J8': 'VSS', 'J9': 'VSS', 'J10': 'VSS', 'J11': 'VSS', 'J12': 'VSS', 'J13': 'VSS', 'J14': 'VSS', 'L14': 'VDDIOSC', 'N2': 'VSS', 'R5': 'VSS', 'R7': 'VSS', 'T1': 'AVSS_DSI', 'T21': 'VSS'}
chipnets={'G16': 'UART4_RX_1V8', 'F16': 'UART4_TX_1V8', 'C14': 'HOME_N', 'M16': 'SWDIO_1V8', 'L15': 'SWCLK_1V8', 'J6': 'LCD_TE', 'E14': 'TP_RST', 'F15': 'TP_INT', 'D12': 'LCD_D0', 'E13': 'LCD_CLK', 'G14': 'LCD_CS', 'F13': 'LCD_D3', 'E12': 'LCD_D2', 'F12': 'LCD_D1', 'F6': 'LCD_BL', 'G6': 'LCD_RST', 'N6': 'TP_SDA', 'M6': 'TP_SCL', 'R21': 'SENS_SCL', 'T20': 'SENS_SDA', 'R18': 'PPG_MFIO', 'R19': 'PPG_RST_N', 'P19': 'IMU_INT1', 'R20': 'IMU_INT2', 'P21': 'HAPTIC_EN', 'P20': 'BOOST5_EN', 'N20': 'CHG_N', 'M18': 'PGOOD_N', 'K17': 'VBAT_ADC', 'B16': 'CROWN_A', 'C17': 'CROWN_B', 'D18': 'CROWN_SW_N', 'D17': 'SIDE_SW_N', 'B2': '+1V8', 'B3': '+1V8', 'C1': 'CORE_BUCK1_SW', 'D1': 'CORE_BUCK1_FB', 'A2': 'CORE_BUCK2_SW', 'B1': 'CORE_BUCK2_FB', 'D2': 'CORE_LDO_HP', 'C4': 'CORE_LDO_RET', 'C5': 'CORE_LDO_RTC', 'E20': '+1V8', 'K18': '+3V3', 'J18': '+3V3', 'P5': '+3V3', 'P3': '+1V8', 'H6': 'VIOA', 'P12': '+1V8', 'E17': '+1V8', 'M2': 'VDD_SIP', 'G1': 'VDD_SIP', 'L14': '+1V8', 'F20': 'CORE_MIC_BIAS', 'F21': 'CORE_AUD_VREF', 'L21': 'CORE_ADC_VREF', 'K19': 'CORE_SDM_VREF', 'B7': 'CORE_X32_IN', 'A6': 'CORE_X32_OUT', 'A17': 'CORE_X48_IN', 'A18': 'CORE_X48_OUT', 'D4': 'RESET_N', 'E6': 'BOOT_MODE', 'A15': 'SIP_PWR_EN', 'A9': 'FLASH_PWR_EN', 'M5': 'FLASH_CS_N', 'N1': 'FLASH_D2', 'L5': 'FLASH_D1', 'K5': 'FLASH_D3', 'M1': 'FLASH_CLK', 'K4': 'FLASH_D0', 'P4': 'CORE_DSI_REXT', 'T5': 'CORE_USB_REXT', 'C2': 'GND', 'C3': 'GND', 'A3': 'GND', 'A4': 'GND', 'B20': 'GND', 'B21': 'GND', 'D20': 'GND', 'D21': 'GND', 'C20': 'GND', 'E21': 'GND', 'L20': 'GND', 'K20': 'GND', 'B18': 'GND', 'B17': 'GND', 'P2': 'GND', 'J17': 'GND', 'G20': 'GND', 'H19': 'GND', 'A1': 'GND', 'A21': 'GND', 'B6': 'GND', 'E19': 'GND', 'G3': 'GND', 'G8': 'GND', 'G17': 'GND', 'H8': 'GND', 'H9': 'GND', 'H10': 'GND', 'H11': 'GND', 'H12': 'GND', 'H13': 'GND', 'H14': 'GND', 'J8': 'GND', 'J9': 'GND', 'J10': 'GND', 'J11': 'GND', 'J12': 'GND', 'J13': 'GND', 'J14': 'GND', 'N2': 'GND', 'R5': 'GND', 'R7': 'GND', 'T1': 'GND', 'T21': 'GND'}
groups={'A_PMU': ['B2', 'B3', 'C1', 'D1', 'A2', 'B1', 'D2', 'C4', 'C5', 'B4', 'B5'], 'B_RAILS': ['E20', 'K18', 'J18', 'P5', 'P3', 'H6', 'P12', 'E17', 'M2', 'G1', 'L14', 'F20', 'F21', 'L21', 'K19'], 'C_CLOCK_DEBUG': ['B7', 'A6', 'A17', 'A18', 'D4', 'E6', 'G16', 'F16', 'L15', 'M16', 'C14'], 'D_MEMORY': ['A15', 'A9', 'M5', 'N1', 'L5', 'K5', 'M1', 'K4'], 'B_DISPLAY': ['J6', 'E14', 'F15', 'D12', 'E13', 'G14', 'F13', 'E12', 'F12', 'F6', 'G6', 'N6', 'M6'], 'C_SENSORS': ['R21', 'T20', 'R18', 'R19', 'P19', 'R20', 'P21', 'P20', 'N20', 'M18', 'K17', 'B16', 'C17', 'D18', 'D17'], 'G_GROUND': ['C2', 'C3', 'A3', 'A4', 'B20', 'B21', 'D20', 'D21', 'C20', 'E21', 'L20', 'K20', 'B18', 'B17', 'P2', 'J17', 'G20', 'H19', 'A1', 'A21', 'B6', 'E19', 'G3', 'G8', 'G17', 'H8', 'H9', 'H10', 'H11', 'H12', 'H13', 'H14', 'J8', 'J9', 'J10', 'J11', 'J12', 'J13', 'J14', 'N2', 'R5', 'R7', 'T1', 'T21'], 'E_GPIO_SPARE': ['N13', 'R13', 'L12', 'M12', 'R12', 'T12', 'R11', 'T11', 'N12', 'L11', 'M11', 'L10', 'M10', 'N10', 'N9', 'R10', 'T9', 'R9', 'P9', 'M9', 'L9', 'L8', 'N8', 'T8', 'R8', 'M8', 'M7', 'L7', 'N5', 'N4', 'N3', 'L6', 'M4', 'L2', 'K6', 'K3', 'K2', 'K1', 'J5', 'J4', 'J2', 'J1', 'G5', 'G4', 'H2', 'G2', 'F4', 'E5', 'E4', 'E3', 'F1', 'F2', 'E2', 'D5', 'D6', 'C7', 'D7', 'E7', 'E8', 'F9'], 'F_GPIO_SPARE': ['A8', 'B8', 'B9', 'D9', 'E9', 'F10', 'E10', 'D10', 'B10', 'C10', 'E11', 'B11', 'A11', 'C11', 'M13', 'L13', 'M14', 'R14', 'T14', 'R15', 'T15', 'M15', 'P15', 'N15', 'N16', 'L16', 'K16', 'P17', 'R16', 'T17', 'R17', 'N17', 'J16', 'N18', 'P18', 'T19', 'N19', 'H16', 'L17', 'L18', 'H17', 'M20', 'M21', 'A19', 'A20', 'B19', 'C19', 'D19', 'B15', 'C15', 'D16', 'D14', 'D15', 'E15', 'B14', 'A14', 'B13', 'B12', 'A12'], 'H_ANALOG_SPARE': ['C21', 'R6', 'T6', 'T5', 'G18', 'G19', 'F18', 'F19', 'J21', 'J20', 'H20', 'H21', 'L19', 'T2', 'T3', 'R1', 'R2', 'R3', 'R4', 'P4']}
shortnames={'N13': 'PA00', 'R13': 'PA01', 'L12': 'PA02', 'M12': 'PA03', 'R12': 'PA04', 'T12': 'PA05', 'R11': 'PA06', 'T11': 'PA07', 'N12': 'PA08', 'L11': 'PA09', 'M11': 'PA10', 'L10': 'PA11', 'M10': 'PA12', 'N10': 'PA13', 'N9': 'PA14', 'R10': 'PA15', 'T9': 'PA16', 'R9': 'PA17', 'P9': 'PA18', 'M9': 'PA19', 'L9': 'PA20', 'L8': 'PA21', 'N8': 'PA22', 'T8': 'PA23', 'R8': 'PA24', 'M8': 'PA25', 'M7': 'PA26', 'L7': 'PA27', 'M6': 'PA28', 'N6': 'PA29', 'M5': 'PA30', 'N5': 'PA31', 'N4': 'PA32', 'N3': 'PA33', 'L6': 'PA34', 'M4': 'PA35', 'N1': 'PA36', 'L5': 'PA37', 'K5': 'PA38', 'M1': 'PA39', 'K4': 'PA40', 'L2': 'PA41', 'K6': 'PA42', 'J6': 'PA43', 'K3': 'PA44', 'K2': 'PA45', 'K1': 'PA46', 'J5': 'PA47', 'J4': 'PA48', 'J2': 'PA49', 'J1': 'PA50', 'G6': 'PA51', 'F6': 'PA52', 'G5': 'PA53', 'G4': 'PA54', 'H2': 'PA55', 'G2': 'PA56', 'F4': 'PA57', 'E5': 'PA58', 'E4': 'PA59', 'E3': 'PA60', 'F1': 'PA61', 'F2': 'PA62', 'E2': 'PA63', 'D5': 'PA64', 'D6': 'PA65', 'C7': 'PA66', 'D7': 'PA67', 'E7': 'PA68', 'E8': 'PA69', 'F9': 'PA70', 'A8': 'PA71', 'B8': 'PA72', 'B9': 'PA73', 'A9': 'PA74', 'D9': 'PA75', 'E9': 'PA76', 'F10': 'PA77', 'E10': 'PA78', 'D10': 'PA79', 'B10': 'PA80', 'C10': 'PA81', 'F12': 'PA82', 'E11': 'PA83', 'E12': 'PA84', 'B11': 'PA85', 'F13': 'PA86', 'A11': 'PA87', 'G14': 'PA88', 'C11': 'PA89', 'E13': 'PA90', 'D12': 'PA91', 'F15': 'PA92', 'E14': 'PA93', 'M13': 'PB00', 'L13': 'PB01', 'M14': 'PB02', 'R14': 'PB03', 'T14': 'PB04', 'R15': 'PB05', 'T15': 'PB06', 'L15': 'PB07', 'M15': 'PB08', 'P15': 'PB09', 'N15': 'PB10', 'M16': 'PB11', 'N16': 'PB12', 'L16': 'PB13', 'K16': 'PB14', 'P17': 'PB15', 'R16': 'PB16', 'T17': 'PB17', 'R17': 'PB18', 'N17': 'PB19', 'J16': 'PB20', 'N18': 'PB21', 'P18': 'PB22', 'R18': 'PB23', 'P19': 'PB24', 'T19': 'PB25', 'R19': 'PB26', 'R20': 'PB27', 'R21': 'PB28', 'T20': 'PB29', 'P20': 'PB30', 'P21': 'PB31', 'N19': 'PB32', 'H16': 'PB33', 'N20': 'PB34', 'L17': 'PB35', 'G16': 'PB36', 'F16': 'PB37', 'K17': 'PB38', 'M18': 'PB39', 'L18': 'PB40', 'H17': 'PB41', 'M20': 'PB42', 'M21': 'PB43', 'A19': 'PB44', 'A20': 'PB45', 'B19': 'PB46', 'B16': 'PB47', 'C17': 'PB48', 'C19': 'PB49', 'D19': 'PB50', 'D18': 'PB51', 'D17': 'PB52', 'B15': 'PB53', 'C14': 'PB54', 'C15': 'PB55', 'D16': 'PB56', 'D14': 'PB57', 'D15': 'PB58', 'E15': 'PB59', 'A15': 'PBR0', 'B14': 'PBR1', 'A14': 'PBR2', 'B13': 'PBR3', 'B12': 'PBR4', 'A12': 'PBR5', 'B2': 'PVDD_PMU1', 'B3': 'PVDD_PMU2', 'C2': 'PVSS_PMU1', 'C3': 'PVSS_PMU2', 'C1': 'PMU_BUCK1_VSW', 'D1': 'PMU_BUCK1_VOUT', 'B1': 'PMU_BUCK2_VOUT', 'A2': 'PMU_BUCK2_VSW', 'A3': 'VSS', 'A4': 'VSS', 'B4': 'VDD_EXT1', 'B5': 'VDD_EXT2', 'D2': 'HPSYS_LDO_VOUT', 'C4': 'LPMU_VDD07_RET', 'C5': 'LPMU_VDD11_RTC', 'B7': 'XTAL32K_XI', 'A6': 'XTAL32K_XO', 'A17': 'XTAL48M_XI', 'A18': 'XTAL48M_XO', 'C21': 'BRF_ANT', 'E20': 'AVDD_BRF', 'B20': 'AVSS_RRF', 'B21': 'AVSS_RRF', 'D20': 'AVSS_TRF', 'D21': 'AVSS_TRF', 'C20': 'AVSS_VCO', 'E21': 'AVSS_TRF2', 'K18': 'AVDD33_ANA', 'J18': 'AVDD33_AUD', 'P5': 'AVDD33_USB', 'P3': 'AVDD18_DSI', 'H6': 'VDDIOA', 'P12': 'VDDIOA2', 'E17': 'VDDIOB', 'M2': 'VDDIOSA', 'G1': 'VDDIOSB', 'F20': 'MIC_BIAS', 'R6': 'USB2_DN', 'T6': 'USB2_DP', 'T5': 'USB2_REXT', 'D4': 'RSTN', 'E6': 'BOOT_MODE', 'G18': 'ADC1N', 'G19': 'ADC1P', 'F18': 'ADC2N', 'F19': 'ADC2P', 'J21': 'DAC1N', 'J20': 'DAC1P', 'H20': 'DAC2N', 'H21': 'DAC2P', 'L20': 'GPADC_VREFN', 'L21': 'GPADC_VREFP', 'L19': 'SDMADC_INPUT', 'K19': 'SDMADC_VREF', 'K20': 'SDMADC_VSS_VREF', 'B18': 'AVSS_BB', 'B17': 'AVSS_CAU', 'P2': 'AVSS_DSI', 'J17': 'AVSS33_ANA', 'G20': 'AVSS33_AUD', 'F21': 'AUD_VREF', 'H19': 'AUD_VREF_GND', 'T2': 'DSI_CLKN', 'T3': 'DSI_CLKP', 'R1': 'DSI_D0N', 'R2': 'DSI_D0P', 'R3': 'DSI_D1N', 'R4': 'DSI_D1P', 'P4': 'DSI_REXT', 'A1': 'VSS', 'A21': 'VSS', 'B6': 'VSS', 'E19': 'VSS', 'G3': 'VSS', 'G8': 'VSS', 'G17': 'VSS', 'H8': 'VSS', 'H9': 'VSS', 'H10': 'VSS', 'H11': 'VSS', 'H12': 'VSS', 'H13': 'VSS', 'H14': 'VSS', 'J8': 'VSS', 'J9': 'VSS', 'J10': 'VSS', 'J11': 'VSS', 'J12': 'VSS', 'J13': 'VSS', 'J14': 'VSS', 'L14': 'VDDIOSC', 'N2': 'VSS', 'R5': 'VSS', 'R7': 'VSS', 'T1': 'AVSS_DSI', 'T21': 'VSS'}

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
module(s,'B_DISPLAY',340,1400)
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
module(s,'C_SENSORS',310,1400)
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
s.note(1140,350,['Host I2C / MFIO / reset all 1.8 V. I2C pull-ups are on sheet 08.',
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
with (OUT/'BOM_v0.2.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(COMPONENTS[0]));writer.writeheader();seen=set()
    for c in COMPONENTS:
        if c['ref'] not in seen:writer.writerow(c);seen.add(c['ref'])
with (OUT/'pin-net-map_v0.2.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(PINS[0]));writer.writeheader();writer.writerows(PINS)
assert len({(p['ref'],p['pin']) for p in PINS})==len(PINS),'duplicate physical pin'
assert {p['pin'] for p in PINS if p['ref']=='U1'}==set(ballmap)
netdict=collections.defaultdict(list)
for p in PINS:
    if p['net']:netdict[p['net']].append(p['ref']+'.'+p['pin'])
single={n:v for n,v in netdict.items() if len(v)<2}
assert not single,single
assert sum(p['net'] is not None for p in PINS if p['ref']=='U1')==len(chipnets)
report={'sheets':len(SHEETS),'unique_components':len({c['ref'] for c in COMPONENTS}),'physical_pins':len(PINS),'electrical_nets':len(netdict),'intentionally_open_pins':sum(p['net'] is None for p in PINS),'checks':['All component pins are explicitly connected or intentionally open','No duplicated physical pin across BGA units','All 256 SF32LB586VDD36 BGA balls represented','No singleton named nets','All symbol/device references resolve within the epro archive'],'limitations':['These checks are model-level, not a substitute for EDA ERC or circuit review','BGA and most IC footprints are not yet bound; no PCB release','NC text denotes open pins; EDA no-connect markers still require ERC review','Battery, panel adapter, optics and LRA mechanical definitions are pending']}
(OUT/'validation-model.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'output':str(OUT),'report':report},ensure_ascii=False,indent=2))

with (OUT/'chip-ball-audit.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.writer(f);writer.writerow(['ball','original_sifli_pin_name','schematic_short_name','net','unit'])
    for unit,balls in groups.items():
        for b in balls:writer.writerow([b,ballmap[b],shortnames[b],chipnets.get(b),unit])
