from pathlib import Path
import json, zipfile, collections
base=Path(__file__).resolve().parent
cfg=json.loads((base/'config.json').read_text(encoding='utf8'))
refs=(base/cfg['reference_dir']).resolve()
z=zipfile.ZipFile(refs/'SF58-official.epro')
p=json.loads(z.read('project.json'))
print('OFFICIAL FOOTPRINTS')
for k,v in p['footprints'].items():print(k,v['title'])
print('SAMPLE FOOTPRINT')
fid=next(iter(p['footprints']))
print(z.read('FOOTPRINT/'+fid+'.efoo').decode()[:9000])
types=collections.Counter()
samples={}
for f in z.namelist():
 if f.endswith('.esch'):
  for l in z.read(f).decode().splitlines():
   r=json.loads(l);types[r[0]]+=1
   if r[0] not in ['ATTR','WIRE','COMPONENT','FONTSTYLE','LINESTYLE','POLY','TEXT']:
    samples.setdefault(r[0],r)
print('SCH ROW TYPES',types)
print('SPECIAL ROW EXAMPLES',json.dumps(samples,ensure_ascii=False))
print('DEVICES WITH PIN MAPPING')
for k,v in p['devices'].items():
 if any('pin' in str(t).lower() or 'pad' in str(t).lower() for t in v.get('attributes',{})):
  print(k,json.dumps(v,ensure_ascii=False)[:2500])
print('UNBOUND')
old=base.parents[1]/'hardware/原理图_v0.2_裸芯片/circuit-model.json'
m=json.loads(old.read_text(encoding='utf8'));seen=set()
for c in m['components']:
 if c['ref'] not in seen and not c['footprint']:print(c['ref'],c['value'],c['package'])
 seen.add(c['ref'])
