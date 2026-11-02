import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyCutStarting({page,rpc,calls,output,intentId,automotive=false,focusContour=false}) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const before=await read(),cut=before.intentToolpaths.find(c=>intentId?c.id===intentId:c.previewPoints.length>200);
  assert.ok(cut);intentId=cut.id;
  const fit=async()=>{
    await page.evaluate(() => fixture.view.viewport.fitView());
    if(focusContour)await page.evaluate(id=>{
      const c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport;
      const lo=[0,1,2].map(i=>Math.min(...c.previewPoints.map(p=>p[i]))),hi=[0,1,2].map(i=>Math.max(...c.previewPoints.map(p=>p[i])));
      const p=v.camera.position.clone().set(...lo.map((x,i)=>(x+hi[i])/2));v.sceneObjects.get(c.placementId).localToWorld(p);
      v.setCameraState({...v.getCameraState(),target:{x:p.x,y:p.y,z:p.z},radius:Math.max(5,Math.hypot(...lo.map((x,i)=>hi[i]-x))*2)});
    },intentId);
    await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  };
  await command('five.tool.path-start');await idle();
  assert.equal(await page.locator('.cam-info-pane').isVisible(),true);
  assert.equal(await page.locator('[data-five-field="startNearX"],[data-five-field="startMode"],[data-five-command="five.cut.apply"]').count(),0);
  assert.equal(await page.locator('.five-dock').isVisible(),false);
  await fit();
  const pick=await page.evaluate(id=>{
    const c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport,box=v.renderer.domElement.getBoundingClientRect(),object=v.sceneObjects.get(c.placementId);
    const screen=p=>{const q=object.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);return {x:box.x+(q.x+1)*box.width/2,y:box.y+(1-q.y)*box.height/2};};
    const start=screen(c.startPoint);
    for(let i=Math.floor(c.previewPoints.length/4);i<c.previewPoints.length-2;i++){
      const p=c.previewPoints[i].map((n,j)=>(n+c.previewPoints[i+1][j])/2),q=screen(p);
      if(q.x>box.x+35&&q.x<box.right-35&&q.y>box.y+75&&q.y<box.bottom-100&&Math.hypot(q.x-start.x,q.y-start.y)>30)return q;
    }
    throw Error('No visible new contour start');
  },intentId);
  const click=()=>page.mouse.click(pick.x,pick.y);

  // Escape during the exact lookup must not submit a late result.
  await page.evaluate(()=>{fixture.holdMethod='CamCut.ResolveStart';fixture.hold=true;});
  await click();await page.waitForFunction(()=>fixture.held);
  await page.keyboard.press('Escape');
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.locator('.five-start-prompt').count(),0);
  assert.deepEqual(await read(),before);
  await command('five.tool.path-start');
  await page.evaluate(()=>{fixture.holdMethod='CamCut.ResolveStart';fixture.hold=true;});
  await click();await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
  await page.locator('[data-five-search]').fill('正在查看');
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>document.activeElement?.hasAttribute('data-five-search')),true);
  assert.deepEqual(await read(),before);
  await page.locator('[data-five-search]').fill('');await command('five.tool.path-start');
  await fit();

  const setCount=calls.filter(c=>c.method==='CamProcess.Update').length;
  await page.evaluate(()=>{fixture.loseMethod='CamProcess.Update';fixture.startCanvas=document.querySelector('canvas');fixture.startTree=document.querySelector('.five-cut-tree');});
  if(!automotive)await page.evaluate(()=>{
    const proxy=fixture.context.sceneProxy,invoke=proxy.invoke.bind(proxy);let lost=false;
    proxy.invoke=async(method,payload)=>{
      if(method==='CamScene.QueryCommand'&&!lost){lost=true;throw Error('Injected receipt transport failure');}
      return invoke(method,payload);
    };
  });
  await click();await idle();
  let uncertain;
  if(!automotive){
    uncertain=await read();
    assert.equal(await page.locator('[data-five-command="five.start.retry"]').count(),1);
    await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
    await command('five.tool.path-start');await idle();
    await page.evaluate(()=>{fixture.startCanvas=document.querySelector('canvas');fixture.startTree=document.querySelector('.five-cut-tree');});
    await page.locator('[data-five-command="five.start.retry"]').click();await idle();
  }
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const after=await read(),changed=after.intentToolpaths.find(c=>c.id===intentId);
  if(uncertain)assert.deepEqual(after,uncertain,'retry reuses the completed command, without another mutation');
  assert.notDeepEqual(changed.start,cut.start);
  assert.notDeepEqual(changed.startPoint,cut.startPoint);
  for(const k of ['curveRef','sourceKey','previewPoints','processRef','poseRef','number','reversed','retainedSide'])assert.deepEqual(changed[k],cut[k],k);
  assert.deepEqual(after.intentToolpaths.filter(c=>c.id!==intentId),before.intentToolpaths.filter(c=>c.id!==intentId));
  assert.equal(calls.filter(c=>c.method==='CamProcess.Update').length,setCount+(automotive?1:2),'lost response uses receipt or the same command');
  const markers=await page.evaluate(id=>{
    const cut=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport,object=v.sceneObjects.get(cut.placementId);
    const p=object.localToWorld(v.camera.position.clone().set(...cut.startPoint));
    const labels=v.specificationLabels.filter(l=>l.element.textContent===cut.number||l.element.textContent.startsWith(`${cut.number}-`));
    const points=fixture.view.fiveCutPreview.group.children.find(c=>c.userData.startMarkers).geometry.getAttribute('position');
    let gap=Infinity;for(let i=0;i<points.count;i++)gap=Math.min(gap,Math.hypot(points.getX(i)-cut.startPoint[0],points.getY(i)-cut.startPoint[1],points.getZ(i)-cut.startPoint[2]));
    return {canvas:fixture.startCanvas===document.querySelector('canvas'),tree:fixture.startTree===document.querySelector('.five-cut-tree'),gap,labelGap:labels[0].anchorPoint.distanceTo(p),offset:labels[0].screenOffset,leader:!!v.specificationLeaderSvg.querySelector('line')};
  },intentId);
  assert.equal(markers.canvas,true);assert.equal(markers.tree,true);assert.ok(markers.gap<1e-5);assert.ok(markers.labelGap<1e-10);assert.deepEqual(markers.offset,[42,-12]);assert.equal(markers.leader,true);
  assert.deepEqual((await rpc({action:'undo'})).state.intentToolpaths,before.intentToolpaths);
  assert.deepEqual((await rpc({action:'redo'})).state.intentToolpaths,after.intentToolpaths);
  await command('five.refresh');
  if(automotive&&process.env.ICAX_AUTOMOTIVE_START_PROJECT)await rpc({action:'export-project',path:resolve(process.env.ICAX_AUTOMOTIVE_START_PROJECT)});
  await page.screenshot({path:resolve(output,automotive?'automotive-start-point.png':'cut-start-point.png')});
  const result={passed:true,intentId,start:changed.start,startPoint:changed.startPoint,markers,cases:['direct contour click without coordinate form','Escape cancels in-flight lookup','cross-page lookup preserves focus without mutation','exact source and other contours unchanged','lost response receipt recovery','physical marker and number follow start','undo and redo'],coverage:{native:true,realMouse:true,standaloneCef:false}};
  if(uncertain)result.cases.push('unavailable receipt retains request across pages; retry cannot duplicate mutation');
  if(automotive){
    const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.intentToolpaths,after.intentToolpaths);
    await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
    await page.evaluate(async()=>{
      const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
      const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
      fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);
    });
    await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
    result.cases.push('embedded project reopen retains spatial spline anchor');
  }
  writeFileSync(resolve(output,'start-report.json'),JSON.stringify(result,null,2));return result;
}
