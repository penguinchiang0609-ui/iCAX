import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { renderProfileLibraryLeftPane, libraryProfiles } from '../../apps/tube-designer/webpage/profileLibrary.mjs';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const root = new URL('../../apps/tube-designer/templates/profile/', import.meta.url);
const profiles = readdirSync(root, { withFileTypes: true }).filter(e => e.isDirectory() && !e.name.startsWith('_')).map(e => {
  const descriptor = JSON.parse(readFileSync(new URL(`${e.name}/profile.json`, root), 'utf8'));
  return { id: descriptor.id, name: descriptor.displayName['zh-CN'], descriptor,
    profileForm: 'parametric', profileType: 'profile-package' };
});
const ordered=libraryProfiles({tubeDesignerSystemProfiles:profiles});
assert.deepEqual([...new Set(ordered.map(p=>p.descriptor.category))], ['常用管材','常用型材','其他']);
assert.deepEqual(ordered.slice(0,4).map(p=>p.descriptor.displayName), ['方管','圆管','腰型管','椭圆管']);
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
try {
  const page = await browser.newPage();
  for (const height of [400, 650, 900]) {
    await page.setViewportSize({ width: 1000, height });
    await page.setContent(`<style>body{margin:0}#host{width:360px;height:${height}px}${tubeDesignerCss}</style><div id="host">${renderProfileLibraryLeftPane({}, {tubeDesignerSystemProfiles:profiles})}</div>`);
    const result = await page.evaluate(() => {
      const list = document.querySelector('.tube-profile-library-list');
      const cards = [...list.querySelectorAll('.tube-profile-library-card')];
      const clipped = cards.filter(card => {
        const group = card.closest('.tube-profile-library-group').getBoundingClientRect();
        const rect = card.getBoundingClientRect();
        return rect.bottom > group.bottom + 1 || rect.height < 59;
      }).length;
      list.scrollTop = list.scrollHeight;
      const last = cards.at(-1).getBoundingClientRect();
      const bounds = list.getBoundingClientRect();
      return { clipped, count:cards.length, scrollable:list.scrollHeight > list.clientHeight,
        reachable:last.bottom <= bounds.bottom + 1 && last.top >= bounds.top - 1,
        bounded:bounds.bottom <= window.innerHeight + 1 };
    });
    assert.equal(result.count, profiles.length);
    assert.equal(result.clipped, 0, `clipped groups at ${height}px`);
    assert.ok(result.scrollable && result.reachable && result.bounded, JSON.stringify(result));
  }
  console.log('Profile scroll layout passed at 400/650/900px with all catalog entries.');
} finally { await browser.close(); }
