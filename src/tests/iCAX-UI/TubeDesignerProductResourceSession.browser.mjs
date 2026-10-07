// Actual AppShell -> TubeDesigner entry -> workbench DOM. Native transport is
// deliberately controlled; no production renderer, patch or resource cache is mocked.
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('tube-designer-product-resource-session');
mkdirSync(output, { recursive: true });
const rawTemplate = JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/template.json'));
const rawProfile = JSON.parse(readBrowserAsset('apps/tube-designer/templates/profile/round/profile.json'));
rawProfile.display = JSON.parse(readBrowserAsset('apps/tube-designer/templates/profile/round/display.json'));
const report = { status: 'running', checks: [], screenshots: [],
  scope: 'Production AppShell, TubeDesigner entry, workbench and DOM modules in a real browser. Scope-aware native transport replies are controlled fixtures. No user projects or standalone CEF application were operated.' };
const errors = [];
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  page.setDefaultTimeout(45000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://product-resource-session.test/**', async route => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript',
      body: 'export async function connectApplication(){return window.fixture.appProxy;}' });
    if (path === '/src/iCAX-UI/SDK/AppShell/index.html') return route.fulfill({ contentType: 'text/html',
      body: readBrowserAsset('iCAX-UI/SDK/AppShell/index.html').replace(/<script type="module"[^>]*><\/script>/, '') });
    if (path.startsWith('/src/') && /\.(css|svg)$/.test(path)) return route.fulfill({
      contentType: path.endsWith('.css') ? 'text/css' : 'image/svg+xml', body: readBrowserAsset(path.slice('/src/'.length)) });
    return serveBrowserAsset(route);
  });
  await page.goto('http://product-resource-session.test/src/iCAX-UI/SDK/AppShell/index.html');
  await page.evaluate(async ({ rawTemplate, rawProfile }) => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const normalize = path => String(path ?? '').replace(/\\/g, '/').toLowerCase();
    const schema = { ...rawTemplate, available: true, descriptorLoaded: true, name: catalogText(rawTemplate.displayName),
      groups: rawTemplate.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: rawTemplate.parameters.map(field => ({ ...field, type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const parameters = Object.fromEntries(schema.parameters.map(field => [field.key, field.defaultValue]));
    const gates = new Map(), requests = [], openCalls = [], products = new Map(), projects = new Map();
    const f = window.fixture = { getProjectView, gates, requests, openCalls, products, projects, opens: [], failures: [] };
    const gate = (owner, method, projectId = '') => {
      const key = [owner, method, projectId].join('|'); let release;
      const promise = new Promise(done => { release = done; });
      const entry = { key, owner, method, projectId, promise, release, waiting: false }; gates.set(key, entry); return key;
    };
    f.hold = gate;
    f.release = key => { const entry = gates.get(key); if (!entry) throw new Error('Missing gate ' + key); entry.release(); };
    const request = async (owner, scope, method, payload, projectId = '') => {
      requests.push({ owner, scope, method, projectId, payload: structuredClone(payload ?? {}) });
      const entry = gates.get([owner, method, projectId].join('|')) ?? gates.get([owner, method, ''].join('|'));
      if (entry) { entry.waiting = true; await entry.promise; entry.waiting = false; gates.delete(entry.key); }
    };
    let projectSequence = 0;
    function makeProject(product, path, name) {
      const index = ++projectSequence, projectId = `resource-project-${index}`, sceneId = `resource-scene-${index}`;
      const template = { id: schema.id, version: product.catalogVersion ?? 'resource-test-v1', name: product.owner + ' catalogue', available: true };
      const instances = Array.from({ length: 24 }, (_, i) => ({ entityId: `${projectId}-product-${i}`, name: `${name} ${i + 1}`,
        templateId: schema.id, quantity: 1, parameters: { ...parameters, productCode: `${name}-CODE` } }));
      const scene = { sceneId, undoRedo: { revision: 1, canUndo: false }, tubeDesigner: {
        templates: [template], instances, product: instances[0], activeProductId: instances[0].entityId,
        members: [], joints: [], parts: [], manufacturingGroups: [], nestingGroups: [] } };
      const snapshot = { viewId: 'resource-view-' + index, revision: '1', rows: [] };
      const sceneProxy = { state: scene, pdo: { enabled: false }, resources: { get() { throw new Error('Empty scene requested geometry'); } },
        views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
        async getState() { await request(product.owner, 'scene', 'Scene.GetState', {}, projectId); return structuredClone(scene); },
        async invoke(method, payload = {}) {
          await request(product.owner, 'scene', method, payload, projectId);
          if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(scene.tubeDesigner) };
          if (method === 'TubeDesigner.GetTemplateDescriptor' || method === 'TubeDesigner.GetProductTemplateDescriptor')
            return { template: { ...structuredClone(schema), version: template.version, resourceOwner: product.owner } };
          if (method === 'TubeDesigner.GetPunchTools') return { tools: [{ id: product.owner + '-circle', displayName: '圆孔',
            libraryScope: 'system', available: true, resourceOwner: product.owner }] };
          throw new Error('Unexpected scene request ' + method);
        } };
      const state = { projectId, projectName: name, projectPath: path, mainScene: scene };
      const proxy = { projectId, state, getMainScene() { return sceneProxy; } };
      const entry = { owner: product.owner, product, projectId, sceneId, state, scene, proxy, sceneProxy };
      product.projects.set(projectId, proxy); projects.set(projectId, entry);
      product.state.catalogs.push({ mainProject: state }); return entry;
    }
    function makeProduct(owner, productId) {
      const state = { productId, productName: owner + ' TubeDesigner', isStarted: true, productChannelId: owner + '-product-channel',
        frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs', catalogs: [], projectFile: { fileExtensions: ['ictd'] },
        recentProjects: [{ path: 'D:/ResourceTests/A.ictd', displayName: 'A' }, { path: 'D:/ResourceTests/B.ictd', displayName: 'B' }] };
      const proxy = { owner, productId, productChannelId: state.productChannelId, state, projects: new Map(),
        getProject(id) { return this.projects.get(id); }, async getState() { return state; },
        async openProjectCatalog(path) {
          const entry = makeProject(this, path, path ? path.split('/').at(-1) : '初始项目');
          return { projectProxy: entry.proxy, sceneProxy: entry.sceneProxy, catalog: { mainProject: entry.state } };
        },
        async invoke(method, payload = {}) {
          await request(owner, 'product', method, payload);
          if (method === 'TubeDesigner.ListUserData') return { systemProfiles: [{ id: 'round', name: owner + ' 圆管', libraryScope: 'system',
            descriptor: rawProfile, defaultParameters: Object.fromEntries(rawProfile.parameters.map(p => [p.key, p.defaultValue])), resourceOwner: owner }],
            profiles: [], templateProfiles: [], customers: [{ id: owner + '-customer', name: owner }], parameterPresets: [], punchTools: [], productTemplates: [] };
          if (method === 'TubeDesigner.ScanBatchExcelAutomation') return { workbooks: [], files: [], pending: [] };
          throw new Error('Unexpected product request ' + method);
        } };
      products.set(productId, proxy); return proxy;
    }
    const a = makeProduct('A', 'icax.tube-designer');
    const b = makeProduct('B', 'resource-other-product');
    const c = makeProduct('C', 'resource-late-product');
    f.appProxy = { products, bridge: { async windowCommand() {} },
      getProduct(id) { return products.get(id); }, async startProduct(id) { return products.get(id); },
      async getState() { return { products: [...products.values()].map(product => product.state) }; },
      async openProjectFile(path) {
        const owner = normalize(path).includes('/other') ? b : normalize(path).includes('/late') ? c : a;
        openCalls.push({ owner: owner.owner, path });
        const entry = makeProject(owner, path, path.replace(/\\/g, '/').split('/').at(-1));
        return { productProxy: owner, product: owner.state, projectProxy: entry.proxy, sceneProxy: entry.sceneProxy,
          catalog: { mainProject: entry.state }, projectPath: path };
      } };
    f.open = (path, productId = a.productId) => {
      const job = window.__icaxAppShell.openProject(path, productId).catch(error => { f.failures.push(error.message); throw error; });
      f.opens.push(job); return job;
    };
    f.waitIdle = async () => { await Promise.all(f.opens); };
    f.waitProject = async id => {
      const v = getProjectView(id);
      await v.tubeDesignerSynchronizationPromise; await v.tubeDesignerUserDataSynchronizationPromise;
      await v.tubeDesignerUserDataRefreshPromise;
    };
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, { rawTemplate, rawProfile });
  const shell = () => page.evaluate(() => window.__icaxAppShell.getState());
  const code = page.locator('[data-tube-designer-parameter="productCode"]');
  const activeReady = async () => {
    await page.waitForFunction(() => {
      const s = window.__icaxAppShell.getState(), v = window.fixture.getProjectView(s.activeProjectId);
      return s.startupPhase === 'ready' && !s.pendingCount && v.tubeDesignerLoaded && !v.tubeDesignerLoading
        && !v.tubeDesignerUserDataSynchronizationPromise && !v.tubeDesignerUserDataRefreshPromise;
    });
    await code.waitFor({ state: 'visible' });
  };
  const counts = () => page.evaluate(() => Object.fromEntries(['A', 'B', 'C'].map(owner => [owner,
    Object.fromEntries(['TubeDesigner.ListUserData', 'TubeDesigner.GetPunchTools', 'TubeDesigner.GetTemplateDescriptor'].map(method =>
      [method, window.fixture.requests.filter(r => r.owner === owner && r.method === method).length]))])));
  const screenshot = async name => { const path = resolve(output, name + '.png'); await page.screenshot({ path }); report.screenshots.push(path); };
  const selectProject = async id => {
    await page.evaluate(id => {
      const selector = `[data-action="select-open-project"][data-project-id="${id}"]`;
      // The actual shell displays three project tabs at a time.
      while (!document.querySelector(selector)) {
        const prev = document.querySelector('[data-action="project-window-prev"]');
        if (!prev || prev.disabled) break;
        prev.click();
      }
      while (!document.querySelector(selector)) {
        const next = document.querySelector('[data-action="project-window-next"]');
        if (!next || next.disabled) throw new Error('Requested project is missing from actual AppShell tabs');
        next.click();
      }
      document.querySelector(selector).click();
    }, id);
    await page.waitForFunction(id => window.__icaxAppShell.getState().activeProjectId === id, id);
    await code.waitFor({ state: 'visible' });
  };
  await activeReady();
  report.initialCounts = await counts();
  assert.deepEqual(Object.values(report.initialCounts.A), [1, 1, 1], 'First product session must load each resource exactly once, including no second background user-data request');
  const startupId = (await shell()).activeProjectId;
  report.checks.push('Initial AppShell startup loads the actual product descriptor/profile/tool catalogues exactly once');

  await page.evaluate(() => window.fixture.open('D:/ResourceTests/A.ictd')); await activeReady();
  const aId = (await shell()).activeProjectId;
  await page.evaluate(() => window.fixture.open('D:/ResourceTests/B.ictd')); await activeReady();
  const bId = (await shell()).activeProjectId;
  report.warmCounts = await counts();
  assert.deepEqual(report.warmCounts.A, report.initialCounts.A, 'Two new projects of the same live product must reuse successful resources');
  assert.notEqual(aId, bId); assert.notEqual(startupId, aId);
  await page.evaluate(async aId => {
    const f = window.fixture, a = f.getProjectView(aId), b = f.getProjectView(window.__icaxAppShell.getState().activeProjectId);
    f.independent = { profiles: a.tubeDesignerSystemProfiles !== b.tubeDesignerSystemProfiles,
      profileRecord: a.tubeDesignerSystemProfiles[0] !== b.tubeDesignerSystemProfiles[0],
      userData: a.tubeDesignerUserData !== b.tubeDesignerUserData,
      descriptor: a.tubeDesignerTemplateDescriptors !== b.tubeDesignerTemplateDescriptors };
    b.tubeDesignerSystemProfiles[0].name = 'B local draft';
    b.tubeDesignerUserData.customers[0].name = 'B local customer';
    f.independent.originalProfile = a.tubeDesignerSystemProfiles[0].name;
    f.independent.originalCustomer = a.tubeDesignerUserData.customers[0].name;
  }, aId);
  report.independent = await page.evaluate(() => window.fixture.independent);
  for (const key of ['profiles', 'profileRecord', 'userData', 'descriptor']) assert.equal(report.independent[key], true, key + ' must be per-view mutable state');
  assert.equal(report.independent.originalProfile, 'A 圆管'); assert.equal(report.independent.originalCustomer, 'A');
  report.checks.push('Same-product projects reuse one resource session while keeping independent catalogue objects, personal data and editing state');

  const nativeOpens = await page.evaluate(() => window.fixture.openCalls.length);
  await page.evaluate(() => window.fixture.open('d:\\resourcetests\\a.ICTD')); await activeReady();
  assert.equal((await shell()).activeProjectId, aId);
  assert.equal(await page.evaluate(() => window.fixture.openCalls.length), nativeOpens, 'Opening an already-open path must select its project without native OpenProjectFile');
  assert.deepEqual((await counts()).A, report.initialCounts.A);
  report.checks.push('Recent-file path slash/case variants select the existing project without repeating native open or resource discovery');

  await page.evaluate(() => window.fixture.open('D:/ResourceTests/Other.ictd', 'resource-other-product')); await activeReady();
  const otherId = (await shell()).activeProjectId;
  report.otherCounts = await counts();
  assert.deepEqual(Object.values(report.otherCounts.B), [1, 1, 1], 'A distinct live product proxy must have its own catalogues');
  assert.equal(await page.evaluate(() => window.fixture.getProjectView(window.__icaxAppShell.getState().activeProjectId).tubeDesignerSystemProfiles[0].resourceOwner), 'B');
  assert.equal(await page.evaluate(() => Object.values(window.fixture.getProjectView(window.__icaxAppShell.getState().activeProjectId).tubeDesignerTemplateDescriptors)[0].resourceOwner), 'B');
  report.checks.push('A different product proxy loads and exposes its own descriptors/profile/tool catalogues without borrowing another product session');

  // Cold C is suspended on actual resource transport while D, another project
  // of the same product, mounts. Only one session request may be in flight.
  const gateKey = await page.evaluate(() => window.fixture.hold('C', 'TubeDesigner.ListUserData'));
  await page.evaluate(() => { void window.fixture.open('D:/ResourceTests/Late-C.ictd', 'resource-late-product'); });
  await page.waitForFunction(key => window.fixture.gates.get(key)?.waiting, gateKey);
  const lateId = (await shell()).activeProjectId;
  await page.evaluate(() => { void window.fixture.open('D:/ResourceTests/Late-D.ictd', 'resource-late-product'); });
  await page.waitForFunction(id => window.__icaxAppShell.getState().activeProjectId !== id && window.__icaxAppShell.getState().activeProductId === 'resource-late-product', lateId);
  const lateDId = (await shell()).activeProjectId;
  await page.waitForTimeout(150);
  assert.equal((await counts()).C['TubeDesigner.ListUserData'], 1, 'Concurrent projects must join the product resource request');
  // Dispatch the actual AppShell action while the controlled cold-resource
  // overlay is held. This tests late ownership, not the overlay's hit testing.
  await selectProject(aId);
  await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{height:360px!important;overflow:auto!important}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:650px}.tube-designer-instance-list{height:230px!important;overflow:auto!important;display:block!important;flex:none!important}.tube-designer-instance-list:after{content:"";display:block;height:800px}.tube-designer-parameter-scroll{height:500px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:800px}' });
  await code.focus(); await code.fill('LATEST-PROJECT-DRAFT');
  await page.evaluate(() => {
    const f = window.fixture; f.code = document.querySelector('[data-tube-designer-parameter="productCode"]');
    f.code.setSelectionRange(2, 12, 'backward');
    f.scrollNodes = ['.cam-context-pane', '.tube-designer-instance-list', '.cam-info-pane', '.tube-designer-parameter-scroll'].map(s => document.querySelector(s));
    f.scrollNodes.forEach((node, i) => { node.scrollTop = 100 + i * 30; });
    f.scrolls = f.scrollNodes.map(node => node.scrollTop);
    f.surface = document.querySelector('[data-product-surface="project"]'); f.canvas = f.surface.querySelector('canvas');
    f.inputEvents = 0; f.canvasEvents = 0;
    f.code.addEventListener('test-retained', () => f.inputEvents++); f.canvas.addEventListener('test-retained', () => f.canvasEvents++);
  });
  await page.evaluate(key => window.fixture.release(key), gateKey);
  await page.evaluate(() => window.fixture.waitIdle());
  await page.evaluate(async ids => { for (const id of ids) await window.fixture.waitProject(id); }, [lateId, lateDId]);
  await page.waitForTimeout(100);
  report.continuity = await page.evaluate(() => {
    const f = window.fixture, shell = window.__icaxAppShell.getState();
    f.code.dispatchEvent(new Event('test-retained')); f.canvas.dispatchEvent(new Event('test-retained'));
    return { projectId: shell.activeProjectId, sceneId: shell.activeSceneId,
      productId: shell.activeProductId, code: f.code === document.querySelector('[data-tube-designer-parameter="productCode"]'),
      focus: document.activeElement === f.code, value: f.code.value,
      selection: [f.code.selectionStart, f.code.selectionEnd, f.code.selectionDirection],
      surface: f.surface === document.querySelector('[data-product-surface="project"]'), canvas: f.canvas === document.querySelector('[data-product-surface="project"] canvas'),
      scrolls: f.scrollNodes.map(node => node.scrollTop), expectedScrolls: f.scrolls, listeners: [f.inputEvents, f.canvasEvents] };
  });
  assert.equal(report.continuity.projectId, aId); assert.equal(report.continuity.productId, 'icax.tube-designer');
  for (const key of ['code', 'focus', 'surface', 'canvas']) assert.equal(report.continuity[key], true, 'Late other-product callback must retain ' + key);
  assert.equal(report.continuity.value, 'LATEST-PROJECT-DRAFT'); assert.deepEqual(report.continuity.selection, [2, 12, 'backward']);
  assert.deepEqual(report.continuity.scrolls, report.continuity.expectedScrolls); assert.ok(report.continuity.scrolls.every(n => n > 0));
  assert.deepEqual(report.continuity.listeners, [1, 1]);
  report.checks.push('Concurrent same-product cold loads share one in-flight resource request; late responses from inactive projects do not remount the current project or steal its latest focus/caret/scroll/nodes/listeners');
  await screenshot('01-warm-project-after-late-other-product-resource');
  await selectProject(lateDId); await activeReady();
  report.finalCounts = await counts();
  assert.deepEqual(Object.values(report.finalCounts.C), [1, 1, 1], 'Joined resource session must install one catalogue of each kind');
  assert.equal(await page.evaluate(() => window.fixture.getProjectView(window.__icaxAppShell.getState().activeProjectId).tubeDesignerSystemProfiles[0].resourceOwner), 'C');
  report.checks.push('Returning to the joined product project installs its own complete resources without another discovery call');
  report.lateReplies = [];
  for (const method of ['TubeDesigner.List', 'TubeDesigner.GetTemplateDescriptor']) {
    const key = await page.evaluate(method => {
      const f = window.fixture;
      if (method === 'TubeDesigner.GetTemplateDescriptor') f.products.get('icax.tube-designer').catalogVersion = 'resource-test-v2';
      return f.hold('A', method);
    }, method);
    await page.evaluate(method => { void window.fixture.open('D:/ResourceTests/' + method.split('.').at(-1) + '-race.ictd'); }, method);
    await page.waitForFunction(key => window.fixture.gates.get(key)?.waiting, key);
    const requesterId = (await shell()).activeProjectId;
    await selectProject(lateDId); await activeReady();
    await code.focus(); await code.fill('LATEST-' + method);
    await page.evaluate(() => {
      const f = window.fixture; f.code = document.querySelector('[data-tube-designer-parameter="productCode"]');
      f.code.setSelectionRange(1, 8, 'backward');
      f.surface = document.querySelector('[data-product-surface="project"]'); f.canvas = f.surface.querySelector('canvas');
      f.scrollNodes = ['.cam-context-pane', '.tube-designer-instance-list', '.cam-info-pane', '.tube-designer-parameter-scroll'].map(s => document.querySelector(s));
      f.scrollNodes.forEach((node, i) => { node.scrollTop = 115 + i * 25; }); f.scrolls = f.scrollNodes.map(n => n.scrollTop);
    });
    await page.evaluate(key => window.fixture.release(key), key);
    await page.evaluate(() => window.fixture.waitIdle()); await page.evaluate(id => window.fixture.waitProject(id), requesterId);
    await page.waitForTimeout(80);
    const state = await page.evaluate(() => {
      const f = window.fixture;
      return { shell: window.__icaxAppShell.getState(), input: f.code === document.querySelector('[data-tube-designer-parameter="productCode"]'),
        focus: document.activeElement === f.code, value: f.code.value, selection: [f.code.selectionStart, f.code.selectionEnd, f.code.selectionDirection],
        surface: f.surface === document.querySelector('[data-product-surface="project"]'), canvas: f.canvas === document.querySelector('[data-product-surface="project"] canvas'),
        scroll: f.scrollNodes.map(n => n.scrollTop), expectedScroll: f.scrolls };
    });
    assert.equal(state.shell.activeProjectId, lateDId); assert.equal(state.shell.activeProductId, 'resource-late-product');
    for (const key of ['input', 'focus', 'surface', 'canvas']) assert.equal(state[key], true, method + ' stale reply ' + key);
    assert.equal(state.value, 'LATEST-' + method); assert.deepEqual(state.selection, [1, 8, 'backward']);
    assert.deepEqual(state.scroll, state.expectedScroll); assert.ok(state.scroll.every(n => n > 0));
    report.lateReplies.push({ method, requesterId, state });
    report.checks.push(method + ' late reply cannot replace a different active product/project, its input selection, latest nested scroll or canvas');
  }
  report.countsAfterPackageVersionChange = await counts();
  assert.deepEqual(Object.values(report.countsAfterPackageVersionChange.A), [1, 1, 2],
    'Only the deliberately changed product package version may fetch one fresh descriptor; profiles and tools remain reused');
  assert.deepEqual(report.countsAfterPackageVersionChange.B, report.finalCounts.B);
  assert.deepEqual(report.countsAfterPackageVersionChange.C, report.finalCounts.C);
  report.projects = { startupId, aId, bId, otherId, lateId, lateDId };
  assert.deepEqual(errors, []); assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
  report.status = 'passed';
  console.log('TubeDesigner product resource session actual browser PASS: ' + report.checks.length + ' groups. ' + output);
} catch (error) {
  report.status = 'failed'; report.error = error.message; report.stack = error.stack;
  if (page) {
    const path = resolve(output, 'failure.png'); await page.screenshot({ path }).catch(() => {}); report.screenshots.push(path);
    report.failureState = await page.evaluate(() => ({ shell: window.__icaxAppShell?.getState(),
      requests: window.fixture?.requests, failures: window.fixture?.failures,
      gates: [...window.fixture?.gates?.values() ?? []].map(g => ({ key: g.key, waiting: g.waiting })) })).catch(() => null);
  }
  throw error;
} finally {
  report.browserErrors = errors;
  if (page) report.requests = await page.evaluate(() => window.fixture?.requests ?? []).catch(() => []);
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  await browser.close();
}
