import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { deserializeVariantText } from "../../iCAX-UI/SDK/SDO/variantSerializer.mjs";
import {
  buildProfileGroups,
  renderNestingLeftPane,
  renderNestingResultDock,
  renderNestingRightPane,
} from "../../apps/tube-designer/webpage/partsArea.mjs";
import {
  buildNestingRequest,
  getNestingInputSignature,
  handleNestingRibbonCommand,
  isNestingResultStale,
  restoreSavedNestingTask,
} from "../../apps/tube-designer/webpage/nestingWorkflow.mjs";

const rect = { id: "rect", kind: "rect", displayName: "矩形管", specification: "40 × 20 × R2 × 1.5", width: 40, depth: 20, wallThickness: 1.5, cornerRadius: 2 };
const round = { id: "round", kind: "round", displayName: "圆管", specification: "⌀19 × 1", width: 19, depth: 19, wallThickness: 1 };
const clone = (value) => structuredClone(value);
const partsIn = (view) => view.scene.tubeDesigner.manufacturingGroups.flatMap((group) => group.parts);

function defaultParts() {
  return [
    { entityId: "rect-a", name: "横杆 A", length: 1200, quantity: 1, profile: clone(rect), properties: { "manufacturing.material": "Q235B" } },
    { entityId: "rect-b", name: "横杆 B", length: 1200, quantity: 1, profile: clone(rect), properties: { "manufacturing.material": "304" } },
    { entityId: "round-a", name: "竖杆 A", length: 800, quantity: 1, profile: clone(round) },
  ];
}

function completeResult(request, view) {
  const source = new Map(partsIn(view).map((part) => [part.entityId, part]));
  const plans = request.stocks.flatMap((stock) => {
    const demand = request.parts.filter((part) => part.profileKey === stock.profileKey);
    if (!demand.length) return [];
    let end = 0;
    const placements = demand.flatMap((part) => Array.from({ length: part.quantity }, (_, index) => {
      const start = end ? end + request.parameters.partGap : 0;
      end = start + source.get(part.partEntityId).length;
      return {
        partId: part.partEntityId, instanceId: `${part.partEntityId}-${index}`, start, end,
        gapBefore: start ? request.parameters.partGap : 0,
        nestedWithPrevious: false, variantId: "default", reversed: false, rotationRadians: 0,
      };
    }));
    return [{ id: `plan-${stock.id}`, stockTypeId: stock.id, profileKey: stock.profileKey, stockLength: stock.length, usedLength: end, remainingLength: stock.length - end, placements }];
  });
  return { status: "feasible", plans, unplaced: [], metrics: {}, diagnostics: [] };
}

function harness(options = {}) {
  const parts = clone(options.parts ?? defaultParts());
  const view = {
    scene: { tubeDesigner: { manufacturingGroups: [{ productEntityId: "product-a", name: "测试产品", parts }] } },
    pending: false,
    tubeDesignerNestingSettings: {
      version: 2,
      stocks: buildProfileGroups(parts).map((group, index) => ({ profileKey: group.key, rows: [{ id: `stock-${index}`, length: 6000, quantity: 3 }] })),
      parameters: { partGap: options.partGap ?? 2 },
    },
  };
  view.scene.tubeDesigner.nestingSettings = clone(view.tubeDesignerNestingSettings);
  getNestingInputSignature(view);
  const calls = [];
  const logs = [];
  let renderCount = 0;
  const context = { sceneProxy: { async invoke(method, request, settings) {
    calls.push({ method, request: clone(request), settings });
    return options.invoke ? options.invoke(method, request, view) : completeResult(request, view);
  } } };
  const ops = { renderProject() { renderCount++; }, appendProjectLog(...args) { logs.push(args); } };
  return { view, context, calls, logs, ops, get renderCount() { return renderCount; }, run(command = "nesting.start") { return handleNestingRibbonCommand(context, view, command, ops); } };
}

const completed = [];
async function test(name, body) {
  await body();
  completed.push(name);
}

await test("Default selection includes all parts and only matching profile stocks", () => {
  const h = harness();
  const request = buildNestingRequest(h.view);
  assert.deepEqual(new Set(request.parts.map((part) => part.partEntityId)), new Set(["rect-a", "rect-b", "round-a"]));
  assert.equal(request.stocks.length, 2);
  assert.deepEqual(request.parameters, { partGap: 2 });
  h.view.tubeDesignerSelectedPartIds = ["rect-b", "missing-id"];
  const selected = buildNestingRequest(h.view);
  assert.deepEqual(selected.parts.map((part) => part.partEntityId), ["rect-b"]);
  assert.equal(selected.stocks.length, 1);
  assert.equal(selected.stocks[0].profileKey, selected.parts[0].profileKey);
  assert.ok(!Object.hasOwn(selected.parts[0], "material"));
});

await test("Instance production quantities are already expanded and are not multiplied twice", () => {
  const h = harness({ parts: [{ ...defaultParts()[0], unitQuantity: 2, instanceQuantity: 3, quantity: 6 }] });
  const request = buildNestingRequest(h.view);
  assert.equal(request.parts[0].quantity, 6);
  assert.equal(request.parts[0].instanceQuantity, 3);
  assert.equal(buildNestingRequest(h.view).parts[0].quantity, 6);
  const signature = getNestingInputSignature(h.view);
  partsIn(h.view)[0].instanceQuantity = 2;
  partsIn(h.view)[0].quantity = 4;
  assert.notEqual(getNestingInputSignature(h.view), signature);
});

await test("Empty scene and explicitly empty selection are errors", () => {
  assert.throws(() => buildNestingRequest(harness({ parts: [] }).view), /零件/);
  const h = harness();
  h.view.tubeDesignerSelectedPartIds = [];
  assert.throws(() => buildNestingRequest(h.view), /零件/);
});

await test("Zero stocks and missing section inventory cannot start", () => {
  const h = harness();
  for (const group of h.view.tubeDesignerNestingSettings.stocks) group.rows[0].quantity = 0;
  assert.throws(() => buildNestingRequest(h.view), /母材/);
  h.view.tubeDesignerNestingSettings.stocks[0].rows[0].quantity = 1;
  assert.throws(() => buildNestingRequest(h.view), /母材/);
});

await test("Part quantities and gap are forwarded without multiplying inventory", () => {
  const parts = defaultParts();
  parts[0].quantity = 2;
  const h = harness({ parts, partGap: 3.5 });
  const request = buildNestingRequest(h.view);
  assert.equal(request.parts.find((part) => part.partEntityId === "rect-a").quantity, 2);
  assert.equal(request.parameters.partGap, 3.5);
  assert.ok(request.stocks.every((stock) => stock.quantity === 3));
  for (const quantity of [0, -1, 1.5, NaN, Infinity]) {
    partsIn(h.view)[0].quantity = quantity;
    assert.throws(() => buildNestingRequest(h.view), /数量/);
  }
  partsIn(h.view)[0].quantity = 10_000;
  assert.equal(buildNestingRequest(h.view).parts.find((part) => part.partEntityId === "rect-a").quantity, 10_000);
  partsIn(h.view)[0].quantity = 1_000_001;
  assert.throws(() => buildNestingRequest(h.view), /1000000/);
});

await test("Invalid IDs and lengths are rejected before calling native", async () => {
  for (const change of [
    (parts) => { parts[1].entityId = parts[0].entityId; },
    (parts) => { parts[0].entityId = ""; },
    (parts) => { parts[0].length = 0; },
    (parts) => { parts[0].length = Infinity; },
  ]) {
    const h = harness();
    change(partsIn(h.view));
    await h.run();
    assert.equal(h.calls.length, 0);
    assert.match(h.view.error, /排样失败/);
    assert.equal(h.view.pending, false);
  }
});

await test("Success stores native plans and selects first result in 3D", async () => {
  const h = harness();
  assert.equal(await h.run(), true);
  assert.equal(h.calls.length, 1);
  assert.equal(h.calls[0].method, "TubeDesigner.Nest");
  const result = h.view.tubeDesignerNestingResult;
  assert.equal(result.plans.length, 2);
  assert.equal(result.plans.reduce((count, plan) => count + plan.placements.length, 0), 3);
  assert.equal(h.view.tubeDesignerActiveNestingPlanId, result.plans[0].id);
  assert.equal(h.view.tubeDesignerNestingSelectionKind, "plan");
  assert.equal(result.inputSignature, getNestingInputSignature(h.view));
  assert.equal(isNestingResultStale(h.view), false);
  assert.equal(h.view.pending, false);
  assert.equal(h.view.tubeDesignerNestingOperation, null);
  assert.ok(h.renderCount >= 2);
  assert.ok(result.plans.every((plan) => plan.partCount === plan.placements.length));
  assert.ok(result.plans.every((plan) => /^.+-\d+$/.test(plan.name)), "result names use profile specification plus an ordinal");
  assert.doesNotMatch(result.plans.map((plan) => plan.name).join("\n"), /^母材 /m);
  h.view.tubeDesignerLockedNestingPlanIds = [result.plans[0].id];
  const rerunRequest = buildNestingRequest(h.view);
  assert.equal(rerunRequest.lockedPlans.length, 1);
  assert.equal(rerunRequest.lockedPlans[0].id, result.plans[0].id);
  assert.deepEqual(rerunRequest.lockedPlans[0].placements.map((placement) => placement.instanceId),
    result.plans[0].placements.map((placement) => placement.instanceId));
});

for (const partGap of [0, 1, 5]) {
  await test(`Ordinary placements accept a non-nested first part with gap ${partGap}`, async () => {
    const h = harness({ partGap });
    await h.run();
    assert.equal(h.view.error, "");
    assert.equal(h.calls[0].request.parameters.partGap, partGap);
    const result = h.view.tubeDesignerNestingResult;
    assert.equal(result.status, "feasible");
    assert.equal(result.plans.length, 2);
    for (const plan of result.plans) {
      assert.equal(plan.placements[0].start, 0);
      assert.equal(plan.placements[0].gapBefore, 0);
      assert.equal(plan.placements[0].nestedWithPrevious, false);
      for (const [index, placement] of plan.placements.entries()) {
        assert.equal(placement.nestedWithPrevious, false);
        if (index) {
          assert.equal(placement.gapBefore, partGap);
          assert.equal(placement.start - plan.placements[index - 1].end, partGap);
        }
      }
    }
    assert.equal(isNestingResultStale(h.view), false);
  });

  await test(`First part incorrectly marked nested is rejected with gap ${partGap}`, async () => {
    const h = harness({ partGap, invoke: async (_method, request, view) => {
      const result = completeResult(request, view);
      result.plans[0].placements[0].nestedWithPrevious = true;
      return result;
    } });
    await h.run();
    assert.equal(h.calls.length, 1);
    assert.equal(h.view.tubeDesignerNestingResult, undefined);
    assert.match(h.view.error, /非法套切/);
    assert.equal(h.view.tubeDesignerNestingLastRunFailed, true);
    assert.equal(h.view.pending, false);
    assert.equal(h.view.tubeDesignerNestingOperation, null);
  });

  await test(`Later parts can legally nest with gap ${partGap}`, async () => {
    const nestedGap = partGap - 10;
    const h = harness({ partGap, invoke: async (_method, request, view) => {
      const result = completeResult(request, view);
      const plan = result.plans.find((item) => item.placements.length > 1);
      const placement = plan.placements[1];
      const length = placement.end - placement.start;
      placement.start = plan.placements[0].end + nestedGap;
      placement.end = placement.start + length;
      placement.gapBefore = nestedGap;
      placement.nestedWithPrevious = true;
      placement.variantId = "reversed";
      placement.reversed = true;
      plan.usedLength = placement.end;
      plan.remainingLength = plan.stockLength - plan.usedLength;
      return result;
    } });
    await h.run();
    assert.equal(h.view.error, "");
    const plan = h.view.tubeDesignerNestingResult.plans.find((item) => item.placements.length > 1);
    assert.equal(plan.placements[0].nestedWithPrevious, false);
    assert.equal(plan.placements[1].nestedWithPrevious, true);
    assert.equal(plan.placements[1].gapBefore, nestedGap);
    assert.equal(plan.placements[1].start - plan.placements[0].end, nestedGap);
    assert.equal(plan.placements[1].length, 1200);
    assert.equal(plan.placements[1].variantId, "reversed");
    assert.equal(plan.placements[1].reversed, true);
  });
}

await test("Unknown commands and concurrent starts do not duplicate native work", async () => {
  let release;
  const h = harness({ invoke: (_method, request, view) => new Promise((resolve) => { release = () => resolve(completeResult(request, view)); }) });
  assert.equal(await h.run("unrelated.command"), false);
  const first = h.run();
  await Promise.resolve();
  assert.equal(h.view.pending, true);
  assert.equal(await h.run(), true);
  assert.equal(h.calls.length, 1);
  release();
  await first;
  assert.equal(h.view.pending, false);
  assert.ok(h.view.tubeDesignerNestingResult);
});

await test("Signatures ignore order and material, but detect geometry, settings and selection", async () => {
  const h = harness();
  await h.run();
  const signature = getNestingInputSignature(h.view);
  const source = partsIn(h.view);
  source[0].properties["manufacturing.material"] = "Aluminium";
  h.view.scene.tubeDesigner.manufacturingGroups[0].parts.reverse();
  h.view.tubeDesignerNestingSettings.stocks.reverse();
  assert.equal(getNestingInputSignature(h.view), signature);
  assert.equal(isNestingResultStale(h.view), false);
  const snapshot = clone(h.view);
  for (const mutate of [
    (view) => { view.tubeDesignerNestingSettings.parameters.partGap++; },
    (view) => { view.tubeDesignerNestingSettings.stocks[0].rows[0].quantity++; },
    (view) => { partsIn(view)[0].length++; },
    (view) => { partsIn(view)[0].manufacturingGeometryResourceVersion = 2; },
    (view) => { view.tubeDesignerSelectedPartIds = ["rect-a"]; },
  ]) {
    const changed = clone(snapshot);
    mutate(changed);
    assert.equal(isNestingResultStale(changed), true);
  }
});

await test("Rerun failure preserves old result but explicitly marks it stale", async () => {
  const h = harness();
  await h.run();
  const old = h.view.tubeDesignerNestingResult;
  h.context.sceneProxy.invoke = async () => { throw new Error("native unavailable"); };
  await h.run();
  assert.equal(h.view.tubeDesignerNestingResult, old);
  assert.equal(isNestingResultStale(h.view), true);
  assert.match(h.view.error, /native unavailable/);
  assert.equal(h.view.pending, false);
  h.context.sceneProxy.invoke = async (_method, request) => completeResult(request, h.view);
  await h.run();
  assert.notEqual(h.view.tubeDesignerNestingResult, old);
  assert.equal(isNestingResultStale(h.view), false);
});

await test("Malformed and unaccounted empty responses are not accepted", async () => {
  for (const response of [null, undefined, {}, { plans: [] }, { plans: [], unplaced: [] }]) {
    const h = harness({ invoke: async () => response });
    await h.run();
    assert.equal(h.view.tubeDesignerNestingResult, undefined);
    assert.match(h.view.error, /排样失败/);
    assert.equal(h.view.pending, false);
  }
});

await test("Genuine infeasible result accounts for all unplaced demand without fake plans", async () => {
  const h = harness({ invoke: async (_method, request) => ({ status: "infeasible", plans: [], unplaced: request.parts.map((part) => ({ partId: part.partEntityId, quantity: part.quantity, reason: "没有足够长度的母材" })), diagnostics: [] }) });
  await h.run();
  assert.equal(h.view.tubeDesignerNestingResult.status, "infeasible");
  assert.equal(h.view.tubeDesignerNestingResult.plans.length, 0);
  assert.equal(h.view.tubeDesignerNestingResult.unplaced.length, 3);
  assert.equal(h.view.tubeDesignerNestingSelectionKind, "part");
  assert.equal(h.view.tubeDesignerActiveNestingPlanId, "");
  assert.equal(isNestingResultStale(h.view), false);
});

await test("Partial result keeps successful plans and explains unplaced parts", async () => {
  const h = harness({ invoke: async (_method, request, view) => {
    const result = completeResult(request, view);
    const rejected = result.plans.pop();
    result.unplaced = rejected.placements.map((placement) => ({ partId: placement.partId, quantity: 1, reason: "库存不足" }));
    return result;
  } });
  await h.run();
  assert.equal(h.view.tubeDesignerNestingResult.status, "partial");
  assert.equal(h.view.tubeDesignerNestingResult.plans.length, 1);
  assert.ok(h.view.tubeDesignerNestingResult.unplaced.every((row) => row.partName && row.reason));
});

await test("Invalid placements, profile, inventory, remnant and quantity results fail safely", async () => {
  for (const corrupt of [
    (result) => { result.plans[0].profileKey = "wrong-profile"; },
    (result) => { result.plans[0].stockTypeId = "unknown-stock"; },
    (result) => { result.plans[0].stockLength = 6100; },
    (result) => { result.plans[0].placements[0].start = -10; },
    (result) => { result.plans[0].placements[0].end = Infinity; },
    (result) => { result.plans[0].placements[0].partId = "unknown-part"; },
    (result) => { result.plans[0].remainingLength++; },
    (result) => { result.plans[0].usedLength++; },
    (result) => { result.plans[0].placements.pop(); },
    (result) => { const plan = result.plans.find((item) => item.placements.length > 1); plan.placements[1].start = plan.placements[0].end; },
    (result) => { const plan = result.plans.find((item) => item.placements.length > 1); plan.placements[1].instanceId = plan.placements[0].instanceId; },
    (result) => { result.unplaced.push({ partId: "rect-a", quantity: .5 }); },
  ]) {
    const h = harness({ invoke: async (_method, request, view) => { const result = completeResult(request, view); corrupt(result); return result; } });
    await h.run();
    assert.equal(h.view.tubeDesignerNestingResult, undefined);
    assert.match(h.view.error, /排样失败/);
  }
  const h = harness({ invoke: async (_method, request, view) => {
    const result = completeResult(request, view);
    const plan = result.plans.find((item) => item.placements.length > 1);
    const duplicate = clone(plan);
    duplicate.id += "-extra";
    result.plans.push(duplicate);
    return result;
  } });
  h.view.tubeDesignerNestingSettings.stocks.forEach((group) => { group.rows[0].quantity = 1; });
  await h.run();
  assert.equal(h.view.tubeDesignerNestingResult, undefined);
  assert.match(h.view.error, /可用数量/);
});

await test("Input changes during calculation prevent stale response from replacing old result", async () => {
  const h = harness();
  await h.run();
  const old = h.view.tubeDesignerNestingResult;
  h.context.sceneProxy.invoke = async (_method, request) => {
    const result = completeResult(request, h.view);
    h.view.tubeDesignerNestingSettings.parameters.partGap++;
    return result;
  };
  await h.run();
  assert.equal(h.view.tubeDesignerNestingResult, old);
  assert.equal(isNestingResultStale(h.view), true);
  assert.match(h.view.error, /变化/);
  assert.equal(h.view.pending, false);
});

await test("Unavailable native service is a clear error without changing existing data", async () => {
  const h = harness();
  h.context.sceneProxy = null;
  const scene = clone(h.view.scene);
  await h.run();
  assert.deepEqual(h.view.scene, scene);
  assert.equal(h.view.tubeDesignerNestingResult, undefined);
  assert.match(h.view.error, /排样服务/);
  assert.equal(h.view.pending, false);
});

await test("Native response cannot shorten a source part to fabricate a fit", async () => {
  const h = harness({ invoke: async (_method, request, view) => {
    const result = completeResult(request, view);
    const plan = result.plans.find((item) => item.placements.length === 1);
    plan.placements[0].end = 1;
    plan.usedLength = 1;
    plan.remainingLength = plan.stockLength - 1;
    return result;
  } });
  await h.run();
  assert.equal(h.view.tubeDesignerNestingResult, undefined);
  assert.match(h.view.error, /排样失败/);
});

await test("Result dock and selected stock inspector clearly identify stale plans", async () => {
  const h = harness();
  await h.run();
  assert.doesNotMatch(renderNestingResultDock(h.context, h.view), /以下是旧结果|需重新排样/);
  assert.doesNotMatch(renderNestingRightPane(h.context, h.view), /需重新排样/);
  h.view.tubeDesignerNestingSettings.parameters.partGap = 7;
  const dock = renderNestingResultDock(h.context, h.view);
  const inspector = renderNestingRightPane(h.context, h.view);
  assert.match(dock, /以下是旧结果/);
  assert.match(dock, /这些结果不再对应当前输入/);
  assert.match(dock, /重新计算/);
  assert.match(inspector, /需重新排样/);
  assert.match(dock, /开始排样/);
});

await test("Partial and infeasible result dock displays all unplaced reasons safely", async () => {
  const h = harness({ invoke: async (_method, request, view) => {
    const result = completeResult(request, view);
    const removed = result.plans.pop();
    result.unplaced = removed.placements.map((placement) => ({ partId: placement.partId, quantity: 1, reason: "库存不足 <检查长度> & 数量" }));
    return result;
  } });
  await h.run();
  const dock = renderNestingResultDock(h.context, h.view);
  assert.match(dock, /未排入 1 件/);
  assert.match(dock, /库存不足 &lt;检查长度&gt; &amp; 数量/);
  assert.doesNotMatch(dock, /库存不足 <检查长度>/);
  assert.match(dock, /竖杆 A/);
  assert.match(dock, /data-cam-action="tube-designer-select-nesting-plan"/);
  h.view.tubeDesignerNestingResult.plans = [];
  const infeasible = renderNestingResultDock(h.context, h.view);
  assert.match(infeasible, /本次没有可用排样方案/);
  assert.match(infeasible, /请根据未排原因调整母材/);
  assert.match(infeasible, /库存不足/);
});

await test("Section-only grouping and plan rows never introduce material groups", async () => {
  const h = harness();
  await h.run();
  const left = renderNestingLeftPane(h.context, h.view);
  assert.equal((left.match(/class="tube-designer-cutting-group"/g) ?? []).length, 2);
  assert.match(left, /矩形管/);
  assert.match(left, /圆管/);
  assert.doesNotMatch(left, /材料截面组|材料未指定|Q235B|304/);
  const dock = renderNestingResultDock(h.context, h.view);
  const inspector = renderNestingRightPane(h.context, h.view);
  assert.match(dock, /截面规格/);
  assert.doesNotMatch(dock, /材料|Q235B|304/);
  assert.doesNotMatch(inspector, /材料|Q235B|304/);
});

await test("Selected stock inspector does not truncate cutting order after 20 parts", async () => {
  const parts = defaultParts().slice(0, 1);
  parts[0].length = 100;
  parts[0].quantity = 25;
  const h = harness({ parts });
  await h.run();
  const inspector = renderNestingRightPane(h.context, h.view);
  assert.equal((inspector.match(/data-cam-action="tube-designer-select-nesting-placement"/g) ?? []).length, 25);
  assert.match(inspector, /<b\b[^>]*>25<\/b>/);
  assert.match(inspector, /切割顺序 <span>25 件<\/span>/);
});

await test("Active nesting result uses selection yellow and exposes a stable row locator", async () => {
  const h = harness();
  await h.run();
  const dock = renderNestingResultDock(h.context, h.view);
  assert.match(dock, /class="row active /);
  assert.match(dock, /data-tube-designer-nesting-plan-row/);
  assert.match(dock, new RegExp(`data-tube-designer-nesting-plan-id="${h.view.tubeDesignerActiveNestingPlanId}"`));
});

await test("Active nesting result restores or centers in one scroll assignment", async () => {
  const h = harness();
  await h.run();
  const [firstPlan, secondPlan] = h.view.tubeDesignerNestingResult.plans;
  h.view.tubeDesignerActiveNestingPlanId = secondPlan.id;

  const row = (id, top, active = false) => ({
    dataset: { tubeDesignerNestingPlanId: id },
    active,
    getBoundingClientRect: () => ({ top, height: 32 }),
  });
  const scroller = (rows, scrollTop, writeCounter = null) => {
    let currentScrollTop = scrollTop;
    const result = {
      clientHeight: 200,
      scrollHeight: 1000,
      getBoundingClientRect: () => ({ top: 0, height: 200 }),
      querySelector: (selector) => selector.endsWith(".active") ? rows.find((item) => item.active) ?? null : null,
      querySelectorAll: () => rows,
    };
    Object.defineProperty(result, "scrollTop", {
      get: () => currentScrollTop,
      set: (value) => {
        currentScrollTop = value;
        if (writeCounter) writeCounter.count += 1;
      },
    });
    return result;
  };
  const renderWithTables = async (oldTable, replacementTable) => {
    let tableReadCount = 0;
    h.context.mount = { querySelector(selector) {
      if (selector !== ".tube-designer-nesting-result-table") return null;
      return tableReadCount++ === 0 ? oldTable : replacementTable;
    } };
    renderNestingResultDock(h.context, h.view);
    await Promise.resolve();
  };

  const comfortableWrites = { count: 0 };
  const comfortableOld = scroller([
    row(firstPlan.id, 25, true),
    row(secondPlan.id, 80),
  ], 240);
  const comfortableNew = scroller([
    row(firstPlan.id, 25),
    row(secondPlan.id, 80, true),
  ], 0, comfortableWrites);
  await renderWithTables(comfortableOld, comfortableNew);
  assert.equal(comfortableNew.scrollTop, 240, "a target already in the middle band preserves the old position");
  assert.equal(comfortableWrites.count, 1, "replacement DOM receives one final scroll position");

  const offscreenWrites = { count: 0 };
  const offscreenOld = scroller([
    row(firstPlan.id, 25, true),
    row(secondPlan.id, 175),
  ], 240);
  const offscreenNew = scroller([
    row(firstPlan.id, 25),
    row(secondPlan.id, 500, true),
  ], 0, offscreenWrites);
  await renderWithTables(offscreenOld, offscreenNew);
  assert.equal(offscreenNew.scrollTop, 416, "an out-of-band target is centered directly");
  assert.equal(offscreenWrites.count, 1, "centering never animates through an intermediate position");
});

for (const scenario of ["unchanged", "generation", "stocks", "invalid-stocks"]) {
  await test(`Saved nesting task restores safely: ${scenario}`, () => {
    const h = harness();
    h.view.scene.tubeDesigner.manufacturingGroups[0].generationRunId = "run-1";
    const request = buildNestingRequest(h.view);
    h.view.scene.tubeDesigner.nestingTask = {
      revision: "saved-1",
      parts: request.parts.map(part => ({ partEntityId: part.partEntityId, generationRunId: "run-1" })),
      request: clone(request),
      result: completeResult(request, h.view),
    };
    if (scenario === "generation") h.view.scene.tubeDesigner.manufacturingGroups[0].generationRunId = "run-2";
    if (scenario === "stocks") h.view.tubeDesignerNestingSettings.stocks[0].rows[0].length = 6500;
    if (scenario === "invalid-stocks") for (const stock of h.view.tubeDesignerNestingSettings.stocks) stock.rows[0].quantity = 0;
    restoreSavedNestingTask(h.view, h.context);
    if (scenario === "generation") {
      assert.equal(h.view.tubeDesignerNestingResult, null);
      assert.deepEqual(h.view.tubeDesignerSelectedPartIds, []);
    } else {
      assert.ok(h.view.tubeDesignerNestingResult);
      assert.equal(isNestingResultStale(h.view), scenario !== "unchanged");
    }
    assert.equal(h.calls.length, 0, "restoring a saved task must not invoke the solver");
    delete h.view.scene.tubeDesigner.nestingTask;
    restoreSavedNestingTask(h.view, h.context);
    assert.equal(h.view.tubeDesignerNestingResult, null, "undoing the saved task clears its result");
    assert.deepEqual(h.view.tubeDesignerLockedNestingPlanIds, []);
  });
}

// Optional integration input is emitted by the native solver probe with VariantSerializer.
// It contains [{ gap, partsLength, quantity, stockLength, stockQuantity, result }].
// Only opaque profile/part/stock IDs are adapted to this UI fixture; solver geometry is preserved.
let nativeResultCount = 0;
if (process.env.ICAX_NESTING_NATIVE_RESULTS) {
  const nativeCases = deserializeVariantText(readFileSync(process.env.ICAX_NESTING_NATIVE_RESULTS, "utf8"));
  assert.ok(Array.isArray(nativeCases) && nativeCases.length > 0, "native results must be a nonempty Variant array");
  for (const [index, nativeCase] of nativeCases.entries()) {
    await test(`Native solver result ${index + 1} accepts gap ${nativeCase.gap}`, async () => {
      const { gap, partsLength, quantity, stockLength, stockQuantity, result } = nativeCase;
      assert.ok(Number.isFinite(gap) && gap >= 0);
      assert.ok(Number.isFinite(partsLength) && partsLength > 0);
      assert.ok(Number.isSafeInteger(quantity) && quantity > 0);
      assert.ok(Number.isFinite(stockLength) && stockLength > 0);
      assert.ok(Number.isSafeInteger(stockQuantity) && (stockQuantity === -1 || stockQuantity > 0));
      assert.ok(result.plans.length > 0);
      assert.deepEqual(result.unplaced, []);
      assert.equal(new Set(result.plans.map((plan) => plan.stockTypeId)).size, 1, "native fixture uses one stock type");
      assert.equal(new Set(result.plans.map((plan) => plan.profileKey)).size, 1, "native fixture uses one profile");
      assert.equal(new Set(result.plans.flatMap((plan) => plan.placements.map((placement) => placement.partId))).size, 1,
        "native fixture uses one source part with repeated instances");
      const parts = defaultParts().slice(0, 1);
      parts[0].length = partsLength;
      parts[0].quantity = quantity;
      let mappedResult;
      const h = harness({ parts, partGap: gap, invoke: async (_method, request) => {
        mappedResult = clone(result);
        for (const plan of mappedResult.plans) {
          plan.stockTypeId = request.stocks[0].id;
          plan.profileKey = request.stocks[0].profileKey;
          for (const placement of plan.placements) placement.partId = request.parts[0].partEntityId;
        }
        return mappedResult;
      } });
      h.view.tubeDesignerNestingSettings.stocks[0].rows[0].length = stockLength;
      h.view.tubeDesignerNestingSettings.stocks[0].rows[0].quantity = stockQuantity;
      await h.run();
      assert.equal(h.calls.length, 1);
      assert.equal(h.view.error, "", `native case ${index + 1} was rejected: ${h.view.error}`);
      const accepted = h.view.tubeDesignerNestingResult;
      assert.ok(["optimal", "feasible"].includes(result.status), "native fixture must report a successful solve");
      assert.equal(accepted.status, result.status, "fully placed results preserve the native solver status");
      assert.equal(accepted.plans.length, result.plans.length);
      assert.equal(accepted.plans.reduce((count, plan) => count + plan.placements.length, 0), quantity);
      for (const [planIndex, plan] of accepted.plans.entries()) {
        assert.equal(plan.placements[0].nestedWithPrevious, false);
        assert.equal(plan.placements[0].gapBefore, 0);
        for (const [placementIndex, placement] of plan.placements.entries()) {
          for (const [key, value] of Object.entries(mappedResult.plans[planIndex].placements[placementIndex])) {
            assert.deepEqual(placement[key], value, `native placement field ${key} must be preserved`);
          }
        }
      }
      assert.equal(isNestingResultStale(h.view), false);
      nativeResultCount++;
    });
  }
}

console.log(`TubeDesigner nesting workflow tests passed (${completed.length} cases${nativeResultCount ? `, including ${nativeResultCount} native solver results` : ""})`);
