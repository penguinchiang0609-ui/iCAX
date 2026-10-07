// Product choices must change actual branch ends and receiver walls while keeping the display generation.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCachedNeutralInspectionPool } from './CachedNeutralInspectionPool.mjs';
import { applyProductControlChoice, productControlFields, productControlValue } from '../../apps/tube-designer/webpage/productControls.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const runtime = resolve(process.env.ICAX_PRODUCT_BRANCH_RUNTIME || resolve(root, 'src/x64/Debug'));
const deployed = process.env.ICAX_PRODUCT_BRANCH_DEPLOYED === '1';
const previewOnly = process.env.ICAX_PRODUCT_BRANCH_PREVIEW_ONLY === '1';
const selectedCase = process.env.ICAX_PRODUCT_BRANCH_CASE || null;
assert.ok(selectedCase === null || ['insert-insert', 'weld-weld', 'tabs-tabs', 'tabs-insert', 'insert-tabs',
  'tabs-tabs-larger-clearance', 'opening-fixed-frame-receives-main-tabs-leaf-unchanged',
  'continuous-v-groove-frame-receives-round-branch-tabs'].includes(selectedCase), 'Unknown branch connection case');
const output = resolve(process.env.ICAX_PRODUCT_BRANCH_OUTPUT
  || resolve(root, `output/tests/branch-end-connections/${deployed ? 'deployed' : 'source'}-manufacturing-native.json`));
const directory = dirname(output), bridgeDirectory = resolve(directory, 'branch-bridge-runtime');
mkdirSync(bridgeDirectory, { recursive: true });
assert.deepEqual(readdirSync(bridgeDirectory).filter(name => /\.dll$/i.test(name)), [], 'The bridge must load current runtime DLLs');
const bridge = resolve(bridgeDirectory, 'SecurityWindowFramesBridge.exe');
copyFileSync(resolve(process.env.ICAX_PRODUCT_BRANCH_BRIDGE
  || resolve(root, 'output/tests/product-tab-connections/bridge-build/SecurityWindowFramesBridge.exe')), bridge);
const assets = resolve(deployed ? runtime : resolve(root, 'src'), 'apps/tube-designer');
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const report = { passed: false, deployed, previewOnly, selectedCase, runtime, assets, bridge, bridgeSha256: hash(bridge), cases: [] };
const save = () => writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
const artifact = (name, value) => { const path = resolve(directory, name);
  writeFileSync(path, JSON.stringify(value, null, 2) + '\n'); return path; };
const displayIdentity = state => state.members.map(member => ({ entityId: member.entityId, key: member.stableKey,
  resourceId: member.previewGeometryResourceId, version: member.previewGeometryResourceVersion,
  transform: member.transform })).sort((left, right) => left.key.localeCompare(right.key));
const near = (actual, expected, label) => assert.ok(Math.abs(actual - expected) < 0.002,
  `${label}: ${actual} differs from ${expected}`);
const dot = (first, second) => first.reduce((sum, value, index) => sum + value * second[index], 0);
function placement(part) {
  const sources = part.properties['manufacturing.sourceMembers'];
  assert.equal(sources.length, 1, `${part.stableKey}: one straight source`);
  assert.equal(sources[0].spans.length, 1);
  return sources[0].spans[0].placement;
}
function toLocal(part, world) {
  const transform = placement(part);
  return transform.origin.map((value, index) => value + world[0] * transform.xAxis[index]
    + world[1] * transform.yAxis[index] + world[2] * transform.zAxis[index]);
}
function toWorld(part, local) {
  const transform = placement(part), translated = local.map((value, index) => value - transform.origin[index]);
  return [transform.xAxis, transform.yAxis, transform.zAxis].map(axis => dot(axis, translated));
}
const native = createCachedNeutralInspectionPool({ bridgePath: bridge, runtimeRoot: deployed ? runtime : root,
  binaryRoot: runtime, workerCount: 1, timeoutMs: 300000 });
save();
try {
  const templateId = 'single-face-security-window';
  const template = (await native.invoke('GetTemplateDescriptor', { templateId })).template;
  const observed = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
    `(Get-Process -Id ${native.workerMetadata[0].pid}).Modules | Where-Object { $_.ModuleName -in @('TemplateRuntime.dll','TubeDesigner.dll') } | Select-Object ModuleName,FileName | ConvertTo-Json -Compress`],
  { windowsHide: true, encoding: 'utf8', timeout: 30000 });
  assert.equal(observed.status, 0, observed.stderr);
  const modules = JSON.parse(observed.stdout.trim());
  report.modules = (Array.isArray(modules) ? modules : [modules]).map(item => ({ name: item.ModuleName,
    path: item.FileName, sha256: hash(item.FileName) }));
  assert.equal(report.modules.length, 2);
  assert.ok(report.modules.every(item => item.path.toLowerCase().startsWith(runtime.toLowerCase() + '\\')));
  report.descriptorSha256 = hash(resolve(assets, 'templates/product/single_face_security_window/template.json'));
  report.manufacturingSha256 = hash(resolve(assets, 'templates/_shared/product_window_manufacturing.py'));
  report.sourceDigests = Object.fromEntries([
    '_shared/product_window_manufacturing.py', '_shared/security_window_product_contract.py',
    '_shared/assembly_window_process.py', '_shared/assembly_post_machining.py', '_shared/assembly_material_allocation.py',
    'mold/paired-end-tabs/tool.py', 'mold/paired-side-slots/tool.py',
  ].map(file => [file, hash(resolve(assets, 'templates', file))]));
  report.packageDigest = template.packageDigest;
  const controls = productControlFields(template), defaults = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
  for (const axis of ['horizontal', 'vertical']) {
    assert.equal(defaults[`${axis}EndConnection`], 'insert');
    assert.deepEqual(template.parameters.find(field => field.key === `${axis}EndConnection`).choices.map(choice => choice.value),
      ['insert', 'weld', 'tabs']);
  }
  assert.equal(defaults.assemblyClearance, 0.1);
  const baseValues = { ...defaults, faceType: 'single', width: 600, height: 800, accessDoorEnabled: false,
    frameManufacturingMode: 'segment_weld', foldedPostJoint: 'weld', frameJoinType: 'miter_45',
    verticalMaximumCenterSpacing: 600, horizontalMaximumCenterSpacing: 1000 };
  const generated = await native.invoke('GeneratePreview', { templateId, ...baseValues });
  const productId = generated.tubeDesigner.product.entityId, runId = generated.tubeDesigner.product.activeGenerationRunId;
  const display = displayIdentity(generated.tubeDesigner);
  assert.deepEqual(generated.tubeDesigner.product.parameters, baseValues);
  let previousGapVolume;
  for (const scenario of [
    { name: 'insert-insert', horizontal: 'insert', vertical: 'insert', gap: 0.1 },
    { name: 'weld-weld', horizontal: 'weld', vertical: 'weld', gap: 0.1 },
    { name: 'tabs-tabs', horizontal: 'tabs', vertical: 'tabs', gap: 0.1 },
    { name: 'tabs-insert', horizontal: 'tabs', vertical: 'insert', gap: 0.1 },
    { name: 'insert-tabs', horizontal: 'insert', vertical: 'tabs', gap: 0.1 },
    { name: 'tabs-tabs-larger-clearance', horizontal: 'tabs', vertical: 'tabs', gap: 0.4 },
  ]) {
    if (selectedCase && scenario.name !== selectedCase
      && !(selectedCase === 'tabs-tabs-larger-clearance' && scenario.name === 'tabs-tabs')) continue;
    const entry = { ...scenario, passed: false, assertions: [] }, started = Date.now();
    report.cases.push(entry); save();
    try {
      let values = { ...baseValues, assemblyClearance: scenario.gap };
      for (const axis of ['horizontal', 'vertical']) {
        const control = controls.find(field => field.key === `${axis}EndConnectionChoice`); assert.ok(control);
        const previous = values, before = structuredClone(values);
        values = applyProductControlChoice({}, template, control, values, scenario[axis]);
        assert.deepEqual(previous, before);
        assert.equal(productControlValue(template, control, values), scenario[axis]);
        assert.equal(Object.hasOwn(values, control.key), false);
      }
      const input = { templateId, parameters: values }, frozen = structuredClone(input);
      const preview = await native.invoke('GenerateProductTemplatePreview', input);
      assert.deepEqual(input, frozen); assert.deepEqual(preview.parameters, values);
      const updated = await native.invoke('UpdateProductParameters', { productEntityId: productId, parameters: values });
      assert.equal(updated.tubeDesigner.product.modelOutdated, false);
      assert.equal(updated.tubeDesigner.product.activeGenerationRunId, runId);
      assert.deepEqual(displayIdentity(updated.tubeDesigner), display);
      if (previewOnly) {
        entry.previewParametersEchoed = true; entry.displayGenerationAndResourcesPreserved = true;
        entry.actualManufacturingExecuted = false; entry.passed = true; continue;
      }
      if (scenario.name !== 'insert-insert') assert.equal(updated.tubeDesigner.product.partsOutdated, true);
      const disassembled = await native.invoke('Disassemble', { productEntityIds: [productId],
        productParametersByEntityId: { [productId]: values } });
      assert.deepEqual(displayIdentity(disassembled.tubeDesigner), display);
      assert.equal(disassembled.tubeDesigner.product.activeGenerationRunId, runId);
      assert.ok(disassembled.geometryChecks.every(check => check.valid && check.solids === 1 && check.volume > 0));
      entry.disassemblyPath = artifact(`${scenario.name}-disassembled.json`, disassembled);
      const scripts = await native.invoke('InspectScriptResources');
      assert.deepEqual(scripts.generatedParameters, baseValues);
      assert.deepEqual(scripts.manufacturingParameters, values);
      entry.scriptResourcesPath = artifact(`${scenario.name}-script-resources.json`, scripts);
      const parts = disassembled.tubeDesigner.parts;
      const part = key => { const found = parts.find(row => row.stableKey === key); assert.ok(found, key); return found; };
      const branches = { horizontal: part('main_grid.horizontal.0001'), vertical: part('main_grid.vertical.0001') };
      assert.equal(branches.vertical.profile.kind, 'round'); near(branches.vertical.profile.width, 19, 'Actual round branch diameter');
      const hosts = { horizontal: [part('outer_frame.left.0001'), part('outer_frame.right.0001')],
        vertical: [part('outer_frame.bottom.0001'), part('outer_frame.top.0001')] };
      const inspected = await native.invoke('InspectNativeGeometry', { entityIds: [...new Set([...Object.values(branches),
        ...Object.values(hosts).flat()].map(row => row.entityId))] });
      const bounds = key => inspected.geometryChecks.find(check => check.key === key).bounds;
      const probes = [];
      const add = (target, point, expected, role) => probes.push({ entityId: target.entityId, key: target.stableKey, point, expected, role });
      for (const axis of ['horizontal', 'vertical']) {
        const branch = branches[axis], actual = bounds(branch.stableKey), wall = branch.profile.wallThickness;
        const mode = scenario[axis], dimension = axis === 'horizontal' ? 'width' : 'height', worldAxis = axis === 'horizontal' ? 0 : 2;
        const clearSpan = values[dimension] - 2 * values.frameWidth;
        const extension = mode === 'weld' ? 0 : mode === 'tabs' ? (axis === 'horizontal' ? 12 : 8) : values[`${axis}BranchReserve`];
        near(branch.length, clearSpan + 2 * extension, `${axis}/${mode} actual part length`);
        near(actual[1] - actual[0], clearSpan + 2 * extension, `${axis}/${mode} actual BRep axial length`);
        const yCenter = (actual[2] + actual[3]) / 2, zCenter = (actual[4] + actual[5]) / 2;
        const radius = branch.profile.depth / 2, skin = radius - wall / 2;
        const tongueWidth = axis === 'horizontal' ? 8 : 6;
        for (let end = 0; end < 2; end++) {
          const host = hosts[axis][end], plane = end === 0 ? values.frameWidth : values[dimension] - values.frameWidth;
          const localTip = end === 0 ? actual[0] + 2 : actual[1] - 2;
          add(branch, [localTip, yCenter, zCenter + skin], 'inside', `${axis}-end-${end}-tongue-or-complete-skin-retained`);
          add(branch, [localTip, yCenter + skin, zCenter], mode === 'tabs' ? 'outside' : 'inside',
            `${axis}-end-${end}-other-skin-${mode === 'tabs' ? 'trimmed' : 'retained'}`);
          const localContact = toLocal(branch, toWorld(branch, [localTip, yCenter, zCenter]));
          const worldContact = toWorld(branch, localContact); worldContact[worldAxis] = plane;
          const localPlane = toLocal(branch, worldContact)[0];
          for (const sign of [-1, 1]) {
            const onSlot = toWorld(branch, [localPlane, yCenter, zCenter + sign * skin]);
            onSlot[worldAxis] = plane + (end === 0 ? -1 : 1) * host.profile.wallThickness / 2;
            add(host, toLocal(host, onSlot), mode === 'weld' ? 'inside' : 'outside', `${axis}-end-${end}-receiver-${mode}`);
            const farWall = [...onSlot];
            farWall[worldAxis] = end === 0 ? host.profile.wallThickness / 2 : values[dimension] - host.profile.wallThickness / 2;
            add(host, toLocal(host, farWall), 'inside', `${axis}-end-${end}-opposite-wall-retained`);
          }
          if (mode === 'tabs') {
            const center = [...worldContact]; center[worldAxis] += (end === 0 ? -1 : 1) * host.profile.wallThickness / 2;
            add(host, toLocal(host, center), 'inside', `${axis}-end-${end}-bridge-between-mother-slots`);
            if (branch.profile.kind !== 'round') {
              const capsuleHalfLength = tongueWidth / 2 + wall / 2 + scenario.gap;
              for (const [distance, expected, role] of [[capsuleHalfLength - 0.05, 'outside', 'rounded-socket-tip-cut'],
                [capsuleHalfLength + 0.05, 'inside', 'beyond-rounded-socket-tip-retained']]) {
                const tip = toWorld(branch, [localPlane, yCenter + distance, zCenter + skin]);
                tip[worldAxis] = plane + (end === 0 ? -1 : 1) * host.profile.wallThickness / 2;
                add(host, toLocal(host, tip), expected, `${axis}-end-${end}-${role}`);
              }
            }
          }
          if (mode !== 'weld') for (const [extra, expected, role] of [[scenario.gap / 2, 'outside', 'single-side-clearance-cut'],
            [scenario.gap + 0.05, 'inside', 'beyond-clearance-retained']]) {
            // The existing square socket has rounded long-axis relief beyond the tab root.
            // Its mating gap is measured across the ear thickness; the round socket also clips its curved band width.
            const roundTabs = mode === 'tabs' && branch.profile.kind === 'round';
            const y = roundTabs ? tongueWidth / 2 + extra : 0;
            const z = roundTabs ? Math.sqrt(skin * skin - y * y) : radius + extra;
            const world = toWorld(branch, [localPlane, yCenter + y, zCenter + z]);
            world[worldAxis] = plane + (end === 0 ? -1 : 1) * host.profile.wallThickness / 2;
            add(host, toLocal(host, world), expected, `${axis}-end-${end}-${role}`);
          }
        }
      }
      // A vertical branch still crosses both skins of an interior horizontal member for every end choice.
      const horizontal = branches.horizontal, vertical = branches.vertical;
      const horizontalCenter = toWorld(horizontal, [(bounds(horizontal.stableKey)[0] + bounds(horizontal.stableKey)[1]) / 2, 0, 0]);
      const verticalCenter = toWorld(vertical, [(bounds(vertical.stableKey)[0] + bounds(vertical.stableKey)[1]) / 2, 0, 0]);
      for (const sign of [-1, 1]) add(horizontal, toLocal(horizontal, [verticalCenter[0] + vertical.profile.width / 2 - vertical.profile.wallThickness / 2,
        verticalCenter[1], horizontalCenter[2] + sign * (horizontal.profile.width / 2 - horizontal.profile.wallThickness / 2)]),
      'outside', 'middle-through-hole-survives-end-choice');
      entry.probes = probes;
      const final = await native.invoke('InspectNativeGeometry', { entityIds: [...new Set(probes.map(probe => probe.entityId))],
        points: probes.map(({ entityId, point }) => ({ entityId, point })) });
      entry.finalProbePath = artifact(`${scenario.name}-final-brep-probes.json`, final);
      const stocks = [...new Set(probes.map(probe => probe.key))].map(key => {
        const stock = final.canonicalStockResources.find(row => row.stockId === key && row.kind === 'baseGeometry'); assert.ok(stock, key);
        return { key, url: stock.url, version: stock.version, points: probes.filter(probe => probe.key === key).map(probe => probe.point) };
      });
      const stockInspection = await native.invoke('InspectBRepResources', { resources: stocks });
      entry.stockProbePath = artifact(`${scenario.name}-canonical-stock-probes.json`, stockInspection);
      for (const check of stockInspection.geometryChecks) for (const [index, actual] of check.pointChecks.entries()) {
        const probe = probes.filter(row => row.key === check.key)[index];
        assert.equal(actual.inside, true, `${scenario.name}/${probe.role} must begin in the actual uncut stock`);
      }
      for (const check of final.geometryChecks) for (const [index, actual] of check.pointChecks.entries()) {
        const probe = probes.filter(row => row.entityId === check.entityId)[index];
        entry.assertions.push({ ...probe, actual, passed: actual[probe.expected] === true });
      }
      assert.ok(entry.assertions.length > 0 && entry.assertions.every(item => item.passed),
        `${scenario.name}: actual branch-end/receiver material proof failed`);
      const hostVolume = final.geometryChecks.find(check => check.key === 'outer_frame.left.0001').volume;
      if (scenario.name === 'tabs-tabs') previousGapVolume = hostVolume;
      if (scenario.name === 'tabs-tabs-larger-clearance') assert.ok(hostVolume < previousGapVolume,
        'Increasing the one-sided mating clearance must remove additional receiver material');
      entry.previewParametersEchoed = true; entry.displayGenerationAndResourcesPreserved = true;
      entry.actualManufacturingExecuted = true;
      entry.actualRoundTongueAdaptation = scenario.vertical === 'tabs'; entry.passed = true;
    } catch (error) { entry.error = String(error.stack || error); throw error; }
    finally { entry.milliseconds = Date.now() - started; save();
      console.log(JSON.stringify({ name: entry.name, passed: entry.passed, milliseconds: entry.milliseconds,
        probes: entry.assertions.length, error: entry.error })); }
  }
  if (!previewOnly && (!selectedCase || selectedCase === 'opening-fixed-frame-receives-main-tabs-leaf-unchanged')) {
    const entry = { name: 'opening-fixed-frame-receives-main-tabs-leaf-unchanged', passed: false, assertions: [] };
    report.cases.push(entry); save();
    const started = Date.now();
    try {
      const doorBaselineValues = { ...defaults, faceType: 'single', accessDoorEnabled: true,
        frameManufacturingMode: 'segment_weld', foldedPostJoint: 'weld', frameJoinType: 'miter_45',
        verticalMaximumCenterSpacing: 600, doorVerticalMaximumCenterSpacing: 600,
        horizontalEndConnection: 'weld', verticalEndConnection: 'weld' };
      const doorGenerated = await native.invoke('GeneratePreview', { templateId, ...doorBaselineValues });
      const doorId = doorGenerated.tubeDesigner.product.entityId, doorRun = doorGenerated.tubeDesigner.product.activeGenerationRunId;
      const doorDisplay = displayIdentity(doorGenerated.tubeDesigner);
      const baseline = await native.invoke('Disassemble', { productEntityIds: [doorId] });
      entry.baselinePath = artifact('opening-weld-baseline-disassembled.json', baseline);
      const leafBefore = baseline.tubeDesigner.parts.filter(part => part.stableKey.startsWith('access_door.leaf.'));
      assert.ok(leafBefore.length > 0);
      const leafPoints = leafBefore.flatMap(part => {
        const profile = part.profile, wall = profile.wallThickness;
        const radius = (profile.diameter ?? profile.width) / 2;
        const shell = ['circle', 'round'].includes(profile.kind)
          ? Array.from({ length: 8 }, (_, index) => { const angle = index * Math.PI / 4;
            return [Math.cos(angle) * (radius - wall / 2),
              Math.sin(angle) * (radius - wall / 2)]; })
          : [[0, profile.depth / 2 - wall / 2], [0, -profile.depth / 2 + wall / 2],
            [profile.width / 2 - wall / 2, 0], [-profile.width / 2 + wall / 2, 0]];
        return [0.2, part.length / 2, part.length - 0.2].flatMap(station =>
          [...shell, [0, 0]].map(([y, z]) => ({ key: part.stableKey,
            entityId: part.entityId, point: [station, y, z] })));
      });
      const leafGeometryBefore = await native.invoke('InspectNativeGeometry', {
        entityIds: leafBefore.map(part => part.entityId), points: leafPoints });
      entry.leafBeforePath = artifact('opening-leaf-before.json', leafGeometryBefore);
      const leafPlan = scripts => {
        const plan = scripts.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessNative'].plan;
        const operations = plan.operations.filter(operation => operation.stockId.startsWith('access_door.leaf.'));
        const ids = new Set(operations.map(operation => operation.instanceId));
        return { operations, instances: plan.instances.filter(instance => ids.has(instance.instanceId)) };
      };
      const leafPlanBefore = leafPlan(await native.invoke('InspectScriptResources'));
      entry.leafPlanBeforePath = artifact('opening-leaf-plan-before.json', leafPlanBefore);
      const values = { ...doorBaselineValues, horizontalEndConnection: 'tabs', verticalEndConnection: 'tabs' };
      const preview = await native.invoke('GenerateProductTemplatePreview', { templateId, parameters: values });
      assert.deepEqual(preview.parameters, values);
      const updated = await native.invoke('UpdateProductParameters', { productEntityId: doorId, parameters: values });
      assert.equal(updated.tubeDesigner.product.modelOutdated, false);
      assert.deepEqual(displayIdentity(updated.tubeDesigner), doorDisplay);
      const actual = await native.invoke('Disassemble', { productEntityIds: [doorId], productParametersByEntityId: { [doorId]: values } });
      entry.disassemblyPath = artifact('opening-tabs-disassembled.json', actual);
      assert.deepEqual(displayIdentity(actual.tubeDesigner), doorDisplay);
      assert.equal(actual.tubeDesigner.product.activeGenerationRunId, doorRun);
      const part = key => { const found = actual.tubeDesigner.parts.find(row => row.stableKey === key); assert.ok(found, key); return found; };
      const leafAfter = actual.tubeDesigner.parts.filter(row => row.stableKey.startsWith('access_door.leaf.'));
      assert.deepEqual(leafAfter.map(row => row.stableKey).sort(), leafBefore.map(row => row.stableKey).sort());
      const leafGeometryAfter = await native.invoke('InspectNativeGeometry', { entityIds: leafAfter.map(row => row.entityId),
        points: leafPoints.map(point => ({ ...point,
          entityId: leafAfter.find(row => row.stableKey === point.key).entityId })) });
      entry.leafAfterPath = artifact('opening-leaf-after.json', leafGeometryAfter);
      for (const leaf of leafAfter) {
        const previous = leafBefore.find(row => row.stableKey === leaf.stableKey);
        near(leaf.length, previous.length, `${leaf.stableKey} unchanged length`);
        assert.deepEqual(leaf.profile, previous.profile, `${leaf.stableKey} unchanged section`);
        assert.deepEqual(leaf.properties, previous.properties, `${leaf.stableKey} unchanged complete manufacturing recipe`);
        const before = leafGeometryBefore.geometryChecks.find(row => row.key === leaf.stableKey);
        const after = leafGeometryAfter.geometryChecks.find(row => row.key === leaf.stableKey);
        // BRep serialization records may differ between identical Boolean runs. Check the complete
        // recipe and actual solid, including shell/cavity probes at both ends and the midpoint.
        assert.ok(before.valid && after.valid);
        assert.equal(after.solids, before.solids); assert.equal(after.solids, 1);
        after.bounds.forEach((value, index) => near(value, before.bounds[index], `${leaf.stableKey} unchanged bound ${index}`));
        near(after.volume, before.volume, `${leaf.stableKey} unchanged actual material volume`);
        assert.deepEqual(after.pointChecks, before.pointChecks, `${leaf.stableKey} unchanged end/body material`);
        assert.ok(after.pointChecks.some(point => point.inside));
        assert.ok(after.pointChecks.some(point => point.outside));
      }
      entry.leafMaterialProbeCount = leafPoints.length;
      const scripts = await native.invoke('InspectScriptResources');
      assert.deepEqual(scripts.generatedParameters, doorBaselineValues); assert.deepEqual(scripts.manufacturingParameters, values);
      const leafPlanAfter = leafPlan(scripts);
      entry.leafPlanAfterPath = artifact('opening-leaf-plan-after.json', leafPlanAfter);
      assert.deepEqual(leafPlanAfter, leafPlanBefore, 'All actual leaf machining operations and inputs remain unchanged');
      const branch = part('main_grid.horizontal.left.0001'), receiver = part('access_door.fixed_frame.left.0001');
      const receiverDesign = scripts.display.document.items.find(item => item.key === receiver.stableKey); assert.ok(receiverDesign);
      const receiverMember = receiverDesign.properties['assemblyFrame.member'];
      const plane = receiverMember.start[0] - receiver.profile.width / 2;
      const shape = await native.invoke('InspectNativeGeometry', { entityIds: [branch.entityId, receiver.entityId] });
      const branchBounds = shape.geometryChecks.find(row => row.key === branch.stableKey).bounds;
      const center = toWorld(branch, [(branchBounds[0] + branchBounds[1]) / 2, 0, 0]); center[0] = plane;
      const station = toLocal(branch, center)[0], skin = branch.profile.depth / 2 - branch.profile.wallThickness / 2;
      const probes = [];
      for (const sign of [-1, 1]) {
        const slot = toWorld(branch, [station, 0, sign * skin]); slot[0] = plane + receiver.profile.wallThickness / 2;
        probes.push({ key: receiver.stableKey, entityId: receiver.entityId, point: toLocal(receiver, slot),
          expected: 'outside', role: 'opening-fixed-frame-main-branch-mother-slot' });
        const far = [...slot]; far[0] = receiverMember.start[0] + receiver.profile.width / 2 - receiver.profile.wallThickness / 2;
        probes.push({ key: receiver.stableKey, entityId: receiver.entityId, point: toLocal(receiver, far),
          expected: 'inside', role: 'opening-fixed-frame-opposite-wall-retained' });
      }
      const final = await native.invoke('InspectNativeGeometry', { entityIds: [receiver.entityId],
        points: probes.map(({ entityId, point }) => ({ entityId, point })) });
      const stock = final.canonicalStockResources.find(row => row.stockId === receiver.stableKey && row.kind === 'baseGeometry'); assert.ok(stock);
      const stockInspection = await native.invoke('InspectBRepResources', { resources: [{ key: receiver.stableKey,
        url: stock.url, version: stock.version, points: probes.map(probe => probe.point) }] });
      entry.finalProbePath = artifact('opening-fixed-frame-final-probes.json', final);
      entry.stockProbePath = artifact('opening-fixed-frame-stock-probes.json', stockInspection);
      for (const [index, probe] of probes.entries()) {
        assert.equal(stockInspection.geometryChecks[0].pointChecks[index].inside, true, `${probe.role} must begin in actual stock`);
        const classification = final.geometryChecks[0].pointChecks[index];
        entry.assertions.push({ ...probe, actual: classification, passed: classification[probe.expected] === true });
      }
      assert.ok(entry.assertions.every(row => row.passed), 'Actual opening fixed-frame sockets must receive main branches');
      entry.leafGeometryAndLengthUnchanged = true; entry.displayGenerationAndResourcesPreserved = true;
      entry.actualManufacturingExecuted = true; entry.passed = true;
    } catch (error) { entry.error = String(error.stack || error); throw error; }
    finally { entry.milliseconds = Date.now() - started; save();
      console.log(JSON.stringify({ name: entry.name, passed: entry.passed, milliseconds: entry.milliseconds, error: entry.error })); }
  }
  if (!previewOnly && (!selectedCase || selectedCase === 'continuous-v-groove-frame-receives-round-branch-tabs')) {
    const entry = { name: 'continuous-v-groove-frame-receives-round-branch-tabs', passed: false };
    report.cases.push(entry); save();
    const started = Date.now();
    try {
      const foldedBaseline = { ...baseValues, frameManufacturingMode: 'plane_v_notch',
        horizontalEndConnection: 'insert', verticalEndConnection: 'insert' };
      const foldedGenerated = await native.invoke('GeneratePreview', { templateId, ...foldedBaseline });
      const foldedId = foldedGenerated.tubeDesigner.product.entityId, foldedRun = foldedGenerated.tubeDesigner.product.activeGenerationRunId;
      const foldedDisplay = displayIdentity(foldedGenerated.tubeDesigner);
      const values = { ...foldedBaseline, horizontalEndConnection: 'tabs', verticalEndConnection: 'tabs' };
      const preview = await native.invoke('GenerateProductTemplatePreview', { templateId, parameters: values });
      assert.deepEqual(preview.parameters, values);
      const updated = await native.invoke('UpdateProductParameters', { productEntityId: foldedId, parameters: values });
      assert.equal(updated.tubeDesigner.product.modelOutdated, false);
      assert.deepEqual(displayIdentity(updated.tubeDesigner), foldedDisplay);
      assert.equal(updated.tubeDesigner.product.activeGenerationRunId, foldedRun);
      const actual = await native.invoke('Disassemble', { productEntityIds: [foldedId],
        productParametersByEntityId: { [foldedId]: values } });
      entry.disassemblyPath = artifact('continuous-v-groove-tabs-disassembled.json', actual);
      assert.deepEqual(displayIdentity(actual.tubeDesigner), foldedDisplay);
      assert.equal(actual.tubeDesigner.product.activeGenerationRunId, foldedRun);
      assert.ok(actual.geometryChecks.length > 0);
      assert.ok(actual.geometryChecks.every(check => check.valid && check.solids === 1 && check.volume > 0));
      const outer = actual.tubeDesigner.parts.filter(part => part.stableKey.startsWith('outer_frame.'));
      assert.equal(outer.length, 1, 'The receiving frame must be the actual continuous unfolded stock');
      const scripts = await native.invoke('InspectScriptResources');
      assert.deepEqual(scripts.generatedParameters, foldedBaseline); assert.deepEqual(scripts.manufacturingParameters, values);
      entry.scriptResourcesPath = artifact('continuous-v-groove-tabs-script-resources.json', scripts);
      const plan = scripts.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessNative'].plan;
      const sockets = plan.operations.filter(operation => operation.stockId === outer[0].stableKey
        && operation.kind === 'neutral-csg' && operation.operation === 'subtract'
        && operation.tool === 'post-aperture.cropped'
        && plan.instances.some(instance => instance.instanceId === operation.instanceId
          && instance.templateId === 'tube-post-aperture' && instance.parameters.joint === 'tabs'
          && instance.processInput.parts.branch.id.startsWith('main_grid.vertical.')));
      assert.ok(sockets.length > 0, 'Actual folded receiver operations must include the curved-wall round sockets');
      entry.curvedRoundSocketOperations = sockets; entry.geometryChecks = actual.geometryChecks;
      entry.displayGenerationAndResourcesPreserved = true; entry.actualManufacturingExecuted = true; entry.passed = true;
    } catch (error) { entry.error = String(error.stack || error); throw error; }
    finally { entry.milliseconds = Date.now() - started; save();
      console.log(JSON.stringify({ name: entry.name, passed: entry.passed, milliseconds: entry.milliseconds, error: entry.error })); }
  }
  for (const [file, digest] of Object.entries(report.sourceDigests))
    assert.equal(hash(resolve(assets, 'templates', file)), digest, `Actual manufacturing source changed during validation: ${file}`);
  report.sourceBeforeAfterUnchanged = true;
  report.passed = true;
} catch (error) { report.error = String(error.stack || error); process.exitCode = 1; }
finally { await native.close(); save(); console.log(JSON.stringify({ passed: report.passed, output, cases: report.cases.length, error: report.error })); }
