import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdirSync,writeFileSync} from 'node:fs';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const output=resolve('output/tests/machine-simulation');mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0];
await page.setViewportSize({width:1600,height:950});
const previous=await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath);
const bind=async()=>{await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.mi=(method,payload={})=>mv.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000});});await page.waitForFunction(()=>mv.scene?.domain);};
const cmd=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await page.waitForFunction(()=>!mv.fiveMachine?.busy);assert.equal(await page.evaluate(()=>mv.fiveMachine?.error||''),'');};
try{
 await page.evaluate(projectPath=>__icaxAppShell.createProject({projectName:'独立机床实例验收',projectPath,productId:'icax.five-axis-cam'}),resolve(output,'empty-instance.i5cam'));await bind();
 await page.locator('[data-tab-id="simulation"]').click();
 for(const id of ['machine-definitions','machine-instances','machine-simulation'])assert.equal(await page.locator(`[data-command-id="five.tool.${id}"]`).isEnabled(),true);
 await cmd('five.tool.machine-simulation');assert.equal(await page.locator('[data-five-command="five.machine.build"]').isDisabled(),true);assert.equal(await page.locator('[data-five-command="five.machine.export"]').isDisabled(),true);
 await cmd('five.tool.machine-definitions');await cmd('five.machine.sample');
 const definition=await page.evaluate(()=>mv.scene.machineDefinitions.find(d=>d.sourcePath.endsWith('/industrial-ac/LC-3015.urdf')).id);
 await cmd('five.machine.instantiate:'+definition);assert.equal(await page.locator('[data-five-key="instance-state"]').isVisible(),true);
 const x=await page.evaluate(()=>mv.scene.machines[0].joints.find(j=>j.jointName==='X').entityId);
 await cmd('five.machine.jog-plus:'+x);
 const detail=await page.evaluate(id=>mi('Machine.GetElement',{entityId:id}),x);assert.equal(detail.machineElement.transform.position[0],-429);
 await cmd('five.machine.jog-minus:'+x);await cmd('five.machine.post-save');
 // Keep the native view's field, listener and nested scroll state across delayed real refresh.
 await page.setViewportSize({width:1300,height:600});
 const interaction=await page.evaluate(async()=>{
  const field=document.querySelector('[data-five-machine-field="post:name"]'),canvas=mv.viewport.renderer.domElement,left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
  field.focus({preventScroll:true});field.setSelectionRange(1,3);let events=0;field.addEventListener('instance-check',()=>events++);
  const original=mv.viewportSceneProxy.invoke;mv.viewportSceneProxy.invoke=async function(...args){const value=await original.apply(this,args);if(args[0]==='CamScene.ReadState')await new Promise(r=>setTimeout(r,250));return value;};
  try{const refresh=__icaxAppShell.executeRibbonCommand('five.refresh');await new Promise(r=>setTimeout(r,80));left.scrollTop=90;right.scrollTop=600;const expected=[left.scrollTop,right.scrollTop];await refresh;field.dispatchEvent(new Event('instance-check'));return{same:field===document.querySelector('[data-five-machine-field="post:name"]'),canvas:canvas===mv.viewport.renderer.domElement,focused:document.activeElement===field,selection:[field.selectionStart,field.selectionEnd],scroll:[left.scrollTop,right.scrollTop],expected,events};}finally{mv.viewportSceneProxy.invoke=original;}
 });assert.equal(interaction.same,true);assert.equal(interaction.canvas,true);assert.equal(interaction.focused,true);assert.deepEqual(interaction.selection,[1,3]);assert.deepEqual(interaction.scroll,interaction.expected);assert.ok(interaction.expected.every(n=>n>0));assert.equal(interaction.events,1);
 await page.setViewportSize({width:1600,height:950});await cmd('five.tool.machine-definitions');await cmd('five.machine.definition-delete:'+definition);assert.equal(await page.locator(`[data-five-command="five.machine.instantiate:${definition}"]`).count(),0);
 await cmd('five.tool.machine-instances');await cmd('five.machine.jog-plus:'+x);await cmd('five.machine.home');
 await cmd('five.tool.machine-definitions');await cmd('five.machine.sample');
 await cmd('five.tool.machine-instances');await cmd('five.machine.jog-plus:'+x);
 await page.evaluate(()=>mi('Project.Save',{projectPath:__icaxAppShell.getState().activeProjectPath}));
 writeFileSync(resolve(output,'empty-instance-acceptance.json'),JSON.stringify({noWorkpieceJog:true,nativeTransformX:detail.machineElement.transform.position[0],definitionDeletionKeepsInstance:true,interaction},null,2));
 if(previous){await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),previous);await bind();}
 console.log('PASS: three menus, independent instance jog, native pose, definition deletion, both-pane interaction preservation');process.exit(0);
}catch(error){console.error(error);process.exit(1);}
