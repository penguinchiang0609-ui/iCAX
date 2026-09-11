// Non-modal punch parameter window: production UI, DOM patch and Three viewport.
// Native computation alone is replaced with deterministic local mesh resources.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url)).replace(/[\\/]$/, "");
const artifacts = resolve(sourceRoot, "../tmp/punch-parameter-window-browser");
const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, "apps/tube-designer/templates/mold/circle/tool.json"), "utf8"));
const tool = { ...descriptor, digest: "parameter-window-fixture", defaultParameters: Object.fromEntries(descriptor.parameters.map(p => [p.key, p.defaultValue])) };
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
mkdirSync(artifacts, { recursive: true });
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(15000);
  const errors = [], externalRequests = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.origin !== "http://punch-window.test") { externalRequests.push(url.href); return route.abort(); }
    if (url.pathname === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body></html>' });
    const file = resolve(sourceRoot, url.pathname.replace(/^\/src\//, ""));
    if (!url.pathname.startsWith("/src/") || !file.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://punch-window.test/");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}" + readFileSync(resolve(sourceRoot, "apps/_shared/workbench/styles/laser3dcam.css"), "utf8") + tubeDesignerCss });
  await page.evaluate(async tool => {
    const wizard = await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const creation = await import("/src/apps/tube-designer/webpage/nestingPunchPart.mjs");
    const editor = await import("/src/apps/tube-designer/webpage/punchEditor.mjs");
    const { patchPunchDom } = await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const { renderDesignerOperationOverlay } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const { ThreeRenderViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { encodeNestingGeometry, encodePreviewMaterial } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const profile = { id: "round", name: "圆管", profileType: "parametric-package", defaultParameters: { diameter: 80, wallThickness: 2 },
      previewProfile: { kind: "round", name: "圆管", diameter: 80, width: 80, depth: 80, wallThickness: 2, contours: [{ kind: "circle", center: [0, 0], radius: 40 }] } };
    const draft = { profileKey: "system:round", length: "1000", name: "非模态窗口测试", quantity: "1", material: "", error: "" };
    const state = wizard.createPunchWizardState({ entityId: "__new_nesting_punch_part__", length: 1000, name: draft.name, profile: profile.previewProfile });
    state.creationMode = "main-tube-punch";
    state.creationInput = { draft, profileParameters: { "system:round": structuredClone(profile.defaultParameters) } };
    wizard.installPunchCatalogue(state, { tools: [tool] });
    const feature = wizard.normalizePunchFeature({ recordKind: "tool", layoutDatum: "base", face: "top", station: 300, arrayCount: 1 });
    wizard.selectPunchTool(state, feature, "circle"); feature.toolParameters.diameter = 12;
    state.features = [feature]; state.draft = structuredClone(feature); state.draft.id += "-draft"; state.draft.station = 600;
    const view = { pending: false, activeAreaId: "nesting", scene: { tubeDesigner: {} }, tubeDesignerPunchWizard: state,
      tubeDesignerNestingPunchPartDraft: draft, tubeDesignerSystemProfiles: [profile], tubeDesignerUserData: { profiles: [] }, tubeDesignerTemplateProfiles: [] };
    const f = window.fixture = { view, pending: 0, calls: [], failures: [], resources: [], patches: 0 };
    const originalMount = ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount = function (...args) { f.viewport = this; return originalMount.apply(this, args); };
    const meshes = new Map(), material = encodePreviewMaterial(0x55a8baff), toolMaterial = encodePreviewMaterial(0xf0a630ff);
    const boxBytes = (length, width, depth, x) => {
      const box = new THREE.BoxGeometry(length, width, depth).toNonIndexed(); box.translate(x, 0, 35);
      const bytes = encodeNestingGeometry({ positions: [...box.attributes.position.array], indices: Array.from({ length: box.attributes.position.count }, (_, i) => i) });
      box.dispose(); return bytes;
    };
    meshes.set("blank", boxBytes(1000, 80, 80, 500));
    const mount = document.querySelector("#app");
    const context = { mount, sceneProxy: { async invoke(method, payload) {
      if (method !== "TubeDesigner.PreviewPunchWizard") throw new Error("Unexpected fixture operation: " + method);
      f.calls.push(structuredClone(payload));
      const serial = f.calls.length, item = payload.features[0];
      const key = "tools-" + serial;
      meshes.set(key, boxBytes(Number(item.toolParameters.diameter), 110, 110, Number(item.station)));
      // A controllable completion gate exercises camera ownership across an
      // outstanding response. It does not measure native execution time.
      if (f.deferOnePreview) { f.deferOnePreview = false; await new Promise(resolve => { f.releasePreview = resolve; }); f.releasePreview = null; }
      return { toolsOnly: true, baseGeometry: { url: "blank", version: 1 }, baseMaterial: { url: "material", version: 1 },
        toolGeometry: { url: key, version: 1 }, toolMaterial: { url: "tool-material", version: 1 },
        length: 1000, baseBounds: { min: [0, -40, -40], max: [1000, 40, 40] }, toolCount: 1, toolCountExact: false };
    }, resources: { async get(url) {
      f.resources.push(url); const bytes = url === "material" ? material : url === "tool-material" ? toolMaterial : meshes.get(url);
      if (!bytes) throw new Error("Unexpected fixture resource: " + url); return new Response(bytes);
    } } } };
    const ops = { renderProject() {
      const html = creation.renderNestingPunchPartDialog(view) + renderDesignerOperationOverlay(context, view);
      if (patchPunchDom(view, mount, html)) f.patches++; else mount.innerHTML = html;
      editor.attachPunchEditor(context, view, mount, ops);
    } };
    f.render = ops.renderProject;
    f.dispatch = async (action, target) => {
      if (!action?.startsWith("tube-designer-nesting-punch-create-")) return;
      f.pending++;
      try { await creation.handleNestingPunchPartAction(context, view, action, target, ops); }
      catch (error) { f.failures.push(error.message); } finally { f.pending--; }
    };
    document.addEventListener("change", event => void f.dispatch(event.target.dataset.camChangeAction, event.target));
    document.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target && !target.disabled) void f.dispatch(target.dataset.camAction, target); });
    f.render();
  }, tool);
  const popup = page.locator("[data-punch-parameter-dialog]");
  const idle = async () => {
    await page.waitForFunction(() => { const f = window.fixture, s = f.view.tubeDesignerPunchWizard;
      return !f.pending && !f.view.pending && !s.previewPending && !s.previewRenderPending && !s.uiPendingClick; });
    assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
  };
  const ready = async () => { await idle(); await page.waitForFunction(() => document.querySelector("[data-tube-designer-punch-viewport]")?.dataset.punchPreviewReady === "true"); };
  const camera = () => page.evaluate(() => window.fixture.viewport.getCameraState());
  const frame = () => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const open = async () => { await page.locator('[data-tube-designer-punch-row="0"] [data-tube-designer-punch-editor-mode="shape"]').click(); await ready(); };
  const scenePoint = () => page.evaluate(() => {
    const canvas = window.fixture.viewport.renderer.domElement, r = canvas.getBoundingClientRect();
    for (const y of [r.top + r.height * .48, r.top + r.height * .25, r.bottom - 55]) for (const x of [r.left + 120, r.right - 180, r.left + 40])
      if (document.elementFromPoint(x, y) === canvas && document.elementFromPoint(x + 50, y + 25) === canvas) return { x, y };
    throw new Error("No directly reachable scene canvas outside the parameter window");
  });
  const drag = async (x, y, dx, dy, button = "left") => {
    await page.mouse.move(x, y); await page.mouse.down({ button }); await page.mouse.move(x + dx, y + dy, { steps: 12 }); await page.mouse.up({ button }); await frame();
  };
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-preview"]').click(); await ready();
  const beforeOpen = await camera(); await open();
  assert.equal(await popup.getAttribute("aria-modal"), "false");
  let box = await popup.boundingBox();
  assert.ok(Math.abs(box.x + box.width / 2 - 800) < 2 && Math.abs(box.y + box.height / 2 - 500) < 2, "The initial parameter window remains centered");
  assert.deepEqual(await camera(), beforeOpen, "Opening a parameter window must not reset the chosen camera");
  await page.evaluate(() => { const f = window.fixture; f.originalCanvas = f.viewport.renderer.domElement; f.originalWindow = document.querySelector("[data-punch-parameter-dialog]"); });
  const p = await scenePoint(); await drag(p.x, p.y, 50, 25, "right");
  const rotated = await camera();
  assert.ok(Math.abs(rotated.theta - beforeOpen.theta) > .01 || Math.abs(rotated.phi - beforeOpen.phi) > .01, "Actual right-drag outside the window must rotate the real Three camera");
  await page.mouse.move(p.x, p.y); await page.mouse.wheel(0, -240); await frame();
  assert.notEqual((await camera()).radius, rotated.radius, "Actual wheel outside the window must zoom the real Three camera");
  await page.screenshot({ path: resolve(artifacts, "01-centered-window-live-scene.png") });

  const diameter = popup.locator('[data-tube-designer-punch-parameter="diameter"]');
  const beforeUnsubmitted = await page.evaluate(() => window.fixture.calls.length);
  await diameter.fill("14");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 12, "Typing without blur leaves the committed parameter unchanged");
  const beforeDrag = await camera(), header = await popup.locator("[data-punch-parameter-drag]").boundingBox();
  await drag(header.x + 90, header.y + 20, -box.x + 20, 120);
  let moved = await popup.boundingBox();
  assert.ok(Math.abs(moved.x - box.x) > 50 && Math.abs(moved.y - box.y) > 30, "Dragging the title moves the window");
  assert.deepEqual(await camera(), beforeDrag, "Dragging the parameter title cannot rotate the underlying scene");
  assert.equal(await diameter.inputValue(), "14", "Dragging the title preserves the unsubmitted input text");
  assert.equal(await page.evaluate(() => window.fixture.calls.length), beforeUnsubmitted, "Dragging the title does not submit or request geometry");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 12);
  assert.ok(await diameter.evaluate(element => element === document.activeElement), "Title drag keeps focus in the unsubmitted input");
  assert.ok(await page.evaluate(() => { const f = window.fixture; return f.viewport.renderer.domElement === f.originalCanvas && document.querySelector("[data-punch-parameter-dialog]") === f.originalWindow; }), "Title dragging preserves both window and WebGL DOM nodes");
  const position = await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.parameterEditor.windowPosition);
  assert.ok(Number.isFinite(position?.left) && Number.isFinite(position?.top));
  // Right-drag directly from a focused, dirty input. Hold its one native-stage
  // response until the real pointer gesture has changed the camera, then prove
  // completion cannot restore an old camera or steal focus back into the form.
  await page.evaluate(() => { window.fixture.deferOnePreview = true; });
  const dirtyScenePoint = await scenePoint();
  await drag(dirtyScenePoint.x, dirtyScenePoint.y, 50, 25, "right");
  await page.waitForFunction(() => typeof window.fixture.releasePreview === "function");
  const cameraWhilePending = await camera();
  assert.ok(cameraWhilePending.theta !== beforeDrag.theta || cameraWhilePending.phi !== beforeDrag.phi, "The dirty-input transition still delivers the actual right-drag to the canvas");
  assert.equal(await page.evaluate(() => window.fixture.calls.length), beforeUnsubmitted + 1);
  await page.evaluate(() => window.fixture.releasePreview()); await ready();
  assert.deepEqual(await camera(), cameraWhilePending, "A response after the mouse gesture cannot restore an older camera snapshot");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 14);
  assert.equal(await page.evaluate(() => window.fixture.calls.length), beforeUnsubmitted + 1, "Scene blur commits exactly once, including all display-completion renders");
  assert.equal(await diameter.evaluate(element => element === document.activeElement), false, "Completed preview must not refocus the input after the user entered the scene");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.uiFocus ?? null), null);
  assert.equal(await popup.count(), 1);
  const continuePoint = await scenePoint();
  await drag(continuePoint.x, continuePoint.y, 45, 20, "right");
  const continuedCamera = await camera();
  assert.ok(continuedCamera.theta !== cameraWhilePending.theta || continuedCamera.phi !== cameraWhilePending.phi, "Subsequent real scene rotation remains active after the input commit");
  await page.mouse.move(continuePoint.x, continuePoint.y); await page.mouse.wheel(0, 160); await frame();
  const interactiveCamera = await camera();
  assert.notEqual(interactiveCamera.radius, continuedCamera.radius);
  assert.equal(await diameter.evaluate(element => element === document.activeElement), false);
  assert.equal(await page.evaluate(() => window.fixture.calls.length), beforeUnsubmitted + 1, "Continued rotation and wheel zoom do not resubmit the input");
  await diameter.fill("18"); await diameter.press("Tab"); await ready();
  assert.deepEqual(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.parameterEditor.windowPosition), position, "A production partial patch preserves the dragged position");
  assert.deepEqual(await camera(), interactiveCamera, "Editing geometry preserves the user camera");
  const afterPatch = await popup.boundingBox();
  assert.ok(Math.abs(afterPatch.x - moved.x) < 1 && Math.abs(afterPatch.y - moved.y) < 1);
  assert.ok(await page.evaluate(() => { const f = window.fixture; return f.viewport.renderer.domElement === f.originalCanvas && document.querySelector("[data-punch-parameter-dialog]") === f.originalWindow; }), "Parameter updates patch rather than replace the window/canvas");

  // Use the production ViewCube's visible face hit target, never call setView.
  const cubeTarget = await page.evaluate(() => {
    const candidates = [...document.querySelectorAll('[data-cam-viewcube] [data-cam-view]')];
    for (const element of candidates) { const r = element.getBoundingClientRect(), x = r.x + r.width / 2, y = r.y + r.height / 2;
      if (r.width > 8 && r.height > 8 && element.contains(document.elementFromPoint(x, y))) return { x, y, name: element.dataset.camView }; }
    return null;
  });
  assert.ok(cubeTarget, "The ViewCube must remain directly reachable with the parameter window open");
  const beforeCube = await camera(); await page.mouse.click(cubeTarget.x, cubeTarget.y); await frame();
  await page.waitForFunction(previous => { const c = window.fixture.viewport.getCameraState(); return c.theta !== previous.theta || c.phi !== previous.phi; }, beforeCube);
  const chosenCamera = await camera(), outside = await scenePoint();
  await page.mouse.click(outside.x, outside.y); await idle();
  assert.equal(await popup.count(), 1, "Clicking the scene neither confirms nor cancels the transaction");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 18);
  const locked = await page.evaluate(() => ['[data-tube-designer-nesting-punch-field="length"]', '[data-tube-designer-punch-row="0"] input', '[data-tube-designer-punch-new]', '.tube-designer-punch-footer button']
    .map(selector => { const e = document.querySelector(selector); return { selector, exists: !!e, locked: !!e && (e.matches(":disabled") || !!e.closest("[inert]")) }; }));
  assert.ok(locked.every(item => item.exists && item.locked), "Only scene interaction is unlocked: " + JSON.stringify(locked));
  const beforeLockedClick = await page.evaluate(() => ({ calls: window.fixture.calls.length, length: window.fixture.view.tubeDesignerNestingPunchPartDraft.length, count: window.fixture.view.tubeDesignerPunchWizard.features.length }));
  for (const selector of ['[data-tube-designer-nesting-punch-field="length"]', '[data-tube-designer-punch-row="0"] [data-cam-action$="-copy"]']) {
    const controlBox = await page.locator(selector).boundingBox();
    await page.mouse.click(controlBox.x + controlBox.width / 2, controlBox.y + controlBox.height / 2); await idle();
  }
  assert.deepEqual(await page.evaluate(() => ({ calls: window.fixture.calls.length, length: window.fixture.view.tubeDesignerNestingPunchPartDraft.length, count: window.fixture.view.tubeDesignerPunchWizard.features.length })), beforeLockedClick, "Physical clicks on locked stock/table controls do not alter the transaction or request geometry");
  assert.equal(await popup.count(), 1);
  await page.screenshot({ path: resolve(artifacts, "02-dragged-window-cube-and-patch.png") });
  // Parameter edits are live; closing the window keeps the displayed recipe.
  await popup.locator('[data-cam-action$="parameters-cancel"]').click(); await ready();
  assert.equal(await popup.count(), 0);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 18, "Closing the parameter window keeps live edits");
  assert.deepEqual(await camera(), chosenCamera, "Cancel must not undo the camera changes made outside the window");

  await open(); box = await popup.boundingBox();
  const header2 = await popup.locator("[data-punch-parameter-drag]").boundingBox();
  await drag(header2.x + 90, header2.y + 20, 650, 350);
  await page.setViewportSize({ width: 900, height: 650 }); await frame();
  const smallBox = await popup.boundingBox(), smallHeader = await popup.locator("[data-punch-parameter-drag]").boundingBox();
  const close = popup.locator('[data-cam-action$="parameters-cancel"]'), closeBox = await close.boundingBox();
  assert.ok(smallHeader.x >= 0 && smallHeader.y >= 0 && smallHeader.x + smallHeader.width <= 901 && smallHeader.y + smallHeader.height <= 651, "After resize the drag header remains reachable: " + JSON.stringify(smallHeader));
  assert.ok(closeBox.x >= 0 && closeBox.y >= 0 && closeBox.x + closeBox.width <= 901 && closeBox.y + closeBox.height <= 651, "After resize the close button remains reachable: " + JSON.stringify(closeBox));
  assert.ok(smallBox.y >= 0 && smallBox.y < 650);
  await page.screenshot({ path: resolve(artifacts, "03-resized-window-actions-reachable.png") });
  await close.click(); await idle(); assert.equal(await popup.count(), 0);

  // Count requests without pretending these synthetic meshes measure native
  // performance. Report an identical confirmation request if one is made.
  await page.setViewportSize({ width: 1600, height: 1000 }); await frame(); await open();
  const countBefore = await page.evaluate(() => window.fixture.calls.length);
  await diameter.fill("16"); await diameter.press("Tab"); await ready();
  const countChanged = await page.evaluate(() => window.fixture.calls.length);
  await popup.locator('[data-cam-action$="parameters-cancel"]').click(); await ready();
  const ordinaryCircleRequests = await page.evaluate(({ countBefore, countChanged }) => {
    const calls = window.fixture.calls;
    return { change: countChanged - countBefore, confirm: calls.length - countChanged,
      confirmationRepeatedSamePayload: calls.length > countChanged && JSON.stringify(calls[countChanged - 1]) === JSON.stringify(calls.at(-1)) };
  }, { countBefore, countChanged });
  assert.equal(ordinaryCircleRequests.change, 1, "One ordinary circle change sends one preview request");
  assert.equal(ordinaryCircleRequests.confirm, 0, "Confirming an already displayed unchanged recipe must not repeat the preview request");
  assert.deepEqual(errors, []); assert.deepEqual(externalRequests, []);
  const result = { passed: true, scenarios: ["centered non-modal window", "real pointer rotation and wheel zoom", "real ViewCube click", "title drag retains camera/DOM/parameters", "dirty input survives title drag without request", "dirty input commits once on scene right-drag without stealing focus or resetting camera", "local patch retains dragged position", "outside click preserves transaction", "background controls remain locked", "close keeps live parameters and camera", "resize keeps title and close reachable"],
    ordinaryCircleRequests, metrics: await page.evaluate(() => ({ calls: window.fixture.calls.length, resources: window.fixture.resources.length, patches: window.fixture.patches })) };
  writeFileSync(resolve(artifacts, "result.json"), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result, null, 2));
} catch (error) {
  await page?.screenshot({ path: resolve(artifacts, "failure.png") }).catch(() => {});
  writeFileSync(resolve(artifacts, "failure.json"), JSON.stringify({ error: error.stack, state: await page?.evaluate(() => ({ failures: window.fixture?.failures, editor: window.fixture?.view.tubeDesignerPunchWizard?.parameterEditor?.windowPosition })).catch(() => null) }, null, 2));
  throw error;
} finally { await browser.close(); }
