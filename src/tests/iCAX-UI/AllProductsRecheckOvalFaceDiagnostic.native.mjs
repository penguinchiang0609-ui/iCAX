// Read-only geometric measurement diagnostics; this is not a completed case report.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const output = resolve(repository, 'output/tests/assembly-process');
const runtimeRoot = resolve(repository, 'src/x64/Debug');
const bridge = resolve(repository, 'tmp/oval-face-diagnostic/bin/SecurityWindowFramesBridge.exe');
const inspector = resolve(repository, 'tmp/oval-face-diagnostic/SecurityWindowFramesFaceDiagnosticBridge.cpp');
const driver = fileURLToPath(import.meta.url);
const faceHelper = resolve(repository, 'tmp/oval-face-diagnostic/OvalFaceDiagnostics.hxx');
const faceHelperImplementation = resolve(repository, 'tmp/oval-face-diagnostic/OvalFaceDiagnostic.h');
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const read = path => JSON.parse(readFileSync(path, 'utf8').replace(/^\uFEFF/, ''));
const prefix = resolve(output, 'all-products-recheck-native-branches-stair-l-oval');
const failed = read(`${prefix}.json`), previous = failed.cases[0];
assert.equal(failed.passed, false);
assert.equal(previous.name, 'stair-l-oval');
assert.equal(previous.partFacts.parts, 100);
const index = read(`${prefix}-stage-evidence-index.json`);
const stages = new Map(index.stageArtifacts.map(row => [row.stage, row]));
function stage(name) {
  const artifact = stages.get(name);
  assert.ok(artifact, `Missing frozen stage ${name}`);
  assert.equal(digest(artifact.path), artifact.sha256);
  const saved = read(artifact.path);
  assert.equal(saved.evidence.stage, name);
  for (const path of saved.negativeZeroPaths) {
    let target = saved.evidence;
    for (const key of path.slice(0, -1)) target = target[key];
    assert.equal(target[path.at(-1)], 0);
    target[path.at(-1)] = -0;
  }
  return saved.evidence;
}
const before = stage('scene-before-shape-comparison');
const world = stage('world-inputs');
const local = stage('legacy-local.neutral-points');
const host = read(previous.hostInputFile), reference = read(previous.sourceReferenceFile);
assert.equal(digest(previous.hostInputFile), index.hostInput.sha256);
assert.equal(digest(previous.sourceReferenceFile), index.sourceReference.sha256);
assert.deepEqual(host.parameters, previous.parameters);
for (const [path, hash] of Object.entries(reference.sourceDigests)) {
  assert.equal(digest(resolve(repository, 'src', path)), hash);
  assert.equal(digest(resolve(runtimeRoot, path)), hash);
}
for (const [key, pathKey] of [['bridgeSha256', 'bridgePath'], ['runtimeDllSha256', 'runtimeDLL'],
  ['inspectorSourceSha256', 'inspectorSource'], ['routerSha256', 'routerFile']]) {
  assert.equal(digest(failed.inspectionArtifacts[pathKey]), index.inspectionArtifacts[key]);
}
const previousGKFile = resolve(output, 'takeover-oval-volume-gk-diagnostic.json');
const previousGK = read(previousGKFile);
assert.ok(previousGK.finishedAt); assert.equal(previousGK.completedFullCase, false);
assert.equal(previousGK.cases.length, 3); assert.equal(previousGK.cases[2].passed, false);
const capturedByKey = new Map(previousGK.cases.map(row => [row.key, row.capturedResource]));
for (const capture of capturedByKey.values()) assert.equal(digest(capture.path), capture.sha256);
const previousGKArtifacts = new Map(previousGK.artifacts.map(row => {
  assert.equal(digest(row.path), row.sha256); const wrapper = read(row.path);
  for (const keys of wrapper.negativeZeroPaths) {
    let target = wrapper.evidence; for (const key of keys.slice(0, -1)) target = target[key]; target[keys.at(-1)] = -0;
  }
  return [row.name, wrapper.evidence];
}));
const reportFile = resolve(output, 'takeover-oval-face-diagnostic.json');
const report = { schema: 'icax.oval-face-and-pcurve-diagnostic', schemaVersion: 1,
  startedAt: new Date().toISOString(), passed: false, completedFullCase: false,
  scope: 'Read-only face contributions and PCurve residuals from frozen neutral and three exact captured BRep byte files; no scene regeneration; diagnostic only',
  previousFailure: { path: `${prefix}.json`, sha256: digest(`${prefix}.json`) },
  frozenStageIndex: { path: `${prefix}-stage-evidence-index.json`, sha256: digest(`${prefix}-stage-evidence-index.json`) },
  hostInput: index.hostInput, sourceReference: index.sourceReference,
  previousGKReport: { path: previousGKFile, sha256: digest(previousGKFile) },
  capturedResources: [...capturedByKey.entries()].map(([key, resource]) => ({ key, ...resource })),
  captureSceneDocumentBinding: previousGK.freshSceneBinding,
  previousInspectionArtifacts: failed.inspectionArtifacts,
  diagnosticInspectionArtifacts: { bridge, bridgeSha256: digest(bridge), inspector,
    inspectorSha256: digest(inspector), driver, driverSha256: digest(driver),
    faceHelper, faceHelperSha256: digest(faceHelper),
    faceHelperImplementation, faceHelperImplementationSha256: digest(faceHelperImplementation),
    runtimeRoot, runtimeDllSha256: digest(resolve(runtimeRoot, 'TemplateRuntime.dll')) },
  thresholds: { boundsAbsolute: 1e-5, volumeAbsoluteMinimum: 1e-4, volumeRelative: 1e-7 },
  artifacts: [] };
const save = () => writeFileSync(reportFile, JSON.stringify(report, null, 2) + '\n');
function artifact(name, value) {
  const path = resolve(output, `takeover-oval-face-diagnostic-${name}.json`);
  const negativeZeroPaths = [];
  const visit = (item, keys) => {
    if (typeof item === 'number' && Object.is(item, -0)) negativeZeroPaths.push(keys);
    else if (Array.isArray(item)) item.forEach((entry, index) => visit(entry, [...keys, index]));
    else if (item && typeof item === 'object') Object.entries(item).forEach(([key, entry]) => visit(entry, [...keys, key]));
  };
  visit(value, []);
  writeFileSync(path, JSON.stringify({ schema: 'icax.oval-measurement-diagnostic-artifact', schemaVersion: 1,
    capturedAt: new Date().toISOString(), evidence: value, negativeZeroPaths }, null, 2) + '\n');
  report.artifacts.push({ name, path, sha256: digest(path), negativeZeros: negativeZeroPaths.length }); save();
}
const connections = [];
function connection(label) {
const child = spawn(bridge, [], { cwd: runtimeRoot, windowsHide: true,
  env: { ...process.env, PATH: `${runtimeRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'] });
const pending = new Map(); let sequence = 0, stderr = '';
child.stderr.on('data', value => { stderr += value; });
function fail(error) {
  for (const entry of pending.values()) { clearTimeout(entry.timer); entry.reject(error); }
  pending.clear();
}
child.on('error', fail);
child.on('exit', code => { if (pending.size) fail(new Error(`Diagnostic bridge exit ${code}: ${stderr}`)); });
createInterface({ input: child.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { fail(new Error(line)); return; }
  const entry = pending.get(response.id); if (!entry) return;
  pending.delete(response.id); clearTimeout(entry.timer);
  console.log(JSON.stringify({ root: label, method: entry.method, phase: 'finished', at: new Date().toISOString() }));
  response.ok ? entry.resolve(response.result) : entry.reject(new Error(`${entry.method}: ${response.error}`));
});
function invoke(method, payload = {}) {
  console.log(JSON.stringify({ root: label, method, phase: 'started', at: new Date().toISOString() }));
  return new Promise((resolveRow, reject) => {
    const id = ++sequence;
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 600000);
    pending.set(id, { method, resolve: resolveRow, reject, timer });
    child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
  });
}

  const close = () => new Promise(resolveClosed => {
    if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
    child.once('exit', resolveClosed); child.stdin.end(); child.kill();
  });
  const handle = { invoke, close }; connections.push(handle);
  console.log(JSON.stringify({ root: label, phase: 'bridge-process-started', pid: child.pid }));
  return handle;
}
function compareMeasurements(expected, actual) {
  for (const check of [expected, actual]) {
    assert.equal(check.valid, true); assert.equal(check.solids, 1);
    assert.ok(Number.isFinite(check.adaptiveVolume) && check.adaptiveVolume > 0);
    assert.ok(Number.isFinite(check.adaptiveRelativeError) && check.adaptiveRelativeError >= 0
      && check.adaptiveRelativeError < 1e-7, 'Adaptive error estimate must be below the original relative threshold');
    assert.equal(check.optimalBounds.length, 6);
    assert.ok(check.optimalBounds.every(Number.isFinite));
  }
  const boundsDelta = actual.optimalBounds.map((value, axis) => value - expected.optimalBounds[axis]);
  const volumeDelta = actual.adaptiveVolume - expected.adaptiveVolume;
  const volumeThreshold = Math.max(1e-4, expected.adaptiveVolume * 1e-7);
  return { volumeDelta, volumeThreshold, boundsDelta,
    originalVolumeDelta: actual.volume - expected.volume,
    originalBoundsDelta: actual.bounds.map((value, axis) => value - expected.bounds[axis]),
    passed: Math.abs(volumeDelta) <= volumeThreshold && boundsDelta.every(value => Math.abs(value) <= 1e-5) };
}
function gaussKronrodDiagnostic(reference, native, intermediates) {
  const all = [{ stage: 'reference', check: reference },
    ...intermediates.map(check => ({ stage: check.stage, check })),
    { stage: 'native', check: native }];
  for (const { check } of all) {
    assert.equal(check.gaussKronrodMeasurements.length, 2);
    for (const row of check.gaussKronrodMeasurements) {
      assert.ok(Number.isFinite(row.volume) && row.volume > 0);
      assert.ok(Number.isFinite(row.relativeError) && row.relativeError >= 0 && row.relativeError < 1e-7);
      assert.equal(row.method, 'VolumePropertiesGK(OnlyClosed=false,IsUseSpan=true,CGFlag=false,IFlag=false,SkipShared=false)');
    }
  }
  const referenceFor = eps => reference.gaussKronrodMeasurements.find(row => row.requestedPrecision === eps);
  const comparisons = [];
  for (const eps of [1e-10, 1e-12]) {
    const baseline = referenceFor(eps);
    assert.ok(baseline);
    for (const { stage, check } of all.slice(1)) {
      const measured = check.gaussKronrodMeasurements.find(row => row.requestedPrecision === eps);
      assert.ok(measured);
      const volumeDelta = measured.volume - baseline.volume;
      const volumeThreshold = Math.max(1e-4, baseline.volume * 1e-7);
      comparisons.push({ stage, eps, reference: baseline, measured, volumeDelta, volumeThreshold,
        passed: Math.abs(volumeDelta) <= volumeThreshold });
    }
  }
  const stabilityBudget = Math.max(1e-4, referenceFor(1e-10).volume * 1e-7) * .01;
  const stability = all.map(({ stage, check }) => {
    const volumeDelta = check.gaussKronrodMeasurements[1].volume - check.gaussKronrodMeasurements[0].volume;
    return { stage, volumeDelta, stabilityBudget, passed: Math.abs(volumeDelta) <= stabilityBudget };
  });
  return { comparisons, stability, passed: comparisons.every(row => row.passed) && stability.every(row => row.passed) };
}
save();
try {
  const selected = ['flight.1.guard.-1.infill.0.0', 'flight.2.guard.-1.top', 'landing.1.guard.0.post.1'];
  const rootFor = item => { const r = item.geometry || item.representations.result;
    return typeof r === 'string' ? r : r.instanceKey || (r.placement ? `instance.${item.key}.result` : r.resource); };
  const rows = [];
  report.parallelRootProcesses = 3; save();
  await Promise.all(selected.map(async key => {
    const native = connection(key); const invoke = native.invoke;
    try {
    const root = rootFor(local.baseline.items.find(item => item.key === key));
    const baselineReply = await invoke('InspectNeutralModel', { model: local.baseline,
      geometryKeys: [root], ...local.inspectionOptions, preciseMeasurements: true, roundTrip: true, normalizeNurbs: true });
    const nativeReply = await invoke('InspectCapturedGeometry', { key, persistencePath: capturedByKey.get(key).path, preciseMeasurements: true });
    const strictBaselineReply = await invoke('InspectNeutralModel', { model: local.baseline,
      geometryKeys: [root], ...local.inspectionOptions, preciseMeasurements: true,
      roundTrip: true, normalizeNurbs: true, precisionEps: 1e-12 });
    const strictNativeReply = await invoke('InspectCapturedGeometry', {
      key, persistencePath: capturedByKey.get(key).path, preciseMeasurements: true, precisionEps: 1e-12 });
    artifact(key.replace(/[^a-z0-9]/gi, '-'), { key, root, baselineReply, nativeReply,
      strictBaselineReply, strictNativeReply });
    const expected = baselineReply.geometryChecks[0], actual = { ...nativeReply.geometryChecks[0] };
    const capturedResource = capturedByKey.get(key);
    assert.equal(actual.resourceHash, capturedResource.resourceHash);
    assert.equal(actual.resourceBytes, capturedResource.byteCount);
    const previousCaptured = previousGKArtifacts.get(key.replace(/[^a-z0-9]/gi, '-')).nativeReply.geometryChecks[0];
    for (const field of ['resourceBytes','resourceHash','volume','bounds','valid','solids','brepErrors'])
      assert.deepEqual(actual[field], previousCaptured[field], 'Exact captured model must rebuild the same measurements');
    const comparison = compareMeasurements(expected, actual);
    const strictExpected = strictBaselineReply.geometryChecks[0], strictActual = strictNativeReply.geometryChecks[0];
    const strictComparison = compareMeasurements(strictExpected, strictActual);
    const stabilityBudget = comparison.volumeThreshold * 0.01;
    const stability = {
      referenceDelta: strictExpected.adaptiveVolume - expected.adaptiveVolume,
      nativeDelta: strictActual.adaptiveVolume - actual.adaptiveVolume, stabilityBudget,
    };
    stability.passed = Math.abs(stability.referenceDelta) <= stabilityBudget
      && Math.abs(stability.nativeDelta) <= stabilityBudget;
    const row = { key, capturedResource, comparison, baseline: expected, actual,
      strictComparison, strictBaseline: strictExpected, strictActual, stability,
      intermediateMeasurements: baselineReply.roundTripChecks,
      strictIntermediateMeasurements: strictBaselineReply.roundTripChecks,
      gaussKronrod: gaussKronrodDiagnostic(expected, actual, baselineReply.roundTripChecks),
      passed: comparison.passed && strictComparison.passed && stability.passed };
    rows.push(row); report.cases = selected.map(name => rows.find(value => value.key === name)).filter(Boolean); report.gaussKronrodDiagnosticPassed = rows.every(value => value.gaussKronrod.passed); save();
    console.log(JSON.stringify({ key, passed: row.passed, comparison, strictComparison, stability, gaussKronrod: row.gaussKronrod }));
    } finally { await native.close(); }
  }));
  assert.equal(digest(driver), report.diagnosticInspectionArtifacts.driverSha256);
  assert.equal(digest(inspector), report.diagnosticInspectionArtifacts.inspectorSha256);
  assert.equal(digest(bridge), report.diagnosticInspectionArtifacts.bridgeSha256);
  assert.equal(digest(faceHelper), report.diagnosticInspectionArtifacts.faceHelperSha256);
  assert.equal(digest(faceHelperImplementation), report.diagnosticInspectionArtifacts.faceHelperImplementationSha256);
  for (const capture of capturedByKey.values()) assert.equal(digest(capture.path), capture.sha256);
  assert.equal(digest(previousGKFile), report.previousGKReport.sha256);
  report.passed = rows.every(row => row.passed); report.finishedAt = new Date().toISOString(); save();
  console.log(JSON.stringify({ passed: report.passed, roots: rows.length,
    noSceneRegeneration: true, completedFullCase: false, reportFile }));
} catch (error) {
  report.error = error.stack; report.finishedAt = new Date().toISOString(); save(); throw error;
} finally {
  await Promise.all(connections.map(handle => handle.close()));
}
