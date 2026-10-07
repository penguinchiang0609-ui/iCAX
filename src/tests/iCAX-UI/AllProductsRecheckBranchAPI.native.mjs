// Revalidate six recorded native branch inputs without promoting API checks to shape proofs.
// The default mode still requires the complete aggregate BRep and persistence proof first.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { isDeepStrictEqual } from 'node:util';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const binaryRoot = resolve(repository, 'src/x64/Debug');
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || binaryRoot);
const output = resolve(repository, 'output/tests/assembly-process');
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const read = path => JSON.parse(readFileSync(path, 'utf8'));
const independentProofs = process.env.ICAX_NATIVE_BRANCH_API_INDEPENDENT_PROOFS === '1';
const aggregateFile = resolve(output, 'all-products-recheck-native-branches.json');
const branchNames = ['stair-l-oval', 'stair-l-racetrack', 'guardrail-continuous-contacts',
  'guardrail-stepped-contacts', 'guardrail-multi-u-contacts', 'louver-external-profile-stock'];
const inputArtifacts = new Map(), producerArtifacts = new Map();
function freeze(path, map = inputArtifacts) {
  path = resolve(path);
  const sha256 = digest(path);
  if (map.has(path)) assert.equal(map.get(path), sha256, `Conflicting frozen input: ${path}`);
  map.set(path, sha256);
  return { path, sha256 };
}
const inputRecords = [];
let initial;
if (independentProofs) {
  assert.ok(!process.env.ICAX_PRODUCT_BRANCH_CASE, 'Independent API proof must run all six branches');
  const cases = branchNames.map(name => {
    const file = resolve(output, `all-products-recheck-native-branches-${name}.json`);
    const originalReport = read(file), originalReportEvidence = freeze(file);
    assert.equal(originalReport.schema, 'icax.all-products-recheck-native-branches');
    assert.equal(originalReport.prepareOnly, false);
    assert.equal(originalReport.runtimeRoot, runtimeRoot);
    assert.equal(originalReport.cases.length, 1);
    const previous = originalReport.cases[0];
    assert.equal(previous.name, name);
    assert.equal(typeof previous.passed, 'boolean');
    assert.equal(typeof originalReport.passed, 'boolean');
    const hostInput = freeze(previous.hostInputFile), sourceReference = freeze(previous.sourceReferenceFile);
    const request = read(hostInput.path), reference = read(sourceReference.path);
    assert.equal(request.templateId, previous.templateId);
    assert.equal(reference.templateId, previous.templateId);
    assert.equal(request.template.id, previous.templateId);
    assert.deepEqual(request.parameters, previous.parameters);
    assert.deepEqual(reference.parameters, previous.parameters);
    assert.deepEqual(reference.document.parameters, previous.parameters);
    assert.deepEqual(previous.sourceChecks, reference.sourceChecks);
    for (const key of ['pureDeclaration', 'independentExecutor', 'hostInputsUnchanged',
      'strictWorldCSGEqual', 'processResultsEqual', 'partFactsEqual'])
      assert.equal(previous.sourceChecks[key], true, `${name}: ${key}`);
    const facts = previous.partFacts;
    assert.ok(Number.isInteger(facts.parts) && facts.parts > 0);
    assert.equal(facts.parts, reference.document.items.length);
    assert.equal(facts.checkedLengths, reference.document.items.filter(item =>
      typeof item.properties.length === 'number').length);
    assert.ok(facts.checkedLengths > 0);
    for (const key of ['exactStockLengthsEqual', 'materialListLengthsEqual',
      'materialsAndQuantitiesEqual', 'identitiesEqual']) assert.equal(facts[key], true, `${name}: ${key}`);
    let sceneProof;
    if (!Object.hasOwn(previous, 'nativePreviewAndPlanningAccepted')) {
      // A failed later shape check never records the harness's terminal API flags.
      // Require actual generated/disassembled scene evidence instead; acceptance is tested fresh below.
      assert.equal(previous.passed, false);
      const indexEvidence = freeze(previous.stageEvidence.path), index = read(indexEvidence.path);
      assert.equal(indexEvidence.sha256, previous.stageEvidence.sha256);
      assert.equal(index.name, name);
      assert.deepEqual(index.hostInput, hostInput);
      assert.deepEqual(index.sourceReference, sourceReference);
      const artifact = index.stageArtifacts.find(row => row.stage === 'scene-before-shape-comparison');
      assert.ok(artifact, `${name}: missing actual generated/disassembled scene`);
      const sceneEvidence = freeze(artifact.path);
      assert.equal(sceneEvidence.sha256, artifact.sha256);
      const scene = read(sceneEvidence.path).evidence;
      assert.equal(scene.stage, 'scene-before-shape-comparison');
      assert.ok(scene.snapshot.generationRunId);
      assert.deepEqual(scene.snapshot.generatedParameters, request.parameters);
      assert.deepEqual(scene.snapshot.manufacturingParameters, request.parameters);
      // The native stage was captured by JSON.stringify; Python source JSON preserves -0.
      // Compare in that exact capture encoding without changing either original document.
      assert.ok(isDeepStrictEqual(scene.snapshot.manufacturing.document,
        JSON.parse(JSON.stringify(reference.manufacturing))), `${name}: captured manufacturing declaration differs`);
      const expectedKeys = reference.document.items.map(item => item.key).sort();
      assert.deepEqual(scene.snapshot.manufacturingExecution.document.items.map(item => item.key).sort(), expectedKeys);
      assert.deepEqual(scene.nativeGeometry.geometryChecks.map(item => item.key).sort(), expectedKeys);
      assert.equal(scene.nativeGeometry.geometryChecks.length, facts.parts);
      assert.ok(scene.nativeGeometry.geometryChecks.every(item => item.valid && item.solids === 1));
      sceneProof = { index: indexEvidence, scene: sceneEvidence,
        actualGeneratedAndDisassembledInputsAndPartsBound: true,
        declarationComparisonEncoding: 'JSON.stringify then JSON.parse, matching native stage capture' };
    } else assert.equal(previous.nativePreviewAndPlanningAccepted, true);
    for (const [path, hash] of Object.entries(reference.sourceDigests)) {
      assert.equal(freeze(resolve(repository, 'src', path), producerArtifacts).sha256, hash);
      assert.equal(freeze(resolve(runtimeRoot, path), producerArtifacts).sha256, hash);
    }
    inputRecords.push({ name, originalReport: originalReportEvidence, hostInput, sourceReference,
      initialReportPassed: originalReport.passed, initialFullShapePassed: previous.passed,
      previousNativePreviewAndPlanningAcceptedRecorded: Object.hasOwn(previous, 'nativePreviewAndPlanningAccepted'),
      previousNativePreviewAndPlanningAccepted: previous.nativePreviewAndPlanningAccepted ?? null,
      previousHarnessTerminalFlagsRecorded: previous.harnessArtifacts?.beforeAfterUnchanged === true,
      templateId: previous.templateId, parameters: structuredClone(previous.parameters),
      sourceChecks: structuredClone(previous.sourceChecks), partFacts: structuredClone(facts), sceneProof });
    return structuredClone(previous);
  });
  initial = { cases };
} else {
  initial = read(aggregateFile);
  assert.equal(initial.passed, true, 'The initial full shape and save/reopen proof must pass first');
  assert.equal(initial.cases.length, 6);
  freeze(aggregateFile);
}
const selected = initial.cases.filter(row => !process.env.ICAX_PRODUCT_BRANCH_CASE
  || row.name === process.env.ICAX_PRODUCT_BRANCH_CASE);
assert.ok(selected.length);
const bridge = resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
  || resolve(repository, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe'));
assert.equal(process.arch, 'x64', 'Native module inspection requires the verified x64 Node host');
const moduleInspector = resolve(process.env.ICAX_NATIVE_API_MODULE_INSPECTOR
  || resolve(process.env.ProgramFiles, 'PowerShell/7/pwsh.exe'));
const reportFile = resolve(output, 'all-products-recheck-native-branches-api-stage2.json');
const moduleFiles = readdirSync(runtimeRoot).filter(name => name.toLowerCase().endsWith('.dll')).sort();
const modules = moduleFiles.map(name => ({ file: name, ...freeze(resolve(runtimeRoot, name), producerArtifacts) }));
const bridgeEvidence = freeze(bridge, producerArtifacts);
const harnessEvidence = freeze(fileURLToPath(import.meta.url), producerArtifacts);
const moduleInspectorEvidence = freeze(moduleInspector, producerArtifacts);
let manifestEvidence;
if (independentProofs) {
  const manifestFile = resolve(output, 'all-products-recheck-native-branches-api-independent-input-manifest.json');
  writeFileSync(manifestFile, JSON.stringify({ schema: 'icax.native-branch-api-independent-input-manifest',
    schemaVersion: 1, frozenAt: new Date().toISOString(), runtimeRoot, apiOnly: true,
    fullShapeAcceptanceAsserted: false, cases: inputRecords,
    inputArtifacts: [...inputArtifacts].map(([path, sha256]) => ({ path, sha256 })),
    producerArtifacts: [...producerArtifacts].map(([path, sha256]) => ({ path, sha256 })) }, null, 2) + '\n');
  manifestEvidence = freeze(manifestFile);
}
function assertFrozen() {
  for (const [path, sha256] of [...inputArtifacts, ...producerArtifacts])
    assert.equal(digest(path), sha256, `API input or runtime artifact changed: ${path}`);
}
const report = { schema: 'icax.all-products-recheck-native-branch-api', schemaVersion: 1,
  initialShapeEvidence: independentProofs ? null : aggregateFile,
  independentProofs, independentInputManifest: manifestEvidence,
  fullShapeComparisonPerformed: false, saveReopenPerformed: false, fullShapeAcceptanceAsserted: false,
  validationScope: 'API-only: same complete host inputs, descriptor, preview identities and manufacturing plan identity/material/quantity/length facts; no BRep equality or save/reopen acceptance asserted',
  inputRecords, modules, harnessEvidence, bridgeEvidence, moduleInspectorEvidence, nodeArchitecture: process.arch,
  runtimeRoot, nativeModuleDigest: digest(resolve(runtimeRoot, 'TemplateRuntime.dll')),
  bridgeDigest: digest(bridge), startedAt: new Date().toISOString(), cases: [], passed: false };
const save = () => writeFileSync(reportFile, JSON.stringify(report, null, 2) + '\n');

function connection(caseName) {
  const child = spawn(bridge, [], { cwd: runtimeRoot, windowsHide: true,
    env: { ...process.env, PATH: `${runtimeRoot};${binaryRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'] });
  const pending = new Map();
  let sequence = 0, stderr = '';
  console.log(JSON.stringify({ nativeAPIStage: 'bridge-started', case: caseName, bridgePid: child.pid,
    bridge, at: new Date().toISOString() }));
  function fail(error) {
    for (const row of pending.values()) { clearTimeout(row.timer); row.reject(error); }
    pending.clear();
  }
  child.stderr.on('data', value => { stderr += value; });
  child.on('error', fail);
  child.on('exit', code => { if (pending.size) fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { fail(new Error(line)); return; }
    const row = pending.get(response.id);
    if (!row) return;
    pending.delete(response.id); clearTimeout(row.timer);
    response.ok ? row.resolve(response.result) : row.reject(new Error(`${row.method}: ${response.error}`));
  });
  return {
    inspectLoadedModules() {
      assert.ok(child.pid && child.exitCode === null && child.signalCode === null);
      const script = `[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new();
        $taskApiProcess = Get-Process -Id ${child.pid} -ErrorAction Stop;
        $taskApiAllModules = @(Get-Process -Id ${child.pid} -Module -ErrorAction Stop);
        $taskApiModules = @($taskApiAllModules | Where-Object {
          $_.ModuleName -in @('TemplateRuntime.dll', 'TubeDesigner.dll', 'SDO.dll')
        } | ForEach-Object { [PSCustomObject]@{
          file = $_.ModuleName; actualPath = $_.FileName;
          actualSha256 = (Get-FileHash -LiteralPath $_.FileName -Algorithm SHA256).Hash.ToLowerInvariant()
        } });
        [PSCustomObject]@{ processId = $taskApiProcess.Id; processPath = $taskApiProcess.Path;
          capturedAt = [DateTime]::UtcNow.ToString('o'); modules = $taskApiModules;
          inspectorIs64Bit = [Environment]::Is64BitProcess; inspectorVersion = $PSVersionTable.PSVersion.ToString();
          allLoadedModuleNames = @($taskApiAllModules | ForEach-Object { $_.ModuleName }) } | ConvertTo-Json -Depth 5 -Compress`;
      const inspection = spawnSync(moduleInspector, ['-NoProfile', '-NonInteractive', '-Command', script],
        { encoding: 'utf8', windowsHide: true, timeout: 30000, maxBuffer: 1024 * 1024 });
      assert.equal(inspection.status, 0, `Loaded native module inspection failed: ${inspection.stderr?.slice(0, 2000)}`);
      const actual = JSON.parse(inspection.stdout.trim());
      assert.equal(actual.processId, child.pid);
      assert.equal(actual.inspectorIs64Bit, true);
      assert.equal(resolve(actual.processPath), bridge);
      assert.equal(digest(actual.processPath), bridgeEvidence.sha256);
      const required = ['TemplateRuntime.dll', 'TubeDesigner.dll', 'SDO.dll'];
      assert.deepEqual(actual.modules.map(module => module.file).sort(), required.sort(),
        `Loaded module list did not contain every required DLL; all names: ${actual.allLoadedModuleNames.join(',')}`);
      actual.modules = actual.modules.map(module => {
        const frozen = modules.find(row => row.file === module.file);
        assert.ok(frozen, `Missing frozen native module: ${module.file}`);
        assert.equal(module.actualSha256, frozen.sha256, `Loaded native DLL differs: ${module.actualPath}`);
        return { ...module, frozenRuntimePath: frozen.path, frozenSha256: frozen.sha256,
          actualLoadedHashMatchesFrozenRuntime: true };
      });
      return { ...actual, capturedWhileBridgeAlive: true, inspectionStderr: inspection.stderr };
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRow, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 600000);
        pending.set(id, { resolve: resolveRow, reject, timer, method });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
    close() {
      return new Promise(resolveClosed => {
        if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
        child.once('exit', resolveClosed); child.stdin.end(); child.kill();
      });
    },
  };
}

save();
try {
  for (const previous of selected) {
    const native = connection(previous.name), result = { name: previous.name, passed: false };
    try {
      if (!independentProofs) assert.equal(previous.passed, true);
      assertFrozen();
      const request = JSON.parse(readFileSync(previous.hostInputFile, 'utf8'));
      const reference = JSON.parse(readFileSync(previous.sourceReferenceFile, 'utf8'));
      for (const [path, hash] of Object.entries(reference.sourceDigests)) {
        assert.equal(digest(resolve(repository, 'src', path)), hash, `The proven producer changed: ${path}`);
        assert.equal(digest(resolve(runtimeRoot, path)), hash, `The deployed producer changed: ${path}`);
      }
      const descriptor = (await native.invoke('GetTemplateDescriptor', { templateId: request.templateId })).template;
      assert.deepEqual(descriptor, request.template, 'The same generic descriptor must reach the updated native codec');
      const actualLoadedModules = native.inspectLoadedModules();
      const values = structuredClone(request.parameters), frozen = structuredClone(values);
      const preview = await native.invoke('GenerateProductTemplatePreview', { templateId: request.templateId, parameters: values });
      assert.deepEqual(preview.parameters, frozen);
      assert.equal(preview.templateId, request.templateId);
      assert.deepEqual(preview.items.map(item => item.key).sort(), reference.display.items.map(item => item.key).sort());
      const plan = await native.invoke('GetProductManufacturingPlan', { templateId: request.templateId, parameters: values });
      assert.deepEqual(plan.parameters, frozen);
      assert.equal(plan.partCount, reference.document.items.length);
      assert.equal(plan.partCount, previous.partFacts.parts);
      const rows = plan.tables.flatMap(table => table.rows);
      assert.equal(rows.length, reference.document.items.length);
      assert.deepEqual(rows.map(row => row.itemKey).sort(), reference.document.items.map(item => item.key).sort());
      let lengths = 0;
      for (const item of reference.document.items) {
        const row = rows.find(candidate => candidate.itemKey === item.key), properties = item.properties;
        if (typeof properties.length === 'number') {
          assert.ok(Math.abs(Number(row.values.length) - properties.length) <= 0.001, item.key);
          lengths += 1;
        }
        if (typeof properties.quantity === 'number') assert.equal(row.values.quantity, properties.quantity, item.key);
        const material = properties['manufacturing.materialGrade'] || properties['manufacturing.material']
          || properties.materialGrade || properties.material;
        if (material) assert.equal(row.values.materialGrade || row.values.material, material, item.key);
      }
      assert.equal(lengths, previous.partFacts.checkedLengths);
      assert.deepEqual(values, frozen);
      const apiFile = resolve(output, `all-products-recheck-native-branches-api-stage2-${previous.name}.json`);
      writeFileSync(apiFile, JSON.stringify({ name: previous.name, apiOnly: true,
        fullShapeComparisonPerformed: false, initialFullShapePassed: previous.passed,
        actualLoadedModules, parameters: frozen, descriptor, preview, plan }, null, 2) + '\n');
      Object.assign(result, { passed: true, templateId: request.templateId, previewItems: preview.items.length,
        manufacturingParts: plan.partCount, checkedLengths: lengths, hostInputsUnchanged: true,
        normalizedParametersEqual: true, descriptorUnchanged: true, identityMaterialQuantityLengthEqual: true,
        nativePreviewAndPlanningAccepted: true, initialFullShapePassed: previous.passed,
        previousNativePreviewAndPlanningAcceptedRecorded: Object.hasOwn(previous, 'nativePreviewAndPlanningAccepted'),
        actualLoadedModules, apiOnly: true, fullShapeAcceptanceAsserted: false,
        apiFile, apiFileSha256: digest(apiFile) });
    } catch (error) { result.error = error.stack; report.cases.push(result); save(); throw error; }
    finally { await native.close(); }
    report.cases.push(result); save(); console.log(JSON.stringify(result));
  }
  assertFrozen();
  assert.equal(digest(resolve(runtimeRoot, 'TemplateRuntime.dll')), report.nativeModuleDigest);
  report.inputsAndRuntimeArtifactsUnchangedDuringRun = true;
  report.inputArtifactsBeforeAfter = [...inputArtifacts].map(([path, sha256]) =>
    ({ path, sha256Before: sha256, sha256After: digest(path) }));
  report.producerArtifactsBeforeAfter = [...producerArtifacts].map(([path, sha256]) =>
    ({ path, sha256Before: sha256, sha256After: digest(path) }));
  report.passed = report.cases.every(row => row.passed); report.finishedAt = new Date().toISOString(); save();
  console.log(JSON.stringify({ passed: report.passed, cases: report.cases.length, reportFile }));
} catch (error) { report.error = error.message; report.finishedAt = new Date().toISOString(); save(); throw error; }
