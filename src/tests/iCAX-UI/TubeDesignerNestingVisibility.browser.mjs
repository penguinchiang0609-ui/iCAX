// Real WebGL regression for the nesting geometry visibility lifecycle.
// Nesting intentionally does not measure parts or display dimension annotations.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 850 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  const root = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://nesting-visibility.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><html><head></head><body></body></html>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://nesting-visibility.test/");
  await page.evaluate(async () => {
    const area = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML = `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px Arial,"Microsoft Yahei",sans-serif;background:#eff4f5}#workspace{display:grid;grid-template-columns:minmax(0,1fr) 300px;height:100vh}#stage{position:relative;min-width:0;min-height:0}#scene,#overlay{position:absolute;inset:0}#overlay{pointer-events:none}#right{overflow:auto}</style>`;
    document.body.classList.add("tube-designer-workspace");
    document.body.innerHTML = '<div id="workspace"><main id="stage"><div id="scene"></div><div id="overlay"></div></main><aside id="right"></aside></div>';
    const part = id => ({ entityId: id, name: "程式圆管 Ø100 × 3 · 1000 mm", length: 1000, quantity: 1,
      profile: { sectionIdentity: JSON.stringify({ schema: "icax.nesting-section.v1", contours: ['{"edges":["round-100-t3"]}'] }), kind: "round", width: 100, depth: 100, diameter: 100, wallThickness: 3, displayName: "圆管", specification: "Ø100 × 3" },
      thumbnailGeometryResourceId: `icax-resource://${id}`, thumbnailGeometryResourceVersion: 1,
      manufacturingGeometryResourceVersion: 1, properties: { "manufacturing.partKind": "tube", "nesting.snapshot": { source: "standard-part" } } });
    const shape = new THREE.Shape();
    shape.absarc(0, 0, 50, 0, Math.PI * 2, false);
    const hole = new THREE.Path();
    hole.absarc(0, 0, 47, 0, Math.PI * 2, true);
    shape.holes.push(hole);
    const geometry = new THREE.ExtrudeGeometry(shape, { depth: 1000, bevelEnabled: false, steps: 1, curveSegments: 48 });
    geometry.rotateY(Math.PI / 2);
    geometry.translate(-500, 0, 0);
    const bytes = encodeNestingGeometry({ positions: [...geometry.attributes.position.array], indices: Array.from({ length: geometry.attributes.position.count }, (_, index) => index) });
    geometry.dispose();
    const view = { activeAreaId: "nesting", tubeDesignerNestingSelectionKind: "part", tubeDesignerActivePartId: "standard-1",
      scene: { tubeDesigner: { nestingGroups: [{ name: "独立下料零件", parts: [part("standard-1"), part("standard-2")] }] } } };
    const counters = { reads: 0, measures: 0, snapshots: 0 };
    const delays = {};
    delays.geometry = new Promise(resolve => { delays.finishGeometry = resolve; });
    const context = { mount: document.body, sceneProxy: {
      resources: { async get() { counters.reads++; await delays.geometry; return new Response(bytes); } },
      async invoke(method) { counters.measures++; throw new Error(`Nesting must not invoke measurement: ${method}`); },
    } };
    view.viewport = createThreeViewport({ continuousRender: false });
    view.viewport.mount(document.querySelector("#scene"));
    const apply = view.viewport.applyViewSnapshot.bind(view.viewport);
    view.viewport.applyViewSnapshot = (...args) => { counters.snapshots++; return apply(...args); };
    const render = () => {
      document.querySelector("#overlay").innerHTML = area.renderNestingViewportOverlay(context, view);
      document.querySelector("#right").innerHTML = area.renderNestingRightPane(context, view);
      view.viewport.setVisibleEntityIds([]);
    };
    window.fixture = { area, view, context, counters, delays, render };
    render();
  });
  await page.addStyleTag({ content: readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css", import.meta.url), "utf8") });
  const progress = page.locator("[data-tube-designer-part-progress]");
  await progress.waitFor({ state: "visible" });
  assert.match(await progress.innerText(), /正在加载三维模型/);
  assert.equal(await progress.locator('[role="progressbar"]').getAttribute("aria-valuenow"), null);
  await page.waitForFunction(() => window.fixture.counters.reads === 1);
  assert.equal(await progress.locator('[role="progressbar"] > i').evaluate(element => getComputedStyle(element).animationName), "tube-designer-export-indeterminate");
  assert.notEqual(await progress.locator('[role="progressbar"] > i').evaluate(element => getComputedStyle(element).backgroundColor), "rgba(0, 0, 0, 0)");
  if (process.env.ICAX_ARTIFACT_DIR) {
    mkdirSync(process.env.ICAX_ARTIFACT_DIR, { recursive: true });
    await page.waitForFunction(() => {
      const band = document.querySelector('[data-tube-designer-part-progress] [role="progressbar"] > i');
      const box = band.getBoundingClientRect(), track = band.parentElement.getBoundingClientRect();
      return box.left > track.left + 8 && box.right < track.right - 8;
    });
    await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, "nesting-standard-part-loading.png") });
  }
  await page.evaluate(() => window.fixture.delays.finishGeometry());
  await page.waitForFunction(() => window.fixture.view.viewport.getDebugState().visibleObjectCount === 1
    && window.fixture.view.tubeDesignerPartProgress === null);
  await progress.waitFor({ state: "hidden" });
  const inspect = () => page.evaluate(() => ({
    debug: window.fixture.view.viewport.getDebugState({ samplePixels: true }),
    ids: [...window.fixture.view.viewport.visibleEntityIds], counters: { ...window.fixture.counters },
  }));
  const first = await inspect();
  assert.equal(first.debug.visibleObjectCount, 1);
  assert.equal(first.debug.dimensionAnnotationCount, 0, "nesting displays geometry without annotations");
  assert.equal(first.counters.measures, 0, "nesting does not measure geometry in the background");
  assert.equal(await page.locator('[data-cam-action="tube-designer-parts-toggle-dimensions"]').count(), 0,
    "nesting does not offer a dimension control");
  assert.ok(first.debug.renderInfo.triangles > 500, "tube surfaces are actual rendered triangles");
  assert.deepEqual(first.ids, ["standard-1"]);
  for (let index = 0; index < 12; index++) {
    const hiddenCount = await page.evaluate(async () => {
      const f = window.fixture;
      f.render();
      const hidden = f.view.viewport.getDebugState().visibleObjectCount;
      await new Promise(resolve => requestAnimationFrame(resolve));
      return hidden;
    });
    assert.equal(hiddenCount, 0, "workbench mount hides the previous solid synchronously");
    const cached = await inspect();
    assert.equal(cached.debug.visibleObjectCount, 1, "cached render restores the real Three.js mesh before the next frame");
    assert.equal(cached.debug.dimensionAnnotationCount, 0);
    assert.deepEqual(cached.ids, ["standard-1"]);
    assert.deepEqual(cached.counters, first.counters, "no backend, resource or remeshing work on a cached render");
    assert.equal(await progress.isVisible(), false, "cached redraws must not flash the progress bar");
  }
  await page.evaluate(() => { const f = window.fixture; f.view.tubeDesignerActivePartId = "standard-2"; f.render(); });
  await page.waitForFunction(() => window.fixture.view.tubeDesignerPartViewportKey?.startsWith("standard-2@")
    && window.fixture.view.tubeDesignerPartProgress === null);
  const second = await inspect();
  assert.deepEqual(second.ids, ["standard-2"]);
  assert.equal(second.debug.visibleObjectCount, 1);
  assert.equal(second.debug.dimensionAnnotationCount, 0, "a different nesting part also has no annotations");
  assert.equal(second.counters.measures, 0);
  assert.equal(second.counters.reads, 2, "switching parts reads only the selected geometry");
  await page.evaluate(async () => {
    const f = window.fixture;
    await f.area.handlePartsAreaAction(f.context, f.view, 'tube-designer-parts-toggle-dimensions', {}, {});
    f.render();
    await new Promise(resolve => requestAnimationFrame(resolve));
  });
  const afterIgnoredDimensionAction = await inspect();
  assert.equal(afterIgnoredDimensionAction.debug.visibleObjectCount, 1);
  assert.equal(afterIgnoredDimensionAction.debug.dimensionAnnotationCount, 0);
  assert.deepEqual(afterIgnoredDimensionAction.counters, second.counters,
    "a dimension action outside the nesting UI cannot trigger backend work");
  if (process.env.ICAX_ARTIFACT_DIR) {
    mkdirSync(process.env.ICAX_ARTIFACT_DIR, { recursive: true });
    await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, "nesting-standard-part-visible.png") });
  }
  await page.evaluate(() => { const f = window.fixture; f.render(); f.view.scene.tubeDesigner.nestingGroups = []; f.render(); });
  await page.waitForFunction(() => window.fixture.view.viewport.getDebugState().visibleObjectCount === 0);
  assert.equal((await inspect()).debug.visibleObjectCount, 0);
  assert.equal((await inspect()).debug.dimensionAnnotationCount, 0);
  assert.equal((await inspect()).counters.measures, 0);
  assert.deepEqual(errors, []);
  await page.evaluate(() => window.fixture.view.viewport.dispose());
  console.log("Nesting visibility browser regression passed: real tube geometry survives cached remounts, with no background measurements or dimension annotations.");
} finally {
  await browser.close();
}
