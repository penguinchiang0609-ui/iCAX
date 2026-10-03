// Only explicitly declared fields may change the manufacturing result while
// keeping the committed display model. Profile dimensions are display inputs
// unless the template deliberately declares a frozen-display editing policy.
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
