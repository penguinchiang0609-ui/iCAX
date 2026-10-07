// Verify pure design resources and host parameters through real preview, machining and reopen.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compareBaselineShapes, inspectManufacturedShapes } from './productManufacturingShapeAssertions.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = resolve(root, 'output/tests/assembly-process');
mkdirSync(output, { recursive: true });
const reports = [];
function connection() {
  const child = spawn(resolve(root, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe'), [], {
    cwd: process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || root, windowsHide: true,
    env: { ...process.env, PATH: resolve(root, 'src/x64/Debug') + ';' + process.env.PATH },
    stdio: ['pipe', 'pipe', 'pipe'],
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
      return new Promise((resolve, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 600000);
        pending.set(id, { method, resolve, reject, timer });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

const memberState = state => state.members.map(row => [row.entityId, row.previewGeometryResourceId,
  row.previewGeometryResourceVersion, row.transform]);
const partState = state => state.parts.map(row => [row.stableKey, row.entityId,
  row.manufacturingGeometryResourceId, row.manufacturingGeometryResourceVersion])
  .sort((first, second) => first[0].localeCompare(second[0]));
const activeInstance = state => state.instances.find(row => row.active);
function generateWindowLegacyBaseline(scenarioName, templateId, descriptor, parameters) {
  const requestFile = resolve(output, `manufacturing-v4-${scenarioName}-host-input.json`);
  const baselineFile = resolve(output, `manufacturing-v4-old-${scenarioName}-geometry.json`);
  writeFileSync(requestFile, JSON.stringify({ templateId, template: descriptor, parameters }, null, 2));
  const python = process.env.ICAX_TEST_PYTHON || resolve(process.env.USERPROFILE,
    '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
  const generator = resolve(root,
    'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/SecurityWindowLegacyManufacturingBaseline.py');
  const result = spawnSync(python, [generator, requestFile, baselineFile], {
    cwd: root, windowsHide: true, encoding: 'utf8', timeout: 300000, maxBuffer: 1024 * 1024 });
  assert.equal(result.status, 0, `Same-input legacy reference generation failed: ${result.error || result.stderr}`);
  const baseline = JSON.parse(readFileSync(baselineFile, 'utf8'));
  assert.deepEqual(baseline.parameters, parameters);
  return { baseline, baselineFile };
}

async function compareWindowPartFacts(native, templateId, parameters, execution, baseline) {
  const current = new Map(execution.items.map(item => [item.key, item]));
  assert.equal(current.size, baseline.items.length);
  const plan = await native.invoke('GetProductManufacturingPlan', { templateId, parameters });
  assert.deepEqual(plan.parameters, parameters);
  assert.equal(plan.partCount, baseline.items.length);
  const rows = plan.tables.flatMap(table => table.rows);
  let lengths = 0;
  for (const item of baseline.items) {
    assert.ok(current.has(item.key), `Missing manufactured item ${item.key}`);
    const length = item.properties.length;
    if (typeof length !== 'number') continue;
    const actualLength = current.get(item.key).properties.length;
    assert.ok(Math.abs(actualLength - length) <= 1e-7,
      `${item.key} final length: ${actualLength} vs ${length}`);
    const row = rows.find(entry => entry.itemKey === item.key);
    assert.ok(row, `Missing material row ${item.key}`);
    assert.ok(Math.abs(Number(row.values.length) - length) <= 0.001,
      `${item.key} derived material length: ${row.values.length} vs ${length}`);
    lengths += 1;
  }
  assert.ok(lengths > 0);
  return { parts: current.size, finalPartLengthsEqual: true, materialListLengthsEqual: true, checkedLengths: lengths };
}

function inspectResources(document, purpose, mustShare = true) {
  const raw = document.document;
  assert.equal(document.schema, purpose === 'display' ? 'icax.display-model' : 'icax.manufacturing-model');
  assert.equal(document.schemaVersion, purpose === 'display' ? 2 : 4);
  assert.equal(raw.schema, document.schema);
  if (purpose === 'manufacturing') {
    assert.deepEqual(document.items, []);
    assert.deepEqual(document.resourceKeys, []);
    assert.equal(document.booleanResources, 0);
    assert.deepEqual(Object.keys(raw).sort(), ['schema', 'schemaVersion', 'connections', 'processes'].sort());
    for (const key of ['items', 'resources', 'roots', 'parameters', 'template', 'sourceMappings']) {
      assert.ok(!Object.hasOwn(raw, key), `Manufacturing must reference host design instead of duplicating ${key}`);
    }
    assert.equal(new Set(raw.connections.map(row => row.key)).size, raw.connections.length);
    assert.equal(new Set(raw.processes.map(row => row.key)).size, raw.processes.length);
    const processKeys = new Set(raw.processes.map(row => row.key));
    for (const process of raw.processes) {
      assert.equal(process.kind, 'assembly-process');
      const definition = process.definition;
      const wholeAssembly = Object.hasOwn(definition, 'outputs');
      assert.deepEqual(Object.keys(definition).sort(),
        ['templateId', 'processInput', 'parameters', 'processDrafts',
          wholeAssembly ? 'outputs' : 'targets', 'dependencies'].sort());
      assert.equal(definition.processInput.schema, 'icax.assembly-process-input');
      assert.equal(definition.processInput.schemaVersion, wholeAssembly ? 3 : 2);
      assert.ok(!Object.hasOwn(definition.processInput.geometry, 'connections'));
      if (wholeAssembly) {
        assert.equal(definition.templateId, 'security-window-assembly');
        assert.equal(raw.processes.length, 1);
        assert.equal(definition.processInput.parts.members.scope, 'design');
        assert.deepEqual(definition.outputs,
          { manufacturing: { kind: 'manufacturing-set', key: 'window.manufacturing' } });
      }
      assert.ok(definition.dependencies.every(key => processKeys.has(key) && key !== process.key));
    }
    return { connections: raw.connections.length, processes: raw.processes.length, duplicateDesignObjects: false };
  }
  assert.deepEqual(Object.keys(raw).sort(), ['schema', 'schemaVersion', 'coordinateSystem', 'lengthUnit',
    'resources', 'items', 'roots', 'annotations'].sort());
  assert.ok(!Object.hasOwn(raw, 'connections'), 'Connections belong to the manufacturing declaration');
  const forbidden = value => {
    if (Array.isArray(value)) return value.forEach(forbidden);
    if (value && typeof value === 'object') {
      for (const [key, child] of Object.entries(value)) {
        assert.ok(!key.startsWith('manufacturing.') && !key.startsWith('tubeDesigner.assemblyProcess')
          && !key.startsWith('tubeDesigner.assemblyGeometryProcess'), key);
        assert.ok(!['tubeDesigner.endProcess', 'tubeDesigner.connectionProcess',
          'tubeDesigner.manufacturingPartCount', 'stockState', 'stockInterval'].includes(key), key);
        forbidden(child);
      }
    }
  };
  forbidden(raw);
  assert.equal(document.hasLegacyGeometry, false);
  const keys = new Set(document.resourceKeys);
  assert.equal(keys.size, document.resourceKeys.length);
  assert.ok(keys.size > 0);
  const references = new Map();
  let placements = 0;
  for (const item of raw.items) {
    assert.ok(!Object.hasOwn(item, 'representations'));
    const reference = item.geometry;
    if (!reference) continue;
    assert.equal(typeof reference, 'object');
    assert.ok(keys.has(reference.resource), item.key);
    const group = references.get(reference.resource) || [];
    group.push(reference); references.set(reference.resource, group);
    if (reference.placement) {
      placements += 1;
      for (const key of ['origin', 'xAxis', 'yAxis', 'zAxis']) {
        assert.equal(reference.placement[key].length, 3);
        assert.ok(reference.placement[key].every(Number.isFinite));
      }
    }
  }
  const shared = [...references.values()].filter(group => group.length > 1);
  assert.equal(document.booleanResources, 0);
  if (mustShare) {
    assert.ok(shared.some(group => group[0].placement && group[1].placement
      && JSON.stringify(group[0].placement) !== JSON.stringify(group[1].placement)),
    'Finished parts at different positions must reference one actual shared resource');
  }
  return { resources: keys.size, items: raw.items.length, placements, sharedGroups: shared.length };
}

function stableManufacturingInput(snapshot) {
  const raw = snapshot.document;
  assert.equal(raw.processes.length, 1);
  const { parts, geometry } = raw.processes[0].definition.processInput;
  return { connections: raw.connections, parts, geometry };
}

function inspectAssemblyOutputSet(snapshot) {
  const raw = snapshot.display.document;
  const execution = snapshot.manufacturingExecution.document;
  const outputs = execution.extensions['tubeDesigner.assemblyOutputSets'];
  assert.equal(outputs.length, 1);
  const output = outputs[0];
  assert.deepEqual({ processKey: output.processKey, outputKey: output.outputKey, key: output.key, kind: output.kind },
    { processKey: 'window.assembly', outputKey: 'manufacturing', key: 'window.manufacturing', kind: 'manufacturing-set' });
  assert.deepEqual([...output.sourceItemKeys].sort(), raw.items.map(item => item.key).sort());
  assert.deepEqual([...output.itemKeys].sort(), execution.items.map(item => item.key).sort());
  assert.equal(new Set(output.itemKeys).size, output.itemKeys.length);
  return { designMembers: output.sourceItemKeys.length, physicalParts: output.itemKeys.length };
}

const scenarios = [
  { name: 'single-weld', changes: { faceType: 'single', frameManufacturingMode: 'segment_weld' }, displayOuter: 4, manufacturingOuter: 4 },
  { name: 'three-continuous', changes: { faceType: 'three', frameManufacturingMode: 'spatial_v_notch' }, displayOuter: 10, manufacturingOuter: 3, legacyBaseline: true },
  { name: 'cross-no-process', templateId: 'assembly-cross-fixture', changes: {}, noProcess: true, expectedParts: 2 },
  { name: 'two-weld', changes: { faceType: 'two', frameManufacturingMode: 'segment_weld' }, displayOuter: 7, manufacturingOuter: 7, legacyBaseline: true },
  { name: 'two-plane', changes: { faceType: 'two', frameManufacturingMode: 'plane_v_notch' }, displayOuter: 7, manufacturingOuter: 5, legacyBaseline: true },
  { name: 'five-continuous', changes: { faceType: 'five', frameManufacturingMode: 'spatial_v_notch' }, displayOuter: 12, manufacturingOuter: 6, legacyBaseline: true },
];
async function runManufacturingUpdateScenario(name = 'single-weld-update') {
  const threeFace = name === 'three-route-update';
  const initialMode = threeFace ? 'plane_v_notch' : 'segment_weld';
  const updatedMode = threeFace ? 'spatial_v_notch' : 'plane_v_notch';
  const initialParts = threeFace ? 25 : 11, updatedPartCount = threeFace ? 22 : 8;
  const displayOuter = threeFace ? 10 : 4;
  const native = connection();
  try {
    const templateId = 'single-face-security-window';
    const descriptor = (await native.invoke('GetTemplateDescriptor', { templateId })).template;
    const parameters = { ...Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue])),
      accessDoorEnabled: false, verticalMaximumCenterSpacing: 600, sideVerticalMaximumCenterSpacing: 600,
      topBottomRodMaximumCenterSpacing: 600, faceType: threeFace ? 'three' : 'single', frameManufacturingMode: initialMode };
    const generated = await native.invoke('GeneratePreview', { templateId, ...parameters });
    const productEntityId = generated.tubeDesigner.product.entityId;
    const finished = memberState(generated.tubeDesigner);
    const before = await native.invoke('InspectScriptResources');
    const display = inspectResources(before.display, 'display');
    assert.deepEqual(before.generatedParameters, parameters);
    assert.equal(before.display.items.filter(item => item.key.startsWith('outer_frame.')).length, displayOuter);

    const initial = await native.invoke('Disassemble', { productEntityIds: [productEntityId] });
    assert.deepEqual(memberState(initial.tubeDesigner), finished);
    assert.equal(initial.tubeDesigner.parts.length, initialParts);
    const initialSnapshot = await native.invoke('InspectScriptResources');
    const initialManufacturing = inspectResources(initialSnapshot.manufacturing, 'manufacturing');
    assert.equal(initialSnapshot.display.document.items.filter(item => item.key.startsWith('outer_frame.')).length, displayOuter);
    assert.equal(initialSnapshot.manufacturingExecution.document.items.filter(item => item.key.startsWith('outer_frame.')).length,
      threeFace ? 6 : 4);
    assert.deepEqual(initialSnapshot.manufacturingParameters, parameters);
    const initialOutputSet = inspectAssemblyOutputSet(initialSnapshot);
    assert.equal(initialOutputSet.physicalParts, initialParts);
    const initialShapes = await inspectManufacturedShapes(native, true);
    let initialBaselineShapes, initialBaselinePartFacts;
    if (threeFace) {
      const reference = generateWindowLegacyBaseline(`${name}-initial`, templateId, descriptor, parameters);
      initialBaselinePartFacts = await compareWindowPartFacts(native, templateId, parameters,
        initialSnapshot.manufacturingExecution.document, reference.baseline);
      initialBaselineShapes = await compareBaselineShapes(native, reference.baseline);
    }
    const firstReopened = await native.invoke('SaveAndReopen');
    assert.deepEqual(memberState(firstReopened.tubeDesigner), finished);
    assert.deepEqual(await native.invoke('InspectScriptResources'), initialSnapshot);
    assert.deepEqual(await inspectManufacturedShapes(native, true), initialShapes,
      'Save/reopen must preserve the exact manufactured and canonical BRep resources');

    const manufacturingParameters = { ...parameters, frameManufacturingMode: updatedMode };
    const updated = await native.invoke('Disassemble', { productEntityIds: [productEntityId],
      productParametersByEntityId: { [productEntityId]: manufacturingParameters } });
    assert.deepEqual(memberState(updated.tubeDesigner), finished);
    assert.equal(updated.tubeDesigner.parts.length, updatedPartCount);
    assert.equal(activeInstance(updated.tubeDesigner).expectedPartCount, updatedPartCount);
    assert.equal(activeInstance(updated.tubeDesigner).manufacturingPartCountKnown, true);
    assert.ok(updated.geometryChecks.every(check => check.valid && check.solids === 1 && check.volume > 0));
    const updatedSnapshot = await native.invoke('InspectScriptResources');
    assert.deepEqual(updatedSnapshot.display, before.display);
    assert.deepEqual(updatedSnapshot.generatedParameters, before.generatedParameters);
    assert.deepEqual(updatedSnapshot.manufacturingParameters, manufacturingParameters);
    const updatedManufacturing = inspectResources(updatedSnapshot.manufacturing, 'manufacturing');
    assert.equal(updatedSnapshot.display.document.items.filter(item => item.key.startsWith('outer_frame.')).length, displayOuter);
    assert.deepEqual(stableManufacturingInput(updatedSnapshot.manufacturing),
      stableManufacturingInput(initialSnapshot.manufacturing),
      'Route changes must leave design identities, sections, positions and connections identical');
    assert.equal(updatedSnapshot.manufacturingExecution.document.items.filter(item => item.key.startsWith('outer_frame.')).length,
      threeFace ? 3 : 1);
    assert.notDeepEqual(updatedSnapshot.manufacturing, initialSnapshot.manufacturing);
    assert.notDeepEqual(updatedSnapshot.manufacturingExecution, initialSnapshot.manufacturingExecution);
    assert.equal(updatedSnapshot.manufacturingExecution.schema, 'icax.neutral-model');
    const updatedOutputSet = inspectAssemblyOutputSet(updatedSnapshot);
    assert.equal(updatedOutputSet.physicalParts, updatedPartCount);
    assert.equal(updatedOutputSet.designMembers, initialOutputSet.designMembers);
    const processes = updatedSnapshot.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessSource'].instances;
    if (!threeFace) {
      assert.equal(processes.filter(process => process.templateId === 'tube-profile-aperture'
        && process.targets.stock === 'outer_frame.continuous.0001').length, 15,
      'Contact apertures must include both portions of the hole crossing the stock seam');
      assert.equal(processes.filter(process => process.templateId === 'tube-profile-aperture').length, 27);
      assert.equal(processes.filter(process => process.templateId === 'tube-end-joint').length, 14);
      assert.equal(processes.filter(process => process.templateId === 'node-v-notch-integrated').length, 4);
    }
    assert.deepEqual(updatedSnapshot.manufacturingExecution.document.parameters, manufacturingParameters);
    const updatedShapes = await inspectManufacturedShapes(native, true);
    const updatedParts = partState(updated.tubeDesigner);
    const secondReopened = await native.invoke('SaveAndReopen');
    assert.deepEqual(memberState(secondReopened.tubeDesigner), finished);
    assert.deepEqual(partState(secondReopened.tubeDesigner), updatedParts);
    assert.deepEqual(await native.invoke('InspectScriptResources'), updatedSnapshot);
    assert.deepEqual(await inspectManufacturedShapes(native, true), updatedShapes);

    const stateBeforeIllegalUpdate = (await native.invoke('List')).tubeDesigner;
    const illegalParameters = { ...manufacturingParameters, width: Number(manufacturingParameters.width) + 1 };
    await assert.rejects(native.invoke('Disassemble', { productEntityIds: [productEntityId],
      productParametersByEntityId: { [productEntityId]: illegalParameters } }), /会改变产品模型的参数: width/);
    assert.deepEqual(await native.invoke('InspectScriptResources'), updatedSnapshot,
      'Rejected design parameter updates must leave both raw definitions and all host snapshots untouched');
    assert.deepEqual((await native.invoke('List')).tubeDesigner, stateBeforeIllegalUpdate,
      'Rejected design parameter updates must leave display members and manufactured parts untouched');
    let baselineShapes, baselinePartFacts;
    try {
      const reference = generateWindowLegacyBaseline(name, templateId, descriptor, manufacturingParameters);
      baselinePartFacts = await compareWindowPartFacts(native, templateId, manufacturingParameters,
        updatedSnapshot.manufacturingExecution.document, reference.baseline);
      baselineShapes = await compareBaselineShapes(native, reference.baseline);
    } catch (error) {
      writeFileSync(resolve(output, `product-v4-resources-native-${name}-failure.json`),
        JSON.stringify({ error: error.message, comparison: error.comparison, parameters, manufacturingParameters,
          updatedSnapshot, savedFrozenBRepPreserved: true, illegalDesignUpdateRejectedWithoutSideEffects: true }, null, 2));
      throw error;
    }

    const report = { name, display, initialManufacturing, updatedManufacturing,
      initialParts, updatedParts: updatedPartCount, displayAndGeneratedInputUnchanged: true,
      designInputStableAcrossManufacturingRoutes: true, manufacturingSetGeneratedDownstream: true,
      initialOutputSet, updatedOutputSet,
      manufacturingInputAndDefinitionUpdated: true, hostExecutionSnapshotUpdated: true,
      updatedManufacturingSavedAndReopened: true, exactFrozenBRepPreserved: true, baselineShapes, baselinePartFacts,
      ...(threeFace ? { initialBaselineShapes, initialBaselinePartFacts } : {}),
      illegalDesignUpdateRejectedWithoutSideEffects: true };
    reports.push(report); console.log(JSON.stringify(report));
  } finally { native.close(); }
}

async function runSharedReferencesScenario() {
  const native = connection();
  try {
    const templateId = 'assembly-cross-fixture';
    const descriptor = (await native.invoke('GetTemplateDescriptor', { templateId })).template;
    const parameters = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
    await native.invoke('GeneratePreview', { templateId, ...parameters });
    await native.invoke('Disassemble');
    const snapshot = await native.invoke('InspectScriptResources');
    inspectResources(snapshot.display, 'display', false);
    inspectResources(snapshot.manufacturing, 'manufacturing');
    const designModel = snapshot.display.document, manufacturingDefinition = snapshot.manufacturing.document;
    const request = { designModel, manufacturingDefinition };
    const frozen = structuredClone(request);
    const valid = await native.invoke('ValidateSharedManufacturing', request);
    assert.deepEqual(valid, { valid: true, designItemCount: designModel.items.length, manufacturingPartCountKnown: false });
    assert.deepEqual(request, frozen);
    const keys = designModel.items.map(item => item.key);
    assert.ok(keys.length >= 2);
    const failures = ['unknown-connection-item', 'unknown-process-item', 'unknown-anchor-item',
      'duplicate-connection', 'same-connection-participant', 'namespace-occupies-design-item', 'missing-host-design'];
    const fixedRequest = structuredClone(request);
    fixedRequest.manufacturingDefinition.processes = [{ key: 'reference.fixture', kind: 'assembly-process', definition: {
      templateId: 'structural-stock-fit', processInput: { schema: 'icax.assembly-process-input', schemaVersion: 2,
        parts: { stock: { scope: 'manufacturing', itemKey: keys[0], state: 'initial' } }, geometry: {} },
      parameters: {}, processDrafts: {}, dependencies: [], targets: { stock: keys[0] },
    } }];
    assert.deepEqual(await native.invoke('ValidateSharedManufacturing', fixedRequest), valid,
      'The fixed-process reference fixture must be valid before its item references are changed');
    for (const failure of failures.filter(name => name !== 'namespace-occupies-design-item')) {
      const invalid = structuredClone(request), raw = invalid.manufacturingDefinition;
      if (failure === 'unknown-connection-item') raw.connections[0].items[1] = 'absent';
      else if (failure === 'unknown-anchor-item') raw.connections[0].properties.participantAnchors = [{ itemKey: 'absent' }];
      else if (failure === 'duplicate-connection') raw.connections.push(structuredClone(raw.connections[0]));
      else if (failure === 'same-connection-participant') raw.connections[0].items[1] = raw.connections[0].items[0];
      else if (failure === 'missing-host-design') delete invalid.designModel;
      else {
        raw.processes = structuredClone(fixedRequest.manufacturingDefinition.processes);
        raw.processes[0].definition.processInput.parts.stock.itemKey = 'absent';
        raw.processes[0].definition.targets.stock = 'absent';
      }
      const unchanged = structuredClone(invalid);
      await assert.rejects(native.invoke('ValidateSharedManufacturing', invalid), undefined, `Must reject ${failure}`);
      assert.deepEqual(invalid, unchanged);
      assert.deepEqual(await native.invoke('InspectScriptResources'), snapshot,
        `${failure} must leave host definitions and all saved execution snapshots unchanged`);
    }
    const windowId = 'single-face-security-window';
    const windowDescriptor = (await native.invoke('GetTemplateDescriptor', { templateId: windowId })).template;
    const windowParameters = { ...Object.fromEntries(windowDescriptor.parameters.map(field => [field.key, field.defaultValue])),
      accessDoorEnabled: false, verticalMaximumCenterSpacing: 600, sideVerticalMaximumCenterSpacing: 600,
      topBottomRodMaximumCenterSpacing: 600 };
    await native.invoke('GeneratePreview', { templateId: windowId, ...windowParameters });
    await native.invoke('Disassemble');
    const windowSnapshot = await native.invoke('InspectScriptResources');
    inspectResources(windowSnapshot.display, 'display');
    inspectResources(windowSnapshot.manufacturing, 'manufacturing');
    const windowRequest = { designModel: windowSnapshot.display.document,
      manufacturingDefinition: windowSnapshot.manufacturing.document };
    assert.deepEqual(await native.invoke('ValidateSharedManufacturing', windowRequest),
      { valid: true, designItemCount: windowRequest.designModel.items.length, manufacturingPartCountKnown: false });
    const invalidNamespace = structuredClone(windowRequest);
    invalidNamespace.manufacturingDefinition.processes[0].definition.outputs.manufacturing.key
      = windowRequest.designModel.items[0].key;
    const unchangedNamespace = structuredClone(invalidNamespace);
    await assert.rejects(native.invoke('ValidateSharedManufacturing', invalidNamespace), undefined,
      'Must reject a real window output namespace occupying a shared design item key');
    assert.deepEqual(invalidNamespace, unchangedNamespace);
    assert.deepEqual(await native.invoke('InspectScriptResources'), windowSnapshot);
    const report = { name: 'shared-references', validSharedDesign: true, geometryDefinedOnlyByDisplay: true,
      connectionsDefinedOnlyByManufacturing: true, unresolvedInputCountNotTreatedAsPartCount: true,
      rejectedInvalidReferences: failures, rejectedWithoutMutatingHostSnapshots: true };
    reports.push(report); console.log(JSON.stringify(report));
  } finally { native.close(); }
}
try {
  if (process.env.ICAX_SCRIPT_RESOURCES_CASE === 'shared-references') await runSharedReferencesScenario();
  if (process.env.ICAX_SCRIPT_RESOURCES_CASE === 'single-weld-update') await runManufacturingUpdateScenario();
  if (process.env.ICAX_SCRIPT_RESOURCES_CASE === 'three-route-update') await runManufacturingUpdateScenario('three-route-update');
  for (const scenario of scenarios) {
    if (process.env.ICAX_SCRIPT_RESOURCES_CASE && process.env.ICAX_SCRIPT_RESOURCES_CASE !== scenario.name) continue;
    const native = connection();
    try {
      const templateId = scenario.templateId || 'single-face-security-window';
      const descriptor = (await native.invoke('GetTemplateDescriptor', { templateId })).template;
      const parameters = { ...Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue])),
        ...(scenario.noProcess ? {} : { accessDoorEnabled: false, verticalMaximumCenterSpacing: 600,
          sideVerticalMaximumCenterSpacing: 600, topBottomRodMaximumCenterSpacing: 600 }), ...scenario.changes };
      const reference = scenario.legacyBaseline
        ? generateWindowLegacyBaseline(scenario.name, templateId, descriptor, parameters) : undefined;
      const preview = await native.invoke('GenerateProductTemplatePreview', { templateId, parameters });
      assert.deepEqual(preview.parameters, parameters);
      const generated = await native.invoke('GeneratePreview', { templateId, ...parameters });
      const finished = memberState(generated.tubeDesigner);
      const before = await native.invoke('InspectScriptResources');
      assert.deepEqual(before.generatedParameters, parameters,
        'The host must own the generated input snapshot outside the pure display document');
      const display = inspectResources(before.display, 'display', !scenario.noProcess);
      if (scenario.noProcess) {
        assert.equal(display.items, 2);
        assert.deepEqual(before.manufacturing, {});
        const initial = activeInstance(generated.tubeDesigner);
        assert.equal(initial.partCount, 0);
        assert.equal(initial.expectedPartCount, 0);
        assert.equal(initial.manufacturingPartCountKnown, false);
        const connections = await native.invoke('GetProductAssemblyConnections', {
          productEntityId: generated.tubeDesigner.product.entityId, generationRunId: before.generationRunId });
        assert.equal(connections.connections.length, 1);
        assert.deepEqual(connections.manufacturingStocks, []);
        assert.deepEqual(await native.invoke('InspectScriptResources'), before,
          'Geometric connection inspection must not create a manufacturing snapshot');
      } else {
        assert.equal(before.display.items.filter(item => item.key.startsWith('outer_frame.')).length, scenario.displayOuter);
      }
      const disassembled = await native.invoke('Disassemble');
      assert.deepEqual(memberState(disassembled.tubeDesigner), finished);
      assert.ok(disassembled.geometryChecks.every(check => check.valid && check.solids === 1 && check.volume > 0));
      const after = await native.invoke('InspectScriptResources');
      assert.deepEqual(after.display, before.display);
      assert.deepEqual(after.generatedParameters, before.generatedParameters);
      assert.deepEqual(after.manufacturingParameters, parameters,
        'The host must freeze manufacturing input independently of the script definition');
      const manufacturing = inspectResources(after.manufacturing, 'manufacturing');
      assert.equal(after.manufacturingExecution.schema, 'icax.neutral-model');
      assert.deepEqual(after.manufacturingExecution.document.parameters, after.manufacturingParameters);
      const shapes = await inspectManufacturedShapes(native, !scenario.noProcess);
      let baselineShapes, baselinePartFacts;
      if (reference) {
        baselinePartFacts = await compareWindowPartFacts(native, templateId, parameters,
          after.manufacturingExecution.document, reference.baseline);
        baselineShapes = await compareBaselineShapes(native, reference.baseline);
      }
      const manufactured = partState(disassembled.tubeDesigner);
      if (scenario.noProcess) {
        assert.equal(display.items, scenario.expectedParts);
        assert.equal(disassembled.geometryChecks.length, scenario.expectedParts);
        assert.equal(manufactured.length, scenario.expectedParts);
        assert.equal(after.manufacturing.booleanResources, 0);
        const current = activeInstance(disassembled.tubeDesigner);
        assert.equal(current.expectedPartCount, scenario.expectedParts);
        assert.equal(current.manufacturingPartCountKnown, true);
        assert.deepEqual(after.manufacturing.document.processes, []);
      } else {
        assert.equal(after.display.document.items.filter(item => item.key.startsWith('outer_frame.')).length, scenario.displayOuter,
          'The product must retain its design members independent of the manufacturing route');
        assert.equal(after.manufacturingExecution.document.items.filter(item => item.key.startsWith('outer_frame.')).length,
          scenario.manufacturingOuter, 'Only downstream assembly determines the physical manufacturing pieces');
        inspectAssemblyOutputSet(after);
        assert.equal(activeInstance(disassembled.tubeDesigner).expectedPartCount, manufactured.length);
        assert.equal(activeInstance(disassembled.tubeDesigner).manufacturingPartCountKnown, true);
      }
      const reopened = await native.invoke('SaveAndReopen');
      assert.deepEqual(memberState(reopened.tubeDesigner), finished);
      assert.deepEqual(partState(reopened.tubeDesigner), manufactured);
      assert.deepEqual(await native.invoke('InspectScriptResources'), after,
        'Saved projects must retain the exact pure display, resource placements and host input snapshot');
      assert.deepEqual(await inspectManufacturedShapes(native, !scenario.noProcess), shapes,
        'Save/reopen must preserve exact frozen geometry bytes, validity, volume and placement');
      const report = { name: scenario.name, display, manufacturing, finishedUnchanged: true,
        storedPureDisplayDocument: true, generatedParametersPreserved: true,
        storedPureManufacturingDocument: true, manufacturingParametersPreserved: true,
        hostExecutionSnapshotPreserved: true,
        noDuplicatedProductGeometry: true, connectionsOwnedByManufacturing: true,
        ...(!scenario.noProcess ? { generatedAssemblyOutputSetPreserved: true } : {}),
        savedReferencesAndPlacementsEqual: true, exactFrozenBRepPreserved: true,
        ...(reference ? { sameHostInputLegacyBaseline: reference.baselineFile, baselinePartFacts, baselineShapes } : {}),
        ...(scenario.noProcess ? { initialPartCountUnknown: true, connectionQueryDidNotManufacture: true,
          noProcessSnapshotPreserved: true } : {}) };
      reports.push(report); console.log(JSON.stringify(report));
    } finally { native.close(); }
  }
} finally {
  writeFileSync(resolve(output, `product-v4-resources-native-${process.env.ICAX_SCRIPT_RESOURCES_CASE || 'all'}.json`),
    JSON.stringify(reports, null, 2));
}
