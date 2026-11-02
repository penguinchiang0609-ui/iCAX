import test from 'node:test';
import assert from 'node:assert/strict';
import {inspectToolAxisAtPoint,syncToolAxisPoints,toolAxisPointField,toolAxisPointCommand} from '../../apps/five-axis-cam/webpage/toolAxisPoints.mjs';

const transform=[0,-1,0,10,1,0,0,20,0,0,1,30,0,0,0,1];
const deferred=()=>{let resolve;const promise=new Promise(done=>resolve=done);return{promise,resolve};};
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(){
 let current=true,saves=0;
 const calls=[],axis=x=>({point:[x,0,0],setupPoint:[10,20+x,30],direction:[0,.6,.8],setupDirection:[-.6,0,.8],surfaceNormal:[0,0,1],anchor:{sourceSpanId:'span',nativeParameter:x}});
 const point=(id,x,system=false)=>({id,label:id,system,overridden:!system,anchor:{sourceSpanId:'span',nativeParameter:x},point:[x,0,0],setupPoint:[10,20+x,30],transition:'DirectionChange'});
 const nativePoints=[point('start',0,true),point('end',10,true),point('saved-local',4)],cut={id:'cut',setupId:'setup',placementId:'placement',machiningKind:'Cut',enabled:true,poseRef:{url:'pose',version:'1'}};
 const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',setups:[{id:'setup',definitionOrder:['cut']}],placements:[{id:'placement',setupId:'setup',workpieceId:'workpiece',transform}],workpieces:[{id:'workpiece',geometryRef:{url:'geometry'}}],intentToolpaths:[cut],fixtures:[],fixtureUnits:[]};
 const view={fiveAxisArea:'toolpath',fiveCamPage:'cutting',scene:{activePlacementId:'placement',domain,intentToolpaths:[cut]},fiveAxisUI:{search:'',selectedIntentId:'cut',selectedIntentIds:['cut'],drafts:{},tools:{toolpath:'keypoint'}}};
 const context={isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/test/inspect.i5cam'},async autosave(){saves++;}},sceneProxy:{async invoke(method,request){
  calls.push([method,structuredClone(request)]);
  if(method==='CamCut.ListToolAxisPoints')return{points:structuredClone(nativePoints)};
  if(method==='CamCut.ResolveToolAxisPoint'){const x=request.nearPoint[0],existing=nativePoints.find(p=>p.anchor.nativeParameter===x);return{pointId:existing?.id||'',anchor:{sourceSpanId:'span',nativeParameter:x},axis:axis(x)};}
  if(method==='CamCut.ReadToolAxis')return axis(request.anchor.nativeParameter);
  if(method==='CamCut.EditToolAxisPoint'){
   const saved=point(request.pointId,request.anchor.nativeParameter),nextCut={...cut,poseRef:{url:'pose',version:'2'}};nativePoints.push(saved);
   return{points:structuredClone(nativePoints),selectedAxis:axis(saved.anchor.nativeParameter),statePatch:{expected:{epoch:'epoch',mutationSequence:'2'},inputRevision:'2',setup:{id:'setup',generatedGeometry:{status:'Stale'}},intentToolpaths:[nextCut],fixtureValidity:{setup:{},units:{}}}};
  }
  throw Error('Unexpected '+method);
 }}};
 const ops={renderProject(){},appendProjectLog(){}};
 return{view,context,ops,calls,cut,nativePoints,axis,get saves(){return saves;},setCurrent(value){current=value;}};
}
const inspect=(f,x=4)=>inspectToolAxisAtPoint(f.context,f.view,{intentId:'cut',nearPoint:[x,0,0],maximumDistanceMm:1},f.ops);
const edits=f=>f.calls.filter(c=>c[0]==='CamCut.EditToolAxisPoint');

test('a detected keypose source anchor opens XYZ and IJK without a nearest-point query or an edit',async()=>{
 const f=fixture(),anchor={sourceSpanId:'span',nativeParameter:5.5};
 assert.equal(await inspectToolAxisAtPoint(f.context,f.view,{intentId:'cut',anchor},f.ops),true);
 assert.deepEqual(f.view.fiveAxisPointEdit.newPoint.anchor,anchor);assert.deepEqual(f.view.fiveAxisPointEdit.positionValues,['10','25.5','30']);
 assert.deepEqual(f.calls.map(c=>c[0]).sort(),['CamCut.ListToolAxisPoints','CamCut.ReadToolAxis']);assert.equal(edits(f).length,0);assert.equal(f.saves,0);
});

test('an IJK-only edit omits XYZ while an explicit XYZ edit sends the selected coordinate frame',async()=>{
 for(const move of [false,true]){const f=fixture();await inspect(f);toolAxisPointField(f.view,'0','0.1');
  if(move)toolAxisPointField(f.view,'position-2','37.125');await toolAxisPointCommand(f.context,f.view,'apply',f.ops);
  const request=edits(f)[0][1];assert.equal('position' in request,move);if(move){assert.deepEqual(request.position,[10,24,37.125]);assert.equal(request.frame,'Setup');}
  assert.equal(f.saves,1);assert(!f.calls.some(([method])=>/Generate|CheckCollision|AutoRepair/.test(method)));
 }
});

test('diagnostic navigation resolves the exact saved anchor instead of selecting the default start and creates no native edit or autosave',async()=>{
 const f=fixture();assert.equal(await inspect(f),true);const d=f.view.fiveAxisPointEdit;
 assert.equal(d.pointId,'saved-local');assert.equal(d.newPoint,null);assert.equal(d.dirty,false);assert.equal(d.loading,false);assert.deepEqual(d.values,['-0.6','0','0.8']);assert.equal(d.frame,'Setup');assert.deepEqual(d.points.find(p=>p.id===d.pointId).anchor,{sourceSpanId:'span',nativeParameter:4});
 assert.deepEqual(f.calls.map(c=>c[0]).sort(),['CamCut.ListToolAxisPoints','CamCut.ResolveToolAxisPoint']);const request=f.calls.find(c=>c[0]==='CamCut.ResolveToolAxisPoint')[1];assert.deepEqual(request.nearPoint,[4,0,0]);assert.equal(request.frame,'Workpiece');assert.deepEqual(request.expected,{epoch:'epoch',mutationSequence:'1'});assert.equal(edits(f).length,0);assert.equal(f.saves,0);
});
test('interpolated navigation keeps an uncommitted draft clean until an explicit angle edit and apply use the existing Upsert command',async()=>{
 const f=fixture();assert.equal(await inspect(f,5.5),true);const d=f.view.fiveAxisPointEdit,id=d.pointId;assert.match(id,/^[a-f0-9-]{36}$/);assert.equal(d.newPoint.id,id);assert.deepEqual(d.newPoint.anchor,{sourceSpanId:'span',nativeParameter:5.5});assert.equal(d.dirty,false);assert.equal(f.nativePoints.length,3);
 await toolAxisPointCommand(f.context,f.view,'apply',f.ops);assert.equal(edits(f).length,0);assert.equal(f.saves,0);
 toolAxisPointField(f.view,'angle-0','7.5');assert.equal(d.dirty,true);assert.equal(edits(f).length,0);await toolAxisPointCommand(f.context,f.view,'apply',f.ops);
 assert.equal(edits(f).length,1);assert.equal(edits(f)[0][1].action,'Upsert');assert.equal(edits(f)[0][1].pointId,id);assert.deepEqual(edits(f)[0][1].anchor,{sourceSpanId:'span',nativeParameter:5.5});assert.equal(f.saves,1);assert.equal(d.newPoint,null);
});
test('workpiece display coordinates are retained while diagnostic proximity resolution always uses Workpiece input',async()=>{
 const f=fixture();f.view.fiveAxisPointEdit={frame:'Workpiece'};assert.equal(await inspect(f),true);assert.equal(f.view.fiveAxisPointEdit.frame,'Workpiece');assert.deepEqual(f.view.fiveAxisPointEdit.values,['0','0.6','0.8']);assert.equal(f.calls.find(c=>c[0]==='CamCut.ResolveToolAxisPoint')[1].frame,'Workpiece');
});
test('lead-in and lead-out navigation read the exact start or end system anchor without a proximity lookup',async()=>{
 for(const pointId of ['start','end']){const f=fixture();assert.equal(await inspectToolAxisAtPoint(f.context,f.view,{intentId:'cut',pointId},f.ops),true);assert.equal(f.view.fiveAxisPointEdit.pointId,pointId);assert.equal(f.view.fiveAxisPointEdit.newPoint,null);assert.deepEqual(f.calls.map(c=>c[0]),['CamCut.ListToolAxisPoints','CamCut.ReadToolAxis']);assert.deepEqual(f.calls[1][1].anchor,f.nativePoints.find(p=>p.id===pointId).anchor);assert.equal(f.saves,0);}
});
test('late inspection results cannot overwrite another editor, a newer read token or changed source and revision facts',async()=>{
 for(const change of [f=>{f.view.fiveAxisPointEdit={id:'another',dirty:true,values:['later']};},f=>{const d=f.view.fiveAxisPointEdit;d.readToken++;d.values=['later'];d.dirty=true;},f=>{f.cut.poseRef={url:'pose',version:'2'};},f=>{f.view.scene.domain.expected={epoch:'epoch',mutationSequence:'2'};},f=>f.setCurrent(false)]){
  const f=fixture(),gate=deferred(),invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async(method,request)=>{if(method==='CamCut.ResolveToolAxisPoint')await gate.promise;return invoke(method,request);};const promise=inspect(f,5.5);change(f);const editor=f.view.fiveAxisPointEdit;gate.resolve();assert.equal(await promise,false);assert.equal(f.view.fiveAxisPointEdit,editor);assert.equal(edits(f).length,0);assert.equal(f.saves,0);if(editor.dirty)assert.deepEqual(editor.values,['later']);else assert.equal(editor.axis,undefined);
 }
});
test('a pending default-start background load becomes obsolete when a diagnostic inspection installs its own draft',async()=>{
 const f=fixture(),gate=deferred(),invoke=f.context.sceneProxy.invoke;let lists=0;
 f.context.sceneProxy.invoke=async(method,request)=>{if(method==='CamCut.ListToolAxisPoints'&&++lists===1){await gate.promise;return{points:structuredClone(f.nativePoints)};}return invoke(method,request);};
 syncToolAxisPoints(f.context,f.view,f.ops);assert.equal(f.view.fiveAxisPointEdit.pointId,'start');assert.equal(await inspect(f),true);const editor=f.view.fiveAxisPointEdit;gate.resolve();await tick();await tick();assert.equal(f.view.fiveAxisPointEdit,editor);assert.equal(editor.pointId,'saved-local');assert.equal(f.calls.filter(c=>c[0]==='CamCut.ReadToolAxis').length,0);assert.equal(f.saves,0);
});
test('unknown resolved IDs and unsaved existing edits cannot fall back to start or trigger an insertion',async()=>{
 const f=fixture(),invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async(method,request)=>method==='CamCut.ResolveToolAxisPoint'?{pointId:'missing',axis:f.axis(5),anchor:{sourceSpanId:'span',nativeParameter:5}}:invoke(method,request);assert.equal(await inspect(f),false);assert.equal(f.view.fiveAxisPointEdit.pointId,'');assert.match(f.view.fiveAxisPointEdit.error,/关键点已改变/);assert.equal(edits(f).length,0);
 const g=fixture(),original=g.view.fiveAxisPointEdit={id:'cut',dirty:true,values:['user']};assert.equal(await inspect(g),false);assert.equal(g.view.fiveAxisPointEdit,original);assert.deepEqual(g.calls,[]);assert.equal(g.saves,0);
});
