// Editable source-domain wrapped construction, independently selected pieces,
// current native business transactions and fresh project persistence.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { repositoryRoot, startSideSketchNativeBridge } from './fixtures/sideSketchNativeBridge.mjs';
const output=resolve(repositoryRoot,'output/tests/cad-side-components');mkdirSync(output,{recursive:true});
const bridge=await startSideSketchNativeBridge(output),results=[];let serial=0;
const clone=value=>structuredClone(value);
const descriptor=JSON.parse(readFileSync(resolve(repositoryRoot,'src/apps/tube-designer/templates/profile/rect/profile.json'),'utf8'));
const parameters={...Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue])),width:60,depth:40,wallThickness:2,cornerRadius:0,innerRadius:0};
const profile=(await bridge.invoke('EvaluateProfilePackage',{profileRef:{scope:'system',id:'rect'},parameters},'product')).profile;
const get=id=>bridge.invoke('GetTubeCadDocument',{partEntityId:id,authoringView:'side'});
const circle=(id,cy,radius=6,cx=20)=>({id,kind:'circle',cx,cy,radius,closed:true});
const sketch=entities=>({schema:'icax.tube-sketch',schemaVersion:1,kind:'side',unit:'mm',coordinateSpace:'arc-length-axial',length:1000,perimeter:200,faceHeight:200,trajectoryWidth:1,entities});
const wrap=read=>read.geometry.SolidNodes.filter(n=>n.Data.type==='SWrappedVolumeNode');
const entities=read=>read.authoringView.sketch.entities;
const selector=read=>read.geometry.SolidNodes.find(n=>n.Data.type==='SConnectedComponentNode');
async function create(label,source){const added=await bridge.invoke('AddNestingSideSketchPart',{name:label,profileRef:{scope:'system',id:'rect'},parameters,length:1000,quantity:1,sketch:source});
  const reads=await Promise.all((added.partEntityIds??[added.partEntityId]).map(get));
  writeFileSync(resolve(output,'source-'+(++serial)+'.json'),JSON.stringify({label,source,reads},null,2));return reads;}
async function body(read){const published=await bridge.rpc({action:'geometry',payload:{partEntityId:read.partEntityId}});
  const evaluated=await bridge.rpc({action:'cad-document-geometry',payload:{partEntityId:read.partEntityId}});
  assert.equal(published.valid,true);assert.equal(evaluated.valid,true);
  assert.ok(evaluated.directedDifferenceVolume<=Math.max(1e-4,published.volume*1e-8),'Two independent BRep directions preserve all material');
  return {published,evaluated};}
async function apply(read,draft){const result=await bridge.invoke('ApplyTubeCadDocument',{partEntityId:read.partEntityId,resourceVersion:read.resourceVersion,revision:read.revision,
  authoringEdit:{mode:'side',sketch:draft,bindings:read.authoringView.bindings}});assert.ok(result.snapshot?.tubeDesigner);
  const saved=await get(read.partEntityId);assert.equal(saved.documentResourceId,read.documentResourceId);assert.ok(Number(saved.revision)>Number(read.revision));return saved;}
async function preview(read,draft,label){const key='side-component-'+label;
  const result=await bridge.invoke('PreviewTubeCadDocument',{partEntityId:read.partEntityId,resourceVersion:read.resourceVersion,revision:read.revision,previewResourceKey:key,
    authoringEdit:{mode:'side',sketch:draft,bindings:read.authoringView.bindings}});
  assert.ok(result.geometry?.url);assert.deepEqual(await get(read.partEntityId),read,'Runtime preview must not alter the authoritative piece');
  const released=await bridge.invoke('ReleaseTubeCadDocumentPreview',{previewResourceKey:key});assert.ok(released.resourceCount>0);
  await assert.rejects(bridge.rpc({action:'resource',payload:result.geometry}),/not present|不存在|缺失/);}
async function reopen(read,label){const file='side-components-'+label+'.ictd';await bridge.rpc({action:'save',payload:{file}});await bridge.rpc({action:'open',payload:{file}});
  assert.deepEqual(await get(read.partEntityId),read,'Fresh binary repository reopen retains full source graph, exact curves and unique anchor');return {file,...await body(read)};}
async function check(label,action){console.log('START '+label);try{results.push({case:label,passed:true,...await action()});console.log('PASS '+label);}
  catch(error){results.push({case:label,passed:false,error:error.stack});console.log('FAIL '+label+': '+error.message);}}
try{
  await check('Nonzero axial trim retains original circle stations and editable source stock domain',async()=>{
    const source=sketch([circle('source-hole',250)]);source.endCuts={start:{position:100,angleDegrees:0,rotationDegrees:0},end:{position:900,angleDegrees:0,rotationDegrees:0}};
    const [read]=await create('源管截取100到900',source);assert.equal(wrap(read).length,1);assert.equal(entities(read)[0].cy,250);
    assert.equal(read.authoringView.reference.length,1000);assert.equal(read.authoringView.reference.actualPiece.sourceAxialFirst,100);
    const original=await body(read);assert.ok(Math.abs(read.authoringView.reference.actualPiece.bounds.width-800)<1e-6);
    const draft=clone(read.authoringView.sketch);draft.entities[0].radius=7;draft.entities.push(circle('new-source-hole',600,4));
    await preview(read,draft,'trim');const saved=await apply(read,draft);assert.ok(entities(saved).some(e=>e.cy===250&&e.radius===7));
    assert.ok(entities(saved).some(e=>e.cy===600&&e.radius===4));assert.ok(wrap(saved).some(n=>n.ID===wrap(read)[0].ID));
    return {id:saved.partEntityId,sourceDomain:saved.authoringView.reference.sourceDomain,actualPiece:saved.authoringView.reference.actualPiece,...await reopen(saved,'trim')};
  });
  await check('Split pieces preserve the full editable graph and select their own material after separator edits',async()=>{
    const source=sketch([circle('left-hole',200),circle('right-hole',800),{id:'ring-band',kind:'rectangle',x:0,y:490,width:200,height:20,closed:true}]);source.splitParts=true;
    const reads=await create('源管环带分件',source);assert.equal(reads.length,2);
    const sorted=reads.sort((a,b)=>a.authoringView.reference.actualPiece.sourceAxialFirst-b.authoringView.reference.actualPiece.sourceAxialFirst);
    for(const read of sorted){assert.ok(selector(read));assert.equal(wrap(read).length,3);assert.equal(entities(read).length,3);
      assert.equal(read.authoringView.reference.length,1000);assert.ok(entities(read).some(e=>e.cy===200)&&entities(read).some(e=>e.cy===800));
      assert.ok(read.geometry.SolidNodes.every(n=>!['SResidualBRepNode','SCompositeVolumeNode'].includes(n.Data.type)),'A selected piece uses source CAD construction, never a frozen finished BRep');await body(read);}
    const right=sorted[1],left=sorted[0],before=await body(right),draft=clone(right.authoringView.sketch);
    const band=draft.entities.find(e=>e.kind==='path'&&e.segments.length===4);assert.ok(band);
    for(const segment of band.segments){segment.y1+=10;segment.y2+=10;}
    draft.entities.find(e=>e.kind==='circle'&&e.cy===800).radius=7;
    await preview(right,draft,'split');const saved=await apply(right,draft),after=await body(saved);
    assert.ok(Math.abs(saved.authoringView.reference.actualPiece.bounds.width-(right.authoringView.reference.actualPiece.bounds.width-10))<1e-5,'Piece boundary reevaluates from the edited band rather than fixed slabs');
    assert.deepEqual(await get(left.partEntityId),left,'An independent sibling piece document is unchanged');
    assert.equal(selector(saved).ID,selector(right).ID);assert.deepEqual(selector(saved).Data.value.Anchor,selector(right).Data.value.Anchor);
    const outside=clone(saved.geometry);outside.SolidNodes.find(n=>n.ID===selector(saved).ID).Data.value.Anchor={X:-99999,Y:0,Z:0};
    await assert.rejects(bridge.invoke('ApplyTubeCadDocument',{partEntityId:saved.partEntityId,resourceVersion:saved.resourceVersion,revision:saved.revision,geometry:outside}),/anchor|锚点/i);
    assert.deepEqual(await get(saved.partEntityId),saved,'Invalid anchor failure is atomic');assert.deepEqual(await body(saved),after);
    return {ids:sorted.map(r=>r.partEntityId),selectionId:selector(saved).ID,...await reopen(saved,'split')};
  });
  await check('Cross-period circle and ellipse remain the original complete exact editable curves',async()=>{
    const source=sketch([circle('period-circle',250,8,195),{id:'period-ellipse',kind:'ellipse',cx:195,cy:500,radiusX:15,radiusY:22,rotation:.2,closed:true},circle('next-period',750,6,220)]);
    const [read]=await create('跨周期完整精确源图',source);assert.equal(wrap(read).length,3);assert.ok(wrap(read).every(n=>n.Data.value.UVRegion.Boundaries.length===1));
    assert.equal(entities(read).length,3);assert.ok(entities(read).some(e=>e.kind==='circle'&&e.cx===195));assert.ok(entities(read).some(e=>e.kind==='circle'&&e.cx===220));
    const ellipse=entities(read).find(e=>e.kind==='ellipse');assert.ok(ellipse);assert.equal(ellipse.closed,true);
    assert.equal(ellipse.radiusX,22);assert.equal(ellipse.radiusY,15);assert.ok(Math.abs(ellipse.rotation-(.2+Math.PI/2))<1e-8);
    const draft=clone(read.authoringView.sketch);draft.entities.find(e=>e.kind==='circle'&&e.cx===195).radius=9;
    const retained=clone(wrap(read).find(n=>n.Data.value.UVRegion.Boundaries[0].Curves[0].Segment.Curve.type==='Ellipse2'));
    await preview(read,draft,'period');const saved=await apply(read,draft);assert.deepEqual(saved.geometry.SolidNodes.find(n=>n.ID===retained.ID),retained);
    return {id:saved.partEntityId,...await reopen(saved,'period')};
  });
  await check('Even-odd geometric loops retain holes and island pieces without a second sketch recipe',async()=>{
    const source=sketch([{...circle('outside-counter',400,14),fillGroup:'counter',fillRule:'evenodd'},
      {...circle('inside-counter',400,7),fillGroup:'counter',fillRule:'evenodd'}]);source.splitParts=true;
    const reads=await create('偶奇环岛分件',source);assert.equal(reads.length,2);
    for(const read of reads){assert.equal(wrap(read).length,1);assert.equal(wrap(read)[0].Data.value.UVRegion.Boundaries.length,2);
      assert.equal(entities(read).length,2);assert.ok(entities(read).every(e=>e.fillGroup===wrap(read)[0].ID&&e.fillRule==='evenodd'));await body(read);}
    const bulk=reads.find(r=>r.authoringView.reference.actualPiece.sourceAxialLast-r.authoringView.reference.actualPiece.sourceAxialFirst>900);
    assert.ok(bulk);const sibling=reads.find(r=>r.partEntityId!==bulk.partEntityId),draft=clone(bulk.authoringView.sketch);
    draft.entities.find(e=>e.radius===7).radius=8;await preview(bulk,draft,'evenodd');const saved=await apply(bulk,draft);
    assert.equal(wrap(saved)[0].ID,wrap(bulk)[0].ID);assert.equal(wrap(saved)[0].Data.value.UVRegion.Boundaries.length,2);
    assert.deepEqual(await get(sibling.partEntityId),sibling);return {ids:reads.map(r=>r.partEntityId),...await reopen(saved,'evenodd')};
  });
  await check('Selected exact 2D bevel segment survives reopen and angle editing without expanding its boundary',async()=>{
    const source=sketch([{id:'selected-wall-edge',kind:'path',closed:true,segments:[
      {kind:'line',x1:10,y1:420,x2:25,y2:420},{kind:'line',x1:25,y1:420,x2:25,y2:460},
      {kind:'line',x1:25,y1:460,x2:10,y2:460},{kind:'line',x1:10,y1:460,x2:10,y2:420}],
      bevel:{enabled:true,side:'outer',angleDegrees:45,rootFace:.5,segmentIndices:[1]}}]);
    const [read]=await create('单段精确侧壁坡口',source);assert.deepEqual(entities(read)[0].bevel.segmentIndices,[1]);
    const original=read.geometry.SolidNodes.find(n=>n.Data.type==='STopologyBevelNode').Data.value.Selections[0].UVBoundary;assert.equal(original.length,1);
    await reopen(read,'selected-bevel-first');const draft=clone(read.authoringView.sketch);draft.entities[0].bevel.angleDegrees=30;
    await preview(read,draft,'selected-bevel');const saved=await apply(read,draft);assert.deepEqual(entities(saved)[0].bevel.segmentIndices,[1]);
    const changed=saved.geometry.SolidNodes.find(n=>n.Data.type==='STopologyBevelNode').Data.value.Selections[0];assert.equal(changed.AngleDegrees,30);
    assert.deepEqual(changed.UVBoundary,original,'Changing angle preserves the exact one-segment geometric binding');return {id:saved.partEntityId,...await reopen(saved,'selected-bevel')};
  });
}finally{const report={passed:results.every(r=>r.passed),checks:results.length,results,tubeDesignerSha256:bridge.dllHash,openCascadeSha256:bridge.importerHash,
  completionClaim:false,browserInteraction:false,scope:'Registered native source-domain wrapped CAD and strict material component selection',requests:bridge.requests};
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await bridge.close();}
assert.ok(results.every(r=>r.passed),'Native side-source/component acceptance failed');
