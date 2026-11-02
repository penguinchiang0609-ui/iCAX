import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { verifyRegistrationSnapping } from './FiveAxisRegistrationSnapping.native.browser.mjs';
import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('five-axis-registration-browser');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root, 'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, 'FiveAxisCAMTest.exe'),
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
    const { serializeVariantText } = await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs');
    const entry = await import('/src/apps/five-axis-cam/webpage/entry.mjs');
    const { ViewClient } = await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const view = getProjectView(info.projectId);
    const f = window.fixture = { entry, view, fileSelections: 0, logs: [], hold: false, held: false, release: null };
    const sceneProxy = { sceneId: info.sceneId, state: { sceneId: info.sceneId }, pdo: { enabled: false },
      async invoke(method, payload = {}) {
        if (method === (f.holdMethod || 'CamScene.ReadState') && f.hold) { f.hold = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false; }
        const result = await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payload, payloadText:serializeVariantText(payload) });
        if (method === 'CamPlacement.ApplyRegistration' && f.holdAfterApply) {
          f.holdAfterApply = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false;
        }
        if (method === 'CamPlacement.ApplyRegistration' && f.dropApply) { f.dropApply = false; throw Error('Injected lost apply receipt'); }
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
      project: { projectId: info.projectId, projectName: '工件坐标系与实测校准 · 原生验收' }, activeRibbonTabId: 'workpiece',
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
  const refresh = () => page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  const command = id => page.evaluate(id => fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle = () => page.waitForFunction(() => !fixture.view.pending);
  const draft = () => page.evaluate(async () => (await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs')).currentDraft(fixture.view));
  const setPoint = async (prefix, values) => { for(const [i,axis] of [...'XYZ'].entries()) await page.locator(`[data-five-field="${prefix}${axis}"]`).fill(String(values[i])); };
  const near = (actual,expected,tolerance=1e-7) => { assert.equal(actual.length,expected.length); actual.forEach((v,i)=>assert.ok(Math.abs(v-expected[i])<tolerance,`${actual} != ${expected}`)); };
  await selectWorkpieceFile(page, info.sourcePath);
  await page.waitForFunction(() => !fixture.view.pending && fixture.view.viewport?.getDebugState()?.objectCount===1);
  const original = await invoke('CamScene.ReadState');
  const placementId = original.placements[0].id;
  const screenPoint = async localPoint => page.evaluate(({localPoint,placementId}) => {
      const viewport=fixture.view.viewport,object=viewport.sceneObjects.get(placementId);
      const point=object.localToWorld(viewport.camera.position.clone().set(...localPoint)).project(viewport.camera);
      const rect=viewport.renderer.domElement.getBoundingClientRect();
      return {x:rect.x+(point.x+1)*rect.width/2,y:rect.y+(1-point.y)*rect.height/2};
    },{localPoint,placementId});
  const exposedPoint=async localPoint=>{
    let p=await screenPoint(localPoint);
    if(!await page.evaluate(p=>document.elementFromPoint(p.x,p.y)?.tagName==='CANVAS',p)){
      await page.evaluate(()=>fixture.view.viewport.fitView(2));
      await page.keyboard.down('Control');await page.mouse.move(800,700);await page.mouse.down({button:'middle'});await page.mouse.move(960,700,{steps:8});await page.mouse.up({button:'middle'});await page.keyboard.up('Control');p=await screenPoint(localPoint);
    }return p;
  };
  const hoverModel = async localPoint => {
    const p=await exposedPoint(localPoint);await page.mouse.move(p.x,p.y);
    try { await page.waitForFunction(localPoint=>{
      const h=fixture.view.fiveRegistrationInteraction?.hover;
      return h && Math.hypot(...h.query.nearPoint.map((v,i)=>v-localPoint[i]))<.3;
    },localPoint); } catch(error) {
      const diagnostic=await page.evaluate(()=>{const s=fixture.view.fiveRegistrationInteraction;return {hover:s?.hover,error:s?.error,mapError:s?.mapError,busy:s?.busy,latest:s?.latest?.request,pick:fixture.view.fiveAxisRegistrationPick?.mode,pending:fixture.view.pending};});
      throw Error(`${error.message}\n${JSON.stringify({localPoint,diagnostic})}`);
    }
    assert.equal(await page.locator('.five-reference-prompt').getAttribute('data-state'),'ready');
  };
  const clickModel = async localPoint => {
    const p=await exposedPoint(localPoint);
    await page.mouse.click(p.x,p.y); await idle();
  };
  const selected = prefix => page.waitForFunction(prefix => {
    const ds=Object.values(fixture.view.fiveAxisUI.drafts); return ds.some(d=>d.referenceQueries?.[prefix]);
  },prefix);
  const open = async (tool,method) => { await command(method ? `five.rough.${method}` : `five.tool.${tool}`); await page.locator('[role="dialog"]').waitFor(); };
  const confirm = async tool => { await page.locator(`[data-five-command="five.registration.confirm.${tool}"]`).click(); await idle(); };
  const cancel = () => page.locator('[data-five-command="five.registration.cancel"]').last().click();
  const snapping = await verifyRegistrationSnapping({ page, command, invoke, screenPoint, cancel, output });
  writeFileSync(resolve(output, 'snapping-report.json'), JSON.stringify({ passed: true, cases: snapping, standaloneCef: false }, null, 2));
  cases.push('point/line/plane snap eight pixels outside the mesh at two zooms; exact hover/click agreement; far blank and stale replies rejected');
  // Reproduce the user's three-point mode: the whole CAD face must react even
  // before the exact point query returns, and follow the pointer between faces.
  await page.evaluate(()=>fixture.view.viewport.fitView(1.35));
  await open('rough','point');
  await page.waitForFunction(()=>fixture.view.fiveRegistrationInteraction?.faces?.length>0);
  const visibleFaces=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const v=fixture.view,vp=v.viewport,source=vp.sceneObjects.get(v.scene.domain.placements[0].id),r=vp.renderer.domElement.getBoundingClientRect();
    const ray=new THREE.Raycaster(),found=new Map();source.updateMatrixWorld(true);
    for(let x=400;x<r.width-100;x+=20)for(let y=90;y<r.height-120;y+=20){
      ray.setFromCamera(new THREE.Vector2(x/r.width*2-1,1-y/r.height*2),vp.camera);
      const hit=ray.intersectObject(source,false)[0];if(!hit)continue;
      const face=v.fiveRegistrationInteraction.faces.find(f=>hit.faceIndex>=f.first&&hit.faceIndex<f.first+f.count);
      if(face&&!found.has(face.faceId))found.set(face.faceId,{x:r.x+x,y:r.y+y,faceId:face.faceId,count:face.count});
    }
    return [...found.values()];
  });
  assert.ok(visibleFaces.length>=2,'two visible BRep faces for a real pointer transition');
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.ResolveReference';fixture.hold=true;});
  await page.mouse.move(visibleFaces[0].x,visibleFaces[0].y);
  await page.waitForFunction(()=>fixture.held);
  const assertFace=async target=>{
    await page.waitForFunction(id=>fixture.view.fiveRegistrationInteraction?.previewFace?.faceId===id,target.faceId);
    const feedback=await page.evaluate(()=>{
      const s=fixture.view.fiveRegistrationInteraction,o=s.group.children.find(o=>o.userData.registrationHover);
      return {faceId:o?.userData.registrationFace,triangles:o?.geometry.getAttribute('position').count/3,resolved:!!s.hover};
    });
    assert.equal(feedback.faceId,target.faceId);assert.equal(feedback.triangles,target.count);assert.equal(feedback.resolved,false);
  };
  await assertFace(visibleFaces[0]);
  await page.screenshot({path:resolve(output,'point-mode-face-hover.png')});
  await page.mouse.move(visibleFaces[1].x,visibleFaces[1].y);
  await assertFace(visibleFaces[1]);
  await page.evaluate(()=>fixture.release());
  await page.waitForFunction(id=>fixture.view.fiveRegistrationInteraction?.hover?.faceId===id,visibleFaces[1].faceId);
  await page.mouse.move(1350,80);
  await page.waitForFunction(()=>!fixture.view.fiveRegistrationInteraction?.previewFace);
  assert.deepEqual(await invoke('CamScene.ReadState'),original);
  await cancel();
  cases.push('three-point mode: whole face highlights before native snap returns, follows adjacent faces, ignores late reply and clears on exit without mutation');
  await page.evaluate(() => { fixture.view.viewport.setStandardView('top'); fixture.view.viewport.fitView(2); window.registrationCanvas=document.querySelector('canvas'); });
  // Keep all three corners clear of the floating coordinate dialog using the
  // same camera pan available to a user while picking.
  await page.keyboard.down('Control');await page.mouse.move(800,700);await page.mouse.down({button:'middle'});await page.mouse.move(960,700,{steps:8});await page.mouse.up({button:'middle'});await page.keyboard.up('Control');
  for(const mode of ['point','line','face']) {
    await open('rough',mode);
    await page.getByRole('button',{name:'端点',exact:true}).click();
    const order=mode==='point'?['origin','axisX','plane']:mode==='line'?['axisX','plane','origin']:['plane','origin','axisX'];
    const points=mode==='point'?[[.15,.15,20],[39.85,.15,20],[.15,59.85,20]]:mode==='line'?[[20,.2,20],[.2,30,20],[.15,.15,20]]:[[20,30,20],[.15,.15,20],[39.85,.15,20]];
    const before=await invoke('CamScene.ReadState');
    for(let i=0;i<3;i++) {
      assert.equal(await page.evaluate(()=>fixture.view.fiveAxisRegistrationPick?.prefix),order[i]);
      await hoverModel(points[i]);
      assert.equal(await page.evaluate(()=>{
        const s=fixture.view.fiveRegistrationInteraction;
        return s.group.children.some(o=>o.userData.registrationHover&&o.userData.registrationFace===s.hover.faceId);
      }),true,'point, edge and plane picking all highlight the underlying CAD face');
      if(i===0)await page.screenshot({path:resolve(output,`coordinate-${mode}-hover.png`)});
      await clickModel(points[i]); await selected(order[i]);
      const feedback=await page.evaluate(()=>{
        const group=fixture.view.fiveRegistrationInteraction.group,children=[];
        group.traverse(o=>{if(o.userData.registrationFace)children.push('face');if(o.userData.registrationEdge)children.push('line');if(o.isPoints)children.push('point');});return children;
      });
      assert.ok(feedback.includes(mode==='face'&&i===0?'face':mode==='line'&&i<2?'line':'point'));
      if(mode==='line'&&i===0) {
        assert.deepEqual(await page.locator('.five-reference-row>span').allTextContents(),['X 轴直线','XY 面内直线','原点']);
        // Reproduce the screenshot: X is selected, origin is filled, and the
        // user clicks a face while the second straight reference is active.
        await page.locator('[data-five-command="five.reference.pick.origin"]').click();
        await clickModel([.15,.15,20]);await selected('origin');
        await page.locator('[data-five-command="five.reference.pick.plane"]').click();
        const saved=(await draft()).referenceResults;
        await clickModel([20,30,20]);
        assert.match(await page.locator('.five-position-error').innerText(),/XY 平面内另一根不平行直线/);
        assert.doesNotMatch(await page.locator('.five-position-error').innerText(),/圆边/);
        assert.equal(await page.evaluate(()=>fixture.view.fiveAxisRegistrationPick.prefix),'plane');
        assert.deepEqual((await draft()).referenceResults,saved,'an invalid face click retains the chosen X line and origin');
        await page.screenshot({path:resolve(output,'line-second-reference-prompt.png')});
        await page.locator('[data-five-command="five.reference.pick.plane"]').click();
        assert.equal(await page.locator('.five-position-error').count(),0,'retrying a reference clears the previous pick error');
        // Restore the original sequence for the remaining line/point/apply checks.
        await page.locator('[data-five-command="five.reference.clear.origin"]').click();
        await page.locator('[data-five-command="five.reference.pick.plane"]').click();
      }
    }
    assert.equal(await page.evaluate(()=>fixture.view.fiveAxisRegistrationPick),null);
    assert.deepEqual(await invoke('CamScene.ReadState'),before,'picking does not modify placement');
    const bounds=await page.locator('[role="dialog"]').boundingBox();
    assert.ok(bounds.height<420 && bounds.width<=350);
    assert.ok(await page.getByRole('button',{name:'确定',exact:true}).isVisible());
    assert.equal(await page.locator('[data-five-key="workpiece-flow"]').count(),0);
    assert.equal(await page.locator('.cam-context-pane').isVisible(),true);
    assert.equal(await page.locator('[data-five-workpiece]').count(),1);
    await page.screenshot({path:resolve(output,`coordinate-${mode}-selected.png`)});
    if(mode==='face'||mode==='line') {
      const prefix=mode==='face'?'plane':'axisX',before=(await draft());
      await page.locator(`[data-five-command="five.reference.flip.${prefix}"]`).click();
      const flipped=await draft();for(const axis of 'XYZ')assert.ok(Math.abs(Number(flipped[prefix+axis])+Number(before[prefix+axis]))<1e-9);
      await page.locator(`[data-five-command="five.reference.flip.${prefix}"]`).click();
    }
    await confirm('rough');
    await page.waitForFunction(()=>!fixture.view.fiveAxisRegistrationOpen);
    assert.equal(await page.locator('.five-reference-prompt').count(),0);
    const applied=await invoke('CamScene.ReadState');
    assert.equal(applied.placements[0].registrationMethod,({point:'Points',line:'Lines',face:'Plane'})[mode]);
    const t=applied.placements[0].transform;
    near([t[2]*20+t[3],t[6]*20+t[7],t[10]*20+t[11]],[0,0,0]);
    if(mode!=='line')near(t,[1,0,0,0,0,1,0,0,0,0,1,-20,0,0,0,1]);
    const report=await invoke('CamPlacement.ReadRegistration',{expected:applied.expected,placementId,setupId:applied.activeSetupId});
    if(mode==='face') { assert.equal(report.report.input.referenceQueries.axisX.mode,'endpoint');near(report.report.input.xPoint,[40,0,20]); }
    assert.equal(await page.evaluate(() => registrationCanvas===document.querySelector('canvas')),true);
    if(mode!=='face') {await rpc({action:'undo'});await refresh();await idle();await page.evaluate(()=>fixture.view.viewport.fitView(1.35));}
    cases.push(`${mode}: native hover, persistent feature highlight, step feedback, direction arrows, actual viewport picks and one transactional Confirm`);
  }
  await page.evaluate(()=>fixture.view.viewport.fitView(1.6));
  await open('rough','point'); await clickModel([.15,.15,20]);await selected('origin');
  const beforeCancel=await invoke('CamScene.ReadState');await cancel();
  assert.deepEqual(await invoke('CamScene.ReadState'),beforeCancel);
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisRegistrationPick),null);
  assert.equal(await page.locator('[role="dialog"]').count(),0);
  cases.push('Cancel closes the floating task and clears picking without changing the Scene');

  await open('rough','line');
  const interior=await screenPoint([20,30,20]);await page.mouse.move(interior.x,interior.y);
  await page.waitForFunction(()=>fixture.view.fiveRegistrationInteraction?.error);
  assert.match(await page.locator('.five-reference-prompt').innerText(),/请靠近直线边/);
  assert.equal(await page.evaluate(()=>fixture.view.fiveRegistrationInteraction.hover),null);
  await hoverModel([20,.2,20]);
  await cancel();
  cases.push('invalid straight-edge hover is rejected and moving within the same mesh recovers a valid edge');

  await open('rough','face');
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.ResolveReference';fixture.hold=true;});
  const facePoint=await screenPoint([20,30,20]);await page.mouse.move(facePoint.x,facePoint.y);
  await page.waitForFunction(()=>fixture.held);
  await cancel();await page.evaluate(()=>fixture.release());
  await page.waitForFunction(()=>!fixture.held);
  assert.equal(await page.locator('.five-reference-prompt').count(),0);
  assert.equal(await page.evaluate(()=>fixture.view.fiveRegistrationInteraction),null);
  assert.deepEqual(await invoke('CamScene.ReadState'),beforeCancel);
  cases.push('late hover response after Cancel neither restores overlays nor changes scene state');

  await open('fine');
  const model=[[0,0,20],[40,0,20],[0,60,20]], measured=model.map(([x,y,z])=>[100-y,200+x,300+z]);
  for(let i=0;i<3;i++)await setPoint(`measured${i+1}`,measured[i]);
  await page.getByRole('button',{name:'端点',exact:true}).click();
  for(let i=0;i<3;i++){await clickModel([[.15,.15,20],[39.85,.15,20],[.15,59.85,20]][i]);await selected(`model${i+1}`);}
  assert.equal(await page.locator('[data-five-field^="measured"]').count(),9);
  await page.locator('[data-five-field="measured3X"]').fill('42');
  await confirm('fine');
  assert.equal(await page.locator('[role="dialog"]').count(),1);
  assert.match(await page.locator('[role="dialog"]').innerText(),/误差超限/);
  await setPoint('measured3',measured[2]);
  await page.screenshot({path:resolve(output,'fine-selected.png')});
  const exact=[0,-1,0,100,1,0,0,200,0,0,1,300,0,0,0,1];
  const beforeSolve=await invoke('CamScene.ReadState');
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.SolveRegistration';fixture.hold=true;});
  await page.locator('[data-five-command="five.registration.confirm.fine"]').click();
  await page.waitForFunction(()=>fixture.held);
  await page.locator('[data-five-field="measured1X"]').fill('101');
  await page.evaluate(()=>fixture.release());await idle();
  assert.deepEqual(await invoke('CamScene.ReadState'),beforeSolve,'Confirm must not apply a solve after newer input');
  await page.locator('[data-five-field="measured1X"]').fill('100');
  cases.push('editing during Confirm invalidates the read-only solve and prevents application');
  await page.evaluate(()=>{fixture.holdAfterApply=true;fixture.dropApply=true;});
  await page.locator('[data-five-command="five.registration.confirm.fine"]').click();
  await page.waitForFunction(()=>fixture.held);
  await page.locator('[data-five-field="measured1X"]').fill('101');
  await page.evaluate(()=>{const input=document.querySelector('[data-five-field="measured1X"]');input.focus({preventScroll:true});input.setSelectionRange(0,2);window.registrationInput=input;});
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.locator('[data-five-field="measured1X"]').inputValue(),'101');
  assert.deepEqual(await page.evaluate(()=>({same:registrationInput===document.querySelector('[data-five-field="measured1X"]'),focused:document.activeElement===registrationInput,selection:[registrationInput.selectionStart,registrationInput.selectionEnd]})),{same:true,focused:true,selection:[0,2]});
  const fineState=await invoke('CamScene.ReadState');near(fineState.placements[0].transform,exact);
  assert.ok(calls.some(c=>c.method==='CamScene.QueryCommand'));
  cases.push('fine: 3 measured rows, 3 actual model picks, residual rejection, exact native transform; delayed receipt preserves newer input and selection');
  await page.locator('[data-five-field="measured1X"]').blur();
  await command('five.registration.load.fine');
  await page.waitForFunction(()=>document.querySelector('[data-five-field="measured1X"]').value==='100');
  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});
  await confirm('fine');
  const uncertain=await draft();assert.ok(uncertain.registrationRequest);
  const committed=await invoke('CamScene.ReadState');
  await confirm('fine');
  assert.deepEqual(await invoke('CamScene.ReadState'),committed,'same command receipt, no second mutation');
  const repeats=calls.filter(c=>c.method==='CamPlacement.ApplyRegistration'&&c.payload.commandId===uncertain.registrationRequest.payload.commandId);assert.equal(repeats.length,2);
  cases.push('unknown application receipt retries the same immutable command from Confirm');
  await rpc({action:'undo'});await refresh();await idle();await rpc({action:'redo'});await refresh();await idle();
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});
  near(reopened.placements[0].transform,exact);
  const saved=await invoke('CamPlacement.ReadRegistration',{expected:reopened.expected,setupId:reopened.activeSetupId,placementId});
  assert.deepEqual(saved.report.input.modelPoints,model);assert.deepEqual(saved.report.input.measuredPoints,measured);
  cases.push('undo, redo and source-free reopen preserve the applied three-point registration');
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,coverage:{scene:'production SDO, exact BRep picking, Repository and View',browser:'Chromium Edge',payloadEncoding:'production SDK serializeVariantText (int/double)',fileChooser:'injected local path',standaloneCef:false},calls},null,2));
  console.log(JSON.stringify({passed:true,cases:cases.length,output}));
} catch (error) {
  failure = error;
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));
  if (page) { console.error(await page.evaluate(() => ({ text: document.body.innerText.slice(0,3500), error: window.fixture?.view.error }))); await page.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {}); }
  throw error;
} finally {
  writeFileSync(resolve(output, 'native.log'), nativeLog.join('\n'));
  await browser?.close();
  try { await rpc({ action: 'exit' }); } catch {}
  processHandle.stdin.end();
  if (failure) processHandle.kill();
}
