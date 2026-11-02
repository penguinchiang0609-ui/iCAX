// CEF acceptance. Start with rapid-policy-ui.i5cam open in the rapid editor.
// rapid-policy-baseline.json is captured before the real Windows restore click.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {chromium} = require(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = path.resolve(process.env.ICAX_AUTOSAVE_OUTPUT || 'output/tests/machine-simulation');
const canonical = value => JSON.stringify(value);
(async () => {
  const browser = await chromium.connectOverCDP(process.env.ICAX_CEF_DEBUG_URL || 'http://127.0.0.1:9483');
  const page = browser.contexts()[0].pages()[0];
  page.setDefaultTimeout(60000);
  await page.evaluate(() => {
    window.policyGeometry = () => tv.scene.domain.setups.find(s => s.id === tv.scene.domain.activeSetupId).generatedGeometry;
    window.policySnapshot = () => policyGeometry().transitions.map(t => ({from:t.from,to:t.to,policy:t.policy,manualPath:t.manualPath,points:t.points,directions:t.directions,geometryStatus:t.geometryStatus,validation:t.validation}));
  });
  const field = name => page.locator(`[data-five-rapid-field="${name}"]`).last();
  const button = name => page.locator(`[data-five-command="five.rapid.${name}"]`).last();
  const idle = () => page.waitForFunction(() => !tv.pending && !tv.fiveHistoryBusy && !tv.fiveRapid.savingPath && !tv.fiveRapid.saveTimer && !tv.fiveRapid.policyChanged && !tv.fiveRapid.automaticChanged && !tv.fiveRapid.pathEdit?.changed);
  const snap = () => page.evaluate(() => policySnapshot());
  const nativeHistory = () => page.evaluate(() => tv.viewportSceneProxy.getUndoRedoState());
  async function verify3D() {
    const actual = await page.evaluate(() => ({
      expected: policyGeometry().transitions.filter(t => ['Generated','Draft'].includes(t.geometryStatus) && t.points?.length).map(t => t.points.flat()),
      lines: tv.fiveGeneratedPreview.group.children.filter(o => o.userData.generatedRapid).map(o => [...o.geometry.attributes.position.array]),
      error: tv.error
    }));
    assert.equal(actual.error, '');
    assert.equal(actual.lines.length, actual.expected.length);
    actual.lines.forEach((line,i) => {assert.equal(line.length,actual.expected[i].length);line.forEach((x,j) => assert.ok(Math.abs(x-actual.expected[i][j])<1e-4));});
  }
  async function history(direction, expected) {
    await page.getByRole('button',{name:direction==='undo'?'撤销':'重做',exact:true}).click();
    await page.waitForFunction(expected => !tv.fiveHistoryBusy && JSON.stringify(policySnapshot()) === expected, canonical(expected));
    await verify3D();
  }
  const baseline = JSON.parse(fs.readFileSync(path.join(output,'rapid-policy-baseline.json')));
  const states = [baseline.transitions];
  await idle();
  assert.equal((await snap())[0].manualPath, undefined);
  assert.notDeepEqual((await snap())[0].points, baseline.transitions[0].points);
  states.push(await snap());
  await verify3D();
  await history('undo',states[0]);
  assert.ok(await page.locator('[data-five-rapid-field="pathZ"]').count());
  await history('redo',states[1]);
  assert.equal(await page.locator('[data-five-rapid-field="pathZ"]').count(),0);
  console.log('PASS actual restore automatic, native undo/redo, manual editor restoration and 3D');
  for (const [name,value] of [['strategy','Frog'],['liftHeightMm','18'],['marginMm','0.65'],['strategy','Quick']]) {
    if(name==='strategy') await field(name).selectOption(value);
    else {await field(name).fill(value);await field(name).press('Tab');}
    await page.waitForFunction(({name,value}) => String(policyGeometry().transitions[0].policy[name]) === value && !tv.fiveRapid.savingPath && !tv.fiveRapid.policyChanged,{name,value});
    assert.equal(await field(name).inputValue(),value);
    states.push(await snap());
    await verify3D();
  }
  for(let i=states.length-2;i>=0;i--) await history('undo',states[i]);
  for(let i=1;i<states.length;i++) await history('redo',states[i]);
  console.log('PASS four continuous policy edits and complete undo/redo across manual/automatic mode');
  await button('path-edit').click();
  await page.waitForFunction(() => tv.fiveRapid.pathEdit);
  await field('pathZ').fill('37');
  await idle();
  const manual = await snap();
  for(const value of ['','-','abc','1e999','100000001']) {
    const beforeHistory = await nativeHistory();
    await field('pathZ').fill(value);
    await page.waitForFunction(() => tv.fiveRapid.pathEdit.changed);
    await page.getByRole('button',{name:'撤销',exact:true}).click();
    await idle();
    assert.equal(canonical(await snap()),canonical(manual));
    assert.equal(canonical(await nativeHistory()),canonical(beforeHistory));
    assert.equal(await field('pathZ').inputValue(),'37');
    await verify3D();
  }
  await history('undo',states.at(-1));
  const redoHistory = await nativeHistory();
  assert.equal(redoHistory.canRedo,true);
  await button('path-edit').click();
  await field('pathZ').fill('-');
  await page.getByRole('button',{name:'撤销',exact:true}).click();
  await idle();
  assert.equal(canonical(await nativeHistory()),canonical(redoHistory));
  await history('redo',manual);
  assert.equal(await field('pathZ').inputValue(),'37');
  console.log('PASS invalid coordinates discard only text; prior undo and redo remain intact');
  await page.screenshot({path:path.join(output,'rapid-policy-ui.png')});
  fs.writeFileSync(path.join(output,'rapid-policy-ui-report.json'),JSON.stringify({passed:true,standaloneCef:true,restoreAutomatic:true,continuousPolicyEdits:4,invalidInputs:5,redoPreserved:true,renderedGeometryMatches:true,path:baseline.path,states,final:await snap()},null,2));
  process.exit(0);
})().catch(error => {console.error(error);process.exit(1);});
