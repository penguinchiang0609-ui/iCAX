import assert from 'node:assert/strict';
import { resolve } from 'node:path';

// Use the same production viewport and native scene as the placement suite.
// Helpers must stay fixed in WORLD coordinates, independently of manipulation.
export async function captureWorldFrame(page) {
  return page.evaluate(() => {
    const viewport = fixture.view.viewport;
    viewport.scene.updateMatrixWorld(true);
    return ['iCAX XY Workbench Grid', 'iCAX World XYZ',
      ...['X','Y','Z'].flatMap(axis => [`iCAX World ${axis}`, `iCAX World ${axis} Label`])]
      .map(name => {
        const object = viewport.scene.getObjectByName(name);
        if (!object) throw Error(`Missing world reference: ${name}`);
        // Axis presentation scales with zoom; its world direction/origin must
        // not change. Labels sit on the corresponding positive world ray.
        const round = values => values.map(value => Math.round(value * 1e9) / 1e9);
        if (name.includes('Grid')) return { name, matrix: round(object.matrixWorld.toArray()), visible: object.visible };
        const position = object.getWorldPosition(object.position.clone());
        const rotation = object.getWorldQuaternion(object.quaternion.clone());
        return { name, position: round(name.endsWith('Label') ? position.normalize().toArray() : position.toArray()),
          rotation: round(rotation.toArray()), visible: object.visible };
      });
  });
}

export async function verifyCenteredWorkbench({page, output, placement, sourceBounds}) {
  const before = await captureWorldFrame(page);
  assert.ok(before.every(object => object.visible));
  const result = await page.evaluate(() => {
    const viewport = fixture.view.viewport, grid = viewport.scene.getObjectByName('iCAX XY Workbench Grid');
    const positions = grid.geometry.getAttribute('position');
    const point = grid.position.clone(), gridBounds = [];
    for (let i = 0; i < positions.count; ++i) {
      point.fromBufferAttribute(positions, i).applyMatrix4(grid.matrixWorld);
      gridBounds.push(point.toArray());
    }
    const box = viewport.getVisibleBounds();
    return { gridBounds, box, content: viewport.getDebugState().contentBounds };
  });
  assert.ok(result.gridBounds.every(point => Math.abs(point[2]) < 1e-8), 'every grid vertex lies on world XY');
  assert.ok(result.box, 'native imported workpiece is visible');
  if (sourceBounds) {
    // Independently measured CAD bounds, not bounds reverse-engineered from the
    // placement. Trimmed-surface CAD bounds can be wider than display triangles.
    for (let axis = 0; axis < 3; ++axis) {
      const expected = -(sourceBounds.min[axis] + sourceBounds.max[axis]) / 2;
      assert.ok(Math.abs(placement.transform[axis * 4 + 3] - expected) < 1e-6, 'placement centers the source CAD bounds');
      assert.ok(result.box.min[axis] >= sourceBounds.min[axis] + expected - 0.15);
      assert.ok(result.box.max[axis] <= sourceBounds.max[axis] + expected + 0.15);
    }
  }
  result.box.min.forEach((min, axis) => {
    assert.ok(min < 0.15 && result.box.max[axis] > -0.15, 'displayed part surrounds the world origin');
    const low = Math.min(...result.gridBounds.map(point => point[axis]));
    const high = Math.max(...result.gridBounds.map(point => point[axis]));
    if (axis < 2) assert.ok(low < min && high > result.box.max[axis], 'XY workbench surrounds the centered workpiece');
  });
  // Fitting uses model extents only. Changing projection/transform space must not
  // attach the world helpers to the camera or to a selected workpiece.
  for (const mode of ['perspective', 'orthographic']) {
    await page.locator(`button[data-projection-mode="${mode}"]`).click();
    await page.evaluate(() => fixture.view.viewport.fitView());
    assert.deepEqual(await captureWorldFrame(page), before);
    await page.screenshot({ path: resolve(output, `workbench-centered-${mode}.png`) });
  }
  return before;
}
