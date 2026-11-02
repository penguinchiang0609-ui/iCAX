import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {writeFileSync} from 'node:fs';

export async function verifyCutGroups({page,rpc,output}){
  const cases=[],invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveGeneration?.running);
  const refresh=async()=>{await command('five.refresh');await idle();};
  const click=async action=>{await page.locator(`[data-five-command="five.tree.${action}"]`).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const row=id=>page.locator(`[data-five-intent="${id}"]`);
  const select=async ids=>{await row(ids[0]).click();for(const id of ids.slice(1))await row(id).click({modifiers:['Control']});};
  const selected=()=>page.evaluate(()=>fixture.view.fiveAxisUI.selectedIntentIds);
  const treeMutation=async(action,extra)=>{const s=await read();return invoke('CamCut.ManageTree',{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,intentIds:[],action,...extra});};
  await command('five.tool.extract');await command('five.extraction.close');
  assert.deepEqual(await page.getByRole('toolbar',{name:'刀路管理'}).getByRole('button').evaluateAll(ns=>ns.map(n=>n.getAttribute('aria-label'))),['新增','删除','成组','解组','上移','下移','手动排序','自动排序']);
  const initial=await read(),ids=setup(initial).definitionOrder;assert.ok(ids.length>5);
  const members=[ids[1],ids[3],ids[4]];
  await select(members);
  assert.equal(await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='toolpath').groups[0].commands.find(c=>c.id==='five.tree.Group').disabled),false);
  await command('five.tree.Group');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  let s=await read(),group=setup(s).cutGroups[0],groupId=group.id;
  assert.deepEqual(s.intentToolpaths.filter(c=>c.groupId===groupId).map(c=>c.id).sort(),[...members].sort());
  const grouped=[ids[0],...members,ids[2],...ids.slice(5)];assert.deepEqual(setup(s).definitionOrder,grouped);
  const groupRow=()=>page.locator(`[data-five-group="${groupId}"]`);
  await groupRow().click();await click('Up');assert.deepEqual(setup(await read()).definitionOrder,[...members,ids[0],ids[2],...ids.slice(5)]);
  await click('Down');assert.deepEqual(setup(await read()).definitionOrder,grouped);
  await click(`Collapse:${groupId}`);assert.equal(await row(members[0]).isVisible(),false);
  await click(`Collapse:${groupId}`);assert.equal(await row(members[0]).isVisible(),true);
  cases.push('non-adjacent multiple selection forms a contiguous group, collapse and whole-group movement');
  // Splitting a grouped source must retain its parent and insert the new fragments beside it.
  s=await read();const splitCut=s.intentToolpaths.find(c=>c.id===members[0]);
  const resolved=await invoke('CamCut.ResolveSplitPoint',{expected:s.expected,intentId:splitCut.id,nearPoint:splitCut.previewPoints[Math.floor(splitCut.previewPoints.length/2)],maximumDistanceMm:1,mode:'ClosestPoint',frame:'Workpiece'});
  const split=await invoke('CamCut.Split',{commandId:crypto.randomUUID(),expected:s.expected,cuts:[{intentId:splitCut.id,points:[resolved.anchor]}]});
  s=await read();assert.ok(split.intentIds.length>=2);assert.ok(split.intentIds.every(id=>s.intentToolpaths.find(c=>c.id===id).groupId===groupId));
  assert.deepEqual(setup(s).definitionOrder.slice(1,1+split.intentIds.length),split.intentIds);
  await rpc({action:'undo'});await refresh();assert.deepEqual(setup(await read()).definitionOrder,grouped);
  cases.push('split fragments inherit their group and remain contiguous; undo restores the group');


  await click(`ToggleEnabled:${members[1]}`);
  await click(`ToggleGroupEnabled:${groupId}`);s=await read();assert.ok(s.intentToolpaths.filter(c=>c.groupId===groupId).every(c=>!c.enabled));
  await click(`ToggleGroupEnabled:${groupId}`);s=await read();assert.equal(s.intentToolpaths.find(c=>c.id===members[1]).enabled,false);
  assert.ok(s.intentToolpaths.filter(c=>c.groupId===groupId&&c.id!==members[1]).every(c=>c.enabled));
  await click(`ToggleEnabled:${members[1]}`);
  cases.push('group enable inherits without overwriting the individual eye state');

  await select([members[0]]);await click(`ToggleLocked:${members[0]}`);
  assert.equal(await row(members[0]).isDisabled(),true);assert.ok(!(await selected()).includes(members[0]));
  await assert.rejects(treeMutation('Delete',{intentIds:[members[0]]}),/CAM_CUT_LOCKED/);
  await assert.rejects(treeMutation('Up',{intentIds:[members[0]]}),/CAM_CUT_LOCKED/);
  s=await read();await assert.rejects(invoke('CamCut.Reverse',{commandId:crypto.randomUUID(),expected:s.expected,intentIds:[members[0]]}),/CAM_CUT_LOCKED/);
  await assert.rejects(invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:s.expected,intentIds:[members[0]],patch:{overcutLengthMm:0}}),/CAM_CUT_LOCKED/);
  await click(`ToggleLocked:${members[0]}`);
  await groupRow().click();await click(`ToggleGroupLocked:${groupId}`);
  assert.equal(await groupRow().isDisabled(),true);assert.deepEqual(await selected(),[]);
  for(const id of members)assert.equal(await row(id).isDisabled(),true);
  await assert.rejects(treeMutation('Unlock',{intentIds:[members[0]]}),/CAM_CUT_GROUP_LOCKED/);
  await assert.rejects(treeMutation('Disable',{groupId}),/CAM_CUT_LOCKED/);
  await select([ids[0]]);assert.equal(await page.locator('[data-five-command="five.tree.Down"]').isDisabled(),true);
  cases.push('individual and group lock clear selection; native delete, edit, reorder and inherited unlock are rejected');

  // Real pointer events: click a locked curve, then box-select the whole viewport.
  await page.evaluate(()=>fixture.view.viewport.fitView());
  const point=await page.evaluate(async id=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const view=fixture.view,v=view.viewport,cut=view.scene.intentToolpaths.find(c=>c.id===id),placement=view.scene.domain.placements.find(p=>p.id===cut.placementId);
    const p=new THREE.Vector3(...cut.previewPoints[2]).applyMatrix4(new THREE.Matrix4().set(...placement.transform)).project(v.camera),b=v.renderer.domElement.getBoundingClientRect();
    return{x:b.x+(p.x+1)*b.width/2,y:b.y+(1-p.y)*b.height/2};
  },members[0]);
  await page.mouse.click(point.x,point.y);assert.ok((await selected()).every(id=>!members.includes(id)));
  const box=await page.evaluate(()=>fixture.view.viewport.renderer.domElement.getBoundingClientRect().toJSON());
  await page.mouse.move(box.x+3,box.y+75);await page.mouse.down();await page.mouse.move(box.right-3,box.bottom-60,{steps:9});await page.mouse.up();await idle();
  assert.ok((await selected()).length>0);assert.ok((await selected()).every(id=>!members.includes(id)));
  const reversedBefore=await read();await command('five.tool.reverse');await idle();assert.equal(await page.getByRole('dialog',{name:'切割方向'}).count(),0);assert.ok((await selected()).every(id=>!members.includes(id)));await rpc({action:'undo'});await refresh();assert.deepEqual((await read()).intentToolpaths,reversedBefore.intentToolpaths);
  cases.push('locked members excluded from actual viewport clicks, rectangle selection and select-all');

  await click(`ToggleGroupLocked:${groupId}`);
  await click(`ToggleGroupEnabled:${groupId}`);
  await command('five.tool.generate');await idle();s=await read();assert.equal(setup(s).generatedGeometry.status,'Current');
  assert.ok(setup(s).generatedGeometry.operations.every(o=>!members.includes(o.intentId)));
  await command('five.tool.extract');await command('five.extraction.close');
  await click(`ToggleGroupEnabled:${groupId}`);await click(`ToggleGroupLocked:${groupId}`);
  await command('five.tool.generate');await idle();s=await read();assert.equal(setup(s).generatedGeometry.status,'Current');
  assert.ok(members.every(id=>setup(s).generatedGeometry.operations.some(o=>o.intentId===id)));
  const beforeOrder=setup(s).definitionOrder;
  await invoke('CamPlan.SetOrder',{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,mode:'Automatic'});
  s=await read();for(const id of members)assert.equal(setup(s).definitionOrder.indexOf(id),beforeOrder.indexOf(id));
  cases.push('disabled groups omitted from generation, locked groups still generated, automatic sorting preserves locked group');
  await refresh();await command('five.tool.manual-order');
  await row(ids[5]).click();
  const manual=await page.evaluate(async()=>{const m=await import('/src/apps/five-axis-cam/webpage/toolpathOrdering.mjs');const v=fixture.view,s=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId);return m.orderingPreview(v,s.generatedGeometry).operations.map(o=>o.intentId);});
  s=await read();for(const id of members)assert.equal(manual.indexOf(id),setup(s).generatedGeometry.operations.findIndex(o=>o.intentId===id));
  await invoke('CamPlan.SetOrder',{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,mode:'Manual',intentIds:manual});
  await page.locator('[data-five-command="five.order.close"]').first().click();
  cases.push('manual ordering preview and native order agree while respecting group and lock constraints');


  await refresh();await command('five.tool.extract');await command('five.extraction.close');
  await click(`ToggleGroupLocked:${groupId}`);await groupRow().click();
  await command('five.tree.Ungroup');await idle();assert.equal(setup(await read()).cutGroups.length,0);
  await rpc({action:'undo'});await refresh();assert.equal(setup(await read()).cutGroups.length,1);
  await rpc({action:'redo'});await refresh();assert.equal(setup(await read()).cutGroups.length,0);
  await rpc({action:'undo'});await refresh();await groupRow().click();
  await command('five.tree.Delete');await idle();assert.equal((await read()).intentToolpaths.length,ids.length-members.length);
  assert.equal(setup(await read()).cutGroups.length,0);
  await rpc({action:'undo'});await refresh();assert.equal((await read()).intentToolpaths.length,ids.length);
  cases.push('ungroup and group delete are atomic undo/redo operations');

  // Keep both panes, a nested scroller, input caret and canvas stable during a lost response.
  await select([ids[0]]);await command('five.tool.compensation');await idle();
  const input=page.locator('[data-five-field="cutKerf"]');await input.fill('0.12345');
  const style=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.cam-info-pane .five-section::after{content:"";display:block;height:1200px}.cam-info-pane .five-section{height:170px;overflow:auto}.cam-info-pane{max-height:450px!important}'});
  await page.evaluate(()=>{fixture.holdMethod='CamCut.ManageTree';fixture.hold=true;fixture.loseMethod='CamCut.ManageTree';fixture.treeInput=document.querySelector('[data-five-field="cutKerf"]');fixture.treeCanvas=fixture.view.viewport.renderer.domElement;fixture.treeRow=document.querySelector('[data-five-intent]');});
  await page.locator(`[data-five-command="five.tree.ToggleEnabled:${ids[0]}"]`).click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const n=fixture.treeInput;n.focus({preventScroll:true});n.setSelectionRange(2,5);const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.cam-info-pane .five-section')];nodes.forEach((n,i)=>n.scrollTop=40+i*9);return nodes.map(n=>n.scrollTop);});
  assert.ok(scroll.every(x=>x>0));await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.cam-info-pane .five-section')].map(n=>n.scrollTop)),scroll);
  assert.deepEqual(await input.evaluate(n=>[n===fixture.treeInput,document.activeElement===n,n.value,n.selectionStart,n.selectionEnd]),[true,true,'0.12345',2,5]);
  assert.equal(await page.evaluate(()=>fixture.treeCanvas===fixture.view.viewport.renderer.domElement&&fixture.treeRow.isConnected),true);
  await style.evaluate(n=>n.remove());
  cases.push('lost receipt recovery retains canvas, row, latest pane and nested scroll, input node, focus and text selection');

  await command('five.tool.extract');await command('five.extraction.close');
  await click(`ToggleGroupLocked:${groupId}`);const beforeSave=await read();
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));await rpc({action:'save-reopen'});
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await idle();await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount===1);
  await command('five.tool.extract');await command('five.extraction.close');s=await read();
  assert.deepEqual(setup(s).cutGroups,setup(beforeSave).cutGroups);assert.deepEqual(s.intentToolpaths,beforeSave.intentToolpaths);
  assert.deepEqual(setup(s).definitionOrder,setup(beforeSave).definitionOrder);
  await page.locator('.cam-context-pane').evaluate(n=>n.scrollTop=0);
  await page.evaluate(()=>fixture.view.viewport.fitView());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  await page.screenshot({path:resolve(output,'cut-groups.png')});
  cases.push('native save/reopen preserves group membership, group and item flags, order and resources');
  writeFileSync(resolve(output,'cut-groups-report.json'),JSON.stringify({passed:true,cases,coverage:{native:true,browser:'Edge real pointer events',standaloneCef:false}},null,2));
}
