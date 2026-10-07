// Run the complete AppShell -> TubeDesigner entry -> actual workbench DOM.
// Only the native transport is replaced, with independently controlled replies.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../../../", import.meta.url));
const source = resolve(process.env.ICAX_BROWSER_RUNTIME_ROOT || resolve(root, "src"));
const output = resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY || resolve(root, "output/tests/tube-designer-startup-animation"));
const templateDescriptor = JSON.parse(readFileSync(resolve(source, "apps/tube-designer/templates/product/single_face_security_window/template.json"), "utf8"));
mkdirSync(output, { recursive: true });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge",
});
const results = [], errors = [];

async function assertStartupGuards(page, label) {
  const coverage = await page.evaluate(() => {
    const splash = document.querySelector("[data-startup-screen]");
    const application = document.getElementById("app");
    const rect = splash.getBoundingClientRect();
    const points = [{ name: "top-center", x: innerWidth / 2, y: 12 }, { name: "top-left", x: 12, y: 12 }, { name: "top-right", x: innerWidth - 12, y: 12 }];
    for (const selector of ['[data-command-id="app.save"]', '[data-action="window-minimize"]', '[data-action="window-maximize"]', '[data-action="window-close"]']) {
      const bounds = document.querySelector(selector)?.getBoundingClientRect();
      if (bounds?.width && bounds?.height) points.push({ name: selector, x: bounds.x + bounds.width / 2, y: bounds.y + bounds.height / 2 });
    }
    return { bounds: { x: rect.x, y: rect.y, width: rect.width, height: rect.height }, viewport: { width: innerWidth, height: innerHeight },
      inert: application.inert, ariaHidden: application.getAttribute("aria-hidden"), opacity: Number(getComputedStyle(splash).opacity),
      topHits: points.map(point => ({ name: point.name, covered: splash.contains(document.elementsFromPoint(point.x, point.y)[0]) })) };
  });
  assert.deepEqual(coverage.bounds, { x: 0, y: 0, ...coverage.viewport }, label + " startup must cover the whole viewport, including the titlebar");
  assert.equal(coverage.opacity, 1, label + " startup must be opaque until ready");
  assert.equal(coverage.inert, true, label + " underlying workbench must be inert");
  assert.equal(coverage.ariaHidden, "true", label + " underlying workbench must be hidden from accessibility navigation");
  assert(coverage.topHits.every(point => point.covered), label + " all titlebar hit locations must belong to the startup layer: " + JSON.stringify(coverage.topHits));
  for (let step = 0; step < 3; step++) {
    await page.keyboard.press("Tab");
    assert.equal(await page.evaluate(() => document.getElementById("app").contains(document.activeElement)), false, label + " Tab must not enter underlying workbench controls");
  }
}

async function assertWorkbenchReleased(page) {
  assert.deepEqual(await page.evaluate(() => {
    const application = document.getElementById("app");
    return { inert: application.inert, ariaHidden: application.getAttribute("aria-hidden") };
  }), { inert: false, ariaHidden: null }, "Ready workbench must synchronously release its startup interaction guard");
}

async function createScenario(options = {}) {
  const page = await browser.newPage({ viewport: options.viewport ?? { width: 1280, height: 800 }, reducedMotion: options.reducedMotion ?? "no-preference" });
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://startup-animation.test/**", route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === "/src/iCAX-UI/SDK/runtime.mjs") return route.fulfill({ contentType: "text/javascript", body: "export async function connectApplication(){return globalThis.__startupTransport.connect();}" });
    const file = resolve(source, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(source + sep)) return route.abort();
    try {
      let body = readFileSync(file, "utf8");
      // Defer only the script entry until the transport fixture is installed.
      if (path.endsWith("/AppShell/index.html")) body = body.replace(/<script type="module"[^>]*><\/script>/, "");
      return route.fulfill({ contentType: path.endsWith(".html") ? "text/html" : path.endsWith(".css") ? "text/css" : "text/javascript", body });
    } catch { return route.abort(); }
  });
  await page.goto("http://startup-animation.test/src/iCAX-UI/SDK/AppShell/index.html");
  assert.equal(await page.locator("[data-startup-screen]").isVisible(), true, "Static first paint must already show TubeDesigner before JavaScript/native connection");
  assert.equal(await page.title(), "TubeDesigner");
  assert.equal(await page.locator(".start-center,.start-products").count(), 0);
  await assertStartupGuards(page, "static first paint");
  await page.evaluate(async options => {
    const gate = () => {
      let done;
      const promise = new Promise(resolve => { done = resolve; });
      return { promise, release: done, waiting: false };
    };
    const gates = Object.fromEntries(["connect", "app", "scene", "userData", "tools", "template"].map(key => [key, gate()]));
    const awaitGate = async key => { gates[key].waiting = true; await gates[key].promise; gates[key].waiting = false; };
    const requests = [], openCalls = [], forbiddenFrames = [], forbiddenModals = [], windowCommands = [];
    new MutationObserver(records => {
      for (const record of records) for (const node of record.addedNodes) {
        if (!(node instanceof Element)) continue;
        if (node.matches(".start-center,.start-center-page,.start-center-overlay,.start-products") || node.querySelector(".start-center,.start-center-page,.start-center-overlay,.start-products")) {
          forbiddenFrames.push(node.textContent.slice(0, 250));
        }
        if (globalThis.__icaxAppShell?.getState().startupPhase !== "ready" && (node.matches(".cam-progress-backdrop,.global-progress-backdrop") || node.querySelector(".cam-progress-backdrop,.global-progress-backdrop"))) {
          forbiddenModals.push(node.textContent.slice(0, 250));
        }
      }
    }).observe(document.getElementById("app"), { childList: true, subtree: true });
    const projectId = "a784c2ed-ab61-4d5a-99a3-46210794bc00";
    const sceneId = "a784c2ed-ab61-4d5a-99a3-46210794bc01";
    const scene = { sceneId, undoRedo: { revision: 1, canUndo: false }, tubeDesigner: {
      templates: [{ id: options.template.id, displayName: options.template.displayName, available: true }], instances: [], product: null, activeProductId: "", members: [], joints: [], parts: [], manufacturingGroups: [],
    } };
    const snapshot = { viewId: "startup-view", revision: "1", rows: [] };
    let sceneCalls = 0, laterSceneGate = null;
    const sceneProxy = { state: scene, resources: { get() { throw new Error("Empty startup scene requested geometry"); } }, pdo: { enabled: false },
      views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
      async getState() { return structuredClone(scene); },
      async invoke(method) {
        requests.push(method);
        if (method === "TubeDesigner.List") {
          sceneCalls++; await awaitGate("scene");
          if (laterSceneGate) { laterSceneGate.waiting = true; await laterSceneGate.promise; laterSceneGate = null; }
          if (options.failScene && sceneCalls === 1) throw new Error("模拟场景读取失败，请重试");
          return { tubeDesigner: structuredClone(scene.tubeDesigner) };
        }
        if (method === "TubeDesigner.GetPunchTools") {
          await awaitGate("tools"); return { tools: [{ id: "startup-v-groove", displayName: "V 槽", libraryScope: "system", available: true }] };
        }
        if (method === "TubeDesigner.GetTemplateDescriptor") { await awaitGate("template"); return { template: options.template }; }
        throw new Error("Unexpected scene transport request: " + method);
      } };
    const projectState = { projectId, projectName: "未命名项目", projectPath: "", mainScene: scene };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const productState = { productId: "icax.tube-designer", productName: "TubeDesigner", isStarted: true,
      frontendEntry: "/src/apps/tube-designer/webpage/entry.mjs", catalogs: [], projectFile: { fileExtensions: ["ictd"] },
      recentProjects: [{ path: "D:/existing-project.ictd", displayName: "已有用户项目" }] };
    const productProxy = { productId: productState.productId, state: productState,
      getProject() { return projectProxy; }, async getState() { return productState; },
      async openProjectCatalog(path, payload) { openCalls.push({ path, payload }); return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; },
      async invoke(method) {
        requests.push(method);
        if (method === "TubeDesigner.ListUserData") { await awaitGate("userData"); return { systemProfiles: [{ id: "startup-square", name: "方管", libraryScope: "system" }], profiles: [], templateProfiles: [] }; }
        throw new Error("Unexpected product transport request: " + method);
      } };
    let connectCalls = 0, appCalls = 0;
    globalThis.icax = { async windowCommand(command) { windowCommands.push(command); } };
    const appProxy = { bridge: globalThis.icax,
      getProduct(id) { return id === productState.productId ? productProxy : null; },
      async startProduct() { return productProxy; },
      async getState() { appCalls++; await awaitGate("app"); return { products: options.missingProduct && appCalls === 1 ? [] : [productState] }; } };
    globalThis.__startupTransport = { requests, openCalls, forbiddenFrames, forbiddenModals, windowCommands, gates,
      async connect() { connectCalls++; await awaitGate("connect"); if (options.failConnect && connectCalls === 1) throw new Error("模拟连接失败，请重试"); return appProxy; },
      release(key) { gates[key].release(); }, releaseAll() { Object.values(gates).forEach(item => item.release()); },
      holdNextScene() { laterSceneGate = gate(); }, releaseLaterScene() { laterSceneGate?.release(); }, get laterSceneWaiting() { return Boolean(laterSceneGate?.waiting); },
      projectId,
      get connectCalls() { return connectCalls; }, get appCalls() { return appCalls; }, get sceneCalls() { return sceneCalls; } };
    await import("/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs");
  }, { ...options, template: templateDescriptor });
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === "loading");
  return page;
}

async function assertLoading(page, gateName) {
  await page.waitForFunction(key => globalThis.__startupTransport.gates[key].waiting, gateName);
  const state = await page.evaluate(() => globalThis.__icaxAppShell.getState());
  assert.equal(state.startupPhase, "loading", gateName);
  assert.equal(state.startCenterOpen, false, gateName);
  assert.equal(await page.locator(".start-center,.start-products,.global-progress-backdrop,.cam-progress-backdrop").count(), 0, gateName);
  assert.equal(await page.locator("[data-startup-screen]").count(), 1, gateName);
  assert.equal(await page.title(), "TubeDesigner", gateName);
  assert.deepEqual(await page.evaluate(() => globalThis.__startupTransport.forbiddenFrames), [], gateName);
  assert.deepEqual(await page.evaluate(() => globalThis.__startupTransport.forbiddenModals), [], gateName);
  await assertStartupGuards(page, gateName);
  const stageNames = { userData: "加载管型", tools: "加载单件工艺", template: "加载产品模板" };
  if (stageNames[gateName]) {
    assert((await page.locator("[data-startup-status]").innerText()).includes(stageNames[gateName]), gateName + " actual resource stage must be integrated into startup");
    assert((await page.locator("[data-startup-detail]").innerText()).trim().length > 0, gateName + " actual resource detail must be integrated into startup");
  }
}

async function release(page, key) { await page.evaluate(key => globalThis.__startupTransport.release(key), key); }
async function waitReady(page) {
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === "ready");
  await assertWorkbenchReleased(page);
}

async function openTransitionScenario() {
  const page = await createScenario();
  for (const key of ["connect", "app", "scene", "userData", "tools"]) {
    await page.waitForFunction(key => globalThis.__startupTransport.gates[key].waiting, key);
    await release(page, key);
  }
  await page.waitForFunction(() => globalThis.__startupTransport.gates.template.waiting);
  return page;
}

async function recordWorkbenchTransition() {
  const page = await openTransitionScenario();
  // Let the existing introduction assemble completely before illustrating
  // the actual handoff. No transition timing or visibility is overridden.
  await page.waitForTimeout(1400);
  await page.evaluate(() => {
    const splash = document.querySelector("[data-startup-screen]");
    const content = splash.querySelector(".td-startup__content");
    let observing = false, startedAt = 0;
    const samples = [];
    const observer = new MutationObserver(() => {
      if (observing || splash.dataset.startupState !== "ready") return;
      observing = true; startedAt = performance.now(); observer.disconnect();
      const canvas = document.querySelector("[data-product-surface='project'] canvas");
      const workbench = document.querySelector(".workbench");
      const surface = document.querySelector("[data-product-surface='project']");
      const transformChain = () => {
        const transforms = [];
        for (let node = canvas; node; node = node.parentElement) transforms.push(getComputedStyle(node).transform);
        return transforms;
      };
      const sample = () => {
        const rect = surface.getBoundingClientRect();
        samples.push({ elapsed: performance.now() - startedAt, connected: splash.isConnected,
          opacity: splash.isConnected ? Number(getComputedStyle(splash).opacity) : 0,
          foregroundOpacity: splash.isConnected ? Number(getComputedStyle(content).opacity) : 0,
          foregroundTransform: splash.isConnected ? getComputedStyle(content).transform : "none",
          pointerEvents: splash.isConnected ? getComputedStyle(splash).pointerEvents : "none",
          applicationInert: document.getElementById("app").inert,
          applicationAriaHidden: document.getElementById("app").getAttribute("aria-hidden"),
          workbenchOpacity: Number(getComputedStyle(workbench).opacity), transformChain: transformChain(),
          canvasRetained: canvas === document.querySelector("[data-product-surface='project'] canvas"),
          workbenchRetained: workbench === document.querySelector(".workbench"),
          surfaceRetained: surface === document.querySelector("[data-product-surface='project']"),
          completeWorkbench: surface.isConnected && rect.width > 0 && rect.height > 0 && canvas.isConnected && canvas.width > 0 && canvas.height > 0,
          progressModal: Boolean(document.querySelector(".cam-progress-backdrop,.global-progress-backdrop")) });
        if (splash.isConnected) requestAnimationFrame(sample);
        else globalThis.__startupTransition.finished = true;
      };
      sample();
    });
    observer.observe(splash, { attributes: true, attributeFilter: ["data-startup-state"] });
    globalThis.__startupTransition = { samples, finished: false };
  });
  const frames = [];
  const frameDirectory = resolve(output, "transition-frames");
  mkdirSync(frameDirectory, { recursive: true });
  const capture = async () => {
    const name = `frame-${String(frames.length).padStart(3, "0")}.png`;
    await page.screenshot({ path: resolve(frameDirectory, name) });
    frames.push({ file: name, elapsed: performance.now() - begin });
  };
  const begin = performance.now();
  await capture();
  const captureSession = await page.context().newCDPSession(page);
  const onFrame = async frame => {
    const name = `frame-${String(frames.length).padStart(3, "0")}.png`;
    writeFileSync(resolve(frameDirectory, name), Buffer.from(frame.data, "base64"));
    frames.push({ file: name, elapsed: performance.now() - begin });
    await captureSession.send("Page.screencastFrameAck", { sessionId: frame.sessionId });
  };
  captureSession.on("Page.screencastFrame", onFrame);
  await captureSession.send("Page.startScreencast", { format: "png", maxWidth: 1280, maxHeight: 800, everyNthFrame: 1 });
  await release(page, "template");
  await page.waitForTimeout(1100);
  await captureSession.send("Page.stopScreencast");
  captureSession.off("Page.screencastFrame", onFrame);
  await captureSession.detach();
  await capture();
  await waitReady(page);
  await page.waitForFunction(() => globalThis.__startupTransition.finished);
  const transition = await page.evaluate(() => ({ samples: globalThis.__startupTransition.samples,
    forbiddenFrames: globalThis.__startupTransport.forbiddenFrames, forbiddenModals: globalThis.__startupTransport.forbiddenModals }));
  const mid = transition.samples.filter(sample => sample.connected && sample.opacity > 0 && sample.opacity < 1);
  assert(mid.length >= 3, "Natural handoff must paint intermediate opacity frames");
  assert(mid.some(sample => sample.foregroundOpacity < sample.opacity - 0.1), "Foreground should fade before its backdrop to avoid overlapping workbench text");
  assert(mid.some(sample => sample.foregroundTransform !== "none"), "Foreground should softly shrink during the fade");
  for (const sample of transition.samples) {
    assert.equal(sample.pointerEvents, "none", "Startup must release interaction as soon as workbench is ready");
    assert.equal(sample.applicationInert, false, "Underlying workbench must be interactive throughout the fade");
    assert.equal(sample.applicationAriaHidden, null, "Underlying workbench accessibility must be restored throughout the fade");
    assert.equal(sample.workbenchOpacity, 1, "Only the foreground splash fades");
    assert(sample.transformChain.every(transform => transform === "none"), "CAD viewport and its ancestors must not be transformed during handoff");
    for (const key of ["canvasRetained", "workbenchRetained", "surfaceRetained", "completeWorkbench"]) assert.equal(sample[key], true, key);
    assert.equal(sample.progressModal, false, "No old loading modal should enter the handoff");
  }
  assert.deepEqual(transition.forbiddenFrames, []); assert.deepEqual(transition.forbiddenModals, []);
  assert.equal(await page.locator("[data-startup-screen]").count(), 0);
  writeFileSync(resolve(frameDirectory, "capture.json"), JSON.stringify({ frames, samples: transition.samples }, null, 2));
  results.push({ case: "natural-startup-to-workbench-crossfade", intermediateFrames: mid.length,
    foregroundFadesFirst: true, viewportNeverTransformedOrReplaced: true, completeWorkbenchThroughEveryPaint: true, screenshotFrames: frames.length,
    completedAfter: transition.samples.at(-1).elapsed });
  await page.close();
}

async function checkInterruptedTransition(kind) {
  const page = await openTransitionScenario();
  await release(page, "template");
  await waitReady(page);
  await page.waitForFunction(() => document.querySelector('[data-startup-screen][data-startup-state="ready"]'));
  if (kind === "transitioncancel") {
    // A real media-setting change cancels the running CSS opacity transition.
    await page.emulateMedia({ reducedMotion: "reduce" });
  } else {
    await page.evaluate(() => {
      Object.defineProperty(document, "hidden", { configurable: true, get: () => true });
      document.dispatchEvent(new Event("visibilitychange"));
    });
  }
  await page.waitForFunction(() => !document.querySelector("[data-startup-screen]"), null, { timeout: 200 });
  assert((await page.locator("[data-product-surface='project'] canvas").count()) > 0);
  results.push({ case: "handoff-" + kind + "-cleanup", noStaleBlockingScreen: true });
  await page.close();
}

try {
  const page = await createScenario();
  for (const key of ["connect", "app", "scene", "userData", "tools", "template"]) {
    await assertLoading(page, key);
    if (key === "connect") {
      await page.screenshot({ path: resolve(output, "startup-loading.png") });
      for (const command of ["minimize", "maximize", "close"]) {
        await assert.rejects(page.locator(`[data-action="window-${command}"]`).click({ timeout: 150 }), { name: "TimeoutError" }, "Normal titlebar clicks must be blocked during startup");
      }
      assert.deepEqual(await page.evaluate(() => globalThis.__startupTransport.windowCommands), [], "Loading must not invoke underlying window controls");
    }
    if (["userData", "tools", "template"].includes(key)) await page.screenshot({ path: resolve(output, `startup-${key}-stage.png`) });
    await release(page, key);
  }
  await waitReady(page);
  const ready = await page.evaluate(() => ({ state: globalThis.__icaxAppShell.getState(), calls: globalThis.__startupTransport.openCalls, requests: globalThis.__startupTransport.requests, forbiddenFrames: globalThis.__startupTransport.forbiddenFrames, forbiddenModals: globalThis.__startupTransport.forbiddenModals }));
  assert.equal(ready.state.activeProductId, "icax.tube-designer");
  assert.equal(ready.state.activeProjectName, "未命名项目");
  assert.equal(ready.state.activeProjectPath, "");
  assert.equal(ready.state.startCenterOpen, false);
  assert.equal(ready.calls.length, 1);
  assert.equal(ready.calls[0].path, "");
  assert.deepEqual(ready.forbiddenFrames, []);
  assert.deepEqual(ready.forbiddenModals, []);
  assert(ready.requests.includes("TubeDesigner.GetPunchTools"));
  assert((await page.locator("[data-product-surface='project'] canvas").count()) > 0);
  results.push({ case: "cold-real-workbench-startup", stages: ["connect", "app", "scene", "userData", "tools", "template"], recentProjectsNeverMounted: true, unsavedTubeDesignerProject: true, resourceStagesIntegrated: true, noStackedProgressModal: true, staticAndAsyncStagesCoverFullWindow: true, underlyingWorkbenchInertAndHidden: true, loadingTitlebarCommandsBlocked: true });

  // Protect interaction while the independent splash finishes its fade. Real
  // workbench panes and canvas must survive, along with their live listeners.
  await page.evaluate(() => {
    if (!document.querySelector('[data-startup-screen][data-startup-state="ready"]')) throw new Error("The fade must still be in progress for this regression");
    const left = document.querySelector(".cam-context-pane"), right = document.querySelector(".cam-info-pane");
    const canvas = document.querySelector("[data-product-surface='project'] canvas");
    const button = document.querySelector("[data-action='select-ribbon-tab']");
    const surface = document.querySelector("[data-product-surface='project']");
    if (!left || !right || !canvas || !button) throw new Error("Real workbench nodes did not mount");
    const inputs = [];
    for (const pane of [left, right]) {
      pane.style.height = "200px"; pane.style.overflow = "auto";
      // An empty project has no product parameter inputs yet. Use text
      // probes inside the real panes to detect focus/selection replacement.
      const input = document.createElement("input"); input.type = "text";
      input.value = "STARTUP-USER-DRAFT"; pane.prepend(input); inputs.push(input);
      const spacer = document.createElement("div"); spacer.style.height = "1600px"; pane.append(spacer);
    }
    left.scrollTop = 125; right.scrollTop = 215;
    inputs[1].focus({ preventScroll: true }); inputs[1].setSelectionRange(1, 8, "backward");
    let events = 0;
    for (const node of [left, right, canvas, button, ...inputs]) node.addEventListener("startup-node-probe", () => { events++; });
    globalThis.__startupInteraction = { left, right, canvas, button, surface, inputs, get events() { return events; } };
  });
  await page.evaluate(() => {
    const f = globalThis.__startupInteraction;
    f.inputs[0].value = "LATEST-LEFT-DRAFT";
    f.inputs[0].focus({ preventScroll: true }); f.inputs[0].setSelectionRange(2, 9, "backward");
    f.left.scrollTop = 147; f.right.scrollTop = 227;
  });
  await page.waitForFunction(() => !document.querySelector("[data-startup-screen]"));
  const interaction = await page.evaluate(() => {
    const f = globalThis.__startupInteraction;
    for (const node of [f.left, f.right, f.canvas, f.button, ...f.inputs]) node.dispatchEvent(new Event("startup-node-probe"));
    return { left: f.left === document.querySelector(".cam-context-pane"), right: f.right === document.querySelector(".cam-info-pane"),
      canvas: f.canvas === document.querySelector("[data-product-surface='project'] canvas"), surface: f.surface === document.querySelector("[data-product-surface='project']"),
      focus: document.activeElement === f.inputs[0], scrolls: [f.left.scrollTop, f.right.scrollTop], listeners: f.events,
      inputNodesRetained: f.inputs.every(input => input.isConnected), text: f.inputs[0].value,
      selection: [f.inputs[0].selectionStart, f.inputs[0].selectionEnd, f.inputs[0].selectionDirection] };
  });
  for (const key of ["left", "right", "canvas", "surface", "focus"]) assert.equal(interaction[key], true, key);
  assert.deepEqual(interaction.scrolls, [147, 227]); assert.equal(interaction.listeners, 6);
  assert.equal(interaction.inputNodesRetained, true); assert.equal(interaction.text, "LATEST-LEFT-DRAFT");
  assert.deepEqual(interaction.selection, [2, 9, "backward"]);
  results.push({ case: "fade-does-not-rebuild-workbench-or-restore-stale-interaction", ...interaction });
  await page.screenshot({ path: resolve(output, "startup-ready.png") });
  for (const command of ["minimize", "maximize", "close"]) await page.locator(`[data-action="window-${command}"]`).click();
  assert.deepEqual(await page.evaluate(() => globalThis.__startupTransport.windowCommands), ["minimize", "maximize", "close"], "Window controls must become usable only after startup is ready");
  results[0].readyTitlebarCommands = ["minimize", "maximize", "close"];
  await page.evaluate(async () => {
    const { getProjectView } = await import("/src/apps/_shared/workbench/state/projectViewStore.mjs");
    const f = globalThis.__startupTransport;
    f.holdNextScene(); getProjectView(f.projectId).tubeDesignerLoaded = false;
    globalThis.__normalProjectRefresh = globalThis.__icaxAppShell.refresh();
  });
  await page.waitForFunction(() => globalThis.__startupTransport.laterSceneWaiting && document.querySelector(".cam-progress-backdrop"));
  assert.equal(await page.locator("[data-startup-screen]").count(), 0);
  assert.equal(await page.locator(".cam-progress-backdrop").isVisible(), true, "Normal project reload retains its own progress overlay after startup");
  await page.evaluate(async () => { globalThis.__startupTransport.releaseLaterScene(); await globalThis.__normalProjectRefresh; });
  await page.waitForFunction(() => !document.querySelector(".cam-progress-backdrop"));
  results.push({ case: "later-project-reload-keeps-normal-progress-overlay", startupScreenWasNotRecreated: true });
  await page.close();

  for (const failure of ["failConnect", "missingProduct", "failScene"]) {
    const retry = await createScenario({ [failure]: true });
    await retry.evaluate(() => globalThis.__startupTransport.releaseAll());
    await retry.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === "error");
    assert.equal(await retry.locator("[data-startup-retry]").isVisible(), true);
    assert.equal(await retry.locator(".start-center").count(), 0);
    assert.equal(await retry.locator("[data-startup-screen]").count(), 1);
    await assertStartupGuards(retry, failure + " error screen");
    await retry.screenshot({ path: resolve(output, `startup-${failure}.png`) });
    await retry.locator("[data-startup-retry]").click();
    await waitReady(retry);
    await retry.waitForFunction(() => !document.querySelector("[data-startup-screen]"));
    assert((await retry.locator("[data-product-surface='project'] canvas").count()) > 0);
    if (failure === "failScene") {
      assert.equal(await retry.evaluate(() => globalThis.__startupTransport.sceneCalls), 2, "Scene retry must actually reread the failed scene");
      assert.equal(await retry.locator(".cam-status.error").count(), 0, "Successful startup retry must clear the previous failed scene status");
    }
    assert.deepEqual(await retry.evaluate(() => globalThis.__startupTransport.forbiddenFrames), []);
    assert.deepEqual(await retry.evaluate(() => globalThis.__startupTransport.forbiddenModals), []);
    results.push({ case: failure + "-visible-error-and-real-retry", recovered: true, fullWindowErrorCoverage: true, errorWorkbenchInertAndHidden: true });
    await retry.close();
  }

  const reduced = await createScenario({ reducedMotion: "reduce", viewport: { width: 720, height: 520 } });
  await assertLoading(reduced, "connect");
  const motion = await reduced.evaluate(() => {
    const splash = document.querySelector("[data-startup-screen]");
    const animated = [splash, ...splash.querySelectorAll("*")].filter(node => getComputedStyle(node).animationName !== "none");
    return { reduced: matchMedia("(prefers-reduced-motion: reduce)").matches,
      animations: animated.map(node => ({ duration: getComputedStyle(node).animationDuration, iterations: getComputedStyle(node).animationIterationCount })),
      bounds: { width: splash.getBoundingClientRect().width, height: splash.getBoundingClientRect().height },
      viewport: { width: innerWidth, height: innerHeight }, overflow: document.documentElement.scrollWidth > innerWidth };
  });
  assert.equal(motion.reduced, true);
  assert.equal(motion.overflow, false);
  for (const animation of motion.animations) {
    assert.notEqual(animation.iterations, "infinite", "Reduced motion must not loop");
    assert(animation.duration.split(",").every(value => parseFloat(value) <= 0.01), "Reduced motion should disable animation");
  }
  await reduced.screenshot({ path: resolve(output, "startup-reduced-motion-small.png") });
  await reduced.evaluate(() => globalThis.__startupTransport.releaseAll());
  await waitReady(reduced);
  await reduced.waitForFunction(() => !document.querySelector("[data-startup-screen]"));
  results.push({ case: "small-window-and-reduced-motion", ...motion });
  await reduced.close();
  await recordWorkbenchTransition();
  await checkInterruptedTransition("transitioncancel");
  await checkInterruptedTransition("hidden");
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, "report.json"), JSON.stringify({ results, errors }, null, 2));
  console.log(`PASS ${results.length} complete AppShell startup cases: asynchronous gates, no chooser flash, unsaved project, retry, reduced motion, live pane/canvas interaction`);
} finally {
  await browser.close();
}
