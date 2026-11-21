// Genuine AppShell/SDK input with a private native runtime. File-dialog choices
// and JSON-lines transport are test plumbing; geometry and ECS are never mocked.
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
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-dynamic-input"));
assert.ok(process.env.ICAX_PLANE_NATIVE_BRIDGE, "Set ICAX_PLANE_NATIVE_BRIDGE to the isolated native acceptance runtime");
const executable = resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE);
assert.ok(existsSync(executable), "The selected native acceptance runtime does not exist");
mkdirSync(output, { recursive: true });
const runId = String(Date.now()), savedProject = resolve(output, `dynamic-input-${runId}.ipcam`);
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
const hud = () => page.locator("[data-iplane-dynamic-input]");
const dimension = name => page.locator(`[data-iplane-dimension="${name}"]`);
const dimensionRow = name => page.locator(`[data-iplane-dimension-row="${name}"]`);
async function expectHud(visible) { if (visible) await hud().waitFor({ state: "visible" });else await waitForAsync(() => ![...document.querySelectorAll("[data-iplane-dynamic-input]")].some(node => node.getClientRects().length)); }
async function assertNoCommit(count, points = 1) { const state = await drawingState();assert.equal(state.native.drawings.length, count);assert.equal(state.points.length, points);return state; }
async function assertCircle(row, radius, center) {
  const result = await geometry(row.id);assert.equal(result.curves.length, 1);const curve = result.curves[0];assert.equal(curve.type, "circle");assert.equal(curve.radius, radius);
  if (center) for (let axis = 0; axis < 2; axis++) assert.ok(Math.abs(curve.center[axis] - center[axis]) < 1e-7);
  return result;
}
async function assertRectangle(row, width, height, anchor, signs) {
  const result = await geometry(row.id);assert.equal(result.curves.length, 1);const curve = result.curves[0];assert.equal(curve.type, "polyline");assert.equal(curve.closed, true);assert.equal(curve.points.length, 4);
  const lo = [0, 1].map(axis => Math.min(...curve.points.map(point => point[axis]))), hi = [0, 1].map(axis => Math.max(...curve.points.map(point => point[axis])));
  assert.ok(Math.abs(hi[0] - lo[0] - width) < 1e-7);assert.ok(Math.abs(hi[1] - lo[1] - height) < 1e-7);
  if (anchor && signs) for (let axis = 0; axis < 2; axis++) assert.ok(Math.abs((signs[axis] < 0 ? hi : lo)[axis] - anchor[axis]) < 1e-7, "The rectangle lost the mouse quadrant around its first corner");
  return result;
}
async function assertPreviewRectangle(width, height) {
  const state = await drawingState(), positions = state.preview.points;assert.equal(state.preview.visible, true);assert.ok(positions.length >= 15);
  const xs = positions.filter((_, index) => index % 3 === 0), ys = positions.filter((_, index) => index % 3 === 1);
  assert.ok(Math.abs(Math.max(...xs) - Math.min(...xs) - width) < 1e-3);assert.ok(Math.abs(Math.max(...ys) - Math.min(...ys) - height) < 1e-3);
}
async function cancelWithMenu(point, action = "cancel-drawing") {
  await page.mouse.click(point.x, point.y, { button: "right" });await page.locator("[data-iplane-context-menu]").waitFor({ state: "visible" });
  const drawing = await drawingState();await assertDrawingMenu(page.locator("[data-iplane-context-menu]"), { polyline: drawing.tool === "plane.draw-polyline" });
  await page.locator(`[data-iplane-context-menu] [data-iplane-action="${action}"]`).click();await expectHud(false);
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return !ui.busy && ui.activeTool === "plane.select" && !ui.commandPoints.length; });
}
async function exerciseDynamicInputs() {
  await ensurePlaneObjectsExpanded(page);
  assert.equal((await drawingState()).native.drawings.length, 0);await expectHud(false);
  await clickPlaneCommand(page, "plane.snap-toggle");
  report.smallMouseShapes = [];
  for (const primitive of ["circle", "rect"]) {
    await start(primitive);const first = await screenPoint(.22, .23), second = { x: first.x + 40, y: first.y + 40 }, a = await pointerWorld(first), b = await pointerWorld(second);
    await click(first);await waitPrefix(1);await expectHud(true);await assertNoCommit(0);
    await click(second);const row = await newRow([], 1);
    report.smallMouseShapes.push({ primitive, first, second, geometry: primitive === "circle" ? await assertCircle(row, Math.hypot(b[0] - a[0], b[1] - a[1]), a) : await assertRectangle(row, Math.abs(b[0] - a[0]), Math.abs(b[1] - a[1]), a, [1, -1]) });
    await page.locator('.title-bar [data-action="undo-project"]').click();await waitCount(0);
  }
  passed("Passive HUDs leave small mouse circles and rectangles unobstructed; native undo restores the initially empty drawing");

  const radius = dimension("radius"), width = dimension("width"), height = dimension("height");
  let before = (await drawingState()).native.drawings;
  await start("circle");const centerPoint = await screenPoint(.23, .28), center = await pointerWorld(centerPoint);await click(centerPoint);await waitPrefix(1);await expectHud(true);
  assert.equal(await dimensionRow("radius").isVisible(), true);assert.match(await dimensionRow("radius").locator("[data-iplane-dimension-value]").textContent(), /R/);assert.equal(await radius.evaluate(input => document.activeElement === input), false, "Showing R stole canvas focus");
  await hover(await screenPoint(.37, .20));await page.keyboard.type("25.5");assert.equal(await radius.inputValue(), "25.5");assert.equal(await radius.evaluate(input => document.activeElement === input), true);
  await assertNoCommit(0);const firstHudBounds = await dimensionRow("radius").boundingBox();await hover(await screenPoint(.61, .39));assert.equal(await radius.inputValue(), "25.5");assert.deepEqual(await dimensionRow("radius").boundingBox(), firstHudBounds);
  const preview = (await drawingState()).preview;assert.equal(preview.visible, true);assert.ok(preview.points.length > 30);
  for (let index = 0; index < preview.points.length; index += 3) assert.ok(Math.abs(Math.hypot(preview.points[index] - center[0], preview.points[index + 1] - center[1]) - 25.5) < 1e-3, "The radius draft did not control the transient circle");
  await page.screenshot({ path: resolve(output, "radius-draft-preview.png") });
  await radius.press("Enter");const circle = await newRow(before, 1);report.radius = await assertCircle(circle, 25.5, center);await expectHud(false);
  passed("After a mouse-selected center, canvas digits enter R without a click; the draft previews transiently and Enter commits the exact native radius");

  before = (await drawingState()).native.drawings;await start("circle");const badCenterPoint = await screenPoint(.55, .25), badCenter = await pointerWorld(badCenterPoint);await click(badCenterPoint);await waitPrefix(1);await hover(await screenPoint(.69, .24));await page.keyboard.type("1");
  report.invalidRadius = [];
  for (const value of ["-5", "0", "1e309", "wrong"]) {
    await radius.fill(value);await radius.press("Enter");await page.locator("[data-iplane-dimension-error]").waitFor({ state: "visible" });await assertNoCommit(1);assert.equal(await radius.inputValue(), value);
    report.invalidRadius.push({ value, error: await page.locator("[data-iplane-dimension-error]").textContent() });
  }
  await click(await screenPoint(.82, .43));await assertNoCommit(1);assert.equal(await radius.inputValue(), "wrong");assert.equal(await radius.evaluate(input => document.activeElement === input), true, "Invalid mouse confirmation did not retain its field");
  await radius.fill("12.25");await radius.press("Enter");const correctedCircle = await newRow(before, 2);report.correctedRadius = await assertCircle(correctedCircle, 12.25, badCenter);
  passed("Nonpositive, nonfinite and invalid radius text stays editable and blocks Enter and mouse confirmation; correcting it creates the exact circle");

  before = (await drawingState()).native.drawings;await start("rect");const anchorPoint = await screenPoint(.24, .58), anchor = await pointerWorld(anchorPoint);await click(anchorPoint);await waitPrefix(1);await hover(await screenPoint(.44, .37));await expectHud(true);
  assert.equal(await dimensionRow("width").isVisible(), true);assert.equal(await dimensionRow("height").isVisible(), true);assert.equal(await width.evaluate(input => document.activeElement === input), false);
  await page.keyboard.type("60");assert.equal(await width.inputValue(), "60");await width.press("Enter");await assertNoCommit(2);assert.equal(await width.getAttribute("data-iplane-dimension-locked"), "true");assert.equal(await height.evaluate(input => document.activeElement === input), true);
  await page.keyboard.type("35");assert.equal(await height.inputValue(), "35");await hover(await screenPoint(.47, .34));assert.equal(await width.inputValue(), "60");assert.equal(await height.inputValue(), "35");await assertPreviewRectangle(60, 35);await assertNoCommit(2);
  await page.screenshot({ path: resolve(output, "rectangle-two-dimensions.png") });await height.press("Enter");const rectangle = await newRow(before, 3);report.rectangle = await assertRectangle(rectangle, 60, 35, anchor, [1, 1]);
  passed("Rectangle W Enter locks width and advances to H without committing; H Enter submits the exact two-dimensional native rectangle");

  before = (await drawingState()).native.drawings;await start("rect");const tabAnchorPoint = await screenPoint(.55, .57), tabAnchor = await pointerWorld(tabAnchorPoint);await click(tabAnchorPoint);await waitPrefix(1);await page.keyboard.press("Tab");assert.equal(await width.evaluate(input => document.activeElement === input), true);
  await width.fill("");await width.press("Tab");assert.equal(await height.evaluate(input => document.activeElement === input), true);await height.fill("bad");await height.press("Tab");assert.equal(await width.evaluate(input => document.activeElement === input), true);assert.equal(await width.inputValue(), "");assert.equal(await height.inputValue(), "bad");await assertNoCommit(3);
  assert.equal(await page.locator("[data-iplane-dimension-error]").isVisible(), false, "Tab validated an unfinished dimension instead of switching fields");
  await hover(await screenPoint(.40, .76));
  await page.keyboard.type("42");await width.press("Tab");assert.equal(await width.getAttribute("data-iplane-dimension-locked"), "true");assert.equal(await height.evaluate(input => document.activeElement === input), true);await assertNoCommit(3);
  await height.fill("26");await height.press("Tab");assert.equal(await height.getAttribute("data-iplane-dimension-locked"), "true");assert.equal(await width.evaluate(input => document.activeElement === input), true);await assertNoCommit(3);
  await hover(await screenPoint(.71, .42));await assertPreviewRectangle(42, 26);await width.press("Enter");await assertNoCommit(3);await height.press("Enter");const tabRectangle = await newRow(before, 4);report.tabRectangle = await assertRectangle(tabRectangle, 42, 26, tabAnchor, [1, 1]);
  passed("Tab switches and locks valid W/H fields without creating geometry; locked sizes survive a mouse quadrant change");

  report.quadrants = [];
  for (const [index, [x, y]] of [[.74, .27], [.74, .73], [.27, .27], [.27, .73]].entries()) {
    before = (await drawingState()).native.drawings;await start("rect");const originPoint = await screenPoint(.50, .50), originWorld = await pointerWorld(originPoint), target = await screenPoint(x, y), targetWorld = await pointerWorld(target);
    await click(originPoint);await waitPrefix(1);await hover(target);await page.keyboard.type(String(31 + index));await width.press("Enter");await assertNoCommit(4 + index);
    await hover({ x: target.x + 2, y: target.y + 1 });await hover(target);const expectedHeight = Math.abs(targetWorld[1] - originWorld[1]);await assertPreviewRectangle(31 + index, expectedHeight);
    await click(target);const row = await newRow(before, 5 + index);const signs = [Math.sign(targetWorld[0] - originWorld[0]), Math.sign(targetWorld[1] - originWorld[1])];
    report.quadrants.push({ signs, geometry: await assertRectangle(row, 31 + index, expectedHeight, originWorld, signs) });
  }
  passed("A locked W combines with an unlocked mouse-selected H in every quadrant while native dimensions remain positive and exact");

  before = (await drawingState()).native.drawings;await start("rect");await click(await screenPoint(.23, .52));await waitPrefix(1);await hover(await screenPoint(.39, .31));await page.keyboard.press("Tab");
  await width.fill("0");await width.press("Enter");await page.locator("[data-iplane-dimension-error]").waitFor({ state: "visible" });await assertNoCommit(8);assert.equal(await width.inputValue(), "0");assert.equal(await width.evaluate(input => document.activeElement === input), true);
  await width.fill("20");await width.press("Enter");await height.fill("-3");await height.press("Enter");await assertNoCommit(8);assert.equal(await height.inputValue(), "-3");
  await click(await screenPoint(.41, .71));await assertNoCommit(8);assert.equal(await height.inputValue(), "-3");assert.equal(await height.evaluate(input => document.activeElement === input), true);
  await height.fill("15");await height.press("Enter");const correctedRectangle = await newRow(before, 9);report.correctedRectangle = await assertRectangle(correctedRectangle, 20, 15);
  passed("Invalid rectangle dimensions block keyboard and mouse commits, retain the original draft and allow correction without restarting");

  // Use a compact real layout so both sidebars have actual scroll ranges.
  await page.setViewportSize({ width: 1280, height: 440 });await start("circle");const focusAnchor = await screenPoint(.45, .43);await click(focusAnchor);await waitPrefix(1);await hover(await screenPoint(.63, .31));await page.keyboard.press("Tab");await radius.click();assert.equal(await radius.evaluate(input => document.activeElement === input), true);await radius.press("Control+a");await page.keyboard.type("123.45");
  await radius.press("Home");await radius.press("ArrowRight");for (let index = 0; index < 3; index++) await radius.press("Shift+ArrowRight");
  const dimensionSelector = '[data-iplane-dimension="radius"]';
  const interactionBefore = await page.evaluate(async selector => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), input = document.querySelector(selector), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane"), canvas = document.querySelector(".icax-three-viewport canvas"), overlay = document.querySelector("[data-iplane-dynamic-input]"), row = input.closest("label");
    left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .25);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .25);
    globalThis.__dynamicInteraction = { input, left, right, canvas, overlay, inputEvents: 0 };input.addEventListener("input", () => __dynamicInteraction.inputEvents++);
    const box = row.getBoundingClientRect();return { value: input.value, selection: [input.selectionStart, input.selectionEnd], bounds: { x: box.x, y: box.y, width: box.width, height: box.height }, camera: view.viewport.getCameraState(), scroll: [left.scrollTop, right.scrollTop], capacity: [left.scrollHeight - left.clientHeight, right.scrollHeight - right.clientHeight] };
  }, dimensionSelector);
  assert.ok(interactionBefore.capacity.every(capacity => capacity > 0), "The refresh case did not provide scrollable real sidebars");
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });
  const refreshing = page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);await refreshPlaneState(view.iplaneLive.context, view, view.iplaneLive.ops, { render: true }); });
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());await hover(await screenPoint(.82, .77));
  const latestScroll = await page.evaluate(() => { const { left, right } = __dynamicInteraction;left.scrollTop = Math.floor((left.scrollHeight - left.clientHeight) * .75);right.scrollTop = Math.floor((right.scrollHeight - right.clientHeight) * .75);return [left.scrollTop, right.scrollTop]; });
  assert.ok(latestScroll.every((value, index) => value > interactionBefore.scroll[index]));
  await page.evaluate(() => __planeNativeBridge.releaseResponse());await refreshing;
  report.interaction = await page.evaluate(async selector => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), saved = __dynamicInteraction, input = document.querySelector(selector), overlay = document.querySelector("[data-iplane-dynamic-input]"), box = input.closest("label").getBoundingClientRect();return { sameInput: input === saved.input, sameCanvas: document.querySelector(".icax-three-viewport canvas") === saved.canvas, sameOverlay: overlay === saved.overlay, sameLeft: document.querySelector("[data-iplane-list]") === saved.left, sameRight: document.querySelector(".cam-info-pane") === saved.right, focused: document.activeElement === input, value: input.value, selection: [input.selectionStart, input.selectionEnd], bounds: { x: box.x, y: box.y, width: box.width, height: box.height }, camera: view.viewport.getCameraState(), scroll: [saved.left.scrollTop, saved.right.scrollTop] }; }, dimensionSelector);
  assert.ok(report.interaction.sameInput && report.interaction.sameCanvas && report.interaction.sameOverlay && report.interaction.sameLeft && report.interaction.sameRight && report.interaction.focused);
  assert.equal(report.interaction.value, interactionBefore.value);assert.deepEqual(report.interaction.selection, interactionBefore.selection);assert.deepEqual(report.interaction.bounds, interactionBefore.bounds);assert.deepEqual(report.interaction.camera, interactionBefore.camera);assert.deepEqual(report.interaction.scroll, latestScroll);await assertNoCommit(9);
  await radius.press("End");await page.keyboard.type("6");assert.equal(await radius.inputValue(), "123.456");assert.ok(await page.evaluate(() => __dynamicInteraction.inputEvents) > 0);
  await page.screenshot({ path: resolve(output, "dynamic-input-focus-preserved.png") });
  passed("Mouse movement and a delayed real native refresh retain HUD nodes, input listeners, draft, focus, selection, fixed editing position, camera and the latest sidebar scroll");

  await page.locator(".cam-info-pane h3").first().click();await assertNoCommit(9);assert.equal(await radius.inputValue(), "123.456");
  const menuPoint = await screenPoint(.70, .70);await page.mouse.click(menuPoint.x, menuPoint.y, { button: "right" });await page.locator("[data-iplane-context-menu]").waitFor({ state: "visible" });await assertNoCommit(9);
  await page.keyboard.press("Escape");await expectHud(true);assert.equal(await radius.inputValue(), "123.456");await assertNoCommit(9);
  await cancelWithMenu(menuPoint, "finish-drawing");assert.equal((await drawingState()).native.drawings.length, 9);
  passed("Blurring a radius field and opening or dismissing the cursor menu never commits; Finish discards the unfinished stage and clears the HUD");

  await page.setViewportSize({ width: 1600, height: 1000 });await start("circle");await click(await screenPoint(.30, .34));await waitPrefix(1);await hover(await screenPoint(.43, .25));await page.keyboard.type("18");await page.keyboard.press("Escape");await expectHud(false);await assertNoCommit(9, 0);
  await start("rect");await click(await screenPoint(.45, .52));await waitPrefix(1);await hover(await screenPoint(.58, .38));await page.keyboard.type("30");await width.press("Enter");await page.keyboard.type("20");await cancelWithMenu(await screenPoint(.72, .73));await assertNoCommit(9, 0);
  await start("circle");await click(await screenPoint(.30, .34));await waitPrefix(1);assert.notEqual(await radius.getAttribute("data-iplane-dimension-locked"), "true");assert.notEqual(await radius.inputValue(), "18");await hover(await screenPoint(.43, .25));await page.keyboard.type("45");
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();await expectHud(false);await assertNoCommit(9, 0);await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();
  for (const primitive of ["line", "polyline"]) { await start(primitive);await click(await screenPoint(.35, .42));await waitPrefix(1);await expectHud(false);await page.keyboard.press("Escape");await assertNoCommit(9, 0); }
  await start("arc");await click(await screenPoint(.35, .42));await waitPrefix(1);await expectHud(true);
  for (const key of ["radius", "startAngleDeg", "sweepAngleDeg"]) assert.equal(await page.locator(`[data-iplane-dimension-row="${key}"]`).isVisible(), true);
  await page.keyboard.press("Escape");await expectHud(false);await assertNoCommit(9, 0);
  passed("Escape, cursor Cancel, page or tool changes clear dimension state; new stages are clean, arcs receive R/alpha/sweep and unrelated line tools do not receive this HUD");

  await page.locator('.title-bar [data-action="undo-project"]').click();await waitCount(8);await page.locator('.title-bar [data-action="redo-project"]').click();await waitCount(9);await assertRectangle(correctedRectangle, 20, 15);
  const exactBefore = await Promise.all((await drawingState()).native.drawings.map(async row => ({ id: row.id, curves: (await geometry(row.id)).curves })));
  report.saved = await saveAndReopen();assert.deepEqual(report.saved.after.native.drawings.map(row => row.id).sort(), report.saved.before.native.drawings.map(row => row.id).sort());
  for (const original of exactBefore) assert.deepEqual((await geometry(original.id)).curves, original.curves);
  report.resources = await readRenderResources(report.saved.after.native.drawings);assert.ok(report.resources.every(resource => resource.type === "geometry" && resource.points.length > 0));
  await page.screenshot({ path: resolve(output, "dynamic-input-reopened.png") });
  passed("Native undo/redo and actual project save/close/reopen preserve every entered radius, rectangle dimension and render resource");
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
  if (!await page.locator(".cam-workbench").count()) await page.evaluate(() => __icaxAppShell.createProject({ productId: "icax.iplane-cam", projectName: "绘图动态输入原生验收" }));
  await page.waitForSelector(".cam-workbench", { timeout: 60000 });
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene?.iplaneCam?.capabilities?.enabledMethods?.includes("PlaneDrawing.Create"); });
  await exerciseDynamicInputs();assert.ok(checks.length > 0, "No dynamic-input acceptance cases ran");
  assert.deepEqual(pageErrors, []);assert.deepEqual(failures, []);assert.equal(await page.evaluate(() => globalThis.__planeNativeTransportError ?? ""), "");
  report.pass = true;await page.evaluate(() => __planeNativeBridge.stop());
} catch (error) {
  report.error = error.stack ?? String(error);if (page) { try { report.failureState = await drawingState();await page.screenshot({ path: resolve(output, "failure.png") }); } catch {} }throw error;
} finally {
  Object.assign(report, { checks, failures, assets, stderr: stderr.join(""), requests });
  writeFileSync(resolve(output, "dynamic-input-native-browser-report.json"), JSON.stringify(report, null, 2));
  await browser?.close();await new Promise(done => server.close(done));
  if (!processClosed) { try { await rpc({ action: "exit" }); } catch {}child.stdin.end(); }
}
console.log(JSON.stringify({ pass: report.pass, checks: checks.length, output }));
