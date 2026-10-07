import assert from 'node:assert/strict';

// Save a real branch record while its latest actual section callback is held.
// The fixture scene is isolated from the desktop user's document.
export async function runPunchBranchApplyBrowserChecks(page,{ready,screenshot,report}) {
  const definition=page.locator('[data-punch-batch-definition-editor]');
  await page.locator('[data-tube-designer-nesting-punch-field="profileKey"]').selectOption('system:rect');await ready();
  await definition.locator('[data-cam-change-action$="record-kind-change"]').selectOption('branch');await ready();
  await definition.locator('[data-cam-change-action$="profile-select"]').selectOption('system:round');await ready();
  const width=definition.locator('[data-tube-designer-punch-profile-parameter="width"]');
  await width.fill('12');await width.press('Tab');await ready();
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-add"]').click();await ready();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.preview.toolCount),1);
  await page.evaluate(()=>{const f=window.fixture;f.holdActualProfileReplies=true;f.branchApplyCanvas=f.viewport.renderer.domElement;});
  await width.fill('14');await width.press('Tab');await width.focus();
  await page.waitForFunction(()=>window.fixture.actualProfileHolds?.length===1);
  assert(await width.evaluate(node=>!node.disabled&&node===document.activeElement));
  await page.locator('.punch-batch-main-actions [data-cam-action$="-apply"]').click();
  await page.waitForFunction(()=>window.fixture.view.pending);
  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(c=>c.method.endsWith('AddNestingPunchPart')).length),0,
    'Confirm waits for the actual current section instead of submitting null or stale contours');
  await page.evaluate(()=>{const f=window.fixture;f.holdActualProfileReplies=false;f.actualProfileHolds[0].release();});
  await page.waitForFunction(()=>!window.fixture.pending&&!window.fixture.view.pending&&!window.fixture.view.tubeDesignerPunchBatch);
  const receipt=await page.evaluate(()=>{
    const f=window.fixture,creates=f.calls.filter(c=>c.method.endsWith('AddNestingPunchPart'));
    return {creates:creates.map(c=>({scope:c.scope,payload:c.payload})),holds:f.actualProfileHolds.map(h=>({parameters:h.parameters,nativeMilliseconds:h.nativeMilliseconds,released:h.released})),errors:f.errors};
  });
  assert.equal(receipt.creates.length,1);assert.equal(receipt.creates[0].scope,'scene');assert.deepEqual(receipt.errors,[]);
  const part=receipt.creates[0].payload;
  assert.equal(part.features.length,1);assert.equal(part.features[0].section.parameters.width,14);
  assert.equal(part.features[0].section.profile.width,14);assert(part.features[0].section.profile.contours.length>0);
  report.branchApply=receipt;await screenshot('branch-confirm-current-section');
  report.checks.push('Confirm during an actual pending branch-section generation locks first, awaits the latest contour, and creates exactly one current part with width 14 through the scene channel');
}
