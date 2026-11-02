import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {handleToolAxisPointPick,toolAxisPointCommand} from '../../apps/five-axis-cam/webpage/toolAxisPoints.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const deferred=()=>{let resolve;const promise=new Promise(done=>resolve=done);return {promise,resolve};};
function fixture(){
  let current=true,saves=0,resolveGate=null,editGate=null;
  const edits=[],axis=point=>({point,setupPoint:point,direction:[0,0,1],setupDirection:[0,0,1],surfaceNormal:[0,0,1]}),point=(id,x)=>({id,label:id,system:true,anchor:{sourceSpanId:'span',nativeParameter:x},point:[x,0,0],setupPoint:[x,0,0],transition:'ArcLength'});
  const nativePoints=[point('start',-20),point('end',20)],cut={id:'cut',setupId:'setup',placementId:'placement',machiningKind:'Cut',enabled:true,poseRef:{url:'pose',version:'1'},previewPoints:[[-20,0,0],[20,0,0]]};
  const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',placements:[{id:'placement',setupId:'setup',workpieceId:'workpiece',transform:identity}],workpieces:[{id:'workpiece',geometryRef:{url:'geometry'}}],setups:[{id:'setup',definitionOrder:['cut']}],intentToolpaths:[cut],fixtures:[],fixtureUnits:[]};
  const camera=new THREE.PerspectiveCamera(45,1,.1,1000);camera.position.z=100;camera.updateMatrixWorld(true);
  const view={fiveAxisArea:'toolpath',fiveCamPage:'cutting',scene:{activePlacementId:'placement',domain,intentToolpaths:[cut]},fiveAxisUI:{search:'',selectedIntentId:'cut',selectedIntentIds:['cut'],drafts:{},tools:{toolpath:'keypoint'}},viewport:{camera,renderer:{domElement:{getBoundingClientRect:()=>({x:0,y:0,left:0,top:0,width:1000,height:1000})}}}};
  const key=()=>JSON.stringify([domain.expected.epoch,cut.id,cut.poseRef,cut.processRef,cut.featureRef,cut.curveRef,cut.sourceState,cut.start,cut.reversed,identity]);
  view.fiveAxisPointEdit={id:'cut',key:key(),frame:'Setup',pointId:'start',points:structuredClone(nativePoints),axis:axis([-20,0,0]),values:['0','0','1'],dirty:false};
  const context={isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/test/continuous.i5cam'},async autosave(){saves++;}},sceneProxy:{async invoke(method,request){
    if(method==='CamCut.ResolveToolAxisPoint'){
      if(resolveGate)await resolveGate.promise;
      const x=request.nearPoint[0],existing=nativePoints.find(p=>Math.abs(p.point[0]-x)<1e-7);
      return {pointId:existing?.id,anchor:{sourceSpanId:'span',nativeParameter:x},axis:axis([x,0,0])};
    }
    if(method==='CamCut.ReadToolAxis'){const p=nativePoints.find(p=>p.anchor.nativeParameter===request.anchor.nativeParameter);return axis(p.point);}
    if(method==='CamCut.ListToolAxisPoints')return {points:structuredClone(nativePoints)};
    if(method==='CamCut.EditToolAxisPoint'){
      if(editGate)await editGate.promise;
      assert.deepEqual(request.expected,domain.expected);edits.push(structuredClone(request));
      const p={...point(request.pointId,request.anchor.nativeParameter),system:false,overridden:true};
      const index=nativePoints.findIndex(p=>p.id===request.pointId);if(index<0)nativePoints.push(p);else nativePoints[index]=p;
      const nextCut={...cut,poseRef:{url:'pose',version:String(edits.length+1)}};
      return {points:structuredClone(nativePoints),selectedAxis:axis(p.point),statePatch:{expected:{...domain.expected,mutationSequence:String(edits.length+1)},inputRevision:String(edits.length+1),setup:{id:'setup',generatedGeometry:{status:'Stale'}},intentToolpaths:[nextCut],fixtureValidity:{setup:{},units:{}}}};
    }
    throw Error(method);
  }}};
  const ops={renderProject(){},appendProjectLog(){}};
  return {view,context,ops,edits,cut,get saves(){return saves;},get nativePoints(){return nativePoints;},setCurrent(v){current=v;},holdResolve(){resolveGate=deferred();return resolveGate;},holdEdit(){editGate=deferred();return editGate;},async add(){await toolAxisPointCommand(context,view,'add',ops);},click(x=500,button=0){return handleToolAxisPointPick(context,view,null,null,{clientX:x,clientY:500,button},null,ops);},async idle(){for(let i=0;i<100&&(view.pending||view.fiveAxisPointLookup);i++)await tick();assert.equal(!!view.pending||!!view.fiveAxisPointLookup,false);}};
}

test('one Add activation commits a series through the shared automatic saver and selecting a saved marker creates no duplicate',async()=>{
  const f=fixture();await f.add();for(const x of [440,500,560]){f.click(x);await f.idle();assert.equal(f.view.fiveAxisPointInsert,true);assert.equal(f.view.fiveAxisPointEdit.dirty,false);assert.equal(f.view.fiveAxisPointEdit.newPoint,null);}
  assert.equal(f.edits.length,3);assert.equal(f.saves,3);assert.equal(f.nativePoints.filter(p=>!p.system).length,3);
  f.click(440);await f.idle();assert.equal(f.edits.length,3);assert.equal(f.saves,3);assert.equal(f.view.fiveAxisPointInsert,true);
  await f.add();assert.equal(f.view.fiveAxisPointInsert,false);
});

test('invalid current IJK blocks the next insertion while valid edits commit before adding the next point',async()=>{
  const f=fixture();await f.add();f.click(440);await f.idle();const d=f.view.fiveAxisPointEdit,id=d.pointId;
  d.values=['0','0','0'];d.dirty=true;f.click(560);await f.idle();assert.equal(f.edits.length,1);assert.equal(d.pointId,id);assert.equal(d.dirty,true);assert.match(d.error,/不能为零向量/);assert.equal(f.view.fiveAxisPointInsert,true);
  d.values=['0','0.1','1'];f.click(560);await f.idle();assert.equal(f.edits.length,3);assert.equal(f.edits[1].pointId,id);assert.notEqual(f.edits[2].pointId,id);assert.equal(f.saves,3);assert.equal(f.view.fiveAxisPointInsert,true);
});

test('leaving during anchor lookup discards its late result without changing the current editor or saving',async()=>{
  const f=fixture(),gate=f.holdResolve();await f.add();const d=f.view.fiveAxisPointEdit;f.click();assert.equal(f.view.pending,true);
  await toolAxisPointCommand(f.context,f.view,'exit-add',f.ops);assert.equal(f.view.pending,false);d.values=['0','0.2','1'];d.dirty=true;gate.resolve();await tick();await tick();
  assert.equal(f.edits.length,0);assert.equal(f.saves,0);assert.deepEqual(d.values,['0','0.2','1']);assert.equal(f.view.fiveAxisPointInsert,false);
});

test('leaving during a submitted edit lets its native receipt and automatic save finish without rearming',async()=>{
  const f=fixture(),gate=f.holdEdit();await f.add();f.click();for(let i=0;i<100&&f.view.fiveAxisPointLookup?.phase!=='committing';i++)await tick();
  assert.equal(f.view.fiveAxisPointLookup?.phase,'committing');await toolAxisPointCommand(f.context,f.view,'exit-add',f.ops);assert.equal(f.view.pending,true);
  gate.resolve();await f.idle();assert.equal(f.edits.length,1);assert.equal(f.saves,1);assert.equal(f.view.fiveAxisPointInsert,false);
});

test('source changes and project switches invalidate unresolved insertions; navigation buttons never insert points',async()=>{
  for(const change of [f=>{f.cut.poseRef={url:'changed',version:'2'};},f=>f.setCurrent(false)]){
    const f=fixture(),gate=f.holdResolve();await f.add();f.click();change(f);gate.resolve();await f.idle();assert.equal(f.edits.length,0);assert.equal(f.saves,0);
  }
  const f=fixture();await f.add();assert.equal(f.click(500,1),false);assert.equal(f.click(500,2),false);await f.idle();assert.equal(f.edits.length,0);
});
