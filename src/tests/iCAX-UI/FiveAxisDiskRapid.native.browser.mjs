import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';
import { verifyOrderingBeforeGeneration } from './FiveAxisOrderingBeforeGeneration.native.browser.mjs';

export async function verifyDiskRapid({page,rpc,root,output,calls,errors}) {
  const expectedCuts=19,expectedRapids=expectedCuts-1;
  page.setDefaultTimeout(180000);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const verifyNumberDisplay=async generated=>{
    const before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    await page.evaluate(()=>{const v=fixture.view;fixture.numberDisplayNodes={canvas:v.viewport.renderer.domElement,model:[...v.viewport.sceneObjects.values()][0],
      cuts:v.fiveCutPreview.group,generated:v.fiveGeneratedPreview?.group,labels:v.viewport.specificationLabels.map(l=>l.element),
      markers:v.fiveCutPreview.group.children.find(c=>c.userData.startMarkers)};});
    assert.equal(await page.locator('.icax-three-specification-label').count(),19);
    await page.getByRole('button',{name:'刀路序号',exact:true}).click();
    assert.equal(await page.getByRole('button',{name:'刀路序号',exact:true}).getAttribute('aria-pressed'),'false');
    assert.equal(await page.locator('.icax-three-specification-label:visible').count(),0);
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));await idle();
    assert.equal(await page.locator('.icax-three-specification-label:visible').count(),0,'hidden numbering survives a scene refresh');
    const nodes=await page.evaluate(()=>{const v=fixture.view,p=fixture.numberDisplayNodes;return {canvas:p.canvas===v.viewport.renderer.domElement,model:p.model===[...v.viewport.sceneObjects.values()][0],
      cuts:p.cuts===v.fiveCutPreview.group,generated:p.generated===v.fiveGeneratedPreview?.group,labels:p.labels.every((e,i)=>e===v.viewport.specificationLabels[i].element),
      markers:p.markers===v.fiveCutPreview.group.children.find(c=>c.userData.startMarkers)&&p.markers.visible};});
    assert.ok(Object.values(nodes).every(Boolean),JSON.stringify(nodes));
    await page.screenshot({path:resolve(output,generated?'generated-numbers-hidden.png':'definition-numbers-hidden.png')});
    await page.getByRole('button',{name:'刀路序号',exact:true}).click();
    assert.ok(await page.locator('.icax-three-specification-label:visible').count()>0);
    assert.deepEqual(await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),before,'number display never changes machining data');
  };
  await selectWorkpieceFile(page,resolve(root,'TestData/FA/2.stp.STEP'));await idle();
  await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1);
  const scale=Number(process.env.ICAX_DISK_SCALE||'1');
  if(scale!==1){
    await page.evaluate(()=>{const m=[...fixture.view.viewport.sceneObjects.values()][0];m.geometry.computeBoundingBox();fixture.diskOriginalWidth=m.geometry.boundingBox.max.x-m.geometry.boundingBox.min.x;});
    await page.evaluate(async()=>{fixture.view.viewport.fitView(1.7);await fixture.entry.handleRibbonCommand(fixture.context,'five.tool.scale');});
    const driver=gizmoDriver(page);await driver.pick('XYZ');await driver.numeric().fill(String(scale));await driver.numeric().press('Enter');await idle();
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.select'));
    await page.waitForFunction(scale=>{const m=[...fixture.view.viewport.sceneObjects.values()][0];m.geometry.computeBoundingBox();return Math.abs(m.geometry.boundingBox.max.x-m.geometry.boundingBox.min.x-fixture.diskOriginalWidth*scale)<.001;},scale);
  }
  await page.evaluate(async()=>{
    await fixture.context.actions.selectRibbonTab('toolpath');
    await fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract');
    fixture.view.viewport.fitView(1.7);
    await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
  });
  // Reproduce the user's 34-face top + outer-wall selection. Each face is
  // clicked through the viewport and production ResolveFace route, including
  // the rear-facing wall after orbiting. Never substitute the half-rim face 8.
  const selectedFaces=[76,78,159,157,156,160,77,158,161,165,164,163,162,74,72,71,182,68,9,70,69,10,11,12,13,14,15,16,17,18,19,20,21,22];
  await page.evaluate(async()=>{
    const THREE=await import(`${fixture.sourceRoot??'/src/'}iCAX-UI/SDK/ThirdParty/three/three.module.js`),v=fixture.view.viewport,d=fixture.view.scene.domain,p=d.placements[0],m=[...v.sceneObjects.values()][0];
    m.updateMatrixWorld(true);m.geometry.computeBoundingBox();const center=m.geometry.boundingBox.getCenter(new THREE.Vector3()).applyMatrix4(m.matrixWorld);
    fixture.diskPickCamera={...v.getCameraState(),target:{x:center.x,y:center.y,z:center.z},radius:m.geometry.boundingBox.getSize(new THREE.Vector3()).length()*2};
    v.setCameraState(fixture.diskPickCamera);window.diskCameraDebug=v.getDebugState();
    fixture.diskFaceMap=await fixture.context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:d.expected,setupId:p.setupId,placementId:p.id});
  });
  // Move the nonmodal dialog with its real title handle before picking the skin.
  const title=await page.locator('[data-five-dialog-drag]').boundingBox(),host=await page.locator('[data-five-overlay]').boundingBox();
  await page.mouse.move(title.x+50,title.y+20);await page.mouse.down();
  await page.mouse.move(host.x+62,host.y+32,{steps:8});await page.mouse.up();
  const pick=[];
  for(const faceId of selectedFaces){
    const p=await page.evaluate(async faceId=>{
      const THREE=await import(`${fixture.sourceRoot??'/src/'}iCAX-UI/SDK/ThirdParty/three/three.module.js`),v=fixture.view.viewport,m=[...v.sceneObjects.values()][0];
      const p=m.geometry.getAttribute('position'),index=m.geometry.index,r=v.renderer.domElement.getBoundingClientRect();
      m.updateMatrixWorld(true);m.geometry.computeBoundingBox();const radius=m.geometry.boundingBox.getSize(new THREE.Vector3()).length()*1.3;
      const triangles=fixture.diskFaceMap.faces.filter(f=>Number(f.faceId)===faceId).flatMap(f=>Array.from({length:Number(f.count)},(_,j)=>{
        const i=Number(f.first)+j,a=new THREE.Vector3().fromBufferAttribute(p,index?index.getX(i*3):i*3),b=new THREE.Vector3().fromBufferAttribute(p,index?index.getX(i*3+1):i*3+1),c=new THREE.Vector3().fromBufferAttribute(p,index?index.getX(i*3+2):i*3+2);
        const normal=b.clone().sub(a).cross(c.clone().sub(a));return {i,area:normal.length(),normal:normal.normalize(),point:a.add(b).add(c).divideScalar(3)};
      })).sort((a,b)=>b.area-a.area);
      for(const t of triangles){const point=t.point.clone().applyMatrix4(m.matrixWorld),direction=t.normal.clone().transformDirection(m.matrixWorld);
        v.setCameraState({...v.getCameraState(),radius,target:{x:point.x,y:point.y,z:point.z},theta:Math.atan2(direction.y,direction.x),phi:Math.acos(direction.z)});
        const ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2(0,0),v.camera);
        if(ray.intersectObject(m,false)[0]?.faceIndex===t.i)return {faceId,triangle:t.i,x:r.left+r.width/2,y:r.top+r.height/2};
      }throw Error(`No visible exterior face ${faceId}`);
    },faceId);pick.push(p);
    const d=await page.locator('[data-five-floating="extraction"]').boundingBox();
    if(p.x>=d.x&&p.x<=d.x+d.width&&p.y>=d.y&&p.y<=d.y+d.height){
      const h=await page.locator('[data-five-overlay]').boundingBox(),t=await page.locator('[data-five-dialog-drag]').boundingBox();
      const corners=[{x:h.x+12,y:h.y+12},{x:h.x+h.width-d.width-12,y:h.y+12},{x:h.x+12,y:h.y+h.height-d.height-12},{x:h.x+h.width-d.width-12,y:h.y+h.height-d.height-12}];
      const dest=corners.sort((a,b)=>Math.hypot(b.x+d.width/2-p.x,b.y+d.height/2-p.y)-Math.hypot(a.x+d.width/2-p.x,a.y+d.height/2-p.y))[0];
      await page.mouse.move(t.x+50,t.y+20);await page.mouse.down();await page.mouse.move(dest.x+50,dest.y+20,{steps:5});await page.mouse.up();
    }
    await page.mouse.click(p.x,p.y);await idle();
    assert.ok(await page.evaluate(async id=>{const module=await import(`${fixture.sourceRoot??'/src/'}apps/five-axis-cam/webpage/extractionActions.mjs`);return module.extractionDraft(fixture.view).extraction.faces.some(f=>Number(f.localId)===id);},faceId),`pointer selected face ${faceId}`);
  }
  await page.evaluate(()=>fixture.view.viewport.setCameraState(fixture.diskPickCamera));
  writeFileSync(resolve(output,'disk-pick.json'),JSON.stringify(pick));
  writeFileSync(resolve(output,'disk-camera.json'),JSON.stringify(await page.evaluate(()=>diskCameraDebug),null,2));
  const client=await page.evaluate(()=>({width:innerWidth,height:innerHeight}));
  assert.ok(pick.every(p=>p.x>=0&&p.y>=0&&p.x<client.width&&p.y<client.height),'every real face pointer pick is inside the browser');
  await page.screenshot({path:resolve(output,'disk-faces-selected.png')});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.run'));await idle();
  writeFileSync(resolve(output,'disk-extraction.json'),JSON.stringify(await page.evaluate(async()=>{
    const module=await import(`${fixture.sourceRoot??'/src/'}apps/five-axis-cam/webpage/extractionActions.mjs`);return module.extractionDraft(fixture.view).extraction.result;
  }),null,2));
  const lowerRim=await page.evaluate(async()=>{const module=await import(`${fixture.sourceRoot??'/src/'}apps/five-axis-cam/webpage/extractionActions.mjs`);return module.extractionResult(fixture.view).candidates.find(c=>c.boundaryRole==='OuterTrim');});
  assert.ok(lowerRim,'the actual lower rim must be classified');
  assert.deepEqual(lowerRim.boundary.sourceSpans.map(s=>Number(s.edgeId)).sort((a,b)=>a-b),[77,80,88,89,90,93,94,95,99,102,105,108,112,120,122,125]);
  assert.ok(Math.abs(lowerRim.boundary.perimeterMm-265.0399444945867*scale)<1e-5);
  assert.ok(lowerRim.boundary.sourceSpans.every(s=>s.faceId!=='8'),'a half-rim face is never a successful full lower-rim test');
  const trimCandidate=lowerRim.candidateId;
  await page.locator(`[data-five-candidate="${trimCandidate}"]`).click();
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  const previous=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.equal(previous.intentToolpaths.length,18,'an existing hole/slot-only project');
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract'));
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.run'));await idle();
  assert.equal(await page.evaluate(async()=>{const module=await import(`${fixture.sourceRoot??'/src/'}apps/five-axis-cam/webpage/extractionActions.mjs`);return module.extractionDraft(fixture.view).extraction.selected.length;}),1,'re-extraction adds only the missing exterior trim');
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  let state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  for(const old of previous.intentToolpaths)assert.deepEqual(state.intentToolpaths.find(c=>c.id===old.id),old,'existing starts, process definitions and identities remain intact');
  writeFileSync(resolve(output,'disk-cuts.json'),JSON.stringify(state,null,2));
  assert.equal(state.intentToolpaths.length,expectedCuts,await page.evaluate(()=>fixture.view.error));
  assert.equal(state.intentToolpaths.filter(c=>c.boundaryRole==='OuterTrim').length,1);
  await verifyNumberDisplay(false);
  await page.screenshot({path:resolve(output,'disk-cuts.png')});
  await page.evaluate(async()=>{const THREE=await import(`${fixture.sourceRoot??'/src/'}iCAX-UI/SDK/ThirdParty/three/three.module.js`),v=fixture.view.viewport,m=[...v.sceneObjects.values()][0];
    const d=new THREE.Vector3(.65,1,-.35).normalize().transformDirection(m.matrixWorld);v.setCameraState({...fixture.diskPickCamera,theta:Math.atan2(d.y,d.x),phi:Math.acos(d.z)});});
  await page.screenshot({path:resolve(output,'disk-lower-rim.png')});
  const sortedIds=process.env.ICAX_ORDER_BEFORE_GENERATION?await verifyOrderingBeforeGeneration({page,rpc,calls,output}):null;
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.generate'));
  await idle();
  const result=await page.evaluate(()=>({error:fixture.view.error,generation:fixture.view.fiveGeneration?.task,
    rapid:fixture.view.fiveRapidBatch?.task&&{status:fixture.view.fiveRapidBatch.task.status,error:fixture.view.fiveRapidBatch.task.error,
      clearCount:fixture.view.fiveRapidBatch.task.result?.clearCount,count:fixture.view.fiveRapidBatch.task.result?.count}}));
  state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  writeFileSync(resolve(output,'disk-generation.json'),JSON.stringify({result,state,calls:calls.filter(c=>c.method?.startsWith('CamPlan')||c.method?.startsWith('CamRapid'))},null,2));
  await page.screenshot({path:resolve(output,'disk-generation.png')});
  assert.equal(result.error,'');assert.deepEqual(errors,[]);
  assert.equal(result.rapid.status,'Completed');assert.equal(Number(result.rapid.clearCount),expectedRapids);
  assert.equal(state.setups[0].generatedGeometry.operations.length,state.intentToolpaths.length);
  assert.equal(state.setups[0].generatedGeometry.transitions.length,expectedRapids);
  if(sortedIds){
    const g=state.setups[0].generatedGeometry;assert.deepEqual(g.operations.map(o=>o.intentId),sortedIds);
    for(let i=1;i<sortedIds.length;i++){assert.equal(g.transitions[i-1].from,sortedIds[i-1]);assert.equal(g.transitions[i-1].to,sortedIds[i]);}
  }
  assert.ok(state.setups[0].generatedGeometry.transitions.every(t=>t.validation==='Clear'));
  const trim=state.setups[0].generatedGeometry.operations.find(c=>c.boundaryRole==='OuterTrim');
  assert.equal(Number(trim.cadJunctions),0,'the requested lower rim has exact closed joins');assert.equal(trim.maximumCadJunctionMm,0);
  assert.ok(trim.parts.flatMap(p=>p.sourceSamples??[]).every(s=>s.kind!=='CadJunction'),'no synthetic connectors replace the lower rim');
  await verifyNumberDisplay(true);
  if(scale!==1)await rpc({action:'export-project',path:resolve(output,`disk-${scale}x-trim.i5cam`)});
  if(process.env.ICAX_DISK_SIMULATION_PROFILE){
    const enter=await page.evaluate(async()=>{const started=performance.now();await fixture.entry.handleRibbonCommand(fixture.context,'five.tool.simulation');
      await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));return {elapsedMs:performance.now()-started,frames:fixture.view.fiveSimulation.frames.length};});
    await page.evaluate(()=>{fixture.simulationStarted=performance.now();void fixture.entry.handleRibbonCommand(fixture.context,'five.simulation.play').then(()=>fixture.simulationFinished=performance.now());});
    let completed=true;try{await page.waitForFunction(()=>!!fixture.simulationFinished,null,{timeout:30000});}catch{completed=false;}
    const playback=await page.evaluate(()=>({elapsedMs:(fixture.simulationFinished??performance.now())-fixture.simulationStarted,
      checking:fixture.view.fiveSimulation.checking,progress:fixture.view.fiveSimulation.checkProgress,playing:fixture.view.fiveSimulation.playing,
      message:fixture.view.fiveSimulation.message,task:fixture.view.fiveSimulation.collisionTask?.status}));
    assert.equal(calls.filter(c=>c.method==='CamSimulation.CheckCollision').length,0,'Play never launches the full-path collision operation');
    await page.waitForFunction(()=>fixture.view.fiveSimulation.cursor>0||!!fixture.view.fiveSimulation.alarmCollision||(!fixture.view.fiveSimulation.playing&&fixture.view.fiveSimulation.message),null,{timeout:10000});
    const firstMotion=await page.evaluate(()=>({elapsedMs:performance.now()-fixture.simulationStarted,cursor:fixture.view.fiveSimulation.cursor,message:fixture.view.fiveSimulation.message}));
    assert.ok(firstMotion.cursor>0,firstMotion.message);
    if(!completed){await page.locator('[data-five-command="five.simulation.collision-cancel"]').click();await idle();}
    else await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.simulation.pause'));
    assert.ok(playback.elapsedMs<1000,'Play returns without a full-path precheck');
    await page.evaluate(()=>{fixture.simulationCanvas=fixture.view.viewport.renderer.domElement;});
    let domain=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    const originalSettings=domain.setups[0].rapidSettings;
    await rpc({action:'invoke',scope:'scene',method:'CamRapid.Configure',payload:{commandId:crypto.randomUUID(),expected:domain.expected,setupId:domain.activeSetupId,settings:{...originalSettings,collisionThresholdMm:5}}});
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));await idle();
    await page.locator('[data-five-command="five.simulation.play"]').click();
    await page.waitForFunction(()=>!fixture.view.fiveSimulation.playing&&fixture.view.fiveSimulation.alarmCollision?.status==='Collision',null,{timeout:10000});
    const alarm=await page.evaluate(()=>({cursor:fixture.view.fiveSimulation.cursor,collision:fixture.view.fiveSimulation.alarmCollision,message:fixture.view.fiveSimulation.message,
      sameCanvas:fixture.simulationCanvas===fixture.view.viewport.renderer.domElement}));
    assert.equal(alarm.sameCanvas,true);assert.match(alarm.message,/碰撞.*暂停/);
    assert.ok(Math.abs(alarm.cursor-Number(alarm.collision.index)-Number(alarm.collision.fraction))<1e-8);
    assert.equal(await page.locator('[data-five-playback-message][role="alert"]').isVisible(),true);
    await page.screenshot({path:resolve(output,'disk-runtime-contact-alarm.png')});
    domain=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    assert.equal(domain.setups[0].simulationCheck.validity,'Missing','live contact checks do not publish a full-path report');
    await rpc({action:'invoke',scope:'scene',method:'CamRapid.Configure',payload:{commandId:crypto.randomUUID(),expected:domain.expected,setupId:domain.activeSetupId,settings:originalSettings}});
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));await idle();
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.collision'));
    assert.equal(await page.getByRole('dialog',{name:'碰撞检测',exact:true}).isVisible(),true);
    assert.equal(await page.locator('[data-five-command="five.simulation.play"]').count(),0);
    await page.addStyleTag({content:'.cam-context-pane{max-height:160px;overflow:auto}.five-cut-tree{max-height:100px;overflow:auto}.cam-info-pane{height:170px!important;min-height:0!important;max-height:170px!important;overflow:auto!important}.cam-info-pane::after{content:"";display:block;height:240px}'});
    await page.evaluate(()=>{
      const input=document.querySelector('[data-five-simulation-field="threshold"]');input.focus({preventScroll:true});input.setSelectionRange(0,2);
      fixture.contactPreserved={input,canvas:fixture.view.viewport.renderer.domElement,panes:[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-cut-tree')]};
      fixture.holdMethod='CamScene.ReadState';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
    });await page.waitForFunction(()=>fixture.held);
    await page.evaluate(()=>{const p=fixture.contactPreserved;p.input.focus({preventScroll:true});p.input.setSelectionRange(1,2);p.panes.forEach((n,i)=>n.scrollTop=40+i*10);p.scroll=p.panes.map(n=>n.scrollTop);fixture.release();});await idle();
    const preserved=await page.evaluate(()=>{const p=fixture.contactPreserved;return {node:p.input===document.querySelector('[data-five-simulation-field="threshold"]'),focus:document.activeElement===p.input,
      selection:[p.input.selectionStart,p.input.selectionEnd],canvas:p.canvas===fixture.view.viewport.renderer.domElement,scroll:p.panes.map(n=>n.scrollTop),expectedScroll:p.scroll};});
    assert.equal(preserved.node,true);assert.equal(preserved.focus,true);assert.equal(preserved.canvas,true);assert.deepEqual(preserved.selection,[1,2]);assert.deepEqual(preserved.scroll,preserved.expectedScroll);
    assert.ok(preserved.scroll.every(n=>n>0),'both panes and the nested cut tree actually scroll');
    await page.evaluate(()=>{fixture.holdMethod='CamSimulation.CheckCollision';fixture.hold=true;});
    await page.locator('[data-five-command="five.simulation.collision-check"]').click();await page.waitForFunction(()=>fixture.held);
    await page.locator('[data-five-command="five.simulation.collision-cancel"]').click();
    await page.evaluate(()=>fixture.release());await idle();
    assert.equal(calls.filter(c=>c.method==='CamSimulation.CheckCollision').length,1);
    writeFileSync(resolve(output,'simulation-profile.json'),JSON.stringify({enter,playback,completed,firstMotion,alarm,preserved,independentFullPathCheck:true},null,2));
    await page.screenshot({path:resolve(output,'disk-simulation.png')});
  }
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,source:resolve(root,'TestData/FA/2.stp.STEP'),selectedFaces:pick.map(p=>p.faceId),
    scale,cuts:expectedCuts,lowerRim:{sourceEdges:lowerRim.boundary.sourceSpans.map(s=>s.edgeId),perimeterMm:lowerRim.boundary.perimeterMm},transitions:expectedRapids,clearTransitions:Number(result.rapid.clearCount),numberDisplay:{definition:true,generated:true,canvasPathsMarkersAndLabelsRetained:true,sceneUnchanged:true},originalFailure:'full lower rim from 34 top and outer-wall faces was Blocked as BOUNDARY_TERMINATION_UNRESOLVED',
    coverage:{native:'production scene SDO and geometry services',browser:'Edge, production model chooser, face pointer picks, confirm and ribbon command handler',standaloneCef:false}},null,2));
}
