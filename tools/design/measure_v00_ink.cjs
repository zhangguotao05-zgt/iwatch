/* 仅测静态样片指定区域的亮色文字像素，不把像素框冒充字体基线。 */
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '../..');
const targetDir = path.join(root, 'docs/assets/v00-design-handoff/screens');
const hostDir = process.env.V00_HOST_DIR ? path.resolve(process.env.V00_HOST_DIR) :
  path.join(root, 'docs/ui/assets/v00/host');
const output = process.env.V00_TYPE_INK_OUTPUT ? path.resolve(process.env.V00_TYPE_INK_OUTPUT) :
  path.join(root, 'docs/ui/assets/v00/type-ink.json');
const samples = [
  {name: '表盘时间', page: 'Faces.modular', box: [180, 75, 390, 155]},
  {name: '计时器列表标题', page: 'Timers.home', box: [25, 95, 230, 145]},
  {name: '计时器 1', page: 'Timers.home', box: [76, 195, 132, 265]},
  {name: '闹钟小时', page: 'Alarms.edit', box: [90, 202, 174, 292]},
  {name: '闹钟分钟', page: 'Alarms.edit', box: [205, 202, 290, 292]},
  {name: '设置文字大小', page: 'Settings.display', box: [30, 280, 175, 330]},
  {name: '控制中心电量', page: 'System.control', box: [65, 207, 151, 265]}
];

(async () => {
  const browser = await chromium.launch({headless: true,
    executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page = await browser.newPage();
  try {
    const results = [];
    for (const sample of samples) {
      const item = {name: sample.name, page: sample.page, box: sample.box};
      for (const [kind, directory] of [['target', targetDir], ['host', hostDir]]) {
        const source = fs.readFileSync(path.join(directory, `${sample.page}.png`));
        const encoded = source.toString('base64');
        item[kind] = await page.evaluate(async ({encoded, box}) => {
          const image = new Image();
          image.src = `data:image/png;base64,${encoded}`;
          await image.decode();
          const canvas = document.createElement('canvas');
          canvas.width = 390; canvas.height = 450;
          const context = canvas.getContext('2d', {willReadFrequently: true});
          context.drawImage(image, 0, 0);
          const pixels = context.getImageData(0, 0, 390, 450).data;
          let left = 390, top = 450, right = -1, bottom = -1, count = 0;
          for (let y = box[1]; y < box[3]; y++) {
            for (let x = box[0]; x < box[2]; x++) {
              const i = (y * 390 + x) * 4;
              const [r, g, b] = pixels.slice(i, i + 3);
              if (r < 170 || g < 170 || b < 170 ||
                  Math.abs(r - g) > 28 || Math.abs(g - b) > 28) continue;
              left = Math.min(left, x); right = Math.max(right, x);
              top = Math.min(top, y); bottom = Math.max(bottom, y); count++;
            }
          }
          if (!count) throw new Error('指定区域未找到亮色文字像素');
          return {left, top, right, bottom, width: right - left + 1,
            height: bottom - top + 1, lit_pixels: count};
        }, {encoded, box: sample.box});
      }
      results.push(item);
    }
    fs.writeFileSync(output, JSON.stringify({method:
      '指定 ROI 中 RGB 各通道 ≥170、相邻通道差 ≤28 的亮色像素包围框；可含局部高亮边缘，不能代替字体基线',
      samples: results}, null, 2) + '\n');
    console.log(`V00 TYPE INK OK: ${results.length} regions`);
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
