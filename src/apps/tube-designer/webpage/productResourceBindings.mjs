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
  templateProfilesForProduct,
} from "./profileLibrary.mjs";

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
  const aliases = [key, key.replace(/^saved:/, "user:"), key.replace(/^user:/, "saved:")];
  const specific = byResource && typeof byResource === "object" && !Array.isArray(byResource)
    ? aliases.map((alias) => byResource[alias]).find((value) => value && typeof value === "object" && !Array.isArray(value))
    : null;
  return {
    ...(common && typeof common === "object" && !Array.isArray(common) ? common : {}),
    ...(specific && typeof specific === "object" && !Array.isArray(specific) ? specific : {}),
  };
}

export function profileAllowedForProductField(profile, field) {
  const presentation = field?.presentation ?? {};
  const scopes = Array.isArray(presentation.allowedScopes)
    ? new Set(strings(presentation.allowedScopes)) : null;
  const forms = Array.isArray(presentation.allowedForms)
    ? new Set(strings(presentation.allowedForms)) : null;
  const ids = Array.isArray(presentation.allowedResourceIds)
    ? new Set(strings(presentation.allowedResourceIds)) : null;
  const constraints = presentation.profileConstraints ?? {};
  const sectionKinds = new Set(strings(constraints.sectionKinds));
  const scope = profileScope(profile);
  const form = String(profile?.profileForm ?? profile?.descriptor?.profileForm ?? profile?.previewProfile?.profileForm ?? "");
  const snapshot = profileSnapshot(profile) ?? {};
  const contourCount = Number(snapshot?.contourCount ?? snapshot?.contours?.length ?? 0);
  const sectionKind = String(snapshot?.sectionKind ?? snapshot?.kind ?? "arbitrary");
  const hollow = Boolean(snapshot?.hollow ?? contourCount > 1);
  return (!scopes || scopes.has(scope)) && (!forms || forms.has(form))
    && (!ids || ids.has(String(profile?.id ?? "")))
    && (!sectionKinds.size || sectionKinds.has(sectionKind))
    && (constraints.hollow == null || Boolean(constraints.hollow) === hollow)
    && (constraints.minimumContourCount == null || contourCount >= Number(constraints.minimumContourCount))
    && (constraints.maximumContourCount == null || contourCount <= Number(constraints.maximumContourCount));
}

export function productProfileCandidates(view, template, field) {
  return [
    ...libraryProfiles(view),
    ...templateProfilesForProduct(view, template?.id),
  ].filter((profile) => profile?.available !== false && profileAllowedForProductField(profile, field));
}

export function findProductProfile(view, template, field, selectionKey) {
  return productProfileCandidates(view, template, field)
    .find((profile) => profileSelectionKey(profile) === String(selectionKey ?? "")) ?? null;
}

export function isProductToolField(field) {
  return field?.presentation?.editor === "tool-library";
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
    return !ids.size || ids.has(String(tool?.id ?? ""));
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
  return tool ? toolDisplayName(tool) : String(binding?.snapshot?.displayName ?? binding?.ref?.id ?? "未选择模具");
}
