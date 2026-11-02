import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/ui-history-20261023';
const machineId='577ddf2b-dda3-4fc2-895d-4d138983637a',tip='5f54d1b6-05db-4a50-b827-7a153dc9bfad';
const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0];
const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
await page.setViewportSize({width:1600,height:1000});
await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);});
const cmd=c=>page.locator(`[data-five-command="five.machine.${c}"]`).click();
const field=k=>page.locator(`[data-five-machine-field="${k}"]`);
const fill=async(k,value)=>{await field(k).click();await page.keyboard.press('Control+a');await page.keyboard.insertText(String(value));await page.keyboard.press('Tab');};
const history=()=>page.evaluate(()=>mv.viewportSceneProxy.getUndoRedoState());
const read=()=>page.evaluate(async id=>{const m=(await mv.viewportSceneProxy.invoke('Machine.List',{})).machines.find(m=>m.entityId===id);return {enabled:m.enabled,post:m.postprocessor,joints:m.joints.map(j=>({id:j.entityId,position:j.position,lower:j.lower,upper:j.upper}))};},machineId);
const wait=async fn=>{for(let n=0;n<150;n++){if(await fn())return;await new Promise(r=>setTimeout(r,100));}throw Error('audit state did not settle');};
const eq=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const report={drafts:[],committed:[]};
const save=()=>writeFileSync(out+'/coverage-audit.json',JSON.stringify(report,null,2));
await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
await page.locator('[data-five-machine-instance]').selectOption(machineId);
await cmd('part:'+tip);
assert.equal((await history()).canUndo,false,'start with freshly reopened project');
for(const [name,key,value] of [['行程下限','lower:'+tip,-359],['逻辑轴映射','axis:'+tip+':offset',.25]]){
 const before=await read(),h=await history(),original=await field(key).inputValue();
 await fill(key,value);await page.locator('[data-tab-id="simulation"]').focus();
 report.drafts.push({name,visibleValue:await field(key).inputValue(),nativeUnchanged:eq(before,await read()),historyUnchanged:eq(h,await history()),undoDisabled:await page.locator('[data-action="undo-project"]').isDisabled()});
 await page.screenshot({path:out+`/draft-gap-${report.drafts.length}.png`});await fill(key,original);save();
}
await cmd('part:');
{
 const key='post:name',original=await field(key).inputValue(),before=await read(),h=await history();
 await fill(key,original+' 未提交验收');await page.locator('[data-tab-id="simulation"]').focus();
 report.drafts.push({name:'后置配置',visibleValue:await field(key).inputValue(),nativeUnchanged:eq(before,await read()),historyUnchanged:eq(h,await history()),undoDisabled:await page.locator('[data-action="undo-project"]').isDisabled()});
 await page.screenshot({path:out+'/draft-gap-3.png'});await fill(key,original);save();
}
const cycle=async(name,act)=>{
 const before=await read(),h=await history();await act();await wait(async()=>!eq(before,await read())&&!await page.locator('[data-action="undo-project"]').isDisabled());
 const after=await read();await page.locator('[data-action="undo-project"]').click();await wait(async()=>eq(before,await read())&&!await page.locator('[data-action="redo-project"]').isDisabled());
 await page.locator('[data-action="redo-project"]').click();await wait(async()=>eq(after,await read())&&!await page.locator('[data-action="undo-project"]').isDisabled());
 report.committed.push({name,undo:true,redo:true,historyBefore:h.undoSteps.length});save();console.log('PASS',name);
 await page.locator('[data-action="undo-project"]').click();await wait(async()=>eq(before,await read())&&!await page.locator('[data-action="redo-project"]').isDisabled());
};
await cycle('实例禁用',()=>cmd('instance-toggle'));
await cycle('后置配置保存',async()=>{await fill('post:name','撤销覆盖检查');await cmd('post-save');});
await cmd('part:'+tip);
await cycle('应用行程',async()=>{await fill('lower:'+tip,-359);await cmd('limits:'+tip);});
await cycle('保存轴映射',async()=>{await fill('axis:'+tip+':offset',.25);await cmd('axis-save:'+tip);});
report.completed=true;save();console.log(JSON.stringify(report));process.exit();
