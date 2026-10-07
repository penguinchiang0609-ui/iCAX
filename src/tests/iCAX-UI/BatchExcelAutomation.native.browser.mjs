// Production timed controller, real registered product/scene APIs and real exports.
// Only directory pickers and the application/scene container are test adapters.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { basename, delimiter, dirname, join, resolve, sep } from 'node:path';
import { createInterface } from 'node:readline';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

assert(process.env.ICAX_NATIVE_RUNTIME_ROOT && process.env.ICAX_NATIVE_SCOPE_BRIDGE,
  'Set the current automation runtime and BatchExcelAutomationScopeBridge');
assert(process.env.ICAX_NATIVE_TEST_PYTHON, 'Set the bundled Python path for exported XLSX inspection');
const runtime = resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT);
const bridge = resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE);
const inputWorkbook = resolve(process.env.ICAX_AUTOMATION_WORKBOOK || 'output/防盗窗款式批量导入测试.xlsx');
const output = browserReportDirectory('excel-temp-directory-20261006/native-browser');
const scenario = mkdtempSync(join(output, 'case-'));
const inputDirectory = join(scenario, 'in');
const tempDirectory = join(scenario, 'temp');
const outputDirectory = join(scenario, 'out');
const userData = join(scenario, '用户数据');
for (const directory of [inputDirectory, tempDirectory, outputDirectory, userData]) mkdirSync(directory);
let sourceWorkbook = inputWorkbook;
if (!process.env.ICAX_AUTOMATION_WORKBOOK) {
  sourceWorkbook = join(scenario, '自动处理两款式.xlsx');
  // Keep every native ZIP entry and XML namespace exactly as exported. Only
  // remove data rows after the first two, without touching hidden metadata.
  const script = String.raw`import re,sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as source,zipfile.ZipFile(sys.argv[2],'w') as target:
 for entry in source.infolist():
  data=source.read(entry.filename)
  if entry.filename=='xl/worksheets/sheet1.xml':
   text=data.decode('utf-8')
   text=re.sub(r'<(?:\w+:)?row\b[^>]*\br="(\d+)"[^>]*>.*?</(?:\w+:)?row>',lambda m:m.group(0) if int(m.group(1))<=4 else '',text,flags=re.S)
   data=text.encode('utf-8')
  target.writestr(entry,data)`;
  const prepared = spawnSync(process.env.ICAX_NATIVE_TEST_PYTHON, ['-c', script, inputWorkbook, sourceWorkbook],
    { encoding: 'utf8', windowsHide: true });
  assert.equal(prepared.status, 0, prepared.stderr);
}
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const sourceHash = sha(readFileSync(sourceWorkbook));
const samePath = (a, b) => resolve(a).toLowerCase() === resolve(b).toLowerCase();
const inside = (file, directory) => resolve(file).toLowerCase().startsWith((resolve(directory) + sep).toLowerCase());
const calls = [], stderr = [], stdout = [];
let activeHost, browser, page, epoch = 0;
const report = { status: 'running', assetRoot: browserAssetRoot, nativeRuntime: runtime, bridge,
  inputWorkbook, sourceWorkbook, sourceHash, inputDirectory, tempDirectory, outputDirectory, userData,
  nativeBusinessResponsesMocked: false, directoryPicker: 'controlled actual directory paths; Windows picker UI not exercised',
  applicationAndSceneContainer: 'isolated registered native SDO fixture',
  viewportContainer: 'controlled view acknowledgement; checks real native member IDs and generation correlation, without CEF rendering',
  standaloneCefEndToEnd: false, jobs: [], checks: [] };

function compactDesigner(designer) {
  if (!designer) return designer;
  const part = value => ({ entityId: value.entityId, stableKey: value.stableKey, status: value.status,
    quantity: value.quantity, unitQuantity: value.unitQuantity, instanceQuantity: value.instanceQuantity,
    length: value.length, name: value.name, manufacturingGeometryResourceId: value.manufacturingGeometryResourceId,
    manufacturingGeometryResourceVersion: value.manufacturingGeometryResourceVersion });
  return { ...designer, members: designer.members?.map(value => ({ entityId: value.entityId,
    stableKey: value.stableKey, previewGeometryResourceId: value.previewGeometryResourceId })),
  parts: designer.parts?.map(part), manufacturingGroups: designer.manufacturingGroups?.map(group => ({ ...group,
    parts: group.parts?.map(part) })) };
}
function startHost() {
  const currentEpoch = ++epoch;
  const env = { ...process.env, ICAX_AUTOMATION_USER_DATA: userData };
  const oldPath = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
  for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
  env.Path = runtime + delimiter + oldPath;
  const child = spawn(bridge, [], { cwd: runtime, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  let sequence = 0;
  const waiting = new Map();
  const ended = new Promise(resolveExit => child.once('exit', code => {
    for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(new Error(`Native host ${currentEpoch} exited ${code}`)); }
    waiting.clear(); resolveExit(code);
  }));
  child.on('error', error => { for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(error); } waiting.clear(); });
  child.stderr.on('data', bytes => stderr.push(`[${currentEpoch}] ${bytes}`));
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { stdout.push(`[${currentEpoch}] ${line}`); return; }
    const job = waiting.get(response.id);
    if (!job) return;
    waiting.delete(response.id); clearTimeout(job.timer);
    const audit = { ...response, result: response.result ? { ...response.result,
      ...(response.result.tubeDesigner ? { tubeDesigner: compactDesigner(response.result.tubeDesigner) } : {}) } : undefined };
    const record = { epoch: currentEpoch, request: job.request, response: audit, milliseconds: Date.now() - job.started };
    if (job.request.method === 'ClaimBatchExcelAutomation' && response.result?.claimed) {
      const claim = response.result;
      record.movedWorkbook = { path: claim.sourcePath, sha256: sha(readFileSync(claim.sourcePath)),
        insideConfiguredTemp: inside(claim.sourcePath, tempDirectory),
        originalInputAbsent: !existsSync(claim.originalSourcePath) };
      assert(record.movedWorkbook.insideConfiguredTemp && record.movedWorkbook.originalInputAbsent,
        'Claim moves the original workbook out of in into configured temp before processing');
    }
    calls.push(record);
    response.ok ? job.resolve(response.result) : job.reject(new Error(response.error));
  });
  return { epoch: currentEpoch,
    invoke(scope, method, payload = {}) {
      const request = { id: ++sequence, scope, method: method.replace(/^TubeDesigner\./, ''), payload };
      return new Promise((resolveRequest, reject) => {
        const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error(`Native ${method} timed out`)); }, 240000);
        waiting.set(request.id, { request, resolve: resolveRequest, reject, timer, started: Date.now() });
        child.stdin.write(JSON.stringify(request) + '\n');
      });
    },
    async close() { child.stdin.end(); const timer = setTimeout(() => child.kill(), 30000);
      const code = await ended; clearTimeout(timer); assert.equal(code, 0); },
  };
}
async function attachController(initial) {
  page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  page.on('pageerror', error => { report.browserErrors ??= []; report.browserErrors.push(error.message); });
  await page.exposeFunction('nativeAutomationInvoke', (scope, method, payload) => activeHost.invoke(scope, method, payload));
  await page.route('http://automation-native.test/**', serveBrowserAsset);
  await page.goto('http://automation-native.test/');
  const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  const { nestingSettingsCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/nestingSettings.css.mjs');
  const { batchExcelAutomationCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/batchExcelAutomation.css.mjs');
  await page.setContent(`<style>${tubeDesignerCss}${nestingSettingsCss}${batchExcelAutomationCss}</style><main id="mount"></main>`);
  await page.evaluate(async ({ initial, inputDirectory, tempDirectory, outputDirectory }) => {
    const automation = await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs');
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const f = { automation, logs: [], calls: [], directoryPicks: [], successes: 0,
      activeSceneRefreshCalls: [], areaViewSyncCalls: [], defaultFitCalls: [],
      view: { tubeDesignerLoaded: true, pending: false, activeAreaId: 'view', scene: initial } };
    f.context = { mount: document.querySelector('#mount'), project: { projectId: 'native-automation' },
      appProxy: { bridge: { async openDirectoryDialog(payload) {
        f.directoryPicks.push(payload);
        return payload.title.includes('侦听') ? inputDirectory
          : payload.title.includes('临时') ? tempDirectory : outputDirectory;
      } } }, productProxy: { async invoke(method, payload) {
        f.calls.push({ scope: 'product', method, payload });
        return globalThis.nativeAutomationInvoke('product', method, payload);
      } }, sceneProxy: { async invoke(method, payload) {
        f.calls.push({ scope: 'scene', method, payload });
        return globalThis.nativeAutomationInvoke('scene', method, payload);
      } }, actions: {} };
    f.render = () => {
      f.context.mount.innerHTML = views.renderDesignerLeftPane(f.context, f.view)
        + automation.renderBatchExcelAutomationDialog(f.view) + views.renderDesignerRuntimeStatus(f.view);
    };
    f.ops = { renderProject: f.render, appendProjectLog(context, level, message) {
      f.logs.push({ level, message }); if (level === 'info' && message.startsWith('Excel 自动处理完成')) f.successes++;
    }, refreshDesignerState: () => actions.refreshDesignerState(f.context, f.view, f.ops),
    async refreshActiveAreaView(context, view, expectation) {
      const actual = (await f.context.sceneProxy.invoke('TubeDesigner.List', {})).tubeDesigner;
      const entityIds = actual.members.map(member => member.entityId);
      if (JSON.stringify([...entityIds].sort()) !== JSON.stringify([...expectation.expectedEntityIds].sort()))
        throw new Error('Viewport container requested members differ from real native scene');
      if (expectation.correlationId !== actual.product.activeGenerationRunId)
        throw new Error('Viewport correlation does not match the actual native generation run');
      const revision = `native-container-${f.areaViewSyncCalls.length + 1}`;
      f.areaViewSyncCalls.push({ expectation, revision, actualProductId: actual.product.entityId, entityIds });
      return { revision, viewportReceipt: { applied: true, revision, entityIds } };
    },
    fitDesignerDefaultView(view, revision, product) {
      f.defaultFitCalls.push({ revision, productEntityId: product.entityId });
    } };
    f.context.actions.refreshActiveSceneState = async request => {
      f.activeSceneRefreshCalls.push({ sameSceneProxy: request?.sceneProxy === f.context.sceneProxy,
        projectId: request?.projectId, ownsMutation: f.view.tubeDesignerOwnMutation === true });
      return f.context.sceneProxy.invoke('TubeDesigner.List', {});
    };
    f.context.mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target) f.lastAction = automation.handleBatchExcelAutomationAction(f.context, f.view, target.dataset.camAction, target, f.ops);
    });
    f.context.mount.addEventListener('change', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target) f.lastAction = automation.handleBatchExcelAutomationAction(f.context, f.view, target.dataset.camAction, target, f.ops);
    });
    globalThis.nativeAutomationFixture = f;
    f.render(); automation.connectBatchExcelAutomation(f.context, f.view, f.ops);
  }, { initial, inputDirectory, tempDirectory, outputDirectory });
}
async function openSettings() {
  await page.evaluate(() => {
    const f = nativeAutomationFixture;
    return f.automation.handleBatchExcelAutomationRibbonCommand(f.context, f.view, 'designer.excel.automation-settings', f.ops);
  });
  await page.getByRole('dialog').waitFor();
}
async function waitForSuccess(count) {
  await page.waitForFunction(count => nativeAutomationFixture.successes >= count && !nativeAutomationFixture.view.pending,
    count, { timeout: 240000 });
  const logs = await page.evaluate(() => nativeAutomationFixture.logs);
  assert(!logs.some(item => item.level === 'error'), JSON.stringify(logs));
}
function inspectExportedWorkbook(path) {
  const script = `import json,sys,zipfile,xml.etree.ElementTree as E
with zipfile.ZipFile(sys.argv[1]) as z:
 root=E.fromstring(z.read('xl/worksheets/sheet1.xml'))
 def value(c):
  v=c.find('{*}v')
  return v.text if v is not None else ''.join(t.text or '' for t in c.findall('.//{*}t'))
 rows=[]
 for row in root.findall('.//{*}sheetData/{*}row'):
  cells={''.join(c.get('r','').rstrip('0123456789')):value(c) for c in row.findall('{*}c')}
  try: index=int(cells.get('D',''))
  except ValueError: continue
  if index>0: rows.append({'row':int(row.get('r')),'index':index,'quantity':int(cells['J']),'length':float(cells['I']),'fileName':cells['K']})
 print(json.dumps({'partRows':rows},ensure_ascii=True))`;
  const result = spawnSync(process.env.ICAX_NATIVE_TEST_PYTHON, ['-c', script, path], { encoding: 'utf8', windowsHide: true });
  assert.equal(result.status, 0, result.stderr); return JSON.parse(result.stdout);
}
async function validateJob(sourcePath, expectedRows, expectedDefaults, currentEpoch) {
  const completed = calls.find(call => call.epoch === currentEpoch && call.request.method === 'CompleteBatchExcelAutomation'
    && samePath(call.request.payload.sourcePath, sourcePath) && call.request.payload.status === 'succeeded');
  assert(completed?.response.ok, 'Real completion API records successful processing after exports');
  const claim = calls.find(call => call.epoch === currentEpoch && call.request.method === 'ClaimBatchExcelAutomation'
    && samePath(call.request.payload.sourcePath, sourcePath) && call.response.result?.claimed);
  assert(claim.movedWorkbook.insideConfiguredTemp && claim.movedWorkbook.originalInputAbsent);
  assert.equal(claim.movedWorkbook.sha256, sourceHash); assert.equal(claim.response.result.fileToken, sourceHash);
  assert(!existsSync(sourcePath), 'Processed workbook no longer remains in in');
  assert(existsSync(claim.response.result.sourcePath), 'Successful source workbook remains available in temp');
  assert.equal(sha(readFileSync(claim.response.result.sourcePath)), sourceHash);
  assert.deepEqual(readdirSync(inputDirectory), [], 'in stays clear of processing and completed workbooks');
  const start = claim.request.id, end = completed.request.id;
  const jobCalls = calls.filter(call => call.epoch === currentEpoch && call.request.id >= start && call.request.id <= end);
  const reader = jobCalls.find(call => call.request.method === 'ReadBatchExcelImport');
  assert.equal(reader.request.scope, 'product');
  assert(samePath(reader.request.payload.sourcePath, claim.response.result.sourcePath));
  assert.deepEqual(reader.response.result.rows, expectedRows);
  const generations = jobCalls.filter(call => call.request.method === 'GeneratePreview');
  assert.equal(generations.length, expectedRows.length); assert(generations.every(call => call.request.scope === 'scene' && call.response.ok));
  const ids = generations.map(call => call.response.result.tubeDesigner.product.entityId);
  const disassembly = jobCalls.find(call => call.request.method === 'DisassembleSelected');
  const exported = jobCalls.find(call => call.request.method === 'ExportSelected');
  assert.equal(disassembly.request.scope, 'scene'); assert.equal(exported.request.scope, 'scene');
  assert.deepEqual(disassembly.request.payload.productEntityIds, ids);
  assert(disassembly.response.ok && exported.response.ok);
  assert(reader.request.id < generations[0].request.id && generations.at(-1).request.id < disassembly.request.id
    && disassembly.request.id < exported.request.id && exported.request.id < completed.request.id);
  const snapshot = (await activeHost.invoke('scene', 'List')).tubeDesigner;
  const selected = snapshot.instances.filter(instance => ids.includes(instance.entityId));
  assert.equal(selected.length, expectedRows.length);
  const groups = snapshot.manufacturingGroups.filter(group => ids.includes(group.productEntityId));
  assert.equal(groups.length, expectedRows.length);
  for (const [index, row] of expectedRows.entries()) {
    const instance = selected.find(item => item.entityId === ids[index]);
    assert.equal(instance.name, row.instanceName); assert.equal(instance.quantity, row.instanceQuantity);
    assert.equal(instance.hasDisassembly, true); assert.equal(instance.modelOutdated, false); assert.equal(instance.partsOutdated, false);
    for (const [key, value] of Object.entries(row.parameters)) assert.deepEqual(instance.parameters[key], value);
    assert.equal(instance.parameters.assemblyClearance, row.parameters.assemblyClearance ?? expectedDefaults.assemblyClearance);
    const group = groups.find(item => item.productEntityId === instance.entityId);
    assert.equal(group.parts.length, instance.partCount);
    assert(group.parts.every(part => part.status === 'Ready' && part.manufacturingGeometryResourceId
      && part.quantity === part.unitQuantity * instance.quantity));
  }
  const parts = groups.flatMap(group => group.parts);
  const singleInstancePartCount = parts.reduce((count, part) => count + Number(part.unitQuantity), 0);
  assert(singleInstancePartCount > parts.length, 'Fixture must exercise merged part types with multiple pieces per instance');
  assert.deepEqual(exported.request.payload.partEntityIds.sort(), parts.map(part => part.entityId).sort());
  const result = exported.response.result;
  assert.equal(result.exportedCount, singleInstancePartCount);
  assert.equal(result.exportedFiles.length, singleInstancePartCount);
  assert.equal(new Set(result.exportedFiles.map(file => resolve(file).toLowerCase())).size, singleInstancePartCount);
  for (const group of groups) {
    const outputGroup = result.exportedGroups.find(value => value.productEntityId === group.productEntityId);
    const instance = selected.find(value => value.entityId === group.productEntityId);
    assert.equal(outputGroup.quantity, instance.quantity);
    assert(basename(outputGroup.directory).endsWith(`×${instance.quantity}`),
      'Product production quantity is carried in the single-instance export folder name');
    assert.equal(outputGroup.exportedCount, group.parts.reduce((count, part) => count + Number(part.unitQuantity), 0));
  }
  assert(inside(result.partListFile, outputDirectory));
  for (const path of [...result.exportedFiles, result.partListFile]) assert(existsSync(path) && statSync(path).size > 100, path);
  for (const path of result.exportedFiles) {
    assert(inside(path, claim.response.result.targetDirectory));
    const content = readFileSync(path, 'utf8'); assert(content.startsWith('ISO-10303-21;') && content.includes('END-ISO-10303-21;'));
  }
  const book = inspectExportedWorkbook(result.partListFile);
  assert.equal(book.partRows.length, parts.length, 'Excel continues to summarize each merged part type');
  assert.deepEqual(book.partRows.map(row => row.quantity).sort((a, b) => a - b),
    parts.map(part => part.quantity).sort((a, b) => a - b));
  assert.equal(book.partRows.reduce((count, row) => count + row.quantity, 0),
    parts.reduce((count, part) => count + part.quantity, 0), 'Excel preserves the total production quantity');
  assert.deepEqual(book.partRows.flatMap(row => row.fileName.split('\n')).sort(),
    result.exportedFiles.map(file => basename(file)).sort(), 'Every expanded STEP file appears in its type summary');
  const viewport = await page.evaluate(() => {
    const f = nativeAutomationFixture;
    return { activeSceneRefreshCalls: f.activeSceneRefreshCalls, areaViewSyncCalls: f.areaViewSyncCalls,
      defaultFitCalls: f.defaultFitCalls, ownsMutationAfterCompletion: f.view.tubeDesignerOwnMutation === true };
  });
  assert.equal(viewport.activeSceneRefreshCalls.length, 1, 'Automatic mutation publishes the native scene once');
  assert(viewport.activeSceneRefreshCalls[0].sameSceneProxy && viewport.activeSceneRefreshCalls[0].ownsMutation);
  assert.equal(viewport.areaViewSyncCalls.length, 1, 'Automatic creation requests an acknowledged area view');
  assert.equal(viewport.defaultFitCalls.length, 1, 'A first automatic import frames the actual active product');
  assert.equal(viewport.defaultFitCalls[0].productEntityId, snapshot.product.entityId);
  assert.equal(viewport.ownsMutationAfterCompletion, false);
  report.jobs.push({ sourcePath, epoch: currentEpoch, instanceIds: ids, quantities: selected.map(instance => instance.quantity),
    assemblyClearances: selected.map(instance => instance.parameters.assemblyClearance), partTypeCount: parts.length,
    singleInstancePartCount, stepFileCount: result.exportedFiles.length,
    productionPartQuantity: parts.reduce((sum, part) => sum + part.quantity, 0), targetDirectory: claim.response.result.targetDirectory,
    exportedFiles: result.exportedFiles, partListFile: result.partListFile, actualWorkbookRows: book.partRows.length,
    tempWorkbook: claim.response.result.sourcePath, movedBeforeImport: true, inputDirectoryEmpty: true,
    sourceRetainedInTempHashVerified: true, productReadSceneCreateDisassembleExportProductComplete: true,
    nativeSceneViewCallbacks: viewport });
  writeFileSync(join(output, 'native-frontend-progress.json'), JSON.stringify(report, null, 2));
  console.log(`PASS timed workflow: ${sourcePath}, ${ids.length} instance(s), ${singleInstancePartCount} actual STEP files and ${parts.length} XLSX type rows`);
}
try {
  activeHost = startHost();
  const modules = (await activeHost.invoke('inspection', 'GetRuntimeModules')).modules;
  report.modules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
    assert(samePath(path, join(runtime, name))); return [name, { path, sha256: sha(readFileSync(path)) }];
  }));
  assert.equal((await activeHost.invoke('product', 'GetBatchExcelAutomationSettings')).settings.enabled, false);
  const workbook = await activeHost.invoke('product', 'ReadBatchExcelImport', { sourcePath: sourceWorkbook });
  assert(workbook.rows.length > 0 && workbook.rows.length <= 3, 'Choose a real small workbook containing one to three rows');
  const descriptor = (await activeHost.invoke('product', 'GetTemplateDescriptor', { templateId: workbook.templateId })).template;
  const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
  for (const method of ['GeneratePreview', 'DisassembleSelected', 'ExportSelected'])
    await assert.rejects(activeHost.invoke('product', method), /requires a scene/);
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
  await attachController(await activeHost.invoke('scene', 'List'));
  await openSettings();
  await page.getByLabel('启用自动处理', { exact: true }).check();
  await page.locator('[data-directory-field="inputDirectory"]').click();
  await page.locator('[data-directory-field="tempDirectory"]').click();
  await page.locator('[data-directory-field="outputDirectory"]').click();
  assert.equal(await page.getByLabel('侦听目录', { exact: true }).inputValue(), inputDirectory);
  assert.equal(await page.getByLabel('临时目录', { exact: true }).inputValue(), tempDirectory);
  assert.equal(await page.getByLabel('输出目录', { exact: true }).inputValue(), outputDirectory);
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.waitForFunction(() => !nativeAutomationFixture.view.tubeDesignerExcelAutomationDialog);
  report.directoryPickerCalls = await page.evaluate(() => nativeAutomationFixture.directoryPicks.length);
  assert.equal(report.directoryPickerCalls, 3);
  report.settingsConfiguredThroughProductionControls = true;
  const firstPath = join(inputDirectory, '首次到达.xlsx');
  copyFileSync(sourceWorkbook, firstPath);
  await page.waitForFunction(() => nativeAutomationFixture.view.pending
    && nativeAutomationFixture.calls.some(call => call.method.endsWith('GeneratePreview')), null, { timeout: 45000 });
  const parallel = await page.evaluate(async () => {
    const f = nativeAutomationFixture, before = f.calls.length;
    const results = await Promise.all(Array.from({ length: 4 }, () => f.automation.runBatchExcelAutomationOnce(f.context, f.view, f.ops)));
    f.automation.connectBatchExcelAutomation(f.context, f.view, f.ops);
    f.automation.connectBatchExcelAutomation(f.context, f.view, f.ops);
    return { results, newImmediateCalls: f.calls.length - before };
  });
  assert.deepEqual(parallel.results, [false, false, false, false]); assert.equal(parallel.newImmediateCalls, 0);
  await waitForSuccess(1); await validateJob(firstPath, workbook.rows, defaults, 1);
  report.checks.push('Actual timed watch detects arriving file; simultaneous ticks and repeated attachment do not duplicate the job');
  assert.equal(calls.filter(call => call.epoch === 1 && call.request.method === 'GeneratePreview' && call.response.ok).length, workbook.rows.length);
  await page.evaluate(() => nativeAutomationFixture.automation.stopBatchExcelAutomation());
  await page.close(); page = null; await activeHost.close(); activeHost = startHost();
  const persistedSettings = (await activeHost.invoke('product', 'GetBatchExcelAutomationSettings')).settings;
  assert.equal(persistedSettings.enabled, true);
  assert(samePath(persistedSettings.tempDirectory, tempDirectory));
  copyFileSync(sourceWorkbook, firstPath);
  await attachController(await activeHost.invoke('scene', 'List'));
  await page.waitForTimeout(7800);
  assert.equal(calls.filter(call => call.epoch === 2 && call.request.method === 'GeneratePreview').length, 0);
  assert(calls.filter(call => call.epoch === 2 && call.request.method === 'ScanBatchExcelAutomation').length >= 3);
  assert.equal((await activeHost.invoke('scene', 'List')).tubeDesigner.instances.length, 0,
    'Restarted fixture has a fresh scene; persisted completion must suppress duplicate creation');
  assert(!existsSync(firstPath), 'Duplicate arrival is archived out of in without creating products again');
  assert.deepEqual(readdirSync(inputDirectory), []);
  report.checks.push('Native restart retains all three directories; duplicate arrival is archived to temp without creating products again');
  const secondPath = join(inputDirectory, '随后到达.xlsx');
  copyFileSync(sourceWorkbook, secondPath);
  await waitForSuccess(1); await validateJob(secondPath, workbook.rows, defaults, 2);
  report.checks.push('A new file name with a valid workbook is detected and receives a separate real export directory');
  assert.notEqual(report.jobs[0].targetDirectory, report.jobs[1].targetDirectory);
  await openSettings(); await page.getByLabel('启用自动处理', { exact: true }).uncheck();
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.waitForFunction(() => !nativeAutomationFixture.view.tubeDesignerExcelAutomationDialog);
  const disabledPath = join(inputDirectory, '停用后到达.xlsx');
  copyFileSync(sourceWorkbook, disabledPath);
  await page.waitForTimeout(7800);
  assert.equal((await activeHost.invoke('product', 'GetBatchExcelAutomationSettings')).settings.enabled, false);
  assert.equal(calls.filter(call => call.epoch === 2 && call.request.method === 'GeneratePreview').length, workbook.rows.length);
  assert(!calls.some(call => call.request.method === 'ClaimBatchExcelAutomation' && samePath(call.request.payload.sourcePath, disabledPath)));
  assert(existsSync(disabledPath), 'Disabled automation does not move an unclaimed file');
  const state = JSON.parse(readFileSync(join(userData, 'icax.tube-designer/batch-excel-automation/state.json'), 'utf8'));
  const successes = Object.values(state.records).filter(record => record.status === 'succeeded');
  assert.equal(successes.length, 2); assert(successes.every(record => record.outputs?.length > 0));
  assert(report.jobs.every(job => existsSync(job.tempWorkbook) && sha(readFileSync(job.tempWorkbook)) === sourceHash));
  report.checks.push('Disabling the real saved settings prevents new files from creating products or exports');
  report.savedSuccessRecords = successes.length;
  assert.deepEqual(report.browserErrors ?? [], []);
  report.logs = await page.evaluate(() => nativeAutomationFixture.logs);
  assert(!report.logs.some(item => item.level === 'error'));
  await page.screenshot({ path: join(output, 'native-automation-completed.png') });
  report.status = 'passed'; console.log('PASS real native timed Excel directory automation, restart, concurrency and disable checks');
} catch (error) {
  report.status = 'failed'; report.failure = { message: error.message, stack: error.stack }; throw error;
} finally {
  await page?.evaluate(() => nativeAutomationFixture.automation.stopBatchExcelAutomation()).catch(() => {});
  await browser?.close(); await activeHost?.close();
  writeFileSync(join(output, 'native-frontend-report.json'), JSON.stringify(report, null, 2));
  writeFileSync(join(output, 'native-calls.json'), JSON.stringify(calls, null, 2));
  writeFileSync(join(output, 'native-stderr.log'), stderr.join(''));
  writeFileSync(join(output, 'native-stdout.log'), stdout.join('\n'));
}
