/* 从冻结设计包生成字体交接补充；不修改批准稿、候选字库或固件。 */
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const {pathToFileURL} = require('node:url');
const runtime = '';
const {chromium} = require(runtime + 'playwright');
const {PNG} = require(runtime + 'pngjs');
const root = path.resolve(__dirname, '../..');
const base = 'docs/assets/v00-design-handoff/';
const out = 'docs/assets/v00-typography-v1/';
const doc = 'docs/ui/V00_字体规格与工程交接_v1.html';
const read = p => fs.readFileSync(path.join(root, p));
const json = p => JSON.parse(read(p));
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const write = (p, value) => {
  fs.mkdirSync(path.dirname(path.join(root, p)), {recursive:true});
  fs.writeFileSync(path.join(root, p), typeof value === 'string' ? value : JSON.stringify(value, null, 2) + '\n');
};
const samePixels = (a, b) => {
  const x = PNG.sync.read(a), y = PNG.sync.read(b);
  return x.width === y.width && x.height === y.height && x.data.equals(y.data);
};

function role(page, text) {
  if (page === 'Faces.modular') return ({'1':'FACE_DATE','周二':'FACE_WEEKDAY','10:09':'FACE_TIME','计时器':'FACE_CAPTION'})[text] || 'FACE_QUICK_TIME';
  if (text === '10:09') return 'TOP_TIME';
  if (text === '计时器' || text === '显示与亮度') return 'PAGE_TITLE';
  if (page === 'Timers.home') return text === '所有计时器' ? 'TIMER_SECTION' : text === '分钟' ? 'TIMER_UNIT' : text === '自定义' ? 'ACTION_LABEL' : 'TIMER_PRESET';
  if (page === 'Alarms.edit') return text === '24小时' ? 'ALARM_FORMAT' : text === ':' ? 'ALARM_SEPARATOR' : 'ALARM_VALUE';
  if (page === 'Settings.display') return text === '外观' ? 'SETTINGS_SECTION' : text === '已打开' ? 'ROW_DETAIL' : text === '›' ? 'ROW_CHEVRON' : 'ROW_TITLE';
  return 'CONTROL_BATTERY';
}

async function main() {
  const manifest = json(base + 'manifest.json');
  for (const input of manifest.source_inputs) assert.equal(sha(read(input.path)), input.sha256, '批准输入变化：' + input.path);
  for (const input of manifest.files) assert.equal(sha(read(base + input.path)), input.sha256, '批准包变化：' + input.path);
  const ids = ['System.grid','Faces.modular','Timers.home','Alarms.edit','Settings.display','System.control'];
  const specs = ids.map(id => json(base + 'specs/' + id + '.json'));
  const records = [], baselineChecks = [];
  const browser = await chromium.launch({headless:true, executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  try {
    const page = await browser.newPage({viewport:{width:1460,height:1000},deviceScaleFactor:1});
    await page.goto(pathToFileURL(path.join(root, 'docs/ui/Apple_Watch全量UI审阅台_v2.html')).href);
    await page.waitForFunction(() => window.IWUI && IWUI.pages.length === 359);
    for (const spec of specs) {
      await page.evaluate(id => {
        document.body.innerHTML = IWUI.render(IWUI.pages.find(p => p.id === id));
        const screen = document.querySelector('.w-screen');
        [screen,...screen.querySelectorAll('*')].forEach((e,n) => e.dataset.specId = 'e' + String(n).padStart(3,'0'));
      }, spec.id);
      await page.waitForFunction(() => [...document.images].every(i => i.complete && i.naturalWidth));
      await page.evaluate(() => document.fonts.ready);
      const original = await page.locator('.w-screen').screenshot();
      assert(samePixels(original, read(base + 'screens/' + spec.id + '.png')), '批准画面不能重现：' + spec.id);
      for (const text of spec.texts) {
        const staticDial = spec.id === 'Alarms.edit' && /^\d\d$/.test(text.text) && !['e082','e084'].includes(text.parent_key);
        let baseline = text.baseline_y, method = text.baseline_method;
        if (baseline === null && !staticDial) {
          const probe = await page.evaluate(({key,value}) => {
            const parent = document.querySelector('[data-spec-id="' + key + '"]');
            const node = [...parent.childNodes].find(n => n.nodeType === 3 && n.nodeValue.trim() === value);
            if (!node) return {ok:false,reason:'未找到唯一文本节点'};
            const screen = document.querySelector('.w-screen').getBoundingClientRect();
            const range = document.createRange(); range.selectNodeContents(node);
            const before = range.getBoundingClientRect();
            const wrapper = document.createElement('iw-type-probe');
            wrapper.style.cssText = 'display:inline;font:inherit;font-variant-numeric:inherit;letter-spacing:inherit;line-height:inherit;color:inherit;white-space:inherit;margin:0;padding:0;border:0';
            const marker = document.createElement('i');
            marker.style.cssText = 'display:inline-block!important;width:0!important;height:0!important;margin:0!important;padding:0!important;border:0!important;vertical-align:baseline!important;letter-spacing:0!important';
            parent.replaceChild(wrapper,node); wrapper.append(node,marker);
            range.selectNodeContents(node);
            const after = range.getBoundingClientRect();
            const shift = Math.max(...['x','y','width','height'].map(k => Math.abs(before[k]-after[k])));
            window.__v00TypeProbe = {parent,node,wrapper};
            return {ok:shift < .01,shift,baseline_y:+(marker.getBoundingClientRect().y-screen.y).toFixed(3)};
          }, {key:text.parent_key,value:text.text});
          const probed = await page.locator('.w-screen').screenshot();
          const unchanged = samePixels(original, probed);
          await page.evaluate(() => {
            const p = window.__v00TypeProbe;
            if (p) p.parent.replaceChild(p.node,p.wrapper);
            delete window.__v00TypeProbe;
          });
          if (probe.ok && unchanged) {
            baseline = probe.baseline_y;
            method = '补充测量：文本包裹与零尺寸基线标记；文本框不移位且整页像素不变';
          }
          baselineChecks.push({page:spec.id,key:text.parent_key,text:text.text,...probe,pixels_unchanged:unchanged,accepted:probe.ok && unchanged});
        }
        records.push({
          id:spec.id + ':' + text.parent_key, page:spec.id, source_key:text.parent_key,
          sample:text.text, role:staticDial ? 'ALARM_STATIC_TICK' : role(spec.id,text.text),
          rendering:staticDial ? 'already_in_dial_asset' : text.text === '›' ? 'glyph_or_reviewed_vector' : 'runtime_text',
          design:{size_px:parseFloat(text.style.fontSize),weight:+text.style.fontWeight,
            tracking_px:parseFloat(text.style.letterSpacing) || 0,line_height_px:parseFloat(text.style.lineHeight),
            numeric_variant:text.style.fontVariantNumeric,color:text.style.color,
            text_bounds:{x:text.x,y:text.y,width:text.width,height:text.height},
            alignment:text.style.textAlign,css_font_family:text.style.fontFamily,
            reported_browser_fonts:text.actual_fonts,
            baseline_y:baseline,baseline_method:method,original_baseline_y:text.baseline_y,
            scroll_area:spec.scroll_areas.find(s => text.y >= s.y)?.key || null},
          firmware:{approved:false,font_file:null,font_sha256:null,size_px:null,weight_file:null,
            tracking_px:null,baseline_y:null,approved_by:null},
          gate:staticDial ? '随刻度素材审核；禁止再画一层数字' : '设计值固定；固件映射须文字样张签字，不能取最近字号'
        });
      }
    }
  } finally { await browser.close(); }
  const roles = [...new Set(records.map(r => r.role))].map(id => {
    const entries = records.filter(r => r.role === id), r = entries[0];
    for (const entry of entries) for (const field of ['size_px','weight','tracking_px','line_height_px'])
      assert.equal(entry.design[field],r.design[field], id + ' 样式参数冲突');
    return {id,sample:r.sample,design_size_px:r.design.size_px,design_weight:r.design.weight,
      tracking_px:r.design.tracking_px,line_height_px:r.design.line_height_px,
      references:entries.map(e => e.id),firmware_approved:false};
  });
  const candidatePath = 'docs/ui/assets/v00/font-candidate/NotoSansSC-review-400.ttf';
  const licensePath = 'docs/ui/assets/v00/font-candidate/OFL.txt';
  const mapping = {
    schema:1,package_id:'IW-V00-TYPOGRAPHY-1',scope:'仅 V00 六页；不代表其余 353 个页面/状态已经完成字体规格',
    approval_id:manifest.approval_id,design_manifest_sha256:sha(read(base+'manifest.json')),
    statuses:{design_parameters:'frozen_from_approved_reference',font_candidate:'pending_visual_review',
      firmware_mapping:'not_approved',target_build:'not_run',hardware:'not_run'},
    unit:'390×450、DPR=1 设计像素；不是 pt，也不保证等于 TinyTTF 的 size_px',
    files:specs.map(s => ({path:base+'specs/'+s.id+'.json',sha256:sha(read(base+'specs/'+s.id+'.json'))})),
    candidate:{path:candidatePath,sha256:sha(read(candidatePath)),bytes:read(candidatePath).length,
      weight:400,license:licensePath,license_sha256:sha(read(licensePath)),status:'工程师现有单字重评审候选，不是正式字体',
      embedded_release_approved:false},
    controls:{no_nearest_size_fallback:true,no_unapproved_synthetic_bold:true,no_condensing_to_fit:true,
      no_dynamic_text_screenshot:true,font_payload_limit_bytes:131072,geometry_tolerance_px:1,
      old_page_migration_requires_regression:true},
    required_test_strings:['00:00','01:11','08:08','10:09','23:59','1','3','5','10','15','30','00','06','45','59','0%','9%','96%','100%','--%','所有计时器','显示与亮度','全天候显示','未接入','取消','确认'],
    roles,records
  };
  write(out+'typography.json',mapping);
  write(out+'baseline-checks.json',{method:'批准渲染器、相同浏览器/DPR；补测时整页像素必须不变',checks:baselineChecks});
  const unresolved = records.filter(r => r.rendering !== 'already_in_dial_asset' && r.design.baseline_y === null);
  const rows = roles.map(r => `<tr><td><code>${r.id}</code><br>${esc(r.sample)}</td><td>${r.design_size_px}</td><td>${r.design_weight}</td><td>${r.tracking_px}</td><td>${r.line_height_px}</td><td>${r.references.length}</td></tr>`).join('\n');
  const sections = specs.map(spec => {
    const entries = records.filter(r => r.page === spec.id);
    const body = entries.map(r => `<tr><td><code>${r.source_key}</code><br>${esc(r.sample)}</td><td><code>${r.role}</code></td><td>${r.design.size_px} / ${r.design.weight}<br>字距 ${r.design.tracking_px}；行高 ${r.design.line_height_px}</td><td>${r.design.text_bounds.x}, ${r.design.text_bounds.y}<br>${r.design.text_bounds.width} × ${r.design.text_bounds.height}</td><td>${r.design.baseline_y === null ? (r.rendering === 'already_in_dial_asset'?'随素材保留':'待补测，禁止猜测') : r.design.baseline_y}${r.design.original_baseline_y === null && r.design.baseline_y !== null ? '<br><small>本轮无像素变化补测</small>':''}</td><td>${r.rendering === 'already_in_dial_asset'?'静态刻度已在素材中':'映射未签字'}${r.design.scroll_area?'<br>随内容滚动':''}</td></tr>`).join('\n');
    return `<section id="page-${spec.id.replace(/\./g,'-')}"><h2>${esc(spec.name)} · ${spec.id}</h2><p><a href="../assets/v00-design-handoff/screens/${spec.id}.png">批准目标图</a> · <a href="../assets/v00-design-handoff/specs/${spec.id}.json">原始完整页面规格</a></p>${entries.length ? `<div class="scroll"><table><thead><tr><th>源节点／文字</th><th>统一样式 ID</th><th>字号／字重／字距／行高</th><th>设计文本范围 x,y / w,h</th><th>设计基线 y</th><th>处理</th></tr></thead><tbody>${body}</tbody></table></div>` : '<p>本批准蜂窝首屏没有动态文字，不增加应用名称或占位标签。</p>'}</section>`;
  }).join('\n');
  write(doc, `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>V00 字体规格与工程交接 v1</title>
<style>*{box-sizing:border-box}html{color-scheme:dark;scroll-padding-top:80px}body{margin:0;background:#10141b;color:#e8edf4;font:15px/1.75 "Segoe UI","Microsoft YaHei UI",sans-serif}a{color:#8fcaff}header{border-bottom:1px solid #344258;padding:16px 24px;display:flex;justify-content:space-between;gap:16px}main{max-width:1280px;margin:auto;padding:32px 24px 80px}h1{font-size:34px;line-height:1.35}h2{font-size:23px;margin:0 0 16px}h3{font-size:18px}section{border-top:1px solid #334055;margin-top:34px;padding-top:30px}.notice{border-left:4px solid #ffca72;background:#282216;padding:16px 20px}.good{background:#122923;border-color:#69d4b0}.stats{display:flex;gap:14px;flex-wrap:wrap;margin:24px 0}.stats>div{border:1px solid #344258;background:#192230;border-radius:10px;min-width:180px;padding:14px}.stats b{display:block;font-size:27px}.muted,small{color:#aab9ca}.links{display:flex;flex-wrap:wrap;gap:16px}.scroll{max-width:100%;overflow:auto}table{width:100%;border-collapse:collapse;font-size:13px}th,td{padding:11px;border:1px solid #344258;text-align:left;vertical-align:top}th{background:#202e40}code{font-size:12px;overflow-wrap:anywhere}table code{white-space:normal}.scroll table{min-width:850px}li{margin:8px 0}.compare{width:100%;height:auto;display:block;border:1px solid #334055;margin:14px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#192230;border:1px solid #344258;padding:15px}details{margin:15px 0}summary{cursor:pointer;color:#8fcaff}@media(max-width:600px){main{padding:24px 14px}header{padding:14px}h1{font-size:27px}.stats>div{min-width:140px;flex:1}}</style></head>
<body><header><strong>iwatch / V00 字体交接</strong><a href="../../index.html">项目首页</a></header><main>
<p class="muted">2026-09-23 · 390×450 · 设计参数冻结，固件字体映射待签字</p>
<h1>字号不用猜。<br>先冻结文字样式，再接入页面。</h1>
<p class="notice"><strong>交付状态：</strong>本页补齐六页的字号、字重、字距、行高、逐文字定位和工程执行规则；不是“最终字体文件已经获批”。当前 Noto Sans SC 400 仍是候选，不能把重复描画加粗当作 500／600 字重，也不能把主机样片当成实机通过。</p>
<div class="stats"><div><b>${roles.length}</b>统一文字样式</div><div><b>${records.length}</b>逐文字规格</div><div><b>${baselineChecks.filter(c=>c.accepted).length}</b>新增可靠基线测量</div><div><b>${unresolved.length}</b>动态／字形基线待补</div></div>
<div class="links"><a href="#sizes">字号速查</a><a href="#font-choice">字体方案</a><a href="#handoff">工程师任务</a><a href="#gates">验收规则</a><a href="../assets/v00-typography-v1/typography.json">机器规格 JSON</a><a href="../assets/v00-typography-v1/baseline-checks.json">基线补测记录</a></div>
<section id="sizes"><h2>1. 唯一的字号与字重表</h2>
<p>以下是已批准设计稿的值。400＝常规、500＝中等、600＝半粗、300＝细。字号单位是设计 px，不是 pt。没有对应字号、字重时必须报告缺口，不能自动改成最近的 24／26／48／80。</p>
<div class="scroll"><table><thead><tr><th>样式 ID／示例</th><th>字号 px</th><th>字重</th><th>字距 px</th><th>行高 px</th><th>使用处</th></tr></thead><tbody>${rows}</tbody></table></div>
<p>同一个字号不等于同一个样式：例如 26 px 的列表正文是 400，页标题是 500；不能只用字号作为字体缓存／样式的唯一身份。参数更改应集中到样式表，页面引用样式 ID，不分别手改。</p>
<p class="notice good">设计值与固件值是两列。若字体／栅格器导致 TinyTTF 必须用不同 size_px，先交同一区域的字形、宽度、基线、行高和颜色对照，批准后记录映射；绝不能反改设计值来掩盖差异。</p></section>
<section id="font-choice"><h2>2. 字体方案与当前候选</h2>
<p>批准稿使用 <strong>Segoe UI Variable／Microsoft YaHei UI</strong>，它们是浏览器参考字体，不是可直接复制进固件的授权文件。当前正式 DroidSansFallback 字库不在本轮替换。</p>
<p>工程师已有候选：<a href="assets/v00/font-candidate/NotoSansSC-review-400.ttf">NotoSansSC-review-400.ttf</a>，${mapping.candidate.bytes.toLocaleString('en-US')} B，字重 400，SHA-256：<code>${mapping.candidate.sha256}</code>。保留<a href="assets/v00/font-candidate/OFL.txt">OFL 1.1 与版权声明</a>；这份许可不覆盖 Apple 图标，也不代表候选已获视觉／固件发布签字。</p>
<p><strong>推荐试验方向：</strong>中文采用有许可的 Noto Sans SC 子集；常规、标题与半粗只收实际需要的字符。数字单独比较常规／中等／半粗的实际字形子集。先比较同一 Noto 家族；数字仍不接近时，再提交另一种有许可数字字体的并排样张，不由工程师独自替换。</p>
<ul><li>至少覆盖 300／400／500／600 的设计效果，但不等于打包四份完整中文字体。可选静态分字重子集或受支持的字形资源方案，均需预算证明。</li><li>当前 400 候选只是评审材料。它已有 105,596 B，不能把它与旧字库、其他字重简单全部叠加。所有新旧字库合计仍受现有 128 KiB TTF 上限约束；其他格式也不能绕过资源记账。</li><li>不默认要求 TinyTTF 支持可变字体轴；如选静态字重，记录生成工具版本、源文件、字符集、字体名称、权重、许可及文件哈希。</li><li>不准水平压缩字形来塞进按钮；不准将额外横向描画的一次“假粗体”写成已实现 Semibold。确需仿粗或分数字拼接，单列候选并先审阅。</li></ul>
<details><summary>工程师已有候选对照（仅引用，不是本轮批准）</summary><p><a href="V00_五页整改与字体对照_v1.html">完整候选记录</a>。下面的主机帧是工程师现有试验，不由本文重新渲染或宣称通过。</p><img class="compare" src="assets/v00/font-candidate/compare/Faces.modular.png" alt="表盘批准稿与现有 Noto 候选对照"><img class="compare" src="assets/v00/font-candidate/compare/Timers.home.png" alt="计时器批准稿与现有 Noto 候选对照"></details></section>
<section id="coordinates"><h2>3. 坐标与基线怎么用</h2>
<ul><li>下表的文本范围来自浏览器文本测量，<strong>不是 LVGL 控件框</strong>。控件容器、对齐锚点、滚动窗口仍取原页面规格。</li><li>基线是文字落脚的水平参考线，不是字形最下方像素，也不是控件底边。设计基线和 LVGL 字体的 ascent／descent、base_line 要通过专门适配映射。</li><li>本轮只在文本框位置不变、整页像素完全不变的情况下接受补测值。原 JSON 保持不变，新增数值只写在本补充包。</li><li>滚动页坐标是初始内容坐标；绘制时正文基线减去 scroll_y，固定标题不减。不能滚动后又将基线重复减一次。</li><li>字距 −0.3／−0.8 等小数不能静默舍为 0 或 −1。目标接口无法表达时，记录取整策略，验证整段宽度后签字；不恢复有缺陷的 kerning 缓存。</li><li>使用等宽数字时需证明字体／布局确实支持；不要只设置一个标志就宣称数字不会跳动。日期跨日、10→11、09→10、99→100 都要检查。</li></ul></section>
${sections}
<section id="handoff"><h2>4. 工程师下一步：只做字体校准小批次</h2>
<ol><li><strong>架构／设计：</strong>本补充包确定目标值、样式身份与坐标。工程师已有 80／48／32 等候选字号均不自动获得批准。需要偏离时交差异单。</li><li><strong>工程师：</strong>按样式制作独立主机字体样张，不先改五个页面。记录实际文件／字重／size_px／字距／基线／着色／测量值。字体缺失时明确失败，不偷偷回退系统字体或相邻字号。</li><li><strong>架构／用户：</strong>审阅候选；接受的映射填写到 JSON 的 firmware 字段及批准记录。当前这些字段有意为 null／false，生成器必须拒绝把未批准项当正式资源。</li><li><strong>工程师：</strong>签字后集中接入样式表，再更新五页。保持 GUI owner、字体引用与回收、OOM 路径；不能直接全局替换旧字库而不回归旧页。</li></ol>
<p>优先交 6 组样张：表盘大时间、计时器数字、所有计时器标题、闹钟时分、设置中文正文、控制中心电量。其余样式随相同字体体系补齐；同尺寸不同字重仍需分别检查。</p>
<pre>样式 ID：FACE_TIME
设计：84 px / 400 / 字距 −4 / 行高 88.2 / 基线 y=144
实际候选：字体文件＋SHA／实际字重／TinyTTF size_px／字距实现／基线
样例：00:00、01:11、08:08、10:09、23:59
证据：目标、主机、50% 叠图；宽度、可见字高、位置、裁切
批准：待签字；未经批准不得作为产品映射</pre></section>
<section id="gates"><h2>5. 验收与禁止事项</h2>
<ul><li>静态位置／容器几何按原契约整数化偏差不超过 1 px；字形本身不要求跨字体逐像素相同。按文字区域逐一检查字重、宽度、基线和行高，不能用整页平均误差签字。</li><li>样例覆盖 00:00、01:11、08:08、10:09、23:59；计时 1／3／5／10／15／30；闹钟 00／06／45／59；电量 0%／9%／96%／100%／--%；中文包含未接入、取消、确认。不得仅凭一个 10:09 调字距。</li><li>静态刻度数字已在闹钟刻度素材中，不能再绘制一层；动态时间／电量仍是真实文本，不能烘焙为整页截图。</li><li>字体服务／缓存若新增字重或字体来源，缓存身份必须包含相应区别，测试获取／释放、故障、重复进出、峰值内存和旧页面，不能只改字号枚举。</li><li>目标构建记录 GCC／Keil 的总字体载荷和主堆／PSRAM 预算；实机另验清晰度、缺字、滚动、刷新与性能。图片许可、字体许可、视觉签字各自独立。</li></ul>
<p class="notice">本次仅补文档、机器规格与浏览器基线证据。未修改工程师固件或字体文件，未重跑当前在制固件主机回归，未执行 GCC／Keil、COM9／COM10、J-Link、推送或关闭 Issue。六页之外的 353 项后续状态仍需各批提取规格，不宣称全量字体已交付。</p>
<p><a href="V00_设计资源与工程交接_v1.html">原设计交接</a> · <a href="Apple_Watch_UI实现计划_v3.html">实施计划</a> · <a href="../../index.html">项目首页</a></p></section>
</main></body></html>`);
  write(out+'manifest.json',{schema:1,package_id:mapping.package_id,design_manifest_sha256:mapping.design_manifest_sha256,
    design_parameters:'frozen',firmware_font_mapping:'not_approved',records:records.length,roles:roles.length,
    measured_baselines:baselineChecks.filter(c=>c.accepted).length,unresolved_dynamic_baselines:unresolved.map(r=>r.id),
    files:[doc,out+'typography.json',out+'baseline-checks.json','tools/design/build_v00_typography.cjs'].map(p=>({path:p,sha256:sha(read(p)),bytes:read(p).length}))});
  console.log(JSON.stringify({document:doc,records:records.length,roles:roles.length,baseline_checks:baselineChecks.length,
    supplemented_baselines:baselineChecks.filter(c=>c.accepted).length,unresolved_dynamic_baselines:unresolved.map(r=>r.id),status:'design_handoff_only'},null,2));
}
main().catch(error => { console.error(error); process.exitCode=1; });
