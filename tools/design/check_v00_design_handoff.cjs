/* 设计资源包校验：格式、来源、坐标与网页；不构建固件、不访问串口。 */
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {pathToFileURL}=require('node:url');
const runtime='';
const {PNG}=require(runtime+'pngjs'),{chromium}=require(runtime+'playwright');
const root=path.resolve(__dirname,'../..'),out=path.join(root,'docs/assets/v00-design-handoff');
const read=p=>fs.readFileSync(path.join(out,p));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const manifest=JSON.parse(read('manifest.json')),contract=JSON.parse(read('contract.json'));
const resources=new Map(manifest.resources.map(a=>[a.id,a]));
assert.equal(manifest.release_allowed,false);assert.equal(manifest.automatic_firmware_import,false);
assert.equal(manifest.counts.screens,6);assert.equal(manifest.counts.assets,resources.size);
for(const f of [...manifest.source_inputs,...manifest.generation_inputs,manifest.tool]){
  assert.equal(hash(fs.readFileSync(path.join(root,f.path))),f.sha256,'输入变更后须重新导出：'+f.path);
}
assert.equal(new Set(manifest.files.map(f=>f.path)).size,manifest.files.length);
for(const f of manifest.files){
  assert(!f.path.includes('..')&&!path.isAbsolute(f.path),'非法资源路径');
  const buffer=read(f.path);assert.equal(buffer.length,f.bytes,f.path);
  assert.equal(hash(buffer),f.sha256,f.path);
}
const references=JSON.parse(fs.readFileSync(path.join(root,'docs/assets/watchos26-review/sources.json'),'utf8'));
for(const asset of resources.values()){
  assert.equal(asset.release_allowed,false);
  const png=PNG.sync.read(read(asset.encodings.png.path)),n=png.width*png.height;
  assert.equal(png.width,asset.width);assert.equal(png.height,asset.height);
  const bgra=read(asset.encodings.bgra8888.path),rgb=read(asset.encodings.rgb565a8.path);
  assert.equal(bgra.length,n*4);assert.equal(rgb.length,n*3);
  for(let p=0;p<n;p++){
    const i=p*4,r=png.data[i],g=png.data[i+1],b=png.data[i+2],a=png.data[i+3];
    assert.equal(bgra.readUInt32LE(i),((a<<24)|(r<<16)|(g<<8)|b)>>>0,asset.id+' BGRA');
    assert.equal(rgb.readUInt16LE(p*2),((r>>3)<<11)|((g>>2)<<5)|(b>>3),asset.id+' RGB565');
    assert.equal(rgb[n*2+p],a,asset.id+' A8');
  }
  if(asset.kind==='apple_reference'){
    const reference=references.images.find(i=>i.original_name===asset.source_name);
    assert(reference);assert.equal(hash(read(asset.original_path)),reference.sha256);
  }
  if(asset.encodings.rgb565)assert(read(asset.encodings.rgb565.path).equals(rgb.subarray(0,n*2)));
}
const specs=manifest.screens.map(s=>JSON.parse(read(s.spec.path)));
for(const spec of specs){
  assert.equal(spec.canvas.width,390);assert.equal(spec.canvas.height,450);
  const screenshot=PNG.sync.read(read(spec.reference.path));
  assert.equal(screenshot.width,390);assert.equal(screenshot.height,450);
  for(const placement of spec.resource_placements){
    const asset=resources.get(placement.asset_id);assert(asset,'缺少素材 '+placement.asset_id);
    assert.equal(asset.width,Math.round(placement.width));assert.equal(asset.height,Math.round(placement.height));
  }
  for(const c of spec.controls)assert(c.requires_runtime_action_mapping);
}
const grid=specs.find(s=>s.id==='System.grid');
assert.equal(grid.controls.length,17);
assert.equal(grid.resource_placements.length,17);
assert.equal(grid.runtime_actions.filter(a=>a.runtime_route).length,4);
for(const button of grid.controls){
  assert.equal(button.shape,'circle');
  assert.equal(button.radius,button.width/2);
  assert.equal(button.center_x,button.x+button.width/2);
  assert.equal(button.center_y,button.y+button.height/2);
}
assert.deepEqual(Object.keys(contract.grid_routes).sort(),['Alarms','Settings','Stopwatch','Timers']);
const fonts=JSON.parse(read('font-spec.json'));
assert.deepEqual(fonts.font_files_exported,[]);
(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const work=path.join(root,'work/v00-design-handoff-check');fs.mkdirSync(work,{recursive:true});
  const errors=[],viewports=[],recomposition=[];
  try{
    const page=await browser.newPage({viewport:{width:1460,height:1000},deviceScaleFactor:1});
    page.on('pageerror',e=>errors.push(e.message));
    await page.goto(pathToFileURL(path.join(root,'docs/ui/Apple_Watch全量UI审阅台_v2.html')).href);
    await page.waitForFunction(()=>window.IWUI);
    for(const spec of specs){
      await page.evaluate(id=>{document.body.innerHTML=IWUI.render(IWUI.pages.find(p=>p.id===id));const screen=document.querySelector('.w-screen');[screen,...screen.querySelectorAll('*')].forEach((e,n)=>e.dataset.specId='e'+String(n).padStart(3,'0'));},spec.id);
      await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
      await page.evaluate(()=>document.fonts.ready);
      const original=await page.locator('.w-screen').screenshot();
      assert(original.equals(read(spec.reference.path)),'基准图与冻结绘制器不一致：'+spec.id);
      const controls=await page.evaluate(()=>{const root=document.querySelector('.w-screen').getBoundingClientRect();return [...document.querySelectorAll('.w-screen button')].map(e=>{const b=e.getBoundingClientRect();return {key:e.dataset.specId,x:b.x-root.x,y:b.y-root.y,w:b.width,h:b.height};});});
      for(const c of controls){
        const target=spec.controls.find(t=>t.key===c.key);assert(target);
        assert(Math.abs(c.x-target.x)<.01&&Math.abs(c.y-target.y)<.01&&Math.abs(c.w-target.width)<.01&&Math.abs(c.h-target.height)<.01);
      }
      const replacements=spec.resource_placements.map(p=>({key:p.key,data:'data:image/png;base64,'+read(resources.get(p.asset_id).encodings.png.path).toString('base64')}));
      await page.evaluate(items=>{
        for(const item of items){
          const old=document.querySelector('[data-spec-id="'+item.key+'"]');
          if(old.tagName.toLowerCase()==='img'){old.src=item.data;old.style.filter='none';}
          else {
            const style=getComputedStyle(old),rect=old.getBoundingClientRect(),img=document.createElement('img');
            img.src=item.data;img.style.cssText='width:'+rect.width+'px;height:'+rect.height+'px;display:'+style.display+';position:'+style.position+';top:'+style.top+';right:'+style.right+';bottom:'+style.bottom+';left:'+style.left+';margin:'+style.margin+';flex-shrink:'+style.flexShrink+';object-fit:contain';
            old.replaceWith(img);
          }
        }
      },replacements);
      await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
      const recomposed=await page.locator('.w-screen').screenshot();
      fs.writeFileSync(path.join(work,spec.id+'.recomposed.png'),recomposed);
      const a=PNG.sync.read(original),b=PNG.sync.read(recomposed);
      let total=0;
      for(let n=0;n<a.data.length;n+=4)for(let channel=0;channel<3;channel++)total+=Math.abs(a.data[n+channel]-b.data[n+channel]);
      const mean=total/(390*450*3);
      // 这是导出自检阈值，只防止空图、错色、错资源；不充当固件视觉签字。
      assert(mean<2,'导出素材复合差异异常：'+spec.id+' '+mean);
      recomposition.push({id:spec.id,mean_channel_error:Number(mean.toFixed(5)),note:'仅设计素材复合自检，非固件验收'});
    }
    await page.goto(pathToFileURL(path.join(root,'docs/ui/V00_设计资源与工程交接_v1.html')).href);
    await page.addStyleTag({content:'html{scroll-behavior:auto!important}'});
    await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
    for(const width of [390,768,1440]){
      await page.setViewportSize({width,height:1000});await page.evaluate(()=>scrollTo(0,0));
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'页面横向溢出 '+width);
      await page.screenshot({path:path.join(work,'handoff-'+width+'.png')});
      await page.locator('details').first().evaluate(e=>e.open=true);
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      await page.locator('details').first().evaluate(e=>e.open=false);
      viewports.push({width,overflow:false});
    }
    await page.locator('#grid').evaluate(e=>e.scrollIntoView({behavior:'instant'}));
    await page.screenshot({path:path.join(work,'honey-map.png')});
    await page.goto(pathToFileURL(path.join(out,'index.html')).href);
    await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
    assert.equal(errors.length,0);
  }finally{await browser.close();}
  const report={schema:1,status:'passed',scope:'仅设计包来源、像素载荷、坐标、浏览器布局与素材复合；非固件/板验',generated_at:new Date().toISOString(),manifest_sha256:hash(read('manifest.json')),files:manifest.files.length,assets:resources.size,grid_icons:17,real_routes:4,screens:specs.length,fonts:fonts.actual_browser_fonts,recomposition,viewports,errors,firmware_modified:false,hardware_access:false,release_allowed:false};
  fs.writeFileSync(path.join(out,'validation.json'),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
