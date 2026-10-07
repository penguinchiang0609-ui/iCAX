// Real browser interaction with current production modules and descriptors.
// Native workbook files, CAD generation and Windows pickers are tested apart;
// this test deliberately substitutes only their protocol responses.
import assert from 'node:assert/strict';
import { existsSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  browserAssetRoot, browserReportDirectory, importBrowserAsset, readBrowserAsset,
  serveBrowserAsset,
} from './browserPackageRuntime.mjs';

const output = browserReportDirectory('excel-acceptance-20261005/frontend');
const browserChannel = process.env.ICAX_BROWSER_CHANNEL || 'msedge';
const { catalogText } = await importBrowserAsset('apps/tube-designer/webpage/productCatalog.mjs');
const { batchExcelColumnsFromTemplate } = await importBrowserAsset('apps/tube-designer/webpage/designerActions.mjs');
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const descriptorDirectory = resolve(browserAssetRoot, 'apps/tube-designer/templates/product');
const descriptors = readdirSync(descriptorDirectory, { withFileTypes: true })
  .filter(item => item.isDirectory() && existsSync(resolve(descriptorDirectory, item.name, 'template.json'))).map(item => {
  const raw = JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${item.name}/template.json`));
  const groups = (raw.groups ?? []).map(group => ({ ...group, displayName: catalogText(group.displayName) }));
  return {
    ...raw, available: true, name: catalogText(raw.displayName), groups,
    parameters: (raw.parameters ?? []).map(field => ({
      ...field, displayName: catalogText(field.displayName),
      type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
      groupKey: field.group, group: groups.find(group => group.key === field.group)?.displayName ?? field.group,
      options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })),
    })),
  };
});
const publicTemplates = descriptors.filter(template => template.extensions?.catalog?.listed !== false);
const expectedIds = [
  'single-face-security-window', 'modular-guardrail',
  'modular-guardrail-cross-straight', 'modular-guardrail-diamond-straight',
  'modular-guardrail-glass-straight', 'straight-steel-staircase',
].sort();
const cases = [], templateReports = [];
const lifecycle = [];
let assertionCount = 0, browser, browserServer, page, stage = 'startup', intentionalBrowserClose = false;
const trace = (event, details = {}) => {
  lifecycle.push({ at: new Date().toISOString(), event, stage, intentionalBrowserClose, ...details });
  writeFileSync(resolve(output, 'browser-lifecycle.json'), JSON.stringify(lifecycle, null, 2));
};
const check = (actual, expected, message) => { assertionCount++; assert.deepEqual(actual, expected, message); };
const save = (status, error) => writeFileSync(resolve(output, 'report.json'), JSON.stringify({
  status, finishedAt: new Date().toISOString(), browserEngine: `Chromium (${browserChannel}) via Playwright`,
  assetRoot: browserAssetRoot, nativeTransport: 'controlled protocol fixture',
  nativeWorkbookRoundtrip: false, windowsFilePickerAcceptance: false, standaloneCefEndToEnd: false,
  assertions: assertionCount, templates: templateReports, cases,
  lifecycleEvidence: 'browser-lifecycle.json',
  importFailureSemantics: 'Stops at first failed row; preceding successful instances persist and are consumed; selecting the unchanged workbook resumes only remaining rows, without confirmation.',
  ...(error ? { failure: { stage, message: error.message, stack: error.stack } } : {}),
}, null, 2));

async function reset(config = {}) {
  await page.evaluate(async ({ descriptors, config }) => {
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const mount = document.querySelector('#mount');
    const f = {
      templates: structuredClone(descriptors), calls: [], pickers: [], notices: [], logs: [], errors: [], busy: 0,
      mode: 'success', savePath: 'D:\\exports\\中文产品_批量导入.xlsx', openPath: 'D:\\imports\\中文产品.xlsx',
      readRows: [], failedGenerate: 0, generated: [], ...config,
    };
    f.view = { pending: false, activeAreaId: 'product', scene: { tubeDesigner: { templates: f.templates, instances: [] } } };
    f.render = () => {
      mount.innerHTML = f.view.tubeDesignerExcelTemplateDialog
        ? views.renderBatchExcelTemplateDialog(f.view.scene.tubeDesigner, f.view)
        : views.renderDesignerDialogs(f.view.scene.tubeDesigner, f.view);
      if (mount.querySelector('[aria-modal="true"]') && !f.view.tubeDesignerExcelTemplateDialog) f.importConfirmationRendered = true;
    };
    f.bridge = {
      async saveFileDialog(options) { f.pickers.push({ kind: 'save', options }); return f.savePath; },
      async openFileDialog(options) { f.pickers.push({ kind: 'open', options }); return f.openPath; },
    };
    f.context = {
      mount, appProxy: { bridge: f.bridge },
      productProxy: { async invoke(method, payload, options) {
        f.calls.push({ scope: 'product', method, payload: structuredClone(payload), options });
        if (method === 'TubeDesigner.ExportBatchExcelTemplate') {
          if (f.mode === 'export-error') throw new Error('受控宿主：目标文件不可写');
          if (f.mode === 'export-no-path') return {};
          if (f.mode === 'export-held') await new Promise(resolve => { f.release = resolve; });
          return { templatePath: payload.targetPath };
        }
        if (method === 'TubeDesigner.ReadBatchExcelImport') {
          if (f.mode === 'read-error') throw new Error('受控宿主：工作簿列定义无效');
          const readTemplate = f.templates.find(template => template.id === 'single-face-security-window');
          return { templateId: readTemplate.id, templateName: readTemplate.name, rows: structuredClone(f.readRows) };
        }
        if (method === 'TubeDesigner.GeneratePreview') throw new Error('TubeDesigner.GeneratePreview requires a scene');
        throw new Error(`Unexpected product request ${method}`);
      } },
      sceneProxy: { async invoke(method, payload, options) {
        f.calls.push({ scope: 'scene', method, payload: structuredClone(payload), options });
        if (method === 'TubeDesigner.GeneratePreview') {
          const number = f.calls.filter(call => call.method === method).length;
          if (number === f.failedGenerate) throw new Error(`受控宿主：Excel 第 ${number + 2} 行尺寸不合法`);
          const instance = { entityId: `instance-${number}`, name: payload.instanceName, quantity: payload.instanceQuantity, parameters: structuredClone(payload) };
          f.generated.push(instance);
          f.view.scene.tubeDesigner.instances.push(instance);
          return { tubeDesigner: f.view.scene.tubeDesigner };
        }
        if (method === 'TubeDesigner.List') return { tubeDesigner: { templates: f.templates, instances: structuredClone(f.generated) } };
        if (method === 'TubeDesigner.GetTemplateDescriptor') {
          if (f.mode === 'descriptor-error') throw new Error('受控宿主：模板描述加载失败');
          if (f.mode === 'descriptor-held') await new Promise(resolve => { f.release = resolve; });
          return { template: descriptors.find(template => template.id === payload.templateId) };
        }
        throw new Error(`Unexpected scene request ${method}`);
      } },
    };
    f.ops = {
      renderProject() { f.render(); },
      showNotice(context, view, text) { f.notices.push(text); view.notice = text; },
      appendProjectLog(context, level, text) { f.logs.push({ level, text }); },
    };
    f.run = async (action, target) => {
      f.busy++;
      try { return await actions.handleDesignerAreaAction(f.context, f.view, action, target, f.ops); }
      catch (error) { f.errors.push(error.message); }
      finally { f.busy--; }
    };
    mount.onclick = event => {
      const target = event.target.closest('[data-cam-action]');
      if (target && !target.disabled) { event.preventDefault(); void f.run(target.dataset.camAction, target); }
    };
    mount.onchange = event => {
      const target = event.target.closest('[data-cam-change-action]');
      if (target && !target.disabled) void f.run(target.dataset.camChangeAction, target);
    };
    window.excelFixture = f;
    if (config.open !== false) await f.run('tube-designer-excel-template-open', null);
    if (f.errors.length) throw new Error(f.errors.at(-1));
  }, { descriptors, config });
}
const idle = () => page.waitForFunction(() => window.excelFixture.busy === 0);
const snapshot = () => page.evaluate(() => {
  const f = window.excelFixture;
  return { calls: f.calls, pickers: f.pickers, notices: f.notices, logs: f.logs, errors: f.errors,
    pending: f.view.pending, operation: f.view.tubeDesignerOperation ?? null,
    templateDialog: f.view.tubeDesignerExcelTemplateDialog ?? null,
    importState: f.view.tubeDesignerExcelImport ?? null,
    importConfirmationRendered: f.importConfirmationRendered === true,
    generated: f.generated, instances: f.view.scene.tubeDesigner.instances };
});
async function click(selector) { await page.locator(selector).last().click(); await idle(); }
async function selectTemplate(id) {
  await page.locator('[data-cam-change-action="tube-designer-excel-template-select"]').selectOption(id);
  await idle();
}
const exportButton = '[data-cam-action="tube-designer-excel-template-export"]';
const includeHeader = '[data-tube-designer-excel-select-all="include"]';
const requiredHeader = '[data-tube-designer-excel-select-all="required"]';
const rowInput = (attribute, key) => `[data-tube-designer-excel-${attribute}="${key}"]`;
async function allIncluded() {
  await page.locator(includeHeader).check(); await idle();
}
async function importRead(config = {}) {
  await reset({ open: false, ...config });
  await page.evaluate(() => window.excelFixture.run('tube-designer-batch-add', null));
  await idle();
}


try {
  check(publicTemplates.map(template => template.id).sort(), expectedIds, 'Current public product set');
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  trace('launch-start', { headless: true, assetRoot: browserAssetRoot });
  // launchServer exposes its owned process through a documented public API,
  // allowing an unexpected disconnect to retain the real exit code/signal.
  browserServer = await chromium.launchServer({ headless: true, channel: browserChannel });
  const browserProcess = browserServer.process();
  trace('browser-process-start', { pid: browserProcess.pid });
  browserProcess.on('exit', (code, signal) => trace('browser-process-exit', { pid: browserProcess.pid, code, signal }));
  browserProcess.on('error', error => trace('browser-process-error', { message: error.message }));
  browserProcess.stderr?.on('data', bytes => trace('browser-process-stderr', { message: bytes.toString().slice(0, 8000) }));
  browserServer.on('close', (code, signal) => trace('browser-server-close', { code, signal }));
  browser = await chromium.connect(browserServer.wsEndpoint());
  browser.on('disconnected', () => trace('browser-disconnected'));
  page = await browser.newPage({ viewport: { width: 1500, height: 950 } });
  page.on('close', () => trace('page-close'));
  page.on('crash', () => trace('page-crash'));
  page.context().on('close', () => trace('context-close'));
  await page.route('http://tube-designer.test/**', serveBrowserAsset);
  await page.goto('http://tube-designer.test/');
  await page.setContent(`<style>${tubeDesignerCss}</style><main id="mount" class="tube-designer-workspace"></main>`);
  const browserErrors = [];
  page.on('pageerror', error => browserErrors.push(error.message));

  stage = 'public catalogue and all template columns';
  await reset();
  check((await page.locator('[data-cam-change-action="tube-designer-excel-template-select"] option').evaluateAll(nodes => nodes.map(node => node.value))).sort(), expectedIds);
  await page.evaluate(() => {
    const f = window.excelFixture;
    f.templates.push(structuredClone(f.templates.find(template => template.id === 'single-face-security-window')));
    f.render();
  });
  check(await page.locator('[data-cam-change-action="tube-designer-excel-template-select"] option').count(), 6, 'Duplicate catalogue descriptors must produce one option');
  cases.push({ name: 'catalogue', passed: true, publicTemplates: 6, hiddenValidationTemplatesExcluded: 3, retiredTemplatesExcluded: true, duplicateIdsDeduplicated: true });

  for (const template of publicTemplates) {
    stage = `template ${template.id}`;
    trace('template-start', { templateId: template.id });
    await reset();
    await selectTemplate(template.id);
    const columns = batchExcelColumnsFromTemplate(template);
    check(columns[0].title, '实例名称'); check(columns[1].title, '生产数量');
    check(new Set(columns.map(column => column.title)).size, columns.length, 'Chinese names must be unique');
    check(columns.every(column => column.title.trim() && /[\u3400-\u9fff]/u.test(column.title)), true, 'Visible column names must contain Chinese');
    check(columns.filter(column => column.inputKind === 'select').every(column => column.options.every(option => option.label
      && (/[\u3400-\u9fff]/u.test(option.label) || /^[\d\s.·×/ΦφØ+-]+$/u.test(option.label)))), true, 'Options use Chinese labels or literal numeric tube specifications');
    const excluded = template.parameters.filter(field => field.readOnly || field.presentation?.visible === false || ['installation', 'project_rules'].includes(field.groupKey)).map(field => field.key);
    check(columns.every(column => !excluded.includes(column.key)), true, 'Derived and project-only inputs must be absent');
    check(await page.locator('[data-cam-change-action="tube-designer-excel-template-select"]').inputValue(), template.id, 'Selected template is bound before clicking the dropdown');
    check(await page.locator('[data-tube-designer-excel-include]').count(), columns.length);
    check(await page.locator('[data-tube-designer-excel-alias]').count(), columns.length);
    check(await page.locator('.tube-designer-excel-include').allTextContents(), columns.map(() => ''));
    check(await page.locator('.tube-designer-excel-required').allTextContents(), columns.map(() => ''));
    const actualDefaults = await page.locator('[data-tube-designer-excel-default]').evaluateAll(nodes => Object.fromEntries(nodes.map(node => [node.getAttribute('data-tube-designer-excel-default'), node.value])));
    check(actualDefaults, Object.fromEntries(columns.map(column => [column.key, column.defaultValue])), 'Browser default controls preserve descriptor values');
    await allIncluded();
    await page.locator(requiredHeader).check(); await idle();
    await page.locator(requiredHeader).uncheck(); await idle();
    check(await page.locator('[data-tube-designer-excel-required]').evaluateAll(nodes => nodes.every(node => !node.checked)), true);
    await page.locator(requiredHeader).check(); await idle();
    check(await page.locator('[data-tube-designer-excel-required]').evaluateAll(nodes => nodes.every(node => node.checked)), true);
    await page.locator(rowInput('include', '__instanceName')).uncheck(); await idle();
    check(await page.locator(includeHeader).evaluate(node => node.indeterminate && !node.checked), true);
    await page.locator(rowInput('include', '__instanceName')).check(); await idle();
    await page.locator(rowInput('required', '__instanceName')).uncheck(); await idle();
    check(await page.locator(requiredHeader).evaluate(node => node.indeterminate && !node.checked), true);
    const aliasColumn = columns.find(column => !column.key.startsWith('__') && column.inputKind === 'number');
    check(Boolean(aliasColumn), true);
    await page.locator(rowInput('alias', aliasColumn.key)).fill('验收尺寸别名');
    await page.locator(rowInput('default', '__instanceQuantity')).fill('3');
    const enumColumn = columns.find(column => column.inputKind === 'select' && column.options.length > 1);
    if (enumColumn) await page.locator(rowInput('default', enumColumn.key)).selectOption(enumColumn.options.at(-1).value);
    await click(exportButton);
    const result = await snapshot(), request = result.calls.find(call => call.method === 'TubeDesigner.ExportBatchExcelTemplate');
    check(result.errors, []); check(result.templateDialog, null); check(result.pending, false); check(result.operation, null);
    check(request.payload.templateId, template.id); check(request.payload.columns.length, columns.length);
    check(request.payload.columns.find(column => column.key === aliasColumn.key).title, '验收尺寸别名');
    check(request.payload.columns.find(column => column.key === '__instanceQuantity').defaultValue, '3');
    check(request.payload.columns.find(column => column.key === '__instanceName').required, false);
    if (enumColumn) check(request.payload.columns.find(column => column.key === enumColumn.key).defaultValue, enumColumn.options.at(-1).value);
    check(result.pickers[0].options.defaultExtension, 'xlsx');
    check(result.pickers[0].options.filters[0].extensions, ['xlsx']);
    check(result.notices.at(-1), '已导出 Excel 模板：D:\\exports\\中文产品_批量导入.xlsx');
    templateReports.push({ id: template.id, name: template.name, passed: true, columns: columns.length,
      enums: columns.filter(column => column.inputKind === 'select').length,
      excludedReadOnlyOrProjectKeys: excluded, localizedNamesAndOptions: true, exportPayloadValidated: true });
    trace('template-passed', { templateId: template.id, assertionCount });
  }
  await reset(); await allIncluded();
  await page.screenshot({ path: resolve(output, 'template-dialog.png'), fullPage: true });

  stage = 'export cancellations and validation';
  await reset({ savePath: '' }); await click(exportButton);
  let result = await snapshot();
  check(result.calls.length, 0); check(Boolean(result.templateDialog), true); check(result.pending, false);
  cases.push({ name: 'save-picker-cancel', passed: true });
  await reset(); await page.locator(includeHeader).check(); await idle(); await page.locator(includeHeader).uncheck(); await idle();
  await click(exportButton); result = await snapshot();
  check(result.errors.at(-1), '请至少勾选一个需要携带到 Excel 的字段。'); check(result.pickers.length, 0); check(result.calls.length, 0);
  cases.push({ name: 'no-columns-rejected-before-picker', passed: true });
  await reset();
  await page.locator(rowInput('alias', '__instanceName')).fill('生产数量');
  await click(exportButton); result = await snapshot();
  check(result.errors.at(-1), 'Excel 列名或别名不能重复：生产数量'); check(result.pickers.length, 0); check(result.calls.length, 0);
  cases.push({ name: 'duplicate-alias-rejected-before-picker', passed: true });
  await reset(); await page.evaluate(() => { delete window.excelFixture.bridge.saveFileDialog; });
  await click(exportButton); result = await snapshot();
  check(result.errors.at(-1), '当前宿主没有提供保存文件能力。'); check(result.calls.length, 0);
  cases.push({ name: 'missing-save-host', passed: true });
  for (const [mode, expected] of [['export-error', '受控宿主：目标文件不可写'], ['export-no-path', 'Excel 模板导出没有返回文件路径。']]) {
    await reset({ mode }); await click(exportButton); result = await snapshot();
    check(result.errors.at(-1), expected); check(result.pending, false); check(result.operation, null); check(Boolean(result.templateDialog), true);
    cases.push({ name: mode, passed: true, draftPreserved: true });
  }
  await reset({ mode: 'export-held' }); await page.locator(exportButton).click();
  await page.waitForFunction(() => typeof window.excelFixture.release === 'function');
  check(await page.locator('[data-tube-designer-excel-template-form] input, [data-tube-designer-excel-template-form] select, .tube-designer-dialog-footer button').evaluateAll(nodes => nodes.every(node => node.disabled)), true);
  await page.evaluate(() => window.excelFixture.release()); await idle();
  check((await snapshot()).templateDialog, null); cases.push({ name: 'export-pending-disables-controls', passed: true });
  await reset(); await click('[data-cam-action="tube-designer-excel-template-close"]');
  check((await snapshot()).templateDialog, null); check((await snapshot()).calls.length, 0);
  cases.push({ name: 'template-dialog-cancel', passed: true });

  stage = 'descriptor lazy load and failure';
  await reset();
  const firstSelectedId = (await snapshot()).templateDialog.templateId;
  const secondId = publicTemplates.find(template => template.id !== firstSelectedId).id;
  await page.evaluate(id => {
    const f = window.excelFixture;
    f.templates = f.templates.map(template => template.id === id ? { ...template, parameters: undefined } : template);
    f.view.scene.tubeDesigner.templates = f.templates;
    delete f.view.tubeDesignerTemplateDescriptors[id];
  }, secondId);
  await selectTemplate(secondId); result = await snapshot();
  check(result.calls.filter(call => call.method === 'TubeDesigner.GetTemplateDescriptor').length, 1);
  check(result.templateDialog.templateId, secondId); check(result.errors, []);
  cases.push({ name: 'lazy-load-template-descriptor', passed: true });
  await reset({ mode: 'descriptor-error' });
  const oldId = (await snapshot()).templateDialog.templateId;
  await page.evaluate(id => {
    const f = window.excelFixture;
    f.templates = f.templates.map(template => template.id === id ? { ...template, parameters: undefined } : template);
    f.view.scene.tubeDesigner.templates = f.templates;
    delete f.view.tubeDesignerTemplateDescriptors[id];
  }, secondId);
  await selectTemplate(secondId); result = await snapshot();
  check(result.templateDialog.templateId, oldId); check(result.templateDialog.loading, false);
  check(result.errors.at(-1), '受控宿主：模板描述加载失败');
  cases.push({ name: 'descriptor-load-error-preserves-previous-template', passed: true });

  stage = 'import picker/read failures';
  await importRead({ openPath: '' }); result = await snapshot();
  check(result.calls.length, 0); check(result.importState, null); check(result.pending, false);
  cases.push({ name: 'open-picker-cancel', passed: true });
  await importRead(); result = await snapshot();
  check(result.errors.at(-1), 'Excel 中没有可导入的数据行。请从第 3 行开始填写产品数据。');
  check(result.importState, null); check(result.pending, false); check(result.operation, null);
  cases.push({ name: 'empty-workbook-read', passed: true });
  await importRead({ mode: 'read-error' }); result = await snapshot();
  check(result.errors.at(-1), '受控宿主：工作簿列定义无效'); check(result.pending, false); check(result.operation, null);
  cases.push({ name: 'malformed-workbook-read', passed: true });
  await reset({ open: false }); await page.evaluate(async () => {
    const f = window.excelFixture; delete f.bridge.openFileDialog; await f.run('tube-designer-batch-add', null);
  }); result = await snapshot();
  check(await page.evaluate(() => window.excelFixture.view.error), '当前宿主没有提供文件选择能力。'); check(result.calls.length, 0);
  cases.push({ name: 'missing-open-host', passed: true });

  stage = 'direct import and multiple instances';
  const rows = [
    { sourceRow: 3, instanceName: '甲产品', instanceQuantity: 2, parameters: { width: 1100, height: 800 } },
    { sourceRow: 4, instanceName: '乙产品', instanceQuantity: 5, parameters: { width: 1400, height: 900 } },
    { sourceRow: 8, instanceName: '', instanceQuantity: 1, parameters: { width: 1600, height: 1000 } },
  ];
  await importRead({ readRows: rows }); result = await snapshot();
  check(result.calls[0].method, 'TubeDesigner.ReadBatchExcelImport');
  check(result.calls[0].payload.sourcePath, 'D:\\imports\\中文产品.xlsx');
  check(result.pickers[0].options.filters[0].extensions, ['xlsx']);
  check(result.errors, []); check(result.importState, null); check(result.pending, false); check(result.operation, null);
  check(result.importConfirmationRendered, false);
  const generateCalls = result.calls.filter(call => call.method === 'TubeDesigner.GeneratePreview');
  check(generateCalls.map(call => call.scope), rows.map(() => 'scene'), 'Instance generation must use the scene proxy');
  check(generateCalls.map(call => call.payload), rows.map(row => ({ templateId: 'single-face-security-window', ...row.parameters, instanceName: row.instanceName, instanceQuantity: row.instanceQuantity })));
  check(result.generated.length, 3); check(result.instances.length, 3);
  check(result.calls.filter(call => call.method === 'TubeDesigner.List').length, 1);
  check(result.notices.at(-1), '已按 Excel 创建 3 个产品实例。');
  cases.push({ name: 'direct-import-three-independent-products', passed: true, sourceRows: [3, 4, 8], quantities: [2, 5, 1], confirmation: false });

  await importRead({ readRows: rows, failedGenerate: 2 }); result = await snapshot();
  check(result.errors.at(-1), 'Excel 第 4 行导入失败：受控宿主：Excel 第 4 行尺寸不合法；已创建 1 个实例。');
  check(result.pending, false); check(result.operation, null); check(result.importConfirmationRendered, false);
  check(result.generated.length, 1); check(result.instances.length, 1);
  check(result.importState.rows, rows.slice(1)); check(result.importState.createdCount, 1);
  check(result.calls.filter(call => call.method === 'TubeDesigner.List').length, 1);
  cases.push({ name: 'partial-failure-shows-persisted-first-instance', passed: true, failedSourceRow: 4 });

  stage = 'retry unchanged workbook without duplicate instances';
  await page.evaluate(async () => { const f = window.excelFixture; f.failedGenerate = 0; await f.run('tube-designer-batch-add', null); });
  result = await snapshot();
  check(result.generated.map(instance => instance.name), rows.map(row => row.instanceName));
  check(result.notices.at(-1), '已按 Excel 创建 3 个产品实例。');
  check(result.calls.filter(call => call.method === 'TubeDesigner.GeneratePreview').map(call => call.payload.instanceName), ['甲产品', '乙产品', '乙产品', '']);
  check(result.importState, null); check(result.importConfirmationRendered, false);
  cases.push({ name: 'unchanged-workbook-retry-only-remaining-rows', passed: true });

  await importRead({ readRows: rows, failedGenerate: 2 });
  await page.evaluate(async () => {
    const f = window.excelFixture; f.failedGenerate = 0;
    f.readRows = [{ sourceRow: 3, instanceName: '修改后的工作簿', instanceQuantity: 1, parameters: { width: 1000 } }];
    await f.run('tube-designer-batch-add', null);
  });
  result = await snapshot();
  check(result.generated.map(instance => instance.name), ['甲产品', '修改后的工作簿']);
  check(result.notices.at(-1), '已按 Excel 创建 1 个产品实例。');
  cases.push({ name: 'changed-workbook-starts-new-import', passed: true });

  stage = 'large workbook direct import';
  const manyRows = Array.from({ length: 15 }, (_, index) => ({ sourceRow: index + 3,
    instanceName: index ? `产品 ${index + 1}` : '<img src=x onerror="window.excelEscaped=false">',
    instanceQuantity: index + 1, parameters: {} }));
  await page.setViewportSize({ width: 1100, height: 600 });
  await importRead({ readRows: manyRows }); result = await snapshot();
  check(result.generated.length, 15); check(result.importConfirmationRendered, false);
  check(result.importState, null); check(result.generated[0].name, manyRows[0].instanceName);
  check(await page.locator('img').count(), 0);
  cases.push({ name: 'small-viewport-fifteen-rows-direct-import', passed: true, rows: 15, confirmation: false });
  check(browserErrors, []);
  trace('assertions-passed', { assertionCount });
  save('passed');
  console.log(`Excel browser acceptance passed: ${publicTemplates.length} current public templates; ${cases.length} cases; ${assertionCount} assertions. Native transport and file pickers were controlled fixtures.`);
} catch (error) {
  trace('test-error', { message: error.message, assertionCount });
  save('failed', error);
  if (page) await page.screenshot({ path: resolve(output, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally {
  intentionalBrowserClose = true;
  trace('test-cleanup-start');
  await browser?.close();
  await browserServer?.close();
  trace('test-cleanup-finished');
}
