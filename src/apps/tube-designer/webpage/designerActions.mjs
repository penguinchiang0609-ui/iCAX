import { getDefaultParameters, getTemplateById } from "./designerViews.mjs";
import {
  clearDesignerPartMeasurement,
  fitDesignerInspectedPart,
  setDesignerInspectedPartView,
  toggleDesignerAutomaticDimensions,
  toggleDesignerPartMeasurement,
} from "./partInspection.mjs";

export async function handleDesignerAreaAction(context, view, action, target, ops) {
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
    selectTemplate(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-parameter-change") {
    updateParameterDraft(context, view, target, ops);
    return { handled: true };
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
  if (action === "tube-designer-open-breakdown") {
    openBreakdownResults(context, view, ops);
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
  if (action === "tube-designer-open-part-inspection") {
    openPartInspection(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-close-part-inspection") {
    view.tubeDesignerPartInspectionOpen = false;
    view.tubeDesignerInspectedPartId = "";
    view.viewport?.setContinuousRendering?.(true);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-part-measurement") {
    return { handled: toggleDesignerPartMeasurement(context) };
  }
  if (action === "tube-designer-toggle-automatic-dimensions") {
    return { handled: toggleDesignerAutomaticDimensions(context) };
  }
  if (action === "tube-designer-clear-part-measurement") {
    return { handled: clearDesignerPartMeasurement(context) };
  }
  if (action === "tube-designer-inspection-fit-view") {
    return { handled: fitDesignerInspectedPart(context) };
  }
  if (action === "tube-designer-inspection-iso-view") {
    return { handled: setDesignerInspectedPartView(context, "iso") };
  }
  if (action === "tube-designer-export-selected" || action === "tube-designer-export-all") {
    return { handled: true, result: await exportSelected(context, view, target, ops) };
  }
  return false;
}

export async function handleDesignerRibbonCommand(context, view, commandId, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return true;
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
  return false;
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
    view.scene ??= {};
    view.scene.tubeDesigner = designer;
    view.tubeDesignerRightDraft = { ...(designer.product?.parameters ?? {}) };
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
  const template = designer.templates?.find((item) => item?.available) ?? null;
  if (!template) {
    view.error = "当前没有可用的产品模板。";
    ops.renderProject(context, view);
    return;
  }
  const createdAt = new Date().toISOString();
  view.tubeDesignerAddDialogOpen = true;
  view.tubeDesignerAddTemplateId = template.id;
  view.tubeDesignerAddCreatedAt = createdAt;
  view.tubeDesignerAddInstanceName = makeInstanceName(template, createdAt);
  view.tubeDesignerAddDraft = getDefaultParameters(designer.templates, template.id);
  view.tubeDesignerDisassemblySelectorOpen = false;
  view.tubeDesignerBreakdownOpen = false;
  view.error = "";
  ops.renderProject(context, view);
}

function closeAddDialog(context, view, ops) {
  view.tubeDesignerAddDialogOpen = false;
  view.tubeDesignerAddTemplateId = "";
  view.tubeDesignerAddDraft = null;
  view.error = "";
  ops.renderProject(context, view);
}

function selectTemplate(context, view, target, ops) {
  if (!view.tubeDesignerAddDialogOpen || view.pending) return;
  const designer = view.scene?.tubeDesigner ?? {};
  const templateId = String(target?.dataset?.tubeDesignerTemplateId ?? target?.value ?? "").trim();
  const template = getTemplateById(designer.templates, templateId);
  if (!template?.available) return;
  view.tubeDesignerAddTemplateId = template.id;
  view.tubeDesignerAddDraft = getDefaultParameters(designer.templates, template.id);
  view.tubeDesignerAddInstanceName = makeInstanceName(template, view.tubeDesignerAddCreatedAt);
  view.error = "";
  ops.renderProject(context, view);
}

function updateParameterDraft(context, view, target, ops) {
  if (view.pending || !target) return;
  const addForm = target.closest?.("[data-tube-designer-add-form]");
  const rightForm = target.closest?.("[data-tube-designer-parameter-form]");
  const designer = view.scene?.tubeDesigner ?? {};
  if (addForm) {
    const template = getTemplateById(designer.templates, view.tubeDesignerAddTemplateId);
    view.tubeDesignerAddDraft = normalizeDependentParameters(collectParameters(context.mount, "[data-tube-designer-add-form]", {
      ...getDefaultParameters(designer.templates, view.tubeDesignerAddTemplateId),
      ...(view.tubeDesignerAddDraft ?? {}),
    }), template, String(target?.dataset?.tubeDesignerParameter ?? ""));
  } else if (rightForm) {
    captureParameterPanelState(context, view, target);
    const template = getTemplateById(designer.templates, designer.product?.templateId);
    view.tubeDesignerRightDraft = normalizeDependentParameters(collectParameters(context.mount, "[data-tube-designer-parameter-form]", {
      ...getDefaultParameters(designer.templates, designer.product?.templateId),
      ...(view.tubeDesignerRightDraft ?? designer.product?.parameters ?? {}),
    }), template, String(target?.dataset?.tubeDesignerParameter ?? ""));
  } else {
    return;
  }
  ops.renderProject(context, view);
  restoreParameterPanelState(context, view);
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
  view.tubeDesignerSelectedInstanceIds = instances.map((item) => item.entityId);
  view.tubeDesignerDisassemblySelectorOpen = true;
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
  return runDesignerOperation(context, view, ops, async () => {
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
    view.tubeDesignerSelectedPartIds = groups.flatMap((group) => group.parts.map((part) => part.entityId));
    view.tubeDesignerDisassemblySelectorOpen = false;
    view.tubeDesignerBreakdownOpen = true;
    view.tubeDesignerLastOperation = {
      kind: "disassemble",
      productEntityIds,
      groupCount: groups.length,
      partCount: view.tubeDesignerSelectedPartIds.length,
    };
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
}

function openBreakdownResults(context, view, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const groups = view.scene?.tubeDesigner?.manufacturingGroups ?? [];
  if (!groups.length) {
    openDisassemblySelector(context, view, ops);
    return;
  }
  view.tubeDesignerBreakdownProductIds = groups.map((group) => group.productEntityId);
  view.tubeDesignerSelectedPartIds = groups.flatMap((group) => group.parts.map((part) => part.entityId));
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
  const groups = getVisibleBreakdownGroups(view);
  view.tubeDesignerSelectedPartIds = target?.checked
    ? groups.flatMap((group) => group.parts.map((part) => part.entityId))
    : [];
  synchronizeBreakdownSelection(view, target);
}

function synchronizeBreakdownSelection(view, sourceTarget = null) {
  const dialog = typeof sourceTarget?.closest === "function"
    ? sourceTarget.closest(".tube-designer-breakdown-dialog")
    : document.querySelector(".tube-designer-breakdown-dialog");
  if (!dialog) return;

  const selectedIds = new Set(view.tubeDesignerSelectedPartIds ?? []);
  const allParts = getVisibleBreakdownGroups(view).flatMap((group) => group.parts ?? []);
  const partIds = allParts.map((part) => String(part.entityId));
  const selectionCheckboxes = [...dialog.querySelectorAll("[data-tube-designer-selection-ids]")];
  for (const checkbox of selectionCheckboxes) {
    const checkboxPartIds = parseSelectionIds(checkbox);
    const checkedCount = checkboxPartIds.filter((partId) => selectedIds.has(partId)).length;
    checkbox.checked = checkboxPartIds.length > 0 && checkedCount === checkboxPartIds.length;
    checkbox.indeterminate = checkedCount > 0 && checkedCount < checkboxPartIds.length;
  }
  const selectedCount = partIds.filter((partId) => selectedIds.has(partId)).length;
  const allSelected = partIds.length > 0 && selectedCount === partIds.length;
  const selectAll = dialog.querySelector("[data-tube-designer-all-parts]");
  if (selectAll) {
    selectAll.checked = allSelected;
    selectAll.indeterminate = selectedCount > 0 && !allSelected;
  }

  const groupCount = getVisibleBreakdownGroups(view).length;
  const headingSummary = dialog.querySelector("[data-tube-designer-breakdown-summary]");
  if (headingSummary) {
    headingSummary.textContent = `${groupCount} 个产品 · ${partIds.length} 个零件 · 已选择 ${selectedCount} 个`;
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
  const collapsed = new Set(view.tubeDesignerCollapsedBreakdownProductIds ?? []);
  if (collapsed.has(productId)) collapsed.delete(productId);
  else collapsed.add(productId);
  view.tubeDesignerCollapsedBreakdownProductIds = [...collapsed];
  ops.renderProject(context, view);
}

function toggleCategoryTree(context, view, target, ops) {
  if (view.pending || view.tubeDesignerExportOperation) return;
  const categoryId = String(target?.dataset?.tubeDesignerCategoryId ?? "").trim();
  if (!categoryId) return;
  const expanded = new Set(view.tubeDesignerExpandedBreakdownCategoryIds ?? []);
  if (expanded.has(categoryId)) expanded.delete(categoryId);
  else expanded.add(categoryId);
  view.tubeDesignerExpandedBreakdownCategoryIds = [...expanded];
  ops.renderProject(context, view);
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
  const dialog = context.mount?.querySelector?.(".tube-designer-breakdown-dialog");
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
  view.tubeDesignerSelectedPartIds = (view.tubeDesignerSelectedPartIds ?? []).filter((id) => partIds.has(id));
}

function getVisibleBreakdownGroups(view) {
  const groups = view.scene?.tubeDesigner?.manufacturingGroups ?? [];
  const visibleIds = new Set(view.tubeDesignerBreakdownProductIds ?? groups.map((group) => group.productEntityId));
  return groups.filter((group) => visibleIds.has(group.productEntityId));
}

function makeInstanceName(template, createdAt) {
  const date = createdAt ? new Date(createdAt) : new Date();
  const timestamp = new Intl.DateTimeFormat("zh-CN", {
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit",
    hour12: false,
  }).format(date).replaceAll("/", "-");
  return `${template?.name ?? "产品"} ${timestamp}`;
}

function collectParameters(mount, formSelector, seed) {
  const result = { ...seed };
  const form = mount?.querySelector?.(formSelector);
  if (!form) throw new Error("参数表单尚未就绪。");
  for (const input of form.querySelectorAll("[data-tube-designer-parameter]")) {
    const name = input.dataset.tubeDesignerParameter;
    if (!name) continue;
    result[name] = input.type === "checkbox"
      ? input.checked
      : input.type === "number" ? Number(input.value) : input.value;
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
  view.tubeDesignerParameterPanelProductId = String(
    view.scene?.tubeDesigner?.product?.entityId ?? "",
  );
  view.tubeDesignerExpandedParameterGroups = Array.from(
    panel.querySelectorAll("[data-tube-designer-parameter-group][open]"),
    (group) => String(group.dataset.tubeDesignerParameterGroup ?? ""),
  ).filter(Boolean);
  view.tubeDesignerParameterPanelScrollTop = Number(sections?.scrollTop ?? 0);
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
  sections.scrollTop = Number(view.tubeDesignerParameterPanelScrollTop ?? 0);
  if (view.pending || !view.tubeDesignerRestoreParameterFocus) return;
  const parameterKey = String(view.tubeDesignerLastEditedParameterKey ?? "");
  const field = Array.from(panel.querySelectorAll("[data-tube-designer-parameter]"))
    .find((item) => String(item.dataset.tubeDesignerParameter ?? "") === parameterKey);
  field?.focus?.({ preventScroll: true });
  view.tubeDesignerRestoreParameterFocus = false;
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
  if (!context.sceneProxy || view.pending) return null;
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
  try {
    return await work();
  } catch (error) {
    view.error = error?.message ?? String(error);
    throw error;
  } finally {
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
