import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
import { assemblyPresentationShape, assemblyShapeOrder, normalizeAssemblyCatalogue } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
// Exercise the deployed catalogue and UI too: source-only checks cannot catch
// an application that still loads an older copy from its binary directory.
const appRoot = resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT || source, "apps/tube-designer");
const assemblyRoot = resolve(appRoot, "templates/assembly");
const templates = readdirSync(assemblyRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory())
  .filter((entry) => {
    try { readFileSync(resolve(assemblyRoot, entry.name, "assembly.json")); return true; } catch { return false; }
  })
  .map((entry) => JSON.parse(readFileSync(resolve(assemblyRoot, entry.name, "assembly.json"), "utf8")));
const bendTemplate = templates.find((item) => item.id === "segmented-bend");
assert.ok(bendTemplate, "分段开槽折弯必须是独立装配模板");
for (const process of bendTemplate.partProcesses) {
  if (process.resource?.id) process.resource.descriptor = JSON.parse(readFileSync(
    resolve(appRoot, "templates/mold", process.resource.id, "tool.json"), "utf8"));
}
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-library.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    const appPrefix = "/src/apps/tube-designer/";
    const served = pathname.startsWith(appPrefix) ? resolve(appRoot, pathname.slice(appPrefix.length)) : file;
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(served, "utf8") });
  });
  await page.goto("http://assembly-library.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:820px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative;min-width:0;background:#13252d}.cam-viewport canvas{width:100%;height:100%}.tube-connection-library-editor-body{height:140px;overflow:auto}.tube-connection-library-list{height:40px!important;min-height:0!important;flex:none!important;overflow:auto}.tube-connection-library-processes .tube-connection-library-parameter-grid{max-height:70px;overflow:auto}` });
  const result = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const productConnections = await import("/src/apps/tube-designer/webpage/productAssemblyConnections.mjs");
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "segmented-bend", workMode: "example", workModeUserSelected: true, parameterDrafts: {} },
      scene: { tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" }, product: {} } } };
    const planResolvers = [];
    const productResolvers = [];
    const catalogueResolvers = [];
    const calls = [];
    let resolveFirstBlank = null;
    const snapshots = [];
    const viewportRecords = [];
    const finishedBounds = { min: [-100, -15, -10], max: [30, 15, 145] };
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const plan = (payload) => ({
      processInput: structuredClone(payload.processInput), parameters: structuredClone(payload.parameters),
      schema: "icax.assembly-preview-plan", templateId: payload.templateId,
      sceneParameters: structuredClone(payload.processInput.geometry),
      resolvedWorkflow: {
        realization: "integrated",
        blankParts: [{ id: "bentBlank", label: "连续折弯下料件", participantRoles: ["segmentA", "segmentB"], sourceRole: "segmentA" }],
        partOperations: [{ id: "node-slot", blankId: "bentBlank", participantRoles: ["segmentA", "segmentB"], resourceRef: { id: "segmented-bend", version: "1.0.0" }, parameters: {}, placement: {} }],
        assemblySteps: [{ id: "prepare", label: "准备连续母材" }, { id: "bend", label: "形成目标角度" }],
        bom: [{ id: "sample", label: "示例辅料", quantity: 1, unit: "件" }],
        checks: [{ id: "geometry", status: "pass", message: "样件尺寸有效" }], parameterEffects: {},
      },
      designParts: Object.entries(payload.processInput.parts).map(([role, part]) => ({
        id: `design-${role}`, role, label: role, request: {
          profileRef: structuredClone(part.profileRef), parameters: structuredClone(part.parameters),
          length: part.length, features: [], ends: {},
        }, matrix: structuredClone(part.matrix), compareMatrix: identity })),
      manufacturingParts: [{ id: "manufacturing-blank", sourceRole: "segmentA", participantRoles: ["segmentA", "segmentB"], label: "blank", request: { length: 200 + payload.parameters.bendRadius, features: [], ends: {} }, matrix: identity, compareMatrix: identity }],
    });
    const context = { sceneProxy: { resources: {}, invoke(method, payload) {
      calls.push({ method, payload });
      if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return Promise.resolve({ schema: "icax.assembly-applicability", schemaVersion: 1, templateId: payload.templateId, applicable: true, reason: "" });
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview" && payload.finishedOnly) return Promise.resolve({
        schema: "icax.finished-product-preview", schemaVersion: 1, finishedProduct: structuredClone(payload.finishedProduct),
        layoutShape: payload.finishedProduct.shapeId, sceneParameters: payload.finishedProduct.parameters,
        designParts: Object.entries(payload.finishedProduct.spans).map(([id, span]) => ({ id: `design-${id}`, role: id, label: id,
          request: { ...structuredClone(span), features: [], ends: {} }, matrix: identity, compareMatrix: identity })),
      });
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
        return new Promise((resolve) => planResolvers.push(() => resolve(plan(payload))));
      }
      if (method === "TubeDesigner.GetProductAssemblyConnections") {
        return new Promise((resolve) => productResolvers.push(() => resolve({
          productEntityId: payload.productEntityId, generationRunId: payload.generationRunId,
          source: "committed-product-model", connections: [{
            key: "joint-1", kind: "端部连接", participants: [{ name: "构件 A" }, { name: "构件 B" }],
          }],
        })));
      }
      if (method === "TubeDesigner.GetAssemblyTemplates") {
        return new Promise((resolve) => catalogueResolvers.push(() => resolve({ assemblies: templates,
          errors: ["invalid-assembly: missing assembly.json"] })));
      }
      if (method === "TubeDesigner.PreviewAssemblyManufacturingPart") {
        const index = calls.filter((item) => item.method.endsWith("PreviewAssemblyManufacturingPart")).length;
        const result = { previewComputed: true, resultValid: true, solidCount: 1, baseGeometry: { url: `memory://base-${index}`, version: 1 },
          geometry: { url: `memory://result-${index}`, version: 1 } };
        if (index === 1) return new Promise((resolve) => { resolveFirstBlank = () => resolve(result); });
        return Promise.resolve(result);
      }
      throw new Error(`unexpected method ${method}`);
    } } };
    view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {} };
    const viewportFactory = (options) => {
      const index = viewportRecords.length;
      const root = document.createElement("div");
      root.className = "fake-three-viewport";
      let camera = { side: index, revision: 0 };
      const record = { index, options, root, snapshots: [], annotations: [], views: [], visible: [], fits: 0 };
      const viewport = {
        root,
        mount(host) { host.replaceChildren(root); return viewport; },
        async applyViewSnapshot(snapshot) { snapshots.push({ index, snapshot }); record.snapshots.push(snapshot); return { applied: true, entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] }; },
        setDimensionAnnotations(annotations) { record.annotations.push(structuredClone(annotations)); },
        getVisibleBounds() { return finishedBounds; },
        setVisibleEntityIds(ids) { record.visible = [...ids]; }, setStandardView(name) { record.views.push(name); camera = { ...camera, view: name }; return true; },
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
    const hasParallelBendCards = mount.querySelector(".cam-context-pane").textContent.includes("冷折弯")
      && mount.querySelector(".cam-context-pane").textContent.includes("分段开槽折弯");
    const canvas = mount.querySelector("canvas");
    const sceneRoot = mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild;
    const cube = mount.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]");
    const input = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    const rightScroll = mount.querySelector(".tube-connection-library-editor-body");
    const leftScroll = mount.querySelector(".tube-connection-library-list");
    const pendingPlaceholderAbsent = !mount.querySelector("[data-tube-assembly-workflow]")
      && !mount.textContent.includes("等待计算当前工艺结果");
    input.focus({ preventScroll: true });
    input.value = "55";
    leftScroll.scrollTop = 95;
    rightScroll.scrollTop = 180;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", input, { renderProject: render });
    await Promise.resolve();
    rightScroll.scrollTop = 220;
    for (let tick = 0; tick < 30 && !view.tubeDesignerAssemblyLibrary.preview?.finishedShape; tick += 1) await new Promise((resolve) => setTimeout(resolve, 0));
    const finishedDoesNotManufacture = !calls.some((call) => call.method === "TubeDesigner.PreviewAssemblyManufacturingPart");
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view", { dataset: { tubeAssemblyView: "exploded" } }, { renderProject: render });
    planResolvers.splice(0).forEach((resolve) => resolve());
    for (let index = 0; index < 30 && (!resolveFirstBlank || !view.tubeDesignerAssemblyLibrary.preview?.finishedShape
        || !snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url.startsWith("icax-assembly-finished://")); index += 1)
      { await new Promise((resolve) => setTimeout(resolve, 0)); planResolvers.splice(0).forEach((resolve) => resolve()); }
    const earlyBlankButton = mount.querySelector('[data-tube-assembly-view="exploded"]');
    const earlyWait = mount.querySelector(".tube-assembly-preview-wait");
    const earlyBlankDebug = { nativePending: !!resolveFirstBlank,
      blankCount: view.tubeDesignerAssemblyLibrary.preview?.manufacturingParts.length,
      phase: view.tubeDesignerAssemblyLibrary.previewRequest?.progress?.phase,
      disabled: earlyBlankButton?.disabled, waitClass: earlyWait?.className,
      blur: earlyWait && getComputedStyle(earlyWait).backdropFilter,
      focused: document.activeElement === input };
    const finishedWhileBlankPending = !!resolveFirstBlank
      && view.tubeDesignerAssemblyLibrary.preview?.manufacturingParts.length === 0
      && view.tubeDesignerAssemblyLibrary.previewRequest?.progress?.phase.includes("下料件")
      && earlyBlankButton?.disabled === true
      && earlyWait?.getAttribute("role") === "status"
      && document.activeElement === input && rightScroll.scrollTop > 0
      && canvas === mount.querySelector("canvas")
      && sceneRoot === mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild;
    resolveFirstBlank?.();
    for (let index = 0; index < 30 && view.tubeDesignerAssemblyLibrary.previewRequest; index += 1) {
      await new Promise((resolve) => setTimeout(resolve, 0));
      if (planResolvers.length) planResolvers.splice(0).forEach((resolve) => resolve());
    }
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view", { dataset: { tubeAssemblyView: "finished" } }, { renderProject: render });
    const updated = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    const redundantWorkflowAbsent = !mount.querySelector("[data-tube-assembly-workflow]")
      && !mount.textContent.includes("工艺明细");
    const search = mount.querySelector(".tube-connection-library-search input");
    search.value = "端";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-search", search, { renderProject: render });
    search.focus({ preventScroll: true });
    search.setSelectionRange(1, 1);
    const moreDetails = [...mount.querySelectorAll("details.tube-connection-library-parameter-section")]
      .find((details) => details.querySelector("summary")?.textContent.includes("更多参数"));
    const editableProcessFields = !!moreDetails?.querySelector('[data-tube-part-process-parameter="spacingModel"]')
      && !!moreDetails?.querySelector('[data-tube-part-process-parameter="bendCompensation"]')
      && !mount.querySelector(".tube-connection-library-process-section");
    moreDetails.open = true;
    const nestedScroll = moreDetails.querySelector(".tube-connection-library-parameter-grid");
    nestedScroll.scrollTop = 20;
    rightScroll.scrollTop = 240;
    leftScroll.scrollTop = 110;
    input.value = "60";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", input, { renderProject: render });
    await Promise.resolve();
    rightScroll.scrollTop = 260;
    leftScroll.scrollTop = 125;
    nestedScroll.scrollTop = 25;
    planResolvers.splice(0).forEach((resolve) => resolve());
    for (let index = 0; index < 30 && view.tubeDesignerAssemblyLibrary.previewRequest; index += 1) {
      await new Promise((resolve) => setTimeout(resolve, 0));
      if (planResolvers.length) planResolvers.splice(0).forEach((resolve) => resolve());
    }
    const searchFocused = document.activeElement === search;
    const searchSelection = [search.selectionStart, search.selectionEnd];
    const nestedAfter = moreDetails.querySelector(".tube-connection-library-parameter-grid");
    const nestedStable = nestedAfter === nestedScroll && nestedAfter.scrollTop > 0;
    const nestedDebug = { same: nestedAfter === nestedScroll, top: nestedAfter.scrollTop,
      height: nestedAfter.clientHeight, contentHeight: nestedAfter.scrollHeight, open: moreDetails.open };
    const leftScrollAfter = leftScroll.scrollTop;
    const rightScrollAfter = rightScroll.scrollTop;
    const finishedShapeReady = view.tubeDesignerAssemblyLibrary.preview.finishedShape?.mesh?.metadata?.previewKind === "finished-shape"
      && mount.querySelector(".tube-assembly-preview-pane.scene")?.getAttribute("aria-label")?.includes("成品外形与尺寸")
      && !mount.querySelector(".tube-assembly-view-only-blank")
      && !!mount.querySelector('[data-tube-assembly-view="finished"]');
    const productRequest = productConnections.ensureProductAssemblyConnections(context, view, { renderProject: render });
    leftScroll.scrollTop = 135;
    rightScroll.scrollTop = 280;
    nestedScroll.scrollTop = 28;
    productResolvers.splice(0).forEach((resolve) => resolve());
    await productRequest;
    const productResponseStable = document.activeElement === search && search.selectionStart === 1
      && search.selectionEnd === 1 && canvas === mount.querySelector("canvas")
      && sceneRoot === mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild
      && leftScroll.scrollTop > 0 && rightScroll.scrollTop > 0
      && nestedScroll === moreDetails.querySelector(".tube-connection-library-parameter-grid") && nestedScroll.scrollTop > 0;
    const exampleHidesProductConnections = !mount.querySelector(".tube-assembly-product-connections");
    moreDetails.open = false;
    const finishedSnapshot = snapshots.at(-1)?.snapshot;
    const finishedAnnotations = viewportRecords[0].annotations.at(-1);
    const explode = mount.querySelector('[data-tube-assembly-view="exploded"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view", explode, { renderProject: render });
    for (let index = 0; index < 10 && snapshots.at(-1)?.snapshot === finishedSnapshot; index += 1) {
      await new Promise((resolve) => setTimeout(resolve, 0));
    }
    for (let tick = 0; tick < 60 && !view.tubeDesignerAssemblyLibrary.preview?.processReady; tick += 1) { await new Promise((resolve) => setTimeout(resolve, 0)); planResolvers.splice(0).forEach((resolve) => resolve()); }
    const explodedSnapshot = snapshots.at(-1)?.snapshot;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-toggle-diagram", {}, { renderProject: render });
    const processAnnotationAbsent = !mount.querySelector('[data-assembly-annotation-key="bendRadius"]');
    const annotation = mount.querySelector('[data-assembly-annotation-key="angle"]');
    annotation?.dispatchEvent(new MouseEvent("click", { bubbles: true }));
    const leftCards = mount.querySelector(".tube-connection-library-cards");
    const diagramFocused = !!annotation && document.activeElement === mount.querySelector('[data-finished-parameter="angle"]');
    if (!view.tubeDesignerAssemblyLibrary.preview.manufacturingParts.length)
      throw new Error(`missing requested blank: ${view.tubeDesignerAssemblyLibrary.previewError}; ${JSON.stringify(calls.map((call) => [call.method, call.payload.finishedOnly, call.payload.templateId]))}`);
    const nativeBlankUrl = view.tubeDesignerAssemblyLibrary.preview.manufacturingParts[0].response.geometry.url;
    const formedUrl = "memory://formed-single-blank";
    view.tubeDesignerAssemblyLibrary.preview.formedPreview = {
      rows: [{ entityId: "assembly-formed:continuousBlank", roles: ["segmentA", "segmentB"], data: {
        geometry: { url: formedUrl, version: 1 }, geometryKind: 1, renderClass: 1,
        visible: true, selectable: false,
      } }], resources: new Map([[formedUrl, new ArrayBuffer(8)]]),
      mesh: { metadata: { previewKind: "target-shape", bendRadius: 999 } },
      annotations: { kind: "segmented-bend-target", values: { bendRadius: 999 } },
    };
    view.tubeDesignerAssemblyLibrary.exploded = false;
    search.focus({ preventScroll: true });
    search.setSelectionRange(1, 1);
    leftScroll.scrollTop = 125;
    rightScroll.scrollTop = 260;
    nestedScroll.scrollTop = 25;
    render();
    const finishedUrl = view.tubeDesignerAssemblyLibrary.preview.finishedShape.rows[0].data.geometry.url;
    for (let index = 0; index < 20 && (snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url !== finishedUrl
        || !viewportRecords[0].annotations.at(-1)?.some(({ id }) => id === "finished-angle")); index += 1)
      await new Promise((resolve) => setTimeout(resolve, 0));
    const formedScene = snapshots.at(-1)?.snapshot;
    const formedAnnotations = viewportRecords[0].annotations.at(-1);
    const formedPane = mount.querySelector(".tube-assembly-preview-pane.scene");
    const formedHeader = formedPane.querySelector("header");
    const formedDescription = formedPane.getAttribute("aria-label");
    const formedSwitchRect = formedPane.querySelector(".tube-assembly-view-switch").getBoundingClientRect();
    const formedHeaderRect = formedHeader.getBoundingClientRect();
    const compactHeader = !formedHeader.querySelector(".tube-assembly-preview-caption, .tube-assembly-preview-title")
      && !formedHeader.textContent.includes("按槽位逐段折起")
      && formedHeader.title.includes("成品外形与尺寸")
      && !formedHeader.querySelector('[data-tube-assembly-camera]');
    const switchCentered = Math.abs((formedSwitchRect.left + formedSwitchRect.right - formedHeaderRect.left - formedHeaderRect.right) / 2) < 1;
    const formedButton = mount.querySelector('[data-tube-assembly-view="finished"]');
    const blankButton = mount.querySelector('[data-tube-assembly-view="exploded"]');
    const formedControlsAvailable = !!formedButton && !!blankButton;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view", blankButton, { renderProject: render });
    for (let index = 0; index < 10 && snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url !== nativeBlankUrl; index += 1)
      await new Promise((resolve) => setTimeout(resolve, 0));
    const blankScene = snapshots.at(-1)?.snapshot;
    const formedSwitch = {
      formedControlsAvailable,
      formedDescription, compactHeader, switchCentered,
      formedUrls: formedScene?.rows.map((row) => row.data.geometry.url),
      formedAnnotations,
      blankUrls: blankScene?.rows.map((row) => row.data.geometry.url),
      blankDescription: mount.querySelector(".tube-assembly-preview-pane.scene")?.getAttribute("aria-label"),
      canvasStable: canvas === mount.querySelector("canvas"),
      cubeStable: !!cube && cube === mount.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]"),
      inputStable: input === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
      sceneRootStable: sceneRoot === mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild,
      focused: document.activeElement === search && search.selectionStart === 1 && search.selectionEnd === 1,
      leftScroll: leftScroll.scrollTop, rightScroll: rightScroll.scrollTop, nestedScroll: nestedScroll.scrollTop,
    };
    view.tubeDesignerAssemblyLibrary.exploded = false;
    render();
    window.__assemblyLayoutReview = { view, render, context, library, planResolvers, catalogueResolvers,
      viewportRecords };
    return {
      canvasStable: canvas === mount.querySelector("canvas"), inputStable: input === updated,
      cubeStable: !!cube && cube === mount.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]"),
      sceneRootStable: sceneRoot === mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild,
      focused: searchFocused, searchSelection, scroll: rightScrollAfter, leftScroll: leftScrollAfter,
      nestedStable, nestedDebug, pendingPlaceholderAbsent, redundantWorkflowAbsent, editableProcessFields,
      value: view.tubeDesignerAssemblyLibrary.parameterDrafts["segmented-bend"].bendRadius,
      hasParallelBendCards,
      normalHudAbsent: !mount.querySelector(".tube-connection-library-hud"),
      diagramFocused, processAnnotationAbsent,
      moreCollapsed: moreDetails?.tagName === "DETAILS" && !moreDetails.hasAttribute("open"),
      relationAbsent: !mount.querySelector(".tube-connection-library-relation"),
      finishedShapeReady, finishedDoesNotManufacture, finishedWhileBlankPending, earlyBlankDebug, formedSwitch, finishedAnnotations,
      productResponseStable, exampleHidesProductConnections,
      conciseHeading: mount.querySelector(".tube-connection-library-editor-heading")?.textContent.includes("连续母材")
        && !mount.querySelector(".tube-assembly-choice-summary")
        && !mount.querySelector(".tube-assembly-preview-notes"),
      localProcessProtocol: calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability"
        || call.method === "TubeDesigner.ResolveAssemblyTemplatePreview" && !call.payload.finishedOnly)
        .every(({ payload }) => payload.processInput?.schema === "icax.assembly-process-input"
          && payload.processInput.schemaVersion === 1 && !Object.hasOwn(payload, "finishedProduct")
          && !Object.hasOwn(payload.processInput, "shapeId")
          && Object.keys(payload.processInput.parts).length === 2
          && Object.values(payload.processInput.parts).every((part) => part.length > 0
            && part.profileRef && part.parameters && part.matrix.length === 16)),
      leftColumns: getComputedStyle(leftCards).gridTemplateColumns.split(" ").filter(Boolean).length,
      finishedRows: finishedSnapshot?.rows.length,
      explodedRows: explodedSnapshot?.rows.length,
      finishedGeometryUrls: finishedSnapshot?.rows.map((row) => row.data.geometry.url),
      explodedGeometryUrls: explodedSnapshot?.rows.map((row) => row.data.geometry.url),
      sceneViews: viewportRecords[0].views,
      sceneVisible: viewportRecords[0].visible,
      projectionDefault: viewportRecords[0].options.projectionMode,
      exploded: view.tubeDesignerAssemblyLibrary.exploded,
      viewportCount: viewportRecords.length,
      resolveCalls: calls.filter((item) => item.method.endsWith("ResolveAssemblyTemplatePreview")).length,
      previewCalls: calls.filter((item) => item.method.endsWith("PreviewAssemblyManufacturingPart")).length,
    };
  }, templates);
  if (process.env.ICAX_ASSEMBLY_SCREENSHOT) {
    const screenshotStyle = await page.addStyleTag({ content: ".tube-connection-library-editor-body{height:auto!important}.tube-connection-library-list{height:auto!important}.tube-connection-library-processes .tube-connection-library-parameter-grid{max-height:none!important}" });
    await page.evaluate(() => {
      const review = window.__assemblyLayoutReview;
      review.view.tubeDesignerAssemblyLibrary.exploded = false;
      review.render();
      document.querySelector(".tube-assembly-product-connections details")?.setAttribute("open", "");
      document.querySelector("[data-tube-assembly-diagram-dock]")?.setAttribute("hidden", "");
      document.querySelector(".tube-connection-library-editor-body").scrollTop = 0;
      document.querySelector(".tube-connection-library-list").scrollTop = 0;
    });
    await page.screenshot({ path: `${process.env.ICAX_ASSEMBLY_SCREENSHOT}-top.png` });
    await page.evaluate(() => { document.querySelector(".tube-connection-library-editor-body").scrollTop = 320; });
    await page.screenshot({ path: `${process.env.ICAX_ASSEMBLY_SCREENSHOT}-result.png` });
    await page.evaluate(() => {
      const review = window.__assemblyLayoutReview;
      const workflow = review.view.tubeDesignerAssemblyLibrary.preview.plan.resolvedWorkflow;
      workflow.validationStatus = "requires-definition";
      workflow.checks.push({ id: "required-process", status: "requires-definition", blocking: true,
        detail: "示例缺少必要的加工定义" });
      review.render();
      const diagram = document.querySelector("[data-tube-assembly-diagram-dock]");
      if (diagram) diagram.style.display = "none";
      document.querySelector(".tube-connection-library-editor-body").scrollTop = 290;
    });
    await page.screenshot({ path: `${process.env.ICAX_ASSEMBLY_SCREENSHOT}-blocked.png` });
    await screenshotStyle.evaluate((element) => element.remove());
  }
  const refreshResult = await page.evaluate(async () => {
    const { view, render, context, library, planResolvers, catalogueResolvers, viewportRecords } = window.__assemblyLayoutReview;
    const mount = document.querySelector("main");
    const canvas = mount.querySelector(".cam-viewport > canvas");
    const sceneRoot = mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild;
    const input = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    const search = mount.querySelector(".tube-connection-library-search input");
    const left = mount.querySelector(".tube-connection-library-list");
    const right = mount.querySelector(".tube-connection-library-editor-body");
    const details = [...mount.querySelectorAll("details.tube-connection-library-parameter-section")]
      .find((node) => node.querySelector("summary")?.textContent.includes("更多参数"));
    details.open = true;
    const nested = details.querySelector(".tube-connection-library-parameter-grid");
    let sceneClicks = 0;
    sceneRoot.addEventListener("click", () => { sceneClicks += 1; });
    sceneRoot.dispatchEvent(new MouseEvent("click"));
    search.focus({ preventScroll: true });
    search.setSelectionRange(0, 1);
    left.scrollTop = 90;
    right.scrollTop = 170;
    nested.scrollTop = 20;
    const refresh = mount.querySelector('[data-cam-action="tube-designer-assembly-retry"]');
    const pendingCatalogue = library.handleAssemblyLibraryAction(context, view,
      "tube-designer-assembly-retry", refresh, { renderProject: render });
    await Promise.resolve();
    if (catalogueResolvers.length !== 1) throw new Error("刷新按钮没有重新读取模板目录");
    left.scrollTop = 110;
    right.scrollTop = 190;
    nested.scrollTop = 25;
    catalogueResolvers.splice(0).forEach((complete) => complete());
    await pendingCatalogue;
    for (let index = 0; index < 40 && view.tubeDesignerAssemblyLibrary.previewRequest; index += 1) {
      left.scrollTop = 120;
      right.scrollTop = 205;
      nested.scrollTop = 30;
      await new Promise((resolve) => setTimeout(resolve, 0));
      planResolvers.splice(0).forEach((complete) => complete());
    }
    sceneRoot.dispatchEvent(new MouseEvent("click"));
    return {
      catalogueReady: view.tubeDesignerAssemblyLibrary.catalogueStatus === "ready",
      warningShown: mount.querySelector(".tube-assembly-catalogue-warnings summary")?.textContent === "1 个模板未载入"
        && mount.querySelector(".tube-assembly-catalogue-warnings li")?.textContent === "invalid-assembly: missing assembly.json",
      previewReady: !!view.tubeDesignerAssemblyLibrary.preview,
      canvasStable: canvas === mount.querySelector(".cam-viewport > canvas"),
      sceneRootStable: sceneRoot === mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild,
      inputStable: input === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
      searchStable: search === mount.querySelector(".tube-connection-library-search input"),
      focusAndSelection: document.activeElement === search && search.selectionStart === 0 && search.selectionEnd === 1,
      leftStable: left === mount.querySelector(".tube-connection-library-list") && left.scrollTop > 0,
      rightStable: right === mount.querySelector(".tube-connection-library-editor-body") && right.scrollTop > 0,
      nestedStable: nested === details.querySelector(".tube-connection-library-parameter-grid") && nested.scrollTop > 0,
      sceneClicks, viewportCount: viewportRecords.length,
    };
  });
  const shapeGroupResult = await page.evaluate(async () => {
    const { view, render, context, library } = window.__assemblyLayoutReview;
    view.tubeDesignerAssemblyLibrary.search = "";
    render();
    const mount = document.querySelector("main");
    const canvas = mount.querySelector(".cam-viewport > canvas");
    const input = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    const search = mount.querySelector(".tube-connection-library-search input");
    const left = mount.querySelector(".tube-connection-library-list");
    const right = mount.querySelector(".tube-connection-library-editor-body");
    const headings = () => [...mount.querySelectorAll(".tube-connection-library-group-heading")]
      .map((button) => [button.dataset.tubeAssemblyShape, button.querySelector("span")?.textContent.trim().slice(2),
        Number(button.querySelector("small")?.textContent)]);
    const initial = headings();
    search.focus({ preventScroll: true });
    search.setSelectionRange(0, 0);
    left.scrollTop = 90;
    right.scrollTop = 180;
    const selectedId = view.tubeDesignerAssemblyLibrary.selectedId;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-toggle-shape",
      mount.querySelector('[data-tube-assembly-shape="t"]'), { renderProject: render });
    const tGroup = mount.querySelector('[data-tube-assembly-shape="t"]').closest(".tube-connection-library-group");
    const collapsed = tGroup.querySelector(".tube-connection-library-cards").hidden
      && tGroup.querySelector(".tube-connection-library-group-heading").getAttribute("aria-expanded") === "false"
      && canvas === mount.querySelector(".cam-viewport > canvas")
      && input === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]')
      && search === mount.querySelector(".tube-connection-library-search input")
      && left === mount.querySelector(".tube-connection-library-list")
      && right === mount.querySelector(".tube-connection-library-editor-body")
      && document.activeElement === search && search.selectionStart === 0 && search.selectionEnd === 0
      && left.scrollTop > 0 && right.scrollTop > 0
      && view.tubeDesignerAssemblyLibrary.selectedId === selectedId;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-toggle-shape",
      mount.querySelector('[data-tube-assembly-shape="t"]'), { renderProject: render });
    const expanded = !mount.querySelector('[data-tube-assembly-shape="t"]').closest(".tube-connection-library-group")
      .querySelector(".tube-connection-library-cards").hidden;
    search.value = "T形";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-search", search,
      { renderProject: render });
    return { initial, collapsed, expanded, filtered: headings(), selectedIdStable: view.tubeDesignerAssemblyLibrary.selectedId === selectedId };
  });
  if (process.env.ICAX_ASSEMBLY_SHAPE_SCREENSHOT) {
    await page.evaluate(() => {
      const review = window.__assemblyLayoutReview;
      review.view.tubeDesignerAssemblyLibrary.search = "";
      review.view.tubeDesignerAssemblyLibrary.collapsed = ["l", "t", "cross", "straight", "parallel"];
      review.view.tubeDesignerAssemblyLibrary.showDiagram = false;
      review.view.tubeDesignerAssemblyLibrary.catalogueWarnings = [];
      review.render();
      document.querySelector(".tube-connection-library-list").scrollTop = 0;
    });
    const shapeScreenshotStyle = await page.addStyleTag({ content: ".tube-connection-library-list{height:auto!important;overflow:visible!important}" });
    await page.locator(".cam-context-pane").screenshot({ path: process.env.ICAX_ASSEMBLY_SHAPE_SCREENSHOT });
    await shapeScreenshotStyle.evaluate((element) => element.remove());
  }
  const straightFinishedResult = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const template = templates.find((item) => item.id === "insert-sleeve");
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const positioned = (x) => [1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const designParts = [
      { id: "insert", role: "insertPart", request: { length: 280 }, matrix: positioned(180),
        response: { baseGeometry: { url: "memory://insert" } } },
      { id: "receiver", role: "receivePart", request: { length: 360 }, matrix: positioned(-180),
        response: { baseGeometry: { url: "memory://receiver" } } },
    ];
    const manufacturingParts = designParts.map((part) => ({
      id: `blank-${part.id}`, sourceRole: part.role, participantRoles: [part.role],
      request: part.request, matrix: identity,
      response: { geometry: { url: `memory://processed-${part.id}` } },
    }));
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example",
        workModeUserSelected: true, parameterDrafts: {} } };
    const state = view.tubeDesignerAssemblyLibrary;
    state.preview = { key: library.assemblyPreviewKey(view), layoutShape: "straight",
      plan: { templateId: template.id, designParts, manufacturingParts,
        resolvedWorkflow: { realization: "separate" } }, designParts, manufacturingParts };
    const bounds = { min: [-180, -20, -20], max: [460, 20, 20] };
    const annotations = [];
    let boundsReads = 0, sceneRows = [];
    const context = { sceneProxy: { resources: { get() { throw Error("fake viewport does not fetch geometry"); } },
 } };
    const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
    document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane"></aside><div class="cam-viewport">${overlay}</div><aside class="cam-info-pane"></aside></div></main>`;
    library.attachAssemblyLibraryViewports(context, view, document.querySelector("main"), {
      viewportFactory() {
        const root = document.createElement("div");
        const viewport = { root,
          mount(host) { host.replaceChildren(root); return viewport; },
          async applyViewSnapshot(snapshot) { sceneRows = snapshot.rows;
            return { applied: true, entityIds: sceneRows.map((row) => row.entityId), missingGeometryEntityIds: [] }; },
          getVisibleBounds() { boundsReads += 1; return bounds; },
          setDimensionAnnotations(items) { annotations.push(items); },
          setVisibleEntityIds() {}, setStandardView() { return true; },
          fitViewToViewport() { return true; }, dispose() {},
        };
        return viewport;
      },
    });
    for (let index = 0; index < 20 && !annotations.length; index += 1)
      await new Promise((resolve) => setTimeout(resolve, 0));
    library.disposeAssemblyLibraryViewports(view);
    return { boundsReads, rows: sceneRows.length,
      placement: sceneRows.map((row) => row.data.localToWorldMatrix[3]),
      geometryUrls: sceneRows.map((row) => row.data.geometry.url),
      designLengths: designParts.map((part) => part.request.length), annotations: annotations.at(-1) };
  }, templates);
  const processBlindProductPanels = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const products = await import("/src/apps/tube-designer/webpage/finishedProductModel.mjs");
    return templates.filter((template) => !template.catalogueHidden).map((template) => {
      const id = template.id;
      const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
        tubeDesignerAssemblyLibrary: { selectedId: id, workMode: "example", workModeUserSelected: true } };
      document.body.innerHTML = library.renderAssemblyLibraryRightPane({}, view);
      const product = document.querySelector("[data-tube-assembly-scene-parameters]");
      const names = [...product.querySelectorAll(".tube-assembly-scene-member summary strong")]
        .map((node) => node.textContent.trim());
      const productKeys = [...product.querySelectorAll("[data-finished-parameter]")]
        .map((node) => node.dataset.finishedParameter);
      const processKeys = [...document.querySelectorAll("[data-tube-assembly-parameter]")]
        .filter((node) => !product.contains(node)).map((node) => node.dataset.tubeAssemblyParameter);
      const finished = products.finishedProductInput(view);
      const definition = products.finishedProductShape(finished.shapeId);
      return { id, names, productKeys, processKeys, memberCount: definition.spans.length,
        sceneKeys: definition.parameters.map((item) => item.key) };
    });
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
    const result = { count: canvases.length };
    library.disposeAssemblyLibraryViewports(view);
    return result;
  }, templates);
  const productModeResult = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const view = { tubeDesignerAssemblyTemplates: templates,
      scene: { tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" } } },
      tubeDesignerAssemblyLibrary: { selectedId: "bend", workMode: "example", workModeUserSelected: true } };
    let nativeCalls = 0;
    const context = { sceneProxy: { invoke() { nativeCalls += 1; throw new Error("切换模式不应提交工艺"); } } };
    const render = () => { document.body.innerHTML = library.renderAssemblyLibraryRightPane(context, view); };
    render();
    const productButton = document.querySelector('[data-tube-assembly-mode="product"]');
    const labeledAsMode = productButton?.textContent.trim() === "产品节点"
      && productButton?.getAttribute("data-cam-action") === "tube-designer-assembly-work-mode"
      && productButton?.title.includes("仅切换工作方式");
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-work-mode",
      productButton, { renderProject: render });
    const inProductMode = view.tubeDesignerAssemblyLibrary.workMode === "product"
      && document.querySelector('[data-tube-assembly-mode="product"]')?.getAttribute("aria-pressed") === "true"
      && document.body.textContent.includes("请在左侧选择连接节点")
      && library.renderAssemblyLibraryLeftPane(context, view).includes("当前产品连接")
      && !document.querySelector('[data-cam-action="tube-designer-binding-apply"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-work-mode",
      document.querySelector('[data-tube-assembly-mode="example"]'), { renderProject: render });
    return { labeledAsMode, inProductMode, backToExample: view.tubeDesignerAssemblyLibrary.workMode === "example"
      && document.querySelector('[data-tube-assembly-mode="example"]')?.getAttribute("aria-pressed") === "true",
      nativeCalls };
  }, templates);
  const conditionalResult = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    document.body.replaceChildren();
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "weld-interface" } };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane({}, view);
      const right = library.renderAssemblyLibraryRightPane({}, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay({}, view);
      const mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) {
        throw new Error("条件字段变化没有走局部更新");
      }
    };
    render();
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    const leftScroll = mount.querySelector(".tube-connection-library-list");
    const rightScroll = mount.querySelector(".tube-connection-library-editor-body");
    const gap = mount.querySelector('[data-tube-assembly-parameter="fitGap"]');
    const overlap = mount.querySelector('[data-finished-parameter="overlapLength"]');
    const weldType = mount.querySelector('[data-tube-assembly-parameter="weldType"]');
    gap.focus({ preventScroll: true });
    leftScroll.scrollTop = 80;
    rightScroll.scrollTop = 90;
    weldType.value = "plug";
    await library.handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", weldType, { renderProject: render });
    const diameter = mount.querySelector('[data-tube-assembly-parameter="openingDiameter"]');
    const appearedWithoutReplacement = !!diameter && overlap === mount.querySelector('[data-finished-parameter="overlapLength"]')
      && gap === mount.querySelector('[data-tube-assembly-parameter="fitGap"]')
      && document.activeElement === gap && leftScroll.scrollTop > 0 && rightScroll.scrollTop > 0;
    diameter.focus({ preventScroll: true });
    weldType.value = "lap";
    await library.handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", weldType, { renderProject: render });
    return { appearedWithoutReplacement, removed: !mount.querySelector('[data-tube-assembly-parameter="openingDiameter"]'),
      focusNotMovedToOtherField: document.activeElement !== overlap && document.activeElement !== gap,
      canvasStable: canvas === mount.querySelector("canvas"),
      leftScroll: leftScroll.scrollTop, rightScroll: rightScroll.scrollTop };
  }, templates);
  const conciseWrapResult = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const view = { tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "wrap-a-over-b", parameterDrafts: {} } };
    const render = () => { document.body.innerHTML = library.renderAssemblyLibraryRightPane({}, view); };
    render();
    const initial = !document.querySelector(".tube-connection-library-process-section, .tube-assembly-workflow-review")
      && !document.querySelector('[data-tube-assembly-parameter="pairCount"]');
    view.tubeDesignerAssemblyLibrary.parameterDrafts["wrap-a-over-b"] = { maleFemale: true, pairCount: "four" };
    render();
    return { initial, fourChoiceVisible: document.querySelector('[data-tube-assembly-parameter="pairCount"]')?.value === "four",
      noRedundantCards: !document.querySelector(".tube-connection-library-process-section, .tube-assembly-workflow-review"),
      unnecessaryMoreParameters: [...document.querySelectorAll("details.tube-connection-library-parameter-section")]
        .some((details) => details.querySelector("summary")?.textContent.includes("更多参数")) };
  }, templates);
  const inheritedFieldResult = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const view = { tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "segmented-bend" } };
    const render = () => { document.body.innerHTML = library.renderAssemblyLibraryRightPane({}, view); };
    render();
    const field = document.querySelector('[data-tube-part-process-parameter="spacingModel"]');
    const inMoreParameters = !!field?.closest("details.tube-connection-library-parameter-section")
      && field.closest("details")?.querySelector("summary")?.textContent.includes("更多参数");
    field.value = "tangent";
    await library.handleAssemblyLibraryAction({}, view, "tube-designer-assembly-process-parameter-change",
      field, { renderProject: render });
    return { inMoreParameters, draft: library.assemblyLibraryState(view).processDrafts?.["segmented-bend"]?.["node-slot"]?.["segmented-bend"]?.spacingModel,
      visibleAfterEdit: document.querySelector('[data-tube-part-process-parameter="spacingModel"]')?.value,
      noProcessCard: !document.querySelector(".tube-connection-library-process-section") };
  }, templates);
  const visibleTemplates = normalizeAssemblyCatalogue(templates);
  const connectionTemplates = visibleTemplates.filter((template) => !template.inputContract.supportedInputModes?.includes("stock-operation"));
  assert.deepEqual(shapeGroupResult.initial, [...assemblyShapeOrder(connectionTemplates).map(([shape, label]) =>
    [shape, shape === "straight" ? "共线形" : label,
      connectionTemplates.filter((template) => assemblyPresentationShape(template) === shape).length]),
    ["fold-processing", "折弯加工", visibleTemplates.length-connectionTemplates.length]],
  "左栏应覆盖连接示例与独立的可重复折弯工艺");
  assert.equal(shapeGroupResult.collapsed && shapeGroupResult.expanded && shapeGroupResult.selectedIdStable, true,
    "形态组折叠与展开必须保留选择、焦点、滚动和画布及输入节点");
  assert.deepEqual(shapeGroupResult.filtered, [["t", "T形", 3]], "搜索形态名称应只保留对应工艺组");
  assert.equal(straightFinishedResult.boundsReads, 1, "成品尺寸必须读取当前视口的可见外形范围");
  assert.deepEqual(straightFinishedResult.placement, [180, -180],
    "成品两管端面相接，套接深度不能把它们预先叠在一起");
  assert.deepEqual(straightFinishedResult.geometryUrls, ["memory://insert", "memory://receiver"],
    "浏览器成品场景只显示两根管的产品外形，不显示任何加工后的下料几何");
  assert.deepEqual(straightFinishedResult.designLengths, [280, 360]);
  assert.equal(straightFinishedResult.rows, 2);
  assert.deepEqual(straightFinishedResult.annotations.map(({ id, label }) => [id, label]),
    [["finished-envelope:x", "成品总长 640 mm"]],
    "两根端面相接的管只应标注成品外廓总长");
  for (const panel of processBlindProductPanels) {
    assert.equal(panel.names.length, panel.memberCount, `${panel.id} 的成品参数应列出全部管件`);
    assert.equal(new Set(panel.names).size, panel.memberCount, `${panel.id} 的成品管件应能区分`);
    assert.equal(panel.names.every((name) => !/插入|承接|插舌|插槽|公口|母口|焊件|被连接件|调节件|逻辑/.test(name)), true,
      `${panel.id} 的成品构件不能按装配工艺命名：${panel.names.join("、")}`);
    assert.ok(panel.productKeys.every((key) => panel.sceneKeys.includes(key)), `${panel.id} 的成品区只能展示成品字段`);
    assert.ok(panel.processKeys.every((key) => !panel.sceneKeys.includes(key)), `${panel.id} 的成品字段不能混入工艺区`);
  }
  const sleevePanel = processBlindProductPanels.find((panel) => panel.id === "insert-sleeve");
  const tabSlotPanel = processBlindProductPanels.find((panel) => panel.id === "tab-slot-lock");
  assert.equal(sleevePanel.productKeys.includes("insertDepth"), false,
    "插入深度不能列在成品参数中");
  assert.equal(sleevePanel.processKeys.includes("insertDepth"), true,
    "插入深度属于装配工艺参数");
  assert.equal(tabSlotPanel.productKeys.some((key) => ["nominalWidth", "straightDepth"].includes(key)), false,
    "插舌与插槽加工尺寸不能列在成品参数中");
  assert.equal(["nominalWidth", "straightDepth"].every((key) => tabSlotPanel.processKeys.includes(key)), true,
    "插舌与插槽加工尺寸应由工艺参数控制");
  assert.equal(result.canvasStable, true);
  assert.equal(result.cubeStable, true, "异步预览刷新不得替换装配 ViewCube");
  assert.equal(result.inputStable, true);
  assert.equal(result.sceneRootStable, true);
  assert.equal(result.focused, true);
  assert.deepEqual(result.searchSelection, [1, 1]);
  assert.ok(result.scroll > 0);
  assert.ok(result.leftScroll > 0);
  assert.equal(result.nestedStable, true, `异步预览刷新必须保留更多参数中的嵌套滚动：${JSON.stringify(result.nestedDebug)}`);
  assert.equal(result.value, 60);
  assert.equal(result.hasParallelBendCards, true, "冷折弯与分段开槽折弯须作为并列模板显示");
  assert.equal(result.normalHudAbsent, true, "正常预览不应常驻状态提示");
  assert.equal(result.diagramFocused, true, "场景角度标注应定位到产品参数");
  assert.equal(result.processAnnotationAbsent, true, "成品参数图不得标注加工半径 bendRadius");
  assert.equal(result.moreCollapsed, true, "更多参数默认应折叠");
  assert.equal(result.relationAbsent, true, "右侧不应再显示重复的零件对照块");
  assert.equal(result.pendingPlaceholderAbsent && result.redundantWorkflowAbsent, true,
    "示例右栏不应显示待计算占位或重复的工艺明细卡");
  assert.equal(result.editableProcessFields, true,
    "有可编辑的单件工艺字段时，应并入更多参数且不显示加工设置卡");
  assert.equal(result.finishedShapeReady, true, "L 形示例应直接生成独立于加工配方的成品外形");
  assert.equal(result.finishedDoesNotManufacture, true, "成品视图不能触发下料加工，须由用户显式选择下料件");
  assert.equal(result.localProcessProtocol, true, "适用性与加工接口必须只接收实际局部管材与必要几何量");
  assert.equal(result.finishedWhileBlankPending, true,
    `成品应先显示，显式下料计算保留输入与场景：${JSON.stringify(result.earlyBlankDebug)}`);
  assert.equal(result.formedSwitch.formedControlsAvailable, true, "L 形成品外形应可与下料件切换");
  assert.match(result.formedSwitch.formedDescription, /成品外形与尺寸/);
  assert.equal(result.formedSwitch.compactHeader, true, "顶部不得重复模板标题、长说明或显示第二套相机按钮");
  assert.equal(result.formedSwitch.switchCentered, true, "成品外形／下料件切换应相对于整个视口居中");
  assert.deepEqual(result.formedSwitch.formedUrls, result.finishedGeometryUrls,
    "注入工艺成形预览后，L 形成品仍须显示只由产品参数生成的外形");
  assert.deepEqual(result.formedSwitch.formedAnnotations, result.finishedAnnotations,
    "注入工艺成形预览后，L 形成品标注不得改变");
  assert.deepEqual(result.finishedAnnotations.map(({ id }) => id),
    ["finished-envelope:x", "finished-envelope:z", "finished-angle"],
    "L 形成品只标注完整外廓尺寸和转角");
  assert.match(result.finishedAnnotations[0].label, /成品.*外廓.*130 mm/);
  assert.match(result.finishedAnnotations[1].label, /成品.*外廓.*155 mm/);
  assert.equal(result.finishedAnnotations.some(({ label }) => /逻辑段设定长|下料基准长/.test(label)), false);
  assert.equal(result.finishedAnnotations.some(({ label }) => /bendRadius|弯曲半径/.test(label)), false,
    "加工半径不得进入成品尺寸标注");
  assert.deepEqual(result.formedSwitch.blankUrls, ["memory://result-2"], "下料件必须保留原生加工几何");
  assert.match(result.formedSwitch.blankDescription, /按模板加工的下料件/);
  assert.equal(result.formedSwitch.canvasStable && result.formedSwitch.cubeStable && result.formedSwitch.inputStable && result.formedSwitch.sceneRootStable
    && result.formedSwitch.focused, true, "切换视图不得替换画布或输入框，也不得丢失焦点与选区");
  assert.ok(result.formedSwitch.leftScroll > 0 && result.formedSwitch.rightScroll > 0
    && result.formedSwitch.nestedScroll > 0, "切换视图应保留左右侧栏和嵌套容器滚动位置");
  assert.equal(result.conciseHeading, true, "示例标题应只有简短工艺说明，不重复概述卡或画布参数便签");
  assert.equal(result.productResponseStable, true, "真实产品连接异步返回不得替换画布、焦点、选区或滚动容器");
  assert.equal(result.exampleHidesProductConnections, true, "示例模式不应混入当前产品的节点列表");
  assert.equal(result.leftColumns, 1, "装配工艺必须单列显示，避免名称截断和误读");
  assert.equal(result.finishedRows, 1);
  assert.equal(result.explodedRows, 1);
  assert.match(result.finishedGeometryUrls[0], /^icax-assembly-finished:\/\/preview\/\d+$/,
    "L 形成品外形应使用产品专属网格");
  assert.deepEqual(result.explodedGeometryUrls, ["memory://result-2"],
    "炸开状态必须显示单件工艺处理后的真实下料几何");
  assert.ok(result.sceneViews.includes("iso"), "三维视口默认初始化等轴视角");
  assert.equal(result.sceneVisible.length, 1, "炸开状态应显示当前工艺生成的完整下料结果");
  assert.equal(result.projectionDefault, "perspective");
  assert.equal(result.exploded, false, "注入工艺成形外观后仍应能返回产品成品外形");
  assert.equal(result.viewportCount, 1, "成品与炸开图必须复用同一个三维场景和相机");
  assert.ok(result.resolveCalls >= 2, "参数变化必须使旧请求失效并启动新预览");
  assert.equal(result.previewCalls, 2, "独立 L 成品外形可用时，每次有效计划只应生成下料件");
  assert.deepEqual(realViewportResult, { count: 1 }, "页面只应创建一个 WebGL 视口，成品与炸开图在同一场景切换");
  for (const [key, value] of Object.entries(refreshResult)) {
    if (key === "sceneClicks" || key === "viewportCount") continue;
    assert.equal(value, true, `刷新模板后必须保留 ${key}`);
  }
  assert.equal(refreshResult.sceneClicks, 2, "刷新模板不得替换画布节点或丢失原有监听器");
  assert.equal(refreshResult.viewportCount, 1, "刷新模板应复用原三维视口");
  assert.deepEqual(productModeResult, { labeledAsMode: true, inProductMode: true, backToExample: true, nativeCalls: 0 },
    "产品节点仅切换工作方式，不能在按钮点击时提交加工");
  assert.equal(conditionalResult.appearedWithoutReplacement, true, "条件字段出现时必须保留其他输入节点、焦点与两侧滚动");
  assert.equal(conditionalResult.removed, true, "关闭上级选项后应移除不适用字段");
  assert.equal(conditionalResult.focusNotMovedToOtherField, true, "字段移除时不得把焦点误给其他字段");
  assert.equal(conditionalResult.canvasStable, true);
  assert.ok(conditionalResult.leftScroll > 0 && conditionalResult.rightScroll > 0);
  assert.deepEqual(conciseWrapResult, { initial: true, fourChoiceVisible: true, noRedundantCards: true,
    unnecessaryMoreParameters: false }, "包接工艺条件参数直接显示，成品参数不应制造空的更多参数区");
  assert.deepEqual(inheritedFieldResult, { inMoreParameters: true, draft: "tangent", visibleAfterEdit: "tangent",
    noProcessCard: true }, "继承资源的单件参数并入更多参数后仍须可编辑和持久保存草稿");
  assert.deepEqual(errors, []);
  console.log("Assembly finished/exploded single viewport, live parameters and focus/scroll preservation passed.");
} finally {
  await browser.close();
}
