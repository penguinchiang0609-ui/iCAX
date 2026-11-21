import { ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
// Genuine AppShell/SDK input with a private native runtime. File-dialog choices
// and JSON-lines transport are test plumbing; geometry and ECS are never mocked.
import assert from "node:assert/strict";
import { exerciseSketchEditing } from "./iPlaneCAM.sketch-editing.native.browserCase.mjs";
import { exerciseEditingMenus } from "./iPlaneCAM.editing-menu.native.browserCase.mjs";
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
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-sketch-editing"));
assert.ok(process.env.ICAX_PLANE_NATIVE_BRIDGE, "Set ICAX_PLANE_NATIVE_BRIDGE to the isolated native acceptance runtime");
const executable = resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE);
assert.ok(existsSync(executable), "The selected native acceptance runtime does not exist");
mkdirSync(output, { recursive: true });
const runId = String(Date.now()), savedProject = resolve(output, `sketch-editing-${runId}.ipcam`);
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
const report = { pass: false, scope: "Actual SDK/AppShell menus and shared planar editor with native sketch/curve EC ownership, layer business, async refresh, undo and project persistence. File choices and JSON-lines transport are test plumbing; no desktop CEF or operating-system dialog acceptance is asserted.", executable, assetRoot, savedProject };
report.binaries = Object.fromEntries([basename(executable), "iPlaneCAM.dll", "PlaneNestingServices.dll", "PlaneGeometryServices.dll", "iPlaneNesting.dll", "Product.dll", "Project.dll", "ProjectFile.dll", "Database.dll", "Resources.dll"].map(name => {
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
    return { native: response.iplaneCam, revision: response.revision, tool: ui.activeTool, points: structuredClone(ui.commandPoints ?? []), busy: ui.busy, error: ui.operationError ?? "", commandInput: ui.commandInput, activeSketchId: ui.activeSketchId, formDrafts: structuredClone(ui.formDrafts), geometryTaskId: ui.geometryTaskId, applied: view.viewport.getAppliedViewState(), preview: { visible: preview?.group.visible ?? false, points: positions ? Array.from(positions.array) : [] } };
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
async function select(id) { await page.keyboard.press("Escape");await page.locator(`[data-iplane-object-id="${id}"]`).click();await waitSelected(id); }
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

try {
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome", args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const pageErrors = [];page.on("pageerror", error => pageErrors.push(error.message));report.pageErrors = pageErrors;
  await page.addInitScript(({ savedProject }) => {
    globalThis.__planeNativeFileQueue = [];
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
      async openFileDialog() { return __planeNativeFileQueue.shift() ?? null; },async saveFileDialog() { return savedProject; },
    };
  }, { savedProject });
  await page.goto(origin + "/iCAX-UI/SDK/AppShell/index.html");
  await page.waitForFunction(() => ["ready", "error"].includes(globalThis.__icaxAppShell?.getState().startupPhase), null, { timeout: 90000 });
  const startup = await page.evaluate(() => __icaxAppShell.getState());assert.equal(startup.startupPhase, "ready", startup.error || "Native startup failed");
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "草图分层重构原生验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await ensurePlaneObjectsExpanded(page);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.capabilities?.enabledMethods?.includes("PlaneDrawing.Create"); });
  await exerciseSketchEditing({ page, report, assets, source, output, savedProject, waitForAsync, passed });
  await exerciseEditingMenus({ page, report, waitForAsync, passed });assert.ok(checks.length > 0, "No sketch acceptance cases ran");
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  report.pass = true;await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  report.error = error.stack ?? String(error);if (page) { try { report.failureState = await drawingState();await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests });
  writeFileSync(resolve(output, "sketch-editing-native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
