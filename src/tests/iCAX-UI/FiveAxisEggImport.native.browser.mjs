import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyEggImport({page,rpc,root,output,errors}) {
  const source=resolve(root,'TestData/FA/刀路/刀路/整理测试刀路/蛋型零件-各种形状孔-two-layer-1-5.stp');
  page.setDefaultTimeout(300000);
  await selectWorkpieceFile(page,source);
  await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive,{},{timeout:300000});
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.equal(state.workpieces.length,1);
  assert.equal(state.workpieces[0].importMetadata.validBRep,true);
  await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount>0);
  const display=await page.evaluate(()=>fixture.view.viewport.getDebugState());
  assert.ok(display.contentBounds);
  await page.screenshot({path:resolve(output,'egg-import.png')});
  let cutting;
  if(process.env.ICAX_EGG_CUTS_ONLY) {
    await rpc({action:'export-project',path:resolve(output,'egg-import.i5cam')});
    await page.evaluate(async()=>{
      window.extractionModule=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
      await fixture.context.actions.selectRibbonTab('toolpath');
    });
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract'));
    assert.equal(await page.getByRole('dialog',{name:'添加刀路'}).count(),1);
    assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const sources=await rpc({action:'invoke',scope:'scene',method:'CamCut.QuerySources',payload:{expected:state.expected,
      setupId:state.activeSetupId,placementId:state.placements[0].id}});
    assert.equal(sources.faceUses.length,3996);
    writeFileSync(resolve(output,'sources.json'),JSON.stringify(sources,null,2));
    await page.evaluate(()=>fixture.view.viewport.fitView(1.1));
    const point=await page.evaluate(async()=>{
      const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
      const v=fixture.view.viewport,object=[...v.sceneObjects.values()][0],box=v.renderer.domElement.getBoundingClientRect();
      v.scene.updateMatrixWorld(true);v.camera.updateMatrixWorld(true);
      const mesh=object,geometry=mesh.geometry,p=geometry.getAttribute('position'),a=new THREE.Vector3(),b=a.clone(),c=a.clone();
      let best;
      const project=(q,i)=>q.fromBufferAttribute(p,geometry.index?geometry.index.getX(i):i).applyMatrix4(mesh.matrixWorld).project(v.camera);
      const count=geometry.index?.count??p.count;
      for(let i=0;i<count;i+=3){
        project(a,i);project(b,i+1);project(c,i+2);
        if([a,b,c].some(q=>q.z < -1||q.z > 1))continue;
        const x=box.x+((a.x+b.x+c.x)/3+1)*box.width/2,y=box.y+(1-(a.y+b.y+c.y)/3)*box.height/2;
        const area=Math.abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x));
        if(x>box.left+20&&x<box.right-20&&y>box.top+20&&y<box.bottom-20&&document.elementFromPoint(x,y)===v.renderer.domElement&&(!best||area>best.area))best={x,y,area};
      }
      if(!best)throw Error('No visible egg face');return best;
    });
    // Exercise the rectangle route that queries all source face uses, including
    // back faces, then make the machining selection with a real pointer click.
    await page.mouse.move(point.x+5,point.y-5);await page.mouse.down();
    await page.mouse.move(point.x-5,point.y+5,{steps:5});await page.mouse.up();
    await page.waitForFunction(()=>!fixture.view.pending);
    assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const rectangleFaces=await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.faces.length);
    assert.ok(rectangleFaces>0);
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.clear'));
    await page.mouse.click(point.x,point.y);
    await page.waitForFunction(()=>!fixture.view.pending);
    const selectedFaces=await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.faces.map(f=>f.localId));
    assert.equal(selectedFaces.length,1);assert.equal(await page.evaluate(()=>fixture.view.error),'');
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.run'));
    await page.waitForFunction(()=>!fixture.view.pending);
    const status=await page.evaluate(()=>({error:fixture.view.error,result:extractionModule.extractionResult(fixture.view),selected:extractionModule.extractionDraft(fixture.view).extraction.selected}));
    writeFileSync(resolve(output,'extraction.json'),JSON.stringify({selectedFaces,rectangleFaces,...status},null,2));
    assert.equal(status.error,'');assert.ok(status.selected.length>1);
    assert.ok(status.result.candidates.every(c=>c.boundary.topologyClosed));
    await page.screenshot({path:resolve(output,'egg-extracted.png')});
    await page.locator('[data-five-command="five.extraction.confirm"]').click();
    await page.waitForFunction(()=>!fixture.view.pending);
    const created=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    assert.equal(await page.evaluate(()=>fixture.view.error),'');
    assert.equal(created.intentToolpaths.length,status.selected.length);
    assert.equal(await page.locator('.five-cut-tree [data-five-intent]').count(),status.selected.length);
    await page.screenshot({path:resolve(output,'egg-confirmed.png')});
    assert.equal((await rpc({action:'undo'})).state.intentToolpaths.length,0);
    assert.equal((await rpc({action:'redo'})).state.intentToolpaths.length,status.selected.length);
    cutting={selectedFaces,rectangleFaces,candidateCount:status.result.candidates.length,created:created.intentToolpaths};
  }
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.workpieces,state.workpieces);
  assert.deepEqual(reopened.placements,state.placements);
  if(cutting)assert.deepEqual(reopened.intentToolpaths,cutting.created);
  assert.deepEqual(errors,[]);
  const sdoTimings=await page.evaluate(()=>fixture.sdoTimings??[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,source,state,display,cutting,sdoTimings,
    saveReopen:true,coverage:cutting?'Production file picker, Scene import, real View, rectangle and pointer face selection, extraction, cut creation, undo/redo and save/reopen in Edge; standalone CEF not automated':'Production file picker, Scene import, persisted workpiece, real View resources and Edge rendering; standalone CEF not automated'},null,2));
}
