// Real Chrome interaction coverage for sketch arrays. Native preview/persistence
// transport is deliberately mocked; this test checks production SVG and DOM.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const repository = fileURLToPath(new URL("../../../", import.meta.url));
const root = resolve(repository, "src");
const output = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repository, "output/tests/sketch-array-browser"));
mkdirSync(output, { recursive: true });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome" });
const results = [];
const assets = new Map();
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.addInitScript(() => {
    const originalClone = window.structuredClone;
    window.arrayPerf = { active: false, clones: 0, frames: 0, frameMs: [], previewWrites: 0 };
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
      html.set.call(this, value);
    } });
  });
  await page.route("http://sketch-array.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset='utf-8'><body><div id='app'></div></body>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    const code = readFileSync(path, "utf8");
    assets.set(pathname, createHash("sha256").update(code).digest("hex"));
    return route.fulfill({ contentType: "text/javascript", body: code });
  });
  await page.goto("http://sketch-array.test/");
  await page.evaluate(async () => {
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
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
    Object.assign(check, { sketch, context, ops, render });
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
  });
  await page.getByLabel("长度（mm）", { exact: true }).fill("300");
  await page.getByRole("button", { name: "开始绘制", exact: true }).click();
  await page.getByRole("dialog", { name: "二维绘制零件", exact: true }).waitFor();
  await page.locator("[data-side-sketch-preview-ready=true]").waitFor();
  const selectSources = async entities => page.evaluate(entities => {
    const c = window.check, d = c.draft(), s = c.view.tubeDesignerSketch;
    d.entities = structuredClone(entities); d.selectedIds = entities.map(entity => entity.id);
    d.selectedPoints = []; d.history = []; d.future = []; d.dirty = false;
    s.command = null; s.tool = "select"; s.snapEnabled = false; s.orthoEnabled = false;
    s.sideViewport = { offsetU: 0, zoom: 1 }; c.view.error = ""; c.render();
  }, entities);
  const field = name => page.locator(`[data-tube-sketch-array-field="${name}"]`);
  const setField = async (name, value) => { await field(name).fill(String(value)); await field(name).dispatchEvent("change"); };
  const enter = async kind => page.locator(`[data-sketch-command="sketch.array-${kind}"]`).click();
  const apply = () => page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').click();
  const cancel = () => page.locator('[data-cam-action="tube-designer-sketch-array-cancel"]').click();
  const entities = () => page.evaluate(() => window.check.draft().entities);
  const snapshot = () => page.evaluate(() => JSON.stringify(window.check.draft().entities));
  const idle = () => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const modelPoint = async (x, y) => page.locator("[data-tube-sketch-canvas]").evaluate((svg, [x, y]) => {
    const value = name => Number(svg.getAttribute(`data-sketch-${name}`));
    const point = svg.createSVGPoint();
    point.x = value("canvas-left") + (x - value("x-min")) / (value("x-max") - value("x-min")) * (value("canvas-right") - value("canvas-left"));
    point.y = value("canvas-bottom") - (y - value("y-min")) / (value("y-max") - value("y-min")) * (value("canvas-bottom") - value("canvas-top"));
    const client = point.matrixTransform(svg.getScreenCTM()); return [client.x, client.y];
  }, [x, y]);
  const clickModel = async (x, y, options) => page.mouse.click(...await modelPoint(x, y), options);
  const moveModel = async (x, y) => { await page.mouse.move(...await modelPoint(x, y)); await idle(); };
  const near = (actual, expected, message, tolerance = 1e-7) => assert.ok(Math.abs(actual - expected) < tolerance, `${message}: ${actual} vs ${expected}`);
  const sources = [
    { id: "circle", kind: "circle", cx: 20, cy: 90, radius: 4, closed: true },
    { id: "arc", kind: "circleArc", cx: 35, cy: 90, radius: 3, startAngle: 0.2, sweep: 2.1, rotation: 0.4 },
  ];

  // One transaction copies the entire multi-selection with signed U/S spacing.
  await selectSources(sources);
  await enter("rectangular");
  await setField("columns", 3); await setField("rows", 2);
  await setField("spacingX", -140); await setField("spacingY", 250);
  assert.equal(await snapshot(), JSON.stringify(sources), "A parameter preview cannot mutate the source drawing");
  assert.equal(await page.locator("[data-tube-sketch-array-copy]").count(), 5, "Each SVG instance reuses the entire selected group");
  const periodic = await page.evaluate(() => {
    const svg = document.querySelector("[data-tube-sketch-canvas]");
    const values = matrix => [matrix.a, matrix.b, matrix.c, matrix.d, matrix.e, matrix.f];
    return [...svg.querySelectorAll("[data-tube-sketch-array-periodic-copy]")].map(use => {
      const logical = svg.querySelector(`[data-tube-sketch-array-copy="${use.dataset.tubeSketchArrayInstance}"]`);
      return { turn: Number(use.dataset.tubeSketchArrayPeriodicCopy), instance: Number(use.dataset.tubeSketchArrayInstance),
        actual: values(use.transform.baseVal.consolidate().matrix), original: values(logical.transform.baseVal.consolidate().matrix),
        sameSource: use.getAttribute("href") === logical.getAttribute("href"), periodPixels: 120 * (Number(svg.getAttribute("data-sketch-canvas-right")) - Number(svg.getAttribute("data-sketch-canvas-left"))) / (Number(svg.getAttribute("data-sketch-x-max")) - Number(svg.getAttribute("data-sketch-x-min"))) };
    });
  });
  assert.ok(periodic.length > 0, "Off-screen U copies appear again at visible circumference periods");
  for (const copy of periodic) {
    assert.equal(copy.sameSource, true);
    for (let index = 0; index < 4; index++) near(copy.actual[index], copy.original[index], "Periodic copies retain the rigid instance matrix", 1e-4);
    near(copy.actual[4] - copy.original[4], copy.periodPixels * copy.turn, "One U period translates by one circumference on the current canvas", 1e-3);
    near(copy.actual[5], copy.original[5], "Periodic repeats do not shift the axial coordinate", 1e-4);
  }
  await page.screenshot({ path: resolve(output, "parameter-rectangular-preview.png"), fullPage: true });
  const beforeNative = await page.evaluate(() => window.check.calls.length);
  await apply();
  let copied = await entities();
  assert.equal(copied.length, 12, JSON.stringify(await page.evaluate(() => ({
    loggedErrors: window.check.loggedErrors, command: window.check.view.tubeDesignerSketch.command,
  }))));
  assert.equal(new Set(copied.map(entity => entity.id)).size, copied.length, "Every copied graph has a fresh ID");
  assert.deepEqual(copied.slice(0, 2), sources, "The originals retain their exact geometry and IDs");
  assert.equal(copied.filter(entity => entity.kind === "circleArc").length, 6);
  assert.ok(copied.some(entity => entity.cx < -120), "U remains unbounded beyond multiple circumference periods");
  assert.ok(copied.some(entity => entity.cy > 300), "Axial overflow stays in the raw draft for native clipping");
  assert.equal(await page.evaluate(() => window.check.calls.length), beforeNative, "Array creation is a draft operation without native recutting");
  await page.locator('[data-sketch-command="sketch.undo"]').click();
  assert.deepEqual(await entities(), sources, "One undo removes every copy in the group");
  await page.locator('[data-sketch-command="sketch.redo"]').click();
  assert.deepEqual(await entities(), copied, "One redo restores the same exact geometry and IDs");
  results.push({ case: "parameter rectangular", entities: copied.length, undoTransactions: 1, nativeCalls: 0, visiblePeriodicCopies: periodic.length });

  // A polar copy retains true conics, spline controls and open/closed semantics.
  const curves = [
    { id: "ellipse", kind: "ellipseArc", cx: 35, cy: 150, radiusX: 7, radiusY: 3, rotation: 0.35, startAngle: 0.2, sweep: -2.4 },
    { id: "bezier", kind: "path", segments: [{ kind: "bezier", points: [[40, 151], [43, 159], [49, 142], [53, 151]] }], closed: false },
    { id: "spline", kind: "spline", points: [[32, 162], [38, 166], [45, 160]], closed: false },
  ];
  await selectSources(curves); await enter("polar");
  await setField("count", 4); await setField("angleStep", 90);
  await setField("centerX", 30); await setField("centerY", 150);
  await field("rotateItems").check();
  assert.equal(await snapshot(), JSON.stringify(curves));
  await apply(); copied = await entities();
  assert.equal(copied.length, 12, JSON.stringify(await page.evaluate(() => ({
    loggedErrors: window.check.loggedErrors, command: window.check.view.tubeDesignerSketch.command,
  }))));
  for (const kind of ["ellipseArc", "path", "spline"]) assert.equal(copied.filter(entity => entity.kind === kind).length, 4);
  assert.ok(copied.filter(entity => entity.kind === "path").every(entity => entity.segments[0].kind === "bezier"));
  const ellipse = copied.filter(entity => entity.kind === "ellipseArc")[1];
  near(ellipse.cx, 30, "90-degree ellipse center U"); near(ellipse.cy, 155, "90-degree ellipse center S");
  near(ellipse.radiusX, 7, "Major radius remains exact"); near(ellipse.sweep, -2.4, "Directed arc sweep remains exact");
  near(ellipse.rotation, 0.35 + Math.PI / 2, "Polar copies rotate the native ellipse parameter frame");
  assert.equal(new Set(copied.map(entity => entity.id)).size, 12);
  await page.locator('[data-sketch-command="sketch.undo"]').click(); assert.deepEqual(await entities(), curves);
  await page.locator('[data-sketch-command="sketch.redo"]').click(); assert.deepEqual(await entities(), copied);
  results.push({ case: "parameter polar", preservedKinds: ["ellipseArc", "path/bezier", "spline"], undoTransactions: 1 });

  // Keeping orientation still rotates the selected group as a single rigid group.
  await selectSources(sources); await enter("polar");
  await setField("count", 3); await setField("angleStep", -90);
  await setField("centerX", 0); await setField("centerY", 0); await field("rotateItems").uncheck();
  await apply(); copied = await entities();
  const firstArc = copied.find(entity => entity.id !== "arc" && entity.kind === "circleArc");
  near(firstArc.startAngle, sources[1].startAngle, "Keep-orientation preserves arc start angle");
  near(firstArc.rotation, sources[1].rotation, "Keep-orientation preserves arc rotation");
  const copyCircle = copied.find(entity => entity.id !== "circle" && entity.kind === "circle");
  near(firstArc.cx - copyCircle.cx, 15, "The multi-selection retains its internal horizontal displacement");
  near(firstArc.cy - copyCircle.cy, 0, "The multi-selection retains its internal vertical displacement");
  results.push({ case: "polar keep orientation", groupRemainsRigid: true });

  // Compare actual rendered curve sets before/after committing a rotated copy.
  // The U and S scales differ by more than 5x in this side sketch fixture.
  const contourCases = [
    { id: "shape", kind: "circle", cx: 55, cy: 150, radius: 8, closed: true },
    { id: "shape", kind: "ellipse", cx: 55, cy: 150, radiusX: 10, radiusY: 4, rotation: 0.37, closed: true },
    { id: "shape", kind: "rectangle", x: 48, y: 144, width: 14, height: 12, radius: 3, closed: true },
  ];
  const directedDistance = (from, to) => Math.max(...from.map(point => {
    let distance = Infinity;
    for (let index = 1; index < to.length; index++) {
      const a = to[index - 1], b = to[index], dx = b[0] - a[0], dy = b[1] - a[1];
      const square = dx * dx + dy * dy;
      const t = square ? Math.max(0, Math.min(1, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / square)) : 0;
      distance = Math.min(distance, Math.hypot(point[0] - a[0] - t * dx, point[1] - a[1] - t * dy));
    }
    return distance;
  }));
  for (const source of contourCases) {
    await selectSources([source]); await enter("polar");
    await setField("count", 2); await setField("angleStep", source.kind === "circle" ? 90 : 37);
    await setField("centerX", 30); await setField("centerY", 150);
    const beforeContour = await page.evaluate(() => window.check.renderedContour("shape", true));
    await apply();
    const newId = (await entities()).find(entity => entity.id !== "shape").id;
    const afterContour = await page.evaluate(id => window.check.renderedContour(id), newId);
    const error = Math.max(directedDistance(beforeContour.points, afterContour.points), directedDistance(afterContour.points, beforeContour.points));
    assert.ok(error < 0.15, `${source.kind} preview and committed rendering differ by ${error} screen pixels`);
    for (let index = 0; index < 4; index++) near(beforeContour.bounds[index], afterContour.bounds[index], `${source.kind} true rendered extent`, 0.15);
    results.push({ case: `${source.kind} rotated preview matches committed contour`, maximumScreenError: error });
  }

  // Mouse arrays update only SVG transforms. Forty events share one frame.
  await selectSources(sources); await enter("rectangular");
  await setField("columns", 3); await setField("rows", 2);
  await page.locator('[data-tube-sketch-array-mode="mouse"]').click();
  await clickModel(30, 150);
  const move = await modelPoint(50, 180);
  const performance = await page.evaluate(async ([x, y]) => {
    const c = window.check, svg = document.querySelector("[data-tube-sketch-canvas]");
    c.renders = 0;
    Object.assign(window.arrayPerf, { active: true, clones: 0, frames: 0, frameMs: [], previewWrites: 0 });
    const before = JSON.stringify(c.draft().entities), calls = c.calls.length;
    for (let i = 0; i < 40; i++) svg.dispatchEvent(new PointerEvent("pointermove", {
      bubbles: true, pointerId: 1, pointerType: "mouse", clientX: x - 10 + i * 10 / 39, clientY: y, buttons: 0,
    }));
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    window.arrayPerf.active = false;
    return { ...window.arrayPerf, renders: c.renders, nativeCalls: c.calls.length - calls,
      draftUnchanged: before === JSON.stringify(c.draft().entities) };
  }, move);
  assert.equal(performance.renders, 0); assert.equal(performance.clones, 0); assert.equal(performance.nativeCalls, 0);
  assert.equal(performance.draftUnchanged, true);
  assert.ok(performance.previewWrites <= 2, "A burst repaints at most once per animation frame");
  near(Number(await field("spacingX").inputValue()), 20, "Mouse sets column spacing", 1e-4);
  near(Number(await field("spacingY").inputValue()), 30, "Mouse sets row spacing", 1e-4);
  assert.equal(await page.locator("[data-tube-sketch-array-copy]").count(), 5);
  await clickModel(51, 182); copied = await entities(); assert.equal(copied.length, 12);
  assert.ok(copied.some(entity => entity.kind === "circle" && Math.abs(entity.cx - 62) < 0.01 && Math.abs(entity.cy - 122) < 0.01),
    "The confirmation click uses its latest point instead of an older animation-frame position");
  await page.locator('[data-sketch-command="sketch.undo"]').click(); assert.deepEqual(await entities(), sources);
  results.push({ case: "mouse rectangular", ...performance });

  // A realistic large selection keeps the mouse path independent of copy count.
  const largeSelection = Array.from({ length: 96 }, (_, index) => {
    const x = 18 + index % 12 * 7, y = 45 + Math.floor(index / 12) * 25;
    return index % 3 ? { id: `large-${index}`, kind: "circle", cx: x, cy: y, radius: 2, closed: true }
      : { id: `large-${index}`, kind: "path", segments: [{ kind: "bezier", points: [[x, y], [x + 2, y + 6], [x + 4, y - 6], [x + 6, y]] }], closed: false };
  });
  await selectSources(largeSelection); await enter("rectangular");
  await setField("columns", 10); await setField("rows", 4);
  await page.locator('[data-tube-sketch-array-mode="mouse"]').click(); await clickModel(30, 150); await moveModel(50, 180);
  const largeMove = await modelPoint(52, 181);
  const largePerformance = await page.evaluate(async ([x, y]) => {
    const c = window.check, svg = document.querySelector("[data-tube-sketch-canvas]");
    c.renders = 0;
    Object.assign(window.arrayPerf, { active: true, clones: 0, frames: 0, frameMs: [], previewWrites: 0 });
    const before = JSON.stringify(c.draft().entities), calls = c.calls.length;
    for (let i = 0; i < 40; i++) svg.dispatchEvent(new PointerEvent("pointermove", {
      bubbles: true, pointerId: 1, pointerType: "mouse", clientX: x - 20 + i * 20 / 39, clientY: y, buttons: 0,
    }));
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    window.arrayPerf.active = false;
    return { ...window.arrayPerf, renders: c.renders, nativeCalls: c.calls.length - calls,
      draftUnchanged: before === JSON.stringify(c.draft().entities) };
  }, largeMove);
  assert.equal(largePerformance.renders, 0); assert.equal(largePerformance.clones, 0);
  assert.equal(largePerformance.nativeCalls, 0); assert.equal(largePerformance.draftUnchanged, true);
  assert.ok(largePerformance.previewWrites <= 2);
  await cancel();
  results.push({ case: "96-source 3840-entity array preview", ...largePerformance });

  // The first click chooses the polar center, the second commits its angle.
  const line = [{ id: "line", kind: "line", x1: 50, y1: 150, x2: 60, y2: 150 }];
  await selectSources(line); await enter("polar"); await setField("count", 4);
  await page.locator('[data-tube-sketch-array-mode="mouse"]').click();
  await clickModel(30, 150); await moveModel(30, 180);
  near(Number(await field("angleStep").inputValue()), 90, "Mouse sets the polar angular spacing", 0.01);
  assert.equal(await snapshot(), JSON.stringify(line));
  await page.screenshot({ path: resolve(output, "mouse-polar-preview.png"), fullPage: true });
  await clickModel(30, 180); copied = await entities(); assert.equal(copied.length, 4);
  near(copied[1].x1, 30, "Interactive polar line start U", 0.01); near(copied[1].y1, 170, "Interactive polar line start S", 0.01);
  near(copied[1].x2, 30, "Interactive polar line end U", 0.01); near(copied[1].y2, 180, "Interactive polar line end S", 0.01);
  await page.locator('[data-sketch-command="sketch.undo"]').click(); assert.deepEqual(await entities(), line);
  results.push({ case: "mouse polar", count: copied.length, undoTransactions: 1 });

  // Pan and zoom repaint an in-progress preview in the current coordinate frame.
  await selectSources(sources); await enter("rectangular");
  await setField("spacingX", 20); await setField("spacingY", 30);
  const initialPreview = await page.locator("[data-tube-sketch-array-preview]").innerHTML();
  const center = await modelPoint(60, 150);
  await page.mouse.move(...center); await page.mouse.wheel(0, -200); await idle();
  const zoomed = await page.locator("[data-tube-sketch-array-preview]").innerHTML();
  assert.notEqual(zoomed, initialPreview);
  await page.mouse.down({ button: "middle" }); await page.mouse.move(center[0] + 70, center[1], { steps: 4 }); await page.mouse.up({ button: "middle" }); await idle();
  assert.notEqual(await page.locator("[data-tube-sketch-array-preview]").innerHTML(), zoomed);
  assert.equal(await snapshot(), JSON.stringify(sources));
  assert.equal(await page.locator("[data-tube-sketch-array-copy]").count(), 5);
  await cancel();
  results.push({ case: "active preview zoom and pan", draftUnchanged: true });

  // Every exit path abandons preview copies, with Enter committing only once.
  for (const method of ["cancel", "Escape", "contextmenu"]) {
    await selectSources(sources); await enter("rectangular");
    if (method === "cancel") await cancel();
    else if (method === "Escape") await page.locator("[data-tube-sketch-canvas]").press("Escape");
    else await clickModel(60, 150, { button: "right" });
    assert.equal(await snapshot(), JSON.stringify(sources));
    assert.equal(await page.evaluate(() => window.check.draft().history.length), 0);
    assert.equal(await page.locator("[data-tube-sketch-array-copy]").count(), 0);
  }
  await selectSources(sources); await enter("rectangular");
  await field("spacingX").press("Enter");
  assert.equal((await entities()).length, 12); assert.equal(await page.evaluate(() => window.check.draft().history.length), 1);
  results.push({ case: "cancel Escape right-click and Enter", cancelledWithoutHistory: 3 });

  // Invalid counts/angles cannot create partially applied arrays or native work.
  await selectSources(sources); await enter("rectangular"); await setField("columns", 5000); await setField("rows", 2);
  await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').evaluate(button => button.click());
  assert.equal(await snapshot(), JSON.stringify(sources)); assert.equal(await page.locator("[data-tube-sketch-array-copy]").count(), 0);
  await cancel();
  await enter("rectangular");
  for (const [name, value] of [["columns", "1.5"], ["columns", ""], ["columns", "2"], ["spacingX", "0"]]) {
    await setField(name, value);
    if (!(name === "columns" && value === "2")) {
      assert.equal(await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').isDisabled(), true);
      assert.equal(await snapshot(), JSON.stringify(sources));
    }
  }
  await cancel();
  await enter("polar"); await setField("count", 4); await setField("angleStep", 120);
  assert.equal(await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').isDisabled(), true,
    "A full-turn duplicate cannot be committed");
  assert.equal(await snapshot(), JSON.stringify(sources)); await cancel();
  await enter("polar");
  for (const name of ["centerX", "centerY"]) {
    const previous = await field(name).inputValue(); await setField(name, "");
    assert.equal(await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').isDisabled(), true,
      `An empty ${name} cannot silently become zero`);
    assert.equal(await snapshot(), JSON.stringify(sources)); await setField(name, previous);
  }
  await cancel();
  await page.evaluate(() => { const c = window.check; c.draft().selectedIds = []; c.render(); });
  await enter("polar");
  assert.equal(await snapshot(), JSON.stringify(sources));
  assert.equal(await page.evaluate(() => window.check.draft().history.length), 0);
  results.push({ case: "no selection and entity limit", draftUnchanged: true });

  // A stale source cannot be copied after an asynchronous external replacement.
  await selectSources(sources); await enter("rectangular");
  await page.evaluate(() => { window.check.draft().entities[0].cx += 1; });
  const editedSource = await entities(); await apply();
  assert.deepEqual(await entities(), editedSource);
  assert.equal(await page.evaluate(() => window.check.draft().history.length), 0);
  await cancel();
  await selectSources(sources); await enter("rectangular");
  await page.evaluate(() => { const c = window.check; c.draft().entities[0] = { ...c.draft().entities[0], cx: 21 }; c.render(); });
  assert.equal(await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').isDisabled(), true);
  await cancel();
  await selectSources(sources); await enter("rectangular");
  await page.locator("[data-tube-sketch-canvas]").press("Delete");
  assert.deepEqual(await entities(), []);
  assert.equal(await page.locator("[data-tube-sketch-array-copy]").count(), 0);
  await page.locator('[data-sketch-command="sketch.undo"]').click(); assert.deepEqual(await entities(), sources);
  results.push({ case: "stale source rejection and Delete cancels array", staleWrites: 0, deleteUndoTransactions: 1 });

  // Editing/history commands must leave array mode even when their stacks are
  // empty. A subsequent canvas click verifies the select tool actually works.
  const assertArrayExited = async () => {
    assert.deepEqual(await page.evaluate(() => ({ command: window.check.view.tubeDesignerSketch.command,
      tool: window.check.view.tubeDesignerSketch.tool })), { command: null, tool: "select" });
    assert.equal(await page.locator("[data-tube-sketch-array-copy]").count(), 0);
    assert.equal(await page.locator("[data-tube-sketch-array-periodic-copy]").count(), 0);
    assert.equal(await page.locator("[data-tube-sketch-array-field]").count(), 0);
  };
  for (const action of ["undo", "redo", "Control+z", "Control+y", "join"]) {
    await selectSources(sources); await enter("rectangular");
    if (action.startsWith("Control+")) await page.locator("[data-tube-sketch-canvas]").press(action);
    else await page.locator(`[data-sketch-command="sketch.${action}"]`).click();
    await assertArrayExited(); assert.deepEqual(await entities(), sources);
    assert.equal(await page.evaluate(() => window.check.draft().history.length), 0,
      `${action} does not create an array history entry`);
    await clickModel(24, 90);
    assert.deepEqual(await page.evaluate(() => window.check.draft().selectedIds), ["circle"],
      `The canvas returns to working selection after ${action}`);
  }
  await selectSources(sources); await enter("rectangular");
  await page.locator('[data-sketch-command="sketch.delete"]').click();
  await assertArrayExited(); assert.deepEqual(await entities(), []);
  await page.locator('[data-sketch-command="sketch.undo"]').click(); assert.deepEqual(await entities(), sources);
  await selectSources(sources); await enter("rectangular"); await apply();
  const previousArray = await entities(); await enter("rectangular");
  await page.locator('[data-sketch-command="sketch.undo"]').click();
  await assertArrayExited(); assert.deepEqual(await entities(), sources);
  await enter("rectangular");
  await page.locator('[data-sketch-command="sketch.redo"]').click();
  await assertArrayExited(); assert.deepEqual(await entities(), previousArray);
  results.push({ case: "active array exits on undo redo delete join and shortcuts", verifiedTransitions: 8, emptyHistoryStillSelects: true });

  // Late native preview completion must keep the newest input and pane state.
  await selectSources(sources); await enter("rectangular"); await setField("spacingX", 20); await setField("spacingY", 30);
  await page.evaluate(() => { window.check.holdPreview = true; });
  await page.getByRole("button", { name: "预览三维", exact: true }).click();
  const retention = await page.evaluate(async () => {
    const c = window.check, svg = document.querySelector("[data-tube-sketch-canvas]"), canvas = document.querySelector(".fixture-three-canvas");
    const input = document.querySelector('[data-tube-sketch-array-field="spacingX"]');
    let probes = 0; svg.addEventListener("array-node-probe", () => probes++);
    input.focus({ preventScroll: true }); input.value = "23.75"; input.dispatchEvent(new Event("input", { bubbles: true }));
    const canSelect = ["text", "search", "url", "tel", "password"].includes(input.type);
    if (canSelect) input.setSelectionRange(1, 4, "forward");
    const panes = [...document.querySelectorAll(".tube-section-sketch-body>aside")], nested = document.querySelector(".tube-sketch-property-body");
    panes[0].scrollTop = 61; panes[1].scrollTop = 43; nested.scrollTop = 37;
    const positions = [panes[0].scrollTop, panes[1].scrollTop, nested.scrollTop];
    c.resolvePreview();
    await new Promise(resolve => setTimeout(resolve, 30));
    const nextInput = document.querySelector('[data-tube-sketch-array-field="spacingX"]');
    svg.dispatchEvent(new Event("array-node-probe"));
    return { sameSvg: svg === document.querySelector("[data-tube-sketch-canvas]"),
      sameCanvas: canvas === document.querySelector(".fixture-three-canvas"), sameInput: input === nextInput,
      focused: document.activeElement === nextInput, value: nextInput.value,
      selection: canSelect ? [nextInput.selectionStart, nextInput.selectionEnd] : null,
      positions, actualPositions: [panes[0].scrollTop, panes[1].scrollTop, nested.scrollTop], probes,
      pending: c.view.tubeDesignerSketch.sidePreviewPending };
  });
  assert.equal(retention.sameSvg, true); assert.equal(retention.sameCanvas, true); assert.equal(retention.sameInput, true);
  assert.equal(retention.focused, true); assert.equal(retention.value, "23.75");
  if (retention.selection) assert.deepEqual(retention.selection, [1, 4]);
  assert.deepEqual(retention.actualPositions, retention.positions); assert.equal(retention.probes, 1); assert.equal(retention.pending, false);
  assert.ok(retention.positions.every(position => position > 0), "Both sidebars and the nested pane genuinely scrolled");
  results.push({ case: "late preview DOM interaction retention", ...retention });
  await field("spacingX").focus();
  await enter("polar");
  assert.equal(await page.evaluate(() => document.activeElement?.matches?.('[data-tube-sketch-array-field="centerX"]')), false,
    "Removing a conditional array input never gives its focus to an unrelated new input");
  await cancel();
  assert.equal(errors.length, 0, errors.join("\n"));
  assert.equal(await page.evaluate(() => window.check.loggedErrors.length), 0);
  await page.screenshot({ path: resolve(output, "sketch-array-browser.png"), fullPage: true });
  writeFileSync(resolve(output, "sketch-array-browser.json"), JSON.stringify({ nativeTransport: "mocked", browser: "Chrome",
    productionAssets: Object.fromEntries(assets), results, pageErrors: errors }, null, 2));
  console.log(`Sketch array browser regression passed (${results.length} scenarios); native transport mocked.`);
} finally {
  await browser.close();
}
