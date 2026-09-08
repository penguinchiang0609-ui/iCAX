// Real WebGL/hydration regression with local mesh bytes, no project or native edits.
// Set ICAX_PLAYWRIGHT_MODULE to the bundled module URL and ICAX_BROWSER_CHANNEL=msedge
// when Playwright's default browser is not installed.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = fileURLToPath(new URL("../../", import.meta.url)).replace(/[\\/]$/, "");
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 760 } }), errors = [], externalRequests = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.origin !== "http://hydrate.test") { externalRequests.push(url.href); return route.abort(); }
    if (url.pathname === "/") return route.fulfill({ contentType: "text/html", body: '<div id="app"></div>' });
    const file = resolve(root, url.pathname.replace(/^\/src\//, ""));
    if (!url.pathname.startsWith("/src/") || !file.startsWith(root + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://hydrate.test");
  const result = await page.evaluate(async () => {
    const editor = await import("/src/apps/tube-designer/webpage/punchEditor.mjs");
    const wizard = await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const { ThreeRenderViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { encodeNestingGeometry, encodePreviewMaterial } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const check = (condition, message) => { if (!condition) throw new Error(message); };
    const waitFor = async (test, message) => {
      for (let n = 0; n < 400 && !test(); n++) await new Promise(resolve => setTimeout(resolve, 5));
      check(test(), message);
    };
    const mount = document.querySelector("#app"), part = { entityId: "lifecycle-regression", length: 1000 };
    const view = { pending: false, tubeDesignerPunchWizard: wizard.createPunchWizardState(part) };
    view.tubeDesignerPunchWizard.catalogueStatus = "ready";
    const originalMount = ThreeRenderViewport.prototype.mount;
    let viewport, currentLength = 1000, held = false, release = [], readCount = 0, failTools = false;
    ThreeRenderViewport.prototype.mount = function (...args) { viewport = this; return originalMount.apply(this, args); };
    const material = encodePreviewMaterial(0x61a8bbff);
    const resources = { async get(url) {
      readCount++; if (held) await new Promise(resolve => release.push(resolve));
      if (failTools && url === "tools") return new Response("tool unavailable", { status: 503 });
      if (url.includes("material")) return new Response(material);
      const box = new THREE.BoxGeometry(currentLength, 80, 80).toNonIndexed(); box.translate(currentLength / 2, 0, 0);
      const bytes = encodeNestingGeometry({ positions: [...box.attributes.position.array], indices: Array.from({ length: box.attributes.position.count }, (_, i) => i) });
      box.dispose(); return new Response(bytes);
    } };
    const context = { mount, sceneProxy: { resources } }, ops = { renderProject() {
      // Deliberately synchronous DOM replacement/reentry, like afterProjectRender.
      mount.innerHTML = view.tubeDesignerPunchWizard ? '<div data-tube-designer-punch-viewport style="position:relative;width:1000px;height:600px"></div>' : "";
      editor.attachPunchEditor(context, view, mount, ops);
    } };
    const idle = () => waitFor(() => !view.pending, "Hydration did not release pending");
    const preview = (version, length = 1000) => ({ geometry: { url: "result", version }, baseGeometry: { url: "base", version }, toolGeometry: { url: "tools", version },
      baseMaterial: { url: "material", version: 1 }, toolMaterial: { url: "material", version: 1 },
      baseBounds: { min: [0, -40, -40], max: [length, 40, 40] }, length, revision: version, includesDraft: false });
    // This isolated hydration fixture injects resources directly. Mirror the
    // actual preview response transition, including revision/error ownership.
    const acceptPreview=(version,length=1000)=>{
      const state=view.tubeDesignerPunchWizard;
      state.revision=version;state.preview=preview(version,length);
      state.previewRenderError="";state.error="";ops.renderProject();
    };
    const stats = [];
    let firstCanvas;
    for (let n = 1; n <= 30; n++) {
      const state = view.tubeDesignerPunchWizard;
      acceptPreview(n); await idle();
      firstCanvas ??= viewport.renderer.domElement;
      check(viewport.renderer.domElement === firstCanvas, "A new revision recreated the WebGL canvas");
      if ([1, 10, 30].includes(n)) {
        state.previewMode = "result"; ops.renderProject(); await idle();
        const afterResultReads = readCount, resultCamera = viewport.getCameraState();
        state.previewMode = "tools"; ops.renderProject(); await idle();
        check(readCount === afterResultReads, "Returning to tools fetched already loaded current-version resources");
        check(JSON.stringify(viewport.getCameraState()) === JSON.stringify(resultCamera), "Mode switch changed the intentional camera");
        stats.push({ version: n, decodedPromises: viewport.resourcePromises.size, geometryObjects: viewport.geometryObjects.size, sceneObjects: viewport.sceneObjects.size });
      }
    }
    editor.setPunchPreviewView(mount, "top");
    const beforeCamera = viewport.getCameraState(); currentLength = 10000;
    view.tubeDesignerPunchWizard.baseLength = 10000; acceptPreview(31,10000); await idle();
    const afterCamera = viewport.getCameraState();
    check(beforeCamera.theta === afterCamera.theta && beforeCamera.phi === afterCamera.phi, "Refitting a longer tube discarded the chosen view direction");
    const failedStats = []; failTools = true;
    for (let n = 40; n < 50; n++) {
      const state = view.tubeDesignerPunchWizard; acceptPreview(n,10000); await idle();
      if ([40, 44, 49].includes(n)) failedStats.push({ version: n, decodedPromises: viewport.resourcePromises.size, error: state.previewRenderError });
    }
    failTools = false; acceptPreview(50,10000); await idle();
    check(!view.tubeDesignerPunchWizard.previewRenderError, "A subsequent successful snapshot did not recover from partial resource failure");
    // The native-stage owner may request finish before its display resources
    // resolve. It must stay locked, and then a replaced wizard must be isolated.
    editor.beginPunchOperation(context, view, "previous native stage");
    const oldOperation = view.tubeDesignerOperation;
    held = true; acceptPreview(51,10000);
    await waitFor(() => release.length > 0, "Expected delayed resources to enter their display hold");
    const oldViewport = viewport, beforeRepeatedRenderReads = readCount;
    ops.renderProject(); ops.renderProject();
    check(readCount === beforeRepeatedRenderReads, "Synchronous render reentry started duplicate resource loads");
    check(editor.finishPunchOperation(view, oldOperation) === false && view.pending && oldOperation.finishRequested,
      "Native completion unlocked while its render hold was live");
    view.tubeDesignerPunchWizard = wizard.createPunchWizardState(part); view.tubeDesignerPunchWizard.catalogueStatus = "ready";
    const newState = view.tubeDesignerPunchWizard; newState.error = "new-state-marker"; ops.renderProject();
    editor.beginPunchOperation(context, view, "replacement owner");
    const replacementOperation = view.tubeDesignerOperation;
    held = false; release.splice(0).forEach(resolve => resolve());
    await waitFor(() => oldOperation.renderHolds === 0, "Obsolete display hold was not released");
    check(view.pending && view.tubeDesignerOperation === replacementOperation, "Obsolete completion unlocked the replacement operation");
    editor.finishPunchOperation(view, replacementOperation); ops.renderProject(); await idle();
    return { stats, failedStats, readCount, camera: { beforeRadius: beforeCamera.radius, afterRadius: afterCamera.radius },
      oldViewportDisposed: oldViewport.isDisposed, newStateError: newState.error, operation: view.tubeDesignerOperation, pending: view.pending };
  });
  assert.deepEqual(result.stats.map(item => item.decodedPromises), [4, 4, 4], "Successful revisions retain only the current recipe's decoded resources");
  assert.ok(result.stats.every(item => item.geometryObjects <= 3 && item.sceneObjects === 2));
  assert.deepEqual(result.failedStats.map(item => item.decodedPromises), [2, 2, 2], "Partially successful failed versions must not accumulate base meshes");
  assert.ok(result.failedStats.every(item => /503/.test(item.error)));
  assert.ok(result.camera.afterRadius > result.camera.beforeRadius * 5, "Tenfold tube length change must refit the viewport");
  assert.equal(result.oldViewportDisposed, true);
  assert.equal(result.newStateError, "new-state-marker");
  assert.equal(result.pending, false); assert.equal(result.operation, null);
  assert.deepEqual(errors, []); assert.deepEqual(externalRequests, []);
  console.log("Punch hydration lifecycle: 30 versions, bounded success/failure caches, mode reuse, length refit, synchronous reentry, delayed close and operation ownership passed.");
} finally { await browser.close(); }
