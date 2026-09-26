/* 从已批准网页导出同源素材和工程规格；不改画面、不接入固件、不访问硬件。 */
'use strict';
const fs=require('node:fs'), path=require('node:path'), crypto=require('node:crypto'), assert=require('node:assert/strict');
const {pathToFileURL}=require('node:url');
const runtime='';
const {chromium}=require(runtime+'playwright');
const {PNG}=require(runtime+'pngjs');
const root=path.resolve(__dirname,'../..');
const base='docs/assets/v00-design-handoff/';
const out=path.join(root,base);
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const contract=JSON.parse(read('tools/design/v00_handoff_contract.json'));
const plan=JSON.parse(read('docs/assets/watchos26-review/implementation-plan-v3.json'));
const references=JSON.parse(read('docs/assets/watchos26-review/sources.json'));
const byName=new Map(references.images.map(a=>[a.original_name.split('/').pop(),a]));
const inputs=plan.frozen_inputs.map(f=>{
  const actual=hash(fs.readFileSync(path.join(root,f.path)));
  assert.equal(actual,f.sha256,'批准画面输入已变化：'+f.path);
  return {...f};
});
const files=[];
function save(relative,data) {
  const full=path.join(out,relative);
  fs.mkdirSync(path.dirname(full),{recursive:true});
  fs.writeFileSync(full,data);
  const bytes=Buffer.isBuffer(data)?data:Buffer.from(data);
  const entry={path:relative,bytes:bytes.length,sha256:hash(bytes)};
  files.push(entry);return entry;
}
const json=(name,data)=>save(name,JSON.stringify(data,null,2)+'\n');
const safe=s=>s.replace(/[^a-zA-Z0-9._-]/g,'_');
const glyphNames=['back','close','plus','minus','check','play','pause','stop','next','previous','heart','music','run','flower','bell','lock','drop','sun','moon','bed','gear','phone','search','info','mic','send','power','grid','trash','arrow','location','wifi','battery','volume','checkcircle','record','book','list','compass','restore','bars','trend','users','award','cloud','type'];
const resources=[], resourceKeys=new Map(), screens=[];
const generatedAt=new Date().toISOString();
function rawVariants(id,pngBuffer,opaque=false) {
  const png=PNG.sync.read(pngBuffer);
  const count=png.width*png.height;
  const bgra=Buffer.alloc(count*4), rgb565=Buffer.alloc(count*2), a8=Buffer.alloc(count);
  let transparent=0;
  for(let n=0;n<count;n++){
    const p=n*4,r=png.data[p],g=png.data[p+1],b=png.data[p+2],a=png.data[p+3];
    bgra[p]=b;bgra[p+1]=g;bgra[p+2]=r;bgra[p+3]=a;
    rgb565.writeUInt16LE(((r>>3)<<11)|((g>>2)<<5)|(b>>3),n*2);
    a8[n]=a;if(a<255)transparent++;
  }
  const entries={
    png:save('assets/png/'+id+'.png',pngBuffer),
    bgra8888:save('assets/raw/'+id+'.bgra8888.bin',bgra),
    rgb565a8:save('assets/raw/'+id+'.rgb565a8.bin',Buffer.concat([rgb565,a8]))
  };
  if(opaque){assert.equal(transparent,0,'不透明背景包含透明像素');entries.rgb565=save('assets/raw/'+id+'.rgb565.bin',rgb565);}
  return {width:png.width,height:png.height,transparent_pixels:transparent,encodings:entries};
}
function svgName(markup,known){
  const d=markup.match(/<path[^>]* d="([^"]+)"/)?.[1];
  return d&&known[d]||'decoration';
}
(async()=>{
  fs.mkdirSync(out,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  try {
    const context=await browser.newContext({viewport:{width:1460,height:1000},deviceScaleFactor:1});
    const page=await context.newPage(), render=await context.newPage();
    await page.goto(pathToFileURL(path.join(root,'docs/ui/Apple_Watch全量UI审阅台_v2.html')).href);
    await page.waitForFunction(()=>window.IWUI&&IWUI.pages.length===359);
    const cdp=await context.newCDPSession(page);
    await cdp.send('DOM.enable');await cdp.send('CSS.enable');
    const known=await page.evaluate(names=>Object.fromEntries(names.map(n=>{
      const host=document.createElement('div');host.innerHTML=IWUI.glyph(n);
      return [host.querySelector('path').getAttribute('d'),n];
    })),glyphNames);
    async function raster(markup,width,height){
      await render.setViewportSize({width:Math.max(390,width+20),height:Math.max(450,height+20)});
      await render.setContent('<style>html,body{margin:0;background:transparent;color-scheme:light}#asset{position:relative;overflow:hidden;width:'+width+'px;height:'+height+'px}img,svg{display:block}</style><div id="asset">'+markup+'</div>');
      await render.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
      await render.evaluate(()=>document.fonts.ready);
      return render.locator('#asset').screenshot({omitBackground:true});
    }
    for(const screenContract of contract.screens){
      const id=screenContract.id;
      await page.evaluate(screenId=>{
        const p=IWUI.pages.find(p=>p.id===screenId);
        document.body.innerHTML=IWUI.render(p);
      },id);
      await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth));
      await page.evaluate(()=>document.fonts.ready);
      const screenshot=await page.locator('.w-screen').screenshot();
      const shot=save('screens/'+id+'.png',screenshot);
      const measured=await page.evaluate(({id,regions})=>{
        const screen=document.querySelector('.w-screen'),origin=screen.getBoundingClientRect();
        const all=[screen,...screen.querySelectorAll('*')];
        all.forEach((e,n)=>e.setAttribute('data-spec-id','e'+String(n).padStart(3,'0')));
        const num=v=>Number(v.toFixed(3));
        const box=e=>{const b=e.getBoundingClientRect();return {x:num(b.x-origin.x),y:num(b.y-origin.y),width:num(b.width),height:num(b.height)};};
        const rect=(e,b)=>({x:num(b.x-origin.x),y:num(b.y-origin.y),width:num(b.width),height:num(b.height)});
        const style=e=>{
          const s=getComputedStyle(e);
          return Object.fromEntries(['color','backgroundColor','backgroundImage','borderRadius','borderTopWidth','borderTopColor','boxShadow','filter','opacity','fontFamily','fontSize','fontWeight','fontStyle','fontVariantNumeric','letterSpacing','lineHeight','textAlign','paddingTop','paddingRight','paddingBottom','paddingLeft','gap','display','overflowX','overflowY','objectFit','objectPosition','transform'].map(k=>[k,s[k]]));
        };
        const componentElements=[...new Set(regions.flatMap(selector=>[...screen.querySelectorAll(selector)]))];
        const components=componentElements.map(e=>({key:e.dataset.specId,tag:e.tagName.toLowerCase(),class:String(e.getAttribute('class')||''),...box(e),style:style(e)}));
        const controls=[...screen.querySelectorAll('button')].map(e=>{
          const b=box(e),s=getComputedStyle(e),circle=e.classList.contains('app-dot')||e.classList.contains('round')||e.classList.contains('preset');
          return {key:e.dataset.specId,label:e.getAttribute('aria-label')||e.textContent.trim(),...b,prototype_target:e.dataset.go||null,
            shape:circle?'circle':'rounded_rect',center_x:num(b.x+b.width/2),center_y:num(b.y+b.height/2),radius:circle?num(Math.min(b.width,b.height)/2):null,
            corner_radius:s.borderRadius,requires_runtime_action_mapping:true};
        });
        const scrollAreas=[...screen.querySelectorAll('[data-scroll]')].map(e=>({key:e.dataset.specId,class:e.className,...box(e),scroll_height:e.scrollHeight,client_height:e.clientHeight,max_scroll_y:e.scrollHeight-e.clientHeight,padding_bottom:getComputedStyle(e).paddingBottom}));
        const texts=[],walker=document.createTreeWalker(screen,NodeFilter.SHOW_TEXT);
        const canvas=document.createElement('canvas'),ctx=canvas.getContext('2d');
        let node;
        while(node=walker.nextNode()){
          const value=node.nodeValue;if(!value.trim())continue;
          const parent=node.parentElement,s=getComputedStyle(parent),range=document.createRange();range.selectNodeContents(node);
          const b=range.getBoundingClientRect();if(!b.width||!b.height)continue;
          ctx.font=s.font;const metrics=ctx.measureText(value);
          let baseline=null,method='未测，需按字体度量与主机帧校准';
          if(!parent.closest('svg')&&!/flex|grid/.test(s.display)&&parent.childNodes.length===1){
            const marker=document.createElement('i');marker.setAttribute('style','display:inline-block!important;width:0!important;height:0!important;margin:0!important;padding:0!important;border:0!important;vertical-align:baseline!important');
            const before=parent.getBoundingClientRect();parent.append(marker);
            const after=parent.getBoundingClientRect();
            if(Math.abs(before.width-after.width)<.01&&Math.abs(before.height-after.height)<.01){baseline=num(marker.getBoundingClientRect().y-origin.y);method='浏览器零尺寸基线标记，验证容器尺寸未变化';}
            marker.remove();
          }
          texts.push({parent_key:parent.dataset.specId,text:value.trim(),...rect(parent,b),style:style(parent),baseline_y:baseline,baseline_method:method,
            canvas_metrics:{advance_without_css_tracking:num(metrics.width),actual_ascent:num(metrics.actualBoundingBoxAscent),actual_descent:num(metrics.actualBoundingBoxDescent)},
            actual_fonts:[]});
        }
        const images=[...screen.querySelectorAll('img')].map(e=>({key:e.dataset.specId,parent_key:e.parentElement.dataset.specId,...box(e),natural_width:e.naturalWidth,natural_height:e.naturalHeight,source:e.getAttribute('src'),style:style(e)}));
        const svgs=[...screen.querySelectorAll('svg')].map(e=>{
          const clone=e.cloneNode(true),s=getComputedStyle(e);clone.setAttribute('xmlns','http://www.w3.org/2000/svg');
          clone.setAttribute('width',e.getBoundingClientRect().width);clone.setAttribute('height',e.getBoundingClientRect().height);
          clone.setAttribute('style','display:block;color:'+s.color+';font-family:'+s.fontFamily+';font-weight:'+s.fontWeight);
          for(const n of [clone,...clone.querySelectorAll('*')])n.removeAttribute('data-spec-id');
          return {key:e.dataset.specId,parent_key:e.parentElement.dataset.specId,...box(e),markup:new XMLSerializer().serializeToString(clone),contains_text:!!e.querySelector('text'),style:style(e),glyph:e.classList.contains('glyph')};
        });
        return {id,canvas:{width:origin.width,height:origin.height,corner_radius:getComputedStyle(screen).borderRadius},components,controls,scroll_areas:scrollAreas,texts,images,svgs};
      },{id,regions:screenContract.regions});
      const {root:documentRoot}=await cdp.send('DOM.getDocument');
      for(const key of new Set(measured.texts.map(t=>t.parent_key))){
        const {nodeId}=await cdp.send('DOM.querySelector',{nodeId:documentRoot.nodeId,selector:'[data-spec-id="'+key+'"]'});
        const result=await cdp.send('CSS.getPlatformFontsForNode',{nodeId});
        for(const t of measured.texts.filter(t=>t.parent_key===key))t.actual_fonts=result.fonts;
      }
      const resourcePlacements=[];
      for(const im of measured.images){
        const localName=path.basename(im.source);
        const original=references.images.find(a=>a.local_name===localName);
        assert(original,'图片缺少来源：'+localName);
        const bytes=fs.readFileSync(path.join(root,'docs/assets/watchos26-review/originals',localName));
        assert.equal(hash(bytes),original.sha256,'参考文件变更');
        const key=original.sha256+'|'+im.width+'|'+im.height+'|'+im.style.filter;
        let resource=resourceKeys.get(key);
        if(!resource){
          const name=original.original_name.split('/').pop().replace(/\.png$/i,'');
          const assetId=safe(name)+'-'+Math.round(im.width)+'x'+Math.round(im.height)+'-'+hash(key).slice(0,6);
          const originalPath='assets/originals/'+safe(name)+'.png';
          if(!files.some(f=>f.path===originalPath))save(originalPath,bytes);
          const png=await raster('<img src="data:image/png;base64,'+bytes.toString('base64')+'" style="width:'+im.width+'px;height:'+im.height+'px;object-fit:'+im.style.objectFit+';filter:'+im.style.filter+'">',Math.round(im.width),Math.round(im.height));
          resource={id:assetId,kind:'apple_reference',role:name.includes('AppIcon')?'app_icon':'system_symbol',label:name,source_name:original.original_name,source_url:original.url,source_sha256:original.sha256,original_path:originalPath,filter:im.style.filter,release_allowed:false,license_status:'未获得再分发许可，本包不授予许可',...rawVariants(assetId,png)};
          resources.push(resource);resourceKeys.set(key,resource);
        }
        resourcePlacements.push({key:im.key,parent_key:im.parent_key,asset_id:resource.id,x:im.x,y:im.y,width:im.width,height:im.height});
      }
      for(const svg of measured.svgs){
        const key='svg|'+hash(svg.markup);
        let resource=resourceKeys.get(key);
        if(!resource){
          const name=svgName(svg.markup,known),assetId=name+'-'+Math.round(svg.width)+'x'+Math.round(svg.height)+'-'+hash(svg.markup).slice(0,6);
          const vector=save('assets/svg/'+assetId+'.svg',svg.markup+'\n');
          const png=await raster(svg.markup,Math.round(svg.width),Math.round(svg.height));
          resource={id:assetId,kind:'project_vector',role:svg.glyph?'glyph':'dial_decoration',label:name,vector,contains_text:svg.contains_text,source_path:'docs/assets/watchos26-review/ui-kit-v2.js',release_allowed:false,license_status:'项目绘制路径；仍需资源发布核查，不是 SF Symbols 授权包',...rawVariants(assetId,png)};
          resources.push(resource);resourceKeys.set(key,resource);
        }
        resourcePlacements.push({key:svg.key,parent_key:svg.parent_key,asset_id:resource.id,x:svg.x,y:svg.y,width:svg.width,height:svg.height});
      }
      const spec={...screenContract,...measured,reference:shot,resource_placements:resourcePlacements};
      delete spec.svgs;
      for(const im of spec.images)delete im.source;
      spec.runtime_actions=spec.controls.map(c=>({key:c.key,visual_label:c.label,prototype_target:c.prototype_target,
        runtime_route:id==='System.grid'?contract.grid_routes[c.label]||null:null,
        note:id==='System.grid'?(contract.grid_routes[c.label]?'现有真实入口，仍经统一路由与能力检查':'仅视觉/明确演示，禁止伪造可用 App'):'需要与现有页面契约绑定，禁止照抄网页 next/back'}));
      const scrollShots=[];
      for(const area of measured.scroll_areas.filter(a=>a.max_scroll_y>0)){
        await page.locator('[data-spec-id="'+area.key+'"]').evaluate(e=>e.scrollTop=e.scrollHeight);
        const png=await page.locator('.w-screen').screenshot();
        scrollShots.push({key:area.key,offset_y:area.max_scroll_y,...save('screens/'+id+'.scroll-end.png',png)});
        await page.locator('[data-spec-id="'+area.key+'"]').evaluate(e=>e.scrollTop=0);
      }
      spec.scroll_end_references=scrollShots;
      if(id==='System.control'){
        await page.locator('.w-screen').evaluate(e=>[...e.children].filter(c=>!c.classList.contains('cc-background')).forEach(c=>c.remove()));
        const png=await page.locator('.w-screen').screenshot();
        const resource={id:'control-background-only',kind:'project_material',role:'optional_background',label:'仅背景，无图标/电量/文字',release_allowed:false,license_status:'项目 CSS 背景；发布仍走项目资源清单',...rawVariants('control-background-only',png,true)};
        resources.push(resource);spec.optional_background=resource.id;
      }
      const pageFile=json('specs/'+id+'.json',spec);
      screens.push({...screenContract,spec:pageFile,reference:shot,scroll_end_references:scrollShots,controls:spec.controls,texts:spec.texts,components:spec.components,resource_placements:resourcePlacements,runtime_actions:spec.runtime_actions,scroll_areas:spec.scroll_areas});
      console.log('设计导出：'+id+'，'+resourcePlacements.length+' 个素材引用');
    }
    const allTexts=screens.flatMap(s=>s.texts.map(t=>({...t,screen:s.id})));
    const fonts=[...new Set(allTexts.flatMap(t=>t.actual_fonts.map(f=>f.familyName)))];
    const fontReport={status:'字体文件未打包；目标字体与固件字体映射仍需样片验收',actual_browser_fonts:fonts,
      font_files_exported:[],browser:browser.version(),device_scale_factor:1,
      firmware_candidate:{path:'firmware/iwatch/src/resource/fonts/DroidSansFallback.ttf',sha256:hash(fs.readFileSync(path.join(root,'firmware/iwatch/src/resource/fonts/DroidSansFallback.ttf'))),note:'当前工作树的候选字库；记录身份但不修改、不宣称等同目标字体'},
      rules:['动态文字保留文本控件，不贴整页图','字体替换先比数字宽度、中文基线、字重和行高，不能随意压缩字符','原始 SVG 中含文字的刻度装饰依赖浏览器字体；对应 PNG 为固定样例，动态数字不烘焙','新增中文须进入正式字集与子集门禁；不从 Windows/Apple 目录拷贝字体发布'],samples:allTexts};
    json('font-spec.json',fontReport);
    json('color-tokens.json',await page.evaluate(()=>IWUI.palette));
    json('contract.json',contract);
    const recommended=resources.filter(r=>r.role==='app_icon'||r.role==='system_symbol'||r.role==='glyph');
    const manifest={schema:1,package_id:contract.package_id,generated_at:generatedAt,approval_id:contract.approval_id,scope:'从冻结视觉稿导出的设计交付；未修改或验收固件',
      source_inputs:inputs,tool:{path:'tools/design/export_v00_design_handoff.cjs',sha256:hash(fs.readFileSync(__filename))},browser:browser.version(),device_scale_factor:1,
      release_allowed:false,automatic_firmware_import:false,hardware_access:false,
      binary_contract:{header:'全部 raw 文件无 LVGL header，不是可直接 lv_image_set_src 的 .bin 文件',bgra8888:'逐行紧密排列 B,G,R,A；每行 width*4；straight alpha，非预乘；逻辑字为 0xAARRGGBB',rgb565a8:'先 H 行 little-endian RGB565（每行 width*2），再 H 行 A8（每行 width）；straight alpha，非预乘',rgb565:'仅不透明背景；逐行 little-endian RGB565，每行 width*2',import:'工程师生成匹配当前 LVGL/EPIC 的描述符或资源容器，验证 stride/格式/Alpha；不同编码二选一，禁止全部链接'},
      budget:{image_limit_bytes:1048576,recommended_icon_rgb565a8_bytes:recommended.reduce((n,r)=>n+r.encodings.rgb565a8.bytes,0),recommended_icon_bgra8888_bytes:recommended.reduce((n,r)=>n+r.encodings.bgra8888.bytes,0),optional_background_rgb565_bytes:351000,all_variants_are_not_a_link_budget:true,firmware_existing_usage:'本轮未构建，需工程师记录已有镜像占用并计算剩余额度'},
      counts:{screens:screens.length,assets:resources.length,apple_references:resources.filter(r=>r.kind==='apple_reference').length,project_vectors:resources.filter(r=>r.kind==='project_vector').length,project_materials:resources.filter(r=>r.kind==='project_material').length,actual_fonts:fonts.length},
      generation_inputs:['tools/design/v00_handoff_contract.json','tools/design/v00_handoff_html.cjs'].map(p=>({path:p,sha256:hash(fs.readFileSync(path.join(root,p)))})),
      acceptance:contract.acceptance,screens:screens.map(s=>({id:s.id,name:s.name,spec:s.spec,reference:s.reference,scroll_end_references:s.scroll_end_references})),resources,files:[...files]};
    json('manifest.json',manifest);
    const payload={manifest,screens,fonts,contract};
    // 网页生成器使用同一份实测数据，避免手抄尺寸偏离。
    require('./v00_handoff_html.cjs').writePages(root,base,payload);
    console.log(JSON.stringify({status:'exported',...manifest.counts,budget:manifest.budget,fonts},null,2));
  } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});
