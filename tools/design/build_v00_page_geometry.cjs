/* 把批准稿页面与真实 TinyTTF 绝对坐标渲染并列，展示基线和裁剪边界。 */
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..');
const spec=JSON.parse(fs.readFileSync(path.join(root,'docs/assets/v00-typography-v1/typography.json'),'utf8'));
const output=path.join(root,'docs/ui/assets/v00/font-specimens/page-geometry');
const source=fs.readFileSync(path.join(root,'work/v00/type-specimens/page-baselines.log'),'utf8');
const cases=[
  ['FACE_TIME','Faces.modular:e004',3],['TIMER_PRESET','Timers.home:e010',5],
  ['TIMER_SECTION','Timers.home:e007',11],['ALARM_VALUE','Alarms.edit:e082',13],
  ['PAGE_TITLE','Timers.home:e006',34],
  ['ROW_TITLE','Settings.display:e020',16],['CONTROL_BATTERY','System.control:e012',21]
];
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
fs.mkdirSync(output,{recursive:true});
(async()=>{
  const browser=await chromium.launch({headless:true,
    executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage();
  const manifest=[];
  try {
    for (const [role,id,index] of cases) {
      const record=spec.records.find(item=>item.id===id);
      const line=source.split(/\r?\n/).find(item=>item.startsWith(`${id} `)&&item.includes(`index=${index} `));
      if (!record || !line || !line.endsWith('NO_CLIP')) throw Error(`页面基线证据无效：${id}`);
      const box=line.match(/container=([\d,]+)/);
      const ink=line.match(/ink=([\d,]+)/);
      if (!box || !ink) throw Error(`缺少裁剪或字形记录：${id}`);
      const raw=fs.readFileSync(path.join(root,`work/v00/type-specimens/raw-page/${String(index).padStart(2,'0')}.rgb565`));
      if (raw.length!==390*450*2) throw Error(`主机帧大小错误：${id}`);
      const png=await page.evaluate(async ({role,record,raw,target,container,ink})=>{
        const bytes=Uint8Array.from(atob(raw),c=>c.charCodeAt(0));
        const host=document.createElement('canvas');host.width=390;host.height=450;
        const hostContext=host.getContext('2d');
        const image=hostContext.createImageData(390,450);
        for(let i=0;i<390*450;i++) {
          const c=bytes[i*2]|bytes[i*2+1]<<8;
          image.data[i*4]=((c>>11)&31)*255/31;
          image.data[i*4+1]=((c>>5)&63)*255/63;
          image.data[i*4+2]=(c&31)*255/31;
          image.data[i*4+3]=255;
        }
        hostContext.putImageData(image,0,0);
        const targetImage=new Image();targetImage.src=`data:image/png;base64,${target}`;
        await targetImage.decode();
        const board=document.createElement('canvas');board.width=810;board.height=490;
        const ctx=board.getContext('2d');
        ctx.fillStyle='#10141b';ctx.fillRect(0,0,810,490);
        ctx.font='15px Segoe UI';ctx.fillStyle='#e8edf4';
        ctx.fillText(`${role} · 批准稿`,0,18);
        ctx.fillText('真实 TinyTTF 原坐标 · 黄色为设计文本框，青色为内部字体对象',410,18);
        ctx.drawImage(targetImage,0,30);ctx.drawImage(host,410,30);
        const bounds=record.design.text_bounds;
        ctx.strokeStyle='#ffe46b';ctx.lineWidth=1;
        ctx.strokeRect(Math.round(bounds.x)+.5,Math.round(bounds.y)+30.5,
          Math.ceil(bounds.width),Math.ceil(bounds.height));
        ctx.strokeRect(Math.round(bounds.x)+410.5,Math.round(bounds.y)+30.5,
          Math.ceil(bounds.width),Math.ceil(bounds.height));
        ctx.strokeStyle='#4adbed';
        ctx.strokeRect(container[0]+410.5,container[1]+30.5,container[2],container[3]);
        ctx.strokeStyle='#ff6d86';
        ctx.strokeRect(ink[0]+410.5,ink[1]+30.5,ink[2],ink[3]);
        return board.toDataURL('image/png').split(',')[1];
      },{role,record,raw:raw.toString('base64'),
        target:fs.readFileSync(path.join(root,`docs/assets/v00-design-handoff/screens/${record.page}.png`)).toString('base64'),
        container:box[1].split(',').map(Number),ink:ink[1].split(',').map(Number)});
      const filename=`${role}.png`;
      fs.writeFileSync(path.join(output,filename),Buffer.from(png,'base64'));
      manifest.push({role,record:id,index,container:box[1],ink:ink[1],no_clip:true,
        filename,sha256:sha(path.join(output,filename))});
    }
  } finally {await browser.close();}
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({status:'host_only_candidate',cases:manifest},null,2)+'\n');
  console.log(`V00 PAGE GEOMETRY OK: ${manifest.length} 对照`);
})().catch(error=>{console.error(error);process.exitCode=1});
