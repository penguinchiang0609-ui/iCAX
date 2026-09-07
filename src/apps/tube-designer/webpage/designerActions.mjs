import {
  buildPartCategories,
  applyReusablePresetValues,
  getDefaultParameters,
  getDefaultTemplate,
  getReusablePresetValues,
  getTemplateById,
  getTemplateDisplayName,
  renderDesignerAddDialog,
  renderDesignerAddParameterContent,
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
  profileSelectionKey,
  resolveSelectedProfileSketchSource,
  templateProfilesForProduct,
} from "./profileLibrary.mjs";
import { handlePartsAreaAction } from "./partsArea.mjs";
import { handleNestingSettingsAction, handleNestingSettingsRibbonCommand } from "./nestingSettings.mjs";
import { handleNestingRibbonCommand } from "./nestingWorkflow.mjs";
import { handleNestingExportAction } from "./nestingExport.mjs";
import {
  beginNewSectionSketch,
  beginProfileSectionSketch,
  handleSketchAreaAction,
  handleSketchRibbonCommand,
} from "./sketchArea.mjs";
import { getCatalogEntry, getCatalogEntryGroupKeys } from "./productCatalog.mjs";
import { handleComponentLibraryAction, handleComponentLibraryRibbonCommand } from "./componentLibrary.mjs";

export const DESIGNER_OPERATION_PROGRESS_MINIMUM_VISIBLE_MS = 500;
import { handleLicenseCommand } from "./licensing.mjs";
export const ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS = DESIGNER_OPERATION_PROGRESS_MINIMUM_VISIBLE_MS;

export async function handleDesignerAreaAction(context, view, action, target, ops) {
  if (action === "tube-designer-profile-library-new-sketch") {
    return { handled: true, result: await openProfileSectionSketch(context, view, ops, true) };
  }
  if (action === "tube-designer-profile-library-edit-sketch") {
    return { handled: true, result: await openProfileSectionSketch(context, view, ops, false) };
  }
  const componentResult = await handleComponentLibraryAction(context, view, action, target, ops);
  if (componentResult.handled) return componentResult;
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
  if (action === "tube-designer-open-add") {
    openAddDialog(context, view, ops);
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
    updateParameterDraft(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-profile-selection-change") {
    changeProfileSelection(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-profile-parameter-change") {
    return { handled: true, result: await updateParametricProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-import-profile-dxf") {
    return { handled: true, result: await importProfileDxf(context, view, target, ops) };
  }
  if (action === "tube-designer-clear-imported-profile") {
    clearImportedProfile(context, view, target, ops);
    return { handled: true };
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
    applyParameterPreset(context, view, target, ops);
    return { handled: true };
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
    return { handled: true, result: await disassembleSelected(context, view, ops) };
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
    await context.actions?.selectRibbonTab?.("nesting");
    return { handled: true };
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
  return false;
}

export async function handleDesignerRibbonCommand(context, view, commandId, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return true;
  if (await handleLicenseCommand(context, view, commandId, ops)) return true;
  if (commandId === "profiles.new-sketch") {
    await openProfileSectionSketch(context, view, ops, true);
    return true;
  }
  if (await handleNestingRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleNestingSettingsRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleSketchRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleProfileLibraryRibbonCommand(context, view, commandId, ops)) return true;
  if (await handleComponentLibraryRibbonCommand(context, view, commandId, ops)) return true;
  if (commandId === "designer.add" || commandId === "designer.generate") {
    openAddDialog(context, view, ops);
    return true;
  }
  if (commandId === "designer.batch-add" || commandId === "designer.import-excel") {
    await chooseBatchAddWorkbook(context, view, ops);
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
    return true;
  }
  return false;
}

async function openProfileSectionSketch(context, view, ops, createNew) {
  if (view.pending) return null;
  const current = view.tubeDesignerSketch?.section;
  if (current?.dirty && typeof globalThis.confirm === "function"
      && !globalThis.confirm("当前截面还有未保存的修改，确定开始另一个截面吗？")) return null;
  if (createNew) {
    const state = beginNewSectionSketch(view);
    await selectDesignerArea(context, view, "sketch");
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
    await selectDesignerArea(context, view, "sketch");
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

async function selectDesignerArea(context, view, areaId) {
  await context.actions?.selectRibbonTab?.(areaId);
  // The host may publish the selected tab on the next render tick. Keep this
  // render on the requested area as well, otherwise it can immediately fall
  // back to the source tab and appear as if navigation did nothing.
  context.activeRibbonTabId = areaId;
  view.activeAreaId = areaId;
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
      title: "选择批量添加 Excel 文件",
      filters: [{ name: "Excel 工作簿", extensions: ["xlsx", "xls"] }],
    }) ?? "").trim();
  }
  if (!sourcePath) return null;

  view.tubeDesignerBatchImportPath = sourcePath;
  view.error = "";
  const fileName = sourcePath.split(/[\\/]/).pop() || sourcePath;
  ops.appendProjectLog(context, "info", `选择批量添加文件：${sourcePath}`);
  ops.showNotice(context, view, `已选择 Excel 文件：${fileName}`);
  return { sourcePath };
}

export async function refreshDesignerState(context, view, ops = null) {
  if (!context.sceneProxy) return false;
  try {
    const response = await context.sceneProxy.invoke("TubeDesigner.List", {}, { timeoutMs: 30000 });
    const designer = response?.tubeDesigner ?? {};
    const previousProductId = String(view.scene?.tubeDesigner?.product?.entityId ?? "");
    view.scene ??= {};
    view.scene.tubeDesigner = designer;
    view.tubeDesignerRightDraft = { ...(designer.product?.parameters ?? {}) };
    if (previousProductId !== String(designer.product?.entityId ?? "")) {
      view.tubeDesignerRightPresetSelection = "";
    }
    reconcileSelections(view, designer);
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
  if (typeof context.productProxy?.invoke !== "function") {
    view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], profileId: "" };
    view.tubeDesignerSystemProfiles ??= [];
    view.tubeDesignerTemplateProfiles ??= [];
    return false;
  }
  try {
    const response = await context.productProxy.invoke(
      "TubeDesigner.ListUserData", {}, { timeoutMs: 30000 },
    );
    view.tubeDesignerUserData = {
      customers: Array.isArray(response?.customers) ? response.customers : [],
      parameterPresets: Array.isArray(response?.parameterPresets) ? response.parameterPresets : [],
      profiles: Array.isArray(response?.profiles) ? response.profiles : [],
      profileId: String(response?.profileId ?? ""),
    };
    view.tubeDesignerSystemProfiles = Array.isArray(response?.systemProfiles)
      ? response.systemProfiles : [];
    view.tubeDesignerTemplateProfiles = Array.isArray(response?.templateProfiles)
      ? response.templateProfiles : [];
    view.tubeDesignerUserDataError = "";
    ops?.renderProject?.(context, view);
    return true;
  } catch (error) {
    view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], profileId: "" };
    view.tubeDesignerSystemProfiles ??= [];
    view.tubeDesignerTemplateProfiles ??= [];
    view.tubeDesignerUserDataError = error?.message ?? String(error);
    ops?.renderProject?.(context, view);
    return false;
  }
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

function openAddDialog(context, view, ops) {
  const designer = view.scene?.tubeDesigner ?? {};
  const template = getDefaultTemplate(designer.templates ?? []);
  if (!template) {
    view.error = "当前没有可用的产品模板。";
    ops.renderProject(context, view);
    return;
  }
  const createdAt = new Date().toISOString();
  const entry = getCatalogEntry(designer.templates, template.id);
  view.tubeDesignerAddDialogOpen = true;
  view.tubeDesignerAddTemplateId = template.id;
  view.tubeDesignerAddCatalogPresetId = entry?.presetId ?? "";
  view.tubeDesignerAddCatalogEntryId = entry?.catalogEntryId ?? template.id;
  view.tubeDesignerAddCreatedAt = createdAt;
  view.tubeDesignerAddInstanceName = makeInstanceName({ ...template, name: entry?.displayName ?? template.name }, createdAt);
  view.tubeDesignerAddDraft = { ...getDefaultParameters(designer.templates, template.id), ...(entry?.catalogParameters ?? {}) };
  view.tubeDesignerAddPresetSelection = "";
  view.tubeDesignerExpandedTemplateGroupIds = getCatalogEntryGroupKeys(
    designer.templates, template.id, entry?.presetId ?? "",
  );
  view.tubeDesignerAddCatalogTabId = view.tubeDesignerExpandedTemplateGroupIds[0] ?? "";
  view.tubeDesignerAddScrollAnchor = null;
  view.tubeDesignerTemplateSwitchPending = false;
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.tubeDesignerBreakdownOpen = false;
  view.error = "";
  view.viewport?.setContinuousRendering?.(false);
  if (!mountAddDialog(context, designer, view)) ops.renderProject(context, view);
}

function closeAddDialog(context, view, ops) {
  view.tubeDesignerAddDialogOpen = false;
  view.tubeDesignerAddTemplateId = "";
  view.tubeDesignerAddCatalogPresetId = "";
  view.tubeDesignerAddCatalogEntryId = "";
  view.tubeDesignerAddDraft = null;
  view.tubeDesignerAddPresetSelection = "";
  view.tubeDesignerPresetDialog = null;
  view.tubeDesignerProfileDialog = null;
  view.tubeDesignerProfileLibraryDialog = null;
  view.tubeDesignerExpandedTemplateGroupIds = [];
  view.tubeDesignerAddCatalogTabId = "";
  view.tubeDesignerAddScrollAnchor = null;
  view.tubeDesignerTemplateSwitchPending = false;
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
  const template = getTemplateById(designer.templates, templateId);
  if (!template?.available) return;
  const presetId = String(target?.dataset?.tubeDesignerCatalogPresetId ?? "").trim();
  const entry = getCatalogEntry(designer.templates, template.id, presetId);
  if (!entry) return;
  // Clicking the active style must not wipe dimensions already entered.
  if (view.tubeDesignerAddCatalogEntryId === entry.catalogEntryId) return template.id;
  view.tubeDesignerTemplateSwitchPending = true;
  const progress = showAddTemplateSwitchProgress(context);
  await waitForPaint();
  const progressShownAt = progress ? nowMilliseconds() : null;
  try {
    view.tubeDesignerAddTemplateId = template.id;
    view.tubeDesignerAddCatalogPresetId = entry.presetId;
    view.tubeDesignerAddCatalogEntryId = entry.catalogEntryId;
    view.tubeDesignerExpandedTemplateGroupIds = getCatalogEntryGroupKeys(
      designer.templates, template.id, entry.presetId,
    );
    view.tubeDesignerAddCatalogTabId = view.tubeDesignerExpandedTemplateGroupIds[0] ?? "";
    view.tubeDesignerAddDraft = { ...getDefaultParameters(designer.templates, template.id), ...entry.catalogParameters };
    view.tubeDesignerAddPresetSelection = "";
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

function updateParameterDraft(context, view, target, ops) {
  if (view.pending || !target) return;
  const addForm = target.closest?.("[data-tube-designer-add-form]");
  const rightForm = target.closest?.("[data-tube-designer-parameter-form]");
  const designer = view.scene?.tubeDesigner ?? {};
  if (addForm) {
    captureAddDialogScrollAnchor(context, view, target);
    const template = getTemplateById(designer.templates, view.tubeDesignerAddTemplateId);
    view.tubeDesignerAddDraft = normalizeDependentParameters(collectParameters(context.mount, "[data-tube-designer-add-form]", {
      ...getDefaultParameters(designer.templates, view.tubeDesignerAddTemplateId),
      ...(view.tubeDesignerAddDraft ?? {}),
    }), template, String(target?.dataset?.tubeDesignerParameter ?? ""));
    markSelectedPresetModified(
      view, "add", template, String(target?.dataset?.tubeDesignerParameter ?? ""),
    );
  } else if (rightForm) {
    captureParameterPanelState(context, view, target);
    const template = getTemplateById(designer.templates, designer.product?.templateId);
    view.tubeDesignerRightDraft = normalizeDependentParameters(collectParameters(context.mount, "[data-tube-designer-parameter-form]", {
      ...getDefaultParameters(designer.templates, designer.product?.templateId),
      ...(view.tubeDesignerRightDraft ?? designer.product?.parameters ?? {}),
    }), template, String(target?.dataset?.tubeDesignerParameter ?? ""));
    markSelectedPresetModified(
      view, "right", template, String(target?.dataset?.tubeDesignerParameter ?? ""),
    );
  } else {
    return;
  }
  if (addForm) {
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
  } else {
    ops.renderProject(context, view);
    restoreParameterPanelState(context, view);
  }
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

function setProfileDraft(context, view, ops, mode, prefix, profile, builtInValue = null) {
  const { designer, isAdd, values } = profileActionState(view, mode);
  const overrides = { ...(values.tubeDesignerProfileOverrides ?? {}) };
  if (profile) overrides[prefix] = profile;
  else delete overrides[prefix];
  values.tubeDesignerProfileOverrides = overrides;
  if (builtInValue != null) values[`${prefix}ProfileType`] = builtInValue;
  if (isAdd) {
    view.tubeDesignerAddDraft = values;
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
  } else {
    view.tubeDesignerRightDraft = values;
    ops.renderProject(context, view);
    restoreParameterPanelState(context, view);
  }
}

function changeProfileSelection(context, view, target, ops) {
  if (view.pending || !target) return;
  const mode = String(target.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  const prefix = String(target.dataset?.tubeDesignerProfilePrefix ?? "").trim();
  const selection = String(target.value ?? "");
  if (!prefix || selection === "current") return;
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  if (selection.startsWith("builtin:")) {
    setProfileDraft(context, view, ops, mode, prefix, null, selection.slice("builtin:".length));
    return;
  }
  if (selection.startsWith("template:")) {
    const { templateId } = profileActionState(view, mode);
    const bundled = templateProfilesForProduct(view, templateId)
      .find((item) => profileSelectionKey(item) === selection);
    if (!bundled) {
      view.error = "所选管型不属于当前模板，或模板资源已经不存在。";
      ops.renderProject(context, view);
      return;
    }
    const snapshot = importedProfileSnapshot({
      ...(bundled.previewProfile ?? bundled.profile ?? {}),
      name: bundled.name ?? bundled.previewProfile?.name,
      profileScope: "template", templateId: bundled.templateId, profileDefinitionId: bundled.id,
    });
    if (!snapshot?.contours?.length) {
      view.error = "模板自带管型缺少可用截面。";
      ops.renderProject(context, view);
      return;
    }
    setProfileDraft(context, view, ops, mode, prefix, snapshot);
    return;
  }
  if (selection.startsWith("saved:")) {
    const id = selection.slice("saved:".length);
    const saved = (view.tubeDesignerUserData?.profiles ?? [])
      .find((item) => String(item?.id ?? "") === id);
    if (!saved) {
      view.error = "所选的我的管型已经不存在。";
      ops.renderProject(context, view);
      return;
    }
    const source = saved.profileType === "parametric-package"
      ? { ...(saved.previewProfile ?? {}), name: saved.name ?? saved.previewProfile?.name }
      : saved;
    const snapshot = importedProfileSnapshot(source, id);
    if (!snapshot?.contours?.length) {
      view.error = "所选管型缺少可用截面。";
      ops.renderProject(context, view);
      return;
    }
    setProfileDraft(context, view, ops, mode, prefix, snapshot);
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
  const bundled = current?.profileScope === "template";
  if (!prefix || !parameterKey || current?.kind !== "parametric-package"
      || (bundled ? !current.profileDefinitionId || String(current.templateId) !== templateId : !current?.savedProfileId)) {
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
        ...(bundled ? { profileRef: { scope: "template", templateId, id: current.profileDefinitionId } } : { id: current.savedProfileId }),
        parameters,
      }, { timeoutMs: 120000 });
      const profile = importedProfileSnapshot(response?.profile, current.savedProfileId);
      if (!profile?.contours?.length) throw new Error("可编辑管型没有返回有效截面。" );
      const nextValues = profileActionState(view, mode).values;
      nextValues.tubeDesignerProfileOverrides = {
        ...(nextValues.tubeDesignerProfileOverrides ?? {}),
        [prefix]: profile,
      };
      if (mode === "add") view.tubeDesignerAddDraft = nextValues;
      else view.tubeDesignerRightDraft = nextValues;
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
      else view.tubeDesignerRightDraft = values;
      ops.showNotice(context, view, `已为当前实例导入 ${profile.name ?? profile.sourceFileName ?? "DXF 管型"}。`);
      return profile;
    } finally {
      await waitForMinimumDuration(startedAt, ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS);
    }
  }, {
    operation: {
      kind: "profile-import",
      title: "正在导入 DXF 管型",
      phase: "parsing-profile",
      phaseLabel: "截面校验",
      message: "正在识别外轮廓、内孔、单位和精确曲线",
    },
  });
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
  return result;
}

function clearImportedProfile(context, view, target, ops) {
  if (view.pending) return;
  const mode = String(target?.dataset?.tubeDesignerProfileMode ?? "") === "add" ? "add" : "right";
  const prefix = String(target?.dataset?.tubeDesignerProfilePrefix ?? "").trim();
  if (!prefix) return;
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view, target);
  setProfileDraft(context, view, ops, mode, prefix, null);
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
      else view.tubeDesignerRightDraft = values;
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
      else view.tubeDesignerRightDraft = values;
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

function applyParameterPreset(context, view, target, ops) {
  if (view.pending || !target) return;
  const mode = String(target?.dataset?.tubeDesignerPresetMode ?? "") === "add" ? "add" : "right";
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
  if (selection.startsWith("builtin:")) {
    const value = selection.slice("builtin:".length);
    const preset = (definition?.presets ?? []).find((item) => String(item?.value ?? "") === value);
    values = applyReusablePresetValues(template, currentValues, preset?.values ?? {});
    if (selector) values[selector] = value;
  } else if (selection.startsWith("user:")) {
    const presetId = selection.slice("user:".length);
    const preset = (view.tubeDesignerUserData?.parameterPresets ?? [])
      .find((item) => String(item?.id ?? "") === presetId
        && String(item?.templateId ?? "") === String(template.id));
    if (!preset) return;
    values = applyReusablePresetValues(template, currentValues, preset.values ?? {});
    if (selector) values[selector] = String(definition?.customValue ?? "custom");
  } else if (selector) {
    values[selector] = String(definition?.customValue ?? "custom");
  }

  if (mode === "add") {
    captureAddDialogScrollAnchor(context, view, target);
    view.tubeDesignerAddDraft = values;
    view.tubeDesignerAddPresetSelection = selection;
    if (!refreshAddParameterContent(context, designer, view)) ops.renderProject(context, view);
    restoreAddDialogScrollAnchor(context, view);
  } else {
    captureParameterPanelState(context, view);
    view.tubeDesignerRightDraft = values;
    view.tubeDesignerRightPresetSelection = selection;
    ops.renderProject(context, view);
    restoreParameterPanelState(context, view);
  }
  focusPresetSelection(context, mode);
}

function markSelectedPresetModified(view, mode, template, changedKey) {
  if (!changedKey) return;
  const stateKey = mode === "add" ? "tubeDesignerAddPresetSelection" : "tubeDesignerRightPresetSelection";
  const selected = String(view[stateKey] ?? "");
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
  if (presetKeys.has(changedKey)) view[stateKey] = "custom";
}

function openParameterPresetDialog(context, view, target, ops) {
  if (view.pending) return;
  const mode = String(target?.dataset?.tubeDesignerPresetMode ?? "") === "add" ? "add" : "right";
  if (mode === "add") captureAddDialogScrollAnchor(context, view, target);
  else captureParameterPanelState(context, view);
  view.tubeDesignerPresetDialog = {
    mode,
    presetId: String(target?.dataset?.tubeDesignerPresetId ?? "").trim(),
  };
  ops.renderProject(context, view);
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
  queueMicrotask(() => resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-preset-name]")?.focus?.());
}

function closeParameterPresetDialog(context, view, ops) {
  const mode = view.tubeDesignerPresetDialog?.mode === "add" ? "add" : "right";
  view.tubeDesignerPresetDialog = null;
  ops.renderProject(context, view);
  if (mode === "add") restoreAddDialogScrollAnchor(context, view);
  else restoreParameterPanelState(context, view);
  focusPresetSelection(context, mode);
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
        customerId,
        values: getReusablePresetValues(template, currentValues),
      });
      const saved = response?.parameterPreset;
      if (!saved?.id) throw new Error("保存常用参数后没有返回记录标识。");
      upsertUserDataItem(view, "parameterPresets", saved);
      if (mode === "add") view.tubeDesignerAddPresetSelection = `user:${saved.id}`;
      else view.tubeDesignerRightPresetSelection = `user:${saved.id}`;
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
    focusPresetSelection(context, mode);
  }
}

async function deleteParameterPreset(context, view, target, ops) {
  if (view.pending) return null;
  const id = String(target?.dataset?.tubeDesignerPresetId ?? "").trim();
  const preset = (view.tubeDesignerUserData?.parameterPresets ?? [])
    .find((item) => String(item?.id ?? "") === id);
  if (!preset) return null;
  const mode = view.tubeDesignerPresetDialog?.mode === "add" ? "add" : "right";
  try {
    return await runDesignerOperation(context, view, ops, async () => {
      const response = await invokeProductRequest(context, "TubeDesigner.DeleteParameterPreset", {
        id,
        revision: Number(preset.revision ?? 0),
      });
      if (!response?.deleted) throw new Error("常用参数方案未能删除。");
      view.tubeDesignerUserData.parameterPresets = view.tubeDesignerUserData.parameterPresets
        .filter((item) => String(item?.id ?? "") !== id);
      if (mode === "add") view.tubeDesignerAddPresetSelection = "custom";
      else view.tubeDesignerRightPresetSelection = "custom";
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
    focusPresetSelection(context, mode);
  }
}

function upsertUserDataItem(view, collection, item) {
  view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], profileId: "" };
  const items = Array.isArray(view.tubeDesignerUserData[collection])
    ? view.tubeDesignerUserData[collection] : [];
  const index = items.findIndex((candidate) => String(candidate?.id ?? "") === String(item?.id ?? ""));
  if (index >= 0) items[index] = item;
  else items.push(item);
  view.tubeDesignerUserData[collection] = items;
}

function focusPresetSelection(context, mode) {
  queueMicrotask(() => {
    const selectors = resolveDesignerMount(context)?.querySelectorAll?.("[data-tube-designer-preset-selection]") ?? [];
    Array.from(selectors)
      .find((element) => String(element?.dataset?.tubeDesignerPresetSelection ?? "") === mode)
      ?.focus?.({ preventScroll: true });
  });
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
  return result;
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
    updateDesignerOperation(context, view, {
      phase: "synchronizing-view",
      phaseLabel: "显示同步",
      message: `几何生成完成，正在把 ${memberIds.length} 个构件同步到三维视图`,
    });
    view.scene ??= {};
    view.scene.tubeDesigner = nextDesigner;
    view.tubeDesignerRightDraft = { ...(nextDesigner.product?.parameters ?? {}) };
    view.tubeDesignerBreakdownOpen = false;
    view.tubeDesignerDisassemblySelectorOpen = false;
    view.tubeDesignerSelectedPartIds = [];

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
    ops.showNotice(context, view, isAdd ? "产品实例已添加并生成预览。" : "参数已确认，产品预览已重新生成。");
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
    view.tubeDesignerRightDraft = { ...(nextDesigner.product.parameters ?? {}) };
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
  view.error = "";
  ops.renderProject(context, view);
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

async function disassembleSelected(context, view, ops) {
  const productEntityIds = [...new Set(view.tubeDesignerSelectedInstanceIds ?? [])];
  if (!productEntityIds.length) {
    view.error = "请至少选择一个要拆单的产品实例。";
    ops.renderProject(context, view);
    return null;
  }
  const result = await runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(context, "TubeDesigner.DisassembleSelected", { productEntityIds });
    const designer = response?.tubeDesigner ?? {};
    const groupIdSet = new Set(productEntityIds);
    const groups = (designer.manufacturingGroups ?? []).filter((group) => groupIdSet.has(group.productEntityId));
    if (groups.length !== productEntityIds.length || groups.some((group) => !(group.parts?.length > 0))) {
      throw new Error("批量拆单没有返回完整的产品零件组。");
    }
    const partCount = groups.reduce((count, group) => count + group.parts.length, 0);
    updateDesignerOperation(context, view, {
      phase: "organizing-results",
      phaseLabel: "结果整理",
      message: `拆分完成，正在整理 ${groups.length} 个产品、${partCount} 个零件`,
    });
    view.scene ??= {};
    view.scene.tubeDesigner = designer;
    view.tubeDesignerBreakdownProductIds = productEntityIds;
    view.tubeDesignerBreakdownPageProductId = String(productEntityIds[0] ?? "");
    view.tubeDesignerSelectedPartIds = groups.flatMap((group) => group.parts.map((part) => part.entityId));
    view.tubeDesignerDisassemblySelectorOpen = false;
    view.tubeDesignerBreakdownOpen = true;
    view.tubeDesignerActivePartId = String(groups[0]?.parts?.[0]?.entityId ?? "");
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerLastOperation = {
      kind: "disassemble",
      productEntityIds,
      groupCount: groups.length,
      partCount: view.tubeDesignerSelectedPartIds.length,
    };
    view.tubeDesignerPostDisassemblyChoice = null;
    await acknowledgeOwnMutation(context, view);
    ops.showNotice(context, view, `拆单完成：${groups.length} 个产品，共 ${view.tubeDesignerSelectedPartIds.length} 个零件。`);
    return view.tubeDesignerLastOperation;
  }, {
    operation: {
      kind: "disassemble",
      title: "正在生成加工拆单",
      phase: "building-parts",
      phaseLabel: "零件生成",
      message: `正在拆分所选的 ${productEntityIds.length} 个产品实例`,
    },
  });
  return result;
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
  view.tubeDesignerDisassemblySelectorOpen = false;
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
  const partExists = (view.scene?.tubeDesigner?.manufacturingGroups ?? [])
    .some((group) => (group.parts ?? []).some((part) => String(part.entityId) === partId));
  if (!partExists) return;
  view.viewport?.setContinuousRendering?.(false);
  view.tubeDesignerInspectedPartId = partId;
  view.tubeDesignerPartInspectionOpen = true;
  ops.renderProject(context, view);
}

function parseSelectionIds(target) {
  return String(target?.dataset?.tubeDesignerSelectionIds ?? target?.dataset?.tubeDesignerPartId ?? "")
    .split(/\s+/)
    .map((value) => value.trim())
    .filter(Boolean);
}

async function exportSelected(context, view, target, ops) {
  if (!context.sceneProxy || view.pending || view.tubeDesignerExportOperation) return null;
  const partEntityIds = [...new Set(view.tubeDesignerSelectedPartIds ?? [])];
  if (!partEntityIds.length) {
    view.error = "请至少选择一个零件。";
    ops.renderProject(context, view);
    return null;
  }
  const operationId = Number(view.tubeDesignerExportSequence ?? 0) + 1;
  view.tubeDesignerExportSequence = operationId;
  view.tubeDesignerExportOperation = {
    id: operationId,
    phase: "selecting-directory",
    completed: 0,
    total: partEntityIds.length,
    message: "请选择导出总目录",
  };
  return runDesignerOperation(context, view, ops, async () => {
    let targetDirectory = String(target?.dataset?.tubeDesignerExportDirectory ?? "").trim();
    if (!targetDirectory) {
      const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
      if (typeof bridge?.openDirectoryDialog !== "function") {
        throw new Error("当前宿主没有提供目录选择能力。");
      }
      targetDirectory = String(await bridge.openDirectoryDialog({
        title: `选择 ${getVisibleBreakdownGroups(view).length} 个产品的 STEP 导出总目录`,
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
      partEntityIds,
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
    ops.showNotice(context, view, `已导出 ${view.tubeDesignerLastOperation.exportedGroups.length} 个产品、${view.tubeDesignerLastOperation.exportedCount} 个零件及 Excel 清单。`);
    return view.tubeDesignerLastOperation;
  }, {
    beforeFinish: () => {
      if (view.tubeDesignerExportOperation?.id === operationId) {
        view.tubeDesignerExportOperation = null;
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
  const partIds = new Set((designer.manufacturingGroups ?? []).flatMap((group) => group.parts ?? []).map((part) => part.entityId));
  const currentPartIds = Array.isArray(view.tubeDesignerSelectedPartIds)
    ? view.tubeDesignerSelectedPartIds
    : [...partIds];
  view.tubeDesignerSelectedPartIds = currentPartIds.filter((id) => partIds.has(id));
}

function getVisibleBreakdownGroups(view) {
  const groups = view.scene?.tubeDesigner?.manufacturingGroups ?? [];
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
  if (String(product?.templateId ?? "") === "two-face-security-window") {
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
  const panel = context.mount?.querySelector?.("[data-tube-designer-parameter-form]");
  if (!panel) return;
  const sections = panel.querySelector(".tube-designer-parameter-sections");
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
  view.tubeDesignerParameterPanelScrollTop = Number(sections?.scrollTop ?? 0);
  view.tubeDesignerParameterPanelScrollAnchor = captureScrollAnchor(sections, editedTarget);
  const parameterKey = String(editedTarget?.dataset?.tubeDesignerParameter ?? "").trim();
  if (parameterKey) {
    view.tubeDesignerLastEditedParameterKey = parameterKey;
    view.tubeDesignerRestoreParameterFocus = true;
  }
}

function restoreParameterPanelState(context, view) {
  if (String(view.tubeDesignerParameterPanelProductId ?? "") !== String(
    view.scene?.tubeDesigner?.product?.entityId ?? "",
  )) return;
  const panel = context.mount?.querySelector?.("[data-tube-designer-parameter-form]");
  const sections = panel?.querySelector?.(".tube-designer-parameter-sections");
  if (!sections) return;
  const shouldRestoreFocus = !view.pending && Boolean(view.tubeDesignerRestoreParameterFocus);
  const restoredAnchor = restoreScrollAnchor(
    sections,
    view.tubeDesignerParameterPanelScrollAnchor,
    { restoreFocus: shouldRestoreFocus },
  );
  if (!view.tubeDesignerParameterPanelScrollAnchor) {
    sections.scrollTop = Number(view.tubeDesignerParameterPanelScrollTop ?? 0);
  }
  if (!shouldRestoreFocus) return;
  const parameterKey = String(view.tubeDesignerLastEditedParameterKey ?? "");
  const field = Array.from(panel.querySelectorAll("[data-tube-designer-parameter]"))
    .find((item) => String(item.dataset.tubeDesignerParameter ?? "") === parameterKey);
  if (!restoredAnchor) field?.focus?.({ preventScroll: true });
  view.tubeDesignerRestoreParameterFocus = false;
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
  const scroller = target?.closest?.(".tube-designer-template-list, .tube-designer-config-parameters");
  if (!scroller) return;
  view.tubeDesignerAddScrollAnchor = {
    scrollerSelector: scroller.classList?.contains?.("tube-designer-template-list")
      ? ".tube-designer-template-list"
      : ".tube-designer-config-parameters",
    anchor: captureScrollAnchor(scroller, target),
  };
}

function restoreAddDialogScrollAnchor(context, view) {
  const snapshot = view.tubeDesignerAddScrollAnchor;
  if (!snapshot?.scrollerSelector || !snapshot.anchor) return;
  const restore = () => {
    const scroller = resolveDesignerMount(context)?.querySelector?.(snapshot.scrollerSelector);
    restoreScrollAnchor(scroller, snapshot.anchor, { restoreFocus: !view.pending && snapshot.anchor.restoreFocus });
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

function showAddTemplateSwitchProgress(context) {
  const dialog = resolveDesignerMount(context)?.querySelector?.("[data-tube-designer-add-dialog]");
  if (!dialog?.insertAdjacentHTML) return null;
  dialog.querySelector?.("[data-tube-designer-template-switch-progress]")?.remove?.();
  dialog.insertAdjacentHTML("beforeend", `
    <div class="tube-designer-add-template-progress" data-tube-designer-template-switch-progress role="status" aria-live="polite">
      <div class="tube-designer-export-progress-card">
        <span class="tube-designer-export-spinner" aria-hidden="true"></span>
        <strong>正在切换模板</strong>
        <span>正在更新参数与示意图</span>
        <div class="tube-designer-export-progress-track is-indeterminate"><i style="width:36%"></i></div>
        <small>正在更新</small>
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
  let operationId = null;
  if (options.operation) {
    operationId = Number(view.tubeDesignerOperationSequence ?? 0) + 1;
    view.tubeDesignerOperationSequence = operationId;
    view.tubeDesignerOperation = { id: operationId, ...options.operation };
  }
  view.pending = true;
  view.error = "";
  view.notice = "";
  ops.renderProject(context, view);
  restoreParameterPanelState(context, view);
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
    ops.renderProject(context, view);
    restoreParameterPanelState(context, view);
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
