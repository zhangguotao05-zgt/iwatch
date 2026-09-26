/* 字体补充包的只读一致性与浏览器排版检查；不验证固件或硬件。 */
'use strict';
const fs = require('node:fs'), path = require('node:path');
const crypto = require('node:crypto'), assert = require('node:assert/strict');
const {pathToFileURL} = require('node:url');
const {chromium} = require('playwright');
const root = path.resolve(__dirname,'../..');
const read = p => fs.readFileSync(path.join(root,p));
const json = p => JSON.parse(read(p));
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const out = 'docs/assets/v00-typography-v1/';
const pkg = json(out+'manifest.json'), type = json(out+'typography.json');
const checks = json(out+'baseline-checks.json').checks;
assert.equal(type.records.length,50);
assert.equal(type.roles.length,20);
assert.equal(new Set(type.records.map(r=>r.id)).size,50);
assert.equal(pkg.firmware_font_mapping,'not_approved');
assert.equal(type.statuses.firmware_mapping,'not_approved');
assert.equal(type.statuses.target_build,'not_run');
assert.equal(type.statuses.hardware,'not_run');
assert.equal(sha(read('docs/assets/v00-design-handoff/manifest.json')),pkg.design_manifest_sha256);
for (const file of [...pkg.files,...type.files]) assert.equal(sha(read(file.path)),file.sha256,file.path);
assert.equal(sha(read(type.candidate.path)),type.candidate.sha256);
assert.equal(sha(read(type.candidate.license)),type.candidate.license_sha256);
assert.equal(type.candidate.embedded_release_approved,false);
assert(checks.length===10 && checks.every(c=>c.accepted && c.pixels_unchanged && c.shift===0));
for (const record of type.records) {
  const original = json('docs/assets/v00-design-handoff/specs/'+record.page+'.json').texts.find(t=>t.parent_key===record.source_key);
  assert(original && original.text===record.sample);
  assert.equal(record.design.size_px,parseFloat(original.style.fontSize));
  assert.equal(record.design.weight,+original.style.fontWeight);
  assert.equal(record.design.tracking_px,parseFloat(original.style.letterSpacing)||0);
  assert.equal(record.design.line_height_px,parseFloat(original.style.lineHeight));
  assert.deepEqual(record.design.text_bounds,{x:original.x,y:original.y,width:original.width,height:original.height});
  if (original.baseline_y!==null) assert.equal(record.design.baseline_y,original.baseline_y);
  else if (record.rendering!=='already_in_dial_asset') {
    const proof=checks.find(c=>c.page===record.page && c.key===record.source_key);
    assert(proof && proof.accepted);
    assert.equal(record.design.baseline_y,proof.baseline_y);
  }
  assert.equal(record.firmware.approved,false);
  assert.equal(record.firmware.font_file,null);
  assert.equal(record.firmware.size_px,null);
}
const destinations=['README.md','index.html','firmware/README.html',
  'docs/ui/Apple_Watch_UI实现计划_v3.html','docs/ui/V00_设计资源与工程交接_v1.html',
  'docs/ui/V00_五页整改与字体对照_v1.html'];
for (const p of destinations) assert(read(p).toString().includes('V00_字体规格与工程交接_v1.html'),'入口缺失：'+p);

(async()=>{
  const evidence=path.join(root,'work/v00-typography-check-20260923');
  fs.mkdirSync(evidence,{recursive:true});
  const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const results=[],errors=[];
  try{
    const page=await browser.newPage({viewport:{width:1440,height:960},deviceScaleFactor:1});
    page.on('pageerror',e=>errors.push(e.message));
    await page.goto(pathToFileURL(path.join(root,'docs/ui/V00_字体规格与工程交接_v1.html')).href);
    await page.waitForFunction(()=>[...document.images].every(i=>i.complete && i.naturalWidth));
    assert.equal(await page.locator('tbody tr').count(),70);
    for(const width of [1440,768,390]){
      await page.setViewportSize({width,height:960});
      await page.evaluate(()=>scrollTo(0,0));
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'溢出：'+width);
      await page.screenshot({path:path.join(evidence,'typography-'+width+'.png')});
      await page.locator('details').evaluate(e=>e.open=true);
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      await page.locator('details').evaluate(e=>e.open=false);
      results.push({width,horizontal_overflow:false});
    }
    await page.setViewportSize({width:1440,height:960});
    await page.locator('#page-Alarms-edit').evaluate(e=>e.scrollIntoView());
    await page.screenshot({path:path.join(evidence,'alarm-baselines.png')});
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
  const report={status:'passed',scope:'仅设计规格与网页，不代表候选字体、固件或实机通过',
    records:50,roles:20,additional_baselines:10,entry_links:destinations.length,
    responsive:results,errors,host_firmware_tests:'not_run',target_build:'not_run',hardware:'not_run'};
  fs.writeFileSync(path.join(evidence,'report.json'),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
