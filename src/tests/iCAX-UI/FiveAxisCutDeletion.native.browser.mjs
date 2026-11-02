import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
export async function verifyCutDeletion({page,rpc,calls,output}){
 const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
 const before=await rpc({action:'read-state'}),target=before.intentToolpaths.at(-1).id,keep=before.intentToolpaths[0].id;
 await page.locator(`[data-five-intent="${keep}"]`).evaluate(n=>n.click());await command('five.tool.compensation');
 const input=page.locator('[data-five-field="cutKerf"]');await input.waitFor();
 const style=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.cam-info-pane .five-section::after{content:"";display:block;height:1200px;min-height:1200px;flex:0 0 1200px}.cam-info-pane .five-section{height:170px;overflow:auto;flex:none}.cam-context-pane,.cam-info-pane{overflow:auto!important}.cam-info-pane{height:450px!important;max-height:450px!important}'});
 // Retry a recorded deletion while retaining another row's editor. The
 // normal command route and native receipt still perform the complete edit.
 await page.evaluate(target=>{const f=fixture,v=f.view;f.deleteInput=document.querySelector('[data-five-field="cutKerf"]');f.deleteCanvas=document.querySelector('canvas');f.deleteRow=document.querySelector('[data-five-intent]');f.deleteEvents=0;f.deleteCanvas.addEventListener('deletion-probe',()=>f.deleteEvents++);
 v.fiveTreeRequest={commandId:crypto.randomUUID(),expected:structuredClone(v.scene.domain.expected),setupId:v.scene.domain.activeSetupId,intentIds:[target],groupIds:[],action:'Delete'};f.holdMethod='CamCut.ManageTree';f.hold=true;f.deleteRun=f.entry.handleRibbonCommand(f.context,'five.tree.retry');},target);
 await page.waitForFunction(()=>fixture.held);
 const scrolls=await page.evaluate(()=>{const f=fixture,n=f.deleteInput;n.focus({preventScroll:true});n.setSelectionRange(0,1);const panes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.cam-info-pane .five-section')];panes.forEach((p,i)=>p.scrollTop=90+i*20);return panes.map(p=>p.scrollTop)});assert(scrolls.every(x=>x>0));
 const callStart=calls.length;await page.evaluate(async()=>{fixture.release();await fixture.deleteRun});
 assert(!calls.slice(callStart).some(c=>c.method==='CamScene.ReadState'));
 assert.equal(await page.locator(`[data-five-intent="${target}"]`).count(),0);
 const interaction=await page.evaluate(()=>{const f=fixture,n=document.querySelector('[data-five-field="cutKerf"]');f.deleteCanvas.dispatchEvent(new Event('deletion-probe'));return {inputRetained:n===f.deleteInput,focused:document.activeElement===n,selection:[n.selectionStart,n.selectionEnd],canvasRetained:f.deleteCanvas===document.querySelector('canvas'),rowRetained:f.deleteRow===document.querySelector('[data-five-intent]'),events:f.deleteEvents,scrolls:[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.cam-info-pane .five-section')].map(p=>p.scrollTop),error:f.view.error}});
 assert.equal(interaction.inputRetained,true);assert.equal(interaction.focused,true);assert.deepEqual(interaction.selection,[0,1]);assert.equal(interaction.canvasRetained,true);assert.equal(interaction.rowRetained,true);assert.equal(interaction.events,1);assert.deepEqual(interaction.scrolls,scrolls);assert.equal(interaction.error,'');
 const after=await rpc({action:'read-state'});assert.deepEqual(after.intentGenerationTasks,[]);assert.equal(after.intentToolpaths.length,before.intentToolpaths.length-1);for(const key of ['workpieces','fixtures','placements'])assert.deepEqual(after[key],before[key]);
 await style.evaluate(n=>n.remove());writeFileSync(resolve(output,'delete-interaction-report.json'),JSON.stringify({passed:true,fullSceneReadDuringDeletion:false,newGenerationTasks:0,...interaction},null,2));
}
