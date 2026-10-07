// Actual tabs manufacturing and final-BRep measurement, including the stale-model UI sequence.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { copyFileSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseRenderGeometryResource } from '../../iCAX-UI/SDK/Viewport/renderResource.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const runtime = resolve(process.env.ICAX_PRODUCT_TABS_RUNTIME || resolve(root, 'src/x64/Debug'));
const deployed = process.env.ICAX_PRODUCT_TABS_DEPLOYED !== '0';
const currentManufacturingOnly = process.env.ICAX_PRODUCT_TABS_CURRENT_ONLY !== '0';
const materialSelection = process.env.ICAX_PRODUCT_TABS_MATERIAL || 'same_frame';
assert.ok(['same_frame', 'independent'].includes(materialSelection));
const assets = deployed ? resolve(runtime, 'apps/tube-designer') : resolve(root, 'src/apps/tube-designer');
const output = resolve(process.env.ICAX_PRODUCT_TABS_OUTPUT
  || resolve(root, `output/tests/product-tab-connections/${materialSelection}/${deployed ? 'deployed' : 'source'}-native.json`));
// Deployed assets resolve SDK imports through the browser asset service. Node imports the identical source file.
const helperPath = resolve(root, 'src/apps/tube-designer/webpage/productControls.mjs');
const deployedHelperPath = resolve(assets, 'webpage/productControls.mjs');
const { applyProductControlChoice, productControlFields, productControlValue } = await import(pathToFileURL(helperPath));
const artifactDirectory = dirname(output);
const bridgeDirectory = resolve(artifactDirectory, 'bridge-runtime');
mkdirSync(bridgeDirectory, { recursive: true });
assert.deepEqual(readdirSync(bridgeDirectory).filter(name => /\.dll$/i.test(name)), [], 'Do not stage runtime DLLs');
const bridge = resolve(bridgeDirectory, 'SecurityWindowFramesBridge.exe');
copyFileSync(resolve(process.env.ICAX_PRODUCT_TABS_BRIDGE
  || resolve(root, 'output/tests/product-tab-connections/bridge-build/SecurityWindowFramesBridge.exe')), bridge);
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const report = { passed: false, deployed, currentManufacturingOnly, materialSelection, runtime, assets, bridge, bridgeSha256: hash(bridge),
  helperSha256: hash(helperPath), deployedHelperSha256: hash(deployedHelperPath),
  descriptorSha256: hash(resolve(assets, 'templates/product/single_face_security_window/template.json')),
  productContractSha256: hash(resolve(assets, 'templates/_shared/security_window_product_contract.py')),
  manufacturingDeclarationSha256: hash(resolve(assets, 'templates/_shared/product_window_manufacturing.py')),
  stages: [], assertions: [] };
assert.equal(report.helperSha256, report.deployedHelperSha256);
const save = () => writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
const artifact = (name, value) => {
  const path = resolve(artifactDirectory, name);
  writeFileSync(path, JSON.stringify(value, null, 2) + '\n');
  return path;
};
save();

function connection() {
  const child = spawn(bridge, [], { cwd: deployed ? runtime : root, windowsHide: true,
    env: { ...process.env, PATH: `${runtime};${process.env.PATH || ''}` }, stdio: ['pipe', 'pipe', 'pipe'] });
  let sequence = 0, stderr = '', ended = false;
  const pending = new Map();
  const fail = error => { for (const request of pending.values()) {
    clearTimeout(request.timer); request.reject(error);
  } pending.clear(); };
  child.stderr.on('data', data => { stderr = (stderr + data).slice(-10000); });
  child.on('error', fail);
  child.on('exit', code => { ended = true; fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { return fail(new Error(`Invalid native response: ${line.slice(0, 500)}`)); }
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    close() { child.stdin.end(); child.kill(); },
    modules() {
      const observed = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
        `(Get-Process -Id ${child.pid}).Modules | Where-Object { $_.ModuleName -in @('TemplateRuntime.dll','TubeDesigner.dll') } | Select-Object ModuleName,FileName | ConvertTo-Json -Compress`],
      { windowsHide: true, encoding: 'utf8', timeout: 30000 });
      assert.equal(observed.status, 0, observed.stderr);
      const parsed = JSON.parse(observed.stdout.trim());
      const modules = (Array.isArray(parsed) ? parsed : [parsed]).map(item => ({ name: item.ModuleName,
        path: item.FileName, sha256: hash(item.FileName) }));
      assert.equal(modules.length, 2);
      assert.ok(modules.every(item => item.path.toLowerCase().startsWith(runtime.toLowerCase() + '\\')));
      return modules;
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        if (ended) return reject(new Error(`Native already exited: ${stderr}`));
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 300000);
        pending.set(id, { method, resolve: resolveRequest, reject, timer });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

const native = connection();
try {
  const templateId = 'single-face-security-window';
  let template;
  try { template = (await native.invoke('GetTemplateDescriptor', { templateId })).template; }
  catch (error) { report.modules = native.modules(); throw error; }
  report.templatePackageDigest = template.packageDigest;
  report.modules = native.modules();
  const defaults = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
  const outerControl = productControlFields(template).find(control => control.key === 'outerFrameConnection');
  assert.ok(outerControl);
  const choose = (values, choice) => {
    const before = structuredClone(values);
    const result = applyProductControlChoice({}, template, outerControl, values, choice);
    assert.deepEqual(values, before);
    assert.equal(productControlValue(template, outerControl, result), choice);
    assert.equal(Object.hasOwn(result, 'outerFrameConnection'), false);
    return result;
  };
  assert.equal(defaults.outerFramePostMaterial, 'same_frame', 'The regression must start from the production material default');
  const baselineDefaults = materialSelection === 'same_frame' ? defaults : { ...defaults, outerFramePostMaterial: materialSelection };
  if (currentManufacturingOnly) assert.ok(template.parameters.some(field => field.key === 'outerFramePostMaterial'));
  const miterValues = choose(baselineDefaults, 'miter');
  const tabsValues = choose(baselineDefaults, 'tabs');
  assert.equal(tabsValues.foldedPostJoint, 'tabs');
  assert.equal(tabsValues.frameManufacturingMode, 'segment_weld');
  report.productionDefaults = defaults;
  report.baselineMaterialSelection = baselineDefaults.outerFramePostMaterial;
  report.miterParameters = miterValues;
  report.tabsParameters = tabsValues;
  for (const [key, value] of Object.entries(baselineDefaults)) if (!['frameManufacturingMode', 'foldedPostJoint', 'frameJoinType'].includes(key))
    assert.deepEqual(tabsValues[key], value, `Production default ${key} was changed`);
  const stage = async (name, work) => {
    const entry = { name, milliseconds: 0 };
    report.stages.push(entry); save();
    const start = Date.now();
    try { await work(entry); entry.completed = true; }
    catch (error) { entry.error = String(error.stack || error); throw error; }
    finally { entry.milliseconds = Date.now() - start; save(); console.log(JSON.stringify({ name,
      completed: entry.completed, milliseconds: entry.milliseconds, geometryChecks: entry.geometryChecks,
      staleMiterManufacturingReproduced: entry.staleMiterManufacturingReproduced, error: entry.error })); }
  };
  let productId, outer, originalDisplay, originalRunId;
  await stage('actual-default-tabs-native-descriptor-preview-echo', async entry => {
    const request = { templateId, parameters: tabsValues }, before = structuredClone(request);
    const generated = await native.invoke('GenerateProductTemplatePreview', request);
    assert.deepEqual(request, before);
    assert.deepEqual(generated.parameters, tabsValues);
    entry.previewPath = artifact('tabs-native-template-preview.json', generated);
  });
  const displayIdentity = snapshot => snapshot.tubeDesigner.members.map(member => ({
    entityId: member.entityId, stableKey: member.stableKey,
    resourceId: member.previewGeometryResourceId, resourceVersion: member.previewGeometryResourceVersion,
    transform: member.transform })).sort((left, right) => left.stableKey.localeCompare(right.stableKey));
  if (currentManufacturingOnly) await stage('baseline-display-only-miter', async entry => {
    const generated = await native.invoke('GeneratePreview', { templateId, ...miterValues });
    productId = generated.tubeDesigner.product.entityId;
    originalRunId = generated.tubeDesigner.product.activeGenerationRunId;
    originalDisplay = displayIdentity(generated);
    entry.generationRunId = originalRunId;
    entry.displayIdentity = originalDisplay;
  });
  if (!currentManufacturingOnly && process.env.ICAX_PRODUCT_TABS_SKIP_STALE !== '1') await stage('baseline-miter-actual-disassembly', async entry => {
    const generated = await native.invoke('GeneratePreview', { templateId, ...miterValues });
    assert.deepEqual(generated.tubeDesigner.product.parameters, miterValues);
    productId = generated.tubeDesigner.product.entityId;
    const result = await native.invoke('Disassemble', { productEntityIds: [productId] });
    entry.geometryChecks = result.geometryChecks.filter(check => check.key.startsWith('outer_frame.'));
    entry.snapshot = artifact('miter-disassembled.json', result);
    const inspected = await native.invoke('InspectScriptResources');
    entry.manufacturingParameters = inspected.manufacturingParameters;
    report.baselineOuterGeometry = entry.geometryChecks;
  });
  if (!currentManufacturingOnly && process.env.ICAX_PRODUCT_TABS_SKIP_STALE !== '1') await stage('edit-tabs-then-disassemble-without-current-parameters', async entry => {
    const updated = await native.invoke('UpdateProductParameters', { productEntityId: productId, parameters: tabsValues });
    assert.deepEqual(updated.tubeDesigner.product.parameters, tabsValues);
    entry.modelOutdatedAfterEdit = updated.tubeDesigner.product.modelOutdated;
    entry.partsOutdatedAfterEdit = updated.tubeDesigner.product.partsOutdated;
    entry.updated = artifact('tabs-edited-before-generation.json', updated);
    const result = await native.invoke('Disassemble', { productEntityIds: [productId] });
    entry.snapshot = artifact('tabs-edit-stale-disassembled.json', result);
    entry.geometryChecks = result.geometryChecks.filter(check => check.key.startsWith('outer_frame.'));
    const inspected = await native.invoke('InspectScriptResources');
    entry.generatedParameters = inspected.generatedParameters;
    entry.manufacturingParameters = inspected.manufacturingParameters;
    entry.savedParameters = result.tubeDesigner.product.parameters;
    entry.actualConnection = inspected.manufacturingParameters.foldedPostJoint;
    entry.staleMiterManufacturingReproduced = entry.actualConnection === 'weld';
    report.staleMiterManufacturingReproduced = entry.staleMiterManufacturingReproduced;
  });
  if (currentManufacturingOnly) await stage('current-tabs-manufacturing-with-frozen-display', async entry => {
    const updated = await native.invoke('UpdateProductParameters', { productEntityId: productId, parameters: tabsValues });
    entry.modelOutdatedAfterEdit = updated.tubeDesigner.product.modelOutdated;
    entry.partsOutdatedAfterEdit = updated.tubeDesigner.product.partsOutdated;
    entry.updated = artifact('tabs-current-manufacturing-updated.json', updated);
    assert.equal(entry.modelOutdatedAfterEdit, false, 'A manufacturing-only connection choice must not expire the display model');
    assert.equal(entry.partsOutdatedAfterEdit, true);
    assert.equal(updated.tubeDesigner.product.activeGenerationRunId, originalRunId);
    assert.deepEqual(displayIdentity(updated), originalDisplay);
    const result = await native.invoke('Disassemble', { productEntityIds: [productId],
      productParametersByEntityId: { [productId]: tabsValues } });
    entry.snapshot = artifact('tabs-disassembled.json', result);
    entry.geometryChecks = result.geometryChecks.filter(check => check.key.startsWith('outer_frame.'));
    outer = result.tubeDesigner.parts.filter(part => part.stableKey.startsWith('outer_frame.'));
    assert.equal(outer.length, 4);
    for (const check of entry.geometryChecks) {
      assert.equal(check.valid, true); assert.equal(check.solids, 1); assert.ok(check.volume > 0);
    }
    entry.outerParts = outer;
    const inspected = await native.invoke('InspectScriptResources');
    entry.scriptResources = artifact('tabs-script-resources.json', inspected);
    assert.deepEqual(inspected.generatedParameters, miterValues);
    assert.deepEqual(inspected.manufacturingParameters, tabsValues);
    assert.equal(result.tubeDesigner.product.activeGenerationRunId, originalRunId);
    assert.deepEqual(displayIdentity(result), originalDisplay);
    report.displayRunAndResourcesPreserved = true;
  });
  if (!currentManufacturingOnly) await stage('regenerate-tabs-and-actual-disassembly', async entry => {
    const request = { ...(productId ? { productEntityId: productId } : {}), templateId, ...tabsValues }, frozen = structuredClone(request);
    const generated = await native.invoke('GeneratePreview', request);
    assert.deepEqual(request, frozen);
    assert.deepEqual(generated.tubeDesigner.product.parameters, tabsValues);
    productId = generated.tubeDesigner.product.entityId;
    const result = await native.invoke('Disassemble', { productEntityIds: [productId] });
    entry.snapshot = artifact('tabs-disassembled.json', result);
    entry.geometryChecks = result.geometryChecks.filter(check => check.key.startsWith('outer_frame.'));
    outer = result.tubeDesigner.parts.filter(part => part.stableKey.startsWith('outer_frame.'));
    assert.equal(outer.length, 4, 'Tabs must preserve four independently machinable outer frame pieces');
    entry.outerParts = outer;
    for (const check of entry.geometryChecks) {
      assert.equal(check.valid, true); assert.equal(check.solids, 1); assert.ok(check.volume > 0);
    }
    const inspected = await native.invoke('InspectScriptResources');
    entry.scriptResources = artifact('tabs-script-resources.json', inspected);
    assert.deepEqual(inspected.generatedParameters, tabsValues);
    assert.deepEqual(inspected.manufacturingParameters, tabsValues);
  });
  await stage('final-brep-measurement-and-real-thumbnail', async entry => {
    entry.parts = [];
    for (const part of outer) {
      const measured = await native.invoke('MeasurePartGeometry', { partEntityId: part.entityId,
        resourceVersion: part.manufacturingGeometryResourceVersion });
      assert.equal(measured.source, 'final-brep'); assert.equal(measured.available, true);
      assert.equal(measured.resourceId, part.manufacturingGeometryResourceId);
      assert.equal(measured.resourceVersion, part.manufacturingGeometryResourceVersion);
      const label = part.stableKey.replaceAll('.', '-');
      const item = { stableKey: part.stableKey, measurement: measured,
        measurementPath: artifact(`${label}-measure.json`, measured) };
      entry.parts.push(item); save();
      const rawThumbnail = await native.invoke('InspectPartThumbnail', { partEntityId: part.entityId });
      assert.equal(rawThumbnail.resourceId, part.thumbnailGeometryResourceId);
      assert.equal(rawThumbnail.resourceVersion, part.thumbnailGeometryResourceVersion);
      const bytes = Uint8Array.from(rawThumbnail.flatbufferBytes);
      const decoded = parseRenderGeometryResource(bytes.buffer);
      assert.equal(decoded.kind, 'mesh');
      assert.ok(decoded.positions.length > 0 && decoded.indices.length > 0);
      const { flatbufferBytes, ...thumbnailMetadata } = rawThumbnail;
      const thumbnail = { ...thumbnailMetadata, positions: Array.from(decoded.positions), indices: Array.from(decoded.indices) };
      item.thumbnailPath = artifact(`${label}-thumbnail.json`, thumbnail);
      item.thumbnailVertices = thumbnail.positions.length / 3;
      item.thumbnailTriangles = thumbnail.indices.length / 3;
      try { item.drawing = await native.invoke('GetPartDrawing', { partEntityId: part.entityId }); }
      catch (error) { item.drawingAvailability = String(error.message); }
      save();
    }
    const post = outer.find(part => /^outer_frame\.(left|right)\./.test(part.stableKey));
    const across = (post.profile.depth - post.profile.wallThickness) / 2;
    for (const item of entry.parts.filter(part => /^outer_frame\.(top|bottom)\./.test(part.stableKey))) {
      const actual = outer.find(part => part.stableKey === item.stableKey);
      const stations = [actual.profile.width / 2, actual.length - actual.profile.width / 2];
      item.socketMeasurements = item.measurement.features.filter(feature =>
        stations.some(station => Math.abs(feature.station - station) < 0.05)
        && Math.abs(Math.abs(feature.center[1]) - across) < 0.05
        && Math.abs(feature.axis[2]) > 0.99);
      item.socketCount = item.socketMeasurements.length;
      item.openBoundaryMeasurements = item.measurement.features.filter(feature => feature.kind === 'side-opening');
      item.receiverCutStyle = materialSelection === 'same_frame' ? 'open-side-slots' : 'closed-sockets';
      item.endSocketFeaturesRecognized = item.socketCount === 4 || item.openBoundaryMeasurements.filter(feature =>
        stations.some(station => Math.abs(feature.station - station) < tabsValues.foldedPostTabWidth / 2 + 1)).length === 4;
      save();
      if (materialSelection === 'independent')
        assert.equal(item.socketCount, 4, 'Each final receiver BRep must measure two sockets at each end');
    }
    report.receiverFeatureRecognition = entry.parts.filter(part => /^outer_frame\.(top|bottom)\./.test(part.stableKey))
      .map(part => ({ stableKey: part.stableKey, recognitionStatus: part.measurement.recognitionStatus,
        allOpeningCount: part.measurement.openingCount, closedSocketCount: part.socketCount,
        openBoundaryCount: part.openBoundaryMeasurements.length, endSocketFeaturesRecognized: part.endSocketFeaturesRecognized }));
  });
  await stage('receiver-end-socket-and-post-end-tongue-brep-probes', async entry => {
    entry.savedProject = await native.invoke('SaveAndReopen');
    entry.savedProjectPath = artifact('tabs-saved-project.json', entry.savedProject);
    // Derive skin midplanes from the persisted BRep bounds and actual saved section.
    const bounds = await native.invoke('InspectNativeGeometry', { entityIds: outer.map(part => part.entityId) });
    entry.actualBounds = bounds.geometryChecks;
    const probes = [];
    const post = outer.find(part => /^outer_frame\.(left|right)\./.test(part.stableKey));
    assert.ok(post);
    const postBounds = bounds.geometryChecks.find(check => check.entityId === post.entityId).bounds;
    const earAcross = (postBounds[5] - postBounds[4] - post.profile.wallThickness) / 2;
    for (const part of outer) {
      const actual = bounds.geometryChecks.find(check => check.entityId === part.entityId).bounds;
      const receiver = /^outer_frame\.(top|bottom)\./.test(part.stableKey);
      const wall = part.profile.wallThickness;
      assert.ok(wall > 0);
      if (receiver) {
        const bottom = part.stableKey.includes('.bottom.');
        const z = bottom ? actual[5] - wall / 2 : actual[4] + wall / 2;
        const yCenter = (actual[2] + actual[3]) / 2;
        for (const station of [actual[0] + part.profile.width / 2, actual[1] - part.profile.width / 2])
          for (const y of [yCenter - earAcross, yCenter + earAcross]) {
          probes.push({ entityId: part.entityId, point: [station, y, z], expected: 'outside', role: 'socket-center' });
          const bridgeDistance = tabsValues.foldedPostTabWidth / 2 + wall + 2 * tabsValues.foldedPostSideClearance;
          probes.push({ entityId: part.entityId, point: [station - bridgeDistance, y, z], expected: 'inside', role: 'socket-bridge' });
          if (materialSelection === 'same_frame') {
            // Same-sized tabs intersect the curved corner and side skin, creating an open side channel.
            const depth = tabsValues.foldedPostTabLength;
            assert.ok(depth > wall && depth < part.profile.depth - wall);
            const channelZ = bottom ? actual[5] - depth / 2 : actual[4] + depth / 2;
            const channelTipZ = bottom ? actual[5] - depth + 0.005 : actual[4] + depth - 0.005;
            const farWallZ = bottom ? actual[4] + wall / 2 : actual[5] - wall / 2;
            probes.push({ entityId: part.entityId, point: [station, y, channelZ], expected: 'outside', role: 'side-channel-open' });
            probes.push({ entityId: part.entityId, point: [station, y, channelTipZ], expected: 'outside', role: 'side-channel-tip-clearance' });
            probes.push({ entityId: part.entityId, point: [station - bridgeDistance, y, channelZ], expected: 'inside', role: 'side-channel-bridge' });
            probes.push({ entityId: part.entityId, point: [station, y, farWallZ], expected: 'inside', role: 'opposite-wall-retained' });
          }
        }
      } else {
        // At either end, the double ear retains two narrow sidewall tabs and removes the other skins.
        const yCenter = (actual[2] + actual[3]) / 2, zCenter = (actual[4] + actual[5]) / 2;
        for (const x of [actual[0] + 2, actual[1] - 2]) {
          for (const z of [actual[4] + wall / 2, actual[5] - wall / 2]) probes.push({ entityId: part.entityId,
            point: [x, yCenter, z], expected: 'inside', role: 'tongue-retained' });
          probes.push({ entityId: part.entityId, point: [x, actual[3] - wall / 2, zCenter],
            expected: 'outside', role: 'end-other-skin-trimmed' });
        }
      }
    }
    entry.probes = probes;
    // Capture final material proof before raw-stock controls, retaining useful evidence if a control fails.
    entry.inspection = await native.invoke('InspectNativeGeometry', {
      entityIds: outer.map(part => part.entityId), points: probes.map(({ entityId, point }) => ({ entityId, point })) });
    entry.inspectionPath = artifact('tabs-final-brep-probes.json', entry.inspection);
    save();
    const stockRequests = outer.map(part => {
      const stock = bounds.canonicalStockResources.find(resource => resource.stockId === part.stableKey && resource.kind === 'baseGeometry');
      assert.ok(stock, `Actual canonical stock was not recorded for ${part.stableKey}`);
      return { url: stock.url, version: stock.version, key: part.stableKey,
        points: probes.filter(point => point.entityId === part.entityId).map(point => point.point) };
    });
    entry.actualStockInspection = await native.invoke('InspectBRepResources', { resources: stockRequests });
    entry.actualStockInspectionPath = artifact('tabs-canonical-stock-probes.json', entry.actualStockInspection);
    for (const check of entry.actualStockInspection.geometryChecks) {
      const part = outer.find(item => item.stableKey === check.key);
      const requested = probes.filter(point => point.entityId === part.entityId);
      assert.equal(requested.length, check.pointChecks.length);
      requested.forEach((point, index) => assert.equal(check.pointChecks[index].inside, true,
        `${point.role} must start inside the real uncut stock; avoid outside-stock false positives`));
    }
    for (const check of entry.inspection.geometryChecks) {
      const requested = probes.filter(point => point.entityId === check.entityId);
      assert.equal(requested.length, check.pointChecks.length);
      requested.forEach((point, index) => {
        const actual = check.pointChecks[index];
        report.assertions.push({ key: check.key, role: point.role, point: point.point,
          expected: point.expected, passed: actual[point.expected] === true, actual });
      });
    }
    assert.ok(report.assertions.length > 0);
    assert.ok(report.assertions.every(item => item.passed), 'Actual tabs/socket material classifications failed; inspect retained proof');
  });
  report.passed = true;
} catch (error) { report.error = String(error.stack || error); process.exitCode = 1; }
finally { native.close(); save(); console.log(JSON.stringify({ passed: report.passed, output, error: report.error })); }
