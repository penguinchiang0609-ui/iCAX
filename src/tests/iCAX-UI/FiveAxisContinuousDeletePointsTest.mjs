import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {handleToolAxisPointPick,toolAxisPointCommand,prepareToolAxisPointModes} from '../../apps/five-axis-cam/webpage/toolAxisPoints.mjs';
import {toolAxisActions} from '../../apps/five-axis-cam/webpage/toolAxisActions.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],tick=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(){
  let saves=0,gate=null;
  const edits=[],axis=point=>({point,setupPoint:point,direction:[0,0,1],setupDirection:[0,0,1],surfaceNormal:[0,0,1]});
  const point=(id,x,system=false,sampled=false)=>({id,label:id,system,sampled,anchor:{sourceSpanId:'span',nativeParameter:x},point:[x,0,0],setupPoint:[x,0,0],transition:'ArcLength'});
  const nativePoints=[point('start',-20,true),point('end',20,true),point('system-mid',16,true),point('sample',-16,false,true),point('a',-10),point('b',0),point('c',10)];
  const cut={id:'cut',setupId:'setup',placementId:'placement',machiningKind:'Cut',enabled:true,poseRef:{url:'pose',version:'1'},previewPoints:[[-20,0,0],[20,0,0]]};
  const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',placements:[{id:'placement',setupId:'setup',workpieceId:'workpiece',transform:identity}],workpieces:[{id:'workpiece',geometryRef:{url:'geometry'}}],setups:[{id:'setup',definitionOrder:['cut']}],intentToolpaths:[cut],fixtures:[],fixtureUnits:[]};
  const camera=new THREE.PerspectiveCamera(45,1,.1,1000);camera.position.z=100;camera.updateMatrixWorld(true);
  const view={fiveAxisArea:'toolpath',fiveCamPage:'cutting',scene:{activePlacementId:'placement',domain,intentToolpaths:[cut]},fiveAxisUI:{search:'',selectedIntentId:'cut',selectedIntentIds:['cut'],drafts:{},tools:{toolpath:'keypoint'}},viewport:{camera,renderer:{domElement:{getBoundingClientRect:()=>({x:0,y:0,left:0,top:0,width:1000,height:1000})}}}};
  const key=()=>JSON.stringify([domain.expected.epoch,cut.id,cut.poseRef,cut.processRef,cut.featureRef,cut.curveRef,cut.sourceState,cut.start,cut.reversed,identity]);
  view.fiveAxisPointEdit={id:'cut',key:key(),frame:'Setup',pointId:'start',points:structuredClone(nativePoints),axis:axis([-20,0,0]),values:['0','0','1'],dirty:false};
  const context={isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/continuous-delete.i5cam'},async autosave(){saves++;}},sceneProxy:{async invoke(method,request){
    if(method==='CamCut.ReadToolAxis')return axis(nativePoints.find(p=>p.anchor.nativeParameter===request.anchor.nativeParameter).point);
    if(method==='CamCut.EditToolAxisPoint'){
      if(gate?.action===request.action){gate.entered=true;await gate.promise;}
      assert.deepEqual(request.expected,domain.expected);
      const index=nativePoints.findIndex(p=>p.id===request.pointId),p=nativePoints[index];assert.ok(p);
      if(request.action==='Delete'){assert.equal(p.system,false);assert.equal(p.sampled,false);nativePoints.splice(index,1);}
      edits.push(structuredClone(request));
      return {points:structuredClone(nativePoints),selectedAxis:axis(p.point),statePatch:{expected:{...domain.expected,mutationSequence:String(edits.length+1)},inputRevision:String(edits.length+1),setup:{id:'setup',generatedGeometry:{status:'Stale'}},intentToolpaths:[{...cut,poseRef:{url:'pose',version:String(edits.length+1)}}],fixtureValidity:{setup:{},units:{}}}};
    }
    throw Error(method);
  }}};
  const ops={renderProject(){},appendProjectLog(){}};
  return {view,context,ops,edits,cut,nativePoints,get saves(){return saves;},command:action=>toolAxisPointCommand(context,view,action,ops),click(id,button=0){const p=nativePoints.find(p=>p.id===id),q=new THREE.Vector3(...p.point).project(camera);return handleToolAxisPointPick(context,view,null,null,{clientX:(q.x+1)*500,clientY:(1-q.y)*500,button},null,ops);},blank(){return handleToolAxisPointPick(context,view,null,null,{clientX:500,clientY:520,button:0},null,ops);},hold(action){let resolve;gate={action,promise:new Promise(done=>resolve=done),resolve:()=>resolve()};return gate;},async idle(){for(let i=0;i<100&&(view.pending||view.fiveAxisPointLookup);i++)await tick();assert.equal(!!view.pending||!!view.fiveAxisPointLookup,false);}};
}

test('one Delete activation removes a series with one native Delete and one shared automatic save per point',async()=>{
  const f=fixture();assert.equal(toolAxisActions(f.view).find(a=>a.id==='five.points.delete').disabled,false,'system selection must not prevent entering Delete');
  await f.command('delete');assert.equal(f.edits.length,0,'toggling Delete is UI state only');
  for(const id of ['a','b','c']){f.click(id);await f.idle();assert.equal(f.view.fiveAxisPointDelete,true);assert.equal(f.nativePoints.some(p=>p.id===id),false);assert.equal(f.view.fiveAxisPointEdit.pointId,'start');assert.deepEqual(f.view.fiveAxisPointEdit.axis.point,[-20,0,0]);}
  assert.deepEqual(f.edits.map(r=>[r.action,r.pointId]),[['Delete','a'],['Delete','b'],['Delete','c']]);assert.equal(f.saves,3);
  await f.command('delete');assert.equal(f.view.fiveAxisPointDelete,false);
});

test('all system and sampled markers, empty space and camera buttons are read-only in Delete mode',async()=>{
  const f=fixture();await f.command('delete');
  for(const id of ['start','end','system-mid','sample']){f.click(id);await f.idle();f.view.fiveAxisPointEdit.pointId=id;await f.command('delete-point');}
  f.view.fiveAxisPointEdit.points.find(p=>p.id==='start').system=false;f.view.fiveAxisPointEdit.pointId='start';await f.command('delete-point');
  f.blank();f.click('a',1);f.click('a',2);await f.idle();assert.equal(f.edits.length,0);assert.equal(f.saves,0);assert.equal(f.view.fiveAxisPointDelete,true);
});

test('invalid current input survives a clicked deletion; valid current input saves before the next point is deleted',async()=>{
  const f=fixture();await f.command('delete');const d=f.view.fiveAxisPointEdit;d.pointId='a';d.axis={point:[-10,0,0],setupPoint:[-10,0,0],direction:[0,0,1],setupDirection:[0,0,1]};
  d.values=['0','0','0'];d.dirty=true;f.click('b');await f.idle();assert.equal(d.pointId,'a');assert.equal(d.dirty,true);assert.match(d.error,/不能为零向量/);assert.equal(f.edits.length,0);assert.equal(f.view.fiveAxisPointDelete,true);
  d.values=['0','0.1','1'];f.click('b');await f.idle();assert.deepEqual(f.edits.map(r=>[r.action,r.pointId]),[['Upsert','a'],['Delete','b']]);assert.equal(f.saves,2);assert.equal(f.view.fiveAxisPointDelete,true);
});

test('Esc during the current draft save completes that save but does not submit the queued deletion',async()=>{
  const f=fixture();await f.command('delete');const d=f.view.fiveAxisPointEdit;d.pointId='a';d.values=['0','0.1','1'];d.dirty=true;const gate=f.hold('Upsert');f.click('b');
  for(let i=0;i<100&&!gate.entered;i++)await tick();assert.equal(gate.entered,true);
  await f.command('exit-delete');assert.equal(f.view.pending,true);gate.resolve();await f.idle();assert.deepEqual(f.edits.map(r=>r.action),['Upsert']);assert.equal(f.nativePoints.some(p=>p.id==='b'),true);assert.equal(f.saves,1);assert.equal(f.view.fiveAxisPointDelete,false);
});

test('Esc after Delete submission preserves its committed receipt and automatic save without rearming',async()=>{
  const f=fixture();await f.command('delete');const gate=f.hold('Delete');f.click('a');for(let i=0;i<100&&!gate.entered;i++)await tick();assert.equal(gate.entered,true);
  await f.command('exit-delete');assert.equal(f.view.pending,true);gate.resolve();await f.idle();assert.deepEqual(f.edits.map(r=>r.action),['Delete']);assert.equal(f.saves,1);assert.equal(f.view.fiveAxisPointDelete,false);
});

test('a rejected clicked deletion preserves the previously displayed point and matching IJK',async()=>{
  const f=fixture(),invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async(method,request)=>{
    if(method==='CamCut.EditToolAxisPoint'&&request.action==='Delete')throw Error('CAM_POSE_INFLUENCED_POINT');
    if(method==='CamScene.QueryCommand')return {status:'NotFound'};
    return invoke(method,request);
  };
  await f.command('delete');const d=f.view.fiveAxisPointEdit,before={id:d.pointId,axis:structuredClone(d.axis),values:[...d.values]};f.click('a');await f.idle();
  assert.equal(d.pointId,before.id);assert.deepEqual(d.axis,before.axis);assert.deepEqual(d.values,before.values);assert.equal(f.edits.length,0);assert.equal(f.saves,0);assert.equal(f.view.fiveAxisPointDelete,true);assert.match(d.error,/跟随/);
});

test('Add and Delete are exclusive; editor and lifecycle changes exit their UI state without creating history',async()=>{
  const f=fixture();await f.command('delete');await f.command('add');assert.equal(f.view.fiveAxisPointInsert,true);assert.equal(f.view.fiveAxisPointDelete,false);
  await f.command('delete');assert.equal(f.view.fiveAxisPointInsert,false);assert.equal(f.view.fiveAxisPointDelete,true);
  await f.command('edit');assert.equal(f.view.fiveAxisPointDelete,false);await f.command('delete');await f.command('whole');assert.equal(f.view.fiveAxisPointDelete,false);
  await f.command('delete');f.view.fiveCamPage='rapid';prepareToolAxisPointModes(f.view);assert.equal(f.view.fiveAxisPointDelete,false);assert.equal(f.edits.length,0);assert.equal(f.saves,0);
});
