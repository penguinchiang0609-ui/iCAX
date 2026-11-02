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
const output = browserReportDirectory('five-axis-registration-solid');
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
  const timer = setTimeout(() => { waiting.delete(id); reject(Error(`Native timeout ${message.action}`)); }, 120000);
  waiting.set(id, { resolve, reject, timer });
  const wire = message.action === 'invoke'
    ? { ...message, payload: undefined, payloadText: serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  const info = await rpc({ action: 'info' });
  info.sourcePath=resolve(root,'TestData/real-sheet-metal/pololu-stamped-bracket/Pololu-2257-stamped-bracket.step');
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
    const entry = await import('/src/apps/five-axis-cam/webpage/entry.mjs');
    const { ViewClient } = await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const view = getProjectView(info.projectId);
    const f = window.fixture = { entry, view, fileSelections: 0, logs: [], hold: false, held: false, release: null };
    const sceneProxy = { sceneId: info.sceneId, state: { sceneId: info.sceneId }, pdo: { enabled: false },
      async invoke(method, payload = {}) {
        if (method === (f.holdMethod || 'CamScene.ReadState') && f.hold) { f.hold = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false; }
        const result = await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payload });
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


  const cases=[];
  await selectWorkpieceFile(page, info.sourcePath);
  await page.waitForFunction(()=>!fixture.view.pending && fixture.view.viewport?.sceneObjects.size===1);
  const before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const inspection=(await rpc({action:'invoke',scope:'scene',method:'CamWorkpiece.Inspect',payload:{workpieceId:before.workpieces[0]?.id}})).cadInspection;
  assert.equal(Number(inspection.solidCount),1);assert.equal(Number(inspection.faceCount),319);
  assert.equal(before.workpieces[0].importMetadata.validBRep,true);assert.equal(inspection.status,'Ready');
  assert.equal(await page.locator('[data-five-command="view.fit"]').count(),0);
  assert.equal(await page.getByRole('checkbox',{name:'曲面拼接边'}).count(),0);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.scene.children.some(o=>o.name==='iCAX World XYZ')),true);
  assert.ok(await page.locator('.icax-three-axis-gizmo').isVisible());
  for(const width of [1024,1440]) {
    await page.setViewportSize({width,height:900});
    const display=await page.locator('.icax-three-display-mode').boundingBox(),cube=await page.locator('.cam-viewcube').boundingBox();
    assert.ok(display.y<50 && display.x+display.width<cube.x,'display selector is at the top right beside the cube, without overlap');
  }
  const modes=page.getByRole('combobox',{name:'显示模式'});
  const baseGeometry=await page.evaluate(()=>{
    fixture.displayMesh=[...fixture.view.viewport.sceneObjects.values()][0];
    fixture.displayGeometry=fixture.displayMesh.geometry;return fixture.displayGeometry.uuid;
  });
  for(const mode of ['wireframe','shaded','shadedEdges']) {
    await modes.selectOption(mode);
    assert.equal(await page.evaluate(()=>fixture.view.viewport.displayMode),mode);
    assert.equal(await page.evaluate(()=>fixture.displayMesh.geometry.uuid),baseGeometry);
  }
  await page.evaluate(()=>{fixture.view.viewport.fitView(3);fixture.cameraBeforeFit=fixture.view.viewport.getCameraState();});
  const canvas=await page.locator('.icax-three-viewport-canvas').boundingBox();
  await page.mouse.dblclick(canvas.x+80,canvas.y+100,{delay:50});
  await page.waitForFunction(()=>JSON.stringify(fixture.cameraBeforeFit)!==JSON.stringify(fixture.view.viewport.getCameraState()));
  assert.deepEqual(await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),before,'blank double click changes camera only');
  cases.push('display selector beside view cube at two widths; no seam checkbox or fit button or world axes; modes retain geometry; real blank double click fits without changing scene');
  await page.evaluate(()=>fixture.view.viewport.fitView(1.4));
  await page.screenshot({path:resolve(output,'solid-workpiece.png')});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.rough.point'));
  await page.waitForFunction(()=>fixture.view.fiveRegistrationInteraction?.faces?.length>0);
  const spots=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const v=fixture.view,vp=v.viewport,source=vp.sceneObjects.get(v.scene.domain.placements[0].id);
    source.updateMatrixWorld(true);const rect=vp.renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster(),spots=new Map();
    for(let x=400;x<rect.width-100;x+=12)for(let y=80;y<rect.height-100;y+=12){
      ray.setFromCamera(new THREE.Vector2(x/rect.width*2-1,1-y/rect.height*2),vp.camera);
      const hit=ray.intersectObject(source,false)[0];if(!hit)continue;
      const face=v.fiveRegistrationInteraction.faces.find(f=>hit.faceIndex>=f.first&&hit.faceIndex<f.first+f.count);
      if(!face)continue;
      if(spots.has(face.faceId)){spots.get(face.faceId).pixels++;continue;}
      spots.set(face.faceId,{x:rect.x+x,y:rect.y+y,faceId:face.faceId,count:face.count,pixels:1,local:source.worldToLocal(hit.point.clone()).toArray()});
    }
    return [...spots.values()].sort((a,b)=>b.pixels-a.pixels);
  });
  assert.ok(spots.length>=2,'actual stamped solid presents multiple CAD faces');
  const hover=async spot=>{
    await page.mouse.move(spot.x,spot.y);
    await page.waitForFunction(id=>fixture.view.fiveRegistrationInteraction?.hover?.faceId===id,spot.faceId);
    assert.equal(await page.evaluate(()=>{
      const s=fixture.view.fiveRegistrationInteraction,mesh=s.group.children.find(o=>o.userData.registrationHover);
      return mesh?.geometry.getAttribute('position').count/3;
    }),spot.count,'highlight covers complete BRep face, preserving holes and trims');
  };
  await hover(spots[0]);await page.screenshot({path:resolve(output,'solid-point-mode-face-hover.png')});
  await page.mouse.click(spots[0].x,spots[0].y);await page.waitForFunction(()=>!fixture.view.pending);
  await hover(spots[1]);await page.screenshot({path:resolve(output,'solid-adjacent-face-hover.png')});
  await page.locator('[data-five-command="five.registration.cancel"]').last().click();
  assert.deepEqual(await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),before);
  // Find visible silhouette positions in the actual STEP, then exercise real
  // mouse hover/click from eight pixels outside its displayed boundary.
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.rough.line'));
  const outsideEdges=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const vp=fixture.view.viewport,object=[...vp.sceneObjects.values()][0],rect=vp.renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster(),found=[];
    const hit=(x,y)=>{ray.setFromCamera(new THREE.Vector2(x/rect.width*2-1,1-y/rect.height*2),vp.camera);return ray.intersectObject(object,false)[0];};
    for(let y=120;y<rect.height-100;y+=65){
      let x=rect.width-140;while(x>400&&!hit(x,y))x-=10;
      if(x<=400)continue;
      let lo=x,hi=x+10;for(let i=0;i<12;i++){const mid=(lo+hi)/2;if(hit(mid,y))lo=mid;else hi=mid;}
      if(!hit(hi+8,y))found.push({x:rect.x+hi+8,y:rect.y+y});
    }
    return found;
  });
  let snappedEdge;
  for(const p of outsideEdges){
    await page.mouse.move(p.x,p.y);
    await page.waitForFunction(()=>{const s=fixture.view.fiveRegistrationInteraction;return !!s?.hover||!!s?.error;});
    const result=await page.evaluate(()=>fixture.view.fiveRegistrationInteraction.hover);
    if(result?.query.mode==='line'){snappedEdge={p,result};break;}
  }
  assert.ok(snappedEdge,'the real stamped solid has a straight boundary available from outside');
  await page.screenshot({path:resolve(output,'solid-line-snap-outside.png')});
  await page.mouse.click(snappedEdge.p.x,snappedEdge.p.y);await page.waitForFunction(()=>!fixture.view.pending);
  const lineResult=await page.evaluate(async()=>(await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs')).currentDraft(fixture.view).referenceResults.axisX);
  assert.deepEqual(lineResult,snappedEdge.result);
  await page.locator('[data-five-command="five.registration.cancel"]').last().click();
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.rough.face'));
  await page.mouse.move(snappedEdge.p.x,snappedEdge.p.y);
  await page.waitForFunction(()=>fixture.view.fiveRegistrationInteraction?.hover?.query.mode==='plane');
  const planeResult=await page.evaluate(()=>fixture.view.fiveRegistrationInteraction.hover);
  await page.mouse.click(snappedEdge.p.x,snappedEdge.p.y);await page.waitForFunction(()=>!fixture.view.pending);
  assert.deepEqual(await page.evaluate(async()=>(await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs')).currentDraft(fixture.view).referenceResults.plane),planeResult);
  await page.locator('[data-five-command="five.registration.cancel"]').last().click();
  assert.deepEqual(await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),before);
  cases.push('official stamped STEP: straight edge and plane snap from eight pixels outside the silhouette; real clicks match previews; Cancel preserves the project');
  assert.deepEqual(errors,[]);
  cases.push('official stamped part imports through native SDO as one valid closed BRep solid with 319 faces');
  cases.push('three-point mode: actual pointer highlights whole face, picks point and follows an adjacent face; Cancel leaves scene unchanged');
  const report={passed:true,cases,sourcePath:info.sourcePath,inspection,importMetadata:before.workpieces[0].importMetadata,standaloneCef:false,calls};
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify({passed:true,inspection,output}));
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
