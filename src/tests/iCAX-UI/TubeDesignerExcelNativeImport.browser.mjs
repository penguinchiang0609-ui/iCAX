// Production frontend actions call the deployed DLL through scope-aware stdio.
// Only the file picker and application/scene container are test adapters.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { delimiter, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('excel-import-scene-20261006/native-browser');
const runtime = resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT || browserAssetRoot);
assert(process.env.ICAX_NATIVE_SCOPE_BRIDGE, 'Set ICAX_NATIVE_SCOPE_BRIDGE to the freshly built ExcelImportScopeBridge');
const bridge = resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE);
const expectedFailure = process.argv.includes('--expect-missing-scene');
const workbook = resolve(process.env.ICAX_NATIVE_EXCEL_WORKBOOK || 'output/防盗窗导入测试.xlsx');
const env = { ...process.env };
const originalPath = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
env.Path = runtime + delimiter + originalPath;
const native = spawn(bridge, [], { cwd: runtime, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
const calls = [], stderr = [], stdout = [], waiting = new Map();
let id = 0, browser, modules, exitCode;
const exited = new Promise(resolve => native.once('exit', code => {
  exitCode = code;
  for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(new Error(`Native host exited ${code}`)); }
  waiting.clear(); resolve(code);
}));
native.stderr.on('data', bytes => stderr.push(bytes.toString()));
createInterface({ input: native.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { stdout.push(line); return; }
  const job = waiting.get(response.id);
  if (!job) { stdout.push(line); return; }
  waiting.delete(response.id); clearTimeout(job.timer);
  calls.push({ request: job.request, response, milliseconds: Date.now() - job.started });
  if (response.ok) job.resolve(response.result); else job.reject(new Error(response.error));
});
function invoke(scope, method, payload = {}) {
  const request = { id: ++id, scope, method: method.replace(/^TubeDesigner\./, ''), payload };
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error(`Native ${method} timed out`)); }, 180000);
    waiting.set(request.id, { resolve, reject, timer, request, started: Date.now() });
    native.stdin.write(JSON.stringify(request) + '\n');
  });
}
const report = {
  status: 'running', assetRoot: browserAssetRoot, nativeRuntime: runtime, bridge,
  nativeBusinessResponsesMocked: false, windowsFilePicker: 'controlled file path',
  applicationAndSceneContainer: 'isolated native fixture', standaloneCefEndToEnd: false,
  cases: [],
};
try {
  modules = (await invoke('inspection', 'GetRuntimeModules')).modules;
  report.modules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
    assert.equal(resolve(path).toLowerCase(), resolve(runtime, name).toLowerCase(), 'Native test loaded a module outside the selected installation');
    return [name, { path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex') }];
  }));
  // Real DLL, null scene: reproduce the endpoint contract independently of UI.
  await assert.rejects(invoke('product', 'GeneratePreview', {}), /GeneratePreview requires a scene/);
  const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome', headless: true });
  const page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  const browserErrors = [];
  page.on('pageerror', error => browserErrors.push(error.message));
  await page.exposeFunction('nativeExcelInvoke', invoke);
  await page.route('http://excel-native.test/**', serveBrowserAsset);
  await page.goto('http://excel-native.test/');
  await page.setContent(`<style>${tubeDesignerCss}</style><main id="mount" class="tube-designer-workspace"></main>`);

  async function importWorkbook(path) {
    await invoke('inspection', 'ResetScene');
    const initial = await invoke('scene', 'List');
    assert.equal(initial.tubeDesigner.instances.length, 0);
    await page.evaluate(async ({ initial, path }) => {
      const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
      const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
      const mount = document.querySelector('#mount');
      const f = { view: { pending: false, activeAreaId: 'product', scene: initial,
        tubeDesignerRightDraftDirty: true }, errors: [], notices: [], busy: 0 };
      f.context = {
        mount,
        appProxy: { bridge: { async openFileDialog() { return path; } } },
        productProxy: { async invoke(method, payload) {
          const response = await globalThis.nativeExcelInvoke('product', method, payload);
          if (method === 'TubeDesigner.ReadBatchExcelImport') f.readResult = structuredClone(response);
          return response;
        } },
        sceneProxy: { invoke(method, payload) { return globalThis.nativeExcelInvoke('scene', method, payload); } },
      };
      f.render = () => { mount.innerHTML = views.renderDesignerDialogs(f.view.scene.tubeDesigner, f.view)
        + views.renderDesignerRuntimeStatus(f.view)
        + `<p data-native-instance-count>${f.view.scene.tubeDesigner.instances.length}</p>`;
        if (mount.querySelector('[aria-modal="true"]')) f.confirmationRendered = true;
      };
      f.ops = { renderProject() { f.render(); }, showNotice(context, view, message) { f.notices.push(message); }, appendProjectLog() {} };
      f.run = async action => {
        f.busy++;
        try { await actions.handleDesignerAreaAction(f.context, f.view, action, null, f.ops); }
        catch (error) { f.errors.push(error.message); }
        finally { f.busy--; }
      };
      mount.onclick = event => {
        const button = event.target.closest('[data-cam-action]');
        if (button && !button.disabled) void f.run(button.dataset.camAction);
      };
      globalThis.nativeExcelFixture = f;
      await f.run('tube-designer-batch-add');
    }, { initial, path });
    const imported = await page.evaluate(() => structuredClone(globalThis.nativeExcelFixture.readResult));
    assert.equal(imported.rows.length, 1, 'Real reader must return the filled workbook row');
    const frontend = await page.evaluate(() => {
      const f = globalThis.nativeExcelFixture;
      const status = document.querySelector('[data-tube-designer-runtime-status]');
      return { errors: f.errors, notices: f.notices, importState: f.view.tubeDesignerExcelImport,
        confirmationRendered: f.confirmationRendered === true,
        modelDirty: f.view.tubeDesignerRightDraftDirty, statusHidden: status.hidden,
        instances: f.view.scene.tubeDesigner.instances, members: f.view.scene.tubeDesigner.members };
    });
    const actual = (await invoke('scene', 'List')).tubeDesigner;
    if (expectedFailure) {
      assert(frontend.errors.some(message => /GeneratePreview requires a scene/.test(message)), 'Old frontend must fail against the real scene contract');
      assert.equal(actual.instances.length, 0, 'Failed product-scope invocation must not create an instance');
      report.cases.push({ workbook: path, status: 'reproduced-missing-scene', actualInstances: 0, frontendErrors: frontend.errors });
    } else {
      assert.deepEqual(frontend.errors, []);
      assert.equal(frontend.importState, null);
      assert.equal(frontend.confirmationRendered, false, 'Choosing a workbook must not require confirmation');
      assert.equal(actual.product.modelOutdated, false);
      assert.equal(frontend.modelDirty, false, 'Import must clear the preceding product status');
      assert.equal(frontend.statusHidden, true, 'Current model must not retain a status banner');
      assert.equal(actual.instances.length, 1, 'Real repository must contain the imported instance');
      const instance = actual.instances[0];
      assert.equal(instance.templateId, imported.templateId);
      assert.equal(instance.name, imported.rows[0].instanceName);
      assert.equal(instance.quantity, imported.rows[0].instanceQuantity);
      assert(actual.members.length > 0, 'Native generation must persist actual display members');
      assert(actual.generationRun?.entityId, 'Native generation receipt must be persisted');
      assert.equal(frontend.instances[0].entityId, instance.entityId, 'Frontend must query the real created instance');
      const successfulGeneration = calls.filter(call => call.request.method === 'GeneratePreview' && call.response.ok).at(-1);
      assert.equal(successfulGeneration.request.scope, 'scene');
      report.cases.push({ workbook: path, templateId: instance.templateId, name: instance.name, quantity: instance.quantity,
        entityId: instance.entityId, members: actual.members.length, generationRun: actual.generationRun.entityId,
        generationScope: successfulGeneration.request.scope, frontendRefreshedFromNativeScene: true,
        directImportWithoutConfirmation: true, currentModelStatusHidden: true });
    }
    await page.screenshot({ path: resolve(output, `native-import-${imported.templateId}.png`) });
    return actual;
  }
  const userResult = await importWorkbook(workbook);
  if (!expectedFailure) {
    assert.equal(userResult.instances[0].parameters.width, 1200);
    assert.equal(userResult.instances[0].parameters.height, 1800);
    const fixtures = resolve('output/tests/product-parameter-visibility-20261006/TubeDesigner-20261005-excel');
    for (const templateId of ['modular-guardrail', 'modular-guardrail-cross-straight',
      'modular-guardrail-diamond-straight', 'modular-guardrail-glass-straight', 'straight-steel-staircase'])
      await importWorkbook(resolve(fixtures, templateId + '-filled.xlsx'));
  }
  assert.deepEqual(browserErrors, []);
  report.status = expectedFailure ? 'reproduced-before-fix' : 'passed';
  console.log(`Native Excel frontend import: ${report.status}; ${report.cases.length} workbook(s), deployed DLL responses.`);
} catch (error) {
  report.status = 'failed'; report.failure = { message: error.message, stack: error.stack };
  throw error;
} finally {
  await browser?.close();
  native.stdin.end();
  const timer = setTimeout(() => native.kill(), 30000);
  await exited; clearTimeout(timer);
  report.nativeExitCode = exitCode;
  writeFileSync(resolve(output, 'native-frontend-report.json'), JSON.stringify(report, null, 2));
  writeFileSync(resolve(output, 'native-calls.json'), JSON.stringify(calls, null, 2));
  writeFileSync(resolve(output, 'native-stderr.log'), stderr.join(''));
  writeFileSync(resolve(output, 'native-stdout.log'), stdout.join('\n'));
}
