// Production chooser and batch-import controller in a real browser. Native
// transport is controlled here; standalone CEF/native acceptance is separate.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('nesting-file-multiselect-20261007');
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
const checks = [], errors = [];
const check = (actual, expected, name) => { assert.deepEqual(actual, expected, name); checks.push(name); };
const paths = Array.from({ length: 6 }, (_, index) => `D:/CAD/${index + 1}_tube.step`);
let page;
try {
  page = await browser.newPage({ viewport: { width: 1440, height: 960 } });
  page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://multi-picker.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><style>body{margin:0;font:14px Segoe UI}.tube-designer-modal-backdrop{position:fixed;inset:0;display:grid;place-items:center;background:#0006}</style><div id="app"><input id="original" value="编辑中的值"></div>' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(path.slice(5)), 'utf8') });
  });
  await page.goto('http://multi-picker.test/');
  await page.evaluate(async paths => {
    localStorage.clear();
    const picker = await import('/src/apps/tube-designer/webpage/nestingPartFilePicker.mjs');
    const importer = await import('/src/apps/tube-designer/webpage/nestingPartImport.mjs');
    const f = window.multiFixture = { paths, calls: [], choices: [], imports: [], notices: [], parts: [], refreshes: 0, renders: 0,
      view: { activeAreaId: 'nesting', scene: { tubeDesigner: {} } } };
    const proxy = { async invoke(method, payload = {}) {
      f.calls.push({ method, payload });
      if (method === 'TubeDesigner.ListNestingPartFiles') return { directory: payload.directory || 'D:/CAD', parentDirectory: 'D:/',
        entries: payload.directory === 'D:/empty' ? [] : [
          { path: 'D:/empty', name: 'empty', directory: true },
          ...paths.map((path, i) => ({ path, name: `${i + 1}_tube.step`, sizeBytes: 500, lastModifiedNs: '1001' })),
          { path: 'D:/CAD/readme.txt', name: 'readme.txt' } ] };
      if (method === 'TubeDesigner.PreviewNestingPartFile') return { sourcePath: payload.sourcePath, sizeBytes: 500, lastModifiedNs: '1001',
        wireframe: { polylines: [[-100,-10,0,100,-10,0,100,10,0,-100,10,0,-100,-10,0]], edgeCount: 1, pointCount: 5 } };
      if (method === 'TubeDesigner.ReleaseNestingPartFilePreview') return { released: true };
      if (method === 'TubeDesigner.ImportNestingPart') {
        if (f.pendingImport) throw Error('Concurrent imports are forbidden');
        return new Promise((resolve, reject) => { f.pendingImport = { payload, resolve, reject }; f.imports.push(payload); });
      }
      throw Error(`Unexpected scene method: ${method}`);
    } };
    f.context = { mount: document.querySelector('#app'), sceneProxy: proxy,
      actions: { async refreshActiveSceneState() { f.refreshes++; } } };
    f.ops = { renderProject() { f.renders++; }, showNotice(context, view, text) { f.notices.push(text); } };
    f.openPicker = () => { f.choice = picker.chooseNestingPartFiles(f.context, f.view); void f.choice.then(paths => f.choices.push(paths)); };
    f.openImport = () => { f.importDone = false; f.importError = '';
      f.job = importer.handleNestingPartImportRibbonCommand(f.context, f.view, 'nesting.import-part', f.ops)
        .catch(error => { f.importError = error.message; }).finally(() => { f.importDone = true; }); };
    f.finishImport = (fail = false) => {
      const pending = f.pendingImport; f.pendingImport = null;
      if (fail) { pending.reject(Error('Invalid CAD fixture')); return; }
      const partEntityId = 'part-' + (f.parts.length + 1);
      f.parts.push({ entityId: partEntityId, name: pending.payload.name, quantity: pending.payload.quantity });
      pending.resolve({ partEntityId, sourceFileName: pending.payload.sourcePath.split('/').at(-1),
        tubeDesigner: { nestingGroups: [{ parts: structuredClone(f.parts) }] } });
    };
    f.openPicker();
  }, paths);
  const row = index => page.locator(`[data-nesting-file-path="${paths[index]}"]`);
  const selected = () => page.locator('.tube-nesting-file-row[aria-selected="true"]').evaluateAll(rows => rows.map(row => row.dataset.nestingFilePath));
  const ready = index => page.waitForFunction(path => multiFixture.view.tubeDesignerNestingFilePicker?.selectedPath === path
    && multiFixture.view.tubeDesignerNestingFilePicker.previewStatus === 'ready', paths[index]);
  const open = page.locator('[data-nesting-file-action="open"]');
  await row(0).waitFor();
  check(await page.locator('[data-nesting-file-action="go"]').count(), 0, 'redundant Go button removed');
  check(await page.locator('.tube-nesting-file-list').getAttribute('aria-multiselectable'), 'true', 'list announces multiple selection');
  await row(0).click(); await ready(0);
  await row(2).click({ modifiers: ['Control'] }); await ready(2);
  check(await selected(), [paths[0], paths[2]], 'Ctrl adds independent files');
  check(await open.textContent(), '打开（2）', 'Open shows selected count');
  await row(2).click({ modifiers: ['Control'] });
  check(await selected(), [paths[0]], 'Ctrl toggles current preview file off');
  await row(0).click({ modifiers: ['Control'] });
  check(await selected(), [], 'Ctrl deselects last file');
  check(await open.isDisabled(), true, 'empty selection cannot open even while preview exists');
  await row(1).click(); await row(4).click({ modifiers: ['Shift'] });
  check(await selected(), paths.slice(1, 5), 'Shift selects contiguous range');
  await row(0).click({ modifiers: ['Control', 'Shift'] });
  check(await selected(), paths.slice(0, 5), 'Ctrl+Shift adds a range without losing existing files');
  await row(5).click();
  check(await selected(), [paths[5]], 'ordinary click resets selection');
  await row(5).press('Control+a'); await ready(5);
  check(await selected(), paths, 'Ctrl+A selects only CAD files, excluding folders and other formats');
  const count = await page.evaluate(() => multiFixture.calls.filter(call => call.method === 'TubeDesigner.PreviewNestingPartFile').length);
  await page.waitForTimeout(180);
  check(await page.evaluate(() => multiFixture.calls.filter(call => call.method === 'TubeDesigner.PreviewNestingPartFile').length), count, 'select all previews only the current file');
  await page.locator('[data-nesting-file-directory]').press('Control+a');
  check(await selected(), paths, 'Ctrl+A in path input retains native text selection');
  await page.screenshot({ path: resolve(output, 'multi-selected.png') });
  await page.locator('footer [data-nesting-file-action="cancel"]').click();
  await page.waitForFunction(() => multiFixture.choices.length === 1);
  check(await page.evaluate(() => multiFixture.choices[0]), [], 'Cancel returns no files');
  await page.evaluate(() => multiFixture.openPicker()); await ready(5);
  check(await selected(), paths, 'reopening remembers the exact selected files and current preview');
  await row(1).click(); await row(4).click({ modifiers: ['Control'] });
  await open.click(); await page.waitForFunction(() => multiFixture.choices.length === 2);
  check(await page.evaluate(() => multiFixture.choices[1]), [paths[1], paths[4]], 'Open returns selected paths in visible order');

  // Directory navigation discards selection; Enter replaces the deleted Go control.
  await page.evaluate(() => multiFixture.openPicker()); await row(1).waitFor();
  await page.locator('[data-nesting-file-directory]').fill('D:/empty');
  await page.locator('[data-nesting-file-directory]').press('Enter');
  await page.waitForFunction(() => multiFixture.view.tubeDesignerNestingFilePicker?.directory === 'D:/empty');
  check(await selected(), [], 'directory navigation clears file selection');
  check(await open.isDisabled(), true, 'empty directory cannot open old files');
  await page.locator('[data-nesting-file-directory]').fill('D:/CAD');
  await page.locator('[data-nesting-file-directory]').press('Enter'); await row(1).waitFor();
  await row(1).click(); await row(1).press('Shift+ArrowDown');
  check(await selected(), paths.slice(1, 3), 'Shift+arrow extends file selection');
  await page.locator('footer [data-nesting-file-action="cancel"]').click();

  // The actual production import controller receives the chooser result.
  await page.evaluate(() => multiFixture.openImport()); await row(0).waitFor();
  await row(0).click(); await row(2).click({ modifiers: ['Shift'] });
  await open.click(); await page.waitForFunction(() => !!multiFixture.pendingImport);
  check(await page.evaluate(() => ({ pending: multiFixture.view.pending, calls: multiFixture.imports })),
    { pending: true, calls: [{ sourcePath: paths[0], name: '1_tube', material: '', quantity: 1 }] }, 'batch starts one native import with direct metadata');
  await page.evaluate(() => multiFixture.openImport());
  check(await page.locator('.tube-nesting-file-picker').count(), 0, 'reentry cannot open another picker during batch');
  await page.evaluate(() => multiFixture.finishImport());
  await page.waitForFunction(() => multiFixture.imports.length === 2);
  check(await page.evaluate(() => multiFixture.view.pending), true, 'pending stays set between files');
  await page.evaluate(() => multiFixture.finishImport(true));
  await page.waitForFunction(() => multiFixture.imports.length === 3);
  await page.evaluate(() => multiFixture.finishImport());
  await page.waitForFunction(() => !multiFixture.view.pending && !multiFixture.view.tubeDesignerNestingImportChoosing);
  check(await page.evaluate(() => multiFixture.imports.map(part => part.sourcePath)), paths.slice(0, 3), 'imports run sequentially and continue after individual failure');
  check(await page.evaluate(() => multiFixture.view.tubeDesignerNestingSelectedPartIds), ['part-1', 'part-2'], 'all successful parts remain selected');
  check(await page.evaluate(() => multiFixture.view.tubeDesignerActiveNestingPartId), 'part-2', 'last successful part becomes current');
  check(await page.evaluate(() => multiFixture.refreshes), 1, 'batch refreshes native scene once after completion');
  check(await page.evaluate(() => multiFixture.notices.some(text => text.includes('2_tube.step') && text.includes('Invalid CAD'))), true, 'failed filename and reason are reported');
  check(await page.evaluate(() => multiFixture.view.tubeDesignerOperation), null, 'batch releases operation state');

  // Switching the scene during native work must stop the remaining queue.
  await page.evaluate(() => multiFixture.openImport()); await row(0).waitFor();
  await row(0).click(); await row(1).click({ modifiers: ['Shift'] }); await open.click();
  await page.waitForFunction(() => multiFixture.imports.length === 4);
  await page.evaluate(() => {
    multiFixture.oldScene = multiFixture.view.scene;
    multiFixture.context.sceneProxy = { async invoke() { throw Error('Must not import into replacement project'); } };
    multiFixture.finishImport();
  });
  await page.waitForFunction(() => !multiFixture.view.pending);
  check(await page.evaluate(() => multiFixture.imports.length), 4, 'context replacement stops queued imports');
  check(await page.evaluate(() => multiFixture.view.scene === multiFixture.oldScene), true, 'late response cannot overwrite new project view');
  check(errors, [], 'no uncaught browser errors');
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, checks, nativeTransport: 'controlled', actualProductionChooser: true,
    actualProductionBatchController: true, standaloneNativeImport: false }, null, 2));
  console.log(`Multi-select browser: passed (${checks.length} checks).`);
} catch (error) {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: false, checks, errors, error: error.stack }, null, 2));
  await page?.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {});
  throw error;
} finally { await browser.close(); }
