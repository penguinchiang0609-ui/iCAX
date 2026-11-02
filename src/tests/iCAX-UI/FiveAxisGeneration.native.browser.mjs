import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyGeneration({page,rpc,calls,output}){
  console.log('Generate: exact automotive spatial curves, leads, closure and ordering');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const geometry=state=>state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveGeneration?.running&&!fixture.view.fiveRapidBatch?.running);
  const remount=async()=>{
    await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
    await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
      fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
    await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  };
  await remount();const initial=await read(),ids=initial.intentToolpaths.map(c=>c.id);
  await invoke('CamProcess.Clear',{commandId:crypto.randomUUID(),expected:initial.expected,intentIds:ids,categories:['Leads','Overcut','Gap','Kerf']});
  const definitions=(await invoke('CamCut.Read',{expected:(await read()).expected,intentIds:ids})).items;
  const closed=initial.intentToolpaths.filter(c=>definitions.find(d=>d.intentId===c.id)?.closed).sort((a,b)=>b.previewPoints.length-a.previewPoints.length);
  const open=initial.intentToolpaths.find(c=>c.split&&!definitions.find(d=>d.intentId===c.id)?.closed);assert.ok(closed.length>=2&&open);
  const patch=async(id,patch)=>invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:(await read()).expected,intentIds:[id],patch});
  const lead={kind:'Line',placement:'ScrapSide',lengthMm:.02,angleRad:0};
  await patch(closed[0].id,{gapLengthMm:1,leadOut:lead});await patch(closed[1].id,{overcutLengthMm:.5});await patch(open.id,{leadIn:lead});
  await command('five.refresh');const before=await read();
  await page.evaluate(()=>fixture.generationCanvas=document.querySelector('canvas'));
  await command('five.tool.generate');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const result=geometry(await read());assert.equal(result.status,'Current');assert.equal(result.operations.length,ids.length);assert.equal(result.transitions.length,ids.length-1);
  assert.equal(result.productionQualified,false);assert.equal(result.motionState,'NotPlanned');assert.equal(result.collisionState,'Unchecked');
  assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);
  assert.equal(await page.evaluate(()=>fixture.generationCanvas===document.querySelector('canvas')),true);
  assert.ok(result.operations.some(o=>!o.closed)&&result.operations.some(o=>o.gapLengthMm===1)&&result.operations.some(o=>o.overcutLengthMm===.5));
  let outer=false;
  for(let i=0;i<result.operations.length;++i){const op=result.operations[i];assert.equal(op.order,String(i+1));assert.ok(op.curveRef.url&&op.lengthMm>0);
    if(op.boundaryRole==='OuterTrim')outer=true;else assert.equal(outer,false,'outer contours must be last');
    for(const part of op.parts){assert.equal(part.points.length,part.directions.length);assert.ok(part.points.length>=2);
      for(const n of part.directions)assert.ok(Math.abs(Math.hypot(...n)-1)<1e-8);
      if(part.role==='Cut')assert.equal(part.sourceSamples.length,part.points.length);
    }
    if(i){const t=result.transitions[i-1];assert.equal(t.from,result.operations[i-1].intentId);assert.equal(t.to,op.intentId);assert.equal(t.frame,'Setup');assert.equal(t.points.length,4);assert.equal(t.reason,'TRAVEL_ENVELOPE_REQUIRED');assert.equal(t.laser,false);assert.equal(t.validation,'Unchecked');}
  }
  const gapOp=result.operations.find(o=>o.intentId===closed[0].id),gapPart=gapOp.parts.find(p=>p.role==='Cut');
  const leadPreview=(await invoke('CamProcess.PreviewLeads',{expected:(await read()).expected,intentIds:[closed[0].id]})).items[0];
  const close=(a,b)=>Math.hypot(...a.map((v,i)=>v-b[i]))<1e-6;
  assert.ok(close(gapPart.points[0],leadPreview.start));assert.ok(close(gapPart.points.at(-1),leadPreview.end));
  assert.equal(await page.evaluate(()=>fixture.view.fiveGeneratedPreview.group.children.filter(o=>o.userData.generatedRapid).length),ids.length-1);
  assert.ok(await page.evaluate(()=>fixture.view.fiveGeneratedPreview.group.children.some(o=>o.userData.generatedIntentId)));
  assert.equal(await page.locator('.icax-three-specification-label').first().evaluate(n=>getComputedStyle(n).backgroundColor),'rgb(216, 61, 61)');
  await page.evaluate(() => fixture.view.viewport.fitView());await page.screenshot({path:resolve(output,'automotive-generated-toolpaths.png')});
  // Exact editing changes leave a visibly stale result; undo restores eligibility.
  await invoke('CamCut.Reverse',{commandId:crypto.randomUUID(),expected:(await read()).expected,intentIds:[closed[1].id]});await command('five.refresh');
  assert.equal(geometry(await read()).status,'Stale');assert.match(await page.getByRole('dialog',{name:'生成空移'}).innerText(),/已过期/);
  assert.equal(await page.locator('.icax-three-specification-label').first().evaluate(n=>getComputedStyle(n).backgroundColor),'rgb(153, 153, 153)');
  assert.ok(await page.evaluate(()=>fixture.view.fiveGeneratedPreview.group.children.filter(o=>o.isLine).every(o=>o.material.color.getHex()===0x9a9a9a||o.material.color.getHex()===0x999999)));
  await rpc({action:'undo'});await command('five.refresh');assert.deepEqual(geometry(await read()),result);
  // Executor-owned compensation must not silently become CAM compensation.
  await patch(closed[1].id,{kerf:{widthMm:.2,owner:'Executor'}});await command('five.refresh');await command('five.tool.generate');await idle();
  assert.match(await page.evaluate(()=>fixture.view.error),/尚不支持执行设备补偿/);assert.deepEqual(geometry(await read()).resource,result.resource);
  await rpc({action:'undo'});await command('five.refresh');
  // Lost acceptance: keep the same command, preserve DOM and latest scroll.
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.five-position-body::after{content:"";display:block;height:1200px}.five-position-body{height:150px;overflow:auto}.cam-info-pane{max-height:180px!important}'});
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.Generate';fixture.hold=true;fixture.loseMethod='CamPlan.Generate';fixture.genButton=document.querySelector('[data-five-command="five.generate.apply"]');});
  const count=calls.filter(c=>c.method==='CamPlan.Generate').length;
  await page.locator('[data-five-command="five.generate.apply"]').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')];nodes.forEach((n,i)=>n.scrollTop=23+i*6);return nodes.map(n=>n.scrollTop);});
  assert.ok(scroll.every(v=>v>0));await page.evaluate(()=>fixture.release());await idle();
  assert.match(await page.evaluate(()=>fixture.view.error),/尚未确认/);
  assert.equal(await page.evaluate(()=>fixture.genButton===document.querySelector('[data-five-command="five.generate.apply"]')&&fixture.generationCanvas===document.querySelector('canvas')),true);
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')].map(n=>n.scrollTop)),scroll);
  await stress.evaluate(n=>n.remove());await page.locator('[data-five-command="five.generate.apply"]').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');const retries=calls.filter(c=>c.method==='CamPlan.Generate').slice(count);assert.equal(retries.length,2);assert.deepEqual(retries[0].payload,retries[1].payload);
  // A lost polling reply must expose Retry even though the last known task was Running.
  await page.evaluate(()=>{const proxy=fixture.context.sceneProxy,original=proxy.invoke.bind(proxy);proxy.invoke=async(method,payload)=>{
    const result=await original(method,payload);if(method==='CamPlan.QueryStatus'){proxy.invoke=original;throw Error('Injected lost task status');}return result;};});
  const pollingCount=calls.filter(c=>c.method==='CamPlan.Generate').length;
  await command('five.tool.generate');await idle();assert.match(await page.evaluate(()=>fixture.view.error),/尚未确认/);
  assert.equal(await page.locator('[data-five-command="five.generate.apply"]').isVisible(),true);
  await page.locator('[data-five-command="five.generate.apply"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const pollRetries=calls.filter(c=>c.method==='CamPlan.Generate').slice(pollingCount);assert.equal(pollRetries.length,2);assert.deepEqual(pollRetries[0].payload,pollRetries[1].payload);
  // A late result must not steal focus after navigating to another page.
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.Generate';fixture.hold=true;});
  await page.locator('[data-five-command="five.generate.apply"]').click();await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
  const search=page.locator('[data-five-search]');await search.fill('focus');await search.evaluate(n=>{n.focus();n.setSelectionRange(1,3);});
  await page.evaluate(()=>fixture.release());await idle();
  assert.deepEqual(await search.evaluate(n=>[document.activeElement===n,n.selectionStart,n.selectionEnd]),[true,1,3]);
  const saved=geometry(await read()),reopened=await rpc({action:'save-reopen'});assert.deepEqual(geometry(reopened),saved);
  await rpc({action:'export-project',path:resolve(output,'Automotive-generated-toolpaths.i5cam')});
  await remount();await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.refresh');
  assert.equal(await page.evaluate(()=>!!fixture.view.fiveGeneratedPreview?.group),true);
  await page.evaluate(() => fixture.view.viewport.fitView());await page.screenshot({path:resolve(output,'automotive-generated-reopened.png')});
  writeFileSync(resolve(output,'generation-report.json'),JSON.stringify({passed:true,cases:['one-click generation uses all enabled definitions','exact spatial curve resources and support normals','shared leads gap and overcut endpoints','deterministic internal-before-outer ordering','automatic laser-off rapid geometry with unconfigured-envelope gate','source data unchanged','stale results and undo','unsupported CRC preserves prior result','lost acceptance retries same task','canvas controls dual nested scroll preserved','late result preserves focus and selection','embedded save and reopen'],coverage:{native:true,browser:'Edge',standaloneCef:false,machineMotion:false,collision:false},operationCount:ids.length,transitionCount:ids.length-1},null,2));
}
