import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {browserReportDirectory,serveBrowserAsset} from './browserPackageRuntime.mjs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

const {chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('five-axis-mounted-placement');
const browser=await chromium.launch({channel:process.env.ICAX_BROWSER_CHANNEL||'msedge',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
const report={passed:false,cases:[],nativeTransport:'controlled command fixture; production browser modules and WebGL viewport'},errors=[];
try{
 const page=await browser.newPage({viewport:{width:960,height:720}});page.on('pageerror',e=>errors.push(e.message));
 await page.route('http://127.0.0.1:49198/**',serveBrowserAsset);await page.goto('http://127.0.0.1:49198/');
 await page.evaluate(async()=>{
  const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
  const W=await import('/src/apps/five-axis-cam/webpage/workpiecePlacement.mjs');
  const F=await import('/src/apps/five-axis-cam/webpage/fixturePlacement.mjs');
  const A=await import('/src/apps/five-axis-cam/webpage/workpieceAlignment.mjs');
  const actions=await import('/src/apps/five-axis-cam/webpage/fixtureActions.mjs');
  const {selectTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');
  const {setupWorldFrame}=await import('/src/apps/five-axis-cam/webpage/setupWorldFrame.mjs');
  const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
  document.body.style='margin:0;width:960px;height:720px';const host=document.createElement('div');host.style='width:960px;height:720px';document.body.append(host);
  const vp=createThreeViewport({continuousRender:false,showGrid:false,showWorldAxes:false,showOrientationGizmo:false}).mount(host);
  const row=m=>m.clone().transpose().toArray(),mat=a=>new THREE.Matrix4().set(...a);
  const rigid=(p,r)=>new THREE.Matrix4().compose(new THREE.Vector3(...p),new THREE.Quaternion().setFromEuler(new THREE.Euler(...r)),new THREE.Vector3(1,1,1));
  const support=rigid([430,-210,140],[.41,0,.65]),registration=rigid([20,30,40],[0,.23,-.17]),domain=rigid([30,-20,60],[0,0,.22]);
  const view={fiveAxisArea:'workpiece',fivePreparationKind:'setup',scene:{activePlacementId:'part',model:{isLoaded:true},intentToolpaths:[],domain:{expected:{epoch:'test',mutationSequence:'0'},activeSetupId:'setup',placements:[{id:'part',setupId:'setup',workpieceId:'workpiece',transform:row(domain)}],workpieces:[{id:'workpiece',enabled:true}],setups:[{id:'setup',enabled:true,machineSetup:{machineId:'machine',supportId:'support',placement:row(registration)}}],fixtures:[],fixtureUnits:[]}},viewport:vp};
  const saved=new Map(),mesh=(id,m,color)=>{const object=new THREE.Mesh(new THREE.BoxGeometry(70,50,40),new THREE.MeshBasicMaterial({color}));object.matrixAutoUpdate=false;object.matrix.copy(m);vp.sceneObjects.set(id,object);vp.content.add(object);saved.set(id,m.clone());return object;};
  mesh('support',support,0x555555);mesh('part',domain,0x33aaff);
  const actualPreview=vp.setTransformPreview.bind(vp),actualClear=vp.clearTransformPreview.bind(vp),actualManipulator=vp.setTransformManipulator.bind(vp);
  // Controlled PDO adapter: production viewport previews still own the map;
  // the fixture supplies actual meshes instead of an external native process.
  vp.setTransformPreview=(id,values)=>{actualPreview(id,values);const object=vp.sceneObjects.get(id);if(object){object.matrix.fromArray(values);object.matrixWorldNeedsUpdate=true;vp.scene.updateMatrixWorld(true);}return vp;};
  vp.clearTransformPreview=id=>{actualClear(id);const object=vp.sceneObjects.get(id);if(object&&saved.has(id)){object.matrix.copy(saved.get(id));object.matrixWorldNeedsUpdate=true;vp.scene.updateMatrixWorld(true);}return vp;};
  vp.setTransformManipulator=options=>{window.fixture.manipulator=options;return actualManipulator(options);};
  const context={mount:document.body,isCurrentProject:()=>true,sceneProxy:{invoke:async(method,request)=>{
   window.fixture.requests.push({method,request:structuredClone(request)});
   if(method==='CamPlacement.SetTransform'){view.scene.domain.placements[0].transform=[...request.transform];saved.set('part',setupWorldFrame(view,'setup').multiply(mat(request.transform)));return {};}
   if(method==='CamFixture.UnitUpdate'){
    const unit=view.scene.domain.fixtureUnits[0],delta=mat(request.transform).multiply(mat(unit.transform).invert());unit.transform=[...request.transform];++unit.inputRevision;
    for(const part of view.scene.domain.fixtures){part.transform=row(delta.clone().multiply(mat(part.transform)));saved.set(part.id,setupWorldFrame(view,'setup').multiply(mat(part.transform)));}return {};
   }
   throw Error('Unexpected fixture call '+method);
  }}};
  const ops={renderProject:()=>{},refreshSceneState:async()=>{},appendProjectLog:()=>{}};
  window.fixture={THREE,W,F,A,actions,selectTool,setupWorldFrame,view,context,ops,row,mat,rigid,support,registration,domain,saved,mesh,requests:[]};
  vp.camera.position.set(1100,-1000,1000);vp.camera.up.set(0,0,1);vp.camera.lookAt(430,-210,200);vp.camera.updateMatrixWorld(true);vp.scene.updateMatrixWorld(true);
 });
 const results=await page.evaluate(async()=>{
  const f=fixture,{THREE,W,F,A,actions,selectTool,setupWorldFrame,view,context,ops,row,mat}=f,rows=[];
  const near=(a,b,message)=>{const x=a.elements??a,y=b.elements??b,error=Math.max(...x.map((v,i)=>Math.abs(v-y[i])));if(error>1e-5)throw Error(message+': '+error);return error;};
  const frame=()=>setupWorldFrame(view,'setup'),part=()=>view.viewport.sceneObjects.get('part'),domain=()=>mat(view.scene.domain.placements[0].transform);
  const supportObject=view.viewport.sceneObjects.get('support'),expectedFrame=frame();
  view.viewport.sceneObjects.delete('support');selectTool(view,'translate');
  if(W.preparePlacement(view)!==true||W.preparePlacement(view)!==false)throw Error('A CAM editor must initialize without a displayed machine support');
  view.viewport.transformPayloads.set('part',{localToWorld:part().matrix.toArray()});
  near(frame(),expectedFrame,'hidden machine support frame');
  const authoritative=part().matrix.clone();view.viewport.setTransformPreview('part',authoritative.clone().multiply(new THREE.Matrix4().makeTranslation(17,0,0)).toArray());
  near(frame(),expectedFrame,'live child preview changed its parent frame');view.viewport.clearTransformPreview('part');
  if(W.preparePlacement(view)!==false)throw Error('Ready placement must initialize only once');
  W.disposePlacementPreview(view);view.fivePlacement=null;view.viewport.sceneObjects.set('support',supportObject);view.viewport.transformPayloads.delete('part');
  view.fiveAxisArea='simulation';near(frame(),f.support.clone().multiply(f.registration),'machine simulation retains physical support frame');view.fiveAxisArea='workpiece';near(frame(),new THREE.Matrix4(),'workpiece view must use setup coordinates');view.fiveAxisArea='toolpath';near(frame(),new THREE.Matrix4(),'CAM view must use setup coordinates');view.fiveAxisArea='workpiece';
  rows.push({case:'workpiece and CAM stay in setup coordinates; physical support frame is confined to simulation',passed:true});
  const run=async({mode='translate',space='world',axis='x',value='10'})=>{
   selectTool(view,mode);view.fivePlacementSpace=space;W.openPlacement(view,mode);const before=frame().multiply(domain());W.syncPlacementInteraction(context,view,ops);near(part().matrix,before,'opening editor moved mounted part');
   const d=view.fivePlacement;d.axis=axis;d.value=value;W.syncPlacementInteraction(context,view,ops);
   let expected;
   if(mode==='translate'){const direction=new THREE.Vector3(axis==='x'?1:0,axis==='y'?1:0,axis==='z'?1:0);if(space==='local')direction.transformDirection(before);expected=new THREE.Matrix4().makeTranslation(...direction.multiplyScalar(Number(value)).toArray()).multiply(before);}
   else{part().geometry.computeBoundingBox();const pivot=part().geometry.boundingBox.getCenter(new THREE.Vector3()).applyMatrix4(before),direction=new THREE.Vector3(axis==='x'?1:0,axis==='y'?1:0,axis==='z'?1:0);if(space==='local')direction.transformDirection(before);expected=new THREE.Matrix4().makeTranslation(...pivot.toArray()).multiply(new THREE.Matrix4().makeRotationAxis(direction,Number(value)*Math.PI/180)).multiply(new THREE.Matrix4().makeTranslation(...pivot.negate().toArray())).multiply(before);}
   near(part().matrix,expected,'mounted preview in wrong frame');const expectedDomain=frame().invert().multiply(expected);await W.placementCommand(context,view,'apply',ops);const request=f.requests.at(-1);near(mat(request.request.transform),expectedDomain,'commit leaked world coordinates into domain');W.disposePlacementPreview(view);near(part().matrix,expected,'commit changed mounted world pose');rows.push({case:`${mode}/${space}/${axis}`,passed:true});
  };
  for(const space of ['world','local']){await run({space,axis:'x',value:'10'});await run({mode:'rotate',space,axis:'z',value:'23'});}
  selectTool(view,'translate');W.openPlacement(view,'translate');const beforePanel=domain();W.transformParameterChanged(view,'position-X','12.5');const panel=beforePanel.clone();panel.elements[12]=12.5;near(part().matrix,frame().multiply(panel),'numeric setup panel jumped to world');await W.placementCommand(context,view,'apply',ops);near(domain(),panel,'absolute panel commit is not setup-local');rows.push({case:'absolute setup panel preserves domain values',passed:true});W.disposePlacementPreview(view);
  selectTool(view,'translate');W.openPlacement(view,'translate');W.syncPlacementPreview(view);const movedSupport=f.support.clone().multiply(new THREE.Matrix4().makeRotationZ(.3));view.viewport.sceneObjects.get('support').matrix.copy(movedSupport);W.syncPlacementPreview(view);near(part().matrix,frame().multiply(domain()),'support movement did not refresh preview');W.disposePlacementPreview(view);rows.push({case:'live physical support frame refresh',passed:true});
  const unitMatrix=f.rigid([5,7,9],[0,0,.4]),left=unitMatrix.clone().multiply(new THREE.Matrix4().makeTranslation(-60,0,0)),right=unitMatrix.clone().multiply(new THREE.Matrix4().makeTranslation(60,0,0));
  view.scene.domain.fixtureUnits=[{id:'unit',setupId:'setup',name:'fixture',transform:row(unitMatrix),partIds:['left','right'],inputRevision:1,enabled:true}];view.scene.domain.fixtures=[{id:'left',setupId:'setup',transform:row(left)},{id:'right',setupId:'setup',transform:row(right)}];f.mesh('left',frame().multiply(left),0xe0a033);f.mesh('right',frame().multiply(right),0xe0a033);
  selectTool(view,'fixture');const draft=actions.fixtureDraft(view);draft.x='15';draft.dirty=true;F.syncFixturePlacement(context,view,ops);const numericUnit=unitMatrix.clone();numericUnit.elements[12]=15;const numericDelta=numericUnit.clone().multiply(unitMatrix.clone().invert());near(view.viewport.sceneObjects.get('left').matrix,frame().multiply(numericDelta.clone().multiply(left)),'fixture numeric preview frame');
  draft.dirty=false;F.syncFixturePlacement(context,view,ops);const options=f.manipulator,delta=new THREE.Matrix4().makeTranslation(8,-3,4),expectedUnit=frame().invert().multiply(delta.clone().multiply(frame()).multiply(numericUnit));
  options.onStart({axis:'X',direction:[1,0,0]});options.onChange({translation:[8,-3,4],angle:0});near(view.fiveFixtureGizmo.matrix,expectedUnit,'fixture drag not converted to domain');const expectedLeft=frame().multiply(expectedUnit.clone().multiply(unitMatrix.clone().invert()).multiply(left));near(view.viewport.sceneObjects.get('left').matrix,expectedLeft,'fixture multipart preview lost relative pose');options.onEnd({moved:true});await new Promise(r=>setTimeout(r,0));await view.fixtureRequest;near(mat(f.requests.at(-1).request.transform),expectedUnit,'fixture submit is not setup-local');rows.push({case:'fixture numeric and multipart world drag preview/submit',passed:true});F.disposeFixturePlacement(view);
  selectTool(view,'align');A.openAlignment(view);const d=view.fiveAlignment,aligned=f.rigid([2,4,6],[.1,.2,.3]);d.report={transform:row(aligned)};d.selecting=null;
  view.sceneSnapping={epoch:view.scene.domain.expected.epoch,catalogs:new Map(),pending:new Set(),keys:new Map(),errors:new Map()};
  for(const [id,object]of view.viewport.sceneObjects){view.sceneSnapping.keys.set(id,JSON.stringify([view.scene.domain.expected.epoch,object.geometry.uuid]));view.sceneSnapping.catalogs.set(id,{features:[],faces:[]});}
  A.syncAlignment(context,view);near(part().matrix,frame().multiply(aligned),'alignment preview bypassed physical support');A.disposeAlignment(view);rows.push({case:'alignment domain result remains on mounted support',passed:true});
  return rows;
 });
 report.cases=results;assert.equal(results.length,9);assert.ok(results.every(r=>r.passed));
 const beforeDrag=await page.evaluate(()=>{const f=fixture;f.selectTool(f.view,'translate');f.view.fivePlacementSpace='world';f.W.openPlacement(f.view,'translate');f.W.syncPlacementInteraction(f.context,f.view,f.ops);return {domain:[...f.view.scene.domain.placements[0].transform],world:f.row(f.view.viewport.sceneObjects.get('part').matrix),requests:f.requests.length};});
 await gizmoDriver(page).drag('X',35,false);
 const preview=await page.evaluate(()=>({domain:[...fixture.view.scene.domain.placements[0].transform],world:fixture.row(fixture.view.viewport.sceneObjects.get('part').matrix),requests:fixture.requests.length}));
 assert.deepEqual(preview.domain,beforeDrag.domain);assert.equal(preview.requests,beforeDrag.requests);assert.ok(Math.abs(preview.world[3]-beforeDrag.world[3])>1);for(const i of [7,11])assert.ok(Math.abs(preview.world[i]-beforeDrag.world[i])<1e-5);
 await page.mouse.up();await page.waitForFunction(()=>!fixture.view.pending);
 const committed=await page.evaluate(()=>{const f=fixture;f.W.disposePlacementPreview(f.view);return {request:f.requests.at(-1),world:f.row(f.view.viewport.sceneObjects.get('part').matrix),domain:[...f.view.scene.domain.placements[0].transform]};});
 committed.world.forEach((v,i)=>assert.ok(Math.abs(v-preview.world[i])<1e-5));assert.deepEqual(committed.domain,committed.request.request.transform);assert.deepEqual(committed.domain,committed.world);
 report.cases.push({case:'real pointer world-X drag remains on support and commits setup-local coordinates',passed:true});
 const overlay=await page.evaluate(async()=>{
  const f=fixture,{view,THREE,selectTool}=f,{updateCutPreview,disposeCutPreview}=await import('/src/apps/five-axis-cam/webpage/cutPreview.mjs'),{extractionDraft}=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
  view.fiveAxisArea='toolpath';selectTool(view,'extract');view.fiveAxisExtractionOpen=true;
  const state=extractionDraft(view).extraction,source=view.viewport.sceneObjects.get('part'),geometry=source.geometry;
  state.mode='Faces';state.faces=[{localId:'face'}];state.faceRanges={face:[{first:0,count:geometry.index.count/3}]};
  updateCutPreview(view);let group=view.fiveCutPreview.group;
  const error=()=>{view.viewport.scene.updateMatrixWorld(true);const mesh=group.children.find(m=>m.userData.selectedMachiningFaces),positions=mesh.geometry.attributes.position;mesh.updateWorldMatrix(true,false);let maximum=0;for(let i=0;i<positions.count;i++){const actual=new THREE.Vector3().fromBufferAttribute(positions,i).applyMatrix4(mesh.matrixWorld),expected=new THREE.Vector3().fromBufferAttribute(geometry.attributes.position,geometry.index.getX(i)).applyMatrix4(source.matrixWorld);maximum=Math.max(maximum,actual.distanceTo(expected));}return maximum;};
  const before=error();source.matrix.premultiply(new THREE.Matrix4().makeRotationZ(Math.PI/6));source.matrixWorldNeedsUpdate=true;const after=error();
  if(before>1e-6||after>1e-6)throw Error('Selected machining faces do not follow the displayed workpiece: '+before+', '+after);
  const sourceGeometry=source.geometry;disposeCutPreview(view);if(source.geometry!==sourceGeometry)throw Error('Disposing highlight replaced workpiece geometry');
  return {case:'selected machining faces remain on the actual workpiece before and after 30 degree support rotation',passed:true,maximumVertexError:Math.max(before,after)};
 });report.cases.push(overlay);assert.deepEqual(errors,[]);report.passed=true;
 await page.screenshot({path:resolve(output,'mounted-placement.png')});
}finally{writeFileSync(resolve(output,'mounted-placement-report.json'),JSON.stringify({...report,errors},null,2));await browser.close();}
console.log(JSON.stringify(report));
