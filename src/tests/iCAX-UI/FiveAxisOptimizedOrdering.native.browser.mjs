import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { verifyOrdering } from './FiveAxisOrdering.native.browser.mjs';

export async function verifyOptimizedOrdering({page,rpc,calls,output,sourcePath}) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const request=s=>({commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId});
  const wait=async task=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
  let state=await read();
  console.log('Optimized order: loaded',state.workpieces.length,'workpieces',state.intentToolpaths.length,'cuts');
  if(!state.workpieces.length){
    await selectWorkpieceFile(page,sourcePath);await idle();state=await read();assert.equal(state.workpieces[0].importMetadata.validBRep,true);
    await page.evaluate(async()=>{window.extractionModule=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');await fixture.context.actions.selectRibbonTab('toolpath');});
    await command('five.tool.extract');
    if(process.env.ICAX_BRACKET_FACE){const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:state.activeSetupId,placementId:state.placements[0].id});
      const face=sources.faceUses.find(f=>f.localId===process.env.ICAX_BRACKET_FACE);assert.ok(face);
      await page.evaluate(face=>{const e=extractionModule.extractionDraft(fixture.view).extraction;e.mode='Faces';e.faces=[face];extractionModule.extractionChanged(fixture.view);},face);await command('five.extraction.run');
    }else await page.locator('[data-five-command="five.extraction.automatic"]').click();
    await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();state=await read();
  }
  assert.ok(state.intentToolpaths.length>=7);
  const sourceMetadata=state.workpieces.map(w=>({name:w.name,importMetadata:w.importMetadata}));
  await invoke('CamProcess.Clear',{commandId:crypto.randomUUID(),expected:state.expected,intentIds:state.intentToolpaths.map(c=>c.id),categories:['Leads','Overcut','Gap','Kerf']});
  state=await read();await wait(await invoke('CamPlan.Generate',request(state)));state=await read();
  console.log('Optimized order: exact generation completed');
  const original=setup(state).generatedGeometry,ops=original.operations;
  // Reproduce the removed policy on actual generated endpoints, not invented coordinates.
  const candidates=[...ops].sort((a,b)=>Number(a.cutNumber)-Number(b.cutNumber)||a.intentId.localeCompare(b.intentId));
  const phase=o=>o.machiningKind==='Mark'?0:o.boundaryRole==='OuterTrim'?2:1;
  const dist=(a,b)=>Math.hypot(...a.map((v,i)=>v-b[i]));
  const baseline=[];
  for(const p of [0,1,2])while(candidates.some(o=>phase(o)===p)){
    const available=candidates.filter(o=>phase(o)===p);let best=available[0];
    if(baseline.length)for(const op of available)if(dist(baseline.at(-1).setupEnd,op.setupStart)<dist(baseline.at(-1).setupEnd,best.setupStart))best=op;
    baseline.push(best);candidates.splice(candidates.indexOf(best),1);
  }
  const setManual=async ids=>invoke('CamPlan.SetOrder',{...request(await read()),mode:'Manual',intentIds:ids});
  const auto=async()=>invoke('CamPlan.SetOrder',{...request(await read()),mode:'Automatic'});
  const metrics=operations=>{let distanceMm=0,turnRad=0;for(let i=1;i<operations.length;++i){const a=operations[i-1],b=operations[i];distanceMm+=dist(a.setupEnd,b.setupStart);
    turnRad+=Math.acos(Math.max(-1,Math.min(1,a.setupEndDirection.reduce((v,x,k)=>v+x*b.setupStartDirection[k],0))));}return {distanceMm,turnRad};};
  const travel=async()=>{const s=await read();await wait(await invoke('CamRapid.Generate',{...request(s),geometryRevision:setup(s).generatedGeometry.revision}));
    const connections=setup(await read()).generatedGeometry.transitions;return {clear:connections.filter(t=>t.validation==='Clear').length,blocked:connections.filter(t=>t.validation!=='Clear').map(t=>({from:t.from,to:t.to,reason:t.reason})),
      secondsOfClearConnections:connections.filter(t=>t.validation==='Clear').reduce((sum,t)=>sum+t.estimatedSeconds,0),lengthOfClearConnectionsMm:connections.filter(t=>t.validation==='Clear').reduce((sum,t)=>sum+t.lengthMm,0)};};
  await setManual(baseline.map(o=>o.intentId));await command('five.refresh');await page.evaluate(()=>fixture.view.viewport.fitView());
  await page.screenshot({path:resolve(output,'ordering-before.png')});const oldTravel=await travel();
  console.log('Optimized order: baseline travel',oldTravel);
  const started=Date.now(),sorted=await auto(),elapsedMs=Date.now()-started;state=await read();const optimized=setup(state).generatedGeometry;
  assert.deepEqual(optimized.revision,original.revision);assert.equal(optimized.status,'Current');
  assert.equal(sorted.optimization.costScope,'HeightPenalizedStraightLineTravelEstimate');assert.ok(sorted.optimization.afterCost<=sorted.optimization.beforeCost+1e-8);
  const optimizedTravel=await travel();await command('five.refresh');await page.screenshot({path:resolve(output,'ordering-after.png')});
  console.log('Optimized order: candidate travel',optimizedTravel,'elapsed',elapsedMs,'ms',sorted.optimization);
  writeFileSync(resolve(output,'comparison.json'),JSON.stringify({sourcePath:process.env.ICAX_RAPID_PROJECT||sourcePath,sourceMetadata,operations:ops.length,elapsedMs,
    baseline:{ids:baseline.map(o=>o.intentId),...metrics(baseline),travel:oldTravel},optimized:{ids:optimized.operations.map(o=>o.intentId),...metrics(optimized.operations),travel:optimizedTravel},optimization:sorted.optimization},null,2));
  await rpc({action:'export-project',path:resolve(output,'comparison-model.i5cam')});
  assert.equal((await auto()).changed,false,'repeated optimization is stable');
  if(process.env.ICAX_ORDER_COMPARISON_ONLY)return;
  // Native persistence/retry and real browser interaction regression on this model.
  await verifyOrdering({page,rpc,calls,output});await command('five.order.close');
  // The manual-order regression deliberately ends with a user-defined order.
  // Restore a feasible machining order before testing independent group/lock rules.
  await auto();
  const tree=async(action,ids=[],groupId)=>invoke('CamCut.ManageTree',{...request(await read()),action,intentIds:ids,...(groupId?{groupId}:{})});
  let current=await read();const ordered=setup(current).definitionOrder,byID=new Map(current.intentToolpaths.map(c=>[c.id,c]));
  const ordinary=ordered.filter(id=>byID.get(id).machiningKind!=='Mark'&&byID.get(id).boundaryRole!=='OuterTrim');assert.ok(ordinary.length>=3);
  await tree('Group',[ordinary[0],ordinary[2]]);current=await read();let groups=setup(current).cutGroups;const inner=groups.at(-1).id;
  // Group the existing group with a third member using the production tree API.
  await invoke('CamCut.ManageTree',{...request(current),action:'Group',intentIds:[ordinary[1]],groupIds:[inner]});
  current=await read();groups=setup(current).cutGroups;const outer=groups.find(g=>g.id!==inner&&!g.parentId).id;
  const grouped=await auto();assert.ok(grouped.optimization.afterCost<=grouped.optimization.beforeCost+1e-8);
  const groupOrder=setup(await read()).definitionOrder,positions=[ordinary[0],ordinary[2]].map(id=>groupOrder.indexOf(id));
  assert.equal(Math.max(...positions)-Math.min(...positions),1,'inner group remains contiguous while optimized');
  await tree('Lock',[],outer);const locked=setup(await read()).definitionOrder;
  await auto();const after=setup(await read()).definitionOrder;
  for(const id of [ordinary[0],ordinary[1],ordinary[2]])assert.equal(after.indexOf(id),locked.indexOf(id));
  const free=ordinary.find(id=>![ordinary[0],ordinary[1],ordinary[2]].includes(id));if(free){await tree('Disable',[free]);await auto();assert.ok(setup(await read()).definitionOrder.includes(free));}
  const persistent=setup(await read()).definitionOrder;const reopened=await rpc({action:'save-reopen'});assert.deepEqual(setup(reopened).definitionOrder,persistent);
  await rpc({action:'export-project',path:resolve(output,'optimized-grouped-model.i5cam')});
  writeFileSync(resolve(output,'optimization-acceptance.json'),JSON.stringify({passed:true,operations:ops.length,cases:['actual STEP process endpoints','old-policy baseline and real travel planner before/after','per-placement process precedence','unchanged exact geometry','stable repeated automatic order','native undo redo and embedded persistence','lost response retry','canvas scroll focus selection regression','nested groups locked at original positions','disabled membership','grouped project reopen'],coverage:{native:true,browser:'Edge',standaloneCef:false,machineAxes:false,travelPlanner:true}},null,2));
}
