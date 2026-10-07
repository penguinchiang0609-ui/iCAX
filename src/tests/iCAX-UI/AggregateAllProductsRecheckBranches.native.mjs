// Assemble independently completed native cases only after validating their evidence.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const output = resolve(repository, 'output/tests/assembly-process');
const names = ['stair-l-oval', 'stair-l-racetrack', 'guardrail-continuous-contacts',
  'guardrail-stepped-contacts', 'guardrail-multi-u-contacts', 'louver-external-profile-stock'];
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const read = path => JSON.parse(readFileSync(path, 'utf8').replace(/^\uFEFF/, ''));
const records = names.map(name => {
  const path = resolve(output, `all-products-recheck-native-branches-${name}.json`);
  const report = read(path);
  assert.equal(report.schema, 'icax.all-products-recheck-native-branches');
  assert.equal(report.passed, true, `Incomplete native case: ${name}`);
  assert.ok(report.finishedAt);
  assert.equal(report.cases.length, 1);
  assert.equal(report.cases[0].name, name);
  const logPath = resolve(output, `all-products-recheck-native-branches-${name}.log`);
  assert.ok(existsSync(logPath), `Missing native case log: ${name}`);
  return { path, sha256: digest(path), logPath, logSha256: digest(logPath), report };
});
const first = records[0].report;
const migrationFile = resolve(output, 'all-products-recheck-native-point-classification-harness-checkpoint.json');
const migration = read(migrationFile);
assert.equal(migration.schema, 'icax.native-point-classification-harness-checkpoint');
assert.equal(migration.samplingUnchanged, true);
assert.equal(migration.pointsPerRoot, 343);
assert.equal(migration.classifierTolerance, 1e-6);
assert.equal(migration.timeoutMsBefore, 600000);
assert.equal(migration.timeoutMsAfter, 600000);
assert.equal(migration.productionTemplatesOrRuntimeChanged, false);
assert.equal(migration.oldCasesRecordExecutionHarnessHashes, false);
for (const artifact of [migration.oldHelper, migration.newHelper, migration.oldHarness, migration.newHarness]) {
  assert.equal(digest(resolve(repository, artifact.path)), artifact.sha256);
}
const retained = new Map(migration.retainedOldCases.map(row => [row.name, row]));
assert.equal(retained.size, 4);
const artifacts = first.inspectionArtifacts;
const hashes = Object.fromEntries(['bridgeSha256', 'runtimeDllSha256', 'inspectorSourceSha256',
  'routerSha256'].map(key => [key, artifacts[key]]));
for (const [pathKey, hashKey] of [['bridgePath', 'bridgeSha256'], ['runtimeDLL', 'runtimeDllSha256'],
  ['inspectorSource', 'inspectorSourceSha256'], ['routerFile', 'routerSha256']]) {
  assert.equal(digest(artifacts[pathKey]), hashes[hashKey], `Inspection artifact changed: ${pathKey}`);
}
const controls = read(first.poolControls.path);
assert.equal(digest(first.poolControls.path), first.poolControls.sha256);
assert.equal(controls.passed, true);
assert.equal(controls.workerCount, first.workerCount);
assert.equal(controls.cases.length, first.workerCount * 12 + 7);
assert.equal(controls.inspectionArtifactsBeforeAfterUnchanged, true);
assert.equal(controls.bridgeSha256, hashes.bridgeSha256);
assert.equal(controls.runtimeDllSha256, hashes.runtimeDllSha256);
assert.equal(controls.sourceSha256, hashes.inspectorSourceSha256);
assert.equal(controls.routerBeforeSha256, hashes.routerSha256);
assert.equal(controls.routerAfterSha256, hashes.routerSha256);

function validateRouting(routes) {
  assert.ok(routes.length > 0);
  for (const route of routes) {
    assert.deepEqual(route.returnedKeys, route.requestedKeys);
    assert.equal(route.checkedPoints, route.points);
    assert.equal(route.everyRootReturnedOncePerRequest, true);
  }
}
validateRouting(controls.routingEvidence);
for (const { report, path, sha256 } of records) {
  assert.equal(report.runtimeRoot, first.runtimeRoot);
  assert.equal(report.workerCount, first.workerCount);
  assert.equal(report.sourcePhase, first.sourcePhase);
  assert.deepEqual(report.inspectionArtifacts, first.inspectionArtifacts);
  assert.deepEqual(report.poolControls, first.poolControls);
  const result = report.cases[0];
  if (result.harnessArtifacts) {
    const harness = result.harnessArtifacts;
    assert.equal(harness.beforeAfterUnchanged, true);
    assert.equal(digest(harness.assertionFile), harness.assertionSha256);
    assert.equal(digest(harness.harnessFile), harness.harnessSha256);
    assert.equal(harness.assertionSha256, migration.newHelper.sha256);
    assert.equal(harness.harnessSha256, migration.newHarness.sha256);
    assert.equal(result.nativePointClassification.rootsPerRequest, 5);
    assert.equal(result.nativePointClassification.pointsPerRoot, 343);
    assert.equal(result.nativePointClassification.timeoutMs, 600000);
    const stages = read(result.stageEvidence.path);
    assert.equal(digest(result.stageEvidence.path), result.stageEvidence.sha256);
    assert.equal(stages.passed, true);
    assert.equal(stages.name, result.name);
    assert.equal(stages.runtimeRoot, report.runtimeRoot);
    assert.deepEqual(stages.inspectionArtifacts, hashes);
    assert.deepEqual(stages.harnessDigests, {
      assertionSha256: harness.assertionSha256, harnessSha256: harness.harnessSha256,
    });
    assert.deepEqual(stages.stageArtifacts, result.stageEvidence.artifacts);
    assert.equal(stages.hostInput.path, result.hostInputFile);
    assert.equal(stages.sourceReference.path, result.sourceReferenceFile);
    assert.equal(digest(stages.hostInput.path), stages.hostInput.sha256);
    assert.equal(digest(stages.sourceReference.path), stages.sourceReference.sha256);
    const stageNames = stages.stageArtifacts.map(entry => entry.stage);
    for (const required of ['scene-before-shape-comparison', 'world-inputs', 'world-point-results',
      'world-complete', 'legacy-local.neutral-points', 'legacy-local-complete',
      'actual-local.neutral-points', 'actual-local-complete', 'save-reopen-complete']) {
      assert.ok(stageNames.includes(required), `Missing completed stage ${result.name}: ${required}`);
    }
    for (const phase of ['legacy-local', 'actual-local']) {
      assert.equal(stageNames.filter(stage => stage.startsWith(`${phase}.native-points.`)).length,
        Math.ceil(result.partFacts.parts / 5));
    }
    for (const artifact of stages.stageArtifacts) assert.equal(digest(artifact.path), artifact.sha256);
  } else {
    const previous = retained.get(result.name);
    assert.ok(previous, `Unbound original helper evidence: ${result.name}`);
    assert.equal(previous.rerunWithNewHelper, false);
    assert.equal(resolve(repository, previous.report), path);
    assert.equal(previous.sha256, sha256);
    assert.deepEqual(previous.originalInspectionArtifacts, report.inspectionArtifacts);
  }
  for (const key of ['passed', 'exactSaveReopenPreserved', 'hostParametersEqual',
    'nativePreviewAndPlanningAccepted']) assert.equal(result[key], true, `${result.name}: ${key}`);
  assert.equal(result.inspectionArtifacts.beforeAfterUnchanged, true);
  assert.deepEqual(result.inspectionArtifacts.hashes, hashes);
  const shape = result.baselineShapes;
  assert.equal(shape.sharedPublicNormalizerAppliedToBoth, true);
  assert.equal(shape.normalizationHintsFromActualSavedExecution, true);
  for (const layer of [shape, shape.worldExecutionShapes, shape.actualExecutionLocal]) {
    assert.equal(layer.parts, result.partFacts.parts);
    assert.equal(layer.volumesAndBoundsEqual, true);
    assert.equal(layer.solidClassificationsEqual, true);
    for (const side of ['baseline', 'actual']) {
      const checked = layer.classificationReuseControls[side];
      assert.equal(checked.available, true);
      assert.equal(checked.points, layer.parts * 343);
      assert.ok(checked.controls > 0);
      assert.equal(checked.everyEncounteredStateComparedWithFreshClassifier, true);
    }
  }
  const cache = result.inspectionCache;
  assert.ok(cache.calls.length && cache.hits > 0 && cache.misses > 0);
  assert.equal(cache.completeDocumentAndRootKeys, true);
  assert.equal(cache.privateUnnormalizedCopies, true);
  validateRouting(result.routingEvidence);
  const reference = read(result.sourceReferenceFile), host = read(result.hostInputFile);
  assert.equal(reference.templateId, result.templateId);
  assert.equal(host.templateId, result.templateId);
  assert.deepEqual(reference.parameters, result.parameters);
  assert.deepEqual(reference.document.parameters, result.parameters);
  assert.deepEqual(host.parameters, result.parameters);
  for (const [path, expected] of Object.entries(reference.sourceDigests)) {
    assert.equal(digest(resolve(repository, 'src', path)), expected, `Producer source changed: ${path}`);
    assert.equal(digest(resolve(report.runtimeRoot, path)), expected, `Deployed producer changed: ${path}`);
  }
}

const reportFile = resolve(output, 'all-products-recheck-native-branches.json');
const previous = resolve(output, 'all-products-recheck-native-branches-before-takeover.json');
if (existsSync(reportFile) && !existsSync(previous)) copyFileSync(reportFile, previous);
const previousLog = resolve(output, 'all-products-recheck-native-branches-before-takeover.log');
const reportLog = resolve(output, 'all-products-recheck-native-branches.log');
if (existsSync(reportLog) && !existsSync(previousLog)) copyFileSync(reportLog, previousLog);
const aggregate = { schema: first.schema, schemaVersion: first.schemaVersion,
  runtimeRoot: first.runtimeRoot, prepareOnly: false, workerCount: first.workerCount,
  sourcePhase: first.sourcePhase, inspectionArtifacts: first.inspectionArtifacts,
  poolControls: first.poolControls,
  harnessMigration: { path: migrationFile, sha256: digest(migrationFile),
    oldHelperCases: [...retained.keys()],
    newHelperCases: records.filter(({ report }) => report.cases[0].harnessArtifacts)
      .map(({ report }) => report.cases[0].name) },
  startedAt: records.map(({ report }) => report.startedAt).sort()[0],
  finishedAt: records.map(({ report }) => report.finishedAt).sort().at(-1),
  aggregatedAt: new Date().toISOString(),
  independentCaseReports: records.map(({ path, sha256, logPath, logSha256 }) =>
    ({ path, sha256, logPath, logSha256 })),
  cases: records.map(({ report }) => report.cases[0]), passed: true };
writeFileSync(reportFile, JSON.stringify(aggregate, null, 2) + '\n');
const summary = { passed: true, cases: aggregate.cases.length, reportFile,
  aggregatedAt: aggregate.aggregatedAt, independentCaseReports: aggregate.independentCaseReports };
writeFileSync(reportLog, JSON.stringify(summary, null, 2) + '\n');
console.log(JSON.stringify(summary));
