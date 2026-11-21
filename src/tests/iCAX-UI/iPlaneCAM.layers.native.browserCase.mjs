import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync, existsSync, statSync, unlinkSync } from "node:fs";
import { resolve } from "node:path";
import { clickPlaneCommand } from "./iPlaneCAM.browserCommands.mjs";

// All mutations use actual controls. Scene/geometry reads below only inspect
// persisted outcomes; they never replace a UI command or mock a service result.
export async function exerciseLayers({ page, report, assets, source, output, savedProject, waitForAsync, passed }) {
  const paletteColors = ["#22C55E", "#06B6D4", "#3B82F6", "#F59E0B", "#14B8A6", "#84CC16", "#8B5CF6", "#EAB308"];
  const model = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return structuredClone(getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam);
  });
  const read = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return (await getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneScene.ReadState", {})).iplaneCam;
  });
  const nativeGeometry = (id, kind = "drawing") => page.evaluate(async ({ id, kind }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke(kind === "sketch-curves" ? "PlaneSketch.ReadGeometry" : "PlaneDrawing.ReadGeometry", { id });
  }, { id, kind });
  const resultReport = resultId => page.evaluate(async resultId => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneNesting.ReadReport", { resultId });
  }, resultId);
  const sortedLayers = data => [...data.layers].sort((a, b) => a.order - b.order || a.id.localeCompare(b.id));
  const identity = row => Object.fromEntries(["id", "geometryRef", "previewRef", "geometryVersion", "quantity", "width", "height", "holeCount"].filter(key => key in row).map(key => [key, row[key]]));
  const object = (data, id) => [data.drawings, data.parts, data.stocks, data.sketchCurves].flatMap(rows => rows ?? []).find(row => row.id === id);
  const settled = () => waitForAsync(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    if (ui.operationError || view.iplaneLayers?.error) throw new Error(ui.operationError || view.iplaneLayers.error);
    return !ui.busy && !ui.geometryTaskId && !ui.runningTaskId && !view.iplaneLayers?.busy && !view.iplaneLayers?.queued;
  });
  const expect = async (predicate, value) => {
    const deadline = Date.now() + 30000;
    while (Date.now() < deadline) { if (predicate(await model(), value)) return;await page.waitForTimeout(25); }
    throw new Error("Timed out waiting for actual layer model: " + predicate.toString());
  };
  const chooseLayer = async id => { await page.locator(`[data-layer-ui-action="choose"][data-layer-ui-id="${id}"]`).click();await settled(); };
  const select = async (id, additive = false) => {
    await page.keyboard.press("Escape");
    await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${id}"]`).first().click(additive ? { modifiers: ["Control"] } : {});
  };
  const clearSelection = async () => {
    // Snapshot stable EC IDs before changing the live selected-node collection.
    const ids = await page.locator('[data-iplane-action="select-object"][aria-selected="true"]').evaluateAll(nodes => nodes.filter(node => node.getClientRects().length).map(node => node.dataset.iplaneObjectId));
    for (const id of ids) await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${id}"]`).first().click({ modifiers: ["Control"] });
    await settled();
  };
  const command = async text => {
    const input = page.locator("[data-iplane-command-input]");await input.fill(text);await input.press("Enter");
    await waitForAsync(async () => {
      const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
      const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
      if (ui.operationError) throw new Error(ui.operationError);
      return !ui.busy && !ui.geometryTaskId && ui.commandInput === "";
    });
  };
  const ensureManager = async () => {
    if (!await page.locator(".icax-layer-manager").isVisible().catch(() => false)) await page.locator('[data-layer-ui-action="manage"]').click();
    await page.locator(".icax-layer-manager").waitFor({ state: "visible" });
  };
  const updateToggle = async (id, field, value) => {
    await ensureManager();
    await page.locator(`[data-iplane-action="layer-update"][data-iplane-layer-id="${id}"][data-iplane-layer-field="${field}"][data-iplane-layer-value="${value}"]`).click();
    await expect((data, request) => data.layers.some(row => row.id === request.id && row[request.field] === request.value), { id, field, value });await settled();
  };
  const layerInput = (id, field) => page.locator(`[data-layer-ui-field="${field}"][data-layer-ui-id="${id}"]`);
  const actualMaterials = id => page.evaluate(async id => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { LinearSRGBColorSpace } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const root = getProjectView(__icaxAppShell.getState().activeProjectId).viewport.sceneObjects.get(id), colors = [], linearColors = [];
    root?.traverse(node => { for (const material of Array.isArray(node.material) ? node.material : [node.material]) if (material?.color) { colors.push(material.color.getHexString().toLowerCase());linearColors.push(material.color.getHexString(LinearSRGBColorSpace).toLowerCase()); } });
    return { exists: Boolean(root), visible: root?.visible, colors, linearColors, geometry: root?.geometry?.uuid, materialId: root?.userData.instance?.materialId };
  }, id);
  const expectColor = async (id, color) => {
    try { await waitForAsync(async ({ id, color }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { LinearSRGBColorSpace } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const root = getProjectView(__icaxAppShell.getState().activeProjectId).viewport.sceneObjects.get(id), colors = [];
    root?.traverse(node => { for (const material of Array.isArray(node.material) ? node.material : [node.material]) if (material?.color) colors.push(material.color.getHexString(LinearSRGBColorSpace).toLowerCase()); });
    return colors.length > 0 && colors.every(value => value === color.replace("#", "").toLowerCase());
    }, { id, color }); }
    catch (error) {
      report.materialFailure = { id, expected: color, actual: await actualMaterials(id), native: object(await read(), id), view: await page.evaluate(async id => {
        const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), vp = view.viewport;
        return { selected: view.iplaneCamUI.selectedIdsByKind, emphasized: [...vp.emphasizedObjectIds], instance: vp.sceneObjects.get(id)?.userData.instance, applied: vp.getAppliedViewState() };
      }, id) };
      throw error;
    }
  };
  const switchArea = async id => { await page.locator(`[data-action="select-ribbon-tab"][data-tab-id="${id}"]`).click();await settled(); };
  const history = async (verb, predicate, value) => { await page.locator(`.title-bar [data-action="${verb}-project"]`).click();await expect(predicate, value);await settled(); };

  let data = await read();const initial = sortedLayers(data);assert.equal(initial.length, 8);
  report.colorAcceptance = { asserted: "EC palette values through ICRM material channels to real viewport materials", readColorSpace: "LinearSRGBColorSpace", screenPixelCalibrationAsserted: false, diagnosticSRGBReadbackIncluded: true };
  assert.deepEqual(initial.map(row => row.color.toUpperCase()), paletteColors);
  assert.equal(await page.locator('[data-layer-ui-action="choose"]').count(), 8);
  const layout = await page.locator(".icax-layer-strip").boundingBox(), canvas = await page.locator(".icax-three-viewport canvas").boundingBox();
  assert.ok(layout && layout.x > canvas.x + canvas.width * .65 && layout.x + layout.width <= canvas.x + canvas.width + 1 && layout.height > layout.width, "The vertical layer strip must stay inside the right side of the canvas, clear of the parameter pane");
  await page.locator('[data-layer-ui-action="toggle"]').click();assert.equal(await page.locator('[data-layer-ui-action="choose"]').first().isVisible(), false);
  await page.locator('[data-layer-ui-action="toggle"]').click();assert.equal(await page.locator('[data-layer-ui-action="choose"]').first().isVisible(), true);
  report.sharedLayerAssets = Object.keys(assets).filter(path => path.includes("/iCAX-UI/SDK/") && /layer/i.test(path));
  assert.ok(report.sharedLayerAssets.length, "The product did not load the shared layer palette component");
  passed("The actual shared UI presents the Laser3D eight-color palette as a collapsible right-side vertical layer strip");

  await command("圆 20,20 5");await command("圆 50,20 7");data = await read();assert.equal(data.drawings.length, 2);
  const [first, second] = data.drawings, originals = new Map(await Promise.all(data.drawings.map(async row => [row.id, await nativeGeometry(row.id)])));
  await select(first.id);await chooseLayer(initial[2].id);await expect((model, request) => model.drawings.find(row => row.id === request.id)?.layerId === request.layerId, { id: first.id, layerId: initial[2].id });
  assert.deepEqual(await nativeGeometry(first.id), originals.get(first.id));await clearSelection();await expectColor(first.id, initial[2].color);
  report.firstMaterial = await actualMaterials(first.id);
  passed("Selecting one toolpath and clicking a color persists its layer and updates the real native-rendered line material without changing exact geometry");

  await select(first.id);await select(second.id, true);await chooseLayer(initial[4].id);
  await expect((model, request) => request.ids.every(id => model.drawings.find(row => row.id === id)?.layerId === request.layerId), { ids: [first.id, second.id], layerId: initial[4].id });
  await clearSelection();for (const id of [first.id, second.id]) { await expectColor(id, initial[4].color);assert.deepEqual(await nativeGeometry(id), originals.get(id)); }
  passed("Multiple selected toolpaths move to one layer in the same actual palette operation and retain their source curves");

  await chooseLayer(initial[7].id);await expect((model, id) => model.currentLayerId === id, initial[7].id);
  await command("圆 80,20 4");await expect(model => model.drawings.length === 3);data = await read();const third = data.drawings.find(row => ![first.id, second.id].includes(row.id));
  assert.equal(third.layerId, initial[7].id);for (const id of [first.id, second.id]) assert.equal(object(data, id).layerId, initial[4].id);
  await clearSelection();await expectColor(third.id, initial[7].color);
  passed("Clicking a color with no selection changes only the current layer, and a subsequently drawn toolpath inherits it");

  await ensureManager();await page.locator('[data-layer-ui-field="newName"]').fill("自定义刀路层");
  await page.locator('[data-layer-ui-field="newColor"]').evaluate(input => { input.value = "#123abc";input.dispatchEvent(new Event("input", { bubbles: true }));input.dispatchEvent(new Event("change", { bubbles: true })); });
  await page.locator('[data-layer-ui-action="create"]').click();await expect(model => model.layers.length === 9);data = await read();const custom = data.layers.find(row => row.name === "自定义刀路层");assert.ok(custom);assert.equal(custom.color.toLowerCase(), "#123abc");
  await select(first.id);await select(second.id, true);await chooseLayer(custom.id);await clearSelection();await expectColor(first.id, "#123abc");
  await layerInput(custom.id, "name").fill("薄板轮廓");await layerInput(custom.id, "name").press("Tab");await expect((model, id) => model.layers.find(row => row.id === id)?.name === "薄板轮廓", custom.id);
  await layerInput(custom.id, "color").evaluate(input => { input.value = "#aabbcc";input.dispatchEvent(new Event("input", { bubbles: true }));input.dispatchEvent(new Event("change", { bubbles: true })); });
  await expect((model, id) => model.layers.find(row => row.id === id)?.color.toLowerCase() === "#aabbcc", custom.id);await settled();for (const id of [first.id, second.id]) await expectColor(id, "#aabbcc");
  await chooseLayer(custom.id);await expect((model, id) => model.currentLayerId === id, custom.id);
  passed("The manager creates a ninth layer, persists its name and custom color, and recolors actual existing line materials after selection is cleared");

  const inputs = resolve(output, "inputs");mkdirSync(inputs, { recursive: true });const partPath = resolve(inputs, "part-with-hole.dxf");writeFileSync(partPath, readFileSync(resolve(source, "tests/icax-plugins/product/iPlaneCAM/Fixtures/part_with_hole.dxf")));
  await switchArea("nesting");await page.evaluate(path => __planeNativeFileQueue.push(path), partPath);await clickPlaneCommand(page, "plane.import-parts");await expect(model => model.parts.length === 1);
  await clickPlaneCommand(page, "plane.stock-rect");await page.locator('[data-iplane-action="create-rectangle"]').click();await expect(model => model.stocks.length === 1);
  await clickPlaneCommand(page, "plane.nesting-run");await expect(model => model.results.length > 0);await settled();data = await read();const part = data.parts[0], result = data.results[0], partBefore = identity(part), resultBefore = await resultReport(result.id);
  assert.equal(part.layerId, custom.id);assert.equal(resultBefore.stale, false);
  await switchArea("drawing");await clearSelection();await updateToggle(custom.id, "visible", false);
  await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.sceneObjects.get(id)?.visible === false; }, first.id);
  data = await read();assert.deepEqual(identity(data.parts[0]), partBefore);assert.equal(data.drawings.length, 3);assert.equal((await resultReport(result.id)).stale, false);
  const reportMetrics = value => Object.fromEntries(["status", "stale", "required", "placed", "unplacedCount", "boardCount", "utilization", "stockNetArea", "placedMaterialArea", "valid", "parameters"].map(key => [key, value[key]]));
  assert.deepEqual(reportMetrics(await resultReport(result.id)), reportMetrics(resultBefore));
  await updateToggle(custom.id, "visible", true);await expectColor(first.id, "#aabbcc");
  passed("Hiding a display layer changes actual visibility while preserving toolpaths, quantities, exact part geometry and the freshness and metrics of a real nesting result");

  await updateToggle(initial[5].id, "locked", true);await select(first.id);
  assert.equal(await page.locator(`[data-layer-ui-action="choose"][data-layer-ui-id="${initial[5].id}"]`).isDisabled(), true);
  assert.equal(object(await read(), first.id).layerId, custom.id);await updateToggle(initial[5].id, "locked", false);
  passed("A locked receiving layer cannot be selected as an assignment target in the actual palette");

  await clearSelection();await ensureManager();await page.locator(`[data-layer-ui-action="remove"][data-layer-ui-id="${custom.id}"]`).click();
  await page.locator('[data-layer-ui-field="targetId"]').selectOption(initial[1].id);await page.locator('[data-layer-ui-action="delete-confirm"]').click();
  await expect((model, request) => !model.layers.some(row => row.id === request.id) && model.currentLayerId === request.target && [request.first, request.second].every(id => model.drawings.find(row => row.id === id)?.layerId === request.target) && model.parts[0].layerId === request.target, { id: custom.id, target: initial[1].id, first: first.id, second: second.id });
  await history("undo", (model, id) => model.layers.some(row => row.id === id) && model.currentLayerId === id && model.parts[0].layerId === id, custom.id);
  assert.equal(object(await read(), first.id).layerId, custom.id);await expectColor(first.id, "#aabbcc");
  await history("redo", (model, id) => !model.layers.some(row => row.id === id), custom.id);assert.equal((await resultReport(result.id)).stale, false);
  for (const id of [first.id, second.id]) assert.deepEqual(await nativeGeometry(id), originals.get(id));
  passed("Deleting an occupied current layer migrates its objects and current-layer ownership atomically, with one framework undo/redo and no nesting invalidation");

  await switchArea("nesting");await select(part.id);await page.locator(`[data-iplane-action="sketch-open"][data-iplane-object-id="${part.id}"]`).click();
  await expect(model => model.sketches.some(row => row.status === "Editing"));data = await read();const editing = data.sketches.find(row => row.status === "Editing"), sketchRows = data.sketchCurves.filter(row => row.sketchId === editing.id), sketchLayerIds = sketchRows.map(row => [row.id, row.layerId]);
  await select(sketchRows[0].id);await chooseLayer(initial[6].id);await expect((model, request) => model.sketchCurves.find(row => row.id === request.id)?.layerId === request.layerId, { id: sketchRows[0].id, layerId: initial[6].id });
  await clickPlaneCommand(page, "plane.sketch-cancel");await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.activeSketchId; });await settled();
  data = await read();for (const [id, layerId] of sketchLayerIds) assert.equal(object(data, id).layerId, layerId);assert.deepEqual(identity(data.parts[0]), partBefore);assert.equal(data.parts[0].layerId, initial[1].id);
  passed("Owned CAD sketch curves use the same palette, and Cancel restores their backed-up layer assignments while preserving the owner");

  await switchArea("drawing");await clearSelection();for (let index = 0; index < 15; index++) await command(`圆 ${15 + index * 8},50 2`);
  await ensureManager();for (let index = 0; index < 9; index++) { await page.locator('[data-layer-ui-field="newName"]').fill(`滚动验收层 ${index + 1}`);await page.locator('[data-layer-ui-action="create"]').click();await expect((model, count) => model.layers.length === count, 9 + index);await settled(); }
  await select(first.id);await page.setViewportSize({ width: 1280, height: 520 });await ensureManager();
  const name = layerInput(initial[0].id, "name"), originalName = (await read()).layers.find(row => row.id === initial[0].id).name;
  await name.fill("未提交图层名称草稿");await name.press("Home");await name.press("ArrowRight");await name.press("Shift+ArrowRight");await name.press("Shift+ArrowRight");
  const beforeRefresh = await page.evaluate(async id => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);
    const field = document.querySelector(`[data-layer-ui-field="name"][data-layer-ui-id="${id}"]`), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane"), manager = document.querySelector(".icax-layer-edit-list"), swatches = document.querySelector(".icax-layer-swatches");
    const containers = [left, right, manager, swatches];containers.forEach(node => { node.scrollTop = (node.scrollHeight - node.clientHeight) * .25; });
    globalThis.__layerInteractionProbe = { field, containers, canvas: document.querySelector(".icax-three-viewport canvas"), listenerEvents: 0 };field.addEventListener("input", () => __layerInteractionProbe.listenerEvents++);
    return { text: field.value, selection: [field.selectionStart, field.selectionEnd], scroll: containers.map(node => node.scrollTop), camera: view.viewport.getCameraState() };
  }, initial[0].id);
  assert.ok(beforeRefresh.scroll.every(value => value > 0), "The real list, parameter pane, manager and swatches must all scroll");
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });
  const refreshing = page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);await refreshPlaneState(view.iplaneLive.context, view, view.iplaneLive.ops, { refreshView: true }); });
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());const latest = await page.evaluate(() => { const p = __layerInteractionProbe;p.containers.forEach(node => { node.scrollTop = (node.scrollHeight - node.clientHeight) * .8; });return { scroll: p.containers.map(node => node.scrollTop), text: p.field.value, selection: [p.field.selectionStart, p.field.selectionEnd] }; });
  await page.evaluate(() => __planeNativeBridge.releaseResponse());await refreshing;
  const retained = await page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), p = __layerInteractionProbe, field = document.querySelector(`[data-layer-ui-field="name"][data-layer-ui-id="${id}"]`);field.dispatchEvent(new Event("input", { bubbles: true }));return { sameField: field === p.field, sameCanvas: document.querySelector(".icax-three-viewport canvas") === p.canvas, focused: document.activeElement === field, text: field.value, selection: [field.selectionStart, field.selectionEnd], scroll: p.containers.map(node => node.scrollTop), listenerEvents: p.listenerEvents, camera: view.viewport.getCameraState() }; }, initial[0].id);
  assert.equal(retained.sameField, true);assert.equal(retained.sameCanvas, true);assert.equal(retained.focused, true);assert.equal(retained.text, latest.text);assert.deepEqual(retained.selection, latest.selection);assert.deepEqual(retained.scroll, latest.scroll);assert.deepEqual(retained.camera, beforeRefresh.camera);assert.equal(retained.listenerEvents, 1);report.asyncLayerRefresh = { beforeRefresh, latest, retained };
  await name.fill(originalName);await name.press("Tab");await settled();await page.setViewportSize({ width: 1600, height: 1000 });await page.screenshot({ path: resolve(output, "layers-manager.png") });
  passed("A delayed native response preserves the layer name draft, caret, listeners, latest nested manager/strip and side-pane scroll, canvas and camera");

  const saved = await read(), savedIdentity = data => ({ layers: sortedLayers(data).map(row => ({ id: row.id, name: row.name, color: row.color, visible: row.visible, locked: row.locked, order: row.order })), currentLayerId: data.currentLayerId, objects: [data.drawings, data.parts, data.stocks, data.sketchCurves].flatMap(rows => rows ?? []).map(row => ({ ...identity(row), layerId: row.layerId })).sort((a, b) => a.id.localeCompare(b.id)) });
  await page.locator('.title-bar [data-command-id="app.save"]').click();const saveDeadline = Date.now() + 30000;while (!existsSync(savedProject) && Date.now() < saveDeadline) await page.waitForTimeout(25);
  assert.ok(existsSync(savedProject) && statSync(savedProject).size > 100, "The actual framework did not save the layer project");await waitForAsync(() => !document.querySelector('.title-bar [data-command-id="app.save"]')?.disabled);unlinkSync(partPath);
  const projectId = await page.evaluate(() => __icaxAppShell.getState().activeProjectId), closing = page.evaluate(id => __icaxAppShell.closeProject(id), projectId);
  await waitForAsync(id => __icaxAppShell.getState().activeProjectId !== id || Boolean(document.querySelector("[data-project-close-dialog]")), projectId);if (await page.locator("[data-project-close-dialog]").count()) await page.locator('[data-project-close-choice="discard"]').click();await closing;
  await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), savedProject);await expect((model, count) => model.layers.length === count, saved.layers.length);
  assert.deepEqual(savedIdentity(await read()), savedIdentity(saved));await clearSelection();await expectColor(first.id, initial[1].color);assert.equal((await resultReport(result.id)).stale, false);await ensureManager();await page.screenshot({ path: resolve(output, "layers-reopened.png") });
  report.palette = initial;report.project = { path: savedProject, bytes: statSync(savedProject).size, layerCount: saved.layers.length };report.persisted = savedIdentity(saved);
  passed("Actual save, close and reopen preserve custom layer identities, colors, current layer, assignments and native materials after the source DXF copy is removed");
}
