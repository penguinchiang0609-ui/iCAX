import assert from "node:assert/strict";
import { clickPlaneCommand, ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";

// Commands and edits go through normal Ribbon/menu/form interactions. Queries
// only inspect the exact geometry and EC state produced by the native runtime.
export async function exerciseEditingMenus({ page, report, waitForAsync, passed }) {
  const model = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return structuredClone(getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam);
  });
  const geometry = id => page.evaluate(async id => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id });
  }, id);
  const settled = count => waitForAsync(async count => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);
    return !ui.busy && !ui.geometryTaskId && view.scene.iplaneCam.drawings.length === count;
  }, count);
  const choose = async id => {
    await page.keyboard.press("Escape");
    await ensurePlaneObjectsExpanded(page);
    await page.locator(`[data-iplane-object-id="${id}"]`).first().click();
  };
  const apply = async (operation, fields) => {
    await clickPlaneCommand(page, `plane.edit-${operation}`);
    for (const [key, value] of Object.entries(fields))
      await page.locator(`[data-iplane-form="drawing-edit"][data-iplane-form-field="${key}"]`).fill(String(value));
    await page.locator('[data-iplane-action="apply-drawing-edit"]').click();
  };
  const points = geometry => {
    assert.equal(geometry.curves.length, 1);assert.equal(geometry.curves[0].type, "line");
    return geometry.curves[0].points;
  };
  const nearPoint = (actual, expected) => {
    assert.equal(actual.length, expected.length);
    for (let axis = 0; axis < actual.length; axis++) assert.ok(Math.abs(actual[axis] - expected[axis]) < 1e-8, `${actual} != ${expected}`);
  };
  const undo = async count => {
    const button = page.locator('.title-bar [data-action="undo-project"]');
    await button.click();await settled(count);
  };

  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();
  assert.equal((await model()).drawings.length, 0);
  const input = page.locator("[data-iplane-command-input]");await input.fill("直线 10,20 14,20");await input.press("Enter");await settled(1);
  const source = (await model()).drawings[0], original = await geometry(source.id);
  nearPoint(points(original)[0], [10, 20]);nearPoint(points(original)[1], [14, 20]);
  await choose(source.id);await apply("copy", { dx: 10, dy: 5 });await settled(2);
  const copied = (await model()).drawings.find(row => row.id !== source.id);
  assert.ok(copied && copied.id !== source.id);const copiedGeometry = await geometry(copied.id);
  nearPoint(points(copiedGeometry)[0], [20, 25]);nearPoint(points(copiedGeometry)[1], [24, 25]);
  assert.deepEqual(await geometry(source.id), original);

  await choose(copied.id);await apply("mirror", { axis: "y", cx: 0, cy: 0 });
  await waitForAsync(async ({ id, reference }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);
    return !ui.busy && !ui.geometryTaskId && view.scene.iplaneCam.drawings.find(row => row.id === id)?.geometryRef.url !== reference.url;
  }, { id: copied.id, reference: copied.geometryRef });
  const mirrored = await geometry(copied.id);nearPoint(points(mirrored)[0], [-20, 25]);nearPoint(points(mirrored)[1], [-24, 25]);
  assert.deepEqual(await geometry(source.id), original);

  await choose(source.id);await apply("array-rectangular", { rows: 2, columns: 3, spacingX: 10, spacingY: 20 });await settled(7);
  const rectangularRows = (await model()).drawings.filter(row => row.id !== source.id && row.id !== copied.id);
  assert.equal(rectangularRows.length, 5);const rectangular = await Promise.all(rectangularRows.map(row => geometry(row.id)));
  const expectedRect = [[20, 20], [30, 20], [10, 40], [20, 40], [30, 40]];
  assert.deepEqual(rectangular.map(value => points(value)[0]).sort(), expectedRect.sort());
  for (const value of rectangular) { const pair = points(value);nearPoint(pair[1], [pair[0][0] + 4, pair[0][1]]); }
  assert.deepEqual(await geometry(source.id), original);assert.deepEqual(await geometry(copied.id), mirrored);
  await undo(2);assert.deepEqual((await model()).drawings.map(row => row.id).sort(), [source.id, copied.id].sort());

  await choose(source.id);await apply("array-polar", { count: 4, cx: 10, cy: 20, angleDeg: 360 });await settled(5);
  const polarRows = (await model()).drawings.filter(row => row.id !== source.id && row.id !== copied.id);
  assert.equal(polarRows.length, 3);const polar = await Promise.all(polarRows.map(row => geometry(row.id)));
  const expectedEnds = [[10, 24], [6, 20], [10, 16]], observed = polar.map(value => points(value));
  for (const pair of observed) nearPoint(pair[0], [10, 20]);
  for (const expected of expectedEnds) assert.ok(observed.some(pair => Math.hypot(pair[1][0] - expected[0], pair[1][1] - expected[1]) < 1e-8));
  assert.deepEqual(await geometry(source.id), original);assert.deepEqual(await geometry(copied.id), mirrored);
  await undo(2);assert.deepEqual((await model()).drawings.map(row => row.id).sort(), [source.id, copied.id].sort());
  const lifecycle = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
    return { busy: Boolean(ui.busy), geometryTaskId: ui.geometryTaskId || "", geometryTask: ui.geometryTask ?? null };
  });
  assert.deepEqual(lifecycle, { busy: false, geometryTaskId: "", geometryTask: null });
  report.editingMenu = { sourceId: source.id, copyId: copied.id, copiedPoints: points(copiedGeometry), mirroredPoints: points(mirrored), rectangularCount: rectangularRows.length, polarCount: polarRows.length, lifecycle };
  passed("Normal Ribbon menus and forms copy to a new EC, mirror exact coordinates and run both async arrays with correct counts, one-step undo and no remaining task guard");
}
