import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

const near=(a,b,tolerance=1e-8)=>{assert.equal(a.length,b.length);a.forEach((n,i)=>assert.ok(Math.abs(n-b[i])<=tolerance,`${a} != ${b}`));};
const sameAnchor=(a,b)=>a.sourceSpanId===b.sourceSpanId&&Math.abs(a.nativeParameter-b.nativeParameter)<1e-10;
const frame=page=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
const distance=(point,segment)=>{
  const dx=segment.tip.x-segment.origin.x,dy=segment.tip.y-segment.origin.y,n=dx*dx+dy*dy;
  const t=n?Math.max(0,Math.min(1,((point.x-segment.origin.x)*dx+(point.y-segment.origin.y)*dy)/n)):0;
  return Math.hypot(point.x-segment.origin.x-t*dx,point.y-segment.origin.y-t*dy);
};

// Reconstruct the actual fixed-pixel glyphs from rendered GPU instance data.
// Only mouse events select poses; these calculations choose test coordinates.
async function shownGlyphs(page){
  await frame(page);
  return page.evaluate(()=>{
    const v=fixture.view.viewport,b=v.renderer.domElement.getBoundingClientRect(),rows=[];
    const project=point=>({x:b.left+(point.x+1)*b.width/2,y:b.top+(1-point.y)*b.height/2});
    for(const cut of fixture.view.fiveToolAxisDisplay.group.children)for(const mesh of cut.children){
      const kind=mesh.userData.kind,length=22*mesh.material.uniforms.size.value,shown=mesh.geometry.attributes.instanceShown.array;
      for(let index=0;index<shown.length;index++){
        if(!shown[index])continue;const sample=mesh.userData.toolAxisSamples[index],direction=kind==='axis'?sample.direction:sample.surfaceNormal;
        const local=v.camera.position.clone().set(...sample.point),a=local.clone().applyMatrix4(cut.matrixWorld).project(v.camera),next=local.clone().add(v.camera.position.clone().set(...direction)).applyMatrix4(cut.matrixWorld).project(v.camera);
        const origin=project(a),end=project(next),size=Math.hypot(end.x-origin.x,end.y-origin.y);if(size<.01||a.z<-1||a.z>1)continue;
        const u={x:(end.x-origin.x)/size,y:(end.y-origin.y)/size},tip={x:origin.x+u.x*length,y:origin.y+u.y*length};
        rows.push({kind,intentId:cut.userData.intentId,anchor:sample.anchor,sample,index,origin,tip,u,length});
      }
    }
    return{box:{left:b.left,right:b.right,top:b.top,bottom:b.bottom,width:b.width,height:b.height},glyphs:rows};
  });
}
function isolatedOffset(data,{kind='axis',part='shaft',excludeIntentId=null,onlyIntentId=null,offset=8,clearance=offset+4,predicate=()=>true}={}){
  const axes=data.glyphs.filter(g=>g.kind==='axis');
  for(const glyph of data.glyphs){
    if(glyph.kind!==kind||glyph.intentId===excludeIntentId||onlyIntentId&&glyph.intentId!==onlyIntentId||!predicate(glyph))continue;
    for(const side of [1,-1]){
      const at=part==='tip'?1:.5,p={x:glyph.origin.x+(glyph.tip.x-glyph.origin.x)*at-glyph.u.y*offset*side,y:glyph.origin.y+(glyph.tip.y-glyph.origin.y)*at+glyph.u.x*offset*side};
      if(p.x<data.box.left+35||p.x>data.box.right-35||p.y<data.box.top+135||p.y>data.box.bottom-125)continue;
      const other=axes.filter(a=>a!==glyph).map(a=>distance(p,a));
      if(other.some(d=>d<clearance))continue;
      return{...p,glyph,otherDistance:other.length?Math.min(...other):Infinity};
    }
  }
  throw Error(`No unambiguous ${kind} ${part} hit at ${offset} pixels from its rendered stroke`);
}

export async function verifyPoseSelection({page,rpc,calls,output}){
  page.setDefaultTimeout(60000);console.log('Pose selection: actual-model test started');
  const shaderErrors=[];page.on('console',message=>{if(message.type()==='error'&&/WebGLProgram|Shader Error|VALIDATE_STATUS|GL_INVALID_OPERATION/i.test(message.text()))shaderErrors.push(message.text());});
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload}),read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id),cases=[];
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisPointEdit?.loading&&!fixture.view.fiveToolAxisDisplay?.loading);
  const field=a=>page.getByRole('textbox',{name:`关键点 ${a}`,exact:true});
  let state=await read();const setup=state.setups.find(s=>s.id===state.activeSetupId),initialOwner=setup.definitionOrder[2],referenceOwner=setup.definitionOrder.at(-1);
  // This isolated bridge-only setup makes a visible gray reference point in the
  // opposite direction from its purple axis. The saved input project is untouched.
  const reference=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:referenceOwner});
  await invoke('CamCut.SetToolAxis',{commandId:crypto.randomUUID(),expected:state.expected,intentId:referenceOwner,mode:'Fixed',scope:'WholePath',frame:'Workpiece',direction:reference.surfaceNormal.map(n=>-n)});
  await command('five.refresh');await idle();await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await page.getByRole('tab',{name:'切割',exact:true}).click();await idle();
  await page.locator(`[data-five-intent="${initialOwner}"]`).click();await command('five.points.edit');await idle();await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('IJK');await field('I').waitFor();
  assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator?.control.mode),'rotate');
  if(await page.locator('[data-five-command="five.display.numbers"]').getAttribute('aria-pressed')==='true')await command('five.display.numbers');
  const baseline=await read(),writesAtStart=calls.filter(c=>/^(CamCut\.EditToolAxisPoint|CamCut\.SetToolAxis|CamPlacement\.SetTransform|CamProcess\.Update|CamCut\.Create|CamPlan\.Generate)$/.test(c.method||'')).length;
  await page.getByRole('button',{name:'姿态选择模式',exact:true}).click();await idle();await frame(page);
  assert.equal(await page.evaluate(()=>fixture.view.fivePoseSelection.active),true);assert.equal(await page.getByRole('button',{name:'姿态选择模式',exact:true}).getAttribute('aria-pressed'),'true');
  assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),false);assert.equal(await page.evaluate(()=>fixture.view.viewport.cameraNavigationEnabled),true);assert.equal(await page.evaluate(()=>fixture.view.fiveToolAxisDisplay.visible),true);
  const objectSelection=await page.evaluate(()=>fixture.view.viewport.getDebugState().selectedObjectIds),camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  cases.push('entering pose-selection mode removes the pose manipulator, forces visible axes and enables camera navigation without writing machining state');

  const box=await page.evaluate(()=>fixture.view.viewport.renderer.domElement.getBoundingClientRect().toJSON()),nav={x:box.x+box.width*.55,y:box.y+box.height*.35};
  await page.mouse.move(nav.x,nav.y);await page.mouse.down({button:'middle'});await page.mouse.move(nav.x+50,nav.y+25,{steps:8});await page.mouse.up({button:'middle'});await frame(page);
  const orbited=await page.evaluate(()=>fixture.view.viewport.getCameraState());assert.ok(orbited.theta!==camera.theta||orbited.phi!==camera.phi,'middle drag orbits in selection mode');
  await page.keyboard.down('Control');
  await page.mouse.move(nav.x,nav.y);await page.mouse.down({button:'middle'});await page.mouse.move(nav.x+26,nav.y+17,{steps:7});await page.mouse.up({button:'middle'});await frame(page);
  await page.keyboard.up('Control');
  const panned=await page.evaluate(()=>fixture.view.viewport.getCameraState());assert.notDeepEqual(panned.target,orbited.target,'Ctrl + middle drag pans in selection mode');
  await page.mouse.wheel(0,-200);await frame(page);assert.notEqual((await page.evaluate(()=>fixture.view.viewport.getCameraState())).radius,panned.radius,'wheel zoom remains available');
  await page.mouse.move(nav.x,nav.y);await page.mouse.down();await page.mouse.move(nav.x+100,nav.y+75,{steps:8});await page.mouse.up();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fivePoseSelection.selected),null,'navigation and left drag do not select a pose');assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getDebugState().selectedObjectIds),objectSelection);assert.deepEqual(await read(),baseline);
  await page.evaluate(camera=>fixture.view.viewport.setCameraState(camera),camera);await frame(page);
  cases.push('actual middle-orbit, Ctrl + middle-pan, wheel-zoom and left drag preserve native state and CAD selection and never start a pose edit');

  const currentPick=()=>page.evaluate(()=>fixture.view.fivePoseSelection.selected);
  const waitPicked=async target=>{
    await page.waitForFunction(target=>{const s=fixture.view.fivePoseSelection.selected,d=fixture.view.fiveAxisPointEdit;return s?.intentId===target.intentId&&s.sample.anchor.sourceSpanId===target.anchor.sourceSpanId&&Math.abs(s.sample.anchor.nativeParameter-target.anchor.nativeParameter)<1e-10&&d?.id===target.intentId&&!d.loading;},{intentId:target.intentId,anchor:target.anchor});
    const selected=await currentPick();assert.equal(selected.intentId,target.intentId);assert.ok(sameAnchor(selected.sample.anchor,target.anchor));assert.ok(selected.distance<=10.001);await frame(page);
    const native=await read(),truth=await invoke('CamCut.ReadToolAxis',{expected:native.expected,intentId:target.intentId,anchor:target.anchor});
    near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),await page.evaluate(truth=>fixture.view.fiveAxisPointEdit.frame==='Setup'?truth.setupDirection:truth.direction,truth));
    assert.equal(await page.locator('[data-five-point-field="0"][readonly],[data-five-point-field="1"][readonly],[data-five-point-field="2"][readonly]').count(),3);assert.equal(await page.locator('[data-five-command="five.points.apply"]').isDisabled(),true);
    assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),false);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getDebugState().selectedObjectIds),objectSelection);return selected;
  };
  let data=await shownGlyphs(page);const shaft=isolatedOffset(data,{part:'shaft',excludeIntentId:initialOwner});
  await page.mouse.move(shaft.x,shaft.y);await page.waitForFunction(({id,index})=>fixture.view.fivePoseSelection.hover?.intentId===id&&fixture.view.fivePoseSelection.hover.sampleIndex===index,{id:shaft.glyph.intentId,index:shaft.glyph.index});await frame(page);
  const hover=await page.evaluate(id=>{const group=fixture.view.fiveToolAxisDisplay.group.children.find(g=>g.userData.intentId===id),mesh=group.children.find(m=>m.userData.kind==='axis');return mesh.material.uniforms.hoverIndex.value;},shaft.glyph.intentId);assert.equal(hover,shaft.glyph.index,'hover reaches the rendered shader instance');
  assert.deepEqual(await read(),baseline);await page.mouse.click(shaft.x,shaft.y);await waitPicked(shaft.glyph);
  assert.equal(await page.evaluate(()=>!!fixture.view.fiveAxisPointEdit.newPoint),false);assert.ok(shaft.glyph.sample.keypointIds.length);assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.dirty),false);
  data=await shownGlyphs(page);const tip=isolatedOffset(data,{part:'tip',excludeIntentId:shaft.glyph.intentId});await page.mouse.click(tip.x,tip.y);await waitPicked(tip.glyph);assert.deepEqual(await read(),baseline);
  const tipAxis=await invoke('CamCut.ReadToolAxis',{expected:baseline.expected,intentId:tip.glyph.intentId,anchor:tip.glyph.anchor});await page.getByRole('combobox',{name:'关键点刀轴坐标系',exact:true}).selectOption('Workpiece');near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),tipAxis.direction);await page.getByRole('combobox',{name:'关键点刀轴坐标系',exact:true}).selectOption('Setup');near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),tipAxis.setupDirection);assert.deepEqual(await read(),baseline);
  await page.mouse.move(tip.x,tip.y);await frame(page);await page.screenshot({path:resolve(output,'pose-selection-hit.png')});
  cases.push('real hover plus shaft/tip clicks eight pixels from the visible stroke select exact displayed native anchors and owners; IJK is read-only and sampled points are not inserted or saved');
  cases.push('read-only inspection can display IJK in Workpiece or Setup coordinates without modifying machining policy');
  const pinCamera=await page.evaluate(()=>fixture.view.viewport.getCameraState()),pinCalls=calls.filter(c=>c.method==='CamCut.ReadToolAxisSamples').length;
  await page.evaluate(c=>fixture.view.viewport.setCameraState({...c,theta:c.theta+.06,phi:c.phi+.025,radius:c.radius*.94}),pinCamera);await frame(page);
  const pin=await page.evaluate(id=>{const group=fixture.view.fiveToolAxisDisplay.group.children.find(g=>g.userData.intentId===id),mesh=group.children.find(m=>m.userData.kind==='axis'),index=mesh.material.uniforms.selectedIndex.value;return{index,shown:mesh.geometry.attributes.instanceShown.array[index]};},tip.glyph.intentId);assert.ok(pin.index>=0);assert.equal(pin.shown,1,'selected source station stays visible after camera movement');assert.ok(sameAnchor((await currentPick()).sample.anchor,tip.glyph.anchor));assert.equal(calls.filter(c=>c.method==='CamCut.ReadToolAxisSamples').length,pinCalls);await page.evaluate(c=>fixture.view.viewport.setCameraState(c),pinCamera);await frame(page);
  cases.push('camera orbit and zoom preserve the selected exact source station in the display density mask without rereading native CAD');
  console.log('Pose selection: navigation, 8px shaft/tip and native readonly inspector passed');

  // Find visible source lines and model faces away from every purple axis.
  data=await shownGlyphs(page);const ordinary=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view.viewport,b=v.renderer.domElement.getBoundingClientRect(),rows=[];
    const add=(kind,p)=>{const q=p.clone().project(v.camera),x=b.left+(q.x+1)*b.width/2,y=b.top+(1-q.y)*b.height/2;if(q.z>=-1&&q.z<=1&&x>b.left+35&&x<b.right-35&&y>b.top+135&&y<b.bottom-125)rows.push({kind,x,y});};
    for(const cut of fixture.view.scene.intentToolpaths){const object=v.sceneObjects.get(cut.placementId);for(let i=1;i<cut.previewPoints.length-1;i+=3)add('line',new THREE.Vector3(...cut.previewPoints[i]).applyMatrix4(object.matrixWorld));}
    const object=[...v.sceneObjects.values()][0],position=object.geometry.attributes.position,index=object.geometry.index;
    for(let i=0;i<(index?.count||position.count)-3;i+=111){const ids=[0,1,2].map(k=>index?index.getX(i+k):i+k),p=new THREE.Vector3();for(const j of ids)p.add(new THREE.Vector3().fromBufferAttribute(position,j));add('model',p.multiplyScalar(1/3).applyMatrix4(object.matrixWorld));}
    return rows;
  });
  const axes=data.glyphs.filter(g=>g.kind==='axis'),away=point=>axes.every(a=>distance(point,a)>16);
  for(const kind of ['line','model']){const p=ordinary.find(p=>p.kind===kind&&away(p));assert.ok(p,`ordinary ${kind} target away from pose arrows`);const before=await currentPick();await page.mouse.click(p.x,p.y);await frame(page);assert.deepEqual(await currentPick(),before,`${kind} clicks are consumed without selecting paths or CAD objects`);}
  const beforeBlank=await currentPick();await page.mouse.click(box.right-25,box.top+box.height*.6);await frame(page);assert.deepEqual(await currentPick(),beforeBlank);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getDebugState().selectedObjectIds),objectSelection);
  // With only keypoints visible, the trim's sole start/end may lie near a
  // canvas edge. Center that real keypoint before exercising its gray arrow.
  await page.evaluate(({camera,point})=>fixture.view.viewport.setCameraState({...camera,target:{x:point[0],y:point[1],z:point[2]},radius:camera.radius*.5}),{camera,point:reference.setupPoint});await frame(page);
  const gray=isolatedOffset(await shownGlyphs(page),{kind:'normal',part:'tip',onlyIntentId:referenceOwner,offset:0,clearance:14.1});await page.mouse.click(gray.x,gray.y);await frame(page);assert.deepEqual(await currentPick(),beforeBlank,'gray reference normals are excluded from pose selection');assert.deepEqual(await read(),baseline);
  await page.evaluate(camera=>fixture.view.viewport.setCameraState(camera),camera);await frame(page);
  cases.push('ordinary cutting lines, model faces, blank canvas and distinct gray reference arrows cannot replace the selected pose or select CAD objects');

  // An overlapped screen hit must remain deterministic across repeat clicks.
  await page.evaluate(camera=>fixture.view.viewport.setCameraState({...camera,radius:camera.radius*1.8}),camera);await frame(page);data=await shownGlyphs(page);
  let overlap;for(const a of data.glyphs.filter(g=>g.kind==='axis')){for(const at of [.4,.7,1]){const p={x:a.origin.x+(a.tip.x-a.origin.x)*at,y:a.origin.y+(a.tip.y-a.origin.y)*at};if(p.x<data.box.left+35||p.x>data.box.right-35||p.y<data.box.top+135||p.y>data.box.bottom-125)continue;const candidates=data.glyphs.filter(g=>g.kind==='axis'&&distance(p,g)<8);if(candidates.length>1){overlap={...p,candidates:candidates.length};break;}}if(overlap)break;}
  assert.ok(overlap,'actual fit model contains overlapping displayed-axis hit areas after zooming out');let stable;
  for(let i=0;i<3;i++){await page.mouse.click(overlap.x,overlap.y);await idle();const hit=await currentPick(),signature=JSON.stringify([hit.intentId,hit.sample.anchor]);if(stable)assert.equal(signature,stable);else stable=signature;}
  await page.evaluate(camera=>fixture.view.viewport.setCameraState(camera),camera);await frame(page);assert.deepEqual(await read(),baseline);
  cases.push('overlapping displayed-axis hit areas select the same owner/source anchor on repeated clicks');

  state=await read();await invoke('CamCut.ManageTree',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,intentIds:[shaft.glyph.intentId],action:'Lock'});await command('five.refresh');await idle();
  data=await shownGlyphs(page);const locked=isolatedOffset(data,{part:'tip',onlyIntentId:shaft.glyph.intentId});await page.mouse.click(locked.x,locked.y);await waitPicked(locked.glyph);const lockedState=await read();
  await command('five.points.edit');await idle();assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),false);assert.deepEqual(await read(),lockedState,'locked pose inspection cannot start a modifying operation');
  if(!await page.evaluate(()=>fixture.view.fivePoseSelection.active))await command('five.pose-select.toggle');
  state=await read();await invoke('CamCut.ManageTree',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,intentIds:[shaft.glyph.intentId],action:'Unlock'});await command('five.refresh');await idle();
  cases.push('locked paths remain selectable as read-only poses; Edit cannot expose a manipulator or save changes');

  // Hold an old exact-axis read while a newer pose is selected and focused.
  data=await shownGlyphs(page);const lateA=isolatedOffset(data,{part:'shaft',onlyIntentId:shaft.glyph.intentId}),lateB=isolatedOffset(data,{part:'tip',excludeIntentId:lateA.glyph.intentId});
  await page.evaluate(()=>{fixture.poseOriginalInvoke=fixture.context.sceneProxy.invoke;fixture.poseDelayedReadDone=false;fixture.context.sceneProxy.invoke=async function(method,payload){const delayed=method==='CamCut.ReadToolAxis'&&fixture.hold;const result=await fixture.poseOriginalInvoke.call(this,method,payload);if(delayed)fixture.poseDelayedReadDone=true;return result;};fixture.holdMethod='CamCut.ReadToolAxis';fixture.hold=true;});
  await page.mouse.click(lateA.x,lateA.y);await page.waitForFunction(()=>fixture.held);await page.mouse.click(lateB.x,lateB.y);await waitPicked(lateB.glyph);
  await page.evaluate(()=>{fixture.poseLatestInput=document.querySelector('[aria-label="关键点 K"]');fixture.poseLatestInput.focus({preventScroll:true});fixture.poseLatestInput.setSelectionRange(0,1);fixture.release();});await page.waitForFunction(()=>fixture.poseDelayedReadDone);await frame(page);
  const latest=await currentPick();assert.equal(latest.intentId,lateB.glyph.intentId);assert.ok(sameAnchor(latest.sample.anchor,lateB.glyph.anchor));assert.equal(await page.evaluate(()=>fixture.poseLatestInput===document.querySelector('[aria-label="关键点 K"]')&&document.activeElement===fixture.poseLatestInput),true);assert.deepEqual(await field('K').evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,1]);await page.evaluate(()=>fixture.context.sceneProxy.invoke=fixture.poseOriginalInvoke);
  cases.push('a delayed old pose read cannot steal the newer selected anchor, input node, focus or text selection');

  await command('five.pose-select.exit');await idle();assert.equal(await page.evaluate(()=>fixture.view.fivePoseSelection.active),false);assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator?.control.mode),'rotate');assert.equal(await field('I').getAttribute('readonly'),null);
  await command('five.pose-select.toggle');await idle();data=await shownGlyphs(page);const edit=isolatedOffset(data,{part:'shaft',onlyIntentId:tip.glyph.intentId});await page.mouse.click(edit.x,edit.y);await waitPicked(edit.glyph);await command('five.points.edit');await idle();assert.equal(await page.evaluate(()=>fixture.view.fivePoseSelection.active),false);assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator?.control.mode),'rotate');
  const beforeTyping=await read();await field('I').fill('0.15');assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.dirty),true);assert.deepEqual(await read(),beforeTyping);await field('I').press('Escape');await idle();assert.deepEqual(await read(),beforeTyping);
  assert.equal(calls.filter(c=>/^(CamCut\.EditToolAxisPoint|CamCut\.SetToolAxis|CamPlacement\.SetTransform|CamProcess\.Update|CamCut\.Create|CamPlan\.Generate)$/.test(c.method||'')).length,writesAtStart,'selection and resumed draft editing never auto-save machining changes');
  cases.push('mode Exit and explicit Edit restore the ordinary point gizmo and editable IJK; typing remains a cancellable unsaved draft');
  await command('five.pose-select.toggle');await idle();await page.evaluate(()=>fixture.view.viewport.fitView());await frame(page);
  state=await read();const existing=await invoke('CamCut.ListToolAxisPoints',{expected:state.expected,intentId:shaft.glyph.intentId});data=await shownGlyphs(page);
  const history=isolatedOffset(data,{part:'shaft',onlyIntentId:shaft.glyph.intentId});await page.mouse.click(history.x,history.y);await waitPicked(history.glyph);
  state=await read();const v=[...history.glyph.sample.direction],component=Math.abs(v[0])<.9?0:1;v[component]+=.2;const n=Math.hypot(...v),direction=v.map(x=>x/n);
  await invoke('CamCut.SetToolAxis',{commandId:crypto.randomUUID(),expected:state.expected,intentId:history.glyph.intentId,mode:'Fixed',scope:'WholePath',frame:'Workpiece',direction});await command('five.refresh');await idle();await frame(page);
  const updatedPose=await read();assert.equal(updatedPose.expected.epoch,state.expected.epoch);assert.notDeepEqual(updatedPose.intentToolpaths.find(c=>c.id===history.glyph.intentId).poseRef,state.intentToolpaths.find(c=>c.id===history.glyph.intentId).poseRef,'history preparation changes the policy inside the same scene epoch');
  const activeAfterPoseChange=await page.evaluate(()=>fixture.view.fivePoseSelection.active);
  if(activeAfterPoseChange){assert.equal(await currentPick(),null,'changed pose policy clears the old selected source sample');assert.equal(await page.evaluate(()=>fixture.view.fivePoseSelection.hover),null);assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit),null);assert.equal(await field('I').count(),0,'old IJK inspector must not silently switch to the path start');assert.equal(await page.getByRole('button',{name:'姿态选择模式',exact:true}).getAttribute('aria-pressed'),'true');}
  else{assert.equal(await page.getByRole('button',{name:'姿态选择模式',exact:true}).getAttribute('aria-pressed'),'false');assert.equal((await page.locator('.five-point-scene-hint').allTextContents()).some(text=>text.includes('姿态选择中')),false,'an invalidated mode exits in both state and DOM');}
  cases.push('a pose policy change within the same epoch clears the selected sample and old IJK inspector before rendering; picking a fresh arrow then reads its updated native axis');
  if(!await page.evaluate(()=>fixture.view.fivePoseSelection.active))await command('five.pose-select.toggle');await idle();data=await shownGlyphs(page);const finalHit=isolatedOffset(data,{part:'tip',onlyIntentId:history.glyph.intentId});await page.mouse.click(finalHit.x,finalHit.y);await waitPicked(finalHit.glyph);await page.mouse.move(finalHit.x,finalHit.y);await frame(page);await page.screenshot({path:resolve(output,'pose-selection-mode.png')});
  const controls=[];for(const [width,height] of [[1024,700],[1440,900],[1920,1080]]){await page.setViewportSize({width,height});await frame(page);const rects=await page.evaluate(()=>{const box=n=>{const r=n.getBoundingClientRect();return{left:r.left,right:r.right,top:r.top,bottom:r.bottom};};return{canvas:box(fixture.view.viewport.renderer.domElement),control:box(document.querySelector('[data-five-command="five.pose-select.toggle"]'))};});assert.ok(rects.control.left>=rects.canvas.left&&rects.control.right<=rects.canvas.right&&rects.control.top>=rects.canvas.top&&rects.control.bottom<=rects.canvas.bottom,`${width}x${height}: pose-mode control remains inside canvas`);controls.push({width,height,...rects});}
  assert.deepEqual(shaderErrors,[],'displayed pose hover/selection highlights render without shader errors');
  writeFileSync(resolve(output,'pose-selection-report.json'),JSON.stringify({passed:true,operations:setup.definitionOrder.length,cases,controls,hits:{shaft:shaft.glyph,tip:tip.glyph,reference:gray.glyph,overlap},coverage:{native:true,realMouse:true,browser:'Edge',standaloneCef:false,scope:'navigation and readonly pose picking; explicit edit resumes existing keypoint interaction'}},null,2));
}
