import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {hasRapidGeometry} from '../../apps/five-axis-cam/webpage/rapidGeometry.mjs';

export async function verifyRapid206({page,command,read,rpc,output,info}){
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const geom=s=>s.setups.find(t=>t.id===s.activeSetupId).generatedGeometry;
  const field=name=>page.locator(`[data-five-rapid-field="${name}"]`).last();
  const button=name=>page.locator(`[data-five-command="five.rapid.${name}"]`).last();
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveRapid?.running);
  const wait=async task=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,25));task=await invoke('CamRapid.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
  const mutate=async(method,extra)=>{const s=await read();return invoke(method,{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,...extra});};
  const before=await read(),g=geom(before);assert.equal(g.operations.length,8);
  const selected=g.transitions[6],pair=`${selected.from}/${selected.to}`;
  // Actual production rendering resource via a one-setup native View fixture.
  const refreshMesh=()=>page.evaluate(async()=>{
    const {RENDER_ENTITY_VIEW_PROJECTION}=await import('/src/apps/_shared/workbench/projection.mjs'),proxy=fixture.context.sceneProxy;
    await fixture.rapidReader?.stop();
    fixture.rapidReader=await proxy.views.start({sources:[{sourceId:'rapid-206',role:'scene',language:'sql',where:'WHERE HAS CRenderInstanceComponent',projection:RENDER_ENTITY_VIEW_PROJECTION}]});
    await fixture.view.viewport.applyViewSnapshot(await fixture.rapidReader.poll(),proxy.resources);
  });
  await refreshMesh();
  await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size>0);
  await command('five.tool.rapid');await field('connection').selectOption(pair);await field('strategy').selectOption('Frog');await field('liftHeightMm').fill('10');
  const preview=async()=>{const at=performance.now();await button('preview').click();await idle();return {ms:performance.now()-at,...await page.evaluate(()=>fixture.view.fiveRapid.preview)};};
  const direct=await preview();assert.equal(direct.result.validation,'Clear');assert.equal(direct.result.geometryStatus,'Generated');assert.equal(direct.policy.strategy,'Frog');assert.equal(direct.policy.liftHeightMm,10);assert.equal(Math.max(...direct.result.heightsMm),11);
  await button('apply').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  let saved=geom(await read()).transitions[6];assert.equal(saved.policy.strategy,'Frog');assert.equal(saved.policy.liftHeightMm,10);assert.equal(saved.validation,'Clear');
  assert.deepEqual(geom(await read()).operations,g.operations);
  assert.match(await page.locator(`[data-five-rapid-id="${pair}"]`).innerText(),/已避障/);
  await command('view.fit');await page.screenshot({path:resolve(output,'206-7-to-8-frog10.png')});
  await rpc({action:'export-project',path:resolve(output,'206-7-to-8-verified.i5cam')});

  // Latest focus, selection, nodes and dual/nested scrolling survive a result.
  const style=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-rapid-body::after{content:"";display:block;height:1400px}.cam-info-pane{max-height:260px!important}.five-rapid-body{height:180px;overflow:auto}'});
  await page.evaluate(()=>{fixture.rapidInput=document.querySelector('[data-five-rapid-field="marginMm"]');fixture.rapidCanvas=document.querySelector('canvas');fixture.holdMethod='CamRapid.Preview';fixture.hold=true;fixture.rapidEvents=0;fixture.rapidInput.addEventListener('input',()=>fixture.rapidEvents++);});
  await button('preview').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{fixture.rapidInput.focus({preventScroll:true});fixture.rapidInput.setSelectionRange(1,3);return ['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=50+i*20;return n.scrollTop;});});assert.ok(scroll.every(n=>n>0));
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.rapidCanvas===document.querySelector('canvas')&&fixture.rapidInput===document.querySelector('[data-five-rapid-field="marginMm"]')&&document.activeElement===fixture.rapidInput),true);
  assert.deepEqual(await field('marginMm').evaluate(n=>[n.selectionStart,n.selectionEnd]),[1,3]);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop)),scroll);
  await style.evaluate(n=>n.remove());await field('marginMm').fill('0.25');assert.ok(await page.evaluate(()=>fixture.rapidEvents>0));assert.equal(await button('apply').isDisabled(),true);await field('marginMm').fill('0.2');

  // Insert a real, thin BRep obstacle across this same connection, keeping
  // its endpoints free. Source STEP, cut order and pose fields stay unchanged.
  const points=direct.result.points,start=points[0],end=points.at(-1),delta=end.map((v,i)=>v-start[i]),length=Math.hypot(delta[0],delta[1]),x=delta[0]/length,y=delta[1]/length,mid=start.map((v,i)=>(v+end[i])/2);
  const imported=await wait(await mutate('CamFixture.Import',{sourcePath:info.rapidFixturePath})),fixtureId=imported.result.fixtureId;
  const transform=[x,-y,0,mid[0],y,x,0,mid[1],0,0,1,14,0,0,0,1];
  await mutate('CamFixture.Update',{fixtureId,transform});await wait(await mutate('CamPlan.Generate',{}));await command('five.refresh');await command('five.tool.rapid');await field('connection').selectOption(pair);
  await field('strategy').selectOption('Frog');await field('liftHeightMm').fill('10');const detour=await preview();
  assert.equal(detour.result.validation,'Clear',JSON.stringify(detour.result));assert.equal(detour.result.planningMethod,'LateralDetour');assert.ok(Number(detour.result.candidateCount)>1);assert.equal(Math.max(...detour.result.heightsMm),11);assert.notDeepEqual(detour.result.points,direct.result.points);assert.deepEqual(detour.result.points[0],start);assert.deepEqual(detour.result.points.at(-1),end);
  assert.equal(await field('strategy').inputValue(),'Frog');assert.equal(await field('liftHeightMm').inputValue(),'10');await button('apply').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  saved=geom(await read()).transitions[6];assert.equal(saved.planningMethod,'LateralDetour');
  await rpc({action:'export-project',path:resolve(output,'206-7-to-8-obstacle-detour.i5cam')});
  await refreshMesh();assert.equal(await page.evaluate(id=>fixture.view.viewport.sceneObjects.get(id)?.isMesh,fixtureId),true);
  await page.screenshot({path:resolve(output,'206-7-to-8-detour.png')});
  const persisted=await read();await page.evaluate(async()=>{await Promise.all([...fixture.context.sceneProxy.views.readers].map(r=>r.stop()));});const reopened=await rpc({action:'save-reopen'});
  const stable=g=>{const {sampleGeometryRef,...saved}=g;return saved;};assert.deepEqual(stable(geom(reopened)),stable(geom(persisted)));
  await command('five.refresh');

  // Move that obstacle onto the departure nozzle. The failed candidate stays
  // editable, while its failed check cannot be adopted as a cleared result.
  transform[3]=start[0];transform[7]=start[1];transform[11]=start[2];
  await mutate('CamFixture.Update',{fixtureId,transform});await wait(await mutate('CamPlan.Generate',{}));await command('five.refresh');await command('five.tool.rapid');await field('connection').selectOption(pair);await field('strategy').selectOption('Frog');await field('liftHeightMm').fill('10');
  const failed=await preview();assert.equal(failed.result.geometryStatus,'Failed');assert.equal(failed.result.reason,'RAPID_ENDPOINT_BLOCKED');assert.ok(hasRapidGeometry(failed.result),'blocked endpoint retains the original editable setup-space ToolTip candidate');assert.notEqual(failed.result.validation,'Clear');
  assert.deepEqual(failed.result.points[0],start);assert.deepEqual(failed.result.points.at(-1),end);assert.deepEqual(failed.result.directions[0],direct.result.directions[0]);assert.deepEqual(failed.result.directions.at(-1),direct.result.directions.at(-1));
  assert.equal(await button('apply').isDisabled(),true);assert.match(await page.locator('[role="alert"]').last().innerText(),/起止姿态.*间隙/);assert.equal(await field('strategy').inputValue(),'Frog');assert.equal(await field('liftHeightMm').inputValue(),'10');
  await button('path-edit').click();await idle();const editing=await page.evaluate(async()=>{const {manualRapidDraft}=await import('/src/apps/five-axis-cam/webpage/rapidPathEditing.mjs'),v=fixture.view;return {points:v.fiveRapid.pathEdit.points,directions:v.fiveRapid.pathEdit.directions,draft:manualRapidDraft(v),lines:v.fiveGeneratedPreview.group.children.filter(o=>o.userData.rapidTransitionId===v.fiveRapid.id).map(o=>({color:o.material.color.getHex(),unverified:o.userData.rapidUnverified}))};});
  assert.deepEqual(editing.points[0],start);assert.deepEqual(editing.points.at(-1),end);assert.equal(editing.draft.geometryStatus,'Failed');assert.notEqual(editing.draft.validation,'Clear');assert.ok(editing.lines.some(line=>line.unverified&&[0xff862b,0xe65b59].includes(line.color)));assert.equal(await button('apply').isDisabled(),true);
  await refreshMesh();
  await page.screenshot({path:resolve(output,'206-7-to-8-failed-explicit.png')});
  const report={passed:true,source:'TestData/FA/刀路/刀路/整理测试刀路/多种斜孔-多种边-206.STEP',cuttingPaths:8,connection:'7 → 8',policy:{strategy:'Frog',liftHeightMm:10,marginMm:.2},direct,detour,failed,cases:['real native scene and geometry','selected connection preview/save','independent constrained generation','thin BRep fixture forces lateral detour at identical lift','blocked endpoint preserves a warning candidate that can be edited without adopting its failed check','source cutting geometry unchanged by planning','latest focus selection scroll and DOM nodes preserved','embedded save reopen'],coverage:{native:true,browser:'Edge',nativeView:'single setup CRenderInstanceComponent',standaloneCef:false,screenshotProjectOrder:'top face extraction defaults'}};
  writeFileSync(resolve(output,'206-browser-report.json'),JSON.stringify(report,null,2));console.log('206 rapid passed:',JSON.stringify({directMs:direct.ms,detourMs:detour.ms,candidates:detour.result.candidateCount,failed:failed.result.reason}));
}
