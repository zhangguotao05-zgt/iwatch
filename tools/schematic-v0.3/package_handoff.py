from pathlib import Path
import json,csv,hashlib,zipfile
from pypdf import PdfReader
BASE=Path(__file__).resolve().parent
cfg=json.loads((BASE/'config.json').read_text(encoding='utf8'))
out=(BASE/cfg['output_dir']).resolve()
required=['HealthWatch-SF58-BareChip-v0.3.epro','HealthWatch-SF58-v0.3-裸芯片原理图审阅.pdf','BOM_v0.3.csv','采购合并表.csv','阅读说明.md','上电与固件调试.md','PCB实施约束.md','validation-eda-drc.json','validation-pcb-transfer.json','validation-reference.json']
assert all((out/x).is_file() for x in required)
assert len(PdfReader(out/required[1]).pages)==13
rows=list(csv.DictReader((out/'采购合并表.csv').open(encoding='utf-8-sig',newline='')))
assert sum(int(r['数量']) for r in rows)==133
assert sum(int(r['数量']) for r in rows if r['装配']=='DNP')==8
assert zipfile.ZipFile(out/required[0]).testzip() is None
manifest=[]
for p in sorted(out.iterdir()):
    if p.is_file() and p.name!='SHA256SUMS.txt':manifest.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name)
(out/'SHA256SUMS.txt').write_text('\n'.join(manifest)+'\n',encoding='utf8')
bundle=out.parent/'HealthWatch-SF58-v0.3-工程资料包.zip'
with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(out.iterdir()):
        if p.is_file():z.write(p,out.name+'/'+p.name)
with zipfile.ZipFile(bundle) as z:assert z.testzip() is None
print(bundle)
print('files',len(manifest)+1,'bytes',bundle.stat().st_size,'sha256',hashlib.sha256(bundle.read_bytes()).hexdigest())
