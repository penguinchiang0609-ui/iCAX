import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { runSupportPatterns } from './FixtureSupportPatterns.browserCase.mjs';
import { recheckSupportInputs, recheckSupportPreview } from './FixtureSupportInputs.browserCase.mjs';
import { recheckJoineryPresentation } from './FixtureJoineryPresentation.browserCase.mjs';
import { recheckSupportOcclusion } from './FixtureSupportOcclusion.browserCase.mjs';
import { recheckOpeningStrategy } from './FixtureOpeningNative.browserCase.mjs';
import { recheckPlateEditing } from './FixturePlateEditingNative.browserCase.mjs';
import { recheckPlateCutouts } from './FixturePlateCutoutsNative.browserCase.mjs';
import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('fixture-design-browser');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root, 'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, 'FiveAxisCAMTest.exe'),
  ['--gtest_filter=FiveAxisBridge.DISABLED_Serve', '--gtest_also_run_disabled_tests'],
  { cwd: nativeDirectory, windowsHide: true, stdio: ['pipe','pipe','pipe'] });
let sequence = 0, browser, page, failure, pageImportSource;
const waiting = new Map(), calls = [], errors = [], nativeLog = [];
const diagnostic = setInterval(()=>console.log('Native stage:',calls.at(-1)?.method??calls.at(-1)?.action),30000);
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
  const timer = setTimeout(() => { waiting.delete(id); reject(Error(`Native timeout ${message.action}`)); }, 120000);
  waiting.set(id, { resolve, reject, timer });
  const wire = message.action === 'invoke'
    ? { ...message, payload: undefined, payloadText: message.payloadText ?? serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  if(process.env.ICAX_FIXTURE_PROJECT)await rpc({action:'open-project',path:resolve(process.env.ICAX_FIXTURE_PROJECT)});
  const info = await rpc({ action: 'info' });
  if(process.env.ICAX_AUTOMOTIVE_STEP) info.sourcePath=resolve(process.env.ICAX_AUTOMOTIVE_STEP);
  if(process.env.ICAX_TEST_SUPPORT_STEP)writeFileSync(resolve(process.env.ICAX_TEST_SUPPORT_STEP),readFileSync(info.fixturePath));
  info.exportRoot=output;
  pageImportSource = info.sourcePath;
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge', headless: true, args:['--disable-gpu'] });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.setDefaultTimeout(30000);
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
        // Preserve typed UUID parameters before Playwright's structured clone
        // drops the serializer's private Symbol marker.
        const { serializeVariantText } = await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs');
        const result = await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payloadText: serializeVariantText(payload) });
        if (method === 'CamWorkpiece.CancelTask') f.cancelAcknowledged = true;
        return result;
      }, async getState() { return this.state; } };
    const resource = async (url, options, head) => {
      const version = new Headers(options?.headers).get('ICAX-Resource-Version') ?? String(options?.version ?? '');
      const result = await fiveNativeRpc({ action: 'resource', url, version, head, encoding:'base64' });
      const bytes=head?null:Uint8Array.from(atob(result.base64),c=>c.charCodeAt(0));
      if(!head){try{const {parseViewResource}=await import('/src/iCAX-UI/SDK/View/viewResource.mjs');f.debugView=parseViewResource(bytes.buffer);}catch{}}
      return new Response(bytes, { headers: { 'Content-Type': 'application/vnd.icax.flatbuffer', 'ICAX-Resource-Version': result.version } });
    };
    sceneProxy.resources = { get: (url, options) => resource(url, options, false), head: (url, options) => resource(url, options, true) };
    sceneProxy.views = new ViewClient(sceneProxy);
    const context = f.context = { mount: document.querySelector('#app'), sceneProxy,
      project: { projectId: info.projectId, projectName: '汽车冲压件 · 装夹测试' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openDirectoryDialog() { return info.exportRoot; }, async openFileDialog(options) { ++f.fileSelections; return options.title.includes('工装') ? info.fixturePath : info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) { return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);

  if(process.env.ICAX_FIXTURE_PART_MODE_RECHECK){
    const {recheckFixturePartMode}=await import('./FixturePartModeNative.browserCase.mjs');
    await recheckFixturePartMode({page,rpc,info,output,errors});
  }else if(process.env.ICAX_FIXTURE_CUTOUT_RECHECK){
    await recheckPlateCutouts({page,rpc,info,output,errors});
  }else if(process.env.ICAX_FIXTURE_PART_EDITING_RECHECK){
    await recheckPlateEditing({page,rpc,info,output,errors});
  }else if(process.env.ICAX_FIXTURE_OPENING_RECHECK){
    await recheckOpeningStrategy({page,rpc,info,output,errors});
  }else if(process.env.ICAX_FIXTURE_DELIVERY_RECHECK){
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
    for(const name of ['breakdown','export']) {
      await page.locator('[data-five-command="five.fixture.design.'+name+'"]').click();
      await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));
      await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,null,{timeout:120000});
      assert.equal(await page.evaluate(()=>fixture.view.error),'');
    }
    const saved=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    const design=saved.setups.find(s=>s.id===saved.activeSetupId).fixtureDesign;
    assert.equal(design.breakdown.parts.length,11);assert.ok(design.delivery.directory);
    await rpc({action:'export-project',path:resolve(output,'grid-support.i5cam')});
    const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtures,saved.fixtures);assert.deepEqual(reopened.workpieces,saved.workpieces);
    assert.deepEqual(errors,[]);writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,parts:design.breakdown.parts.length,delivery:design.delivery,sourceGeometry:saved.workpieces.map(w=>w.geometryRef),cases:['actual packaged breakdown and export buttons complete native DXF readback and delivery','corrected full-thickness grid, joinery and display resources survive save/reopen']},null,2));
  }else if(process.env.ICAX_FIXTURE_OCCLUSION){
    await recheckSupportOcclusion({page,rpc,output,errors});
  }else if(process.env.ICAX_FIXTURE_JOINERY_PRESENTATION){
    await recheckJoineryPresentation({page,output,errors});
  }else if(process.env.ICAX_FIXTURE_PREVIEW_RECHECK){
    await recheckSupportPreview({page,rpc,output,errors});
  }else if(process.env.ICAX_FIXTURE_INPUT_RECHECK){
    await recheckSupportInputs({page,rpc,output,errors});
  }else if(process.env.ICAX_FIXTURE_SUPPORT_CASE){
    await runSupportPatterns({page,rpc,info,output,errors});
  }else if(process.env.ICAX_FIXTURE_SLOT_CASE){
    const cases=[],idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,null,{timeout:600000});
    const click=async name=>{await page.locator('[data-five-command="five.fixture.design.'+name+'"]').click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();};
    const fill=async(key,value)=>page.locator('[data-five-fixture-field="design:'+key+'"]').fill(String(value));
    await selectWorkpieceFile(page,info.fixturePath);await idle();
    const firstPlacement=await page.evaluate(()=>fixture.view.scene.activePlacementId);
    await selectWorkpieceFile(page,info.sourcePath);await idle();
    const coverPlacement=await page.evaluate(()=>fixture.view.scene.activePlacementId);
    assert.notEqual(firstPlacement,coverPlacement);
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
    await click('pick');
    assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignPlacement),coverPlacement);
    assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignFaces.length),4);
    for(const direction of ['top','bottom']){
      const spots=await page.evaluate(async direction=>{
        const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
        const v=fixture.view,vp=v.viewport;vp.setStandardView(direction);vp.fitView(1.15);await new Promise(requestAnimationFrame);
        const source=vp.sceneObjects.get(v.fixtureDesignPlacement),rect=vp.renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster(),found=new Map();source.updateMatrixWorld(true);
        for(let x=rect.width*.08;x<rect.width*.92;x+=9)for(let y=rect.height*.08;y<rect.height*.92;y+=9){
          ray.setFromCamera(new THREE.Vector2(x/rect.width*2-1,1-y/rect.height*2),vp.camera);const hit=ray.intersectObject(source,false)[0];if(!hit)continue;
          const face=v.fixtureDesignFaces.find(f=>hit.faceIndex>=Number(f.first)&&hit.faceIndex<Number(f.first)+Number(f.count));
          if(face&&!found.has(face.faceId))found.set(face.faceId,{x:rect.x+x,y:rect.y+y,faceId:String(face.faceId)});
        }return [...found.values()];
      },direction);
      for(const spot of spots){
        if(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),spot.faceId))continue;
        await page.mouse.click(spot.x,spot.y);
        assert.ok(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),spot.faceId));
      }
    }
    assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces.length),4);
    await fill('positions','0');await click('generate');assert.equal(await page.evaluate(()=>fixture.view.error),'');
    let result=await page.evaluate(()=>fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).fixtureDesign.candidate);
    assert.equal(result.parts.length,2);assert.ok(result.parts[0].reliefCount>0);assert.ok(result.parts.every(p=>p.minimumDistance>=.2));
    assert.equal(await page.evaluate(()=>fixture.view.fixtureCandidatePreview.group.children.length),2);
    cases.push('original four-face slotted cover selected by pointer as second workpiece; zero seam section generates relieved connected support and base');
    await click('confirm');const first=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});assert.equal(first.fixtures.length,2);
    await page.setViewportSize({width:1440,height:520});
    await page.locator('[data-five-fixture-field="design:thickness"]').scrollIntoViewIfNeeded();
    await page.evaluate(()=>{
      const input=document.querySelector('[data-five-fixture-field="design:thickness"]'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
      input.focus({preventScroll:true});input.setSelectionRange(0,1);window.kept={input,left,right,canvas:document.querySelector('canvas'),calls:0};input.addEventListener('retention-probe',()=>kept.calls++);
      fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
    });await page.waitForFunction(()=>fixture.held);
    await page.evaluate(()=>{kept.input.value='6';kept.input.dispatchEvent(new Event('input',{bubbles:true}));kept.input.setSelectionRange(0,1);kept.left.scrollTop+=15;kept.right.scrollTop+=30;kept.scroll=[kept.left.scrollTop,kept.right.scrollTop];fixture.release();});await idle();
    const retention=await page.evaluate(()=>{kept.input.dispatchEvent(new Event('retention-probe'));return {node:kept.input===document.querySelector('[data-five-fixture-field="design:thickness"]'),canvas:kept.canvas===document.querySelector('canvas'),focus:document.activeElement===kept.input,value:kept.input.value,selection:[kept.input.selectionStart,kept.input.selectionEnd],scroll:[kept.left.scrollTop,kept.right.scrollTop],expected:kept.scroll,calls:kept.calls};});
    assert.equal(retention.node,true);assert.equal(retention.canvas,true);assert.equal(retention.focus,true);assert.equal(retention.value,'6');assert.deepEqual(retention.selection,[0,1]);assert.deepEqual(retention.scroll,retention.expected);assert.equal(retention.calls,1);
    await page.setViewportSize({width:1440,height:1000});await click('generate');assert.equal(await page.evaluate(()=>fixture.view.error),'');await click('confirm');
    const repeated=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});assert.equal(repeated.fixtures.length,2);assert.deepEqual(repeated.fixtures.map(f=>f.id).sort(),first.fixtures.map(f=>f.id).sort());
    cases.push('six millimetre full-thickness regeneration; stable part identities; latest field, focus, selection, nodes, listener, canvas and both pane scroll positions retained');
    await fill('positions','100000');await click('generate');assert.ok(await page.evaluate(()=>fixture.view.error));assert.equal(await page.evaluate(()=>fixture.view.scene.domain.fixtures.length),2);
    await fill('positions','0');await click('generate');await click('confirm');await click('breakdown');await click('export');assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const saved=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),design=saved.setups.find(s=>s.id===saved.activeSetupId).fixtureDesign,exportDirectory=design.delivery.directory;
    await page.locator('[data-five-workpiece="'+firstPlacement+'"]').click();await idle();assert.equal(await page.evaluate(()=>!!fixture.view.fixtureDesignPicking),false);await click('pick');assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignPlacement),firstPlacement);
    await page.locator('[data-five-workpiece="'+coverPlacement+'"]').click();await idle();assert.equal(await page.evaluate(()=>!!fixture.view.fixtureDesignPicking),false);assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces.length),4);
    cases.push('switching workpieces ends old face picking and restores the correct setup recipe; failed regeneration preserves the accepted assembly');
    await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtures,saved.fixtures);
    await page.evaluate(async()=>{fixture.entry.mountProject(fixture.context);fixture.view=(await import('/src/apps/_shared/workbench/state/projectViewStore.mjs')).getProjectView(fixture.context.project.projectId);});await idle();
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
    await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size>=3);
    await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.6]);fixture.view.viewport.fitView();document.querySelector('.cam-info-pane').scrollTop=10000;});
    await page.screenshot({path:resolve(output,'assembled.png')});await rpc({action:'export-project',path:resolve(output,'perforated-cover-fixture.i5cam')});
    cases.push('adopt, breakdown, native DXF readback, export and embedded save/reopen complete with the original cover');assert.deepEqual(errors,[]);
    writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,source:info.sourcePath,firstPlacement,coverPlacement,retention,parts:design.assembly.parts,exportDirectory,coverage:{browser:'Edge Chromium',fileChooser:'injected paths',native:'production SDO and repository',standaloneCef:false}},null,2));console.log(JSON.stringify({passed:true,cases,exportDirectory}));
  }else{
  const cases=[];
  await selectWorkpieceFile(page,info.sourcePath);
  await page.waitForFunction(()=>!fixture.view.pending,null,{timeout:600000});
  await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size>0);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending,null,{timeout:600000});
  const click=async name=>{await page.locator('[data-five-command="five.fixture.design.'+name+'"]').click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();};
  const fill=async(key,value)=>page.locator('[data-five-fixture-field="design:'+key+'"]').fill(String(value));
  await click('pick');
  await page.waitForFunction(()=>fixture.view.fixtureDesignPicking&&fixture.view.fixtureDesignPlacement);
  await page.evaluate(()=>fixture.view.viewport.fitView(1.15));
  const spots=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const v=fixture.view, vp=v.viewport, source=vp.sceneObjects.get(v.fixtureDesignPlacement), rect=vp.renderer.domElement.getBoundingClientRect();
    const ray=new THREE.Raycaster(),found=new Map();source.updateMatrixWorld(true);
    for(let x=rect.width*.1;x<rect.width*.9;x+=18)for(let y=rect.height*.1;y<rect.height*.9;y+=18){
      ray.setFromCamera(new THREE.Vector2(x/rect.width*2-1,1-y/rect.height*2),vp.camera);
      const hit=ray.intersectObject(source,false)[0];if(!hit)continue;
      const face=v.fixtureDesignFaces.find(f=>hit.faceIndex>=Number(f.first)&&hit.faceIndex<Number(f.first)+Number(f.count));if(!face)continue;
      const normal=hit.face.normal.clone().transformDirection(source.matrixWorld);if(Math.abs(normal.z)<.65)continue;
      const old=found.get(face.faceId);if(!old||Math.abs(hit.point.x)<Math.abs(old.position))found.set(face.faceId,{x:rect.x+x,y:rect.y+y,position:hit.point.x,faceId:face.faceId,count:Number(face.count)});
    }
    return [...found.values()].sort((a,b)=>b.count-a.count).slice(0,24);
  });
  assert.ok(spots.length,'visible smooth support-face targets');
  let candidate,chosen;
  const attempts=[];
  for(const spot of spots){
    await click('clear');
    if(!await page.evaluate(()=>fixture.view.fixtureDesignPicking))await click('pick');
    await page.mouse.click(spot.x,spot.y);
    const selected=await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces);
    if(!selected.length)continue;
    await fill('positions',spot.position.toFixed(4));await click('generate');
    const error=await page.evaluate(()=>fixture.view.error);attempts.push({face:spot.faceId,position:spot.position,error});
    if(error)continue;
    candidate=await page.evaluate(()=>fixture.view.scene.domain.setups[0].fixtureDesign.candidate);chosen=spot;break;
  }
  assert.ok(candidate,JSON.stringify(attempts));assert.equal(candidate.parts.length,2);
  assert.ok(candidate.parts.every(p=>p.minimumDistance>=.2-1e-6));
  assert.ok(await page.evaluate(()=>fixture.view.fixtureCandidatePreview?.group.children.length===2));
  cases.push('real imported automotive surface picked with pointer; VTK/Clipper2/Coal candidate preview and finite-thickness support/base');
  await click('confirm');assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size===3);
  await click('breakdown');await click('export');assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const first=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.ok(first.setups[0].fixtureDesign.delivery.directory);
  // Actual native refresh, retaining the latest field edit and scroll positions.
  await page.setViewportSize({width:1440,height:520});
  await page.locator('[data-five-fixture-field="design:baseDrop"]').scrollIntoViewIfNeeded();
  await page.evaluate(()=>{
    const left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),input=document.querySelector('[data-five-fixture-field="design:baseDrop"]');
    input.focus({preventScroll:true});input.setSelectionRange(0,1);window.kept={left,right,input,canvas:document.querySelector('canvas'),calls:0};input.addEventListener('retention-probe',()=>kept.calls++);
    fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
  });
  await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>{
    kept.input.value='50';kept.input.dispatchEvent(new Event('input',{bubbles:true}));kept.input.setSelectionRange(1,2);kept.right.scrollTop+=25;kept.left.scrollTop+=10;
    kept.scroll=[kept.left.scrollTop,kept.right.scrollTop];fixture.release();
  });await idle();
  const retention=await page.evaluate(()=>{kept.input.dispatchEvent(new Event('retention-probe'));return {node:kept.input===document.querySelector('[data-five-fixture-field="design:baseDrop"]'),canvas:kept.canvas===document.querySelector('canvas'),focus:document.activeElement===kept.input,value:kept.input.value,selection:[kept.input.selectionStart,kept.input.selectionEnd],scroll:[kept.left.scrollTop,kept.right.scrollTop],expected:kept.scroll,calls:kept.calls};});
  assert.equal(retention.node,true);assert.equal(retention.canvas,true);assert.equal(retention.focus,true);assert.equal(retention.value,'50');assert.deepEqual(retention.selection,[1,2]);assert.deepEqual(retention.scroll,retention.expected);assert.equal(retention.calls,1);
  assert.equal(await page.locator('[data-five-command="five.fixture.design.export"]').isDisabled(),true);
  await page.setViewportSize({width:1440,height:1000});await click('generate');assert.equal(await page.evaluate(()=>fixture.view.error),'');await click('confirm');await click('breakdown');await click('export');
  const second=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.equal(second.fixtures.length,2);assert.deepEqual(second.fixtures.map(f=>f.id).sort(),first.fixtures.map(f=>f.id).sort());
  cases.push('native refresh retains latest typing, nodes, canvas, listener, focus, selection and scroll; dirty draft blocks export');
  cases.push('repeat generation replaces same stable part identities without accumulating fixtures');
  const exportDirectory=second.setups[0].fixtureDesign.delivery.directory;
  await fill('positions','100000');await click('generate');assert.ok(await page.evaluate(()=>fixture.view.error));
  assert.equal(await page.evaluate(()=>fixture.view.scene.domain.fixtures.length),2);
  await fill('positions',chosen.position.toFixed(4));await click('generate');await click('confirm');await click('breakdown');
  cases.push('failed regeneration preserves previous assembled parts');
  const saved=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.fixtures,saved.fixtures);assert.deepEqual(reopened.setups,saved.setups);
  await page.evaluate(async()=>{fixture.entry.mountProject(fixture.context);fixture.view=(await import('/src/apps/_shared/workbench/state/projectViewStore.mjs')).getProjectView(fixture.context.project.projectId);});await idle();
  await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size===3);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
  await page.evaluate(()=>{fixture.view.viewport.fitView();document.querySelector('.cam-info-pane').scrollTop=0;});
  await page.screenshot({path:resolve(output,'assembled.png')});
  await rpc({action:'export-project',path:resolve(output,'curved-support.i5cam')});
  cases.push('generated BRep resources, recipe, assembly and breakdown survive saved project reopen');
  const fixtureAction=async action=>{await page.locator('[data-five-command="five.fixture.'+action+'"]').first().click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  await fixtureAction('import');
  const importedFixture=await page.evaluate(()=>fixture.view.fiveAxisFixtures.selected);
  await page.locator('[data-five-fixture-field="x"]').fill('1000');await fixtureAction('apply');await fixtureAction('check');
  const checked=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.ok(checked.setups[0].fixtureCheck.pairs.filter(p=>p.first===importedFixture||p.second===importedFixture).every(p=>p.status==='Separated'));
  await fixtureAction('remove');assert.equal(await page.evaluate(()=>fixture.view.scene.domain.fixtures.length),2);
  cases.push('imported fixture positioning and Coal static checks coexist with generated assembly; removal preserves generated parts');
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,source:info.sourcePath,chosen,attempts,retention,exportDirectory,parts:second.setups[0].fixtureDesign.assembly.parts,coverage:{browser:'Edge Chromium',fileChooser:'injected paths',native:'production SDO and repository',standaloneCef:false}},null,2));
  console.log(JSON.stringify({passed:true,cases,exportDirectory}));
  }
} catch(error) {
  failure=error;
  writeFileSync(resolve(output,'failure.json'),JSON.stringify({error:error.stack,calls,errors},null,2));
  if(page){writeFileSync(resolve(output,'view-debug.json'),JSON.stringify(await page.evaluate(()=>({view:fixture.debugView,state:fixture.view.scene.domain,debug:fixture.view.viewport?.getDebugState()})),null,2));console.error(await page.evaluate(()=>({text:document.body.innerText.slice(-3000),error:window.fixture?.view.error})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  clearInterval(diagnostic);
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));
  await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}
