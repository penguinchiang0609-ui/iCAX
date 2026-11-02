import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyCutClearing({page,rpc,calls,output}){
  console.log('Clear: automotive spatial and split contours');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const button=id=>page.locator(`[data-five-command="five.clear.${id}"]`).last();
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const all=['Leads','Overcut','Gap','Kerf'];
  const policies=async ids=>(await invoke('CamProcess.Read',{expected:(await read()).expected,intentIds:ids})).items;
  const patch=async(ids,patch)=>invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:(await read()).expected,intentIds:ids,patch});
  const categories=async chosen=>{for(const id of all)await button(`category.${id}`).setChecked(chosen.includes(id));};
  const select=async ids=>{
    await page.locator('[data-five-clear-select]').selectOption(ids[0]);
    for(const id of ids.slice(1))await page.locator(`[data-five-intent="${id}"]`).click({modifiers:['Control']});
  };
  const apply=async()=>{await button('apply').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  // The preceding reverse test reopens the repository; use the normal project
  // lifecycle so no runtime resource URLs from the old repository are reused.
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  await page.evaluate(async()=>{
    const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);
  });
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  const initial=await read(),definitions=(await invoke('CamCut.Read',{expected:initial.expected,intentIds:initial.intentToolpaths.map(c=>c.id)})).items;
  const closed=initial.intentToolpaths.filter(c=>c.cutType==='Contour'&&definitions.find(d=>d.intentId===c.id)?.closed).sort((a,b)=>b.previewPoints.length-a.previewPoints.length);
  const open=initial.intentToolpaths.find(c=>c.split&&!definitions.find(d=>d.intentId===c.id)?.closed);
  assert.ok(closed.length>=2&&open);const ids=[closed[0].id,closed[1].id,open.id];
  const none={kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0};
  await patch(ids,{kerf:{widthMm:.17},leadIn:none,leadOut:none,overcutLengthMm:0,gapLengthMm:0});
  await patch([ids[0]],{gapLengthMm:1,leadOut:{...none,kind:'Line',lengthMm:.02}});
  await patch([ids[1]],{overcutLengthMm:.5});
  await patch([ids[2]],{leadIn:{...none,kind:'Line',lengthMm:.02}});
  await command('five.refresh');await command('five.tool.clear');
  const before=await read(),original=await policies(ids);
  assert.equal(await page.getByRole('dialog',{name:'清除工艺'}).count(),1);
  assert.equal(await page.locator('[data-five-command="five.process.compile"]').count(),0);
  assert.equal(await page.getByText('恢复继承值',{exact:true}).count(),0);
  await button('all').check();assert.equal(await page.locator('[data-five-intent][aria-pressed="true"]').count(),initial.intentToolpaths.length);
  await button('all').uncheck();assert.equal(await button('apply').isDisabled(),true);
  await select(ids);await categories([]);assert.equal(await button('apply').isDisabled(),true);
  await button('close').click();assert.deepEqual(await read(),before,'opening/selection/cancel cannot clear process');
  await command('five.tool.clear');await categories(['Leads']);
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({path:resolve(output,'automotive-clear-dialog.png')});
  await apply();const afterLeads=await read(),leadPolicies=await policies(ids);
  for(let i=0;i<ids.length;++i){const expected=structuredClone(original[i].assignment);expected.leadIn=none;expected.leadOut=none;assert.deepEqual(leadPolicies[i].assignment,expected);}
  await rpc({action:'undo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);
  await rpc({action:'redo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,afterLeads.intentToolpaths);
  await command('five.tool.clear');await select(ids);await categories(['Overcut']);await apply();
  const beforeLast=await read(),afterOvercut=await policies(ids);
  assert.equal(afterOvercut[0].assignment.gapLengthMm,1);assert.equal(afterOvercut[1].assignment.overcutLengthMm,0);
  // Preserve the latest scroll positions while the mutation and receipt replies
  // are both lost. The retry uses the same immutable selection/categories.
  await command('five.tool.clear');await categories(['Gap','Kerf']);
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.five-position-body::after{content:"";display:block;height:1200px}.five-position-body{height:190px;overflow:auto}.cam-info-pane{max-height:200px!important}'});
  const prior=calls.filter(c=>c.method==='CamProcess.Clear').length;
  await page.evaluate(()=>{
    fixture.clearCanvas=document.querySelector('canvas');fixture.clearSelect=document.querySelector('[data-five-clear-select]');
    fixture.holdMethod='CamProcess.Clear';fixture.hold=true;fixture.loseMethod='CamProcess.Clear';
    const proxy=fixture.context.sceneProxy,original=proxy.invoke.bind(proxy);
    proxy.invoke=async(method,payload)=>{if(method==='CamScene.QueryCommand'){proxy.invoke=original;throw Error('Injected lost clear receipt');}return original(method,payload);};
  });
  await button('apply').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')];nodes.forEach((n,i)=>n.scrollTop=20+i*7);return nodes.map(n=>n.scrollTop);});
  assert.ok(scroll.every(n=>n>0));await page.evaluate(()=>fixture.release());await idle();
  assert.match(await page.evaluate(()=>fixture.view.error),/尚未确认/);
  assert.equal(await button('close').isDisabled(),true);assert.equal(await button('category.Leads').isDisabled(),true);
  assert.equal(await page.evaluate(()=>fixture.clearCanvas===document.querySelector('canvas')&&fixture.clearSelect===document.querySelector('[data-five-clear-select]')),true);
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')].map(n=>n.scrollTop)),scroll);
  await stress.evaluate(n=>n.remove());await apply();
  const requests=calls.filter(c=>c.method==='CamProcess.Clear').slice(prior);assert.equal(requests.length,2);assert.deepEqual(requests[0].payload,requests[1].payload);
  const cleared=await read(),result=await policies(ids);
  for(const policy of result){assert.deepEqual(policy.assignment.leadIn,none);assert.deepEqual(policy.assignment.leadOut,none);assert.equal(policy.assignment.overcutLengthMm,0);assert.equal(policy.assignment.gapLengthMm,0);assert.equal(policy.assignment.kerf.widthMm,0);}
  for(const a of before.intentToolpaths){const b=cleared.intentToolpaths.find(c=>c.id===a.id);
    if(!ids.includes(a.id)){assert.deepEqual(b,a);continue;}
    for(const key of ['curveRef','featureRef','poseRef','sourceKey','sourceRecipeRef','start','startPoint','reversed','retainedSide','cutType'])assert.deepEqual(b[key],a[key],key);
  }
  await rpc({action:'undo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,beforeLast.intentToolpaths);
  await rpc({action:'redo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,cleared.intentToolpaths);
  // Clearing empty categories is a true no-op, including the revision fence.
  const emptyBefore=await read();await command('five.tool.clear');await apply();assert.deepEqual(await read(),emptyBefore);
  // Reopening the lead editor confirms the actual persisted result.
  await command('five.tool.leads');await page.locator(`[data-five-intent="${ids[0]}"]`).click();
  await page.waitForFunction(()=>fixture.view.fiveLeads?.loaded&&!fixture.view.fiveLeads.previewBusy);
  assert.equal(await page.locator('[data-five-lead-field="entry.kind"]').inputValue(),'None');
  assert.equal(await page.locator('[data-five-lead-field="exit.kind"]').inputValue(),'None');
  assert.equal(await page.locator('[data-five-lead-field="closure.kind"]').inputValue(),'Normal');
  assert.ok(await page.evaluate(()=>fixture.view.fiveLeads.preview.items[0].leads.length===0));
  await page.screenshot({path:resolve(output,'automotive-cleared-leads.png')});
  await page.locator('[data-five-command="five.leads.close"]').last().click();
  // A late clear response updates the current page without restoring its old UI.
  await patch([ids[0]],{gapLengthMm:.3});await command('five.refresh');await command('five.tool.clear');
  await page.evaluate(()=>{fixture.holdMethod='CamProcess.Clear';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.clear.apply');});
  await page.waitForFunction(()=>fixture.held);await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
  await page.locator('[data-five-search]').fill('保留当前输入');await page.evaluate(()=>document.querySelector('[data-five-search]').setSelectionRange(1,4));
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.activeAreaId),'help');assert.equal(await page.locator('[data-five-search]').inputValue(),'保留当前输入');
  assert.equal(await page.evaluate(()=>document.activeElement?.hasAttribute('data-five-search')),true);
  assert.deepEqual(await page.evaluate(()=>[document.activeElement.selectionStart,document.activeElement.selectionEnd]),[1,4]);
  const saved=await read(),savedPolicies=await policies(ids),reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.intentToolpaths,saved.intentToolpaths);assert.deepEqual(await policies(ids),savedPolicies);
  writeFileSync(resolve(output,'clear-report.json'),JSON.stringify({passed:true,ids,cases:['compact categories and selection/all/cancel','closed spatial and split open curves in one batch','selective clearing leaves other categories intact','start direction retained side source and pose preserved','one-step undo redo','lost mutation and receipt retry once','no-op keeps revision and references','canvas controls latest dual and nested scroll preserved','saved lead editor shows actual cleared values','cross-page focus and selection preserved','embedded save and reopen'],coverage:{native:true,browser:'Edge',standaloneCef:false}},null,2));
}
