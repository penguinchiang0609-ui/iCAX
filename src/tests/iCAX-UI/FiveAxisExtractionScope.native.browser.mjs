import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {deserializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

export async function verifyExtractionScope({page,rpc,calls,output}){
 const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState'});
 const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await page.waitForFunction(()=>!fixture.view.pending);assert.equal(await page.evaluate(()=>fixture.view.error),'',id);};
 const initial=await read(),inventory=await rpc({action:'face-inventory'});
 await page.evaluate(()=>{window.scopeNativeCandidates=[];const original=fixture.context.sceneProxy.invoke.bind(fixture.context.sceneProxy);fixture.context.sceneProxy.invoke=async(method,payload,options)=>{const result=await original(method,payload,options);if(method==='CamCut.QueryCandidates')scopeNativeCandidates.push(...result.candidates);return result;};});
 const faceId=inventory.faces.sort((a,b)=>Number(b.wireCount)-Number(a.wireCount))[0].id;
 const cases=[];
 const setScope=async scope=>{await page.getByRole('checkbox',{name:'内孔',exact:true}).setChecked(scope==='inner'||scope==='all');await page.getByRole('checkbox',{name:'外轮廓',exact:true}).setChecked(scope==='outer'||scope==='all');};
 const prepare=async()=>{
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));await command('five.setup.create');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.extraction.begin.Faces');
  await page.evaluate(async faceId=>{const {extractionDraft,extractionChanged}=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');const v=fixture.view,d=v.scene.domain,p=d.placements.find(p=>p.id===v.scene.activePlacementId);const sources=await fixture.context.sceneProxy.invoke('CamCut.QuerySources',{expected:d.expected,setupId:p.setupId,placementId:p.id});const f=sources.faceUses.find(f=>f.localId===faceId);if(!f)throw Error('Native face missing');extractionDraft(v).extraction.faces=[f];extractionChanged(v);fixture.entry.mountProject(fixture.context);},faceId);
 };
 for(const [scope,count] of [['inner',3],['outer',1],['all',4]]){
  await prepare();
  await page.evaluate(()=>{scopeNativeCandidates=[]});
  const selector=page.getByRole('group',{name:'识别范围',exact:true});assert.equal(await selector.getByRole('checkbox').count(),2);assert.equal(await selector.locator('select').count(),0);await setScope(scope);
  await page.waitForFunction(async scope=>{const {extractionDraft}=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');return extractionDraft(fixture.view).extraction.boundaryScope===scope;},scope);
  assert.equal(await page.locator('[data-five-command="five.extraction.run"]').count(),0);
  await page.getByRole('button',{name:'确定',exact:true}).click();
  await page.waitForFunction(count=>fixture.view.error||!fixture.view.pending&&fixture.view.scene.intentToolpaths.length===count,count,{timeout:60000});assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const d=await read(),cuts=d.intentToolpaths.filter(c=>c.setupId===d.activeSetupId);
  assert.equal(cuts.length,count);assert.deepEqual(d.workpieces,initial.workpieces);
  const request=calls.filter(c=>c.method==='CamCut.Create').at(-1),payload=request.payload??deserializeVariantText(request.payloadText);assert.equal(payload.candidateIds.length,count);
  const requested=await page.evaluate(ids=>scopeNativeCandidates.filter(c=>ids.includes(c.candidateId)),payload.candidateIds);assert.equal(requested.length,count);
  assert.ok(requested.every(c=>scope==='inner'?c.boundary.regionRelation==='InnerLoop'||c.boundaryRole==='Opening':scope==='outer'?c.boundary.regionRelation==='OuterLoop'||c.boundaryRole==='OuterTrim':c.boundary.regionRelation==='InnerLoop'||c.boundary.regionRelation==='OuterLoop'||['Opening','OuterTrim'].includes(c.boundaryRole)));
  cases.push({scope,count,roles:[...new Set(cuts.map(c=>c.boundaryRole))],singleConfirm:true});
 }
 await prepare();await command('five.extraction.run');
 const changes=[];
 for(const [scope,count] of [['inner',3],['outer',1],['all',4]]){
  await setScope(scope);
  await page.waitForFunction(async count=>{const {extractionDraft}=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');return extractionDraft(fixture.view).extraction.selected.length===count;},count);
  assert.equal(await page.locator('[data-five-candidate]').count(),count);changes.push({scope,count});
 }
 await setScope('none');assert.equal(await page.getByRole('button',{name:'确定',exact:true}).isDisabled(),true);assert.equal(await page.locator('[data-five-candidate]').count(),0);
 await setScope('all');assert.equal(await page.getByRole('button',{name:'确定',exact:true}).isEnabled(),true);
 await page.screenshot({path:resolve(output,'recognition-scope.png')});
 writeFileSync(resolve(output,'extraction-scope-native.json'),JSON.stringify({passed:true,nativeRecognition:true,nativeCreation:true,cases,changes,independentCheckboxes:true,emptySelectionDisabled:true,cadUnchanged:true},null,2));
}
