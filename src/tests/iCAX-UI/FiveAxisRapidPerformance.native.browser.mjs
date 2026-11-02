import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

export async function verifyRapidPerformance({page,command,read,rpc,output}) {
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveGeneration?.running);
  const geometry=s=>s.setups.find(t=>t.id===s.activeSetupId).generatedGeometry;
  const initial=await read();assert.equal(geometry(initial).operations.length,19);
  await page.evaluate(()=>fixture.rapidNodes={canvas:fixture.view.viewport.renderer.domElement,model:[...fixture.view.viewport.sceneObjects.values()][0]});
  await command('five.tool.settings');
  await page.locator('[data-five-rapid-field="strategy"]').selectOption('Frog');
  await page.locator('[data-five-rapid-field="marginMm"]').fill('0.2');
  await page.locator('[data-five-rapid-field="liftHeightMm"]').fill('10');
  await page.locator('[data-five-command="five.rapid.save-settings"]').click();await idle();
  await page.evaluate(()=>{fixture.holdMethod='CamRapid.Generate';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.tool.generate');});
  await page.waitForFunction(()=>fixture.held);
  assert.equal(await page.locator('.five-computation-progress').isVisible(),true);
  assert.equal(await page.locator('.five-generation-status').count(),0,'old 18/18 is not presented as current progress');
  const started=performance.now();await page.evaluate(()=>fixture.release());await idle();
  const elapsedMs=performance.now()-started,done=await read(),g=geometry(done);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(g.transitions.length,18);
  assert.ok(g.transitions.every(t=>t.validation==='Clear'&&t.strategy==='Frog'));
  assert.ok(Math.max(...g.transitions.at(-1).heightsMm)>11,'the lower-rim hop raises above the obstructed minimum');
  await page.screenshot({path:resolve(output,'disk-frog-generated.png')});
  const repeatedAt=performance.now();await page.locator('[data-five-command="five.generate.apply"]').click();await idle();
  const repeatMs=performance.now()-repeatedAt,task=await page.evaluate(()=>fixture.view.fiveRapidBatch.task);
  assert.equal(task.result.reusedCount,'18');assert.equal(task.result.calculatedCount,'0');assert.deepEqual(geometry(await read()),g);
  assert.equal(await page.evaluate(()=>fixture.rapidNodes.canvas===fixture.view.viewport.renderer.domElement&&fixture.rapidNodes.model===[...fixture.view.viewport.sceneObjects.values()][0]),true);
  await rpc({action:'export-project',path:resolve(output,'disk-5x-frog-verified.i5cam')});
  writeFileSync(resolve(output,'rapid-performance-report.json'),JSON.stringify({passed:true,elapsedMs,repeatMs,clear:18,reused:18,lastHopHeight:Math.max(...g.transitions.at(-1).heightsMm),cases:['Frog selected and saved in production UI','five-times disk all 19 operations and 18 clear hops','automatic safe height for the lower rim','repeat generates zero geometry queries','previous result hidden while running','canvas and model nodes retained'],coverage:{native:true,browser:'Edge',standaloneCef:false}},null,2));
}
