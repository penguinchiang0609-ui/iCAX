import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";

// A CAD feature recipe. Wall thickness and exact edge geometry are resolved by
// the native geometry service; the editor only owns the user's parameters.
export function normalizeCadBevel(value) {
  return { ...value, enabled: value ? value.enabled !== false : false,
    side: value?.side ?? "outer", angleDegrees: value?.angleDegrees ?? 45,
    rootFace: value?.rootFace ?? 0,
    ...(Array.isArray(value?.segmentIndices) ? { segmentIndices: [...value.segmentIndices] } : {}) };
}

export function validateCadBevel(value, { segmentCount } = {}) {
  if (value == null) return "";
  if (typeof value !== "object" || Array.isArray(value)) return "坡口设置无效。";
  if (value.enabled !== undefined && typeof value.enabled !== "boolean") return "坡口启用状态无效。";
  // An inactive child preserves its draft without influencing generation.
  if (value.enabled === false) return "";
  const bevel = normalizeCadBevel(value);
  if (!["outer", "inner", "both"].includes(bevel.side)) return "请选择有效的坡口侧。";
  if (!Number.isFinite(bevel.angleDegrees) || bevel.angleDegrees <= 0 || bevel.angleDegrees >= 90)
    return "坡口角须大于 0° 且小于 90°。";
  if (!Number.isFinite(bevel.rootFace) || bevel.rootFace < 0) return "钝边须为有效的非负数。";
  if (value.segmentIndices !== undefined && (!Array.isArray(value.segmentIndices)
    || !value.segmentIndices.length || new Set(value.segmentIndices).size !== value.segmentIndices.length
    || value.segmentIndices.some(index => !Number.isSafeInteger(index) || index < 0
      || index >= (segmentCount ?? 10000)))) return "请至少选择一条有效的坡口边段。";
  return "";
}

export function changeCadBevel(value, field, target) {
  const bevel = normalizeCadBevel(value);
  if (field === "enabled") bevel.enabled = !!target.checked;
  else if (field === "side") bevel.side = String(target.value);
  else if (field === "angleDegrees" || field === "rootFace")
    bevel[field] = String(target.value).trim() === "" ? NaN : Number(target.value);
  else if (field === "scope") {
    if (target.value === "all") delete bevel.segmentIndices;
    else bevel.segmentIndices ??= [0];
  } else if (field === "segment") {
    const index = Number(target.dataset.cadBevelSegment);
    const selected = new Set(bevel.segmentIndices ?? []);
    if (target.checked) selected.add(index); else selected.delete(index);
    bevel.segmentIndices = [...selected].sort((a, b) => a - b);
  }
  return bevel;
}

// Curve operations provide source indices; this common recipe layer owns how
// the user's boundary selection follows splitting, reordering and joining.
export function remapCadBevel(value, segmentSources) {
  if (!value) return undefined;
  const bevel = structuredClone(value);
  if (!Array.isArray(bevel.segmentIndices)) return bevel;
  const selected = new Set(bevel.segmentIndices);
  bevel.segmentIndices = segmentSources.flatMap((source, index) => {
    const origins = Array.isArray(source) ? source : source == null ? [] : [source];
    return origins.length && origins.every(origin => selected.has(origin)) ? [index] : [];
  });
  if (!bevel.segmentIndices.length) bevel.enabled = false;
  return bevel;
}

export function mergeCadBevels(firstValue, secondValue, firstCount, secondCount) {
  const first = firstValue && normalizeCadBevel(firstValue), second = secondValue && normalizeCadBevel(secondValue);
  const same = first && second && ["side", "angleDegrees", "rootFace"].every(key => Object.is(first[key], second[key]));
  if (first?.enabled && second?.enabled && !same)
    throw new Error("两个图形的坡口参数不同，请统一或关闭坡口后再合并。");
  const value = first?.enabled ? first : second?.enabled ? second : first ?? second;
  if (!value) return undefined;
  if (first?.enabled && second?.enabled && same && first.segmentIndices === undefined && second.segmentIndices === undefined)
    return structuredClone(value);
  const selected = (recipe, count, offset) => recipe && (value.enabled ? recipe.enabled : !recipe.enabled)
    ? Array.from({ length: count }, (_, index) => index).filter(index => recipe.segmentIndices === undefined
      || recipe.segmentIndices.includes(index)).map(index => index + offset) : [];
  return { ...structuredClone(value), segmentIndices: [...selected(first, firstCount, 0), ...selected(second, secondCount, firstCount)] };
}

export function renderCadBevelEditor(value, { action, attributes = "", disabled = false,
  segmentCount, sideLabels = ["外侧", "内侧", "双面"] } = {}) {
  const bevel = normalizeCadBevel(value), inactive = disabled || !bevel.enabled;
  const data = field => `data-cad-bevel-field="${field}" data-cam-change-action="${escapeAttr(action ?? "")}" ${attributes}`;
  const number = (field, title, unit, step) => `<label><span>${title}</span><input type="number" step="${step}" value="${escapeAttr(Number.isFinite(bevel[field]) ? bevel[field] : "")}" ${data(field)} ${inactive ? "disabled" : ""}/><small>${unit}</small></label>`;
  return `<section class="cad-bevel-editor" aria-label="坡口设置">
    <label class="cad-bevel-enable"><input type="checkbox" ${data("enabled")} ${bevel.enabled ? "checked" : ""} ${disabled ? "disabled" : ""}/><span>启用坡口</span></label>
    <div class="tube-sketch-property-grid tube-designer-punch-field-grid">
      <label><span>坡口侧</span><select ${data("side")} ${inactive ? "disabled" : ""}>${["outer", "inner", "both"].map((side, index) => `<option value="${side}" ${bevel.side === side ? "selected" : ""}>${escapeText(sideLabels[index])}</option>`).join("")}</select></label>
      ${number("angleDegrees", "坡口角", "°", "1")}${number("rootFace", "钝边", "mm", "0.1")}
      ${segmentCount > 1 ? `<label><span>作用范围</span><select ${data("scope")} ${inactive ? "disabled" : ""}><option value="all" ${bevel.segmentIndices === undefined ? "selected" : ""}>整个轮廓</option><option value="selected" ${bevel.segmentIndices !== undefined ? "selected" : ""}>指定边段</option></select></label>
      ${bevel.segmentIndices !== undefined ? `<div class="wide cad-bevel-segments" aria-label="坡口边段">${Array.from({ length: segmentCount }, (_, index) => `<label><input type="checkbox" ${data("segment")} data-cad-bevel-segment="${index}" ${bevel.segmentIndices.includes(index) ? "checked" : ""} ${inactive ? "disabled" : ""}/><span>边 ${index + 1}</span></label>`).join("")}</div>` : ""}` : ""}
    </div><small>坡口角相对于原切口壁面；钝边是保留的壁厚。</small>
  </section>`;
}
export function bindBevelSelection(definition, topology) {
  delete definition.edgeNames;
  delete definition.referenceFaceName;
  const names = (definition.edgeIds ?? []).map(id => topology?.edges?.find(edge => Number(edge.id) === Number(id))?.persistentName);
  const face = Number(definition.referenceFaceId ?? 0);
  const faceName = face ? topology?.faces?.find(item => Number(item.id) === face)?.persistentName : "";
  if (names.length && names.every(Boolean) && (!face || faceName)) {
    definition.edgeNames = names;
    if (face) definition.referenceFaceName = faceName;
  }
  return definition;
}
