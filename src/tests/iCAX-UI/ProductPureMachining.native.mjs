// Real descriptor validation, function-owned machining, and saved BRep checks.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repository = fileURLToPath(new URL("../../../", import.meta.url));
const binaryRoot = resolve(repository, "src/x64/Debug");
const workingRoot = process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || repository;
const executable = resolve(repository, "tmp/security-window-frames-native/SecurityWindowFramesBridge.exe");
const output = resolve(repository, "output/tests/assembly-process");
mkdirSync(output, { recursive: true });
const deployed = workingRoot !== repository;
const report = [];

class Native {
  constructor() {
    this.child = spawn(executable, [], { cwd: workingRoot, windowsHide: true,
      env: { ...process.env, PATH: `${binaryRoot};${process.env.PATH}` }, stdio: ["pipe", "pipe", "pipe"] });
    this.sequence = 0; this.pending = new Map(); this.stderr = "";
    const fail = (error) => { for (const pending of this.pending.values()) {
      clearTimeout(pending.timer); pending.reject(error);
    } this.pending.clear(); };
    this.child.stderr.on("data", (data) => { this.stderr += data; });
    this.child.on("error", fail);
    this.child.on("exit", (code) => { if (this.pending.size) fail(new Error(`Native exited ${code}: ${this.stderr}`)); });
    createInterface({ input: this.child.stdout }).on("line", (line) => {
      let response;
      try { response = JSON.parse(line); } catch { return fail(new Error(line)); }
      const pending = this.pending.get(response.id);
      if (!pending) return;
      this.pending.delete(response.id); clearTimeout(pending.timer);
      response.ok ? pending.resolve(response.result) : pending.reject(new Error(response.error));
    });
  }
  invoke(method, payload = {}) {
    return new Promise((resolveRequest, reject) => {
      const id = ++this.sequence;
      this.pending.set(id, { resolve: resolveRequest, reject,
        timer: setTimeout(() => { this.pending.delete(id); reject(new Error(`Native timeout ${method}`)); }, 240000) });
      this.child.stdin.write(`${JSON.stringify({ id, method, payload })}\n`);
    });
  }
  close() { this.child.stdin.end(); }
}

const keyOf = (row) => row.stableKey || row.itemKey || row.key;
const members = (state) => state.members.map((row) => [row.entityId, row.previewGeometryResourceId,
  row.previewGeometryResourceVersion, row.transform]);
const checksById = (checks) => Object.fromEntries(checks.map((check) => [check.entityId, check]));

async function productCase(name, templateId, changes, expectedFunctions, expectedCount) {
  const native = new Native();
  try {
    const descriptor = (await native.invoke("GetTemplateDescriptor", { templateId })).template;
    const parameters = { ...Object.fromEntries(descriptor.parameters.map((parameter) => [parameter.key, parameter.defaultValue])), ...changes };
    const preview = await native.invoke("GenerateProductTemplatePreview", { templateId, parameters });
    assert.deepEqual(preview.parameters, parameters);
    assert.ok(preview.items.length > 0);
    const plan = await native.invoke("GetProductManufacturingPlan", { templateId, parameters });
    assert.deepEqual(plan.parameters, parameters);
    const generated = await native.invoke("GeneratePreview", { templateId, ...parameters });
    const finished = members(generated.tubeDesigner);
    const disassembled = await native.invoke("Disassemble");
    assert.deepEqual(members(disassembled.tubeDesigner), finished, "machining cannot alter finished member resources or placement");
    assert.equal(disassembled.geometryChecks.length, plan.partCount);
    for (const check of disassembled.geometryChecks) {
      assert.equal(check.valid, true, check.key); assert.equal(check.solids, 1, check.key); assert.ok(check.volume > 0, check.key);
    }
    const before = await native.invoke("InspectNativeGeometry");
    assert.equal(before.processPlans.length, 1, "function instances must be stored in the generation run");
    const instances = before.processPlans[0].instances;
    assert.equal(new Set(instances.map((instance) => instance.instanceId)).size, instances.length);
    for (const functionId of expectedFunctions) assert.ok(instances.some((instance) => instance.templateId === functionId), functionId);
    if (expectedCount !== undefined) assert.equal(instances.length, expectedCount);
    const grouped = new Map();
    for (const instance of instances) {
      assert.ok(instance.functionDigest && instance.result.applicable);
      assert.deepEqual(instance.parameters, instance.result.parameters);
      const previous = grouped.get(instance.stockId);
      if (previous) assert.equal(instance.targetGeometry, previous.resultGeometry, "later operations must continue on the same raw stock");
      grouped.set(instance.stockId, instance);
    }
    assert.ok(instances.some((instance, index) => instances.slice(0, index).some((previous) => previous.stockId === instance.stockId)), "one actual stock must accept repeat operations");
    const reopened = await native.invoke("SaveAndReopen");
    assert.equal(reopened.savedAndReopened, true);
    assert.deepEqual(members(reopened.tubeDesigner), finished);
    const after = await native.invoke("InspectNativeGeometry");
    assert.deepEqual(after.processPlans, before.processPlans, "saved function inputs, parameters, operation roots, and digests must be exact");
    assert.deepEqual(checksById(after.geometryChecks), checksById(before.geometryChecks), "save/open must restore identical BRep bytes and physical measurements");
    const repeated = await native.invoke("Disassemble");
    assert.deepEqual(repeated.tubeDesigner.parts.map((row) => row.entityId).sort(), disassembled.tubeDesigner.parts.map((row) => row.entityId).sort());
    assert.deepEqual(members(repeated.tubeDesigner), finished);
    const result = { name, templateId, partCount: plan.partCount, instanceCount: instances.length,
      functions: [...new Set(instances.map((instance) => instance.templateId))], valid: true, savedBytesEqual: true };
    report.push(result); console.log(JSON.stringify(result));
  } finally { native.close(); }
}

async function independentPlan() {
  const native = new Native();
  try {
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const stock = { id: "joint-stock", profileRef: { scope: "system", id: "rect" },
      parameters: { width: 40, depth: 30, wallThickness: 2, cornerRadius: 0, innerRadius: 0 }, length: 400, matrix: identity };
    const instance = (instanceId, end, station, mateDirection) => ({ instanceId, templateId: "tube-end-joint",
      processInput: { schema: "icax.assembly-process-input", schemaVersion: 1,
        parts: { stock: structuredClone(stock) }, geometry: { mode: "miter", end, station, mateDirection } },
      parameters: {}, targets: { stock: stock.id } });
    const first = instance("start-cut", "start", 0, [0, 1, 0]);
    const second = instance("end-cut", "end", 400, [0, -1, 0]);
    const evaluated = await native.invoke("EvaluateAssemblyProcess", { templateId: "tube-end-joint", processInput: first.processInput, parameters: {} });
    assert.equal(evaluated.applicable, true, evaluated.reason);
    const source = { stocks: [stock], instances: [first, second, structuredClone(first)] };
    const resolved = await native.invoke("ResolveAssemblyProcessPlan", source);
    assert.equal(resolved.instances.length, 2); assert.equal(resolved.manufacturingParts.length, 1);
    assert.equal(resolved.manufacturingParts[0].request.neutralOperations.length, 2);
    const preview = await native.invoke("PreviewAssemblyProcessPlan", { ...source, expectedPlanDigest: resolved.planDigest });
    assert.equal(preview.geometryChecks.length, 1);
    assert.equal(preview.geometryChecks[0].valid, true); assert.equal(preview.geometryChecks[0].solids, 1);
    const request = { ...source, planId: "local-joint-plan", requestId: "save-local-joints", expectedPlanDigest: resolved.planDigest };
    const saved = await native.invoke("CommitAssemblyProcessPlan", request);
    const id = saved.entityIds[stock.id];
    assert.ok(id);
    const inspected = await native.invoke("InspectNativeGeometry", { entityIds: [id], points: [
      { entityId: id, point: [200.0, 19.0, 0.0] },
      { entityId: id, point: [1.0, 19.0, 0.0] },
      { entityId: id, point: [399.0, -19.0, 0.0] }] });
    const shape = inspected.geometryChecks.find((check) => check.entityId === id);
    assert.ok(shape); assert.equal(shape.pointChecks[0].inside, true);
    assert.equal(shape.pointChecks[1].outside, true, "start-end cutter must remove actual wall material");
    assert.equal(shape.pointChecks[2].outside, true, "end-end cutter must independently remove actual wall material");
    assert.ok(shape.volume < 400 * (40 * 30 - 36 * 26));
    const retry = await native.invoke("CommitAssemblyProcessPlan", request);
    assert.equal(retry.replayed, true); assert.deepEqual(retry.entityIds, saved.entityIds);
    const before = await native.invoke("GetAssemblyProcessPlans");
    await native.invoke("SaveAndReopen");
    assert.deepEqual(await native.invoke("GetAssemblyProcessPlans"), before);
    const after = await native.invoke("InspectNativeGeometry", { entityIds: [id] });
    const reopened = after.geometryChecks.find((check) => check.entityId === id);
    assert.equal(reopened.resourceHash, shape.resourceHash); assert.equal(reopened.resourceBytes, shape.resourceBytes);
    assert.equal(reopened.volume, shape.volume);
    const result = { name: "independent-repeat-joints", partCount: 1, instanceCount: 2,
      valid: true, bothEndsCut: true, retryOnce: true, savedBytesEqual: true };
    report.push(result); console.log(JSON.stringify(result));
  } finally { native.close(); }
}

async function worldSpacePlan() {
  const native = new Native();
  try {
    const matrix = [0, -1, 0, 100, 1, 0, 0, 50, 0, 0, 1, 20, 0, 0, 0, 1];
    const stock = { id: 'world-stock', profileRef: { scope: 'system', id: 'rect' },
      parameters: { width: 40, depth: 30, wallThickness: 2, cornerRadius: 0, innerRadius: 0 }, length: 400, matrix };
    const instance = { instanceId: 'world-hole', templateId: 'structural-apertures', parameters: {}, targets: { stock: stock.id },
      processInput: { schema: 'icax.assembly-process-input', schemaVersion: 1,
        parts: { stock: structuredClone(stock) }, geometry: { holes: [{ diameter: 6,
          placement: { origin: [100, 250, -5], xAxis: [0, 1, 0], yAxis: [-1, 0, 0] }, vector: [0, 0, 50] }] } } };
    const source = { stocks: [stock], instances: [instance] };
    const wrongFrame = structuredClone(source);
    wrongFrame.instances[0].processInput.parts.stock.matrix[3] += 100;
    await assert.rejects(native.invoke('ResolveAssemblyProcessPlan', wrongFrame), /原材坐标/);
    const resolved = await native.invoke('ResolveAssemblyProcessPlan', source);
    const preview = await native.invoke('PreviewAssemblyProcessPlan', { ...source, expectedPlanDigest: resolved.planDigest });
    assert.equal(preview.geometryChecks[0].valid, true); assert.equal(preview.geometryChecks[0].solids, 1);
    const expectedVolume = 400 * (40 * 30 - 36 * 26) - Math.PI * 3 * 3 * 4;
    assert.ok(Math.abs(preview.geometryChecks[0].volume - expectedVolume) < 1e-5,
      'document-space preview must cut the placed stock at its actual material station');
    const saved = await native.invoke('CommitAssemblyProcessPlan', { ...source, planId: 'world-frame-plan',
      requestId: 'world-frame-request', expectedPlanDigest: resolved.planDigest });
    const entityId = saved.entityIds[stock.id];
    const inspected = await native.invoke('InspectNativeGeometry', { entityIds: [entityId], points: [
      { entityId, point: [200, 0, 14] }, { entityId, point: [190, 0, 14] }, { entityId, point: [200, 0, -14] }] });
    const shape = inspected.geometryChecks[0];
    assert.equal(shape.valid, true); assert.equal(shape.solids, 1);
    assert.ok(Math.abs(shape.volume - preview.geometryChecks[0].volume) < 1e-6, 'native preview and commit must agree');
    assert.equal(shape.pointChecks[0].outside, true); assert.equal(shape.pointChecks[1].inside, true);
    assert.equal(shape.pointChecks[2].outside, true);
    await native.invoke('SaveAndReopen');
    const after = await native.invoke('InspectNativeGeometry', { entityIds: [entityId] });
    assert.equal(after.geometryChecks[0].resourceHash, shape.resourceHash);
    report.push({ name: 'world-space-plan', partCount: 1, valid: true, previewCommitEqual: true, actualFrameChecked: true, savedBytesEqual: true });
    console.log(JSON.stringify(report.at(-1)));
  } finally { native.close(); }
}

// Retired door/window native cases are excluded from this active suite.
try {
  if (!process.env.ICAX_NATIVE_PURE_FILTER || /independent/.test(process.env.ICAX_NATIVE_PURE_FILTER)) await independentPlan();
  if (!process.env.ICAX_NATIVE_PURE_FILTER || /world-space/.test(process.env.ICAX_NATIVE_PURE_FILTER)) await worldSpacePlan();
} finally {
  writeFileSync(resolve(output, `product-pure-native-${process.env.ICAX_NATIVE_PURE_REPORT_TAG || (deployed ? "deployed" : "source")}.json`), JSON.stringify(report, null, 2));
}
