// Shipped AppShell -> TubeDesigner entry -> empty project -> actual ribbon click.
// The only replacement is the native transport fixture; this does not run CEF.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('batch-excel-settings-ribbon');
const features = JSON.parse(readFileSync(new URL('../../licensing/features.json', import.meta.url))).features;
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1518, height: 1240 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://settings-ribbon.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (path === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){return globalThis.__settingsRibbon.appProxy;}' });
    if (!path.startsWith('/src/')) return route.abort();
    const file = browserAssetPath(path.slice('/src/'.length));
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(file, 'utf8') });
  });
  await page.goto('http://settings-ribbon.test/');
  await page.evaluate(async features => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const projectId = 'blank-settings-project', sceneId = 'blank-settings-scene';
    const requests = [];
    const designer = { templates: [], instances: [], product: null, activeProductId: '', members: [], joints: [], parts: [], manufacturingGroups: [] };
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false, canRedo: false }, tubeDesigner: designer };
    const view = getProjectView(projectId);
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true });
    const snapshot = { viewId: 'empty-settings-view', revision: '1', rows: [] };
    const sceneProxy = { state, pdo: { enabled: false }, resources: { get() { throw new Error('Empty scene has no geometry resources'); } },
      views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
      async getState() { return structuredClone(state); }, async invoke(method, payload) {
        requests.push({ scope: 'scene', method, payload });
        if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(designer) };
        if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
        throw new Error('Unexpected scene request in settings ribbon test: ' + method);
      } };
    const projectState = { projectId, projectName: '未命名项目', mainScene: state };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const productState = { productId: 'icax.tube-designer', productName: 'TubeDesigner', isStarted: true,
      frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs', catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ['ictd'] } };
    const settings = { enabled: false, inputDirectory: '', tempDirectory: '', outputDirectory: '' };
    const productProxy = { productId: productState.productId, state: productState, async getState() { return productState; },
      getProject() { return projectProxy; }, async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; },
      async invoke(method, payload) {
        requests.push({ scope: 'product', method, payload });
        if (method === 'TubeDesignerLicensing.Status') return { featureSchemaVersion: 1, activated: true,
          featureCatalog: features.map(({ id, label, parent }) => ({ id, label, parent })),
          capabilities: Object.fromEntries(features.map(feature => [feature.id, true])) };
        if (method === 'TubeDesignerLicensing.CheckAccess') return { granted: true, featureId: payload.featureId };
        if (method === 'TubeDesigner.GetBatchExcelAutomationSettings') return { settings: structuredClone(settings) };
        if (method === 'TubeDesigner.SaveBatchExcelAutomationSettings') {
          if (payload.settings.enabled && !payload.settings.tempDirectory) throw new Error('启用自动处理前请设置临时目录。');
          Object.assign(settings, payload.settings);
          return { settings: structuredClone(settings) };
        }
        if (method === 'TubeDesigner.ScanBatchExcelAutomation') return { settings: structuredClone(settings), files: [], errors: [] };
        throw new Error('Unexpected product request in settings ribbon test: ' + method);
      } };
    const appProxy = { bridge: { async openDirectoryDialog(options) {
        requests.push({ scope: 'bridge', method: 'openDirectoryDialog', payload: options });
        if (options.title !== '选择自动处理临时目录（temp）') throw new Error('Unexpected directory picker');
        return 'C:/automation/temp';
      } }, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
    globalThis.__settingsRibbon = { appProxy, view, state, requests };
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, features);
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready');
  const button = page.locator('[data-action="ribbon-command"][data-command-id="designer.excel.automation-settings"]');
  await button.waitFor({ state: 'visible' });
  assert.equal(await button.isEnabled(), true, 'Settings must be available before adding a product');
  assert.match(await page.locator('.cam-context-pane').innerText(), /0\s*个实例/);
  assert.match(await button.innerText(), /自动处理设置/);
  const excelCommands = await page.locator('.ribbon-group').filter({ has: page.locator('.ribbon-group-title', { hasText: /^Excel$/ }) }).locator('.ribbon-command').allTextContents();
  assert.deepEqual(excelCommands.map(text => text.trim()), ['导出模板', '导入产品', '自动处理设置']);
  await page.screenshot({ path: resolve(output, 'empty-project-ribbon.png') });
  await button.click();
  await page.getByRole('dialog', { name: 'Excel 自动处理设置', exact: true }).waitFor({ state: 'visible' });
  await page.getByLabel('侦听目录', { exact: true }).waitFor({ state: 'visible' });
  await page.waitForFunction(() => !__settingsRibbon.view.tubeDesignerExcelAutomationDialog?.loading);
  assert.equal(await page.getByLabel('侦听目录', { exact: true }).isEnabled(), true);
  assert.equal(await page.getByLabel('临时目录', { exact: true }).isEnabled(), true);
  assert.equal(await page.getByLabel('输出目录', { exact: true }).isEnabled(), true);
  assert.equal(await page.getByLabel('启用自动处理', { exact: true }).isChecked(), false);
  assert.equal(await page.getByRole('alert').count(), 0);
  await page.getByLabel('侦听目录', { exact: true }).fill('C:/automation/in');
  await page.getByLabel('输出目录', { exact: true }).fill('C:/automation/out');
  await page.locator('[data-directory-field="tempDirectory"]').click();
  await page.waitForFunction(() => __settingsRibbon.view.tubeDesignerExcelAutomationDialog?.draft.tempDirectory === 'C:/automation/temp');
  assert.equal(await page.getByLabel('临时目录', { exact: true }).inputValue(), 'C:/automation/temp');
  await page.getByLabel('启用自动处理', { exact: true }).check();
  await page.getByRole('dialog', { name: 'Excel 自动处理设置', exact: true }).getByRole('button', { name: '保存', exact: true }).click();
  await page.waitForFunction(() => !__settingsRibbon.view.tubeDesignerExcelAutomationDialog);
  await button.click();
  await page.waitForFunction(() => !__settingsRibbon.view.tubeDesignerExcelAutomationDialog?.loading);
  assert.equal(await page.getByLabel('侦听目录', { exact: true }).inputValue(), 'C:/automation/in');
  assert.equal(await page.getByLabel('临时目录', { exact: true }).inputValue(), 'C:/automation/temp');
  assert.equal(await page.getByLabel('输出目录', { exact: true }).inputValue(), 'C:/automation/out');
  assert.equal(await page.getByLabel('启用自动处理', { exact: true }).isChecked(), true);
  await page.screenshot({ path: resolve(output, 'settings-dialog.png') });
  const requests = await page.evaluate(() => __settingsRibbon.requests);
  assert(requests.some(call => call.method === 'TubeDesigner.GetBatchExcelAutomationSettings' && call.scope === 'product'));
  assert(!requests.some(call => call.method.includes('BatchExcelAutomation') && call.scope !== 'product'));
  assert.deepEqual(requests.find(call => call.method === 'TubeDesigner.SaveBatchExcelAutomationSettings')?.payload.settings,
    { enabled: true, inputDirectory: 'C:/automation/in', tempDirectory: 'C:/automation/temp', outputDirectory: 'C:/automation/out' });
  assert.deepEqual(errors, []);
  await page.getByRole('button', { name: '关闭自动处理设置', exact: true }).click();
  assert.equal(await page.getByRole('dialog', { name: 'Excel 自动处理设置', exact: true }).count(), 0);
  await page.evaluate(async () => (await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs')).stopBatchExcelAutomation());
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, emptyProject: true, commands: excelCommands.map(text => text.trim()),
    settingsButtonVisible: true, settingsButtonEnabled: true, mouseClickOpenedDialog: true, inputsEnabled: true,
    configuredTempDirectoryPicker: true, allThreeDirectoriesSavedAndRestored: true,
    requestScopes: requests, errors, nativeTransport: 'controlled native transport fixture', standaloneCefEndToEnd: false }, null, 2));
  console.log('PASS deployed AppShell and TubeDesigner: empty project displays Excel settings button; actual mouse click opens enabled settings form through product scope.');
} finally { await browser.close(); }
