// Short same-input API recheck after a validator-only DLL change. No BRep resampling.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const binaryRoot = resolve(repository, 'src/x64/Debug');
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || binaryRoot);
const output = resolve(repository, 'output/tests/assembly-process');
const lifecycleFile = resolve(output, 'all-products-recheck-native-shared-workpieces.json');
const lifecycle = JSON.parse(readFileSync(lifecycleFile, 'utf8'));
const completed = lifecycle.cases.filter(entry => entry.passed);
assert.ok(completed.length > 0);
const reportFile = resolve(output, 'all-products-recheck-native-shared-workpieces-new-dll-api.json');
const deploymentFile = resolve(output, 'all-products-recheck-deployment-hashes.json');
const deployment = JSON.parse(readFileSync(deploymentFile, 'utf8'));
assert.equal(deployment.passed, true);
for (const binary of deployment.binaries) assert.equal(
  createHash('sha256').update(readFileSync(resolve(runtimeRoot, binary.file))).digest('hex'), binary.sha256);
const report = { schema: 'icax.native-shared-workpiece-api-recheck', schemaVersion: 1,
  runtimeRoot, lifecycleFile, deploymentFile, dllHashes: deployment.binaries,
  checkpoint: 'after-host-applicability-fix', retainedBRepCheckpoint: 'before-host-applicability-fix',
  startedAt: new Date().toISOString(), cases: [], passed: false };
const save = () => writeFileSync(reportFile, JSON.stringify(report, null, 2));

function connection() {
  const child = spawn(resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
    || resolve(repository, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe')), [], {
    cwd: runtimeRoot, windowsHide: true,
    env: { ...process.env, PATH: `${binaryRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'],
  });
  const pending = new Map();
  let sequence = 0, stderr = '';
  const fail = error => {
    for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
    pending.clear();
  };
  child.stderr.on('data', value => { stderr += value; });
  child.on('error', fail);
  child.on('exit', code => { if (pending.size) fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { fail(new Error(line)); return; }
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    close() {
      return new Promise(resolveClosed => {
        if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
        child.once('exit', resolveClosed); child.stdin.end(); child.kill();
      });
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 600000);
        pending.set(id, { resolve: resolveRequest, reject, timer });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

function comparePlan(plan, reference, parameters) {
  assert.equal(plan.templateId, reference.templateId);
  assert.deepEqual(plan.parameters, parameters);
  assert.equal(plan.partCount, reference.document.items.length);
  const rows = plan.tables.flatMap(table => table.rows);
  for (const item of reference.document.items) {
    const row = rows.find(entry => entry.itemKey === item.key);
    assert.ok(row, `Missing actual material row: ${item.key}`);
    if (typeof item.properties.length === 'number') assert.ok(
      Math.abs(Number(row.values.length) - item.properties.length) <= 0.001, item.key);
    if (typeof item.properties.quantity === 'number') assert.equal(row.values.quantity, item.properties.quantity, item.key);
    const material = item.properties['manufacturing.materialGrade'] || item.properties['manufacturing.material']
      || item.properties.materialGrade || item.properties.material;
    if (material) assert.equal(row.values.materialGrade || row.values.material, material, item.key);
  }
}

try {
  for (const completedCase of completed) {
    const reference = JSON.parse(readFileSync(completedCase.sourceReferenceFile, 'utf8'));
    const parameters = structuredClone(completedCase.parameters);
    const native = connection();
    const result = { name: completedCase.name, templateId: completedCase.templateId, passed: false };
    try {
      const descriptor = (await native.invoke('GetTemplateDescriptor', { templateId: reference.templateId })).template;
      assert.equal(descriptor.id, reference.templateId);
      const preview = await native.invoke('GenerateProductTemplatePreview', { templateId: reference.templateId, parameters });
      assert.equal(preview.templateId, reference.templateId); assert.deepEqual(preview.parameters, parameters);
      assert.deepEqual(preview.items.map(item => item.key).sort(), reference.display.items.map(item => item.key).sort());
      const shared = { designModel: reference.display, manufacturingDefinition: reference.manufacturing };
      const frozen = structuredClone(shared);
      assert.deepEqual(await native.invoke('ValidateSharedManufacturing', shared),
        { valid: true, designItemCount: reference.document.items.length, manufacturingPartCountKnown: false });
      assert.deepEqual(shared, frozen);
      const plan = await native.invoke('GetProductManufacturingPlan', { templateId: reference.templateId, parameters });
      comparePlan(plan, reference, parameters);
      assert.deepEqual(parameters, completedCase.parameters);
      Object.assign(result, { passed: true, nativeDescriptorAccepted: true, previewKeysAndHostParametersEqual: true,
        sharedDesignAndManufacturingReferencesAccepted: true, partCountAndMaterialListEqual: true,
        actualManufacturingParts: plan.partCount, retainedLifecycleEvidence: true });
      report.cases.push(result); save(); console.log(JSON.stringify(result));
    } catch (error) {
      result.error = error.stack; report.cases.push(result); save(); throw error;
    } finally { await native.close(); }
  }
  report.finishedAt = new Date().toISOString(); report.passed = report.cases.every(entry => entry.passed);
  save(); console.log(JSON.stringify({ passed: report.passed, cases: report.cases.length, reportFile }));
} catch (error) {
  report.finishedAt = new Date().toISOString(); report.error = error.message; save();
  console.error(JSON.stringify({ passed: false, reportFile, error: error.message.slice(0, 300) }));
  process.exitCode = 1;
}
