from pathlib import Path
import json,math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle,Ellipse
base=Path(__file__).resolve().parent;cfg=json.loads((base/'config.json').read_text());out=(base/cfg['output_dir']).resolve()
c=json.loads((out/'footprint-catalog.json').read_text());qa=base/'qa';qa.mkdir(exist_ok=True)
def draw(ax,key,v,label=True):
 for p in v['pads']:
  w,h=p['w'],p['h'];x,y=p['x'],p['y'];a=p['angle']
  if p['shape']=='ELLIPSE':shape=Ellipse((x,y),w,h,angle=a,color='#bf4b27')
  else:
   shape=Rectangle((x-w/2,y-h/2),w,h,angle=a,rotation_point='center',color='#bf4b27')
  ax.add_patch(shape)
  if label:ax.text(x,y,p['number'],ha='center',va='center',fontsize=5,color='white')
 w,h=v['body'];ax.add_patch(Rectangle((-w/2,-h/2),w,h,fill=False,ls='--',lw=.7,color='#1f6790'))
 ax.autoscale();ax.margins(.25);ax.set_aspect('equal');ax.set_title(key+' | '+str(len(v['pads']))+' pads',fontsize=9);ax.tick_params(labelsize=6);ax.grid(alpha=.15)
fig,axs=plt.subplots(5,5,figsize=(15,15))
for ax,(key,v) in zip(axs.flat,c.items()):draw(ax,key,v,key!='U1')
fig.suptitle('Footprint review - top view - coordinates in mm',fontsize=17);fig.tight_layout(rect=[0,0,1,.97]);fig.savefig(qa/'footprints-contact.png',dpi=140);plt.close(fig)
fig,ax=plt.subplots(figsize=(11,9));draw(ax,'U1 / SF32LB586VDD36',c['U1']);ax.set_xlabel('mm');ax.set_ylabel('mm');fig.savefig(qa/'bga256.png',dpi=160);plt.close(fig)
print(qa)
