// Real browser and production chooser; native directory/preview transport is
// controlled to exercise delayed responses and user interaction deterministically.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('nesting-folder-navigation-20261007');
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
const checks = [], errors = [];
const check = (actual, expected, name) => { assert.deepEqual(actual, expected, name); checks.push(name); };
let page;
try {
  page = await browser.newPage({ viewport: { width: 1440, height: 960 } });
  page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://folder-picker.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><style>body{margin:0;font:14px Segoe UI}.tube-designer-modal-backdrop{position:fixed;inset:0;display:grid;place-items:center;background:#0006}</style><div id="app"><input id="original" value="原来的字段"></div>' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(path.slice(5)), 'utf8') });
  });
  await page.goto('http://folder-picker.test/');
  await page.evaluate(async () => {
    localStorage.clear();
    const { chooseNestingPartFiles } = await import('/src/apps/tube-designer/webpage/nestingPartFilePicker.mjs');
    const directory = path => ({ path, name: path.split('/').at(-1), directory: true });
    const file = path => ({ path, name: path.split('/').at(-1), sizeBytes: 100, lastModifiedNs: '1000' });
    const normalize = path => path.replaceAll('\\', '/').toLowerCase();
    const root = 'D:/CAD', target = root + '/group-25';
    const f = window.folderFixture = { root, target, calls: [], hold: '', fail: '', pending: null, view: { activeAreaId: 'nesting' } };
    f.result = requested => {
      const path = normalize(requested);
      let entries;
      if (path === normalize(root)) entries = [
        ...Array.from({ length: 32 }, (_, i) => directory(`${root}/group-${String(i + 1).padStart(2, '0')}`)),
        file(`${root}/root.step`), { path: `${root}/notes.txt`, name: 'notes.txt' },
      ];
      else if (path === normalize(target)) entries = [directory(target + '/parts'), file(target + '/a.step'), file(target + '/b.step')];
      else if (path === normalize(target + '/parts')) entries = [file(target + '/parts/c.step')];
      else entries = [file(requested + '/child.step')];
      return { directory: requested, parentDirectory: requested.slice(0, requested.lastIndexOf('/')), entries };
    };
    f.context = { mount: document.querySelector('#app'), appProxy: { bridge: { async openDirectoryDialog() { return target; } } }, sceneProxy: { async invoke(method, payload = {}) {
      f.calls.push({ method, payload });
      if (method === 'TubeDesigner.ListNestingPartFiles') {
        const directory = payload.directory || root;
        if (f.fail === directory) { f.fail = ''; throw Error('目录暂时无法读取'); }
        if (f.hold === directory) return new Promise(resolve => { f.pending = { resolve, directory }; });
        return f.result(directory);
      }
      if (method === 'TubeDesigner.PreviewNestingPartFile') return { sourcePath: payload.sourcePath, sizeBytes: 100, lastModifiedNs: '1000',
        wireframe: { polylines: [[-100,-10,0,100,-10,0,100,10,0,-100,10,0,-100,-10,0]], edgeCount: 1, pointCount: 5 } };
      if (method === 'TubeDesigner.ReleaseNestingPartFilePreview') return { released: true };
      throw Error('Unexpected native method: ' + method);
    } } };
    f.finish = () => { const pending = f.pending; f.hold = ''; f.pending = null; pending.resolve(f.result(pending.directory)); };
    f.open = () => { f.choice = chooseNestingPartFiles(f.context, f.view); };
    f.open();
  });
  const pathRow = path => page.locator(`[data-nesting-file-path="${path}"]`);
  const root = 'D:/CAD', target = root + '/group-25';
  const row = pathRow(target);
  const toggle = path => pathRow(path).locator('..').locator('[data-nesting-file-action="toggle-folder"]');
  const list = page.locator('.tube-nesting-file-list');
  const input = page.locator('[data-nesting-file-directory]');
  const open = page.locator('[data-nesting-file-action="open"]');
  const selected = () => page.locator('.tube-nesting-file-row[aria-selected="true"]').evaluateAll(rows => rows.map(row => row.dataset.nestingFilePath));
  const at = path => page.waitForFunction(path => folderFixture.view.tubeDesignerNestingFilePicker.directory === path
    && !document.querySelector('.tube-nesting-file-list').hasAttribute('aria-busy'), path);
  const ready = path => page.waitForFunction(path => folderFixture.view.tubeDesignerNestingFilePicker.selectedPath === path
    && folderFixture.view.tubeDesignerNestingFilePicker.previewStatus === 'ready', path);
  const directoryCalls = path => page.evaluate(path => folderFixture.calls.filter(call => call.method === 'TubeDesigner.ListNestingPartFiles' && call.payload.directory === path).length, path);
  await row.waitFor();
  check(await page.locator('[data-nesting-file-action="browse"]').textContent(), '浏览…', 'Browse label replaces Select Directory');
  check(await page.locator('.tube-nesting-file-navigation-button svg').count(), 2, 'Up and Browse each have a vector icon');
  check(await row.locator('svg .folder-closed').isVisible(), true, 'folder rows use a proper closed-folder icon');
  await row.click();
  check(await selected(), [target], 'single click selects a directory without navigating or importing');
  check(await input.inputValue(), root, 'folder selection keeps current directory');
  check(await open.isDisabled(), true, 'selected folder never enables CAD import');
  await toggle(target).click();
  await pathRow(target + '/a.step').waitFor();
  check(await toggle(target).getAttribute('aria-expanded'), 'true', 'disclosure announces expanded state');
  check(await row.locator('svg .folder-open').isVisible(), true, 'expanded branch shows open-folder icon');
  check(await input.inputValue(), root, 'expansion leaves navigation path unchanged');
  await pathRow(target + '/a.step').click(); await ready(target + '/a.step');
  await pathRow(target + '/b.step').click({ modifiers: ['Control'] }); await ready(target + '/b.step');
  await page.evaluate(() => {
    folderFixture.input = document.querySelector('[data-nesting-file-directory]');
    folderFixture.svg = document.querySelector('[data-nesting-wireframe]');
    folderFixture.svgClicks = 0;
    folderFixture.svg.addEventListener('custom-check', () => folderFixture.svgClicks++);
  });
  await toggle(target + '/parts').click(); await pathRow(target + '/parts/c.step').waitFor();
  check(await page.evaluate(() => folderFixture.input === document.querySelector('[data-nesting-file-directory]')
    && folderFixture.svg === document.querySelector('[data-nesting-wireframe]')), true, 'nested expansion preserves input and preview nodes');
  await pathRow(target + '/parts/c.step').click({ modifiers: ['Control'] }); await ready(target + '/parts/c.step');
  check(await open.textContent(), '打开（3）', 'multi-selection includes files in nested expanded branches');
  await page.screenshot({ path: resolve(output, 'expanded.png') });
  await toggle(target).click();
  check(await pathRow(target + '/a.step').isVisible(), false, 'collapse hides descendants');
  check(await open.textContent(), '打开（3）', 'collapse retains explicitly selected files');
  check(await page.evaluate(() => folderFixture.view.tubeDesignerNestingFilePicker.previewStatus), 'ready', 'collapse does not discard current wireframe preview');
  await row.press('Control+a');
  check(await selected(), [root + '/root.step'], 'Select All excludes collapsed descendants and folders');
  const callsBefore = await directoryCalls(target);
  await row.press('ArrowRight'); await pathRow(target + '/a.step').waitFor();
  check(await directoryCalls(target), callsBefore, 're-expanding a loaded branch avoids native directory reload');
  await row.press('ArrowLeft');
  check(await toggle(target).getAttribute('aria-expanded'), 'false', 'Left arrow collapses folder');
  await row.press('ArrowRight'); await row.press('ArrowRight');
  check(await page.evaluate(() => document.activeElement.dataset.nestingFilePath), target + '/parts', 'Right arrow focuses first child of an expanded folder');

  // Late branch data must not open a collapsed branch, replace other nodes,
  // restore an old input selection, or scroll after the user's newer actions.
  const slow = root + '/group-10';
  await page.evaluate(path => { folderFixture.hold = path; }, slow);
  await toggle(slow).click(); await page.waitForFunction(() => !!folderFixture.pending);
  await toggle(slow).click();
  await input.fill('D:/用户正在编辑');
  const interaction = await page.evaluate(() => {
    const input = document.querySelector('[data-nesting-file-directory]');
    input.focus(); input.setSelectionRange(3, 8, 'backward');
    document.querySelector('.tube-nesting-file-list').scrollTop = 170;
    return { value: input.value, start: input.selectionStart, end: input.selectionEnd, direction: input.selectionDirection,
      scroll: document.querySelector('.tube-nesting-file-list').scrollTop };
  });
  await page.evaluate(() => folderFixture.finish());
  await page.waitForFunction(path => !!document.querySelector(`[data-nesting-file-path="${path}/child.step"]`), slow);
  check(await pathRow(slow + '/child.step').isVisible(), false, 'late expansion response cannot reopen collapsed branch');
  check(await page.evaluate(() => {
    const input = document.querySelector('[data-nesting-file-directory]');
    return { value: input.value, start: input.selectionStart, end: input.selectionEnd, direction: input.selectionDirection,
      scroll: document.querySelector('.tube-nesting-file-list').scrollTop };
  }), interaction, 'late hidden-branch response preserves latest caret, selection and scroll');
  check(await page.evaluate(() => document.activeElement === folderFixture.input), true, 'late branch response preserves active input');
  check(await page.evaluate(() => {
    folderFixture.svg.dispatchEvent(new Event('custom-check'));
    return folderFixture.svgClicks === 1 && folderFixture.svg === document.querySelector('[data-nesting-wireframe]');
  }), true, 'preview node and its event listener survive branch updates');

  const failed = root + '/group-09';
  await page.evaluate(path => { folderFixture.fail = path; }, failed);
  await toggle(failed).click();
  await pathRow(failed).locator('../..').getByRole('status').filter({ hasText: '目录暂时无法读取' }).waitFor();
  await toggle(failed).click(); await toggle(failed).click(); await pathRow(failed + '/child.step').waitFor();
  check(await pathRow(failed + '/child.step').isVisible(), true, 'failed folder read retries on next expansion');

  await row.dblclick(); await at(target);
  await pathRow(target + '/parts').dblclick(); await at(target + '/parts');
  await page.locator('[data-nesting-file-action="up"]').click(); await at(target);
  check(await selected(), [target + '/parts'], 'Up selects the exact nested directory just exited');
  check(await open.isDisabled(), true, 'Up does not restore previously selected CAD files');
  await page.locator('[data-nesting-file-action="up"]').click(); await at(root);
  check(await selected(), [target], 'Up selects the exact root folder just exited');
  check(await row.evaluate(row => { const list = row.closest('.tube-nesting-file-list'), a = row.getBoundingClientRect(), b = list.getBoundingClientRect();
    return list.scrollTop > 0 && a.top >= b.top - 1 && a.bottom <= b.bottom + 1; }), true, 'Up scrolls selected folder into view');
  check(await row.evaluate(row => document.activeElement === row), true, 'Up focuses returned folder for immediate keyboard navigation');
  await page.screenshot({ path: resolve(output, 'up-selected.png') });

  // A later input edit takes precedence over delayed Up focus/scroll restoration.
  await row.dblclick(); await at(target);
  await page.evaluate(() => { folderFixture.hold = folderFixture.root; });
  await page.locator('[data-nesting-file-action="up"]').click(); await page.waitForFunction(() => !!folderFixture.pending);
  await input.fill('D:/新的路径草稿');
  await input.evaluate(input => input.setSelectionRange(2, 7));
  await page.evaluate(() => folderFixture.finish()); await at(root);
  check(await input.evaluate(input => ({ value: input.value, start: input.selectionStart, end: input.selectionEnd, focused: document.activeElement === input })),
    { value: 'D:/新的路径草稿', start: 2, end: 7, focused: true }, 'delayed Up cannot override newer input focus or draft');
  check(await selected(), [target], 'returned directory remains selected even when input is being edited');
  check(await list.evaluate(list => list.scrollTop), 0, 'delayed Up does not scroll after intervening input');

  await page.evaluate(path => { folderFixture.hold = path; }, slow);
  await toggle(slow).click(); await page.waitForFunction(() => !!folderFixture.pending);
  await input.fill(target + '/parts'); await input.press('Enter'); await at(target + '/parts');
  await page.evaluate(() => folderFixture.finish());
  await page.waitForTimeout(50);
  check(await pathRow(slow + '/child.step').count(), 0, 'old branch response cannot insert rows into another directory');
  check(await input.inputValue(), target + '/parts', 'old branch response cannot overwrite new navigation');
  await page.locator('footer [data-nesting-file-action="cancel"]').click();
  check(await page.evaluate(() => folderFixture.choice), [], 'Cancel returns no selected folder or file');
  check(errors, [], 'no uncaught browser errors');
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, checks, nativeTransport: 'controlled', productionChooser: true }, null, 2));
  console.log(`Folder navigation browser: passed (${checks.length} checks).`);
} catch (error) {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: false, checks, errors, error: error.stack }, null, 2));
  await page?.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {});
  throw error;
} finally { await browser.close(); }
