import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { verifyPlacementGizmo, gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';
import { verifyCameraNavigation } from './FiveAxisCameraNavigation.native.browser.mjs';
import { verifyScaleGizmo } from './FiveAxisScale.native.browser.mjs';
import { captureWorldFrame, verifyCenteredWorkbench } from './FiveAxisWorldFrame.native.browser.mjs';
import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('manual-setups-placement-20261030/cancel-browser');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root, 'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, process.env.ICAX_FIVE_AXIS_NATIVE_EXECUTABLE || 'FiveAxisCAMTest.exe'),
  ['--gtest_filter=FiveAxisBridge.DISABLED_Serve', '--gtest_also_run_disabled_tests'],
  { cwd: nativeDirectory, windowsHide: true, env: { ...process.env, ICAX_FIVE_AXIS_BRIDGE_BOX: '1' }, stdio: ['pipe','pipe','pipe'] });
let sequence = 0, browser, page, failure;
const waiting = new Map(), calls = [], errors = [], nativeLog = [];
const failAll = error => { for (const entry of waiting.values()) { clearTimeout(entry.timer); entry.reject(error); } waiting.clear(); };
processHandle.on('error', failAll);
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
  const timer = setTimeout(() => { waiting.delete(id); reject(Error(`Native timeout ${message.action}`)); }, 15000);
  waiting.set(id, { resolve, reject, timer });
  const wire = message.action === 'invoke'
    ? { ...message, payload: undefined, payloadText: message.payloadText ?? serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  const info = await rpc({ action: 'info' });
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge', headless: true });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor:Number(process.env.ICAX_BROWSER_DEVICE_SCALE_FACTOR||1) });
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
      async invoke(method, payload = {}) {
        (f.methods??=[]).push({method,at:performance.now()});
        if(['CamScene.ReadSnapFeatures','CamScene.ResolvePickReference'].includes(method))f.snapCalls=(f.snapCalls||0)+1;
        if (method === (f.holdMethod || 'CamScene.ReadState') && f.hold) { f.hold = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false; }
        const result = await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payload, payloadText:serializeVariantText(payload) });
        if (method === 'CamPlacement.SetTransform' && f.holdAfterApply) {
          f.holdAfterApply = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false;
        }
        if (['CamFixture.Confirm','CamPlacement.SetTransform','CamWorkpiece.Scale','CamPlacement.ApplyRegistration','CamWorkpiece.Remove','CamSetup.SetLocked','CamSetup.Manage','CamSetup.SetRegistrationPlan','CamWorkpiece.SetEnabled'].includes(method) && f.dropApply) { f.dropApply = false; throw Error('Injected lost apply receipt'); }
        if (method === 'CamScene.QueryCommand' && f.dropQuery) { f.dropQuery = false; throw Error('Injected unknown receipt'); }
        return result;
      }, async getState() { return this.state; } };
    const resource = async (url, options, head) => {
      const version = new Headers(options?.headers).get('ICAX-Resource-Version') ?? String(options?.version ?? '');
      const result = await fiveNativeRpc({ action: 'resource', url, version, head });
      return new Response(head ? null : Uint8Array.from(result.bytes), { headers: { 'Content-Type': 'application/vnd.icax.flatbuffer', 'ICAX-Resource-Version': result.version } });
    };
    sceneProxy.resources = { get: (url, options) => resource(url, options, false), head: (url, options) => resource(url, options, true) };
    sceneProxy.views = new ViewClient(sceneProxy);
    const context = f.context = { mount: document.querySelector('#app'), sceneProxy,
      projectProxy:{state:{}},
      project: { projectId: info.projectId, projectName: '工件平移与旋转 · 原生验收' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog() { ++f.fileSelections; return info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) {
        (f.methods??=[]).push({method,at:performance.now()}); return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);

  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id),idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await selectWorkpieceFile(page,process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||info.sourcePath);await idle();await command('five.setup.create');await idle();
  await command('five.fixture.generate');await command('five.fixture.design.auto');await idle();const before=await read();
  await page.evaluate(()=>{fixture.holdMethod='CamFixture.Generate';fixture.hold=true;});const pending=command('five.fixture.design.generate');await page.waitForFunction(()=>fixture.held);
  assert.equal(await page.locator('dialog.five-blocking-progress').count(),0);
  await command('five.fixture.design.cancel');assert.equal(await page.locator('.five-fixture-generator').count(),0);
  await page.evaluate(()=>fixture.release());await pending;await idle();assert.deepEqual(await read(),before);
  assert.equal(await page.locator('.five-fixture-generator').count(),0);assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,realNative:true,lateReceiptCancelled:true,committedStateUnchanged:true},null,2));console.log('PASS cancel during late native generation acknowledgement');
} catch(error){failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack},null,2));throw error;}
finally{writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();}
