import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {writeFileSync} from 'node:fs';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyManualSelection({page,rpc,info,root,output,calls,errors}) {
  page.setDefaultTimeout(120000);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const clickPoint=async point=>{
    const screen=await page.evaluate(p=>{
      const v=fixture.view.viewport,o=[...v.sceneObjects.values()][0],box=v.renderer.domElement.getBoundingClientRect();
      const q=o.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);
      return {x:box.x+(q.x+1)*box.width/2,y:box.y+(1-q.y)*box.height/2};
    },point);
    await page.mouse.click(screen.x,screen.y);await idle();
  };
  console.log('Manual selection: import');
  await selectWorkpieceFile(page,info.sourcePath);await idle();
  await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size===1);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await command('five.tool.extract');await idle();
  await page.evaluate(async()=>{
    window.manual=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
    const v=fixture.view.viewport;v.fitView(1.3);v.setCameraState({...v.getCameraState(),phi:.0001,theta:0});
    fixture.originalCanvas=v.renderer.domElement;fixture.originalMesh=[...v.sceneObjects.values()][0];
  });
  await page.addStyleTag({content:'.five-position-dialog{left:8px!important;top:8px!important;width:320px!important;transform:none!important}'});
  await page.locator('[data-five-command="five.extraction.mode.Region"]').click();await clickPoint([10,10,10]);
  await page.locator('[data-five-command="five.extraction.run"]').click();await idle();
  const planarRegion=await page.evaluate(()=>manual.extractionResult(fixture.view));assert.equal(planarRegion.candidates.length,2);
  await page.locator('[data-five-command="five.extraction.filter.outer"]').click();assert.equal(await page.evaluate(()=>manual.extractionDraft(fixture.view).extraction.selected.length),1);
  await page.locator('[data-five-command="five.extraction.filter.inner"]').click();assert.equal(await page.evaluate(()=>manual.extractionDraft(fixture.view).extraction.selected.length),1);
  await page.locator('[data-five-command="five.extraction.mode.Edges"]').click();
  console.log('Manual selection: mouse edges');
  for(const p of [[19.9,10,10],[-19.9,10,10],[10,-19.9,10]])await clickPoint(p);
  assert.equal(await page.evaluate(()=>manual.extractionDraft(fixture.view).extraction.edges.length),3);
  await page.locator('[data-five-command="five.extraction.run"]').click();await idle();
  assert.match(await page.evaluate(()=>fixture.view.error),/尚未闭合/);
  assert.equal((await read()).intentToolpaths.length,0);
  await command('five.extraction.pick');await clickPoint([10,19.9,10]);
  assert.equal(await page.evaluate(()=>manual.extractionDraft(fixture.view).extraction.edges.length),4);
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1200px}.five-extraction-faces{max-height:12px!important}'});
  await page.evaluate(()=>{fixture.holdMethod='CamCut.ExtractCandidates';fixture.hold=true;});
  await page.locator('[data-five-command="five.extraction.run"]').click();await page.waitForFunction(()=>fixture.held);
  const interaction=await page.evaluate(()=>{
    const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-extraction-faces')];
    nodes.forEach((n,i)=>n.scrollTop=40+i*10);return nodes.map(n=>n.scrollTop);
  });
  await page.evaluate(()=>fixture.release());await idle();
  const retained=await page.evaluate(()=>({scroll:[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-extraction-faces')].map(n=>n.scrollTop),
    canvas:fixture.originalCanvas===fixture.view.viewport.renderer.domElement,mesh:fixture.originalMesh===[...fixture.view.viewport.sceneObjects.values()][0]}));
  assert.deepEqual(retained.scroll,interaction);
  assert.equal(retained.canvas,true);assert.equal(retained.mesh,true);
  await stress.evaluate(n=>n.remove());
  await page.evaluate(()=>document.querySelectorAll('.cam-context-pane,.cam-info-pane').forEach(n=>n.scrollTop=0));
  const loop=await page.evaluate(()=>manual.extractionResult(fixture.view));
  assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(loop.candidates.length,1);
  assert.equal(loop.candidates[0].boundary.sourceSpans.length,4);
  assert.equal((await read()).intentToolpaths.length,0,'preview never creates');
  await page.screenshot({path:resolve(output,'selected-edge-loop.png')});
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  const created=await read();assert.equal(created.intentToolpaths.length,1);
  console.log('Manual selection: creation and reopen');
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.equal((await read()).intentToolpaths.length,0);
  await rpc({action:'redo'});await command('five.refresh');await idle();assert.equal((await read()).intentToolpaths.length,1);
  await command('five.tool.generate');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const edgeGenerated=(await read()).setups.find(s=>s.generatedGeometry?.status==='Current')?.generatedGeometry;
  assert.ok(edgeGenerated);assert.equal(edgeGenerated.operations.length,1);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.intentToolpaths,created.intentToolpaths);
  await page.evaluate(async()=>{
    const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);
    fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);
  });await idle();
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));
  await selectWorkpieceFile(page,resolve(root,'TestData/FA/26.9.2零件.IGS'));await idle();
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  console.log('Manual selection: adjacent IGES region');
  await command('five.tool.extract');await idle();
  await page.locator('[data-five-command="five.extraction.mode.Region"]').click();
  await page.evaluate(async()=>{
    const v=fixture.view,d=v.scene.domain,p=d.placements.find(p=>p.id===v.scene.activePlacementId)||d.placements.at(-1);
    const sources=await fixture.context.sceneProxy.invoke('CamCut.QuerySources',{expected:d.expected,setupId:p.setupId,placementId:p.id});
    const mapped=await fixture.context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:d.expected,setupId:p.setupId,placementId:p.id});
    const state=manual.extractionDraft(v).extraction;state.faces=sources.faceUses.filter(f=>['1','176'].includes(f.localId));
    state.faceRanges=Object.fromEntries(state.faces.map(f=>[f.localId,mapped.faces.filter(r=>String(r.faceId)===f.localId)]));
    manual.extractionChanged(v);fixture.entry.mountProject(fixture.context);
  });
  await page.locator('[data-five-command="five.extraction.run"]').click();await idle();
  const region=await page.evaluate(()=>manual.extractionResult(fixture.view));
  writeFileSync(resolve(output,'region-candidates.json'),JSON.stringify(region,null,2));
  assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(region.candidates.length,7);
  const seam=new Set(['2','13','24','35','39','50','61','6400','6391','6382','6373','6369','6360','6351']);
  assert.ok(region.candidates.flatMap(c=>c.boundary.sourceSpans).every(s=>!seam.has(s.edgeId)));
  for(const [filter,relation] of [['outer','OuterLoop'],['inner','InnerLoop']]){
    const count=region.candidates.filter(c=>c.boundary.regionRelation===relation).length;
    if(count){await page.locator(`[data-five-command="five.extraction.filter.${filter}"]`).click();assert.equal(await page.evaluate(()=>manual.extractionDraft(fixture.view).extraction.selected.length),count);}
    else assert.equal(await page.locator(`[data-five-command="five.extraction.filter.${filter}"]`).isDisabled(),true);
  }
  await page.locator('[data-five-command="five.extraction.filter.all"]').click();
  assert.equal(await page.evaluate(()=>manual.extractionDraft(fixture.view).extraction.selected.length),7);
  await page.screenshot({path:resolve(output,'selected-machining-region.png')});
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  assert.equal((await read()).intentToolpaths.length,8);
  await command('five.tool.generate');await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const generatedState=await read(),generated=generatedState.setups.find(s=>s.id===generatedState.activeSetupId)?.generatedGeometry;
  assert.equal(generated?.status,'Current');assert.equal(generated.operations.length,7);
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,edgeLoop:loop,planarRegion,region,generatedOperations:{edgeLoop:edgeGenerated.operations.length,region:generated.operations.length},interaction:retained,undoRedo:true,saveReopen:true,coverage:'Real mouse face and edge selection and production native SDO; adjacent IGES faces selected by topology IDs; CEF standalone not automated',calls},null,2));
}
