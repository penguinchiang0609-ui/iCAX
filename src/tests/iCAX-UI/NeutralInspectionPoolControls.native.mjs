// Controls for the isolated inspection-only BRep cache; no product or runtime source changes.
import assert from 'node:assert/strict';
import { createCachedNeutralInspectionPool, rootWorkerIndex } from './CachedNeutralInspectionPool.mjs';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || resolve(repository, 'src/x64/Debug'));
const bridgePath = resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
  || resolve(repository, 'tmp/security-window-frames-native-recheck-cached/SecurityWindowFramesBridge.exe'));
const output = resolve(repository, 'output/tests/assembly-process');
mkdirSync(output, { recursive: true });
const controlWorkerCount = Number(process.env.ICAX_NATIVE_NEUTRAL_WORKERS || 3);
const reportFile = resolve(output, `all-products-recheck-neutral-pool-controls${controlWorkerCount === 3 ? '' : `-workers-${controlWorkerCount}`}.json`);
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const report = { schema: 'icax.neutral-inspection-pool-controls', schemaVersion: 1,
  startedAt: new Date().toISOString(), runtimeRoot, bridgePath,
  bridgeSha256: digest(bridgePath), runtimeDllSha256: digest(resolve(runtimeRoot, 'TemplateRuntime.dll')),
  sourceSha256: digest(resolve(repository,
    'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/SecurityWindowFramesCachedInspectionBridge.cpp')),
  cases: [], passed: false, workerCount: controlWorkerCount };
const save = () => writeFileSync(reportFile, JSON.stringify(report, null, 2));

const model = {
  schema: 'icax.display-model', schemaVersion: 2,
  coordinateSystem: 'right-handed-x-width-y-depth-z-height', lengthUnit: 'mm',
  resources: [
    { key: 'same.section', operator: 'profile2d', inputs: [], arguments: {
      placement: { origin: [15, -10, 5], xAxis: [1, 0, 0], yAxis: [0, 1, 0] },
      contours: [{ kind: 'path', closed: true, segments: [
        { kind: 'line', start: [0, 0], end: [10, 0] },
        { kind: 'line', start: [10, 0], end: [10, 20] },
        { kind: 'line', start: [10, 20], end: [0, 20] },
        { kind: 'line', start: [0, 20], end: [0, 0] },
      ] }] } },
    { key: 'same.root', operator: 'extrude', inputs: ['same.section'], arguments: { vector: [0, 0, 30] } },
    { key: 'second.root', operator: 'extrude', inputs: ['same.section'], arguments: { vector: [0, 0, 12] } },
  ],
  items: [
    { key: 'part.main', displayName: 'Cache control main', geometry: { resource: 'same.root' }, children: [], properties: {} },
    { key: 'part.second', displayName: 'Cache control second', geometry: { resource: 'second.root' }, children: [], properties: {} },
  ], roots: ['part.main', 'part.second'], annotations: [],
};
const frozen = structuredClone(model);
const fractions = [0.017, 0.05, 0.25, 0.5, 0.75, 0.95, 0.983];
const pointsFor = (key, bounds) => fractions.flatMap(x => fractions.flatMap(y => fractions.map(z => ({
  geometryKey: key, point: [x, y, z].map((fraction, axis) => bounds[axis * 2]
    + fraction * (bounds[axis * 2 + 1] - bounds[axis * 2])) }))));
const basePoints = [...pointsFor('same.root', [15, 25, -10, 10, 5, 35]),
  { geometryKey: 'same.root', point: [20, 0, 40] }];
const equalKnownBounds = (actual, expected) => expected.forEach((coordinate, index) =>
  assert.ok(Math.abs(actual[index] - coordinate) <= 1e-5, `Known box bound ${index}`));
const reversedKeys = value => Array.isArray(value) ? value.map(reversedKeys)
  : value && typeof value === 'object'
    ? Object.fromEntries(Object.keys(value).reverse().map(key => [key, reversedKeys(value[key])])) : value;
const keyFor = (prefix, target) => {
  let suffix = 0;
  while (rootWorkerIndex(`${prefix}.${suffix}`, report.workerCount) !== target) suffix += 1;
  return `${prefix}.${suffix}`;
};
const rename = (value, names) => Array.isArray(value) ? value.map(child => rename(child, names))
  : value && typeof value === 'object' ? Object.fromEntries(Object.entries(value).map(([key, child]) =>
    [names[key] || key, rename(child, names)])) : typeof value === 'string' ? names[value] || value : value;

const workerEvents = [];
const native = createCachedNeutralInspectionPool({ bridgePath, runtimeRoot, workerCount: report.workerCount,
  onEvent: event => workerEvents.push(event) });
report.workers = native.workerMetadata;
report.router = native.router;
report.routerBeforeSha256 = native.router.sha256;
try {
  for (let targetWorker = 0; targetWorker < report.workerCount; targetWorker += 1) {
  const names = { 'same.section': `worker.${targetWorker}.section`,
    'same.root': keyFor(`worker.${targetWorker}.main`, targetWorker),
    'second.root': keyFor(`worker.${targetWorker}.second`, targetWorker) };
  const reverseNames = Object.fromEntries(Object.entries(names).map(([key, value]) => [value, key]));
  async function inspect(name, input, roots, points, expected, extra = {}) {
    const request = rename({ model: input, geometryKeys: roots, points, ...extra }, names);
    const before = structuredClone(request), started = Date.now();
    const result = await native.invoke('InspectNeutralModel', request);
    assert.deepEqual(request, before);
    assert.ok(result.routing.workers.every(worker => worker.workerIndex === targetWorker));
    assert.deepEqual(result.routing.returnedKeys, request.geometryKeys);
    assert.deepEqual(result.inspectionCache, { keyKind: 'complete-standard-json-document-and-root',
      ...expected, unnormalizedBRepCached: true, privateShapeCopyBeforeInspection: true });
    for (const check of result.geometryChecks) {
      assert.equal(check.valid, true); assert.equal(check.solids, 1); assert.ok(check.volume > 0);
      assert.equal(check.pointChecks.length, request.points.filter(point => point.geometryKey === check.geometryKey).length);
      const states = new Set(check.pointChecks.map(point => point.boundary ? 'boundary'
        : point.inside ? 'inside' : point.outside ? 'outside' : 'unknown'));
      assert.equal(check.classificationReuseControls, states.size);
    }
    report.cases.push({ name: `worker-${targetWorker}:${name}`, seconds: (Date.now() - started) / 1000, ...result });
    save(); console.log(JSON.stringify({ name, targetWorker, cache: result.inspectionCache, passed: true }));
    return rename(result.geometryChecks, reverseNames);
  }
  const base = await inspect('first-complete-model-miss', model, ['same.root'], basePoints,
    { hits: 0, misses: 1, models: 1, roots: 1 });
  equalKnownBounds(base[0].bounds, [15, 25, -10, 10, 5, 35]);
  assert.ok(Math.abs(base[0].volume - 6000) < 1e-6);
  assert.equal(base[0].pointChecks.at(-1).outside, true);
  const repeat = await inspect('same-model-reordered-json-positive-hit', reversedKeys(model), ['same.root'], basePoints,
    { hits: 1, misses: 0, models: 1, roots: 1 });
  assert.deepEqual(repeat, base);

  const changed = structuredClone(model); changed.resources[1].arguments.vector[2] = 45;
  const modified = await inspect('same-root-changed-geometry-negative-isolation', changed, ['same.root'], basePoints,
    { hits: 0, misses: 1, models: 2, roots: 2 });
  equalKnownBounds(modified[0].bounds, [15, 25, -10, 10, 5, 50]);
  assert.ok(Math.abs(modified[0].volume - 9000) < 1e-6);
  assert.equal(modified[0].pointChecks.at(-1).inside, true);
  assert.notDeepEqual(modified, base);
  const returned = await inspect('original-document-after-changed-geometry', model, ['same.root'], basePoints,
    { hits: 1, misses: 0, models: 2, roots: 2 });
  assert.deepEqual(returned, base);

  const secondPoints = pointsFor('second.root', [15, 25, -10, 10, 5, 17]);
  const second = await inspect('same-document-second-root-miss', model, ['second.root'], secondPoints,
    { hits: 0, misses: 1, models: 2, roots: 3 });
  assert.ok(Math.abs(second[0].volume - 2400) < 1e-6);
  const combined = await inspect('complete-model-two-root-positive-hits', model, ['same.root', 'second.root'],
    [...basePoints, ...secondPoints], { hits: 2, misses: 0, models: 2, roots: 3 });
  assert.deepEqual(combined, [...base, ...second]);

  const localOptions = { manufacturingCoordinates: true,
    manufacturingProperties: { 'same.root': { 'tubeDesigner.manufacturingAxis': [0, 0, 1], 'manufacturing.partKind': 'tube' } } };
  const normalized = await inspect('actual-normalizer-on-private-copy', model, ['same.root'], [],
    { hits: 1, misses: 0, models: 2, roots: 3 }, localOptions);
  assert.equal(normalized[0].coordinateSpace, 'manufacturing-local');
  assert.ok(Math.abs(normalized[0].volume - base[0].volume) < 1e-6);
  assert.notDeepEqual(normalized[0].bounds, base[0].bounds);
  const localPoints = pointsFor('same.root', normalized[0].bounds);
  const local = await inspect('normalized-copy-all-343-classifications', model, ['same.root'], localPoints,
    { hits: 1, misses: 0, models: 2, roots: 3 }, localOptions);
  const localRepeat = await inspect('normalized-copy-repeat-positive-hit', model, ['same.root'], localPoints,
    { hits: 1, misses: 0, models: 2, roots: 3 }, localOptions);
  assert.deepEqual(localRepeat, local);
  const afterLocal = await inspect('world-brep-unchanged-after-normalization', model, ['same.root'], basePoints,
    { hits: 1, misses: 0, models: 2, roots: 3 });
  assert.deepEqual(afterLocal, base);

  const metadata = structuredClone(model); metadata.items[0].displayName = 'Different complete document';
  const withMetadata = await inspect('nongeometry-document-content-isolated', metadata, ['same.root'], basePoints,
    { hits: 0, misses: 1, models: 3, roots: 4 });
  assert.deepEqual(withMetadata, base);
  const finalOriginal = await inspect('original-cache-remains-independent', model, ['same.root'], basePoints,
    { hits: 1, misses: 0, models: 3, roots: 4 });
  assert.deepEqual(finalOriginal, base); assert.deepEqual(model, frozen);
  }
  // Exercise simultaneous routing with interleaved roots, not only each worker in isolation.
  const merged = { ...structuredClone(model), resources: [], items: [], roots: [] };
  const mergedKeys = [];
  for (let workerIndex = 0; workerIndex < report.workerCount; workerIndex += 1) {
    const names = { 'same.section': `merged.${workerIndex}.section`,
      'same.root': keyFor(`merged.${workerIndex}.main`, workerIndex),
      'second.root': keyFor(`merged.${workerIndex}.second`, workerIndex),
      'part.main': `merged.${workerIndex}.part.main`, 'part.second': `merged.${workerIndex}.part.second` };
    const copy = rename(model, names);
    merged.resources.push(...copy.resources); merged.items.push(...copy.items); merged.roots.push(...copy.roots);
    mergedKeys.unshift(names['same.root']);
  }
  const mergedPoints = mergedKeys.flatMap(key => [...pointsFor(key, [15, 25, -10, 10, 5, 35]),
    { geometryKey: key, point: [20, 0, 40] }]);
  async function mergedInspect(name, input, points = mergedPoints, extra = {}) {
    const request = { model: input, geometryKeys: mergedKeys, points, ...extra }, before = structuredClone(input);
    const result = await native.invoke('InspectNeutralModel', request);
    assert.deepEqual(input, before);
    assert.deepEqual(result.geometryChecks.map(check => check.geometryKey), mergedKeys);
    assert.deepEqual(result.routing.requestedKeys, mergedKeys);
    assert.deepEqual(result.routing.returnedKeys, mergedKeys);
    assert.equal(result.routing.workers.length, report.workerCount);
    assert.deepEqual(result.routing.workers.map(worker => worker.workerIndex).sort(),
      Array.from({ length: report.workerCount }, (_, index) => index));
    for (const check of result.geometryChecks) {
      assert.equal(check.valid, true); assert.equal(check.solids, 1);
      assert.equal(check.pointChecks.length, points.filter(point => point.geometryKey === check.geometryKey).length);
      const states = new Set(check.pointChecks.map(point => point.boundary ? 'boundary'
        : point.inside ? 'inside' : point.outside ? 'outside' : 'unknown'));
      assert.equal(check.classificationReuseControls, states.size);
    }
    report.cases.push({ name, ...result }); save(); return result;
  }
  const mixed = await mergedInspect('interleaved-all-workers-complete-order-and-points', merged);
  assert.equal(mixed.inspectionCache.misses, report.workerCount);
  const mixedAgain = await mergedInspect('interleaved-all-workers-positive-cache-hit', merged);
  assert.equal(mixedAgain.inspectionCache.hits, report.workerCount);
  assert.deepEqual(mixedAgain.geometryChecks, mixed.geometryChecks);
  const mergedChanged = structuredClone(merged);
  mergedChanged.resources.find(node => node.key === mergedKeys[0]).arguments.vector[2] = 45;
  const mixedChanged = await mergedInspect('same-keys-full-document-change-isolated-on-every-worker', mergedChanged);
  assert.equal(mixedChanged.inspectionCache.misses, report.workerCount);
  assert.equal(mixedChanged.geometryChecks[0].pointChecks.at(-1).inside, true);
  for (let index = 1; index < mergedKeys.length; index += 1)
    assert.deepEqual(mixedChanged.geometryChecks[index], mixed.geometryChecks[index]);
  const reverted = await mergedInspect('all-workers-original-world-survives-different-document', merged);
  assert.equal(reverted.inspectionCache.hits, report.workerCount); assert.deepEqual(reverted.geometryChecks, mixed.geometryChecks);
  const localOptions = { manufacturingCoordinates: true, manufacturingProperties: Object.fromEntries(mergedKeys.map(key =>
    [key, { 'tubeDesigner.manufacturingAxis': [0, 0, 1], 'manufacturing.partKind': 'tube' }])) };
  const localBounds = await mergedInspect('all-workers-normalize-private-copies', merged, [], localOptions);
  const localPoints = localBounds.geometryChecks.flatMap(check => pointsFor(check.geometryKey, check.bounds));
  const localSamples = await mergedInspect('all-workers-all-343-local-points', merged, localPoints, localOptions);
  assert.equal(localSamples.routing.checkedPoints, report.workerCount * 343);
  const restored = await mergedInspect('all-workers-world-unchanged-after-local-copies', merged);
  assert.deepEqual(restored.geometryChecks, mixed.geometryChecks);
  report.routingEvidence = native.routingEvidence; report.workerEvents = workerEvents;
  report.routerAfterSha256 = digest(native.router.path);
  assert.equal(report.routerAfterSha256, report.routerBeforeSha256);
  assert.equal(digest(bridgePath), report.bridgeSha256);
  assert.equal(digest(resolve(runtimeRoot, 'TemplateRuntime.dll')), report.runtimeDllSha256);
  assert.equal(digest(resolve(repository,
    'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/SecurityWindowFramesCachedInspectionBridge.cpp')), report.sourceSha256);
  report.inspectionArtifactsBeforeAfterUnchanged = native.verifySourceUnchanged();
  report.passed = true; report.hostInputsUnchanged = true;
  report.sameKeyGeometryIsolation = true; report.normalizedInspectionCannotMutateCachedWorld = true;
} catch (error) {
  report.error = error.stack; throw error;
} finally {
  report.finishedAt = new Date().toISOString(); save(); await native.close();
  console.log(JSON.stringify({ passed: report.passed, cases: report.cases.length, reportFile }));
}
