// Real browser controls -> production SDO -> BRep and GPU resources -> .ictd.
// This uses the normal Debug development license bypass, never a success stub.
// The shell is an isolated host for production dialogs; desktop file pickers and
// the installed WebView container are outside this test's coverage.
// Coordinates are horizontal periodic arc length U and vertical axial S. Raw
// coordinates survive persistence; clipping is exercised in the native mapping.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { copyFileSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const root = fileURLToPath(new URL("../../../", import.meta.url)), sourceRoot = resolve(root, "src");
const artifacts = resolve(root, "output/tests/side-sketch-native-browser");
const runtime = resolve(artifacts, "runtime"); mkdirSync(runtime, { recursive: true });
const dllDirectory = process.env.ICAX_SIDE_SKETCH_NATIVE_DLL_DIR || resolve(sourceRoot, "x64/Debug");
for (const name of readdirSync(dllDirectory).filter(name => /\.dll$/i.test(name))) copyFileSync(resolve(dllDirectory, name), resolve(runtime, name));
copyFileSync(process.env.ICAX_SIDE_SKETCH_NATIVE_BRIDGE || resolve(artifacts, "native/SideSketchAcceptanceBridge.exe"), resolve(runtime, "SideSketchAcceptanceBridge.exe"));
const dllHash = createHash("sha256").update(readFileSync(resolve(runtime, "TubeDesigner.dll"))).digest("hex");
const bridge = spawn(resolve(runtime, "SideSketchAcceptanceBridge.exe"), [], { cwd: root, windowsHide: true,
  env: { ...process.env, PATH: runtime + ";" + dllDirectory + ";" + process.env.PATH }, stdio: ["pipe", "pipe", "pipe"] });
const waiting = new Map(), requests = []; let sequence = 0, stderr = "", browser, page;
bridge.stderr.on("data", bytes => { stderr += bytes; });
const failAll = error => { for (const request of waiting.values()) { clearTimeout(request.timer); request.reject(error); } waiting.clear(); };
bridge.on("error", failAll); bridge.on("exit", code => { if (waiting.size) failAll(new Error(`Native bridge exited ${code}: ${stderr}`)); });
createInterface({ input: bridge.stdout }).on("line", line => {
  let response; try { response = JSON.parse(line); } catch { failAll(new Error("Invalid native response: " + line)); return; }
  const request = waiting.get(response.id); if (!request) return;
  waiting.delete(response.id); clearTimeout(request.timer);
  if (response.ok) request.resolve(response.result); else request.reject(new Error(response.error));
});
const rpc = message => new Promise((complete, reject) => {
  const id = ++sequence, record = { id, action: message.action, scope: message.scope, method: message.method, started: Date.now() }; requests.push(record);
  const timer = setTimeout(() => { waiting.delete(id); reject(new Error(`Native timeout: ${message.action}/${message.method}`)); }, 180000);
  waiting.set(id, { timer, reject, resolve(result) { record.elapsed = Date.now() - record.started; complete(result); } });
  bridge.stdin.write(JSON.stringify({ id, ...message }) + "\n");
});
const invoke = (method, payload = {}, scope = "scene") => rpc({ action: "invoke", scope, method, payload });
const partsIn = scene => scene.tubeDesigner?.nestingGroups?.flatMap(group => group.parts ?? []) ?? [];

try {
  await rpc({ action: "reset" });
  const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, "apps/tube-designer/templates/profile/round/profile.json"), "utf8"));
  const parameters = Object.fromEntries(descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]));
  const evaluated = await invoke("EvaluateProfilePackage", { profileRef: { scope: "system", id: "round" }, parameters }, "product");
  const profile = { id: "round", name: "圆管", descriptor, defaultParameters: parameters, previewProfile: evaluated.profile };
  const scene = await invoke("List"); assert.equal(partsIn(scene).length, 0);
  const scopeFailures = [];
  const scopeSketch = { schema: "icax.tube-sketch", schemaVersion: 1, kind: "side", length: 400,
    faceHeight: Math.PI * 40, coordinateSpace: "arc-length-axial", perimeter: Math.PI * 40, trajectoryWidth: 2,
    entities: [{ id: "scope-window", kind: "rectangle", x: 20, y: 100, width: 16, height: 40, radius: 0, closed: true }] };
  for (const method of ["PreviewNestingSideSketchPart", "AddNestingSideSketchPart", "ReleaseNestingSideSketchPreview"]) {
    await assert.rejects(invoke(method, { profileRef: { scope: "system", id: "round" }, parameters, length: 400, previewResourceKey: "wrong-scope", sketch: scopeSketch }, "product"), error => {
      scopeFailures.push({ method, error: error.message }); return error.message.length > 0;
    });
  }
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
  page = await browser.newPage({ viewport: { width: 1660, height: 1020 } }); page.setDefaultTimeout(45000);
  const errors = [], external = []; page.on("pageerror", error => errors.push(error.message));
  page.on("dialog", dialog => dialog.accept());
  await page.exposeFunction("sideSketchNativeRpc", rpc);
  await page.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.origin !== "http://side-sketch-native.test") { external.push(url.href); return route.abort(); }
    if (url.pathname === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body></html>' });
    const file = resolve(sourceRoot, url.pathname.replace(/^\/src\//, ""));
    if (!url.pathname.startsWith("/src/") || !file.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://side-sketch-native.test");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}" + readFileSync(resolve(sourceRoot, "apps/_shared/workbench/styles/laser3dcam.css"), "utf8") + tubeDesignerCss });
  await page.evaluate(async ({ profile, scene }) => {
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    const standard = await import("/src/apps/tube-designer/webpage/nestingStandardPart.mjs");
    const parts = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const preview = await import("/src/apps/tube-designer/webpage/sideSketchPreview.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { renderDesignerOperationOverlay } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const mount = document.querySelector("#app");
    const view = { activeAreaId: "nesting", pending: false, scene, tubeDesignerSystemProfiles: [profile],
      tubeDesignerSelectedProfileId: "system:round", tubeDesignerTemplateProfiles: [], tubeDesignerUserData: { profiles: [] } };
    const f = window.fixture = { view, sketch, standard, parts, preview, pending: 0, calls: [], resources: [], failures: [], patches: 0, fullRenders: 0 };
    Object.defineProperty(f, "state", { get() { return view.tubeDesignerSketch; } });
    Object.defineProperty(f, "draft", { get() { return f.state?.sideByPart?.[f.state.targetPartId]; } });
    f.partList = () => parts.listNestingParts(view.scene.tubeDesigner ?? {});
    const invoke = async (method, payload, options, scope = "scene") => {
      const record = { method, payload: structuredClone(payload), scope }; f.calls.push(record);
      options?.onReport?.({ message: "正在运行真实二维建模", completed: 0, total: 1 });
      try { const result = await window.sideSketchNativeRpc({ action: "invoke", scope, method: method.replace(/^TubeDesigner\./, ""), payload }); record.result = result; return result; }
      catch (error) { record.error = error.message; throw error; }
    };
    const resources = { async get(url, options) {
      const version = Number(options?.headers?.get("ICAX-Resource-Version") || 0);
      const result = await window.sideSketchNativeRpc({ action: "resource", payload: { url, version } });
      f.resources.push({ url, version, bytes: result.bytes });
      return new Response(Uint8Array.from(atob(result.base64), char => char.charCodeAt(0)));
    } };
    const context = f.context = { mount, sceneProxy: { invoke, resources }, productProxy: { invoke: (method, payload, options) => invoke(method, payload, options, "product") } };
    const ops = f.ops = { createSideSketchViewport(options) { return f.viewport = createThreeViewport(options); }, showNotice() {}, renderProject() {
      const html = standard.renderNestingStandardPartDialog(view) + sketch.renderSectionSketchDialog(context, view) + renderDesignerOperationOverlay(context, view);
      if (sketch.patchSketchDialogDom(context, view, mount, ops)) f.patches++;
      else if (standard.patchNestingStandardPartDom(view, mount, html, context.sceneProxy)) f.patches++;
      else {
        f.fullRenders++;
        mount.innerHTML = '<main id="main-workbench"><button data-test-new-side-part>二维绘制零件</button>'
          + f.partList().map(part => `<button data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${part.entityId}">二维编辑 ${part.entityId}</button>`).join("") + "</main>" + html;
        standard.rememberNestingStandardPartDom(view, mount, context.sceneProxy);
        sketch.rememberSketchDialogDom(view, mount, context.sceneProxy);
      }
      sketch.attachSketchAreaInteractions(context, view, mount, ops);
    } };
    f.dispatch = async (action, target = {}) => {
      f.pending++;
      try {
        if (action === "new") return await standard.handleNestingStandardPartRibbonCommand(context, view, "nesting.draw-2d-part", ops);
        if (action?.startsWith("tube-designer-nesting-standard-")) return await standard.handleNestingStandardPartAction(context, view, action, target, ops);
        if (action?.startsWith("tube-designer-sketch-")) return await sketch.handleSketchAreaAction(context, view, action, target, ops);
        if (action === "tube-designer-part-open-sketch") return await parts.handlePartsAreaAction(context, view, action, target, ops);
      } catch (error) { f.failures.push({ action, error: error.message }); }
      finally { f.pending--; }
    };
    document.addEventListener("change", event => void f.dispatch(event.target.dataset.camChangeAction, event.target));
    document.addEventListener("click", event => {
      const target = event.target.closest("[data-cam-action],[data-test-new-side-part]");
      if (target && !target.disabled) void f.dispatch(target.hasAttribute("data-test-new-side-part") ? "new" : target.dataset.camAction, target);
    });
    f.render = ops.renderProject; f.render();
  }, { profile, scene });
  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending && !window.fixture.state?.sidePreviewPending, null, { timeout: 180000 });
  const ready = async () => {
    await idle();
    assert.equal(await page.evaluate(() => window.fixture.state?.sidePreviewError || ""), "", "The new native preview must succeed; a previous GPU model cannot count as its result");
    const loaded = await page.evaluate(async () => await window.fixture.preview.waitForSideSketchPreview(document.querySelector("#app")));
    assert.equal(loaded, true, await page.evaluate(() => JSON.stringify({ error: window.fixture.view.error, previewError: window.fixture.state?.sidePreviewError, failures: window.fixture.failures, notice: document.querySelector("[data-side-sketch-render-notice]")?.textContent })));
    assert.equal(await page.locator("[data-side-sketch-preview-ready=true]").count(), 1);
    assert.ok(await page.evaluate(() => window.fixture.viewport.geometryObjects.get(window.fixture.state.sidePreview.baseGeometry.url)?.getAttribute("position")?.count) > 0);
  };
  const command = name => page.locator(`[data-sketch-command="sketch.${name}"]`);
  const action = name => page.locator(`[data-cam-action="tube-designer-sketch-${name}"]`);
  const field = name => page.locator(`[data-sketch-field="${name}"]`);
  const input = async (locator, value) => { await locator.fill(String(value)); await locator.press("Tab"); await idle(); };
  const screenshot = name => page.screenshot({ path: resolve(artifacts, name + ".png") });
  const clickPoint = async (u, s) => {
    const point = await page.evaluate(({ u, s }) => {
      const f = window.fixture, svg = document.querySelector("[data-tube-sketch-canvas]");
      const domain = f.state.sideReference.unfolding;
      const p = svg.createSVGPoint(); p.x = 70 + u / domain.perimeter * 880; p.y = 510 - s / domain.length * 420;
      const screen = p.matrixTransform(svg.getScreenCTM()); return { x: screen.x, y: screen.y };
    }, { u, s });
    await page.mouse.click(point.x, point.y); await idle();
  };
  const openNew = async (expectedCount = 0) => {
    await page.locator("[data-test-new-side-part]").click(); await idle();
    await input(page.getByRole("spinbutton", { name: "长度（mm）", exact: true }), 400);
    await page.getByRole("button", { name: "开始绘制", exact: true }).click(); await ready();
    assert.equal(await page.evaluate(() => window.fixture.partList().length), expectedCount);
  };

  // Preparing and cancelling the drafting session must leave the scene empty.
  await openNew();
  await command("rectangle").click(); await clickPoint(20, 100); await clickPoint(36, 140);
  await command("cancel").click(); await idle();
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerSketchDialogOpen), false);
  assert.equal(partsIn(await invoke("List")).length, 0);
  assert.equal(await page.evaluate(() => window.fixture.calls.filter(call => call.method === "TubeDesigner.AddNestingSideSketchPart").length), 0);

  await openNew();
  await page.evaluate(() => {
    const f = window.fixture; f.svg = document.querySelector("[data-tube-sketch-canvas]");
    f.canvas = document.querySelector(".icax-three-viewport-canvas"); f.workbench = document.querySelector("#main-workbench");
  });
  const drawingPerimeter = await page.evaluate(() => window.fixture.state.sideReference.unfolding.perimeter);
  await command("rectangle").click(); await clickPoint(20, 100); await clickPoint(36, 140);
  await input(field("width"), 16); await input(field("height"), 40); await input(field("centerY"), 120); await input(field("centerX"), drawingPerimeter * 5);
  const rectangle = await page.evaluate(() => window.fixture.draft.entities[0]);
  assert.equal(rectangle.kind, "rectangle"); assert.ok(Math.abs(rectangle.x - (drawingPerimeter * 5 - 8)) < 1e-3); assert.ok(Math.abs(rectangle.width - 16) < 1e-3);
  assert.ok(Math.abs(rectangle.y - 100) < 1e-3); assert.ok(Math.abs(rectangle.height - 40) < 1e-3);
  await command("line").click(); await clickPoint(35, 250); await clickPoint(35, 280);
  await page.locator("[data-tube-sketch-canvas]").press("Enter"); await idle();
  await input(field("x1"), 35); await input(field("y1"), 250); await input(field("x2"), 35); await input(field("y2"), 280);
  await input(page.getByRole("spinbutton", { name: "开放线条切缝宽度（mm）" }), 2);
  assert.equal(await page.evaluate(() => window.fixture.draft.entities.length), 2);
  await action("preview-side").click(); await ready();
  assert.deepEqual(await page.evaluate(() => {
    const f = window.fixture; return { svg: f.svg === document.querySelector("[data-tube-sketch-canvas]"), canvas: f.canvas === document.querySelector(".icax-three-viewport-canvas"), workbench: f.workbench === document.querySelector("#main-workbench") };
  }), { svg: true, canvas: true, workbench: true });
  assert.equal(partsIn(await invoke("List")).length, 0);
  await screenshot("01-real-closed-seam-and-open-kerf-preview");
  await command("commit").click(); await idle();
  const created = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === "TubeDesigner.AddNestingSideSketchPart"));
  assert.ok(created?.result?.partEntityId, JSON.stringify({ error: created?.error, failures: await page.evaluate(() => window.fixture.failures) }));
  const id = created.result.partEntityId;
  const firstPart = partsIn(await invoke("List")).find(part => part.entityId === id);
  assert.equal(partsIn(await invoke("List")).length, 1);
  assert.equal(firstPart.properties["tubeDesigner.sideSketch"].trajectoryWidth, 2);
  assert.equal(firstPart.properties["tubeDesigner.sideSketch"].coordinateSpace, "arc-length-axial");
  assert.ok(firstPart.properties["tubeDesigner.sideSketch"].entities[0].x > drawingPerimeter * 4, "Unbounded circumferential coordinates are preserved in the saved sketch");
  assert.equal(firstPart.properties["tubeDesigner.sideSketch"].entities.length, 2);
  assert.ok(firstPart.properties["tubeDesigner.sideSketchRecipe"]);
  assert.ok(firstPart.properties["tubeDesigner.sideSketchBaseResourceId"]);
  const firstGeometry = await rpc({ action: "geometry", payload: { partEntityId: id } }); assert.equal(firstGeometry.valid, true);
  const baseVolume = Math.PI * (20 ** 2 - 18 ** 2) * 400;
  assert.ok(firstGeometry.volume > 0 && firstGeometry.volume < baseVolume - 100);

  const saved = await rpc({ action: "save", payload: { file: "side-sketch-native.ictd", name: "二维绘制跨缝及开放切缝验收" } }); assert.equal(saved.saved, true);
  const reopened = await rpc({ action: "open", payload: { file: "side-sketch-native.ictd" } }); assert.equal(reopened.opened, true);
  const reopenedGeometry = await rpc({ action: "geometry", payload: { partEntityId: id } });
  assert.equal(reopenedGeometry.valid, true); assert.ok(Math.abs(reopenedGeometry.volume - firstGeometry.volume) < 1e-5);
  await page.evaluate(scene => { const f = window.fixture; f.view.scene = scene; f.render(); }, reopened.snapshot);
  await page.locator(`[data-cam-action="tube-designer-part-open-sketch"][data-tube-designer-part-id="${id}"]`).click(); await ready();
  assert.equal(await page.evaluate(() => window.fixture.draft.entities.length), 2);
  assert.ok(await page.evaluate(() => window.fixture.draft.entities[0].x) > drawingPerimeter * 4);
  assert.equal(await page.getByRole("spinbutton", { name: "开放线条切缝宽度（mm）" }).inputValue(), "2");
  const existingPreview = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === "TubeDesigner.PreviewNestingSideSketchPart"));
  assert.equal(existingPreview.payload.partEntityId, id); assert.equal(existingPreview.payload.profileRef, undefined);
  await page.evaluate(() => {
    const f = window.fixture; f.draft.selectedIds = [f.draft.entities.find(entity => entity.kind === "rectangle").id]; f.render();
  });
  await input(field("centerY"), 220);
  await input(page.getByRole("spinbutton", { name: "开放线条切缝宽度（mm）" }), 4);
  await action("preview-side").click(); await ready(); await screenshot("02-reopened-edit-replaces-cuts-from-frozen-blank");
  await command("commit").click(); await idle();
  const updated = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === "TubeDesigner.SavePartSketch"));
  assert.equal(updated.payload.partEntityId, id); assert.equal(updated.error, undefined);
  const updatedGeometry = await rpc({ action: "geometry", payload: { partEntityId: id } }); assert.equal(updatedGeometry.valid, true);
  assert.equal(updatedGeometry.resourceId, firstGeometry.resourceId);
  assert.ok(updatedGeometry.version > firstGeometry.version, "Editing creates a new immutable manufacturing-resource version");
  assert.equal(updated.payload.resourceVersion, firstGeometry.version);
  assert.ok(updatedGeometry.volume < firstGeometry.volume - 40, "A larger open kerf removes more wall material");

  // A new part made from the edited recipe is an independent geometric oracle.
  // Comparing real BRep intersection catches accumulated cuts at the old site.
  const reference = await invoke("AddNestingSideSketchPart", { profileRef: { scope: "system", id: "round" }, parameters, length: 400,
    sketch: updated.payload.sketch, name: "编辑结果独立几何参照" });
  const comparison = await rpc({ action: "compare-geometry", payload: { firstPartEntityId: id, secondPartEntityId: reference.partEntityId } });
  assert.ok(Math.abs(comparison.firstVolume - comparison.secondVolume) < baseVolume * 1e-7, JSON.stringify(comparison));
  // OpenCascade's default volume integration on the common result is less
  // precise than on these identical input shapes. 0.01% remains far below the
  // omitted old window (over 1,000 mm3), so accumulated cuts still fail.
  assert.ok(Math.abs(comparison.firstVolume - comparison.commonVolume) < baseVolume * 1e-4, JSON.stringify(comparison));

  await rpc({ action: "save", payload: { file: "side-sketch-edited-native.ictd" } });
  const finalReopen = await rpc({ action: "open", payload: { file: "side-sketch-edited-native.ictd" } });
  const finalPart = partsIn(finalReopen.snapshot).find(part => part.entityId === id);
  assert.equal(finalPart.properties["tubeDesigner.sideSketch"].trajectoryWidth, 4);
  assert.ok(Math.abs(finalPart.properties["tubeDesigner.sideSketch"].entities.find(entity => entity.kind === "rectangle").y - 200) < 1e-3);

  // End trimming changes the manufactured length but keeps the original
  // drafting rectangle; reopening must still edit the same immutable blank.
  const perimeter = Number(created.payload.sketch.perimeter);
  const trimSketch = { ...created.payload.sketch, entities: [{ id: "end-trim", kind: "rectangle", x: -2 * perimeter - 7, y: -15, width: 5 * perimeter + 14, height: 45, radius: 0, closed: true }] };
  const trimmed = await invoke("AddNestingSideSketchPart", { profileRef: { scope: "system", id: "round" }, parameters, length: 400, sketch: trimSketch, name: "全周端部切除重开验收" });
  const trimmedPart = trimmed.tubeDesigner.nestingGroups.flatMap(group => group.parts ?? []).find(part => part.entityId === trimmed.partEntityId);
  assert.ok(Math.abs(trimmedPart.length - 370) < 0.1, JSON.stringify({ length: trimmedPart.length }));
  assert.equal(trimmedPart.properties["tubeDesigner.sideSketchRecipe"].length, 400);
  assert.equal(trimmedPart.properties["tubeDesigner.sideSketch"].entities[0].y, -15, "Only the mapping clips axial geometry; the saved drawing keeps its original extent");
  assert.equal(trimmedPart.properties["tubeDesigner.sideSketch"].entities[0].width, 5 * perimeter + 14);
  await rpc({ action: "save", payload: { file: "side-sketch-end-trim-native.ictd" } });
  const trimmedReopen = await rpc({ action: "open", payload: { file: "side-sketch-end-trim-native.ictd" } });
  await page.evaluate(scene => { const f = window.fixture; f.view.scene = scene; f.render(); }, trimmedReopen.snapshot);
  await page.locator(`[data-cam-action="tube-designer-part-open-sketch"][data-tube-designer-part-id="${trimmed.partEntityId}"]`).click(); await ready();
  assert.ok(Math.abs(await page.evaluate(() => window.fixture.state.sideReference.unfolding.length) - 400) < 1e-6);
  assert.equal(await page.evaluate(() => window.fixture.draft.entities[0].y), -15);
  assert.ok(await page.evaluate(() => window.fixture.draft.entities[0].width) > perimeter * 5);
  await page.evaluate(() => { const f = window.fixture; f.draft.selectedIds = [f.draft.entities[0].id]; f.render(); });
  await input(field("height"), 50); await input(field("centerY"), 10);
  await command("commit").click(); await idle();
  const endUpdate = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === "TubeDesigner.SavePartSketch"));
  assert.equal(endUpdate.error, undefined, JSON.stringify(endUpdate));
  assert.equal(endUpdate.payload.sketch.length, 400);
  assert.equal(endUpdate.payload.sketch.coordinateSpace, "arc-length-axial");
  assert.equal(endUpdate.payload.sketch.entities[0].y, -15);
  const endUpdatedPart = partsIn(await invoke("List")).find(part => part.entityId === trimmed.partEntityId);
  assert.ok(Math.abs(endUpdatedPart.length - 365) < 0.1, JSON.stringify({ length: endUpdatedPart.length }));
  const endGeometry = await rpc({ action: "geometry", payload: { partEntityId: trimmed.partEntityId } }); assert.equal(endGeometry.valid, true);
  assert.ok(Math.abs(endGeometry.volume - baseVolume * 365 / 400) < 1, "Multiple wraps map to one full-circumference cut and axial overflow is clipped");
  const topSketch = { ...created.payload.sketch, entities: [{ id: "far-end-trim", kind: "rectangle", x: perimeter, y: 390, width: perimeter * 2, height: 25, radius: 0, closed: true }] };
  const topTrim = await invoke("AddNestingSideSketchPart", { profileRef: { scope: "system", id: "round" }, parameters, length: 400, sketch: topSketch, name: "另一端超界截断验收" });
  const topPart = topTrim.tubeDesigner.nestingGroups.flatMap(group => group.parts ?? []).find(part => part.entityId === topTrim.partEntityId);
  assert.ok(Math.abs(topPart.length - 390) < 0.1);
  assert.equal(topPart.properties["tubeDesigner.sideSketch"].entities[0].height, 25);
  const topGeometry = await rpc({ action: "geometry", payload: { partEntityId: topTrim.partEntityId } }); assert.equal(topGeometry.valid, true);
  assert.ok(Math.abs(topGeometry.volume - baseVolume * 390 / 400) < 1, "Overflow at the far axial end is clipped to the blank");
  const outsideSketch = { ...created.payload.sketch, entities: [{ id: "entirely-outside", kind: "rectangle", x: perimeter * 5, y: 450, width: 16, height: 40, radius: 0, closed: true }] };
  const outsidePayload = { profileRef: { scope: "system", id: "round" }, parameters, length: 400, sketch: outsideSketch };
  const beforeOutsidePreview = partsIn(await invoke("List")).length;
  const outsidePreview = await invoke("PreviewNestingSideSketchPart", { ...outsidePayload, previewResourceKey: "outside-axial-range" });
  assert.equal(outsidePreview.available, true);
  assert.equal(partsIn(await invoke("List")).length, beforeOutsidePreview);
  assert.ok((await rpc({ action: "resource", payload: outsidePreview.preview.baseGeometry })).bytes > 0);
  await invoke("ReleaseNestingSideSketchPreview", { previewResourceKey: "outside-axial-range" });
  const outsidePart = await invoke("AddNestingSideSketchPart", { ...outsidePayload, name: "全部纵向域外草图保留验收" });
  const outsideGeometry = await rpc({ action: "geometry", payload: { partEntityId: outsidePart.partEntityId } });
  assert.equal(outsideGeometry.valid, true); assert.ok(Math.abs(outsideGeometry.volume - baseVolume) < 1, "An empty clipped intersection keeps the whole blank");
  await rpc({ action: "save", payload: { file: "side-sketch-all-outside-native.ictd" } });
  const outsideReopen = await rpc({ action: "open", payload: { file: "side-sketch-all-outside-native.ictd" } });
  const outsideStored = partsIn(outsideReopen.snapshot).find(part => part.entityId === outsidePart.partEntityId);
  assert.equal(outsideStored.length, 400);
  assert.equal(outsideStored.properties["tubeDesigner.sideSketch"].entities[0].y, 450);
  assert.equal(outsideStored.properties["tubeDesigner.sideSketch"].entities[0].height, 40);

  // Reproduce the user's round tube with a rectangle and a circular opening.
  // Confirmation used to spend over four minutes assembling its cutters.
  await page.evaluate(scene => { const f = window.fixture; f.view.scene = scene; f.render(); }, outsideReopen.snapshot);
  await page.locator("[data-test-new-side-part]").click(); await idle();
  await input(page.locator('[data-standard-part-parameter="width"]'), 62);
  await input(page.getByRole("spinbutton", { name: "长度（mm）", exact: true }), 1000);
  await page.getByRole("button", { name: "开始绘制", exact: true }).click(); await ready();
  await command("rectangle").click(); await clickPoint(18, 595); await clickPoint(53, 885);
  await input(field("width"), 35); await input(field("height"), 290);
  await input(field("centerX"), 35.5); await input(field("centerY"), 740);
  await command("circle").click(); await clickPoint(80, 550); await clickPoint(103, 550);
  await input(field("diameter"), 46); await input(field("centerX"), 80); await input(field("centerY"), 550);
  assert.equal(await page.evaluate(() => window.fixture.draft.entities.length), 2);
  const circleSaveStarted = Date.now();
  await command("commit").click();
  await page.locator("[data-sketch-save-progress]").waitFor({ state: "visible" });
  assert.equal(await command("commit").isDisabled(), true);
  assert.match(await command("commit").innerText(), /保存中/);
  await screenshot("03-circle-confirmation-visible-progress");
  await idle();
  const circleSaveElapsed = Date.now() - circleSaveStarted;
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerSketchDialogOpen), false,
    "A real circular opening saves and exits without another confirmation");
  assert.equal(await page.locator(".tube-section-sketch-dialog").count(), 0,
    "Successful native confirmation removes the drafting modal");
  const circleCreated = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === "TubeDesigner.AddNestingSideSketchPart"));
  assert.ok(circleCreated?.result?.partEntityId, JSON.stringify(circleCreated));
  assert.equal(circleCreated.error, undefined);
  assert.ok(circleSaveElapsed < 60000, `Circular confirmation took ${circleSaveElapsed} ms`);
  const circleGeometry = await rpc({ action: "geometry", payload: { partEntityId: circleCreated.result.partEntityId } });
  assert.equal(circleGeometry.valid, true);
  const circlePart = partsIn(await invoke("List")).find(part => part.entityId === circleCreated.result.partEntityId);
  assert.ok(Math.abs(circlePart.properties["tubeDesigner.sideSketch"].entities.find(entity => entity.kind === "circle").radius - 23) < 1e-3);
  assert.equal(circlePart.properties["tubeDesigner.sideSketchRecipe"].length, 1000);
  assert.ok(circleGeometry.volume > 0 && circleGeometry.volume < Math.PI * (31 ** 2 - 29 ** 2) * 1000 - 1000);

  const final = await page.evaluate(() => ({ calls: window.fixture.calls.map(call => ({ method: call.method, scope: call.scope, error: call.error })),
    failures: window.fixture.failures, fullRenders: window.fixture.fullRenders, localPatches: window.fixture.patches, resourceReads: window.fixture.resources }));
  assert.deepEqual(final.failures, []); assert.deepEqual(errors, []); assert.deepEqual(external, []);
  assert.equal(final.calls.filter(call => call.method === "TubeDesigner.AddNestingSideSketchPart").length, 2);
  assert.ok(final.resourceReads.every(resource => resource.bytes > 0));
  assert.ok(final.calls.some(call => call.method === "TubeDesigner.ReleaseNestingSideSketchPreview"));
  writeFileSync(resolve(artifacts, "acceptance-results.json"), JSON.stringify({ ...final, browserChannel: process.env.ICAX_BROWSER_CHANNEL || "msedge", nativeDll: { directory: dllDirectory, sha256: dllHash },
    coverage: "Real Debug production SDO, browser production dialogs and GPU resources, true BRep comparison, .ictd save/open; isolated host, no desktop file chooser or installed WebView", scopeFailures,
    partEntityId: id, firstGeometry, reopenedGeometry, updatedGeometry, comparison,
    coordinateSpace: "arc-length-axial", unboundedCircumferentialCenter: drawingPerimeter * 5,
    endTrim: { partEntityId: trimmed.partEntityId, recipeLength: 400, wraps: 5, originalAxialStart: -15, initialManufacturedLength: trimmedPart.length, editedManufacturedLength: endUpdatedPart.length, geometry: endGeometry },
    farEndTrim: { partEntityId: topTrim.partEntityId, originalAxialEnd: 415, manufacturedLength: topPart.length, geometry: topGeometry },
    entirelyOutside: { partEntityId: outsidePart.partEntityId, originalAxialStart: 450, manufacturedLength: outsideStored.length, geometry: outsideGeometry },
    circularConfirmation: { partEntityId: circleCreated.result.partEntityId, elapsedMs: circleSaveElapsed,
      circumference: circleCreated.payload.sketch.perimeter, blankLength: 1000, circleRadius: 23,
      visibleProgress: true, exitedAutomatically: true, geometry: circleGeometry },
    project: saved.file, requests }, null, 2));
  console.log("Side sketch native browser acceptance passed: horizontal periodic U, vertical clipped S, closed seam cut at 5P, open kerf width, real 3D meshes, cancel without entity, correct scopes, .ictd reload, nonaccumulating edits, multi-wrap end clipping and circular confirmation with visible progress and automatic modal exit.");
  console.log("Artifacts: " + artifacts);
} catch (error) {
  if (page) await page.screenshot({ path: resolve(artifacts, "failure.png") }).catch(() => {});
  const ui = page ? await page.evaluate(() => ({ error: window.fixture?.view.error, state: window.fixture?.state,
    calls: window.fixture?.calls?.map(call => ({ method: call.method, error: call.error })), failures: window.fixture?.failures })).catch(() => null) : null;
  writeFileSync(resolve(artifacts, "failure.json"), JSON.stringify({ error: error.message, stack: error.stack, requests, stderr, ui }, null, 2));
  throw error;
} finally {
  if (browser) await browser.close();
  if (bridge.exitCode == null) { try { await rpc({ action: "exit" }); } catch {} bridge.stdin.end(); }
}
