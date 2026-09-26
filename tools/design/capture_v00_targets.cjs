/* 从冻结的视觉审阅台截取六页目标图，不修改已批准资源。 */
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '../..');
const output = path.join(root, 'work/v00/target');
const ids = ['System.grid', 'Faces.modular', 'Timers.home', 'Alarms.edit',
             'Settings.display', 'System.control'];

(async () => {
  fs.mkdirSync(output, {recursive: true});
  const browser = await chromium.launch({headless: true,
    executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  try {
    const page = await browser.newPage({viewport: {width: 1460, height: 900},
      deviceScaleFactor: 1});
    await page.goto(pathToFileURL(path.join(root,
      'docs/ui/Apple_Watch全量UI审阅台_v2.html')).href);
    await page.waitForFunction(() => window.IWUI && window.IWUI.pages.length === 359);
    for (const id of ids) {
      await page.evaluate(pageId => {
        const item = IWUI.pages.find(p => p.id === pageId);
        document.body.innerHTML = IWUI.render(item);
      }, id);
      await page.waitForFunction(() => [...document.images].every(i => i.complete && i.naturalWidth));
      await page.evaluate(() => document.fonts.ready);
      const screen = page.locator('.w-screen');
      await screen.screenshot({path: path.join(output, `${id}.png`)});
      const boxes = await screen.evaluate(element => {
        const base = element.getBoundingClientRect();
        return [...element.querySelectorAll('button,[data-safe],.w-heading,.w-clock')]
          .map(item => {
            const rect = item.getBoundingClientRect();
            const style = getComputedStyle(item);
            return {text: item.textContent.trim().slice(0, 48),
              className: String(item.className || ''),
              x: Math.round(rect.x - base.x), y: Math.round(rect.y - base.y),
              width: Math.round(rect.width), height: Math.round(rect.height),
              color: style.color, background: style.backgroundColor,
              fontPx: style.fontSize};
          }).filter(item => item.width && item.height);
      });
      fs.writeFileSync(path.join(output, `${id}.json`), JSON.stringify({id, boxes}, null, 2));
    }
    console.log(`V00 目标页已截取：${ids.join(', ')}`);
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
