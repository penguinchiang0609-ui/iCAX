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
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await idle();};
  const tab=async id=>{await page.evaluate(id=>fixture.context.actions.selectRibbonTab(id),id);await idle();};
  const click=async id=>{await page.locator(`[data-five-command="${id}"]`).last().click();await idle();};
  const tabs=()=>page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.map(t=>t.id));
  assert.deepEqual(await tabs(),['workpiece','toolpath','simulation','help']);
  await selectWorkpieceFile(page,resolve('TestData/FA/刀路/刀路/AI构造刀路/带底座球体-球面底座孔-多面空移.step'));
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  assert.ok((await tabs()).includes('toolpath'));
  await click('five.setup.activate:'+(await read()).placements[0].id);
  await command('five.tool.translate');await page.locator('[data-five-transform-field="position-X"]').fill('12.5');
  await page.locator('[data-five-transform-field="position-X"]').press('Enter');await idle();let state=await read();assert.equal(state.placements[0].transform[3],12.5);
  assert.ok((await tabs()).includes('toolpath'));cases.push('CAM remains visible and becomes editable after import; numeric placement edits commit directly');
  await tab('toolpath');await command('five.tool.manual-extract');
  await page.evaluate(async()=>{fixture.extraction=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');});
  const placement=state.placements[0],ranges=(await invoke('CamPlacement.ReadReferenceFaces',{expected:state.expected,placementId:placement.id,setupId:placement.setupId})).faces;
  // Use native fixed-view triangle/face identity only to locate actual mouse targets.
  // Production pick handling, candidate preview and creation remain the normal UI.
  for(const faceId of ['1','4','6']){
    let point;
    for(const eye of [[1,-1,.35],[1,-1,1],[0,0,1],[-1,1,.1],[0,-1,-.3]]){
      point=await page.evaluate(async({ranges,faceId,eye})=>{
        const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,o=vp.sceneObjects.get(v.scene.activePlacementId);
        vp.setViewDirection(eye);vp.fitView(1.2);vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
        const box=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
        for(let y=.17;y<.87;y+=.025)for(let x=.15;x<.86;x+=.025){
          const clientX=box.left+x*box.width,clientY=box.top+y*box.height;if(document.elementFromPoint(clientX,clientY)!==vp.renderer.domElement)continue;
          ray.setFromCamera({x:x*2-1,y:1-y*2},vp.camera);const hit=ray.intersectObject(o,false)[0];if(!hit)continue;
          const f=ranges.find(f=>hit.faceIndex>=Number(f.first)&&hit.faceIndex<Number(f.first)+Number(f.count));if(f?.faceId===faceId)return{x:clientX,y:clientY};
        }return null;
      },{ranges,faceId,eye});if(point)break;
    }
    assert.ok(point,`visible mouse target for exterior face ${faceId}`);await page.mouse.click(point.x,point.y);await idle();
    assert.equal(await page.evaluate(()=>fixture.view.error),'');
  }
  assert.deepEqual(await page.evaluate(()=>fixture.extraction.extractionDraft(fixture.view).extraction.faces.map(f=>f.localId).sort()),['1','4','6']);
  await click('five.extraction.run');const candidates=await page.evaluate(()=>fixture.extraction.extractionResult(fixture.view)?.candidates);
  assert.equal(candidates.length,4);assert.ok(candidates.every(c=>c.confirmationEligibility==='Ready'));assert.equal(candidates.filter(c=>c.machiningKind==='Mark').length,3);assert.equal(candidates.filter(c=>c.boundaryRole==='OuterTrim').length,1);
  await page.screenshot({path:resolve(output,'sphere-exterior-candidates.png')});
  await click('five.extraction.confirm');state=await read();assert.equal(state.intentToolpaths.length,4);
  await page.waitForFunction(()=>fixture.view.fiveToolAxisDisplay?.group?.children.length===4&&!fixture.view.fiveToolAxisDisplay.loading);
  const display=await page.evaluate(()=>({visible:fixture.view.fiveToolAxisDisplay.visible,rows:fixture.view.fiveToolAxisDisplay.group.children.map(g=>({id:g.userData.intentId,count:g.userData.renderedCount}))}));
  assert.ok(display.visible);assert.ok(display.rows.every(r=>r.count>0));
  await page.screenshot({path:resolve(output,'sphere-all-tool-axes.png')});
  cases.push('Actual pointer selection of cylinder, annulus and sphere yields three marking paths and the missing basal trim; all four display real native tool-axis keypoints');
  await click('five.normals.toggle');assert.equal(await page.evaluate(()=>fixture.view.fiveToolAxisDisplay.group),null);await click('five.normals.toggle');
  await page.waitForFunction(()=>fixture.view.fiveToolAxisDisplay?.group?.children.length===4);
  const mark=state.intentToolpaths.find(c=>c.machiningKind==='Mark');assert.ok(mark);
  await click('five.pose-select.toggle');
  const target=await page.evaluate(async id=>{
    const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),{pickDisplayedToolAxis}=await import('/src/apps/five-axis-cam/webpage/toolAxisPicking.mjs'),v=fixture.view,vp=v.viewport;
    vp.setViewDirection([1,-1,.5]);vp.fitView(1.25);await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
    const rect=vp.renderer.domElement.getBoundingClientRect();
    for(const g of v.fiveToolAxisDisplay.group.children)if(g.userData.intentId===id){const m=g.children.find(m=>m.userData.kind==='axis');for(const s of m.userData.toolAxisSamples){
      const p=new T.Vector3(...s.point).applyMatrix4(m.matrixWorld).project(vp.camera),x=rect.left+(p.x+1)*rect.width/2,y=rect.top+(1-p.y)*rect.height/2;
      if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;const hit=pickDisplayedToolAxis(v,{clientX:x,clientY:y});if(hit?.intentId===id)return{x,y};
    }}return null;
  },mark.id);assert.ok(target);await page.mouse.click(target.x,target.y);
  await page.waitForFunction(()=>fixture.view.fiveAxisPointEdit?.axis&&!fixture.view.fiveAxisPointEdit.loading);
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.id),mark.id);
  await command('five.points.edit');const before=await page.evaluate(()=>fixture.view.fiveAxisPointEdit.axis.direction);
  await click('five.points.reverse');await click('five.points.apply');
  const after=await invoke('CamCut.ReadToolAxis',{expected:(await read()).expected,intentId:mark.id});for(let i=0;i<3;++i)assert.ok(Math.abs(after.direction[i]+before[i])<1e-7);
  cases.push('The display switch hides and restores all axes; a marking-path arrow is picked with the mouse, reversed in the point editor and saved through the native Scene');
  await tab('workpiece');await command('five.tool.setup');await click('five.setup.remove');assert.ok((await tabs()).includes('toolpath'));
  await rpc({action:'undo'});await command('five.refresh');assert.ok((await tabs()).includes('toolpath'));
  const beforeSave=await read();await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.intentToolpaths,beforeSave.intentToolpaths);assert.deepEqual(reopened.setups[0].definitionOrder,beforeSave.setups[0].definitionOrder);assert.deepEqual(reopened.placements,beforeSave.placements);cases.push('Deleting the last setup keeps CAM accessible; undo restores its paths and axes; native save/reopen preserves setup and edited marking posture');
  assert.deepEqual(errors,[]);writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:true,realSphere:true,mouseFaceSelection:true,mouseAxisSelection:true,saveReopen:true,standaloneCef:false}},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
}catch(error){failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));if(page){console.error(await page.evaluate(()=>({error:fixture.view.error,text:document.body.innerText.slice(0,3000)})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}throw error;}
finally{writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();}
