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
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-arc-angles"));
assert.ok(process.env.ICAX_PLANE_NATIVE_BRIDGE, "Set ICAX_PLANE_NATIVE_BRIDGE to the isolated native acceptance runtime");
const executable = resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE);
assert.ok(existsSync(executable), "The selected native acceptance runtime does not exist");
mkdirSync(output, { recursive: true });
const runId = String(Date.now()), savedProject = resolve(output, `arc-angles-${runId}.ipcam`);
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
async function assertDimensionLines(descriptors, owner = "selected") {
  await waitForAsync(({ keys, owner }) => keys.every(key => {
    const row = document.querySelector(owner === "selected" ? `[data-iplane-selected-dimension-label="${key}"]` : `[data-iplane-dimension-row="${key}"]`);
    return row && !row.hidden && Number.isFinite(Number(row.dataset.iplaneDimensionAnchorX));
  }), { keys: descriptors.map(item => item.parameter ?? item.key), owner });
  const result = await page.evaluate(async ({ descriptors, owner }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { Vector3 } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const vp = getProjectView(__icaxAppShell.getState().activeProjectId).viewport, box = vp.root.querySelector("canvas").getBoundingClientRect();
    vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
    const a = vp.pointOnWorkPlane(box.left + box.width / 2, box.top + box.height / 2, [0, 0, 1]), b = vp.pointOnWorkPlane(box.left + box.width / 2 + 1, box.top + box.height / 2, [0, 0, 1]), scale = Math.hypot(b[0] - a[0], b[1] - a[1]);
    const world = point => new Vector3().fromArray(point).applyMatrix4(vp.content.matrixWorld).toArray();
    const groups = vp.dimensionContent.children.filter(object => object.isLine).map(object => {
      const p = object.geometry.attributes.position, points = [];for (let i = 0; i < p.count; i++) points.push(new Vector3(p.getX(i), p.getY(i), p.getZ(i)).applyMatrix4(object.matrixWorld).toArray());return points;
    });
    const rows = descriptors.map(descriptor => {
      const key = descriptor.parameter ?? descriptor.key;
      let expectedPoints, anchor, expectedVertexCount = null;
      if (descriptor.kind === "angle") {
        const radius = key === "startAngleDeg" ? Math.max(scale * 22, descriptor.radius * .32) : descriptor.radius + scale * 30;
        const degrees = descriptor.sweepAngleDeg, start = descriptor.startAngleDeg % 360, center = descriptor.center;
        const at = degree => [center[0] + radius * Math.cos(degree * Math.PI / 180), center[1] + radius * Math.sin(degree * Math.PI / 180), center[2] ?? 0];
        expectedPoints = [center, at(start), center, at(start + degrees)];
        const segments = Math.max(1, Math.ceil(Math.abs(degrees) / 4));
        for (let i = 0; i < segments; i++) expectedPoints.push(at(start + degrees * i / segments), at(start + degrees * (i + 1) / segments));
        expectedVertexCount = expectedPoints.length + (Math.abs(degrees) > Number.EPSILON ? 8 : 0);
        anchor = at(start + degrees / 2);
      } else {
        const distance = descriptor.kind === "radius" ? 0 : scale * 30, offset = (descriptor.offsetDirection ?? [0, 0, 0]).map(value => value * distance);
        const start = descriptor.start.map((value, axis) => value + offset[axis]), end = descriptor.end.map((value, axis) => value + offset[axis]);
        expectedPoints = [start, end];anchor = start.map((value, axis) => (value + end[axis]) / 2);
      }
      const projected = new Vector3().fromArray(anchor).applyMatrix4(vp.content.matrixWorld).project(vp.camera), expected = [box.left + (projected.x + 1) * box.width / 2, box.top + (1 - projected.y) * box.height / 2];
      const row = document.querySelector(owner === "selected" ? `[data-iplane-selected-dimension-label="${key}"]` : `[data-iplane-dimension-row="${key}"]`), bounds = row.getBoundingClientRect();
      return { key, kind: descriptor.kind, expectedPoints: expectedPoints.map(world), expectedVertexCount, expected, actual: [Number(row.dataset.iplaneDimensionAnchorX), Number(row.dataset.iplaneDimensionAnchorY)], text: row.textContent, title: row.title, bounds: { x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height } };
    });
    return { groups, sprites: vp.dimensionContent.children.filter(object => object.isSprite).length, rows, camera: vp.getCameraState() };
  }, { descriptors, owner });
  assert.equal(result.groups.length, descriptors.length, "SDK dimensionContent did not contain exactly one real line group per descriptor");assert.equal(result.sprites, 0, "Duplicate dimension sprites appeared behind the DOM labels");
  const close = (a, b) => Math.hypot(...a.map((value, axis) => value - b[axis])) < 1e-3;
  for (let index = 0; index < result.rows.length; index++) {
    const row = result.rows[index], points = result.groups[index];
    if (row.kind === "angle") {
      assert.equal(points.length, row.expectedVertexCount, row.key + " lost its actual arc samples or arrows");
      for (let i = 0; i < row.expectedPoints.length; i++) assert.ok(close(points[i], row.expectedPoints[i]), row.key + " SDK ray/arc vertex " + i + " differs from the native geometry");
      assert.match(row.text, /°/);assert.match(row.title, /°/);
    } else for (const point of row.expectedPoints) assert.ok(points.some(actual => close(actual, point)), row.key + " lost a real SDK linear/radius endpoint");
    for (let axis = 0; axis < 2; axis++) near(row.actual[axis], row.expected[axis], row.key + " label left its native world anchor", 1e-3);
  }
  return result;
}
async function zoomBy(delta) {
  const radius = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius; });
  await hover(await screenPoint(.5, .5));await page.mouse.wheel(0, delta);
  await waitForAsync(async radius => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius !== radius; }, radius);
}
const sweep = c => ((c.endAngleDeg - c.startAngleDeg) % 360 + 360) % 360 || 360;
function expectedArcDimensions(center, radius, alpha, theta) {
  const c = [...center.slice(0, 2), 0], at = degree => [c[0] + radius * Math.cos(degree * Math.PI / 180), c[1] + radius * Math.sin(degree * Math.PI / 180), 0];
  return [{ key: "radius", kind: "radius", value: radius, start: c, end: at(alpha + theta / 2) },
    { key: "startAngleDeg", kind: "angle", unit: "deg", value: alpha, center: c, radius, startAngleDeg: 0, sweepAngleDeg: alpha, start: at(0), end: at(alpha) },
    { key: "sweepAngleDeg", kind: "angle", unit: "deg", value: theta, center: c, radius, startAngleDeg: alpha, sweepAngleDeg: theta, start: at(alpha), end: at(alpha + theta) }];
}
function assertArc(geometry, { radius, alpha, theta, center }) {
  const c = curve(geometry);assert.equal(c.type, "arc");near(c.radius, radius);near(((c.startAngleDeg % 360) + 360) % 360, alpha);near(sweep(c), theta);
  if (center) assert.deepEqual(c.center, center);
  assert.deepEqual(geometry.dimensions.map(item => item.parameter), ["radius", "startAngleDeg", "sweepAngleDeg"]);
  for (const [key, value] of [["radius", radius], ["startAngleDeg", alpha], ["sweepAngleDeg", theta]]) { const d = dimensionValue(geometry, key);near(d.value, value);assert.equal(d.editable, true);if (key !== "radius") { assert.equal(d.kind, "angle");assert.equal(d.unit, "deg"); } }
}
async function history() { return page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.getUndoRedoState(); }); }
async function refresh({ refreshView = false } = {}) { return page.evaluate(async refreshView => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return refreshPlaneState(view.iplaneLive.context, view, view.iplaneLive.ops, { render: true, refreshView }); }, refreshView); }
async function exerciseArcAngles() {
  const initial = await drawingState();assert.equal(initial.native.drawings.length, 0);assert.ok(initial.native.capabilities.drawingEdits.includes("dimensions"));await clickPlaneCommand(page, "plane.snap-toggle");
  const createCode = await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");return String(makeSDOMethodCodeFromName("PlaneDrawing.Create")); });
  const createCalls = () => requests.filter(item => item.action === "post" && String(item.frame?.methodCode) === createCode).length;
  let before = initial.native.drawings;
  await start("arc");const centerScreen = await screenPoint(.5, .5), startScreen = await screenPoint(.65, .5), high = await screenPoint(.5, .3), low = await screenPoint(.5, .7), mouseCenter = await pointerWorld(centerScreen), startWorld = await pointerWorld(startScreen), radius = Math.hypot(startWorld[0] - mouseCenter[0], startWorld[1] - mouseCenter[1]);
  await click(centerScreen);await waitPrefix(1);await click(startScreen);await waitPrefix(2);
  const accepted = (await drawingState()).points, acceptedCenter = accepted[0], acceptedStart = accepted[1], acceptedRadius = Math.hypot(acceptedStart[0] - acceptedCenter[0], acceptedStart[1] - acceptedCenter[1]);
  const angleAt = point => { const angle = Math.atan2(point[1] - acceptedCenter[1], point[0] - acceptedCenter[0]) * 180 / Math.PI;return angle < 0 ? angle + 360 : angle; }, mouseAlpha = angleAt(acceptedStart), highWorld = await pointerWorld(high), lowWorld = await pointerWorld(low), minorTheta = angleAt(highWorld) - mouseAlpha, majorTheta = angleAt(lowWorld) - mouseAlpha;
  await hover(high);report.mouseMinorPreview = await assertDimensionLines(expectedArcDimensions(acceptedCenter, acceptedRadius, mouseAlpha, minorTheta), "drawing");
  await hover(low);report.mouseMajorPreview = await assertDimensionLines(expectedArcDimensions(acceptedCenter, acceptedRadius, mouseAlpha, majorTheta), "drawing");
  assert.equal((await drawingState()).native.drawings.length, 0);assert.equal(createCalls(), 0);assert.notEqual(report.mouseMinorPreview.rows[2].text, report.mouseMajorPreview.rows[2].text);
  await click(low);const mouseArc = await newRow(before, 1);assertArc(await geometry(mouseArc.id), { radius: acceptedRadius, alpha: mouseAlpha, theta: majorTheta });assert.equal(createCalls(), 1);
  passed("Real three-point arc input previews minor and major positive sweeps with actual SDK angle rays, arcs and degree labels before a single native Create");

  before = (await drawingState()).native.drawings;const numericCalls = createCalls();await start("arc");const numericCenterScreen = await screenPoint(.37, .58), numericCenter = await pointerWorld(numericCenterScreen);await click(numericCenterScreen);await waitPrefix(1);await hover(await screenPoint(.53, .4));await page.keyboard.type("22");
  await drawInput("radius").press("Tab");await drawInput("startAngleDeg").fill("-30");await drawInput("startAngleDeg").press("Tab");await drawInput("sweepAngleDeg").fill("270");await drawInput("sweepAngleDeg").press("Tab");assert.equal(await drawInput("radius").isVisible(), true);assert.equal(createCalls(), numericCalls);
  await drawInput("radius").press("Enter");assert.equal(await drawInput("startAngleDeg").isVisible(), true);assert.equal(createCalls(), numericCalls);
  await drawInput("startAngleDeg").press("Enter");await waitPrefix(2);assert.equal(await drawInput("sweepAngleDeg").isVisible(), true);assert.equal(createCalls(), numericCalls);assert.equal((await drawingState()).native.drawings.length, before.length);
  report.numericDrawing = await assertDimensionLines(expectedArcDimensions(numericCenter, 22, 330, 270), "drawing");
  await page.screenshot({ path: resolve(output, "arc-angle-drawing.png") });await drawInput("sweepAngleDeg").press("Enter");const numericArc = await newRow(before, 2);assertArc(await geometry(numericArc.id), { radius: 22, alpha: 330, theta: 270 });assert.equal(createCalls(), numericCalls + 1);
  await select(numericArc.id);report.numericSelected = await assertDimensionLines((await geometry(numericArc.id)).dimensions);await page.screenshot({ path: resolve(output, "arc-angle-selected.png") });
  passed("R, alpha and sweep accept independent numeric drafts; Tab never creates, R/alpha Enter only advance, and the final sweep Enter creates once with negative alpha normalized");

  before = (await drawingState()).native.drawings;await command("圆弧 30,20 18 350.123456789 30");const precisionArc = await newRow(before, 3);await select(precisionArc.id);const original = await geometry(precisionArc.id), originalCurve = curve(original), originalSweep = sweep(originalCurve);
  // As in normal use, zoom a small arc before double-clicking nearby labels.
  await zoomBy(-850);
  await page.screenshot({ path: resolve(output, "arc-angle-selected.png") });
  await edit("radius", 32);await assertSelectedPreview(precisionArc.id, original);const radiusChanged = await commit("radius", precisionArc.id);
  assert.equal(curve(radiusChanged).startAngleDeg, originalCurve.startAngleDeg);assert.equal(curve(radiusChanged).endAngleDeg, originalCurve.endAngleDeg);assert.deepEqual(curve(radiusChanged).center, originalCurve.center);near(curve(radiusChanged).radius, 32);assert.equal(BigInt(radiusChanged.geometryVersion), BigInt(original.geometryVersion) + 1n);
  await edit("startAngleDeg", "-20");await assertSelectedPreview(precisionArc.id, radiusChanged);const alphaChanged = await commit("startAngleDeg", precisionArc.id);assertArc(alphaChanged, { radius: 32, alpha: 340, theta: originalSweep, center: originalCurve.center });
  passed("A radius-only arc edit preserves exact native endpoint angles; editing negative alpha normalizes it while preserving the original positive sweep and center");

  await edit("radius", 36);await selectedInput("radius").press("Tab");await selectedInput("startAngleDeg").fill("-450");await selectedInput("startAngleDeg").press("Tab");await selectedInput("sweepAngleDeg").fill("360");await assertSelectedPreview(precisionArc.id, alphaChanged);assert.deepEqual((await geometry(precisionArc.id)).geometryRef, alphaChanged.geometryRef);
  const full = await commit("sweepAngleDeg", precisionArc.id);assertArc(full, { radius: 36, alpha: 270, theta: 360, center: originalCurve.center });assert.equal(BigInt(full.geometryVersion), BigInt(alphaChanged.geometryVersion) + 1n);report.fullCircleAngles = await assertDimensionLines(full.dimensions);
  await edit("startAngleDeg", "0");const zeroAlpha = await commit("startAngleDeg", precisionArc.id);assertArc(zeroAlpha, { radius: 36, alpha: 0, theta: 360, center: originalCurve.center });report.zeroAlphaFullCircle = await assertDimensionLines(zeroAlpha.dimensions);
  passed("Selected R/alpha/sweep Tab drafts commit in one native geometry version, including 360-degree sweep and zero alpha without collapsing the real full-circle angle arc");

  const invalidBase = await geometry(precisionArc.id), invalidState = await drawingState(), invalidHistory = await history();await edit("sweepAngleDeg", "0");report.invalidSweeps = [];
  for (const value of ["0", "361", "-1", "1e309"]) {
    await selectedInput("sweepAngleDeg").fill(value);await selectedInput("sweepAngleDeg").press("Enter");await page.locator("[data-iplane-selected-dimension-error]").waitFor({ state: "visible" });
    const after = await geometry(precisionArc.id), state = await drawingState();assert.deepEqual(after.geometryRef, invalidBase.geometryRef);assert.equal(after.geometryVersion, invalidBase.geometryVersion);assert.deepEqual(after.curves, invalidBase.curves);assert.equal(state.revision, invalidState.revision);assert.deepEqual(await history(), invalidHistory);assert.equal(await selectedInput("sweepAngleDeg").inputValue(), value);report.invalidSweeps.push(value);
  }
  await selectedInput("sweepAngleDeg").fill("180");const half = await commit("sweepAngleDeg", precisionArc.id);assertArc(half, { radius: 36, alpha: 0, theta: 180, center: originalCurve.center });
  passed("Zero, negative, over-360 and nonfinite selected sweeps preserve native geometry reference/version, scene revision, history and editable drafts; correction commits normally");

  before = (await drawingState()).native.drawings;await start("arc");await click(await screenPoint(.61, .55));await waitPrefix(1);await hover(await screenPoint(.74, .4));await page.keyboard.type("12");await drawInput("radius").press("Enter");await drawInput("startAngleDeg").fill("37");await drawInput("startAngleDeg").press("Enter");await drawInput("sweepAngleDeg").fill("1e-20");
  const tinyState = await drawingState(), tinyHistory = await history(), tinyCalls = createCalls();await drawInput("sweepAngleDeg").press("Enter");await page.locator("[data-iplane-dimension-error]").waitFor({ state: "visible" });
  const afterTiny = await drawingState();assert.equal(afterTiny.native.drawings.length, before.length);assert.equal(afterTiny.revision, tinyState.revision);assert.equal(createCalls(), tinyCalls);assert.deepEqual(await history(), tinyHistory);assert.equal(await drawInput("sweepAngleDeg").inputValue(), "1e-20");
  await drawInput("sweepAngleDeg").fill("180");await page.screenshot({ path: resolve(output, "arc-angle-drawing.png") });await drawInput("sweepAngleDeg").press("Enter");const tinyCorrected = await newRow(before, before.length + 1);assertArc(await geometry(tinyCorrected.id), { radius: 12, alpha: 37, theta: 180 });
  passed("A positive sweep too small to be represented at alpha 37 is blocked before native Create, keeps the draft and history, and accepts a corrected sweep");

  await zoomBy(850);
  await select(numericArc.id);const numericGeometry = await geometry(numericArc.id);report.beforeZoom = await assertDimensionLines(numericGeometry.dimensions);const zoomPoint = await screenPoint(.82, .2);await hover(zoomPoint);await page.mouse.wheel(0, -180);
  await waitForAsync(async radius => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius !== radius; }, report.beforeZoom.camera.radius);
  report.afterZoom = await assertDimensionLines((await geometry(numericArc.id)).dimensions);assert.notDeepEqual(report.afterZoom.rows.map(row => row.actual), report.beforeZoom.rows.map(row => row.actual));assert.deepEqual((await geometry(numericArc.id)).geometryRef, numericGeometry.geometryRef);
  before = (await drawingState()).native.drawings;await command("圆 -50,30 12");const circleRow = await newRow(before, before.length + 1);await select(circleRow.id);const circle = await geometry(circleRow.id);assert.deepEqual(circle.dimensions.map(item => item.parameter), ["radius"]);assert.equal(await selectedRow("startAngleDeg").isVisible(), false);assert.equal(await selectedRow("sweepAngleDeg").isVisible(), false);report.circleRadiusOnly = await assertDimensionLines(circle.dimensions);
  before = (await drawingState()).native.drawings;await command("直线 -65,-30 -30,-10");await newRow(before, before.length + 1);
  passed("Angle labels remain attached to independently projected world anchors during zoom, and circles retain radius-only native annotations");

  await page.setViewportSize({ width: 1280, height: 480 });await select(numericArc.id);const asyncBase = await geometry(numericArc.id);await edit("sweepAngleDeg", "210.5");await selectedInput("sweepAngleDeg").press("Home");await selectedInput("sweepAngleDeg").press("ArrowRight");await selectedInput("sweepAngleDeg").press("Shift+ArrowRight");await selectedInput("sweepAngleDeg").press("Shift+ArrowRight");
  const snapshot = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), view = getProjectView(__icaxAppShell.getState().activeProjectId), input = document.querySelector('[data-iplane-selected-dimension="sweepAngleDeg"]'), row = input.closest("label"), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane"), canvas = document.querySelector(".icax-three-viewport canvas");
    left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .2);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .2);
    globalThis.__arcSnapshot = { input, row, left, right, canvas, inputs: 0 };input.addEventListener("input", () => __arcSnapshot.inputs++);
    const box = row.getBoundingClientRect();return { value: input.value, selection: [input.selectionStart, input.selectionEnd], bounds: { x: box.x, y: box.y, width: box.width, height: box.height }, camera: view.viewport.getCameraState(), scroll: [left.scrollTop, right.scrollTop], capacity: [left.scrollHeight - left.clientHeight, right.scrollHeight - right.clientHeight] };
  });
  assert.ok(snapshot.capacity.every(value => value > 0), "The actual angle editor must have scrollable left and right panes");
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });const refreshing = refresh({ refreshView: true });await waitForAsync(() => __planeNativeBridge.isResponseHeld());await hover(await screenPoint(.79, .71));
  const latestScroll = await page.evaluate(() => { const { left, right } = __arcSnapshot;left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .8);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .8);return [left.scrollTop, right.scrollTop]; });assert.ok(latestScroll.every((value, axis) => value > snapshot.scroll[axis]));
  await page.evaluate(() => __planeNativeBridge.releaseResponse());await refreshing;await waitSelected(numericArc.id);
  report.asyncAngleDraft = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), view = getProjectView(__icaxAppShell.getState().activeProjectId), saved = __arcSnapshot, input = document.querySelector('[data-iplane-selected-dimension="sweepAngleDeg"]'), row = input.closest("label"), box = row.getBoundingClientRect();
    return { sameInput: input === saved.input, sameRow: row === saved.row, sameCanvas: document.querySelector(".icax-three-viewport canvas") === saved.canvas, sameLeft: document.querySelector("[data-iplane-list]") === saved.left, sameRight: document.querySelector(".cam-info-pane") === saved.right, focused: document.activeElement === input, value: input.value, selection: [input.selectionStart, input.selectionEnd], bounds: { x: box.x, y: box.y, width: box.width, height: box.height }, camera: view.viewport.getCameraState(), scroll: [saved.left.scrollTop, saved.right.scrollTop] };
  });
  assert.ok(report.asyncAngleDraft.sameInput && report.asyncAngleDraft.sameRow && report.asyncAngleDraft.sameCanvas && report.asyncAngleDraft.sameLeft && report.asyncAngleDraft.sameRight && report.asyncAngleDraft.focused);assert.equal(report.asyncAngleDraft.value, snapshot.value);assert.deepEqual(report.asyncAngleDraft.selection, snapshot.selection);assert.deepEqual(report.asyncAngleDraft.bounds, snapshot.bounds);assert.deepEqual(report.asyncAngleDraft.camera, snapshot.camera);assert.deepEqual(report.asyncAngleDraft.scroll, latestScroll);await assertSelectedPreview(numericArc.id, asyncBase);
  await selectedInput("sweepAngleDeg").fill("211.5");assert.equal(await page.evaluate(() => __arcSnapshot.inputs), 1, "The original angle input listener was replaced during refresh");
  await page.locator(".cam-info-pane .iplane-panel-heading").first().click();assert.equal(await selectedInput("sweepAngleDeg").isVisible(), true);assert.deepEqual((await geometry(numericArc.id)).geometryRef, asyncBase.geometryRef);await page.keyboard.press("Escape");assert.equal(await selectedInput("sweepAngleDeg").isVisible(), false);assert.deepEqual((await geometry(numericArc.id)).curves, asyncBase.curves);
  await edit("sweepAngleDeg", "210.5");const asyncChanged = await commit("sweepAngleDeg", numericArc.id);assertArc(asyncChanged, { radius: 22, alpha: 330, theta: 210.5, center: curve(asyncBase).center });
  passed("A genuine delayed native refresh preserves angle editor nodes/listeners, focus, caret, draft, position, latest pane scroll and camera; blur/Escape do not write and Enter commits the exact corrected angle");

  await page.locator('.title-bar [data-action="undo-project"]').click();await waitSelectedReference(numericArc.id, asyncBase.geometryRef);assertArc(await geometry(numericArc.id), { radius: 22, alpha: 330, theta: 270 });
  await page.locator('.title-bar [data-action="redo-project"]').click();await waitSelectedReference(numericArc.id, asyncChanged.geometryRef);assertArc(await geometry(numericArc.id), { radius: 22, alpha: 330, theta: 210.5 });report.redoneAngles = await assertDimensionLines((await geometry(numericArc.id)).dimensions);
  passed("Framework Undo/Redo restore the actual angle geometry reference and all three native dimension descriptors");

  await page.setViewportSize({ width: 1600, height: 1000 });const exactBefore = await Promise.all((await drawingState()).native.drawings.map(async row => ({ id: row.id, curves: (await geometry(row.id)).curves, dimensions: (await geometry(row.id)).dimensions })));
  report.saved = await saveAndReopen();assert.deepEqual(report.saved.after.native.drawings.map(row => row.id).sort(), report.saved.before.native.drawings.map(row => row.id).sort());
  for (const original of exactBefore) { const actual = await geometry(original.id);assert.deepEqual(actual.curves, original.curves);assert.deepEqual(actual.dimensions, original.dimensions); }
  report.resources = await readRenderResources(report.saved.after.native.drawings);assert.ok(report.resources.every(item => item.type === "geometry" && item.points.length > 0));await select(numericArc.id);report.reopenedAngles = await assertDimensionLines((await geometry(numericArc.id)).dimensions);await page.screenshot({ path: resolve(output, "arc-angles-reopened.png") });
  passed("Real project save, close and reopen retain all exact arc/circle/line curves, native angular descriptors and SDK geometry resources");
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
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "圆弧角度原生验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await ensurePlaneObjectsExpanded(page);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.capabilities?.enabledMethods?.includes("PlaneDrawing.Create"); });
  await exerciseArcAngles();assert.ok(checks.length > 0, "No arc-angle acceptance cases ran");
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  report.pass = true;await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  report.error = error.stack ?? String(error);if (page) { try { report.failureState = await drawingState();await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests });
  writeFileSync(resolve(output, "arc-angles-native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
