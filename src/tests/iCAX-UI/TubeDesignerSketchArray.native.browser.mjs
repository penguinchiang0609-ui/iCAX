// Production sketch controls -> real scene SDOs/BRep/GPU resources -> .ictd reopen.
// The stdio host and Chrome shell are controlled; installed CEF is outside scope.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { repositoryRoot, startSideSketchNativeBridge } from "./fixtures/sideSketchNativeBridge.mjs";

const sourceRoot = resolve(repositoryRoot, "src");
const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, "output/tests/sketch-array-native-browser"));
mkdirSync(artifacts, { recursive: true });
const bridge = await startSideSketchNativeBridge(artifacts);
const assets = new Map(), cases = [], errors = [];
let browser, page;
const near = (actual, expected, label, tolerance = 1e-5) => assert.ok(Math.abs(actual - expected) < tolerance, `${label}: ${actual} vs ${expected}`);
try {
  const rectangle = (width, height) => {
    const points = [[-width / 2, -height / 2], [width / 2, -height / 2], [width / 2, height / 2], [-width / 2, height / 2]];
    return { kind: "path", closed: true, segments: points.map((start, index) => ({ kind: "line", start, end: points[(index + 1) % points.length] })) };
  };
  const profile = { schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "fixed-section", profileForm: "fixed",
    name: "阵列真实原生验收方管", width: 80, depth: 60, contours: [rectangle(80, 60), rectangle(76, 56)] };
  const blank = { profile, length: 200, quantity: 1, previewResourceKey: "sketch-array-native-blank" };
  const initialReport = await bridge.invoke("PreviewNestingSideSketchPart", blank);
  assert.equal(initialReport.available, true);
  const period = initialReport.unfolding.surfaces.find(surface => !surface.inner).uPeriod;
  near(period, 280, "Native blank perimeter");
  const blankVolume = (80 * 60 - 76 * 56) * 200;
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome" });
  page = await browser.newPage({ viewport: { width: 1680, height: 1040 } });
  page.setDefaultTimeout(180000);
  page.on("pageerror", error => errors.push(error.message));
  await page.exposeFunction("nativeRpc", bridge.rpc);
  await page.route("http://sketch-array-native.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset='utf-8'><body><div id='app' class='tube-designer-workspace'></div></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    const code = readFileSync(path, "utf8"); assets.set(pathname, createHash("sha256").update(code).digest("hex"));
    return route.fulfill({ contentType: "text/javascript", body: code });
  });
  await page.goto("http://sketch-array-native.test/");
  await page.addStyleTag({ content: readFileSync(resolve(sourceRoot, "apps/_shared/workbench/styles/laser3dcam.css"), "utf8") });
  await page.evaluate(async () => {
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    const parts = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const preview = await import("/src/apps/tube-designer/webpage/sideSketchPreview.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    document.head.insertAdjacentHTML("beforeend", `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px "Segoe UI",sans-serif;background:#13252d}</style>`);
    const mount = document.querySelector("#app"), view = { activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [] } } };
    const f = window.fixture = { mount, view, sketch, parts, preview, calls: [], resources: [], pending: 0, failures: [] };
    Object.defineProperty(f, "state", { get() { return view.tubeDesignerSketch; } });
    Object.defineProperty(f, "draft", { get() { return f.state.sideByPart[f.state.targetPartId]; } });
    const invoke = async (method, payload) => {
      const call = { method, payload: structuredClone(payload) }; f.calls.push(call);
      try {
        call.result = await window.nativeRpc({ action: "invoke", scope: "scene", method: method.replace(/^TubeDesigner\./, ""), payload });
        return call.result;
      } catch (error) { call.error = error.message; throw error; }
    };
    f.context = { mount, sceneProxy: { invoke, resources: { async get(url, options) {
      const version = Number(options?.headers?.get("ICAX-Resource-Version") || 0);
      const response = await window.nativeRpc({ action: "resource", payload: { url, version } });
      f.resources.push({ url, version, bytes: response.bytes });
      return new Response(Uint8Array.from(atob(response.base64), character => character.charCodeAt(0)));
    } } } };
    f.ops = { createSideSketchViewport(options) { return f.viewport = createThreeViewport(options); }, showNotice() {}, renderProject() {
      if (!sketch.patchSketchDialogDom(f.context, view, mount, f.ops)) {
        const list = parts.listNestingParts(view.scene.tubeDesigner ?? {});
        mount.innerHTML = "<main>" + list.map(part => `<button data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${part.entityId}">二维编辑 ${part.entityId}</button>`).join("") + "</main>" + sketch.renderSectionSketchDialog(f.context, view);
        sketch.rememberSketchDialogDom(view, mount, f.context.sceneProxy);
      }
      sketch.attachSketchAreaInteractions(f.context, view, mount, f.ops);
    } };
    f.load = ({ payload, report, sources }) => {
      sketch.beginNewPartSideSketch(view, payload, report);
      view.tubeDesignerSketchDialogOpen = true;
      f.draft.entities = structuredClone(sources); f.draft.selectedIds = sources.map(entity => entity.id);
      f.draft.trajectoryWidth = .8; f.state.snapEnabled = false; f.state.orthoEnabled = false;
      f.ops.renderProject();
    };
    const dispatch = (action, target) => {
      f.pending++;
      const operation = action.startsWith("tube-designer-sketch-")
        ? sketch.handleSketchAreaAction(f.context, view, action, target, f.ops)
        : parts.handlePartsAreaAction(f.context, view, action, target, f.ops);
      Promise.resolve(operation).catch(error => f.failures.push(error.message)).finally(() => f.pending--);
    };
    mount.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target && !target.disabled) dispatch(target.dataset.camAction, target); });
    mount.addEventListener("change", event => { const target = event.target.closest("[data-cam-change-action]"); if (target) dispatch(target.dataset.camChangeAction, target); });
  });
  const idle = async () => {
    await page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending && !window.fixture.state?.sidePreviewPending);
    assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
  };
  const ready = async () => { await idle(); assert.equal(await page.evaluate(() => window.fixture.preview.waitForSideSketchPreview(window.fixture.mount)), true); };
  const setField = async (key, value) => { const field = page.locator(`[data-tube-sketch-array-field="${key}"]`); await field.fill(String(value)); await field.dispatchEvent("change"); };
  const command = key => page.locator(`[data-sketch-command="sketch.${key}"]`);
  const modelPoint = async (x, y) => page.locator("[data-tube-sketch-canvas]").evaluate((svg, [x, y]) => {
    const value = name => Number(svg.getAttribute(`data-sketch-${name}`));
    const point = svg.createSVGPoint();
    point.x = value("canvas-left") + (x - value("x-min")) / (value("x-max") - value("x-min")) * (value("canvas-right") - value("canvas-left"));
    point.y = value("canvas-bottom") - (y - value("y-min")) / (value("y-max") - value("y-min")) * (value("canvas-bottom") - value("canvas-top"));
    const client = point.matrixTransform(svg.getScreenCTM()); return [client.x, client.y];
  }, [x, y]);
  const clickModel = async (x, y) => page.mouse.click(...await modelPoint(x, y));
  const moveModel = async (x, y) => { await page.mouse.move(...await modelPoint(x, y)); await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))); };
  const scenarios = [
    { id: "parameter-rectangular", kind: "rectangular", mode: "parameters", columns: 2, rows: 2, spacingX: period + 80, spacingY: 157,
      sources: [
        { id: "periodic-circle", kind: "circle", cx: period + 5, cy: 40, radius: 4, closed: true },
        { id: "rounded-window", kind: "rectangle", x: period + 25, y: 80, width: 8, height: 12, radius: 2, closed: true },
        { id: "open-arc", kind: "circleArc", cx: period + 50, cy: 120, radius: 4, startAngle: -.5, sweep: 2, closed: false },
      ] },
    { id: "parameter-polar", kind: "polar", mode: "parameters", count: 3, angleStep: 60, centerX: 45, centerY: 100,
      sources: [
        { id: "polar-window", kind: "rectangle", x: 69, y: 91, width: 8, height: 12, radius: 2, closed: true },
        { id: "polar-bezier", kind: "path", closed: false, segments: [{ kind: "bezier", points: [[80, 110], [83, 118], [89, 105], [94, 110]] }] },
      ] },
    { id: "mouse-rectangular", kind: "rectangular", mode: "mouse", columns: 2, rows: 2, base: [10, 30], pointer: [130, 210],
      sources: [
        { id: "mouse-circle", kind: "circle", cx: period * 2 + 10, cy: 50, radius: 3, closed: true },
        { id: "mouse-open-arc", kind: "circleArc", cx: period * 2 + 30, cy: 90, radius: 3, startAngle: .3, sweep: 1.7, closed: false },
      ] },
    { id: "mouse-polar", kind: "polar", mode: "mouse", count: 3, base: [150, 100],
      sources: [{ id: "mouse-polar-window", kind: "rectangle", x: 175, y: 95, width: 10, height: 10, radius: 2, closed: true }] },
  ];
  for (const scenario of scenarios) {
    const started = Date.now(), payload = { ...blank, previewResourceKey: scenario.id };
    const report = await bridge.invoke("PreviewNestingSideSketchPart", payload);
    await page.evaluate(item => window.fixture.load(item), { payload, report, sources: scenario.sources }); await ready();
    await command(`array-${scenario.kind}`).click();
    for (const key of scenario.kind === "rectangular" ? ["columns", "rows", "spacingX", "spacingY"] : ["count", "angleStep", "centerX", "centerY"]) {
      if (scenario[key] !== undefined) await setField(key, scenario[key]);
    }
    if (scenario.mode === "mouse") {
      await page.locator('[data-tube-sketch-array-mode="mouse"]').click();
      await clickModel(...scenario.base);
      const pointer = scenario.pointer ?? await page.evaluate(() => {
        const command = window.fixture.state.command, [gx, gy] = command.arraySpec.groupCenter, [cx, cy] = command.points[0];
        return [cx + (gx - cx) * .5 - (gy - cy) * Math.sqrt(3) / 2, cy + (gx - cx) * Math.sqrt(3) / 2 + (gy - cy) * .5];
      });
      await moveModel(...pointer);
      assert.equal(await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').isEnabled(), true);
      await clickModel(...pointer);
    } else await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').click();
    const expected = await page.evaluate(() => structuredClone(window.fixture.draft.entities));
    const instances = scenario.kind === "rectangular" ? scenario.columns * scenario.rows : scenario.count;
    assert.equal(expected.length, scenario.sources.length * instances);
    assert.deepEqual(expected.slice(0, scenario.sources.length), scenario.sources);
    if (scenario.kind === "polar") {
      assert.ok(expected.some(entity => entity.kind === "path" && entity.closed && entity.segments.filter(segment => segment.kind === "circleArc").length === 4), "Rotated rounded rectangles retain four analytic corner arcs");
    }
    if (scenario.id === "parameter-polar") assert.equal(expected.filter(entity => entity.segments?.some(segment => segment.kind === "bezier")).length, 3);
    await page.locator('[data-cam-action="tube-designer-sketch-preview-side"]').click(); await ready();
    await page.screenshot({ path: resolve(artifacts, `${scenario.id}-actual-cut-preview.png`) });
    await command("commit").click(); await idle();
    assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerSketchDialogOpen), false);
    const created = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === "TubeDesigner.AddNestingSideSketchPart"));
    assert.ok(created?.result?.partEntityId); assert.deepEqual(created.payload.sketch.entities, expected);
    const geometry = await bridge.rpc({ action: "geometry", payload: { partEntityId: created.result.partEntityId } });
    assert.equal(geometry.valid, true); assert.ok(geometry.volume > 0 && geometry.volume < blankVolume - 1);
    const scene = await bridge.invoke("List"), part = scene.tubeDesigner.nestingGroups.flatMap(group => group.parts).find(part => part.entityId === created.result.partEntityId);
    assert.deepEqual(part.properties["tubeDesigner.sideSketch"].entities, expected);
    assert.ok(part.properties["tubeDesigner.sideSketchBaseResourceId"]); assert.equal(part.properties["tubeDesigner.sideSketchRecipe"].length, 200);
    cases.push({ id: scenario.id, kind: scenario.kind, mode: scenario.mode, partEntityId: created.result.partEntityId, entities: expected, geometry, sketch: created.payload.sketch, elapsedMs: Date.now() - started });
    console.log(`${scenario.id}: saved ${expected.length} exact entities through native SDO (${Date.now() - started} ms)`);
  }
  // Whole periods preserve physical holes. Entirely axial-outside copies vanish
  // from the BRep while remaining present in the persistent editable draft.
  const periodic = cases[0];
  assert.ok(periodic.entities.some(entity => entity.cy > 200 || entity.y > 200));
  const oracleEntities = periodic.entities.filter(entity => !(entity.cy > 205 || entity.y > 205)).map(entity => {
    const copy = structuredClone(entity);
    if (copy.kind === "rectangle") copy.x -= period * 4;
    else copy.cx -= period * 4;
    return copy;
  });
  const oracle = await bridge.invoke("AddNestingSideSketchPart", { ...blank, name: "阵列周期与轴向截断独立几何参照", sketch: { ...periodic.sketch, entities: oracleEntities } });
  const comparison = await bridge.rpc({ action: "compare-geometry", payload: { firstPartEntityId: periodic.partEntityId, secondPartEntityId: oracle.partEntityId } });
  near(comparison.firstVolume, comparison.secondVolume, "Whole-period translation and removing axial-outside copies preserve volume", .001);
  near(comparison.firstVolume, comparison.commonVolume, "The independent periodic/clipping oracle has the same true BRep", .001);
  await bridge.rpc({ action: "save", payload: { file: "sketch-arrays-native.ictd" } });
  const reopened = await bridge.rpc({ action: "open", payload: { file: "sketch-arrays-native.ictd" } });
  await page.evaluate(scene => { const f = window.fixture; f.view.scene = scene; f.ops.renderProject(); }, reopened.snapshot);
  for (const item of cases) {
    await page.locator(`[data-cam-action="tube-designer-part-open-sketch"][data-tube-designer-part-id="${item.partEntityId}"]`).click(); await ready();
    assert.deepEqual(await page.evaluate(() => window.fixture.draft.entities), item.entities, `${item.id}: .ictd and native open restore every original exact control point and curve`);
    near(await page.evaluate(() => window.fixture.state.sideReference.unfolding.surfaces.find(surface => !surface.inner).uPeriod), period, "Reopen retains frozen blank perimeter");
    const geometry = await bridge.rpc({ action: "geometry", payload: { partEntityId: item.partEntityId } });
    assert.equal(geometry.valid, true); near(geometry.volume, item.geometry.volume, "Reopen retains actual cut volume");
    await command("cancel").click(); await idle();
  }
  const browserEvidence = await page.evaluate(() => ({ resources: window.fixture.resources, failures: window.fixture.failures,
    calls: window.fixture.calls.map(call => ({ method: call.method, error: call.error })) }));
  assert.deepEqual(errors, []); assert.deepEqual(browserEvidence.failures, []);
  assert.ok(browserEvidence.resources.length >= 8 && browserEvidence.resources.every(resource => resource.bytes > 0));
  writeFileSync(resolve(artifacts, "report.json"), JSON.stringify({ passed: true, dllHash: bridge.dllHash, assets: Object.fromEntries(assets), period, blankVolume,
    cases, comparison, browserEvidence, coverage: "All four production array UI combinations saved through real scene AddNestingSideSketchPart; true BRep validity/volume/common-volume; production SVG/Three loading actual GPU resources; circle, rounded rectangle, open circular arc and Bezier preserved; periodic U and clipped S; .ictd save/open then original draft reopen. Controlled stdio host and Chrome, no installed CEF or native file chooser." }, null, 2));
  console.log(JSON.stringify({ passed: true, artifacts, cases: cases.map(({ id, elapsedMs, entities }) => ({ id, elapsedMs, entityCount: entities.length })), resourceReads: browserEvidence.resources.length }));
} catch (error) {
  if (page) await page.screenshot({ path: resolve(artifacts, "failure.png") }).catch(() => {});
  writeFileSync(resolve(artifacts, "failure.json"), JSON.stringify({ error: error.stack, dllHash: bridge.dllHash, cases }, null, 2));
  throw error;
} finally {
  if (browser) await browser.close();
  await bridge.close();
}
