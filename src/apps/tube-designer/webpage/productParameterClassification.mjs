const DIMENSION_QUANTITIES = new Set([
  "length", "angle", "area", "volume",
]);

const DIMENSION_UNITS = new Set([
  "mm", "cm", "m", "in", "ft", "deg", "rad", "mm2", "mm3",
]);

const DIMENSION_NAME = /(width|height|length|depth|diameter|radius|thickness|clearance|gap|offset|spacing|pitch|margin|ratio|angle|drop|extension|going|rise|size|宽|高|长|深|厚|径|半径|间隙|净距|偏移|间距|边距|比例|角|延伸)/i;

function declaredKind(template, key) {
  const declaration = template?.extensions?.parameterLayout?.parameterKinds;
  if (!declaration || typeof declaration !== "object") return "";
  for (const kind of ["structure", "dimension"]) {
    if (Array.isArray(declaration[kind]) && declaration[kind].includes(key)) return kind;
  }
  return "";
}

export function productParameterKind(field, template) {
  const key = String(field?.key ?? field?.name ?? "");
  const explicit = String(field?.productParameterKind ?? declaredKind(template, key));
  if (explicit === "structure" || explicit === "dimension") return explicit;
  const valueType = String(field?.valueType ?? field?.type ?? "").toLowerCase();
  if (["boolean", "bool", "enum", "select", "string", "text", "integer", "int"].includes(valueType)) {
    return "structure";
  }
  const quantity = String(field?.quantity ?? "").toLowerCase();
  const unit = String(field?.unit ?? "").toLowerCase().replaceAll("²", "2").replaceAll("³", "3");
  if (DIMENSION_QUANTITIES.has(quantity) || DIMENSION_UNITS.has(unit)) return "dimension";
  const label = typeof field?.displayName === "object"
    ? String(field.displayName["zh-CN"] ?? field.displayName.zh ?? "")
    : String(field?.displayName ?? field?.label ?? "");
  return DIMENSION_NAME.test(`${key} ${label}`) ? "dimension" : "structure";
}
