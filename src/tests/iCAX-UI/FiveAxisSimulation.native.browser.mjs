import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
import { verifySimulationTrace } from './FiveAxisSimulationTrace.native.browser.mjs';
import { verifyRapidPerformance } from './FiveAxisRapidPerformance.native.browser.mjs';
import { verifyDiscretePlayback } from './FiveAxisDiscretePlayback.native.browser.mjs';
import { prepareDiscreteScene } from './fixtures/fiveAxisDiscreteScene.mjs';
import { verifyRapid206 } from './FiveAxisRapid206.native.browser.mjs';
import { verifyManualRapids } from './FiveAxisRapidManual.native.browser.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('five-axis-simulation');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root,'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, process.env.ICAX_FIVE_AXIS_NATIVE_EXECUTABLE || 'FiveAxisCAMTest.exe'),
  ['--gtest_filter=FiveAxisBridge.DISABLED_Serve', '--gtest_also_run_disabled_tests'],
  { cwd: nativeDirectory, windowsHide: true, env: { ...process.env, ICAX_FIVE_AXIS_BRIDGE_RECOGNITION: '1' }, stdio: ['pipe','pipe','pipe'] });
let sequence = 0, browser, page, failure;
const waiting = new Map(), calls = [], errors = [], nativeLog = [];
const diagnostic=setInterval(async()=>{console.log('Native stage:',calls.length,calls.at(-1)?.method||calls.at(-1)?.action);if(page)console.log(await page.evaluate(()=>{const v=window.fixture?.view,s=v?.fiveSimulation;return s?{playing:s.playing,cursor:s.cursor,message:s.message,headError:s.headError,hidden:document.hidden,pending:v.pending,checking:s.checking,progress:s.checkProgress,task:s.collisionTask?.status,report:s.collisionReport?.status}:{};}).catch(()=>({})));},30000);
const failAll = error => { for (const entry of waiting.values()) { clearTimeout(entry.timer); entry.reject(error); } waiting.clear(); };
processHandle.on('error', failAll);
processHandle.on('exit', code => { if (waiting.size) failAll(Error(`Native bridge exited ${code}`)); });
processHandle.stderr.on('data', bytes => { nativeLog.push(String(bytes)); if(process.env.ICAX_COLLISION_PROFILE)console.log(String(bytes)); });
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
  const info = await rpc({ action: 'info' });let candidates=[];
  info.contactOnly=Boolean(process.env.ICAX_SIMULATION_CONTACT_ONLY);
  if(process.env.ICAX_SIMULATION_PROJECT)Object.assign(info,await rpc({action:'open-project',path:resolve(process.env.ICAX_SIMULATION_PROJECT)}));
  if(process.env.ICAX_SIMULATION_IMPORT)await prepareDiscreteScene(rpc,process.env.ICAX_SIMULATION_IMPORT,output);
  info.sourcePath=resolve(root,'TestData/automotive-stamping/mercedes-benz-t-node/Test-support-block.step');
  const domain=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  if(info.contactOnly||process.env.ICAX_RAPID_206||process.env.ICAX_RAPID_MANUAL)info.initialDomain=domain;
  const setup=domain.setups.find(s=>s.id===domain.activeSetupId);
  if(setup.generatedGeometry.transitions.some(t=>t.frame!=='Setup')) {
    let rapidTask=await rpc({action:'invoke',scope:'scene',method:'CamRapid.Generate',payload:{commandId:crypto.randomUUID(),expected:domain.expected,setupId:setup.id,geometryRevision:setup.generatedGeometry.revision}});
    while(rapidTask.status==='Running'){await new Promise(r=>setTimeout(r,100));rapidTask=await rpc({action:'invoke',scope:'scene',method:'CamRapid.QueryStatus',payload:{epoch:rapidTask.epoch,taskId:rapidTask.taskId}});}
    assert.equal(rapidTask.status,'Completed',rapidTask.error);
    await rpc({action:'export-project',path:resolve(output,'Automotive-simulation.i5cam')});
  }
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ ...(process.env.ICAX_BROWSER_EXECUTABLE ? {executablePath:process.env.ICAX_BROWSER_EXECUTABLE} : {channel:process.env.ICAX_BROWSER_CHANNEL || 'msedge'}), headless: true, args:['--disable-gpu'] });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.setDefaultTimeout(180000);
  page.on('pageerror', error => {errors.push(error.message);console.error(error.stack);});
  page.on('console',message=>{if(message.type()==='error')console.error('Browser:',message.text());});
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
    if(info.contactOnly){const {setToolAxisDisplayVisible}=await import('/src/apps/five-axis-cam/webpage/toolAxisDisplay.mjs');setToolAxisDisplayVisible(view,false);}
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
    if(info.initialDomain){const {mergeDomainState}=await import('/src/apps/five-axis-cam/webpage/domainActions.mjs');const {hydratePathSamples}=await import('/src/apps/five-axis-cam/webpage/pathSampleResource.mjs');await hydratePathSamples(info.initialDomain,sceneProxy);view.scene??={};mergeDomainState(view.scene,info.initialDomain);}
    sceneProxy.views = new ViewClient(sceneProxy);
    const context = f.context = { mount: document.querySelector('#app'), sceneProxy,
      project: { projectId: info.projectId, projectName: '汽车冲压件 · 轨迹仿真' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog() { ++f.fileSelections; return info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) { return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const field=name=>page.locator(`[data-five-simulation-field="${name}"]`);
  const button=name=>page.locator(`[data-five-command="five.simulation.${name}"]`);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const cursor=()=>page.evaluate(()=>fixture.view.fiveSimulation.cursor);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const head=s=>s.setups.find(t=>t.id===s.activeSetupId).headModel;
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  if(process.env.ICAX_RAPID_MANUAL){await verifyManualRapids({page,command,read,rpc,output,info,errors});}
  else if(process.env.ICAX_RAPID_206){await verifyRapid206({page,command,read,rpc,output,info});}
  else if(process.env.ICAX_RAPID_PERFORMANCE_ONLY){await verifyRapidPerformance({page,command,read,rpc,output});}
  else {
  await command('five.tool.simulation');await command('view.fit');
  await page.waitForFunction(()=>fixture.view.fiveSimulation.frames.length>10&&!!fixture.view.fiveSimulation.group);
  assert.equal(await page.locator('.five-simulation-bar').isVisible(),true);
  assert.equal(await page.getByRole('button',{name:'切割头模型',exact:true}).isVisible(),true);
  const count=await page.evaluate(()=>fixture.view.fiveSimulation.frames.length);console.log('Simulation frames:',count);
  if(process.env.ICAX_SIMULATION_CONTACT_ONLY){
    await verifyDiscretePlayback({page,button,field,read,command,idle,calls,errors,count,output});
  }
  else if(process.env.ICAX_SIMULATION_TRACE_ONLY){await verifySimulationTrace({page,command,field,button,idle,read,output});}
  else {
  // Checking is cancellable and does not publish a partial green result.
  await command('five.tool.collision');
  await button('collision-check').click();await page.waitForFunction(()=>!!fixture.view.fiveSimulation.cancelCheck);
  await button('collision-cancel').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.collisionReport),null);
  await command('five.tool.simulation');
  assert.equal(await cursor(),0);await button('next').click();assert.equal(await cursor(),1);await button('previous').click();assert.equal(await cursor(),0);
  const builtinStarted=performance.now();
  await field('speed').selectOption('0.25');await button('play').click();await page.waitForFunction(()=>fixture.view.fiveSimulation.cursor>0.1);await button('pause').click();
  const builtinCheckMs=Math.round(performance.now()-builtinStarted);
  assert.equal((await read()).setups[0].simulationCheck.validity,'Missing','playback does not publish a full-path check');
  await command('five.tool.collision');await button('collision-check').click();await idle();
  const initialReport=(await read()).setups[0].simulationCheck;assert.equal(initialReport.validity,'Current');
  await command('five.tool.simulation');
  assert.equal(initialReport.frames.length,count);assert.equal(initialReport.segments.length,count-1);
  console.log('Builtin collision result:',initialReport.collisionCount,'collisions,',initialReport.uncheckedCount,'unchecked');
  assert.ok(await page.evaluate(()=>!!fixture.view.fiveSimulation.collisionReport));
  const paused=await cursor();await page.waitForTimeout(200);assert.equal(await cursor(),paused);
  const seek=async n=>{await field('seek').fill(String(n));await field('seek').dispatchEvent('input');};
  const rapid=await page.evaluate(()=>fixture.view.fiveSimulation.frames.findIndex(f=>f.role==='Rapid'));
  assert.ok(rapid>0);await seek(rapid+1);
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.group.userData.laser),false);
  const actual=await page.evaluate(()=>{const s=fixture.view.fiveSimulation;return {display:s.group.position.toArray(),source:s.frames[Math.floor(s.cursor)].position.toArray(),rotation:s.group.quaternion.toArray(),expected:s.frames[Math.floor(s.cursor)].rotation.toArray()};});
  assert.deepEqual(actual.display,actual.source);assert.deepEqual(actual.rotation,actual.expected);
  await button('head-toggle').click();assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.group.visible),false);
  await button('head-toggle').click();assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.group.visible),true);
  await field('speed').selectOption('5');assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.speed),5);
  await seek(count-1);assert.equal(await button('next').isDisabled(),true);await button('play').click();await page.waitForFunction(()=>fixture.view.fiveSimulation.cursor<10);await button('pause').click();
  await page.waitForFunction(()=>fixture.view.viewport.getVisibleBounds()!=null);await command('view.fit');
  await seek(0);await button('collision-next').click();await page.screenshot({path:resolve(output,'automotive-collision-builtin.png')});
  const displayIndex=await page.evaluate(()=>fixture.view.fiveSimulation.frames.findIndex(f=>f.order==='6'&&f.role==='Cut'));
  await seek(displayIndex);await page.screenshot({path:resolve(output,'automotive-trajectory-playback.png')});
  await button('head-open').click();await page.screenshot({path:resolve(output,'automotive-head-controls.png')});
  console.log('Head import: synthetic STEP calibration fixture');
  const before=await read();await button('head-import').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.headError),'');assert.equal(head(await read()).kind,'Imported');
  assert.equal(await page.evaluate(()=>!!fixture.view.fiveSimulation.group.getObjectByName('imported-cutting-head')),false);
  // Lost committed reply must reuse exactly the accepted command.
  await field('tcp0').fill('5');await field('tcp1').fill('3');await field('tcp2').fill('10');
  await field('beam0').fill('0');await field('beam1').fill('-2');await field('beam2').fill('0');
  await page.evaluate(()=>fixture.loseMethod='CamHead.Configure');await button('head-apply').click();await idle();
  assert.ok(await page.evaluate(()=>fixture.view.fiveSimulation.request));await button('head-apply').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.headError),'');
  await page.waitForFunction(()=>!!fixture.view.fiveSimulation.group?.getObjectByName('imported-cutting-head'));
  const calibrated=head(await read());assert.deepEqual(calibrated.tcpLocalPosition,[5,3,10]);assert.deepEqual(calibrated.beamLocalDirection,[0,-1,0]);
  const after=await read();assert.deepEqual(before.workpieces,after.workpieces);assert.deepEqual(before.fixtures,after.fixtures);
  assert.deepEqual(before.setups[0].generatedGeometry,after.setups[0].generatedGeometry);
  assert.equal(after.setups[0].simulationCheck.validity,'Stale');
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.collisionReport),null);
  await seek(20);
  const calibration=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const s=fixture.view.fiveSimulation, mesh=s.group.getObjectByName('imported-cutting-head');s.group.updateMatrixWorld(true);
    const tcp=mesh.localToWorld(new THREE.Vector3(5,3,10));const beam=new THREE.Vector3(0,-1,0).transformDirection(mesh.matrixWorld);
    return {tcp:tcp.toArray(),display:s.group.position.toArray(),beam:beam.toArray(),expected:s.frames[20].direction.clone().negate().toArray()};
  });
  assert.ok(calibration.tcp.every((v,i)=>Math.abs(v-calibration.display[i])<1e-6));assert.ok(calibration.beam.every((v,i)=>Math.abs(v-calibration.expected[i])<1e-6));
  await button('head-close').click();
  await command('five.tool.collision');
  const importedStarted=performance.now();await button('collision-check').click();await idle();
  const importedCheckMs=Math.round(performance.now()-importedStarted);
  const importedReport=(await read()).setups[0].simulationCheck;
  assert.equal(importedReport.validity,'Current');assert.equal(importedReport.scope,'ImportedHeadWorkpieceAndFixtures');
  assert.equal(importedReport.status,'Collision');assert.ok(Number(importedReport.collisionCount)>0);
  assert.ok(importedReport.segments.some(r=>r.status==='Collision'&&importedReport.frames[Number(r.index)].role==='Cut'));
  await command('five.tool.simulation');
  assert.ok(await page.evaluate(()=>!!fixture.view.viewport.scene.getObjectByName('collision-Collision')));
  await seek(0);await button('collision-next').click();
  assert.ok(await cursor()>0);assert.ok((await page.locator('[data-five-playback-role]').textContent()).includes('碰撞'));
  await page.screenshot({path:resolve(output,'automotive-head-collision.png')});
  console.log('Imported collision result:',importedReport.collisionCount,'collisions,',importedReport.uncheckedCount,'unchecked');
  await rpc({action:'export-project',path:resolve(output,'Automotive-collision.i5cam')});
  await button('collision-previous').click();
  await button('head-open').click();
  await rpc({action:'undo'});await command('five.refresh');assert.equal(head(await read()).calibrated,false);
  await rpc({action:'redo'});await command('five.refresh');assert.deepEqual(head(await read()),calibrated);
  // Patching during a delayed response preserves latest focus/selection, nested
  // scroll, event listeners and the WebGL canvas while editing calibration.
  await page.addStyleTag({content:'.five-head-popover{max-height:230px}.cam-context-pane{max-height:240px}.five-cut-tree{max-height:100px;overflow:auto}'});
  await page.evaluate(()=>{fixture.savedCanvas=document.querySelector('canvas');fixture.savedInput=document.querySelector('[data-five-simulation-field="tcp0"]');fixture.inputEvents=0;fixture.savedInput.addEventListener('input',()=>fixture.inputEvents++);fixture.holdMethod='CamScene.ReadState';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');});
  await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const input=fixture.savedInput;input.focus({preventScroll:true});input.setSelectionRange(0,1);const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.five-cut-tree'),document.querySelector('.five-head-popover')];nodes.forEach((n,i)=>n.scrollTop=20+i*12);return nodes.map(n=>n.scrollTop);});
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.savedCanvas===document.querySelector('canvas')&&fixture.savedInput===document.querySelector('[data-five-simulation-field="tcp0"]')),true);
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane'),document.querySelector('.five-cut-tree'),document.querySelector('.five-head-popover')].map(n=>n.scrollTop)),scroll);
  assert.deepEqual(await field('tcp0').evaluate(n=>[document.activeElement===n,n.selectionStart,n.selectionEnd]),[true,0,1]);
  await field('tcp0').fill('6');assert.ok(await page.evaluate(()=>fixture.inputEvents>0));
  await field('beam0').fill('0');await field('beam1').fill('0');await field('beam2').fill('0');await button('head-apply').click();await idle();
  assert.ok((await page.evaluate(()=>fixture.view.fiveSimulation.headError)).includes('不能为零'));assert.deepEqual(head(await read()),calibrated);
  await rpc({action:'save-reopen'});await command('five.refresh');assert.deepEqual(head(await read()),calibrated);
  assert.equal((await read()).setups[0].simulationCheck.validity,'Current');
  assert.equal((await read()).setups[0].simulationCheck.collisionCount,importedReport.collisionCount);
  await page.waitForFunction(()=>!!fixture.view.fiveSimulation.group?.getObjectByName('imported-cutting-head'));
  // Leaving CAM playback releases the animation and transient model.
  await button('head-close').click();await button('play').click();await command('five.tool.rapid');
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.playing),false);assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.group),null);
  await command('five.tool.simulation');await button('head-open').click();await button('head-builtin').click();await idle();
  assert.equal(head(await read()).kind,'Builtin');
  assert.equal((await read()).setups[0].simulationCheck.validity,'Stale');
    await command('five.tool.collision');
    const threshold=(await read()).setups[0].rapidSettings.collisionThresholdMm;
  await field('threshold').fill('-1');await button('threshold-apply').click();await idle();assert.equal((await read()).setups[0].rapidSettings.collisionThresholdMm,threshold);
  await field('threshold').fill(String(threshold+.05));await button('threshold-apply').click();await idle();assert.equal((await read()).setups[0].rapidSettings.collisionThresholdMm,threshold+.05);
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'simulation-report.json'),JSON.stringify({passed:true,frames:count,builtin:{collisionSegments:initialReport.collisionCount,uncheckedSegments:initialReport.uncheckedCount,checkMs:builtinCheckMs},importedCheckMs,collisionSegments:importedReport.collisionCount,uncheckedSegments:importedReport.uncheckedCount,cases:['play pause previous next seek speed and end restart','generated cut and rapid TCP positions and orientation','laser off during rapid','head model controls beside playback','visibility toggle','native STEP import and calibration','lost response idempotent retry','TCP and emitted beam world transform','undo redo embedded save reopen','canvas input event selection and latest nested scroll retained','invalid zero beam rejected','tool switch stops animation and frees head','cancelled collision check never publishes partial results','play checks only the displayed mesh pose','calibration invalidates prior collision results','imported BRep cutting segment collisions shown red','previous next collision navigation','collision report persists through reopen','editable collision threshold'],coverage:{native:true,browser:'Edge',standaloneCef:false,headImportFixture:'support block STEP, not a qualified cutting head',machineMotion:false,headBrepCollision:true}},null,2));
  }
  }
} catch(error) {
  await page?.screenshot({path:resolve(output,'simulation-failed.png')}).catch(()=>{});
  writeFileSync(resolve(output,'failure.json'),JSON.stringify({error:error.stack,errors,nativeLog:nativeLog.slice(-30),calls:calls.slice(-8)},null,2));throw error;
} finally { clearInterval(diagnostic);await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end(); }
