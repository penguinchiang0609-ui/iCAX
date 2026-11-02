import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyCutSplitting({page,rpc,output}) {
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const refresh=()=>page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  const open=()=>page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.split'));
  const settled=()=>page.waitForFunction(()=>!fixture.view.pending);
  const before=await read();assert.equal(before.intentToolpaths.length,5);
  const numbers=before.intentToolpaths.map(c=>c.number);assert.equal(new Set(numbers).size,5);
  await open();await page.evaluate(() => fixture.view.viewport.fitView());
  assert.equal(await page.getByRole('dialog',{name:'打断刀路'}).count(),1);
  assert.equal(await page.locator('.cam-info-pane input,.cam-info-pane select').count(),0);
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  // Use actual mouse clicks on two distant positions of a visible spatial loop.
  const picks=await page.evaluate(()=>{
    const v=fixture.view.viewport,box=v.renderer.domElement.getBoundingClientRect();
    for(const c of [...fixture.view.scene.intentToolpaths].sort((a,b)=>b.previewPoints.length-a.previewPoints.length)){
      const object=v.sceneObjects.get(c.placementId),candidates=[];
      const start=object.localToWorld(v.camera.position.clone().set(...c.previewPoints[0])).project(v.camera);
      const sx=box.x+(start.x+1)*box.width/2,sy=box.y+(1-start.y)*box.height/2;
      for(const [i,p] of c.previewPoints.entries()){
        const q=object.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);
        const x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
        if(x>650&&x<1350&&y>280&&y<790&&Math.hypot(x-sx,y-sy)>25)candidates.push({id:c.id,i,x,y});
      }
      for(const a of candidates)for(const b of candidates)if(Math.hypot(a.x-b.x,a.y-b.y)>30&&Math.abs(a.i-b.i)>c.previewPoints.length/4)return[a,b];
    }
    throw Error('No unobstructed automotive loop for splitting');
  });
  await page.evaluate(()=>{fixture.splitCanvas=document.querySelector('.cam-render-viewport canvas');fixture.splitTree=document.querySelector('.five-cut-tree');});
  await page.mouse.click(picks[0].x,picks[0].y);await settled();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points.length),1);
  assert.ok(await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.some(c=>c.userData.splitMarkers)));
  assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths,'picking only previews');
  await page.mouse.click(picks[0].x,picks[0].y);await settled();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points.length),0,'reclick removes point');
  await page.mouse.click(picks[0].x,picks[0].y);await settled();
  await page.locator('[data-five-command="five.split.close"]').last().click();
  assert.equal(await page.getByRole('dialog',{name:'打断刀路'}).count(),0);
  assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);

  // An in-flight resolve cannot put markers back after Cancel.
  await open();await page.evaluate(()=>{fixture.hold=true;fixture.holdMethod='CamCut.ResolveSplitPoint';});
  await page.mouse.click(picks[0].x,picks[0].y);await page.waitForFunction(()=>fixture.held);
  await page.locator('[data-five-command="five.split.close"]').last().click();
  await page.evaluate(()=>fixture.release());await settled();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit),null);
  assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);

  await open();
  for(const p of picks){await page.mouse.click(p.x,p.y);await settled();}
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points.length),2);
  await page.mouse.click(picks[0].x,picks[0].y);await settled();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points.length),1,'confirm one new point, as in the FACAM example');
  assert.ok(await page.evaluate(()=>fixture.splitCanvas===document.querySelector('.cam-render-viewport canvas')&&fixture.splitTree===document.querySelector('.five-cut-tree')),'picking preserves canvas and tree nodes');
  await page.screenshot({path:resolve(output,'automotive-split-points.png')});
  await page.evaluate(()=>{fixture.loseMethod='CamCut.Split';});
  await page.locator('[data-five-command="five.split.confirm"]').click();await settled();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const after=await read();assert.equal(after.intentToolpaths.length,6,'one new point and the retained start divide the closed contour into two');
  const pieces=after.intentToolpaths.filter(c=>c.split);assert.equal(pieces.length,2);
  assert.equal(pieces.filter(c=>c.id===picks[0].id).length,1,'first fragment retains original identity');
  assert.equal(after.intentToolpaths.find(c=>c.id===picks[0].id).number,before.intentToolpaths.find(c=>c.id===picks[0].id).number);
  const retainedStart=after.intentToolpaths.find(c=>c.id===picks[0].id).startPoint;
  const previousStart=before.intentToolpaths.find(c=>c.id===picks[0].id).startPoint;
  assert.ok(Math.hypot(...retainedStart.map((v,i)=>v-previousStart[i]))<1e-7,'splitting retains the newly selected physical start');
  assert.deepEqual(after.intentToolpaths.filter(c=>!c.split),before.intentToolpaths.filter(c=>c.id!==picks[0].id));
  assert.equal(await page.locator('[data-five-intent]').count(),6);
  assert.ok(await page.evaluate(()=>fixture.view.fiveCutAnnotations));
  await page.screenshot({path:resolve(output,'automotive-split-confirmed.png')});
  const definitions=await rpc({action:'invoke',scope:'scene',method:'CamCut.Read',payload:{expected:after.expected,intentIds:pieces.map(c=>c.id)}});
  assert.ok(definitions.items.every(d=>!d.closed&&d.geometryKind==='GeneralContour'));
  assert.deepEqual((await rpc({action:'undo'})).state.intentToolpaths,before.intentToolpaths);
  assert.deepEqual((await rpc({action:'redo'})).state.intentToolpaths,after.intentToolpaths);
  await refresh();

  // Continue splitting an already-open spatial fragment through the same dialog.
  await open();
  const endpoint=await page.evaluate(number=>{
    const v=fixture.view.viewport,l=v.specificationLabels.find(l=>l.element.textContent===number),b=v.renderer.domElement.getBoundingClientRect();
    const p=l.anchorPoint.clone().project(v.camera);return{x:b.x+(p.x+1)*b.width/2,y:b.y+(1-p.y)*b.height/2};
  },pieces[0].number);
  await page.mouse.click(endpoint.x,endpoint.y);await settled();
  assert.match(await page.evaluate(()=>fixture.view.error),/端点/,'number leader points at the real endpoint');
  await open();
  const next=await page.evaluate(ids=>{
    const v=fixture.view.viewport,box=v.renderer.domElement.getBoundingClientRect();
    for(const c of fixture.view.scene.intentToolpaths.filter(c=>ids.includes(c.id))){
      const object=v.sceneObjects.get(c.placementId);
      const screen=p=>{const q=object.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);return{x:box.x+(q.x+1)*box.width/2,y:box.y+(1-q.y)*box.height/2};};
      const ends=[screen(c.previewPoints[0]),screen(c.previewPoints.at(-1))];
      for(const p of c.previewPoints.slice(2,-2)){
        const q=object.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);
        const x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
        if(x>650&&x<1350&&y>280&&y<790&&ends.every(e=>Math.hypot(e.x-x,e.y-y)>35))return{x,y};
      }
    }throw Error('No open-fragment interior point');
  },pieces.map(c=>c.id));
  await page.mouse.click(next.x,next.y);await settled();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points.length),1,'open contour interior click must reach the viewport');
  await page.locator('[data-five-command="five.split.confirm"]').click({timeout:10000});await settled();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const resplit=await read();assert.equal(resplit.intentToolpaths.length,7);
  assert.equal(resplit.intentToolpaths.filter(c=>c.split).length,3);
  if(process.env.ICAX_AUTOMOTIVE_SPLIT_PROJECT)await rpc({action:'export-project',path:resolve(process.env.ICAX_AUTOMOTIVE_SPLIT_PROJECT)});
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.intentToolpaths,resplit.intentToolpaths);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  await page.evaluate(async()=>{
    const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);
    fixture.entry.mountProject(fixture.context);
  });
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  await open();await page.locator('[data-five-command="five.split.close"]').last().click();
  assert.equal(await page.locator('[data-five-intent]').count(),7);
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({path:resolve(output,'automotive-split-reopened.png')});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract'));
  await page.locator('[data-five-command="five.extraction.automatic"]').click();await settled();
  assert.equal(await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.selected.length),0,'split ancestry still suppresses original duplicate extraction');
  await page.locator('[data-five-command="five.extraction.close"]').last().click();
  writeFileSync(resolve(output,'split-report.json'),JSON.stringify({passed:true,before:5,after:6,afterResplit:7,
    cases:['FACAM compact dialog','real mouse exact point resolution','preview markers and toggle','cancel and in-flight cancellation','canvas and tree node identity','lost response receipt recovery','closed contour split and stable numbers','undo and redo','number leader retains exact endpoint','open contour resplit','embedded reopen','original-source duplicate suppression'],
    coverage:{native:'production SDO and geometry service',browser:'Edge real viewport',fileChooser:'injected path',standaloneCef:false}},null,2));
}
