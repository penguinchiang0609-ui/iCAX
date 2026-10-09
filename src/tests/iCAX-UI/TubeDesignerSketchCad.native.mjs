import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync, existsSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { startSideSketchNativeBridge, repositoryRoot } from './fixtures/sideSketchNativeBridge.mjs';

const artifacts = resolve(repositoryRoot, process.env.ICAX_CAD_NATIVE_ARTIFACTS || 'output/tests/cad-native-completion');
mkdirSync(artifacts, {recursive:true});
const bridge = await startSideSketchNativeBridge(artifacts);
const report = {dllHash:bridge.dllHash, scenarios:[], scope:'Real scene/product SDO, BRep volume/validity and .ictd persistence; native stdio transport'};
const rectangle = (w,h) => ({kind:'path',closed:true,segments:[[-w/2,-h/2],[w/2,-h/2],[w/2,h/2],[-w/2,h/2]].map((start,i,a)=>({kind:'line',start,end:a[(i+1)%4]}))});
const profile = {schema:'icax.imported-tube-profile',schemaVersion:1,kind:'fixed-section',profileForm:'fixed',name:'CAD实体验收',width:40,depth:20,contours:[rectangle(40,20),rectangle(36,16)]};
const draft = extras => ({schema:'icax.tube-sketch',schemaVersion:1,kind:'side',unit:'mm',length:200,faceHeight:120,coordinateSpace:'arc-length-axial',trajectoryWidth:1,entities:[],...extras});
const payload = sketch => ({profile,length:200,quantity:2,name:'CAD实体验收',sketch});
const parts = snapshot => snapshot.tubeDesigner.nestingGroups.flatMap(g=>g.parts);
const geometry = id => bridge.rpc({action:'geometry',payload:{partEntityId:id}});
try {
  let sketch = draft({endCuts:{start:{position:20,angleDegrees:0,rotationDegrees:0},end:{position:180,angleDegrees:0,rotationDegrees:0}},arrays:[{id:'preserved-array',spec:{columns:2}}]});
  let added = await bridge.invoke('AddNestingSideSketchPart',payload(sketch));
  let part = parts(added).find(p=>p.entityId===added.partEntityId), solid = await geometry(part.entityId);
  assert.equal(solid.valid,true); assert.ok(Math.abs(solid.volume-224*160)<.01); assert.ok(Math.abs(part.length-160)<.001);
  assert.deepEqual(part.properties['tubeDesigner.sideSketch'].arrays,sketch.arrays);
  const trimmedReference=await bridge.invoke('PreviewNestingSideSketchPart',{partEntityId:part.entityId,
    resourceVersion:part.manufacturingGeometryResourceVersion,previewResourceKey:'cad-trimmed-reference'});
  assert.equal(trimmedReference.length,200); assert.equal(trimmedReference.unfolding.length,200);
  report.scenarios.push({name:'Empty drawing with two end planes produces true trimmed BRep',volume:solid.volume,length:part.length});
  const oblique = draft({endCuts:{start:{position:30,angleDegrees:30,rotationDegrees:37},end:{position:170,angleDegrees:30,rotationDegrees:37}}});
  const slanted = await bridge.invoke('AddNestingSideSketchPart',payload(oblique)); solid=await geometry(slanted.partEntityId);
  assert.equal(solid.valid,true); assert.ok(Math.abs(solid.volume-224*140)<.05);
  report.scenarios.push({name:'Oblique full cross-section planes retain exact material volume',volume:solid.volume});
  const circleContour=r=>({kind:'path',closed:true,segments:Array.from({length:4},(_,i)=>{
    const p=a=>[r*Math.cos(a),r*Math.sin(a)];return{kind:'arc',start:p(i*Math.PI/2),middle:p((i+.5)*Math.PI/2),end:i===3?p(0):p((i+1)*Math.PI/2)};})});
  const roundProfile={...profile,name:'圆管端部裁切',width:40,depth:40,contours:[circleContour(20),circleContour(18)]};
  const roundAdded=await bridge.invoke('AddNestingSideSketchPart',{...payload(draft({faceHeight:40*Math.PI,endCuts:{start:{position:50,angleDegrees:30,rotationDegrees:73},end:{position:150,angleDegrees:30,rotationDegrees:73}}})),profile:roundProfile});
  const roundSolid=await geometry(roundAdded.partEntityId);assert.equal(roundSolid.valid,true);assert.ok(Math.abs(roundSolid.volume-7600*Math.PI)<.05);
  report.scenarios.push({name:'Same end-plane implementation trims a round tube without section-specific branches',volume:roundSolid.volume});
  const ring = draft({splitParts:true,entities:[{id:'band',kind:'rectangle',x:0,y:90,width:120,height:10,closed:true}]});
  const preview = await bridge.invoke('PreviewNestingSideSketchPart',{...payload(ring),previewResourceKey:'cad-split-preview'});
  assert.equal(preview.splitPartCount,2); assert.equal(preview.parts.length,2);
  const beforeCount=parts(await bridge.invoke('List')).length;
  await assert.rejects(bridge.invoke('AddNestingSideSketchPart',payload({...ring,splitParts:false})),/分件/);
  assert.equal(parts(await bridge.invoke('List')).length,beforeCount);
  added = await bridge.invoke('AddNestingSideSketchPart',payload(ring));
  assert.equal(added.splitPartCount,2); assert.equal(added.partEntityIds.length,2);
  const created=added.partEntityIds.map(id=>parts(added).find(p=>p.entityId===id));
  assert.deepEqual(created.map(p=>Math.round(p.length)),[90,100]);
  assert.ok(created.every(p=>p.quantity===2)); assert.match(created[0].name,/1\/2/); assert.match(created[1].name,/2\/2/);
  const solids=await Promise.all(created.map(p=>geometry(p.entityId))); assert.ok(solids.every(s=>s.valid));
  assert.ok(Math.abs(solids.reduce((n,s)=>n+s.volume,0)-224*190)<.05);
  report.scenarios.push({name:'One circumferential removal becomes two independent manufacturing solids',lengths:created.map(p=>p.length),volumes:solids.map(s=>s.volume)});
  const projectFile='split-cad.ictd'; await bridge.rpc({action:'save',payload:{file:projectFile}});
  const reopened=await bridge.rpc({action:'open',payload:{file:projectFile}});
  for(const p of created) {
    const restored=parts(reopened.snapshot).find(v=>v.entityId===p.entityId);
    assert.deepEqual(restored.properties['tubeDesigner.sideSketch'].entities,ring.entities);
    assert.equal(restored.properties['tubeDesigner.sideSketchRecipe'].length,200);
    assert.equal((await geometry(p.entityId)).valid,true);
    const reference=await bridge.invoke('PreviewNestingSideSketchPart',{partEntityId:p.entityId,
      resourceVersion:restored.manufacturingGeometryResourceVersion,previewResourceKey:`cad-split-reference-${p===created[0]?0:1}`});
    assert.equal(reference.length,200); assert.equal(reference.unfolding.length,200); assert.equal(reference.splitPartCount,2);
  }
  const first=parts(reopened.snapshot).find(p=>p.entityId===created[1].entityId);
  const changed=await bridge.invoke('SavePartSketch',{partEntityId:first.entityId,resourceVersion:first.manufacturingGeometryResourceVersion,sketch:draft({splitParts:true})});
  assert.deepEqual(changed.partEntityIds,[first.entityId]); assert.equal(changed.splitPartCount,1);
  assert.ok(!parts(changed).some(p=>p.entityId===created[0].entityId));
  assert.ok(Math.abs((await geometry(first.entityId)).volume-224*200)<.01);
  report.scenarios.push({name:'Editing split member atomically replaces group, retains edited ID and frozen blank'});
  const text=await bridge.invoke('GenerateSketchTextOutline',{text:'O',fontFamily:'Arial',height:20,x:5,y:60,rotation:0,letterSpacing:0},'product');
  assert.equal(text.bOK,true,text.message); assert.equal(text.entities.length,2);
  text.entities.forEach((entity,index)=>{entity.id=`glyph-O-${index}`;});
  const withHole=await bridge.invoke('AddNestingSideSketchPart',payload(draft({splitParts:true,entities:text.entities})));
  const holeSolids=await Promise.all(withHole.partEntityIds.map(geometry));
  const holeGeom={volume:holeSolids.reduce((v,g)=>v+g.volume,0),valid:holeSolids.every(g=>g.valid)};
  const withoutHole=await bridge.invoke('AddNestingSideSketchPart',payload(draft({entities:text.entities.map(({fillGroup,fillRule,...e})=>e)})));
  const filledGeom=await geometry(withoutHole.partEntityId);
  assert.ok(holeGeom.volume>filledGeom.volume+5); assert.equal(holeGeom.valid,true);
  assert.equal(withHole.splitPartCount,2);
  report.scenarios.push({name:'True font Bezier paths preserve O counter as a real material island using exact even-odd regions',contours:text.entities.length,solids:withHole.splitPartCount,withCounter:holeGeom.volume,filled:filledGeom.volume});
  const mixedText=await bridge.invoke('GenerateSketchTextOutline',{text:'BO8管',fontFamily:'Microsoft YaHei',height:10,x:2,y:100,rotation:0,letterSpacing:.2},'product');
  assert.equal(mixedText.bOK,true,mixedText.message);
  assert.ok(mixedText.entities.some(e=>e.segments.some(s=>s.kind==='bezier')));
  mixedText.entities.forEach((e,i)=>{e.id=`mixed-glyph-${i}`;});
  const groups=new Map();for(const e of mixedText.entities)groups.set(e.fillGroup,(groups.get(e.fillGroup)??0)+1);
  assert.ok([...groups.values()].filter(v=>v>1).length>=3);
  const mixedAdded=await bridge.invoke('AddNestingSideSketchPart',payload(draft({splitParts:true,entities:mixedText.entities})));
  assert.ok(mixedAdded.splitPartCount>=4);
  for(const id of mixedAdded.partEntityIds)assert.equal((await geometry(id)).valid,true);
  for(const badText of [{text:'',fontFamily:'Arial',height:10},{text:'A',fontFamily:'Definitely not an installed font 20261009',height:10},{text:'\u{10ffff}',fontFamily:'Arial',height:10}])
    assert.equal((await bridge.invoke('GenerateSketchTextOutline',badText,'product')).bOK,false);
  report.scenarios.push({name:'Mixed BO8 Chinese exact glyph outlines generate valid independent solids; invalid font, empty and missing glyph rejected',groups:[...groups.values()],solids:mixedAdded.splitPartCount});
  const targetPath=resolve(artifacts,'二维图纸.dxf'),content='0\r\nSECTION\r\n2\r\nENTITIES\r\n0\r\nENDSEC\r\n0\r\nEOF\r\n';
  await bridge.invoke('ExportSketchDxf',{targetPath,content},'product'); assert.equal(readFileSync(targetPath,'utf8'),content);
  assert.equal((await bridge.invoke('ReadSketchDxf',{sourcePath:targetPath},'product')).content,content);
  const gbPath=resolve(artifacts,'gb18030.dxf');writeFileSync(gbPath,Buffer.concat([Buffer.from('999\r\n'),Buffer.from([0xd6,0xd0,0xce,0xc4]),Buffer.from('\r\n0\r\nEOF\r\n')]));
  assert.equal((await bridge.invoke('ReadSketchDxf',{sourcePath:gbPath},'product')).content,'999\r\n中文\r\n0\r\nEOF\r\n');
  await assert.rejects(bridge.invoke('ExportSketchDxf',{targetPath:resolve(artifacts,'invalid.txt'),content},'product'),/DXF/);
  assert.ok(!existsSync(resolve(artifacts,'invalid.txt')));
  writeFileSync(targetPath,'previous-content');
  await assert.rejects(bridge.invoke('ExportSketchDxf',{targetPath,content:''},'product'),/DXF/);
  assert.equal(readFileSync(targetPath,'utf8'),'previous-content');
  const lockedTarget=resolve(artifacts,'directory.dxf');mkdirSync(lockedTarget,{recursive:true});
  await assert.rejects(bridge.invoke('ExportSketchDxf',{targetPath:lockedTarget,content},'product'),/保存失败/);
  assert.ok(!readdirSync(artifacts).some(n=>n.startsWith('directory.dxf.')&&n.endsWith('.tmp')));
  report.scenarios.push({name:'Product channel saves and reads exact DXF text with Unicode file paths'});
  for(const endCuts of [{start:{position:190},end:{position:10}},{start:{position:10,angleDegrees:85}},{end:{position:250}}])
    await assert.rejects(bridge.invoke('AddNestingSideSketchPart',payload(draft({endCuts}))),/端部|斜切|首端/);
  report.scenarios.push({name:'Invalid end planes rejected before scene mutation'});
  report.passed=true;
} catch(error) { report.passed=false; report.error=String(error.stack??error); throw error; }
finally {report.requests=bridge.requests;writeFileSync(resolve(artifacts,'report.json'),JSON.stringify(report,null,2));await bridge.close();}
console.log(JSON.stringify({passed:report.passed,scenarios:report.scenarios.length,report:resolve(artifacts,'report.json')}));
