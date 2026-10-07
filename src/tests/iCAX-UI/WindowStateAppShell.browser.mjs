// Actual AppShell and TubeDesigner workbench; only native scene/product transport
// is controlled. Two browser processes reopen the same actual disk profile.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('window-state-appshell-20261006');
const userDataDirectory = mkdtempSync(resolve(output, 'browser-profile-'));
const launchOptions = { headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome',
  viewport: { width: 1440, height: 960 } };
const errors = [], cases = [];
let browserContext;

async function createAppShell(context) {
  const page = await context.newPage();
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://window-state-appshell.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (path === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){return globalThis.__windowStateApp.appProxy;}' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(path.slice('/src/'.length)), 'utf8') });
  });
  await page.goto('http://window-state-appshell.test/');
  await page.evaluate(async () => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const { installDialogPathMemory } = await import('/src/iCAX-UI/SDK/Bridge/createBridge.mjs');
    const projectId = 'window-memory-project', sceneId = 'window-memory-scene';
    const requests = [], nativeDialogRequests = [], nativeResults = [];
    const designer = { templates: [], instances: [], product: null, activeProductId: '', members: [], joints: [], parts: [], manufacturingGroups: [], nestingGroups: [], nestingSettings: {} };
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false, canRedo: false }, tubeDesigner: designer };
    const view = getProjectView(projectId);
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true });
    const snapshot = { viewId: 'window-memory-view', revision: '1', rows: [] };
    const sceneProxy = { state, pdo: { enabled: false }, resources: { get() { throw Error('Empty window-memory fixture requested geometry'); } },
      views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
      async getState() { return structuredClone(state); }, async invoke(method, payload) {
        requests.push({ scope: 'scene', method, payload });
        if (method === 'TubeDesigner.List') return { tubeDesigner: structuredClone(designer) };
        if (method === 'TubeDesigner.GetPunchTools') return { tools: [] };
        throw Error('Unexpected window-memory scene method: ' + method);
      } };
    const projectState = { projectId, projectName: '窗体记忆验收', mainScene: state };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const productState = { productId: 'icax.tube-designer', productName: 'TubeDesigner', isStarted: true,
      frontendEntry: '/src/apps/tube-designer/webpage/entry.mjs', catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ['ictd'] } };
    const settings = { enabled: false, inputDirectory: '', tempDirectory: '', outputDirectory: '' };
    const productProxy = { productId: productState.productId, state: productState, async getState() { return productState; },
      getProject() { return projectProxy; }, async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; },
      async invoke(method, payload) {
        requests.push({ scope: 'product', method, payload });
        if (method === 'TubeDesigner.GetBatchExcelAutomationSettings') return { settings: structuredClone(settings) };
        if (method === 'TubeDesigner.ScanBatchExcelAutomation') return { settings: structuredClone(settings), files: [], errors: [] };
        if (method === 'TubeDesigner.ListUserData') return { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], systemProfiles: [], templateProfiles: [] };
        throw Error('Unexpected window-memory product method: ' + method);
      } };
    const bridge = Object.fromEntries(['openFileDialog', 'saveFileDialog', 'openDirectoryDialog'].map(method => [method, async function (options) {
      if (this !== bridge) throw Error('Native dialog binding changed');
      nativeDialogRequests.push({ method, options }); return nativeResults.shift() ?? null;
    }]));
    globalThis.icax = bridge;
    installDialogPathMemory(bridge);
    const appProxy = { bridge, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
    globalThis.__windowStateApp = { appProxy, view, requests, nativeDialogRequests, nativeResults };
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  });
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready');
  await page.locator('.project-progress-backdrop').waitFor({ state: 'detached' });
  return page;
}

async function openNewProject(page) {
  await page.locator('[data-action="open-start-center"]').click();
  await page.locator('[data-action="open-new-project-dialog"]').click();
  await page.locator('.new-project-dialog').waitFor({ state: 'visible' });
}
async function cancelNewProject(page) {
  await page.locator('.new-project-dialog [data-action="close-new-project-dialog"]').last().click();
  await page.locator('.new-project-dialog').waitFor({ state: 'detached' });
  await page.locator('[data-action="close-start-center"]').click();
}

try {
  browserContext = await chromium.launchPersistentContext(userDataDirectory, launchOptions);
  const first = await createAppShell(browserContext);
  await openNewProject(first);
  const name = first.locator('[data-field="newProjectName"]'), path = first.locator('[data-field="newProjectPath"]');
  await name.fill('窗体最近草稿');
  await path.fill('D:\\Projects\\记忆验收.ictd');
  await name.fill('修改名称后保留路径');
  assert.equal(await path.inputValue(), 'D:\\Projects\\记忆验收.ictd', 'current explicit path survives subsequent name edits');
  await cancelNewProject(first);
  await openNewProject(first);
  assert.equal(await name.inputValue(), '修改名称后保留路径');
  assert.equal(await path.inputValue(), 'D:\\Projects\\记忆验收.ictd');
  await name.fill('重启后草稿');
  assert.equal(await path.inputValue(), 'D:\\Projects\\记忆验收.ictd', 'reopened remembered path is treated as a user draft');
  await cancelNewProject(first);
  cases.push({ name: 'actual AppShell input/cancel/reopen', passed: true });

  await first.evaluate(async () => {
    const f = __windowStateApp;
    f.nativeResults.push('D:\\CAD\\加工件.step', 'E:/outputs/last.xlsx', 'F:/folders/last');
    await icax.openFileDialog({ memoryKey: 'test-part-import', title: '导入加工件' });
    await icax.saveFileDialog({ memoryKey: 'test-list-export', title: '导出清单', defaultPath: 'last.xlsx' });
    await icax.openDirectoryDialog({ memoryKey: 'test-output-folder', title: '选择输出目录' });
  });
  // Closing the persistent context flushes Chromium's profile to disk and closes
  // the first process. No storageState/addInitScript injection supplies defaults.
  await browserContext.close();
  browserContext = null;
  browserContext = await chromium.launchPersistentContext(userDataDirectory, launchOptions);
  const next = await createAppShell(browserContext);
  await openNewProject(next);
  assert.equal(await next.locator('[data-field="newProjectName"]').inputValue(), '重启后草稿');
  assert.equal(await next.locator('[data-field="newProjectPath"]').inputValue(), 'D:\\Projects\\记忆验收.ictd');
  await next.screenshot({ path: resolve(output, 'new-project-after-browser-restart.png') });
  await cancelNewProject(next);
  cases.push({ name: 'closed browser restarts with same persistent disk profile', passed: true });

  const dialogs = await next.evaluate(async () => {
    const f = __windowStateApp;
    await icax.openFileDialog({ memoryKey: 'test-part-import', title: '导入加工件' });
    await icax.saveFileDialog({ memoryKey: 'test-list-export', title: '导出清单', defaultPath: 'next.xlsx' });
    await icax.openDirectoryDialog({ memoryKey: 'test-output-folder', title: '选择输出目录' });
    await icax.saveFileDialog({ memoryKey: 'test-list-export', defaultPath: 'C:/entity/current.ictd' });
    return f.nativeDialogRequests;
  });
  assert.equal(dialogs[0].options.defaultPath, 'D:\\CAD\\');
  assert.equal(dialogs[1].options.defaultPath, 'E:/outputs/next.xlsx');
  assert.equal(dialogs[2].options.initialDirectory, 'F:/folders/last');
  assert.equal(dialogs[3].options.defaultPath, 'C:/entity/current.ictd');
  cases.push({ name: 'shared native dialog wrapper restores paths across processes and preserves current entity path', passed: true });
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, cases, errors,
    coverage: { actualAppShell: true, actualTubeDesignerEntry: true, actualInputEvents: true,
      browserProcesses: 2, persistentProfileDirectory: userDataDirectory, persistedStorageReused: true,
      storageStateInjected: false, controlledNativeTransport: true, standaloneCef: false } }, null, 2));
  console.log(JSON.stringify({ passed: true, output, cases: cases.length }));
} finally { await browserContext?.close(); }
