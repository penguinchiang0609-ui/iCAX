// All longitudinal dimensions here locate tool centres, not hole-edge clearances.
// Keep the editable rule on the feature; offsets are a derived transport contract
// shared by the preview and the native cutter expansion.
const LIMIT = 1000;
const EPS = 1e-7;
const MODES = new Set(["pitch", "equal", "middle-fixed", "end-margins", "center-out", "fill", "max-spacing", "sequence", "positions"]);
const ROW_MODES = new Set(["pitch", "full-circle", "angle-range"]);
const numeric = (value, fallback) => value === undefined ? fallback : value === "" || value === null ? NaN : Number(value);
const finite = Number.isFinite;
const clean = value => Math.abs(value) < EPS ? 0 : value;

export function normalizePunchLayout(feature = {}) {
  return {
    ...feature,
    distributionMode: String(feature.distributionMode ?? "pitch"),
    reference: String(feature.reference ?? "start"),
    station: numeric(feature.station, 0),
    arrayCount: numeric(feature.arrayCount, 1),
    arrayPitch: numeric(feature.arrayPitch, 50),
    headMargin: numeric(feature.headMargin, numeric(feature.station, 0)),
    tailMargin: numeric(feature.tailMargin, 0),
    centerOffset: numeric(feature.centerOffset, 0),
    centerMode: String(feature.centerMode ?? "hole"),
    // null is an intentional automatic half-step, not a fixed previous half-step.
    centerFirstOffset: feature.centerFirstOffset == null || (typeof feature.centerFirstOffset === "string" && !feature.centerFirstOffset.trim())
      ? null : numeric(feature.centerFirstOffset, NaN),
    fillAlign: String(feature.fillAlign ?? "start"),
    maxSpacing: numeric(feature.maxSpacing, numeric(feature.arrayPitch, 50)),
    spacingSequence: String(feature.spacingSequence ?? ""),
    positionList: String(feature.positionList ?? ""),
    skipInstancesText: feature.skipInstancesText === undefined
      ? (Array.isArray(feature.skippedInstances) ? feature.skippedInstances.map(key => {
        const match = /^(\d+):(\d+)$/.exec(String(key));
        return match ? `${Number(match[1]) + 1}:${Number(match[2]) + 1}` : String(key);
      }).join(", ") : "") : String(feature.skipInstancesText),
    rowCount: numeric(feature.rowCount, 1),
    rowPitch: numeric(feature.rowPitch, 20),
    rowDistributionMode: String(feature.rowDistributionMode ?? "pitch"),
    rowStartAngle: numeric(feature.rowStartAngle, numeric(feature.offset, 0)),
    rowEndAngle: numeric(feature.rowEndAngle, 180),
  };
}

function parseNumbers(text, { repeat = false, title = "位置表" } = {}) {
  const input = text.trim();
  if (!input) throw new Error(`${title}不能为空。`);
  if (input.length > 50000) throw new Error(`${title}过长，单条记录最多支持 ${LIMIT} 个候选位置。`);
  const tokens = input.split(/[\s,，;；]+/u);
  const numberPattern = "[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?";
  const pattern = new RegExp(`^(${numberPattern})${repeat ? "(?:[*×xX](\\d+))?" : ""}$`);
  const values = [];
  for (const token of tokens) {
    const match = pattern.exec(token);
    if (!match || !finite(Number(match[1]))) throw new Error(`${title}中“${token}”不是有效数字${repeat ? "或重复项（例如 50*3）" : ""}。`);
    const count = match[2] === undefined ? 1 : Number(match[2]);
    if (!Number.isSafeInteger(count) || count < 1) throw new Error(`${title}的重复次数须为正整数。`);
    if (values.length + count > LIMIT) throw new Error(`${title}超过 ${LIMIT} 个候选位置。`);
    for (let i = 0; i < count; i++) values.push(Number(match[1]));
  }
  return values;
}

function requireCount(value, title) {
  if (!Number.isInteger(value) || value < 1 || value > LIMIT) throw new Error(`${title}须为 1 至 ${LIMIT} 的整数。`);
}
function requirePositive(value, title) {
  if (!finite(value) || value <= 0) throw new Error(`${title}必须是大于零的有效数字。`);
}
function intervals(f, length) {
  if (![f.headMargin, f.tailMargin].every(finite) || f.headMargin < 0 || f.tailMargin < 0) throw new Error("首端中心留距和尾端中心留距须为不小于零的有效数字。");
  const span = length - f.headMargin - f.tailMargin;
  if (span < -EPS) throw new Error("两端中心留距之和不能超过主管长度。");
  return Math.max(0, span);
}
function referencePosition(f, length) {
  if (!finite(f.station)) throw new Error("首孔位置必须是有效数字。");
  if (f.reference === "start") return f.station;
  if (f.reference === "end") return length - f.station;
  if (f.reference === "center") return length / 2 + f.station;
  throw new Error("请选择有效的定位基准。");
}
function duplicates(values, wrap = false) {
  const indexed = values.map((value, index) => ({ value: wrap ? ((value % 360) + 360) % 360 : value, index }))
    .sort((a, b) => a.value - b.value);
  for (let i = 1; i < indexed.length; i++) {
    if (Math.abs(indexed[i].value - indexed[i - 1].value) <= EPS) return [indexed[i - 1].index, indexed[i].index];
  }
  if (wrap && indexed.length > 1 && 360 - indexed.at(-1).value + indexed[0].value <= EPS) return [indexed.at(-1).index, indexed[0].index];
  return null;
}

function resolveRows(f) {
  requireCount(f.rowCount, "横向／周向排数");
  if (!ROW_MODES.has(f.rowDistributionMode)) throw new Error("请选择有效的横向／周向布局方式。");
  if (f.rowDistributionMode !== "pitch") {
    if (f.face !== "round") throw new Error("整圈和局部角区布局须选择周向角度加工面。");
    if (!finite(f.rowStartAngle)) throw new Error("周向起角必须是有效数字。");
    // Store the phase in the shared instance transform, not in side-tool-only
    // positioning. Part-coordinate branch/DXF tools intentionally ignore offset.
    f.offset = 0;
    if (f.rowDistributionMode === "full-circle") f.rowPitch = 360 / f.rowCount;
    else {
      if (!finite(f.rowEndAngle)) throw new Error("周向终角必须是有效数字。");
      const span = f.rowEndAngle - f.rowStartAngle;
      if (f.rowCount === 1 && Math.abs(span) > EPS) throw new Error("只有一排时，角区起角和终角必须相同；整圈请使用整圈等分。");
      if (f.rowCount > 1 && Math.abs(span) >= 360 - EPS) throw new Error("局部角区须小于 360°；整圈请使用整圈等分，避免首尾重复。");
      f.rowPitch = f.rowCount > 1 ? span / (f.rowCount - 1) : 0;
    }
  }
  if (!finite(f.rowPitch)) throw new Error("横向／周向步距必须是有效数字。");
  if (f.rowCount > 1 && Math.abs(f.rowPitch) <= EPS) throw new Error("多个孔的横向／周向阵列间距不能为零。");
  const phase = f.rowDistributionMode === "pitch" ? 0 : f.rowStartAngle;
  f.rowOffsets = Array.from({ length: f.rowCount }, (_, i) => clean(phase + i * f.rowPitch));
  if (f.rowOffsets.some(value => !finite(value))) throw new Error("横向／周向偏移不是有限数字，请减小步距或数量。");
  const repeated = duplicates(f.rowOffsets, f.face === "round");
  if (repeated) throw new Error(`第 ${repeated[0] + 1} 排与第 ${repeated[1] + 1} 排位置重复，请调整周向步距或排数。`);
}

function resolveSkips(f) {
  const tokens = f.skipInstancesText.trim() ? f.skipInstancesText.trim().split(/[\s,，;；]+/u) : [];
  const skipped = new Set();
  for (const token of tokens) {
    const match = /^(?:(\d+)[:：])?(\d+)$/.exec(token);
    if (!match) throw new Error(`跳过位置“${token}”无效，请填写孔序号或“排:孔”，例如 3、2:4。`);
    const row = match[1] === undefined ? 1 : Number(match[1]);
    const col = Number(match[2]);
    if (!Number.isSafeInteger(row) || !Number.isSafeInteger(col) || row < 1 || col < 1 || row > f.rowCount || col > f.arrayCount) {
      throw new Error(`跳过位置 ${token} 超出当前 ${f.rowCount} 排 × ${f.arrayCount} 孔的范围，请检查缩短后的布局。`);
    }
    const key = `${row - 1}:${col - 1}`;
    if (skipped.has(key)) throw new Error(`跳过位置 ${token} 重复填写。`);
    skipped.add(key);
  }
  f.skippedInstances = [...skipped].sort((a, b) => {
    const [ar, ac] = a.split(":").map(Number), [br, bc] = b.split(":").map(Number);
    return ar - br || ac - bc;
  });
}

/** Resolve once for both preview and generation. Re-resolving this output is safe.
 * arrayOffsets use world +X millimetres relative to the first tool, including
 * reverse direction for an end reference. The native layer must not reverse twice.
 * rowOffsets transform the source tool (degrees on the round face, mm otherwise).
 * In full-circle/angle-range they INCLUDE rowStartAngle and offset is set to zero,
 * so side tools and part-coordinate tools apply the same phase exactly once.
 * Legacy pitch rows remain relative to the first row and retain the old offset.
 */
export function resolvePunchLayout(feature = {}, baseLength = 0) {
  const f = normalizePunchLayout(feature), length = Number(baseLength);
  f.arrayOffsets = []; f.rowOffsets = []; f.skippedInstances = [];
  f.layoutError = "";
  f.layoutSummary = { positions: [], candidateCount: 0, skippedCount: 0, actualCount: 0, expandedCount: 0,
    firstCenter: null, lastCenter: null, headMargin: null, tailMargin: null, centerPitch: null, remainder: 0, distanceBasis: "center",
    outsideCenterCount: 0, requiresIntersectionCheck: false };
  try {
    if (!finite(length) || length <= 0) throw new Error("主管长度必须是大于零的有效数字。");
    if (!MODES.has(f.distributionMode)) throw new Error("请选择有效的轴向布局方式。");
    const mode = f.distributionMode;
    if (!["sequence", "positions", "fill", "max-spacing"].includes(mode)) requireCount(f.arrayCount, "每排总孔数");
    let positions = [], remainder = 0;
    if (mode === "positions") {
      positions = parseNumbers(f.positionList);
      f.reference = "start"; f.station = positions[0]; f.arrayCount = positions.length;
      f.arrayPitch = positions.length > 1 ? positions[1] - positions[0] : 0;
    } else if (mode === "sequence") {
      const steps = parseNumbers(f.spacingSequence, { repeat: true, title: "间距序列" });
      if (steps.some(step => step <= 0)) throw new Error("间距序列的每个步距必须大于零；从尾端排列请选择尾端基准。");
      if (steps.length + 1 > LIMIT) throw new Error(`间距序列包含首孔后超过 ${LIMIT} 个候选位置。`);
      const sign = f.reference === "end" ? -1 : 1;
      positions = [referencePosition(f, length)];
      for (const step of steps) positions.push(positions.at(-1) + sign * step);
      f.arrayCount = positions.length; f.arrayPitch = steps[0];
    } else if (mode === "center-out") {
      requirePositive(f.arrayPitch, "向外步距");
      if (!finite(f.centerOffset)) throw new Error("中心偏移必须是有效数字。");
      if (!["hole", "gap"].includes(f.centerMode)) throw new Error("请选择中心有孔或中心留空。");
      const center = length / 2 + f.centerOffset;
      if (f.centerMode === "hole") {
        if (f.arrayCount % 2 !== 1) throw new Error("中心有孔的对称布局须使用奇数总孔数；偶数请选择中心留空。");
        positions.push(center);
        for (let i = 1; i <= (f.arrayCount - 1) / 2; i++) positions.push(center - i * f.arrayPitch, center + i * f.arrayPitch);
      } else {
        if (f.arrayCount % 2 !== 0) throw new Error("中心留空的对称布局须使用偶数总孔数。");
        const first = f.centerFirstOffset === null ? f.arrayPitch / 2 : f.centerFirstOffset;
        requirePositive(first, "首对孔距中心偏移");
        for (let i = 0; i < f.arrayCount / 2; i++) positions.push(center - first - i * f.arrayPitch, center + first + i * f.arrayPitch);
      }
      positions.sort((a, b) => a - b);
      f.reference = "start"; f.station = positions[0];
    } else {
      if (mode === "equal") {
        f.reference = "start"; f.station = length / (f.arrayCount + 1); f.arrayPitch = f.station;
        f.headMargin = f.station; f.tailMargin = f.station;
      } else if (mode === "middle-fixed") {
        requirePositive(f.arrayPitch, "中心步距");
        f.reference = "start"; f.station = (length - (f.arrayCount - 1) * f.arrayPitch) / 2;
        f.headMargin = f.station; f.tailMargin = f.station;
      } else if (["end-margins", "fill", "max-spacing"].includes(mode)) {
        const span = intervals(f, length);
        f.reference = "start"; f.station = f.headMargin;
        if (mode === "end-margins") {
          if (f.arrayCount === 1 && span > EPS) throw new Error("只有一个孔时，两端中心留距必须定位到同一点；请修改留距或改用单孔定位。");
          f.arrayPitch = f.arrayCount > 1 ? span / (f.arrayCount - 1) : 0;
        } else if (mode === "fill") {
          requirePositive(f.arrayPitch, "固定中心步距");
          if (!["start", "center", "end"].includes(f.fillAlign)) throw new Error("请选择首端、居中或尾端对齐。");
          const quotient = span / f.arrayPitch;
          // Rounding tolerance is relative to physical endpoint distance, not to
          // a potentially huge quotient from a tiny (invalid) pitch.
          const intervalsCount = Math.floor(quotient + Math.min(EPS / f.arrayPitch, 1e-8));
          f.arrayCount = intervalsCount + 1;
          remainder = Math.max(0, span - intervalsCount * f.arrayPitch);
          f.station += f.fillAlign === "center" ? remainder / 2 : f.fillAlign === "end" ? remainder : 0;
        } else {
          requirePositive(f.maxSpacing, "最大中心步距");
          const intervalsCount = span <= EPS ? 0 : Math.max(1, Math.ceil(span / f.maxSpacing - 1e-12));
          f.arrayCount = intervalsCount + 1;
          f.arrayPitch = intervalsCount ? span / intervalsCount : 0;
        }
      }
      requireCount(f.arrayCount, "每排总孔数");
      if (!finite(f.arrayPitch)) throw new Error("中心步距必须是有效数字。");
      if (f.arrayCount > 1 && Math.abs(f.arrayPitch) <= EPS) throw new Error("多个孔的阵列间距不能为零。");
      const first = referencePosition(f, length), sign = f.reference === "end" ? -1 : 1;
      positions = Array.from({ length: f.arrayCount }, (_, i) => first + sign * i * f.arrayPitch);
    }
    positions = positions.map(clean);
    const repeated = duplicates(positions);
    if (repeated) throw new Error(`第 ${repeated[0] + 1} 孔与第 ${repeated[1] + 1} 孔中心位置重复或间距过小。`);
    // A layout specifies tool positions, not a requirement that all centres or
    // full profiles fit inside the stock. End intersections are clipped by the
    // native geometry; no separate open-cut switch is needed for any source.
    let outsideCenterCount = 0;
    for (let i = 0; i < positions.length; i++) {
      const s = positions[i];
      if (!finite(s)) throw new Error(`第 ${i + 1} 孔位置不是有限数字，请减小位置或步距。`);
      if (s < -EPS || s > length + EPS) {
        outsideCenterCount++;
      }
    }
    f.arrayOffsets = positions.map(position => clean(position - positions[0]));
    if (f.arrayOffsets.some(value => !finite(value))) throw new Error("轴向偏移不是有限数字，请减小位置范围。");
    resolveRows(f);
    const candidateCount = f.arrayCount * f.rowCount;
    if (candidateCount > LIMIT) throw new Error(`单条记录最多支持 ${LIMIT} 个候选位置，当前为 ${candidateCount} 个。`);
    const both = f.depthMode === "both" || (f.opposite && !f.through && f.depthMode !== "through");
    if (candidateCount * (both ? 2 : 1) > LIMIT) throw new Error(`单条记录最多支持 ${LIMIT} 个候选展开刀具，含双面后为 ${candidateCount * 2} 个；跳过孔仍计入阵列上限。`);
    resolveSkips(f);
    const actualCount = candidateCount - f.skippedInstances.length;
    if (!actualCount) throw new Error("所有候选孔位均已跳过，请保留至少一个孔位；无需冲孔时可停用或删除整条记录。");
    const expandedCount = actualCount * (both ? 2 : 1);
    const firstCenter = Math.min(...positions), lastCenter = Math.max(...positions);
    const diffs = positions.slice(1).map((p, i) => p - positions[i]);
    const centerPitch = !diffs.length ? 0 : diffs.every(p => Math.abs(p - diffs[0]) <= EPS) ? Math.abs(diffs[0]) : null;
    f.layoutSummary = { positions, candidateCount, skippedCount: f.skippedInstances.length, actualCount, expandedCount,
      firstCenter, lastCenter, headMargin: firstCenter, tailMargin: length - lastCenter, centerPitch, remainder,
      distanceBasis: "center", rowCount: f.rowCount, columnCount: f.arrayCount, rowPitch: f.rowPitch,
      outsideCenterCount, requiresIntersectionCheck: outsideCenterCount > 0 };
  } catch (error) {
    f.layoutError = error instanceof Error ? error.message : "布局无法求解，请检查参数。";
    // An invalid result must never carry a stale or partially expanded cutter list.
    f.arrayOffsets = []; f.rowOffsets = []; f.skippedInstances = [];
  }
  return f;
}

export function validatePunchLayout(feature = {}, baseLength = 0) {
  return feature.enabled === false ? "" : resolvePunchLayout(feature, baseLength).layoutError;
}

/** Number of effective cutter instances, including a separately cut opposite face. */
export function punchLayoutInstanceCount(feature = {}, baseLength = 0) {
  if (feature.enabled === false) return 0;
  const resolved = resolvePunchLayout(feature, baseLength);
  return resolved.layoutError ? 0 : resolved.layoutSummary.expandedCount;
}
