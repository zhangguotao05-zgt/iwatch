"""Local EasyEDA Pro footprints. All geometry stored in mm, top-view Y up.

Sources: SiFli native PADS decals, ADI published ODB++, and manufacturer
dimensioned package/land drawings. No downloaded code is executed.
"""
from pathlib import Path
import re,json,copy,math

BASE=Path(__file__).resolve().parent
CFG=json.loads((BASE/'config.json').read_text(encoding='utf8'))
OLD=(BASE/CFG['reference_dir']).resolve()
SIFLI='https://downloads.sifli.com/hardware/files/documentation/SF32LB58-MOD-V1.0.1.zip'
ADI='https://www.analog.com/media/en/evaluation-documentation/evaluation-design-files/maxm86146_gerbers.zip'
def pad(n,x,y,w,h=None,angle=0,shape='RECT',hole=None):
 return dict(number=str(n),x=x,y=y,w=w,h=h if h is not None else w,angle=angle,shape=shape,hole=hole)

def pads_decal(name,body):
 text=(OLD/'SF32LB58-MOD-PCB_V1.0.1.asc').read_text(errors='replace')
 start=re.search(r'^'+re.escape(name)+r'\s+M\s+',text,re.M).start()
 first_eol=text.index('\n',start)
 end=re.search(r'^\S+\s+M\s+[-\d]+\s+[-\d]+\s+\d+\s+\d+\s+\d+\s+\d+\s+\d+\s*$',text[first_eol+1:],re.M)
 block=text[start:first_eol+1+end.start()] if end else text[start:]
 # PADS BASIC uses 1,500,000 coordinates/mm in this export. Independent
 # check: BGA pitch=600000 ticks/0.4mm; body=12750000 ticks/8.5mm.
 scale=1500000
 ts=re.findall(r'^T(-?\d+)\s+(-?\d+)\s+-?\d+\s+-?\d+\s+(\S+)\s*$',block,re.M)
 definitions={}
 for n,lines in re.findall(r'^PAD (\d+) \d+\n(.*?)(?=^PAD |\Z)',block,re.M|re.S):
  line=next((l for l in lines.splitlines() if l.startswith('-2 ')),None)
  if line:definitions[n]=line.split()
 result=[]
 for x,y,n in ts:
  d=definitions.get(n,definitions['0']);shape='ELLIPSE' if d[2]=='R' else 'RECT'
  if shape=='ELLIPSE':w=h=float(d[1])/scale;angle=0
  else:w=float(d[4])/scale;h=float(d[1])/scale;angle=float(d[3])
  result.append(pad(n,int(x)/scale,int(y)/scale,w,h,angle,shape))
 assert len(result)==len({p['number'] for p in result})
 return dict(title=name,pads=result,body=body,source=SIFLI,basis='Direct SiFli PADS decal conversion; copper pads preserved',mask=.04 if 'BGA' in name else .05)

def odb_package(name,body):
 p=BASE/'references/adi-design/odb/maxm86146_osb_evkit_a_odb/steps/stp/eda/data'
 text=p.read_text(encoding='utf8')
 block=text.split('PKG '+name+' ')[1].split('\nPKG ')[0]
 lines=block.splitlines();result=[]
 for i,line in enumerate(lines):
  if line.startswith('PIN '):
   a=line.split();n=a[1];x,y=float(a[3])*25.4,float(a[4])*25.4
   shape=lines[i+1].split()
   if shape[0]=='RC':
    result.append(pad(n,x,y,float(shape[3])*25.4,float(shape[4])*25.4))
   elif shape[0]=='CR':result.append(pad(n,x,y,float(shape[3])*50.8,shape='ELLIPSE'))
   else:raise ValueError(shape)
 return dict(title=name,pads=result,body=body,source=ADI,basis='Direct ADI ODB++ package copper-pad geometry; no placement mirroring applied',mask=.035 if '0459' in name else .04)

def dualrow(title,n,pitch,rowspan,pw,ph,body,source,ep=None):
 m=n//2;p=[]
 for i in range(m):
  y=(m-1)*pitch/2-i*pitch
  p+=[pad(i+1,-rowspan/2,y,pw,ph),pad(n-i,rowspan/2,y,pw,ph)]
 if ep:p.append(pad(n+1,0,0,*ep))
 return dict(title=title,pads=p,body=body,source=source,basis='Manufacturer dimensioned land pattern',mask=.05)

def build_catalog():
 c={}
 for key,name,body in [
  ('U1','BGA_256_P040_8500X6500_H094_B025',(8.5,6.5)),
  ('LCORE','IND_C_2016_H100',(2.0,1.6)),
  ('Y101','XTAL_CC_200X160_H050',(2,1.6)),
  ('Y102','XTAL_2SM_320X150_H080',(3.2,1.5)),
  ('MOS','SOT723-1.2X1.2X0.5MM',(1.2,.8))]:c[key]=pads_decal(name,body)
 for key,name,body in [('U601','MAXIM_90-100112',(4.1,4.5)),('U701','MAXIM_21-0459_W91B1-7',(1.2,1.2)),('D701','LED_SFH7015',(2,.8)),('GREEN','OSRM_CT-DBLP31_12-1',(1.8,2.2)),('Y601','XTALDFN160X100X50-2M',(1.6,1.0))]:c[key]=odb_package(name,body)
 c['U301']=dualrow('TI_DLC0008B',8,.5,1.3,.6,.25,(1.5,2),'https://www.ti.com/lit/ds/symlink/tps62840.pdf')
 c['U302']=dualrow('TI_DSK0010A',10,.5,2.3,.6,.25,(2.5,2.5),'https://www.ti.com/lit/ds/symlink/tps63031.pdf',ep=(1.2,2.0))
 c['U303']=dualrow('TI_DRV0006A',6,.65,1.95,.45,.3,(2,2),'https://www.ti.com/lit/ds/symlink/tps61099.pdf',ep=(1,1.6))
 c['U502']=dualrow('TI_DGS0010A',10,.5,4.4,1.45,.3,(3,3),'https://www.ti.com/lit/ds/symlink/drv2605l.pdf')
 p=[]
 for i in range(4):
  q=.75-i*.5
  p.extend([pad(i+1,-1.4,q,.6,.24),pad(i+5,-q,-1.4,.24,.6),pad(i+9,1.4,-q,.6,.24),pad(i+13,q,1.4,.24,.6)])
 p.append(pad(17,0,0,1.68,1.68))
 c['U201']=dict(title='TI_RGT0016C',pads=p,body=(3,3),source='https://www.ti.com/lit/ds/symlink/bq24074.pdf',basis='TI land pattern 422419/E 07/2025, p51',mask=.05)
 # Dedicated 6x5 WSON instead of the larger dual-package SiFli land pattern.
 p=[]
 for i in range(4):
  x=-1.905+i*1.27
  p.extend([pad(i+1,x,-2.8,.5,.9),pad(8-i,x,2.8,.5,.9)])
 p.append(pad(9,0,0,4.0,3.4))
 c['U104']=dict(title='PUYA_WSON8_6x5_P1.27_EP4x3.4',pads=p,body=(5,6),source='https://www.puyasemi.com/en/h_series653/3187.html',basis='Puya V2.3 p101 top-view package geometry; pads extended for solder fillet, engineering land pattern',mask=.05)
 # LSM6DSO DS12140 p142 is a bottom view; mirror X to obtain top view.
 p=[]
 for i in range(4):
  p.extend([pad(i+1,-1.2125,.75-i*.5,.575,.25),pad(i+8,1.2125,-.75+i*.5,.575,.25)])
 for i in range(3):p.extend([pad(i+5,-.5+i*.5,-.9625,.25,.575),pad(i+12,.5-i*.5,.9625,.25,.575)])
 c['U501']=dict(title='ST_LSM6DSO_LGA14_3x2.5_P0.5',pads=p,body=(3,2.5),source='https://www.st.com/resource/en/datasheet/lsm6dso.pdf',basis='ST DS12140 Rev3 p142 top-view pin geometry; lands add 0.05mm on each terminal end',mask=.035)
 c['LPOWER']=dict(title='Coilcraft_XFL3012',pads=[pad(1,-1.015,0,1,2.9),pad(2,1.015,0,1,2.9)],body=(3,3),source='https://www.coilcraft.com/en-us/products/power/shielded-inductors/molded-inductor/xfl/xfl3012/xfl3012-222/',basis='Coilcraft dimensioned recommended land pattern: 2.03mm centers, 1.0x2.90 pads',mask=.05)
 c['F201']=dict(title='PTC_1206_Littelfuse',pads=[pad(1,-1.4,0,1,1.8),pad(2,1.4,0,1,1.8)],body=(3.4,1.8),source='https://www.littelfuse.com/assetdocs/littelfuse-ptc-1206l-datasheet?assetguid=2b6a1515-d4ee-4c83-8bd4-152b4901b8f5',basis='1206L datasheet pad layout p4: 1.0x1.8mm pads, 1.8mm gap',mask=.05)
 c['C0805']=dict(title='C_0805_2012',pads=[pad(1,-.95,0,1,1.45),pad(2,.95,0,1,1.45)],body=(2,1.25),source='',basis='Engineering MLCC 0805 land pattern; confirm selected MLCC DC-bias capacitance',mask=.05)
 for ref,n,pitch,w,h in [('J101',7,1.27,.8,1.4),('J202',3,2,1.2,2),('J402',5,1.27,.8,1.4),('J501',2,2,1.2,2)]:
  c[ref]=dict(title=ref+'_WIRE_OR_POGO_'+str(n)+'P',pads=[pad(i+1,(i-(n-1)/2)*pitch,0,w,h) for i in range(n)],body=((n-1)*pitch+w+.4,h+.4),source='',basis='Defined solder-wire / pogo interface on the custom PCB; no purchased connector footprint implied',mask=.05,nopaste=True)
 c['TEST']=dict(title='TEST_PAD_D1.0',pads=[pad(1,0,0,1,shape='ELLIPSE')],body=(1.4,1.4),source='',basis='Defined 1.0mm exposed top copper test point; no purchased component',mask=.05,nopaste=True)
 return c

def install(ns):
 catalog=build_catalog();pr=ns['PROJECT'];files=ns['FILES'];uid=ns['uid'];dump=ns['dump'];refzip=ns['REF']
 template=[json.loads(l) for l in refzip.read('FOOTPRINT/e3f3bece9e6c4c37a159570def6f46e2.efoo').decode().splitlines()]
 base=[r for r in template if r[0] in ['DOCTYPE','HEAD','CANVAS','LAYER','ACTIVE_LAYER']]
 mm=lambda x:round(x/0.0254,6)
 fpids={}
 for key,f in catalog.items():
  fid=uid('footprint/'+f['title']);fpids[key]=fid
  rows=copy.deepcopy(base);rows[1]=['HEAD',{'editorVersion':'2.2.40.3','importFlag':0,'title':f['title']}]
  for i,p in enumerate(f['pads']):
   shape=[p['shape'],mm(p['w']),mm(p['h'])]
   if p['shape']=='RECT':shape.append(0)
   rows.append(['PAD','p'+str(i),0,'',1,p['number'],mm(p['x']),mm(p['y']),p['angle'],None,shape,[],0,0,None,0,0,mm(f['mask']),mm(f['mask']),-1000 if f.get('nopaste') else 0,-1000 if f.get('nopaste') else 0,0,None,None,None,None,[]])
  w,h=f['body'];xy=[-mm(w/2),-mm(h/2),'L',mm(w/2),-mm(h/2),mm(w/2),mm(h/2),-mm(w/2),mm(h/2),-mm(w/2),-mm(h/2)]
  rows.append(['POLY','body',0,'',48,mm(.05),xy,0])
  # Courtyard on Top Assembly so it is not mistaken for copper.
  margin=.25;xx,yy=mm(w/2+margin),mm(h/2+margin)
  rows.append(['POLY','courtyard',0,'',9,mm(.05),[-xx,-yy,'L',xx,-yy,xx,yy,-xx,yy,-xx,-yy],0])
  first=f['pads'][0]
  rows.append(['FILL','pin1',0,'',49,mm(.01),0,[['CIRCLE',mm(first['x']),mm(first['y']),mm(.08)]],0])
  rows.append(['ATTR','a0',0,'',3,None,None,'Footprint',f['title'],0,0,'default',mm(1),mm(.12),0,0,3,0,0,0,0,0])
  rows.append(['ATTR','a1',0,'',3,None,None,'Designator','U?',0,mm(h/2+.8),'default',mm(1),mm(.12),0,0,3,0,0,0,0,0])
  pr['footprints'][fid]={'title':f['title'],'type':4,'version':'1','desc':f['basis'],'tags':{'parent_tag':[],'child_tag':[]}}
  files['FOOTPRINT/'+fid+'.efoo']=dump(rows)
 # Manufacturer reference footprints that include plated slots / mounting pins.
 fpids['J201']=ns['footprint']('d253286b093641388fb3882a5ad11ef5')
 fpids['SW']=ns['footprint']('6c0c3bab771e4503b73ba1a140a611b1')
 mapping={c['ref']:c['footprint'] for c in ns['COMPONENTS']}
 for ref in mapping:
  key=ref if ref in fpids else 'LCORE' if ref in ['L101','L102'] else 'LPOWER' if ref in ['L301','L302','L303'] else 'MOS' if ref.startswith('Q') else 'GREEN' if ref in ['D702','D703'] else 'SW' if ref.startswith('SW') else 'TEST' if ref.startswith('TP') else None
  if ref in ['C702','C703']:key='C0805'
  if key:mapping[ref]=fpids[key]
 for s in ns['SHEETS']:
  byparent={}
  for r in s.rows:
   if r[0]=='ATTR':byparent.setdefault(r[2],{})[r[3]]=r
  for parent,attrs in byparent.items():
   if 'Designator' in attrs:
    ref=attrs['Designator'][4]
    attrs['Footprint'][4]=mapping[ref]
    d=pr['devices'][attrs['Device'][4]]['attributes'];d['Footprint']=mapping[ref];d['Footprint Status']='BOUND - geometry and pin set checked; PCB layout review required'
    if ref.startswith('TP') or ref in ['J101','J202','J402','J501']:
     attrs['Add into BOM'][4]='no';d['Add into BOM']='no'
 for c in ns['COMPONENTS']:
  c['footprint']=mapping[c['ref']]
  c['package']=pr['footprints'][c['footprint']]['title']
  c['assembly']='DNP' if 'DNP' in c['value'] else 'PCB feature' if c['ref'].startswith('TP') or c['ref'] in ['J101','J202','J402','J501'] else 'FIT'
  c['mpn']=c['device']
  if c['ref'] in ['L101','L102']:c['mpn']='WPN201610U4R7MT'
  if c['ref'] in ['L301','L303']:c['mpn']='XFL3012-222MEC'
  if c['ref']=='L302':c['mpn']='XFL3012-152MEC'
  if c['ref']=='Y601':c['mpn']='CM1610H32768DZB'
  if c['ref']=='F201':c['mpn']='1206L050/15YR'
  if c['ref'].startswith('SW'):c['mpn']='TC-1109DE-C-C'
  if c['ref']=='J201':c['mpn']='1012-16FGG0201R3'
  if c['assembly']=='PCB feature':c['mpn']='NO COMPONENT - PCB PAD'
  for d in pr['devices'].values():
   if d['title']==c['device']:d['attributes']['Manufacturer Part']=c['mpn']
 out=ns['OUT']
 (out/'footprint-catalog.json').write_text(json.dumps(catalog,indent=2),encoding='utf8')
 (out/'footprint-binding.json').write_text(json.dumps(mapping,indent=2),encoding='utf8')
 assert all(mapping.values()),[r for r,f in mapping.items() if not f]
 return catalog,mapping
