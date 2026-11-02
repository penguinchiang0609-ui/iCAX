// Reuse the adopted automatic assembly from an isolated acceptance project.
// No geometry generation, user-project writes or formal release publication.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {createHash} from 'node:crypto';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY);
const project=resolve(process.env.ICAX_FIXTURE_PROJECT);
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/fixture-opening-strategy/manufacturing');
mkdirSync(output,{recursive:true});
const native=spawn(resolve(runtime,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
let sequence=0;const pending=new Map(),logs=[],calls=[];
native.stderr.on('data',bytes=>logs.push(String(bytes)));
createInterface({input:native.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){logs.push(line);return;}const r=JSON.parse(line.slice(12)),p=pending.get(r.id);if(!p)return;pending.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
native.on('exit',code=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Native exited '+code));}pending.clear();});
const rpc=message=>new Promise((resolve,reject)=>{const id=++sequence,timer=setTimeout(()=>{pending.delete(id);reject(Error('Native timeout '+message.action));},120000);pending.set(id,{resolve,reject,timer});native.stdin.write(JSON.stringify({id,...message})+'\n');});
const invoke=(method,payload={})=>{calls.push(method);return rpc({action:'invoke',scope:'scene',method,payloadText:serializeVariantText(payload)});};
const state=()=>invoke('CamScene.ReadState');
const command=async(method,extra)=>{const s=await state();return invoke(method,{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,...extra});};
const parseDxf=text=>{
  const lines=text.trim().split(/\r?\n/);assert.equal(lines.length%2,0,'DXF group/value pairs');
  const pairs=[];for(let i=0;i<lines.length;i+=2)pairs.push([Number(lines[i]),lines[i+1]]);
  const entities=[];for(const [code,value] of pairs){if(code===0)entities.push({type:value,groups:[]});else entities.at(-1)?.groups.push([code,value]);}
  return {pairs,entities};
};
const group=(entity,code)=>entity.groups.find(([c])=>c===code)?.[1];
const vertices=entity=>{const points=[];for(const [code,value] of entity.groups){if(code===10)points.push([Number(value),NaN]);else if(code===20)points.at(-1)[1]=Number(value);}return points;};
const report={passed:false,project,calls,coverage:{native:'production Breakdown/Export and native DXF readback',independentDxfAudit:true,regenerated:false,userProjectModified:false,formalRelease:false,toolpathOpticalValidation:false}};
try{
  await rpc({action:'open-project',path:project});
  const initial=await state(),unit=initial.fixtureUnits.find(u=>u.kind==='Generated'&&u.design.recipe.windowMode==='Automatic');
  assert.ok(unit,'An adopted automatic fixture unit is required');assert.equal(unit.design.recipe.windowWeb,8);assert.equal(unit.design.recipe.jointMode,'TabSlot');
  assert.equal(initial.intentToolpaths.length,0);const assembly=unit.design.assembly;assert.equal(assembly.supportCheck.status,'Supported');
  const breakdown=await command('CamFixture.Breakdown',{unitId:unit.id});assert.equal(breakdown.parts.length,assembly.parts.length);assert.equal(breakdown.unit,'mm');
  const delivery=await command('CamFixture.Export',{unitId:unit.id,destination:output});
  const manifest=JSON.parse(readFileSync(resolve(delivery.directory,'manifest.json'),'utf8'));assert.equal(manifest.unit,'mm');assert.equal(manifest.scale,1);assert.equal(manifest.parts.length,assembly.parts.length);
  const audits=[];let maxCoordinateErrorMm=0;
  for(const part of assembly.parts){
    const file=resolve(delivery.directory,part.number+'.dxf'),{pairs,entities}=parseDxf(readFileSync(file,'utf8'));
    const units=pairs.findIndex(([code,value])=>code===9&&value==='$INSUNITS');assert.ok(units>=0);assert.deepEqual(pairs[units+1],[70,'4'],'DXF uses millimetres');
    const cuts=entities.filter(e=>e.type==='LWPOLYLINE'&&['CUT_OUTER','CUT_INNER'].includes(group(e,8)));
    const outer=cuts.filter(e=>group(e,8)==='CUT_OUTER'),inner=cuts.filter(e=>group(e,8)==='CUT_INNER');assert.equal(outer.length,1,part.number+' outer contour');assert.equal(inner.length,part.holes.length,part.number+' inner contours');
    const loops=[part.outline,...part.holes],exported=[...outer,...inner];let partError=0;
    for(let i=0;i<exported.length;++i){
      const entity=exported[i],points=vertices(entity);assert.equal(Number(group(entity,70))&1,1,part.number+' closed LWPOLYLINE');assert.equal(Number(group(entity,90)),points.length);assert.equal(points.length,loops[i].length);assert.ok(points.length>=3&&points.flat().every(Number.isFinite));
      for(let j=0;j<points.length;++j)for(let k=0;k<2;++k)partError=Math.max(partError,Math.abs(points[j][k]-loops[i][j][k]));
      const area=Math.abs(points.reduce((sum,p,j)=>{const q=points[(j+1)%points.length];return sum+p[0]*q[1]-p[1]*q[0];},0))/2;assert.ok(area>1e-10,part.number+' nonzero contour area');
    }
    assert.ok(partError<=1e-8,part.number+' coordinate error '+partError);maxCoordinateErrorMm=Math.max(maxCoordinateErrorMm,partError);
    audits.push({part:part.number,role:part.role,outerContours:outer.length,innerContours:inner.length,nativeHoles:part.holes.length,allCutContoursClosed:true,unit:'mm',maxCoordinateErrorMm:partError});
  }
  for(const file of manifest.files){const bytes=readFileSync(resolve(delivery.directory,file.name));assert.equal(bytes.length,Number(file.bytes));assert.equal(createHash('sha256').update(bytes).digest('hex'),file.sha256);}
  const base=audits.find(p=>p.role==='Base');assert.equal(base.innerContours,35);assert.equal(audits.filter(p=>p.role==='Support').reduce((n,p)=>n+p.innerContours,0),19);
  const originalAcceptance=JSON.parse(readFileSync(resolve(project,'../report.json'),'utf8'));
  assert.equal(base.innerContours-originalAcceptance.solid.base,8,'Eight newly generated base windows reach CUT_INNER manufacturing output');
  assert.equal(manifest.openingPreclearance.status,'Partial');assert.equal(manifest.openingPreclearance.toolpathRequired,false);assert.equal(manifest.openingPreclearance.toolpathStatus,'NotChecked');
  const after=await state();assert.equal(after.fixtureUnits.find(u=>u.id===unit.id).design.recipe.windowMode,'Automatic');assert.equal(after.intentToolpaths.length,0);
  await rpc({action:'export-project',path:resolve(output,'actual-step-exported.i5cam')});
  Object.assign(report,{passed:true,unitId:unit.id,directory:delivery.directory,parts:audits.length,audits,maxCoordinateErrorMm,baseWindowIncrease:8,openingPreclearance:manifest.openingPreclearance,manifestFileHashesVerified:true});
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:true,directory:delivery.directory,parts:audits.length,totalInnerContours:audits.reduce((n,p)=>n+p.innerContours,0),baseInnerContours:base.innerContours,baseWindowIncrease:8,maxCoordinateErrorMm},null,2));
}catch(error){report.error=error.stack||error.message;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));throw error;}
finally{writeFileSync(resolve(output,'native.log'),logs.join('\n'));try{await rpc({action:'exit'});}finally{native.stdin.end();}}
