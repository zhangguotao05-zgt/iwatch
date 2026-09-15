from pathlib import Path
from pypdf import PdfReader
import pypdfium2 as pdfium
base=Path(__file__).resolve().parent
old=base.parent/'schematic-v0.1/references'
new=base/'references';qa=base/'reference-qa';qa.mkdir(exist_ok=True)
for name in ['bq24074','tps62840','tps63031','drv2605l','lsm6dso','tps61099','sfh7015','ctdblp31']:
 p=(old if (old/(name+'.pdf')).exists() else new)/(name+'.pdf')
 r=PdfReader(p);d=pdfium.PdfDocument(p)
 for i,page in enumerate(r.pages):
  txt=page.extract_text() or ''
  take='EXAMPLE BOARD LAYOUT' in txt or ('Recommended' in txt and ('land' in txt.lower() or 'Solder' in txt))
  take=take or (name=='lsm6dso' and 'LGA-14' in txt and ('mechanical data' in txt or 'dimensions' in txt))
  take=take or (name=='sfh7015' and ('Dimension' in txt or 'Package outline' in txt or 'Soldering pad' in txt))
  take=take or (name=='ctdblp31' and i in [10,11])
  if take:
   f=qa/(name+'-p'+str(i+1)+'.png');d[i].render(scale=1.5).to_pil().save(f)
   print(name,i+1,f)
