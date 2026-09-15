from pathlib import Path
import urllib.request,json,zipfile,concurrent.futures
dst=Path(__file__).resolve().parent/'references'
url='https://www.analog.com/media/en/evaluation-documentation/evaluation-design-files/maxm86146_gerbers.zip'
p=dst/'maxm86146_gerbers.zip'
if not p.exists() or not zipfile.is_zipfile(p):
 req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0','Range':'bytes=0-1048575'})
 with urllib.request.urlopen(req,timeout=45) as r:
  data=r.read(1024*1024*2)
  total=int(r.headers.get('Content-Range','/'+str(len(data))).split('/')[-1])
  print('SIZE',total,flush=True)
 def piece(start):
  req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0','Range':f'bytes={start}-{min(total-1,start+1024*1024-1)}'})
  with urllib.request.urlopen(req,timeout=45) as r:
   chunk=r.read(1024*1024)
   if r.status!=206:raise ValueError('Expected range response')
  return chunk
 data+=b''.join(concurrent.futures.ThreadPoolExecutor(max_workers=5).map(piece,range(len(data),total,1024*1024)))
 assert len(data)==total
 p.write_bytes(data)
z=zipfile.ZipFile(p)
print('\n'.join(z.namelist()))
# Original public hardware design files only. Never execute downloaded programs.
for n in z.namelist():
 if n.lower().endswith(('.asc','.txt','.rep','.csv','.dxf','.gbr','.art','.pdf','.net','.ipc','.cad','.brd','.pcb','.sch','.dsn')):
  target=dst/'adi-design'/n
  if not target.resolve().is_relative_to((dst/'adi-design').resolve()):raise ValueError('Unsafe archive path')
  target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(n))
