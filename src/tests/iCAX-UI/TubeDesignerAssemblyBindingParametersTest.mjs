import assert from "node:assert/strict";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { assemblyBindingAxisAngle, assemblyBindingParameterValues, assemblyProcessWithInput,
  finishedProductInput } from "../../apps/tube-designer/webpage/finishedProductModel.mjs";
import { assemblyParameterValues, handleAssemblyLibraryAction, renderAssemblyLibraryRightPane } from "../../apps/tube-designer/webpage/assemblyLibrary.mjs";
import { handleProductAssemblyBindingAction, productAssemblyBindingDraft, productAssemblyBindingIdentity,
  productAssemblyBindingInputKey, productAssemblyBindingsState, renderProductAssemblyBindings } from "../../apps/tube-designer/webpage/productAssemblyBindings.mjs";
import { productAssemblyConnectionsState, productAssemblyConnectionIdentity } from "../../apps/tube-designer/webpage/productAssemblyConnections.mjs";

const root = new URL("../../apps/tube-designer/templates/assembly/", import.meta.url);
const templates = readdirSync(root, { withFileTypes: true }).filter((entry) => entry.isDirectory()
  && existsSync(new URL(`${entry.name}/assembly.json`, root)))
  .map((entry) => JSON.parse(readFileSync(new URL(`${entry.name}/assembly.json`, root), "utf8")));
const load = (id) => structuredClone(templates.find((template) => template.id === id));
const defaults = (template) => Object.fromEntries(template.parameters.map((p) => [p.key, p.defaultValue]));

// Native binding descriptors permit precisely the process fields and explicit
// input aliases; independent shape fields remain editable regardless of aliases.
for (const stored of templates) {
  const before = structuredClone(stored);
  const adapted = assemblyProcessWithInput(stored);
  assert.equal(adapted.parameters.filter((p) => p.scope === "scene").length, stored.inputContract.geometryParameters.length, stored.id);
  const keys = [...new Set([...stored.parameters.map((p) => p.key), ...stored.inputContract.geometryParameters.map((p) => p.key)])];
  assert.deepEqual(Object.keys(assemblyBindingParameterValues(adapted, defaults(adapted))).sort(), keys.sort(), stored.id);
  assert.deepEqual(stored, before, "the editor must not rewrite a stored template");
}

function configured(stored, topology, rawAngle) {
  const template = assemblyProcessWithInput(stored);
  const view = { activeAreaId: "assemblies", scene: { tubeDesigner: {
    activeProductId: "product", generationRun: { entityId: "run" }, product: {},
  } }, tubeDesignerAssemblyTemplates: [stored], tubeDesignerAssemblyLibrary: {
    selectedId: stored.id, workMode: "product", workModeUserSelected: true, selectedProductConnectionKey: "node",
    selectedProductConnectionProductKey: "product/run",
    selectedProductTemplateConnectionKey: "node", selectedProductTemplateId: stored.id,
    parameterDrafts: {}, processDrafts: {},
  } };
  const participants = stored.participants.map((p, index) => ({ memberEntityId: `member-${index}`, itemKey: `part-${index}` }));
  const anchors = stored.participants.map((p, index) => ({ itemKey: `part-${index}`, anchor:
    stored.productBinding?.anchors?.[p.role] === "side" ? { kind: "side", face: "top", reference: "start", station: 120 }
      : { kind: "end", end: index === 0 ? "end" : "start" } }));
  const connection = { key: "node", participants, properties: { topology, participantAnchors: anchors },
    nodeGeometry: { nodeGeometryStatus: "verified", axisAngleDeg: rawAngle } };
  const result = { productEntityId: "product", generationRunId: "run", sourceRevision: "1", bindings: [],
    members: participants.map((p) => ({ ...p, length: 500 })), connections: [connection],
    capabilities: { anchorKinds: ["side", "end"], templates: [{ templateId: stored.id, supported: true }] } };
  const identity = productAssemblyBindingIdentity(view);
  Object.assign(productAssemblyBindingsState(view), { key: identity.key, productKey: identity.productKey, status: "ready", result });
  Object.assign(productAssemblyConnectionsState(view), { key: productAssemblyConnectionIdentity(view).key, status: "ready", result });
  productAssemblyBindingDraft(view, template).connectionKey = connection.key;
  renderProductAssemblyBindings(view, { template, parameters: assemblyParameterValues(view, template), processDrafts: {} });
  return { view, template, connection };
}

for (const [id, topology, raw, expected, alias] of [
  ["two-end-end-angle", "L", 120, 60, "jointAngle"],
  ["t-profile-insert", "T", 65, 65, "intersectionAngle"],
]) {
  const { view, template, connection } = configured(load(id), topology, raw);
  view.tubeDesignerAssemblyLibrary.parameterDrafts[id] = { [alias]: 90 };
  assert.equal(assemblyParameterValues(view, template)[alias], expected, "verified node geometry replaces an old angle draft");
  const calls = [];
  const context = { sceneProxy: { invoke(method, payload) { calls.push({ method, payload }); return Promise.resolve({}); } } };
  await handleAssemblyLibraryAction(context, view, "tube-designer-binding-preview", {}, { renderProject() {} });
  assert.equal(calls.length, 1);
  assert.equal(calls[0].payload.parameters[alias], expected);
  assert.deepEqual(Object.keys(calls[0].payload.parameters).sort(),
    [...new Set([...load(id).parameters.map((p) => p.key), ...load(id).inputContract.geometryParameters.map((p) => p.key)])].sort());
  assert.match(renderAssemblyLibraryRightPane({}, view), new RegExp(`成品节点轴夹角：${expected}°`));
  connection.nodeGeometry.nodeGeometryStatus = "invalid";
  assert.equal(assemblyParameterValues(view, template)[alias], null);
  await handleAssemblyLibraryAction(context, view, "tube-designer-binding-preview", {}, { renderProject() {} });
  assert.equal(calls.length, 1, "missing verified node geometry cannot be replaced by the 90 degree default");
  assert.match(productAssemblyBindingDraft(view, template).previewError, /轴夹角未核验/);
}

// Neither fixed-angle joints nor parallel fastening introduce product fields
// into a process request; direct callers receive the same projection as the UI.
for (const [id, topology] of [["wrap-a-over-b", "L"], ["end-side-tab-slot", "T"], ["mechanical-fastener", "parallel"]]) {
  const { view, template, connection } = configured(load(id), topology, 90);
  const parameters = assemblyParameterValues(view, template);
  const input = { template, parameters, processDrafts: {} };
  const before = structuredClone(parameters);
  const calls = [];
  await handleProductAssemblyBindingAction({ sceneProxy: { invoke(method, payload) {
    calls.push({ method, payload }); return Promise.resolve({});
  } } }, view, "tube-designer-binding-preview", {}, { renderProject() {} }, () => input);
  assert.equal(calls.length, 1);
  assert.deepEqual(calls[0].payload.parameters, assemblyBindingParameterValues(template, defaults(template)));
  assert.deepEqual(parameters, before);
  const key = productAssemblyBindingInputKey(view, input);
  parameters.exampleMemberLength = 123;
  parameters.exampleSectionWidth = 456;
  assert.equal(productAssemblyBindingInputKey(view, input), key, "unbound example dimensions cannot stale a product candidate");
  if (template.productBinding?.axisAngle) {
    delete connection.nodeGeometry;
    await handleProductAssemblyBindingAction({ sceneProxy: { invoke() { throw Error("unverified angle submitted"); } } },
      view, "tube-designer-binding-preview", {}, { renderProject() {} }, () => input);
    assert.match(productAssemblyBindingDraft(view, template).previewError, /轴夹角未核验/);
  }
}

const renamed = load("two-end-end-angle");
renamed.id = "unrecognized-process";
renamed.exampleInput.parameterBindings = { turn: "angle" };
renamed.inputContract.geometryParameters[0].key = "turn";
renamed.inputContract.requirements.turn = renamed.inputContract.requirements.jointAngle;
delete renamed.inputContract.requirements.jointAngle;
renamed.productBinding.axisAngle.expected.parameter = "turn";
const renamedCase = configured(renamed, "L", 120);
assert.equal(assemblyParameterValues(renamedCase.view, renamedCase.template).turn, 60);
assert.equal(assemblyBindingAxisAngle(renamedCase.template, renamedCase.connection), 60);
renamedCase.connection.nodeGeometry.axisAngleDeg = 5;
assert.equal(assemblyParameterValues(renamedCase.view, renamedCase.template).turn, null,
  "a measured angle outside the template's input requirements must not silently use a default");

const exampleView = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [load("bend")],
  tubeDesignerAssemblyLibrary: { selectedId: "bend", workMode: "example", workModeUserSelected: true } };
await handleAssemblyLibraryAction({}, exampleView, "tube-designer-finished-parameter-change", {
  dataset: { finishedShape: "l", finishedParameter: "planeRotation" }, value: "35",
}, { renderProject() {} });
assert.equal(finishedProductInput(exampleView).parameters.planeRotation, 35,
  "a product field remains editable even when the current process does not bind it");
console.log(`TubeDesignerAssemblyBindingParametersTest: passed (${templates.length} templates)`);
