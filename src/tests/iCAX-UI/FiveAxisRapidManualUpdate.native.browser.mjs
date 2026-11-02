import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('manual-rapid-update-20261029/browser');
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
  const timer = setTimeout(() => { waiting.delete(id); reject(Error(`Native timeout ${message.action}`)); }, 120000);
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


  await rpc({action:'prepare-axis-test'});
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>rpc({action:'read-state'}),geometry=s=>s.setups.find(x=>x.id===s.activeSetupId).generatedGeometry;
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveRapid?.running&&!fixture.view.fiveRapid?.regenerating&&!fixture.view.fiveCompensation?.loading);
  const mutate=async(method,fields)=>{const s=await read();return invoke(method,{commandId:crypto.randomUUID(),expected:s.expected,...(!method.startsWith('CamCut.')?{setupId:s.activeSetupId}:{}),...fields});};
  const wait=async task=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,30));task=await invoke('CamRapid.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
  const ordered=await read();await mutate('CamPlan.SetOrder',{mode:'Manual',intentIds:geometry(ordered).operations.map(o=>o.intentId)});
  await command('five.refresh');await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.cam-page.rapid');await idle();
  let s=await read(),t=geometry(s).transitions[0],pair=`${t.from}/${t.to}`;
  const request={expected:s.expected,setupId:s.activeSetupId,geometryRevision:geometry(s).revision,from:t.from,to:t.to,policy:t.policy};
  const source=await invoke('CamRapid.ReadEditPath',request),points=source.points.slice(1,-1),directions=source.directions.slice(1,-1);
  points[0]=[points[0][0],17,23];directions[0]=[0,.1,Math.sqrt(.99)];const manual={points,directions};
  await mutate('CamRapid.EditPath',{...request,manualPath:manual});
  const checked=await wait(await mutate('CamRapid.Preview',{geometryRevision:geometry(await read()).revision,from:t.from,to:t.to,policy:t.policy,manualPath:manual}));
  await mutate('CamRapid.Apply',{taskId:checked.taskId});
  await command('five.refresh');await command('five.cam-page.rapid');await page.locator('[data-five-rapid-field="connection"]').selectOption(pair);await idle();
  const current=geometry(await read()).transitions.find(t=>`${t.from}/${t.to}`===pair),revisionCount=calls.length;
  await mutate('CamCut.Reverse',{intentIds:[t.from]});await command('five.refresh');await idle();
  const unchanged=geometry(await read()).transitions.find(t=>`${t.from}/${t.to}`===pair);
  assert.equal(unchanged.endpointStatus,'Current');assert.equal(unchanged.geometryStatus,'Generated');assert.deepEqual(unchanged.points,current.points);assert.deepEqual(unchanged.manualPath,manual);
  assert.equal(await page.locator('[data-five-command="five.rapid.update"]').count(),0);
  assert.doesNotMatch(await page.locator('.cam-info-pane').innerText(),/相邻切割已变化|工程已变化|待更新/);
  assert.equal(await page.locator('[data-five-command="five.rapid.preview"]').isEnabled(),true,'a different scene sequence cannot disable this unchanged manual route');
  assert.ok(!calls.slice(revisionCount).some(c=>c.method==='CamPlan.Generate'||c.method==='CamRapid.Generate'));
  const shifted=await mutate('CamCut.Transform',{intentIds:[t.from],matrix:[1,0,0,.5,0,1,0,0,0,0,1,0,0,0,0,1]});await wait(shifted);
  await command('five.refresh');await command('five.cam-page.rapid');await page.locator('[data-five-rapid-field="connection"]').selectOption(pair);await idle();
  const preserved=await read(),stale=geometry(preserved).transitions.find(t=>`${t.from}/${t.to}`===pair);
  assert.equal(stale.geometryStatus,'Stale');assert.deepEqual(stale.manualPath,manual);assert.deepEqual(stale.points,source.points.map((p,i)=>i===1?points[0]:p));
  const displayed=await page.evaluate(()=>{const v=fixture.view,t=v.scene.domain.setups.find(x=>x.id===v.scene.domain.activeSetupId).generatedGeometry.transitions.find(t=>`${t.from}/${t.to}`===v.fiveRapid.id);return {t,line:!!v.fiveGeneratedPreview?.group.children.find(n=>n.userData.rapidTransitionId===v.fiveRapid.id)};});
  assert.deepEqual(displayed.t.points,stale.points,'native resource hydration retains the authored stale route');assert.equal(displayed.line,true);
  assert.match(await page.locator('.cam-context-pane').innerText(),/手动路径已保留/);
  const button=page.locator('[data-five-command="five.rapid.update"]');assert.equal(await button.isEnabled(),true);assert.equal(await page.locator('[data-five-command="five.rapid.regenerate"]').count(),0);
  await page.screenshot({path:resolve(output,'manual-path-retained-before-update.png')});
  await page.locator('[data-five-command="five.rapid.path-edit"]' ).click();await idle();
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.Generate';fixture.hold=true;fixture.rapidInput=document.querySelector('.cam-info-pane [data-five-rapid-field="pathY"]');fixture.rapidCanvas=document.querySelector('canvas');fixture.rapidEvents=0;fixture.rapidInput.addEventListener('input',()=>fixture.rapidEvents++);});
  const style=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-rapid-body::after{content:"";display:block;height:1600px}.cam-context-pane,.cam-info-pane,.five-rapid-body{height:200px!important;max-height:200px!important;overflow-y:auto!important;display:block!important}'});
  await page.evaluate(()=>{fixture.rapidInput.focus({preventScroll:true});fixture.rapidInput.setSelectionRange(0,2);['.cam-context-pane','.cam-info-pane','.five-rapid-body'].forEach((s,i)=>document.querySelector(s).scrollTop=40+i*20);});
  const scroll=await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop+=25;return n.scrollTop;}));assert.ok(scroll.every(x=>x>0));
  // The production command intentionally blocks the page while publishing.
  // Keep the pre-command input focus, which its modal must restore afterwards.
  const count=calls.length;await button.dispatchEvent('click');await page.waitForFunction(()=>fixture.held);await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const kept=await page.evaluate(()=>({input:fixture.rapidInput===document.querySelector('.cam-info-pane [data-five-rapid-field="pathY"]'),canvas:fixture.rapidCanvas===document.querySelector('canvas'),focus:document.activeElement===fixture.rapidInput,selection:[fixture.rapidInput.selectionStart,fixture.rapidInput.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop)}));
  assert.ok(kept.input&&kept.canvas&&kept.focus,JSON.stringify(kept));assert.deepEqual(kept.selection,[0,2]);assert.deepEqual(kept.scroll,scroll);
  await page.evaluate(()=>fixture.rapidInput.dispatchEvent(new Event('input')));assert.equal(await page.evaluate(()=>fixture.rapidEvents),1);await style.evaluate(n=>n.remove());
  const after=await read(),actual=geometry(after).transitions.find(t=>`${t.from}/${t.to}`===pair);
  assert.equal(actual.geometryStatus,'Generated');assert.equal(actual.validation,'Clear');assert.equal(actual.planningMethod,'Manual');assert.deepEqual(actual.manualPath,manual);
  assert.deepEqual(actual.points.slice(1,-1),manual.points);assert.deepEqual(actual.directions.slice(1,-1),manual.directions);
  assert.deepEqual(after.intentToolpaths,preserved.intentToolpaths);assert.deepEqual(after.fixtures,preserved.fixtures);assert.deepEqual(after.placements,preserved.placements);
  const updates=calls.slice(count).filter(c=>c.action==='invoke'),preview=updates.filter(c=>c.method==='CamRapid.Preview');assert.equal(preview.length,1);assert.deepEqual(preview[0].payload.manualPath,manual);assert.equal(updates.filter(c=>c.method==='CamRapid.Apply').length,1);assert.ok(!updates.some(c=>c.method==='CamRapid.Generate'||c.method==='CamRapid.EditPolicy'));
  assert.ok(!calls.some(c=>c.method==='CamProcess.BeginPreview'&&c.payload?.kind==='Compensation'),'zero kerf must not start compensation geometry at any point');
  assert.ok(!await page.evaluate(()=>fixture.logs.some(x=>x.some(y=>String(y).includes('刀补预览失败')))));
  await page.screenshot({path:resolve(output,'manual-connection-updated.png')});
  // A native rejected check keeps every saved manual point and never applies.
  const bad={points:[[6,6,source.points[0][2]-1]],directions:[[0,0,1]]};
  s=await read();t=geometry(s).transitions.find(t=>`${t.from}/${t.to}`===pair);
  await mutate('CamRapid.EditPath',{geometryRevision:geometry(s).revision,from:t.from,to:t.to,policy:t.policy,manualPath:bad});
  await command('five.refresh');await command('five.cam-page.rapid');await page.locator('[data-five-rapid-field="connection"]').selectOption(pair);await idle();
  const blockedCount=calls.length;await page.locator('[data-five-command="five.rapid.preview"]').click();await page.waitForFunction(()=>!!fixture.view.fiveRapid?.preview||!!fixture.view.error);await idle();
  assert.match(await page.evaluate(()=>fixture.view.error),/手动路径.*碰撞/);
  assert.deepEqual(geometry(await read()).transitions.find(t=>`${t.from}/${t.to}`===pair).manualPath,bad);
  assert.ok(!calls.slice(blockedCount).some(c=>c.method==='CamRapid.Apply'||c.method==='CamRapid.Generate'));
  assert.equal(await page.locator('[data-five-command="five.rapid.path-edit"]').isEnabled(),true);
  // Filtering zero kerfs must still admit an actually applied compensation.
  s=await read();const cut=s.intentToolpaths.find(c=>c.machiningKind==='Cut');
  await invoke('CamCut.SetRetainedSide',{commandId:crypto.randomUUID(),expected:s.expected,intentIds:[cut.id],retainedSide:cut.boundaryRole==='OuterTrim'?'Interior':'Exterior'});
  s=await read();const compensationStart=calls.length;
  await invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:s.expected,intentIds:[cut.id],patch:{kerf:{widthMm:.24}}});
  await command('five.refresh');await idle();
  assert.equal((await read()).intentToolpaths.find(c=>c.id===cut.id).kerfWidthMm,.24);
  const compensation=calls.slice(compensationStart).filter(c=>c.method==='CamProcess.BeginPreview'&&c.payload.kind==='Compensation');
  assert.ok(compensation.length>0);assert.ok(compensation.every(c=>c.payload.intentIds.length===1&&c.payload.intentIds[0]===cut.id));
  s=await read();await invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:s.expected,intentIds:[cut.id],patch:{kerf:{widthMm:0}}});
  const clearedStart=calls.length;await command('five.refresh');await idle();
  assert.ok(!calls.slice(clearedStart).some(c=>c.method==='CamProcess.BeginPreview'&&c.payload.kind==='Compensation'));
  assert.deepEqual(errors,[]);
  const report={passed:true,native:true,browser:'Edge',standaloneCef:false,manualPath:manual,preserved:kept,cases:['changed scene and cut revisions leave the unchanged physical endpoint connection current without regeneration','stale native manual geometry remains visible through ICPS transport after an actual endpoint movement','manual-only update control without regenerate','exact manual points and tool axes retained','one fixed-route preview and apply without automatic search','focus selection canvas listeners and dual/nested scroll retained across the blocking update','zero compensation never starts a compensation preview','failed collision check retains saved manual data without apply','actual nonzero compensation still previews only its affected path; clearing it stops preview requests']};
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}catch(error){failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));if(page){console.error(await page.evaluate(()=>({error:fixture?.view?.error,rapid:fixture?.view?.fiveRapid?.id,preview:fixture?.view?.fiveRapid?.preview?.result})));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}throw error;}
finally{writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();}





