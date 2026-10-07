// Production import and batch-disassembly controls, registered real native SDOs.
// Only the file picker and application/scene container are test adapters.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { delimiter, join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

assert(process.env.ICAX_NATIVE_RUNTIME_ROOT && process.env.ICAX_NATIVE_SCOPE_BRIDGE,
  'Select the real Debug runtime and scope-aware bridge');
assert(process.env.ICAX_BROWSER_RUNTIME_ROOT, 'Select deployed production browser assets explicitly');
assert(process.env.ICAX_NATIVE_TEST_PYTHON, 'Set the bundled Python path for the three-row workbook fixture');
const runtime = resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT);
const bridge = resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE);
const output = browserReportDirectory('product-batch-disassembly-20261006/native-browser');
const scenario = mkdtempSync(join(output, 'case-'));
const userData = join(scenario, '用户数据');
mkdirSync(userData);
const sourceWorkbook = resolve(process.env.ICAX_BATCH_DISASSEMBLY_WORKBOOK || 'output/防盗窗款式批量导入测试.xlsx');
const workbook = join(scenario, '批量拆单三款式.xlsx');
const prepare = String.raw`import re,sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as source,zipfile.ZipFile(sys.argv[2],'w') as target:
 for entry in source.infolist():
  data=source.read(entry.filename)
  if entry.filename=='xl/worksheets/sheet1.xml':
   text=data.decode('utf-8')
   text=re.sub(r'<(?:\w+:)?row\b[^>]*\br="(\d+)"[^>]*>.*?</(?:\w+:)?row>',lambda m:m.group(0) if int(m.group(1))<=5 else '',text,flags=re.S)
   data=text.encode('utf-8')
  target.writestr(entry,data)`;
const prepared = spawnSync(process.env.ICAX_NATIVE_TEST_PYTHON, ['-c', prepare, sourceWorkbook, workbook],
  { encoding: 'utf8', windowsHide: true });
assert.equal(prepared.status, 0, prepared.stderr);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const env = { ...process.env, ICAX_AUTOMATION_USER_DATA: userData };
const oldPath = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
env.Path = runtime + delimiter + oldPath;
const native = spawn(bridge, [], { cwd: runtime, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
const calls = [], waiting = new Map(), stderr = [], stdout = [];
let sequence = 0, browser, page, nativeExitCode;
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
function compactDesigner(designer) {
  if (!designer) return designer;
  const part = value => ({ entityId: value.entityId, stableKey: value.stableKey, status: value.status,
    quantity: value.quantity, unitQuantity: value.unitQuantity, instanceQuantity: value.instanceQuantity,
    length: value.length, manufacturingGeometryResourceId: value.manufacturingGeometryResourceId,
    manufacturingGeometryResourceVersion: value.manufacturingGeometryResourceVersion });
  return { ...designer,
    members: designer.members?.map(value => ({ entityId: value.entityId, stableKey: value.stableKey,
      previewGeometryResourceId: value.previewGeometryResourceId, transform: value.transform })),
    parts: designer.parts?.map(part),
    manufacturingGroups: designer.manufacturingGroups?.map(group => ({ ...group, parts: group.parts?.map(part) })) };
}
createInterface({ input: native.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { stdout.push(line); return; }
  const job = waiting.get(response.id);
  if (!job) { stdout.push(line); return; }
  waiting.delete(response.id); clearTimeout(job.timer);
  const audited = { ...response, result: response.result ? { ...response.result,
    ...(response.result.tubeDesigner ? { tubeDesigner: compactDesigner(response.result.tubeDesigner) } : {}) } : undefined };
  calls.push({ request: job.request, response: audited, milliseconds: Date.now() - job.started });
  response.ok ? job.resolve(response.result) : job.reject(new Error(response.error));
});
function invoke(scope, method, payload = {}) {
  const request = { id: ++sequence, scope, method: method.replace(/^TubeDesigner\./, ''), payload };
  return new Promise((resolveRequest, reject) => {
    const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error(`Native ${method} timed out`)); },
      request.method === 'DisassembleSelected' ? 900000 : 180000);
    waiting.set(request.id, { request, resolve: resolveRequest, reject, timer, started: Date.now() });
    native.stdin.write(JSON.stringify(request) + '\n');
  });
}
const report = { status: 'running', assetRoot: browserAssetRoot, nativeRuntime: runtime, bridge,
  workbook, workbookSha256: sha(readFileSync(workbook)), sourceWorkbook, userData,
  nativeBusinessResponsesMocked: false,
  windowsFilePicker: 'controlled real workbook path; Windows file picker UI not exercised',
  applicationAndSceneContainer: 'isolated fixture through registered actual product/scene SDOs',
  standaloneCefEndToEnd: false, viewportRenderingExercised: false, cases: [], checks: [] };
const memberSignature = members => members.map(member => ({ entityId: member.entityId,
  stableKey: member.stableKey, previewGeometryResourceId: member.previewGeometryResourceId,
  transform: member.transform })).sort((a, b) => a.entityId.localeCompare(b.entityId));
const sorted = values => [...values].sort();
try {
  const modules = (await invoke('inspection', 'GetRuntimeModules')).modules;
  report.modules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
    assert.equal(resolve(path).toLowerCase(), resolve(runtime, name).toLowerCase(), 'Native modules must use the selected runtime');
    return [name, { path, sha256: sha(readFileSync(path)) }];
  }));
  await assert.rejects(invoke('product', 'GeneratePreview', {}), /requires a scene/);
  await assert.rejects(invoke('product', 'DisassembleSelected', {}), /requires a scene/);
  const read = await invoke('product', 'ReadBatchExcelImport', { sourcePath: workbook });
  assert.equal(read.rows.length, 3, 'The unchanged native workbook envelope contains exactly three data rows');
  const descriptor = (await invoke('product', 'GetTemplateDescriptor', { templateId: read.templateId })).template;
  await invoke('inspection', 'ResetScene');
  const initial = await invoke('scene', 'List');
  assert.equal(initial.tubeDesigner.instances.length, 0);
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome', headless: true });
  page = await browser.newPage({ viewport: { width: 1500, height: 980 } });
  const browserErrors = [];
  page.on('pageerror', error => browserErrors.push(error.message));
  await page.exposeFunction('nativeBatchDisassemblyInvoke', invoke);
  await page.route('http://batch-disassembly-native.test/**', serveBrowserAsset);
  await page.goto('http://batch-disassembly-native.test/');
  const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  await page.setContent(`<style>${tubeDesignerCss}</style><main id="mount" class="tube-designer-workspace"></main>`);
  const importStart = calls.length;
  await page.evaluate(async ({ initial, workbook }) => {
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const f = { actions, logs: [], notices: [], errors: [], pickerCalls: 0,
      view: { tubeDesignerLoaded: true, pending: false, activeAreaId: 'view', scene: initial } };
    f.context = { mount: document.querySelector('#mount'), project: { projectId: 'native-batch-disassembly' },
      appProxy: { bridge: { async openFileDialog() { f.pickerCalls++; return workbook; } } },
      productProxy: { invoke(method, payload) { return globalThis.nativeBatchDisassemblyInvoke('product', method, payload); } },
      sceneProxy: { invoke(method, payload) { return globalThis.nativeBatchDisassemblyInvoke('scene', method, payload); } },
      actions: {} };
    f.render = () => {
      const designer = f.view.scene?.tubeDesigner ?? {};
      f.context.mount.innerHTML = views.renderDesignerLeftPane(f.context, f.view)
        + views.renderDesignerDialogs(designer, f.view) + views.renderDesignerRuntimeStatus(f.view);
      if (f.importing && f.context.mount.querySelector('[aria-modal="true"]')) f.importConfirmationRendered = true;
    };
    f.ops = { renderProject: f.render, showNotice(context, view, message) { f.notices.push(message); },
      appendProjectLog(context, level, message) { f.logs.push({ level, message }); } };
    f.context.actions.refreshActiveSceneState = async ({ sceneProxy, projectId } = {}) => {
      if (sceneProxy && sceneProxy !== f.context.sceneProxy) throw new Error('Wrong scene acknowledgement');
      if (projectId && projectId !== f.context.project.projectId) throw new Error('Wrong project acknowledgement');
      return f.context.sceneProxy.invoke('TubeDesigner.List', {});
    };
    function dispatch(event) {
      const target = event.target.closest('[data-cam-action]');
      if (!target) return;
      // Checkbox changes are handled once, after the native checked state changes.
      if (target.matches('input[type="checkbox"]') && event.type === 'click') return;
      f.lastAction = actions.handleDesignerAreaAction(f.context, f.view, target.dataset.camAction, target, f.ops)
        .catch(error => { f.errors.push(error.message); });
    }
    f.context.mount.addEventListener('click', dispatch);
    f.context.mount.addEventListener('change', dispatch);
    globalThis.nativeBatchDisassemblyFixture = f;
    f.importing = true;
    await actions.handleDesignerAreaAction(f.context, f.view, 'tube-designer-batch-add', null, f.ops);
    f.importing = false;
  }, { initial, workbook });
  const importedCalls = calls.slice(importStart);
  const frontendReaders = importedCalls.filter(call => call.request.method === 'ReadBatchExcelImport');
  assert.equal(frontendReaders.length, 1);
  assert.equal(frontendReaders[0].request.scope, 'product');
  assert.deepEqual(frontendReaders[0].response.result.rows, read.rows);
  const created = importedCalls.filter(call => call.request.method === 'GeneratePreview');
  assert.equal(created.length, 3);
  assert(created.every(call => call.request.scope === 'scene' && call.response.ok));
  const before = (await invoke('scene', 'List')).tubeDesigner;
  assert.equal(before.instances.length, 3);
  assert.equal(before.manufacturingGroups.length, 0);
  const instances = read.rows.map(row => {
    const instance = before.instances.find(item => item.name === row.instanceName);
    assert(instance);
    assert.equal(instance.quantity, row.instanceQuantity);
    for (const [key, value] of Object.entries(row.parameters)) assert.deepEqual(instance.parameters[key], value);
    assert.equal(instance.hasDisassembly, false);
    return instance;
  });
  const memberBaselines = new Map();
  for (const instance of instances) {
    await invoke('scene', 'ActivateProduct', { productEntityId: instance.entityId });
    const active = (await invoke('scene', 'List')).tubeDesigner;
    assert.equal(active.generationRun.entityId, instance.activeGenerationRunId);
    assert(active.members.length > 0);
    memberBaselines.set(instance.entityId, memberSignature(active.members));
  }
  const activeBefore = (await invoke('scene', 'List')).tubeDesigner;
  await page.evaluate(snapshot => { const f = nativeBatchDisassemblyFixture; f.view.scene.tubeDesigner = snapshot; f.render(); }, activeBefore);
  const noRequestStart = calls.length;
  await page.evaluate(async () => {
    const f = nativeBatchDisassemblyFixture;
    await f.actions.handleDesignerRibbonCommand(f.context, f.view, 'designer.disassemble', f.ops);
  });
  const dialog = page.locator('.tube-designer-batch-disassembly-dialog');
  await dialog.waitFor();
  assert.equal(await dialog.locator('[data-tube-designer-batch-disassembly-instance-row]').count(), 3);
  assert.equal(calls.filter(call => call.request.method === 'DisassembleSelected').length, 1,
    'Only the deliberately rejected product-scope probe precedes confirmation');
  assert(!calls.slice(noRequestStart).some(call => ['DisassembleSelected', 'GeneratePreview'].includes(call.request.method)),
    'Opening the selector must not manufacture or regenerate products');
  // Cancel once, then select only the first and third persisted products.
  await dialog.locator('[data-cam-action="tube-designer-batch-disassembly-close"]').last().click();
  await page.evaluate(() => nativeBatchDisassemblyFixture.lastAction);
  assert.equal(await dialog.count(), 0);
  assert(!calls.slice(noRequestStart).some(call => call.request.method === 'DisassembleSelected'));
  await page.evaluate(async () => {
    const f = nativeBatchDisassemblyFixture;
    await f.actions.handleDesignerRibbonCommand(f.context, f.view, 'designer.disassemble', f.ops);
  });
  await dialog.waitFor();
  const ids = instances.map(instance => instance.entityId);
  for (const id of ids) await dialog.locator(`[data-cam-action="tube-designer-batch-disassembly-toggle"][data-tube-designer-instance-id="${id}"]`).uncheck();
  for (const id of [ids[0], ids[2]]) await dialog.locator(`[data-cam-action="tube-designer-batch-disassembly-toggle"][data-tube-designer-instance-id="${id}"]`).check();
  assert.deepEqual(sorted(await page.evaluate(() => nativeBatchDisassemblyFixture.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds)), sorted([ids[0], ids[2]]));
  assert(!calls.slice(noRequestStart).some(call => ['DisassembleSelected', 'GeneratePreview'].includes(call.request.method)),
    'Changing product checkboxes must not start native manufacturing');
  await page.screenshot({ path: join(output, 'native-batch-disassembly-selected.png'), fullPage: true });
  const confirmationStart = calls.length;
  await dialog.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]').click();
  await page.evaluate(() => nativeBatchDisassemblyFixture.lastAction);
  const frontend = await page.evaluate(() => {
    const f = nativeBatchDisassemblyFixture;
    return { errors: f.errors, logs: f.logs, notices: f.notices, error: f.view.error,
      pickerCalls: f.pickerCalls, importConfirmationRendered: Boolean(f.importConfirmationRendered),
      pending: f.view.pending, dialogOpen: Boolean(f.view.tubeDesignerBatchDisassemblyDialog),
      selectedInstanceIds: f.view.tubeDesignerSelectedInstanceIds,
      lastOperation: f.view.tubeDesignerLastOperation, instances: f.view.scene.tubeDesigner.instances };
  });
  assert.deepEqual(frontend.errors, []);
  assert(!frontend.error, frontend.error);
  assert.equal(frontend.pending, false);
  assert.equal(frontend.pickerCalls, 1);
  assert.equal(frontend.importConfirmationRendered, false);
  assert.equal(frontend.dialogOpen, false);
  const confirmationCalls = calls.slice(confirmationStart);
  const disassemblyCalls = confirmationCalls.filter(call => call.request.method === 'DisassembleSelected');
  assert.equal(disassemblyCalls.length, 1);
  assert.equal(disassemblyCalls[0].request.scope, 'scene');
  assert.equal(disassemblyCalls[0].response.ok, true);
  assert.deepEqual(sorted(disassemblyCalls[0].request.payload.productEntityIds), sorted([ids[0], ids[2]]));
  assert(!confirmationCalls.some(call => call.request.method === 'GeneratePreview'), 'Batch manufacturing preserves display generation');
  const after = (await invoke('scene', 'List')).tubeDesigner;
  assert.equal(after.product.entityId, activeBefore.product.entityId);
  assert.equal(after.generationRun.entityId, activeBefore.generationRun.entityId);
  assert.deepEqual(memberSignature(after.members), memberSignature(activeBefore.members));
  assert.deepEqual(sorted(after.manufacturingGroups.map(group => group.productEntityId)), sorted([ids[0], ids[2]]));
  for (const [index, instance] of instances.entries()) {
    const saved = after.instances.find(item => item.entityId === instance.entityId);
    const selected = index !== 1;
    assert.equal(saved.hasDisassembly, selected);
    assert.equal(saved.quantity, instance.quantity);
    assert.equal(saved.activeGenerationRunId, instance.activeGenerationRunId);
    assert.deepEqual(saved.parameters, instance.parameters);
    const group = after.manufacturingGroups.find(item => item.productEntityId === instance.entityId);
    if (selected) {
      assert(group?.parts.length > 0, 'Selected product has actual persisted manufactured parts');
      assert.equal(group.quantity, instance.quantity);
      assert.equal(group.generationRunId, instance.activeGenerationRunId);
      assert.equal(group.parameters.assemblyClearance, instance.parameters.assemblyClearance);
      assert.equal(saved.partsOutdated, false);
      assert.equal(saved.partCount, group.parts.length);
      assert.equal(saved.expectedPartCount, group.parts.length);
      for (const part of group.parts) {
        assert.equal(part.status, 'Ready');
        assert(part.manufacturingGeometryResourceId && part.manufacturingGeometryResourceVersion > 0);
        assert.equal(part.instanceQuantity, instance.quantity);
        assert.equal(part.quantity, part.unitQuantity * instance.quantity);
      }
    } else {
      assert.equal(group, undefined);
      assert.equal(saved.partCount, 0);
    }
    await invoke('scene', 'ActivateProduct', { productEntityId: instance.entityId });
    const active = (await invoke('scene', 'List')).tubeDesigner;
    assert.deepEqual(memberSignature(active.members), memberBaselines.get(instance.entityId));
    assert.equal(active.generationRun.entityId, instance.activeGenerationRunId);
    report.cases.push({ name: instance.name, entityId: instance.entityId, selected,
      quantity: saved.quantity, assemblyClearance: saved.parameters.assemblyClearance,
      hasDisassembly: saved.hasDisassembly, partCount: saved.partCount,
      displayMemberCount: active.members.length, generationRunId: instance.activeGenerationRunId,
      displayMembersAndGenerationUnchanged: true, allPartResourcesReady: selected,
      productionPartQuantity: group?.parts.reduce((sum, part) => sum + part.quantity, 0) ?? 0 });
  }
  await invoke('scene', 'ActivateProduct', { productEntityId: activeBefore.product.entityId });
  assert.deepEqual(browserErrors, []);
  assert(!stderr.length, stderr.join(''));
  report.frontend = { ...frontend, instances: undefined };
  report.checks = ['Opening, changing selection and cancellation never disassemble or regenerate',
    'One actual scene-scope request disassembles only selected first and third products',
    'Both selected groups contain ready native manufacturing resources and persisted parts',
    'The unselected second product has no manufacturing group or persisted parts',
    'Production quantities, parameters, display member identities and active generation runs remain unchanged'];
  report.selectedProductCount = 2;
  report.totalManufacturingParts = report.cases.reduce((sum, item) => sum + item.partCount, 0);
  report.status = 'passed';
  await page.screenshot({ path: join(output, 'native-batch-disassembly-completed.png'), fullPage: true });
  console.log(`PASS real native batch disassembly: 2 of 3 products, ${report.totalManufacturingParts} persisted ready parts`);
} catch (error) {
  report.status = 'failed'; report.failure = { message: error.message, stack: error.stack };
  throw error;
} finally {
  await browser?.close();
  native.stdin.end();
  const timer = setTimeout(() => native.kill(), 30000);
  await exited; clearTimeout(timer);
  report.nativeExitCode = nativeExitCode;
  writeFileSync(join(output, 'native-frontend-report.json'), JSON.stringify(report, null, 2));
  writeFileSync(join(output, 'native-calls.json'), JSON.stringify({ calls }, null, 2));
  writeFileSync(join(output, 'native-stderr.log'), stderr.join(''));
  writeFileSync(join(output, 'native-stdout.log'), stdout.join('\n'));
}
