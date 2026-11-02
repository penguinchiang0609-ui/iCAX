import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copyFileSync,readFileSync,writeFileSync} from 'node:fs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',path=out+'/collider-parity.i5cam';
try{
 copyFileSync(out+'/collider-components.i5cam',path);
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0];
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async()=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.machines?.length&&!mv.pending;});
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 const saved=JSON.parse(readFileSync(out+'/collider-components-acceptance.json','utf8'));
 await page.locator('[data-five-machine-instance]').selectOption(saved.machineId);await page.waitForFunction(()=>!mv.fiveMachine.busy);
 const command=a=>page.evaluate(a=>__icaxAppShell.executeRibbonCommand('five.machine.'+a),a);
 await command('part:'+saved.tip);
 const boxes=()=>page.evaluate(()=>structuredClone(mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes));
 const base=await boxes(),id=base[0].id,driver=gizmoDriver(page),value=()=>page.locator('[data-five-collider-value="value"]');
 const settle=()=>page.waitForFunction(()=>{const s=mv.fiveMachineColliders;return !s.saving&&![...s.drafts.values()].some(d=>d.dirty||d.timer||d.requested)&&!s.fileDirty;},null,{timeout:60000});
 await command('box-edit:'+id+':translate');await command('box-focus');
 await page.evaluate(()=>{window.ownerMatrix=mv.fiveMachineColliders.overlayPart.matrixWorld.toArray();window.parityCanvas=mv.viewport.renderer.domElement;window.parityCalls=0;const proxy=mv.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);proxy.invoke=async(method,...args)=>{if(method==='Machine.SetBoxColliders')parityCalls++;return invoke(method,...args);};});
 const cases=[];
 for(const projection of ['perspective','orthographic'])for(const space of ['world','local'])for(const [mode,axis,typed] of [['translate','X','7.5'],['translate','XY','2.5'],['rotate','Z','10'],['rotate','E','10'],['scale','X','1.2'],['scale','XYZ','1.2']]){
  await page.locator(`button[data-projection-mode="${projection}"]`).click();await page.locator(`button[data-transform-space="${space}"]`).click();await command('box-edit:'+id+':'+mode);
  await page.evaluate(async space=>{const T=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=new T.Vector3(1,-1.5,.8),o=mv.fiveMachineColliders.overlay.children[0];if(space==='local')v.applyQuaternion(o.getWorldQuaternion(new T.Quaternion()));mv.viewport.setViewDirection(v.toArray());},space);await command('box-focus');
  await driver.pick(axis);assert.equal(await page.locator('.five-placement-tools[data-transform-owner="collider"] button').count(),3);
  const input=axis==='XY'?page.locator('[data-five-collider-value="x"]'):value();assert.equal(await input.evaluate(e=>e===document.activeElement),true);
  await input.fill(typed);if(axis==='XY')await page.locator('[data-five-collider-value="y"]').fill('3.5');
  const expected=await page.evaluate(({mode,axis,space,typed})=>{
   const s=mv.fiveMachineColliders,h=s.hud,o=s.overlay.children[0];o.updateWorldMatrix(true,false);
   const before=h.world,after=o.matrixWorld.elements,delta=[after[12]-before[12],after[13]-before[13],after[14]-before[14]];
   return {delta,before,size:h.candidate.size,rotation:h.candidate.rotation,baseSize:h.baseBox.size,owner:s.overlayPart.matrixWorld.toArray(),camera:mv.viewport.getCameraState(),fields:document.querySelectorAll('[data-five-collider-value]').length,calls:parityCalls};
  },{mode,axis,space,typed});
  if(mode==='translate'){
   const components=axis==='X'?[7.5,0,0]:[2.5,3.5,0];
   const wanted=space==='world'?components:[0,1,2].map(i=>components.reduce((n,v,j)=>n+v*expected.before[j*4+i],0));
   wanted.forEach((v,i)=>assert.ok(Math.abs(expected.delta[i]-v)<1e-6));
  }else if(mode==='scale')expected.size.forEach((v,i)=>assert.ok(Math.abs(v-expected.baseSize[i]*(axis.includes('XYZ'[i])?1.2:1))<1e-6));
  else assert.notDeepEqual(expected.rotation,base[0].rotation);
  assert.equal(expected.calls,0);assert.deepEqual(await boxes(),base,'numeric preview is not a persisted edit');
  await input.press('Escape');assert.equal(await page.locator('[data-five-collider-value]').count(),0);assert.deepEqual(await boxes(),base);
  assert.deepEqual(await page.evaluate(()=>mv.fiveMachineColliders.overlayPart.matrixWorld.toArray()),expected.owner);
  cases.push([projection,space,mode,axis]);
 }
 // Native commits and history keep the relative-input baseline, not additive deltas.
 for(const [mode,axis,typed] of [['translate','X','7.5'],['rotate','Z','10'],['scale','XYZ','1.1']]){
  await command('box-edit:'+id+':'+mode);await driver.pick(axis);await value().fill(typed);await value().press('Enter');await settle();
  const committed=await boxes(),calls=await page.evaluate(()=>parityCalls);assert.notDeepEqual(committed,base);
  await value().press('Enter');await settle();assert.equal(await page.evaluate(()=>parityCalls),calls);
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await settle();assert.deepEqual(await boxes(),base);
 }
 await command('box-edit:'+id+':translate');await page.keyboard.press('Alt');await driver.drag('X',25,false);
 assert.ok(Math.abs(Number(await value().inputValue()))>.1);await page.keyboard.press('Escape');await page.mouse.up();await page.keyboard.up('Alt');assert.deepEqual(await boxes(),base);
 await command('box-edit:'+id+':translate');await driver.drag('X',20);await settle();const dragged=await boxes();assert.notDeepEqual(dragged,base);
 assert.equal(await value().evaluate(e=>e===document.activeElement),true);await value().fill('7.5');await value().press('Enter');await settle();await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await settle();assert.deepEqual(await boxes(),dragged);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await settle();assert.deepEqual(await boxes(),base);
 for(const width of [1100,1600]){await page.setViewportSize({width,height:900});await command('box-focus');await driver.pick('X');const bounds=await page.locator('[data-transform-owner="collider"]').evaluateAll(nodes=>nodes.map(n=>{const r=n.getBoundingClientRect(),v=mv.viewport.renderer.domElement.getBoundingClientRect();return r.left>=v.left&&r.right<=v.right&&r.top>=v.top&&r.bottom<=v.bottom;}));assert.ok(bounds.every(Boolean));}
 await page.screenshot({path:out+'/collider-transform-parity.png'});assert.deepEqual(errors,[]);
 writeFileSync(out+'/collider-transform-parity.json',JSON.stringify({pass:true,cases,nativeCommitUndo:true,repeatEnterNoop:true,dragThenExact:true,ownerUnchanged:true,twoWidths:true},null,2));console.log('PASS collider transform parity');process.exit(0);
}catch(e){console.error(e);process.exit(1);}
