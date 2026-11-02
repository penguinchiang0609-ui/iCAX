import {createRequire} from 'node:module';
import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {createUndoRedoAudit} from './FiveAxisUndoRedoAudit.cef.native.mjs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/ui-history-20261023';
const machineId='84eafb00-dcd1-4b76-85c1-a484a5b3dc7d',tip='b88dbca0-02b0-4522-9726-b69895c9c20c';
const resume=process.argv.includes('--resume-gizmos');
try {
const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0];
const errors=[];page.on('pageerror',e=>errors.push(e.message));
if(!resume)await page.reload();await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),out+'/acceptance.i5cam');
await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return !!mv.scene&&!mv.pending;});
const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
await page.locator('[data-five-machine-instance]').selectOption(machineId);await page.waitForFunction(()=>!mv.fiveMachine.busy);
const click=cmd=>page.locator(`[data-five-command="${cmd}"]`).first().click();
await click('five.machine.part:'+tip);
const audit=await createUndoRedoAudit(page,out,machineId,tip,resume);
const field=k=>page.locator(`[data-five-machine-field="${k}"]`);
const input=async(f,value)=>{await f.click();await f.press('Control+a');await page.keyboard.insertText(value);assert.equal(await f.inputValue(),value);await f.blur();};
const boxId=await page.evaluate(()=>mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes[0].id);
if(!resume){
for(const [kind,axis,delta] of [['position',0,1.25],['rotation',2,1],['size',0,2]]){
  await audit.cycle('碰撞盒 '+kind,async()=>{const f=field(`box:${boxId}:${kind}:${axis}`);await input(f,String(Number(await f.inputValue())+delta));});console.log('PASS',kind);
}
await audit.cycle('碰撞盒禁用',()=>field(`box:${boxId}:enabled`).uncheck());console.log('PASS collider disable');
await audit.cycle('碰撞盒启用',()=>field(`box:${boxId}:enabled`).check(),{keyboard:true});console.log('PASS collider enable / keyboard');
const idsBeforeAdd=(await audit.read()).viewport.boxes.map(b=>b.id);
const addedState=await audit.cycle('添加碰撞盒',()=>click('five.machine.box-add'));console.log('PASS collider add');
const added=addedState.viewport.boxes.find(b=>!idsBeforeAdd.includes(b.id)).id;
await audit.cycle('删除碰撞盒',()=>click('five.machine.box-remove:'+added));console.log('PASS collider delete');
}
for(const [mode,axis] of [['translate','X'],['rotate','Z'],['scale','X']]){
  // Select the first component through its visible card's Edit Bounds button.
  await click('five.machine.box-edit:'+boxId+':bounds');
  await click('five.machine.box-edit:'+boxId+':'+mode);await click('five.machine.box-focus');
  await audit.cycle('碰撞盒手柄 '+mode,()=>gizmoDriver(page).drag(axis,8));console.log('PASS gizmo',mode);
}
await page.screenshot({path:out+'/colliders.png'});
await click('five.machine.box-edit-end');
for(const [axis,delta,label] of [[0,.5,'部件位置 IK'],[5,.05,'部件空间角 IK']]){
  await audit.cycle(label,async()=>{const f=field('ik-pose:'+axis);await input(f,String(Number(await f.inputValue())+delta));});console.log('PASS',label);
}
await click('five.machine-ik.translate');
await audit.cycle('末端平移手柄',()=>gizmoDriver(page).drag('X',2));console.log('PASS tip translate');
await click('five.machine-ik.rotate');
await audit.cycle('末端旋转手柄',()=>gizmoDriver(page).drag('Z',2));console.log('PASS tip rotate');
await page.locator('[data-command-id="app.save"]').click();await audit.settle();
const final=await audit.read();assert.deepEqual(errors,[]);
await page.screenshot({path:out+'/part-pose.png'});
writeFileSync(out+'/expected.json',JSON.stringify({project:out+'/acceptance.i5cam',machineId,tip,final,errors},null,2));
console.log('PASS all actual scene UI undo/redo operations');process.exit();
}catch(e){console.error(e);process.exit(1);}
