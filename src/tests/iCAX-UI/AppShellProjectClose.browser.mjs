// Real AppShell, ProductProxy, ProjectProxy and TubeDesigner entry/workbench.
// Only native transport and empty scene-view snapshots are controlled fixtures.
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('appshell-project-close');
mkdirSync(output, { recursive: true });
const rawTemplate = JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/template.json'));
const rawProfile = JSON.parse(readBrowserAsset('apps/tube-designer/templates/profile/round/profile.json'));
rawProfile.display = JSON.parse(readBrowserAsset('apps/tube-designer/templates/profile/round/display.json'));
const report = { status: 'running', checks: [], screenshots: [],
  scope: 'Real browser AppShell DOM, production ProductProxy/ProjectProxy and TubeDesigner entry/workbench. Scope-checked native transport is controlled. No user project files, native file selector or standalone CEF application were operated.' };
const errors = [];
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  page.setDefaultTimeout(30000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://project-close.test/**', async route => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript',
      body: 'export async function connectApplication(){return window.fixture.appProxy;}' });
    if (path === '/src/iCAX-UI/SDK/AppShell/index.html') return route.fulfill({ contentType: 'text/html',
      body: readBrowserAsset('iCAX-UI/SDK/AppShell/index.html').replace(/<script type="module"[^>]*><\/script>/, '') });
    if (path.startsWith('/src/') && /\.(css|svg)$/.test(path)) return route.fulfill({
      contentType: path.endsWith('.css') ? 'text/css' : 'image/svg+xml', body: readBrowserAsset(path.slice('/src/'.length)) });
    return serveBrowserAsset(route);
  });
  await page.goto('http://project-close.test/src/iCAX-UI/SDK/AppShell/index.html');
  await page.evaluate(async ({ rawTemplate, rawProfile }) => {
    const { ProductProxy } = await import('/src/iCAX-UI/ProductProxy/ProductProxy.mjs');
    const { ProjectProxy } = await import('/src/iCAX-UI/ProjectProxy/ProjectProxy.mjs');
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const schema = { ...rawTemplate, available: true, descriptorLoaded: true, version: 'close-test-v1', name: catalogText(rawTemplate.displayName),
      groups: rawTemplate.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: rawTemplate.parameters.map(field => ({ ...field, type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const parameters = Object.fromEntries(schema.parameters.map(field => [field.key, field.defaultValue]));
    const f = window.fixture = { getProjectView, requests: [], gates: new Map(), entries: new Map(), opens: [],
      saveResults: [], closeErrors: new Map(), saveErrors: new Map(), stopChannels: [], failures: [], generation: 0 };
    f.hold = (method, projectId) => {
      const key = method + '|' + projectId; let release;
      const promise = new Promise(done => { release = done; });
      f.gates.set(key, { key, method, projectId, promise, release, waiting: false }); return key;
    };
    f.release = key => { const g = f.gates.get(key); if (!g) throw Error('Missing gate ' + key); g.release(); };
    const request = async (scope, method, payload = {}, projectId = '') => {
      f.requests.push({ scope, method, projectId, payload: structuredClone(payload) });
      const g = f.gates.get(method + '|' + projectId);
      if (g) { g.waiting = true; await g.promise; g.waiting = false; f.gates.delete(g.key); }
    };
    const nativeProduct = { productId: 'icax.tube-designer', productName: 'TubeDesigner', isStarted: true,
      productChannelId: 'close-product-channel', frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs',
      catalogs: [], recentProjects: [], projectFile: { fileExtensions: ['ictd'] } };
    const bridge = { async windowCommand() {}, async registerSceneChannel(projectId) { return projectId + '-scene-channel'; },
      async saveFileDialog(options) { await request('host-dialog', 'saveFileDialog', options); return f.saveResults.shift() ?? null; } };
    const transport = { bridge, stop(channel) { f.stopChannels.push(channel); },
      async invoke(channel, method, payload = {}) {
        if (channel !== nativeProduct.productChannelId) throw Error('Product operation used wrong channel: ' + channel + '/' + method);
        const catalog = nativeProduct.catalogs.find(c => c.catalogId === payload.catalogId);
        const projectId = catalog?.mainProject?.projectId ?? '';
        await request('product', method, payload, projectId);
        if (method === 'Product.GetState') return structuredClone(nativeProduct);
        if (method === 'Product.OpenProjectCatalog') {
          const entry = makeProject(payload.projectPath, payload.projectName || '未命名项目');
          return { state: structuredClone(nativeProduct), catalog: structuredClone(entry.catalog) };
        }
        if (method === 'Product.CloseProjectCatalog') {
          if (!catalog) throw Error('Unknown close catalogue: ' + payload.catalogId);
          if (f.closeErrors.has(projectId)) throw Error(f.closeErrors.get(projectId));
          nativeProduct.catalogs = nativeProduct.catalogs.filter(c => c.catalogId !== payload.catalogId);
          f.entries.get(projectId).closed = true;
          return structuredClone(nativeProduct);
        }
        if (method === 'TubeDesigner.ListUserData') return { systemProfiles: [{ id: 'round', name: '圆管', libraryScope: 'system',
          descriptor: rawProfile, defaultParameters: Object.fromEntries(rawProfile.parameters.map(p => [p.key, p.defaultValue])) }],
          profiles: [], templateProfiles: [], customers: [], parameterPresets: [], punchTools: [], productTemplates: [] };
        if (method === 'TubeDesigner.ScanBatchExcelAutomation') return { workbooks: [], files: [], pending: [] };
        throw Error('Unexpected product request: ' + method);
      } };
    const product = f.product = new ProductProxy(transport, structuredClone(nativeProduct), { bridge });
    let projectSequence = 0;
    function makeProject(path, name, reopenId = '') {
      const index = ++projectSequence, projectId = reopenId || 'close-project-' + index, sceneId = projectId + '-scene';
      const generation = ++f.generation;
      const instances = Array.from({ length: 24 }, (_, i) => ({ entityId: `${projectId}-product-${i}`, name: `${name} ${i + 1}`,
        templateId: schema.id, quantity: 1, parameters: { ...parameters, productCode: `${name}-GEN-${generation}` } }));
      const scene = { sceneId, sceneChannelId: projectId + '-scene-channel', undoRedo: { revision: 1, canUndo: false }, tubeDesigner: {
        templates: [{ id: schema.id, version: schema.version, name: schema.name, available: true }], instances,
        product: instances[0], activeProductId: instances[0].entityId, members: [], joints: [], parts: [], manufacturingGroups: [], nestingGroups: [] } };
      const state = { projectId, projectName: name, projectPath: path, mainScene: scene };
      const catalog = { catalogId: 'close-catalog-' + projectId, catalogPath: path, mainProject: state };
      const snapshot = { viewId: projectId + '-view-' + generation, revision: '1', rows: [] };
      const sceneProxy = { state: scene, sceneId, sceneChannelId: scene.sceneChannelId, pdo: { enabled: false },
        resources: { get() { throw Error('Empty fixture requested geometry'); } },
        views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
        updateState(next) { this.state = next; }, dispose() { f.stopChannels.push(this.sceneChannelId); },
        async getState() { await request('scene', 'Scene.GetState', {}, projectId); return structuredClone(scene); },
        async invoke(method, payload = {}) {
          await request('scene', method, payload, projectId);
          if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(scene.tubeDesigner) };
          if (method === 'TubeDesigner.GetTemplateDescriptor' || method === 'TubeDesigner.GetProductTemplateDescriptor') return { template: structuredClone(schema) };
          if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
          if (method === 'Project.Save') {
            if (f.saveErrors.has(projectId)) throw Error(f.saveErrors.get(projectId));
            state.projectPath = payload.projectPath; catalog.catalogPath = payload.projectPath;
            return { saved: true, project: structuredClone(state) };
          }
          // The production editor commits existing-instance information before
          // Project.Save; persist those values in this controlled scene too.
          if (method === 'TubeDesigner.UpdateProductParameters') {
            Object.assign(scene.tubeDesigner.product.parameters, payload.parameters ?? {});
            return { tubeDesigner: structuredClone(scene.tubeDesigner) };
          }
          throw Error('Unexpected scene request: ' + method);
        } };
      const proxy = new ProjectProxy(transport, state, { bridge, product });
      proxy.scenes.set(sceneId, sceneProxy); proxy.mainSceneProxy = sceneProxy;
      const entry = { projectId, sceneId, state, scene, proxy, sceneProxy, catalog, generation, closed: false };
      product.projects.set(projectId, proxy); nativeProduct.catalogs.push(catalog); f.entries.set(projectId, entry);
      return entry;
    }
    f.appProxy = { products: new Map([[product.productId, product]]), bridge,
      getProduct(id) { return id === product.productId ? product : null; }, async startProduct() { return product; },
      async getState() { return { products: [structuredClone(nativeProduct)] }; },
      async openProjectFile(path) {
        await request('application', 'App.OpenProjectFile', { projectPath: path });
        const previous = [...f.entries.values()].find(e => e.closed && e.state.projectPath === path);
        const entry = makeProject(path, path.split('/').at(-1), previous?.projectId);
        product.updateState(structuredClone(nativeProduct));
        return { productProxy: product, product: product.state, projectProxy: entry.proxy, sceneProxy: entry.sceneProxy,
          catalog: entry.catalog, projectPath: path };
      } };
    f.open = path => {
      const job = window.__icaxAppShell.openProject(path, product.productId).catch(error => { f.failures.push(error.message); throw error; });
      f.opens.push(job); return job;
    };
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, { rawTemplate, rawProfile });

  const shell = () => page.evaluate(() => window.__icaxAppShell.getState());
  const code = () => page.locator('[data-tube-designer-parameter="productCode"]');
  const activeReady = async () => {
    await page.waitForFunction(() => {
      const s = window.__icaxAppShell.getState(), v = window.fixture.getProjectView(s.activeProjectId);
      return s.startupPhase === 'ready' && !s.pendingCount && v.tubeDesignerLoaded && !v.tubeDesignerLoading
        && !v.tubeDesignerUserDataSynchronizationPromise && !v.tubeDesignerUserDataRefreshPromise;
    });
    await code().waitFor({ state: 'visible' });
  };
  const open = async name => {
    await page.evaluate(name => window.fixture.open('D:/ProjectCloseTests/' + name + '.ictd'), name);
    await activeReady(); return (await shell()).activeProjectId;
  };
  const create = async name => {
    await page.evaluate(name => window.__icaxAppShell.createProject({ productId: window.fixture.product.productId,
      projectName: name, projectPath: 'D:/ProjectCloseTests/' + name + '.ictd' }), name);
    await activeReady(); return (await shell()).activeProjectId;
  };
  const reveal = async id => {
    await page.evaluate(id => {
      const selector = `[data-action="close-open-project"][data-project-id="${id}"]`;
      while (!document.querySelector(selector)) {
        const prev = document.querySelector('[data-action="project-window-prev"]');
        if (!prev || prev.disabled) break; prev.click();
      }
      while (!document.querySelector(selector)) {
        const next = document.querySelector('[data-action="project-window-next"]');
        if (!next || next.disabled) throw Error('Project tab not available: ' + id); next.click();
      }
    }, id);
  };
  const select = async id => {
    await reveal(id);
    await page.locator(`[data-action="select-open-project"][data-project-id="${id}"]`).click();
    await page.waitForFunction(id => window.__icaxAppShell.getState().activeProjectId === id, id); await activeReady();
  };
  const promptClose = async id => {
    await reveal(id);
    await page.locator(`[data-action="close-open-project"][data-project-id="${id}"]`).click();
    await page.locator(`dialog[data-project-close-dialog="${id}"]`).waitFor({ state: 'visible' });
  };
  const choose = async choice => {
    await page.locator(`[data-project-close-choice="${choice}"]`).click();
    await page.locator('dialog[data-project-close-dialog]').waitFor({ state: 'detached' });
  };
  const closed = async id => page.waitForFunction(id => !window.fixture.product.projects.has(id), id);
  const close = async (id, choice = 'discard') => { await promptClose(id); await choose(choice); await closed(id); };
  const requestCount = method => page.evaluate(method => window.fixture.requests.filter(r => r.method === method).length, method);
  const screenshot = async name => { const path = resolve(output, name + '.png'); await page.screenshot({ path }); report.screenshots.push(path); };

  await activeReady();
  const initialId = (await shell()).activeProjectId;
  const aId = await open('A'), bId = await open('B');
  const semantics = await page.locator('.project-tabs .project-tab').evaluateAll(tabs => tabs.map(tab => ({
    container: tab.tagName, selects: tab.querySelectorAll('[data-action="select-open-project"]').length,
    closes: tab.querySelectorAll('[data-action="close-open-project"]').length, nestedButtons: tab.querySelectorAll('button button').length,
    closeLabel: tab.querySelector('[data-action="close-open-project"]')?.getAttribute('aria-label') })));
  assert.equal(semantics.length, 3);
  assert.ok(semantics.every(t => t.container === 'DIV' && t.selects === 1 && t.closes === 1 && !t.nestedButtons && t.closeLabel));
  report.checks.push('Every visible project tab has its own accessible sibling select/close controls, including the single-project layout');

  await select(aId);
  const closeBeforeCancel = await requestCount('Product.CloseProjectCatalog');
  await promptClose(bId);
  assert.equal((await shell()).activeProjectId, aId, 'Clicking a background close must not select that project');
  await choose('cancel');
  assert.equal(await requestCount('Product.CloseProjectCatalog'), closeBeforeCancel);
  assert.equal(await page.evaluate(id => window.fixture.product.projects.has(id), bId), true);
  report.checks.push('Cancel retains the background project and selection and sends no native close');

  await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{height:360px!important;overflow:auto!important}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:650px}.tube-designer-instance-list{height:230px!important;overflow:auto!important;display:block!important;flex:none!important}.tube-designer-instance-list:after{content:"";display:block;height:800px}.tube-designer-parameter-scroll{height:500px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:800px}' });
  const rememberInteraction = async value => {
    await code().focus(); await code().fill(value);
    await page.evaluate(() => {
      const f = window.fixture;
      f.input = document.querySelector('[data-tube-designer-parameter="productCode"]'); f.input.setSelectionRange(2, 12, 'backward');
      f.surface = document.querySelector('[data-product-surface="project"]'); f.canvas = f.surface.querySelector('canvas');
      f.scrollNodes = ['.cam-context-pane', '.tube-designer-instance-list', '.cam-info-pane', '.tube-designer-parameter-scroll'].map(s => document.querySelector(s));
      f.scrollNodes.forEach((node, i) => { node.scrollTop = 65 + i * 20; });
      const listenerCounts = f.listenerCounts = [0, 0];
      f.input.addEventListener('test-retained', () => listenerCounts[0]++); f.canvas.addEventListener('test-retained', () => listenerCounts[1]++);
    });
  };
  const latestInteraction = () => page.evaluate(() => {
    const f = window.fixture; f.input.setSelectionRange(3, 14, 'backward');
    f.scrollNodes.forEach((node, i) => { node.scrollTop = 125 + i * 25; }); f.expectedScrolls = f.scrollNodes.map(n => n.scrollTop);
  });
  const interaction = () => page.evaluate(() => {
    const f = window.fixture; f.input.dispatchEvent(new Event('test-retained')); f.canvas.dispatchEvent(new Event('test-retained'));
    return { projectId: window.__icaxAppShell.getState().activeProjectId, sceneId: window.__icaxAppShell.getState().activeSceneId,
      surface: f.surface === document.querySelector('[data-product-surface="project"]'), canvas: f.canvas === document.querySelector('[data-product-surface="project"] canvas'),
      input: f.input === document.querySelector('[data-tube-designer-parameter="productCode"]'), focus: document.activeElement === f.input,
      value: f.input.value, selection: [f.input.selectionStart, f.input.selectionEnd, f.input.selectionDirection],
      scrolls: f.scrollNodes.map(n => n.scrollTop), expectedScrolls: f.expectedScrolls, listeners: [...f.listenerCounts] };
  });
  const assertInteraction = (value, id, result) => {
    assert.equal(result.projectId, id); assert.equal(result.sceneId, id + '-scene');
    for (const key of ['surface', 'canvas', 'input', 'focus']) assert.equal(result[key], true, 'Retain active ' + key);
    assert.equal(result.value, value); assert.deepEqual(result.selection, [3, 14, 'backward']);
    assert.deepEqual(result.scrolls, result.expectedScrolls); assert.ok(result.scrolls.every(n => n > 0));
    assert.deepEqual(result.listeners, [1, 1]);
  };

  const bgGate = await page.evaluate(id => window.fixture.hold('Product.CloseProjectCatalog', id), bId);
  await promptClose(bId); await choose('discard');
  await page.waitForFunction(key => window.fixture.gates.get(key)?.waiting, bgGate);
  await rememberInteraction('CURRENT-LATEST-BACKGROUND'); await latestInteraction();
  await page.evaluate(key => window.fixture.release(key), bgGate); await closed(bId);
  await page.waitForTimeout(80);
  report.backgroundContinuity = await interaction(); assertInteraction('CURRENT-LATEST-BACKGROUND', aId, report.backgroundContinuity);
  assert.equal(await page.locator(`[data-action="close-open-project"][data-project-id="${bId}"]`).count(), 0);
  report.checks.push('A delayed background close patches only tabs and retains the current scene/canvas/input/listeners plus latest focus, selection and four outer/nested scroll positions');
  await screenshot('01-background-close-retains-active-editor');

  const cId = await open('C'), dId = await open('D');
  await select(cId); await close(cId); await activeReady();
  assert.equal((await shell()).activeProjectId, dId, 'Closing the current middle project must select its right neighbour');
  await close(dId); await activeReady();
  assert.equal((await shell()).activeProjectId, aId, 'Closing the current rightmost project must select its left neighbour');
  report.checks.push('Closing the active tab selects the original right neighbour first and the left neighbour when no right neighbour remains');

  const manyIds = []; for (const name of ['E', 'F', 'G', 'H', 'I']) manyIds.push(await open(name));
  await select(aId); await reveal(manyIds.at(-1));
  assert.equal(await page.locator('.project-tabs .project-tab').count(), 3);
  assert.equal(await page.locator('[data-action="project-window-prev"]').isEnabled(), true);
  await close(manyIds.at(-1));
  assert.equal((await shell()).activeProjectId, aId);
  const visible = await page.locator('[data-action="select-open-project"]').evaluateAll(nodes => nodes.map(n => n.dataset.projectId));
  assert.equal(visible.length, 3); assert.ok(!visible.includes(manyIds.at(-1)));
  await select(aId);
  report.checks.push('More than three projects remain individually selectable/closable through the real tab arrows; closing the last page clamps the window and preserves the active project');

  const raceId = manyIds[0], raceDestination = manyIds[1];
  await select(raceId);
  const raceGate = await page.evaluate(id => window.fixture.hold('Product.CloseProjectCatalog', id), raceId);
  await promptClose(raceId); await choose('discard');
  await page.waitForFunction(key => window.fixture.gates.get(key)?.waiting, raceGate);
  await select(raceDestination); await rememberInteraction('USER-SWITCHED-LATEST'); await latestInteraction();
  await page.evaluate(key => window.fixture.release(key), raceGate); await closed(raceId); await page.waitForTimeout(80);
  report.navigationContinuity = await interaction(); assertInteraction('USER-SWITCHED-LATEST', raceDestination, report.navigationContinuity);
  report.checks.push('When the user selects another project during an active close reply, that project owns navigation and its live editor state is retained');

  const saveId = await create('SaveTarget'); await select(aId);
  await page.evaluate(() => window.fixture.saveResults.push('D:/ProjectCloseTests/SavedTarget.ictd'));
  await close(saveId, 'save');
  const saveRequests = await page.evaluate(id => window.fixture.requests.filter(r => r.method === 'Project.Save' && r.projectId === id), saveId);
  assert.equal(saveRequests.length, 1); assert.equal(saveRequests[0].scope, 'scene');
  assert.equal(saveRequests[0].payload.projectPath, 'D:/ProjectCloseTests/SavedTarget.ictd');
  assert.equal((await shell()).activeProjectId, aId);
  report.checks.push('Save and close persists the specific background project through its scene Project.Save channel, then closes its product catalogue without selecting it');

  const cancelSaveId = await create('CancelSave'); await select(aId);
  const closeBeforeSaveCancel = await requestCount('Product.CloseProjectCatalog');
  await page.evaluate(() => window.fixture.saveResults.push(null));
  await promptClose(cancelSaveId); await choose('save');
  await page.waitForFunction(id => !document.querySelector(`[data-action="close-open-project"][data-project-id="${id}"]`)?.disabled, cancelSaveId);
  assert.equal(await requestCount('Product.CloseProjectCatalog'), closeBeforeSaveCancel);
  assert.equal(await page.evaluate(id => window.fixture.product.projects.has(id), cancelSaveId), true);
  assert.equal((await shell()).activeProjectId, aId);
  report.checks.push('Cancelling the controlled save file choice leaves the project open and sends no native close');

  const failSaveId = await create('FailSave'); await select(aId);
  await page.evaluate(id => { window.fixture.saveResults.push('D:/ProjectCloseTests/FailSave.ictd'); window.fixture.saveErrors.set(id, 'Controlled save failure'); }, failSaveId);
  const closeBeforeSaveFailure = await requestCount('Product.CloseProjectCatalog');
  await promptClose(failSaveId); await choose('save');
  await page.waitForFunction(id => !document.querySelector(`[data-action="close-open-project"][data-project-id="${id}"]`)?.disabled, failSaveId);
  assert.equal(await requestCount('Product.CloseProjectCatalog'), closeBeforeSaveFailure);
  assert.equal(await page.evaluate(id => window.fixture.product.projects.has(id), failSaveId), true);
  assert.equal((await shell()).activeProjectId, aId);
  assert.equal((await shell()).error, '', 'A close-triggered save failure is logged without storing a stale shell error');
  assert.equal(await page.evaluate(() => window.fixture.requests.some(r => r.scope === 'scene' && r.method === 'Product.CloseProjectCatalog')), false);
  report.checks.push('A save failure retains the background project/current selection and prevents close; close operations never use a scene channel');

  const reopenId = await open('Reopen');
  await page.evaluate(id => {
    const v = window.fixture.getProjectView(id); v.tubeDesignerRightDraft = { productCode: 'STALE-CLOSED-DRAFT' };
    v.tubeDesignerPunchDraft = { test: 'stale' }; window.fixture.closedView = v;
  }, reopenId);
  const resourcesBeforeReopen = await requestCount('TubeDesigner.ListUserData');
  const sceneReadsBeforeReopen = await requestCount('TubeDesigner.List');
  await close(reopenId); await activeReady();
  assert.equal(await open('Reopen'), reopenId, 'Controlled reopened document retains its native project ID');
  assert.equal(await requestCount('TubeDesigner.ListUserData'), resourcesBeforeReopen);
  assert.equal(await requestCount('TubeDesigner.List'), sceneReadsBeforeReopen + 1);
  const reopened = await page.evaluate(id => {
    const v = window.fixture.getProjectView(id), e = window.fixture.entries.get(id);
    return { sameView: v === window.fixture.closedView, draft: v.tubeDesignerRightDraft,
      punchDraft: v.tubeDesignerPunchDraft, actualCode: v.scene.tubeDesigner.product.parameters.productCode,
      nativeCode: e.scene.tubeDesigner.product.parameters.productCode };
  }, reopenId);
  report.reopened = reopened;
  assert.equal(reopened.sameView, false, 'Closed project view must be released');
  assert.notEqual(reopened.draft?.productCode, 'STALE-CLOSED-DRAFT'); assert.ok(!reopened.punchDraft?.test);
  assert.equal(reopened.actualCode, reopened.nativeCode); assert.notEqual(reopened.actualCode, 'STALE-CLOSED-DRAFT');
  report.checks.push('Reopening the same project ID reads its fresh native scene and discards closed editor drafts while reusing the live product resource catalogue');

  // Native close failure must not erase the active editor either.
  const failCloseId = await open('FailClose'); await select(aId);
  const failGate = await page.evaluate(id => { window.fixture.closeErrors.set(id, 'Controlled close failure'); return window.fixture.hold('Product.CloseProjectCatalog', id); }, failCloseId);
  await promptClose(failCloseId); await choose('discard'); await page.waitForFunction(key => window.fixture.gates.get(key)?.waiting, failGate);
  await rememberInteraction('CLOSE-FAILED-LATEST'); await latestInteraction();
  await page.evaluate(key => window.fixture.release(key), failGate);
  await page.waitForFunction(id => !document.querySelector(`[data-action="close-open-project"][data-project-id="${id}"]`)?.disabled, failCloseId);
  await page.waitForTimeout(80);
  report.failureContinuity = await interaction(); assertInteraction('CLOSE-FAILED-LATEST', aId, report.failureContinuity);
  assert.equal(await page.evaluate(id => window.fixture.product.projects.has(id), failCloseId), true);
  assert.equal((await shell()).error, '', 'Native close failure is logged without a persistent shell error');
  await page.evaluate(() => { window.fixture.closeErrors.clear(); window.fixture.saveErrors.clear(); });
  report.checks.push('A delayed native close failure keeps the project open and preserves the current live scene/input/focus/latest scroll nodes');

  const closeBeforeGuard = await requestCount('Product.CloseProjectCatalog');
  await promptClose(failCloseId);
  await page.evaluate(id => { window.fixture.getProjectView(id).tubeDesignerOperation = {
    kind: 'export', title: 'Controlled pending export', completed: 0, total: 2 }; }, failCloseId);
  await choose('discard');
  await page.waitForFunction(id => !document.querySelector(`[data-action="close-open-project"][data-project-id="${id}"]`)?.disabled, failCloseId);
  assert.equal(await requestCount('Product.CloseProjectCatalog'), closeBeforeGuard, 'A task begun after the dialog opens must prevent native close');
  assert.equal(await page.evaluate(id => window.fixture.product.projects.has(id), failCloseId), true);
  assert.equal((await shell()).error, '', 'Close guard failure is logged without a persistent shell error');
  await page.evaluate(id => { window.fixture.getProjectView(id).tubeDesignerOperation = null; }, failCloseId);
  await close(failCloseId);
  assert.equal((await shell()).activeProjectId, aId);
  report.checks.push('A pending task started after the close dialog opens is rechecked before native close, and the same project closes normally after the task clears');

  const remainingIds = await page.evaluate(() => [...window.fixture.product.projects.keys()]);
  for (const id of remainingIds.filter(id => id !== initialId)) await close(id);
  await activeReady();
  assert.equal((await shell()).activeProjectId, initialId);
  assert.equal(await page.locator('.project-switcher.single-project [data-action="close-open-project"]').count(), 1);
  await close(initialId);
  await page.waitForFunction(() => window.__icaxAppShell.getState().startCenterOpen && !window.__icaxAppShell.getState().activeProjectId);
  assert.equal((await shell()).activeSceneId, '');
  assert.equal((await shell()).error, '', 'Last close must not display an earlier transient guard failure on the start centre');
  assert.equal(await page.locator('[data-product-surface="project"]').count(), 0);
  assert.equal(await page.locator('[data-action="select-open-project"]').count(), 0);
  await page.locator('.start-center-page').waitFor({ state: 'visible' });
  assert.equal(await page.locator('.start-center-page').innerText().then(text => text.includes('加工文件正在导出')), false);
  report.checks.push('Closing the final project clears project/scene identity and returns to the recent-project start centre with no lingering scene surface');
  await screenshot('02-last-project-closed-start-centre');

  assert.deepEqual(errors, []);
  report.projects = { initialId, aId, bId, manyIds, raceId, raceDestination, saveId, cancelSaveId, failSaveId, reopenId, failCloseId };
  report.status = 'passed';
  console.log('AppShell project close actual browser PASS: ' + report.checks.length + ' groups. ' + output);
} catch (error) {
  report.status = 'failed'; report.error = error.message; report.stack = error.stack;
  if (page) {
    const path = resolve(output, 'failure.png'); await page.screenshot({ path }).catch(() => {}); report.screenshots.push(path);
    report.failureState = await page.evaluate(() => ({ shell: window.__icaxAppShell?.getState(),
      requests: window.fixture?.requests, gates: [...window.fixture?.gates.values() ?? []].map(g => ({ key: g.key, waiting: g.waiting })) })).catch(() => null);
  }
  throw error;
} finally {
  report.browserErrors = errors;
  if (page) report.requests = await page.evaluate(() => window.fixture?.requests ?? []).catch(() => []);
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  await browser.close();
}
