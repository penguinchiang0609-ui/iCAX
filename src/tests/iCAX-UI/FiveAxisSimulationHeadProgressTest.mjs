import test from 'node:test';
import assert from 'node:assert/strict';
import {simulationCommand} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';
import {encodeTaskProgress} from '../../apps/five-axis-cam/protocol/taskProgressPDOProtocol.mjs';

function fixture(){
 let current=true,cancelled=false,reads=0;
 const calls=[],s={cursor:0,frames:[],request:{method:'Import',payload:{commandId:'head',expected:{epoch:'e',mutationSequence:'1'},setupId:'setup',sourcePath:'C:/head.step'}}};
 const view={pending:false,fiveAxisArea:'toolpath',fiveAxisUI:{tools:{toolpath:'simulation'}},fiveSimulation:s,scene:{domain:{expected:{epoch:'e',mutationSequence:'1'},activeSetupId:'setup',setups:[{id:'setup',rapidSettings:{collisionThresholdMm:.1}}],placements:[]}}};
 const receipt={epoch:'e',taskId:'head',status:'Running',progressPDO:{id:'3',version:1,payloadSize:65536,arenaName:'arena'}};
 const context={mount:{querySelector:()=>null},isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/test/head.i5cam'},async autosave(){calls.push(['save']);}},sceneProxy:{pdo:{enabled:true,async withReadDescriptor(_,reader){reads++;return reader(encodeTaskProgress({taskId:'head',epoch:'e',sequence:reads,status:cancelled?'Cancelled':reads<2?'Running':'Completed',kind:'HeadImport'}).buffer,{dataVersion:String(reads)});}},async invoke(method,request){calls.push([method,request]);if(method==='CamHead.Import')return receipt;if(method==='CamHead.CancelTask'){cancelled=true;return receipt;}if(method==='CamHead.QueryTask')return {...receipt,status:cancelled?'Cancelled':'Completed',result:{headRef:{url:'resource://head',version:'1'}}};return {};}}};
 const ops={renderProject(){calls.push(['render',current]);},appendProjectLog(){},async refreshSceneState(){calls.push(['refresh']);}};
 return {context,view,s,ops,calls,setCurrent(v){current=v;}};
}
async function browser(fn){const old=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};try{await fn();}finally{globalThis.cancelAnimationFrame=old;}}

test('simulation head import uses product PDO progress with one CamHead terminal query and one captured autosave',()=>browser(async()=>{
 const f=fixture();await simulationCommand(f.context,f.view,'head-import',f.ops);
 assert.equal(f.calls.filter(c=>c[0]==='CamHead.Import').length,1);assert.equal(f.calls.filter(c=>c[0]==='CamHead.QueryTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,0);
 assert.equal(f.calls.filter(c=>c[0]==='save').length,1);assert.ok(f.calls.findIndex(c=>c[0]==='save')<f.calls.findIndex(c=>c[0]==='refresh'));assert.equal(f.view.pending,false);assert.equal(f.s.task.status,'Completed');
}));
test('simulation head cancel sends one reliable CamHead cancel then drains PDO and queries once without saving',()=>browser(async()=>{
 const f=fixture(),running=simulationCommand(f.context,f.view,'head-import',f.ops);await Promise.resolve();await simulationCommand(f.context,f.view,'head-cancel',f.ops);await running;
 assert.equal(f.calls.filter(c=>c[0]==='CamHead.CancelTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='CamHead.QueryTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='save').length,0);assert.equal(f.view.pending,false);
}));
test('an imported head acknowledged after switching projects still saves only its captured project',()=>browser(async()=>{
 const f=fixture(),invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async(method,request)=>{
  const result=await invoke(method,request);if(method==='CamHead.Import'){f.setCurrent(false);return {...result,status:'Completed'};}return result;
 };
 await simulationCommand(f.context,f.view,'head-import',f.ops);assert.equal(f.calls.filter(c=>c[0]==='save').length,1);assert.equal(f.calls.filter(c=>c[0]==='refresh').length,0);assert.equal(f.calls.filter(c=>c[0]==='render'&&c[1]===false).length,0);
}));
test('simulation head calibration and collision threshold remain reliable parameter commits and autosave once',()=>browser(async()=>{
 for(const action of ['head-builtin','threshold-apply']){
  const f=fixture();f.s.request=null;f.s.builtinHeadIdDraft='lc3015';if(action==='threshold-apply')f.s.thresholdDraft='.2';await simulationCommand(f.context,f.view,action,f.ops);
  assert.equal(f.calls.filter(c=>c[0]=== (action==='head-builtin'?'CamHead.Configure':'CamRapid.Configure')).length,1);
  assert.equal(f.calls.filter(c=>c[0]==='save').length,1);assert.equal(f.calls.filter(c=>c[0].includes('QueryTask')).length,0);assert.equal(f.view.pending,false);
 }
}));
