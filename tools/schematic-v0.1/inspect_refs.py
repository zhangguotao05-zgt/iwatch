import json, pathlib, zipfile, urllib.request, concurrent.futures
ROOT = pathlib.Path(__file__).resolve().parent
REF = ROOT / 'references'
z = zipfile.ZipFile(REF / 'SF58-official.epro')
meta = json.loads(z.read('project.json'))
(REF / 'official-project.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding='utf-8')
print('metadata', {k: (len(v) if isinstance(v,(dict,list)) else v) for k,v in meta.items()})
for uid, item in meta['symbols'].items():
    if any(x in item.get('title','') for x in ['SF32LB58','RES-0','RES-10','CAP-0.1','SW-','SWITCH']):
        print('symbol',uid,item['title'])
for uid,item in meta.get('devices',{}).items():
    if 'SF32LB58' in json.dumps(item) or 'CAP-0.1' in json.dumps(item):
        print('device',uid,json.dumps(item,ensure_ascii=False)[:1600])
for name in z.namelist():
    if name.endswith('.esch'):
        rows=[json.loads(l) for l in z.read(name).decode().splitlines() if l]
        out=REF / ('official-' + pathlib.Path(name).name)
        out.write_text('\n'.join(json.dumps(a,ensure_ascii=False) for a in rows),encoding='utf-8')
        comps={a[1]:{'record':a,'attrs':{}} for a in rows if a[0]=='COMPONENT'}
        for a in rows:
            if a[0]=='ATTR' and a[2] in comps: comps[a[2]]['attrs'][a[3]]=a[4]
        if name.endswith('/1.esch'):
            for c in comps.values():
                if c['attrs'].get('Designator') == 'U0100': print('module-unit',json.dumps(c,ensure_ascii=False))
        if name.endswith('/3.esch'):
            for c in comps.values():
                if c['attrs'].get('Designator'): print('reference-power',json.dumps(c,ensure_ascii=False)[:800])
        print('sheet-shapes',name,{key:next((a for a in rows if a[0]==key),None) for key in ['WIRE','JUNCTION','LINESTYLE','NET','TEXT','COMPONENT','ATTR']})
for name in zipfile.ZipFile(REF/'SF32LB58-MOD-V1.0.1.zip').namelist():
    if name.endswith('SCH-V1.0.1.pdf'):
        (REF/'SF58-module.pdf').write_bytes(zipfile.ZipFile(REF/'SF32LB58-MOD-V1.0.1.zip').read(name))
sources={
 'bq24074.pdf':'https://www.ti.com/lit/ds/symlink/bq24074.pdf',
 'tps62840.pdf':'https://www.ti.com/lit/ds/symlink/tps62840.pdf',
 'tps63031.pdf':'https://www.ti.com/lit/ds/symlink/tps63031.pdf',
 'drv2605l.pdf':'https://www.ti.com/lit/ds/symlink/drv2605l.pdf',
 'lsm6dso.pdf':'https://www.st.com/resource/en/datasheet/lsm6dso.pdf',
 'maxm86146.pdf':'https://www.analog.com/media/en/technical-documentation/data-sheets/maxm86146.pdf',
 'maxm86146evsys.pdf':'https://www.analog.com/media/en/technical-documentation/data-sheets/maxm86146evsys.pdf',
}
def fetch(kv):
    name,url=kv
    try:
        target=REF/name
        if not target.exists():
            req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0'})
            with urllib.request.urlopen(req,timeout=35) as r: data=r.read()
            if not data.startswith(b'%PDF'): raise ValueError('Not a PDF')
            target.write_bytes(data)
        from pypdf import PdfReader
        reader=PdfReader(target)
        (REF/(name+'.txt')).write_text('\n\n'.join(f'PAGE {i+1}\n'+(page.extract_text() or '') for i,page in enumerate(reader.pages)),encoding='utf-8')
        return name,len(reader.pages)
    except Exception as exc:return name,str(exc)
with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
    for result in pool.map(fetch,sources.items()):print('download',result)
