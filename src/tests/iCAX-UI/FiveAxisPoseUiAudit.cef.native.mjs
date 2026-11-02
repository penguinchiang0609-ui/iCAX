import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import {createUndoRedoAudit} from './FiveAxisUndoRedoAudit.cef.native.mjs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/ui-history-20261023';mkdirSync(out+'/pose',{recursive:true});
const resume=process.argv.includes('--resume-gizmos');
const persist=process.argv.includes('--verify-persistence');
try{
const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0];
const blocked=await page.evaluate(()=>mv.fiveMachineIK.lastResult);
if(blocked?.status==='Blocked')writeFileSync(out+'/collision-rejected-pose.json',JSON.stringify(blocked,null,2));
// UI undo cancels a rejected, uncommitted pose draft without consuming a native
// repository command. There is deliberately no new redo entry for a failed edit.
if(await page.evaluate(()=>!!mv.fiveMachineIK.pose?.dirty)){
 const before=await page.evaluate(()=>mv.viewportSceneProxy.getUndoRedoState());
 await page.locator('[data-action="undo-project"]').click();await page.waitForFunction(()=>!mv.fiveMachineIK.pose?.dirty);
 assert.deepEqual(await page.evaluate(()=>mv.viewportSceneProxy.getUndoRedoState()),before);
}
const machineId='577ddf2b-dda3-4fc2-895d-4d138983637a';
await page.locator('[data-five-machine-instance]').selectOption(machineId);await page.waitForFunction(()=>!mv.fiveMachine.busy);
const joints=await page.evaluate(()=>mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints);
const tip=joints.find(j=>j.jointName==='J6').entityId,j5=joints.find(j=>j.jointName==='J5').entityId;
const click=cmd=>page.locator(`[data-five-command="${cmd}"]`).first().click();
if(!resume&&!persist){await click('five.machine.part:'+j5);await page.locator('[data-five-machine-field="jogStep"]').selectOption('10');
await click('five.machine.jog-plus:'+j5);await page.waitForFunction(()=>!mv.fiveMachineJog?.running&&!mv.fiveMachine.busy&&!mv.pending);}
await click('five.machine.part:'+tip);
const audit=await createUndoRedoAudit(page,out+'/pose',machineId,tip,resume||persist);
const field=k=>page.locator(`[data-five-machine-field="${k}"]`);
const input=async(f,value)=>{await f.click();await f.press('Control+a');await page.keyboard.insertText(value);assert.equal(await f.inputValue(),value);await f.blur();};
if(persist)await audit.cycle('保存前位置编辑撤销重做',async()=>{const f=field('ik-pose:0');await input(f,String(Number(await f.inputValue())+.1));});
if(!resume&&!persist)for(const [axis,delta,label] of [[0,.5,'部件位置 IK'],[5,.05,'部件空间角 IK']]){
 await audit.cycle(label,async()=>{const f=field('ik-pose:'+axis);await input(f,String(Number(await f.inputValue())+delta));});console.log('PASS',label);
}
if(!persist)for(const [mode,axis] of [['translate','X'],['rotate','Z']]){
 if(resume&&audit.records.some(r=>r.name==='末端手柄 '+mode))continue;
 await click('five.machine-ik.'+mode);
 await audit.cycle('末端手柄 '+mode,()=>gizmoDriver(page).drag(axis,mode==='rotate'?-5:5));console.log('PASS tip',mode);
}
writeFileSync(out+'/pose/expected.json',JSON.stringify({project:out+'/acceptance.i5cam',machineId,tip,final:await audit.read()},null,2));
console.log('EXPECTATION_SAVED_BEFORE_UI_SAVE');
await page.locator('[data-command-id="app.save"]').click({timeout:180000});await audit.settle();
await page.waitForFunction(()=>!document.querySelector('[data-command-id="app.save"]').disabled);
await page.screenshot({path:out+'/pose/final.png'});
console.log('PASS actual pose UI undo redo');process.exit();
}catch(e){console.error(e);process.exit(1);}
