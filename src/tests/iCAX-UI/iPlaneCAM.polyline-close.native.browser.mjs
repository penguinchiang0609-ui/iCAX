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
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-polyline-close"));
mkdirSync(output, { recursive: true });
const runId = String(Date.now()), savedProject = resolve(output, `polyline-close-${runId}.ipcam`);
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
const report = { pass: false, scope: "Actual AppShell and SDK pointer/keyboard UI with native PlaneDrawing ECS, exact geometry, render resources, undo/redo and project save/close/reopen. The test supplies file choices; desktop CEF and OS dialogs are outside this scope.", executable, assetRoot, savedProject };
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
  if ((await drawingState()).tool === "plane.draw-polyline") await assertDrawingMenu(menu, { polyline: true });
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
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "折线闭合原生验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const scene = getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam;return scene?.capabilities?.drawingEdits?.includes("close"); });
  await ensurePlaneObjectsExpanded(page);
  assert.equal((await drawingState()).native.drawings.length, 0);
  // Self-return closure is a drawing gesture, so it must also work when object
  // snapping is disabled rather than depending on a previously saved entity.
  await clickPlaneCommand(page, "plane.snap-toggle");
  passed("The real native product exposes close editing on an initially empty drawing");

  const tri = await Promise.all([[.20,.24],[.35,.30],[.26,.43]].map(([x, y]) => screenPoint(x, y)));
  await start();const emptyMenu = await openMenu(tri[0]);
  assert.equal(await emptyMenu.menu.locator('[data-iplane-action="close-drawing"]').isEnabled(), false);
  assert.equal(await emptyMenu.menu.locator('[data-iplane-action="finish-drawing"]').isEnabled(), false);
  assert.equal(await emptyMenu.menu.locator('[data-iplane-action="cancel-drawing"]').isEnabled(), true);
  await emptyMenu.menu.locator('[data-iplane-action="cancel-drawing"]').press("Enter");await assertSelectionTool();assert.equal((await drawingState()).native.drawings.length, 0);
  passed("An empty drawing command offers disabled Close/Finish and an enabled Cancel; Enter on its menu button cancels without creating an entity");
  let before = (await drawingState()).native.drawings;
  await triangle(tri);await page.keyboard.press("c");const keyboardRow = await newRow(before, 1);
  report.keyboardClosure = await assertPolyline(keyboardRow.id, true, 3);
  assert.equal(keyboardRow.isPolyline, true);assert.equal(keyboardRow.closed, true);assert.equal(keyboardRow.canClosePolyline, false);
  await assertSelectionTool();
  passed("Canvas C closes three entered vertices as one native closed polyline without duplicating its first vertex");

  before = (await drawingState()).native.drawings;
  const tri2 = await Promise.all([[.48,.20],[.64,.24],[.57,.39]].map(([x, y]) => screenPoint(x, y)));
  await triangle(tri2);
  assert.equal(await page.locator('.cam-info-pane [data-iplane-action="close-drawing"],.cam-info-pane [data-iplane-action="finish-drawing"],.cam-info-pane [data-iplane-action="cancel-drawing"]').count(), 0, "Drawing end actions must not appear in the parameter sidebar");
  const closeMenu = await openMenu(tri2[0]);report.closeMenu = { bounds: closeMenu.bounds, point: closeMenu.point, viewport: closeMenu.viewport };
  await assertDrawingMenu(closeMenu.menu, { polyline: true });
  assert.equal((await drawingState()).points.length, 3, "Right-clicking the accepted start point appended a vertex");
  assert.equal(await closeMenu.menu.locator('[data-iplane-action="close-drawing"]').isEnabled(), true);
  await page.screenshot({ path: resolve(output, "cursor-close-menu.png") });
  await menuAction("close-drawing");const buttonRow = await newRow(before, 2);
  report.buttonClosure = await assertPolyline(buttonRow.id, true, 3);
  await assertSelectionTool();assert.equal(await page.locator("[data-iplane-context-menu]").count(), 0);
  passed("The cursor right-click menu offers Close/Cancel/Finish, stays near the pointer and closes without a sidebar action or extra vertex");

  before = (await drawingState()).native.drawings;
  const tri3 = await Promise.all([[.73,.20],[.86,.30],[.71,.43]].map(([x, y]) => screenPoint(x, y)));
  await triangle(tri3);await page.mouse.move(tri3[0].x + 2, tri3[0].y + 1, { steps: 5 });
  report.returnPreview = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), preview = view.iplaneDrawingPreview, p = preview?.outline.geometry.attributes.position;return { nativeCount: view.scene.iplaneCam.drawings.length, vertices: p?.count ?? 0, first: p ? [p.getX(0), p.getY(0)] : [], last: p ? [p.getX(p.count - 1), p.getY(p.count - 1)] : [] }; });
  assert.equal(report.returnPreview.nativeCount, 2);assert.ok(report.returnPreview.vertices >= 4);
  assert.ok(Math.hypot(...report.returnPreview.first.map((value, axis) => value - report.returnPreview.last[axis])) < 1e-4, "Returning to the first point did not preview a closed outline");
  await page.screenshot({ path: resolve(output, "return-to-start-preview.png") });
  await click({ x: tri3[0].x + 2, y: tri3[0].y + 1 });const returnRow = await newRow(before, 3);
  report.returnClosure = await assertPolyline(returnRow.id, true, 3);
  await assertSelectionTool();
  passed("Returning within the first-point tolerance previews and commits a closed loop even with object snapping off");

  before = (await drawingState()).native.drawings;
  const tri4 = await Promise.all([[.20,.58],[.36,.65],[.24,.80]].map(([x, y]) => screenPoint(x, y)));
  await triangle(tri4);await page.keyboard.press("Enter");const openRow = await newRow(before, 4);
  report.openBeforeEdit = await assertPolyline(openRow.id, false, 3);
  assert.equal(openRow.canClosePolyline, true);assert.equal(openRow.closed, false);
  await assertSelectionTool();
  passed("Enter still finishes an open polyline and the native query advertises whether it can be closed later");

  await page.locator(`[data-iplane-object-id="${openRow.id}"]`).click();
  assert.equal(await page.locator('.cam-info-pane [data-iplane-action="close-polyline"]').count(), 0, "Finished-polyline closure must be a canvas context action");
  const existingMenu = await openMenu(tri4[0]);assert.deepEqual((await existingMenu.menu.locator("button").allTextContents()).map(text => text.trim()), ["闭合轮廓"]);
  assert.equal((await drawingState()).points.length, 0, "Right-clicking an existing graphic created drawing input");
  await menuAction("close-polyline");let editedRow = await waitClosed(openRow.id, true);
  assert.equal((await drawingState()).native.drawings.length, 4);assert.ok(BigInt(editedRow.geometryVersion) > BigInt(openRow.geometryVersion));
  report.closedAfterEdit = await assertPolyline(openRow.id, true, 3);
  await page.locator('.title-bar [data-action="undo-project"]').click();const undoneRow = await waitClosed(openRow.id, false);
  assert.equal(undoneRow.canClosePolyline, true);await assertPolyline(openRow.id, false, 3);
  await page.locator('.title-bar [data-action="redo-project"]').click();editedRow = await waitClosed(openRow.id, true);
  assert.equal(editedRow.canClosePolyline, false);await assertPolyline(openRow.id, true, 3);
  report.editHistory = { original: openRow, afterClose: editedRow, afterUndo: undoneRow };
  passed("The selected open graphic closes through its canvas context menu with the same ECS identity and native undo/redo of the closing edge");

  before = (await drawingState()).native.drawings;
  const tri5 = await Promise.all([[.47,.56],[.62,.62],[.53,.79]].map(([x, y]) => screenPoint(x, y)));
  await triangle(tri5);await page.keyboard.press("Enter");const selectedRow = await newRow(before, 5);
  await page.keyboard.press("Escape");await page.locator(`[data-iplane-object-id="${selectedRow.id}"]`).click();
  const name = page.locator('[data-iplane-field="name"]');await name.fill("C");await name.press("c");
  assert.equal((await geometry(selectedRow.id)).curves[0].closed, false, "Typing a letter in an object field invoked canvas closure");
  await page.locator('[data-iplane-action="reset-draft"]').click();
  await page.locator(`[data-iplane-object-id="${selectedRow.id}"]`).press("c");await waitClosed(selectedRow.id, true);
  report.selectionShortcut = await assertPolyline(selectedRow.id, true, 3);
  passed("C closes the selected finished polyline while normal typing inside a property field leaves geometry unchanged");

  before = (await drawingState()).native.drawings;
  const tri6 = await Promise.all([[.72,.56],[.87,.61],[.79,.79]].map(([x, y]) => screenPoint(x, y)));
  await triangle(tri6);const value = page.locator("[data-iplane-draw-value]");await value.fill("C");
  assert.equal((await drawingState()).native.drawings.length, 5);assert.equal((await drawingState()).points.length, 3);
  await value.press("Enter");const inputRow = await newRow(before, 6);report.inputClosure = await assertPolyline(inputRow.id, true, 3);
  await assertSelectionTool();
  passed("An explicit C plus Enter in the drawing input closes the path; entering the letter alone stays a draft");

  await start();await click(tri[0]);await waitPrefix(1);await click(tri[1]);await waitPrefix(2);
  await openMenu(tri[1]);await page.keyboard.press("Escape");await waitPrefix(2);
  assert.equal(await page.locator("[data-iplane-context-menu]").count(), 0);assert.equal((await drawingState()).tool, "plane.draw-polyline");
  await page.keyboard.press("Escape");await assertSelectionTool();assert.equal((await drawingState()).native.drawings.length, 6);
  passed("Escape dismisses the cursor menu first while retaining entered vertices; a second Escape cancels only the unfinished command");

  await start();await click(tri[0]);await waitPrefix(1);await click(tri[1]);await waitPrefix(2);
  const tooFewMenu = await openMenu(tri[1]);assert.equal(await tooFewMenu.menu.locator('[data-iplane-action="close-drawing"]').isEnabled(), false);
  const outside = await screenPoint(.93, .93);
  assert.ok(outside.x < tooFewMenu.bounds.x || outside.x > tooFewMenu.bounds.x + tooFewMenu.bounds.width || outside.y < tooFewMenu.bounds.y || outside.y > tooFewMenu.bounds.y + tooFewMenu.bounds.height);
  await click(outside);
  assert.equal(await page.locator("[data-iplane-context-menu]").count(), 0);await waitPrefix(2);
  assert.equal((await drawingState()).native.drawings.length, 6, "An outside click used to dismiss the menu created geometry");
  passed("Clicking outside the context menu on the canvas dismisses it without appending a drawing vertex");
  await page.keyboard.press("c");assert.equal((await drawingState()).native.drawings.length, 6);
  await value.fill("C");await value.press("Enter");await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return Boolean(getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.operationError); });
  const rejected = await drawingState();assert.equal(rejected.native.drawings.length, 6);assert.equal(rejected.points.length, 2);report.insufficientPoints = { count: rejected.native.drawings.length, points: rejected.points, error: rejected.error };
  const edgePoint = await screenPoint(.995, .995), cancelMenu = await openMenu(edgePoint);
  report.edgeMenu = { bounds: cancelMenu.bounds, point: cancelMenu.point, viewport: cancelMenu.viewport, canvas: cancelMenu.canvas };
  assert.ok(cancelMenu.bounds.x < edgePoint.x && cancelMenu.bounds.y < edgePoint.y, "A menu at the bottom-right canvas edge was not clamped back into the canvas");
  await menuAction("cancel-drawing");await assertSelectionTool();assert.equal((await drawingState()).native.drawings.length, 6);
  passed("Closure with fewer than three vertices is clearly rejected without creating an entity or discarding accepted points");

  before = (await drawingState()).native.drawings;
  await triangle(tri4);const finishMenu = await openMenu(tri4[1]);assert.equal((await drawingState()).points.length, 3);
  await menuAction("finish-drawing");const finishedRow = await newRow(before, 7);
  report.contextFinishedOpen = await assertPolyline(finishedRow.id, false, 3);await assertSelectionTool();
  await page.locator(`[data-iplane-object-id="${finishedRow.id}"]`).press("c");await waitClosed(finishedRow.id, true);
  passed("Right-click Finish ends an open polyline and returns to selection; right-click Cancel discards only the unfinished command");

  report.beforeSave = await drawingState();await page.locator('.title-bar [data-command-id="app.save"]').click();
  const savedDeadline = Date.now() + 30000;while (!existsSync(savedProject) && Date.now() < savedDeadline) await page.waitForTimeout(25);
  assert.ok(existsSync(savedProject) && statSync(savedProject).size > 100, "The actual framework did not write a project file");
  await waitForAsync(() => !document.querySelector('.title-bar [data-command-id="app.save"]')?.disabled);
  const projectId = await page.evaluate(() => __icaxAppShell.getState().activeProjectId);
  const closing = page.evaluate(projectId => __icaxAppShell.closeProject(projectId), projectId);
  await waitForAsync(projectId => Boolean(document.querySelector("[data-project-close-dialog]")) || __icaxAppShell.getState().activeProjectId !== projectId, projectId);
  if (await page.locator("[data-project-close-dialog]").count()) await page.locator('[data-project-close-choice="discard"]').click();
  await closing;await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), savedProject);
  await waitCount(7);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), rows = view.scene?.iplaneCam?.drawings ?? [], applied = new Set(view.viewport?.getAppliedViewState().entityIds ?? []);return rows.length === 7 && rows.every(row => row.entityIds.every(id => applied.has(String(id)))); });
  report.reopened = await drawingState();
  assert.deepEqual(report.reopened.native.drawings.map(row => row.id).sort(), report.beforeSave.native.drawings.map(row => row.id).sort());
  for (const row of report.reopened.native.drawings) { assert.equal(row.closed, true);assert.equal(row.canClosePolyline, false);await assertPolyline(row.id, true, 3); }
  const resources = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const { loadRenderResource } = await import("/iCAX-UI/SDK/Viewport/renderResource.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), native = (await view.sceneProxy.invoke("PlaneScene.ReadState", {})).iplaneCam;
    return Promise.all(native.drawings.map(async row => { const resource = await loadRenderResource(view.sceneProxy.resources, row.previewRef);return { id: row.id, kind: resource?.data?.kind, points: Array.from(resource?.data?.points ?? []) }; }));
  });
  for (const resource of resources) {
    assert.equal(resource.kind, "polyline");const exact = (await geometry(resource.id)).curves[0], first = exact.points[0], last = exact.points.at(-1);
    let closingEdge = false;for (let i = 0; i + 5 < resource.points.length; i += 6) {
      const a = resource.points.slice(i, i + 2), b = resource.points.slice(i + 3, i + 5);
      if (Math.hypot(a[0] - last[0], a[1] - last[1]) < 1e-3 && Math.hypot(b[0] - first[0], b[1] - first[1]) < 1e-3) closingEdge = true;
    }
    assert.equal(closingEdge, true, "The saved native render resource omitted the closing edge");
  }
  report.restoredResources = resources;report.projectFile = { path: savedProject, bytes: statSync(savedProject).size, sha256: createHash("sha256").update(readFileSync(savedProject)).digest("hex") };
  await page.screenshot({ path: resolve(output, "closed-polylines-reopened.png") });
  passed("Actual project save/close/reopen preserves closed ECS geometry and each restored display resource contains the last-to-first edge");
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  report.pass = true;await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  report.error = error.stack ?? String(error);if (page) { try { report.failureState = await drawingState();await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests });
  writeFileSync(resolve(output, "polyline-close-native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
