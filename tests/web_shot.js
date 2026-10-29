// The web view as a browser shows it (headless Chromium, with Playwright):
// two pictures of the page's screen, half a second apart. For
// emu/sprite_test.sh.
//
//   node tests/web_shot.js URL FIRST.png SECOND.png
const { chromium } = require("playwright");

(async () => {
  const [url, first, second] = process.argv.slice(2);
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1000, height: 800 } });
  await page.goto(url);
  // the screen's picture, and the sprites placed over it
  await page.waitForFunction(() => document.getElementById("s").naturalWidth > 0, null,
                             { timeout: 15000 });
  await page.waitForTimeout(1500);
  const wrap = await page.$("#wrap");
  await wrap.screenshot({ path: first });
  await page.waitForTimeout(500);
  await wrap.screenshot({ path: second });
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
