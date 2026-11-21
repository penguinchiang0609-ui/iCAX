// Actual AppShell/SDK pointer and keyboard operations reach the native ECS.
// The loopback server and stdio bridge provide transport and file choices only.
import assert from "node:assert/strict";
import { clickPlaneCommand, ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
import { assertDrawingMenu } from "./iPlaneCAM.drawingMenuAssertions.mjs";
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
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-polyline-angle"));
mkdirSync(output, { recursive: true });
const runId = String(Date.now()), savedProject = resolve(output, `polyline-angle-${runId}.ipcam`);
const executable = resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE || resolve(root, "output/tests/iplane-cam-native/runtime-final/iPlaneCAMNativeBridge.exe"));
assert.ok(existsSync(executable), "Set ICAX_PLANE_NATIVE_BRIDGE to the updated native test runtime");
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
const report = { pass: false, scope: "Actual AppShell/SDK mouse preview and readonly tangent angles with a private native PlaneDrawing runtime. SDK ray/arc vertices and camera-projected anchors are independently checked; native closure/finish/Escape entity counts are real. Desktop CEF and OS dialogs are outside this scope.", executable, assetRoot, savedProject };
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
    const native = await view.sceneProxy.invoke("PlaneScene.ReadState", {});
    return { native: native.iplaneCam, revision: native.revision, points: structuredClone(ui.commandPoints ?? []), tool: ui.activeTool, busy: ui.busy, error: ui.operationError ?? "", applied: view.viewport.getAppliedViewState() };
  });
}
async function geometry(id) {
  return page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }); }, id);
}
async function waitCount(count) {
  await waitForAsync(async count => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);
    return !ui.busy && view.scene?.iplaneCam?.drawings?.length === count;
  }, count);
  assert.equal((await drawingState()).native.drawings.length, count);
}
async function waitPrefix(count) {
  await waitForAsync(async count => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.commandPoints.length === count; }, count);
}
async function start() {
  await clickPlaneCommand(page, "plane.draw-polyline");
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return ui.activeTool === "plane.draw-polyline" && !ui.busy && !ui.commandPoints.length; });
}
async function screenPoint(x, y) {
  const box = await page.locator(".icax-three-viewport canvas").boundingBox();return { x: Math.round(box.x + box.width * x), y: Math.round(box.y + box.height * y) };
}
async function click(point) { await page.mouse.click(point.x, point.y); }
async function triangle(points) { await start();for (const [index, point] of points.entries()) { await click(point);await waitPrefix(index + 1); } }
async function openMenu(point) {
  await page.mouse.click(point.x, point.y, { button: "right" });
  const menu = page.locator("[data-iplane-context-menu]");await menu.waitFor({ state: "visible" });
  await assertDrawingMenu(menu, { polyline: true });
  const bounds = await menu.boundingBox(), viewport = page.viewportSize(), canvas = await page.locator(".icax-three-viewport canvas").boundingBox();
  assert.ok(bounds.x >= 0 && bounds.y >= 0 && bounds.x + bounds.width <= viewport.width + 1 && bounds.y + bounds.height <= viewport.height + 1, "The cursor menu escaped the visible window");
  assert.ok(bounds.x >= canvas.x - 1 && bounds.y >= canvas.y - 1 && bounds.x + bounds.width <= canvas.x + canvas.width + 1 && bounds.y + bounds.height <= canvas.y + canvas.height + 1, "The cursor menu escaped the drawing canvas boundary");
  assert.ok(Math.abs(bounds.x - point.x) <= bounds.width + 12 && Math.abs(bounds.y - point.y) <= bounds.height + 12, "The context menu did not open near the pointer");
  return { menu, bounds, point, viewport, canvas };
}
async function menuAction(action) { await page.locator(`[data-iplane-context-menu] [data-iplane-action="${action}"]`).click(); }
async function assertSelectionTool() {
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return !ui.busy && ui.activeTool === "plane.select" && !ui.commandPoints.length; });
}
async function newRow(before, count) {
  await waitCount(count);const rows = (await drawingState()).native.drawings, ids = new Set(before.map(row => row.id));
  const added = rows.filter(row => !ids.has(row.id));assert.equal(added.length, 1);return added[0];
}
async function assertPolyline(id, closed, pointCount) {
  const result = await geometry(id);assert.equal(result.curves.length, 1);
  const curve = result.curves[0];assert.equal(curve.type, "polyline");assert.equal(curve.closed, closed);assert.equal(curve.points.length, pointCount);
  if (closed) assert.ok(Math.hypot(...curve.points[0].map((value, axis) => value - curve.points.at(-1)[axis])) > 1e-7, "A closed polyline retained a duplicate terminal vertex");
  return result;
}
async function waitClosed(id, closed) {
  await waitForAsync(async ({ id, closed }) => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);
    return !ui.busy && view.scene?.iplaneCam?.drawings?.find(row => row.id === id)?.closed === closed;
  }, { id, closed });
  return (await drawingState()).native.drawings.find(row => row.id === id);
}
const near = (a, b, message, tolerance = 1e-7) => assert.ok(Math.abs(a - b) <= tolerance, message ?? `${a} differs from ${b}`);
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
        const radius = key === "includedAngleDeg" ? descriptor.radius : key === "startAngleDeg" ? Math.max(scale * 22, descriptor.radius * .32) : descriptor.radius + scale * 30;
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
async function pointerWorld(point) { return page.evaluate(async point => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.pointOnWorkPlane(point.x, point.y, [0, 0, 1]).slice(0, 2); }, point); }
async function hover(point) { await page.mouse.move(point.x, point.y, { steps: 4 }); }
function segmentDimensions(a, b, c) {
  const ab = b.map((value, axis) => value - a[axis]), bc = c.map((value, axis) => value - b[axis]), previousLength = Math.hypot(...ab), length = Math.hypot(...bc), center = [...b, 0], radius = Math.min(previousLength, length) * .28;
  const degrees = vector => { const angle = Math.atan2(vector[1], vector[0]) * 180 / Math.PI;return angle < 0 ? angle + 360 : angle; };
  const tangent = degrees(ab), current = degrees(bc);let difference = Math.atan2(bc[1], bc[0]) - Math.atan2(ab[1], ab[0]);
  if (difference > Math.PI) difference -= Math.PI * 2;if (difference < -Math.PI) difference += Math.PI * 2;
  const sweepAngleDeg = Math.abs(difference) * 180 / Math.PI, startAngleDeg = difference < 0 ? current : tangent;
  const at = angle => [b[0] + radius * Math.cos(angle * Math.PI / 180), b[1] + radius * Math.sin(angle * Math.PI / 180), 0];
  return [{ key: "length", kind: "length", value: length, start: center, end: [...c, 0], offsetDirection: [-bc[1] / length, bc[0] / length, 0] },
    { key: "includedAngleDeg", label: "∠", kind: "angle", unit: "deg", value: sweepAngleDeg, center, radius, startAngleDeg, sweepAngleDeg, start: at(startAngleDeg), end: at(startAngleDeg + sweepAngleDeg) }];
}
async function assertNoAngle() {
  assert.equal(await page.locator('[data-iplane-dimension-row="includedAngleDeg"]').isVisible(), false);
}
async function assertAngleCleared() {
  await assertSelectionTool();assert.equal(await page.locator("[data-iplane-dynamic-input]").isVisible(), false);
  const state = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return { auxiliary: view.iplaneCamUI.auxiliaryDimensions?.length ?? 0, owner: view.iplaneDimensionOwner }; });
  assert.equal(state.auxiliary, 0);assert.notEqual(state.owner, "drawing");
}
async function assertTangentAngle(a, b, c, expectedAngle) {
  const descriptors = segmentDimensions(a, b, c);near(descriptors[1].value, expectedAngle, "The independent forward-tangent angle differs");
  const view = await assertDimensionLines(descriptors, "drawing"), row = page.locator('[data-iplane-dimension-row="includedAngleDeg"]');
  assert.match(await row.innerText(), /∠.*°/);assert.equal(await row.locator("input").isVisible(), false);
  assert.equal(await page.locator('[data-iplane-dimension-row="length"]').isVisible(), true);return view;
}
async function exercisePolylineAngles() {
  await ensurePlaneObjectsExpanded(page);
  assert.equal((await drawingState()).native.drawings.length, 0);await clickPlaneCommand(page, "plane.snap-toggle");
  const aScreen = await screenPoint(.34, .55), bScreen = await screenPoint(.53, .55), upScreen = await screenPoint(.53, .34), downScreen = await screenPoint(.53, .76), aheadScreen = await screenPoint(.72, .55), behindScreen = await screenPoint(.38, .55);
  await start();await click(aScreen);await waitPrefix(1);await hover(bScreen);await page.locator('[data-iplane-dimension-row="length"]').waitFor({ state: "visible" });await assertNoAngle();
  await click(bScreen);await waitPrefix(2);await assertNoAngle();const accepted = (await drawingState()).points, [a, b] = accepted;await hover(bScreen);await assertNoAngle();assert.equal((await drawingState()).native.drawings.length, 0);
  passed("The first segment shows only L, and a zero-length current segment never invents a forward-tangent angle or native entity");

  report.angles = [];
  for (const [name, screen, expected] of [["left", upScreen, 90], ["right", downScreen, 90], ["straight", aheadScreen, 0], ["reverse", behindScreen, 180]]) {
    const c = await pointerWorld(screen);await hover(screen);report.angles.push({ name, expected, a, b, c, presentation: await assertTangentAngle(a, b, c, expected) });assert.equal((await drawingState()).native.drawings.length, 0);
  }
  assert.notDeepEqual(report.angles[0].presentation.groups[1], report.angles[1].presentation.groups[1]);
  passed("Live mouse movement shows readonly L and degree-valued tangent angle together: left/right 90, straight 0 and reverse 180, with actual SDK rays, sampled arcs and arrows");

  const c = await pointerWorld(upScreen);await hover(upScreen);report.beforeZoom = await assertTangentAngle(a, b, c, 90);await page.mouse.wheel(0, -240);
  await waitForAsync(async radius => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius !== radius; }, report.beforeZoom.camera.radius);
  report.afterZoom = await assertTangentAngle(a, b, c, 90);assert.notDeepEqual(report.afterZoom.rows[1].actual, report.beforeZoom.rows[1].actual);assert.deepEqual((await drawingState()).points, accepted);assert.equal((await drawingState()).native.drawings.length, 0);
  await page.screenshot({ path: resolve(output, "polyline-tangent-angle.png") });await page.mouse.wheel(0, 240);await waitForAsync(async radius => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius !== radius; }, report.afterZoom.camera.radius);
  passed("Zoom preserves the model-space tangent rays and true angle radius while both DOM labels follow independent camera projections without changing accepted vertices");

  await click(upScreen);await waitPrefix(3);await hover(aheadScreen);await page.keyboard.press("c");const closedRow = await newRow([], 1);const closed = await assertPolyline(closedRow.id, true, 3);await assertAngleCleared();
  await start();await click(aScreen);await waitPrefix(1);await click(bScreen);await waitPrefix(2);const finishPoints = (await drawingState()).points;await hover(upScreen);await page.locator('[data-iplane-dimension-row="includedAngleDeg"]').waitFor({ state: "visible" });await openMenu(aheadScreen);await menuAction("finish-drawing");const openRow = await newRow([closedRow], 2), open = await assertPolyline(openRow.id, false, 2);assert.deepEqual(open.curves[0].points, finishPoints);await assertAngleCleared();report.nativePolylines = { closed, open };
  passed("Closing and finishing clear the temporary angle and length annotations and create exactly one native polyline each, without committing the hovered endpoint");

  await start();await click(aScreen);await waitPrefix(1);const beforeRepeatedPoint = await drawingState();await click(aScreen);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return Boolean(getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.operationError); });
  const rejectedRepeatedPoint = await drawingState();assert.match(rejectedRepeatedPoint.error, /曲段参数无效/);assert.deepEqual(rejectedRepeatedPoint.points, beforeRepeatedPoint.points);assert.equal(rejectedRepeatedPoint.points.length, 1);assert.equal(rejectedRepeatedPoint.revision, beforeRepeatedPoint.revision);assert.equal(rejectedRepeatedPoint.native.drawings.length, 2);
  report.repeatedPointRejected = { pointsPreserved: true, revisionUnchanged: true, nativeDrawingCount: 2, error: rejectedRepeatedPoint.error };
  await hover(upScreen);await assertNoAngle();await page.keyboard.press("Escape");await assertAngleCleared();assert.equal((await drawingState()).native.drawings.length, 2);
  const diagonalA = await screenPoint(.36, .65), diagonalB = await screenPoint(.52, .44), diagonalC = { x: diagonalB.x + diagonalB.y - diagonalA.y, y: diagonalB.y - (diagonalB.x - diagonalA.x) };
  await start();await click(diagonalA);await waitPrefix(1);await click(diagonalB);await waitPrefix(2);const diagonalPoints = (await drawingState()).points;await hover(diagonalC);const diagonalEnd = await pointerWorld(diagonalC), ab = diagonalPoints[1].map((value, axis) => value - diagonalPoints[0][axis]), bc = diagonalEnd.map((value, axis) => value - diagonalPoints[1][axis]);
  report.diagonalAngle = Math.acos(Math.max(-1, Math.min(1, (ab[0] * bc[0] + ab[1] * bc[1]) / (Math.hypot(...ab) * Math.hypot(...bc))))) * 180 / Math.PI;
  report.diagonalTangent = await assertTangentAngle(diagonalPoints[0], diagonalPoints[1], diagonalEnd, report.diagonalAngle);await page.keyboard.press("Escape");await assertAngleCleared();await waitCount(2);
  passed("A repeated point is rejected explicitly without changing the single-point prefix, revision or native entities and does not create a zero-length angle; a valid diagonal segment gives its independently calculated relative angle, and Escape clears both drafts");
}

try {
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome", args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const pageErrors = [];page.on("pageerror", error => pageErrors.push(error.message));report.pageErrors = pageErrors;
  await page.addInitScript(({ savedProject }) => {
    const listeners = new Map();let polling = false, stopped = false;
    async function call(action, parameters = {}) { const response = await fetch("/native-rpc", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action, ...parameters }) }).then(result => result.json());if (!response.ok) throw new Error(response.error);return response.result; }
    async function poll() {
      if (polling || stopped) return;polling = true;
      try { for (const frame of (await call("poll")).frames ?? []) listeners.get(frame.channelId)?.forEach(listener => listener(frame)); }
      catch (error) { globalThis.__planeNativeTransportError = error.message; }
      finally { polling = false;if (!stopped) setTimeout(poll, 16); }
    }
    globalThis.__planeNativeBridge = { stop() { stopped = true; } };
    globalThis.icax = {
      async getApplicationChannelId() { return (await call("application-channel")).channelId; },
      async registerProductChannel(productId) { return (await call("register-product", { productId })).channelId; },
      async registerSceneChannel(projectId, sceneId) { return (await call("register-scene", { projectId, sceneId })).channelId; },
      subscribeSDOFrames(channel, listener) { if (!listeners.has(channel)) listeners.set(channel, new Set());listeners.get(channel).add(listener);setTimeout(poll, 0);return () => listeners.get(channel)?.delete(listener); },
      async postSDOFrame(frame) { await call("post", { frame: { ...frame, callId: String(frame.callId), methodCode: String(frame.methodCode) } }); },
      async requestResource(request) { const result = await call("resource", { request });return { status: result.status, headers: result.headers, body: Uint8Array.from(atob(result.bodyBase64 ?? ""), character => character.charCodeAt(0)).buffer }; },
      async openFileDialog() { return null; },async saveFileDialog() { return savedProject; },
    };
  }, { savedProject });
  await page.goto(origin + "/iCAX-UI/SDK/AppShell/index.html");
  await page.waitForFunction(() => ["ready", "error"].includes(globalThis.__icaxAppShell?.getState().startupPhase), null, { timeout: 90000 });
  const startup = await page.evaluate(() => __icaxAppShell.getState());assert.equal(startup.startupPhase, "ready", startup.error || "Native startup failed");
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "多段线切向夹角原生验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const scene = getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam;return scene?.capabilities?.drawingEdits?.includes("close"); });
  await exercisePolylineAngles();
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  report.pass = true;await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  report.error = error.stack ?? String(error);if (page) { try { report.failureState = await drawingState();await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests });
  writeFileSync(resolve(output, "polyline-angle-native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
