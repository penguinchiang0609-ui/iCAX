import { matchesParameterCondition } from "./parameterConditions.mjs";

function usableNumber(value) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0) return null;
  // Section analysis may expose harmless floating-point noise such as
  // 1.9999999999999998.  An editable field should show the engineering value.
  return Number(number.toFixed(6));
}

/**
 * Applies descriptor-declared values that become known when another parameter
 * is changed. Conditions deliberately use the shared parameter condition
 * evaluator so resource, product and embedded editors have identical rules.
 */
export function parameterAutoFillPatch(definitions, currentValues = {}, changedKey = "", changedValue, sourceValues = {}) {
  const values = { ...currentValues, [changedKey]: changedValue };
  const patch = { [changedKey]: changedValue };
  for (const definition of definitions ?? []) {
    const rule = definition?.autoFill;
    if (!rule || (rule.triggerParameter && rule.triggerParameter !== changedKey)) continue;
    if (!matchesParameterCondition(rule.when, values)) continue;
    if (rule.replaceWhen && !matchesParameterCondition(rule.replaceWhen, values)) continue;
    const sourceKey = String(rule.sourceParameter ?? "").trim();
    if (!sourceKey) continue;
    const source = usableNumber(sourceValues?.[sourceKey]) ?? usableNumber(values[sourceKey]);
    if (source == null) continue;
    patch[definition.key] = source;
    values[definition.key] = source;
  }
  return patch;
}
