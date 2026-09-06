import { buildProfileGroups, isTubeNestingPart, listManufacturingParts } from "./partsArea.mjs";
import { getNestingParameters, getNestingStockInputs } from "./nestingSettings.mjs";

const MAX_PARTS = 2000;
const EPSILON = 0.011; // Native lengths are conservatively quantized to 0.01 mm.

function selectedParts(view) {
  const parts = listManufacturingParts(view.scene?.tubeDesigner ?? {});
  const selected = Array.isArray(view.tubeDesignerSelectedPartIds)
    ? new Set(view.tubeDesignerSelectedPartIds.map(String)) : null;
  return parts.filter((part) => isTubeNestingPart(part) && (!selected || selected.has(String(part.entityId))));
}

function partName(part) {
  return String(part?.name || part?.partNumber || part?.entityId || "零件");
}

function serializeLockedPlan(plan) {
  const placements = Array.isArray(plan?.placements) ? plan.placements.map((placement) => ({
    partId: String(placement.partId ?? ""),
    instanceId: String(placement.instanceId ?? ""),
    start: Number(placement.start),
    end: Number(placement.end),
    length: Number(placement.length ?? (Number(placement.end) - Number(placement.start))),
    gapBefore: Number(placement.gapBefore ?? 0),
    nestedWithPrevious: Boolean(placement.nestedWithPrevious),
    variantId: String(placement.variantId ?? "default"),
    reversed: Boolean(placement.reversed),
    rotationRadians: Number(placement.rotationRadians ?? 0),
  })) : [];
  return {
    id: String(plan?.id ?? ""),
    stockTypeId: String(plan?.stockTypeId ?? ""),
    profileKey: String(plan?.profileKey ?? ""),
    stockLength: Number(plan?.stockLength),
    usedLength: Number(plan?.usedLength),
    remainingLength: Number(plan?.remainingLength),
    partLength: Number(plan?.partLength ?? placements.reduce((sum, placement) => sum + placement.length, 0)),
    utilization: Number(plan?.utilization ?? 0),
    placements,
  };
}

function lockedPlansForRequest(view, context) {
  const result = view?.tubeDesignerNestingResult;
  const locked = new Set((view?.tubeDesignerLockedNestingPlanIds ?? []).map(String));
  if (!result || !locked.size
    || result.inputSignature !== getNestingInputSignature(view, context)) return [];
  return (result.plans ?? []).filter((plan) => locked.has(String(plan.id))).map(serializeLockedPlan);
}

export function buildNestingRequest(view, context = null) {
  const parts = selectedParts(view);
  if (!parts.length) throw new Error("请先在左侧勾选需要排样的零件。");
  const groups = buildProfileGroups(parts);
  const stocks = getNestingStockInputs(view, context);
  if (!stocks.length) throw new Error("请先打开“母材设置”，填写母材长度和可用数量。");
  let count = 0;
  const seen = new Set();
  const demands = groups.flatMap((group) => {
    if (!stocks.some((stock) => stock.profileKey === group.key)) {
      throw new Error(`“${group.profile}”没有可用母材，请在“母材设置”中补充长度和数量。`);
    }
    return group.parts.map((part) => {
      const id = String(part.entityId ?? "");
      const quantity = Number(part.quantity ?? 1);
      if (!id || seen.has(id)) throw new Error("零件清单中存在无效或重复的零件标识，请重新拆单。");
      seen.add(id);
      if (!Number.isFinite(Number(part.length)) || Number(part.length) <= 0) {
        throw new Error(`“${partName(part)}”没有有效的下料长度。`);
      }
      if (!Number.isSafeInteger(quantity) || quantity <= 0) {
        throw new Error(`“${partName(part)}”的零件数量无效。`);
      }
      count += quantity;
      return { partEntityId: id, profileKey: group.key, quantity };
    });
  });
  if (count > MAX_PARTS) throw new Error(`单次最多支持 ${MAX_PARTS} 件零件，请分批选择排样。`);
  const keys = new Set(groups.map((group) => group.key));
  const usedStocks = stocks.filter((stock) => keys.has(stock.profileKey)).map(({ id, profileKey, length, quantity }) => {
    if (!id || !Number.isFinite(length) || length <= 0 || !Number.isSafeInteger(quantity)
      || (quantity !== -1 && quantity <= 0)) {
      throw new Error("母材长度或数量无效，请检查“母材设置”。");
    }
    return { id, profileKey, length, quantity };
  });
  return {
    parts: demands,
    stocks: usedStocks,
    parameters: getNestingParameters(view, context),
    lockedPlans: lockedPlansForRequest(view, context),
  };
}

export function getNestingInputSignature(view, context = null) {
  const parts = selectedParts(view);
  const groups = buildProfileGroups(parts);
  const keys = new Set(groups.map((group) => group.key));
  return JSON.stringify({
    parts: groups.flatMap((group) => group.parts.map((part) => ({
      id: String(part.entityId), profileKey: group.key,
      length: Number(part.length), quantity: Number(part.quantity ?? 1),
      resource: part.manufacturingGeometryResourceId ?? "",
      version: part.manufacturingGeometryResourceVersion ?? 0,
      thumbnail: part.thumbnailGeometryResourceId ?? "",
      thumbnailVersion: part.thumbnailGeometryResourceVersion ?? 0,
      endProcess: part.endProcess ?? part.properties?.["manufacturing.endProcess"] ?? null,
    }))).sort((a, b) => a.id.localeCompare(b.id)),
    stocks: getNestingStockInputs(view, context).filter((stock) => keys.has(stock.profileKey))
      .map(({ id, profileKey, length, quantity }) => ({ id, profileKey, length, quantity }))
      .sort((a, b) => a.id.localeCompare(b.id)),
    parameters: getNestingParameters(view, context),
  });
}

export function isNestingResultStale(view, context = null) {
  const result = view.tubeDesignerNestingResult;
  return Boolean(result && (view.tubeDesignerNestingLastRunFailed
    || result.inputSignature !== getNestingInputSignature(view, context)));
}

function validateResult(result, request, view) {
  if (!result || !Array.isArray(result.plans) || !Array.isArray(result.unplaced)) {
    throw new Error("算法没有返回有效的排样结果。");
  }
  const demands = new Map(request.parts.map((part) => [part.partEntityId, { ...part, accounted: 0 }]));
  const stocks = new Map(request.stocks.map((stock) => [stock.id, { ...stock, used: 0 }]));
  const planIds = new Set();
  const instanceIds = new Set();
  const sourceLengths = new Map(selectedParts(view).map((part) => [String(part.entityId), Number(part.length)]));
  for (const plan of result.plans) {
    const stock = stocks.get(plan.stockTypeId);
    if (!plan.id || planIds.has(plan.id) || !stock || stock.profileKey !== plan.profileKey
      || !Number.isFinite(plan.stockLength) || Math.abs(plan.stockLength - stock.length) > EPSILON
      || !Array.isArray(plan.placements) || !plan.placements.length) {
      throw new Error("算法返回的母材方案不符合输入设置。");
    }
    planIds.add(plan.id);
    stock.used++;
    if (stock.quantity !== -1 && stock.used > stock.quantity) throw new Error("算法使用的母材超出了可用数量。");
    let end = 0;
    for (const [index, placement] of plan.placements.entries()) {
      const part = demands.get(placement.partId);
      const gapBefore = Number(placement.gapBefore ?? (index ? request.parameters.partGap : 0));
      const rotationRadians = Number(placement.rotationRadians ?? 0);
      const nestedWithPrevious = Boolean(placement.nestedWithPrevious);
      if (!part || part.profileKey !== plan.profileKey || !Number.isFinite(placement.start)
        || !Number.isFinite(placement.end) || placement.end <= placement.start
        || !Number.isFinite(gapBefore) || !Number.isFinite(rotationRadians)
        || typeof placement.variantId !== "string" || !placement.variantId
        || placement.end - placement.start < sourceLengths.get(placement.partId) - EPSILON
        || Math.abs(placement.start - (end + gapBefore)) > EPSILON
        || (!index && (Math.abs(gapBefore) > EPSILON || nestedWithPrevious))
        || (index && nestedWithPrevious && gapBefore >= request.parameters.partGap - 1e-6)
        || (index && !nestedWithPrevious && gapBefore < request.parameters.partGap - EPSILON)
        || placement.end > plan.stockLength + EPSILON) {
        throw new Error("算法结果存在零件截面不匹配、非法套切、间距不足或超出母材的情况。");
      }
      if (placement.instanceId && instanceIds.has(placement.instanceId)) {
        throw new Error("算法结果中同一零件实例被重复排入。");
      }
      if (placement.instanceId) instanceIds.add(placement.instanceId);
      part.accounted++;
      end = placement.end;
    }
    if (!Number.isFinite(plan.usedLength) || Math.abs(end - plan.usedLength) > EPSILON
      || !Number.isFinite(plan.remainingLength)
      || Math.abs(plan.stockLength - plan.usedLength - plan.remainingLength) > EPSILON) {
      throw new Error("算法返回的已用长度与余料不一致。");
    }
  }
  for (const row of result.unplaced) {
    const demand = demands.get(row.partId);
    if (!demand || !Number.isSafeInteger(row.quantity) || row.quantity <= 0) {
      throw new Error("算法返回的未排零件清单无效。");
    }
    demand.accounted += row.quantity;
  }
  if ([...demands.values()].some((part) => part.accounted !== part.quantity)) {
    throw new Error("算法结果的零件数量与所选清单不一致。");
  }
}

function decorateResult(result, view, inputSignature, solveRequest) {
  const parts = new Map(listManufacturingParts(view.scene?.tubeDesigner ?? {}).map((part) => [String(part.entityId), part]));
  const profiles = new Map(buildProfileGroups([...parts.values()]).map((group) => [group.key, group]));
  const unplaced = result.unplaced.map((row) => ({ ...row, partName: partName(parts.get(row.partId)), reason: row.reason || "可用母材不足或母材长度不够" }));
  const profileOrdinals = new Map();
  return {
    ...result, inputSignature, solveRequest: structuredClone(solveRequest), generatedAt: new Date().toISOString(), unplaced,
    status: unplaced.length ? (result.plans.length ? "partial" : "infeasible") : result.status,
    plans: result.plans.map((plan) => {
      const profile = profiles.get(plan.profileKey)?.profile || "截面未指定";
      const ordinal = (profileOrdinals.get(profile) ?? 0) + 1;
      profileOrdinals.set(profile, ordinal);
      return {
        ...plan, name: `${profile}-${ordinal}`, profile,
        profileData: profiles.get(plan.profileKey)?.parts[0]?.profile ?? {},
        status: "已排样", partCount: plan.placements.length,
        utilization: Number.isFinite(Number(plan.utilization)) ? Number(plan.utilization) : 0,
        placements: plan.placements.map((placement) => ({
          ...placement, length: placement.end - placement.start,
          partName: partName(parts.get(placement.partId)),
          partNumber: parts.get(placement.partId)?.partNumber ?? "",
        })),
      };
    }),
  };
}

export async function handleNestingRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "nesting.start") return false;
  if (view.pending || view.tubeDesignerNestingOperation || view.tubeDesignerNestingSettingsSaving) return true;
  let operation = null;
  try {
    const request = buildNestingRequest(view, context);
    const inputSignature = getNestingInputSignature(view, context);
    if (!context.sceneProxy?.invoke) throw new Error("当前项目未连接排样服务。");
    operation = { kind: "nesting", title: "正在排样", stage: "按截面计算母材分配与切割顺序" };
    view.tubeDesignerNestingOperation = operation;
    view.tubeDesignerOperation = operation;
    view.pending = true;
    view.error = "";
    view.notice = "";
    ops.renderProject(context, view);
    // Give the busy overlay a frame before requesting the native calculation.
    await new Promise((resolve) => typeof requestAnimationFrame === "function" ? requestAnimationFrame(resolve) : resolve());
    const response = await context.sceneProxy.invoke("TubeDesigner.Nest", request, { timeoutMs: 180000 });
    if (inputSignature !== getNestingInputSignature(view, context)) {
      throw new Error("计算期间零件或设置已变化，请重新开始排样。");
    }
    validateResult(response, request, view);
    if (!view.tubeDesignerNestingResult && view.layout
      && (!view.layout.bottomHeight || view.layout.bottomHeight === 142)) {
      view.layout.bottomHeight = 220;
    }
    view.tubeDesignerNestingResult = decorateResult(response, view, inputSignature, request);
    view.tubeDesignerNestingLastRunFailed = false;
    view.tubeDesignerActiveNestingPlanId = response.plans[0]?.id ?? "";
    view.tubeDesignerSelectedNestingPlanIds = response.plans.map((plan) => String(plan.id));
    view.tubeDesignerLockedNestingPlanIds = request.lockedPlans.map((plan) => String(plan.id));
    view.tubeDesignerNestingSelectionKind = response.plans.length ? "plan" : "part";
    view.tubeDesignerActiveNestingPartId = "";
    view.tubeDesignerActiveNestingPlacementId = "";
    view.tubeDesignerPartViewportKey = "";
    const missing = response.unplaced.reduce((sum, row) => sum + row.quantity, 0);
    const placed = response.plans.reduce((sum, plan) => sum + plan.placements.length, 0);
    const lockedCount = request.lockedPlans.length;
    view.notice = `排样完成：${response.plans.length} 根母材，已排 ${placed} 件${missing ? `，未排 ${missing} 件，请查看下方未排原因` : "，全部排入"}${lockedCount ? `；已保留 ${lockedCount} 根锁定结果` : ""}。`;
    ops.appendProjectLog?.(context, missing ? "warning" : "info", view.notice);
  } catch (error) {
    view.error = `排样失败：${error?.message ?? String(error)}`;
    view.tubeDesignerNestingLastRunFailed = true;
    ops.appendProjectLog?.(context, "error", view.error);
  } finally {
    if (operation && view.tubeDesignerNestingOperation === operation) {
      view.tubeDesignerNestingOperation = null;
      if (view.tubeDesignerOperation === operation) view.tubeDesignerOperation = null;
      view.pending = false;
    }
    ops.renderProject(context, view);
  }
  return true;
}
