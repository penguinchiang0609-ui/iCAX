// Regenerate PNG/ICO deliverables from the canonical vector, not a raster mockup.
// ICAX_PLAYWRIGHT_MODULE may point to a bundled Playwright installation.
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { tubeDesignerSvg } from '../webpage/branding.mjs';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = new URL('../webpage/assets/', import.meta.url);
await mkdir(output, { recursive: true });
const svg = tubeDesignerSvg();
await writeFile(new URL('tube-designer.svg', output), svg);
await writeFile(new URL('tube-designer-mark.svg', output), tubeDesignerSvg({ tile: false }));
await writeFile(new URL('tube-designer-logo.svg', output), `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 760 160" role="img" aria-label="TubeDesigner"><svg width="160" height="160">${svg.replace(/^<svg[^>]*>|<\/svg>$/g, '')}</svg><text x="184" y="103" fill="#06233d" font-family="Segoe UI,Arial,sans-serif" font-size="76" letter-spacing="-2"><tspan font-weight="700">Tube</tspan><tspan font-weight="400">Designer</tspan></text></svg>`.replace('<svg width="160" height="160">', '<svg width="160" height="160" viewBox="0 0 256 256">'));
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
try {
  const page = await browser.newPage({ deviceScaleFactor: 1 });
  const sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256];
  for (const [name, art, destination] of [
    ['tube-designer', svg, output],
    ['icax', await readFile(new URL('../../branding/icax.svg', import.meta.url), 'utf8'), new URL('../../branding/', import.meta.url)],
  ]) {
  const frames = [];
  for (const size of [...sizes, 512]) {
    await page.setViewportSize({ width: size, height: size });
    await page.setContent(`<style>html,body{margin:0;background:transparent}svg{display:block;width:100%;height:100%}</style>${art}`);
    const png = await page.screenshot({ omitBackground: true });
    await writeFile(new URL(`${name}-${size}.png`, destination), png);
    if (size <= 256) frames.push({ size, png });
  }
  const header = Buffer.alloc(6 + frames.length * 16);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(frames.length, 4);
  let offset = header.length;
  frames.forEach(({ size, png }, index) => {
    const at = 6 + index * 16;
    header[at] = header[at + 1] = size === 256 ? 0 : size;
    header.writeUInt16LE(1, at + 4);
    header.writeUInt16LE(32, at + 6);
    header.writeUInt32LE(png.length, at + 8);
    header.writeUInt32LE(offset, at + 12);
    offset += png.length;
  });
  await writeFile(new URL(`${name}.ico`, destination), Buffer.concat([header, ...frames.map(frame => frame.png)]));
  console.log(`Generated ${name} PNGs and ICO in ${destination.pathname}`);
  }
} finally {
  await browser.close();
}
