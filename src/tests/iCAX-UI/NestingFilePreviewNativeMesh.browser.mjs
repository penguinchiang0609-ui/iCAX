// The production SVG chooser consumes actual native STEP edge polylines.
// Only the native transport is controlled; no mesh generation or WebGL is used.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, basename } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const manifestPath = resolve(process.env.ICAX_NESTING_NATIVE_WIREFRAME_MANIFEST ||
  'output/tests/nesting-preview-blank-20261007/wireframe-native.json');
const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
const measurements = manifest.measurements;
assert.equal(measurements.length, 7, 'All seven actual machined vendor STEP files are required');
const records = measurements;
for (const item of records) {
  assert.ok(item.wireframe.polylines.length > 12);
  assert.ok(item.lastModifiedNs && Number.isSafeInteger(item.sizeBytes));
}
const output = browserReportDirectory('nesting-preview-blank-20261007/browser-native-wireframe');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1500, height: 1000 } });
  page.setDefaultTimeout(20000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://native-preview.test/**', route => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><style>body{margin:0;font:14px Arial;background:#edf4f5}.tube-designer-modal-backdrop{position:fixed;inset:0;display:grid;place-items:center;background:#0006}</style><body><main id="workspace"><input id="draft" value="验收草稿"></main></body>' });
    if (!pathname.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'text/javascript',
      body: readFileSync(browserAssetPath(decodeURIComponent(pathname.slice(5))), 'utf8') });
  });
  await page.goto('http://native-preview.test/');
  await page.evaluate(async records => {
    const { chooseNestingPartFiles } = await import('/src/apps/tube-designer/webpage/nestingPartFilePicker.mjs');
    const canonical = value => value.replaceAll('\\', '/').toLowerCase();
    const nativePreviews = [], resourceReads = [], releases = [];
    const sceneProxy = {
      async invoke(method, payload) {
        if (method === 'TubeDesigner.ListNestingPartFiles') return {
          directory: records[0].sourcePath.replaceAll('\\', '/').split('/').slice(0, -1).join('/'), parentDirectory: '',
          entries: records.map(item => ({ name: item.filename, path: item.sourcePath, directory: false,
            sizeBytes: item.sizeBytes, lastModifiedNs: item.lastModifiedNs })) };
        if (method === 'TubeDesigner.PreviewNestingPartFile') {
          if (payload.representation !== 'wireframe') throw Error('SVG chooser must request wireframe');
          nativePreviews.push(payload);
          const item = records.find(item => canonical(item.sourcePath) === canonical(payload.sourcePath));
          if (!item) throw Error('Unexpected source file');
          return structuredClone(item);
        }
        if (method === 'TubeDesigner.ReleaseNestingPartFilePreview') { releases.push(payload); return { released: true }; }
        throw Error('Preview must never import or change entities: ' + method);
      },
      resources: { async get() { resourceReads.push(true); throw Error('SVG must never download mesh'); } },
    };
    const view = { activeAreaId: 'nesting' };
    const context = { mount: document.querySelector('#workspace'), sceneProxy };
    document.querySelector('#draft').focus();
    const fixture = globalThis.nativeMeshFixture = { records, context, view, nativePreviews, resourceReads, releases };
    fixture.choice = chooseNestingPartFiles(context, view);
  }, records);
  await page.locator('[data-nesting-file-path]').first().waitFor();
  const browserTimings = [];
  for (let index = 0; index < records.length; index++) {
    await page.evaluate(index => {
      const f = nativeMeshFixture;
      f.selectionStart = performance.now();
      [...document.querySelectorAll('[data-nesting-file-path]')][index].click();
    }, index);
    await page.waitForFunction(index => {
      const f = nativeMeshFixture, picker = f.view.tubeDesignerNestingFilePicker;
      return picker?.previewStatus === 'ready' && picker.selectedPath === f.records[index].sourcePath;
    }, index);
    const actual = await page.evaluate(async () => {
      const f = nativeMeshFixture, picker = f.view.tubeDesignerNestingFilePicker;
      const readyMs = performance.now() - f.selectionStart;
      await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      const debug = picker.viewport.getDebugState();
      return { readyMs, paintedMs: performance.now() - f.selectionStart, debug,
        cacheEntries: picker.previewCache.size, cacheBytes: picker.previewCacheBytes,
        canvasCount: document.querySelectorAll('[data-nesting-file-viewport] canvas').length,
        pathLength: document.querySelector('[data-nesting-wireframe] path').getAttribute('d').length };
    });
    assert.equal(actual.debug.edgeCount, records[index].wireframe.edgeCount);
    assert.equal(actual.debug.pointCount, records[index].wireframe.pointCount);
    assert.equal(actual.canvasCount, 0);
    assert.ok(actual.pathLength > 100);
    assert.ok(actual.debug.viewBox.every(Number.isFinite));
    assert.ok(actual.cacheEntries <= 8 && actual.cacheBytes <= 32 * 1024 * 1024);
    browserTimings.push({ filename: records[index].filename, nativeInvocationMs: records[index].invocationMs,
      nativeProfile: records[index].profile, wireframePoints: records[index].wireframe.pointCount, ...actual });
  }
  assert.equal(await page.evaluate(() => nativeMeshFixture.nativePreviews.length), 7);
  assert.equal(await page.evaluate(() => nativeMeshFixture.resourceReads.length), 0);
  await page.evaluate(() => {
    const f = nativeMeshFixture; f.selectionStart = performance.now();
    [...document.querySelectorAll('[data-nesting-file-path]')][0].click();
  });
  await page.waitForFunction(() => nativeMeshFixture.view.tubeDesignerNestingFilePicker?.previewStatus === 'ready');
  const cached = await page.evaluate(() => {
    const f = nativeMeshFixture, picker = f.view.tubeDesignerNestingFilePicker;
    return { readyMs: performance.now() - f.selectionStart, nativePreviews: f.nativePreviews.length,
      resourceReads: f.resourceReads.length, edgeCount: picker.viewport.getDebugState().edgeCount };
  });
  assert.equal(cached.nativePreviews, 7, 'Previously viewed wireframe must not reread STEP');
  assert.equal(cached.resourceReads, 0, 'SVG never requests native mesh URLs');
  assert.ok(cached.edgeCount > 0);
  const svg = page.locator('[data-nesting-wireframe]');
  const viewportBounds = await svg.boundingBox();
  const initialViewBox = await svg.getAttribute('viewBox');
  await page.mouse.move(viewportBounds.x + 20, viewportBounds.y + 20);
  await page.mouse.wheel(0, -400);
  await page.waitForFunction(initial => document.querySelector('[data-nesting-wireframe]').getAttribute('viewBox') !== initial, initialViewBox);
  await svg.dblclick({ position: { x: 20, y: 20 } });
  assert.equal(await svg.getAttribute('viewBox'), initialViewBox, 'Left double click on blank SVG resets fit');
  const initialYaw = await page.evaluate(() => nativeMeshFixture.view.tubeDesignerNestingFilePicker.viewport.getDebugState().yaw);
  await page.mouse.move(viewportBounds.x + 30, viewportBounds.y + 30);
  await page.mouse.down({ button: 'right' });
  await page.mouse.move(viewportBounds.x + 100, viewportBounds.y + 60);
  await page.mouse.up({ button: 'right' });
  await page.waitForFunction(initial => nativeMeshFixture.view.tubeDesignerNestingFilePicker.viewport.getDebugState().yaw !== initial, initialYaw);
  await page.locator('[data-nesting-file-action="fit"]').click();
  await page.screenshot({ path: resolve(output, 'native-vendor-step-preview.png') });
  await page.locator('footer [data-nesting-file-action="cancel"]').click();
  await page.waitForFunction(() => nativeMeshFixture.releases.length > 0);
  assert.equal(await page.evaluate(() => document.activeElement?.id), 'draft');
  assert.deepEqual(errors, []);
  const report = { passed: true, manifestPath, browserTimings, cached, errors,
    geometrySource: 'Native PreviewNestingPartFile actual coarse edge polylines for all 7 vendor STEP files',
    frontend: 'Actual production file chooser and SVG wireframe renderer',
    transport: 'Controlled HTTP/native bridge; native timing is measured separately, browser timing includes 120 ms debounce',
    standaloneCefEndToEnd: false };
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ passed: true, files: 7, cached, output }));
} finally {
  await browser.close();
}
