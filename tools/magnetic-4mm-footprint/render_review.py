"""Render a review sheet from the actual serialized .efoo pad geometry.

This is an independent PDF preview, not an EasyEDA editor screenshot or DRC.
"""
from pathlib import Path
import json, math, zipfile
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.pagesizes import A4, landscape
import pypdfium2 as pdfium

BASE = Path(__file__).resolve().parent
C = json.loads((BASE / 'config.json').read_text(encoding='utf-8'))
OUT = Path(C['output_directory'])
rows = [json.loads(l) for l in (OUT / (C['name']+'.efoo')).read_text(encoding='utf-8').splitlines()]
checks=json.loads((OUT/'校核记录.json').read_text(encoding='utf-8'))
pdfmetrics.registerFont(TTFont('YaHei', 'C:/Windows/Fonts/msyh.ttc', subfontIndex=0))
pdfmetrics.registerFont(TTFont('YaHei-Bold', 'C:/Windows/Fonts/msyhbd.ttc', subfontIndex=0))
W,H=landscape(A4)
pdf_path=OUT/'4mm磁吸母头_封装尺寸预览.pdf'
c=canvas.Canvas(str(pdf_path),pagesize=(W,H))
c.setTitle('4mm磁吸母头 PCB封装草稿 R01')
c.setAuthor('HealthWatch project')
NAVY='#14243B'; MUTED='#536477'; BORDER='#D8E1EB'; TEAL='#078B83'; AMBER='#956216'


def txt(x,y,text,size=10,color=NAVY,bold=False,align='left'):
    c.setFillColor(HexColor(color));c.setFont('YaHei-Bold' if bold else 'YaHei',size)
    fn=c.drawCentredString if align=='center' else c.drawRightString if align=='right' else c.drawString
    fn(x,y,str(text))


def ln(x1,y1,x2,y2,color=BORDER,width=.7,dash=None):
    c.saveState();c.setStrokeColor(HexColor(color));c.setLineWidth(width)
    if dash:c.setDash(*dash)
    c.line(x1,y1,x2,y2);c.restoreState()


def box(x,y,w,h,color,r=8):
    c.setFillColor(HexColor(color));c.roundRect(x,y,w,h,r,stroke=0,fill=1)


def dim(x1,x2,y,label,base_y):
    ln(x1,base_y,x1,y-5,MUTED,.5);ln(x2,base_y,x2,y-5,MUTED,.5)
    ln(x1,y,x2,y,MUTED,.6)
    for x,sgn in [(x1,1),(x2,-1)]:
        p=c.beginPath();p.moveTo(x,y);p.lineTo(x+sgn*5,y+1.8);p.lineTo(x+sgn*5,y-1.8);p.close()
        c.setFillColor(HexColor(MUTED));c.drawPath(p,stroke=0,fill=1)
    txt((x1+x2)/2,y+6,label,9,MUTED,align='center')


def render_native(cx,cy,scale):
    def xy(x,y): return cx+x*.0254*scale,cy+y*.0254*scale
    # True-size reference courtyard and assembly edges, deliberately outside copper.
    for r in rows:
        if r[0]!='POLY' or r[4] not in (9,71):continue
        c.saveState();c.setStrokeColor(HexColor('#8C9CAA' if r[4]==9 else '#73B6C1'))
        c.setLineWidth(.65);c.setDash(3,3)
        p=r[6]
        if p[0]=='CIRCLE':
            x,y=xy(p[1],p[2]);c.circle(x,y,p[3]*.0254*scale,fill=0,stroke=1)
        else:
            q=c.beginPath();q.moveTo(*xy(p[0],p[1]))
            for i in range(3,len(p),2):q.lineTo(*xy(p[i],p[i+1]))
            c.drawPath(q,fill=0,stroke=1)
        c.restoreState()
    for r in rows:
        if r[0]!='PAD':continue
        x,y=xy(r[6],r[7])
        c.saveState();c.translate(x,y);c.rotate(r[8])
        p=r[10];w,h=p[1]*.0254*scale,p[2]*.0254*scale
        c.setFillColor(HexColor('#D5AD54'));c.setStrokeColor(HexColor('#967326'));c.setLineWidth(.6)
        if p[0]=='ELLIPSE':c.ellipse(-w/2,-h/2,w/2,h/2,stroke=1,fill=1)
        else:c.roundRect(-w/2,-h/2,w,h,min(w,h)/2,stroke=1,fill=1)
        c.rotate(r[14]);p=r[9];w,h=p[1]*.0254*scale,p[2]*.0254*scale
        c.setFillColor(HexColor('#FFFFFF'));c.setStrokeColor(HexColor('#7A6744'));c.setLineWidth(.6)
        if p[0]=='ROUND':c.circle(0,0,w/2,stroke=1,fill=1)
        else:c.roundRect(-w/2,-h/2,w,h,min(w,h)/2,stroke=1,fill=1)
        c.restoreState()
    # Physical pin sections are reference-only, reconstructed from document paths.
    for r in rows:
        if r[0]!='POLY' or r[4]!=13:continue
        p=r[6];c.saveState();c.setStrokeColor(HexColor('#90A3B0'));c.setLineWidth(.6)
        if p[0]=='CIRCLE':
            x,y=xy(p[1],p[2]);c.circle(x,y,p[3]*.0254*scale,stroke=1,fill=0)
        else:
            q=c.beginPath();q.moveTo(*xy(p[0],p[1]))
            for i in range(3,len(p),2):q.lineTo(*xy(p[i],p[i+1]))
            c.drawPath(q,stroke=1,fill=0)
        c.restoreState()
    for r in rows:
        if r[0]=='POLY' and r[4]==3 and r[6][0]=='CIRCLE':
            c.setStrokeColor(HexColor(TEAL));c.setLineWidth(r[5]*.0254*scale)
            c.circle(cx,cy,r[6][3]*.0254*scale,stroke=1,fill=0)
    for number,x in [(2,-1.7),(1,0),(3,1.7)]:
        tx,ty=cx+x*scale,cy+1.58*scale
        box(tx-10,ty-4,20,20,'#FFFFFF',4);txt(tx,ty+1,number,12,NAVY,True,'center')
    txt(cx,cy+3.72*scale,'J?',13,NAVY,True,'center')
    ln(cx-4,cy,cx+4,cy,MUTED,.5);ln(cx,cy-4,cx,cy+4,MUTED,.5)


c.setFillColor(HexColor('#FFFFFF'));c.rect(0,0,W,H,fill=1,stroke=0)
txt(30,H-39,'4mm 磁吸母头 · PCB 封装',23,bold=True)
txt(31,H-61,'嘉立创 EDA 专业版  |  R01  |  ' + C['name'],9,MUTED)
box(W-164,H-52,132,28,'#FFF2D9',6)
txt(W-98,H-43,'草稿 · 待实物复核',10,AMBER,True,'center')
ln(30,H-78,W-30,H-78)
txt(40,494,'PCB 顶视图',12,bold=True)
txt(391,494,'单位：mm',9,MUTED,align='right')
render_native(218,320,40)
dim(218-1.7*40,218+1.7*40,171,'3.40  槽孔中心距（暂定）',199)
txt(218,151,'原点 = 中心针；侧槽长边沿 Y 方向',9,MUTED,align='center')
ln(86,128,106,128,'#D5AD54',6);txt(112,124,'铜焊盘',9,MUTED)
ln(182,128,202,128,TEAL,3);txt(208,124,'丝印 Ø5.60',9,MUTED)
ln(303,128,324,128,'#8C9CAA',.7,(3,2));txt(330,124,'参考轮廓',9,MUTED)
txt(218,105,'浅灰脚形为推算截面；虚线占位框 6.20 × 6.20',9,MUTED,align='center')
ln(424,105,424,498,BORDER)

txt(445,494,'焊盘与孔位',12,bold=True)
xs=[445,494,589,686]
box(440,457,371,24,'#EDF3F8',4)
for x,label in zip(xs,['编号','中心 (X, Y)','铜尺寸 X × Y','孔尺寸 X × Y']):txt(x+4,465,label,8.5,MUTED,True)
table=[['1','(0, 0)','Ø1.40','Ø0.80'],['2','(-1.70, 0)','1.50 × 2.50','0.90 × 1.90'],['3','(+1.70, 0)','1.50 × 2.50','0.90 × 1.90']]
for i,data in enumerate(table):
    y=439-i*29
    for x,value in zip(xs,data):txt(x+4,y,value,9)
    ln(444,y-10,808,y-10)
txt(445,348,'1 为镀铜圆孔；2、3 为镀铜长圆槽孔。',9,MUTED)

txt(445,318,'图纸直接标注',11,bold=True)
txt(445,298,'中心针 Ø0.50；侧脚宽 1.25；侧脚外缘 R2。',9.5)
txt(445,280,'本体高 3.50；出脚长 2.00；上台阶 Ø4.00。',9.5)

box(438,196,374,65,'#FFF7E8',6)
txt(449,241,'推算尺寸与设计取值',10,AMBER,True)
txt(449,223,'底座外径约 Ø5.00；侧脚径向壁厚约 0.50。',9,AMBER)
txt(449,207,'槽孔中心距 3.40 为设计取值，须以实物试装确认。',9,AMBER)

txt(445,173,'编号仅对应物理位置',10,bold=True)
txt(445,155,'1 = 中心，2 = 左侧，3 = 右侧。',9.5)
txt(445,138,'图纸未说明极性及侧脚是否共通，未预设网络。',9.5)
txt(445,111,'焊环 ≥0.30；铜间距 0.25；阻焊桥 0.15。',9,MUTED)

ln(30,88,W-30,88)
txt(31,67,'已校核文件结构、旋转后的孔尺寸及几何间距。尚未在 EDA 内导入验证或进行实物试装。',9,MUTED)
txt(31,49,'来源：用户提供的《4mm磁吸母头(插件） (1).PDF》；图纸未提供安装尺寸公差。',9,MUTED)
txt(W-31,30,'2026-09-11  /  1',8,MUTED,align='right')
c.save()
doc=pdfium.PdfDocument(str(pdf_path))
for i,page in enumerate(doc):page.render(scale=2.5).to_pil().save(OUT / '4mm磁吸母头_封装预览.png')

readme='''# 4mm 磁吸母头 PCB 封装草稿 R01

已按用户提供的机械 PDF 制作嘉立创 EDA 专业版封装草稿。所有表格和设计参数的单位为 mm。
本文件为可编辑草稿：侧脚厚度和底座外径含图形比例推算，槽孔中心距为设计取值；用户已确认先按图纸做草稿。

## 导入与使用

1. 在嘉立创 EDA 专业版选择 **文件 → 导入 → 嘉立创EDA（专业版）**。
2. 首选导入 `4mm磁吸母头_嘉立创专业版_封装草稿.epro`。它含一个放置了该封装的 PCB 预览页，未画生产板框。
3. 导入时可提取封装库；也可打开工程后，在工程库中找到 `CON-MAGNETIC-F-4MM-TH_DRAFT_R01` 并另存到自己的封装库。
4. 同目录的 `.efoo` 是独立封装源码；支持独立封装文件的导入界面可直接选择它。
5. 后续在器件编辑器绑定封装，按实际电气连接映射焊盘编号。

导入流程依据：[嘉立创 EDA 专业版官方导入说明](https://prodocs.lceda.cn/cn/import-export/import-easyeda-pro/)。
本次完成了本地文件结构与几何校核，未声称已在 EDA 编辑器里导入成功或通过原生 DRC。

## 焊盘定义

PCB 顶视图，原点为中心针，X 向右，Y 向上。尺寸均为旋转后的实际 X/Y 尺寸。

| 编号 | 位置 | 中心 X | 中心 Y | 铜尺寸 X × Y | 孔尺寸 X × Y | 孔型 |
|---|---|---:|---:|---|---|---|
| 1 | 中心针 | 0 | 0 | Ø1.40 | Ø0.80 | 镀铜圆孔 |
| 2 | 左侧脚 | -1.70 | 0 | 1.50 × 2.50 | 0.90 × 1.90 | 镀铜长圆槽孔 |
| 3 | 右侧脚 | +1.70 | 0 | 1.50 × 2.50 | 0.90 × 1.90 | 镀铜长圆槽孔 |

槽孔尺寸为两端外缘之间的完整尺寸，不是铣刀移动行程。原生源码用 `SLOT` + `OVAL` 并旋转 90° 表示竖向槽；三个焊盘均为 Multi-Layer，孔壁镀铜。
所列孔径是封装中的设计孔径，实际成品孔及公差需和板厂确认。
本图没有电气定义：1/2/3 是本草稿的物理编号，未指定正负极，也未把两侧脚强制设成同一个编号或网络。确认两侧共通后，可在原理图接至同一网络并核对符号映射。

## 图纸标注与推算依据

图纸直接标注：上台阶 Ø4.00、接触端孔 Ø1.10、本体高 3.50、台阶高 1.30、出脚长 2.00、中心针 Ø0.50、侧脚宽 1.25、侧脚外圆弧 R2。

图形比例推算：以已标注的 Ø4.00、Ø0.50 及 1.25 为比例交叉参照，底座直径约 Ø5.00，侧脚内圆弧半径约 1.50，径向壁厚约 0.50。R2 标注的箭头尖落在侧脚外弧，不是最外侧底座圆。

侧脚是圆弧截面，不能简单把 0.50 × 1.25 矩形当作完整脚形。按推算的内 R1.50 / 外 R2.00、Y=±0.625 截取，右侧脚 X 包络约为 1.364～2.000。因此槽孔中心暂取 X=±1.70，孔宽/长取 0.90 × 1.90，给圆弧脚形留出插入空间。
**3.40 mm 是所设计两槽孔的中心距，不是厂家图纸直接标注的侧脚中心距。**

按这一推算截面采样，脚形到槽孔边界的名义最小余量约 0.091 mm；该结果不含器件、孔径、位置或装配公差，不能替代实物试装。

## 图层与校核

- 多层：三个镀铜通孔焊盘。顶/底阻焊各外扩 0.05；未添加自定义钢网图形。专业版中通孔焊盘的助焊扩展设置不生效，见[官方 API 说明](https://prodocs.lceda.cn/cn/api/reference/pro-api.ipcb_primitivesoldermaskandpastemaskexpansion.html)。
- 顶层丝印：Ø5.60，线宽 0.15，位号 `J?`。
- 顶层装配层：本体约 Ø5.00 / 上台阶 Ø4.00 的参考轮廓。
- 文档层：中心针、推算侧脚截面、脚号及 `DRAFT - VERIFY FIT` 标记。
- 自定义 `Courtyard_REF` 层：6.20 × 6.20 参考占位框。它是参考线框，不是电气禁布区，也不保证自动参加 DRC。

计算检查：最小焊环 0.30，异焊盘铜间距 0.25，阻焊桥 0.15，丝印到阻焊开窗最小间距约 0.153。
已核对三焊盘唯一编号、镀铜属性、槽孔旋转、mil/mm 换算、压缩工程完整性及工程内封装一致性。未完成 EDA 原生导入/DRC 或实物试装。

## 下一步复核

制作正式 PCB 前，测量底座外径、两侧脚截面/位置，试装孔位，并测通确认中心针和两侧脚之间的电气关系。
如参数改变，可编辑 `封装参数.json`；生成脚本使用工作目录 `tools/magnetic-4mm-footprint/config.json`，同步相应值后运行 `build_footprint.py`、`render_review.py` 重新生成。
详细数值见 `焊盘尺寸_mm.csv` 与 `校核记录.json`。预览 PDF/PNG 由同一个原生封装文件独立渲染，不是 EDA 截图。
'''
(OUT/'使用说明.md').write_text(readme,encoding='utf-8')
bundle=OUT/'4mm磁吸母头_封装草稿_R01.zip'
with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(OUT.iterdir()):
        if p.is_file() and p!=bundle:z.write(p,p.name)
print('REVIEW',pdf_path)
print('BUNDLE',bundle)
