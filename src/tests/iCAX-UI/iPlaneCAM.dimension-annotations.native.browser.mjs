import { ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
// Genuine AppShell/SDK input with a private native runtime. File-dialog choices
// and JSON-lines transport are test plumbing; geometry and ECS are never mocked.
import assert from "node:assert/strict";
import { clickPlaneCommand } from "./iPlaneCAM.browserCommands.mjs";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { createServer } from "node:http";
import { existsSync, mkdirSync, readFileSync, writeFileSync, statSync } from "node:fs";
import { resolve, dirname, sep, extname, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const source = resolve(fileURLToPath(new URL("../../", import.meta.url))), root = resolve(source, "..");
const assetRoot = resolve(process.env.ICAX_PLANE_ASSET_ROOT || source);
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-dimension-annotations"));
assert.ok(process.env.ICAX_PLANE_NATIVE_BRIDGE, "Set ICAX_PLANE_NATIVE_BRIDGE to the isolated native acceptance runtime");
const executable = resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE);
assert.ok(existsSync(executable), "The selected native acceptance runtime does not exist");
mkdirSync(output, { recursive: true });
const runId = String(Date.now()), savedProject = resolve(output, `dimension-annotations-${runId}.ipcam`);
const child = spawn(executable, ["--data-dir", resolve(output, `native-user-data-${runId}`)], { cwd: dirname(executable), windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
const pending = new Map(), requests = [], stderr = [], failures = [], checks = [], assets = {};
let sequence = 0, processClosed = false;
child.stderr.on("data", bytes => stderr.push(bytes.toString()));
child.on("exit", code => { processClosed = true;for (const entry of pending.values()) { clearTimeout(entry.timer);entry.reject(new Error("Native runtime exited: " + code)); }pending.clear(); });
createInterface({ input: child.stdout }).on("line", line => {
  let response;try { response = JSON.parse(line); } catch { stderr.push("stdout: " + line);return; }
  const entry = pending.get(String(response.id));if (!entry) return;
  clearTimeout(entry.timer);pending.delete(String(response.id));response.ok ? entry.resolve(response.result) : entry.reject(new Error(response.error || "Native bridge failed"));
});
function rpc(request) {
  if (processClosed) return Promise.reject(new Error("Native bridge is closed"));
  const id = String(++sequence);if (request.action !== "poll") requests.push({ id, ...request });
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending.delete(id);reject(new Error("Native bridge timed out: " + request.action)); }, 180000);
    pending.set(id, { resolve, reject, timer });child.stdin.write(JSON.stringify({ id, ...request }) + "\n");
  });
}
const mime = { ".html": "text/html", ".mjs": "text/javascript", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml", ".png": "image/png", ".json": "application/json" };
const server = createServer(async (request, response) => {
  const pathname = decodeURIComponent(new URL(request.url, "http://native.test").pathname);
  if (pathname === "/native-rpc" && request.method === "POST") {
    try { const chunks = [];for await (const chunk of request) chunks.push(chunk);const result = await rpc(JSON.parse(Buffer.concat(chunks).toString()));response.writeHead(200, { "Content-Type": "application/json" });response.end(JSON.stringify({ ok: true, result })); }
    catch (error) { response.writeHead(200, { "Content-Type": "application/json" });response.end(JSON.stringify({ ok: false, error: error.message })); }
    return;
  }
  if (pathname === "/favicon.ico") { response.writeHead(204);response.end();return; }
  const path = resolve(assetRoot, "." + pathname);
  if (!path.toLowerCase().startsWith((assetRoot + sep).toLowerCase())) { response.writeHead(403);response.end();return; }
  try { const bytes = readFileSync(path);assets[pathname] = createHash("sha256").update(bytes).digest("hex");response.writeHead(200, { "Content-Type": mime[extname(path)] || "application/octet-stream" });response.end(bytes); }
  catch { failures.push(pathname);response.writeHead(404);response.end(); }
});
await new Promise(done => server.listen(0, "127.0.0.1", done));
const origin = `http://127.0.0.1:${server.address().port}`;
let browser, page;
const report = { pass: false, scope: "Actual AppShell and SDK pointer/keyboard UI with native PlaneDrawing ECS, exact geometry, render resources and history. Test transport supplies file paths; no desktop CEF or OS-dialog interaction is asserted.", executable, assetRoot, savedProject };
report.binaries = Object.fromEntries([basename(executable), "iPlaneCAM.dll", "PlaneNestingServices.dll", "iPlaneNesting.dll", "Product.dll", "Project.dll", "ProjectFile.dll", "Database.dll", "Resources.dll"].map(name => {
  const path = resolve(dirname(executable), name);return [name, existsSync(path) ? createHash("sha256").update(readFileSync(path)).digest("hex") : "missing"];
}));
const passed = name => { checks.push(name);console.log("PASS " + name); };
async function waitForAsync(predicate, argument, { timeout = 30000 } = {}) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { if (await page.evaluate(predicate, argument)) return;await page.waitForTimeout(25); }
  throw new Error("Timed out waiting for native/UI state: " + predicate.toString().slice(0, 240));
}
async function drawingState() {
  return page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    const response = await view.sceneProxy.invoke("PlaneScene.ReadState", {}), preview = view.iplaneDrawingPreview;
    const positions = preview?.outline.geometry.attributes.position;
    return { native: response.iplaneCam, revision: response.revision, tool: ui.activeTool, points: structuredClone(ui.commandPoints ?? []), busy: ui.busy, error: ui.operationError ?? "", commandInput: ui.commandInput, applied: view.viewport.getAppliedViewState(), preview: { visible: preview?.group.visible ?? false, points: positions ? Array.from(positions.array) : [] } };
  });
}
async function geometry(id) {
  return page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }); }, id);
}
async function waitCount(count) {
  await waitForAsync(async count => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);
    return !ui.busy && view.scene?.iplaneCam?.drawings?.length === count;
  }, count);
  assert.equal((await drawingState()).native.drawings.length, count);
}
async function waitPrefix(count) {
  await waitForAsync(async count => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.commandPoints.length === count; }, count);
}
async function start(primitive) {
  await clickPlaneCommand(page, `plane.draw-${primitive}`);
  await waitForAsync(async primitive => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return ui.activeTool === `plane.draw-${primitive}` && !ui.busy && !ui.commandPoints.length; }, primitive);
}
async function screenPoint(x, y) {
  const box = await page.locator(".icax-three-viewport canvas").boundingBox();return { x: Math.round(box.x + box.width * x), y: Math.round(box.y + box.height * y) };
}
async function pointerWorld(point) {
  return page.evaluate(async point => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.pointOnWorkPlane(point.x, point.y, [0, 0, 1]).slice(0, 2); }, point);
}
async function click(point) { await page.mouse.click(point.x, point.y); }
async function hover(point) { await page.mouse.move(point.x, point.y, { steps: 5 }); }
async function newRow(before, count) {
  await waitCount(count);const ids = new Set(before.map(row => row.id)), added = (await drawingState()).native.drawings.filter(row => !ids.has(row.id));assert.equal(added.length, 1);return added[0];
}
async function readRenderResources(rows) {
  return page.evaluate(async rows => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const { loadRenderResource } = await import("/iCAX-UI/SDK/Viewport/renderResource.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return Promise.all(rows.map(async row => { const resource = await loadRenderResource(view.sceneProxy.resources, row.previewRef);return { id: row.id, type: resource?.type, kind: resource?.data?.kind, points: Array.from(resource?.data?.points ?? []) }; })); }, rows);
}
async function saveAndReopen() {
  const before = await drawingState();await page.locator('.title-bar [data-command-id="app.save"]').click();
  const deadline = Date.now() + 30000;while (!existsSync(savedProject) && Date.now() < deadline) await page.waitForTimeout(25);
  assert.ok(existsSync(savedProject) && statSync(savedProject).size > 100, "The actual framework did not write a project file");
  await waitForAsync(() => !document.querySelector('.title-bar [data-command-id="app.save"]')?.disabled);
  const projectId = await page.evaluate(() => __icaxAppShell.getState().activeProjectId), closing = page.evaluate(projectId => __icaxAppShell.closeProject(projectId), projectId);
  await waitForAsync(projectId => Boolean(document.querySelector("[data-project-close-dialog]")) || __icaxAppShell.getState().activeProjectId !== projectId, projectId);
  if (await page.locator("[data-project-close-dialog]").count()) await page.locator('[data-project-close-choice="discard"]').click();
  await closing;await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), savedProject);
  await waitCount(before.native.drawings.length);const after = await drawingState();
  return { before, after, file: { path: savedProject, bytes: statSync(savedProject).size, sha256: createHash("sha256").update(readFileSync(savedProject)).digest("hex") } };
}
const near = (a, b, message, tolerance = 1e-7) => assert.ok(Math.abs(a - b) <= tolerance, message ?? `${a} differs from ${b}`);
const selectedRow = key => page.locator(`[data-iplane-selected-dimension-label="${key}"]`);
const selectedButton = key => page.locator(`[data-iplane-dimension-edit="${key}"]`);
const selectedInput = key => page.locator(`[data-iplane-selected-dimension="${key}"]`);
const drawRow = key => page.locator(`[data-iplane-dimension-row="${key}"]`);
const drawInput = key => page.locator(`[data-iplane-dimension="${key}"]`);
async function idle() {
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy; });
}
async function command(text) {
  const input = page.locator("[data-iplane-command-input]");await input.fill(text);await input.press("Enter");
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && ui.commandInput === ""; });
}
async function waitSelected(id) {
  await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), state = view.iplaneSelectedDimensions, row = view.scene?.iplaneCam?.drawings?.find(row => row.id === id);return state?.id === id && Boolean(state.geometry?.dimensions?.length) && state.geometry.geometryRef?.url === row?.geometryRef?.url && String(state.geometry.geometryRef?.version) === String(row?.geometryRef?.version); }, id);
  await page.locator("[data-iplane-selected-dimensions]").waitFor({ state: "visible" });
}
async function waitSelectedReference(id, reference) {
  await waitForAsync(async ({ id, reference }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), row = view.scene?.iplaneCam?.drawings?.find(row => row.id === id), actual = row?.geometryRef;return actual?.url === reference.url && String(actual?.version) === String(reference.version); }, { id, reference });
  await waitSelected(id);
}
async function select(id) { await page.keyboard.press("Escape");await ensurePlaneObjectsExpanded(page);await page.locator(`[data-iplane-object-id="${id}"]`).click();await waitSelected(id); }
async function edit(key, value) { await selectedButton(key).dblclick();await selectedInput(key).waitFor({ state: "visible" });await selectedInput(key).fill(String(value)); }
async function zoomBy(delta) {
  const radius = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius; });
  await hover(await screenPoint(.5, .5));await page.mouse.wheel(0, delta);
  await waitForAsync(async radius => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius !== radius; }, radius);
}
async function commit(key, id) {
  await selectedInput(key).press("Enter");
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;if (ui.operationError) throw new Error(ui.operationError);return !ui.busy && !view.iplaneSelectedDimensions?.draft; });
  await waitSelected(id);return geometry(id);
}
function curve(geometry) { assert.equal(geometry.curves.length, 1);return geometry.curves[0]; }
function dimensionValue(geometry, key) { const descriptor = geometry.dimensions.find(item => item.parameter === key);assert.ok(descriptor, `Missing native ${key} descriptor`);return descriptor; }
function rectangleFrame(geometry) {
  const c = curve(geometry);assert.equal(c.type, "polyline");assert.equal(c.closed, true);assert.equal(c.points.length, 4);
  const origin = c.points[0], u = c.points[1].map((value, axis) => value - origin[axis]), v = c.points[3].map((value, axis) => value - origin[axis]);
  const width = Math.hypot(...u), height = Math.hypot(...v);return { origin, u: u.map(value => value / width), v: v.map(value => value / height), width, height };
}
async function assertSelectedPreview(id, before) {
  const actual = await geometry(id);assert.deepEqual(actual.geometryRef, before.geometryRef);assert.deepEqual(actual.curves, before.curves);
  const preview = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), preview = view.iplaneDrawingPreview;return { active: view.iplaneSelectedPreviewActive, marked: preview?.group.userData.iplaneSelectedDimensionPreview, visible: preview?.group.visible, points: Array.from(preview?.outline.geometry.attributes.position?.array ?? []) }; });
  assert.equal(preview.active, true);assert.equal(preview.marked, true);assert.equal(preview.visible, true);assert.ok(preview.points.length >= 6);return preview;
}
// Independently project the native model-space anchors with the SDK camera.
// Do not call the product's dimensionPresentation calculations under test.
async function assertDimensionLines(descriptors, owner = "selected") {
  await waitForAsync(({ keys, owner }) => keys.every(key => { const row = document.querySelector(owner === "selected" ? `[data-iplane-selected-dimension-label="${key}"]` : `[data-iplane-dimension-row="${key}"]`);return row && !row.hidden && Number.isFinite(Number(row.dataset.iplaneDimensionAnchorX)); }), { keys: descriptors.map(item => item.parameter ?? item.key), owner });
  const result = await page.evaluate(async ({ descriptors, owner }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { Vector3 } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");const view = getProjectView(__icaxAppShell.getState().activeProjectId), vp = view.viewport, canvas = vp.root.querySelector("canvas"), box = canvas.getBoundingClientRect();
    vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
    const a = vp.pointOnWorkPlane(box.left + box.width / 2, box.top + box.height / 2, [0, 0, 1]), b = vp.pointOnWorkPlane(box.left + box.width / 2 + 1, box.top + box.height / 2, [0, 0, 1]), scale = Math.hypot(b[0] - a[0], b[1] - a[1]);
    const groups = vp.dimensionContent.children.filter(object => object.isLine).map(object => { const p = object.geometry.attributes.position, points = [];for (let i = 0; i < p.count; i++) points.push(new Vector3(p.getX(i), p.getY(i), p.getZ(i)).applyMatrix4(object.matrixWorld).toArray());return points; });
    const rows = descriptors.map(descriptor => {
      const key = descriptor.parameter ?? descriptor.key, distance = descriptor.kind === "radius" ? 0 : scale * 30, offset = (descriptor.offsetDirection ?? [0, 0, 0]).map(value => value * distance);
      const start = descriptor.start.map((value, axis) => value + offset[axis]), end = descriptor.end.map((value, axis) => value + offset[axis]), startWorld = new Vector3().fromArray(start).applyMatrix4(vp.content.matrixWorld).toArray(), endWorld = new Vector3().fromArray(end).applyMatrix4(vp.content.matrixWorld).toArray();
      const projected = new Vector3().fromArray(start).lerp(new Vector3().fromArray(end), .5).applyMatrix4(vp.content.matrixWorld).project(vp.camera), expected = [box.left + (projected.x + 1) * box.width / 2, box.top + (1 - projected.y) * box.height / 2];
      const row = document.querySelector(owner === "selected" ? `[data-iplane-selected-dimension-label="${key}"]` : `[data-iplane-dimension-row="${key}"]`), bounds = row.getBoundingClientRect();
      return { key, startWorld, endWorld, expected, actual: [Number(row.dataset.iplaneDimensionAnchorX), Number(row.dataset.iplaneDimensionAnchorY)], bounds: { x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height } };
    });
    return { groups, sprites: vp.dimensionContent.children.filter(object => object.isSprite).length, rows, camera: vp.getCameraState() };
  }, { descriptors, owner });
  assert.ok(result.groups.length >= descriptors.length, "Dimension lines were not emitted into SDK dimensionContent");assert.equal(result.sprites, 0, "The SDK emitted duplicate dimension text behind the DOM labels");
  const close = (a, b) => Math.hypot(...a.map((value, axis) => value - b[axis])) < 1e-3;
  for (const row of result.rows) { assert.ok(result.groups.some(points => points.some(point => close(point, row.startWorld)) && points.some(point => close(point, row.endWorld))), `Missing actual SDK dimension endpoints for ${row.key}`);for (let axis = 0; axis < 2; axis++) near(row.actual[axis], row.expected[axis], `The ${row.key} label left its native world anchor`, 1e-3); }
  return result;
}
async function history() { return page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.getUndoRedoState(); }); }
async function refresh({ refreshView = false } = {}) { return page.evaluate(async refreshView => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return refreshPlaneState(view.iplaneLive.context, view, view.iplaneLive.ops, { render: true, refreshView }); }, refreshView); }
async function exerciseDimensionAnnotations() {
  const initial = await drawingState();assert.equal(initial.native.drawings.length, 0);assert.ok(initial.native.capabilities.drawingEdits.includes("dimensions"), "The test requires the updated native dimension-edit module");await clickPlaneCommand(page, "plane.snap-toggle");
  report.dragAnnotations = [];
  for (const primitive of ["circle", "rect"]) {
    await start(primitive);const from = await screenPoint(.40, .48), to = await screenPoint(.58, .31), a = await pointerWorld(from), b = await pointerWorld(to), dx = b[0] - a[0], dy = b[1] - a[1];
    await page.mouse.move(from.x, from.y);await page.mouse.down();await hover(to);assert.equal((await drawingState()).native.drawings.length, 0);
    const descriptors = primitive === "circle" ? [{ key: "radius", kind: "radius", start: [...a, 0], end: [...b, 0] }]
      : [{ key: "width", kind: "linear", start: [...a, 0], end: [b[0], a[1], 0], offsetDirection: [0, -Math.sign(dy), 0] }, { key: "height", kind: "linear", start: [...a, 0], end: [a[0], b[1], 0], offsetDirection: [-Math.sign(dx), 0, 0] }];
    report.dragAnnotations.push({ primitive, presentation: await assertDimensionLines(descriptors, "drawing") });for (const descriptor of descriptors) assert.equal(await drawInput(descriptor.key).isVisible(), false);
    await page.mouse.up();await waitCount(1);await page.locator('.title-bar [data-action="undo-project"]').click();await waitCount(0);
  }
  let before = initial.native.drawings;
  await start("circle");const centerPoint = await screenPoint(.32, .35), center = await pointerWorld(centerPoint);await click(centerPoint);await waitPrefix(1);await hover(await screenPoint(.48, .25));await drawRow("radius").waitFor({ state: "visible" });assert.equal(await drawInput("radius").isVisible(), false, "A passive dimension label unexpectedly opened an editor");
  await page.keyboard.type("24");await drawInput("radius").waitFor({ state: "visible" });assert.equal((await drawingState()).native.drawings.length, 0);
  report.drawRadiusLines = await assertDimensionLines([{ key: "radius", kind: "radius", start: [...center, 0], end: [center[0] + 24, center[1], 0] }], "drawing");
  await page.screenshot({ path: resolve(output, "drawing-radius-on-geometry.png") });await drawInput("radius").press("Enter");const circleRow = await newRow(before, 1);near(curve(await geometry(circleRow.id)).radius, 24);
  passed("Mouse drags and typed radius creation show real SDK dimension lines and geometry-anchored labels before creating native geometry");

  before = (await drawingState()).native.drawings;await start("rect");const rectAnchorPoint = await screenPoint(.48, .55), rectAnchor = await pointerWorld(rectAnchorPoint);await click(rectAnchorPoint);await waitPrefix(1);await hover(await screenPoint(.64, .35));
  await drawRow("width").waitFor({ state: "visible" });await drawRow("height").waitFor({ state: "visible" });const passiveW = await drawRow("width").boundingBox(), passiveH = await drawRow("height").boundingBox();assert.ok(Math.hypot(passiveW.x - passiveH.x, passiveW.y - passiveH.y) > 20, "W and H were combined into one floating card");
  await page.keyboard.type("65");await drawInput("width").press("Enter");await page.keyboard.type("28");assert.equal((await drawingState()).native.drawings.length, 1);
  report.drawRectangleLines = await assertDimensionLines([{ key: "width", kind: "linear", start: [...rectAnchor, 0], end: [rectAnchor[0] + 65, rectAnchor[1], 0], offsetDirection: [0, -1, 0] }, { key: "height", kind: "linear", start: [...rectAnchor, 0], end: [rectAnchor[0], rectAnchor[1] + 28, 0], offsetDirection: [-1, 0, 0] }], "drawing");
  await page.screenshot({ path: resolve(output, "drawing-width-height-on-geometry.png") });await drawInput("height").press("Enter");const rectangleRow = await newRow(before, 2);
  passed("Rectangle creation keeps W/H on separate geometry dimensions and preserves the two-field native commit sequence");

  await select(circleRow.id);let original = await geometry(circleRow.id);const r = dimensionValue(original, "radius");assert.equal(r.kind, "radius");assert.equal(r.editable, true);near(r.value, curve(original).radius);assert.deepEqual(r.start.slice(0, 2), curve(original).center);report.circleView = await assertDimensionLines(original.dimensions);
  await edit("radius", 32);report.circleDraftPreview = await assertSelectedPreview(circleRow.id, original);await page.locator(".cam-info-pane .iplane-panel-heading").first().click();assert.equal(await selectedInput("radius").isVisible(), true);assert.deepEqual((await geometry(circleRow.id)).geometryRef, original.geometryRef);assert.equal(await selectedInput("radius").inputValue(), "32");
  await page.keyboard.press("Escape");assert.equal(await selectedInput("radius").isVisible(), false);assert.deepEqual((await geometry(circleRow.id)).curves, original.curves);assert.equal(await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneSelectedPreviewActive; }), false);
  passed("Selected actual dimensions support a real temporary edit preview while blur and Escape leave native geometry unchanged");

  await edit("radius", 0);await selectedInput("radius").press("Enter");await page.locator("[data-iplane-selected-dimension-error]").waitFor({ state: "visible" });assert.deepEqual((await geometry(circleRow.id)).geometryRef, original.geometryRef);
  await selectedInput("radius").fill("32");const changedCircle = await commit("radius", circleRow.id);near(curve(changedCircle).radius, 32);assert.deepEqual(curve(changedCircle).center, curve(original).center);assert.equal(BigInt(changedCircle.geometryVersion), BigInt(original.geometryVersion) + 1n);assert.equal((await drawingState()).native.drawings.length, 2);
  passed("Enter edits a circle radius on the same entity and geometry version while invalid values and preview remain noncommitting");

  before = (await drawingState()).native.drawings;await command("圆弧 40,40 18 25 155");const arcRow = await newRow(before, 3);await select(arcRow.id);original = await geometry(arcRow.id);near(dimensionValue(original, "radius").value, 18);
  // Nearby arc labels are edited after the same normal zoom used at the desk.
  await zoomBy(-850);await edit("radius", 27);await assertSelectedPreview(arcRow.id, original);const changedArc = await commit("radius", arcRow.id);near(curve(changedArc).radius, 27);assert.deepEqual(curve(changedArc).center, curve(original).center);near(curve(changedArc).startAngleDeg, 25);near(curve(changedArc).endAngleDeg, 155);await zoomBy(850);
  passed("An arc radius edit keeps its native center and start/end angles");

  before = (await drawingState()).native.drawings;await command("直线 -60,-30 -20,0");const lineRow = await newRow(before, 4);await select(lineRow.id);original = await geometry(lineRow.id);near(dimensionValue(original, "length").value, 50);await edit("length", 80);await assertSelectedPreview(lineRow.id, original);const changedLine = await commit("length", lineRow.id), points = curve(changedLine).points;assert.deepEqual(points[0], [-60, -30]);near(points[1][0], 4);near(points[1][1], 18);near(dimensionValue(changedLine, "length").value, 80);
  before = (await drawingState()).native.drawings;await command("折线 -80,-60 -50,-20");const twoPointRow = await newRow(before, 5);await select(twoPointRow.id);original = await geometry(twoPointRow.id);near(dimensionValue(original, "length").value, 50);await edit("length", 70);await assertSelectedPreview(twoPointRow.id, original);const changedTwoPoint = await commit("length", twoPointRow.id);assert.equal(curve(changedTwoPoint).type, "polyline");assert.deepEqual(curve(changedTwoPoint).points[0], [-80, -60]);near(curve(changedTwoPoint).points[1][0], -38);near(curve(changedTwoPoint).points[1][1], -4);
  passed("Line and two-point polyline length labels edit native length while preserving the first endpoint and original direction");

  await select(rectangleRow.id);await command("旋转 30");await waitSelected(rectangleRow.id);const rotated = await geometry(rectangleRow.id), frame = rectangleFrame(rotated);near(frame.width, 65);near(frame.height, 28);near(dimensionValue(rotated, "width").value, 65);near(dimensionValue(rotated, "height").value, 28);
  await edit("width", 90);await selectedInput("width").press("Tab");await selectedInput("height").waitFor({ state: "visible" });await selectedInput("height").fill("44");await assertSelectedPreview(rectangleRow.id, rotated);assert.deepEqual((await geometry(rectangleRow.id)).geometryRef, rotated.geometryRef);const changedRectangle = await commit("height", rectangleRow.id), nextFrame = rectangleFrame(changedRectangle);near(nextFrame.width, 90);near(nextFrame.height, 44);
  for (let axis = 0; axis < 2; axis++) { near(nextFrame.origin[axis], frame.origin[axis]);near(nextFrame.u[axis], frame.u[axis]);near(nextFrame.v[axis], frame.v[axis]); }
  assert.equal(BigInt(changedRectangle.geometryVersion), BigInt(rotated.geometryVersion) + 1n);
  passed("A rotated rectangle edits local W/H together in one transaction while preserving its first corner and axes");

  report.rotatedAnchorsBeforeZoom = await assertDimensionLines(changedRectangle.dimensions);const zoomPoint = await screenPoint(.84, .18);await page.mouse.move(zoomPoint.x, zoomPoint.y);await page.mouse.wheel(0, -180);
  await waitForAsync(async radius => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius !== radius; }, report.rotatedAnchorsBeforeZoom.camera.radius);
  report.rotatedAnchorsAfterZoom = await assertDimensionLines((await geometry(rectangleRow.id)).dimensions);assert.notDeepEqual(report.rotatedAnchorsAfterZoom.rows.map(row => row.actual), report.rotatedAnchorsBeforeZoom.rows.map(row => row.actual));assert.deepEqual((await geometry(rectangleRow.id)).geometryRef, changedRectangle.geometryRef);
  await page.screenshot({ path: resolve(output, "rotated-rectangle-native-dimensions.png") });
  passed("Rotated and zoomed labels track native world anchors and real SDK dimension lines rather than fixed screen positions");

  await page.locator('.title-bar [data-action="undo-project"]').click();await waitSelectedReference(rectangleRow.id, rotated.geometryRef);near(rectangleFrame(await geometry(rectangleRow.id)).width, 65);near(rectangleFrame(await geometry(rectangleRow.id)).height, 28);
  await page.locator('.title-bar [data-action="redo-project"]').click();await waitSelectedReference(rectangleRow.id, changedRectangle.geometryRef);near(rectangleFrame(await geometry(rectangleRow.id)).width, 90);near(rectangleFrame(await geometry(rectangleRow.id)).height, 44);
  passed("Framework undo and redo restore both edited local rectangle dimensions and their native descriptors");

  await select(circleRow.id);const projectA = await page.evaluate(() => __icaxAppShell.getState().activeProjectId);await edit("radius", 36);
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneDrawing.Edit"))); });await selectedInput("radius").press("Enter");await waitForAsync(() => __planeNativeBridge.isResponseHeld());near(curve(await geometry(circleRow.id)).radius, 36);
  await page.evaluate(projectPath => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "标注异步隔离 B", projectPath, catalogPath: projectPath }), resolve(output, `dimension-isolation-B-${runId}.ipcam`));
  await waitForAsync(async projectA => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const id = __icaxAppShell.getState().activeProjectId;return id !== projectA && getProjectView(id).scene?.iplaneCam?.capabilities?.drawingEdits?.includes("dimensions"); }, projectA);
  await command("圆 0,0 10");const projectBRow = (await drawingState()).native.drawings[0];assert.equal((await drawingState()).native.drawings.length, 1);await select(projectBRow.id);await edit("radius", "12.5");await selectedInput("radius").press("Home");await selectedInput("radius").press("ArrowRight");await selectedInput("radius").press("Shift+ArrowRight");await selectedInput("radius").press("Shift+ArrowRight");
  const bBefore = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), input = document.querySelector('[data-iplane-selected-dimension="radius"]');globalThis.__dimensionProjectBInput = input;return { project: __icaxAppShell.getState().activeProjectId, value: input.value, selection: [input.selectionStart, input.selectionEnd], camera: view.viewport.getCameraState() }; });
  await page.evaluate(() => __planeNativeBridge.releaseResponse());await waitForAsync(async projectA => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(projectA).iplaneCamUI.busy; }, projectA);
  const bAfter = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), input = document.querySelector('[data-iplane-selected-dimension="radius"]');return { project: __icaxAppShell.getState().activeProjectId, sameInput: input === __dimensionProjectBInput, focused: document.activeElement === input, value: input.value, selection: [input.selectionStart, input.selectionEnd], camera: view.viewport.getCameraState() }; });
  assert.equal(bAfter.project, bBefore.project);assert.equal(bAfter.sameInput, true);assert.equal(bAfter.focused, true);assert.equal(bAfter.value, bBefore.value);assert.deepEqual(bAfter.selection, bBefore.selection);assert.deepEqual(bAfter.camera, bBefore.camera);near(curve(await geometry(projectBRow.id)).radius, 10);report.crossProjectEdit = { before: bBefore, after: bAfter };
  await page.keyboard.press("Escape");await page.locator(`[data-action="select-open-project"][data-project-id="${projectA}"]`).click();await waitSelected(circleRow.id);near(curve(await geometry(circleRow.id)).radius, 36);
  passed("A genuine late native edit from another open project completes without replacing the current dimension editor, focus, selection or camera");

  before = (await drawingState()).native.drawings;await command("折线 -90,70 -40,70 -15,45 -35,15 -80,25");const complexRow = await newRow(before, 6);await select(complexRow.id);const complex = await geometry(complexRow.id);assert.equal(complex.dimensions.length, 2);assert.ok(complex.dimensions.every(item => item.kind === "bounds" && item.editable === false));
  for (const key of ["width", "height"]) { await select(complexRow.id);assert.equal(await selectedRow(key).getAttribute("data-iplane-dimension-editable"), "false");assert.equal(await selectedButton(key).isVisible(), false);const box = await selectedRow(key).boundingBox();await page.mouse.dblclick(box.x + box.width / 2, box.y + box.height / 2);assert.equal(await selectedInput(key).isVisible(), false); }
  assert.deepEqual((await geometry(complexRow.id)).geometryRef, complex.geometryRef);await select(complexRow.id);report.readOnlyBounds = await assertDimensionLines(complex.dimensions);
  passed("Complex polyline extents are real native read-only dimension labels and do not open an editor");

  await page.setViewportSize({ width: 1280, height: 480 });await select(rectangleRow.id);const draftBase = await geometry(rectangleRow.id);await edit("width", "96.25");await selectedInput("width").press("Home");await selectedInput("width").press("ArrowRight");for (let i = 0; i < 3; i++) await selectedInput("width").press("Shift+ArrowRight");
  const snapshot = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), input = document.querySelector('[data-iplane-selected-dimension="width"]'), row = input.closest("label"), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane"), canvas = document.querySelector(".icax-three-viewport canvas");left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .2);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .2);globalThis.__dimensionEditSnapshot = { input, row, left, right, canvas, inputs: 0 };input.addEventListener("input", () => __dimensionEditSnapshot.inputs++);const box = row.getBoundingClientRect();return { value: input.value, selection: [input.selectionStart, input.selectionEnd], bounds: { x: box.x, y: box.y, width: box.width, height: box.height }, camera: view.viewport.getCameraState(), scroll: [left.scrollTop, right.scrollTop], capacity: [left.scrollHeight - left.clientHeight, right.scrollHeight - right.clientHeight] }; });
  assert.ok(snapshot.capacity.every(value => value > 0), "The actual selected-dimension layout was not scrollable");
  await page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), current = await view.sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }), state = await view.sceneProxy.invoke("PlaneScene.ReadState", {});await view.sceneProxy.invoke("PlaneDrawing.Edit", { id, operation: "dimensions", width: 78, expectedGeometryRef: current.geometryRef, expectedRevision: state.revision }); }, rectangleRow.id);
  const nativeAfterConcurrentEdit = await geometry(rectangleRow.id);near(rectangleFrame(nativeAfterConcurrentEdit).width, 78);assert.notDeepEqual(nativeAfterConcurrentEdit.geometryRef, draftBase.geometryRef);
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });const refreshing = refresh({ refreshView: true });await waitForAsync(() => __planeNativeBridge.isResponseHeld());await hover(await screenPoint(.78, .72));
  const latestScroll = await page.evaluate(() => { const { left, right } = __dimensionEditSnapshot;left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .8);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .8);return [left.scrollTop, right.scrollTop]; });assert.ok(latestScroll.every((value, axis) => value > snapshot.scroll[axis]));await page.evaluate(() => __planeNativeBridge.releaseResponse());await refreshing;await waitSelected(rectangleRow.id);
  report.asyncEdit = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), saved = __dimensionEditSnapshot, input = document.querySelector('[data-iplane-selected-dimension="width"]'), row = input.closest("label"), box = row.getBoundingClientRect();return { sameInput: input === saved.input, sameRow: row === saved.row, sameCanvas: document.querySelector(".icax-three-viewport canvas") === saved.canvas, sameLeft: document.querySelector("[data-iplane-list]") === saved.left, sameRight: document.querySelector(".cam-info-pane") === saved.right, focused: document.activeElement === input, value: input.value, selection: [input.selectionStart, input.selectionEnd], bounds: { x: box.x, y: box.y, width: box.width, height: box.height }, camera: view.viewport.getCameraState(), scroll: [saved.left.scrollTop, saved.right.scrollTop], revision: view.iplaneRevision, baseRef: structuredClone(view.iplaneSelectedDimensions.draft.baseRef) }; });
  assert.ok(report.asyncEdit.sameInput && report.asyncEdit.sameRow && report.asyncEdit.sameCanvas && report.asyncEdit.sameLeft && report.asyncEdit.sameRight && report.asyncEdit.focused);assert.equal(report.asyncEdit.value, snapshot.value);assert.deepEqual(report.asyncEdit.selection, snapshot.selection);assert.deepEqual(report.asyncEdit.bounds, snapshot.bounds);assert.deepEqual(report.asyncEdit.camera, snapshot.camera);assert.deepEqual(report.asyncEdit.scroll, latestScroll);assert.deepEqual(report.asyncEdit.baseRef, draftBase.geometryRef);
  const beforeRejected = await drawingState(), historyBeforeRejected = await history();assert.equal(report.asyncEdit.revision, String(beforeRejected.revision));await selectedInput("width").press("Enter");await page.locator("[data-iplane-selected-dimension-error]").waitFor({ state: "visible" });await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.busy; });
  const rejectedGeometry = await geometry(rectangleRow.id), afterRejected = await drawingState(), historyAfterRejected = await history();assert.deepEqual(rejectedGeometry.geometryRef, nativeAfterConcurrentEdit.geometryRef);assert.equal(rejectedGeometry.geometryVersion, nativeAfterConcurrentEdit.geometryVersion);assert.deepEqual(rejectedGeometry.curves, nativeAfterConcurrentEdit.curves);assert.equal(afterRejected.revision, beforeRejected.revision);assert.deepEqual(historyAfterRejected, historyBeforeRejected);assert.equal(afterRejected.native.drawings.length, 6);
  const diagnostics = await page.locator(".log-row.error").allTextContents();assert.ok(diagnostics.some(text => /PLANE_STALE_GEOMETRY/.test(text)), "The stale edit was not rejected by the geometry-reference gate");report.staleEdit = { oldRef: draftBase.geometryRef, newerRef: nativeAfterConcurrentEdit.geometryRef, uiRevision: report.asyncEdit.revision, nativeRevision: beforeRejected.revision, beforeHistory: historyBeforeRejected, afterHistory: historyAfterRejected, diagnostic: diagnostics.at(-1) };
  await page.screenshot({ path: resolve(output, "stale-dimension-draft-preserved.png") });await page.keyboard.press("Escape");
  passed("A delayed actual geometry refresh preserves selected-editor nodes, draft, caret, position, scroll and camera; a current Scene revision cannot bypass the old geometry-reference gate");

  const exactBefore = await Promise.all((await drawingState()).native.drawings.map(async row => ({ id: row.id, curves: (await geometry(row.id)).curves, dimensions: (await geometry(row.id)).dimensions })));
  report.saved = await saveAndReopen();assert.deepEqual(report.saved.after.native.drawings.map(row => row.id).sort(), report.saved.before.native.drawings.map(row => row.id).sort());for (const original of exactBefore) { const current = await geometry(original.id);assert.deepEqual(current.curves, original.curves);assert.deepEqual(current.dimensions, original.dimensions); }
  report.resources = await readRenderResources(report.saved.after.native.drawings);assert.ok(report.resources.every(item => item.type === "geometry" && item.points.length > 0));await select(rectangleRow.id);report.reopenedAnnotations = await assertDimensionLines((await geometry(rectangleRow.id)).dimensions);await page.screenshot({ path: resolve(output, "dimension-annotations-reopened.png") });
  passed("Actual project save/close/reopen preserves native dimension edits, derived annotations and render resources");
}

try {
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome", args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const pageErrors = [];page.on("pageerror", error => pageErrors.push(error.message));report.pageErrors = pageErrors;
  await page.addInitScript(({ savedProject }) => {
    const listeners = new Map();let polling = false, stopped = false, responseHold = null;
    const deliver = frame => listeners.get(frame.channelId)?.forEach(listener => listener(frame));
    async function call(action, parameters = {}) { const response = await fetch("/native-rpc", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action, ...parameters }) }).then(result => result.json());if (!response.ok) throw new Error(response.error);return response.result; }
    async function poll() {
      if (polling || stopped) return;polling = true;
      try { for (const frame of (await call("poll")).frames ?? []) { if (responseHold?.callId === String(frame.callId) && Number(frame.kind) === 2) responseHold.frames.push(frame);else deliver(frame); } }
      catch (error) { globalThis.__planeNativeTransportError = error.message; }
      finally { polling = false;if (!stopped) setTimeout(poll, 16); }
    }
    globalThis.__planeNativeBridge = { stop() { stopped = true; }, holdNextResponse(methodCode) { if (responseHold) throw new Error("A native response is already held");responseHold = { methodCode: String(methodCode), callId: null, frames: [] }; }, isResponseHeld() { return Boolean(responseHold?.frames.length); }, releaseResponse() { if (!responseHold?.frames.length) throw new Error("No completed native response to release");const frames = responseHold.frames;responseHold = null;frames.forEach(deliver); } };
    globalThis.icax = {
      async getApplicationChannelId() { return (await call("application-channel")).channelId; },
      async registerProductChannel(productId) { return (await call("register-product", { productId })).channelId; },
      async registerSceneChannel(projectId, sceneId) { return (await call("register-scene", { projectId, sceneId })).channelId; },
      subscribeSDOFrames(channel, listener) { if (!listeners.has(channel)) listeners.set(channel, new Set());listeners.get(channel).add(listener);setTimeout(poll, 0);return () => listeners.get(channel)?.delete(listener); },
      async postSDOFrame(frame) { if (responseHold && !responseHold.callId && String(frame.methodCode) === responseHold.methodCode) responseHold.callId = String(frame.callId);await call("post", { frame: { ...frame, callId: String(frame.callId), methodCode: String(frame.methodCode) } }); },
      async requestResource(request) { const result = await call("resource", { request });return { status: result.status, headers: result.headers, body: Uint8Array.from(atob(result.bodyBase64 ?? ""), character => character.charCodeAt(0)).buffer }; },
      async openFileDialog() { return null; },async saveFileDialog() { return savedProject; },
    };
  }, { savedProject });
  await page.goto(origin + "/iCAX-UI/SDK/AppShell/index.html");
  await page.waitForFunction(() => ["ready", "error"].includes(globalThis.__icaxAppShell?.getState().startupPhase), null, { timeout: 90000 });
  const startup = await page.evaluate(() => __icaxAppShell.getState());assert.equal(startup.startupPhase, "ready", startup.error || "Native startup failed");
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "尺寸标注编辑原生验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await ensurePlaneObjectsExpanded(page);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.capabilities?.enabledMethods?.includes("PlaneDrawing.Create"); });
  await exerciseDimensionAnnotations();assert.ok(checks.length > 0, "No dimension-annotation acceptance cases ran");
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  report.pass = true;await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  report.error = error.stack ?? String(error);if (page) { try { report.failureState = await drawingState();await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests });
  writeFileSync(resolve(output, "dimension-annotations-native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
