import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const assemblyRoot = fileURLToPath(new URL("../../apps/tube-designer/templates/assembly/", import.meta.url));
const templates = readdirSync(assemblyRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory())
  .filter((entry) => {
    try { readFileSync(new URL(`../../apps/tube-designer/templates/assembly/${entry.name}/assembly.json`, import.meta.url)); return true; } catch { return false; }
  })
  .map((entry) => JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/assembly/${entry.name}/assembly.json`, import.meta.url), "utf8")));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://assembly-library.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-library.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:820px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative;min-width:0;background:#13252d}.cam-viewport canvas{width:100%;height:100%}.tube-connection-library-editor-body{height:140px;overflow:auto}` });
  const result = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates, tubeDesignerAssemblyLibrary: { selectedId: "bend" } };
    const planResolvers = [];
    const calls = [];
    const snapshots = [];
    const viewportRecords = [];
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const plan = (payload) => ({
      schema: "icax.assembly-preview-plan", templateId: payload.templateId,
      designParts: ["a", "b"].map((id) => ({ id: `design-${id}`, label: id, request: { length: 100, features: [], ends: {} }, matrix: identity, compareMatrix: identity })),
      manufacturingParts: [{ id: "manufacturing-blank", label: "blank", request: { length: 200, features: [], ends: {} }, matrix: identity, compareMatrix: identity }],
    });
    const context = { sceneProxy: { resources: {}, invoke(method, payload) {
      calls.push({ method, payload });
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
        return new Promise((resolve) => planResolvers.push(() => resolve(plan(payload))));
      }
      if (method === "TubeDesigner.PreviewPunchWizard") {
        const index = calls.filter((item) => item.method.endsWith("PreviewPunchWizard")).length;
        return Promise.resolve({ previewComputed: true, baseGeometry: { url: `memory://base-${index}`, version: 1 }, geometry: { url: `memory://result-${index}`, version: 1 } });
      }
      throw new Error(`unexpected method ${method}`);
    } } };
    view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {} };
    const viewportFactory = (options) => {
      const index = viewportRecords.length;
      const root = document.createElement("div");
      root.className = "fake-three-viewport";
      let camera = { side: index, revision: 0 };
      const record = { index, options, root, snapshots: [], views: [], fits: 0 };
      const viewport = {
        root,
        mount(host) { host.replaceChildren(root); return viewport; },
        async applyViewSnapshot(snapshot) { snapshots.push({ index, snapshot }); record.snapshots.push(snapshot); return { applied: true, entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] }; },
        setVisibleEntityIds() {}, setStandardView(name) { record.views.push(name); camera = { ...camera, view: name }; return true; },
        fitViewToViewport() { record.fits += 1; return true; }, getCameraState() { return { ...camera }; },
        setCameraState(value) { camera = { ...value }; }, dispose() { record.disposed = true; },
      };
      record.viewport = viewport;
      viewportRecords.push(record);
      return viewport;
    };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      if (!document.querySelector("main")) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, document.querySelector("main"), { left, right, overlay, suffix: "" })) {
        throw new Error("装配参数更新没有走局部更新");
      }
      library.bindAssemblyParameterDiagrams(document.querySelector("main"));
      library.attachAssemblyLibraryViewports(context, view, document.querySelector("main"), { viewportFactory });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    render();
    await Promise.resolve();
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    const finishedRoot = mount.querySelector("[data-tube-assembly-finished-viewport]").firstElementChild;
    const blankRoot = mount.querySelector("[data-tube-assembly-blank-viewport]").firstElementChild;
    const input = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    const rightScroll = mount.querySelector(".tube-connection-library-editor-body");
    input.focus({ preventScroll: true });
    input.value = "55";
    rightScroll.scrollTop = 180;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", input, { renderProject: render });
    await Promise.resolve();
    rightScroll.scrollTop = 220;
    planResolvers.splice(0).forEach((resolve) => resolve());
    for (let index = 0; index < 30 && view.tubeDesignerAssemblyLibrary.previewRequest; index += 1) {
      await new Promise((resolve) => setTimeout(resolve, 0));
      if (planResolvers.length) planResolvers.splice(0).forEach((resolve) => resolve());
    }
    const updated = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    const finishedRows = snapshots.filter((item) => item.index === 0).at(-1)?.snapshot.rows.length;
    const blankRows = snapshots.filter((item) => item.index === 1).at(-1)?.snapshot.rows.length;
    const finishedFront = mount.querySelector('[data-tube-assembly-side="finished"][data-tube-assembly-camera="front"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-camera", finishedFront, { renderProject: render });
    const annotation = mount.querySelector('[data-assembly-annotation-key="bendRadius"]');
    annotation?.dispatchEvent(new MouseEvent("click", { bubbles: true }));
    const finishedBox = mount.querySelector('[data-tube-assembly-preview-pane="finished"]').getBoundingClientRect();
    const blankBox = mount.querySelector('[data-tube-assembly-preview-pane="blank"]').getBoundingClientRect();
    return {
      canvasStable: canvas === mount.querySelector("canvas"), inputStable: input === updated,
      finishedRootStable: finishedRoot === mount.querySelector("[data-tube-assembly-finished-viewport]").firstElementChild,
      blankRootStable: blankRoot === mount.querySelector("[data-tube-assembly-blank-viewport]").firstElementChild,
      focused: document.activeElement === input, scroll: rightScroll.scrollTop,
      value: view.tubeDesignerAssemblyLibrary.parameterDrafts.bend.bendRadius,
      hasColdBend: mount.querySelector(".cam-info-pane").textContent.includes("冷折展开"),
      hudText: mount.querySelector(".tube-connection-library-hud").textContent,
      diagramFocused: document.activeElement === input,
      finishedRows,
      blankRows,
      finishedViews: viewportRecords[0].views,
      blankViews: viewportRecords[1].views,
      independentProjectionDefaults: [viewportRecords[0].options.projectionMode, viewportRecords[1].options.projectionMode],
      splitLayout: { finishedLeft: finishedBox.left, finishedWidth: finishedBox.width, blankLeft: blankBox.left, blankWidth: blankBox.width },
      resolveCalls: calls.filter((item) => item.method.endsWith("ResolveAssemblyTemplatePreview")).length,
      previewCalls: calls.filter((item) => item.method.endsWith("PreviewPunchWizard")).length,
    };
  }, templates);
  const realViewportResult = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "bend", showDiagram: false } };
    const overlay = library.renderAssemblyLibraryViewportOverlay({}, view);
    document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane"></aside><div class="cam-viewport">${overlay}</div><aside class="cam-info-pane"></aside></div></main>`;
    library.attachAssemblyLibraryViewports({}, view, document.querySelector("main"));
    await new Promise((resolve) => requestAnimationFrame(() => resolve()));
    const canvases = [...document.querySelectorAll(".tube-assembly-preview-host .icax-three-viewport-canvas")];
    const result = { count: canvases.length, distinct: canvases.length === 2 && canvases[0] !== canvases[1] };
    library.disposeAssemblyLibraryViewports(view);
    return result;
  }, templates);
  assert.equal(result.canvasStable, true);
  assert.equal(result.inputStable, true);
  assert.equal(result.finishedRootStable, true);
  assert.equal(result.blankRootStable, true);
  assert.equal(result.focused, true);
  assert.ok(result.scroll > 0);
  assert.equal(result.value, 55);
  assert.equal(result.hasColdBend, true);
  assert.match(result.hudText, /左侧显示装配成品，右侧显示对应下料零件/);
  assert.equal(result.diagramFocused, true);
  assert.equal(result.finishedRows, 2);
  assert.equal(result.blankRows, 1);
  assert.ok(result.finishedViews.includes("front"));
  assert.equal(result.blankViews.includes("front"), false, "左侧视角操作不得改变右侧相机");
  assert.deepEqual(result.independentProjectionDefaults, ["perspective", "orthographic"]);
  assert.ok(result.splitLayout.finishedWidth > 200 && result.splitLayout.blankWidth > 200);
  assert.ok(result.splitLayout.blankLeft >= result.splitLayout.finishedLeft + result.splitLayout.finishedWidth,
    "成品视口必须固定在左侧，下料视口必须固定在右侧");
  assert.ok(result.resolveCalls >= 2, "参数变化必须使旧请求失效并启动新预览");
  assert.equal(result.previewCalls, 3, "过期计划不得继续生成几何，当前计划只生成两个逻辑件和一个下料件");
  assert.deepEqual(realViewportResult, { count: 2, distinct: true }, "页面必须创建两个独立 WebGL 视口，而不是在一个场景中偏移摆放");
  assert.deepEqual(errors, []);
  console.log("Assembly dual viewports, independent cameras, live parameters and focus/scroll preservation passed.");
} finally {
  await browser.close();
}
