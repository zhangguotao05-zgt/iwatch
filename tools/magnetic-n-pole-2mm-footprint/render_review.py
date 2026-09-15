"""Render the actual native footprint copper and custom paste to a review PDF."""
from pathlib import Path
import json,math,zipfile
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.pagesizes import A4,landscape
import pypdfium2 as pdfium

BASE=Path(__file__).resolve().parent
C=json.loads((BASE/'config.json').read_text(encoding='utf-8'));D=C['design_mm'];O=C['documented_mm']
OUT=Path(C['output_directory']);rows=[json.loads(l) for l in (OUT/(C['name']+'.efoo')).read_text(encoding='utf-8').splitlines()]
pdfmetrics.registerFont(TTFont('YaHei','C:/Windows/Fonts/msyh.ttc',subfontIndex=0))
pdfmetrics.registerFont(TTFont('YaHei-Bold','C:/Windows/Fonts/msyhbd.ttc',subfontIndex=0))
W,H=landscape(A4);pdf=OUT/'SJ056_N极母座_封装尺寸预览.pdf'
c=canvas.Canvas(str(pdf),pagesize=(W,H));c.setTitle('SJ-056 N极母座2.0 PCB封装草稿 R01');c.setAuthor('HealthWatch project')
NAVY='#14243B';MUTED='#536477';BORDER='#D8E1EB';TEAL='#078B83';GOLD='#D5AD54';PASTE='#7864B2'

def txt(x,y,t,size=10,color=NAVY,bold=False,align='left'):
    c.setFillColor(HexColor(color));c.setFont('YaHei-Bold' if bold else 'YaHei',size)
    fn=c.drawCentredString if align=='center' else c.drawRightString if align=='right' else c.drawString
    fn(x,y,str(t))
def ln(x1,y1,x2,y2,color=BORDER,width=.7,dash=None):
    c.saveState();c.setStrokeColor(HexColor(color));c.setLineWidth(width)
    if dash:c.setDash(*dash)
    c.line(x1,y1,x2,y2);c.restoreState()
def box(x,y,w,h,color,r=6):
    c.setFillColor(HexColor(color));c.roundRect(x,y,w,h,r,stroke=0,fill=1)

def path_from_native(path,cx,cy,scale,offset=(0,0)):
    def xy(x,y):return cx+(x*.0254+offset[0])*scale,cy+(y*.0254+offset[1])*scale
    point=xy(path[0],path[1]);p=c.beginPath();p.moveTo(*point);i=2;mode=None
    while i<len(path):
        if isinstance(path[i],str):mode=path[i];i+=1
        if mode=='L':point=xy(path[i],path[i+1]);p.lineTo(*point);i+=2
        elif mode=='ARC':
            angle=path[i];end=xy(path[i+1],path[i+2]);i+=3
            dx=end[0]-point[0];dy=end[1]-point[1];factor=.5/math.tan(math.radians(angle)/2)
            center=((point[0]+end[0])/2-dy*factor,(point[1]+end[1])/2+dx*factor)
            radius=math.hypot(point[0]-center[0],point[1]-center[1]);start=math.degrees(math.atan2(point[1]-center[1],point[0]-center[0]))
            p.arcTo(center[0]-radius,center[1]-radius,center[0]+radius,center[1]+radius,start,angle);point=end
        else:raise ValueError(mode)
    p.close();return p

def draw_copper(cx,cy,scale,refs=True,labels=True):
    c.setFillColor(HexColor(GOLD))
    for r in rows:
        if r[0]!='PAD':continue
        shape=r[10]
        if shape[0]=='ELLIPSE':
            c.circle(cx+r[6]*.0254*scale,cy+r[7]*.0254*scale,shape[1]*.0254*scale/2,stroke=0,fill=1)
        else:c.drawPath(path_from_native(shape[1],cx,cy,scale,(r[6]*.0254,r[7]*.0254)),stroke=0,fill=1)
    if refs:
        for r in rows:
            if r[0]!='POLY' or r[4] not in (3,9,71):continue
            c.saveState();c.setStrokeColor(HexColor(TEAL if r[4]==3 else '#94A8B8'))
            c.setLineWidth(r[5]*.0254*scale if r[4]==3 else .7)
            if r[4]!=3:c.setDash(3,3)
            if r[6][0]=='CIRCLE':c.circle(cx,cy,r[6][3]*.0254*scale,stroke=1,fill=0)
            else:c.drawPath(path_from_native(r[6],cx,cy,scale),stroke=1,fill=0)
            c.restoreState()
    if labels:
        txt(cx,cy-5,'1  +',13,NAVY,True,'center')
        txt(cx,cy+1.5*scale-5,'2  −',13,NAVY,True,'center')
        txt(cx,cy+3.1*scale,'J?',13,NAVY,True,'center')

def draw_paste(cx,cy,scale):
    c.setFillColor(HexColor(PASTE))
    for r in rows:
        if r[0]=='FILL' and r[4]==7:
            for path in r[7]:c.drawPath(path_from_native(path,cx,cy,scale),stroke=0,fill=1)

def dim(x1,x2,y,label,top):
    for x in (x1,x2):ln(x,top,x,y-5,MUTED,.5)
    ln(x1,y,x2,y,MUTED,.5)
    for x,sgn in ((x1,1),(x2,-1)):
        p=c.beginPath();p.moveTo(x,y);p.lineTo(x+sgn*5,y+1.8);p.lineTo(x+sgn*5,y-1.8);p.close()
        c.setFillColor(HexColor(MUTED));c.drawPath(p,stroke=0,fill=1)
    txt((x1+x2)/2,y+6,label,9,MUTED,align='center')

txt(30,H-39,'SJ-056 · N极母座 2.0',23,bold=True)
txt(31,H-61,'嘉立创 EDA 专业版  |  R01  |  Ø4.00 × H2.00 mm  |  同心圆贴片封装',9.5,MUTED)
box(W-165,H-52,134,28,'#FFF2D9');txt(W-98,H-43,'草稿 · 待装配验证',10,'#956216',True,'center')
ln(30,H-78,W-30,H-78)
txt(40,494,'PCB 顶视图 · 顶层铜',12,bold=True);txt(390,494,'单位：mm',9,MUTED,align='right')
draw_copper(216,327,49)
dim(216-D['negative_copper_outer_diameter']/2*49,216+D['negative_copper_outer_diameter']/2*49,178,'外环铜外径 Ø3.90',202)
txt(216,154,'中心正极 Ø1.40 · 外环铜内径 Ø2.10',10,MUTED,align='center')
ln(72,130,92,130,GOLD,6);txt(99,126,'铜焊盘',9,MUTED)
ln(163,130,183,130,TEAL,3);txt(190,126,'丝印 Ø4.50',9,MUTED)
ln(293,130,313,130,'#94A8B8',.7,(3,2));txt(320,126,'本体 / 占位',9,MUTED)
txt(216,105,'原点 = 同心圆中心；参考占位框 5.00 × 5.00',9,MUTED,align='center')
ln(424,103,424,498)

txt(445,494,'图纸与 PCB 设计尺寸',12,bold=True)
box(439,454,373,25,'#EDF3F8',4)
for x,label in [(448,'项目'),(584,'图纸接触面'),(700,'PCB 铜尺寸')]:txt(x,463,label,9,MUTED,True)
data=[('中心正极外径','Ø1.30','Ø1.40'),('负极环外径','Ø3.70','Ø3.90'),('负极环内径','Ø2.30','Ø2.10')]
for i,row in enumerate(data):
    y=436-i*29
    for x,value in zip([448,584,700],row):txt(x,y,value,10)
    ln(445,y-10,810,y-10)
txt(445,349,'PCB 为表面贴装，无钻孔、无预放过孔。',9.5,MUTED)

box(438,262,374,68,'#EDF7F5')
txt(449,311,'1 脚：中心正极     2 脚：连续外环负极',10,TEAL,True)
txt(449,292,'Ø0.50 是器件自身孔，未作为 PCB 钻孔。',9.5)
txt(449,275,'“N 极”描述磁极；电气正负按图纸箭头确定。',9.5)

draw_paste(489,200,25)
txt(551,235,'顶层钢网 · 分段开窗草案',10,bold=True)
txt(551,216,'中心 4 窗：外 Ø1.30 / 内 Ø0.60',9)
txt(551,198,'外环 4 窗：外 Ø3.60 / 内 Ø2.40',9)
txt(551,180,'留连接桥；中心避开器件小孔。',9,MUTED)
txt(551,162,'开窗量需结合钢网厚度与试焊确认。',8.5,MUTED)
txt(445,134,'正负铜间距 0.35；最小阻焊桥 0.25。',9,MUTED)
txt(445,112,'中心正极需换层引出，布局时安排相应过孔工艺。',9,NAVY,True)

ln(30,88,W-30,88)
txt(31,67,'已校核圆环连续性、中心隔离、钢网开窗及文件结构。尚未在 EDA 内验证导入或进行实物试焊。',9,MUTED)
txt(31,49,'来源：用户提供的《N极母座2.0.PDF》，SJ-056。PCB 铜、阻焊及钢网尺寸为本次设计取值。',9,MUTED)
txt(W-31,30,'2026-09-11  /  1',8,MUTED,align='right')
c.save()
doc=pdfium.PdfDocument(str(pdf))
doc[0].render(scale=2.5).to_pil().save(OUT/'SJ056_N极母座_封装预览.png')

readme='''# SJ-056 N极母座2.0 — 嘉立创 EDA 专业版封装草稿

依据用户提供的《N极母座2.0.PDF》制作。型号 SJ-056，本体直径 4.00 mm、高度 2.00 mm；这是同心圆表面贴装母座。所有尺寸单位为 mm。

## 导入

在嘉立创 EDA 专业版选择 **文件 → 导入 → 嘉立创EDA（专业版）**，导入 `SJ056_N极母座_嘉立创专业版_封装草稿.epro`。
工程中含一个放置了该封装的 PCB 预览页。导入时可提取封装库，也可在工程库找到 `CON-MAGNETIC-SJ056-N-D4-H2-SMD_DRAFT_R01`，另存至自己的封装库后绑定器件。
同目录 `.efoo` 为独立封装源码，支持独立封装导入的界面可直接选择它。

流程依据：[专业版官方导入说明](https://prodocs.lceda.cn/cn/import-export/import-easyeda-pro/)。本次未实际启动 EDA 完成导入和原生 DRC，不能把本地几何检查视为导入成功记录。

## 电气与焊盘

图纸的“贴片焊盘正极”箭头指向中心接触面，“贴片焊盘负极”箭头指向外侧圆环。封装原点是同心圆中心。

| 焊盘号 | 电气定义 | PCB 铜形状 | 铜外径 | 铜内径 | PCB 孔 |
|---|---|---|---:|---:|---|
| 1 | 正极 | 中心实心圆盘 | 1.40 | 0 | 无 |
| 2 | 负极 | 连续同心圆环 | 3.90 | 2.10 | 无 |

均在顶层。负极圆环用两个微量重叠的半圆环异形焊盘组成，两个图元都编号 **2**，铜形状合并后为连续圆环；不能把这两个半环独立重编号。两个半环的连接锚点位于各自铜区，避免将负极连接锚点放进中心正极。
“N极”是磁极标识，不是电气负极的依据；电气极性按本图的明确标注映射。

**图纸上的 Ø0.50 是器件自身孔，不是 PCB 安装孔。** 本封装未放置任何钻孔或过孔；中心 PCB 铜为实心圆盘，Ø0.50 只作为文档层参考轮廓。

## 图纸尺寸与设计取值

| 项目 | 图纸直接标注 | 本次 PCB 设计 |
|---|---|---|
| 中心正极接触面 | 外 Ø1.30，孔 Ø0.50 | 铜 Ø1.40，无 PCB 孔 |
| 外环负极接触面 | 外 Ø3.70，内 Ø2.30 | 铜外 Ø3.90，内 Ø2.10 |
| 本体 | 外 Ø4.00，高 2.00 | 装配层 Ø4.00 参考轮廓 |
| 正面配合结构 | Ø2.20 / Ø1.00 | 非 PCB 焊盘尺寸 |

原图 R1.85 / R1.15 分别与 Ø3.70 / Ø2.30 一致。本次没有使用上一款插件母头的脚距、长圆孔或未知极性设置。
铜盘相对中心接触面径向加宽 0.05；负极环内外边缘各向接触面外扩 0.10。它们是为编辑和装配评审提出的设计值，并不是供应商给出的推荐 PCB land pattern。

## 阻焊与钢网

- 顶层阻焊各边外扩 0.05：中心开窗 Ø1.50，负极环开窗约外 Ø4.00 / 内 Ø2.00。
- 正负铜间距 0.35；最小阻焊桥 0.25。
- 取消焊盘自动助焊开窗，使用顶层助焊层的独立填充图形。
- 中心正极分 4 个环形扇区开窗，外 Ø1.30 / 内 Ø0.60，每窗 70°，最窄连接桥约 0.104。
- 外环负极分 4 个环形扇区开窗，外 Ø3.60 / 内 Ø2.40，每窗 82°，最窄连接桥约 0.167。

分段让钢网中央区域保留连接桥，中心开窗避开器件 Ø0.50 孔；钢网孔不是 PCB 钻孔。
钢网厚度和回流工艺未设定，开窗是初始草案，需结合试焊和供应商工艺调整。图纸仅提到 PA46 胶料可过回炉，未提供整件（含磁铁）的回流温度曲线。
异形焊盘、圆环及自定义锡膏层设置参考[专业版官方 PCB 说明](https://prodocs.lceda.cn/cn/faq/pcb/)。

## 布局注意与验证

完整负极环包围中心正极，中心线不能在顶层跨过负极铜环。**在 PCB 布局阶段为 1 脚安排换层引出**；本封装不预设板层和过孔。若采用盘中孔，需配合填孔/盖帽等具体板厂工艺，避免把普通敞口过孔直接当作稳定的贴片焊接面。
专业版的盘中孔工艺说明见[官方扇出布线文档](https://prodocs.lceda.cn/cn/pcb/route-fanout-routing/index.html)。

顶层丝印 Ø4.50，线宽 0.12，位号 `J?`；丝印与阻焊开窗的最小间距为 0.19。
`Courtyard_REF` 自定义层有 5.00 × 5.00 参考占位框，这是图形参考框，不是自动电气禁布区。
文档层保留图纸接触面、器件小孔轮廓、极性和草稿标记，装配层保留本体外形。

已从保存后的原生封装重新解析圆弧和焊盘，完成 5040 个点位检查，覆盖 720 个角度，确认负极连续成环且不占据中心区；另校核钢网位于对应铜面内并避开器件中心孔、两个逻辑引脚、零 PCB 钻孔、文件结构及压缩工程完整性。
未执行 EDA 原生导入/DRC、PCB 完整布线检查、实物尺寸公差确认或回流试焊。
预览 PDF/PNG 由实际 `.efoo` 图元独立渲染，是审阅图，不是 EDA 截图。

文件中的 `封装参数.json` 为参数快照。工作脚本位于 `tools/magnetic-n-pole-2mm-footprint/`，修改该目录 `config.json` 后，顺序运行 `build_footprint.py`、`render_review.py` 可重新生成。详见 `焊盘尺寸_mm.csv` 和 `校核记录.json`。
'''
(OUT/'使用说明.md').write_text(readme,encoding='utf-8')
bundle=OUT/'SJ056_N极母座_封装草稿_R01.zip'
with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(OUT.iterdir()):
        if p.is_file() and p!=bundle:z.write(p,p.name)
print('RENDERED',pdf)
print('BUNDLE',bundle)
