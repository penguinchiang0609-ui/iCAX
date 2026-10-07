// Execute only after the final runtime deployment. Each case owns one bridge.
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
const fixture = resolve(output, 'all-products-recheck-generic-verified-synthetic-system.json');
const prepareOnly = process.argv.includes('--prepare-only');
const system = { systemSource: 'file', systemFile: fixture };
const scenarios = [
  ...['legacy_processed', 'external_templates'].map(mode => ({
    name: `minimal-closed-${mode === 'legacy_processed' ? 'legacy' : 'external'}`,
    directory: 'minimal_protective_grille', check: 'minimal',
    changes: { frameType: 'closed_frame', assemblyPlanningMode: mode },
  })),

];
const requested = scenarios.filter(scenario => !process.env.ICAX_SHARED_WORKPIECE_CASE
  || process.env.ICAX_SHARED_WORKPIECE_CASE === scenario.name);
assert.ok(requested.length > 0, 'Unknown ICAX_SHARED_WORKPIECE_CASE');
mkdirSync(output, { recursive: true });
const reportFile = resolve(output, `all-products-recheck-native-shared-workpieces${prepareOnly ? '-prepared' : ''}`
  + `${process.env.ICAX_SHARED_WORKPIECE_CASE ? `-${process.env.ICAX_SHARED_WORKPIECE_CASE}` : ''}.json`);
const previous = process.argv.includes('--resume') ? JSON.parse(readFileSync(reportFile, 'utf8')) : undefined;
assert.ok(!previous || (!prepareOnly && previous.runtimeRoot === runtimeRoot));
const completed = previous?.cases.filter(entry => entry.passed) || [];
const deploymentFile = resolve(output, 'all-products-recheck-deployment-hashes.json');
const deployment = prepareOnly ? undefined : JSON.parse(readFileSync(deploymentFile, 'utf8'));
if (deployment) {
  assert.equal(deployment.passed, true);
  for (const binary of deployment.binaries) assert.equal(
    createHash('sha256').update(readFileSync(resolve(runtimeRoot, binary.file))).digest('hex'), binary.sha256);
}
const beforeCheckpointFile = resolve(output, 'all-products-recheck-deployment-hashes-before-native-bounds.json');
if (completed.length) {
  const beforeCheckpoint = JSON.parse(readFileSync(beforeCheckpointFile, 'utf8'));
  for (const entry of completed) {
    entry.validationStage ||= 'before-host-applicability-checkpoint';
    entry.dllHashes ||= beforeCheckpoint.binaries;
  }
}
const selected = requested.filter(scenario => !completed.some(entry => entry.name === scenario.name));
assert.ok(selected.length > 0, 'There are no unfinished selected cases');
const report = { schema: 'icax.native-shared-workpiece-branches', schemaVersion: 1, runtimeRoot, prepareOnly,
  startedAt: new Date().toISOString(), initialStartedAt: previous?.initialStartedAt || previous?.startedAt,
  resumedCompletedCases: completed.map(entry => entry.name), scenarios: requested.map(scenario => scenario.name),
  deploymentFile, dllHashes: deployment?.binaries, previousCheckpointDeploymentFile: completed.length ? beforeCheckpointFile : undefined,
  cases: completed, passed: false };
const save = () => writeFileSync(reportFile, JSON.stringify(report, null, 2));
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');

function connection(scenario) {
  const bridgePath = resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
    || resolve(repository, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe'));
  const classificationEvidence = [];
  const apiTimings = [];
  // The 13k-face round scene is a deliberately complete slow-branch check.
  // This controls only the test wait; it is not a product-interface deadline.
  const requestTimeoutMs = scenario.name === 'door-uncalled-round_scene-two_blocks' ? 1800000 : 600000;
  const child = spawn(bridgePath, [], {
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
    Object.assign(request.timing, { finishedAt: new Date().toISOString(),
      elapsedMilliseconds: Date.now() - request.started, ok: response.ok });
    console.log(JSON.stringify({ case: scenario.name, stage: 'native-end', ...request.timing }));
    if (response.ok && request.pointsRequested) {
      try {
        const rows = response.result.geometryChecks;
        const controls = rows.map(row => {
          assert.ok(row.pointChecks.length > 0);
          const states = [...new Set(row.pointChecks.map(point =>
            `${point.inside ? 'I' : ''}${point.outside ? 'O' : ''}${point.boundary ? 'B' : ''}` || 'U'))];
          assert.equal(row.classificationReuseControls, states.length,
            'Every observed classification state must have a real fresh-classifier control');
          return { key: row.geometryKey || row.key, points: row.pointChecks.length,
            classificationReuseControls: row.classificationReuseControls, states };
        });
        classificationEvidence.push({ method: request.method, roots: controls });
      } catch (error) { request.reject(error); return; }
    }
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    bridgePath,
    classificationEvidence,
    apiTimings,
    requestTimeoutMilliseconds: requestTimeoutMs,
    close() {
      return new Promise(resolveClosed => {
        if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
        child.once('exit', resolveClosed); child.stdin.end(); child.kill();
      });
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        const id = ++sequence;
        const started = Date.now();
        const timing = { id, method, startedAt: new Date(started).toISOString(),
          waitLimitMilliseconds: requestTimeoutMs };
        apiTimings.push(timing);
        console.log(JSON.stringify({ case: scenario.name, stage: 'native-begin', ...timing }));
        const timer = setTimeout(() => {
          pending.delete(id);
          Object.assign(timing, { finishedAt: new Date().toISOString(), elapsedMilliseconds: Date.now() - started,
            ok: false, waitLimitReached: true });
          console.log(JSON.stringify({ case: scenario.name, stage: 'native-end', ...timing }));
          reject(new Error(`Timeout ${method}: ${stderr}`));
        }, requestTimeoutMs);
        pending.set(id, { resolve: resolveRequest, reject, timer, method, started, timing,
          pointsRequested: ['InspectNeutralModel', 'InspectNativeGeometry'].includes(method) && payload.points?.length > 0 });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

function pythonAudit(action, inputFile, outputFile) {
  const python = process.env.ICAX_TEST_PYTHON || resolve(process.env.USERPROFILE,
    '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
  const helper = resolve(repository,
    'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/SharedWorkpieceNativeBaseline.py');
  const result = spawnSync(python, [helper, action, inputFile, outputFile], {
    cwd: repository, windowsHide: true, encoding: 'utf8', timeout: 300000, maxBuffer: 1024 * 1024,
    env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1',
      PYTHONPATH: resolve(repository, 'src/iCAX-Engine/framework/TemplateRuntime/python') },
  });
  assert.equal(result.status, 0, `${action} failed: ${result.error || result.stderr || result.stdout}`);
  return JSON.parse(readFileSync(outputFile, 'utf8'));
}

function constructReference(scenario, descriptor, parameters) {
  const stem = resolve(output, `all-products-recheck-native-shared-workpieces-${scenario.name}`);
  const requestFile = `${stem}-host-input.json`, referenceFile = `${stem}-source-reference.json`;
  writeFileSync(requestFile, JSON.stringify({ directory: scenario.directory,
    templateId: descriptor.id, template: descriptor, parameters }, null, 2));
  const reference = pythonAudit('reference', requestFile, referenceFile);
  assert.deepEqual(reference.parameters, parameters);
  assert.deepEqual(reference.document.parameters, parameters);
  for (const [path, hash] of Object.entries(reference.sourceDigests)) {
    assert.equal(digest(resolve(repository, 'src', path)), hash, `Reference source changed: ${path}`);
    const installedPath = path.replace(/^iCAX-Engine\/framework\/TemplateRuntime\/python\//, 'runtime/template-python/');
    if (!prepareOnly) assert.equal(digest(resolve(runtimeRoot, installedPath)), hash, `Deployed source differs: ${installedPath}`);
  }
  return { ...reference, requestFile, referenceFile, stem };
}

function verifyPublic(reference) {
  const { display, manufacturing } = reference;
  assert.deepEqual(Object.keys(display).sort(),
    ['schema', 'schemaVersion', 'coordinateSystem', 'lengthUnit', 'resources', 'items', 'roots', 'annotations'].sort());
  assert.equal(display.schema, 'icax.display-model'); assert.equal(display.schemaVersion, 2);
  assert.deepEqual(Object.keys(manufacturing).sort(), ['schema', 'schemaVersion', 'connections', 'processes'].sort());
  assert.equal(manufacturing.schema, 'icax.manufacturing-model'); assert.equal(manufacturing.schemaVersion, 4);
  const keys = new Set(display.items.map(item => item.key));
  assert.equal(keys.size, display.items.length);
  for (const connection of manufacturing.connections) {
    assert.ok(connection.items.every(key => keys.has(key)), connection.key);
    const anchors = (connection.properties?.participantAnchors || []).map(anchor => anchor.itemKey);
    assert.equal(new Set(anchors).size, anchors.length, connection.key);
    assert.ok(anchors.every(key => connection.items.includes(key)), connection.key);
  }
  for (const item of display.items) {
    const profilePart = item.properties['window.profilePart'];
    if (profilePart) {
      assert.ok(['cutPlanes', 'machiningFeatures', 'assemblyTransform'].every(key => !Object.hasOwn(profilePart, key)));
      assert.equal(profilePart.length, item.properties.length);
    }
  }
  if (reference.counterpartDisplay) assert.deepEqual(display, reference.counterpartDisplay);
  return { designItems: display.items.length, designResources: display.resources.length,
    connections: manufacturing.connections.length, exactPublicFields: true, referencesValid: true,
    productParametersExcludedFromDeclarations: true, entireDisplayRouteIndependent: !!reference.counterpartDisplay };
}

function compareMaterialFacts(plan, baseline, execution) {
  assert.equal(plan.partCount, baseline.items.length);
  const actual = new Map(execution.items.map(item => [item.key, item]));
  assert.deepEqual([...actual.keys()].sort(), baseline.items.map(item => item.key).sort());
  const rows = plan.tables.flatMap(table => table.rows);
  const material = properties => properties['manufacturing.materialGrade'] || properties['manufacturing.material']
    || properties.materialGrade || properties.material;
  let checkedLengths = 0;
  for (const item of baseline.items) {
    const previous = item.properties, current = actual.get(item.key).properties;
    const row = rows.find(entry => entry.itemKey === item.key);
    assert.ok(row, `Missing actual material row: ${item.key}`);
    if (typeof previous.length === 'number') {
      assert.ok(Math.abs(current.length - previous.length) <= 1e-7, `${item.key} exact reporting length`);
      assert.ok(Math.abs(Number(row.values.length) - previous.length) <= 0.001, `${item.key} material-list length`);
      checkedLengths += 1;
    }
    if (previous.partNumber) assert.equal(current.partNumber, previous.partNumber, item.key);
    if (typeof previous.quantity === 'number') {
      assert.equal(current.quantity, previous.quantity, item.key);
      assert.equal(row.values.quantity, previous.quantity, item.key);
    }
    if (material(previous)) {
      assert.equal(material(current), material(previous), item.key);
      assert.equal(row.values.materialGrade || row.values.material, material(previous), item.key);
    }
  }
  assert.ok(checkedLengths > 0);
  return { actualParts: actual.size, checkedLengths, originalLengthsMaterialsQuantitiesAndNumbersEqual: true };
}

function branchChecks(scenario, reference, execution, nativeShapes) {
  const items = new Map(reference.display.items.map(item => [item.key, item]));
  const resources = new Map(reference.display.resources.map(resource => [resource.key, resource]));
  const oldItems = new Map(reference.document.items.map(item => [item.key, item]));
  const actualItems = new Map(execution.items.map(item => [item.key, item]));
  const calls = reference.manufacturing.processes;
  const preparations = execution.extensions['tubeDesigner.workpiecePreparations'] || [];
  const byItem = new Map(preparations.map(entry => [entry.itemKey, entry]));
  assert.equal(byItem.size, preparations.length);
  const machining = calls.filter(call => !['profile-stock-preparation', 'product-manufacturing-members']
    .includes(call.definition.templateId));
  const profileItems = reference.display.items.filter(item => item.properties['assemblyFrame.member']);
  if (scenario.check.startsWith('aluminium')) {
    const expectedPreparations = reference.sourceExecution.extensions['tubeDesigner.workpiecePreparations'] || [];
    assert.deepEqual([...byItem.keys()].sort(), expectedPreparations.map(entry => entry.itemKey).sort());
    for (const preparation of preparations) {
      const item = items.get(preparation.itemKey);
      const profile = item.properties['tubeDesigner.profile'];
      assert.equal(resources.get(profile.sectionResource).operator, 'profile2d');
      assert.equal(preparation.sourceSectionResource, profile.sectionResource);
      assert.ok(Math.abs(preparation.reportedLength - oldItems.get(item.key).properties.length) <= 1e-7, item.key);
    }
    if (scenario.check === 'aluminium-external') {
      assert.deepEqual([...byItem.keys()].sort(), profileItems.map(item => item.key).sort());
      assert.equal(machining.length, 0, 'External assembly still awaits real external machining');
      assert.ok(preparations.every(entry => entry.status === 'await-external' && entry.ready === false));
      const internal = ['mullion.1', 'transom.1.1', 'transom.1.2'];
      const lengths = internal.map(key => {
        assert.ok(byItem.has(key), `Missing full-stock preparation: ${key}`);
        const actual = byItem.get(key).physicalLength, shown = items.get(key).properties.length;
        assert.ok(actual > shown, `${key} stock must include real assembly-end reserves`);
        assert.ok(Math.abs(actual - oldItems.get(key).properties.length) <= 1e-7, key);
        assert.equal(actualItems.get(key).properties['tubeDesigner.assemblyPlanning'].ready, false);
        return { key, physicalLength: actual, displayLength: shown };
      });
      assert.ok(Math.abs(lengths[0].physicalLength - 1500) <= 1e-7);
      assert.ok(lengths.slice(1).every(entry => Math.abs(entry.physicalLength - 775) <= 1e-7));
      return { internalFullStocks: lengths, externalMachiningNotPretended: true, pendingStatusRetained: true };
    }
    assert.ok(preparations.every(entry => entry.status === 'prepared' && entry.ready === true));
    if (scenario.check === 'aluminium-uncalled') {
      const targets = new Set(machining.flatMap(call => Object.values(call.definition.targets)));
      const rails = profileItems.filter(item => !targets.has(item.key) && /\.(bottom|top)$/.test(item.key));
      assert.ok(rails.length >= 2, 'Real side-wrap horizontal rails must have no machining call');
      assert.ok(rails.some(item => byItem.get(item.key).physicalLength < item.properties.length),
        'Uncalled legacy rail requires its own shorter assembly interval, not the full display outline');
      return { uncalledPreparedRails: rails.map(item => ({ key: item.key,
        displayLength: item.properties.length, physicalLength: byItem.get(item.key).physicalLength })),
        noCuttingCallInvented: true, allPreparedFromIntrinsicSections: true };
    }
    return { preparedItems: preparations.length, originalInternalJointResultsRetained: true };
  }
  if (scenario.check === 'minimal') {
    assert.equal(preparations.length, reference.display.items.length);
    let swapped = 0;
    for (const item of reference.display.items) {
      const profile = item.properties['tubeDesigner.profile'], resource = resources.get(profile.sectionResource);
      assert.equal(resource.operator, 'profile2d');
      assert.deepEqual(resource.arguments.contours, profile.contours, `${item.key} original profile coordinates`);
      assert.equal(byItem.get(item.key).sourceSectionResource, profile.sectionResource);
      const map = profile.sectionCoordinateMap;
      if (JSON.stringify(map) === JSON.stringify([[0, 1], [1, 0]])) swapped += 1;
      else assert.deepEqual(map, [[1, 0], [0, 1]]);
      assert.equal(actualItems.get(item.key).properties.partNumber, oldItems.get(item.key).properties.partNumber);
    }
    assert.ok(swapped > 0, 'Horizontal original profiles require an explicit swap map');
    const external = scenario.changes.assemblyPlanningMode === 'external_templates';
    assert.ok(preparations.every(entry => entry.status === (external ? 'await-external' : 'prepared')
      && entry.ready === !external));
    const ends = (execution.extensions['tubeDesigner.assemblyGeometryProcesses']?.instances || [])
      .filter(record => record.templateId === 'tube-end-joint');
    if (external) assert.equal(ends.length, 0);
    else {
      assert.equal(ends.length, 8);
      for (const record of ends) assert.deepEqual(record.processInput.parts.stock.profileRef,
        { scope: 'template', id: record.stockId, templateId: 'minimal-protective-grille' });
    }
    return { preparedItems: preparations.length, originalProfileResources: true, explicitlySwappedSections: swapped,
      originalPartNumbersRetained: true, originalMiterProfileReferences: ends.length,
      entireDisplayRouteIndependent: true };
  }
  assert.equal(scenario.check, 'door');
  assert.ok(machining.every(call => !Object.values(call.definition.targets).includes('leaf.1')),
    'This actual branch must exercise a leaf without a surface-cutting call');
  const preparation = byItem.get('leaf.1');
  assert.ok(preparation); assert.equal(preparation.templateId, 'intrinsic-plate-preparation');
  assert.equal(preparation.sourceComponent, 'plate'); assert.equal(preparation.status, 'prepared');
  const leaf = nativeShapes.geometryChecks.find(check => check.key === 'leaf.1');
  assert.ok(leaf && leaf.valid && leaf.solids === 1 && leaf.volume > 0 && leaf.resourceBytes > 0,
    'The uncalled decorative leaf must be a real persisted solid, not a six-face display compound');
  return { uncalledItem: 'leaf.1', intrinsicPlatePreparation: true, actualPersistedSolids: leaf.solids,
    actualPersistedVolume: leaf.volume, noSurfaceMachiningCallInvented: true };
}

try {
  for (const scenario of selected) {
    const native = prepareOnly ? undefined : connection(scenario);
    const result = { name: scenario.name, passed: false,
      validationStage: 'after-host-applicability-checkpoint', dllHashes: deployment?.binaries,
      bridgePath: native?.bridgePath, bridgeSha256: native ? digest(native.bridgePath) : undefined,
      apiTimings: native?.apiTimings, requestTimeoutMilliseconds: native?.requestTimeoutMilliseconds };
    try {
      const sourceDescriptor = JSON.parse(readFileSync(resolve(repository,
        'src/apps/tube-designer/templates/product', scenario.directory, 'template.json'), 'utf8'));
      const descriptor = prepareOnly ? sourceDescriptor
        : (await native.invoke('GetTemplateDescriptor', { templateId: sourceDescriptor.id })).template;
      const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
      assert.ok(Object.keys(scenario.changes).every(key => Object.hasOwn(defaults, key)), 'Undeclared branch parameter');
      if (scenario.directory === 'aluminium_window') {
        const systemFixture = JSON.parse(readFileSync(fixture, 'utf8'));
        assert.equal(systemFixture.status, 'verified'); assert.equal(systemFixture.manufacturer, 'UNIT TEST ONLY');
        assert.match(systemFixture.source, /Synthetic/); result.syntheticProfileSystem = true;
      }
      const parameters = { ...defaults, ...scenario.changes }, frozen = structuredClone(parameters);
      const reference = constructReference(scenario, descriptor, parameters);
      result.templateId = descriptor.id; result.parameters = frozen;
      result.hostInputFile = reference.requestFile; result.sourceReferenceFile = reference.referenceFile;
      result.sourceChecks = reference.sourceChecks; result.publicChecks = verifyPublic(reference);
      if (prepareOnly) result.prepared = true;
      else {
        const preview = await native.invoke('GenerateProductTemplatePreview', { templateId: descriptor.id, parameters });
        assert.deepEqual(preview.parameters, frozen); assert.equal(preview.templateId, descriptor.id);
        assert.deepEqual(preview.items.map(item => item.key).sort(), reference.display.items.map(item => item.key).sort());
        assert.deepEqual(await native.invoke('ValidateSharedManufacturing',
          { designModel: reference.display, manufacturingDefinition: reference.manufacturing }),
        { valid: true, designItemCount: reference.document.items.length, manufacturingPartCountKnown: false });
        let productEntityId;
        if (reference.counterpartParameters) {
          const alternate = await native.invoke('GenerateProductTemplatePreview',
            { templateId: descriptor.id, parameters: reference.counterpartParameters });
          assert.deepEqual(alternate.parameters, reference.counterpartParameters);
          const alternateGenerated = await native.invoke('GeneratePreview',
            { templateId: descriptor.id, ...reference.counterpartParameters });
          productEntityId = alternateGenerated.tubeDesigner.product.entityId;
          assert.deepEqual((await native.invoke('InspectScriptResources')).display.document, reference.display,
            'Actual native opposite-route preview must preserve the entire shared display document');
          result.oppositeRouteEntireNativeDisplayEqual = true;
        }
        const plan = await native.invoke('GetProductManufacturingPlan', { templateId: descriptor.id, parameters });
        assert.deepEqual(plan.parameters, frozen);
        const generated = await native.invoke('GeneratePreview', { templateId: descriptor.id, ...parameters,
          ...(productEntityId ? { productEntityId } : {}) });
        productEntityId = generated.tubeDesigner.product.entityId;
        const before = await native.invoke('InspectScriptResources');
        writeFileSync(`${reference.stem}-native-before-disassemble.json`, JSON.stringify(before));
        assert.deepEqual(before.display.document, reference.display);
        assert.deepEqual(before.manufacturing, {}, 'A fresh display preview has no persisted manufacturing declaration');
        assert.deepEqual(before.manufacturingExecution, {}, 'A fresh display preview has no executed manufacturing model');
        assert.deepEqual(before.generatedParameters, frozen);
        await native.invoke('Disassemble', { productEntityIds: [productEntityId] });
        const snapshot = await native.invoke('InspectScriptResources');
        assert.deepEqual(snapshot.display.document, reference.display);
        assert.deepEqual(snapshot.manufacturing.document, reference.manufacturing);
        assert.deepEqual(snapshot.generatedParameters, frozen);
        assert.deepEqual(snapshot.manufacturingParameters, frozen);
        const actualFile = `${reference.stem}-native-snapshot.json`, semanticFile = `${reference.stem}-native-semantics.json`;
        writeFileSync(actualFile, JSON.stringify({ reference, snapshot }));
        result.nativeSemanticChecks = pythonAudit('verify-native', actualFile, semanticFile);
        result.nativeSnapshotFile = actualFile; result.nativeSemanticsFile = semanticFile;
        result.materialFacts = compareMaterialFacts(plan, reference.document, snapshot.manufacturingExecution.document);
        const shapes = await inspectManufacturedShapes(native);
        result.branchChecks = branchChecks(scenario, reference, snapshot.manufacturingExecution.document, shapes);
        result.baselineShapes = await compareBaselineShapes(native, reference.document);
        assert.ok(native.classificationEvidence.length >= 6, 'All three BRep comparison layers need fresh-classifier evidence');
        const classificationFile = `${reference.stem}-classification-reuse-controls.json`;
        writeFileSync(classificationFile, JSON.stringify(native.classificationEvidence, null, 2));
        result.classificationReuseControls = { evidenceFile: classificationFile,
          calls: native.classificationEvidence.length,
          freshControls: native.classificationEvidence.flatMap(call => call.roots)
            .reduce((count, row) => count + row.classificationReuseControls, 0),
          checkedPoints: native.classificationEvidence.flatMap(call => call.roots)
            .reduce((count, row) => count + row.points, 0),
          everyObservedStateCheckedAgainstFreshClassifier: true };
        await native.invoke('SaveAndReopen');
        assert.deepEqual(await native.invoke('InspectScriptResources'), snapshot,
          'Save/reopen must preserve exact display, raw declarations and actual execution');
        assert.deepEqual(await native.invoke('InspectNativeGeometry'), shapes,
          'Save/reopen must preserve exact persisted BRep bytes, placement, volume and bounds');
        assert.deepEqual(parameters, frozen);
        result.hostParametersEqual = true; result.nativePreviewAndPlanAccepted = true;
        result.exactFrozenSaveReopenPreserved = true; result.passed = true;
      }
      report.cases.push(result); save(); console.log(JSON.stringify(result));
    } catch (error) {
      result.error = error.stack; if (error.comparison) result.comparison = error.comparison;
      report.cases.push(result); save(); throw error;
    } finally { await native?.close(); }
  }
  report.finishedAt = new Date().toISOString(); report.passed = !prepareOnly
    && report.cases.length === requested.length && report.cases.every(result => result.passed);
  save(); console.log(JSON.stringify({ prepared: prepareOnly, passed: report.passed, cases: report.cases.length, reportFile }));
} catch (error) {
  report.finishedAt = new Date().toISOString(); report.error = error.message; save();
  console.error(JSON.stringify({ passed: false, reportFile, case: report.cases.at(-1)?.name,
    error: error.message.slice(0, 300) }));
  process.exitCode = 1;
}
