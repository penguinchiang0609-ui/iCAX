// Exercise the actual AppShell -> product entry -> createWorkbench chain.
// Only the native proxy transport is adapted; no renderer or DOM patch is mocked.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../../../", import.meta.url));
const source = resolve(root, "src"), output = resolve(root, "output/tests/product-fold-parameters");
mkdirSync(output, { recursive: true });
const descriptor = JSON.parse(readFileSync(resolve(source, "apps/tube-designer/templates/product/single_face_security_window/template.json"), "utf8"));
const display = JSON.parse(readFileSync(resolve(source, "apps/tube-designer/templates/product/single_face_security_window/display.json"), "utf8"));
const baseline = process.argv.includes("--baseline");
const failureBaseline = process.argv.includes("--failure-baseline");
const specificationBaseline = process.argv.includes("--specification-baseline");
const addStructureScreenshot = process.argv.includes("--add-structure-screenshot");
const structurePopupScreenshot = process.argv.includes("--structure-dialog-screenshot");
const outerStructureScreenshot = process.argv.includes("--spatial-screenshot") || structurePopupScreenshot ? "spatial"
  : process.argv.includes("--outer-u-screenshot") ? "plane-u" : null;
const screenshotOnly = process.argv.includes("--screenshot-only") || Boolean(outerStructureScreenshot) || addStructureScreenshot;
const tools = ["v-notch-sharp", "edge-arc-groove"].map(id => {
  const directory = resolve(source, "apps/tube-designer/templates/mold", id);
  const manifest = readFileSync(resolve(directory, "tool.json"));
  const descriptor = JSON.parse(manifest);
  const digest = createHash("sha256").update(manifest).update(Buffer.from([0]))
    .update(readFileSync(resolve(directory, descriptor.kind === "programmatic" ? "tool.py" : "geometry.json")))
    .update(readFileSync(resolve(source, "apps/tube-designer/templates/_shared/section_geometry.py"))).digest("hex");
  return { ...descriptor, libraryScope: "system", digest,
    defaultParameters: Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue])) };
});
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ channel: "msedge", headless: true });
try {
  const page = await browser.newPage({ viewport: { width: 1450, height: screenshotOnly ? 1500 : 940 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://right-focus.test/**", route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html><head><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (path === "/src/iCAX-UI/SDK/runtime.mjs") return route.fulfill({ contentType: "text/javascript", body: "export async function connectApplication(){return globalThis.__focusTransport.appProxy;}" });
    const file = resolve(source, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(source + sep)) return route.abort();
    try {
      let body = readFileSync(file, "utf8");
      if (baseline && path.endsWith("/AppShell/app/bootstrap.mjs")) {
        // Reproduce the original production bug without an unversioned fixture:
        // the acknowledgement replaced AppShell's entire root via render().
        const begin = body.indexOf("  async refreshActiveSceneState(");
        const end = body.indexOf("  async withProgress(", begin);
        assert(begin >= 0 && end > begin, "production acknowledgement action missing");
        body = body.slice(0, begin) + `  async refreshActiveSceneState() {
    if (!state.activeSceneProxy) return null;
    const sceneState = await state.activeSceneProxy.getState();
    state.activeSceneState = sceneState;
    const surfaceMount = render();
    if (await surfaceMount === false) throw new Error(state.error || "Product surface failed to synchronize with scene state");
    return sceneState;
  },\n\n` + body.slice(end);
      }
      if (failureBaseline && path.endsWith("/_shared/workbench/createWorkbench.mjs")) {
        const change = body.indexOf("  mount.onchange = (event) => {");
        const begin = body.indexOf("      .catch((error) => {", change);
        const end = body.indexOf("      .finally(() => {", begin);
        assert(change >= 0 && begin > change && end > begin, "actual workbench onchange failure handler missing");
        body = body.slice(0, begin) + `      .catch((error) => {
        view.error = error?.message ?? String(error);
        appendProjectLog(context, "error", \`\${action} 失败：\${view.error}\`);
        renderProject(context, view);
      })\n` + body.slice(end);
      }
      if (specificationBaseline && path.endsWith("/tube-designer/webpage/designerActions.mjs")) {
        const begin = body.indexOf("  if (commitMount?.isConnected === false");
        const end = body.indexOf("  view.tubeDesignerManufacturingPlans ??= {};", begin);
        assert(begin >= 0 && end > begin, "scene-specification response scope guard missing");
        body = body.slice(0, begin) + body.slice(end);
      }
      return route.fulfill({ contentType: path.endsWith(".css") ? "text/css" : "text/javascript", body });
    } catch { return route.abort(); }
  });
  await page.goto("http://right-focus.test/");
  await page.evaluate(async ({ descriptor, display, failureBaseline, specificationBaseline, tools, screenshotOnly, outerStructureScreenshot }) => {
    const { getProjectView } = await import("/src/apps/_shared/workbench/state/projectViewStore.mjs");
    const { catalogText } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
    const template = { ...descriptor, display, available: true, descriptorLoaded: true, name: catalogText(descriptor.displayName),
      groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: descriptor.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const projectId = "c40e92a4-90a7-4df5-8bfe-0c9d608a6411";
    const sceneId = "0514ae0e-8e25-4926-a339-e4285ea9ba31";
    const parameters = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
    Object.assign(parameters, { faceType: "three", accessDoorEnabled: true, frameManufacturingMode: "plane_v_notch",
      doorFrameJoinType: "v_groove_90:tool_library", doorLeafFrameJoinType: "v_groove_90:tool_library",
      outerFrameGrooveTool: "system:edge-arc-groove", doorFrameGrooveTool: "system:edge-arc-groove", doorLeafFrameGrooveTool: "system:edge-arc-groove",
      outerFrameBendKFactor: .51, doorFrameBendKFactor: .62, doorLeafFrameBendKFactor: .73 });
    if (screenshotOnly) Object.assign(parameters, { faceType: "single",
      outerFrameGrooveTool: "system:v-notch-sharp", doorFrameGrooveTool: "system:v-notch-sharp", doorLeafFrameGrooveTool: "system:v-notch-sharp",
      outerFrameVGrooveBottomStrategy: "rounded", doorFrameVGrooveBottomStrategy: "rounded", doorLeafFrameVGrooveBottomStrategy: "rounded",
      outerFrameVGrooveRoundRadius: 2, doorFrameVGrooveRoundRadius: 3, doorLeafFrameVGrooveRoundRadius: 4,
      outerFrameFoldBridge: 1, doorFrameFoldBridge: .8, doorLeafFrameFoldBridge: .6,
      outerFrameVGrooveMaleFemale: true, doorFrameVGrooveMaleFemale: false, doorLeafFrameVGrooveMaleFemale: true });
    if (outerStructureScreenshot) Object.assign(parameters, { faceType: "three", foldedPostJoint: "weld",
      frameManufacturingMode: outerStructureScreenshot === "spatial" ? "spatial_v_notch" : "plane_v_notch",
      doorFrameJoinType: "miter_45", doorLeafFrameJoinType: "miter_45" });
    const { makeProductToolBinding } = await import("/src/apps/tube-designer/webpage/productResourceBindings.mjs");
    const fields = ["outerFrameGrooveTool", "doorFrameGrooveTool", "doorLeafFrameGrooveTool"].map(key => template.parameters.find(field => field.key === key));
    parameters.tubeDesignerToolBindings = Object.fromEntries(fields.map(field => {
      const binding = makeProductToolBinding(field, tools.find(tool => tool.id === (screenshotOnly ? "v-notch-sharp" : "edge-arc-groove")), null, template);
      return [binding.role, binding];
    }));
    const instances = Array.from({ length: 24 }, (_, index) => ({ entityId: `c40e92a4-90a7-4df5-8bfe-${String(index + 1).padStart(12, "0")}`,
      name: `防盗窗 ${index + 1}`, templateId: template.id, quantity: 1, parameters: structuredClone(parameters), createdAt: `2026-10-04T00:${String(index).padStart(2, "0")}:00` }));
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false }, tubeDesigner: {
      templates: [template], instances, product: instances[0], activeProductId: instances[0].entityId, members: [], joints: [], parts: [], manufacturingGroups: [],
      specificationAnnotations: [{ id: 'focus-height', parameter: 'height', kind: 'spacing', start: [0, 0, 0], end: [0, 0, 1800],
        offset: [32, 0, 24], generatedValue: parameters.height }],
    } };
    const view = getProjectView(projectId);
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
      tubeDesignerSystemPunchTools: tools,
      tubeDesignerParameterPanelProductId: instances[0].entityId,
      tubeDesignerParameterDisclosureState: { initialized: true },
      tubeDesignerExpandedParameterGroups: ["section:materials", "section:process", ...template.groups.map(group => `group:${group.key}`)] });
    const requests = [];
    let releaseUpdate, releaseState, holdState = false, nextUpdateFailure = null;
    const snapshot = () => ({ viewId: "focus-view", revision: String(state.undoRedo.revision),
      rows: state.tubeDesigner.members.map(member => ({ entityId: member.entityId, data: { geometry: { url: member.entityId, version: 1 }, geometryKind: 1 } })) });
    const sceneProxy = { state, resources: { get() { throw new Error("empty View should not fetch geometry"); } }, pdo: { enabled: false },
      views: { async start() { return { get snapshot() { return snapshot(); }, async poll() { return snapshot(); },
        async waitForSnapshot(predicate) { const current = snapshot(); if (!predicate(current)) throw new Error("View transport did not match expected instance"); return current; }, async stop() {} }; } },
      async getState() {
        requests.push({ method: "Scene.GetState" });
        if (holdState) await new Promise(resolve => { releaseState = resolve; });
        holdState = false; releaseState = null;
        return structuredClone(state);
      },
      async invoke(method, payload = {}) {
        requests.push({ method, payload: structuredClone(payload), transportProjectId: projectId });
        if (method === "TubeDesigner.UpdateProductParameters") {
          const failure = nextUpdateFailure; nextUpdateFailure = null;
          await new Promise((resolve, reject) => { releaseUpdate = () => {
            releaseUpdate = null;
            if (failure) { const error = new Error(failure === 'timeout' ? 'SDO call timed out' : 'Native parameter validation rejected');
              error.name = failure === 'timeout' ? 'SDOTimeoutError' : 'SDOError'; reject(error); }
            else resolve();
          }; });
          const instance = state.tubeDesigner.instances.find(item => item.entityId === payload.productEntityId);
          instance.parameters = structuredClone(payload.parameters);
          instance.partsOutdated = true;
          if (state.tubeDesigner.activeProductId === instance.entityId) state.tubeDesigner.product = structuredClone(instance);
          state.undoRedo.revision++;
          return { tubeDesigner: { ...structuredClone(state.tubeDesigner), product: structuredClone(instance) } };
        }
        if (method === "TubeDesigner.GeneratePreview") {
          const entityId = `c40e92a4-90a7-4df5-8bfe-${String(state.tubeDesigner.instances.length + 1).padStart(12, "0")}`;
          const keys = new Set(template.parameters.map(field => field.key));
          const values = Object.fromEntries(Object.entries(payload).filter(([key]) => keys.has(key) || key === "tubeDesignerToolBindings"));
          state.tubeDesigner.instances.push({ entityId, name: payload.instanceName ?? "新建防盗窗", templateId: template.id,
            quantity: payload.instanceQuantity ?? 1, parameters: structuredClone(values), createdAt: payload.createdAt });
          payload = { productEntityId: entityId };
        }
        if (method === "TubeDesigner.ActivateProduct" || method === "TubeDesigner.GeneratePreview") {
          state.tubeDesigner.product = structuredClone(state.tubeDesigner.instances.find(item => item.entityId === payload.productEntityId));
          state.tubeDesigner.activeProductId = state.tubeDesigner.product.entityId;
          const memberId = `display-${payload.productEntityId}`;
          state.tubeDesigner.members = [{ entityId: memberId, stableKey: 'outer_frame.left.0001' }];
          state.tubeDesigner.generationRun = { entityId: `generation-${payload.productEntityId}` };
          state.undoRedo.revision++;
          view.viewport.resourcePromises.set(`${memberId}@1`, Promise.resolve({ url: memberId, version: 1, type: "geometry", data: {
            kind: "mesh", positions: [-19,-19,0, 19,-19,0, 19,19,0, -19,19,0, -19,-19,1800, 19,-19,1800, 19,19,1800, -19,19,1800],
            indices: [0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7],
          } }));
          return { tubeDesigner: structuredClone(state.tubeDesigner) };
        }
        if (method === "TubeDesigner.GetProductTemplateDescriptor") return { template };
        if (method === "TubeDesigner.GetPunchTools") return { tools };
        if (method === "TubeDesigner.List") return { tubeDesigner: structuredClone(state.tubeDesigner) };
        throw new Error("Unexpected native transport request: " + method);
      } };
    const projectState = { projectId, projectName: "焦点回归", mainScene: state };
    let savedScene = null, releaseSave = null, nextSaveFailure = false, cancelNextSaveDialog = false;
    const saveRequests = [], fileRequests = [];
    const savePath = "D:/fixtures/security-window-public.icax";
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; },
      async save(path) {
        const snapshot = structuredClone(state), failure = nextSaveFailure; nextSaveFailure = false;
        saveRequests.push({ projectId, path, snapshot });
        await new Promise((resolve, reject) => { releaseSave = () => { releaseSave = null; failure ? reject(new Error("Fixture save failed")) : resolve(); }; });
        savedScene = snapshot; projectState.projectPath = path;
      } };
    const otherProjectId = 'c40e92a4-90a7-4df5-8bfe-0c9d608a6412';
    const otherState = { ...structuredClone(state), sceneId: '0514ae0e-8e25-4926-a339-e4285ea9ba32' };
    const otherView = getProjectView(otherProjectId);
    Object.assign(otherView, { scene: structuredClone(otherState), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
      tubeDesignerParameterPanelProductId: otherState.tubeDesigner.product.entityId,
      tubeDesignerParameterDisclosureState: { initialized: true },
      tubeDesignerExpandedParameterGroups: ["section:materials", "section:process", ...template.groups.map(group => `group:${group.key}`)] });
    const otherSceneProxy = { state: otherState, resources: sceneProxy.resources, pdo: { enabled: false },
      async getState() { return structuredClone(otherState); }, async invoke(method, payload) {
        requests.push({ method, payload: structuredClone(payload), transportProjectId: otherProjectId });
        throw new Error('Unexpected second-project transport request ' + method);
      },
      views: { async start() { const snapshot = { viewId: 'other-focus-view', revision: '1', rows: [] };
        return { snapshot, async poll() { return snapshot; }, async stop() {} }; } } };
    const otherProjectState = { projectId: otherProjectId, projectName: '另一个项目', mainScene: otherState };
    const otherProjectProxy = { projectId: otherProjectId, state: otherProjectState, getMainScene() { return otherSceneProxy; } };
    const productState = { productId: "icax.tube-designer", productName: "TubeDesigner", isStarted: true,
      frontendEntry: "/src/apps/tube-designer/webpage/entry.mjs", catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ["icax"] } };
    const productProxy = { productId: productState.productId, state: productState, projects: new Map([[projectId, projectProxy], [otherProjectId, otherProjectProxy]]),
      async getState() { return productState; }, getProject(id) { return this.projects.get(id) ?? projectProxy; },
      async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; } };
    const bridge = { async saveFileDialog(options) { fileRequests.push({ kind: "save", options }); if (cancelNextSaveDialog) { cancelNextSaveDialog = false; return null; } return savePath; },
      async openFileDialog(options) { fileRequests.push({ kind: "open", options }); return savePath; } };
    const appProxy = { bridge, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; },
      async openProjectFile(path) {
        if (!savedScene || path !== savePath) throw new Error("Unexpected reopened project path");
        const reopenedId = "c40e92a4-90a7-4df5-8bfe-0c9d608a6413";
        const reopenedState = structuredClone(savedScene), reopenedView = getProjectView(reopenedId);
        Object.assign(reopenedView, { scene: reopenedState, tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
          tubeDesignerSystemPunchTools: tools, tubeDesignerParameterPanelProductId: reopenedState.tubeDesigner.product.entityId,
          tubeDesignerParameterDisclosureState: { initialized: true }, tubeDesignerExpandedParameterGroups: ["section:materials", "section:process", ...template.groups.map(group => `group:${group.key}`)] });
        const reopenedScene = { ...sceneProxy, state: reopenedState, async getState() { return structuredClone(reopenedState); },
          views: { async start() { const data = { viewId: "reopened-view", revision: "1", rows: reopenedState.tubeDesigner.members.map(member => ({ entityId: member.entityId, data: { geometry: { url: member.entityId, version: 1 }, geometryKind: 1 } })) };
            return { snapshot: data, async poll() { return data; }, async stop() {} }; } } };
        const reopenedProjectState = { projectId: reopenedId, projectName: "保存后重新打开", projectPath: path, mainScene: reopenedState };
        const reopenedProject = { projectId: reopenedId, state: reopenedProjectState, getMainScene() { return reopenedScene; } };
        productProxy.projects.set(reopenedId, reopenedProject);
        // The native transport fixture supplies the existing display resources.
        const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
        reopenedView.viewport = createThreeViewport({ continuousRender: false });
        reopenedView.viewport.resourcePromises = new Map(view.viewport.resourcePromises);
        globalThis.__focusTransport.reopenedView = reopenedView;
        return { productProxy, product: productState, projectProxy: reopenedProject, sceneProxy: reopenedScene,
          catalog: { mainProject: reopenedProjectState }, projectPath: path };
      } };
    globalThis.__focusTransport = { appProxy, view, state, requests, projectId, otherProjectId, otherView, otherState, failureBaseline, specificationBaseline,
      saveRequests, fileRequests, savePath, get waitingSave() { return Boolean(releaseSave); }, get savedScene() { return savedScene; },
      releaseSave() { if (!releaseSave) throw new Error("No save waiting"); releaseSave(); }, failNextSave() { nextSaveFailure = true; }, cancelNextSaveDialog() { cancelNextSaveDialog = true; },
      get waitingUpdate() { return Boolean(releaseUpdate); }, get waitingState() { return Boolean(releaseState); },
      failNextUpdate(kind = 'rejected') { nextUpdateFailure = kind; },
      holdNextState() { holdState = true; }, releaseUpdate() { if (!releaseUpdate) throw new Error("No waiting EC update"); releaseUpdate(); },
      releaseState() { if (!releaseState) throw new Error("No waiting Scene.GetState"); releaseState(); },
      async waitIdle() { await view.activeAreaAction?.promise; await view.tubeDesignerProductParameterCommitPromise; } };
    await import("/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs");
  }, { descriptor, display, failureBaseline, specificationBaseline, tools, screenshotOnly, outerStructureScreenshot });
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().activeProjectId && !globalThis.__icaxAppShell.getState().pendingCount && document.querySelector('[data-cam-action="tube-designer-open-product-structure"]'));
  await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{height:360px!important;overflow:auto!important}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:600px}.tube-designer-instance-panel{height:580px!important}.tube-designer-instance-list{height:250px!important;min-height:0!important;max-height:250px!important;overflow:auto!important;flex:none!important;display:block!important}.tube-designer-instance-list:after{content:"";display:block;height:1200px}.tube-designer-parameter-panel{height:680px!important}.tube-designer-parameter-scroll{height:520px!important;overflow:auto!important;flex:none!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:500px}' });
  await page.evaluate(() => {
    for (const key of ["assembly_outer","assembly_fixed","assembly_leaf","assembly_grid"]) {
      const group = document.querySelector('[data-tube-designer-parameter-group="scene:process:section-group:' + key + '"]');
      if(!group?.open) throw new Error("Product part group should start expanded: " + key);
    }
    // The fixture forces materials open only to create enough nested scrolling.
    document.querySelectorAll('details').forEach(node => { node.open = true; });
  });

  const results = screenshotOnly ? [] : await page.evaluate(async () => {
    const f = globalThis.__focusTransport, results = [];
    const { productControlValue, productControlChoices } = await import("/src/apps/tube-designer/webpage/productControls.mjs");
    const q = selector => document.querySelector(selector);
    const field = key => q('[data-tube-designer-parameter="' + key + '"]');
    const structureKeys=["outerFrameStructure","doorFrameStructure","doorLeafFrameStructure"];
    const structureButton=key=>q('[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="'+key+'"]');
    const choiceValue=key=>productControlValue(f.state.tubeDesigner.templates[0],key,f.state.tubeDesigner.product.parameters);
    const option = key => structureKeys.includes(key) ? structureButton(key)
      : q('[data-tube-designer-parameter-form] [data-product-control-editor="' + key + '"] select');
    const group = key => q('[data-tube-designer-parameter-group="scene:process:section-group:' + key + '"]');
    const require = (value, label) => { if (!value) throw new Error(label); };
    const equal = (actual, expected, label) => {
      if (JSON.stringify(actual) !== JSON.stringify(expected)) throw new Error(label + ": " + JSON.stringify({actual, expected}));
    };
    const checkOuterOrder = second => {
      const rows = [...group("assembly_outer").querySelectorAll(".tube-designer-field")];
      require(rows[0]?.contains(option("outerFrameStructure")) && rows[1]?.contains(option(second)) && choiceValue(second),
        "actual outer-frame summary must precede its right-side bend or splice selector");
      for(const key of structureKeys)
        require(!q('[data-tube-designer-parameter-form] [data-product-control-editor="'+key+'"] select'),
          "right pane must contain summaries rather than inline structure choices: "+key);
    };
    const scrollNodes = () => [q(".cam-context-pane"), q(".tube-designer-instance-list"), q(".cam-info-pane"), q(".tube-designer-parameter-scroll")];
    const scrolls = () => scrollNodes().map(node => node.scrollTop);
    const setScrolls = values => { scrollNodes().forEach((node, index) => { node.scrollTop = values[index]; }); equal(scrolls(), values, "real four-container overflow"); };
    const frames = () => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const wait = async getter => { for (let count = 0; !getter() && count < 250; count++) await new Promise(resolve => setTimeout(resolve, 10)); require(getter(), "native transport did not become pending"); };
    const identities = () => ({surface:q('[data-product-surface="project"]'), form:q('[data-tube-designer-parameter-form]'),
      canvas:q("canvas"), code:field("productCode"), fixed:field("doorFrameBendKFactor"), leaf:field("doorLeafFrameBendKFactor"),
      bridge:field("outerFrameFoldBridge"), outerChoice:option("outerFrameBendType"), fixedChoice:option("doorFrameConnection"),
      outerGroup:group("assembly_outer"), fixedGroup:group("assembly_fixed"), leafGroup:group("assembly_leaf"), gridGroup:group("assembly_grid")});
    const initial = identities();
    const assertIdentities = () => { const current = identities(); for (const key of Object.keys(initial)) require(current[key] === initial[key], "production patch replaced " + key); };
    let listenerCalls = 0;
    for (const node of Object.values(initial)) { require(node, "actual product node missing"); node.addEventListener("fold-regression-probe", () => listenerCalls++); }
    async function commit(target, value, updating=()=>{}, responding=()=>{}) {
      f.holdNextState(); target.focus({preventScroll:true});
      if (target.dataset.camAction === "tube-designer-open-product-structure") {
        const before = JSON.stringify(f.state.tubeDesigner.product.parameters), requestCount = f.requests.length;
        target.click(); await frames();
        const dialog = q("[data-tube-designer-structure-dialog]");
        const mainKey=target.dataset.productControlKey;
        const primary=dialog?.querySelector('select[data-product-control-key="'+mainKey+'"]');
        const select=primary;
        require(primary&&dialog.querySelectorAll("select").length===1,"structure dialog must contain only its main structure selector: "+mainKey);
        require(![...primary.options].some(item=>["edge-arc","v-groove","rounded-v-groove","butt","miter","insert"].includes(item.value)),
          "structure dialog exposes a specific machining or connection choice");
        require(document.activeElement === primary, "structure dialog did not focus its primary local selector");
        select.value = value; select.dispatchEvent(new Event("change", {bubbles:true})); await frames();
        equal(f.requests.length, requestCount, "local structure selection wrote to native before confirmation");
        equal(JSON.stringify(f.state.tubeDesigner.product.parameters), before, "local structure choice changed product before confirmation");
        dialog.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]').click();
      } else {
        if (target.type === "checkbox") target.checked = value; else target.value = value;
        target.dispatchEvent(new Event("input", {bubbles:true}));
        target.dispatchEvent(new Event("change", {bubbles:true}));
      }
      await wait(() => f.waitingUpdate); updating(); f.releaseUpdate();
      await wait(() => f.waitingState); responding(); f.releaseState();
      await f.waitIdle(); await frames();
    }
    const outer = field("outerFrameBendKFactor"), fixed = initial.fixed, leaf = initial.leaf;
    checkOuterOrder("outerFrameBendType");
    for(const [key,part] of [["outerFrameStructure","assembly_outer"],["outerFrameBendType","assembly_outer"],
      ["doorFrameStructure","assembly_fixed"],["doorLeafFrameStructure","assembly_leaf"],["doorFrameConnection","assembly_fixed"],["doorLeafFrameConnection","assembly_leaf"],
      ["horizontalEndConnectionChoice","assembly_grid"],["verticalEndConnectionChoice","assembly_grid"]])
      require(option(key)?.closest("details")===group(part),"connection grouped under wrong product part: "+key);
    for(const [prefix,part] of [["outerFrame","assembly_outer"],["doorFrame","assembly_fixed"],["doorLeafFrame","assembly_leaf"]])
      for(const suffix of ["BendKFactor","FoldBridge"])
        require(field(prefix+suffix)?.closest("details")===group(part),"fold setting grouped under wrong part");
    for(const part of ["assembly_outer","assembly_fixed","assembly_leaf","assembly_grid"]) {
      const rows=[...group(part).querySelectorAll(".tube-designer-field")].map(node=>node.getBoundingClientRect());
      for(let index=1;index<rows.length;index++)require(rows[index].top>=rows[index-1].bottom-1,"part fields must each occupy a separate line");
    }
    results.push({case:"product-part-groups",defaultExpanded:true,connectionsAndFoldFieldsTogether:true,eachFieldOneLine:true});
    require(outer && fixed && leaf, "three edge roles must have independent K controls");
    equal([outer.value, fixed.value, leaf.value], ["0.51", "0.62", "0.73"], "independent initial values");
    await commit(outer, "0.54", () => { outer.value = "0.57"; outer.focus({preventScroll:true}); setScrolls([45,115,55,205]); },
      () => { outer.value = "0.59"; setScrolls([61,131,71,221]); });
    require(field("outerFrameBendKFactor") === outer && document.activeElement === outer && outer.value === "0.59",
      "K asynchronous response lost actual node, focus or later typing");
    assertIdentities(); equal(scrolls(), [61,131,71,221], "latest nested scrolls after K update");
    equal([fixed.value,leaf.value], ["0.62","0.73"], "another role K changed");
    results.push({case:"independent-k-latest-edit", value:outer.value, scrolls:scrolls(), nodesAndFocusPreserved:true});

    const code = initial.code;
    await commit(fixed, "0.64", () => { code.focus({preventScroll:true}); code.value="UNCOMMITTED-NEW-FOLD-DRAFT"; code.setSelectionRange(2,10,"backward"); setScrolls([75,145,85,235]); },
      () => { code.setSelectionRange(4,13,"forward"); setScrolls([91,161,101,251]); });
    require(document.activeElement === code && code.value === "UNCOMMITTED-NEW-FOLD-DRAFT", "later code focus/draft overwritten");
    equal([code.selectionStart,code.selectionEnd,code.selectionDirection], [4,13,"forward"], "latest text caret/selection");
    assertIdentities(); equal(scrolls(), [91,161,101,251], "latest four scroll positions");
    results.push({case:"new-fold-field-preserves-text-selection", latestSelectionPreserved:true, scrolls:scrolls()});
    // User disclosure is also interaction state, and must not reset to the
    // default-open declaration when an unrelated role edit responds later.
    await commit(leaf,"0.75",()=>{ initial.gridGroup.open=false; initial.gridGroup.dispatchEvent(new Event("toggle")); },
      ()=>{ code.focus({preventScroll:true}); code.setSelectionRange(1,9,"backward"); setScrolls([95,165,105,255]); });
    require(group("assembly_grid")===initial.gridGroup&&!initial.gridGroup.open,"asynchronous refresh reopened user's collapsed part group");
    require(document.activeElement===code,"part disclosure refresh stole focus");
    equal([code.selectionStart,code.selectionEnd,code.selectionDirection],[1,9,"backward"],"group edit latest caret");
    assertIdentities();equal(scrolls(),[95,165,105,255],"group disclosure edit latest nested scroll");
    results.push({case:"part-group-disclosure-preserved",collapsedGroupRetained:true,latestCaretAndScrollRetained:true});

    await commit(initial.outerChoice, "rounded-v-groove", () => { outer.focus({preventScroll:true}); setScrolls([105,175,115,265]); },
      () => { if (outer.isConnected) outer.focus({preventScroll:true}); setScrolls([121,191,131,281]); });
    require(!outer.isConnected && !field("outerFrameBendKFactor"), "rounded V must remove only outer edge K");
    require(document.activeElement !== fixed && document.activeElement !== leaf && !document.activeElement.matches("input,select,textarea"),
      "removed outer K lent its focus to another role field");
    assertIdentities(); equal(scrolls(), [121,191,131,281], "condition removal changed latest scroll");
    const radius=field("outerFrameVGrooveRoundRadius"), male=field("outerFrameVGrooveMaleFemale");
    require(radius && male && !field("outerFrameVGrooveBottomStrategy"), "rounded product choice did not expose only product-owned radius/male");
    equal(radius.value,"2","original mold radius default");
    equal(choiceValue("outerFrameBendType"),"rounded-v-groove","rounded choice must recognize hidden strategy");
    await commit(radius,"3.4");
    require(field("outerFrameVGrooveRoundRadius")===radius && document.activeElement===radius,"radius commit rebuilt its node");
    const native = f.state.tubeDesigner.product.parameters;
    equal([native.outerFrameVGrooveBottomStrategy,native.outerFrameVGrooveRoundRadius,native.doorFrameBendKFactor,native.doorLeafFrameBendKFactor],
      ["rounded",3.4,.64,.75],"native normalized request cross-linked role drafts");
    results.push({case:"rounded-v-product-choice-and-radius", independentRoleValues:true, hiddenStrategy:true});

    await commit(initial.outerChoice,"v-groove", () => { radius.focus({preventScroll:true}); setScrolls([135,205,145,295]); },
      () => { if(radius.isConnected)radius.focus({preventScroll:true}); setScrolls([151,221,161,311]); });
    require(!radius.isConnected && !field("outerFrameVGrooveRoundRadius"),"sharp choice must hide radius");
    require(document.activeElement !== fixed && document.activeElement !== leaf,"removed radius borrowed other K focus");
    equal(f.state.tubeDesigner.product.parameters.outerFrameVGrooveRoundRadius,3.4,"hidden radius draft discarded");
    await commit(initial.outerChoice,"rounded-v-groove");
    equal(field("outerFrameVGrooveRoundRadius").value,"3.4","rounded restored another radius");
    assertIdentities();
    for(const prefix of ["outerFrame","doorFrame","doorLeafFrame"])
      require(!field(prefix+"VGrooveBottomStrategy"),"low-level bottom strategy became a visible editor");
    require(!field("bendKFactor")&&!field("vGrooveMaleFemale")&&!field("bendKByMaterial")&&!field("bendKMaterialOverrides"),
      "obsolete common settings appeared");
    for(const node of Object.values(initial))node.dispatchEvent(new Event("fold-regression-probe"));
    equal(listenerCalls,Object.values(initial).length,"listeners lost or duplicated");
    require(!f.requests.some(request=>/GenerateProduct|DisassembleSelected/.test(request.method)),"manufacturing-only edit regenerated model/parts");
    results.push({case:"sharp-rounded-draft-and-listeners", hiddenRadiusRetained:true, nodesAndListenersPreserved:true, modelRegenerations:0});
    const roleNumbers=prefix=>Object.fromEntries(["BendKFactor","FoldBridge","VGrooveRoundRadius","VGrooveMaleFemale"]
      .map(suffix=>[prefix+suffix,f.state.tubeDesigner.product.parameters[prefix+suffix]]));
    const fixedNumbers=roleNumbers("doorFrame"),leafNumbers=roleNumbers("doorLeafFrame");
    const roleButtonColors=[];
    for(const key of structureKeys) {
      const button=structureButton(key),style=getComputedStyle(button);
      const teal=color=>{const components=color.match(/[\d.]+/g)?.slice(0,3).map(Number);return components?.length===3&&components[1]>components[0]&&components[2]>components[0];};
      require(teal(style.backgroundColor)||teal(style.color),"structure button does not use the product's teal color: "+key);
      roleButtonColors.push({key,background:style.backgroundColor,color:style.color});
    }
    for(const key of structureKeys) {
      const button=structureButton(key),savedParameters=structuredClone(f.state.tubeDesigner.product.parameters),
        savedDraft=structuredClone(f.view.tubeDesignerRightDraft),requestCount=f.requests.length;
      button.click();await frames();
      const dialog=q("[data-tube-designer-structure-dialog]");
      const select=dialog?.querySelector('select[data-product-control-key="'+key+'"]');
      require(select&&dialog.querySelectorAll("select").length===1,"each popup must edit only its own main structure");
      require(![...select.options].some(item=>["edge-arc","v-groove","rounded-v-groove","butt","miter","insert"].includes(item.value)),
        "a structure popup exposed a detailed machining choice");
      select.value="joined";select.dispatchEvent(new Event("change",{bubbles:true}));await frames();
      require(f.view.tubeDesignerStructureDialog.controlKey===key&&f.view.tubeDesignerStructureDialog.parameters,
        "role popup must own a complete local physical draft");
      equal(f.state.tubeDesigner.product.parameters,savedParameters,"role local selection saved before confirmation");
      setScrolls([179,249,189,339]);
      dialog.querySelector('[data-cam-action="tube-designer-cancel-product-structure"]').click();await frames();
      equal(f.requests.length,requestCount,"cancel saved a fixed/leaf connection");
      equal(f.state.tubeDesigner.product.parameters,savedParameters,"role cancellation lost saved tool settings");
      equal(f.view.tubeDesignerRightDraft,savedDraft,"role cancellation changed right-side drafts");
      require(document.activeElement===button&&structureButton(key)===button,"role cancel lost the actual opener focus/node");
      equal(scrolls(),[179,249,189,339],"role cancellation changed current nested scrolling");
    }
    results.push({case:"three-role-summary-dialogs",allSummariesReplaceInlineStructureChoices:true,allDialogsOnlyMainStructure:true,allRolesCancelWithoutWrites:true,
      fullLocalPhysicalDrafts:true,focusScrollNodesPreserved:true,buttonColors:roleButtonColors});
    // The two butt-weld wrap fields share a descriptor group but have distinct
    // product-part display declarations. Exercise the actual hidden branches.
    await commit(structureButton("doorFrameStructure"),"joined");
    require(option("doorFrameConnection")===initial.fixedChoice,"fixed structure change replaced its right-side concrete connection selector");
    require([...initial.fixedChoice.options].every(item=>!["edge-arc","v-groove","rounded-v-groove"].includes(item.value)),
      "joined fixed frame kept folded-only machining choices");
    await commit(initial.fixedChoice,"butt");
    const fixedWrap=field("doorFrameButtWrapMode");
    require(fixedWrap?.closest("details")===initial.fixedGroup,"fixed-frame butt wrap escaped its product part group");
    require(!field("doorLeafFrameButtWrapMode"),"fixed-frame butt choice exposed a leaf-only field");
    const fixedWrapValue=[...fixedWrap.options].find(option=>option.value!==fixedWrap.value)?.value;
    require(fixedWrapValue,"real fixed wrap choice missing");
    await commit(fixedWrap,fixedWrapValue,()=>{ code.focus({preventScroll:true});code.setSelectionRange(2,8,"backward");setScrolls([163,233,173,323]); },
      ()=>{ code.setSelectionRange(3,11,"forward");setScrolls([179,249,189,339]); });
    require(field("doorFrameButtWrapMode")===fixedWrap,"fixed wrap asynchronous commit replaced its input node");
    require(document.activeElement===code,"fixed wrap response stole text focus");
    equal([code.selectionStart,code.selectionEnd,code.selectionDirection],[3,11,"forward"],"fixed wrap latest caret");
    equal(scrolls(),[179,249,189,339],"fixed wrap latest four scroll positions");
    await commit(structureButton("doorLeafFrameStructure"),"joined",()=>{code.focus({preventScroll:true});code.value="LEAF-STRUCTURE-LATEST-DRAFT";
      code.setSelectionRange(2,9,"backward");setScrolls([183,253,193,343]);},
      ()=>{code.setSelectionRange(4,13,"forward");setScrolls([199,269,209,359]);});
    require(document.activeElement===code&&code.value==="LEAF-STRUCTURE-LATEST-DRAFT","leaf popup confirmation lost later right-side text focus/draft");
    equal([code.selectionStart,code.selectionEnd,code.selectionDirection],[4,13,"forward"],"leaf popup latest selection");
    equal(scrolls(),[199,269,209,359],"leaf popup latest four scroll positions");
    await commit(option("doorLeafFrameConnection"),"butt");
    const leafWrap=field("doorLeafFrameButtWrapMode");
    require(leafWrap?.closest("details")===initial.leafGroup,"leaf-frame butt wrap escaped its product part group");
    require(field("doorFrameButtWrapMode")===fixedWrap,"leaf butt selection replaced the fixed wrap input");
    require(initial.fixedGroup===group("assembly_fixed")&&initial.leafGroup===group("assembly_leaf"),
      "butt conditional branch rebuilt product part groups");
    require(initial.canvas===q("canvas")&&initial.form===q('[data-tube-designer-parameter-form]'),
      "butt branch rebuilt the canvas or entire form");
    async function confirmUnchangedStructure(key) {
      const requestCount=f.requests.length,saved=structuredClone(f.state.tubeDesigner.product.parameters);
      structureButton(key).click();await frames();
      const dialog=q("[data-tube-designer-structure-dialog]"),select=dialog.querySelector('select[data-product-control-key="'+key+'"]');
      select.dispatchEvent(new Event("change",{bubbles:true}));await frames();
      dialog.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]').click();
      await f.waitIdle();await frames();
      equal(f.requests.length,requestCount,"confirming the unchanged structure should not write native parameters");
      equal(f.state.tubeDesigner.product.parameters,saved,"confirming the unchanged structure overwrote a concrete choice or draft");
      require(!q("[data-tube-designer-structure-dialog]"),"unchanged structure confirmation did not close the dialog");
    }
    await commit(structureButton("doorFrameStructure"),"folded");
    require(option("doorFrameConnection")===initial.fixedChoice,"folding fixed frame replaced its concrete connection selector");
    require([...initial.fixedChoice.options].every(item=>!["miter","butt","insert"].includes(item.value)),
      "folded fixed frame kept joined-only connection choices");
    await commit(initial.fixedChoice,"rounded-v-groove");
    equal(roleNumbers("doorFrame"),fixedNumbers,"fixed popup overwrote its inactive numeric tool drafts");
    equal(roleNumbers("doorLeafFrame"),leafNumbers,"fixed popup affected leaf numeric tool drafts");
    equal(choiceValue("doorFrameConnection"),"rounded-v-groove","fixed summary does not recognize the confirmed rounded choice");
    equal(choiceValue("doorLeafFrameConnection"),"butt","fixed popup changed leaf's confirmed connection");
    require(!fixedWrap.isConnected&&!field("doorFrameButtWrapMode"),"folded fixed frame kept butt-only wrap visible");
    require(field("doorLeafFrameButtWrapMode")===leafWrap,"fixed folding choice changed leaf wrap");
    await confirmUnchangedStructure("doorFrameStructure");
    equal(choiceValue("doorFrameConnection"),"rounded-v-groove","confirming the current folded structure overwrote its concrete rounded choice");
    equal(roleNumbers("doorFrame"),fixedNumbers,"confirming unchanged fixed structure overwrote its numeric tool drafts");
    await confirmUnchangedStructure("doorLeafFrameStructure");
    equal(choiceValue("doorLeafFrameConnection"),"butt","confirming the current joined structure overwrote its concrete butt choice");
    require(field("doorLeafFrameButtWrapMode")===leafWrap,"confirming unchanged leaf structure replaced its current concrete input node");
    results.push({case:"butt-wrap-part-grouping",fixedAndLeafInOwnPart:true,conditionalFieldsIndependent:true,
      sameStructureKeepsConcreteChoices:true,wrapNodeGroupCanvasAndFormPreserved:true,latestCaretAndScrollPreserved:true});
    const structure=option("outerFrameStructure");
    require(structure&&!option("outerFrameConnection")&&!option("outerFrameFoldLayout"),"continuous outer frame should show structure then bend type");
    require(!q('[data-product-control-editor="outerFrameStructure"] select'), "right pane must show current structure and a change button");
    const canceledParameters = structuredClone(f.state.tubeDesigner.product.parameters), canceledRequests = f.requests.length;
    structure.click(); await frames();
    const canceledSelect=q('[data-tube-designer-structure-dialog] select');
    require(canceledSelect,"structure popup did not open");
    canceledSelect.value="joined";canceledSelect.dispatchEvent(new Event("change",{bubbles:true}));
    setScrolls([183,253,193,343]);
    q('[data-tube-designer-structure-dialog] [data-cam-action="tube-designer-cancel-product-structure"]').click();await frames();
    require(!q("[data-tube-designer-structure-dialog]")&&option("outerFrameStructure")===structure,"cancel replaced structure button or left its dialog");
    require(document.activeElement===structure,"cancel did not return focus to its stable opener");
    equal(scrolls(),[183,253,193,343],"cancel restored stale sidebar scrolling");
    equal(f.state.tubeDesigner.product.parameters,canceledParameters,"cancel changed persisted parameters");
    equal(f.requests.length,canceledRequests,"cancel submitted a structure update");
    const draftBefore=structuredClone(f.state.tubeDesigner.product.parameters);
    await commit(structure,"spatial");
    equal(f.state.tubeDesigner.product.parameters.frameManufacturingMode,"spatial_v_notch","structure selection failed");
    require(option("outerFrameStructure")===structure,"structure selector node was replaced");
    await commit(option("outerFrameBendType"),"edge-arc");
    equal(f.state.tubeDesigner.product.parameters.frameManufacturingMode,"spatial_v_notch","bend selection forced spatial into plane");
    const spatialK=field("outerFrameBendKFactor");
    require(spatialK,"spatial edge K missing");
    await commit(option("outerFrameBendType"),"rounded-v-groove",()=>{spatialK.focus({preventScroll:true});setScrolls([193,263,203,353]);},
      ()=>{if(spatialK.isConnected)spatialK.focus({preventScroll:true});setScrolls([209,279,219,369]);});
    equal(f.state.tubeDesigner.product.parameters.frameManufacturingMode,"spatial_v_notch","rounded V forced spatial into plane");
    require(!spatialK.isConnected&&document.activeElement!==field("doorFrameBendKFactor"),"hidden spatial K borrowed another role focus");
    const spatialRadius=field("outerFrameVGrooveRoundRadius");
    require(spatialRadius&&spatialRadius.value==="3.4","spatial rounded V draft was not preserved");
    await commit(structure,"joined",()=>{spatialRadius.focus({preventScroll:true});setScrolls([223,293,233,383]);},
      ()=>{if(spatialRadius.isConnected)spatialRadius.focus({preventScroll:true});setScrolls([239,309,249,399]);});
    require(!option("outerFrameBendType")&&!spatialRadius.isConnected&&option("outerFrameConnection"),
      "joined structure must show only the actual splice choice");
    checkOuterOrder("outerFrameConnection");
    require(!document.activeElement.matches("input,select,textarea"),"removed bend radius lent focus to a splice field");
    equal(scrolls(),[239,309,249,399],"structure switch restored stale nested scroll");
    if (choiceValue("outerFrameConnection") === "butt") await commit(option("outerFrameConnection"),"miter");
    await commit(option("outerFrameConnection"),"butt");
    equal(f.state.tubeDesigner.product.parameters.frameManufacturingMode,"segment_weld","splice choice changed structure mode");
    const wraps=field("frameButtWrapMode");
    if(wraps)require(wraps.closest("details")===initial.outerGroup,"outer splice wrap wrong part");
    const code2=initial.code;
    await commit(structure,"plane-u",()=>{code2.focus({preventScroll:true});code2.value="STRUCTURE-LATEST-DRAFT";code2.setSelectionRange(2,9,"backward");setScrolls([253,323,263,413]);},
      ()=>{code2.setSelectionRange(3,12,"forward");setScrolls([269,339,279,429]);});
    require(document.activeElement===code2&&code2.value==="STRUCTURE-LATEST-DRAFT","structure reply overwrote later text");
    equal([code2.selectionStart,code2.selectionEnd,code2.selectionDirection],[3,12,"forward"],"structure latest caret");
    equal(scrolls(),[269,339,279,429],"structure latest four scrolling positions");
    equal(f.state.tubeDesigner.product.parameters.frameManufacturingMode,"plane_v_notch","U structure should select plane mode");
    equal(choiceValue("outerFrameBendType"),"rounded-v-groove","structure change discarded groove draft");
    checkOuterOrder("outerFrameBendType");
    for(const key of ["outerFrameBendKFactor","outerFrameFoldBridge","outerFrameVGrooveRoundRadius","outerFrameVGrooveMaleFemale","outerFrameVGrooveBottomStrategy"])
      equal(f.state.tubeDesigner.product.parameters[key],draftBefore[key],"structure change overwrote retained fold draft "+key);
    require(initial.canvas===q("canvas")&&initial.form===q('[data-tube-designer-parameter-form]')&&initial.outerGroup===group("assembly_outer"),
      "two-step structure switching rebuilt the canvas/form/outer group");
    require(!f.requests.some(request=>/GenerateProduct|DisassembleSelected/.test(request.method)),"structure edits regenerated display");
    results.push({case:"outer-structure-then-bend-or-splice",spatialGroovesStaySpatial:true,roundedVDirectlySelectable:true,
      independentDraftsPreserved:true,latestFocusCaretScrollNodesPreserved:true,localSelectionAndCancelDoNotWrite:true,displayRegenerations:0});
    const beforeAdd=structuredClone(f.state.tubeDesigner.product.parameters), beforeAddCanvas=q("canvas"), beforeAddForm=initial.form;
    q('[data-action="ribbon-command"][data-command-id="designer.add"]').click();
    await wait(()=>q("[data-tube-designer-add-form]")&&!f.view.tubeDesignerTemplateSwitchPending);
    await f.waitIdle();await frames();
    const addForm=q("[data-tube-designer-add-form]"), addGroupKeys=["product-shape","outer-frame-structure","product-options"];
    const addGroups=addGroupKeys.map(key=>addForm.querySelector('[data-tube-designer-parameter-group="add:'+key+'"]'));
    require(addGroups.every(node=>node?.open),"add shape/structure/options groups must be expanded");
    require(addGroups[0].compareDocumentPosition(addGroups[1])&Node.DOCUMENT_POSITION_FOLLOWING
      &&addGroups[1].compareDocumentPosition(addGroups[2])&Node.DOCUMENT_POSITION_FOLLOWING,"actual add groups have wrong order");
    const addField=key=>addForm.querySelector('[data-tube-designer-parameter="'+key+'"]');
    const addControl=()=>addForm.querySelector('[data-product-control-editor="outerFrameStructure"] select');
    const face=addField("faceType"), addStructure=addControl();
    require(face&&addStructure,"add product is missing shape or structure");
    let addListenerCalls=0;face.addEventListener("structure-add-probe",()=>addListenerCalls++);addStructure.addEventListener("structure-add-probe",()=>addListenerCalls++);
    async function addChange(target,value) {
      target.focus({preventScroll:true});target.value=value;target.dispatchEvent(new Event("change",{bubbles:true}));
      await f.waitIdle();await frames();
    }
    await addChange(face,"three");
    require(face===addField("faceType")&&addStructure===addControl(),"changing product shape replaced add input nodes");
    require([...addStructure.options].some(option=>option.value==="spatial"),"three-face add dialog does not offer spatial structure");
    await addChange(addStructure,"spatial");
    equal(f.view.tubeDesignerAddDraft.frameManufacturingMode,"spatial_v_notch","add structure must update existing mode draft");
    require(!Object.hasOwn(f.view.tubeDesignerAddDraft,"outerFrameStructure"),"add persisted a virtual control key");
    // Materialize the declared default tool through ordinary form editing
    // before asserting that an existing saved resource draft stays unchanged.
    const pattern=addField("infillPattern"), patternValue=pattern.value;
    const alternatePattern=[...pattern.options].find(option=>option.value!==patternValue&&!option.disabled)?.value;
    require(alternatePattern,"add fixture needs another actual infill choice");
    await addChange(pattern,alternatePattern);await addChange(pattern,patternValue);
    require(f.view.tubeDesignerAddDraft.tubeDesignerToolBindings?.outerFrameGroove,"default tool binding was not materialized before draft preservation test");
    const threeDraft=structuredClone(f.view.tubeDesignerAddDraft), addDraftRequests=f.requests.filter(request=>request.method==="TubeDesigner.UpdateProductParameters").length;
    await addChange(face,"single");
    require(![...addStructure.options].some(option=>option.value==="spatial"),"single-face add dialog retained spatial choice");
    equal(f.view.tubeDesignerAddDraft.frameManufacturingMode,"spatial_v_notch","shape choice prematurely rewrote inactive structure draft");
    await addChange(face,"three");
    equal(addStructure.value,"spatial","returning to three-face did not restore structure choice");
    equal(f.view.tubeDesignerAddDraft,threeDraft,"add shape round trip discarded parameter/tool drafts");
    require(document.activeElement===face&&face===addField("faceType")&&addStructure===addControl(),"add local refresh lost focus or nodes");
    face.dispatchEvent(new Event("structure-add-probe"));addStructure.dispatchEvent(new Event("structure-add-probe"));equal(addListenerCalls,2,"add listeners replaced or duplicated");
    equal(f.requests.filter(request=>request.method==="TubeDesigner.UpdateProductParameters").length,addDraftRequests,"add draft wrote existing product");
    q('[data-tube-designer-add-dialog] [data-cam-action="tube-designer-cancel-add"]').click();await frames();
    require(!q("[data-tube-designer-add-dialog]")&&q("canvas")===beforeAddCanvas&&q('[data-tube-designer-parameter-form]')===beforeAddForm,
      "add draft or cancel rebuilt the scene canvas/current product form");
    equal(f.state.tubeDesigner.product.parameters,beforeAdd,"add draft modified the current product");
    results.push({case:"add-shape-before-outer-structure",actualGroupsInOrder:true,shapeConditionalChoices:true,
      virtualKeyNotPersisted:true,inactiveDraftAndFocusNodesListenersRetained:true,currentProductUnchanged:true});
    const previousProductId=f.state.tubeDesigner.product.entityId, otherProductId=f.state.tubeDesigner.instances[1].entityId;
    option("outerFrameStructure").click();await frames();require(q("[data-tube-designer-structure-dialog]"),"scope test structure popup missing");
    q('[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="'+otherProductId+'"]').click();
    await f.waitIdle();await frames();
    equal(f.state.tubeDesigner.product.entityId,otherProductId,"scope test did not activate another real product");
    require(!q("[data-tube-designer-structure-dialog]")&&!f.view.tubeDesignerStructureDialog,"navigation retained a stale structure dialog");
    q('[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="'+previousProductId+'"]').click();
    await f.waitIdle();await frames();
    equal(f.state.tubeDesigner.product.entityId,previousProductId,"scope test did not return to the original product");
    require(!q("[data-tube-designer-structure-dialog]")&&!f.view.tubeDesignerStructureDialog,"returning to original product revived a phantom dialog");
    results.push({case:"structure-dialog-instance-scope",switchingInstanceClosesDialog:true,returnDoesNotReviveDialog:true});
    // Open real product instances through the production action. Each host
    // snapshot deliberately retains a weld draft while its groove changes;
    // shared parameterConditions must decide the offered/effective choices.
    const { productControlEffectiveValues } = await import("/src/apps/tube-designer/webpage/productControls.mjs");
    const postOriginalId=f.state.tubeDesigner.product.entityId;
    const postBase=structuredClone(f.state.tubeDesigner.product.parameters);
    const postCases=[];
    for(const face of ["two","three"])
      for(const mode of ["plane_v_notch","spatial_v_notch"])
        for(const bend of ["edge-arc","v-groove","rounded-v-groove"])
          postCases.push({face,mode,bend,post:"weld",visible:true,weld:bend!=="edge-arc"});
    for(const face of ["two","three"])postCases.push({face,mode:"segment_weld",bend:"edge-arc",post:"weld",visible:true,weld:true});
    for(const bend of ["edge-arc","v-groove","rounded-v-groove"])
      postCases.push({face:"five",mode:"plane_v_notch",bend,post:"weld",visible:true,weld:bend!=="edge-arc"});
    for(const bend of ["edge-arc","v-groove","rounded-v-groove"])
      postCases.push({face:"five",mode:"spatial_v_notch",bend,post:"tabs",visible:false,weld:bend!=="edge-arc"});
    const postCoverage=[];
    for(const [index,scenario] of postCases.entries()) {
      const instance=f.state.tubeDesigner.instances[index+2];
      const values={...structuredClone(postBase),faceType:scenario.face,frameManufacturingMode:scenario.mode,
        foldedPostJoint:scenario.post,foldedPostTabWidth:17.3,foldedPostTabLength:14.6,foldedPostSideClearance:.24};
      const groove=scenario.bend==="edge-arc"?"system:edge-arc-groove":"system:v-notch-sharp";
      values.outerFrameGrooveTool=groove;
      values.outerFrameVGrooveBottomStrategy=scenario.bend==="rounded-v-groove"?"rounded":"sharp";
      const { makeProductToolBinding }=await import("/src/apps/tube-designer/webpage/productResourceBindings.mjs");
      const toolField=f.state.tubeDesigner.templates[0].parameters.find(item=>item.key==="outerFrameGrooveTool");
      const binding=makeProductToolBinding(toolField,f.view.tubeDesignerSystemPunchTools.find(tool=>"system:"+tool.id===groove),null,f.state.tubeDesigner.templates[0]);
      values.tubeDesignerToolBindings={...values.tubeDesignerToolBindings,[binding.role]:binding};
      instance.parameters=values;
      q('[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="'+instance.entityId+'"]').click();
      await f.waitIdle();await frames();
      equal(f.state.tubeDesigner.product.entityId,instance.entityId,"post-choice matrix did not activate the real product instance");
      equal(f.state.tubeDesigner.product.parameters,values,"rendering post-choice availability changed the native host draft");
      const post=field("foldedPostJoint");
      equal(Boolean(post),scenario.visible,"physical post selector visibility differs from actual frame route");
      if(post) {
        equal([...post.options].some(item=>item.value==="weld"&&!item.disabled),scenario.weld,"post weld availability differs from supported groove");
        equal(post.value,scenario.weld?"weld":"tabs","unavailable weld draft must show the declared tabs fallback");
        require([...post.options].some(item=>item.value==="tabs"&&!item.disabled),"valid tabs choice was removed");
      } else {
        equal(productControlEffectiveValues(f.state.tubeDesigner.templates[0],values).frameManufacturingMode,"plane_v_notch","five-face raw spatial draft must recognize effective plane structure");
        for(const key of ["foldedPostJoint","foldedPostTabWidth","foldedPostTabLength","foldedPostSideClearance"])
          require(!field(key),"five-face retained spatial post draft leaked a hidden field: "+key);
      }
      require(q("canvas")===initial.canvas,"post-choice product activation replaced the main scene canvas");
      if(scenario.face==="three"&&scenario.mode==="plane_v_notch"&&scenario.bend==="edge-arc") {
        const stablePost=post,k=field("outerFrameBendKFactor"),caseCode=field("productCode"),caseForm=q('[data-tube-designer-parameter-form]');
        await commit(k,"0.53",()=>{caseCode.focus({preventScroll:true});caseCode.setSelectionRange(1,7,"backward");setScrolls([151,221,161,311]);},
          ()=>{caseCode.setSelectionRange(2,9,"forward");setScrolls([167,237,177,327]);});
        require(field("foldedPostJoint")===stablePost&&field("productCode")===caseCode&&document.activeElement===caseCode,"post fallback response replaced its selector or stole later focus");
        equal([caseCode.selectionStart,caseCode.selectionEnd,caseCode.selectionDirection],[2,9,"forward"],"post fallback latest caret");
        equal(scrolls(),[167,237,177,327],"post fallback latest four scrolling positions");
        require(q("canvas")===initial.canvas&&caseForm===q('[data-tube-designer-parameter-form]'),"post fallback EC acknowledgement rebuilt product surface");
      }
      postCoverage.push(scenario);
    }
    q('[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="'+postOriginalId+'"]').click();
    await f.waitIdle();await frames();
    equal(f.state.tubeDesigner.product.entityId,postOriginalId,"post matrix did not restore the original product");
    equal(f.state.tubeDesigner.product.parameters,postBase,"post matrix changed another product's independent drafts");
    results.push({case:"post-weld-groove-applicability",scenarios:postCoverage,edgeFoldWeldUnavailable:true,
      sharpAndRoundedVWeldAvailable:true,segmentWeldAvailable:true,fiveRawSpatialPostDraftsHiddenAndUnchanged:true,
      sharedConditionFallback:true,latestFocusCaretFourScrollAndCanvasNodesPreserved:true,nativeGeometryVerifiedSeparately:true});
    // Exercise Confirm's real flattened SDO request. An inapplicable spatial
    // draft survives shape changes, but only an applicable physical mode may
    // cross the creation boundary. No production action is substituted here.
    const roleDraftKeys=["outerFrame","doorFrame","doorLeafFrame"].flatMap(prefix=>
      ["GrooveTool","VGrooveBottomStrategy","VGrooveRoundRadius","BendKFactor","FoldBridge","VGrooveMaleFemale"]
        .map(suffix=>prefix+suffix));
    const creationCases=[{face:"single",returnToThree:false,expected:"plane_v_notch"},
      {face:"five",returnToThree:false,expected:"plane_v_notch"},
      {face:"single",returnToThree:true,expected:"spatial_v_notch"}];
    for(const scenario of creationCases) {
      q('[data-action="ribbon-command"][data-command-id="designer.add"]').click();
      await wait(()=>q("[data-tube-designer-add-form]")&&!f.view.tubeDesignerTemplateSwitchPending);
      await f.waitIdle();await frames();
      const form=q("[data-tube-designer-add-form]");
      const shape=form.querySelector('[data-tube-designer-parameter="faceType"]');
      const structure=form.querySelector('[data-product-control-editor="outerFrameStructure"] select');
      await addChange(shape,"three");await addChange(structure,"spatial");
      const fixedStructure=form.querySelector('[data-product-control-editor="doorFrameStructure"] select'),
        leafStructure=form.querySelector('[data-product-control-editor="doorLeafFrameStructure"] select');
      require(fixedStructure&&leafStructure,"creation must include escape-frame and leaf-frame main structures");
      await addChange(fixedStructure,"folded");await addChange(leafStructure,"joined");
      for(const key of ["outerFrameBendType","outerFrameConnection","doorFrameConnection","doorLeafFrameConnection"])
        require(!form.querySelector('[data-product-control-editor="'+key+'"]'),"creation exposes a concrete connection or bend type: "+key);
      for(const select of [structure,fixedStructure,leafStructure])
        require(![...select.options].some(item=>["edge-arc","v-groove","rounded-v-groove","butt","miter","insert"].includes(item.value)),
          "creation main structure selector exposes a specific machining choice");
      for(const prefix of ["outerFrame","doorFrame","doorLeafFrame"])
        for(const suffix of ["BendKFactor","FoldBridge","VGrooveRoundRadius","VGrooveMaleFemale"])
          require(!form.querySelector('[data-tube-designer-parameter="'+prefix+suffix+'"]'),"creation exposed detailed machining numbers: "+prefix+suffix);
      const infill=form.querySelector('[data-tube-designer-parameter="infillPattern"]'), infillValue=infill.value;
      await addChange(infill,[...infill.options].find(choice=>choice.value!==infillValue&&!choice.disabled).value);
      await addChange(infill,infillValue);
      const original=structuredClone(f.view.tubeDesignerAddDraft);
      await addChange(shape,scenario.face);
      equal(f.view.tubeDesignerAddDraft.frameManufacturingMode,"spatial_v_notch","inactive creation draft changed before confirm");
      if(scenario.returnToThree)await addChange(shape,"three");
      if(scenario.returnToThree)equal(structure.value,"spatial","creation roundtrip did not restore saved spatial structure");
      const beforeRequest=f.requests.length;
      q('[data-tube-designer-add-dialog] [data-cam-action="tube-designer-confirm-add"]').click();
      await wait(()=>f.requests.slice(beforeRequest).some(request=>request.method==="TubeDesigner.GeneratePreview"));
      const request=f.requests.slice(beforeRequest).find(request=>request.method==="TubeDesigner.GeneratePreview");
      equal(request.payload.frameManufacturingMode,scenario.expected,"Confirm sent an inapplicable physical manufacturing mode");
      equal(request.payload.faceType,scenario.returnToThree?"three":scenario.face,"Confirm sent another product shape");
      for(const key of roleDraftKeys)equal(request.payload[key],original[key],"Confirm overwrote an independent tool draft: "+key);
      equal(request.payload.tubeDesignerToolBindings,original.tubeDesignerToolBindings,"Confirm overwrote frozen tool drafts");
      equal(request.payload.doorFrameJoinType,"v_groove_90:tool_library","creation did not persist escape-frame folded structure");
      equal(request.payload.doorLeafFrameJoinType,"miter_45","creation did not persist leaf-frame joined structure");
      equal(productControlValue(f.state.tubeDesigner.templates[0],"doorFrameStructure",request.payload),"folded","wire fixed-frame structure not recognized");
      equal(productControlValue(f.state.tubeDesigner.templates[0],"doorLeafFrameStructure",request.payload),"joined","wire leaf-frame structure not recognized");
      require(!["outerFrameStructure","outerFrameBendType","outerFrameConnection","outerFrameFoldLayout","doorFrameStructure","doorLeafFrameStructure","doorFrameConnection","doorLeafFrameConnection"].some(key=>Object.hasOwn(request.payload,key)),
        "Confirm leaked a virtual product control into the native wire request");
      await f.waitIdle();await frames();
      require(!q("[data-tube-designer-add-dialog]"),"successful real creation receipt did not close the add dialog");
      require(f.state.tubeDesigner.product.parameters.frameManufacturingMode===scenario.expected,"created product does not own the wire mode");
      require(!q('[data-tube-designer-parameter-form] [data-product-control-editor="outerFrameStructure"] select'),
        "created product exposes an inline structure selector instead of the summary");
    }
    results.push({case:"creation-native-wire-effective-mode",singleAndFiveUsePlane:true,threeRoundtripUsesSpatial:true,
      fixedAndLeafMainStructuresPersisted:true,allCreateControlsOnlyMainStructure:true,concreteChoicesAndNumbersStayInRightPane:true,
      independentToolDraftsPreserved:true,syntheticKeysNotSent:true,realViewportReceipts:true});

    // A pending structure write must remain bound to its original scene even
    // after AppShell mounts another project. Check the new project's actual
    // input node, later selection and all four nested scroll containers.
    const pendingProductId=f.state.tubeDesigner.product.entityId;
    option("outerFrameStructure").click();await frames();
    const popup=q("[data-tube-designer-structure-dialog]");
    popup.querySelector("select").value="joined";
    popup.querySelector("select").dispatchEvent(new Event("change",{bubbles:true}));await frames();
    popup.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]').click();
    await wait(()=>f.waitingUpdate);
    const pendingCommit=f.view.tubeDesignerProductParameterCommitPromise;
    const pendingRequest=f.requests.filter(request=>request.method==="TubeDesigner.UpdateProductParameters").at(-1);
    equal(pendingRequest.payload.productEntityId,pendingProductId,"structure request used another product before navigation");
    const tab=q('[data-action="select-open-project"][data-project-id="'+f.otherProjectId+'"]');
    require(tab&&!tab.disabled,"actual second-project tab unavailable while an EC write is pending");tab.click();
    await wait(()=>globalThis.__icaxAppShell.getState().activeProjectId===f.otherProjectId&&q('[data-tube-designer-parameter="productCode"]'));
    await frames();
    const otherCode=field("productCode"), otherForm=q('[data-tube-designer-parameter-form]'), otherCanvas=q("canvas");
    require(otherCode&&otherForm&&otherCanvas,"new project must mount the actual parameter editor and viewport");
    otherCode.focus({preventScroll:true});otherCode.value="OTHER-PROJECT-LATEST-DRAFT";otherCode.setSelectionRange(2,12,"backward");
    setScrolls([33,63,43,153]);
    f.releaseUpdate();await pendingCommit;await frames();
    require(document.activeElement===otherCode&&field("productCode")===otherCode&&otherCode.value==="OTHER-PROJECT-LATEST-DRAFT",
      "late original-project structure response stole new-project focus/draft/node");
    equal([otherCode.selectionStart,otherCode.selectionEnd,otherCode.selectionDirection],[2,12,"backward"],"late response changed new-project selection");
    equal(scrolls(),[33,63,43,153],"late response changed new-project nested scrolling");
    require(q('[data-tube-designer-parameter-form]')===otherForm&&q("canvas")===otherCanvas,"late response rebuilt new-project form or canvas");
    require(!q("[data-tube-designer-structure-dialog]")&&!f.view.tubeDesignerStructureDialog&&!f.otherView.tubeDesignerStructureDialog,
      "late response resurrected a structure popup after project navigation");
    equal(pendingRequest.transportProjectId,f.projectId,"structure write was redirected to another scene transport");
    require(!f.requests.some(request=>request.method==="TubeDesigner.UpdateProductParameters"&&request.transportProjectId===f.otherProjectId),
      "original-project EC write crossed into the new project's native proxy");
    const returnTab=q('[data-action="select-open-project"][data-project-id="'+f.projectId+'"]');returnTab.click();
    await wait(()=>globalThis.__icaxAppShell.getState().activeProjectId===f.projectId&&option("outerFrameStructure"));await frames();
    require(!q("[data-tube-designer-structure-dialog]")&&!f.view.tubeDesignerStructureDialog,"returning to original project revived the closed structure popup");
    equal(f.requests.filter(request=>request.method==="TubeDesigner.GeneratePreview").length,creationCases.length,
      "a structure-only EC update unexpectedly regenerated the display model");
    results.push({case:"structure-pending-project-navigation",originalSceneOwnsWrite:true,newProjectFocusSelectionScrollNodesRetained:true,
      popupDoesNotResurrect:true,structureDisplayRegenerations:0});

    // Exercise real AppShell Save/keyboard Open rather than a copied save routine.
    // Only native persistence transport is deferred; the UI and EC pipeline are production.
    const savedNodes={surface:q('[data-product-surface="project"]'),form:q('[data-tube-designer-parameter-form]'),canvas:q("canvas"),
      code:field("productCode"),left:q(".tube-designer-instance-list"),right:q(".tube-designer-parameter-scroll")};
    let saveProbes=0;Object.values(savedNodes).forEach(node=>node.addEventListener("save-node-probe",()=>saveProbes++));
    const checkSaveNodes=()=>{for(const [key,node] of Object.entries(savedNodes))require(node.isConnected&&({surface:q('[data-product-surface="project"]'),form:q('[data-tube-designer-parameter-form]'),canvas:q("canvas"),code:field("productCode"),left:q(".tube-designer-instance-list"),right:q(".tube-designer-parameter-scroll")})[key]===node,"save replaced "+key);};
    const shortcut=(key,shiftKey=false)=>savedNodes.code.dispatchEvent(new KeyboardEvent("keydown",{key,ctrlKey:true,shiftKey,bubbles:true,cancelable:true}));
    const saveDone=()=>q('[data-command-id="app.save"]')?.textContent.trim()==="保存"&&!f.waitingSave;
    const cameraBefore=JSON.stringify({position:f.view.viewport.camera.position.toArray(),quaternion:f.view.viewport.camera.quaternion.toArray()});
    savedNodes.code.value="SAVE-FIRST-DRAFT";savedNodes.code.dispatchEvent(new Event("input",{bubbles:true}));
    savedNodes.code.dispatchEvent(new Event("change",{bubbles:true}));await wait(()=>f.waitingUpdate);
    f.holdNextState();savedNodes.code.focus({preventScroll:true});shortcut("s");await frames();
    equal(f.saveRequests.length,0,"save ran before pending EC submission");checkSaveNodes();
    savedNodes.code.value="SAVE-LATEST-DRAFT";savedNodes.code.dispatchEvent(new Event("input",{bubbles:true}));
    savedNodes.code.setSelectionRange(2,10,"backward");setScrolls([41,81,51,181]);f.releaseUpdate();
    await wait(()=>f.waitingState);setScrolls([57,97,67,197]);f.releaseState();
    await wait(()=>f.waitingUpdate);
    equal(f.requests.filter(request=>request.method==="TubeDesigner.UpdateProductParameters").at(-1).payload.parameters.productCode,"SAVE-LATEST-DRAFT","save did not commit the later live edit");
    f.releaseUpdate();await wait(()=>f.waitingSave);checkSaveNodes();
    savedNodes.code.focus({preventScroll:true});savedNodes.code.setSelectionRange(3,11,"backward");setScrolls([71,111,81,211]);
    f.releaseSave();await wait(saveDone);await frames();checkSaveNodes();
    require(document.activeElement===savedNodes.code,"save completion lost field focus");
    equal([savedNodes.code.selectionStart,savedNodes.code.selectionEnd,savedNodes.code.selectionDirection],[3,11,"backward"],"save completion lost latest caret");
    equal(scrolls(),[71,111,81,211],"save completion lost latest nested scrolling");
    equal(f.savedScene.tubeDesigner.product.parameters.productCode,"SAVE-LATEST-DRAFT","saved native payload has old product parameters");
    equal(JSON.stringify({position:f.view.viewport.camera.position.toArray(),quaternion:f.view.viewport.camera.quaternion.toArray()}),cameraBefore,"save changed scene camera");
    Object.values(savedNodes).forEach(node=>node.dispatchEvent(new Event("save-node-probe")));equal(saveProbes,Object.keys(savedNodes).length,"save lost node listeners");
    require(!q(".global-progress-backdrop"),"save unnecessarily blocks the entire product page");
    results.push({case:"save-waits-latest-product-draft",pendingEcBeforeSave:true,laterLiveEditCommitted:true,saveDoesNotRemount:true,
      latestFocusCaretFourScrolls:true,sameCanvasCameraListeners:true,noDisplayGeneration:true});

    f.failNextSave();shortcut("s");await wait(()=>f.waitingSave);
    savedNodes.code.focus({preventScroll:true});savedNodes.code.setSelectionRange(1,9,"backward");setScrolls([85,125,95,225]);
    f.releaseSave();await wait(saveDone);await frames();checkSaveNodes();
    require(document.activeElement===savedNodes.code,"failed save lost current input focus");
    equal(scrolls(),[85,125,95,225],"failed save lost nested scrolling");
    require(q(".log-list").textContent.includes("Fixture save failed"),"failed save did not show its error");
    shortcut("s");await wait(()=>f.waitingSave);f.releaseSave();await wait(saveDone);checkSaveNodes();
    results.push({case:"failed-save-can-retry",saveControlsRecover:true,errorReported:true,editorCanvasListenersAndScrollPreserved:true});

    const beforeRejectedFlush=f.saveRequests.length;
    savedNodes.code.value="SAVE-RETRY-DRAFT";savedNodes.code.dispatchEvent(new Event("input",{bubbles:true}));
    f.failNextUpdate();shortcut("s");await wait(()=>f.waitingUpdate);
    savedNodes.code.focus({preventScroll:true});savedNodes.code.setSelectionRange(2,8,"backward");setScrolls([91,131,101,231]);
    f.releaseUpdate();await wait(saveDone);await frames();checkSaveNodes();
    equal(f.saveRequests.length,beforeRejectedFlush,"native save must not start after a rejected live parameter submission");
    equal(field("productCode").value,"SAVE-RETRY-DRAFT","rejected save flush discarded the live draft");
    require(document.activeElement===savedNodes.code,"rejected flush lost input focus");
    equal([savedNodes.code.selectionStart,savedNodes.code.selectionEnd,savedNodes.code.selectionDirection],[2,8,"backward"],"rejected flush lost latest caret");
    equal(scrolls(),[91,131,101,231],"rejected flush lost nested scrolls");
    shortcut("s");await wait(()=>f.waitingUpdate);f.releaseUpdate();await wait(()=>f.waitingSave);f.releaseSave();await wait(saveDone);checkSaveNodes();
    equal(f.savedScene.tubeDesigner.product.parameters.productCode,"SAVE-RETRY-DRAFT","retry failed to persist retained live draft");
    results.push({case:"save-rejected-parameter-flush",noStaleNativeSave:true,liveDraftNodesFocusCaretScrollRetained:true,retryCommitsAndSavesLatest:true});

    const beforeCancel=f.saveRequests.length,beforeDialog=f.fileRequests.length;
    f.cancelNextSaveDialog();shortcut("s",true);
    await wait(()=>f.fileRequests.length>beforeDialog&&saveDone());await frames();checkSaveNodes();
    equal(f.saveRequests.length,beforeCancel,"cancelled Save As wrote a project");
    require(document.activeElement===savedNodes.code,"cancelled Save As lost input focus");
    results.push({case:"save-as-cancel",noNativeWrite:true,saveControlsAndProductNodesRetained:true});

    const beforeLateSave=f.saveRequests.length;shortcut("s");await wait(()=>f.waitingSave);
    q('[data-action="select-open-project"][data-project-id="'+f.otherProjectId+'"]').click();
    await wait(()=>globalThis.__icaxAppShell.getState().activeProjectId===f.otherProjectId&&field("productCode"));await frames();
    const saveOtherCode=field("productCode"),saveOtherCanvas=q("canvas"),saveOtherForm=q('[data-tube-designer-parameter-form]');
    saveOtherCode.focus({preventScroll:true});saveOtherCode.value="SAVE-OTHER-PROJECT";saveOtherCode.setSelectionRange(2,10,"backward");setScrolls([39,69,49,159]);
    f.releaseSave();await wait(()=>f.saveRequests.length===beforeLateSave+1&&q('[data-command-id="app.save"]').textContent.trim()==="保存");await frames();
    equal(globalThis.__icaxAppShell.getState().activeProjectId,f.otherProjectId,"old save reactivated its source project");
    require(document.activeElement===saveOtherCode&&field("productCode")===saveOtherCode&&q("canvas")===saveOtherCanvas&&q('[data-tube-designer-parameter-form]')===saveOtherForm,"late save remounted or refocused another project");
    equal([saveOtherCode.selectionStart,saveOtherCode.selectionEnd,saveOtherCode.selectionDirection],[2,10,"backward"],"late save stole another project's caret");
    equal(scrolls(),[39,69,49,159],"late save stole another project's nested scrolls");
    results.push({case:"save-completion-project-scope",sourceProjectOnly:true,newProjectNodesFocusCaretScrollPreserved:true});

    q('[data-action="select-open-project"][data-project-id="'+f.projectId+'"]').click();
    await wait(()=>globalThis.__icaxAppShell.getState().activeProjectId===f.projectId&&field("productCode"));await frames();
    f.failNextSave();field("productCode").dispatchEvent(new KeyboardEvent("keydown",{key:"s",ctrlKey:true,bubbles:true,cancelable:true}));await wait(()=>f.waitingSave);
    q('[data-action="select-open-project"][data-project-id="'+f.otherProjectId+'"]').click();
    await wait(()=>globalThis.__icaxAppShell.getState().activeProjectId===f.otherProjectId&&field("productCode"));await frames();
    const failureOtherCode=field("productCode"),failureOtherCanvas=q("canvas"),failureOtherForm=q('[data-tube-designer-parameter-form]');
    failureOtherCode.focus({preventScroll:true});failureOtherCode.value="LATE-FAILURE-OTHER-DRAFT";failureOtherCode.setSelectionRange(3,11,"backward");setScrolls([47,77,57,167]);
    const otherErrorBefore=globalThis.__icaxAppShell.getState().error;
    f.releaseSave();await frames();await frames();
    equal(globalThis.__icaxAppShell.getState().error,otherErrorBefore,"old project's failed save replaced current-project error state");
    require(document.activeElement===failureOtherCode&&field("productCode")===failureOtherCode&&q("canvas")===failureOtherCanvas&&q('[data-tube-designer-parameter-form]')===failureOtherForm,"late failed save remounted or refocused another project");
    equal([failureOtherCode.selectionStart,failureOtherCode.selectionEnd,failureOtherCode.selectionDirection],[3,11,"backward"],"late failed save stole another project's caret");
    equal(scrolls(),[47,77,57,167],"late failed save stole another project's scrolling");
    results.push({case:"failed-save-project-scope",currentProjectErrorUnchanged:true,newProjectFocusCaretScrollNodesRetained:true});

    const savedData=structuredClone(f.savedScene.tubeDesigner);
    failureOtherCode.dispatchEvent(new KeyboardEvent("keydown",{key:"o",ctrlKey:true,bubbles:true,cancelable:true}));
    await wait(()=>globalThis.__icaxAppShell.getState().activeProjectId==="c40e92a4-90a7-4df5-8bfe-0c9d608a6413"&&field("productCode"));await frames();
    equal(f.reopenedView.scene.tubeDesigner,savedData,"freshly opened product UI did not use saved scene state");
    equal(field("productCode").value,"SAVE-RETRY-DRAFT","reopened actual product editor did not display saved parameters");
    equal(f.reopenedView.scene.tubeDesigner.instances.length,savedData.instances.length,"reopening lost product instances");
    equal(f.reopenedView.scene.tubeDesigner.product.parameters.tubeDesignerToolBindings,savedData.product.parameters.tubeDesignerToolBindings,"reopening changed independent tool drafts");
    require(f.fileRequests.some(request=>request.kind==="save")&&f.fileRequests.some(request=>request.kind==="open"),"real AppShell did not call project file dialogs");
    results.push({case:"project-save-reopen-production-route",actualSaveAndOpenCommands:true,freshProjectView:true,
      instancesCurrentProductPhysicalParametersAndToolSnapshotsRetained:true,nativeFilesystemOutsideBrowserFixture:true});
    return results;
  });
  assert.deepEqual(errors, []);
  // Keep the result image on the actual product choices after all interaction
  // assertions finish; the artificial overflow is only needed by those tests.
  await page.evaluate(() => {
    document.querySelectorAll("style").forEach(style => {
      if (style.textContent.includes(".cam-context-pane:after")) style.remove();
    });
    const materials = document.querySelector('[data-tube-designer-parameter-group="section:materials"]');
    if (materials) materials.open = false;
    document.querySelector(".cam-info-pane").scrollTop = 0;
    document.querySelector(".tube-designer-parameter-scroll").scrollTop = 0;
    document.querySelector('[data-tube-designer-parameter-group="scene:process:section-group:assembly_outer"]').scrollIntoView({block:"start"});
  });
  if(addStructureScreenshot) {
    await page.evaluate(async () => {
      const f=globalThis.__focusTransport;
      const frames=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
      document.querySelector('[data-action="ribbon-command"][data-command-id="designer.add"]').click();
      for(let count=0;(!document.querySelector("[data-tube-designer-add-form]")||f.view.tubeDesignerTemplateSwitchPending)&&count<300;count++)
        await new Promise(resolve=>setTimeout(resolve,10));
      await f.waitIdle();await frames();
      const form=document.querySelector("[data-tube-designer-add-form]");
      const face=form.querySelector('[data-tube-designer-parameter="faceType"]');
      face.value="three";face.dispatchEvent(new Event("change",{bubbles:true}));await f.waitIdle();await frames();
      const select=form.querySelector('[data-product-control-editor="outerFrameStructure"] select');
      select.value="spatial";select.dispatchEvent(new Event("change",{bubbles:true}));await f.waitIdle();await frames();
      for(const key of ["doorFrameStructure","doorLeafFrameStructure"]) {
        const choice=form.querySelector('[data-product-control-editor="'+key+'"] select');
        if(!choice)throw new Error("Create screenshot missing an applicable frame connection: "+key);
        choice.value=key==="doorLeafFrameStructure"?"joined":"folded";
        choice.dispatchEvent(new Event("change",{bubbles:true}));await f.waitIdle();await frames();
      }
      if(f.view.tubeDesignerAddDraft.faceType!=="three"||f.view.tubeDesignerAddDraft.frameManufacturingMode!=="spatial_v_notch")
        throw new Error("Add screenshot did not use the actual shape/structure draft flow");
      form.scrollTop=0;
    });
    await page.locator("[data-tube-designer-add-dialog]").screenshot({path:resolve(output,"add-product-structure.png")});
    await page.evaluate(()=>document.querySelector('[data-tube-designer-parameter-group="add:outer-frame-structure"]').scrollIntoView({block:"start"}));
    await page.locator("[data-tube-designer-add-dialog]").screenshot({path:resolve(output,"add-product-frame-connections.png")});
  } else if(outerStructureScreenshot) {
    await page.evaluate(async expectedStructure => {
      const q = key => document.querySelector('[data-tube-designer-parameter="' + key + '"]');
      const option = key => document.querySelector('[data-tube-designer-parameter-form] [data-product-control-editor="' + key + '"] select');
      const structure = document.querySelector('[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="outerFrameStructure"]');
      const group = document.querySelector('[data-tube-designer-parameter-group="scene:process:section-group:assembly_outer"]');
      const rows = [...group.querySelectorAll(".tube-designer-field")];
      const { productControlChoices, productControlValue } = await import("/src/apps/tube-designer/webpage/productControls.mjs");
      const state=globalThis.__focusTransport.state.tubeDesigner;
      const current=key=>productControlValue(state.templates[0],key,state.product.parameters);
      const label=key=>productControlChoices(state.templates[0],key,state.product.parameters).find(choice=>choice.value===current(key))?.label;
      const summary=document.querySelector('[data-product-control-editor="outerFrameStructure"] [data-tube-designer-current-structure]');
      if(current("outerFrameStructure") !== expectedStructure || summary?.textContent !== label("outerFrameStructure")
        || option("outerFrameBendType")?.value !== "rounded-v-groove" || !structure || option("outerFrameStructure") || !rows[0]?.contains(structure) || !rows[1]?.contains(option("outerFrameBendType"))
        || q("outerFrameVGrooveRoundRadius")?.value !== "2" || q("outerFrameFoldBridge")?.value !== "1"
        || q("foldedPostJoint")?.value !== "weld" || option("outerFrameConnection")
        || q("outerFrameVGrooveBottomStrategy") || q("outerFrameBendKFactor") || option("outerFrameFoldLayout"))
        throw new Error("Outer-frame screenshot does not show the actual two-step rounded-V controls in order");
      for(const key of ["doorFrameConnection","doorLeafFrameConnection"])
        if(current(key) !== "miter" || option(key)?.value !== "miter" || !document.querySelector('[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="'+key.replace("Connection","Structure")+'"]'))
          throw new Error("Screenshot fixed/leaf frame must show its main structure summary and right-side miter choice");
    }, outerStructureScreenshot);
    await page.locator(".cam-info-pane").screenshot({path:resolve(output,
      outerStructureScreenshot === "spatial" ? "outer-spatial-rounded-settings.png" : "outer-u-rounded-settings.png")});
    if(structurePopupScreenshot) {
      await page.locator('[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="outerFrameStructure"]').click();
      await page.locator("[data-tube-designer-structure-dialog]").screenshot({path:resolve(output,"change-product-structure.png")});
      for(const [key,file] of [["doorFrameStructure","change-fixed-frame-structure.png"],["doorLeafFrameStructure","change-leaf-frame-structure.png"]]) {
        await page.locator('[data-tube-designer-structure-dialog] [data-cam-action="tube-designer-cancel-product-structure"]').first().click();
        await page.locator('[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="'+key+'"]').click();
        await page.locator("[data-tube-designer-structure-dialog]").screenshot({path:resolve(output,file)});
      }
    }
  } else if(screenshotOnly) {
    await page.evaluate(async () => {
      const { productControlValue } = await import("/src/apps/tube-designer/webpage/productControls.mjs");
      const state=globalThis.__focusTransport.state.tubeDesigner;
      for(const prefix of ["outerFrame","doorFrame","doorLeafFrame"]) {
        const q = key => document.querySelector('[data-tube-designer-parameter="' + key + '"]');
        const key=prefix==="outerFrame"?"outerFrameBendType":prefix+"Connection", mainKey=prefix+"Structure";
        const button=document.querySelector('[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="'+mainKey+'"]');
        const option = document.querySelector('[data-tube-designer-parameter-form] [data-product-control-editor="' + key + '"] select');
        if(productControlValue(state.templates[0],key,state.product.parameters) !== "rounded-v-groove" || option?.value !== "rounded-v-groove" || !button || !q(prefix + "VGrooveRoundRadius") || !q(prefix + "FoldBridge")
          || !q(prefix + "VGrooveMaleFemale") || q(prefix + "VGrooveBottomStrategy") || q(prefix + "BendKFactor"))
          throw new Error("Rounded screenshot must show correct current product-owned fields: " + prefix);
      }
    });
    await page.locator(".cam-info-pane").screenshot({path:resolve(output,"three-rounded-frame-settings.png")});
  } else {
    await page.screenshot({path:resolve(output,"independent-fold-settings.png")});
    writeFileSync(resolve(output,"browser-report.json"),JSON.stringify({results,errors},null,2));
  }
  console.log(JSON.stringify(results));
} finally {
  await browser.close();
}
