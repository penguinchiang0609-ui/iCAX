import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import fs from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
const out=resolve(process.env.ICAX_POSE_REPORT_DIRECTORY||'output/tests/point-pose-latency-20261029'),dir=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||resolve(out,'runtime'));fs.mkdirSync(out,{recursive:true});
const source=resolve(process.env.ICAX_DEFERRED_POSE_PROJECT||'output/tests/rapid-safe-parallel-20261029/汽车冲压件-紧凑空移验证.i5cam'),project=resolve(out,'汽车冲压件-延迟计算验证.i5cam');
fs.copyFileSync(source,project);if(fs.existsSync(source+'.log'))fs.copyFileSync(source+'.log',project+'.log');
const report={passed:false,source,project,dir,measurements:[],checks:[],originalProjectModified:false};
const child=spawn(resolve(dir,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:dir,windowsHide:true,env:{...process.env,PATH:dir+';'+resolve('src/x64/Debug')+';'+process.env.PATH}}),pending=new Map();let serial=0;
const log=fs.createWriteStream(resolve(out,'deferred-native.log'));child.stdout.on('data',x=>log.write(x));child.stderr.pipe(log);
createInterface({input:child.stdout}).on('line',l=>{if(!l.startsWith('__FIVE_RPC__'))return;const r=JSON.parse(l.slice(12)),p=pending.get(r.id);if(p){pending.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));}});
const rpc=m=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>reject(Error('timeout '+m.action+' '+(m.method||''))),240000);pending.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...m,...(m.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(m.payload||{})}:{})})+'\n');});
const timed=async(name,f)=>{console.log('start '+name);const start=performance.now(),r=await f(),ms=performance.now()-start;report.measurements.push({name,ms});console.log(JSON.stringify({name,ms}));return r;};
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const id='c9629454-2602-4757-8d23-2e3952b07b17',close=(a,b)=>a.every((x,i)=>Math.abs(x-b[i])<1e-7);
try{
 await timed('open-copy',()=>rpc({action:'open-project',path:project,replayJournal:true}));
 let state=await timed('initial-state',()=>invoke('CamScene.ReadState')),expected=state.expected;
 report.counts={cuts:state.intentToolpaths.length,fixtures:state.fixtureUnits.length};
 const workpieces=structuredClone(state.workpieces),initialOps=await rpc({action:'intent-operations'});
 let points=(await timed('initial-points',()=>invoke('CamCut.ListToolAxisPoints',{expected,intentId:id}))).points;
 let axis=await invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:points[0].anchor});const original=axis.direction;
 let lastRequest,lastResult,firstOperationCount;
 for(let i=0;i<(process.env.ICAX_POSE_DIAGNOSTIC?2:6);++i){
  const a=.02*(i+1),direction=[original[0],original[1]*Math.cos(a)-original[2]*Math.sin(a),original[1]*Math.sin(a)+original[2]*Math.cos(a)];
  const request={commandId:crypto.randomUUID(),expected,intentId:id,pointId:'start',action:'Upsert',anchor:points[0].anchor,direction,frame:'Workpiece',transition:'ArcLength'};
  const result=await timed('consecutive-edit-'+i,()=>invoke('CamCut.EditToolAxisPoint',request));
  console.log(JSON.stringify({timings:result.timingsMs}));assert.equal(result.deferred,true);assert(close(result.selectedAxis.direction,direction));expected=result.statePatch.expected;points=result.points;lastRequest=request;lastResult=result;if(i===0)firstOperationCount=(await rpc({action:'intent-operations'})).operations.length;
 }
 if(process.env.ICAX_POSE_DIAGNOSTIC){report.timings=lastResult.timingsMs;fs.writeFileSync(resolve(out,'diagnostic.json'),JSON.stringify(report,null,2));await rpc({action:'exit'});child.kill();process.exit(0);}
 assert.deepEqual(await invoke('CamCut.EditToolAxisPoint',lastRequest),lastResult);report.checks.push('same immutable command returns the original receipt');const unchanged=await invoke('CamCut.EditToolAxisPoint',{...lastRequest,commandId:crypto.randomUUID(),expected,anchor:points[0].anchor});assert.equal(unchanged.changed,false);assert.deepEqual(unchanged.statePatch.expected,expected);report.checks.push('reapplying the same direction creates no mutation or undo step');
 const after=await timed('state-after-edits',()=>invoke('CamScene.ReadState'));expected=after.expected;
 assert(!(after.intentGenerationTasks||[]).some(t=>t.status==='Running'));assert.deepEqual(after.workpieces,workpieces);report.checks.push('six edits start no generation task and leave CAD unchanged');
 const operations=await rpc({action:'intent-operations'});assert.equal(operations.operations.length,firstOperationCount);assert(firstOperationCount<=initialOps.operations.length+1);report.checks.push('consecutive edits retain one pending authoring operation');
 axis=await timed('read-authored-axis',()=>invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:points[0].anchor}));assert(close(axis.direction,lastResult.selectedAxis.direction));
 const undone=await timed('undo',()=>rpc({action:'undo'}));assert.equal(undone.changed,true);assert(!(undone.state.intentGenerationTasks||[]).some(t=>t.status==='Running'));
 const redone=await timed('redo',()=>rpc({action:'redo'}));assert.equal(redone.changed,true);assert(!(redone.state.intentGenerationTasks||[]).some(t=>t.status==='Running'));expected=redone.state.expected;report.checks.push('undo and redo preserve deferred calculation');
 const saved=resolve(out,'汽车冲压件-姿态参数持久化.i5cam');await timed('save',()=>rpc({action:'export-project',path:saved}));await timed('reopen',()=>rpc({action:'open-project',path:saved}));state=await invoke('CamScene.ReadState');expected=state.expected;assert(!(state.intentGenerationTasks||[]).some(t=>t.status==='Running'));assert(Number(state.intentToolpaths.find(c=>c.id===id).previewPointCount)>0,'reopening a pending pose must retain its editable contour');
 points=(await invoke('CamCut.ListToolAxisPoints',{expected,intentId:id})).points;axis=await invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:points[0].anchor});assert(close(axis.direction,lastResult.selectedAxis.direction));report.checks.push('saved and reopened parameters retain the latest authored direction without generation');
 let task=await timed('explicit-generate',()=>invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected,setupId:state.activeSetupId}));
 while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
 assert.equal(task.status,'Completed',JSON.stringify(task));state=await invoke('CamScene.ReadState');expected=state.expected;
 const generated=await invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:points[0].anchor});assert(close(generated.direction,lastResult.selectedAxis.direction));report.checks.push('explicit generation materializes the latest saved direction');
 const generate=async()=>{let t=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected,setupId:state.activeSetupId});while(t.status==='Running'){await new Promise(r=>setTimeout(r,100));t=await invoke('CamPlan.QueryStatus',{epoch:t.epoch,taskId:t.taskId});}assert.equal(t.status,'Completed',JSON.stringify(t));state=await invoke('CamScene.ReadState');expected=state.expected;};
 const reset=await invoke('CamCut.EditToolAxisPoint',{commandId:crypto.randomUUID(),expected,intentId:id,pointId:'start',action:'Reset'});expected=reset.statePatch.expected;
 await generate();const resetAxis=await invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:points[0].anchor});assert(close(reset.selectedAxis.direction,resetAxis.direction));report.checks.push('reset previews the exact saved fallback at one point and agrees with explicit generation');
 const sampled=await invoke('CamCut.ReadToolAxisSamples',{expected,intentIds:[id]});const rows=sampled.cuts[0].samples;const sample=rows.slice(Math.floor(rows.length/3),Math.floor(rows.length*2/3)).find(s=>!s.keypointIds.length);assert(sample,'real contour needs an interior authoring station');
 const manualId=crypto.randomUUID(),manualDirection=[.1,.2,Math.sqrt(.95)],manual=await invoke('CamCut.EditToolAxisPoint',{commandId:crypto.randomUUID(),expected,intentId:id,pointId:manualId,action:'Upsert',anchor:sample.anchor,direction:manualDirection,frame:'Workpiece',transition:'ArcLength'});expected=manual.statePatch.expected;
 await generate();const hardPoint=await invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:sample.anchor});assert(close(hardPoint.direction,manualDirection));
 const deletion=await invoke('CamCut.EditToolAxisPoint',{commandId:crypto.randomUUID(),expected,intentId:id,pointId:manualId,action:'Delete'});expected=deletion.statePatch.expected;assert(!deletion.points.some(p=>p.id===manualId));
 const preview=await invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:sample.anchor});await generate();const deleted=await invoke('CamCut.ReadToolAxis',{expected,intentId:id,anchor:sample.anchor});assert(close(preview.direction,deleted.direction));report.checks.push('interior insertion is an exact hard point; deletion retains the correct fallback without eager interpolation');

 report.editMs=report.measurements.filter(m=>m.name.startsWith('consecutive-edit')).map(m=>m.ms);
 assert(Math.max(...report.editMs)<1000,'actual-project consecutive editing still exceeds one second');
 report.passed=true;
}catch(e){report.error=e.stack;console.error(e);}finally{
 fs.writeFileSync(resolve(out,'deferred.json'),JSON.stringify(report,null,2));await rpc({action:'exit'}).catch(()=>{});child.kill();process.exit(report.passed?0:1);
}
