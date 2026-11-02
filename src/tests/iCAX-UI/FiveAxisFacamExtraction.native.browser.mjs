import { verifyGenerationRefresh } from './FiveAxisGenerationRefresh.native.browser.mjs';
import {verifyBatchCreation} from './FiveAxisBatchCreation.native.browser.mjs';
import {verifyExtractionScope} from './FiveAxisExtractionScope.native.browser.mjs';
import {verifyStampingCollection} from './FiveAxisStampingCollection.native.browser.mjs';
import {verifyScopedCreation,verifyScopedOrdering,verifySimulationScene} from './FiveAxisScopedCreation.native.browser.mjs';
import { verifyUnifiedIntent } from './FiveAxisUnifiedIntent.native.browser.mjs';
import {verifyPathEditing} from './FiveAxisPathEditing.native.browser.mjs';
import { verifyLargePlan } from './FiveAxisLargePlan.native.browser.mjs';
import { verifyMultiWorkpiece } from './FiveAxisMultiWorkpiece.native.browser.mjs';
import { verifyRapidSafety } from './FiveAxisRapidSafety.native.browser.mjs';
import { verifyCutGroups } from './FiveAxisCutGroups.native.browser.mjs';
import { verifyNestedGroups } from './FiveAxisNestedGroups.native.browser.mjs';
import { verifyCutTree } from './FiveAxisCutTree.native.browser.mjs';
import { verifyCutDeletion } from './FiveAxisCutDeletion.native.browser.mjs';
import { verifyMultiFace } from './FiveAxisMultiFace.native.browser.mjs';
import { verifyCutPreview } from './FiveAxisCutPreview.native.browser.mjs';
import { verifyPathNumbers } from './FiveAxisPathNumbers.native.browser.mjs';
import {verifyPathDrag} from './FiveAxisPathDrag.native.browser.mjs';
import {verifyRapidOriginalVertices} from './FiveAxisRapidOriginalVertices.native.browser.mjs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { verifyLeads } from './FiveAxisLeads.native.browser.mjs';
import { verifyLeadClosure } from './FiveAxisLeadClosure.native.browser.mjs';
import { verifyCutReversing } from './FiveAxisCutReversing.native.browser.mjs';
import { verifyCutClearing } from './FiveAxisCutClearing.native.browser.mjs';
import { verifyGeneration } from './FiveAxisGeneration.native.browser.mjs';
import { verifyCompensation } from './FiveAxisCompensation.native.browser.mjs';
import { verifyOrdering } from './FiveAxisOrdering.native.browser.mjs';
import { verifyOptimizedOrdering } from './FiveAxisOptimizedOrdering.native.browser.mjs';
import { verifyTreeOrdering } from './FiveAxisTreeOrdering.native.browser.mjs';
import { verifyRapidPages } from './FiveAxisRapidPages.native.browser.mjs';
import { verifyPassiveTabs } from './FiveAxisPassiveTabs.native.browser.mjs';
import { verifyToolAxisParameters } from './FiveAxisToolAxis.native.browser.mjs';
import { verifyToolAxisPoints } from './FiveAxisToolAxisPoints.native.browser.mjs';
import { verifyToolAxisInfluence } from './FiveAxisToolAxisInfluence.native.browser.mjs';
import { verifyToolAxisScene } from './FiveAxisToolAxisScene.native.browser.mjs';
import { verifyToolAxisEuler } from './FiveAxisToolAxisEuler.native.browser.mjs';
import { verifyToolAxisDeferred } from './FiveAxisToolAxisDeferred.native.browser.mjs';
import { verifyFixtureAutomaticFaces } from './FixtureAutomaticFaces.native.browser.mjs';
import { verifyFixtureSupportDirection } from './FixtureSupportDirection.native.browser.mjs';
import { verifyPoseSelection } from './FiveAxisPoseSelection.native.browser.mjs';
import { verifyNamesAndToggle } from './FiveAxisNamesAndToggle.native.browser.mjs';
import { verifyRapids } from './FiveAxisRapid.native.browser.mjs';
import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
import { verifyDisplayModes } from './FiveAxisDisplayModes.native.browser.mjs';
import { verifyCutStarting } from './FiveAxisCutStarting.native.browser.mjs';
import { verifyCutSplitting } from './FiveAxisCutSplitting.native.browser.mjs';
import { verifyStandardHoles } from './FiveAxisStandardHoles.native.browser.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('five-axis-facam-extraction');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root,'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, process.env.ICAX_FIVE_AXIS_NATIVE_EXECUTABLE || 'FiveAxisCAMTest.exe'),
  ['--gtest_filter=FiveAxisBridge.DISABLED_Serve', '--gtest_also_run_disabled_tests'],
  { cwd: nativeDirectory, windowsHide: true, env: { ...process.env, ICAX_FIVE_AXIS_BRIDGE_RECOGNITION: '1' }, stdio: ['pipe','pipe','pipe'] });
let sequence = 0, browser, page, failure, nativeExit;
const waiting = new Map(), calls = [], errors = [], nativeLog = [];
const diagnostic=setInterval(()=>{
  console.log('Native stage:',calls.length,calls.at(-1)?.method||calls.at(-1)?.action);
  if(process.env.ICAX_RESOURCE_DIAGNOSTICS){
    writeFileSync(resolve(output,'native-progress.log'),nativeLog.join(''));
    writeFileSync(resolve(output,'resource-progress.json'),JSON.stringify({calls:calls.slice(-40),errors},null,2));
    void page?.evaluate(()=>({text:document.body.innerText,pending:fixture.view.pending,error:fixture.view.error})).then(state=>writeFileSync(resolve(output,'ui-progress.json'),JSON.stringify(state,null,2))).catch(()=>{});
  }
},15000);
const failAll = error => { for (const entry of waiting.values()) { clearTimeout(entry.timer); entry.reject(error); } waiting.clear(); };
processHandle.on('error', failAll);
processHandle.on('exit', code => { nativeExit=code??'by signal';failAll(Error(`Native bridge exited ${nativeExit}`)); });
processHandle.stderr.on('data', bytes => nativeLog.push(String(bytes)));
createInterface({ input: processHandle.stdout }).on('line', line => {
  if (!line.startsWith('__FIVE_RPC__')) { nativeLog.push(line); return; }
  const response = JSON.parse(line.slice(12)), pending = waiting.get(response.id);
  if (!pending) return;
  pending.call.elapsedMs=Date.now()-pending.started;
  waiting.delete(response.id); clearTimeout(pending.timer);
  if (response.ok) pending.resolve(response.result); else {
    pending.call.error=response.error;const error=Error(response.error);
    if(response.sdoStatus!==undefined)error.frame={kind:2,status:response.sdoStatus};
    pending.reject(error);
  }
});
const rpc = message => new Promise((resolve, reject) => {
  if(nativeExit!==undefined){reject(Error(`Native bridge exited ${nativeExit}`));return;}
  const id = ++sequence;
  const call={ id, ...message };calls.push(call);
  const timer = setTimeout(() => { waiting.delete(id); reject(Error(`Native timeout ${message.action}`)); }, 120000);
  waiting.set(id, { resolve, reject, timer,call,started:Date.now() });
  const wire = message.action === 'invoke'
    ? { ...message, payload: undefined, payloadText: message.payloadText ?? serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  const info = await rpc({ action: 'info' });let candidates=[];
  if(process.env.ICAX_EXTRACTION_SCOPE_ONLY||process.env.ICAX_AXIS_REVERSE_ONLY||process.env.ICAX_TOOL_AXIS_POINTS_ONLY||process.env.ICAX_TOOL_AXIS_SCENE_ONLY||process.env.ICAX_AXIS_INFLUENCE_ONLY||process.env.ICAX_TREE_ORDERING_ONLY||process.env.ICAX_DELETE_LATENCY_ONLY)await rpc({action:'prepare-axis-test'});
  info.validateSdoTimeouts=!!process.env.ICAX_LARGE_PLAN_ONLY;
  info.resourceBase64=!!(process.env.ICAX_TOOL_AXIS_DEFERRED_ONLY||process.env.ICAX_FIXTURE_AUTO_ONLY||process.env.ICAX_FIXTURE_DIRECTION_ONLY);
  if(process.env.ICAX_RAPID_PROJECT)Object.assign(info,await rpc({action:'open-project',path:resolve(process.env.ICAX_RAPID_PROJECT)}));
  if(process.env.ICAX_AUTOMOTIVE_STEP)info.sourcePath=resolve(process.env.ICAX_AUTOMOTIVE_STEP);
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ ...(process.env.ICAX_BROWSER_EXECUTABLE ? {executablePath:process.env.ICAX_BROWSER_EXECUTABLE} : {channel:process.env.ICAX_BROWSER_CHANNEL || 'msedge'}), headless: true, args:['--disable-gpu'] });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.setDefaultTimeout(300000);
  page.on('pageerror', error => {errors.push(error.message);console.error('Browser error:',error.stack);});
  await page.exposeFunction('fiveNativeRpc', rpc);
  await page.exposeFunction('fiveNativeInvoke',async message=>{
    try{return {ok:true,result:await rpc(message)};}
    catch(error){return {ok:false,error:error.message,sdoStatus:error.frame?.status};}
  });
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
    const { PDOStore } = await import('/src/iCAX-UI/SDK/PDO/pdoStore.mjs');
    const invokeNative=async message=>{
      const response=await fiveNativeInvoke(message);
      if(response.ok)return response.result;
      const error=Error(response.error);if(response.sdoStatus!==undefined)error.frame={kind:2,status:response.sdoStatus};throw error;
    };
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const view = getProjectView(info.projectId);
    const f = window.fixture = { entry, view, fileSelections: 0, logs: [], hold: false, held: false, release: null };
    const events=new EventTarget();
    const sceneProxy = { sceneId: info.sceneId, state: { sceneId: info.sceneId }, pdoStore:new PDOStore(),
      // This cut fixture declares no collider streams or defragmentation events.
      // Task descriptors below still read the actual native PDO slots.
      subscribe(name,handler){const receive=event=>handler(event.detail);events.addEventListener(String(name),receive);return()=>events.removeEventListener(String(name),receive);},
      pdo: { enabled: true, async withReadDescriptor(descriptor,reader){
        const result=await fiveNativeRpc({action:'pdo-read',slotId:String(descriptor.id),arenaName:descriptor.arenaName});
        return reader(Uint8Array.from(result.bytes).buffer,{dataVersion:result.dataVersion});
      } },
      async invoke(method, payload = {}, options = {}) {
        // This native cut fixture does not host the machine product module or
        // any machine entities. Its unrelated catalogue reads are explicit;
        // all CAM engineering reads, writes and resources remain native.
        if (method === 'MachineDefinition.List') return { definitions: [] };
        if (method === 'Machine.List') return { machines: [] };
        if (method === (f.holdMethod || 'CamScene.ReadState') && f.hold) { f.hold = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false; }
        const result = f.sdoClient ? await f.invokeSdo(method,payload,options) : await invokeNative({ action: 'invoke', scope: 'scene', method, payloadText: serializeVariantText(payload) });
        if (method === 'CamCut.Create' && f.loseCreateResponse) { f.loseCreateResponse = false; throw Error('Injected lost confirmation response'); }
        if (method === f.loseMethod) { f.loseMethod = null; throw Error('Injected lost editing response'); }
        return result;
      }, async getState() { return this.state; } };
    if(info.validateSdoTimeouts){
      const {SDOClient}=await import('/src/iCAX-UI/SDK/SDO/sdoClient.mjs');
      const {makeSDOMethodCodeFromName}=await import('/src/iCAX-UI/SDK/SDO/sdoMethod.mjs');
      const methods=new Map();let receive;f.sdoTimings=[];
      f.sdoClient=new SDOClient({subscribeSDOFrames(channel,handler){receive=handler;return()=>{};},async postSDOFrame(frame){
        const method=methods.get(String(frame.methodCode)),started=performance.now();
        try{const result=await invokeNative({action:'invoke',scope:'scene',method,payloadText:frame.payloadText});
          f.sdoTimings.push({method,elapsedMs:performance.now()-started});receive({...frame,kind:2,status:0,payloadText:serializeVariantText(result)});
        }catch(error){receive({...frame,kind:2,status:1,payloadText:serializeVariantText({error:error.message})});}
      }});
      f.invokeSdo=(method,payload,options)=>{methods.set(makeSDOMethodCodeFromName(method),method);return f.sdoClient.invoke(info.sceneId,method,payload,options);};
    }
    const resource = async (url, options, head) => {
      const version = new Headers(options?.headers).get('ICAX-Resource-Version') ?? String(options?.version ?? '');
      const result = await fiveNativeRpc({ action: 'resource', url, version, head,...(info.resourceBase64?{encoding:'base64'}:{}) });
      const bytes=result.base64!==undefined?Uint8Array.from(atob(result.base64),c=>c.charCodeAt(0)):Uint8Array.from(result.bytes);
      if(!head&&String.fromCharCode(...bytes.slice(4,8))==='ICPS'){
        (f.pathResourceReads??=[]).push({url,version:result.version,bytes:bytes.length});
        if(f.holdPathResource){f.holdPathResource=false;f.heldPathResource=true;await new Promise(resolve=>{f.releasePathResource=resolve;});f.heldPathResource=false;}
      }
      return new Response(head ? null : bytes, { headers: { 'Content-Type': 'application/vnd.icax.flatbuffer', 'ICAX-Resource-Version': result.version } });
    };
    sceneProxy.resources = { get: (url, options) => resource(url, options, false), head: (url, options) => resource(url, options, true) };
    sceneProxy.views = new ViewClient(sceneProxy);
    const context = f.context = { mount: document.querySelector('#app'), sceneProxy,
      project: { projectId: info.projectId, projectName: '汽车冲压件 · 刀路验收' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog() { ++f.fileSelections; return info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) { return invokeNative({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain, undefined, { timeout: process.env.ICAX_TOOL_AXIS_DEFERRED_ONLY||process.env.ICAX_INSTANCE_VISIBILITY_ONLY||process.env.ICAX_SCOPED_ORDER_ONLY?120000:30000 });
  if(process.env.ICAX_EXTRACTION_SCOPE_ONLY){
    await verifyExtractionScope({page,rpc,calls,output});
  }else if(process.env.ICAX_STAMPING_COLLECTION_ONLY){
    await verifyStampingCollection({page,rpc,output});
  }else if(process.env.ICAX_BATCH_CREATION_ONLY){
    await verifyBatchCreation({page,rpc,calls,output,sourcePath:info.sourcePath});
  }else if(process.env.ICAX_FIXTURE_DIRECTION_ONLY){
    await verifyFixtureSupportDirection({page,rpc,calls,output,sourcePath:info.sourcePath});
  }else if(process.env.ICAX_FIXTURE_AUTO_ONLY){
    await verifyFixtureAutomaticFaces({page,rpc,calls,output,sourcePath:info.sourcePath});
  }else if(process.env.ICAX_TOOL_AXIS_DEFERRED_ONLY){
    await verifyToolAxisDeferred({page,rpc,calls,output});
  }else if(process.env.ICAX_SCOPED_ORDER_ONLY){
    await verifyScopedOrdering({page,rpc,calls,output});
  }else if(process.env.ICAX_INSTANCE_VISIBILITY_ONLY){
    await verifySimulationScene({page,calls,output});
  }else if(process.env.ICAX_SCOPED_CREATE_ONLY){
    await verifyScopedCreation({page,rpc,calls,output,sourcePath:info.sourcePath});
  }else if(process.env.ICAX_RAPID_ORIGINAL_VERTICES_ONLY){
    await verifyRapidOriginalVertices({page,rpc,output});
  }else if(process.env.ICAX_PATH_DRAG_ONLY){
    await verifyPathDrag({page,rpc,output});
  }else if(process.env.ICAX_PATH_NUMBERS_ONLY){
    await verifyPathNumbers({page,rpc,output});
  }else if(process.env.ICAX_PATH_EDIT_ONLY){
    await verifyPathEditing({page,rpc,calls,output});
  }else if(process.env.ICAX_DELETE_LATENCY_ONLY){
    await verifyCutDeletion({page,rpc,calls,output});
  }else if(process.env.ICAX_LARGE_PLAN_ONLY){
    await verifyLargePlan({page,rpc,calls,output});
  }else if(process.env.ICAX_GENERATION_REFRESH_ONLY){
    await verifyGenerationRefresh({page,rpc,calls,output});
  }else if(process.env.ICAX_TOOL_AXIS_EULER_ONLY){
    await verifyToolAxisEuler({page,rpc,calls,output});
  }else if(process.env.ICAX_POSE_SELECTION_ONLY){
    await verifyPoseSelection({page,rpc,calls,output});
  }else if(process.env.ICAX_TOOL_AXIS_SCENE_ONLY||process.env.ICAX_TOOL_AXIS_SCENE_LAYOUT_ONLY){
    await verifyToolAxisScene({page,rpc,calls,output});
  }else if(process.env.ICAX_AXIS_INFLUENCE_ONLY){
    await verifyToolAxisInfluence({page,rpc,calls,output});
  }else if(process.env.ICAX_TOOL_AXIS_POINTS_ONLY){
    await verifyToolAxisPoints({page,rpc,calls,output});
  }else if(process.env.ICAX_TOOL_AXIS_ONLY||process.env.ICAX_AXIS_REVERSE_ONLY){
    await verifyToolAxisParameters({page,rpc,calls,output});
  }else if(process.env.ICAX_NAMES_TOGGLE_ONLY){
    await verifyNamesAndToggle({page,rpc,calls,output});
  }else if(process.env.ICAX_PASSIVE_TABS_ONLY){
    await verifyPassiveTabs({page,rpc,calls,output});
  }else if(process.env.ICAX_RAPID_PAGES_ONLY){
    await verifyRapidPages({page,rpc,calls,output});
  }else if(process.env.ICAX_TREE_ORDERING_ONLY){
    await verifyTreeOrdering({page,rpc,calls,output});
  }else if(process.env.ICAX_ORDER_OPTIMIZATION_ONLY){
    await verifyOptimizedOrdering({page,rpc,calls,output,sourcePath:info.sourcePath});
  }else if(process.env.ICAX_RAPID_PROJECT&&process.env.ICAX_RAPID_ONLY){
    await verifyRapids({page,rpc,calls,output});
  }else{
  if(!process.env.ICAX_RAPID_PROJECT){
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

  await page.evaluate(async () => {
    window.extractionModule=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
    await fixture.context.actions.selectRibbonTab('toolpath');
  });
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract'));
  assert.equal(await page.getByRole('dialog',{name:'添加刀路'}).count(),1);
  assert.equal(await page.locator('[data-five-field="cutFeed"]').count(),0);
  if(process.env.ICAX_BRACKET_FACE||process.env.ICAX_UNIFIED_INTENT_ONLY){
    const sources=await rpc({action:'invoke',scope:'scene',method:'CamCut.QuerySources',payload:{expected:state.expected,setupId:state.activeSetupId,placementId:state.placements[0].id}});
    const inventory=process.env.ICAX_BRACKET_FACE?null:await rpc({action:'face-inventory'});
    const faceId=process.env.ICAX_BRACKET_FACE||inventory.faces.toSorted((a,b)=>Number(b.wireCount)-Number(a.wireCount))[0].id;
    const face=sources.faceUses.find(f=>f.localId===faceId);assert.ok(face);
    await page.evaluate(face=>{const state=extractionModule.extractionDraft(fixture.view).extraction;state.mode='Faces';state.faces=[face];extractionModule.extractionChanged(fixture.view);},face);
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.run'));
  }else {
    assert.equal(await page.locator('[data-five-command="five.extraction.automatic"]').count(),1,
      'This extraction regression requires a surface model (ICAX_AUTOMOTIVE_STEP) or an explicit solid face (ICAX_BRACKET_FACE); the default bridge solid has no automatic extraction button.');
    await page.locator('[data-five-command="five.extraction.automatic"]').click();
  }
  await page.waitForFunction(()=>!fixture.view.pending);
  const status=await page.evaluate(()=>({error:fixture.view.error,result:extractionModule.extractionResult(fixture.view)}));
  assert.equal(status.error,'');assert.ok(status.result);
  candidates=status.result.candidates;
  writeFileSync(resolve(output,'extracted-candidates.json'),JSON.stringify(candidates,null,2));
  assert.ok(candidates.length>1);
  assert.ok(candidates.some(c=>c.geometryKind==='GeneralContour'),'automotive spatial spline outer trim retained');
  assert.ok(candidates.every(c=>c.boundary.previewPoints.length>1));
  assert.equal((await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}})).intentToolpaths.length,0);
  await page.evaluate(() => fixture.view.viewport.fitView());
  writeFileSync(resolve(output,'layout.json'),JSON.stringify(await page.evaluate(()=>[...document.querySelectorAll('.cam-workbench,.cam-context-pane,.cam-info-pane,.cam-viewer,.cam-render-viewport,canvas')].map(n=>({class:n.className,rect:n.getBoundingClientRect().toJSON(),style:n.getAttribute('style'),display:getComputedStyle(n).display,position:getComputedStyle(n).position,grid:getComputedStyle(n).gridArea,attrs:[...n.attributes].filter(a=>a.name.startsWith('data')).map(a=>[a.name,a.value])}))),null,2));
  await page.screenshot({path:resolve(output,'automotive-extracted.png')});
  const selectedCandidates=await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.selected);
  await page.locator('[data-five-command="five.extraction.confirm"]').click();
  await page.waitForFunction(()=>!fixture.view.pending);
  const created=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(created.intentToolpaths.length,selectedCandidates.length);
  assert.equal(await page.locator('.five-cut-tree [data-five-intent]').count(),selectedCandidates.length);
  assert.equal(await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.filter(c=>c.userData.cuttingLineId).length),selectedCandidates.length);
  if(process.env.ICAX_BRACKET_FACE){
    assert.equal(created.intentToolpaths.filter(c=>c.machiningKind==='Mark').length,21);
    assert.equal(created.intentToolpaths.filter(c=>c.machiningKind==='Cut').length,6);
    assert.ok(created.intentToolpaths.filter(c=>c.machiningKind==='Mark').every(c=>c.cutType==='Contour'&&!c.canBeStandardHole));
    assert.equal(await page.locator('.five-cut-tree .is-marking').count(),21);
    assert.equal(await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.filter(c=>c.isLine&&c.material.isLineDashedMaterial&&c.material.color.getHex()===0x55b9e7).length),created.intentToolpaths.filter(c=>c.machiningKind==='Mark'&&c.id!==created.intentToolpaths[0].id).length);
    await page.screenshot({path:resolve(output,'stamped-bracket-cut-mark.png')});
  }
  if(process.env.ICAX_UNIFIED_INTENT_ONLY){await verifyUnifiedIntent({page,rpc,calls,output});}
  else if(process.env.ICAX_LEADS_ONLY){await verifyLeads({page,rpc,calls,output});}
  else if(process.env.ICAX_MULTI_WORKPIECE_ONLY){await verifyMultiWorkpiece({page,rpc,calls,output,sourcePath:info.sourcePath});}
  else if(process.env.ICAX_COMPENSATION_ONLY){await verifyCompensation({page,rpc,calls,output});}
  else if(process.env.ICAX_RAPID_SAFETY_ONLY){await verifyRapidSafety({page,rpc,calls,output});}
  else if(process.env.ICAX_CUT_GROUPS_ONLY){await (process.env.ICAX_CUT_GROUPS_ONLY==='nested'?verifyNestedGroups:verifyCutGroups)({page,rpc,calls,output});}
  else if(process.env.ICAX_PREVIEW_ONLY){await verifyCutPreview({page,rpc,calls,output});}
  else if(process.env.ICAX_MULTI_FACE_ONLY){await verifyMultiFace({page,rpc,calls,output});}
  else if(process.env.ICAX_CUT_TREE_ONLY){await verifyCutTree({page,rpc,calls,output});}
  else {
  assert.ok(created.intentToolpaths.every(c=>c.retainedSide==='Unspecified'),'geometry extraction does not invent machining retained side');
  assert.equal((await rpc({action:'undo'})).state.intentToolpaths.length,0);
  assert.equal((await rpc({action:'redo'})).state.intentToolpaths.length,candidates.length);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  await page.waitForFunction(()=>!fixture.view.pending);
  await page.screenshot({path:resolve(output,'automotive-confirmed.png')});
  if(process.env.ICAX_AUTOMOTIVE_PROJECT)await rpc({action:'export-project',path:resolve(process.env.ICAX_AUTOMOTIVE_PROJECT)});
  const reopened=await rpc({action:'save-reopen'});
  assert.equal(reopened.intentToolpaths.length,candidates.length);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  await page.evaluate(async()=>{
    const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);
    fixture.entry.mountProject(fixture.context);
  });
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1&&fixture.view.fiveCutPreview?.group.children.length>1);
  if(process.env.ICAX_AUTOMOTIVE_STEP) await verifyDisplayModes({page,rpc,calls,output});
  const assertWorkpieceVisible = async step => {
    const state = await page.evaluate(() => ({
      area: fixture.view.activeAreaId,
      visibleIds: [...(fixture.view.viewport.visibleEntityIds || [])],
      objects: [...fixture.view.viewport.sceneObjects.values()].map(o => ({ id: o.userData.objectId, visible: o.visible,
        materialVisible: o.material?.visible, edges: o.userData.meshEdges?.visible })),
      areas: Object.fromEntries(Object.entries(fixture.view.areas).map(([id, a]) => [id, { revision: a.appliedViewRevision, snapshot: !!a.viewContent?.snapshot }])),
    }));
    assert.ok(state.objects.length && state.objects.every(o => o.visible), `${step}: ${JSON.stringify(state)}`);
  };
  await assertWorkpieceVisible('display switches');
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract'));
  await page.locator('[data-five-command="five.extraction.automatic"]').click();
  await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.selected.length),0,'repeated extraction must not duplicate cuts');
  await page.locator('[data-five-command="five.extraction.close"]').first().click();
  await assertWorkpieceVisible('duplicate extraction');
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.locator('[data-five-intent]').first().click();
  await page.locator('[data-five-intent]').nth(1).click({modifiers:['Control']});
  assert.equal(await page.locator('[data-five-intent][aria-pressed="true"]').count(),2);
  await page.locator('[data-five-intent]').last().click({modifiers:['Shift']});
  assert.equal(await page.locator('[data-five-intent][aria-pressed="true"]').count(),5);
  await page.locator('[data-five-intent]').last().click({modifiers:['Shift']});
  assert.equal(await page.locator('[data-five-intent][aria-pressed="true"]').count(),1);
  await assertWorkpieceVisible('tree selection');
  const linePoint=await page.evaluate(()=>{
    const v=fixture.view.viewport,box=v.renderer.domElement.getBoundingClientRect();
    for(const c of fixture.view.scene.intentToolpaths.filter(c=>c.previewPoints.length<200)){
      const object=v.sceneObjects.get(c.placementId);
      for(const p of c.previewPoints){
        const q=object.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);
        const x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
        if(x>650&&x<1100&&y>300&&y<650)return{id:c.id,x,y};
      }
    }
    throw Error('No visible cutting-line point');
  });
  await page.mouse.click(linePoint.x,linePoint.y);
  assert.equal(await page.locator(`[data-five-intent="${linePoint.id}"]`).getAttribute('aria-pressed'),'true');
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.manual-extract'));
  await assertWorkpieceVisible('manual extraction opened');
  const pickPoint=await page.evaluate(()=>{
    const v=fixture.view.viewport,object=[...v.sceneObjects.values()][0];let mesh;
    object.traverse(n=>{if(n.isMesh&&!mesh)mesh=n;});
    const p=mesh.geometry.getAttribute('position'),box=v.renderer.domElement.getBoundingClientRect();
    for(let i=0;i<p.count;i+=3){
      const world=mesh.localToWorld(v.camera.position.clone().set(p.getX(i),p.getY(i),p.getZ(i))).project(v.camera);
      const x=box.x+(world.x+1)*box.width/2,y=box.y+(1-world.y)*box.height/2;
      if(x>650&&x<1100&&y>350&&y<600)return{x,y};
    }
    throw Error('No visible automotive test point');
  });
  await page.mouse.click(pickPoint.x,pickPoint.y);
  await page.waitForFunction(()=>!fixture.view.pending&&extractionModule.extractionDraft(fixture.view).extraction.faces.length===1);
  assert.ok(calls.some(c=>c.method==='CamCut.ResolveFace'));
  assert.ok(await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.some(c=>c.userData.selectedMachiningFaces)),'picked face is visibly highlighted');
  await page.screenshot({path:resolve(output,'automotive-manual-face.png')});
  await page.mouse.click(pickPoint.x,pickPoint.y);
  await page.waitForFunction(()=>!fixture.view.pending&&extractionModule.extractionDraft(fixture.view).extraction.faces.length===0);
  await page.mouse.click(pickPoint.x,pickPoint.y);
  await page.waitForFunction(()=>!fixture.view.pending&&extractionModule.extractionDraft(fixture.view).extraction.faces.length===1);
  await page.locator('[data-five-command="five.extraction.confirm"]').click();
  await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.ok((await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}})).intentToolpaths.length>candidates.length,'manual selected patch confirms without process fields');
  await rpc({action:'undo'});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  assert.deepEqual(errors,[]);
  await page.screenshot({path:resolve(output,'automotive-reopened.png')});
  await verifyStandardHoles({page,rpc,output,automotive:true});
  await verifyCutStarting({page,rpc,calls,output,automotive:true});
  await verifyCutSplitting({page,rpc,output});
  if(!process.env.ICAX_GENERATION_FOCUS){
    await verifyLeads({page,rpc,calls,output});
    await verifyLeadClosure({page,rpc,calls,output});
    await verifyCutReversing({page,rpc,calls,output});
    await verifyCutClearing({page,rpc,calls,output});
  }
  }
  if(!process.env.ICAX_UNIFIED_INTENT_ONLY&&!process.env.ICAX_LEADS_ONLY&&!process.env.ICAX_MULTI_WORKPIECE_ONLY&&!process.env.ICAX_COMPENSATION_ONLY&&!process.env.ICAX_RAPID_SAFETY_ONLY&&!process.env.ICAX_CUT_GROUPS_ONLY&&!process.env.ICAX_PREVIEW_ONLY&&!process.env.ICAX_RAPID_ONLY&&!process.env.ICAX_CUT_TREE_ONLY&&!process.env.ICAX_MULTI_FACE_ONLY){
  if(!process.env.ICAX_PATH_EDIT_ONLY){await verifyGeneration({page,rpc,calls,output});await verifyOrdering({page,rpc,calls,output});}
  }
  if(!process.env.ICAX_UNIFIED_INTENT_ONLY&&!process.env.ICAX_MULTI_WORKPIECE_ONLY&&!process.env.ICAX_COMPENSATION_ONLY&&!process.env.ICAX_RAPID_SAFETY_ONLY&&!process.env.ICAX_CUT_GROUPS_ONLY&&!process.env.ICAX_PREVIEW_ONLY&&!process.env.ICAX_CUT_TREE_ONLY&&!process.env.ICAX_MULTI_FACE_ONLY)await verifyRapids({page,rpc,calls,output});
  }
  }
  assert.deepEqual(errors,[]);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases:process.env.ICAX_STAMPING_COLLECTION_ONLY?['four original stamping STEP solids: real native import, rendering and project save/reopen; see native-import-audit.json']:process.env.ICAX_FIXTURE_DIRECTION_ONLY?['manual and real mouse support-axis picking, oblique automatic support geometry; see fixture-support-direction.json']:process.env.ICAX_FIXTURE_AUTO_ONLY?['automatic editable support-face selection and explicit native plate generation; see fixture-automatic-faces.json']:process.env.ICAX_TOOL_AXIS_DEFERRED_ONLY?['real automotive project: consecutive deferred pose editing, interaction preservation and explicit simulation preparation; see deferred-pose-browser.json']:process.env.ICAX_SCOPED_CREATE_ONLY?['selected faces create and display scoped cutting paths; see scoped-creation-report.json']:process.env.ICAX_SCOPED_ORDER_ONLY?['31 actual cutting paths retain geometry through asynchronous ordering; see scoped-order-report.json']:process.env.ICAX_INSTANCE_VISIBILITY_ONLY?['instance scene excludes CAM setup geometry; see simulation-scene-report.json']:process.env.ICAX_UNIFIED_INTENT_ONLY?['real-model unified edit, native history, interaction preservation and save/reopen; see unified-intent-report.json']:process.env.ICAX_TREE_ORDERING_ONLY?['native manual and automatic tree ordering, nested groups, locked boundaries, interaction preservation and save/reopen; see tree-ordering-report.json']:process.env.ICAX_LARGE_PLAN_ONLY?['real egg 480-path generation and 479 rapid connections; see large-plan-report.json']:process.env.ICAX_TOOL_AXIS_EULER_ONLY?['Euler pose editing and camera navigation; see pose-euler-navigation-report.json']:process.env.ICAX_POSE_SELECTION_ONLY?['navigation and readonly pose selection; see pose-selection-report.json']:process.env.ICAX_TOOL_AXIS_SCENE_LAYOUT_ONLY?['scene legend layout at three sizes; see scene-layout-report.json']:process.env.ICAX_TOOL_AXIS_SCENE_ONLY?['all-path normals and scene keypoint rotation; see tool-axis-scene-report.json']:process.env.ICAX_TOOL_AXIS_VISUAL_ONLY?['native local IJK panel visual layout and save; see visual-report.json']:process.env.ICAX_TOOL_AXIS_POINTS_ONLY?['native local IJK keypoints, interpolation and regeneration; see tool-axis-points-report.json']:process.env.ICAX_TOOL_AXIS_ONLY?['native tool-axis IJK editing and regeneration; see tool-axis-report.json']:process.env.ICAX_NAMES_TOGGLE_ONLY?['persistent cutting names, immutable identifiers and enable-switch latency; see names-toggle-report.json']:process.env.ICAX_ORDER_OPTIMIZATION_ONLY?['actual-model machining-order optimization; see comparison.json and optimization-acceptance.json']:process.env.ICAX_MULTI_WORKPIECE_ONLY?['multiple workpiece import, selection, positioning, extraction, generation, persistence']:process.env.ICAX_COMPENSATION_ONLY?['native compensation editing and generation','exact compensated path resource','reversal preserves compensation side']:process.env.ICAX_CUT_GROUPS_ONLY?['native groups, enable, lock, selection, persistence, ordering']:process.env.ICAX_PREVIEW_ONLY?['left-tree new action and extraction dialog','small screen-space directions across zoom, projection and reversal']:process.env.ICAX_MULTI_FACE_ONLY?['multi-face classification and confirmation','independent opposite-side paths in UI and native creation','user deletion and undo preserve the other side','repeat extraction feedback and persistence']:process.env.ICAX_CUT_TREE_ONLY?['native face extraction distinguishes marking and cutting','tree multiselect delete enable disable ordering and persistence']:process.env.ICAX_RAPID_PROJECT?['opened saved automotive project',...(process.env.ICAX_RAPID_ONLY?[]:['generation regression','ordering regression']),'rapid planning and editing regression']:['FACAM compact auto/manual dialog','whole automotive surface automatic extraction','spatial spline boundary preserved','preview only before confirm','native cut creation and viewport/tree','one transaction undo and redo','embedded save and reopen','duplicate suppression','Ctrl and Shift tree selection plus viewport line selection','actual mouse face toggle and visible highlight','manual face confirm and undo'],coverage:{native:'production SDO, Repository, Resources',browser:'Edge',fileChooser:'injected path',standaloneCef:false},candidates:candidates.map(c=>({kind:c.geometryKind,spans:c.boundary.sourceSpans.length,points:c.boundary.previewPoints.length,issues:c.issues})),calls:calls.filter(c=>c.action!=='resource')},null,2));
  console.log(process.env.ICAX_TOOL_AXIS_DEFERRED_ONLY?'Deferred pose real-project workflow passed':process.env.ICAX_UNIFIED_INTENT_ONLY?'Unified real-model edit, interaction and persistence workflow passed':process.env.ICAX_LARGE_PLAN_ONLY?'Real egg 480-path generation workflow passed':process.env.ICAX_TOOL_AXIS_EULER_ONLY?'Euler pose editing and camera navigation passed':process.env.ICAX_POSE_SELECTION_ONLY?'Readonly pose-selection workflow passed':process.env.ICAX_TOOL_AXIS_SCENE_LAYOUT_ONLY?'Scene legend layout workflow passed':process.env.ICAX_TOOL_AXIS_SCENE_ONLY?'All-path normals and scene rotation workflow passed':process.env.ICAX_TOOL_AXIS_POINTS_ONLY?'Local IJK keypoint workflow passed':process.env.ICAX_TOOL_AXIS_ONLY?'Whole-path IJK workflow passed':process.env.ICAX_NAMES_TOGGLE_ONLY?'Cut names and enable switches passed':process.env.ICAX_ORDER_OPTIMIZATION_ONLY?'Actual-model machining order workflow passed':process.env.ICAX_MULTI_WORKPIECE_ONLY?'Multiple workpiece browser workflow passed':process.env.ICAX_COMPENSATION_ONLY?'Compensated toolpath browser workflow passed':process.env.ICAX_CUT_GROUPS_ONLY?'Cut groups, enable/lock and ordering workflow passed':process.env.ICAX_PREVIEW_ONLY?'Cut tree entry and direction preview passed':process.env.ICAX_MULTI_FACE_ONLY?'Multi-face and opposite-side extraction workflow passed':process.env.ICAX_CUT_TREE_ONLY?'Cut tree and classification workflow passed':process.env.ICAX_RAPID_PROJECT?'Automotive rapid workflow passed':'FACAM automotive extraction and rapid workflow passed');
} catch (error) {
  failure=error;
  writeFileSync(resolve(output,'failed-calls.json'),JSON.stringify(calls.filter(c=>c.action!=='resource'),null,2));
  if(page){console.error(await page.evaluate(() => ({text:document.body.innerText.slice(0,4500),error:window.fixture?.view.error,leads:window.fixture?.view.fiveLeads})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  clearInterval(diagnostic);
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));
  await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}
