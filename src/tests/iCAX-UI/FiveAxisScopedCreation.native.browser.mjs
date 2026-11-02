import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyScopedCreation({page,rpc,calls,output,sourcePath}){
 const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
 const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveOrder?.running&&(!window.extractionModule||!extractionModule.extractionDraft(fixture.view).extraction.confirming));
 let state=await invoke('CamScene.ReadState');
 if(!state.workpieces.length){await selectWorkpieceFile(page,sourcePath);await idle();state=await invoke('CamScene.ReadState');}
 if(state.intentToolpaths.length){await invoke('CamCut.Delete',{commandId:crypto.randomUUID(),expected:state.expected,intentIds:state.intentToolpaths.map(c=>c.id)});await command('five.refresh');await idle();state=await invoke('CamScene.ReadState');}
 const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:state.activeSetupId,placementId:state.placements[0].id});
 const inventory=await rpc({action:'face-inventory'}),best=inventory.faces.toSorted((a,b)=>Number(b.wireCount)-Number(a.wireCount))[0];
 const selected=process.env.ICAX_FACE_IDS?JSON.parse(process.env.ICAX_FACE_IDS):[best.id];
 const faces=selected.map(id=>sources.faceUses.find(f=>f.localId===id));assert.ok(faces.every(Boolean));
 await page.evaluate(async faces=>{
   const E=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');window.extractionModule=E;
   await fixture.context.actions.selectRibbonTab('toolpath');
   await fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract');
   const e=E.extractionDraft(fixture.view).extraction;e.mode='Faces';e.faces=faces;E.extractionChanged(fixture.view);
 },faces);
 await command('five.extraction.run');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
 const candidates=await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.selected.length);assert.ok(candidates>1);
 const start=calls.length;
 await page.evaluate(()=>{fixture.holdMethod='CamCut.Read';fixture.hold=true;fixture.creationCanvas=fixture.view.viewport.renderer.domElement;});
 await page.locator('[data-five-command="five.extraction.confirm"]').click();
 await page.waitForFunction(()=>fixture.held||!fixture.view.pending);
 assert.equal(await page.evaluate(()=>fixture.held),true,await page.evaluate(()=>fixture.view.error));
 // The async result must update the page now in front of the user. Use the
 // production search control, with scroll dimensions forced only for the probe.
 await page.evaluate(async()=>{
   fixture.view.pending=false;await fixture.context.actions.selectRibbonTab('help');
   const left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
   const style=document.createElement('style');style.id='interaction-probe-style';style.textContent='.cam-context-pane,.cam-info-pane{height:240px!important;overflow:auto!important}.cam-context-pane>[data-five-key],.cam-info-pane>[data-five-key]{min-height:360px}.five-tree-rows{height:200px;overflow:auto}';document.head.append(style);
   const input=document.querySelector('[data-five-search]');if(!input)throw Error('production search input missing');
   input.focus({preventScroll:true});input.value='HatRail';input.setSelectionRange(1,4);
   let listener=0;input.addEventListener('probe',()=>listener++);
   left.scrollTop=120;right.scrollTop=150;
   window.interactionProbe={left,right,input,canvas:fixture.view.viewport.renderer.domElement,get listener(){return listener;}};
   left.scrollTop=220;right.scrollTop=250;fixture.release();
 });
 await idle();
 const creationCalls=calls.slice(start).filter(c=>c.action==='invoke');
 assert.equal(creationCalls.filter(c=>['CamScene.ReadState','Machine.List','MachineDefinition.List','CamPlan.SetOrder','CamPlan.BeginOrder','CamPlan.Generate','CamRapid.Generate'].includes(c.method)).length,0,'creation must stay scoped');
 assert.equal(creationCalls.filter(c=>c.method==='CamProcess.BeginPreview').length,0,'plain contours must not launch saved-lead previews');
 const interaction=await page.evaluate(()=>{const p=interactionProbe;p.input.dispatchEvent(new Event('probe'));return {input:p.input===document.querySelector('[data-five-search]'),canvas:p.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===p.input,selection:[p.input.selectionStart,p.input.selectionEnd],scroll:[p.left.scrollTop,p.right.scrollTop],listener:p.listener,error:fixture.view.error,cuts:fixture.view.scene.intentToolpaths.length};});
 assert.equal(interaction.error,'');assert.equal(interaction.cuts,candidates);assert.equal(interaction.input,true);assert.equal(interaction.canvas,true);assert.equal(interaction.focus,true);assert.deepEqual(interaction.selection,[1,4]);assert.equal(interaction.listener,1);
 assert.deepEqual(interaction.scroll,[220,250]);
 await page.evaluate(()=>document.querySelector('#interaction-probe-style').remove());
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
 await command('five.tool.auto-order');
 const sortStart=calls.length;
 await page.evaluate(()=>{fixture.holdMethod='CamPlan.BeginOrder';fixture.hold=true;fixture.loseMethod='CamPlan.BeginOrder';void fixture.entry.handleRibbonCommand(fixture.context,'five.order.apply');});
 await page.waitForFunction(()=>fixture.held);
 assert.equal(await page.locator('[data-five-command="five.order.close"]').last().isEnabled(),true);
 await command('five.order.close');assert.equal(await page.getByRole('dialog',{name:'自动排序'}).count(),0);
 await page.evaluate(()=>fixture.release());await idle();
 assert.equal(await page.evaluate(()=>fixture.view.error),'');
 assert.equal(calls.slice(sortStart).filter(c=>['CamScene.ReadState','Machine.List','MachineDefinition.List'].includes(c.method)).length,0);
 assert.equal(calls.slice(sortStart).filter(c=>c.method==='CamPlan.BeginOrder').length,1);
 assert.equal(calls.slice(sortStart).filter(c=>c.method==='CamScene.QueryCommand').length,1);
 const order=await page.evaluate(()=>{const v=fixture.view,s=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId);return {closed:!v.fiveOrder.open,done:v.fiveOrder.done,ids:s.definitionOrder,expected:v.scene.domain.expected};});
 assert.equal(order.closed,true);assert.equal(order.done,true);assert.equal(order.ids.length,candidates);
 await verifySimulationScene({page,calls,output});
 await rpc({action:'export-project',path:resolve(output,'selected-face.i5cam')});
 await page.screenshot({path:resolve(output,'selected-face-created.png')});
 writeFileSync(resolve(output,'scoped-creation-report.json'),JSON.stringify({passed:true,source:state.workpieces[0].name,faces:selected,cuts:candidates,interaction,creationCalls:creationCalls.map(c=>({method:c.method,elapsedMs:c.elapsedMs})),order,coverage:{native:true,browser:true,standaloneCef:false},cases:['selected faces create visible paths without full Scene or machine reread','lost async sort reply automatically reconciled','Close remains enabled while sorting and stays closed after completion','instance editing hides setup meshes and CAM overlays; returning to CAM restores paths','page switches perform no generation or ordering']},null,2));
}
export async function verifySimulationScene({page,calls,output}){
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
 const beforeTabs=calls.length;
 const original=await page.evaluate(()=>({cuts:fixture.view.scene.intentToolpaths.length,fixtures:fixture.view.scene.domain.fixtures.length}));
 for(let i=0;i<2;i++){
   await page.evaluate(()=>fixture.context.actions.selectRibbonTab('simulation'));await idle();
   await page.waitForFunction(()=>{const v=fixture.view,ids=new Set([...v.scene.domain.placements,...v.scene.domain.fixtures].map(o=>o.id));return !v.fiveCutPreview&&!v.fiveGeneratedPreview&&[...v.viewport.sceneObjects].every(([id,obj])=>!ids.has(id)||!obj.visible);});
   assert.equal(await page.evaluate(()=>!!fixture.view.fiveCutPreview||!!fixture.view.fiveGeneratedPreview),false);
   await page.screenshot({path:resolve(output,`simulation-instance-${i}.png`)});
   await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();
   await page.waitForFunction(()=>!!fixture.view.fiveCutPreview);
   assert.equal(await page.evaluate(()=>fixture.view.scene.intentToolpaths.length),original.cuts);
 }
 assert.equal(calls.slice(beforeTabs).filter(c=>/CamCut\.(Create|Extract)|CamPlan\.(Generate|BeginOrder|SetOrder)|CamRapid\.Generate/.test(c.method||'')).length,0);
 writeFileSync(resolve(output,'simulation-scene-report.json'),JSON.stringify({passed:true,...original,coverage:{native:true,browser:true,standaloneCef:false},cases:['instance editing excludes workpiece and fixture meshes and CAM overlays','returning to CAM restores all cuts','page switches do not generate or sort']},null,2));
}
export async function verifyScopedOrdering({page,rpc,calls,output}){
 const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
 const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 await page.evaluate(async()=>{
   const {waitForTaskResult}=await import('/src/apps/five-axis-cam/webpage/taskProgressStream.mjs');
   const D=await import('/src/apps/five-axis-cam/webpage/domainActions.mjs');
   const v=fixture.view,c=fixture.context;
   const task=await c.sceneProxy.invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:v.scene.domain.expected,setupId:v.scene.domain.activeSetupId});
   const done=await waitForTaskResult(c,task,{queryMethod:'CamPlan.QueryStatus',cancelMethod:'CamPlan.CancelTask',timeoutMs:120000});if(done.status!=='Completed')throw Error(done.error);
   const state=await D.readMachiningState(c);D.mergeDomainState(v.scene,state);
   const setup=state.setups.find(s=>s.id===state.activeSetupId),rank=new Map(setup.definitionOrder.map((id,i)=>[id,i]));
   const priority=o=>o.machiningKind==='Mark'?0:o.boundaryRole==='OuterTrim'?2:1;
   const ids=[...state.intentToolpaths].filter(o=>o.enabled!==false).sort((a,b)=>priority(a)-priority(b)||rank.get(b.id)-rank.get(a.id)).map(o=>o.id);
   await c.sceneProxy.invoke('CamPlan.SetOrder',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,mode:'Manual',intentIds:ids});
   D.mergeDomainState(v.scene,await D.readMachiningState(c));
   await c.actions.selectRibbonTab('toolpath');
 });
 const baseline=await page.evaluate(()=>{const v=fixture.view,g=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).generatedGeometry;return {ids:g.operations.map(o=>o.intentId),parts:Object.fromEntries(g.operations.map(o=>[o.intentId,o.parts])),pairs:g.transitions.map(t=>`${t.from}/${t.to}`)};});
 assert.equal(baseline.ids.length,31);
 const start=calls.length;await command('five.tool.auto-order');
 await page.evaluate(()=>{fixture.holdMethod='CamPlan.BeginOrder';fixture.hold=true;fixture.loseMethod='CamPlan.BeginOrder';void fixture.entry.handleRibbonCommand(fixture.context,'five.order.apply');});
 await page.waitForFunction(()=>fixture.held);
 assert.equal(await page.locator('[data-five-command="five.order.close"]').last().isEnabled(),true);
 await command('five.order.close');assert.equal(await page.getByRole('dialog',{name:'自动排序'}).count(),0);
 await page.evaluate(()=>fixture.release());await page.waitForFunction(()=>!fixture.view.fiveOrder.running);
 const result=await page.evaluate(()=>{const v=fixture.view,s=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId),g=s.generatedGeometry;return {error:v.error,closed:!v.fiveOrder.open,done:v.fiveOrder.done,status:g.status,expected:v.scene.domain.expected,ids:g.operations.map(o=>o.intentId),parts:Object.fromEntries(g.operations.map(o=>[o.intentId,o.parts])),transitions:g.transitions};});
 assert.equal(result.error,'');assert.equal(result.closed,true);assert.equal(result.done,true);assert.deepEqual(result.parts,baseline.parts);
 assert.notDeepEqual(result.ids,baseline.ids);
 const scopedCalls=calls.slice(start).filter(c=>c.action==='invoke');
 assert.equal(scopedCalls.filter(c=>['CamScene.ReadState','Machine.List','MachineDefinition.List','CamPlan.Generate','CamRapid.Generate'].includes(c.method)).length,0);
 assert.equal(scopedCalls.filter(c=>c.method==='CamProcess.BeginPreview').length,0,'sorting must not launch saved-lead previews');
 assert.equal(scopedCalls.filter(c=>c.method==='CamScene.QueryCommand').length,1);
 const state=await invoke('CamScene.ReadState'),native=state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;
 assert.equal(native.status,'Current');assert.equal(result.status,'Current');assert.equal(state.intentGenerationTasks.length,0);
 assert.deepEqual(result.ids,native.operations.map(o=>o.intentId));assert.deepEqual(result.expected,state.expected);
 const newPairs=result.transitions.filter(t=>!baseline.pairs.includes(`${t.from}/${t.to}`));assert.ok(newPairs.length);assert.ok(newPairs.every(t=>t.geometryStatus==='Missing'&&t.validation==='Unchecked'));
 await page.screenshot({path:resolve(output,'scoped-order-31.png')});
 writeFileSync(resolve(output,'scoped-order-report.json'),JSON.stringify({passed:true,cuts:result.ids.length,newConnections:newPairs.length,originalCuttingSamplesRetained:true,orderMatchesNative:true,closedWhilePending:true,calls:scopedCalls.map(c=>({method:c.method,elapsedMs:c.elapsedMs})),coverage:{native:true,browser:true,standaloneCef:false}},null,2));
}
