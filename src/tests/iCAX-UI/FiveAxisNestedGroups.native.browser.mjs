import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

export async function verifyNestedGroups({page,rpc,calls,output}){
  page.setDefaultTimeout(30000);
  const cases=[],invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const refresh=async()=>{await command('five.refresh');await idle();};
  const row=id=>page.locator(`[data-five-intent="${id}"]`),group=id=>page.locator(`[data-five-group="${id}"]`),check=id=>page.locator(`[data-five-check="g:${id}"]`);
  const drag=async(source,target,top=false)=>{await source.scrollIntoViewIfNeeded();const a=await source.boundingBox();await page.mouse.move(a.x+a.width/2,a.y+a.height/2);await page.mouse.down();await page.mouse.move(a.x+a.width/2+8,a.y+a.height/2+3,{steps:4});await target.scrollIntoViewIfNeeded();const b=await target.boundingBox(),x=b.x+Math.min(30,b.width/2),y=b.y+(top?2:b.height/2);await page.mouse.move(x,y,{steps:8});await page.mouse.move(x+1,y);await page.mouse.up();await idle();};
  const selection=()=>page.evaluate(()=>fixture.view.fiveAxisUI.selectedIntentIds);
  const click=async action=>{await page.locator(`[data-five-command="five.tree.${action}"]`).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const mutate=async(action,extra={})=>{const s=await read();return invoke('CamCut.ManageTree',{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,intentIds:[],groupIds:[],action,...extra});};
  await command('five.extraction.close');const initial=await read(),ids=setup(initial).definitionOrder;
  async function makeGroup(members){await row(members[0]).click();for(const id of members.slice(1))await row(id).click({modifiers:['Control']});await command('five.tree.Group');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');return (await page.evaluate(()=>fixture.view.fiveAxisUI.selectedGroupIds))[0];}
  const a=await makeGroup(ids.slice(0,2)),b=await makeGroup(ids.slice(2,4));
  await group(a).click();await check(b).click();assert.equal((await selection()).length,4);assert.equal(await check(a).getAttribute('aria-checked'),'true');assert.equal(await check(b).getAttribute('aria-checked'),'true');
  await command('five.tree.Group');await idle();let s=await read(),parent=setup(s).cutGroups.find(g=>g.id!==a&&g.id!==b).id;
  assert.ok(setup(s).cutGroups.filter(g=>[a,b].includes(g.id)).every(g=>g.parentId===parent));
  assert.equal(await page.locator(`[data-five-key="children-${parent}"] [data-five-group]`).count(),2);
  await row(ids[0]).click();assert.equal(await check(parent).getAttribute('aria-checked'),'mixed');
  cases.push('real group checkboxes, multiple group selection, nested grouping and partial state');
  // Real HTML5 drag events driven by the mouse through Playwright.
  await row(ids[4]).dragTo(group(a));await idle();s=await read();assert.equal(s.intentToolpaths.find(c=>c.id===ids[4]).groupId,a);
  await group(b).dragTo(group(a));await idle();s=await read();assert.equal(setup(s).cutGroups.find(g=>g.id===b).parentId,a);
  const beforeCycle=s;await assert.rejects(mutate('Move',{groupIds:[parent],target:{kind:'Group',id:b,position:'Inside'}}),/CYCLE/);assert.deepEqual(await read(),beforeCycle);
  await drag(row(ids[4]),page.locator('[data-five-root-drop]'));s=await read();assert.equal(s.intentToolpaths.find(c=>c.id===ids[4]).groupId,'');assert.equal(setup(s).definitionOrder.at(-1),ids[4]);
  await drag(group(b),row(ids[5]),true);s=await read();assert.equal(setup(s).cutGroups.find(g=>g.id===b).parentId,'');
  assert.equal(setup(s).definitionOrder.indexOf(ids[3])+1,setup(s).definitionOrder.indexOf(ids[5]));
  cases.push('real dragging moves cuts and groups between levels, out of groups and before siblings; cycles rejected atomically');
  await click(`ToggleGroupEnabled:${parent}`);s=await read();assert.ok(ids.slice(0,2).every(id=>!s.intentToolpaths.find(c=>c.id===id).enabled));
  const drawn=await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.filter(c=>c.userData.cuttingLineId).map(c=>c.userData.cuttingLineId));assert.ok(ids.slice(0,2).every(id=>!drawn.includes(id)));
  await click(`ToggleGroupEnabled:${parent}`);await group(parent).click();await click(`ToggleGroupLocked:${parent}`);assert.deepEqual(await selection(),[]);assert.equal(await row(ids[0]).isDisabled(),true);assert.equal(await check(a).isDisabled(),true);
  await assert.rejects(mutate('Move',{intentIds:[ids[0]],target:{kind:'Root',id:'',position:'Inside'}}),/LOCKED/);
  await click(`ToggleGroupLocked:${parent}`);await row(ids[0]).click();
  cases.push('ancestor enable and lock affect all descendants, viewport visibility, picking, selection and native edits');
  // No reverse mode or dialog, just one atomic toggle of the selected paths.
  const beforeReverse=await read();await command('five.tool.reverse');await idle();s=await read();assert.equal(s.intentToolpaths.find(c=>c.id===ids[0]).reversed,!beforeReverse.intentToolpaths.find(c=>c.id===ids[0]).reversed);
  assert.equal(await page.getByRole('dialog',{name:'切割方向'}).count(),0);
  assert.equal(await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='toolpath').groups.flatMap(g=>g.commands).find(c=>c.id==='five.tool.reverse').active??false),false);
  await rpc({action:'undo'});await refresh();
  cases.push('reverse ribbon command executes immediately without dialog or active mode');
  const beforeText='  M10\n(custom <tag> & 中文)\n',afterText='M11\nG04 P1\n';
  const pre=page.locator('[data-five-cut-command="beforeCommands"]'),post=page.locator('[data-five-cut-command="afterCommands"]');
  await pre.fill(beforeText);await post.click();await idle();await post.fill(afterText);await row(ids[0]).click();await idle();s=await read();
  assert.equal(s.intentToolpaths.find(c=>c.id===ids[0]).beforeCommands,beforeText);assert.equal(s.intentToolpaths.find(c=>c.id===ids[0]).afterCommands,afterText);
  await rpc({action:'undo'});await refresh();assert.equal((await read()).intentToolpaths.find(c=>c.id===ids[0]).afterCommands,'');await rpc({action:'redo'});await refresh();assert.equal(await post.inputValue(),afterText);
  await pre.fill(beforeText+'temporary');await row(ids[1]).click();await idle();assert.equal((await selection())[0],ids[1]);assert.equal(await pre.inputValue(),'');await rpc({action:'undo'});await refresh();await row(ids[0]).click();assert.equal(await pre.inputValue(),beforeText);
  await pre.fill(beforeText+'save without blur');await page.evaluate(()=>fixture.entry.prepareProjectSave(fixture.context));assert.equal((await read()).intentToolpaths.find(c=>c.id===ids[0]).beforeCommands,beforeText+'save without blur');assert.equal(await pre.evaluate(n=>n===document.activeElement),true);await rpc({action:'undo'});await refresh();assert.equal(await pre.inputValue(),beforeText);
  const panes=await page.evaluate(()=>['.cam-context-pane','.cam-viewport','.cam-info-pane'].map(sel=>document.querySelector(sel).getBoundingClientRect().toJSON()));assert.ok(panes[0].right<=panes[1].x&&panes[1].right<=panes[2].x);
  // Keep latest focus/caret, both panes and a nested scroller through an async save.
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-cut-parameters::after{content:"";display:block;height:1200px}.five-cut-parameters{height:240px;overflow:auto}'});
  await page.evaluate(()=>{fixture.holdMethod='CamCut.SetCommands';fixture.hold=true;fixture.commandInput=document.querySelector('[data-five-cut-command="beforeCommands"]');fixture.commandCanvas=fixture.view.viewport.renderer.domElement;});
  await pre.fill(beforeText+'G90');await post.click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{fixture.commandInput.focus({preventScroll:true});fixture.commandInput.setSelectionRange(2,5);const n=['.cam-context-pane','.cam-info-pane','.five-cut-parameters'].map(s=>document.querySelector(s));n.forEach((n,i)=>n.scrollTop=20+i*10);return n.map(n=>n.scrollTop);});assert.ok(scroll.every(x=>x>0));
  await page.evaluate(()=>fixture.release());await idle();
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-cut-parameters'].map(s=>document.querySelector(s).scrollTop)),scroll);
  assert.deepEqual(await pre.evaluate(n=>[n===fixture.commandInput,n===document.activeElement,n.selectionStart,n.selectionEnd]),[true,true,2,5]);assert.equal(await page.evaluate(()=>fixture.commandCanvas===fixture.view.viewport.renderer.domElement),true);await stress.evaluate(n=>n.remove());
  await command('five.tool.generate');await idle();s=await read();const op=setup(s).generatedGeometry.operations.find(o=>o.intentId===ids[0]);assert.equal(op.beforeCommands,beforeText+'G90');assert.equal(op.afterCommands,afterText);
  await mutate('Disable',{intentIds:ids});await refresh();assert.equal(await page.evaluate(()=>fixture.view.fiveGeneratedPreview===null&&fixture.view.viewport.specificationLabels.length===0),true);await rpc({action:'undo'});await refresh();
  cases.push('three fixed panes and verbatim per-path commands persist into generated operations; asynchronous save preserves focus, caret, canvas and scrolling');
  await command('five.tool.extract');await command('five.extraction.close');await group(parent).click();await command('five.tree.Ungroup');await idle();s=await read();assert.ok(!setup(s).cutGroups.some(g=>g.id===parent));assert.equal(setup(s).cutGroups.find(g=>g.id===a).parentId,'');
  await rpc({action:'undo'});await refresh();s=await read();assert.equal(setup(s).cutGroups.find(g=>g.id===a).parentId,parent);
  await row(ids[0]).click();await page.screenshot({path:resolve(output,'nested-groups-commands.png')});
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));await rpc({action:'save-reopen'});const reopened=await read();assert.deepEqual(reopened.intentToolpaths,s.intentToolpaths);assert.deepEqual(setup(reopened).cutGroups,setup(s).cutGroups);assert.deepEqual(setup(reopened).definitionOrder,setup(s).definitionOrder);
  cases.push('nested ungroup and undo; native save/reopen preserves hierarchy, order, flags and user commands');
  writeFileSync(resolve(output,'nested-groups-report.json'),JSON.stringify({passed:true,cases,coverage:{native:true,browser:'Edge real pointer events',standaloneCef:false}},null,2));
}
