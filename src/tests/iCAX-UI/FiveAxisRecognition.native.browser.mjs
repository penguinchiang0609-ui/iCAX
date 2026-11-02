import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
import { verifyViewportDisplay } from './ViewportDisplay.native.browser.mjs';
import { verifyCutEditing } from './FiveAxisCutEditing.native.browser.mjs';
import { verifyStandardHoles } from './FiveAxisStandardHoles.native.browser.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('five-axis-recognition-browser');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root,'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, 'FiveAxisCAMTest.exe'),
  ['--gtest_filter=FiveAxisBridge.DISABLED_Serve', '--gtest_also_run_disabled_tests'],
  { cwd: nativeDirectory, windowsHide: true, env: { ...process.env, ICAX_FIVE_AXIS_BRIDGE_RECOGNITION: '1' }, stdio: ['pipe','pipe','pipe'] });
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
    ? { ...message, payload: undefined, payloadText: serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  const info = await rpc({ action: 'info' });
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ ...(process.env.ICAX_BROWSER_EXECUTABLE ? {executablePath:process.env.ICAX_BROWSER_EXECUTABLE} : {channel:process.env.ICAX_BROWSER_CHANNEL || 'msedge'}), headless: true });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
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
    const entry = await import('/src/apps/five-axis-cam/webpage/entry.mjs');
    const { ViewClient } = await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const view = getProjectView(info.projectId);
    const f = window.fixture = { entry, view, fileSelections: 0, logs: [], hold: false, held: false, release: null };
    const sceneProxy = { sceneId: info.sceneId, state: { sceneId: info.sceneId }, pdo: { enabled: false },
      async invoke(method, payload = {}) {
        if (method === (f.holdMethod || 'CamScene.ReadState') && f.hold) { f.hold = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false; }
        const result = await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payload });
        if (method === 'CamCut.Create' && f.loseCreateResponse) { f.loseCreateResponse = false; throw Error('Injected lost confirmation response'); }
        if (method === f.loseMethod) { f.loseMethod = null; throw Error('Injected lost editing response'); }
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
      project: { projectId: info.projectId, projectName: '封头曲面导入 · 原生验收' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog() { ++f.fileSelections; return info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) { return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);
  await selectWorkpieceFile(page, info.sourcePath);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain?.workpieces?.[0]?.id);
  await page.waitForFunction(() => fixture.view.viewport?.getDebugState()?.objectCount === 1);
  assert.equal(await page.evaluate(() => fixture.fileSelections), 1);
  const state = await rpc({ action: 'invoke', scope: 'scene', method: 'CamScene.ReadState', payload: {} });
  assert.ok(state.workpieces[0]?.id); assert.equal(state.placements.length, 1);
  assert.equal(state.workpieces[0].importMetadata.geometryLengthUnit, 'mm');
  assert.equal(state.workpieces[0].importMetadata.validBRep, true);
  assert.ok(calls.some(call => call.method === 'CamWorkpiece.Import'));
  assert.ok(!calls.some(call => ['WorkpieceModel.Import','Workpiece.Instantiate','Job.Get'].includes(call.method)));
  await assert.rejects(rpc({ action: 'invoke', scope: 'product', method: 'CamScene.ReadState', payload: {} }), /SCENE_CHANNEL/);
  await page.screenshot({ path: resolve(output, 'imported.png') });

  await verifyViewportDisplay({page,rpc,calls,output});

  // Extraction UI is covered by FiveAxisFacamExtraction.native.browser.mjs.
  // Seed an explicitly qualified circular opening for the process-editor regression.
  await page.evaluate(async()=>{
    window.extractionModule=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
    await fixture.entry.handleRibbonCommand(fixture.context,'five.tool.manual-extract');
  });
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const screenPoint=await page.evaluate(()=>{
    const v=fixture.view.viewport,object=[...v.sceneObjects.values()][0];
    const p=object.localToWorld(v.camera.position.clone().set(10,10,10)).project(v.camera),box=v.renderer.domElement.getBoundingClientRect();
    return{x:box.x+(p.x+1)*box.width/2,y:box.y+(1-p.y)*box.height/2};
  });
  await page.mouse.click(screenPoint.x,screenPoint.y);
  await page.waitForFunction(()=>!fixture.view.pending&&extractionModule.extractionDraft(fixture.view).extraction.faces.length===1);
  const seed=await page.evaluate(async()=>{
    const invoke=(m,p)=>fixture.context.sceneProxy.invoke(m,p),d=fixture.view.scene.domain,placement=d.placements[0];
    const request={commandId:crypto.randomUUID(),expected:d.expected,setupId:placement.setupId,placementId:placement.id,
      sourceGeometry:d.workpieces[0].geometryRef,extractionRuleRef:d.setups[0].planningProfileRef,
      selection:{mode:'Faces',faceUses:extractionModule.extractionDraft(fixture.view).extraction.faces},
      representation:'Solid',workpieceFamily:'GeneralSolid',side:{outwardSign:1},requestedKinds:['Circle','Rectangle','Slot','GeneralContour'],
      draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:'process-editor-test'};
    let task=await invoke('CamCut.ExtractCandidates',request);
    while(task.status==='Running'){await new Promise(r=>setTimeout(r,30));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
    if(task.status!=='Completed')throw Error(task.error);
    const page=await invoke('CamCut.QueryCandidates',{epoch:task.epoch,candidateSet:task.result.candidateSet});
    const {cutPolicyDraft,processDraftDefaults}=await import('/src/apps/five-axis-cam/webpage/processPolicyDraft.mjs');
    const policy=cutPolicyDraft({...processDraftDefaults,cutFeed:'12',cutPower:'700',cutMaterial:'Acceptance steel',cutThickness:'10',cutGas:'N2',
      cutPressure:'2',cutStandOff:'1',cutFocus:'0',cutPierceTime:'0.1',cutPierceRadius:'0.05',cutKerf:'0.12'});
    const prepared=await invoke('CamProcess.Prepare',{commandId:crypto.randomUUID(),expected:d.expected,setupId:placement.setupId,placementId:placement.id,...policy});
    const result=await invoke('CamCut.Create',{commandId:crypto.randomUUID(),expected:d.expected,setupId:placement.setupId,placementId:placement.id,
      candidateSet:task.result.candidateSet,candidateIds:page.candidates.filter(c=>c.confirmationEligibility==='Ready').map(c=>c.candidateId),
      draftDigest:request.draftDigest,decisions:[],process:prepared.process,pose:prepared.pose});
    fixture.view.fiveAxisExtractionOpen=false;fixture.view.fiveAxisExtractionPick=null;
    await fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
    return{result,candidates:page.candidates};
  });
  const candidates=seed.candidates,intentId=seed.result.createdIntentIds[0];
  await verifyStandardHoles({page,rpc,output});
  const createdState=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const savedPolicy=(await rpc({action:'invoke',scope:'scene',method:'CamProcess.Read',payload:{expected:createdState.expected,intentIds:[intentId]}})).items[0];
  await page.locator(`[data-five-intent="${intentId}"]`).click();
  const stressStyle=await page.addStyleTag({content:'.five-pane-heading{padding-bottom:230px!important}.five-tree-footer{padding-bottom:500px}'});
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.compensation'));
  await page.waitForFunction(() => !fixture.view.pending && document.querySelector('[data-five-command="five.process.apply"]'));
  assert.equal(await page.locator('[data-five-field="cutKerf"]').inputValue(),'0.12');
  await page.locator('[data-five-command="five.process.compile"]').click();
  await page.waitForFunction(() => !fixture.view.pending);
  assert.equal(await page.evaluate(() => fixture.view.error),'');
  const compiled = await page.evaluate(async () => {
    const {currentDraft}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');
    return currentDraft(fixture.view).processCompilation?.result;
  });
  assert.ok(compiled,'native compilation returns a displayed result');
  assert.equal(compiled.scope,'LocalCircularBoundary');assert.equal(compiled.compensatedRadiusMm,3.94);
  assert.equal(compiled.motionState,'NotPlanned');assert.equal(compiled.productionQualified,false);
  assert.equal(compiled.eventSequence.length,7);
  assert.equal(await page.getByText('局部圆形边界',{exact:true}).count(),1);
  assert.deepEqual((await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}})).expected,
    (await page.evaluate(() => fixture.view.scene.domain.expected)),'compiling does not change the engineering revision');
  await page.locator('[data-five-field="cutKerf"]').fill('0.15');
  await page.evaluate(() => {
    fixture.holdMethod='CamProcess.Update';fixture.hold=true;
    window.processInput=document.querySelector('[data-five-field="cutKerf"]');window.processInputEvents=0;
    processInput.addEventListener('input',()=>++window.processInputEvents);
    void fixture.entry.handleRibbonCommand(fixture.context,'five.process.apply');
  });
  await page.waitForFunction(() => fixture.held);
  await page.locator('[data-five-field="cutKerf"]').fill('0.17');
  const editScroll=await page.evaluate(() => {
    processInput.focus({preventScroll:true});processInput.setSelectionRange(2,4);
    const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane')];
    nodes.forEach((node,i)=>{node.scrollTop=70+i*10;});return nodes.map(node=>node.scrollTop);
  });
  await page.evaluate(() => fixture.release());
  await page.waitForFunction(() => !fixture.view.pending);
  const editRetained=await page.evaluate(() => ({input:processInput===document.querySelector('[data-five-field="cutKerf"]'),
    focused:document.activeElement===processInput,value:processInput.value,selection:[processInput.selectionStart,processInput.selectionEnd],
    scroll:[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane')].map(node=>node.scrollTop),events:window.processInputEvents}));
  assert.equal(editRetained.input,true);assert.equal(editRetained.focused,true);assert.equal(editRetained.value,'0.17');
  assert.deepEqual(editRetained.selection,[2,4]);assert.deepEqual(editRetained.scroll,editScroll);assert.ok(editRetained.events>0);
  let live=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  let livePolicy=(await rpc({action:'invoke',scope:'scene',method:'CamProcess.Read',payload:{expected:live.expected,intentIds:[intentId]}})).items[0];
  assert.equal(livePolicy.assignment.kerf.widthMm,.15,'new edit remains draft while submitted edit saves');
  assert.equal(await page.getByText('局部圆形边界',{exact:true}).count(),0,'saving invalidates the old compiled result');
  assert.deepEqual(live.intentToolpaths[0].curveRef,createdState.intentToolpaths[0].curveRef,'process updates do not replace exact geometry');
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.adjust'));
  await page.locator('[data-five-field="cutLeadTilt"]').fill('0');
  await page.locator('[data-five-field="cutLeadTilt"]').fill('unfinished');
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.compensation'));
  await page.locator('[data-five-command="five.process.apply"]').click();
  await page.waitForFunction(() => !fixture.view.pending);
  live=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  livePolicy=(await rpc({action:'invoke',scope:'scene',method:'CamProcess.Read',payload:{expected:live.expected,intentIds:[intentId]}})).items[0];
  assert.equal(livePolicy.assignment.kerf.widthMm,.17,'unrelated unfinished lead draft does not block compensation');
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.adjust'));
  assert.equal(await page.locator('[data-five-field="cutLeadTilt"]').inputValue(),'unfinished','saving one category preserves another category draft');
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.clear'));
  for(const category of ['Leads','Overcut','Gap'])await page.locator(`[data-five-command="five.clear.category.${category}"]`).uncheck();
  await page.locator('[data-five-command="five.clear.apply"]').click();
  await page.waitForFunction(() => !fixture.view.pending);
  live=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  livePolicy=(await rpc({action:'invoke',scope:'scene',method:'CamProcess.Read',payload:{expected:live.expected,intentIds:[intentId]}})).items[0];
  assert.equal(livePolicy.assignment.kerf.widthMm,0);
  // Library inheritance remains a domain API, not an extra action invented in
  // the FACAM clear window.
  await rpc({action:'invoke',scope:'scene',method:'CamProcess.RestoreInherited',payload:{commandId:crypto.randomUUID(),expected:live.expected,intentIds:[intentId],categories:['Kerf']}});
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  live=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  livePolicy=(await rpc({action:'invoke',scope:'scene',method:'CamProcess.Read',payload:{expected:live.expected,intentIds:[intentId]}})).items[0];
  assert.equal(livePolicy.assignment.kerf.widthMm,.12,'restores the fixed library value');
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.compensation'));
  await page.locator('[data-five-command="five.process.load"]').click();
  // Only saved values enter compilation, including while another panel has an unfinished draft.
  await page.locator('[data-five-command="five.process.compile"]').click();
  await page.waitForFunction(() => !fixture.view.pending);
  assert.equal(await page.evaluate(() => fixture.view.error),'');
  assert.equal(await page.getByText('局部圆形边界',{exact:true}).count(),1);
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.adjust'));
  await page.locator('[data-five-field="cutLeadTilt"]').fill('5');
  await page.locator('[data-five-command="five.process.apply"]').click();
  await page.waitForFunction(() => !fixture.view.pending);
  live=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  livePolicy=(await rpc({action:'invoke',scope:'scene',method:'CamProcess.Read',payload:{expected:live.expected,intentIds:[intentId]}})).items[0];
  assert.ok(Math.abs(livePolicy.posePolicy.leadAngleRad-5*Math.PI/180)<1e-12);
  assert.deepEqual(errors,[]);
  assert.equal(await page.evaluate(() => fixture.view.error),'');
  await stressStyle.evaluate(node => node.remove());
  await page.evaluate(() => document.querySelectorAll('.cam-context-pane,.cam-info-pane').forEach(node => {node.scrollTop=0;}));
  await page.screenshot({path:resolve(output,'process-editing.png')});
  const cutEditing=await verifyCutEditing({page,rpc,calls,output,intentId});
  await page.evaluate(() => fixture.entry.releaseProject(fixture.context));
  const reopened = await rpc({action:'save-reopen'});
  assert.equal(reopened.intentToolpaths.length,1);
  const restoredCut=(await rpc({action:'invoke',scope:'scene',method:'CamCut.Read',payload:{expected:reopened.expected,intentIds:[intentId]}})).items[0];
  assert.deepEqual(restoredCut,cutEditing.savedDefinition,'cut definition survives source-free save and reopen');
  const restoredPolicy = await rpc({action:'invoke',scope:'scene',method:'CamProcess.Read',payload:{expected:reopened.expected,intentIds:[intentId]}});
  assert.deepEqual(restoredPolicy.items[0],livePolicy,'saved policy dependencies restored after deleting source CAD file');
  await page.evaluate(async () => {
    const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);
    fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);
    fixture.entry.mountProject(fixture.context);
  });
  await page.waitForFunction(() => !fixture.view.pending && fixture.view.scene?.intentToolpaths?.length===1 && fixture.view.viewport?.getDebugState()?.objectCount===1);
  await page.locator('[data-five-intent]').click();
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.adjust'));
  await page.waitForFunction(() => !fixture.view.pending);
  assert.equal(await page.locator('[data-five-field="cutLeadTilt"]').inputValue(),'5');
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.tool.side'));
  await page.waitForFunction(() => !fixture.view.pending);
  assert.equal(await page.locator('[data-five-field="retainedSide"]').inputValue(),cutEditing.savedDefinition.retainedSide);
  assert.equal(await page.evaluate(() => fixture.view.error),'');
  assert.deepEqual(errors,[]);
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({path:resolve(output,'reopened-process.png')});
  await page.evaluate(() => fixture.entry.releaseProject(fixture.context));
  writeFileSync(resolve(output,'report.json'),JSON.stringify({ passed:true, cases:[
    'native fixture preparation from real mouse face selection',
    'process save preserves newer draft and live focus/selection/scroll/listener',
    'unrelated unfinished draft neither blocks nor is overwritten by category update',
    'clear and restore fixed library values differ', 'pose edit converts degrees to radians and preserves geometry',
    'native circle prototype compile and seven events without qualifying motion',
    'saved edits invalidate compiled results while unrelated local drafts stay outside compilation',
    'cut start reverse and retained-side edits plus persistence'],
    coverage:{ browser:'Chromium Edge',scene:'production SDO/Repository/Resources',fileChooser:'injected path',standaloneCef:false,createUI:false,cutEditingUI:true },candidates,editRetained,savedPolicy,compiled,cutEditing,calls },null,2));
  console.log('Five-axis native browser recognition passed');
} catch (error) {
  failure=error;
  if(page){console.error(await page.evaluate(() => ({text:document.body.innerText.slice(0,4500),error:window.fixture?.view.error})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));
  await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}
