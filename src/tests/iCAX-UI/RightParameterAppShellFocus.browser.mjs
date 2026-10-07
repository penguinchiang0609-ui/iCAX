// Exercise the actual AppShell -> product entry -> createWorkbench chain.
// Only the native proxy transport is adapted; no renderer or DOM patch is mocked.
import assert from "node:assert/strict";
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { browserAssetPath, browserAssetRoot, readBrowserAsset } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL("../../../", import.meta.url));
const source = browserAssetRoot, output = resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY || resolve(root, "output/tests/right-parameter-focus"));
mkdirSync(output, { recursive: true });
const descriptor = JSON.parse(readBrowserAsset("apps/tube-designer/templates/product/single_face_security_window/template.json"));
const baseline = process.argv.includes("--baseline");
const failureBaseline = process.argv.includes("--failure-baseline");
const specificationBaseline = process.argv.includes("--specification-baseline");
const excelBatch = process.argv.includes("--excel-batch");
const excelTemplate = process.argv.includes("--excel-template");
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || "msedge", headless: true });
try {
  const page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://right-focus.test/**", route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html><head><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (path === "/src/iCAX-UI/SDK/runtime.mjs") {
      browserAssetPath('iCAX-UI/SDK/runtime.mjs');
      return route.fulfill({ contentType: "text/javascript", body: "export async function connectApplication(){return globalThis.__focusTransport.appProxy;}" });
    }
    const file = resolve(source, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(source + sep)) return route.abort();
    try {
      let body = readFileSync(browserAssetPath(path.slice('/src/'.length)), "utf8");
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
  await page.evaluate(async ({ descriptor, failureBaseline, specificationBaseline, excelBatch }) => {
    const { getProjectView } = await import("/src/apps/_shared/workbench/state/projectViewStore.mjs");
    const { catalogText } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
    const template = { ...descriptor, available: true, descriptorLoaded: true, name: catalogText(descriptor.displayName),
      groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: descriptor.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const projectId = "c40e92a4-90a7-4df5-8bfe-0c9d608a6411";
    const sceneId = "0514ae0e-8e25-4926-a339-e4285ea9ba31";
    const parameters = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
    const instances = Array.from({ length: 24 }, (_, index) => ({ entityId: `c40e92a4-90a7-4df5-8bfe-${String(index + 1).padStart(12, "0")}`,
      name: `防盗窗 ${index + 1}`, templateId: template.id, quantity: 1, parameters: structuredClone(parameters), createdAt: `2026-10-04T00:${String(index).padStart(2, "0")}:00` }));
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false }, tubeDesigner: {
      templates: [template], instances, product: instances[0], activeProductId: instances[0].entityId, members: [], joints: [], parts: [], manufacturingGroups: [],
      specificationAnnotations: [{ id: 'focus-height', parameter: 'height', kind: 'spacing', start: [0, 0, 0], end: [0, 0, 1800],
        offset: [32, 0, 24], generatedValue: parameters.height }],
    } };
    const view = getProjectView(projectId);
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
      tubeDesignerParameterPanelProductId: instances[0].entityId,
      tubeDesignerRightDraftDirty: excelBatch,
      tubeDesignerParameterDisclosureState: { initialized: true },
      tubeDesignerExpandedParameterGroups: ["section:materials", "section:process", ...template.groups.map(group => `group:${group.key}`)] });
    const requests = [];
    let releaseUpdate, releaseState, holdState = false, nextUpdateFailure = null;
    const excelRows = [
      { sourceRow: 3, instanceName: 'Excel 甲', instanceQuantity: 2, parameters: { ...parameters, height: 1300 } },
      { sourceRow: 7, instanceName: 'Excel 乙', instanceQuantity: 4, parameters: { ...parameters, height: 1600 } },
      { sourceRow: 9, instanceName: 'Excel 丙', instanceQuantity: 1, parameters: { ...parameters, height: 1900 } },
    ];
    let generateCalls = 0, rejectSecondExcelRow = true;
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
        requests.push({ scope: 'scene', method, payload: structuredClone(payload),
          ...(method === 'TubeDesigner.GeneratePreview' ? { operation: structuredClone(view.tubeDesignerOperation) } : {}) });
        if (method === 'TubeDesigner.GeneratePreview') {
          generateCalls++;
          if (rejectSecondExcelRow && generateCalls === 2) throw new Error('Excel 第 7 行尺寸不合法');
          const instance = { entityId: `excel-instance-${generateCalls}`, templateId: template.id, name: payload.instanceName,
            quantity: payload.instanceQuantity, parameters: { ...parameters, ...payload }, createdAt: new Date().toISOString() };
          state.tubeDesigner.instances.push(instance);
          state.tubeDesigner.product = structuredClone(instance);
          state.tubeDesigner.activeProductId = instance.entityId;
          state.undoRedo.revision++;
          return { tubeDesigner: structuredClone(state.tubeDesigner) };
        }
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
        if (method === "TubeDesigner.ActivateProduct") {
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
        if (method === "TubeDesigner.List") return { tubeDesigner: structuredClone(state.tubeDesigner) };
        throw new Error("Unexpected native transport request: " + method);
      } };
    const projectState = { projectId, projectName: "焦点回归", mainScene: state };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const otherProjectId = 'c40e92a4-90a7-4df5-8bfe-0c9d608a6412';
    const otherState = { ...structuredClone(state), sceneId: '0514ae0e-8e25-4926-a339-e4285ea9ba32' };
    const otherView = getProjectView(otherProjectId);
    Object.assign(otherView, { scene: structuredClone(otherState), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
      tubeDesignerParameterPanelProductId: otherState.tubeDesigner.product.entityId,
      tubeDesignerParameterDisclosureState: { initialized: true },
      tubeDesignerExpandedParameterGroups: ["section:materials", "section:process", ...template.groups.map(group => `group:${group.key}`)] });
    const otherSceneProxy = { state: otherState, resources: sceneProxy.resources, pdo: { enabled: false },
      async getState() { return structuredClone(otherState); }, async invoke(method) { throw new Error('Unexpected second-project transport request ' + method); },
      views: { async start() { const snapshot = { viewId: 'other-focus-view', revision: '1', rows: [] };
        return { snapshot, async poll() { return snapshot; }, async stop() {} }; } } };
    const otherProjectState = { projectId: otherProjectId, projectName: '另一个项目', mainScene: otherState };
    const otherProjectProxy = { projectId: otherProjectId, state: otherProjectState, getMainScene() { return otherSceneProxy; } };
    const productState = { productId: "icax.tube-designer", productName: "TubeDesigner", isStarted: true,
      frontendEntry: "/src/apps/tube-designer/webpage/entry.mjs", catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ["icax"] } };
    const productProxy = { productId: productState.productId, state: productState, projects: new Map([[projectId, projectProxy], [otherProjectId, otherProjectProxy]]),
      async invoke(method, payload) {
        requests.push({ scope: 'product', method, payload: structuredClone(payload),
          ...(method === 'TubeDesigner.GeneratePreview' ? { operation: structuredClone(view.tubeDesignerOperation) } : {}) });
        if (method === 'TubeDesigner.ReadBatchExcelImport') return { templateId: template.id, templateName: template.name, rows: structuredClone(excelRows) };
        if (method === 'TubeDesigner.GeneratePreview') throw new Error('TubeDesigner.GeneratePreview requires a scene');
        throw new Error('Unexpected product transport request ' + method);
      },
      async getState() { return productState; }, getProject(id) { return this.projects.get(id) ?? projectProxy; },
      async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; } };
    const appProxy = { bridge: { async openFileDialog() { return 'D:\\imports\\Excel实际工作台.xlsx'; } }, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
    globalThis.__focusTransport = { appProxy, view, state, requests, projectId, otherProjectId, otherView, otherState, failureBaseline, specificationBaseline,
      excelBatch, excelRows, allowExcelRetry() { rejectSecondExcelRow = false; },
      get waitingUpdate() { return Boolean(releaseUpdate); }, get waitingState() { return Boolean(releaseState); },
      failNextUpdate(kind = 'rejected') { nextUpdateFailure = kind; },
      holdNextState() { holdState = true; }, releaseUpdate() { if (!releaseUpdate) throw new Error("No waiting EC update"); releaseUpdate(); },
      releaseState() { if (!releaseState) throw new Error("No waiting Scene.GetState"); releaseState(); },
      async waitIdle() { await view.activeAreaAction?.promise; await view.tubeDesignerProductParameterCommitPromise; } };
    await import("/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs");
  }, { descriptor, failureBaseline, specificationBaseline, excelBatch });
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().activeProjectId && !globalThis.__icaxAppShell.getState().pendingCount && document.querySelector('[data-product-control-editor="outerFrameConnection"] select'));
  await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{height:360px!important;overflow:auto!important}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:600px}.tube-designer-instance-panel{height:580px!important}.tube-designer-instance-list{height:250px!important;min-height:0!important;max-height:250px!important;overflow:auto!important;flex:none!important;display:block!important}.tube-designer-instance-list:after{content:"";display:block;height:1200px}.tube-designer-parameter-panel{height:680px!important}.tube-designer-parameter-scroll{height:520px!important;overflow:auto!important;flex:none!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:500px}' });
  await page.evaluate(() => { document.querySelectorAll('details').forEach(node => { node.open = true; }); });
  const results = [];
  if (excelTemplate) {
    const directory = resolve(source, 'apps/tube-designer/templates/product');
    const descriptors = readdirSync(directory).filter(name => existsSync(resolve(directory, name, 'template.json')))
      .map(name => JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${name}/template.json`)));
    const publicDescriptors = descriptors.filter(raw => raw.extensions?.catalog?.listed !== false);
    await page.evaluate(async descriptors => {
      const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
      const templates = descriptors.map(raw => ({ ...raw, available: true, descriptorLoaded: true,
        name: catalogText(raw.displayName), groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
        parameters: raw.parameters.map(field => ({ ...field, displayName: catalogText(field.displayName), groupKey: field.group,
          type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
          options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) }));
      const f = globalThis.__focusTransport;
      f.state.tubeDesigner.templates = templates;
      f.view.scene.tubeDesigner.templates = templates;
    }, descriptors);
    for (let pass = 0; pass < 2; pass++) {
      await page.evaluate(() => globalThis.__icaxAppShell.executeRibbonCommand('designer.excel.export-template'));
      const selector = page.locator('[data-cam-change-action="tube-designer-excel-template-select"]');
      assert.equal(await selector.inputValue(), descriptor.id, 'Opening the actual AppShell dialog selects its default template');
      assert.equal(await selector.locator('option:checked').textContent(), '防盗窗', 'Default template text is visible without opening the dropdown');
      for (const raw of publicDescriptors) {
        await selector.selectOption(raw.id);
        await page.evaluate(() => globalThis.__focusTransport.waitIdle());
        assert.equal(await selector.inputValue(), raw.id);
        assert.equal(await selector.locator('option:checked').textContent(), raw.displayName['zh-CN']);
        for (const field of raw.parameters.filter(field => field.presentation?.visible === false))
          assert.equal(await page.locator(`[data-tube-designer-excel-include="${field.key}"]`).count(), 0);
      }
      await page.locator('[data-tube-designer-excel-select-all="include"]').check();
      await page.evaluate(() => globalThis.__focusTransport.waitIdle());
      assert.equal(await selector.inputValue(), publicDescriptors.at(-1).id, 'Checkbox refresh keeps the selected template');
      await page.screenshot({ path: resolve(output, `excel-template-${pass}.png`) });
      await page.locator('header [data-cam-action="tube-designer-excel-template-close"]').click();
      await page.evaluate(() => globalThis.__focusTransport.waitIdle());
    }
    assert.deepEqual(errors, []);
    writeFileSync(resolve(output, 'excel-template-app-shell-report.json'), JSON.stringify({ status: 'passed', assetRoot: source,
      openCloseReopen: true, defaultTextVisible: true, templates: publicDescriptors.map(raw => raw.id), hiddenParametersExcluded: true }, null, 2));
    console.log('PASS actual AppShell Excel template default selection and reopen');
    process.exitCode = 0;
    await browser.close();
    process.exit(0);
  }
  if (excelBatch) {
    const excelResult = await page.evaluate(async () => {
      const f = globalThis.__focusTransport;
      const query = selector => document.querySelector(selector);
      const require = (condition, message) => { if (!condition) throw new Error(message); };
      const same = (actual, expected, label) => require(JSON.stringify(actual) === JSON.stringify(expected), label + ': ' + JSON.stringify({ actual, expected }));
      const frames = () => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      const wait = async getter => { for (let n = 0; !getter() && n < 400; n++) await new Promise(resolve => setTimeout(resolve, 10)); require(getter(), 'Expected actual AppShell callback did not become pending'); };
      const nodes = () => [query('.cam-context-pane'), query('.tube-designer-instance-list'), query('.cam-info-pane'), query('.tube-designer-parameter-scroll')];
      const scrolls = () => nodes().map(node => node.scrollTop);
      const setScrolls = values => { nodes().forEach((node, index) => { node.scrollTop = values[index]; }); same(scrolls(), values, 'Actual four panes must overflow'); };
      let confirmationRendered = false;
      const checkConfirmation = () => { if (query('[data-cam-action="tube-designer-excel-import-confirm"]') || query('.tube-designer-excel-import-dialog')) confirmationRendered = true; };
      const observer = new MutationObserver(checkConfirmation);
      observer.observe(document.body, { childList: true, subtree: true });
      setScrolls([150, 190, 210, 230]);
      await globalThis.__icaxAppShell.executeRibbonCommand('designer.import-excel').catch(() => {});
      await f.waitIdle().catch(() => {}); await frames();
      same(f.view.tubeDesignerExcelImport.rows.map(row => row.sourceRow), [7, 9], 'Partial failure must retain only remaining source rows');
      require(f.view.tubeDesignerExcelImport.createdCount === 1, 'Succeeded row count missing after failure');
      require(query('.log-list').textContent.includes('Excel 第 7 行导入失败'), 'Actual log must include the failed workbook row');
      require(!f.view.tubeDesignerRightDraftDirty, 'Committed imported model retained the preceding dirty status');
      same(scrolls(), [150, 190, 210, 230], 'Partial failure reset sidebars or nested scrolls');
      f.allowExcelRetry();
      await globalThis.__icaxAppShell.executeRibbonCommand('designer.import-excel');
      await f.waitIdle(); await frames();
      observer.disconnect(); checkConfirmation();
      require(!confirmationRendered, 'Import must not show a confirmation dialog at any stage');
      require(!f.view.notice && !query('.cam-status.notice'), 'Success must not create a viewport notice');
      require(query('.log-list').textContent.includes('已按 Excel 创建 3 个产品实例'), 'Import completion must be written to the actual log');
      require(query('[data-tube-designer-runtime-status]').hidden && !f.view.tubeDesignerRightDraftDirty, 'Current imported model must not retain a status banner');
      const batchRequests = f.requests.filter(request => request.method === 'TubeDesigner.GeneratePreview');
      same(batchRequests.map(request => request.scope), ['scene', 'scene', 'scene', 'scene'], 'Excel instance generation must use the actual scene channel');
      same(batchRequests.map(request => request.operation.message), ['Excel 第 3 行', 'Excel 第 7 行', 'Excel 第 7 行', 'Excel 第 9 行'], 'Progress must preserve original workbook row numbers');
      same(batchRequests.map(request => request.operation.phaseLabel), ['正在创建第 1 / 3 个实例', '正在创建第 2 / 3 个实例', '正在创建第 2 / 3 个实例', '正在创建第 3 / 3 个实例'], 'Retry progress must preserve cumulative created count');
      // The imported final product is now current. Change its real structure
      // control, including applicable conditional fields, and hold AppShell's
      // asynchronous acknowledgement while the user continues typing.
      f.holdNextState();
      const structure = query('[data-product-control-editor="outerFrameConnection"] select');
      require(structure && !structure.disabled, 'Imported product has no actual editable structure control');
      structure.value = 'tabs'; structure.dispatchEvent(new Event('change', { bubbles: true }));
      await wait(() => f.waitingUpdate); f.releaseUpdate();
      await wait(() => f.waitingState);
      require(!f.view.pending, 'Completed batch should allow editing during AppShell acknowledgement');
      const code = query('[data-tube-designer-parameter="productCode"]');
      const saved = { code, canvas: query('canvas'), form: query('[data-tube-designer-parameter-form]'), surface: query('[data-product-surface="project"]') };
      require(code && !code.disabled, 'Actual latest product code input remains disabled after batch');
      let listenerCalls = 0; code.addEventListener('excel-node-probe', () => listenerCalls++);
      code.focus({ preventScroll: true }); code.value = 'Excel异步草稿'; code.setSelectionRange(1, 4, 'forward');
      setScrolls([180, 240, 260, 290]);
      // Continue editing and scrolling after the acknowledgement was requested.
      code.value = 'Excel后续输入草稿'; code.setSelectionRange(2, 7, 'backward'); setScrolls([230, 300, 310, 350]);
      f.releaseState(); await f.waitIdle(); await frames();
      for (const [key, selector] of [['code', '[data-tube-designer-parameter="productCode"]'], ['canvas', 'canvas'], ['form', '[data-tube-designer-parameter-form]'], ['surface', '[data-product-surface="project"]']])
        require(saved[key] === query(selector), 'Excel acknowledgement replaced actual ' + key);
      require(document.activeElement === code && code.value === 'Excel后续输入草稿', 'Excel acknowledgement lost latest actual focus or text draft');
      same([code.selectionStart, code.selectionEnd, code.selectionDirection], [2, 7, 'backward'], 'Excel acknowledgement lost latest selection');
      same(scrolls(), [230, 300, 310, 350], 'Excel acknowledgement reset latest four scroll positions');
      code.dispatchEvent(new Event('excel-node-probe')); require(listenerCalls === 1, 'Actual input listener was replaced');
      const imported = f.state.tubeDesigner.instances.filter(instance => instance.entityId.startsWith('excel-instance-'));
      same(imported.map(instance => [instance.name, instance.quantity]), [['Excel 甲', 2], ['Excel 乙', 4], ['Excel 丙', 1]], 'Actual AppShell retry duplicated prior instance or lost quantity');
      same(f.requests.filter(request => request.method === 'TubeDesigner.GeneratePreview').map(request => request.payload.instanceName), ['Excel 甲', 'Excel 乙', 'Excel 乙', 'Excel 丙'], 'Original source rows must be retried in order');
      code.value = 'Excel更晚输入'; code.setSelectionRange(1, 5, 'forward'); setScrolls([240, 310, 320, 360]); await frames();
      require(code.value === 'Excel更晚输入', 'Deferred callback overwrote later actual text');
      same([code.selectionStart, code.selectionEnd, code.selectionDirection], [1, 5, 'forward'], 'Deferred callback overwrote later actual selection');
      same(scrolls(), [240, 310, 320, 360], 'Deferred callback overwrote later scrolls');
      return { case: 'actual-app-shell-partial-excel-import-and-retry', successfulRows: 3, quantities: [2, 4, 1],
        failedSourceRow: 7, retryOnlyRemainingRows: true, totalCountCorrect: true,
        directImportWithoutConfirmation: true, successOnlyInLog: true, currentStatusHidden: true,
        conditionalStructureControl: 'outerFrameConnection',
        sameLatestProductDuringAcknowledgement: true, focusTextSelectionFourScrollsPreserved: true,
        canvasFormInputSurfaceAndListenersPreserved: true, noDeferredOverwrite: true };
    });
    await page.screenshot({ path: resolve(output, 'excel-app-shell.png') });
    assert.deepEqual(errors, []);
    writeFileSync(resolve(output, 'excel-app-shell-report.json'), JSON.stringify({ status: 'passed',
      assetRoot: source, installationSelected: Boolean(process.env.ICAX_BROWSER_RUNTIME_ROOT),
      nativeTransport: 'controlled protocol fixture with current GeneratePreview activation semantics',
      actualAppShellAndWorkbench: true, standaloneCefEndToEnd: false, results: [excelResult], errors }, null, 2));
    console.log(JSON.stringify(excelResult));
    await browser.close();
    process.exit(0);
  }
  const first = await page.evaluate(async () => {
    const f = globalThis.__focusTransport;
    const select = document.querySelector('[data-product-control-editor="outerFrameConnection"] select');
    const surface = document.querySelector('[data-product-surface="project"]');
    const form = document.querySelector('[data-tube-designer-parameter-form]');
    const canvas = document.querySelector('canvas');
    select.focus({ preventScroll: true }); select.value = "tabs";
    select.dispatchEvent(new Event("change", { bubbles: true }));
    for (let attempt = 0; !f.waitingUpdate && attempt < 200; attempt++) await new Promise(resolve => setTimeout(resolve, 10));
    if (!f.waitingUpdate) throw new Error("Real change event did not invoke native EC update");
    f.releaseUpdate(); await f.waitIdle();
    return { case: "select-stays-focused", focusPreserved: document.activeElement === select, inputPreserved: select.isConnected,
      surfacePreserved: surface === document.querySelector('[data-product-surface="project"]'), formPreserved: form === document.querySelector('[data-tube-designer-parameter-form]'),
      canvasPreserved: canvas === document.querySelector('canvas'), activeTag: document.activeElement.tagName, requests: f.requests.map(item => item.method) };
  });
  results.push(first);
  if (baseline) {
    assert.equal(first.focusPreserved, false, "Baseline should reproduce lost focus");
    assert.equal(first.surfacePreserved, false, "Baseline should reproduce outer shell replacement");
    writeFileSync(resolve(output, "baseline-report.json"), JSON.stringify({ baseline: true, results, errors }, null, 2));
    console.log(JSON.stringify(first));
  } else {
    for (const [key, value] of Object.entries(first)) if (key.endsWith("Preserved")) assert.equal(value, true, key);
    results.push(...await page.evaluate(async () => {
      const f = globalThis.__focusTransport, results = [];
      const query = selector => document.querySelector(selector);
      const ordinary = key => query(`[data-tube-designer-parameter="${key}"]`);
      const option = key => query(`[data-product-control-editor="${key}"] select`);
      const scrollNodes = () => [query('.cam-context-pane'), query('.tube-designer-instance-list'), query('.cam-info-pane'), query('.tube-designer-parameter-scroll')];
      const scrolls = () => scrollNodes().map(node => node.scrollTop);
      const check = (actual, expected, label) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) throw new Error(label + ': ' + JSON.stringify({ actual, expected })); };
      const assert = (condition, label) => { if (!condition) throw new Error(label); };
      const setScrolls = values => { scrollNodes().forEach((node, index) => { node.scrollTop = values[index]; }); check(scrolls(), values, 'four panes must really overflow'); };
      const wait = async getter => {
        for (let attempt = 0; !getter() && attempt < 250; attempt++) await new Promise(resolve => setTimeout(resolve, 10));
        assert(getter(), 'expected transport request did not become pending');
      };
      const frames = () => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      const identities = () => ({ surface: query('[data-product-surface="project"]'), form: query('[data-tube-designer-parameter-form]'), canvas: query('canvas'),
        code: ordinary('productCode'), width: ordinary('frameWidth'), select: option('outerFrameConnection') });
      const assertIdentities = saved => { const current = identities(); for (const key of Object.keys(saved)) assert(saved[key] === current[key], 'production refresh replaced ' + key); };
      const initial = identities();
      const clicks = { code: 0, width: 0, select: 0, canvas: 0 };
      for (const key of Object.keys(clicks)) initial[key].addEventListener('focus-regression-probe', () => { clicks[key]++; });
      function dispatch(target, value) {
        assert(target?.isConnected, 'missing actual parameter field');
        target.focus({ preventScroll: true }); target.value = value;
        target.dispatchEvent(new Event('input', { bubbles: true }));
        target.dispatchEvent(new Event('change', { bubbles: true }));
      }
      async function commit(target, value, whileUpdate = () => {}, whileState = () => {}) {
        f.holdNextState(); dispatch(target, value);
        await wait(() => f.waitingUpdate);
        whileUpdate(); f.releaseUpdate();
        await wait(() => f.waitingState);
        whileState(); f.releaseState();
        await f.waitIdle(); await frames();
      }
      // Number inputs do not expose a selection range, but must retain the exact
      // node and the later typing that occurs while either native reply is pending.
      const width = ordinary('frameWidth');
      await commit(width, '42', () => {
        width.focus({ preventScroll: true }); width.value = '43.5'; setScrolls([51, 121, 61, 201]);
      }, () => {
        width.value = '44.5'; setScrolls([63, 143, 73, 223]);
      });
      assert(document.activeElement === width && width.value === '44.5', 'numeric edit lost current focus or later typing ' + JSON.stringify({ focus: document.activeElement === width, value: width.value, connected: width.isConnected, current: ordinary('frameWidth')?.value, active: document.activeElement.outerHTML?.slice(0, 500), scrolls: scrolls() }));
      assertIdentities(initial); check(scrolls(), [63, 143, 73, 223], 'numeric response restored stale scrolls');
      results.push({ case: 'numeric-latest-typing', focusPreserved: true, laterValue: width.value, scrolls: scrolls() });

      const code = ordinary('productCode');
      await commit(code, 'TD-FOCUS-01', () => {
        code.focus({ preventScroll: true }); code.setSelectionRange(1, 7, 'forward'); setScrolls([75, 155, 85, 235]);
      }, () => {
        code.setSelectionRange(3, 9, 'backward'); setScrolls([87, 167, 97, 247]);
      });
      assert(document.activeElement === code && code.value === 'TD-FOCUS-01', 'text edit lost focus or committed value');
      check([code.selectionStart, code.selectionEnd, code.selectionDirection], [3, 9, 'backward'], 'text edit lost latest selection');
      assertIdentities(initial); check(scrolls(), [87, 167, 97, 247], 'text response restored stale scrolls');
      results.push({ case: 'text-latest-selection', selection: [code.selectionStart, code.selectionEnd, code.selectionDirection], scrolls: scrolls() });

      // A response must follow the interaction at response time, even when the
      // user moved away from the edited control into the opposite pane.
      const leftButton = query('[data-tube-designer-instance-id]:not([disabled])');
      await commit(option('horizontalEndConnectionChoice'), 'weld', () => {
        code.focus({ preventScroll: true }); code.value = 'UNCOMMITTED-RIGHT-DRAFT'; code.setSelectionRange(2, 11, 'backward');
        setScrolls([99, 179, 109, 259]);
      }, () => {
        leftButton.focus({ preventScroll: true }); setScrolls([111, 191, 121, 271]);
      });
      assert(document.activeElement === leftButton, 'later left-pane focus was replaced by original edited control');
      assertIdentities(initial); check(scrolls(), [111, 191, 121, 271], 'later left-pane scrolling was overwritten');
      results.push({ case: 'async-move-to-left-pane', focusPreserved: true, scrolls: scrolls() });

      await commit(option('verticalEndConnectionChoice'), 'tabs', () => {
        leftButton.focus({ preventScroll: true }); setScrolls([123, 203, 133, 283]);
      }, () => {
        code.focus({ preventScroll: true }); code.value = 'LATEST-UNCOMMITTED-TEXT'; code.setSelectionRange(2, 10, 'backward');
        setScrolls([135, 215, 145, 295]);
      });
      assert(document.activeElement === code && code.value === 'LATEST-UNCOMMITTED-TEXT', 'latest right typing/focus was overwritten');
      check([code.selectionStart, code.selectionEnd, code.selectionDirection], [2, 10, 'backward'], 'latest right selection was overwritten');
      assertIdentities(initial); check(scrolls(), [135, 215, 145, 295], 'latest right-pane scrolls were overwritten');
      code.value = 'LATER-TYPING'; code.setSelectionRange(1, 5, 'forward'); setScrolls([147, 227, 157, 307]);
      await frames(); await new Promise(resolve => setTimeout(resolve, 35));
      check([code.value, code.selectionStart, code.selectionEnd, code.selectionDirection], ['LATER-TYPING', 1, 5, 'forward'], 'late restoration overrode later user input');
      check(scrolls(), [147, 227, 157, 307], 'late restoration overrode later scrolls');
      results.push({ case: 'async-move-to-right-text', latestSelectionPreserved: true, delayedInteractionPreserved: true, scrolls: scrolls() });

      await commit(option('outerFrameConnection'), 'butt');
      const conditional = ordinary('frameButtWrapMode');
      assert(conditional, 'production butt choice did not show conditional wrap field');
      await commit(option('outerFrameConnection'), 'miter', () => {
        conditional.focus({ preventScroll: true }); setScrolls([159, 239, 169, 319]);
      }, () => {
        conditional.focus({ preventScroll: true }); setScrolls([171, 251, 181, 331]);
      });
      assert(!conditional.isConnected && !ordinary('frameButtWrapMode'), 'conditional field should have been removed');
      assert(!document.activeElement.matches('input,select,textarea'), 'removed conditional field lent its focus to another parameter');
      assertIdentities(initial); check(scrolls(), [171, 251, 181, 331], 'conditional removal overwrote current scrolls');
      results.push({ case: 'conditional-removal', removed: true, focusWasNotBorrowed: true, scrolls: scrolls() });

      for (const key of Object.keys(clicks)) initial[key].dispatchEvent(new Event('focus-regression-probe'));
      check(clicks, { code: 1, width: 1, select: 1, canvas: 1 }, 'retained DOM listeners were lost or duplicated');
      results.push({ case: 'input-canvas-listeners', preserved: true });

      if (!f.failureBaseline) {
        f.failNextUpdate(); dispatch(ordinary('assemblyClearance'), '0.25');
        await wait(() => f.waitingUpdate);
        code.focus({ preventScroll: true }); code.value = 'CURRENT-ERROR-DRAFT'; code.setSelectionRange(2, 8, 'backward');
        setScrolls([183, 263, 193, 343]);
        f.releaseUpdate(); await f.waitIdle().catch(() => {}); await frames();
        assert(f.view.error.includes('Native parameter validation rejected'), 'current-surface validation error was not shown');
        assert(document.activeElement === code && code.value === 'CURRENT-ERROR-DRAFT', 'current-surface validation error lost user focus/draft');
        check([code.selectionStart, code.selectionEnd, code.selectionDirection], [2, 8, 'backward'], 'current-surface validation error reset selection');
        assertIdentities(initial); check(scrolls(), [183, 263, 193, 343], 'current-surface validation error reset scrolls');
        results.push({ case: 'current-page-with-rejected-update', errorShown: true, focusSelectionDraftNodesAndScrollsPreserved: true });
      }

      // Switch with a pending EC reply through AppShell's real navigation.
      dispatch(option('horizontalEndConnectionChoice'), 'tabs');
      await wait(() => f.waitingUpdate);
      await globalThis.__icaxAppShell.selectRibbonTab('nesting');
      const pageFocus = query('.cam-context-pane input:not([disabled]),.cam-info-pane input:not([disabled]),[data-action="select-ribbon-tab"][data-tab-id="nesting"]');
      assert(pageFocus, 'new page has no focusable control');
      pageFocus.focus({ preventScroll: true });
      const newSurface = query('[data-product-surface="project"]');
      f.releaseUpdate(); await f.waitIdle(); await frames();
      assert(globalThis.__icaxAppShell.getState().activeRibbonTabId === 'nesting' && f.view.activeAreaId === 'nesting', 'late parameter reply navigated back to product');
      assert(document.activeElement === pageFocus && newSurface === query('[data-product-surface="project"]'), 'late reply stole focus or replaced the new page');
      results.push({ case: 'page-switch-with-pending-update', currentPagePreserved: true, focusPreserved: true });
      await globalThis.__icaxAppShell.selectRibbonTab('view');

      f.holdNextState(); dispatch(option('horizontalEndConnectionChoice'), 'insert');
      await wait(() => f.waitingUpdate); f.releaseUpdate(); await wait(() => f.waitingState);
      await globalThis.__icaxAppShell.selectRibbonTab('nesting');
      const laterPageFocus = query('[data-action="select-ribbon-tab"][data-tab-id="nesting"]');
      laterPageFocus.focus({ preventScroll: true });
      const laterSurface = query('[data-product-surface="project"]'), laterCanvas = query('canvas');
      f.releaseState(); await f.waitIdle(); await frames();
      assert(globalThis.__icaxAppShell.getState().activeRibbonTabId === 'nesting' && f.view.activeAreaId === 'nesting', 'late Scene.GetState mounted previous product page');
      assert(document.activeElement === laterPageFocus && laterSurface === query('[data-product-surface="project"]') && laterCanvas === query('canvas'), 'late Scene.GetState stole new page focus or rebuilt it');
      results.push({ case: 'page-switch-with-pending-scene-state', currentPagePreserved: true, focusAndSurfacePreserved: true });
      await globalThis.__icaxAppShell.selectRibbonTab('view');

      // Rejections are delivered through the actual workbench onchange promise
      // catch, which must not render its old context into the newly active page.
      f.failNextUpdate(); dispatch(option('horizontalEndConnectionChoice'), 'weld');
      await wait(() => f.waitingUpdate);
      await globalThis.__icaxAppShell.selectRibbonTab('nesting');
      const rejectedPageFocus = query('[data-action="select-ribbon-tab"][data-tab-id="nesting"]');
      rejectedPageFocus.focus({ preventScroll: true });
      const rejectedSurface = query('[data-product-surface="project"]'), rejectedCanvas = query('canvas');
      f.releaseUpdate(); await f.waitIdle().catch(() => {}); await frames();
      const rejectedPagePreserved = globalThis.__icaxAppShell.getState().activeRibbonTabId === 'nesting' && f.view.activeAreaId === 'nesting';
      const rejectedFocusPreserved = document.activeElement === rejectedPageFocus && rejectedSurface === query('[data-product-surface="project"]') && rejectedCanvas === query('canvas');
      if (f.failureBaseline) assert(!rejectedPagePreserved, 'unguarded original catch must reproduce old-page revival');
      else {
        assert(rejectedPagePreserved, 'failed parameter reply resurrected previous product page');
        assert(rejectedFocusPreserved, 'failed parameter reply stole new page focus or rebuilt it');
      }
      results.push({ case: 'page-switch-with-rejected-update', currentPagePreserved: rejectedPagePreserved, focusPreserved: rejectedFocusPreserved, expectedFailure: f.failureBaseline });
      await globalThis.__icaxAppShell.selectRibbonTab('view');

      // Click a different product instance while the first parameter update is
      // still pending. Neither response may restore the previous product editor.
      const sourceProductId = f.view.scene.tubeDesigner.product.entityId;
      dispatch(option('verticalEndConnectionChoice'), 'weld');
      await wait(() => f.waitingUpdate);
      const target = [...document.querySelectorAll('[data-tube-designer-instance-id]')].find(node => node.dataset.tubeDesignerInstanceId !== sourceProductId);
      target.click(); await f.view.activeAreaAction?.promise;
      const nextProductId = f.view.scene.tubeDesigner.product.entityId;
      assert(nextProductId !== sourceProductId, 'actual instance click did not switch product');
      const nextCode = ordinary('productCode');
      nextCode.focus({ preventScroll: true }); nextCode.value = 'NEXT-PRODUCT-DRAFT'; nextCode.setSelectionRange(2, 8, 'backward');
      f.releaseUpdate(); await f.waitIdle(); await frames();
      assert(f.view.scene.tubeDesigner.product.entityId === nextProductId, 'old EC response replaced active product');
      assert(document.activeElement === nextCode && ordinary('productCode') === nextCode && nextCode.value === 'NEXT-PRODUCT-DRAFT', 'old EC response stole new product focus/draft');
      check([nextCode.selectionStart, nextCode.selectionEnd, nextCode.selectionDirection], [2, 8, 'backward'], 'old EC response reset new product selection');
      results.push({ case: 'product-switch-with-pending-update', activeProductPreserved: true, focusAndDraftPreserved: true });

      f.failNextUpdate('timeout'); dispatch(option('horizontalEndConnectionChoice'), 'weld');
      await wait(() => f.waitingUpdate);
      query(`[data-action="select-open-project"][data-project-id="${f.otherProjectId}"]`).click();
      await wait(() => globalThis.__icaxAppShell.getState().activeProjectId === f.otherProjectId && ordinary('productCode') !== nextCode);
      await frames();
      const timeoutCode = ordinary('productCode'), timeoutSurface = query('[data-product-surface="project"]'), timeoutCanvas = query('canvas');
      timeoutCode.focus({ preventScroll: true }); timeoutCode.value = 'TIMEOUT-PROJECT-DRAFT'; timeoutCode.setSelectionRange(2, 8, 'backward');
      f.releaseUpdate(); await f.waitIdle().catch(() => {}); await frames();
      assert(globalThis.__icaxAppShell.getState().activeProjectId === f.otherProjectId, 'timed-out old parameter reply changed active project');
      const timeoutFocusPreserved = document.activeElement === timeoutCode && ordinary('productCode') === timeoutCode && timeoutCode.value === 'TIMEOUT-PROJECT-DRAFT';
      if (f.failureBaseline) assert(!timeoutFocusPreserved, 'unguarded original timeout catch must reproduce editor/focus replacement in new project');
      else {
        assert(timeoutFocusPreserved, 'timed-out old parameter reply replaced another project editor/focus');
        check([timeoutCode.selectionStart, timeoutCode.selectionEnd, timeoutCode.selectionDirection], [2, 8, 'backward'], 'timed-out old parameter reply reset another project selection');
        assert(timeoutSurface === query('[data-product-surface="project"]') && timeoutCanvas === query('canvas'), 'timed-out old parameter reply rebuilt another project surface');
      }
      results.push({ case: 'project-switch-with-timed-out-update', focusDraftPreserved: timeoutFocusPreserved, expectedFailure: f.failureBaseline });
      query(`[data-action="select-open-project"][data-project-id="${f.projectId}"]`).click();
      await wait(() => globalThis.__icaxAppShell.getState().activeProjectId !== f.otherProjectId);
      await frames();

      f.holdNextState(); dispatch(option('horizontalEndConnectionChoice'), 'tabs');
      await wait(() => f.waitingUpdate); f.releaseUpdate(); await wait(() => f.waitingState);
      const switchProject = query(`[data-action="select-open-project"][data-project-id="${f.otherProjectId}"]`);
      assert(switchProject, 'actual AppShell project switch button missing');
      switchProject.click();
      await wait(() => globalThis.__icaxAppShell.getState().activeProjectId === f.otherProjectId && ordinary('productCode') !== nextCode);
      await frames();
      const otherCode = ordinary('productCode'), otherSurface = query('[data-product-surface="project"]'), otherCanvas = query('canvas');
      otherCode.focus({ preventScroll: true }); otherCode.value = 'OTHER-PROJECT-DRAFT'; otherCode.setSelectionRange(2, 9, 'backward');
      f.releaseState(); await f.waitIdle(); await frames();
      const shellState = globalThis.__icaxAppShell.getState();
      assert(shellState.activeProjectId === f.otherProjectId && shellState.activeSceneId === f.otherState.sceneId, 'late old-scene reply corrupted active project/scene');
      assert(document.activeElement === otherCode && ordinary('productCode') === otherCode && otherCode.value === 'OTHER-PROJECT-DRAFT', 'late old-scene reply stole another project input/draft');
      check([otherCode.selectionStart, otherCode.selectionEnd, otherCode.selectionDirection], [2, 9, 'backward'], 'late old-scene reply reset another project selection');
      assert(otherSurface === query('[data-product-surface="project"]') && otherCanvas === query('canvas'), 'late old-scene reply rebuilt another project surface');
      results.push({ case: 'project-switch-with-pending-scene-state', projectAndScenePreserved: true, focusSelectionDraftAndSurfacePreserved: true });

      if (!f.failureBaseline) {
        query(`[data-action="select-open-project"][data-project-id="${f.projectId}"]`).click();
        await wait(() => globalThis.__icaxAppShell.getState().activeProjectId === f.projectId);
        await frames();
        const startSpecificationEdit = async value => {
          const label = query('[data-cam-action="tube-designer-edit-scene-specification"][data-tube-designer-parameter-key="height"]');
          assert(label, 'actual Three specification annotation trigger missing');
          label.click(); await f.view.activeAreaAction?.promise;
          const input = query('[data-tube-designer-scene-specification-input="height"]');
          assert(input && document.activeElement === input, 'actual specification click did not open/focus its editor');
          input.value = value; input.dispatchEvent(new Event('input', { bubbles: true }));
          input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
          await wait(() => f.waitingUpdate);
          assert(!input.isConnected, 'Enter should close the temporary scene editor synchronously');
        };
        await startSpecificationEdit('1900');
        await globalThis.__icaxAppShell.selectRibbonTab('nesting');
        const specificationPageFocus = query('[data-action="select-ribbon-tab"][data-tab-id="nesting"]');
        specificationPageFocus.focus({ preventScroll: true });
        const specificationPageSurface = query('[data-product-surface="project"]'), specificationPageCanvas = query('canvas');
        f.releaseUpdate(); await f.waitIdle(); await frames();
        assert(globalThis.__icaxAppShell.getState().activeRibbonTabId === 'nesting' && f.view.activeAreaId === 'nesting', 'late specification reply revived the old product page');
        assert(document.activeElement === specificationPageFocus && specificationPageSurface === query('[data-product-surface="project"]') && specificationPageCanvas === query('canvas'), 'late specification reply changed new page focus/surface');
        results.push({ case: 'scene-specification-page-switch', currentPageFocusAndSurfacePreserved: true });
        await globalThis.__icaxAppShell.selectRibbonTab('view');

        await startSpecificationEdit('2000');
        query(`[data-action="select-open-project"][data-project-id="${f.otherProjectId}"]`).click();
        await wait(() => globalThis.__icaxAppShell.getState().activeProjectId === f.otherProjectId);
        await frames();
        const specificationProjectCode = ordinary('productCode'), specificationProjectStatus = query('[data-tube-designer-runtime-status]');
        const specificationProjectDock = query('.tube-designer-product-parts-dock'), specificationProjectDockText = specificationProjectDock.textContent;
        const specificationProjectSurface = query('[data-product-surface="project"]'), specificationProjectCanvas = query('canvas');
        specificationProjectCode.focus({ preventScroll: true }); specificationProjectCode.value = 'SPECIFICATION-OTHER-PROJECT'; specificationProjectCode.setSelectionRange(2, 9, 'backward');
        f.releaseUpdate(); await f.waitIdle(); await frames();
        assert(globalThis.__icaxAppShell.getState().activeProjectId === f.otherProjectId && globalThis.__icaxAppShell.getState().activeSceneId === f.otherState.sceneId, 'late specification reply changed another project/scene');
        const specificationProjectStatusAndPartsPreserved = specificationProjectStatus === query('[data-tube-designer-runtime-status]') && specificationProjectDock === query('.tube-designer-product-parts-dock') && specificationProjectDockText === specificationProjectDock.textContent;
        if (f.specificationBaseline) assert(!specificationProjectStatusAndPartsPreserved, 'unguarded specification continuation must reproduce writing old status/parts into another project');
        else assert(specificationProjectStatusAndPartsPreserved, 'late specification continuation wrote old status/parts into another project');
        assert(document.activeElement === specificationProjectCode && ordinary('productCode') === specificationProjectCode && specificationProjectCode.value === 'SPECIFICATION-OTHER-PROJECT', 'late specification reply changed another project focus/draft');
        check([specificationProjectCode.selectionStart, specificationProjectCode.selectionEnd, specificationProjectCode.selectionDirection], [2, 9, 'backward'], 'late specification reply changed another project selection');
        assert(specificationProjectSurface === query('[data-product-surface="project"]') && specificationProjectCanvas === query('canvas'), 'late specification reply rebuilt another project surface');
        results.push({ case: 'scene-specification-project-switch', projectSceneFocusSelectionDraftAndSurfacePreserved: true,
          statusAndPartsPreserved: specificationProjectStatusAndPartsPreserved, expectedFailure: f.specificationBaseline });
      }
      assert(!f.requests.some(request => request.method.includes('GeneratePreview')), 'ordinary edit generated display geometry');
      return results;
    }));
    assert.deepEqual(errors, []);
    writeFileSync(resolve(output, failureBaseline ? "failure-baseline-report.json" : specificationBaseline ? "specification-baseline-report.json" : "report.json"), JSON.stringify({ passed: true, failureBaseline, specificationBaseline, results }, null, 2));
    if (!failureBaseline && !specificationBaseline) await page.screenshot({ path: resolve(output, "right-parameter-focus.png"), fullPage: true });
    console.log(failureBaseline ? "Actual unguarded workbench catch reproduces old-page revival after native rejection and new-project editor/focus replacement after native timeout."
      : specificationBaseline ? "Actual unguarded specification response continuation reproduces writing the previous product status/parts into another project."
      : `Actual AppShell/entry/createWorkbench: ${results.length} cases preserve input focus, latest typing and selection, four scroll positions, DOM/canvas/listeners, conditional removal and late successful/failed replies after page/product/project switches.`);
  }
} finally { await browser.close(); }
