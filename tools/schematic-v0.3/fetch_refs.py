from pathlib import Path
import urllib.request, concurrent.futures, json, re
from pypdf import PdfReader
base=Path(__file__).resolve().parent
dst=base/'references';dst.mkdir(exist_ok=True)
sources={
 'tps61099.pdf':'https://www.ti.com/lit/ds/symlink/tps61099.pdf',
 'max14689.pdf':'https://www.analog.com/media/en/technical-documentation/data-sheets/MAX14689.pdf',
 'sfh7015.pdf':'https://look.ams-osram.com/m/e1bd35e2ed36dce/original/SFH-7015.pdf',
 'ctdblp31.pdf':'https://look.ams-osram.com/m/7a84e13abf7bd695/original/CT-DBLP31-12.pdf',
 'maxm86146evsys.pdf':'https://www.analog.com/media/en/technical-documentation/data-sheets/MAXM86146EVSYS.pdf',
 'maxm86146-lands.pdf':'https://www.analog.com/media/en/package-pcb-resources/pcb-footprints/90-100112.pdf',
 'maxm86146-package.pdf':'https://www.analog.com/media/en/package-pcb-resources/package-outline-drawings/21-100323.pdf',
 'max14689-lands.pdf':'https://www.analog.com/media/en/package-pcb-resources/pcb-footprints/90-0616.pdf',
 'eda-format.html':'https://dev-docs.kicad.org/en/import-formats/easyeda/index.html',
 'adi-eval.html':'https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/maxm86146evsys.html',
}
def fetch(item):
 name,url=item;p=dst/name
 try:
  if not p.exists():
   req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0'})
   with urllib.request.urlopen(req,timeout=35) as r:data=r.read()
   if name.endswith('.pdf') and not data.startswith(b'%PDF'):raise ValueError('Not a PDF')
   p.write_bytes(data)
  if name.endswith('.pdf'):
   r=PdfReader(p);texts=[page.extract_text() or '' for page in r.pages]
   p.with_suffix('.txt').write_text('\n\f\n'.join(texts),encoding='utf8')
   pages=[i+1 for i,t in enumerate(texts) if any(s in t for s in ['LAND PATTERN','EXAMPLE BOARD','Recommended Solder','Package Outline','Dimensions','PACKAGE OUTLINE','Land Pattern'])]
   return {'file':name,'url':url,'pages':len(texts),'figure_pages':pages}
  return {'file':name,'url':url,'bytes':p.stat().st_size}
 except Exception as e:return {'file':name,'url':url,'error':str(e)}
res=list(concurrent.futures.ThreadPoolExecutor(max_workers=6).map(fetch,sources.items()))
(dst/'downloads.json').write_text(json.dumps(res,indent=2),encoding='utf8')
for r in res: print(json.dumps(r))
for name in ['adi-eval.html','eda-format.html']:
 p=dst/name
 if p.exists():
  txt=p.read_text(encoding='utf8')
  if name=='adi-eval.html':print('EVAL ZIP LINKS',re.findall(r'href="([^"]+\.zip[^\"]*)"',txt,re.I))
  else:
   i=txt.find('NO_CONNECT');print('NO CONNECT FORMAT',re.sub('<[^>]+>',' ',txt[max(0,i-800):i+1800]))
