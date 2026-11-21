import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { clickPlaneCommand, ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
import { assertDrawingMenu } from "./iPlaneCAM.drawingMenuAssertions.mjs";

// Actual menu, canvas and keyboard events own every mutation. Native calls
// below only read resulting geometry and formal drawing/sketch EC identities.
export async function exercisePolylineMenu({ page, report, assets, source, output, waitForAsync, passed }) {
  const kinds = ["line", "arc", "ellipseArc", "bezier", "bspline"];
  const root = () => page.locator("[data-iplane-context-menu]");
  const child = () => page.locator('[data-planar-context-submenu="polyline-segments"]');
  const toggle = () => page.locator('[data-planar-submenu-toggle="polyline-segments"]');
  const leaf = kind => child().locator(`[data-iplane-action="polyline-kind"][data-iplane-polyline-kind="${kind}"]`);
  const state = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
    return { tool: ui.activeTool, mode: ui.polylineMode, points: structuredClone(ui.commandPoints), segments: structuredClone(ui.polylineSegments ?? []), pending: structuredClone(ui.polylinePending ?? []), error: ui.operationError ?? "", busy: ui.busy, sketchId: ui.activeSketchId };
  });
  const prefix = value => ({ tool: value.tool, mode: value.mode, points: value.points, segments: value.segments, pending: value.pending });
  const model = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return (await getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneScene.ReadState", {})).iplaneCam;
  });
  const geometry = (id, sketch = false) => page.evaluate(async ({ id, sketch }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke(sketch ? "PlaneSketch.ReadGeometry" : "PlaneDrawing.ReadGeometry", { id });
  }, { id, sketch });
  const settled = () => waitForAsync(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && !ui.drawingCommitting && !ui.geometryTaskId;
  });
  const command = async text => {
    await page.locator('[data-bottom-dock-tab="command"]').click();const input = page.locator("[data-iplane-command-input]");await input.fill(text);await input.press("Enter");
    await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && !ui.geometryTaskId && ui.commandInput === ""; });
  };
  const screen = point => page.evaluate(async point => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { Vector3 } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const vp = getProjectView(__icaxAppShell.getState().activeProjectId).viewport, box = vp.root.querySelector("canvas").getBoundingClientRect();vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
    const p = new Vector3(point[0], point[1], 0).applyMatrix4(vp.content.matrixWorld).project(vp.camera);return { x: box.left + (p.x + 1) * box.width / 2, y: box.top + (1 - p.y) * box.height / 2 };
  }, point);
  const point = async value => { const p = await screen(value);await page.mouse.move(p.x, p.y);await page.mouse.click(p.x, p.y);await settled(); };
  const openRoot = async position => {
    const before = prefix(await state()), box = await page.locator(".icax-three-viewport canvas").boundingBox();position ??= { x: box.x + box.width * .55, y: box.y + box.height * .35 };
    await page.mouse.click(position.x, position.y, { button: "right" });await root().waitFor({ state: "visible" });
    const current = await state();assert.deepEqual(prefix(current), before, "Right-click appended or dropped a construction point");
    await assertDrawingMenu(root(), { polyline: current.tool === "plane.draw-polyline", splinePending: current.mode === "bspline" && current.pending.length > 0 });return position;
  };
  const assertChildren = async current => {
    assert.equal(await child().getAttribute("role"), "menu");assert.equal(await toggle().getAttribute("aria-haspopup"), "menu");assert.equal(await toggle().getAttribute("aria-expanded"), "true");
    assert.deepEqual(await child().locator('[data-iplane-action="polyline-kind"]').evaluateAll(nodes => nodes.map(node => node.dataset.iplanePolylineKind)), kinds);
    for (const kind of kinds) { assert.equal(await leaf(kind).getAttribute("role"), "menuitemradio");assert.equal(await leaf(kind).getAttribute("aria-checked"), String(kind === current)); }
  };
  const expand = async click => { if (click) await toggle().click();else await toggle().hover();await child().waitFor({ state: "visible" });await assertChildren((await state()).mode); };
  const choose = async (kind, click = false) => {
    const before = await state();await openRoot();await expand(click);
    const from = await toggle().boundingBox(), to = await leaf(kind).boundingBox();
    await page.mouse.move(from.x + from.width * .7, from.y + from.height / 2);await page.mouse.move(to.x + to.width / 2, to.y + to.height / 2, { steps: 8 });
    assert.equal(await child().isVisible(), true, "Moving into a child menu dismissed it");await leaf(kind).click();
    await waitForAsync(async kind => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return ui.polylineMode === kind && !ui.busy; }, kind);
    assert.equal(await root().count(), 0);assert.equal(await child().count(), 0);assert.equal(await page.locator(".icax-three-viewport canvas").evaluate(node => node === node.ownerDocument.activeElement), true);
    const after = await state();assert.equal(after.tool, "plane.draw-polyline");
    if (!before.pending.length) { assert.deepEqual(after.points, before.points);assert.deepEqual(after.segments, before.segments);assert.deepEqual(after.pending, before.pending); }
    return after;
  };
  const dismiss = async () => { if (await child().isVisible().catch(() => false)) await page.keyboard.press("Escape");if (await root().count()) await page.keyboard.press("Escape");assert.equal(await root().count(), 0); };
  const rootAction = async action => { await openRoot();const button = root().locator(`[data-iplane-action="${action}"]`);assert.equal(await button.isEnabled(), true);await button.click();await settled(); };
  const exactChain = (result, expected) => { assert.equal(result.curves.length, 1);const curve = result.curves[0];assert.equal(curve.type, "polycurve");assert.equal("points" in curve, false);assert.deepEqual(curve.segments.map(s => s.type), expected);return curve; };

  const palette = page.locator('[data-iplane-tool-palette-host] .icax-planar-tool-palette');assert.equal(await palette.getAttribute("role"), "toolbar");assert.equal(await palette.getAttribute("aria-orientation"), "vertical");
  for (const id of ["plane.select", "plane.draw-line", "plane.draw-polyline", "plane.draw-rect", "plane.draw-circle", "plane.draw-arc", "plane.erase", "plane.objects-toggle"]) assert.equal(await palette.locator(`[data-planar-tool="${id}"]`).count(), 1);
  assert.equal(await page.locator(".cam-workbench").getAttribute("data-iplane-objects-open"), "false");assert.equal(await palette.locator('[data-planar-tool="plane.objects-toggle"]').getAttribute("aria-pressed"), "false");
  await page.evaluate(() => { globalThis.__menuPaletteProbe = { canvas: document.querySelector(".icax-three-viewport canvas"), list: document.querySelector("[data-iplane-list]") }; });
  await ensurePlaneObjectsExpanded(page);const expanded = await page.locator("[data-iplane-list]").boundingBox();await palette.locator('[data-planar-tool="plane.objects-toggle"]').click();assert.equal(await page.locator(".cam-workbench").getAttribute("data-iplane-objects-open"), "false");await ensurePlaneObjectsExpanded(page);
  assert.equal(await page.evaluate(() => __menuPaletteProbe.canvas === document.querySelector(".icax-three-viewport canvas") && __menuPaletteProbe.list === document.querySelector("[data-iplane-list]")), true);assert.deepEqual(await page.locator("[data-iplane-list]").boundingBox(), expanded);
  passed("The vertical SDK tool palette exposes current tools, starts with Objects collapsed and restores that panel without replacing its list or viewport canvas");

  await clickPlaneCommand(page, "plane.draw-polyline");await point([0, 0]);
  report.menuBounds = [];
  const box = await page.locator(".icax-three-viewport canvas").boundingBox();
  for (const [horizontal, vertical] of [[0, 0], [1, 0], [0, 1], [1, 1]]) {
    const at = { x: box.x + (horizontal ? box.width - 4 : 4), y: box.y + (vertical ? box.height - 4 : 4) }, before = prefix(await state());
    await openRoot(at);await expand();const rootBox = await root().boundingBox(), childBox = await child().boundingBox(), anchor = await toggle().boundingBox();
    for (const bounds of [rootBox, childBox]) assert.ok(bounds.x >= box.x - 1 && bounds.y >= box.y - 1 && bounds.x + bounds.width <= box.x + box.width + 1 && bounds.y + bounds.height <= box.y + box.height + 1, "The root or child menu escaped the canvas");
    if (horizontal) assert.ok(childBox.x + childBox.width <= anchor.x + 1, "The right-edge submenu did not flip left");else assert.ok(childBox.x >= anchor.x + anchor.width - 1, "The left-edge submenu did not open right");
    report.menuBounds.push({ corner: [horizontal, vertical], root: rootBox, child: childBox, anchor });
    if (horizontal && vertical) await page.screenshot({ path: resolve(output, "polyline-menu-corner.png") });
    await page.keyboard.press("Escape");assert.equal(await child().count(), 0);assert.equal(await root().isVisible(), true);assert.equal(await toggle().getAttribute("aria-expanded"), "false");assert.deepEqual(prefix(await state()), before);
    await page.keyboard.press("Escape");assert.equal(await root().count(), 0);assert.deepEqual(prefix(await state()), before);
  }
  await openRoot();await expand(true);const beforeOutside = prefix(await state());await page.mouse.click(box.x + 18, box.y + box.height * .7);assert.equal(await root().count(), 0);assert.equal(await child().count(), 0);assert.deepEqual(prefix(await state()), beforeOutside);
  passed("The exact root/child menu, current-type checkmarks, pointer entry and four-corner flips stay inside the canvas; Escape closes child then root and outside clicks preserve the construction prefix");

  const five = async ({ started = false, continued = false, protect = false, finish = "finish-drawing" } = {}) => {
    if (!started) { await clickPlaneCommand(page, "plane.draw-polyline");await point([0, 0]); }
    await choose("line", true);await point([20, 0]);await choose("arc");await point([20 + Math.sqrt(50), 10 - Math.sqrt(50)]);
    if (protect) {
      const before = prefix(await state());await openRoot();assert.equal(await root().locator('[data-iplane-action="finish-drawing"]').isEnabled(), false);await expand();
      for (const kind of kinds) if (kind !== "arc") assert.equal(await leaf(kind).isEnabled(), false, `Incomplete arc unexpectedly allowed ${kind}`);
      assert.match(await child().innerText(), /未完成|补齐|退回/);await page.screenshot({ path: resolve(output, "polyline-menu-incomplete-arc.png") });await dismiss();assert.deepEqual(prefix(await state()), before);
      await openRoot();await expand();const beforeLeave = prefix(await state());await page.mouse.click(box.x + 22, box.y + box.height * .8);assert.deepEqual(prefix(await state()), beforeLeave);assert.equal(await root().count(), 0);
    }
    await point([30, 10]);await choose("ellipseArc", true);await point([20, 10]);await point([20, 15]);await point([20, 15]);
    await choose("bezier");for (const p of [[15, 15], [15, 25], [10, 25]]) await point(p);
    await choose("bspline", true);await point([5, 25]);await openRoot();assert.equal(await root().locator('[data-iplane-action="finish-polyline-segment"]').isEnabled(), false);await dismiss();
    for (const p of [[5, 30], [0, 25], [-5, 30], [-10, 30]]) await point(p);
    await rootAction("finish-polyline-segment");let current = await state();assert.equal(current.mode, "bspline");assert.equal(current.pending.length, 0);assert.equal(current.segments.length, 5);assert.equal(current.segments[4].controlPoints.length, 6);
    if (continued) {
      for (const p of [[-15, 35], [-20, 30], [-25, 35], [-30, 35], [-35, 30]]) await point(p);
      current = await choose("line");assert.equal(current.pending.length, 0);assert.equal(current.segments.length, 6);assert.equal(current.segments[5].type, "bspline");assert.equal(current.segments[5].controlPoints.length, 6);await point([-40, 30]);
    }
    await rootAction(finish);
  };
  await five({ started: true, continued: true, protect: true });
  let data = await model();assert.equal(data.drawings.length, 1);const drawing = data.drawings[0], drawingExact = await geometry(drawing.id), drawingCurve = exactChain(drawingExact, [...kinds, "bspline", "line"]);
  assert.equal(drawingCurve.closed, false);for (const spline of drawingCurve.segments.filter(segment => segment.type === "bspline")) { assert.equal(spline.degree, 3);assert.equal(spline.controlPoints.length, 6);assert.equal(spline.knots.length, 10); }
  assert.equal((await state()).tool, "plane.select");report.menuDrawing = { id: drawing.id, geometry: drawingExact };
  passed("Mouse-only context switching and canvas points draw line, arc, ellipse arc, Bezier and six-pole B-spline into one exact native drawing EC; an incomplete arc disables other kinds without losing input");
  passed("Complete Segment commits a B-spline while keeping that mode active; a second six-pole B-spline continues the same chain and switching kind commits its pending controls once");

  await clickPlaneCommand(page, "plane.draw-circle");await openRoot();await assertDrawingMenu(root());assert.equal(await root().locator('[data-planar-submenu-toggle]').count(), 0);assert.equal(await root().locator('[data-iplane-action="finish-polyline-segment"]').count(), 0);await root().locator('[data-iplane-action="cancel-drawing"]').click();await settled();assert.equal((await model()).drawings.length, 1);
  passed("Ordinary circle drawing retains exactly Close, Cancel and Finish, with no polyline submenu or segment action");
  await command("圆 60,45 5");assert.equal((await model()).drawings.length, 2);await palette.locator('[data-planar-tool="plane.erase"]').click();assert.equal((await state()).tool, "plane.erase");await point([100, 90]);assert.equal((await model()).drawings.length, 2);await point([65, 45]);assert.equal((await model()).drawings.length, 1);assert.deepEqual(await geometry(drawing.id), drawingExact);await palette.locator('[data-planar-tool="plane.select"]').click();
  passed("The palette eraser ignores empty canvas and deletes the actual hit drawing through the native edit path while preserving the other exact mixed object");

  const inputs = resolve(output, "inputs");mkdirSync(inputs, { recursive: true });const partPath = resolve(inputs, "menu-owner-part.dxf");writeFileSync(partPath, readFileSync(resolve(source, "tests/icax-plugins/product/iPlaneCAM/Fixtures/part_with_hole.dxf")));
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();await page.evaluate(path => __planeNativeFileQueue.push(path), partPath);await clickPlaneCommand(page, "plane.import-parts");await settled();
  const part = (await model()).parts[0];await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${part.id}"]`).click();await page.locator(`[data-iplane-action="sketch-open"][data-iplane-object-id="${part.id}"]`).click();await settled();const sketchId = (await state()).sketchId;assert.ok(sketchId);
  const originalCurves = (await model()).sketchCurves.filter(row => row.sketchId === sketchId);let erased;
  for (const curve of originalCurves) { const exact = await geometry(curve.id, true);if (exact.curves[0].type === "polyline") { const [a, b] = exact.curves[0].points;await palette.locator('[data-planar-tool="plane.erase"]').click();await point(a.map((v, axis) => (v + b[axis]) / 2));erased = curve.id;break; } }assert.ok(erased);
  assert.equal((await model()).sketchCurves.some(row => row.id === erased), false);assert.deepEqual((await model()).parts.find(row => row.id === part.id).geometryRef, part.geometryRef);assert.deepEqual(await geometry(drawing.id), drawingExact);await palette.locator('[data-planar-tool="plane.select"]').click();
  for (const curve of (await model()).sketchCurves.filter(row => row.sketchId === sketchId)) { await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${curve.id}"]`).click();await command("删除"); }
  await five({ finish: "close-drawing" });await command("圆 5,10 1");data = await model();const draftOwner = data.parts.find(row => row.id === part.id);assert.deepEqual(draftOwner.geometryRef, part.geometryRef);assert.equal(draftOwner.geometryVersion, part.geometryVersion);
  const formalCurves = data.sketchCurves.filter(row => row.sketchId === sketchId);assert.equal(formalCurves.length, 2);let mixed;
  for (const curve of formalCurves) { const exact = await geometry(curve.id, true);if (exact.curves[0].type === "polycurve") { assert.equal(exactChain(exact, [...kinds, "line"]).closed, true);mixed = { id: curve.id, geometry: exact }; } }assert.ok(mixed);
  await clickPlaneCommand(page, "plane.sketch-complete");await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && !ui.geometryTaskId && !ui.activeSketchId; });
  const committed = (await model()).parts.find(row => row.id === part.id);assert.equal(committed.id, part.id);assert.equal(committed.holeCount, 1);assert.equal(BigInt(committed.geometryVersion), BigInt(part.geometryVersion) + 1n);assert.notDeepEqual(committed.geometryRef, part.geometryRef);assert.deepEqual(await geometry(mixed.id, true), mixed.geometry);assert.equal((await model()).drawings.length, 1);
  report.menuOwnedSketch = { ownerId: part.id, sketchId, mixed, committed };await page.screenshot({ path: resolve(output, "polyline-menu-owned-sketch.png") });
  passed("The same mouse submenu draws a closed five-type exact contour in a formal owned sketch; adding its hole and async Complete update the same part once while preserving drawing separation");
  assert.ok(Object.keys(assets).some(path => /SDK\/Cad\/.*[Cc]ontext.*[Mm]enu/.test(path)), "The product did not load a shared SDK context-menu module");
}
