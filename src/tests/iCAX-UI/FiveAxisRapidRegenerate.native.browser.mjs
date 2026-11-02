import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

// Supply a saved project with one edited cut and generated rapid connectors.
// The production panel must update its selected connector without a batch job.
export async function verifyCurrentRapidRegeneration({page,rpc,calls,output}){
  const read=()=>rpc({action:'read-state'}),geometry=s=>s.setups.find(x=>x.id===s.activeSetupId).generatedGeometry;
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveRapid?.running&&!fixture.view.fiveRapid?.regenerating);
  const before=await read(),old=geometry(before),selected=old.transitions.findLast(t=>t.endpointStatus==='Stale'||t.geometryStatus==='Stale');
  assert.ok(selected,'fixture needs a connector whose adjacent cut has changed');const pair=`${selected.from}/${selected.to}`;
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.cam-page.rapid'));await idle();
  await page.locator('[data-five-rapid-field="connection"]').selectOption(pair);await idle();
  const button=page.locator('[data-five-command="five.rapid.regenerate"]');assert.equal(await button.isEnabled(),true);assert.match(await button.innerText(),/重新生成当前空移/);
  assert.doesNotMatch(await page.locator('.cam-info-pane').innerText(),/工程已变化，请重新打开/);
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.Generate';fixture.hold=true;fixture.rapidInput=document.querySelector('[data-five-rapid-field="marginMm"]');fixture.rapidCanvas=document.querySelector('canvas');fixture.rapidEvents=0;fixture.rapidInput.addEventListener('input',()=>fixture.rapidEvents++);});
  const count=calls.length;await button.click();await page.waitForFunction(()=>fixture.held);
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-rapid-body::after{content:"";display:block;height:1600px}.cam-context-pane,.cam-info-pane,.five-rapid-body{height:200px!important;max-height:200px!important;overflow-y:auto!important;display:block!important}'});
  const scroll=await page.evaluate(()=>{fixture.rapidInput.focus({preventScroll:true});fixture.rapidInput.setSelectionRange(1,3);return ['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=70+i*20;return n.scrollTop;});});
  assert.ok(scroll.every(x=>x>0));await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(await page.locator('[data-five-rapid-field="connection"]').inputValue(),pair);
  const kept=await page.evaluate(()=>({input:fixture.rapidInput===document.querySelector('[data-five-rapid-field="marginMm"]'),canvas:fixture.rapidCanvas===document.querySelector('canvas'),focus:document.activeElement===fixture.rapidInput,selection:[fixture.rapidInput.selectionStart,fixture.rapidInput.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop)}));
  assert.ok(kept.input&&kept.canvas&&kept.focus);assert.deepEqual(kept.selection,[1,3]);assert.deepEqual(kept.scroll,scroll);
  await page.evaluate(()=>fixture.rapidInput.dispatchEvent(new Event('input')));assert.equal(await page.evaluate(()=>fixture.rapidEvents),1);await stress.evaluate(n=>n.remove());
  const methods=calls.slice(count).filter(c=>c.action==='invoke').map(c=>c.method);assert.equal(methods.filter(x=>x==='CamPlan.Generate').length,1);assert.equal(methods.filter(x=>x==='CamRapid.Preview').length,1);assert.equal(methods.filter(x=>x==='CamRapid.Apply').length,1);assert.ok(!methods.includes('CamRapid.Generate'));
  const after=await read(),now=geometry(after),actual=now.transitions.find(t=>`${t.from}/${t.to}`===pair);assert.equal(actual.geometryStatus,'Generated');assert.equal(actual.validation,'Clear');assert.deepEqual(after.intentToolpaths,before.intentToolpaths);assert.deepEqual(after.fixtures,before.fixtures);assert.deepEqual(after.placements,before.placements);
  for(const t of now.transitions){const prior=old.transitions.find(p=>p.from===t.from&&p.to===t.to);if(!prior||t===actual||t.inputKey!==prior.inputKey)continue;assert.deepEqual(t.points,prior.points);assert.deepEqual(t.directions,prior.directions);}
  await page.screenshot({path:resolve(output,'current-rapid-updated.png')});
  const report={passed:true,pair,cuttingRequests:1,preserved:kept,cases:['right panel offers an enabled regenerate action for stale endpoints','updates prerequisite cutting then previews and commits only the selected rapid','keeps native authored poses and fixtures','preserves input, canvas, listeners, focus, selection and latest dual/nested scrolling'],coverage:{native:true,browser:'Edge',standaloneCef:false}};
  writeFileSync(resolve(output,'rapid-regenerate-report.json'),JSON.stringify(report,null,2));return report;
}
