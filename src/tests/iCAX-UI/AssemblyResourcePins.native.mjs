// Exercise real UI resource pins through native preview, generation, machining and reopen.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { makeProductToolBinding } from '../../apps/tube-designer/webpage/productResourceBindings.mjs';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const runtimeRoot = resolve(process.env.ICAX_RESOURCE_PINS_RUNTIME_ROOT || resolve(repository, 'src/x64/Debug'));
const bridgePath = resolve(process.env.ICAX_RESOURCE_PINS_BRIDGE
  || resolve(repository, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe'));
const reportPath = resolve(process.env.ICAX_RESOURCE_PINS_OUTPUT
  || resolve(repository, 'output/tests/assembly-resource-pins-native.json'));
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const errorText = error => String(error.message || error).slice(0, 4000);
const templatesRoot = resolve(runtimeRoot, 'apps/tube-designer/templates');
const toolRoot = resolve(templatesRoot, 'mold/v-notch-sharp');
const toolManifest = resolve(toolRoot, 'tool.json');
const report = { passed: false, runtimeRoot, bridgePath, cases: [] };
mkdirSync(dirname(reportPath), { recursive: true });
const save = () => writeFileSync(reportPath, JSON.stringify(report, null, 2) + '\n');

function loadTool() {
  const descriptor = JSON.parse(readFileSync(toolManifest, 'utf8'));
  const member = resolve(toolRoot, descriptor.kind === 'programmatic' ? 'tool.py' : 'geometry.json');
  // This is the current package contract used by punch_tool_runtime._package and the UI catalogue.
  const digest = createHash('sha256').update(readFileSync(toolManifest)).update(Buffer.from([0]))
    .update(readFileSync(member)).update(readFileSync(resolve(templatesRoot, '_shared/section_geometry.py')))
    .digest('hex');
  return { ...descriptor, libraryScope: 'system', digest,
    defaultParameters: Object.fromEntries((descriptor.parameters || []).map(field => [field.key, field.defaultValue])) };
}

function connection() {
  const child = spawn(bridgePath, [], { cwd: runtimeRoot, windowsHide: true,
    env: { ...process.env, PATH: `${dirname(bridgePath)};${runtimeRoot};${process.env.PATH || ''}` },
    stdio: ['pipe', 'pipe', 'pipe'] });
  let sequence = 0, stderr = '', ended = false;
  const pending = new Map();
  const fail = error => {
    for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
    pending.clear();
  };
  child.stderr.on('data', data => { stderr = (stderr + data).slice(-12000); });
  child.on('error', fail);
  child.on('exit', code => { ended = true; fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { fail(new Error(`Invalid native response: ${line.slice(0, 500)}`)); return; }
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    response.ok ? request.resolve(response.result)
      : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    close() { child.stdin.end(); child.kill(); },
    modules() {
      // Observe the loaded modules: changing PATH alone does not select a DLL beside the executable.
      const result = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
        `(Get-Process -Id ${child.pid}).Modules | Where-Object { $_.ModuleName -in @('TemplateRuntime.dll', 'TubeDesigner.dll', '${basename(bridgePath)}') } | Select-Object ModuleName,FileName | ConvertTo-Json -Compress`],
      { windowsHide: true, encoding: 'utf8', timeout: 30000 });
      assert.equal(result.status, 0, result.stderr || 'Cannot inspect native modules');
      const parsed = JSON.parse(result.stdout.trim());
      const modules = (Array.isArray(parsed) ? parsed : [parsed])
        .map(entry => ({ name: entry.ModuleName, path: entry.FileName, sha256: hash(entry.FileName) }));
      assert.ok(modules.some(entry => entry.name === 'TemplateRuntime.dll'), 'Native TemplateRuntime module was not observed');
      return modules;
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        if (ended) { reject(new Error(`Native process already ended: ${stderr}`)); return; }
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 300000);
        pending.set(id, { method, resolve: resolveRequest, reject, timer });
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

function assertPins(snapshot, reference) {
  const declarationRefs = snapshot.manufacturing.document.processes.flatMap(process =>
    Object.values(process.definition.processInput.resources || {}).map(slot => slot.ref))
    .filter(ref => ref.id === reference.id);
  const executionRefs = snapshot.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessSource']
    .instances.flatMap(instance => Object.values(instance.processInput.resources || {}).map(slot => slot.ref))
    .filter(ref => ref.id === reference.id);
  assert.ok(declarationRefs.length > 0, 'Manufacturing declaration must retain the selected UI pin');
  assert.ok(executionRefs.length > 0, 'Executed assembly processes must retain the selected UI pin');
  for (const ref of [...declarationRefs, ...executionRefs]) assert.deepEqual(ref, reference);
  return { declaration: declarationRefs.length, execution: executionRefs.length };
}

// The catalogue merges former single/two/three/five templates into this real descriptor.
const scenarios = [
  { name: 'single-plane', faceType: 'single', frameManufacturingMode: 'plane_v_notch' },
  { name: 'three-spatial', faceType: 'three', frameManufacturingMode: 'spatial_v_notch' },
  { name: 'five-plane', faceType: 'five', frameManufacturingMode: 'plane_v_notch' },
];
try {
  report.bridgeSha256 = hash(bridgePath);
  const tool = loadTool();
  report.tool = { id: tool.id, version: tool.version, digest: tool.digest };
  for (const scenario of scenarios) {
    const native = connection();
    const current = { name: scenario.name, templateId: 'single-face-security-window', passed: false, stages: [] };
    report.cases.push(current); save();
    const stage = async (name, action) => {
      const started = Date.now();
      try {
        const value = await action();
        current.stages.push({ name, passed: true, milliseconds: Date.now() - started }); save();
        return value;
      } catch (error) {
        current.stages.push({ name, passed: false, milliseconds: Date.now() - started, error: errorText(error) }); save();
        return undefined;
      }
    };
    try {
      const descriptorResult = await stage('GetTemplateDescriptor', () =>
        native.invoke('GetTemplateDescriptor', { templateId: current.templateId }));
      if (!descriptorResult) continue;
      const descriptor = descriptorResult.template;
      current.modules = native.modules();
      current.template = { id: descriptor.id, version: descriptor.version,
        descriptorSha256: hash(resolve(templatesRoot, 'product/single_face_security_window/template.json')),
        scriptSha256: hash(resolve(templatesRoot, 'product/single_face_security_window/template.py')) };
      const field = descriptor.parameters.find(row => row.key === 'outerFrameGrooveTool');
      assert.ok(field, 'Real template must declare the outer-frame tool selector');
      const binding = makeProductToolBinding(field, tool, {}, descriptor);
      assert.deepEqual(binding.ref, { scope: 'system', id: tool.id, version: tool.version, digest: tool.digest });
      const parameters = { ...Object.fromEntries(descriptor.parameters.map(row => [row.key, row.defaultValue])),
        faceType: scenario.faceType, frameManufacturingMode: scenario.frameManufacturingMode,
        accessDoorEnabled: false, verticalMaximumCenterSpacing: 600, sideVerticalMaximumCenterSpacing: 600,
        topBottomRodMaximumCenterSpacing: 600, outerFrameGrooveTool: binding.selectionKey,
        tubeDesignerToolBindings: { [binding.role]: binding } };
      const frozenParameters = structuredClone(parameters);
      const previewInput = { templateId: current.templateId, parameters };
      const frozenPreview = structuredClone(previewInput);
      await stage('GenerateProductTemplatePreview', async () => {
        const preview = await native.invoke('GenerateProductTemplatePreview', previewInput);
        assert.deepEqual(preview.parameters, frozenParameters, 'Native preview must echo host normalized input exactly');
        assert.deepEqual(previewInput, frozenPreview, 'Native preview input must remain unchanged');
        assert.ok(preview.items.length > 0);
        current.previewItems = preview.items.length;
      });
      await stage('GetProductManufacturingPlan', async () => {
        const plan = await native.invoke('GetProductManufacturingPlan', previewInput);
        assert.deepEqual(plan.parameters, frozenParameters);
        assert.deepEqual(previewInput, frozenPreview);
        assert.ok(plan.partCount > 0);
        current.plannedParts = plan.partCount;
      });
      const generated = await stage('GeneratePreview', async () => {
        const input = { templateId: current.templateId, ...parameters };
        const frozen = structuredClone(input);
        const result = await native.invoke('GeneratePreview', input);
        assert.deepEqual(result.tubeDesigner.product.parameters, frozenParameters);
        assert.deepEqual(input, frozen);
        return result;
      });
      if (!generated) continue;
      const before = await native.invoke('InspectScriptResources');
      assert.deepEqual(before.generatedParameters, frozenParameters);
      const members = memberState(generated.tubeDesigner);
      const disassembled = await stage('Disassemble', async () => {
        const input = { productEntityIds: [generated.tubeDesigner.product.entityId] };
        const frozen = structuredClone(input);
        const result = await native.invoke('Disassemble', input);
        assert.deepEqual(input, frozen);
        assert.ok(result.tubeDesigner.parts.length > 0);
        assert.deepEqual(memberState(result.tubeDesigner), members);
        const snapshot = await native.invoke('InspectScriptResources');
        assert.deepEqual(snapshot.generatedParameters, frozenParameters);
        assert.deepEqual(snapshot.manufacturingParameters, frozenParameters);
        assert.deepEqual(snapshot.manufacturingExecution.document.parameters, frozenParameters);
        assert.deepEqual(snapshot.display, before.display);
        current.pins = assertPins(snapshot, binding.ref);
        current.manufacturedParts = result.tubeDesigner.parts.length;
        return { result, snapshot };
      });
      if (disassembled) await stage('SaveAndReopen', async () => {
        const reopened = await native.invoke('SaveAndReopen');
        assert.equal(reopened.savedAndReopened, true);
        assert.deepEqual(memberState(reopened.tubeDesigner), members);
        assert.deepEqual(partState(reopened.tubeDesigner), partState(disassembled.result.tubeDesigner));
        assert.deepEqual(reopened.tubeDesigner.product.parameters, frozenParameters);
        const snapshot = await native.invoke('InspectScriptResources');
        assert.deepEqual(snapshot, disassembled.snapshot);
        assert.deepEqual(assertPins(snapshot, binding.ref), current.pins);
      });
      assert.deepEqual(parameters, frozenParameters, 'The complete UI parameter input must remain unchanged');
      current.parametersEchoedUnchanged = true;
      current.passed = current.stages.every(entry => entry.passed);
    } catch (error) {
      current.error = errorText(error);
    } finally {
      native.close(); save();
      console.log(JSON.stringify(current));
    }
  }
  report.passed = report.cases.length === scenarios.length && report.cases.every(entry => entry.passed);
} catch (error) {
  report.error = errorText(error);
} finally {
  save();
  console.log(JSON.stringify({ passed: report.passed, reportPath }));
  if (!report.passed) process.exitCode = 1;
}
