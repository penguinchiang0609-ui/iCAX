// Recovered side-curve editing through the real drawing model, view and DOM patch.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE
  || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
const root = fileURLToPath(new URL("../../", import.meta.url));
const descriptor = JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/mold/curve-pocket/tool.json", import.meta.url), "utf8"));
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } }), errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://curve-pocket.test/**", route => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body><main id='app'></main></body>" });
    const file = resolve(root, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://curve-pocket.test/");
  await page.evaluate(async descriptor => {
    const drawing = await import("/src/apps/tube-designer/webpage/partDrawing.mjs");
    const model = await import("/src/apps/tube-designer/webpage/partDrawingModel.mjs");
    const dom = await import("/src/apps/tube-designer/webpage/partDrawingDom.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    const style = document.createElement("style");
    style.textContent = tubeDesignerCss + ".td-draw-sidebar,.td-draw-parameters{display:block;height:180px;overflow:auto}.td-draw-property-scroll{height:240px;overflow:auto}.td-draw-tree,.td-draw-inspector{height:320px}";
    document.head.append(style);
    const section = { source: "recovered", name: "识别曲线", profile: { contours: [
      { kind: "path", closed: true, segments: [
        { kind: "bspline", degree: 2, controlPoints: [[-10, 0], [0, 5], [10, 0]], knots: [0, 1], multiplicities: [3, 3], periodic: false, startParameter: 0, endParameter: 1 },
        { kind: "line", start: [-10, 0], end: [10, 0], reversed: true },
      ] }, { kind: "circle", center: [0, 1], radius: 0.5 },
    ] } };
    const feature = { id: "recovered-curve", toolRef: { id: descriptor.id }, toolTarget: "side", section,
      toolParameters: {}, station: 100, face: "top", cutDepth: 0.2, blindHole: true };
    const state = model.createDrawingState({ entityId: "P1026", length: 500, properties: { "tubeDesigner.partDrawing": {
      features: [feature], ends: {}, drawing: { length: 500, section: { source: "recovered", name: "主管", profile: { contours: [{ kind: "circle", radius: 20 }] } } },
    } } });
    model.installDrawingCatalogue(state, { tools: [{ ...descriptor, digest: "fixture" }] });
    state.drawing = { length: 500, name: "P1026", section: { name: "主管", profile: { contours: [{ kind: "circle", radius: 20 }] } } };
    // Enough real feature-tree rows to exercise independent left/nested scrolling.
    state.features.push(...Array.from({ length: 35 }, (_, index) => model.normalizeDrawingFeature({ ...structuredClone(feature), id: `extra-${index}` })));
    state.draft = state.features[0];
    const view = { activeAreaId: "nesting", pending: false, tubeDesignerPartDrawing: {
      part: { entityId: "P1026" }, mainApplied: true, selected: feature.id, mode: "feature", state,
    } };
    const mount = document.querySelector("#app");
    mount.innerHTML = drawing.renderPartDrawingDialog(view);
    dom.rememberPartDrawingDom(view, mount);
    const canvas = document.createElement("canvas");
    mount.querySelector("[data-part-drawing-viewport]").append(canvas);
    const patch = () => {
      if (!dom.patchPartDrawingDom(view, mount, drawing.renderPartDrawingDialog(view))) throw new Error("Drawing patch was not accepted");
    };
    const fixture = window.fixture = { view, state, section, canvas, patch, model, inputEvents: 0, canvasEvents: 0 };
    canvas.addEventListener("click", () => fixture.canvasEvents++);
    mount.addEventListener("change", event => {
      if (model.updateDrawingField(state, event.target)) patch();
    });
    fixture.beginPreview = () => new Promise(resolve => { fixture.finishPreview = () => {
      state.preview = { revision: state.revision }; patch(); resolve();
    }; });
  }, descriptor);
  const control = key => page.locator(`[data-tube-designer-punch-field="${key}"]`);
  assert.equal(await control("cutDepth").inputValue(), "0.2");
  assert.equal(await control("face").count(), 1);
  assert.equal(await control("rotation").count(), 1);
  assert.equal(await control("angle").count(), 0);
  assert.equal(await control("direction").count(), 0);
  assert.equal(await page.locator("[data-drawing-section=side]").count(), 1);
  assert.match(await page.locator(".td-draw-inspector").innerText(), /切除轮廓/);
  await control("face").selectOption("bottom");
  await control("rotation").fill("25"); await control("rotation").dispatchEvent("change");
  await control("cutDepth").fill("0.4"); await control("cutDepth").dispatchEvent("change");
  assert.deepEqual(await page.evaluate(() => {
    const f = fixture.model.getDrawingPayload(fixture.state).features[0];
    return { depth: f.cutDepth, face: f.face, rotation: f.rotation, exact: JSON.stringify(f.section) === JSON.stringify(fixture.section), parameters: f.toolParameters };
  }), { depth: 0.4, face: "bottom", rotation: 25, exact: true, parameters: {} });
  await control("cutDepth").fill("0.45");
  await control("cutDepth").press("ArrowLeft"); await control("cutDepth").press("Shift+ArrowLeft");
  await page.evaluate(() => {
    const f = fixture;
    f.depthInput = document.activeElement;
    f.depthInput.addEventListener("input", () => f.inputEvents++);
    f.running = f.beginPreview();
    const scrolls = [".td-draw-sidebar", ".td-draw-tree [role=tree]", ".td-draw-parameters", ".td-draw-property-scroll"];
    f.scrolls = scrolls.map((selector, index) => { const node = document.querySelector(selector); node.scrollTop = 30 + index * 20; return { selector, node }; });
    // User scrolls again while the preview is in flight.
    for (const [index, item] of f.scrolls.entries()) item.node.scrollTop = 55 + index * 15;
    f.expectedScroll = f.scrolls.map(item => item.node.scrollTop);
    f.finishPreview();
  });
  await page.evaluate(() => fixture.running);
  const interaction = await page.evaluate(() => ({
    focus: document.activeElement === fixture.depthInput,
    input: fixture.depthInput === document.querySelector('[data-tube-designer-punch-field="cutDepth"]'),
    canvas: fixture.canvas === document.querySelector("canvas"),
    scroll: fixture.scrolls.map(item => item.node.scrollTop), expected: fixture.expectedScroll,
  }));
  assert.deepEqual(interaction, { focus: true, input: true, canvas: true, scroll: interaction.expected, expected: interaction.expected });
  assert.ok(interaction.expected.every(value => value > 0), "Both outer and nested panes actually scroll");
  await control("cutDepth").press("9");
  assert.equal(await control("cutDepth").inputValue(), "0.95", "Caret and selected digit survive asynchronous preview patch");
  assert.equal(await page.evaluate(() => fixture.inputEvents), 1, "Original input listener stays attached");
  await page.evaluate(() => fixture.canvas.dispatchEvent(new MouseEvent("click")));
  assert.equal(await page.evaluate(() => fixture.canvasEvents), 1);
  await control("cutDepth").dispatchEvent("change");
  await control("blindHole").uncheck();
  assert.equal(await control("cutDepth").count(), 0);
  assert.equal(await page.evaluate(() => fixture.state.draft.cutDepth), 0.95, "Hidden depth remains as a draft");
  await control("blindHole").check();
  assert.equal(await control("cutDepth").inputValue(), "0.95");
  assert.equal(await page.evaluate(() => JSON.stringify(fixture.state.draft.section) === JSON.stringify(fixture.section)), true);
  assert.deepEqual(errors, []);
  console.log("Curve pocket browser: exact section/islands, depth/face/rotation editing, side controls, hidden depth draft, focused selection, latest pane scroll, input/canvas nodes and listeners passed.");
} finally { await browser.close(); }
