const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{const browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),p=browser.contexts()[0].pages()[0];
 const path='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/工业机床碰撞验证.i5cam';
 await p.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await p.waitForFunction(async path=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machines?.length&&!mv.pending;},path);
 const result=await p.evaluate(async()=>{const invoke=(name,args)=>mv.viewportSceneProxy.invoke(name,args,{timeoutMs:120000}),state=await invoke('CamScene.ReadState',{}),machine=(await invoke('Machine.List',{})).machines[0],z=machine.joints.find(j=>j.jointName==='Z'),x=machine.joints.find(j=>j.jointName==='X');
  const req={expected:state.expected,setupId:state.activeSetupId};
  const initial=await invoke('CamSimulation.ReadMachine',{setupId:state.activeSetupId});
  const blocked=[];for(let i=0;i<2;i++){const t=performance.now();const r=await invoke('CamSimulation.MoveMachine',{...req,positions:{[z.entityId]:-450}});blocked.push({status:r.status,collisionStatus:r.collisionStatus,reason:r.reason,ms:performance.now()-t});}
  const after=await invoke('CamSimulation.ReadMachine',{setupId:state.activeSetupId});
  let limitRejected=false;try{await invoke('CamSimulation.MoveMachine',{...req,positions:{[x.entityId]:x.upper+1}});}catch(e){limitRejected=/LIMIT/.test(e.message);}
  const t=performance.now(),move=await invoke('CamSimulation.MoveMachine',{...req,positions:{[x.entityId]:x.position+1}}),safeMs=performance.now()-t;
  if(move.status==='Ready')await invoke('CamSimulation.MoveMachine',{...req,positions:{[x.entityId]:x.position}});
  return {blocked,unchanged:JSON.stringify(initial.positions)===JSON.stringify(after.positions),limitRejected,safe:move.status,safeMs};
 });assert.equal(result.unchanged,true);assert.equal(result.limitRejected,true);assert.equal(result.safe,'Ready');for(const r of result.blocked)assert.equal(r.collisionStatus,'Blocked');
 fs.writeFileSync('output/tests/machine-simulation/jog-safety-acceptance.json',JSON.stringify(result,null,2));console.log(result);process.exit(0);
})().catch(e=>{console.error(e);process.exit(1)});
