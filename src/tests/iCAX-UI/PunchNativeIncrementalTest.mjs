// Real native preview benchmark and geometry comparison. Every host owns an
// isolated scene and settings directory; no final part is created.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, delimiter } from 'node:path';
import { pathToFileURL } from 'node:url';

const runtime=resolve(process.env.ICAX_PUNCH_BATCH_NATIVE_RUNTIME??'src/x64/Debug');
const bridge=resolve(process.env.ICAX_PUNCH_BATCH_NATIVE_BRIDGE??'output/tests/punch-batch-20261007/native-bridge/BatchExcelAutomationScopeBridge.exe');
const output=resolve(process.env.ICAX_NATIVE_INCREMENTAL_REPORT??'output/tests/punch-native-incremental/baseline');
const comparison=process.env.ICAX_NATIVE_INCREMENTAL_COMPARE;
mkdirSync(output,{recursive:true});mkdirSync(resolve(output,'resources'),{recursive:true});
const env={...process.env,ICAX_AUTOMATION_USER_DATA:resolve(output,'native-user-data')};
const originalPath=Object.entries(env).find(([key])=>key.toLowerCase()==='path')?.[1]??'';
for(const key of Object.keys(env))if(key.toLowerCase()==='path')delete env[key];
env.Path=runtime+delimiter+originalPath;
const host=spawn(bridge,[],{cwd:runtime,env,windowsHide:true,stdio:['pipe','pipe','pipe']});
let serial=0,stderr='';const pending=new Map(),calls=[],resources=new Map();
host.stderr.on('data',bytes=>stderr+=bytes);
function failJobs(error){for(const job of pending.values()){clearTimeout(job.timer);job.reject(error);}pending.clear();}
host.on('error',failJobs);host.on('exit',code=>failJobs(new Error('Native host exited: '+code)));
createInterface({input:host.stdout}).on('line',line=>{
  let reply;try{reply=JSON.parse(line);}catch{stderr+=line+'\n';return;}
  const job=pending.get(reply.id);if(!job)return;pending.delete(reply.id);clearTimeout(job.timer);
  const elapsed=performance.now()-job.started;
  const saved=reply.result?.base64?{...reply,result:{...reply.result,base64:undefined}}:reply;
  calls.push({request:job.request,response:saved,milliseconds:elapsed});
  reply.ok?job.resolve({result:reply.result,milliseconds:elapsed}):job.reject(new Error(reply.error));
});
function invoke(scope,method,payload={}){
  const request={id:++serial,scope,method,payload};return new Promise((resolveResult,reject)=>{
    const timer=setTimeout(()=>{pending.delete(request.id);reject(new Error('Native timeout: '+method));},90000);
    pending.set(request.id,{request,started:performance.now(),resolve:resolveResult,reject,timer});host.stdin.write(JSON.stringify(request)+'\n');
  });
}
const {parseRenderGeometryResource}=await import(pathToFileURL(resolve(runtime,'iCAX-UI/SDK/Viewport/renderResource.mjs')).href);
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
function meshStats(bytes){
  const buffer=bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),mesh=parseRenderGeometryResource(buffer);
  const p=mesh.positions,idx=mesh.indices??[],bounds={min:[Infinity,Infinity,Infinity],max:[-Infinity,-Infinity,-Infinity]};
  for(let i=0;i<p.length;i++){
    assert(Number.isFinite(p[i]),'Every mesh position is finite');const axis=i%3;
    bounds.min[axis]=Math.min(bounds.min[axis],p[i]);bounds.max[axis]=Math.max(bounds.max[axis],p[i]);
  }
  let area=0;const triangles=[];
  for(let i=0;i<idx.length;i+=3){
    const points=[idx[i],idx[i+1],idx[i+2]].map(v=>[p[v*3],p[v*3+1],p[v*3+2]]);
    const a=points[1].map((x,j)=>x-points[0][j]),b=points[2].map((x,j)=>x-points[0][j]);
    area+=Math.hypot(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])/2;
    triangles.push(points.map(point=>point.map(v=>Math.round(v*1e5)).join(',')).sort().join(';'));
  }
  return {kind:mesh.kind,vertices:p.length/3,triangles:idx.length/3,bounds,area,
    canonicalTriangleHash:sha(triangles.sort().join('|'))};
}
async function readReference(reference){
  // Read every publication even when its URL/version repeats: a cached client
  // entry cannot establish that the actual native resource still exists or is
  // the same mesh after another preview updates/discards resources.
  if(!reference?.url)return null;const key=reference.url+'@'+reference.version;
  const {result}=await invoke('inspection','ReadResource',{url:reference.url,version:reference.version});
  const bytes=Buffer.from(result.base64,'base64');assert.equal(bytes.length,result.bytes);
  const file=sha(key+'#'+sha(bytes))+'.bin';writeFileSync(resolve(output,'resources',file),bytes);
  const item={url:reference.url,version:reference.version,bytes:bytes.length,sha256:sha(bytes),file,
    ...(bytes.toString('ascii',4,8)==='ICRG'?{mesh:meshStats(bytes)}:{})};resources.set(key,item);return item;
}
const report={scope:'Actual scene SDO/native flatbuffer resources in an isolated host; WebGL screenshots display those saved resources. No independent desktop CEF control.',
  runtime,bridge,comparison,cases:[],calls,checks:[],screenshots:[]};
let browser;
try{
  const modules=(await invoke('inspection','GetRuntimeModules')).result.modules;
  report.modules=Object.fromEntries(Object.entries(modules).map(([name,path])=>[name,{path,sha256:sha(readFileSync(path))}]));
  await invoke('inspection','ResetScene');
  const tools=(await invoke('scene','GetPunchTools')).result.tools;
  const catalogue=new Map(tools.map(tool=>[tool.id,tool]));
  const ref=id=>({id,version:catalogue.get(id)?.version,digest:catalogue.get(id)?.digest});
  const clone=value=>structuredClone(value);
  const circle=(id,station,diameter=10)=>({id,recordKind:'tool',type:'circle',enabled:true,
    toolRef:ref('circle'),toolParameters:{diameter},diameter,station,face:'top',reference:'start',layoutDatum:'base',
    blindHole:false,cutDepth:5,opposite:false,rotation:0,offset:0,arrayCount:1,arrayPitch:50,rowCount:1,rowPitch:20,
    arrayOffsets:[0],rowOffsets:[0]});
  const request=features=>({profileRef:{scope:'system',id:'round'},parameters:{width:40,wallThickness:2,innerOffsetX:0,innerOffsetY:0},
    length:1000,name:'增量原生验收',quantity:1,toolsOnly:true,individualToolPreviews:true,
    ends:{start:{type:'keep'},end:{type:'keep'}},features:clone(features)});
  const list=Array.from({length:11},(_,i)=>circle('incremental-'+i,70+i*80));
  const sequence=[0,1,5,10,11].map(count=>({name:'circle-'+count,payload:request(list.slice(0,count)),expected:count}));
  sequence.push({name:'circle-11-repeat',payload:request(list),expected:11});
  const moved=clone(list);moved[4].station+=15;
  sequence.push({name:'circle-11-one-moved',payload:request(moved),expected:11});
  const small=request(list.slice(0,2));
  sequence.push({name:'two-circle-base',payload:clone(small),expected:2});
  const longer=clone(small);longer.length=1200;
  sequence.push({name:'two-circle-length',payload:longer,expected:2});
  const wider=clone(longer);wider.parameters.width=48;
  sequence.push({name:'two-circle-main-dimension',payload:wider,expected:2});
  const larger=clone(wider);larger.features[0].toolParameters.diameter=14;larger.features[0].diameter=14;
  sequence.push({name:'two-circle-tool-parameter',payload:larger,expected:2});
  const endChanged=clone(small);endChanged.ends.start={type:'template',datum:'long',rotation:0,trim:8,toolRef:ref('end-miter'),toolParameters:{angle:45}};
  sequence.push({name:'two-circle-end-miter',payload:endChanged,expected:3});
  const blind=request([circle('blind',300)]);blind.features[0].blindHole=true;blind.features[0].cutDepth=1;
  sequence.push({name:'circle-blind',payload:blind,expected:1});
  const opposite=clone(blind);opposite.features[0].opposite=true;
  sequence.push({name:'circle-blind-opposite',payload:opposite,expected:2});
  const slot=request([{...circle('slot',500),type:'slot',toolRef:ref('slot'),toolParameters:{spanAlong:36,spanAcross:10},spanAlong:36,spanAcross:10}]);
  sequence.push({name:'slot-template',payload:slot,expected:1});
  const slotSize=clone(slot);slotSize.features[0].toolParameters.spanAlong=44;slotSize.features[0].spanAlong=44;
  sequence.push({name:'slot-template-parameter',payload:slotSize,expected:1});
  const arrays=request([circle('two-dimensional',300,6)]),f=arrays.features[0];
  f.arrayGroups=[{id:'length',type:'linear',axis:'X',count:2,spacing:100},{id:'around',type:'polar',axis:'X',count:3,angleMode:'pitch',angleStep:120,origin:[0,0,0]}];
  f.arrayCandidateCount=6;f.arrayTransforms=[];
  for(const x of [0,100])for(const angle of [0,120,240]){
    const c=Math.cos(angle*Math.PI/180),s=Math.sin(angle*Math.PI/180);f.arrayTransforms.push([1,0,0,x,0,c,-s,0,0,s,c,0,0,0,0,1]);
  }
  sequence.push({name:'two-dimensional-around-tube',payload:arrays,expected:6});
  const referenceEnd=request([{...circle('from-end',100),reference:'end'}]);
  sequence.push({name:'circle-reference-end',payload:referenceEnd,expected:1});
  const referenceEndLong=clone(referenceEnd);referenceEndLong.length=1200;
  sequence.push({name:'circle-reference-end-length',payload:referenceEndLong,expected:1});
  const noIndividual=request([circle('merged-default',250)]);delete noIndividual.individualToolPreviews;
  sequence.push({name:'default-api-merged',payload:noIndividual,expected:1});
  for(const item of sequence){
    const {result,milliseconds}=await invoke('scene','PreviewPunchWizard',item.payload);
    assert.equal(result.placedToolCount??result.toolCount,item.expected,item.name+' actual placements');
    assert.equal(result.toolsOnly,true);assert(result.baseGeometry?.url);
    const geometries={blank:await readReference(result.baseGeometry)};
    for(const tool of result.toolPreviews??[])geometries[tool.target+':'+tool.key]=await readReference(tool.geometry);
    if(result.toolGeometry)geometries.merged=await readReference(result.toolGeometry);
    await readReference(result.baseMaterial);await readReference(result.toolMaterial);
    const saved={...item,milliseconds,response:result,geometries};report.cases.push(saved);
    console.log(item.name+': '+milliseconds.toFixed(1)+' ms / '+item.expected+' tools');
    // Write progress after each actual call so a failed run still has evidence.
    writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));
  }
  const byName=name=>report.cases.find(item=>item.name===name);
  const initial=byName('circle-11'),repeat=byName('circle-11-repeat'),move=byName('circle-11-one-moved');
  assert.deepEqual(repeat.response.baseGeometry,initial.response.baseGeometry);
  for(const key of Object.keys(initial.geometries).filter(key=>key.startsWith('feature:'))){
    assert.deepEqual(repeat.geometries[key].mesh,initial.geometries[key].mesh);
    if(key!=='feature:incremental-4')assert.deepEqual(move.geometries[key].mesh,initial.geometries[key].mesh);
  }
  assert.notEqual(move.geometries['feature:incremental-4'].mesh.canonicalTriangleHash,initial.geometries['feature:incremental-4'].mesh.canonicalTriangleHash);
  assert.notEqual(byName('two-circle-length').geometries.blank.mesh.canonicalTriangleHash,byName('two-circle-base').geometries.blank.mesh.canonicalTriangleHash);
  assert.notEqual(byName('two-circle-main-dimension').geometries.blank.mesh.canonicalTriangleHash,byName('two-circle-length').geometries.blank.mesh.canonicalTriangleHash);
  assert.notEqual(byName('two-circle-tool-parameter').geometries['feature:incremental-0'].mesh.canonicalTriangleHash,
    byName('two-circle-main-dimension').geometries['feature:incremental-0'].mesh.canonicalTriangleHash);
  assert.notEqual(byName('slot-template-parameter').geometries['feature:slot'].mesh.canonicalTriangleHash,byName('slot-template').geometries['feature:slot'].mesh.canonicalTriangleHash);
  assert.notEqual(byName('circle-reference-end').geometries['feature:from-end'].mesh.canonicalTriangleHash,
    byName('circle-reference-end-length').geometries['feature:from-end'].mesh.canonicalTriangleHash);
  assert(byName('default-api-merged').response.toolGeometry?.url,'Default API retains merged tool geometry');
  report.checks.push('Actual finite mesh geometry/placement counts for fixed 0/1/5/10/11 sequence, repeats, one moved tool, blank length/size, dimensions, ends, blind/opposite, slot and circumferential 2D array');
  report.checks.push('Unchanged tools have equivalent actual mesh; changed records, blank, template sizes and end-based length visibly alter their corresponding geometry');
  if(comparison){
    const before=JSON.parse(readFileSync(resolve(comparison),'utf8'));
    report.comparison=report.cases.map(item=>{
      const previous=before.cases.find(candidate=>candidate.name===item.name);assert(previous,item.name+' baseline exists');
      // JSON is the actual bridge transport (including normalizing JS -0).
      assert.deepEqual(JSON.parse(JSON.stringify(item.payload)),previous.payload,'Before/after use identical native inputs for '+item.name);
      assert.deepEqual(Object.keys(item.geometries).filter(key=>key!=='merged').sort(),Object.keys(previous.geometries).filter(key=>key!=='merged').sort());
      for(const [key,data] of Object.entries(item.geometries).filter(([key])=>key!=='merged')){
        const old=previous.geometries[key].mesh;assert.deepEqual(data.mesh,old,'Actual native mesh matches baseline for '+item.name+'/'+key);
      }
      return {name:item.name,beforeMilliseconds:previous.milliseconds,afterMilliseconds:item.milliseconds,
        speedup:previous.milliseconds/item.milliseconds,meshEquivalent:true};
    });
    report.checks.push('Every before/after case uses identical inputs and matches canonical triangles, vertices, bounds and mesh area');
    // Native reuse counters distinguish incremental geometry work from a
    // faster full rebuild. These are reported by the real SDO, not inferred
    // from elapsed time or URL equality.
    for(const name of ['circle-1','circle-5','circle-10','circle-11']){
      const item=byName(name),count=item.expected,previous=name==='circle-1'?0:name==='circle-5'?1:name==='circle-10'?5:10;
      assert.equal(item.response.previewReuse.base,true,name+' reuses its blank');
      assert.equal(item.response.previewReuse.featureHits,previous,name+' reuses unchanged features');
      assert.equal(item.response.previewReuse.featureMisses,count-previous,name+' builds only new features');
      assert.equal(item.response.toolGeometry,undefined,'Explicit individual preview avoids duplicate merged mesh');
    }
    assert.equal(repeat.response.previewReuse.featureHits,11);assert.equal(repeat.response.previewReuse.featureMisses,0);
    assert.equal(move.response.previewReuse.featureHits,10);assert.equal(move.response.previewReuse.featureMisses,1);
    assert.equal(repeat.response.previewReuse.displayMisses,0);
    report.checks.push('Actual native cache reuses the blank and all unchanged feature shapes; repeat has zero feature/display misses, and moving one of eleven rebuilds exactly one');

    report.resourceProbes=[];
    const previewProbe=async(name,payload,expectedMeshes)=>{
      const {result,milliseconds}=await invoke('scene','PreviewPunchWizard',payload);
      const meshes={blank:await readReference(result.baseGeometry)};
      for(const tool of result.toolPreviews??[])meshes[tool.target+':'+tool.key]=await readReference(tool.geometry);
      if(expectedMeshes)for(const [key,item] of Object.entries(expectedMeshes).filter(([key])=>key!=='merged'))
        assert.deepEqual(meshes[key].mesh,item.mesh,name+' actual published geometry for '+key);
      const item={name,payload:clone(payload),milliseconds,response:result,geometries:meshes};report.resourceProbes.push(item);
      console.log(name+': '+milliseconds.toFixed(1)+' ms');return item;
    };
    let warm=await previewProbe('warm-before-resource-probes',small,byName('two-circle-base').geometries);
    const warmMeshes=clone(warm.geometries);
    const toolReference=()=>warm.response.toolPreviews.find(tool=>tool.key==='incremental-0').geometry;
    const sourceURL=(await invoke('inspection','GetResourceInfo',{namedKey:'tube-designer/punch-preview/tool/side/incremental-0'})).result.url;
    assert.equal((await invoke('inspection','GetResourceInfo',{url:sourceURL})).result.exists,true);
    for(const [name,url] of [['current-display-version',toolReference().url],['current-source-version',sourceURL]]){
      const mutation=(await invoke('inspection','BumpResourceVersion',{url})).result;
      assert(mutation.version>mutation.previousVersion);
      warm=await previewProbe(name,small,warmMeshes);
      assert.equal(warm.response.previewReuse.featureMisses,0,name+' retains correct feature shape');
      assert(warm.response.previewReuse.displayMisses>=1,name+' rebuilds stale published resources');
    }
    const removedDisplay=(await invoke('inspection','DiscardResource',{url:toolReference().url})).result;
    assert.equal(removedDisplay.mutation,2);assert.equal(removedDisplay.exists,false);
    warm=await previewProbe('deleted-display',small,warmMeshes);
    assert(warm.response.previewReuse.displayMisses>=1);assert.equal(warm.response.previewReuse.featureMisses,0);
    assert.equal((await invoke('inspection','DiscardResource',{url:toolReference().url})).result.mutation,2);
    const removedSource=(await invoke('inspection','DiscardResource',{url:sourceURL})).result;
    assert.equal(removedSource.mutation,2);assert.equal(removedSource.exists,false);
    warm=await previewProbe('deleted-source-and-display',small,warmMeshes);
    assert(warm.response.previewReuse.displayMisses>=1);assert.equal(warm.response.previewReuse.featureMisses,0);
    report.checks.push('After a warm hit, replacing current source/display versions or deleting their actual resources rebuilds publication; every mesh is freshly read and equals the baseline');

    await invoke('inspection','ClearResources');
    const different=clone(small);different.parameters.width=48;
    const other=await previewProbe('resource-pool-recreated-different-blank',different);
    assert.notEqual(other.geometries.blank.mesh.canonicalTriangleHash,warmMeshes.blank.mesh.canonicalTriangleHash);
    const recreated=await previewProbe('resource-pool-recreated-original-blank',small,warmMeshes);
    assert(recreated.response.previewReuse.displayMisses>=3,'Same URL/version with a different pool object cannot masquerade as current cached geometry');
    report.checks.push('Resource pool clear/recreation with unchanged scene IDs cannot reuse another blank at the same stable URL/version; original geometry is republished correctly');

    const tooMany=clone(small);
    for(const feature of tooMany.features){
      feature.arrayOffsets=Array.from({length:501},(_,index)=>index);feature.rowOffsets=[0];feature.arrayCount=501;
    }
    const rejected=await invoke('scene','PreviewPunchWizard',tooMany);
    assert.match(rejected.result.resultError??'',/1000/,'Warm cache must still enforce total candidates across features');
    assert.equal(rejected.result.previewToolsComplete,false);
    assert.equal(rejected.result.toolCount,0,'Candidate budget rejects before a partial set of hundreds of tools is published');
    assert.equal(rejected.result.toolPreviews?.length??0,0);
    assert(rejected.milliseconds<2000,'Candidate budget is checked before expensive repeated CAD work');
    report.semanticRejections=[{name:'cross-feature-candidate-budget',milliseconds:rejected.milliseconds,error:rejected.result.resultError}];
    console.log('cross-feature-candidate-budget: '+rejected.milliseconds.toFixed(1)+' ms');
    const outdated=clone(small);outdated.features[0].toolRef.digest='outdated-cache-digest';
    let invalid,invalidTransportError;
    try { invalid=await invoke('scene','PreviewPunchWizard',outdated); }
    catch(error) { invalidTransportError=error; }
    if(invalid) {
      assert.match(invalid.result.resultError??'',/版本|摘要|更改|digest|资源|一致|匹配|退化定式刀具/i);
      assert.equal(invalid.result.previewToolsComplete,false);
      assert.equal(invalid.result.toolCount,0);
      assert.equal(invalid.result.toolPreviews?.length??0,0);
      report.semanticRejections.push({name:'outdated-tool-digest',milliseconds:invalid.milliseconds,error:invalid.result.resultError});
      report.expectedNativeFailures=0;
    } else {
      assert.match(invalidTransportError.message,/版本|摘要|更改|digest|资源|一致|匹配|退化定式刀具/i,'Warm cache does not bypass current tool reference validation');
      report.expectedNativeFailures=1;
    }
    report.checks.push('Warm-cache requests still reject more than 1000 total candidates across records and an outdated tool reference digest before geometry publication');
  }
  assert.equal((await invoke('scene','List')).result.tubeDesigner.nestingGroups.flatMap(group=>group.parts??[]).length,0);
  report.checks.push('No final parts were created in the isolated scene');
  // Render the exact flatbuffer resources captured above, through the deployed
  // shared WebGL viewport and the same production preview-row builder.
  if(process.env.ICAX_PLAYWRIGHT_MODULE){
    const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE);
    const {serveBrowserAsset}=await import('./browserPackageRuntime.mjs');
    browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL??'chrome'});
    const page=await browser.newPage({viewport:{width:1600,height:900}});
    await page.route('http://incremental.test/**',serveBrowserAsset);await page.goto('http://incremental.test/');
    // A resource-pool recreation can legitimately reuse URL/version numbers
    // for different contents. Replay each captured case's actual bytes, never
    // whatever a later case left under that same key.
    let activeResources=new Map();
    await page.exposeFunction('readSavedNativeResource',key=>readFileSync(resolve(output,'resources',(activeResources.get(key)??resources.get(key)).file)).toString('base64'));
    await page.evaluate(async()=>{
      const {ThreeRenderViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
      const {buildPunchPreviewRows}=await import('/src/apps/tube-designer/webpage/punchEditor.mjs');
      document.body.style.cssText='margin:0;background:#15262d;color:#e4edf0;font:16px Segoe UI';
      document.body.innerHTML='<header style="padding:18px;height:56px"></header><main style="width:100vw;height:calc(100vh - 56px)"></main>';
      const viewport=new ThreeRenderViewport({continuousRender:false,projectionMode:'orthographic'});viewport.mount(document.querySelector('main'));
      const client={get:async(url,init)=>{const version=init?.headers?.get('ICAX-Resource-Version')??'0';
        const base64=await window.readSavedNativeResource(url+'@'+version);return new Response(Uint8Array.from(atob(base64),c=>c.charCodeAt(0)));}};
      window.showNativeCase=async item=>{
        document.querySelector('header').textContent=item.name+' · '+item.expected+' 个真实刀具 · '+item.milliseconds.toFixed(1)+' ms';
        await viewport.applyViewSnapshot({revision:item.name,rows:buildPunchPreviewRows(item.response)},client);
        viewport.setStandardView('top-front');viewport.fitViewToViewport(1.15);
        return {objects:[...viewport.sceneObjects.keys()],canvas:viewport.renderer.domElement.width>0};
      };
    });
    for(const name of ['circle-11','circle-11-one-moved','circle-blind-opposite','slot-template-parameter','two-dimensional-around-tube']){
      activeResources=new Map(Object.values(byName(name).geometries).map(item=>[item.url+'@'+item.version,item]));
      const receipt=await page.evaluate(item=>window.showNativeCase(item),byName(name));assert(receipt.canvas&&receipt.objects.length>1);
      const path=resolve(output,name+'.png');await page.screenshot({path});report.screenshots.push(path);
    }
    report.checks.push('Representative native flatbuffers rendered on the deployed WebGL viewport; screenshots include 11 tools, edited tool, blind/opposite, slot and a six-position circumferential array');
  }
  report.status='passed';
}catch(error){report.status='failed';report.error=error.stack;throw error;}
finally{
  report.nativeFailures=calls.filter(call=>call.response.ok===false).length;
  report.unexpectedNativeFailures=Math.max(0,report.nativeFailures-(report.expectedNativeFailures??0));
  report.nativePreviews=calls.filter(call=>call.request.method==='PreviewPunchWizard').length;
  report.nativeCreates=calls.filter(call=>call.request.method==='AddNestingPunchPart').length;
  report.stderr=stderr;report.resourceManifest=[...resources.values()];
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));
  await browser?.close();host.stdin.end();host.kill();
}
console.log('PASS actual native incremental: '+report.cases.length+' cases, '+report.nativeFailures+' failures, '+report.nativeCreates+' creates.');
