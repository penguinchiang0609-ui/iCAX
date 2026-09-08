import { escapeAttr as attr, escapeText as text } from "../../_shared/workbench/utils/format.mjs";
import { renderPunchLayoutControls } from "./punchLayoutView.mjs";

const fmt = value => Number.isFinite(Number(value)) ? new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3, useGrouping: false }).format(Number(value)) : "—";
const modeNames = { pitch: "固定间距", equal: "首尾等距均分", "end-margins": "两端留距均分", "middle-fixed": "整体居中", "center-out": "中心扩散", fill: "区间排满", "max-spacing": "最大间距均分", sequence: "不等距序列", positions: "位置表" };
const axes = [["X", "主管长度方向（X）"], ["Y", "横向（Y）"], ["Z", "高度方向（Z）"]];

function fieldAttrs(action, field, itemIndex, groupId, disabled) {
  return ' data-cam-change-action="' + attr(action("array-group-field")) + '" data-tube-designer-punch-index="' + attr(itemIndex)
    + '" data-tube-designer-punch-array-group="' + attr(groupId) + '" data-tube-designer-punch-array-field="' + attr(field) + '"' + (disabled ? " disabled" : "");
}
function select(action, field, value, values, itemIndex, groupId, disabled, label) {
  return '<label class="punch-array-field"><span>' + text(label) + '</span><select aria-label="' + attr(label) + '"'
    + fieldAttrs(action, field, itemIndex, groupId, disabled) + '>' + values.map(([key, name]) => '<option value="' + attr(key) + '"'
      + (String(key) === String(value) ? " selected" : "") + '>' + text(name) + '</option>').join("") + '</select></label>';
}
function input(action, field, value, itemIndex, groupId, disabled, label, unit = "mm") {
  return '<label class="punch-array-field"><span>' + text(label) + '</span><span class="tube-designer-punch-sheet-number"><input type="number" step="any" aria-label="'
    + attr(label) + '" value="' + attr(Number.isFinite(Number(value)) && value !== null ? value : "") + '"'
    + fieldAttrs(action, field, itemIndex, groupId, disabled) + '/><small>' + text(unit) + '</small></span></label>';
}
function check(action, field, checked, itemIndex, groupId, disabled, label) {
  return '<label class="punch-array-check"><input type="checkbox"' + fieldAttrs(action, field, itemIndex, groupId, disabled)
    + (checked ? " checked" : "") + '/><span>' + text(label) + '</span></label>';
}
function button(action, suffix, label, itemIndex, attrs = "", disabled = false) {
  return '<button type="button" class="tube-designer-secondary" data-cam-action="' + attr(action(suffix)) + '" data-tube-designer-punch-index="' + attr(itemIndex) + '" ' + attrs
    + (disabled ? " disabled" : "") + '>' + text(label) + '</button>';
}

export function punchArrayGroupSummary(group, resolved = group) {
  const active = group.enabled === false ? "（停用）" : "";
  if (group.type === "polar") return active + "绕 " + group.axis + " 圆周 · " + fmt(group.count) + " 个 · "
    + (group.angleMode === "full-circle" ? "整圈 " + fmt(360 / group.count) + "°" : group.angleMode === "angle-range" ? fmt(group.startAngle) + "°～" + fmt(group.endAngle) + "°" : "角距 " + fmt(group.angleStep) + "°");
  const mode=group.axis==="X"?group.distributionMode??"pitch":"pitch";
  const pitch=group.axis==="X"&&resolved.layoutSummary?resolved.layoutSummary.centerPitch:(["sequence","positions","equal","end-margins","max-spacing"].includes(mode)?null:group.spacing*(group.direction==="negative"?-1:1));
  const instances=group.enabled===false?group.count:resolved.instanceCount??group.count;
  return active + (group.axis === "X" ? "沿主管长度" : "沿 " + group.axis + " 直线") + " · " + fmt(instances) + " 个"
    + (pitch===null?"":" · "+fmt(pitch)+" mm") + (mode==="pitch"?"":" · "+(modeNames[mode]??mode));
}

export function renderPunchArrayGroupsSummary(groups = [], result = {}) {
  const error = result.arrayGroupsError;
  return '<div class="punch-array-summary" data-punch-array-summary>'
    + (groups.length ? groups.map(group => '<span data-array-group-id="' + attr(group.id) + '">' + text(punchArrayGroupSummary(group,result.arrayGroupsSummary?.groups?.find(resolved=>resolved.id===group.id))) + '</span>').join("") : '<span>单个刀具 · 未添加阵列</span>')
    + (error ? '<small class="tube-designer-punch-layout-error" role="alert">' + text(error) + '</small>' : result.arrayGroupsSummary ? '<small>组合后 ' + fmt(result.arrayGroupsSummary.instanceCount) + ' 个位置</small>' : "") + '</div>';
}

function renderGroup(action, feature, itemIndex, disabled, length, group, groupIndex) {
  const id = group.id, n = (field, label, unit) => input(action, field, group[field], itemIndex, id, disabled, label, unit);
  let fields;
  if (group.type === "polar") {
    fields = select(action, "angleMode", group.angleMode ?? "pitch", [["pitch", "固定角度步距"], ["full-circle", "整圈均分"], ["angle-range", "角区间均分"]], itemIndex, id, disabled, "圆周规则")
      + n("count", "数量（含首个）", "个") + n("startAngle", "起始角", "°")
      + (group.angleMode === "full-circle" ? '<small class="punch-array-help">全圆均布，360° 位置不重复。</small>' : group.angleMode === "angle-range" ? n("endAngle", "结束角", "°") : n("angleStep", "角度步距", "°"));
    const origin = group.origin ?? [length / 2, 0, 0];
    fields += '<details class="punch-array-origin"><summary>旋转轴位置 · ' + (group.origin ? "自定义" : "通过母材中心") + '</summary>'
      + check(action, "originAuto", !group.origin, itemIndex, id, disabled, "通过母材中心")
      + '<div class="punch-array-fields">' + ["X", "Y", "Z"].map((axis, i) => input(action, "origin" + axis, origin[i], itemIndex, id, disabled || !group.origin, "轴上一点 " + axis)).join("") + '</div>'
      + '<small class="punch-array-help">坐标以母材首端为 X=0；母材中心为 X=' + fmt(length / 2) + ' mm。</small></details>';
  } else if (group.axis === "X") {
    const item = { ...feature, ...group, layoutDatum: "base", rowCount: 1, skipInstancesText: "", arrayCount: group.count, arrayPitch: group.spacing };
    let controls = renderPunchLayoutControls(action, item, itemIndex, disabled, length, { showSummary: false, showSkip: false });
    controls = controls.replaceAll(attr(action("field-change")), attr(action("array-group-field")))
      .replace(/data-tube-designer-punch-field="([^"]+)"/g, (_, field) => 'data-tube-designer-punch-array-group="' + attr(id) + '" data-tube-designer-punch-array-field="' + ({ arrayCount: "count", arrayPitch: "spacing" }[field] ?? field) + '"');
    fields = '<div class="punch-array-length-rules">' + controls + '</div>'
      + (["pitch", "sequence"].includes(group.distributionMode) ? select(action, "direction", group.direction ?? "positive", [["positive", "沿轴正向"], ["negative", "沿轴反向"]], itemIndex, id, disabled, "复制方向") : "")
      + '<small class="punch-array-help">固定间距从刀具当前位置开始；正、负间距决定复制方向。两端留距与中心规则使用母材基准。</small>';
  } else fields = n("count", "数量（含首个）", "个") + n("spacing", "中心间距", "mm")
    + select(action, "direction", group.direction ?? "positive", [["positive", "沿轴正向"], ["negative", "沿轴反向"]], itemIndex, id, disabled, "复制方向")
    + '<small class="punch-array-help">从刀具当前位置开始，负间距会翻转所选复制方向。</small>';
  return '<section class="punch-array-group" data-array-group-id="' + attr(id) + '" data-array-group-index="' + groupIndex + '"><header><strong>阵列 ' + (groupIndex + 1) + '</strong>'
    + check(action, "enabled", group.enabled !== false, itemIndex, id, disabled, "启用")
    + button(action, "array-group-remove", "删除此组", itemIndex, 'data-tube-designer-punch-array-group="' + attr(id) + '"', disabled) + '</header>'
    + '<div class="punch-array-fields">' + select(action, "type", group.type, [["linear", "直线阵列"], ["polar", "圆周阵列"]], itemIndex, id, disabled, "阵列类型")
    + select(action, "axis", group.axis, axes, itemIndex, id, disabled, group.type === "polar" ? "旋转轴方向" : "直线方向") + fields + '</div></section>';
}

export function renderPunchArrayGroupsControls(action, feature, itemIndex, disabled, length, groups = [], result = {}, skipText = "") {
  const add = (label, type, axis) => button(action, "array-group-add", label, itemIndex,
    'data-tube-designer-punch-array-type="' + type + '" data-tube-designer-punch-array-axis="' + axis + '"', disabled);
  return '<div class="punch-array-editor" data-punch-array-editor><p>各组独立设置，可同时组合。按列表顺序逐组复制前面的结果；数量均包含首个刀具。刀具自身位置、姿态在独立入口设置。</p>'
    + '<nav class="punch-array-add" aria-label="添加独立阵列组">'
    + ["X","Y","Z"].map(axis=>add("＋ 沿 "+axis,"linear",axis)).join("")
    + ["X","Y","Z"].map(axis=>add("＋ 绕 "+axis,"polar",axis)).join("")+'</nav>'
    + '<small class="punch-array-help">沿 X / Y / Z：直线复制；绕 X / Y / Z：圆周复制。X 是主管长度方向。</small>'
    + groups.map((group, i) => renderGroup(action, feature, itemIndex, disabled, length, group, i)).join("")
    + (!groups.length ? '<p class="punch-array-empty">尚未添加阵列，只加工当前位置的一个刀具。选择上方按钮添加。</p>' : "")
    + '<details class="punch-array-skips"><summary>跳过指定组合位置' + (skipText ? " · 已设置" : "") + '</summary><label><span>各组序号（从 1 开始）</span>'
    + '<textarea rows="2" aria-label="跳过组合位置" data-cam-change-action="' + attr(action("array-skips-change")) + '" data-tube-designer-punch-index="' + attr(itemIndex)
    + '" data-tube-designer-punch-array-field="arraySkips"' + (disabled ? " disabled" : "") + ' placeholder="2:3, 4:*">' + text(skipText) + '</textarea></label>'
    + '<small class="punch-array-help">按组顺序填写：2:3 表示第 1 组第 2 个、第 2 组第 3 个；* 表示该组全部位置。多个组合用逗号分隔，不移动其他孔。</small></details>'
    + '<footer>' + renderPunchArrayGroupsSummary(groups, result) + '</footer></div>';
}
