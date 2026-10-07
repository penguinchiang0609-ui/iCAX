// Finished-member separation and real stock machining for security windows.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = resolve(process.env.ICAX_SECURITY_PURE_REPORT || resolve(root, 'output/tests/assembly-process'));
mkdirSync(output, { recursive: true });
const runtime = process.env.ICAX_SECURITY_WINDOW_RUNTIME
  ? resolve(process.env.ICAX_SECURITY_WINDOW_RUNTIME) : null;
const reports = [];
function connection() {
  const child = spawn(process.env.ICAX_SECURITY_WINDOW_NATIVE_BRIDGE
    || resolve(runtime || resolve(root, 'tmp/security-window-frames-native'), 'SecurityWindowFramesBridge.exe'), [], {
    cwd: runtime || process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || root, windowsHide: true,
    env: { ...process.env, PATH: (runtime || resolve(root, 'src/x64/Debug')) + ';' + process.env.PATH }, stdio: ['pipe', 'pipe', 'pipe'] });
  let sequence = 0, stderr = '';
  const pending = new Map();
  const fail = error => { for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); } pending.clear(); };
  child.stderr.on('data', data => stderr += data); child.on('error', fail);
  child.on('exit', code => { if (pending.size) fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response; try { response = JSON.parse(line); } catch { return fail(new Error(line)); }
    const request = pending.get(response.id); if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return { close() { child.stdin.end(); child.kill(); }, invoke(method, payload = {}) {
    return new Promise((resolve, reject) => {
      const id = ++sequence;
      pending.set(id, { method, resolve, reject, timer: setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}`)); }, 300000) });
      child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
    });
  } };
}
const members = state => state.members.map(row => [row.entityId, row.previewGeometryResourceId,
  row.previewGeometryResourceVersion, row.transform]);
const cases = [
  { name: 'single-miter', changes: { faceType: 'single', frameManufacturingMode: 'segment_weld' }, outer: 4 },
  { name: 'single-butt', changes: { faceType: 'single', frameManufacturingMode: 'segment_weld', frameJoinType: 'butt_90' }, outer: 4 },
  { name: 'single-plane-opening', changes: { faceType: 'single', frameManufacturingMode: 'plane_v_notch', accessDoorEnabled: true,
    doorFrameJoinType: 'v_groove_90:tool_library', doorLeafFrameJoinType: 'v_groove_90:tool_library' }, outer: 1 },
  { name: 'three-spatial', changes: { faceType: 'three', frameManufacturingMode: 'spatial_v_notch' }, outer: 3 },
  { name: 'five-plane', changes: { faceType: 'five', frameManufacturingMode: 'plane_v_notch' }, outer: 6 },
];
try {
  for (const scenario of cases) {
    if (process.env.ICAX_SECURITY_PURE_CASE && process.env.ICAX_SECURITY_PURE_CASE !== scenario.name) continue;
    const native = connection();
    try {
      const templateId = 'single-face-security-window';
      const descriptor = (await native.invoke('GetTemplateDescriptor', { templateId })).template;
      const parameters = { ...Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue])),
        accessDoorEnabled: false, verticalMaximumCenterSpacing: 600, sideVerticalMaximumCenterSpacing: 600,
        topBottomRodMaximumCenterSpacing: 600, ...scenario.changes };
      const preview = await native.invoke('GenerateProductTemplatePreview', { templateId, parameters });
      assert.deepEqual(preview.parameters, parameters);
      const plan = await native.invoke('GetProductManufacturingPlan', { templateId, parameters });
      assert.deepEqual(plan.parameters, parameters, 'Native plan must preserve the complete normalized input');
      const generated = await native.invoke('GeneratePreview', { templateId, ...parameters });
      const finished = members(generated.tubeDesigner);
      const disassembled = await native.invoke('Disassemble');
      assert.deepEqual(members(disassembled.tubeDesigner), finished);
      const parts = disassembled.tubeDesigner.parts;
      const quantity = rows => rows.reduce((total, part) => total + part.unitQuantity, 0);
      assert.equal(disassembled.geometryChecks.length, parts.length);
      assert.equal(quantity(parts), plan.partCount, 'Merged rows must retain every physical stock');
      const outerParts = parts.filter(part => part.stableKey.startsWith('outer_frame.'));
      assert.equal(quantity(outerParts), scenario.outer);
      for (const check of disassembled.geometryChecks) {
        assert.equal(check.valid, true, check.key); assert.equal(check.solids, 1, check.key); assert.ok(check.volume > 0, check.key);
      }
      const inspected = await native.invoke('InspectNativeGeometry');
      const resources = await native.invoke('InspectScriptResources');
      const processPlan = resources.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessNative'].plan;
      assert.ok(processPlan.instances.length, 'Final native stocks must retain their actual assembly instances');
      if (scenario.name === 'single-plane-opening') {
        const foldedStocks = new Set(processPlan.forming.map(forming => forming.stockId));
        for (const prefix of ['access_door.fixed_frame.', 'access_door.leaf.frame.'])
          assert.ok([...foldedStocks].some(stock => stock.startsWith(prefix)),
            `${prefix}: independent frame must retain its actual forming calls (${[...foldedStocks].join(', ')})`);
      }
      if (scenario.name === 'three-spatial' || scenario.name === 'five-plane') {
        const posts = outerParts.filter(part => part.stableKey.startsWith('outer_frame.vertical.'));
        assert.equal(quantity(posts), scenario.name === 'three-spatial' ? 2 : 4);
      }
      if (!process.env.ICAX_SECURITY_PURE_SKIP_SAVE) {
        const reopened = await native.invoke('SaveAndReopen');
        assert.deepEqual(members(reopened.tubeDesigner), finished);
        assert.deepEqual(reopened.tubeDesigner.product.parameters, parameters);
        assert.deepEqual(reopened.tubeDesigner.parts, parts, 'Save/open must preserve merged rows and source mappings');
        assert.deepEqual(await native.invoke('InspectScriptResources'), resources,
          'Save/open must preserve the exact design, manufacturing definitions and executed assembly plan');
        const after = await native.invoke('InspectNativeGeometry');
        assert.ok(inspected.canonicalStockResources.length, 'processed and raw stocks must be frozen project resources');
        assert.deepEqual(after.canonicalStockResources, inspected.canonicalStockResources,
          'save/open must preserve exact raw and processed stock resources');
        assert.deepEqual(after.geometryChecks, inspected.geometryChecks);
      }
      const report = { name: scenario.name, partCount: plan.partCount, partTypes: parts.length,
        outerStocks: quantity(outerParts),
        validSingleSolids: disassembled.geometryChecks.length, finishedUnchanged: true,
        instanceCount: processPlan.instances.length,
        savedBytesEqual: !process.env.ICAX_SECURITY_PURE_SKIP_SAVE };
      reports.push(report); console.log(JSON.stringify(report));
    } finally { native.close(); }
  }
} finally {
  const tag = process.env.ICAX_SECURITY_PURE_CASE || 'all';
  writeFileSync(resolve(output, `security-pure-native-${tag}.json`), JSON.stringify(reports, null, 2));
}
