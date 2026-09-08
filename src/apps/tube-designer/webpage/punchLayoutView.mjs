import { escapeAttr as attr, escapeText as text } from "../../_shared/workbench/utils/format.mjs";
import { resolvePunchLayout } from "./punchLayout.mjs";

const MODES = [
  ["pitch", "起始位置 + 固定间距"],
  ["end-margins", "两端留距、中间均分"],
  ["middle-fixed", "固定步距、整体居中"],
  ["center-out", "中心向两端扩散"],
  ["fill", "固定步距、区间排满"],
  ["max-spacing", "最大中心距、自动均分"],
  ["sequence", "不等距序列"],
  ["positions", "自定义孔中心位置"],
  ["equal", "首尾与孔距等距均分"],
];

const numberText = value => Number.isFinite(Number(value))
  ? new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3, useGrouping: false }).format(Number(value)) : "—";
const numericValue = value => typeof value === "number" && !Number.isFinite(value) ? "" : value ?? "";
const usesFinishedEndDatum = feature => feature?.toolTarget !== "part" && feature?.layoutDatum !== "base";
const datumNames = { long: "长点 / 包络", center: "端面中心", short: "短点" };
const referenceNames = { start: "距成品首端", end: "距成品尾端", center: "距成品中心" };

function endDatumNotice(feature) {
  return "成品端面基准 · " + (datumNames[feature.endDatum ?? "long"] ?? "待核对基准")
    + "；实际孔位以三维预览为准，不按母材绝对端距推算。";
}

function fieldAttrs(action, field, index, disabled) {
  return ' data-cam-change-action="' + attr(action("field-change")) + '"'
    + ' data-tube-designer-punch-field="' + attr(field) + '"'
    + ' data-tube-designer-punch-index="' + attr(index) + '"' + (disabled ? " disabled" : "");
}

function select(action, field, value, options, index, disabled, label) {
  return '<select aria-label="' + attr(label) + '"' + fieldAttrs(action, field, index, disabled) + '>'
    + options.map(([key, name]) => '<option value="' + attr(key) + '"' + (value === key ? " selected" : "") + '>' + text(name) + '</option>').join("")
    + '</select>';
}

function input(action, field, value, index, disabled, label, unit = "mm", options = {}) {
  const title = options.title ?? label;
  return '<label class="tube-designer-punch-layout-field" title="' + attr(title) + '"><span>' + text(label) + '</span>'
    + '<span class="tube-designer-punch-sheet-number"><input type="number" step="' + attr(options.step ?? "any")
    + '" aria-label="' + attr(label) + '" value="' + attr(numericValue(value)) + '"'
    + (options.min !== undefined ? ' min="' + attr(options.min) + '"' : "")
    + (options.placeholder ? ' placeholder="' + attr(options.placeholder) + '"' : "")
    + fieldAttrs(action, field, index, disabled) + '/>' + (unit ? '<small>' + text(unit) + '</small>' : "") + '</span></label>';
}

function textarea(action, field, value, index, disabled, label, hint, placeholder) {
  return '<label class="tube-designer-punch-layout-text"><span>' + text(label) + '</span><textarea rows="2" aria-label="'
    + attr(label) + '" placeholder="' + attr(placeholder) + '"' + fieldAttrs(action, field, index, disabled) + '>'
    + text(value) + '</textarea><small>' + text(hint) + '</small></label>';
}

function detail(summary, content, expanded = false) {
  return '<details class="tube-designer-punch-layout-details"' + (expanded ? " open" : "") + '><summary>' + text(summary) + '</summary>' + content + '</details>';
}

function resolved(feature, baseLength) {
  // Use the same solver as submission; the view must never maintain another set of layout rules.
  try { return resolvePunchLayout(feature ?? {}, baseLength); }
  catch (error) { return { layoutError: error?.message ?? "布局计算失败，请检查参数。" }; }
}

function summaryText(layout) {
  if (typeof layout.layoutSummary === "string") return layout.layoutSummary;
  const summary = layout.layoutSummary;
  if (!summary?.positions?.length) return "";
  const count = summary.columnCount ?? summary.positions.length;
  return "每排 " + count + " 孔 · "
    + (summary.centerPitch === null ? "不等中心距" : "中心距 " + numberText(summary.centerPitch) + " mm")
    + (summary.remainder > 0 ? " · 余量 " + numberText(summary.remainder) + " mm" : "")
    + (summary.skippedCount ? " · 跳过 " + summary.skippedCount + " 个" : "");
}

function computedSummary(layout) {
  return layout.layoutError
    ? '<span class="tube-designer-punch-layout-error" role="alert">' + text(layout.layoutError) + '</span>'
    : '<output class="tube-designer-punch-layout-result" aria-label="布局计算结果">' + text(summaryText(layout)) + '</output>';
}

export function renderPunchLayoutControls(action, item = {}, index = "draft", disabled = false, baseLength = 0, options = {}) {
  const mode = item.distributionMode ?? "pitch";
  const n = (field, label, unit = "mm", options = {}) => input(action, field, item[field], index, disabled, label, unit, options);
  const count = () => n("arrayCount", "孔数", "个", { step: 1, min: 1, title: "沿主管拉伸轴 X 的每排总孔数，包含首孔和末孔；额外多排在此基础上复制" });
  const pitch = () => n("arrayPitch", "孔间距", "mm", { title: "沿主管拉伸轴 X，相邻孔中心之间的距离，不是孔边净距" });
  const margins = () => n("headMargin", "首孔中心端距", "mm", { min: 0 }) + n("tailMargin", "末孔中心端距", "mm", { min: 0 });
  let fields = "", extra = "";
  if (mode === "equal") fields = count();
  else if (mode === "end-margins") fields = count() + margins();
  else if (mode === "middle-fixed") fields = count() + pitch();
  else if (mode === "center-out") {
    fields = count() + pitch() + n("centerOffset", "中心偏移", "mm", { title: "布局中心相对母材中心的长度方向偏移；0 表示以母材中心对称" });
    extra = select(action, "centerMode", item.centerMode ?? "hole", [["hole", "中心有孔（奇数）"], ["gap", "中心留空（偶数）"]], index, disabled, "扩散中心形式")
      + (item.centerMode === "gap" ? n("centerFirstOffset", "首对孔偏移", "mm", { placeholder: "半步距", min: 0, title: "第一对孔中心距布局中心的距离；留空自动取半个中心步距" }) : "");
  } else if (mode === "fill") {
    fields = pitch() + margins();
    extra = select(action, "fillAlign", item.fillAlign ?? "start", [["start", "余量留尾端"], ["center", "余量两端平分"], ["end", "余量留首端"]], index, disabled, "区间排满余量分配");
  } else if (mode === "max-spacing") {
    fields = n("maxSpacing", "最大中心距", "mm", { min: 0, title: "允许的相邻孔中心距离上限；自动增加孔数后均分，实际中心距不超过此值" }) + margins();
  } else if (mode === "sequence") {
    extra = detail("编辑不等距序列", textarea(action, "spacingSequence", item.spacingSequence ?? "", index, disabled,
      "相邻孔中心距序列", "从指定首孔开始，每个数值增加一个孔；逗号、空格或换行分隔。50*3 表示连续 3 段 50 mm。",
      "100, 150, 50*3"), !String(item.spacingSequence ?? "").trim());
  } else if (mode === "positions") {
    extra = detail("编辑孔中心位置表", textarea(action, "positionList", item.positionList ?? "", index, disabled,
      "距母材起点的孔中心位置", "每个数值对应一个孔中心，单位 mm；可粘贴表格的一列，逗号、空格或换行分隔。",
      "100, 250, 400, 750"), !String(item.positionList ?? "").trim());
  } else fields = count() + pitch();
  const skip = detail("跳过指定孔" + (String(item.skipInstancesText ?? "").trim() ? " · 已设置" : ""),
    textarea(action, "skipInstancesText", item.skipInstancesText ?? "", index, disabled, "跳过孔序号", "从 1 开始：排号:孔号；单个孔号表示第 1 排。逗号、空格或换行分隔，不移动其余孔。", "3, 2:5"));
  return '<div class="tube-designer-punch-layout-controls" data-punch-layout-mode="' + attr(mode) + '">'
    + select(action, "distributionMode", mode, MODES, index, disabled, "沿主管长度阵列方式（X 轴）")
    + (fields ? '<div class="tube-designer-punch-layout-fields">' + fields + '</div>' : "")
    + extra + (options.showSummary === false ? "" : computedSummary(resolved(item, baseLength)))
    + (usesFinishedEndDatum(item) ? '<span class="tube-designer-punch-layout-hint">' + text(endDatumNotice(item)) + '</span>' : "")
    + (options.showSkip === false ? "" : skip) + '</div>';
}

export function renderPunchRowLayoutControls(action, item = {}, index = "draft", disabled = false, baseLength = 0) {
  const round = item.face === "round", mode = round ? item.rowDistributionMode ?? "pitch" : "pitch";
  const n = (field, label, unit, options = {}) => input(action, field, item[field], index, disabled, label, unit, options);
  let fields = n("rowCount", round ? "周向数量" : "排数", "个", { step: 1, min: 1 });
  if (mode === "pitch") fields += n("rowPitch", round ? "角度步距" : "排间中心距", round ? "°" : "mm");
  else if (mode === "full-circle") fields += n("rowStartAngle", "相对起始角", "°", { title: "相对刀具初始姿态绕主管 X 轴旋转，不是跨孔型来源统一的绝对方位角" });
  else fields += n("rowStartAngle", "相对起始角", "°", { title: "相对刀具初始姿态绕主管 X 轴旋转" })
    + n("rowEndAngle", "相对结束角", "°", { title: "与起始角采用相同的刀具初始姿态基准" });
  const layout = resolved(item, baseLength);
  const hint = mode === "full-circle" ? "全圆均布，360° 位置不重复"
    : mode === "angle-range" ? "指定角区间均分，数量包含两端" : "";
  const sourceHint = item.section || item.recordKind === "branch" || item.recordKind === "dxf"
    ? "支管 / DXF 默认从 +Z 起，初始姿态可在刀具周向参数中调整。"
    : item.toolTarget === "part" ? "初始姿态由刀具参数决定。" : "壁面刀具默认以 +Y 为零角度方向。";
  const direction = item.toolTarget === "part" ? select(action, "face", item.face ?? "top", [["top", "Y 向多排"], ["left", "Z 向多排"], ["round", "周向多排"]], index, disabled, "额外多排方向") : "";
  const controls = direction
    + (round ? select(action, "rowDistributionMode", mode, [["pitch", "固定角度步距"], ["full-circle", "整圈均分"], ["angle-range", "指定角区间均分"]], index, disabled, "周向布局方式") : "")
    + '<div class="tube-designer-punch-layout-fields">' + fields + '</div>'
    + (hint ? '<span class="tube-designer-punch-layout-hint">' + text(hint) + '</span>' : "")
    + (round ? '<span class="tube-designer-punch-layout-hint">角度相对刀具初始姿态；' + text(sourceHint) + '不同来源的相同角值不代表相同绝对方向。</span>' : "")
    + (round && Array.isArray(layout.rowOffsets) && !layout.layoutError && mode !== "pitch"
      ? '<output class="tube-designer-punch-layout-result" aria-label="周向计算结果">'
        + (layout.rowOffsets.length > 1 ? "实际角距 " + numberText(layout.rowOffsets[1] - layout.rowOffsets[0]) + "°" : "单个周向位置") + '</output>' : "")
    + (item.toolTarget === "part" ? '<small class="tube-designer-punch-layout-hint">此处仅增加排数；单个刀具方位在参数中设置。</small>' : "");
  return '<div class="tube-designer-punch-layout-controls tube-designer-punch-row-layout">'
    + detail(Number(item.rowCount ?? 1) > 1 ? (round ? "周向 " : "横向 ") + item.rowCount + " 排" : "单排 · 横向 / 周向多排", controls, Number(item.rowCount ?? 1) > 1)
    + '</div>';
}

function positionsFromLayout(layout, baseLength) {
  const exact = layout.layoutPositions ?? layout.layoutSummary?.positions;
  if (Array.isArray(exact)) return exact;
  const origin = layout.reference === "end" ? baseLength - Number(layout.station ?? 0)
    : layout.reference === "center" ? baseLength / 2 + Number(layout.station ?? 0) : Number(layout.station ?? 0);
  const offsets = Array.isArray(layout.arrayOffsets) ? layout.arrayOffsets
    : Array.from({ length: Math.max(0, Math.min(1000, Number(layout.arrayCount) || 0)) }, (_, i) => (layout.reference === "end" ? -1 : 1) * i * Number(layout.arrayPitch ?? 0));
  return offsets.map(offset => origin + Number(offset));
}

export function renderPunchLayoutOverview(feature, baseLength = 0) {
  if (!feature) return '<section class="tube-designer-punch-layout-overview is-empty"><span>选择一条冲孔记录查看布局</span></section>';
  // Legacy side recipes can anchor to a trimmed/mitered finished end. This view
  // receives only the base length, not that face's actual geometric datum.
  if (usesFinishedEndDatum(feature)) {
    return '<section class="tube-designer-punch-layout-overview is-datum-relative" aria-label="端面基准布局说明">'
      + '<header><strong>端面基准布局</strong><span>' + text(endDatumNotice(feature)) + '</span></header>'
      + '<p>' + text((referenceNames[feature.reference ?? "start"] ?? "定位参数") + " " + numberText(feature.station) + " mm")
      + ' · 保留原记录定位方式；不显示未经端面几何验证的母材绝对位置及首末端距。</p></section>';
  }
  const layout = resolved(feature, baseLength);
  if (layout.layoutError) return '<section class="tube-designer-punch-layout-overview is-invalid" aria-label="孔位布局预览"><strong>布局无法应用</strong><span role="alert">' + text(layout.layoutError) + '</span></section>';
  const length = Number(baseLength), positions = positionsFromLayout(layout, length);
  if (!(length > 0) || !positions.length || positions.some(value => !Number.isFinite(value))) {
    return '<section class="tube-designer-punch-layout-overview is-invalid"><span role="alert">请输入有效的母材长度和孔位参数。</span></section>';
  }
  const rows = Math.max(1, Number(layout.rowCount) || 1), total = positions.length * rows;
  const enabledCount = Number.isFinite(layout.layoutSummary?.actualCount) ? layout.layoutSummary.actualCount : total;
  const skipped = new Set(layout.skippedInstances ?? []);
  const sorted = positions.map((position, index) => ({ position, index,
    activeRows: rows - Array.from({ length: rows }, (_, row) => skipped.has(row + ":" + index)).filter(Boolean).length,
  })).sort((a, b) => a.position - b.position);
  const active = sorted.filter(item => item.activeRows > 0);
  const shown = sorted.length <= 60 ? sorted : Array.from({ length: 60 }, (_, i) => sorted[Math.round(i * (sorted.length - 1) / 59)]);
  const first = sorted[0].position, last = sorted.at(-1).position;
  const rangeStart = Math.min(0, first), rangeEnd = Math.max(length, last);
  const scale = position => 36 + 608 * (position - rangeStart) / (rangeEnd - rangeStart);
  const stockStart = scale(0), stockEnd = scale(length), stockCenter = scale(length / 2);
  const dots = shown.map(({ position, index, activeRows }) => {
    const x = scale(position);
    return '<circle class="punch-layout-hole' + (activeRows === 0 ? ' is-skipped' : '') + (position < 0 || position > length ? ' is-outside' : '') + '" cx="' + attr(x) + '" cy="34" r="3" data-punch-layout-point="' + (index + 1)
      + '" data-punch-layout-active-rows="' + activeRows + '"><title>第 ' + (index + 1) + ' 个长度位置，距起点 ' + text(numberText(position)) + ' mm；'
      + (activeRows === 0 ? '各排均已跳过' : activeRows + ' 排启用') + '</title></circle>'
      + (activeRows === 0 ? '<path class="punch-layout-skipped-mark" d="M' + (x - 3) + ' 31l6 6m-6 0l6-6"/>' : '');
  }).join("");
  const candidatePrefix = skipped.size ? '候选' : '';
  const activePrefix = feature.enabled === false ? '未跳过' : '启用';
  const activeDistances = skipped.size && active.length
    ? activePrefix + '首孔中心端距 ' + numberText(active[0].position) + ' mm；' + activePrefix + '末孔中心端距 ' + numberText(length - active.at(-1).position) + ' mm。' : '';
  const label = '母材全长 ' + numberText(length) + ' mm；每排 ' + positions.length + ' 个孔中心，共 ' + rows + ' 排；' + candidatePrefix + '首孔距起点 ' + numberText(first) + ' mm，' + candidatePrefix + '末孔距终点 ' + numberText(length - last) + ' mm。' + activeDistances;
  return '<section class="tube-designer-punch-layout-overview" aria-label="孔位布局预览">'
    + '<header><strong>孔位布局</strong><span>' + text(summaryText(layout)) + '</span>'
    + '<small>' + (feature.enabled === false ? "此条已停用 · " : "") + '每排 ' + positions.length + ' × ' + rows + ' 排 · 已启用 ' + enabledCount + ' 个位置'
    + (layout.layoutSummary?.expandedCount > enabledCount ? ' · 含对面 ' + layout.layoutSummary.expandedCount + ' 个刀具' : '') + '</small></header>'
    + '<svg viewBox="0 0 680 82" role="img" aria-label="' + attr(label) + '">'
    + '<title>' + text(label) + '</title><rect class="punch-layout-stock" x="' + attr(stockStart) + '" y="26" width="' + attr(stockEnd - stockStart) + '" height="16" rx="2"/>'
    + '<path class="punch-layout-center" d="M' + attr(stockCenter) + ' 16V51"/><text class="punch-layout-caption" x="' + attr(stockCenter) + '" y="12" text-anchor="middle">母材中心 X=0</text>'
    + dots + '<text class="punch-layout-caption" x="' + attr(stockStart) + '" y="56">起点</text><text class="punch-layout-caption" x="' + attr(stockEnd) + '" y="56" text-anchor="end">终点</text>'
    + '<path class="punch-layout-dimension" d="M' + attr(stockStart) + ' 64H' + attr(stockEnd) + ' M' + attr(stockStart) + ' 60V68 M' + attr(stockEnd) + ' 60V68"/>'
    + '<text class="punch-layout-caption" x="' + attr(stockCenter) + '" y="77" text-anchor="middle">全长 ' + text(numberText(length)) + ' mm</text></svg>'
    + '<footer><span>' + candidatePrefix + '首孔中心端距 ' + text(numberText(first)) + ' mm</span><span>' + candidatePrefix + '末孔中心端距 ' + text(numberText(length - last)) + ' mm</span>'
    + (activeDistances ? '<span>' + text(activeDistances) + '</span>' : '')
    + '<small>尺寸均为孔中心距' + (skipped.size ? ' · 候选端距保留原布局规则；未跳过端距按至少一排启用的位置统计，不代表实际切孔边界' : '')
    + (positions.length > shown.length ? ' · 图示抽样 ' + shown.length + ' / ' + positions.length + ' 个长度位置' : '') + '</small></footer>'
    + (layout.layoutSummary?.outsideCenterCount ? '<p class="tube-designer-punch-layout-hint">端外刀具按实际相交裁剪；图中 ' + layout.layoutSummary.outsideCenterCount + ' 个长度位置的中心位于母材端外，不按完整孔计数。</p>' : '') + '</section>';
}

export const punchLayoutStyles = `
.tube-designer-punch-layout-controls { display: grid; gap: 3px; min-width: 0; }
.tube-designer-punch-layout-fields { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 2px 5px; min-width: 0; }
.tube-designer-punch-layout-field { display: grid; gap: 1px; min-width: 0; }
.tube-designer-punch-layout-field > span:first-child { color: #58727a; font-size: 8px; white-space: normal; }
.tube-designer-punch-layout-result { display: block; color: #316f67; font-size: 8px; line-height: 1.45; white-space: normal; }
.tube-designer-punch-layout-error { color: #a6372d; font-size: 9px; line-height: 1.45; white-space: normal; }
.tube-designer-punch-layout-hint { color: #6e858b; font-size: 8px; line-height: 1.4; }
.tube-designer-punch-layout-details { min-width: 0; font-size: 8px; }
.tube-designer-punch-layout-details > summary { padding: 2px 0; color: #376e72; cursor: pointer; }
.tube-designer-punch-layout-details > summary:focus-visible { outline: 1px solid #168f84; outline-offset: 1px; }
.tube-designer-punch-layout-text { display: grid; gap: 3px; min-width: 0; padding: 3px 0; }
.tube-designer-punch-layout-text > span { color: #58727a; }
.tube-designer-punch-layout-text > small { color: #6e858b; font-size: 8px; line-height: 1.45; white-space: normal; }
.tube-designer-punch-sheet .tube-designer-punch-layout-text textarea { width: 100%; min-width: 0; min-height: 40px; box-sizing: border-box; resize: vertical; padding: 4px; border: 1px solid #b8c9cd; border-radius: 2px; background: #fff; color: #29434b; font: inherit; }
.tube-designer-punch-sheet .tube-designer-punch-layout-text textarea:focus { outline: 1px solid #168f84; border-color: #168f84; }
.tube-designer-punch-layout-overview { min-width: 0; padding: 5px 10px 6px; margin: 0 10px 5px; border: 1px solid #b8c9cd; border-radius: 2px; background: #f5faf9; color: #3a5961; }
.tube-designer-punch-layout-overview > header, .tube-designer-punch-layout-overview > footer { display: flex; align-items: baseline; flex-wrap: wrap; gap: 3px 14px; font-size: 9px; }
.tube-designer-punch-layout-overview > header > strong { font-size: 10px; }
.tube-designer-punch-layout-overview > header > small { margin-left: auto; }
.tube-designer-punch-layout-overview > footer > small { color: #6e858b; margin-left: auto; }
.tube-designer-punch-layout-overview > svg { display: block; width: 100%; height: 66px; overflow: visible; }
.tube-designer-punch-layout-overview .punch-layout-stock { fill: #d9e8e9; stroke: #92aeb6; }
.tube-designer-punch-layout-overview .punch-layout-center { fill: none; stroke: #849da5; stroke-dasharray: 3 3; }
.tube-designer-punch-layout-overview .punch-layout-hole { fill: #127f75; stroke: #fff; stroke-width: .7; }
.tube-designer-punch-layout-overview .punch-layout-hole.is-skipped { fill: #e6ecee; stroke: #93a8ae; }
.tube-designer-punch-layout-overview .punch-layout-hole.is-outside { fill: #d59836; stroke: #9c6d2a; }
.tube-designer-punch-layout-overview .punch-layout-skipped-mark { fill: none; stroke: #9a5a4d; stroke-width: 1; }
.tube-designer-punch-layout-overview .punch-layout-caption { fill: #526e77; font-size: 10px; }
.tube-designer-punch-layout-overview .punch-layout-dimension { fill: none; stroke: #93aab0; stroke-width: .8; }
.tube-designer-punch-layout-overview.is-invalid { display: flex; flex-wrap: wrap; gap: 6px 12px; background: #fff4f1; border-color: #dcb1a6; color: #9c372c; font-size: 10px; }
.tube-designer-punch-layout-overview.is-empty { color: #6e858b; font-size: 10px; }
.tube-designer-punch-layout-overview.is-datum-relative { background: #faf8ef; border-color: #d9cd9f; }
.tube-designer-punch-layout-overview.is-datum-relative > p { margin: 4px 0 0; color: #796b3d; font-size: 10px; line-height: 1.5; }
`;
