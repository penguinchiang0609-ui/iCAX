// Independent window native acceptance over the final frozen display/assembly contract.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
const repository=fileURLToPath(new URL('../../../',import.meta.url));
const binaryRoot=resolve(repository,'src/x64/Debug');
const runtimeRoot=resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT||binaryRoot);
const output=resolve(repository,'output/tests/assembly-process');
mkdirSync(output,{recursive:true});
const prepareOnly=process.argv.includes('--prepare-only');
const python=process.env.ICAX_TEST_PYTHON||resolve(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
const generator=resolve(repository,'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/AllProductsWindowNativeReference.py');
const scenarioFile=resolve(output,'all-products-recheck-window-native-scenarios.json');
const runPython=args=>{
 const result=spawnSync(python,['-B',generator,...args],{cwd:repository,windowsHide:true,encoding:'utf8',timeout:300000,maxBuffer:1024*1024,
  env:{...process.env,PYTHONDONTWRITEBYTECODE:'1',PYTHONIOENCODING:'utf-8'}});
 assert.equal(result.status,0,`Reference failed: ${result.error||result.stderr}`);
};
runPython(['--prepare-scenarios',scenarioFile]);
const scenarios=JSON.parse(readFileSync(scenarioFile,'utf8')).filter(c=>!process.env.ICAX_WINDOW_NATIVE_CASE||process.env.ICAX_WINDOW_NATIVE_CASE===c.name);
assert.ok(scenarios.length);
const reportFile=resolve(output,`all-products-recheck-window-native${prepareOnly?'-prepared':process.env.ICAX_WINDOW_NATIVE_CASE?'-'+process.env.ICAX_WINDOW_NATIVE_CASE:''}.json`);
const report={schema:'icax.all-products-recheck-window-native',schemaVersion:1,runtimeRoot,prepareOnly,startedAt:new Date().toISOString(),cases:[],passed:false};
const save=()=>writeFileSync(reportFile,JSON.stringify(report,null,2));
const digest=path=>createHash('sha256').update(readFileSync(path)).digest('hex');
function connection() {
  const child = spawn(resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
    || resolve(repository, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe')), [], {
    cwd: runtimeRoot, windowsHide: true,
    env: { ...process.env, PATH: `${binaryRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'],
  });
  let sequence = 0, stderr = '';
  const pending = new Map();
  const fail = error => {
    for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
    pending.clear();
  };
  child.stderr.on('data', value => { stderr += value; });
  child.on('error', fail);
  child.on('exit', code => { if (pending.size) fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { fail(new Error(line)); return; }
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    close() {
      return new Promise(resolveClosed => {
        if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
        child.once('exit', resolveClosed);
        child.stdin.end(); child.kill();
      });
    },
    invoke(method, payload = {}) {
      console.log(JSON.stringify({nativeRequest:method}));
      return new Promise((resolveRequest, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 600000);
        pending.set(id, { resolve: resolveRequest, reject, timer, method });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}


function sourceReference(scenario,descriptor){
 const requestFile=resolve(output,`all-products-recheck-window-native-${scenario.name}-host-input.json`);
 const referenceFile=resolve(output,`all-products-recheck-window-native-${scenario.name}-source-reference.json`);
 writeFileSync(requestFile,JSON.stringify({templateId:descriptor.id,template:descriptor,parameters:scenario.parameters},null,2));
 runPython([requestFile,referenceFile]);
 const reference=JSON.parse(readFileSync(referenceFile,'utf8'));
 for(const [path,hash] of Object.entries(reference.sourceDigests)){
  assert.equal(digest(resolve(repository,'src',path)),hash,`Frozen source changed: ${path}`);
  if(!prepareOnly){
   const normalized=path.replaceAll('\\','/');
   const deployed=normalized.startsWith('iCAX-Engine/framework/TemplateRuntime/python/')
    ?'runtime/template-python/'+normalized.slice('iCAX-Engine/framework/TemplateRuntime/python/'.length):normalized;
   assert.equal(digest(resolve(runtimeRoot,deployed)),hash,`Deployment differs: ${path}`);
  }
 }
 return {...reference,requestFile,referenceFile};
}
function rootReference(item){
 const ref=item.geometry||item.representations.result;
 return typeof ref==='string'?{resource:ref}:ref;
}
function genericProfileDialect(model){
 const copy=structuredClone(model),converted=[];
 for(const node of copy.resources||copy.geometry){
  if(node.operator!=='profile2d')continue;
  node.arguments.contours=node.arguments.contours.map(contour=>{
   if(contour.kind!=='polygon')return contour;
   const points=contour.points,curve={kind:'path',closed:true,segments:points.map((point,index)=>
     ({kind:'line',start:structuredClone(point),end:structuredClone(points[(index+1)%points.length])}))};
   assert.deepEqual(curve.segments.map(edge=>edge.start),points);
   assert.deepEqual(curve.segments.map(edge=>edge.end),points.slice(1).concat([points[0]]));
   converted.push({resourceKey:node.key,vertices:points.length});return curve;
  });
 }
 return {model:copy,evidence:{originalPayloadHash:createHash('sha256').update(JSON.stringify(model)).digest('hex'),
   genericPayloadHash:createHash('sha256').update(JSON.stringify(copy)).digest('hex'),
   convertedContours:converted.length,converted,originalVerticesWindingClosureAndDatumPreserved:true,
   onlyProfileCurveDialectChanged:true}};
}
const pathArea=curve=>Math.abs(curve.segments.reduce((sum,edge)=>sum+edge.start[0]*edge.end[1]-edge.end[0]*edge.start[1],0)/2);
async function referencePolygonControls(native,baseline){
 const originalNodes=baseline.resources||baseline.geometry;
 const profile=originalNodes.find(node=>node.operator==='profile2d'&&node.arguments.contours.some(curve=>curve.kind==='polygon'));
 if(!profile)return undefined;
 const extrusion=originalNodes.find(node=>node.operator==='extrude'&&node.inputs.length===1&&node.inputs[0]===profile.key);
 assert.ok(extrusion,'Observed polygon profile must have an actual historical extrusion');
 const control=structuredClone(baseline),item=structuredClone(control.items[0]);
 delete item.geometry;item.representations={result:{resource:extrusion.key}};item.children=[];
 control.items=[item];if(control.roots)control.roots=[item.key];
 const raw=shapeModel(control,[item.key],undefined,false),generic=genericProfileDialect(raw.model);
 const nodes=generic.model.resources||generic.model.geometry,convertedProfile=nodes.find(node=>node.key===profile.key);
 const area=pathArea(convertedProfile.arguments.contours[0])-convertedProfile.arguments.contours.slice(1).reduce((sum,curve)=>sum+pathArea(curve),0);
 const length=Math.hypot(...extrusion.arguments.vector),expectedVolume=area*length;
 const positive=(await native.invoke('InspectNeutralModel',{model:generic.model,geometryKeys:raw.keys})).geometryChecks[0];
 assert.ok(positive.valid&&positive.solids===1&&area>0&&length>0);
 assert.ok(Math.abs(positive.volume-expectedVolume)<=Math.max(1e-4,expectedVolume*1e-7),'Historical polygon area times actual extrusion length');
 const negative=structuredClone(generic.model),negativeNodes=negative.resources||negative.geometry;
 const edges=negativeNodes.find(node=>node.key===profile.key).arguments.contours[0].segments;
 const originalArea=pathArea(convertedProfile.arguments.contours[0]);let changed=false;
 for(let index=0;index<edges.length&&!changed;index++)for(let axis=0;axis<2&&!changed;axis++){
  edges[index].start[axis]+=0.5;edges[(index+edges.length-1)%edges.length].end[axis]+=0.5;
  if(Math.abs(pathArea({segments:edges})-originalArea)>1e-6)changed=true;
  else{edges[index].start[axis]-=0.5;edges[(index+edges.length-1)%edges.length].end[axis]-=0.5;}
 }
 assert.ok(changed);
 const bad=(await native.invoke('InspectNeutralModel',{model:negative,geometryKeys:raw.keys})).geometryChecks[0];
 assert.ok(bad.valid&&Math.abs(bad.volume-positive.volume)>Math.max(1e-4,positive.volume*1e-7),'Changed source vertex must yield distinguishable actual native geometry');
 return {...generic.evidence,normalizationChecksOnActualNativeGeometry:true,theoreticalSectionArea:area,
  actualAxisLength:length,theoreticalInitialVolume:expectedVolume,positiveNativeVolume:positive.volume,
  negativeVertexNativeVolume:bad.volume,negativeVertexControlDistinguishable:true};
}
// Inspect only actual selected items and their complete transitive resource closure.
function shapeModel(model,itemKeys,localMatrices,normalizeDialect=true,initialStockOffsets){
 const copy=structuredClone(model),nodes=copy.resources||copy.geometry;
 const items=new Map(copy.items.map(item=>[item.key,item])),selected=[];
 for(const key of itemKeys){
  const item=items.get(key),ref=rootReference(item);let root=ref.resource;
  if(ref.placement){root=`window.acceptance.instance.${key}`;nodes.push({key:root,operator:'transform',inputs:[ref.resource],arguments:{placement:ref.placement}});}
  if(localMatrices){
   const m=localMatrices.get(key);assert.ok(m,`Stock matrix ${key}`);
   const placement={xAxis:m.slice(0,3),yAxis:m.slice(4,7),zAxis:m.slice(8,11),
    origin:[0,1,2].map(column=>-[0,1,2].reduce((sum,row)=>sum+m[row*4+column]*m[row*4+3],0))};
   const local=`window.acceptance.local.${key}`;nodes.push({key:local,operator:'transform',inputs:[root],arguments:{placement}});root=local;
  }
  if(initialStockOffsets?.has(key)){
   const local=`window.acceptance.initial-stock-convention.${key}`;
   nodes.push({key:local,operator:'transform',inputs:[root],arguments:{placement:{
    origin:initialStockOffsets.get(key),xAxis:[1,0,0],yAxis:[0,1,0],zAxis:[0,0,1]}}});root=local;
  }
  item.representations={result:{resource:root}};delete item.geometry;item.children=[];selected.push(item);
 }
 const byKey=new Map(nodes.map(node=>[node.key,node])),needed=new Set();
 const include=key=>{if(needed.has(key))return;const node=byKey.get(key);assert.ok(node,`Missing resource ${key}`);needed.add(key);node.inputs.forEach(include);};
 selected.forEach(item=>include(item.representations.result.resource));
 copy.resources=nodes.filter(node=>needed.has(node.key));delete copy.geometry;copy.items=selected;
 copy.outputs=[{key:'result',purpose:'result',items:itemKeys,properties:{}}];
 if(copy.roots)copy.roots=itemKeys;copy.relationships=[];copy.tables=[];copy.extensions={};
 if(copy.processes)copy.processes=[];
 const generic=normalizeDialect?genericProfileDialect(copy):{model:copy};
 return {model:generic.model,keys:selected.map(item=>item.representations.result.resource),dialectEvidence:generic.evidence};
}
const fractions=[.017,.05,.25,.5,.75,.95,.983];
function assertMetrics(old,next,label){
 assert.ok(old.valid&&next.valid,label);assert.equal(next.solids,old.solids,label);
 assert.ok(Math.abs(next.volume-old.volume)<=Math.max(1e-4,old.volume*1e-7),`${label} volume ${next.volume} != ${old.volume}`);
 next.bounds.forEach((v,i)=>assert.ok(Math.abs(v-old.bounds[i])<=1e-5,`${label} bounds ${i}: ${v} != ${old.bounds[i]}`));
}
function samples(check){const result=[];for(const x of fractions)for(const y of fractions)for(const z of fractions)
 result.push([x,y,z].map((v,i)=>check.bounds[i*2]+v*(check.bounds[i*2+1]-check.bounds[i*2])));return result;}
function assertPoints(old,next,label){let checked=0,inside=0;
 assert.equal(old.pointChecks.length,next.pointChecks.length);
 for(const row of [old,next]){
  const states=new Set(row.pointChecks.map(point=>point.inside?'inside':point.outside?'outside':point.boundary?'boundary':'unknown'));
  assert.equal(row.classificationReuseControls,states.size,`${label} each classification state needs a fresh classifier control`);
 }
 old.pointChecks.forEach((point,i)=>{const actual=next.pointChecks[i];if(point.boundary||actual.boundary)return;
  assert.equal(actual.inside,point.inside,`${label} inside ${point.point}`);assert.equal(actual.outside,point.outside,`${label} outside ${point.point}`);
  checked++;if(point.inside)inside++;});return {checked,inside,freshClassifierControls:old.classificationReuseControls+next.classificationReuseControls};}
async function representativeShapes(native,baseline,current){
 const keys=baseline.items.map(item=>item.key),wanted=[];
 for(const pattern of ['access_door.fixed_frame.top.','access_door.fixed_frame.bottom.','access_door.leaf.frame.continuous.',
  'outer_frame.spatial.continuous','outer_frame.top.','outer_frame.left.','outer_frame.vertical.','main_grid.horizontal.','main_grid.vertical.']){
  const key=keys.find(key=>key.startsWith(pattern));if(key&&!wanted.includes(key))wanted.push(key);if(wanted.length===4)break;
 }
 assert.ok(wanted.length>=2);
 const previous=shapeModel(baseline,wanted),actual=shapeModel(current,wanted);
 const inspect=async(info,points,options={})=> (await native.invoke('InspectNeutralModel',{model:info.model,geometryKeys:info.keys,...options,...(points?{points}: {})})).geometryChecks;
 const before=await inspect(previous),after=await inspect(actual);let points=0,inside=0,freshClassifierControls=0;
 const currentItems=new Map(current.items.map(item=>[item.key,item]));
 const sourceCalls=current.extensions['tubeDesigner.assemblyProcessSource'].instances;
 const deferred=new Map(wanted.map(key=>[key,{
   secondaryCutters:currentItems.get(key).properties['tubeDesigner.assemblyProcessSecondaryCutters']||[],
   frozenStockProcesses:sourceCalls.filter(call=>Object.values(call.targets||{}).some(target=>
     (typeof target==='string'?target:target.stockId)===key)),
 }]));
 const hasDeferred=key=>deferred.get(key).secondaryCutters.length||deferred.get(key).frozenStockProcesses.length;
 const rawStageDifferences=[];
 const p1=[],p2=[];before.forEach((old,i)=>{
  if(hasDeferred(wanted[i])){rawStageDifferences.push({itemKey:wanted[i],
    deferredSecondaryCutters:deferred.get(wanted[i]).secondaryCutters.length,
    frozenStockProcessIds:deferred.get(wanted[i]).frozenStockProcesses.map(call=>call.instanceId),
    referenceFinalVolume:old.volume,executionInitialVolume:after[i].volume,volumeDifference:after[i].volume-old.volume,
    referenceFinalBounds:old.bounds,executionInitialBounds:after[i].bounds,
    reason:'Execution resource is initial stock; explicit frozen stock processes or secondary cutters must be consumed by the host before persistent BRep verification'});return;}
  assertMetrics(old,after[i],wanted[i]+' world');samples(old).forEach(point=>{
  p1.push({geometryKey:previous.keys[i],point});p2.push({geometryKey:actual.keys[i],point});});});
 const oldPoints=await inspect(previous,p1),newPoints=await inspect(actual,p2);
 oldPoints.forEach((old,i)=>{if(hasDeferred(wanted[i]))return;
  const result=assertPoints(old,newPoints[i],wanted[i]+' world');points+=result.checked;inside+=result.inside;freshClassifierControls+=result.freshClassifierControls;});
 const currentStockMatrices=new Map(current.extensions['tubeDesigner.assemblyProcessSource'].stocks.map(stock=>[stock.id,stock.matrix]));
 const stockMatrices=new Map(baseline.extensions['tubeDesigner.assemblyProcessSource'].stocks.map(stock=>[stock.id,stock.matrix]));
 for(const [key,matrix] of stockMatrices)matrix.forEach((value,index)=>assert.ok(Math.abs(value-currentStockMatrices.get(key)[index])<=1e-7,`${key} original/current stock pose ${index}`));
 let initialStockCoordinateConvention;
 const initialStockOffsets=new Map();
 if(previous.dialectEvidence.convertedContours>0){
  const initialBaseline=structuredClone(baseline),oldNodes=initialBaseline.resources||initialBaseline.geometry;
  for(const item of initialBaseline.items.filter(item=>wanted.includes(item.key))){
   const initial=`${item.key}.solid`;
   if(oldNodes.some(node=>node.key===initial)){
    delete item.geometry;item.representations={result:{resource:initial}};
   }else{
    assert.equal(oldNodes.find(node=>node.key===rootReference(item).resource)?.operator,'extrude',`${item.key} inline historical unprocessed stock extrusion`);
   }
  }
  const oldInitialWorld=shapeModel(initialBaseline,wanted),oldInitialStock=shapeModel(initialBaseline,wanted,stockMatrices);
  const oldInitialWorldChecks=await inspect(oldInitialWorld),oldInitialStockChecks=await inspect(oldInitialStock);
  const publicOptions=info=>({manufacturingCoordinates:true,manufacturingProperties:Object.fromEntries(
   info.keys.map((root,index)=>[root,currentItems.get(wanted[index]).properties]))});
  const normalizedOld=await inspect(oldInitialWorld,undefined,publicOptions(oldInitialWorld));
  const normalizedCurrent=await inspect(actual,undefined,publicOptions(actual));
  const worldOldPoints=[],worldCurrentPoints=[],normalizedOldPoints=[],normalizedCurrentPoints=[];
  wanted.forEach((key,index)=>{
   assertMetrics(oldInitialWorldChecks[index],after[index],`${key} original datum initial world`);
   assertMetrics(normalizedOld[index],normalizedCurrent[index],`${key} shared public normalizer initial stock`);
   samples(oldInitialWorldChecks[index]).forEach(point=>{worldOldPoints.push({geometryKey:oldInitialWorld.keys[index],point});worldCurrentPoints.push({geometryKey:actual.keys[index],point});});
   samples(normalizedOld[index]).forEach(point=>{normalizedOldPoints.push({geometryKey:oldInitialWorld.keys[index],point});normalizedCurrentPoints.push({geometryKey:actual.keys[index],point});});
   const b=oldInitialStockChecks[index].bounds;
   initialStockOffsets.set(key,[-b[0],-(b[2]+b[3])/2,-(b[4]+b[5])/2]);
  });
  const oldWorldPoints=await inspect(oldInitialWorld,worldOldPoints),actualWorldPoints=await inspect(actual,worldCurrentPoints);
  const oldNormalizedPoints=await inspect(oldInitialWorld,normalizedOldPoints,publicOptions(oldInitialWorld));
  const actualNormalizedPoints=await inspect(actual,normalizedCurrentPoints,publicOptions(actual));
  initialStockCoordinateConvention={basis:'TubeDesignerSDO.cpp:7127 BuildPunchBlank: initial stock X min=0; original profile Y/Z bbox center=0',
   source:'historical unprocessed .solid resource and original stock matrix, independently measured before cuts; never fitted to persisted or final geometry',
   publicNormalizerAppliedToBothWithActualSavedProperties:true,items:wanted.map((key,index)=>({itemKey:key,
    originalInitialStockBounds:oldInitialStockChecks[index].bounds,offset:initialStockOffsets.get(key),
    originalDatumWorld:assertPoints(oldWorldPoints[index],actualWorldPoints[index],`${key} initial world points`),
    sharedPublicNormalizer:assertPoints(oldNormalizedPoints[index],actualNormalizedPoints[index],`${key} normalized initial points`)}))};
 }
 const local=shapeModel(baseline,wanted,stockMatrices,true,initialStockOffsets),expected=await inspect(local);
 const persisted=await native.invoke('InspectNativeGeometry');const byKey=new Map(persisted.geometryChecks.map(check=>[check.key,check]));
 if(process.env.ICAX_WINDOW_NORMALIZER_DIAGNOSTIC==='1'){
  const options=info=>({manufacturingCoordinates:true,manufacturingProperties:Object.fromEntries(
   info.keys.map((root,index)=>[root,currentItems.get(wanted[index]).properties]))});
  const oldWorldNormalized=await inspect(previous,undefined,options(previous));
  const currentWorldNormalized=await inspect(actual,undefined,options(actual));
  const oldStockNormalized=await inspect(local,undefined,options(local));
  writeFileSync(resolve(output,'all-products-recheck-window-shifted-native-normalizer-diagnostic.json'),JSON.stringify({
   passed:false,source:'existing native public NormalizeLinearPartForManufacturing, actual saved execution item.properties unchanged',
   items:wanted.map((key,index)=>({itemKey:key,stockMatrix:stockMatrices.get(key),actualProperties:currentItems.get(key).properties,
    oldFinalWorld:before[index],currentInitialWorld:after[index],oldFinalStockLocal:expected[index],
    oldFinalWorldPublicNormalized:oldWorldNormalized[index],currentInitialWorldPublicNormalized:currentWorldNormalized[index],
    oldFinalStockPublicNormalized:oldStockNormalized[index],actualPersistent:byKey.get(key)}))},null,2));
 }
 const neutralPoints=[],nativePoints=[];
 expected.forEach((old,i)=>{const check=byKey.get(wanted[i]);assert.ok(check);assertMetrics(old,check,wanted[i]+' persisted');samples(old).forEach(point=>{
  neutralPoints.push({geometryKey:local.keys[i],point});nativePoints.push({entityId:check.entityId,point});});});
 const expectedPoints=await inspect(local,neutralPoints);
 const nativeChecks=new Map((await native.invoke('InspectNativeGeometry',{points:nativePoints})).geometryChecks.map(check=>[check.key,check]));
 let persistedPoints=0,persistedInside=0;const persistedComparisons=[];
 expectedPoints.forEach((old,i)=>{const actual=nativeChecks.get(wanted[i]),result=assertPoints(old,actual,wanted[i]+' persisted');
  persistedPoints+=result.checked;persistedInside+=result.inside;freshClassifierControls+=result.freshClassifierControls;
  persistedComparisons.push({itemKey:wanted[i],referenceFinalVolume:old.volume,persistedFinalVolume:actual.volume,
    referenceFinalBounds:old.bounds,persistedFinalBounds:actual.bounds,referenceSolids:old.solids,persistedSolids:actual.solids,
    checkedPoints:result.checked,interiorPoints:result.inside,freshClassifierControls:result.freshClassifierControls});});
 let changedNativePointControl;
 if(initialStockCoordinateConvention){
  const index=expectedPoints.findIndex(row=>row.pointChecks.some(point=>point.inside));assert.ok(index>=0);
  const old=expectedPoints[index],insidePoint=old.pointChecks.find(point=>point.inside),actual=nativeChecks.get(wanted[index]);
  const outsidePoint=[actual.bounds[1]+100,actual.bounds[3]+100,actual.bounds[5]+100];
  const check=(await native.invoke('InspectNativeGeometry',{points:[{entityId:actual.entityId,point:outsidePoint}]})).geometryChecks.find(row=>row.key===wanted[index]);
  assert.ok(check.pointChecks[0].outside);
  assert.throws(()=>assertPoints({...old,pointChecks:[insidePoint],classificationReuseControls:1},check,'changed actual native point control'),/inside/);
  changedNativePointControl={itemKey:wanted[index],referenceInsidePoint:insidePoint.point,changedActualNativePoint:outsidePoint,
   actualNativeOutside:true,strictComparisonRejected:true};
 }
 assert.ok(persistedPoints&&persistedInside);if(p1.length)assert.ok(points&&inside);
 return {parts:wanted.length,itemKeys:wanted,worldPoints:points,worldInside:inside,persistedPoints,persistedInside,
  volumesBoundsSolidsEqual:true,classificationsEqual:true,actualSourceAndPersistedCoordinatesChecked:true,
  worldRawComparisonOnlyForMaterializedItems:true,rawStageDifferences,persistedComparisons,freshClassifierControls,
  classifierReuseCheckedAgainstFreshPerState:true,baselineGenericDialect:previous.dialectEvidence,
  initialStockCoordinateConvention,changedNativePointControl};
}
try{
 for(const scenario of scenarios){
  const native=prepareOnly?undefined:connection(),result={name:scenario.name,passed:false};
  try{
   const source=JSON.parse(readFileSync(resolve(repository,'src/apps/tube-designer/templates/product/single_face_security_window/template.json'),'utf8'));
   const descriptor=prepareOnly?source:(await native.invoke('GetTemplateDescriptor',{templateId:source.id})).template;
   const reference=sourceReference(scenario,descriptor),parameters=structuredClone(scenario.parameters),frozen=structuredClone(parameters);
   result.templateId=descriptor.id;result.sourceChecks=reference.sourceChecks;result.declarations=reference.declarations;
   result.hostInputFile=reference.requestFile;result.sourceReferenceFile=reference.referenceFile;
   if(prepareOnly){result.prepared=true;}
   else{
    const preview=await native.invoke('GenerateProductTemplatePreview',{templateId:descriptor.id,parameters});
    assert.deepEqual(preview.parameters,frozen);assert.equal(preview.templateId,descriptor.id);
    assert.deepEqual(preview.items.map(i=>i.key||i.stableKey).sort(),reference.display.items.map(i=>i.key).sort());
    result.referencePolygonControls=await referencePolygonControls(native,reference.document);
    if(parameters.faceType!=='single'&&parameters.assemblyPlanningMode==='external_templates'){
     for(const key of ['frameManufacturingMode','horizontalBranchReserve','verticalBranchReserve','assemblyClearance'])
      assert.ok(parameterVisible(descriptor.parameters.find(field=>field.key===key),parameters),`${key} hidden active multi branch`);
     result.retainedExternalUsesVisibleEffectiveLegacyConditions=true;
    }
    let productEntityId;
    if(reference.planningCounterpart){
     const probe=await native.invoke('GeneratePreview',{templateId:descriptor.id,...reference.planningProbeParameters});
     productEntityId=probe.tubeDesigner.product.entityId;
     assert.deepEqual((await native.invoke('InspectScriptResources')).display.document,reference.display);
     const counterpart=await native.invoke('GeneratePreview',{templateId:descriptor.id,...reference.planningCounterpart,productEntityId});
     productEntityId=counterpart.tubeDesigner.product.entityId;
     assert.deepEqual((await native.invoke('InspectScriptResources')).display.document,reference.display);
     result.entireDisplayEqualAcrossPlanning=true;
    }
    const request={designModel:reference.display,manufacturingDefinition:reference.manufacturing};
    assert.deepEqual(await native.invoke('ValidateSharedManufacturing',request),
     {valid:true,designItemCount:reference.display.items.length,manufacturingPartCountKnown:false});
    const plan=await native.invoke('GetProductManufacturingPlan',{templateId:descriptor.id,parameters});assert.deepEqual(plan.parameters,frozen);
    const generated=await native.invoke('GeneratePreview',{templateId:descriptor.id,...parameters,...(productEntityId?{productEntityId}:{})});
    productEntityId=generated.tubeDesigner.product.entityId;
    await native.invoke('Disassemble',{productEntityIds:[productEntityId]});
    const snapshot=await native.invoke('InspectScriptResources'),current=snapshot.manufacturingExecution.document;
    writeFileSync(resolve(output,`all-products-recheck-window-native-${scenario.name}-actual-execution.json`),JSON.stringify(current));
    const actualProfileNodes=current.resources.filter(node=>node.operator==='profile2d');
    for(const node of actualProfileNodes)assert.ok(node.arguments.contours.every(curve=>curve.kind==='path'),`${node.key} actual executed geometry profile must be generic path`);
    result.actualExecutionGenericProfileNodes=actualProfileNodes.length;
    assert.deepEqual(snapshot.display.document,reference.display);assert.deepEqual(snapshot.manufacturing.document,reference.manufacturing);
    assert.deepEqual(current.items.map(i=>i.key).sort(),reference.document.items.map(i=>i.key).sort());
    const byKey=new Map(current.items.map(item=>[item.key,item]));
    const expectedByKey=new Map(reference.sourceExecution.items.map(item=>[item.key,item]));
    assert.deepEqual(current.extensions['tubeDesigner.assemblyProcessSource'],reference.sourceExecution.extensions['tubeDesigner.assemblyProcessSource'],
      'Native must consume the complete frozen stock and actual process plan');
    for(const item of reference.document.items){const p=byKey.get(item.key).properties;
     assert.ok(Math.abs(p.length-item.properties.length)<=.00051,`${item.key} actual material length`);
     for(const field of ['manufacturing.categoryKey','manufacturing.categoryName','manufacturing.material','manufacturing.materialGrade'])
      assert.equal(p[field],item.properties[field],`${item.key} ${field}`);
     assert.deepEqual(p['tubeDesigner.assemblyProcessSecondaryCutters']||[],
       expectedByKey.get(item.key).properties['tubeDesigner.assemblyProcessSecondaryCutters']||[],`${item.key} frozen deferred cutters`);
     assert.deepEqual(p['manufacturing.sourceMembers'],expectedByKey.get(item.key).properties['manufacturing.sourceMembers'],`${item.key} source coverage`);
    }
    result.shapes=await representativeShapes(native,reference.document,current);
    const shapes=await native.invoke('InspectNativeGeometry');
    await native.invoke('SaveAndReopen');assert.deepEqual(await native.invoke('InspectScriptResources'),snapshot);
    assert.deepEqual(await native.invoke('InspectNativeGeometry'),shapes);
    assert.deepEqual(parameters,frozen);result.parts=current.items.length;result.hostParametersEqual=true;
    result.nativePreviewAndManufacturingAccepted=true;result.sharedReferencesConsumed=true;result.exactSaveReopenPreserved=true;result.passed=true;
   }
   report.cases.push(result);save();console.log(JSON.stringify(result));
  }catch(error){result.error=error.stack;report.cases.push(result);save();throw error;}
  finally{await native?.close();}
 }
 report.finishedAt=new Date().toISOString();report.passed=!prepareOnly&&report.cases.every(c=>c.passed);save();
 console.log(JSON.stringify({cases:report.cases.length,passed:report.passed,prepareOnly,reportFile}));
}catch(error){report.error=error.message;report.finishedAt=new Date().toISOString();save();throw error;}
