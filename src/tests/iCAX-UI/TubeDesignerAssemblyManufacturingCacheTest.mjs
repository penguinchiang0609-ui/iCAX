import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { normalizeAssemblyCatalogue } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";
import { assemblyLibraryState, assemblyPreviewKey, ensureAssemblyLibraryPreview,
  handleAssemblyLibraryAction } from "../../apps/tube-designer/webpage/assemblyLibrary.mjs";
import { createFinishedProduct, finishedProductInput } from "../../apps/tube-designer/webpage/finishedProductModel.mjs";

const raw = ["two-end-end-angle", "bend", "node-v-notch-integrated"].map((id) => JSON.parse(readFileSync(
  new URL(`../../apps/tube-designer/templates/assembly/${id}/assembly.json`, import.meta.url), "utf8")));
const matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const noRender = { renderProject() {} };
const deferred = () => { let resolve; const promise = new Promise((done) => { resolve = done; }); return { promise, resolve }; };
const waitFor = async (predicate) => {
  for (let i = 0; i < 40; i++) { if (predicate()) return; await new Promise(setImmediate); }
  assert.fail("expected preview request to start");
};
const fixture = () => {
  const templates = normalizeAssemblyCatalogue(structuredClone(raw));
  const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
    tubeDesignerAssemblyLibrary: { selectedId: "two-end-end-angle" },
    tubeDesignerSystemProfiles: [{ id: "rect", packageDigest: "profile-a", descriptor: { version: 1 } }],
    tubeDesignerUserData: { punchTools: [{ id: "v-notch-sharp", revision: 1 }] } };
  const f = { view, templates, calls: [], nextPlanGate: null, nextPartGate: null, nextResponse: null,
    nextPlanError: null, nativeActive: 0, nativeMaximum: 0 };
  const design = (product) => Object.entries(product.spans).map(([role, span]) => ({
    id: role, role, request: { ...structuredClone(span), features: [], ends: {} }, matrix, compareMatrix: matrix,
  }));
  f.context = { project: { projectId: "project-a" }, sceneProxy: { sceneId: "scene-a", resources: {},
    async invoke(method, payload) {
      f.calls.push({ method, payload: structuredClone(payload) });
      const template = templates.find((item) => item.id === payload.templateId);
      if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return {
        schema: "icax.assembly-applicability", templateId: template.id,
        applicable: template.inputContract.roles.every((role) => payload.processInput.parts[role]),
      };
      if (method === "TubeDesigner.GetAssemblyTemplateExampleProduct") return {
        schema: "icax.assembly-example-product", templateId: template.id,
        finishedProduct: createFinishedProduct(template.exampleInput.shapeId),
      };
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
        if (payload.finishedOnly) return { schema: "icax.finished-product-preview",
          finishedProduct: payload.finishedProduct, layoutShape: payload.finishedProduct.shapeId,
          sceneParameters: payload.finishedProduct.parameters, designParts: design(payload.finishedProduct) };
        assert.equal(payload.manufacturingOnly, true);
        const gate = f.nextPlanGate; f.nextPlanGate = null;
        if (gate) await gate.promise;
        const error = f.nextPlanError; f.nextPlanError = null;
        if (error) throw error;
        assert.ok(payload.processInput && !payload.finishedProduct);
        const spans = Object.values(payload.processInput.parts);
        return { schema: "icax.assembly-preview-plan", templateId: template.id,
          processInput: payload.processInput, parameters: payload.parameters,
          sceneParameters: payload.processInput.geometry, designParts: Object.entries(payload.processInput.parts).map(([role, part]) => ({
            id: role, role, request: { ...structuredClone(part), features: [], ends: {} }, matrix: part.matrix })),
          manufacturingParts: Array.from({ length: template.outputs.manufacturingPartCount }, (_, index) => ({
            id: `blank-${index}`, label: `blank ${index}`, matrix, compareMatrix: matrix,
            request: { profileRef: spans[index].profileRef, parameters: spans[index].parameters,
              length: spans[index].length + index + (payload.parameters.bendRadius ?? 0),
              features: [{ processDrafts: payload.processDrafts }], ends: {} },
          })),
        };
      }
      if (method === "TubeDesigner.PreviewAssemblyManufacturingPart") {
        f.nativeActive++; f.nativeMaximum = Math.max(f.nativeMaximum, f.nativeActive);
        try {
          const gate = f.nextPartGate; f.nextPartGate = null;
          if (gate) await gate.promise;
          const response = f.nextResponse; f.nextResponse = null;
          return response ?? { resultValid: true, previewComputed: true, solidCount: 1,
            geometry: { url: `memory://${payload.previewResourceKey}`, version: 1 } };
        } finally { f.nativeActive--; }
      }
      throw new Error(`unexpected ${method}`);
    } } };
  f.state = () => assemblyLibraryState(view);
  f.counts = () => [f.calls.filter((c) => c.payload.finishedOnly).length,
    f.calls.filter((c) => c.method.endsWith("ResolveAssemblyTemplatePreview") && !c.payload.finishedOnly).length,
    f.calls.filter((c) => c.method.endsWith("PreviewAssemblyManufacturingPart")).length];
  f.ensure = () => ensureAssemblyLibraryPreview(f.context, view);
  f.settle = async (error = false) => {
    for (let i = 0; i < 20; i++) {
      const request = f.ensure(), promises = [request?.promise, f.state().previewRequest?.promise,
        f.state().selectionRequest?.promise].filter(Boolean);
      if (!promises.length) break;
      await Promise.all(promises);
    }
    assert.equal(f.state().previewRequest ?? null, null);
    if (!error) assert.equal(f.state().previewError, "");
  };
  f.action = (name, target = {}) => handleAssemblyLibraryAction(f.context, view, `tube-designer-assembly-${name}`, target, noRender);
  f.mode = async (mode) => f.action("set-view", { dataset: { tubeAssemblyView: mode } });
  f.select = async (id) => f.action("select", { dataset: { tubeAssemblyId: id } });
  f.length = async (value) => {
    await handleAssemblyLibraryAction(f.context, view, "tube-designer-finished-length-change", {
      value: String(value), dataset: { finishedShape: "l", finishedSpan: "armA" },
    }, noRender);
    await f.mode("exploded");
  };
  f.urls = () => f.state().preview.manufacturingParts.map((part) => part.response.geometry.url);
  return f;
};

const completed = fixture();
await completed.settle();
await completed.mode("exploded"); await completed.settle();
assert.deepEqual(completed.counts(), [1, 1, 2]);
const originalUrls = completed.urls();
await completed.select("bend"); await completed.settle();
await completed.mode("exploded"); await completed.settle();
await completed.select("two-end-end-angle"); await completed.settle();
await completed.mode("exploded"); await completed.settle();
assert.deepEqual(completed.counts(), [1, 2, 3], "template return reuses its complete plan and blanks");
assert.deepEqual(completed.urls(), originalUrls);
const originalLength = finishedProductInput(completed.view).spans.armA.length;
await completed.length(originalLength + 7); await completed.settle();
assert.deepEqual(completed.counts(), [2, 3, 4], "unchanged second part is reusable after first-span edit");
assert.notEqual(completed.urls()[0], originalUrls[0]);
assert.equal(completed.urls()[1], originalUrls[1]);
await completed.length(originalLength); await completed.settle();
assert.deepEqual(completed.counts(), [2, 3, 4], "previously resolved independent geometry is reused after restoring the product");
assert.deepEqual(completed.urls(), originalUrls, "restoring old inputs retains original resources");
const reordered = structuredClone(finishedProductInput(completed.view));
const oldKey = assemblyPreviewKey(completed.view);
reordered.parameters = Object.fromEntries(Object.entries(reordered.parameters).reverse());
completed.view.tubeDesignerFinishedProduct.drafts.l = reordered;
assert.equal(assemblyPreviewKey(completed.view), oldKey, "object insertion order is not a cache dependency");
completed.view.tubeDesignerFinishedProduct.manufacturingCache.parts.clear();
completed.state().parameterDrafts["two-end-end-angle"] = { fitGap: 1 };
await completed.settle();
assert.notDeepEqual(completed.urls(), originalUrls, "republication after leaf eviction cannot overwrite a retained plan's URL");
completed.state().parameterDrafts["two-end-end-angle"] = { fitGap: 0 };
await completed.settle();
assert.deepEqual(completed.urls(), originalUrls, "older complete plan retains its original response after same-content republication");

const dependency = fixture();
await dependency.settle(); await dependency.mode("exploded"); await dependency.settle();
for (const mutate of [
  () => { dependency.templates[0].version = "updated"; },
  () => { dependency.templates[0].generationDigest = "a".repeat(64); },
  () => { dependency.templates[0].outputs.descriptorChanged = true; },
  () => { dependency.view.tubeDesignerSystemProfiles[0].packageDigest = "profile-b"; },
  () => { dependency.view.tubeDesignerSystemProfiles[0].revision = 2; },
]) {
  const before = dependency.counts(); const urls = dependency.urls(); mutate(); await dependency.settle();
  await dependency.mode("exploded"); await dependency.settle();
  assert.equal(dependency.counts()[1], before[1] + 1, "descriptor/profile revision changes plan identity");
  assert.equal(dependency.counts()[2], before[2] + 2, "dependency changes force fresh geometry");
  assert.notDeepEqual(dependency.urls(), urls);
}
await dependency.select("node-v-notch-integrated"); await dependency.settle();
await dependency.mode("exploded"); await dependency.settle();
for (const mutate of [
  () => { dependency.view.tubeDesignerUserData.punchTools[0].revision++; },
  () => { dependency.state().processDrafts["node-v-notch-integrated"] = { "node-slot": { "v-notch-sharp": { gap: 0.4 } } }; },
]) {
  const before = dependency.counts(); mutate(); await dependency.settle();
  assert.equal(dependency.counts()[1], before[1] + 1);
  assert.equal(dependency.counts()[2], before[2] + 1, "mold revision and complete processDrafts are dependencies");
}
for (const mutate of [
  () => { dependency.context.project.projectId = "project-b"; },
  () => { dependency.context.sceneProxy.sceneId = "scene-b"; },
]) {
  const before = dependency.counts(); const urls = dependency.urls(); mutate(); await dependency.settle();
  assert.deepEqual(dependency.counts(), before.map((value, index) => value + [1, 1, 1][index]));
  assert.notDeepEqual(dependency.urls(), urls, "same proxy cannot reuse resource URLs across projects/scenes");
}

const pending = fixture();
await pending.settle(); pending.nextPlanGate = deferred(); const planGate = pending.nextPlanGate;
await pending.mode("exploded"); const first = pending.ensure();
await waitFor(() => pending.counts()[1] === 1);
await pending.mode("finished"); await pending.settle();
await pending.mode("exploded"); const resumed = pending.ensure();
planGate.resolve(); await Promise.all([first.promise, resumed.promise]); await pending.settle();
assert.deepEqual(pending.counts(), [1, 1, 2], "same pending plan is shared across view changes");
assert.equal(pending.state().preview.processReady, true);

const serial = fixture();
await serial.settle(); serial.nextPartGate = deferred(); const partGate = serial.nextPartGate;
await serial.mode("exploded"); const stale = serial.ensure();
await waitFor(() => serial.counts()[2] === 1);
await serial.length(originalLength + 3); const latest = serial.ensure();
await waitFor(() => serial.counts()[1] === 2);
assert.equal(serial.counts()[2], 1, "new geometry waits for the active native resource operation");
partGate.resolve(); await Promise.all([stale.promise, latest.promise]); await serial.settle();
assert.equal(serial.nativeMaximum, 1);
assert.equal(serial.state().preview.processReady, true);
assert.equal(serial.state().preview.plan.processInput.parts.memberA.length, originalLength + 3);
assert.equal(serial.counts()[2], 3, "obsolete operation never schedules its second blank");

const selectedMold = fixture();
const selectedTemplate = selectedMold.templates.find((item) => item.id === "node-v-notch-integrated");
selectedTemplate.id = "declarative-user-mold";
selectedTemplate.partProcesses[0].resource = null;
selectedTemplate.partProcesses[0].resourceSelection = { parameter: "selectedMold", resources: [
  { id: "v-notch-sharp", scope: "user" }, { id: "alternate-user-mold", scope: "user" },
] };
selectedTemplate.parameters.push({ key: "selectedMold", valueType: "enum", defaultValue: "v-notch-sharp",
  choices: ["v-notch-sharp", "alternate-user-mold"] });
selectedMold.view.tubeDesignerUserData.punchTools.push({ id: "alternate-user-mold", packageDigest: "tool-a", revision: 1 });
await selectedMold.select(selectedTemplate.id); await selectedMold.settle();
await selectedMold.mode("exploded"); await selectedMold.settle();
let selectedMoldBefore = selectedMold.counts();
selectedMold.view.tubeDesignerUserData.punchTools[0].revision++;
await selectedMold.settle();
assert.deepEqual(selectedMold.counts(), [selectedMoldBefore[0], selectedMoldBefore[1] + 1, selectedMoldBefore[2] + 1],
  "declared resourceSelection.resources includes selected user mold revision");
selectedMold.state().parameterDrafts[selectedTemplate.id] = { selectedMold: "alternate-user-mold" };
await selectedMold.settle();
selectedMoldBefore = selectedMold.counts();
selectedMold.view.tubeDesignerUserData.punchTools[1].packageDigest = "tool-b";
await selectedMold.settle();
assert.deepEqual(selectedMold.counts(), [selectedMoldBefore[0], selectedMoldBefore[1] + 1, selectedMoldBefore[2] + 1],
  "selected alternate mold package digest invalidates even without template digest changes");

for (const invalid of [
  { geometry: { url: "memory://false-success" } },
  { resultValid: true, previewComputed: true, solidCount: 0, geometry: { url: "memory://empty" } },
  { resultValid: false, previewComputed: true, solidCount: 1, geometry: { url: "memory://invalid" } },
  { resultValid: true, previewComputed: true, solidCount: 3, geometry: { url: "memory://multiple" } },
  { resultValid: true, previewComputed: true, solidCount: 1, resultError: "native failed", geometry: { url: "memory://bad" } },
]) {
  const failed = fixture(); await failed.settle(); failed.nextResponse = invalid;
  await failed.mode("exploded"); await failed.settle(true);
  assert.match(failed.state().previewError, /下料形状生成失败/);
  assert.equal(failed.state().preview.processReady, false);
  assert.equal(failed.view.tubeDesignerFinishedProduct.manufacturingCache.plans.size, 0);
  await failed.action("retry-preview"); await failed.settle();
  assert.deepEqual(failed.counts(), [1, 2, 3], "invalid responses are not cached and retry invokes native again");
}
const failedPlan = fixture(); await failedPlan.settle(); failedPlan.nextPlanError = new Error("plan failed");
await failedPlan.mode("exploded"); await failedPlan.settle(true);
await failedPlan.action("retry-preview"); await failedPlan.settle();
assert.deepEqual(failedPlan.counts(), [1, 2, 2]);

const bounded = fixture(); await bounded.settle(); await bounded.mode("exploded"); await bounded.settle();
for (let index = 1; index < 38; index++) {
  finishedProductInput(bounded.view).spans.armB.length = originalLength + 1000 + index * 2;
  await bounded.length(originalLength + index); await bounded.settle();
}
const cache = bounded.view.tubeDesignerFinishedProduct.manufacturingCache;
assert.equal(cache.plans.size, 16);
assert.equal(cache.parts.size, 64);
assert.equal(bounded.nativeMaximum, 1);
const keys = bounded.calls.filter((call) => call.method.endsWith("PreviewAssemblyManufacturingPart"))
  .map((call) => call.payload.previewResourceKey);
assert.equal(new Set(keys).size, keys.length, "every native publication has a distinct URL after cache eviction");
assert.ok([completed, dependency, pending, serial, bounded].every((f) =>
  !f.calls.some((call) => call.method === "TubeDesigner.PreviewPunchWizard")), "manufacturing has no legacy API fallback");
console.log("Assembly manufacturing cache: complete/partial reuse, independent URLs, revisions and scope, in-flight reuse, obsolete responses, serial native calls, strict failures and bounded caches passed.");
