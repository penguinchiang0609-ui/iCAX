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
const output = browserReportDirectory('five-axis-placement-browser');
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
        if (['CamPlacement.SetTransform','CamWorkpiece.Scale','CamPlacement.ApplyRegistration','CamWorkpiece.Remove','CamWorkpiece.SetLocked','CamWorkpiece.SetEnabled'].includes(method) && f.dropApply) { f.dropApply = false; throw Error('Injected lost apply receipt'); }
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

  const cases = [];
  const invoke = (method,payload={}) => rpc({action:'invoke',scope:'scene',method,payload});
  const command = id => page.evaluate(id => fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle = () => page.waitForFunction(() => !fixture.view.pending);
  const refresh = async () => { await command('five.refresh');await idle(); };
  const {pick,numeric}=gizmoDriver(page);
  const confirm=async()=>{await numeric().press('Enter');await idle();};
  const near = (a,b) => { assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-6,`${a} != ${b}`)); };
  const ribbon = () => page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='workpiece').groups);
  let groups=await ribbon();assert.deepEqual(groups.map(g=>g.title),['工件','几何','工件调整']);
  assert.deepEqual(groups[2].commands.map(c=>[c.title,c.disabled]),[['摆放',true],['自身坐标系重置',true]]);
  assert.ok(groups.flatMap(g=>g.commands).every(c=>!['选择','平移','旋转','缩放'].includes(c.title)));
  assert.equal(await page.locator('.five-placement-hud').count(),0);
  assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),false);
  await selectWorkpieceFile(page, process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL || info.sourcePath);
  await page.waitForFunction(() => !fixture.view.pending && fixture.view.viewport?.getDebugState()?.objectCount===1);
  groups=await ribbon();assert.ok(groups.flatMap(g=>g.commands).every(c=>!['选择','平移','旋转','缩放'].includes(c.title)));
  assert.deepEqual(groups[2].commands.map(c=>[c.title,c.disabled]),[['摆放',false],['自身坐标系重置',false]]);
  await page.locator('.five-placement-tools').waitFor();
  assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.mode),'translate');
  await command('five.tool.select');
  cases.push('Ribbon separates Placement from Reset object frame; import activates Move and a three-mode nearby toolbar without an extra selection click');

  const read = () => invoke('CamScene.ReadState');
  const initial=await read(),placementId=initial.placements[0].id;
  const sourceBounds = process.env.ICAX_FIVE_AXIS_SOURCE_BOUNDS ? JSON.parse(process.env.ICAX_FIVE_AXIS_SOURCE_BOUNDS)
    : process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL ? null : { min:[0,0,0], max:[40,60,20] };
  const worldFrame = await verifyCenteredWorkbench({page, output, placement:initial.placements[0], sourceBounds});
  cases.push('native import centers displayed geometry on world XY; grid and XYZ references remain fixed through fitting and projection changes');
  const base=[0,-1,0,10, 1,0,0,20, 0,0,1,30, 0,0,0,1];
  await invoke('CamPlacement.SetTransform',{commandId:crypto.randomUUID(),expected:initial.expected,placementId,setupId:initial.activeSetupId,transform:base});
  await refresh();
  await page.evaluate(()=>fixture.view.viewport.fitView());
  const rendered = () => page.evaluate(id=>fixture.view.viewport.sceneObjects.get(id).matrix.clone().transpose().toArray(),placementId);
  const waitRendered = async matrix => {
    await page.waitForFunction(({id,m})=>fixture.view.viewport.sceneObjects.get(id)?.matrix.clone().transpose().toArray().every((v,i)=>Math.abs(v-m[i])<1e-6),{id:placementId,m:matrix});
  };
  await waitRendered(base);

  if(process.env.ICAX_GEOMETRY_ONLY){
    const {verifyWorkpieceGeometry}=await import('./FiveAxisWorkpieceGeometry.native.browser.mjs');
    cases.push(...await verifyWorkpieceGeometry({page,read,rpc,refresh,output,sourcePath:info.sourcePath}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production SDO, View resources and repository',browser:'Edge WebGL and real pointer/input events',standaloneCef:false},calls},null,2));
    console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_WORKPIECE_ENABLE_ONLY){
    const {verifyWorkpieceEnable}=await import('./FiveAxisWorkpieceEnable.native.browser.mjs');
    cases.push(...await verifyWorkpieceEnable({page,read,rpc,refresh,output}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production SDO, View resources, transactions and repository',browser:'Edge WebGL and real pointer/input events',standaloneCef:false},calls},null,2));
    console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_WORKPIECE_REMOVAL_ONLY){
    const {verifyWorkpieceLock}=await import('./FiveAxisWorkpieceLock.native.browser.mjs');
    cases.push(...await verifyWorkpieceLock({page,read,rpc,refresh,output}));
    const {verifyWorkpieceRemoval}=await import('./FiveAxisWorkpieceRemoval.native.browser.mjs');
    cases.push(...await verifyWorkpieceRemoval({page,read,rpc,refresh,output,secondPath:process.env.ICAX_FIVE_AXIS_SECOND_MODEL||info.sourcePath}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production Scene/SDO, atomic workpiece deletion, history and project persistence',browser:'Edge actual pointer and production UI',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_ALIGNMENT_ONLY){
    const verify=(await import('./FiveAxisFrameAlignment.native.browser.mjs')).verifyFrameAlignment;
    cases.push(...await verify({page,command,read,rpc,invoke,refresh,base,output}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production Scene/SDO, exact BRep references and placement persistence',browser:'Edge actual pointer and production UI',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_VIEW_CUBE_ONLY){
    const {verifyCompactViewCube}=await import('./FiveAxisCompactViewCube.native.browser.mjs');
    cases.push(...await verifyCompactViewCube({page,read,output}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production scene SDO and View resources',browser:'Edge real pointer events',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_CONTEXT_TRANSFORM_ONLY){
    const {verifyContextTransform}=await import('./FiveAxisContextTransform.native.browser.mjs');
    cases.push(...await verifyContextTransform({page,command,read,rpc,refresh,base,output}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production scene SDO and View resources',browser:'Edge real pointer events',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_SCREEN_ROTATION_ONLY){
    const {verifyScreenRotation}=await import('./FiveAxisScreenRotation.native.browser.mjs');
    cases.push(...await verifyScreenRotation({page,command,read,rpc,refresh,base,output}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production scene SDO and View resources',browser:'Edge real pointer events in both projections',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_PLANE_TRANSLATION_ONLY){
    const {verifyPlaneTranslation}=await import('./FiveAxisPlaneTranslation.native.browser.mjs');
    cases.push(...await verifyPlaneTranslation({page,command,read,rpc,refresh,base,output}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production scene SDO and View resources',browser:'Edge real pointer events in both projections',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_CAMERA_NAVIGATION_ONLY){
    cases.push(...await verifyCameraNavigation({page,command,read,rpc,refresh}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production scene SDO and View resources',browser:'Edge real pointer events in both projections',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_TRANSFORM_PANEL_ONLY){
    const {verifyTransformPanel}=await import('./FiveAxisTransformPanel.native.browser.mjs');
    cases.push(...await verifyTransformPanel({page,command,read,refresh,rpc,base,output}));assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production scene SDO and View resources',browser:'Edge real pointer events in both projections',standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }else if(process.env.ICAX_PLACEMENT_EXIT_ONLY){
    const {verifyPlacementExit}=await import('./FiveAxisPlacementExit.native.browser.mjs');
    cases.push(...await verifyPlacementExit({page,command,read,rpc,refresh,output}));
    cases.push(...await verifyCameraNavigation({page,command,read,rpc,refresh}));
    assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production scene SDO and View resources',browser:'Edge real pointer events in both projections',standaloneCef:false},calls},null,2));
    console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  } else {
  cases.push(...await verifyCameraNavigation({page,command,read,rpc,refresh}));
  cases.push(...await verifyPlacementGizmo({page,command,read,refresh,rpc,waitRendered,base,output}));
  cases.push(...await verifyScaleGizmo({page,command,read,refresh,rpc,base,output}));
  assert.deepEqual(await captureWorldFrame(page), worldFrame, 'world workbench stays fixed through world/local XYZ translation, rotation, and undo');

  // Actual mouse orbit crosses both poles, including while the fixture sidebar is open.
  await command('five.tool.fixture');
  const camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());assert.equal(camera.orbitConstrained,false);
  for (const [phi,dy] of [[0.04,80],[Math.PI-.04,-80]]) {
    await page.evaluate(({camera,phi})=>fixture.view.viewport.setCameraState({...camera,phi}),{camera,phi});
    const r=await page.locator('.icax-three-viewport canvas').first().boundingBox();
    await page.mouse.move(r.x+r.width*.65,r.y+r.height*.5);await page.mouse.down({button:'middle'});
    await page.mouse.move(r.x+r.width*.65,r.y+r.height*.5+dy,{steps:12});await page.mouse.up({button:'middle'});
    const after=await page.evaluate(()=>({state:fixture.view.viewport.getCameraState(),matrix:fixture.view.viewport.camera.matrixWorld.toArray()}));
    assert.ok(dy>0?after.state.phi<0:after.state.phi>Math.PI);assert.ok(after.matrix.every(Number.isFinite));
  }
  await page.evaluate(camera=>fixture.view.viewport.setCameraState(camera),camera);
  near((await read()).placements[0].transform,base);
  cases.push('actual middle-drag crosses top and bottom poles without changing placement');

  await command('five.tool.translate');await pick('X');await numeric().fill('12');
  const moved=[...base];moved[3]+=12;
  await waitRendered(moved);near((await read()).placements[0].transform,base);
  await page.evaluate(()=>{
    const v=fixture.view;fixture.input=document.querySelector('[data-five-placement-field="value"]');
    fixture.canvas=v.viewport.renderer.domElement;fixture.geometry=v.viewport.sceneObjects.get(v.fivePlacement.placementId).geometry;
    fixture.input.focus({preventScroll:true});fixture.input.setSelectionRange(0,1);
    fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
  });
  await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>{fixture.input.setSelectionRange(1,2);fixture.release();});await idle();
  assert.deepEqual(await page.evaluate(()=>({same:fixture.input===document.querySelector('[data-five-placement-field="value"]'),focus:document.activeElement===fixture.input,selection:[fixture.input.selectionStart,fixture.input.selectionEnd],canvas:fixture.canvas===fixture.view.viewport.renderer.domElement,geometry:fixture.geometry===fixture.view.viewport.sceneObjects.get(fixture.view.fivePlacement.placementId).geometry})),{same:true,focus:true,selection:[1,2],canvas:true,geometry:true});
  await numeric().fill('-');await confirm();await waitRendered(base);near((await read()).placements[0].transform,base);
  assert.match(await page.locator('.five-placement-error').innerText(),/有效数值/);
  assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),true);
  await numeric().fill('12');await confirm();await waitRendered(moved);near((await read()).placements[0].transform,moved);
  await rpc({action:'undo'});await refresh();await waitRendered(base);
  await rpc({action:'redo'});await refresh();await waitRendered(moved);
  cases.push('delayed refresh preserves input/caret/canvas/geometry; invalid Enter is rejected; valid Enter is undoable and redoable');

  await command('five.tool.translate');await pick('X');await numeric().fill('5');
  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await confirm();
  const uncertain=await page.evaluate(()=>fixture.view.fivePlacement.request);assert.ok(uncertain);
  const once=await read(),finalMatrix=[...moved];finalMatrix[3]+=5;near(once.placements[0].transform,finalMatrix);
  assert.equal(await numeric().getAttribute('readonly'),'');
  assert.match(await page.locator('.five-placement-error').innerText(),/Enter/);
  await confirm();assert.deepEqual(await read(),once,'retry must not apply the delta twice');await waitRendered(finalMatrix);
  assert.equal(calls.filter(c=>c.method==='CamPlacement.SetTransform'&&c.payload.commandId===uncertain.commandId).length,2);
  cases.push('lost command/query receipts retry with Enter using the original immutable command');

  await numeric().fill('100');await rpc({action:'undo'});await refresh();await waitRendered(moved);
  assert.equal(await numeric().count(),0);assert.equal(await page.evaluate(()=>fixture.view.viewport.transformPreviews.size),0);
  await rpc({action:'redo'});await refresh();await waitRendered(finalMatrix);
  await command('five.tool.rotate');await pick('Z');await numeric().fill('45');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();
  assert.equal(await page.evaluate(()=>fixture.view.viewport.transformPreviews.size),0);near((await read()).placements[0].transform,finalMatrix);
  cases.push('external undo and leaving the tool discard unsubmitted numeric previews');
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});near(reopened.placements[0].transform,finalMatrix);
  cases.push('repository save/reopen preserves placement without source geometry import');
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{native:'production SDO, View resources, transactions and repository',browser:'Edge WebGL and real pointer/input events',standaloneCef:false},calls},null,2));
  console.log(JSON.stringify({passed:true,cases:cases.length,output}));
  }
} catch(error) {
  failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));
  if(page){console.error(await page.evaluate(()=>({text:document.body.innerText.slice(0,2000),error:window.fixture?.view.error})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();
  try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}

