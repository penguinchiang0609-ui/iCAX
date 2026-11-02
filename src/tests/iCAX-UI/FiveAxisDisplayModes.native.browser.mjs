import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyDisplayModes({ page, rpc, calls, output }) {
  const state = () => rpc({ action: 'invoke', scope: 'scene', method: 'CamScene.ReadState', payload: {} });
  const before = await state();
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
  const count = calls.length;
  const setMode = async mode => {
    await page.getByRole('combobox', { name: '显示模式' }).selectOption(mode);
    await page.waitForFunction(mode => fixture.view.viewport.displayMode === mode, mode);
  };
  const initial = await page.evaluate(() => {
    const v = fixture.view.viewport, mesh = [...v.sceneObjects.values()].find(o => o.isMesh);
    fixture.displayBaseline = { canvas: v.renderer.domElement, mesh, geometry: mesh.geometry, camera: v.getCameraState(),
      tree: document.querySelector('.five-cut-tree'), cuts: fixture.view.fiveCutPreview.group };
    return { edges: mesh.geometry.userData.topologyEdgeCount,
      all: mesh.geometry.userData.topologyEdges.length, feature: mesh.geometry.userData.topologyFeatureEdges.length };
  });
  assert.equal(initial.edges, 7851, 'every non-degenerate automotive CAD edge is present after save/reopen');
  assert.ok(initial.all > initial.feature && initial.feature > 0, 'smooth patch seams classified separately');
  const modes = page.getByRole('combobox', { name: '显示模式' });
  assert.equal(await modes.inputValue(), 'shadedEdges');
  const report = [];
  for (const mode of ['shaded', 'shadedEdges', 'wireframe']) {
    await setMode(mode);
    const result = await page.evaluate(() => {
      const v = fixture.view.viewport, base = fixture.displayBaseline, mesh = base.mesh;
      return { camera: v.getCameraState(), previousCamera: base.camera,
        sameCanvas: base.canvas === v.renderer.domElement, sameMesh: [...v.sceneObjects.values()].includes(mesh),
        sameGeometry: mesh.geometry === base.geometry, sameTree: base.tree === document.querySelector('.five-cut-tree'),
        sameCuts: base.cuts === fixture.view.fiveCutPreview.group, visible: mesh.visible,
        surfaceVisible: mesh.material.visible, edgeMode: mesh.userData.meshEdges?.userData.edgeMode ?? '',
        edgeVertices: mesh.userData.meshEdges?.geometry.getAttribute('position').count ?? 0,
        cuts: fixture.view.fiveCutPreview.group.children.filter(n => n.userData.cuttingLineId).length };
    });
    assert.deepEqual(result.camera, result.previousCamera);
    for (const k of ['sameCanvas', 'sameMesh', 'sameGeometry', 'sameTree', 'sameCuts', 'visible']) assert.equal(result[k], true, k);
    assert.equal(result.surfaceVisible, mode !== 'wireframe');
    assert.equal(result.edgeMode, mode === 'shaded' ? '' : 'cad-all');
    assert.equal(result.edgeVertices,mode === 'shaded'?0:initial.all/3);
    assert.equal(result.cuts, 5);
    report.push({ mode, edgeVertices: result.edgeVertices, cuts: result.cuts });
    await page.screenshot({ path: resolve(output, `automotive-${mode}.png`) });
  }
  await setMode('shadedEdges');
  assert.equal(await page.getByRole('checkbox', { name: '曲面拼接边' }).count(),0);
  assert.deepEqual(calls.slice(count).filter(c => !(c.action === 'resource' && c.head)), [],
    'viewport switches must not invoke SDOs or fetch geometry');
  assert.deepEqual(await state(), before);
  // Mode persists through product area remount; the following face/start tests run in wireframe.
  await setMode('wireframe');
  await page.evaluate(() => fixture.context.actions.selectRibbonTab('workpiece'));
  await page.waitForFunction(() => !fixture.view.pending);
  await page.evaluate(() => fixture.context.actions.selectRibbonTab('toolpath'));
  await page.waitForFunction(() => !fixture.view.pending);
  assert.equal(await modes.inputValue(), 'wireframe');
  assert.equal(await page.evaluate(() => fixture.view.viewport.getDebugState().displayMode), 'wireframe');
  assert.equal(await page.evaluate(() => [...fixture.view.viewport.sceneObjects.values()].every(o => o.visible && o.userData.meshEdges?.visible)), true,
    'workpiece and CAD wireframe remain visible after switching work areas');
  writeFileSync(resolve(output, 'display-modes-report.json'), JSON.stringify({ passed: true, ...initial, modes: report,
    coverage: { native: 'production BRep display service and embedded resources', browser: 'Edge', standaloneCef: false } }, null, 2));
}
