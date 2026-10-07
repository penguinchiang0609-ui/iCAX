// Actual add/Excel/batch-selection controls, delegated handlers and shared viewport.
// Native descriptor/generation/export transport is controlled, not a native CAD acceptance test.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('tube-designer-creation-window-memory-20261006');
const checks = [], errors = [];
const check = (actual, expected, name) => { assert.deepEqual(actual, expected, name); checks.push(name); };
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://creation-memory.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body></body>' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript',
      body: readFileSync(browserAssetPath(path.slice('/src/'.length)), 'utf8') });
  });
  await page.goto('http://creation-memory.test/');
  await page.evaluate(async () => {
    localStorage.clear();
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const memory = await import('/src/apps/tube-designer/webpage/windowStateMemory.mjs');
    const { patchParameterContent } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { ensureTubeDesignerStyles } = await import('/src/apps/tube-designer/webpage/styles/ensureStyles.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { encodeNestingGeometry } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    document.body.className = 'tube-designer-workspace'; ensureTubeDesignerStyles();
    const style = document.createElement('style'); style.textContent = 'body{margin:0}#mount{height:100vh}.cam-workbench{height:100vh}#preview{width:900px;height:650px}.tube-designer-excel-column-table{max-height:620px;overflow:auto}'; document.head.append(style);
    document.body.innerHTML = '<div id="mount"><div class="cam-workbench"><main id="preview"></main><div id="dialogs"></div></div></div>';
    const mount = document.querySelector('#mount'), workbench = mount.querySelector('.cam-workbench'), dialogs = document.querySelector('#dialogs');
    const template = (id, width, order) => ({ id, name: `测试产品/${id}`, version: '1.0', available: true,
      groups: [{ key: 'structure', displayName: '结构', defaultOpen: true }], parameters: [
        { key: 'width', groupKey: 'structure', displayName: '宽度', valueType: 'number', defaultValue: width, min: 1 },
        { key: 'mode', groupKey: 'structure', displayName: '款式', type: 'select', valueType: 'string', defaultValue: 'fixed', options: [{ value: 'fixed', label: '固定' }, { value: 'opening', label: '开启' }] },
        { key: 'enabled', groupKey: 'structure', displayName: '开启选项', valueType: 'boolean', defaultValue: true },
        { key: 'internal', groupKey: 'structure', valueType: 'string', defaultValue: 'system', presentation: { visible: false } },
      ], extensions: { catalog: { categoryPath: ['测试产品'], templateOrder: order },
        addDialog: { structureParameters: ['width', 'mode', 'enabled'], parameterGroups: [{ key: 'structure', displayName: '结构', parameters: ['width', 'mode', 'enabled'] }] } } });
    const templates = [template('alpha', 1200, 1), template('beta', 800, 2)];
    const f = window.creationFixture = { actions, views, memory, mount, dialogs, calls: [], errors: [], templates, version: 0 };
    f.view = { activeAreaId: 'view', pending: false, scene: { tubeDesigner: { templates: structuredClone(templates), instances: [], members: [] } } };
    f.context = { mount, project: { projectId: 'creation-memory-project' }, actions: { async refreshActiveSceneState() {}, refreshProjectHistoryControls() {} },
      appProxy: { bridge: { async saveFileDialog() { return 'D:/test/remembered-template.xlsx'; } } },
      productProxy: { async invoke(method, payload) {
        f.calls.push({ scope: 'product', method, payload: structuredClone(payload) });
        if (method === 'TubeDesigner.ExportBatchExcelTemplate') return { templatePath: payload.targetPath };
        throw Error('No scene generation on product channel: ' + method);
      } }, sceneProxy: { resources: { async get() { return new Response(f.mesh); } }, async invoke(method, payload) {
        f.calls.push({ scope: 'scene', method, payload: structuredClone(payload) });
        if (method !== 'TubeDesigner.GeneratePreview') throw Error('Unexpected scene method ' + method);
        f.lastGenerated = structuredClone(payload);
        const product = { entityId: `product-${++f.version}`, templateId: payload.templateId, name: payload.instanceName,
          quantity: payload.instanceQuantity, createdAt: payload.createdAt, parameters: { width: payload.width, mode: payload.mode, enabled: payload.enabled, internal: payload.internal }, active: true };
        return { tubeDesigner: { templates: structuredClone(templates), instances: [product], product,
          generationRun: { entityId: `run-${f.version}` }, members: [{ entityId: `member-${f.version}` }], parts: [], manufacturingGroups: [] } };
      } } };
    f.viewport = createThreeViewport({ showGrid: false }); f.viewport.mount(document.querySelector('#preview')); f.view.viewport = f.viewport;
    f.render = () => {
      for (const backdrop of workbench.querySelectorAll(':scope > .tube-designer-modal-backdrop')) dialogs.append(backdrop);
      const markup = views.renderDesignerDialogs(f.view.scene.tubeDesigner, f.view);
      if (f.replaceDialogDOM) {
        dialogs.innerHTML = markup;
        f.dialogReplacementCount = (f.dialogReplacementCount ?? 0) + 1;
      } else patchParameterContent(mount, dialogs, markup);
      f.controller = memory.bindTubeDesignerWindowMemory(f.context, f.view, f.ops);
      actions.synchronizeBatchExcelTemplateSelection(f.view, mount);
    };
    f.ops = { renderProject() { f.render(); }, appendProjectLog() {}, showNotice() {}, async refreshActiveAreaView(_context, view, expected) {
      const width = f.lastGenerated.width, mesh = new THREE.BoxGeometry(width, 50, 10);
      f.mesh = encodeNestingGeometry({ positions: [...mesh.attributes.position.array], indices: [...mesh.index.array] }); mesh.dispose();
      const revision = `view-${f.version}`;
      const viewportReceipt = await view.viewport.applyViewSnapshot({ revision, rows: expected.expectedEntityIds.map(entityId => ({ entityId,
        data: { geometry: { url: 'resource://creation-memory/preview', version: f.version }, geometryKind: 1, renderClass: 1, visible: true } })) }, f.context.sceneProxy.resources);
      return { revision, viewportReceipt };
    } };
    f.run = (action, target = null) => {
      const operation = actions.handleDesignerAreaAction(f.context, f.view, action, target, f.ops).then(result => {
        memory.rememberLinkedWindowControls(f.context, f.view, action, target);
        return result;
      });
      f.view.activeAreaAction = { action, promise: operation };
      void operation.catch(error => f.errors.push(error.message)).finally(() => {
        if (f.view.activeAreaAction?.promise === operation) f.view.activeAreaAction = null;
      });
      return operation;
    };
    mount.onclick = event => { const target = event.target.closest('[data-cam-action]'); if (target && !target.disabled) void f.run(target.dataset.camAction, target); };
    mount.onchange = event => { const target = event.target.closest('[data-cam-change-action]'); if (target && !target.disabled) void f.run(target.dataset.camChangeAction, target); };
    f.openAdd = () => f.run('tube-designer-open-add');
    f.openExcel = () => f.run('tube-designer-excel-template-open');
    f.openBatch = () => actions.handleDesignerRibbonCommand(f.context, f.view, 'designer.disassemble', f.ops);
    f.render();
  });
  const settle = async () => {
    await page.waitForFunction(() => !creationFixture.view.pending && !creationFixture.view.activeAreaAction
      && !creationFixture.view.tubeDesignerTemplateSwitchPending);
    await page.evaluate(async () => {
      for (const dialog of document.querySelectorAll('[role="dialog"]')) {
        await Promise.race([creationFixture.controller.restoreWindow(dialog), new Promise((_, reject) => setTimeout(() =>
          reject(new Error('Dialog restoration timed out: ' + dialog.getAttribute('aria-labelledby') + '; action=' + creationFixture.view.activeAreaAction?.action)), 10000))]);
      }
    });
    await page.waitForFunction(() => !creationFixture.view.pending && !creationFixture.view.activeAreaAction);
  };
  const change = async (selector, value) => { await page.locator(selector).fill(String(value)); await page.locator(selector).dispatchEvent('change'); await settle(); };
  const addSnapshot = () => page.evaluate(() => ({ templateId: creationFixture.view.tubeDesignerAddTemplateId,
    draft: creationFixture.view.tubeDesignerAddDraft, width: document.querySelector('[data-tube-designer-parameter="width"]').value,
    mode: document.querySelector('[data-tube-designer-parameter="mode"]').value, enabled: document.querySelector('[data-tube-designer-parameter="enabled"]').checked }));
  await page.evaluate(() => creationFixture.openAdd()); await settle();
  await change('[data-tube-designer-parameter="width"]', 1480);
  await page.locator('[data-tube-designer-parameter="mode"]').selectOption('opening'); await settle();
  await page.locator('[data-tube-designer-parameter="enabled"]').uncheck(); await settle();
  await page.locator('[data-cam-action="tube-designer-cancel-add"]').last().click(); await settle();
  await page.evaluate(() => creationFixture.openAdd()); await settle();
  check(await addSnapshot(), { templateId: 'alpha', draft: { width: 1480, mode: 'opening', enabled: false, internal: 'system' }, width: '1480', mode: 'opening', enabled: false }, 'actual add controls and model restore typed fields after cancel/reopen');
  check(await page.locator('[data-tube-designer-parameter="internal"]').count(), 0, 'hidden descriptor parameter remains internal');
  await page.locator('[data-tube-designer-template-id="beta"]').click(); await settle();
  check((await addSnapshot()).width, '800', 'other template retains its own defaults');
  await change('[data-tube-designer-parameter="width"]', 900);
  await page.locator('[data-cam-action="tube-designer-cancel-add"]').last().click(); await settle();
  await page.evaluate(() => creationFixture.openAdd()); await settle();
  check({ templateId: (await addSnapshot()).templateId, width: (await addSnapshot()).width }, { templateId: 'beta', width: '900' }, 'last template choice and its own inputs are remembered');
  await page.locator('[data-tube-designer-template-id="alpha"]').click(); await settle();
  check((await addSnapshot()).width, '1480', 'template A inputs are independent of template B');
  await page.locator('[data-cam-action="tube-designer-confirm-add"]').click(); await settle();
  check(await page.evaluate(() => ({ width: creationFixture.lastGenerated.width, mode: creationFixture.lastGenerated.mode,
    enabled: creationFixture.lastGenerated.enabled, internal: creationFixture.lastGenerated.internal })),
    { width: 1480, mode: 'opening', enabled: false, internal: 'system' }, 'actual creation payload uses remembered visible values and internal defaults');
  check(await page.evaluate(() => {
    const bounds = creationFixture.viewport.getVisibleBounds();
    return { width: bounds.max[0] - bounds.min[0], entityIds: creationFixture.viewport.getAppliedViewState().entityIds,
      operation: creationFixture.view.tubeDesignerLastOperation.kind };
  }), { width: 1480, entityIds: ['member-1'], operation: 'add' }, 'shared viewport commits and fits preview consistent with remembered creation payload');
  check(await page.evaluate(() => creationFixture.calls.filter(row => row.method === 'TubeDesigner.GeneratePreview').map(row => row.scope)), ['scene'], 'actual creation uses scene channel');

  await page.evaluate(() => creationFixture.openExcel()); await settle();
  await page.locator('[data-tube-designer-excel-include="width"]').check();
  await page.locator('[data-tube-designer-excel-required="width"]').check();
  await page.locator('[data-tube-designer-excel-alias="width"]').fill('实测宽度');
  await page.locator('[data-tube-designer-excel-default="width"]').fill('1480');
  await page.locator('[data-cam-action="tube-designer-excel-template-close"]').last().click(); await settle();
  await page.evaluate(() => creationFixture.openExcel()); await settle();
  check(await page.evaluate(() => {
    const by = type => document.querySelector(`[data-tube-designer-excel-${type}="width"]`);
    return { included: by('include').checked, required: by('required').checked, alias: by('alias').value, defaultValue: by('default').value };
  }), { included: true, required: true, alias: '实测宽度', defaultValue: '1480' }, 'actual Excel controls restore include/required/alias/default after cancellation');
  await page.locator('[data-cam-change-action="tube-designer-excel-template-select"]').selectOption('beta'); await settle();
  check(await page.locator('[data-tube-designer-excel-alias="width"]').inputValue(), '', 'Excel template B has independent column configuration');
  await page.locator('[data-tube-designer-excel-alias="width"]').fill('B宽度');
  await page.locator('[data-cam-action="tube-designer-excel-template-close"]').last().click(); await settle();
  await page.evaluate(() => creationFixture.openExcel()); await settle();
  check(await page.locator('[data-cam-change-action="tube-designer-excel-template-select"]').inputValue(), 'beta', 'Excel last selected template is restored');
  await page.locator('[data-cam-change-action="tube-designer-excel-template-select"]').selectOption('alpha'); await settle();
  await page.locator('[data-cam-action="tube-designer-excel-template-export"]').click(); await settle();
  check(await page.evaluate(() => creationFixture.calls.find(row => row.method === 'TubeDesigner.ExportBatchExcelTemplate').payload.columns.find(row => row.key === 'width')),
    { key: 'width', title: '实测宽度', required: true, defaultValue: '1480' }, 'real Excel export submits exactly the remembered column configuration');

  await page.evaluate(() => {
    // Selection controls must also restore correctly when a legacy/full render
    // replaces every dialog node, rather than retaining keyed input nodes.
    creationFixture.replaceDialogDOM = true;
    creationFixture.view.scene.tubeDesigner.instances = ['instance-A', 'instance-B'].map(entityId => ({ entityId,
      templateId: 'alpha', name: entityId, quantity: 1, parameters: { width: 1200, mode: 'fixed', enabled: true, internal: 'system' } }));
    creationFixture.view.scene.tubeDesigner.product = null;
    creationFixture.openBatch();
  }); await settle();
  console.log('Batch full DOM replacement: opened');
  const leaf = id => page.locator(`[data-cam-action="tube-designer-batch-disassembly-toggle"][data-tube-designer-instance-id="${id}"]`);
  await leaf('instance-B').click(); await settle();
  console.log('Batch full DOM replacement: first leaf toggled');
  check(await page.evaluate(() => ({ selected: creationFixture.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds,
    inputs: [...document.querySelectorAll('[data-cam-action="tube-designer-batch-disassembly-toggle"]')].map(input => input.checked) })),
    { selected: ['instance-A'], inputs: [true, false] }, 'individual selection remains consistent after complete dialog DOM replacement');
  await leaf('instance-A').click(); await settle();
  await leaf('instance-A').click(); await settle();
  await page.evaluate(() => {
    creationFixture.beforeAggregateLeaf = document.querySelector('[data-cam-action="tube-designer-batch-disassembly-toggle"]');
  });
  await page.locator('[data-cam-action="tube-designer-batch-disassembly-toggle-all"]').click(); await settle();
  await page.locator('[data-cam-action="tube-designer-batch-disassembly-toggle-all"]').click(); await settle();
  console.log('Batch full DOM replacement: aggregate cleared');
  check(await page.evaluate(() => ({ selected: creationFixture.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds,
    inputs: [...document.querySelectorAll('[data-cam-action="tube-designer-batch-disassembly-toggle"]')].map(input => input.checked),
    replaced: !creationFixture.beforeAggregateLeaf.isConnected && creationFixture.dialogReplacementCount >= 5 })),
    { selected: [], inputs: [false, false], replaced: true }, 'master clear keeps model and replacement checkbox nodes consistent');
  await page.locator('[data-cam-action="tube-designer-batch-disassembly-close"]').last().click(); await settle();
  await page.evaluate(() => creationFixture.openBatch()); await settle();
  console.log('Batch full DOM replacement: reopened');
  check(await page.evaluate(() => ({ selected: creationFixture.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds,
    inputs: [...document.querySelectorAll('[data-cam-action="tube-designer-batch-disassembly-toggle"]')].map(input => input.checked),
    master: document.querySelector('[data-cam-action="tube-designer-batch-disassembly-toggle-all"]').checked })),
    { selected: [], inputs: [false, false], master: false }, 'last master clear overrides earlier individual selection on batch dialog reopen');
  check(await page.evaluate(() => creationFixture.calls.filter(row => row.method === 'TubeDesigner.DisassembleSelected').length), 0, 'restoring batch selection never starts disassembly');
  check(await page.evaluate(() => creationFixture.errors), [], 'actual delegated handlers complete without errors');
  check(errors, [], 'browser reports no uncaught errors');
  await page.screenshot({ path: resolve(output, 'batch-selection-cleared.png'), fullPage: true });
  await page.evaluate(() => creationFixture.viewport.dispose());
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'passed', checks, actualProductionDialogs: true,
    actualSharedViewport: true, nativeTransport: 'controlled generation and Excel export protocol', nativeCadGeneration: false,
    nativeWorkbookExport: false, standaloneCefEndToEnd: false }, null, 2));
  console.log(`TubeDesigner creation memory: passed (${checks.length} browser checks).`);
} catch (error) {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'failed', checks, errors, error: { message: error.message, stack: error.stack } }, null, 2));
  if (page) await page.screenshot({ path: resolve(output, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally { await browser.close(); }
