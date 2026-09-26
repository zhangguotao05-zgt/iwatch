/* 只对蜂窝页做本地架构复核；设计包原图与主机帧均不进入固件。 */
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '../..');
const output = path.join(root, 'docs/ui/assets/v00/grid-review');
const reference = fs.readFileSync(path.join(root,
  'docs/assets/v00-design-handoff/screens/System.grid.png'));
const host = fs.readFileSync(path.join(output, 'host.png'));
const hash = data => crypto.createHash('sha256').update(data).digest('hex');

(async () => {
  const browser = await chromium.launch({headless: true,
    executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  try {
    const page = await browser.newPage({viewport: {width: 1230, height: 510},
      deviceScaleFactor: 1});
    const targetUrl = `data:image/png;base64,${reference.toString('base64')}`;
    const hostUrl = `data:image/png;base64,${host.toString('base64')}`;
    await page.setContent(`<style>
      *{box-sizing:border-box}body{margin:0;background:#15171a;color:#e9e9eb;
        font:16px "Microsoft YaHei UI",sans-serif}#comparison{display:flex;gap:15px;
        padding:15px;width:1230px;height:510px}.pane{width:390px}.label{height:30px}
      .image{position:relative;width:390px;height:450px;overflow:hidden}
      img{display:block;width:390px;height:450px}
      .overlay img{position:absolute;inset:0}
      .overlay img:last-child{opacity:.5}
    </style><div id="comparison">
      <div class="pane"><div class="label">批准目标 · System.grid</div>
        <div class="image"><img src="${targetUrl}"></div></div>
      <div class="pane"><div class="label">真实 LVGL 主机 RGB565</div>
        <div class="image"><img src="${hostUrl}"></div></div>
      <div class="pane"><div class="label">50% 叠图</div>
        <div class="image overlay"><img src="${targetUrl}"><img src="${hostUrl}"></div></div>
    </div>`);
    await page.waitForFunction(() => [...document.images].every(i => i.complete && i.naturalWidth));
    const alpha = await page.locator('.overlay img').evaluateAll(nodes =>
      nodes.map(node => getComputedStyle(node).opacity));
    if (alpha[0] !== '1' || alpha[1] !== '0.5')
      throw new Error('叠图必须保持底图不透明、主机图 50% 透明');
    const metrics = await page.evaluate(() => {
      const images = [...document.images];
      const canvas = document.createElement('canvas'); canvas.width=390; canvas.height=450;
      const context = canvas.getContext('2d', {willReadFrequently:true});
      context.drawImage(images[0],0,0);
      const first=context.getImageData(0,0,390,450).data;
      context.clearRect(0,0,390,450); context.drawImage(images[1],0,0);
      const second=context.getImageData(0,0,390,450).data;
      let total=0, changed=0;
      for(let i=0;i<first.length;i+=4){
        const difference=Math.abs(first[i]-second[i])+
          Math.abs(first[i+1]-second[i+1])+Math.abs(first[i+2]-second[i+2]);
        total+=difference; if(difference>60)changed++;
      }
      return {mean_channel_error:+(total/(390*450*3)).toFixed(3),
        changed_pixels_over_20:+(changed/(390*450)).toFixed(4)};
    });
    const composite = await page.locator('#comparison').screenshot();
    fs.writeFileSync(path.join(output,'comparison.png'),composite);
    fs.writeFileSync(path.join(output,'comparison-manifest.json'),
      JSON.stringify({target_sha256:hash(reference),host_sha256:hash(host),
        comparison_sha256:hash(composite),...metrics},null,2)+'\n');
    console.log(`V00 GRID COMPARISON OK: ${JSON.stringify(metrics)}`);
  } finally {await browser.close();}
})().catch(error => {console.error(error);process.exitCode=1;});
