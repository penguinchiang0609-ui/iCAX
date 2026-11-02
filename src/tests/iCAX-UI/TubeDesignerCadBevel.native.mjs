import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { startSideSketchNativeBridge, repositoryRoot } from './fixtures/sideSketchNativeBridge.mjs';

const artifacts = resolve(repositoryRoot, process.env.ICAX_CAD_BEVEL_ARTIFACTS || 'output/tests/cad-bevel/native-sdo');
mkdirSync(artifacts, { recursive: true });
const bridge = await startSideSketchNativeBridge(artifacts);
const report = { dllHash: bridge.dllHash, importerHash: bridge.importerHash, scenarios: [],
  scope: 'Current creation SDOs, exact selectable preview resources, BRep material and neutral CAD document disk roundtrip; stdio transport.' };
const rectangle = (w, h) => {
  const p = [[-w/2,-h/2],[w/2,-h/2],[w/2,h/2],[-w/2,h/2]];
  return {kind:'path',closed:true,segments:p.map((start,i)=>({kind:'line',start,end:p[(i+1)%4]}))};
};
const profile = {schema:'icax.imported-tube-profile',schemaVersion:1,kind:'fixed-section',profileForm:'fixed',
  name:'原生 CAD 坡口方管',width:40,depth:20,contours:[rectangle(40,20),rectangle(36,16)]};
const drawing = {schemaVersion:1,baseType:'parametric',length:200,section:{source:'fixed',name:profile.name,profile}};
const parts = snapshot => snapshot.tubeDesigner.nestingGroups.flatMap(group=>group.parts);
const geometry = partEntityId => bridge.rpc({action:'geometry',payload:{partEntityId}});
const document = partEntityId => bridge.invoke('GetTubeCadDocument',{partEntityId});
const near = (a,b) => Math.abs(a-b)<1e-5;
const savedParts = [];
async function keepCreated(id) {
  const solid = await geometry(id), cad = await document(id);
  assert.equal(solid.valid,true); assert.ok(cad.documentResourceId && cad.geometry.SolidNodes.length);
  const evaluated = await bridge.rpc({action:'cad-document-geometry',payload:{partEntityId:id}});
  assert.equal(evaluated.valid,true);
  assert.ok(evaluated.directedDifferenceVolume<1e-3,'Published material equals independently evaluated document');
  savedParts.push({id,solid,cad}); return solid;
}
function startTopSelection(topology) {
  const start=Math.min(...topology.edges.flatMap(e=>e.points.map(p=>p[0])));
  const top=Math.max(...topology.edges.flatMap(e=>e.points.map(p=>p[2])));
  const edge=topology.edges.find(e=>e.points.every(p=>near(p[0],start)&&near(p[2],top))
    &&Math.max(...e.points.map(p=>p[1]))-Math.min(...e.points.map(p=>p[1]))>5);
  assert.ok(edge,'Exact outer start edge');
  const byId=new Map(topology.edges.map(e=>[e.id,e]));
  const face=topology.faces.find(f=>f.edgeIds.includes(edge.id)&&f.edgeIds.every(id=>byId.get(id)?.points.every(p=>near(p[0],start))));
  assert.ok(face,'Exact cut-wall reference face'); return {edge,face};
}
try {
  const payload={name:'创建前 CAD 坡口选边',quantity:1,profile,length:200,previewResourceKey:'new-part-cad-bevel',
    drawing,features:[],ends:{start:{type:'keep'},end:{type:'keep'}}};
  const blank=await bridge.invoke('AddPartDrawing',payload),base=await keepCreated(blank.partEntityId);
  const preview=await bridge.invoke('PreviewPartDrawing',payload);
  assert.equal(preview.previewComputed,true,preview.resultError); assert.ok(preview.geometry.url);
  const {edge,face}=startTopSelection(preview.topology);
  const repeated=await bridge.invoke('PreviewPartDrawing',payload);
  assert.deepEqual(repeated.baseGeometry,preview.baseGeometry); assert.equal(repeated.topology.revision,preview.topology.revision);
  for(const item of [edge,face]) {
    const resource=await bridge.rpc({action:'resource',payload:item.geometry});
    assert.ok(resource.bytes>0); assert.equal(resource.previewOwner,payload.previewResourceKey);
  }
  const recipe={id:'start-bevel',topologyRevision:preview.topology.revision,edgeIds:[edge.id],referenceFaceId:face.id,
    bevel:{enabled:true,side:'outer',angleDegrees:45,rootFace:.5}};
  const bevelPayload={...payload,drawing:{...drawing,bevels:[recipe]}};
  const bevelPreview=await bridge.invoke('PreviewPartDrawing',bevelPayload);
  assert.equal(bevelPreview.previewComputed,true,bevelPreview.resultError);
  assert.deepEqual(await geometry(blank.partEntityId),base,'Creation preview preserves existing part');
  const beveled=await bridge.invoke('AddPartDrawing',bevelPayload),bevelSolid=await keepCreated(beveled.partEntityId);
  assert.ok(bevelSolid.volume<base.volume);
  report.scenarios.push({name:'Exact boundary selection creates real bevel in neutral document',beforeVolume:base.volume,afterVolume:bevelSolid.volume});
  const count=parts(await bridge.invoke('List')).length;
  for(const invalid of [{...recipe,topologyRevision:'stale-revision'},{...recipe,bevel:{...recipe.bevel,rootFace:2}}]) {
    await assert.rejects(bridge.invoke('AddPartDrawing',{...payload,drawing:{...drawing,bevels:[invalid]}}),/重新选择|已经变化|实际壁厚/);
    assert.equal(parts(await bridge.invoke('List')).length,count); assert.deepEqual(await geometry(beveled.partEntityId),bevelSolid);
  }
  const disabled=await bridge.invoke('AddPartDrawing',{...payload,drawing:{...drawing,bevels:[{...recipe,
    bevel:{enabled:false,side:'retained',angleDegrees:-1,rootFace:-2}}]}});
  assert.ok(near((await keepCreated(disabled.partEntityId)).volume,base.volume));
  report.scenarios.push({name:'Invalid selection/root land reject atomically; disabled parent ignores child draft values'});
  const other=await bridge.invoke('PreviewPartDrawing',{...payload,previewResourceKey:'other-editor'});
  const otherEdge=other.topology.edges[0];
  assert.equal((await bridge.rpc({action:'resource',payload:otherEdge.geometry})).previewOwner,'other-editor');
  const released=await bridge.invoke('ReleasePartDrawingPreview',{previewResourceKey:payload.previewResourceKey});
  assert.equal(released.released,true); assert.ok(released.resourceCount>0);
  await assert.rejects(bridge.rpc({action:'resource',payload:edge.geometry}),/not present/);
  assert.ok((await bridge.rpc({action:'resource',payload:otherEdge.geometry})).bytes>0);
  assert.equal((await bridge.invoke('ReleasePartDrawingPreview',{previewResourceKey:payload.previewResourceKey})).resourceCount,0);
  await bridge.invoke('ReleasePartDrawingPreview',{previewResourceKey:'other-editor'});
  report.scenarios.push({name:'Creation previews release only their own resources idempotently'});
  const entity={id:'slot',kind:'rectangle',x:10,y:15,width:8,height:12,closed:true};
  const sidePayload=e=>({name:'侧面草图坡口',profile,length:200,quantity:1,sketch:{schema:'icax.tube-sketch',schemaVersion:1,
    kind:'side',unit:'mm',length:200,faceHeight:120,perimeter:120,coordinateSpace:'arc-length-axial',trajectoryWidth:1,entities:[e]}});
  const ordinary=await keepCreated((await bridge.invoke('AddNestingSideSketchPart',sidePayload(entity))).partEntityId),volumes=[];
  for(const side of ['outer','inner','both']) {
    const added=await bridge.invoke('AddNestingSideSketchPart',sidePayload({...entity,bevel:{enabled:true,side,angleDegrees:45,rootFace:.5}}));
    const solid=await keepCreated(added.partEntityId); assert.ok(solid.volume<ordinary.volume,side); volumes.push({side,volume:solid.volume});
  }
  for(const invalid of [{angleDegrees:90},{rootFace:2}]) {
    const before=parts(await bridge.invoke('List')).length;
    await assert.rejects(bridge.invoke('AddNestingSideSketchPart',sidePayload({...entity,
      bevel:{enabled:true,side:'outer',angleDegrees:45,rootFace:.5,...invalid}})),/坡口|实际壁厚/);
    assert.equal(parts(await bridge.invoke('List')).length,before);
  }
  const disabledSide=await bridge.invoke('AddNestingSideSketchPart',sidePayload({...entity,bevel:{enabled:false,side:'retained',angleDegrees:-1,rootFace:-2}}));
  assert.ok(near((await keepCreated(disabledSide.partEntityId)).volume,ordinary.volume));
  report.scenarios.push({name:'Side creation preserves outer/inner/both bevel geometry and validates active branches',ordinaryVolume:ordinary.volume,volumes});
  await bridge.rpc({action:'save',payload:{file:'cad-bevel-current-documents.ictd'}});
  await bridge.rpc({action:'open',payload:{file:'cad-bevel-current-documents.ictd'}});
  for(const saved of savedParts) {
    assert.deepEqual(await document(saved.id),saved.cad,'Complete graph, IDs and revisions survive disk reopen');
    const reopened=await geometry(saved.id); assert.equal(reopened.valid,true); assert.ok(near(reopened.volume,saved.solid.volume));
  }
  report.scenarios.push({name:'All created documents and material survive actual project save/reopen',parts:savedParts.length});
  report.passed=true;
} catch(error) {report.passed=false;report.error=String(error.stack??error);throw error;}
finally {report.requests=bridge.requests;writeFileSync(resolve(artifacts,'report.json'),JSON.stringify(report,null,2));await bridge.close();}
console.log(JSON.stringify({passed:report.passed,scenarios:report.scenarios.length,report:resolve(artifacts,'report.json')}));
