/* 检查已批准视觉与工程分包的一致性；只读文件，不访问硬件。 */
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');
const assets = 'docs/assets/watchos26-review/';
const plan = JSON.parse(read(assets + 'implementation-plan-v3.json'));
const catalog = JSON.parse(read(assets + 'catalog-v2.json'));
const html = read('docs/ui/Apple_Watch_UI实现计划_v3.html');
const expected = {V00:0, V01:54, V02:73, V03:0, V04:0, V05:41, V06:69, V07:115, V08:7, V09:0};
const parents = {V00:'D14', V01:'D14', V02:'D14', V03:'D15', V04:'D16', V05:'D17', V06:'D17', V07:'D17', V08:'D14', V09:'D18'};
assert.equal(plan.approval.status, 'approved_visual_baseline');
assert.equal(plan.approval.id, 'IW-VISUAL-APPROVAL-20260923-V2');
assert.equal(plan.status.visual_baseline, 'approved');
assert.equal(plan.status.new_visual_firmware, 'not_implemented');
assert.equal(plan.status.board, 'not_run_this_planning_change');
assert.equal(plan.status.gcc_keil, 'not_run_this_planning_change');
assert.equal(plan.status.host_firmware_tests, 'not_run_this_planning_change');
assert.equal(plan.pages.length, 359);
assert.equal(plan.counts.pages, catalog.page_count);
assert.equal(plan.counts.native_apps, catalog.app_count);
assert.equal(plan.counts.existing_routes, catalog.route_count);
assert.deepEqual(plan.counts.evidence, catalog.evidence);
assert.equal(new Set(plan.pages.map(p => p.id)).size, 359);
assert.equal(new Set(plan.pages.map(p => p.code)).size, 359);
assert.equal(new Set(plan.pages.map(p => p.app)).size, 52);
assert.equal(new Set(catalog.pages.filter(p => p.native).map(p => p.appId)).size, 50);
const source = new Map(catalog.pages.map(p => [p.id, p]));
const bundleMap = new Map(plan.bundles.map(b => [b.id, b]));
assert.deepEqual([...bundleMap.keys()].sort(), Object.keys(expected).sort());
const visited = new Set();
function visit(id, visiting = new Set()) {
  assert(!visiting.has(id), '工作包依赖不能成环：' + id);
  if (visited.has(id)) return;
  assert(bundleMap.has(id), '缺少依赖工作包：' + id);
  const next = new Set(visiting).add(id);
  for (const dependency of bundleMap.get(id).depends_on) visit(dependency, next);
  visited.add(id);
}
for (const b of plan.bundles) {
  assert.equal(b.parent, parents[b.id]);
  assert.equal(b.page_count, expected[b.id]);
  assert.equal(plan.pages.filter(p => p.bundle === b.id).length, b.page_count);
  visit(b.id);
}
const routeEntries = [...read('firmware/iwatch/src/gui_core/iw_routes.c').matchAll(/^\s+PAGE\((\w+),[^\n]*?,\s*(READY|RESERVED|OVERLAY|LEGACY),\s*\w+\),?$/gm)];
const routeNames = new Set(routeEntries.map(m => m[1]));
assert.equal(routeNames.size, 46, '路由表已扩展时请明确修订计划映射，不静默复用旧统计');
const routeCounts = Object.fromEntries(['READY','RESERVED','OVERLAY','LEGACY'].map(state => [state, routeEntries.filter(m => m[2] === state).length]));
assert.deepEqual(routeCounts, {READY:25, RESERVED:19, OVERLAY:2, LEGACY:0});
const mapped = new Set();
for (const p of plan.pages) {
  const original = source.get(p.id);
  assert(original, '计划出现视觉目录外的状态：' + p.id);
  assert.equal(p.code, original.code);
  assert.equal(p.title, original.title);
  assert.equal(p.app, original.appId);
  assert.equal(p.reference_evidence, original.evidence);
  assert.equal(p.reference_file, original.ref || null);
  assert.deepEqual(p.existing_routes, original.route);
  assert.equal(p.parent_task, parents[p.bundle]);
  assert.equal(p.visual_status, 'approved_baseline');
  assert.equal(p.implementation_status, 'not_implemented_against_v2');
  assert.equal(p.runtime_route_review, 'required');
  assert(Object.hasOwn(plan.capability_modes, p.target_mode));
  assert(html.includes('data-page-id="' + p.id + '" data-bundle="' + p.bundle + '"'), 'HTML 台账未同步：' + p.id);
  for (const route of p.existing_routes) { assert(routeNames.has(route), route); mapped.add(route); }
}
assert.equal(mapped.size, 46);
assert.equal([...html.matchAll(/data-page-id="/g)].length, 359);
assert.deepEqual(plan.first_delivery.screen_ids, ['System.grid','Faces.modular','Timers.home','Alarms.edit','Settings.display','System.control']);
for (const id of plan.first_delivery.screen_ids) assert(source.has(id));
assert.equal(plan.first_delivery.design_handoff.package_id, 'IW-V00-DESIGN-HANDOFF-1');
assert.equal(plan.first_delivery.design_handoff.release_allowed, false);
assert.equal(plan.first_delivery.integration_order[0], 'System.grid');
for (const field of ['document', 'manifest']) assert(fs.existsSync(path.join(root, plan.first_delivery.design_handoff[field])), '设计交付缺少文件：' + field);
assert(html.includes('V00_设计资源与工程交接_v1.html'), '计划缺少同源设计包入口');
// 安全敏感视觉不能因批准外观而升级成真实业务。
for (const p of plan.pages.filter(p => p.bundle === 'V06' || p.bundle === 'V07' || /^(System\.(passcode.*|pairing|siri|dictation|medical|sos|fall|ping))$/.test(p.id))) {
  assert.equal(p.target_mode, 'demo', p.id + ' 必须保留离线边界');
}
assert.equal(plan.frozen_inputs.length, 8);
assert.equal(new Set(plan.frozen_inputs.map(f => f.path)).size, 8);
for (const f of plan.frozen_inputs) {
  assert(f.path.startsWith(assets) && !f.path.includes('..'), '冻结路径不在参考目录');
  const actual = crypto.createHash('sha256').update(fs.readFileSync(path.join(root, f.path))).digest('hex');
  assert.equal(actual, f.sha256, '已批准画面输入发生变化：' + f.path);
}
const reference = JSON.parse(read(assets + 'sources.json'));
const referenceContext = vm.createContext({window:{}});
vm.runInContext(read(assets + 'sources.js'), referenceContext);
assert.equal(JSON.stringify(referenceContext.window.WATCH_REFERENCE), JSON.stringify(reference), '浏览器参考目录与冻结 JSON 不一致');
assert.equal(reference.images.length, 551);
for (const asset of reference.images) {
  assert.equal(path.basename(asset.local_name), asset.local_name, '参考图片路径不合法');
  const buffer = fs.readFileSync(path.join(root, assets, 'originals', asset.local_name));
  assert.equal(buffer.length, asset.bytes);
  assert.equal(crypto.createHash('sha256').update(buffer).digest('hex'), asset.sha256, '原始参考图片变更：' + asset.local_name);
}
const entryPoints = [
  'README.md', 'index.html', 'firmware/README.html', 'docs/项目状态.html', 'docs/UI复刻计划_watchOS26.html',
  'docs/ui/Apple_Watch功能差距与复刻验收矩阵_v1.html', 'docs/ui/Apple_Watch界面复刻定稿_v2.html',
  'docs/ui/Apple_Watch设计指南研究与项目适配_v1.html', 'docs/ui/视觉与交互规范_v1.html',
  'docs/ui/模块接口与开发任务_v1.html', 'docs/ui/D13_系统页面视觉目标_v1.html',
  'docs/ui/D14_蜂窝桌面与转场实施记录.html', 'docs/ui/D14-B_蜂窝视觉与手势实施记录.html',
  'docs/ui/功能逻辑与页面契约_v1.html', 'docs/ui/Apple_Watch全量UI审阅台_v2.html',
  'docs/ui/Apple_Watch官方视觉图册_待审核_v1.html', 'docs/ui/Apple_Watch_390x450视觉提案_待审核_v1.html'
];
for (const file of entryPoints) {
  const text = read(file);
  assert(text.includes('Apple_Watch_UI实现计划_v3.html'), '入口缺少新实施计划：' + file);
  assert(!text.includes('均未获视觉签字'), '入口仍错误标记整体未获批：' + file);
}
console.log(JSON.stringify({status:'passed', scope:'只读文档和视觉基线检查；非固件或实机验收', approval:plan.approval.id, pages:plan.pages.length, bundles:expected, existing_routes:routeCounts, frozen_inputs:8, synchronized_entries:entryPoints.length}, null, 2));
// 浏览器测试采用隔离上下文，不读取或覆盖用户的审核意见。
if (process.argv.includes('--browser')) {
  (async () => {
    const {chromium} = require('playwright');
    const {pathToFileURL} = require('node:url');
    const browser = await chromium.launch({headless:true, executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
    const output = path.join(root, 'work/ui-plan-v3');
    fs.mkdirSync(output, {recursive:true});
    const results = [], errors = [];
    try {
      const page = await browser.newPage();
      page.on('pageerror', error => errors.push(error.message));
      await page.goto(pathToFileURL(path.join(root, 'docs/ui/Apple_Watch_UI实现计划_v3.html')).href);
      await page.addStyleTag({content:'html{scroll-behavior:auto!important}'});
      await page.evaluate(() => document.fonts.ready);
      assert.equal(await page.locator('[data-page-id]').count(), 359);
      assert.equal(await page.locator('[data-app]').count(), 52);
      for (const width of [390, 768, 1440]) {
        await page.setViewportSize({width, height:1000});
        await page.evaluate(() => scrollTo(0, 0));
        let overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
        assert(!overflow, '计划首页横向溢出：' + width);
        await page.screenshot({path:path.join(output, 'plan-' + width + '.png')});
        await page.locator('[data-app="System"] > summary').click();
        overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
        assert(!overflow, '展开台账后横向溢出：' + width);
        await page.locator('[data-app="System"] > summary').click();
        results.push({width, overflow:false, expanded_catalog_overflow:false});
      }
      await page.evaluate(() => document.getElementById('first').scrollIntoView({behavior:'instant'}));
      await page.screenshot({path:path.join(output, 'first-delivery.png')});
      assert.equal(errors.length, 0);
      const report = {status:'passed', scope:'计划网页布局及台账检查；非固件或板验', pages:359, reference_images:551, errors, viewports:results};
      fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify(report, null, 2));
      console.log(JSON.stringify(report, null, 2));
    } finally {
      await browser.close();
    }
  })().catch(error => { console.error(error); process.exitCode = 1; });
}
