import {
  libraryTools,
  toolCategory,
  toolDisplayName,
  toolReference,
  toolScope,
  toolSelectionKey,
} from "./toolLibrary.mjs";
import {
  libraryProfiles,
  profileScope,
  profileSelectionKey,
  profileSnapshot,
} from "./profileLibrary.mjs";
import { catalogText } from "./productCatalog.mjs";

export const PRODUCT_RESOURCE_BINDING_SCHEMA = "icax.product-resource-binding";
export const PRODUCT_RESOURCE_BINDING_VERSION = 1;

function strings(value, fallback = []) {
  return (Array.isArray(value) ? value : fallback)
    .map((item) => String(item ?? "").trim()).filter(Boolean);
}

export function isProductProfileField(field) {
  const key = String(field?.key ?? field?.name ?? "");
  return field?.presentation?.editor === "profile-library"
    || (field?.type === "select" && key.endsWith("ProfileType"));
}

export function productProfileRole(field) {
  const key = String(field?.key ?? field?.name ?? "");
  return String(field?.presentation?.resourceRole
    ?? (key.endsWith("ProfileType") ? key.slice(0, -"ProfileType".length) : key)).trim();
}

export function productProfileRoleDeclaration(template, field) {
  const profiles = template?.extensions?.resourceRoles?.profiles;
  const declaration = profiles && typeof profiles === "object" && !Array.isArray(profiles)
    ? profiles[productProfileRole(field)] : null;
  return declaration && typeof declaration === "object" && !Array.isArray(declaration)
    ? declaration : {};
}

/**
 * A product template may tune the initial values of a referenced resource,
 * without copying its parameter schema or its diagram into the product.
 * Resource-specific defaults win over role-wide defaults.
 */
export function productResourceParameterDefaults(declaration, selectionKey = "") {
  const common = declaration?.defaultParameters;
  const byResource = declaration?.defaultParametersByResource;
  const key = String(selectionKey ?? "");
  const specific = byResource && typeof byResource === "object" && !Array.isArray(byResource)
    && byResource[key] && typeof byResource[key] === "object" && !Array.isArray(byResource[key])
    ? byResource[key]
    : null;
  return {
    ...(common && typeof common === "object" && !Array.isArray(common) ? common : {}),
    ...(specific && typeof specific === "object" && !Array.isArray(specific) ? specific : {}),
  };
}

export function productProfileConstraintError(snapshot, field) {
  const constraints = field?.presentation?.profileConstraints ?? {};
  const sectionKinds = strings(constraints.sectionKinds);
  if (!sectionKinds.length) return "此构件尚未声明支持的截面类型。";
  if (!snapshot || snapshot.schema !== "icax.imported-tube-profile"
      || snapshot.schemaVersion !== 1
      || !["fixed-section", "profile-package"].includes(snapshot.kind)) {
    return "管型没有返回有效的截面快照。";
  }
  if (!sectionKinds.includes(snapshot.sectionKind)) return "此管型不适用于当前构件，请选择模板支持的截面类型。";
  const contours = snapshot.contours;
  if (!Array.isArray(contours) || !contours.length || contours.length > 1000) return "管型缺少可用截面。";
  const count = contours.length;
  if (Object.hasOwn(snapshot, "contourCount") && snapshot.contourCount !== count) return "管型轮廓数量与截面不一致。";
  const hollow = count > 1;
  if (Object.hasOwn(snapshot, "hollow") && snapshot.hollow !== hollow) return "管型空心标记与截面不一致。";
  if (constraints.hollow != null && constraints.hollow !== hollow) return "此构件要求的空心或实心截面与所选管型不一致。";
  if (constraints.minimumContourCount != null && count < constraints.minimumContourCount) return "此管型轮廓数量少于构件要求。";
  if (constraints.maximumContourCount != null && count > constraints.maximumContourCount) return "此管型轮廓数量超过构件要求。";
  return "";
}

export function builtinProfileAllowedForProductField(value, field) {
  const choices = Array.isArray(field?.options) ? field.options : field?.choices ?? [];
  return choices.some((choice) => String(typeof choice === "object" ? choice?.value : choice) === String(value));
}

export function profileAllowedForProductField(profile, field) {
  const presentation = field?.presentation ?? {};
  const scopes = Array.isArray(presentation.allowedScopes)
    ? new Set(strings(presentation.allowedScopes)) : null;
  const forms = Array.isArray(presentation.allowedForms)
    ? new Set(strings(presentation.allowedForms)) : null;
  const ids = Array.isArray(presentation.allowedResourceIds)
    ? new Set(strings(presentation.allowedResourceIds)) : null;
  const scope = profileScope(profile);
  const form = String(profile?.profileForm ?? profile?.descriptor?.profileForm ?? profile?.previewProfile?.profileForm ?? "");
  const snapshot = profileSnapshot(profile) ?? {};
  return ["system", "user"].includes(scope) && (!scopes || scopes.has(scope)) && (!forms || forms.has(form))
    && (!ids || ids.has(String(profile?.id ?? "")))
    && !productProfileConstraintError(snapshot, field);
}

export function productProfileCandidates(view, template, field) {
  return libraryProfiles(view).filter((profile) => profile?.available !== false && profileAllowedForProductField(profile, field));
}

// Product declarations own the editor schema; a resource supplies geometry.
export function productProfileParameterBindings(template, field, profile) {
  const declaration = productProfileRoleDeclaration(template, field);
  const kind = profile?.sectionKind ?? "";
  const bindings = declaration.parameterBindingsBySectionKind?.[kind] ?? {};
  const fields = new Map((template?.parameters ?? []).map(item => [item.key ?? item.name, item]));
  const resourceFields = new Set((profile?.parameterDefinitions ?? []).map(item => item.key));
  return Object.entries(bindings).flatMap(([resourceParameter, productParameter]) => {
    const productField = fields.get(productParameter);
    return productField && resourceFields.has(resourceParameter)
      ? [{ resourceParameter, productParameter, field: productField,
        label: catalogText(declaration.parameterLabelsBySectionKind?.[kind]?.[resourceParameter]) }] : [];
  });
}

export function productProfileDefaultSelection(template, field, values) {
  const kind = values?.[field?.key ?? field?.name];
  return productProfileRoleDeclaration(template, field).defaultResourcesBySectionKind?.[kind] ?? "";
}

export function findProductProfile(view, template, field, selectionKey) {
  return productProfileCandidates(view, template, field)
    .find((profile) => profileSelectionKey(profile) === String(selectionKey ?? "")) ?? null;
}

export function isProductToolField(field) {
  return ["tool-library", "product-option"].includes(field?.presentation?.editor);
}

export function productToolRole(field) {
  return String(field?.presentation?.resourceRole ?? field?.key ?? field?.name ?? "").trim();
}

export function productToolRoleDeclaration(template, field) {
  const tools = template?.extensions?.resourceRoles?.tools;
  const declaration = tools && typeof tools === "object" && !Array.isArray(tools)
    ? tools[productToolRole(field)] : null;
  return declaration && typeof declaration === "object" && !Array.isArray(declaration)
    ? declaration : {};
}

export function productUsesToolLibrary(template) {
  return (template?.parameters ?? []).some(isProductToolField);
}

/** The product owns its editable fields; a resource schema alone never opens them. */
export function productToolParameterUI(template, field, binding, tool) {
  const selectionKey = tool ? toolSelectionKey(tool) : String(binding?.selectionKey ?? "");
  const declaration = productToolRoleDeclaration(template, field);
  const policies = declaration.productParameterUIByResource;
  const policy = policies && typeof policies === "object" && !Array.isArray(policies)
    && Object.hasOwn(policies, selectionKey) ? policies[selectionKey] : null;
  const unavailable = (error) => ({ available: false, error, definitions: [], values: {}, fixedParameters: {}, displayName: "" });
  if (!tool) return unavailable("当前工艺资源不可用，请重新选择适用的工艺。");
  if (!policy || typeof policy !== "object" || Array.isArray(policy)
      || !Array.isArray(policy.fields) || !policy.fixedParameters
      || typeof policy.fixedParameters !== "object" || Array.isArray(policy.fixedParameters)) {
    return unavailable("产品模板尚未声明此工艺的适用参数。");
  }
  const source = productToolDefinitions(binding, tool);
  const byKey = new Map(source.map((definition) => [definition.key, definition]));
  const seen = new Set();
  const definitions = [];
  const combine = (base, extra) => base && extra ? { op: "all", conditions: [base, extra] } : base ?? extra;
  for (const item of policy.fields) {
    const key = String(item?.key ?? "");
    const definition = byKey.get(key);
    if (!key || !definition || definition.derived === true || seen.has(key)
        || item.level != null && !["basic", "advanced"].includes(item.level)
        || Object.hasOwn(policy.fixedParameters, key)) return unavailable("产品工艺参数声明与资源定义不一致。");
    seen.add(key);
    definitions.push({ ...definition,
      displayName: catalogText(item.displayName, catalogText(definition.displayName, key)),
      order: Number.isFinite(item.order) ? item.order : definitions.length,
      level: item.level ?? "basic",
      presentation: { ...(definition.presentation ?? {}), advanced: item.level === "advanced" },
      visibleWhen: combine(definition.visibleWhen, item.visibleWhen),
      enabledWhen: combine(definition.enabledWhen, item.enabledWhen),
    });
  }
  for (const key of Object.keys(policy.fixedParameters)) {
    const definition = byKey.get(key), value = policy.fixedParameters[key];
    if (!definition || definition.derived === true) return unavailable("产品固定工艺参数声明与资源定义不一致。");
    const type = definition.valueType;
    const options = definition.options ?? definition.choices ?? [];
    if ((["number", "integer"].includes(type) && (typeof value !== "number" || !Number.isFinite(value)
        || type === "integer" && !Number.isInteger(value)
        || definition.min != null && value < definition.min || definition.max != null && value > definition.max))
        || type === "boolean" && typeof value !== "boolean"
        || ["string", "enum"].includes(type) && typeof value !== "string"
        || options.length && !options.some((option) => Object.is(typeof option === "object" ? option.value : option, value))) {
      return unavailable("产品固定工艺参数超出资源允许范围。");
    }
  }
  definitions.sort((a, b) => a.order - b.order);
  const values = { ...Object.fromEntries(source.map((definition) => [definition.key, definition.defaultValue])),
    ...(binding?.parameters ?? { ...(tool.defaultParameters ?? {}),
      ...productResourceParameterDefaults(declaration, selectionKey), ...policy.fixedParameters }) };
  const fixedConflict = Object.entries(policy.fixedParameters).some(([key, value]) => !Object.is(values[key], value));
  return { available: true, error: fixedConflict ? "已保存工艺与产品的固定设置不一致，请重新选择工艺。" : "", fixedConflict, definitions,
    // Keep every real resource value for conditions and geometry. Fixed values
    // initialize a new binding; an existing conflicting draft is not rewritten.
    values,
    fixedParameters: { ...policy.fixedParameters },
    displayName: catalogText(policy.displayName, catalogText(field?.displayName, "工艺参数")),
  };
}

export function productToolCandidates(view, template, field) {
  const presentation = field?.presentation ?? {};
  const declaration = productToolRoleDeclaration(template, field);
  const scopes = new Set(strings(presentation.allowedScopes, ["system", "template", "user"]));
  const targets = new Set(strings(presentation.targets ?? presentation.allowedTargets
    ?? declaration.targets ?? declaration.allowedTargets));
  const categories = new Set(strings(presentation.categories ?? presentation.allowedCategories
    ?? declaration.categories ?? declaration.allowedCategories));
  const ids = new Set(strings(presentation.allowedResourceIds ?? declaration.allowedResourceIds));
  return libraryTools(view).filter((tool) => {
    const scope = toolScope(tool);
    if (!scopes.has(scope)) return false;
    if (scope === "template" && String(tool?.templateId ?? "") !== String(template?.id ?? "")) return false;
    if (targets.size && !targets.has(String(tool?.target ?? ""))) return false;
    if (categories.size && !categories.has(toolCategory(tool))) return false;
    if (ids.size && !ids.has(String(tool?.id ?? ""))) return false;
    return productToolParameterUI(template, field, null, tool).available;
  });
}

export function productToolBindings(values) {
  const bindings = values?.tubeDesignerToolBindings;
  return bindings && typeof bindings === "object" && !Array.isArray(bindings) ? bindings : {};
}

export function productToolBinding(values, field) {
  return productToolBindings(values)[productToolRole(field)] ?? null;
}

export function makeProductToolBinding(field, tool, parameters = null, template = null) {
  const definitions = Array.isArray(tool?.parameters) ? tool.parameters : [];
  const defaults = Object.fromEntries(definitions.map((item) => [item.key, item.defaultValue]));
  const declaration = productToolRoleDeclaration(template, field);
  const values = parameters && typeof parameters === "object" && !Array.isArray(parameters)
    ? parameters : {
      ...defaults,
      ...(tool?.defaultParameters ?? {}),
      ...productResourceParameterDefaults(declaration, toolSelectionKey(tool)),
      ...productToolParameterUI(template, field, null, tool).fixedParameters,
    };
  return {
    schema: PRODUCT_RESOURCE_BINDING_SCHEMA,
    schemaVersion: PRODUCT_RESOURCE_BINDING_VERSION,
    resourceKind: "punch-tool",
    role: productToolRole(field),
    selectionKey: toolSelectionKey(tool),
    ref: toolReference(tool),
    parameters: values,
    snapshot: {
      displayName: toolDisplayName(tool),
      kind: String(tool?.kind ?? "programmatic"),
      target: String(tool?.target ?? "side"),
      category: toolCategory(tool),
      targetProfileRole: String(declaration?.targetProfileRole ?? ""),
      parameterDefinitions: definitions,
    },
  };
}

export function findProductTool(view, template, field, selectionKey) {
  return productToolCandidates(view, template, field)
    .find((tool) => toolSelectionKey(tool) === String(selectionKey ?? "")) ?? null;
}

export function productToolDefinitions(binding, tool = null) {
  return Array.isArray(tool?.parameters) ? tool.parameters
    : Array.isArray(binding?.snapshot?.parameterDefinitions) ? binding.snapshot.parameterDefinitions : [];
}

export function productToolLabel(binding, tool = null) {
  return tool ? toolDisplayName(tool) : String(binding?.snapshot?.displayName ?? binding?.ref?.id ?? "未选择单件工艺");
}
