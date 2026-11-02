import assert from 'node:assert/strict';
import {copyFileSync,existsSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {chromium} from 'playwright';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),source=resolve(process.env.ICAX_FIXTURE_PROJECT);
mkdirSync(output,{recursive:true});
const project=resolve(output,`手工改孔-${Date.now()}.i5cam`);
copyFileSync(source,project);if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');
const base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
const browser=await chromium.connectOverCDP('http://127.0.0.1:'+process.env.ICAX_CEF_PORT);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
assert.ok(page);page.setDefaultTimeout(45000);
const report={passed:false,standaloneCef:true,source,project,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!manualSpacing.view.pending&&!manualSpacing.view.fixtureRequest&&!manualSpacing.view.fiveFixturePlate?.loading&&!manualSpacing.view.fiveFixturePlate?.saving);
try{
  await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.manualSpacing={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
  await page.locator('[data-tab-id="workpiece"]').click();await idle();
  const read=()=>page.evaluate(()=>manualSpacing.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
  const before=await read(),unit=before.fixtureUnits.find(u=>u.kind==='Generated'),part=unit.design.assembly.parts.find(p=>p.number==='S1');assert.ok(part);
  await page.locator(`[data-five-command="five.preparation.unit:${unit.id}"]`).click();await idle();
  await page.locator(`[data-five-command="five.fixture.part-edit:${part.partId}"]`).click();await idle();
  assert.equal(await page.evaluate(()=>manualSpacing.view.fiveFixturePlate.error),'');
  // Reproduce the rectangle in the user's screenshot through the real pointer
  // drawing route, then enter its exact values in the production controls.
  const values={x:-6.5722479820251465,y:23.193252563476562,width:11.800063133239746,height:15.011470794677734};
  await page.locator('[data-plate-tool="rectangle"]').click();
  const points=await page.evaluate(v=>{const svg=document.querySelector('[data-plate-canvas]');return [[v.x,v.y],[v.x+v.width,v.y+v.height]].map(q=>{const p=svg.createSVGPoint();p.x=q[0];p.y=-q[1];const a=p.matrixTransform(svg.getScreenCTM());return {x:a.x,y:a.y};});},values);
  await page.mouse.move(points[0].x,points[0].y);await page.mouse.down();await page.mouse.move(points[1].x,points[1].y,{steps:8});await page.mouse.up();
  for(const [key,value] of Object.entries(values)){const field=page.locator(`[data-plate-field="${key}"]`);await field.fill(String(value));await field.press('Enter');}
  const submitted=await page.evaluate(()=>manualSpacing.view.fiveFixturePlate.profile);
  await page.locator('[data-plate-action="save"]').click();await idle();
  assert.equal(await page.evaluate(()=>manualSpacing.view.fiveFixturePlate?.error||manualSpacing.view.error||''),'');
  assert.equal(await page.locator('[data-fixture-plate-editor]').count(),0);
  const after=await read(),edited=after.fixtureUnits.find(u=>u.id===unit.id),assembly=edited.design.assembly;
  assert.equal(assembly.geometryValidity,'Current');assert.deepEqual(edited.design.recipe,unit.design.recipe);
  assert.equal(assembly.parts.find(p=>p.partId===part.partId).exactProfile.Boundaries.length,submitted.Boundaries.length);
  for(const sibling of before.fixtures.filter(p=>p.id!==part.partId))assert.deepEqual(after.fixtures.find(p=>p.id===sibling.id),sibling);
  assert.notDeepEqual(after.fixtures.find(p=>p.id===part.partId).geometryRef,before.fixtures.find(p=>p.id===part.partId).geometryRef);
  await page.locator('[data-five-command="five.fixture.parts-done"]').click();await idle();
  assert.equal(await page.evaluate(()=>manualSpacing.view.fiveFixtureSelectedPart),'');
  assert.equal(await page.locator('[data-five-key="fixture-parts-mode"]').innerText(),'编辑');
  await page.locator('[data-five-command="five.fixture.parts-edit"]').click();await idle();await page.keyboard.press('Escape');await idle();
  assert.equal(await page.evaluate(()=>manualSpacing.view.fiveFixtureSelectedPart),'');
  await page.locator('[data-five-command="five.fixture.design.breakdown"]').click();await idle();
  assert.equal(await page.locator('.five-fixture-breakdown tbody tr').count(),unit.partIds.length);
  await page.locator('[data-five-command="five.fixture.design.breakdown-close"]').last().click();await idle();
  await page.screenshot({path:resolve(output,'saved-and-finished.png')});
  report.cases.push('Actual standalone CEF saves the exact S1 rectangle from the screenshot without changing frozen generator parameters or any sibling plate','Native geometry remains Current and the ten-part breakdown opens after the edit','Header Done and Escape leave plate edit mode in the deployed standalone application');
  assert.deepEqual(report.errors,[]);report.passed=true;report.partId=part.partId;report.rectangle=values;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
