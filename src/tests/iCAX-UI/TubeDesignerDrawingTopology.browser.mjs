// Actual SDK picking and drawing DOM. CAD topology/geometry is a transport fixture;
// native bevel geometry and persistence are verified separately by the SDO suite.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { importBrowserAsset, serveBrowserAsset } from "./browserPackageRuntime.mjs";
const { tubeDesignerCss } = await importBrowserAsset("apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const output = resolve(process.env.ICAX_ARTIFACT_DIR || "output/tests/drawing-topology-browser");
mkdirSync(output, { recursive: true });
const browser = await chromium.launch({ headless: true, channel: "msedge" });
const report = { passed: false, nativeTransport: "controlled topology DTO fixture", actualSdkPicking: true, standaloneCefEndToEnd: false };
try {
  const page = await browser.newPage({ viewport: { width: 1250, height: 670 } }), errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://drawing-topology.test/**", serveBrowserAsset);
  await page.goto("http://drawing-topology.test/");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI}" + readFileSync("src/apps/_shared/workbench/styles/workbench.css", "utf8") + tubeDesignerCss
    + "[data-drawing-topology-editor]>div{max-height:120px;overflow:auto}" });
  await page.evaluate(async () => {
    const drawing = await import("/src/apps/tube-designer/webpage/partDrawing.mjs");
    const model = await import("/src/apps/tube-designer/webpage/partDrawingModel.mjs");
    const preview = await import("/src/apps/tube-designer/webpage/partDrawingPreview.mjs");
    const dom = await import("/src/apps/tube-designer/webpage/partDrawingDom.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { encodeNestingGeometry, encodePreviewMaterial } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const mount = document.createElement("main"); document.body.append(mount);
    const profile = { contours: [{ kind: "circle", radius: 20, center: [0, 0] }, { kind: "circle", radius: 18, center: [0, 0] }], width: 40, depth: 40 };
    const view = { activeAreaId: "nesting", pending: false, tubeDesignerSystemProfiles: [{ id: "round", name: "圆管", previewProfile: profile }] };
    drawing.openPartDrawing(view); const state = view.tubeDesignerPartDrawing.state;
    model.installDrawingCatalogue(state, { tools: [] });
    state.features = Array.from({ length: 70 }, (_, index) => model.normalizeDrawingFeature({ id: `feature-${index}`, type: "fixture", recordKind: "tool", station: index * 5 }));
    const resources = new Map(); let serial = 0, viewport, finishPreview;
    const put = (bytes, kind) => { const reference = { url: `fixture://drawing-topology/${++serial}`, version: 1, ...(kind ? { kind } : {}) }; resources.set(reference.url, bytes); return reference; };
    const box = new THREE.BoxGeometry(500, 40, 40); box.translate(250, 0, 0);
    const finished = put(encodeNestingGeometry({ positions: Array.from(box.attributes.position.array), indices: Array.from(box.index.array) })); box.dispose();
    const material = put(encodePreviewMaterial(0x668899ff));
    const topologyMaterial = put(encodePreviewMaterial(0x6699aa16));
    const edge = (id, z) => ({ id, adjacentFaceIds: [15, 19], geometry: put(encodeNestingGeometry({ kind: 2, positions: [100, -20, z, 400, -20, z], ranges: [0, 2, 0, 0, 0] }), "polyline") });
    const topology = { revision: "native-reference-42", edges: [edge(31, 20), edge(37, -20)], faces: [{ id: 15, edgeIds: [31, 37], geometry: put(encodeNestingGeometry({ positions: [100, -20, -20, 400, -20, -20, 400, -20, 20, 100, -20, 20], indices: [0, 1, 2, 0, 2, 3] })) }, { id: 19, edgeIds: [31], geometry: null }] };
    const result = { toolsOnly: false, geometry: finished, material, baseGeometry: finished, baseMaterial: material, edgeMaterial: material, topologyMaterial, topology, baseBounds: { min: [0, -20, -20], max: [500, 20, 20] }, toolPreviews: [], length: 500 };
    const f = window.fixture = { drawing, model, view, state, mount, result, calls: [], renders: 0, patches: 0, delayNext: false, get viewport() { return viewport; } };
    const context = { mount, sceneProxy: { async invoke(method, payload) {
      if (method === "TubeDesigner.ReleasePartDrawingPreview") { f.releases ??= []; f.releases.push(structuredClone(payload)); return { released: true }; }
      if (method !== "TubeDesigner.PreviewPartDrawing") throw new Error(`Unexpected scene method ${method}`);
      if (payload.toolsOnly !== false) throw new Error("Drawing must evaluate a finished CAD solid");
      f.calls.push(structuredClone(payload));
      if (f.delayNext) { f.delayNext = false; await new Promise(resolve => { finishPreview = resolve; f.releasePreview = resolve; }); }
      if (payload.drawing.bevels?.some(item => item.bevel.enabled && item.topologyRevision !== result.topology.revision))
        return { ...structuredClone(result), geometry: undefined, resultError: "坡口所选边已经变化，请重新选择",
          cutBoundaryGeometry: finished, cutBoundaryMaterial: material, resultValid: false, selectionTarget: "cut-boundary" };
      return structuredClone(result);
    }, resources: { async get(url) { return new Response(resources.get(url)); } } } };
    const ops = { createPartDrawingViewport(options) { return viewport = createThreeViewport(options); }, renderProject() {
      const html = drawing.renderPartDrawingDialog(view);
      if (dom.patchPartDrawingDom(view, mount, html)) f.patches++;
      else { f.renders++; mount.innerHTML = html; dom.rememberPartDrawingDom(view, mount); }
      preview.attachPartDrawingPreview(context, view, mount, ops); drawing.attachPartDrawingEditor(context, view, mount, ops);
    } };
    f.render = ops.renderProject;
    f.disposePreview = () => preview.disposePartDrawingPreview(mount);
    f.project = point => {
      const vector = new THREE.Vector3(point[0] - 250, point[1], point[2]).project(viewport.camera);
      const rect = viewport.renderer.domElement.getBoundingClientRect();
      return { x: rect.left + (vector.x + 1) * rect.width / 2, y: rect.top + (1 - vector.y) * rect.height / 2 };
    };
    f.action = (name, target = {}) => drawing.handlePartDrawingAction(context, view, "tube-designer-drawing-" + name, target, ops);
    mount.addEventListener("click", event => { const button = event.target.closest("[data-cam-action]"); if (button) void f.action(button.dataset.camAction.replace("tube-designer-drawing-", ""), button); });
    mount.addEventListener("change", event => { const name = event.target.dataset.camChangeAction; if (name) void f.action(name.replace("tube-designer-drawing-", ""), event.target); });
    ops.renderProject();
  });
  const ready = () => page.waitForFunction(() => fixture.state.preview?.revision === fixture.state.revision && !fixture.state.previewPending && !fixture.state.previewRenderPending && document.querySelector("[data-part-drawing-preview-ready=true]"));
  await ready();
  assert.equal(await page.evaluate(() => fixture.state.previewMode), "finished");
  assert.equal(await page.evaluate(() => fixture.viewport.sceneObjects.get("drawing:finished")?.isMesh), true);
  const first = await page.evaluate(() => fixture.project([250, -20, 20])); await page.mouse.click(first.x, first.y);
  await page.waitForFunction(() => fixture.state.topologySelection?.id === 31);
  assert.equal(await page.evaluate(() => fixture.state.topologySelection.faceIndex), undefined);
  const second = await page.evaluate(() => fixture.project([250, -20, -20])); await page.keyboard.down("Control"); await page.mouse.click(second.x, second.y); await page.keyboard.up("Control");
  assert.deepEqual(await page.evaluate(() => fixture.state.topologySelection.edgeIds), [31, 37]);
  await page.locator('[data-cad-bevel-field="enabled"]').check(); await ready();
  assert.deepEqual(await page.evaluate(() => fixture.calls.at(-1).drawing.bevels[0].edgeIds), [31, 37]);
  assert.equal(await page.evaluate(() => fixture.calls.at(-1).drawing.bevels[0].topologyRevision), "native-reference-42");
  await page.locator('[data-cad-bevel-field="side"]').selectOption("both"); await ready();
  await page.locator('[data-cam-change-action="tube-designer-drawing-bevel-reference-face"]').selectOption("15"); await ready();
  assert.equal(await page.evaluate(() => fixture.viewport.sceneObjects.has("drawing:topology:face:15")), true);
  assert.equal(await page.evaluate(() => fixture.viewport.selectedObjectIds.has("drawing:topology:face:15")), true);
  // Both scroll panes and nested right scroll are changed after an async request
  // begins; the accepted response must preserve the newest interaction state.
  await page.evaluate(async () => {
    fixture.canvas = document.querySelector(".icax-three-viewport-canvas");
    fixture.input = document.querySelector('[data-drawing-field="name"]');
    fixture.input.value = "正在继续输入的名字"; fixture.input.focus({ preventScroll: true }); fixture.input.setSelectionRange(2, 6);
    fixture.listenerCount = 0; fixture.input.addEventListener("input", () => fixture.listenerCount++);
    fixture.delayNext = true; await fixture.action("bevel-change", { dataset: { cadBevelField: "angleDegrees" }, value: "40" });
  });
  await page.waitForFunction(() => typeof fixture.releasePreview === "function");
  await page.evaluate(() => {
    fixture.left = document.querySelector('[role="tree"]'); fixture.right = document.querySelector(".td-draw-property-scroll");
    fixture.left.scrollTop = 450; fixture.right.scrollTop = 160;
    const nested = document.querySelector("[data-drawing-topology-editor] > div");
    nested.scrollTop = 45;
    fixture.nested = nested; fixture.expected = { left: fixture.left.scrollTop, right: fixture.right.scrollTop, nested: nested.scrollTop };
    fixture.releasePreview();
  });
  await ready();
  const preserved = await page.evaluate(() => {
    fixture.input.dispatchEvent(new Event("input", { bubbles: true }));
    return { left: fixture.left.scrollTop, right: fixture.right.scrollTop, nested: fixture.nested.scrollTop, expected: fixture.expected,
      focus: document.activeElement === fixture.input, range: [fixture.input.selectionStart, fixture.input.selectionEnd],
      inputSame: fixture.input === document.querySelector('[data-drawing-field="name"]'), canvasSame: fixture.canvas === document.querySelector(".icax-three-viewport-canvas"), listenerCount: fixture.listenerCount, fullRenders: fixture.renders };
  });
  assert.equal(preserved.left, preserved.expected.left); assert.equal(preserved.right, preserved.expected.right); assert.equal(preserved.nested, preserved.expected.nested);
  assert.equal(preserved.focus, true); assert.deepEqual(preserved.range, [2, 6]); assert.equal(preserved.inputSame, true); assert.equal(preserved.canvasSame, true); assert.equal(preserved.listenerCount, 1); assert.equal(preserved.fullRenders, 1);
  await page.locator('[data-cad-bevel-field="enabled"]').uncheck(); await ready();
  assert.equal(await page.locator('[data-cad-bevel-field="angleDegrees"]').isDisabled(), true);
  await page.locator('[data-cad-bevel-field="enabled"]').check(); await ready();
  assert.equal(await page.locator('[data-cad-bevel-field="angleDegrees"]').inputValue(), "40");
  await page.getByRole("combobox", { name: "三维选择类型" }).selectOption("face"); await ready();
  const face = await page.evaluate(() => fixture.project([250, -20, 0])); await page.mouse.click(face.x, face.y);
  await page.waitForFunction(() => fixture.state.topologySelection?.kind === "face");
  assert.equal(await page.evaluate(() => fixture.state.topologySelection.id), 15);
  assert.deepEqual(await page.evaluate(() => fixture.state.topologySelection.edgeIds), [31, 37]);
  assert.equal(await page.evaluate(() => fixture.viewport.sceneObjects.has("drawing:finished")), true);
  // The preceding CAD cut changes the exact topology. Its failed bevel still
  // exposes the current cut solid and native topology so the user can rebind.
  await page.evaluate(async () => {
    fixture.result.topology.revision = "native-reference-43";
    await fixture.action("main-change", { dataset: { drawingField: "length" }, value: "600" });
  });
  await ready();
  assert.match(await page.evaluate(() => fixture.state.error), /已经变化/);
  assert.equal(await page.evaluate(() => fixture.viewport.pickingEnabled), true);
  const bevelId = await page.evaluate(() => fixture.state.drawing.bevels[0].id);
  await page.locator(`[data-drawing-node="${bevelId}"]`).click();
  await page.getByRole("button", { name: "重新选择边面", exact: true }).click();
  await page.mouse.click(face.x, face.y);
  await page.waitForFunction(() => fixture.state.topologySelection?.kind === "face");
  await page.getByRole("button", { name: "应用选择", exact: true }).click(); await ready();
  assert.equal(await page.evaluate(() => fixture.state.drawing.bevels.length), 1);
  assert.equal(await page.evaluate(() => fixture.state.drawing.bevels[0].topologyRevision), "native-reference-43");
  assert.equal(await page.evaluate(() => fixture.state.drawing.bevels[0].bevel.angleDegrees), 40);
  assert.equal(await page.evaluate(() => fixture.state.error), "");
  assert.deepEqual(errors, []);
  await page.addStyleTag({ content: "[data-drawing-topology-editor]>div{max-height:none;overflow:visible}" });
  await page.evaluate(() => { document.querySelector(".td-draw-property-scroll").scrollTop = 0; });
  await page.screenshot({ path: resolve(output, "topology-and-bevel.png") });
  const scope = await page.evaluate(() => fixture.calls[0].previewResourceKey);
  assert.match(scope, /^[a-zA-Z0-9_-]{1,96}$/);
  assert.equal(await page.evaluate(() => new Set(fixture.calls.map(item => item.previewResourceKey)).size), 1);
  await page.evaluate(async () => {
    fixture.delayNext = true; fixture.releasePreview = null;
    await fixture.action("main-change", { dataset: { drawingField: "length" }, value: "700" });
  });
  await page.waitForFunction(() => fixture.state.previewPending && typeof fixture.releasePreview === "function");
  await page.evaluate(async () => { await fixture.action("cancel"); fixture.disposePreview(); });
  assert.equal(await page.evaluate(() => fixture.releases?.length ?? 0), 0, "Resources must remain until the pending native request finishes");
  await page.evaluate(() => fixture.releasePreview());
  await page.waitForFunction(() => fixture.releases?.length === 1);
  assert.deepEqual(await page.evaluate(() => fixture.releases[0]), { previewResourceKey: scope });
  Object.assign(report, { passed: true, interaction: preserved, requestCount: await page.evaluate(() => fixture.calls.length), errors });
  console.log("Drawing topology: real SDK native-ID edge/face picking, multi-edge bevel transport, latest async focus/selection/scroll and DOM identity passed.");
} finally {
  writeFileSync(resolve(output, "report.json"), JSON.stringify(report, null, 2)); await browser.close();
}
