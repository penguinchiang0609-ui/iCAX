// Isolated browser regression: shipped editor, renderer, picking and handlers.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
const errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.on("pageerror", error => errors.push(error.message));
  const root = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://machining.test/**", async route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><html><head></head><body></body></html>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    await route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://machining.test/");
  await page.evaluate(async () => {
    const area = await import("/src/apps/tube-designer/webpage/machiningArea.mjs");
    const editorModule = await import("/src/apps/tube-designer/webpage/machiningEditor.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML = `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial,"Microsoft Yahei",sans-serif;background:#eff4f5}#workspace{display:grid;grid-template-columns:230px minmax(0,1fr) 320px;height:100vh}aside{overflow:auto}#stage{position:relative;min-width:0;min-height:0}#scene,#overlay{position:absolute;inset:0}#overlay{pointer-events:none}#error{position:absolute;bottom:0;left:0;color:#a33;z-index:15}</style>`;
    document.body.innerHTML = '<div id="workspace"><aside id="left"></aside><main id="stage"><div id="scene"></div><div id="overlay"></div><div id="error"></div></main><aside id="right"></aside></div>';
    const job = { id: "job", sourceKind: "nesting", planId: "plan", sourceRevision: "old", name: "方管排样 · 01", analysis: { revision: "a1", independent: true,
      paths: [{ id: "cut", name: "端部切断", origin: "analysis", closed: true, points: [[0,30,0],[200,30,0],[200,30,50],[0,30,50]] },
        { id: "hole", name: "开孔轮廓", origin: "analysis", closed: true, points: [[75,30,15],[125,30,15],[125,30,35],[75,30,35]] }] } };
    const view = { activeAreaId: "machining", scene: { tubeDesigner: { machiningTask: { revision: "m1", jobs: [job] } } } };
    const context = { mount: document.body, sceneProxy: { async invoke() { throw new Error("Unexpected native call"); } } };
    const ops = { renderProject: render, showNotice() {} };
    view.viewport = createThreeViewport({ continuousRender: false, onPick: (...args) => area.handleTubeMachiningViewportPick(context, view, ...args, ops) });
    view.viewport.mount(document.querySelector("#scene"));
    function render() {
      document.querySelector("#left").innerHTML = area.renderTubeMachiningLeftPane(context, view);
      document.querySelector("#right").innerHTML = area.renderTubeMachiningRightPane(context, view);
      document.querySelector("#overlay").innerHTML = area.renderTubeMachiningViewportOverlay(context, view);
      document.querySelector("#error").textContent = view.error || "";
      area.attachTubeMachining(context, view, document.body, ops);
    }
    document.body.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target) area.handleTubeMachiningAction(context, view, target.dataset.camAction, target, ops); });
    document.body.addEventListener("change", event => { const target = event.target.closest("[data-cam-change-action]"); if (target) area.handleTubeMachiningAction(context, view, target.dataset.camChangeAction, target, ops); });
    const editor = editorModule.pathEditor(view, job);
    editor.space = "2d";
    window.check = { view, context, ops, editor, area, editorModule, render, THREE };
    render();
  });
  const clickAction = action => page.locator(`[data-cam-action="tube-path-${action}"]`).click();
  const setField = async (key, value) => { const field = page.locator(`[data-path-field="${key}"]`); await field.fill(String(value)); await field.press("Tab"); };
  const read = () => page.evaluate(() => structuredClone(window.check.editor));
  const point = async xyz => page.evaluate(xyz => {
    const { editor, editorModule } = window.check, svg = document.querySelector("[data-machining-side-canvas]"), box = svg.getBoundingClientRect();
    const [x,y] = editorModule.sideLayout(editor).project(xyz), scale = Math.min(box.width/1000, box.height/600);
    return [box.x+(box.width-1000*scale)/2+x*scale,box.y+(box.height-600*scale)/2+y*scale];
  }, xyz);
  const clickAt = async xyz => page.mouse.click(...await point(xyz));
  await page.locator('button[data-path-id="hole"]').click();
  assert.deepEqual((await read()).selectedIds, ["hole"]);
  assert.match(await page.locator(".tube-machining-path-properties").innerText(), /开孔轮廓/);
  assert.equal(await page.locator('polyline[data-path-id="hole"]').getAttribute("stroke"), "#ffd400");
  await clickAction("copy");
  const copyId = (await read()).selectedIds[0];
  await setField("dx", 30); await setField("angle", 90); await clickAction("pivot"); await clickAction("transform");
  let e = await read();
  assert.deepEqual(e.paths[1].points[0], [75,30,15]);
  assert.equal(e.paths.length, 3);
  assert.notDeepEqual(e.paths.find(p => p.id === copyId).points, e.paths[1].points);
  await clickAction("undo"); await clickAction("redo");
  await page.locator('[data-path-mode="draw"]').click();
  await setField("depth", 7);
  await clickAt([35,7,20]); await clickAt([55,7,30]);
  await clickAction("finish");
  e = await read(); assert.equal(e.paths.length,4); assert.ok(e.paths.at(-1).points.every(p => Math.abs(p[1]-7)<1e-8));
  await page.locator('[data-path-mode="break"]').click();
  await clickAt([45,7,25]);
  e = await read(); assert.equal(e.paths.length,5); assert.equal(e.selectedIds.length,2);
  await clickAction("merge"); assert.equal((await read()).paths.length,4);
  await page.locator('[data-path-mode="select"]').click();
  await clickAt([100,30,0]); assert.deepEqual((await read()).selectedIds,["cut"]);
  // Real WebGL objects and real viewport ray picking, not a synthetic hit.
  await clickAction("space");
  await page.waitForFunction(() => window.check.view.tubeDesignerMachining.preview?.ready);
  await page.evaluate(() => window.check.view.viewport.setStandardView("front"));
  const threePoint = xyz => page.evaluate(xyz => {
    const { view, THREE } = window.check, v = view.viewport;
    const p = new THREE.Vector3(...xyz).applyMatrix4(v.content.matrixWorld).project(v.camera), box = v.renderer.domElement.getBoundingClientRect();
    return [box.x+(p.x+1)*box.width/2,box.y+(1-p.y)*box.height/2];
  }, xyz);
  await page.mouse.click(...await threePoint([100,30,15]));
  assert.deepEqual((await read()).selectedIds,["hole"]);
  await page.locator('[data-path-mode="draw"]').click();
  await page.mouse.click(...await threePoint([35,7,40]));
  await page.mouse.click(...await threePoint([55,7,45]));
  await clickAction("finish");
  e = await read(); assert.equal(e.paths.length,5); assert.ok(e.paths.at(-1).points.every(p => Math.abs(p[1]-7)<1e-8));
  await clickAction("space");
  await page.locator('button[data-path-id="hole"]').click();
  const output = resolve(root,"../Temp/machining-verify-0908"); mkdirSync(output,{recursive:true});
  await page.screenshot({ path: resolve(output,"machining-editor.png") });
  for (const width of [1100,1920]) {
    await page.setViewportSize({width,height:900});
    assert.ok(await page.locator("[data-machining-side-canvas]").isVisible());
    const toolbar = await page.locator(".tube-machining-edit-toolbar").boundingBox();
    assert.ok(toolbar.width > 200 && toolbar.x+toolbar.width < width-300);
  }
  assert.deepEqual(errors,[]);
  console.log("Machining browser tests passed: real 2D/3D drawing, ray picking, list/property linkage, copy, transform, split, merge, undo/redo and responsive layout.");
} finally { await browser.close(); }
