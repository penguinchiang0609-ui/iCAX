import assert from "node:assert/strict";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { spawnSync } from "node:child_process";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
const appRoot = resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT || source, "apps/tube-designer");
const root = resolve(appRoot, "templates/assembly");
const python = process.env.ICAX_PYTHON_EXECUTABLE
  || "C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe";
const runtimeScript = `import importlib.util, json, pathlib, sys
spec = importlib.util.spec_from_file_location("browser_assembly_runtime", pathlib.Path(sys.argv[1]) / "templates/_shared/assembly_template_runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
print(json.dumps(runtime.generate(json.load(sys.stdin), {}), ensure_ascii=False))`;
const templates = readdirSync(root, { withFileTypes: true }).filter((entry) => entry.isDirectory()
  && existsSync(resolve(root, entry.name, "assembly.json")))
  .map((entry) => JSON.parse(readFileSync(resolve(root, entry.name, "assembly.json"), "utf8")));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 760 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://finished-input.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/assembly-runtime") {
      const generated = spawnSync(python, ["-B", "-c", runtimeScript, appRoot], {
        input: route.request().postData(), encoding: "utf8", windowsHide: true,
      });
      if (generated.status !== 0) return route.fulfill({ status: 500, contentType: "text/plain",
        body: generated.stderr || generated.error?.message || "assembly runtime failed" });
      return route.fulfill({ contentType: "application/json", body: generated.stdout });
    }
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    const prefix = "/src/apps/tube-designer/";
    const served = pathname.startsWith(prefix) ? resolve(appRoot, pathname.slice(prefix.length)) : file;
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(served, "utf8") });
  });
  await page.goto("http://finished-input.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}
    .cam-workbench{display:grid;grid-template-columns:290px minmax(0,1fr) 380px;height:760px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{position:relative;min-width:0;background:#13252d}
    .tube-connection-library-list{height:110px!important;max-height:110px!important;flex:none!important;overflow:auto}
    .tube-connection-library-editor-body{height:250px;overflow:auto}
    .tube-assembly-scene-member .tube-connection-library-parameter-grid{max-height:110px;overflow:auto}
  ` });
  const result = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const model = await import("/src/apps/tube-designer/webpage/finishedProductModel.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const calls = [], pending = [], pendingManufacturing = [], pendingFinished = [], pendingSnapshots = [];
    let holdFinished = false, holdSnapshot = false;
    let clickedAction;
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "bend", workMode: "example", workModeUserSelected: true },
      tubeDesignerSystemProfiles: [{ id: "rect", name: "方矩管", profileForm: "parametric", defaultParameters: {}, descriptor: { parameters: [
        { key: "note", displayName: "外形备注", valueType: "string", defaultValue: "ABCDE" },
        ...["width", "depth", "wallThickness", "cornerRadius", "innerRadius"].map((key) => ({
          key, displayName: key, valueType: "number", defaultValue: 2, min: 0,
        })),
      ] } }], viewport: { setVisibleEntityIds() {}, setContinuousRendering() {} } };
    const design = (product) => Object.entries(product.spans).map(([role, span]) => ({
      role, id: `design-${role}`, label: role, request: { ...structuredClone(span), features: [], ends: {}, fixtureKind: "design" },
      matrix: identity, compareMatrix: identity,
    }));
    const actualRuntime = async (payload) => {
      const response = await fetch("/assembly-runtime", { method: "POST", body: JSON.stringify(payload) });
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    };
    const context = { project: { projectId: "project-a" }, sceneProxy: { sceneId: "scene-a", resources: {}, async invoke(method, payload) {
      calls.push({ method, payload: structuredClone(payload) });
      if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") {
        const template = templates.find((entry) => entry.id === payload.templateId);
        if (template.exampleInput.shapeId === "orthogonal-corner")
          return actualRuntime({ action: "check-applicability", ...payload });
        const reason = model.processInputProblem(template, payload.processInput);
        return Promise.resolve({ schema: "icax.assembly-applicability", schemaVersion: 1,
          templateId: template.id, applicable: !reason, reason });
      }
      if (method === "TubeDesigner.GetAssemblyTemplateExampleProduct") {
        const template = templates.find((entry) => entry.id === payload.templateId);
        if (template.exampleInput.shapeId === "orthogonal-corner")
          return actualRuntime({ action: "example-product", ...payload });
        return Promise.resolve({ schema: "icax.assembly-example-product", schemaVersion: 1,
          templateId: template.id, finishedProduct: model.createFinishedProduct(template.exampleInput.shapeId, view) });
      }
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
        if (payload.finishedOnly) {
          const response = await actualRuntime({ action: "preview-plan", ...payload });
          for (const part of response.designParts) part.request.fixtureKind = "design";
          return holdFinished ? new Promise((resolve) => pendingFinished.push(() => resolve(response))) : Promise.resolve(response);
        }
        const template = templates.find((t) => t.id === payload.templateId);
        if (template.exampleInput.shapeId === "orthogonal-corner") {
          const response = await actualRuntime({ action: "preview-plan", ...payload });
          for (const part of response.manufacturingParts) part.request.fixtureKind = "manufacturing";
          return new Promise((resolve) => pending.push(() => resolve(response)));
        }
        return new Promise((resolve) => pending.push(() => resolve({ schema: "icax.assembly-preview-plan",
          templateId: template.id, processInput: structuredClone(payload.processInput), parameters: payload.parameters,
          sceneParameters: payload.processInput.geometry, designParts: Object.entries(payload.processInput.parts).map(([role, part]) => ({
            role, id: `design-${role}`, label: role, request: { ...structuredClone(part), features: [], ends: {} }, matrix: part.matrix })),
          resolvedWorkflow: { realization: template.manufacturingPlan.realization, checks: [], parameterEffects: {} },
          manufacturingParts: template.manufacturingPlan.blankParts.map((blank, index) => ({
            id: blank.id, label: blank.label, participantRoles: blank.participants, matrix: identity,
            request: { ...Object.values(payload.processInput.parts)[0],
              length: Object.values(payload.processInput.parts)[0].length + index + (payload.parameters.bendRadius ?? 0),
              features: [{ fixtureParameters: payload.parameters, fixtureProcessDrafts: payload.processDrafts }],
              ends: {}, fixtureKind: "manufacturing" },
          })),
        })));
      }
      if (method === "TubeDesigner.PreviewPunchWizard" || method === "TubeDesigner.PreviewAssemblyManufacturingPart") {
        const response = { previewComputed: true, resultValid: true, solidCount: 1,
          ...(method === "TubeDesigner.PreviewPunchWizard" ? { baseGeometry: { url: "memory://product", version: 1 } } : {}),
          geometry: { url: `memory://blank-${payload.previewResourceKey}`, version: 1 } };
        return payload.fixtureKind === "manufacturing"
          ? new Promise((resolve) => pendingManufacturing.push(() => resolve(response))) : Promise.resolve(response);
      }
      if (method === "TubeDesigner.EvaluateProfilePackage") return Promise.reject(new Error("no diagram in fixture"));
      throw new Error(`unexpected ${method}`);
    } } };
    const root = document.createElement("div");
    const appliedSnapshots = [];
    const dimensionAnnotations = [];
    let fitCalls = 0;
    const viewport = { root, mount(host) { host.replaceChildren(root); return this; },
      applyViewSnapshot: async (snapshot) => {
        appliedSnapshots.push(snapshot);
        const receipt = { applied: true, entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] };
        return holdSnapshot ? new Promise((resolve) => pendingSnapshots.push(() => resolve(receipt))) : receipt;
      },
      setDimensionAnnotations(annotations) { dimensionAnnotations.push(annotations); },
      getVisibleBounds: () => ({ min: [-260, -30, -20], max: [20, 30, 260] }),
      setVisibleEntityIds() {}, setStandardView() {}, fitViewToViewport() { fitCalls++; }, dispose() {} };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      let mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside>
          <div class="cam-viewport"><canvas id="stable"></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        mount = document.querySelector("main");
        mount.addEventListener("click", (event) => {
          const target = event.target.closest("[data-cam-action]");
          if (target) clickedAction = library.handleAssemblyLibraryAction(context, view,
            target.dataset.camAction, target, { renderProject: render });
        });
        patch.rememberLibraryDom(view, mount, "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) throw new Error("full redraw");
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory: () => viewport });
    };
    const settle = async (targetView = view) => {
      for (let i = 0; i < 200 && (targetView.tubeDesignerAssemblyLibrary.previewRequest
          || targetView.tubeDesignerAssemblyLibrary.selectionRequest || pendingSnapshots.length); i++) {
        pending.splice(0).forEach((resolve) => resolve());
        pendingManufacturing.splice(0).forEach((resolve) => resolve());
        pendingFinished.splice(0).forEach((resolve) => resolve());
        pendingSnapshots.splice(0).forEach((resolve) => resolve());
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
    };
    const action = (name, target) => library.handleAssemblyLibraryAction(context, view, name, target, { renderProject: render });
    const click = async (selector) => {
      const target = document.querySelector(selector);
      if (!target) throw new Error(`missing tree action ${selector}`);
      clickedAction = null;
      target.click();
      if (!clickedAction) throw new Error(`tree action was not handled ${selector}`);
      await clickedAction;
      await settle();
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    render();
    await settle();
    const state = view.tubeDesignerAssemblyLibrary;
    let product = model.finishedProductInput(view);
    const groupShapeIds = [...document.querySelectorAll('[data-cam-action="tube-designer-assembly-toggle-shape"]')]
      .map((node) => node.dataset.tubeAssemblyShape);
    const noShapePicker = !document.querySelector('select[data-cam-change-action="tube-designer-finished-shape-change"]');
    const beforeFold = { template: state.selectedId, shape: view.tubeDesignerFinishedProduct.selectedShapeId,
      product: model.finishedProductKey(product), previews: calls.length };
    await click('[data-tube-assembly-shape="t"]');
    const folded = document.querySelector('[data-tube-assembly-shape="t"]').getAttribute("aria-expanded") === "false"
      && document.querySelector('[data-tube-assembly-shape="t"]').nextElementSibling.hidden;
    await click('[data-tube-assembly-shape="t"]');
    const foldPreserved = folded && state.selectedId === beforeFold.template
      && view.tubeDesignerFinishedProduct.selectedShapeId === beforeFold.shape
      && model.finishedProductKey(product) === beforeFold.product && calls.length === beforeFold.previews;
    const original = model.finishedProductKey(product);
    const firstMesh = state.preview.finishedShape;
    const finishedCalls = () => calls.filter((c) => c.payload.finishedOnly).length;
    const processCalls = () => calls.filter((c) => c.method.endsWith("ResolveAssemblyTemplatePreview") && !c.payload.finishedOnly).length;
    const manufacturingCalls = () => calls.filter((c) => c.method.endsWith("PreviewAssemblyManufacturingPart")).length;
    const applicabilityCalls = () => calls.filter((c) => c.method.endsWith("CheckAssemblyTemplateApplicability"));
    const exampleCalls = () => calls.filter((c) => c.method.endsWith("GetAssemblyTemplateExampleProduct"));
    const counts = () => [finishedCalls(), processCalls(), manufacturingCalls()];
    const setView = async (mode, finish = true) => {
      const target = document.querySelector(`[data-cam-action="tube-designer-assembly-set-view"][data-tube-assembly-view="${mode}"]`);
      if (!target || target.disabled) throw new Error(`view action unavailable ${mode}`);
      clickedAction = null;
      target.click();
      if (!clickedAction) throw new Error(`view action was not handled ${mode}`);
      await clickedAction;
      if (finish) await settle();
    };
    const waitManufacturing = async () => {
      for (let i = 0; i < 200 && !pendingManufacturing.length && state.previewRequest; i++) {
        pending.splice(0).forEach((resolve) => resolve());
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
      if (!pendingManufacturing.length) throw new Error(`manufacturing request did not become pending: ${JSON.stringify(counts())}; ${state.previewError}`);
    };
    const select = (id) => click(`.tube-connection-library-card[data-tube-assembly-id="${id}"]`);
    const afterFirst = finishedCalls();
    const initialCallCounts = counts();
    const firstAppliedCount = appliedSnapshots.length;
    const firstExampleCount = exampleCalls().length;
    const initialFinishedOnly = afterFirst === 1 && processCalls() === 0 && manufacturingCalls() === 0
      && state.preview.processReady === false && state.preview.manufacturingParts.length === 0 && !state.exploded;
    const lTemplateIds = templates.filter((template) => template.exampleInput?.shapeId === "l" && !template.catalogueHidden)
      .map((template) => template.id);
    let allLTemplatesReuseFinished = lTemplateIds.length === 8;
    for (const id of lTemplateIds) {
      await select(id);
      allLTemplatesReuseFinished &&= state.preview.finishedShape === firstMesh && finishedCalls() === afterFirst
        && processCalls() === 0 && manufacturingCalls() === 0 && !state.exploded
        && appliedSnapshots.length === firstAppliedCount && exampleCalls().length === firstExampleCount
        && model.finishedProductInput(view) === product;
    }
    await select("bend");
    const sharedLCallCounts = counts();
    const card = document.querySelector('[data-finished-span="armA"]');
    card.open = true;
    view.tubeDesignerFinishedProduct.disclosure.l = { armA: true };
    render();
    const note = document.querySelector('[data-finished-span="armA"] [data-finished-profile-parameter="note"]');
    note.value = "ABCDE";
    note.focus({ preventScroll: true }); note.setSelectionRange(1, 4);
    let listenerCalls = 0;
    note.addEventListener("input", () => listenerCalls++);
    const canvas = document.querySelector("#stable");
    const left = document.querySelector(".tube-connection-library-list");
    const right = document.querySelector(".tube-connection-library-editor-body");
    const nested = card.querySelector(".tube-connection-library-parameter-grid");
    left.scrollTop = 70; right.scrollTop = 100; nested.scrollTop = 30;
    const radius = document.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    radius.value = "55";
    await action("tube-designer-assembly-parameter-change", radius);
    await settle();
    const editLazyBeforeManufacturing = processCalls() === 0 && manufacturingCalls() === 0
      && state.preview.finishedShape === firstMesh;
    await setView("exploded", false);
    await waitManufacturing();
    const manufacturingPendingDuringInteraction = state.previewRequest?.kind === "manufacturing"
      && pendingManufacturing.length > 0;
    left.scrollTop = 90; right.scrollTop = 130; nested.scrollTop = 40;
    const expectedScroll = [left.scrollTop, right.scrollTop, nested.scrollTop];
    await settle();
    note.dispatchEvent(new Event("input"));
    const retained = document.activeElement === note && note.selectionStart === 1 && note.selectionEnd === 4
      && document.querySelector("#stable") === canvas && document.querySelector('[data-finished-profile-parameter="note"]') === note
      && document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild === root
      && JSON.stringify([left.scrollTop, right.scrollTop, nested.scrollTop]) === JSON.stringify(expectedScroll) && listenerCalls === 1;
    const processEditPreserved = original === model.finishedProductKey(product) && state.preview.finishedShape === firstMesh;
    const oneBlank = state.preview.manufacturingParts.length;
    const afterFirstManufacturing = counts();
    await setView("finished");
    await setView("exploded");
    await setView("finished");
    const repeatedViewsReuseManufacturing = JSON.stringify(counts()) === JSON.stringify(afterFirstManufacturing);
    const beforeDeferredEdit = counts();
    const deferredRadius = document.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    deferredRadius.value = "61";
    await action("tube-designer-assembly-parameter-change", deferredRadius);
    await settle();
    const processEditInFinishedLazy = JSON.stringify(counts()) === JSON.stringify(beforeDeferredEdit)
      && state.preview.finishedShape === firstMesh;
    await setView("exploded");
    const editedManufacturingRegenerated = processCalls() === beforeDeferredEdit[1] + 1
      && manufacturingCalls() === beforeDeferredEdit[2] + 1 && state.preview.processReady;
    await setView("finished");
    const beforeProcessSwitch = counts();
    const processBeforeSwitch = processCalls();
    await select("two-end-end-angle");
    const processSwitchLazy = processCalls() === processBeforeSwitch && manufacturingCalls() === beforeProcessSwitch[2]
      && state.preview.manufacturingParts.length === 0 && !state.exploded;
    const switchPreserved = original === model.finishedProductKey(product) && state.preview.finishedShape === firstMesh;
    await setView("exploded", false);
    await waitManufacturing();
    const beforeManufacturingCancel = counts();
    await setView("finished");
    const finishedViewStopsRemainingBlanks = !state.exploded && !state.previewRequest && !state.preview.processReady
      && state.preview.finishedShape === firstMesh && state.preview.manufacturingParts.length === 0
      && JSON.stringify(counts()) === JSON.stringify(beforeManufacturingCancel);
    await setView("exploded");
    const twoBlanks = state.preview.manufacturingParts.length;
    await setView("finished");
    const processBeforeReset = processCalls();
    await action("tube-designer-assembly-reset", { dataset: { tubeAssemblyId: "two-end-end-angle" } });
    await settle();
    const resetPreserved = original === model.finishedProductKey(product) && finishedCalls() === afterFirst
      && state.preview.finishedShape === firstMesh && processCalls() === processBeforeReset;
    const angle = document.querySelector('[data-finished-parameter="angle"]');
    angle.value = "170";
    await action("tube-designer-finished-parameter-change", angle);
    await settle();
    const unsupportedFinishedOnly = product.parameters.angle === 170 && state.preview.finishedShape.mesh.metadata.angle === 170
      && !state.previewError && processCalls() === processBeforeReset;
    await setView("exploded");
    const blockedKeepsProduct = product.parameters.angle === 170 && state.preview.finishedShape.mesh.metadata.angle === 170
      && state.previewError.includes("不支持") && state.preview.manufacturingParts.length === 0;
    await setView("finished");
    const beforeUnsupportedSelection = { examples: exampleCalls().length, finished: finishedCalls(), product };
    await select("bend");
    const supportedSelectionKeepsCurrentProduct = model.finishedProductInput(view) === product
      && product.parameters.angle === 170 && exampleCalls().length === beforeUnsupportedSelection.examples
      && finishedCalls() === beforeUnsupportedSelection.finished;
    await select("two-end-end-angle");
    const refusedMiter = applicabilityCalls().findLast((call) => call.payload.templateId === "two-end-end-angle");
    product = model.finishedProductInput(view);
    const unsupportedSelectionKeepsProduct = Math.abs(refusedMiter.payload.processInput.geometry.jointAngle - 170) < 1e-7
      && exampleCalls().length === beforeUnsupportedSelection.examples
      && product === beforeUnsupportedSelection.product && product.parameters.angle === 170
      && state.preview.finishedShape.mesh.metadata.angle === 170 && state.selectionProblem.includes("不支持");
    await click('[data-cam-action="tube-designer-assembly-load-example"]');
    product = model.finishedProductInput(view);
    const exampleRequiresExplicitAction = exampleCalls().length === beforeUnsupportedSelection.examples + 1
      && product !== beforeUnsupportedSelection.product && product.parameters.angle === 90;
    const oldSizeMesh = state.preview.finishedShape;
    const beforeResize = counts();
    holdFinished = true;
    const firstLCard = document.querySelector('[data-finished-span]');
    firstLCard.open = true;
    view.tubeDesignerFinishedProduct.disclosure.l = { [firstLCard.dataset.finishedSpan]: true };
    render();
    const lLength = document.querySelector('[data-cam-change-action="tube-designer-finished-length-change"]');
    lLength.value = "421";
    await action("tube-designer-finished-length-change", lLength);
    for (let i = 0; i < 200 && !pendingFinished.length; i++) await new Promise((resolve) => setTimeout(resolve, 5));
    const changedSizeHidesOldCache = pendingFinished.length === 1 && state.preview?.finishedShape !== oldSizeMesh;
    const inFlightSelection = action("tube-designer-assembly-select", { dataset: { tubeAssemblyId: "bend" } });
    await settle();
    await inFlightSelection;
    await settle();
    holdFinished = false;
    const resizedLMesh = state.preview.finishedShape;
    const inFlightFinishedSharedAcrossL = state.selectedId === "bend" && finishedCalls() === beforeResize[0] + 1;
    const changedSizeRecomputed = resizedLMesh !== oldSizeMesh && finishedCalls() === beforeResize[0] + 1
      && processCalls() === beforeResize[1] && manufacturingCalls() === beforeResize[2];
    const beforeSnapshotRace = { snapshots: appliedSnapshots.length, fit: fitCalls, counts: counts() };
    holdSnapshot = true;
    const snapshotLength = document.querySelector('[data-cam-change-action="tube-designer-finished-length-change"]');
    snapshotLength.value = "422";
    await action("tube-designer-finished-length-change", snapshotLength);
    for (let i = 0; i < 200 && !pendingSnapshots.length; i++) await new Promise((resolve) => setTimeout(resolve, 5));
    const snapshotSelection = action("tube-designer-assembly-select", { dataset: { tubeAssemblyId: "two-end-end-angle" } });
    for (let i = 0; i < 200 && state.selectionRequest; i++) await new Promise((resolve) => setTimeout(resolve, 5));
    const snapshotSharedWhilePending = pendingSnapshots.length === 1
      && appliedSnapshots.length === beforeSnapshotRace.snapshots + 1
      && finishedCalls() === beforeSnapshotRace.counts[0] + 1;
    holdSnapshot = false;
    await settle();
    await snapshotSelection;
    await settle();
    const pendingSnapshotAppliedToLatestSelection = state.selectedId === "two-end-end-angle" && !!state.appliedKey
      && appliedSnapshots.at(-1).revision === `assembly-${state.appliedKey}`
      && state.appliedKey.endsWith(state.preview.finishedKey) && dimensionAnnotations.at(-1)?.length > 0;
    render();
    await settle();
    await select("bend");
    const sharedSnapshotDoesNotRepeatApplyOrFit = appliedSnapshots.length === beforeSnapshotRace.snapshots + 1
      && fitCalls === beforeSnapshotRace.fit && processCalls() === beforeSnapshotRace.counts[1]
      && manufacturingCalls() === beforeSnapshotRace.counts[2];
    const snapshotRaceCounts = { before: beforeSnapshotRace.snapshots, after: appliedSnapshots.length,
      fitBefore: beforeSnapshotRace.fit, fitAfter: fitCalls, annotations: dimensionAnnotations.at(-1)?.length ?? 0 };
    await action("tube-designer-finished-reset", { dataset: {} });
    await settle();
    product = model.finishedProductInput(view);
    const defaultLMesh = state.preview.finishedShape;
    const lDraft = model.finishedProductKey(product);
    const selectedShapes = [];
    const shape = async (id) => {
      await action("tube-designer-finished-shape-change", { value: id });
      await settle();
    };
    const examplesBeforeShapeChanges = exampleCalls().length;
    await shape("t"); await select("t-contact-fit");
    selectedShapes.push(view.tubeDesignerFinishedProduct.selectedShapeId);
    const tProduct = model.finishedProductInput(view), tDraft = model.finishedProductKey(tProduct);
    const tGeometry = state.preview.designParts;
    await select("end-side-tab-slot");
    const sameSceneProcessPreserved = model.finishedProductInput(view) === tProduct
      && model.finishedProductKey(model.finishedProductInput(view)) === tDraft;
    await shape("cross"); await select("cross-through");
    selectedShapes.push(view.tubeDesignerFinishedProduct.selectedShapeId);
    const crossProduct = model.finishedProductInput(view), crossDraft = model.finishedProductKey(crossProduct);
    const crossGeometry = state.preview.designParts;
    await shape("straight"); await select("insert-sleeve");
    selectedShapes.push(view.tubeDesignerFinishedProduct.selectedShapeId);
    const straightSceneLabel = document.querySelector('[data-tube-assembly-scene-parameters] > header > small').textContent;
    const straightProduct = model.finishedProductInput(view), straightDraft = model.finishedProductKey(straightProduct);
    const straightGeometry = state.preview.designParts;
    const beforeCachedReturns = counts();
    await shape("l"); await select("bend");
    selectedShapes.push(view.tubeDesignerFinishedProduct.selectedShapeId);
    const returnedLDraft = model.finishedProductKey(model.finishedProductInput(view)) === lDraft;
    const returnedLGeometry = state.preview.finishedShape === defaultLMesh;
    await shape("t"); await select("t-contact-fit");
    const returnedTDraft = model.finishedProductKey(model.finishedProductInput(view)) === tDraft;
    const returnedTGeometry = state.preview.designParts === tGeometry;
    await shape("cross"); await select("cross-through");
    const returnedCrossDraft = model.finishedProductKey(model.finishedProductInput(view)) === crossDraft;
    const returnedCrossGeometry = state.preview.designParts === crossGeometry;
    await shape("straight"); await select("insert-sleeve");
    const returnedStraightDraft = model.finishedProductKey(model.finishedProductInput(view)) === straightDraft;
    const returnedStraightGeometry = state.preview.designParts === straightGeometry;
    await shape("l"); await select("bend");
    const independentSceneSamples = new Set([product, tProduct, crossProduct, straightProduct]).size === 4
      && returnedLDraft && returnedTDraft && returnedCrossDraft && returnedStraightDraft
      && model.finishedProductKey(product) === lDraft;
    const returnedSceneGeometryCached = returnedLGeometry && returnedTGeometry && returnedCrossGeometry
      && returnedStraightGeometry && JSON.stringify(counts()) === JSON.stringify(beforeCachedReturns);
    const sceneChangesAreIndependent = exampleCalls().length === examplesBeforeShapeChanges;
    const legacyParallel = model.createFinishedProduct("parallel", view);
    legacyParallel.spans[Object.keys(legacyParallel.spans)[0]].length = 853;
    const legacyView = { ...view,
      tubeDesignerAssemblyLibrary: { selectedId: "mechanical-fastener", workMode: "example", workModeUserSelected: true },
      tubeDesignerFinishedProduct: { selectedShapeId: "parallel", drafts: { parallel: legacyParallel } } };
    library.renderAssemblyLibraryLeftPane(context, legacyView);
    const migratedSelection = legacyView.tubeDesignerAssemblyLibrary.selectedId;
    await library.handleAssemblyLibraryAction(context, legacyView, "tube-designer-assembly-select",
      { dataset: { tubeAssemblyId: "mechanical-fastener" } }, { renderProject: () => {} });
    library.ensureAssemblyLibraryPreview(context, legacyView);
    await settle(legacyView);
    const legacyParallelPreserved = migratedSelection === "mechanical-fastener"
      && legacyView.tubeDesignerAssemblyLibrary.selectedId === migratedSelection
      && legacyView.tubeDesignerFinishedProduct.selectedShapeId === "parallel"
      && legacyView.tubeDesignerFinishedProduct.drafts.parallel === legacyParallel
      && legacyParallel.spans[Object.keys(legacyParallel.spans)[0]].length === 853;
    await select("two-end-end-angle");
    const supportedAngle = document.querySelector('[data-finished-parameter="angle"]');
    supportedAngle.value = "90";
    await action("tube-designer-finished-parameter-change", supportedAngle);
    await settle();
    await select("wrap-a-over-b");
    const setMaleFemale = async (enabled) => {
      const field = document.querySelector('[data-tube-assembly-parameter="maleFemale"]');
      field.checked = enabled;
      await action("tube-designer-assembly-parameter-change", field);
    };
    const conditionalKeys = ["pairCount", "tabWidth", "tabLength", "sideClearance"];
    const directConditionalFields = () => {
      const section = document.querySelector('[data-tube-assembly-parameter="maleFemale"]')
        .closest("section.tube-connection-library-parameter-section.basic");
      return section?.querySelector("header > strong")?.textContent === "工艺参数"
        && conditionalKeys.every((key) => {
          const field = document.querySelector(`[data-tube-assembly-parameter="${key}"]`);
          return field && field.closest("section.tube-connection-library-parameter-section") === section
            && !field.closest("details") && field.getClientRects().length > 0;
        })
        && ![...document.querySelectorAll("details.tube-connection-library-parameter-section > summary > span")]
          .some((summary) => summary.textContent === "更多参数");
    };
    const conditionalDraft = () => JSON.stringify(conditionalKeys.map((key) => state.parameterDrafts["wrap-a-over-b"][key]));
    await setMaleFemale(true);
    await settle();
    const conditionalFieldsDirectInitially = directConditionalFields();
    const conditionalWidth = document.querySelector('[data-tube-assembly-parameter="tabWidth"]');
    conditionalWidth.value = "24.2";
    await action("tube-designer-assembly-parameter-change", conditionalWidth);
    await settle();
    const conditionalDraftBeforeHide = conditionalDraft();
    view.tubeDesignerFinishedProduct.disclosure.l = { armA: true };
    render();
    const wrapCard = document.querySelector('[data-finished-span="armA"]');
    wrapCard.open = true;
    const wrapNote = wrapCard.querySelector('[data-finished-profile-parameter="note"]');
    const wrapNested = wrapCard.querySelector(".tube-connection-library-parameter-grid");
    const wrapLeft = document.querySelector(".tube-connection-library-list");
    const wrapRight = document.querySelector(".tube-connection-library-editor-body");
    let wrapInputs = 0, canvasClicks = 0;
    wrapNote.addEventListener("input", () => wrapInputs++);
    canvas.addEventListener("click", () => canvasClicks++);
    wrapNote.focus({ preventScroll: true }); wrapNote.setSelectionRange(1, 4);
    wrapLeft.scrollTop = 70; wrapRight.scrollTop = 80; wrapNested.scrollTop = 30;
    await setMaleFemale(false);
    await settle();
    await setView("exploded", false);
    await waitManufacturing();
    wrapLeft.scrollTop = 100; wrapRight.scrollTop = 110; wrapNested.scrollTop = 50;
    const conditionalScroll = [wrapLeft.scrollTop, wrapRight.scrollTop, wrapNested.scrollTop];
    const conditionalResponsePending = pendingManufacturing.length > 0 && state.previewRequest?.kind === "manufacturing";
    await settle();
    wrapNote.dispatchEvent(new Event("input"));
    canvas.click();
    const conditionChecks = {
      responsePending: conditionalResponsePending,
      removed: conditionalKeys.every((key) => !document.querySelector(`[data-tube-assembly-parameter="${key}"]`)),
      draft: state.parameterDrafts["wrap-a-over-b"].tabWidth === 24.2 && conditionalDraft() === conditionalDraftBeforeHide,
      focus: document.activeElement === wrapNote && wrapNote.selectionStart === 1 && wrapNote.selectionEnd === 4,
      inputNode: document.querySelector('[data-finished-span="armA"] [data-finished-profile-parameter="note"]') === wrapNote,
      canvasNode: document.querySelector("#stable") === canvas,
      viewportNode: document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild === root,
      listeners: wrapInputs === 1 && canvasClicks === 1,
      scroll: JSON.stringify([wrapLeft.scrollTop, wrapRight.scrollTop, wrapNested.scrollTop]) === JSON.stringify(conditionalScroll),
    };
    const conditionPreservedInteraction = Object.values(conditionChecks).every(Boolean);
    await setView("finished");
    await setMaleFemale(true);
    await settle();
    const restoredConditionalWidth = document.querySelector('[data-tube-assembly-parameter="tabWidth"]');
    const conditionalDraftRestored = restoredConditionalWidth.value === "24.2"
      && conditionalDraft() === conditionalDraftBeforeHide;
    const conditionalFieldsDirectRestored = directConditionalFields();
    restoredConditionalWidth.focus({ preventScroll: true });
    await setMaleFemale(false);
    const removedFieldNotRefocused = !document.querySelector('[data-tube-assembly-parameter="tabWidth"]')
      && !document.activeElement.matches("input, select, textarea");
    await settle();
    const secondConditionLength = wrapCard.querySelector('[data-cam-change-action="tube-designer-finished-length-change"]');
    secondConditionLength.value = "423";
    await action("tube-designer-finished-length-change", secondConditionLength);
    await settle();
    await setView("exploded", false);
    await waitManufacturing();
    wrapNote.focus({ preventScroll: true }); wrapNote.setSelectionRange(0, 2);
    wrapLeft.scrollTop = 120; wrapRight.scrollTop = 140; wrapNested.scrollTop = 60;
    const afterRemovalScroll = [wrapLeft.scrollTop, wrapRight.scrollTop, wrapNested.scrollTop];
    await settle();
    const laterInteractionPreserved = document.activeElement === wrapNote
      && wrapNote.selectionStart === 0 && wrapNote.selectionEnd === 2
      && JSON.stringify([wrapLeft.scrollTop, wrapRight.scrollTop, wrapNested.scrollTop]) === JSON.stringify(afterRemovalScroll);
    const blankUrls = () => state.preview.manufacturingParts.map((part) => part.response.geometry.url);
    const restoredWrapUrls = blankUrls();
    await select("bend");
    await setView("exploded");
    const beforeReturningTemplate = counts();
    await select("wrap-a-over-b");
    await setView("exploded");
    const templatesReuseManufacturing = JSON.stringify(counts()) === JSON.stringify(beforeReturningTemplate)
      && JSON.stringify(blankUrls()) === JSON.stringify(restoredWrapUrls) && state.preview.processReady;
    const cacheInvalidations = [];
    for (const mutate of [
      () => { templates.find((item) => item.id === "wrap-a-over-b").generationDigest = "a".repeat(64); },
      () => { view.tubeDesignerSystemProfiles[0].packageDigest = "profile-next"; },
      () => { context.project.projectId = "project-b"; },
      () => { context.sceneProxy.sceneId = "scene-b"; },
    ]) {
      const before = counts(), previousUrls = blankUrls();
      mutate(); render(); await settle();
      cacheInvalidations.push(processCalls() === before[1] + 1 && manufacturingCalls() === before[2] + 2
        && JSON.stringify(blankUrls()) !== JSON.stringify(previousUrls) && state.preview.processReady);
    }
    await setView("finished");
    // Leave a new configuration pending while switching views twice. The
    // current consumer must attach to the original native request.
    const cacheLength = document.querySelector('[data-finished-span="armA"] [data-cam-change-action="tube-designer-finished-length-change"]');
    cacheLength.value = "431";
    await action("tube-designer-finished-length-change", cacheLength); await settle();
    const beforeInFlightCache = counts();
    await setView("exploded", false); await waitManufacturing();
    await setView("finished", false);
    await setView("exploded", false);
    const inFlightCacheCounts = counts();
    await settle();
    const manufacturingInFlightShared = processCalls() === beforeInFlightCache[1] + 1
      && manufacturingCalls() === beforeInFlightCache[2] + 2 && state.preview.processReady
      && inFlightCacheCounts[1] === beforeInFlightCache[1] + 1
      && inFlightCacheCounts[2] === beforeInFlightCache[2] + 1;
    const cornerTemplate = templates.find((entry) => entry.exampleInput?.shapeId === "orthogonal-corner"
      && !entry.catalogueHidden);
    if (!cornerTemplate) throw new Error("actual orthogonal corner descriptor missing");
    const beforeCornerSelection = { product: model.finishedProductInput(view), key: model.finishedProductKey(model.finishedProductInput(view)) };
    await select(cornerTemplate.id);
    const missingRoleSelectionKeepsProduct = state.selectedId === cornerTemplate.id && !!state.selectionProblem
      && !state.previewError && model.finishedProductInput(view) === beforeCornerSelection.product
      && model.finishedProductKey(model.finishedProductInput(view)) === beforeCornerSelection.key
      && state.preview.independentFinishedProduct;
    await click('[data-cam-action="tube-designer-assembly-load-example"]');
    const cornerProduct = model.finishedProductInput(view);
    let cornerFinished = state.preview.designParts;
    const cornerShape = model.finishedProductShape(cornerProduct.shapeId, view);
    const cornerCards = [...document.querySelectorAll("details[data-finished-span]")];
    const cornerScene = {
      roles: cornerCards.map((card) => card.dataset.finishedSpan),
      labels: cornerCards.map((card) => card.querySelector("summary strong").textContent),
      shapeLabel: document.querySelector('[data-tube-assembly-scene-parameters] > header > small').textContent,
      params: cornerProduct.parameters,
      example: exampleCalls().some((call) => call.payload.templateId === cornerTemplate.id),
    };
    const cornerMatrices = state.preview.plan.designParts.map((part) => ({
      role: part.role, axis: [part.matrix[0], part.matrix[4], part.matrix[8]],
      origin: [part.matrix[3], part.matrix[7], part.matrix[11]], length: part.request.length,
    }));
    const cornerRows = library.assemblySceneRows(state.preview);
    const cornerTransformsApplied = cornerRows.length === 3 && cornerRows.every((row, index) =>
      JSON.stringify(row.data.localToWorldMatrix) === JSON.stringify(state.preview.designParts[index].matrix));
    const beforeCornerFold = counts();
    await click('[data-tube-assembly-shape="orthogonal-corner"]');
    const cornerFolded = document.querySelector('[data-tube-assembly-shape="orthogonal-corner"]')
      .getAttribute("aria-expanded") === "false";
    await click('[data-tube-assembly-shape="orthogonal-corner"]');
    const cornerFoldPreserved = cornerFolded && model.finishedProductInput(view) === cornerProduct
      && state.preview.designParts === cornerFinished && JSON.stringify(counts()) === JSON.stringify(beforeCornerFold);
    const setCornerChoice = async (key, value) => {
      const field = document.querySelector(`[data-tube-assembly-parameter="${key}"]`);
      if (!field) throw new Error(`actual corner field missing ${key}: ${state.selectedId}; ${document.querySelector('.tube-connection-library-editor')?.textContent.slice(0, 1100)}`);
      field.value = value;
      await action("tube-designer-assembly-parameter-change", field);
      await settle();
    };
    await setCornerChoice("abJoint", "wrap");
    await setCornerChoice("cJoint", "tabs");
    const cornerConditionalKeys = ["tabWidth", "tabLength", "sideClearance"];
    const cornerDeclarativeFields = cornerConditionalKeys.every((key) => {
      const definition = cornerTemplate.parameters.find((parameter) => parameter.key === key);
      const field = document.querySelector(`[data-tube-assembly-parameter="${key}"]`);
      return definition.level === "basic" && field && !field.closest("details")
        && field.min === String(definition.min) && field.max === String(definition.max)
        && field.step === String(definition.step ?? 0.1);
    });
    const cornerWidth = document.querySelector('[data-tube-assembly-parameter="tabWidth"]');
    const cornerWidthValue = cornerWidth.value;
    const cornerCard = document.querySelector('[data-finished-span="armC"]');
    view.tubeDesignerFinishedProduct.disclosure[cornerProduct.shapeId] = { armC: true };
    cornerCard.open = true; render();
    const cornerNote = cornerCard.querySelector('[data-finished-profile-parameter="note"]');
    const cornerNested = cornerCard.querySelector(".tube-connection-library-parameter-grid");
    let cornerInputs = 0, cornerCanvasClicks = 0;
    cornerNote.addEventListener("input", () => cornerInputs++);
    canvas.addEventListener("click", () => cornerCanvasClicks++);
    cornerNote.focus({ preventScroll: true }); cornerNote.setSelectionRange(1, 3);
    left.scrollTop = 80; right.scrollTop = 90; cornerNested.scrollTop = 25;
    await setCornerChoice("cJoint", "weld");
    const cornerFieldsHidden = cornerConditionalKeys.every((key) =>
      !document.querySelector(`[data-tube-assembly-parameter="${key}"]`));
    await setView("exploded", false);
    await waitManufacturing();
    left.scrollTop = 125; right.scrollTop = 170; cornerNested.scrollTop = 45;
    const cornerScroll = [left.scrollTop, right.scrollTop, cornerNested.scrollTop];
    const cornerResponsePending = pendingManufacturing.length > 0;
    await settle();
    cornerNote.dispatchEvent(new Event("input")); canvas.click();
    const cornerInteraction = {
      pending: cornerResponsePending,
      hidden: cornerFieldsHidden,
      focus: document.activeElement === cornerNote && cornerNote.selectionStart === 1 && cornerNote.selectionEnd === 3,
      scroll: JSON.stringify([left.scrollTop, right.scrollTop, cornerNested.scrollTop]) === JSON.stringify(cornerScroll),
      inputNode: document.querySelector('[data-finished-span="armC"] [data-finished-profile-parameter="note"]') === cornerNote,
      canvasNode: document.querySelector("#stable") === canvas,
      viewportNode: document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild === root,
      listeners: cornerInputs === 1 && cornerCanvasClicks === 1,
      blanks: state.preview.manufacturingParts.length === 3 && state.preview.processReady,
      product: model.finishedProductInput(view) === cornerProduct && state.preview.designParts === cornerFinished,
    };
    await setView("finished");
    await setCornerChoice("cJoint", "tabs");
    const cornerRestoredDraft = document.querySelector('[data-tube-assembly-parameter="tabWidth"]').value === cornerWidthValue;
    document.querySelector('[data-tube-assembly-parameter="tabWidth"]').focus({ preventScroll: true });
    await setCornerChoice("cJoint", "weld");
    const cornerRemovedFieldNotRefocused = !document.querySelector('[data-tube-assembly-parameter="tabWidth"]')
      && !document.activeElement.matches("input, select, textarea");
    cornerNote.focus({ preventScroll: true }); cornerNote.setSelectionRange(0, 2);
    left.scrollTop = 85; right.scrollTop = 95; cornerNested.scrollTop = 25;
    const cornerBeforeResize = counts();
    holdFinished = true;
    const cornerLength = cornerCard.querySelector('[data-cam-change-action="tube-designer-finished-length-change"]');
    cornerLength.value = "235";
    await action("tube-designer-finished-length-change", cornerLength);
    for (let i = 0; i < 100 && !pendingFinished.length; i++)
      await new Promise((resolve) => setTimeout(resolve, 5));
    const cornerSizeResponsePending = pendingFinished.length === 1;
    left.scrollTop = 135; right.scrollTop = 180; cornerNested.scrollTop = 55;
    const cornerResizeScroll = [left.scrollTop, right.scrollTop, cornerNested.scrollTop];
    await settle();
    holdFinished = false;
    cornerFinished = state.preview.designParts;
    const cornerResize = {
      pending: cornerSizeResponsePending,
      dimensions: cornerProduct.spans.armC.length === 235 && cornerProduct.spans.armA.length === 260
        && cornerProduct.spans.armB.length === 260,
      lazy: finishedCalls() === cornerBeforeResize[0] + 1 && processCalls() === cornerBeforeResize[1]
        && manufacturingCalls() === cornerBeforeResize[2],
      focus: document.activeElement === cornerNote && cornerNote.selectionStart === 0 && cornerNote.selectionEnd === 2,
      scroll: JSON.stringify([left.scrollTop, right.scrollTop, cornerNested.scrollTop]) === JSON.stringify(cornerResizeScroll),
      inputNode: document.querySelector('[data-finished-span="armC"] [data-finished-profile-parameter="note"]') === cornerNote,
      canvasNode: document.querySelector("#stable") === canvas,
      viewportNode: document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild === root,
    };
    const beforeCornerReturn = counts();
    await select("bend");
    await select(cornerTemplate.id);
    const cornerFinishedCached = state.preview.designParts === cornerFinished
      && model.finishedProductInput(view) === cornerProduct && JSON.stringify(counts()) === JSON.stringify(beforeCornerReturn);
    await select("bend");
    const independentRequests = calls.filter((c) => c.method.endsWith("ResolveAssemblyTemplatePreview"));
    return { groupShapeIds, noShapePicker, foldPreserved, lTemplateIds, initialCallCounts, sharedLCallCounts,
      snapshotRaceCounts, callCounts: { finished: finishedCalls(), plans: processCalls(), manufacturing: manufacturingCalls(),
        applicability: applicabilityCalls().length, examples: exampleCalls().length },
      initialFinishedOnly, allLTemplatesReuseFinished, editLazyBeforeManufacturing, manufacturingPendingDuringInteraction,
      repeatedViewsReuseManufacturing, processEditInFinishedLazy, editedManufacturingRegenerated,
      processSwitchLazy, finishedViewStopsRemainingBlanks, inFlightFinishedSharedAcrossL,
      snapshotSharedWhilePending, pendingSnapshotAppliedToLatestSelection, sharedSnapshotDoesNotRepeatApplyOrFit,
      supportedSelectionKeepsCurrentProduct, unsupportedSelectionKeepsProduct, exampleRequiresExplicitAction, sceneChangesAreIndependent,
      templateApplicabilityReceivesCurrentProduct: applicabilityCalls().length >= lTemplateIds.length
        && applicabilityCalls().every((call) => call.payload.processInput?.schema === "icax.assembly-process-input" && !call.payload.finishedProduct && !call.payload.processInput.shapeId),
      unsupportedFinishedOnly, changedSizeHidesOldCache, changedSizeRecomputed, returnedSceneGeometryCached,
      selectedShapes, straightSceneLabel, sameSceneProcessPreserved, independentSceneSamples,
      legacyParallelPreserved, conditionPreservedInteraction, conditionChecks, conditionalScroll,
      conditionalFieldsDirectInitially, conditionalFieldsDirectRestored,
      conditionalDraftRestored, removedFieldNotRefocused, laterInteractionPreserved,
      templatesReuseManufacturing, cacheInvalidations, manufacturingInFlightShared,
      missingRoleSelectionKeepsProduct, cornerScene, cornerMatrices, cornerTransformsApplied, cornerFoldPreserved, cornerDeclarativeFields,
      cornerInteraction, cornerScroll, cornerRestoredDraft, cornerRemovedFieldNotRefocused, cornerFinishedCached,
      cornerResize, cornerResizeScroll,
      cornerExpectedLabels: cornerShape.spans.map((span) => span.label),
      manufacturingPlanOnly: independentRequests.filter((c) => !c.payload.finishedOnly)
        .every((c) => c.payload.manufacturingOnly === true),
      retained, expectedScroll, processEditPreserved, switchPreserved, resetPreserved,
      oneBlank, twoBlanks, blockedKeepsProduct,
      finishedHasNoProcess: independentRequests.filter((c) => c.payload.finishedOnly).every((c) => !c.payload.templateId && !c.payload.parameters),
      processHasInput: independentRequests.filter((c) => !c.payload.finishedOnly).every((c) => c.payload.processInput
        && !c.payload.finishedProduct && !c.payload.processInput.shapeId && !c.payload.sceneParts && !c.payload.sceneParameters && !Object.hasOwn(c.payload.parameters, "angle")),
      inputs: [...document.querySelectorAll('[data-tube-assembly-scene-parameters] summary strong')].map((node) => node.textContent),
    };
  }, templates);
  assert.deepEqual(errors, []);
  assert.equal(result.oneBlank, 1);
  assert.equal(result.twoBlanks, 2);
  assert.deepEqual(new Set(result.groupShapeIds), new Set(templates.filter((item) => !item.catalogueHidden
    && item.exampleInput?.shapeId).map((item) => item.inputContract?.supportedInputModes?.includes("stock-operation") ? "fold-processing" : item.layoutShape)));
  assert.equal(result.noShapePicker, false);
  assert.equal(result.foldPreserved, true);
  assert.deepEqual(result.selectedShapes, ["t", "cross", "straight", "l"]);
  assert.equal(result.straightSceneLabel, "直线形");
  assert.equal(result.sameSceneProcessPreserved, true);
  assert.equal(result.independentSceneSamples, true);
  assert.deepEqual(result.cornerScene.roles, ["armA", "armB", "armC"]);
  assert.deepEqual(result.cornerScene.labels, result.cornerExpectedLabels);
  assert.equal(result.cornerScene.shapeLabel, "三向直角节点");
  assert.deepEqual(result.cornerScene.params, {});
  assert.equal(result.cornerScene.example, true);
  assert.deepEqual(result.cornerMatrices, [
    { role: "armA", axis: [1, 0, 0], origin: [-260, 0, 0], length: 260 },
    { role: "armB", axis: [0, 0, 1], origin: [0, 0, 0], length: 260 },
    { role: "armC", axis: [0, 1, 0], origin: [0, 0, 0], length: 220 },
  ]);
  assert.ok(result.cornerScroll.every((value) => value > 0));
  assert.ok(Object.values(result.cornerInteraction).every(Boolean), JSON.stringify(result.cornerInteraction));
  assert.ok(result.cornerResizeScroll.every((value) => value > 0));
  assert.ok(Object.values(result.cornerResize).every(Boolean), JSON.stringify(result.cornerResize));
  for (const key of ["missingRoleSelectionKeepsProduct", "cornerTransformsApplied", "cornerFoldPreserved", "cornerDeclarativeFields",
    "cornerRestoredDraft", "cornerRemovedFieldNotRefocused", "cornerFinishedCached"])
    assert.equal(result[key], true, `${key}: ${JSON.stringify(result)}`);
  assert.ok(result.conditionalScroll.every((value) => value > 0), JSON.stringify(result));
  for (const key of ["legacyParallelPreserved", "conditionalFieldsDirectInitially", "conditionalFieldsDirectRestored",
    "conditionPreservedInteraction", "conditionalDraftRestored",
    "removedFieldNotRefocused", "laterInteractionPreserved"]) assert.equal(result[key], true, `${key}: ${JSON.stringify(result)}`);
  assert.ok(result.cacheInvalidations.every(Boolean), `cache invalidations: ${JSON.stringify(result)}`);
  for (const key of ["templatesReuseManufacturing", "manufacturingInFlightShared", "manufacturingPlanOnly"])
    assert.equal(result[key], true, `${key}: ${JSON.stringify(result)}`);
  assert.ok(result.expectedScroll.every((value) => value > 0));
  for (const key of ["retained", "processEditPreserved", "switchPreserved", "resetPreserved", "blockedKeepsProduct",
    "finishedHasNoProcess", "processHasInput", "initialFinishedOnly", "allLTemplatesReuseFinished",
    "editLazyBeforeManufacturing", "manufacturingPendingDuringInteraction", "repeatedViewsReuseManufacturing",
    "processEditInFinishedLazy", "editedManufacturingRegenerated", "processSwitchLazy", "unsupportedFinishedOnly",
    "finishedViewStopsRemainingBlanks", "inFlightFinishedSharedAcrossL",
    "snapshotSharedWhilePending", "pendingSnapshotAppliedToLatestSelection", "sharedSnapshotDoesNotRepeatApplyOrFit",
    "supportedSelectionKeepsCurrentProduct", "unsupportedSelectionKeepsProduct", "exampleRequiresExplicitAction", "sceneChangesAreIndependent",
    "templateApplicabilityReceivesCurrentProduct",
    "changedSizeHidesOldCache", "changedSizeRecomputed", "returnedSceneGeometryCached"])
    assert.equal(result[key], true, `${key}: ${JSON.stringify(result)}`);
  assert.deepEqual(result.inputs, result.cornerExpectedLabels);
  console.log("Finished product input Edge regression passed: actual three-way corner descriptor/example/poses, five scene groups, lazy manufacturing, shared finished geometry, focus, selection, nested scroll and retained nodes.");
  console.log(JSON.stringify({ initial: result.initialCallCounts, sharedL: result.sharedLCallCounts,
    total: result.callCounts, snapshotRace: result.snapshotRaceCounts,
    interaction: { retained: result.retained, scroll: result.expectedScroll, conditions: result.conditionChecks,
      conditionalScroll: result.conditionalScroll, later: result.laterInteractionPreserved },
    races: { canceledBlanks: result.finishedViewStopsRemainingBlanks, sharedInFlight: result.inFlightFinishedSharedAcrossL,
      latestSnapshot: result.pendingSnapshotAppliedToLatestSelection },
    cache: { templates: result.templatesReuseManufacturing, invalidations: result.cacheInvalidations,
      inFlight: result.manufacturingInFlightShared, manufacturingOnly: result.manufacturingPlanOnly },
    corner: { scene: result.cornerScene, matrices: result.cornerMatrices,
      interaction: result.cornerInteraction, scroll: result.cornerScroll, resize: result.cornerResize,
      resizeScroll: result.cornerResizeScroll, cached: result.cornerFinishedCached } }));
} finally { await browser.close(); }
