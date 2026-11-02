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
const output = browserReportDirectory('manual-setups-placement-20261030/browser');
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
        if (['CamPlacement.SetTransform','CamWorkpiece.Scale','CamPlacement.ApplyRegistration','CamWorkpiece.Remove','CamSetup.SetLocked','CamSetup.Manage','CamSetup.SetRegistrationPlan','CamWorkpiece.SetEnabled'].includes(method) && f.dropApply) { f.dropApply = false; throw Error('Injected lost apply receipt'); }
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

  const cases=[],timings={},invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fivePlacement?.saving&&!fixture.view.fivePlacement?.request);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  assert.equal((await read()).setups.length,0);
  await selectWorkpieceFile(page,process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||info.sourcePath);
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  let state=await read();const originalCad=state.workpieces[0],placementId=state.placements[0].id;
  assert.equal(state.setups.length,0);assert.equal(state.fixtures.length,0);assert.equal(state.activeSetupId,'');
  assert.equal(await page.locator('.five-preparation-setup').count(),0);
  assert.equal(await page.locator('.five-preparation-workpiece').count(),1);
  const available=await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='workpiece').groups.flatMap(g=>g.commands).map(c=>({id:c.id,disabled:c.disabled})));
  assert.equal(available.find(c=>c.id==='five.setup.create').disabled,false);
  assert.equal(available.find(c=>c.id==='five.tool.translate').disabled,true);
  cases.push('Import shows one independent workpiece with zero setups and fixtures; adding the first setup is available explicitly');

  await command('five.setup.create');await idle();
  state=await read();assert.equal(state.setups.length,1);assert.equal(state.placements.length,1);
  assert.deepEqual(state.workpieces[0],originalCad);
  await command('five.tool.translate');
  const xInput=page.locator('[data-five-transform-field="position-X"]');
  await xInput.fill('43.75');
  await page.evaluate(()=>{fixture.hold=true;fixture.holdMethod='CamPlacement.SetTransform';fixture.methods=[];});
  const submit=xInput.press('Enter');await page.waitForFunction(()=>fixture.held);
  const interaction=await page.evaluate(()=>{
    const input=document.querySelector('[data-five-transform-field="position-X"]');fixture.inputNode=input;fixture.canvas=fixture.view.viewport.renderer.domElement;
    input.focus({preventScroll:true});input.setSelectionRange(1,3);fixture.inputEvents=0;input.addEventListener('icax-identity',()=>fixture.inputEvents++);
    const style=document.createElement('style');style.id='acceptance-scroll-style';style.textContent='.cam-context-pane,.cam-info-pane{height:150px!important;overflow:auto!important}[data-five-key="transform-position"]{height:40px!important;overflow:auto!important}';document.head.append(style);
    const panes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')];
    for(const p of panes)p.scrollTop=30;
    const nested=input.closest('section[data-five-key="transform-position"]');fixture.nested=nested;nested.scrollTop=20;
    // Scroll after request dispatch: restoration must use these latest values.
    for(const p of panes)p.scrollTop=65; nested.scrollTop=47;
    return {scroll:panes.map(p=>p.scrollTop),nested:nested.scrollTop,selection:[input.selectionStart,input.selectionEnd]};
  });
  await page.evaluate(()=>fixture.release());await submit;await idle();
  const restored=await page.evaluate(()=>{
    fixture.inputNode.dispatchEvent(new Event('icax-identity'));
    return {same:fixture.inputNode===document.querySelector('[data-five-transform-field="position-X"]'),canvas:fixture.canvas===fixture.view.viewport.renderer.domElement,
      focused:document.activeElement===fixture.inputNode,selection:[fixture.inputNode.selectionStart,fixture.inputNode.selectionEnd],
      scroll:[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')].map(p=>p.scrollTop),events:fixture.inputEvents,
      nested:fixture.nested.scrollTop,
      methods:fixture.methods.map(x=>x.method),refresh:fixture.view.sceneRefreshPending};
  });
  assert.equal(restored.same,true);assert.equal(restored.canvas,true);assert.equal(restored.focused,true);assert.equal(restored.events,1);
  assert.deepEqual(restored.selection,interaction.selection);assert.deepEqual(restored.scroll,interaction.scroll);
  assert.equal(restored.nested,interaction.nested);
  assert.ok(!restored.methods.some(m=>['CamScene.ReadState','Machine.List','MachineDefinition.List'].includes(m)));assert.ok(!restored.refresh);
  assert.equal((await read()).placements[0].transform[3],43.75);
  await page.evaluate(()=>document.querySelector('#acceptance-scroll-style').remove());
  cases.push('Delayed native placement receipt preserves input, canvas, listener, current selection and both latest sidebar scroll positions without a complete engineering-state read');

  // A slow disk writer must not hold the next gesture. The real native archive
  // persistence is independently timed below; this gate isolates UI behavior.
  await page.evaluate(()=>{
    Object.assign(fixture.context.projectProxy,{state:{projectPath:'isolated-acceptance.i5cam'},async autosave(){fixture.saveCalls=(fixture.saveCalls||0)+1;
      if(fixture.holdSave){fixture.holdSave=false;await new Promise(r=>fixture.releaseSave=r);}return {saved:true};}});
    fixture.holdSave=true;
  });
  await xInput.fill('48.5');await xInput.press('Enter');await idle();await page.waitForFunction(()=>!!fixture.releaseSave);
  assert.equal(await page.evaluate(()=>fixture.view.pending),false);
  await xInput.fill('51.25');await xInput.press('Enter');await idle();assert.equal((await read()).placements[0].transform[3],51.25);
  await page.evaluate(()=>fixture.releaseSave());
  cases.push('A deliberately held disk save allows the next placement edit to commit; the save writer coalesces consecutive changes');

  const {gizmoDriver}=await import('./FiveAxisPlacementGizmo.native.browser.mjs');const driver=gizmoDriver(page);
  const angles=[];
  for(const projection of ['perspective','orthographic'])for(const space of ['world','local']){
    await page.locator(`button[data-projection-mode="${projection}"]`).click();await page.locator(`[data-transform-space="${space}"]`).click();
    await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.7]);fixture.view.viewport.fitView();});
    await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
    for(const axis of ['X','Y','Z','E']){
      await command('five.tool.rotate');
      const p=await driver.locate(axis);await page.mouse.move(p.x,p.y);await page.mouse.down();
      await page.mouse.move(p.x+48,p.y+30,{steps:5});
      const right=Number(await page.locator('[data-five-placement-field="value"]').inputValue());assert.equal(right,24);
      await page.mouse.move(p.x-36,p.y-20,{steps:5});
      const left=Number(await page.locator('[data-five-placement-field="value"]').inputValue());assert.equal(left,-18);
      await page.mouse.move(p.x-36,p.y+60,{steps:3});assert.equal(Number(await page.locator('[data-five-placement-field="value"]').inputValue()),-30);
      await page.mouse.move(p.x+20,p.y-70,{steps:3});assert.equal(Number(await page.locator('[data-five-placement-field="value"]').inputValue()),35);
      await page.mouse.move(p.x-36,p.y-20,{steps:3});
      const beforeMethods=await page.evaluate(()=>fixture.methods.length);const start=performance.now();
      await page.mouse.up();await idle();angles.push({projection,space,axis,right,left,commitMs:performance.now()-start});
      const methods=await page.evaluate(n=>fixture.methods.slice(n).map(x=>x.method),beforeMethods);
      assert.ok(!methods.some(m=>['CamScene.ReadState','Machine.List','MachineDefinition.List'].includes(m)));
      assert.equal((await read()).intentGenerationTasks.length,0);
    }
  }
  timings.rotationCommits=angles;
  assert.deepEqual((await read()).workpieces[0],originalCad);
  cases.push('Actual X/Y/Z/view mouse rotation uses the signed dominant horizontal/up displacement in both projections and frames; right/up increase and left/down decrease; no toolpath generation or whole-state refresh follows release');

  // Lost acknowledgements recover the exact same compact native receipt.
  await command('five.tool.translate');await page.evaluate(()=>{fixture.dropApply=true;fixture.methods=[];});
  await xInput.fill('62.5');await xInput.press('Enter');await idle();assert.equal((await read()).placements[0].transform[3],62.5);
  assert.ok((await page.evaluate(()=>fixture.methods.map(x=>x.method))).includes('CamScene.QueryCommand'));
  cases.push('A lost transform reply recovers idempotently without re-reading the full scene');

  const beforeSave=await read();const archive=resolve(output,'placement.i5cam');let start=performance.now();
  await rpc({action:'export-project',path:archive});timings.fullArchiveSaveMs=performance.now()-start;
  await command('five.setup.remove');await idle();state=await read();assert.equal(state.setups.length,0);assert.equal(state.placements.length,1);
  assert.deepEqual(state.workpieces[0],originalCad);assert.equal(await page.locator('.five-preparation-setup').count(),0);
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.equal((await read()).setups.length,1);
  await rpc({action:'redo'});await command('five.refresh');await idle();assert.equal((await read()).setups.length,0);
  const noSetup=await read();await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  start=performance.now();const reopened=await rpc({action:'save-reopen'});timings.saveAndReopenMs=performance.now()-start;
  assert.equal(reopened.setups.length,0);assert.deepEqual(reopened.placements,noSetup.placements);assert.deepEqual(reopened.workpieces,noSetup.workpieces);
  cases.push('Deleting the final setup leaves the workpiece with zero setups; undo/redo and actual native archive save/reopen preserve that state');
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,timings,coverage:{native:true,realMouse:true,standaloneCef:false},calls},null,2));
  console.log(JSON.stringify({passed:true,cases,timings,output}));
} catch(error) {
  failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));
  if(page){console.error(await page.evaluate(()=>({text:document.body.innerText.slice(0,2000),error:window.fixture?.view.error})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();
  try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}

