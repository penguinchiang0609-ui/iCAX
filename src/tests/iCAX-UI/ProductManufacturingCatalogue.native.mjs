// Exercise every deployed manufacturing entry through the native host planning API.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compareBaselineShapes, inspectManufacturedShapes } from './productManufacturingShapeAssertions.mjs';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const binaryRoot = resolve(repository, 'src/x64/Debug');
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || binaryRoot);
const output = resolve(repository, 'output/tests/assembly-process');
const baselineFile = resolve(output, 'manufacturing-before.json');
const retiredIds = new Set(['louver-window', 'aluminium-window', 'decorative-door']);
const baselines = JSON.parse(readFileSync(baselineFile, 'utf8')).filter(entry => !retiredIds.has(entry.document.template.id));
assert.equal(baselines.length, 10);
assert.equal(new Set(baselines.map(entry => entry.document.template.id)).size, 10);
mkdirSync(output, { recursive: true });
const report = { runtimeRoot, baselineFile, cases: [], aluminiumFixtureIsSynthetic: false,
  defaultUnverifiedAluminiumRejected: false, passed: false };

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
  child.stderr.on('data', data => { stderr += data; });
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
    close() { child.stdin.end(); child.kill(); },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 300000);
        pending.set(id, { resolve: resolveRequest, reject, timer });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

function compareMaterialList(plan, baseline) {
  const rows = plan.tables.flatMap(table => table.rows);
  assert.ok(rows.length > 0, 'Assembly execution must produce a material list');
  for (const item of baseline.items) {
    const row = rows.find(entry => entry.itemKey === item.key);
    assert.ok(row, `The derived material list must include ${item.key}`);
    const properties = item.properties;
    if (typeof properties.length === 'number') {
      assert.ok(Math.abs(Number(row.values.length) - properties.length) <= 0.001,
        `${item.key} material length must retain the baseline to the list's millimetre precision`);
    }
    const material = properties['manufacturing.materialGrade'] || properties['manufacturing.material']
      || properties.materialGrade || properties.material;
    if (material) assert.equal(row.values.materialGrade || row.values.material, material, item.key);
    if (typeof properties.quantity === 'number') assert.equal(row.values.quantity, properties.quantity, item.key);
  }
}

const native = connection();
try {
  for (const baseline of baselines) {
    const templateId = baseline.document.template.id;
    const parameters = structuredClone(baseline.parameters);
    assert.deepEqual(parameters, baseline.document.parameters, `${templateId} baseline host input`);

    const plan = await native.invoke('GetProductManufacturingPlan', { templateId, parameters });
    assert.equal(plan.templateId, templateId);
    assert.deepEqual(plan.parameters, parameters, `${templateId} host parameters must retain the exact input`);
    assert.equal(plan.partCount, baseline.document.items.length, `${templateId} manufacturing item count`);
    compareMaterialList(plan, baseline.document);
    assert.deepEqual(parameters, baseline.parameters);
    const result = { name: baseline.name, templateId, partCount: plan.partCount,
      tableCount: plan.tables.length, tableRows: plan.tables.reduce((count, table) => count + table.rows.length, 0),
      hostParametersEqual: true, baselinePartCountEqual: true, derivedMaterialsAndLengthsEqual: true };
    if (['single_face_security_window', 'straight_steel_staircase'].includes(baseline.name)) {
      await native.invoke('GeneratePreview', { templateId, ...parameters });
      await native.invoke('Disassemble');
      await inspectManufacturedShapes(native, baseline.name === 'single_face_security_window');
      result.baselineShapes = await compareBaselineShapes(native, baseline.document);
    }
    report.cases.push(result); console.log(JSON.stringify(result));
  }
  report.passed = true;
  console.log(JSON.stringify({ passed: true, catalogueCases: report.cases.length,
    activeProductCount: 10 }));
} catch (error) {
  report.error = error.message;
  throw error;
} finally {
  native.close();
  writeFileSync(resolve(output, 'manufacturing-v4-catalogue-native-deployed.json'), JSON.stringify(report, null, 2));
}
