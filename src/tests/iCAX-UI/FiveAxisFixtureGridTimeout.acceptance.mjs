import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFixtureGridTimeout({page,rpc,info,output,modelPath,terminalDelayMs=16000}){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,undefined,{timeout:300000});
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await selectWorkpieceFile(page,modelPath||info.sourcePath);await idle();
  await page.waitForFunction(()=>!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  await command('five.setup.create');await idle();const before=await read();
  assert.equal(before.setups.length,1);
  assert.deepEqual(before.placements[0].transform.map(n=>n||0),[1,0,0,0,0,1,0,0,0,0,1,-35,0,0,0,1]);
  await command('five.fixture.generate');await idle();
  // The dense grid remains an explicit layout; the product default is skeleton.
  await page.locator('[data-five-fixture-field="design:pattern"]').selectOption('Grid');
  await page.locator('[data-five-fixture-field="design:spacing"]').fill('35');
  await page.locator('[data-five-command="five.fixture.design.auto"]').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces.length),46);
  assert.deepEqual(await page.evaluate(()=>({pattern:fixture.view.fixtureDesignDraft.pattern,spacing:fixture.view.fixtureDesignDraft.spacing,tenonRadius:fixture.view.fixtureDesignDraft.tenonRadius,windowMode:fixture.view.fixtureDesignDraft.windowMode})),{pattern:'Grid',spacing:35,tenonRadius:1,windowMode:'Automatic'});
  // Keep the real SDO client's ordinary 15-second budget. Delay only the
  // native terminal response so the long-operation budget is actually tested.
  await page.evaluate(delay=>{fixture.delayFixtureTerminalMs=delay;},terminalDelayMs);
  const started=Date.now();await page.locator('[data-five-command="five.fixture.design.generate"]').click();
  if(terminalDelayMs)await page.waitForFunction(()=>fixture.delayingFixtureTerminal,undefined,{timeout:300000});
  else await page.waitForFunction(()=>fixture.view.fiveAxisFixtureTask?.status==='Running',undefined,{timeout:300000});
  assert.equal(await page.evaluate(()=>fixture.sdoClient.timeoutMs),15000);
  assert.equal(await page.evaluate(()=>fixture.view.pending&&!!fixture.view.fixtureRequest),true);
  assert.equal(await page.locator('dialog.five-blocking-progress[open] [role="progressbar"]').isVisible(),true);
  assert.equal(await page.locator('.five-design-progress').count(),0);
  await page.screenshot({path:resolve(output,'grid-publishing.png')});
  await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const candidate=await page.evaluate(()=>fixture.view.fiveFixtureGenerated?.design.candidate);
  assert.ok(candidate.parts.length>=30);assert.equal(candidate.supportCheck.status,'Supported');
  const after=await read();assert.deepEqual(after.workpieces,before.workpieces);assert.deepEqual(after.placements,before.placements);assert.equal(after.fixtures.length,0);
  assert.equal(await page.locator('.five-design-progress').count(),0);
  const timings=await page.evaluate(()=>fixture.sdoTimings.filter(t=>t.method.startsWith('CamFixture.')));
  assert.equal(timings.filter(t=>t.method==='CamFixture.Generate').length,1);
  if(terminalDelayMs>15000)assert.ok(timings.some(t=>t.method==='CamFixture.QueryTask'&&t.elapsedMs>15000));
  await page.screenshot({path:resolve(output,'grid-generated.png')});
  return {cases:['The actual unregistered HatRail at Z=-35, 46 faces, explicit 35 mm grid and R1 tenons generates a supported candidate without changing CAD or adopting fixtures',terminalDelayMs?'The production SDO client retains its 15-second default while a native terminal response delayed beyond it succeeds under the fixture deadline; progress stays visible and only one generation is submitted':'The actual production SDO client receives the parallel candidate with one centered progress bar and one generation command, without an artificial response delay'],partCount:candidate.parts.length,totalGenerationMs:Date.now()-started,terminalDelayMs,longestNativeQueryMs:Math.max(...timings.filter(t=>t.method==='CamFixture.QueryTask').map(t=>t.nativeMs)),timings};
}
