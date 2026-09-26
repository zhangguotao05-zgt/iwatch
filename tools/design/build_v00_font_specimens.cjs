/* 将真实 TinyTTF 主机帧与批准稿文字区域并列归档，不把候选写入正式映射。 */
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '../..');
const spec = JSON.parse(fs.readFileSync(path.join(root, 'docs/assets/v00-typography-v1/typography.json'), 'utf8'));
const mode = ['candidate','color','battery32'].includes(process.env.V00_SPEC_MODE) ? process.env.V00_SPEC_MODE : 'design';
const output = path.join(root, 'docs/ui/assets/v00/font-specimens', mode === 'design' ? '' : mode);
const raw = path.join(root, 'work/v00/type-specimens',
  mode === 'battery32' ? 'raw-battery32' : mode === 'color' ? 'raw-color' : mode === 'candidate' ? 'raw-candidate' : 'raw');
const log = fs.readFileSync(path.join(root, 'work/v00/type-specimens',
  mode === 'battery32' ? 'battery32.log' : mode === 'color' ? 'color-size.log' : mode === 'candidate' ? 'candidate-size.log' : 'design-size.log'), 'utf8');
const candidateSizes = {FACE_TIME:81,FACE_WEEKDAY:30,TIMER_PRESET:49,TIMER_SECTION:25,
  ALARM_VALUE:44,ROW_TITLE:26,PAGE_TITLE:26,CONTROL_BATTERY:31,
  ALARM_SEPARATOR:35,ROW_DETAIL:20,ACTION_LABEL:26};
const groups = [
  {role:'FACE_TIME', indexes:[0,1,2,3,4,30], canonical:3, record:'Faces.modular:e004', weight:400},
  {role:'FACE_WEEKDAY', indexes:[32], canonical:32, record:'Faces.modular:e003', weight:600},
  {role:'TIMER_PRESET', indexes:[5,6,7,8,9,10], canonical:5, record:'Timers.home:e010', weight:600},
  {role:'TIMER_SECTION', indexes:[11], canonical:11, record:'Timers.home:e007', weight:600},
  {role:'ALARM_VALUE', indexes:[12,13,14,15], canonical:13, record:'Alarms.edit:e082', weight:400},
  {role:'ROW_TITLE', indexes:[16,17,18], canonical:16, record:'Settings.display:e020', weight:400},
  {role:'CONTROL_BATTERY', indexes:[19,20,21,22,23,31], canonical:21, record:'System.control:e012', weight:500},
  {role:'ALARM_SEPARATOR', indexes:[24], canonical:24, record:'Alarms.edit:e083', weight:300},
  {role:'ROW_DETAIL', indexes:[25,26,33], canonical:25, record:'Settings.display:e031', weight:400},
  {role:'ACTION_LABEL', indexes:[27,28,29], canonical:27, record:'Timers.home:e027', weight:500},
  {role:'PAGE_TITLE', indexes:[34], canonical:34, record:'Timers.home:e006', weight:500}
];
if (mode === 'battery32') groups.splice(0,groups.length,...groups.filter(item=>item.role==='CONTROL_BATTERY'));
const parsed = new Map();
for (const line of log.split(/\r?\n/)) {
  if (!line.startsWith('V00_FONT ')) continue;
  const entry = {};
  for (const pair of line.slice(9).split(' ')) {
    const equal = pair.indexOf('=');
    if (equal > 0) entry[pair.slice(0,equal)] = pair.slice(equal+1);
  }
  parsed.set(Number(entry.index), entry);
}
const fileHash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const expected = groups.flatMap(group => group.indexes);
if (expected.length !== (mode==='battery32'?6:35) || parsed.size !== expected.length ||
    expected.some(index => !parsed.has(index)))
  throw Error('TinyTTF 样例不完整');
for (const group of groups) {
      const role = spec.roles.find(item => item.id === group.role);
  if (!role || role.design_weight !== group.weight) throw Error(`字重不匹配：${group.role}`);
  for (const index of group.indexes) {
    const item = parsed.get(index);
    const expectedSize = mode === 'design' ? role.design_size_px :
      mode === 'battery32' ? 32 : candidateSizes[group.role];
    if (item.role !== group.role || Number(item.size) !== expectedSize ||
        item.result !== 'ok' || Number(item.pixels) < 1)
      throw Error(`字体样例不匹配：${index}`);
    if (fs.statSync(path.join(raw, `${String(index).padStart(2,'0')}.rgb565`)).size !== 390*450*2)
      throw Error(`帧大小错误：${index}`);
  }
}
fs.mkdirSync(output, {recursive:true});

async function main() {
  const browser = await chromium.launch({headless:true,
    executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page = await browser.newPage();
  const client = await page.context().newCDPSession(page);
  await client.send('DOM.enable');
  await client.send('CSS.enable');
  const results = [];
  try {
    for (const group of groups) {
      const record = spec.records.find(item => item.id === group.record);
      if (!record || record.role !== group.role || record.firmware.approved)
        throw Error(`设计记录无效：${group.record}`);
      if (mode === 'color' || mode === 'battery32') {
        const components = record.design.color.match(/\d+/g).map(Number);
        const hex = components.map(value => value.toString(16).padStart(2,'0')).join('').toUpperCase();
        for (const index of group.indexes) {
          const item = parsed.get(index);
          if (item.fg !== hex || item.bg !== '17181C')
            throw Error(`同色同底配置不匹配：${index}`);
        }
      }
      const references = new Map();
      let platformFonts = [];
      let referenceStyle = null;
      if (mode === 'color' || mode === 'battery32') {
        await page.setContent('<!doctype html><html><head><meta charset="utf-8"></head><body style="margin:0">'+
          '<div id="v00-ref" style="position:relative;width:300px;height:104px;overflow:hidden;background:#17181c">'+
          '<span id="v00-line" style="position:absolute;left:50%;white-space:nowrap;transform:translateX(-50%)">'+
          '<span id="v00-text"></span><span id="v00-baseline" style="display:inline-block;width:0;height:0"></span>'+
          '</span></div></body></html>');
        for (const index of group.indexes) {
          const text = parsed.get(index).text;
          const computed = await page.evaluate(({design,text}) => {
            const box = document.querySelector('#v00-ref');
            const line = document.querySelector('#v00-line');
            const label = document.querySelector('#v00-text');
            label.textContent = text;
            label.style.fontFamily = design.css_font_family;
            label.style.fontWeight = String(design.weight);
            label.style.fontSize = `${design.size_px}px`;
            label.style.lineHeight = `${design.line_height_px}px`;
            label.style.letterSpacing = `${design.tracking_px}px`;
            label.style.fontVariantNumeric = design.numeric_variant;
            label.style.color = design.color;
            line.style.top = '0px';
            const baseline = document.querySelector('#v00-baseline').getBoundingClientRect().bottom -
              box.getBoundingClientRect().top;
            line.style.top = `${82-baseline}px`;
            const style = getComputedStyle(label);
            return {fontFamily:style.fontFamily,fontWeight:style.fontWeight,
              fontSize:style.fontSize,lineHeight:style.lineHeight,
              letterSpacing:style.letterSpacing,numericVariant:style.fontVariantNumeric,
              color:style.color,background:getComputedStyle(box).backgroundColor,
              baseline:document.querySelector('#v00-baseline').getBoundingClientRect().bottom-
                box.getBoundingClientRect().top};
          }, {design:record.design,text});
          if (computed.numericVariant !== record.design.numeric_variant ||
              Math.abs(computed.baseline - 82) > 0.01)
            throw Error(`DOM 文字规格或基线不匹配：${group.role} ${text}`);
          await page.evaluate(() => document.fonts.ready);
          references.set(index,(await page.locator('#v00-ref').screenshot({type:'png'})).toString('base64'));
          if (index === group.canonical) {
            referenceStyle = computed;
            const dom = await client.send('DOM.getDocument');
            const node = await client.send('DOM.querySelector',
              {nodeId:dom.root.nodeId,selector:'#v00-text'});
            const fonts = await client.send('CSS.getPlatformFontsForNode',{nodeId:node.nodeId});
            platformFonts = fonts.fonts.map(font => ({familyName:font.familyName,
              postScriptName:font.postScriptName,glyphCount:font.glyphCount}));
            if (!platformFonts.length) throw Error(`无法识别浏览器实际字体：${group.role}`);
          }
        }
      }
      const fontFile = path.join(root, `work/v00/fonts/NotoSansSC-review-${group.weight}.ttf`);
      const payload = {
        role:group.role,
        mode,
        design:record.design,
        actualSize:Number(parsed.get(group.canonical).size),
        canonical:group.canonical,
        samples:group.indexes.map(index => ({index, ...parsed.get(index),
          raw:fs.readFileSync(path.join(raw, `${String(index).padStart(2,'0')}.rgb565`)).toString('base64'),
          reference:references.get(index)})),
        target:fs.readFileSync(path.join(root, `docs/assets/v00-design-handoff/screens/${record.page}.png`)).toString('base64'),
      };
      const png = await page.evaluate(async ({role,mode,design,actualSize,canonical,samples,target}) => {
        const loadImage = async source => {
          const image = new Image();
          image.src = `data:image/png;base64,${source}`;
          await image.decode();
          return image;
        };
        const targetImage = await loadImage(target);
        const hostImages = new Map();
        const referenceImages = new Map();
        for (const sample of samples) {
          const bytes = Uint8Array.from(atob(sample.raw), char => char.charCodeAt(0));
          const canvas = document.createElement('canvas');
          canvas.width = 390; canvas.height = 450;
          const ctx = canvas.getContext('2d');
          const image = ctx.createImageData(390,450);
          for (let i=0; i<390*450; i++) {
            const color = bytes[i*2] | bytes[i*2+1] << 8;
            image.data[i*4] = ((color >> 11) & 31) * 255 / 31;
            image.data[i*4+1] = ((color >> 5) & 63) * 255 / 63;
            image.data[i*4+2] = (color & 31) * 255 / 31;
            image.data[i*4+3] = (mode==='color' || mode==='battery32') ? 255 : color ? 255 : 0;
          }
          ctx.putImageData(image,0,0);
          hostImages.set(Number(sample.index),canvas);
          if (sample.reference) {
            const source = await loadImage(sample.reference);
            const reference=document.createElement('canvas');
            reference.width=300;reference.height=104;
            const referenceContext=reference.getContext('2d');
            referenceContext.drawImage(source,0,0);
            const data=referenceContext.getImageData(0,0,300,104);
            for (let i=0;i<data.data.length;i+=4) {
              data.data[i]=((data.data[i]>>3)*255/31);
              data.data[i+1]=((data.data[i+1]>>2)*255/63);
              data.data[i+2]=((data.data[i+2]>>3)*255/31);
              data.data[i+3]=255;
            }
            referenceContext.putImageData(data,0,0);
            const hostBackground=ctx.getImageData(0,0,1,1).data;
            if (data.data[0]!==hostBackground[0] ||
                data.data[1]!==hostBackground[1] ||
                data.data[2]!==hostBackground[2])
              throw Error(`参考与主机的 RGB565 背景不一致：${role} ${sample.text}`);
            referenceImages.set(Number(sample.index),reference);
          }
        }
        if (mode==='color' || mode==='battery32') {
          const board=document.createElement('canvas');
          board.width=960; board.height=90+samples.length*130;
          const ctx=board.getContext('2d');
          ctx.fillStyle='#10141b'; ctx.fillRect(0,0,board.width,board.height);
          ctx.fillStyle='#e8edf4'; ctx.font='bold 24px Segoe UI'; ctx.fillText(role,16,34);
          ctx.font='14px Segoe UI';ctx.fillStyle='#b1c0d0';
          ctx.fillText(`DOM/CSS 等宽数字 · 两侧 RGB565 量化 · 300×104 · 设计 ${design.size_px}px / TinyTTF ${actualSize}px`,16,60);
          for (let row=0;row<samples.length;row++) {
            const sample=samples[row], y=86+row*130;
            const labels=['浏览器参考字体','真实 TinyTTF RGB565','同底色 50% 叠图'];
            for (let col=0;col<3;col++) {
              const x=16+315*col;
              ctx.fillStyle='#263345';ctx.fillRect(x,y,300,123);
              ctx.font='13px Segoe UI';ctx.fillStyle='#e8edf4';
              ctx.fillText(`${sample.text} · ${labels[col]}`,x+7,y+17);
              ctx.save();ctx.beginPath();ctx.rect(x,y+19,300,104);ctx.clip();
              if (col!==1) ctx.drawImage(referenceImages.get(Number(sample.index)),x,y+19);
              if (col!==0) {
                ctx.globalAlpha=col===2?0.5:1;
                ctx.drawImage(hostImages.get(Number(sample.index)),x+150-195,y+101-220);
                ctx.globalAlpha=1;
              }
              ctx.restore();
            }
          }
          return board.toDataURL('image/png').split(',')[1];
        }
        const board = document.createElement('canvas');
        const rowHeight=role==='FACE_TIME' ? 104 : 74;
        board.width = 960; board.height = 250 + samples.length * rowHeight;
        const ctx = board.getContext('2d');
        ctx.fillStyle='#10141b'; ctx.fillRect(0,0,board.width,board.height);
        ctx.fillStyle='#e8edf4'; ctx.font='bold 25px Segoe UI';
        ctx.fillText(role,18,36);
        ctx.font='15px Segoe UI'; ctx.fillStyle='#aab9ca';
        ctx.fillText(`设计 ${design.size_px}px / ${design.weight} / 字距 ${design.tracking_px}px   ·   TinyTTF ${actualSize}px 主机候选`,18,63);
        const labels=['批准稿文字区域','TinyTTF 候选','50% 叠图'];
        const panelY=91, panelH=125, panelW=300;
        const bounds=design.text_bounds;
        const middleX=bounds.x+bounds.width/2;
        for (let col=0; col<3; col++) {
          const panelX=16+col*315;
          ctx.fillStyle='#253040'; ctx.fillRect(panelX,panelY,panelW,panelH);
          ctx.fillStyle='#e8edf4'; ctx.font='13px Segoe UI';
          ctx.fillText(labels[col],panelX+7,panelY+17);
          ctx.save();
          ctx.beginPath(); ctx.rect(panelX,panelY+23,panelW,panelH-24); ctx.clip();
          if (col !== 1) ctx.drawImage(targetImage,panelX+panelW/2-middleX,
            panelY+90-design.baseline_y);
          if (col !== 0) {
            ctx.globalAlpha=col===2 ? 0.5 : 1;
            ctx.drawImage(hostImages.get(canonical),panelX+panelW/2-195,panelY+90-220);
            ctx.globalAlpha=1;
          }
          ctx.restore();
        }
        ctx.fillStyle='#aab9ca'; ctx.font='13px Segoe UI';
        ctx.fillText('以下均为 390×450 RGB565 主机帧的真实文字；横向居中，基线 220。',18,238);
        for (let row=0; row<samples.length; row++) {
          const sample=samples[row], y=251+row*rowHeight;
          ctx.fillStyle=row%2 ? '#151d28' : '#1c2633';
          ctx.fillRect(16,y,929,rowHeight-6);
          ctx.fillStyle='#e8edf4'; ctx.font='16px Segoe UI';
          ctx.fillText(sample.text,29,y+rowHeight/2+5);
          ctx.save(); ctx.beginPath(); ctx.rect(150,y,490,rowHeight-6); ctx.clip();
          ctx.drawImage(hostImages.get(Number(sample.index)),390-195,y+rowHeight-27-220);
          ctx.restore();
          ctx.font='13px Segoe UI'; ctx.fillStyle='#aab9ca';
          ctx.fillText(`${sample.ink} px  ·  #${sample.index}`,662,y+rowHeight/2+4);
        }
        return board.toDataURL('image/png').split(',')[1];
      }, payload);
      const filename = `${group.role}.png`;
      fs.writeFileSync(path.join(output,filename),Buffer.from(png,'base64'));
      results.push({role:group.role,weight:group.weight,
        font_sha256:fileHash(fontFile),font_bytes:fs.statSync(fontFile).size,
        browser_platform_fonts:platformFonts,browser_reference_style:referenceStyle,
        design_size_px:record.design.size_px,design_tracking_px:record.design.tracking_px,
        tiny_ttf_size_px:mode === 'design' ? record.design.size_px :
          mode === 'battery32' ? 32 : candidateSizes[group.role],
        tiny_ttf_tracking_px:Number(parsed.get(group.canonical).tracking),
        design_baseline_y:record.design.baseline_y,specimen_baseline_y:220,
        reference:group.record,filename,sha256:fileHash(path.join(output,filename)),
        samples:group.indexes.map(index => ({text:parsed.get(index).text,
          ink:parsed.get(index).ink,pixels:Number(parsed.get(index).pixels)}))});
      console.log(`V00 FONT BOARD ${group.role}: ${group.indexes.length} 样例`);
    }
  } finally { await browser.close(); }
  const manifest={status:'candidate_only_not_approved',mode,source:'real LVGL TinyTTF host RGB565',
    typography_package:spec.package_id,designer_approval:spec.approval_id,
    ink_metric:'近白文字 RGB 三通道均 ≥170；有色文字相对前景/背景对比 ≥65%；画面仍保留完整抗锯齿',
    measured_at_design_size:mode === 'design',
    reference_renderer:mode === 'color' || mode === 'battery32' ?
      'DOM/CSS font-variant-numeric:tabular-nums, RGB565 quantized' : null,
    comparison_background:mode === 'color' || mode === 'battery32' ?
      '#17181C quantized to opaque RGB565 on both sides' : null,
    tracking_note:'−4 精确应用；−0.3 因 LVGL 整数字距暂用 0，须审批映射',
    boards:results};
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
}
main().catch(error => {console.error(error);process.exitCode=1;});
