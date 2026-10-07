// Actual installed browser modules, registered SDOs and persisted native scene.
// The application/viewport containers are controlled test adapters, not CEF.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { delimiter, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

assert(process.env.ICAX_NATIVE_RUNTIME_ROOT && process.env.ICAX_NATIVE_SCOPE_BRIDGE,
  'Set ICAX_NATIVE_RUNTIME_ROOT and ICAX_NATIVE_SCOPE_BRIDGE to the selected Release installation and BatchExcelAutomationScopeBridge');
const runtime = resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT);
const bridge = resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE);
const output = browserReportDirectory('release-security-window-add-20261007/manual-browser');
const descriptorFixtureArgument = process.argv.find(value => value.startsWith('--descriptor-fixture='));
const descriptorFixturePath = descriptorFixtureArgument ? resolve(descriptorFixtureArgument.slice('--descriptor-fixture='.length)) : null;
const expectedEmptyProfile = process.argv.includes('--expect-empty-profile');
assert(!descriptorFixturePath || expectedEmptyProfile, 'Descriptor fixtures are only accepted for explicit historical failure reproduction');
const descriptorFixture = descriptorFixturePath ? JSON.parse(readFileSync(descriptorFixturePath, 'utf8')) : null;
const templateId = 'single-face-security-window';
const userData = resolve(process.env.ICAX_AUTOMATION_USER_DATA || resolve(output, 'isolated-user-data'));
mkdirSync(userData, { recursive: true });
const environment = { ...process.env, ICAX_AUTOMATION_USER_DATA: userData };
const previousPath = Object.entries(environment).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
for (const key of Object.keys(environment)) if (key.toLowerCase() === 'path') delete environment[key];
environment.Path = runtime + delimiter + previousPath;
const native = spawn(bridge, [], { cwd: runtime, env: environment, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
const waiting = new Map(), calls = [], stdout = [], stderr = [];
let sequence = 0, browser, nativeExitCode;
const exited = new Promise(resolveExit => native.once('exit', code => {
  nativeExitCode = code;
  for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(new Error(`Native host exited ${code}`)); }
  waiting.clear(); resolveExit(code);
}));
native.on('error', error => {
  for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(error); }
  waiting.clear();
});
native.stderr.on('data', bytes => stderr.push(bytes.toString()));
createInterface({ input: native.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { stdout.push(line); return; }
  const job = waiting.get(response.id);
  if (!job) { stdout.push(line); return; }
  waiting.delete(response.id); clearTimeout(job.timer);
  calls.push({ request: job.request, response, milliseconds: Date.now() - job.started });
  response.ok ? job.resolve(response.result) : job.reject(new Error(response.error));
});
function invoke(scope, method, payload = {}) {
  const request = { id: ++sequence, scope, method: method.replace(/^TubeDesigner\./, ''), payload };
  return new Promise((resolveResult, reject) => {
    const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error(`Native ${method} timed out`)); }, 180000);
    waiting.set(request.id, { resolve: resolveResult, reject, timer, request, started: Date.now() });
    native.stdin.write(JSON.stringify(request) + '\n');
  });
}
const report = { status: 'running', assetRoot: browserAssetRoot, nativeRuntime: runtime, bridge, userData,
  descriptorFixturePath, nativeBusinessResponsesMocked: false,
  applicationAndSceneContainer: 'isolated registered native SDO fixture',
  viewportContainer: 'controlled acknowledgement checked against actual persisted native member IDs and generation correlation',
  windowsFilePicker: 'not used for manual product addition', standaloneCefEndToEnd: false, checks: [] };

try {
  const { modules } = await invoke('inspection', 'GetRuntimeModules');
  report.modules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
    assert.equal(resolve(path).toLowerCase(), resolve(runtime, name).toLowerCase(), 'Native test loaded a DLL outside the selected installation');
    return [name, { path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex') }];
  }));
  await assert.rejects(invoke('product', 'GeneratePreview', {}), /GeneratePreview requires a scene/);
  report.checks.push('Product-scoped GeneratePreview is rejected by the real registered SDO');
  const initial = await invoke('scene', 'List');
  assert.equal(initial.tubeDesigner.instances.length, 0);
  assert(initial.tubeDesigner.templates.some(template => template.id === templateId && template.available), 'Installed native catalogue must contain the security window');
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome', headless: true });
  const page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  const browserErrors = [];
  page.on('pageerror', error => browserErrors.push(error.message));
  await page.exposeFunction('nativeSecurityWindowInvoke', invoke);
  await page.route('http://security-window-native.test/**', serveBrowserAsset);
  await page.goto('http://security-window-native.test/');
  const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  await page.setContent(`<style>${tubeDesignerCss}</style><main id="mount" class="tube-designer-workspace"></main>`);
  await page.evaluate(async ({ initial, templateId }) => {
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const resources = await import('/src/apps/tube-designer/webpage/productResourceBindings.mjs');
    const conditions = await import('/src/apps/tube-designer/webpage/parameterConditions.mjs');
    // The fixture focuses one native catalogue entry; its descriptor is still
    // loaded through the production action and real scene SDO.
    initial.tubeDesigner.templates = initial.tubeDesigner.templates.filter(template => template.id === templateId);
    const f = { actions, views, resources, conditions, errors: [], notices: [], logs: [], viewSync: [],
      view: { pending: false, activeAreaId: 'view', tubeDesignerLoaded: true, scene: initial } };
    let appliedRevision = '0', renderSequence = 0;
    f.view.viewport = { setContinuousRendering() {}, setCustomVisibleEntityIds() {},
      setViewDirection() { return true; },
      fitViewForRevision(revision) { return { fitted: true, revision, renderSequence: ++renderSequence }; },
      getAppliedViewState() { return { revision: appliedRevision, renderSequence }; } };
    f.context = { mount: document.querySelector('#mount'), project: { projectId: 'native-security-window-add' },
      productProxy: { invoke(method, payload) { return globalThis.nativeSecurityWindowInvoke('product', method, payload); } },
      sceneProxy: { invoke(method, payload) { return globalThis.nativeSecurityWindowInvoke('scene', method, payload); } }, actions: {} };
    f.render = () => { f.context.mount.innerHTML = views.renderDesignerDialogs(f.view.scene.tubeDesigner, f.view)
      + views.renderDesignerRuntimeStatus(f.view)
      + `<p data-native-instance-count>${f.view.scene.tubeDesigner.instances.length}</p>`; };
    f.ops = { renderProject: f.render, showNotice(context, view, message) { f.notices.push(message); },
      appendProjectLog(context, level, message) { f.logs.push({ level, message }); },
      async refreshActiveAreaView(context, view, expectation) {
        const actual = (await f.context.sceneProxy.invoke('TubeDesigner.List', {})).tubeDesigner;
        const entityIds = actual.members.map(member => member.entityId);
        if (JSON.stringify([...entityIds].sort()) !== JSON.stringify([...expectation.expectedEntityIds].sort()))
          throw new Error('Viewport container member IDs differ from the persisted native scene');
        if (expectation.correlationId !== actual.product.activeGenerationRunId)
          throw new Error('Viewport container correlation differs from the actual native generation run');
        appliedRevision = `native-window-${f.viewSync.length + 1}`;
        f.viewSync.push({ expectation, revision: appliedRevision, entityIds, productEntityId: actual.product.entityId });
        return { revision: appliedRevision, viewportReceipt: { applied: true, revision: appliedRevision, entityIds, renderSequence: ++renderSequence } };
      } };
    f.context.actions.refreshActiveSceneState = () => f.context.sceneProxy.invoke('TubeDesigner.List', {});
    f.run = async (action, target = null) => {
      f.busy = true;
      try { return await actions.handleDesignerAreaAction(f.context, f.view, action, target, f.ops); }
      catch (error) { f.errors.push(error.message); }
      finally { f.busy = false; }
    };
    f.context.mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target && !target.disabled) void f.run(target.dataset.camAction, target);
    });
    globalThis.nativeSecurityWindowFixture = f;
    if (!await actions.refreshDesignerUserData(f.context, f.view, f.ops)) throw new Error(f.view.tubeDesignerUserDataError);
    await f.run('tube-designer-open-add');
  }, { initial, templateId });
  await page.getByRole('dialog').waitFor();
  const loaded = await page.evaluate(() => {
    const f = nativeSecurityWindowFixture;
    const template = f.view.scene.tubeDesigner.templates[0];
    return { errors: f.errors, version: template.version, quantity: f.view.tubeDesignerAddInstanceQuantity,
      systemProfiles: f.view.tubeDesignerSystemProfiles.length, descriptorLoaded: Array.isArray(template.parameters) };
  });
  assert.deepEqual(loaded.errors, []);
  assert(loaded.descriptorLoaded && loaded.systemProfiles > 0);
  assert.equal(loaded.quantity, 1, 'Opening the real creation dialog must default production quantity to one');
  report.installedTemplateVersion = loaded.version;
  report.checks.push('Installed native descriptor and system profiles hydrate the real add dialog; default quantity is one');

  if (!expectedEmptyProfile) {
    const defaults = await page.evaluate(() => {
      const f = nativeSecurityWindowFixture;
      return f.views.getDefaultParameters(f.view.scene.tubeDesigner.templates, f.view.tubeDesignerAddTemplateId);
    });
    const preview = await invoke('scene', 'GenerateProductTemplatePreview', { templateId, parameters: defaults });
    assert.deepEqual(preview.parameters, defaults, 'Product preview must return exactly the host normalized default inputs');
    report.checks.push('Native GenerateProductTemplatePreview validates the affected descriptor and preserves host normalized default parameters');
    report.previewParameterCount = Object.keys(preview.parameters).length;
  }

  if (descriptorFixture) {
    // Preserve the historical deployed manifest exactly. Only host display
    // metadata comes from the live descriptor; no missing resource default is
    // fabricated, and no successful native business response is substituted.
    await page.evaluate(async descriptorFixture => {
      const f = nativeSecurityWindowFixture;
      const live = f.view.scene.tubeDesigner.templates[0];
      const template = { ...live, ...descriptorFixture, available: true, descriptorLoaded: true };
      const values = f.views.getDefaultParameters([template], template.id);
      f.reproduction = { version: template.version, roleDefaults: template.extensions?.resourceRoles?.profiles };
      try { await f.actions.prepareProductProfileBindings(f.context, f.view, template, values); }
      catch (error) { f.reproduction.error = error.message; }
    }, descriptorFixture);
  } else {
    await page.locator('[data-cam-action="tube-designer-confirm-add"]').click();
    await page.waitForFunction(() => !nativeSecurityWindowFixture.busy, null, { timeout: 180000 });
    if (!expectedEmptyProfile) await page.waitForFunction(() => nativeSecurityWindowFixture.view.scene.tubeDesigner.receiptOnly !== true, null, { timeout: 180000 });
  }
  const frontend = await page.evaluate(() => {
    const f = nativeSecurityWindowFixture;
    return { errors: f.errors, error: f.view.error, notices: f.notices, reproduction: f.reproduction,
      dialogOpen: f.view.tubeDesignerAddDialogOpen, viewSync: f.viewSync,
      instances: f.view.scene.tubeDesigner.instances, members: f.view.scene.tubeDesigner.members,
      operation: f.view.tubeDesignerLastOperation,
      statusHidden: document.querySelector('[data-tube-designer-runtime-status]').hidden };
  });
  const actual = (await invoke('scene', 'List')).tubeDesigner;
  if (expectedEmptyProfile) {
    const errors = [...frontend.errors, frontend.reproduction?.error].filter(Boolean);
    assert(errors.some(message => /^当前构件缺少支持的管型：$/.test(message)), 'Historical deployed descriptor must reproduce the empty resource selection error');
    assert.equal(actual.instances.length, 0, 'The rejected profile binding must not create an instance');
    report.reproduction = { version: frontend.reproduction?.version || loaded.version, errors, persistedInstances: 0,
      source: descriptorFixturePath ? 'unmodified historical deployed template snapshot with current production binding action' : 'selected native installation' };
    report.status = 'reproduced-before-fix';
  } else {
    assert.deepEqual(frontend.errors, []);
    assert.equal(frontend.error, '');
    assert.equal(frontend.dialogOpen, false);
    assert.equal(actual.instances.length, 1, 'The native scene must contain the manually added instance');
    const instance = actual.instances[0];
    assert.equal(instance.templateId, templateId);
    assert.equal(instance.quantity, 1);
    assert(actual.members.length > 0, 'Native generation must persist actual display members');
    assert(actual.generationRun?.entityId);
    assert.equal(actual.product.modelOutdated, false);
    assert.equal(frontend.instances[0].entityId, instance.entityId);
    assert.deepEqual(frontend.members.map(member => member.entityId).sort(), actual.members.map(member => member.entityId).sort());
    assert.equal(frontend.statusHidden, true, 'A successfully generated current model must not show a persistent status banner');
    const generation = calls.filter(call => call.request.method === 'GeneratePreview' && call.response.ok).at(-1);
    assert.equal(generation.request.scope, 'scene');
    assert(calls.some(call => call.request.method === 'EvaluateProfilePackage' && call.response.ok), 'Product profile snapshots must be evaluated through the real native API');
    report.instance = { entityId: instance.entityId, templateId, quantity: instance.quantity, name: instance.name,
      generationRunId: actual.generationRun.entityId, displayMembers: actual.members.length };
    report.viewSync = frontend.viewSync;
    report.checks.push('Real product profile evaluation and scene generation persist the instance, quantity and display members',
      'Production action closes the add dialog, verifies member IDs with native List, and hides current-model status');
    report.status = 'passed';
  }
  assert.deepEqual(browserErrors, []);
  await page.screenshot({ path: resolve(output, expectedEmptyProfile ? 'before-fix-browser.png' : 'security-window-added.png') });
  console.log(`Security window native manual addition: ${report.status}`);
} catch (error) {
  report.status = 'failed'; report.failure = { message: error.message, stack: error.stack }; throw error;
} finally {
  await browser?.close();
  native.stdin.end();
  const timer = setTimeout(() => native.kill(), 30000);
  await exited; clearTimeout(timer);
  report.nativeExitCode = nativeExitCode;
  writeFileSync(resolve(output, 'native-manual-add-report.json'), JSON.stringify(report, null, 2));
  writeFileSync(resolve(output, 'native-calls.json'), JSON.stringify(calls, null, 2));
  writeFileSync(resolve(output, 'native-stdout.log'), stdout.join('\n'));
  writeFileSync(resolve(output, 'native-stderr.log'), stderr.join(''));
}
