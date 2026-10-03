import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge",
});
try {
  const page = await browser.newPage({ viewport: { width: 960, height: 740 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://assembly-annotation-layout.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body style='margin:0'></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://assembly-annotation-layout.test/");
  await page.evaluate(async () => {
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { buildAssemblyPreviewDimensionAnnotations } = await import(
      "/src/apps/tube-designer/webpage/assemblyPreviewAnnotations.mjs");
    document.body.innerHTML = '<div id="host" style="width:960px;height:740px"></div>';
    const viewport = createThreeViewport({ continuousRender: false, showGrid: false,
      showProjectionToggle: false, pickingEnabled: false });
    viewport.mount(document.querySelector("#host"));
    const material = new THREE.MeshBasicMaterial({ color: 0x60767c, transparent: true, opacity: 0.8 });
    const tab = new THREE.Mesh(new THREE.BoxGeometry(280, 60, 40), material);
    tab.position.set(-140, 0, 0); // 插舌件实体占 x=-280..0
    const slot = new THREE.Mesh(new THREE.BoxGeometry(280, 60, 40), material);
    slot.position.set(110, 0, 0); // 插槽件实体占 x=-30..250
    viewport.content.add(tab, slot);
    // Register the actual meshes as visible scene rows, which is what
    // getVisibleBounds() reads after an assembly snapshot has loaded.
    viewport.sceneObjects.set("assembly-finished:tab", tab);
    viewport.sceneObjects.set("assembly-finished:slot", slot);
    const bounds = viewport.getVisibleBounds();
    const preview = { plan: { templateId: "tab-slot-lock", designParts: [
      { id: "tab", role: "tab", label: "插舌件", request: { length: 280 } },
      { id: "slot", role: "slot", label: "插槽件", request: { length: 280 } },
    ] } };
    const dimensions = buildAssemblyPreviewDimensionAnnotations(preview, "finished", bounds);
    viewport.setCameraState({ target: { x: -15, y: 0, z: 0 }, radius: 850,
      theta: -Math.PI / 2, phi: Math.PI / 2 });
    viewport.setDimensionAnnotations(dimensions);
    window.assemblyLayout = { viewport, dimensions, bounds, meshes: [tab, slot] };
  });

  const inspect = () => page.evaluate(() => {
    const { viewport, dimensions, bounds, meshes } = window.assemblyLayout;
    const host = document.querySelector("#host").getBoundingClientRect();
    const canvas = viewport.renderer.domElement;
    const project = (point) => {
      const p = point.clone().project(viewport.camera);
      return { x: (p.x * 0.5 + 0.5) * canvas.clientWidth + host.left,
        y: (-p.y * 0.5 + 0.5) * canvas.clientHeight + host.top };
    };
    const tubeRects = meshes.map((mesh) => {
      mesh.updateMatrixWorld(true);
      const points = [];
      const size = mesh.geometry.parameters;
      for (const x of [-0.5, 0.5]) for (const y of [-0.5, 0.5]) for (const z of [-0.5, 0.5]) {
        points.push(project(mesh.position.clone().set(
          x * size.width, y * size.height, z * size.depth)
          .applyMatrix4(mesh.matrixWorld)));
      }
      return { left: Math.min(...points.map((p) => p.x)), right: Math.max(...points.map((p) => p.x)),
        top: Math.min(...points.map((p) => p.y)), bottom: Math.max(...points.map((p) => p.y)) };
    });
    const labelRects = [...document.querySelectorAll(".icax-three-dimension-label:not([hidden])")]
      .map((element) => {
        const rect = element.getBoundingClientRect();
        return { id: element.dataset.icaxDimensionAnnotation, left: rect.left, right: rect.right,
          top: rect.top, bottom: rect.bottom, text: element.textContent };
      });
    const leaders = [...document.querySelectorAll("[data-icax-dimension-leader]")]
      .map((element) => element.getAttribute("d"));
    const anchors = dimensions.map((dimension) => {
      const point = (coordinates) => {
        const vector = viewport.camera.position.clone().set(...coordinates).project(viewport.camera);
        return { x: (vector.x * 0.5 + 0.5) * canvas.clientWidth,
          y: (-vector.y * 0.5 + 0.5) * canvas.clientHeight };
      };
      return { start: point(dimension.start), end: point(dimension.end) };
    });
    return { host: { left: host.left, right: host.right, top: host.top, bottom: host.bottom },
      tubeRects, labelRects, leaders, anchors, dimensions, bounds,
      count: viewport.getDebugState().dimensionAnnotationCount,
      spriteCount: viewport.dimensionContent.children.length };
  });
  const intersects = (a, b) => Math.min(a.right, b.right) - Math.max(a.left, b.left) > 2
    && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 2;
  const check = (state, name) => {
    assert.deepEqual(state.bounds, { min: [-280, -30, -20], max: [250, 30, 20] },
      `${name}: dimensions use the real viewport mesh bounds`);
    assert.equal(state.count, 1, `${name}: finished assembly has one overall dimension`);
    assert.equal(state.labelRects.length, 1, `${name}: the overall caption remains in view`);
    assert.equal(state.labelRects[0].text, "成品总长 530 mm");
    assert.equal(state.leaders.length, 1, `${name}: only the current overall leader remains`);
    assert.equal(state.spriteCount, 0, `${name}: assembly labels no longer sit on 3D sprites`);
    assert.ok(state.dimensions.every((dimension) => dimension.placement === "outside"));
    assert.deepEqual([state.dimensions[0].start[0], state.dimensions[0].end[0]], [-280, 250]);
    assert.ok(state.dimensions.every((dimension) => !/逻辑段设定长|下料基准长|设定长/.test(dimension.label)));
    assert.ok(state.leaders.every((path) => path?.includes("L")), `${name}: every label has a leader`);
    for (const [index, path] of state.leaders.entries()) {
      const coordinates = path.match(/-?\d+(?:\.\d+)?(?:e[+-]?\d+)?/gi).map(Number);
      const anchor = state.anchors[index];
      assert.ok(Math.abs(coordinates[0] - anchor.start.x) < 0.01
        && Math.abs(coordinates[1] - anchor.start.y) < 0.01
        && Math.abs(coordinates[4] - anchor.end.x) < 0.01
        && Math.abs(coordinates[5] - anchor.end.y) < 0.01,
      `${name}: the leader must begin at both measured tube endpoints`);
    }
    for (const label of state.labelRects) {
      assert.ok(label.left >= state.host.left && label.right <= state.host.right
        && label.top >= state.host.top && label.bottom <= state.host.bottom,
      `${name}: ${label.id} remains inside the viewport`);
      assert.ok(state.tubeRects.every((tube) => !intersects(label, tube)),
        `${name}: ${label.id} must not cover a tube: ${JSON.stringify(state)}`);
    }
  };
  const front = await inspect();
  check(front, "front view");
  await page.evaluate(() => {
    const { viewport, dimensions } = window.assemblyLayout;
    for (let index = 0; index < 8; index++) viewport.setDimensionAnnotations(dimensions);
  });
  check(await inspect(), "repeated preview refresh");
  await page.evaluate(() => window.assemblyLayout.viewport.setDimensionAnnotations([]));
  const cleared = await inspect();
  assert.equal(cleared.count, 0, "clearing dimensions removes their visible labels");
  assert.equal(cleared.leaders.length, 0, "clearing dimensions removes their SVG leaders");
  await page.evaluate(() => {
    const { viewport, dimensions } = window.assemblyLayout;
    viewport.setDimensionAnnotations(dimensions);
  });
  await page.evaluate(() => {
    const { viewport, dimensions } = window.assemblyLayout;
    viewport.setDimensionAnnotations(dimensions.map((item) => ({ ...item, color: 0xffc857 })));
    viewport.setDimensionAnnotations(dimensions);
  });
  const switched = await inspect();
  check(switched, "formed and blank view switch");
  assert.ok(await page.locator("[data-icax-dimension-leader]").evaluateAll(
    (paths) => paths.every((path) => path.getAttribute("stroke") === "#66d7ca")),
  "switching back to the formed view removes the old blank-view leaders");
  if (process.env.ICAX_ASSEMBLY_ANNOTATION_SCREENSHOT) {
    await page.screenshot({ path: process.env.ICAX_ASSEMBLY_ANNOTATION_SCREENSHOT });
  }
  await page.evaluate(() => {
    window.assemblyLayout.viewport.setCameraState({ target: { x: 150, y: 0, z: 80 },
      radius: 660, theta: -1.04, phi: 1.22 });
  });
  const oblique = await inspect();
  check(oblique, "oblique view");
  assert.notDeepEqual(oblique.leaders, front.leaders, "leaders must follow camera rotation");
  await page.evaluate(() => {
    document.querySelector("#host").style.width = "760px";
    document.querySelector("#host").style.height = "620px";
    window.assemblyLayout.viewport.resize();
  });
  check(await inspect(), "resized view");
  await page.evaluate(() => {
    window.assemblyLayout.viewport.setProjectionMode("orthographic");
    window.assemblyLayout.viewport.setStandardView("front");
  });
  check(await inspect(), "orthographic view");
  await page.evaluate(() => {
    const { viewport, dimensions } = window.assemblyLayout;
    const legacyDimension = { ...dimensions[0] };
    delete legacyDimension.placement;
    viewport.setDimensionAnnotations([legacyDimension]);
  });
  const legacy = await inspect();
  assert.equal(legacy.count, 1, "ordinary dimension annotations retain their prior viewport path");
  assert.equal(legacy.labelRects.length, 0);
  assert.equal(legacy.spriteCount, 2, "ordinary dimensions still use a line and sprite");
  await page.evaluate(() => window.assemblyLayout.viewport.dispose());
  assert.deepEqual(errors, []);
  console.log("Assembly viewport annotation layout: front, oblique, resize, orthographic passed");
} finally {
  await browser.close();
}
