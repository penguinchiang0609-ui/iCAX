// Render the canonical vector at native Windows icon sizes.
import { readFile, writeFile } from 'node:fs/promises';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = new URL('../webpage/assets/', import.meta.url);
const svg = await readFile(new URL('five-axis-cam.svg', output), 'utf8');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
try {
  const page = await browser.newPage({ deviceScaleFactor: 1 });
  const frames = [];
  for (const size of [16, 20, 24, 32, 40, 48, 64, 128, 256]) {
    await page.setViewportSize({ width: size, height: size });
    await page.setContent(`<style>html,body{margin:0;background:transparent}svg{display:block;width:100%;height:100%}</style>${svg}`);
    const png = await page.screenshot({ omitBackground: true });
    if (size === 256) await writeFile(new URL('five-axis-cam.png', output), png);
    frames.push({ size, png });
  }
  const header = Buffer.alloc(6 + frames.length * 16);
  header.writeUInt16LE(1, 2); header.writeUInt16LE(frames.length, 4);
  let offset = header.length;
  frames.forEach(({ size, png }, index) => {
    const at = 6 + index * 16;
    header[at] = header[at + 1] = size === 256 ? 0 : size;
    header.writeUInt16LE(1, at + 4); header.writeUInt16LE(32, at + 6);
    header.writeUInt32LE(png.length, at + 8); header.writeUInt32LE(offset, at + 12);
    offset += png.length;
  });
  await writeFile(new URL('five-axis-cam.ico', output), Buffer.concat([header, ...frames.map(frame => frame.png)]));
  console.log('Generated FiveAxisCAM icon (16–256 px).');
} finally { await browser.close(); }
