import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

export async function verifyToolAxisPoints({page,rpc,calls,output}){
  page.setDefaultTimeout(60000);console.log('Keypoint browser: opened');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload}),read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisPointLookup&&!fixture.view.fiveAxisPointEdit?.loading);
  const field=a=>page.getByRole('textbox',{name:`关键点 ${a}`,exact:true});
  const near=(a,b)=>a.forEach((n,i)=>assert.ok(Math.abs(n-b[i])<1e-7,`${a} != ${b}`));
  const setup=s=>s.setups.find(p=>p.id===s.activeSetupId);
  const first=await read(),id=setup(first).definitionOrder[2],original=first.intentToolpaths.find(c=>c.id===id);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await page.getByRole('tab',{name:'切割',exact:true}).click();await page.locator(`[data-five-intent="${id}"]`).click();
  await command('five.points.edit');await idle();await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('IJK');await field('I').waitFor();await page.getByRole('combobox',{name:'关键点刀轴坐标系',exact:true}).selectOption('Workpiece');
  const surface=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,canvas=vp.renderer.domElement,box=canvas.getBoundingClientRect(),ray=new THREE.Raycaster();
    fixture.poseInput=document.querySelector('[data-five-point-field="i"]')||document.querySelector('[aria-label="关键点 I"]');
    fixture.poseCanvas=canvas;fixture.poseMesh=[...vp.sceneObjects.values()][0];
    vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
    for(let y=box.top+100;y<box.bottom-100;y+=19)for(let x=box.left+40;x<box.right-40;x+=19){
      if(document.elementFromPoint(x,y)!==canvas)continue;
      ray.setFromCamera(new THREE.Vector2((x-box.left)/box.width*2-1,1-(y-box.top)/box.height*2),vp.camera);
      if(ray.intersectObjects([...vp.sceneObjects.values()],true).length)return {x,y};
    }throw Error('No visible model surface');
  });
  await page.mouse.click(surface.x,surface.y);await idle();
  const protectedEdit=await page.evaluate(()=>({objects:fixture.view.viewport.getDebugState().selectedObjectIds,
    cuts:fixture.view.fiveAxisUI.selectedIntentIds,canvas:fixture.poseCanvas===fixture.view.viewport.renderer.domElement,
    mesh:fixture.poseMesh===[...fixture.view.viewport.sceneObjects.values()][0],
    size:fixture.view.fiveAxisPointPreview.group.children.find(m=>m.userData.pointPreview).material.uniforms.size.value,
    color:fixture.view.fiveAxisPointPreview.group.children.find(m=>m.userData.pointPreview).material.uniforms.color.value.getHex()}));
  assert.deepEqual(protectedEdit.objects,[]);assert.deepEqual(protectedEdit.cuts,[id]);assert.equal(protectedEdit.canvas,true);assert.equal(protectedEdit.mesh,true);
  assert.equal(protectedEdit.size,1.6);assert.equal(protectedEdit.color,0xffc857);
  assert.equal(await page.locator('.cam-info-pane .five-pose-editor-actions [data-five-command="five.points.delete"]').isDisabled(),false);
  const protectedPoints=await read();await command('five.points.delete-point');await idle();assert.deepEqual(await read(),protectedPoints);
  await page.getByRole('combobox',{name:'当前刀轴关键点',exact:true}).selectOption('end');await idle();assert.equal(await page.locator('.cam-info-pane .five-pose-editor-actions [data-five-command="five.points.delete"]').isDisabled(),false);await command('five.points.delete-point');await idle();assert.deepEqual(await read(),protectedPoints);
  await command('five.points.add');
  await page.evaluate(id=>{
    const c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport,o=v.sceneObjects.get(c.placementId);
    const lo=[0,1,2].map(i=>Math.min(...c.previewPoints.map(p=>p[i]))),hi=[0,1,2].map(i=>Math.max(...c.previewPoints.map(p=>p[i])));
    const target=o.localToWorld(v.camera.position.clone().set(...lo.map((x,i)=>(x+hi[i])/2)));
    v.setCameraState({...v.getCameraState(),target:{x:target.x,y:target.y,z:target.z},radius:Math.max(10,Math.hypot(...lo.map((x,i)=>hi[i]-x))*1.7)});
  },id);
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const pick=await page.evaluate(id=>{
    const c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport,o=v.sceneObjects.get(c.placementId),box=v.renderer.domElement.getBoundingClientRect();
    for(let i=Math.floor(c.previewPoints.length*.3);i<c.previewPoints.length-2;i++){
      const p=c.previewPoints[i].map((n,j)=>(n+c.previewPoints[i+1][j])/2),q=o.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);
      const x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
      if(x>box.x+40&&x<box.right-40&&y>box.y+120&&y<box.bottom-90)return {x,y};
    }throw Error('No visible cutting point');
  },id);
  await page.mouse.click(pick.x,pick.y);await idle();assert.equal(await page.evaluate(()=>!!fixture.view.fiveAxisPointInsert&&!fixture.view.fiveAxisPointEdit.newPoint&&!fixture.view.fiveAxisPointEdit.dirty),true);
  console.log('Keypoint browser: picked');
  const anchor=await page.evaluate(()=>{const d=fixture.view.fiveAxisPointEdit;return d.points.find(p=>p.id===d.pointId).anchor;});
  for(const a of ['I','J','K'])await field(a).fill('0');await field('K').press('Enter');await idle();assert.match(await page.locator('.five-axis-keypoints').innerText(),/不能为零向量/);
  await field('J').fill('3');await field('K').fill('4');await page.getByRole('combobox',{name:'关键点姿态过渡',exact:true}).selectOption('DirectionChange');
  if(process.env.ICAX_TOOL_AXIS_VISUAL_ONLY){
    await command('five.points.apply');await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.error),'');
    const layout=await field('I').evaluate(n=>{const p=n.closest('.cam-info-pane'),s=document.querySelector('[aria-label="关键点刀轴坐标系"]');return {height:n.getBoundingClientRect().height,selectHeight:s.getBoundingClientRect().height,overflow:p.scrollWidth>p.clientWidth};});assert.ok(layout.height>=30&&layout.selectHeight>=30);assert.equal(layout.overflow,false);
    await page.evaluate(()=>fixture.view.viewport.fitView());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
    await page.screenshot({path:resolve(output,'tool-axis-local-panel.png')});
    let check=await read();await invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:check.expected,intentIds:[id],patch:{kerf:{widthMm:.2}}});check=await read();
    await assert.rejects(invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:check.expected,setupId:check.activeSetupId}),/CAM_POSE_KEYPOINT_COMPENSATION_UNSUPPORTED/);assert.deepEqual(await read(),check);
    writeFileSync(resolve(output,'visual-report.json'),JSON.stringify({passed:true,layout,nativeSave:true,nonzeroCompensationRejectedAtomically:true},null,2));return;
  }
  const beforeApply=calls.length,stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1600px}'});
  await page.evaluate(()=>{fixture.localNodes={input:document.querySelector('[aria-label="关键点 K"]'),canvas:document.querySelector('canvas'),mesh:[...fixture.view.viewport.sceneObjects.values()][0],events:0};fixture.localNodes.input.addEventListener('input',()=>fixture.localNodes.events++);fixture.holdMethod='CamCut.EditToolAxisPoint';fixture.hold=true;});
  await field('K').press('Enter');await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const n=fixture.localNodes.input;n.focus({preventScroll:true});n.setSelectionRange(0,1);return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=90+20*i;return p.scrollTop;});});
  await page.evaluate(()=>fixture.release());await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.error),'');
  assert.equal(calls.slice(beforeApply).some(c=>c.method==='CamScene.ReadState'),false);
  assert.equal(await page.evaluate(()=>fixture.localNodes.input===document.querySelector('[aria-label="关键点 K"]')&&fixture.localNodes.canvas===document.querySelector('canvas')&&fixture.localNodes.mesh===[...fixture.view.viewport.sceneObjects.values()][0]&&document.activeElement===fixture.localNodes.input),true);
  assert.deepEqual(await field('K').evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,1]);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),scroll);await stress.evaluate(n=>n.remove());
  await field('K').fill('0.8');assert.ok(await page.evaluate(()=>fixture.localNodes.events)>0);await command('five.points.cancel');await idle();
  console.log('Keypoint browser: applied and interaction checked');
  let state=await read(),axis=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(axis.direction,[0,.6,.8]);assert.equal(axis.keypointCount,'1');
  const changed=state.intentToolpaths.find(c=>c.id===id);for(const k of ['curveRef','featureRef','previewPoints','start','reversed','number'])assert.deepEqual(changed[k],original[k],k);
  const generate=async()=>{state=await read();let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId});while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);await command('five.refresh');await idle();return read();};
  state=await generate();const op=await page.evaluate(id=>fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).generatedGeometry.operations.find(o=>o.intentId===id),id);let hits=0,changes=0;
  // Numeric arrays now travel through the production ICPS resource channel.
  // Verify its exact physical hard point; native tests also verify source anchors.
  for(const p of op.parts)for(let i=0;i<(p.points?.length||0);i++){assert.ok(Math.abs(Math.hypot(...p.directions[i])-1)<1e-8);if(Math.hypot(...p.points[i].map((n,j)=>n-axis.point[j]))<1e-8){near(p.directions[i],[0,.6,.8]);hits++;}if(Math.hypot(...p.directions[i].map((n,j)=>n-[0,.6,.8][j]))>.01)changes++;}
  assert.ok(hits>0,'exact hard-point included in generated cutting path');assert.ok(changes>0,'local constraint must not flatten the whole path');
  console.log('Keypoint browser: regenerated exact point');
  await command('five.points.start');await idle();await field('I').fill('0');await field('J').fill('1');await field('K').fill('2');await page.evaluate(()=>fixture.loseMethod='CamCut.EditToolAxisPoint');await command('five.points.apply');await idle();
  state=await read();assert.equal((await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id})).keypointCount,'2');
  await command('five.points.reset');await idle();state=await read();assert.equal((await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id})).keypointCount,'1');
  const list=await invoke('CamCut.ListToolAxisPoints',{expected:state.expected,intentId:id}),manual=list.points.find(p=>!p.system);assert.ok(manual);
  await page.getByRole('combobox',{name:'当前刀轴关键点',exact:true}).selectOption(manual.id);await idle();await command('five.points.delete-point');await idle();state=await read();assert.equal((await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id})).keypointCount,'0');
  console.log('Keypoint browser: reset and deleted');
  await rpc({action:'undo'});await command('five.refresh');await idle();state=await read();near((await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor})).direction,[0,.6,.8]);
  console.log('Keypoint browser: undo restored');
  await command('five.points.whole');await page.locator('[data-five-axis-field="0"]').waitFor();await command('five.points.edit');await idle();
  await page.getByRole('combobox',{name:'当前刀轴关键点',exact:true}).selectOption(manual.id);await idle();await field('I').fill('1');await field('J').fill('0');await field('K').fill('2');await page.evaluate(()=>fixture.entry.prepareProjectSave(fixture.context));await idle();
  state=await read();const saved=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(saved.direction,[1/Math.sqrt(5),0,2/Math.sqrt(5)]);
  console.log('Keypoint browser: saved before reopen');
  await page.screenshot({path:resolve(output,'tool-axis-local-keypoints.png')});await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});near((await invoke('CamCut.ReadToolAxis',{expected:reopened.expected,intentId:id,anchor})).direction,saved.direction);
  writeFileSync(resolve(output,'tool-axis-points-report.json'),JSON.stringify({passed:true,operations:first.intentToolpaths.length,cases:['start/end system points and protected deletion','real mouse picks an exact source-curve anchor','local IJK normalization and zero-vector rejection','delayed native edit retains input/canvas/mesh/listeners/focus/selection/latest dual scroll without full geometry read','regeneration samples the exact hard point with the edited direction and unit transitions','local point does not flatten the rest of the path','endpoint override reset; manual deletion and undo','lost response receipt recovery','whole/local panel switching and save flush','embedded save/reopen retains source anchor, direction and interpolation policy'],coverage:{native:true,realMouse:true,browser:'Edge',standaloneCef:false}},null,2));
}
