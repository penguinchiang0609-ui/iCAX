// Targeted nondefault branches: the native host and persisted BReps use the same shared design.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compareBaselineShapes, inspectManufacturedShapes } from './productManufacturingShapeAssertions.mjs';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const binaryRoot = resolve(repository, 'src/x64/Debug');
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || binaryRoot);
const output = resolve(repository, 'output/tests/assembly-process');
const prepareOnly = process.argv.includes('--prepare-only');
const scenarios = [
  ...['oval', 'racetrack'].map(kind => ({ name: `stair-l-${kind}`, directory: 'straight_steel_staircase',
    changes: { totalRiserCount: 6, floorHeight: 1080, firstFlightRiserCount: 3, stairRoute: 'l_turn',
      handrailProfileType: kind, postProfileType: kind, infillProfileType: kind,
      handrailWidth: 60, handrailDepth: 40, postWidth: 60, postDepth: 40,
      infillWidth: 25, infillDepth: 15 } })),
  { name: 'guardrail-continuous-contacts', directory: 'modular_guardrail',
    changes: { pathMode: 'continuous', slopeAngle: 30, handrailMode: 'continuous',
      sideLength1: 1200, maximumPostSpacing: 600 } },
  { name: 'guardrail-stepped-contacts', directory: 'modular_guardrail',
    changes: { pathMode: 'stepped', slopeAngle: 30, sideLength1: 1200, maximumPostSpacing: 600 } },
  { name: 'guardrail-multi-u-contacts', directory: 'modular_guardrail',
    changes: { layout: 'u', pathMode: 'continuous', slopeAngle: 30, slopeAngle2: 0, slopeAngle3: -20,
      sideLength1: 1200, sideLength2: 900, sideLength3: 900, maximumPostSpacing: 600 } },
];
const selected = scenarios.filter(scenario => !process.env.ICAX_PRODUCT_BRANCH_CASE
  || process.env.ICAX_PRODUCT_BRANCH_CASE === scenario.name);
assert.ok(selected.length > 0, 'Unknown ICAX_PRODUCT_BRANCH_CASE');
mkdirSync(output, { recursive: true });
const reportFile = resolve(output, prepareOnly ? 'all-products-recheck-native-branches-prepared.json'
  : `all-products-recheck-native-branches${process.env.ICAX_PRODUCT_BRANCH_CASE ? `-${process.env.ICAX_PRODUCT_BRANCH_CASE}` : ''}.json`);
const report = { schema: 'icax.all-products-recheck-native-branches', schemaVersion: 1,
  runtimeRoot, prepareOnly, startedAt: new Date().toISOString(), cases: [], passed: false };
const save = () => writeFileSync(reportFile, JSON.stringify(report, null, 2));
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
let activeCase = '';
let activeCacheCalls = [];
const cacheEvidence = () => ({ calls: activeCacheCalls,
  hits: activeCacheCalls.reduce((sum, call) => sum + call.hits, 0),
  misses: activeCacheCalls.reduce((sum, call) => sum + call.misses, 0),
  completeDocumentAndRootKeys: activeCacheCalls.every(call => call.keyKind === 'complete-standard-json-document-and-root'),
  privateUnnormalizedCopies: activeCacheCalls.every(call => call.unnormalizedBRepCached && call.privateShapeCopyBeforeInspection) });

function connection() {
  const child = spawn(resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
    || resolve(repository, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe')), [], {
    cwd: runtimeRoot, windowsHide: true,
    env: { ...process.env, PATH: `${binaryRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'],
  });
  let sequence = 0, stderr = '';
  const pending = new Map();
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
    if (response.ok && response.result.inspectionCache)
      activeCacheCalls.push({ ...request.workload, ...response.result.inspectionCache });
    console.log(JSON.stringify({ nativeStage: request.method, case: activeCase, phase: 'finished',
      seconds: (Date.now() - request.startedAt) / 1000, ok: response.ok,
      ...request.workload, inspectionCache: response.result?.inspectionCache }));
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    close() {
      return new Promise(resolveClosed => {
        if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
        child.once('exit', resolveClosed);
        child.stdin.end(); child.kill();
      });
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        const id = ++sequence;
        const workload = method === 'InspectNeutralModel' ? { requestedRoots: payload.geometryKeys.length,
          points: payload.points?.length || 0, manufacturingCoordinates: payload.manufacturingCoordinates === true } : {};
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${JSON.stringify(workload)} ${stderr}`)); }, 600000);
        pending.set(id, { resolve: resolveRequest, reject, timer, method, workload, startedAt: Date.now() });
        console.log(JSON.stringify({ nativeStage: method, case: activeCase, phase: 'started',
          at: new Date().toISOString(), ...workload }));
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

function constructReference(scenario, descriptor, parameters) {
  const requestFile = resolve(output, `all-products-recheck-native-branches-${scenario.name}-host-input.json`);
  const referenceFile = resolve(output, `all-products-recheck-native-branches-${scenario.name}-source-reference.json`);
  writeFileSync(requestFile, JSON.stringify({ directory: scenario.directory, templateId: descriptor.id,
    template: descriptor, parameters }, null, 2));
  const python = process.env.ICAX_TEST_PYTHON || resolve(process.env.USERPROFILE,
    '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
  const generator = resolve(repository,
    'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/GenericProductLegacyManufacturingBaseline.py');
  const result = spawnSync(python, [generator, requestFile, referenceFile], {
    cwd: repository, windowsHide: true, encoding: 'utf8', timeout: 300000, maxBuffer: 1024 * 1024,
    env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1',
      PYTHONPATH: resolve(repository, 'src/iCAX-Engine/framework/TemplateRuntime/python') },
  });
  assert.equal(result.status, 0, `Same-input source reference failed: ${result.error || result.stderr}`);
  const reference = JSON.parse(readFileSync(referenceFile, 'utf8'));
  assert.deepEqual(reference.parameters, parameters);
  assert.deepEqual(reference.document.parameters, parameters);
  for (const [path, hash] of Object.entries(reference.sourceDigests)) {
    assert.equal(digest(resolve(repository, 'src', path)), hash, `Source changed after reference: ${path}`);
    if (!prepareOnly) assert.equal(digest(resolve(runtimeRoot, path)), hash, `Deployed source differs: ${path}`);
  }
  return { ...reference, requestFile, referenceFile };
}

function verifyPublic(reference) {
  const { display, manufacturing } = reference;
  assert.deepEqual(Object.keys(display).sort(),
    ['schema', 'schemaVersion', 'coordinateSystem', 'lengthUnit', 'resources', 'items', 'roots', 'annotations'].sort());
  assert.equal(display.schemaVersion, 2); assert.equal(display.schema, 'icax.display-model');
  assert.deepEqual(Object.keys(manufacturing).sort(), ['schema', 'schemaVersion', 'connections', 'processes'].sort());
  assert.equal(manufacturing.schemaVersion, 4); assert.equal(manufacturing.schema, 'icax.manufacturing-model');
  const keys = new Set(display.items.map(item => item.key)), pairs = new Set();
  assert.equal(keys.size, display.items.length);
  for (const connection of manufacturing.connections) {
    assert.ok(connection.items.every(key => keys.has(key)), connection.key);
    pairs.add(JSON.stringify([...connection.items].sort()));
    const anchors = (connection.properties?.participantAnchors || []).map(anchor => anchor.itemKey);
    assert.equal(new Set(anchors).size, anchors.length, connection.key);
    assert.ok(anchors.every(key => connection.items.includes(key)), connection.key);
  }
  for (const pair of reference.requiredPostContacts) assert.ok(pairs.has(JSON.stringify(pair)), `Missing actual contact: ${pair}`);
  const counts = new Map();
  for (const item of display.items) if (item.geometry) counts.set(item.geometry.resource, (counts.get(item.geometry.resource) || 0) + 1);
  assert.ok([...counts.values()].some(count => count > 1), 'Real design members must reuse a shared prototype');
  if (reference.requiredPostContacts.length) {
    assert.ok(manufacturing.connections.some(connection => connection.items.some(key => key.includes('.elevation.cap')
      || key.includes('.continuous.cap')) && connection.items.some(key => key.startsWith('post.'))));
    assert.ok(manufacturing.connections.every(connection => connection.items.every(key => !/^segment\.\d+\.cap\.\d+$/.test(key))),
      'Retired level cap keys must never survive the elevation layout');
  }
  return { designItems: display.items.length, designResources: display.resources.length,
    manufacturingConnections: manufacturing.connections.length, requiredPostContacts: reference.requiredPostContacts.length,
    exactPublicFields: true, sharedGeometryReused: true, connectionReferencesValid: true,
    anchorsBelongToUniqueParticipants: true, realCapBottomUpperContactsPresent: reference.requiredPostContacts.length > 0 };
}

function compareFacts(plan, baseline, execution) {
  assert.equal(plan.partCount, baseline.items.length);
  const current = new Map(execution.items.map(item => [item.key, item]));
  assert.deepEqual([...current.keys()].sort(), baseline.items.map(item => item.key).sort());
  const rows = plan.tables.flatMap(table => table.rows);
  let lengths = 0;
  const material = properties => properties['manufacturing.materialGrade'] || properties['manufacturing.material']
    || properties.materialGrade || properties.material;
  for (const item of baseline.items) {
    const next = current.get(item.key).properties, old = item.properties;
    const row = rows.find(entry => entry.itemKey === item.key);
    assert.ok(row, `Missing actual derived material row: ${item.key}`);
    if (typeof old.length === 'number') {
      assert.ok(Math.abs(next.length - old.length) <= 1e-7, `${item.key} exact stock length`);
      assert.ok(Math.abs(Number(row.values.length) - old.length) <= 0.001, `${item.key} material list millimetre precision`);
      lengths += 1;
    }
    if (material(old)) {
      assert.equal(material(next), material(old), item.key);
      assert.equal(row.values.materialGrade || row.values.material, material(old), item.key);
    }
    if (typeof old.quantity === 'number') {
      assert.equal(next.quantity, old.quantity, item.key);
      assert.equal(row.values.quantity, old.quantity, item.key);
    }
    if (old.partNumber) assert.equal(next.partNumber, old.partNumber, item.key);
  }
  assert.ok(lengths > 0);
  return { parts: current.size, checkedLengths: lengths, exactStockLengthsEqual: true,
    materialListLengthsEqual: true, materialsAndQuantitiesEqual: true, identitiesEqual: true };
}

function verifyLouverPreparation(reference, execution) {
  const { display, manufacturing } = reference;
  const resources = new Map(display.resources.map(resource => [resource.key, resource]));
  const calls = new Map(manufacturing.processes.map(process => [process.key, process.definition]));
  const connections = new Map(manufacturing.connections.map(connection => [connection.key, connection]));
  const items = new Map(display.items.map(item => [item.key, item]));
  const preparations = execution.extensions['tubeDesigner.workpiecePreparations'];
  assert.equal(preparations.length, display.items.length);
  let blades = 0, mappedSections = 0;
  for (const preparation of preparations) {
    const item = items.get(preparation.itemKey), profile = item.properties['tubeDesigner.profile'];
    assert.equal(resources.get(profile.sectionResource).operator, 'profile2d');
    assert.equal(preparation.sourceSectionResource, profile.sectionResource);
    if (profile.sectionCoordinateMap) {
      assert.deepEqual(profile.sectionCoordinateMap, [[0, 1], [1, 0]]); mappedSections += 1;
    }
    const call = calls.get(preparation.processKey);
    assert.equal(call.templateId, 'profile-stock-preparation');
    assert.equal(call.processInput.parts.stock.itemKey, item.key);
    const endpoints = {}, allowances = {};
    for (const key of call.processInput.geometry.connectionKeys) {
      const connection = connections.get(key);
      assert.ok(connection.items.includes(item.key));
      for (const participant of connection.properties.participantAnchors) {
        if (participant.itemKey !== item.key || participant.anchor.kind !== 'end') continue;
        const end = participant.anchor.end, point = participant.centerlinePoint || participant.anchor.point;
        assert.ok(Array.isArray(point) && point.length === 3 && point.every(Number.isFinite));
        if (endpoints[end]) assert.deepEqual(endpoints[end], point);
        if (Object.hasOwn(allowances, end)) assert.equal(allowances[end], participant.anchor.stockAllowance);
        endpoints[end] = point; allowances[end] = participant.anchor.stockAllowance;
      }
    }
    assert.deepEqual(Object.keys(endpoints).sort(), ['end', 'start']);
    assert.deepEqual(preparation.endAllowances, allowances);
    const member = item.properties['assemblyFrame.member'];
    const vector = member.end.map((value, index) => value - member.start[index]);
    const length = Math.hypot(...vector), axis = vector.map(value => value / length);
    const actualVector = endpoints.end.map((value, index) => value + axis[index] * allowances.end
      - endpoints.start[index] + axis[index] * allowances.start);
    assert.ok(Math.abs(preparation.physicalLength - Math.hypot(...actualVector)) <= 1e-7,
      `${item.key} preparation must consume actual assembly end points and stock allowances`);
    assert.equal(preparation.status, 'await-external'); assert.equal(preparation.ready, false);
    if (item.key.startsWith('blade.')) {
      assert.ok(Math.abs(preparation.physicalLength - 1200) <= 1e-7, `${item.key} original uncut 1200mm stock`);
      assert.equal(allowances.start, 20); assert.equal(allowances.end, 20); blades += 1;
    }
  }
  assert.ok(blades > 0 && mappedSections > 0);
  return { preparedItems: preparations.length, blades, mappedSections, intrinsicSectionResourcesConsumed: true,
    exactSectionCoordinateMapsConsumed: true, assemblyEndPointsAndAllowancesConsumed: true,
    bladeUncutLength: 1200, pendingExternalStatusRetained: true };
}

try {
  for (const scenario of selected) {
    activeCase = scenario.name; activeCacheCalls = [];
    const native = prepareOnly ? undefined : connection();
    const result = { name: scenario.name, passed: false };
    try {
      const sourceDescriptor = JSON.parse(readFileSync(resolve(repository,
        'src/apps/tube-designer/templates/product', scenario.directory, 'template.json'), 'utf8'));
      const descriptor = prepareOnly ? sourceDescriptor
        : (await native.invoke('GetTemplateDescriptor', { templateId: sourceDescriptor.id })).template;
      const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
      assert.ok(Object.keys(scenario.changes).every(key => Object.hasOwn(defaults, key)), 'Undeclared branch parameter');
      const parameters = { ...defaults, ...scenario.changes }, frozen = structuredClone(parameters);
      const reference = constructReference(scenario, descriptor, parameters);
      result.templateId = descriptor.id; result.parameters = frozen;
      result.sourceReferenceFile = reference.referenceFile;
      result.hostInputFile = reference.requestFile;
      result.sourceChecks = reference.sourceChecks;
      result.publicReferences = verifyPublic(reference);
      if (prepareOnly) {
        result.prepared = true;
      } else {
        const preview = await native.invoke('GenerateProductTemplatePreview', { templateId: descriptor.id, parameters });
        assert.deepEqual(preview.parameters, frozen); assert.equal(preview.templateId, descriptor.id);
        assert.deepEqual(preview.items.map(item => item.key).sort(), reference.display.items.map(item => item.key).sort());
        const request = { designModel: reference.display, manufacturingDefinition: reference.manufacturing };
        const requestBefore = structuredClone(request);
        assert.deepEqual(await native.invoke('ValidateSharedManufacturing', request),
          { valid: true, designItemCount: reference.display.items.length, manufacturingPartCountKnown: false });
        assert.deepEqual(request, requestBefore);
        const plan = await native.invoke('GetProductManufacturingPlan', { templateId: descriptor.id, parameters });
        assert.deepEqual(plan.parameters, frozen);
        await native.invoke('GeneratePreview', { templateId: descriptor.id, ...parameters });
        const before = await native.invoke('InspectScriptResources');
        assert.deepEqual(before.display.document, reference.display);
        assert.deepEqual(before.manufacturing, {});
        assert.deepEqual(before.manufacturingExecution, {});
        await native.invoke('Disassemble');
        const snapshot = await native.invoke('InspectScriptResources');
        assert.deepEqual(snapshot.display.document, reference.display);
        assert.deepEqual(snapshot.manufacturing.document, reference.manufacturing);
        result.partFacts = compareFacts(plan, reference.document, snapshot.manufacturingExecution.document);

        const shapes = await inspectManufacturedShapes(native);
        result.baselineShapes = await compareBaselineShapes(native, reference.document);
        await native.invoke('SaveAndReopen');
        assert.deepEqual(await native.invoke('InspectScriptResources'), snapshot,
          'Save/reopen must preserve exact shared design, manufacturing refs and actual world execution');
        assert.deepEqual(await native.invoke('InspectNativeGeometry'), shapes,
          'Save/reopen must preserve exact persisted BRep bytes, bounds, volume and placement');
        result.inspectionCache = cacheEvidence();
        if (process.env.ICAX_NATIVE_INSPECTION_CACHE_REQUIRED === '1') {
          assert.ok(result.inspectionCache.calls.length > 0);
          assert.ok(result.inspectionCache.hits > 0 && result.inspectionCache.misses > 0);
          assert.equal(result.inspectionCache.completeDocumentAndRootKeys, true);
          assert.equal(result.inspectionCache.privateUnnormalizedCopies, true);
        }
        assert.deepEqual(parameters, frozen);
        result.hostParametersEqual = true; result.nativePreviewAndPlanningAccepted = true;
        result.exactSaveReopenPreserved = true; result.passed = true;
      }
      report.cases.push(result); save(); console.log(JSON.stringify(result));
    } catch (error) {
      result.inspectionCache = cacheEvidence();
      result.error = error.stack; if (error.comparison) result.comparison = error.comparison;
      report.cases.push(result); save(); throw error;
    } finally { await native?.close(); }
  }
  report.finishedAt = new Date().toISOString(); report.passed = !prepareOnly && report.cases.every(result => result.passed);
  save(); console.log(JSON.stringify({ prepared: prepareOnly, passed: report.passed, cases: report.cases.length, reportFile }));
} catch (error) {
  report.finishedAt = new Date().toISOString(); report.error = error.message; save(); throw error;
}
