import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0];
 await page.setViewportSize({width:1600,height:1000});
 const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',path=out+'/六轴机械臂SDF演示.i5cam';
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.mouse.up();await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machines?.length===2&&!mv.pending;},path);
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 const machines=await page.evaluate(()=>mv.scene.machines.map(m=>({id:m.entityId,name:m.name})));
 assert.equal(await page.locator('[data-five-machine-instance]').count(),1);assert.equal(await page.locator('[data-five-command^="five.machine.select:"]').count(),0);
 const results=[];
 for(const machine of machines){
  await page.locator('[data-five-machine-instance]').focus();
  await page.evaluate(()=>{window.instanceCombo=document.querySelector('[data-five-machine-instance]');window.instanceCanvas=mv.viewport.renderer.domElement;document.querySelector('.cam-info-pane').scrollTop=100;});
  await page.locator('[data-five-machine-instance]').selectOption(machine.id);
  await page.locator('.cam-info-pane').evaluate(e=>e.scrollTop=140);
  await page.waitForFunction(()=>!mv.fiveMachine.busy);
  const switching=await page.evaluate(()=>({same:instanceCombo===document.querySelector('[data-five-machine-instance]'),focus:document.activeElement===instanceCombo,value:instanceCombo.value,canvas:instanceCanvas===mv.viewport.renderer.domElement,scroll:document.querySelector('.cam-info-pane').scrollTop,name:document.querySelector('[data-five-key="instance-state"] dd')?.textContent}));
  assert.deepEqual(switching,{same:true,focus:true,value:machine.id,canvas:true,scroll:140,name:machine.name});
  await page.waitForFunction(id=>{const m=mv.scene.machines.find(m=>m.entityId===id);return m.elements.filter(e=>e.kind!=='machine').every(e=>mv.viewport.sceneObjects.has(e.entityId));},machine.id);
  const hierarchy=await page.evaluate(id=>{
   const m=mv.scene.machines.find(m=>m.entityId===id),tree=document.querySelector('.five-machine-tree'),elements=m.elements;
   return elements.map(e=>{const node=tree.querySelector(`[data-machine-node="${e.entityId}"]`),parent=node?.parentElement.closest('[data-machine-node]');return {name:e.name,id:e.entityId,expected:elements.some(n=>n.entityId===e.parentEntityId)?e.parentEntityId:null,actual:parent?.dataset.machineNode||null,exists:!!node,depth:node?[...function*(n){while(n){yield n;n=n.parentElement.closest('[data-machine-node]');}}(node)].length:0,joint:m.joints.find(j=>j.entityId===e.entityId)?.jointName,text:node?.querySelector(':scope > .five-machine-tree-row')?.textContent};});
  },machine.id);
  for(const n of hierarchy){assert.ok(n.exists,n.name);assert.equal(n.actual,n.expected,n.name);if(n.joint)assert.ok(n.text.includes(n.joint));}
  assert.ok(Math.max(...hierarchy.map(n=>n.depth))>=6,'actual axis chain');
  const branch=hierarchy.find(n=>n.joint==='X'||n.joint==='J1'),leaf=hierarchy.find(n=>n.joint==='A'||n.joint==='J6');assert.ok(branch&&leaf);
  const toggle=page.locator(`[data-five-command="five.machine.tree-toggle:${branch.id}"]`),leafButton=page.locator(`[data-five-command="five.machine.part:${leaf.id}"]`);
  await toggle.click();assert.equal(await leafButton.isVisible(),false);assert.equal(await toggle.getAttribute('aria-expanded'),'false');
  await toggle.press('Enter');assert.equal(await leafButton.isVisible(),true);
  await page.locator(`[data-five-command="five.machine.part:${leaf.id}"]`).click();await page.waitForFunction(()=>!mv.fiveMachine.busy);
  assert.equal(await page.locator('[data-five-machine-field="box:part"]').inputValue(),leaf.id);
  assert.equal(await page.locator(`[data-five-command="five.machine.part:${leaf.id}"]`).getAttribute('aria-pressed'),'true');
  const retained=await page.evaluate(async ({branch,leaf})=>{
   const field=document.querySelector('[data-five-machine-field="post:name"]'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),canvas=mv.viewport.renderer.domElement;
   const nodes=[...document.querySelectorAll('[data-machine-node]')],objects=new Map(mv.viewport.sceneObjects),proxy=mv.viewportSceneProxy,invoke=proxy.invoke,calls=[];
   field.focus({preventScroll:true});field.setSelectionRange(1,3);left.scrollTop=30;right.scrollTop=600;const scroll=[left.scrollTop,right.scrollTop];let events=0;field.addEventListener('tree-test',()=>events++);
   proxy.invoke=async function(...args){calls.push(args[0]);return invoke.apply(this,args);};
   try{
    await __icaxAppShell.executeRibbonCommand('five.machine.tree-toggle:'+branch.id);
    const hidden=document.querySelector(`[data-machine-node="${leaf.id}"]`).getClientRects().length===0;
    // A subsequent regular panel patch must retain the stored disclosure state.
    const {renderMachineLeft}=await import('../../../apps/five-axis-cam/webpage/machineSimulation.mjs');
    const {patchWorkspacePanels}=await import('../../../apps/five-axis-cam/webpage/workspaceDom.mjs');
    patchWorkspacePanels(document,[['.cam-context-pane',renderMachineLeft(mv)]]);
    const stillHidden=document.querySelector(`[data-machine-node="${leaf.id}"]`).getClientRects().length===0;
    await __icaxAppShell.executeRibbonCommand('five.machine.tree-toggle:'+branch.id);
    field.dispatchEvent(new Event('tree-test'));
    return {hidden,stillHidden,shown:document.querySelector(`[data-machine-node="${leaf.id}"]`).getClientRects().length>0,sameNodes:nodes.every(n=>n===document.querySelector(`[data-machine-node="${n.dataset.machineNode}"]`)),sameField:field===document.querySelector('[data-five-machine-field="post:name"]'),focused:document.activeElement===field,selection:[field.selectionStart,field.selectionEnd],scroll,afterScroll:[left.scrollTop,right.scrollTop],events,calls,sameCanvas:canvas===mv.viewport.renderer.domElement,sameObjects:[...objects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o)};
   }finally{proxy.invoke=invoke;}
  },{branch,leaf});
  for(const key of ['hidden','stillHidden','shown','sameNodes','sameField','focused','sameCanvas','sameObjects'])assert.equal(retained[key],true,key);
  assert.deepEqual(retained.selection,[1,3]);assert.deepEqual(retained.scroll,retained.afterScroll);assert.equal(retained.events,1);assert.deepEqual(retained.calls,[]);
  await page.setViewportSize({width:1600,height:1000});await page.locator('.cam-context-pane').evaluate(e=>e.scrollTop=0);await page.locator('.cam-info-pane').evaluate(e=>e.scrollTop=0);
  await page.evaluate(()=>mv.viewport.fitViewToViewport?.(1));await page.waitForTimeout(200);
  await page.screenshot({path:out+'/'+(machine.name.includes('IR-1600')?'robot':'gantry')+'-mechanical-tree.png'});
  results.push({machine:machine.name,hierarchy,retained,switching});
 }
 // Multiple changes while a view switch is still pending retain the last choice.
 const last=machines[0].id;
 await page.evaluate(ids=>{
  const proxy=mv.viewportSceneProxy.views,start=proxy.start;window.restoreInstanceViewStart=()=>proxy.start=start;
  proxy.start=async function(...args){await new Promise(r=>setTimeout(r,80));return start.apply(this,args);};
  const combo=document.querySelector('[data-five-machine-instance]');combo.focus({preventScroll:true});
  for(const id of [ids[0],ids[1],ids[0]]){combo.value=id;combo.dispatchEvent(new Event('change',{bubbles:true}));}
 },machines.map(m=>m.id));
 try{await page.waitForFunction(id=>!mv.fiveMachine.busy&&mv.fiveMachine.machineId===id&&!mv.fiveMachine.nextMachineId,last);assert.equal(await page.locator('[data-five-machine-instance]').inputValue(),last);}
 finally{await page.evaluate(()=>restoreInstanceViewStart());}
 writeFileSync(out+'/tree-acceptance.json',JSON.stringify({pass:true,results,lastChoiceWins:true},null,2));console.log(JSON.stringify({pass:true,machines:results.map(r=>({name:r.machine,nodes:r.hierarchy.length,maxDepth:Math.max(...r.hierarchy.map(n=>n.depth))})),instanceCombo:true,lastChoiceWins:true,foldingAndSelection:true,nodesAndInteractionPreserved:true,noSceneReads:true}));process.exit(0);
}catch(e){console.error(e);process.exit(1);}
