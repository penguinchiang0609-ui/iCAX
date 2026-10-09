// Real Chrome interaction coverage for sketch arrays. Native preview/persistence
// transport is deliberately mocked; this test checks production SVG and DOM.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const repository = fileURLToPath(new URL("../../../", import.meta.url));
const root = resolve(repository, "src");
const output = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repository, "output/tests/sketch-cad-browser"));
mkdirSync(output, { recursive: true });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome" });
const results = [];
const recordResult = results.push.bind(results);
results.push = value => { console.log("Passed: " + value.case); return recordResult(value); };
const assets = new Map();
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(12000);
  page.setDefaultNavigationTimeout(12000);
  page.on("console", message => { if (message.type() === "error" || message.text().startsWith("CAD fixture")) console.error(message.text()); });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.addInitScript(() => {
    const originalClone = window.structuredClone;
    window.arrayPerf = { active: false, clones: 0, frames: 0, frameMs: [], previewWrites: 0 };
    window.cadMovePerf = { active: false, hostWrites: 0 };
    window.structuredClone = (...args) => {
      if (window.arrayPerf.active) window.arrayPerf.clones++;
      return originalClone(...args);
    };
    const originalRaf = window.requestAnimationFrame;
    window.requestAnimationFrame = callback => originalRaf.call(window, time => {
      const active = window.arrayPerf.active, start = performance.now();
      if (active) window.arrayPerf.frames++;
      try { callback(time); }
      finally { if (active) window.arrayPerf.frameMs.push(performance.now() - start); }
    });
    const html = Object.getOwnPropertyDescriptor(Element.prototype, "innerHTML");
    Object.defineProperty(Element.prototype, "innerHTML", { ...html, set(value) {
      if (window.arrayPerf.active && this.matches("[data-tube-sketch-array-preview]")) window.arrayPerf.previewWrites++;
      if (window.cadMovePerf.active && this.matches("[data-tube-sketch-cad-preview]")) window.cadMovePerf.hostWrites++;
      html.set.call(this, value);
    } });
  });
  await page.route("http://sketch-cad.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset='utf-8'><body><div id='app'></div></body>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    const code = readFileSync(path, "utf8");
    assets.set(pathname, createHash("sha256").update(code).digest("hex"));
    return route.fulfill({ contentType: "text/javascript", body: code });
  });
  await page.goto("http://sketch-cad.test/");
  await page.evaluate(async () => {
    console.log("CAD fixture loading production modules");
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    console.log("CAD fixture imported sketchArea");
    const standard = await import("/src/apps/tube-designer/webpage/nestingStandardPart.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML = `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif;background:#13252d;color:#e4f2ee}.tube-section-sketch-body>aside{height:330px;overflow:auto}.tube-sketch-preview-panel{height:540px}.tube-sketch-property-body{height:270px;overflow:auto}.fixture-three-canvas{width:100%;height:100%}</style>`;
    const profile = { id: "rect", name: "矩形管", width: 40, depth: 20,
      contours: [{ kind: "polygon", points: [[-20, -10], [20, -10], [20, 10], [-20, 10]] }] };
    const reference = { available: true, profile, length: 300,
      unfolding: { available: true, method: "section-arc-length", surfaces: [{ inner: false, uPeriod: 120,
        rectangle: { available: true, width: 300, height: 120 },
        wires: [40, 60, 100].map(u => ({ role: "profile-junction", points: [[0, u], [300, u]] })) }] },
      preview: { baseGeometry: { url: "neutral", version: 1 }, baseBounds: { min: [0, -20, -10], max: [300, 20, 10] } } };
    const mount = document.querySelector("#app"), calls = [], loggedErrors = [];
    const view = { activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [] } },
      tubeDesignerSystemProfiles: [profile], tubeDesignerUserData: { profiles: [] } };
    let pendingPreview = null;
    const check = window.check = { view, calls, loggedErrors, renders: 0, holdPreview: false,
      draft() { const s = view.tubeDesignerSketch; return s.sideByPart[s.targetPartId]; },
      resolvePreview() { pendingPreview?.(); pendingPreview = null; } };
    const context = { mount, sceneProxy: { resources: {}, async invoke(method, payload) {
      calls.push({ method, payload: structuredClone(payload) });
      if (method === "TubeDesigner.ReleaseNestingSideSketchPreview") return {};
      if (method === "TubeDesigner.AddNestingSideSketchPart") return { partEntityId: "cad-saved", tubeDesigner: { nestingGroups: [{ id: "cad-group", parts: [{ entityId: "cad-saved", name: "CAD 保存", independentNesting: true }] }] } };
      if (method === "TubeDesigner.PreviewNestingSideSketchPart") {
        if (payload.sketch && check.holdPreview) return new Promise(resolve => {
          pendingPreview = () => resolve({ ...reference, preview: { ...reference.preview,
            baseGeometry: { url: "cut", version: calls.length } } });
        });
        return reference;
      }
      throw Error(`Unexpected native call ${method}`);
    } }, actions: { async selectRibbonTab() {}, async refreshActiveSceneState() {} } };
    const ops = { renderProject: render, showNotice() {}, createSideSketchViewport() {
      const root = document.createElement("div"), canvas = document.createElement("canvas");
      root.style.cssText = "position:absolute;inset:0"; canvas.className = "fixture-three-canvas"; root.append(canvas);
      return { root, mount(host) { host.append(root); }, setStandardView() {}, resize() {}, fitViewToViewport() {},
        async applyViewSnapshot(snapshot) { return { applied: true, entityIds: snapshot.rows.map(row => row.entityId) }; },
        retainViewResources() {}, setVisibleEntityIds() {}, dispose() { root.remove(); } };
    } };
    function render() {
      check.renders++;
      if (view.tubeDesignerSketchDialogOpen && sketch.patchSketchDialogDom(context, view, mount, ops)) return;
      mount.innerHTML = standard.renderNestingStandardPartDialog(view) + sketch.renderSectionSketchDialog(context, view);
      sketch.rememberSketchDialogDom(view, mount, context.sceneProxy);
      standard.rememberNestingStandardPartDom(view, mount, context.sceneProxy);
      sketch.attachSketchAreaInteractions(context, view, mount, ops);
    }
    function dispatchAction(action, target) {
      const operation = action.startsWith("tube-designer-sketch-")
        ? sketch.handleSketchAreaAction(context, view, action, target, ops)
        : standard.handleNestingStandardPartAction(context, view, action, target, ops);
      void Promise.resolve(operation).catch(error => { loggedErrors.push(error.message); view.error = error.message; render(); });
    }
    mount.addEventListener("click", event => {
      const target = event.target.closest("[data-cam-action]");
      if (target) dispatchAction(target.dataset.camAction, target);
    });
    mount.addEventListener("change", event => {
      const target = event.target.closest("[data-cam-change-action]");
      if (target) dispatchAction(target.dataset.camChangeAction, target);
    });
    Object.assign(check, { sketch, standard, context, ops, render });
    context.productProxy = { async invoke(method, payload) {
      calls.push({ method, payload: structuredClone(payload) });
      if (method === "TubeDesigner.ReadSketchDxf") return { content: "0\nSECTION\n2\nENTITIES\n0\nCIRCLE\n10\n63\n20\n155\n40\n3\n0\nLINE\n10\n70\n20\n150\n11\n80\n21\n156\n0\nENDSEC\n0\nEOF\n" };
      if (method === "TubeDesigner.ExportSketchDxf") return { targetPath: payload.targetPath };
      if (method === "TubeDesigner.GenerateSketchTextOutline") return { bOK: true, entities: [
        { kind: "circle", cx: payload.x, cy: payload.y, radius: 6, closed: true, fillGroup: "text-0", fillRule: "evenodd" },
        { kind: "circle", cx: payload.x, cy: payload.y, radius: 3, closed: true, fillGroup: "text-0", fillRule: "evenodd" },
      ] };
      throw Error(`Unexpected product native call ${method}`);
    } };
    context.appProxy = { bridge: { async openFileDialog() { return "D:/fixture/input.dxf"; }, async saveFileDialog() { return "D:/fixture/output.dxf"; } } };
    check.renderedContour = (id, preview = false) => {
      const svg = document.querySelector("[data-tube-sketch-canvas]");
      const copy = preview ? svg.querySelector('[data-tube-sketch-array-copy="0"]') : null;
      const host = copy ? document.getElementById(copy.getAttribute("href").slice(1)) : svg.querySelector(".tube-sketch-geometry");
      const element = host.querySelector(`[data-tube-sketch-entity-id="${id}"]`);
      if (!element?.getTotalLength) throw Error(`Missing renderable geometry ${id}`);
      const transform = copy ? copy.getScreenCTM().multiply(svg.getScreenCTM().inverse()).multiply(element.getScreenCTM()) : element.getScreenCTM();
      const length = element.getTotalLength();
      const points = Array.from({ length: 1025 }, (_, index) => {
        const point = element.getPointAtLength(length * index / 1024), transformed = new DOMPoint(point.x, point.y).matrixTransform(transform);
        return [transformed.x, transformed.y];
      });
      return { points, bounds: [Math.min(...points.map(p => p[0])), Math.min(...points.map(p => p[1])),
        Math.max(...points.map(p => p[0])), Math.max(...points.map(p => p[1]))] };
    };
    await standard.handleNestingStandardPartRibbonCommand(context, view, "nesting.draw-2d-part", ops);
    console.log("CAD fixture created standard part dialog");
  });
  console.log("CAD fixture initialized");
  await page.getByLabel("长度（mm）", { exact: true }).fill("300");
  await page.getByRole("button", { name: "开始绘制", exact: true }).click();
  await page.getByRole("dialog", { name: "二维绘制零件", exact: true }).waitFor();
  await page.locator("[data-side-sketch-preview-ready=true]").waitFor();
  const selectSources = async entities => page.evaluate(entities => {
    const c = window.check, d = c.draft(), s = c.view.tubeDesignerSketch;
    d.entities = structuredClone(entities); d.selectedIds = entities.map(entity => entity.id);
    d.arrays = []; d.endCuts = undefined; d.splitParts = false; d.selectedPoints = []; d.history = []; d.future = []; d.dirty = false;
    s.command = null; s.tool = "select"; s.snapEnabled = false; s.orthoEnabled = false;
    s.sideViewport = { offsetU: 0, zoom: 1 }; c.view.error = ""; c.render();
  }, entities);
  const field = name => page.locator(`[data-tube-sketch-cad-field="${name}"]`);
  const setField = async (name, value) => {
    const input = field(name);
    if ((await input.evaluate(node => node.tagName)) === "SELECT") await input.selectOption(String(value));
    else await input.fill(String(value));
    await input.dispatchEvent("change");
  };
  const enter = name => page.locator(`[data-sketch-command="sketch.${name}"]`).click();
  const apply = () => page.locator('[data-cam-action="tube-designer-sketch-cad-apply"]').click();
  const cancel = () => page.locator('[data-cam-action="tube-designer-sketch-cad-cancel"]').click();
  const entities = () => page.evaluate(() => window.check.draft().entities);
  const draft = () => page.evaluate(() => window.check.draft());
  const state = () => page.evaluate(() => window.check.view.tubeDesignerSketch);
  const snapshot = () => page.evaluate(() => JSON.stringify(window.check.draft().entities));
  const idle = () => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const modelPoint = (x, y) => page.locator("[data-tube-sketch-canvas]").evaluate((svg, [x, y]) => {
    const value = name => Number(svg.getAttribute(`data-sketch-${name}`));
    const point = svg.createSVGPoint();
    point.x = value("canvas-left") + (x - value("x-min")) / (value("x-max") - value("x-min")) * (value("canvas-right") - value("canvas-left"));
    point.y = value("canvas-bottom") - (y - value("y-min")) / (value("y-max") - value("y-min")) * (value("canvas-bottom") - value("canvas-top"));
    const client = point.matrixTransform(svg.getScreenCTM()); return [client.x, client.y];
  }, [x, y]);
  const clickModel = async (x, y) => page.mouse.click(...await modelPoint(x, y));
  const moveModel = async (x, y) => { await page.mouse.move(...await modelPoint(x, y)); await idle(); };
  const near = (actual, expected, label = "coordinate", tolerance = 1e-6) => assert.ok(Math.abs(actual - expected) < tolerance, `${label}: ${actual} != ${expected}`);
  const noCadError = async () => { const message = page.locator("[data-tube-sketch-cad-error]"); if (await message.count()) assert.equal(await message.innerText(), ""); };
  const sources = [{ id: "circle", kind: "circle", cx: 20, cy: 90, radius: 4, closed: true }, { id: "line", kind: "line", x1: 34, y1: 90, x2: 44, y2: 98, closed: false }];

  // The developed CAD plane uses a single physical mm-to-pixel scale. A circle
  // remains circular at every zoom and pan, and zoom stays under the cursor.
  await selectSources([{ id: "aspect-circle", kind: "circle", cx: 30, cy: 150, radius: 10, closed: true }]);
  const circleRatio = () => page.locator('.tube-sketch-geometry [data-tube-sketch-entity-id="aspect-circle"]').first().evaluate(node => {
    const box = node.getBoundingClientRect(); return { width: box.width, height: box.height, ratio: box.width / box.height };
  });
  const initialAspect = await circleRatio(); near(initialAspect.ratio, 1, "initial actual circle screen aspect", 1e-5);
  const anchor = await modelPoint(35, 155), initialNative = await page.evaluate(() => window.check.calls.length);
  await page.mouse.move(...anchor); await page.mouse.wheel(0, -180); await idle();
  const zoomAspect = await circleRatio(); near(zoomAspect.ratio, 1, "zoomed actual circle screen aspect", 1e-5); assert.ok(zoomAspect.width > initialAspect.width);
  const zoomAnchor = await modelPoint(35, 155); near(zoomAnchor[0], anchor[0], "zoom U is cursor anchored", 0.2); near(zoomAnchor[1], anchor[1], "zoom S is cursor anchored", 0.2);
  await page.mouse.move(...zoomAnchor); await page.mouse.down({ button: "middle" }); await page.mouse.move(zoomAnchor[0] + 45, zoomAnchor[1] + 35, { steps: 4 }); await page.mouse.up({ button: "middle" }); await idle();
  const panAspect = await circleRatio(); near(panAspect.ratio, 1, "panned actual circle screen aspect", 1e-5);
  const panAnchor = await modelPoint(35, 155); near(panAnchor[0], zoomAnchor[0] + 45, "U pans with pointer", 0.2); near(panAnchor[1], zoomAnchor[1] + 35, "S pans with pointer", 0.2);
  assert.equal(await page.evaluate(() => window.check.calls.length), initialNative);
  results.push({ case: "equal mm scale circle stays circular through cursor zoom and two-axis pan", initialAspect, zoomAspect, panAspect, nativeCalls: 0 });

  for (const kind of ["move", "copy", "rotate", "mirror", "scale", "align"]) {
    await selectSources(sources); await enter(kind);
    if (["move", "copy"].includes(kind)) { await setField("dx", 10); await setField("dy", -7); }
    if (kind === "rotate") { await setField("centerX", 0); await setField("centerY", 0); await setField("angle", 90); }
    if (kind === "mirror") { await setField("x1", 0); await setField("y1", 0); await setField("x2", 0); await setField("y2", 100); }
    if (kind === "scale") { await setField("centerX", 0); await setField("centerY", 0); await setField("factor", 2); }
    if (kind === "align") await setField("direction", "left");
    assert.equal(await snapshot(), JSON.stringify(sources), "preview cannot mutate source geometry");
    await apply(); await noCadError();
    const output = await entities();
    assert.equal(output.length, kind === "copy" ? 4 : 2, `${kind} operation count`);
    const circle = output.filter(entity => entity.kind === "circle").at(-1);
    if (["move", "copy"].includes(kind)) { near(circle.cx, 30); near(circle.cy, 83); }
    if (kind === "rotate") { near(circle.cx, -90); near(circle.cy, 20); }
    if (kind === "mirror") { near(circle.cx, -20); near(circle.cy, 90); }
    if (kind === "scale") { near(circle.cx, 40); near(circle.cy, 180); near(circle.radius, 8); }
    assert.equal((await draft()).history.length, 1, `${kind} uses one undo transaction`);
    await enter("undo"); assert.deepEqual(await entities(), sources);
    await enter("redo"); assert.deepEqual(await entities(), output);
    results.push({ case: `parameter ${kind}`, entities: output.length, undoTransactions: 1 });
  }

  await selectSources(sources); await enter("move");
  await page.locator('[data-cam-action="tube-designer-sketch-cad-mode"][data-cad-mode="mouse"]').click();
  await clickModel(20, 90);
  const beforeNative = await page.evaluate(() => window.check.calls.length);
  await moveModel(27, 101);
  assert.equal(await snapshot(), JSON.stringify(sources));
  assert.ok(await page.locator("[data-tube-sketch-cad-preview] use").count() > 0, "transform preview reuses exact source SVG");
  assert.equal(await page.evaluate(() => window.check.calls.length), beforeNative);
  await page.evaluate(() => {
    const host = document.querySelector("[data-tube-sketch-cad-preview]");
    window.cadMoveNodes = { host, source: host.querySelector("defs > g"), instance: host.querySelector("use") };
    window.cadMovePerf = { active: true, hostWrites: 0 };
  });
  for (let i = 0; i < 10; i++) await moveModel(27 + i / 10, 101 + i / 10);
  const mousePerf = await page.evaluate(() => {
    window.cadMovePerf.active = false;
    const { host, source, instance } = window.cadMoveNodes;
    return { hostWrites: window.cadMovePerf.hostWrites, sourceIdentity: source === host.querySelector("defs > g"), instanceIdentity: instance === host.querySelector("use"), nativeCalls: window.check.calls.length };
  });
  assert.equal(mousePerf.hostWrites, 0, "warmed pointer movement never replaces CAD preview innerHTML");
  assert.equal(mousePerf.sourceIdentity, true); assert.equal(mousePerf.instanceIdentity, true); assert.equal(mousePerf.nativeCalls, beforeNative);
  results.push({ case: "CAD pointer preview updates only transform across ten frames", ...mousePerf });
  await clickModel(27, 101); await noCadError();
  near((await entities())[0].cx, 27, "mouse move U", 0.01); near((await entities())[0].cy, 101, "mouse move S", 0.01);
  results.push({ case: "mouse move exact preview", nativeCalls: 0, sourceMutationBeforeApply: false });

  for (const shape of ["racetrack", "polygon", "star"]) {
    await selectSources([]); await enter(shape);
    if (shape === "racetrack") { await setField("width", 32); await setField("height", 14); }
    else { await setField("centerX", 30); await setField("centerY", 140); await setField("outerRadius", 12); }
    await apply(); await noCadError();
    assert.equal((await entities()).length, 1); assert.equal((await entities())[0].closed, true);
    if (shape === "racetrack") assert.ok((await entities())[0].segments.some(segment => segment.kind === "circleArc"));
    results.push({ case: `precise ${shape}` });
  }

  await selectSources([]); await enter("line");
  const numeric = name => page.locator(`[data-tube-sketch-numeric-field="${name}"]`);
  const nset = async (name, value) => { if (name === "mode") await numeric(name).selectOption(value); else await numeric(name).fill(String(value)); await numeric(name).dispatchEvent("change"); };
  await nset("x", 10); await nset("y", 120); await page.locator('[data-cam-action="tube-designer-sketch-numeric-apply"]').click();
  await nset("mode", "polar"); await nset("length", 20); await nset("angle", 90); await page.locator('[data-cam-action="tube-designer-sketch-numeric-apply"]').click();
  const drawnLine = (await entities())[0]; near(drawnLine.x1, 10); near(drawnLine.y1, 120); near(drawnLine.x2, 10); near(drawnLine.y2, 140);
  await selectSources([]); await enter("ellipse"); await clickModel(40, 100); await clickModel(50, 120);
  const ellipse = (await entities())[0]; assert.equal(ellipse.kind, "ellipse"); near(ellipse.radiusX, 10, "ellipse radius U", 0.05); near(ellipse.radiusY, 20, "ellipse radius S", 0.05);
  results.push({ case: "absolute and polar precise line plus interactive ellipse" });

  await selectSources([]); await enter("measure"); await clickModel(20, 100); await moveModel(23, 104); await clickModel(23, 104);
  assert.match(await page.locator("[data-tube-sketch-measure-result]").innerText(), /5/); await cancel();
  await selectSources(sources); await page.locator("[data-tube-sketch-canvas]").press("ArrowRight"); near((await entities())[0].cx, 21);
  await page.locator("[data-tube-sketch-canvas]").press("Shift+ArrowUp"); near((await entities())[0].cy, 100);
  results.push({ case: "measure and precise keyboard nudge" });

  await selectSources([{ id: "offset-circle", kind: "circle", cx: 30, cy: 150, radius: 5, closed: true }]); await enter("offset"); await setField("distance", 2); await apply(); await noCadError();
  assert.equal((await entities()).length, 2); near((await entities())[1].radius, 7);
  const corner = [{ id: "corner-a", kind: "line", x1: 20, y1: 150, x2: 40, y2: 150 }, { id: "corner-b", kind: "line", x1: 40, y1: 150, x2: 40, y2: 170 }];
  await selectSources(corner); await enter("fillet"); await setField("radius", 3); await apply(); await noCadError();
  assert.ok((await entities()).flatMap(entity => entity.segments ?? [entity]).some(segment => segment.kind === "circleArc"));
  results.push({ case: "circle offset and general corner fillet" });

  await selectSources([{ id: "offset-ellipse", kind: "ellipse", cx: 35, cy: 150, radiusX: 8, radiusY: 5, closed: true }]);
  await enter("offset"); await setField("distance", 1.5); await apply(); await noCadError();
  let curves = await entities(); assert.equal(curves.length, 2);
  assert.ok(curves[1].closed && curves[1].segments.length > 1 && curves[1].segments.every(segment => segment.kind === "bezier"));
  await enter("undo"); assert.equal((await entities()).length, 1); await enter("redo"); assert.equal((await entities()).length, 2);
  await selectSources([{ id: "offset-spline", kind: "spline", points: [[20, 120], [30, 140], [45, 125], [55, 150]], closed: false }]);
  await enter("offset"); await setField("distance", 1); await apply(); await noCadError();
  curves = await entities(); assert.equal(curves.length, 2); assert.ok(curves[1].segments.every(segment => segment.kind === "bezier"));
  results.push({ case: "editable ellipse and spline offsets with undo redo" });

  const arcCorner = [{ id: "arc-side", kind: "circleArc", cx: 30, cy: 140, radius: 10, startAngle: Math.PI, sweep: -Math.PI / 2 },
    { id: "line-side", kind: "line", x1: 30, y1: 150, x2: 30, y2: 175 }];
  await selectSources(arcCorner); await enter("fillet"); await setField("radius", 2); await apply(); await noCadError();
  curves = await entities(); assert.ok(curves.flatMap(entity => entity.segments ?? [entity]).filter(segment => segment.kind === "circleArc").length >= 2);
  results.push({ case: "curve to line tangent fillet retains original arc" });

  const trims = [{ id: "trim-line", kind: "line", x1: 10, y1: 150, x2: 90, y2: 150 }, { id: "cut-a", kind: "line", x1: 30, y1: 130, x2: 30, y2: 170 }, { id: "cut-b", kind: "line", x1: 60, y1: 130, x2: 60, y2: 170 }];
  await selectSources(trims); await page.evaluate(() => { window.check.draft().selectedIds = ["cut-a", "cut-b"]; window.check.render(); }); await enter("trim"); await clickModel(45, 150); await noCadError();
  assert.equal((await entities()).filter(entity => entity.id.startsWith("cut-")).length, 2);
  assert.ok((await entities()).length >= 4); await cancel();
  await selectSources([{ id: "ext", kind: "line", x1: 20, y1: 150, x2: 45, y2: 150 }, { id: "bound", kind: "line", x1: 60, y1: 130, x2: 60, y2: 170 }]);
  await page.evaluate(() => { window.check.draft().selectedIds = ["bound"]; window.check.render(); }); await enter("extend"); await clickModel(44.8, 150); await noCadError();
  const extended = (await entities()).find(entity => entity.id === "ext"); near(extended.x2, 60); await cancel();
  results.push({ case: "trim middle region and extend selected endpoint" });

  const duplicate = [{ id: "dup-a", kind: "circle", cx: 30, cy: 140, radius: 5, closed: true }, { id: "dup-b", kind: "circle", cx: 30, cy: 140, radius: 5, closed: true }];
  await selectSources(duplicate); await enter("diagnose");
  assert.ok(await page.locator('[data-cam-action="tube-designer-sketch-cad-locate"]').count() > 0); await page.locator('[data-cam-action="tube-designer-sketch-cad-locate"]').first().click();
  assert.ok((await draft()).selectedIds.length > 0); await cancel();
  await selectSources(duplicate); await enter("repair"); await apply(); await noCadError(); assert.equal((await entities()).length, 1);
  results.push({ case: "diagnose duplicate locate and batch repair" });

  await selectSources([{ id: "overlap-long", kind: "line", x1: 20, y1: 150, x2: 60, y2: 150 },
    { id: "overlap-short", kind: "line", x1: 40, y1: 150, x2: 20, y2: 150 }]);
  await enter("repair"); await field("joinGaps").uncheck(); await apply(); await noCadError();
  curves = await entities(); assert.equal(curves.length, 1); assert.equal(curves[0].kind, "line");
  results.push({ case: "remove partial reversed line overlap" });

  await selectSources([{ id: "split-bezier", kind: "path", closed: false, segments: [
    { kind: "bezier", points: [[20, 120], [20, 125], [25, 127.5], [30, 130]] },
    { kind: "bezier", points: [[30, 130], [35, 132.5], [40, 135], [40, 140]] }] }]);
  await enter("repair"); await apply(); await noCadError();
  curves = await entities(); assert.equal(curves[0].segments.length, 1); assert.equal(curves[0].segments[0].kind, "bezier");
  results.push({ case: "simplify split Bezier without changing editable curve" });

  const bow = { id: "bow-crossing", kind: "polyline", closed: true, points: [[20, 120], [40, 140], [20, 140], [40, 120]] };
  await selectSources([bow]); await enter("repair");
  assert.equal(await field("trimSelfIntersections").isChecked(), false, "Self crossing clipping is explicitly selected");
  await field("trimSelfIntersections").check(); await apply(); await noCadError();
  curves = await entities(); assert.equal(curves.length, 2); assert.ok(curves.every(entity => entity.closed));
  assert.equal(new Set(curves.map(entity => entity.id)).size, 2);
  await enter("undo"); assert.deepEqual(await entities(), [bow]); await enter("redo"); assert.equal((await entities()).length, 2);
  await enter("diagnose"); await apply(); await noCadError();
  assert.ok(!(await state()).command.issues.some(issue => issue.type === "self-intersection")); await cancel();
  results.push({ case: "explicit self crossing clip splits closed loops and preserves undo" });

  await selectSources(sources); await enter("import"); await page.waitForFunction(() => !window.check.view.pending);
  assert.equal((await entities()).length, 4, "DXF import appends all figures and open line");
  await enter("export"); await page.waitForFunction(() => !window.check.view.pending);
  const exported = await page.evaluate(() => window.check.calls.findLast(call => call.method === "TubeDesigner.ExportSketchDxf"));
  assert.match(exported.payload.content, /CIRCLE/); assert.match(exported.payload.content, /LINE/);
  await enter("text-outline"); await setField("text", "O"); await setField("x", 60); await setField("y", 140); await apply(); await page.waitForFunction(() => !window.check.view.pending); await noCadError();
  const outlines = (await entities()).filter(entity => entity.fillRule === "evenodd"); assert.equal(outlines.length, 2); assert.equal(outlines[0].fillGroup, outlines[1].fillGroup);
  await enter("text-outline"); await apply(); await page.waitForFunction(() => !window.check.view.pending);
  const groups = (await entities()).filter(entity => entity.fillRule === "evenodd").map(entity => entity.fillGroup); assert.equal(new Set(groups).size, 2, "separate text operations do not share fill groups");
  results.push({ case: "DXF append export and exact text outline integration", nativeTransport: "mocked", retainedHoleGroups: 2 });

  await selectSources(sources); await enter("end-cuts"); await setField("start", 10); await setField("end", 290); await setField("startAngle", 12); await apply();
  assert.deepEqual((await draft()).endCuts.start, { position: 10, angleDegrees: 12, rotationDegrees: 0 }); await enter("split-parts"); await field("splitParts").check(); await apply(); assert.equal((await draft()).splitParts, true);
  await enter("undo"); assert.equal((await draft()).splitParts, false); assert.equal((await draft()).endCuts.start.position, 10);
  results.push({ case: "end cuts and real-solid split configuration undo" });

  await selectSources([sources[0]]); await enter("array-circumferential"); await setField("count", 6); await setField("rows", 2); await setField("spacingY", 40); await apply(); await noCadError();
  assert.equal((await entities()).length, 12); assert.equal((await draft()).arrays.length, 1);
  await enter("array-edit"); const af = name => page.locator(`[data-tube-sketch-array-field="${name}"]`); await af("columns").fill("4"); await af("columns").dispatchEvent("change"); await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').click();
  assert.equal((await entities()).length, 8); assert.equal((await draft()).arrays.length, 1);
  near((await draft()).arrays[0].spec.spacingX, 30, "editing circumferential quantity recomputes equal spacing");
  await selectSources([sources[0]]); await enter("array-rectangular");
  await af("spacingX").fill("10"); await af("spacingX").dispatchEvent("change"); await af("spacingY").fill("20"); await af("spacingY").dispatchEvent("change");
  await page.locator('[data-tube-sketch-array-mode="fill"]').click(); await clickModel(10, 100); await moveModel(42, 145); await clickModel(42, 145);
  assert.ok((await entities()).length > 6); results.push({ case: "circumferential fill and associative array editing" });

  await selectSources(sources); await enter("move"); await setField("dx", 20); await setField("dy", 30);
  await page.evaluate(() => { window.check.holdPreview = true; }); await page.getByRole("button", { name: "预览三维", exact: true }).click();
  const retention = await page.evaluate(async () => {
    const c = window.check, svg = document.querySelector("[data-tube-sketch-canvas]"), canvas = document.querySelector(".fixture-three-canvas"), input = document.querySelector('[data-tube-sketch-cad-field="dx"]');
    let probes = 0, inputProbes = 0; svg.addEventListener("cad-node-probe", () => probes++); input.addEventListener("cad-input-probe", () => inputProbes++);
    input.focus({ preventScroll: true }); input.value = "23.75"; input.dispatchEvent(new Event("input", { bubbles: true })); input.setSelectionRange(1, 4, "forward");
    const panes = [...document.querySelectorAll(".tube-section-sketch-body>aside")], nested = document.querySelector(".tube-sketch-property-body");
    panes[0].scrollTop = 61; panes[1].scrollTop = 43; nested.scrollTop = 37;
    const positions = [panes[0].scrollTop, panes[1].scrollTop, nested.scrollTop]; c.resolvePreview(); await new Promise(resolve => setTimeout(resolve, 30));
    const next = document.querySelector('[data-tube-sketch-cad-field="dx"]'); svg.dispatchEvent(new Event("cad-node-probe")); next.dispatchEvent(new Event("cad-input-probe"));
    return { sameSvg: svg === document.querySelector("[data-tube-sketch-canvas]"), sameCanvas: canvas === document.querySelector(".fixture-three-canvas"), sameInput: input === next, focused: document.activeElement === next, value: next.value, selection: [next.selectionStart, next.selectionEnd], positions, actualPositions: [panes[0].scrollTop, panes[1].scrollTop, nested.scrollTop], probes, inputProbes, pending: c.view.tubeDesignerSketch.sidePreviewPending };
  });
  assert.equal(retention.sameSvg, true); assert.equal(retention.sameCanvas, true); assert.equal(retention.sameInput, true); assert.equal(retention.focused, true); assert.equal(retention.value, "23.75"); assert.deepEqual(retention.selection, [1, 4]); assert.deepEqual(retention.actualPositions, retention.positions); assert.equal(retention.probes, 1); assert.equal(retention.inputProbes, 1); assert.equal(retention.pending, false); assert.ok(retention.positions.every(position => position > 0));
  results.push({ case: "late preview CAD focus selection and dual-sidebar nested scroll retention", ...retention });
  await cancel();
  await selectSources(sources); await enter("move"); await setField("dx", 3);
  await page.screenshot({ path: resolve(output, "sketch-cad-browser.png"), fullPage: true });
  const savesBeforeCancel = await page.evaluate(() => window.check.calls.filter(call => call.method === "TubeDesigner.AddNestingSideSketchPart").length);
  await enter("cancel");
  await page.getByRole("dialog", { name: "二维绘制零件", exact: true }).waitFor({ state: "detached" });
  assert.equal(await page.evaluate(() => window.check.calls.filter(call => call.method === "TubeDesigner.AddNestingSideSketchPart").length), savesBeforeCancel, "global cancel closes editor without applying the active tool");
  await page.evaluate(async () => { const c = window.check; await c.standard.handleNestingStandardPartRibbonCommand(c.context, c.view, "nesting.draw-2d-part", c.ops); });
  await page.getByLabel("长度（mm）", { exact: true }).fill("300"); await page.getByRole("button", { name: "开始绘制", exact: true }).click();
  await page.getByRole("dialog", { name: "二维绘制零件", exact: true }).waitFor(); await page.locator("[data-side-sketch-preview-ready=true]").waitFor();
  await selectSources(sources); await enter("move"); await setField("dx", 3); await setField("dy", 4);
  await enter("commit");
  await page.getByRole("dialog", { name: "二维绘制零件", exact: true }).waitFor({ state: "detached" });
  const saves = await page.evaluate(() => window.check.calls.filter(call => call.method === "TubeDesigner.AddNestingSideSketchPart"));
  assert.equal(saves.length, savesBeforeCancel + 1, "global confirm applies active CAD operation and saves exactly once");
  near(saves.at(-1).payload.sketch.entities[0].cx, 23); near(saves.at(-1).payload.sketch.entities[0].cy, 94);
  results.push({ case: "global confirm applies tool and saves exits once while global cancel exits without save", saves: 1 });
  assert.equal(errors.length, 0, errors.join("\n")); assert.equal(await page.evaluate(() => window.check.loggedErrors.length), 0);
  await page.screenshot({ path: resolve(output, "sketch-cad-editor-closed.png"), fullPage: true });
  writeFileSync(resolve(output, "report.json"), JSON.stringify({ nativeTransport: "mocked", browser: "Chrome", productionAssets: Object.fromEntries(assets), results, pageErrors: errors }, null, 2));
  console.log(`Sketch CAD browser regression passed (${results.length} scenarios); native transport mocked.`);
} finally { await browser.close(); }
