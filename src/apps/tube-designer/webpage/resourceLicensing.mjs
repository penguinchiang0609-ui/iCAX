// Resource operations share the native ProductDesign/NestingEdit grants.
// These are alternatives, not new resource entitlements or page grants.
export const resourceEditLicenseFeatures = Object.freeze(["product.design", "nesting.edit"]);
const RESOURCE_EDIT = resourceEditLicenseFeatures;
const RESOURCE_EXPORT = Object.freeze(["product.export", "nesting.export"]);
const RESOURCE_PAGE = Object.freeze(["page.product", "page.nesting", "page.machining"]);

const resourceNavigationCommands = new Set([
  "resources.products", "resources.profiles", "resources.tools", "resources.assemblies",
]);

const readOnlyResourceCommands = new Set([
  "designer.templates.manage", "tools.refresh",
]);

const readOnlyResourceActions = new Set([
  "tube-designer-template-manager-open", "tube-designer-template-manager-close",
  "tube-designer-template-select",
  "tube-designer-product-template-library-toggle-category",
  "tube-designer-product-template-library-scope",
  "tube-designer-product-template-library-select",
  "tube-designer-product-template-library-search",
  "tube-designer-profile-library-toggle-category",
  "tube-designer-profile-library-scope", "tube-designer-profile-library-type",
  "tube-designer-profile-library-search", "tube-designer-profile-library-select",
  "tube-designer-profile-library-toggle-diagram", "tube-designer-profile-library-close-diagram",
  "tube-designer-profile-package-import-cancel",
  "tube-designer-tool-library-scope", "tube-designer-tool-library-type",
  "tube-designer-tool-library-category", "tube-designer-tool-library-toggle-category",
  "tube-designer-tool-library-search", "tube-designer-tool-library-select",
  "tube-designer-tool-library-toggle-main-tube",
  "tube-designer-tool-library-toggle-diagram", "tube-designer-tool-library-close-diagram",
  "tube-designer-assembly-retry", "tube-designer-assembly-search",
  "tube-designer-assembly-select", "tube-designer-assembly-toggle-shape",
  "tube-designer-assembly-toggle-category", "tube-designer-assembly-toggle-diagram",
  "tube-designer-assembly-close-diagram", "tube-designer-assembly-diagram-mode",
  "tube-designer-assembly-camera", "tube-designer-assembly-set-view",
  "tube-designer-stock-plan-reload", "tube-designer-stock-plan-load",
  "tube-designer-component-refresh", "tube-designer-component-select",
  "tube-designer-component-scope", "tube-designer-component-search",
  "tube-designer-component-toggle-category", "tube-designer-component-cancel-csg",
  "tube-designer-component-cancel-import", "tube-designer-component-cancel-delete",
  "tube-designer-component-csg-select", "tube-designer-component-csg-camera",
  "tube-designer-component-csg-fit",
]);

const resourceActionPrefixes = [
  "tube-designer-product-template-library-", "tube-designer-template-",
  "tube-designer-profile-", "tube-designer-tool-library-",
  "tube-designer-assembly-", "tube-designer-finished-",
  "tube-designer-stock-", "tube-designer-component-",
];
const resourceAreas = new Set(["resources", "templates", "profiles", "tools", "assemblies", "components"]);

export function resourceCommandLicenseFeature(commandId) {
  const id = String(commandId ?? "");
  if (resourceNavigationCommands.has(id)) return RESOURCE_PAGE;
  if (readOnlyResourceCommands.has(id)) return RESOURCE_PAGE;
  if (id.startsWith("designer.templates.")) {
    return id === "designer.templates.export" ? "product.export" : "product.design";
  }
  if (/^(profiles|tools|assemblies|components|resources)\./.test(id)) {
    return /(?:^|[.-])export(?:[.-]|$)/.test(id) ? RESOURCE_EXPORT : RESOURCE_EDIT;
  }
  return undefined;
}

export function resourceActionLicenseFeature(action, areaId) {
  const id = String(action ?? "");
  // These names belong to product instance configuration, rather than the
  // pipe-profile library. Let the product policy require ProductDesign.
  if (id === "tube-designer-profile-selection-change" || id === "tube-designer-profile-parameter-change") return undefined;
  if (id === "tube-designer-assembly-select-connection") return "";
  if (readOnlyResourceActions.has(id)) return "";
  if (id === "tube-designer-stock-plan-save" || id === "tube-designer-stock-plan-new") return "nesting.edit";
  if (id.startsWith("tube-designer-template-")) {
    return id === "tube-designer-template-export" ? "product.export" : "product.design";
  }
  if (resourceActionPrefixes.some(prefix => id.startsWith(prefix))) {
    return /(?:^|-)export(?:-|$)/.test(id) ? RESOURCE_EXPORT : RESOURCE_EDIT;
  }
  // Resource editors sometimes expose generic product draft controls. An
  // unrecognised business action in such an editor must remain protected.
  if (resourceAreas.has(areaId) && id.startsWith("tube-designer-")) return RESOURCE_EDIT;
  return undefined;
}
