import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { verifyWorkpieceFilePreview } from './FiveAxisFilePreview.native.browser.mjs';
import { verifyLargeWorkpiecePreview } from './FiveAxisLargePreview.native.browser.mjs';
import { verifyFaceFeedback } from './FiveAxisFaceFeedback.native.browser.mjs';
import { verifyIgesAfterStep } from './FiveAxisIgesSequence.native.browser.mjs';
import { verifyIgesCuts } from './FiveAxisIgesCuts.native.browser.mjs';
import { verifyMachiningUX } from './FiveAxisMachiningUX.native.browser.mjs';
import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('five-axis-import-browser');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root, 'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, process.env.ICAX_FIVE_AXIS_NATIVE_EXECUTABLE || 'FiveAxisCAMTest.exe'),
  ['--gtest_filter=FiveAxisBridge.DISABLED_Serve', '--gtest_also_run_disabled_tests'],
  { cwd: nativeDirectory, windowsHide: true, stdio: ['pipe','pipe','pipe'] });
let sequence = 0, browser, page, failure;
const waiting = new Map(), calls = [], errors = [], nativeLog = [];
const failAll = error => { for (const entry of waiting.values()) { clearTimeout(entry.timer); entry.reject(error); } waiting.clear(); };
processHandle.on('error', failAll);
processHandle.stdin.on('error', failAll);
processHandle.on('exit', code => { if (waiting.size) failAll(Error(`Native bridge exited ${code}`)); });
processHandle.stderr.on('data', bytes => nativeLog.push(String(bytes)));
createInterface({ input: processHandle.stdout }).on('line', line => {
  if (!line.startsWith('__FIVE_RPC__')) { nativeLog.push(line); return; }
  const response = JSON.parse(line.slice(12)), pending = waiting.get(response.id);
  if (!pending) return;
  waiting.delete(response.id); clearTimeout(pending.timer);
  if (response.ok) pending.resolve(response.result); else pending.reject(Error(response.error));
});
const rpc = message => new Promise((resolve, reject) => {
  const id = ++sequence;
  calls.push({ id, ...message });
  const timer = setTimeout(() => { waiting.delete(id); reject(Error(`Native timeout ${message.action}`)); }, 120000);
  waiting.set(id, { resolve, reject, timer });
  const wire = message.action === 'invoke'
    ? { ...message, payload: undefined, payloadText: message.payloadText ?? serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  const info = await rpc({ action: 'info' });
  info.validateSdoTimeouts = !!(process.env.ICAX_VISIBILITY_IMPORT_ONLY || process.env.ICAX_ROUNDED_COVER_RAPID_ONLY || process.env.ICAX_BULK_CUTS_ONLY || process.env.ICAX_EGG_CUTS_ONLY || process.env.ICAX_LARGE_PREVIEW_ONLY || process.env.ICAX_FACE_FEEDBACK_ONLY || process.env.ICAX_IGES_SEQUENCE_ONLY);
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge', headless: true });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  if(process.env.ICAX_EGG_IMPORT_ONLY)page.setDefaultTimeout(300000);
  page.on('pageerror', error => errors.push(error.message));
  await page.exposeFunction('fiveNativeRpc', rpc);
  await page.route('**/*', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><body style="margin:0"><div id="app" style="height:100vh"></div></body></html>' });
    if (!path.startsWith('/src/')) return route.abort();
    try { return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(path.slice(5))) }); }
    catch { return route.abort(); }
  });
  await page.goto('http://localhost/');
  await page.evaluate(async info => {
    const { serializeVariantText } = await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs');
    const entry = await import('/src/apps/five-axis-cam/webpage/entry.mjs');
    const { ViewClient } = await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const view = getProjectView(info.projectId);
    const f = window.fixture = { entry, view, fileSelections: 0, logs: [], hold: false, held: false, release: null };
    const sceneProxy = { sceneId: info.sceneId, state: { sceneId: info.sceneId }, pdo: { enabled: false },
      async invoke(method, payload = {}, options = {}) {
        if (method === (f.holdMethod || 'CamScene.ReadState') && f.hold) { f.hold = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false; }
        const result = f.sdoClient ? await f.invokeSdo(method,payload,options)
          : await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payloadText:serializeVariantText(payload) });
        if (method === 'CamWorkpiece.CancelTask') f.cancelAcknowledged = true;
        if (method === 'CamWorkpiece.QueryTask' && result.status === 'Completed' && f.holdAfterImport) { f.holdAfterImport = false; f.holdMethod = 'CamScene.ReadState'; f.hold = true; }
        return result;
      }, async getState() { return this.state; } };
    if (info.validateSdoTimeouts) {
      const { SDOClient } = await import('/src/iCAX-UI/SDK/SDO/sdoClient.mjs');
      const { makeSDOMethodCodeFromName } = await import('/src/iCAX-UI/SDK/SDO/sdoMethod.mjs');
      const { serializeVariantText } = await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs');
      const methods = new Map();let receive;
      f.sdoTimings=[];
      f.sdoClient=new SDOClient({
        subscribeSDOFrames(channelId,handler) { receive=handler;return ()=>{}; },
        async postSDOFrame(frame) {
          const method=methods.get(String(frame.methodCode)),started=performance.now();
          if(method==='CamCut.Create'&&f.withholdCreateOnce){f.withholdCreateOnce=false;return;}
          const result=await fiveNativeRpc({action:'invoke',scope:'scene',method,payloadText:frame.payloadText});
          const nativeMs=performance.now()-started;
          if(f.delayResponses?.[method])await new Promise(resolve=>setTimeout(resolve,f.delayResponses[method]));
          f.sdoTimings.push({method,nativeMs,elapsedMs:performance.now()-started});
          receive({...frame,kind:2,status:0,payloadText:serializeVariantText(result)});
        },
      });
      f.invokeSdo=(method,payload,options)=>{
        methods.set(makeSDOMethodCodeFromName(method),method);
        if(method==='CamCut.Create'&&f.forceCreateTimeoutOnce){options={...options,timeoutMs:f.forceCreateTimeoutOnce};f.forceCreateTimeoutOnce=0;}
        return f.sdoClient.invoke(info.sceneId,method,payload,options);
      };
    }
    const resource = async (url, options, head) => {
      const version = new Headers(options?.headers).get('ICAX-Resource-Version') ?? String(options?.version ?? '');
      const result = await fiveNativeRpc({ action: 'resource', url, version, head });
      return new Response(head ? null : Uint8Array.from(result.bytes), { headers: { 'Content-Type': 'application/vnd.icax.flatbuffer', 'ICAX-Resource-Version': result.version } });
    };
    sceneProxy.resources = { get: (url, options) => resource(url, options, false), head: (url, options) => resource(url, options, true) };
    sceneProxy.views = new ViewClient(sceneProxy);
    const context = f.context = { mount: document.querySelector('#app'), sceneProxy,
      project: { projectId: info.projectId, projectName: '封头曲面导入 · 原生验收' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog() { ++f.fileSelections; return info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) { return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);
  if (process.env.ICAX_FILE_PICKER_ONLY) {
    const {verifyFilePickerFlow}=await import('./FilePickerFlow.native.browser.mjs');
    await verifyFilePickerFlow({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_LAYER_COLLAPSE_ONLY) {
    const { verifyLayerCollapse } = await import('./FiveAxisLayerCollapse.native.browser.mjs');
    await verifyLayerCollapse({page,rpc,output,errors});
  } else if (process.env.ICAX_LAYERS_ONLY) {
    const { verifyToolpathLayers } = await import('./FiveAxisToolpathLayers.native.browser.mjs');
    await verifyToolpathLayers({page,rpc,info,root,output,calls,errors});
  } else if (process.env.ICAX_VISIBILITY_IMPORT_ONLY) {
    const { verifyVisibilityImport } = await import('./FiveAxisVisibilityImport.native.browser.mjs');
    await verifyVisibilityImport({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_ROUNDED_COVER_RAPID_ONLY) {
    const { verifyRoundedCoverRapid } = await import('./FiveAxisRoundedCoverRapid.native.browser.mjs');
    await verifyRoundedCoverRapid({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_BULK_CUTS_ONLY) {
    const { verifyBulkCuts } = await import('./FiveAxisBulkCuts.native.browser.mjs');
    await verifyBulkCuts({page,rpc,info,output,calls,errors});
  } else if (process.env.ICAX_EGG_IMPORT_ONLY) {
    const { verifyEggImport } = await import('./FiveAxisEggImport.native.browser.mjs');
    await verifyEggImport({page,rpc,root,output,errors});
  } else if (process.env.ICAX_DISK_RAPID_ONLY) {
    const { verifyDiskRapid } = await import('./FiveAxisDiskRapid.native.browser.mjs');
    await verifyDiskRapid({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_MANUAL_SELECTION_ONLY) {
    const { verifyManualSelection } = await import('./FiveAxisManualSelection.native.browser.mjs');
    await verifyManualSelection({page,rpc,info,root,output,calls,errors});
  } else if (process.env.ICAX_PATCH_BOUNDARY_ONLY) {
    const { verifyPatchBoundary } = await import('./FiveAxisPatchBoundary.native.browser.mjs');
    await verifyPatchBoundary({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_LAIRD_IMPORT_ONLY) {
    const { verifyLairdImport } = await import('./FiveAxisLairdImport.native.browser.mjs');
    await verifyLairdImport({page,rpc,root,output,errors,scaleModels:!!process.env.ICAX_LAIRD_SCALE});
  } else if (process.env.ICAX_IGES_CUTS_ONLY) {
    await verifyIgesCuts({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_MACHINING_UX_ONLY) {
    await verifyMachiningUX({page,rpc,root,output,calls,errors,faceBoxOnly:!!process.env.ICAX_FACE_BOX_ONLY});
  } else if (process.env.ICAX_IGES_SEQUENCE_ONLY) {
    await verifyIgesAfterStep({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_FACE_FEEDBACK_ONLY) {
    await verifyFaceFeedback({page,rpc,root,output,calls,errors});
  } else if (process.env.ICAX_LARGE_PREVIEW_ONLY) {
    await verifyLargeWorkpiecePreview({page,rpc,root,output,calls,errors});
  } else {
  assert.equal(await page.locator('[data-five-command="five.tool.models"], [data-five-workpiece]').count(), 0);
  await verifyWorkpieceFilePreview({ page, rpc, info, root, output, calls });
  await page.evaluate(() => { fixture.holdMethod = 'CamWorkpiece.QueryTask'; fixture.hold = true; });
  await selectWorkpieceFile(page, info.sourcePath);
  await page.waitForFunction(() => fixture.held);
  const importProgress = page.locator('.five-blocking-progress [role="progressbar"]');
  assert.equal(await importProgress.isVisible(), true);
  assert.equal(await importProgress.getAttribute('aria-valuenow'), null, 'no fabricated percentage');
  assert.equal(await page.locator('#five-blocking-progress-detail').innerText(), info.sourcePath.split(/[\\/]/).at(-1));
  assert.equal(await page.locator('.five-import-progress').count(),0);
  assert.equal(await page.locator('.five-blocking-progress').count(),1);
  await page.screenshot({ path: resolve(output, 'import-progress.png') });
  await page.getByRole('button',{name:'取消导入',exact:true}).click();
  assert.equal(await page.locator('[data-progress-cancel]').isDisabled(), true);
  await page.waitForFunction(() => fixture.cancelAcknowledged);
  await page.evaluate(() => { fixture.holdMethod = ''; fixture.release(); });
  await page.waitForFunction(() => !fixture.view.pending);
  assert.equal(await page.evaluate(() => !!fixture.view.scene.domain.workpieces[0]?.id), false);
  assert.ok(calls.some(call => call.method === 'CamWorkpiece.CancelTask'));
  assert.equal(await importProgress.count(), 0);
  await page.evaluate(() => { fixture.holdAfterImport = true; });
  await selectWorkpieceFile(page, info.sourcePath);
  await page.waitForFunction(() => fixture.held);
  assert.equal(await importProgress.isVisible(), true, 'progress stays until the imported model is displayed');
  assert.equal(await page.locator('#five-blocking-progress-title').innerText(), '正在显示模型…');
  assert.equal(await page.locator('[data-progress-cancel]').isVisible(), false);
  await page.evaluate(() => { fixture.holdMethod = ''; fixture.release(); });
  await page.waitForFunction(() => !fixture.view.pending && !fixture.view.fiveAxisImportActive && !!fixture.view.scene?.domain?.workpieces?.[0]?.id);
  assert.equal(await importProgress.count(), 0);
  await page.waitForFunction(() => fixture.view.viewport?.getDebugState()?.objectCount === 1);
  assert.equal(await page.evaluate(() => fixture.fileSelections), 2);
  const state = await rpc({ action: 'invoke', scope: 'scene', method: 'CamScene.ReadState', payload: {} });
  assert.ok(state.workpieces[0]?.id); assert.equal(state.placements.length, 1);
  const workbench = await page.evaluate(() => {
    const scene = fixture.view.viewport.scene;
    const grid = scene.getObjectByName('iCAX XY Workbench Grid');
    grid.geometry.computeBoundingBox();
    const bounds = grid.geometry.boundingBox.clone().applyMatrix4(grid.matrixWorld);
    return { axes: !!scene.getObjectByName('iCAX World XYZ'),
      labels: ['X','Y','Z'].map(axis => !!scene.getObjectByName(`iCAX World ${axis} Label`)),
      min: bounds.min.toArray(), max: bounds.max.toArray() };
  });
  assert.equal(workbench.axes, true);
  assert.deepEqual(workbench.labels, [true,true,true]);
  assert.ok(Math.abs(workbench.min[2]) < 1e-8 && Math.abs(workbench.max[2]) < 1e-8, 'grid is in world XY');
  const centered = await page.evaluate(() => fixture.view.viewport.getDebugState().contentBounds);
  assert.ok(centered);
  // Tessellated sphere bounds are slightly asymmetric; exact CAD placement is checked natively.
  for (const axis of ['x','y','z']) assert.ok(Math.abs(centered.center[axis]) < .5, `imported display centered on ${axis}: ${JSON.stringify(centered)}`);
  for (const entry of await page.locator('[data-five-command="workpiece.import"]').all()) assert.equal(await entry.isDisabled(), false);
  assert.equal(state.workpieces[0].importMetadata.geometryLengthUnit, 'mm');
  assert.equal(state.workpieces[0].importMetadata.validBRep, true);
  assert.ok(calls.some(call => call.method === 'CamWorkpiece.Import'));
  assert.ok(!calls.some(call => ['WorkpieceModel.Import','Workpiece.Instantiate','Job.Get'].includes(call.method)));
  await assert.rejects(rpc({ action: 'invoke', scope: 'product', method: 'CamScene.ReadState', payload: {} }), /SCENE_CHANNEL/);
  await page.screenshot({ path: resolve(output, 'imported.png') });

  await page.evaluate(() => { window.originalCanvas = document.querySelector('canvas'); return fixture.entry.handleRibbonCommand(fixture.context, 'five.tool.rough'); });
  const setPoint = async (prefix, values) => {
    for (const [i, axis] of ['X','Y','Z'].entries()) await page.locator(`[data-five-field="${prefix}${axis}"]`).fill(String(values[i]));
  };
  const clickWorkpiece = async (hoverOnly=false) => {
    const point = await page.evaluate(() => {
      const viewport = fixture.view.viewport, object = [...viewport.sceneObjects.values()][0];
      const center = object.localToWorld(viewport.camera.position.clone().set(0,0,0)).project(viewport.camera);
      const box = viewport.renderer.domElement.getBoundingClientRect();
      return { x: box.x+(center.x+1)*box.width/2, y: box.y+(1-center.y)*box.height/2 };
    });
    if(hoverOnly)await page.mouse.move(point.x,point.y);
    else await page.mouse.click(point.x,point.y);
  };
  await clickWorkpiece();
  await page.waitForFunction(() => !fixture.view.pending && fixture.view.fiveAxisRegistrationPick?.prefix==='axisX');
  const picked = await page.evaluate(async () => { const d=(await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs')).currentDraft(fixture.view);return [...'XYZ'].map(axis=>Number(d[`origin${axis}`])); });
  assert.ok(Math.abs(Math.hypot(...picked)-30)<1e-7, 'surface pick projects onto the exact sphere');
  assert.ok(calls.some(call => call.method === 'CamPlacement.ResolveReference'));
  await page.locator('[data-five-command="five.registration.cancel"]').last().click();
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context, 'five.rough.face'));
  await clickWorkpiece(true);
  await page.waitForFunction(()=>fixture.view.fiveRegistrationInteraction?.error);
  assert.match(await page.locator('.five-reference-prompt').innerText(),/此面不是平面/);
  assert.equal(await page.evaluate(()=>fixture.view.fiveRegistrationInteraction.hover),null);
  await clickWorkpiece();
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.error);
  assert.match(await page.locator('[role="dialog"]').innerText(),/曲面不能用作平面/);
  assert.deepEqual(await rpc({ action: 'invoke', scope: 'scene', method: 'CamScene.ReadState', payload: {} }),state);
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context, 'five.tool.fine'));
  await page.locator('[data-five-field="measured1X"]').fill('123.45');
  await page.evaluate(() => {
    const input = document.querySelector('[data-five-field="measured1X"]'); input.focus({ preventScroll: true }); input.setSelectionRange(2,5);
    window.preserved = { input, canvas: document.querySelector('canvas'), calls:0 };
    input.addEventListener('test-preserved',()=>preserved.calls++);
    fixture.hold = true; void fixture.entry.handleRibbonCommand(fixture.context, 'five.refresh');
  });
  await page.waitForFunction(() => fixture.held);
  await page.locator('[data-five-field="measured1X"]').fill('1234.56');
  await page.evaluate(()=>preserved.input.setSelectionRange(2,5));
  await page.evaluate(() => fixture.release());
  await page.waitForFunction(() => !fixture.view.pending);
  const retained = await page.evaluate(() => {preserved.input.dispatchEvent(new Event('test-preserved'));return { input: preserved.input === document.querySelector('[data-five-field="measured1X"]'),
    canvas: preserved.canvas === document.querySelector('canvas'), focused: document.activeElement === preserved.input,
    selection: [preserved.input.selectionStart,preserved.input.selectionEnd], calls:preserved.calls, value:preserved.input.value };});
  assert.equal(retained.input, true);assert.equal(retained.canvas, true);assert.equal(retained.focused, true);
  assert.deepEqual(retained.selection, [2,5]);assert.equal(retained.calls,1);assert.equal(retained.value,'1234.56');
  await page.screenshot({ path: resolve(output, 'interaction-preserved.png') });
  await rpc({ action: 'undo' });
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context, 'five.refresh'));
  assert.equal(await page.evaluate(() => !!fixture.view.scene.domain.workpieces[0]?.id), false);
  await rpc({ action: 'redo' });
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context, 'five.refresh'));
  assert.equal(await page.evaluate(() => !!fixture.view.scene.domain.workpieces[0]?.id), true);
  await page.evaluate(() => fixture.entry.releaseProject(fixture.context));
  const reopened = await rpc({ action: 'save-reopen' });
  assert.deepEqual(reopened.workpieces, state.workpieces);assert.deepEqual(reopened.placements, state.placements);
  assert.notEqual(reopened.expected.epoch, state.expected.epoch);
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, cases: ['STEP chooser -> native async import -> committed placement -> real View resource -> SDK canvas',
    'indeterminate progress bar through import and display; cancellation and completion close it', 'import remains available for additional workpieces',
    'product channel rejected', 'exact sphere reference picking and nonplanar face rejection',
    'async refresh preserves canvas, input, listeners, focus, selection and newer typing', 'native undo/redo', 'source removed -> save/reopen'],
    coverage: { browser: 'Chromium Edge', fileChooser: 'production file preview picker; typed directory', scene: 'production SDO and Repository', rendering: 'production View and resources', standaloneCef: false }, state, reopened, retained, calls }, null, 2));
  console.log('Five-axis native browser import passed');
  }
} catch (error) {
  failure = error;
  console.error(calls.filter(c=>c.action==='invoke').slice(-8));
  if (page) { console.error(await page.evaluate(() => ({ text: document.body.innerText.slice(0,3500), error: window.fixture?.view.error }))); await page.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {}); }
  throw error;
} finally {
  writeFileSync(resolve(output, 'native.log'), nativeLog.join('\n'));
  await browser?.close();
  try { await rpc({ action: 'exit' }); } catch {}
  processHandle.stdin.end();
  if (failure) processHandle.kill();
}
