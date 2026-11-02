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
const output = browserReportDirectory('setup-redesign-browser');
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
      project: { projectId: info.projectId, projectName: '工件平移与旋转 · 原生验收' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog() { ++f.fileSelections; return info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) { return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);

  const cases=[],invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const refresh=async()=>{await command('five.refresh');await idle();};
  const tab=async id=>{await page.evaluate(id=>fixture.context.actions.selectRibbonTab(id),id);await idle();};
  const click=async id=>{await page.locator(`[data-five-command="${id}"]`).last().click();await idle();};
  await selectWorkpieceFile(page,process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||info.sourcePath);
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  let state=await read();const first=state.placements[0];
  const tabs=await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs);
  assert.ok(tabs.find(t=>t.id==='workpiece').groups.flatMap(g=>g.commands).some(c=>c.title==='缩放'));
  assert.ok(!tabs.find(t=>t.id==='workpiece').groups.flatMap(g=>g.commands).some(c=>['摆放','平移','旋转'].includes(c.title)));
  assert.ok(tabs.find(t=>t.id==='fixture').groups.flatMap(g=>g.commands).some(c=>c.title==='三点定位'));
  assert.ok(!tabs.some(t=>t.id==='toolpath'));
  await tab('workpiece');
  await tab('workpiece');await page.locator('.five-placement-tools').waitFor();
  assert.equal(await page.locator('.five-placement-tools button').count(),2);
  assert.equal(await page.locator('[data-five-transform-field^="scale"]').count(),0);
  await command('five.tool.setup');await click('five.setup.create');
  assert.equal(await page.locator('.five-draft-note,[data-five-command="five.apply"]').count(),0);
  state=await read();assert.equal(state.workpieces.length,1);assert.equal(state.placements.length,2);assert.equal(state.setups.length,2);
  const second=state.placements.find(p=>p.id!==first.id);assert.equal(second.workpieceId,first.workpieceId);
  await command('five.tool.translate');
  await page.locator('[data-five-transform-field="position-X"]').fill('43.75');
  await page.evaluate(()=>{fixture.hold=true;fixture.holdMethod='CamPlacement.SetTransform';});
  const submit=page.locator('[data-five-transform-field="position-X"]').press('Enter');await page.waitForFunction(()=>fixture.held);
  const interaction=await page.evaluate(()=>{
    const input=document.querySelector('[data-five-transform-field="position-X"]');fixture.parameterNode=input;input.focus({preventScroll:true});input.setSelectionRange(1,3);
    const panes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')];for(const p of panes){p.style.maxHeight='180px';p.scrollTop=75;}
    return {scroll:panes.map(p=>p.scrollTop),selection:[input.selectionStart,input.selectionEnd]};
  });
  await page.evaluate(()=>fixture.release());await submit;await idle();
  const restored=await page.evaluate(()=>({same:fixture.parameterNode===document.querySelector('[data-five-transform-field="position-X"]'),focused:document.activeElement===fixture.parameterNode,scroll:[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')].map(p=>p.scrollTop),selection:[fixture.parameterNode.selectionStart,fixture.parameterNode.selectionEnd]}));
  assert.equal(restored.same,true);assert.equal(restored.focused,true);assert.deepEqual(restored.selection,interaction.selection);assert.deepEqual(restored.scroll,interaction.scroll);
  await page.evaluate(()=>{for(const p of document.querySelectorAll('.cam-context-pane,.cam-info-pane'))p.style.maxHeight='';});
  state=await read();assert.equal(state.placements.find(p=>p.id===second.id).transform[3],43.75);
  assert.deepEqual(state.placements.find(p=>p.id===first.id).transform,first.transform);
  await click('workpiece.lock');assert.ok((await read()).setups.find(s=>s.id===second.setupId).locked);
  await click(`five.setup.activate:${first.id}`);await command('five.tool.translate');
  assert.equal(await page.locator('[data-five-transform-field="position-X"]').getAttribute('readonly'),null);
  await tab('workpiece');assert.equal(await page.locator('[data-five-workpiece]').count(),1);
  assert.ok(await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='workpiece').groups.flatMap(g=>g.commands).find(c=>c.id==='five.tool.scale').disabled));
  cases.push('Real round-cover geometry has two independent setups; pose locks are independent; asynchronous numeric apply preserves the input node, focus, selection and both latest sidebar scroll positions');
  await tab('workpiece');await click(`five.setup.activate:${second.id}`);await click('workpiece.lock');
  await tab('workpiece');await command('five.tool.scale');
  const old=(await read()).workpieces[0].geometryRef;
  await page.locator('[data-five-transform-field="scale-X"]').fill('1.1');await page.locator('[data-five-transform-field="scale-X"]').press('Enter');await idle();
  state=await read();assert.notDeepEqual(state.workpieces[0].geometryRef,old);assert.equal(state.workpieces.length,1);
  assert.equal(await page.locator('[data-five-transform-field^="position"]').count(),0);
  assert.equal(await page.locator('[data-five-transform-field^="rotation"]').count(),0);
  cases.push('Geometry scaling updates the shared BRep and every placement preview without introducing a placement scale in the setup page');
  await tab('workpiece');await command('five.tool.registration');await click('five.locating.pick:0');
  assert.equal(await page.locator('.five-draft-note,[data-five-command="five.apply"]').count(),0);
  await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.scene.activePlacementId));
  for(let index=0;index<3;++index){
    let found;
    for(const eye of [[0,0,1],[0,0,-1],[1,-1,1],[-1,1,1]]){
      found=await page.evaluate(async({eye,index})=>{
        const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),{sceneSnapCandidate}=await import('/src/apps/five-axis-cam/webpage/sceneSnapping.mjs');
        const v=fixture.view,vp=v.viewport,id=v.scene.activePlacementId,object=vp.sceneObjects.get(id),catalog=v.sceneSnapping.catalogs.get(id),selected=v.fiveLocating.points.filter(Boolean).map(p=>new T.Vector3(...p.point));
        vp.setViewDirection(eye);vp.fitView(1.3);vp.scene.updateMatrixWorld(true);const r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
        for(const f of catalog.features.filter(f=>['endpoint','center','datum-point','sphere-center'].includes(f.mode))){
          const p=new T.Vector3(...f.point),s=p.clone().applyMatrix4(object.matrixWorld).project(vp.camera),x=r.left+(s.x+1)*r.width/2,y=r.top+(1-s.y)*r.height/2;if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
          ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},vp.camera);const hit=ray.intersectObject(object,false)[0];
          const c=sceneSnapCandidate(fixture.context,v,{objectId:id,allowedModes:['endpoint','midpoint','center','sphere-center','datum-point','point']},hit,{clientX:x,clientY:y});
          if(!c?.result)continue;const actual=new T.Vector3(...c.result.point);
          if(selected.some(p=>p.distanceTo(actual)<5))continue;
          if(selected.length===2&&selected[1].clone().sub(selected[0]).cross(actual.clone().sub(selected[0])).length()<10)continue;
          return {x,y,point:c.result.point};
        }return null;
      },{eye,index});if(found)break;
    }
    assert.ok(found,`Visible locating feature ${index+1}`);await page.mouse.move(found.x,found.y);await page.mouse.click(found.x,found.y);await idle();
    assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(await page.evaluate(i=>fixture.view.fiveLocating.points.filter(Boolean).length,index),index+1);
  }
  await page.evaluate(()=>{fixture.dropApply=true;});await click('five.locating.save');
  state=await read();const plan=state.setups.find(s=>s.id===second.setupId).registrationPlan;
  assert.equal(plan.status,'Prepared');assert.equal(plan.machineStatus,'Unmeasured');assert.equal(plan.points.length,3);assert.equal(plan.coordinateFrame,'Setup');
  await page.evaluate(path=>{fixture.context.appProxy.bridge.saveFileDialog=async()=>path;},resolve(output,'locating-points.json'));await click('five.locating.export');
  const exported=JSON.parse(readFileSync(resolve(output,'locating-points.json'),'utf8'));assert.deepEqual(exported.points,plan.points);
  await page.screenshot({path:resolve(output,'round-cover-three-points.png')});
  cases.push('Three points are picked with actual mouse input from the existing BRep snap service, saved with idempotent receipt recovery, and exported in the Setup frame as Unmeasured');
  await command('five.tool.translate');await page.locator('[data-five-transform-field="position-Y"]').fill('17.25');await page.locator('[data-five-transform-field="position-Y"]').press('Enter');await idle();
  await command('five.tool.registration');assert.equal((await read()).setups.find(s=>s.id===second.setupId).registrationPlan.status,'Stale');
  assert.ok(await page.locator('[data-five-command="five.locating.export"]').isDisabled());
  await click('five.locating.save');await idle();
  const before=await read();await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.placements,before.placements);assert.deepEqual(reopened.workpieces,before.workpieces);
  assert.deepEqual(reopened.setups.find(s=>s.id===second.setupId).registrationPlan,before.setups.find(s=>s.id===second.setupId).registrationPlan);
  cases.push('Changing placement invalidates the locating record; re-saving resolves the feature queries again; native project save/reopen preserves both setups and locating points');
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:true,realRoundCover:true,mouseSnap:true,saveReopen:true,standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
} catch(error) {
  failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));
  if(page){console.error(await page.evaluate(()=>({text:document.body.innerText.slice(0,2000),error:window.fixture?.view.error})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();
  try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}

