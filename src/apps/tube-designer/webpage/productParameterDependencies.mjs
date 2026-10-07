// Structure choices are fixed at creation. Dimensions/layout remain editable;
// Only assembly/process inputs skip display regeneration. Material/profile
// inputs remain display dependencies, independently of their editor location.
export function manufacturingOnlyParameterKeys(template = {}) {
  const declared = template.extensions?.parameterDependencies?.manufacturingOnly;
  if (declared === undefined) return new Set();
  if (!Array.isArray(declared)) throw new TypeError("产品制造参数依赖声明必须是数组");
  const known = new Set((template.parameters ?? []).map((field) => String(field.key ?? field.name ?? "")));
  known.add("tubeDesignerProfileOverrides");
  known.add("tubeDesignerToolBindings");
  for (const key of declared) {
    if (typeof key !== "string" || !known.has(key))
      throw new TypeError(`产品制造参数依赖引用了未声明的字段: ${String(key)}`);
  }
  return new Set(declared);
}

export function productDisplayParameters(template, values = {}) {
  const result = { ...values };
  for (const key of manufacturingOnlyParameterKeys(template)) delete result[key];
  return result;
}

export function validateProductPostCreationChanges(template, current, next) {
  for (const key of creationOnlyParameterKeys(template)) {
    if (JSON.stringify(current[key]) !== JSON.stringify(next[key]))
      throw new Error("产品结构在创建后固定，请在添加产品时选择结构。");
  }
}

export function creationOnlyParameterKeys(template = {}) {
  const declared = template.extensions?.parameterDependencies?.creationOnly ?? [];
  if (!Array.isArray(declared)) throw new TypeError("产品创建参数依赖声明必须是数组");
  const known = new Set((template.parameters ?? []).map(field => String(field.key ?? field.name ?? "")));
  const manufacturing = manufacturingOnlyParameterKeys(template);
  for (const key of declared) {
    if (typeof key !== "string" || !known.has(key) || manufacturing.has(key))
      throw new TypeError(`产品创建参数依赖引用了无效的字段: ${String(key)}`);
  }
  return new Set(declared);
}
