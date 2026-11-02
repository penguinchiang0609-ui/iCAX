import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('five-axis-fixtures-browser');
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
    ? { ...message, payload: undefined, payloadText: serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  const info = await rpc({ action: 'info' });
  if(process.env.ICAX_AUTOMOTIVE_STEP) info.sourcePath=resolve(process.env.ICAX_AUTOMOTIVE_STEP);
  if(process.env.ICAX_TEST_SUPPORT_STEP)writeFileSync(resolve(process.env.ICAX_TEST_SUPPORT_STEP),readFileSync(info.fixturePath));
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
        const result = await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payload });
        if (method === 'CamWorkpiece.CancelTask') f.cancelAcknowledged = true;
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
      project: { projectId: info.projectId, projectName: '汽车冲压件 · 装夹测试' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog(options) { ++f.fileSelections; return options.title.includes('工装') ? info.fixturePath : info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) { return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);
  const cases = [];
  const tabs=await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.map(t=>[t.id,t.title]));
  assert.deepEqual(tabs,[['workpiece','工件'],['fixture','装夹'],['toolpath','CAM'],['simulation','孪生'],['help','帮助']]);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('fixture'));
  assert.match(await page.locator('.five-fixture-editor').innerText(),/请先导入工件/);
  assert.equal(await page.locator('.five-placement-tools').count(),0);
  await page.locator('[data-five-command="five.area.workpiece"]').click();
  await selectWorkpieceFile(page, info.sourcePath);
  await page.waitForFunction(() => !fixture.view.pending, null, {timeout:600000});
  const imported = await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.ok(imported.workpieces[0]?.id, await page.evaluate(()=>fixture.view.error));
  assert.equal(imported.workpieces[0].importMetadata.validBRep,true);
  await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount===1,null,{timeout:120000});
  const inspection = await rpc({action:'invoke',scope:'scene',method:'CamWorkpiece.Inspect',payload:{workpieceId:imported.workpieces[0]?.id}});
  const geometry = await page.evaluate(()=>{
    const viewport=fixture.view.viewport,object=[...viewport.sceneObjects.values()][0];
    let mesh;object.traverse(child=>{if(!mesh&&child.isMesh&&child.geometry?.attributes.position)mesh=child;});
    const v=mesh.geometry.attributes.position,p=mesh.localToWorld(viewport.camera.position.clone().set(v.getX(0),v.getY(0),v.getZ(0)));
    mesh.geometry.computeBoundingBox();const bounds=mesh.geometry.boundingBox;
    return {point:p.toArray(),bounds:{min:bounds.min.toArray(),max:bounds.max.toArray()},vertices:v.count};
  });
  cases.push('real STEP -> native exact BRep and production viewport');
  const initialCanvas=await page.evaluate(()=>{window.initialCanvas=fixture.view.viewport.renderer.domElement;return !!initialCanvas;});assert.equal(initialCanvas,true);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'fixture');
  assert.equal(await page.locator('.five-placement-tools').count(),0);
  assert.equal(await page.locator('[data-five-command="workpiece.remove"]').count(),0);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.fixture.focus.design'));
  assert.ok(await page.locator('[data-five-key="fixture-design"]').isVisible());
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.fixture.focus.check'));
  assert.ok(await page.locator('[data-five-key="fixture-check"]').isVisible());
  cases.push('independent Fixture page has an empty-state route, fixture controls and direct design/check entries; Workpiece keeps placement separate');
  const waitIdle=()=>page.waitForFunction(()=>!fixture.view.pending,null,{timeout:300000});
  const click=async action=>{await page.locator(`[data-five-command="five.fixture.${action}"]`).first().click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await waitIdle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  await click('import');
  await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount===2);
  const fixtureId = await page.evaluate(()=>fixture.view.scene.domain.fixtures[0].id);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));
  await page.locator('.five-placement-tools').waitFor();
  assert.equal(await page.locator('.five-fixture-editor').count(),0);
  const workpieceCommands=await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='workpiece').groups.flatMap(g=>g.commands.map(c=>c.id)));
  assert.ok(!workpieceCommands.some(id=>id.includes('fixture')||id==='five.tool.fine'));
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,`five.fixture.select:${id}`),fixtureId);
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'fixture');
  assert.equal(await page.evaluate(()=>fixture.view.viewport.renderer.domElement===initialCanvas),true);
  assert.deepEqual(await page.evaluate(()=>fixture.view.scene.domain.workpieces),imported.workpieces);
  cases.push('Workpiece, Fixture and CAM switches retain the same canvas, setup and model; selecting a fixture from CAM opens Fixture');
  const coordinates=geometry.point.map((v,i)=>v-[15,15,30][i]);
  const fill=async(key,value)=>page.locator(`[data-five-fixture-field="${key}"]`).fill(String(value));
  await fill('name','测试支撑块');
  for(const [i,key] of ['x','y','z'].entries())await fill(key,coordinates[i]);
  await click('apply');
  await page.waitForFunction(({id,x})=>Math.abs(fixture.view.viewport.sceneObjects.get(id).matrixWorld.elements[12]-x)<1e-5,{id:fixtureId,x:coordinates[0]});
  await click('check');
  const report=()=>page.evaluate(()=>fixture.view.scene.domain.setups[0].fixtureCheck);
  const contact=await report();assert.equal(contact.contactCount,1);
  assert.match(await page.locator('.five-fixture-results').innerText(),/接触 \/ 相交/);
  await fill('clearance',3);assert.equal(await page.locator('.five-fixture-results').count(),0);
  assert.match(await page.locator('.five-fixture-stale').innerText(),/重新检查/);await fill('clearance',2);
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({path:resolve(output,'automotive-contact.png')});
  cases.push('fixture imported, named, moved via UI and exact contact detected on real workpiece');
  await click('visible');
  await page.waitForFunction(id=>!fixture.view.viewport.sceneObjects.get(id)?.visible,fixtureId);
  await click('check');assert.equal((await report()).contactCount,1);
  await click('collision');
  assert.equal(await page.locator('[data-five-command="five.fixture.check"]').isDisabled(),true);
  assert.match(await page.locator('.five-fixture-stale').innerText(),/重新检查/);
  await click('collision');await click('visible');
  cases.push('hidden fixture remains checked; collision participation is separate and invalidates report');
  await fill('x',geometry.bounds.max[0]+100);await click('apply');await click('check');
  const separated=await report();assert.equal(separated.contactCount,0);assert.equal(separated.clearanceCount,0);
  assert.ok(separated.pairs[0].distanceMm>50);
  cases.push('moving fixture away changes geometry and measured clearance');
  // Additional real imported fixtures make the object tree scroll naturally.
  const extraFixtures=[];
  for(let n=0;n<7;++n) {
    const state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    let task=await rpc({action:'invoke',scope:'scene',method:'CamFixture.Import',payload:{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,sourcePath:info.fixturePath}});
    while(task.status==='Running') {await new Promise(r=>setTimeout(r,30));task=await rpc({action:'invoke',scope:'scene',method:'CamFixture.QueryTask',payload:{epoch:task.epoch,taskId:task.taskId}});}
    assert.equal(task.status,'Completed',task.error);extraFixtures.push(task.result.fixtureId);
  }
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  await page.setViewportSize({width:1440,height:460});
  // Check newer typing, focus/selection/listeners and nested/outer scrolls
  // during an actual native refresh in the constrained viewport.
  await page.evaluate(()=>{
    const left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),nested=document.querySelector('.five-tree-rows');
    left.scrollTop=20;right.scrollTop=40;nested.scrollTop=30;
    const input=document.querySelector('[data-five-fixture-field="x"]');input.focus({preventScroll:true});input.setSelectionRange(1,3);
    window.kept={left,right,nested,input,canvas:document.querySelector('canvas'),calls:0};input.addEventListener('test-retained',()=>kept.calls++);
    fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
  });
  await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>{
    kept.input.value='1234.567';kept.input.dispatchEvent(new Event('input',{bubbles:true}));kept.input.setSelectionRange(2,5);
    kept.left.scrollTop=35;kept.right.scrollTop=70;kept.nested.scrollTop=45;
    kept.expected=[kept.left.scrollTop,kept.right.scrollTop,kept.nested.scrollTop];fixture.release();
  });
  await waitIdle();
  const retained=await page.evaluate(()=>{
    kept.input.dispatchEvent(new Event('test-retained'));
    return {input:kept.input===document.querySelector('[data-five-fixture-field="x"]'),canvas:kept.canvas===document.querySelector('canvas'),
      focus:document.activeElement===kept.input,selection:[kept.input.selectionStart,kept.input.selectionEnd],value:kept.input.value,
      calls:kept.calls,scroll:[kept.left.scrollTop,kept.right.scrollTop,kept.nested.scrollTop],expected:kept.expected};
  });
  assert.equal(retained.input,true);assert.equal(retained.canvas,true);assert.equal(retained.focus,true);assert.equal(retained.calls,1);
  assert.equal(retained.value,'1234.567');assert.deepEqual(retained.selection,[2,5]);
  assert.ok(retained.expected.every(n=>n>0));assert.deepEqual(retained.scroll,retained.expected);
  await page.setViewportSize({width:1440,height:900});
  for(const id of extraFixtures) {
    const state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    await rpc({action:'invoke',scope:'scene',method:'CamFixture.Remove',payload:{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,fixtureId:id}});
  }
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  await click('reset');
  cases.push('real native refresh preserves input/canvas nodes, listener, latest typing, selection, focus and pane scroll');
  // Conditional editor disappears when deleting; undo restores the real object.
  const before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await click('remove');assert.equal(await page.locator('[data-five-fixture-field="x"]').count(),0);
  await rpc({action:'undo'});await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  assert.deepEqual(await page.evaluate(()=>fixture.view.scene.domain.fixtures),before.fixtures);
  await rpc({action:'redo'});await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  assert.equal(await page.evaluate(()=>fixture.view.scene.domain.fixtures.length),0);
  await rpc({action:'undo'});await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  assert.deepEqual(await page.evaluate(()=>fixture.view.scene.domain.workpieces),imported.workpieces);
  cases.push('fixture deletion, undo/redo and conditional fields preserve machining workpiece');
  // Deliver a recognizable assembled case, preserving its contact report.
  for(const [i,key] of ['x','y','z'].entries())await fill(key,coordinates[i]);
  await click('apply');await click('check');
  const savedState=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.fixtures,savedState.fixtures);assert.deepEqual(reopened.setups,savedState.setups);
  await page.evaluate(async()=>{fixture.entry.mountProject(fixture.context);fixture.view=(await import('/src/apps/_shared/workbench/state/projectViewStore.mjs')).getProjectView(fixture.context.project.projectId);});await waitIdle();
  await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount===2,null,{timeout:120000});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({path:resolve(output,'automotive-fixture.png')});
  if(process.env.ICAX_AUTOMOTIVE_PROJECT)await rpc({action:'export-project',path:resolve(process.env.ICAX_AUTOMOTIVE_PROJECT)});
  cases.push('embedded geometry, fixture transforms and report survive project save/reopen');
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,source:pageImportSource,inspection,geometry,contact,separated,retained,
    coverage:{browser:'Chromium Edge',fileChooser:'injected selected paths',scene:'production SDO and Repository',rendering:'production View and resources',standaloneCef:false},savedState,reopened},null,2));
  console.log(`Fixture native browser passed: ${cases.length} cases, ${inspection.cadInspection.faceCount} faces`);
} catch(error) {
  failure=error;
  if(page){console.error(await page.evaluate(()=>({text:document.body.innerText.slice(-3000),error:window.fixture?.view.error})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  clearInterval(diagnostic);
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));
  await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}
