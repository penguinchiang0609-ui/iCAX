import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  resourceActionLicenseFeature, resourceCommandLicenseFeature,
} from "../../apps/tube-designer/webpage/resourceLicensing.mjs";
import { renderProfileLibraryViewportOverlay } from "../../apps/tube-designer/webpage/profileLibrary.mjs";
import { renderProductTemplateLibraryViewportOverlay } from "../../apps/tube-designer/webpage/templateLibrary.mjs";
import { renderToolLibraryViewportOverlay } from "../../apps/tube-designer/webpage/toolLibrary.mjs";
import { assemblyLibraryState, ensureAssemblyLibraryPreview, handleAssemblyLibraryAction } from "../../apps/tube-designer/webpage/assemblyLibrary.mjs";
import { normalizeAssemblyCatalogue } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";
import { componentLibraryState, ensureComponentModelPreview } from "../../apps/tube-designer/webpage/componentLibrary.mjs";

const edit = ["product.design", "nesting.edit"];
const exportFeatures = ["product.export", "nesting.export"];
const pages = ["page.product", "page.nesting", "page.machining"];
const featureIds = new Set(JSON.parse(readFileSync(new URL("../../licensing/features.json", import.meta.url))).features.map(item => item.id));

function checkExpressions(lookup, groups) {
  for (const [expected, ids, area] of groups) {
    for (const id of ids) {
      const actual = lookup(id, area);
      assert.deepEqual(actual, expected, `${id} (${area ?? "ribbon"})`);
      for (const feature of Array.isArray(actual) ? actual : actual ? [actual] : []) {
        assert.ok(featureIds.has(feature), `${id} references unknown native feature ${feature}`);
      }
    }
  }
}

checkExpressions(resourceCommandLicenseFeature, [
  [pages, ["resources.products", "resources.profiles", "resources.tools", "resources.assemblies", "designer.templates.manage", "tools.refresh"]],
  ["product.design", ["designer.templates.new", "designer.templates.import", "designer.templates.delete"]],
  ["product.export", ["designer.templates.export"]],
  [edit, ["profiles.new-sketch", "profiles.import-package", "profiles.import-dxf", "tools.new-sketch", "tools.import-dxf", "tools.import-package", "assemblies.import-package", "components.import", "components.draw"]],
  [exportFeatures, ["profiles.export-dxf", "profiles.export-step", "components.export-step"]],
]);

checkExpressions(resourceActionLicenseFeature, [
  ["", ["tube-designer-template-manager-open", "tube-designer-template-manager-close", "tube-designer-template-select"], "templates"],
  ["product.design", ["tube-designer-template-create-open", "tube-designer-template-create-confirm", "tube-designer-template-import", "tube-designer-template-delete"], "templates"],
  ["product.export", ["tube-designer-template-export"], "templates"],
  ["", ["tube-designer-product-template-library-toggle-category", "tube-designer-product-template-library-scope", "tube-designer-product-template-library-select", "tube-designer-product-template-library-search"], "templates"],
  [edit, ["tube-designer-product-template-library-parameter-change", "tube-designer-product-template-library-retry-preview"], "templates"],
  ["", ["tube-designer-profile-library-toggle-category", "tube-designer-profile-library-scope", "tube-designer-profile-library-type", "tube-designer-profile-library-search", "tube-designer-profile-library-select", "tube-designer-profile-library-toggle-diagram", "tube-designer-profile-library-close-diagram", "tube-designer-profile-package-import-cancel"], "profiles"],
  [edit, ["tube-designer-profile-package-import-confirm", "tube-designer-profile-library-save", "tube-designer-profile-library-delete", "tube-designer-profile-library-edit-sketch", "tube-designer-profile-preview-change", "tube-designer-profile-regenerate"], "profiles"],
  [exportFeatures, ["tube-designer-profile-export-dxf", "tube-designer-profile-export-step"], "profiles"],
  ["", ["tube-designer-tool-library-scope", "tube-designer-tool-library-type", "tube-designer-tool-library-category", "tube-designer-tool-library-toggle-category", "tube-designer-tool-library-search", "tube-designer-tool-library-select", "tube-designer-tool-library-toggle-main-tube", "tube-designer-tool-library-toggle-diagram", "tube-designer-tool-library-close-diagram"], "tools"],
  [edit, ["tube-designer-tool-library-use-recommended-profiles", "tube-designer-tool-library-profile-change", "tube-designer-tool-library-profile-parameter-change", "tube-designer-tool-library-parameter-change", "tube-designer-tool-library-operation-parameter-change", "tube-designer-tool-library-retry-preview"], "tools"],
  ["", ["tube-designer-assembly-retry", "tube-designer-assembly-search", "tube-designer-assembly-select", "tube-designer-assembly-toggle-shape", "tube-designer-assembly-toggle-category", "tube-designer-assembly-toggle-diagram", "tube-designer-assembly-close-diagram", "tube-designer-assembly-diagram-mode", "tube-designer-assembly-camera", "tube-designer-assembly-set-view", "tube-designer-stock-plan-reload", "tube-designer-stock-plan-load"], "assemblies"],
  [edit, ["tube-designer-assembly-local-anchor-change", "tube-designer-assembly-input-mode", "tube-designer-assembly-load-example", "tube-designer-assembly-retry-preview", "tube-designer-assembly-scene-profile-change", "tube-designer-assembly-scene-length-change", "tube-designer-assembly-scene-profile-parameter-change", "tube-designer-assembly-parameter-change", "tube-designer-assembly-process-parameter-change", "tube-designer-assembly-reset", "tube-designer-finished-length-change", "tube-designer-finished-profile-change", "tube-designer-finished-shape-change", "tube-designer-finished-parameter-change", "tube-designer-finished-profile-parameter-change", "tube-designer-finished-reset", "tube-designer-stock-profile", "tube-designer-stock-length"], "assemblies"],
  ["nesting.edit", ["tube-designer-stock-plan-new", "tube-designer-stock-plan-save"], "assemblies"],
  ["", ["tube-designer-component-refresh", "tube-designer-component-select", "tube-designer-component-scope", "tube-designer-component-search", "tube-designer-component-toggle-category", "tube-designer-component-cancel-csg", "tube-designer-component-cancel-import", "tube-designer-component-cancel-delete", "tube-designer-component-csg-select", "tube-designer-component-csg-camera", "tube-designer-component-csg-fit"], "components"],
  [edit, ["tube-designer-component-import", "tube-designer-component-draw", "tube-designer-component-edit-csg", "tube-designer-component-csg-add", "tube-designer-component-csg-profile-select", "tube-designer-component-csg-profile-parameter-change", "tube-designer-component-csg-import-profile-dxf", "tube-designer-component-csg-new-profile-sketch", "tube-designer-component-csg-tool", "tube-designer-component-csg-delete", "tube-designer-component-csg-operation", "tube-designer-component-csg-move", "tube-designer-component-csg-model-change", "tube-designer-component-csg-feature-change", "tube-designer-component-save-csg", "tube-designer-component-draft", "tube-designer-component-import-draft", "tube-designer-component-confirm-import", "tube-designer-component-save", "tube-designer-component-delete", "tube-designer-component-confirm-delete", "tube-designer-component-retry-preview"], "components"],
]);

// This registry is also used by operations embedded in the product page.
assert.deepEqual(resourceActionLicenseFeature("tube-designer-component-save", "view"), edit);
assert.equal(resourceCommandLicenseFeature("designer.add"), undefined);
assert.equal(resourceActionLicenseFeature("tube-designer-parameter-change", "view"), undefined);
assert.equal(resourceActionLicenseFeature("tube-designer-profile-selection-change", "view"), undefined);
assert.equal(resourceActionLicenseFeature("tube-designer-profile-parameter-change", "view"), undefined);
assert.equal(resourceActionLicenseFeature("tube-designer-assembly-select-connection", "view"), "");
assert.equal(resourceActionLicenseFeature("view-standard", "profiles"), undefined);
for (const prefix of ["profiles", "tools", "assemblies", "components", "resources"]) {
  assert.deepEqual(resourceCommandLicenseFeature(`${prefix}.future-update`), edit);
}
for (const prefix of ["profile", "tool-library", "assembly", "finished", "stock", "component"]) {
  assert.deepEqual(resourceActionLicenseFeature(`tube-designer-${prefix}-future-update`, "about"), edit);
}
for (const area of ["resources", "templates", "profiles", "tools", "assemblies", "components"]) {
  assert.deepEqual(resourceActionLicenseFeature("tube-designer-new-business-action", area), edit);
}

// Check the operation names against the current handlers, including the
// component library's dynamically prefixed action names. A typo must not
// silently turn an intended browsing exception into a protected operation.
const source = name => readFileSync(new URL(`../../apps/tube-designer/webpage/${name}.mjs`, import.meta.url), "utf8");
const actions = new Set();
for (const name of ["profileLibrary", "toolLibrary", "assemblyLibrary", "templateLibrary", "finishedProductEditor"]) {
  for (const match of source(name).matchAll(/["'](tube-designer-[a-z0-9-]+)["']/g)) actions.add(match[1]);
}
for (const match of source("componentLibrary").matchAll(/suffix === ["']([a-z0-9-]+)["']/g)) actions.add(`tube-designer-component-${match[1]}`);
for (const action of actions) {
  if (!/^tube-designer-(?:product-template-library|profile|tool-library|assembly|finished|stock|component)-/.test(action)) continue;
  assert.notEqual(resourceActionLicenseFeature(action, "resources"), undefined, `${action} has no resource licensing decision`);
}

// Verify the shared alternatives against the actual native registration, so
// a NestingEdit-only certificate does not lose access in the resource UI.
const native = readFileSync(new URL("../../iCAX-Plugins/product/TubeDesigner/TubeDesignerSDO.cpp", import.meta.url), "utf8");
for (const method of ["ImportProfilePackage", "GenerateProfilePreview", "SavePunchTool", "ImportAssemblyTemplatePackage", "UpdateComponentModel", "GenerateProductTemplatePreview"]) {
  const line = native.split(/\r?\n/).find(text => text.includes(`ExposeMethod("${method}",`));
  assert.match(line ?? "", /ProtectAnyMethod<\d+, tube::license::Feature::ProductDesign, tube::license::Feature::NestingEdit>/, method);
}
for (const method of ["ExportProfile", "ExportComponentModel"]) {
  const line = native.split(/\r?\n/).find(text => text.includes(`ExposeMethod("${method}",`));
  assert.match(line ?? "", /ProtectAnyMethod<\d+, tube::license::Feature::ProductExport, tube::license::Feature::NestingExport>/, method);
}

// Normal resource rendering must not start protected computation. Each case
// also runs with both native alternative grants, proving that the fixture
// reaches the generation path rather than returning for missing catalog data.
const round = {
  id: "round", name: "系统圆管", profileType: "profile-package", profileForm: "parametric",
  descriptor: { id: "round", version: "1.0.0", parameters: [] }, defaultParameters: {},
  previewProfile: { schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "profile-package", sectionKind: "round", width: 40, depth: 40, contours: [{ kind: "circle", radius: 20 }] },
};
const bend = normalizeAssemblyCatalogue([JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/bend/assembly.json", import.meta.url), "utf8"))])[0];
const previewCases = [
  { name: "profile", method: "TubeDesigner.GenerateProfilePreview", area: "profiles",
    data: { tubeDesignerSystemProfiles: [round] }, run: renderProfileLibraryViewportOverlay },
  { name: "template", method: "TubeDesigner.GenerateProductTemplatePreview", area: "templates",
    data: { scene: { tubeDesigner: { templates: [{ id: "test-template", name: "测试模板", available: true, parameters: [] }] } } }, run: renderProductTemplateLibraryViewportOverlay },
  { name: "tool", method: "TubeDesigner.CheckPunchToolApplicability", area: "tools",
    data: { tubeDesignerSystemProfiles: [round], tubeDesignerSystemPunchTools: [{ id: "hole", displayName: "孔型", kind: "programmatic", target: "side", category: "孔型", parameters: [] }] }, run: renderToolLibraryViewportOverlay },
  { name: "assembly", method: "TubeDesigner.ResolveAssemblyTemplatePreview", area: "assemblies",
    data: { tubeDesignerAssemblyTemplates: [bend], tubeDesignerAssemblyLibrary: { selectedId: bend.id } }, run: ensureAssemblyLibraryPreview },
  { name: "component", method: "TubeDesigner.GenerateComponentModelPreview", area: "components",
    data: { tubeDesignerComponentLibrary: { models: [{ id: "cap", scope: "system", name: "柱帽", revision: 1 }], scope: "system", selectedKey: "system:cap" } }, run: ensureComponentModelPreview },
];
const tick = () => new Promise(resolve => setImmediate(resolve));
function previewFixture(item, status) {
  const calls = [], logs = [];
  const view = { ...structuredClone(item.data), activeAreaId: item.area, tubeDesignerLicense: status,
    viewport: { async applyViewSnapshot({ rows }) { return { applied: true, entityIds: rows.map(row => row.entityId) }; }, getAppliedViewState() { return {}; }, setVisibleEntityIds() {}, setSelectedObjectIds() {}, setDimensionAnnotations() {}, fitViewForRevision() {}, setStandardView() {}, fitViewToViewport() {} },
  };
  const context = { actions: { log(...args) { logs.push(args); } }, sceneProxy: { resources: { get() {} }, async invoke(method) {
    calls.push(method);
    return { geometryResourceId: "resource://preview", geometryResourceVersion: 1 };
  } } };
  if (item.area === "components") componentLibraryState(view).loadState = "loaded";
  return { view, context, calls, logs };
}
for (const item of previewCases) {
  for (const status of [null, { capabilities: {} }, { capabilities: { "page.product": true, "page.nesting": true } }, { capabilities: { "product.export": true, "nesting.export": true } }]) {
    const fixture = previewFixture(item, status);
    item.run(fixture.context, fixture.view);
    await tick();
    assert.deepEqual(fixture.calls, [], `${item.name} must not generate while browsing without an edit grant`);
    assert.deepEqual(fixture.logs, [], `${item.name} must not log an unrequested licensing denial`);
    assert.equal(fixture.view.error ?? "", "", `${item.name} must not install a licensing error in the normal page`);
  }
  for (const feature of edit) {
    const fixture = previewFixture(item, { capabilities: { [feature]: true } });
    item.run(fixture.context, fixture.view);
    await tick();
    assert.ok(fixture.calls.includes(item.method), `${item.name} accepts the native ${feature} alternative`);
  }
}
// Profile generation is queued. Losing the grant before its queued paint
// runs must cancel the request without treating the repaint as an operation.
const queuedProfile = previewFixture(previewCases[0], { capabilities: { "product.design": true } });
renderProfileLibraryViewportOverlay(queuedProfile.context, queuedProfile.view);
queuedProfile.view.tubeDesignerLicense = { capabilities: {} };
await tick();
assert.deepEqual(queuedProfile.calls, []);
assert.deepEqual(queuedProfile.logs, []);

const browsingAssembly = previewFixture(previewCases[3], { capabilities: { "page.product": true } });
const secondBend = { ...structuredClone(bend), id: "second-bend" };
browsingAssembly.view.tubeDesignerAssemblyTemplates.push(secondBend);
await handleAssemblyLibraryAction(browsingAssembly.context, browsingAssembly.view, "tube-designer-assembly-select",
  { dataset: { tubeAssemblyId: secondBend.id } }, { renderProject() {} });
assert.equal(assemblyLibraryState(browsingAssembly.view).selectedId, secondBend.id);
assert.deepEqual(browsingAssembly.calls, [], "template selection only changes the descriptor without an edit grant");

console.log("TubeDesigner resource licensing tests passed");
