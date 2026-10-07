import assert from "node:assert/strict";
import { buildProfileGroups, renderNestingResultDock } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildNestingRequest, getNestingInputSignature, handleNestingRibbonCommand } from "../../apps/tube-designer/webpage/nestingWorkflow.mjs";
import { handleNestingExportAction, selectedNestingPlanIds } from "../../apps/tube-designer/webpage/nestingExport.mjs";
import { groupNestingPlans } from "../../apps/tube-designer/webpage/nestingGroups.mjs";

function fixture(profile = { id: "rect", kind: "rect", width: 30, depth: 20, wallThickness: 2, specification: "30 × 20 × 2" }) {
  const parts = ["a", "b"].map((entityId) => ({ entityId, name: entityId, profile, length: 1200, quantity: 1 }));
  const group = buildProfileGroups(parts)[0];
  const view = { activeAreaId: "nesting", pending: false, scene: { tubeDesigner: {
    manufacturingGroups: [{ productEntityId: "product", parts }],
    nestingSettings: { version: 2, stocks: [{ profileKey: group.key, rows: [{ id: "stock", length: 6000, quantity: -1 }] }], parameters: { partGap: 3 } },
  } } };
  const calls = [];
  const context = { project: { projectId: "project" }, sceneProxy: { async invoke(method, payload) {
    calls.push({ method, payload });
    if (method === "TubeDesigner.Nest") return { status: "feasible", unplaced: [], plans: parts.map((part, i) => ({
      id: `plan-${i}`, stockTypeId: "stock", profileKey: group.key, stockLength: 6000,
      usedLength: 1200, remainingLength: 4800, partLength: 1200, utilization: 0.2,
      placements: [{
        partId: part.entityId, instanceId: `${part.entityId}-1`, start: 0, end: 1200,
        gapBefore: 0, nestedWithPrevious: false, variantId: "default",
        reversed: false, rotationRadians: 0,
      }],
    })) };
    return { exportedCount: payload.plans.length, exportedFiles: payload.plans.map((_, index) => `stock-${index + 1}.step`), partListFile: "result.xlsx", outputDirectory: "D:/exports/result" };
  } }, appProxy: { bridge: { openDirectoryDialog: async () => "D:/exports" } } };
  const ops = { renderProject() {} };
  return { view, context, calls, ops,
    run: () => handleNestingRibbonCommand(context, view, "nesting.start", ops),
    act: (suffix, target = {}) => handleNestingExportAction(context, view, `tube-designer-nesting-${suffix}`, target, ops),
  };
}

{
  const f = fixture();
  assert.equal(buildNestingRequest(f.view).stocks[0].quantity, -1);
  await f.run();
  assert.ok(!f.view.error, "unlimited stock can open more than one bar");
  assert.equal(selectedNestingPlanIds(f.view).size, 2);
  const first = f.view.tubeDesignerActiveNestingPlanId;
  await f.act("toggle-plan", { checked: false, dataset: { tubeDesignerNestingPlanId: "plan-0" } });
  assert.deepEqual([...selectedNestingPlanIds(f.view)], ["plan-1"]);
  assert.equal(f.view.tubeDesignerActiveNestingPlanId, first, "export selection does not change the 3D active plan");
  await f.act("export-selected");
  assert.equal(f.calls.at(-1).method, "TubeDesigner.ExportNesting");
  assert.deepEqual(f.calls.at(-1).payload.plans, ["plan-1"]);
  await f.act("toggle-all-plans", { checked: true });
  await f.act("export-selected");
  assert.equal(f.calls.at(-1).payload.plans.length, 2, "selecting all rows exports all results through the same action");
  assert.equal(f.view.pending, false);
  assert.equal(f.view.tubeDesignerOperation, null);
  const dock = renderNestingResultDock(f.context, f.view);
  assert.match(dock, /导出排样结果/);
  assert.doesNotMatch(dock, /导出所选|导出所有/);
  assert.match(dock, /type="checkbox"/);
  assert.doesNotMatch(dock, /<span>状态<\/span>|已排样/);
}

{
  const f = fixture(); await f.run();
  await f.act("toggle-all-plans", { checked: false });
  assert.equal(selectedNestingPlanIds(f.view).size, 0);
  const before = f.calls.length;
  await f.act("export-selected");
  assert.equal(f.calls.length, before);
  assert.match(f.view.error, /请勾选/);
  await f.act("toggle-all-plans", { checked: true });
  assert.equal(selectedNestingPlanIds(f.view).size, 2);
  f.context.appProxy.bridge.openDirectoryDialog = async () => "";
  const cancelled = await f.act("export-selected");
  assert.equal(cancelled.result.cancelled, true);
  assert.equal(f.calls.length, before);
  assert.equal(f.view.pending, false);
}

{
  const f = fixture(); await f.run();
  f.view.scene.tubeDesigner.nestingSettings.parameters.partGap = 5;
  const before = f.calls.length;
  assert.notEqual(f.view.tubeDesignerNestingResult.inputSignature, getNestingInputSignature(f.view));
  await f.act("export-selected");
  assert.equal(f.calls.length, before);
  assert.match(f.view.error, /重新排样/);
}

{
  const f = fixture(); await f.run();
  f.context.sceneProxy.invoke = async () => { throw new Error("写入失败"); };
  await f.act("export-selected");
  assert.match(f.view.error, /写入失败/);
  assert.equal(f.view.pending, false);
  assert.equal(f.view.tubeDesignerOperation, null);
  assert.equal(f.view.tubeDesignerNestingResult.plans.length, 2);
}

// A section identity is a complete stable JSON snapshot, not a short resource
// ID. Normal parameterized sections and imported contours both exceed 160
// bytes and must survive the nesting-to-export workflow unchanged.
for (const profile of [
  { id: "rect", kind: "rect", width: 30, depth: 20, wallThickness: 2,
    cornerRadius: 3, packageVersion: 1, contentDigest: "a".repeat(64),
    parameters: { width: 30, depth: 20, wallThickness: 2, cornerRadius: 3 },
    specification: "30 × 20 × 2" },
  { id: "imported-section", kind: "custom", specification: "导入轮廓",
    contours: [{ points: Array.from({ length: 200 }, (_, index) =>
      ({ x: index, y: index % 17 })) }] },
]) {
  const f = fixture(profile);
  const request = buildNestingRequest(f.view);
  const key = request.parts[0].profileKey;
  const keyBytes = Buffer.byteLength(key, "utf8");
  assert.ok(keyBytes > 160 && keyBytes <= 65536, "real section snapshots exceed the short-ID limit");
  assert.deepEqual(JSON.parse(key).parameters ?? JSON.parse(key).contours,
    profile.parameters ?? profile.contours);
  assert.ok(request.parts.every((part) => part.profileKey === key));
  assert.ok(request.stocks.every((stock) => stock.profileKey === key));
  await f.run();
  assert.equal(f.view.error, "");
  assert.ok(f.view.tubeDesignerNestingResult.plans.every((plan) => plan.profileKey === key));
  assert.equal(groupNestingPlans(f.view.tubeDesignerNestingResult.plans)[0].key, key);
  await f.act("toggle-all-plans", { checked: false });
  await f.act("toggle-group", { checked: true, dataset: { tubeDesignerNestingGroupKey: key } });
  assert.deepEqual([...selectedNestingPlanIds(f.view)], ["plan-0", "plan-1"]);
  await f.act("export-group", { dataset: { tubeDesignerNestingGroupKey: key } });
  assert.deepEqual(f.calls.at(-1).payload.plans, ["plan-0", "plan-1"]);
  assert.equal(Object.hasOwn(f.calls.at(-1).payload, "profileKey"), false,
    "native export resolves the full section identity from the saved plan");
  assert.equal(f.view.error, "");
  await f.act("toggle-plan", { checked: false, dataset: { tubeDesignerNestingPlanId: "plan-0" } });
  await f.act("export-selected");
  assert.deepEqual(f.calls.at(-1).payload.plans, ["plan-1"]);
  assert.ok(f.view.tubeDesignerNestingResult.plans.every((plan) => plan.profileKey === key),
    "export does not shorten, hash or replace the section identity");
}

{
  const f = fixture(); await f.run();
  const plans = f.view.tubeDesignerNestingResult.plans;
  plans[0].profileKey = 'section-a'; plans[0].profile = '同名规格 <A>';
  plans[1].profileKey = 'section-b'; plans[1].profile = '同名规格 <A>';
  plans.push({ ...plans[0], id: 'plan-2' });
  assert.deepEqual(groupNestingPlans(plans).map(g => g.entries.map(e => e.index)), [[0, 2], [1]],
    'group by identity, keep original order, do not merge identical display names');
  await f.act('toggle-all-plans', { checked: false });
  await f.act('toggle-group', { checked: true, dataset: { tubeDesignerNestingGroupKey: 'section-a' } });
  assert.deepEqual([...selectedNestingPlanIds(f.view)], ['plan-0', 'plan-2']);
  await f.act('export-selected');
  assert.deepEqual(f.calls.at(-1).payload.plans, ['plan-0', 'plan-2']);
  await f.act('export-group', { dataset: { tubeDesignerNestingGroupKey: 'section-b' } });
  assert.deepEqual(f.calls.at(-1).payload.plans, ['plan-1']);
  assert.deepEqual([...selectedNestingPlanIds(f.view)], ['plan-0', 'plan-2'], 'group export preserves checked selection');
  f.view.tubeDesignerClosedNestingResultGroups = ['section-a'];
  const dock = renderNestingResultDock(f.context, f.view);
  assert.equal((dock.match(/class="tube-designer-nesting-result-group"/g) ?? []).length, 2);
  assert.match(dock, /同名规格 &lt;A&gt;/);
  assert.match(dock, /导出本组/);
  assert.match(dock, /data-tube-designer-nesting-group-key="section-a" >/);
  await f.act('toggle-group', { checked: false, dataset: { tubeDesignerNestingGroupKey: 'section-a' } });
  assert.equal(selectedNestingPlanIds(f.view).size, 0);
  const before = f.calls.length;
  await f.act('export-group', { dataset: { tubeDesignerNestingGroupKey: 'missing' } });
  assert.equal(f.calls.length, before);
  assert.match(f.view.error, /规格组已不存在/);
}

console.log("Nesting export selection and workflow tests passed.");
