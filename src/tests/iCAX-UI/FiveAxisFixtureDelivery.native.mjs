import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync,readFileSync,writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY),output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),project=resolve(process.env.ICAX_FIXTURE_PROJECT);
mkdirSync(output,{recursive:true});
const native=spawn(resolve(runtime,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
let sequence=0;const pending=new Map(),logs=[];
native.stderr.on('data',data=>logs.push(String(data)));
createInterface({input:native.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){logs.push(line);return;}const r=JSON.parse(line.slice(12)),p=pending.get(r.id);if(!p)return;pending.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
native.on('exit',code=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Native exited '+code));}pending.clear();});
const rpc=message=>new Promise((resolve,reject)=>{const id=++sequence,timer=setTimeout(()=>{pending.delete(id);reject(Error('Native timeout '+message.action));},120000);pending.set(id,{resolve,reject,timer});native.stdin.write(JSON.stringify({id,...message})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payloadText:serializeVariantText(payload)});
const state=()=>invoke('CamScene.ReadState');
const command=async(method,extra={})=>{const s=await state();return invoke(method,{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,...extra});};
try{
  await rpc({action:'open-project',path:project});const initial=await state();let recipe=initial.setups.find(s=>s.id===initial.activeSetupId).fixtureDesign.recipe;
  const deliveries={},reuse=Boolean(process.env.ICAX_FIXTURE_REEXPORT_ONLY);
  for(const pattern of reuse?[recipe.pattern]:['Staggered','Grid']){
    if(!reuse){recipe={...recipe,layout:'Automatic',pattern,baseShape:pattern==='Grid'?'Rectangle':'Circle',stations:[],sizing:'Manual',areaWidth:240,areaDepth:240,areaX:0,areaY:0,spacing:40,jointMode:'TabSlot',jointClearance:.3,tenonWidth:8,tenonPitch:60,jointWeb:1.5};recipe=await command('CamFixture.SetRecipe',{recipe});}
    const started=Date.now();let task=reuse?{status:'Completed',result:initial.setups.find(s=>s.id===initial.activeSetupId).fixtureDesign.assembly}:await command('CamFixture.Generate');
    while(task.status==='Running'){await new Promise(r=>setTimeout(r,200));task=await invoke('CamFixture.QueryTask',{epoch:task.epoch,taskId:task.taskId});if(Date.now()-started>300000)throw Error('Generation timeout');}
    assert.equal(task.status,'Completed',task.error);assert.equal(task.result.supportCheck.status,'Supported');
    const base=task.result.parts.find(p=>p.role==='Base');
    if(recipe.jointMode==='TabSlot'){
      assert.equal(task.result.joint,'TabSlot');assert.equal(base.holes.length,task.result.joinery.baseTenons);assert.ok(base.holes.length>=task.result.parts.length-1);
      const pairs=new Map();for(const p of task.result.parts)for(const j of p.joints){const pair=pairs.get(j.id)||[];pair.push({part:p.partId,...j});pairs.set(j.id,pair);}for(const pair of pairs.values()){assert.equal(pair.length,2);assert.equal(pair[0].part,pair[1].matePartId);assert.equal(pair[1].part,pair[0].matePartId);}
      assert.equal(task.result.joinery.crossLaps>0,pattern==='Grid');
      for(const h of base.holes){const widths=[Math.max(...h.map(p=>p[0]))-Math.min(...h.map(p=>p[0])),Math.max(...h.map(p=>p[1]))-Math.min(...h.map(p=>p[1]))].sort((a,b)=>a-b),expected=[recipe.thickness+recipe.jointClearance,recipe.tenonWidth+recipe.jointClearance].sort((a,b)=>a-b);for(let i=0;i<2;++i)assert.ok(Math.abs(widths[i]-expected[i])<1e-5);}
    }
    if(!reuse){await command('CamFixture.Confirm',{candidateId:task.result.id,supportReviewed:true});await command('CamFixture.Breakdown');}
    const delivery=await command('CamFixture.Export',{destination:output});
    const text=readFileSync(resolve(delivery.directory,'B01.dxf'),'utf8').trim().split(/\r?\n/),entities=[];
    for(let i=0;i<text.length;i+=2){const code=Number(text[i]),value=text[i+1];if(code===0)entities.push({type:value,groups:[]});else entities.at(-1)?.groups.push([code,value]);}
    const marks=entities.filter(e=>e.type==='TEXT'&&e.groups.some(([code,value])=>code===8&&value==='MARK_ID')).map(e=>({number:e.groups.find(([code])=>code===1)[1],x:Number(e.groups.find(([code])=>code===10)[1]),y:Number(e.groups.find(([code])=>code===20)[1])}));
    assert.equal(entities.filter(e=>e.type==='LWPOLYLINE'&&e.groups.some(([code,value])=>code===8&&value==='CUT_INNER')).length,base.holes.length);
    assert.equal(marks.length,task.result.parts.length);assert.equal(new Set(marks.map(p=>p.x+','+p.y)).size,marks.length);
    for(const p of marks){assert.ok(Math.abs(p.x)<=120&&Math.abs(p.y)<=120);if(pattern==='Staggered')assert.ok(Math.hypot(p.x,p.y)<120);}
    deliveries[pattern]={...delivery,parts:task.result.parts.length,joinery:task.result.joinery,holes:base.holes,marks,workflowMs:Date.now()-started,reusedExistingAssembly:reuse};
    await rpc({action:'export-project',path:resolve(output,pattern==='Grid'?'grid-support.i5cam':'staggered-support.i5cam')});
    console.log(JSON.stringify({pattern,parts:task.result.parts.length,directory:delivery.directory,workflowMs:deliveries[pattern].workflowMs}));
  }
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,project,deliveries},null,2));
}finally{writeFileSync(resolve(output,'native.log'),logs.join('\n'));try{await rpc({action:'exit'});}finally{native.stdin.end();}}
