from pathlib import Path
import zipfile,io,tarfile
dst=Path(__file__).resolve().parent/'references/adi-design'
outer=zipfile.ZipFile(dst.parent/'maxm86146_gerbers.zip')
for n in outer.namelist():
 if n.endswith('.zip'):
  z=zipfile.ZipFile(io.BytesIO(outer.read(n)))
  for f in z.namelist():
   if f.endswith('.tgz'):
    t=tarfile.open(fileobj=io.BytesIO(z.read(f)),mode='r:gz')
    for member in t.getmembers():
     if member.isfile():
      p=(dst/'odb'/member.name).resolve()
      if not p.is_relative_to((dst/'odb').resolve()):raise ValueError('Unsafe path')
      p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(t.extractfile(member).read())
   else:
    p=(dst/f).resolve()
    if not p.is_relative_to(dst.resolve()):raise ValueError('Unsafe path')
    p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(f))
print('\n'.join(str(p.relative_to(dst)) for p in dst.rglob('*') if p.is_file())[:10000])
