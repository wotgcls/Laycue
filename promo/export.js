// Renders the loop frame by frame (deterministic: seek(t) draws the exact frame for time t), 30 fps, at 2x
// for crisp edges. Usage: node export.js <outDir> [fps]
const { chromium } = require('/opt/node22/lib/node_modules/playwright');
(async () => {
  const out = process.argv[2], fps = Number(process.argv[3] || 30);
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 2 });
  p.on('pageerror', e => console.log('page error:', e.message));
  await p.goto('file://' + __dirname + '/prototype.html?export=1');
  await p.waitForFunction('window.__ready === true', null, { timeout: 20000 });
  const loop = await p.evaluate('window.LOOP'), n = Math.round(loop * fps);
  for (let i = 0; i < n; i++) {
    await p.evaluate(t => window.seek(t), i / fps);
    await p.screenshot({ path: `${out}/f${String(i).padStart(4, '0')}.png` });
    if (i % 50 === 0) console.log('frame', i, '/', n);
  }
  await b.close(); console.log('done', n, 'frames');
})();
