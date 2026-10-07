// Current public security-window entry, real native SDO, final persisted BReps.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = resolve(process.env.ICAX_SECURITY_PUBLIC_REPORT || resolve(root, 'output/tests/security-window-public-native/public'));
const runtime = resolve(process.env.ICAX_SECURITY_WINDOW_RUNTIME || resolve(root, 'output/tests/security-window-public-native/runtime'));
const descriptor = JSON.parse(readFileSync(resolve(root, 'src/apps/tube-designer/templates/product/single_face_security_window/template.json'), 'utf8'));
const normalized = values => Object.fromEntries(descriptor.parameters.map(field => [field.key,
  field.readOnly || values[field.key] == null ? field.defaultValue : values[field.key]]));
const templateId = descriptor.id;
const filter = process.env.ICAX_SECURITY_PUBLIC_FILTER ? new RegExp(process.env.ICAX_SECURITY_PUBLIC_FILTER) : null;
mkdirSync(output, { recursive: true });
const cases = [];
for (const faceType of ['single', 'two', 'three', 'five']) {
  cases.push({ id: `${faceType}.segment`, parameters: { faceType }, measure: true });
  for (const [kind, parameters] of [
    ['sharp-v', { outerFrameGrooveTool: 'system:v-notch-sharp', outerFrameVGrooveBottomStrategy: 'sharp' }],
    ['rounded-v', { outerFrameGrooveTool: 'system:v-notch-sharp', outerFrameVGrooveBottomStrategy: 'rounded', outerFrameVGrooveRoundRadius: 2 }],
    ['edge-arc', { outerFrameGrooveTool: 'system:edge-arc-groove' }],
  ]) cases.push({ id: `${faceType}.plane.${kind}`, parameters: { faceType, frameManufacturingMode: 'plane_v_notch', ...parameters }, measure: true });
  if (['two', 'three'].includes(faceType)) {
    for (const [kind, parameters] of [
      ['sharp-v', { outerFrameGrooveTool: 'system:v-notch-sharp' }],
      ['rounded-v', { outerFrameGrooveTool: 'system:v-notch-sharp', outerFrameVGrooveBottomStrategy: 'rounded', outerFrameVGrooveRoundRadius: 2 }],
      ['edge-arc', { outerFrameGrooveTool: 'system:edge-arc-groove' }],
    ]) cases.push({ id: `${faceType}.spatial.${kind}`, parameters: { faceType, frameManufacturingMode: 'spatial_v_notch', ...parameters }, measure: true });
  }
}
for (const horizontalEndConnection of ['insert', 'weld', 'tabs']) for (const verticalEndConnection of ['insert', 'weld', 'tabs'])
  cases.push({ id: `connections.${horizontalEndConnection}.${verticalEndConnection}`, parameters: { horizontalEndConnection, verticalEndConnection }, measure: true });
for (const foldedPostJoint of ['weld', 'tabs']) cases.push({ id: `equal-section-posts.${foldedPostJoint}`,
  parameters: { faceType: 'three', frameManufacturingMode: 'plane_v_notch', foldedPostJoint }, measure: true });
cases.push({ id: 'materials.rectangular-vertical.tabs', parameters: { tubeSpecificationPreset: 'custom',
  verticalProfileType: 'rect', verticalWidth: 16, verticalDepth: 16, verticalCornerRadius: 1,
  verticalWallThickness: 0.8, horizontalEndConnection: 'tabs', verticalEndConnection: 'tabs' }, measure: true });
for (const preset of descriptor.extensions.parameterPresets.presets) cases.push({
  id: `materials.preset.${preset.value}`, parameters: {
    [descriptor.extensions.parameterPresets.selectorParameter]: preset.value, ...preset.values,
  }, measure: true });
cases.push({ id: 'independent-window-folds', parameters: { frameManufacturingMode: 'plane_v_notch',
  outerFrameGrooveTool: 'system:v-notch-sharp', outerFrameVGrooveBottomStrategy: 'rounded', outerFrameVGrooveRoundRadius: 3,
  doorFrameJoinType: 'v_groove_90:tool_library', doorFrameGrooveTool: 'system:v-notch-sharp',
  doorFrameVGrooveBottomStrategy: 'rounded', doorFrameVGrooveRoundRadius: 1.5,
  doorLeafFrameJoinType: 'v_groove_90:tool_library', doorLeafFrameGrooveTool: 'system:edge-arc-groove',
  doorLeafFrameFoldBridge: 0.7 }, measure: true });

for (const [kind, tool, strategy] of [
  ['rounded-v', 'system:v-notch-sharp', 'rounded'],
  ['edge-arc', 'system:edge-arc-groove', 'sharp'],
]) cases.push({ id: `three.spatial.${kind}.kfactor`, parameters: {
  faceType: 'three', frameManufacturingMode: 'spatial_v_notch', outerFrameGrooveTool: tool,
  outerFrameVGrooveBottomStrategy: strategy, outerFrameVGrooveRoundRadius: 2,
  outerFrameBendKFactor: 0.42,
}, measure: true });
// A retained unsupported spatial draft on five faces must keep the host echo
// without consuming any of its hidden independent-post machining dimensions.
for (const [kind, tool, strategy] of [
  ['sharp-v', 'system:v-notch-sharp', 'sharp'],
  ['rounded-v', 'system:v-notch-sharp', 'rounded'],
  ['edge-arc', 'system:edge-arc-groove', 'sharp'],
]) cases.push({ id: `five.retained-spatial.${kind}`, parameters: {
  faceType: 'five', frameManufacturingMode: 'spatial_v_notch', outerFrameGrooveTool: tool,
  outerFrameVGrooveBottomStrategy: strategy, foldedPostJoint: 'tabs',
  foldedPostTabWidth: 0, foldedPostTabLength: 0, foldedPostSideClearance: -1,
}, measure: true });
const report = { startedAt: new Date().toISOString(), templateId, version: descriptor.version, selectedCases: 0, cases: [], nativeCalls: 0, methods: {} };
const save = () => writeFileSync(resolve(output, 'results.json'), JSON.stringify(report, null, 2));
class Bridge {
  constructor() {
    this.sequence = 0; this.pending = new Map(); this.stderr = '';
    this.process = spawn(resolve(runtime, 'SecurityWindowFramesBridge.exe'), [], { cwd: runtime, windowsHide: true,
      env: { ...process.env, PATH: runtime + ';' + process.env.PATH }, stdio: ['pipe', 'pipe', 'pipe'] });
    this.process.stderr.on('data', bytes => { this.stderr += bytes; });
    const fail = error => { for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(error); } this.pending.clear(); };
    this.process.on('error', fail); this.process.on('exit', code => { if (this.pending.size) fail(new Error(`Native bridge exited ${code}: ${this.stderr}`)); });
    createInterface({ input: this.process.stdout }).on('line', line => {
      let result; try { result = JSON.parse(line); } catch { return fail(new Error(`Invalid bridge output: ${line}`)); }
      const pending = this.pending.get(result.id); if (!pending) return;
      this.pending.delete(result.id); clearTimeout(pending.timer);
      result.ok ? pending.resolve(result.result) : pending.reject(new Error(result.error));
    });
  }
  invoke(method, payload = {}) {
    report.nativeCalls++;
    const started = performance.now();
    const record = rejected => {
      const seconds = (performance.now() - started) / 1000;
      const statistics = report.methods[method] ??= { count: 0, rejected: 0, totalSeconds: 0, maximumSeconds: 0 };
      statistics.count++; statistics.rejected += rejected ? 1 : 0;
      statistics.totalSeconds += seconds; statistics.maximumSeconds = Math.max(statistics.maximumSeconds, seconds);
    };
    return new Promise((resolveRequest, reject) => {
      const id = ++this.sequence;
      this.pending.set(id, { resolve: resolveRequest, reject, timer: setTimeout(() => { this.pending.delete(id); reject(new Error(`Native timeout: ${method}`)); this.stop(); }, 120000) });
      this.process.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
    }).then(value => { record(false); return value; }, error => { record(true); throw error; });
  }
  stop() { this.process.stdin.end(); this.process.kill(); }
}

for (const test of cases.filter(row => !filter || filter.test(row.id))) {
  report.selectedCases++;
  const bridge = new Bridge();
  const started = performance.now();
  const row = { id: test.id, parameters: test.parameters, status: 'passed' };
  try {
    if (!report.nativeModules) {
      const { modules } = await bridge.invoke('GetRuntimeModules');
      report.nativeModules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
        assert.equal(dirname(path).toLowerCase(), runtime.toLowerCase());
        return [name, { path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex') }];
      }));
    }
    const values = normalized(test.parameters);
    const preview = await bridge.invoke('GenerateProductTemplatePreview', { templateId, parameters: test.parameters });
    assert.deepEqual(preview.parameters, values, 'Native preview must preserve normalized host parameters');
    row.members = preview.items.length;
    const generated = await bridge.invoke('GeneratePreview', { templateId, ...values });
    assert.deepEqual(generated.tubeDesigner.product.parameters, values);
    const disassemblyStarted = performance.now();
    let result;
    try { result = await bridge.invoke('Disassemble'); }
    finally { row.disassemblySeconds = (performance.now() - disassemblyStarted) / 1000; }
    const product = result.tubeDesigner.product;
    const parts = result.tubeDesigner.parts;
    row.types = parts.length; row.quantity = parts.reduce((total, part) => total + part.unitQuantity, 0);
    row.validBReps = result.geometryChecks.length;
    for (const check of result.geometryChecks) { assert.equal(check.valid, true, check.key); assert.equal(check.solids, 1, check.key); assert.ok(check.volume > 0, check.key); }
    const resources = await bridge.invoke('InspectScriptResources');
    assert.deepEqual(resources.generatedParameters, values);
    assert.deepEqual(resources.manufacturingParameters, values);
    const native = resources.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessNative'];
    const stocks = native.stocks;
    const postInstances = native.plan.instances.filter(instance => instance.templateId === 'orthogonal-corner');
    if (values.faceType !== 'single' && values.frameManufacturingMode !== 'segment_weld') {
      assert.ok(postInstances.length, 'Folded multi-face corners must retain their actual post processes');
      const retainedSpatial = values.faceType === 'five' && values.frameManufacturingMode === 'spatial_v_notch';
      const expectedJoint = values.outerFrameGrooveTool === 'system:edge-arc-groove'
        ? 'tabs' : retainedSpatial ? 'weld' : values.foldedPostJoint;
      for (const instance of postInstances) {
        assert.equal(instance.parameters.cJoint, expectedJoint, `${instance.instanceId}: actual supported post connection`);
        if (retainedSpatial && expectedJoint === 'tabs') for (const [key, value] of [
          ['tabWidth', 8], ['tabLength', 12], ['sideClearance', 0.2],
        ]) assert.equal(instance.parameters[key], value, `${instance.instanceId}: hidden draft must not enter ${key}`);
      }
      row.actualPostJoint = expectedJoint;
    }
    assert.equal(row.quantity, Object.keys(stocks).length, 'Aggregated row quantities cover every native stock');
    const sourceIds = new Set(), sourceMembers = new Set();
    for (const part of parts) {
      const properties = part.properties;
      assert.equal(properties['manufacturing.sourceStockIds'].length, part.unitQuantity);
      const identities = new Set();
      for (const id of properties['manufacturing.sourceStockIds']) { assert.ok(!sourceIds.has(id)); sourceIds.add(id); identities.add(stocks[id].manufacturingIdentity); }
      assert.equal(identities.size, 1, 'Only identical final process/profile/geometry identities may merge');
      for (const member of properties['manufacturing.sourceMembers']) { assert.ok(!sourceMembers.has(member.itemKey)); sourceMembers.add(member.itemKey); }
    }
    assert.deepEqual([...sourceMembers].sort(), preview.items.map(item => item.key).sort(), 'Every displayed member must remain linked to its final part');
    row.measuredParts = 0; row.measuredFeatures = 0;
    if (test.measure) for (const part of parts) {
      const measurement = await bridge.invoke('MeasurePartGeometry', { partEntityId: part.entityId, resourceVersion: part.manufacturingGeometryResourceVersion });
      assert.equal(measurement.source, 'final-brep');
      assert.equal(measurement.recoveredFromGeneration, false, 'Measurement must read the committed final BRep');
      assert.equal(measurement.resourceId, part.manufacturingGeometryResourceId);
      assert.equal(measurement.resourceVersion, part.manufacturingGeometryResourceVersion);
      assert.ok(measurement.available);
      assert.ok(Number.isFinite(measurement.length) && measurement.length > 0, part.stableKey);
      const reference = measurement.linearReference;
      assert.ok(reference.start.every(Number.isFinite) && reference.end.every(Number.isFinite));
      assert.ok(Math.abs(Math.hypot(...reference.end.map((value, index) => value - reference.start[index])) - measurement.length) < 1e-5);
      assert.equal(measurement.openingCount, measurement.features.length);
      for (const feature of measurement.features) {
        assert.ok(feature.center.every(Number.isFinite), `${part.stableKey}: opening center`);
        assert.ok(Number.isFinite(feature.station) && feature.station >= -1e-5 && feature.station <= measurement.length + 1e-5,
          `${part.stableKey}: opening station ${feature.station} outside final body`);
      }
      row.measuredParts++; row.measuredFeatures += measurement.features.length;
    }
    if (test.measure && parts.length) await assert.rejects(bridge.invoke('MeasurePartGeometry', {
      partEntityId: parts[0].entityId, resourceVersion: parts[0].manufacturingGeometryResourceVersion + 1,
    }), /version changed/, 'Final measurement must reject a stale BRep version');
    assert.deepEqual(product.parameters, values);
    row.performancePassed = row.disassemblySeconds <= 10;
    if (!row.performancePassed) row.performanceIssue = 'Disassembly exceeded the requested 10 second limit';
  } catch (error) { row.status = 'failed'; row.error = error.stack; }
  finally { writeFileSync(resolve(output, `${test.id}.stderr.log`), bridge.stderr); bridge.stop(); }
  row.seconds = (performance.now() - started) / 1000;
  report.cases.push(row); save();
  console.log(`${row.status.toUpperCase()} ${row.id}: ${row.types ?? '?'} types / ${row.quantity ?? '?'} units, disassembly ${(row.disassemblySeconds ?? 0).toFixed(2)}s${row.error ? '\n' + row.error.split('\n').slice(0, 2).join('\n') : ''}`);
}
report.finishedAt = new Date().toISOString();
report.passed = report.cases.filter(row => row.status === 'passed').length;
report.failed = report.cases.length - report.passed;
report.performanceFailures = report.cases.filter(row => row.performancePassed === false).map(row => ({ id: row.id, seconds: row.disassemblySeconds }));
save();
if (report.failed || report.performanceFailures.length) process.exitCode = 1;
console.log(`Native public acceptance: ${report.passed}/${report.cases.length} functional passes; ${report.performanceFailures.length} timing failures`);
