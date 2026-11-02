// Repeat the actual rounded-stock trim workspace read without changing limits.
import assert from 'node:assert/strict';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {repositoryRoot,startSideSketchNativeBridge} from './fixtures/sideSketchNativeBridge.mjs';
const output=resolve(repositoryRoot,'output/tests',process.env.ICAX_CAD_READ_TIMING_OUTPUT||'cad-side-read-timing');mkdirSync(output,{recursive:true});
const bridge=await startSideSketchNativeBridge(output),report={passed:false,tubeDesignerSha256:bridge.dllHash,importerSha256:bridge.importerHash,timings:[]};
const descriptor=JSON.parse(readFileSync(resolve(repositoryRoot,'src/apps/tube-designer/templates/profile/rect/profile.json'),'utf8'));
const parameters={...Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue])),width:60,depth:40,wallThickness:2,cornerRadius:3,innerRadius:1};
const period=194.84955592153878;
const sketch={schema:'icax.tube-sketch',schemaVersion:1,kind:'side',unit:'mm',coordinateSpace:'arc-length-axial',length:1000,perimeter:period,faceHeight:period,trajectoryWidth:.5,
  entities:[{id:'trim-circle',kind:'circle',cx:20,cy:250,radius:6,closed:true}],endCuts:{start:{position:100,angleDegrees:0,rotationDegrees:0},end:{position:900,angleDegrees:0,rotationDegrees:0}}};
const timed=async(label,action)=>{const started=performance.now();const result=await action();const elapsed=performance.now()-started;
  report.timings.push({label,elapsed});console.log(label+': '+Math.round(elapsed)+'ms');return result;};
try{
  const added=await timed('create',()=>bridge.invoke('AddNestingSideSketchPart',{profileRef:{scope:'system',id:'rect'},parameters,length:1000,quantity:1,name:'R3裁切重复回读',sketch}));
  const get=()=>bridge.invoke('GetTubeCadDocument',{partEntityId:added.partEntityId,authoringView:'side'});
  let read=await timed('first-side-get',get);const draft=structuredClone(read.authoringView.sketch);draft.entities[0].radius=7;
  await timed('apply-radius7',()=>bridge.invoke('ApplyTubeCadDocument',{partEntityId:read.partEntityId,resourceVersion:read.resourceVersion,revision:read.revision,authoringEdit:{mode:'side',sketch:draft,bindings:read.authoringView.bindings}}));
  read=await timed('edited-side-get',get);assert.equal(read.authoringView.sketch.entities[0].radius,7);
  await bridge.rpc({action:'save',payload:{file:'trim-read-timing.ictd'}});await bridge.rpc({action:'open',payload:{file:'trim-read-timing.ictd'}});
  for(let i=0;i<2;++i)assert.deepEqual(await timed('reopened-side-get-'+(i+1),get),read);
  const actual=await bridge.rpc({action:'cad-document-geometry',payload:{partEntityId:read.partEntityId}});
  assert.equal(actual.valid,true);assert.equal(actual.directedDifferenceVolume,0);report.geometry=actual;report.passed=true;
}catch(error){report.error=error.stack;console.error(error.stack);process.exitCode=1;}
finally{report.requests=bridge.requests;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await bridge.close();}
