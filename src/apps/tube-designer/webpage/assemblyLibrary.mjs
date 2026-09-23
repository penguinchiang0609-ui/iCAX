import { availableParameterChoices, parameterEnabled, parameterVisible } from "./parameterConditions.mjs";
import { parameterAutoFillPatch } from "./parameterAutoFill.mjs";
import { renderToolParameterDiagram } from "./toolParameterDiagram.mjs";
import { bindParameterDiagramScopes } from "./parameterDiagramBinding.mjs";
import { libraryDiagramPositionStyle, renderDiagramResizeHandles } from "./floatingParameterDiagram.mjs";
import { createThreeViewport } from "../../../iCAX-UI/SDK/Viewport/threeViewport.mjs";
import {
  assemblyCategoryOrder,
  assemblyParameterDefaults,
  assemblyTemplateById,
  normalizeAssemblyCatalogue,
} from "./assemblyCatalog.mjs";

export function assemblyLibraryState(view) {
  const state = view.tubeDesignerAssemblyLibrary ??= {};
  state.search ??= "";
  state.selectedId ??= "";
  state.collapsed ??= [];
  state.parameterDrafts ??= {};
  state.processDrafts ??= {};
  state.catalogueStatus ??= "idle";
  state.catalogueError ??= "";
  state.cataloguePromise ??= null;
  state.preview ??= null;
  state.previewRequest ??= null;
  state.previewFailureKey ??= "";
  state.previewError ??= "";
  state.appliedKey ??= "";
  state.projectionMode ??= "perspective";
  state.exploded ??= false;
  state.showDiagram ??= false;
  // The assembly scene is the source of truth: always show the whole result.
  // Part-isolation controls duplicated that information and made the workflow
  // look like a relation inspector instead of an assembly editor.
  state.focusRole = "";
  const templates = assemblyTemplates(view);
  if (!assemblyTemplateById(templates, state.selectedId)) state.selectedId = templates[0]?.id ?? "";
  return state;
}

export function assemblyTemplates(view) {
  return normalizeAssemblyCatalogue(view?.tubeDesignerAssemblyTemplates);
}

export function selectedAssemblyTemplate(view) {
  const templates = assemblyTemplates(view);
  return assemblyTemplateById(templates, assemblyLibraryState(view).selectedId) ?? templates[0];
}

export function assemblyParameterValues(view, template = selectedAssemblyTemplate(view)) {
  return { ...assemblyParameterDefaults(template), ...(assemblyLibraryState(view).parameterDrafts[template?.id] ?? {}) };
}

export function ensureAssemblyLibraryCatalogue(context, view, ops) {
  const state = assemblyLibraryState(view);
  if (state.cataloguePromise) return state.cataloguePromise;
  if (state.catalogueStatus === "ready" || typeof context?.sceneProxy?.invoke !== "function") return Promise.resolve();
  state.catalogueStatus = "loading";
  state.catalogueError = "";
  const request = context.sceneProxy.invoke("TubeDesigner.GetAssemblyTemplates", {}, { timeoutMs: 30000 })
    .then((response) => {
      const templates = normalizeAssemblyCatalogue(response?.assemblies);
      if (!templates.length) {
        const details = Array.isArray(response?.errors) ? response.errors.filter(Boolean).join("；") : "";
        throw new Error(details || "装配模板目录为空，请检查模板资源是否完整。");
      }
      view.tubeDesignerAssemblyTemplates = templates;
      state.catalogueStatus = "ready";
      state.catalogueError = "";
      if (!assemblyTemplateById(templates, state.selectedId)) state.selectedId = templates[0].id;
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
  const search = state.search.trim().toLocaleLowerCase("zh-CN");
  const visible = templates.filter((item) => !search || assemblySearchText(item).includes(search));
  const groups = assemblyCategoryOrder(templates).map(([id, label]) => {
    const items = visible.filter((item) => item.category === id);
    if (!items.length) return "";
    const collapsed = state.collapsed.includes(id);
    return `<section class="tube-connection-library-group"><button type="button" class="tube-connection-library-group-heading" data-cam-action="tube-designer-assembly-toggle-category" data-tube-assembly-category="${attr(id)}" aria-expanded="${!collapsed}"><span>${collapsed ? "▸" : "▾"} ${text(label)}</span><small>${items.length}</small></button><div class="tube-connection-library-cards"${collapsed ? " hidden" : ""}>${items.map((item) => assemblyCard(item, state.selectedId)).join("")}</div></section>`;
  }).join("");
  const body = state.catalogueStatus === "loading" && !templates.length
    ? `<div class="tube-profile-library-empty"><strong>正在读取装配模板</strong><span>正在解析逻辑零件与下料归并方案。</span></div>`
    : state.catalogueError && !templates.length
      ? `<div class="tube-profile-library-empty"><strong>装配模板读取失败</strong><span>${text(state.catalogueError)}</span><button type="button" data-cam-action="tube-designer-assembly-retry">重新读取</button></div>`
      : groups || `<div class="tube-profile-library-empty"><strong>没有匹配的装配模板</strong><span>换一个关键词试试</span></div>`;
  return `<section class="tube-profile-library-panel tube-connection-library-panel"><header class="tube-profile-library-heading"><div><strong>装配工艺</strong><span>${templates.length} 种 · 按零件数量与连接位置分类</span></div></header><label class="tube-connection-library-search"><span class="sr-only">搜索装配工艺</span><input type="search" value="${attr(state.search)}" placeholder="搜索装配工艺" data-cam-change-action="tube-designer-assembly-search"></label><div class="tube-connection-library-list">${body}</div></section>`;
}

export function renderAssemblyLibraryRightPane(_context, view) {
  const template = selectedAssemblyTemplate(view);
  if (!template) return `<div class="tube-profile-library-editor-empty"><strong>暂无装配模板</strong><span>装配模板决定多个逻辑零件如何归并为下料零件。</span></div>`;
  const values = assemblyParameterValues(view, template);
  const state = assemblyLibraryState(view);
  const basic = template.parameters.filter((item) => item.level !== "advanced" && parameterVisible(item, values));
  const advanced = template.parameters.filter((item) => item.level === "advanced" && parameterVisible(item, values));
  return `<section class="tube-connection-library-editor" data-assembly-parameter-scope data-parameter-diagram-owner="assembly-library:${attr(template.id)}"><header class="tube-connection-library-editor-heading"><div><strong>${text(template.displayName)}</strong><span>修改参数后自动更新成品与炸开图</span></div><div class="tube-connection-library-heading-actions"><button type="button" data-cam-action="tube-designer-assembly-toggle-diagram" aria-expanded="${state.showDiagram}">示意图</button><button type="button" data-cam-action="tube-designer-assembly-reset" data-tube-assembly-id="${attr(template.id)}">重置</button></div></header><div class="tube-connection-library-editor-body">${parameterSection("装配参数", basic, values, template.id, false)}${renderPartProcesses(view, template, values)}${advanced.length ? parameterSection("高级设置", advanced, values, template.id, true) : ""}</div></section>`;
}

export function renderAssemblyLibraryViewportOverlay(context, view) {
  const template = selectedAssemblyTemplate(view);
  if (!template) return "";
  const values = assemblyParameterValues(view, template);
  const state = assemblyLibraryState(view);
  ensureAssemblyLibraryPreview(context, view, template);
  const status = state.previewRequest ? "正在更新装配…" : state.previewError ? state.previewError
    : state.preview ? "" : "正在准备三维预览…";
  const hud = status ? `<div class="tube-connection-library-hud${state.previewError ? " error" : ""}" aria-live="polite"><i aria-hidden="true"></i><span>${text(status)}</span>${state.previewError ? `<button type="button" data-cam-action="tube-designer-assembly-retry-preview">重新生成</button>` : ""}</div>` : "";
  return `${renderAssemblyLibraryDiagramDock(view, template, values)}${renderAssemblyViewportStage(template, state)}${hud}`;
}

function renderAssemblyViewportStage(template, state) {
  const exploded = !!state.exploded;
  return `<div class="tube-assembly-preview" data-tube-assembly-preview><section class="tube-assembly-preview-pane scene" data-tube-assembly-preview-pane="scene">
    <header><div><strong>${text(template.displayName)}</strong><span>${exploded ? "炸开：查看各下料件及加工形状" : "成品：查看最终装配关系"}</span></div><div class="tube-assembly-preview-toolbar">
      <div class="tube-assembly-view-switch" role="group" aria-label="装配视图">
        <button type="button" class="tube-assembly-view-mode${exploded ? "" : " active"}" data-cam-action="tube-designer-assembly-set-view" data-tube-assembly-view="finished" aria-pressed="${!exploded}">成品</button>
        <button type="button" class="tube-assembly-view-mode${exploded ? " active" : ""}" data-cam-action="tube-designer-assembly-set-view" data-tube-assembly-view="exploded" aria-pressed="${exploded}">炸开</button>
      </div>
      <div class="tube-assembly-preview-camera" role="group" aria-label="三维视角">${[["iso", "等轴"], ["front", "前视"], ["top", "顶视"], ["fit", "适合"]].map(([camera, label]) => `<button type="button" data-cam-action="tube-designer-assembly-camera" data-tube-assembly-camera="${camera}">${label}</button>`).join("")}</div>
    </div></header>
    <div class="tube-assembly-preview-host" data-tube-assembly-scene-viewport></div>
  </section></div>`;
}

function renderAssemblyLibraryDiagramDock(view, template, values) {
  const state = assemblyLibraryState(view);
  if (!state.showDiagram || !template?.parameterDiagram) return "";
  const definitions = template.parameters.filter((item) => parameterVisible(item, values));
  return `<aside class="tube-library-diagram-dock" data-tube-assembly-diagram-dock data-library-floating-diagram="assemblies" style="${libraryDiagramPositionStyle(view, "assemblies")}">
    ${renderDiagramResizeHandles()}
    <header class="tube-library-diagram-drag" data-floating-diagram-drag><strong>${text(template.displayName)} · 参数示意图</strong><button type="button" data-library-diagram-close data-cam-action="tube-designer-assembly-close-diagram" aria-label="关闭示意图">×</button></header>
    <div class="tube-library-diagram-content" data-parameter-diagram-for="assembly-library:${attr(template.id)}">${renderToolParameterDiagram({ tool: template, values, definitions, expanded: true, showToggle: false, annotationKind: "assembly" })}</div>
  </aside>`;
}

export function bindAssemblyParameterDiagrams(mount) {
  bindParameterDiagramScopes(mount, "assembly");
}

export async function handleAssemblyLibraryAction(context, view, action, target, ops) {
  const state = assemblyLibraryState(view);
  if (action === "tube-designer-assembly-retry") {
    state.catalogueStatus = "idle";
    state.catalogueError = "";
    await ensureAssemblyLibraryCatalogue(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-search") {
    state.search = String(target?.value ?? "");
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-toggle-category") {
    const id = String(target?.dataset?.tubeAssemblyCategory ?? "");
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
  if (action === "tube-designer-assembly-camera") {
    const camera = String(target?.dataset?.tubeAssemblyCamera ?? "");
    controlAssemblyLibraryCamera(view, camera);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-set-view") {
    const exploded = String(target?.dataset?.tubeAssemblyView ?? "finished") === "exploded";
    if (state.exploded !== exploded) {
      state.exploded = exploded;
      state.appliedKey = "";
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-assembly-retry-preview") {
    state.previewFailureKey = "";
    state.previewError = "";
    state.preview = null;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-select") {
    const id = String(target?.dataset?.tubeAssemblyId ?? "");
    if (id !== state.selectedId && assemblyTemplateById(assemblyTemplates(view), id)) {
      state.selectedId = id;
      state.exploded = false;
      invalidateAssemblyPreview(view, true);
    }
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-assembly-parameter-change") {
    const template = assemblyTemplateById(assemblyTemplates(view), String(target?.dataset?.tubeAssemblyId ?? ""));
    const key = String(target?.dataset?.tubeAssemblyParameter ?? "");
    const definition = template?.parameters.find((item) => item.key === key);
    if (template && definition) {
      const value = definition.valueType === "boolean" ? !!target.checked
        : definition.valueType === "choice" ? String(target.value ?? "") : Number(target.value);
      if (!['number', 'integer'].includes(definition.valueType) || Number.isFinite(value)) {
        state.parameterDrafts[template.id] = { ...(state.parameterDrafts[template.id] ?? {}), [key]: value };
        invalidateAssemblyPreview(view);
        ops.renderProject(context, view);
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
        const templateDrafts = state.processDrafts[templateId] ??= {};
        const processDrafts = templateDrafts[processId] ??= {};
        const current = processParameterValues(view, template, process, descriptor);
        const sources = assemblyProcessAutoFillSources(template, process);
        processDrafts[descriptor.id] = {
          ...(processDrafts[descriptor.id] ?? {}),
          ...parameterAutoFillPatch(definitions, current, key, value, sources),
        };
        invalidateAssemblyPreview(view);
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-assembly-reset") {
    const id = String(target?.dataset?.tubeAssemblyId ?? state.selectedId);
    delete state.parameterDrafts[id];
    delete state.processDrafts[id];
    invalidateAssemblyPreview(view);
    ops.renderProject(context, view);
    return { handled: true };
  }
  return { handled: false };
}

function invalidateAssemblyPreview(view, clearScene = false) {
  const state = assemblyLibraryState(view);
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
  return JSON.stringify([
    template.id,
    template.version,
    assemblyParameterValues(view, template),
    state.processDrafts?.[template.id] ?? {},
  ]);
}

function clone(value) {
  return typeof structuredClone === "function" ? structuredClone(value) : JSON.parse(JSON.stringify(value));
}

async function resolveAssemblyRequestSections(context, request, cache) {
  const resolved = clone(request);
  const sections = [];
  for (const end of Object.values(resolved.ends ?? {})) if (end?.section) sections.push(end.section);
  for (const feature of resolved.features ?? []) if (feature?.section) sections.push(feature.section);
  for (const section of sections) {
    if (section.profile || !section.profileRef) continue;
    const key = JSON.stringify([section.profileRef, section.parameters ?? {}]);
    let profile = cache.get(key);
    if (!profile) {
      const response = await context.sceneProxy.invoke("TubeDesigner.EvaluateProfilePackage", {
        profileRef: section.profileRef,
        parameters: section.parameters ?? {},
      }, { timeoutMs: 120000 });
      profile = response?.profile;
      if (!profile?.contours?.length) throw new Error("装配工艺需要的目标截面没有生成有效轮廓。");
      cache.set(key, profile);
    }
    section.profile = profile;
  }
  return resolved;
}

async function evaluateAssemblyPreviewParts(context, parts, kind, sectionCache) {
  const result = [];
  for (const part of parts) {
    const request = await resolveAssemblyRequestSections(context, part.request, sectionCache);
    const response = await context.sceneProxy.invoke("TubeDesigner.PreviewPunchWizard", request, { timeoutMs: 180000 });
    if (!response?.baseGeometry?.url) throw new Error(`${part.label}没有返回原始零件几何。`);
    if (kind === "manufacturing") {
      const detail = String(response?.resultError ?? "").trim();
      if (detail || response?.previewComputed === false || !response?.geometry?.url) {
        throw new Error(`${part.label}下料形状生成失败${detail ? `：${detail}` : "。"}`);
      }
    }
    result.push({ ...part, response });
  }
  return result;
}

export function assemblyFinishedRows(preview) {
  const designParts = preview?.designParts ?? [];
  const manufacturedByRole = new Map();
  for (const part of preview?.manufacturingParts ?? []) {
    const roles = Array.isArray(part.participantRoles) && part.participantRoles.length
      ? part.participantRoles : part.sourceRole ? [part.sourceRole] : [];
    if (roles.length !== 1) continue;
    manufacturedByRole.set(roles[0], part);
  }
  return designParts.map((part) => {
    const manufactured = manufacturedByRole.get(part.role);
    return {
      entityId: `assembly-finished:${part.id}`,
      roles: part.role ? [part.role] : [],
      data: {
        // Separate parts are shown with their real end/side processing so the
        // finished scene closes at the interface.  A merged blank represents
        // several roles and deliberately falls back to the logical geometry.
        geometry: manufactured?.response.geometry ?? part.response.baseGeometry,
        material: manufactured?.response.material ?? part.response.baseMaterial,
        geometryKind: 1,
        renderClass: 1,
        visible: true,
        selectable: false,
        localToWorldMatrix: part.matrix,
      },
    };
  });
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
  return exploded ? assemblyBlankRows(preview) : assemblyFinishedRows(preview);
}

const assemblyViewportControllers = new WeakMap();

function createAssemblyViewport(state, viewportFactory) {
  const viewport = viewportFactory({
    backgroundColor: 0x13252d,
    continuousRender: false,
    constrainOrbit: false,
    projectionMode: state.projectionMode,
    showProjectionToggle: true,
    pickingEnabled: false,
    antialias: true,
    pixelRatioCap: 2,
    onProjectionChange(mode) { state.projectionMode = mode; },
  });
  return { viewport, host: null, ready: false, appliedKey: "", templateId: "", mode: "", rows: [] };
}

function mountAssemblyViewportPane(pane, host) {
  if (pane.host === host && pane.viewport.root?.parentElement === host) return;
  pane.host = host;
  pane.viewport.mount(host);
}

export function attachAssemblyLibraryViewports(context, view, mount, options = {}) {
  if (!view || view.activeAreaId !== "assemblies") {
    disposeAssemblyLibraryViewports(view);
    return null;
  }
  const sceneHost = mount?.querySelector?.("[data-tube-assembly-scene-viewport]");
  if (!sceneHost) return null;
  const state = assemblyLibraryState(view);
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
  if (state.preview && (state.appliedKey !== assemblyAppliedKey(state, state.preview.key) || !controller.scene.ready)) {
    void applyAssemblyPreview(context, view, state.preview, state.preview.key);
  } else {
    refreshAssemblyVisibility(view);
  }
  return controller;
}

export function disposeAssemblyLibraryViewports(view) {
  if (!view || (typeof view !== "object" && typeof view !== "function")) return;
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return;
  controller.generation += 1;
  controller.scene.viewport.dispose?.();
  assemblyViewportControllers.delete(view);
}

function clearAssemblyLibraryViewports(view) {
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return;
  controller.generation += 1;
  const pane = controller.scene;
  pane.viewport.setVisibleEntityIds?.([]);
  pane.ready = false;
  pane.appliedKey = "";
  pane.templateId = "";
  pane.mode = "";
  pane.rows = [];
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

async function applyAssemblyPreviewPane(pane, rows, resources, revision, templateId, mode, focusRole) {
  if (!rows.length) throw new Error("装配模板没有生成可显示的三维零件。");
  const previousCamera = pane.ready ? pane.viewport.getCameraState?.() : null;
  const modeChanged = pane.ready && pane.mode !== mode;
  const receipt = await pane.viewport.applyViewSnapshot({ revision, rows }, resources);
  if (!receipt?.applied || receipt.missingGeometryEntityIds?.length || !receipt.entityIds?.length) {
    throw new Error("装配预览几何未完整进入三维场景。");
  }
  pane.rows = rows;
  pane.viewport.setVisibleEntityIds?.(visibleAssemblyEntityIds(rows, focusRole));
  if (!pane.ready || pane.templateId !== templateId) {
    pane.viewport.setStandardView?.("iso");
    pane.viewport.fitViewToViewport?.(1.18);
  } else if (previousCamera) {
    pane.viewport.setCameraState?.(previousCamera);
    if (modeChanged) pane.viewport.fitViewToViewport?.(1.18);
  }
  pane.ready = true;
  pane.appliedKey = revision;
  pane.templateId = templateId;
  pane.mode = mode;
}

function assemblyAppliedKey(state, key) {
  return `${state.exploded ? "exploded" : "finished"}:${key}`;
}

async function applyAssemblyPreview(context, view, preview, key) {
  if (view.activeAreaId !== "assemblies" || assemblyPreviewKey(view) !== key || assemblyLibraryState(view).preview !== preview) return false;
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return false;
  const generation = ++controller.generation;
  const resources = context.sceneProxy?.resources ?? view.sceneProxy?.resources ?? context.projectProxy?.resources;
  const templateId = String(preview.plan?.templateId ?? "");
  const state = assemblyLibraryState(view);
  const mode = state.exploded ? "exploded" : "finished";
  const appliedKey = assemblyAppliedKey(state, key);
  await applyAssemblyPreviewPane(controller.scene, assemblySceneRows(preview, state.exploded), resources,
    `assembly-${appliedKey}`, templateId, mode, state.focusRole);
  if (controller.generation !== generation || view.activeAreaId !== "assemblies"
      || assemblyPreviewKey(view) !== key || assemblyLibraryState(view).preview !== preview
      || assemblyAppliedKey(assemblyLibraryState(view), key) !== appliedKey) return false;
  assemblyLibraryState(view).appliedKey = appliedKey;
  return true;
}

async function applyCurrentAssemblyPreview(context, view) {
  const state = assemblyLibraryState(view);
  if (!state.preview) return false;
  try {
    return await applyAssemblyPreview(context, view, state.preview, state.preview.key);
  } catch (error) {
    state.previewError = `装配预览显示失败：${error?.message ?? error}`;
    view.tubeDesignerAssemblyLibraryRenderProject?.();
    return false;
  }
}

export function ensureAssemblyLibraryPreview(context, view, template = selectedAssemblyTemplate(view)) {
  if (view.activeAreaId !== "assemblies" || !template || typeof context?.sceneProxy?.invoke !== "function") return null;
  const state = assemblyLibraryState(view);
  const key = assemblyPreviewKey(view, template);
  if (state.preview?.key === key) {
    if (state.appliedKey !== assemblyAppliedKey(state, key)) void applyCurrentAssemblyPreview(context, view);
    return state.preview;
  }
  if (state.previewRequest?.key === key || state.previewFailureKey === key) return state.previewRequest;
  state.previewError = "";
  const request = { key, templateId: template.id, promise: null };
  request.promise = Promise.resolve().then(async () => {
    const plan = await context.sceneProxy.invoke("TubeDesigner.ResolveAssemblyTemplatePreview", {
      templateId: template.id,
      parameters: assemblyParameterValues(view, template),
      processDrafts: state.processDrafts?.[template.id] ?? {},
    }, { timeoutMs: 120000 });
    if (plan?.schema !== "icax.assembly-preview-plan" || plan?.templateId !== template.id
        || plan?.designParts?.length !== template.participants.length || !plan?.manufacturingParts?.length) {
      throw new Error("装配模板没有返回覆盖全部逻辑零件的预览计划。");
    }
    if (state.previewRequest !== request || view.activeAreaId !== "assemblies" || assemblyPreviewKey(view, template) !== key) return null;
    const sectionCache = new Map();
    const designParts = await evaluateAssemblyPreviewParts(context, plan.designParts, "design", sectionCache);
    const manufacturingParts = await evaluateAssemblyPreviewParts(context, plan.manufacturingParts, "manufacturing", sectionCache);
    return { key, plan, designParts, manufacturingParts };
  }).then(async (preview) => {
    if (!preview || state.previewRequest !== request || view.activeAreaId !== "assemblies" || assemblyPreviewKey(view, template) !== key) return;
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

function assemblyCard(item, selectedId) {
  const blanks = item.manufacturingPlan?.blankParts?.length ?? 0;
  const partLabel = `${item.participants.length} 件`;
  const blankLabel = item.participants.length === blanks ? "分别下料" : blanks === 1 ? "一体下料" : `${blanks} 个下料件`;
  return `<button type="button" class="tube-connection-library-card${item.id === selectedId ? " selected" : ""}" data-cam-action="tube-designer-assembly-select" data-tube-assembly-id="${attr(item.id)}" aria-label="${attr(`${item.displayName}，${partLabel}，${blankLabel}`)}"><span class="tube-connection-library-card-art">${assemblyIllustration(item)}</span><span><strong>${text(item.displayName)}</strong><small>${text(partLabel)} · ${text(blankLabel)}</small></span></button>`;
}

function parameterSection(title, definitions, values, templateId, advanced) {
  if (!definitions.length) return "";
  const fields = definitions.map((item) => parameterField(item, values, templateId)).join("");
  if (advanced) return `<details class="tube-connection-library-parameter-section"><summary><span>${title}</span><small>${definitions.length} 项</small></summary><div class="tube-connection-library-parameter-grid">${fields}</div></details>`;
  return `<section class="tube-connection-library-parameter-section basic"><header><strong>${title}</strong><small>${definitions.length} 项</small></header><div class="tube-connection-library-parameter-grid">${fields}</div></section>`;
}

function parameterField(definition, values, templateId) {
  const enabled = parameterEnabled(definition, values);
  const data = `data-cam-change-action="tube-designer-assembly-parameter-change" data-tube-assembly-id="${attr(templateId)}" data-tube-assembly-parameter="${attr(definition.key)}" data-assembly-parameter-key="${attr(definition.key)}"`;
  if (definition.valueType === "boolean") return `<label class="tube-connection-library-check"><input type="checkbox" ${data}${values[definition.key] ? " checked" : ""}${enabled ? "" : " disabled"}><span>${text(definition.displayName)}</span></label>`;
  if (definition.valueType === "choice") return `<label><span>${text(definition.displayName)}</span><select ${data}${enabled ? "" : " disabled"}>${availableParameterChoices(definition, values).map((option) => `<option value="${attr(option.value)}"${String(option.value) === String(values[definition.key]) ? " selected" : ""}>${text(option.label ?? option.value)}</option>`).join("")}</select></label>`;
  return `<label><span>${text(definition.displayName)}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="number" value="${attr(values[definition.key])}" min="${attr(definition.min)}" max="${attr(definition.max)}" step="${attr(definition.step ?? 0.1)}" ${data}${enabled ? "" : " disabled"}></label>`;
}

function renderPartProcesses(view, template, values) {
  const processes = template.partProcesses.filter((item) => processApplies(item, values));
  if (!processes.length) return "";
  const cards = processes.map((item) => {
    const descriptor = selectedProcessDescriptor(item, values);
    const processValues = processParameterValues(view, template, item, descriptor);
    const effectiveBindings = { ...(item.parameterBindings ?? {}), ...(item.parameterBindingsByResource?.[descriptor?.id] ?? {}) };
    const definitions = [...(descriptor?.parameters ?? []), ...(descriptor?.operationParameters ?? [])]
      .filter((definition) => !definition.derived && !(definition.key in effectiveBindings) && parameterVisible(definition, processValues));
    const editable = item.parameterMode === "inherit-resource" ? definitions : [];
    const basic = editable.filter((definition) => definition.level !== "advanced");
    const advanced = editable.filter((definition) => definition.level === "advanced");
    const roleNames = template.participants.filter((participant) => item.participants?.includes(participant.role)).map((participant) => participant.label).join(" + ");
    const heading = `<header><i>${text(roleNames || "装配件")}</i><span><strong>${text(item.label)}</strong><small>${text(descriptor?.displayName ?? item.label)}</small></span></header>`;
    const basicFields = basic.length
      ? `<div class="tube-connection-library-parameter-grid">${basic.map((definition) => processParameterField(definition, processValues, template.id, item.id, descriptor?.id)).join("")}</div>` : "";
    const advancedFields = advanced.length
      ? `<details class="tube-connection-library-process-advanced"><summary><span>高级设置</span><small>${advanced.length} 项</small></summary><div class="tube-connection-library-parameter-grid">${advanced.map((definition) => processParameterField(definition, processValues, template.id, item.id, descriptor?.id)).join("")}</div></details>` : "";
    return { html: `<article>${heading}${basicFields}${advancedFields}</article>` };
  });
  return `<details class="tube-connection-library-parameter-section tube-connection-library-process-section"><summary><span>加工设置</span><small>${cards.length} 道</small></summary><div class="tube-connection-library-processes">${cards.map((item) => item.html).join("")}</div></details>`;
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
  const drafts = assemblyLibraryState(view).processDrafts?.[template.id]?.[process.id]?.[descriptor?.id] ?? {};
  return { ...defaults, ...drafts };
}

function assemblyProcessAutoFillSources(template, process) {
  const blank = (template?.previewScene?.manufacturingParts ?? [])
    .find((item) => (item?.processes ?? []).includes(process?.id));
  const source = (template?.previewScene?.designParts ?? [])
    .find((item) => item?.role === blank?.sourceRole);
  return { ...(source?.parameters ?? {}) };
}

function processParameterField(definition, values, templateId, processId, resourceId) {
  const enabled = parameterEnabled(definition, values);
  const data = `data-cam-change-action="tube-designer-assembly-process-parameter-change" data-tube-assembly-id="${attr(templateId)}" data-tube-assembly-process="${attr(processId)}" data-tube-part-process="${attr(resourceId)}" data-tube-part-process-parameter="${attr(definition.key)}"`;
  if (definition.valueType === "boolean") return `<label class="tube-connection-library-check"><input type="checkbox" ${data}${values[definition.key] ? " checked" : ""}${enabled ? "" : " disabled"}><span>${text(definition.displayName)}</span></label>`;
  if (Array.isArray(definition.options)) return `<label><span>${text(definition.displayName)}</span><select ${data}${enabled ? "" : " disabled"}>${availableParameterChoices(definition, values).map((option) => `<option value="${attr(option.value)}"${String(option.value) === String(values[definition.key]) ? " selected" : ""}>${text(option.label ?? option.value)}</option>`).join("")}</select></label>`;
  return `<label><span>${text(definition.displayName)}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="number" value="${attr(values[definition.key])}" min="${attr(definition.min)}" max="${attr(definition.max)}" step="${attr(definition.step ?? 0.1)}" ${data}${enabled ? "" : " disabled"}></label>`;
}

function assemblyIllustration(template) {
  const paths = {
    bend: '<path d="M8 35h18V13h14M23 35a3 3 0 0 0 3-3"/>',
    "tab-slot-lock": '<path d="M5 14h17v6h6a4 4 0 0 1 0 8h-6v6H5M43 14H32v6h-4a4 4 0 0 0 0 8h4v6h11"/>',
    "through-bolt": '<path d="M6 16h36v8H6zm0 12h36v8H6zM18 12v28m12-28v28"/><circle cx="18" cy="26" r="3"/><circle cx="30" cy="26" r="3"/>',
    "slot-bolt-adjustable": '<path d="M6 14h36v20H6zM14 24h18"/><circle cx="34" cy="24" r="4"/>',
    "saddle-weld": '<path d="M6 31h36M24 31V8M17 31a7 7 0 0 1 14 0"/>',
    "insert-sleeve": '<path d="M5 18h24v12H5M43 14H27v20h16M15 24h22"/>',
    "mechanical-fastener": '<path d="M6 15h36v7H6zm0 11h36v7H6zM17 10v28m14-28v28"/><circle cx="17" cy="24" r="3"/><circle cx="31" cy="24" r="3"/>',
    "weld-interface": '<path d="M5 31h38M24 31V7M16 31l3-4 3 4 3-4 3 4 3-4 3 4"/>',
    "two-end-end-angle": '<path d="M5 34h18V14M43 34H25V14M23 14l2 2 2-2"/>',
    "two-end-middle": '<path d="M5 28h38M24 28V7M18 28a6 6 0 0 1 12 0"/>',
    "three-end-end-end": '<path d="M5 34h38M24 34V7M20 30l4 4 4-4"/>',
    "three-end-end-middle": '<path d="M5 29h38M16 29V8M32 29V8M13 29a3 3 0 0 1 6 0m10 0a3 3 0 0 1 6 0"/>',
    "four-end-end-end-end": '<path d="M5 24h38M24 5v38M19 24l5-5 5 5-5 5z"/>',
  };
  return `<svg viewBox="0 0 48 48">${paths[template.id] ?? '<path d="M7 13h16v12H7zm18 10h16v12H25M23 19l4 4-4 4"/>'}</svg>`;
}

function assemblySearchText(item) {
  return [item.displayName, item.categoryName, item.summary, item.topology, item.interfaceType, item.lockType,
    ...item.participants.flatMap((participant) => [participant.label, participant.responsibility]),
    ...(item.manufacturingPlan?.blankParts ?? []).map((blank) => blank.label),
    ...item.assemblyPath.flatMap((step) => [step.label, step.kind]),
    ...item.partProcesses.flatMap((process) => [process.label, process.resource?.id, ...(process.resourceSelection?.options ?? [])])].join(" ").toLocaleLowerCase("zh-CN");
}

function text(value) { return String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;"); }
function attr(value) { return text(value).replaceAll('"', "&quot;"); }
