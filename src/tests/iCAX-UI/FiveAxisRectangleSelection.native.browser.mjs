import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyRectangleSelection({page,rpc,output}) {
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const selected=()=>page.locator('[data-five-intent][aria-pressed="true"]').evaluateAll(nodes=>nodes.map(n=>n.dataset.fiveIntent).sort());
  const bounds=()=>page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const view=fixture.view,v=view.viewport,box=v.renderer.domElement.getBoundingClientRect();v.camera.updateMatrixWorld(true);
    return view.scene.intentToolpaths.map(c=>{
      const placement=view.scene.domain.placements.find(p=>p.id===c.placementId),matrix=new THREE.Matrix4().set(...placement.transform);
      const points=c.previewPoints.map(p=>new THREE.Vector3(...p).applyMatrix4(matrix).project(v.camera));
      const xs=points.map(p=>box.left+(p.x+1)*box.width/2),ys=points.map(p=>box.top+(1-p.y)*box.height/2);
      return {id:c.id,kind:c.machiningKind,left:Math.min(...xs),right:Math.max(...xs),top:Math.min(...ys),bottom:Math.max(...ys)};
    });
  });
  const frame=b=>({left:b.left-5,right:b.right+5,top:b.top-5,bottom:b.bottom+5});
  const enclosed=(all,b)=>all.filter(p=>p.left>=b.left&&p.right<=b.right&&p.top>=b.top&&p.bottom<=b.bottom).map(p=>p.id).sort();
  const drag=async(box,{crossing=false,key=null,finish=true}={})=>{
    if(key)await page.keyboard.down(key);
    await page.mouse.move(crossing?box.right:box.left,box.top);await page.mouse.down();
    await page.mouse.move(crossing?box.left:box.right,box.bottom,{steps:8});
    if(finish){await page.mouse.up();await idle();}
    if(key)await page.keyboard.up(key);
  };
  await page.locator('[data-five-command="five.reverse.close"]').first().click();
  await page.evaluate(()=>fixture.view.viewport.fitView());
  const initial=await read();let all=await bounds();
  const holes=all.filter(p=>p.kind==='Cut').sort((a,b)=>(a.right-a.left)*(a.bottom-a.top)-(b.right-b.left)*(b.bottom-b.top));
  const first=holes[0],second=holes.find(p=>p.id!==first.id&&(p.right<first.left||p.left>first.right||p.bottom<first.top||p.top>first.bottom));
  assert.ok(first&&second);
  const originalCamera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  await page.evaluate(()=>{fixture.rectangleCanvas=fixture.view.viewport.renderer.domElement;fixture.rectangleRows=[...document.querySelectorAll('[data-five-intent]')];});
  const firstBox=frame(first);await drag(firstBox,{finish:false});
  assert.equal(await page.locator('.icax-three-selection-rectangle[data-mode="contain"]').count(),1);
  await page.screenshot({path:resolve(output,'rectangle-selection-drag.png')});
  await page.mouse.up();await idle();
  assert.deepEqual(await selected(),enclosed(all,firstBox));
  assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),originalCamera,'left drag does not move camera');
  await drag(frame(second),{key:'Control'});
  const combined=[...new Set([...enclosed(all,firstBox),...enclosed(all,frame(second))])].sort();
  assert.deepEqual(await selected(),combined);
  const highlighted=await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.filter(c=>c.userData.cuttingLineId&&c.material.color.getHex()===0xff922b).map(c=>c.userData.cuttingLineId).sort());
  assert.deepEqual(highlighted,combined,'viewport and tree agree');
  await page.locator('.cam-context-pane').evaluate(n=>n.scrollTop=0);
  await page.screenshot({path:resolve(output,'rectangle-selection-multiple.png')});
  assert.equal(await page.evaluate(()=>fixture.rectangleCanvas===fixture.view.viewport.renderer.domElement&&fixture.rectangleRows.every(n=>n.isConnected)),true);

  // A check mark is part of the row, not an implicit Ctrl modifier. Exercise
  // real clicks after a multi-selection, including an already selected path.
  const row=id=>page.locator(`[data-five-intent="${id}"]`);
  await row(first.id).locator('.five-cut-check').click();
  assert.deepEqual(await selected(),[first.id],'plain check-mark click replaces the selection');
  await row(first.id).locator('.five-cut-check').click();
  assert.deepEqual(await selected(),[first.id],'plain repeat click keeps the path selected');
  await row(second.id).locator('.five-cut-check').click({modifiers:['Control']});
  assert.deepEqual(await selected(),[first.id,second.id].sort(),'Ctrl click adds a path');
  await row(first.id).locator('.five-cut-check').click({modifiers:['Control']});
  assert.deepEqual(await selected(),[second.id],'Ctrl click toggles a path off');
  await row(first.id).click();
  assert.deepEqual(await selected(),[first.id],'plain row click selects only that path');
  const treeIds=await page.locator('[data-five-intent]').evaluateAll(nodes=>nodes.map(n=>n.dataset.fiveIntent));
  await row(treeIds[0]).click();await row(treeIds[2]).locator('.five-cut-check').click({modifiers:['Shift']});
  assert.deepEqual(await selected(),treeIds.slice(0,3).sort(),'Shift click selects the row range');
  await row(treeIds[1]).locator('.five-cut-check').click();
  assert.deepEqual(await selected(),[treeIds[1]],'plain click collapses a Shift range');

  await drag(firstBox);await drag(frame(second),{key:'Control'});
  const point=await page.evaluate(async id=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const view=fixture.view,v=view.viewport,box=v.renderer.domElement.getBoundingClientRect();
    const cut=view.scene.intentToolpaths.find(c=>c.id===id),placement=view.scene.domain.placements.find(p=>p.id===cut.placementId);
    const p=new THREE.Vector3(...cut.previewPoints[Math.floor(cut.previewPoints.length/3)]).applyMatrix4(new THREE.Matrix4().set(...placement.transform)).project(v.camera);
    return {x:box.left+(p.x+1)*box.width/2,y:box.top+(1-p.y)*box.height/2};
  },first.id);
  await page.mouse.click(point.x,point.y);await idle();
  assert.deepEqual(await selected(),[first.id],'plain viewport click collapses rectangle selection');
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.filter(c=>c.userData.cuttingLineId&&c.material.color.getHex()===0xff922b).map(c=>c.userData.cuttingLineId)),[first.id],'single click immediately highlights only that path');
  assert.equal(await page.evaluate(()=>fixture.rectangleCanvas===fixture.view.viewport.renderer.domElement&&fixture.rectangleRows.every(n=>n.isConnected)),true);

  const partial={left:first.right-4,right:first.right+4,top:first.top-5,bottom:first.bottom+5};
  await drag(partial);assert.ok(!(await selected()).includes(first.id),'window requires the entire path');
  await drag(partial,{crossing:true,finish:false});
  assert.equal(await page.locator('.icax-three-selection-rectangle[data-mode="crossing"]').count(),1);
  await page.mouse.up();assert.ok((await selected()).includes(first.id),'crossing selects the intersected path');
  await drag(frame(second),{key:'Shift'});assert.ok((await selected()).includes(first.id)&&(await selected()).includes(second.id));
  const beforeCancel=await selected();
  await drag(firstBox,{finish:false});await page.keyboard.press('Escape');await page.mouse.up();
  assert.equal(await page.locator('.icax-three-selection-rectangle').count(),0);assert.deepEqual(await selected(),beforeCancel);
  await drag(firstBox,{finish:false});
  await page.evaluate(()=>{const v=fixture.view.viewport;v.renderer.domElement.releasePointerCapture(v.navigation.pointerId);});
  await page.mouse.up();assert.equal(await page.locator('.icax-three-selection-rectangle').count(),0);assert.deepEqual(await selected(),beforeCancel);

  await drag(firstBox);await drag(frame(second),{key:'Control'});
  const batch=await selected();assert.ok(batch.length>=2);
  const action=async name=>{if(name==='Delete')await command('five.tree.Delete');else if(['Enable','Disable'].includes(name)){for(const id of await selected()){await page.locator(`[data-five-command="five.tree.ToggleEnabled:${id}"]`).click();await idle();}}else await page.locator(`[data-five-command="five.tree.${name}"]`).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const treeOrder=()=>page.locator('[data-five-intent]').evaluateAll(nodes=>nodes.map(n=>n.dataset.fiveIntent));
  const originalOrder=await treeOrder();await action('Up');assert.notDeepEqual(await treeOrder(),originalOrder);
  assert.deepEqual(await selected(),batch);
  // A selected path already at the boundary cannot move up; moving the batch
  // down afterwards is not its inverse. Restore each move with native undo.
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.deepEqual(await treeOrder(),originalOrder);
  await action('Down');assert.notDeepEqual(await treeOrder(),originalOrder);assert.deepEqual(await selected(),batch);
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.deepEqual(await treeOrder(),originalOrder);
  await action('Disable');let state=await read();assert.deepEqual(state.intentToolpaths.filter(c=>c.enabled===false).map(c=>c.id).sort(),batch);
  await action('Enable');assert.ok((await read()).intentToolpaths.every(c=>c.enabled));
  await action('Delete');assert.equal((await read()).intentToolpaths.length,initial.intentToolpaths.length-batch.length);
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.equal((await read()).intentToolpaths.length,initial.intentToolpaths.length);

  const canvas=await page.locator('.cam-render-viewport .icax-three-viewport > canvas').first().boundingBox();
  const empty={left:canvas.x+30,right:canvas.x+80,top:canvas.y+120,bottom:canvas.y+165};
  await page.evaluate(()=>{
    const canvas=fixture.view.viewport.renderer.domElement;
    fixture.blankClickSamples=[];
    canvas.addEventListener('pointerup',()=>{fixture.pointerUpTime=performance.now();},true);
    canvas.addEventListener('pointerup',()=>{
      if(!fixture.measureBlankClick)return;
      fixture.blankClickSamples.push({elapsedMs:performance.now()-fixture.pointerUpTime,
        empty:fixture.view.fiveAxisUI.selectedIntentIds.length===0,
        pendingTimer:fixture.view.viewport.pendingBlankPick,
        checkedRows:document.querySelectorAll('[data-five-intent][aria-pressed="true"]').length});
    });
  });
  const clearByBlankClick=async()=>{
    await page.evaluate(()=>{fixture.measureBlankClick=true;});
    await page.mouse.click(empty.left,empty.top);
    const sample=await page.evaluate(()=>{fixture.measureBlankClick=false;return fixture.blankClickSamples.at(-1);});
    assert.equal(sample.empty,true,'selection clears within the same pointerup event');
    assert.equal(sample.checkedRows,0,'tree check marks clear synchronously');
    assert.equal(sample.pendingTimer,null,'no double-click delay timer is scheduled');
    assert.deepEqual(await page.evaluate(()=>{const ui=fixture.view.fiveAxisUI;return [ui.selectedIntentIds,ui.selectedIntentId,ui.cutSelectionAnchor,fixture.view.selectedSceneObjectId];}),[[],'','','']);
    assert.equal(await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.some(c=>c.userData.cutDirectionId||(c.userData.cuttingLineId&&c.material.color.getHex()===0xff922b))),false,'blank click clears direction markers and cut highlights');
    for(const action of ['Up','Down','Disable','Enable','Delete'])assert.equal(await page.locator(`[data-five-command="five.tree.${action}"]`).isDisabled(),true);
  };
  const beforeBlankClicks=await read();
  await row(first.id).click();await clearByBlankClick();
  await drag(firstBox);await drag(frame(second),{key:'Control'});assert.ok((await selected()).length>=2);
  await clearByBlankClick();
  await drag(firstBox);const retained=await selected();
  for(const key of ['Control','Shift']){
    await page.keyboard.down(key);await page.mouse.click(empty.left,empty.top);await page.keyboard.up(key);
    await page.waitForTimeout(600);assert.deepEqual(await selected(),retained,`${key} blank click retains selection`);
  }
  // No delayed blank click may erase a subsequent list selection.
  await page.mouse.click(empty.left,empty.top);await row(second.id).click();await page.waitForTimeout(600);
  assert.deepEqual(await selected(),[second.id]);
  assert.deepEqual(await read(),beforeBlankClicks,'selection changes do not mutate the project');
  assert.equal(await page.evaluate(()=>fixture.rectangleCanvas===fixture.view.viewport.renderer.domElement),true);
  await drag(firstBox);const chosen=await selected();
  await drag(empty,{key:'Control'});assert.deepEqual(await selected(),chosen);
  await drag(empty);assert.deepEqual(await selected(),[]);
  await drag(firstBox,{finish:false});await command('five.tool.path-start');await page.mouse.up();
  assert.equal(await page.locator('.icax-three-selection-rectangle').count(),0);assert.deepEqual(await selected(),[],'tool change cancels the drag');
  await drag(firstBox,{finish:false});assert.equal(await page.locator('.icax-three-selection-rectangle').count(),0,'start-point picking owns left drag');await page.mouse.up();
  await command('five.tool.extract');await page.locator('[data-five-command="five.extraction.pick"]').click();await idle();
  await drag(firstBox,{finish:false});assert.equal(await page.locator('.icax-three-selection-rectangle').count(),0,'face picking owns left drag');await page.mouse.up();
  await page.locator('[data-five-command="five.extraction.close"]').first().click();

  for(const ctrl of [false,true]){
    const before=await page.evaluate(()=>fixture.view.viewport.getCameraState());
    if(ctrl)await page.keyboard.down('Control');
    await page.mouse.move(empty.left,empty.top);await page.mouse.down({button:'middle'});await page.mouse.move(empty.right,empty.bottom,{steps:5});await page.mouse.up({button:'middle'});
    if(ctrl)await page.keyboard.up('Control');
    assert.notDeepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),before,`${ctrl?'Ctrl + ':''}middle camera gesture retained`);
    assert.equal(await page.locator('.icax-three-selection-rectangle').count(),0);
  }
  await page.evaluate(()=>fixture.view.viewport.fitViewToViewport(1.35));
  await row(first.id).click();
  const fitRadius=await page.evaluate(()=>fixture.view.viewport.getCameraState().radius);
  await page.mouse.move(empty.left,empty.top);await page.mouse.wheel(0,350);
  await page.mouse.dblclick(empty.left,empty.top);await page.waitForTimeout(600);
  assert.ok(Math.abs(await page.evaluate(()=>fixture.view.viewport.getCameraState().radius)-fitRadius)<1e-6,'blank double-click still fits');
  assert.deepEqual(await selected(),[],'the first blank click clears immediately; the second still fits');
  await page.evaluate(()=>{const v=fixture.view.viewport;v.setCameraState({...v.getCameraState(),projectionMode:'perspective'});v.fitView();});
  all=await bounds();await drag(frame(all.find(c=>c.id===first.id)));assert.ok((await selected()).includes(first.id),'perspective rectangle selection');
  await command('five.tool.generate');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await page.locator('[data-five-command="five.generate.close"]').first().click();
  await drag(frame(all.find(c=>c.id===first.id)));await drag(frame(all.find(c=>c.id===second.id)),{key:'Control'});
  const generatedSelection=await selected();assert.ok(generatedSelection.includes(first.id)&&generatedSelection.includes(second.id));
  const generatedHighlights=await page.evaluate(()=>[...new Set(fixture.view.fiveGeneratedPreview.group.children.filter(c=>c.userData.generatedIntentId&&c.material.color.getHex()===0x3583ff).map(c=>c.userData.generatedIntentId))].sort());
  assert.deepEqual(generatedHighlights,generatedSelection,'generated path multi-selection excludes rapid links');
  await clearByBlankClick();
  assert.equal(await page.evaluate(()=>fixture.view.fiveGeneratedPreview.group.children.some(c=>c.userData.generatedIntentId&&c.material.color.getHex()===0x3583ff)),false,'blank click clears generated path highlights too');
  writeFileSync(resolve(output,'selection-response-report.json'),JSON.stringify({passed:true,immediateBlankPick:true,
    samples:await page.evaluate(()=>fixture.blankClickSamples),
    cases:['single and rectangle selection clear in the same pointerup event','tree and viewport clear together','Ctrl and Shift blank clicks retain selection','no delayed click clears a subsequent selection','double-click still fits; first click clears selection','generated path highlights clear'],native:true,browser:'Edge',standaloneCef:false},null,2));
  writeFileSync(resolve(output,'rectangle-selection-report.json'),JSON.stringify({passed:true,cases:['real pointer drag with window and crossing frames','Ctrl and Shift add paths','plain row check-mark and viewport clicks replace multi-selection; repeat plain click retains selection','Ctrl toggles and Shift selects a range only with actual keyboard modifiers','tree and viewport highlights agree; canvas and rows preserved','batch move disable enable delete and undo through native SDO','empty frame clears; additive empty frame preserves','Escape lost capture and tool changes cancel','face and start picking remain exclusive','middle orbit Ctrl + middle pan and blank double-click fit retained','perspective selection','generated machining paths support multi-selection and highlighting'],native:true,browser:'Edge',standaloneCef:false},null,2));
}
