// One real workbook -> production frontend product reader -> scene creation.
// The file picker and application/scene containers are the only test adapters.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { delimiter, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, readBrowserAsset,
  serveBrowserAsset } from './browserPackageRuntime.mjs';

assert(process.env.ICAX_BATCH_IMPORT_FIXTURE_MANIFEST, 'Set ICAX_BATCH_IMPORT_FIXTURE_MANIFEST');
assert(process.env.ICAX_NATIVE_SCOPE_BRIDGE, 'Set ICAX_NATIVE_SCOPE_BRIDGE to the scope-aware Release bridge');
const manifestPath = resolve(process.env.ICAX_BATCH_IMPORT_FIXTURE_MANIFEST);
const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
assert.equal(manifest.templateId, 'single-face-security-window');
assert(manifest.cases.length > 1, 'The fixture must exercise multiple styles in one workbook');
const workbook = resolve(manifest.workbook);
const runtime = resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT || browserAssetRoot);
const bridge = resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE);
const output = browserReportDirectory('security-window-batch-import-20261006/native-browser');
const env = { ...process.env };
const originalPath = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
env.Path = runtime + delimiter + originalPath;
const native = spawn(bridge, [], { cwd: runtime, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
const calls = [], waiting = new Map(), stderr = [], stdout = [];
let sequence = 0, browser, exitCode;
const exited = new Promise(resolveExit => native.once('exit', code => {
  exitCode = code;
  for (const job of waiting.values()) {
    clearTimeout(job.timer); job.reject(new Error(`Native host exited ${code}: ${stderr.join('')}`));
  }
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
  // Retain exact metadata and parameters, omit only bulky mesh/profile bytes
  // from the audit. Assertions below use the complete native response.
  const audited = structuredClone(response);
  const designer = audited.result?.tubeDesigner;
  if (designer?.members) designer.members = designer.members.map(member => ({
    entityId: member.entityId, stableKey: member.stableKey, name: member.name,
    role: member.role, length: member.length, previewGeometryResourceId: member.previewGeometryResourceId,
    transform: member.transform,
  }));
  if (designer?.manufacturingGroups) designer.manufacturingGroups = designer.manufacturingGroups.map(group => ({
    ...group, parts: (group.parts ?? []).map(part => ({
      entityId: part.entityId, stableKey: part.stableKey, status: part.status,
      quantity: part.quantity, unitQuantity: part.unitQuantity, instanceQuantity: part.instanceQuantity,
      length: part.length, name: part.name, role: part.role, partKind: part.partKind,
      manufacturingGeometryResourceId: part.manufacturingGeometryResourceId,
      manufacturingGeometryResourceVersion: part.manufacturingGeometryResourceVersion,
      properties: Object.fromEntries(Object.entries(part.properties ?? {}).filter(([key]) =>
        !['tubeDesigner.assemblyProcessStockProfile', 'tubeDesigner.profile'].includes(key))),
    })),
  }));
  calls.push({ request: job.request, response: audited, milliseconds: Date.now() - job.started });
  response.ok ? job.resolve(response.result) : job.reject(new Error(response.error));
});
function invoke(scope, method, payload = {}) {
  const request = { id: ++sequence, scope, method: method.replace(/^TubeDesigner\./, ''), payload };
  return new Promise((resolveRequest, reject) => {
    const timer = setTimeout(() => {
      waiting.delete(request.id); reject(new Error(`Native ${method} timed out`));
    }, method.endsWith('DisassembleSelected') ? 900000 : 180000);
    waiting.set(request.id, { request, resolve: resolveRequest, reject, timer, started: Date.now() });
    native.stdin.write(JSON.stringify(request) + '\n');
  });
}
const report = {
  status: 'running', manifest: manifestPath, workbook, templateId: manifest.templateId,
  assetRoot: browserAssetRoot, nativeRuntime: runtime, bridge,
  nativeBusinessResponsesMocked: false, windowsFilePicker: 'controlled workbook path; no Windows picker UI',
  applicationAndSceneContainer: 'isolated native fixture through registered real SDOs',
  standaloneCefEndToEnd: false, cases: [],
};
try {
  const modules = (await invoke('inspection', 'GetRuntimeModules')).modules;
  report.modules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
    assert.equal(resolve(path).toLowerCase(), resolve(runtime, name).toLowerCase(),
      'Native module must come from the selected isolated Release runtime');
    return [name, { path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex') }];
  }));
  const sourceRelative = 'apps/tube-designer/templates/product/single_face_security_window/template.json';
  const sourceDescriptor = JSON.parse(readBrowserAsset(sourceRelative));
  assert.equal(readFileSync(resolve(runtime, sourceRelative), 'utf8'), readBrowserAsset(sourceRelative));
  const descriptor = (await invoke('product', 'GetTemplateDescriptor', { templateId: manifest.templateId })).template;
  const parameterContract = fields => fields.map(field => [field.key, field.defaultValue,
    (field.choices ?? []).map(choice => choice.value)]);
  assert.deepEqual(parameterContract(descriptor.parameters), parameterContract(sourceDescriptor.parameters));
  assert.equal(descriptor.version, manifest.templateVersion);
  const { parameterExposed, parameterVisible, parameterEnabled } = await importBrowserAsset(
    'apps/tube-designer/webpage/parameterConditions.mjs');
  const definitions = new Map(descriptor.parameters.map(field => [field.key, field]));
  const assemblyKeys = manifest.assemblyKeys ?? [];
  assert(assemblyKeys.length > 0, 'Fixture manifest must declare the public assembly columns');
  for (const key of [...(manifest.materialKeys ?? []), ...assemblyKeys]) {
    assert(definitions.has(key) && parameterExposed(definitions.get(key)), `${key}: permanent hidden fields cannot enter Excel`);
    assert(manifest.columns.includes(key), `${key}: declared material/assembly column must be in the real workbook`);
  }
  report.currentTemplateVerified = { version: descriptor.version, parameters: descriptor.parameters.length,
    nativeDescriptorKeysDefaultsChoicesMatchSource: true, runtimeTemplateBytesMatchSource: true };

  // Even a perfectly readable workbook must not make product-scope creation succeed.
  await assert.rejects(invoke('product', 'GeneratePreview', {}), /GeneratePreview requires a scene/);
  await assert.rejects(invoke('product', 'ActivateProduct', {}), /ActivateProduct requires a scene/);
  await assert.rejects(invoke('product', 'DisassembleSelected', {}), /Disassemble requires a scene/);
  await invoke('inspection', 'ResetScene');
  const initial = await invoke('scene', 'List');
  assert.equal(initial.tubeDesigner.instances.length, 0);
  const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome', headless: true });
  const page = await browser.newPage({ viewport: { width: 1450, height: 940 } });
  const browserErrors = [];
  page.on('pageerror', error => browserErrors.push(error.message));
  await page.exposeFunction('nativeBatchInvoke', invoke);
  await page.route('http://security-batch-native.test/**', serveBrowserAsset);
  await page.goto('http://security-batch-native.test/');
  await page.setContent(`<style>${tubeDesignerCss}</style><main id="mount" class="tube-designer-workspace"></main>`);
  const callsBeforeImport = calls.length;
  await page.evaluate(async ({ initial, workbook }) => {
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const mount = document.querySelector('#mount');
    const f = { view: { pending: false, activeAreaId: 'product', scene: initial,
      tubeDesignerRightDraftDirty: true }, errors: [], notices: [], logs: [], pickerCalls: 0 };
    f.context = {
      mount,
      appProxy: { bridge: { async openFileDialog() { f.pickerCalls++; return workbook; } } },
      productProxy: { async invoke(method, payload) {
        const response = await globalThis.nativeBatchInvoke('product', method, payload);
        if (method === 'TubeDesigner.ReadBatchExcelImport') f.readResult = structuredClone(response);
        return response;
      } },
      sceneProxy: { invoke(method, payload) { return globalThis.nativeBatchInvoke('scene', method, payload); } },
    };
    f.render = () => {
      mount.innerHTML = views.renderDesignerLeftPane(f.context, f.view)
        + views.renderDesignerDialogs(f.view.scene.tubeDesigner, f.view)
        + views.renderDesignerRuntimeStatus(f.view);
      if (mount.querySelector('[aria-modal="true"]')) f.confirmationRendered = true;
    };
    f.ops = { renderProject() { f.render(); }, showNotice(context, view, message) { f.notices.push(message); },
      appendProjectLog(context, type, message) { f.logs.push({ type, message }); } };
    globalThis.securityBatchFixture = f;
    try { await actions.handleDesignerAreaAction(f.context, f.view, 'tube-designer-batch-add', null, f.ops); }
    catch (error) { f.errors.push(error.message); }
  }, { initial, workbook });
  const frontend = await page.evaluate(() => {
    const f = globalThis.securityBatchFixture;
    return { errors: f.errors, notices: f.notices, logs: f.logs, imported: f.readResult,
      pickerCalls: f.pickerCalls, importState: f.view.tubeDesignerExcelImport,
      confirmationRendered: f.confirmationRendered === true, modelDirty: f.view.tubeDesignerRightDraftDirty,
      statusHidden: document.querySelector('[data-tube-designer-runtime-status]')?.hidden,
      refreshError: f.view.error, instances: f.view.scene.tubeDesigner.instances,
      members: f.view.scene.tubeDesigner.members.length };
  });
  report.frontend = { ...frontend, imported: undefined, instances: undefined };
  assert.deepEqual(frontend.errors, []);
  assert(!frontend.refreshError, `Scene refresh failed: ${frontend.refreshError}`);
  assert.equal(frontend.pickerCalls, 1);
  assert.equal(frontend.confirmationRendered, false, 'File selection must directly create every row');
  assert.equal(frontend.importState, null);
  assert.equal(frontend.modelDirty, false);
  assert.equal(frontend.statusHidden, true);
  assert.equal(frontend.imported.templateId, manifest.templateId);
  assert.equal(frontend.imported.rows.length, manifest.cases.length);
  for (const [index, expected] of manifest.cases.entries()) {
    const row = frontend.imported.rows[index];
    assert.equal(row.sourceRow, expected.sourceRow);
    assert.equal(row.instanceName, expected.name);
    assert.equal(row.instanceQuantity, expected.quantity);
    assert.deepEqual(row.parameters, expected.parameters, `${expected.id}: selected columns from real reader`);
  }
  const importedCalls = calls.slice(callsBeforeImport);
  const readCalls = importedCalls.filter(call => call.request.method === 'ReadBatchExcelImport');
  assert.equal(readCalls.length, 1); assert.equal(readCalls[0].request.scope, 'product');
  const generateCalls = importedCalls.filter(call => call.request.method === 'GeneratePreview');
  assert.equal(generateCalls.length, manifest.cases.length);
  assert(generateCalls.every(call => call.request.scope === 'scene' && call.response.ok));
  assert(importedCalls.some(call => call.request.method === 'List' && call.request.scope === 'scene'));
  const actual = (await invoke('scene', 'List')).tubeDesigner;
  assert.equal(actual.instances.length, manifest.cases.length, 'All workbook rows share one real scene');
  assert.equal(frontend.instances.length, manifest.cases.length);
  assert.deepEqual(frontend.instances.map(item => item.entityId).sort(), actual.instances.map(item => item.entityId).sort());
  assert.equal(new Set(actual.instances.map(item => item.entityId)).size, manifest.cases.length);
  for (const [index, expected] of manifest.cases.entries()) {
    const instance = actual.instances.find(item => item.name === expected.name);
    assert(instance, `${expected.id}: repository instance exists`);
    assert.equal(instance.templateId, manifest.templateId);
    assert.equal(instance.quantity, expected.quantity);
    for (const [key, value] of Object.entries(expected.parameters))
      assert.deepEqual(instance.parameters[key], value, `${expected.id}: persisted ${key}`);
    if (expected.parameters.productCode != null) assert.equal(instance.productCode, expected.parameters.productCode);
    const request = generateCalls[index].request;
    assert.equal(request.payload.instanceName, expected.name);
    assert.equal(request.payload.instanceQuantity, expected.quantity);
    for (const [key, value] of Object.entries(expected.parameters)) assert.deepEqual(request.payload[key], value);
    await invoke('scene', 'ActivateProduct', { productEntityId: instance.entityId });
    const active = (await invoke('scene', 'List')).tubeDesigner;
    assert.equal(active.product.entityId, instance.entityId);
    assert.equal(active.product.quantity, expected.quantity);
    assert.equal(active.product.modelOutdated, false);
    assert(active.members.length > 0, `${expected.id}: actual display members exist`);
    assert.equal(active.members.length, instance.memberCount);
    assert(active.members.every(member => member.entityId && member.stableKey && member.previewGeometryResourceId
      && Array.isArray(member.transform) && member.transform.length === 16), `${expected.id}: persisted geometry resources and transforms`);
    assert.equal(active.generationRun.entityId, instance.activeGenerationRunId);
    assert.equal(active.generationRun.status, 'Succeeded');
    assert.equal(active.generationRun.templateId, manifest.templateId);
    assert(active.generationRun.hasNeutralModel);
    const escapeMembers = active.members.filter(member => member.stableKey.startsWith('access_door.'));
    assert.equal(escapeMembers.length > 0, expected.parameters.accessDoorEnabled, `${expected.id}: opening branch in real geometry`);
    const assemblyParameters = Object.fromEntries(assemblyKeys.map(key => [key, instance.parameters[key]]));
    const activeAssemblyKeys = assemblyKeys.filter(key => parameterVisible(definitions.get(key), instance.parameters)
      && parameterEnabled(definitions.get(key), instance.parameters));
    for (const key of assemblyKeys) assert.deepEqual(assemblyParameters[key], expected.parameters[key], `${expected.id}: imported assembly ${key}`);
    console.log(`Disassemble ${expected.id}: ${active.members.length} native display members, clearance ${instance.parameters.assemblyClearance}`);
    const started = Date.now();
    const disassembled = (await invoke('scene', 'DisassembleSelected', { productEntityIds: [instance.entityId] })).tubeDesigner;
    const group = disassembled.manufacturingGroups.find(item => item.productEntityId === instance.entityId);
    assert(group?.parts?.length > 0, `${expected.id}: actual native disassembly must produce a nonempty parts group`);
    assert.equal(group.quantity, expected.quantity);
    assert.equal(group.generationRunId, instance.activeGenerationRunId);
    for (const [key, value] of Object.entries(expected.parameters))
      assert.deepEqual(group.parameters[key], value, `${expected.id}: manufacturing group uses imported ${key}`);
    assert(group.parts.every(part => part.status === 'Ready' && part.manufacturingGeometryResourceId
      && part.manufacturingGeometryResourceVersion > 0), `${expected.id}: every part has ready final geometry`);
    for (const part of group.parts) {
      assert.equal(part.instanceQuantity, expected.quantity);
      assert.equal(part.quantity, part.unitQuantity * expected.quantity, `${expected.id}: production quantity for ${part.stableKey}`);
      assert(part.properties?.['manufacturing.sourceMembers']?.length > 0,
        `${expected.id}: ${part.stableKey} is tied to actual displayed source members`);
      const validMemberKeys = new Set(active.members.map(member => member.stableKey));
      for (const source of part.properties['manufacturing.sourceMembers'])
        assert(validMemberKeys.has(source.itemKey), `${expected.id}: actual source member ${source.itemKey}`);
    }
    const listed = (await invoke('scene', 'List')).tubeDesigner;
    const listedInstance = listed.instances.find(item => item.entityId === instance.entityId);
    assert.equal(listedInstance.hasDisassembly, true, `${expected.id}: native instance reports real persisted parts`);
    assert.equal(listedInstance.partsOutdated, false);
    assert.equal(listedInstance.partCount, group.parts.length);
    assert.equal(listedInstance.expectedPartCount, group.parts.length);
    const listedGroup = listed.manufacturingGroups.find(item => item.productEntityId === instance.entityId);
    assert.deepEqual(listedGroup.parts.map(part => part.entityId).sort(), group.parts.map(part => part.entityId).sort());
    // Check one actual frame and one infill stock per case through the final
    // BRep API. This checks usable processed geometry beyond the list receipt.
    const receivingFrame = group.parts.find(part => part.stableKey.startsWith(
      instance.parameters.infillPattern === 'vertical' ? 'outer_frame.bottom.' : 'outer_frame.left.'))
      ?? group.parts.find(part => /frame/.test(part.stableKey));
    const measuredParts = [receivingFrame,
      group.parts.find(part => /grid/.test(part.stableKey))].filter(Boolean);
    const measurements = [];
    for (const part of [...new Map(measuredParts.map(part => [part.entityId, part])).values()]) {
      const measurement = await invoke('scene', 'MeasurePartGeometry', {
        partEntityId: part.entityId, resourceVersion: part.manufacturingGeometryResourceVersion,
      });
      assert.equal(measurement.available, true, `${expected.id}: final BRep is readable`);
      assert.equal(measurement.source, 'final-brep');
      assert.equal(measurement.resourceId, part.manufacturingGeometryResourceId);
      assert.equal(measurement.resourceVersion, part.manufacturingGeometryResourceVersion);
      assert(Number.isFinite(measurement.length) && measurement.length > 0);
      assert(Array.isArray(measurement.features));
      assert.equal(measurement.recognitionStatus, 'direct-topology');
      measurements.push({ stableKey: part.stableKey, partEntityId: part.entityId,
        finalLength: measurement.length, openingCount: measurement.openingCount,
        features: measurement.features, section: measurement.section,
        linearReference: measurement.linearReference });
    }
    assert(measurements.length > 0);
    const clearance = Number(instance.parameters.assemblyClearance);
    const expectedOpeningDimensions = ['horizontalWidth', 'horizontalDepth', 'verticalWidth', 'verticalDepth']
      .filter(key => Number(instance.parameters[key]) > 0)
      .map(key => ({ parameter: key, base: Number(instance.parameters[key]),
        opening: Number(instance.parameters[key]) + 2 * clearance }));
    const clearanceGeometryEvidence = measurements.flatMap(measurement => measurement.features
      .filter(feature => feature.kind === 'through-opening')
      .flatMap(feature => expectedOpeningDimensions.filter(expectedOpening =>
        Math.abs(feature.openingSpanAlong - expectedOpening.opening) < 1e-4).map(expectedOpening => ({
          stableKey: measurement.stableKey, featureKind: feature.kind,
          parameter: expectedOpening.parameter, baseDimension: expectedOpening.base,
          clearance, expectedOpeningSpan: expectedOpening.opening,
          measuredOpeningSpan: feature.openingSpanAlong,
        }))));
    assert(clearanceGeometryEvidence.length > 0,
      `${expected.id}: real through-opening size must include the selected two-sided assembly clearance`);
    const materialRows = group.parts.map(part => ({ stableKey: part.stableKey, name: part.name,
      partKind: part.partKind, length: part.length, quantity: part.quantity, unitQuantity: part.unitQuantity,
      sourceMembers: part.properties['manufacturing.sourceMembers'].map(source => source.itemKey),
      rawStockLength: part.properties['manufacturing.rawStockLength'],
      segmentCount: part.properties['manufacturing.segmentCount'] }));
    report.cases.push({ id: expected.id, name: instance.name, sourceRow: expected.sourceRow,
      quantity: instance.quantity, coverage: expected.coverage, entityId: instance.entityId,
      memberCount: active.members.length, escapeWindowMembers: escapeMembers.length,
      generationRunId: active.generationRun.entityId, parametersChecked: Object.keys(expected.parameters).length,
      activatedAndQueriedFromNativeScene: true, assemblyParameters, activeAssemblyKeys,
      inactiveAssemblyDraftKeys: assemblyKeys.filter(key => !activeAssemblyKeys.includes(key)),
      nativeDisassembly: { successful: true, partCount: group.parts.length, milliseconds: Date.now() - started,
        persistedPartIdsQueriedFromScene: true, readyFinalGeometryForEveryPart: true,
        productionQuantitiesVerified: true, materialRows, finalBRepMeasurements: measurements,
        clearanceGeometryEvidence } });
    writeFileSync(resolve(output, 'native-frontend-progress.json'), JSON.stringify(report, null, 2));
    console.log(`PASS ${expected.id}: ${instance.quantity} production quantity, ${active.members.length} display members, ${group.parts.length} real disassembled parts`);
  }
  assert.deepEqual(browserErrors, []);
  report.totalInstances = actual.instances.length;
  report.totalProductionQuantity = actual.instances.reduce((sum, instance) => sum + instance.quantity, 0);
  report.directImportWithoutConfirmation = true;
  report.productReadSceneCreationAndQueriesVerified = true;
  report.allCasesNativeDisassemblyVerified = true;
  report.totalManufacturingParts = report.cases.reduce((sum, sample) => sum + sample.nativeDisassembly.partCount, 0);
  report.assemblyClearanceValues = [...new Set(report.cases.map(sample => sample.assemblyParameters.assemblyClearance))].sort((a, b) => a - b);
  report.status = 'passed';
  await page.screenshot({ path: resolve(output, 'native-batch-import-security-window.png'), fullPage: true });
  console.log(`Native security-window batch import: ${report.totalInstances} styles in one workbook, ${report.totalProductionQuantity} production quantity.`);
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
