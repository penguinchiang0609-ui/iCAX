// Production picker + real shared WebGL viewport/resource decoder. CAD work is
// a controlled delayed fixture; timings below measure UI scheduling, not STEP IO.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('nesting-picker-performance-20261006');
const baselineOnly = process.env.ICAX_NESTING_PICKER_BASELINE === '1';
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 960 } });
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.route('http://picker-performance.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><style>body{margin:0;font:14px Segoe UI}.tube-designer-modal-backdrop{position:fixed;inset:0;display:grid;place-items:center;background:#0006}#app{height:100vh}#main-view{height:500px}</style></head><body><div id="app"><input id="main-field" value="正在编辑的零件"><div id="main-view"></div></div></body></html>' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(path.slice(5)), 'utf8') });
  });
  await page.goto('http://picker-performance.test/');
  await page.evaluate(async () => {
    const picker = await import('/src/apps/tube-designer/webpage/nestingPartFilePicker.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { encodeNestingGeometry } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const f = globalThis.__pickerPerformance = { calls: [], gets: [], stamp: '1001', delayMs: 200, view: { activeAreaId: 'nesting' } };
    const files = Array.from({ length: 10 }, (_, i) => ({ name: `sample-${i}.step`, path: `D:/CAD/sample-${i}.step`, directory: false, sizeBytes: 300 + i, lastModifiedNs: f.stamp }));
    const geometry = new THREE.BoxGeometry(400, 40, 20);
    f.bytes = encodeNestingGeometry({ positions: [...geometry.attributes.position.array], indices: [...geometry.index.array] }); geometry.dispose();
    const sceneProxy = { resources: { async get(url, init) {
      f.gets.push({ url, version: new Headers(init?.headers).get('ICAX-Resource-Version') });
      if (f.resourcePaddingBytes) { const body = new Uint8Array(f.bytes.byteLength + f.resourcePaddingBytes); body.set(new Uint8Array(f.bytes)); return new Response(body); }
      return new Response(f.bytes);
    } }, async invoke(method, payload) {
      f.calls.push({ method, payload, time: performance.now() });
      if (method === 'TubeDesigner.ListNestingPartFiles') return { directory: 'D:/CAD', parentDirectory: 'D:/', entries: files.map(file => ({ ...file, sizeBytes: file.sizeBytes + (f.sizeDelta || 0), lastModifiedNs: f.stamp })) };
      if (method === 'TubeDesigner.PreviewNestingPartFile') {
        const file = files.find(file => file.path === payload.sourcePath);
        await new Promise(resolve => setTimeout(resolve, f.delayMs));
        return { sourceFileName: file.name, sourcePath: file.path, sizeBytes: file.sizeBytes + (f.sizeDelta || 0), lastModifiedNs: f.stamp,
          geometry: { url: 'resource://performance-preview', version: f.calls.filter(call => call.method === method).length }, bounds: { min: [0, 0, 0], max: [400, 40, 20] } };
      }
      if (method === 'TubeDesigner.ReleaseNestingPartFilePreview') return { released: true };
      throw Error('Unexpected scene method: ' + method);
    } };
    f.context = { mount: document.querySelector('#app'), sceneProxy, productProxy: { invoke() { throw Error('Scene preview forbidden on product channel'); } } };
    f.mainViewport = createThreeViewport({ showGrid: false }); f.mainViewport.mount(document.querySelector('#main-view'));
    f.start = () => { f.choice = picker.chooseNestingPartFiles(f.context, f.view); };
    f.count = () => ({ native: f.calls.filter(call => call.method === 'TubeDesigner.PreviewNestingPartFile').length, gets: f.gets.length });
    f.start();
  });
  const dialog = page.locator('.tube-nesting-file-picker');
  const file = index => dialog.locator(`[data-nesting-file-path="D:/CAD/sample-${index}.step"]`);
  const ready = path => page.waitForFunction(path => {
    const p = __pickerPerformance.view.tubeDesignerNestingFilePicker;
    return p?.selectedPath === path && p?.viewport?.getAppliedViewState()?.entityIds.length === 1 && document.querySelector('[data-nesting-file-preview-status]')?.hidden;
  }, `D:/CAD/sample-${path}.step`);
  await file(0).click(); await ready(0);
  const first = await page.evaluate(() => __pickerPerformance.count());
  const box = await dialog.locator('.icax-three-viewport-canvas').boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down({ button: 'right' }); await page.mouse.move(box.x + box.width / 2 + 70, box.y + box.height / 2 + 30, { steps: 5 }); await page.mouse.up({ button: 'right' });
  await page.mouse.wheel(0, 160);
  const navigatedCamera = await page.evaluate(() => {
    const p = __pickerPerformance.view.tubeDesignerNestingFilePicker.viewport;
    return { theta: p.cameraState.theta, phi: p.cameraState.phi, radius: p.cameraState.radius };
  });
  await file(0).click();
  await page.waitForTimeout(380); await ready(0);
  const repeated = await page.evaluate(() => __pickerPerformance.count());
  const repeatedCamera = await page.evaluate(() => {
    const p = __pickerPerformance.view.tubeDesignerNestingFilePicker.viewport;
    return { theta: p.cameraState.theta, phi: p.cameraState.phi, radius: p.cameraState.radius };
  });
  if (baselineOnly) {
    assert.equal(repeated.native - first.native, 1); assert.equal(repeated.gets - first.gets, 1);
    const rapidStart = await page.evaluate(() => performance.now());
    await file(1).click(); await file(2).click(); await file(3).click(); await ready(3);
    const rapid = await page.evaluate(start => ({ ...__pickerPerformance.count(), elapsedMs: performance.now() - start,
      paths: __pickerPerformance.calls.filter(call => call.method === 'TubeDesigner.PreviewNestingPartFile').map(call => call.payload.sourcePath) }), rapidStart);
    writeFileSync(resolve(output, 'baseline.json'), JSON.stringify({ first, repeated, duplicateClickAddedNativeReads: repeated.native - first.native,
      duplicateClickAddedResourceGets: repeated.gets - first.gets, rapid, rapidAddedNativeReads: rapid.native - repeated.native,
      evidence: 'actual production selectFile with controlled 200ms native work', nativeCadBenchmark: false }, null, 2));
    console.log('BASELINE duplicate same-file click repeats native CAD request and resource GET.');
  } else {
    assert.deepEqual(repeated, first, '已显示同文件重复点击不能重读CAD或mesh');
    assert.deepEqual(repeatedCamera, navigatedCamera, '重复点已显示文件须保持用户转角和缩放');
    // Loading deduplication and rapid-click coalescing are distinct cases.
    await file(1).click(); await page.waitForFunction(() => __pickerPerformance.count().native === 2);
    await file(1).click(); await file(1).click(); await ready(1);
    const sameLoading = await page.evaluate(() => __pickerPerformance.count());
    assert.equal(sameLoading.native, 2);
    await file(0).click(); await ready(0);
    assert.deepEqual(await page.evaluate(() => __pickerPerformance.count()), sameLoading, '返回已预览文件只用窗口内mesh缓存');
    const rapidStart = await page.evaluate(() => performance.now());
    await file(2).click(); await file(3).click(); await file(4).click(); await ready(4);
    const rapid = await page.evaluate(start => ({ ...__pickerPerformance.count(), elapsedMs: performance.now() - start,
      paths: __pickerPerformance.calls.filter(call => call.method === 'TubeDesigner.PreviewNestingPartFile').map(call => call.payload.sourcePath) }), rapidStart);
    assert.equal(rapid.native, 3); assert.equal(rapid.paths.at(-1), 'D:/CAD/sample-4.step');
    await file(0).click(); await ready(0);
    assert.equal(await page.evaluate(() => __pickerPerformance.count().native), 3);
    // A fresh directory identity invalidates a cached file even at the same path.
    await page.evaluate(() => { __pickerPerformance.stamp = '1002'; });
    await dialog.locator('[data-nesting-file-directory]').press('Enter');
    await file(0).click(); await ready(0);
    assert.equal(await page.evaluate(() => __pickerPerformance.count().native), 4);
    await page.evaluate(() => { __pickerPerformance.sizeDelta = 10; });
    await dialog.locator('[data-nesting-file-directory]').press('Enter');
    await file(0).click(); await ready(0);
    assert.equal(await page.evaluate(() => __pickerPerformance.count().native), 5, 'size变化同样使文件cache失效');
    for (const index of [1, 2, 3, 4, 5, 6]) { await file(index).click(); await ready(index); }
    const sevenFirstPass = await page.evaluate(() => __pickerPerformance.count());
    for (const index of [0, 1, 2, 3, 4, 5, 6]) { await file(index).click(); await ready(index); }
    const sevenReturnPass = await page.evaluate(() => __pickerPerformance.count());
    assert.deepEqual(sevenReturnPass, sevenFirstPass, '七个已预览文件循环回看均须命中缓存');
    await file(7).click(); await ready(7);
    const eightEntries = await page.evaluate(() => __pickerPerformance.view.tubeDesignerNestingFilePicker.previewCache.size);
    assert.equal(eightEntries, 8);
    await file(0).click(); await ready(0); // A cache hit refreshes its LRU position.
    await file(8).click(); await ready(8);
    const bounded = await page.evaluate(() => {
      const p = __pickerPerformance.view.tubeDesignerNestingFilePicker;
      return { entries: p.previewCache.size, bytes: p.previewCacheBytes, sdkResources: p.viewport.resourcePromises.size,
        oldestEntryEvicted: ![...p.previewCache.values()].some(entry => entry.sourcePath.endsWith('sample-1.step')),
        refreshedEntryRetained: [...p.previewCache.values()].some(entry => entry.sourcePath.endsWith('sample-0.step')) };
    });
    assert.equal(bounded.entries, 8); assert(bounded.bytes <= 32 * 1024 * 1024); assert.equal(bounded.sdkResources, 1);
    assert.equal(bounded.oldestEntryEvicted, true, '第九个distinct文件须淘汰最久未用项');
    assert.equal(bounded.refreshedEntryRetained, true, '缓存命中须刷新LRU顺序');
    // Two individually cacheable meshes cannot exceed the total byte budget.
    await page.evaluate(() => { __pickerPerformance.resourcePaddingBytes = 17 * 1024 * 1024; });
    await file(9).click(); await ready(9);
    await file(1).click(); await ready(1);
    const byteBounded = await page.evaluate(() => {
      const p = __pickerPerformance.view.tubeDesignerNestingFilePicker;
      return { entries: p.previewCache.size, bytes: p.previewCacheBytes, sdkResources: p.viewport.resourcePromises.size,
        olderLargeEntryEvicted: ![...p.previewCache.values()].some(entry => entry.sourcePath.endsWith('sample-9.step')),
        newestLargeEntryRetained: [...p.previewCache.values()].some(entry => entry.sourcePath.endsWith('sample-1.step')) };
    });
    assert.equal(byteBounded.entries, 1); assert(byteBounded.bytes <= 32 * 1024 * 1024); assert.equal(byteBounded.sdkResources, 1);
    assert.equal(byteBounded.olderLargeEntryEvicted, true); assert.equal(byteBounded.newestLargeEntryRetained, true);
    await page.evaluate(() => { __pickerPerformance.resourcePaddingBytes = 33 * 1024 * 1024; });
    await file(2).click(); await ready(2);
    const oversized = await page.evaluate(() => {
      const p = __pickerPerformance.view.tubeDesignerNestingFilePicker;
      return { entries: p.previewCache.size, bytes: p.previewCacheBytes, cachedLargeFile: [...p.previewCache.values()].some(entry => entry.sourcePath.endsWith('sample-2.step')) };
    });
    assert.deepEqual(oversized, { entries: byteBounded.entries, bytes: byteBounded.bytes, cachedLargeFile: false }, '超预算mesh仍显示但不得进入缓存');
    await page.evaluate(() => { __pickerPerformance.resourcePaddingBytes = 0; });
    await page.evaluate(() => { __pickerPerformance.closedPicker = __pickerPerformance.view.tubeDesignerNestingFilePicker; });
    await dialog.locator('footer [data-nesting-file-action="cancel"]').click();
    await page.waitForFunction(() => __pickerPerformance.calls.some(call => call.method === 'TubeDesigner.ReleaseNestingPartFilePreview'));
    const released = await page.evaluate(() => ({ entries: __pickerPerformance.closedPicker.previewCache.size, bytes: __pickerPerformance.closedPicker.previewCacheBytes,
      viewportDisposed: !__pickerPerformance.closedPicker.viewport, mainCanvasConnected: __pickerPerformance.mainViewport.root.isConnected }));
    assert.deepEqual(released, { entries: 0, bytes: 0, viewportDisposed: true, mainCanvasConnected: true });
    assert.deepEqual(errors, []);
    writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, first, repeated, navigatedCamera, repeatedCamera, sameLoading, rapid, sevenFirstPass, sevenReturnPass, eightEntries, bounded, byteBounded, oversized, released,
      fileIdentityInvalidatesCache: true, nativeTransport: 'controlled 200ms native protocol and mesh fixture', nativeCadBenchmark: false, standaloneCefEndToEnd: false }, null, 2));
    console.log('PASS picker scheduling, same-file loading dedupe, bounded identity-aware mesh cache and cleanup.');
  }
  await page.evaluate(() => { __pickerPerformance.view.tubeDesignerNestingFilePicker?.close(); __pickerPerformance.mainViewport.dispose(); });
} finally { await browser.close(); }
