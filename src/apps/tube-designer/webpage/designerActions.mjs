import {
  buildPartCategories,
  applyReusablePresetValues,
  getDefaultParameters,
  getDefaultTemplate,
  getDefaultParameterPresetScopeKey,
  getParameterPresetScopeKey,
  getReusablePresetValues,
  getTemplateById,
  getTemplateDisplayName,
  renderDesignerAddDialog,
  renderDesignerAddParameterContent,
  renderDesignerRightParameterContent,
  renderDesignerRuntimeStatus,
  renderDesignerProductPartsDock,
  renderBatchExcelTemplateDialog,
  renderBatchExcelImportDialog,
  renderDesignerBreakdownBody,
  renderDesignerBreakdownRows,
} from "./designerViews.mjs";
import {
  fitDesignerInspectedPart,
  setDesignerInspectedPartView,
  toggleDesignerAutomaticDimensions,
} from "./partInspection.mjs";
import { scheduleDesignerPartThumbnailHydration } from "./partThumbnail.mjs";
import { captureScrollAnchor, restoreScrollAnchor } from "./scrollAnchor.mjs";
import {
  handleProfileLibraryAction,
  handleProfileLibraryRibbonCommand,
  libraryProfiles,
  profileName,
  profileRef,
  profileScope,
  profileSelectionKey,
  resolveSelectedProfileSketchSource,
  templateProfilesForProduct,
} from "./profileLibrary.mjs";
import { handlePartsAreaAction, listNestingParts } from "./partsArea.mjs";
import { handleNestingSettingsAction, handleNestingSettingsRibbonCommand } from "./nestingSettings.mjs";
import { handleNestingPartImportAction, handleNestingPartImportRibbonCommand } from "./nestingPartImport.mjs";
import { handleNestingStandardPartAction, handleNestingStandardPartRibbonCommand } from "./nestingStandardPart.mjs";
import { handleNestingPunchPartAction, handleNestingPunchPartRibbonCommand } from "./nestingPunchPart.mjs";
import { handlePartDrawingAction, handlePartDrawingRibbonCommand } from "./partDrawing.mjs";
import { handleNestingRibbonCommand, restoreSavedNestingTask } from "./nestingWorkflow.mjs";
import { handleNestingExportAction } from "./nestingExport.mjs";
import { handleTubeMachiningAction, handleTubeMachiningRibbonCommand } from "./machiningArea.mjs";
import {
  beginNewSectionSketch,
  beginProfileSectionSketch,
  handleSketchAreaAction,
  handleSketchRibbonCommand,
} from "./sketchArea.mjs";
import { catalogText, getCatalogEntry, getCatalogEntryGroupKeys } from "./productCatalog.mjs";
import { componentLibraryState, handleComponentLibraryAction, handleComponentLibraryRibbonCommand, refreshComponentModels } from "./componentLibrary.mjs";
import { ensureToolLibraryCatalogue, handleToolLibraryAction, handleToolLibraryRibbonCommand } from "./toolLibrary.mjs";
import {
  findProductProfile,
  findProductTool,
  isProductToolField,
  makeProductToolBinding,
  productToolBinding,
  productToolDefinitions,
  productToolRole,
  productUsesToolLibrary,
  productProfileRole,
  productProfileRoleDeclaration,
  productResourceParameterDefaults,
} from "./productResourceBindings.mjs";
import {
  productManufacturingPlanFingerprint,
  renderProductManufacturingPlan,
} from "./productManufacturingPlan.mjs";
import { handleProductTemplateLibraryAction, productTemplateLibrarySelectedTemplateId } from "./templateLibrary.mjs";
import {
  bindProductParameterDiagrams,
  bindProductSpecificationAnnotations,
} from "./productParameterDiagram.mjs";
import { matchesParameterCondition } from "./parameterConditions.mjs";
import { escapeText } from "../../_shared/workbench/utils/format.mjs";

export const DESIGNER_OPERATION_PROGRESS_MINIMUM_VISIBLE_MS = 500;
export const RESOURCE_LIBRARY_SWITCH_PROGRESS_MINIMUM_VISIBLE_MS = 500;
import { handleLicenseCommand } from "./licensing.mjs";
export const ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS = DESIGNER_OPERATION_PROGRESS_MINIMUM_VISIBLE_MS;

function sameTemplateVersion(left, right) {
  const leftVersion = String(left?.version ?? "").trim();
  const rightVersion = String(right?.version ?? "").trim();
  return !leftVersion || !rightVersion || leftVersion === rightVersion;
}

function attachCatalogueStateToDescriptor(catalogue, descriptor) {
  return {
    ...(catalogue ?? {}),
    ...(descriptor ?? {}),
    // Runtime catalogue state may disable or rename a package, but it must
    // never replace the descriptor's parameters, conditions or diagrams.
    id: catalogue?.id ?? descriptor?.id,
    version: catalogue?.version ?? descriptor?.version,
    name: catalogue?.name ?? descriptor?.name,
    available: catalogue?.available ?? descriptor?.available,
    status: catalogue?.status ?? descriptor?.status,
    descriptorLoaded: true,
  };
}

// Product cards and editors share one complete template registry. Native
// scene responses may still contain compact catalogue records for transport,
// but those records are references only: immediately replace every matching
// record with the already loaded descriptor before any UI reads the list.
export function restoreLoadedProductTemplateDescriptors(view, designer = view?.scene?.tubeDesigner) {
  if (!designer || !Array.isArray(designer.templates)) return designer;
  const registry = view.tubeDesignerTemplateDescriptors ??= {};
  designer.templates = designer.templates.map((catalogue) => {
    const id = String(catalogue?.id ?? "").trim();
    if (!id) return catalogue;
    if (Array.isArray(catalogue?.parameters)) {
      registry[id] = catalogue;
      return catalogue;
    }
    const descriptor = registry[id];
    if (!Array.isArray(descriptor?.parameters)) return catalogue;
    if (!sameTemplateVersion(catalogue, descriptor)) {
      delete registry[id];
      return catalogue;
    }
    return attachCatalogueStateToDescriptor(catalogue, descriptor);
  });
  return designer;
}

// Fetch a full descriptor only when startup has not populated the registry or
// when the catalogue reports a newer package version.
async function ensureTemplateDescriptor(context, view, templateId) {
  const id = String(templateId ?? "").trim();
  const designer = view.scene?.tubeDesigner;
  if (!id || !designer) return null;
  restoreLoadedProductTemplateDescriptors(view, designer);
  const cachedDescriptors = view.tubeDesignerTemplateDescriptors ??= {};
  const current = (designer.templates ?? []).find((item) => String(item?.id ?? "") === id);
  if (current?.available === false) return current;

  // A generation/list response intentionally contains the lightweight
  // catalogue only.  Keep the full descriptor outside that response so the
  // right-hand parameter editor never becomes empty after the next refresh.
  // `descriptorLoaded` is not itself proof of a descriptor: native product
  // responses reuse that flag for a template which is known to the catalog,
  // while deliberately omitting its parameter schema.  Treating that summary
  // as a loaded descriptor overwrote the cache after every disassembly.
  const cached = cachedDescriptors[id];
  if (Array.isArray(current?.parameters)) {
    const merged = { ...cached, ...current, descriptorLoaded: true };
    cachedDescriptors[id] = merged;
    return merged;
  }
  if (Array.isArray(cached?.parameters)) {
    const merged = attachCatalogueStateToDescriptor(current, cached);
    const templates = designer.templates ??= [];
    const index = templates.findIndex((item) => String(item?.id ?? "") === id);
    if (index >= 0) templates[index] = merged;
    else templates.push(merged);
    return merged;
  }
  const requests = view.tubeDesignerTemplateDescriptorRequests ??= {};
  if (!requests[id]) {
    requests[id] = invokeDesignerRequest(context, "TubeDesigner.GetTemplateDescriptor", {
      templateId: id,
    }).then((response) => response?.template ?? null);
  }
  try {
    const detail = await requests[id];
    if (!detail || !Array.isArray(detail.parameters)) {
      throw new Error(`模板“${current?.name ?? id}”的参数描述未能加载。`);
    }
    const merged = attachCatalogueStateToDescriptor(current, detail);
    const templates = designer.templates ??= [];
    const index = templates.findIndex((item) => String(item?.id ?? "") === id);
    if (index >= 0) templates[index] = merged;
    else templates.push(merged);
    cachedDescriptors[id] = merged;
    view.tubeDesignerTemplateLoadError = "";
    return merged;
  } finally {
    delete requests[id];
  }
}

// Complete the template discovery before the main product screen becomes
// interactive.  Each callback follows a real package request so the startup
// screen reports actual work and counts, rather than simulated progress.
export async function preloadTubeDesignerTemplates(context, view, onProgress = async () => {}) {
  const uniqueResources = (items) => {
    const seen = new Set();
    return items.filter((item) => {
      const scope = String(item?.libraryScope ?? item?.profileScope ?? item?.ownerScope ?? "system");
      const id = String(item?.id ?? item?.descriptor?.id ?? "").trim();
      const key = `${scope}:${id}`;
      if (!id || seen.has(key)) return false;
      seen.add(key);
      return true;
    });
  };
  const resourceName = (item) => {
    const value = item?.displayName ?? item?.name ?? item?.descriptor?.displayName
      ?? item?.descriptor?.name ?? item?.id ?? item?.descriptor?.id ?? "未命名资源";
    if (value && typeof value === "object") {
      return String(value["zh-CN"] ?? value.zh ?? value["en-US"] ?? value.en ?? "未命名资源");
    }
    return String(value);
  };
  const templates = (view.scene?.tubeDesigner?.templates ?? [])
    .filter((template) => template?.available !== false && String(template?.id ?? "").trim());

  // Resource selection must be ready before product parameters are opened:
  // the product descriptors directly reference both catalogues.
  await onProgress("profile", 0, null, "正在读取系统、模板和我的管型");
  await refreshDesignerUserData(context, view);
  const profiles = uniqueResources([
    ...(view.tubeDesignerSystemProfiles ?? []),
    ...(view.tubeDesignerTemplateProfiles ?? []),
    ...(view.tubeDesignerUserData?.profiles ?? []),
  ]);
  if (!profiles.length) {
    await onProgress("profile", 0, 0, "当前没有可用管型模板");
  } else {
    for (let index = 0; index < profiles.length; index += 1) {
      await onProgress("profile", index + 1, profiles.length,
        `已加载：${resourceName(profiles[index])}`);
    }
  }

  await onProgress("tool", 0, null, "正在读取系统、模板和我的模具");
  await ensureToolLibraryCatalogue(context, view, null, { skipStartupGates: true });
  const tools = uniqueResources([
    ...(view.tubeDesignerSystemPunchTools ?? []),
    ...(view.tubeDesignerTemplatePunchTools ?? []),
    ...(view.tubeDesignerUserData?.punchTools ?? []),
  ]);
  if (!tools.length) {
    await onProgress("tool", 0, 0, "当前没有可用模具模板");
  } else {
    for (let index = 0; index < tools.length; index += 1) {
      await onProgress("tool", index + 1, tools.length,
        `已加载：${resourceName(tools[index])}`);
    }
  }

  const productTotal = templates.length;
  await onProgress("product", 0, productTotal, "正在读取产品模板参数与资源声明");
  for (let index = 0; index < templates.length; index += 1) {
    const template = templates[index];
    await ensureTemplateDescriptor(context, view, template.id);
    await onProgress("product", index + 1, productTotal,
      `已加载：${resourceName(template)}`);
  }
}

export async function handleDesignerAreaAction(context, view, action, target, ops) {
  const drawingResult=await handlePartDrawingAction(context,view,action,target,ops);
  if(drawingResult.handled)return drawingResult;
  const machiningResult = await handleTubeMachiningAction(context, view, action, target, ops);
  if (machiningResult.handled) return machiningResult;
  if (action === "tube-designer-profile-library-new-sketch") {
    return { handled: true, result: await openProfileSectionSketch(context, view, ops, true) };
  }
  if (action === "tube-designer-profile-library-edit-sketch") {
    return { handled: true, result: await openProfileSectionSketch(context, view, ops, false) };
  }
  const componentResult = await handleComponentLibraryAction(context, view, action, target, ops);
  if (componentResult.handled) return componentResult;
  const toolLibraryResult = await handleToolLibraryAction(context, view, action, target, ops);
  if (toolLibraryResult.handled) return toolLibraryResult;
  const productTemplateLibraryResult = await handleProductTemplateLibraryAction(context, view, action, target, ops);
  if (productTemplateLibraryResult.handled) return productTemplateLibraryResult;
  const nestingPunchPartResult = await handleNestingPunchPartAction(context, view, action, target, ops);
  if (nestingPunchPartResult.handled) return nestingPunchPartResult;
  const nestingStandardPartResult = await handleNestingStandardPartAction(context, view, action, target, ops);
  if (nestingStandardPartResult.handled) return nestingStandardPartResult;
  const nestingPartImportResult = await handleNestingPartImportAction(context, view, action, target, ops);
  if (nestingPartImportResult.handled) return nestingPartImportResult;
  const nestingExportResult = await handleNestingExportAction(context, view, action, target, ops);
  if (nestingExportResult.handled) return nestingExportResult;
  const nestingSettingsResult = await handleNestingSettingsAction(context, view, action, target, ops);
  if (nestingSettingsResult.handled) return nestingSettingsResult;
  const sketchResult = await handleSketchAreaAction(context, view, action, target, ops);
  if (sketchResult.handled) return sketchResult;
  const partsAreaResult = await handlePartsAreaAction(context, view, action, target, ops);
  if (partsAreaResult.handled) return partsAreaResult;
  const profileLibraryResult = await handleProfileLibraryAction(
    context, view, action, target, ops,
  );
  if (profileLibraryResult.handled) return profileLibraryResult;
  if (action === "tube-designer-template-manager-open") {
    openProductTemplateManager(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-template-manager-close") {
    closeProductTemplateManager(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-template-select") {
    selectProductTemplate(view, target, ops, context);
    return { handled: true };
  }
  if (action === "tube-designer-template-create-open") {
    openProductTemplateCreate(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-template-create-confirm") {
    return { handled: true, result: await createProductTemplate(context, view, ops) };
  }
  if (action === "tube-designer-template-import") {
    return { handled: true, result: await importProductTemplate(context, view, ops) };
  }
  if (action === "tube-designer-template-export") {
    return { handled: true, result: await exportProductTemplate(context, view, ops) };
  }
  if (action === "tube-designer-template-delete") {
    return { handled: true, result: await deleteProductTemplate(context, view, ops) };
  }
  if (action === "tube-designer-open-add") {
    await openAddDialog(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-batch-add") {
    return {
      handled: true,
      result: await chooseBatchAddWorkbook(
        context,
        view,
        ops,
        String(target?.dataset?.tubeDesignerBatchPath ?? "").trim(),
      ),
    };
  }
  if (action === "tube-designer-excel-template-open") {
    return { handled: true, result: await openBatchExcelTemplateDialog(context, view, ops) };
  }
  if (action === "tube-designer-excel-template-select") {
    return { handled: true, result: await selectBatchExcelTemplate(context, view, target, ops) };
  }
  if (action === "tube-designer-excel-template-close") {
    closeBatchExcelTemplateDialog(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-excel-template-export") {
    return { handled: true, result: await exportBatchExcelTemplate(context, view, ops) };
  }
  if (action === "tube-designer-excel-import-close") {
    if (!view.pending) {
      view.tubeDesignerExcelImportDialog = null;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-excel-import-confirm") {
    return { handled: true, result: await importBatchExcelProducts(context, view, ops) };
  }
  if (action === "tube-designer-cancel-add") {
    if (!view.pending) closeAddDialog(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-select-template") {
    return { handled: true, result: await selectTemplate(context, view, target, ops) };
  }
  if (action === "tube-designer-toggle-template-group") {
    toggleTemplateGroup(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-select-template-tab") {
    selectTemplateTab(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-parameter-change") {
    return { handled: true, result: await updateParameterDraft(context, view, target, ops) };
  }
  if (action === "tube-designer-scene-specification-change") {
    return { handled: true, result: await updateSceneSpecificationDraft(context, view, target, ops) };
  }
  if (action === "tube-designer-toggle-specification-annotations") {
    const visible = view.tubeDesignerSpecificationAnnotationsVisible === false;
    view.tubeDesignerSpecificationAnnotationsVisible = visible;
    target?.setAttribute?.("aria-pressed", String(visible));
    target?.classList?.toggle?.("is-active", visible);
    bindProductSpecificationAnnotations(context.mount, view);
    return { handled: true, result: visible };
  }
  if (action === "tube-designer-product-tool-selection-change") {
    return { handled: true, result: await changeProductToolSelection(context, view, target, ops) };
  }
  if (action === "tube-designer-product-tool-parameter-change") {
    return { handled: true, result: await changeProductToolParameter(context, view, target, ops) };
  }
  if (action === "tube-designer-select-product-editor-stage") {
    return { handled: true, result: await selectProductEditorStage(context, view, target) };
  }
  if (action === "tube-designer-select-parameter-category") {
    selectParameterCategory(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-refresh-manufacturing-plan") {
    return { handled: true, result: await refreshProductManufacturingPlan(context, view, target) };
  }
  if (action === "tube-designer-toggle-product-parts-dock") {
    view.tubeDesignerProductPartsDockCollapsed = !view.tubeDesignerProductPartsDockCollapsed;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-export-active-product-parts") {
    return { handled: true, result: await exportActiveProductParts(context, view, ops) };
  }
  if (action === "tube-designer-select-product-part") {
    selectProductPartInScene(context, view, target);
    return { handled: true };
  }
  if (action === "tube-designer-reload-product-parameters") {
    return { handled: true, result: await reloadProductParameters(context, view, ops) };
  }
  if (action === "tube-designer-disassemble-active-product") {
    return { handled: true, result: await disassembleActiveProduct(context, view, ops) };
  }
  if (action === "tube-designer-focus-product-parameter") {
    focusProductParameter(context, view, target);
    return { handled: true };
  }
  if (action === "tube-designer-edit-scene-specification") {
    editSceneSpecification(context, view, target);
    return { handled: true };
  }
  if (action === "tube-designer-instance-quantity-change") {
    return { handled: true, result: await changeInstanceQuantity(context, view, target, ops) };
  }
  if (action === "tube-designer-profile-selection-change") {
    return { handled: true, result: await changeProfileSelection(context, view, target, ops) };
  }
  if (action === "tube-designer-profile-parameter-change") {
    return { handled: true, result: await updateParametricProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-import-profile-dxf") {
    return { handled: true, result: await importProfileDxf(context, view, target, ops) };
  }
  if (action === "tube-designer-clear-imported-profile") {
    return { handled: true, result: await clearImportedProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-open-profile-dialog") {
    openImportedProfileDialog(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-open-profile-library") {
    openImportedProfileLibrary(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-close-profile-dialog") {
    closeImportedProfileDialog(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-close-profile-library") {
    closeImportedProfileLibrary(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-save-imported-profile") {
    return { handled: true, result: await saveImportedProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-rename-library-profile") {
    return { handled: true, result: await renameLibraryProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-delete-library-profile") {
    return { handled: true, result: await deleteLibraryProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-delete-imported-profile") {
    return { handled: true, result: await deleteImportedProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-apply-parameter-preset") {
    return { handled: true, result: await applyParameterPreset(context, view, target, ops) };
  }
  if (action === "tube-designer-open-save-preset" || action === "tube-designer-open-manage-preset") {
    openParameterPresetDialog(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-close-preset-dialog") {
    closeParameterPresetDialog(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-save-parameter-preset") {
    return { handled: true, result: await saveParameterPreset(context, view, target, ops) };
  }
  if (action === "tube-designer-delete-parameter-preset") {
    return { handled: true, result: await deleteParameterPreset(context, view, target, ops) };
  }
  if (action === "tube-designer-confirm-add") {
    return { handled: true, result: await generatePreview(context, view, ops, "add") };
  }
  if (action === "tube-designer-confirm-update" || action === "tube-designer-preview" || action === "tube-designer-generate") {
    return { handled: true, result: await generatePreview(context, view, ops, "update") };
  }
  if (action === "tube-designer-select-instance") {
    return { handled: true, result: await activateInstance(context, view, target, ops) };
  }
  if (action === "tube-designer-disassemble" || action === "tube-designer-open-disassemble") {
    openDisassemblySelector(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-close-disassemble") {
    if (!view.pending) {
      view.tubeDesignerDisassemblySelectorOpen = false;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-toggle-instance") {
    toggleInstance(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-all-instances") {
    toggleAllInstances(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-confirm-disassemble") {
    return { handled: true, result: await disassembleSelected(context, view, ops, { importToNesting: true }) };
  }
  if (action === "tube-designer-dismiss-disassembly-choice") {
    view.tubeDesignerPostDisassemblyChoice = null;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-export-after-disassembly") {
    view.tubeDesignerPostDisassemblyChoice = null;
    ops.renderProject(context, view);
    return { handled: true, result: await exportSelected(context, view, target, ops) };
  }
  if (action === "tube-designer-enter-cutting") {
    view.tubeDesignerPostDisassemblyChoice = null;
    view.tubeDesignerBreakdownOpen = false;
    view.tubeDesignerPartDimensionsVisible = false;
    view.tubeDesignerNestingSelectionKind = "part";
    return handlePartsAreaAction(context, view, "tube-designer-parts-open-nesting", target, ops);
  }
  if (action === "tube-designer-open-breakdown") {
    openBreakdownResults(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-open-instance-breakdown") {
    openInstanceBreakdown(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-close-breakdown") {
    if (!view.pending && !view.tubeDesignerExportOperation) {
      view.tubeDesignerBreakdownOpen = false;
      view.tubeDesignerBreakdownMode = "";
      view.tubeDesignerBreakdownProductIds = [];
      view.tubeDesignerSelectedPartIds = [];
      view.tubeDesignerActivePartId = "";
      view.tubeDesignerPartMeasurementState = null;
      view.viewport?.setCustomVisibleEntityIds?.([]);
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-toggle-part") {
    togglePart(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-part-group") {
    togglePartGroup(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-all-parts") {
    toggleAllParts(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-product-tree") {
    toggleProductTree(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-category-tree") {
    toggleCategoryTree(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-expand-breakdown-all") {
    setAllBreakdownExpansion(context, view, true, ops);
    return { handled: true };
  }
  if (action === "tube-designer-collapse-breakdown-all") {
    setAllBreakdownExpansion(context, view, false, ops);
    return { handled: true };
  }
  if (action === "tube-designer-set-breakdown-page") {
    setBreakdownPage(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-open-part-inspection") {
    openPartInspection(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-close-part-inspection") {
    view.tubeDesignerPartInspectionOpen = false;
    view.tubeDesignerInspectedPartId = "";
    view.viewport?.setContinuousRendering?.(!view.tubeDesignerAddDialogOpen);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-automatic-dimensions") {
    return { handled: toggleDesignerAutomaticDimensions(context) };
  }
  if (action === "tube-designer-inspection-fit-view") {
    return { handled: fitDesignerInspectedPart(context) };
  }
  if (action === "tube-designer-inspection-iso-view") {
    return { handled: setDesignerInspectedPartView(context) };
  }
  if (action === "tube-designer-export-selected" || action === "tube-designer-export-all") {
    return { handled: true, result: await exportSelected(context, view, target, ops) };
  }
  if (action === "tube-designer-nesting-export-parts") {
    openNestingPartList(context, view, ops);
    return { handled: true };
  }
  return false;
}

export async function handleDesignerRibbonCommand(context, view, commandId, ops) {
  if (commandId === "designer.history.undo" || commandId === "designer.history.redo") {
    return false;
  }
  const opensAddDialog = commandId === "designer.add" || commandId === "designer.generate";
  if (opensAddDialog) {
    if (view.tubeDesignerExportOperation) return true;
    const startupInFlight = Boolean(
      view.tubeDesignerLoading || view.tubeDesignerSynchronizationPromise,
    );
    // A click made during the initial scene read is a user request, not noise.
    // Queue it behind that read so one click eventually opens the catalogue.
    if (!view.pending || startupInFlight) await openAddDialog(context, view, ops);
    return true;
  }
  if (commandId === "designer.delete-active-product") {
    await deleteActiveProduct(context, view, ops);
    return true;
  }
  if (view.pending || view.tubeDesignerExportOperation) return true;
  const resourceAreas = {
    "resources.products": "products",
    "resources.profiles": "profiles",
    "resources.tools": "tools",
  };
  if (resourceAreas[commandId]) {
    const resourceArea = resourceAreas[commandId];
    view.tubeDesignerResourceLibraryArea = resourceArea;
    const progress = {
      title: "正在切换资源库",
      detail: resourceArea === "profiles"
        ? "正在装载管型资源与三维预览"
        : resourceArea === "tools"
          ? "正在读取模具目录与类别"
          : "正在读取产品模板目录",
      stage: "切换资源类型",
      mode: "Resources",
      minimumVisibleMs: RESOURCE_LIBRARY_SWITCH_PROGRESS_MINIMUM_VISIBLE_MS,
      steps: [
        ["切换资源类型", "正在更新当前资源库"],
        ["读取资源目录", "正在准备可用的资源记录"],
        ["准备预览", "正在把资源交给中央场景"],
      ],
    };
    const switchResource = async () => {
      // Resource commands are rendered by the app-shell ribbon. Updating only
      // the workbench view leaves the ribbon's active command stuck on the
      // default profile entry. Use the shell action so its state and the
      // workspace are refreshed together.
      await context.actions?.selectRibbonTab?.("resources");
      // selectRibbonTab() updates the app-shell state, but this command's
      // context was created before the tab changed.
      context.activeRibbonTabId = "resources";
      ops.renderProject(context, view);
      // The initial user-data hydration is intentionally non-blocking. If it
      // failed during the first scene load (or is still in flight), opening
      // the library must wait for it and retry instead of rendering an empty
      // 管型库 forever.
      await waitForDesignerUserData(context, view);
      if (resourceArea === "tools") await ensureToolLibraryCatalogue(context, view, ops);
    };
    if (typeof context.actions?.withProjectProgress === "function"
        && String(context.project?.projectId ?? "").trim()) {
      await context.actions.withProjectProgress(
        context.project.projectId,
        progress,
        switchResource,
      );
    } else {
      await switchResource();
    }
    return true;
  }
  if (await handleTubeMachiningRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleLicenseCommand(context, view, commandId, ops)) return true;
  if (commandId === "profiles.new-sketch") {
    await openProfileSectionSketch(context, view, ops, true);
    return true;
  }
  if (commandId === "tools.new-sketch") {
    await openToolSectionSketch(context, view, ops);
    return true;
  }
  if (await handleNestingPunchPartRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleNestingStandardPartRibbonCommand(context, view, commandId, ops)) return true;
  if (await handlePartDrawingRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleNestingPartImportRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleNestingRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleNestingSettingsRibbonCommand(context, view, commandId, ops)) return true;
  if (commandId === "nesting.export-parts") {
    openNestingPartList(context, view, ops);
    return true;
  }
  if (commandId === "nesting.export-result") {
    await handleNestingExportAction(context, view, "tube-designer-nesting-export-selected", null, ops);
    return true;
  }
  if (commandId === "nesting.results") {
    context.mount?.querySelector?.(".tube-designer-nesting-bottom")?.scrollIntoView?.({
      behavior: "smooth",
      block: "nearest",
    });
    return true;
  }
  if (await handleSketchRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleProfileLibraryRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleComponentLibraryRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleToolLibraryRibbonCommand(context, view, commandId, ops)) return true;
  if (commandId === "designer.templates.manage") {
    openProductTemplateManager(context, view, ops);
    return true;
  }
  if (commandId === "designer.templates.new") {
    openProductTemplateCreate(context, view, ops);
    return true;
  }
  if (commandId === "designer.templates.import") {
    await importProductTemplate(context, view, ops);
    return true;
  }
  if (commandId === "designer.templates.export") {
    await exportProductTemplate(context, view, ops);
    return true;
  }
  if (commandId === "designer.templates.delete") {
    await deleteProductTemplate(context, view, ops);
    return true;
  }
  if (commandId === "designer.excel.export-template") {
    await openBatchExcelTemplateDialog(context, view, ops);
    return true;
  }
  if (commandId === "designer.batch-add" || commandId === "designer.import-excel") {
    await chooseBatchAddWorkbook(context, view, ops);
    return true;
  }
  if (commandId === "designer.inspect-active-part") {
    openActiveProductPartInspection(context, view, ops);
    return true;
  }
  if (commandId === "designer.export-active-product-parts") {
    await exportActiveProductParts(context, view, ops);
    return true;
  }
  if (commandId === "designer.export-parts") {
    await openTransientPartList(context, view, ops);
    return true;
  }
  if (commandId === "designer.export-machining" || commandId === "designer.disassemble") {
    openDisassemblySelector(context, view, ops);
    return true;
  }
  if (commandId === "designer.breakdown" || commandId === "designer.export-step") {
    openBreakdownResults(context, view, ops);
    return true;
  }
  if (commandId === "parts.export-list") {
    await exportSelected(context, view, null, ops);
    return true;
  }
  if (commandId === "parts.open-nesting") {
    await context.actions?.selectRibbonTab?.("nesting");
    context.activeRibbonTabId = "nesting";
    view.activeAreaId = "nesting";
    ops.renderProject(context, view);
    return true;
  }
  return false;
}

function openProductTemplateManager(context, view, ops) {
  const designer = view.scene?.tubeDesigner ?? {};
  const first = (designer.templates ?? []).find((item) => item?.available)?.id
    ?? view.tubeDesignerUserData?.productTemplates?.[0]?.id ?? "";
  const selectedPackageId = productTemplateLibrarySelectedTemplateId(view);
  view.tubeDesignerTemplateManager = {
    mode: "list",
    selectedId: selectedPackageId
      || view.tubeDesignerTemplateManager?.selectedId || String(first),
  };
  view.error = "";
  ops.renderProject(context, view);
}

function closeProductTemplateManager(context, view, ops) {
  if (view.pending) return;
  view.tubeDesignerTemplateManager = null;
  view.tubeDesignerTemplateCreateDialog = null;
  ops.renderProject(context, view);
}

function selectProductTemplate(view, target, ops, context) {
  if (view.pending) return;
  const id = String(target?.dataset?.tubeTemplateId ?? "").trim();
  if (!id) return;
  view.tubeDesignerTemplateManager = {
    ...(view.tubeDesignerTemplateManager ?? { mode: "list" }),
    mode: "list",
    selectedId: id,
  };
  ops.renderProject(context, view);
}

function openProductTemplateCreate(context, view, ops) {
  if (view.pending) return;
  const designer = view.scene?.tubeDesigner ?? {};
  const selectedId = String(view.tubeDesignerTemplateManager?.selectedId ?? "");
  const selectedBuiltin = (designer.templates ?? []).find((item) => item?.available
    && String(item?.id ?? "") === selectedId);
  const baseTemplateId = String(
    selectedBuiltin?.id
      || (designer.templates ?? []).find((item) => item?.available)?.id
      || "",
  );
  view.tubeDesignerTemplateManager = {
    ...(view.tubeDesignerTemplateManager ?? {}),
    mode: "create",
    baseTemplateId,
    name: "",
    description: "",
  };
  view.error = "";
  ops.renderProject(context, view);
}

function templateManagerItems(view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const builtins = Array.isArray(designer.templates) ? designer.templates : [];
  const custom = Array.isArray(view.tubeDesignerUserData?.productTemplates)
    ? view.tubeDesignerUserData.productTemplates : [];
  return { builtins, custom };
}

function templateManagerSelection(view) {
  const { builtins, custom } = templateManagerItems(view);
  const selectedId = String(view.tubeDesignerTemplateManager?.selectedId ?? "");
  const customItem = custom.find((item) => String(item?.id ?? "") === selectedId);
  if (customItem) return { item: customItem, scope: "personal", id: selectedId };
  const builtIn = builtins.find((item) => String(item?.id ?? "") === selectedId);
  return builtIn ? { item: builtIn, scope: "builtin", id: selectedId } : null;
}

async function importProductTemplate(context, view, ops) {
  if (view.pending) return null;
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力。";
    ops.renderProject(context, view);
    return null;
  }
  const sourcePath = String(await bridge.openFileDialog({
    title: "导入产品模板（.itpt）",
    filters: [{ name: "iCAX 产品模板包", extensions: ["itpt"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  openProductTemplateManager(context, view, ops);
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.ImportProductTemplatePackage", {
      sourcePath,
    }, { timeoutMs: 120000 });
    const template = response?.template ?? response?.productTemplate;
    if (!template?.id) throw new Error("导入 .itpt 后没有返回模板记录。");
    upsertUserDataItem(view, "productTemplates", template);
    view.tubeDesignerTemplateManager = { mode: "list", selectedId: String(template.id) };
    await refreshDesignerUserData(context, view);
    ops.showNotice(context, view, `已导入产品模板“${template.name ?? template.displayName ?? template.id}”。`);
    return template;
  }, {
    operation: {
      kind: "product-template-import",
      title: "正在导入产品模板",
      phase: "importing-product-template",
      phaseLabel: "校验 .itpt 压缩包",
      message: "正在读取模板描述、脚本和资源",
    },
  });
}

async function exportProductTemplate(context, view, ops) {
  if (view.pending) return null;
  const selection = templateManagerSelection(view);
  if (!selection) {
    view.error = "请先选择一个产品模板。";
    openProductTemplateManager(context, view, ops);
    return null;
  }
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
  if (typeof bridge?.saveFileDialog !== "function") {
    view.error = "当前宿主没有提供保存文件能力。";
    ops.renderProject(context, view);
    return null;
  }
  const name = String(selection.item?.name ?? selection.item?.displayName ?? selection.id ?? "product-template")
    .replace(/[\\/:*?"<>|]/g, "_").trim() || "product-template";
  const targetPath = String(await bridge.saveFileDialog({
    title: "导出产品模板（.itpt）",
    defaultPath: `${name}.itpt`,
    defaultExtension: "itpt",
    filters: [{ name: "iCAX 产品模板包", extensions: ["itpt"] }],
  }) ?? "").trim();
  if (!targetPath) return null;
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.ExportProductTemplatePackage", {
      scope: selection.scope,
      id: selection.id,
      targetPath,
    }, { timeoutMs: 120000 });
    if (!response?.path) throw new Error("导出 .itpt 后没有返回文件路径。");
    ops.showNotice(context, view, `产品模板已导出：${response.path}`);
    return response;
  }, {
    operation: {
      kind: "product-template-export",
      title: "正在导出产品模板",
      phase: "exporting-product-template",
      phaseLabel: "生成 .itpt 压缩包",
      message: "正在整理模板描述、脚本和资源",
    },
  });
}

async function createProductTemplate(context, view, ops) {
  if (view.pending) return null;
  const state = view.tubeDesignerTemplateManager;
  const form = context.mount?.querySelector?.(".tube-designer-template-manager-dialog");
  const baseTemplateId = String(form?.querySelector?.("[data-tube-template-create-base]")?.value ?? state?.baseTemplateId ?? "").trim();
  const name = String(form?.querySelector?.("[data-tube-template-create-name]")?.value ?? state?.name ?? "").trim();
  const description = String(form?.querySelector?.("[data-tube-template-create-description]")?.value ?? state?.description ?? "").trim();
  if (!baseTemplateId) throw new Error("请选择基础模板。");
  if (!name) {
    form?.querySelector?.("[data-tube-template-create-name]")?.focus?.();
    throw new Error("请填写模板名称。");
  }
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.CreateProductTemplate", {
      baseTemplateId, name, description,
    }, { timeoutMs: 120000 });
    const template = response?.template ?? response?.productTemplate;
    if (!template?.id) throw new Error("新建模板后没有返回模板记录。");
    upsertUserDataItem(view, "productTemplates", template);
    view.tubeDesignerTemplateManager = { mode: "list", selectedId: String(template.id) };
    await refreshDesignerUserData(context, view);
    ops.showNotice(context, view, `已新增产品模板“${template.name ?? template.id}”。`);
    return template;
  }, {
    operation: {
      kind: "product-template-create",
      title: "正在新增产品模板",
      phase: "creating-product-template",
      phaseLabel: "保存模板包",
      message: "正在生成可导出的 .itpt 模板包",
    },
  });
}

async function deleteProductTemplate(context, view, ops) {
  if (view.pending) return null;
  const selection = templateManagerSelection(view);
  if (!selection) return null;
  if (selection.scope !== "personal") {
    view.error = "内置产品模板只读，不能删除。";
    ops.renderProject(context, view);
    return null;
  }
  if (typeof globalThis.confirm === "function"
      && !globalThis.confirm(`确定删除产品模板“${selection.item?.name ?? selection.id}”吗？`)) return null;
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.DeleteProductTemplate", {
      id: selection.id,
      revision: Number(selection.item?.revision ?? 0),
    }, { timeoutMs: 30000 });
    if (!response?.deleted) throw new Error("产品模板未能删除。");
    view.tubeDesignerUserData.productTemplates = (view.tubeDesignerUserData.productTemplates ?? [])
      .filter((item) => String(item?.id ?? "") !== selection.id);
    view.tubeDesignerTemplateManager = { mode: "list", selectedId: "" };
    ops.showNotice(context, view, `已删除产品模板“${selection.item?.name ?? selection.id}”。`);
    return response;
  }, {
    operation: {
      kind: "product-template-delete",
      title: "正在删除产品模板",
      phase: "deleting-product-template",
      phaseLabel: "删除模板记录",
      message: `正在删除“${selection.item?.name ?? selection.id}”`,
    },
  });
}

async function openProfileSectionSketch(context, view, ops, createNew) {
  if (view.pending) return null;
  const current = view.tubeDesignerSketch?.section;
  if (current?.dirty && typeof globalThis.confirm === "function"
      && !globalThis.confirm("当前截面还有未保存的修改，确定开始另一个截面吗？")) return null;
  if (createNew) {
    const state = beginNewSectionSketch(view);
    view.tubeDesignerSketchDialogOpen = true;
    ops.renderProject(context, view);
    return state;
  }

  view.pending = true;
  view.error = "";
  view.progress = {
    title: "正在打开截面草图",
    detail: "正在读取当前参数对应的截面轮廓",
    stage: "准备可编辑几何",
    mode: "Sketch",
  };
  ops.renderProject(context, view);
  try {
    const source = await resolveSelectedProfileSketchSource(context, view);
    const state = beginProfileSectionSketch(view, source);
    view.pending = false;
    view.progress = null;
    view.tubeDesignerSketchDialogOpen = true;
    ops.renderProject(context, view);
    return state;
  } catch (error) {
    view.pending = false;
    view.progress = null;
    view.error = error?.message ?? String(error);
    ops.renderProject(context, view);
    return null;
  }
}

async function openToolSectionSketch(context, view, ops) {
  if (view.pending) return null;
  const current = view.tubeDesignerSketch?.section;
  if (current?.dirty && typeof globalThis.confirm === "function"
      && !globalThis.confirm("当前草图还有未保存的修改，确定开始绘制新模具吗？")) return null;
  const state = beginNewSectionSketch(view);
  state.sectionName = "我的草图模具";
  view.tubeDesignerToolSketchContext = { kind: "fixed", target: "side", category: "孔型" };
  view.tubeDesignerSketchDialogOpen = true;
  view.error = "";
  ops.renderProject(context, view);
  return state;
}

async function chooseBatchAddWorkbook(context, view, ops, suppliedPath = "") {
  if (view.pending) return null;
  let sourcePath = suppliedPath;
  if (!sourcePath) {
    const bridge = context.appProxy?.bridge
      ?? context.productProxy?.bridge
      ?? context.sceneProxy?.bridge
      ?? null;
    if (typeof bridge?.openFileDialog !== "function") {
      view.error = "当前宿主没有提供文件选择能力。";
      ops.renderProject(context, view);
      return null;
    }
    sourcePath = String(await bridge.openFileDialog({
      title: "选择 TubeDesigner 批量导入 Excel 文件",
      filters: [{ name: "Excel 工作簿", extensions: ["xlsx"] }],
    }) ?? "").trim();
  }
  if (!sourcePath) return null;

  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.ReadBatchExcelImport", {
      sourcePath,
    }, { timeoutMs: 120000 });
    const rows = Array.isArray(response?.rows) ? response.rows : [];
    if (!rows.length) throw new Error("Excel 中没有可导入的数据行。请从第 3 行开始填写产品数据。");
    view.tubeDesignerBatchImportPath = sourcePath;
    view.tubeDesignerExcelImportDialog = {
      sourcePath,
      templateId: String(response?.templateId ?? ""),
      templateName: String(response?.templateName ?? response?.templateId ?? "产品模板"),
      rows,
    };
    view.error = "";
    const fileName = sourcePath.split(/[\\/]/).pop() || sourcePath;
    ops.appendProjectLog(context, "info", `读取批量产品 Excel：${sourcePath}`);
    ops.showNotice(context, view, `已读取 ${rows.length} 行产品数据：${fileName}`);
    return response;
  }, {
    operation: {
      kind: "batch-product-read",
      title: "正在读取 Excel 产品数据",
      phase: "reading-excel",
      phaseLabel: "核对模板与列名",
      message: "正在读取 Excel 模板内置的列定义",
    },
  });
}

async function openBatchExcelTemplateDialog(context, view, ops) {
  if (view.pending) return null;
  const designer = view.scene?.tubeDesigner ?? {};
  const first = getDefaultTemplate(designer.templates ?? []);
  if (!first?.id) throw new Error("当前没有可导出的产品模板。");
  const template = await ensureTemplateDescriptor(context, view, first.id);
  if (!Array.isArray(template?.parameters)) throw new Error("产品模板参数尚未加载完成。请稍后重试。");
  view.tubeDesignerExcelTemplateDialog = buildBatchExcelTemplateDialogState(template);
  view.error = "";
  ops.renderProject(context, view);
  return template;
}

function buildBatchExcelTemplateDialogState(template) {
  return {
    templateId: String(template.id),
    columns: batchExcelColumnsFromTemplate(template),
  };
}

function batchExcelColumnsFromTemplate(template) {
  const columns = [
    { key: "__instanceName", title: "instanceName", displayName: "实例名称", groupTitle: "实例信息", description: "留空时自动命名。", inputKind: "text", included: true, required: false, defaultValue: "" },
    { key: "__instanceQuantity", title: "quantity", displayName: "生产数量", groupTitle: "实例信息", description: "每一行产品实例的生产数量。", inputKind: "number", minimum: 1, step: 1, included: true, required: true, defaultValue: "1" },
  ];
  const groupTitles = new Map((template?.groups ?? []).map((group) => [
    String(group?.key ?? ""),
    catalogText(group?.displayName ?? group?.name, String(group?.key ?? "")),
  ]));
  const titles = new Set(columns.map((column) => column.title));
  for (const field of template?.parameters ?? []) {
    if (field?.readOnly) continue;
    const key = String(field?.key ?? field?.name ?? "").trim();
    if (!key) continue;
    // Project notes and installation verification are retained by the product
    // template, but they are not per-row manufacturing inputs.  Keeping them
    // out of the interchange contract avoids making third-party spreadsheets
    // carry fields that do not define a product instance.
    const group = String(field?.groupKey ?? field?.group ?? "").trim();
    if (["installation", "project_rules"].includes(group)) continue;
    const rawDefault = field?.defaultValue ?? field?.default ?? "";
    const displayName = catalogText(field?.displayName ?? field?.label ?? field?.name, key);
    // The worksheet and embedded template contract use stable English parameter keys.  The
    // Chinese label remains in the dialog as a human-facing explanation.
    const title = key;
    if (titles.has(title)) continue;
    titles.add(title);
    const optionSource = Array.isArray(field?.options) ? field.options : (Array.isArray(field?.choices) ? field.choices : []);
    const options = optionSource.map((option) => {
      const value = typeof option === "object" ? option?.value : option;
      const label = typeof option === "object"
        ? catalogText(option?.label ?? option?.displayName, String(value ?? ""))
        : String(option ?? "");
      return { value: value == null ? "" : String(value), label };
    }).filter((option) => option.value !== "");
    const rawType = String(field?.type ?? field?.valueType ?? "").toLowerCase();
    const inputKind = options.length ? "select"
      : (rawType === "boolean" ? "boolean"
        : (["number", "integer", "decimal", "float"].includes(rawType) ? "number" : "text"));
    columns.push({
      key,
      title,
      displayName,
      groupTitle: (groupTitles.get(group) ?? group) || "其他参数",
      description: catalogText(field?.description, ""),
      inputKind,
      options,
      minimum: field?.min ?? field?.constraints?.minimum,
      maximum: field?.max ?? field?.constraints?.maximum,
      step: field?.step ?? field?.constraints?.step,
      included: false,
      required: field?.required !== false,
      defaultValue: rawDefault == null ? "" : String(rawDefault),
    });
  }
  return columns;
}

async function selectBatchExcelTemplate(context, view, target, ops) {
  const state = view.tubeDesignerExcelTemplateDialog;
  if (!state || view.pending || state.loading) return null;
  const templateId = String(target?.value ?? target?.dataset?.tubeDesignerExcelTemplateId ?? "").trim();
  if (!templateId || templateId === String(state.templateId ?? "")) return state.templateId;
  const designer = view.scene?.tubeDesigner ?? {};
  const candidate = getTemplateById(designer.templates, templateId);
  if (!candidate?.available) throw new Error("所选产品模板当前不可用。");

  // A template can be listed before its descriptor arrives.  Keep the dialog
  // in place while loading it, then replace only its column-definition draft.
  view.tubeDesignerExcelTemplateDialog = { ...state, loading: true };
  ops.renderProject(context, view);
  try {
    const template = await ensureTemplateDescriptor(context, view, templateId);
    if (!Array.isArray(template?.parameters)) throw new Error("产品模板参数尚未加载完成。请稍后重试。");
    view.tubeDesignerExcelTemplateDialog = {
      ...buildBatchExcelTemplateDialogState(template), loading: false,
    };
    view.error = "";
    ops.renderProject(context, view);
    return template.id;
  } catch (error) {
    view.tubeDesignerExcelTemplateDialog = { ...state, loading: false };
    ops.renderProject(context, view);
    throw error;
  }
}

function closeBatchExcelTemplateDialog(context, view, ops) {
  view.tubeDesignerExcelTemplateDialog = null;
  view.error = "";
  ops.renderProject(context, view);
}

async function exportBatchExcelTemplate(context, view, ops) {
  if (view.pending) return null;
  const state = view.tubeDesignerExcelTemplateDialog;
  if (!state?.templateId) throw new Error("请先选择产品模板。");
  const form = resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-excel-template-form]");
  const columns = (state.columns ?? []).filter((column) => {
    const key = String(column.key ?? "");
    const input = Array.from(form?.querySelectorAll?.("[data-tube-designer-excel-include]") ?? [])
      .find((item) => String(item?.getAttribute?.("data-tube-designer-excel-include") ?? "") === key);
    return input ? Boolean(input.checked) : column.included !== false;
  }).map((column) => {
    const key = String(column.key ?? "");
    const inputByKey = (attribute) => Array.from(form?.querySelectorAll?.(`[${attribute}]`) ?? [])
      .find((input) => String(input?.getAttribute?.(attribute) ?? "") === key);
    const required = Boolean(inputByKey("data-tube-designer-excel-required")?.checked);
    const defaultValue = String(inputByKey("data-tube-designer-excel-default")?.value ?? "");
    return { key, title: column.title, required, defaultValue };
  });
  if (!columns.length) throw new Error("请至少勾选一个需要携带到 Excel 的字段。");
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
  if (typeof bridge?.saveFileDialog !== "function") throw new Error("当前宿主没有提供保存文件能力。");
  const template = await ensureTemplateDescriptor(context, view, state.templateId);
  const baseName = String(template?.name ?? template?.displayName ?? state.templateId)
    .replace(/[\\/:*?\"<>|]/g, "_").trim() || "产品批量导入";
  const targetPath = String(await bridge.saveFileDialog({
    title: "导出 Excel 批量导入工作簿（.xlsx）",
    defaultPath: `${baseName}_批量导入.xlsx`,
    defaultExtension: "xlsx",
    filters: [{ name: "Excel 工作簿", extensions: ["xlsx"] }],
  }) ?? "").trim();
  if (!targetPath) return null;
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.ExportBatchExcelTemplate", {
      templateId: state.templateId,
      columns,
      targetPath,
    }, { timeoutMs: 120000 });
    if (!response?.templatePath) throw new Error("Excel 模板导出没有返回文件路径。");
    closeBatchExcelTemplateDialog(context, view, ops);
    ops.showNotice(context, view, `已导出 Excel 模板：${response.templatePath}`);
    return response;
  }, {
    operation: {
      kind: "batch-product-template-export",
      title: "正在导出 Excel 产品模板",
      phase: "writing-excel-template",
      phaseLabel: "写入列定义与工作簿",
        message: "将生成可直接填写并导入的 .xlsx 工作簿",
    },
  });
}

async function importBatchExcelProducts(context, view, ops) {
  const state = view.tubeDesignerExcelImportDialog;
  const rows = Array.isArray(state?.rows) ? state.rows : [];
  if (!rows.length || view.pending) return null;
  return runDesignerOperation(context, view, ops, async () => {
    for (let index = 0; index < rows.length; index += 1) {
      const row = rows[index] ?? {};
      updateDesignerOperation(context, view, {
        phaseLabel: `正在创建第 ${index + 1} / ${rows.length} 个实例`,
        message: `Excel 第 ${row.sourceRow ?? index + 3} 行`,
      });
      await invokeProductRequest(context, "TubeDesigner.GeneratePreview", {
        templateId: state.templateId,
        ...(row.parameters ?? {}),
        instanceName: String(row.instanceName ?? ""),
        instanceQuantity: Number(row.instanceQuantity ?? 1),
      }, { timeoutMs: 180000 });
    }
    view.tubeDesignerExcelImportDialog = null;
    await refreshDesignerState(context, view, ops);
    ops.showNotice(context, view, `已按 Excel 创建 ${rows.length} 个产品实例。`);
    return { count: rows.length };
  }, {
    operation: {
      kind: "batch-product-import",
      title: "正在创建 Excel 产品实例",
      phase: "creating-products",
      phaseLabel: `准备创建 ${rows.length} 个实例`,
      message: "每一行将生成一个独立产品实例",
    },
  });
}

export async function refreshDesignerState(context, view, ops = null) {
  if (!context.sceneProxy) return false;
  try {
    const nestingOnly = view.activeAreaId === "nesting";
    const includeManufacturingGeometry = view.activeAreaId === "parts"
      || Boolean(view.tubeDesignerBreakdownOpen);
    const response = await context.sceneProxy.invoke(
      "TubeDesigner.List",
      { nestingOnly, includeManufacturingGeometry },
      // The normal product mount only reads the snapshot.  Keep the long
      // timeout for deliberate manufacturing-geometry restoration, but do
      // not leave the first screen waiting for three minutes.
      { timeoutMs: includeManufacturingGeometry ? 180000 : 30000 },
    );
    const designer = response?.tubeDesigner ?? {};
    const previousProductId = String(view.scene?.tubeDesigner?.product?.entityId ?? "");
    view.scene ??= {};
    view.scene.tubeDesigner = designer;
    restoreLoadedProductTemplateDescriptors(view, designer);
    const activeTemplateId = String(designer.product?.templateId ?? "").trim();
    if (activeTemplateId) {
      try {
        await ensureTemplateDescriptor(context, view, activeTemplateId);
      } catch (error) {
        // A catalog refresh should still be usable when a single detail
        // request is temporarily unavailable; editing will retry on demand.
        view.tubeDesignerTemplateLoadError = error?.message ?? String(error);
      }
    }
    restoreRightDraftForActiveProduct(view, designer.product);
    if (previousProductId !== String(designer.product?.entityId ?? "")) {
      view.tubeDesignerRightPresetSelection = "";
      view.tubeDesignerRightPresetSelections = {};
    }
    reconcileSelections(view, designer);
    restoreSavedNestingTask(view, context);
    if (!(designer.manufacturingGroups?.length > 0)) view.tubeDesignerBreakdownOpen = false;
    ops?.renderProject?.(context, view);
    return true;
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops?.renderProject?.(context, view);
    throw error;
  }
}

export async function refreshDesignerUserData(context, view, ops = null) {
  // User-data hydration can be requested by the initial workbench mount and
  // again when a resource-library tab is opened. Share one in-flight request
  // so the two callers never race the same product SDO.
  if (view.tubeDesignerUserDataRefreshPromise) {
    return view.tubeDesignerUserDataRefreshPromise;
  }
  const refresh = (async () => {
  if (typeof context.productProxy?.invoke !== "function") {
    view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], profileId: "" };
    view.tubeDesignerSystemProfiles ??= [];
    view.tubeDesignerTemplateProfiles ??= [];
    return false;
  }
  try {
    const response = await context.productProxy.invoke(
      "TubeDesigner.ListUserData", {}, { timeoutMs: 30000 },
    );
    if (!Array.isArray(response?.systemProfiles)) {
      throw new Error("管型列表响应缺少系统目录");
    }
    view.tubeDesignerUserData = {
      customers: Array.isArray(response?.customers) ? response.customers : [],
      parameterPresets: Array.isArray(response?.parameterPresets) ? response.parameterPresets : [],
      profiles: Array.isArray(response?.profiles) ? response.profiles : [],
      punchTools: Array.isArray(response?.punchTools) ? response.punchTools : [],
      productTemplates: Array.isArray(response?.productTemplates) ? response.productTemplates : [],
      profileId: String(response?.profileId ?? ""),
    };
    view.tubeDesignerSystemProfiles = Array.isArray(response?.systemProfiles)
      ? response.systemProfiles : [];
    view.tubeDesignerTemplateProfiles = Array.isArray(response?.templateProfiles)
      ? response.templateProfiles : [];
    view.tubeDesignerUserDataError = "";
    view.tubeDesignerSystemProfilesError = "";
    ops?.renderProject?.(context, view);
    return true;
  } catch (error) {
    view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], profileId: "" };
    view.tubeDesignerSystemProfiles ??= [];
    view.tubeDesignerTemplateProfiles ??= [];
    view.tubeDesignerUserDataError = error?.message ?? String(error);
    // System resources must not depend on personal data or product templates.
    try {
      const catalog = await context.productProxy.invoke(
        "TubeDesigner.ListSystemProfiles", {}, { timeoutMs: 30000 },
      );
      if (!Array.isArray(catalog?.systemProfiles)) throw new Error("系统管型目录返回的数据无效");
      view.tubeDesignerSystemProfiles = catalog.systemProfiles;
      view.tubeDesignerSystemProfilesError = "";
    } catch (catalogError) {
      view.tubeDesignerSystemProfilesError = catalogError?.message ?? String(catalogError);
    }
    ops?.renderProject?.(context, view);
    return false;
  }
  })();
  view.tubeDesignerUserDataRefreshPromise = refresh;
  try {
    return await refresh;
  } finally {
    if (view.tubeDesignerUserDataRefreshPromise === refresh) {
      view.tubeDesignerUserDataRefreshPromise = null;
    }
  }
}

async function waitForDesignerUserData(context, view) {
  const synchronization = view.tubeDesignerUserDataSynchronizationPromise;
  if (synchronization) {
    try { await synchronization; } catch (_) { /* retry below when needed */ }
  }
  if (view.tubeDesignerUserDataRefreshPromise) {
    try { await view.tubeDesignerUserDataRefreshPromise; } catch (_) { /* retry below */ }
  }
  const hasSystemProfiles = Array.isArray(view.tubeDesignerSystemProfiles)
    && view.tubeDesignerSystemProfiles.length > 0;
  if (!hasSystemProfiles || view.tubeDesignerUserDataError) {
    view.tubeDesignerUserDataLoading = true;
    let loaded = false;
    try {
      loaded = await refreshDesignerUserData(context, view);
      return loaded;
    } finally {
      view.tubeDesignerUserDataLoading = false;
      view.tubeDesignerUserDataLoaded = Boolean(loaded)
        && Array.isArray(view.tubeDesignerSystemProfiles)
        && view.tubeDesignerSystemProfiles.length > 0;
    }
  }
  return true;
}

export function getDesignerRenderSignature(designer = {}) {
  return JSON.stringify([...(designer.members ?? [])]
    .map((member) => ({
      entityId: String(member?.entityId ?? ""),
      resourceId: String(member?.previewGeometryResourceId ?? ""),
      version: String(member?.previewGeometryResourceVersion ?? "0"),
    }))
    .sort((left, right) => left.entityId.localeCompare(right.entityId)));
}

async function openAddDialog(context, view, ops) {
  if (view.tubeDesignerAddOpeningPromise) return view.tubeDesignerAddOpeningPromise;
  if (view.tubeDesignerAddDialogOpen) return true;
  const startupInFlight = Boolean(
    view.tubeDesignerLoading || view.tubeDesignerSynchronizationPromise,
  );
  if (view.pending && !startupInFlight) return false;
  const opening = openAddDialogAfterStartup(context, view, ops);
  view.tubeDesignerAddOpeningPromise = opening;
  try {
    return await opening;
  } finally {
    if (view.tubeDesignerAddOpeningPromise === opening) {
      view.tubeDesignerAddOpeningPromise = null;
    }
  }
}

async function openAddDialogAfterStartup(context, view, ops) {
  // The command can arrive immediately after the first workbench paint. Yield
  // once so entry.mjs can publish its shared initialization promises.
  await Promise.resolve();
  const sceneSynchronization = view.tubeDesignerSynchronizationPromise;
  if (sceneSynchronization) {
    try { await sceneSynchronization; } catch (_) { /* expose the scene error below */ }
  }
  view.tubeDesignerAddInstanceQuantity = 1;
  const designer = view.scene?.tubeDesigner ?? {};
  let template = getDefaultTemplate(designer.templates ?? []);
  if (!template) {
    view.error = "当前没有可用的产品模板。";
    ops.renderProject(context, view);
    return;
  }
  const createdAt = new Date().toISOString();
  let entry = getCatalogEntry(designer.templates, template.id);
  view.tubeDesignerAddDialogOpen = true;
  view.tubeDesignerAddTemplateId = template.id;
  view.tubeDesignerAddCatalogPresetId = entry?.presetId ?? "";
  view.tubeDesignerAddCatalogEntryId = entry?.catalogEntryId ?? template.id;
  view.tubeDesignerAddCreatedAt = createdAt;
  view.tubeDesignerAddInstanceName = makeInstanceName(
    { ...template, name: entry?.displayName ?? template.name },
    createdAt,
  );
  view.tubeDesignerAddDraft = {
    ...getDefaultParameters(designer.templates, template.id),
    ...(entry?.catalogParameters ?? {}),
  };
  view.tubeDesignerAddPresetSelection = "";
  view.tubeDesignerAddPresetSelections = {};
  view.tubeDesignerExpandedTemplateGroupIds = getCatalogEntryGroupKeys(
    designer.templates, template.id, entry?.presetId ?? "",
  );
  view.tubeDesignerAddCatalogTabId = view.tubeDesignerExpandedTemplateGroupIds[0] ?? "";
  view.tubeDesignerAddScrollAnchor = null;
  view.tubeDesignerAddScrollAnchors = null;
  view.tubeDesignerAddEditorStage = "parameters";
  view.tubeDesignerManufacturingPlans ??= {};
  view.tubeDesignerManufacturingPlans.add = null;
  view.tubeDesignerTemplateSwitchPending = true;
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.tubeDesignerBreakdownOpen = false;
  view.error = "";
  view.viewport?.setContinuousRendering?.(false);
  if (!mountAddDialog(context, designer, view)) ops.renderProject(context, view);
  let progress = showAddTemplateSwitchProgress(context, {
    title: "正在打开产品目录",
    message: "正在读取模板参数与常用配置",
    phase: "首次加载",
  });
  await waitForPaint();
  let progressShownAt = progress ? nowMilliseconds() : null;
  try {
    // ListUserData and template descriptors share the embedded Python host.
    // Reuse the startup requests instead of racing them on the first click.
    const userDataSynchronization = view.tubeDesignerUserDataSynchronizationPromise;
    if (userDataSynchronization) {
      try { await userDataSynchronization; } catch (_) { /* template loading remains usable */ }
    }
    const userDataRefresh = view.tubeDesignerUserDataRefreshPromise;
    if (userDataRefresh) {
      try { await userDataRefresh; } catch (_) { /* template loading remains usable */ }
    }
    // Startup hydration may repaint the workbench while this modal is open.
    // Restore the in-dialog feedback before starting the descriptor request.
    if (!progress?.isConnected) {
      progress = showAddTemplateSwitchProgress(context, {
        title: "正在打开产品目录",
        message: "正在读取模板参数与常用配置",
        phase: "首次加载",
      });
      progressShownAt = progress ? nowMilliseconds() : progressShownAt;
      await waitForPaint();
    }
    await ensureTemplateDescriptor(context, view, template.id);
    template = getTemplateById(designer.templates, template.id) ?? template;
    if (productUsesToolLibrary(template)) await ensureToolLibraryCatalogue(context, view, ops);
  } catch (error) {
    view.tubeDesignerTemplateSwitchPending = false;
    const message = error?.message ?? String(error);
    closeAddDialog(context, view, ops);
    view.error = message;
    ops.renderProject(context, view);
    return false;
  } finally {
    if (progressShownAt != null) {
      await waitForMinimumDuration(progressShownAt, ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS);
    }
    progress?.remove?.();
  }
  entry = getCatalogEntry(designer.templates, template.id);
  view.tubeDesignerAddTemplateId = template.id;
  view.tubeDesignerAddCatalogPresetId = entry?.presetId ?? "";
  view.tubeDesignerAddCatalogEntryId = entry?.catalogEntryId ?? template.id;
  view.tubeDesignerAddCreatedAt = createdAt;
  view.tubeDesignerAddInstanceName = makeInstanceName({ ...template, name: entry?.displayName ?? template.name }, createdAt);
  view.tubeDesignerAddDraft = { ...getDefaultParameters(designer.templates, template.id), ...(entry?.catalogParameters ?? {}) };
  view.tubeDesignerAddPresetSelection = "";
  view.tubeDesignerAddPresetSelections = {};
  view.tubeDesignerExpandedTemplateGroupIds = getCatalogEntryGroupKeys(
    designer.templates, template.id, entry?.presetId ?? "",
  );
  view.tubeDesignerAddCatalogTabId = view.tubeDesignerExpandedTemplateGroupIds[0] ?? "";
  view.tubeDesignerAddScrollAnchor = null;
  view.tubeDesignerAddScrollAnchors = null;
  view.tubeDesignerAddEditorStage = "parameters";
  view.tubeDesignerManufacturingPlans ??= {};
  view.tubeDesignerManufacturingPlans.add = null;
  view.tubeDesignerTemplateSwitchPending = false;
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.tubeDesignerBreakdownOpen = false;
  view.error = "";
  // Rebuild only the modal so the newly loaded descriptor enables its fields
  // without refreshing the scene or either side pane.
  if (!mountAddDialog(context, designer, view)) ops.renderProject(context, view);
  return true;
}

function closeAddDialog(context, view, ops) {
  view.tubeDesignerAddDialogOpen = false;
  view.tubeDesignerAddTemplateId = "";
  view.tubeDesignerAddCatalogPresetId = "";
  view.tubeDesignerAddCatalogEntryId = "";
  view.tubeDesignerAddDraft = null;
  view.tubeDesignerAddPresetSelection = "";
  view.tubeDesignerAddPresetSelections = {};
  view.tubeDesignerPresetDialog = null;
  view.tubeDesignerProfileDialog = null;
  view.tubeDesignerProfileLibraryDialog = null;
  view.tubeDesignerExpandedTemplateGroupIds = [];
  view.tubeDesignerAddCatalogTabId = "";
  view.tubeDesignerAddScrollAnchor = null;
  view.tubeDesignerAddScrollAnchors = null;
  view.tubeDesignerTemplateSwitchPending = false;
  view.tubeDesignerAddEditorStage = "parameters";
  view.tubeDesignerManufacturingPlans ??= {};
  view.tubeDesignerManufacturingPlans.add = null;
  view.error = "";
  const dialog = resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-add-dialog]");
  const backdrop = dialog?.closest?.(".tube-designer-modal-backdrop");
  if (backdrop) backdrop.remove();
  else ops.renderProject(context, view);
  view.viewport?.setContinuousRendering?.(!view.tubeDesignerPartInspectionOpen);
}

async function selectTemplate(context, view, target, ops) {
  if (!view.tubeDesignerAddDialogOpen || view.pending || view.tubeDesignerTemplateSwitchPending) return;
  captureAddDialogScrollAnchor(context, view, target);
  const designer = view.scene?.tubeDesigner ?? {};
  const templateId = String(target?.dataset?.tubeDesignerTemplateId ?? target?.value ?? "").trim();
  let template = getTemplateById(designer.templates, templateId);
  if (!template?.available) return;
  const presetId = String(target?.dataset?.tubeDesignerCatalogPresetId ?? "").trim();
  let entry = getCatalogEntry(designer.templates, template.id, presetId);
  if (!entry) return;
  // Clicking the active style must not wipe dimensions already entered.
  if (view.tubeDesignerAddCatalogEntryId === entry.catalogEntryId) return template.id;
  view.tubeDesignerTemplateSwitchPending = true;
  const progress = showAddTemplateSwitchProgress(context);
  await waitForPaint();
  const progressShownAt = progress ? nowMilliseconds() : null;
  try {
    await ensureTemplateDescriptor(context, view, template.id);
    const loadedTemplate = getTemplateById(designer.templates, template.id);
    if (!loadedTemplate?.available) throw new Error("模板详情不可用，无法编辑该模板。");
    const loadedEntry = getCatalogEntry(designer.templates, loadedTemplate.id, presetId);
    if (!loadedEntry) throw new Error("模板目录项已变化，请重新选择。");
    template = loadedTemplate;
    if (productUsesToolLibrary(template)) await ensureToolLibraryCatalogue(context, view, ops);
    entry = loadedEntry;
    view.tubeDesignerAddTemplateId = template.id;
    view.tubeDesignerAddCatalogPresetId = entry.presetId;
    view.tubeDesignerAddCatalogEntryId = entry.catalogEntryId;
    view.tubeDesignerExpandedTemplateGroupIds = getCatalogEntryGroupKeys(
      designer.templates, template.id, entry.presetId,
    );
    view.tubeDesignerAddCatalogTabId = view.tubeDesignerExpandedTemplateGroupIds[0] ?? "";
    view.tubeDesignerAddDraft = { ...getDefaultParameters(designer.templates, template.id), ...entry.catalogParameters };
    view.tubeDesignerAddPresetSelection = "";
    view.tubeDesignerAddPresetSelections = {};
    view.tubeDesignerAddEditorStage = "parameters";
    view.tubeDesignerManufacturingPlans ??= {};
    view.tubeDesignerManufacturingPlans.add = null;
    view.tubeDesignerAddInstanceName = makeInstanceName({ ...template, name: entry.displayName }, view.tubeDesignerAddCreatedAt);
    view.error = "";
    synchronizeSelectedTemplateCard(context, entry.catalogEntryId);
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
    return template.id;
  } finally {
    if (progressShownAt != null) {
      await waitForMinimumDuration(progressShownAt, ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS);
    }
    progress?.remove?.();
    view.tubeDesignerTemplateSwitchPending = false;
  }
}

function selectTemplateTab(context, view, target, ops) {
  if (!view.tubeDesignerAddDialogOpen || view.pending) return;
  const groupId = String(target?.dataset?.tubeDesignerTemplateTabId ?? "").trim();
  if (!groupId || view.tubeDesignerAddCatalogTabId === groupId) return;
  captureAddDialogScrollAnchor(context, view, target);
  view.tubeDesignerAddCatalogTabId = groupId;
  // A tab change only changes the catalogue browser; it must not reset the
  // selected template, its parameters, or the preview on the right.
  ops.renderProject(context, view);
  restoreAddDialogScrollAnchor(context, view);
}

function toggleTemplateGroup(context, view, target, ops) {
  if (!view.tubeDesignerAddDialogOpen || view.pending) return;
  const groupId = String(target?.dataset?.tubeDesignerTemplateGroupId ?? "").trim();
  if (!groupId) return;
  captureAddDialogScrollAnchor(context, view, target);
  const expanded = new Set(view.tubeDesignerExpandedTemplateGroupIds ?? []);
  const nextExpanded = !expanded.has(groupId);
  if (nextExpanded) expanded.add(groupId);
  else expanded.delete(groupId);
  view.tubeDesignerExpandedTemplateGroupIds = [...expanded];
  const group = target.closest?.(".tube-designer-template-group");
  const items = Array.from(group?.children ?? [])
    .find((child) => child.classList?.contains?.("tube-designer-template-group-items"));
  group?.classList?.toggle?.("expanded", nextExpanded);
  target.setAttribute?.("aria-expanded", String(nextExpanded));
  if (items) items.hidden = !nextExpanded;
  if (!group || !items) ops.renderProject(context, view);
  restoreAddDialogScrollAnchor(context, view);
}

async function updateParameterDraft(context, view, target, ops) {
  if (view.pending || !target) return;
  const addForm = target.closest?.("[data-tube-designer-add-form]");
  const rightForm = target.closest?.("[data-tube-designer-parameter-form]");
  const designer = view.scene?.tubeDesigner ?? {};
  const editedParameter = String(target?.dataset?.tubeDesignerParameter ?? "");
  view.tubeDesignerLastEditedParameterKey = editedParameter;
  if (addForm) {
    captureAddDialogScrollAnchor(context, view, target);
    const template = getTemplateById(designer.templates, view.tubeDesignerAddTemplateId);
    view.tubeDesignerAddDraft = materializeProductToolDefaults(view, template, normalizeDependentParameters(collectParameters(context.mount, "[data-tube-designer-add-form]", {
      ...getDefaultParameters(designer.templates, view.tubeDesignerAddTemplateId),
      ...(view.tubeDesignerAddDraft ?? {}),
    }), template, editedParameter));
    markSelectedPresetModified(
      view, "add", template, editedParameter,
    );
  } else if (rightForm) {
    captureParameterPanelState(context, view, target);
    const template = getTemplateById(designer.templates, designer.product?.templateId);
    const nextRightDraft = materializeProductToolDefaults(view, template, normalizeDependentParameters(collectParameters(context.mount, "[data-tube-designer-parameter-form]", {
      ...getDefaultParameters(designer.templates, designer.product?.templateId),
      ...(view.tubeDesignerRightDraft ?? designer.product?.parameters ?? {}),
    }), template, editedParameter));
    markSelectedPresetModified(
      view, "right", template, editedParameter,
    );
    return commitRightProductParameters(context, view, nextRightDraft, ops);
  } else {
    return;
  }
  if (addForm) {
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
  } else {
    // Existing instances use an explicit commit. Editing updates the linked
    // product diagram and conditional fields only; the three-dimensional
    // scene remains the last confirmed version until “重新生成” is pressed.
    if (!refreshRightParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreParameterPanelState(context, view);
    context.actions?.refreshProjectHistoryControls?.();
  }
  return null;
}

function editSceneSpecification(context, view, target) {
  if (view.pending) return;
  const designer = view.scene?.tubeDesigner ?? {};
  if (String(designer.product?.templateId ?? "") !== "single-face-security-window") return;
  const parameter = String(target?.dataset?.tubeDesignerParameterKey ?? "").trim();
  if (!parameter) return;
  view.tubeDesignerSceneSpecificationEditorParameter = parameter;
  view.tubeDesignerLastEditedParameterKey = parameter;
  bindProductSpecificationAnnotations(resolveDesignerMount(context), view);
  view.viewport?.focusSpecificationAnnotationEditor?.(parameter);
}

function sceneSpecificationInputValue(target, field) {
  if (target?.type === "checkbox") return Boolean(target.checked);
  const optionType = target?.selectedOptions?.[0]?.dataset?.tubeDesignerValueType;
  if (optionType === "number") return Number(target.value);
  if (optionType === "boolean") return target.value === "true";
  const type = String(field?.type ?? field?.valueType ?? "").toLowerCase();
  return target?.type === "number" || ["number", "integer", "float", "double"].includes(type)
    ? Number(target.value) : target.value;
}

async function updateSceneSpecificationDraft(context, view, target, ops) {
  if (view.pending || !target) return null;
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  if (String(product?.templateId ?? "") !== "single-face-security-window") return null;
  const parameter = String(target.dataset?.tubeDesignerParameter ?? "").trim();
  const template = getTemplateById(designer.templates, product.templateId);
  const field = (template?.parameters ?? []).find((item) => (
    String(item?.key ?? item?.name ?? "") === parameter
  ));
  if (!parameter || !field) return null;
  const value = sceneSpecificationInputValue(target, field);
  if (target.type === "number" && !Number.isFinite(value)) {
    view.tubeDesignerSceneSpecificationEditorParameter = "";
    bindProductSpecificationAnnotations(resolveDesignerMount(context), view);
    return null;
  }
  const previous = {
    ...getDefaultParameters(designer.templates, product.templateId),
    ...(product.parameters ?? {}),
    ...(view.tubeDesignerRightDraft ?? {}),
  };
  const next = materializeProductToolDefaults(view, template, normalizeDependentParameters({
    ...previous,
    [parameter]: value,
  }, template, parameter));
  view.tubeDesignerLastEditedParameterKey = parameter;
  view.tubeDesignerSceneSpecificationEditorParameter = "";
  captureParameterPanelState(context, view, target);
  const parameterCommit = commitRightProductParameters(context, view, next, ops);
  // Closing an in-scene editor is a synchronous UI transition.  Do not leave
  // the input mounted while the product EC request is queued or in flight:
  // Enter and blur must immediately return the annotation to its normal
  // (clean or old -> new) label state.
  bindProductSpecificationAnnotations(resolveDesignerMount(context), view);
  await parameterCommit;
  view.tubeDesignerManufacturingPlans ??= {};
  view.tubeDesignerManufacturingPlans.right = null;

  const mount = resolveDesignerMount(context);
  const currentDesigner = view.scene?.tubeDesigner ?? designer;
  const content = renderDesignerRightParameterContent(currentDesigner, view);
  const runtimeStatus = mount?.querySelector?.("[data-tube-designer-runtime-status]");
  if (runtimeStatus) runtimeStatus.outerHTML = renderDesignerRuntimeStatus(view);
  refreshDesignerProductPartsDock(context, view);
  bindProductParameterDiagrams(mount);
  bindProductSpecificationAnnotations(mount, view);
  context.actions?.refreshProjectHistoryControls?.();
  return next;
}

function refreshRightParameterContent(context, designer, view) {
  const mount = resolveDesignerMount(context);
  const panel = mount?.querySelector?.("[data-tube-designer-parameter-form]");
  const scroller = panel?.querySelector?.("[data-tube-designer-parameter-scroll]");
  const runtimeStatus = mount?.querySelector?.("[data-tube-designer-runtime-status]");
  if (!panel || !scroller) return false;
  const content = renderDesignerRightParameterContent(designer, view);
  scroller.innerHTML = content.scrollContent;
  if (runtimeStatus) runtimeStatus.outerHTML = renderDesignerRuntimeStatus(view);
  refreshDesignerProductPartsDock(context, view);
  panel.dataset.tubeDesignerActiveParameter = String(view.tubeDesignerLastEditedParameterKey ?? "");
  bindProductParameterDiagrams(mount);
  bindProductSpecificationAnnotations(mount, view);
  return true;
}

function stableRuntimeParameterValue(value) {
  if (Array.isArray(value)) return value.map(stableRuntimeParameterValue);
  if (!value || typeof value !== "object") return value;
  return Object.fromEntries(Object.keys(value).sort()
    .filter((key) => value[key] !== undefined)
    .map((key) => [key, stableRuntimeParameterValue(value[key])]));
}

function rightRuntimeParameterFingerprint(view, values) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  const merged = {
    ...getDefaultParameters(designer.templates ?? [], product?.templateId),
    ...(values ?? {}),
  };
  return JSON.stringify(stableRuntimeParameterValue(merged));
}

function securityWindowManufacturingParameterKeys(view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const template = getTemplateById(designer.templates ?? [], designer.product?.templateId);
  if (String(template?.id ?? "") !== "single-face-security-window") return null;
  const sections = Array.isArray(template?.extensions?.parameterLayout?.sections)
    ? template.extensions.parameterLayout.sections : [];
  const groups = new Set(sections
    .filter((section) => ["materials", "process"].includes(String(section?.key ?? "")))
    .flatMap((section) => Array.isArray(section?.groups) ? section.groups : [])
    .map(String));
  return new Set((template?.parameters ?? [])
    .filter((field) => groups.has(String(field?.groupKey ?? field?.group ?? "")))
    .map((field) => String(field?.key ?? field?.name ?? ""))
    .filter(Boolean));
}

function rightRuntimeModelFingerprint(view, values) {
  const manufacturingKeys = securityWindowManufacturingParameterKeys(view);
  if (!manufacturingKeys) return rightRuntimeParameterFingerprint(view, values);
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  const merged = {
    ...getDefaultParameters(designer.templates ?? [], product?.templateId),
    ...(values ?? {}),
  };
  for (const key of manufacturingKeys) delete merged[key];
  delete merged.tubeDesignerProfileOverrides;
  delete merged.tubeDesignerToolBindings;
  return JSON.stringify(stableRuntimeParameterValue(merged));
}

function synchronizeRightRuntimeModel(view, product = view.scene?.tubeDesigner?.product, regenerated = false) {
  const productId = String(product?.entityId ?? "").trim();
  if (!productId) return;
  const versions = view.tubeDesignerInstanceRuntimeVersions ??= {};
  const previous = versions[productId];
  const fingerprint = rightRuntimeModelFingerprint(view, product.parameters ?? {});
  const manufacturingFingerprint = rightRuntimeParameterFingerprint(view, product.parameters ?? {});
  const modelChanged = previous && previous.modelFingerprint !== fingerprint;
  const modelVersion = previous
    ? regenerated || modelChanged
      ? Math.max(Number(previous.modelVersion ?? 1) + 1, Number(previous.parameterVersion ?? 1))
      : Number(previous.modelVersion ?? 1)
    : 1;
  const parameterVersion = modelVersion;
  versions[productId] = {
    modelVersion,
    parameterVersion,
    modelFingerprint: fingerprint,
    parameterFingerprint: manufacturingFingerprint,
    manufacturingFingerprint,
    synchronized: true,
  };
  view.tubeDesignerRightDraftDirty = product?.modelOutdated === true;
  view.tubeDesignerPartsDraftDirty = product?.partsOutdated === true
    && product?.modelOutdated !== true;
}

function restoreRightDraftForActiveProduct(view, product = view.scene?.tubeDesigner?.product) {
  const productId = String(product?.entityId ?? "").trim();
  if (!productId) {
    view.tubeDesignerRightDraft = {};
    view.tubeDesignerRightDraftDirty = false;
    view.tubeDesignerPartsDraftDirty = false;
    return;
  }
  synchronizeRightRuntimeModel(view, product, false);
  if (view.tubeDesignerRightDraftsByProductId)
    delete view.tubeDesignerRightDraftsByProductId[productId];
  view.tubeDesignerRightDraft = structuredCloneSafe(product.parameters ?? {});
}

function rightParameterHistorySnapshot(view, values = view.tubeDesignerRightDraft) {
  return {
    parameters: structuredCloneSafe(values ?? view.scene?.tubeDesigner?.product?.parameters ?? {}),
    presetSelections: structuredCloneSafe(view.tubeDesignerRightPresetSelections ?? {}),
    lastEditedParameterKey: String(view.tubeDesignerLastEditedParameterKey ?? ""),
  };
}

function rightParameterHistoryBucket(view, productId, create = false) {
  if (!productId) return null;
  if (create) view.tubeDesignerRightParameterHistoryByProductId ??= {};
  const histories = view.tubeDesignerRightParameterHistoryByProductId;
  if (!histories) return null;
  if (create) histories[productId] ??= { undo: [], redo: [] };
  return histories[productId] ?? null;
}

function clearRightParameterHistory(view, productId) {
  if (!productId || !view.tubeDesignerRightParameterHistoryByProductId) return;
  delete view.tubeDesignerRightParameterHistoryByProductId[productId];
}

export function clearDesignerRightParameterHistories(view, discardDrafts = false) {
  view.tubeDesignerRightParameterHistoryByProductId = {};
  if (!discardDrafts) return;
  view.tubeDesignerRightDraftsByProductId = {};
  view.tubeDesignerRightDraft = structuredCloneSafe(
    view.scene?.tubeDesigner?.product?.parameters ?? {},
  );
  view.tubeDesignerRightDraftDirty = false;
  view.tubeDesignerPartsDraftDirty = false;
}

export function getDesignerProjectHistoryState(view) {
  // Existing-instance parameters live in the product EC, so the app shell's
  // project history is the single undo/redo authority.
  return { canUndo: false, canRedo: false };
}

async function commitRightProductParameters(context, view, values, ops) {
  const productId = String(view.scene?.tubeDesigner?.product?.entityId ?? "").trim();
  if (!productId) return null;
  const nextValues = structuredCloneSafe(values ?? {});
  // Keep the just-edited value available while this change waits behind an
  // earlier EC commit. The queue preserves one change event per undo step.
  storeRightParameterDraft(view, nextValues, { recordHistory: false });
  // Expiration is derived from the local old/new pair, so it can be shown now.
  // Do not make the user wait for the native EC response before the status and
  // parts dock acknowledge the edit.
  const mount = resolveDesignerMount(context);
  const runtimeStatus = mount?.querySelector?.("[data-tube-designer-runtime-status]");
  if (runtimeStatus) runtimeStatus.outerHTML = renderDesignerRuntimeStatus(view);
  refreshDesignerProductPartsDock(context, view);

  const previousCommit = view.tubeDesignerProductParameterCommitPromise ?? Promise.resolve();
  const commit = previousCommit.catch(() => {}).then(async () => {
    // The native EC update may occupy the UI thread while it commits and builds
    // its response.  Let the annotation's local old/new state paint first so
    // the edited value turns red immediately instead of appearing to lag behind
    // the user's Enter/blur commit.
    await waitForPaint();
    const response = await invokeDesignerRequest(context, "TubeDesigner.UpdateProductParameters", {
      productEntityId: productId,
      parameters: nextValues,
    });
    const designer = response?.tubeDesigner;
    if (!designer?.product) throw new Error("产品参数未能写入实例。");
    view.scene ??= {};
    view.scene.tubeDesigner = designer;
    restoreLoadedProductTemplateDescriptors(view, designer);
    await ensureTemplateDescriptor(context, view, designer.product.templateId);
    const latestDraft = view.tubeDesignerRightDraftsByProductId?.[productId];
    const responseOwnsLatestDraft = latestDraft != null
      && rightRuntimeParameterFingerprint(view, latestDraft)
        === rightRuntimeParameterFingerprint(view, nextValues);
    if (responseOwnsLatestDraft) delete view.tubeDesignerRightDraftsByProductId[productId];
    clearRightParameterHistory(view, productId);
    // A previous queued response must never replace a newer value.  Keep the
    // newest per-instance draft until the EC response for that exact draft
    // arrives; this is the page-level counterpart of the annotation
    // component's oldValue/newValue pair.
    view.tubeDesignerRightDraft = structuredCloneSafe(responseOwnsLatestDraft
      ? designer.product.parameters ?? {}
      : latestDraft ?? designer.product.parameters ?? {});
    synchronizeRightRuntimeModel(view, designer.product, false);
    await acknowledgeOwnMutation(context, view);
    restoreParameterPanelState(context, view);
    context.actions?.refreshProjectHistoryControls?.();
    return response;
  });
  view.tubeDesignerProductParameterCommitPromise = commit;
  try {
    return await commit;
  } finally {
    if (view.tubeDesignerProductParameterCommitPromise === commit)
      view.tubeDesignerProductParameterCommitPromise = null;
  }
}

function storeRightParameterDraft(view, values, options = {}) {
  const recordHistory = options.recordHistory !== false;
  const productId = String(view.scene?.tubeDesigner?.product?.entityId ?? "").trim();
  const previousValues = structuredCloneSafe(
    view.tubeDesignerRightDraft ?? view.scene?.tubeDesigner?.product?.parameters ?? {},
  );
  const nextValues = structuredCloneSafe(values ?? {});
  const changed = rightRuntimeParameterFingerprint(view, previousValues)
    !== rightRuntimeParameterFingerprint(view, nextValues);
  if (productId && recordHistory && changed) {
    const history = rightParameterHistoryBucket(view, productId, true);
    history.undo.push(rightParameterHistorySnapshot(view, previousValues));
    if (history.undo.length > 100) history.undo.shift();
    history.redo = [];
  }
  view.tubeDesignerRightDraft = nextValues;
  if (productId) {
    view.tubeDesignerRightDraftsByProductId ??= {};
    view.tubeDesignerRightDraftsByProductId[productId] = structuredCloneSafe(nextValues);
  }
  const versions = view.tubeDesignerInstanceRuntimeVersions ??= {};
  const previous = versions[productId];
  const committedValues = view.scene?.tubeDesigner?.product?.parameters ?? {};
  const modelFingerprint = previous?.modelFingerprint
    ?? rightRuntimeModelFingerprint(view, committedValues);
  const committedManufacturingFingerprint = previous?.manufacturingFingerprint
    ?? rightRuntimeParameterFingerprint(view, committedValues);
  const draftModelFingerprint = rightRuntimeModelFingerprint(view, nextValues);
  const parameterFingerprint = rightRuntimeParameterFingerprint(view, nextValues);
  const modelVersion = Number(previous?.modelVersion ?? 1);
  const synchronized = draftModelFingerprint === modelFingerprint;
  const parameterVersion = synchronized
    ? modelVersion
    : previous?.parameterFingerprint === parameterFingerprint
      ? Number(previous?.parameterVersion ?? modelVersion + 1)
      : Math.max(Number(previous?.parameterVersion ?? modelVersion), modelVersion) + 1;
  if (productId) {
    versions[productId] = {
      modelVersion,
      parameterVersion,
      modelFingerprint,
      parameterFingerprint,
      manufacturingFingerprint: committedManufacturingFingerprint,
      synchronized,
    };
  }
  view.tubeDesignerRightDraftDirty = !synchronized;
  view.tubeDesignerPartsDraftDirty = synchronized
    && parameterFingerprint !== committedManufacturingFingerprint;
  view.tubeDesignerManufacturingPlans ??= {};
  view.tubeDesignerManufacturingPlans.right = null;
}

function refreshDesignerProductPartsDock(context, view) {
  const mount = resolveDesignerMount(context);
  const dock = mount?.querySelector?.(".tube-designer-product-parts-dock");
  if (!dock) return false;
  const html = renderDesignerProductPartsDock(context, view);
  const template = mount.ownerDocument?.createElement?.("template");
  if (!template) return false;
  template.innerHTML = html;
  const nextDock = template.content.querySelector?.(".tube-designer-product-parts-dock");
  if (!nextDock) return false;
  dock.replaceWith(nextDock);
  return true;
}

function restoreRightParameterHistory(context, view, direction, ops) {
  const productId = String(view.scene?.tubeDesigner?.product?.entityId ?? "").trim();
  if (view.pending || view.activeAreaId !== "view" || !productId) return false;
  const history = rightParameterHistoryBucket(view, productId, false);
  const source = direction === "redo" ? history?.redo : history?.undo;
  const destination = direction === "redo" ? history?.undo : history?.redo;
  const snapshot = source?.pop?.();
  if (!snapshot) return false;
  captureParameterPanelState(context, view);
  destination.push(rightParameterHistorySnapshot(view));
  if (destination.length > 100) destination.shift();
  view.tubeDesignerRightPresetSelections = structuredCloneSafe(snapshot.presetSelections ?? {});
  view.tubeDesignerLastEditedParameterKey = String(snapshot.lastEditedParameterKey ?? "");
  view.tubeDesignerRestoreParameterFocus = Boolean(view.tubeDesignerLastEditedParameterKey);
  storeRightParameterDraft(view, snapshot.parameters, { recordHistory: false });
  refreshStoredRightParameterDraft(context, view.scene?.tubeDesigner ?? {}, view, ops);
  context.actions?.refreshProjectHistoryControls?.();
  return true;
}

function refreshStoredRightParameterDraft(context, designer, view, ops) {
  if (!refreshRightParameterContent(context, designer, view)) ops.renderProject(context, view);
  restoreParameterPanelState(context, view);
  context.actions?.refreshProjectHistoryControls?.();
}

function selectParameterCategory(context, view, target, ops) {
  if (view.pending || !target) return;
  const category = String(target.dataset?.tubeDesignerParameterCategory ?? "").trim();
  if (!category || category === view.tubeDesignerParameterCategory) return;
  view.tubeDesignerParameterCategory = category;
  // Choosing a category is navigation, not an edit.  Start its independent
  // parameter list at the top instead of carrying an unrelated group's scroll.
  view.tubeDesignerParameterPanelScrollTop = 0;
  view.tubeDesignerParameterPanelScrollAnchor = null;
  view.tubeDesignerRestoreParameterFocus = false;
  ops.renderProject(context, view);
}

function productEditorContext(context, view, mode) {
  const designer = view.scene?.tubeDesigner ?? {};
  const isAdd = mode === "add";
  const templateId = isAdd ? view.tubeDesignerAddTemplateId : designer.product?.templateId;
  const template = getTemplateById(designer.templates ?? [], templateId);
  if (!template) throw new Error("当前产品模板不可用。");
  const formSelector = isAdd ? "[data-tube-designer-add-form]" : "[data-tube-designer-parameter-form]";
  const seed = isAdd
    ? { ...getDefaultParameters(designer.templates, templateId), ...(view.tubeDesignerAddDraft ?? {}) }
    : { ...getDefaultParameters(designer.templates, templateId), ...(designer.product?.parameters ?? {}), ...(view.tubeDesignerRightDraft ?? {}) };
  const values = collectParameters(resolveDesignerMount(context), formSelector, seed);
  const instanceQuantity = validateInstanceQuantity(isAdd
    ? view.tubeDesignerAddInstanceQuantity ?? 1
    : designer.product?.quantity ?? 1);
  return { designer, isAdd, template, values, formSelector, instanceQuantity };
}

function patchProductEditorStage(context, mode, stage) {
  const selector = mode === "add" ? "[data-tube-designer-add-form]" : "[data-tube-designer-parameter-form]";
  const root = resolveDesignerMount(context)?.querySelector?.(selector);
  if (!root) return false;
  for (const button of root.querySelectorAll?.(".tube-designer-product-editor-tabs button[data-tube-designer-editor-stage]") ?? []) {
    const selected = String(button.dataset?.tubeDesignerEditorStage ?? "") === stage;
    button.classList?.toggle?.("selected", selected);
    button.setAttribute?.("aria-selected", String(selected));
  }
  for (const section of root.querySelectorAll?.(".tube-designer-product-editor-stage[data-tube-designer-editor-stage]") ?? []) {
    section.hidden = String(section.dataset?.tubeDesignerEditorStage ?? "") !== stage;
  }
  return true;
}

function patchManufacturingPlanPanel(context, mode, state, fingerprint) {
  const selector = mode === "add" ? "[data-tube-designer-add-form]" : "[data-tube-designer-parameter-form]";
  const root = resolveDesignerMount(context)?.querySelector?.(selector);
  const panel = root?.querySelector?.('.tube-designer-product-editor-stage[data-tube-designer-editor-stage="manufacturing"]');
  if (!panel) return false;
  panel.innerHTML = renderProductManufacturingPlan(state, { mode, fingerprint });
  const label = root.querySelector?.('.tube-designer-product-editor-tabs button[data-tube-designer-editor-stage="manufacturing"] small');
  if (label) {
    label.textContent = state?.fingerprint !== fingerprint ? "待计算"
      : state?.status === "loading" ? "计算中"
        : state?.status === "ready" ? "已计算"
          : state?.status === "error" ? "需处理" : "待计算";
  }
  return true;
}

function currentProductManufacturingPlanFingerprint(context, view, mode) {
  try {
    const { template, values, instanceQuantity } = productEditorContext(context, view, mode);
    return productManufacturingPlanFingerprint(template, values, instanceQuantity);
  } catch {
    return "";
  }
}

async function selectProductEditorStage(context, view, target) {
  const mode = String(target?.dataset?.tubeDesignerEditorMode ?? "") === "add" ? "add" : "right";
  const stage = String(target?.dataset?.tubeDesignerEditorStage ?? "") === "manufacturing" ? "manufacturing" : "parameters";
  if (mode === "add") view.tubeDesignerAddEditorStage = stage;
  else view.tubeDesignerRightEditorStage = stage;
  patchProductEditorStage(context, mode, stage);
  if (stage !== "manufacturing") return;
  const { template, values, instanceQuantity } = productEditorContext(context, view, mode);
  const fingerprint = productManufacturingPlanFingerprint(template, values, instanceQuantity);
  const state = view.tubeDesignerManufacturingPlans?.[mode];
  if (state?.fingerprint === fingerprint && ["loading", "ready"].includes(state.status)) return;
  await refreshProductManufacturingPlan(context, view, target);
}

async function refreshProductManufacturingPlan(context, view, target) {
  const mode = String(target?.dataset?.tubeDesignerEditorMode ?? "") === "add" ? "add" : "right";
  const { template, values, instanceQuantity } = productEditorContext(context, view, mode);
  const fingerprint = productManufacturingPlanFingerprint(template, values, instanceQuantity);
  const request = {};
  view.tubeDesignerManufacturingPlans ??= {};
  const loading = { status: "loading", fingerprint, request };
  view.tubeDesignerManufacturingPlans[mode] = loading;
  patchManufacturingPlanPanel(context, mode, loading, fingerprint);
  try {
    const response = await context.sceneProxy.invoke("TubeDesigner.GetProductManufacturingPlan", {
      templateId: template.id,
      templateVersion: template.version,
      parameters: values,
      instanceQuantity,
    }, { timeoutMs: 120000 });
    if (view.tubeDesignerManufacturingPlans?.[mode]?.request !== request) return;
    const ready = { status: "ready", fingerprint, response };
    view.tubeDesignerManufacturingPlans[mode] = ready;
    const currentFingerprint = currentProductManufacturingPlanFingerprint(context, view, mode);
    if (currentFingerprint) patchManufacturingPlanPanel(context, mode, ready, currentFingerprint);
  } catch (error) {
    if (view.tubeDesignerManufacturingPlans?.[mode]?.request !== request) return;
    const failed = { status: "error", fingerprint, error: error?.message ?? String(error) };
    view.tubeDesignerManufacturingPlans[mode] = failed;
    const currentFingerprint = currentProductManufacturingPlanFingerprint(context, view, mode);
    if (currentFingerprint) patchManufacturingPlanPanel(context, mode, failed, currentFingerprint);
  }
}

function focusProductParameter(context, view, target) {
  const mode = String(target?.dataset?.tubeDesignerEditorMode ?? "") === "add" ? "add" : "right";
  const parameter = String(target?.dataset?.tubeDesignerParameterKey ?? "").trim();
  if (!parameter) return;
  const selector = mode === "add" ? "[data-tube-designer-add-form]" : "[data-tube-designer-parameter-form]";
  const root = resolveDesignerMount(context)?.querySelector?.(selector);
  const field = Array.from(root?.querySelectorAll?.("[data-tube-designer-parameter]") ?? [])
    .find((item) => String(item.dataset?.tubeDesignerParameter ?? "") === parameter);
  if (!field) return;
  for (let element = field.parentElement; element && element !== root; element = element.parentElement) {
    if (element.tagName === "DETAILS") element.open = true;
  }
  if (mode === "add") {
    const templateId = String(root?.dataset?.tubeDesignerRenderedTemplateId ?? view.tubeDesignerAddTemplateId ?? "");
    view.tubeDesignerAddDisclosureStates ??= {};
    const states = { ...(view.tubeDesignerAddDisclosureStates[templateId] ?? {}) };
    for (const group of root?.querySelectorAll?.("details[data-tube-designer-parameter-group]") ?? []) {
      states[group.dataset.tubeDesignerParameterGroup] = Boolean(group.open);
    }
    view.tubeDesignerAddDisclosureStates[templateId] = states;
  } else {
    const disclosure = { ...(view.tubeDesignerParameterDisclosureState ?? {}) };
    for (const group of root?.querySelectorAll?.("details[data-tube-designer-parameter-group]") ?? []) {
      disclosure[group.dataset.tubeDesignerParameterGroup] = Boolean(group.open);
    }
    view.tubeDesignerParameterDisclosureState = disclosure;
    view.tubeDesignerParameterPanelProductId = String(view.scene?.tubeDesigner?.product?.entityId ?? "");
    view.tubeDesignerExpandedParameterGroups = Object.keys(disclosure).filter((key) => disclosure[key]);
  }
  view.tubeDesignerLastEditedParameterKey = parameter;
  field.focus?.({ preventScroll: true });
  field.scrollIntoView?.({ block: "center", inline: "nearest" });
  bindProductSpecificationAnnotations(resolveDesignerMount(context), view);
}

function profileActionState(view, mode) {
  const designer = view.scene?.tubeDesigner ?? {};
  const isAdd = mode === "add";
  const templateId = isAdd ? view.tubeDesignerAddTemplateId : designer.product?.templateId;
  const values = isAdd
    ? { ...getDefaultParameters(designer.templates, templateId), ...(view.tubeDesignerAddDraft ?? {}) }
    : { ...getDefaultParameters(designer.templates, templateId), ...(designer.product?.parameters ?? {}), ...(view.tubeDesignerRightDraft ?? {}) };
  return { designer, isAdd, templateId, values };
}

function productToolActionState(view, mode, fieldKey) {
  const state = profileActionState(view, mode);
  const template = getTemplateById(state.designer.templates ?? [], state.templateId);
  const field = (template?.parameters ?? []).find((item) =>
    String(item?.key ?? item?.name ?? "") === String(fieldKey ?? "") && isProductToolField(item));
  return { ...state, template, field };
}

async function setProductToolDraft(context, view, ops, mode, field, binding) {
  const { designer, isAdd, values } = productToolActionState(
    view, mode, field?.key ?? field?.name,
  );
  const role = productToolRole(field);
  const bindings = { ...(values.tubeDesignerToolBindings ?? {}) };
  if (binding) bindings[role] = binding;
  else delete bindings[role];
  values.tubeDesignerToolBindings = bindings;
  values[String(field?.key ?? field?.name ?? "")] = binding?.selectionKey ?? "";
  if (isAdd) {
    view.tubeDesignerAddDraft = values;
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
  } else {
    await commitRightProductParameters(context, view, values, ops);
  }
}

async function changeProductToolSelection(context, view, target, ops) {
  if (view.pending || !target) return null;
  const mode = String(target.dataset?.tubeDesignerToolMode ?? "") === "add" ? "add" : "right";
  const fieldKey = String(target.dataset?.tubeDesignerToolField ?? "").trim();
  const { template, field } = productToolActionState(view, mode, fieldKey);
  if (!template || !field) return null;
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  const selection = String(target.value ?? "");
  if (!selection) {
    await setProductToolDraft(context, view, ops, mode, field, null);
    markSelectedPresetModified(view, mode, template, fieldKey);
    return null;
  }
  const tool = findProductTool(view, template, field, selection);
  if (!tool) {
    view.error = "所选模具不满足当前产品角色的来源、类别或加工位置约束。";
    ops.renderProject(context, view);
    if (mode === "add") restoreAddDialogScrollAnchor(context, view);
    else restoreParameterPanelState(context, view);
    return null;
  }
  await setProductToolDraft(context, view, ops, mode, field, makeProductToolBinding(field, tool, null, template));
  markSelectedPresetModified(view, mode, template, fieldKey);
  return tool;
}

function productToolParameterValue(target, definition) {
  const valueType = String(definition?.valueType ?? "number");
  let value;
  if (valueType === "boolean") value = Boolean(target?.checked);
  else if (valueType === "number" || valueType === "integer") {
    value = Number(target?.value);
    if (!Number.isFinite(value)) throw new Error("请输入有效的模具参数。");
    if (valueType === "integer" && !Number.isInteger(value)) throw new Error("该模具参数必须是整数。");
    if (definition?.min != null && value < Number(definition.min)) throw new Error("模具参数小于允许范围。");
    if (definition?.max != null && value > Number(definition.max)) throw new Error("模具参数大于允许范围。");
  } else value = String(target?.value ?? "");
  const options = Array.isArray(definition?.options)
    ? definition.options.map((option) => typeof option === "object" ? option.value : option) : [];
  if (options.length && !options.some((option) => String(option) === String(value))) {
    throw new Error("该值不是模具允许的选项。");
  }
  return value;
}

async function changeProductToolParameter(context, view, target, ops) {
  if (view.pending || !target) return null;
  const mode = String(target.dataset?.tubeDesignerToolMode ?? "") === "add" ? "add" : "right";
  const fieldKey = String(target.dataset?.tubeDesignerToolField ?? "").trim();
  const parameterKey = String(target.dataset?.tubeDesignerToolParameter ?? "").trim();
  const { template, field, values } = productToolActionState(view, mode, fieldKey);
  let binding = productToolBinding(values, field);
  if (!template || !field || !parameterKey) return null;
  if (!binding) {
    const selection = String(values[field.key ?? field.name] ?? "");
    const selectedTool = findProductTool(view, template, field, selection);
    if (!selectedTool) return null;
    binding = makeProductToolBinding(field, selectedTool, null, template);
  }
  const tool = findProductTool(view, template, field, binding.selectionKey);
  const definition = productToolDefinitions(binding, tool)
    .find((item) => String(item?.key ?? "") === parameterKey && item?.derived !== true);
  if (!definition) return null;
  let value;
  try {
    value = productToolParameterValue(target, definition);
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops.renderProject(context, view);
    return null;
  }
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  const next = {
    ...binding,
    parameters: { ...(binding.parameters ?? {}), [parameterKey]: value },
  };
  await setProductToolDraft(context, view, ops, mode, field, next);
  markSelectedPresetModified(view, mode, template, fieldKey);
  return next;
}

function importedProfileSnapshot(profile, savedProfileId = "") {
  if (!profile || typeof profile !== "object") return null;
  const { id, ownerScope, revision, createdAt, updatedAt, savedProfileId: _oldSavedId, ...value } = profile;
  return {
    ...structuredCloneSafe(value),
    ...(savedProfileId ? { savedProfileId } : {}),
  };
}

function structuredCloneSafe(value) {
  if (typeof globalThis.structuredClone === "function") return globalThis.structuredClone(value);
  return JSON.parse(JSON.stringify(value));
}

async function setProfileDraft(context, view, ops, mode, prefix, profile, builtInValue = null, parameterKey = "") {
  const { designer, isAdd, values } = profileActionState(view, mode);
  const overrides = { ...(values.tubeDesignerProfileOverrides ?? {}) };
  if (profile) overrides[prefix] = profile;
  else delete overrides[prefix];
  values.tubeDesignerProfileOverrides = overrides;
  if (builtInValue != null) values[parameterKey || `${prefix}ProfileType`] = builtInValue;
  if (isAdd) {
    view.tubeDesignerAddDraft = values;
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
  } else {
    await commitRightProductParameters(context, view, values, ops);
  }
}

async function changeProfileSelection(context, view, target, ops) {
  if (view.pending || !target) return;
  const mode = String(target.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  const prefix = String(target.dataset?.tubeDesignerProfilePrefix ?? "").trim();
  const parameterKey = String(target.dataset?.tubeDesignerProfileParameter ?? "").trim();
  const selection = String(target.value ?? "");
  if (!prefix || selection === "current") return;
  const actionState = profileActionState(view, mode);
  const template = getTemplateById(actionState.designer.templates ?? [], actionState.templateId);
  const profileField = (template?.parameters ?? []).find((field) => {
    const key = String(field?.key ?? field?.name ?? "");
    return (parameterKey ? key === parameterKey : productProfileRole(field) === prefix)
      && productProfileRole(field) === prefix;
  });
  if (selection === "external-dxf") {
    // A file-picker action is not a profile value. Restore immediately so
    // cancelling or failing an import leaves the original selection intact.
    target.value = String(target.dataset?.tubeDesignerProfileCurrentSelection ?? "current");
    return importProfileDxf(context, view, target, ops);
  }
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  if (selection.startsWith("builtin:")) {
    await setProfileDraft(context, view, ops, mode, prefix, null, selection.slice("builtin:".length), parameterKey);
    return;
  }
  if (selection.startsWith("system:")) {
    const systemProfile = profileField
      ? findProductProfile(view, template, profileField, selection)
      : libraryProfiles(view).find((item) => profileScope(item) === "system" && profileSelectionKey(item) === selection);
    if (!systemProfile || systemProfile.available === false) {
      view.error = "所选系统管型当前不可用，或不满足此构件角色的约束。";
      ops.renderProject(context, view);
      return;
    }
    const source = systemProfile.previewProfile ?? systemProfile.profile;
    const templateDefaults = productResourceParameterDefaults(
      productProfileRoleDeclaration(template, profileField), selection,
    );
    const parameters = {
      ...(systemProfile.defaultParameters ?? source?.parameters ?? {}),
      ...templateDefaults,
    };
    let snapshot = importedProfileSnapshot({
      ...(source ?? {}),
      name: profileName(systemProfile),
      profileScope: "system",
      profileDefinitionId: String(systemProfile.id ?? ""),
      profileForm: source?.profileForm ?? systemProfile.profileForm ?? systemProfile.descriptor?.profileForm,
      parameterDefinitions: systemProfile.descriptor?.parameters ?? source?.parameterDefinitions ?? [],
      parameters,
    });
    if (!snapshot?.contours?.length
        || (source?.profileForm === "parametric" && Object.keys(templateDefaults).length)) {
      const response = await invokeProductRequest(context, "TubeDesigner.EvaluateProfilePackage", {
        profileRef: profileRef(systemProfile),
        parameters,
      }, { timeoutMs: 120000 });
      snapshot = importedProfileSnapshot(response?.profile);
    }
    if (!snapshot?.contours?.length) {
      view.error = "所选系统管型缺少可用截面。";
      ops.renderProject(context, view);
      return;
    }
    await setProfileDraft(context, view, ops, mode, prefix, snapshot);
    return;
  }
  if (selection.startsWith("template:")) {
    const { templateId } = actionState;
    const bundled = profileField
      ? findProductProfile(view, template, profileField, selection)
      : templateProfilesForProduct(view, templateId).find((item) => profileSelectionKey(item) === selection);
    if (!bundled) {
      view.error = "所选管型不属于当前模板，或模板资源已经不存在。";
      ops.renderProject(context, view);
      return;
    }
    const source = bundled.previewProfile ?? bundled.profile ?? {};
    const templateDefaults = productResourceParameterDefaults(
      productProfileRoleDeclaration(template, profileField), selection,
    );
    let snapshot = importedProfileSnapshot({
      ...source,
      name: bundled.name ?? bundled.previewProfile?.name,
      profileScope: "template", templateId: bundled.templateId, profileDefinitionId: bundled.id,
      parameters: { ...(source?.parameters ?? {}), ...templateDefaults },
    });
    if (source?.profileForm === "parametric" && Object.keys(templateDefaults).length) {
      const response = await invokeProductRequest(context, "TubeDesigner.EvaluateProfilePackage", {
        profileRef: { scope: "template", templateId, id: bundled.id },
        parameters: snapshot?.parameters ?? templateDefaults,
      }, { timeoutMs: 120000 });
      snapshot = importedProfileSnapshot(response?.profile);
      if (snapshot) Object.assign(snapshot, {
        profileScope: "template", templateId, profileDefinitionId: bundled.id,
      });
    }
    if (!snapshot?.contours?.length) {
      view.error = "模板自带管型缺少可用截面。";
      ops.renderProject(context, view);
      return;
    }
    await setProfileDraft(context, view, ops, mode, prefix, snapshot);
    return;
  }
  if (selection.startsWith("saved:")) {
    const id = selection.slice("saved:".length);
    const saved = (view.tubeDesignerUserData?.profiles ?? [])
      .find((item) => String(item?.id ?? "") === id);
    const allowed = !profileField || findProductProfile(
      view, template, profileField, `user:${id}`,
    );
    if (!saved || !allowed) {
      view.error = "所选的我的管型已经不存在，或不满足此构件角色的约束。";
      ops.renderProject(context, view);
      return;
    }
    const source = { ...(saved.previewProfile ?? saved), name: saved.name ?? saved.previewProfile?.name };
    const templateDefaults = productResourceParameterDefaults(
      productProfileRoleDeclaration(template, profileField), selection,
    );
    let snapshot = importedProfileSnapshot({
      ...source, parameters: { ...(source?.parameters ?? {}), ...templateDefaults },
    }, id);
    if (source?.profileForm === "parametric" && Object.keys(templateDefaults).length) {
      const response = await invokeProductRequest(context, "TubeDesigner.EvaluateProfilePackage", {
        profileRef: { scope: "user", id },
        parameters: snapshot?.parameters ?? templateDefaults,
      }, { timeoutMs: 120000 });
      snapshot = importedProfileSnapshot(response?.profile, id);
      if (snapshot) Object.assign(snapshot, { profileScope: "user", profileDefinitionId: id });
    }
    if (!snapshot?.contours?.length) {
      view.error = "所选管型缺少可用截面。";
      ops.renderProject(context, view);
      return;
    }
    await setProfileDraft(context, view, ops, mode, prefix, snapshot);
  }
}

function profileParameterValue(target, definition) {
  const valueType = String(definition?.valueType ?? "number");
  if (valueType === "boolean") return Boolean(target?.checked);
  if (valueType === "number" || valueType === "integer") {
    const number = Number(target?.value);
    if (!Number.isFinite(number)) throw new Error("请输入有效的管型参数。");
    if (valueType === "integer" && !Number.isInteger(number)) {
      throw new Error("该管型参数必须是整数。");
    }
    return number;
  }
  return String(target?.value ?? "");
}

async function updateParametricProfile(context, view, target, ops) {
  if (view.pending || !target) return null;
  const mode = String(target.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  const prefix = String(target.dataset?.tubeDesignerProfilePrefix ?? "").trim();
  const parameterKey = String(target.dataset?.tubeDesignerProfileParameter ?? "").trim();
  const { values, templateId } = profileActionState(view, mode);
  const current = values.tubeDesignerProfileOverrides?.[prefix];
  const currentScope = String(current?.profileScope ?? "");
  const bundled = currentScope === "template";
  const system = currentScope === "system";
  if (!prefix || !parameterKey || current?.profileForm !== "parametric"
      || (bundled ? !current.profileDefinitionId || String(current.templateId) !== templateId
        : system ? !current.profileDefinitionId : !current?.savedProfileId)) {
    return null;
  }
  const definition = (current.parameterDefinitions ?? [])
    .find((item) => String(item?.key ?? "") === parameterKey);
  if (!definition) return null;
  let parameterValue;
  try {
    parameterValue = profileParameterValue(target, definition);
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops.renderProject(context, view);
    return null;
  }
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  const parameters = { ...(current.parameters ?? {}), [parameterKey]: parameterValue };
  const startedAt = nowMilliseconds();
  const result = await runDesignerOperation(context, view, ops, async () => {
    try {
      const response = await invokeProductRequest(context, "TubeDesigner.EvaluateProfilePackage", {
        ...(bundled
          ? { profileRef: { scope: "template", templateId, id: current.profileDefinitionId } }
          : system
            ? { profileRef: { scope: "system", id: current.profileDefinitionId } }
            : { profileRef: { scope: "user", id: current.savedProfileId } }),
        parameters,
      }, { timeoutMs: 120000 });
      const profile = importedProfileSnapshot(response?.profile, current.savedProfileId);
      if (!profile?.contours?.length) throw new Error("程式管型没有返回有效截面。" );
      const nextValues = profileActionState(view, mode).values;
      nextValues.tubeDesignerProfileOverrides = {
        ...(nextValues.tubeDesignerProfileOverrides ?? {}),
        [prefix]: profile,
      };
      if (mode === "add") view.tubeDesignerAddDraft = nextValues;
      else await commitRightProductParameters(context, view, nextValues, ops);
      return profile;
    } finally {
      await waitForMinimumDuration(startedAt, ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS);
    }
  }, {
    operation: {
      kind: "profile-evaluate",
      title: "正在更新管型",
      phase: "evaluating-profile",
      phaseLabel: "重新计算截面",
      message: "正在按新的参数生成精确轮廓",
    },
  });
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
  return result;
}

async function importProfileDxf(context, view, target, ops) {
  if (view.pending) return null;
  const mode = String(target?.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  const prefix = String(target?.dataset?.tubeDesignerProfilePrefix ?? "").trim();
  if (!prefix) return null;
  const bridge = context.appProxy?.bridge
    ?? context.productProxy?.bridge
    ?? context.sceneProxy?.bridge
    ?? null;
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力。";
    ops.renderProject(context, view);
    return null;
  }
  const sourcePath = String(await bridge.openFileDialog({
    title: "选择管型截面 DXF",
    filters: [{ name: "DXF 二维截面", extensions: ["dxf"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  const startedAt = nowMilliseconds();
  const result = await runDesignerOperation(context, view, ops, async () => {
    try {
      const response = await invokeProductRequest(
        context, "TubeDesigner.ImportProfileDxf", { sourcePath }, { timeoutMs: 120000 },
      );
      const profile = importedProfileSnapshot(response?.profile);
      if (!profile?.contours?.length) throw new Error("DXF 导入没有返回有效截面轮廓。" );
      const { values } = profileActionState(view, mode);
      const overrides = { ...(values.tubeDesignerProfileOverrides ?? {}), [prefix]: profile };
      values.tubeDesignerProfileOverrides = overrides;
      if (mode === "add") view.tubeDesignerAddDraft = values;
      else await commitRightProductParameters(context, view, values, ops);
      ops.showNotice(context, view, `已为当前实例导入 ${profile.name ?? profile.sourceFileName ?? "定式管型"}。`);
      return profile;
    } finally {
      await waitForMinimumDuration(startedAt, ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS);
    }
  }, {
    operation: {
      kind: "profile-import",
      title: "正在导入 定式管型",
      phase: "parsing-profile",
      phaseLabel: "截面校验",
      message: "正在识别外轮廓、内孔、单位和精确曲线",
    },
  });
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
  return result;
}

async function clearImportedProfile(context, view, target, ops) {
  if (view.pending) return;
  const mode = String(target?.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  const prefix = String(target?.dataset?.tubeDesignerProfilePrefix ?? "").trim();
  if (!prefix) return;
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  return setProfileDraft(context, view, ops, mode, prefix, null);
}

function openImportedProfileDialog(context, view, target, ops) {
  if (view.pending) return;
  const mode = String(target?.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  const prefix = String(target?.dataset?.tubeDesignerProfilePrefix ?? "").trim();
  const { values } = profileActionState(view, mode);
  const profile = importedProfileSnapshot(values.tubeDesignerProfileOverrides?.[prefix]);
  if (!profile || profile.profileScope === "template") return;
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  const savedProfileId = String(target?.dataset?.tubeDesignerProfileId ?? profile.savedProfileId ?? "");
  const saved = (view.tubeDesignerUserData?.profiles ?? [])
    .find((item) => String(item?.id ?? "") === savedProfileId);
  view.tubeDesignerProfileDialog = {
    mode, prefix, profile,
    savedProfileId: String(saved?.id ?? ""),
    name: String(saved?.name ?? profile.name ?? ""),
  };
  ops.renderProject(context, view);
  queueMicrotask(() => resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-profile-name]")?.focus?.());
}

function closeImportedProfileDialog(context, view, ops) {
  const mode = view.tubeDesignerProfileDialog?.mode === "add" ? "add" : "right";
  view.tubeDesignerProfileDialog = null;
  ops.renderProject(context, view);
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
}

function openImportedProfileLibrary(context, view, target, ops) {
  if (view.pending) return;
  const mode = String(target?.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  view.tubeDesignerProfileLibraryDialog = { mode };
  view.error = "";
  ops.renderProject(context, view);
}

function closeImportedProfileLibrary(context, view, ops) {
  const mode = view.tubeDesignerProfileLibraryDialog?.mode === "add" ? "add" : "right";
  view.tubeDesignerProfileLibraryDialog = null;
  ops.renderProject(context, view);
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
}

function replaceSavedProfileReferences(values, profileId, replacement) {
  if (!values || typeof values !== "object") return values;
  const overrides = values.tubeDesignerProfileOverrides;
  if (!overrides || typeof overrides !== "object" || Array.isArray(overrides)) return values;
  let changed = false;
  const nextOverrides = Object.fromEntries(Object.entries(overrides).map(([prefix, profile]) => {
    if (String(profile?.savedProfileId ?? "") !== profileId) return [prefix, profile];
    changed = true;
    return [prefix, replacement
      ? importedProfileSnapshot(replacement, profileId)
      : importedProfileSnapshot(profile)];
  }));
  return changed ? { ...values, tubeDesignerProfileOverrides: nextOverrides } : values;
}

function synchronizeSavedProfileReferences(view, profileId, replacement) {
  view.tubeDesignerAddDraft = replaceSavedProfileReferences(
    view.tubeDesignerAddDraft, profileId, replacement,
  );
  view.tubeDesignerRightDraft = replaceSavedProfileReferences(
    view.tubeDesignerRightDraft, profileId, replacement,
  );
}

async function renameLibraryProfile(context, view, target, ops) {
  if (view.pending) return null;
  const id = String(target?.dataset?.tubeDesignerProfileId ?? "");
  const existing = (view.tubeDesignerUserData?.profiles ?? [])
    .find((item) => String(item?.id ?? "") === id);
  const row = target?.closest?.("[data-tube-designer-library-profile-row]");
  const input = row?.querySelector?.("[data-tube-designer-library-profile-name]");
  const name = String(input?.value ?? "").trim();
  if (!existing) return null;
  if (!name) {
    view.error = "请填写管型名称。";
    input?.focus?.();
    return null;
  }
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.RenameProfile", {
      id,
      revision: Number(existing.revision ?? 0),
      name,
    });
    const saved = response?.profile;
    if (!saved?.id) throw new Error("重命名后没有返回管型记录。" );
    upsertUserDataItem(view, "profiles", saved);
    synchronizeSavedProfileReferences(view, id, saved);
    ops.showNotice(context, view, `已重命名为“${saved.name}”。`);
    return saved;
  }, {
    operation: {
      kind: "profile-rename",
      title: "正在重命名管型",
      phase: "saving-profile-name",
      phaseLabel: "保存名称",
      message: `正在更新“${existing.name}”的名称`,
    },
  });
}

async function deleteLibraryProfile(context, view, target, ops) {
  if (view.pending) return null;
  const id = String(target?.dataset?.tubeDesignerProfileId ?? "");
  const existing = (view.tubeDesignerUserData?.profiles ?? [])
    .find((item) => String(item?.id ?? "") === id);
  if (!existing) return null;
  if (typeof globalThis.confirm === "function" && !globalThis.confirm(
    `确定删除“${existing.name}”吗？\n已经使用该管型的产品不会受影响。`,
  )) return null;
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeProductRequest(context, "TubeDesigner.DeleteProfile", {
      id,
      revision: Number(existing.revision ?? 0),
    });
    if (!response?.deleted) throw new Error("我的管型未能删除。" );
    view.tubeDesignerUserData.profiles = view.tubeDesignerUserData.profiles
      .filter((item) => String(item?.id ?? "") !== id);
    synchronizeSavedProfileReferences(view, id, null);
    ops.showNotice(context, view, `已删除“${existing.name}”；已使用它的产品仍保留截面副本。`);
    return response;
  }, {
    operation: {
      kind: "profile-delete",
      title: "正在删除管型",
      phase: "deleting-profile",
      phaseLabel: "删除记录",
      message: `正在从我的管型中删除“${existing.name}”`,
    },
  });
}

async function saveImportedProfile(context, view, target, ops) {
  if (view.pending) return null;
  const state = view.tubeDesignerProfileDialog;
  if (!state?.profile || state.profile.profileScope === "template") return null;
  const mount = resolveDesignerMount(context);
  const name = String(mount?.querySelector?.("[data-tube-designer-profile-name]")?.value ?? "").trim();
  if (!name) {
    view.error = "请填写管型名称。";
    mount?.querySelector?.("[data-tube-designer-profile-name]")?.focus?.();
    return null;
  }
  const id = String(target?.dataset?.tubeDesignerProfileId ?? state.savedProfileId ?? "");
  const existing = (view.tubeDesignerUserData?.profiles ?? [])
    .find((item) => String(item?.id ?? "") === id);
  state.name = name;
  try {
    return await runDesignerOperation(context, view, ops, async () => {
      const response = await invokeProductRequest(context, "TubeDesigner.SaveImportedProfile", {
        id: existing?.id ?? "",
        revision: Number(existing?.revision ?? 0),
        name,
        profile: importedProfileSnapshot(state.profile),
      });
      const saved = response?.profile;
      if (!saved?.id) throw new Error("保存我的管型后没有返回记录标识。" );
      upsertUserDataItem(view, "profiles", saved);
      const snapshot = importedProfileSnapshot(saved, saved.id);
      const { values } = profileActionState(view, state.mode);
      values.tubeDesignerProfileOverrides = {
        ...(values.tubeDesignerProfileOverrides ?? {}),
        [state.prefix]: snapshot,
      };
      if (state.mode === "add") view.tubeDesignerAddDraft = values;
      else await commitRightProductParameters(context, view, values, ops);
      view.tubeDesignerProfileDialog = null;
      ops.showNotice(context, view, existing ? "我的管型名称已更新。" : "已保存到我的管型。" );
      return saved;
    }, {
      operation: {
        kind: "profile-save",
        title: existing ? "正在更新我的管型" : "正在保存我的管型",
        phase: "saving-profile",
        phaseLabel: "保存截面",
        message: `正在保存“${name}”的轮廓与来源信息`,
      },
    });
  } finally {
    if (state.mode === "add") restoreAddDialogScrollAnchor(context, view);
    else restoreParameterPanelState(context, view);
  }
}

async function deleteImportedProfile(context, view, target, ops) {
  if (view.pending) return null;
  const state = view.tubeDesignerProfileDialog;
  const id = String(target?.dataset?.tubeDesignerProfileId ?? state?.savedProfileId ?? "");
  const existing = (view.tubeDesignerUserData?.profiles ?? [])
    .find((item) => String(item?.id ?? "") === id);
  if (!state?.profile || !existing) return null;
  if (typeof globalThis.confirm === "function" && !globalThis.confirm(
    `确定删除“${existing.name}”吗？\n当前实例仍会保留截面副本。`,
  )) return null;
  try {
    return await runDesignerOperation(context, view, ops, async () => {
      const response = await invokeProductRequest(context, "TubeDesigner.DeleteImportedProfile", {
        id, revision: Number(existing.revision ?? 0),
      });
      if (!response?.deleted) throw new Error("我的管型未能删除。" );
      view.tubeDesignerUserData.profiles = view.tubeDesignerUserData.profiles
        .filter((item) => String(item?.id ?? "") !== id);
      const snapshot = importedProfileSnapshot(state.profile);
      const { values } = profileActionState(view, state.mode);
      values.tubeDesignerProfileOverrides = {
        ...(values.tubeDesignerProfileOverrides ?? {}),
        [state.prefix]: snapshot,
      };
      if (state.mode === "add") view.tubeDesignerAddDraft = values;
      else await commitRightProductParameters(context, view, values, ops);
      view.tubeDesignerProfileDialog = null;
      ops.showNotice(context, view, "已从我的管型删除；当前实例仍保留截面副本。" );
      return response;
    }, {
      operation: {
        kind: "profile-delete",
        title: "正在删除我的管型",
        phase: "deleting-profile",
        phaseLabel: "删除记录",
        message: `正在删除“${existing.name}”；当前实例的截面副本会保留`,
      },
    });
  } finally {
    if (state.mode === "add") restoreAddDialogScrollAnchor(context, view);
    else restoreParameterPanelState(context, view);
  }
}

function parameterPresetSelectionKey(mode) {
  return mode === "add" ? "tubeDesignerAddPresetSelections" : "tubeDesignerRightPresetSelections";
}

function parameterPresetSelection(view, mode, scopeKey) {
  const key = String(scopeKey ?? "").trim() || "default";
  return String(view?.[parameterPresetSelectionKey(mode)]?.[key] ?? "");
}

function setParameterPresetSelection(view, mode, scopeKey, selection) {
  const stateKey = parameterPresetSelectionKey(mode);
  const key = String(scopeKey ?? "").trim() || "default";
  view[stateKey] = { ...(view[stateKey] ?? {}), [key]: String(selection ?? "") };
}

function presetRecordScopeKey(template, preset) {
  return String(preset?.scopeKey ?? getDefaultParameterPresetScopeKey(template)).trim();
}

async function applyParameterPreset(context, view, target, ops) {
  if (view.pending || !target) return;
  const mode = String(target?.dataset?.tubeDesignerPresetMode ?? "") === "add" ? "add" : "right";
  const scopeKey = String(target?.dataset?.tubeDesignerPresetScope ?? "").trim();
  const designer = view.scene?.tubeDesigner ?? {};
  const templateId = mode === "add" ? view.tubeDesignerAddTemplateId : designer.product?.templateId;
  const template = getTemplateById(designer.templates, templateId);
  if (!template) return;
  const selection = String(target.value ?? "custom");
  const currentValues = mode === "add"
    ? { ...getDefaultParameters(designer.templates, templateId), ...(view.tubeDesignerAddDraft ?? {}) }
    : { ...getDefaultParameters(designer.templates, templateId), ...(designer.product?.parameters ?? {}), ...(view.tubeDesignerRightDraft ?? {}) };
  let values = currentValues;
  const definition = template?.extensions?.parameterPresets ?? {};
  const selector = String(definition?.selectorParameter ?? "").trim();
  const builtInScopeKey = getDefaultParameterPresetScopeKey(template);
  if (selection.startsWith("builtin:")) {
    const value = selection.slice("builtin:".length);
    const preset = (definition?.presets ?? []).find((item) => String(item?.value ?? "") === value);
    values = applyReusablePresetValues(template, currentValues, preset?.values ?? {}, scopeKey);
    if (selector && scopeKey === builtInScopeKey) values[selector] = value;
  } else if (selection.startsWith("user:")) {
    const presetId = selection.slice("user:".length);
    const preset = (view.tubeDesignerUserData?.parameterPresets ?? [])
      .find((item) => String(item?.id ?? "") === presetId
        && String(item?.templateId ?? "") === String(template.id)
        && presetRecordScopeKey(template, item) === scopeKey);
    if (!preset) return;
    values = applyReusablePresetValues(template, currentValues, preset.values ?? {}, scopeKey);
    if (selector && scopeKey === builtInScopeKey) values[selector] = String(definition?.customValue ?? "custom");
  } else if (selector && scopeKey === builtInScopeKey) {
    values[selector] = String(definition?.customValue ?? "custom");
  }

  if (mode === "add") {
    captureAddDialogScrollAnchor(context, view, target);
    view.tubeDesignerAddDraft = values;
    setParameterPresetSelection(view, mode, scopeKey, selection);
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
  } else {
    captureParameterPanelState(context, view);
    setParameterPresetSelection(view, mode, scopeKey, selection);
    await commitRightProductParameters(context, view, values, ops);
  }
  focusPresetSelection(context, mode, scopeKey);
}

function markSelectedPresetModified(view, mode, template, changedKey) {
  if (!changedKey) return;
  const scopeKey = getParameterPresetScopeKey(template, changedKey);
  const selected = parameterPresetSelection(view, mode, scopeKey);
  let presetKeys = new Set();
  if (selected.startsWith("user:")) {
    const presetId = selected.slice("user:".length);
    const preset = (view.tubeDesignerUserData?.parameterPresets ?? [])
      .find((item) => String(item?.id ?? "") === presetId);
    presetKeys = new Set(Object.keys(preset?.values ?? {}));
  } else if (selected.startsWith("builtin:")) {
    const presetValue = selected.slice("builtin:".length);
    const preset = (template?.extensions?.parameterPresets?.presets ?? [])
      .find((item) => String(item?.value ?? "") === presetValue);
    presetKeys = new Set(Object.keys(preset?.values ?? {}));
  }
  if (presetKeys.has(changedKey)) setParameterPresetSelection(view, mode, scopeKey, "custom");
}

function openParameterPresetDialog(context, view, target, ops) {
  if (view.pending) return;
  const mode = String(target?.dataset?.tubeDesignerPresetMode ?? "") === "add" ? "add" : "right";
  const scopeKey = String(target?.dataset?.tubeDesignerPresetScope ?? "").trim();
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view);
  view.tubeDesignerPresetDialog = {
    mode,
    scopeKey,
    presetId: String(target?.dataset?.tubeDesignerPresetId ?? "").trim(),
  };
  ops.renderProject(context, view);
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
  queueMicrotask(() => resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-preset-name]")?.focus?.());
}

function closeParameterPresetDialog(context, view, ops) {
  const mode = view.tubeDesignerPresetDialog?.mode === "add" ? "add" : "right";
  const scopeKey = String(view.tubeDesignerPresetDialog?.scopeKey ?? "").trim();
  view.tubeDesignerPresetDialog = null;
  ops.renderProject(context, view);
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
  focusPresetSelection(context, mode, scopeKey);
}

async function saveParameterPreset(context, view, target, ops) {
  if (view.pending) return null;
  const mount = resolveDesignerMount(context);
  const name = String(mount?.querySelector?.("[data-tube-designer-preset-name]")?.value ?? "").trim();
  if (!name) {
    view.error = "请填写常用参数方案名称。";
    mount?.querySelector?.("[data-tube-designer-preset-name]")?.focus?.();
    return null;
  }
  const mode = view.tubeDesignerPresetDialog?.mode === "add" ? "add" : "right";
  const scopeKey = String(view.tubeDesignerPresetDialog?.scopeKey
    ?? target?.dataset?.tubeDesignerPresetScope ?? "").trim();
  const designer = view.scene?.tubeDesigner ?? {};
  const templateId = mode === "add" ? view.tubeDesignerAddTemplateId : designer.product?.templateId;
  const template = getTemplateById(designer.templates, templateId);
  if (!template) throw new Error("当前模板不存在，无法保存常用参数。");
  const existingId = String(target?.dataset?.tubeDesignerPresetId ?? "").trim();
  const existing = (view.tubeDesignerUserData?.parameterPresets ?? [])
    .find((item) => String(item?.id ?? "") === existingId);
  const customerName = String(mount?.querySelector?.("[data-tube-designer-preset-customer-name]")?.value ?? "").trim();
  let customerId = String(mount?.querySelector?.("[data-tube-designer-preset-customer-id]")?.value ?? "").trim();
  const currentValues = mode === "add"
    ? view.tubeDesignerAddDraft
    : (view.tubeDesignerRightDraft ?? designer.product?.parameters ?? {});

  view.tubeDesignerPresetDialog = {
    ...view.tubeDesignerPresetDialog,
    name,
    customerId,
    customerName,
  };
  try {
    return await runDesignerOperation(context, view, ops, async () => {
      if (customerName) {
        let customer = (view.tubeDesignerUserData?.customers ?? [])
          .find((item) => String(item?.name ?? "").trim().localeCompare(customerName, "zh-CN", { sensitivity: "accent" }) === 0);
        if (!customer) {
          const response = await invokeProductRequest(context, "TubeDesigner.SaveCustomer", {
            name: customerName,
            notes: "",
            revision: 0,
          });
          customer = response?.customer;
          if (customer) upsertUserDataItem(view, "customers", customer);
        }
        customerId = String(customer?.id ?? customerId);
        view.tubeDesignerPresetDialog.customerId = customerId;
        view.tubeDesignerPresetDialog.customerName = "";
        updateDesignerOperation(context, view, {
          phase: "saving-parameter-preset",
          phaseLabel: "保存参数",
          message: `客户信息已确认，正在保存“${name}”`,
        });
      }
      const response = await invokeProductRequest(context, "TubeDesigner.SaveParameterPreset", {
        id: existing?.id ?? "",
        revision: Number(existing?.revision ?? 0),
        name,
        templateId: template.id,
        templateVersion: template.version,
        scopeKey,
        customerId,
        values: getReusablePresetValues(template, currentValues, scopeKey),
      });
      const saved = response?.parameterPreset;
      if (!saved?.id) throw new Error("保存常用参数后没有返回记录标识。");
      upsertUserDataItem(view, "parameterPresets", saved);
      setParameterPresetSelection(view, mode, scopeKey, `user:${saved.id}`);
      view.tubeDesignerPresetDialog = null;
      ops.showNotice(context, view, existing ? "常用参数方案已更新。" : "常用参数方案已保存。" );
      return saved;
    }, {
      operation: {
        kind: "parameter-preset-save",
        title: existing ? "正在更新常用参数" : "正在保存常用参数",
        phase: customerName ? "saving-customer" : "saving-parameter-preset",
        phaseLabel: customerName ? "保存客户" : "保存参数",
        message: customerName
          ? `正在确认客户“${customerName}”并保存参数方案`
          : `正在保存“${name}”`,
      },
    });
  } finally {
    if (mode === "add") restoreAddDialogScrollAnchor(context, view);
    else restoreParameterPanelState(context, view);
    focusPresetSelection(context, mode, scopeKey);
  }
}

async function deleteParameterPreset(context, view, target, ops) {
  if (view.pending) return null;
  const id = String(target?.dataset?.tubeDesignerPresetId ?? "").trim();
  const preset = (view.tubeDesignerUserData?.parameterPresets ?? [])
    .find((item) => String(item?.id ?? "") === id);
  if (!preset) return null;
  const mode = view.tubeDesignerPresetDialog?.mode === "add" ? "add" : "right";
  const scopeKey = String(view.tubeDesignerPresetDialog?.scopeKey ?? "").trim();
  try {
    return await runDesignerOperation(context, view, ops, async () => {
      const response = await invokeProductRequest(context, "TubeDesigner.DeleteParameterPreset", {
        id,
        revision: Number(preset.revision ?? 0),
      });
      if (!response?.deleted) throw new Error("常用参数方案未能删除。");
      view.tubeDesignerUserData.parameterPresets = view.tubeDesignerUserData.parameterPresets
        .filter((item) => String(item?.id ?? "") !== id);
      setParameterPresetSelection(view, mode, scopeKey, "custom");
      view.tubeDesignerPresetDialog = null;
      ops.showNotice(context, view, "常用参数方案已删除。" );
      return response;
    }, {
      operation: {
        kind: "parameter-preset-delete",
        title: "正在删除常用参数",
        phase: "deleting-parameter-preset",
        phaseLabel: "删除方案",
        message: `正在删除“${preset.name}”`,
      },
    });
  } finally {
    if (mode === "add") restoreAddDialogScrollAnchor(context, view);
    else restoreParameterPanelState(context, view);
    focusPresetSelection(context, mode, scopeKey);
  }
}

function upsertUserDataItem(view, collection, item) {
  view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], profileId: "" };
  const items = Array.isArray(view.tubeDesignerUserData[collection])
    ? view.tubeDesignerUserData[collection] : [];
  const index = items.findIndex((candidate) => String(candidate?.id ?? "") === String(item?.id ?? ""));
  if (index >= 0) items[index] = item;
  else items.push(item);
  view.tubeDesignerUserData[collection] = items;
}

function focusPresetSelection(context, mode, scopeKey = "") {
  queueMicrotask(() => {
    const selectors = resolveDesignerMount(context)?.querySelectorAll?.("[data-tube-designer-preset-selection]") ?? [];
    Array.from(selectors)
      .find((element) => String(element?.dataset?.tubeDesignerPresetSelection ?? "") === mode
        && String(element?.dataset?.tubeDesignerPresetScope ?? "") === String(scopeKey ?? ""))
      ?.focus?.({ preventScroll: true });
  });
}

/**
 * A product stores a resource binding, not a duplicated parameter schema.
 * When a normal product field makes a tool selector applicable, materialize
 * the template-declared default immediately so its resource parameters and
 * diagram are editable before the user has to change the selector itself.
 */
function materializeProductToolDefaults(view, template, parameters) {
  const result = { ...parameters };
  if (!template || !productUsesToolLibrary(template)) return result;
  const bindings = { ...(result.tubeDesignerToolBindings ?? {}) };
  let changed = false;
  for (const field of template.parameters ?? []) {
    if (!isProductToolField(field) || !matchesParameterCondition(field.visibleWhen, result)) continue;
    const role = productToolRole(field);
    if (!role || bindings[role]) continue;
    const selection = String(result[field.key ?? field.name] ?? "").trim();
    if (!selection) continue;
    const tool = findProductTool(view, template, field, selection);
    if (!tool) continue;
    bindings[role] = makeProductToolBinding(field, tool, null, template);
    result[String(field.key ?? field.name)] = bindings[role].selectionKey;
    changed = true;
  }
  if (changed) result.tubeDesignerToolBindings = bindings;
  return result;
}

function normalizeDependentParameters(parameters, template = null, changedKey = "") {
  const result = { ...parameters };
  const presetDefinition = template?.extensions?.parameterPresets ?? null;
  const selector = String(presetDefinition?.selectorParameter ?? "");
  const presets = Array.isArray(presetDefinition?.presets) ? presetDefinition.presets : [];
  if (selector && changedKey === selector) {
    const selected = presets.find((preset) => preset?.value === result[selector]);
    if (selected?.values && typeof selected.values === "object") {
      Object.assign(result, selected.values);
    }
  } else if (selector && changedKey) {
    const presetKeys = new Set(presets.flatMap((preset) => Object.keys(preset?.values ?? {})));
    if (presetKeys.has(changedKey)) {
      result[selector] = String(presetDefinition?.customValue ?? "custom");
    }
  }
  if (result.vGrooveStyle && result.vGrooveStyle !== "sharp_v") {
    result.vGrooveBottomCut = typeof result.vGrooveBottomCut === "boolean" ? false : "否";
    result.vGrooveReliefHole = typeof result.vGrooveReliefHole === "boolean" ? false : "否";
    result.vGrooveWallOvercut = typeof result.vGrooveWallOvercut === "boolean" ? false : "否";
  }
  if (result.vGrooveReliefHole !== true && result.vGrooveReliefHole !== "是") {
    result.vGrooveReliefNoThrough = typeof result.vGrooveReliefNoThrough === "boolean" ? false : "否";
  }
  normalizeSecurityWindowOpeningSurface(result, template, changedKey);
  return result;
}

function finiteDimension(value, fallback = 0) {
  const numeric = Number(value);
  return Number.isFinite(numeric) && numeric > 0 ? numeric : fallback;
}

function roundedUp(value, step = 50) {
  return Math.ceil(value / step) * step;
}

/**
 * The add dialog intentionally exposes only style-defining fields.  A side or
 * bottom opening is therefore not allowed to leave the hidden depth at the
 * flat-window default and fail only after the user presses Confirm.  Keep the
 * opening's clearance and offsets, and raise just the selected face span to a
 * feasible initial size.  The dimension remains an ordinary editable value in
 * the scene's detailed editor.
 */
function normalizeSecurityWindowOpeningSurface(values, template, changedKey) {
  if (template?.id !== "single-face-security-window") return;
  if (!new Set(["faceType", "sidePosition", "accessDoorEnabled", "accessDoorFace2", "accessDoorFace3", "accessDoorFace5"]).has(changedKey)) return;
  const faceType = String(values.faceType ?? "single");
  const selectedFace = faceType === "two" ? String(values.accessDoorFace2 ?? "front")
    : faceType === "three" ? String(values.accessDoorFace3 ?? "front")
      : faceType === "five" ? String(values.accessDoorFace5 ?? "front") : "front";
  const sideSelected = (faceType === "two" && selectedFace === "side")
    || (faceType === "three" && (selectedFace === "left" || selectedFace === "right"))
    || (faceType === "five" && (selectedFace === "left" || selectedFace === "right"));
  const bottomSelected = faceType === "five" && selectedFace === "bottom";
  if (!sideSelected && !bottomSelected) return;

  const frameWidth = finiteDimension(values.frameWidth, 38);
  const fixedFrameWidth = finiteDimension(values.doorFrameWidth, 25);
  const leafDepth = finiteDimension(values.doorLeafFrameDepth, finiteDimension(values.doorLeafFrameWidth, 20));
  const hardware = finiteDimension(values.doorHardwareClearance, 10);
  const outerMargin = frameWidth * 5;
  const requiredSide = roundedUp(Math.max(1200,
    finiteDimension(values.doorUOffset, 150) + finiteDimension(values.doorClearWidth, 800)
      + leafDepth + hardware + fixedFrameWidth + outerMargin));
  const requiredBottomDepth = roundedUp(Math.max(1200,
    finiteDimension(values.doorVOffset, 350) + finiteDimension(values.doorClearHeight, 1000)
      + fixedFrameWidth + outerMargin));

  if (bottomSelected) {
    values.depth = Math.max(finiteDimension(values.depth, 0), requiredBottomDepth);
    return;
  }
  if (faceType === "two") values.sideWidth = Math.max(finiteDimension(values.sideWidth, 0), requiredSide);
  else if (faceType === "three" && selectedFace === "left") values.leftWidth = Math.max(finiteDimension(values.leftWidth, 0), requiredSide);
  else if (faceType === "three" && selectedFace === "right") values.rightWidth = Math.max(finiteDimension(values.rightWidth, 0), requiredSide);
  else if (faceType === "five") values.depth = Math.max(finiteDimension(values.depth, 0), requiredSide);
}

function validateInstanceQuantity(value) {
  const quantity = Number(value);
  if (!Number.isSafeInteger(quantity) || quantity < 1 || quantity > 1000000)
    throw new Error("生产数量必须是 1 到 1000000 之间的整数。");
  return quantity;
}

async function changeInstanceQuantity(context, view, target, ops) {
  if (view.pending) return null;
  if (target?.dataset?.tubeDesignerInstanceQuantity === "add") {
    // Preserve raw input across parameter re-renders; validate again on submit.
    view.tubeDesignerAddInstanceQuantity = target.value;
    validateInstanceQuantity(target.value);
    return null;
  }
  const product = view.scene?.tubeDesigner?.product;
  if (!product?.entityId) return null;
  const quantity = validateInstanceQuantity(target?.value);
  if (quantity === Number(product.quantity ?? 1)) return null;
  captureParameterPanelState(context, view);
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(context, "TubeDesigner.SetInstanceQuantity", {
      productEntityId: product.entityId, quantity,
    });
    if (!response?.tubeDesigner) throw new Error("实例数量未能保存。");
    view.scene.tubeDesigner = response.tubeDesigner;
    restoreLoadedProductTemplateDescriptors(view, response.tubeDesigner);
    await ensureTemplateDescriptor(context, view, response.tubeDesigner.product?.templateId);
    restoreSavedNestingTask(view, context);
    await acknowledgeOwnMutation(context, view);
    view.notice = "生产数量已保存；直接关联的下料零件数量已同步。";
    return response;
  });
}

async function generatePreview(context, view, ops, mode) {
  const designer = view.scene?.tubeDesigner ?? {};
  const isAdd = mode === "add";
  if (isAdd && !view.tubeDesignerAddDialogOpen) return null;
  if (!isAdd && !designer.product?.entityId) {
    view.error = "请先添加一个产品实例。";
    ops.renderProject(context, view);
    return null;
  }
  const templateId = isAdd ? view.tubeDesignerAddTemplateId : designer.product.templateId;
  const template = getTemplateById(designer.templates, templateId);
  if (!template?.available) throw new Error("请选择一个可用的产品模板。");
  if (!isAdd) {
    captureParameterPanelState(context, view);
    view.tubeDesignerRestoreParameterFocus = true;
  }
  const regeneratedProductId = !isAdd ? String(designer.product.entityId ?? "") : "";
  // Regeneration invalidates only this product instance's realised parts.
  // Other instances keep their own persisted disassembly results.
  const activeInstanceHadParts = !isAdd && (
    (designer.manufacturingGroups ?? []).some((group) => (
      String(group?.productEntityId ?? "") === regeneratedProductId
      && (group?.parts?.length ?? 0) > 0
    ))
    || (designer.instances ?? []).some((instance) => (
      String(instance?.entityId ?? "") === regeneratedProductId
      && instance?.hasDisassembly === true
    ))
  );
  const releasedTransientProductIds = new Set(activeInstanceHadParts ? [regeneratedProductId] : []);
  const alreadyRequiresRedisassembly = !isAdd
    && (view.tubeDesignerProductsRequiringDisassembly ?? []).map(String).includes(regeneratedProductId);
  const formSelector = isAdd ? "[data-tube-designer-add-form]" : "[data-tube-designer-parameter-form]";
  const payload = collectParameters(context.mount, formSelector, {
    ...getDefaultParameters(designer.templates, template.id),
    ...(isAdd
      ? view.tubeDesignerAddDraft
      : (view.tubeDesignerRightDraft ?? designer.product.parameters)),
    templateId: template.id,
    templateVersion: template.version,
  });
  if (isAdd) {
    const quantityInput = context.mount?.querySelector?.('[data-tube-designer-instance-quantity="add"]');
    payload.instanceQuantity = validateInstanceQuantity(quantityInput?.value ?? view.tubeDesignerAddInstanceQuantity ?? 1);
    payload.instanceName = view.tubeDesignerAddInstanceName;
    payload.createdAt = view.tubeDesignerAddCreatedAt;
  } else {
    payload.productEntityId = designer.product.entityId;
    payload.instanceName = designer.product.name;
    payload.createdAt = designer.product.createdAt;
  }

  const previousViewRevision = ops.getActiveAreaViewRevision?.(view) ?? "0";
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(context, "TubeDesigner.GeneratePreview", payload);
    const nextDesigner = response?.tubeDesigner ?? {};
    const generationRunId = String(nextDesigner.generationRun?.entityId ?? "").trim();
    const memberIds = (nextDesigner.members ?? []).map((member) => String(member?.entityId ?? "").trim()).filter(Boolean);
    if (!generationRunId || memberIds.length === 0) {
      throw new Error("生成结果没有返回已提交的预览标识。");
    }
    if (!isAdd) {
      for (const group of nextDesigner.manufacturingGroups ?? []) {
        if (String(group?.productEntityId ?? "") === regeneratedProductId
            && (group?.transient === true || (group?.parts ?? []).some((part) => part?.transient === true))) {
          const productId = String(group?.productEntityId ?? "").trim();
          if (productId) releasedTransientProductIds.add(productId);
        }
      }
      nextDesigner.manufacturingGroups = (nextDesigner.manufacturingGroups ?? []).flatMap((group) => {
        if (String(group?.productEntityId ?? "") !== regeneratedProductId) return [group];
        if (group?.transient === true) return [];
        const parts = (group?.parts ?? []).filter((part) => part?.transient !== true);
        return parts.length ? [{ ...group, parts }] : [];
      });
      nextDesigner.parts = (nextDesigner.parts ?? []).filter((part) => part?.transient !== true);
      nextDesigner.instances = (nextDesigner.instances ?? []).map((instance) => (
        releasedTransientProductIds.has(String(instance?.entityId ?? ""))
          ? { ...instance, hasDisassembly: false, partCount: 0 }
          : instance
      ));
    }
    updateDesignerOperation(context, view, {
      phase: "synchronizing-view",
      phaseLabel: "显示同步",
      message: `几何生成完成，正在把 ${memberIds.length} 个构件同步到三维视图`,
    });
    view.scene ??= {};
    view.scene.tubeDesigner = nextDesigner;
    restoreLoadedProductTemplateDescriptors(view, nextDesigner);
    await ensureTemplateDescriptor(context, view, nextDesigner.product?.templateId);
    view.tubeDesignerRightDraft = { ...(nextDesigner.product?.parameters ?? {}) };
    view.tubeDesignerLastEditedParameterKey = "";
    view.tubeDesignerSceneSpecificationEditorParameter = "";
    const committedProductId = String(nextDesigner.product?.entityId ?? "").trim();
    if (committedProductId && view.tubeDesignerRightDraftsByProductId) {
      delete view.tubeDesignerRightDraftsByProductId[committedProductId];
    }
    clearRightParameterHistory(view, committedProductId);
    context.actions?.refreshProjectHistoryControls?.();
    synchronizeRightRuntimeModel(view, nextDesigner.product, !isAdd);
    view.tubeDesignerBreakdownOpen = false;
    view.tubeDesignerDisassemblySelectorOpen = false;
    view.tubeDesignerSelectedPartIds = [];
    view.tubeDesignerActivePartId = "";
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerPartInspectionOpen = false;
    view.tubeDesignerPartDimensionsVisible = false;
    view.viewport?.setCustomVisibleEntityIds?.([]);
    if (!isAdd && (releasedTransientProductIds.size || alreadyRequiresRedisassembly)) {
      view.tubeDesignerProductsRequiringDisassembly = [...new Set([
        ...(view.tubeDesignerProductsRequiringDisassembly ?? []).map(String),
        ...releasedTransientProductIds,
        ...(alreadyRequiresRedisassembly ? [regeneratedProductId] : []),
      ])];
    }

    const viewExpectation = {
      correlationId: generationRunId,
      expectedEntityIds: memberIds,
    };
    if (!isAdd) viewExpectation.afterRevision = previousViewRevision;
    const viewContent = await ops.refreshActiveAreaView(context, view, viewExpectation);
    verifyViewportReceipt(viewContent, generationRunId, memberIds);
    const viewReceipt = fitDesignerDefaultView(
      view,
      viewContent.revision,
      nextDesigner.product,
    );
    // The viewport overlay is not part of the generated WebGL scene. Refresh it
    // explicitly after the confirmed model has reached the viewport so the old
    // red draft annotations cannot survive a regeneration.
    bindProductSpecificationAnnotations(resolveDesignerMount(context), view);
    updateDesignerOperation(context, view, {
      phase: "committing-state",
      phaseLabel: "状态确认",
      message: "三维视图已经接收新版本，正在确认项目状态",
    });
    view.tubeDesignerLastOperation = {
      kind: isAdd ? "add" : "update",
      generationRunId,
      productEntityId: nextDesigner.product?.entityId,
      viewRevision: viewContent.revision,
      memberIds,
      renderSequence: viewContent.viewportReceipt.renderSequence,
      fitRenderSequence: viewReceipt.fitRenderSequence,
      defaultViewRenderSequence: viewReceipt.defaultViewRenderSequence,
    };
    if (isAdd) view.tubeDesignerAddDialogOpen = false;
    await acknowledgeOwnMutation(context, view);
    ops.showNotice(context, view, isAdd
      ? "产品实例已添加并生成预览。"
      : releasedTransientProductIds.size || alreadyRequiresRedisassembly
        ? "参数已确认，三维模型已重新生成；原零件清单已清空，需要重新拆单。"
        : "参数已确认，三维模型已重新生成。");
    return view.tubeDesignerLastOperation;
  }, {
    operation: {
      kind: isAdd ? "generate" : "regenerate",
      title: isAdd ? "正在生成产品预览" : "正在重新生成产品预览",
      phase: "evaluating-template",
      phaseLabel: "几何模型生成中",
      message: "正在生成三维几何模型",
    },
  });
}

async function activateInstance(context, view, target, ops) {
  const designer = view.scene?.tubeDesigner ?? {};
  const productEntityId = String(target?.dataset?.tubeDesignerInstanceId ?? "").trim();
  if (!productEntityId || productEntityId === designer.product?.entityId || view.pending) return null;
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(context, "TubeDesigner.ActivateProduct", { productEntityId });
    const nextDesigner = response?.tubeDesigner ?? {};
    const generationRunId = String(nextDesigner.generationRun?.entityId ?? "").trim();
    const memberIds = (nextDesigner.members ?? []).map((member) => String(member?.entityId ?? "").trim()).filter(Boolean);
    if (nextDesigner.product?.entityId !== productEntityId || !generationRunId || !memberIds.length) {
      throw new Error("实例选择没有返回对应的已提交预览。");
    }
    updateDesignerOperation(context, view, {
      phase: "synchronizing-view",
      phaseLabel: "显示同步",
      message: `实例已激活，正在载入 ${memberIds.length} 个三维构件`,
    });
    view.scene ??= {};
    view.scene.tubeDesigner = nextDesigner;
    restoreLoadedProductTemplateDescriptors(view, nextDesigner);
    await ensureTemplateDescriptor(context, view, nextDesigner.product?.templateId);
    restoreRightDraftForActiveProduct(view, nextDesigner.product);
    view.tubeDesignerLastEditedParameterKey = "";
    view.tubeDesignerSceneSpecificationEditorParameter = "";
    view.tubeDesignerRightEditorStage = "parameters";
    view.tubeDesignerManufacturingPlans ??= {};
    view.tubeDesignerManufacturingPlans.right = null;
    view.tubeDesignerBreakdownOpen = false;
    const viewContent = await ops.refreshActiveAreaView(context, view, {
      correlationId: generationRunId,
      expectedEntityIds: memberIds,
    });
    verifyViewportReceipt(viewContent, generationRunId, memberIds);
    const viewReceipt = fitDesignerDefaultView(
      view,
      viewContent.revision,
      nextDesigner.product,
    );
    // Instance drafts and generated annotation anchors are isolated per product.
    // Do not rely on a later full DOM render to replace the previous instance's
    // overlay, because the Three viewport itself is deliberately preserved.
    bindProductSpecificationAnnotations(resolveDesignerMount(context), view);
    view.tubeDesignerActivePartId = "";
    refreshDesignerProductPartsDock(context, view);
    view.tubeDesignerLastOperation = {
      kind: "activate",
      productEntityId,
      generationRunId,
      viewRevision: viewContent.revision,
      memberIds,
      fitRenderSequence: viewReceipt.fitRenderSequence,
      defaultViewRenderSequence: viewReceipt.defaultViewRenderSequence,
    };
    ops.showNotice(context, view, `已切换到 ${nextDesigner.product?.name ?? "所选实例"}。`);
    return view.tubeDesignerLastOperation;
  }, {
    operation: {
      kind: "activate",
      title: "正在切换产品实例",
      phase: "activating-instance",
      phaseLabel: "实例激活",
      message: "正在读取实例引用的几何资源版本",
    },
  });
}

async function deleteActiveProduct(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return null;
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  const productId = String(product?.entityId ?? "").trim();
  if (!productId) {
    view.error = "请先选择要删除的产品实例。";
    ops.renderProject(context, view);
    return null;
  }
  if (typeof globalThis.confirm === "function"
      && !globalThis.confirm(`确定删除当前产品实例“${product.name ?? "未命名产品"}”吗？\n该实例的模型和零件清单会一并删除，可使用撤销恢复。`)) return null;

  captureInstanceListState(context, view);
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(context, "TubeDesigner.DeleteProduct", {
      productEntityId: productId,
    });
    if (response?.deleted !== true || !response?.tubeDesigner)
      throw new Error("产品实例未能删除。");
    const nextDesigner = response.tubeDesigner;
    view.scene ??= {};
    view.scene.tubeDesigner = nextDesigner;
    restoreLoadedProductTemplateDescriptors(view, nextDesigner);
    if (nextDesigner.product?.templateId)
      await ensureTemplateDescriptor(context, view, nextDesigner.product.templateId);
    clearRightParameterHistory(view, productId);
    if (view.tubeDesignerRightDraftsByProductId)
      delete view.tubeDesignerRightDraftsByProductId[productId];
    if (view.tubeDesignerInstanceRuntimeVersions)
      delete view.tubeDesignerInstanceRuntimeVersions[productId];
    view.tubeDesignerProductsRequiringDisassembly = (
      view.tubeDesignerProductsRequiringDisassembly ?? []
    ).filter((id) => String(id) !== productId);
    restoreRightDraftForActiveProduct(view, nextDesigner.product);
    reconcileSelections(view, nextDesigner);
    restoreSavedNestingTask(view, context);
    view.tubeDesignerBreakdownOpen = false;
    view.tubeDesignerDisassemblySelectorOpen = false;
    view.tubeDesignerSelectedPartIds = [];
    view.tubeDesignerActivePartId = "";
    view.tubeDesignerPartInspectionOpen = false;
    view.viewport?.setCustomVisibleEntityIds?.([]);

    const viewContent = await ops.refreshActiveAreaView(context, view, {});
    const memberIds = (nextDesigner.members ?? [])
      .map((member) => String(member?.entityId ?? "").trim()).filter(Boolean);
    if (memberIds.length && nextDesigner.product) {
      fitDesignerDefaultView(view, viewContent.revision, nextDesigner.product);
    }
    bindProductSpecificationAnnotations(resolveDesignerMount(context), view);
    await acknowledgeOwnMutation(context, view);
    context.actions?.refreshProjectHistoryControls?.();
    ops.showNotice(context, view, nextDesigner.product
      ? `已删除“${product.name ?? "产品实例"}”，并切换到下一个实例。`
      : `已删除“${product.name ?? "产品实例"}”。`);
    return response;
  }, {
    operation: {
      kind: "delete-product",
      title: "正在删除产品实例",
      phase: "deleting-product",
      phaseLabel: "删除当前实例",
      message: "正在移除模型及该实例的零件数据",
    },
  });
}

function openDisassemblySelector(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const instances = view.scene?.tubeDesigner?.instances ?? [];
  if (!instances.length) {
    view.error = "请先添加产品实例。";
    ops.renderProject(context, view);
    return;
  }
  const activeProductId = String(view.scene?.tubeDesigner?.activeProductId
    ?? view.scene?.tubeDesigner?.product?.entityId
    ?? instances[0]?.entityId
    ?? "");
  view.tubeDesignerSelectedInstanceIds = instances.map((item) => item.entityId);
  view.tubeDesignerDisassemblySelectorOpen = true;
  view.tubeDesignerPostDisassemblyChoice = null;
  view.tubeDesignerBreakdownOpen = false;
  view.tubeDesignerBreakdownMode = "";
  view.error = "";
  ops.renderProject(context, view);
}

async function openTransientPartList(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return null;
  const instances = view.scene?.tubeDesigner?.instances ?? [];
  if (!instances.length) {
    view.error = "请先添加产品实例。";
    ops.renderProject(context, view);
    return null;
  }
  view.tubeDesignerSelectedInstanceIds = instances.map((item) => item.entityId);
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.tubeDesignerPostDisassemblyChoice = null;
  view.tubeDesignerBreakdownOpen = false;
  view.tubeDesignerBreakdownMode = "export";
  view.error = "";
  return disassembleSelected(context, view, ops, { importToNesting: false });
}

function toggleInstance(context, view, target, ops) {
  const instanceId = String(target?.dataset?.tubeDesignerInstanceId ?? "").trim();
  if (!instanceId) return;
  const selected = new Set(view.tubeDesignerSelectedInstanceIds ?? []);
  if (target?.checked) selected.add(instanceId);
  else selected.delete(instanceId);
  view.tubeDesignerSelectedInstanceIds = [...selected];
  ops.renderProject(context, view);
}

function toggleAllInstances(context, view, target, ops) {
  const instances = view.scene?.tubeDesigner?.instances ?? [];
  view.tubeDesignerSelectedInstanceIds = target?.checked ? instances.map((item) => item.entityId) : [];
  ops.renderProject(context, view);
}

async function disassembleSelected(context, view, ops, { importToNesting = false, showBreakdown = true } = {}) {
  const productEntityIds = [...new Set(view.tubeDesignerSelectedInstanceIds ?? [])];
  if (!productEntityIds.length) {
    view.error = importToNesting
      ? "请至少选择一个要导入下料的产品实例。"
      : "请至少选择一个要导出零件的产品实例。";
    ops.renderProject(context, view);
    return null;
  }
  const activeProductId = String(view.scene?.tubeDesigner?.product?.entityId ?? "");
  const productParametersByEntityId = {};
  if (productEntityIds.includes(activeProductId) && view.tubeDesignerPartsDraftDirty === true) {
    productParametersByEntityId[activeProductId] = structuredCloneSafe(view.tubeDesignerRightDraft ?? {});
  }
  const result = await runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(context, "TubeDesigner.DisassembleSelected", {
      productEntityIds,
      ...(Object.keys(productParametersByEntityId).length ? { productParametersByEntityId } : {}),
    });
    const designer = response?.tubeDesigner ?? {};
    const groupIdSet = new Set(productEntityIds);
    const groups = (designer.manufacturingGroups ?? []).filter((group) => groupIdSet.has(group.productEntityId));
    if (groups.length !== productEntityIds.length || groups.some((group) => !(group.parts?.length > 0))) {
      throw new Error(importToNesting
        ? "批量导入下料没有返回完整的产品零件组。"
        : "零件清单没有返回完整的产品零件组。");
    }
    const partCount = groups.reduce((count, group) => count + group.parts.length, 0);
    updateDesignerOperation(context, view, {
      phase: importToNesting ? "staging" : "organizing-results",
      phaseLabel: importToNesting ? "写入下料清单" : "结果整理",
      message: importToNesting
        ? `正在把 ${groups.length} 个产品、${partCount} 种零件关联到下料清单`
        : `拆分完成，正在整理 ${groups.length} 个产品、${partCount} 个零件`,
    });
    view.scene ??= {};
    view.scene.tubeDesigner = designer;
    restoreLoadedProductTemplateDescriptors(view, designer);
    // Disassembly replies intentionally carry catalogue cards, not parameter
    // descriptors.  Reattach the descriptor before the AppShell acknowledges
    // the scene mutation, otherwise its render briefly replaces the right
    // editor with the misleading “正在载入产品参数” state.
    const activeTemplateId = String(designer.product?.templateId ?? "").trim();
    if (activeTemplateId) await ensureTemplateDescriptor(context, view, activeTemplateId);
    for (const productId of Object.keys(productParametersByEntityId)) {
      if (view.tubeDesignerRightDraftsByProductId) delete view.tubeDesignerRightDraftsByProductId[productId];
      clearRightParameterHistory(view, productId);
    }
    if (Object.hasOwn(productParametersByEntityId, String(designer.product?.entityId ?? ""))) {
      view.tubeDesignerRightDraft = structuredCloneSafe(designer.product?.parameters ?? {});
      synchronizeRightRuntimeModel(view, designer.product, false);
    }
    view.tubeDesignerBreakdownProductIds = productEntityIds;
    view.tubeDesignerBreakdownPageProductId = String(productEntityIds[0] ?? "");
    view.tubeDesignerSelectedPartIds = groups.flatMap((group) => group.parts.map((part) => part.entityId));
    view.tubeDesignerDisassemblySelectorOpen = false;
    view.tubeDesignerActivePartId = String(groups[0]?.parts?.[0]?.entityId ?? "");
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerProductsRequiringDisassembly = (view.tubeDesignerProductsRequiringDisassembly ?? [])
      .map(String)
      .filter((productId) => !productEntityIds.includes(productId));
    view.tubeDesignerLastOperation = {
      kind: importToNesting ? "import-to-nesting" : "persistent-part-list",
      productEntityIds,
      groupCount: groups.length,
      partCount: view.tubeDesignerSelectedPartIds.length,
    };
    view.tubeDesignerPostDisassemblyChoice = null;
    if (importToNesting) {
      view.tubeDesignerBreakdownMode = "";
      const sourcePartIds = view.tubeDesignerSelectedPartIds.map(String);
      const staged = await invokeDesignerRequest(context, "TubeDesigner.StageNestingParts", {
        partEntityIds: sourcePartIds,
      }, { timeoutMs: 180000 });
      if (!staged?.tubeDesigner) throw new Error("下料零件未能关联。");
      view.scene.tubeDesigner = staged.tubeDesigner;
      restoreLoadedProductTemplateDescriptors(view, staged.tubeDesigner);
      const stagedPartIds = Array.isArray(staged.partEntityIds)
        ? staged.partEntityIds.map(String).filter(Boolean)
        : listNestingParts(staged.tubeDesigner).map((part) => String(part.entityId));
      view.tubeDesignerNestingSelectedPartIds = stagedPartIds;
      view.tubeDesignerSelectedPartIds = [];
      view.tubeDesignerActivePartId = stagedPartIds[0] ?? "";
      view.tubeDesignerActiveNestingPartId = stagedPartIds[0] ?? "";
      view.tubeDesignerActiveNestingPlacementId = "";
      view.tubeDesignerBreakdownOpen = false;
      view.tubeDesignerNestingSelectionKind = "part";
      view.tubeDesignerPartDimensionsVisible = false;
      await acknowledgeOwnMutation(context, view);
      await context.actions?.selectRibbonTab?.("nesting");
      // selectRibbonTab() updates the app-shell state, but this action's
      // context was created before the tab changed.
      context.activeRibbonTabId = "nesting";
      view.activeAreaId = "nesting";
      ops.showNotice(context, view, `已导入下料：${groups.length} 个产品，共 ${stagedPartIds.length} 种零件。`);
      return {
        ...view.tubeDesignerLastOperation,
        stagedPartCount: stagedPartIds.length,
      };
    }
    await acknowledgeOwnMutation(context, view);
    view.tubeDesignerBreakdownMode = showBreakdown ? "export" : "";
    view.tubeDesignerBreakdownOpen = showBreakdown;
    if (!showBreakdown) {
      view.tubeDesignerBreakdownProductIds = [];
      view.tubeDesignerSelectedPartIds = [];
      view.tubeDesignerActivePartId = "";
      ops.renderProject(context, view);
    }
    ops.showNotice(context, view, showBreakdown
      ? `已生成并保存零件清单：${groups.length} 个产品，共 ${view.tubeDesignerLastOperation.partCount} 个零件。`
      : `已生成“${groups[0]?.name ?? "当前实例"}”的零件清单，可在场景下方逐件复尺。`);
    return view.tubeDesignerLastOperation;
  }, {
    operation: {
      kind: importToNesting ? "import-to-nesting" : "persistent-part-list",
      title: importToNesting ? "正在导入下料" : "正在生成零件清单",
      phase: "building-parts",
      phaseLabel: "零件生成",
      message: importToNesting
        ? `正在生成所选 ${productEntityIds.length} 个产品实例的下料零件`
        : `正在生成并保存 ${productEntityIds.length} 个产品实例的制造零件`,
    },
  });
  return result;
}

async function disassembleActiveProduct(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return null;
  const productId = String(view.scene?.tubeDesigner?.product?.entityId
    ?? view.scene?.tubeDesigner?.activeProductId ?? "").trim();
  if (!productId) {
    view.error = "请先在左侧选择一个产品实例。";
    ops.renderProject(context, view);
    return null;
  }
  // Generating the realised part list must not disturb the editor which the
  // user is comparing against the model.  The operation below replaces the
  // lightweight scene snapshot, so retain the right editor's interaction
  // state before that render cycle starts.
  captureParameterPanelState(context, view);
  view.tubeDesignerSelectedInstanceIds = [productId];
  return disassembleSelected(context, view, ops, { importToNesting: false, showBreakdown: false });
}

async function exportActiveProductParts(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return null;
  const designer = view.scene?.tubeDesigner ?? {};
  const productId = String(designer.product?.entityId ?? designer.activeProductId ?? "").trim();
  const group = (designer.manufacturingGroups ?? [])
    .find((item) => String(item?.productEntityId ?? "") === productId);
  const partIds = (group?.parts ?? []).map((part) => String(part?.entityId ?? "").trim()).filter(Boolean);
  if (!productId || !partIds.length) {
    view.error = "当前产品实例还没有有效零件清单，请先生成零件清单。";
    ops.renderProject(context, view);
    return null;
  }

  // Exporting files is deliberately read-only.  Do not reuse the modal
  // breakdown workflow here: that workflow owns selection and disclosure
  // state, and its full-project renders made the instance dock disappear
  // after a normal product export.
  return exportProductPartList(context, view, ops, partIds, {
    title: `选择“${designer.product?.name ?? "当前产品"}”的零件导出目录`,
    preserveWorkbench: true,
  });
}

function openBreakdownResults(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const groups = view.scene?.tubeDesigner?.manufacturingGroups ?? [];
  if (!groups.length) {
    openDisassemblySelector(context, view, ops);
    return;
  }
  view.tubeDesignerBreakdownProductIds = groups.map((group) => group.productEntityId);
  view.tubeDesignerBreakdownPageProductId = String(groups[0]?.productEntityId ?? "");
  view.tubeDesignerSelectedPartIds = groups.flatMap((group) => group.parts.map((part) => part.entityId));
  view.tubeDesignerBreakdownOpen = true;
  view.tubeDesignerBreakdownMode = "default";
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.error = "";
  ops.renderProject(context, view);
}

function openNestingPartList(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const designer = view.scene?.tubeDesigner ?? {};
  const groups = Array.isArray(designer.nestingGroups)
    ? designer.nestingGroups
    : (designer.manufacturingGroups ?? []);
  const visibleGroups = groups.filter((group) => Array.isArray(group?.parts) && group.parts.length);
  if (!visibleGroups.length) {
    view.error = "当前下料没有可导出的零件。";
    ops.renderProject(context, view);
    return;
  }
  const partIds = visibleGroups.flatMap((group) => group.parts.map((part) => String(part.entityId)).filter(Boolean));
  view.tubeDesignerBreakdownProductIds = visibleGroups.map((group) => group.productEntityId);
  view.tubeDesignerBreakdownPageProductId = String(visibleGroups[0]?.productEntityId ?? "");
  view.tubeDesignerSelectedPartIds = partIds;
  view.tubeDesignerBreakdownOpen = true;
  view.tubeDesignerBreakdownMode = "nesting-export";
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.tubeDesignerPartInspectionOpen = false;
  view.tubeDesignerPartMeasurementState = null;
  view.tubeDesignerActivePartId = partIds[0] ?? "";
  view.error = "";
  ops.renderProject(context, view);
}

function openInstanceBreakdown(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const productEntityId = String(target?.dataset?.tubeDesignerInstanceId ?? "").trim();
  const group = (view.scene?.tubeDesigner?.manufacturingGroups ?? [])
    .find((item) => String(item.productEntityId) === productEntityId);
  if (!group?.parts?.length) {
    view.error = "该产品实例还没有有效拆单结果，请先拆单。";
    ops.renderProject(context, view);
    return;
  }
  view.tubeDesignerBreakdownProductIds = [productEntityId];
  view.tubeDesignerBreakdownPageProductId = productEntityId;
  view.tubeDesignerSelectedPartIds = group.parts.map((part) => String(part.entityId));
  view.tubeDesignerBreakdownOpen = true;
  view.tubeDesignerBreakdownMode = "default";
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.error = "";
  ops.renderProject(context, view);
}

function togglePart(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const partId = String(target?.dataset?.tubeDesignerPartId ?? "").trim();
  if (!partId) return;
  const selected = new Set(view.tubeDesignerSelectedPartIds ?? []);
  if (target?.checked) selected.add(partId);
  else selected.delete(partId);
  view.tubeDesignerSelectedPartIds = [...selected];
  synchronizeBreakdownSelection(view, target);
}

function togglePartGroup(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const partIds = parseSelectionIds(target);
  if (!partIds.length) return;
  const selected = new Set(view.tubeDesignerSelectedPartIds ?? []);
  for (const partId of partIds) {
    if (target?.checked) selected.add(partId);
    else selected.delete(partId);
  }
  view.tubeDesignerSelectedPartIds = [...selected];
  synchronizeBreakdownSelection(view, target);
}

function toggleAllParts(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const currentPartIds = new Set(getCurrentBreakdownGroups(view)
    .flatMap((group) => group.parts.map((part) => String(part.entityId))));
  const selected = new Set(view.tubeDesignerSelectedPartIds ?? []);
  for (const partId of currentPartIds) {
    if (target?.checked) selected.add(partId);
    else selected.delete(partId);
  }
  view.tubeDesignerSelectedPartIds = [...selected];
  synchronizeBreakdownSelection(view, target);
}

function synchronizeBreakdownSelection(view, sourceTarget = null) {
  const dialog = typeof sourceTarget?.closest === "function"
    ? sourceTarget.closest(".tube-designer-breakdown-dialog")
    : document.querySelector(".tube-designer-breakdown-dialog");
  if (!dialog) return;

  const selectedIds = new Set(view.tubeDesignerSelectedPartIds ?? []);
  const visibleGroups = getVisibleBreakdownGroups(view);
  const visibleParts = visibleGroups.flatMap((group) => group.parts ?? []);
  const pageParts = getCurrentBreakdownGroups(view).flatMap((group) => group.parts ?? []);
  const pagePartIds = pageParts.map((part) => String(part.entityId));
  const selectionCheckboxes = [...dialog.querySelectorAll("[data-tube-designer-selection-ids]")];
  for (const checkbox of selectionCheckboxes) {
    const checkboxPartIds = parseSelectionIds(checkbox);
    const checkedCount = checkboxPartIds.filter((partId) => selectedIds.has(partId)).length;
    checkbox.checked = checkboxPartIds.length > 0 && checkedCount === checkboxPartIds.length;
    checkbox.indeterminate = checkedCount > 0 && checkedCount < checkboxPartIds.length;
  }
  const selectedCount = visibleParts.filter((part) => selectedIds.has(String(part.entityId))).length;
  const selectedPageCount = pagePartIds.filter((partId) => selectedIds.has(partId)).length;
  const allPagePartsSelected = pagePartIds.length > 0 && selectedPageCount === pagePartIds.length;
  const selectAll = dialog.querySelector("[data-tube-designer-all-parts]");
  if (selectAll) {
    selectAll.checked = allPagePartsSelected;
    selectAll.indeterminate = selectedPageCount > 0 && !allPagePartsSelected;
  }

  const groupCount = visibleGroups.length;
  const headingSummary = dialog.querySelector("[data-tube-designer-breakdown-summary]");
  if (headingSummary) {
    headingSummary.textContent = `${groupCount} 个产品 · ${visibleParts.length} 个零件 · 已选择 ${selectedCount} 个`;
  }
  const exportSummary = dialog.querySelector("[data-tube-designer-export-selection-summary]");
  if (exportSummary) exportSummary.textContent = `将导出 ${selectedCount} 个零件`;
  const exportButton = dialog.querySelector("[data-tube-designer-export-selected]");
  if (exportButton) exportButton.disabled = Boolean(view.pending) || selectedCount === 0;
}

function toggleProductTree(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const productId = String(target?.dataset?.tubeDesignerProductId ?? "").trim();
  if (!productId) return;
  const scrollAnchor = captureBreakdownScrollAnchor(target);
  const collapsed = new Set(view.tubeDesignerCollapsedBreakdownProductIds ?? []);
  if (collapsed.has(productId)) collapsed.delete(productId);
  else collapsed.add(productId);
  view.tubeDesignerCollapsedBreakdownProductIds = [...collapsed];
  refreshBreakdownRows(context, view, ops, scrollAnchor);
}

function toggleCategoryTree(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const categoryId = String(target?.dataset?.tubeDesignerCategoryId ?? "").trim();
  if (!categoryId) return;
  const scrollAnchor = captureBreakdownScrollAnchor(target);
  const expanded = new Set(view.tubeDesignerExpandedBreakdownCategoryIds ?? []);
  if (expanded.has(categoryId)) expanded.delete(categoryId);
  else expanded.add(categoryId);
  view.tubeDesignerExpandedBreakdownCategoryIds = [...expanded];
  refreshBreakdownRows(context, view, ops, scrollAnchor);
}

function setAllBreakdownExpansion(context, view, shouldExpand, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const groups = getCurrentBreakdownGroups(view);
  const collapsedProducts = new Set(view.tubeDesignerCollapsedBreakdownProductIds ?? []);
  const expandedCategories = new Set(view.tubeDesignerExpandedBreakdownCategoryIds ?? []);
  for (const group of groups) {
    const productId = String(group.productEntityId ?? "");
    if (shouldExpand) collapsedProducts.delete(productId);
    else collapsedProducts.add(productId);
    for (const category of buildPartCategories(group.parts ?? [], productId)) {
      if (shouldExpand) expandedCategories.add(category.id);
      else expandedCategories.delete(category.id);
    }
  }
  view.tubeDesignerCollapsedBreakdownProductIds = [...collapsedProducts];
  view.tubeDesignerExpandedBreakdownCategoryIds = [...expandedCategories];
  refreshBreakdownRows(context, view, ops);
}

function setBreakdownPage(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const productId = String(target?.dataset?.tubeDesignerBreakdownPageProductId ?? "").trim();
  const valid = getVisibleBreakdownGroups(view)
    .some((group) => String(group.productEntityId ?? "") === productId);
  if (!valid || String(view.tubeDesignerBreakdownPageProductId ?? "") === productId) return;
  view.tubeDesignerBreakdownPageProductId = productId;
  const dialog = resolveDesignerMount(context)?.querySelector?.(".tube-designer-breakdown-dialog");
  const body = dialog?.querySelector?.(".tube-designer-breakdown-body");
  if (!body) {
    ops.renderProject(context, view);
    return;
  }
  body.outerHTML = renderDesignerBreakdownBody(view.scene?.tubeDesigner ?? {}, view);
  scheduleDesignerPartThumbnailHydration(context);
  const currentPageButton = dialog.querySelector(`[data-tube-designer-breakdown-page-product-id="${escapeCssAttribute(productId)}"][aria-current="page"]`);
  currentPageButton?.focus?.({ preventScroll: true });
}

function captureBreakdownScrollAnchor(target) {
  const scroller = target?.closest?.(".tube-designer-sheet-wrap");
  return scroller ? { scroller, anchor: captureScrollAnchor(scroller, target) } : null;
}

function refreshBreakdownRows(context, view, ops, scrollSnapshot = null) {
  const rows = resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-breakdown-rows]");
  if (!rows) {
    ops.renderProject(context, view);
    return;
  }
  rows.innerHTML = renderDesignerBreakdownRows(view.scene?.tubeDesigner ?? {}, view);
  scheduleDesignerPartThumbnailHydration(context);
  if (scrollSnapshot?.scroller?.isConnected) {
    restoreScrollAnchor(scrollSnapshot.scroller, scrollSnapshot.anchor);
  }
}

function openPartInspection(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const partId = String(target?.dataset?.tubeDesignerPartId ?? "").trim();
  if (!partId) return;
  const part = findInspectablePart(view, partId);
  if (!part) return;
  selectProductPartInScene(context, view, target, part);
  view.viewport?.setContinuousRendering?.(false);
  view.tubeDesignerInspectedPartId = partId;
  view.tubeDesignerPartInspectionOpen = true;
  ops.renderProject(context, view);
}

function openActiveProductPartInspection(context, view, ops) {
  const part = findActiveProductPart(view, view.tubeDesignerActivePartId);
  if (!part) {
    ops.showNotice?.(context, view, "请先在底部零件清单中选择要复尺的零件。");
    return;
  }
  openPartInspection(context, view, {
    dataset: { tubeDesignerPartId: String(part.entityId) },
  }, ops);
}

function findInspectablePart(view, partId) {
  const designer = view.scene?.tubeDesigner ?? {};
  const groups = view.tubeDesignerBreakdownMode === "nesting-export"
    && Array.isArray(designer.nestingGroups)
    ? designer.nestingGroups
    : (designer.manufacturingGroups ?? []);
  return groups.flatMap((group) => group?.parts ?? [])
    .find((part) => String(part?.entityId ?? "") === String(partId ?? "")) ?? null;
}

function findActiveProductPart(view, partId) {
  const designer = view.scene?.tubeDesigner ?? {};
  const productId = String(designer.product?.entityId ?? designer.activeProductId ?? "");
  const group = (designer.manufacturingGroups ?? [])
    .find((item) => String(item?.productEntityId ?? "") === productId);
  return (group?.parts ?? []).find((part) => String(part?.entityId ?? "") === String(partId ?? "")) ?? null;
}

function selectProductPartInScene(context, view, target, suppliedPart = null) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const partId = String(target?.dataset?.tubeDesignerPartId ?? suppliedPart?.entityId ?? "").trim();
  const part = suppliedPart ?? findActiveProductPart(view, partId);
  if (!part) return;
  const sceneEntityId = String(part.sourceMemberId ?? part.entityId ?? "").trim();
  if (!sceneEntityId) return;
  view.tubeDesignerActivePartId = String(part.entityId);
  view.selectedSceneObjectId = sceneEntityId;
  if (typeof view.viewport?.setSelectedObjectIds === "function") {
    view.viewport.setSelectedObjectIds([sceneEntityId], sceneEntityId);
  } else {
    view.viewport?.setSelectedObjectId?.(sceneEntityId);
  }

  // Selecting a list row must not rebuild the right editor: a rebuild loses
  // the user's scroll position and in-progress parameter entry.  Update only
  // this dock's visual selection while the viewport receives the linked member.
  const rows = context.mount?.querySelectorAll?.("[data-tube-designer-product-part-row]") ?? [];
  for (const row of rows) {
    const active = String(row.dataset.tubeDesignerProductPartRow ?? "") === String(part.entityId);
    row.classList.toggle("is-active", active);
    row.setAttribute("aria-selected", String(active));
  }
}

async function reloadProductParameters(context, view, ops) {
  const templateId = String(view.scene?.tubeDesigner?.product?.templateId ?? "").trim();
  if (!templateId || view.pending) return null;
  delete (view.tubeDesignerTemplateDescriptors ?? {})[templateId];
  view.tubeDesignerTemplateLoadError = "";
  try {
    const template = await ensureTemplateDescriptor(context, view, templateId);
    if (!Array.isArray(template?.parameters)) throw new Error("产品参数定义不可用。");
    ops.renderProject(context, view);
    return template;
  } catch (error) {
    view.tubeDesignerTemplateLoadError = error?.message ?? String(error);
    ops.renderProject(context, view);
    throw error;
  }
}

function parseSelectionIds(target) {
  return String(target?.dataset?.tubeDesignerSelectionIds ?? target?.dataset?.tubeDesignerPartId ?? "")
    .split(/\s+/)
    .map((value) => value.trim())
    .filter(Boolean);
}

async function exportProductPartList(context, view, ops, partEntityIds, {
  title = "选择零件清单导出总目录",
  preserveWorkbench = false,
  afterSuccess = null,
  targetDirectory: requestedTargetDirectory = "",
} = {}) {
  if (!context.sceneProxy || view.pending || view.tubeDesignerExportOperation) return null;
  const ids = [...new Set(partEntityIds.map((id) => String(id ?? "").trim()).filter(Boolean))];
  if (!ids.length) {
    view.error = "请至少选择一个零件。";
    if (!preserveWorkbench) ops.renderProject(context, view);
    return null;
  }
  const operationId = Number(view.tubeDesignerExportSequence ?? 0) + 1;
  view.tubeDesignerExportSequence = operationId;
  view.tubeDesignerExportOperation = {
    id: operationId,
    phase: "selecting-directory",
    completed: 0,
    total: ids.length,
    message: "请选择导出总目录",
  };
  return runDesignerOperation(context, view, ops, async () => {
    let targetDirectory = String(requestedTargetDirectory ?? "").trim();
    if (!targetDirectory) {
      const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
      if (typeof bridge?.openDirectoryDialog !== "function") {
        throw new Error("当前宿主没有提供目录选择能力。");
      }
      targetDirectory = String(await bridge.openDirectoryDialog({
        title,
        initialDirectory: view.tubeDesignerExportDirectory ?? "",
      }) ?? "").trim();
    }
    if (!targetDirectory) {
      return { kind: "export-cancelled", exportedCount: 0 };
    }
    view.tubeDesignerExportDirectory = targetDirectory;
    updateExportOperation(context, view, operationId, {
      phase: "preparing",
      message: "正在核对零件与目标目录",
    });
    const response = await invokeDesignerRequest(context, "TubeDesigner.ExportSelected", {
      targetDirectory,
      partEntityIds: ids,
    }, {
      onReport: (report) => updateExportOperation(
        context,
        view,
        operationId,
        report?.payload ?? {},
      ),
    });
    view.tubeDesignerLastOperation = {
      kind: "export",
      exportedCount: Number(response?.exportedCount ?? 0),
      exportedFiles: response?.exportedFiles ?? [],
      exportedGroups: response?.exportedGroups ?? [],
      partListFile: response?.partListFile ?? "",
    };
    // ExportSelected writes files only.  Its response must never replace the
    // live product snapshot: doing so discards the dock's realised parts when
    // an older host happens to include a lightweight tubeDesigner payload.
    await afterSuccess?.();
    const message = `已导出 ${view.tubeDesignerLastOperation.exportedGroups.length} 个产品、${view.tubeDesignerLastOperation.exportedCount} 个零件及 Excel 清单。`;
    if (preserveWorkbench) {
      view.notice = message;
      view.noticeRevision = Number(view.noticeRevision ?? 0) + 1;
      view.error = "";
      ops.appendProjectLog?.(context, "info", message);
    } else {
      ops.showNotice(context, view, message);
    }
    return view.tubeDesignerLastOperation;
  }, {
    renderProject: !preserveWorkbench,
    beforeFinish: () => {
      if (view.tubeDesignerExportOperation?.id === operationId) {
        view.tubeDesignerExportOperation = null;
      }
    },
  });
}

async function exportSelected(context, view, target, ops) {
  if (!context.sceneProxy || view.pending || view.tubeDesignerExportOperation) return null;
  const exportAllNesting = target?.dataset?.tubeDesignerExportScope === "nesting-all";
  const partEntityIds = exportAllNesting
    ? [...new Set(listNestingParts(view.scene?.tubeDesigner ?? {}).map((part) => String(part.entityId)).filter(Boolean))]
    : [...new Set(view.tubeDesignerSelectedPartIds ?? [])];
  const title = exportAllNesting || view.tubeDesignerBreakdownMode === "nesting-export"
    ? "选择零件清单导出总目录"
    : `选择 ${getVisibleBreakdownGroups(view).length} 个产品的 STEP 导出总目录`;
  return exportProductPartList(context, view, ops, partEntityIds, {
    title,
    targetDirectory: target?.dataset?.tubeDesignerExportDirectory ?? "",
    afterSuccess: () => {
      if (!exportAllNesting && ["export", "nesting-export"].includes(view.tubeDesignerBreakdownMode)) {
        view.tubeDesignerBreakdownOpen = false;
        view.tubeDesignerBreakdownMode = "";
        view.tubeDesignerBreakdownProductIds = [];
        view.tubeDesignerSelectedPartIds = [];
        view.tubeDesignerActivePartId = "";
        view.tubeDesignerPartMeasurementState = null;
        view.viewport?.setCustomVisibleEntityIds?.([]);
      }
    },
  });
}

function updateExportOperation(context, view, operationId, update = {}) {
  const operation = view.tubeDesignerExportOperation;
  if (!operation || operation.id !== operationId) return;
  const completed = Number(update.completed ?? operation.completed ?? 0);
  const total = Number(update.total ?? operation.total ?? 0);
  Object.assign(operation, update, {
    completed: Number.isFinite(completed) ? Math.max(0, completed) : 0,
    total: Number.isFinite(total) ? Math.max(0, total) : 0,
  });
  synchronizeExportOperationDom(context, operation);
}

function synchronizeExportOperationDom(context, operation) {
  const dialog = context.mount?.querySelector?.(".tube-designer-breakdown-dialog, .tube-designer-operation-wait");
  if (!dialog) return;
  const phase = String(operation?.phase ?? "preparing");
  const completed = Number(operation?.completed ?? 0);
  const total = Number(operation?.total ?? 0);
  const determinate = total > 0 && phase !== "selecting-directory" && phase !== "preparing";
  const percent = determinate ? Math.min(100, Math.max(0, (completed / total) * 100)) : 0;
  const title = exportOperationTitle(phase, completed, total);
  const titleNode = dialog.querySelector("[data-tube-designer-export-progress-title]");
  const messageNode = dialog.querySelector("[data-tube-designer-export-progress-message]");
  const countNode = dialog.querySelector("[data-tube-designer-export-progress-count]");
  const trackNode = dialog.querySelector("[data-tube-designer-export-progress-track]");
  const barNode = dialog.querySelector("[data-tube-designer-export-progress-bar]");
  if (titleNode) titleNode.textContent = title;
  if (messageNode) messageNode.textContent = String(operation?.message ?? "请保持当前窗口开启");
  if (countNode) countNode.textContent = total > 0 && phase !== "selecting-directory"
    ? `${Math.min(completed, total)} / ${total}`
    : "准备中";
  trackNode?.classList.toggle("is-indeterminate", !determinate);
  if (barNode) barNode.style.width = determinate ? `${percent}%` : "36%";
}

function exportOperationTitle(phase, completed, total) {
  if (phase === "selecting-directory") return "等待选择导出目录";
  if (phase === "exporting") return `正在导出 STEP（${Math.min(completed, total)} / ${total}）`;
  if (phase === "workbook") return "正在生成 Excel 零件清单";
  if (phase === "completed") return "正在确认导出结果";
  return "正在准备导出";
}

function reconcileSelections(view, designer) {
  const instanceIds = new Set((designer.instances ?? []).map((item) => item.entityId));
  view.tubeDesignerSelectedInstanceIds = (view.tubeDesignerSelectedInstanceIds ?? []).filter((id) => instanceIds.has(id));
  const partIds = new Set([
    ...(designer.manufacturingGroups ?? []).flatMap((group) => group.parts ?? []).map((part) => part.entityId),
    ...(designer.nestingGroups ?? []).flatMap((group) => group.parts ?? []).map((part) => part.entityId),
  ]);
  const currentPartIds = Array.isArray(view.tubeDesignerSelectedPartIds)
    ? view.tubeDesignerSelectedPartIds
    : [...partIds];
  view.tubeDesignerSelectedPartIds = currentPartIds.filter((id) => partIds.has(id));
}

function getVisibleBreakdownGroups(view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const groups = view.tubeDesignerBreakdownMode === "nesting-export"
    && Array.isArray(designer.nestingGroups)
    ? designer.nestingGroups
    : (designer.manufacturingGroups ?? []);
  const visibleIds = new Set(view.tubeDesignerBreakdownProductIds ?? groups.map((group) => group.productEntityId));
  return groups.filter((group) => visibleIds.has(group.productEntityId));
}

function getCurrentBreakdownGroups(view) {
  const groups = getVisibleBreakdownGroups(view);
  const requestedId = String(view.tubeDesignerBreakdownPageProductId ?? "");
  return [groups.find((group) => String(group.productEntityId ?? "") === requestedId) ?? groups[0]]
    .filter(Boolean);
}

function escapeCssAttribute(value) {
  return String(value).replace(/["\\]/g, (character) => `\\${character}`);
}

function makeInstanceName(template, createdAt) {
  const date = createdAt ? new Date(createdAt) : new Date();
  const timestamp = new Intl.DateTimeFormat("zh-CN", {
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit",
    hour12: false,
  }).format(date).replaceAll("/", "-");
  return `${getTemplateDisplayName(template)} ${timestamp}`;
}

function collectParameters(mount, formSelector, seed) {
  const result = { ...seed };
  const form = mount?.querySelector?.(formSelector);
  if (!form) throw new Error("参数表单尚未就绪。");
  for (const input of form.querySelectorAll("[data-tube-designer-parameter]")) {
    const name = input.dataset.tubeDesignerParameter;
    if (!name) continue;
    const optionType = input.selectedOptions?.[0]?.dataset?.tubeDesignerValueType;
    result[name] = input.type === "checkbox"
      ? input.checked
      : input.type === "number" || optionType === "number" ? Number(input.value)
      : optionType === "boolean" ? input.value === "true" : input.value;
  }
  return result;
}

function verifyViewportReceipt(viewContent, generationRunId, memberIds) {
  const receipt = viewContent?.viewportReceipt;
  if (!receipt?.applied || receipt.revision !== viewContent.revision) {
    throw new Error(`预览 ${generationRunId} 没有到达 View revision ${viewContent?.revision ?? "0"}。`);
  }
  const renderedEntityIds = new Set(receipt.entityIds ?? []);
  const missing = memberIds.filter((entityId) => !renderedEntityIds.has(entityId));
  if (missing.length) {
    throw new Error(`View revision ${viewContent.revision} 缺少 ${missing.length} 个预览构件。`);
  }
}

export function getDesignerDefaultViewDirection(product = null) {
  if (String(product?.templateId ?? "") === "single-face-security-window"
      && String(product?.parameters?.faceType ?? "single") === "two") {
    return String(product?.parameters?.sidePosition ?? "right") === "left"
      ? [-0.18, -1, 0.08]
      : [0.18, -1, 0.08];
  }
  return [0.18, -1, 0.08];
}

export function fitDesignerDefaultView(view, revision, product = null) {
  const expectedRevision = String(revision ?? "0");
  const fitReceipt = view.viewport?.fitViewForRevision?.(expectedRevision);
  if (!fitReceipt?.fitted || fitReceipt.revision !== expectedRevision) {
    throw new Error(`三维模型已显示，但没有在同一个 View revision ${expectedRevision} 适合窗口。`);
  }
  const defaultViewDirection = getDesignerDefaultViewDirection(product);
  if (!view.viewport?.setViewDirection?.(defaultViewDirection)) {
    throw new Error("三维模型已显示，但无法切换到从屋内正对窗外的默认视角。");
  }
  const applied = view.viewport?.getAppliedViewState?.();
  if (applied?.revision !== expectedRevision
    || Number(applied?.renderSequence ?? 0) <= Number(fitReceipt.renderSequence ?? 0)) {
    throw new Error(`默认视角没有应用到 View revision ${expectedRevision}。`);
  }
  return {
    revision: expectedRevision,
    defaultViewDirection,
    fitRenderSequence: fitReceipt.renderSequence,
    defaultViewRenderSequence: applied.renderSequence,
  };
}

async function acknowledgeOwnMutation(context, view) {
  view.tubeDesignerOwnMutation = true;
  try {
    await context.actions?.refreshActiveSceneState?.();
  } finally {
    view.tubeDesignerOwnMutation = false;
  }
}

function captureParameterPanelState(context, view, editedTarget = null) {
  captureInstanceListState(context, view);
  const panel = context.mount?.querySelector?.("[data-tube-designer-parameter-form]");
  if (!panel) return;
  const scroller = panel.querySelector("[data-tube-designer-parameter-scroll]")
    ?? panel.querySelector(".tube-designer-parameter-sections");
  const detailScroller = panel.querySelector("[data-tube-designer-product-detail-scroll]");
  const currentProductId = String(view.scene?.tubeDesigner?.product?.entityId ?? "");
  const disclosure = view.tubeDesignerParameterPanelProductId === currentProductId
    ? { ...(view.tubeDesignerParameterDisclosureState ?? {}) } : {};
  for (const group of panel.querySelectorAll("[data-tube-designer-parameter-group]")) {
    disclosure[group.dataset.tubeDesignerParameterGroup] = group.open;
  }
  view.tubeDesignerParameterDisclosureState = disclosure;
  view.tubeDesignerParameterPanelProductId = String(
    view.scene?.tubeDesigner?.product?.entityId ?? "",
  );
  view.tubeDesignerExpandedParameterGroups = Object.keys(disclosure).filter((key) => disclosure[key]);
  view.tubeDesignerParameterPanelScrollTop = Number(scroller?.scrollTop ?? 0);
  view.tubeDesignerParameterPanelScrollAnchor = captureScrollAnchor(scroller, editedTarget);
  view.tubeDesignerProductDetailScrollTop = Number(detailScroller?.scrollTop ?? 0);
  const activeElement = scroller?.ownerDocument?.activeElement ?? null;
  const parameterTarget = editedTarget
    ?? (scroller?.contains?.(activeElement) ? activeElement : null);
  const parameterKey = String(parameterTarget?.dataset?.tubeDesignerParameter ?? "").trim();
  if (parameterKey) {
    view.tubeDesignerLastEditedParameterKey = parameterKey;
    view.tubeDesignerRestoreParameterFocus = true;
  }
}

function restoreParameterPanelState(context, view) {
  if (String(view.tubeDesignerParameterPanelProductId ?? "") !== String(
    view.scene?.tubeDesigner?.product?.entityId ?? "",
  )) {
    restoreInstanceListState(context, view);
    return;
  }
  const panel = context.mount?.querySelector?.("[data-tube-designer-parameter-form]");
  const scroller = panel?.querySelector?.("[data-tube-designer-parameter-scroll]")
    ?? panel?.querySelector?.(".tube-designer-parameter-sections");
  if (!scroller) {
    restoreInstanceListState(context, view);
    return;
  }
  const detailScroller = panel?.querySelector?.("[data-tube-designer-product-detail-scroll]");
  if (detailScroller) {
    detailScroller.scrollTop = Number(view.tubeDesignerProductDetailScrollTop ?? 0);
  }
  const shouldRestoreFocus = !view.pending && Boolean(view.tubeDesignerRestoreParameterFocus);
  const restoredAnchor = restoreScrollAnchor(
    scroller,
    view.tubeDesignerParameterPanelScrollAnchor,
    { restoreFocus: shouldRestoreFocus },
  );
  if (!view.tubeDesignerParameterPanelScrollAnchor) {
    scroller.scrollTop = Number(view.tubeDesignerParameterPanelScrollTop ?? 0);
  }
  if (!shouldRestoreFocus) {
    restoreInstanceListState(context, view);
    return;
  }
  const parameterKey = String(view.tubeDesignerLastEditedParameterKey ?? "");
  const field = Array.from(panel.querySelectorAll("[data-tube-designer-parameter]"))
    .find((item) => String(item.dataset.tubeDesignerParameter ?? "") === parameterKey);
  if (!restoredAnchor) field?.focus?.({ preventScroll: true });
  view.tubeDesignerRestoreParameterFocus = false;
  restoreInstanceListState(context, view);
}

export function captureDesignerScrollState(context, view) {
  captureParameterPanelState(context, view);
}

export function restoreDesignerScrollState(context, view) {
  restoreParameterPanelState(context, view);
  const restorationToken = Number(view.tubeDesignerScrollRestorationToken ?? 0) + 1;
  view.tubeDesignerScrollRestorationToken = restorationToken;
  const restore = () => {
    if (view.tubeDesignerScrollRestorationToken !== restorationToken) return;
    restoreParameterPanelState(context, view);
  };
  queueMicrotask(() => {
    restore();
    const requestFrame = globalThis.requestAnimationFrame;
    if (typeof requestFrame !== "function") return;
    requestFrame(() => {
      restore();
      requestFrame(restore);
    });
  });
}

function captureInstanceListState(context, view) {
  const scroller = context.mount?.querySelector?.(".tube-designer-instance-list");
  if (!scroller) return;
  const activeId = String(view.scene?.tubeDesigner?.product?.entityId
    ?? view.scene?.tubeDesigner?.activeProductId ?? "").trim();
  const activeCard = activeId
    ? Array.from(scroller.querySelectorAll?.("[data-tube-designer-instance-id]") ?? [])
      .find((item) => String(item.dataset?.tubeDesignerInstanceId ?? "") === activeId)
    : null;
  view.tubeDesignerInstanceListScrollAnchor = captureScrollAnchor(scroller, activeCard);
}

function restoreInstanceListState(context, view) {
  const snapshot = view.tubeDesignerInstanceListScrollAnchor;
  if (!snapshot) return;
  const restore = () => {
    const scroller = context.mount?.querySelector?.(".tube-designer-instance-list");
    if (scroller) restoreScrollAnchor(scroller, snapshot);
  };
  restore();
  const restorationToken = Number(view.tubeDesignerInstanceListScrollRestorationToken ?? 0) + 1;
  view.tubeDesignerInstanceListScrollRestorationToken = restorationToken;
  queueMicrotask(() => {
    if (view.tubeDesignerInstanceListScrollRestorationToken !== restorationToken) return;
    restore();
  });
}

function captureAddDialogScrollAnchor(context, view, target) {
  const form = resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-add-form]");
  const templateId = form?.dataset?.tubeDesignerRenderedTemplateId;
  if (templateId) {
    view.tubeDesignerAddDisclosureStates ??= {};
    const saved = { ...(view.tubeDesignerAddDisclosureStates[templateId] ?? {}) };
    for (const group of form.querySelectorAll?.("details[data-tube-designer-parameter-group]") ?? []) {
      saved[group.dataset.tubeDesignerParameterGroup] = group.open;
    }
    view.tubeDesignerAddDisclosureStates[templateId] = saved;
  }
  const mount = resolveDesignerMount(context);
  const snapshots = [".tube-designer-template-list", ".tube-designer-config-parameters"]
    .map((scrollerSelector) => {
      const scroller = mount?.querySelector?.(scrollerSelector);
      if (!scroller) return null;
      const anchorTarget = scroller.contains?.(target) ? target : null;
      return { scrollerSelector, anchor: captureScrollAnchor(scroller, anchorTarget) };
    })
    .filter(Boolean);
  if (!snapshots.length) return;
  view.tubeDesignerAddScrollAnchors = snapshots;
  // Keep the old singular snapshot as a fallback for state left by an older
  // mounted view; new renders restore every independent scroll container.
  view.tubeDesignerAddScrollAnchor = snapshots.find((item) => item.anchor?.restoreFocus)
    ?? snapshots.find((item) => item.scrollerSelector === ".tube-designer-config-parameters")
    ?? snapshots[0];
}

function restoreAddDialogScrollAnchor(context, view) {
  const snapshots = Array.isArray(view.tubeDesignerAddScrollAnchors)
    ? view.tubeDesignerAddScrollAnchors
    : (view.tubeDesignerAddScrollAnchor ? [view.tubeDesignerAddScrollAnchor] : []);
  if (!snapshots.length) return;
  const restore = () => {
    const mount = resolveDesignerMount(context);
    for (const snapshot of snapshots) {
      const scroller = mount?.querySelector?.(snapshot.scrollerSelector);
      if (!scroller || !snapshot.anchor) continue;
      restoreScrollAnchor(scroller, snapshot.anchor, {
        restoreFocus: !view.pending && snapshot.anchor.restoreFocus,
      });
    }
  };
  restore();
  const restorationToken = Number(view.tubeDesignerAddScrollRestorationToken ?? 0) + 1;
  view.tubeDesignerAddScrollRestorationToken = restorationToken;
  queueMicrotask(() => {
    if (view.tubeDesignerAddScrollRestorationToken !== restorationToken) return;
    if (!view.tubeDesignerAddDialogOpen) return;
    restore();
  });
}

function mountAddDialog(context, designer, view) {
  const mount = resolveDesignerMount(context);
  const workbench = mount?.querySelector?.(".cam-workbench");
  if (!workbench?.insertAdjacentHTML) return false;
  for (const backdrop of workbench.querySelectorAll?.(
    ".tube-designer-modal-backdrop, .tube-designer-breakdown-backdrop",
  ) ?? []) backdrop.remove?.();
  workbench.insertAdjacentHTML("beforeend", renderDesignerAddDialog(designer, view));
  return true;
}

function refreshAddParameterContent(context, designer, view) {
  const form = resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-add-form]");
  if (!form) return false;
  form.innerHTML = renderDesignerAddParameterContent(designer, view);
  if (form.dataset) form.dataset.tubeDesignerRenderedTemplateId = view.tubeDesignerAddTemplateId;
  bindProductParameterDiagrams(resolveDesignerMount(context));
  return true;
}

function synchronizeSelectedTemplateCard(context, catalogEntryId) {
  const cards = resolveDesignerMount(context)?.querySelectorAll?.("[data-tube-designer-template-id]") ?? [];
  for (const card of cards) {
    const selected = String(card.dataset?.tubeDesignerCatalogEntryId ?? card.dataset?.tubeDesignerTemplateId ?? "") === String(catalogEntryId ?? "");
    card.classList?.toggle?.("selected", selected);
    card.setAttribute?.("aria-pressed", String(selected));
  }
}

function showAddTemplateSwitchProgress(context, options = {}) {
  const dialog = resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-add-dialog]");
  if (!dialog?.insertAdjacentHTML) return null;
  dialog.querySelector?.("[data-tube-designer-template-switch-progress]")?.remove?.();
  dialog.insertAdjacentHTML("beforeend", `
    <div class="tube-designer-add-template-progress" data-tube-designer-template-switch-progress role="status" aria-live="polite">
      <div class="tube-designer-export-progress-card">
        <span class="tube-designer-export-spinner" aria-hidden="true"></span>
        <strong>${escapeText(options.title ?? "正在切换模板")}</strong>
        <span>${escapeText(options.message ?? "正在更新参数与示意图")}</span>
        <div class="tube-designer-export-progress-track is-indeterminate"><i style="width:36%"></i></div>
        <small>${escapeText(options.phase ?? "正在更新")}</small>
        <em>完成后将自动恢复当前焦点</em>
      </div>
    </div>`);
  return dialog.querySelector?.("[data-tube-designer-template-switch-progress]") ?? null;
}

function resolveDesignerMount(context) {
  if (context.mount && context.mount.isConnected !== false) return context.mount;
  return globalThis.document?.querySelector?.("[data-product-surface='project']") ?? null;
}

function waitForPaint() {
  const requestFrame = globalThis.requestAnimationFrame;
  if (typeof requestFrame !== "function") return Promise.resolve();
  return new Promise((resolve) => {
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      globalThis.clearTimeout(fallback);
      resolve();
    };
    const fallback = globalThis.setTimeout(finish, 100);
    requestFrame(() => requestFrame(finish));
  });
}

function waitForMinimumDuration(startedAt, minimumVisibleMs) {
  const remaining = Number(minimumVisibleMs) - (nowMilliseconds() - Number(startedAt));
  if (!Number.isFinite(remaining) || remaining <= 0) return Promise.resolve();
  return new Promise((resolve) => globalThis.setTimeout(resolve, remaining));
}

function nowMilliseconds() {
  return globalThis.performance?.now?.() ?? Date.now();
}

function updateDesignerOperation(context, view, update = {}) {
  const operation = view.tubeDesignerOperation;
  if (!operation) return;
  Object.assign(operation, update);
  const overlay = context.mount?.querySelector?.("[data-tube-designer-operation-wait]");
  if (!overlay) return;
  const title = overlay.querySelector("[data-tube-designer-operation-title]");
  const message = overlay.querySelector("[data-tube-designer-operation-message]");
  const phase = overlay.querySelector("[data-tube-designer-operation-phase]");
  if (title) title.textContent = String(operation.title ?? "正在处理");
  if (message) message.textContent = String(operation.message ?? "正在等待后台任务完成");
  if (phase) phase.textContent = String(operation.phaseLabel ?? "处理中");
}

async function runDesignerOperation(context, view, ops, work, options = {}) {
  if (view.pending) return null;
  captureInstanceListState(context, view);
  let operationId = null;
  if (options.operation) {
    operationId = Number(view.tubeDesignerOperationSequence ?? 0) + 1;
    view.tubeDesignerOperationSequence = operationId;
    view.tubeDesignerOperation = { id: operationId, ...options.operation };
  }
  view.pending = true;
  view.error = "";
  view.notice = "";
  if (options.renderProject !== false) {
    ops.renderProject(context, view);
    restoreParameterPanelState(context, view);
  }
  const hasVisibleProgress = operationId !== null || Boolean(view.tubeDesignerExportOperation);
  let progressShownAt = null;
  if (hasVisibleProgress) {
    // Let the browser commit the overlay before starting potentially blocking native work.
    await waitForPaint();
    progressShownAt = nowMilliseconds();
  }
  try {
    return await work();
  } catch (error) {
    view.error = error?.message ?? String(error);
    throw error;
  } finally {
    if (progressShownAt != null) {
      await waitForMinimumDuration(
        progressShownAt,
        DESIGNER_OPERATION_PROGRESS_MINIMUM_VISIBLE_MS,
      );
    }
    options.beforeFinish?.();
    if (operationId !== null && view.tubeDesignerOperation?.id === operationId) {
      view.tubeDesignerOperation = null;
    }
    view.pending = false;
    if (options.renderProject !== false) {
      ops.renderProject(context, view);
      restoreParameterPanelState(context, view);
    }
  }
}

async function invokeDesignerRequest(context, method, payload, options = {}) {
  if (!context.sceneProxy) throw new Error(`${method} requires an active scene`);
  return context.sceneProxy.invoke(method, payload, { timeoutMs: 120000, ...options });
}

async function invokeProductRequest(context, method, payload, options = {}) {
  if (typeof context.productProxy?.invoke !== "function") {
    throw new Error(`${method} requires an active product`);
  }
  return context.productProxy.invoke(method, payload, { timeoutMs: 30000, ...options });
}
