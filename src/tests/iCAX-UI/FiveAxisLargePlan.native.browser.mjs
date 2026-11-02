import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';
import {deserializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

export async function verifyLargePlan({page,rpc,calls,output}){
  page.setDefaultTimeout(300000);
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending,null,{timeout:1200000});
  let state=await read();
  if(!state.workpieces.length){await selectWorkpieceFile(page,resolve('TestData/FA/刀路/刀路/整理测试刀路/蛋型零件-各种形状孔-two-layer-1-5.stp'));await idle();state=await read();assert.equal(await page.evaluate(()=>fixture.view.error),'');}
  assert.equal(state.workpieces.length,1);
  await page.evaluate(async()=>{window.extractionModule=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');await fixture.context.actions.selectRibbonTab('toolpath');});
  if(!state.intentToolpaths.length){
    const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:state.activeSetupId,placementId:state.placements[0].id});
    assert.equal(sources.faceUses.length,3996);
    const inventory=await rpc({action:'face-inventory'}),skin=inventory.faces.filter(f=>Number(f.wireCount)>100);
    assert.equal(skin.length,3);console.log('Egg machining skins:',skin);
    await rpc({action:'export-project',path:resolve(output,'egg-import-current.i5cam')});
    await command('five.tool.extract');
    await page.evaluate(faces=>{const d=extractionModule.extractionDraft(fixture.view).extraction;d.mode='Faces';d.faces=faces;extractionModule.extractionChanged(fixture.view);},sources.faceUses.filter(f=>skin.some(s=>s.id===f.localId)));
    await command('five.extraction.run');await idle();
    const extracted=await page.evaluate(()=>({error:fixture.view.error,result:extractionModule.extractionResult(fixture.view),selected:extractionModule.extractionDraft(fixture.view).extraction.selected}));
    writeFileSync(resolve(output,'extraction-summary.json'),JSON.stringify({error:extracted.error,candidates:extracted.result?.candidates.map(c=>({id:c.candidateId,kind:c.geometryKind,ready:c.confirmationEligibility,faces:[...new Set(c.boundary.sourceSpans.map(s=>s.faceId))]})),selected:extracted.selected},null,2));
    assert.equal(extracted.error,'');console.log('Egg extraction:',extracted.result.candidates.length,extracted.selected.length);
    assert.equal(extracted.selected.length,480);
    await page.getByRole('button',{name:'确定',exact:true}).click();await idle();
    state=await read();assert.equal(state.intentToolpaths.length,480);
    await rpc({action:'export-project',path:resolve(output,'egg-480-cuts.i5cam')});
  }
  assert.equal(state.intentToolpaths.length,480);
  const before=state.intentToolpaths;
  await page.waitForFunction(()=>fixture.view.fiveToolAxisDisplay?.data?.cuts?.length===fixture.view.scene.intentToolpaths.filter(c=>c.enabled!==false&&c.machiningKind==='Cut').length,null,{timeout:300000});
  const display=await page.evaluate(()=>({count:fixture.view.fiveToolAxisDisplay.data.cuts.length,errors:fixture.view.fiveToolAxisDisplay.data.errors,groups:fixture.view.fiveToolAxisDisplay.group.children.length}));
  assert.deepEqual(display.errors,[]);assert.equal(display.count,before.filter(c=>c.enabled!==false&&c.machiningKind==='Cut').length);assert.equal(display.groups,display.count);
  const sampleCalls=calls.filter(c=>c.method==='CamCut.ReadToolAxisSamples').map(c=>(c.payload??deserializeVariantText(c.payloadText)).intentIds.length);
  assert.ok(sampleCalls.length>=Math.ceil(display.count/256)&&sampleCalls.every(n=>n<=256));
  // Requests originate from the production frontend; native read budget remains unchanged.
  console.log('All cutting path keypoints loaded:',display.count);
  const started=Date.now();await command('five.tool.generate');await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  state=await read();const geometry=state.generatedGeometry;
  writeFileSync(resolve(output,'generation-state-summary.json'),JSON.stringify({keys:Object.keys(state),geometry:geometry&&{status:geometry.status,operations:geometry.operations?.length,transitions:geometry.transitions?.length}},null,2));
  const result=state.generatedGeometry??state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;
  const task=await page.evaluate(()=>fixture.view.fiveRapidBatch.task);
  assert.ok(Object.values(task.result.connections).every(c=>/^sha256:[0-9a-f]{64}$/.test(c.inputKey)),'rapid input identity stays compact for large jobs');
  assert.equal(result.status,'Current');assert.equal(result.operations.length,480);assert.equal(result.transitions.length,479);
  assert.deepEqual(new Set(result.operations.map(o=>o.intentId)),new Set(before.map(c=>c.id)));
  assert.deepEqual(state.intentToolpaths,before);
  for(const op of result.operations){assert.ok(op.curveRef.url);for(const part of op.parts){assert.ok(Number(part.pointCount)>0);assert.equal(part.points,undefined);assert.equal(part.directions,undefined);}}
  const sampleProjection=await page.evaluate(()=>{const g=fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).generatedGeometry;let points=0,valid=true;
    for(const op of g.operations)for(const part of op.parts){points+=part.points.length;valid&&=part.points.length===Number(part.pointCount)&&part.points.length===part.directions.length&&part.directions.every(n=>Math.abs(Math.hypot(...n)-1)<1e-8);}
    return {points,valid,resourceReads:fixture.pathResourceReads,operations:g.operations.length,rapids:g.transitions.filter(t=>t.geometryStatus==='Generated'&&t.points.length===Number(t.pointCount)).length};});
  assert.equal(sampleProjection.valid,true);assert.equal(sampleProjection.operations,480);assert.equal(sampleProjection.rapids,479);assert.ok(sampleProjection.resourceReads.length>=1);
  for(const t of result.transitions){assert.ok(t.validation);assert.equal(t.laser,false);assert.equal(t.frame,'Setup');}
  const elapsedMs=Date.now()-started;
  await command('five.generate.close');await page.evaluate(()=>fixture.view.viewport.fitView(1.1));
  await page.screenshot({path:resolve(output,'egg-480-generated.png')});
  await rpc({action:'export-project',path:resolve(output,'egg-480-generated.i5cam')});
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'}),saved=reopened.generatedGeometry??reopened.setups.find(s=>s.id===reopened.activeSetupId).generatedGeometry;
  assert.equal(saved.operations.length,480);assert.equal(saved.transitions.length,479);assert.equal(saved.status,'Current');
  const counts={};for(const t of result.transitions)counts[t.validation]=(counts[t.validation]||0)+1;
  const timings=await page.evaluate(()=>fixture.sdoTimings??[]);
  writeFileSync(resolve(output,'large-plan-report.json'),JSON.stringify({passed:true,operations:480,transitions:479,display,elapsedMs,validation:counts,sampleProjection,sampleCalls,saveReopen:true,timings:timings.filter(t=>t.elapsedMs>1000),coverage:'Real egg STEP BRep, production extraction/create/generate/rapid, default bounded native SDO queue, versioned double-precision path resources, real SDK timeout client and Edge; machine motion and standalone CEF not tested'},null,2));
  console.log('480-path generation passed:',elapsedMs,'ms',counts);
}
