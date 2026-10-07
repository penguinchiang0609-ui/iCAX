// Imported STEP/IGES part recovery: real browser action and pane interaction regression.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE
  || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const browser = await chromium.launch({ headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1300, height: 900 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://recover.test/**", route => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/") return route.fulfill({ contentType: "text/html",
      body: "<!doctype html><html lang='zh-CN'><body><main id='app'></main></body></html>" });
    const file = resolve(sourceRoot, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
      || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://recover.test/");
  await page.addStyleTag({ content: `
    .cam-context-pane,.cam-info-pane{height:250px;width:360px;overflow:auto;display:inline-block;vertical-align:top}
    .tube-designer-cutting-group-list{height:195px;overflow:auto}
    .tube-designer-cutting-inspector-scroll{height:170px;overflow:auto}
    .tube-designer-cutting-scene-actions{display:block}
  ` });
  await page.evaluate(async () => {
    const drawing = await import("/src/apps/tube-designer/webpage/partDrawing.mjs");
    const parts = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const mount = document.querySelector("#app");
    const profile = { schema: "icax.imported-tube-profile", schemaVersion: 1,
      kind: "imported-dxf", name: "圆管", width: 40, depth: 40,
      contours: [{ kind: "circle", radius: 20, center: [0, 0] },
        { kind: "circle", radius: 18, center: [0, 0] }] };
    const entries = Array.from({ length: 25 }, (_, index) => ({
      entityId: `part-${index}`, name: `进口管材 ${index}`, partNumber: `P${index}`,
      quantity: 1, length: 800, profile,
      manufacturingGeometryResourceId: `final-${index}`,
      manufacturingGeometryResourceVersion: 7,
      properties: { "manufacturing.partKind": "tube", "manufacturing.imported": true,
        "tubeDesigner.profile": profile },
    }));
    const scene = { tubeDesigner: { nestingGroups: [{ parts: entries }] } };
    const view = { activeAreaId: "nesting", pending: false, scene,
      tubeDesignerActiveNestingPartId: "part-0", tubeDesignerActivePartId: "part-0",
      tubeDesignerNestingSelectionKind: "part" };
    const recipe = { drawing: { schemaVersion: 1, length: 800,
      section: { source: "dxf", name: "圆管", profile } }, baseLength: 800,
      features: [], ends: { start: { type: "keep" }, end: { type: "keep" } } };
    let resolveRecovery;
    const context = { mount, sceneProxy: { invoke(method, payload) {
      if (method === "TubeDesigner.GetPartDrawing") {
        fixture.detailRequest = payload;
        return Promise.resolve({ partEntityId: payload.partEntityId,
          resourceVersion: payload.resourceVersion, definition: recipe });
      }
      if (method !== "TubeDesigner.RecoverImportedPartDrawing")
        throw new Error("Unexpected request: " + method);
      fixture.request = payload;
      return new Promise(resolve => { resolveRecovery = () => {
        resolve({ partEntityId: payload.partEntityId,
          resourceVersion: payload.resourceVersion, ready: true });
      }; });
    } } };
    const ops = { renderProject() {
      const left = parts.renderNestingLeftPane(context, view);
      const right = parts.renderNestingRightPane(context, view);
      const overlay = parts.renderNestingViewportOverlay(context, view);
      const editor = view.tubeDesignerPartDrawing ? drawing.renderPartDrawingDialog(view) : "";
      const restore = capturePaneInteraction(mount);
      mount.innerHTML = `<aside class="cam-context-pane">${left}</aside>`
        + `<aside class="cam-info-pane">${right}</aside>`
        + `<section class="viewport">${overlay}</section>${editor}`;
      restore();
    }, showNotice() {} };
    const fixture = window.fixture = { view, context, ops, request: null, detailRequest: null, error: "",
      resolveRecovery() { resolveRecovery?.(); } };
    document.addEventListener("click", event => {
      const target = event.target.closest("[data-cam-action]");
      if (target?.dataset.camAction === "tube-designer-drawing-recover")
        fixture.running = drawing.handlePartDrawingAction(context, view,
          target.dataset.camAction, target, ops).catch(error => { fixture.error = error.message; });
    });
    ops.renderProject();
  });
  assert.equal(await page.locator('[data-cam-action="tube-designer-drawing-recover"]').count(), 1);
  await page.evaluate(() => {
    const root = document.querySelector("#app");
    const input = root.querySelector('[data-tube-designer-part-field="name"]');
    input.focus({ preventScroll: true });
    input.value = "等待时仍可编辑";
    input.setSelectionRange(1, 4, "backward");
    root.querySelector(".cam-context-pane").scrollTop = 40;
    root.querySelector(".cam-info-pane").scrollTop = 55;
    root.querySelector(".tube-designer-cutting-group-list").scrollTop = 80;
    root.querySelector(".tube-designer-cutting-inspector-scroll").scrollTop = 70;
    root.querySelector('[data-cam-action="tube-designer-drawing-recover"]').click();
  });
  await page.waitForFunction(() => window.fixture.request
    && window.fixture.view.tubeDesignerPartRecoveryPending === "part-0");
  const latest = await page.evaluate(() => {
    const root = document.querySelector("#app");
    root.querySelector(".cam-context-pane").scrollTop = 105;
    root.querySelector(".cam-info-pane").scrollTop = 115;
    root.querySelector(".tube-designer-cutting-group-list").scrollTop = 220;
    root.querySelector(".tube-designer-cutting-inspector-scroll").scrollTop = 120;
    const input = root.querySelector('[data-tube-designer-part-field="name"]');
    input.focus({ preventScroll: true });
    input.value = "响应前的新输入";
    input.setSelectionRange(2, 5, "backward");
    const values = Object.fromEntries([".cam-context-pane", ".cam-info-pane",
      ".tube-designer-cutting-group-list", ".tube-designer-cutting-inspector-scroll"]
      .map(selector => [selector, root.querySelector(selector).scrollTop]));
    window.fixture.resolveRecovery();
    return values;
  });
  await page.waitForFunction(() => !window.fixture.view.tubeDesignerPartRecoveryPending
    && !!window.fixture.view.tubeDesignerPartDrawing);
  const after = await page.evaluate(() => {
    const root = document.querySelector("#app");
    const input = root.querySelector('[data-tube-designer-part-field="name"]');
    return { method: window.fixture.request, detailRequest: window.fixture.detailRequest,
      error: window.fixture.error,
      scrolls: Object.fromEntries([".cam-context-pane", ".cam-info-pane",
        ".tube-designer-cutting-group-list", ".tube-designer-cutting-inspector-scroll"]
        .map(selector => [selector, root.querySelector(selector).scrollTop])),
      focused: document.activeElement === input, value: input.value,
      selection: [input.selectionStart, input.selectionEnd, input.selectionDirection],
      editor: !!root.querySelector(".td-draw-workbench") };
  });
  assert.deepEqual(after.method, { partEntityId: "part-0", resourceVersion: 7 });
  assert.deepEqual(after.detailRequest, { partEntityId: "part-0", resourceVersion: 7 });
  assert.equal(after.error, "");
  assert.deepEqual(after.scrolls, latest, "Both panes and nested scrollers keep the latest async positions");
  assert.equal(after.focused, true);
  assert.equal(after.value, "响应前的新输入");
  assert.deepEqual(after.selection, [2, 5, "backward"]);
  assert.equal(after.editor, true);
  const conditional = await page.evaluate(() => {
    const f = window.fixture, root = document.querySelector("#app");
    const field = root.querySelector('[data-tube-designer-part-field="material"]');
    field.focus({ preventScroll: true });
    const part = f.view.scene.tubeDesigner.nestingGroups[0].parts[0];
    delete part.properties["manufacturing.imported"];
    f.ops.renderProject();
    return { removed: !root.querySelector('[data-tube-designer-part-field="material"]'),
      wrongFocus: document.activeElement === root.querySelector('[data-tube-designer-part-field="name"]') };
  });
  assert.deepEqual(conditional, { removed: true, wrongFocus: false });
  assert.deepEqual(errors, []);
  console.log("Imported part recovery browser: editor opens; latest pane scroll, focus, selection and conditional field state survive refresh.");
} finally { await browser.close(); }
