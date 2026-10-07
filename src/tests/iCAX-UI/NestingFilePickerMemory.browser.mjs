// Production STEP picker and SVG preview. Directory and edge transport are controlled;
// persistent Chromium profile verifies state survives an actual browser shutdown.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('nesting-file-picker-memory-20261006');
const profileDirectory = mkdtempSync(resolve(output, 'browser-profile-'));
const checks = [], errors = [], snapshots = [];
const directory = 'D:/CAD/machined';
const selectedPath = `${directory}/02_drilled-tube.step`;
const check = (actual, expected, name) => { assert.deepEqual(actual, expected, name); checks.push(name); };
let context, page;

async function launch() {
  context = await chromium.launchPersistentContext(profileDirectory, { headless: true,
    channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge', viewport: { width: 1440, height: 960 } });
  await context.route('http://picker-memory.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><style>body{margin:0;font:14px Segoe UI}.tube-designer-modal-backdrop{position:fixed;inset:0;display:grid;place-items:center;background:#0006}#app{height:100vh}</style><body><div id="app"><input id="original-field" value="正在编辑"/></div></body>' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript',
      body: readFileSync(browserAssetPath(path.slice('/src/'.length)), 'utf8') });
  });
  page = context.pages()[0] || await context.newPage();
  page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('http://picker-memory.test/');
}

async function setup({ clear = false, missing = '' } = {}) {
  await page.evaluate(async ({ clear, missing }) => {
    if (clear) localStorage.clear();
    const picker = await import('/src/apps/tube-designer/webpage/nestingPartFilePicker.mjs');
    const { bindTubeDesignerWindowMemory } = await import('/src/apps/tube-designer/webpage/windowStateMemory.mjs');
    const f = window.pickerFixture = { calls: [], gets: [], choices: [], missing,
      browseDirectory: 'D:/CAD/machined', view: { activeAreaId: 'nesting', scene: { marker: 'unchanged-scene' } } };
    const files = {
      'D:/initial': [{ name: 'machined', path: 'D:/CAD/machined', directory: true }],
      'D:/CAD/machined': [
        { name: '01_mitered-tube.step', path: 'D:/CAD/machined/01_mitered-tube.step', directory: false },
        { name: '02_drilled-tube.step', path: 'D:/CAD/machined/02_drilled-tube.step', directory: false },
        { name: 'other', path: 'D:/CAD/other', directory: true },
        { name: '说明.xlsx', path: 'D:/CAD/machined/说明.xlsx', directory: false },
      ],
      'D:/CAD/other': [{ name: '02_drilled-tube.step', path: 'D:/CAD/other/02_drilled-tube.step', directory: false }],
    };
    const sceneProxy = { resources: { async get() { throw Error('SVG preview must not request mesh resources'); } }, async invoke(method, payload = {}) {
      f.calls.push({ method, payload: structuredClone(payload) });
      if (method === 'TubeDesigner.ListNestingPartFiles') {
        const directory = payload.directory || 'D:/initial';
        return { directory, parentDirectory: 'D:/', entries: (files[directory] || []).filter(row => row.path !== f.missing)
          .map(row => row.directory ? row : { ...row, sizeBytes: 500, lastModifiedNs: '1001' }) };
      }
      if (method === 'TubeDesigner.PreviewNestingPartFile') {
        if (payload.representation !== 'wireframe') throw Error('Chooser must request wireframe');
        if (f.holdPreview) await new Promise(resolve => { f.finishPreview = resolve; });
        return {
        sourcePath: payload.sourcePath, sourceFileName: payload.sourcePath.split('/').at(-1), sizeBytes: 500, lastModifiedNs: '1001',
        wireframe: { polylines: [[-200,-20,-10,200,-20,-10,200,20,-10,-200,20,-10,-200,-20,-10], [-200,-20,10,200,-20,10,200,20,10,-200,20,10,-200,-20,10]], edgeCount: 2, pointCount: 10 },
      }; }
      if (method === 'TubeDesigner.ReleaseNestingPartFilePreview') return { released: true };
      throw Error('Remembered file must never create/import a part: ' + method);
    } };
    f.context = { mount: document.querySelector('#app'), sceneProxy, project: { projectId: 'picker-memory-project' },
      appProxy: { bridge: { async openDirectoryDialog(options) {
        f.calls.push({ method: 'openDirectoryDialog', payload: structuredClone(options) }); return f.browseDirectory;
      } } } };
    f.controller = bindTubeDesignerWindowMemory(f.context, f.view, {});
    f.start = () => {
      f.promise = picker.chooseNestingPartFiles(f.context, f.view);
      void f.promise.then(path => f.choices.push(path));
    };
    f.start();
  }, { clear, missing });
}

const ready = path => page.waitForFunction(path => {
  const state = pickerFixture.view.tubeDesignerNestingFilePicker;
  return state?.selectedPath === path && state.previewStatus === 'ready'
    && state.viewport?.getDebugState()?.edgeCount === 2;
}, path);
const idle = () => page.waitForFunction(() => {
  const list = document.querySelector('.tube-nesting-file-list');
  return list && !list.hasAttribute('aria-busy') && pickerFixture.view.tubeDesignerNestingFilePicker?.previewStatus === 'idle';
});
const snapshot = () => page.evaluate(() => {
  const state = pickerFixture.view.tubeDesignerNestingFilePicker;
  return { directory: document.querySelector('[data-nesting-file-directory]').value, selectedPath: state.selectedPath,
    selectedRows: [...document.querySelectorAll('.tube-nesting-file-row[aria-selected="true"]')].map(row => row.dataset.nestingFilePath),
    previewStatus: state.previewStatus, openDisabled: document.querySelector('[data-nesting-file-action="open"]').disabled,
    initialDirectoryRequest: pickerFixture.calls.find(row => row.method === 'TubeDesigner.ListNestingPartFiles').payload,
    previewPaths: pickerFixture.calls.filter(row => row.method === 'TubeDesigner.PreviewNestingPartFile').map(row => row.payload.sourcePath),
    importedCalls: pickerFixture.calls.filter(row => /Import|Add|Save|Generate/.test(row.method)), scene: pickerFixture.view.scene };
});
const cancel = async () => {
  const count = await page.evaluate(() => pickerFixture.choices.length);
  await page.locator('footer [data-nesting-file-action="cancel"]').click();
  await page.waitForFunction(count => !pickerFixture.view.tubeDesignerNestingFilePicker && pickerFixture.choices.length === count + 1, count);
  check(await page.evaluate(() => pickerFixture.choices.at(-1)), [], 'cancellation returns no import paths');
};

try {
  await launch(); await setup({ clear: true }); await idle();
  check((await snapshot()).directory, 'D:/initial', 'first use keeps host directory default');
  await page.locator('[data-nesting-file-action="browse"]').click();
  await page.locator(`[data-nesting-file-path="${selectedPath}"]`).waitFor();
  check(await page.locator('[data-nesting-file-directory]').inputValue(), directory, 'directory picker navigation updates and remembers directory');
  await page.locator(`[data-nesting-file-path="${selectedPath}"]`).click(); await ready(selectedPath);
  await cancel();
  await page.evaluate(() => pickerFixture.start()); await ready(selectedPath);
  const reopened = await snapshot(); snapshots.push({ stage: 'cancel-reopen', ...reopened });
  check({ directory: reopened.directory, selectedPath: reopened.selectedPath, selectedRows: reopened.selectedRows },
    { directory, selectedPath, selectedRows: [selectedPath] }, 'cancel/reopen remembers directory and exact selected file');
  check(reopened.previewPaths, [selectedPath], 'reopening uses cached wireframe without another STEP read');
  check(await page.locator('[data-nesting-file-viewport] canvas').count(), 0, 'preview never creates a WebGL canvas');
  check(reopened.importedCalls, [], 'remembered selection loads preview without importing or saving');
  check(reopened.scene, { marker: 'unchanged-scene' }, 'preview keeps project scene unchanged');
  await page.evaluate(() => { pickerFixture.holdPreview = true; });
  const otherPath = `${directory}/01_mitered-tube.step`;
  await page.locator(`[data-nesting-file-path="${otherPath}"]`).click();
  await page.waitForFunction(() => typeof pickerFixture.finishPreview === 'function');
  const beforeResponse = await page.evaluate(() => {
    const input = document.querySelector('[data-nesting-file-directory]');
    const list = document.querySelector('.tube-nesting-file-list');
    input.focus({ preventScroll: true }); input.setSelectionRange(3, 10);
    list.style.maxHeight = '55px'; list.scrollTop = 32;
    pickerFixture.originalInput = input; pickerFixture.originalSvg = document.querySelector('[data-nesting-wireframe]');
    return { selection: [input.selectionStart, input.selectionEnd], scroll: list.scrollTop };
  });
  await page.evaluate(() => { pickerFixture.holdPreview = false; pickerFixture.finishPreview(); });
  await ready(otherPath);
  check(await page.evaluate(() => ({
    selection: [pickerFixture.originalInput.selectionStart, pickerFixture.originalInput.selectionEnd],
    scroll: document.querySelector('.tube-nesting-file-list').scrollTop,
  })), beforeResponse, 'async preview preserves current text selection and nested file-list scroll');
  check(await page.evaluate(() => document.activeElement === pickerFixture.originalInput
    && document.querySelector('[data-nesting-wireframe]') === pickerFixture.originalSvg), true,
    'async preview retains input focus, SVG node and its listeners');
  await page.locator(`[data-nesting-file-path="${selectedPath}"]`).click(); await ready(selectedPath);
  await cancel();

  await page.reload(); await setup(); await ready(selectedPath);
  const reloaded = await snapshot(); snapshots.push({ stage: 'page-reload', ...reloaded });
  check(reloaded.initialDirectoryRequest, { directory }, 'fresh page queries persisted directory directly');
  check(reloaded.selectedRows, [selectedPath], 'fresh module instance restores selected file and actual preview');
  await cancel();
  await context.close(); context = null;

  await launch(); await setup(); await ready(selectedPath);
  const restarted = await snapshot(); snapshots.push({ stage: 'browser-restart', ...restarted });
  check({ directory: restarted.directory, selectedPath: restarted.selectedPath, previewPaths: restarted.previewPaths },
    { directory, selectedPath, previewPaths: [selectedPath] }, 'actual browser shutdown/restart retains directory and selected file');
  check(restarted.importedCalls, [], 'browser restart never automatically imports remembered file');
  await cancel();

  await page.reload(); await setup({ missing: selectedPath }); await idle();
  const removed = await snapshot(); snapshots.push({ stage: 'remembered-file-removed', ...removed });
  check({ directory: removed.directory, selectedPath: removed.selectedPath, selectedRows: removed.selectedRows,
    previewPaths: removed.previewPaths, openDisabled: removed.openDisabled },
    { directory, selectedPath: '', selectedRows: [], previewPaths: [], openDisabled: true },
    'missing remembered file leaves selection empty with open disabled');
  check(removed.importedCalls, [], 'missing file does not preview, import or substitute a neighboring file');
  await page.locator('[data-nesting-file-directory]').fill('D:/CAD/other');
  await page.locator('[data-nesting-file-directory]').press('Enter'); await idle();
  check((await snapshot()).selectedRows, [], 'same basename in another directory is not treated as remembered file');
  check((await snapshot()).previewPaths, [], 'navigation to another directory never chooses an unrelated same-name file');
  await page.screenshot({ path: resolve(output, 'missing-file-unselected.png'), fullPage: true });
  await cancel();

  await page.evaluate(() => pickerFixture.start()); await idle();
  check(await page.locator('[data-nesting-file-directory]').inputValue(), 'D:/CAD/other', 'latest directory navigation becomes next opening default');
  await page.locator('[data-nesting-file-directory]').fill('D:/draft/latest-path');
  await cancel();
  await page.evaluate(() => pickerFixture.start()); await idle();
  check(await page.locator('[data-nesting-file-directory]').inputValue(), 'D:/draft/latest-path', 'edited path survives cancellation even before navigation');
  check(await page.evaluate(() => pickerFixture.calls.filter(row => /Import|Add|Save|Generate/.test(row.method))), [], 'all picker memory operations remain preview only');
  await cancel();
  check(errors, [], 'SVG preview and picker have no uncaught browser errors');
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'passed', checks, snapshots,
    persistedBrowserProfile: profileDirectory, actualBrowserShutdownRestart: true, actualProductionFilePicker: true,
    actualSvgViewport: true, nativeTransport: 'controlled directory and edge fixture', nativeCadRecognition: false,
    standaloneCefEndToEnd: false, nativePartImport: false }, null, 2));
  console.log(`Nesting file picker memory: passed (${checks.length} browser checks, including browser restart).`);
} catch (error) {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'failed', checks, snapshots, errors,
    error: { message: error.message, stack: error.stack } }, null, 2));
  if (page && !page.isClosed()) await page.screenshot({ path: resolve(output, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally { await context?.close(); }
