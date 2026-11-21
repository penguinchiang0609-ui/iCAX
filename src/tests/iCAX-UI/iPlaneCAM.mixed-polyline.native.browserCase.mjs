import assert from "node:assert/strict";
import { existsSync, mkdirSync, readFileSync, statSync, unlinkSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { clickPlaneCommand, ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
import { assertDrawingMenu } from "./iPlaneCAM.drawingMenuAssertions.mjs";

// Every geometry mutation below goes through an actual AppShell control or
// physical keyboard/mouse event. Native calls are read-only assertions.
export async function exerciseMixedPolyline({ page, report, assets, source, output, savedProject, waitForAsync, passed }) {
  await ensurePlaneObjectsExpanded(page);
  const input = page.locator("[data-iplane-command-input]");
  const model = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return (await getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneScene.ReadState", {})).iplaneCam;
  });
  const state = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI, preview = view.iplaneDrawingPreview;
    return { tool: ui.activeTool, mode: ui.polylineMode, points: structuredClone(ui.commandPoints), segments: structuredClone(ui.polylineSegments ?? []), pending: structuredClone(ui.polylinePending ?? []), error: ui.operationError, auxiliary: structuredClone(ui.auxiliaryDimensions ?? []), preview: { visible: preview?.group.visible, segmentTypes: preview?.group.userData.segmentTypes, controlPointCount: preview?.group.userData.controlPointCount, positions: Array.from(preview?.outline.geometry.attributes.position?.array ?? []) } };
  });
  const geometry = (id, sketch = false) => page.evaluate(async ({ id, sketch }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke(sketch ? "PlaneSketch.ReadGeometry" : "PlaneDrawing.ReadGeometry", { id });
  }, { id, sketch });
  const settled = () => waitForAsync(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);
    return !ui.busy && !ui.drawingCommitting && !ui.geometryTaskId;
  });
  const command = async text => {
    await page.locator('[data-bottom-dock-tab="command"]').click();await input.fill(text);await input.press("Enter");
    await waitForAsync(async () => {
      const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
      const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
      if (ui.operationError) throw new Error(ui.operationError);
      return !ui.busy && !ui.drawingCommitting && !ui.geometryTaskId && ui.commandInput === "";
    });
  };
  const focusCanvas = async () => page.locator(".icax-three-viewport canvas").focus();
  const key = async value => { await focusCanvas();await page.keyboard.press(value); };
  const mode = async (kind, shortcut) => {
    if (shortcut) await key(shortcut);else await page.locator(`[data-iplane-polyline-kind="${kind}"]`).click();
    await waitForAsync(async kind => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.polylineMode === kind; }, kind);
    assert.equal(await page.locator(`[data-iplane-polyline-kind="${kind}"]`).getAttribute("aria-pressed"), "true");
  };
  const screenPoint = point => page.evaluate(async point => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { Vector3 } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const vp = getProjectView(__icaxAppShell.getState().activeProjectId).viewport, box = vp.root.querySelector("canvas").getBoundingClientRect();
    vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
    const p = new Vector3(point[0], point[1], 0).applyMatrix4(vp.content.matrixWorld).project(vp.camera);
    return { x: box.left + (p.x + 1) * box.width / 2, y: box.top + (1 - p.y) * box.height / 2 };
  }, point);
  const mousePreview = async point => { const p = await screenPoint(point);await page.mouse.move(p.x, p.y);await page.waitForTimeout(50);return state(); };
  const context = async action => {
    const box = await page.locator(".icax-three-viewport canvas").boundingBox();await page.mouse.click(box.x + box.width * .6, box.y + box.height * .55, { button: "right" });
    const menu = page.locator("[data-iplane-context-menu]");await menu.waitFor();
    const drawing = await state();await assertDrawingMenu(menu, { polyline: true, splinePending: drawing.mode === "bspline" && drawing.pending.length > 0 });
    assert.equal(await page.locator('.cam-info-pane [data-iplane-action="finish-drawing"],.cam-info-pane [data-iplane-action="close-drawing"],.cam-info-pane [data-iplane-action="cancel-drawing"]').count(), 0);
    if (action) { const button = menu.locator(`[data-iplane-action="${action}-drawing"]`);assert.equal(await button.isEnabled(), true);await button.click();await settled(); }
    return menu;
  };
  const only = value => { assert.equal(value.curves.length, 1);assert.equal(value.curves[0].type, "polycurve");assert.equal("points" in value.curves[0], false);return value.curves[0]; };
  const checkFive = value => {
    const curve = only(value);assert.deepEqual(curve.segments.slice(0, 5).map(s => s.type), ["line", "arc", "ellipseArc", "bezier", "bspline"]);
    assert.equal(curve.segments[3].controlPoints.length, 4);assert.equal(curve.segments[4].controlPoints.length, 6);assert.equal(curve.segments[4].degree, 3);assert.equal(curve.segments[4].knots.length, 10);
    return curve;
  };
  const drawFive = async ({ finish = true, closed = false, inspect = false } = {}) => {
    await clickPlaneCommand(page, "plane.draw-polyline");await command("0,0");await command("20,0");
    await mode("arc");await command(`${20 + Math.sqrt(50)},${10 - Math.sqrt(50)}`);
    if (inspect) {
      const prefix = await state();assert.equal(await page.locator('[data-iplane-polyline-kind="ellipseArc"]').isEnabled(), false);
      const rejected = await state();assert.equal(rejected.mode, "arc");assert.deepEqual(rejected.points, prefix.points);assert.deepEqual(rejected.segments, prefix.segments);assert.deepEqual(rejected.pending, prefix.pending);
      const menu = await context();assert.equal(await menu.locator('[data-iplane-action="finish-drawing"]').isEnabled(), false);assert.equal(await menu.locator('[data-iplane-action="close-drawing"]').isEnabled(), false);
      // A physical outside click dismisses the menu without accepting a point.
      await page.locator('[data-bottom-dock-tab="command"]').click();await key("Backspace");assert.equal((await state()).pending.length, 0);
      await command(`${20 + Math.sqrt(50)},${10 - Math.sqrt(50)}`);
      const preview = await mousePreview([30, 10]);assert.equal(preview.preview.visible, true);assert.ok(preview.preview.positions.length > 12);assert.deepEqual(preview.preview.segmentTypes.slice(0, 2), ["line", "arc"]);
      const radius = preview.auxiliary.find(d => d.key === "radius");assert.ok(radius && Math.abs(radius.value - 10) < 1e-4);
      await page.screenshot({ path: resolve(output, "mixed-circular-preview.png") });
    }
    await command("30,10");await mode("ellipseArc", "e");await command("20,10");await command("20,15");
    if (inspect) {
      const prefix = await state();await input.fill("0,0");await input.press("Enter");
      await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return Boolean(getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.operationError); });
      const rejected = await state();assert.deepEqual(rejected.points, prefix.points);assert.deepEqual(rejected.pending, prefix.pending);assert.deepEqual(rejected.segments, prefix.segments);assert.match(rejected.error, /椭圆/);
    }
    await command("20,15");await mode("bezier");await command("15,15");await command("15,25");
    if (inspect) {
      const preview = await mousePreview([10, 25]);assert.ok(preview.preview.positions.length > 100);assert.equal(preview.preview.segmentTypes.at(-1), "bezier");assert.ok(preview.preview.controlPointCount >= 4);
    }
    await command("10,25");await mode("bspline", "s");
    for (const p of ["5,25", "5,30", "0,25", "-5,30", "-10,30"]) await command(p);
    assert.equal((await state()).pending.length, 5);
    if (inspect) {
      const preview = await mousePreview([-15, 35]);assert.equal(preview.preview.segmentTypes.at(-1), "bspline");assert.ok(preview.preview.positions.length > 100);
      await key("Space");assert.equal((await state()).segments.length, 5);assert.equal((await state()).pending.length, 0);
      await key("Backspace");assert.equal((await state()).pending.length, 5);assert.equal((await state()).segments.length, 4);
      await key("Control+z");assert.equal((await state()).pending.length, 4);await command("-10,30");await key("Space");
      await mode("line", "l");const tangent = await mousePreview([-10, 40]), angle = tangent.auxiliary.find(d => d.key === "includedAngleDeg");
      assert.ok(angle && Math.abs(angle.value - 90) < 1e-3, `B-spline endpoint tangent should give 90 degrees, got ${angle?.value}`);
      const row = page.locator('[data-iplane-dimension-row="includedAngleDeg"]');assert.equal(await row.isVisible(), true);assert.equal(await row.locator("input").isVisible(), false);
      report.mixedTangent = angle;await page.screenshot({ path: resolve(output, "mixed-bspline-tangent.png") });
    }
    if (finish) await context(closed ? "close" : "finish");
  };

  const initial = await model();assert.equal(initial.drawings.length, 0);
  const drawLayer = [...initial.layers].sort((a, b) => a.order - b.order)[2].id;
  await page.locator(`[data-layer-ui-action="choose"][data-layer-ui-id="${drawLayer}"]`).click();await settled();
  await drawFive({ inspect: true });
  let data = await model();assert.equal(data.drawings.length, 1);const drawing = data.drawings[0], exact = await geometry(drawing.id), curve = checkFive(exact);
  assert.equal(curve.segments.length, 5);assert.equal(curve.closed, false);assert.equal(drawing.layerId, drawLayer);assert.equal((await state()).tool, "plane.select");
  assert.deepEqual(curve.segments[4].controlPoints, [[10, 25], [5, 25], [5, 30], [0, 25], [-5, 30], [-10, 30]]);
  report.exactFive = exact;
  assert.ok(Object.keys(assets).some(path => path.endsWith("/SDK/Cad/planarCurveSegments.mjs")));
  passed("Real controls, shortcuts, coordinate entry and mouse preview create one native EC with all five exact curve types and a genuine six-pole cubic B-spline; incomplete/off-ellipse input preserves the prefix");
  passed("Backspace and Ctrl+Z restore pending controls; a sampled curve preview and read-only included angle use the preceding B-spline endpoint tangent");

  await page.locator('.title-bar [data-action="undo-project"]').click();await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam.drawings.length === 0; });
  await page.locator('.title-bar [data-action="redo-project"]').click();await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam.drawings.some(row => row.id === id); }, drawing.id);await settled();assert.deepEqual(await geometry(drawing.id), exact);
  await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${drawing.id}"]`).click();await command("移动 10,-5");const moved = await geometry(drawing.id);assert.equal((await model()).drawings[0].layerId, drawLayer);checkFive(moved);assert.notDeepEqual(moved.geometryRef, exact.geometryRef);
  await page.locator('.title-bar [data-action="undo-project"]').click();await waitForAsync(async ({ id, ref }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam.drawings.find(row => row.id === id)?.geometryRef.url === ref.url; }, { id: drawing.id, ref: exact.geometryRef });await settled();assert.deepEqual(await geometry(drawing.id), exact);
  passed("Framework history retains the single curve identity and exact mixed payload; normal transform commands preserve curve types and the chosen layer");

  await clickPlaneCommand(page, "plane.draw-polyline");await command("50,0");await mode("arc", "a");await command("60,10");await command("50,20");await context("close");
  data = await model();assert.equal(data.drawings.length, 2);const closedDrawing = data.drawings.find(row => row.id !== drawing.id), closedExact = await geometry(closedDrawing.id), closedCurve = only(closedExact);
  assert.equal(closedCurve.closed, true);assert.deepEqual(closedCurve.segments.map(s => s.type), ["arc", "line"]);assert.deepEqual(closedCurve.segments[1].points, [[50, 20], [50, 0]]);assert.equal(closedDrawing.canClosePolyline, false);
  await clickPlaneCommand(page, "plane.draw-polyline");await command("70,0");await mode("bspline", "s");for (const p of ["75,5", "80,-5", "85,5", "90,-5", "95,0"]) await command(p);await context("finish");
  data = await model();assert.equal(data.drawings.length, 3);const splineDrawing = data.drawings.find(row => row.id !== drawing.id && row.id !== closedDrawing.id), splineExact = await geometry(splineDrawing.id);
  assert.equal(only(splineExact).segments.length, 1);assert.equal(only(splineExact).segments[0].controlPoints.length, 6);
  const directionalEllipses = [];
  for (const [endpoint, expectedSweep] of [["100,-5", -90], ["100,5", -270]]) {
    const beforeIds = new Set((await model()).drawings.map(row => row.id));
    await clickPlaneCommand(page, "plane.draw-polyline");await command("110,0");await mode("ellipseArc", "e");await command("100,0");await command("100,-5");
    if (expectedSweep === -90) { const p = await screenPoint([100, -5]);await page.mouse.move(p.x, p.y);await page.mouse.click(p.x, p.y); }
    else await command(endpoint);
    await context("finish");
    const row = (await model()).drawings.find(row => !beforeIds.has(row.id)), exactEllipse = await geometry(row.id), segment = only(exactEllipse).segments[0];
    assert.equal(only(exactEllipse).segments.length, 1);assert.equal(segment.type, "ellipseArc");assert.equal(segment.radiusX, 10);assert.equal(segment.radiusY, 5);assert.ok(Math.abs(segment.sweepAngleDeg - expectedSweep) < (expectedSweep === -90 ? 1e-3 : 1e-9));
    directionalEllipses.push(row);report.clockwiseEllipses ??= [];report.clockwiseEllipses.push(exactEllipse);
  }
  passed("Selecting the negative second semi-axis produces a true clockwise ellipse arc, including the short -90 degree arc and the opposite-side -270 degree interval");
  const beforeCancel = (await model()).drawings.length;await drawFive({ finish: false });await context("cancel");assert.equal((await model()).drawings.length, beforeCancel);assert.equal((await state()).tool, "plane.select");assert.equal((await state()).preview.visible, false);
  await clickPlaneCommand(page, "plane.draw-polyline");await input.fill("");await input.press("s");assert.equal((await state()).mode, "line");await input.fill("");await key("Escape");
  passed("The exact polyline context menu closes a single arc with one line, finishes a six-pole pending spline, or cancels an entire mixed prefix; typing in a text field cannot change segment mode");

  // The same SDK drawing session also edits a real part's owned CAD sketch.
  const inputs = resolve(output, "inputs");mkdirSync(inputs, { recursive: true });const partPath = resolve(inputs, "mixed-part.dxf");
  writeFileSync(partPath, readFileSync(resolve(source, "tests/icax-plugins/product/iPlaneCAM/Fixtures/part_with_hole.dxf")));
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();await page.evaluate(path => __planeNativeFileQueue.push(path), partPath);await clickPlaneCommand(page, "plane.import-parts");await settled();
  const part = (await model()).parts[0];await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${part.id}"]`).click();await page.locator(`[data-iplane-action="sketch-open"][data-iplane-object-id="${part.id}"]`).click();await settled();
  const sketchId = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.activeSketchId; });assert.ok(sketchId);
  for (const row of (await model()).sketchCurves.filter(row => row.sketchId === sketchId)) { await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${row.id}"]`).click();await command("删除"); }
  await drawFive({ closed: true });await command("圆 5,10 1");
  const ownerDraft = (await model()).parts.find(row => row.id === part.id);assert.deepEqual(ownerDraft.geometryRef, part.geometryRef);assert.equal(ownerDraft.geometryVersion, part.geometryVersion);
  const sketchCurves = (await model()).sketchCurves.filter(row => row.sketchId === sketchId);assert.equal(sketchCurves.length, 2);
  const mixedOuter = [];for (const row of sketchCurves) { const g = await geometry(row.id, true);mixedOuter.push({ id: row.id, geometry: g });if (g.curves[0].type === "polycurve") { assert.equal(checkFive(g).closed, true);assert.equal(checkFive(g).segments.length, 6); } }
  await clickPlaneCommand(page, "plane.sketch-complete");await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && !ui.geometryTaskId && !ui.activeSketchId; });
  const committed = (await model()).parts.find(row => row.id === part.id);assert.equal(committed.id, part.id);assert.equal(committed.holeCount, 1);assert.equal(BigInt(committed.geometryVersion), BigInt(part.geometryVersion) + 1n);assert.notDeepEqual(committed.geometryRef, part.geometryRef);
  passed("The shared editor creates a closed five-type contour and hole in a real owned sketch; drafting leaves the part untouched and asynchronous Complete updates that same material owner once");

  // Delay an actual Scene response while the user continues editing and scrolling.
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();
  for (let index = 0; index < 16; index++) await command(`圆 ${index * 8},60 2`);
  await page.setViewportSize({ width: 1200, height: 500 });await clickPlaneCommand(page, "plane.draw-polyline");await command("0,80");await command("20,80");await mode("bezier");await command("25,80");
  await input.fill("  30.125, 90.500  ");await input.focus();await input.press("Home");await input.press("Shift+ArrowRight");await input.press("Shift+ArrowRight");
  const beforeRefresh = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), field = document.querySelector("[data-iplane-command-input]"), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane");
    left.scrollTop = (left.scrollHeight - left.clientHeight) * .2;right.scrollTop = (right.scrollHeight - right.clientHeight) * .2;
    globalThis.__mixedRefreshProbe = { field, left, right, canvas: document.querySelector(".icax-three-viewport canvas"), mode: document.querySelector('[data-iplane-polyline-kind="bezier"]'), events: 0 };
    field.addEventListener("mixed-listener-probe", () => __mixedRefreshProbe.events++);
    return { scroll: [left.scrollTop, right.scrollTop], camera: view.viewport.getCameraState(), segments: structuredClone(view.iplaneCamUI.polylineSegments), pending: structuredClone(view.iplaneCamUI.polylinePending) };
  });assert.ok(beforeRefresh.scroll.every(value => value > 0), "The live left and right panes must both scroll for this acceptance case");
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });
  const refresh = page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);await refreshPlaneState(view.iplaneLive.context, view, view.iplaneLive.ops, { refreshView: true }); });
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());
  const latest = await page.evaluate(() => { const p = __mixedRefreshProbe;p.left.scrollTop = (p.left.scrollHeight - p.left.clientHeight) * .8;p.right.scrollTop = (p.right.scrollHeight - p.right.clientHeight) * .8;return { text: p.field.value, selection: [p.field.selectionStart, p.field.selectionEnd], scroll: [p.left.scrollTop, p.right.scrollTop] }; });
  await page.evaluate(() => __planeNativeBridge.releaseResponse());await refresh;
  const retained = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), p = __mixedRefreshProbe, field = document.querySelector("[data-iplane-command-input]");field.dispatchEvent(new Event("mixed-listener-probe"));
    return { sameField: field === p.field, sameLeft: p.left === document.querySelector("[data-iplane-list]"), sameRight: p.right === document.querySelector(".cam-info-pane"), sameCanvas: p.canvas === document.querySelector(".icax-three-viewport canvas"), sameMode: p.mode === document.querySelector('[data-iplane-polyline-kind="bezier"]'), focused: document.activeElement === field, events: p.events, text: field.value, selection: [field.selectionStart, field.selectionEnd], scroll: [p.left.scrollTop, p.right.scrollTop], camera: view.viewport.getCameraState(), segments: structuredClone(view.iplaneCamUI.polylineSegments), pending: structuredClone(view.iplaneCamUI.polylinePending) };
  });
  for (const flag of ["sameField", "sameLeft", "sameRight", "sameCanvas", "sameMode", "focused"]) assert.equal(retained[flag], true, flag);
  assert.equal(retained.events, 1);assert.equal(retained.text, latest.text);assert.deepEqual(retained.selection, latest.selection);assert.deepEqual(retained.scroll, latest.scroll);assert.deepEqual(retained.camera, beforeRefresh.camera);assert.deepEqual(retained.segments, beforeRefresh.segments);assert.deepEqual(retained.pending, beforeRefresh.pending);report.asyncMixedRefresh = { beforeRefresh, latest, retained };
  await key("Escape");await page.setViewportSize({ width: 1600, height: 1000 });
  passed("A delayed real native Scene response retains the unfinished mixed chain, pending controls, exact command draft/caret/focus/listener and mode nodes, latest scrolling in both panes, canvas and camera");

  const savedRows = (await model()).drawings;const savedGeometry = [];
  for (const row of [drawing, closedDrawing, splineDrawing, ...directionalEllipses]) savedGeometry.push({ id: row.id, geometry: await geometry(row.id) });
  await page.locator('.title-bar [data-command-id="app.save"]').click();
  const deadline = Date.now() + 30000;while (!existsSync(savedProject) && Date.now() < deadline) await page.waitForTimeout(25);
  await waitForAsync(() => !document.querySelector('.title-bar [data-command-id="app.save"]')?.disabled);assert.ok(existsSync(savedProject) && statSync(savedProject).size > 100);unlinkSync(partPath);
  const projectId = await page.evaluate(() => __icaxAppShell.getState().activeProjectId), closing = page.evaluate(id => __icaxAppShell.closeProject(id), projectId);
  await waitForAsync(id => __icaxAppShell.getState().activeProjectId !== id || Boolean(document.querySelector("[data-project-close-dialog]")), projectId);
  if (await page.locator("[data-project-close-dialog]").count()) await page.locator('[data-project-close-choice="discard"]').click();await closing;
  await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), savedProject);
  await waitForAsync(async count => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId)?.scene?.iplaneCam?.drawings?.length === count; }, savedRows.length);
  data = await model();assert.deepEqual(data.drawings, savedRows);assert.deepEqual(data.parts.find(row => row.id === part.id), committed);
  for (const entry of savedGeometry) assert.deepEqual(await geometry(entry.id), entry.geometry);
  for (const entry of mixedOuter) assert.deepEqual(await geometry(entry.id, true), entry.geometry);
  assert.ok(data.layers.some(row => row.id === drawLayer));report.savedMixedProject = { path: savedProject, bytes: statSync(savedProject).size, drawingIds: savedGeometry.map(row => row.id), sketchCurveIds: mixedOuter.map(row => row.id) };
  await page.screenshot({ path: resolve(output, "mixed-polyline-reopened.png") });
  passed("Actual save/close/reopen preserves mixed exact segments, six-pole splines, closure, identities and layers plus the committed hole-bearing owner after deleting the imported DXF copy");
}
