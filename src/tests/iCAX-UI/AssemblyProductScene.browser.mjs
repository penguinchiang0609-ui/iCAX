import assert from "node:assert/strict";
import { readFileSync, readdirSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
const assemblyRoot = resolve(source, "apps/tube-designer/templates/assembly");
const templates = readdirSync(assemblyRoot).filter((name) => existsSync(resolve(assemblyRoot, name, "assembly.json")))
  .map((name) => JSON.parse(readFileSync(resolve(assemblyRoot, name, "assembly.json"), "utf8")));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-product.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT && pathname.startsWith("/src/apps/tube-designer/") ? resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT, "apps/tube-designer", pathname.slice("/src/apps/tube-designer/".length)) : file, "utf8") });
  });
  await page.goto("http://assembly-product.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:900px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative;min-width:0;background:#13252d}.tube-connection-library-editor-body{height:300px;overflow:auto}.tube-connection-library-list{height:240px;overflow:auto}.tube-assembly-binding-parameters{height:110px;overflow:auto}` });
  const result = await page.evaluate(async (templates) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const product = await import("/src/apps/tube-designer/webpage/assemblyProductScene.mjs");
    const bindings = await import("/src/apps/tube-designer/webpage/productAssemblyBindings.mjs");
    const productConnections = await import("/src/apps/tube-designer/webpage/productAssemblyConnections.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const transform = [0, -1, 0, 128, 1, 0, 0, 56, 0, 0, 1, 32, 0, 0, 0, 1];
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      scene: { tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" }, product: { name: "真实产品" },
        members: [{ entityId: "member-a" }, { entityId: "member-b" }], parts: [
          { entityId: "part-a", name: "已加工横梁", thumbnailGeometryResourceId: "cut-a", thumbnailGeometryResourceVersion: 1 },
          { entityId: "part-b", name: "已加工立杆", thumbnailGeometryResourceId: "cut-b", thumbnailGeometryResourceVersion: 1 },
        ] } } };
    const backgroundCameraCalls = [];
    view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {}, setStandardView(name) { backgroundCameraCalls.push(name); } };
    const lateProduct = { tubeDesignerAssemblyTemplates: templates, scene: { tubeDesigner: {} } };
    const startsAsExample = library.assemblyLibraryState(lateProduct).workMode === "example";
    lateProduct.scene.tubeDesigner = { activeProductId: "later", generationRun: { entityId: "run-later" } };
    const switchesToProduct = library.assemblyLibraryState(lateProduct).workMode === "product";
    library.assemblyLibraryState(view);
    const joint = { key: "joint-1", kind: "weld", label: "横梁与立杆", properties: { topology: "parallel" }, participants: [
      { memberEntityId: "member-a", name: "横梁", manufacturingMappingStatus: "persisted" },
      { memberEntityId: "member-b", name: "立杆", manufacturingMappingStatus: "persisted" },
    ] };
    const secondJoint = { ...joint, key: "joint-2", label: "另一处连接" };
    Object.assign(productConnections.productAssemblyConnectionsState(view), { key: productConnections.productAssemblyConnectionIdentity(view).key, status: "ready", result: {
      productEntityId: "product-1", generationRunId: "run-1", source: "committed-product-model", connections: [joint, secondJoint],
    } });
    const identity = bindings.productAssemblyBindingIdentity(view);
    Object.assign(bindings.productAssemblyBindingsState(view), { ...identity, status: "ready", result: {
      productEntityId: "product-1", generationRunId: "run-1", bindings: [],
      members: [{ memberEntityId: "member-a", name: "横梁" }, { memberEntityId: "member-b", name: "立杆" }],
      capabilities: { anchorKinds: ["side"], templates: [] }, connections: [joint, secondJoint],
    } });
    const readers = [], calls = [], snapshots = [];
    let viewport, viewportCount = 0, stopped = 0;
    let completeRestore;
    const resources = { get() { throw Error("unexpected resource read"); } };
    const context = { sceneProxy: { resources, invoke(method, payload) {
      calls.push({ method, payload });
      if (method === "TubeDesigner.List" && payload?.includeManufacturingGeometry === true)
        return new Promise((resolve) => { completeRestore = () => resolve({ tubeDesigner: { activeProductId: "stale-read" } }); });
      throw Error(`unexpected request ${method}`);
    },
      views: { async start(definition, options) { readers.push({ definition, options }); return { poll: async () => null, stop: async () => stopped++ }; } } } };
    const mesh = (length) => {
      const box = new THREE.BoxGeometry(length, 20, 20);
      return { kind: "mesh", positions: [...box.attributes.position.array], indices: [...box.index.array] };
    };
    const resource = (url, version, length) => ({ url, version, type: "geometry", data: mesh(length) });
    const viewportFactory = (options) => {
      viewportCount++;
      viewport = createThreeViewport(options);
      for (const [url, length] of [["real-a", 100], ["real-b", 80], ["cut-a", 95], ["cut-b", 75]])
        viewport.resourcePromises.set(`${url}@1`, Promise.resolve(resource(url, 1, length)));
      const apply = viewport.applyViewSnapshot.bind(viewport);
      viewport.applyViewSnapshot = async (snapshot, client) => { snapshots.push(snapshot); return apply(snapshot, client); };
      return viewport;
    };
    const render = () => {
      const existingMount = document.querySelector("main");
      const restoreInteraction = existingMount ? capturePaneInteraction(existingMount) : () => {};
      const left = library.renderAssemblyLibraryLeftPane(context, view), right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      let mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport">${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        mount = document.querySelector("main"); patch.rememberLibraryDom(view, mount, "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) throw Error("full DOM replacement");
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory });
      restoreInteraction();
    };
    const settle = async () => { for (let i = 0; i < 6; i++) await new Promise(requestAnimationFrame); };
    const action = (name, target) => library.handleAssemblyLibraryAction(context, view, `tube-designer-assembly-${name}`, target, { renderProject: render });
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    render(); await settle();
    const snapshot = { revision: "1", rows: [
      { entityId: "member-a", data: { geometry: { url: "real-a", version: 1 }, localToWorldMatrix: transform } },
      { entityId: "member-b", data: { geometry: { url: "real-b", version: 1 } } },
      { entityId: "other-product", data: { geometry: { url: "wrong", version: 1 } } },
    ] };
    readers[0].options.onChange(snapshot); await settle();
    // Product mode is node-first: search and parameters appear after a real node and template are chosen.
    await action("select-connection", document.querySelector('[data-tube-connection-key="joint-1"]'));
    await action("select", document.querySelector('[data-tube-assembly-id="mechanical-fastener"]'));
    const pendingSearch = document.querySelector(".tube-connection-library-search input");
    const pendingLeft = document.querySelector(".tube-connection-library-list");
    const pendingRight = document.querySelector(".tube-connection-library-editor-body");
    const pendingCanvas = document.querySelector(".icax-three-viewport-canvas");
    const hydrationWaited = viewport.sceneObjects.size === 0
      && !snapshots.some((item) => item.rows.length > 0)
      && document.querySelector(".tube-assembly-preview-wait .tube-designer-export-progress-card")?.textContent.includes("正在恢复");
    pendingSearch.focus({ preventScroll: true }); pendingSearch.value = "装配工艺"; pendingSearch.setSelectionRange(1, 2);
    pendingLeft.scrollTop = 60; pendingRight.scrollTop = 80;
    // The user keeps moving both sidebars while the native restore is in flight.
    pendingLeft.scrollTop = 75; pendingRight.scrollTop = 95;
    completeRestore(); await settle();
    const hydrationState = {
      search: pendingSearch === document.querySelector(".tube-connection-library-search input"),
      canvas: pendingCanvas === document.querySelector(".icax-three-viewport-canvas"),
      focus: document.activeElement === pendingSearch,
      selection: [pendingSearch.selectionStart, pendingSearch.selectionEnd],
      left: pendingLeft.scrollTop, right: pendingRight.scrollTop,
      productId: view.scene.tubeDesigner.activeProductId,
    };
    const hydrationStable = hydrationState.search && hydrationState.canvas && hydrationState.focus
      && hydrationState.selection[0] === 1 && hydrationState.selection[1] === 2
      && hydrationState.left === 75 && hydrationState.right === 95 && hydrationState.productId === "product-1";
    const canvas = document.querySelector(".icax-three-viewport-canvas");
    const search = document.querySelector(".tube-connection-library-search input");
    let diameter = document.querySelector('[data-tube-assembly-parameter="nominalDiameter"]');
    const left = document.querySelector(".tube-connection-library-list"), right = document.querySelector(".tube-connection-library-editor-body");
    let nested = document.querySelector(".tube-assembly-binding-parameters");
    let inputEvents = 0, canvasEvents = 0;
    search.addEventListener("input", () => inputEvents++); canvas.addEventListener("test-stable", () => canvasEvents++);
    const productPane = document.querySelector(".tube-assembly-preview-pane.scene");
    const productHeader = productPane.querySelector("header");
    const productHeaderRect = productHeader.getBoundingClientRect();
    const productSwitchRect = productHeader.querySelector(".tube-assembly-view-switch").getBoundingClientRect();
    const productHeaderCompact = !productHeader.querySelector(".tube-assembly-preview-caption, .tube-assembly-preview-title, [data-tube-assembly-camera]")
      && !productHeader.textContent.includes("真实产品") && productHeader.title.includes("新增加工见下料件");
    const productSwitchCentered = Math.abs((productSwitchRect.left + productSwitchRect.right - productHeaderRect.left - productHeaderRect.right) / 2) < 1;
    const cube = document.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]");
    const cubeTarget = cube?.querySelector('[data-cam-action="view-standard"][data-cam-view="front"]')
      ?? cube?.querySelector('[data-cam-action="view-standard"]');
    const cameraBeforeCube = viewport.getViewCubeState().direction;
    cubeTarget?.dispatchEvent(new MouseEvent("click", { bubbles: true }));
    await settle();
    const cameraAfterCube = viewport.getViewCubeState().direction;
    const cubeControlsAssemblyOnly = !!cube && !!cubeTarget
      && cube.querySelector(".cam-viewcube-svg") !== null
      && Math.hypot(cameraAfterCube.x - cameraBeforeCube.x, cameraAfterCube.y - cameraBeforeCube.y,
        cameraAfterCube.z - cameraBeforeCube.z) > 0.01
      && backgroundCameraCalls.length === 0;
    const modelCheck = { ids: [...viewport.sceneObjects.keys()], position: new THREE.Vector3().setFromMatrixPosition(viewport.sceneObjects.get("member-a").matrix).toArray(),
      description: productPane.getAttribute("aria-label"), productHeaderCompact, productSwitchCentered, cubeControlsAssemblyOnly };
    const connectionButton = document.querySelector('[data-cam-action="tube-designer-assembly-select-connection"]');
    const connectionsVisible = !!connectionButton && connectionButton.getClientRects().length > 0
      && !document.querySelector(".tube-assembly-product-connections details");
    search.focus({ preventScroll: true }); search.value = "连接"; search.setSelectionRange(1, 2);
    left.scrollTop = 70; right.scrollTop = 90;
    await action("select-connection", connectionButton);
    await action("select", document.querySelector('[data-tube-assembly-id="mechanical-fastener"]'));
    const connectionSelected = bindings.productAssemblyBindingDraft(view, { id: "mechanical-fastener" }).connectionKey === "joint-1"
      && document.querySelector('[data-cam-change-action="tube-designer-binding-connection"]').value === "joint-1"
      && document.querySelector('[data-cam-action="tube-designer-assembly-select-connection"]').getAttribute("aria-pressed") === "true";
    const connectionSelect = document.querySelector('[data-cam-change-action="tube-designer-binding-connection"]');
    connectionSelect.value = "joint-2";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-binding-connection", connectionSelect, { renderProject: render });
    const rightToLeftSelection = library.assemblyLibraryState(view).selectedProductConnectionKey === "joint-2"
      && document.querySelector('[data-tube-connection-key="joint-2"]').getAttribute("aria-pressed") === "true"
      && document.querySelector('[data-tube-connection-key="joint-1"]').getAttribute("aria-pressed") === "false";
    await action("select-connection", document.querySelector('[data-tube-connection-key="joint-1"]'));
    await action("select", document.querySelector('[data-tube-assembly-id="mechanical-fastener"]'));
    diameter = document.querySelector('[data-tube-assembly-parameter="nominalDiameter"]');
    nested = document.querySelector(".tube-assembly-binding-parameters");
    const selectionState = { focus: document.activeElement === search, selection: [search.selectionStart, search.selectionEnd],
      canvasSame: canvas === document.querySelector(".icax-three-viewport-canvas"),
      searchSame: search === document.querySelector(".tube-connection-library-search input"),
      left: left.scrollTop, right: right.scrollTop };
    const selectionStable = selectionState.focus && selectionState.selection[0] === 1 && selectionState.selection[1] === 2
      && selectionState.canvasSame && selectionState.searchSame && selectionState.left === 70;
    let releaseMesh;
    viewport.resourcePromises.set("real-a@2", new Promise((resolve) => { releaseMesh = resolve; }));
    search.focus({ preventScroll: true }); search.value = "装配工艺"; search.setSelectionRange(1, 2);
    left.scrollTop = 80; right.scrollTop = 100; nested.scrollTop = 20;
    readers[0].options.onChange({ ...snapshot, revision: "2", rows: snapshot.rows.map((row) => row.entityId === "member-a"
      ? { ...row, data: { ...row.data, geometry: { url: "real-a", version: 2 } } } : row) });
    left.scrollTop = 125; right.scrollTop = 155; nested.scrollTop = 35; search.setSelectionRange(1, 3);
    releaseMesh(resource("real-a", 2, 120)); await settle();
    const stable = { canvas: canvas === document.querySelector(".icax-three-viewport-canvas"), search: search === document.querySelector(".tube-connection-library-search input"),
      focus: document.activeElement === search, selection: [search.selectionStart, search.selectionEnd],
      left: left.scrollTop, right: right.scrollTop, nested: nested.scrollTop, nestedSame: nested === document.querySelector(".tube-assembly-binding-parameters"),
      cubeSame: cube === document.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]") };
    const beforeParameters = snapshots.length;
    const fastener = document.querySelector('[data-tube-assembly-parameter="fastenerType"]');
    fastener.value = "adjustableBolt"; await action("parameter-change", fastener);
    const conditionalAdded = !!document.querySelector('[data-tube-assembly-parameter="adjustment"]')
      && diameter === document.querySelector('[data-tube-assembly-parameter="nominalDiameter"]') && document.activeElement === search;
    fastener.value = "bolt"; await action("parameter-change", fastener); await settle();
    const conditionalRemoved = !document.querySelector('[data-tube-assembly-parameter="adjustment"]');
    const parametersDoNotRegenerateProduct = snapshots.length === beforeParameters;
    await action("set-view", { dataset: { tubeAssemblyView: "exploded" } }); await settle();
    const cutHeader = document.querySelector(".tube-assembly-preview-pane.scene > header");
    const cutHeaderRect = cutHeader.getBoundingClientRect();
    const cutSwitchRect = cutHeader.querySelector(".tube-assembly-view-switch").getBoundingClientRect();
    const partSelectRect = cutHeader.querySelector(".tube-assembly-preview-part-select")?.getBoundingClientRect();
    const cutHeaderLayout = !!partSelectRect
      && Math.abs((cutSwitchRect.left + cutSwitchRect.right - cutHeaderRect.left - cutHeaderRect.right) / 2) < 1
      && partSelectRect.left > cutSwitchRect.right && Math.abs(partSelectRect.right - cutHeaderRect.right) < 24
      && !cutHeader.querySelector('[data-tube-assembly-camera]')
      && cube === document.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]");
    const cutCubeTarget = [...cube.querySelectorAll('[data-cam-action="view-standard"]')]
      .find((item) => item.dataset.camView !== cubeTarget.dataset.camView);
    const cameraBeforeCutCube = viewport.getViewCubeState().direction;
    cutCubeTarget?.dispatchEvent(new MouseEvent("click", { bubbles: true }));
    await settle();
    const cameraAfterCutCube = viewport.getViewCubeState().direction;
    const cubeAfterSwitchActive = !!cutCubeTarget
      && Math.hypot(cameraAfterCutCube.x - cameraBeforeCutCube.x, cameraAfterCutCube.y - cameraBeforeCutCube.y,
        cameraAfterCutCube.z - cameraBeforeCutCube.z) > 0.01
      && backgroundCameraCalls.length === 0;
    const cut = snapshots.at(-1).rows[0];
    const cutDisplayed = viewport.sceneObjects.has("part-a") && !viewport.sceneObjects.has("member-a")
      && cut.data.geometry.url === "cut-a" && cut.data.localToWorldMatrix === undefined;
    viewport.resourcePromises.set("cut-a@2", Promise.resolve(resource("cut-a", 2, 90)));
    view.scene.tubeDesigner.parts[0].thumbnailGeometryResourceVersion = 2;
    render(); await settle();
    const machiningUpdated = snapshots.at(-1).rows[0].data.geometry.version === 2;
    await action("product-part", { value: "part-b" }); await settle();
    const selectedPart = snapshots.at(-1).rows[0].entityId;
    search.dispatchEvent(new Event("input")); canvas.dispatchEvent(new Event("test-stable"));
    const finalCanvasStable = canvas === document.querySelector(".icax-three-viewport-canvas");
    view.scene.tubeDesigner.product.partsOutdated = true; render(); await settle();
    const outdatedCleared = snapshots.at(-1).rows.length === 0 && document.querySelector(".tube-connection-library-hud").textContent.includes("重新生成");
    library.disposeAssemblyLibraryViewports(view);
    readers[0].options.onChange(snapshot);
    await settle();
    return { modelCheck, startsAsExample, switchesToProduct, hydrationWaited, hydrationStable, hydrationState, connectionsVisible, connectionSelected, rightToLeftSelection, selectionStable, selectionState,
      stable, conditionalAdded, conditionalRemoved, parametersDoNotRegenerateProduct,
      cutHeaderLayout, cubeAfterSwitchActive, cutDisplayed, machiningUpdated, selectedPart, finalCanvasStable, outdatedCleared, viewportCount, inputEvents, canvasEvents,
      hydrationCalls: calls, stopped, readerStatus: product.assemblyProductSceneState(view).status };
  }, templates);
  assert.deepEqual(result.modelCheck.ids.sort(), ["member-a", "member-b"]);
  assert.deepEqual(result.modelCheck.position, [128, 56, 32]);
  assert.match(result.modelCheck.description, /新增加工见下料件/);
  assert.equal(result.modelCheck.productHeaderCompact, true, "产品视口顶部不应重复标题、长说明或显示第二套相机按钮");
  assert.equal(result.modelCheck.productSwitchCentered, true, "产品视图切换应相对于整个视口居中");
  assert.equal(result.modelCheck.cubeControlsAssemblyOnly, true, "ViewCube 必须存在、可点击，并且只控制装配视口相机");
  for (const key of ["startsAsExample", "switchesToProduct", "hydrationWaited", "hydrationStable", "connectionsVisible", "connectionSelected", "rightToLeftSelection", "selectionStable"]) assert.equal(result[key], true, `${key}: ${JSON.stringify(key === "selectionStable" ? result.selectionState : result.hydrationState)}`);
  assert.deepEqual(result.stable, { canvas: true, search: true, focus: true, selection: [1, 3], left: 125, right: 155, nested: 35, nestedSame: true, cubeSame: true });
  for (const key of ["conditionalAdded", "conditionalRemoved", "parametersDoNotRegenerateProduct", "cutHeaderLayout", "cubeAfterSwitchActive", "cutDisplayed", "machiningUpdated", "finalCanvasStable", "outdatedCleared"]) assert.equal(result[key], true, key);
  assert.equal(result.selectedPart, "part-b");
  assert.equal(result.viewportCount, 1);
  assert.equal(result.inputEvents, 1); assert.equal(result.canvasEvents, 1);
  assert.deepEqual(result.hydrationCalls, [{ method: "TubeDesigner.List", payload: { includeManufacturingGeometry: true } }]);
  assert.equal(result.stopped, 1); assert.equal(result.readerStatus, "idle");
  assert.deepEqual(errors, []);
  console.log("AssemblyProductScene.browser: real WebGL product placement, committed machining, stable DOM/focus/selection/scroll passed.");
} finally { await browser.close(); }
