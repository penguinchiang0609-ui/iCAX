import { catalogText } from "./productCatalog.mjs";
import { manufacturingOnlyParameterKeys } from "./productParameterDependencies.mjs";
import { effectiveParameterChoice, matchesParameterCondition, parameterEnabled, parameterVisible } from "./parameterConditions.mjs";
import { findProductTool, isProductToolField, makeProductToolBinding, productToolBinding, productToolRole } from "./productResourceBindings.mjs";

function declarations(template) {
  return Array.isArray(template?.extensions?.productControls) ? template.extensions.productControls : [];
}

function declaration(template, control) {
  const key = typeof control === "string" ? control : control?.key;
  return declarations(template).find((item) => item?.key === key) ?? null;
}

export function productControlEditableAfterCreation(template, control) {
  const declared = declaration(template, control), allowed = manufacturingOnlyParameterKeys(template);
  const affected = (declared?.choices ?? []).flatMap(choice => [
    ...Object.keys(choice.parameters ?? {}),
    ...(choice.assignments ?? []).flatMap(assignment => Object.keys(assignment.parameters ?? {})),
    ...(choice.tool ? [choice.tool.parameter] : []),
  ]);
  return affected.length > 0 && affected.every(key => allowed.has(key));
}

export function productStructureEditors(template) {
  const controls = new Set(declarations(template).map(item => item.key));
  const editors = template?.extensions?.structureEditors;
  return (Array.isArray(editors) ? editors : []).filter(editor =>
    controls.has(editor?.controlKey) && Array.isArray(editor.controls)
      && editor.controls[0] === editor.controlKey
      && editor.controls.every(key => controls.has(key)));
}

export function productStructureEditor(template, control) {
  const key = typeof control === "string" ? control : control?.key;
  return productStructureEditors(template).find(editor => editor.controlKey === key) ?? null;
}

function available(choice, values) {
  return parameterVisible(choice, values) && parameterEnabled(choice, values);
}

export function productControlEffectiveValues(template, values) {
  const result = { ...values };
  for (const field of template?.parameters ?? []) {
    const key = field.key ?? field.name;
    if (isProductToolField(field)) {
      const binding = productToolBinding(values, field);
      if (binding?.selectionKey != null) result[key] = binding.selectionKey;
    }
  }
  for (const field of template?.parameters ?? []) {
    const key = field.key ?? field.name;
    if (field.presentation?.choiceConditions) result[key] = effectiveParameterChoice(field, result[key], result);
  }
  return result;
}

function toolField(template, choice) {
  return (template?.parameters ?? []).find((field) =>
    String(field?.key ?? field?.name ?? "") === choice?.tool?.parameter && isProductToolField(field)) ?? null;
}

/** UI fields are synthesized separately from the host's persisted parameters. */
export function productControlFields(template) {
  const parameterKeys = new Set((template?.parameters ?? []).map((field) => field.key ?? field.name));
  const seen = new Set();
  return declarations(template).flatMap((control) => {
    if (!control || typeof control.key !== "string" || !control.key.trim() || seen.has(control.key)
        || parameterKeys.has(control.key) || !Array.isArray(control.choices)) return [];
    seen.add(control.key);
    return [{ ...control, type: "select", valueType: "enum", groupKey: control.group,
      displayName: catalogText(control.displayName, "产品做法"),
      presentation: { ...(control.presentation ?? {}), editor: "product-control" },
      options: control.choices.map((choice) => ({ ...choice, label: catalogText(choice.displayName, "产品做法") })),
    }];
  });
}

export function productControlChoices(template, control, values = {}) {
  const declared = declaration(template, control);
  const current = productControlEffectiveValues(template, values);
  return (declared?.choices ?? []).filter((choice) => parameterVisible(choice, current))
    .map((choice) => ({ ...choice, label: catalogText(choice.displayName, "产品做法"), disabled: !parameterEnabled(choice, current) }));
}

/** Saved bindings take precedence over an older selector draft when recognizing a choice. */
export function productControlValue(template, control, values = {}) {
  const declared = declaration(template, control);
  const current = productControlEffectiveValues(template, values);
  if (!declared || !parameterVisible(declared, current) || !parameterEnabled(declared, current)) return "";
  const choice = (declared.choices ?? []).find((item) => {
    if (!available(item, current) || !matchesParameterCondition(item.matchWhen, current)) return false;
    if (!item.tool) return true;
    const field = toolField(template, item);
    if (!field) return false;
    const selected = productToolBinding(values, field)?.selectionKey ?? values[field.key ?? field.name];
    return selected === item.tool.selectionKey;
  });
  return choice?.value ?? "";
}

/** Apply a product choice only to existing host fields, retaining inactive resource drafts. */
export function applyProductControlChoice(view, template, control, values = {}, choiceValue, { reapply = false } = {}) {
  if (choiceValue === "") return values;
  const declared = declaration(template, control);
  const choice = (declared?.choices ?? []).find((item) => item?.value === choiceValue);
  const current = productControlEffectiveValues(template, values);
  if (!declared || !choice || !parameterVisible(declared, current) || !parameterEnabled(declared, current)
      || !available(choice, current)) throw new Error("当前产品做法不适用于此结构。");
  const parameterKeys = new Set((template?.parameters ?? []).map((field) => field.key ?? field.name));
  const validPatch = (patch) => patch == null || typeof patch === "object" && !Array.isArray(patch)
    && Object.keys(patch).every((key) => parameterKeys.has(key));
  const assignments = choice.assignments ?? [];
  if (parameterKeys.has(declared.key) || !validPatch(choice.parameters) || !Array.isArray(assignments)
      || assignments.some((item) => !item || !validPatch(item.parameters))) {
    throw new Error("产品做法的参数定义无效。");
  }
  const field = choice.tool ? toolField(template, choice) : null;
  if (choice.tool && (!field || typeof choice.tool.selectionKey !== "string" || !choice.tool.selectionKey)) {
    throw new Error("产品做法的转角定义无效。");
  }
  if (!reapply && productControlValue(template, declared, values) === choiceValue) return values;
  const next = structuredClone(values);
  Object.assign(next, structuredClone(choice.parameters ?? {}));
  for (const assignment of assignments) {
    if (matchesParameterCondition(assignment.when, productControlEffectiveValues(template, next))) Object.assign(next, structuredClone(assignment.parameters ?? {}));
  }
  if (choice.tool) {
    const existing = productToolBinding(next, field);
    let binding = existing;
    if (existing?.selectionKey !== choice.tool.selectionKey) {
      const tool = findProductTool(view, template, field, choice.tool.selectionKey);
      if (!tool) throw new Error("当前产品转角做法暂时不可用。");
      binding = makeProductToolBinding(field, tool, null, template);
    }
    next[field.key ?? field.name] = choice.tool.selectionKey;
    next.tubeDesignerToolBindings = { ...(next.tubeDesignerToolBindings ?? {}), [productToolRole(field)]: binding };
  }
  return JSON.stringify(next) === JSON.stringify(values) ? values : next;
}
