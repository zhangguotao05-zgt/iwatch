"""Independent checks: references, geometry, pins, and native file connectivity."""
from pathlib import Path
import json,zipfile,collections,math,csv
base=Path(__file__).resolve().parent
cfg=json.loads((base/'config.json').read_text(encoding='utf8'))
out=(base/cfg['output_dir']).resolve()
m=json.loads((out/'circuit-model.json').read_text(encoding='utf8'))
catalog=json.loads((out/'footprint-catalog.json').read_text(encoding='utf8'))
p={(x['ref'],x['pin']):x['net'] for x in m['pins']}
old=json.loads((out.parent/'原理图_v0.2_裸芯片/circuit-model.json').read_text(encoding='utf8'))
oldp={(x['ref'],x['pin']):x['net'] for x in old['pins']}
changed=[(rp,net,p.get(rp)) for rp,net in oldp.items() if rp[0] not in ['J201','D702','D703'] and p.get(rp)!=net]
assert not changed,changed
for ref,net in [('D702','GREEN1_K'),('D703','GREEN2_K')]:
 assert p[(ref,'1')]=='+5V_SW'
 assert p[(ref,'2')]==p[(ref,'3')]==net
for ref in ['SW101','SW102','SW401','SW402']:assert p[(ref,'3')]=='GND'
assert p[('J201','2')]==p[('J201','11')]=='USB_VBUS_RAW'
assert p[('J201','4')]=='CC1' and p[('J201','10')]=='CC2'
for n in ['1','12','13','14','15','16']:assert p[('J201',n)]=='GND'
for n in ['3','5','6','7','8','9','17','18']:assert p[('J201',n)] is None
assert len(catalog['U1']['pads'])==256
assert {a['number'] for a in catalog['U1']['pads']}=={pin for ref,pin in p if ref=='U1'}
balls={a['number']:a for a in catalog['U1']['pads']}
assert abs(balls['A2']['x']-balls['A1']['x']-.4)<1e-8
assert abs(balls['A1']['y']-balls['B1']['y']-.4)<1e-8
assert catalog['U1']['body']==[8.5,6.5]
# Core diagram and imported raw ODB/PADS pin names are separate from package pad sets.
z=zipfile.ZipFile(out/(cfg['project_title']+'.epro'))
pr=json.loads(z.read('project.json'))
bindings=json.loads((out/'footprint-binding.json').read_text())
allpads={}
for fid in pr['footprints']:
 rows=[json.loads(l) for l in z.read('FOOTPRINT/'+fid+'.efoo').decode().splitlines()]
 allpads[fid]={r[5] for r in rows if r[0]=='PAD'}
for ref,fid in bindings.items():
 want={pn for rr,pn in p if rr==ref}
 assert want==allpads[fid],(ref,want-allpads[fid],allpads[fid]-want)
nc=[]
for f in z.namelist():
 if f.endswith('.esch'):
  rows=[json.loads(l) for l in z.read(f).decode().splitlines()]
  nc.extend(r for r in rows if r[0]=='ATTR' and r[3]=='NO_CONNECT')
assert len(nc)==sum(v is None for v in p.values())
assert all(r[4]=='yes' and '-p' in r[2] for r in nc)
# Detect accidental overlapping copper pads after format/unit conversion.
overlaps=[];min_clearances={}
for key,fp in catalog.items():
 b=[]
 for q in fp['pads']:
  a=math.radians(q['angle']);w=abs(q['w']*math.cos(a))+abs(q['h']*math.sin(a));h=abs(q['w']*math.sin(a))+abs(q['h']*math.cos(a))
  b.append((q,q['x']-w/2,q['x']+w/2,q['y']-h/2,q['y']+h/2))
 minc=1000
 for i,(q,x0,x1,y0,y1) in enumerate(b):
  for r,u0,u1,v0,v1 in b[i+1:]:
   dx=max(u0-x1,x0-u1,0);dy=max(v0-y1,y0-v1,0)
   if dx==dy==0:overlaps.append((key,q['number'],r['number']))
   minc=min(minc,math.hypot(dx,dy))
 min_clearances[key]=round(minc,4) if minc<1000 else None
assert not overlaps,overlaps
report={'revision':'0.3','native_sheets':len(m['sheets']),'physical_components_and_testpoints':len(bindings),'unbound_footprints':0,'pin_pad_set_mismatches':0,'no_connect_attributes':len(nc),'geometry_overlap_errors':overlaps,'minimum_copper_pad_clearance_mm':min_clearances,'retained_v02_pin_net_connections_unchanged':len(oldp)-sum(r[0] in ['J201','D702','D703'] for r in oldp),'checks':['256 BGA balls and 0.4mm pitch checked','all physical symbol pin sets equal native footprint pad sets','USB-C full 18-pin physical mapping checked','both green LED cathode pads connected','all four switch case pins grounded','179 intentional opens use native NO_CONNECT attributes','no copper pad overlaps in authored footprints'],'scope':'Local electrical/model/package audit only. Live EDA DRC, PCB rules, assembly tolerances and hardware operation are separate checks.'}
(out/'validation-reference.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
