import { ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
// Actual AppShell / SceneProxy / View and the product's native runtime. The
// local HTTP server and JSON-lines process bridge below are test transport;
// they supply no product state, CAD geometry, solver result or saved project.
import assert from "node:assert/strict";
import { clickPlaneCommand } from "./iPlaneCAM.browserCommands.mjs";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { createServer } from "node:http";
import { existsSync, mkdirSync, readFileSync, writeFileSync, statSync, unlinkSync } from "node:fs";
import { resolve, dirname, sep, extname, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const source = resolve(fileURLToPath(new URL("../../", import.meta.url)));
const assetRoot = resolve(process.env.ICAX_PLANE_ASSET_ROOT || source);
const root = resolve(source, "..");
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-native-browser"));
mkdirSync(output, { recursive: true });
const runId = String(Date.now());
const executable = resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE || resolve(root, "output/tests/iplane-cam-native/runtime-final/iPlaneCAMNativeBridge.exe"));
const partDxf = resolve(process.env.ICAX_PLANE_TEST_DXF || resolve(root, "TestData/Plane/steel_frame_7_parts_2_nests_and_dstv_tests/vanthanbk/plate_dxf/Gusset-04_t6_x1.dxf"));
const stockDxf = process.env.ICAX_PLANE_STOCK_DXF ? resolve(process.env.ICAX_PLANE_STOCK_DXF) : "";
const savedProject = resolve(output, `native-browser-${runId}.ipcam`);
assert.ok(existsSync(executable), "Set ICAX_PLANE_NATIVE_BRIDGE to the built test bridge executable");
assert.ok(existsSync(partDxf), "Set ICAX_PLANE_TEST_DXF to a real closed material DXF");
const inputDirectory = resolve(output, `inputs-${runId}`);mkdirSync(inputDirectory, { recursive: true });
const importedPartPath = resolve(inputDirectory, "part-" + basename(partDxf));
writeFileSync(importedPartPath, readFileSync(partDxf));
const unitlessPath = resolve(inputDirectory, "unitless-" + basename(partDxf));
const originalDxf = readFileSync(partDxf, "utf8"), unitlessDxf = originalDxf.replace(/(\$INSUNITS\s*\r?\n\s*70\s*\r?\n)\s*[+-]?\d+/i, (_match, prefix) => prefix + "0");
assert.notEqual(unitlessDxf, originalDxf, "The external test DXF must declare units so the unitless-header case can be prepared without altering geometry");
writeFileSync(unitlessPath, unitlessDxf);
const importedStockPath = stockDxf ? resolve(inputDirectory, "stock-" + basename(stockDxf)) : "";
if (stockDxf) writeFileSync(importedStockPath, readFileSync(stockDxf));

const child = spawn(executable, ["--data-dir", resolve(output, `native-user-data-${runId}`)], { cwd: dirname(executable), windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
const pending = new Map(), transcript = [], stderr = [], failures = [], checks = [];
const assets = {};
let sequence = 0, processClosed = false;
child.stderr.on("data", bytes => stderr.push(bytes.toString()));
child.on("exit", code => { processClosed = true;for (const entry of pending.values()) { clearTimeout(entry.timer);entry.reject(new Error("Native runtime exited: " + code)); }pending.clear(); });
createInterface({ input: child.stdout }).on("line", line => {
  let response;
  try { response = JSON.parse(line); } catch { stderr.push("stdout: " + line);return; }
  const entry = pending.get(String(response.id));if (!entry) return;
  clearTimeout(entry.timer);pending.delete(String(response.id));
  response.ok ? entry.resolve(response.result) : entry.reject(new Error(response.error || "Native bridge failed"));
});
function rpc(request) {
  if (processClosed) return Promise.reject(new Error("Native bridge is closed"));
  const id = String(++sequence);
  if (request.action !== "poll") transcript.push({ id, ...request });
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
const report = { pass: false, scope: "Actual SDK/AppShell, native ApplicationRuntime, iPlaneCAM Scene SDO, Repository/Resources/View and iPlaneNesting service; file dialog selection is supplied by the test. No desktop CEF or operating-system dialog interaction is asserted.", executable, assetRoot, partDxf, stockDxf, savedProject };
report.binaries = Object.fromEntries([basename(executable), "iPlaneCAM.dll", "PlaneNestingServices.dll", "iPlaneNesting.dll", "Product.dll", "Project.dll", "ProjectFile.dll", "Database.dll", "Resources.dll"].map(name => {
  const path = resolve(dirname(executable), name);return [name, existsSync(path) ? createHash("sha256").update(readFileSync(path)).digest("hex") : "missing"];
}));
const passed = name => { checks.push(name);console.log("PASS " + name); };
// This installed Playwright build treats an async predicate's Promise as a
// truthy waitForFunction result, including Promise.resolve(false). Await the
// evaluated predicate explicitly so native operations cannot pass unfinished.
async function waitForAsync(page, predicate, argument, { timeout = 30000 } = {}) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const value = await page.evaluate(predicate, argument);
    if (value) return value;
    await page.waitForTimeout(25);
  }
  throw new Error("Timed out waiting for the actual native/UI state: " + predicate.toString().slice(0, 240));
}
try {
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome", args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 }, acceptDownloads: true });
  const pageErrors = [];page.on("pageerror", error => pageErrors.push(error.message));report.pageErrors = pageErrors;
  await page.addInitScript(({ savedProject }) => {
    const listeners = new Map(), files = [];
    let polling = false, stopped = false;
    async function call(action, parameters = {}) {
      const response = await fetch("/native-rpc", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action, ...parameters }) }).then(result => result.json());
      if (!response.ok) throw new Error(response.error);return response.result;
    }
    async function poll() {
      if (polling || stopped) return;polling = true;
      try { const result = await call("poll");for (const frame of result.frames ?? []) listeners.get(frame.channelId)?.forEach(listener => listener(frame)); }
      catch (error) { globalThis.__planeNativeTransportError = error.message; }
      finally { polling = false;if (!stopped) setTimeout(poll, 16); }
    }
    globalThis.__planeNativeFileQueue = files;
    globalThis.__planeNativeBridge = { call, stop() { stopped = true; } };
    globalThis.icax = {
      async getApplicationChannelId() { return (await call("application-channel")).channelId; },
      async registerProductChannel(productId) { return (await call("register-product", { productId })).channelId; },
      async registerSceneChannel(projectId, sceneId) { return (await call("register-scene", { projectId, sceneId })).channelId; },
      subscribeSDOFrames(channel, listener) { if (!listeners.has(channel)) listeners.set(channel, new Set());listeners.get(channel).add(listener);setTimeout(poll, 0);return () => listeners.get(channel)?.delete(listener); },
      async postSDOFrame(frame) { await call("post", { frame: { ...frame, callId: String(frame.callId), methodCode: String(frame.methodCode) } }); },
      async requestResource(request) { const result = await call("resource", { request });const binary = atob(result.bodyBase64 ?? ""), bytes = Uint8Array.from(binary, character => character.charCodeAt(0));return { status: result.status, headers: result.headers, body: bytes.buffer }; },
      async openFileDialog() { return files.shift() ?? null; },
      async saveFileDialog() { return savedProject; },
    };
  }, { savedProject });
  await page.goto(origin + "/iCAX-UI/SDK/AppShell/index.html");
  await page.waitForFunction(() => ["ready", "error"].includes(globalThis.__icaxAppShell?.getState().startupPhase), null, { timeout: 90000 });
  const startup = await page.evaluate(() => __icaxAppShell.getState());
  assert.equal(startup.startupPhase, "ready", startup.error || "Native product startup failed");
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "平面产品原生验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await ensurePlaneObjectsExpanded(page);
  const model = () => page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return structuredClone(getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam); });
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.capabilities?.enabledMethods?.includes("PlaneDrawing.Create"); });
  assert.deepEqual((await page.locator('.product-ribbon [data-action="select-ribbon-tab"]').allTextContents()).map(value => value.trim()), ["底图", "下料"]);
  assert.equal(await page.locator('[data-command-id="plane.create-part"],[data-command-id="plane.create-stock"]').count(), 0);
  const drawingLayout = await page.evaluate(() => {
    const box = selector => { const r = document.querySelector(selector).getBoundingClientRect();return { left: r.left, right: r.right, top: r.top, bottom: r.bottom }; };
    return { workspace: box(".iplane-workspace"), left: box(".cam-context-pane"), viewer: box(".cam-viewer"), right: box(".cam-info-pane"), command: box("[data-iplane-command-root]") };
  });
  assert.ok(drawingLayout.right.right <= drawingLayout.workspace.right + 1 && drawingLayout.right.right >= drawingLayout.workspace.right - 1);
  assert.ok(drawingLayout.command.left <= drawingLayout.workspace.left + 1 && drawingLayout.command.right >= drawingLayout.workspace.right - 1);
  assert.ok(drawingLayout.command.top >= drawingLayout.viewer.bottom - 1);report.drawingLayout = drawingLayout;
  passed("Actual product manifest and native capabilities load through unmodified AppShell and SDK");

  const command = page.locator("[data-iplane-command-input]");
  await command.fill("矩形 0,0 80,40");await command.press("Enter");
  await page.waitForFunction(() => document.querySelector("[data-iplane-command-input]")?.value === "");
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.drawings?.length === 1; });
  await page.waitForFunction(() => !document.querySelector('[data-command-id="plane.draw-line"]')?.disabled);
  const firstDrawing = (await model()).drawings[0].id;
  await page.locator(`[data-iplane-object-id="${firstDrawing}"]`).click();
  await command.fill("移动 10,20");await command.press("Enter");
  await page.waitForFunction(() => document.querySelector("[data-iplane-command-input]")?.value === "");
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.busy; });
  const geometry = id => page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }); }, id);
  const hasPoint = (geometry, x, y) => geometry.snapPoints?.some(entry => Math.abs(entry.point[0] - x) < 1e-8 && Math.abs(entry.point[1] - y) < 1e-8);
  assert.ok(hasPoint(await geometry(firstDrawing), 10, 20));
  await page.locator('.title-bar [data-action="undo-project"]').click();
  await page.waitForFunction(() => !document.querySelector('.title-bar [data-action="redo-project"]')?.disabled);
  assert.ok(hasPoint(await geometry(firstDrawing), 0, 0));
  await page.locator('.title-bar [data-action="redo-project"]').click();
  await page.waitForFunction(() => document.querySelector('.title-bar [data-action="redo-project"]')?.disabled);
  assert.ok(hasPoint(await geometry(firstDrawing), 10, 20));
  await page.locator('.title-bar [data-action="undo-project"]').click();
  await page.waitForFunction(() => !document.querySelector('.title-bar [data-action="redo-project"]')?.disabled);
  await command.fill("移动 20,10");await command.press("Enter");
  await page.waitForFunction(() => document.querySelector("[data-iplane-command-input]")?.value === "");
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.busy; });
  assert.ok(hasPoint(await geometry(firstDrawing), 20, 10));
  report.historyBranch = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.getUndoRedoState(); });
  assert.equal(report.historyBranch.canRedo, false);
  await page.waitForFunction(() => document.querySelector('.title-bar [data-action="redo-project"]')?.disabled);
  await command.fill("直线 0,0 100,0");await command.press("Enter");
  await page.waitForFunction(() => document.querySelector("[data-iplane-command-input]")?.value === "");
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && view.scene?.iplaneCam?.drawings?.length === 2; });
  const drawingState = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const { getAreaEntityIds } = await import("/apps/iplane-cam/webpage/workspaceState.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return { visible: getAreaEntityIds({}, view), render: view.viewport.getAppliedViewState(), hasTransformManipulator: Boolean(view.viewport.transformManipulator), selectionAxisVisible: view.viewport.selectionAxisHelper.visible }; });
  report.drawingState = drawingState;report.drawingModel = await model();
  assert.ok(drawingState.visible.length >= 2);assert.ok(drawingState.visible.every(id => drawingState.render.entityIds.includes(id)));
  assert.equal(drawingState.hasTransformManipulator, false);assert.equal(drawingState.selectionAxisVisible, false);
  await page.screenshot({ path: resolve(output, "drawing-native.png") });
  passed("Drawing commands commit through native Scene SDO; framework undo/redo and the shared worktable retain multiple native graphics");
  await clickPlaneCommand(page, "plane.draw-line");
  const canvasBox = await page.locator(".icax-three-viewport canvas").boundingBox();
  await page.mouse.click(canvasBox.x + canvasBox.width * 0.3, canvasBox.y + canvasBox.height * 0.3);
  assert.equal((await model()).drawings.length, 2);
  await page.mouse.click(canvasBox.x + canvasBox.width * 0.6, canvasBox.y + canvasBox.height * 0.6);
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);if (view.iplaneCamUI.operationError) throw new Error(view.iplaneCamUI.operationError);return !view.iplaneCamUI.busy && view.scene?.iplaneCam?.drawings?.length === 3; });
  await command.press("Escape");
  passed("Mouse drawing uses the existing viewport work plane; an intermediate point stays transient and only the completed line commits natively");

  await page.evaluate(path => __planeNativeFileQueue.push(path), unitlessPath);
  await page.locator('[data-command-id="plane.import-dxf"]').click();
  await page.waitForSelector('[data-iplane-key="import-units-panel"]');
  assert.equal((await model()).drawings.length, 3);
  assert.equal(await page.locator('[data-iplane-form="import-units"][data-iplane-form-field="units"]').inputValue(), "");
  await page.locator('[data-iplane-form="import-units"][data-iplane-form-field="units"]').selectOption("mm");
  await page.locator('[data-iplane-action="import-with-units"]').click();
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.drawings?.length === 4; });
  passed("A unitless DXF is rejected without creating an object until an explicit right-panel unit choice is submitted to the backend");

  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
  await page.evaluate(path => __planeNativeFileQueue.push(path), importedPartPath);
  await page.locator('[data-command-id="plane.import-parts"]').click();
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);if (view.iplaneCamUI.operationError) throw new Error(view.iplaneCamUI.operationError);return view.scene?.iplaneCam?.parts?.length > 0; }, null, { timeout: 120000 });
  const partId = (await model()).parts[0].id;
  await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${partId}"]`).click();
  await page.locator('[data-iplane-field="quantity"]').fill("3");
  await page.locator('[data-iplane-action="apply-properties"]').click();
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.parts?.[0]?.quantity === 3; });
  passed("A real external DXF enters the native part domain and quantity edits persist through command/query separation");

  await page.locator('[data-command-id="plane.stock-rect"]').click();
  for (const [field, value] of Object.entries({ name: "原生验收板材", width: "1000", height: "1000", quantity: "3" })) await page.locator(`[data-iplane-form="stock-rect"][data-iplane-form-field="${field}"]`).fill(value);
  await page.locator('[data-iplane-action="create-rectangle"]').click();
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.stocks?.length > 0; });
  if (stockDxf) {
    await page.evaluate(path => __planeNativeFileQueue.push(path), importedStockPath);
    await page.locator('[data-command-id="plane.import-stocks"]').click();
    await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.stocks?.length > 1; });
  }
  await page.locator('[data-command-id="plane.nesting-settings"]').click();
  for (const [field, value] of Object.entries({ partGap: "2", stockMargin: "5", rotationStepDeg: "90", timeLimitSeconds: "2" })) await page.locator(`[data-iplane-form="nesting-settings"][data-iplane-form-field="${field}"]`).fill(value);
  await page.locator('[data-iplane-action="apply-nesting-settings"]').click();
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.nestingParameters?.partGap === 2; });
  passed("Rectangle inventory and nesting parameters are native project data rather than frontend drafts");
  await page.locator('[data-iplane-form="nesting-settings"][data-iplane-form-field="partGap"]').fill("-1");
  await page.locator('[data-iplane-action="apply-nesting-settings"]').click();
  await page.waitForSelector(".iplane-operation-error");
  const invalidParameterMessage = await page.locator(".iplane-operation-error").textContent();
  assert.match(invalidParameterMessage, /排样参数/);assert.ok(!invalidParameterMessage.includes("PLANE_"));
  assert.equal((await model()).nestingParameters.partGap, 2);
  assert.equal(await page.locator('[data-iplane-form="nesting-settings"][data-iplane-form-field="partGap"]').inputValue(), "-1");
  await page.locator('[data-iplane-action="reset-form"][data-iplane-form="nesting-settings"]').click();
  assert.equal(await page.locator('[data-iplane-form="nesting-settings"][data-iplane-form-field="partGap"]').inputValue(), "2");
  passed("Rejected native parameters retain the user's input, present a concise Chinese message and leave project values unchanged");

  await page.locator('[data-command-id="plane.nesting-run"]').click();
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);if (view.iplaneCamUI.operationError) throw new Error(view.iplaneCamUI.operationError);return !view.iplaneCamUI.busy && (view.iplaneCamUI.runningTaskId || view.iplaneCamUI.task?.status); });
  await page.locator('[data-command-id="plane.nesting-settings"]').click();
  await page.locator('[data-iplane-form="nesting-settings"][data-iplane-form-field="partGap"]').fill("3");
  await page.locator('[data-iplane-action="apply-nesting-settings"]').click();
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && view.scene?.iplaneCam?.nestingParameters?.partGap === 3; });
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.runningTaskId; }, null, { timeout: 180000 });
  const changedInput = await model();assert.equal(changedInput.nestingParameters.partGap, 3);assert.ok(changedInput.results.every(result => result.stale));
  passed("Changing native nesting input during or after a run preserves the new parameters and leaves every prior result explicitly stale");
  const previousResultCount = changedInput.results.length;
  await page.locator('[data-command-id="plane.nesting-run"]').click();
  await waitForAsync(page, async previousResultCount => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);if (view.iplaneCamUI.operationError) throw new Error(view.iplaneCamUI.operationError);return !view.iplaneCamUI.runningTaskId && view.scene?.iplaneCam?.results?.length > previousResultCount; }, previousResultCount, { timeout: 180000 });
  await page.locator('[data-command-id="plane.layout-report"]').click();
  await page.waitForSelector('[data-iplane-key="report-panel"]');
  const downloadPromise = page.waitForEvent("download");await page.locator('[data-iplane-action="download-report"]').click();const download = await downloadPromise;
  const csvPath = resolve(output, "native-report.csv");await download.saveAs(csvPath);assert.ok(statSync(csvPath).size > 20);
  report.nativeResult = (await model()).results;
  await page.screenshot({ path: resolve(output, "nesting-native.png") });
  passed("The actual iPlaneNesting service publishes native result boards and a backend report that the UI can display and export");

  await page.locator('.title-bar [data-command-id="app.save"]').click();
  await page.waitForFunction(() => !document.querySelector('.title-bar [data-command-id="app.save"]')?.disabled);
  assert.ok(existsSync(savedProject), "Actual project file was not written");assert.ok(statSync(savedProject).size > 100);
  // Only our byte-identical test copies are removed. The user's downloaded
  // source files remain untouched, and reopening must use embedded resources.
  unlinkSync(importedPartPath);unlinkSync(unitlessPath);if (importedStockPath) unlinkSync(importedStockPath);
  const beforeSave = await model(), projectId = await page.evaluate(() => __icaxAppShell.getState().activeProjectId);
  const close = page.evaluate(projectId => __icaxAppShell.closeProject(projectId), projectId);
  await page.waitForSelector("[data-project-close-dialog]");await page.locator('[data-project-close-choice="discard"]').click();await close;
  await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), savedProject);
  await waitForAsync(page, async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.results?.length > 0; }, null, { timeout: 90000 });
  const reopened = await model();
  assert.equal(reopened.parts.length, beforeSave.parts.length);assert.equal(reopened.parts[0].quantity, 3);
  assert.equal(reopened.stocks.length, beforeSave.stocks.length);assert.equal(reopened.drawings.length, beforeSave.drawings.length);assert.equal(reopened.results.length, beforeSave.results.length);
  assert.deepEqual(reopened.nestingParameters, beforeSave.nestingParameters);
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
  await waitForAsync(page, async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { getAreaEntityIds } = await import("/apps/iplane-cam/webpage/workspaceState.mjs");
    const state = __icaxAppShell.getState();if (state.error) throw new Error(state.error);
    const view = getProjectView(state.activeProjectId), visible = getAreaEntityIds({}, view), applied = new Set(view.viewport?.getAppliedViewState().entityIds ?? []);
    return view.activeAreaId === "nesting" && view.areas.nesting?.viewContent?.rows?.length > 0 && visible.length > 0 && visible.every(id => applied.has(id));
  });
  const reopenedView = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { getAreaEntityIds } = await import("/apps/iplane-cam/webpage/workspaceState.mjs");
    const { loadRenderResource } = await import("/iCAX-UI/SDK/Viewport/renderResource.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), visible = getAreaEntityIds({}, view), ids = new Set(visible);
    const rows = view.areas[view.activeAreaId].viewContent.rows.filter(row => ids.has(String(row.entityId)));
    const resources = await Promise.all(rows.map(row => loadRenderResource(view.sceneProxy.resources, row.data.geometry)));
    return { applied: view.viewport.getAppliedViewState(), visible, geometryKinds: rows.map(row => row.data.geometryKind), resourceTypes: resources.map(resource => resource?.type) };
  });
  report.reopenedView = reopenedView;
  assert.ok(reopenedView.visible.length > 0 && reopenedView.visible.every(id => reopenedView.applied.entityIds.includes(id)));
  assert.ok(reopenedView.geometryKinds.length > 0 && reopenedView.geometryKinds.every(kind => kind === 2));
  assert.ok(reopenedView.resourceTypes.every(type => type === "geometry"));
  await page.screenshot({ path: resolve(output, "reopened-native.png") });
  passed("Actual framework save/close/reopen restores drawings, parts, inventory, parameters and native layout resources after the imported source copy is removed");
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);
  for (const asset of ["/iCAX-UI/SDK/AppShell/index.html", "/iCAX-UI/SceneProxy/SceneProxy.mjs", "/apps/_shared/workbench/createWorkbench.mjs", "/apps/iplane-cam/webpage/nativeOperations.mjs"]) assert.ok(assets[asset], "Actual product asset was not loaded: " + asset);
  assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  report.pass = true;
  await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  if (page) { try { report.shellState = await page.evaluate(() => globalThis.__icaxAppShell?.getState());report.transportError = await page.evaluate(() => globalThis.__planeNativeTransportError ?? "");report.nativeFailureState = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return { activeAreaId: view.activeAreaId, stateRequest: view.iplaneStateRequest, busy: view.iplaneCamUI.busy, native: await view.sceneProxy.invoke("PlaneScene.ReadState", {}) }; });await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }
  report.error = error.stack ?? String(error);throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests: transcript });
  writeFileSync(resolve(output, "native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
