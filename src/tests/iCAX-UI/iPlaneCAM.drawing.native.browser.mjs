import { ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
// Genuine SDK pointer interaction on an initially empty native iPlaneCAM
// scene. The HTTP and JSON-lines bridge are test transport only; all drawing
// entities, geometry, history and resources belong to the native runtime.
import assert from "node:assert/strict";
import { clickPlaneCommand } from "./iPlaneCAM.browserCommands.mjs";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { createServer } from "node:http";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, dirname, sep, extname, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const source = resolve(fileURLToPath(new URL("../../", import.meta.url))), root = resolve(source, "..");
const assetRoot = resolve(process.env.ICAX_PLANE_ASSET_ROOT || source);
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-drawing-browser"));
mkdirSync(output, { recursive: true });
const runId = String(Date.now());
const executable = resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE || resolve(root, "output/tests/iplane-cam-native/runtime-final/iPlaneCAMNativeBridge.exe"));
assert.ok(existsSync(executable), "Set ICAX_PLANE_NATIVE_BRIDGE to the built native bridge");
const child = spawn(executable, ["--data-dir", resolve(output, `native-user-data-${runId}`)], { cwd: dirname(executable), windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
const pending = new Map(), requests = [], stderr = [], failures = [], checks = [], assets = {};
let sequence = 0, processClosed = false;
child.stderr.on("data", bytes => stderr.push(bytes.toString()));
child.on("exit", code => { processClosed = true;for (const entry of pending.values()) { clearTimeout(entry.timer);entry.reject(new Error("Native runtime exited: " + code)); }pending.clear(); });
createInterface({ input: child.stdout }).on("line", line => {
  let response;try { response = JSON.parse(line); } catch { stderr.push("stdout: " + line);return; }
  const entry = pending.get(String(response.id));if (!entry) return;
  clearTimeout(entry.timer);pending.delete(String(response.id));
  response.ok ? entry.resolve(response.result) : entry.reject(new Error(response.error || "Native bridge failed"));
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
const report = { pass: false, scope: "Actual AppShell, SDK viewport pointer events, native PlaneDrawing Scene SDO/Repository/View and framework undo/redo. Starts without numeric geometry or imported entities. Desktop CEF is validated separately.", executable, assetRoot };
report.binaries = Object.fromEntries([basename(executable), "iPlaneCAM.dll", "PlaneNestingServices.dll", "iPlaneNesting.dll", "Product.dll", "Project.dll", "ProjectFile.dll", "Database.dll", "Resources.dll"].map(name => {
  const path = resolve(dirname(executable), name);return [name, existsSync(path) ? createHash("sha256").update(readFileSync(path)).digest("hex") : "missing"];
}));
const passed = name => { checks.push(name);console.log("PASS " + name); };
// Await the predicate explicitly: this installed Playwright build treats an
// async waitForFunction predicate's Promise as truthy before it resolves.
async function waitForAsync(predicate, argument, { timeout = 30000 } = {}) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const value = await page.evaluate(predicate, argument);if (value) return value;
    await page.waitForTimeout(25);
  }
  throw new Error("Timed out waiting for native/UI state: " + predicate.toString().slice(0, 240));
}
async function drawingState() {
  return page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    const nativeResponse = await view.sceneProxy.invoke("PlaneScene.ReadState", {}), native = nativeResponse.iplaneCam;
    const preview = [];
    view.viewport.content.traverse(object => {
      if (!object.userData?.iplaneDrawingPreview) return;
      let vertices = 0, lines = 0;
      object.traverse(child => { if (child.isLine) { lines++;vertices += child.geometry?.attributes?.position?.count ?? 0; } });
      preview.push({ visible: object.visible, lines, vertices });
    });
    return { native, nativeRevision: nativeResponse.revision, points: structuredClone(ui.commandPoints ?? []), tool: ui.activeTool, busy: ui.busy, error: ui.operationError ?? "", preview, applied: view.viewport.getAppliedViewState() };
  });
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
async function start(primitive) {
  await clickPlaneCommand(page, `plane.draw-${primitive}`);
  await waitForAsync(async primitive => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
    return ui.activeTool === `plane.draw-${primitive}` && !ui.busy && !ui.commandPoints.length;
  }, primitive);
}
async function screenPoint(x, y) {
  const box = await page.locator(".icax-three-viewport canvas").boundingBox();
  return { x: Math.round(box.x + box.width * x), y: Math.round(box.y + box.height * y) };
}
async function click(point) { await page.mouse.click(point.x, point.y); }
async function hover(point) { await page.mouse.move(point.x, point.y, { steps: 5 }); }
async function assertPreview(count, minimumVertices) {
  await waitForAsync(async minimum => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId);let vertices = 0;
    view.viewport.content.traverse(object => { if (!object.userData?.iplaneDrawingPreview || !object.visible) return;object.traverse(child => { if (child.isLine) vertices += child.geometry?.attributes?.position?.count ?? 0; }); });
    return vertices >= minimum;
  }, minimumVertices);
  const state = await drawingState();assert.equal(state.native.drawings.length, count, "An unfinished pointer preview created a native entity");
  return state.preview;
}
async function waitPrefix(count) {
  await waitForAsync(async count => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.commandPoints.length === count; }, count);
}
try {
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome", args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const pageErrors = [];page.on("pageerror", error => pageErrors.push(error.message));report.pageErrors = pageErrors;
  await page.addInitScript(() => {
    const listeners = new Map();let polling = false, stopped = false, responseHold = null;
    const deliver = frame => listeners.get(frame.channelId)?.forEach(listener => listener(frame));
    async function call(action, parameters = {}) { const response = await fetch("/native-rpc", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action, ...parameters }) }).then(result => result.json());if (!response.ok) throw new Error(response.error);return response.result; }
    async function poll() {
      if (polling || stopped) return;polling = true;
      try { const result = await call("poll");for (const frame of result.frames ?? []) {
        // Only delay delivery of one genuine completed native command. The
        // request is posted normally and every unrelated PDO/View/query frame
        // keeps flowing, so this cannot fabricate product state or success.
        if (responseHold?.callId === String(frame.callId) && Number(frame.kind) === 2) responseHold.frames.push(frame);
        else deliver(frame);
      } }
      catch (error) { globalThis.__planeNativeTransportError = error.message; }
      finally { polling = false;if (!stopped) setTimeout(poll, 16); }
    }
    globalThis.__planeNativeBridge = {
      stop() { stopped = true; },
      holdNextResponse(methodCode) { if (responseHold) throw new Error("A native response is already held");responseHold = { methodCode: String(methodCode), callId: null, frames: [] }; },
      isResponseHeld() { return Boolean(responseHold?.frames.length); },
      releaseResponse() { if (!responseHold?.frames.length) throw new Error("No completed native response to release");const frames = responseHold.frames;responseHold = null;frames.forEach(deliver); },
    };
    globalThis.icax = {
      async getApplicationChannelId() { return (await call("application-channel")).channelId; },
      async registerProductChannel(productId) { return (await call("register-product", { productId })).channelId; },
      async registerSceneChannel(projectId, sceneId) { return (await call("register-scene", { projectId, sceneId })).channelId; },
      subscribeSDOFrames(channel, listener) { if (!listeners.has(channel)) listeners.set(channel, new Set());listeners.get(channel).add(listener);setTimeout(poll, 0);return () => listeners.get(channel)?.delete(listener); },
      async postSDOFrame(frame) { if (responseHold && !responseHold.callId && String(frame.methodCode) === responseHold.methodCode) responseHold.callId = String(frame.callId);await call("post", { frame: { ...frame, callId: String(frame.callId), methodCode: String(frame.methodCode) } }); },
      async requestResource(request) { const result = await call("resource", { request });return { status: result.status, headers: result.headers, body: Uint8Array.from(atob(result.bodyBase64 ?? ""), character => character.charCodeAt(0)).buffer }; },
      async openFileDialog() { return null; },async saveFileDialog() { return null; },
    };
  });
  await page.goto(origin + "/iCAX-UI/SDK/AppShell/index.html");
  await page.waitForFunction(() => ["ready", "error"].includes(globalThis.__icaxAppShell?.getState().startupPhase), null, { timeout: 90000 });
  const startup = await page.evaluate(() => __icaxAppShell.getState());assert.equal(startup.startupPhase, "ready", startup.error || "Native startup failed");
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "空底图鼠标绘图验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await ensurePlaneObjectsExpanded(page);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.capabilities?.enabledMethods?.includes("PlaneDrawing.Create"); });
  assert.equal((await drawingState()).native.drawings.length, 0);
  await clickPlaneCommand(page, "plane.snap-toggle");
  passed("A fresh actual native project starts with an empty drawing and no numerical or imported seed geometry");

  const point = await Promise.all([[.15,.20],[.33,.28],[.42,.18],[.62,.35],[.76,.23],[.85,.23],[.17,.58],[.26,.58],[.17,.48],[.40,.56],[.50,.49],[.58,.64],[.70,.52],[.84,.67]].map(([x,y]) => screenPoint(x,y)));
  await start("line");await click(point[0]);await waitPrefix(1);await hover(point[1]);report.linePreview = await assertPreview(0, 2);
  await page.screenshot({ path: resolve(output, "empty-line-preview.png") });
  await click(point[1]);await waitCount(1);
  passed("Empty-canvas click-click line shows a transient line before one native commit");
  await start("rect");await click(point[2]);await waitPrefix(1);await hover(point[3]);report.rectanglePreview = await assertPreview(1, 5);
  await page.screenshot({ path: resolve(output, "rectangle-preview.png") });await click(point[3]);await waitCount(2);
  passed("Two-corner rectangle previews its full four-sided outline and commits once");
  await start("circle");await click(point[4]);await waitPrefix(1);await hover(point[5]);report.circlePreview = await assertPreview(2, 20);
  await page.screenshot({ path: resolve(output, "circle-preview.png") });await click(point[5]);await waitCount(3);
  passed("Center-radius circle previews a curved outline rather than only a radius and commits once");
  await start("arc");await click(point[6]);await waitPrefix(1);await click(point[7]);await waitPrefix(2);await hover(point[8]);report.arcPreview = await assertPreview(3, 10);
  await page.screenshot({ path: resolve(output, "arc-preview.png") });await click(point[8]);await waitCount(4);
  passed("Three-point center/start/end arc keeps both accepted points transient and previews the actual curved sweep");
  await start("polyline");await click(point[9]);await waitPrefix(1);await click(point[10]);await waitPrefix(2);await click(point[11]);await waitPrefix(3);await hover(point[12]);report.polylinePreview = await assertPreview(4, 4);
  await page.screenshot({ path: resolve(output, "polyline-preview.png") });await page.keyboard.press("Enter");await waitCount(5);
  passed("Polyline preview retains confirmed segments plus the next segment; Enter submits the complete native polyline");

  for (const [index, primitive] of ["line", "rect", "circle"].entries()) {
    await start(primitive);const from = await screenPoint(.18 + index * .23, .78), to = await screenPoint(.31 + index * .23, .89);
    await page.mouse.move(from.x, from.y);await page.mouse.down();await hover(to);
    await assertPreview(5 + index, primitive === "line" ? 2 : primitive === "rect" ? 5 : 20);
    await page.mouse.up();await waitCount(6 + index);await page.waitForTimeout(120);
    assert.equal((await drawingState()).native.drawings.length, 6 + index, "Drag release also submitted an extra click");
  }
  passed("Line, rectangle and circle support pointer-down/drag/release with live previews and exactly one native entity per drag");

  await start("arc");await click(point[12]);await waitPrefix(1);await click(point[13]);await waitPrefix(2);await hover(point[11]);await assertPreview(8, 10);
  await page.keyboard.press("Escape");
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return view.iplaneCamUI.activeTool === "plane.select" && view.iplaneCamUI.commandPoints.length === 0; });
  let state = await drawingState();assert.equal(state.native.drawings.length, 8);assert.ok(state.preview.every(item => !item.visible || !item.vertices));
  await start("circle");const cancelFrom = await screenPoint(.72, .42), cancelTo = await screenPoint(.83, .48);
  await page.mouse.move(cancelFrom.x, cancelFrom.y);await page.mouse.down();await hover(cancelTo);await assertPreview(8, 20);
  await page.keyboard.press("Escape");await page.mouse.up();await page.waitForTimeout(120);
  state = await drawingState();assert.equal(state.native.drawings.length, 8);assert.equal(state.tool, "plane.select");assert.equal(state.points.length, 0);assert.ok(state.preview.every(item => !item.visible || !item.vertices));
  await start("line");await click(point[12]);await waitPrefix(1);await hover(point[13]);await assertPreview(8, 2);await start("circle");
  state = await drawingState();assert.equal(state.native.drawings.length, 8);assert.equal(state.points.length, 0);assert.ok(state.preview.every(item => !item.visible || !item.vertices));
  passed("Escape during a three-point arc or active drag and switching tools clear previews without changing native entities or leaving accepted points behind");

  const value = page.locator("[data-iplane-draw-value]");
  await value.fill("0,0");await value.press("Enter");await waitPrefix(1);
  await value.fill("10");await value.press("Enter");await waitCount(9);
  state = await drawingState();report.createdNames = state.native.drawings.map(row => row.name);
  assert.ok(report.createdNames.some(name => /直线/.test(name)));assert.ok(report.createdNames.some(name => /矩形/.test(name)));assert.ok(report.createdNames.some(name => /圆弧/.test(name)));assert.ok(report.createdNames.some(name => /多段线/.test(name)));
  passed("The right drawing panel shares coordinate/radius input and created entities have understandable shape names");
  await page.keyboard.press("Escape");
  await page.locator('.title-bar [data-action="undo-project"]').click();await waitCount(8);
  await page.locator('.title-bar [data-action="redo-project"]').click();await waitCount(9);
  report.finalState = await drawingState();
  const geometry = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), state = await view.sceneProxy.invoke("PlaneScene.ReadState", {});return Promise.all(state.iplaneCam.drawings.map(async row => ({ id: row.id, name: row.name, geometry: await view.sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id: row.id }) }))); });
  report.exactGeometry = geometry;assert.ok(geometry.every(row => row.geometry.curves?.length));
  const curves = geometry.flatMap(row => row.geometry.curves);
  const curveTypes = curves.reduce((counts, curve) => { counts[curve.type] = (counts[curve.type] ?? 0) + 1;return counts; }, {});
  assert.deepEqual(curveTypes, { line: 2, polyline: 3, circle: 3, arc: 1 });
  assert.equal(curves.filter(curve => curve.type === "polyline" && curve.closed).length, 2, "Both rectangle gestures must create closed four-sided native curves");
  assert.equal(curves.filter(curve => curve.type === "polyline" && !curve.closed).length, 1, "Enter must preserve an open native polyline");
  assert.ok(curves.some(curve => curve.type === "circle" && curve.radius === 10 && curve.center[0] === 0 && curve.center[1] === 0), "The right-panel radius must reach native exact geometry");
  assert.ok(report.finalState.native.drawings.every(row => row.entityIds.every(id => report.finalState.applied.entityIds.includes(String(id)))));
  await page.screenshot({ path: resolve(output, "drawing-final-native.png") });
  passed("Completed drawings expose actual native curves and render resources; framework undo and redo remove and restore the last shape");

  // Changing only the user's selection while a completed native response is
  // still in transport must not retain the already committed point prefix or
  // replace their choice with the new graphic when delivery resumes.
  async function holdNextCreate() {
    await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneDrawing.Create"))); });
  }
  const oldSelection = report.finalState.native.drawings[0].id;
  const readSelection = () => page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return { ids: [...ui.selectedIdsByKind.drawing], epoch: ui.panelEpoch }; });
  await start("line");const delayedStart = await screenPoint(.23, .38), delayedEnd = await screenPoint(.35, .41);
  await click(delayedStart);await waitPrefix(1);await holdNextCreate();await click(delayedEnd);
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());
  state = await drawingState();assert.equal(state.native.drawings.length, 10);assert.equal(state.busy, true);
  const beforeSelection = await readSelection();await page.locator(`[data-iplane-object-id="${oldSelection}"]`).click();
  const duringSelection = await readSelection();assert.deepEqual(duringSelection.ids, [oldSelection]);assert.ok(duringSelection.epoch > beforeSelection.epoch);
  await page.evaluate(() => __planeNativeBridge.releaseResponse());await waitCount(10);
  state = await drawingState();assert.equal(state.points.length, 0, "A changed property panel prevented committed pointer points from clearing");assert.equal(state.tool, "plane.draw-line");assert.deepEqual((await readSelection()).ids, [oldSelection], "A delayed creation stole the user's newer selection");
  const beforeNextIds = new Set(state.native.drawings.map(row => row.id));
  const nextStart = await screenPoint(.46, .41), nextEnd = await screenPoint(.58, .45);
  const nextWorld = await page.evaluate(points => import("/apps/_shared/workbench/state/projectViewStore.mjs").then(({ getProjectView }) => { const view = getProjectView(__icaxAppShell.getState().activeProjectId);return points.map(point => view.viewport.pointOnWorkPlane(point.x, point.y, [0, 0, 1]).slice(0, 2)); }), [nextStart, nextEnd]);
  await click(nextStart);await waitPrefix(1);assert.equal((await drawingState()).native.drawings.length, 10, "The next first point reused a previously committed line prefix");
  await click(nextEnd);await waitCount(11);state = await drawingState();
  const nextId = state.native.drawings.find(row => !beforeNextIds.has(row.id)).id;
  const nextGeometry = await page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }); }, nextId);
  report.delayedPointerCompletion = { beforeSelection, duringSelection, afterSelection: await readSelection(), nextWorld, nextGeometry };
  assert.equal(nextGeometry.curves[0].type, "line");assert.equal(nextGeometry.curves[0].points.length, 2);
  for (let i = 0; i < 2; i++) for (let axis = 0; axis < 2; axis++) assert.ok(Math.abs(nextGeometry.curves[0].points[i][axis] - nextWorld[i][axis]) < 1e-8, "The next native line did not use its newly entered coordinates");
  passed("A delayed native pointer completion clears its committed prefix while preserving a newer left-list selection; the next line starts from two fresh points");

  await start("line");await click(delayedStart);await waitPrefix(1);await holdNextCreate();
  const numeric = page.locator("[data-iplane-command-input]");await numeric.fill("直线 0,0 100,0");await numeric.press("Enter");
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());assert.equal((await drawingState()).native.drawings.length, 12);
  await page.locator(`[data-iplane-object-id="${oldSelection}"]`).click();await page.evaluate(() => __planeNativeBridge.releaseResponse());await waitCount(12);
  state = await drawingState();assert.equal(state.points.length, 0, "A completed numerical command retained a pointer prefix");assert.deepEqual((await readSelection()).ids, [oldSelection], "A delayed numerical creation stole the user's newer selection");
  report.afterDelayedCompletion = state;await page.screenshot({ path: resolve(output, "drawing-delayed-completion-native.png") });
  passed("A full numerical drawing command clears any unfinished pointer prefix and preserves selection changes made before its genuine native response arrives");

  await start("arc");
  await value.fill("0,0");await value.press("Enter");await waitPrefix(1);
  await value.fill("10,0");await value.press("Enter");await waitPrefix(2);
  await clickPlaneCommand(page, "plane.ortho-toggle");
  await hover(await screenPoint(.90, .50));await assertPreview(12, 20);
  report.sameRayArcPreview = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const preview = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneDrawingPreview, positions = preview.outline.geometry.attributes.position;return { vertices: positions.count, first: [positions.getX(0), positions.getY(0)], last: [positions.getX(positions.count - 1), positions.getY(positions.count - 1)] }; });
  assert.ok(Math.hypot(...report.sameRayArcPreview.first.map((number, axis) => number - report.sameRayArcPreview.last[axis])) < 1e-6, "Same-direction arc rays must preview a closed full sweep");
  const beforeArcIds = new Set((await drawingState()).native.drawings.map(row => row.id));
  await value.fill("20,0");await value.press("Enter");await waitCount(13);
  const fullArcId = (await drawingState()).native.drawings.find(row => !beforeArcIds.has(row.id)).id;
  report.sameRayArcGeometry = await page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }); }, fullArcId);
  const fullArc = report.sameRayArcGeometry.curves[0];assert.equal(fullArc.type, "arc");assert.equal(fullArc.radius, 10);assert.equal(fullArc.startAngleDeg, 0);assert.equal(fullArc.endAngleDeg, 0);
  assert.equal(((fullArc.endAngleDeg - fullArc.startAngleDeg) % 360 + 360) % 360 || 360, 360);
  await clickPlaneCommand(page, "plane.ortho-toggle");
  passed("Same-direction arc start/end rays preview a complete circle and preserve the native 360-degree arc sweep");

  report.cancelledNativeFailures = [];
  for (const action of ["escape", "nesting-tab"]) {
    await start("rect");await value.fill("0,0");await value.press("Enter");await waitPrefix(1);await holdNextCreate();
    const errorsBefore = await page.locator(".log-row.error").count();
    await value.fill("10,0");await value.press("Enter");await waitForAsync(() => __planeNativeBridge.isResponseHeld());
    assert.equal((await drawingState()).native.drawings.length, 13, "An invalid rectangle unexpectedly created geometry");
    if (action === "escape") await page.keyboard.press("Escape");
    else await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
    // A new draft with exactly the old request text belongs to the later
    // gesture epoch, and the old completion must leave it untouched.
    await numeric.fill("10,0");await page.evaluate(() => __planeNativeBridge.releaseResponse());
    await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.busy; });
    state = await drawingState();assert.equal(state.native.drawings.length, 13);assert.equal(state.error, "", "An obsolete drawing failure polluted the later workspace");assert.equal(state.tool, "plane.select");assert.equal(state.points.length, 0);assert.equal(await numeric.inputValue(), "10,0", "The cancelled command cleared a newer identical input draft");
    const errors = await page.locator(".log-row.error").allTextContents();assert.ok(errors.length > errorsBefore);assert.match(errors.at(-1), /PLANE_INVALID_RECTANGLE/);
    report.cancelledNativeFailures.push({ action, error: state.error, tool: state.tool, newerDraft: await numeric.inputValue(), diagnostic: errors.at(-1) });
    if (action === "nesting-tab") await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();
  }
  passed("Late genuine invalid-rectangle errors after Escape or leaving drawing remain in logs without reopening errors or clearing a newer identical draft");

  await start("line");await numeric.fill("直线 123.456,78.901 140.123,79.456");await numeric.press("Enter");await waitCount(14);
  const snapEndpoint = [123.456, 78.901];
  const waitForEndpoint = present => waitForAsync(async ({ point, present }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI, found = ui.snapCandidates.some(candidate => candidate.point?.length >= 2 && candidate.point.slice(0, 2).every((value, axis) => Math.abs(value - point[axis]) < 1e-8));return found === present; }, { point: snapEndpoint, present });
  await waitForEndpoint(true);
  report.snapAfterCreate = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return { tool: ui.activeTool, signature: ui.snapSignature, candidates: structuredClone(ui.snapCandidates) }; });
  await page.locator('.title-bar [data-action="undo-project"]').click();await waitCount(13);await waitForEndpoint(false);
  await page.locator('.title-bar [data-action="redo-project"]').click();await waitCount(14);await waitForEndpoint(true);
  assert.equal((await drawingState()).tool, "plane.draw-line");report.finalAfterAllChecks = await drawingState();
  await page.screenshot({ path: resolve(output, "drawing-final-all-native.png") });
  passed("Within the same active drawing tool, exact snap candidates include new native endpoints, remove undone endpoints and recover them after redo");
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  for (const asset of ["/iCAX-UI/SDK/AppShell/index.html", "/iCAX-UI/SceneProxy/SceneProxy.mjs", "/apps/_shared/workbench/createWorkbench.mjs", "/apps/iplane-cam/webpage/drawingCommands.mjs"]) assert.ok(assets[asset], "Actual product asset was not loaded: " + asset);
  report.pass = true;await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  report.error = error.stack ?? String(error);
  if (page) { try { report.failureState = await drawingState();await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }
  throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests });
  writeFileSync(resolve(output, "drawing-native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
