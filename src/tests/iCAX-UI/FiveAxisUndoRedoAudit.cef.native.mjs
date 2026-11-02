import assert from 'node:assert/strict';
import {writeFileSync,readFileSync} from 'node:fs';

// Runs against the real standalone CEF page. Mutations are supplied as actual
// locator/mouse/keyboard actions; native calls below only read verification data.
export async function createUndoRedoAudit(page, out, machineId, tip, resume=false) {
  const records=resume?JSON.parse(readFileSync(out+'/interaction-report.json','utf8')).records:[];
  const settle=async()=>{
    await page.waitForFunction(()=>{
      const v=window.mv,c=v?.fiveMachineColliders,k=v?.fiveMachineIK;
      return v?.scene&&!v.pending&&!v.fiveHistoryBusy&&!v.fiveDefinitionRefresh&&!v.fiveMachine?.busy
        &&!c?.saving&&!c?.fileDirty&&!k?.inflight&&!k?.editTask&&!k?.finishing&&!k?.fileSaving
        &&![...(c?.drafts?.values()||[]),...(k?.drafts?.values()||[])].some(d=>d.timer||d.requested||d.dirty);
    },null,{timeout:60000});
    await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  };
  const read=()=>page.evaluate(async({machineId,tip})=>{
    const m=(await mv.viewportSceneProxy.invoke('Machine.List',{})).machines.find(m=>m.entityId===machineId);
    const e=(await mv.viewportSceneProxy.invoke('Machine.GetElement',{entityId:tip})).machineElement;
    const rounded=a=>a.map(n=>Math.round(n*1e7)/1e7);
    const object=mv.viewport.sceneObjects.get(tip);object.updateWorldMatrix(true,false);
    const boxes=mv.fiveMachineColliders.drafts.get(tip)?.boxes||[];
    return {native:{joints:m.joints.map(j=>({id:j.entityId,position:j.position})),boxes:e.collisions},
      viewport:{world:rounded(object.matrixWorld.toArray()),boxes:boxes.map(b=>({...b,
        position:b.position.map(Number),rotation:b.rotation.map(Number),size:b.size.map(Number)}))}};
  },{machineId,tip});
  const history=()=>page.evaluate(()=>mv.viewportSceneProxy.getUndoRedoState());
  const applyHistory=async(direction,keyboard=false)=>{
    const before=await history();
    if(keyboard){await page.locator('[data-tab-id="simulation"]').focus();await page.keyboard.press(direction==='undo'?'Control+z':'Control+y');}
    else await page.locator(`[data-action="${direction}-project"]`).click();
    await page.waitForFunction(async b=>JSON.stringify(await mv.viewportSceneProxy.getUndoRedoState())!==b,JSON.stringify(before));
    await settle();
  };
  const saveReport=()=>writeFileSync(out+'/interaction-report.json',JSON.stringify({records,machineId,tip},null,2));
  const cycle=async(name,action,{keyboard=false,oneStep=true}={})=>{
    await settle();const before=await read(),h0=await history();
    await action();await settle();const after=await read(),h1=await history();
    assert.notDeepEqual(after.native,before.native,name+' must change native state');
    if(oneStep)assert.equal(h1.undoSteps.length,h0.undoSteps.length+1,name+' one native history step');
    await page.screenshot({path:out+'/'+String(records.length+1).padStart(2,'0')+'-changed.png'});
    await applyHistory('undo',keyboard);const undone=await read();assert.deepEqual(undone,before,name+' undo native + rendered state');
    await applyHistory('redo',keyboard);const redone=await read();assert.deepEqual(redone,after,name+' redo native + rendered state');
    records.push({name,keyboard,oneStep,before,after,undo:undone,redo:redone,pass:true});saveReport();return after;
  };
  return {cycle,settle,read,history,applyHistory,records,saveReport};
}
