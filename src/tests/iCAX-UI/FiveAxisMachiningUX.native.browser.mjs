import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyMachiningUX({page,rpc,root,output,errors,calls,faceBoxOnly=false}) {
  page.setDefaultTimeout(120000);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const area=id=>page.evaluate(id=>fixture.context.actions.selectRibbonTab(id),id);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const faces=()=>page.evaluate(()=>extraction.extractionDraft(fixture.view).extraction.faces.map(f=>f.localId).sort());
  await page.evaluate(async()=>{window.extraction=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');});
  await selectWorkpieceFile(page,resolve(root,'TestData/real-sheet-metal/pololu-stamped-bracket/Pololu-2257-stamped-bracket.step'));await idle();
  await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1&&document.querySelector('[data-five-thumbnail] img')?.naturalWidth===192);
  const first=(await read()).placements[0];
  const firstImage=await page.locator('[data-five-thumbnail] img').first().getAttribute('src');
  await area('toolpath');await command('five.tool.extract');
  await page.evaluate(()=>{fixture.view.viewport.fitView(1.6);fixture.uxCanvas=fixture.view.viewport.renderer.domElement;});
  const mapping=await page.evaluate(async()=>{
    const d=fixture.view.scene.domain,p=d.placements.find(p=>p.id===fixture.view.scene.activePlacementId),request={expected:d.expected,setupId:p.setupId,placementId:p.id};
    return {sources:await fixture.context.sceneProxy.invoke('CamCut.QuerySources',request),map:await fixture.context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',request)};
  });
  const box=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view.viewport,m=v.sceneObjects.get(fixture.view.scene.activePlacementId),r=v.renderer.domElement.getBoundingClientRect(),p=m.geometry.getAttribute('position');
    m.updateMatrixWorld(true);v.camera.updateMatrixWorld(true);const xs=[],ys=[];
    for(let i=0;i<p.count;i++){const q=new THREE.Vector3().fromBufferAttribute(p,i).applyMatrix4(m.matrixWorld).project(v.camera);xs.push(r.left+(q.x+1)*r.width/2);ys.push(r.top+(1-q.y)*r.height/2);}
    return {left:Math.min(...xs)-4,right:Math.max(...xs)+4,top:Math.min(...ys)-4,bottom:Math.max(...ys)+4};
  });
  const drag=async(b,{crossing=false,key,finish=true}={})=>{
    if(key)await page.keyboard.down(key);await page.mouse.move(crossing?b.right:b.left,b.top);await page.mouse.down();await page.mouse.move(crossing?b.left:b.right,b.bottom,{steps:8});
    if(finish){await page.mouse.up();await idle();}if(key)await page.keyboard.up(key);
  };
  const before=await read(),camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  await drag(box,{finish:false});assert.equal(await page.locator('.icax-three-selection-rectangle[data-mode="contain"]').count(),1);await page.mouse.up();await idle();
  assert.deepEqual(await faces(),mapping.sources.faceUses.filter(f=>mapping.map.faces.some(r=>String(r.faceId)===String(f.localId))).map(f=>f.localId).sort());
  assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);
  assert.ok(await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.some(c=>c.userData.selectedMachiningFaces)));
  await page.screenshot({path:resolve(output,'face-box-selected.png')});
  const partial={...box,left:(box.left+box.right)/2};
  await drag(partial);const contained=await faces();await drag(partial,{crossing:true});const crossed=await faces();
  assert.ok(crossed.length>contained.length,'crossing includes partially intersected faces');
  await drag(box,{key:'Control'});assert.equal((await faces()).length,mapping.sources.faceUses.length);
  await drag(box,{key:'Alt'});assert.deepEqual(await faces(),[]);
  await drag(box,{finish:false});await page.keyboard.press('Escape');await page.mouse.up();await idle();assert.deepEqual(await faces(),[]);
  await page.evaluate(()=>{fixture.holdMethod='CamCut.QuerySources';fixture.hold=true;});
  await drag(partial,{finish:false});await page.mouse.up();await page.waitForFunction(()=>fixture.held);
  const center={x:(box.left+box.right)/2,y:(box.top+box.bottom)/2};
  await page.mouse.move(center.x,center.y);await page.mouse.down({button:'middle'});await page.mouse.move(center.x+80,center.y+30,{steps:5});await page.mouse.up({button:'middle'});
  assert.notDeepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);
  await page.evaluate(()=>fixture.release());await idle();assert.deepEqual(await faces(),contained,'async topology lookup uses the camera at gesture time');
  assert.deepEqual(await read(),before,'selection does not persist CAD or machining changes');
  await command('five.extraction.close');
  if(faceBoxOnly){assert.deepEqual(errors,[]);writeFileSync(resolve(output,'face-box-report.json'),JSON.stringify({passed:true,faces:mapping.sources.faceUses.length,contain:contained.length,crossing:crossed.length,asyncCameraPreserved:true},null,2));return;}

  await area('workpiece');await selectWorkpieceFile(page,resolve(root,'TestData/automotive-stamping/mercedes-benz-t-node/Mercedes-Benz_T-Node.stp'));await idle();
  await page.waitForFunction(()=>[...document.querySelectorAll('[data-five-thumbnail] img')].every(i=>i.naturalWidth===192));
  const state=await read(),second=state.placements.find(p=>p.id!==first.id);
  const secondImage=await page.locator(`[data-five-workpiece="${second.id}"] img`).getAttribute('src');
  assert.notEqual(firstImage,secondImage,'different real models have distinct thumbnails');
  await area('toolpath');
  const chooser=page.locator('[data-five-workpiece-chooser]'),summary=chooser.locator('summary');
  await summary.click();assert.equal(await chooser.locator('[data-five-workpiece]').count(),2);
  await page.screenshot({path:resolve(output,'workpiece-thumbnail-chooser.png')});
  await summary.focus();await page.keyboard.press('Home');await page.keyboard.press('Enter');await idle();
  assert.equal(await page.evaluate(()=>fixture.view.scene.activePlacementId),first.id);
  assert.equal(await summary.locator('img').getAttribute('src'),firstImage);
  await summary.press('End');await page.keyboard.press('Enter');await idle();
  assert.equal(await page.evaluate(()=>fixture.view.scene.activePlacementId),second.id);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.sceneObjects.size),1);
  assert.equal(await summary.locator('img').getAttribute('src'),secondImage);
  await summary.click();await page.keyboard.press('Escape');assert.equal(await chooser.getAttribute('open'),null);
  const requests=calls.filter(c=>c.action==='resource').length;await command('five.refresh');await idle();
  assert.equal(await summary.locator('img').getAttribute('src'),secondImage);
  // Request counts may include the main View refresh; thumbnail reads themselves stay cached.
  assert.equal(await page.evaluate(()=>fixture.view.fiveThumbnails.cache.size),2);

  await command('five.tool.extract');await command('five.extraction.automatic');await idle();
  const rows=page.locator('[data-five-candidate]');assert.equal(await rows.count(),5);
  assert.equal(await rows.filter({has:page.locator('small',{hasText:'切割'})}).count(),5);
  assert.equal((await read()).intentToolpaths.length,0);
  await rows.first().click();assert.equal(await rows.first().getAttribute('aria-checked'),'false');
  assert.equal(await page.evaluate(()=>extraction.extractionDraft(fixture.view).extraction.selected.length),4);
  await rows.first().click();await page.screenshot({path:resolve(output,'pending-five-contours.png')});
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  assert.equal(await rows.count(),0);assert.equal(await page.locator('[data-five-intent]').count(),5);assert.equal((await read()).intentToolpaths.length,5);
  const ribbon=await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='toolpath').groups);
  assert.equal(ribbon.some(g=>g.title==='空移'),false);
  assert.ok(!ribbon.flatMap(g=>g.commands).some(c=>c.title==='生成刀路'));
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.Generate';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.tool.generate');});
  await page.waitForFunction(()=>fixture.held);
  const progress=page.getByRole('progressbar',{name:'生成空移进度'});
  assert.equal(await progress.isVisible(),true);assert.equal(await progress.getAttribute('aria-valuenow'),null);
  assert.match(await progress.getAttribute('aria-valuetext'),/准备切割路径/);
  await page.evaluate(()=>{fixture.holdMethod='CamRapid.Generate';fixture.hold=true;fixture.release();});
  await page.waitForFunction(()=>fixture.held&&fixture.view.fiveRapidBatch?.running);
  assert.match(await progress.getAttribute('aria-valuetext'),/规划空移/);
  await page.screenshot({path:resolve(output,'rapid-generation-progress.png')});
  await page.evaluate(()=>fixture.release());
  await page.waitForFunction(()=>fixture.view.fiveRapidBatch?.task?.status==='Running'||!fixture.view.pending);
  if(await page.evaluate(()=>fixture.view.pending))await page.locator('[data-five-command="five.generate.cancel"]').click();await idle();
  assert.equal(await progress.count(),0);
  const preparedCalls=calls.filter(c=>c.method==='CamPlan.Generate').length;
  await page.evaluate(()=>{fixture.holdMethod='CamRapid.Generate';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.tool.generate');});
  await page.waitForFunction(()=>fixture.held&&fixture.view.fiveRapidBatch?.running);
  await page.locator('[data-five-command="five.generate.cancel"]').click();
  assert.match(await progress.getAttribute('aria-valuetext'),/取消/);
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(calls.filter(c=>c.method==='CamPlan.Generate').length,preparedCalls,'current cuts are reused when generating only rapid motion');
  assert.equal(await progress.count(),0);assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await command('five.tool.settings');assert.equal(await page.getByRole('dialog',{name:'空移策略'}).isVisible(),true);
  // Refresh + deferred thumbnail painting must preserve current nodes, focus,
  // caret and the user's newer scroll positions in both panes.
  await page.evaluate(()=>{
    const left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
    left.style.maxHeight='220px';right.style.maxHeight='240px';document.querySelector('[data-five-workpiece-chooser]').open=true;
    const input=document.querySelector('[data-five-rapid-field="marginMm"]');input.focus({preventScroll:true});input.setSelectionRange(0,1);
    fixture.preserved={left,right,input,canvas:fixture.view.viewport.renderer.domElement};
    const get=fixture.context.sceneProxy.resources.get;
    const ref=fixture.view.scene.domain.workpieces.find(w=>w.id===fixture.view.scene.model.id).previewRef;
    fixture.context.sceneProxy.resources.get=async(url,options)=>{
      if(url===ref.url&&!fixture.thumbHeld){fixture.thumbHeld=true;await new Promise(resolve=>fixture.releaseThumb=resolve);}
      return get(url,options);
    };
    fixture.view.fiveThumbnails.cache.clear();fixture.holdMethod='CamScene.ReadState';fixture.hold=true;
    void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
  });
  await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>{const p=fixture.preserved;p.left.scrollTop=74;p.right.scrollTop=63;fixture.release();});await idle();
  await page.waitForFunction(()=>fixture.thumbHeld);
  await page.evaluate(()=>{const p=fixture.preserved;p.left.scrollTop=93;p.right.scrollTop=85;p.scroll=[p.left.scrollTop,p.right.scrollTop];fixture.releaseThumb();});
  await page.waitForFunction(()=>!fixture.view.fiveThumbnails.running);
  const preserved=await page.evaluate(()=>{
    const p=fixture.preserved;return {node:p.input===document.querySelector('[data-five-rapid-field="marginMm"]'),focus:document.activeElement===p.input,
      selection:[p.input.selectionStart,p.input.selectionEnd],canvas:p.canvas===fixture.view.viewport.renderer.domElement,
      scroll:[p.left.scrollTop,p.right.scrollTop],expectedScroll:p.scroll};
  });
  writeFileSync(resolve(output,'interaction.json'),JSON.stringify(preserved,null,2));
  assert.equal(preserved.node,true);assert.equal(preserved.focus,true);assert.equal(preserved.canvas,true);
  assert.deepEqual(preserved.selection,[0,1]);assert.deepEqual(preserved.scroll,preserved.expectedScroll);assert.ok(preserved.scroll.every(n=>n>0));
  await page.evaluate(()=>{const p=fixture.preserved;p.left.style.maxHeight='';p.right.style.maxHeight='';document.querySelector('[data-five-workpiece-chooser]').open=false;});
  await page.screenshot({path:resolve(output,'rapid-strategy.png')});await command('five.rapid.close');
  await command('five.tool.rapid');assert.equal(await page.getByRole('dialog',{name:'空移路径编辑'}).isVisible(),true);
  await page.locator('[data-five-rapid-field="strategy"]').selectOption('Frog');
  await page.evaluate(()=>{fixture.marginNode=document.querySelector('[data-five-rapid-field="marginMm"]');fixture.marginNode.focus({preventScroll:true});fixture.marginNode.setSelectionRange(0,1);});
  await page.locator('[data-five-rapid-field="strategy"]').selectOption('Fly');
  assert.equal(await page.locator('[data-five-rapid-field="liftHeightMm"]').count(),0);
  assert.equal(await page.evaluate(()=>fixture.marginNode===document.querySelector('[data-five-rapid-field="marginMm"]')&&document.activeElement===fixture.marginNode&&fixture.marginNode.selectionEnd===1),true);
  await command('five.rapid.close');
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));await rpc({action:'save-reopen'});
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});await idle();
  await page.waitForFunction(()=>document.querySelector('[data-five-workpiece-chooser] summary img')?.naturalWidth===192);
  assert.equal(await page.locator('[data-five-workpiece-chooser] summary img').getAttribute('src'),secondImage);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.sceneObjects.size),1);assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'machining-ux-report.json'),JSON.stringify({passed:true,faces:mapping.sources.faceUses.length,contain:contained.length,crossing:crossed.length,candidates:5,workpieces:2,requestsBeforeRefresh:requests,cases:['native mesh thumbnails and keyboard workpiece switching','source-preserving face contain/crossing/add/remove/Escape and persistent highlight','real Mercedes five candidate rows, toggling and native confirmation','separate rapid ribbon group and strategy/editor','indeterminate progress for cut preparation and rapid planning; cancel clears progress','save/reopen restores embedded thumbnails'],coverage:{native:'production SDO and resources',browser:'Edge',standaloneCef:false}},null,2));
}
