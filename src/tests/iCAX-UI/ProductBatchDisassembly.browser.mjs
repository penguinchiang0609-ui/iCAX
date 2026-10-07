// Actual AppShell, product entry, ribbon, selection dialog and shared DOM refresh.
// Native transport is controlled here; manufacturing geometry is tested separately.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory, readBrowserAsset } from './browserPackageRuntime.mjs';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const raw = JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/template.json'));
const output = browserReportDirectory('product-batch-disassembly/browser');
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1518, height: 1000 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://batch-disassembly.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (path === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){return globalThis.__batchDisassembly.appProxy;}' });
    if (!path.startsWith('/src/')) return route.abort();
    const file = browserAssetPath(path.slice('/src/'.length));
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(file, 'utf8') });
  });
  await page.goto('http://batch-disassembly.test/');
  await page.evaluate(async raw => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const template = { ...raw, available: true, descriptorLoaded: true, name: catalogText(raw.displayName),
      groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: raw.parameters.map(field => ({ ...field, type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const parameters = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
    const db = new Map(['current', 'parts-outdated', 'model-outdated', 'assembly-draft'].map((id, index) => [id, {
      entityId: id, name: ['当前实例', '装配待拆实例', '模型待更新实例', '装配草稿实例'][index], templateId: template.id,
      quantity: index + 1, parameters: { ...parameters, width: 1200 + index * 100, productCode: `SW-${index + 1}` },
      modelOutdated: id === 'model-outdated', partsOutdated: id === 'parts-outdated', activeGenerationRunId: `run-${id}`,
      createdAt: `2026-10-06T00:00:0${index}`, parts: [],
    }]));
    const requests = [], projectId = 'batch-disassembly-project';
    let revision = 1;
    const designer = () => ({ templates: [template], instances: [...db.values()].map(item => ({ ...structuredClone(item),
      hasDisassembly: item.parts.length > 0, partCount: item.parts.length })), product: structuredClone(db.get('current')),
      activeProductId: 'current', generationRun: { entityId: 'run-current' }, members: [], joints: [], parts: [],
      manufacturingGroups: [...db.values()].filter(item => item.parts.length).map(item => ({ productEntityId: item.entityId,
        generationRunId: item.activeGenerationRunId, name: item.name, templateId: item.templateId,
        parameters: structuredClone(item.parameters), parts: structuredClone(item.parts) })) });
    const sceneState = () => ({ sceneId: 'batch-disassembly-scene', undoRedo: { revision, canUndo: false, canRedo: false }, tubeDesigner: designer() });
    const view = getProjectView(projectId);
    Object.assign(view, { scene: sceneState(), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
      tubeDesignerResourceLibraryArea: 'products', tubeDesignerParameterPanelProductId: 'current',
      tubeDesignerParameterDisclosureState: { initialized: true },
      tubeDesignerExpandedParameterGroups: ['section:materials', 'section:process', ...template.groups.map(group => `group:${group.key}`)],
      tubeDesignerRightDraftsByProductId: { 'assembly-draft': { ...db.get('assembly-draft').parameters, assemblyClearance: 0.25 } } });
    const fixture = { view, db, requests, hold: false, fail: false, release: null };
    const snapshot = () => ({ viewId: 'batch-disassembly-view', revision: String(revision), rows: [] });
    const sceneProxy = { state: sceneState(), pdo: { enabled: false }, resources: { async get() {
      // Return the normal resource response so the production decoder and
      // viewport are exercised. This is the SDK's small ICRG fixture.
      const encoded = 'JAAAAElDUkcAAAAAGAAwAAAABAAkAAgADAAQABQAGAAcACAAGAAAAAEAAAABAAAAAwAAAIQAAABYAAAAOAAAACQAAAAQAAAABwAAAAAAAAAAAAAAAwAAAAAAAAABAAAAAgAAAAMAAAD/ZjP//2Yz//9mM/8GAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/CQAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwkAAAAAAAAAAAAAAAAAAAAAACBBAAAAAAAAAAAAAAAAAAAgQQAAAAA=';
      return new Response(Uint8Array.from(atob(encoded), character => character.charCodeAt(0)), { status: 200 });
    } }, views: { async start() { return { get snapshot() { return snapshot(); }, async poll() { return snapshot(); },
      async waitForSnapshot(predicate) { const value = snapshot(); if (!predicate(value)) throw new Error('View predicate did not match fixture revision'); return value; }, async stop() {} }; } },
      async getState() { return sceneState(); }, async invoke(method, payload, options = {}) {
        requests.push({ scope: 'scene', method, payload: structuredClone(payload) });
        if (method === 'TubeDesigner.List') return { tubeDesigner: designer() };
        if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
        if (method === 'TubeDesigner.GetTemplateDescriptor') return { template };
        if (method === 'TubeDesigner.GenerateProductTemplatePreview') return { items: [{ entityId: 'preview-box', geometry: { url: 'preview-box', version: 1 }, bounds: { min: [-20,-20,0], max: [20,20,200] } }] };
        if (method !== 'TubeDesigner.DisassembleSelected') throw new Error('Unexpected scene request: ' + method);
        if (fixture.hold) await new Promise(resolve => { fixture.release = resolve; });
        fixture.release = null;
        if (fixture.fail) throw new Error('native batch disassembly failed');
        for (const id of payload.productEntityIds) {
          const item = db.get(id);
          if (item.modelOutdated) throw new Error('Cannot disassemble an outdated model');
          if (payload.productParametersByEntityId?.[id]) item.parameters = structuredClone(payload.productParametersByEntityId[id]);
          item.partsOutdated = false;
          item.parts = [{ entityId: `part-${id}`, index: 1, name: '外框横杆', quantity: item.quantity, length: item.parameters.width,
            properties: { assemblyClearance: item.parameters.assemblyClearance }, sourceMemberId: `member-${id}` }];
        }
        revision++;
        return { tubeDesigner: designer() };
      } };
    const projectState = { projectId, projectName: '拆单选择回归', mainScene: sceneState() };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const productState = { productId: 'icax.tube-designer', productName: 'TubeDesigner', isStarted: true,
      frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs', catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ['ictd'] } };
    const settings = { enabled: false, inputDirectory: '', outputDirectory: '' };
    const productProxy = { productId: productState.productId, state: productState, async getState() { return productState; },
      getProject() { return projectProxy; }, async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; },
      async invoke(method, payload) {
        requests.push({ scope: 'product', method, payload: structuredClone(payload) });
        if (method === 'TubeDesigner.GetBatchExcelAutomationSettings') return { settings };
        if (method === 'TubeDesigner.ScanBatchExcelAutomation') return { settings, files: [], errors: [] };
        throw new Error('Unexpected product request: ' + method);
      } };
    const appProxy = { bridge: {}, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
    Object.assign(fixture, { appProxy, sceneProxy, designer, sceneState, projectProxy });
    globalThis.__batchDisassembly = fixture;
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, raw);
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready');
  const partCommands = await page.locator('.ribbon-group').filter({ has: page.locator('.ribbon-group-title', { hasText: /^零件$/ }) }).locator('.ribbon-command').allTextContents();
  assert.deepEqual(partCommands.map(text => text.trim()), ['复尺', '拆单', '导出清单']);
  const action = name => page.locator(`[data-cam-action="tube-designer-batch-disassembly-${name}"]`);
  const dialog = page.getByRole('dialog', { name: '批量拆单', exact: true });
  const count = () => page.evaluate(() => __batchDisassembly.requests.filter(call => call.method === 'TubeDesigner.DisassembleSelected').length);
  const open = async () => { await page.locator('[data-command-id="designer.disassemble"]').click(); await dialog.waitFor({ state: 'visible' }); };
  const checkbox = id => dialog.locator(`input[data-tube-designer-instance-id="${id}"]`);
  // Product errors stay in the log and cannot bleed into a resource preview.
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(() => document.querySelector('.bottom-dock .log-list')?.textContent.includes('当前产品实例还没有有效零件清单'));
  assert.equal(await page.locator('.cam-status').getByText('当前产品实例还没有有效零件清单', { exact: false }).count(), 0);
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="resources"]').click();
  await page.locator('[data-command-id="resources.products"]').click();
  try {
    await page.waitForFunction(() => __batchDisassembly.view.activeAreaId === 'templates' && __batchDisassembly.view.tubeDesignerProductTemplateLibrary?.previewApplied, null, { timeout: 10000 });
  } catch (error) {
    writeFileSync(resolve(output, 'preview-diagnostic.json'), JSON.stringify(await page.evaluate(() => ({
      area: __batchDisassembly.view.activeAreaId, preview: __batchDisassembly.view.tubeDesignerProductTemplateLibrary,
      requests: __batchDisassembly.requests, logs: document.querySelector('.bottom-dock')?.textContent,
    })), null, 2));
    throw error;
  }
  await page.waitForFunction(() => !__batchDisassembly.view.pending && !__icaxAppShell.getState().pendingCount);
  await page.locator('.project-progress-backdrop').waitFor({ state: 'hidden' });
  assert.equal(await page.locator('.cam-workbench').getByText('当前产品实例还没有有效零件清单', { exact: false }).count(), 0);
  assert.match(await page.locator('.bottom-dock .log-list').innerText(), /当前产品实例还没有有效零件清单/);
  await page.screenshot({ path: resolve(output, 'resource-preview-without-product-error.png') });
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="view"]').click();
  await open();
  const headerBox = await dialog.locator('.tube-designer-dialog-header').boundingBox();
  const closeBox = await dialog.getByRole('button', { name: '关闭批量拆单', exact: true }).boundingBox();
  assert(closeBox.x >= headerBox.x && closeBox.y >= headerBox.y && closeBox.x + closeBox.width <= headerBox.x + headerBox.width
    && closeBox.y + closeBox.height <= headerBox.y + headerBox.height, 'Close button must be inside the dialog header');
  await dialog.getByRole('button', { name: '关闭批量拆单', exact: true }).click();
  await dialog.waitFor({ state: 'hidden' });
  assert.equal(await count(), 0, 'Close button must not send native disassembly');
  await open();
  assert.equal(await count(), 0, 'Opening selector must not send native disassembly');
  assert.equal(await checkbox('model-outdated').isDisabled(), true);
  assert.equal(await checkbox('parts-outdated').isEnabled(), true);
  assert.equal(await checkbox('assembly-draft').isEnabled(), true);
  assert.match(await dialog.locator('[data-tube-designer-batch-disassembly-instance-row="model-outdated"]').innerText(), /先更新模型/);
  await dialog.getByRole('button', { name: '取消', exact: true }).click();
  assert.equal(await count(), 0, 'Cancel must not send native disassembly');
  await open();
  await dialog.getByLabel('全选可拆单实例', { exact: true }).uncheck();
  assert.equal(await action('confirm').isDisabled(), true, 'Empty selection disables confirmation');
  assert.deepEqual(await page.evaluate(() => __batchDisassembly.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds), []);
  await dialog.getByLabel('全选可拆单实例', { exact: true }).check();
  assert.deepEqual(await page.evaluate(() => __batchDisassembly.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds), ['current', 'parts-outdated', 'assembly-draft']);
  await checkbox('current').uncheck();
  assert.deepEqual(await page.evaluate(() => __batchDisassembly.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds), ['parts-outdated', 'assembly-draft']);
  await page.screenshot({ path: resolve(output, 'selected-products.png') });
  await page.evaluate(() => { __batchDisassembly.hold = true; __batchDisassembly.fail = true; });
  await action('confirm').click();
  await page.waitForFunction(() => Boolean(__batchDisassembly.release));
  assert.equal(await count(), 1);
  assert.equal(await action('confirm').isDisabled(), true);
  await page.evaluate(() => document.querySelector('[data-cam-action="tube-designer-batch-disassembly-confirm"]').dispatchEvent(new MouseEvent('click', { bubbles: true })));
  assert.equal(await count(), 1, 'Repeated confirmation must be single flight');
  await page.evaluate(() => __batchDisassembly.release());
  await page.waitForFunction(() => !__batchDisassembly.view.pending && __batchDisassembly.view.tubeDesignerBatchDisassemblyDialog?.error);
  assert.match(await dialog.getByRole('alert').innerText(), /native batch disassembly failed/);
  assert.deepEqual(await page.evaluate(() => __batchDisassembly.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds), ['parts-outdated', 'assembly-draft']);
  await page.evaluate(() => { __batchDisassembly.fail = false; });
  await action('confirm').click();
  await page.waitForFunction(() => Boolean(__batchDisassembly.release));
  // Use actual pane/nested scroll containers and a real text field. Product
  // fields remain disabled during disassembly; the separate current-design
  // browser regression exercises focus restoration in editable probe fields.
  await page.addStyleTag({ content: '.cam-context-pane,.cam-info-pane{overflow:auto!important}.tube-designer-instance-panel,.tube-designer-parameter-panel{min-height:1400px!important}.tube-designer-instance-list{height:280px!important;max-height:280px!important;overflow:auto!important;display:block!important}.tube-designer-instance-list>article{min-height:170px!important}.tube-designer-instance-card{min-height:170px!important}.tube-designer-parameter-scroll{height:260px!important;max-height:260px!important;overflow:auto!important}' });
  await page.evaluate(() => {
    const f = __batchDisassembly, mount = document.querySelector('[data-product-surface="project"]');
    const input = mount.querySelector('[data-tube-designer-parameter="productCode"]');
    if (!input) throw new Error('Expected a real product code input');
    input.focus({ preventScroll: true }); input.setSelectionRange(1, 4, 'backward');
    const selectors = ['.cam-context-pane', '.cam-info-pane', '.tube-designer-instance-list', '[data-tube-designer-parameter-scroll]'];
    const values = [120, 140, 160, 180];
    selectors.forEach((selector, index) => { mount.querySelector(selector).scrollTop = values[index]; });
    f.interaction = { mount, input, canvas: mount.querySelector('canvas'), selectors, values,
      selection: [input.selectionStart, input.selectionEnd, input.selectionDirection], listenerEvents: 0,
      initialFocus: document.activeElement === input, disabled: input.disabled };
    input.addEventListener('batch-disassembly-probe', () => f.interaction.listenerEvents++);
    f.release();
  });
  await page.waitForFunction(() => !__batchDisassembly.view.pending && !__batchDisassembly.view.tubeDesignerBatchDisassemblyDialog);
  const result = await page.evaluate(() => {
    const f = __batchDisassembly, saved = f.interaction;
    const input = saved.mount.querySelector('[data-tube-designer-parameter="productCode"]');
    input.dispatchEvent(new Event('batch-disassembly-probe'));
    return { sameInput: saved.input === input, sameCanvas: saved.canvas === saved.mount.querySelector('canvas'),
      focus: document.activeElement === input, selection: [input.selectionStart, input.selectionEnd, input.selectionDirection],
      initialFocus: saved.initialFocus, inputDisabledDuringPending: saved.disabled,
      finalFocusTag: document.activeElement?.tagName,
      scrollSizes: saved.selectors.map(selector => { const node = saved.mount.querySelector(selector); return { height: node.clientHeight, content: node.scrollHeight }; }),
      scrolls: saved.selectors.map(selector => saved.mount.querySelector(selector).scrollTop), listenerEvents: saved.listenerEvents,
      requests: f.requests, selectedParts: [...f.db].filter(([,item]) => item.parts.length).map(([id]) => id),
      activeProduct: f.view.scene.tubeDesigner.product.entityId, assemblyClearance: f.db.get('assembly-draft').parameters.assemblyClearance };
  });
  writeFileSync(resolve(output, 'interaction-diagnostic.json'), JSON.stringify(result, null, 2));
  assert.equal(result.sameInput, true); assert.equal(result.sameCanvas, true);
  assert.equal(result.inputDisabledDuringPending, true);
  assert.equal(result.initialFocus, false, 'Disabled pending product fields cannot take focus');
  assert.deepEqual(result.selection, [1, 4, 'backward']); assert.deepEqual(result.scrolls, [120, 140, 160, 180]); assert.equal(result.listenerEvents, 1);
  assert.deepEqual(result.selectedParts, ['parts-outdated', 'assembly-draft']); assert.equal(result.assemblyClearance, 0.25);
  assert.equal(result.activeProduct, 'current');
  const disassemblies = result.requests.filter(call => call.method === 'TubeDesigner.DisassembleSelected');
  assert.equal(disassemblies.length, 2);
  assert(disassemblies.every(call => call.scope === 'scene' && JSON.stringify(call.payload.productEntityIds) === JSON.stringify(['parts-outdated', 'assembly-draft'])));
  assert(!result.requests.some(call => call.method === 'TubeDesigner.GeneratePreview' || call.method === 'TubeDesigner.ActivateProduct'));
  assert.deepEqual(errors, []);
  const keyboardPageChanges = [];
  for (const fail of [true, false]) {
    await page.evaluate(fail => { __batchDisassembly.hold = true; __batchDisassembly.fail = fail; }, fail);
    await open();
    await dialog.getByLabel('全选可拆单实例', { exact: true }).uncheck();
    await checkbox('current').check();
    await action('confirm').click();
    await page.waitForFunction(() => Boolean(__batchDisassembly.release));
    assert.equal(await page.evaluate(() => document.activeElement.tagName), 'BODY');
    const keyboardFocusPath = [];
    for (let step = 0; step < 500; step++) {
      await page.keyboard.press('Shift+Tab');
      const focused = await page.evaluate(() => ({ tag: document.activeElement.tagName,
        action: document.activeElement.dataset.action, tab: document.activeElement.dataset.tabId,
        html: document.activeElement.outerHTML.slice(0, 500) }));
      keyboardFocusPath.push(focused);
      if (focused.tab === 'machining') break;
    }
    writeFileSync(resolve(output, `keyboard-${fail ? 'failure' : 'success'}-focus-path.json`), JSON.stringify(keyboardFocusPath, null, 2));
    assert.equal(await page.evaluate(() => document.activeElement.dataset.tabId), 'machining');
    await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(() => document.activeElement.dataset.tabId), 'resources');
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => __batchDisassembly.view.activeAreaId === 'templates');
    for (let step = 0; step < 100; step++) {
      await page.keyboard.press('Tab');
      if (await page.evaluate(() => document.activeElement.getAttribute('aria-label') === '搜索产品模板')) break;
    }
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('aria-label')), '搜索产品模板');
    await page.keyboard.press('Control+A');
    await page.keyboard.type('防盗窗');
    await page.keyboard.press('Shift+Home');
    await page.addStyleTag({ content: '.tube-product-template-library-panel,.tube-product-template-library-editor{min-height:1400px!important}.tube-product-template-library-list{height:210px!important;max-height:210px!important;overflow:auto!important}.tube-product-template-library-list:after{content:"";display:block;height:1500px}.tube-product-template-library-editor-body{height:260px!important;max-height:260px!important;overflow:auto!important}.tube-product-template-library-editor-body:after{content:"";display:block;height:1500px}' });
    await page.evaluate(fail => {
      const f = __batchDisassembly, mount = document.querySelector('[data-product-surface="project"]');
      const input = mount.querySelector('[aria-label="搜索产品模板"]');
      if (!input) throw new Error('Expected the real resource editor input');
      const selectors = ['.cam-context-pane', '.cam-info-pane', '.tube-product-template-library-list', '.tube-product-template-library-editor-body'];
      const values = [80, 90, 100, 110];
      selectors.forEach((selector, index) => { mount.querySelector(selector).scrollTop = values[index]; });
      f.resourceInteraction = { mount, input, selectors, values, initialFocus: document.activeElement === input,
        inputSelector: '[aria-label="搜索产品模板"]' };
      f.release();
    }, fail);
    await page.waitForFunction(() => !__batchDisassembly.view.pending && !__batchDisassembly.view.tubeDesignerBatchDisassemblyDialog?.pending);
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    const afterPageChange = await page.evaluate(() => {
      const f = __batchDisassembly, saved = f.resourceInteraction;
      const currentMount = document.querySelector('[data-product-surface="project"]'), input = currentMount.querySelector(saved.inputSelector);
      return { area: f.view.activeAreaId, tab: __icaxAppShell.getState().activeRibbonTabId,
        initialFocus: saved.initialFocus, focus: document.activeElement === input,
        sameMount: saved.mount === currentMount, sameInput: saved.input === input,
        finalFocused: document.activeElement.outerHTML.slice(0, 500),
        value: input.value,
        selection: [input.selectionStart, input.selectionEnd, input.selectionDirection],
        scrolls: saved.selectors.map(selector => currentMount.querySelector(selector).scrollTop),
        pendingMasks: document.querySelectorAll('[data-tube-designer-operation-wait]').length,
        batchDialogs: document.querySelectorAll('.tube-designer-batch-disassembly-dialog').length,
        currentHasParts: f.db.get('current').parts.length > 0 };
    });
    writeFileSync(resolve(output, `keyboard-${fail ? 'failure' : 'success'}-diagnostic.json`), JSON.stringify(afterPageChange, null, 2));
    assert.equal(afterPageChange.area, 'templates'); assert.equal(afterPageChange.tab, 'resources');
    assert.equal(afterPageChange.initialFocus, true); assert.equal(afterPageChange.focus, true);
    assert.equal(afterPageChange.value, '防盗窗');
    assert.deepEqual(afterPageChange.selection, [0, 3, 'backward']);
    assert.deepEqual(afterPageChange.scrolls, [80, 90, 100, 110]);
    assert.equal(afterPageChange.pendingMasks, 0); assert.equal(afterPageChange.batchDialogs, 0);
    assert.equal(afterPageChange.currentHasParts, !fail);
    await page.screenshot({ path: resolve(output, `keyboard-${fail ? 'failure' : 'success'}-resource.png`) });
    keyboardPageChanges.push({ fail, keyboardFocusPath, ...afterPageChange });
    await page.locator('[data-action="select-ribbon-tab"][data-tab-id="view"]').click();
    await page.waitForFunction(() => __batchDisassembly.view.activeAreaId === 'view' && __batchDisassembly.view.tubeDesignerLoaded);
    assert.equal(await page.evaluate(() => __batchDisassembly.view.scene.tubeDesigner.instances.find(item => item.entityId === 'current').hasDisassembly), !fail);
    if (fail) {
      await dialog.waitFor({ state: 'visible' });
      assert.match(await dialog.getByRole('alert').innerText(), /native batch disassembly failed/);
      assert.deepEqual(await page.evaluate(() => __batchDisassembly.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds), ['current']);
      await dialog.getByRole('button', { name: '取消', exact: true }).click();
      keyboardPageChanges.at(-1).failureSelectionRecoveredOnReturn = true;
    }
  }
  await page.evaluate(() => {
    const f = __batchDisassembly;
    f.db.clear(); f.view.scene = f.sceneState(); f.sceneProxy.state = f.sceneState();
    f.view.tubeDesignerRightDraftsByProductId = {};
  });
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="resources"]').click();
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="view"]').click();
  await open();
  assert.match(await dialog.innerText(), /还没有产品实例/);
  assert.equal(await action('confirm').isDisabled(), true);
  assert.equal(await dialog.getByLabel('全选可拆单实例', { exact: true }).isDisabled(), true);
  await page.screenshot({ path: resolve(output, 'empty-project-dialog.png') });
  await dialog.getByRole('button', { name: '取消', exact: true }).click();
  assert.equal(await count(), 4, 'Empty project open/cancel must not send native disassembly');
  assert.deepEqual(errors, []);
  await page.evaluate(async () => (await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs')).stopBatchExcelAutomation());
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, partCommands: partCommands.map(text => text.trim()),
    openAndCancelDoNotDisassemble: true, emptySelectionDisabled: true, selectionAndAvailability: true,
    nativeFailurePreservesSelection: true, singleFlight: true, resourcePreviewHasNoProductError: true,
    pendingProductFieldsDisabled: true, focusDuringPendingCoveredSeparately: 'ProductCurrentDesignDisassembly.browser.mjs',
    productErrorRetainedInAppShellLog: true, emptyProjectDialog: true, closeButtonInsideHeader: true,
    ...result, keyboardPageChanges, errors, nativeTransport: 'controlled native transport fixture', standaloneCefEndToEnd: false }, null, 2));
  console.log('PASS product batch disassembly: actual ribbon/dialog mouse selection, unavailable models, current assembly drafts, retry and single flight, real pane selection/scroll/node preservation, resource preview free of product error, keyboard page changes during both native success/failure preserve real resource-search focus/caret/value and four scrolls without stale masks or dialogs. Pending product fields remain disabled; editable product-pane focus restoration is covered by ProductCurrentDesignDisassembly.browser.mjs.');
} finally { await browser.close(); }
