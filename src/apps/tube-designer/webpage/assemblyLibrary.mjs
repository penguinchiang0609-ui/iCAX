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
  state.projectionModes ??= { finished: "perspective", blank: "orthographic" };
  state.showDiagram ??= true;
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
  return `<section class="tube-profile-library-panel tube-connection-library-panel"><header class="tube-profile-library-heading"><div><strong>装配库</strong><span>${templates.length} 个装配模板 · 定义逻辑零件如何形成下料零件</span></div></header><label class="tube-connection-library-search"><span class="sr-only">搜索装配</span><input type="search" value="${attr(state.search)}" placeholder="搜索装配、下料归并、连接方式" data-cam-change-action="tube-designer-assembly-search"></label><div class="tube-connection-library-list">${body}</div></section>`;
}

export function renderAssemblyLibraryRightPane(_context, view) {
  const template = selectedAssemblyTemplate(view);
  if (!template) return `<div class="tube-profile-library-editor-empty"><strong>暂无装配模板</strong><span>装配模板决定多个逻辑零件如何归并为下料零件。</span></div>`;
  const values = assemblyParameterValues(view, template);
  const state = assemblyLibraryState(view);
  const basic = template.parameters.filter((item) => item.level !== "advanced" && parameterVisible(item, values));
  const advanced = template.parameters.filter((item) => item.level === "advanced" && parameterVisible(item, values));
  return `<section class="tube-connection-library-editor" data-assembly-parameter-scope data-parameter-diagram-owner="assembly-library:${attr(template.id)}"><header class="tube-connection-library-editor-heading"><div><strong>${text(template.displayName)}</strong><span>${text(template.categoryName)} · ${text(template.topology)}</span></div><div class="tube-connection-library-heading-actions"><button type="button" data-cam-action="tube-designer-assembly-toggle-diagram" aria-expanded="${state.showDiagram}">参数示意图</button><button type="button" data-cam-action="tube-designer-assembly-reset" data-tube-assembly-id="${attr(template.id)}">恢复默认</button><b>${realizationLabel(template)}</b></div></header><div class="tube-connection-library-editor-body">${parameterSection("装配参数", basic, values, template.id, false)}${renderPartProcesses(view, template, values)}${advanced.length ? parameterSection("高级设置", advanced, values, template.id, true) : ""}</div></section>`;
}

export function renderAssemblyLibraryViewportOverlay(context, view) {
  const template = selectedAssemblyTemplate(view);
  if (!template) return "";
  const values = assemblyParameterValues(view, template);
  const state = assemblyLibraryState(view);
  const activeProcesses = template.partProcesses.filter((item) => processApplies(item, values));
  ensureAssemblyLibraryPreview(context, view, template);
  const status = state.previewRequest ? "正在根据参数重新生成…" : state.previewError ? state.previewError
    : state.preview ? "左侧显示装配成品，右侧显示对应下料零件。两个视角可分别旋转、缩放和切换投影。" : "正在准备装配三维预览…";
  return `${renderAssemblyLibraryDiagramDock(view, template, values)}${renderAssemblyDualViewportStage()}<div class="tube-connection-library-hud"><strong>${text(template.displayName)}</strong><span>${text(template.participants.length)} 个逻辑零件 → ${text(template.outputs?.manufacturingPartCount)} 个下料零件</span><small>${text(realizationLabel(template))} · ${activeProcesses.length} 道单件工艺</small><p class="${state.previewError ? "error" : ""}" aria-live="polite">${text(status)}</p>${state.previewError ? `<button type="button" data-cam-action="tube-designer-assembly-retry-preview">重新生成</button>` : ""}</div>`;
}

function renderAssemblyDualViewportStage() {
  const pane = (side, title, description) => `<section class="tube-assembly-preview-pane ${side}" data-tube-assembly-preview-pane="${side}">
    <header><div><strong>${title}</strong><span>${description}</span></div><div class="tube-assembly-preview-camera" role="group" aria-label="${title}视角">
      ${[["iso", "等轴"], ["front", "前视"], ["top", "顶视"], ["fit", "适合"]].map(([camera, label]) => `<button type="button" data-cam-action="tube-designer-assembly-camera" data-tube-assembly-side="${side}" data-tube-assembly-camera="${camera}">${label}</button>`).join("")}
    </div></header>
    <div class="tube-assembly-preview-host" data-tube-assembly-${side}-viewport></div>
  </section>`;
  return `<div class="tube-assembly-dual-preview" data-tube-assembly-dual-preview>${pane("finished", "成品", "两个逻辑零件的装配关系")}${pane("blank", "下料", "单件工艺作用后的下料形状")}</div>`;
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
    const side = String(target?.dataset?.tubeAssemblySide ?? "");
    const camera = String(target?.dataset?.tubeAssemblyCamera ?? "");
    controlAssemblyLibraryCamera(view, side, camera);
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
  const manufacturedByRole = new Map((preview?.manufacturingParts ?? [])
    .filter((part) => part.sourceRole)
    .map((part) => [part.sourceRole, part]));
  const canShowFinishedGeometry = designParts.length > 0
    && designParts.every((part) => manufacturedByRole.has(part.role));
  return designParts.map((part) => {
    const manufactured = canShowFinishedGeometry ? manufacturedByRole.get(part.role) : null;
    return {
      entityId: `assembly-finished:${part.id}`,
      data: {
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
    data: {
      geometry: part.response.geometry,
      material: part.response.material,
      geometryKind: 1,
      renderClass: 1,
      visible: true,
      selectable: false,
      localToWorldMatrix: part.matrix,
    },
  }));
}

const assemblyViewportControllers = new WeakMap();

function createAssemblyViewport(state, side, viewportFactory) {
  const viewport = viewportFactory({
    backgroundColor: 0x13252d,
    continuousRender: false,
    constrainOrbit: false,
    projectionMode: state.projectionModes[side],
    showProjectionToggle: true,
    pickingEnabled: false,
    antialias: true,
    pixelRatioCap: 2,
    onProjectionChange(mode) { state.projectionModes[side] = mode; },
  });
  return { viewport, host: null, ready: false, appliedKey: "", templateId: "" };
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
  const finishedHost = mount?.querySelector?.("[data-tube-assembly-finished-viewport]");
  const blankHost = mount?.querySelector?.("[data-tube-assembly-blank-viewport]");
  if (!finishedHost || !blankHost) return null;
  const state = assemblyLibraryState(view);
  let controller = assemblyViewportControllers.get(view);
  if (!controller) {
    const viewportFactory = options.viewportFactory ?? createThreeViewport;
    controller = {
      context,
      view,
      mount,
      generation: 0,
      finished: createAssemblyViewport(state, "finished", viewportFactory),
      blank: createAssemblyViewport(state, "blank", viewportFactory),
    };
    assemblyViewportControllers.set(view, controller);
  }
  controller.context = context;
  controller.mount = mount;
  mountAssemblyViewportPane(controller.finished, finishedHost);
  mountAssemblyViewportPane(controller.blank, blankHost);
  view.viewport?.setVisibleEntityIds?.([]);
  view.viewport?.setContinuousRendering?.(false);
  if (state.preview && (state.appliedKey !== state.preview.key || !controller.finished.ready || !controller.blank.ready)) {
    void applyAssemblyPreview(context, view, state.preview, state.preview.key);
  }
  return controller;
}

export function disposeAssemblyLibraryViewports(view) {
  if (!view || (typeof view !== "object" && typeof view !== "function")) return;
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return;
  controller.generation += 1;
  controller.finished.viewport.dispose?.();
  controller.blank.viewport.dispose?.();
  assemblyViewportControllers.delete(view);
}

function clearAssemblyLibraryViewports(view) {
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return;
  controller.generation += 1;
  for (const pane of [controller.finished, controller.blank]) {
    pane.viewport.setVisibleEntityIds?.([]);
    pane.ready = false;
    pane.appliedKey = "";
    pane.templateId = "";
  }
}

export function controlAssemblyLibraryCamera(view, side, command) {
  if (!["finished", "blank"].includes(side)) return false;
  const pane = assemblyViewportControllers.get(view)?.[side];
  if (!pane?.viewport) return false;
  if (command === "fit") return Boolean(pane.viewport.fitViewToViewport?.(1.18));
  if (!["iso", "front", "top", "right"].includes(command)) return false;
  const changed = pane.viewport.setStandardView?.(command) !== false;
  pane.viewport.fitViewToViewport?.(1.18);
  return changed;
}

async function applyAssemblyPreviewPane(pane, rows, resources, revision, templateId) {
  if (!rows.length) throw new Error("装配模板没有生成可显示的三维零件。");
  const previousCamera = pane.ready ? pane.viewport.getCameraState?.() : null;
  const receipt = await pane.viewport.applyViewSnapshot({ revision, rows }, resources);
  if (!receipt?.applied || receipt.missingGeometryEntityIds?.length || !receipt.entityIds?.length) {
    throw new Error("装配预览几何未完整进入三维场景。");
  }
  pane.viewport.setVisibleEntityIds?.(receipt.entityIds ?? rows.map((row) => row.entityId));
  if (!pane.ready || pane.templateId !== templateId) {
    pane.viewport.setStandardView?.("iso");
    pane.viewport.fitViewToViewport?.(1.18);
  } else if (previousCamera) {
    pane.viewport.setCameraState?.(previousCamera);
  }
  pane.ready = true;
  pane.appliedKey = revision;
  pane.templateId = templateId;
}

async function applyAssemblyPreview(context, view, preview, key) {
  if (view.activeAreaId !== "assemblies" || assemblyPreviewKey(view) !== key || assemblyLibraryState(view).preview !== preview) return false;
  const controller = assemblyViewportControllers.get(view);
  if (!controller) return false;
  const generation = ++controller.generation;
  const resources = context.sceneProxy?.resources ?? view.sceneProxy?.resources ?? context.projectProxy?.resources;
  const templateId = String(preview.plan?.templateId ?? "");
  await Promise.all([
    applyAssemblyPreviewPane(controller.finished, assemblyFinishedRows(preview), resources, `assembly-finished:${key}`, templateId),
    applyAssemblyPreviewPane(controller.blank, assemblyBlankRows(preview), resources, `assembly-blank:${key}`, templateId),
  ]);
  if (controller.generation !== generation || view.activeAreaId !== "assemblies"
      || assemblyPreviewKey(view) !== key || assemblyLibraryState(view).preview !== preview) return false;
  assemblyLibraryState(view).appliedKey = key;
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
    if (state.appliedKey !== key) void applyCurrentAssemblyPreview(context, view);
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
        || plan?.designParts?.length !== 2 || !plan?.manufacturingParts?.length) {
      throw new Error("装配模板没有返回完整的双零件预览计划。");
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
  return `<button type="button" class="tube-connection-library-card${item.id === selectedId ? " selected" : ""}" data-cam-action="tube-designer-assembly-select" data-tube-assembly-id="${attr(item.id)}"><span class="tube-connection-library-card-art">${miniPlan(item)}</span><span><strong>${text(item.displayName)}</strong><small>${item.participants.length} 个逻辑零件 → ${blanks} 个下料零件</small></span></button>`;
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
    if (item.parameterMode !== "inherit-resource" || !definitions.length) return null;
    const basic = definitions.filter((definition) => definition.level !== "advanced");
    const advanced = definitions.filter((definition) => definition.level === "advanced");
    const heading = processes.length > 1
      ? `<header><strong>${text(descriptor?.displayName ?? item.label)}</strong></header>` : "";
    const basicFields = basic.length
      ? `<div class="tube-connection-library-parameter-grid">${basic.map((definition) => processParameterField(definition, processValues, template.id, item.id, descriptor?.id)).join("")}</div>` : "";
    const advancedFields = advanced.length
      ? `<details class="tube-connection-library-process-advanced"><summary><span>高级设置</span><small>${advanced.length} 项</small></summary><div class="tube-connection-library-parameter-grid">${advanced.map((definition) => processParameterField(definition, processValues, template.id, item.id, descriptor?.id)).join("")}</div></details>` : "";
    return { category: descriptor?.category, displayName: descriptor?.displayName ?? item.label,
      html: `<article>${heading}${basicFields}${advancedFields}</article>` };
  }).filter(Boolean);
  if (!cards.length) return "";
  const title = cards.length === 1 && cards[0].category ? `${cards[0].category}参数` : "单件工艺参数";
  const detail = cards.length === 1 ? cards[0].displayName : `${cards.length} 道`;
  return `<section class="tube-connection-library-parameter-section basic tube-connection-library-process-section"><header><strong>${text(title)}</strong><small>${text(detail)}</small></header><div class="tube-connection-library-processes">${cards.map((item) => item.html).join("")}</div></section>`;
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

function miniPlan(template) {
  const logical = template.participants.length;
  const blanks = template.manufacturingPlan?.blankParts?.length ?? 0;
  const top = Array.from({ length: logical }, (_, index) => `<rect x="${4 + index * (40 / logical)}" y="7" width="${Math.max(5, 32 / logical)}" height="10" rx="2"/>`).join("");
  const bottom = Array.from({ length: blanks }, (_, index) => `<rect x="${4 + index * (40 / blanks)}" y="31" width="${Math.max(5, 32 / blanks)}" height="10" rx="2"/>`).join("");
  return `<svg viewBox="0 0 48 48">${top}<path d="M24 19v8m-3-3 3 3 3-3"/>${bottom}</svg>`;
}

function realizationLabel(template) {
  return ({ integrated: "一体成形", separate: "分件装配", hybrid: "混合装配" })[template.manufacturingPlan?.realization] ?? "装配";
}

function assemblySearchText(item) {
  return [item.displayName, item.categoryName, item.summary, item.topology, item.interfaceType, item.lockType,
    ...item.partProcesses.flatMap((process) => [process.resource?.id, ...(process.resourceSelection?.options ?? [])])].join(" ").toLocaleLowerCase("zh-CN");
}

function text(value) { return String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;"); }
function attr(value) { return text(value).replaceAll('"', "&quot;"); }
