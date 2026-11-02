import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const output = resolve(process.env.ICAX_VIEW_NAVIGATION_OUTPUT || "output/tests/minimal-protective-grille-20261026/view-navigation-browser");
mkdirSync(output, { recursive: true });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
const report = { nativeGeometry: false, standaloneCefEndToEnd: false, checks: [], errors: [] };
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  page.on("pageerror", error => report.errors.push(error.message));
  await page.route("http://tube-navigation.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html lang="zh-CN"><head><meta charset="UTF-8"></head><body></body></html>' });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep) || !/\.(m?js|css)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: path.endsWith(".css") ? "text/css" : "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://tube-navigation.test/");
  await page.addStyleTag({ content: readFileSync(resolve(sourceRoot, "apps/_shared/workbench/styles/workbench.css"), "utf8") + tubeDesignerCss + `
    html,body{height:100%;margin:0;background:#182128} #layout{height:100%;display:grid;grid-template-columns:180px minmax(0,1fr) 180px}
    .fixture-pane{height:100%;overflow:auto;background:#e9eff1}.fixture-pane>div{height:1600px;padding:12px;box-sizing:border-box}
    #workspace,#mount{position:relative;min-width:0;height:100%} input{width:130px}
  ` });
  await page.evaluate(async () => {
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { tubeDesignerViewportNavigation } = await import("/src/apps/tube-designer/webpage/viewportNavigation.mjs");
    const cube = await import("/src/apps/_shared/workbench/viewport/viewCube.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    document.body.innerHTML = '<div id="layout"><aside id="left" class="fixture-pane"><div><input id="draft" value="123.45"></div></aside><main id="workspace" class="tube-designer-workspace"><div id="mount"></div></main><aside id="right" class="fixture-pane"><div>右栏</div></aside></div>';
    const viewport = createThreeViewport({ ...tubeDesignerViewportNavigation, showGrid: false, continuousRender: false });
    const mount = document.querySelector("#mount");
    viewport.mount(mount); mount.insertAdjacentHTML("beforeend", cube.renderViewCube());
    const view = { viewport };
    cube.attachViewCube(view, mount);
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(120, 18, 75), new THREE.MeshStandardMaterial({ color: 0x6daab1 }));
    mesh.position.set(9, 12, 7); viewport.content.add(mesh); mesh.updateMatrixWorld(true);
    viewport.status.hidden = true; viewport.setStandardView("top-front-right");
    const originalParameters = { width: 123.45, height: 900, quantity: 1 };
    window.fixture = { view, mesh, cube, originalParameters };
  });
  const frames = () => page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
  const camera = () => page.evaluate(() => fixture.view.viewport.getCameraState());
  const invariant = () => page.evaluate(() => ({ matrix: fixture.mesh.matrix.toArray(), parameters: fixture.originalParameters }));
  const initial = await invariant();
  await frames();
  assert.equal(await page.locator(".cam-viewcube").getAttribute("data-appearance"), "beveled");
  assert.deepEqual((await page.locator(".cam-viewcube-label").allTextContents()).sort(), ["顶视", "前视", "右视"].sort());
  assert.deepEqual(await page.locator("[data-cam-axis]").evaluateAll(nodes => nodes.map(n => n.dataset.camAxis)), ["x", "y", "z"]);
  assert.equal(await page.locator("[data-cam-compass]").count(), 1);
  assert.equal(await page.locator(".icax-three-axis-gizmo").count(), 0);
  report.checks.push("Shared beveled Chinese cube, XYZ triad and compass; no duplicate orientation gizmo");
  let targetsClicked = 0;
  for (const projectionMode of ["perspective", "orthographic"]) {
    await page.locator(`button[data-projection-mode="${projectionMode}"]`).click();
    for (const z of [-1, 0, 1]) for (const y of [-1, 0, 1]) for (const x of [-1, 0, 1]) {
      if (!x && !y && !z) continue;
      const name = [z ? z > 0 ? "top" : "bottom" : "", y ? y > 0 ? "back" : "front" : "", x ? x > 0 ? "right" : "left" : ""].filter(Boolean).join("-");
      await page.evaluate(eye => fixture.view.viewport.setViewDirection(eye.map((v, i) => v + [.18, .12, .15][i])), [x, y, z]); await frames();
      await page.locator(`[data-cam-view="${name}"]`).click(); await frames();
      const state = await page.evaluate(() => fixture.view.viewport.getViewCubeState());
      // The existing constrained camera uses fixed Z-up; Three.js offsets an
      // exactly parallel top/bottom look-at by 1e-4 to keep its basis defined.
      const tolerance = !x && !y ? 1.1e-4 : 1e-5;
      [x, y, z].forEach((v, i) => assert.ok(Math.abs(state.direction["xyz"[i]] + v / Math.hypot(x, y, z)) < tolerance, `${name}: ${JSON.stringify(state)}`));
      assert.equal((await camera()).projectionMode, projectionMode);
      targetsClicked++;
    }
  }
  report.checks.push(`${targetsClicked} actual face/edge/corner clicks in perspective and orthographic projection`);
  for (const mode of ["perspective", "orthographic"]) {
    await page.locator(`button[data-projection-mode="${mode}"]`).click();
    await page.evaluate(() => fixture.view.viewport.setStandardView("top-front-right")); await frames();
    const start = await camera();
    const point = await page.locator('[data-cam-view="front"] polygon').evaluate(n => {
      const b = n.getBBox(), p = new DOMPoint(b.x + b.width / 2, b.y + b.height / 2).matrixTransform(n.getScreenCTM());
      return { x: p.x, y: p.y };
    });
    await page.mouse.move(point.x, point.y); await page.mouse.down();
    await page.mouse.move(point.x - 35, point.y + 20, { steps: 7 }); await frames();
    const changed = await camera();
    assert.notEqual(changed.theta, start.theta); assert.notEqual(changed.phi, start.phi);
    await page.keyboard.press("Escape"); await page.mouse.up(); await frames();
    assert.deepEqual(await camera(), start, "Escape cancels cube rotation");
    const ring = await page.locator("[data-cam-compass]").evaluate(n => {
      const m = n.ownerSVGElement.getScreenCTM(), p = new DOMPoint(+n.dataset.cx, +n.dataset.cy).matrixTransform(m);
      return { x: p.x, y: p.y, rx: +n.dataset.rx * m.a, ry: +n.dataset.ry * m.d, sign: +n.dataset.sign };
    });
    await page.mouse.move(ring.x, ring.y + ring.ry); await page.mouse.down();
    assert.equal(await page.locator(".cam-viewcube").getAttribute("data-compass-dragging"), "true");
    for (let i = 1; i <= 12; i++) {
      const angle = Math.PI / 2 + i * Math.PI / 24;
      await page.mouse.move(ring.x + ring.rx * Math.cos(angle), ring.y + ring.ry * Math.sin(angle)); await frames();
    }
    await page.mouse.up(); await frames();
    const turned = await camera();
    assert.ok(Math.abs(turned.theta - start.theta - ring.sign * Math.PI / 2) < .035);
    assert.equal(turned.phi, start.phi); assert.equal(turned.projectionMode, start.projectionMode);
  }
  report.checks.push("Real cube drag, Escape cancellation and 90 degree compass drag in both projection modes");
  await page.evaluate(() => {
    document.querySelector("#right").scrollTop = 180;
    const input = document.querySelector("#draft"); input.focus({ preventScroll: true }); input.setSelectionRange(1, 4);
    fixture.input = input; fixture.canvas = fixture.view.viewport.renderer.domElement;
    fixture.view.viewport.setStandardView("top-front-right");
  }); await frames();
  const p = await page.locator('[data-cam-view="front"] polygon').evaluate(n => { const b = n.getBBox(); const p = new DOMPoint(b.x + b.width / 2, b.y + b.height / 2).matrixTransform(n.getScreenCTM()); return { x: p.x, y: p.y }; });
  await page.mouse.move(p.x, p.y); await page.mouse.down(); await page.mouse.move(p.x - 25, p.y + 15, { steps: 5 }); await page.mouse.up(); await frames();
  assert.deepEqual(await page.evaluate(() => ({ input: fixture.input === document.querySelector("#draft"), focus: document.activeElement === fixture.input, selection: [fixture.input.selectionStart, fixture.input.selectionEnd], right: document.querySelector("#right").scrollTop, canvas: fixture.canvas === document.querySelector("canvas") })), { input: true, focus: true, selection: [1, 4], right: 180, canvas: true });
  report.checks.push("Camera navigation preserves parameter input, focus, selection, scroll and WebGL canvas");
  for (const size of [{ width: 1280, height: 900 }, { width: 1024, height: 640 }]) {
    await page.setViewportSize(size); await frames();
    for (const kind of ["main", "data-tube-assembly-scene-viewport", "data-tube-designer-part-inspection-viewport", "data-part-drawing-viewport", "data-tube-designer-punch-viewport"]) {
      await page.evaluate(kind => {
        const workspace = document.querySelector("#workspace"); workspace.className = kind === "main" ? "tube-designer-workspace" : "";
        for (const name of workspace.getAttributeNames()) if (name.startsWith("data-")) workspace.removeAttribute(name);
        if (kind !== "main") workspace.setAttribute(kind, "");
      }, kind); await frames();
      const a = await page.locator(".cam-viewcube").boundingBox(), b = await page.locator(".icax-three-projection-toggle").boundingBox();
      assert.ok(b.y >= a.y + a.height - 1 && b.y <= a.y + a.height + 1, `${kind}: projection below cube`);
      assert.ok(Math.abs(a.x + a.width / 2 - b.x - b.width / 2) < 1, `${kind}: projection centered on cube`);
      assert.ok(b.x >= 180 && b.x + b.width <= size.width - 180 && b.height <= 30, `${kind}: navigation within viewport`);
    }
  }
  report.checks.push("Projection buttons centered immediately below cube in main and four preview contexts, desktop and small screen");
  await page.setViewportSize({ width: 1280, height: 900 });
  await page.evaluate(() => { const n = document.querySelector("#workspace"); n.className = "tube-designer-workspace"; n.removeAttribute("data-tube-designer-punch-viewport"); fixture.view.viewport.setStandardView("top-front-right"); }); await frames();
  const cubeBox = await page.locator(".cam-viewcube").boundingBox();
  await page.screenshot({ path: resolve(output, "tube-designer-navigation.png"), clip: { x: cubeBox.x - 12, y: cubeBox.y, width: cubeBox.width + 24, height: cubeBox.height + 34 } });
  assert.deepEqual(await invariant(), initial, "Navigation leaves model placement and parameters unchanged");
  assert.deepEqual(report.errors, []);
  report.status = "passed";
} catch (error) {
  report.status = "failed"; report.failure = error.stack; throw error;
} finally {
  writeFileSync(resolve(output, "report.json"), JSON.stringify(report, null, 2));
  await browser.close();
}
console.log(JSON.stringify(report, null, 2));
