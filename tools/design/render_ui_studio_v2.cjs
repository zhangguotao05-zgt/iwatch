/* 全量 UI 审阅台主机验证；只渲染网页，不打开串口或构建固件。 */
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const {pathToFileURL}=require('node:url');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),assets=path.join(root,'docs/assets/watchos26-review');
const output=path.join(root,'work/ui-review-v2');
// 已批准基线只校验不覆盖；临时审阅截图仍输出到 work。
const verifyOnly=process.argv.includes('--verify-only');
const context=vm.createContext({window:{}});
for(const name of ['sources.js','ui-kit-v2.js','ui-catalog-v2.js'])vm.runInContext(fs.readFileSync(path.join(assets,name),'utf8'),context,{filename:name});
const ui=context.window.IWUI,source=context.window.WATCH_REFERENCE;
const nativeNames=source.images.filter(i=>i.topics.includes('apdf1ebf8704')&&/ICt_.*AppIcon/.test(i.original_name)).map(i=>i.original_name.split('/').pop());
const appNames=ui.apps.map(a=>a.id==='Calendar'?'ICt_CalendarAppIcon.png':`ICt_${a.id}_AppIcon.png`);
const routeSource=fs.readFileSync(path.join(root,'firmware/iwatch/src/gui_core/iw_routes.c'),'utf8');
const routeNames=[...routeSource.matchAll(/^\s+PAGE\((\w+),/gm)].map(m=>m[1]);
const ids=new Set(ui.pages.map(p=>p.id)),mapped=new Set(ui.pages.flatMap(p=>p.route));
assert.equal(ids.size,ui.pages.length,'页面 ID 必须唯一');
assert.equal(nativeNames.length,50);assert.equal(routeNames.length,46);
assert.equal(nativeNames.filter(n=>!appNames.includes(n)).length,0,'所有原生 App 必须有原型组');
assert.equal(routeNames.filter(n=>!mapped.has(n)).length,0,'所有现有路由必须有视觉映射');
for(const p of ui.pages){
  assert(ids.has(p.back),p.id+' 返回无目标');assert(ids.has(p.next),p.id+' 下一状态无目标');
  if(p.ref)assert(fs.existsSync(path.join(assets,'originals',ui.assets.get(p.ref).local_name)),p.id+' 原图文件缺失');
  const rendered=ui.render(p);assert(!/undefined|NaN/.test(rendered),p.id+' 渲染异常');
  for(const m of rendered.matchAll(/data-go="([^"]+)"/g))assert(ids.has(m[1]),p.id+' 控件链接无目标：'+m[1]);
}
const data={release:ui.release,page_count:ui.pages.length,app_count:50,route_count:46,evidence:Object.fromEntries(['direct','derived','project'].map(n=>[n,ui.pages.filter(p=>p.evidence===n).length])),pages:ui.pages};
fs.mkdirSync(output,{recursive:true});
if(verifyOnly)assert.equal(fs.readFileSync(path.join(assets,'catalog-v2.json'),'utf8'),JSON.stringify(data,null,2),'冻结目录与绘制器不一致');
else fs.writeFileSync(path.join(assets,'catalog-v2.json'),JSON.stringify(data,null,2));
(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const browserContext=await browser.newContext({viewport:{width:1460,height:1100},acceptDownloads:true});
  const page=await browserContext.newPage(),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const url=pathToFileURL(path.join(root,'docs/ui/Apple_Watch全量UI审阅台_v2.html')).href;
  await page.goto(url);await page.waitForFunction(()=>window.IWStudio&&document.querySelectorAll('.screen-card').length===18);
  await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
  await page.selectOption('#app-filter','Timers');assert.equal(await page.locator('.screen-card').count(),11);
  await page.fill('#page-search','Timers.paused');assert.equal(await page.locator('.screen-card').count(),1);
  await page.locator('[data-inspect="Timers.paused"]').click();assert(await page.locator('#inspector').isVisible());
  await page.selectOption('#review-decision','revise');await page.fill('#review-note','自动测试：按钮颜色检查');
  await page.locator('#inspect-close').click();await page.reload();
  assert.equal(await page.evaluate(()=>IWStudio.getReview('Timers.paused').note),'自动测试：按钮颜色检查');
  await page.evaluate(()=>IWStudio.showPage('Timers.paused'));
  await page.locator('#inspect-screen [data-go="Timers.running"]').click();
  assert.equal(await page.locator('#inspect-screen .w-screen').getAttribute('data-screen'),'Timers.running');
  await page.locator('#inspect-close').click();
  await page.locator('[data-family="all"]').click();assert.equal(await page.locator('.screen-card').count(),18);
  await page.locator('#next').click();assert((await page.locator('#pagination-label').innerText()).includes('第 2'));
  const downloadPromise=page.waitForEvent('download');await page.locator('#export-review').click();const download=await downloadPromise;
  await download.saveAs(path.join(output,'review-export-test.json'));
  const exported=JSON.parse(fs.readFileSync(path.join(output,'review-export-test.json'),'utf8'));
  assert.equal(exported.baseline_approval.id,'IW-VISUAL-APPROVAL-20260923-V2');
  assert.equal(exported.baseline_approval.status,'approved_visual_baseline');
  assert.equal(exported.pages.length,ui.pages.length);assert.equal(exported.pages.find(p=>p.id==='Timers.paused').decision,'revise');
  await page.evaluate(()=>localStorage.removeItem(IWStudio.storageKey));
  const mobile=[];
  for(const width of [320,360,390,768,1024]){
    await page.setViewportSize({width,height:900});await page.evaluate(()=>IWStudio.selectFamily('priority'));
    mobile.push({width,overflow:await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth)});
    await page.evaluate(()=>IWStudio.showPage('Faces.modular'));
    mobile.push({width,dialog:true,...await page.evaluate(()=>{const d=document.getElementById('inspector'),b=d.getBoundingClientRect();return {overflow:d.scrollWidth>d.clientWidth+1,excess:d.scrollWidth-d.clientWidth,wide:[...d.querySelectorAll('*')].filter(e=>{const r=e.getBoundingClientRect();return r.right>b.right+1&&!e.closest('.w-screen');}).slice(0,8).map(e=>({tag:e.tagName,cls:e.className,id:e.id,width:e.getBoundingClientRect().width}))};})});
    await page.locator('#inspect-close').click();
  }
  await page.setViewportSize({width:1460,height:1100});
  await page.evaluate(()=>IWStudio.selectFamily('priority'));
  await page.screenshot({path:path.join(output,'studio-desktop.png')});
  await page.addStyleTag({content:'.audit-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:24px;padding:24px}.audit-head{padding:24px 32px 0;font:16px/1.8 "Microsoft YaHei UI";color:#c4ced8}.audit-grid .card-note{min-height:0}.audit-grid .card-head{font-size:12px}.audit-grid h3{margin-bottom:16px}'});
  const audit=[];
  for(let n=0;n<ui.pages.length;n+=12){
    const batch=ui.pages.slice(n,n+12).map(p=>p.id);
    await page.evaluate(ids=>{document.body.innerHTML='<div class="audit-grid">'+ids.map(id=>{const p=IWUI.pages.find(p=>p.id===id);return '<article class="screen-card"><div class="card-head">'+p.code+' / '+p.id+'</div><h3>'+IWUI.esc(p.title)+'</h3>'+IWUI.render(p)+'</article>';}).join('')+'</div>';},batch);
    await page.waitForFunction(()=>[...document.images].every(i=>i.complete));
    audit.push(...await page.evaluate(()=>[...document.querySelectorAll('.w-screen')].map(s=>{
      const b=s.getBoundingClientRect(),bad=[];
      for(const e of s.querySelectorAll('[data-safe]')){const r=e.getBoundingClientRect();if(r.left<b.left||r.right>b.right+1||r.top<b.top||r.bottom>b.bottom+1||e.scrollWidth>e.clientWidth+2)bad.push({text:e.textContent,rect:[r.x-b.x,r.y-b.y,r.width,r.height],scroll:[e.scrollWidth,e.clientWidth]});}
      return {id:s.dataset.screen,size:[b.width,b.height],safe_overflow:bad,broken_images:[...s.querySelectorAll('img')].filter(i=>!i.naturalWidth).map(i=>i.src)};
    })));
  }
  const sheets=[];
  const groups=[['priority-core',ui.priority.slice(0,9)],['priority-apps',ui.priority.slice(9)],...Object.keys(ui.families).filter(k=>!['priority','all'].includes(k)).flatMap(f=>{const all=ui.pages.filter(p=>p.family===f);return Array.from({length:Math.ceil(all.length/12)},(_,i)=>[f+'-'+(i+1),all.slice(i*12,i*12+12).map(p=>p.id)]);})];
  for(const [name,group] of groups){
    await page.evaluate(({name,group})=>{document.body.innerHTML='<div class="audit-head">iwatch · 全量首轮视觉稿 v2 / '+name+'<br>待用户审核 · 390 × 450 · 样例数据，不代表功能已实现</div><div class="audit-grid">'+group.map(id=>{const p=IWUI.pages.find(p=>p.id===id);return '<article class="screen-card"><div class="card-head">'+p.code+' / '+({direct:'对应原图复排',derived:'推导稿',project:'项目专有状态'})[p.evidence]+'</div><h3>'+IWUI.esc(p.title)+'</h3>'+IWUI.render(p)+'</article>';}).join('')+'</div>';},{name,group});
    await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
    const dest=name.startsWith('priority-')&&!verifyOnly?path.join(assets,'v2-'+name+'.png'):path.join(output,'v2-'+name+'.png');
    await page.screenshot({path:dest,fullPage:true});sheets.push({name,path:dest,ids:group});
  }
  const problems=audit.filter(r=>r.size[0]!==390||r.size[1]!==450||r.safe_overflow.length||r.broken_images.length);
  const inputs=['docs/ui/Apple_Watch全量UI审阅台_v2.html',...['sources.js','ui-kit-v2.js','ui-catalog-v2.js','studio.js','studio.css','studio-polish.css'].map(n=>'docs/assets/watchos26-review/'+n),'tools/design/render_ui_studio_v2.cjs'].map(name=>({path:name,sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(root,name))).digest('hex')}));
  const report={release:ui.release,approved_baseline_preserved:verifyOnly,generated_at:new Date().toISOString(),scope:'仅网页主机渲染、资源、标注安全区、链接与审核工具验证；不等于全像素视觉验收；未构建固件或连接硬件',inputs,counts:{pages:ui.pages.length,apps:50,routes:46},evidence:data.evidence,errors,mobile,problems,checks:['目录唯一ID','资源加载','50 App 覆盖','46 路由映射','359页逐页绘制','标注安全区','筛选搜索','分页','对照弹窗','状态跳转','意见保存重载','意见导出'],sheets};
  fs.writeFileSync(path.join(assets,'validation-v2.json'),JSON.stringify(report,null,2));
  fs.writeFileSync(path.join(output,'layout-audit.json'),JSON.stringify(audit,null,2));
  await browser.close();
  console.log(JSON.stringify({...report,sheets:report.sheets.length},null,2));
  if(errors.length||problems.length||mobile.some(m=>m.overflow))process.exitCode=1;
})().catch(e=>{console.error(e);process.exitCode=1;});
