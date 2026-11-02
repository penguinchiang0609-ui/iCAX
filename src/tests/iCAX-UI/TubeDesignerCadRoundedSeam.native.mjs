// Exact real-workbench R3 source: retain the full cross-seam ellipse, then edit it.
import assert from 'node:assert/strict';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {repositoryRoot,startSideSketchNativeBridge} from './fixtures/sideSketchNativeBridge.mjs';
const output=resolve(repositoryRoot,'output/tests/cad-rounded-seam');mkdirSync(output,{recursive:true});
const bridge=await startSideSketchNativeBridge(output);
const descriptor=JSON.parse(readFileSync(resolve(repositoryRoot,'src/apps/tube-designer/templates/profile/rect/profile.json'),'utf8'));
const parameters={...Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue])),width:60,depth:40,wallThickness:2,cornerRadius:3,innerRadius:1};
const profileRef={scope:'system',id:'rect'},period=194.84955592153878;
const sketch={schema:'icax.tube-sketch',schemaVersion:1,kind:'side',unit:'mm',coordinateSpace:'arc-length-axial',
  length:1000,perimeter:period,faceHeight:period,trajectoryWidth:.5,
  entities:[{id:'source-ellipse',kind:'ellipse',cx:period-3,cy:350,radiusX:12,radiusY:18,rotation:0,closed:true}]};
const payload={profileRef,parameters,length:1000,quantity:1,name:'R3精确跨接缝椭圆',sketch};
const get=id=>bridge.invoke('GetTubeCadDocument',{partEntityId:id,authoringView:'side'});
const body=async id=>{const result=await bridge.rpc({action:'cad-document-geometry',payload:{partEntityId:id}});
  assert.equal(result.valid,true);assert.ok(result.directedDifferenceVolume<=1e-4);return result;};
const report={passed:false,tubeDesignerDllSha256:bridge.dllHash,cadDllSha256:bridge.importerHash,payload};
try{
  const stock=await bridge.invoke('AddNestingStandardPart',{profileRef,parameters,length:1000,quantity:1,name:'R3诊断原材'});
  writeFileSync(resolve(output,'stock-document.json'),JSON.stringify(await get(stock.partEntityId),null,2));
  const preview=await bridge.invoke('PreviewNestingSideSketchPart',{...payload,previewResourceKey:'rounded-seam-create'});
  assert.equal(preview.available,true);
  const creationReference=preview.preview.baseGeometry;
  assert.ok(creationReference.url);
  const creationMesh=await bridge.rpc({action:'resource',payload:creationReference});
  assert.ok(creationMesh.bytes>0);
  await bridge.invoke('ReleaseNestingSideSketchPreview',{previewResourceKey:'rounded-seam-create'});
  await assert.rejects(bridge.rpc({action:'resource',payload:creationReference}),/not present|不存在|缺失/);
  report.creationPreview={available:true,resourceBytes:creationMesh.bytes,released:true};
  const added=await bridge.invoke('AddNestingSideSketchPart',payload),read=await get(added.partEntityId);
  const wrapped=read.geometry.SolidNodes.find(n=>n.Data.type==='SWrappedVolumeNode');assert.ok(wrapped);
  assert.equal(wrapped.Data.value.UVRegion.Boundaries.length,1);
  const curve=wrapped.Data.value.UVRegion.Boundaries[0].Curves[0];assert.equal(curve.Segment.Curve.type,'Ellipse2');
  assert.equal(read.authoringView.sketch.entities.length,1);report.created=await body(read.partEntityId);
  const draft=structuredClone(read.authoringView.sketch),entity=draft.entities[0];assert.equal(entity.kind,'ellipse');
  assert.equal(entity.closed,true);assert.equal(entity.radiusX,18);assert.equal(entity.radiusY,12);
  assert.ok(Math.abs(Math.abs(entity.rotation)-Math.PI/2)<1e-8);entity.radiusX=19;entity.radiusY=13;
  const request={partEntityId:read.partEntityId,resourceVersion:read.resourceVersion,revision:read.revision,
    authoringEdit:{mode:'side',sketch:draft,bindings:read.authoringView.bindings}};
  await bridge.invoke('PreviewTubeCadDocument',{...request,previewResourceKey:'rounded-seam-edit'});
  assert.deepEqual(await get(read.partEntityId),read);
  await bridge.invoke('ReleaseTubeCadDocumentPreview',{previewResourceKey:'rounded-seam-edit'});
  await bridge.invoke('ApplyTubeCadDocument',request);const saved=await get(read.partEntityId);
  assert.ok(saved.geometry.SolidNodes.some(n=>n.ID===wrapped.ID));report.saved=await body(saved.partEntityId);
  assert.ok(report.saved.volume<report.created.volume,'The original ellipse axes edit removes additional real material');
  const edited=saved.authoringView.sketch.entities[0];assert.equal(edited.kind,'ellipse');assert.equal(edited.radiusX,19);assert.equal(edited.radiusY,13);
  await bridge.rpc({action:'save',payload:{file:'rounded-seam.ictd'}});await bridge.rpc({action:'open',payload:{file:'rounded-seam.ictd'}});
  assert.deepEqual(await get(saved.partEntityId),saved);report.reopened=await body(saved.partEntityId);
  report.passed=true;console.log('PASS R3 rounded stock full cross-period ellipse, exact editable source and preview/apply/reopen');
}catch(error){report.error=error.stack;console.error(error.stack);process.exitCode=1;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await bridge.close();}
