"""Render the same native project geometry to a vector PDF for review.
This is a local rendering, not a claimed EDA PDF export or ERC result.
"""
from pathlib import Path
import zipfile,json
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import A2,landscape
from reportlab.lib.colors import HexColor
import pypdfium2 as pdfium
BASE=Path(__file__).resolve().parent
OUT=BASE.parents[1]/'hardware'/'原理图_v0.1'
model=json.loads((OUT/'circuit-model.json').read_text(encoding='utf8'))
z=zipfile.ZipFile(OUT/(model['title']+'.epro'))
proj=json.loads(z.read('project.json'))
bytitle={v['title']:v for v in proj['devices'].values()}
out=OUT/'HealthWatch-SF58-v0.1-原理图审阅.pdf'
pw,ph=landscape(A2);scale=min(pw/2400,ph/1650)
c=canvas.Canvas(str(out),pagesize=(pw,ph))
c.setTitle('HealthWatch SF58 - Offline schematic review v0.1')
c.setAuthor('HealthWatch project')
def linepts(pts,color='#334155',width=1):
    c.setStrokeColor(HexColor(color));c.setLineWidth(width)
    p=c.beginPath();p.moveTo(pts[0],pts[1])
    for i in range(2,len(pts),2):p.lineTo(pts[i],pts[i+1])
    c.drawPath(p)
def txt(x,y,t,size=11,align='L',bold=False,color='#172337'):
    c.setFillColor(HexColor(color));c.setFont('Helvetica-Bold' if bold else 'Helvetica',size)
    if align=='R':c.drawRightString(x,y-3,t)
    else:c.drawString(x,y-3,t)
for s in model['sheets']:
    c.saveState();c.translate(0,(ph-1650*scale)/2);c.scale(scale,scale)
    for item in s['draw']:
        if item[0]=='text':
            _,x,y,t,st=item;txt(x,y,t,26 if st=='title' else 14 if st=='text' else 11,bold=st=='title')
        elif item[0]=='poly':linepts(item[1])
        elif item[0]=='wire':linepts(list(item[1:]),'#15724c',1.2)
        elif item[0]=='net':
            _,x,y,t,side=item;d=-1 if side=='L' else 1
            linepts([x,y,x+5*d,y+5,x+15*d,y+5,x+20*d,y,x+15*d,y-5,x+5*d,y-5,x,y],'#15724c')
            txt(x-28 if side=='L' else x+28,y,t,11,'R' if side=='L' else 'L',color='#15724c')
        elif item[0]=='component':
            _,ref,title,sym,x,y,value=item
            txt(x,y+35,ref,14,bold=True);txt(x,y+17,value,11)
            c.saveState();c.translate(x,y)
            sid=bytitle[title]['attributes']['Symbol'];rows=[json.loads(l) for l in z.read('SYMBOL/'+sid+'.esym').decode().splitlines() if l.strip()]
            if sym['passive']:
                for r in rows:
                    if r[0]=='POLY':linepts(r[2],'#8a2525')
            if not sym['passive']:linepts([0,0,sym['width'],0,sym['width'],-sym['height'],0,-sym['height'],0,0],'#8a2525')
            for num,p in sym['pins'].items():
                px,py=p['x'],p['y'];left=p['side']=='L';length=10 if sym['passive'] else 30
                linepts([px,py,px+(length if left else -length),py],'#8a2525')
                if not sym['passive']:
                    txt(5 if left else sym['width']-5,py,p['name'],11,'L' if left else 'R')
                    txt(px+15 if left else px-15,py+10,num,9,'R' if left else 'L',color='#8a2525')
            c.restoreState()
    c.restoreState();c.showPage()
c.save()
qa=BASE/'qa';qa.mkdir(exist_ok=True)
doc=pdfium.PdfDocument(str(out))
for i,p in enumerate(doc):p.render(scale=1).to_pil().save(qa/f'page-{i+1:02}.png')
print(out)
print('Rendered',len(doc),'pages to',qa)
