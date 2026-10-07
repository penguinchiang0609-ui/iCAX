// Actual AppShell startup and TubeDesigner DOM. The scope-checked native
// transport only supplies deterministic read data and licence responses.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('tube-designer-license-startup');
mkdirSync(output, { recursive: true });
const rawTemplate = JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/template.json'));
const rawProfile = JSON.parse(readBrowserAsset('apps/tube-designer/templates/profile/round/profile.json'));
rawProfile.display = JSON.parse(readBrowserAsset('apps/tube-designer/templates/profile/round/display.json'));
const rawAssembly = JSON.parse(readBrowserAsset('apps/tube-designer/templates/assembly/wrap-a-over-b/assembly.json'));
const features = JSON.parse(readFileSync(new URL('../../licensing/features.json', import.meta.url))).features;
const protectedSceneWrites = /(?:GeneratePreview|GenerateProduct|UpdateProduct|DeleteProduct|Disassemble|Export|RunNesting|CalculateNesting)/;
const report = { status: 'running', checks: [], scenarios: [], scope: 'Fresh real-browser pages use the actual AppShell startup, ProductProxy, ProjectProxy and TubeDesigner UI. Native reads, access receipts, licence responses and file/directory pickers are scope-checked protocol fixtures. The real contact JPG is served from the selected production asset root. No standalone CEF, TPM or actual machine activation is exercised.' };
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
// Keep a neutral page open while each scenario gets an isolated browser
// context without recycling the native transport or module state.
const browserAnchor = await browser.newPage();
let page;
let currentScenario;

async function freshStartup(configuration) {
  page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  page.setDefaultTimeout(30000);
  currentScenario = { name: configuration.name, browserErrors: [], checks: [], requests: [] };
  report.scenarios.push(currentScenario);
  page.on('pageerror', error => currentScenario.browserErrors.push(error.message));
  await page.route('http://license-startup.test/**', async route => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){return window.fixture.appProxy;}' });
    if (path === '/src/iCAX-UI/SDK/AppShell/index.html') return route.fulfill({ contentType: 'text/html', body: readBrowserAsset('iCAX-UI/SDK/AppShell/index.html').replace(/<script type="module"[^>]*><\/script>/, '') });
    if (path.startsWith('/src/') && /\.(css|svg|jpg)$/.test(path)) return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : path.endsWith('.jpg') ? 'image/jpeg' : 'image/svg+xml', body: readFileSync(browserAssetPath(path.slice(5))) });
    return serveBrowserAsset(route);
  });
  await page.goto('http://license-startup.test/src/iCAX-UI/SDK/AppShell/index.html');
  await page.evaluate(async ({ rawTemplate, rawProfile, rawAssembly, features, configuration }) => {
    const { ProductProxy } = await import('/src/iCAX-UI/ProductProxy/ProductProxy.mjs');
    const { ProjectProxy } = await import('/src/iCAX-UI/ProjectProxy/ProjectProxy.mjs');
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const schema = { ...rawTemplate, available: true, descriptorLoaded: true, version: 'licence-startup-test-v1', name: catalogText(rawTemplate.displayName),
      groups: rawTemplate.groups.map(g => ({ ...g, displayName: catalogText(g.displayName) })),
      parameters: rawTemplate.parameters.map(p => ({ ...p, type: { enum: 'select', string: 'text' }[p.valueType] ?? p.valueType,
        displayName: catalogText(p.displayName), groupKey: p.group, options: p.choices?.map(c => ({ ...c, label: catalogText(c.displayName) })) })) };
    const parameters = Object.fromEntries(schema.parameters.map(p => [p.key, p.defaultValue]));
    const f = window.fixture = { getProjectView, requests: [], sequence: 0, activeGrants: configuration.grants ?? [], activationGrants: [],
      developmentBypass: Boolean(configuration.developmentBypass), statusFailure: configuration.statusFailure, holdActivation: false, forbidden: [] };
    f.status = () => ({ activated: f.activeGrants.length > 0, developmentBypass: f.developmentBypass, kind: '永久授权', licenseId: 'browser-license',
      message: configuration.message ?? '', featureSchemaVersion: 1, featureCatalog: features.map(({ id, label, parent }) => ({ id, label, parent })),
      capabilities: Object.fromEntries(features.map(p => [p.id, f.activeGrants.includes(p.id) && (!p.parent || f.activeGrants.includes(p.parent))])) });
    const nativeProduct = { productId: 'icax.tube-designer', productName: 'TubeDesigner', isStarted: true, productChannelId: 'startup-product-channel',
      frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs', catalogs: [], recentProjects: [], projectFile: { fileExtensions: ['ictd'] } };
    const request = (scope, method, payload = {}) => f.requests.push({ scope, method, payload: structuredClone(payload),
      area: window.__icaxAppShell?.getState().activeRibbonTabId ?? '', projectId: window.__icaxAppShell?.getState().activeProjectId ?? '' });
    const deny = method => { f.forbidden.push(method); throw Error('Fixture rejected unauthorized or wrong-scope business request: ' + method); };
    const bridge = { async windowCommand() {}, async registerSceneChannel(id) { return id + '-scene-channel'; },
      async openFileDialog(options) {
        f.authorizationDialog = structuredClone(options);
        return f.authorizationPath || 'D:/LicenseStartupTest/activation.tdact';
      }, async openDirectoryDialog() { return 'D:/LicenseStartupTest'; } };
    const transport = { bridge, stop() {}, async invoke(channel, method, payload = {}) {
      if (channel !== nativeProduct.productChannelId) throw Error('Wrong product channel');
      request('product', method, payload);
      if (method === 'Product.GetState') return structuredClone(nativeProduct);
      if (method === 'Product.OpenProjectCatalog') { const entry = makeProject(); return { state: structuredClone(nativeProduct), catalog: structuredClone(entry.catalog) }; }
      if (method === 'TubeDesignerLicensing.Status') { if (f.statusFailure) throw Error(f.statusFailure); return f.status(); }
      if (method === 'TubeDesignerLicensing.CheckAccess') { if (!f.status().capabilities[payload.featureId]) throw Error('原生授权已失效'); return { granted: true, featureId: payload.featureId }; }
      if (method === 'TubeDesignerLicensing.Activate') {
        if (f.holdActivation) { f.activationWaiting = true; await new Promise(done => { f.releaseActivation = done; }); f.activationWaiting = false; }
        f.statusFailure = ''; f.activeGrants = [...f.activationGrants]; return f.status();
      }
      if (method === 'TubeDesignerLicensing.Request' || method === 'TubeDesignerLicensing.RequestTrial') return { path: payload.path };
      if (method === 'TubeDesignerLicensing.ExportLicenseFile') {
        if (!f.status().activated && !f.developmentBypass) throw Error('本机尚无可导出的授权文件');
        if (f.holdExport) { f.exportWaiting = true; await new Promise(done => { f.releaseExport = done; }); f.exportWaiting = false; }
        return { created: true };
      }
      // Inspecting an unlicensed workspace is deliberately permitted. Scene
      // methods still cannot be routed through the product channel.
      if (method === 'TubeDesigner.ListUserData') return { systemProfiles: [{ id: 'round', name: '圆管', libraryScope: 'system', descriptor: rawProfile,
        defaultParameters: Object.fromEntries(rawProfile.parameters.map(p => [p.key, p.defaultValue])) }], profiles: [], templateProfiles: [], customers: [], parameterPresets: [], punchTools: [], productTemplates: [] };
      if (method === 'TubeDesigner.ScanBatchExcelAutomation') {
        if (!['product.design', 'product.breakdown', 'product.export'].every(id => f.status().capabilities[id])) return deny(method);
        return { workbooks: [], files: [], pending: [] };
      }
      return deny(method);
    } };
    const product = f.product = new ProductProxy(transport, structuredClone(nativeProduct), { bridge });
    function makeProject() {
      const projectId = 'startup-project-' + (++f.sequence), sceneId = projectId + '-scene';
      const instances = Array.from({ length: 24 }, (_, index) => ({ entityId: projectId + '-instance-' + index, name: '测试产品 ' + index,
        templateId: schema.id, quantity: 1, parameters: { ...parameters, productCode: 'TD-STARTUP-' + index } }));
      const scene = { sceneId, sceneChannelId: projectId + '-scene-channel', undoRedo: { revision: 1, canUndo: false }, tubeDesigner: {
        templates: [{ id: schema.id, version: schema.version, name: schema.name, available: true }], instances, product: instances[0], activeProductId: instances[0].entityId,
        members: [], joints: [], parts: [], manufacturingGroups: [], nestingGroups: [] } };
      const state = { projectId, projectName: '启动授权回归 ' + f.sequence, projectPath: '', mainScene: scene };
      const catalog = { catalogId: 'startup-catalog-' + projectId, mainProject: state };
      const snapshot = { viewId: projectId + '-view', revision: '1', rows: [] };
      const sceneProxy = { state: scene, sceneId, sceneChannelId: scene.sceneChannelId, pdo: { enabled: false }, resources: { get() { throw Error('Empty geometry fixture'); } },
        views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } }, updateState(next) { this.state = next; }, dispose() {},
        async getState() { request('scene', 'Scene.GetState'); return structuredClone(scene); }, async invoke(method, payload = {}) {
          request('scene', method, payload);
          if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(scene.tubeDesigner) };
          if (method === 'TubeDesigner.GetTemplateDescriptor' || method === 'TubeDesigner.GetProductTemplateDescriptor') return { template: structuredClone(schema) };
          if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
          if (method === 'TubeDesigner.GetAssemblyTemplates') return { assemblies: [structuredClone(rawAssembly)], errors: [] };
          if (method === 'TubeDesigner.MeasurePartGeometry') return { available: false, message: 'Geometry is outside the licence transport fixture' };
          if (method === 'TubeDesigner.UpdateProductParameters' && f.status().capabilities['product.design']) {
            Object.assign(scene.tubeDesigner.product.parameters, payload.parameters); return { tubeDesigner: structuredClone(scene.tubeDesigner) };
          }
          return deny(method);
        } };
      const proxy = new ProjectProxy(transport, state, { bridge, product });
      proxy.scenes.set(sceneId, sceneProxy); proxy.mainSceneProxy = sceneProxy; product.projects.set(projectId, proxy); nativeProduct.catalogs.push(catalog);
      return { catalog, proxy, sceneProxy };
    }
    f.appProxy = { products: new Map([[product.productId, product]]), bridge, getProduct(id) { return id === product.productId ? product : null; },
      async startProduct() { return product; }, async getState() { return { products: [structuredClone(nativeProduct)] }; } };
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, { rawTemplate, rawProfile, rawAssembly, features, configuration });
  await page.waitForFunction(() => {
    const s = window.__icaxAppShell?.getState();
    if (!s || s.startupPhase !== 'ready' || s.pendingCount) return false;
    const v = window.fixture.getProjectView(s.activeProjectId);
    return v.tubeDesignerLoaded && !v.tubeDesignerLoading && !v.tubeDesignerUserDataSynchronizationPromise;
  });
  await page.locator('[data-startup-screen]').waitFor({ state: 'detached' });
  assert.equal((await page.evaluate(() => window.__icaxAppShell.getState())).activeRibbonTabId, 'view');
  assert.equal(await page.locator('[data-tube-designer-locked-area]').count(), 0);
  assert.equal(await page.locator('dialog:visible').count(), 0);
  assert.ok(await page.locator('.tube-designer-instance-list').count());
  assert.deepEqual(await page.locator('[data-action="select-ribbon-tab"]').evaluateAll(nodes => nodes.map(node => node.dataset.tabId)), ['view', 'resources', 'about']);
  for (const tab of await page.locator('[data-action="select-ribbon-tab"]').all()) {
    assert.equal(await tab.isDisabled(), false);
    assert.notEqual(await tab.getAttribute('aria-disabled'), 'true');
  }
  currentScenario.checks.push('A fresh startup enters the ordinary product workspace, with inspectable instance data, no locked-area replacement or modal; every exposed primary tab remains clickable');
  await page.screenshot({ path: resolve(output, configuration.name + '-startup.png') });
}

const tab = id => page.locator(`[data-action="select-ribbon-tab"][data-tab-id="${id}"]`);
const command = id => page.locator(`[data-action="ribbon-command"][data-command-id="${id}"]`);
const code = () => page.locator('[data-tube-designer-parameter="productCode"]');
async function expectAboutContact() {
  await tab('about').click();
  await page.waitForFunction(() => window.__icaxAppShell.getState().activeRibbonTabId === 'about');
  const image = page.locator('[data-tube-designer-contact] img');
  await image.waitFor({ state: 'visible' });
  await page.waitForFunction(() => {
    const image = document.querySelector('[data-tube-designer-contact] img');
    return image?.complete && image.naturalWidth > 100 && image.naturalHeight > 100;
  });
  assert.equal(await image.evaluate(node => node.complete && node.naturalWidth > 100 && node.naturalHeight > 100), true);
  assert.match(await image.getAttribute('src'), /assets\/wechat-contact\.jpg$/);
  assert.match(await image.getAttribute('alt'), /CAXStudio1024/);
  assert.equal(await page.locator('dialog:visible').count(), 0);
  for (const id of ['licensing.request', 'licensing.request-trial', 'licensing.activate', 'licensing.export-license']) {
    assert.equal(await command(id).isDisabled(), false);
    assert.notEqual(await command(id).getAttribute('aria-disabled'), 'true');
  }
  assert.equal(await command('licensing.status').count(), 0);
  await page.screenshot({ path: resolve(output, currentScenario.name + '-about.png') });
  currentScenario.checks.push('About opens through its ordinary primary tab, renders the real contact JPG and exposes request, trial request and activation without a modal');
}
async function finishScenario() {
  currentScenario.requests = await page.evaluate(() => window.fixture.requests);
  assert.deepEqual(await page.evaluate(() => window.fixture.forbidden), []);
  assert.deepEqual(currentScenario.browserErrors, []);
  await page.screenshot({ path: resolve(output, currentScenario.name + '.png') });
  await page.close();
  page = null;
}

try {
  for (const configuration of [
    { name: 'missing-license', message: 'Cannot access license file' },
    { name: 'status-failure', statusFailure: 'TPM device unavailable' },
  ]) {
    await freshStartup(configuration);
    assert.match(await page.locator('.bottom-dock .log-list').textContent(), /未授权/);
    assert.equal(await code().isDisabled(), true);
    for (const id of ['designer.add', 'designer.disassemble', 'designer.export-active-product-parts', 'designer.import-excel']) {
      assert.equal(await command(id).getAttribute('aria-disabled'), 'true');
      assert.equal(await command(id).isDisabled(), true);
    }
    for (const button of await page.locator('[data-action="ribbon-command"]').all()) assert.equal(await button.isDisabled(), true);
    const disabledClickBefore = await page.evaluate(() => ({ requests: window.fixture.requests.length,
      log: document.querySelector('.bottom-dock .log-list').textContent }));
    await command('designer.disassemble').click({ force: true });
    assert.deepEqual(await page.evaluate(() => ({ requests: window.fixture.requests.length,
      log: document.querySelector('.bottom-dock .log-list').textContent })), disabledClickBefore);
    currentScenario.checks.push('Startup immediately logs the missing licence; a forced pointer click on a truly disabled secondary button adds no log or native request');
    await page.evaluate(() => {
      window.fixture.normalNodes = [document.querySelector('.cam-viewport canvas'), document.querySelector('[data-tube-designer-parameter="productCode"]')];
      document.querySelector('.cam-viewport canvas').focus({ preventScroll: true });
    });
    await page.evaluate(() => window.__icaxAppShell.executeRibbonCommand('designer.disassemble'));
    assert.equal(await page.locator('dialog:visible').count(), 0);
    assert.equal(await page.evaluate(() => window.fixture.normalNodes.every(node => node.isConnected)), true);
    assert.match(await page.locator('.bottom-dock .log-list').textContent(), /未授权/);
    const requests = await page.evaluate(() => window.fixture.requests);
    assert.equal(requests.some(r => protectedSceneWrites.test(r.method)), false);
    assert.equal(requests.some(r => r.method === 'TubeDesigner.ScanBatchExcelAutomation'), false);
    currentScenario.checks.push('Protected secondary commands are gray; invoking a denied command writes the missing permission into the log without a modal, replacing the scene or issuing a business mutation/automatic Excel scan');
    await tab('resources').click();
    await page.waitForFunction(() => window.__icaxAppShell.getState().activeRibbonTabId === 'resources');
    await page.locator('[data-cam-change-action="tube-designer-profile-library-search"]').waitFor({ state: 'visible' });
    assert.equal(await page.locator('[data-tube-designer-locked-area]').count(), 0);
    assert.equal(await page.locator('dialog:visible').count(), 0);
    for (const id of ['resources.products', 'resources.profiles', 'resources.tools', 'resources.assemblies']) {
      assert.equal(await command(id).getAttribute('aria-disabled'), 'true');
      assert.equal(await command(id).isDisabled(), true);
    }
    assert.equal(await page.locator('[data-cam-change-action="tube-designer-profile-library-search"]').evaluate(node => node.disabled || Boolean(node.closest('[inert]'))), true);
    assert.equal(await page.locator('.cam-viewport canvas').first().evaluate(node => Boolean(node.closest('[inert]'))), true);
    for (const button of await page.locator('[data-action="ribbon-command"]').all()) assert.equal(await button.isDisabled(), true);
    currentScenario.checks.push('An unlicensed user can switch the primary resource tab and see its ordinary UI; secondary commands, search and scene interaction remain disabled/inert');
    await expectAboutContact();
    assert.match(await page.locator('[data-tube-designer-license-status]').textContent(), new RegExp(configuration.statusFailure || configuration.message));
    await command('licensing.export-license').click();
    await page.waitForFunction(() => window.fixture.requests.some(r => r.method === 'TubeDesignerLicensing.ExportLicenseFile')
      && !window.fixture.getProjectView(window.__icaxAppShell.getState().activeProjectId).tubeDesignerLicenseBusy);
    assert.match(await page.locator('.bottom-dock .log-list').textContent(), /本机尚无可导出的授权文件/);
    assert.equal(await page.locator('dialog:visible').count(), 0);
    assert.equal(await command('licensing.export-license').isDisabled(), false);
    currentScenario.checks.push('Export is accessible without a business license; a missing stored file is reported without granting permissions or blocking primary menus');
    const qr = await page.locator('[data-tube-designer-contact] img').elementHandle();
    for (const [id, method] of [['licensing.request', 'TubeDesignerLicensing.Request'], ['licensing.request-trial', 'TubeDesignerLicensing.RequestTrial']]) {
      await command(id).click();
      await page.waitForFunction(method => window.fixture.requests.some(r => r.method === method), method);
    }
    await page.evaluate(() => { window.fixture.activationGrants = ['page.product', 'product.design']; });
    await command('licensing.activate').click();
    await page.waitForFunction(() => window.fixture.requests.some(r => r.method === 'TubeDesignerLicensing.Activate') && !window.fixture.getProjectView(window.__icaxAppShell.getState().activeProjectId).tubeDesignerLicenseBusy);
    assert.equal(await qr.evaluate(node => node.isConnected), true);
    assert.equal((await page.evaluate(() => window.__icaxAppShell.getState())).activeRibbonTabId, 'about');
    await tab('view').click();
    await code().waitFor({ state: 'visible' });
    assert.equal(await code().isDisabled(), false);
    assert.equal(await command('app.save').isDisabled(), false);
    assert.notEqual(await command('designer.add').getAttribute('aria-disabled'), 'true');
    assert.equal(await command('designer.disassemble').getAttribute('aria-disabled'), 'true');
    currentScenario.checks.push('Both application exports and activation are usable on About; activation keeps the contact image and current tab, then independently enables design while leaving breakdown denied');
    await finishScenario();
  }

  await freshStartup({ name: 'product-page-only', grants: ['page.product'] });
  assert.equal(await code().isDisabled(), true);
  assert.equal(await command('designer.add').isDisabled(), true);
  await tab('resources').click();
  await page.waitForFunction(() => window.__icaxAppShell.getState().activeRibbonTabId === 'resources');
  for (const [id, area] of [['resources.products', 'templates'], ['resources.profiles', 'profiles'], ['resources.tools', 'tools'], ['resources.assemblies', 'assemblies']]) {
    assert.equal(await command(id).isDisabled(), false);
    await command(id).click();
    await page.waitForFunction(area => {
      const s = window.__icaxAppShell.getState();
      return !s.pendingCount && window.fixture.getProjectView(s.activeProjectId).activeAreaId === area;
    }, area);
    assert.equal(await page.locator('[data-tube-designer-locked-area]').count(), 0);
    assert.equal(await page.locator('dialog:visible').count(), 0);
  }
  assert.equal(await page.evaluate(() => window.fixture.requests.some(r => /(?:Generate|CheckPunchToolApplicability|ResolveAssembly)/.test(r.method))), false);
  currentScenario.checks.push('A product page grant permits resource type navigation while its missing edit grant keeps product editing and all passive resource generation/applicability/assembly resolution unavailable');
  await expectAboutContact();
  await finishScenario();

  for (const configuration of [
    { name: 'licensed-product', grants: ['page.product', 'product.design'] },
    { name: 'development-bypass', developmentBypass: true, grants: features.map(f => f.id) },
  ]) {
    await freshStartup(configuration);
    assert.equal(await code().isDisabled(), false);
    assert.equal(await command('app.save').isDisabled(), false);
    assert.notEqual(await command('designer.add').getAttribute('aria-disabled'), 'true');
    const input = await code().elementHandle();
    await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{height:370px!important;overflow:auto!important}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:700px}.tube-designer-instance-list{height:180px!important;overflow:auto!important;display:block!important;flex:none!important}.tube-designer-instance-list:after{content:"";display:block;height:600px}.tube-designer-parameter-scroll{height:490px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:700px}' });
    await page.evaluate(() => {
      const input = document.querySelector('[data-tube-designer-parameter="productCode"]');
      const selectors = ['.cam-context-pane', '.cam-info-pane', '.tube-designer-instance-list', '.tube-designer-parameter-scroll'];
      window.fixture.nodes = [input, document.querySelector('.cam-viewport canvas'), ...selectors.map(s => document.querySelector(s))];
      window.fixture.activationGrants = [...window.fixture.activeGrants]; window.fixture.holdActivation = true;
      if (!window.fixture.developmentBypass) {
        window.fixture.activationGrants.push('product.breakdown');
        window.fixture.authorizationPath = 'D:/LicenseStartupTest/upgrade.tdact';
      }
      window.fixture.activationPromise = window.__icaxAppShell.executeRibbonCommand('licensing.activate');
    });
    await page.waitForFunction(() => window.fixture.activationWaiting);
    await page.evaluate(() => {
      const positions = [80, 90, 130, 140];
      window.fixture.nodes.slice(2).forEach((node, index) => { node.scrollTop = positions[index]; });
      const input = window.fixture.nodes[0]; input.focus({ preventScroll: true }); input.setSelectionRange(3, 8);
      window.fixture.before = { scroll: window.fixture.nodes.slice(2).map(n => n.scrollTop), start: input.selectionStart, end: input.selectionEnd };
      window.fixture.releaseActivation();
    });
    await page.evaluate(() => window.fixture.activationPromise);
    const interaction = await page.evaluate(() => {
      const f = window.fixture, input = document.querySelector('[data-tube-designer-parameter="productCode"]');
      return { sameNodes: f.nodes.every(node => node.isConnected), focused: document.activeElement === input,
        selection: [input.selectionStart, input.selectionEnd], scroll: f.nodes.slice(2).map(node => node.scrollTop), before: f.before };
    });
    assert.equal(interaction.sameNodes, true);
    assert.equal(interaction.focused, true);
    assert.deepEqual(interaction.selection, [interaction.before.start, interaction.before.end]);
    assert.deepEqual(interaction.scroll, interaction.before.scroll);
    assert.equal(await input.evaluate(node => node === document.querySelector('[data-tube-designer-parameter="productCode"]')), true);
    if (configuration.name === 'licensed-product') {
      assert.deepEqual(await page.evaluate(() => window.fixture.authorizationDialog.filters[0].extensions), ['tdact']);
      assert.equal(await page.evaluate(() => window.fixture.requests.find(r => r.method === 'TubeDesignerLicensing.Activate').payload.path),
        'D:/LicenseStartupTest/upgrade.tdact');
      assert.equal(await command('designer.disassemble').isDisabled(), false);
      currentScenario.checks.push('Importing an upgrade uses the product licensing channel, preserves design and enables newly granted breakdown without rebuilding either sidebar');
    }
    currentScenario.checks.push('An asynchronous licence result preserves the real canvas, field/list nodes, focus, selection, both sidebar positions and continued nested scrolling');
    await page.evaluate(() => {
      const f = window.fixture;
      f.holdExport = true;
      f.exportNodes = [document.querySelector('[data-tube-designer-parameter="productCode"]'), document.querySelector('.cam-viewport canvas'),
        ...['.cam-context-pane', '.cam-info-pane', '.tube-designer-instance-list', '.tube-designer-parameter-scroll'].map(s => document.querySelector(s))];
      f.exportPromise = window.__icaxAppShell.executeRibbonCommand('licensing.export-license');
    });
    await page.waitForFunction(() => window.fixture.exportWaiting);
    await page.evaluate(() => {
      const f = window.fixture, field = f.exportNodes[0];
      f.exportNodes.slice(2).forEach((node, index) => { node.scrollTop = 105 + index * 19; });
      field.focus({ preventScroll: true }); field.setSelectionRange(1, 7);
      f.exportScroll = f.exportNodes.slice(2).map(node => node.scrollTop);
      f.releaseExport();
    });
    await page.evaluate(() => window.fixture.exportPromise);
    assert.equal(await page.evaluate(() => {
      const f = window.fixture, field = f.exportNodes[0];
      return f.exportNodes.every(node => node.isConnected) && document.activeElement === field
        && field.selectionStart === 1 && field.selectionEnd === 7
        && JSON.stringify(f.exportNodes.slice(2).map(node => node.scrollTop)) === JSON.stringify(f.exportScroll);
    }), true);
    const exportedRequest = await page.evaluate(() => window.fixture.requests.find(r => r.method === 'TubeDesignerLicensing.ExportLicenseFile'));
    assert.equal(exportedRequest.scope, 'product');
    assert.match(exportedRequest.payload.path, /iCAX-Authorization-.*\.tdact$/);
    assert.equal(await page.locator('dialog:visible').count(), 0);
    currentScenario.checks.push('The actual export command uses the product SDO and preserves canvas/input nodes, focus, selection and both sidebars including scrolling continued before its async response');
    if (configuration.name === 'licensed-product') {
      await page.evaluate(() => {
        const f = window.fixture;
        f.oldProjectId = window.__icaxAppShell.getState().activeProjectId;
        f.activationPromise = window.__icaxAppShell.executeRibbonCommand('licensing.activate');
      });
      await page.waitForFunction(() => window.fixture.activationWaiting);
      await page.evaluate(() => window.__icaxAppShell.createProject({ productId: 'icax.tube-designer', projectName: '异步授权后的新项目' }));
      await page.waitForFunction(() => {
        const s = window.__icaxAppShell.getState(), v = window.fixture.getProjectView(s.activeProjectId);
        return s.activeProjectId !== window.fixture.oldProjectId && !s.pendingCount && v.tubeDesignerLoaded && !v.tubeDesignerLoading
          && !v.tubeDesignerUserDataSynchronizationPromise;
      });
      await code().focus();
      await page.evaluate(() => {
        const input = document.querySelector('[data-tube-designer-parameter="productCode"]'); input.setSelectionRange(2, 6);
        window.fixture.newProjectInput = input; window.fixture.newProjectCanvas = document.querySelector('.cam-viewport canvas');
        window.fixture.releaseActivation();
      });
      await page.evaluate(() => window.fixture.activationPromise);
      assert.equal(await page.evaluate(() => {
        const f = window.fixture;
        return f.newProjectInput.isConnected && f.newProjectCanvas.isConnected && document.activeElement === f.newProjectInput
          && f.newProjectInput.selectionStart === 2 && f.newProjectInput.selectionEnd === 6
          && window.__icaxAppShell.getState().activeProjectId !== f.oldProjectId;
      }), true);
      currentScenario.checks.push('A licence operation started in one project cannot replace the next project canvas/input or take back focus when its asynchronous response arrives');
    }
    await expectAboutContact();
    await page.evaluate(() => { window.fixture.aboutRequestStart = window.fixture.requests.length; });
    await page.waitForTimeout(2800);
    const aboutRequests = await page.evaluate(() => window.fixture.requests.slice(window.fixture.aboutRequestStart));
    assert.equal(aboutRequests.some(r => ['TubeDesigner.List', 'TubeDesigner.ListUserData'].includes(r.method)), false);
    if (configuration.developmentBypass) assert.match(await page.locator('[data-tube-designer-license-status]').textContent(), /开发免授权/);
    currentScenario.checks.push('An already licensed or development workspace still starts on Product; switching to About does not reload product data and keeps the existing licensed background service');
    await finishScenario();
  }
  report.status = 'passed';
  report.checks = report.scenarios.flatMap(s => s.checks.map(check => s.name + ': ' + check));
} catch (error) {
  report.status = 'failed'; report.error = error.stack || String(error);
  if (page) {
    await page.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {});
    report.shell = await page.evaluate(() => window.__icaxAppShell?.getState()).catch(() => null);
    report.controls = await page.locator('[data-action="ribbon-command"]').evaluateAll(nodes => nodes.map(node => ({
      id: node.dataset.commandId, disabled: node.disabled, authorizationRequired: node.getAttribute('aria-disabled')
    }))).catch(() => []);
    currentScenario.requests = await page.evaluate(() => window.fixture?.requests).catch(() => []);
  }
  throw error;
} finally {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  await browserAnchor.close().catch(() => {});
  await browser.close();
}
console.log('TubeDesigner licence startup browser checks passed');
