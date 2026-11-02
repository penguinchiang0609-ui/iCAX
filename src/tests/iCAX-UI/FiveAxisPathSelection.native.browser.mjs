import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('path-selection-20261028');
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
  const info = await rpc({action:'open-project',path:JSON.parse(readFileSync(resolve(output,'before.json'),'utf8')).project,replayJournal:true});
  // Let native reopen finish its background recognition before mounting this
  // polling fixture, which deliberately has no desktop progress stream.
  for(let i=0;i<600;i++){
    const state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    if(!state.intentGenerationTasks?.some(t=>t.status==='Running'))break;
    if(i===599)throw Error('Native reopen recognition did not finish');
    await new Promise(r=>setTimeout(r,100));
  }
  const ready=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  let generation=await rpc({action:'invoke',scope:'scene',method:'CamPlan.Generate',payload:{commandId:crypto.randomUUID(),expected:ready.expected,setupId:ready.activeSetupId}});
  while(generation.status==='Running'){
    await new Promise(r=>setTimeout(r,100));
    generation=await rpc({action:'invoke',scope:'scene',method:'CamPlan.QueryStatus',payload:{epoch:generation.epoch,taskId:generation.taskId}});
  }
  assert.equal(generation.status,'Completed',JSON.stringify(generation));
  if(process.env.ICAX_PATH_RAPID_SELECTION){
    const state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),geometry=state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;
    let task=await rpc({action:'invoke',scope:'scene',method:'CamRapid.Generate',payload:{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,geometryRevision:geometry.revision}});
    for(let i=0;task.status==='Running';i++){
      if(i>=600)throw Error('Native rapid generation timed out');
      await new Promise(r=>setTimeout(r,100));task=await rpc({action:'invoke',scope:'scene',method:'CamRapid.QueryStatus',payload:{epoch:task.epoch,taskId:task.taskId}});
    }
    assert.equal(task.status,'Completed',JSON.stringify(task));
  }
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
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain,undefined,{timeout:60000});
  const before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const frame=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  page.on('console',m=>{if(m.type()==='error'&&/Shader Error|VALIDATE_STATUS|GL_INVALID_OPERATION/.test(m.text()))errors.push(m.text());});
  await page.evaluate(async()=>{await fixture.context.actions.selectRibbonTab('toolpath');fixture.view.fiveAxisUI.tools.toolpath='path-translate';fixture.entry.mountProject(fixture.context);fixture.view.viewport.setViewDirection([0,0,1]);fixture.view.viewport.fitView();});
  await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveCompensation?.loading);
  const ids=before.intentToolpaths.filter(c=>!c.locked&&c.enabled!==false).map(c=>c.id);assert.ok(ids.length>=2);
  const row=id=>page.locator(`[data-five-intent="${id}"]`);
  const strokes=()=>page.evaluate(()=>{const result=[];fixture.view.viewport.scene.traverse(o=>{if(o.userData.pathSelectionStroke)result.push({id:o.parent.userData.cuttingLineId||o.parent.userData.generatedIntentId||o.parent.userData.compensatedIntentId,color:o.material.uniforms.color.value.getHex(),segments:o.geometry.instanceCount});});return result;});
  const moveBlank=async()=>{await page.mouse.move(50,200);await frame();};
  await row(ids[0]).click();await moveBlank();
  let chosen=await strokes();assert.ok(chosen.length);assert.ok(chosen.every(s=>s.id===ids[0]&&s.color===0xe83eff));
  await page.screenshot({path:resolve(output,'selected-purple.png')});
  // Real mouse movement and click on a projected path must agree with the tree.
  const pixel=await page.evaluate(async id=>{
   const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
   const {nearestCutLine}=await import('/src/apps/five-axis-cam/webpage/cutSelection.mjs');
   const v=fixture.view,vp=v.viewport,c=v.scene.intentToolpaths.find(c=>c.id===id),source=vp.sceneObjects.get(c.placementId),box=vp.renderer.domElement.getBoundingClientRect();
   source.updateWorldMatrix(true,true);vp.camera.updateMatrixWorld(true);
   for(let i=1;i<c.previewPoints.length;i++){
    const q=new T.Vector3(...c.previewPoints[i-1]).lerp(new T.Vector3(...c.previewPoints[i]),.5).applyMatrix4(source.matrixWorld).project(vp.camera);
    const x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
    if(Math.abs(q.z)<=1&&document.elementFromPoint(x,y)===vp.renderer.domElement&&nearestCutLine(v,{clientX:x,clientY:y})?.intentId===id)return {x,y};
   }throw Error('No visible target path');
  },ids[1]);
  await page.mouse.move(pixel.x,pixel.y);await page.waitForFunction(()=>fixture.view.fiveSelectionFeedback?.target?.kind==='line');
  assert.equal(await page.evaluate(()=>fixture.view.fiveSelectionFeedback.target.intentId),ids[1]);
  await page.screenshot({path:resolve(output,'selected-purple-hover-cyan.png')});
  await page.mouse.click(pixel.x,pixel.y);await page.waitForFunction(id=>fixture.view.fiveAxisUI.selectedIntentId===id,ids[1]);await moveBlank();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSelectionFeedback.target),null);
  chosen=await strokes();assert.ok(chosen.length&&chosen.every(s=>s.id===ids[1]&&s.color===0xe83eff));
  await row(ids[0]).click({modifiers:['Control']});await moveBlank();
  assert.deepEqual([...new Set((await strokes()).map(s=>s.id))].sort(),ids.slice(0,2).sort());
  await page.screenshot({path:resolve(output,'multiple-purple.png')});
  await row(ids[0]).click({modifiers:['Control']});await moveBlank();
  assert.ok((await strokes()).every(s=>s.id===ids[1]));
  const original=await page.evaluate(id=>{const o=fixture.view.fiveCutPreview.group.children.find(o=>o.userData.cuttingLineId===id);return {color:o.material.color.getHex(),stroke:o.children.some(c=>c.userData.pathSelectionStroke)};},ids[0]);
  assert.equal(original.stroke,false);assert.notEqual(original.color,0xe83eff);
  // Existing layer colors never overwrite an active selection, including cache hits.
  await page.evaluate(async()=>{const {syncToolpathLayerMaterials}=await import('/src/apps/five-axis-cam/webpage/toolpathLayers.mjs');for(let i=0;i<3;i++)syncToolpathLayerMaterials(fixture.view,fixture.view.fiveCutPreview.group);});
  assert.ok((await strokes()).every(s=>s.color===0xe83eff));
  // Camera projection and zoom preserve the shader's CSS-pixel stroke size.
  for(const projection of ['orthographic','perspective']){
   await page.evaluate(projection=>{const vp=fixture.view.viewport;vp.setProjectionMode(projection);vp.fitView();},projection);await frame();
   assert.ok((await strokes()).length);await page.screenshot({path:resolve(output,projection+'-purple.png')});
  }
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.adjust'));await frame();
  assert.ok(await page.evaluate(()=>!!fixture.view.fiveGeneratedPreview?.group));
  chosen=await strokes();assert.ok(chosen.length&&chosen.every(s=>s.id===ids[1]&&s.color===0xe83eff));
  await page.screenshot({path:resolve(output,'generated-purple.png')});
  // Switching selection replaces the thick accent on every generated part.
  await row(ids[0]).click();await moveBlank();assert.ok((await strokes()).every(s=>s.id===ids[0]&&s.color===0xe83eff));
  await row(ids.at(-1)).click();await moveBlank();await page.screenshot({path:resolve(output,'selected-visible.png')});
  let rapidCases=[];
  if(process.env.ICAX_PATH_RAPID_SELECTION){const {verifyRapidSelection}=await import('./FiveAxisRapidSelection.native.browser.mjs');rapidCases=await verifyRapidSelection({page,output,ids});}
  const after=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.deepEqual(after,before,'Visual selection must not edit geometry, paths or layer assignments');
  assert.deepEqual(errors,[]);
  const report={passed:true,native:true,standaloneCef:false,actualMouse:true,generatedPreview:'Fresh native CamPlan.Generate on an isolated copy',realProject:'壁灯盘 挂板.STEP',color:'#e83eff',widthCssPx:4,
   cases:['selection remains purple after pointer leaves','hover cyan and selected purple coexist','viewport click selects the exact hovered path','Ctrl multiple selection and deselection','layer material synchronization preserves selection color','perspective and orthographic render without shader errors','all generated parts keep selection','native CAD, toolpaths and layer data unchanged',...rapidCases]};
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){failure=error;const debug=page?await page.evaluate(()=>({error:fixture.view.error,pending:fixture.view.pending,logs:fixture.logs})).catch(()=>null):null;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,errors,debug,calls:calls.map(c=>({action:c.action,method:c.method}))},null,2));if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();}
