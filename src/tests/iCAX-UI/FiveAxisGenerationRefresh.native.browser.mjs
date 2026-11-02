import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
export async function verifyGenerationRefresh({page,rpc,calls,output}){
 const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
 const read=()=>invoke('CamScene.ReadState'),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 let state=await read();assert.equal(state.intentToolpaths.length,50);
 await invoke('CamCut.Reverse',{commandId:crypto.randomUUID(),expected:state.expected,intentIds:[state.intentToolpaths[0].id]});
 state=await read();await page.evaluate(async state=>{const {mergeDomainState}=await import('/src/apps/five-axis-cam/webpage/domainActions.mjs');mergeDomainState(fixture.view.scene,state);await fixture.context.actions.selectRibbonTab('toolpath');},state);
 await page.evaluate(()=>{fixture.loseMethod='CamScene.ReadState';fixture.savedCanvas=fixture.view.viewport.renderer.domElement;});
 const before=calls.filter(c=>c.method==='CamRapid.Generate').length;
 await command('five.tool.generate');
 assert.equal(calls.filter(c=>c.method==='CamRapid.Generate').length,before,'failed refresh must stop before rapid generation');
 assert.equal(await page.evaluate(()=>fixture.view.fiveGeneration.needsRefresh),true);
 assert.match(await page.evaluate(()=>fixture.view.error),/Injected lost editing response/);
 const started=performance.now();await command('five.generate.apply');
 assert.equal(await page.evaluate(()=>fixture.view.error),'');
 assert.equal(await page.evaluate(()=>fixture.view.fiveGeneration.needsRefresh),false);
 assert.equal(await page.evaluate(()=>fixture.savedCanvas===fixture.view.viewport.renderer.domElement),true);
 state=await read();const g=state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;
 assert.equal(g.status,'Current');assert.equal(g.operations.length,50);assert.equal(g.transitions.length,49);
 assert.equal(calls.filter(c=>c.method==='CamRapid.Generate').length,before+1);
 await page.screenshot({path:resolve(output,'part45-refresh-recovered.png')});
 writeFileSync(resolve(output,'part45-refresh-report.json'),JSON.stringify({passed:true,retryMs:performance.now()-started,cuts:50,transitions:49,clear:g.rapidClearCount,cases:['failed post-generation refresh blocks rapid request','retry reads current revision before rapid generation','canvas retained','actual part45 native generation and 49 transitions'],coverage:{native:true,browser:true,standaloneCef:false}},null,2));
}
