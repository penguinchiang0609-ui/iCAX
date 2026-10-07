// Independent, read-only audit of the current deployed product catalogue.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, existsSync, writeFileSync } from 'node:fs';
import { resolve, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || resolve(root, 'src/x64/Debug'));
const productRoot = resolve(root, 'src/apps/tube-designer/templates/product');
const deployedProductRoot = resolve(runtimeRoot, 'apps/tube-designer/templates/product');
const output = resolve(root, 'output/tests/assembly-process',
  process.env.ICAX_RECHECK_EVIDENCE || 'all-products-recheck-native.json');
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
function inventory(directory, archived = false) {
  return readdirSync(directory, { withFileTypes: true }).filter(entry => entry.isDirectory()
    && existsSync(resolve(directory, entry.name, 'template.json'))).map(entry => {
    const path = resolve(directory, entry.name), descriptor = JSON.parse(readFileSync(resolve(path, 'template.json'), 'utf8'));
    const script = readFileSync(resolve(path, 'template.py'), 'utf8');
    const deployedPath = resolve(deployedProductRoot, archived ? '_legacy' : '', entry.name);
    const match = file => existsSync(resolve(deployedPath, file))
      && hash(resolve(path, file)) === hash(resolve(deployedPath, file));
    return { directory: relative(productRoot, path), id: descriptor.id, version: descriptor.version,
      descriptorVersion: descriptor.schemaVersion, archived,
      explicitDeprecated: descriptor.deprecated ?? descriptor.extensions?.deprecated ?? null,
      extensionKeys: Object.keys(descriptor.extensions || {}),
      hasDisplayEntry: /^def display\(/m.test(script), hasManufacturingEntry: /^def manufacturing\(/m.test(script),
      hasGenerateEntry: /^def generate\(/m.test(script) || /^generate\s*=/m.test(script),
      deployedDescriptorMatchesSource: match('template.json'), deployedScriptMatchesSource: match('template.py') };
  }).sort((a, b) => a.id.localeCompare(b.id));
}
const report = { schema: 'icax.all-products-recheck-native', schemaVersion: 1,
  startedAt: new Date().toISOString(), runtimeRoot, productionMutated: false, buildsOrDeployments: false,
  sourceActive: inventory(productRoot), sourceArchived: inventory(resolve(productRoot, '_legacy'), true),
  hostDiscovered: [], activeChecks: [], archivedReachability: [], anchorChecks: null, passed: false };
const child = spawn(resolve(root, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe'), [], {
  cwd: runtimeRoot, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'],
  env: { ...process.env, PATH: resolve(root, 'src/x64/Debug') + ';' + process.env.PATH },
});
let sequence = 0, stderr = '';
const pending = new Map();
const fail = error => { for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); } pending.clear(); };
child.stderr.on('data', value => stderr += value);
child.on('error', fail);
child.on('exit', code => { if (pending.size) fail(new Error(`Native exit ${code}: ${stderr}`)); });
createInterface({ input: child.stdout }).on('line', line => {
  let response; try { response = JSON.parse(line); } catch { fail(new Error(line)); return; }
  const request = pending.get(response.id); if (!request) return;
  pending.delete(response.id); clearTimeout(request.timer);
  response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
});
const invoke = (method, payload = {}) => new Promise((resolveRequest, reject) => {
  const id = ++sequence;
  pending.set(id, { resolve: resolveRequest, reject, timer: setTimeout(() => {
    pending.delete(id); reject(new Error(`Timeout ${method}: ${payload.templateId || ''}: ${stderr}`));
  }, 240000) });
  child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
});
const save = () => writeFileSync(output, JSON.stringify(report, null, 2));
try {
  const snapshot = await invoke('List');
  report.hostDiscovered = snapshot.tubeDesigner.templates.map(template => ({ id: template.id,
    version: template.version, available: template.available, libraryScope: template.libraryScope,
    descriptorLoaded: template.descriptorLoaded, parameterCount: template.parameters.length }));
  console.log(`Actual native discovery: ${report.hostDiscovered.length} products`);
  const systemIds = report.hostDiscovered.filter(template => template.libraryScope === 'system').map(template => template.id).sort();
  assert.deepEqual(systemIds, report.sourceActive.map(template => template.id).sort());
  assert.ok([...report.sourceActive, ...report.sourceArchived].every(template => template.deployedDescriptorMatchesSource
    && template.deployedScriptMatchesSource), 'Current source/deployed product package files differ');
  save();
  for (const entry of report.hostDiscovered) {
    assert.equal(entry.available, true, `Discovered package unavailable: ${entry.id}`);
    const descriptor = (await invoke('GetTemplateDescriptor', { templateId: entry.id })).template;
    const parameters = Object.fromEntries(descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]));
    const inputBefore = structuredClone(parameters);
    const preview = await invoke('GenerateProductTemplatePreview', { templateId: entry.id, parameters });
    assert.equal(preview.templateId, entry.id); assert.deepEqual(preview.parameters, parameters);
    assert.deepEqual(parameters, inputBefore); assert.ok(preview.items.length > 0);
    const checked = { templateId: entry.id, descriptorVersion: descriptor.version,
      parameterCount: Object.keys(parameters).length, previewItemCount: preview.items.length,
      normalizedParametersUnchanged: true, displayVersionTwoEnforcedByNativeHost: true };
    const plan = await invoke('GetProductManufacturingPlan', { templateId: entry.id, parameters });
    assert.deepEqual(plan.parameters, parameters); assert.deepEqual(parameters, inputBefore);
    assert.ok(Number.isInteger(plan.partCount) && plan.partCount > 0);
    checked.manufacturingPlan = { accepted: true, partCount: plan.partCount, manufacturingVersionFourEnforcedByNativeHost: true, normalizedParametersUnchanged: true };
    report.activeChecks.push(checked); save();
    console.log(`Native recheck ${entry.id}: ${preview.items.length} preview items, exact parameters; manufacturing ${checked.manufacturingPlan.accepted ? checked.manufacturingPlan.partCount : 'default demonstration rejected'}`);
  }
  for (const entry of report.sourceArchived) {
    let descriptor;
    try { descriptor = (await invoke('GetTemplateDescriptor', { templateId: entry.id })).template; }
    catch (error) {
      assert.match(error.message, /unsupported Python template/);
      report.archivedReachability.push({ templateId: entry.id, nativeDescriptorReachable: false, error: error.message });
      continue;
    }
    const parameters = Object.fromEntries(descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]));
    const preview = await invoke('GenerateProductTemplatePreview', { templateId: entry.id, parameters });
    assert.deepEqual(preview.parameters, parameters);
    report.archivedReachability.push({ templateId: entry.id, nativeDescriptorReachable: true,
      resolvedDescriptorId: descriptor.id, previewItemCount: preview.items.length });
  }
  const fixturePath = resolve(root, 'output/tests/assembly-process/all-products-recheck-anchor-input.json');
  if (existsSync(fixturePath)) {
    const input = JSON.parse(readFileSync(fixturePath, 'utf8')), before = structuredClone(input);
    const positive = await invoke('ValidateSharedManufacturing', input);
    assert.equal(positive.valid, true); assert.deepEqual(input, before);
    const connection = input.manufacturingDefinition.connections.find(connection => connection.items.length >= 2);
    assert.ok(connection);
    const nonparticipant = input.designModel.items.find(item => item.geometry && !connection.items.includes(item.key));
    assert.ok(nonparticipant);
    const validAnchor = structuredClone(input), validTarget = validAnchor.manufacturingDefinition.connections.find(item => item.key === connection.key);
    validTarget.properties = { ...validTarget.properties, participantAnchors: [{ itemKey: connection.items[0] }] };
    assert.equal((await invoke('ValidateSharedManufacturing', validAnchor)).valid, true);
    const results = [];
    for (const kind of ['nonparticipant', 'duplicate']) {
      const invalid = structuredClone(input), target = invalid.manufacturingDefinition.connections.find(item => item.key === connection.key);
      const member = target.items[0], anchor = { itemKey: kind === 'nonparticipant' ? nonparticipant.key : member };
      target.properties = { ...target.properties, participantAnchors: kind === 'duplicate' ? [anchor, structuredClone(anchor)] : [anchor] };
      const invalidBefore = structuredClone(invalid);
      let rejection; try { await invoke('ValidateSharedManufacturing', invalid); } catch (error) { rejection = error.message; }
      assert.ok(rejection); assert.deepEqual(invalid, invalidBefore); assert.deepEqual(input, before);
      results.push({ kind, rejected: true, error: rejection, inputUnchanged: true });
    }
    report.anchorChecks = { positive: true, validParticipantAnchorAccepted: true, connectionKey: connection.key,
      nonparticipantItemKey: nonparticipant.key, cases: results, originalInputUnchanged: true };
    report.nestedDisplayMachiningChecks = [];
    for (const field of ['cutPlanes', 'machiningFeatures', 'slotFeatures']) {
      const invalid = structuredClone(input);
      invalid.designModel.items[0].properties.customComponent = { [field]: [] };
      const invalidBefore = structuredClone(invalid);
      let rejection;
      try { await invoke('ValidateSharedManufacturing', invalid); }
      catch (error) { rejection = error.message; }
      assert.ok(rejection, `Native shared display accepted nested machining field: ${field}`);
      assert.match(rejection, /not display data/);
      assert.deepEqual(invalid, invalidBefore);
      report.nestedDisplayMachiningChecks.push({ field, rejected: true, inputUnchanged: true, error: rejection });
    }
  }
  report.finishedAt = new Date().toISOString(); report.passed = true; save();
  console.log(JSON.stringify({ passed: true, active: report.activeChecks.length,
    archived: report.archivedReachability.length, legacyIdsReachable: report.archivedReachability.filter(row => row.nativeDescriptorReachable).length,
    anchorChecks: report.anchorChecks?.cases.length ?? 0 }));
} catch (error) {
  report.error = error.stack; report.finishedAt = new Date().toISOString(); save(); throw error;
} finally { child.stdin.end(); child.kill(); }
