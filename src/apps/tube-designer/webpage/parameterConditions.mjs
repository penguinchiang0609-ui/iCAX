// Shared by catalogue, embedded editors and diagrams. Both descriptor and
// native presentation spellings are supported; unknown conditions fail closed.
export function matchesParameterCondition(condition, values = {}) {
  if (condition == null || Object.keys(condition).length === 0) return true;
  const all = condition.op === "all" ? (condition.conditions ?? condition.all) : condition.all;
  const any = condition.op === "any" ? (condition.conditions ?? condition.any) : condition.any;
  if (Array.isArray(all)) return all.every(item => matchesParameterCondition(item, values));
  if (Array.isArray(any)) return any.some(item => matchesParameterCondition(item, values));
  const not = condition.op === "not" ? (condition.condition ?? condition.not) : condition.not;
  if (not) return !matchesParameterCondition(not, values);
  const key = condition.parameter ?? condition.key ?? condition.name;
  if (!key || !Object.hasOwn(values, key)) return false;
  if (condition.op === "eq" || (!condition.op && condition.name)) return Object.is(values[key], condition.value);
  if (condition.op === "ne") return !Object.is(values[key], condition.value);
  return false;
}

export function parameterEnabled(definition, values) {
  return !definition?.readOnly && matchesParameterCondition(definition?.enabledWhen, values);
}
