/* 对照图只用于本地视觉审阅；原厂参考图不进入固件资源。 */
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '../..');
const targetDir = path.join(root, 'docs/assets/v00-design-handoff/screens');
const hostDir = process.env.V00_HOST_DIR ? path.resolve(process.env.V00_HOST_DIR) :
  path.join(root, 'docs/ui/assets/v00/host');
const outputDir = process.env.V00_COMPARE_DIR ? path.resolve(process.env.V00_COMPARE_DIR) :
  path.join(root, 'docs/ui/assets/v00/compare');
const ids = process.env.V00_RUNTIME === '1' ?
  ['System.grid.runtime', 'Faces.modular.runtime', 'Timers.home.runtime',
   'Alarms.edit.runtime', 'Settings.display.runtime',
   'Timers.home.scroll-end.runtime', 'Settings.display.scroll-end.runtime'] :
  ['System.grid', 'Faces.modular', 'Timers.home', 'Alarms.edit',
   'Settings.display', 'System.control',
   'Timers.home.scroll-end', 'Settings.display.scroll-end'];
const hash = buffer => crypto.createHash('sha256').update(buffer).digest('hex');

(async () => {
  const inputs = ids.map(id => ({id,
    target: fs.readFileSync(path.join(targetDir, `${id.replace('.runtime', '')}.png`)),
    host: fs.readFileSync(path.join(hostDir, `${id}.png`))}));
  fs.mkdirSync(outputDir, {recursive: true});
  const browser = await chromium.launch({headless: true,
    executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const manifest = [];
  try {
    const page = await browser.newPage({viewport: {width: 1230, height: 510},
      deviceScaleFactor: 1});
    for (const input of inputs) {
      const reference = `data:image/png;base64,${input.target.toString('base64')}`;
      const current = `data:image/png;base64,${input.host.toString('base64')}`;
      await page.setContent(`<style>
        *{box-sizing:border-box}body{margin:0;background:#15171a;color:#e9e9eb;
          font:16px "Microsoft YaHei UI",sans-serif}#comparison{display:flex;gap:15px;
          padding:15px;width:1230px;height:510px}.pane{width:390px}.label{height:30px}
        .image{position:relative;width:390px;height:450px;overflow:hidden}
        img{display:block;width:390px;height:450px}.image.overlay img{position:absolute;inset:0}
        .image.overlay img:last-child{opacity:.5}
      </style><div id="comparison">
        <div class="pane"><div class="label">批准目标 · ${input.id}</div><div class="image"><img src="${reference}"></div></div>
        <div class="pane"><div class="label">固件主机 RGB565</div><div class="image"><img src="${current}"></div></div>
        <div class="pane"><div class="label">50% 叠图 · 偏差可见</div><div class="image overlay"><img src="${reference}"><img src="${current}"></div></div>
      </div>`);
      await page.waitForFunction(() => [...document.images].every(i => i.complete && i.naturalWidth));
      const alpha = await page.locator('.overlay img').evaluateAll(nodes =>
        nodes.map(node => getComputedStyle(node).opacity));
      if (alpha[0] !== '1' || alpha[1] !== '0.5')
        throw new Error(`${input.id}: 叠图透明度不符合审阅规则`);
      const metrics = await page.evaluate(() => {
        const images = [...document.images];
        const canvas = document.createElement('canvas');canvas.width=390;canvas.height=450;
        const context = canvas.getContext('2d', {willReadFrequently:true});
        context.drawImage(images[0],0,0);const reference=context.getImageData(0,0,390,450).data;
        context.clearRect(0,0,390,450);context.drawImage(images[1],0,0);
        const current=context.getImageData(0,0,390,450).data;
        let total=0, changed=0;
        for(let i=0;i<reference.length;i+=4){
          const diff=Math.abs(reference[i]-current[i])+
            Math.abs(reference[i+1]-current[i+1])+Math.abs(reference[i+2]-current[i+2]);
          total+=diff;if(diff>60)changed++;
        }
        return {mean_channel_error:+(total/(390*450*3)).toFixed(2),
          changed_pixels_over_20:+(changed/(390*450)).toFixed(4)};
      });
      const png = await page.locator('#comparison').screenshot();
      fs.writeFileSync(path.join(outputDir, `${input.id}.png`), png);
      manifest.push({id:input.id, target_sha256:hash(input.target),
        host_sha256:hash(input.host), comparison_sha256:hash(png), ...metrics});
    }
  } finally {
    await browser.close();
  }
  fs.writeFileSync(path.join(outputDir, 'manifest.json'),
    JSON.stringify({note:'像素差仅用于定位视觉偏差，不能替代逐区域验收',samples:manifest},null,2)+'\n');
  console.log(`V00 COMPARISON OK: ${manifest.length} pages`);
})().catch(error => {console.error(error);process.exitCode=1;});
