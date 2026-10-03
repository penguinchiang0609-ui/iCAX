import assert from "node:assert/strict";
import { existsSync, mkdirSync, readFileSync, readdirSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
const appRoot = resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT || source, "apps/tube-designer");
const catalogueRoot = resolve(appRoot, "templates/assembly");
const screenshotRoot = resolve(source, "../artifacts/orthogonal-corner-20261002");
mkdirSync(screenshotRoot, { recursive: true });
const templates = readdirSync(catalogueRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory()
  && existsSync(resolve(catalogueRoot, entry.name, "assembly.json")))
  .map((entry) => JSON.parse(readFileSync(resolve(catalogueRoot, entry.name, "assembly.json"), "utf8")));
const python = process.env.ICAX_PYTHON_EXECUTABLE
  || "C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe";
const runtimeScript = `import importlib.util, json, pathlib, sys
spec = importlib.util.spec_from_file_location("browser_corner_runtime", pathlib.Path(sys.argv[1]) / "templates/_shared/assembly_template_runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
print(json.dumps(runtime.generate(json.load(sys.stdin), {}), ensure_ascii=False))`;
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 760 } });
  await page.exposeFunction("captureCornerStage", async (name) => {
    await page.screenshot({ path: resolve(screenshotRoot, `${name}-ui.png`) });
  });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://orthogonal-corner.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    if (pathname === "/assembly-runtime") {
      const generated = spawnSync(python, ["-B", "-c", runtimeScript, appRoot], {
        input: route.request().postData(), encoding: "utf8", windowsHide: true,
      });
      if (generated.status !== 0) return route.fulfill({ status: 500, contentType: "text/plain",
        body: generated.stderr || generated.error?.message || "assembly runtime failed" });
      return route.fulfill({ contentType: "application/json", body: generated.stdout });
    }
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    const prefix = "/src/apps/tube-designer/";
    const served = pathname.startsWith(prefix) ? resolve(appRoot, pathname.slice(prefix.length)) : file;
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(served, "utf8") });
  });
  await page.goto("http://orthogonal-corner.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}
    .cam-workbench{display:grid;grid-template-columns:290px minmax(0,1fr) 380px;height:760px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{position:relative;min-width:0;background:#13252d}
    .tube-connection-library-list{height:110px!important;max-height:110px!important;flex:none!important;overflow:auto}
    .tube-connection-library-editor-body{height:250px;overflow:auto}
    .tube-assembly-scene-member .tube-connection-library-parameter-grid{max-height:110px;overflow:auto}` });
  const result = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const model = await import("/src/apps/tube-designer/webpage/finishedProductModel.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const conditions = await import("/src/apps/tube-designer/webpage/parameterConditions.mjs");
    const template = templates.find((item) => item.inputContract && item.exampleInput?.shapeId === "orthogonal-corner" && !item.catalogueHidden);
    if (!template) throw new Error("actual orthogonal corner template missing");
    const calls = [], plans = [], manufacturing = [], finished = [], snapshots = [];
    let holdFinished = false, clickedAction;
    const actualRuntime = async (payload) => {
      const response = await fetch("/assembly-runtime", { method: "POST", body: JSON.stringify(payload) });
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    };
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example", workModeUserSelected: true },
      tubeDesignerSystemProfiles: [{ id: "rect", name: "方矩管", profileForm: "parametric", defaultParameters: {},
        descriptor: { parameters: [{ key: "note", displayName: "外形备注", valueType: "string", defaultValue: "ABCDE" },
          ...["width", "depth", "wallThickness", "cornerRadius", "innerRadius"].map((key) => ({ key,
            displayName: key, valueType: "number", defaultValue: 2, min: 0 }))] } }],
      viewport: { setVisibleEntityIds() {}, setContinuousRendering() {} } };
    const context = { project: { projectId: "corner-project" }, sceneProxy: { sceneId: "corner-scene", resources: {},
      async invoke(method, payload) {
        calls.push({ method, payload: structuredClone(payload) });
        if (method.endsWith("CheckAssemblyTemplateApplicability"))
          return actualRuntime({ action: "check-applicability", ...payload });
        if (method.endsWith("GetAssemblyTemplateExampleProduct"))
          return actualRuntime({ action: "example-product", ...payload });
        if (method.endsWith("ResolveAssemblyTemplatePreview")) {
          const response = await actualRuntime({ action: "preview-plan", ...payload });
          if (payload.finishedOnly) return holdFinished ? new Promise((resolve) => finished.push(() => resolve(response))) : response;
          return new Promise((resolve) => plans.push(() => resolve(response)));
        }
        if (method.endsWith("PreviewPunchWizard")) return { baseGeometry: { url: `memory://finished-${payload.previewResourceKey}`, version: 1 } };
        if (method.endsWith("PreviewAssemblyManufacturingPart")) return new Promise((resolve) => manufacturing.push(() => resolve({
          previewComputed: true, resultValid: true, solidCount: 1,
          geometry: { url: `memory://manufacturing-${payload.previewResourceKey}`, version: 1 },
        })));
        if (method.endsWith("EvaluateProfilePackage")) throw new Error("diagram fixture unavailable");
        throw new Error(`unexpected method ${method}`);
      } } };
    const viewportRoot = document.createElement("div");
    viewportRoot.textContent = "UI 结构验证：画布未渲染模型，三轴姿态通过实际成品矩阵检查。";
    Object.assign(viewportRoot.style, { padding: "20px", color: "#dcebe9", fontSize: "13px" });
    const viewport = { root: viewportRoot, mount(host) { host.replaceChildren(viewportRoot); return this; },
      async applyViewSnapshot(snapshot) { snapshots.push(snapshot); return {
        applied: true, entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [],
      }; }, getVisibleBounds: () => ({ min: [-260, -40, -20], max: [40, 220, 260] }),
      setDimensionAnnotations() {}, setVisibleEntityIds() {}, setStandardView() {}, fitViewToViewport() {}, dispose() {} };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view), right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      let mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside>
          <div class="cam-viewport"><canvas id="stable"></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        mount = document.querySelector("main");
        mount.addEventListener("click", (event) => {
          const target = event.target.closest("[data-cam-action]");
          if (target) clickedAction = library.handleAssemblyLibraryAction(context, view, target.dataset.camAction, target, { renderProject: render });
        });
        patch.rememberLibraryDom(view, mount, "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) throw new Error("full redraw");
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory: () => viewport });
    };
    const state = library.assemblyLibraryState(view);
    const action = (name, target) => library.handleAssemblyLibraryAction(context, view, name, target, { renderProject: render });
    const settle = async () => {
      for (let i = 0; i < 300 && (state.previewRequest || state.selectionRequest); i++) {
        plans.splice(0).forEach((resolve) => resolve()); manufacturing.splice(0).forEach((resolve) => resolve());
        finished.splice(0).forEach((resolve) => resolve());
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
      if (state.previewRequest || state.selectionRequest) throw new Error("preview did not settle");
      if (state.previewError) throw new Error(state.previewError);
    };
    const click = async (selector, complete = true) => {
      const target = document.querySelector(selector);
      if (!target || target.disabled) throw new Error(`action unavailable ${selector}`);
      clickedAction = null; target.click();
      if (!clickedAction) throw new Error(`action not handled ${selector}`);
      await clickedAction; if (complete) await settle();
    };
    const setView = (mode, complete = true) => click(`[data-tube-assembly-view="${mode}"]`, complete);
    const choose = async (key, value) => {
      const field = document.querySelector(`[data-tube-assembly-parameter="${key}"]`);
      field.value = value; await action("tube-designer-assembly-parameter-change", field); await settle();
    };
    const counts = () => [calls.filter((call) => call.payload.finishedOnly).length,
      calls.filter((call) => call.method.endsWith("ResolveAssemblyTemplatePreview") && !call.payload.finishedOnly).length,
      calls.filter((call) => call.method.endsWith("PreviewAssemblyManufacturingPart")).length];
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    render(); await settle();
    await click('[data-cam-action="tube-designer-assembly-load-example"]');
    const product = model.finishedProductInput(view), shape = model.finishedProductShape(product.shapeId, view);
    let geometry = state.preview.designParts;
    const groups = [...document.querySelectorAll("[data-tube-assembly-shape]")].map((node) => node.dataset.tubeAssemblyShape);
    const scene = { spans: [...document.querySelectorAll("details[data-finished-span]")].map((node) => node.dataset.finishedSpan),
      labels: [...document.querySelectorAll("details[data-finished-span] summary strong")].map((node) => node.textContent),
      params: product.parameters, label: document.querySelector("[data-finished-product-editor] header small").textContent,
      catalogueC: !!document.querySelector('.tube-connection-library-card[data-tube-assembly-id="' + template.id + '"] .tube-assembly-catalogue-c') };
    const matrices = state.preview.plan.designParts.map((part) => ({ role: part.role,
      axis: [part.matrix[0], part.matrix[4], part.matrix[8]], origin: [part.matrix[3], part.matrix[7], part.matrix[11]], length: part.request.length }));
    const matricesApplied = library.assemblySceneRows(state.preview).every((row, index) =>
      JSON.stringify(row.data.localToWorldMatrix) === JSON.stringify(geometry[index].matrix));
    const capture = async (name, showParameters = false) => {
      const list = document.querySelector(".tube-connection-library-list");
      const group = document.querySelector('[data-tube-assembly-shape="orthogonal-corner"]');
      const editor = document.querySelector(".tube-connection-library-editor-body");
      const previous = [list.scrollTop, editor.scrollTop];
      list.scrollTop += group.getBoundingClientRect().top - list.getBoundingClientRect().top;
      editor.scrollTop = showParameters ? editor.scrollHeight : 0;
      await window.captureCornerStage(name);
      [list.scrollTop, editor.scrollTop] = previous;
    };
    await capture("finished");
    const beforeFold = counts();
    await click('[data-tube-assembly-shape="orthogonal-corner"]');
    const folded = document.querySelector('[data-tube-assembly-shape="orthogonal-corner"]').getAttribute("aria-expanded") === "false";
    await click('[data-tube-assembly-shape="orthogonal-corner"]');
    const foldPreserved = folded && model.finishedProductInput(view) === product && state.preview.designParts === geometry
      && JSON.stringify(counts()) === JSON.stringify(beforeFold);
    const conditionsMatch = [];
    for (const value of ["weld", "insert", "tabs"]) {
      await choose("cJoint", value);
      const values = library.assemblyParameterValues(view);
      conditionsMatch.push(template.parameters.every((parameter) => {
        const field = document.querySelector(`[data-tube-assembly-parameter="${parameter.key}"]`);
        return !!field === conditions.parameterVisible(parameter, values) && (!field || parameter.valueType !== "number"
          || !field.closest("details") && field.min === String(parameter.min) && field.max === String(parameter.max)
          && field.step === String(parameter.step ?? 0.1));
      }));
    }
    const width = document.querySelector('[data-tube-assembly-parameter="tabWidth"]');
    await capture("tabs", true);
    width.value = "7.25"; await action("tube-designer-assembly-parameter-change", width); await settle();
    const card = document.querySelector('details[data-finished-span="armC"]');
    (model.finishedProductState(view).disclosure[shape.id] ??= {}).armC = true;
    card.open = true; render();
    const note = card.querySelector('[data-finished-profile-parameter="note"]');
    const nested = card.querySelector(".tube-connection-library-parameter-grid");
    const left = document.querySelector(".tube-connection-library-list"), right = document.querySelector(".tube-connection-library-editor-body");
    const canvas = document.querySelector("#stable");
    let inputEvents = 0, canvasEvents = 0;
    note.addEventListener("input", () => inputEvents++); canvas.addEventListener("click", () => canvasEvents++);
    note.focus({ preventScroll: true }); note.setSelectionRange(1, 3);
    left.scrollTop = 80; right.scrollTop = 90; nested.scrollTop = 25;
    await choose("abJoint", "wrap"); await choose("cJoint", "weld");
    const fieldsHidden = ["tabWidth", "tabLength", "sideClearance", "insertDepth", "fitGap"]
      .every((key) => !document.querySelector(`[data-tube-assembly-parameter="${key}"]`));
    await setView("exploded", false);
    for (let i = 0; i < 200 && !manufacturing.length; i++) {
      plans.splice(0).forEach((resolve) => resolve()); await new Promise((resolve) => setTimeout(resolve, 5));
    }
    const pending = manufacturing.length > 0;
    left.scrollTop = 125; right.scrollTop = 170; nested.scrollTop = 45;
    const scroll = [left.scrollTop, right.scrollTop, nested.scrollTop];
    await settle(); note.dispatchEvent(new Event("input")); canvas.click();
    const retained = { pending, fieldsHidden, focus: document.activeElement === note,
      selection: note.selectionStart === 1 && note.selectionEnd === 3,
      scroll: JSON.stringify([left.scrollTop, right.scrollTop, nested.scrollTop]) === JSON.stringify(scroll),
      input: document.querySelector('details[data-finished-span="armC"] [data-finished-profile-parameter="note"]') === note,
      canvas: document.querySelector("#stable") === canvas,
      viewport: document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild === viewportRoot,
      listeners: inputEvents === 1 && canvasEvents === 1, blanks: state.preview.manufacturingParts.length === 3 && state.preview.processReady,
      product: model.finishedProductInput(view) === product && state.preview.designParts === geometry };
    await setView("finished"); await choose("cJoint", "tabs");
    const restoredDraft = document.querySelector('[data-tube-assembly-parameter="tabWidth"]').value === "7.25";
    document.querySelector('[data-tube-assembly-parameter="tabWidth"]').focus({ preventScroll: true });
    await choose("cJoint", "weld");
    const removedNotRefocused = !document.activeElement.matches("input, select, textarea");
    const beforeResize = counts(); holdFinished = true;
    note.focus({ preventScroll: true }); note.setSelectionRange(0, 2);
    const length = card.querySelector('[data-cam-change-action="tube-designer-finished-length-change"]');
    length.value = "235"; await action("tube-designer-finished-length-change", length);
    for (let i = 0; i < 200 && !finished.length; i++) await new Promise((resolve) => setTimeout(resolve, 5));
    const resizePending = finished.length === 1;
    left.scrollTop = 135; right.scrollTop = 180; nested.scrollTop = 55;
    const resizeScroll = [left.scrollTop, right.scrollTop, nested.scrollTop];
    await settle(); holdFinished = false; geometry = state.preview.designParts;
    const resize = { pending: resizePending, dimensions: product.spans.armC.length === 235
        && product.spans.armA.length === 260 && product.spans.armB.length === 260,
      lazy: counts()[0] === beforeResize[0] + 1 && counts()[1] === beforeResize[1] && counts()[2] === beforeResize[2],
      focus: document.activeElement === note && note.selectionStart === 0 && note.selectionEnd === 2,
      scroll: JSON.stringify([left.scrollTop, right.scrollTop, nested.scrollTop]) === JSON.stringify(resizeScroll),
      input: document.querySelector('details[data-finished-span="armC"] [data-finished-profile-parameter="note"]') === note,
      canvas: document.querySelector("#stable") === canvas,
      viewport: document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild === viewportRoot };
    const beforeSwitches = counts(), productKey = model.finishedProductKey(product);
    const renderCases = [];
    for (const other of templates.filter((item) => !item.catalogueHidden)) {
      await action("tube-designer-assembly-select", { dataset: { tubeAssemblyId: other.id } }); await settle();
      renderCases.push(state.selectedId === other.id && model.finishedProductInput(view) === product
        && model.finishedProductKey(product) === productKey && state.preview.designParts === geometry
        && document.querySelectorAll("details[data-finished-span]").length === 3
        && document.querySelector("#stable") === canvas);
    }
    await action("tube-designer-assembly-select", { dataset: { tubeAssemblyId: template.id } }); await settle();
    const cached = state.preview.designParts === geometry && JSON.stringify(counts()) === JSON.stringify(beforeSwitches);
    return { scene, expectedLabels: shape.spans.map((span) => span.label), groups, matrices, matricesApplied,
      foldPreserved, conditionsMatch, retained, scroll, restoredDraft, removedNotRefocused, resize, resizeScroll,
      renderCases, cached, counts: counts(), manufacturingOnly: calls.filter((call) => call.method.endsWith("ResolveAssemblyTemplatePreview")
        && !call.payload.finishedOnly).every((call) => call.payload.manufacturingOnly === true) };
  }, templates);
  assert.deepEqual(errors, []);
  assert.deepEqual(result.scene.spans, ["armA", "armB", "armC"]);
  assert.deepEqual(result.scene.labels, result.expectedLabels);
  assert.deepEqual(result.scene.params, {});
  assert.equal(result.scene.label, "三向直角节点");
  assert.equal(result.scene.catalogueC, true);
  assert.ok(["l", "t", "cross", "straight", "orthogonal-corner"].every((id) => result.groups.includes(id)));
  assert.deepEqual(result.matrices, [
    { role: "armA", axis: [1, 0, 0], origin: [-260, 0, 0], length: 260 },
    { role: "armB", axis: [0, 0, 1], origin: [0, 0, 0], length: 260 },
    { role: "armC", axis: [0, 1, 0], origin: [0, 0, 0], length: 220 },
  ]);
  for (const key of ["matricesApplied", "foldPreserved", "restoredDraft", "removedNotRefocused", "cached", "manufacturingOnly"])
    assert.equal(result[key], true, `${key}: ${JSON.stringify(result)}`);
  for (const key of ["conditionsMatch", "renderCases"]) assert.ok(result[key].every(Boolean), `${key}: ${JSON.stringify(result)}`);
  for (const key of ["retained", "resize"]) assert.ok(Object.values(result[key]).every(Boolean), `${key}: ${JSON.stringify(result)}`);
  assert.ok(result.scroll.every((value) => value > 0));
  assert.ok(result.resizeScroll.every((value) => value > 0));
  console.log("Orthogonal corner Edge passed: actual descriptor/example/plans, three poses/cards, conditions and ranges, lazy blanks, latest focus/selection/dual/nested scroll, nodes/listeners, all existing template renders and cached finished geometry.");
  console.log(JSON.stringify({ groups: result.groups, matrices: result.matrices, retained: result.retained,
    scroll: result.scroll, resize: result.resize, resizeScroll: result.resizeScroll, genericTemplates: result.renderCases.length, counts: result.counts }));
} finally { await browser.close(); }
