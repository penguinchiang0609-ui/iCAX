import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const template = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/segmented-bend/assembly.json", import.meta.url), "utf8"));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://assembly-progress.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-progress.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:820px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{position:relative;min-width:0;background:#13252d}
    .test-left-scroll,.test-right-scroll{height:380px;overflow:auto}
  ` });
  const result = await page.evaluate(async (template) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [template],
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example", workModeUserSelected: true,
        parameterDrafts: {}, showDiagram: false },
      scene: { tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" }, product: {} } } };
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const plans = [];
    const parts = [];
    const sceneLoads = [];
    const makePlan = (payload) => ({
      schema: "icax.assembly-preview-plan", templateId: payload.templateId,
      resolvedWorkflow: { realization: "integrated", blankParts: [{ id: "blank", participantRoles: ["segmentA", "segmentB"] }],
        partOperations: [], assemblySteps: [], bom: [], checks: [], parameterEffects: {} },
      designParts: ["segmentA", "segmentB"].map((role) => ({ id: `design-${role}`, role, label: role,
        request: { length: 100, features: [], ends: {} }, matrix: identity, compareMatrix: identity })),
      manufacturingParts: [{ id: "blank", label: "blank", sourceRole: "segmentA",
        participantRoles: ["segmentA", "segmentB"], request: { length: 200, features: [], ends: {} },
        matrix: identity, compareMatrix: identity }],
    });
    const context = { sceneProxy: { resources: {}, invoke(method, payload) {
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview")
        return new Promise((resolve) => plans.push({ payload, resolve: () => resolve(makePlan(payload)) }));
      if (method === "TubeDesigner.PreviewPunchWizard")
        return new Promise((resolve) => parts.push({ payload, resolve: () => resolve({ previewComputed: true,
          baseGeometry: { url: `memory://base-${parts.length}`, version: 1 },
          geometry: { url: `memory://result-${parts.length}`, version: 1 } }) }));
      throw new Error(`unexpected method ${method}`);
    } } };
    view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {} };
    const viewportFactory = () => {
      const root = document.createElement("div");
      root.className = "test-assembly-viewport";
      const viewport = { root, mount(host) { host.replaceChildren(root); return viewport; },
        applyViewSnapshot(snapshot) {
          if (!snapshot.rows.length) return Promise.resolve({ applied: true, entityIds: [], missingGeometryEntityIds: [] });
          return new Promise((resolve) => sceneLoads.push({ snapshot, resolve: () => resolve({ applied: true,
            entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] }) }));
        }, setVisibleEntityIds() {}, setDimensionAnnotations() {}, setStandardView() {},
        fitViewToViewport() {}, getCameraState() { return {}; }, setCameraState() {}, dispose() {} };
      return viewport;
    };
    const filler = Array.from({ length: 55 }, (_, index) => `<p>滚动项目 ${index + 1}</p>`).join("");
    const render = () => {
      const left = `<div class="test-left-scroll">${library.renderAssemblyLibraryLeftPane(context, view)}${filler}</div>`;
      const right = `<div class="test-right-scroll">${library.renderAssemblyLibraryRightPane(context, view)}${filler}</div>`;
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      const mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside>
          <div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) {
        throw new Error("进度更新没有使用侧栏局部刷新");
      }
      library.attachAssemblyLibraryViewports(context, view, document.querySelector("main"), { viewportFactory });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    const tickUntil = async (predicate, message) => {
      for (let index = 0; index < 60 && !predicate(); index += 1)
        await new Promise((resolve) => setTimeout(resolve, 0));
      if (!predicate()) throw new Error(message);
    };
    render();
    await tickUntil(() => plans.length === 1, "初始工艺方案请求未发出");
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    const viewport = mount.querySelector(".test-assembly-viewport");
    const left = mount.querySelector(".test-left-scroll");
    const right = mount.querySelector(".test-right-scroll");
    const initial = mount.querySelector('[role="progressbar"]');
    const preparing = { phase: view.tubeDesignerAssemblyLibrary.previewRequest.progress.phase,
      indeterminate: initial?.classList.contains("is-indeterminate"),
      currentAbsent: !initial?.hasAttribute("aria-valuenow"), label: initial?.getAttribute("aria-valuetext") };

    // Changing a parameter invalidates the first native plan. Its late response must not move the new progress bar.
    const input = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    input.value = "55";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change",
      input, { renderProject: render });
    await tickUntil(() => plans.length === 2, "参数修改后未启动新方案请求");
    const currentRequest = view.tubeDesignerAssemblyLibrary.previewRequest;
    let focusedInput = mount.querySelector(".tube-connection-library-search input");
    focusedInput.value = "分段";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-search",
      focusedInput, { renderProject: render });
    focusedInput = mount.querySelector(".tube-connection-library-search input");
    focusedInput.focus({ preventScroll: true });
    focusedInput.setSelectionRange(0, 1);
    left.scrollTop = 90;
    right.scrollTop = 110;
    plans[0].resolve();
    await new Promise((resolve) => setTimeout(resolve, 0));
    const staleIgnored = view.tubeDesignerAssemblyLibrary.previewRequest === currentRequest
      && currentRequest.progress.phase.includes("准备工艺方案") && parts.length === 0;

    const stages = [];
    const capture = (expectedCurrent, expectedTotal, phase) => {
      const bar = mount.querySelector('[role="progressbar"]');
      const wait = bar?.closest(".tube-assembly-preview-wait");
      const card = bar?.closest(".tube-designer-export-progress-card");
      stages.push({ phase: card?.textContent ?? "", now: bar?.getAttribute("aria-valuenow"),
        max: bar?.getAttribute("aria-valuemax"), text: bar?.getAttribute("aria-valuetext"),
        width: bar?.querySelector("i")?.style.width ?? "", indeterminate: bar?.classList.contains("is-indeterminate"),
        sharedStyle: bar?.classList.contains("tube-designer-export-progress-track")
          && !!card?.querySelector(".tube-designer-export-spinner")
          && getComputedStyle(bar).height === "7px" && getComputedStyle(card).backgroundColor === "rgb(255, 255, 255)",
        noBusyHud: !mount.querySelector(".tube-connection-library-hud"),
        waitVisible: !!wait && getComputedStyle(wait).display === "grid" });
      if (expectedTotal) {
        if (Number(bar?.getAttribute("aria-valuenow")) !== expectedCurrent
            || Number(bar?.getAttribute("aria-valuemax")) !== expectedTotal || !card?.textContent.includes(phase))
          throw new Error(`错误的分段进度：${JSON.stringify(stages.at(-1))}`);
      } else if (!bar?.classList.contains("is-indeterminate") || bar.hasAttribute("aria-valuenow")) {
        throw new Error(`等待阶段不应显示伪造百分比：${JSON.stringify(stages.at(-1))}`);
      }
      if (!stages.at(-1).sharedStyle || !stages.at(-1).noBusyHud || !stages.at(-1).waitVisible)
        throw new Error(`装配进度未复用管型、产品进度样式：${JSON.stringify(stages.at(-1))}`);
      const preserved = { canvas: canvas === mount.querySelector("canvas"),
        viewport: viewport === mount.querySelector(".test-assembly-viewport"),
        input: focusedInput === mount.querySelector(".tube-connection-library-search input"),
        focus: document.activeElement === focusedInput,
        selection: [focusedInput.selectionStart, focusedInput.selectionEnd],
        left: left === mount.querySelector(".test-left-scroll"),
        right: right === mount.querySelector(".test-right-scroll"),
        scroll: [left.scrollTop, right.scrollTop] };
      if (!preserved.canvas || !preserved.viewport || !preserved.input || !preserved.focus
          || preserved.selection[0] !== 0 || preserved.selection[1] !== 1 || !preserved.left
          || !preserved.right || preserved.scroll.some((value) => value <= 0))
        throw new Error(`进度更新破坏了画布、输入焦点或两侧滚动：${JSON.stringify(preserved)}`);
    };
    plans[1].resolve();
    await tickUntil(() => parts.length === 1, "构件 A 未进入生成阶段");
    capture(0, 3, "构件 0/2");
    left.scrollTop = 105; right.scrollTop = 135;
    parts[0].resolve();
    await tickUntil(() => parts.length === 2, "构件 B 未进入生成阶段");
    capture(1, 3, "构件 1/2");
    left.scrollTop = 120; right.scrollTop = 150;
    parts[1].resolve();
    await tickUntil(() => parts.length === 3, "下料件未进入生成阶段");
    capture(2, 3, "下料件 0/1");
    left.scrollTop = 135; right.scrollTop = 165;
    parts[2].resolve();
    await tickUntil(() => sceneLoads.length > 0, "三维画面未进入加载阶段");
    capture(0, 0, "加载三维画面");
    sceneLoads.splice(0).forEach((item) => item.resolve());
    await tickUntil(() => !view.tubeDesignerAssemblyLibrary.previewRequest, "三维加载后进度未结束");
    const completed = !!view.tubeDesignerAssemblyLibrary.preview && !mount.querySelector('[role="progressbar"]')
      && !mount.querySelector(".tube-assembly-preview-wait") && !mount.querySelector(".tube-connection-library-hud")
      && canvas === mount.querySelector("canvas") && viewport === mount.querySelector(".test-assembly-viewport")
      && focusedInput === mount.querySelector(".tube-connection-library-search input")
      && document.activeElement === focusedInput && focusedInput.selectionStart === 0 && focusedInput.selectionEnd === 1
      && left.scrollTop > 0 && right.scrollTop > 0;
    library.disposeAssemblyLibraryViewports(view);
    return { preparing, staleIgnored, stages, completed };
  }, template);
  assert.equal(result.preparing.indeterminate, true);
  assert.equal(result.preparing.currentAbsent, true);
  assert.match(result.preparing.label, /正在准备工艺方案/);
  assert.equal(result.staleIgnored, true, "过期方案不得覆盖当前预览进度");
  assert.deepEqual(result.stages.map((stage) => [stage.now, stage.max, stage.indeterminate]),
    [["0", "3", false], ["1", "3", false], ["2", "3", false], [null, null, true]]);
  assert.deepEqual(result.stages.slice(0, 3).map((stage) => stage.width), ["0%", "33%", "67%"]);
  assert.equal(result.completed, true, "结束后应清除进度条并保留编辑状态");
  assert.deepEqual(errors, []);
  console.log("Assembly preview progress phases, stale response, viewport loading, and focus/scroll preservation passed.");
} finally {
  await browser.close();
}
