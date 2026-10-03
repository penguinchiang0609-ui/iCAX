import assert from "node:assert/strict";
import {
  ensureProductAssemblyBindings, handleProductAssemblyBindingAction,
  productAssemblyBindingDraft, productAssemblyBindingIdentity, productAssemblyBindingsState,
  productAssemblyCandidateCanApply, renderProductAssemblyBindingOverview, renderProductAssemblyBindings,
} from "../../apps/tube-designer/webpage/productAssemblyBindings.mjs";

const template = { id: "test-joint", version: "1", displayName: "孔槽连接",
  productBinding: { mode: "incremental-cut", compatibleProductTopologies: [{ topology: "T" }, { topology: "L" }] }, participants: [
  { role: "host", label: "主管" }, { role: "branch", label: "支管" },
], partProcesses: [{ id: "cut", label: "主管开孔" }] };
const view = { activeAreaId: "assemblies", scene: { tubeDesigner: {
  activeProductId: "product-a", generationRun: { entityId: "run-a" }, product: { assemblyBindingsRevision: "r1" },
} }, tubeDesignerAssemblyTemplates: [template], tubeDesignerAssemblyLibrary: {
  selectedId: template.id, parameterDrafts: {}, processDrafts: {},
} };
let input = { template, parameters: { gap: 1 }, processDrafts: {} };
const getInput = () => input;
const pending = [];
const calls = [];
const context = { sceneProxy: { invoke(method, payload) {
  calls.push({ method, payload });
  return new Promise((resolve, reject) => pending.push({ method, payload, resolve, reject }));
} } };
const ops = { renderProject() {} };
const result = (payload, bindings = [], sourceRevision = "s1") => ({
  ...payload, sourceRevision, members: [
    { memberEntityId: "member-a", name: "真实主管", length: 500 },
    { memberEntityId: "member-b", name: "真实支管", length: 200 },
  ], bindings, capabilities: { detail: "端部和侧面减料加工", anchorKinds: ["end", "side"],
    templates: [{ templateId: template.id, supported: true, detail: "当前支持主管开孔" }] },
  connections: [
    { key: "joint-ab", participants: [{ memberEntityId: "member-a", name: "真实主管" }, { memberEntityId: "member-b", name: "真实支管" }], properties: { topology: "T" } },
    { key: "joint-other", participants: [{ memberEntityId: "member-a", name: "真实主管" }, { memberEntityId: "member-c", name: "其他构件" }], properties: { topology: "T" } },
  ],
});
const candidate = (overrides = {}) => ({ canApply: true, supportStatus: "supported", candidateToken: "token-1",
  manufacturingEffect: "incremental-cut", assemblyFitStatus: "not-verified", sourceRevision: "s1",
  checks: [{ status: "pass", detail: "真实截面有效" }], workflow: {
    blankParts: [{ label: "主管下料件", participantRoles: ["host"] }],
    partOperations: [{ processId: "cut", role: "host", memberEntityId: "member-a", placement: { face: "top", reference: "start", station: 125, arrayCount: 2, arrayPitch: 50 } }],
    assemblySteps: [{ label: "连接定位", distance: 20, unit: "mm" }],
    bom: [{ label: "螺钉", quantity: 2, unit: "件", specificationStatus: "requires-standard" }],
  }, ...overrides });
const action = (name, value = "", dataset = {}) => handleProductAssemblyBindingAction(context, view,
  `tube-designer-binding-${name}`, { value, dataset }, ops, getInput);
async function configure() {
  for (const [role, member, anchor] of [["host", "member-a", "side:top"], ["branch", "member-b", "start"]]) {
    await action("member", member, { tubeBindingRole: role });
    await action("anchor", anchor, { tubeBindingRole: role });
  }
  await action("station", "120", { tubeBindingRole: "host" });
}

const first = ensureProductAssemblyBindings(context, view, ops);
assert.equal(ensureProductAssemblyBindings(context, view, ops), first);
pending.shift().resolve(result(calls[0].payload));
await first;
productAssemblyBindingsState(view).result.manufacturingPlan = {
  schema: "icax.product-assembly-manufacturing-plan", schemaVersion: 1,
  selectedConnectionCount: 2, unassignedConnectionCount: 1, fitStatus: "not-verified", readiness: "not-ready",
  members: [{ itemKey: "frame.left", name: "左立柱", partMappingStatus: "persisted", partLength: 1500,
    connections: [{ key: "corner-l", status: "applied" }, { key: "junction-t", status: "applied" }],
    operations: [{ processId: "miter" }, { processId: "side-slot" }] },
  { itemKey: "frame.top", name: "上横管", partMappingStatus: "not-present",
    connections: [{ key: "corner-top", status: "unassigned" }], operations: [] }],
};
const planHtml = renderProductAssemblyBindingOverview(view, "选择连接节点。");
assert.match(planHtml, /当前下料汇总 · 2\/3 处连接/);
assert.match(planHtml, /当前下料汇总 · 2\/3 处连接 · 待验证/);
assert.match(planHtml, /左立柱<\/strong><span>1500 mm · 2\/2 处连接 · 2 道加工/);
assert.equal(planHtml.match(/左立柱<\/strong>/g)?.length, 1, "one shared member is one blank row");
assert.match(planHtml, /上横管<\/strong><span>下料件未就绪/);
assert.match(planHtml, /装配配合待核对/);
productAssemblyBindingsState(view).result.manufacturingPlan.reason = "仍有未选工艺的连接节点";
assert.match(renderProductAssemblyBindingOverview(view, "选择连接节点。"), /仍有未选工艺的连接节点/);
const oldPlan = productAssemblyBindingsState(view).result.manufacturingPlan;
const sourceMembers = ["左侧上横管", "正面上横管", "右侧上横管"].map((name, index) => ({
  memberEntityId: `member-${index}`, itemKey: `top-${index}`, name,
  spans: [{ stockStart: index * 300, startReserve: 0, endReserve: 0 }],
}));
productAssemblyBindingsState(view).result.manufacturingPlan = { ...oldPlan, stocks: [{
  stockEntityId: "stock-top", itemKey: "blank-top", name: "顶部连续母材", partLength: 900,
  partMappingStatus: "persisted", sourceMembers, operations: [{ id: "left-fold" }, { id: "right-fold" }],
}] };
const sharedPlanBefore = JSON.stringify(productAssemblyBindingsState(view).result.manufacturingPlan);
const sharedPlanHtml = renderProductAssemblyBindingOverview(view, "选择连接节点。");
assert.equal(sharedPlanHtml.match(/data-tube-manufacturing-stock=/g)?.length, 1,
  "three product members sharing one stock render one manufacturing row");
assert.match(sharedPlanHtml, /顶部连续母材<\/strong><span>900 mm · 对应 3 根成品管件 · 2 道加工/);
assert.match(sharedPlanHtml, /左侧上横管 \+ 正面上横管 \+ 右侧上横管/);
assert.doesNotMatch(sharedPlanHtml, /1500 mm|下料件未就绪/);
assert.match(sharedPlanHtml, /当前下料汇总 · 1 件下料 · 下料已保存/);
assert.doesNotMatch(sharedPlanHtml, /当前下料汇总 · 2\/3 处连接|仍有未选工艺的连接节点/,
  "automatic stock status cannot reuse the legacy manual binding counts");
assert.equal(JSON.stringify(productAssemblyBindingsState(view).result.manufacturingPlan), sharedPlanBefore);
productAssemblyBindingsState(view).result.manufacturingPlan.stocks[0].partMappingStatus = "planned";
assert.match(renderProductAssemblyBindingOverview(view, ""), /900 mm · 待生成/);
productAssemblyBindingsState(view).result.manufacturingPlan.stockExecutionStatus = "planned";
assert.match(renderProductAssemblyBindingOverview(view, ""), /1 件下料 · 下料待生成/);
productAssemblyBindingsState(view).result.manufacturingPlan.planningError = "加工失败：<真实原因>";
const failedStockHtml = renderProductAssemblyBindingOverview(view, "");
assert.match(failedStockHtml, /1 件下料 · 下料规划失败/);
assert.match(failedStockHtml, /role="alert">加工失败：&lt;真实原因&gt;/);
delete productAssemblyBindingsState(view).result.manufacturingPlan.planningError;
delete productAssemblyBindingsState(view).result.manufacturingPlan.stockExecutionStatus;
productAssemblyBindingsState(view).result.manufacturingPlan.stocks = [];
assert.doesNotMatch(renderProductAssemblyBindingOverview(view, ""), /左立柱|上横管/,
  "an explicitly empty stock plan cannot fall back to logical member blanks");
assert.match(renderProductAssemblyBindingOverview(view, ""), /0 件下料 · 暂无下料/);
productAssemblyBindingsState(view).result.manufacturingPlan = oldPlan;
const beforeSelection = calls.length;
await action("preview");
assert.equal(calls.length, beforeSelection, "a committed connection is required before preview");
assert.match(renderProductAssemblyBindings(view, input), /请先选择当前产品已提交的真实连接/);
await action("connection", "joint-other");
assert.match(renderProductAssemblyBindings(view, input), /value="joint-other" selected/);
assert.match(renderProductAssemblyBindings(view, input), /所选连接含非独立下料构件/);
await configure();
const beforeWrongConnection = calls.length;
await action("preview");
assert.equal(calls.length, beforeWrongConnection, "roles outside a selected committed connection cannot be previewed");
assert.match(renderProductAssemblyBindings(view, input), /真实构件不一致/);
await action("connection", "joint-ab");
await configure();
assert.match(renderProductAssemblyBindings(view, input), /真实主管/);
assert.match(renderProductAssemblyBindings(view, input), /核对构件角色与位置/);
assert.match(renderProductAssemblyBindings(view, input), /默认按节点中的 A、B 顺序分配/);
assert.match(renderProductAssemblyBindings(view, input), /中间画布显示当前产品或已生成下料件/);

// A changed role/station or template parameter invalidates in-flight checks.
const stale = action("preview");
assert.equal(calls.at(-1).payload.connectionKey, "joint-ab");
assert.deepEqual(calls.at(-1).payload.participants[0].anchor, { kind: "side", face: "top", reference: "start", station: 120 });
await action("station", "125", { tubeBindingRole: "host" });
pending.shift().resolve(candidate());
await stale;
assert.equal(productAssemblyCandidateCanApply(view, input), false);
const checked = action("preview");
pending.shift().resolve(candidate());
await checked;
assert.equal(productAssemblyCandidateCanApply(view, input), true);
const html = renderProductAssemblyBindings(view, input);
assert.match(html, /主管下料件/);
assert.match(html, /主管开孔/);
assert.match(html, /主管开孔 · 真实主管 · 上侧面 · 距起端 125 mm · 2 处，间距 50 mm/);
assert.match(html, /20mm/);
assert.match(html, /规格待确定/);
assert.match(html, /装配位置待核对/);
input = { ...input, parameters: { gap: 2 } };
assert.equal(productAssemblyCandidateCanApply(view, input), false);
const beforeApply = calls.length;
await action("apply");
assert.equal(calls.length, beforeApply, "old candidate cannot apply after a parameter edit");

// Even an inconsistent backend response cannot enable unsupported work.
const unsupported = action("preview");
pending.shift().resolve(candidate({ supportStatus: "unsupported" }));
await unsupported;
assert.equal(productAssemblyCandidateCanApply(view, input), false);
await action("member", "member-a", { tubeBindingRole: "branch" });
await action("anchor", "start", { tubeBindingRole: "branch" });
const beforeDuplicate = calls.length;
await action("preview");
assert.equal(calls.length, beforeDuplicate);
assert.match(renderProductAssemblyBindings(view, input), /不同构件/);
await configure();

// Apply and remove reload authoritative saved state; replacement is atomic.
const savedBinding = { bindingId: "binding-1", templateId: template.id, templateName: "孔槽连接",
  connectionKey: "joint-ab", connectionLabel: "真实主管 + 真实支管",
  participants: [{ role: "host", memberEntityId: "member-a", anchor: { kind: "side", face: "top", reference: "start", station: 120, offset: 7, rotation: 15 } },
    { role: "branch", memberEntityId: "member-b", anchor: { kind: "end", end: "start", trim: 3, rotation: 5 } }], parameters: { gap: 3 }, processDrafts: {}, canRemove: true };
const checkedAgain = action("preview"); pending.shift().resolve(candidate()); await checkedAgain;
const rejected = action("apply");
pending.shift().resolve({ applied: false, detail: "加工存在冲突" }); await rejected;
assert.match(renderProductAssemblyBindings(view, input), /应用失败：加工存在冲突/);
assert.equal(productAssemblyBindingsState(view).result.bindings.length, 0);
const retry = action("preview"); pending.shift().resolve(candidate()); await retry;
const noSnapshot = action("apply"); pending.shift().resolve({ applied: true }); await noSnapshot;
assert.match(renderProductAssemblyBindings(view, input), /加工已提交，但结果读取失败/);
assert.doesNotMatch(productAssemblyBindingsState(view).notice, /单件加工已应用/);
assert.equal(productAssemblyCandidateCanApply(view, input), false);
const checkedWithSnapshot = action("preview"); pending.shift().resolve(candidate()); await checkedWithSnapshot;
const apply = action("apply");
pending.shift().resolve({ applied: true, tubeDesigner: { activeProductId: "product-a", generationRun: { entityId: "run-a" },
  product: { assemblyBindingsRevision: "r-after-apply" }, parts: [{ name: "真实加工结果" }] } });
await Promise.resolve();
pending.shift().resolve(result(calls.at(-1).payload, [savedBinding], "s2"));
await apply;
assert.match(renderProductAssemblyBindings(view, input), /单件加工已应用，装配位置待核对/);
assert.equal(productAssemblyBindingsState(view).result.bindings.length, 1);
assert.equal(view.scene.tubeDesigner.parts[0].name, "真实加工结果");
await action("edit", "", { tubeBindingId: "binding-1" });
assert.equal(productAssemblyBindingDraft(view, template).replaceBindingId, "binding-1");
assert.equal(productAssemblyBindingDraft(view, template).connectionKey, "joint-ab");
assert.deepEqual(view.tubeDesignerAssemblyLibrary.parameterDrafts[template.id], { gap: 3 });
const replacing = action("preview");
assert.equal(calls.at(-1).payload.replaceBindingId, "binding-1");
assert.equal(calls.at(-1).payload.connectionKey, "joint-ab");
assert.deepEqual(calls.at(-1).payload.participants, savedBinding.participants, "loading for edit must preserve placement offsets, rotations and end trim");
pending.shift().resolve(candidate()); await replacing;
const remove = action("remove", "", { tubeBindingId: "binding-1" });
pending.shift().resolve({ removed: true, manufacturingUpdated: true, tubeDesigner: { activeProductId: "product-a", generationRun: { entityId: "run-a" },
  product: { assemblyBindingsRevision: "r-after-remove" }, parts: [{ name: "真实移除结果" }] } });
await Promise.resolve();
pending.shift().resolve(result(calls.at(-1).payload, [], "s3")); await remove;
assert.equal(productAssemblyBindingsState(view).result.bindings.length, 0);
assert.match(renderProductAssemblyBindings(view, input), /实际制造件已重新计算/);
assert.equal(view.scene.tubeDesigner.parts[0].name, "真实移除结果");

// Undo/redo revisions refresh bindings even though product/run do not change.
view.scene.tubeDesigner.product.assemblyBindingsRevision = "r2";
const revisionRefresh = ensureProductAssemblyBindings(context, view, ops);
assert.equal(productAssemblyBindingDraft(view, template).participants.host.station, "120");
pending.shift().resolve(result(calls.at(-1).payload, [savedBinding], "s4")); await revisionRefresh;
assert.equal(productAssemblyBindingsState(view).result.bindings.length, 1);

const staleRemove = action("remove", "", { tubeBindingId: "binding-1" });
pending.shift().resolve({ removed: true, manufacturingUpdated: false, detail: "失效工艺已移除，现有下料仍需重新生成", tubeDesigner: {
  activeProductId: "product-a", generationRun: { entityId: "run-a" }, product: { assemblyBindingsRevision: "stale-removed" },
} });
await Promise.resolve();
pending.shift().resolve(result(calls.at(-1).payload, [], "s5")); await staleRemove;
assert.match(productAssemblyBindingsState(view).notice, /失效工艺已移除，现有下料仍需重新生成/);
assert.doesNotMatch(productAssemblyBindingsState(view).notice, /实际制造件已重新计算/);

// Old product responses cannot overwrite the current product or its draft.
const old = ensureProductAssemblyBindings(context, view, ops, true);
view.scene.tubeDesigner.activeProductId = "product-b";
view.scene.tubeDesigner.generationRun.entityId = "run-b";
const next = ensureProductAssemblyBindings(context, view, ops);
pending.shift().resolve(result({ productEntityId: "product-a", generationRunId: "run-a" }, [savedBinding])); await old;
pending.shift().resolve(result({ productEntityId: "product-b", generationRunId: "run-b" })); await next;
assert.equal(productAssemblyBindingsState(view).result.productEntityId, "product-b");
assert.deepEqual(productAssemblyBindingDraft(view, template).participants, {});

// Current protocol requires the host's committed connection identity.
const invalidBindings = ensureProductAssemblyBindings(context, view, ops, true);
const previousResult = productAssemblyBindingsState(view).result;
pending.shift().resolve(result({ productEntityId: "product-b", generationRunId: "run-b" }, [{ ...savedBinding, connectionKey: "" }]));
await invalidBindings;
assert.equal(productAssemblyBindingsState(view).status, "error");
assert.match(productAssemblyBindingsState(view).error, /缺少连接标识/);
assert.equal(productAssemblyBindingsState(view).result, previousResult, "A malformed response cannot replace accepted current bindings");

// Product topology owns the node locations; the selected process only chooses
// what to cut there. A frame member can therefore receive an L and a T process.
const anchoredView = { activeAreaId: "assemblies", scene: { tubeDesigner: {
  activeProductId: "frame-product", generationRun: { entityId: "frame-run" }, product: {},
} }, tubeDesignerAssemblyTemplates: [template], tubeDesignerAssemblyLibrary: {
  selectedId: template.id, parameterDrafts: {}, processDrafts: {},
} };
const anchoredIdentity = productAssemblyBindingIdentity(anchoredView);
const anchoredState = productAssemblyBindingsState(anchoredView);
Object.assign(anchoredState, { key: anchoredIdentity.key, productKey: anchoredIdentity.productKey, status: "ready",
  result: { productEntityId: "frame-product", generationRunId: "frame-run", bindings: [],
    members: [{ memberEntityId: "left-id", itemKey: "frame.left", name: "左立柱", length: 1500 },
      { memberEntityId: "middle-id", itemKey: "frame.middle", name: "中横管", length: 1000 }],
    connections: [{ key: "left-middle", participants: [
      { memberEntityId: "left-id", itemKey: "frame.left", name: "左立柱" },
      { memberEntityId: "middle-id", itemKey: "frame.middle", name: "中横管" },
    ], properties: { topology: "T", participantAnchors: [
      { itemKey: "frame.left", kind: "side", face: "right", localAxialStation: 720,
        anchor: { kind: "side", face: "right", reference: "start", station: 720 } },
      { itemKey: "frame.middle", kind: "end", end: "start", anchor: { kind: "end", end: "start" } },
    ] } }], capabilities: { anchorKinds: ["end", "side"], templates: [
      { templateId: template.id, supported: true },
    ] }, sourceRevision: "frame-1" } });
const anchoredDraft = productAssemblyBindingDraft(anchoredView, template);
anchoredDraft.connectionKey = "left-middle";
renderProductAssemblyBindings(anchoredView, { template, parameters: {}, processDrafts: {} });
assert.deepEqual(anchoredDraft.participants.host, { memberEntityId: "left-id", anchorId: "side:right", station: "720",
  anchorBase: { kind: "side", face: "right", reference: "start", station: 720 } });
assert.deepEqual(anchoredDraft.participants.branch, { memberEntityId: "middle-id", anchorId: "start", station: "",
  anchorBase: { kind: "end", end: "start" } });
assert.equal(anchoredDraft.autoAssignedConnectionKey, "left-middle");

// L is a product end/end relationship. A wrap template may derive a side cut
// from the host's committed approach face without changing the product node.
anchoredState.result.connections[0].properties.participantAnchors = [
  { itemKey: "frame.left", anchor: { kind: "end", end: "end", approachFace: "top" } },
  { itemKey: "frame.middle", anchor: { kind: "end", end: "start" } },
];
anchoredState.result.connections[0].properties.topology = "L";
anchoredDraft.participants = {};
anchoredDraft.autoAssignedConnectionKey = "";
renderProductAssemblyBindings(anchoredView, { template, parameters: {}, processDrafts: {} });
assert.equal(anchoredDraft.participants.host.anchorBase.approachFace, "top");
const endCalls = [];
await handleProductAssemblyBindingAction({ sceneProxy: { invoke(method, payload) {
  endCalls.push({ method, payload });
  return Promise.resolve(candidate());
} } }, anchoredView, "tube-designer-binding-preview", { dataset: {} }, ops,
() => ({ template, parameters: {}, processDrafts: {} }));
assert.equal(endCalls[0].payload.participants[0].anchor.approachFace, "top");
assert.deepEqual(endCalls[0].payload.participants.map((part) => part.anchor.kind), ["end", "end"]);

const revisionView = { activeAreaId: "assemblies", scene: { tubeDesigner: {
  activeProductId: "revision-product", generationRun: { entityId: "revision-run" }, product: {},
  manufacturingGroups: [{ productEntityId: "revision-product", generationRunId: "revision-run",
    parts: [{ entityId: "same-stock", length: 900, manufacturingGeometryResourceVersion: 1 }] }],
} } };
let revisionReads = 0;
const revisionContext = { sceneProxy: { async invoke(method, payload) {
  revisionReads += 1; return result(payload);
} } };
await ensureProductAssemblyBindings(revisionContext, revisionView, ops);
productAssemblyBindingDraft(revisionView, template).participants.host = { memberEntityId: "member-a", anchorId: "end" };
await ensureProductAssemblyBindings(revisionContext, revisionView, ops);
assert.equal(revisionReads, 1);
revisionView.scene.tubeDesigner.product.partsOutdated = true;
await ensureProductAssemblyBindings(revisionContext, revisionView, ops);
assert.equal(revisionReads, 2, "a process-only edit must invalidate the automatic stock summary");
revisionView.scene.tubeDesigner.product.partsOutdated = false;
revisionView.scene.tubeDesigner.manufacturingGroups[0].parts[0].manufacturingGeometryResourceVersion = 2;
await ensureProductAssemblyBindings(revisionContext, revisionView, ops);
assert.equal(revisionReads, 3, "recomputed actual stocks must refresh the summary without a manual binding revision");
assert.equal(productAssemblyBindingDraft(revisionView, template).participants.host.memberEntityId, "member-a",
  "refreshing stock summaries preserves the current product's role draft");
console.log("TubeDesignerProductAssemblyBindingsTest: passed");
