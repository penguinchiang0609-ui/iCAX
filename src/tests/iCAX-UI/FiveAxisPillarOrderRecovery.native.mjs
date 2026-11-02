import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

const root=resolve('output/tests/order-pillar-recovery-20261101');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||root+'/native');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||root+'/runtime');
const child=spawn(resolve(runtime,'FiveAxisFixturePillarTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
const waiting=new Map(),log=[],calls=[];let serial=0;
const fail=error=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(error);}waiting.clear();};
child.on('error',fail);child.on('exit',code=>fail(Error('Native bridge exited '+code)));child.stderr.on('data',data=>log.push(String(data)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){log.push(line);return;}const r=JSON.parse(line.slice(12)),p=waiting.get(r.id);if(!p)return;waiting.delete(r.id);clearTimeout(p.timer);calls.push({method:p.method,milliseconds:performance.now()-p.started});r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,started=performance.now(),timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native timeout '+(message.method||message.action)));},180000);waiting.set(id,{resolve,reject,timer,started,method:message.method||message.action});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const report={passed:false,native:true,standaloneCef:false,runtime,calls};
try{
 await rpc({action:'open-project',path:resolve(process.env.ICAX_ORDER_PROJECT||root+'/baseline.i5cam'),replayJournal:true});
 const before=await invoke('CamScene.ReadState'),setup=before.setups.find(s=>s.id===before.activeSetupId);
 assert.equal(before.intentToolpaths.length,21);
 report.cutCount=before.intentToolpaths.length;
 // Exercise a cold sorting request, before any tool-axis or machining samples.
 // A saved obsolete option must never enable start edits.
 const options={pattern:'Smart',objective:'Distance',scope:'All',processScope:'Part',plane:'XY',normal:[0,0,1],rowAngleDeg:0,corner:'BottomLeft',inward:true,clockwise:true,skip:0,start:'Auto',optimizeStarts:true};
 const request={commandId:crypto.randomUUID(),expected:before.expected,setupId:setup.id,mode:'Automatic',strategy:'MarkHoleTrim',options,preview:true};
 const started=performance.now();let task=await invoke('CamPlan.BeginOrder',request);report.beginMilliseconds=performance.now()-started;
 console.log(JSON.stringify({stage:'order-started',milliseconds:report.beginMilliseconds}));
 while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
 report.previewMilliseconds=performance.now()-started;report.terminal=task;
 if(process.env.ICAX_ORDER_EXPECT_FAILURE==='1'){assert.equal(task.status,'Failed');}
 else{
 assert.equal(task.status,'Completed',task.error);
 const previewed=await invoke('CamScene.ReadState');assert.deepEqual(previewed,before,'Preview changed the project');
 assert.equal(task.result.definitionOrder.length,21);report.previewReadOnly=true;
 assert.equal(task.result.optimization.costScope,'HeightPenalizedStraightLineTravelEstimate');
 assert.equal(task.result.optimization.endpointSource,'ExistingContourEndpoints');
 assert.equal(task.result.endpoints.length,21);assert.equal(task.result.options.optimizeStarts,false);
 assert.equal(Object.hasOwn(task.result,'operations'),false);assert.equal(Object.hasOwn(task.result,'startEdits'),false);
 const endpoints=new Map(task.result.endpoints.map(p=>[p.intentId,p]));
 const distance=ids=>ids.slice(1).reduce((sum,id,i)=>sum+Math.hypot(...endpoints.get(id).setupStart.map((v,j)=>v-endpoints.get(ids[i]).setupEnd[j])),0);
 const height=ids=>ids.slice(1).reduce((sum,id,i)=>sum+Math.abs(endpoints.get(id).setupStart[2]-endpoints.get(ids[i]).setupEnd[2]),0);
 assert.equal(task.result.options.heightPenaltyWeight,3);
 for(const [prefix,ids] of [['before',setup.definitionOrder],['after',task.result.definitionOrder]]){
  assert.ok(Math.abs(distance(ids)-task.result.optimization[prefix+'TravelCost'])<1e-8);
  assert.ok(Math.abs(height(ids)-task.result.optimization[prefix+'HeightTravelMm'])<1e-8);
  assert.ok(Math.abs(distance(ids)+3*height(ids)-task.result.optimization[prefix+'Cost'])<1e-8);
 }
 let unweighted=await invoke('CamPlan.BeginOrder',{...request,commandId:crypto.randomUUID(),options:{...options,heightPenaltyWeight:0}});
 while(unweighted.status==='Running'){await new Promise(r=>setTimeout(r,100));unweighted=await invoke('CamPlan.QueryStatus',{epoch:unweighted.epoch,taskId:unweighted.taskId});}
 assert.equal(unweighted.status,'Completed',unweighted.error);
 report.heightComparison={unweighted:unweighted.result.optimization,weighted:task.result.optimization};
 assert.ok(task.result.optimization.afterHeightTravelMm<unweighted.result.optimization.afterHeightTravelMm,'Height penalty should reduce layer changes on this real part');
 report.straightLineCostVerified=true;
 const axes=await invoke('CamCut.ReadToolAxisSamples',{expected:before.expected,intentIds:before.intentToolpaths.map(c=>c.id)});
 assert.deepEqual(axes.errors,[]);assert.equal(axes.cuts.length,21);
 report.axisSampleCount=axes.cuts.reduce((n,c)=>n+c.samples.length,0);
 const apply={commandId:crypto.randomUUID(),expected:before.expected,setupId:setup.id,mode:'Automatic',strategy:'MarkHoleTrim',options,previewTaskId:task.taskId};
 const receipt=await invoke('CamPlan.SetOrder',apply);assert.deepEqual(receipt.definitionOrder,task.result.definitionOrder);
 assert.deepEqual(await invoke('CamPlan.SetOrder',apply),receipt,'Retry must return the same commit receipt');
 const after=await invoke('CamScene.ReadState');for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],before[key],key+' changed');
 const axesAfter=await invoke('CamCut.ReadToolAxisSamples',{expected:after.expected,intentIds:before.intentToolpaths.map(c=>c.id)});
 const samplesById=cuts=>Object.fromEntries(cuts.map(c=>[c.intentId,c]));
 assert.deepEqual(samplesById(axesAfter.cuts),samplesById(axes.cuts),'Ordering changed geometry, authored starts or poses');
 assert.deepEqual(axesAfter.errors,[]);report.geometryStartsAndPosesUnchanged=true;
 report.applyIdempotent=true;report.cadAndFixturesUnchanged=true;
 assert.equal(Object.hasOwn(receipt,'adjustedStartCount'),false);report.optimization=task.result.optimization;
 const saved=resolve(output,'sorted.i5cam');await rpc({action:'export-project',path:saved});
 await rpc({action:'open-project',path:saved});const reopened=await invoke('CamScene.ReadState');
 for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(reopened[key],after[key],key+' changed after reopen');
 assert.deepEqual(reopened.setups.find(s=>s.id===reopened.activeSetupId).definitionOrder,receipt.definitionOrder);
 assert.equal(reopened.setups.find(s=>s.id===reopened.activeSetupId).generatedGeometry.ordering.options.heightPenaltyWeight,3);
 report.saveReopen=true;
 report.passed=true;
 }
}catch(error){report.error=error.stack;process.exitCode=1;}
finally{
 writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'native.log'),log.join('\n'));
 await rpc({action:'exit'}).catch(()=>{});child.stdin.end();child.kill();
 console.log(JSON.stringify({passed:report.passed,error:report.error?.slice(0,1500),status:report.terminal?.status,taskError:report.terminal?.error,beginMilliseconds:report.beginMilliseconds,previewMilliseconds:report.previewMilliseconds}));
}
