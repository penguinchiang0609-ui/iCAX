import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

// Runs the production editor against the real automotive project and native
// bridge. A delayed response exercises interaction after the save was sent.
export async function verifyToolAxisDeferred({page,rpc,calls,output}){
 const id='c9629454-2602-4757-8d23-2e3952b07b17',cases=[],edits=[];
 page.setDefaultTimeout(90000);
 const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisPointEdit?.loading&&!fixture.view.fiveAxisPointEdit?.request&&!fixture.view.fiveAxisPointSaving&&!fixture.view.fiveToolAxisDisplay?.loading,undefined,{polling:20});
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await page.getByRole('tab',{name:'切割',exact:true}).click();
 await page.locator(`[data-five-intent="${id}"]`).click();await command('five.points.edit');await idle();
 const field=page.getByRole('textbox',{name:'关键点 欧拉角 X',exact:true});await field.waitFor();
 const start=calls.length;
 await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{display:block!important;overflow:auto!important;height:400px!important}.cam-context-pane::after,.cam-info-pane::after,.five-axis-keypoints::after{content:"";display:block;min-height:1600px;flex:0 0 1600px}.five-axis-keypoints{max-height:360px;overflow:auto}'});
 await page.evaluate(()=>{fixture.poseNodes={input:document.querySelector('[aria-label="关键点 欧拉角 X"]'),canvas:fixture.view.viewport.renderer.domElement,objects:[...fixture.view.viewport.sceneObjects.values()],allAxes:fixture.view.fiveToolAxisDisplay.group};fixture.poseEvents=0;fixture.poseNodes.input.addEventListener('probe',()=>fixture.poseEvents++);fixture.holdMethod='CamCut.EditToolAxisPoint';fixture.hold=true;});
 await field.fill(String(Number(await field.inputValue())+1));await field.press('Enter');await page.waitForFunction(()=>fixture.held);
 const scroll=await page.evaluate(()=>{const n=fixture.poseNodes.input;n.focus({preventScroll:true});n.setSelectionRange(0,2);fixture.poseSelection=[n.selectionStart,n.selectionEnd];return ['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=105+i*40;return p.scrollTop;});});
 assert(scroll.every(n=>n>0));assert.equal(await page.locator('.five-blocking-progress,.five-blocking-operation').count(),0);
 assert.equal(await page.evaluate(()=>fixture.view.pending),false);
 await page.evaluate(()=>fixture.release());await idle();
 const interaction=await page.evaluate(()=>{fixture.poseNodes.input.dispatchEvent(new Event('probe'));return {input:fixture.poseNodes.input===document.querySelector('[aria-label="关键点 欧拉角 X"]'),canvas:fixture.poseNodes.canvas===fixture.view.viewport.renderer.domElement,objects:fixture.poseNodes.objects.every((o,i)=>o===[...fixture.view.viewport.sceneObjects.values()][i]),focus:document.activeElement===fixture.poseNodes.input,selection:[fixture.poseNodes.input.selectionStart,fixture.poseNodes.input.selectionEnd],expectedSelection:fixture.poseSelection,scroll:['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map(s=>document.querySelector(s).scrollTop),events:fixture.poseEvents,axes:fixture.poseNodes.allAxes===fixture.view.fiveToolAxisDisplay.group,error:fixture.view.fiveAxisPointEdit.error};});
 assert.equal(interaction.input,true);assert.equal(interaction.canvas,true);assert.equal(interaction.objects,true);assert.equal(interaction.focus,true);assert.deepEqual(interaction.selection,interaction.expectedSelection);assert.deepEqual(interaction.scroll,scroll);assert.equal(interaction.events,1);assert.equal(interaction.axes,true);assert.equal(interaction.error,'');
 cases.push('delayed native receipt preserves the actual canvas, meshes, input node, focus, selection, listener and latest scroll in both sidebars and nested editor');
 await page.evaluate(()=>{fixture.poseInputTimes=[];document.addEventListener('keydown',event=>{if(event.key!=='Enter'||event.target!==fixture.poseNodes.input)return;const started=performance.now(),node=event.target;const observer=new MutationObserver(()=>{if(!node.readOnly&&!fixture.view.fiveAxisPointEdit?.request&&!fixture.view.fiveAxisPointSaving){fixture.poseInputTimes.push(performance.now()-started);observer.disconnect();}});observer.observe(node,{attributes:true,attributeFilter:['readonly']});},true);});
 const automationTimes=[];
 for(let i=0;i<5;++i){await field.fill(String(Number(await field.inputValue())+.5));const time=performance.now();await page.keyboard.press('Enter');await idle();automationTimes.push(performance.now()-time);assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.error),'');}
 edits.push(...await page.evaluate(()=>fixture.poseInputTimes));assert.equal(edits.length,5,'every real Enter key must restore the same editable input');
 writeFileSync(resolve(output,'pose-editor-requests.json'),JSON.stringify(calls.slice(start),null,2));
 const implicit=calls.slice(start).filter(c=>c.method&&(/Generate|ReadToolAxisSamples/.test(c.method)||c.method==='CamScene.ReadState'));
 assert.deepEqual(implicit,[],'saving pose parameters must not read the whole scene, sample interpolation or generate');
 cases.push('six consecutive native edits trigger no full-state read, pose sampling or generation');
 const availability=await page.evaluate(async()=>{const {commandAvailability}=await import('/src/apps/five-axis-cam/webpage/commandAvailability.mjs');const v=fixture.view;return {geometry:v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).generatedGeometry.status,simulation:commandAvailability(v,'five.tool.simulation'),collision:commandAvailability(v,'five.tool.collision')};});
 assert.equal(availability.geometry,'Stale');assert.equal(availability.simulation.disabled,false);assert.equal(availability.collision.disabled,false);
 cases.push('simulation and collision remain available when pose edits invalidate computed results');
 const state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});assert(!(state.intentGenerationTasks||[]).some(t=>t.status==='Running'));
 writeFileSync(resolve(output,'interactive-latency.json'),JSON.stringify({editsMs:edits,automationRoundTripMs:automationTimes},null,2));
 assert(Math.max(...edits)<1000,`consecutive editor saves still block: ${edits}`);
 const notation=page.getByRole('combobox',{name:'姿态表示方式',exact:true});await notation.selectOption('IJK');
 assert.equal(await field.count(),0);assert.equal(await page.evaluate(()=>document.activeElement?.getAttribute('aria-label')==='关键点 I'),false);
 cases.push('removing the Euler input through an actual notation change does not transfer its old focus to a different field');
 const beforeExplicit=calls.length;await command('five.tool.simulation');
 const generationCalls=calls.slice(beforeExplicit).filter(c=>c.method?.endsWith('.Generate')).map(c=>c.method);
 assert(generationCalls.includes('CamPlan.Generate'),'explicit simulation must calculate stale cutting paths');assert(generationCalls.includes('CamRapid.Generate'),'explicit simulation must prepare stale connections');
 const entered=await page.evaluate(async()=>{const {activeTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');return {tool:activeTool(fixture.view),error:fixture.view.error,geometry:fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).generatedGeometry.status};});
 assert.equal(entered.error,'');assert.equal(entered.tool,'simulation');assert.equal(entered.geometry,'Current');
 const beforeCollision=calls.length;await command('five.tool.collision');assert.equal(await page.getByRole('dialog',{name:'碰撞检测',exact:true}).count(),1);
 assert(!calls.slice(beforeCollision).some(c=>c.method?.endsWith('.Generate')),'entering collision must reuse the explicitly prepared trajectory');
 cases.push('explicit simulation prepares the latest cuts and connections; entering collision reuses that current trajectory');
 await page.screenshot({path:resolve(output,'deferred-pose-real-project.png')});
 writeFileSync(resolve(output,'deferred-pose-browser.json'),JSON.stringify({passed:true,cases,editsMs:edits,automationRoundTripMs:automationTimes,interaction,counts:{cuts:state.intentToolpaths.length},coverage:{actualProject:true,native:true,browser:'Edge',standaloneCef:false}},null,2));
}
