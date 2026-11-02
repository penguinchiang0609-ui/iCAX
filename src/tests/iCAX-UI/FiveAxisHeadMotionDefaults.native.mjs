import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {axisDraft,changeHeadStructure,structureOf} from '../../apps/five-axis-cam/webpage/headMotionDraft.mjs';
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'output/run/five-axis-cam-unified-head-final-20261101');
const output=resolve('output/acceptance/head-unified-20261101');mkdirSync(output,{recursive:true});
const child=spawn(resolve(runtime,'FiveAxisHeadMotionTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
const pending=new Map();let serial=0;
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__'))return;const r=JSON.parse(line.slice(12)),p=pending.get(r.id);if(!p)return;pending.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>reject(Error('Native timeout')),60000);pending.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});const report={passed:false,realNativeSDO:true,cases:[]};
try{
 let state=await rpc({action:'prepare-axis-test'});const cad=structuredClone(state.workpieces),heads=(await invoke('CamHead.ListBuiltins')).items;
 for(const [kind,headCount,workAxes] of [['AB',2,[]],['4+1',1,[[0,0,1]]],['3+2',0,[[1,0,0],[0,0,1]]]]){
  await invoke('CamHead.Configure',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,kind:'Builtin',builtinDefinitionId:heads.find(h=>h.kind===kind).id});
  state=await invoke('CamScene.ReadState');const head=state.setups.find(s=>s.id===state.activeSetupId).headModel,axes=head.instance.parameters.axes;
  assert.equal(axes.filter(a=>a.target==='Head').length,headCount);assert.deepEqual(axes.filter(a=>a.target==='Workpiece').map(a=>a.axis),workAxes);
  const native=await invoke('CamSimulation.ReadMachine',{setupId:state.activeSetupId});assert.equal(native.independentHead,true);assert.equal(native.configured,true);assert.deepEqual(state.workpieces,cad);
  if(kind==='AB'){
   const draft={axes:axes.map(axisDraft)};changeHeadStructure(draft,{assembly:head.sourceAssembly},'HW');assert.equal(structureOf(draft.axes),'HW');assert.deepEqual(draft.axes.map(a=>a.name),['A','B']);assert.deepEqual(draft.axes[1].axis,['0','0','1']);
  }
  report.cases.push({kind,headCount,workAxes,configured:true,cadUnchanged:true});
 }
 report.passed=true;
}finally{writeFileSync(resolve(output,'defaults-native.json'),JSON.stringify(report,null,2));await rpc({action:'exit'}).catch(()=>{});child.stdin.end();child.kill();}
console.log(JSON.stringify(report));
