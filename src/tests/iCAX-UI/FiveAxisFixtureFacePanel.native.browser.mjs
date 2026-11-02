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
const output = browserReportDirectory('manual-setups-placement-20261030/support-browser');
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

  const cases=[],timings={},invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,undefined,{timeout:300000});
  await selectWorkpieceFile(page,process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||info.sourcePath);await idle();
  await page.waitForFunction(()=>!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  await command('five.setup.create');await idle();await command('five.fixture.generate');await idle();
  const before=await read(),count=()=>page.locator('.five-support-face').count();
  let started=performance.now();await rpc({action:'export-project',path:resolve(output,'HatRail-before.i5cam')});timings.fullNativeArchiveSaveMs=performance.now()-started;
  assert.equal(await page.locator('.five-design-tabs').count(),0);
  for(const [action,label] of [['pick','手动修改'],['auto','批量添加'],['clear','清空']]){
    const button=page.locator(`[data-five-command="five.fixture.design.${action}"]`);assert.equal(await button.getAttribute('title'),label);assert.equal(await button.locator('svg').count(),1);assert.equal((await button.innerText()).trim(),'');
  }
  const summary=page.locator('.five-support-direction-menu>summary'),menu=page.locator('.five-support-direction-menu');
  await summary.click();for(const [key,value] of [['supportX','0'],['supportY','0'],['supportZ','1']])assert.equal(await page.locator(`[data-five-fixture-field="design:${key}"]`).inputValue(),value);
  await page.locator('[data-five-fixture-field="design:supportX"]').click();await page.locator('[data-five-fixture-field="design:supportY"]').click();assert.equal(await menu.getAttribute('open'),'');
  await page.locator('[data-five-fixture-field="design:spacing"]').focus();assert.equal(await menu.getAttribute('open'),null);
  await summary.click();await page.mouse.click(450,120);assert.equal(await menu.getAttribute('open'),null);
  await summary.click();await page.locator('[data-five-fixture-field="design:supportZ"]').press('Escape');assert.equal(await menu.getAttribute('open'),null);
  assert.equal(await summary.evaluate(n=>document.activeElement===n),true);
  cases.push('Support toolbar uses icons and tooltips; advanced direction defaults to 0,0,1 and dismisses on external focus, outside click or Escape while retaining internal input focus');
  await page.locator('summary').filter({hasText:'连接与间隙'}).click();
  const radius=page.locator('[data-five-fixture-field="design:tenonRadius"]');assert.equal(await radius.inputValue(),'1');await radius.fill('.75');
  assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.tenonRadius),'.75');
  await page.locator('summary').filter({hasText:'连接与间隙'}).click();

  await command('five.fixture.design.auto');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const automatic=await page.evaluate(()=>[...fixture.view.fixtureDesignDraft.supportFaces]);assert.ok(automatic.length>0);assert.equal(await count(),automatic.length);
  assert.equal(await page.locator('[data-five-command="five.fixture.design.pick"]').getAttribute('aria-pressed'),'false');
  await page.locator('.five-support-face>button:first-child').first().click();
  const selected=await page.evaluate(()=>{const active=fixture.view.fixtureDesignOverlay.children.find(o=>o.userData.activeSelectionFace!=null);return {id:fixture.view.fixtureDesignSelectedFace,color:active.material.color.getHex(),depthTest:active.material.depthTest};});
  assert.ok(selected.id);assert.equal(selected.color,0xe83eff);assert.equal(selected.depthTest,false);
  await page.locator('.five-support-face>button:last-child').first().click();assert.equal(await count(),automatic.length-1);
  await command('five.fixture.design.auto');await idle();assert.equal(await count(),automatic.length);
  assert.deepEqual(await read(),before);
  cases.push('Bulk addition lists real native faces without entering manual mode; selecting a row highlights it in Scene, individual delete works and repeated bulk addition deduplicates the union without touching committed engineering facts');

  await command('five.fixture.design.clear');await idle();assert.equal(await count(),0);
  await command('five.fixture.design.pick');await idle();assert.equal(await page.locator('[data-five-command="five.fixture.design.pick"]').getAttribute('aria-pressed'),'true');
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,1]);fixture.view.viewport.fitView();});
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const pixel=await page.evaluate(async()=>{
    const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,mesh=vp.sceneObjects.get(v.fixtureDesignPlacement),g=mesh.geometry,p=g.attributes.position,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
    mesh.updateWorldMatrix(true,true);vp.camera.updateMatrixWorld(true);
    for(const f of v.fixtureDesignFaces)for(let i=+f.first;i<+f.first+ +f.count;++i){const q=new T.Vector3();for(let k=0;k<3;++k)q.add(new T.Vector3().fromBufferAttribute(p,g.index?g.index.getX(i*3+k):i*3+k));q.multiplyScalar(1/3).applyMatrix4(mesh.matrixWorld).project(vp.camera);
      const x=r.x+(q.x+1)*r.width/2,y=r.y+(1-q.y)*r.height/2;if(Math.abs(q.x)>.85||Math.abs(q.y)>.85||Math.abs(q.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
      ray.setFromCamera({x:q.x,y:q.y},vp.camera);const hit=ray.intersectObject(mesh,false)[0];if(hit&&hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count)return {x,y,id:String(f.faceId)};
    }throw Error('No visible face for mouse acceptance');
  });
  await page.mouse.click(pixel.x,pixel.y);assert.equal(await count(),1);assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignSelectedFace),pixel.id);
  await page.mouse.click(pixel.x,pixel.y);assert.equal(await count(),0);
  await page.mouse.click(pixel.x,pixel.y);await command('five.fixture.design.pick');await idle();
  assert.equal(await page.locator('[data-five-command="five.fixture.design.pick"]').getAttribute('aria-pressed'),'false');
  await command('five.fixture.design.auto');await idle();assert.ok(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),pixel.id));
  await page.mouse.click(pixel.x,pixel.y);assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignSelectedFace),pixel.id);
  assert.equal(await page.locator('.five-support-face.is-selected').count(),1);
  cases.push('Manual mode is a stable toggle: actual mouse clicks add and remove faces, bulk addition retains manual selections and Scene/list selection stays linked after exiting manual mode');

  // Native async face responses must preserve nodes and the latest scrolls.
  const scrollStyle=await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{overflow:auto!important;max-height:400px!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;min-height:1400px}.five-design-body{max-height:300px!important}'});
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.ReadReferenceFaces';fixture.hold=true;fixture.input=document.querySelector('[data-five-fixture-field="design:spacing"]');fixture.canvas=fixture.view.viewport.renderer.domElement;fixture.events=0;fixture.input.addEventListener('probe',()=>fixture.events++);void fixture.entry.handleRibbonCommand(fixture.context,'five.fixture.design.pick');});
  await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{fixture.input.focus({preventScroll:true});fixture.input.setSelectionRange(0,1);return ['.cam-context-pane','.cam-info-pane','.five-design-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=60+20*i;return n.scrollTop;});});
  await page.evaluate(()=>fixture.release());await idle();
  const interaction=await page.evaluate(()=>{fixture.input.dispatchEvent(new Event('probe'));return {same:fixture.input===document.querySelector('[data-five-fixture-field="design:spacing"]'),canvas:fixture.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===fixture.input,selection:[fixture.input.selectionStart,fixture.input.selectionEnd],events:fixture.events,scroll:['.cam-context-pane','.cam-info-pane','.five-design-body'].map(s=>document.querySelector(s).scrollTop)};});
  assert.deepEqual(interaction,{same:true,canvas:true,focus:true,selection:[0,1],events:1,scroll});
  await page.locator('[data-five-fixture-field="design:pattern"]').selectOption('Staggered');await page.locator('[data-five-fixture-field="design:postWidth"]').focus();
  await page.locator('[data-five-fixture-field="design:pattern"]').selectOption('Grid');assert.equal(await page.locator('[data-five-fixture-field="design:postWidth"]').count(),0);
  assert.notEqual(await page.evaluate(()=>document.activeElement?.dataset.fiveFixtureField),'design:spacing');
  cases.push('Delayed native face reads preserve input/canvas/listener identity, focus/selection and both sidebars plus nested scroll; disappearing conditional fields do not steal focus');
  await scrollStyle.evaluate(n=>n.remove());await page.evaluate(()=>{for(const n of document.querySelectorAll('.cam-context-pane,.cam-info-pane,.five-design-body'))n.scrollTop=0;});

  // Keep the generation bounded on the real HatRail, using all lower faces.
  await command('five.fixture.design.clear');await command('five.fixture.design.auto');await idle();
  await page.locator('[data-five-fixture-field="design:spacing"]').fill('100');
  await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('None');
  await page.locator('summary').filter({hasText:'连接与间隙'}).click();await page.locator('[data-five-fixture-field="design:jointMode"]').selectOption('Weld');
  await page.screenshot({path:resolve(output,'face-panel.png')});
  const generationBefore=await read();await page.evaluate(()=>{fixture.holdMethod='CamFixture.Generate';fixture.hold=true;});
  const generating=command('five.fixture.design.generate');await page.waitForFunction(()=>fixture.held);
  assert.equal(await page.locator('dialog.five-blocking-progress[open]').count(),1);assert.equal(await page.locator('.five-design-footer .five-design-progress').count(),0);
  await page.evaluate(()=>fixture.release());await generating;await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.ok(await page.evaluate(()=>fixture.view.fiveFixtureGenerated?.design.candidate.parts.length>1));
  assert.deepEqual(await read(),generationBefore);assert.equal(await page.locator('.five-design-tabs').count(),0);
  assert.equal(await page.locator('[data-five-command="five.fixture.design.confirm"]').isDisabled(),false);
  await command('five.fixture.design.cancel');await idle();assert.equal(await page.locator('.five-fixture-generator').count(),0);assert.deepEqual(await read(),generationBefore);
  cases.push('Generate displays temporary actual plates without changing the project; Cancel closes the editor and discards every edit and generated object');
  await command('five.fixture.generate');await idle();await command('five.fixture.design.auto');await idle();
  await page.evaluate(()=>{fixture.holdMethod='CamFixture.Generate';fixture.hold=true;});const cancelledGeneration=command('five.fixture.design.generate');
  await page.waitForFunction(()=>fixture.held);await command('five.fixture.design.cancel');assert.equal(await page.locator('.five-fixture-generator').count(),0);
  await page.evaluate(()=>fixture.release());await cancelledGeneration;await idle();assert.deepEqual(await read(),generationBefore);
  cases.push('Cancel during an outstanding generation reply closes immediately and cancels the late native task without restoring the discarded editor or changing the project');
  await command('five.fixture.generate');await idle();await command('five.fixture.design.auto');await idle();
  await page.locator('[data-five-fixture-field="design:spacing"]').fill('100');await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('None');
  await page.locator('summary').filter({hasText:'连接与间隙'}).click();await page.locator('[data-five-fixture-field="design:jointMode"]').selectOption('Weld');
  await command('five.fixture.design.generate');await idle();
  await page.locator('.five-design-body').evaluate(n=>n.scrollTop=0);await page.screenshot({path:resolve(output,'generated-before-confirm.png')});
  await page.evaluate(()=>fixture.dropApply=true);await command('five.fixture.design.confirm');await idle();
  const confirmed=await read();assert.equal(confirmed.fixtureUnits.length,1);assert.ok(confirmed.fixtures.length>1);assert.equal(await page.locator('.five-fixture-generator').count(),0);
  assert.deepEqual(confirmed.workpieces[0].geometryRef,before.workpieces[0].geometryRef);
  await rpc({action:'undo'});const undone=await read();for(const key of ['workpieces','placements','setups','fixtures','fixtureUnits'])assert.deepEqual(undone[key],before[key]);
  await rpc({action:'redo'});const redone=await read();assert.deepEqual(redone.fixtures,confirmed.fixtures);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtures,confirmed.fixtures);
  cases.push('Confirm recovers a lost acknowledgement, commits the fixture once, closes the editor and supports atomic undo/redo and native save/reopen with the source CAD geometry preserved');
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,timings,interaction,sourcePath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||info.sourcePath,coverage:{native:true,realMouse:true,standaloneCef:false},calls},null,2));console.log(JSON.stringify({passed:true,cases,timings,output}));
} catch(error) {
  failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));
  if(page){console.error(await page.evaluate(()=>({text:document.body.innerText.slice(-2500),error:window.fixture?.view.error})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();
  try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}

