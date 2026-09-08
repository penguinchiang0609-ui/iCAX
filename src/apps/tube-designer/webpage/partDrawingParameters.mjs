import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";

const label = value => value != null && typeof value === "object"
  ? String(value["zh-CN"] ?? value["en-US"] ?? "") : String(value ?? "");

// Visibility is evaluated against the complete parameter value set, not only
// the field currently being rendered. Missing stored values use schema defaults.
export function isDrawingParameterVisible(condition, values = {}) {
  if (condition == null) return true;
  if (typeof condition === "boolean") return condition;
  if (Array.isArray(condition)) return condition.every(item => isDrawingParameterVisible(item, values));
  if (Array.isArray(condition.all)) return condition.all.every(item => isDrawingParameterVisible(item, values));
  if (Array.isArray(condition.any)) return condition.any.some(item => isDrawingParameterVisible(item, values));
  if (Array.isArray(condition.conditions)) return condition.op === "any"
    ? condition.conditions.some(item => isDrawingParameterVisible(item, values))
    : condition.conditions.every(item => isDrawingParameterVisible(item, values));
  const value = values[condition.parameter ?? condition.key];
  if (condition.op === "eq") return value === condition.value;
  if (condition.op === "ne") return value !== condition.value;
  return true;
}

export function parameterFields(action, descriptor, feature = {}, end = "") {
  if (!descriptor) return feature.toolRef
    ? '<div class="td-draw-fixed-note">当前刀具定义不可用；已保存节点只读，仅可删除。</div>' : "";
  if (descriptor.kind === "fixed") return '<div class="td-draw-fixed-note">定式刀具：形状尺寸固定，只调整定位与阵列。</div>';
  const definitions = Array.isArray(descriptor.parameters) ? descriptor.parameters : [];
  const values = { ...Object.fromEntries(definitions.map(definition => [definition.key, definition.defaultValue])),
    ...descriptor.defaultParameters, ...feature.parameters, ...feature.toolParameters };
  for (const definition of definitions) if (values[definition.key] == null) values[definition.key] = definition.defaultValue;
  return definitions.filter(definition => isDrawingParameterVisible(definition.visibleWhen, values))
    .map(definition => fieldControl(action, "parameter", label(definition.displayName ?? definition.label ?? definition.key),
      values[definition.key] ?? definition.defaultValue, definition, end, definition.key)).join("");
}

export function fieldControl(action, field, title, value, definition = {}, end = "", parameter = "") {
  const actionName = typeof action === "function" ? action("field-change") : String(action ?? "");
  const attributes = ` data-cam-change-action="${escapeAttr(actionName)}" data-tube-designer-punch-field="${escapeAttr(field)}"`
    + (end ? ` data-tube-designer-punch-end="${escapeAttr(end)}"` : "")
    + (parameter ? ` data-tube-designer-punch-parameter="${escapeAttr(parameter)}"` : "");
  const effectiveValue = value ?? definition.defaultValue;
  const options = definition.options ?? definition.choices;
  let control;
  if (Array.isArray(options)) {
    const markup = options.map(option => {
      const object = option != null && typeof option === "object";
      const optionValue = object ? option.value : option;
      const optionLabel = label(object ? option.label ?? option.displayName ?? optionValue : option);
      return `<option value="${escapeAttr(optionValue)}"${String(optionValue) === String(effectiveValue) ? " selected" : ""}>${escapeText(optionLabel)}</option>`;
    }).join("");
    control = `<select${attributes}>${markup}</select>`;
  } else if (definition.valueType === "boolean") {
    const checked = effectiveValue === true || effectiveValue === 1 || effectiveValue === "true";
    control = `<input type="checkbox"${attributes}${checked ? " checked" : ""}/>`;
  } else {
    const type = definition.valueType === "string" ? "text" : "number";
    const number = typeof effectiveValue === "number" && !Number.isFinite(effectiveValue) ? "" : effectiveValue ?? "";
    const step = definition.valueType === "integer" ? 1 : definition.step ?? definition.constraints?.step ?? "any";
    const limits = ["min", "max"].map(key => {
      const limit = definition[key] ?? definition.constraints?.[key === "min" ? "minimum" : "maximum"];
      return type === "number" && limit != null && Number.isFinite(Number(limit)) ? ` ${key}="${escapeAttr(limit)}"` : "";
    }).join("");
    control = `<input type="${type}"${type === "number" ? ` step="${escapeAttr(step)}"${limits}` : ""} value="${escapeAttr(number)}"${attributes}/>`;
  }
  return `<label><span>${escapeText(label(title))}</span>${control}${definition.unit ? `<small>${escapeText(label(definition.unit))}</small>` : ""}</label>`;
}
