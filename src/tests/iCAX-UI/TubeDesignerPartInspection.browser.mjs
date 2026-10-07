// Actual Edge/WebGL regression of the production inspection modal and its local controls.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = fileURLToPath(new URL("../../", import.meta.url));
const commonCss = readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css", import.meta.url), "utf8");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.setDefaultTimeout(15000);
  await page.route("http://inspection.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html; charset=utf-8", body: "<!doctype html><meta charset='utf-8'><body><nav class='product-ribbon'><button data-action='ribbon-command' id='fixture-ribbon'>后台工具栏</button></nav><div id='app'></div></body>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://inspection.test/");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}.product-ribbon{position:fixed;left:0;top:0;z-index:2}.fixture-pane{width:200px;height:160px;overflow:auto}.fixture-pane>div{height:900px}.fixture-pane input{position:sticky;top:0}#background-main-scene{position:fixed;left:0;top:360px;width:180px;height:120px;background:#16303a}#background-parts{position:fixed;bottom:0;left:0;width:100%;height:86px;overflow-y:scroll;background:#fff;z-index:1}#background-parts table{width:100%;border-collapse:collapse}#background-parts td{height:34px;padding:6px 12px;border-bottom:1px solid #ddd}#background-parts::-webkit-scrollbar{width:16px}#background-parts::-webkit-scrollbar-thumb{background:#7b8b8e}" + commonCss + tubeDesignerCss });
  await page.evaluate(async () => {
    const inspection = await import("/src/apps/tube-designer/webpage/partInspection.mjs");
    const { renderDesignerRightPane } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const { handleDesignerAreaAction, bindDesignerProductPartsInspection } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const { patchLibraryDom, rememberLibraryDom } = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { ThreeRenderViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const viewportMount = ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount = function(host) { window.fixture.viewport = this; return viewportMount.call(this, host); };
    const geometry = new THREE.BoxGeometry(2320, 38, 38);
    geometry.translate(1160, 0, 0);
    const bytes = encodeNestingGeometry({ positions: Array.from(geometry.attributes.position.array), indices: Array.from(geometry.index.array) });
    geometry.dispose();
    const makeMeasurement = (count = 17, mode = "tube") => ({
      available: true, source: "final-brep", resourceId: "fixture://final-part", resourceVersion: 9,
      length: 2320, section: { shape: "rectangle", width: 38, height: 38, wallThickness: .7 },
      linearReference: { start: [0, 0, 0], end: [2320, 0, 0], sectionAxes: [[0, 1, 0], [0, 0, 1]], dimensionOffsetDirection: [0, 0, 1] },
      features: Array.from({ length: count }, (_, i) => ({
        kind: i === 3 ? "side-opening" : "through-opening", station: 90 + i * 2140 / Math.max(1, count - 1),
        center: [90 + i * 2140 / Math.max(1, count - 1), 0, 19], shape: "circle", diameter: 19.2,
        openingSpanAlong: 19.2, openingSpanAcross: 19.2, faceTangent: [0, 1, 0],
        centerToFaceEdgeNegative: 19, centerToFaceEdgePositive: 19, faceEdgeClearanceNegative: 9.4, faceEdgeClearancePositive: 9.4,
      })),
      ...(mode === "plate" ? { partKind: "plate", plate: { center: [1160, 0, 0], axes: [[1,0,0],[0,1,0],[0,0,1]], longSide: 2320, shortSide: 38, thickness: 2 } } : {}),
      ...(mode === "accessory" ? { partKind: "accessory", bounds: { width: 30, depth: 24, height: 40 } } : {}),
    });
    let measurementResolve, measurementCalls = 0, resourceReads = 0, renders = 0;
    const requests = [];
    const mount = document.querySelector("#app");
    mount.innerHTML = "<aside class='cam-context-pane fixture-pane'><input value='左侧草稿文本'/><div></div></aside><aside class='cam-info-pane fixture-pane'><input value='右侧草稿文本'/><div></div></aside><div class='cam-viewport'><canvas id='background-main-scene' tabindex='0'></canvas></div><div id='inspection-mount'></div>";
    const context = { mount, sceneProxy: { resources: { async get() { resourceReads++; return new Response(bytes); } }, async invoke(method, payload) {
      if (method !== "TubeDesigner.MeasurePartGeometry") throw new Error("Unexpected method " + method);
      measurementCalls++;
      return new Promise((resolve) => { measurementResolve = resolve; requests.push({ resolve, partId: payload.partEntityId }); });
    } } };
    const part = { entityId: "part-1", partNumber: "防盗窗-上U形折弯框-02", name: "上U形折弯框-02", length: 2320, thumbnailGeometryResourceId: "fixture://display-part", thumbnailGeometryResourceVersion: 4, manufacturingGeometryResourceId: "fixture://final-part", manufacturingGeometryResourceVersion: 9 };
    const parts = [part, { ...part, entityId: "part-2", name: "下框-03", partNumber: "下框-03", manufacturingGeometryResourceId: "fixture://final-part-2", thumbnailGeometryResourceId: "fixture://display-part-2" }, { ...part, entityId: "part-3", name: "左框-04", partNumber: "左框-04", manufacturingGeometryResourceId: "fixture://final-part-3", thumbnailGeometryResourceId: "fixture://display-part-3" }];
    const designer = { product: { entityId: "product-1", templateId: "fixture", name: "防盗窗", parameters: {} }, members: [], manufacturingGroups: [{ productEntityId: "product-1", name: "防盗窗", parts }], nestingGroups: [{ productEntityId: "nesting", parts }] };
    const view = { activeAreaId: "view", tubeDesignerPartInspectionOpen: true, tubeDesignerInspectedPartId: part.entityId, scene: { tubeDesigner: designer }, viewport: { setContinuousRendering() {} } };
    const ops = { renderProject() {
      renders++;
      const html = renderDesignerRightPane(context, view);
      const template = document.createElement("template"); template.innerHTML = html;
      const modal = template.content.querySelector(".tube-designer-part-inspection-backdrop");
      const target = document.querySelector("#inspection-mount");
      target.replaceChildren(...(modal ? [modal] : []));
      // Production entry hydrates after the rendered dialog is connected.
      // The renderer's pre-insertion scheduling alone cannot attach native
      // inert/focus/resize controls to a dialog that does not yet exist.
      inspection.scheduleDesignerPartInspectionHydration(context, designer, view);
    } };
    mount.addEventListener("click", async (event) => {
      const target = event.target.closest("[data-cam-action]");
      if (!target) return;
      if (target.dataset.camAction === "view-standard") { window.fixture.mainCubeClicks++; return; }
      await handleDesignerAreaAction(context, view, target.dataset.camAction, target, ops);
    });
    window.fixture = { context, view, inspection, designer, ops, THREE, mainCubeClicks: 0, forbiddenClicks: 0, backgroundShortcuts: 0,
      makeMeasurement, release(count = 17, mode = "tube") { measurementResolve(makeMeasurement(count, mode)); },
      releaseRequest(index, count = 17) { const request = requests[index - 1]; request.resolve({ ...makeMeasurement(count), resourceId: parts.find((part) => part.entityId === request.partId).manufacturingGeometryResourceId }); },
      reopen(nesting = false) { view.activeAreaId = nesting ? "nesting" : "view"; view.tubeDesignerBreakdownMode = nesting ? "nesting-export" : ""; view.tubeDesignerPartInspectionOpen = true; view.tubeDesignerInspectedPartId = part.entityId; ops.renderProject(); },
      patchBackground() {
        // Retain the renderer-owned inspection outside the pane before the
        // production keyed product/library DOM patch, as the entry caller does.
        const dialog = mount.querySelector(".tube-designer-part-inspection-backdrop");
        const restore = capturePaneInteraction(mount), active = document.activeElement;
        const focused = dialog.contains(active);
        const scrolls = [dialog, ...dialog.querySelectorAll("*")].filter((node) => node.scrollTop || node.scrollLeft).map((node) => [node, node.scrollTop, node.scrollLeft]);
        if (dialog.parentElement !== mount) mount.appendChild(dialog);
        if (focused) active.focus({ preventScroll: true });
        restore();
        for (const [node, top, left] of scrolls) { node.scrollTop = top; node.scrollLeft = left; }
        rememberLibraryDom(view, mount, "", context.sceneProxy);
        designer.product.name += " · 刷新";
        const latest = capturePaneInteraction(mount);
        const right = `<input value="右侧草稿文本"/><div></div>` + renderDesignerRightPane(context, view, { includePartInspection: false });
        const patched = patchLibraryDom(view, mount, { left: mount.querySelector(".cam-context-pane").innerHTML, right, overlay: "", suffix: "", sceneProxy: context.sceneProxy });
        latest();
        return patched;
      },
      get calls() { return measurementCalls; }, get reads() { return resourceReads; }, get renders() { return renders; },
    };
    const background = document.createElement("div"); background.id = "background-parts"; background.className = "tube-designer-product-parts-table";
    background.classList.add('tube-designer-sheet-wrap');
    background.innerHTML = `<table><tbody>${parts.map((part) => `<tr tabindex="0" data-tube-designer-product-part-row="${part.entityId}" data-tube-designer-part-id="${part.entityId}"><td>${part.name}</td></tr>`).join("")}${Array.from({length:40},(_,i)=>`<tr><td>后台清单行 ${i+1}</td></tr>`).join("")}</tbody></table>`;
    const backgroundWrap = document.createElement('section'); backgroundWrap.className = 'tube-designer-breakdown-body'; backgroundWrap.appendChild(background); mount.appendChild(backgroundWrap);
    bindDesignerProductPartsInspection(context, view, ops);
    mount.querySelector(".cam-info-pane").insertAdjacentHTML("beforeend", renderDesignerRightPane(context, view, { includePartInspection: false }));
    for (const node of [document.querySelector('#fixture-ribbon'), ...mount.querySelectorAll('.fixture-pane > input'), document.querySelector('#background-main-scene')]) node.addEventListener('click', () => window.fixture.forbiddenClicks++);
    document.body.addEventListener('keydown', (event) => { if ((event.ctrlKey || event.metaKey) && ['s','o'].includes(event.key.toLowerCase())) { event.preventDefault(); window.fixture.backgroundShortcuts++; } });
    ops.renderProject();
  });
  const viewport = page.locator("[data-tube-designer-part-inspection-viewport]");
  const master = page.locator("[data-tube-inspection-all-dimensions]");
  const close = page.locator('[data-cam-action="tube-designer-close-part-inspection"]');
  const eye = (index) => page.locator(`[data-tube-inspection-element-visibility="hole:${index}"]`);
  const bulk = page.locator("[data-tube-inspection-elements-visibility]");
  const select = (index) => page.locator(`[data-tube-inspection-select-element="hole:${index}"]`);
  const category = (key) => page.locator(`[data-tube-inspection-dimension-category="${key}"]`);
  const uncheckCategory = async key => {
    const input = category(key);
    // A mixed HTML checkbox is unchecked but still has visible children.
    // Clicking it first enables all children, then another click hides them.
    if (await input.evaluate(node => node.indeterminate)) await input.check();
    await input.uncheck();
  };
  const tree = page.locator("[data-tube-inspection-dimension-tree]");
  const collapseTree = page.locator("[data-tube-inspection-tree-collapse]");
  const visibleCount = page.locator("[data-tube-inspection-visible-count]");
  const waitForRequest = (count) => page.waitForFunction((expected) => window.fixture.calls === expected, count);
  const ready = () => page.waitForFunction(() => document.querySelector("[data-tube-designer-part-inspection-viewport]")?.dataset.tubeInspectionProgressVisible === "false");
  const visibleIds = () => viewport.evaluate((host) => JSON.parse(host.dataset.tubeInspectionVisibleAnnotationIds));
  const screenshot = async (name) => { if (process.env.ICAX_ARTIFACT_DIR) { mkdirSync(process.env.ICAX_ARTIFACT_DIR, { recursive: true }); await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, name + ".png") }); } };
  const closeIsPainted = async () => {
    assert.equal(await close.isVisible(), true);
    assert.deepEqual(await close.evaluate((button) => { const r = button.getBoundingClientRect(), svg = button.querySelector("svg"); return { inside: r.left >= 0 && r.right <= innerWidth && r.top >= 0 && r.bottom <= innerHeight, hit: document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2) === button, stroke: getComputedStyle(svg).stroke !== "none" }; }), { inside: true, hit: true, stroke: true });
  };
  const allowedFocus = () => page.evaluate(() => Boolean(document.activeElement?.closest('.tube-designer-part-inspection-dialog,.tube-designer-product-parts-table')));
  const dragResize = async (edge, dx, dy) => {
    const handle = page.locator(`[data-tube-inspection-resize="${edge}"]`), bounds = await handle.boundingBox();
    assert.ok(bounds && bounds.width > 0 && bounds.height > 0, `The ${edge} resize handle is painted`);
    const x = bounds.x + bounds.width / 2, y = bounds.y + bounds.height / 2;
    await page.mouse.move(x, y); await page.mouse.down(); await page.mouse.move(x + dx, y + dy, { steps: 8 }); await page.mouse.up();
    await page.evaluate(() => new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done))));
  };
  const inspectionBounds = () => page.locator('.tube-designer-part-inspection-dialog').evaluate((node) => { const r = node.getBoundingClientRect(); return { left:r.left, top:r.top, width:r.width, height:r.height, right:r.right, bottom:r.bottom }; });
  const blockedBackground = async () => {
    await close.focus();
    for (const selector of ['#fixture-ribbon', '.cam-context-pane > input', '.cam-info-pane > input', '#background-main-scene']) {
      const bounds = await page.locator(selector).boundingBox();
      await page.mouse.click(bounds.x + Math.min(12, bounds.width / 2), bounds.y + Math.min(12, bounds.height / 2));
      assert.equal(await allowedFocus(), true, 'Blocked background clicks cannot move focus outside the inspection/table scope');
    }
    assert.equal(await page.evaluate(() => window.fixture.forbiddenClicks), 0, 'Toolbar outside the product mount, both sidebar fields and main scene reject real mouse clicks');
    const left = page.locator('.cam-context-pane'), bounds = await left.boundingBox(), before = await left.evaluate((node) => node.scrollTop);
    await page.mouse.move(bounds.x + 30, bounds.y + 80); await page.mouse.wheel(0, 250); await page.waitForTimeout(80);
    assert.equal(await left.evaluate((node) => node.scrollTop), before, 'The sidebar cannot scroll through the inspection backdrop');
    for (const selector of ['#fixture-ribbon', '.cam-context-pane > input', '.cam-info-pane > input', '#background-main-scene']) {
      await page.locator(selector).evaluate((node) => node.focus({ preventScroll: true }));
      assert.equal(await allowedFocus(), true, 'Programmatic focus cannot activate a locked background control');
    }
    await close.focus();
    let tableFocused = false;
    let treeCollapseFocused = false;
    for (let i = 0; i < 64; i++) {
      await page.keyboard.press('Tab');
      assert.equal(await allowedFocus(), true, 'Tab navigation stays within inspection and the allowed background parts table');
      tableFocused ||= await page.evaluate(() => Boolean(document.activeElement?.closest('.tube-designer-product-parts-table')));
      if (!treeCollapseFocused && await page.evaluate(() => document.activeElement === document.querySelector('[data-tube-inspection-tree-collapse]'))) {
        treeCollapseFocused = true;
        const unchanged = await page.evaluate(() => { const f = window.fixture; f.summaryCanvas = document.querySelector('.icax-three-viewport-canvas'); f.treeButton = document.activeElement; return JSON.stringify(f.viewport.getCameraState()); });
        await page.keyboard.press('Enter');
        assert.equal(await tree.evaluate((node) => node.classList.contains('is-collapsed')), true, 'The ruler tree collapses horizontally into the scene left edge by keyboard');
        assert.equal(await collapseTree.getAttribute('aria-expanded'), 'false');
        assert.equal(await page.locator('[data-tube-inspection-tree-collapse-icon]').innerText(), '›');
        assert.equal(await tree.evaluate((node) => { const r=node.getBoundingClientRect(),h=node.closest('.tube-designer-part-inspection-canvas').getBoundingClientRect(); return Math.abs(r.left-h.left)<2 && r.width<=35; }), true);
        await page.keyboard.press('Enter');
        assert.equal(await tree.evaluate((node) => node.classList.contains('is-collapsed')), false);
        assert.equal(await collapseTree.getAttribute('aria-expanded'), 'true');
        assert.equal(await page.locator('[data-tube-inspection-tree-collapse-icon]').innerText(), '‹');
        assert.equal(await page.evaluate(() => JSON.stringify(window.fixture.viewport.getCameraState())), unchanged);
        assert.equal(await page.evaluate(() => window.fixture.summaryCanvas === document.querySelector('.icax-three-viewport-canvas')), true);
        assert.equal(await page.evaluate(() => document.activeElement === window.fixture.treeButton && window.fixture.treeButton === document.querySelector('[data-tube-inspection-tree-collapse]')), true, 'Keyboard collapse keeps the same focused control');
      }
    }
    assert.equal(tableFocused, true, 'The allowed background parts rows remain keyboard reachable');
    assert.equal(treeCollapseFocused, true, 'The ruler tree collapse button remains in the inspection Tab scope');
    for (let i = 0; i < 8; i++) { await page.keyboard.press('Shift+Tab'); assert.equal(await allowedFocus(), true); }
    for (const selector of ['[data-cam-action="tube-designer-close-part-inspection"]', '[data-tube-designer-product-part-row="part-1"]']) {
      await page.locator(selector).focus(); await page.keyboard.press('Control+s'); await page.keyboard.press('Control+o');
    }
    assert.equal(await page.evaluate(() => window.fixture.backgroundShortcuts), 0, 'Inspection and the allowed table cannot trigger background save/open shortcuts');
  };

  await waitForRequest(1);
  await closeIsPainted();
  assert.match(await visibleCount.innerText(), /正在载入/);
  await master.uncheck();
  await page.evaluate(() => {
    const left = document.querySelector(".cam-context-pane"), right = document.querySelector(".cam-info-pane");
    left.scrollTop = 140; right.scrollTop = 220;
    const input = document.querySelector('[data-tube-inspection-all-dimensions]'); input.focus({ preventScroll: true });
    window.fixture.focusedInput = input; window.fixture.initialCanvas = document.querySelector(".icax-three-viewport-canvas");
    window.fixture.release(); left.scrollTop = 170; right.scrollTop = 280;
  });
  await ready();
  assert.equal(await viewport.getAttribute("data-tube-inspection-dimension-count"), "0", "Hiding during the request is valid and remains hidden");
  assert.equal(await master.isChecked(), false);
  assert.equal(await page.locator("[data-tube-designer-inspection-status].error").count(), 0);
  assert.deepEqual(await page.evaluate(() => ({ focus: document.activeElement === window.fixture.focusedInput, left: document.querySelector(".cam-context-pane").scrollTop, right: document.querySelector(".cam-info-pane").scrollTop, canvas: document.querySelector(".icax-three-viewport-canvas") === window.fixture.initialCanvas })), { focus: true, left: 170, right: 280, canvas: true });
  assert.equal(await page.locator(".tube-designer-part-inspection-toolbar,.tube-designer-measurement-help").count(), 0);
  assert.equal(await page.locator(".tube-designer-part-inspection-dialog").getAttribute("aria-modal"), null, "Inspection shares its allowed focus scope with the background parts table");
  assert.doesNotMatch(await page.locator(".tube-designer-measurement-panel").innerText(), /数据来源|视图操作|右键拖动|最终几何/);
  assert.equal(await page.locator("[data-tube-inspection-element-row]").count(), 17);
  assert.equal(await page.locator('[data-cam-action="tube-designer-toggle-automatic-dimensions"]').count(), 0, "The duplicate right-side ruler toggle is removed");
  assert.doesNotMatch(await page.locator(".tube-designer-measurement-panel").innerText(), /隐藏标尺|显示标尺/);
  assert.equal(await page.locator(".tube-designer-inspection-element-list table thead th").count(), 3);
  assert.match(await page.locator(".tube-designer-inspection-element-list thead").innerText(), /孔\s*\/\s*开口[\s\S]*尺寸/);
  assert.equal(await bulk.getAttribute("aria-pressed"), "false", "The unchecked master hides every element while delayed measurements finish");
  assert.equal(await page.locator('[data-tube-inspection-dimension-category]:checked').count(), 0);
  assert.equal(await page.locator('[data-tube-inspection-element-visibility][aria-pressed="false"]').count(), 17);
  assert.equal(await bulk.isEnabled(), true);
  assert.match(await visibleCount.innerText(), /已显示\s+0\s+项/);
  assert.equal(await page.locator(".tube-designer-inspection-summary").count(), 1);
  assert.equal(await page.locator(".tube-designer-part-inspection-dialog .tube-designer-dimension-overview").count(), 0);
  assert.match(await page.locator(".tube-designer-inspection-summary").innerText(), /总长\s+2,320 mm[\s\S]*孔 \/ 开口\s+17 个[\s\S]*截面 38 × 38 × 壁厚 0\.7 mm/);
  assert.equal(await page.locator(".tube-designer-part-inspection-dialog .tube-designer-dialog-header > div > span").count(), 0);
  assert.doesNotMatch(await page.locator(".tube-designer-part-inspection-dialog .tube-designer-dialog-header").innerText(), /尺寸以当前版本|防盗窗-/);
  assert.equal(await page.locator(".tube-designer-dimension-list article").count(), 0);
  assert.equal(await page.locator("[data-tube-inspection-dimension-category]").count(), 6);
  await blockedBackground();
  await master.check();
  const allIds = await visibleIds();
  assert.equal(allIds.length, 69);
  assert.match(await visibleCount.innerText(), /已显示\s+69\s+项/, 'The count explicitly describes visible ruler annotations, not holes');
  await select(3).click();
  assert.equal(await viewport.getAttribute("data-tube-inspection-selected-element"), "hole:3");
  assert.match(await page.locator("[data-tube-inspection-element-detail]").innerText(), /孔 3|中心距端/);
  assert.equal(await page.evaluate(() => window.fixture.viewport.measurementContent.children.length), 2);
  await page.evaluate(() => { window.fixture.camera = window.fixture.viewport.getCameraState(); window.fixture.canvas = document.querySelector(".icax-three-viewport-canvas"); window.fixture.eye3 = document.querySelector('[data-tube-inspection-element-visibility="hole:3"]'); });
  await eye(3).click();
  assert.equal(await eye(3).getAttribute("aria-pressed"), "false");
  assert.equal(await bulk.getAttribute("aria-pressed"), "mixed");
  assert.notEqual(await eye(3).locator("[data-tube-inspection-eye-slash]").evaluate((path) => getComputedStyle(path).display), "none", "The crossed eye is visibly painted after hiding a row");
  assert.equal(await page.evaluate(() => window.fixture.viewport.measurementContent.children.length), 0);
  const hiddenThird = await visibleIds();
  assert.equal(hiddenThird.length, 63, "One hole hides its size/distance annotations and both adjacent center/gap pairs");
  await uncheckCategory("opening-size");
  if (await eye(3).getAttribute('aria-pressed') === 'true') await eye(3).click();
  await eye(3).click();
  assert.equal(await bulk.getAttribute("aria-pressed"), "true");
  assert.equal(await eye(3).locator("[data-tube-inspection-eye-slash]").evaluate((path) => getComputedStyle(path).display), "none");
  assert.equal((await visibleIds()).filter((id) => id.startsWith("opening-size:")).length, 1, 'An explicitly shown opening displays its own size without enabling every opening');
  assert.equal(await category('opening-size').evaluate(input => input.indeterminate), true);
  await category("opening-size").check();
  assert.deepEqual(await visibleIds(), allIds);
  for (const key of ["overall", "opening-size", "end-distance", "center-distance", "clearance", "face-distance"]) await uncheckCategory(key);
  assert.equal((await visibleIds()).length, 0, "All category filters may legitimately hide every ruler");
  assert.equal(await page.locator("[data-tube-designer-inspection-status].error").count(), 0);
  for (const key of ["overall", "opening-size", "end-distance", "center-distance", "clearance", "face-distance"]) await category(key).check();
  await eye(3).click();
  await select(3).click();
  assert.equal(await eye(3).getAttribute("aria-pressed"), "true", "Selecting a hidden element reveals its own relevant annotations");
  assert.equal(await page.evaluate(() => window.fixture.viewport.measurementContent.children.length), 2);
  await eye(3).click();
  await uncheckCategory("center-distance");
  if (await eye(3).getAttribute('aria-pressed') === 'true') await eye(3).click();
  assert.equal(await bulk.getAttribute("aria-pressed"), "mixed");
  await bulk.click();
  assert.equal(await bulk.getAttribute("aria-pressed"), "true", "A partially visible list switches all rows on");
  assert.equal(await eye(3).getAttribute("aria-pressed"), "true");
  assert.equal(await category("center-distance").isChecked(), true);
  assert.equal((await visibleIds()).some((id) => id.startsWith("center-distance:")), true, "Batch show reflects every opening's restored annotations in its category checkbox");
  assert.equal(await viewport.getAttribute("data-tube-inspection-selected-element"), "hole:3");
  await bulk.click();
  assert.equal(await bulk.getAttribute("aria-pressed"), "false");
  assert.deepEqual(await visibleIds(), ["overall:0"], "Batch hiding element rulers preserves the unrelated total length");
  assert.equal(await page.evaluate(() => window.fixture.viewport.measurementContent.children.length), 0);
  assert.equal(await page.locator('[data-tube-inspection-element-visibility][aria-pressed="false"]').count(), 17);
  assert.equal(await viewport.getAttribute("data-tube-inspection-selected-element"), "hole:3", "Batch visibility does not clear the selected element");
  await screenshot("inspection-batch-hidden");
  await eye(3).click();
  assert.equal(await bulk.getAttribute("aria-pressed"), "mixed");
  assert.equal(await eye(3).getAttribute("aria-pressed"), "true");
  assert.equal(await eye(4).getAttribute("aria-pressed"), "false");
  assert.equal((await visibleIds()).filter((id) => id.startsWith("opening-size:")).length, 1, "Showing one row restores only that opening's size ruler");
  assert.equal((await visibleIds()).some((id) => id.startsWith("center-distance:")), false);
  await bulk.click();
  assert.equal(await bulk.getAttribute("aria-pressed"), "true");
  await master.uncheck();
  assert.equal(await bulk.getAttribute("aria-pressed"), "false");
  assert.equal(await page.locator('[data-tube-inspection-dimension-category]:checked').count(), 0);
  assert.equal((await visibleIds()).length, 0);
  await bulk.click();
  assert.equal(await bulk.getAttribute("aria-pressed"), "true");
  assert.equal(await master.isChecked(), true, "Explicitly showing all openings reopens their effective master visibility");
  assert.equal(await category("center-distance").isChecked(), true);
  await master.uncheck(); await master.check();
  assert.deepEqual(await visibleIds(), allIds);
  assert.deepEqual(await page.evaluate(() => ({ camera: JSON.stringify(window.fixture.camera) === JSON.stringify(window.fixture.viewport.getCameraState()), canvas: window.fixture.canvas === document.querySelector(".icax-three-viewport-canvas"), eye: window.fixture.eye3 === document.querySelector('[data-tube-inspection-element-visibility="hole:3"]'), calls: window.fixture.calls, reads: window.fixture.reads, renders: window.fixture.renders })), { camera: true, canvas: true, eye: true, calls: 1, reads: 1, renders: 1 });

  const holePoint = await page.evaluate(() => {
    const f = window.fixture, point = new f.THREE.Vector3(...f.makeMeasurement().features[8].center).applyMatrix4(f.viewport.content.matrixWorld).project(f.viewport.camera), bounds = f.viewport.renderer.domElement.getBoundingClientRect();
    return { x: bounds.left + (point.x + 1) * bounds.width / 2, y: bounds.top + (1 - point.y) * bounds.height / 2 };
  });
  await page.mouse.click(holePoint.x, holePoint.y);
  assert.equal(await viewport.getAttribute("data-tube-inspection-selected-element"), "hole:9", "Clicking the actual projected opening selects its list row");
  const cube = page.locator('[data-tube-designer-part-inspection-viewport] [data-cam-viewcube]');
  await cube.locator('.cam-viewcube-piece-center').first().click();
  assert.equal(await page.evaluate(() => window.fixture.mainCubeClicks), 0, "Inspection view cube never controls the main viewport");
  assert.notEqual(await page.evaluate(() => JSON.stringify(window.fixture.viewport.getCameraState())), await page.evaluate(() => JSON.stringify(window.fixture.camera)));
  await page.evaluate(() => window.fixture.viewport.setStandardView("iso"));
  await cube.locator('.cam-viewcube-piece-center').first().focus();
  await page.keyboard.press("Enter");
  assert.equal(await page.evaluate(() => window.fixture.mainCubeClicks), 0);
  await page.evaluate(() => { window.fixture.viewport.setStandardView("iso"); window.fixture.viewport.fitViewToViewport(1.16); });
  await uncheckCategory("center-distance");
  await uncheckCategory("face-distance");
  await screenshot("inspection-compact-elements");
  for (const size of [{ width: 1600, height: 1000 }, { width: 1024, height: 768 }, { width: 780, height: 820 }]) {
    await page.setViewportSize(size); await page.evaluate(() => new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)))); await closeIsPainted();
    assert.equal(await cube.isVisible(), true);
    assert.equal(await page.locator(".tube-designer-inspection-dimension-tree").isVisible(), true);
    assert.equal(await page.locator(".tube-designer-measurement-panel").evaluate((panel) => panel.scrollWidth <= panel.clientWidth + 2), true);
    assert.equal(await page.locator(".tube-designer-inspection-summary").evaluate((card) => card.scrollWidth <= card.clientWidth + 2 && card.getBoundingClientRect().height <= 70), true, "The single two-line information card is compact without losing measurements");
  }
  await close.click(); await page.waitForFunction(() => !window.fixture.view.tubeDesignerPartInspectionOpen);
  assert.equal(await page.locator(".tube-designer-part-inspection-dialog").count(), 0);
  assert.equal(await page.evaluate(() => window.fixture.viewport.isDisposed), true);
  await page.locator('#fixture-ribbon').click();
  assert.equal(await page.evaluate(() => window.fixture.forbiddenClicks), 1, 'Closing the inspection immediately restores background toolbar interaction');
  await page.keyboard.press('Control+s');
  assert.equal(await page.evaluate(() => window.fixture.backgroundShortcuts), 1, 'Closing also restores the normal application shortcut route');
  await page.locator('.cam-info-pane > input').focus();
  assert.equal(await page.evaluate(() => document.activeElement === document.querySelector('.cam-info-pane > input')), true, 'Closing releases the sidebar focus lock');

  await page.setViewportSize({ width: 1600, height: 1000 });
  await page.evaluate(() => window.fixture.reopen()); await waitForRequest(2); await page.evaluate(() => window.fixture.release(80)); await ready();
  await eye(78).scrollIntoViewIfNeeded(); await eye(78).click();
  await page.evaluate(() => {
    const list = document.querySelector(".tube-designer-inspection-element-list"), panel = document.querySelector(".tube-designer-measurement-panel");
    window.fixture.list = list; window.fixture.listScroll = list.scrollTop; window.fixture.panelScroll = panel.scrollTop;
    window.fixture.eye78 = document.querySelector('[data-tube-inspection-element-visibility="hole:78"]');
    window.fixture.eye78.focus({ preventScroll: true }); window.fixture.camera = window.fixture.viewport.getCameraState(); window.fixture.canvas = document.querySelector(".icax-three-viewport-canvas");
    const master = document.querySelector('[data-tube-inspection-all-dimensions]');
    master.checked = false; master.dispatchEvent(new Event('change', { bubbles:true }));
    master.checked = true; master.dispatchEvent(new Event('change', { bubbles:true }));
  });
  assert.deepEqual(await page.evaluate(() => ({ focus: document.activeElement === window.fixture.eye78, list: window.fixture.list === document.querySelector(".tube-designer-inspection-element-list"), listScroll: document.querySelector(".tube-designer-inspection-element-list").scrollTop === window.fixture.listScroll, panelScroll: document.querySelector(".tube-designer-measurement-panel").scrollTop === window.fixture.panelScroll, canvas: window.fixture.canvas === document.querySelector(".icax-three-viewport-canvas"), camera: JSON.stringify(window.fixture.camera) === JSON.stringify(window.fixture.viewport.getCameraState()) })), { focus: true, list: true, listScroll: true, panelScroll: true, canvas: true, camera: true });
  await select(78).click(); assert.equal(await page.locator('[data-tube-inspection-element-row="hole:78"]').evaluate((row) => row.classList.contains("is-selected")), true);
  await eye(78).click(); // Keep the current row selected while exercising a partially visible catalogue.
  await page.evaluate(() => {
    const f=window.fixture, panel=document.querySelector('.tube-designer-measurement-panel');
    const left=document.querySelector('.cam-context-pane'), right=document.querySelector('.cam-info-pane');
    left.scrollTop=180;right.scrollTop=300;panel.scrollTop=24;
    f.bulk=document.querySelector('[data-tube-inspection-elements-visibility]');
    f.bulkTable=f.bulk.closest('table');f.bulkHeader=f.bulk.closest('thead');f.bulkBody=f.bulkTable.querySelector('tbody');
    f.batchMaster=document.querySelector('[data-tube-inspection-all-dimensions]');
    f.batchCategory=document.querySelector('[data-tube-inspection-dimension-category="opening-size"]');
    f.leftInput=left.querySelector('input');f.rightInput=right.querySelector('input');
    f.leftInput.setSelectionRange(2,6);f.rightInput.setSelectionRange(1,5);
    f.batchScroll={list:f.list.scrollTop,panel:panel.scrollTop,left:left.scrollTop,right:right.scrollTop};
    f.batchCamera=JSON.stringify(f.viewport.getCameraState());f.batchCalls=f.calls;f.batchReads=f.reads;f.batchRenders=f.renders;
    f.bulkPointerUps=0;f.eyePointerUps=0;
    f.bulk.addEventListener('pointerup',()=>f.bulkPointerUps++);
    f.eye78.addEventListener('pointerup',()=>f.eyePointerUps++);
    f.treeControl=document.querySelector('[data-tube-inspection-tree-collapse]');
  });
  const batchInteraction = async (focused) => assert.deepEqual(await page.evaluate((kind) => {
    const f=window.fixture,panel=document.querySelector('.tube-designer-measurement-panel');
    return {focus:document.activeElement===(kind==='bulk'?f.bulk:kind==='tree'?f.treeControl:f.eye78),
      canvas:f.canvas===document.querySelector('.icax-three-viewport-canvas'),camera:JSON.stringify(f.viewport.getCameraState())===f.batchCamera,
      list:f.list===document.querySelector('.tube-designer-inspection-element-list'),header:f.bulkHeader===document.querySelector('.tube-designer-inspection-element-list thead'),
      body:f.bulkBody===document.querySelector('.tube-designer-inspection-element-list tbody'),bulk:f.bulk===document.querySelector('[data-tube-inspection-elements-visibility]'),
      master:f.batchMaster===document.querySelector('[data-tube-inspection-all-dimensions]'),category:f.batchCategory===document.querySelector('[data-tube-inspection-dimension-category="opening-size"]'),
      scroll:{list:f.list.scrollTop,panel:panel.scrollTop,left:document.querySelector('.cam-context-pane').scrollTop,right:document.querySelector('.cam-info-pane').scrollTop},
      savedScroll:f.batchScroll,leftInput:f.leftInput===document.querySelector('.cam-context-pane input'),rightInput:f.rightInput===document.querySelector('.cam-info-pane input'),
      selections:[f.leftInput.selectionStart,f.leftInput.selectionEnd,f.rightInput.selectionStart,f.rightInput.selectionEnd],
      selected:document.querySelector('[data-tube-inspection-element-row="hole:78"]').classList.contains('is-selected'),
      calls:f.calls===f.batchCalls,reads:f.reads===f.batchReads,renders:f.renders===f.batchRenders};
  },focused), {focus:true,canvas:true,camera:true,list:true,header:true,body:true,bulk:true,master:true,category:true,
    scroll:await page.evaluate(()=>window.fixture.batchScroll),savedScroll:await page.evaluate(()=>window.fixture.batchScroll),
    leftInput:true,rightInput:true,selections:[2,6,1,5],selected:true,calls:true,reads:true,renders:true}, `${focused} updates retain live controls, selection, view and all scroll positions`);
  assert.equal(await bulk.getAttribute('aria-pressed'),'mixed');
  await bulk.click();assert.equal(await bulk.getAttribute('aria-pressed'),'true');await batchInteraction('bulk');
  await bulk.click();assert.equal(await bulk.getAttribute('aria-pressed'),'false');await batchInteraction('bulk');
  assert.deepEqual(await visibleIds(),['overall:0']);
  await eye(78).click();assert.equal(await bulk.getAttribute('aria-pressed'),'mixed');await batchInteraction('eye');
  await bulk.click();assert.equal(await bulk.getAttribute('aria-pressed'),'true');await batchInteraction('bulk');
  await eye(78).click();assert.equal(await bulk.getAttribute('aria-pressed'),'mixed');await batchInteraction('eye');
  assert.deepEqual(await page.evaluate(()=>({bulk:window.fixture.bulkPointerUps,eye:window.fixture.eyePointerUps})),{bulk:3,eye:2},'Listeners attached to the original header and eye nodes survive every batch patch');
  await collapseTree.click();
  assert.equal(await tree.evaluate((node)=>node.classList.contains('is-collapsed')),true);
  assert.equal(await collapseTree.getAttribute('aria-expanded'),'false');
  await batchInteraction('tree');
  assert.equal(await eye(78).getAttribute('aria-pressed'),'false');
  assert.equal(await bulk.getAttribute('aria-pressed'),'mixed');
  await collapseTree.click();assert.equal(await tree.evaluate((node)=>node.classList.contains('is-collapsed')),false);await batchInteraction('tree');
  assert.equal(await category('opening-size').isChecked(),false);
  assert.equal(await category('opening-size').evaluate(input => input.indeterminate),true);
  assert.equal(await page.locator('[data-tube-inspection-resize]').count(), 8);
  assert.ok(await page.locator('.tube-designer-part-inspection-dialog').evaluate((node) => { const opacity = Number(getComputedStyle(node).opacity); return opacity > .5 && opacity < 1; }), 'The inspection is actually translucent while controls remain legible');
  await dragResize('e', 240, 0); await dragResize('s', 0, 80);
  await page.evaluate(() => {
    const f = window.fixture, panel = document.querySelector('.tube-designer-measurement-panel');
    f.list.scrollTop = 400; f.resizeListScroll = f.list.scrollTop; f.resizePanelScroll = panel.scrollTop;
    f.eye78.focus({ preventScroll:true }); f.resizeCamera = JSON.stringify(f.viewport.getCameraState()); f.resizeCalls = f.calls; f.resizeReads = f.reads; f.resizeRenders = f.renders;
  });
  for (const [edge, dx, dy] of [['n',0,18],['ne',-18,18],['e',-18,0],['se',-18,-18],['s',0,-18],['sw',18,-18],['w',18,0],['nw',18,18]]) {
    const before = await inspectionBounds(); await dragResize(edge, dx, dy); const after = await inspectionBounds();
    if (edge.includes('e') || edge.includes('w')) assert.ok(after.width < before.width - 2, `Dragging ${edge} changes the width`);
    if (edge.includes('n') || edge.includes('s')) assert.ok(after.height < before.height - 2, `Dragging ${edge} changes the height`);
    assert.deepEqual(await page.evaluate(() => ({ focus:document.activeElement===window.fixture.eye78, canvas:window.fixture.canvas===document.querySelector('.icax-three-viewport-canvas'), list:window.fixture.list===document.querySelector('.tube-designer-inspection-element-list'), camera:JSON.stringify(window.fixture.viewport.getCameraState())===window.fixture.resizeCamera, listScroll:window.fixture.list.scrollTop===window.fixture.resizeListScroll, panelScroll:document.querySelector('.tube-designer-measurement-panel').scrollTop===window.fixture.resizePanelScroll, selected:document.querySelector('[data-tube-inspection-element-row="hole:78"]').classList.contains('is-selected'), calls:window.fixture.calls===window.fixture.resizeCalls, reads:window.fixture.reads===window.fixture.resizeReads, renders:window.fixture.renders===window.fixture.resizeRenders })), { focus:true, canvas:true, list:true, camera:true, listScroll:true, panelScroll:true, selected:true, calls:true, reads:true, renders:true }, `Resize ${edge} keeps the existing view and list interaction`);
    assert.equal(await viewport.evaluate((host) => { const canvas = host.querySelector('canvas.icax-three-viewport-canvas'), r = canvas.getBoundingClientRect(), h = host.getBoundingClientRect(); return Math.abs(r.width - h.width) < 2 && Math.abs(r.height - h.height) < 2; }), true, 'The same WebGL canvas follows the new host size');
  }
  await dragResize('e', 5000, 0); await dragResize('s', 0, 5000);
  let bounded = await inspectionBounds();
  assert.ok(bounded.left >= 11 && bounded.top >= 11 && bounded.right <= 1589 && bounded.bottom <= 989, 'Large outward drags stay inside the browser with a usable margin');
  await dragResize('nw', 5000, 5000); bounded = await inspectionBounds();
  assert.ok(bounded.width >= 639 && bounded.height >= 359 && bounded.width <= 641 && bounded.height <= 361, 'Large inward drags stop at a usable 640 × 360 minimum');
  assert.equal(await page.evaluate(() => { const tree = document.querySelector('.tube-designer-inspection-dimension-tree').getBoundingClientRect(), cube = document.querySelector('[data-tube-designer-part-inspection-viewport] [data-cam-viewcube]').getBoundingClientRect(); return tree.right <= cube.left; }), true, 'The tree and view cube remain separately usable at the minimum window width');
  await closeIsPainted();
  await screenshot('inspection-resize-minimum');
  await page.evaluate(() => {
    const f=window.fixture;
    f.eye78.focus({preventScroll:true});
    f.listScroll=f.list.scrollTop;f.panelScroll=document.querySelector(".tube-designer-measurement-panel").scrollTop;
    f.patchCalls=f.calls;f.patchReads=f.reads;
    document.querySelector(".cam-context-pane").scrollTop=195;document.querySelector(".cam-info-pane").scrollTop=305;
    f.patchResult=f.patchBackground();
  });
  await page.waitForTimeout(100);
  assert.equal(await page.locator(".tube-designer-part-inspection-backdrop").count(),1,"A real background product DOM patch cannot add a second inspection window");
  assert.deepEqual(await page.evaluate(() => ({patched:window.fixture.patchResult,focus:document.activeElement===window.fixture.eye78,list:window.fixture.list===document.querySelector(".tube-designer-inspection-element-list"),listScroll:window.fixture.list.scrollTop===window.fixture.listScroll,panelScroll:document.querySelector(".tube-designer-measurement-panel").scrollTop===window.fixture.panelScroll,canvas:window.fixture.canvas===document.querySelector(".icax-three-viewport-canvas"),camera:JSON.stringify(window.fixture.camera)===JSON.stringify(window.fixture.viewport.getCameraState()),left:document.querySelector(".cam-context-pane").scrollTop,right:document.querySelector(".cam-info-pane").scrollTop,calls:window.fixture.calls===window.fixture.patchCalls,reads:window.fixture.reads===window.fixture.patchReads,changed:document.querySelector(".cam-info-pane .tube-designer-heading strong").textContent.endsWith("刷新")})),{patched:true,focus:true,list:true,listScroll:true,panelScroll:true,canvas:true,camera:true,left:195,right:305,calls:true,reads:true,changed:true});
  assert.equal(await page.locator('[data-tube-inspection-element-row="hole:78"]').evaluate((row) => row.classList.contains("is-selected")),true);
  await close.click();

  await page.evaluate(() => window.fixture.reopen()); await waitForRequest(3); await page.evaluate(() => window.fixture.release(1)); await ready();
  const singleIds = await visibleIds();
  assert.equal(new Set(singleIds).size, singleIds.length, "The same single hole retains distinct start/end distance annotation IDs");
  assert.equal(singleIds.filter((id) => id.startsWith("end-distance:")).length, 2);
  assert.equal(await bulk.getAttribute('aria-pressed'),'true');
  await bulk.click();assert.equal(await bulk.getAttribute('aria-pressed'),'false');assert.deepEqual(await visibleIds(),['overall:0']);
  await bulk.click();assert.equal(await bulk.getAttribute('aria-pressed'),'true');assert.deepEqual(await visibleIds(),singleIds);
  await close.click();
  await page.evaluate(() => window.fixture.reopen()); await waitForRequest(4); await page.evaluate(() => window.fixture.release(1, "plate")); await ready();
  assert.equal(await page.locator("[data-tube-inspection-dimension-category]").count(), 1);
  assert.equal(await eye(1).count(), 0, "A plate opening without corresponding rulers does not claim to toggle nonexistent annotations");
  if(await bulk.count())assert.equal(await bulk.isDisabled(), true, "A plate cannot batch-toggle nonexistent hole rulers");
  assert.equal((await visibleIds()).length, 3);
  assert.match(await page.locator(".tube-designer-inspection-summary").innerText(), /长边\s+2,320 mm[\s\S]*板件 2,320 × 38 × 2 mm/);
  assert.doesNotMatch(await page.locator(".tube-designer-inspection-summary").innerText(), /总长/);
  await select(1).click(); assert.equal(await page.locator("[data-tube-inspection-element-detail]").isVisible(), true);
  await close.click();
  await page.evaluate(() => window.fixture.reopen()); await waitForRequest(5); await page.evaluate(() => window.fixture.release(0, "accessory")); await ready();
  assert.equal(await page.locator("[data-tube-inspection-element-row]").count(), 0);
  if(await bulk.count())assert.equal(await bulk.isDisabled(), true, "An accessory with no hole rulers offers no active batch visibility control");
  assert.equal((await visibleIds()).length, 0);
  assert.match(await page.locator("[data-tube-designer-automatic-dimensions]").innerText(), /宽 X|深 Y|高 Z/);
  assert.match(await page.locator(".tube-designer-inspection-summary").innerText(), /宽 X 30 mm[\s\S]*深 Y 24 mm[\s\S]*高 Z 40 mm/);
  assert.doesNotMatch(await page.locator(".tube-designer-inspection-summary").innerText(), /总长|孔 \/ 开口/);
  await close.click();

  await page.evaluate(() => window.fixture.reopen(true)); await waitForRequest(6); await closeIsPainted();
  await close.click(); await page.waitForFunction(() => !window.fixture.view.tubeDesignerPartInspectionOpen);
  await page.evaluate(() => window.fixture.release());
  await page.waitForTimeout(600);
  assert.equal(await page.locator(".tube-designer-part-inspection-dialog").count(), 0);
  assert.equal(await page.evaluate(() => window.fixture.viewport.isDisposed), true, "Nesting-export close immediately disposes the inspection and its cube, even before the async measurement returns");
  await page.evaluate(() => window.fixture.reopen()); await waitForRequest(7);
  const background = page.locator("#background-parts");
  await background.evaluate((list) => list.scrollTop = 0);
  const backgroundRect = await background.boundingBox();
  const scrollbarPoint = { x: backgroundRect.x + backgroundRect.width - 8, y: backgroundRect.y + 12 };
  assert.equal(await page.evaluate(({x,y}) => document.elementFromPoint(x,y)?.closest("#background-parts")?.id, scrollbarPoint), "background-parts", "The exposed background scrollbar remains a real pointer target");
  await page.mouse.move(scrollbarPoint.x, scrollbarPoint.y); await page.mouse.down();
  await page.mouse.move(scrollbarPoint.x, backgroundRect.y + backgroundRect.height - 12, { steps: 8 }); await page.mouse.up();
  assert.ok(await background.evaluate((list) => list.scrollTop) > 0, "Dragging the native background scrollbar works while inspection is open");
  await background.evaluate((list) => list.scrollTop = 0);
  await page.mouse.move(backgroundRect.x + 100, backgroundRect.y + 35); await page.mouse.wheel(0, 400);
  await page.waitForFunction(() => document.querySelector("#background-parts").scrollTop > 0);
  await background.evaluate((list) => list.scrollTop = 0);
  await page.evaluate(() => {
    const f=window.fixture;
    f.background=document.querySelector("#background-parts");f.backgroundTbody=f.background.querySelector("tbody");f.backgroundRow=f.background.querySelector('[data-tube-designer-product-part-row="part-2"]');
    f.switchRenders=f.renders;f.firstSwitchedViewport=f.viewport;
  });
  const switchWindowBounds = await inspectionBounds();
  await page.locator('[data-tube-designer-product-part-row="part-2"] td').dblclick(); await waitForRequest(8);
  assert.match(await page.locator("#tube-designer-part-inspection-title").innerText(), /下框-03/);
  assert.equal(await page.evaluate(() => window.fixture.firstSwitchedViewport.isDisposed), true);
  assert.deepEqual(await inspectionBounds(), switchWindowBounds, 'Double-click switching preserves the manually resized window bounds');
  await page.evaluate(() => window.fixture.secondSwitchedViewport=window.fixture.viewport);
  await page.locator('[data-tube-designer-product-part-row="part-3"] td').dblclick(); await waitForRequest(9);
  assert.match(await page.locator("#tube-designer-part-inspection-title").innerText(), /左框-04/);
  assert.equal(await page.evaluate(() => window.fixture.secondSwitchedViewport.isDisposed), true);
  assert.deepEqual(await inspectionBounds(), switchWindowBounds);
  await page.evaluate(() => {
    const f=window.fixture,left=document.querySelector(".cam-context-pane"),right=document.querySelector(".cam-info-pane");
    left.scrollTop=190;right.scrollTop=310;f.background.scrollTop=75;
    const input=f.backgroundRow;input.focus({preventScroll:true});
    f.switchFocusedInput=input;f.switchCanvas=document.querySelector(".icax-three-viewport-canvas");
    f.releaseRequest(7,17);f.releaseRequest(8,80);
  });
  await page.waitForTimeout(100);
  assert.equal(await page.locator("[data-tube-inspection-element-row]").count(), 0, "Responses from both disposed targets cannot populate the newly selected part");
  await page.evaluate(() => window.fixture.releaseRequest(9,2)); await ready();
  assert.equal(await page.locator("[data-tube-inspection-element-row]").count(), 2);
  assert.match(await page.locator("#tube-designer-part-inspection-title").innerText(), /左框-04/);
  assert.deepEqual(await page.evaluate(() => ({ background:window.fixture.background===document.querySelector("#background-parts"),tbody:window.fixture.backgroundTbody===document.querySelector("#background-parts tbody"),row:window.fixture.backgroundRow===document.querySelector('[data-tube-designer-product-part-row="part-2"]'),scroll:window.fixture.background.scrollTop,focus:document.activeElement===window.fixture.switchFocusedInput,left:document.querySelector(".cam-context-pane").scrollTop,right:document.querySelector(".cam-info-pane").scrollTop,canvas:window.fixture.switchCanvas===document.querySelector(".icax-three-viewport-canvas"),renders:window.fixture.renders===window.fixture.switchRenders })), {background:true,tbody:true,row:true,scroll:75,focus:true,left:190,right:310,canvas:true,renders:true});
  assert.equal(await page.evaluate(() => window.fixture.patchBackground()),true);
  await page.waitForTimeout(100);
  assert.equal(await page.locator(".tube-designer-part-inspection-backdrop").count(),1);
  assert.deepEqual(await page.evaluate(() => ({focus:document.activeElement===window.fixture.switchFocusedInput,left:document.querySelector(".cam-context-pane").scrollTop,right:document.querySelector(".cam-info-pane").scrollTop,background:window.fixture.background.scrollTop,canvas:window.fixture.switchCanvas===document.querySelector(".icax-three-viewport-canvas")})),{focus:true,left:190,right:310,background:75,canvas:true});
  await screenshot("inspection-background-switch");
  const callsBeforeSamePart=await page.evaluate(() => window.fixture.calls);
  await page.locator('[data-tube-designer-product-part-row="part-3"] td').dblclick();
  await page.waitForTimeout(100);
  assert.equal(await page.evaluate(() => window.fixture.calls), callsBeforeSamePart, "Double-clicking the already inspected part preserves its viewport and measurement");
  const finalHandle = await page.locator('[data-tube-inspection-resize="e"]').boundingBox();
  await page.mouse.move(finalHandle.x + finalHandle.width / 2, finalHandle.y + finalHandle.height / 2); await page.mouse.down();
  await page.mouse.move(finalHandle.x + finalHandle.width / 2 + 20, finalHandle.y + finalHandle.height / 2, { steps: 3 });
  await page.evaluate(async () => { const { handleDesignerAreaAction } = await import('/src/apps/tube-designer/webpage/designerActions.mjs'); const f = window.fixture; await handleDesignerAreaAction(f.context, f.view, 'tube-designer-close-part-inspection', null, f.ops); });
  await page.mouse.move(10, 10); await page.mouse.up();
  assert.equal(await page.locator('.tube-designer-part-inspection-dialog').count(), 0, 'Closing during resize removes the window and releases its captured pointer');
  await page.locator('#fixture-ribbon').click();
  assert.equal(await page.evaluate(() => window.fixture.forbiddenClicks), 2, 'Resize cancellation and close cannot leave the background interaction guard installed');
  assert.equal(await page.locator(".tube-designer-part-inspection-dialog").count(),0);
  assert.equal(await page.evaluate(() => window.fixture.background===document.querySelector("#background-parts")),true);
  await page.evaluate(() => {
    const f=window.fixture;
    f.designer.nestingGroups=[{productEntityId:"nesting",parts:[{...f.designer.manufacturingGroups[0].parts[0],name:"仅排版零件",partNumber:"仅排版零件",manufacturingGeometryResourceId:"fixture://wrong-nesting-resource"}]}];
    f.reopen();f.view.tubeDesignerBreakdownMode="nesting-export";f.ops.renderProject();
  });
  await waitForRequest(10);
  assert.match(await page.locator("#tube-designer-part-inspection-title").innerText(),/上U形折弯框-02/);
  assert.doesNotMatch(await page.locator("#tube-designer-part-inspection-title").innerText(),/仅排版零件/);
  await page.evaluate(() => window.fixture.releaseRequest(10,1));await ready();
  assert.equal(await page.locator("[data-tube-inspection-element-row]").count(),1,"Product inspection ignores stale nesting-export mode when resolving the measured target");
  await close.click();
  const mapping = await page.evaluate(() => {
    const f = window.fixture, measurement = f.makeMeasurement(3);
    const duplicateStation = { ...measurement.features[0], center: [90, 5, 19] };
    measurement.features = [measurement.features[2], measurement.features[0], duplicateStation, measurement.features[1]];
    const report = f.inspection.buildAutomaticDimensionReport({ geometryMeasurement: measurement });
    return { holes: report.holes.map((hole) => ({ id: hole.elementId, index: hole.index, station: hole.station })), annotations: report.annotations.map((annotation) => ({ category: annotation.category, ids: annotation.elementIds })) };
  });
  assert.equal(new Set(mapping.holes.map((hole) => hole.id)).size, 4, "Unsorted and coincident-station openings retain unique element IDs");
  assert.deepEqual(mapping.holes.map((hole) => hole.index), [1,2,3,4]);
  assert.deepEqual(mapping.holes.map((hole) => hole.station), [90,90,1160,2230]);
  const ids = new Set(mapping.holes.map((hole) => hole.id));
  assert.ok(mapping.annotations.every((annotation) => annotation.ids.every((id) => ids.has(id))));
  assert.ok(mapping.annotations.filter((annotation) => annotation.ids.length === 2).every((annotation) => mapping.holes.findIndex((hole) => hole.id === annotation.ids[1]) === mapping.holes.findIndex((hole) => hole.id === annotation.ids[0]) + 1));
  assert.deepEqual(errors, []);
  console.log("Inspection Edge/WebGL: 69 rulers/17 elements, three-column table header with all/none/mixed batch visibility, row eye/selection, master/category intersection and surviving overall rulers, horizontal left-edge tree collapse and explicit visible-ruler count; real mouse/keyboard controls retain listeners/nodes/canvas/camera/selection, left/right caret ranges and nested scrolling through 80-row batch changes; pending hide and late header initialization, single-hole IDs, plate/accessory disabled visibility, scene selection, isolated view cube, 3 layouts, compact card; outside ribbon/sidebar/main-scene mouse/wheel/focus blocked, Tab/shortcut isolation, actual translucency, 8 resize handles and clamps, background native scrollbar/double-click target switches, stale replies, real background patches and close/disposal passed.");
} finally { await browser.close(); }
