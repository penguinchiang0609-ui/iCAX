import { availableParameterChoices, parameterEnabled, parameterVisible } from "./parameterConditions.mjs";
import { hasLicenseFeature } from "./licensing.mjs";
import { resourceEditLicenseFeatures } from "./resourceLicensing.mjs";
import { parameterAutoFillPatch } from "./parameterAutoFill.mjs";
import { renderToolParameterDiagram } from "./toolParameterDiagram.mjs";
import { bindParameterDiagramScopes } from "./parameterDiagramBinding.mjs";
import { libraryDiagramPositionStyle, renderDiagramResizeHandles } from "./floatingParameterDiagram.mjs";
import { createThreeViewport } from "../../../iCAX-UI/SDK/Viewport/threeViewport.mjs";
import { attachViewCube, renderViewCube, stopViewCubeAnimation } from "../../_shared/workbench/viewport/viewCube.mjs";
import { buildIntegratedFormedPreview } from "./integratedFormedPreview.mjs";
import { buildAssemblyFinishedShapePreview } from "./assemblyFinishedShape.mjs";
import { buildAssemblyPreviewDimensionAnnotations } from "./assemblyPreviewAnnotations.mjs";
import { renderAssemblyCatalogueIllustration } from "./assemblyCatalogueIllustration.mjs";
import { assemblyProcessInput, assemblyProcessWithInput, finishedProductInput, finishedProductKey, finishedProductShape,
  finishedProductState } from "./finishedProductModel.mjs";
import { handleStockProcessAction, markStockProcessEdited, renderStockProcessEditor, restoreStockProcessPlan,
  selectedStockOperation, stockOperationSettings, stockProcessPayload, stockProcessState,
  supportsStockOperations } from "./assemblyProcessEditor.mjs";
import { bindFinishedProductEditor, handleFinishedProductAction, renderFinishedProductEditor } from "./finishedProductEditor.mjs";
import { libraryProfiles, profileName, profileRef, profileScope, profileSelectionKey, profileSnapshot } from "./profileLibrary.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import { renderProfileSvg } from "./profileSvg.mjs";
import {
  assemblyLayoutShapeLabel,
  assemblyParameterDefaults,
  assemblyPresentationShape,
  assemblyShapeOrder,
  assemblyTemplateById,
  normalizeAssemblyCatalogue,
} from "./assemblyCatalog.mjs";

const supportsExampleScene = (template) => !!template.exampleInput?.shapeId;

export function assemblyLibraryState(view) {
  const state = view.tubeDesignerAssemblyLibrary ??= {};
  state.search ??= "";
  state.selectedId ??= "";
  state.collapsed ??= [];
  state.parameterDrafts ??= {};
  state.processDrafts ??= {};
  state.localAnchorDrafts ??= {};
  state.localProcessInputs ??= {};
  state.sceneDrafts ??= {};
  state.sceneParameterDrafts ??= {};
  state.sceneMemberDisclosure ??= {};
  state.sceneProfileDiagramCache ??= new Map();
  state.catalogueStatus ??= "idle";
  state.catalogueError ??= "";
  state.cataloguePromise ??= null;
  state.preview ??= null;
  state.previewRequest ??= null;
  state.previewFailureKey ??= "";
  state.previewError ??= "";
  state.selectionRequest ??= null;
  state.selectionFailureKey ??= "";
  state.selectionValidatedKey ??= "";
  state.appliedKey ??= "";
  state.projectionMode ??= "perspective";
  state.exploded ??= false;
  state.showDiagram ??= false;
  state.processInputMode ??= "local-parts";
  state.diagramMode ??= "product";
  // The assembly scene is the source of truth: always show the whole result.
  // Part-isolation controls duplicated that information and made the workflow
  // look like a relation inspector instead of an assembly editor.
  state.focusRole = "";
  const templates = assemblyTemplates(view);
  const selectable = templates.filter(supportsExampleScene);
  if (!assemblyTemplateById(selectable, state.selectedId)) state.selectedId = selectable[0]?.id ?? "";
  const selectedShape = assemblyTemplateById(templates, state.selectedId)?.exampleInput?.shapeId;
  if (selectedShape) {
    finishedProductState(view, selectedShape);
  }
  return state;
}

export function assemblyTemplates(view) {
  return normalizeAssemblyCatalogue(view?.tubeDesignerAssemblyTemplates, view);
}

export function addImportedAssemblyTemplate(view, template) {
  const imported = normalizeAssemblyCatalogue([{ ...template, libraryScope: "user" }], view)[0];
  if (!imported) throw new Error("导入装配工艺后没有返回有效的模板记录。");
  view.tubeDesignerAssemblyTemplates = [
    ...assemblyTemplates(view).filter((item) => item.id !== imported.id), imported,
  ];
  const state = assemblyLibraryState(view);
  state.catalogueStatus = "ready";
  state.catalogueError = "";
  return imported;
}

export function selectedAssemblyTemplate(view) {
  const templates = assemblyTemplates(view);
  return assemblyTemplateById(templates, assemblyLibraryState(view).selectedId) ?? templates[0];
}

export function assemblyParameterValues(view, template = selectedAssemblyTemplate(view)) {
  template = assemblyProcessWithInput(template, view);
  const state = assemblyLibraryState(view);
  if (state.processInputMode === "stock-operation" && supportsStockOperations(template)) {
    const row = selectedStockOperation(view, template);
    return { ...stockOperationSettings(view, template).parameters, angle: row?.angle, planeRotation: row?.rotation };
  }
  const declaredKeys = new Set((template?.parameters ?? []).map((item) => item.key));
  const drafts = Object.fromEntries(Object.entries(state.parameterDrafts[template?.id] ?? {})
    .filter(([key]) => declaredKeys.has(key)));
  const values = { ...assemblyParameterDefaults(template), ...drafts };
  if (!template) return values;
  const shared = template.exampleInput
    ? finishedProductInput(view).parameters
    : state.sceneParameterDrafts?.[assemblySceneSignature(template)] ?? {};
  for (const definition of template.parameters.filter((item) => item.scope === "scene")) {
    const key = String(definition.sceneKey || definition.key);
    if (!Object.hasOwn(shared, key)) continue;
    values[definition.key] = shared[key];
  }
  return values;
}

function assemblySceneParameterAccepts(definition, value, values) {
  if (definition.valueType === "number" || definition.valueType === "integer") {
    return Number.isFinite(value) && (definition.valueType !== "integer" || Number.isInteger(value))
      && (definition.min == null || value >= definition.min)
      && (definition.max == null || value <= definition.max);
  }
  if (definition.valueType === "boolean") return typeof value === "boolean";
  if (definition.valueType === "choice") return availableParameterChoices(definition, values)
    .some((option) => String(option?.value ?? option) === String(value));
  return typeof value === "string";
}

function assemblySceneParameterWarnings(view, template, values) {
  const shared = assemblyLibraryState(view).sceneParameterDrafts?.[assemblySceneSignature(template)] ?? {};
  return Object.fromEntries(template.parameters.filter((item) => item.scope === "scene").flatMap((definition) => {
    const key = String(definition.sceneKey || definition.key);
    if (!Object.hasOwn(shared, key) || assemblySceneParameterAccepts(definition, shared[key], values)) return [];
    const unit = definition.unit || "";
    return [[definition.key, `已保存 ${shared[key]}${unit}；超出此工艺范围，当前使用 ${definition.defaultValue}${unit}`]];
  }));
}

export function assemblyPreviewParameterGroups(view, template = selectedAssemblyTemplate(view)) {
  template = assemblyProcessWithInput(template, view);
  const values = assemblyParameterValues(view, template);
  const sceneKeys = new Set((template?.parameters ?? []).filter((item) => item.scope === "scene")
    .map((item) => item.key));
  return {
    parameters: Object.fromEntries(Object.entries(values).filter(([key]) => !sceneKeys.has(key))),
    sceneParameters: Object.fromEntries(Object.entries(values).filter(([key]) => sceneKeys.has(key))),
  };
}

// Example members are saved by a shape-level scene slot. A process template
// may call the same physical member by another role name or list it in a
// different order; map the draft onto that template's own roles only when
// building a request.
function assemblySceneSlot(part) {
  return String(part.sceneSlot || part.role);
}

function assemblySceneSignature(template) {
  const shape = assemblyPresentationShape(template);
  const slots = (template.previewScene?.designParts ?? []).map(assemblySceneSlot).sort();
  return `${shape}:${slots.join("|")}`;
}

export function assemblySceneParts(view, template = selectedAssemblyTemplate(view)) {
  if (!template) return {};
  template = assemblyProcessWithInput(template, view);
  if (template.exampleInput) {
    const product = finishedProductInput(view);
    return Object.fromEntries(Object.entries(template.exampleInput.roles)
      .map(([role, id]) => [role, clone(product.spans[id])]));
  }
  const state = assemblyLibraryState(view);
  const drafts = state.sceneDrafts?.[assemblySceneSignature(template)];
  const groups = assemblyPreviewParameterGroups(view, template);
  const plan = state.preview?.plan;
  const resolved = plan?.templateId === template.id
    && JSON.stringify(plan.parameters ?? {}) === JSON.stringify(groups.parameters)
    && JSON.stringify(plan.sceneParameters ?? {}) === JSON.stringify(groups.sceneParameters)
    ? plan.sceneParts ?? {} : {};
  const profiles = libraryProfiles(view);
  return Object.fromEntries((template.previewScene?.designParts ?? []).map((part) => {
    const draft = drafts?.[assemblySceneSlot(part)];
    const literalParameters = Object.fromEntries(Object.entries(part.parameters ?? {})
      .filter(([, value]) => !containsSceneExpression(value)));
    const source = resolved[part.role] ?? {
      profileRef: part.profileRef,
      parameters: literalParameters,
      length: part.length,
    };
    const selectedRef = draft?.profileRef ?? source.profileRef;
    const profile = profiles.find((item) => profileScope(item) === selectedRef?.scope
      && String(item.id) === String(selectedRef?.id));
    const catalogueDefaults = profile?.defaultParameters ?? Object.fromEntries(
      (profile?.descriptor?.parameters ?? []).map((item) => [item.key, item.defaultValue]));
    const sourceParameters = draft?.profileRef ? catalogueDefaults
      : { ...catalogueDefaults, ...(source.parameters ?? {}) };
    return [part.role, {
      profileRef: { ...(selectedRef ?? {}) },
      parameters: { ...sourceParameters, ...(draft?.parameters ?? {}) },
      length: draft?.length ?? source.length,
    }];
  }));
}

function containsSceneExpression(value) {
  if (typeof value === "string") return value.includes("$");
  if (Array.isArray(value)) return value.some(containsSceneExpression);
  if (value && typeof value === "object") return Object.values(value).some(containsSceneExpression);
  return false;
}

function assemblySceneDraftParts(view, template) {
  const drafts = assemblyLibraryState(view).sceneDrafts?.[assemblySceneSignature(template)];
  if (!drafts || typeof drafts !== "object") return {};
  return Object.fromEntries((template.previewScene?.designParts ?? []).flatMap((part) => {
    const draft = drafts[assemblySceneSlot(part)];
    if (!draft) return [];
    const override = {};
    if (draft.profileRef) override.profileRef = { ...draft.profileRef };
    if (draft.parameters && Object.keys(draft.parameters).length) override.parameters = { ...draft.parameters };
    if (draft.length != null) override.length = draft.length;
    return Object.keys(override).length ? [[part.role, override]] : [];
  }));
}

function updateAssemblyScenePart(view, template, role, change) {
  if (template.exampleInput) {
    const product = finishedProductInput(view);
    const id = template.exampleInput.roles[role];
    const previous = product.spans[id];
    if (!previous) return false;
    product.spans[id] = { ...previous, ...change,
      parameters: change.profileRef ? { ...change.parameters }
        : { ...previous.parameters, ...change.parameters } };
    return true;
  }
  const parts = template.previewScene?.designParts ?? [];
  const part = parts.find((item) => item.role === role);
  if (!part) return false;
  const state = assemblyLibraryState(view);
  const signature = assemblySceneSignature(template);
  const slot = assemblySceneSlot(part);
  const drafts = { ...(state.sceneDrafts[signature] ?? {}) };
  const previous = drafts[slot] ?? {};
  drafts[slot] = {
    ...previous,
    ...change,
    ...(change.profileRef ? { parameters: { ...(change.parameters ?? {}) } }
      : change.parameters ? { parameters: { ...(previous.parameters ?? {}), ...change.parameters } } : {}),
  };
  state.sceneDrafts[signature] = drafts;
  return true;
}

function preserveAssemblySceneOnTemplateChange(view, currentTemplate, nextTemplate) {
  if (currentTemplate?.exampleInput || nextTemplate?.exampleInput) return;
  if (!currentTemplate || !nextTemplate
      || assemblySceneSignature(currentTemplate) !== assemblySceneSignature(nextTemplate)) return;
  const state = assemblyLibraryState(view);
  const signature = assemblySceneSignature(currentTemplate);
  const currentParts = assemblySceneParts(view, currentTemplate);
  const drafts = { ...(state.sceneDrafts[signature] ?? {}) };
  for (const part of currentTemplate.previewScene?.designParts ?? []) {
    const current = currentParts[part.role];
    if (!current) continue;
    drafts[assemblySceneSlot(part)] = {
      profileRef: { ...current.profileRef },
      parameters: { ...current.parameters },
      length: current.length,
    };
  }
  state.sceneDrafts[signature] = drafts;
  const shared = { ...(state.sceneParameterDrafts[signature] ?? {}) };
  const values = assemblyParameterValues(view, currentTemplate);
  for (const definition of currentTemplate.parameters ?? []) {
    const key = definition.sceneKey || definition.key;
    if (definition.scope === "scene" && !Object.hasOwn(shared, key)) shared[key] = values[definition.key];
  }
  state.sceneParameterDrafts[signature] = shared;
}

export function ensureAssemblyLibraryCatalogue(context, view, ops) {
  const state = assemblyLibraryState(view);
  if (state.cataloguePromise) return state.cataloguePromise;
  if (state.catalogueStatus === "ready" || typeof context?.sceneProxy?.invoke !== "function") return Promise.resolve();
  state.catalogueStatus = "loading";
  state.catalogueError = "";
  state.catalogueWarnings = [];
  const request = context.sceneProxy.invoke("TubeDesigner.GetAssemblyTemplates", {}, { timeoutMs: 30000 })
    .then((response) => {
      if (Array.isArray(response?.finishedShapes)) view.tubeDesignerFinishedShapeCatalogue = response.finishedShapes;
      const templates = normalizeAssemblyCatalogue(response?.assemblies, view);
      if (!templates.length) {
        const details = Array.isArray(response?.errors) ? response.errors.filter(Boolean).join("；") : "";
        throw new Error(details || "装配模板目录为空，请检查模板资源是否完整。");
      }
      view.tubeDesignerAssemblyTemplates = templates;
      state.catalogueStatus = "ready";
      state.catalogueError = "";
      state.catalogueWarnings = Array.isArray(response?.errors) ? response.errors.filter(Boolean).map(String) : [];
      if (!assemblyTemplateById(templates, state.selectedId)) state.selectedId = templates.find(supportsExampleScene)?.id ?? "";
      if (view.activeAreaId === "assemblies") ops?.renderProject?.(context, view);
    })
    .catch((error) => {
      state.catalogueStatus = "error";
      state.catalogueError = error?.message ?? String(error);
      if (view.activeAreaId === "assemblies") ops?.renderProject?.(context, view);
    })
    .finally(() => { if (state.cataloguePromise === request) state.cataloguePromise = null; });
  state.cataloguePromise = request;
  return request;
}

export function renderAssemblyLibraryLeftPane(_context, view) {
  const state = assemblyLibraryState(view);
  const templates = assemblyTemplates(view);
  const compatibleTemplates = templates.filter(supportsExampleScene);
  const search = state.search.trim().toLocaleLowerCase("zh-CN");
  const visible = compatibleTemplates.filter((item) => !search || assemblySearchText(item).includes(search));
  const shapeGroups = assemblyShapeOrder(compatibleTemplates.filter((item) => !supportsStockOperations(item)));
  if (compatibleTemplates.some(supportsStockOperations)) shapeGroups.push(["fold-processing", "折弯加工"]);
  const groups = shapeGroups.map(([id, label]) => {
    if (id === "straight") label = "共线形";
    const items = visible.filter((item) => (supportsStockOperations(item) ? "fold-processing" : assemblyPresentationShape(item)) === id);
    if (!items.length) return "";
    const collapsed = state.collapsed.includes(id);
    return `<section class="tube-connection-library-group"><button type="button" class="tube-connection-library-group-heading" data-cam-action="tube-designer-assembly-toggle-shape" data-tube-assembly-shape="${attr(id)}" aria-expanded="${!collapsed}"><span>${collapsed ? "▸" : "▾"} ${text(label)}</span><small>${items.length}</small></button><div class="tube-connection-library-cards"${collapsed ? " hidden" : ""}>${items.map((item) => assemblyCard(item, state.selectedId, assemblyParameterValues(view, item))).join("")}</div></section>`;
  }).join("");
  const body = state.catalogueStatus === "loading" && !templates.length
    ? `<div class="tube-profile-library-empty"><strong>正在读取装配模板</strong><span>正在解析逻辑零件与下料归并方案。</span></div>`
    : state.catalogueError && !templates.length
      ? `<div class="tube-profile-library-empty"><strong>装配模板读取失败</strong><span>${text(state.catalogueError)}</span><button type="button" data-cam-action="tube-designer-assembly-retry">重新读取</button></div>`
      : groups || `<div class="tube-profile-library-empty"><strong>没有匹配的装配模板</strong><span>换一个关键词试试</span></div>`;
  const warnings = state.catalogueWarnings?.length ? `<details class="tube-assembly-catalogue-warnings"><summary>${state.catalogueWarnings.length} 个模板未载入</summary><ul>${state.catalogueWarnings.map((warning) => `<li>${text(warning)}</li>`).join("")}</ul></details>` : "";
  return `<section class="tube-profile-library-panel tube-connection-library-panel"><header class="tube-profile-library-heading"><div><strong>装配工艺</strong><span>${compatibleTemplates.length} 个模板</span></div><button type="button" class="tube-assembly-refresh" data-cam-action="tube-designer-assembly-retry" aria-label="刷新装配工艺模板" title="刷新模板">刷新</button></header>${warnings}<label class="tube-connection-library-search"><span class="sr-only">搜索装配工艺</span><input type="search" value="${attr(state.search)}" placeholder="搜索工艺" data-cam-change-action="tube-designer-assembly-search"></label><div class="tube-connection-library-list">${body}</div></section>`;
}

const assemblyMemberBindings = new WeakMap();
const assemblyMemberBoundMounts = new WeakSet();

export function renderAssemblyLibraryRightPane(context, view) {
  if (context?.mount) assemblyMemberBindings.set(context.mount, { context, view });
  const template = selectedAssemblyTemplate(view);
  if (!template) return `<section class="tube-connection-library-editor" data-window-state-controls="[data-finished-parameter],[data-finished-profile-parameter],select[data-cam-change-action='tube-designer-finished-shape-change'],select[data-cam-change-action='tube-designer-finished-profile-change'],input[data-cam-change-action='tube-designer-finished-length-change']"><header class="tube-connection-library-editor-heading"><strong>成品</strong></header><div class="tube-connection-library-editor-body">${renderFinishedProductEditor(view)}<p>暂无装配工艺</p></div></section>`;
  const values = assemblyParameterValues(view, template);
  const state = assemblyLibraryState(view);
  const processDefinitions = template.parameters.filter((item) => item.scope !== "scene" && item.scope !== "product");
  const basic = processDefinitions.filter((item) => item.level !== "advanced" && parameterVisible(item, values));
  const advanced = processDefinitions.filter((item) => item.level === "advanced" && parameterVisible(item, values));
  const effects = currentAssemblyWorkflow(view, template)?.parameterEffects;
  const processParameters = renderPartProcesses(view, template, values);
  const basicParameters = !basic.length
    ? '<section class="tube-connection-library-parameter-section basic"><header><strong>工艺参数</strong><small>0 项</small></header></section>'
    : parameterSection("工艺参数", basic, values, template.id, false, effects);
  let parameters = `${basicParameters}${parameterSection("更多参数", advanced, values, template.id, true, effects, processParameters)}`;
  const stockMode = state.processInputMode === "stock-operation" && supportsStockOperations(template);
  if (stockMode && !selectedStockOperation(view, template)) parameters = '<p class="tube-assembly-product-angle">当前母材没有加工。添加折弯位置后设置加工参数。</p>';
  if (stockMode) ensureStockProcessStorage(context, view, template);
  const inputModeSwitch = supportsStockOperations(template)
    ? `<div class="tube-assembly-work-mode" role="group" aria-label="工艺输入"><button type="button" data-cam-action="tube-designer-assembly-input-mode" data-process-input-mode="local-parts" aria-pressed="${!stockMode}">成品连接</button><button type="button" data-cam-action="tube-designer-assembly-input-mode" data-process-input-mode="stock-operation" aria-pressed="${stockMode}">连续母材</button></div>` : "";
  const sceneParameters = stockMode ? `${renderStockProcessStorage(view, template)}${renderStockProcessEditor(view, template, assemblyTemplates(view))}`
    : template.exampleInput ? `${renderFinishedProductEditor(view)}${renderLocalProcessPlacement(view, template)}`
    : renderAssemblySceneParameters(view, template, values, effects);
  const interfaceLabel = String(template.interfaceType ?? "").split(/[/、]/, 1)[0].trim();
  const integrated = template.manufacturingPlan?.realization === "integrated";
  const summaryLead = stockMode ? `连续母材 · ${stockProcessState(view, template).instances.length} 处折弯`
    : supportsStockOperations(template) ? "连续母材 · 多处折弯" : integrated
    ? `${interfaceLabel} · ${template.participants.length} 段 → ${template.manufacturingPlan.blankParts.length} 件下料`
    : interfaceLabel;
  const summaryLine = summaryLead ? `<span class="tube-assembly-template-summary" title="${attr(template.summary)}">${text(summaryLead)}</span>` : "";
  const applicability = !stockMode && state.selectionProblem
    ? `<p class="tube-assembly-workflow-block" role="status">${text(state.selectionProblem)}</p>` : "";
  return `<section class="tube-connection-library-editor" data-assembly-parameter-scope data-parameter-diagram-owner="assembly-library:${attr(template.id)}"
    data-window-state-controls="[data-assembly-parameter-key],[data-profile-parameter-key],[data-tube-part-process-parameter],[data-process-anchor-key],[data-stock-operation-parameter],select[data-stock-instance],select[data-cam-change-action='tube-designer-finished-shape-change'],select[data-cam-change-action='tube-designer-finished-profile-change'],input[data-cam-change-action='tube-designer-finished-length-change'],select[data-cam-change-action='tube-designer-assembly-scene-profile-change'],input[data-cam-change-action='tube-designer-assembly-scene-length-change'],select[data-cam-change-action='tube-designer-stock-profile'],input[data-cam-change-action='tube-designer-stock-length']"><header class="tube-connection-library-editor-heading"><div><strong>${text(template.displayName)}</strong>${summaryLine}${inputModeSwitch}</div><div class="tube-connection-library-heading-actions"><button type="button" data-cam-action="tube-designer-assembly-toggle-diagram" aria-expanded="${state.showDiagram}">示意图</button>${!stockMode && template.exampleInput ? '<button type="button" data-cam-action="tube-designer-assembly-load-example">载入示例成品</button>' : ""}<button type="button" data-cam-action="tube-designer-assembly-reset" data-tube-assembly-id="${attr(template.id)}">重置工艺</button></div></header><div class="tube-connection-library-editor-body">${sceneParameters}${applicability}${parameters}${renderAssemblyWorkflowWarning(view, template)}</div></section>`;
}

function renderAssemblyPreviewProgress(progress, status, title = "正在生成装配三维预览", background = false) {
  if (!progress) return "";
  const total = Math.max(0, Number(progress.total) || 0);
  const completed = Math.min(total, Math.max(0, Number(progress.completed) || 0));
  const description = total ? `已完成 ${completed}/${total} 件；${status}` : status;
  const value = total ? ` aria-valuemin="0" aria-valuemax="${total}" aria-valuenow="${completed}"` : "";
  const width = total ? `${Math.round(completed / total * 100)}%` : "36%";
  return `<div class="tube-assembly-preview-wait${background ? " is-background" : ""}" data-tube-assembly-preview-progress role="status" aria-live="polite">
    <div class="tube-designer-export-progress-card">
      <span class="tube-designer-export-spinner" aria-hidden="true"></span>
      <strong>${text(title)}</strong>
      <span>${text(status)}</span>
      <div class="tube-designer-export-progress-track${total ? "" : " is-indeterminate"}" role="progressbar" aria-label="装配预览进度" aria-valuetext="${attr(description)}"${value}><i style="width:${width}"></i></div>
      <small>${total ? `${completed} / ${total}` : "准备中"}</small>
      ${background ? "" : "<em>完成或失败后，界面会自动恢复。</em>"}
    </div>
  </div>`;
}

export function renderAssemblyLibraryViewportOverlay(context, view) {
  const template = selectedAssemblyTemplate(view);
  if (!template) return "";
  const values = assemblyParameterValues(view, template);
  const state = assemblyLibraryState(view);
  ensureAssemblyLibraryPreview(context, view, template);
  const pending = state.selectionRequest ?? state.previewRequest;
  const progress = pending?.progress;
  const status = pending ? progress?.phase || "正在准备工艺方案…" : "";
  const hud = state.previewError ? `<div class="tube-connection-library-hud error" aria-live="polite"><i aria-hidden="true"></i><span>${text(state.previewError)}</span><button type="button" data-cam-action="tube-designer-assembly-retry-preview">重新生成</button></div>` : "";
  const background = !!state.previewRequest && !!state.preview?.finishedShape && !state.exploded;
  const waiting = status ? renderAssemblyPreviewProgress(progress ?? {}, status,
    background ? "正在生成下料件" : "正在生成装配三维预览", background) : "";
  return `${renderAssemblyLibraryDiagramDock(view, template, values)}${renderAssemblyViewportStage(template, state, view)}${hud}${waiting}`;
}

function hasIntegratedFormedPreview(preview) {
  return preview?.formedPreview?.rows?.length === 1
    && preview.formedPreview.resources instanceof Map
    && preview.formedPreview.resources.has(preview.formedPreview.rows[0]?.data?.geometry?.url);
}

function hasFinishedShapePreview(preview) {
  return preview?.finishedShape?.rows?.length === 1
    && preview.finishedShape.resources instanceof Map
    && preview.finishedShape.resources.has(preview.finishedShape.rows[0]?.data?.geometry?.url);
}

function renderAssemblyViewportStage(template, state, view = null) {
  const stockMode = state.processInputMode === "stock-operation";
  if (stockMode) {
    const available = !!state.preview?.designParts?.length, calibration = available && !state.exploded;
    const description = calibration ? "按各折弯位置和方向展示名义目标形态；曲线采用分段近似，实际成形需校核。"
      : "同一根母材上的全部加工；每处折弯参数分别保存。";
    return `<div class="tube-assembly-preview" data-tube-assembly-preview><section class="tube-assembly-preview-pane scene" data-tube-assembly-preview-pane="scene" aria-label="连续母材：${description}"><header title="${description}"><div class="tube-assembly-preview-toolbar"><div class="tube-assembly-view-switch" role="group" aria-label="连续母材视图"><button type="button" class="tube-assembly-view-mode${calibration ? "" : " active"}" data-cam-action="tube-designer-assembly-set-view" data-tube-assembly-view="exploded" aria-pressed="${!calibration}">下料件</button><button type="button" class="tube-assembly-view-mode${calibration ? " active" : ""}" data-cam-action="tube-designer-assembly-set-view" data-tube-assembly-view="finished" aria-pressed="${calibration}"${available ? "" : ' disabled title="正在准备成形校核"'}>成形校核</button></div><small>${calibration ? "名义目标形态 · 曲线分段近似" : "连续母材加工"}</small></div></header><div class="tube-assembly-preview-host" data-tube-assembly-scene-viewport></div></section></div>`;
  }
  const independentExample = !!template.exampleInput;
  const integrated = template.manufacturingPlan?.realization === "integrated";
  const integratedExample = integrated;
  const logicalL = state.preview?.plan?.templateId === template.id
    && state.preview?.layoutShape === "l" && state.preview?.designParts?.length === 2;
  const formedAvailable = integratedExample && (independentExample
    || state.preview?.plan?.templateId === template.id && (logicalL || hasIntegratedFormedPreview(state.preview)));
  const targetShape = formedAvailable && state.preview?.formedPreview?.mesh?.metadata?.previewKind === "target-shape";
  const targetDescription = state.preview?.formedPreview?.mesh?.metadata?.formingValidation === "calibration-required"
    ? "按模板参数生成的目标形状；实际成形需试样标定"
    : "按模板参数生成的目标形状；实际成形尚未验证";
  const exploded = integratedExample && !formedAvailable || !!state.exploded;
  const blankPending = independentExample ? state.previewRequest?.kind === "manufacturing"
    : !!state.previewRequest && !!state.preview?.finishedShape && !state.preview.manufacturingParts?.length;
  const sceneDescription = independentExample && !exploded ? "成品外形与尺寸"
    : integratedExample ? formedAvailable && !exploded
      ? logicalL ? "L 形成品外形示意" : targetShape ? targetDescription
        : "一体成形外形示意（单根连续管；尚未生成成形实体）"
      : formedAvailable ? "按模板加工的下料件（成形前）"
        : "连续母材的下料形状；成形外观暂不可用"
    : exploded ? integrated
    ? "下料件示例：查看连续母材与加工形状"
    : "下料件示例：查看各件加工形状"
    : logicalL ? "L 形成品外形示意" : "连接示例：查看管件位置与接口；未显示辅料";
  const sceneTitle = independentExample && !exploded ? finishedProductShape(finishedProductInput(view).shapeId, view)?.displayName
      ?? "成品" : template.displayName;
  return `<div class="tube-assembly-preview" data-tube-assembly-preview><section class="tube-assembly-preview-pane scene" data-tube-assembly-preview-pane="scene" aria-label="${attr(`${sceneTitle}：${sceneDescription}`)}">
    <header title="${attr(sceneDescription)}"><div class="tube-assembly-preview-toolbar">
      ${integratedExample && !formedAvailable ? `<span class="tube-assembly-view-only-blank" title="${attr(state.preview?.formedPreviewError || "模板尚未提供目标成形预览")}">成形示意未生成 · 当前为下料件</span>` : `<div class="tube-assembly-view-switch" role="group" aria-label="装配视图">
        <button type="button" class="tube-assembly-view-mode${exploded ? "" : " active"}" data-cam-action="tube-designer-assembly-set-view" data-tube-assembly-view="finished" aria-pressed="${!exploded}"${integratedExample ? ` title="${independentExample || logicalL ? "仅显示成品外形" : targetShape ? targetDescription : "仅供核对外观；尚未生成成形实体"}"` : ""}>${independentExample || logicalL ? "成品示意" : integrated ? targetShape ? "目标示意" : "成形示意" : "连接示例"}</button>
        <button type="button" class="tube-assembly-view-mode${exploded ? " active" : ""}" data-cam-action="tube-designer-assembly-set-view" data-tube-assembly-view="exploded" aria-pressed="${exploded}"${blankPending ? ' disabled title="正在生成下料件"' : ""}>下料件</button>
      </div>`}</div>
    </header>
    <div class="tube-assembly-preview-host" data-tube-assembly-scene-viewport></div>
  </section></div>`;
}

function renderAssemblyLibraryDiagramDock(view, template, values) {
  const state = assemblyLibraryState(view);
  if (!state.showDiagram) return "";
  const mode = state.diagramMode === "process" ? "process" : "product";
  const product = mode === "product" && template.exampleInput ? finishedProductInput(view) : null;
  const shape = product ? finishedProductShape(product.shapeId, view) : null;
  const diagramTemplate = shape ?? template;
  const diagramOwner = shape ? `finished-product:${shape.id}` : `assembly-library:${template.id}`;
  if (!diagramTemplate?.parameterDiagram) return "";
  if (product) values = product.parameters;
  const definitions = (shape?.parameters ?? template.parameters).filter((item) =>
    (shape || (mode === "product" ? item.scope === "scene" || item.scope === "product"
      : item.scope !== "scene" && item.scope !== "product")) && parameterVisible(item, values));
  return `<aside class="tube-library-diagram-dock" data-tube-assembly-diagram-dock data-library-floating-diagram="assemblies" style="${libraryDiagramPositionStyle(view, "assemblies")}">
    ${renderDiagramResizeHandles()}
    <header class="tube-library-diagram-drag" data-floating-diagram-drag><strong>${text(diagramTemplate.displayName)} · ${mode === "product" ? "成品参数" : "工艺参数"}</strong><button type="button" data-library-diagram-close data-cam-action="tube-designer-assembly-close-diagram" aria-label="关闭示意图">×</button></header>
    <div class="tube-assembly-diagram-mode" role="group" aria-label="参数示意图类型">${[["product", "成品参数"], ["process", "工艺参数"]].map(([id, label]) => `<button type="button" data-cam-action="tube-designer-assembly-diagram-mode" data-tube-assembly-diagram-mode="${id}" aria-pressed="${mode === id}" class="${mode === id ? "active" : ""}">${label}</button>`).join("")}</div>
    <div class="tube-library-diagram-content" data-parameter-diagram-for="${attr(diagramOwner)}">${renderToolParameterDiagram({ tool: diagramTemplate, values, definitions, expanded: true, showToggle: false, annotationKind: "assembly",
      guidance: mode === "product" ? "成品外形与尺寸" : "单件加工位置与参数" })}</div>
  </aside>`;
}

export function bindAssemblyParameterDiagrams(mount) {
  bindParameterDiagramScopes(mount, "assembly");
  const finishedBinding = assemblyMemberBindings.get(mount);
  if (finishedBinding) bindFinishedProductEditor(mount, finishedBinding.context, finishedBinding.view);
  if (!mount?.addEventListener) return;
  if (!assemblyMemberBoundMounts.has(mount)) {
    assemblyMemberBoundMounts.add(mount);
    mount.addEventListener("toggle", (event) => {
      const card = event.target;
      if (!card?.matches?.("details.tube-assembly-scene-member[data-assembly-scene-role]")) return;
      const binding = assemblyMemberBindings.get(mount);
      if (!binding) return;
      const { context, view } = binding;
      const template = selectedAssemblyTemplate(view);
      if (!template || card.dataset.tubeAssemblyId !== template.id) return;
      const role = card.dataset.assemblySceneRole;
      const disclosure = assemblyLibraryState(view).sceneMemberDisclosure[template.id] ??= {};
      disclosure[role] = card.open;
      if (card.open) ensureAssemblySceneProfileDiagram(context, view, template, role);
    }, true);
  }
  const binding = assemblyMemberBindings.get(mount);
  if (binding) for (const card of mount.querySelectorAll("details.tube-assembly-scene-member[open]")) {
    const template = selectedAssemblyTemplate(binding.view);
    if (template?.id === card.dataset.tubeAssemblyId)
      ensureAssemblySceneProfileDiagram(binding.context, binding.view, template, card.dataset.assemblySceneRole);
  }
}

export async function handleAssemblyLibraryAction(context, view, action, target, ops) {
  const state = assemblyLibraryState(view);
  if (action === "tube-designer-assembly-local-anchor-change"
      && state.processInputMode === "local-parts") {
    const template = selectedAssemblyTemplate(view), role = target.dataset.processRole, key = target.dataset.processAnchorKey;
    const part = state.localProcessInputs[template.id]?.parts?.[role], anchor = part?.anchor;
    const allowed = anchor?.kind === "end" ? ["end", "trim", "rotation"] : ["face", "station", "offset", "rotation"];
    if (anchor && allowed.includes(key)) {
      const value = ["end", "face"].includes(key) ? String(target.value) : Number(target.value);
      if (typeof value === "string" || Number.isFinite(value)) {
        const drafts = state.localAnchorDrafts[template.id] ??= {};
        drafts[role] = { ...(drafts[role] ?? {}), [key]: value };
        state.selectionValidatedKey = ""; state.selectionFailureKey = "";
        invalidateAssemblyPreview(view, false); ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (state.processInputMode === "stock-operation"
      && ["tube-designer-stock-plan-save", "tube-designer-stock-plan-load", "tube-designer-stock-plan-new", "tube-designer-stock-plan-reload"].includes(action)) {
    await handleStockProcessStorageAction(context, view, action, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-input-mode") {
    const template = selectedAssemblyTemplate(view), mode = target?.dataset?.processInputMode;
    if (mode === "local-parts" || mode === "stock-operation" && supportsStockOperations(template)) {
      state.processInputMode = mode;
      if (mode === "stock-operation") { stockProcessState(view, template); state.exploded = true; }
      else state.exploded = false;
      state.selectionProblem = ""; state.selectionRequest = null;
      invalidateAssemblyPreview(view, false);
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (state.processInputMode === "stock-operation") {
    const result = handleStockProcessAction(view, selectedAssemblyTemplate(view), assemblyTemplates(view), action, target);
    if (result.handled) {
      if (result.changed) {
        if (result.selectedTemplateId) state.selectedId = result.selectedTemplateId;
        invalidateAssemblyPreview(view, false);
        ops.renderProject(context, view);
      }
      return { handled: true };
    }
  }
  {
    const result = handleFinishedProductAction(view, action, target);
    if (result.handled) {
      if (result.changed) {
        state.exploded = false;
        state.selectionValidatedKey = "";
        state.selectionProblem = "";
        invalidateAssemblyPreview(view, false);
        ops.renderProject(context, view);
      }
      return { handled: true };
    }
  }
  if (action === "tube-designer-assembly-load-example") {
    const template = selectedAssemblyTemplate(view), owner = context.sceneProxy;
    const productKey = finishedProductKey(finishedProductInput(view));
    const example = await owner.invoke("TubeDesigner.GetAssemblyTemplateExampleProduct", {
      templateId: template.id, parameters: assemblyPreviewParameterGroups(view, template).parameters,
    }, { timeoutMs: 120000 });
    if (view.activeAreaId !== "assemblies" || state.selectedId !== template.id
        || context.sceneProxy !== owner || finishedProductKey(finishedProductInput(view)) !== productKey) return { handled: true };
    const sample = example?.finishedProduct;
    if (example?.schema !== "icax.assembly-example-product" || example.templateId !== template.id
        || sample?.schema !== "icax.finished-product" || !sample.parameters || !sample.spans
        || !finishedProductShape(sample.shapeId, view)) throw new Error("模板未返回有效的示例成品");
    const finished = finishedProductState(view);
    finished.selectedShapeId = sample.shapeId;
    finished.drafts[sample.shapeId] = clone(sample);
    state.selectionValidatedKey = "";
    state.selectionProblem = "";
    state.exploded = false;
    invalidateAssemblyPreview(view, false);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-retry") {
    state.selectionRequest = null;
    state.selectionFailureKey = "";
    state.selectionValidatedKey = "";
    state.catalogueStatus = "idle";
    state.catalogueError = "";
    state.preview = null;
    state.previewRequest = null;
    state.previewFailureKey = "";
    state.appliedKey = "";
    clearAssemblyLibraryViewports(view);
    await ensureAssemblyLibraryCatalogue(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-search") {
    state.search = String(target?.value ?? "");
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-toggle-shape" || action === "tube-designer-assembly-toggle-category") {
    const id = String(target?.dataset?.tubeAssemblyShape ?? target?.dataset?.tubeAssemblyCategory ?? "");
    if (!id) return { handled: true };
    state.collapsed = state.collapsed.includes(id) ? state.collapsed.filter((item) => item !== id) : [...state.collapsed, id];
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-toggle-diagram" || action === "tube-designer-assembly-close-diagram") {
    state.showDiagram = action !== "tube-designer-assembly-close-diagram" && !state.showDiagram;
    if (action === "tube-designer-assembly-close-diagram") state.showDiagram = false;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-diagram-mode") {
    const mode = String(target?.dataset?.tubeAssemblyDiagramMode ?? "");
    if (mode === "product" || mode === "process") {
      state.diagramMode = mode;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-assembly-camera") {
    const camera = String(target?.dataset?.tubeAssemblyCamera ?? "");
    controlAssemblyLibraryCamera(view, camera);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-set-view") {
    if (!selectedAssemblyTemplate(view)?.exampleInput
        && selectedAssemblyTemplate(view)?.manufacturingPlan?.realization === "integrated"
        && !hasIntegratedFormedPreview(state.preview)
        && !(state.preview?.layoutShape === "l" && state.preview?.designParts?.length === 2))
      return { handled: true };
    const exploded = String(target?.dataset?.tubeAssemblyView ?? "finished") === "exploded";
    if (exploded && !selectedAssemblyTemplate(view)?.exampleInput && state.previewRequest && state.preview?.finishedShape
        && !state.preview.manufacturingParts?.length) return { handled: true };
    if (state.exploded !== exploded) {
      state.exploded = exploded;
      if (state.processInputMode !== "stock-operation" && selectedAssemblyTemplate(view)?.exampleInput) {
        // Keep the shared product, but stop scheduling work for the other view.
        state.previewRequest = null;
        state.previewError = "";
        state.previewFailureKey = "";
      }
      state.appliedKey = "";
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-assembly-retry-preview") {
    state.selectionFailureKey = "";
    finishedProductState(view).processInputFailures?.clear();
    state.previewFailureKey = "";
    state.previewError = "";
    state.preview = null;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-select") {
    const id = String(target?.dataset?.tubeAssemblyId ?? "");
    const chosen = assemblyTemplateById(assemblyTemplates(view), id);
    if (state.processInputMode === "stock-operation" && chosen) {
      if (supportsStockOperations(chosen)) {
        const row = selectedStockOperation(view, selectedAssemblyTemplate(view));
        if (row) {
          row.templateId = chosen.id;
          row.settings[chosen.id] ??= { parameters: Object.fromEntries(chosen.parameters.filter((p) => p.scope !== "scene")
            .map((p) => [p.key, clone(p.defaultValue)])), processDrafts: {} };
        }
        state.selectedId = chosen.id;
        markStockProcessEdited(view, chosen);
        invalidateAssemblyPreview(view, false); ops.renderProject(context, view);
        return { handled: true };
      }
      state.processInputMode = "local-parts";
    }
    if (chosen && !supportsExampleScene(chosen)) return { handled: true };
    if (chosen?.exampleInput) {
      if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) {
        state.selectedId = id;
        state.selectionRequest = null;
        state.selectionProblem = "";
        state.selectionValidatedKey = "";
        state.exploded = false;
        invalidateAssemblyPreview(view, false);
        ops.renderProject(context, view);
        return { handled: true };
      }
      const selectionKey = assemblyApplicabilityKey(view, chosen);
      if (id !== state.selectedId || state.selectionValidatedKey !== selectionKey
          || state.selectionOwner !== context.sceneProxy || state.selectionRequest) {
        state.previewRequest = null;
        state.exploded = false;
        state.previewError = "";
        state.selectionFailureKey = "";
        const request = ensureAssemblyExampleSelection(context, view, chosen, ops, true);
        ops.renderProject(context, view);
        await request?.promise;
      }
      return { handled: true };
    }
    if (id !== state.selectedId && chosen) {
      preserveAssemblySceneOnTemplateChange(view, selectedAssemblyTemplate(view), chosen);
      state.selectedId = id;
      state.exploded = false;
      invalidateAssemblyPreview(view, !chosen.exampleInput);
    }
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-scene-profile-change"
      || action === "tube-designer-assembly-scene-length-change"
      || action === "tube-designer-assembly-scene-profile-parameter-change") {
    const template = selectedAssemblyTemplate(view);
    if (template?.exampleInput) return { handled: true };
    const role = String(target?.dataset?.tubeAssemblySceneRole ?? "");
    if (template?.id !== String(target?.dataset?.tubeAssemblyId ?? "")
        || !template.previewScene?.designParts?.some((part) => part.role === role)) return { handled: true };
    const current = assemblySceneParts(view, template)[role];
    let change = null;
    if (action === "tube-designer-assembly-scene-profile-change") {
      const selected = libraryProfiles(view).find((item) => profileSelectionKey(item) === String(target?.value ?? ""));
      if (selected) change = {
        profileRef: profileRef(selected),
        parameters: { ...(selected.defaultParameters ?? Object.fromEntries((selected.descriptor?.parameters ?? [])
          .map((definition) => [definition.key, definition.defaultValue]))) },
      };
    } else if (action === "tube-designer-assembly-scene-length-change") {
      const length = Number(target?.value);
      if (Number.isFinite(length) && length >= 1 && length <= 100000) change = { length };
    } else {
      const profile = libraryProfiles(view).find((item) => profileScope(item) === current.profileRef.scope
        && String(item.id) === String(current.profileRef.id));
      const key = String(target?.dataset?.tubeAssemblySceneProfileParameter ?? "");
      const definition = profile?.descriptor?.parameters?.find((item) => item.key === key);
      if (definition && parameterVisible(definition, current.parameters) && parameterEnabled(definition, current.parameters)) {
        const choices = availableParameterChoices(definition, current.parameters);
        const option = choices.find((item) => String(typeof item === "object" ? item.value : item) === String(target?.value));
        const hasChoices = Array.isArray(definition.options) || Array.isArray(definition.choices);
        const value = definition.valueType === "boolean" ? !!target?.checked
          : hasChoices ? typeof option === "object" ? option?.value : option
          : definition.valueType === "string" ? String(target?.value ?? "") : Number(target?.value);
        const numeric = definition.valueType === "number" || definition.valueType === "integer";
        if (value !== undefined && (!hasChoices || option !== undefined)
            && (!numeric || Number.isFinite(value)
            && (definition.valueType !== "integer" || Number.isInteger(value))
            && (definition.min == null || value >= definition.min)
            && (definition.max == null || value <= definition.max))) {
          change = { parameters: { [key]: value } };
        }
      }
    }
    if (change && updateAssemblyScenePart(view, template, role, change)) {
      invalidateAssemblyPreview(view, false);
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-assembly-parameter-change") {
    const template = assemblyTemplateById(assemblyTemplates(view), String(target?.dataset?.tubeAssemblyId ?? ""));
    const key = String(target?.dataset?.tubeAssemblyParameter ?? "");
    const definition = template?.parameters.find((item) => item.key === key);
    if (template?.exampleInput && definition?.scope === "scene") return { handled: true };
    if (template && definition) {
      const value = definition.valueType === "boolean" ? !!target.checked
        : definition.valueType === "choice" ? String(target.value ?? "") : Number(target.value);
      if (!['number', 'integer'].includes(definition.valueType) || Number.isFinite(value)) {
        if (definition.scope === "scene") {
          if (assemblySceneParameterAccepts(definition, value, assemblyParameterValues(view, template))) {
            const signature = assemblySceneSignature(template);
            state.sceneParameterDrafts[signature] = {
              ...(state.sceneParameterDrafts[signature] ?? {}), [definition.sceneKey || key]: value,
            };
            if (state.parameterDrafts[template.id]) delete state.parameterDrafts[template.id][key];
            invalidateAssemblyPreview(view, false);
            ops.renderProject(context, view);
          }
        } else {
          if (state.processInputMode === "stock-operation") {
            stockOperationSettings(view, template).parameters[key] = value;
            markStockProcessEdited(view, template);
          }
          else state.parameterDrafts[template.id] = { ...(state.parameterDrafts[template.id] ?? {}), [key]: value };
          invalidateAssemblyPreview(view);
          ops.renderProject(context, view);
        }
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-assembly-process-parameter-change") {
    const templateId = String(target?.dataset?.tubeAssemblyId ?? "");
    const processId = String(target?.dataset?.tubeAssemblyProcess ?? "");
    const resourceId = String(target?.dataset?.tubePartProcess ?? "");
    const key = String(target?.dataset?.tubePartProcessParameter ?? "");
    const template = assemblyTemplateById(assemblyTemplates(view), templateId);
    const process = template?.partProcesses.find((item) => item.id === processId);
    const descriptor = selectedProcessDescriptor(process, assemblyParameterValues(view, template), resourceId);
    const definitions = [...(descriptor?.parameters ?? []), ...(descriptor?.operationParameters ?? [])];
    const definition = definitions.find((item) => item.key === key);
    if (template && process && definition) {
      const value = definition.valueType === "boolean" ? !!target.checked
        : Array.isArray(definition.options) ? String(target.value ?? "") : Number(target.value);
      if (definition.valueType !== "number" && definition.valueType !== "integer" || Number.isFinite(value)) {
        const templateDrafts = state.processInputMode === "stock-operation"
          ? stockOperationSettings(view, template).processDrafts : state.processDrafts[templateId] ??= {};
        const processDrafts = templateDrafts[processId] ??= {};
        const current = processParameterValues(view, template, process, descriptor);
        const sources = assemblyProcessAutoFillSources(view, template, process);
        processDrafts[descriptor.id] = {
          ...(processDrafts[descriptor.id] ?? {}),
          ...parameterAutoFillPatch(definitions, current, key, value, sources),
        };
        if (state.processInputMode === "stock-operation") markStockProcessEdited(view, template);
        invalidateAssemblyPreview(view);
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-assembly-reset") {
    const id = String(target?.dataset?.tubeAssemblyId ?? state.selectedId);
    if (state.processInputMode === "stock-operation") {
      const template = selectedAssemblyTemplate(view), row = selectedStockOperation(view, template);
      delete row.settings[id]; stockOperationSettings(view, template);
      markStockProcessEdited(view, template);
      invalidateAssemblyPreview(view, false); ops.renderProject(context, view);
      return { handled: true };
    }
    delete state.parameterDrafts[id];
    delete state.processDrafts[id];
    delete state.localAnchorDrafts[id];
    if (!selectedAssemblyTemplate(view)?.exampleInput) {
      const template = assemblyTemplateById(assemblyTemplates(view), id);
      if (template) {
        const signature = assemblySceneSignature(template);
        delete state.sceneDrafts[signature];
        delete state.sceneParameterDrafts[signature];
      }
    }
    invalidateAssemblyPreview(view);
    ops.renderProject(context, view);
    return { handled: true };
  }
  return { handled: false };
}

function invalidateAssemblyPreview(view, clearScene = false) {
  const state = assemblyLibraryState(view);
  state.selectionRequest = null;
  state.selectionFailureKey = "";
  state.previewRequest = null;
  state.preview = null;
  state.previewFailureKey = "";
  state.previewError = "";
  if (clearScene) {
    state.appliedKey = "";
    clearAssemblyLibraryViewports(view);
  }
}

export function assemblyPreviewKey(view, template = selectedAssemblyTemplate(view)) {
  if (!template) return "";
  const state = assemblyLibraryState(view);
  if (state.processInputMode === "stock-operation")
    return finishedProductKey(["stock-operation", stockProcessPayload(view, template, assemblyTemplates(view)),
      assemblyTemplates(view).filter(supportsStockOperations).map((item) => assemblyManufacturingDependencies(view, item))]);
  const parameterGroups = assemblyPreviewParameterGroups(view, template);
  return finishedProductKey([
    assemblyManufacturingDependencies(view, template),
    parameterGroups.parameters,
    parameterGroups.sceneParameters,
    state.processDrafts?.[template.id] ?? {},
    state.localAnchorDrafts?.[template.id] ?? {},
    template.exampleInput ? finishedProductGeometryKey(view, finishedProductInput(view))
      : assemblySceneDraftParts(view, template),
  ]);
}

function assemblyManufacturingDependencies(view, template) {
  const ids = new Set(template.partProcesses?.flatMap((process) =>
    [process.resource?.id, ...(process.resourceSelection?.resources ?? []).map((resource) => resource.id)]).filter(Boolean));
  const tools = [...(view.tubeDesignerSystemPunchTools ?? []), ...(view.tubeDesignerTemplatePunchTools ?? []),
    ...(view.tubeDesignerUserData?.punchTools ?? []), ...(view.tubeDesignerPunchWizard?.tools ?? [])]
    .filter((tool) => ids.has(tool.id));
  // generationDigest covers scripts and declared mold files. The complete
  // descriptor also invalidates when a caller updates declarations in place.
  return [template, tools];
}

function finishedProductGeometryKey(view, product) {
  return finishedProductKey([finishedProductShape(product.shapeId, view), product,
    finishedProductProfileRevisions(view, product)]);
}

function finishedProductProfileRevisions(view, product) {
  const profiles = libraryProfiles(view);
  return Object.keys(product.spans).sort().map((id) => {
    const ref = product.spans[id].profileRef;
    const profile = profiles.find((item) => profileScope(item) === ref?.scope && String(item.id) === String(ref?.id));
    return [profile?.packageDigest ?? "", profile?.revision ?? "", profile?.descriptor?.version ?? profile?.version ?? ""];
  });
}

function clone(value) {
  return typeof structuredClone === "function" ? structuredClone(value) : JSON.parse(JSON.stringify(value));
}

const assemblyPreviewResourceSession = (globalThis.crypto?.randomUUID?.().replaceAll("-", "")
  ?? `${Date.now().toString(36)}${Math.random().toString(36).slice(2)}`).slice(0, 24);
let assemblyPreviewResourceSequence = 0;
const assemblyResourceOwners = new WeakMap();
const assemblyNativeQueues = new WeakMap();
let assemblyResourceOwnerSequence = 0;

function assemblyPreviewScope(context, view = null) {
  const owner = context.sceneProxy;
  if (!assemblyResourceOwners.has(owner)) assemblyResourceOwners.set(owner, ++assemblyResourceOwnerSequence);
  return finishedProductKey([assemblyResourceOwners.get(owner), context.project?.projectId ?? view?.projectId ?? "",
    owner.sceneId ?? view?.sceneId ?? view?.scene?.entityId ?? view?.scene?.id ?? ""]);
}

function touchCache(cache, key, value, limit) {
  cache.delete(key);
  cache.set(key, value);
  while (cache.size > limit) cache.delete(cache.keys().next().value);
  return value;
}

function assemblyPreviewResourceKey(scope, kind, request, dependency = "") {
  // Cache lookup uses full content identity; every actual publication receives
  // a fresh URL, including after a part-cache eviction. An older configuration
  // can retain its response version without a later request overwriting it.
  const identity = finishedProductKey([scope, kind, dependency, request]);
  let signature = 2166136261;
  for (let index = 0; index < identity.length; index++)
    signature = Math.imul(signature ^ identity.charCodeAt(index), 16777619) >>> 0;
  return `${assemblyPreviewResourceSession}-${signature.toString(16)}-${++assemblyPreviewResourceSequence}`;
}

function invokeAssemblyPart(owner, task) {
  const previous = assemblyNativeQueues.get(owner) ?? Promise.resolve();
  const next = previous.then(task, task);
  const tail = next.catch(() => {});
  assemblyNativeQueues.set(owner, tail);
  void tail.then(() => { if (assemblyNativeQueues.get(owner) === tail) assemblyNativeQueues.delete(owner); });
  return next;
}

async function resolveAssemblyRequestSections(context, request, cache, isCurrent = () => true) {
  const resolved = clone(request);
  const sections = [];
  for (const end of Object.values(resolved.ends ?? {})) if (end?.section) sections.push(end.section);
  for (const feature of resolved.features ?? []) if (feature?.section) sections.push(feature.section);
  for (const section of sections) {
    if (!isCurrent()) return null;
    if (section.profile || !section.profileRef) continue;
    const key = JSON.stringify([section.profileRef, section.parameters ?? {}]);
    let profile = cache.get(key);
    if (!profile) {
      const response = await context.sceneProxy.invoke("TubeDesigner.EvaluateProfilePackage", {
        profileRef: section.profileRef,
        parameters: section.parameters ?? {},
      }, { timeoutMs: 120000 });
      if (!isCurrent()) return null;
      profile = response?.profile;
      if (!profile?.contours?.length) throw new Error("装配工艺需要的目标截面没有生成有效轮廓。");
      cache.set(key, profile);
    }
    section.profile = profile;
  }
  return resolved;
}

async function evaluateAssemblyPreviewParts(context, parts, kind, sectionCache, templateId,
  onComplete = null, isCurrent = () => true, options = null) {
  const result = [];
  const owner = context.sceneProxy;
  const scope = options?.scope ?? assemblyPreviewScope(context);
  for (const part of parts) {
    if (!isCurrent()) return null;
    const partKey = finishedProductKey([scope, options?.dependency ?? "", part.request]);
    const cache = options?.parts;
    let cached = cache?.get(partKey);
    if (cached) touchCache(cache, partKey, cached, 64);
    if (!cached) {
      cached = { response: null, promise: null };
      const entry = cached;
      entry.promise = Promise.resolve().then(async () => {
        const request = await resolveAssemblyRequestSections(context, part.request, sectionCache, isCurrent);
        if (!request || !isCurrent()) return null;
        request.previewResourceKey = assemblyPreviewResourceKey(scope, kind, request, options?.dependency);
        const response = await invokeAssemblyPart(owner, () => isCurrent()
          ? owner.invoke(kind === "manufacturing" ? "TubeDesigner.PreviewAssemblyManufacturingPart"
            : "TubeDesigner.PreviewPunchWizard", request, { timeoutMs: 180000 }) : null);
        if (!response) return null;
        if (kind === "manufacturing") {
          const detail = String(response.resultError ?? "").trim();
          if (detail || response.resultValid !== true || response.previewComputed !== true
              || response.solidCount !== 1 || !response.geometry?.url)
            throw new Error(`${part.label}下料形状生成失败${detail ? `：${detail}` : "。"}`);
        } else if (!response.baseGeometry?.url) throw new Error(`${part.label}没有返回原始零件几何。`);
        entry.response = response;
        return response;
      }).then((response) => {
        if (!response && cache?.get(partKey) === entry) cache.delete(partKey);
        return response;
      }).catch((error) => {
        if (cache?.get(partKey) === entry) cache.delete(partKey);
        throw error;
      });
      if (cache) touchCache(cache, partKey, entry, 64);
    }
    const response = cached.response ?? await cached.promise;
    if (!isCurrent()) return null;
    if (!response) return null;
    result.push({ ...part, response });
    onComplete?.(result.length);
  }
  return result;
}

export function assemblyFinishedRows(preview) {
  const designParts = preview?.designParts ?? [];
  return designParts.map((part) => {
    return {
      entityId: `assembly-finished:${part.id}`,
      roles: part.role ? [part.role] : [],
      data: {
        // Product geometry is deliberately blind to the selected assembly
        // process. Tabs, slots, bores and weld preparation belong to blanks.
        geometry: part.response.baseGeometry,
        material: part.response.baseMaterial,
        geometryKind: 1,
        renderClass: 1,
        visible: true,
        selectable: false,
        localToWorldMatrix: part.matrix,
      },
    };
  });
}

function assemblyLogicalLRows(preview) {
  const parts = preview?.designParts ?? [];
  if (parts.length !== 2) return assemblyFinishedRows(preview);
  const angle = Number(preview.plan?.sceneParameters?.angle
    ?? preview.plan?.sceneParameters?.jointAngle ?? 90) * Math.PI / 180;
  const rotation = Number(preview.plan?.sceneParameters?.planeRotation ?? 0) * Math.PI / 180;
  if (!Number.isFinite(angle) || !Number.isFinite(rotation)) return assemblyFinishedRows(preview);
  const sine = Math.sin(angle), cosine = Math.cos(angle);
  const spinSine = Math.sin(rotation), spinCosine = Math.cos(rotation);
  const direction = [cosine, -sine * spinSine, sine * spinCosine];
  const up = [0, spinCosine, spinSine];
  const sideways = [direction[1] * up[2] - direction[2] * up[1],
    direction[2] * up[0] - direction[0] * up[2],
    direction[0] * up[1] - direction[1] * up[0]];
  const matrices = [
    [1, 0, 0, -Number(parts[0].request?.length || 0),
      0, spinCosine, -spinSine, 0,
      0, spinSine, spinCosine, 0,
      0, 0, 0, 1],
    [direction[0], up[0], sideways[0], 0,
      direction[1], up[1], sideways[1], 0,
      direction[2], up[2], sideways[2], 0,
      0, 0, 0, 1],
  ];
  return parts.map((part, index) => ({
    entityId: `assembly-finished:${part.id}`,
    roles: part.role ? [part.role] : [],
    data: {
      geometry: part.response.baseGeometry,
      material: part.response.baseMaterial,
      geometryKind: 1, renderClass: 1, visible: true, selectable: false,
      localToWorldMatrix: matrices[index],
    },
  }));
}

export function assemblyBlankRows(preview) {
  return (preview?.manufacturingParts ?? []).map((part) => ({
    entityId: `assembly-blank:${part.id}`,
    roles: Array.isArray(part.participantRoles) && part.participantRoles.length
      ? part.participantRoles : part.sourceRole ? [part.sourceRole] : [],
    data: {
      geometry: part.response.geometry,
      material: part.response.material,
      geometryKind: 1,
      renderClass: 1,
      visible: true,
      selectable: false,
      localToWorldMatrix: part.explodedMatrix ?? part.matrix,
    },
  }));
}

export function assemblySceneRows(preview, exploded = false) {
  if (preview?.stockProcess) return exploded ? assemblyBlankRows(preview) : assemblyFinishedRows(preview);
  if (!exploded && hasFinishedShapePreview(preview)) return preview.finishedShape.rows;
  if (!exploded && preview?.layoutShape === "l") return assemblyLogicalLRows(preview);
  if (!exploded && preview?.independentFinishedProduct) return assemblyFinishedRows(preview);
  if (!exploded && hasIntegratedFormedPreview(preview)) return preview.formedPreview.rows;
  if (preview?.plan?.resolvedWorkflow?.realization === "integrated") return assemblyBlankRows(preview);
  return exploded ? assemblyBlankRows(preview) : assemblyFinishedRows(preview);
}

const assemblyViewportControllers = new WeakMap();
const stockInputBindings = new WeakMap();

function createAssemblyViewport(state, viewportFactory) {
  const viewport = viewportFactory({
    backgroundColor: 0x13252d,
    continuousRender: false,
    constrainOrbit: false,
    projectionMode: state.projectionMode,
    showProjectionToggle: true,
    pickingEnabled: false,
    blankDoubleClickFitEnabled: true,
    antialias: true,
    pixelRatioCap: 2,
    onProjectionChange(mode) { state.projectionMode = mode; },
  });
  return { viewport, host: null, ready: false, appliedKey: "", templateId: "", mode: "", rows: [] };
}

function mountAssemblyViewportPane(pane, host) {
  if (pane.host === host && pane.viewport.root?.parentElement === host) return;
  if (pane.cubeView) stopViewCubeAnimation(pane.cubeView);
  if (pane.host && pane.cubeClick) pane.host.removeEventListener?.("click", pane.cubeClick, true);
  pane.host = host;
  pane.viewport.mount(host);
  if (typeof window === "undefined" || typeof host.insertAdjacentHTML !== "function") return;
  host.insertAdjacentHTML("beforeend", renderViewCube());
  pane.cubeView = { viewport: pane.viewport };
  attachViewCube(pane.cubeView, host);
  pane.cubeClick = (event) => {
    const target = event.target?.closest?.('[data-cam-viewcube] [data-cam-action="view-standard"]');
    if (!target || !host.contains(target)) return;
    event.preventDefault();
    event.stopPropagation();
    event.stopImmediatePropagation?.();
    pane.viewport.setStandardView?.(String(target.dataset.camView ?? "iso"));
  };
  host.addEventListener("click", pane.cubeClick, true);
}

export function attachAssemblyLibraryViewports(context, view, mount, options = {}) {
  if (!view || view.activeAreaId !== "assemblies") {
    disposeAssemblyLibraryViewports(view);
    return null;
  }
  const sceneHost = mount?.querySelector?.("[data-tube-assembly-scene-viewport]");
  if (!sceneHost) return null;
  const state = assemblyLibraryState(view);
  if (!stockInputBindings.has(mount)) {
    stockInputBindings.set(mount, { view });
    mount.addEventListener("input", (event) => {
      if (!event.target?.closest?.("[data-stock-process-editor], [data-assembly-parameter-scope]")) return;
      const current = stockInputBindings.get(mount)?.view;
      if (current && assemblyLibraryState(current).processInputMode === "stock-operation")
        markStockProcessEdited(current, selectedAssemblyTemplate(current));
    });
  } else stockInputBindings.get(mount).view = view;
  let controller = assemblyViewportControllers.get(view);
  if (!controller) {
    const viewportFactory = options.viewportFactory ?? createThreeViewport;
    controller = {
      context,
      view,
      mount,
      generation: 0,
      scene: createAssemblyViewport(state, viewportFactory),
    };
    assemblyViewportControllers.set(view, controller);
  }
  controller.context = context;
  controller.mount = mount;
  mountAssemblyViewportPane(controller.scene, sceneHost);
  view.viewport?.setVisibleEntityIds?.([]);
  view.viewport?.setContinuousRendering?.(false);
  if (state.preview && (state.appliedKey !== assemblyAppliedKey(view, state, state.preview.key) || !controller.scene.ready)) {
    void applyAssemblyPreview(context, view, state.preview, state.preview.key);
  } else {
    refreshAssemblyVisibility(view);
  }
  return controller;
}

export function disposeAssemblyLibraryViewports(view) {
  if (view?.tubeDesignerAssemblyLibrary) {
    view.tubeDesignerAssemblyLibrary.selectionRequest = null;
    view.tubeDesignerAssemblyLibrary.previewRequest = null;
  }
  if (!view || (typeof view !== "object" && typeof view !== "function")) return;
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return;
  controller.generation += 1;
  if (controller.scene.cubeView) stopViewCubeAnimation(controller.scene.cubeView);
  if (controller.scene.host && controller.scene.cubeClick)
    controller.scene.host.removeEventListener?.("click", controller.scene.cubeClick, true);
  controller.scene.viewport.dispose?.();
  assemblyViewportControllers.delete(view);
}

function clearAssemblyLibraryViewports(view) {
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return;
  controller.generation += 1;
  const pane = controller.scene;
  pane.viewport.setVisibleEntityIds?.([]);
  pane.viewport.setDimensionAnnotations?.([]);
  // An empty snapshot cancels any resource load already in flight without
  // remounting the canvas or losing its event listeners.
  const resources = controller.context?.sceneProxy?.resources ?? view.sceneProxy?.resources ?? controller.context?.projectProxy?.resources;
  void Promise.resolve(pane.viewport.applyViewSnapshot({ revision: `assembly-clear:${controller.generation}`, rows: [] }, resources)).catch(() => {});
  pane.ready = false;
  pane.appliedKey = "";
  pane.templateId = "";
  pane.mode = "";
  pane.rows = [];
  pane.pendingKey = "";
  pane.failedKey = "";
}

function visibleAssemblyEntityIds(rows, focusRole) {
  return rows.filter((row) => !focusRole || row.roles?.includes(focusRole)).map((row) => row.entityId);
}

function refreshAssemblyVisibility(view) {
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return false;
  const focusRole = assemblyLibraryState(view).focusRole;
  const pane = controller.scene;
  if (pane.rows.length) pane.viewport.setVisibleEntityIds?.(visibleAssemblyEntityIds(pane.rows, focusRole));
  return true;
}

export function controlAssemblyLibraryCamera(view, command) {
  const pane = assemblyViewportControllers.get(view)?.scene;
  if (!pane?.viewport) return false;
  if (command === "fit") return Boolean(pane.viewport.fitViewToViewport?.(1.18));
  if (!["iso", "front", "top", "right"].includes(command)) return false;
  const changed = pane.viewport.setStandardView?.(command) !== false;
  pane.viewport.fitViewToViewport?.(1.18);
  return changed;
}

async function applyAssemblyPreviewPane(pane, rows, resources, revision, templateId, mode, focusRole,
  isCurrent = () => true, preferredView = "iso") {
  const previousCamera = pane.ready ? pane.viewport.getCameraState?.() : null;
  const receipt = await pane.viewport.applyViewSnapshot({ revision, rows }, resources);
  if (!isCurrent() || receipt?.superseded) return false;
  if (!receipt?.applied || receipt.missingGeometryEntityIds?.length || (rows.length && !receipt.entityIds?.length)) {
    throw new Error("装配预览几何未完整进入三维场景。");
  }
  pane.rows = rows;
  pane.viewport.setVisibleEntityIds?.(visibleAssemblyEntityIds(rows, focusRole));
  if (!pane.ready || pane.templateId !== templateId) {
    pane.viewport.setStandardView?.(preferredView);
    pane.viewport.fitViewToViewport?.(1.18);
  } else if (previousCamera) {
    pane.viewport.setCameraState?.(previousCamera);
  }
  pane.ready = rows.length > 0;
  pane.appliedKey = revision;
  pane.templateId = templateId;
  pane.mode = mode;
  return true;
}

function assemblyExampleExploded(view, state) {
  if (state.processInputMode === "stock-operation") return !state.preview?.designParts?.length || !!state.exploded;
  if (selectedAssemblyTemplate(view)?.exampleInput) return !!state.exploded;
  return selectedAssemblyTemplate(view)?.manufacturingPlan?.realization === "integrated"
    && state.preview?.layoutShape !== "l"
    && !hasIntegratedFormedPreview(state.preview) || !!state.exploded;
}

function assemblyAppliedKey(view, state, key) {
  const exploded = assemblyExampleExploded(view, state);
  return `${exploded ? "exploded" : "finished"}:${!exploded && state.preview?.independentFinishedProduct
    ? `${state.preview.finishedOwnerGeneration}:${state.preview.finishedKey}` : key}`;
}

async function applyAssemblyPreview(context, view, preview, key) {
  if (view.activeAreaId !== "assemblies" || assemblyPreviewKey(view) !== key || assemblyLibraryState(view).preview !== preview) return false;
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return false;
  const baseResources = context.sceneProxy?.resources ?? view.sceneProxy?.resources ?? context.projectProxy?.resources;
  const state = assemblyLibraryState(view);
  const exploded = assemblyExampleExploded(view, state);
  const templateId = !exploded && preview.independentFinishedProduct
    ? `finished:${preview.finishedOwnerGeneration}:${preview.layoutShape}` : String(preview.plan?.templateId ?? "");
  const mode = exploded ? "exploded" : "finished";
  const appliedKey = assemblyAppliedKey(view, state, key);
  const revision = `assembly-${appliedKey}`;
  if (state.appliedKey === appliedKey && controller.scene.ready) return true;
  if (controller.scene.pendingKey === revision) return false;
  const generation = ++controller.generation;
  controller.scene.pendingKey = revision;
  const isCurrentDisplay = () => {
    if (controller.generation !== generation || view.activeAreaId !== "assemblies"
        || assemblyAppliedKey(view, state, assemblyPreviewKey(view)) !== appliedKey) return false;
    // A pending product snapshot also satisfies a newer process selection
    // accepting exactly the same product and resource owner.
    return !exploded && preview.independentFinishedProduct
      ? state.preview?.independentFinishedProduct && state.preview.finishedKey === preview.finishedKey
        && state.preview.finishedOwnerGeneration === preview.finishedOwnerGeneration
      : state.preview === preview && assemblyPreviewKey(view) === key;
  };
  const formedResources = !exploded && hasFinishedShapePreview(preview) ? preview.finishedShape.resources
    : !exploded && hasIntegratedFormedPreview(preview) ? preview.formedPreview.resources : null;
  const resources = formedResources ? {
    async get(url, options) {
      const data = formedResources.get(url);
      return data ? new Response(data, { status: 200 }) : baseResources.get(url, options);
    },
  } : baseResources;
  try {
    await applyAssemblyPreviewPane(controller.scene, assemblySceneRows(preview, exploded), resources,
      revision, templateId, mode, state.focusRole,
      isCurrentDisplay,
      selectedAssemblyTemplate(view)?.previewScene?.preferredView ?? "iso");
    if (!isCurrentDisplay()) return false;
    const finishedBounds = mode === "finished" ? controller.scene.viewport.getVisibleBounds?.() : null;
    controller.scene.viewport.setDimensionAnnotations?.(
      buildAssemblyPreviewDimensionAnnotations(state.preview, mode, finishedBounds));
    assemblyLibraryState(view).appliedKey = appliedKey;
    return true;
  } finally {
    if (controller.scene.pendingKey === revision) controller.scene.pendingKey = "";
  }
}

async function applyCurrentAssemblyPreview(context, view) {
  const state = assemblyLibraryState(view);
  if (!state.preview) return false;
  const preview = state.preview;
  const appliedKey = assemblyAppliedKey(view, state, preview.key);
  try {
    return await applyAssemblyPreview(context, view, preview, preview.key);
  } catch (error) {
    const sameProduct = !state.exploded && preview.independentFinishedProduct && state.preview?.independentFinishedProduct
      && state.preview.finishedKey === preview.finishedKey
      && state.preview.finishedOwnerGeneration === preview.finishedOwnerGeneration;
    if ((state.preview !== preview && !sameProduct) || view.activeAreaId !== "assemblies"
        || assemblyPreviewKey(view) !== preview.key || assemblyAppliedKey(view, state, preview.key) !== appliedKey) return false;
    state.previewError = `装配预览显示失败：${error?.message ?? error}`;
    view.tubeDesignerAssemblyLibraryRenderProject?.();
    return false;
  }
}

function assemblyApplicabilityKey(view, template) {
  return finishedProductKey([template.id, template.version, template.generationDigest,
    finishedProductInput(view), assemblyPreviewParameterGroups(view, template).parameters,
    assemblyLibraryState(view).localAnchorDrafts[template.id] ?? {}]);
}

function renderLocalProcessPlacement(view, template) {
  if (supportsStockOperations(template)) return "";
  const input = assemblyLibraryState(view).localProcessInputs[template.id];
  if (!input) return "";
  const fields = Object.entries(input.parts).map(([role, part]) => {
    const anchor = part.anchor;
    if (!anchor) return "";
    const label = template.participants.find((item) => item.role === role)?.label ?? role;
    const data = (key) => `data-cam-change-action="tube-designer-assembly-local-anchor-change" data-process-role="${attr(role)}" data-process-anchor-key="${key}"`;
    const selector = (key, label, values) => `<label class="tube-designer-field"><span>${label}</span><select ${data(key)}>${values.map(([value, name]) => `<option value="${value}"${anchor[key] === value ? " selected" : ""}>${name}</option>`).join("")}</select></label>`;
    const numeric = (key, label) => `<label class="tube-designer-field"><span>${label}</span><input type="number" value="${attr(anchor[key] ?? 0)}" step="0.1" ${data(key)}></label>`;
    return `<details class="tube-assembly-scene-member" id="local-process-anchor-${attr(role)}" open><summary><strong>${text(label)}</strong><small>${anchor.kind === "end" ? "端部加工" : "侧面加工"}</small></summary><div class="tube-connection-library-parameter-grid">${anchor.kind === "end"
      ? selector("end", "加工端", [["start", "起端"], ["end", "末端"]]) + numeric("trim", "距所选端向内（mm）")
      : selector("face", "加工侧面", [["top", "上侧"], ["bottom", "下侧"], ["left", "左侧"], ["right", "右侧"]]) + numeric("station", "距构件起端（mm）") + numeric("offset", "侧面横移（mm）")}${numeric("rotation", "加工平面转角（°）")}</div></details>`;
  }).join("");
  return `<section class="tube-connection-library-parameter-section basic" data-local-process-placement><header><strong>加工定位</strong><small>属于当前工艺</small></header>${fields}</section>`;
}

async function finishedGeometryPlanForPreview(context, view, product) {
  const state = finishedProductState(view), owner = context.sceneProxy;
  const scope = assemblyPreviewScope(context, view);
  if (state.processInputOwner !== owner || state.processInputScope !== scope) {
    state.processInputOwner = owner; state.processInputScope = scope;
    state.processInputPlans = new Map(); state.processInputFailures = new Map();
  }
  const key = finishedProductGeometryKey(view, product);
  if (state.processInputFailures?.has(key)) throw state.processInputFailures.get(key);
  let pending = state.processInputPlans.get(key);
  if (!pending) {
    pending = owner.invoke("TubeDesigner.ResolveAssemblyTemplatePreview", {
      finishedOnly: true, finishedProduct: clone(product),
    }, { timeoutMs: 120000 }).then((plan) => {
      if (plan?.schema !== "icax.finished-product-preview" || finishedProductKey(plan.finishedProduct) !== finishedProductKey(product))
        throw new Error("成品几何未返回原始输入");
      return plan;
    }).catch((error) => {
      if (state.processInputPlans.get(key) === pending) {
        state.processInputPlans.delete(key);
        state.processInputFailures ??= new Map();
        touchCache(state.processInputFailures, key, error, 16);
      }
      throw error;
    });
    touchCache(state.processInputPlans, key, pending, 16);
  }
  return pending;
}

async function finishedProcessInputForPreview(context, view, template, product) {
  const input = assemblyProcessInput(template, product, await finishedGeometryPlanForPreview(context, view, product));
  const state = assemblyLibraryState(view);
  for (const [role, draft] of Object.entries(state.localAnchorDrafts[template.id] ?? {}))
    if (input.parts[role]) input.parts[role].anchor = { ...input.parts[role].anchor, ...clone(draft) };
  state.localProcessInputs[template.id] = clone(input);
  return input;
}

function ensureAssemblyExampleSelection(context, view, template, ops = null, userSelected = false) {
  if (typeof context?.sceneProxy?.invoke !== "function") return null;
  if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
  const state = assemblyLibraryState(view);
  const selectionKey = assemblyApplicabilityKey(view, template);
  if (state.selectionRequest?.key === selectionKey) return state.selectionRequest;
  if (state.selectionFailureKey === selectionKey) return null;
  const owner = context.sceneProxy;
  const scope = assemblyPreviewScope(context, view);
  const product = clone(finishedProductInput(view));
  const productKey = finishedProductKey(product);
  const parameters = assemblyPreviewParameterGroups(view, template).parameters;
  const request = { key: selectionKey, templateId: template.id, promise: null,
    progress: { phase: "正在检查成品是否适用于此工艺…", completed: 0, total: 0 } };
  const isCurrent = () => state.selectionRequest === request && context.sceneProxy === owner
    && assemblyPreviewScope(context, view) === scope
    && view.activeAreaId === "assemblies"
    && finishedProductKey(finishedProductInput(view)) === productKey;
  const repaint = () => {
    if (ops?.renderProject) ops.renderProject(context, view);
    else view.tubeDesignerAssemblyLibraryRenderProject?.();
  };
  state.selectionRequest = request;
  if (userSelected) state.exploded = false;
  state.previewError = "";
  request.promise = Promise.resolve().then(async () => {
    if (!isCurrent() || !hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return;
    const processInput = await finishedProcessInputForPreview(context, view, template, product);
    if (!isCurrent()) return;
    const result = await owner.invoke("TubeDesigner.CheckAssemblyTemplateApplicability", {
      templateId: template.id, processInput, parameters,
    }, { timeoutMs: 120000 });
    if (!isCurrent()) return;
    if (result?.schema !== "icax.assembly-applicability" || result.templateId !== template.id
        || typeof result.applicable !== "boolean") throw new Error("模板未返回有效的成品适用性结果");
    // A failed applicability check keeps the independent product and its model.
    state.selectionProblem = result.applicable ? "" : result.reason || "当前工艺不适用于此成品";
    state.selectedId = template.id;
    state.selectionValidatedKey = selectionKey;
    state.selectionOwner = owner;
    state.selectionRequest = null;
    invalidateAssemblyPreview(view);
    repaint();
    const preview = ensureProductFirstAssemblyPreview(context, view, template);
    if (preview?.promise) await preview.promise;
  }).catch((error) => {
    if (!isCurrent()) return;
    state.selectedId = template.id;
    state.selectionProblem = `当前成品暂不能用于此工艺：${error?.message ?? error}`;
    state.selectionValidatedKey = selectionKey;
    state.selectionOwner = owner;
    state.exploded = false;
    invalidateAssemblyPreview(view);
    state.selectionFailureKey = selectionKey;
    repaint();
    ensureProductFirstAssemblyPreview(context, view, template);
  }).finally(() => {
    if (state.selectionRequest === request) state.selectionRequest = null;
  });
  return request;
}

function cachedAssemblyManufacturing(context, view, template, key, product, parameters, processDrafts, consumer) {
  const productState = finishedProductState(view);
  const cache = productState.manufacturingCache;
  const owner = context.sceneProxy;
  const scope = cache.scope;
  let entry = cache.plans.get(key);
  if (!entry) entry = { plan: null, result: null, run: null, consumers: new Set() };
  touchCache(cache.plans, key, entry, 16);
  entry.consumers.add(consumer);
  const cacheIsCurrent = () => productState.manufacturingCache === cache && context.sceneProxy === owner
    && assemblyPreviewScope(context, view) === scope && cache.plans.get(key) === entry;
  const activeConsumers = () => [...entry.consumers].filter((item) => item.isCurrent());
  const isActive = () => cacheIsCurrent() && activeConsumers().length > 0;
  const reportProgress = (phase, completed = 0, total = 0) => {
    for (const item of activeConsumers()) item.reportProgress(phase, completed, total);
  };
  if (!entry.result && !entry.run) {
    entry.run = Promise.resolve().then(async () => {
      if (!entry.plan) {
        const processInput = await finishedProcessInputForPreview(context, view, template, product);
        if (!cacheIsCurrent()) return null;
        const applicability = await owner.invoke("TubeDesigner.CheckAssemblyTemplateApplicability", {
          templateId: template.id, processInput: clone(processInput), parameters,
        }, { timeoutMs: 120000 });
        if (!cacheIsCurrent()) return null;
        if (applicability?.schema !== "icax.assembly-applicability" || applicability.templateId !== template.id
            || typeof applicability.applicable !== "boolean") throw new Error("模板未返回有效的成品适用性结果");
        if (!applicability.applicable) throw new Error(applicability.reason || "当前工艺不适用于此成品");
        reportProgress("正在计算装配工艺和下料件…");
        const plan = await owner.invoke("TubeDesigner.ResolveAssemblyTemplatePreview", {
          templateId: template.id, processInput: clone(processInput), parameters, processDrafts, manufacturingOnly: true,
        }, { timeoutMs: 120000 });
        if (!cacheIsCurrent()) return null;
        if (plan?.schema !== "icax.assembly-preview-plan" || plan.templateId !== template.id
            || finishedProductKey(plan.processInput) !== finishedProductKey(processInput)
            || !Array.isArray(plan.manufacturingParts) || !plan.manufacturingParts.length)
          throw new Error("装配工艺未返回有效的下料计划，或改写了输入成品");
        entry.plan = plan;
      }
      if (!isActive()) return null;
      const dependency = finishedProductKey([assemblyManufacturingDependencies(view, template),
        finishedProductProfileRevisions(view, product)]);
      // Request content already includes section dimensions and process values.
      // Profile and mold revisions remain dependencies even when inputs match.
      const parts = await evaluateAssemblyPreviewParts(context, entry.plan.manufacturingParts, "manufacturing",
        new Map(), template.id, (done) => reportProgress("正在生成下料件…", done, entry.plan.manufacturingParts.length),
        isActive, { parts: cache.parts, scope, dependency });
      if (!parts || !cacheIsCurrent()) return null;
      entry.result = { plan: entry.plan, manufacturingParts: parts };
      return entry.result;
    }).catch((error) => {
      if (cache.plans.get(key) === entry) cache.plans.delete(key);
      throw error;
    }).finally(() => { entry.run = null; });
  }
  return Promise.resolve(entry.result ?? entry.run).finally(() => entry.consumers.delete(consumer));
}

function stockProcessStorage(context, view, template) {
  const owner = context.sceneProxy, scope = assemblyPreviewScope(context, view);
  let storage = view.tubeDesignerStockProcessStorage;
  if (!storage || storage.owner !== owner || storage.scope !== scope) {
    const draft = stockProcessState(view, template);
    draft.planId = ""; draft.storedPlanDigest = ""; delete draft.unsavedPlanId;
    storage = view.tubeDesignerStockProcessStorage = { owner, scope, status: "idle", plans: [],
      error: "", message: "", loadRequest: null, saveRequest: null, pendingCommit: null };
  }
  return storage;
}

function stockStorageIsCurrent(context, view, storage) {
  return view.tubeDesignerStockProcessStorage === storage && context.sceneProxy === storage.owner
    && assemblyPreviewScope(context, view) === storage.scope;
}

function ensureStockProcessStorage(context, view, template) {
  const storage = stockProcessStorage(context, view, template);
  if (storage.status !== "idle") return storage;
  storage.status = "loading";
  const draft = stockProcessState(view, template), revision = draft.revision ?? 0;
  const request = {};
  storage.loadRequest = request;
  request.promise = Promise.resolve().then(() => storage.owner.invoke("TubeDesigner.GetAssemblyProcessPlans", {}))
    .then((response) => {
      if (!stockStorageIsCurrent(context, view, storage) || storage.loadRequest !== request) return;
      if (response?.schema !== "icax.assembly-process-plans" || !Array.isArray(response.plans))
        throw new Error("保存的加工方案没有返回有效清单。");
      storage.plans = response.plans; storage.status = "ready"; storage.error = "";
      const current = stockProcessState(view, selectedAssemblyTemplate(view));
      const recovered = !current.planId && current.unsavedPlanId
        ? storage.plans.find((record) => record.planId === current.unsavedPlanId) : null;
      if (recovered) {
        current.planId = recovered.planId; current.storedPlanDigest = recovered.planDigest;
        storage.message = "已确认上一次保存；当前编辑草稿保持不变。";
      }
      // A late project read must never replace a draft the user has edited.
      if (storage.plans.length && current === draft && current.revision === revision && revision === 0
          && !current.planId && assemblyLibraryState(view).processInputMode === "stock-operation") {
        try {
          const id = restoreStockProcessPlan(view, storage.plans[0], assemblyTemplates(view));
          assemblyLibraryState(view).selectedId = id;
          invalidateAssemblyPreview(view, false);
          storage.message = "已恢复项目中保存的加工方案。";
        } catch (error) { storage.error = error.message; }
      }
    }).catch((error) => {
      if (!stockStorageIsCurrent(context, view, storage) || storage.loadRequest !== request) return;
      storage.status = "failed"; storage.error = error?.message ?? String(error);
    }).finally(() => {
      if (!stockStorageIsCurrent(context, view, storage) || storage.loadRequest !== request) return;
      storage.loadRequest = null; view.tubeDesignerAssemblyLibraryRenderProject?.();
    });
  return storage;
}

function renderStockProcessStorage(view, template) {
  const storage = view.tubeDesignerStockProcessStorage, draft = stockProcessState(view, template);
  const state = assemblyLibraryState(view);
  const ready = state.preview?.stockProcess && state.preview.key === assemblyPreviewKey(view, template)
    && !!state.preview.plan?.planDigest && !state.previewRequest;
  const saving = !!storage?.saveRequest;
  const plans = storage?.plans ?? [];
  const selected = plans.some((item) => item.planId === draft.planId);
  const message = storage?.error || storage?.message || (storage?.status === "loading" ? "正在读取项目中的加工方案…" : "");
  return `<section class="tube-connection-library-parameter-section basic" data-stock-plan-storage><header><strong>加工方案</strong><small>${draft.planId ? "已保存方案" : "新方案"}</small></header><div class="tube-connection-library-parameter-grid"><label class="tube-designer-field"><span>项目中的方案</span><select data-cam-change-action="tube-designer-stock-plan-load"${saving ? " disabled" : ""}>${selected ? "" : '<option value="" selected>当前草稿</option>'}${plans.map((item, index) => `<option value="${attr(item.planId)}"${item.planId === draft.planId ? " selected" : ""}>${text(item.sourceInput?.stocks?.[0]?.label || `加工方案 ${index + 1}`)}</option>`).join("")}</select></label></div><div class="tube-assembly-work-mode"><button type="button" data-cam-action="tube-designer-stock-plan-save"${ready && !saving ? "" : " disabled"}>${saving ? "正在保存…" : draft.planId ? "更新加工方案" : "保存加工方案"}</button><button type="button" data-cam-action="tube-designer-stock-plan-new"${saving ? " disabled" : ""}>另存为新方案</button>${storage?.status === "failed" || storage?.error ? '<button type="button" data-cam-action="tube-designer-stock-plan-reload">重新读取</button>' : ""}</div>${message ? `<p class="${storage?.error ? "tube-assembly-workflow-block" : "tube-assembly-product-angle"}" role="status">${text(message)}</p>` : ""}</section>`;
}

function stockRequestId() {
  if (globalThis.crypto?.randomUUID) return crypto.randomUUID();
  const bytes = crypto.getRandomValues(new Uint8Array(16)); bytes[6] = (bytes[6] & 15) | 64; bytes[8] = (bytes[8] & 63) | 128;
  const hex = [...bytes].map((value) => value.toString(16).padStart(2, "0")).join("");
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

async function handleStockProcessStorageAction(context, view, action, target, ops) {
  const template = selectedAssemblyTemplate(view), state = assemblyLibraryState(view);
  const storage = stockProcessStorage(context, view, template), draft = stockProcessState(view, template);
  if (storage.saveRequest) return;
  storage.error = ""; storage.message = "";
  if (action === "tube-designer-stock-plan-reload") {
    storage.status = "idle"; ensureStockProcessStorage(context, view, template); ops.renderProject(context, view); return;
  }
  if (action === "tube-designer-stock-plan-new") {
    draft.planId = ""; draft.storedPlanDigest = ""; delete draft.unsavedPlanId; storage.pendingCommit = null;
    markStockProcessEdited(view, template); ops.renderProject(context, view); return;
  }
  if (action === "tube-designer-stock-plan-load") {
    const record = storage.plans.find((item) => item.planId === target.value);
    if (record) {
      try {
        state.selectedId = restoreStockProcessPlan(view, record, assemblyTemplates(view)); storage.pendingCommit = null;
        invalidateAssemblyPreview(view, false);
      } catch (error) { storage.error = error.message; }
    }
    ops.renderProject(context, view); return;
  }
  const preview = state.preview, key = assemblyPreviewKey(view, template);
  if (!preview?.stockProcess || preview.key !== key || state.previewRequest || !preview.plan?.planDigest) return;
  const sourceInput = stockProcessPayload(view, template, assemblyTemplates(view));
  const planId = draft.planId || (draft.unsavedPlanId ??= stockRequestId());
  const commitKey = finishedProductKey([planId, sourceInput, preview.plan.planDigest, draft.storedPlanDigest]);
  if (storage.pendingCommit?.key !== commitKey) storage.pendingCommit = { key: commitKey,
    payload: { planId, requestId: stockRequestId(), expectedPlanDigest: preview.plan.planDigest,
      ...(draft.storedPlanDigest ? { expectedStoredPlanDigest: draft.storedPlanDigest } : {}), ...sourceInput } };
  const request = { payload: clone(storage.pendingCommit.payload) };
  storage.saveRequest = request; ops.renderProject(context, view);
  try {
    const response = await storage.owner.invoke("TubeDesigner.CommitAssemblyProcessPlan", request.payload, { timeoutMs: 180000 });
    if (!stockStorageIsCurrent(context, view, storage) || storage.saveRequest !== request) return;
    if (response?.schema !== "icax.assembly-process-commit" || response.planId !== planId || !response.planDigest)
      throw new Error("加工方案保存结果无效，请重试同一次保存。");
    const record = { planId, planDigest: response.planDigest, sourceInput, entityIds: response.entityIds ?? [] };
    storage.plans = [record, ...storage.plans.filter((item) => item.planId !== planId)];
    // Saving may finish after another edit. Only the saved identity advances; the draft remains untouched.
    if (view.tubeDesignerStockProcess === draft) { draft.planId = planId; draft.storedPlanDigest = response.planDigest; }
    storage.pendingCommit = null;
    storage.message = `加工方案已保存 · ${record.entityIds.length} 件下料。`;
  } catch (error) {
    if (stockStorageIsCurrent(context, view, storage) && storage.saveRequest === request)
      storage.error = error?.message ?? String(error);
  } finally {
    if (stockStorageIsCurrent(context, view, storage) && storage.saveRequest === request) {
      storage.saveRequest = null; ops.renderProject(context, view);
    }
  }
}

function ensureStockProcessPreview(context, view, template) {
  if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
  const state = assemblyLibraryState(view), key = assemblyPreviewKey(view, template);
  const scope = assemblyPreviewScope(context, view), owner = context.sceneProxy;
  if (state.preview?.stockProcess && state.preview.key === key) {
    if (state.appliedKey !== assemblyAppliedKey(view, state, key)) void applyCurrentAssemblyPreview(context, view);
    return state.preview;
  }
  if (state.previewRequest?.key === key || state.previewFailureKey === key) return state.previewRequest;
  const payload = stockProcessPayload(view, template, assemblyTemplates(view));
  const request = { key, kind: "manufacturing", templateId: template.id, promise: null,
    progress: { phase: "正在计算连续母材的全部折弯…", completed: 0, total: payload.instances.length } };
  const isCurrent = () => state.previewRequest === request && context.sceneProxy === owner
    && assemblyPreviewScope(context, view) === scope
    && state.processInputMode === "stock-operation" && view.activeAreaId === "assemblies" && assemblyPreviewKey(view) === key;
  state.previewRequest = request; state.previewError = "";
  request.promise = Promise.resolve().then(async () => {
    if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
    const result = await owner.invoke("TubeDesigner.PreviewAssemblyProcessPlan", payload, { timeoutMs: 180000 });
    if (!isCurrent()) return null;
    const plan = result?.plan ?? result;
    if (plan?.schema !== "icax.assembly-process-plan" || !Array.isArray(plan.manufacturingParts) || !plan.manufacturingParts.length)
      throw new Error("连续母材未返回有效的加工计划");
    const nativeResults = plan.nativeResults ?? result?.nativeResults;
    const manufacturingParts = plan.manufacturingParts.map((part) => {
      const response = nativeResults?.find((item) => item.blankId === (part.blankId ?? part.id));
      if (!response?.geometry?.url || response.previewComputed !== true || response.resultValid !== true || response.solidCount !== 1)
        throw new Error(`${part.label || "母材"}未返回有效的下料形状。`);
      return { ...part, response };
    });
    if (!isCurrent()) return null;
    request.progress = { phase: "正在准备成形校核…", completed: 0, total: plan.calibrationParts?.length ?? 0 };
    const designParts = await evaluateAssemblyPreviewParts(context, plan.calibrationParts ?? [], "design",
      new Map(), template.id, (completed) => { request.progress.completed = completed;
        if (isCurrent()) view.tubeDesignerAssemblyLibraryRenderProject?.(); }, isCurrent);
    if (!designParts || !isCurrent()) return null;
    return { key, stockProcess: true, layoutShape: "straight", designParts, manufacturingParts,
      processReady: true, plan: { ...plan, templateId: template.id,
        resolvedWorkflow: plan.resolvedWorkflow ?? plan.workflow ?? { realization: "integrated",
          partOperations: plan.operations ?? [], checks: [], validationStatus: "resolved" } } };
  }).then(async (preview) => {
    if (!preview || !isCurrent()) return;
    state.preview = preview; state.previewRequest = null;
    view.tubeDesignerAssemblyLibraryRenderProject?.();
    await applyCurrentAssemblyPreview(context, view);
  }).catch((error) => {
    if (!isCurrent()) return;
    state.previewRequest = null; state.previewFailureKey = key; state.previewError = error?.message ?? String(error);
    view.tubeDesignerAssemblyLibraryRenderProject?.();
  });
  return request;
}

function ensureProductFirstAssemblyPreview(context, view, template) {
  if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
  const state = assemblyLibraryState(view);
  const key = assemblyPreviewKey(view, template);
  const product = clone(finishedProductInput(view));
  const productKey = finishedProductKey(product);
  const finishedKey = finishedProductGeometryKey(view, product);
  const kind = state.exploded ? "manufacturing" : "finished";
  const failureKey = `${kind}:${key}`;
  const productState = finishedProductState(view);
  const scope = assemblyPreviewScope(context, view);
  if (productState.previewOwner !== context.sceneProxy || productState.previewOwnerScope !== scope) {
    productState.previewOwner = context.sceneProxy;
    productState.previewOwnerScope = scope;
    productState.previewCache = new Map();
    productState.manufacturingCache = { scope, plans: new Map(), parts: new Map() };
    productState.previewOwnerGeneration = (productState.previewOwnerGeneration ?? 0) + 1;
    state.preview = null;
    state.previewRequest = null;
    state.previewFailureKey = "";
    state.appliedKey = "";
  }
  // A failed render must not start an unobserved cache request on repaint.
  if (state.previewFailureKey === failureKey) return state.previewRequest;
  const cache = productState.previewCache;
  let entry = cache.get(product.shapeId);
  if (!entry || entry.key !== finishedKey) {
    entry = { key: finishedKey, finished: null, promise: null };
    cache.set(product.shapeId, entry);
    const cachedEntry = entry;
    const cacheIsCurrent = () => productState.previewOwner === context.sceneProxy
      && productState.previewCache === cache && cache.get(product.shapeId) === cachedEntry;
    // One result per product shape, shared by every process accepting its input.
    // This request remains usable when a different process is selected.
    entry.promise = Promise.resolve().then(async () => {
      if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
      const plan = await finishedGeometryPlanForPreview(context, view, product);
      if (!cacheIsCurrent()) return null;
      if (plan?.schema !== "icax.finished-product-preview"
          || finishedProductKey(plan.finishedProduct) !== productKey || !Array.isArray(plan.designParts)
          || plan.designParts.length !== Object.keys(product.spans).length)
        throw new Error("成品预览未返回原始成品输入");
      const finishedShape = plan.layoutShape === "l" ? buildAssemblyFinishedShapePreview(plan) : null;
      const designParts = finishedShape ? plan.designParts : await evaluateAssemblyPreviewParts(
        context, plan.designParts, "design", new Map(), `finished:${product.shapeId}`, null,
        () => cacheIsCurrent() && view.activeAreaId === "assemblies");
      if (!designParts || !cacheIsCurrent()) {
        if (cacheIsCurrent()) cache.delete(product.shapeId);
        return null;
      }
      cachedEntry.finished = { plan, layoutShape: plan.layoutShape, finishedShape, designParts };
      return cachedEntry.finished;
    }).catch((error) => {
      if (cacheIsCurrent()) cache.delete(product.shapeId);
      throw error;
    });
    // The display consumer can be cancelled before it awaits this shared
    // cache promise. Keep the failure observed; active consumers still receive it.
    void entry.promise.catch(() => {});
  }
  const bindFinished = (finished) => ({ ...finished, key, finishedKey,
    finishedOwnerGeneration: productState.previewOwnerGeneration,
    finishedPlan: finished.plan,
    plan: { ...finished.plan, templateId: template.id }, manufacturingParts: [],
    processReady: false, independentFinishedProduct: true });
  if (state.preview?.key !== key || state.preview.finishedKey !== finishedKey)
    state.preview = entry.finished ? bindFinished(entry.finished) : null;
  if (state.preview && (!state.exploded || state.preview.processReady)) {
    if (state.appliedKey !== assemblyAppliedKey(view, state, key)) void applyCurrentAssemblyPreview(context, view);
    return state.preview;
  }
  if (state.previewRequest?.key === key && state.previewRequest.kind === kind) return state.previewRequest;
  const request = { key, kind, templateId: template.id, promise: null,
    progress: { phase: entry.finished ? "正在计算装配工艺和下料件…" : "正在生成成品外形…", completed: 0, total: 0 } };
  const isCurrent = () => state.previewRequest === request && view.activeAreaId === "assemblies"
    && state.exploded === (kind === "manufacturing")
    && assemblyPreviewScope(context, view) === scope && assemblyPreviewKey(view) === key;
  const repaint = () => { if (isCurrent()) view.tubeDesignerAssemblyLibraryRenderProject?.(); };
  const parameters = assemblyPreviewParameterGroups(view, template).parameters;
  const processDrafts = clone(state.processDrafts?.[template.id] ?? {});
  state.previewError = "";
  state.previewRequest = request;
  request.promise = Promise.resolve().then(async () => {
    const finished = entry.finished ?? await entry.promise;
    if (!finished || !isCurrent()) return null;
    const preview = state.preview?.key === key ? state.preview : bindFinished(finished);
    state.preview = preview;
    if (kind === "finished") return preview;
    const result = await cachedAssemblyManufacturing(context, view, template, key, product, parameters, processDrafts, {
      isCurrent,
      reportProgress(phase, completed, total) {
        request.progress = { phase, completed, total };
        repaint();
      },
    });
    if (!result || !isCurrent()) return null;
    preview.plan = result.plan;
    preview.manufacturingParts = result.manufacturingParts;
    preview.processReady = true;
    return preview;
  }).then(async (preview) => {
    if (!isCurrent()) {
      if (state.previewRequest === request) state.previewRequest = null;
      return;
    }
    if (preview) { state.preview = preview; await applyCurrentAssemblyPreview(context, view); }
    if (!isCurrent()) return;
    state.previewRequest = null;
    state.previewFailureKey = "";
    view.tubeDesignerAssemblyLibraryRenderProject?.();
  }).catch((error) => {
    if (!isCurrent()) {
      if (state.previewRequest === request) state.previewRequest = null;
      return;
    }
    state.previewRequest = null;
    state.previewFailureKey = failureKey;
    state.previewError = error?.message ?? String(error);
    view.tubeDesignerAssemblyLibraryRenderProject?.();
  });
  return request;
}

export function ensureAssemblyLibraryPreview(context, view, template = selectedAssemblyTemplate(view)) {
  if (view.activeAreaId !== "assemblies" || !template || typeof context?.sceneProxy?.invoke !== "function") return null;
  if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
  const state = assemblyLibraryState(view);
  if (state.processInputMode === "stock-operation" && supportsStockOperations(template))
    return ensureStockProcessPreview(context, view, template);
  if (template.exampleInput) {
    if (!state.selectionRequest && (state.selectionOwner !== context.sceneProxy
        || state.selectionValidatedKey !== assemblyApplicabilityKey(view, template)))
      ensureAssemblyExampleSelection(context, view, template);
    return ensureProductFirstAssemblyPreview(context, view, template);
  }
  const key = assemblyPreviewKey(view, template);
  if (state.preview?.key === key) {
    if (!state.previewRequest && state.appliedKey !== assemblyAppliedKey(view, state, key))
      void applyCurrentAssemblyPreview(context, view);
    return state.preview;
  }
  if (state.previewRequest?.key === key || state.previewFailureKey === key) return state.previewRequest;
  state.previewError = "";
  const parameterGroups = assemblyPreviewParameterGroups(view, template);
  const processDrafts = clone(state.processDrafts?.[template.id] ?? {});
  const sceneParts = assemblySceneDraftParts(view, template);
  const request = { key, templateId: template.id, promise: null,
    progress: { phase: "正在准备工艺方案…", completed: 0, total: 0 } };
  const isCurrent = () => state.previewRequest === request
    && view.activeAreaId === "assemblies" && assemblyPreviewKey(view, template) === key;
  const reportProgress = (phase, completed = 0, total = 0) => {
    if (!isCurrent()) return;
    request.progress = { phase, completed, total };
    view.tubeDesignerAssemblyLibraryRenderProject?.();
  };
  request.promise = Promise.resolve().then(async () => {
    if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
    const plan = await context.sceneProxy.invoke("TubeDesigner.ResolveAssemblyTemplatePreview", {
      templateId: template.id,
      parameters: parameterGroups.parameters,
      sceneParameters: parameterGroups.sceneParameters,
      processDrafts,
      sceneParts,
    }, { timeoutMs: 120000 });
    if (plan?.schema !== "icax.assembly-preview-plan" || plan?.templateId !== template.id
        || plan?.designParts?.length !== template.participants.length || !plan?.manufacturingParts?.length) {
      throw new Error("装配模板没有返回覆盖全部逻辑零件的预览计划。");
    }
    if (!isCurrent()) return null;
    const layoutShape = assemblyPresentationShape(template);
    // The independent finished L uses only the resolved product dimensions.
    // Its design members do not need separate native geometry previews.
    const finishedShape = layoutShape === "l" ? buildAssemblyFinishedShapePreview(plan) : null;
    const sectionCache = new Map();
    const designCount = finishedShape ? 0 : plan.designParts.length;
    const total = designCount + plan.manufacturingParts.length;
    const preview = { key, plan, designParts: plan.designParts, manufacturingParts: [],
      layoutShape, finishedShape };
    if (finishedShape && !state.exploded) {
      // Show the finished product while slower native blank processing continues.
      // The same preview object receives the blank results when they arrive.
      state.preview = preview;
    }
    let designParts = plan.designParts;
    if (designCount) {
      reportProgress(`正在生成构件 0/${designCount}`, 0, total);
      designParts = await evaluateAssemblyPreviewParts(context, plan.designParts, "design", sectionCache,
        template.id, (done) => reportProgress(`正在生成构件 ${done}/${designCount}`, done, total), isCurrent);
      if (!designParts) return null;
    }
    preview.designParts = designParts;
    reportProgress(`正在生成下料件 0/${plan.manufacturingParts.length}`, designCount, total);
    const manufacturingParts = await evaluateAssemblyPreviewParts(context, plan.manufacturingParts, "manufacturing", sectionCache, template.id,
      (done) => reportProgress(`正在生成下料件 ${done}/${plan.manufacturingParts.length}`, designCount + done, total), isCurrent);
    if (!manufacturingParts) return null;
    preview.manufacturingParts = manufacturingParts;
    if (plan.formedPreviewMesh || plan.formedPreviewRecipe) {
      try { preview.formedPreview = buildIntegratedFormedPreview(preview); }
      catch (error) { preview.formedPreviewError = error?.message ?? String(error); }
    }
    return preview;
  }).then(async (preview) => {
    if (!preview) {
      if (state.previewRequest === request) state.previewRequest = null;
      return;
    }
    if (state.previewRequest !== request || view.activeAreaId !== "assemblies" || assemblyPreviewKey(view, template) !== key) return;
    reportProgress("正在加载三维画面…");
    state.preview = preview;
    state.previewFailureKey = "";
    await applyAssemblyPreview(context, view, preview, key);
    if (state.previewRequest !== request) return;
    state.previewRequest = null;
    view.tubeDesignerAssemblyLibraryRenderProject?.();
  }).catch((error) => {
    if (state.previewRequest !== request) return;
    state.previewRequest = null;
    state.previewFailureKey = key;
    state.previewError = `装配三维预览失败：${error?.message ?? error}`;
    view.tubeDesignerAssemblyLibraryRenderProject?.();
  });
  state.previewRequest = request;
  return request;
}

function assemblyCard(item, selectedId, values) {
  const blanks = item.manufacturingPlan?.blankParts?.length ?? 0;
  const integrated = item.manufacturingPlan?.realization === "integrated";
  const countLabel = (supportsStockOperations(item) ? "连续母材 · 多处折弯" : integrated
    ? `${item.participants.length} 段逻辑管段 → ${blanks} 件连续母材`
    : `${item.participants.length} 件构件 → ${blanks} 件下料件`)
    + (item.libraryScope === "user" ? " · 我的" : "");
  const choice = item.parameters.find((definition) => definition.valueType === "choice"
    && definition.level !== "advanced" && parameterVisible(definition, values));
  const selected = choice?.options?.find((option) => String(option.value) === String(values[choice.key]));
  const method = selected ? `${choice.displayName}：${selected.label ?? selected.value}` : item.interfaceType;
  return `<button type="button" class="tube-connection-library-card${item.id === selectedId ? " selected" : ""}" data-cam-action="tube-designer-assembly-select" data-tube-assembly-id="${attr(item.id)}" aria-label="${attr(`${item.displayName}，${countLabel}，${method}`)}"><span class="tube-connection-library-card-art">${renderAssemblyCatalogueIllustration(item)}</span><span><strong>${text(item.displayName)}</strong><small>${text(countLabel)}</small></span></button>`;
}

function currentAssemblyWorkflow(view, template) {
  const state = assemblyLibraryState(view);
  const preview = state.preview?.key === assemblyPreviewKey(view, template) ? state.preview : null;
  return preview?.plan?.resolvedWorkflow ?? null;
}

function renderAssemblyWorkflowWarning(view, template) {
  const state = assemblyLibraryState(view);
  const workflow = currentAssemblyWorkflow(view, template);
  const blocked = ["requires-definition", "failed"].includes(workflow?.validationStatus);
  const issues = (workflow?.checks ?? []).filter((item) => ["warning", "warn", "error", "fail", "failed", "invalid", "requires-definition"]
    .includes(String(item.status ?? "").toLowerCase()));
  const warning = issues.length ? String(issues[0].detail ?? issues[0].message ?? "工艺检查有待处理项。")
    : blocked ? "当前方案缺少必要加工，不能用于产品。" : state.previewError || "";
  return warning ? `<p class="tube-assembly-workflow-block" role="alert">${text(warning)}</p>` : "";
}

function sceneProfileDefinitions(profile, values) {
  return (profile?.descriptor?.parameters ?? []).filter((definition) => parameterVisible(definition, values));
}

function sceneProfileLabel(value, fallback) {
  if (typeof value === "string") return value;
  if (value && typeof value === "object") return String(value["zh-CN"] ?? value.zh ?? value.en ?? fallback);
  return String(fallback);
}

function assemblySceneDiagramKey(profile, scene) {
  return JSON.stringify([scene.profileRef, profile.packageDigest ?? profile.revision
    ?? profile.descriptor?.version ?? profile.version ?? "", scene.parameters]);
}

function ensureAssemblySceneProfileDiagram(context, view, template, role) {
  if (typeof context?.sceneProxy?.invoke !== "function") return;
  if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return;
  const scene = assemblySceneParts(view, template)[role];
  if (!scene) return;
  const profile = libraryProfiles(view).find((item) => profileScope(item) === scene.profileRef.scope
    && String(item.id) === String(scene.profileRef.id));
  if (!profile || !sceneProfileDefinitions(profile, scene.parameters).length) return;
  const key = assemblySceneDiagramKey(profile, scene);
  const cache = assemblyLibraryState(view).sceneProfileDiagramCache;
  if (cache.has(key)) return;
  const request = Promise.resolve().then(() => {
    if (!hasLicenseFeature(context, view, resourceEditLicenseFeatures)) return null;
    return context.sceneProxy.invoke("TubeDesigner.EvaluateProfilePackage", {
      profileRef: scene.profileRef, parameters: scene.parameters,
    }, { timeoutMs: 120000 });
  });
  cache.set(key, { request });
  if (cache.size > 48) cache.delete(cache.keys().next().value);
  void request.then((response) => {
    if (!response) { cache.delete(key); return; }
    if (!response?.profile?.contours?.length) throw new Error("管型截面无有效轮廓");
    cache.set(key, { snapshot: response.profile });
    const currentTemplate = selectedAssemblyTemplate(view);
    const currentScene = currentTemplate?.id === template.id ? assemblySceneParts(view, currentTemplate)[role] : null;
    if (currentScene && assemblyLibraryState(view).sceneMemberDisclosure[template.id]?.[role]
        && assemblySceneDiagramKey(profile, currentScene) === key) {
      view.tubeDesignerAssemblyLibraryRenderProject?.();
    }
  }).catch(() => { cache.set(key, { failed: true }); });
}

function renderAssemblySceneProfileDiagram(view, profile, scene) {
  if (!profile) return `<p class="tube-assembly-scene-diagram-message">选择管型后显示截面参数示意图。</p>`;
  const key = assemblySceneDiagramKey(profile, scene);
  const snapshot = assemblyLibraryState(view).sceneProfileDiagramCache.get(key)?.snapshot ?? profileSnapshot(profile);
  if (!snapshot?.contours?.length) return `<p class="tube-assembly-scene-diagram-message">当前管型暂无截面示意图。</p>`;
  const definitions = profile.descriptor?.parameters ?? [];
  const diagram = renderProfileParameterDiagram(snapshot, {
    definitions, parameters: scene.parameters, compact: true, title: "截面参数示意图",
  });
  return `<div class="tube-assembly-scene-diagram">${diagram || renderProfileSvg(snapshot)}<small>截面示意，尺寸以当前输入值为准</small></div>`;
}

function renderAssemblySceneParameters(view, template, values, effects) {
  const sceneDefinitions = template.parameters.filter((item) => item.scope === "scene" && parameterVisible(item, values));
  const basic = sceneDefinitions.filter((item) => item.level !== "advanced");
  const advanced = sceneDefinitions.filter((item) => item.level === "advanced");
  const warnings = assemblySceneParameterWarnings(view, template, values);
  const parts = assemblySceneParts(view, template);
  const profiles = libraryProfiles(view);
  const memberCards = (template.previewScene?.designParts ?? []).map((part, index) => {
    const scene = parts[part.role];
    if (!scene) return "";
    const currentRef = scene.profileRef ?? {};
    const selected = profiles.find((item) => profileScope(item) === currentRef.scope && String(item.id) === String(currentRef.id));
    const selectedKey = selected ? profileSelectionKey(selected) : `${currentRef.scope}:${currentRef.id}`;
    const missing = selected ? "" : `<option value="${attr(selectedKey)}" selected>${text(`当前管型 · ${currentRef.id || "未指定"}`)}</option>`;
    const options = profiles.map((item) => `<option value="${attr(profileSelectionKey(item))}"${profileSelectionKey(item) === selectedKey ? " selected" : ""}>${text(profileName(item))} · ${profileScope(item) === "user" ? "我的" : "系统"}</option>`).join("");
    const data = `data-tube-assembly-id="${attr(template.id)}" data-tube-assembly-scene-role="${attr(part.role)}"`;
    const definitions = sceneProfileDefinitions(selected, scene.parameters);
    const fields = definitions.map((definition) => renderAssemblySceneProfileField(definition, scene.parameters, data)).join("");
    const open = assemblyLibraryState(view).sceneMemberDisclosure[template.id]?.[part.role] === true;
    return `<details class="tube-assembly-scene-member" id="tube-assembly-scene-member-${attr(template.id)}-${attr(part.role)}" data-profile-parameter-scope data-assembly-scene-role="${attr(part.role)}" data-tube-assembly-id="${attr(template.id)}"${open ? " open" : ""}>
      <summary><strong>${text(part.label || assemblySceneMemberLabel(part, index))}</strong><small>${text(selected ? profileName(selected) : "当前管型")} · ${text(scene.length)} mm</small></summary>
      ${renderAssemblySceneProfileDiagram(view, selected, scene)}
      <div class="tube-connection-library-parameter-grid">
      <label class="tube-designer-field"><span>管型</span><select data-cam-change-action="tube-designer-assembly-scene-profile-change" ${data}${profiles.length ? "" : " disabled"}>${missing}${options}</select></label>
      <label class="tube-designer-field"><span>长度（mm）</span><input type="number" min="1" max="100000" step="1" value="${attr(scene.length)}" data-cam-change-action="tube-designer-assembly-scene-length-change" ${data}></label>
      ${fields}
    </div></details>`;
  }).join("");
  const sceneFields = basic.map((item) => parameterField(item, values, template.id, effects?.[item.key], warnings[item.key])).join("");
  const more = advanced.length ? parameterSection("更多成品参数", advanced, values, template.id, true, effects, null, warnings) : "";
  const shapeLabel = template.finishedProductDefinition?.layoutShape === "straight"
    ? "共线形" : template.finishedProductDefinition?.displayName ?? "外形尺寸";
  return `<section class="tube-connection-library-parameter-section basic tube-assembly-scene-section" data-tube-assembly-scene-parameters><header><strong>成品参数</strong><small>${text(shapeLabel)}</small><button type="button" data-cam-action="tube-designer-finished-reset">重置成品</button></header><div class="tube-assembly-scene-members">${memberCards}</div>${sceneFields ? `<div class="tube-connection-library-parameter-grid">${sceneFields}</div>` : ""}${more}</section>`;
}

function assemblySceneMemberLabel(part, index) {
  // Scene slots describe the product layout. Participant labels describe how
  // a process treats those members (insert/receiver, tab/slot, weld, etc.).
  const labels = { armA: "管段 A", armB: "管段 B", main: "主管", branch: "支管",
    through: "横向管", lower: "下方管", upper: "上方管",
    left: "左侧管", right: "右侧管", up: "上侧管", down: "下侧管" };
  return labels[part.sceneSlot] ?? `管件 ${String.fromCharCode(65 + index)}`;
}

function renderAssemblySceneProfileField(definition, values, data) {
  const key = String(definition.key ?? "");
  if (!key) return "";
  const enabled = parameterEnabled(definition, values);
  const value = values[key] ?? definition.defaultValue ?? "";
  const label = text(sceneProfileLabel(definition.displayName ?? definition.name, key));
  const common = `data-cam-change-action="tube-designer-assembly-scene-profile-parameter-change" ${data} data-tube-assembly-scene-profile-parameter="${attr(key)}" data-profile-parameter-key="${attr(key)}"`;
  if (definition.valueType === "boolean") return `<label class="tube-designer-field tube-designer-boolean-field"><span>${label}</span><input type="checkbox" ${common}${value ? " checked" : ""}${enabled ? "" : " disabled"}></label>`;
  if (Array.isArray(definition.options) || Array.isArray(definition.choices)) {
    const options = availableParameterChoices(definition, values).map((option) => {
      const optionValue = typeof option === "object" ? option.value : option;
      const optionLabel = typeof option === "object" ? option.label ?? option.displayName ?? optionValue : option;
      return `<option value="${attr(optionValue)}"${String(optionValue) === String(value) ? " selected" : ""}>${text(sceneProfileLabel(optionLabel, optionValue))}</option>`;
    }).join("");
    return `<label class="tube-designer-field"><span>${label}</span><select ${common}${enabled ? "" : " disabled"}>${options}</select></label>`;
  }
  const type = definition.valueType === "string" ? "text" : "number";
  return `<label class="tube-designer-field"><span>${label}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="${type}" value="${attr(value)}" ${definition.min != null ? `min="${attr(definition.min)}"` : ""}${definition.max != null ? ` max="${attr(definition.max)}"` : ""}${definition.step != null ? ` step="${attr(definition.step)}"` : ""} ${common}${enabled ? "" : " disabled"}></label>`;
}

function parameterSection(title, definitions, values, templateId, advanced, effects = null, extra = null, warnings = null) {
  if (!definitions.length && !extra?.count) return "";
  const fields = definitions.map((item) => parameterField(item, values, templateId, effects?.[item.key], warnings?.[item.key])).join("");
  if (advanced) return `<details class="tube-connection-library-parameter-section"><summary><span>${title}</span><small>${definitions.length + (extra?.count ?? 0)} 项</small></summary>${fields ? `<div class="tube-connection-library-parameter-grid">${fields}</div>` : ""}${extra?.html ?? ""}</details>`;
  return `<section class="tube-connection-library-parameter-section basic"><header><strong>${title}</strong><small>${definitions.length} 项</small></header><div class="tube-connection-library-parameter-grid">${fields}</div></section>`;
}

function parameterField(definition, values, templateId, _effect = null, inlineWarning = "") {
  const enabled = parameterEnabled(definition, values);
  const data = `data-cam-change-action="tube-designer-assembly-parameter-change" data-tube-assembly-id="${attr(templateId)}" data-tube-assembly-parameter="${attr(definition.key)}" data-assembly-parameter-key="${attr(definition.key)}"`;
  const hint = inlineWarning ? `<small class="tube-assembly-parameter-effect" role="status">${text(inlineWarning)}</small>` : "";
  if (definition.valueType === "boolean") return `<label class="tube-connection-library-check tube-designer-field"><input type="checkbox" ${data}${values[definition.key] ? " checked" : ""}${enabled ? "" : " disabled"}><span>${text(definition.displayName)}</span>${hint}</label>`;
  if (definition.valueType === "choice") return `<label class="tube-designer-field"><span>${text(definition.displayName)}</span><select ${data}${enabled ? "" : " disabled"}>${availableParameterChoices(definition, values).map((option) => `<option value="${attr(option.value)}"${String(option.value) === String(values[definition.key]) ? " selected" : ""}>${text(option.label ?? option.value)}</option>`).join("")}</select>${hint}</label>`;
  return `<label class="tube-designer-field"><span>${text(definition.displayName)}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="number" value="${attr(values[definition.key])}" min="${attr(definition.min)}" max="${attr(definition.max)}" step="${attr(definition.step ?? 0.1)}" ${data}${enabled ? "" : " disabled"}>${hint}</label>`;
}

function renderPartProcesses(view, template, values) {
  const processes = template.partProcesses.filter((item) => processApplies(item, values));
  const cards = processes.map((item) => {
    const descriptor = selectedProcessDescriptor(item, values);
    const processValues = processParameterValues(view, template, item, descriptor);
    const effectiveBindings = { ...(item.parameterBindings ?? {}), ...(item.parameterBindingsByResource?.[descriptor?.id] ?? {}) };
    const definitions = [...(descriptor?.parameters ?? []), ...(descriptor?.operationParameters ?? [])]
      .filter((definition) => !definition.derived && !(definition.key in effectiveBindings) && parameterVisible(definition, processValues));
    const editable = item.parameterMode === "inherit-resource" ? definitions : [];
    if (!editable.length) return null;
    const fields = editable.map((definition) => processParameterField(definition, processValues, template.id, item.id, descriptor?.id)).join("");
    return { count: editable.length, html: `<article><header><strong>${text(item.label)}</strong></header><div class="tube-connection-library-parameter-grid">${fields}</div></article>` };
  }).filter(Boolean);
  return {
    count: cards.reduce((total, item) => total + item.count, 0),
    html: cards.length ? `<div class="tube-connection-library-processes">${cards.map((item) => item.html).join("")}</div>` : "",
  };
}

function processApplies(process, values) {
  return !process.appliesWhen || parameterVisible({ visibleWhen: process.appliesWhen }, values);
}

function selectedProcessDescriptor(process, values, explicitId = "") {
  if (!process) return null;
  if (process.resource?.descriptor) return process.resource.descriptor;
  const selectedId = explicitId || String(values?.[process.resourceSelection?.parameter] ?? "");
  return process.resourceSelection?.resources?.find((item) => item.id === selectedId) ?? null;
}

function processParameterValues(view, template, process, descriptor) {
  const defaults = Object.fromEntries([...(descriptor?.parameters ?? []), ...(descriptor?.operationParameters ?? [])]
    .map((item) => [item.key, item.defaultValue]));
  const state = assemblyLibraryState(view);
  const source = state.processInputMode === "stock-operation"
    ? stockOperationSettings(view, template).processDrafts : state.processDrafts?.[template.id];
  const drafts = source?.[process.id]?.[descriptor?.id] ?? {};
  return { ...defaults, ...drafts };
}

function assemblyProcessAutoFillSources(view, template, process) {
  if (assemblyLibraryState(view).processInputMode === "stock-operation") return { ...stockProcessState(view, template).stock.parameters };
  const blank = (template?.previewScene?.manufacturingParts ?? [])
    .find((item) => (item?.processes ?? []).includes(process?.id));
  const source = assemblySceneParts(view, template)[blank?.sourceRole];
  return { ...(source?.parameters ?? {}) };
}

function processParameterField(definition, values, templateId, processId, resourceId) {
  const enabled = parameterEnabled(definition, values);
  const data = `data-cam-change-action="tube-designer-assembly-process-parameter-change" data-tube-assembly-id="${attr(templateId)}" data-tube-assembly-process="${attr(processId)}" data-tube-part-process="${attr(resourceId)}" data-tube-part-process-parameter="${attr(definition.key)}"`;
  if (definition.valueType === "boolean") return `<label class="tube-connection-library-check tube-designer-field"><input type="checkbox" ${data}${values[definition.key] ? " checked" : ""}${enabled ? "" : " disabled"}><span>${text(definition.displayName)}</span></label>`;
  if (Array.isArray(definition.options)) return `<label class="tube-designer-field"><span>${text(definition.displayName)}</span><select ${data}${enabled ? "" : " disabled"}>${availableParameterChoices(definition, values).map((option) => `<option value="${attr(option.value)}"${String(option.value) === String(values[definition.key]) ? " selected" : ""}>${text(option.label ?? option.value)}</option>`).join("")}</select></label>`;
  return `<label class="tube-designer-field"><span>${text(definition.displayName)}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="number" value="${attr(values[definition.key])}" min="${attr(definition.min)}" max="${attr(definition.max)}" step="${attr(definition.step ?? 0.1)}" ${data}${enabled ? "" : " disabled"}></label>`;
}

function assemblySearchText(item) {
  return [item.displayName, assemblyLayoutShapeLabel(item), assemblyPresentationShape(item) === "straight" ? "共线 共线形" : "", item.categoryName, item.summary, item.topology, item.interfaceType, item.lockType,
    ...item.participants.flatMap((participant) => [participant.label, participant.responsibility]),
    ...(item.manufacturingPlan?.blankParts ?? []).map((blank) => blank.label),
    ...item.assemblyPath.flatMap((step) => [step.label, step.kind]),
    ...item.partProcesses.flatMap((process) => [process.label, process.resource?.id, ...(process.resourceSelection?.options ?? [])])].join(" ").toLocaleLowerCase("zh-CN");
}

function text(value) { return String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;"); }
function attr(value) { return text(value).replaceAll('"', "&quot;"); }
