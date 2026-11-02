import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

// STEP sources are standard XDE fixtures emitted by FiveAxisCadVisibility.
export async function verifyVisibilityImport({ page, rpc, root, output, errors }) {
  page.setDefaultTimeout(120000);
  const fixtures = resolve(root, 'output/tests/cad-source-visibility');
  const cases = [
    { source: resolve(root, 'TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs'), faces: 4, edges: 536 },
    { source: resolve(fixtures, 'StepHiddenObjectAndHiddenLayerAreExcludedBeforeBounds.step'), faces: 6, edges: 12 },
    { source: resolve(fixtures, 'StepHiddenOccurrenceDoesNotHideVisibleUseOfSamePart.step'), faces: 6, edges: 12 },
    { source: resolve(fixtures, 'StepHiddenParentExcludesWholeNestedAssembly.step'), faces: 6, edges: 12 },
    { source: resolve(fixtures, 'StepHiddenFaceKeepsRemainingSurfaceModelValid.step'), faces: 5, edges: 12 },
    { source: resolve(fixtures, 'HiddenStepPresentationExcludesItsStyledMembers.step'), faces: 6, edges: 12 },
    { source: resolve(fixtures, 'StepPartOnVisibleAndHiddenLayersRemainsVisible.step'), faces: 6, edges: 12 },
  ];
  const read = () => rpc({ action: 'invoke', scope: 'scene', method: 'CamScene.ReadState', payload: {} });
  const results = [];
  for (const [index, sample] of cases.entries()) {
    const hash = () => createHash('sha256').update(readFileSync(sample.source)).digest('hex');
    const sourceHash = hash();
    await selectWorkpieceFile(page, sample.source, { preview: true });
    await page.waitForFunction(() => !fixture.view.pending && !fixture.view.fiveAxisImportActive);
    assert.equal(await page.evaluate(() => fixture.view.error), '');
    const state = await read(), workpiece = state.workpieces.at(-1), placement = state.placements.at(-1);
    assert.equal(state.workpieces.length, index + 1);
    assert.equal(workpiece.importMetadata.validBRep, true);
    const inspection = await rpc({ action: 'invoke', scope: 'scene', method: 'CamWorkpiece.Inspect',
      payload: { workpieceId: workpiece.id } });
    assert.equal(inspection.cadInspection.faceCount, sample.faces);
    assert.equal(inspection.cadInspection.edgeCount, sample.edges);
    const selectable = await rpc({ action: 'invoke', scope: 'scene', method: 'CamCut.QuerySources',
      payload: { expected: state.expected, setupId: state.activeSetupId, placementId: placement.id } });
    assert.equal(selectable.faceUses.length, sample.faces, 'hidden source faces must not be selectable for CAM');
    await page.waitForFunction(() => fixture.view.viewport?.getDebugState()?.objectCount > 0);
    const display = await page.evaluate(() => fixture.view.viewport.getDebugState());
    assert.ok(display.contentBounds);
    assert.equal(hash(), sourceHash, 'import must not edit the source file');
    await page.screenshot({ path: resolve(output, `visible-import-${index}.png`) });
    results.push({ ...sample, sourceHash, inspection, selectableFaces: selectable.faceUses.length, display });
  }
  const before = await read();
  assert.equal((await rpc({ action: 'undo' })).state.workpieces.length, cases.length - 1);
  assert.equal((await rpc({ action: 'redo' })).state.workpieces.length, cases.length);
  await page.evaluate(() => fixture.entry.releaseProject(fixture.context));
  const reopened = await rpc({ action: 'save-reopen' });
  assert.deepEqual(reopened.workpieces, before.workpieces);
  assert.deepEqual(reopened.placements, before.placements);
  for (let i = 0; i < cases.length; i++) {
    const inspection = await rpc({ action: 'invoke', scope: 'scene', method: 'CamWorkpiece.Inspect',
      payload: { workpieceId: reopened.workpieces[i].id } });
    assert.equal(inspection.cadInspection.faceCount, cases[i].faces);
  }
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, 'visibility-import-report.json'), JSON.stringify({ passed: true, cases: results,
    undoRedo: true, saveReopen: true,
    coverage: 'Production file picker preview and import, real bounded SDO, native CAD/View resources, CAM face selection, undo/redo and save/reopen in Edge. Standalone CEF not covered.' }, null, 2));
}
