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
  if (condition.op === "in") return (condition.values ?? []).some(value => Object.is(values[key], value));
  if (condition.op === "notIn") return !(condition.values ?? []).some(value => Object.is(values[key], value));
  return false;
}

export function parameterVisible(definition, values = {}) {
  return definition?.presentation?.visible !== false
    && matchesParameterCondition(definition?.visibleWhen, values);
}

export function parameterEnabled(definition, values) {
  return !definition?.readOnly && matchesParameterCondition(definition?.enabledWhen, values);
}

export function availableParameterChoices(definition, values = {}) {
  const conditions = definition?.presentation?.choiceConditions ?? {};
  return (definition?.options ?? definition?.choices ?? []).filter(choice =>
    matchesParameterCondition(conditions[String(choice?.value ?? choice)], values));
}

export function effectiveParameterChoice(definition, value, values = {}) {
  if (!definition?.presentation?.choiceConditions) return value;
  const choices = availableParameterChoices(definition, values);
  const has = v => choices.some(choice => String(choice?.value ?? choice) === String(v));
  if (has(value)) return value;
  const fallback = definition?.presentation?.unavailableChoiceFallback ?? definition?.defaultValue;
  return has(fallback) ? fallback : choices[0]?.value ?? choices[0];
}
