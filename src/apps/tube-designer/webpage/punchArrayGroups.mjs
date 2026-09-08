import { normalizePunchLayout, resolvePunchLayout, punchLayoutInstanceCount } from "./punchLayout.mjs";

// Array transforms act on an already positioned/oriented, unexpanded seed tool
// in native blank coordinates (X=0..L). Later groups multiply on the LEFT:
// final = G_last * ... * G_first. The first group varies fastest in the product.
// No arrayGroups property means the old recipe/expansion path, without migration.
const LIMIT = 1000, EPS = 1e-7;
const numeric = (value, fallback) => value === undefined ? fallback : value === "" || value === null ? NaN : Number(value);
const finite = Number.isFinite;
const clean = value => Math.abs(value) < 1e-12 ? 0 : value;
const identity = () => [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
const layoutFields = ["distributionMode","headMargin","tailMargin","centerOffset","centerMode","centerFirstOffset","fillAlign","maxSpacing","spacingSequence","positionList"];
const count = (value, title) => { if (!Number.isSafeInteger(value) || value < 1 || value > LIMIT) throw new Error(`${title}须为 1 至 ${LIMIT} 的整数。`); };
const enabled = group => group.enabled !== false;
export const hasPunchArrayGroups = feature => Object.hasOwn(feature ?? {}, "arrayGroups");

export function normalizePunchArrayGroup(group = {}, index = 0) {
  if (!group || typeof group !== "object" || Array.isArray(group)) throw new Error("阵列组必须是参数对象。");
  const layout = normalizePunchLayout({ ...group, arrayCount: group.count, arrayPitch: group.spacing });
  return { ...group, id: String(group.id ?? `array-group-${index + 1}`), enabled: group.enabled !== false,
    type: String(group.type ?? "linear"), axis: String(group.axis ?? "X").toUpperCase(),
    count: numeric(group.count, 1), spacing: numeric(group.spacing, 50), direction: String(group.direction ?? "positive"),
    ...Object.fromEntries(layoutFields.map(key => [key, layout[key]])),
    angleMode: String(group.angleMode ?? "full-circle"), startAngle: numeric(group.startAngle, 0),
    endAngle: numeric(group.endAngle, 180), angleStep: numeric(group.angleStep, 90),
    origin: group.origin == null ? null : Array.isArray(group.origin) ? group.origin.map(value => numeric(value, NaN)) : group.origin,
  };
}

export function multiplyPunchArrayTransforms(a, b) {
  return Array.from({ length: 16 }, (_, i) => clean(Array.from({ length: 4 }, (_, k) => a[Math.floor(i / 4) * 4 + k] * b[k * 4 + i % 4]).reduce((sum, value) => sum + value, 0)));
}
const translation = (axis, value) => { const m = identity(); m[{ X: 3, Y: 7, Z: 11 }[axis]] = clean(value); return m; };
function rotation(axis, angle, origin) {
  const radians = (angle % 360) * Math.PI / 180, c = clean(Math.cos(radians)), s = clean(Math.sin(radians));
  const m = axis === "X" ? [1,0,0,0, 0,c,-s,0, 0,s,c,0, 0,0,0,1]
    : axis === "Y" ? [c,0,s,0, 0,1,0,0, -s,0,c,0, 0,0,0,1]
    : [c,-s,0,0, s,c,0,0, 0,0,1,0, 0,0,0,1];
  for (let row = 0; row < 3; row++) m[row * 4 + 3] = clean(origin[row] - origin.reduce((sum, value, col) => sum + m[row * 4 + col] * value, 0));
  return m;
}
function seedX(feature, length) {
  const station = numeric(feature.station, 0);
  if (!finite(station)) throw new Error("刀具起始位置必须是有效数字。");
  if ((feature.reference ?? "start") === "start") return station;
  if (feature.reference === "end") return length - station;
  if (feature.reference === "center") return length / 2 + station;
  throw new Error("请选择有效的刀具定位基准。");
}
function resolveGroup(group, feature, length, seed) {
  if (!["linear", "polar"].includes(group.type)) throw new Error("阵列类型须为直线或圆周。");
  if (!["X", "Y", "Z"].includes(group.axis)) throw new Error("阵列轴须为世界 X、Y 或 Z；尚不支持未定义的局部法向。");
  if (!enabled(group)) return { ...group, instanceCount: 1, transforms: [identity()], values: [0], disabled: true };
  let values, transforms, layoutSummary;
  if (group.type === "linear") {
    if (!["positive", "negative"].includes(group.direction)) throw new Error("请选择有效的直线阵列方向。");
    const sign = group.direction === "negative" ? -1 : 1;
    if (group.axis === "X") {
      const fields = Object.fromEntries(layoutFields.map(key => [key, group[key]]));
      const reverseSequence = group.distributionMode === "sequence" && sign < 0;
      const layout = resolvePunchLayout({ ...fields, reference: reverseSequence ? "end" : "start", station: reverseSequence ? length - seed : seed,
        arrayCount: group.count, arrayPitch: group.spacing * (group.distributionMode === "pitch" ? sign : 1),
        face: "top", offset: 0, rowCount: 1, rowPitch: 0, rowDistributionMode: "pitch", skipInstancesText: "" }, length);
      if (layout.layoutError) throw new Error(layout.layoutError);
      values = layout.layoutSummary.positions.map(position => clean(position - seed));
      layoutSummary = layout.layoutSummary;
    } else {
      count(group.count, "直线阵列数量");
      if (!finite(group.spacing) || (group.count > 1 && Math.abs(group.spacing) <= EPS)) throw new Error("多个直线实例的步距必须是非零有效数字。");
      values = Array.from({ length: group.count }, (_, index) => clean(index * group.spacing * sign));
    }
    transforms = values.map(value => translation(group.axis, value));
  } else {
    count(group.count, "圆周阵列数量");
    if (!["pitch", "full-circle", "angle-range"].includes(group.angleMode)) throw new Error("请选择固定角距、整圈或角区阵列。");
    if (!finite(group.startAngle)) throw new Error("圆周起角必须是有效数字。");
    let step;
    if (group.angleMode === "full-circle") step = 360 / group.count;
    else if (group.angleMode === "pitch") {
      if (!finite(group.angleStep)) throw new Error("圆周角距必须是有效数字。");
      step = group.angleStep;
    } else {
      if (!finite(group.endAngle)) throw new Error("圆周终角必须是有效数字。");
      const span = group.endAngle - group.startAngle;
      if (Math.abs(span) > 360 + EPS) throw new Error("圆周角区不得超过一整圈。");
      const fullCircle = Math.abs(Math.abs(span) - 360) <= EPS;
      if (group.count === 1 && Math.abs(span) > EPS && !fullCircle) throw new Error("只有一个圆周实例时，角区起角与终角必须相同。");
      step = fullCircle ? span / group.count : group.count > 1 ? span / (group.count - 1) : 0;
    }
    values = Array.from({ length: group.count }, (_, index) => group.startAngle + index * step);
    const angles = values.map(value => ((value % 360) + 360) % 360).sort((a,b) => a-b);
    if (angles.some((value, index) => index && Math.abs(value - angles[index-1]) <= EPS)
      || angles.length > 1 && 360 - angles.at(-1) + angles[0] <= EPS) throw new Error("圆周实例存在重复角度；0° 与 360° 是同一位置，请使用整圈均分。");
    const origin = group.origin ?? [length / 2, 0, 0];
    if (!Array.isArray(origin) || origin.length !== 3 || !origin.every(finite)) throw new Error("圆周中心须为三个有效坐标。");
    transforms = values.map(value => rotation(group.axis, value, origin));
  }
  if (transforms.some(matrix => matrix.some(value => !finite(value)))) throw new Error("阵列变换不是有限数字，请减小坐标、步距或数量。");
  return { ...group, values, transforms, instanceCount: transforms.length, ...(layoutSummary ? { layoutSummary } : {}) };
}

export function parsePunchArraySkipText(text, groups) {
  const input = String(text ?? "").trim(); if (!input) return [];
  if (input.length > 50000) throw new Error("跳过组合文本过长。");
  return input.split(/[\s,，;；]+/u).map(token => {
    const indices = token.split(/[:：]/u);
    if (indices.length !== groups.length || indices.some(value => value !== "*" && !/^\d+$/.test(value))) throw new Error(`跳过组合“${token}”须按 ${groups.length} 组填写，例如 2:3，* 表示该组任意实例。`);
    const entries = indices.flatMap((value, index) => {
      if (value === "*") return [];
      const n = Number(value); if (!Number.isSafeInteger(n) || n < 1) throw new Error("跳过组合序号须为正整数。");
      return [[groups[index].id, n - 1]];
    });
    if (!entries.length) throw new Error("不能跳过所有阵列组合。");
    return Object.fromEntries(entries);
  });
}
export function punchArraySkipText(feature) {
  if (feature.arraySkipText !== undefined) return String(feature.arraySkipText);
  return (feature.arraySkips ?? []).map(pattern => (feature.arrayGroups ?? []).map(group =>
    Object.hasOwn(pattern, group.id) ? Number(pattern[group.id]) + 1 : "*").join(":")).join(", ");
}
function skipPatterns(feature, groups) {
  const patterns = feature.arraySkipText === undefined ? feature.arraySkips ?? [] : parsePunchArraySkipText(feature.arraySkipText, groups);
  if (!Array.isArray(patterns) || patterns.length > LIMIT) throw new Error("跳过组合须为有效列表，且不超过 1000 条。");
  const seen = new Set();
  for (const pattern of patterns) {
    if (!pattern || typeof pattern !== "object" || Array.isArray(pattern) || !Object.keys(pattern).length) throw new Error("跳过组合须指定至少一个阵列组序号。");
    for (const [id,index] of Object.entries(pattern)) {
      const group = groups.find(item => item.id === id);
      if (!group) throw new Error(`跳过组合引用了不存在的阵列组 ${id}。`);
      if (!Number.isSafeInteger(index) || index < 0 || (enabled(group) && index >= group.instanceCount)) throw new Error(`跳过组合超出阵列组 ${id} 的实例范围。`);
    }
    const key = JSON.stringify(Object.entries(pattern).sort(([a],[b]) => a.localeCompare(b)));
    if (seen.has(key)) throw new Error("跳过组合重复填写。"); seen.add(key);
  }
  return patterns;
}

export function resolvePunchArrayGroups(feature = {}, baseLength = 0) {
  if (!hasPunchArrayGroups(feature)) return { ...feature };
  const result = { ...feature, arrayTransforms: [], arrayInstances: [], arrayCandidateCount: 0, arrayGroupsError: "",
    arrayGroupsSummary: { groups: [], candidateCount: 0, skippedCount: 0, instanceCount: 0, expandedCount: 0, dormantSkipCount: 0 },
    // These are transport values only; the editable rule lives in arrayGroups.
    // Native must not expand the old arrays on top of the explicit transforms.
    arrayCount: 1, rowCount: 1, arrayPitch: 0, rowPitch: 0, arrayOffsets: [0], rowOffsets: [0], skippedInstances: [], skipInstancesText: "" };
  try {
    const length = Number(baseLength);
    if (!finite(length) || length <= 0) throw new Error("主管长度必须是大于零的有效数字。");
    if (!Array.isArray(feature.arrayGroups) || feature.arrayGroups.length > LIMIT) throw new Error("阵列组须为有效列表，且不超过 1000 组。");
    result.arrayGroups = feature.arrayGroups.map(normalizePunchArrayGroup);
    const ids = new Set();
    for (const group of result.arrayGroups) {
      if (!group.id || group.id.length > 160 || ids.has(group.id)) throw new Error("每个阵列组须有唯一且非空的标识。"); ids.add(group.id);
    }
    const seed = seedX(feature, length);
    const both = feature.depthMode === "both" || (feature.opposite && !feature.through && feature.depthMode !== "through");
    let candidateCount = 1;
    const groups=[];
    for(const group of result.arrayGroups) {
      let resolved;
      try { resolved=resolveGroup(group, feature, length, seed); }
      catch (error) { throw new Error(`阵列组 ${group.id}：${error.message}`); }
      candidateCount *= resolved.instanceCount;
      if (candidateCount * (both ? 2 : 1) > LIMIT) throw new Error("笛卡尔组合超过 1000 个候选刀具（含双面）；跳过组合仍计入上限。");
      groups.push(resolved);
    }
    result.arrayCandidateCount = candidateCount;
    const patterns = skipPatterns(feature, groups);
    result.arraySkips = patterns.map(pattern => ({ ...pattern }));
    let instances = [{ indices: {}, matrix: identity() }];
    for (const group of groups) instances = group.transforms.flatMap((matrix,index) => instances.map(instance => ({
      indices: { ...instance.indices, [group.id]: index }, matrix: multiplyPunchArrayTransforms(matrix, instance.matrix),
    })));
    let dormantSkipCount = 0;
    const activePatterns = patterns.filter(pattern => {
      const dormant = Object.keys(pattern).some(id => !enabled(groups.find(group => group.id === id)));
      if (dormant) dormantSkipCount++; return !dormant;
    });
    const active = instances.filter(instance => !activePatterns.some(pattern => Object.entries(pattern).every(([id,index]) => instance.indices[id] === index)));
    if (!active.length) throw new Error("所有阵列组合均已跳过；请保留至少一个实例，或停用整条记录。");
    if (active.some(instance => instance.matrix.some(value => !finite(value)))) throw new Error("组合变换不是有限数字，请减小坐标或组数。");
    const unique=new Set();
    for(const instance of active) {
      const key=instance.matrix.map(value=>Math.sign(value)*Math.round(Math.abs(value)*1e8)).join(",");
      if(unique.has(key))throw new Error("阵列组组合后存在完全重复的刀具位置与姿态；请调整步距、组数或跳过重复组合。");
      unique.add(key);
    }
    result.arrayInstances = active;
    result.arrayTransforms = active.map(instance => instance.matrix);
    result.arrayGroupsSummary = { groups: groups.map(({ transforms, ...group }) => group), candidateCount,
      skippedCount: candidateCount - active.length, instanceCount: active.length, expandedCount: active.length * (both ? 2 : 1), dormantSkipCount, seedX: seed };
  } catch (error) {
    result.arrayGroupsError = error instanceof Error ? error.message : "阵列组无法求解。";
    result.arrayTransforms = []; result.arrayInstances = [];
  }
  return result;
}
export const validatePunchArrayGroups = (feature, length) => feature?.enabled === false ? "" : resolvePunchArrayGroups(feature, length).arrayGroupsError ?? "";
export function punchArrayGroupInstanceCount(feature, length) {
  if (feature?.enabled === false) return 0;
  if (!hasPunchArrayGroups(feature)) return punchLayoutInstanceCount(feature, length);
  const resolved = resolvePunchArrayGroups(feature, length);
  return resolved.arrayGroupsError ? 0 : resolved.arrayGroupsSummary.expandedCount;
}

/** Explicit editor migration only. Normal reading/rendering must not assign it. */
export function migrateLegacyPunchArrays(feature = {}, length = 0) {
  if (hasPunchArrayGroups(feature)) return { ...feature, arrayGroups: Array.isArray(feature.arrayGroups) ? feature.arrayGroups.map(normalizePunchArrayGroup) : feature.arrayGroups };
  const legacy = resolvePunchLayout(feature, length), normalized = normalizePunchLayout(feature);
  if (legacy.layoutError) return { ...feature, arrayGroupsError: "旧阵列无法迁移：" + legacy.layoutError };
  const x = normalizePunchArrayGroup({ id: "legacy-length", type: "linear", axis: "X", count: normalized.arrayCount, spacing: normalized.arrayPitch,
    ...Object.fromEntries(layoutFields.map(key => [key, normalized[key]])),
    direction: ["pitch","sequence"].includes(normalized.distributionMode) && legacy.reference === "end" ? "negative" : "positive" });
  const arrayGroups = [x];
  if (legacy.rowCount > 1 || Math.abs(legacy.rowOffsets[0] ?? 0) > EPS) {
    const row = legacy.face === "round"
      ? { id: "legacy-rows", type: "polar", axis: "X", count: legacy.rowCount,
        angleMode: normalized.rowDistributionMode === "pitch" ? "pitch" : normalized.rowDistributionMode,
        startAngle: legacy.rowOffsets[0], angleStep: legacy.rowPitch, endAngle: legacy.rowOffsets.at(-1), origin: null }
      // Legacy part-coordinate tools use only "left" for the Z translation;
      // side-coordinate tools use both side faces. Preserve native behavior.
      : { id: "legacy-rows", type: "linear", axis: (feature.toolTarget==="part" ? legacy.face==="left" : ["left","right"].includes(legacy.face)) ? "Z" : "Y", count: legacy.rowCount, spacing: legacy.rowPitch };
    arrayGroups.push(normalizePunchArrayGroup(row, 1));
  }
  const arraySkips = legacy.skippedInstances.map(key => { const [row,col] = key.split(":").map(Number); return {
    [x.id]: col, ...(arrayGroups.length > 1 ? { [arrayGroups[1].id]: row } : {}),
  }; });
  return { ...feature, station: legacy.station, reference: legacy.reference, offset: legacy.offset, arrayGroups, arraySkips };
}
