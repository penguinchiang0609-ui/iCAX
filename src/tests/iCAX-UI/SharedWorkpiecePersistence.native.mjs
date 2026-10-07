// Focused persistence repair check. The unchanged geometry proof stays in its
// original report; this stage freshly generates, disassembles and saves/reopens.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, readdirSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inspectManufacturedShapes } from './productManufacturingShapeAssertions.mjs';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const binaryRoot = resolve(repository, 'src/x64/Debug');
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || binaryRoot);
const bridgePath = resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
  || resolve(repository, 'tmp/security-window-frames-native-recheck-fast/SecurityWindowFramesBridge.exe'));
const output = resolve(repository, 'output/tests/assembly-process');
const stem = resolve(output, 'all-products-recheck-native-shared-workpieces-round-scene-persistence');
const proofFile = resolve(output, 'all-products-recheck-native-shared-workpieces.binary-project-string-failure.json');
const sourceFile = resolve(output, 'all-products-recheck-native-shared-workpieces-door-uncalled-round_scene-two_blocks-source-reference.json');
const snapshotFile = resolve(output, 'all-products-recheck-native-shared-workpieces-door-uncalled-round_scene-two_blocks-native-snapshot.json');
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const proof = JSON.parse(readFileSync(proofFile, 'utf8')).cases
  .find(entry => entry.name === 'door-uncalled-round_scene-two_blocks');
assert.ok(proof && !proof.passed && /Binary project string is too large/.test(proof.error));
assert.ok(proof.baselineShapes.volumesAndBoundsEqual && proof.baselineShapes.solidClassificationsEqual
  && proof.baselineShapes.sharedPublicNormalizerAppliedToBoth);
assert.ok(proof.classificationReuseControls.everyObservedStateCheckedAgainstFreshClassifier);
const reference = JSON.parse(readFileSync(sourceFile, 'utf8'));
const previousSnapshot = JSON.parse(readFileSync(snapshotFile, 'utf8')).snapshot;
const modules = readdirSync(binaryRoot).filter(name => /\.dll$/i.test(name)).map(file => ({
  file, originalSha256: hash(resolve(binaryRoot, file)), actualSha256: hash(resolve(runtimeRoot, file)),
}));
for (const module of modules.filter(entry => entry.file !== 'ProjectFile.dll'))
  assert.equal(module.actualSha256, module.originalSha256, `Only ProjectFile.dll may change: ${module.file}`);
for (const module of proof.dllHashes)
  assert.equal(hash(resolve(runtimeRoot, module.file)), module.sha256, `Unchanged geometry module ${module.file}`);
const projectFileModule = modules.find(entry => entry.file === 'ProjectFile.dll');
assert.ok(projectFileModule && projectFileModule.actualSha256 !== projectFileModule.originalSha256,
  'Use the isolated runtime with the repaired ProjectFile.dll');
// The captured door proof predates the independently verified window resource
// correction. Accept only that recorded transition, and still compare every
// freshly generated native door document against the original capture below.
const sourceBeforeFile = resolve(output, 'all-products-recheck-source-freeze-before-polygon-resource-fix.json');
const sourceAfterFile = resolve(output, 'all-products-recheck-source-freeze.json');
const correctionFile = resolve(output, 'all-products-recheck-polygon-resource-checkpoint.json');
const sourceBefore = JSON.parse(readFileSync(sourceBeforeFile, 'utf8')).sources;
const sourceAfter = JSON.parse(readFileSync(sourceAfterFile, 'utf8')).sources;
const correction = JSON.parse(readFileSync(correctionFile, 'utf8'));
const changedDependencies = [];
for (const [path, digest] of Object.entries(reference.sourceDigests)) {
  const sourcePath = `src/${path.replaceAll('\\', '/')}`;
  assert.equal(sourceBefore[sourcePath], digest, `Captured source checkpoint ${path}`);
  assert.ok(sourceAfter[sourcePath], `Frozen source dependency ${path}`);
  const currentDigest = hash(resolve(repository, 'src', path));
  assert.equal(currentDigest, sourceAfter[sourcePath], `Final frozen source ${path}`);
  if (currentDigest !== digest) changedDependencies.push(sourcePath);
  const installed = path.replace(/^iCAX-Engine\/framework\/TemplateRuntime\/python\//, 'runtime/template-python/');
  assert.equal(hash(resolve(runtimeRoot, installed)), currentDigest, `Final installed source ${installed}`);
}
assert.deepEqual(changedDependencies, ['src/apps/tube-designer/templates/_shared/security_window_product_contract.py']);
assert.deepEqual(correction.changedSourceFiles, changedDependencies);
assert.ok(correction.componentContoursPreserved && correction.genericCurveVerticesAndWindingPreserved
  && correction.bothRenderAndSectionResourceConverted);
const sourceTransition = { changedDependencies,
  before: { file: sourceBeforeFile, sha256: hash(sourceBeforeFile) },
  after: { file: sourceAfterFile, sha256: hash(sourceAfterFile) },
  correction: { file: correctionFile, sha256: hash(correctionFile) },
  everySourceAndInstalledDependencyMatchesFinalFreeze: true };
const digestTransitionFile = resolve(output, 'takeover-native-package-digest-transition.json');
const digestTransition = JSON.parse(readFileSync(digestTransitionFile, 'utf8'));
assert.ok(digestTransition.onlyRecordedWindowSourceDependencyChanged);
assert.deepEqual(digestTransition.changedDependencies, changedDependencies);
assert.equal(digestTransition.allowedMetadataPath, 'manufacturingExecution.document.template.packageDigest');
assert.equal(digestTransition.beforePackageDigestRecomputed, digestTransition.beforePackageDigest);
assert.equal(digestTransition.currentPackageDigestRecomputed, digestTransition.afterPackageDigest);
assert.equal(digestTransition.sourceBefore.sha256, hash(sourceBeforeFile));
assert.equal(digestTransition.sourceAfter.sha256, hash(sourceAfterFile));
for (const key of ['oldNativeSnapshot', 'newNativeSnapshot', 'algorithmSource'])
  assert.equal(hash(resolve(repository, digestTransition[key].file)), digestTransition[key].sha256);
for (const asset of digestTransition.algorithm.nonFreezeLocalAssetsUsedInBothRecomputations) {
  assert.equal(hash(resolve(repository, asset.file)), asset.sha256);
  assert.equal(hash(resolve(runtimeRoot, asset.file.replace(/^src\//, ''))), asset.sha256);
}
assert.equal(previousSnapshot.manufacturingExecution.document.template.packageDigest, digestTransition.beforePackageDigest);
const previousCapturedForComparison = { ...previousSnapshot,
  manufacturingExecution: { ...previousSnapshot.manufacturingExecution,
    document: { ...previousSnapshot.manufacturingExecution.document,
      template: { ...previousSnapshot.manufacturingExecution.document.template,
        packageDigest: digestTransition.afterPackageDigest } } } };
const report = { schema: 'icax.native-focused-persistence-recheck', schemaVersion: 1,
  case: proof.name, stage: 'project-file-properties-limit-repair', startedAt: new Date().toISOString(),
  runtimeRoot, bridgePath, bridgeSha256: hash(bridgePath), modules,
  previousFullGeometryProof: { file: proofFile, sha256: hash(proofFile),
    sourceReferenceFile: sourceFile, sourceReferenceSha256: hash(sourceFile),
    actualSnapshotFile: snapshotFile, actualSnapshotSha256: hash(snapshotFile),
    baselineShapes: proof.baselineShapes, classificationReuseControls: proof.classificationReuseControls,
    nativeGeometryModulesUnchanged: true, sourceTransition },
  packageDigestTransition: { file: digestTransitionFile, sha256: hash(digestTransitionFile),
    before: digestTransition.beforePackageDigest, after: digestTransition.afterPackageDigest,
    allowedMetadataPath: digestTransition.allowedMetadataPath },
  full343PointGeometryComparisonRepeated: false,
  previousSnapshotCaptureEncoding: 'JSON.stringify then JSON.parse, matching the original snapshot file; signed zero is normalized only for this old-capture comparison.',
  liveSaveReopenSignedZeroComparedExactly: true, apiTimings: [], passed: false };
const save = () => writeFileSync(`${stem}.json`, JSON.stringify(report, null, 2));
// AssertionError formats whole unequal documents. These geometry recipes can
// span tens of megabytes, so retain strict comparison with a bounded diagnostic.
function firstDifference(actual, expected, path = '$') {
  if (Object.is(actual, expected)) return null;
  const describe = value => value === null ? null
    : typeof value === 'object' ? (Array.isArray(value) ? `array(${value.length})` : 'object')
    : typeof value === 'string' && value.length > 160 ? `${value.slice(0, 160)}…` : value;
  if (actual === null || expected === null || typeof actual !== 'object' || typeof expected !== 'object'
    || Array.isArray(actual) !== Array.isArray(expected))
    return { path, actual: describe(actual), expected: describe(expected) };
  if (Array.isArray(actual)) {
    if (actual.length !== expected.length) return { path: `${path}.length`, actual: actual.length, expected: expected.length };
    for (let index = 0; index < actual.length; index += 1) {
      const difference = firstDifference(actual[index], expected[index], `${path}[${index}]`);
      if (difference) return difference;
    }
    return null;
  }
  const actualKeys = Object.keys(actual).sort(), expectedKeys = Object.keys(expected).sort();
  if (actualKeys.length !== expectedKeys.length)
    return { path: `${path}.keys.length`, actual: actualKeys.length, expected: expectedKeys.length };
  for (let index = 0; index < actualKeys.length; index += 1) {
    if (actualKeys[index] !== expectedKeys[index])
      return { path: `${path}.keys[${index}]`, actual: actualKeys[index], expected: expectedKeys[index] };
    const key = actualKeys[index], difference = firstDifference(actual[key], expected[key], `${path}.${key}`);
    if (difference) return difference;
  }
  return null;
}
function assertExact(actual, expected, label) {
  const difference = firstDifference(actual, expected);
  if (difference) {
    report.documentComparisonFailure = { label, ...difference };
    save();
    throw new Error(`${label}: ${JSON.stringify(difference)}`);
  }
}
save();
const child = spawn(bridgePath, [], { cwd: runtimeRoot, windowsHide: true,
  env: { ...process.env, PATH: `${runtimeRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'] });
const pending = new Map();
let sequence = 0, stderr = '';
const fail = error => { for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); } pending.clear(); };
child.stderr.on('data', value => { stderr += value; });
child.on('error', fail);
child.on('exit', code => { if (pending.size) fail(new Error(`Native exited ${code}: ${stderr.slice(0, 300)}`)); });
createInterface({ input: child.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { fail(new Error(line.slice(0, 300))); return; }
  const request = pending.get(response.id);
  if (!request) return;
  pending.delete(response.id); clearTimeout(request.timer);
  Object.assign(request.timing, { finishedAt: new Date().toISOString(), elapsedMilliseconds: Date.now() - request.started, ok: response.ok });
  console.log(JSON.stringify({ stage: 'native-end', ...request.timing })); save();
  response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
});
const native = { invoke(method, payload = {}) { return new Promise((resolveRequest, reject) => {
  const id = ++sequence, started = Date.now();
  const timing = { id, method, startedAt: new Date(started).toISOString(), waitLimitMilliseconds: 1800000 };
  report.apiTimings.push(timing);
  console.log(JSON.stringify({ stage: 'native-begin', ...timing })); save();
  const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Test wait reached ${method}: ${stderr.slice(0, 300)}`)); }, 1800000);
  pending.set(id, { resolve: resolveRequest, reject, timer, started, timing });
  child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
}); } };

function savedProjectFiles() {
  const directory = resolve(runtimeRoot, 'tmp/security-window-frames-native');
  if (!existsSync(directory)) return [];
  return readdirSync(directory)
    .filter(name => /^bridge-project-.*\.ictd$/.test(name));
}

function binaryProjectStatistics(bytes) {
  let position = bytes.indexOf(0) + 1;
  assert.equal(bytes.subarray(position, position + 8).toString(), 'ICAXPBIN'); position += 8;
  const uint64 = () => { const value = Number(bytes.readBigUInt64LE(position)); position += 8; assert.ok(Number.isSafeInteger(value)); return value; };
  const string = () => { const size = uint64(), value = bytes.subarray(position, position + size); assert.equal(value.length, size); position += size; return value.toString(); };
  position += 4; string(); string(); position += 4 + 32; string(); string(); string();
  const count = uint64(), componentProperties = [], resourceBodies = [];
  for (let index = 0; index < count; index += 1) {
    const kind = bytes[position++], size = uint64(), end = position + size;
    assert.ok(end <= bytes.length);
    if (kind === 2) {
      position += 16; const componentClass = string(); position += 1;
      const propertyBytes = uint64();
      assert.equal(position + propertyBytes, end);
      componentProperties.push({ componentClass, typedPropertyBytes: propertyBytes,
        sha256: createHash('sha256').update(bytes.subarray(position, end)).digest('hex') });
    } else if (kind === 5) {
      const url = string(), version = uint64(), bodyBytes = uint64();
      assert.equal(position + bodyBytes, end);
      resourceBodies.push({ url, version, bytes: bodyBytes,
        sha256: createHash('sha256').update(bytes.subarray(position, end)).digest('hex') });
    }
    position = end;
  }
  assert.equal(position, bytes.length);
  return { fileBytes: bytes.length, recordCount: count, componentProperties, resourceBodies };
}

try {
  const generated = await native.invoke('GeneratePreview', { templateId: reference.templateId, ...reference.parameters });
  const productEntityId = generated.tubeDesigner.product.entityId;
  const before = await native.invoke('InspectScriptResources');
  assertExact(before.display.document, reference.display, 'Generated display');
  assertExact(before.generatedParameters, reference.parameters, 'Generated parameters');
  assert.deepEqual(before.manufacturing, {}); assert.deepEqual(before.manufacturingExecution, {});
  await native.invoke('Disassemble', { productEntityIds: [productEntityId] });
  const snapshot = await native.invoke('InspectScriptResources');
  writeFileSync(`${stem}-fresh-native-snapshot.json`, JSON.stringify({ snapshot }));
  // The original native snapshot was captured with JSON.stringify, which loses
  // the sign of zero. Match its storage representation for this comparison.
  // Keep the untouched live snapshot for both real save/reopen comparisons.
  const capturedSnapshot = JSON.parse(JSON.stringify(snapshot));
  assertExact(snapshot.manufacturingExecution.document.template.packageDigest,
    digestTransition.afterPackageDigest, 'Current frozen native package digest');
  for (const key of ['display', 'manufacturing', 'manufacturingExecution', 'generatedParameters', 'manufacturingParameters', 'resolvedComponentModels'])
    assertExact(capturedSnapshot[key], previousCapturedForComparison[key], `Previous captured native ${key} snapshot with the proven package digest transition`);
  const shapes = await inspectManufacturedShapes(native);
  assert.equal(shapes.geometryChecks.length, 2);
  writeFileSync(`${stem}-before-save.json`, JSON.stringify({ snapshot, shapes }));
  const filesBefore = new Set(savedProjectFiles());
  await native.invoke('SaveAndReopen');
  assertExact(await native.invoke('InspectScriptResources'), snapshot, 'First reopened native documents');
  assertExact(await native.invoke('InspectNativeGeometry'), shapes,
    'Saved/reopened persisted native BRep hash, bytes, placement, validity, volume and bounds remain exact');
  const newFiles = savedProjectFiles().filter(name => !filesBefore.has(name));
  assert.equal(newFiles.length, 1);
  const savedPath = resolve(runtimeRoot, 'tmp/security-window-frames-native', newFiles[0]);
  const firstBytes = readFileSync(savedPath);
  const statistics = binaryProjectStatistics(firstBytes);
  const generation = statistics.componentProperties.filter(entry => entry.componentClass === 'CGenerationRunComponent');
  assert.equal(generation.length, 1);
  assert.ok(generation[0].typedPropertyBytes > 64 * 1024 * 1024 && generation[0].typedPropertyBytes < 256 * 1024 * 1024);
  await native.invoke('SaveAndReopen');
  const secondBytes = readFileSync(savedPath);
  assert.ok(secondBytes.equals(firstBytes), 'Real project binary bytes must be unchanged by save/reopen/save');
  assertExact(await native.invoke('InspectScriptResources'), snapshot, 'Second reopened native documents');
  assertExact(await native.invoke('InspectNativeGeometry'), shapes, 'Second reopened native geometry');
  report.actualPersistence = { newlyGeneratedAndDisassembled: true,
    oldNativeDocumentsPreservedExceptRecordedPackageDigest: true,
    originalPackageDigest: digestTransition.beforePackageDigest,
    currentPackageDigest: digestTransition.afterPackageDigest,
    realSaveReopenCount: 2, frozenSnapshotEqual: true, persistedNativeGeometryEqual: true,
    entireSavedProjectBytesEqualAcrossSecondSave: true, savedPath,
    savedProjectSha256: createHash('sha256').update(firstBytes).digest('hex'), statistics };
  report.passed = true;
} catch (error) {
  report.error = error.stack;
  console.error(JSON.stringify({ passed: false, error: error.message.slice(0, 300) }));
  process.exitCode = 1;
} finally {
  await new Promise(resolveClosed => {
    if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
    child.once('exit', resolveClosed); child.stdin.end(); child.kill();
  });
  report.finishedAt = new Date().toISOString(); save();
  console.log(JSON.stringify({ passed: report.passed, reportFile: `${stem}.json` }));
}
