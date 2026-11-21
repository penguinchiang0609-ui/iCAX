import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdirSync, existsSync, unlinkSync, statSync } from "node:fs";
import { resolve } from "node:path";
import { clickPlaneCommand, revealPlaneCommand, revealPlaneTool } from "./iPlaneCAM.browserCommands.mjs";

export async function exerciseSketchEditing({ page, report, assets, source, output, savedProject, waitForAsync, passed }) {
  const model = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return structuredClone(getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam);
  });
  const native = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return (await getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneScene.ReadState", {})).iplaneCam;
  });
  const geometry = id => page.evaluate(async id => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneSketch.ReadGeometry", { id });
  }, id);
  const owner = async (kind, id) => (await native())[kind].find(row => row.id === id);
  const identity = row => Object.fromEntries(["id", "geometryRef", "previewRef", "geometryVersion", "width", "height", "holeCount", "quantity", "sourceName", "sourceUnits"].filter(key => key in row).map(key => [key, row[key]]));
  const command = async text => {
    const input = page.locator("[data-iplane-command-input]");await input.fill(text);await input.press("Enter");
    await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && ui.commandInput === ""; });
  };
  const sketch = async id => (await native()).sketches.find(row => row.id === id);
  const curves = async id => (await native()).sketchCurves.filter(row => row.sketchId === id);
  const choose = async id => { await page.keyboard.press("Escape");await page.locator(`[data-iplane-object-id="${id}"]`).first().click(); };
  const openSketch = async id => {
    await choose(id);await page.locator(`[data-iplane-action="sketch-open"][data-iplane-object-id="${id}"]`).click();
    await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;return !ui.busy && Boolean(ui.activeSketchId) && view.scene.iplaneCam.sketches?.some(row => row.id === ui.activeSketchId && row.ownerId === id && row.status === "Editing"); }, id);
    return page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.activeSketchId; });
  };
  const leaveSketch = async action => {
    await clickPlaneCommand(page, `plane.sketch-${action}`);
    await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && !ui.activeSketchId; });
    await waitForAsync(() => !document.querySelector('[data-iplane-action="sketch-cancel"]') && !document.querySelector('.product-ribbon [data-command-id="plane.sketch-cancel"]'));
  };
  const waitCurves = async (id, count) => waitForAsync(async ({ id, count }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && view.scene.iplaneCam.sketchCurves?.filter(row => row.sketchId === id).length === count; }, { id, count });
  const typeCurve = async (id, type) => {
    for (const row of await curves(id)) if ((await geometry(row.id)).curves[0].type === type) return row;
    assert.fail(`No ${type} curve in sketch ${id}`);
  };
  const assertCanvasRetained = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId);
    return { sameCanvas: document.querySelector(".icax-three-viewport canvas") === __sketchOriginalCanvas, sameViewport: view.viewport === __sketchOriginalViewport, area: view.activeAreaId };
  });

  await page.setViewportSize({ width: 1280, height: 820 });
  assert.deepEqual((await page.locator('.product-ribbon [data-action="select-ribbon-tab"]').allTextContents()).map(text => text.trim()), ["底图", "下料"]);
  assert.ok(await page.locator('.product-ribbon [data-action="ribbon-command-menu-toggle"]').count() >= 2, "Common tools were still flattened instead of using AppShell menus");
  const strip = await page.locator(".ribbon-commands").evaluate(node => ({ scroll: node.scrollWidth, client: node.clientWidth }));
  assert.ok(strip.scroll <= strip.client + 2, "The compact Ribbon overflows the available width");
  const line = await revealPlaneTool(page, "plane.draw-line");
  assert.equal(await line.evaluate(node => node.tagName), "BUTTON");
  assert.equal(await line.getAttribute("data-planar-tool"), "plane.draw-line");
  assert.equal(await line.isVisible(), true);assert.equal(await line.isEnabled(), true);await line.click();
  await page.waitForFunction(() => document.querySelector('[data-planar-tool="plane.draw-line"]')?.getAttribute("aria-pressed") === "true");
  assert.equal(await line.getAttribute("aria-pressed"), "true");await page.keyboard.press("Escape");
  passed("The real SDK Ribbon keeps two workspaces and exposes compact tool menus through normal arrow and leaf clicks");

  const inputs = resolve(output, "inputs");mkdirSync(inputs, { recursive: true });
  const partPath = resolve(inputs, "part-with-hole.dxf"), stockPath = resolve(inputs, "irregular-stock-with-hole.dxf");
  writeFileSync(partPath, readFileSync(resolve(source, "tests/icax-plugins/product/iPlaneCAM/Fixtures/part_with_hole.dxf")));
  writeFileSync(stockPath, "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n90\n6\n70\n1\n10\n0\n20\n0\n10\n500\n20\n0\n10\n500\n20\n220\n10\n400\n20\n220\n10\n400\n20\n300\n10\n0\n20\n300\n0\nCIRCLE\n10\n450\n20\n50\n40\n12\n0\nENDSEC\n0\nEOF\n");
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
  for (const [path, commandId, collection] of [[partPath, "plane.import-parts", "parts"], [stockPath, "plane.import-stocks", "stocks"]]) {
    await page.evaluate(path => __planeNativeFileQueue.push(path), path);await clickPlaneCommand(page, commandId);
    await waitForAsync(async collection => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);if (view.iplaneCamUI.operationError) throw new Error(view.iplaneCamUI.operationError);return !view.iplaneCamUI.busy && view.scene.iplaneCam[collection].length === 1; }, collection);
  }
  const imported = await model(), part = imported.parts[0].id, stock = imported.stocks[0].id;
  assert.equal(imported.parts[0].holeCount, 1);assert.equal(imported.stocks[0].holeCount, 1);assert.equal(imported.drawings.length, 0);
  const partBefore = identity(await owner("parts", part)), stockBefore = identity(await owner("stocks", stock));
  await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");globalThis.__sketchOriginalCanvas = document.querySelector(".icax-three-viewport canvas");globalThis.__sketchOriginalViewport = getProjectView(__icaxAppShell.getState().activeProjectId).viewport; });
  const partSketch = await openSketch(part), originalCurves = await curves(partSketch), outer = await typeCurve(partSketch, "polyline");
  assert.equal(originalCurves.length, 2);assert.deepEqual(await assertCanvasRetained(), { sameCanvas: true, sameViewport: true, area: "nesting" });
  assert.equal((await native()).drawings.length, 0);assert.equal((await sketch(partSketch)).ownerId, part);
  report.sharedEditorAssets = Object.keys(assets).filter(path => path.startsWith("/iCAX-UI/SDK/Cad/") && !path.endsWith("dxfDocument.mjs"));
  assert.ok(report.sharedEditorAssets.length, "The product did not load a common SDK planar editor");
  passed("Actual part and irregular stock DXFs retain holes, and entering the owned CAD sketch reuses the existing SDK viewport and shared editor");

  const outerOriginal = await geometry(outer.id);await choose(outer.id);await clickPlaneCommand(page, "plane.edit-nodes");
  const y = page.locator('[data-iplane-form-field="point0y"]');await y.fill("2");await page.locator('[data-iplane-action="apply-node-edit"]').click();
  await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && (await view.sceneProxy.invoke("PlaneSketch.ReadGeometry", { id })).curves[0].points[0][1] === 2; }, outer.id);
  assert.deepEqual(identity(await owner("parts", part)), partBefore);await leaveSketch("cancel");
  assert.deepEqual(identity(await owner("parts", part)), partBefore);assert.deepEqual((await geometry(outer.id)).curves, outerOriginal.curves);
  assert.deepEqual((await curves(partSketch)).map(row => row.id), originalCurves.map(row => row.id));
  const selectionAfterCancel = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.selectedIdsByKind.parts; });assert.deepEqual(selectionAfterCancel, [part]);
  passed("Node editing changes the exact sketch curve, while Cancel restores its original IDs and geometry and returns to the selected owner");

  assert.equal(await openSketch(part), partSketch);await choose(outer.id);await command("缩放 1.25");
  const prior = await curves(partSketch);await clickPlaneCommand(page, "plane.draw-line");const box = await page.locator(".icax-three-viewport canvas").boundingBox();
  await page.mouse.click(box.x + box.width * .44, box.y + box.height * .40);await page.mouse.move(box.x + box.width * .52, box.y + box.height * .35);await page.mouse.click(box.x + box.width * .52, box.y + box.height * .35);await waitCurves(partSketch, prior.length + 1);
  const invalid = (await curves(partSketch)).find(row => !prior.some(old => old.id === row.id));assert.equal((await geometry(invalid.id)).curves[0].type, "line");
  await clickPlaneCommand(page, "plane.sketch-complete");
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return !ui.busy && Boolean(ui.operationError); });
  assert.deepEqual(identity(await owner("parts", part)), partBefore);assert.equal((await sketch(partSketch)).status, "Editing");
  const error = await page.locator(".iplane-operation-error").textContent();assert.ok(error.trim() && !error.includes("PLANE_"));
  await choose(invalid.id);await command("删除");await waitCurves(partSketch, prior.length);await command("圆 60,35 3");await waitCurves(partSketch, prior.length + 1);
  const idsToCommit = (await curves(partSketch)).map(row => row.id);assert.deepEqual(identity(await owner("parts", part)), partBefore);
  await leaveSketch("complete");const partCompleted = identity(await owner("parts", part));
  assert.equal(partCompleted.id, part);assert.equal(BigInt(partCompleted.geometryVersion), BigInt(partBefore.geometryVersion) + 1n);assert.equal(partCompleted.width, 100);assert.equal(partCompleted.holeCount, 2);assert.notDeepEqual(partCompleted.geometryRef, partBefore.geometryRef);assert.deepEqual((await curves(partSketch)).map(row => row.id), idsToCommit);
  assert.deepEqual(await assertCanvasRetained(), { sameCanvas: true, sameViewport: true, area: "nesting" });
  passed("Shared mouse drawing, deletion and transforms edit only sketch curves; invalid completion preserves the owner, and a valid hole-bearing contour commits once to that same part");

  assert.equal(await openSketch(part), partSketch);await choose(outer.id);await clickPlaneCommand(page, "plane.layers");
  await page.locator('[data-iplane-form="layers"][data-iplane-form-field="newName"]').fill("草图验收层");
  await page.locator('[data-iplane-action="layer-create"]').click();
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && view.scene.iplaneCam.layers.some(row => row.name === "草图验收层"); });
  const assignedLayer = (await native()).layers.find(row => row.name === "草图验收层").id;
  await page.locator(`[data-iplane-action="layer-assign"][data-iplane-layer-id="${assignedLayer}"]`).click();
  await waitForAsync(async ({ curve, assignedLayer }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && view.scene.iplaneCam.sketchCurves.find(row => row.id === curve).layerId === assignedLayer; }, { curve: outer.id, assignedLayer });
  for (const locked of [true, false]) {
    await page.locator(`[data-iplane-action="layer-update"][data-iplane-layer-id="${assignedLayer}"][data-iplane-layer-field="locked"][data-iplane-layer-value="${locked}"]`).click();
    await waitForAsync(async ({ curve, locked }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && view.scene.iplaneCam.sketchCurves.find(row => row.id === curve).locked === locked; }, { curve: outer.id, locked });
  }
  assert.deepEqual(identity(await owner("parts", part)), partCompleted);await leaveSketch("cancel");
  assert.deepEqual(identity(await owner("parts", part)), partCompleted);
  passed("The actual layer panel creates a layer, assigns the selected sketch EC curve and locks/unlocks it without changing its owner geometry");

  assert.equal(await openSketch(part), partSketch);
  for (let index = 0; index < 14; index++) await command(`圆 ${8 + index * 5},8 1`);
  await choose(outer.id);await clickPlaneCommand(page, "plane.edit-nodes");await page.setViewportSize({ width: 1280, height: 520 });
  const x = page.locator('[data-iplane-form-field="point0x"]');await x.fill("1.50");await x.press("Home");await x.press("ArrowRight");await x.press("Shift+ArrowRight");await x.press("Shift+ArrowRight");
  const beforeRefresh = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), field = document.querySelector('[data-iplane-form-field="point0x"]'), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane");
    left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .25);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .25);
    globalThis.__sketchRefreshProbe = { field, left, right, canvas: document.querySelector(".icax-three-viewport canvas"), events: 0 };
    field.addEventListener("input", () => __sketchRefreshProbe.events++);
    return { text: field.value, selection: [field.selectionStart, field.selectionEnd], scroll: [left.scrollTop, right.scrollTop], camera: view.viewport.getCameraState() };
  });
  assert.ok(beforeRefresh.scroll.every(value => value > 0), "Both real sketch panes must be scrollable for the async interaction case");
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });
  const refresh = page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);await refreshPlaneState(view.iplaneLive.context, view, view.iplaneLive.ops, { refreshView: true }); });
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());
  const latest = await page.evaluate(() => { const { left, right, field } = __sketchRefreshProbe;left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .8);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .8);return { scroll: [left.scrollTop, right.scrollTop], text: field.value, selection: [field.selectionStart, field.selectionEnd] }; });
  assert.ok(latest.scroll.every((value, axis) => value > beforeRefresh.scroll[axis]));await page.evaluate(() => __planeNativeBridge.releaseResponse());await refresh;
  const afterRefresh = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), probe = __sketchRefreshProbe, field = document.querySelector('[data-iplane-form-field="point0x"]');const sameField = field === probe.field;field.dispatchEvent(new Event("input", { bubbles: true }));return { sameField, sameCanvas: document.querySelector(".icax-three-viewport canvas") === probe.canvas, focused: document.activeElement === field, text: field.value, selection: [field.selectionStart, field.selectionEnd], scroll: [probe.left.scrollTop, probe.right.scrollTop], camera: view.viewport.getCameraState(), listenerEvents: probe.events }; });
  assert.equal(afterRefresh.sameField, true);assert.equal(afterRefresh.sameCanvas, true);assert.equal(afterRefresh.focused, true);assert.equal(afterRefresh.text, latest.text);assert.deepEqual(afterRefresh.selection, latest.selection);assert.deepEqual(afterRefresh.scroll, latest.scroll);assert.deepEqual(afterRefresh.camera, beforeRefresh.camera);assert.equal(afterRefresh.listenerEvents, 1);report.asyncSketchRefresh = { beforeRefresh, latest, afterRefresh };
  await leaveSketch("cancel");assert.deepEqual(identity(await owner("parts", part)), partCompleted);assert.deepEqual((await curves(partSketch)).map(row => row.id), idsToCommit);
  assert.deepEqual(await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return Object.entries(getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.formDrafts).filter(([key, value]) => key.startsWith("nodes:") && Object.keys(value).length); }), [], "Cancelling the owned sketch must discard its node input drafts before project save");
  passed("A delayed real Scene response preserves the shared node editor, raw draft, focus, caret, listeners, latest left/right scrolling, canvas and camera");

  await page.setViewportSize({ width: 1600, height: 1000 });const stockSketch = await openSketch(stock), stockRows = await curves(stockSketch), stockOuter = await typeCurve(stockSketch, "polyline"), stockHole = await typeCurve(stockSketch, "circle");
  assert.equal(stockRows.length, 2);await choose(stockOuter.id);await command("移动 4,-2");await choose(stockHole.id);await command("移动 4,-2");assert.deepEqual(identity(await owner("stocks", stock)), stockBefore);await leaveSketch("complete");
  const stockCompleted = identity(await owner("stocks", stock));assert.equal(stockCompleted.id, stock);assert.equal(stockCompleted.holeCount, 1);assert.equal(BigInt(stockCompleted.geometryVersion), BigInt(stockBefore.geometryVersion) + 1n);assert.notDeepEqual(stockCompleted.geometryRef, stockBefore.geometryRef);assert.deepEqual((await curves(stockSketch)).map(row => row.id), stockRows.map(row => row.id));assert.equal((await native()).drawings.length, 0);
  await page.locator('.title-bar [data-action="undo-project"]').click();await waitForAsync(async ({ stock, ref }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam.stocks.find(row => row.id === stock)?.geometryRef.url === ref.url; }, { stock, ref: stockBefore.geometryRef });
  await page.locator('.title-bar [data-action="redo-project"]').click();await waitForAsync(async ({ stock, ref }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam.stocks.find(row => row.id === stock)?.geometryRef.url === ref.url; }, { stock, ref: stockCompleted.geometryRef });
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.activeSketchId; });
  passed("An irregular stock and its hole use their own CAD sketch; completion and framework undo/redo preserve owner identity and drawing isolation");

  const savedCurves = await Promise.all((await native()).sketchCurves.map(async row => ({ id: row.id, geometry: await geometry(row.id) })));
  await page.locator('.title-bar [data-command-id="app.save"]').click();
  const saveDeadline = Date.now() + 30000;while (!existsSync(savedProject) && Date.now() < saveDeadline) await page.waitForTimeout(25);
  await waitForAsync(() => !document.querySelector('.title-bar [data-command-id="app.save"]')?.disabled);
  assert.ok(existsSync(savedProject) && statSync(savedProject).size > 100);unlinkSync(partPath);unlinkSync(stockPath);
  const projectId = await page.evaluate(() => __icaxAppShell.getState().activeProjectId), closing = page.evaluate(id => __icaxAppShell.closeProject(id), projectId);
  await waitForAsync(id => __icaxAppShell.getState().activeProjectId !== id || Boolean(document.querySelector("[data-project-close-dialog]")), projectId);
  if (await page.locator("[data-project-close-dialog]").count()) await page.locator('[data-project-close-choice="discard"]').click();await closing;
  await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), savedProject);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId)?.scene?.iplaneCam?.sketches?.length === 2; });
  assert.deepEqual(identity(await owner("parts", part)), partCompleted);assert.deepEqual(identity(await owner("stocks", stock)), stockCompleted);
  assert.equal((await owner("parts", part)).layerId, imported.parts[0].layerId);
  assert.ok((await native()).layers.some(row => row.id === assignedLayer && row.name === "草图验收层"));
  for (const entry of savedCurves) assert.deepEqual(await geometry(entry.id), entry.geometry);
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
  assert.equal(await openSketch(part), partSketch);assert.deepEqual((await curves(partSketch)).map(row => row.id), idsToCommit);await leaveSketch("cancel");
  report.savedSketch = { path: savedProject, bytes: statSync(savedProject).size, curveIds: savedCurves.map(row => row.id) };await page.screenshot({ path: resolve(output, "sketch-editing-reopened.png") });
  passed("Actual save/close/reopen restores formal sketch and curve EC identities and exact geometry after both imported source copies are removed");
}
