"""Native EasyEDA Pro SMD footprint for the supplied SJ-056 drawing.

The negative copper ring is composed of overlapping semicircular polygon pads
with the same electrical number 2. Each has its anchor inside copper. This uses
ordinary single-boundary POLY pads and never simulates an annulus with a PCB hole.
"""
from pathlib import Path
import json, math, uuid, zipfile, hashlib, csv

BASE=Path(__file__).resolve().parent
C=json.loads((BASE/'config.json').read_text(encoding='utf-8'))
D=C['design_mm']; O=C['documented_mm']; OUT=Path(C['output_directory'])
OUT.mkdir(parents=True,exist_ok=True)
NAME=C['name']

def mil(mm):return round(mm/.0254,6)
def uid(s):return uuid.uuid5(uuid.NAMESPACE_URL,'healthwatch/sj056-n/r01/'+s).hex
def dump(rows):return '\n'.join(json.dumps(r,ensure_ascii=False,separators=(',',':')) for r in rows)+'\n'
def attr(i,key,value,layer=13,x=None,y=None,visible=0,height=.6,parent=''):
    return ['ATTR',i,0,parent,layer,mil(x) if x is not None else None,mil(y) if y is not None else None,
            key,str(value),0,visible,'default',mil(height),mil(.1),0,0,4,0,0,0,0,0]
def poly(i,layer,width,path):return ['POLY',i,0,'',layer,mil(width),path,0]
def circle(i,layer,diameter,width=.08):return poly(i,layer,width,['CIRCLE',0,0,mil(diameter/2)])
def linepath(pts):return [mil(pts[0][0]),mil(pts[0][1]),'L']+[mil(v) for p in pts[1:] for v in p]

def sector(ro,ri,start,end,anchor=(0,0)):
    def point(r,angle):
        return [mil(r*math.cos(math.radians(angle))-anchor[0]),mil(r*math.sin(math.radians(angle))-anchor[1])]
    return point(ro,start)+['ARC',end-start]+point(ro,end)+['L']+point(ri,end)+['ARC',start-end]+point(ri,start)+['L']+point(ro,start)

with zipfile.ZipFile(BASE.parent/'schematic-v0.1/references/SF58-official.epro') as z:
    template=[json.loads(l) for l in z.read('FOOTPRINT/12dc0c7586c648cbbbd35df935293e88.efoo').decode('utf-8').splitlines() if l.strip()]
layers=[r for r in template if r and r[0]=='LAYER' and (r[1]<=14 or r[1]==47)]
layers += [['LAYER',71,'CUSTOM','Courtyard_REF',3,'#00cccc',1,'#006666',1]]
settings=[r for r in template if r and r[0]=='PRIMITIVE']
def header(kind):
    return [['DOCTYPE',kind,'1.8'],['HEAD',{'editorVersion':'2.2.40.3','importFlag':0}],
            ['CANVAS',0,0,'mm',mil(.5),mil(.5),mil(.05),mil(.05),mil(.01),mil(.01),1,0,5],
            *layers,['ACTIVE_LAYER',1],['NET','',None,None,1,None,0,None],*settings]

def pad(i,number,x,y,shape):
    # Disable automatic paste; purpose-built apertures are separate top-paste FILLs.
    return ['PAD',i,0,'',1,str(number),mil(x),mil(y),0,None,shape,[],0,0,0,0,0,
            mil(D['solder_mask_expansion_each_side']),0,mil(-100),mil(-100),0,None,None,None,None,[]]

rows=header('FOOTPRINT')
rows.append(pad('pad_positive','1',0,0,['ELLIPSE',mil(D['positive_copper_diameter']),mil(D['positive_copper_diameter'])]))
ro=D['negative_copper_outer_diameter']/2;ri=D['negative_copper_inner_diameter']/2
anchor_radius=(ro+ri)/2
for i,(start,end,anchor) in enumerate([(-90.2,90.2,(anchor_radius,0)),(89.8,270.2,(-anchor_radius,0))]):
    rows.append(pad('pad_negative_'+str(i),'2',*anchor,['POLY',sector(ro,ri,start,end,anchor)]))
for polarity in ('positive','negative'):
    gap=C['paste_sectors'][polarity]['half_gap_degrees']
    count=C['paste_sectors'][polarity]['count']
    for k in range(count):
        p=sector(D[polarity+'_paste_outer_diameter']/2,D[polarity+'_paste_inner_diameter']/2,
                 k*360/count+gap,(k+1)*360/count-gap)
        rows.append(['FILL','paste_'+polarity+'_'+str(k),0,'',7,0,0,[p],0])
rows += [circle('silkscreen',3,D['silkscreen_diameter'],D['silkscreen_stroke']),
         circle('body_outline',9,O['body_diameter']),
         circle('positive_face_ref',13,O['positive_solder_face_outer_diameter'],.035),
         circle('part_hole_ref',13,O['positive_solder_face_hole_diameter'],.035),
         circle('negative_outer_ref',13,O['negative_solder_face_outer_diameter'],.035),
         circle('negative_inner_ref',13,O['negative_solder_face_inner_diameter'],.035)]
hx=D['courtyard_width']/2;hy=D['courtyard_height']/2
rows.append(poly('courtyard',71,.05,linepath([(-hx,-hy),(hx,-hy),(hx,hy),(-hx,hy),(-hx,-hy)])))
rows += [attr('fp_name','Footprint',NAME),attr('ref','Designator','J?',3,0,3.15,1,.75),
         attr('status','Review status','DRAFT - VERIFY SMT',13,0,-3.15,1,.5),
         attr('pin1','Pin 1','1 = +',13,0,0,1,.35),attr('pin2','Pin 2','2 = -',13,1.5,0,1,.3),
         attr('height','Body height (mm)',O['body_height']),attr('magnetic','Magnetic pole','N (not electrical polarity)'),
         attr('electrical','Pad mapping','1=positive center; 2=negative annulus'),
         attr('routing','Routing note','Center positive requires layer transition; no via is included.'),
         attr('draft_values','Design status','Copper/mask/stencil dimensions are proposed PCB design values; verify assembly.'),
         attr('source','Source drawing',Path(C['source_pdf']).name)]
raw=dump(rows);fp=OUT/(NAME+'.efoo');fp.write_text(raw,encoding='utf-8')
fid=uid('footprint');pid=uid('pcb')
meta={'schematics':{},'pcbs':{pid:'SJ056_N极母座_封装预览_非生产PCB'},'panels':{},'symbols':{},
      'footprints':{fid:{'title':NAME,'source':'','version':1,'type':4,'desc':'SJ-056，中心正极、外环负极，贴片；PCB焊盘及钢网为设计草案。',
                         'tags':{'parent_tag':[],'child_tag':[]},'custom_tags':''}},
      'devices':{},'boards':{},'config':{'title':'SJ056_N极母座_封装草稿_R01','cbbProject':False,'defaultSheet':pid,'editorVersion':'2.2.40.3'}}
pcb=header('PCB')+[['COMPONENT','j1',0,1,0,0,0,{'Unique ID':uid('j1')},0],
    attr('j1_fp','Footprint',fid,3,parent='j1'),attr('j1_ref','Designator','J1',3,0,3.15,1,.75,'j1')]
epro=OUT/'SJ056_N极母座_嘉立创专业版_封装草稿.epro'
with zipfile.ZipFile(epro,'w',zipfile.ZIP_DEFLATED) as z:
    z.writestr('project.json',json.dumps(meta,ensure_ascii=False,indent=2))
    z.writestr('FOOTPRINT/'+fid+'.efoo',raw);z.writestr('PCB/'+pid+'.epcb',dump(pcb))

# Independently flatten the serialized circular arcs to audit the actual geometry.
def flatten(path,offset=(0,0),max_step_deg=.25):
    pts=[(path[0]*.0254+offset[0],path[1]*.0254+offset[1])];i=2;mode=None
    while i<len(path):
        if isinstance(path[i],str):mode=path[i];i+=1
        if mode=='L':
            pts.append((path[i]*.0254+offset[0],path[i+1]*.0254+offset[1]));i+=2
        elif mode=='ARC':
            angle=path[i];end=(path[i+1]*.0254+offset[0],path[i+2]*.0254+offset[1]);i+=3
            a=pts[-1];dx=end[0]-a[0];dy=end[1]-a[1];factor=.5/math.tan(math.radians(angle)/2)
            center=((a[0]+end[0])/2-dy*factor,(a[1]+end[1])/2+dx*factor)
            radius=math.hypot(a[0]-center[0],a[1]-center[1]);theta=math.atan2(a[1]-center[1],a[0]-center[0])
            n=max(2,math.ceil(abs(angle)/max_step_deg))
            pts += [(center[0]+radius*math.cos(theta+math.radians(angle)*j/n),center[1]+radius*math.sin(theta+math.radians(angle)*j/n)) for j in range(1,n+1)]
        else:raise ValueError('Unsupported path mode '+str(mode))
    return pts

def contains(poly,p):
    inside=False;x,y=p
    for a,b in zip(poly,poly[1:]+poly[:1]):
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:inside=not inside
    return inside

loaded=[json.loads(l) for l in fp.read_text(encoding='utf-8').splitlines()]
pads=[r for r in loaded if r[0]=='PAD'];fills=[r for r in loaded if r[0]=='FILL' and r[4]==7]
assert len(pads)==3 and sorted(p[5] for p in pads)==['1','2','2']
assert all(p[4]==1 and p[9] is None and p[15]==0 and p[19]<-1000 for p in pads)
assert len(fills)==8 and not any(r[0] in ('HOLE','VIA','REGION') for r in loaded)
polygons=[flatten(p[10][1],(p[6]*.0254,p[7]*.0254)) for p in pads if p[5]=='2']
assert all(contains(poly,(p[6]*.0254,p[7]*.0254)) for poly,p in zip(polygons,pads[1:]))
samples=0
for k in range(720):
    a=math.radians(k/2+.123)
    for r,expected in [(0,False),(.71,False),(ri-.01,False),(ri+.01,True),((ro+ri)/2,True),(ro-.01,True),(ro+.01,False)]:
        found=any(contains(p,(r*math.cos(a),r*math.sin(a))) for p in polygons)
        assert found==expected,(r,a,found,expected)
        samples+=1
for fill in fills:
    polarity='positive' if 'positive' in fill[1] else 'negative'
    p=flatten(fill[7][0]);rads=[math.hypot(x,y) for x,y in p]
    assert min(rads)>=D[polarity+'_paste_inner_diameter']/2-1e-5
    assert max(rads)<=D[polarity+'_paste_outer_diameter']/2+1e-5
    if polarity=='positive':assert min(rads)>O['positive_solder_face_hole_diameter']/2 and max(rads)<D['positive_copper_diameter']/2
    else:assert min(rads)>ri and max(rads)<ro
ids=[r[1] for r in loaded if r[0] in ('PAD','POLY','FILL','ATTR')]
assert len(ids)==len(set(ids))
with zipfile.ZipFile(epro) as z:
    assert z.testzip() is None and z.read('FOOTPRINT/'+fid+'.efoo').decode('utf-8')==raw
gap=(D['negative_copper_inner_diameter']-D['positive_copper_diameter'])/2
mask_gap=gap-2*D['solder_mask_expansion_each_side']
silk_gap=D['silkscreen_diameter']/2-D['silkscreen_stroke']/2-ro-D['solder_mask_expansion_each_side']
assert gap>=.3499 and mask_gap>=.2499 and silk_gap>=.1899
report={'footprint':NAME,'units':'mm','logical_pin_count':2,'native_pad_primitive_count':3,'numbers':['1','2','2'],
        'pcb_holes':0,'minimum_copper_clearance_mm':gap,'minimum_solder_mask_web_mm':mask_gap,
        'silkscreen_to_mask_minimum_mm':silk_gap,'paste_aperture_count':len(fills),
        'serialized_annulus_point_containment_checks':samples,'annulus_center_is_copper_free':True,
        'annulus_electrical_continuity_sampled_720_angles':'pass','paste_within_copper_and_part_center_hole_clear':'pass',
        'file_structure_and_zip_integrity':'pass','native_editor_import_and_DRC':'not performed','physical_fit_and_reflow':'not verified',
        'source_sha256':hashlib.sha256(Path(C['source_pdf']).read_bytes()).hexdigest(),
        'routing_note':C['routing'],'design_note':C['status']}
for polarity in ('positive','negative'):
    s=C['paste_sectors'][polarity];rout=D[polarity+'_paste_outer_diameter']/2;rin=D[polarity+'_paste_inner_diameter']/2
    area=math.pi*(rout*rout-rin*rin)*(1-s['count']*2*s['half_gap_degrees']/360)
    report[polarity+'_paste_area_mm2']=area
    report[polarity+'_paste_minimum_web_mm']=2*rin*math.sin(math.radians(s['half_gap_degrees']))
(OUT/'校核记录.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'封装参数.json').write_text(json.dumps(C,ensure_ascii=False,indent=2),encoding='utf-8')
with (OUT/'焊盘尺寸_mm.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f);w.writerow(['焊盘号','极性','结构','铜外径_mm','铜内径_mm','PCB孔径_mm','图层','说明'])
    w.writerow([1,'正极','中心实心圆盘',D['positive_copper_diameter'],0,0,'Top','需要在PCB布局阶段安排换层出线'])
    w.writerow([2,'负极','连续同心圆环',D['negative_copper_outer_diameter'],D['negative_copper_inner_diameter'],0,'Top','由两个重叠半环焊盘组成，均为2号；不可分别重编号'])
print(json.dumps(report,ensure_ascii=True,indent=2))
