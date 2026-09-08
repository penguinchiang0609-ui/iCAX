import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { sketchRibbonGroups } from "./ribbonDefinition.mjs";
import { renderRibbonCommandIcon } from "../../../iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs";
import { arcThroughPoints, curvePoint, editableSegments, editPathAtPoint, isArc, isConic, movePathNode, moveSegmentEnd, nearestPath, nearestSegment, pathNodes, pathSamples, pathSvg, reverseSegment, translateSegment } from "./sketchGeometry.mjs";

const SECTION_MODE = "section";
const SIDE_MODE = "side";
const SKETCH_SCHEMA = "icax.tube-sketch";
const CANVAS = Object.freeze({ width: 1000, height: 600, left: 70, right: 950, top: 90, bottom: 510 });
const DRAW_TOOLS = new Set(["line", "polyline", "path", "rectangle", "circle", "ellipse", "circleArc", "ellipseArc", "arc", "spline", "freehand", "text"]);
const EDIT_TOOLS = new Set(["break", "insert-point"]);
const SECTION_JOIN_TOLERANCE = 0.25;
const MAX_SKETCH_ENTITIES = 5000;
const MAX_ENTITY_POINTS = 10000;
const SECTION_SESSION_CREATE = "create";
const SECTION_SESSION_UPDATE = "update";
const SECTION_SESSION_COPY = "copy";
const SECTION_MIN_ZOOM = 0.0001;
const SECTION_MAX_ZOOM = 10000;
const SECTION_HALF_WIDTH = 110;
const FULL_ANGLE = Math.PI * 2;

export function createInitialSketchState() {
  return {
    mode: SECTION_MODE,
    tool: "select",
    command: null,
    snapEnabled: true,
    orthoEnabled: false,
    sectionViewport: createSectionViewport(),
    saveChoiceDialogOpen: false,
    sectionName: "我的草图管型",
    sectionSession: createSectionSession(),
    section: createDraft(),
    sideByMember: {},
    sideByPart: {},
    sidePartVersions: {},
    sideTargetKind: "member",
    sideTargetSnapshot: null,
    sideReference: null,
    sideReturnAreaId: "view",
    targetMemberId: "",
    targetPartId: "",
    loadedProductId: "",
  };
}

export function ensureSketchState(view) {
  view.tubeDesignerSketch ??= createInitialSketchState();
  const state = view.tubeDesignerSketch;
  if (![SECTION_MODE, SIDE_MODE].includes(state.mode)) state.mode = SECTION_MODE;
  if (state.command && state.command.tool !== state.tool) state.command = null;
  state.snapEnabled = state.snapEnabled !== false;
  state.orthoEnabled = state.orthoEnabled === true;
  state.sectionViewport = normalizeSectionViewport(state.sectionViewport);
  state.saveChoiceDialogOpen = state.saveChoiceDialogOpen === true;
  if (!state.section || !Array.isArray(state.section.entities)) state.section = createDraft();
  normalizeDraftSelection(state.section);
  state.sectionSession = normalizeSectionSession(state.sectionSession);
  state.sectionName = String(state.sectionName || "我的草图管型");
  state.section.persisted ??= false;
  state.sideByMember ??= {};
  state.sideByPart ??= {};
  state.sidePartVersions ??= {};
  state.sideTargetKind = state.sideTargetKind === "part" ? "part" : "member";
  state.targetPartId = String(state.targetPartId ?? "");
  state.sideReturnAreaId = String(state.sideReturnAreaId || "view");

  const designer = view.scene?.tubeDesigner ?? {};
  const productId = String(designer.product?.entityId ?? "");
  const members = sketchableMembers(designer);
  if (state.loadedProductId !== productId) {
    state.loadedProductId = productId;
    state.sideByMember = {};
    const stored = designer.product?.sketches?.side;
    if (stored && typeof stored === "object") {
      for (const [memberId, sketch] of Object.entries(stored)) {
        const stableKey = String(sketch?.targetMemberKey ?? "");
        const current = members.find((member) => member.entityId === memberId)
          ?? (stableKey ? members.find((member) => String(member.stableKey ?? "") === stableKey) : null);
        state.sideByMember[String(current?.entityId ?? memberId)] = draftFromStoredSketch(sketch);
      }
    }
    if (state.sideTargetKind !== "part") {
      state.targetMemberId = members.some((member) => member.entityId === state.targetMemberId)
        ? state.targetMemberId : String(members[0]?.entityId ?? "");
    }
  }
  if (state.sideTargetKind !== "part") {
    if (!members.some((member) => member.entityId === state.targetMemberId)) {
      state.targetMemberId = String(members[0]?.entityId ?? "");
    }
    if (state.targetMemberId && !state.sideByMember[state.targetMemberId]) {
      state.sideByMember[state.targetMemberId] = createDraft();
    }
  }
  return state;
}

export function beginNewSectionSketch(view) {
  const state = ensureSketchState(view);
  state.mode = SECTION_MODE;
  state.tool = "select";
  state.command = null;
  state.sectionViewport = createSectionViewport();
  state.saveChoiceDialogOpen = false;
  state.sectionName = "我的草图管型";
  state.sectionSession = createSectionSession();
  state.section = createDraft();
  view.error = "";
  return state;
}

export function beginProfileSectionSketch(view, source) {
  const snapshot = source?.snapshot;
  const entities = entitiesFromProfile(snapshot);
  if (!entities.length) throw new Error("所选管型没有可编辑的截面轮廓。");
  const state = ensureSketchState(view);
  const directUpdate = source?.directUpdate === true;
  const sourceName = String(source?.name ?? snapshot?.name ?? "未命名管型").trim() || "未命名管型";
  state.mode = SECTION_MODE;
  state.tool = "select";
  state.command = null;
  state.saveChoiceDialogOpen = false;
  state.sectionName = directUpdate ? sourceName : `${sourceName}（草图副本）`;
  state.sectionSession = normalizeSectionSession({
    kind: directUpdate ? SECTION_SESSION_UPDATE : SECTION_SESSION_COPY,
    sourceProfileId: source?.profileId,
    sourceProfileKey: source?.profileKey,
    sourceScope: source?.scope,
    sourceRevision: source?.revision,
    sourceName,
  });
  state.section = {
    ...createDraft(),
    entities,
    selectedId: entities[0]?.id ?? "",
    selectedIds: entities[0]?.id ? [entities[0].id] : [],
  };
  fitSectionViewport(state, entities);
  view.error = "";
  return state;
}

export function beginPartSideSketch(view, part, report) {
  const partId = String(part?.entityId ?? "").trim();
  if (!partId || !(Number(part?.length) > 0)) {
    throw new Error("当前下料零件没有可编辑的侧面尺寸。");
  }
  const state = ensureSketchState(view);
  const resourceVersion = Number(part?.manufacturingGeometryResourceVersion ?? 0);
  const versionKey = `${partId}@${resourceVersion}`;
  if (!state.sideByPart[partId] || state.sidePartVersions[partId] !== versionKey) {
    const stored = part?.properties?.["tubeDesigner.sideSketch"];
    state.sideByPart[partId] = stored && typeof stored === "object"
      ? draftFromStoredSketch(stored)
      : createDraft();
    state.sidePartVersions[partId] = versionKey;
  }
  state.mode = SIDE_MODE;
  state.tool = "select";
  state.command = null;
  state.saveChoiceDialogOpen = false;
  state.sideTargetKind = "part";
  state.targetPartId = partId;
  state.sideTargetSnapshot = structuredCloneValue(part);
  state.sideReference = report && typeof report === "object"
    ? structuredCloneValue(report) : null;
  state.sideReturnAreaId = String(view.activeAreaId || "nesting");
  view.error = "";
  return state;
}

export function isSectionPreviewReady(draft) {
  return analyzeSectionDraft(draft).ready;
}

export function renderSketchLeftPane(_context, view) {
  const state = ensureSketchState(view);
  const draft = currentDraft(view, state);
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetSideEntity(designer, state);
  const members = sketchableMembers(designer);
  const partTarget = state.mode === SIDE_MODE && state.sideTargetKind === "part";
  const componentProfile = state.mode === SECTION_MODE && Boolean(view.tubeDesignerComponentCSGProfileReturn);
  const sectionAnalysis = analyzeSectionDraft(state.section);

  return `<div class="tube-sketch-preview-panel" data-tube-sketch-preview-panel>
    <header>
      <strong>${state.mode === SECTION_MODE ? (componentProfile ? "拉伸截面预览" : "管型预览") : (partTarget ? "零件侧面预览" : "三维切割预览")}</strong>
      <span>${state.mode === SECTION_MODE
        ? (componentProfile ? "截面闭合后可回填并生成三维拉伸体" : "截面闭合后自动生成三维管型")
        : (member ? (partTarget ? "灰色底图来自最终零件，青色图形为当前草图" : "实时查看图形在管子侧面的效果") : "请先选择一根管件")}</span>
    </header>
    <div class="tube-sketch-preview-stage">
      ${state.mode === SECTION_MODE
        ? renderSectionPreview(sectionAnalysis)
        : renderSidePreview(draft, member, state.sideReference)}
    </div>
    <footer>
      ${state.mode === SIDE_MODE && !partTarget && members.length > 1 ? `<label>
        <span>当前管件</span>
        <select data-cam-change-action="tube-designer-sketch-target-member">
          ${members.map((item) => `<option value="${escapeAttr(item.entityId)}" ${item.entityId === state.targetMemberId ? "selected" : ""}>${escapeText(item.name || `管件 ${item.index ?? ""}`)}</option>`).join("")}
        </select>
      </label>` : `<span>${state.mode === SECTION_MODE
        ? sectionAnalysis.message
        : escapeText(member?.name ?? "没有可用管件")}</span>`}
    </footer>
  </div>`;
}

export function renderSectionSketchDialog(context, view) {
  if (!view.tubeDesignerSketchDialogOpen) return "";
  const state = ensureSketchState(view);
  const sidePart = state.mode === SIDE_MODE && state.sideTargetKind === "part";
  const componentProfile = Boolean(view.tubeDesignerComponentCSGProfileReturn);
  const title = sidePart ? "下料零件二维编辑" : (componentProfile ? "拉伸体二维截面" : "截面轮廓草图");
  return `<dialog class="tube-section-sketch-dialog" aria-label="${title}">
    <header class="tube-section-sketch-title">${title}</header>
    <nav class="tube-section-sketch-toolbar" aria-label="${sidePart ? "零件侧面绘制工具" : "截面绘制工具"}">${sketchRibbonGroups.map(group => `<section><div>${group.commands.map(command => `<button type="button" data-cam-action="tube-designer-sketch-dialog-command" data-sketch-command="${escapeAttr(command.id)}" ${view.pending ? "disabled" : ""} ${command.id === `sketch.${state.tool}` ? 'class="selected"' : ""} data-icon-tone="${escapeAttr(command.iconTone ?? "green")}">${renderRibbonCommandIcon(command.iconName)}<span>${escapeText(command.title)}</span></button>`).join("")}</div><small>${escapeText(group.title)}</small></section>`).join("")}</nav>
    ${view.error ? `<div role="alert" class="tube-section-sketch-error">${escapeText(view.error)}</div>` : ""}
    <div class="tube-section-sketch-body"><aside>${renderSketchLeftPane(context, view)}</aside><main>${renderSketchViewportOverlay(context, view)}</main><aside>${renderSketchRightPane(context, view)}</aside></div>
  </dialog>`;
}

export function renderSketchViewportOverlay(_context, view) {
  const state = ensureSketchState(view);
  const draft = currentDraft(view, state);
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetSideEntity(designer, state);
  const metrics = canvasMetrics(state.mode, member, state);
  const selectedIds = draftSelectionIds(draft);
  const selectedPoints = draftSelectedPoints(draft);
  const selectedPointKeys = new Set(selectedPoints.map(pointSelectionKey));
  const selectedId = String(selectedIds[0] ?? "");
  const modeTitle = state.mode === SECTION_MODE ? (view.tubeDesignerComponentCSGProfileReturn ? "拉伸截面图" : "管型截面图")
    : (state.sideTargetKind === "part" ? "零件侧视草图" : "管型侧面切割图");
  const subtitle = state.command
    ? commandPrompt(state.command)
    : state.mode === SECTION_MODE
    ? "无限模型空间 · 中键拖动平移 · 滚轮缩放"
    : (member ? `${member.name || "当前管件"} · ${formatNumber(member.length || metrics.xMax)} mm` : "需要先在产品页生成管件");

  return `<div class="tube-sketch-canvas-shell" data-tube-sketch-canvas-shell data-tube-sketch-mode="${state.mode}" data-tube-sketch-tool="${state.tool}">
    <header class="tube-sketch-canvas-header">
      <div><strong>${modeTitle}</strong><span>${escapeText(subtitle)}</span></div>
      ${view.tubeDesignerSketchDialogOpen ? "" : `<div class="tube-sketch-mode-switch" role="group" aria-label="草图类型">
        <button type="button" data-cam-action="tube-designer-sketch-switch-mode" data-tube-sketch-mode="section" aria-pressed="${state.mode === SECTION_MODE}">管型截面图</button>
        <button type="button" data-cam-action="tube-designer-sketch-switch-mode" data-tube-sketch-mode="side" aria-pressed="${state.mode === SIDE_MODE}">管型侧面切割图</button>
      </div>`}
    </header>
    <div class="tube-sketch-canvas-stage">
      <svg class="tube-sketch-canvas" data-tube-sketch-canvas viewBox="0 0 ${CANVAS.width} ${state.mode===SECTION_MODE?(state.canvasHeight??CANVAS.height):CANVAS.height}" role="img" aria-label="${modeTitle}绘图区" tabindex="0">
        <defs>
          <pattern id="tube-sketch-small-grid-${state.mode}" width="20" height="20" patternUnits="userSpaceOnUse"><path d="M20 0H0V20" /></pattern>
          <pattern id="tube-sketch-grid-${state.mode}" width="100" height="100" patternUnits="userSpaceOnUse"><rect width="100" height="100" fill="url(#tube-sketch-small-grid-${state.mode})"/><path d="M100 0H0V100" /></pattern>
        </defs>
        <rect class="tube-sketch-grid" width="100%" height="100%" ${state.mode === SIDE_MODE ? `fill="url(#tube-sketch-grid-${state.mode})"` : ""} />
        <g data-tube-sketch-guide>${state.mode === SECTION_MODE ? renderSectionCanvasGuide(metrics) : renderSideCanvasGuide(metrics, member, state.sideReference)}</g>
        <g class="tube-sketch-geometry">
          ${draft.entities.map((entity) => renderSketchEntity(entity, state.mode, metrics, selectedIds.includes(entity.id), selectedPointKeys)).join("")}
          ${renderUnselectedNodes(draft, state, metrics)}
        </g>
        <g class="tube-sketch-draft-preview" data-tube-sketch-draft-preview></g>
        <g class="tube-sketch-snap-preview" data-tube-sketch-snap-preview></g>
        <g class="tube-sketch-cursor-preview" data-tube-sketch-cursor-preview></g>
      </svg>
      ${state.mode === SIDE_MODE && !member ? `<div class="tube-sketch-canvas-empty"><strong>还没有可以绘制的管件</strong><span>先到“产品”生成产品，随后即可在管件侧面绘制切割图。</span></div>` : ""}
    </div>
    <footer class="tube-sketch-canvas-status">
      <span data-tube-sketch-command-status>${escapeText(state.command ? commandPrompt(state.command) : toolLabel(state.tool))}</span>
      <span>${draft.entities.length} 个图形${selectedPoints.length ? ` · 已选中 ${selectedPoints.length} 个点` : selectedIds.length ? ` · 已选中 ${selectedIds.length} 个` : ""}</span>
      <div class="tube-sketch-status-toggles">
        <button type="button" data-cam-action="tube-designer-sketch-toggle-snap" aria-pressed="${state.snapEnabled}">F3 捕捉</button>
        <button type="button" data-cam-action="tube-designer-sketch-toggle-ortho" aria-pressed="${state.orthoEnabled}">F8 正交</button>
        ${state.mode === SECTION_MODE ? `<button type="button" data-cam-action="tube-designer-sketch-fit-view">适合窗口</button>` : ""}
      </div>
      <span data-tube-sketch-coordinate-status>X 0.000 · Y 0.000</span>
      <span>${state.mode === SECTION_MODE ? "无限画布 · " : ""}${draft.dirty ? "未保存" : (draft.entities.length ? "已保存" : "空草图")}</span>
    </footer>
    ${state.saveChoiceDialogOpen ? renderSectionSaveChoiceDialog(state) : ""}
  </div>`;
}

export function renderSketchRightPane(_context, view) {
  const state = ensureSketchState(view);
  const draft = currentDraft(view, state);
  const selectedIds = draftSelectionIds(draft);
  const selectedPoints = draftSelectedPoints(draft);
  const selected = selectedIds.length === 1
    ? draft.entities.find((entity) => entity.id === selectedIds[0]) ?? null
    : null;
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetSideEntity(designer, state);
  const validation = state.mode === SECTION_MODE
    ? analyzeSectionDraft(state.section)
    : validateSideSketchDraft(draft, member, state.sideReference);

  return `<div class="tube-sketch-property-panel">
    <header>
      <strong>${selectedPoints.length ? `已选择 ${selectedPoints.length} 个点` : selectedIds.length > 1 ? `已选择 ${selectedIds.length} 个图形` : selected ? entityLabel(selected) : "图形属性"}</strong>
      <span>${selectedPoints.length ? "框选或按 Shift/Ctrl 多选节点；两个开放端点可合并" : selected ? "可拖动夹点；框选不同曲线的端点可合并" : selectedIds.length > 1 ? "端点相接的图形可从菜单执行合并" : "局部框选节点；框住完整图形选图形；Shift/Ctrl 多选"}</span>
    </header>
    <div class="tube-sketch-property-body">
      ${state.mode === SECTION_MODE ? renderSectionSession(state, view?.pending, Boolean(view.tubeDesignerComponentCSGProfileReturn)) : ""}
      ${state.mode === SIDE_MODE && state.sideTargetKind === "part" ? renderSideReferenceSummary(state.sideReference) : ""}
      ${renderSketchValidation(validation, state.mode, draft)}
      ${selected ? renderEntityProperties(selected) : `<div class="tube-sketch-property-empty">
        <span class="tube-sketch-property-empty-icon">⌁</span>
        <strong>${selectedIds.length > 1 ? `已选择 ${selectedIds.length} 个图形` : "尚未选择图形"}</strong>
        <span>${selectedIds.length > 1 ? "使用菜单中的合并或删除，也可按 Delete 键。" : "在绘图区单击图形，或拖出选择框。"}</span>
      </div>`}
    </div>
  </div>`;
}

export async function handleSketchAreaAction(context, view, action, target, ops) {
  if (!String(action).startsWith("tube-designer-sketch-")) return { handled: false };
  const state = ensureSketchState(view);
  if (action === "tube-designer-sketch-dialog-command") {
    if (!view.pending) await handleSketchRibbonCommand(context, view, target?.dataset?.sketchCommand, ops);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-switch-mode") {
    if (view.tubeDesignerSketchDialogOpen) return { handled: true };
    state.mode = target?.dataset?.tubeSketchMode === SIDE_MODE ? SIDE_MODE : SECTION_MODE;
    state.tool = "select";
    state.command = null;
    view.error = "";
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-target-member") {
    state.sideTargetKind = "member";
    state.targetPartId = "";
    state.sideTargetSnapshot = null;
    state.sideReference = null;
    state.targetMemberId = String(target?.value ?? "");
    state.sideByMember[state.targetMemberId] ??= createDraft();
    state.tool = "select";
    state.command = null;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-toggle-snap") {
    state.snapEnabled = !state.snapEnabled;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-toggle-ortho") {
    state.orthoEnabled = !state.orthoEnabled;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-fit-view") {
    fitSectionViewport(state, state.section.entities);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-section-name") {
    state.sectionName = String(target?.value ?? "").slice(0, 120);
    view.error = "";
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-cancel-section") {
    await cancelSketch(context, view, ops);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-save-overwrite") {
    state.saveChoiceDialogOpen = false;
    return { handled: true, result: await saveSectionProfile(context, view, ops, "overwrite") };
  }
  if (action === "tube-designer-sketch-save-copy") {
    state.saveChoiceDialogOpen = false;
    return { handled: true, result: await saveSectionProfile(context, view, ops, "copy") };
  }
  if (action === "tube-designer-sketch-save-choice-cancel") {
    state.saveChoiceDialogOpen = false;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-property") {
    updateSelectedProperty(currentDraft(view, state), target);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-delete-selected") {
    deleteSelected(currentDraft(view, state));
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-reverse") {
    reverseSelected(currentDraft(view, state));
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-toggle-closed") {
    toggleSelectedClosed(currentDraft(view, state));
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-commit") {
    return { handled: true, result: state.mode === SECTION_MODE
      ? await saveSectionProfile(context, view, ops)
      : await saveSideSketch(context, view, ops) };
  }
  return { handled: true };
}

export async function handleSketchRibbonCommand(context, view, commandId, ops) {
  if (!String(commandId).startsWith("sketch.")) return false;
  const state = ensureSketchState(view);
  if (commandId === "sketch.mode-section" || commandId === "sketch.mode-side") {
    if (view.tubeDesignerSketchDialogOpen) return true;
    state.mode = commandId.endsWith("side") ? SIDE_MODE : SECTION_MODE;
    state.tool = "select";
    state.command = null;
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.import") {
    await importSketchDxf(context, view, ops);
    return true;
  }
  if (commandId === "sketch.undo") {
    state.command = null;
    undoDraft(currentDraft(view, state));
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.redo") {
    state.command = null;
    redoDraft(currentDraft(view, state));
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.delete") {
    state.command = null;
    deleteSelected(currentDraft(view, state));
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.join") {
    state.command = null;
    const result = mergeSelectedEntities(currentDraft(view, state));
    view.error = result.changed ? "" : result.message;
    if (result.changed) ops.showNotice?.(context, view, result.message);
    else ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.commit") {
    if (state.mode === SECTION_MODE) await saveSectionProfile(context, view, ops);
    else await saveSideSketch(context, view, ops);
    return true;
  }
  if (commandId === "sketch.cancel") {
    await cancelSketch(context, view, ops);
    return true;
  }
  const tool = commandId.replace(/^sketch\./, "");
  if (tool === "select" || EDIT_TOOLS.has(tool) || DRAW_TOOLS.has(tool)) {
    state.tool = tool;
    state.command = null;
    ops.renderProject(context, view);
    return true;
  }
  return true;
}

export function attachSketchAreaInteractions(context, view, mount, ops) {
  const state = ensureSketchState(view);
  const dialog = mount?.querySelector?.(".tube-section-sketch-dialog");
  if (dialog && !dialog.open) {
    dialog.showModal?.();
    dialog.addEventListener("cancel", event => {
      event.preventDefault();
      if (!view.pending) void cancelSketch(context, view, ops);
    });
  }
  if ((!view.tubeDesignerSketchDialogOpen && view.activeAreaId !== "sketch") || view.pending) return;
  const saveChoice = mount?.querySelector?.(".tube-sketch-save-choice");
  if (saveChoice) {
    saveChoice.querySelector("[data-cam-action='tube-designer-sketch-save-overwrite']")?.focus?.();
    return;
  }
  const svg = mount?.querySelector?.("[data-tube-sketch-canvas]");
  if (!svg || svg.dataset.tubeSketchAttached === "true") return;
  svg.dataset.tubeSketchAttached = "true";
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetSideEntity(designer, state);
  if (state.mode === SIDE_MODE && !member) return;
  let metrics = canvasMetrics(state.mode, member, state);
  const draft = currentDraft(view, state);
  let gesture = null;

  const repaintGeometry = () => {
    const host = svg.querySelector(".tube-sketch-geometry");
    const selected = draftSelectionIds(draft);
    const selectedPointKeys = new Set(draftSelectedPoints(draft).map(pointSelectionKey));
    if (host) host.innerHTML = draft.entities
      .map((entity) => renderSketchEntity(entity, state.mode, metrics, selected.includes(entity.id), selectedPointKeys))
      .join("") + renderUnselectedNodes(draft, state, metrics);
    if(state.mode===SECTION_MODE) {
      const guide=svg.querySelector("[data-tube-sketch-guide]");
      if(guide)guide.innerHTML=renderSectionCanvasGuide(metrics);
    }
  };

  if(state.mode===SECTION_MODE) {
    const resize=()=>{
      if(!svg.isConnected){svg._sketchResizeObserver?.disconnect();return;}
      if(svg.clientWidth<=0||svg.clientHeight<=0)return;
      state.canvasHeight=CANVAS.width*svg.clientHeight/svg.clientWidth;
      svg.setAttribute("viewBox",`0 0 ${CANVAS.width} ${state.canvasHeight}`);
      metrics=canvasMetrics(state.mode,member,state);
      // Snap radius stays approximately ten screen pixels at every panel size.
      metrics.snapTolerance=(metrics.xMax-metrics.xMin)/svg.clientWidth*10;
      repaintGeometry();
    };
    view.tubeSketchResizeObserver?.disconnect();
    resize();
    if(typeof ResizeObserver!=="undefined") {
      const observer=new ResizeObserver(resize);
      observer.observe(svg);
      svg._sketchResizeObserver=observer;
      view.tubeSketchResizeObserver=observer;
    }
  }

  const pointerPoint = (event, base = null, excludeEntityId = "") => {
    const raw = eventToModelPoint(svg, event, state.mode, metrics);
    return resolveCadPoint(draft, raw, metrics, state, base, excludeEntityId);
  };

  svg.addEventListener("pointerdown", (event) => {
    svg.focus?.({ preventScroll: true });
    if (event.button === 1 && state.mode === SECTION_MODE) {
      gesture = {
        type: "pan",
        startClient: [event.clientX, event.clientY],
        originalViewport: { ...state.sectionViewport },
      };
      svg.setPointerCapture?.(event.pointerId);
      event.preventDefault();
      return;
    }
    if (event.button !== 0) return;
    const entityTarget = event.target instanceof Element ? event.target.closest("[data-tube-sketch-entity-id]") : null;
    let gripTarget = event.target instanceof Element ? event.target.closest("[data-tube-sketch-grip-role]") : null;
    // Coincident endpoints share one location but must remain individually reachable.
    if(gripTarget&&event.altKey) {
      const stack=[...svg.querySelectorAll('[data-tube-sketch-grip-role="vertex"]')].filter(g=>{
        const rect=g.getBoundingClientRect();return event.clientX>=rect.left-2&&event.clientX<=rect.right+2&&event.clientY>=rect.top-2&&event.clientY<=rect.bottom+2;
      });
      if(stack.length>1) {
        const selected = new Set(draftSelectedPoints(draft).map(pointSelectionKey));
        const unselected = (event.shiftKey || event.ctrlKey) && stack.find(g => !selected.has(pointSelectionKey({
          entityId: g.dataset.tubeSketchGripEntityId, index: Number(g.dataset.tubeSketchGripIndex),
        })));
        gripTarget = unselected || stack[(stack.indexOf(gripTarget)+1)%stack.length];
      }
    }
    if (state.tool === "select" && gripTarget) {
      const entityId = String(gripTarget.dataset?.tubeSketchGripEntityId ?? "");
      const entity = draft.entities.find((item) => item.id === entityId);
      if (!entity) return;
      const pointSelection = {
        entityId,
        index: Number(gripTarget.dataset?.tubeSketchGripIndex ?? -1),
      };
      if (pointSelection.index >= 0) {
        const currentPoints = draftSelectedPoints(draft);
        const exists = currentPoints.some((value) => pointSelectionKey(value) === pointSelectionKey(pointSelection));
        if (event.ctrlKey || event.shiftKey) {
          setDraftPointSelection(draft, exists
            ? currentPoints.filter((value) => pointSelectionKey(value) !== pointSelectionKey(pointSelection))
            : [...currentPoints, pointSelection]);
          setDraftSelectionKeepingPoints(draft, [...draftSelectionIds(draft), entityId]);
        } else {
          setDraftSelection(draft, [entityId]);
          setDraftPointSelection(draft, [pointSelection]);
        }
      }
      pushHistory(draft);
      gesture = {
        type: "grip",
        entityId,
        role: String(gripTarget.dataset?.tubeSketchGripRole ?? "vertex"),
        index: Number(gripTarget.dataset?.tubeSketchGripIndex ?? -1),
        start: eventToModelPoint(svg, event, state.mode, metrics),
        original: structuredCloneValue(entity),
        originalDirty: draft.dirty,
        originalFuture: [...(draft.future??[])],
        moved: false,
      };
      svg.setPointerCapture?.(event.pointerId);
      event.preventDefault();
      return;
    }
    if (state.tool === "break" || state.tool === "insert-point") {
      const raw = eventToModelPoint(svg, event, state.mode, metrics);
      const nearest = draft.entities.filter(e=>e.kind!=="text").map(entity=>({entity,hit:nearestPath(entity,raw)}))
        .filter(value=>value.hit&&value.hit.distance<=metrics.snapTolerance)
        .sort((a,b)=>a.hit.distance-b.hit.distance)[0];
      const entityId = String(
        entityTarget?.dataset?.tubeSketchEntityId
          ?? gripTarget?.dataset?.tubeSketchGripEntityId
          ?? nearest?.entity.id
          ?? "",
      );
      if (entityId) {
        const resolved = resolveCadPoint(draft, raw, metrics, state);
        const entity=draft.entities.find(e=>e.id===entityId);
        if(nearestPath(entity,raw)?.distance>metrics.snapTolerance)return;
        const onCurve=nearestPath(entity,resolved.point);
        const point=onCurve?.distance<=1e-6?resolved.point:nearestPath(entity,raw).point;
        const result = state.tool === "break"
          ? breakEntityAtPoint(draft, entityId, point)
          : insertPointOnEntity(draft, entityId, point);
        view.error = result.changed ? "" : result.message;
        if (result.changed) state.tool = "select";
        ops.renderProject(context, view);
      }
      return;
    }
    if (state.tool === "select") {
      const selectedId = String(entityTarget?.dataset?.tubeSketchEntityId ?? "");
      if (selectedId) {
        const current = draftSelectionIds(draft);
        if (event.ctrlKey || event.shiftKey) {
          setDraftSelection(draft, current.includes(selectedId)
            ? current.filter((id) => id !== selectedId)
            : [...current, selectedId]);
        } else setDraftSelection(draft, [selectedId]);
        ops.renderProject(context, view);
      } else {
        const point = eventToModelPoint(svg, event, state.mode, metrics);
        gesture = {
          type: "selection",
          start: point,
          current: point,
          additive: event.ctrlKey || event.shiftKey,
          selectedEntityIds: [...draftSelectionIds(draft)],
          pointEntityIds: draft.entities.filter(entity => hasEditableVertices(entity)
            || draftSelectionIds(draft).includes(entity.id)).map(entity => entity.id),
        };
        svg.setPointerCapture?.(event.pointerId);
      }
      return;
    }
    const base = commandBasePoint(state.command);
    const resolved = pointerPoint(event, base);
    const start = resolved.point;
    if (state.tool === "text") {
      const value = String(globalThis.prompt?.("输入文字", "文字") ?? "").trim();
      if (value) addEntity(draft, { id: createEntityId(), kind: "text", x: start[0], y: start[1], value });
      state.tool = "select";
      state.command = null;
      ops.renderProject(context, view);
      return;
    }
    if (state.tool === "freehand") {
      gesture = { type: "freehand", start, points: [start] };
      svg.setPointerCapture?.(event.pointerId);
      return;
    }
    handleCadPoint(state, draft, start, metrics);
    ops.renderProject(context, view);
  });

  svg.addEventListener("pointermove", (event) => {
    if (gesture?.type === "pan") {
      const spanX = metrics.xMax - metrics.xMin;
      const spanY = metrics.yMax - metrics.yMin;
      state.sectionViewport = {
        ...gesture.originalViewport,
        centerX: gesture.originalViewport.centerX - (event.clientX - gesture.startClient[0]) / Math.max(1, svg.clientWidth) * spanX,
        centerY: gesture.originalViewport.centerY + (event.clientY - gesture.startClient[1]) / Math.max(1, svg.clientHeight) * spanY,
      };
      metrics = canvasMetrics(state.mode, member, state);
      repaintGeometry();
      return;
    }
    const base = gesture?.type === "grip" ? gesture.start : commandBasePoint(state.command);
    const excluded = gesture?.type === "grip" ? gesture.entityId : "";
    const resolved = pointerPoint(event, base, excluded);
    renderCadCursor(svg, resolved, metrics, state.command);
    updateCoordinateStatus(mount, resolved.point);
    if (gesture?.type === "grip") {
      const entity = draft.entities.find((item) => item.id === gesture.entityId);
      if (entity) {
        if(!gesture.moved&&pointDistance2d(resolved.point,gesture.start)<1e-7)return;
        updateEntityFromGrip(entity, gesture, resolved.point);
        if(isArc(gesture.original)&&entity.kind==="path")setDraftPointSelection(draft,[{entityId:entity.id,index:gesture.index===0?0:entity.segments.length}]);
        gesture.moved = true;
        draft.future=[];
        draft.dirty = true;
        repaintGeometry();
      }
      return;
    }
    if (gesture?.type === "selection") {
      gesture.current = eventToModelPoint(svg, event, state.mode, metrics);
      renderSelectionWindow(svg, gesture, metrics);
      return;
    }
    if (gesture?.type === "freehand") {
      const point = eventToModelPoint(svg, event, state.mode, metrics);
      const previous = gesture.points.at(-1);
      if (!previous || Math.hypot(point[0] - previous[0], point[1] - previous[1]) > metrics.sampleStep) gesture.points.push(point);
      renderGesturePreview(svg, "freehand", gesture, state.mode, metrics);
      return;
    }
    if (state.command) renderCommandPreview(svg, state.command, resolved.point, state.mode, metrics);
  });

  const finishGesture = (event) => {
    if (!gesture) return;
    const completed = gesture;
    gesture = null;
    clearGesturePreview(svg);
    if (completed.type === "grip") {
      if (!completed.moved) {
        draft.history?.pop();
        draft.dirty = completed.originalDirty;
      }
    } else if (completed.type === "pan") {
      // View state has already been updated during pointer movement.
    } else if (completed.type === "selection") {
      // A box around whole unselected objects still selects objects. A local
      // box around their nodes can select points across any number of curves.
      const enclosed = new Set(selectEntitiesInWindow(draft.entities,
        [Math.min(completed.start[0], completed.current[0]), completed.start[1]],
        [Math.max(completed.start[0], completed.current[0]), completed.current[1]]));
      const points = selectControlPointsInWindow(
        draft.entities,
        completed.pointEntityIds.filter(id => !enclosed.has(id) || completed.selectedEntityIds.includes(id)),
        completed.start,
        completed.current,
      );
      if (points.length) {
        setDraftPointSelection(draft, completed.additive
          ? [...draftSelectedPoints(draft), ...points]
          : points);
        setDraftSelectionKeepingPoints(draft, completed.additive
          ? [...draftSelectionIds(draft), ...points.map(point => point.entityId)]
          : points.map(point => point.entityId));
      } else {
        const selected = selectEntitiesInWindow(draft.entities, completed.start, completed.current);
        setDraftSelection(draft, completed.additive
          ? [...draftSelectionIds(draft), ...selected]
          : selected);
      }
    } else if (completed.type === "freehand") {
      const end = pointerPoint(event).point;
      const entity = entityFromGesture("freehand", completed, end, metrics);
      if (entity) addEntity(draft, entity, state.snapEnabled && state.mode === SECTION_MODE);
      state.tool = "select";
    }
    ops.renderProject(context, view);
  };
  svg.addEventListener("pointerup", finishGesture);
  svg.addEventListener("pointercancel", () => {
    if (gesture?.type === "grip") restoreGripGesture(draft, gesture);
    if (gesture?.type === "pan") state.sectionViewport = gesture.originalViewport;
    gesture = null;
    clearGesturePreview(svg);
    ops.renderProject(context, view);
  });
  svg.addEventListener("contextmenu", (event) => {
    if (!state.command) return;
    event.preventDefault();
    finishCadCommand(state, draft, metrics);
    ops.renderProject(context, view);
  });
  svg.addEventListener("keydown", (event) => {
    const key = String(event.key ?? "");
    if ((event.ctrlKey || event.metaKey) && key.toLowerCase() === "z") {
      event.preventDefault();
      state.command = null;
      undoDraft(draft);
      ops.renderProject(context, view);
      return;
    }
    if ((event.ctrlKey || event.metaKey) && key.toLowerCase() === "y") {
      event.preventDefault();
      state.command = null;
      redoDraft(draft);
      ops.renderProject(context, view);
      return;
    }
    if (key === "Delete") {
      event.preventDefault();
      deleteSelected(draft);
      ops.renderProject(context, view);
      return;
    }
    if (key === "Escape") {
      event.preventDefault();
      if (gesture?.type === "grip") restoreGripGesture(draft, gesture);
      if (gesture?.type === "pan") state.sectionViewport = gesture.originalViewport;
      gesture = null;
      state.command = null;
      state.tool = "select";
      ops.renderProject(context, view);
      return;
    }
    if ((key === "Enter" || key === " ") && state.command) {
      event.preventDefault();
      finishCadCommand(state, draft, metrics);
      ops.renderProject(context, view);
      return;
    }
    if (key === "F3") {
      event.preventDefault();
      state.snapEnabled = !state.snapEnabled;
      ops.renderProject(context, view);
      return;
    }
    if (key === "F8") {
      event.preventDefault();
      state.orthoEnabled = !state.orthoEnabled;
      ops.renderProject(context, view);
      return;
    }
    if (key === "Home" && state.mode === SECTION_MODE) {
      event.preventDefault();
      fitSectionViewport(state, draft.entities);
      ops.renderProject(context, view);
    }
  });
  svg.addEventListener("wheel", (event) => {
    if (state.mode !== SECTION_MODE) return;
    event.preventDefault();
    zoomSectionViewportAtPointer(state, svg, event, metrics);
    metrics = canvasMetrics(state.mode, member, state);
    ops.renderProject(context, view);
  }, { passive: false });
}

export function handleCadPoint(state, draft, point, metrics) {
  const tool = state.tool;
  const command = state.command?.tool === tool ? state.command : { tool, points: [] };
  if (!command.points.length) {
    command.points = [point];
    state.command = command;
    return;
  }
  if (tool === "line") {
    const start = command.points.at(-1);
    if (pointDistance2d(start, point) > metrics.sampleStep) {
      addEntity(draft, { id: createEntityId(), kind: "line", x1: start[0], y1: start[1], x2: point[0], y2: point[1] }, state.snapEnabled && state.mode === SECTION_MODE);
      command.points.push(point);
    }
    state.command = command;
    return;
  }
  if (tool === "polyline" || tool === "spline") {
    const minimum = tool === "spline" ? 3 : 2;
    const closing = command.points.length >= Math.max(3, minimum)
      && pointDistance2d(command.points[0], point) <= metrics.snapTolerance;
    if (closing) {
      addEntity(draft, { id: createEntityId(), kind: tool, points: deduplicatePoints(command.points), closed: true });
      state.command = null;
      state.tool = "select";
    } else if (pointDistance2d(command.points.at(-1), point) > metrics.sampleStep) {
      command.points.push(point);
      state.command = command;
    }
    return;
  }
  if (tool === "rectangle") {
    const start = command.points[0];
    if (pointDistance2d(start, point) > metrics.sampleStep) {
      addEntity(draft, {
        id: createEntityId(), kind: "rectangle",
        x: Math.min(start[0], point[0]), y: Math.min(start[1], point[1]),
        width: Math.abs(point[0] - start[0]), height: Math.abs(point[1] - start[1]),
        radius: 0, closed: true,
      });
    }
    state.command = null;
    state.tool = "select";
    return;
  }
  if (tool === "circle") {
    const center = command.points[0];
    const radius = pointDistance2d(center, point);
    if (radius > metrics.sampleStep) addEntity(draft, { id: createEntityId(), kind: "circle", cx: center[0], cy: center[1], radius, closed: true });
    state.command = null;
    state.tool = "select";
    return;
  }
  if (tool === "arc") {
    if (command.points.some((p) => pointDistance2d(p, point) < 1e-8)) {
      command.error = "三点圆弧的三个点不能重合，请重新指定。";
      state.command = command;
      return;
    }
    if (command.points.length >= 2) {
      const arc = arcThroughPoints(command.points[0], command.points[1], point);
      if (!arc) {
        command.error = "三点共线，无法确定圆弧，请重新指定终点。";
        state.command = command;
        return;
      }
      addEntity(draft, { ...arc, id: createEntityId() }, state.snapEnabled && state.mode === SECTION_MODE);
      state.command = null;
      state.tool = "select";
    } else {
      command.points.push(point);
      delete command.error;
      state.command = command;
    }
  }
}

export function finishCadCommand(state, draft, metrics) {
  const command = state.command;
  if (!command) return;
  if (command.tool === "polyline" && command.points.length >= 2) {
    addEntity(draft, { id: createEntityId(), kind: "polyline", points: deduplicatePoints(command.points), closed: false }, state.snapEnabled && state.mode === SECTION_MODE);
  } else if (command.tool === "spline" && command.points.length >= 3) {
    addEntity(draft, { id: createEntityId(), kind: "spline", points: deduplicatePoints(command.points), closed: false }, state.snapEnabled && state.mode === SECTION_MODE);
  } else if (command.tool === "line" && command.points.length === 1) {
    void metrics;
  }
  state.command = null;
  state.tool = "select";
}

function commandBasePoint(command) {
  return Array.isArray(command?.points) && command.points.length ? command.points.at(-1) : null;
}

function commandPrompt(command) {
  if (command?.error) return command.error;
  const count = command?.points?.length ?? 0;
  if (command?.tool === "line") return count ? "直线：指定下一点 · Enter/右键完成 · Esc 取消" : "直线：指定第一点";
  if (command?.tool === "polyline") return count ? "多段线：指定下一点 · 点击起点闭合 · Enter 完成" : "多段线：指定第一点";
  if (command?.tool === "spline") return count ? `样条：已指定 ${count} 点 · Enter 完成` : "样条：指定第一点";
  if (command?.tool === "rectangle") return "矩形：指定另一个角点";
  if (command?.tool === "circle") return "圆：指定半径点";
  if (command?.tool === "arc") return count === 1 ? "三点圆弧：指定圆弧上的第二点" : "三点圆弧：指定终点";
  return `${toolLabel(command?.tool)}：指定点`;
}

function renderCommandPreview(svg, command, cursor, mode, metrics) {
  const host = svg.querySelector("[data-tube-sketch-draft-preview]");
  if (!host) return;
  const points = [...command.points, cursor];
  let entity = null;
  if (command.tool === "line" && points.length >= 2) {
    const start = command.points.at(-1);
    entity = { id: "command-preview", kind: "line", x1: start[0], y1: start[1], x2: cursor[0], y2: cursor[1] };
  } else if (command.tool === "rectangle" && points.length >= 2) {
    const start = command.points[0];
    entity = { id: "command-preview", kind: "rectangle", x: Math.min(start[0], cursor[0]), y: Math.min(start[1], cursor[1]), width: Math.abs(cursor[0] - start[0]), height: Math.abs(cursor[1] - start[1]), radius: 0, closed: true };
  } else if (command.tool === "circle" && points.length >= 2) {
    const center = command.points[0];
    entity = { id: "command-preview", kind: "circle", cx: center[0], cy: center[1], radius: pointDistance2d(center, cursor), closed: true };
  } else if (command.tool === "arc" && command.points.length === 1) {
    entity = { id: "command-preview", kind: "line", x1: command.points[0][0], y1: command.points[0][1], x2: cursor[0], y2: cursor[1] };
  } else if (command.tool === "arc" && command.points.length >= 2) {
    const [start, middle] = command.points;
    const arc = arcThroughPoints(start, middle, cursor);
    entity = arc ? { ...arc, id: "command-preview" } : null;
  } else if ((command.tool === "polyline" || command.tool === "spline") && points.length >= 2) {
    entity = { id: "command-preview", kind: command.tool, points, closed: false };
  }
  host.innerHTML = entity ? renderSketchEntity(entity, mode, metrics, false) : "";
}

function resolveCadPoint(draft, raw, metrics, state, base = null, excludeEntityId = "") {
  const additional = state.command?.points?.length > 2
    ? [{ point: state.command.points[0], type: "endpoint", label: "闭合" }]
    : [];
  const snap = state.snapEnabled ? findSnapCandidate(draft, raw, metrics.snapTolerance, excludeEntityId, additional) : null;
  if (snap) return { point: snap.point, snap };
  if (state.orthoEnabled && base) {
    const dx = raw[0] - base[0];
    const dy = raw[1] - base[1];
    return { point: Math.abs(dx) >= Math.abs(dy) ? [raw[0], base[1]] : [base[0], raw[1]], snap: null };
  }
  return { point: raw, snap: null };
}

export function findSnapCandidate(draft, point, tolerance, excludeEntityId = "", additional = []) {
  const candidates = [...additional];
  const segments = [];
  for (const entity of draft?.entities ?? []) {
    if (entity.id === excludeEntityId || entity.kind === "text") continue;
    candidates.push(...entitySnapCandidates(entity, point));
    segments.push(...entityLineSegments(entity));
  }
  if (segments.length <= 240) {
    for (let left = 0; left < segments.length; left += 1) {
      for (let right = left + 1; right < segments.length; right += 1) {
        if (segments[left].entityId === segments[right].entityId) continue;
        const intersection = segmentIntersectionPoint(segments[left].start, segments[left].end, segments[right].start, segments[right].end);
        if (intersection) candidates.push({ point: intersection, type: "intersection", label: "交点" });
      }
    }
  }
  const priorities = { endpoint: 0, intersection: 1, center: 2, midpoint: 3, quadrant: 4, nearest: 5 };
  return candidates
    .map((candidate) => ({ ...candidate, distance: pointDistance2d(point, candidate.point) }))
    .filter((candidate) => candidate.distance <= tolerance)
    .sort((left, right) => (priorities[left.type] ?? 9) - (priorities[right.type] ?? 9) || left.distance - right.distance)[0] ?? null;
}

function entitySnapCandidates(entity, cursor) {
  const candidates = [];
  const add = (point, type, label) => candidates.push({ point: point.map(Number), type, label });
  if(isConic(entity)||entity.kind==="path"||entity.kind==="spline"||entity.kind==="arc") {
    const segments=editableSegments(entity);
    if(!segments.length)return candidates;
    if(entity.kind==="path")pathNodes(entity).forEach(p=>add(p,"endpoint","连接点"));
    else if(!isClosedBoundaryEntity(entity)) {
      add(curvePoint(segments[0],0),"endpoint","端点");
      add(curvePoint(segments.at(-1),1),"endpoint","端点");
    }
    if(isConic(entity))add([entity.cx,entity.cy],"center",entity.kind.includes("circle")?"圆心":"中心");
    if(entity.kind==="circle"||entity.kind==="ellipse")for(const t of [0,.25,.5,.75])add(curvePoint(segments[0],t),"quadrant","象限点");
    else segments.forEach(s=>add(curvePoint(s,.5),"midpoint","中点"));
    const hit=nearestPath(entity,cursor);
    if(hit)add(hit.point,"nearest","最近点");
    return candidates;
  }
  if (entity.kind === "line") {
    const start = [entity.x1, entity.y1].map(Number);
    const end = [entity.x2, entity.y2].map(Number);
    add(start, "endpoint", "端点"); add(end, "endpoint", "端点");
    add([(start[0] + end[0]) / 2, (start[1] + end[1]) / 2], "midpoint", "中点");
  } else if (entity.kind === "rectangle") {
    const points = entityPolygon(entity);
    points.forEach((value) => add(value, "endpoint", "角点"));
    add([entity.x + entity.width / 2, entity.y + entity.height / 2], "center", "中心");
    points.forEach((value, index) => {
      const next = points[(index + 1) % points.length];
      add([(value[0] + next[0]) / 2, (value[1] + next[1]) / 2], "midpoint", "中点");
    });
  } else if (entity.kind === "circle") {
    const center = [Number(entity.cx), Number(entity.cy)];
    const radius = Number(entity.radius);
    add(center, "center", "圆心");
    [[radius, 0], [0, radius], [-radius, 0], [0, -radius]].forEach(([x, y]) => add([center[0] + x, center[1] + y], "quadrant", "象限点"));
    const dx = cursor[0] - center[0];
    const dy = cursor[1] - center[1];
    const length = Math.hypot(dx, dy);
    if (length > 1e-9) add([center[0] + dx / length * radius, center[1] + dy / length * radius], "nearest", "最近点");
  } else if (["ellipse", "circleArc", "ellipseArc"].includes(entity.kind)) {
    const points = entityBoundaryPoints(entity);
    if (!isClosedBoundaryEntity(entity) && points.length) {
      add(points[0], "endpoint", "端点");
      add(points.at(-1), "endpoint", "端点");
    }
    if (entity.kind !== "circleArc") add([Number(entity.cx), Number(entity.cy)], "center", "中心");
  } else {
    const points = entityBoundaryPoints(entity);
    if (points.length) {
      selectableEntityPoints(entity).forEach(value => add(value.point, "endpoint", "节点"));
      add(points[0], "endpoint", "端点");
      if (!entity.closed) add(points.at(-1), "endpoint", "端点");
      const limit = Math.min(points.length - 1, 160);
      const step = Math.max(1, Math.ceil((points.length - 1) / Math.max(1, limit)));
      for (let index = 0; index + 1 < points.length; index += step) {
        const next = points[Math.min(points.length - 1, index + step)];
        add([(points[index][0] + next[0]) / 2, (points[index][1] + next[1]) / 2], "midpoint", "中点");
      }
    }
  }
  for (const segment of entityLineSegments(entity)) {
    const projected = projectPointToSegment(cursor, segment.start, segment.end);
    add(projected.point, "nearest", "最近点");
  }
  return candidates;
}

function entityLineSegments(entity) {
  const points = entityBoundaryPoints(entity);
  if (points.length < 2) return [];
  const closed = isClosedBoundaryEntity(entity);
  const count = closed ? points.length : points.length - 1;
  const step = Math.max(1, Math.ceil(count / 160));
  const result = [];
  for (let index = 0; index < count; index += step) {
    const endIndex = Math.min(count, index + step);
    result.push({
      entityId: entity.id,
      start: points[index % points.length],
      end: points[endIndex % points.length],
    });
  }
  return result;
}

function segmentIntersectionPoint(a, b, c, d) {
  const denominator = (a[0] - b[0]) * (c[1] - d[1]) - (a[1] - b[1]) * (c[0] - d[0]);
  if (Math.abs(denominator) <= 1e-12) return null;
  const determinant1 = a[0] * b[1] - a[1] * b[0];
  const determinant2 = c[0] * d[1] - c[1] * d[0];
  const point = [
    (determinant1 * (c[0] - d[0]) - (a[0] - b[0]) * determinant2) / denominator,
    (determinant1 * (c[1] - d[1]) - (a[1] - b[1]) * determinant2) / denominator,
  ];
  const within = (value, left, right) => value >= Math.min(left, right) - 1e-8 && value <= Math.max(left, right) + 1e-8;
  return within(point[0], a[0], b[0]) && within(point[1], a[1], b[1])
    && within(point[0], c[0], d[0]) && within(point[1], c[1], d[1])
    ? point.map(roundCoordinate) : null;
}

function renderCadCursor(svg, resolved, metrics, command) {
  const snapHost = svg.querySelector("[data-tube-sketch-snap-preview]");
  const cursorHost = svg.querySelector("[data-tube-sketch-cursor-preview]");
  const point = modelToCanvas(resolved.point, metrics);
  if (snapHost) snapHost.innerHTML = resolved.snap
    ? `<path d="M${point[0] - 6} ${point[1]}L${point[0]} ${point[1] - 6}L${point[0] + 6} ${point[1]}L${point[0]} ${point[1] + 6}Z"/><text x="${point[0] + 10}" y="${point[1] - 9}">${escapeText(resolved.snap.label)}</text>`
    : "";
  if (cursorHost) {
    const label = command ? commandPrompt(command) : `X ${formatNumber(resolved.point[0])}  Y ${formatNumber(resolved.point[1])}`;
    const width = Math.min(300, Math.max(92, label.length * 6.4));
    const bounds = canvasDrawingBounds(metrics);
    const x = Math.min(bounds.right - width, point[0] + 14);
    const y = Math.max(bounds.top + 24, point[1] - 18);
    cursorHost.innerHTML = `<rect x="${x}" y="${y - 18}" width="${width}" height="24" rx="4"/><text x="${x + 7}" y="${y - 2}">${escapeText(label)}</text>`;
  }
}

function updateCoordinateStatus(mount, point) {
  const status = mount?.querySelector?.("[data-tube-sketch-coordinate-status]");
  if (status) status.textContent = `X ${roundCoordinate(point[0]).toFixed(3)} · Y ${roundCoordinate(point[1]).toFixed(3)}`;
}

function renderSelectionWindow(svg, gesture, metrics) {
  const host = svg.querySelector("[data-tube-sketch-draft-preview]");
  if (!host) return;
  const start = modelToCanvas(gesture.start, metrics);
  const end = modelToCanvas(gesture.current, metrics);
  const crossing = gesture.current[0] < gesture.start[0];
  host.innerHTML = `<rect class="tube-sketch-selection-window ${crossing ? "crossing" : "enclosed"}" x="${Math.min(start[0], end[0])}" y="${Math.min(start[1], end[1])}" width="${Math.abs(end[0] - start[0])}" height="${Math.abs(end[1] - start[1])}"/>`;
}

export function selectEntitiesInWindow(entities, start, end) {
  if (pointDistance2d(start, end) <= 1e-5) return [];
  const windowBounds = { minX: Math.min(start[0], end[0]), minY: Math.min(start[1], end[1]), maxX: Math.max(start[0], end[0]), maxY: Math.max(start[1], end[1]) };
  const crossing = end[0] < start[0];
  return entities.filter((entity) => {
    const bounds = entityBounds(entity);
    return crossing
      ? bounds.maxX >= windowBounds.minX && bounds.minX <= windowBounds.maxX && bounds.maxY >= windowBounds.minY && bounds.minY <= windowBounds.maxY
      : bounds.minX >= windowBounds.minX && bounds.maxX <= windowBounds.maxX && bounds.minY >= windowBounds.minY && bounds.maxY <= windowBounds.maxY;
  }).map((entity) => entity.id);
}

export function selectControlPointsInWindow(entities, entityIds, start, end) {
  if (pointDistance2d(start, end) <= 1e-5) return [];
  const allowed = new Set((Array.isArray(entityIds) ? entityIds : []).map(String));
  if (!allowed.size) return [];
  const bounds = {
    minX: Math.min(start[0], end[0]), minY: Math.min(start[1], end[1]),
    maxX: Math.max(start[0], end[0]), maxY: Math.max(start[1], end[1]),
  };
  const result = [];
  for (const entity of Array.isArray(entities) ? entities : []) {
    if (!allowed.has(String(entity?.id ?? ""))) continue;
    for (const value of selectableEntityPoints(entity)) {
      const point = value.point;
      if (point[0] >= bounds.minX && point[0] <= bounds.maxX
          && point[1] >= bounds.minY && point[1] <= bounds.maxY) {
        result.push({ entityId: String(entity.id), index: value.index });
      }
    }
  }
  return result;
}

function selectableEntityPoints(entity) {
  if (entity?.kind === "path") return pathNodes(entity).map((point,index)=>({point,index}));
  if (entity?.kind === "line") {
    return [
      { index: 0, point: [Number(entity.x1), Number(entity.y1)] },
      { index: 1, point: [Number(entity.x2), Number(entity.y2)] },
    ];
  }
  if (entity?.kind === "rectangle") {
    const x = Number(entity.x); const y = Number(entity.y);
    const width = Number(entity.width); const height = Number(entity.height);
    return [
      { index: 0, point: [x, y + height] },
      { index: 1, point: [x + width, y + height] },
      { index: 2, point: [x + width, y] },
      { index: 3, point: [x, y] },
    ];
  }
  if (entity?.kind === "circle") {
    return [{ index: 0, point: [Number(entity.cx) + Number(entity.radius), Number(entity.cy)] }];
  }
  if (entity?.kind === "ellipse") {
    return [
      { index: 0, point: analyticCurvePoint(entity, 0) },
      { index: 1, point: analyticCurvePoint(entity, Math.PI / 2) },
    ];
  }
  if (entity?.kind === "circleArc" || entity?.kind === "ellipseArc") {
    return [
      { index: 0, point: analyticCurvePoint(entity, Number(entity.startAngle)) },
      { index: 1, point: analyticCurvePoint(entity, Number(entity.startAngle) + Number(entity.sweep)) },
    ];
  }
  return (Array.isArray(entity?.points) ? entity.points : []).map((point, index) => ({
    index,
    point: [Number(point[0]), Number(point[1])],
  }));
}

export function updateEntityFromGrip(entity, gesture, point) {
  for(const key of Object.keys(entity))delete entity[key];
  Object.assign(entity, structuredCloneValue(gesture.original));
  if (gesture.role === "move") {
    translateEntity(entity, point[0] - gesture.start[0], point[1] - gesture.start[1]);
    return;
  }
  if (entity.kind === "path" && gesture.role === "vertex") {
    Object.assign(entity,movePathNode(entity,gesture.index,point));
    return;
  }
  if(entity.kind==="circleArc"&&gesture.role==="arc-middle") {
    const arc=arcThroughPoints(curvePoint(entity,0),point,curvePoint(entity,1));
    if(arc)Object.assign(entity,arc);
    return;
  }
  if (entity.kind === "line" && gesture.role === "vertex") {
    if (gesture.index === 0) [entity.x1, entity.y1] = point;
    else [entity.x2, entity.y2] = point;
    return;
  }
  if (entity.kind === "circle" && gesture.role === "radius") {
    entity.radius = Math.max(0.001, pointDistance2d([entity.cx, entity.cy], point));
    return;
  }
  if (entity.kind === "ellipse" && (gesture.role === "ellipse-radius-x" || gesture.role === "ellipse-radius-y")) {
    const local = ellipseLocalPoint(entity, point);
    if (gesture.role === "ellipse-radius-x") entity.radiusX = Math.max(0.001, Math.abs(local[0]));
    else entity.radiusY = Math.max(0.001, Math.abs(local[1]));
    return;
  }
  if ((entity.kind === "circleArc" || entity.kind === "ellipseArc") && gesture.role === "vertex") {
    if (Math.abs(entity.sweep) >= FULL_ANGLE-1e-8 && pointDistance2d(point,curvePoint(entity,gesture.index))>1e-8) {
      const half={...entity,sweep:entity.sweep/2};
      const path={id:entity.id,kind:"path",segments:[half,{...half,startAngle:entity.startAngle+half.sweep}],closed:false,brokenStart:entity.brokenStart,brokenEnd:entity.brokenEnd};
      Object.assign(entity,movePathNode(path,gesture.index===0?0:2,point));
    } else Object.assign(entity,moveSegmentEnd(entity,gesture.index===1,point));
    return;
  }
  if (entity.kind === "rectangle" && gesture.role === "corner") {
    const corners = [
      [gesture.original.x, gesture.original.y],
      [gesture.original.x + gesture.original.width, gesture.original.y],
      [gesture.original.x + gesture.original.width, gesture.original.y + gesture.original.height],
      [gesture.original.x, gesture.original.y + gesture.original.height],
    ];
    const opposite = corners[(gesture.index + 2) % 4];
    entity.x = Math.min(point[0], opposite[0]);
    entity.y = Math.min(point[1], opposite[1]);
    entity.width = Math.max(0.001, Math.abs(point[0] - opposite[0]));
    entity.height = Math.max(0.001, Math.abs(point[1] - opposite[1]));
    entity.radius = Math.min(Number(entity.radius ?? 0), entity.width / 2, entity.height / 2);
    return;
  }
  if (Array.isArray(entity.points) && gesture.role === "vertex"
      && gesture.index >= 0 && gesture.index < entity.points.length) entity.points[gesture.index] = point;
}

function translateEntity(entity, dx, dy) {
  if (entity.kind === "path") {
    entity.segments=entity.segments.map(s=>translateSegment(s,dx,dy));
  } else if (entity.kind === "line") {
    entity.x1 += dx; entity.y1 += dy; entity.x2 += dx; entity.y2 += dy;
  } else if (entity.kind === "rectangle") {
    entity.x += dx; entity.y += dy;
  } else if (entity.kind === "circle") {
    entity.cx += dx; entity.cy += dy;
  } else if (["ellipse", "circleArc", "ellipseArc"].includes(entity.kind)) {
    entity.cx += dx; entity.cy += dy;
  } else if (entity.kind === "text") {
    entity.x += dx; entity.y += dy;
  } else if (Array.isArray(entity.points)) {
    entity.points = entity.points.map((point) => [point[0] + dx, point[1] + dy]);
  }
}

function restoreGripGesture(draft, gesture) {
  const index = draft.entities.findIndex((entity) => entity.id === gesture.entityId);
  if (index >= 0) draft.entities[index] = gesture.original;
  draft.history?.pop();
  draft.dirty = gesture.originalDirty;
  draft.future = gesture.originalFuture??[];
}

function zoomSectionViewportAtPointer(state, svg, event, metrics) {
  const local = eventToCanvasPoint(svg, event, metrics);
  const anchor = canvasToModel(local, metrics);
  const viewport = normalizeSectionViewport(state.sectionViewport);
  const nextZoom = Math.min(SECTION_MAX_ZOOM, Math.max(SECTION_MIN_ZOOM, viewport.zoom * (event.deltaY < 0 ? 1.18 : 1 / 1.18)));
  const halfWidth = SECTION_HALF_WIDTH / nextZoom;
  const halfHeight = halfWidth * (state.canvasHeight??CANVAS.height)/CANVAS.width;
  const bounds = canvasDrawingBounds(metrics);
  const fractionX = (local[0] - bounds.left) / (bounds.right - bounds.left);
  const fractionY = (bounds.bottom - local[1]) / (bounds.bottom - bounds.top);
  state.sectionViewport = {
    zoom: nextZoom,
    centerX: anchor[0] - (fractionX - 0.5) * halfWidth * 2,
    centerY: anchor[1] - (fractionY - 0.5) * halfHeight * 2,
  };
}

function eventToCanvasPoint(svg, event, metrics) {
  const point = svg.createSVGPoint();
  point.x = event.clientX;
  point.y = event.clientY;
  const local = point.matrixTransform(svg.getScreenCTM().inverse());
  if(metrics.canvasLeft===0)return [local.x,local.y];
  const bounds = canvasDrawingBounds(metrics);
  return [
    Math.min(bounds.right, Math.max(bounds.left, local.x)),
    Math.min(bounds.bottom, Math.max(bounds.top, local.y)),
  ];
}

function pointDistance2d(left, right) {
  return Math.hypot(Number(left?.[0]) - Number(right?.[0]), Number(left?.[1]) - Number(right?.[1]));
}

function structuredCloneValue(value) {
  return typeof globalThis.structuredClone === "function"
    ? globalThis.structuredClone(value)
    : JSON.parse(JSON.stringify(value));
}

export function buildProfileFromSectionDraft(draft, name = "我的草图管型") {
  const analysis = analyzeSectionDraft(draft);
  if (!analysis.ready) throw new Error(analysis.message);
  const polygons = analysis.loops;
  const all = polygons.flat();
  const minX = Math.min(...all.map((point) => point[0]));
  const maxX = Math.max(...all.map((point) => point[0]));
  const minY = Math.min(...all.map((point) => point[1]));
  const maxY = Math.max(...all.map((point) => point[1]));
  const width = maxX - minX;
  const depth = maxY - minY;
  if (!(width > 0.001 && depth > 0.001)) throw new Error("截面尺寸过小，无法生成管型。");
  const centerX = (minX + maxX) / 2;
  const centerY = (minY + maxY) / 2;
  const wallThickness = polygons.length > 1 ? approximateWallThickness(polygons[0], polygons.slice(1)) : 0;
  const geometryEntities = (Array.isArray(draft?.entities) ? draft.entities : []).filter((entity) => entity?.kind !== "text");
  const closedEntities=geometryEntities.filter(isClosedBoundaryEntity);
  const remaining=geometryEntities.filter(e=>!isClosedBoundaryEntity(e)).map(e=>editablePath(e));
  while(remaining.length) {
    let path=remaining.shift();
    let changed=true;
    while(changed&&!closeEditablePath(path,SECTION_JOIN_TOLERANCE)) {
      changed=false;
      for(let i=0;i<remaining.length&&!changed;i++) {
        for(const reverse of [false,true]) {
          const candidate=editablePath(remaining[i],reverse);
          const joined=joinPathsAtEndpoints(path,candidate,SECTION_JOIN_TOLERANCE)??joinPathsAtEndpoints(candidate,path,SECTION_JOIN_TOLERANCE);
          if(joined){path=joined;remaining.splice(i,1);changed=true;break;}
        }
      }
    }
    const closed=closeEditablePath(path,SECTION_JOIN_TOLERANCE);
    if(!closed)throw new Error("截面曲线端点无法精确连接，请合并端点后保存。");
    closedEntities.push(compactEditablePath(closed));
  }
  closedEntities.sort((a,b)=>Math.abs(signedArea(entityBoundaryPoints(b)))-Math.abs(signedArea(entityBoundaryPoints(a))));
  const preservedContours = closedEntities.map((entity) => profileContourFromClosedEntity(entity, centerX, centerY));
  const contours = preservedContours.length === polygons.length && preservedContours.every(Boolean)
    ? preservedContours
    : polygons.map((points) => ({
    kind: "path",
    segments: points.map((point, index) => ({
      kind: "line",
      start: [point[0] - centerX, point[1] - centerY],
      end: [points[(index + 1) % points.length][0] - centerX, points[(index + 1) % points.length][1] - centerY],
    })),
  }));
  return {
    schema: "icax.imported-tube-profile",
    schemaVersion: 1,
    kind: "imported-dxf",
    name,
    sourceFileName: `${name}.sketch`,
    sourceFormat: SKETCH_SCHEMA,
    sourceUnit: "毫米",
    unitScaleToMillimeter: 1,
    width,
    depth,
    wallThickness,
    cornerRadius: 0,
    wallThicknessApproximate: contours.length > 1,
    specification: `草图 ${formatNumber(width)} × ${formatNumber(depth)} mm`,
    hollow: contours.length > 1,
    contourCount: contours.length,
    contours,
  };
}

function profileContourFromClosedEntity(entity, centerX, centerY) {
  if (!isClosedBoundaryEntity(entity)) return null;
  if(entity.kind==="path")return {kind:"path",segments:entity.segments.flatMap(s=>profileSegmentsFromEditable(s,centerX,centerY))};
  // Neutral-model primitive contours are centered at the placement origin. Use
  // explicit curved edges for translated/rotated geometry instead of ignored fields.
  if(entity.kind==="ellipse"||(entity.kind==="circle"&&Math.hypot(entity.cx-centerX,entity.cy-centerY)>1e-9)
    ||entity.kind==="rectangle"||entity.kind==="spline"||entity.kind==="arc") {
    return {kind:"path",segments:editableSegments(entity).flatMap(s=>profileSegmentsFromEditable(s,centerX,centerY))};
  }
  if (entity.kind === "circle") {
    return { kind: "circle", center: [Number(entity.cx) - centerX, Number(entity.cy) - centerY], radius: Number(entity.radius) };
  }
  const points = entityBoundaryPoints(entity);
  if (points.length >= 3) {
    return {
      kind: "path",
      segments: points.map((point, index) => ({
        kind: "line",
        start: [point[0] - centerX, point[1] - centerY],
        end: [points[(index + 1) % points.length][0] - centerX, points[(index + 1) % points.length][1] - centerY],
      })),
    };
  }
  return null;
}

function profileSegmentsFromEditable(segment,dx,dy) {
  const s=translateSegment(segment,-dx,-dy);
  if(s.kind==="line")return [{kind:"line",start:curvePoint(s,0),end:curvePoint(s,1)}];
  if(s.kind==="bezier")return [{kind:"bezier",controlPoints:s.points}];
  if(s.kind==="circleArc") {
    if(Math.abs(s.sweep)>Math.PI*1.99) {
      const first={...s,sweep:s.sweep/2},second={...first,startAngle:s.startAngle+first.sweep};
      return [first,second].flatMap(s=>profileSegmentsFromEditable(s,0,0));
    }
    return [{kind:"arc",start:curvePoint(s,0),middle:curvePoint(s,.5),end:curvePoint(s,1)}];
  }
  const swap=s.radiusY>s.radiusX;
  const start=s.startAngle-(swap?Math.PI/2:0);
  return [{kind:"ellipseArc",center:[s.cx,s.cy],majorRadius:Math.max(s.radiusX,s.radiusY),minorRadius:Math.min(s.radiusX,s.radiusY),rotation:(s.rotation??0)+(swap?Math.PI/2:0),startAngle:s.sweep>0?start:start+s.sweep,endAngle:s.sweep>0?start+s.sweep:start}];
}

export function analyzeSectionDraft(draft) {
  const entities = Array.isArray(draft?.entities) ? draft.entities : [];
  const geometry = entities.filter((entity) => entity?.kind !== "text");
  if (!geometry.length) {
    return { ready: false, kind: "empty", message: "还没有截面", loops: [], holeCount: 0 };
  }
  if (geometry.length > MAX_SKETCH_ENTITIES) {
    return { ready: false, kind: "invalid", message: `截面图形不能超过 ${MAX_SKETCH_ENTITIES} 个`, loops: [], holeCount: 0 };
  }

  const loops = [];
  const openPaths = [];
  let intentionalOpenEndpointCount = 0;
  for (let index = 0; index < geometry.length; index += 1) {
    const entity = geometry[index];
    const issue = entityGeometryIssue(entity);
    if (issue) {
      return { ready: false, kind: "invalid", message: `第 ${index + 1} 个图形${issue}`, loops: [], holeCount: 0 };
    }
    const points = entityBoundaryPoints(entity);
    if (points.length < 2) {
      return { ready: false, kind: "invalid", message: `第 ${index + 1} 个图形没有有效边界`, loops: [], holeCount: 0 };
    }
    if (isClosedBoundaryEntity(entity)) {
      const loop = normalizeLoop(points);
      if (loop.length < 3) {
        return { ready: false, kind: "invalid", message: `第 ${index + 1} 个闭合图形至少需要 3 个有效点`, loops: [], holeCount: 0 };
      }
      loops.push(loop);
    } else {
      openPaths.push(points);
      intentionalOpenEndpointCount += Number(entity.brokenStart === true) + Number(entity.brokenEnd === true);
    }
  }

  if (intentionalOpenEndpointCount) {
    return {
      ready: false,
      kind: "open",
      message: `截面尚未闭合：还有 ${intentionalOpenEndpointCount} 个开放端点`,
      loops: [],
      holeCount: 0,
    };
  }

  const joined = joinOpenBoundaryPaths(openPaths, SECTION_JOIN_TOLERANCE);
  if (joined.branched) {
    return { ready: false, kind: "open", message: "截面边界存在分叉，请让每个端点只连接两条边", loops: [], holeCount: 0 };
  }
  loops.push(...joined.loops);
  if (joined.openEndpointCount) {
    return {
      ready: false,
      kind: "open",
      message: `截面尚未闭合：还有 ${joined.openEndpointCount} 个开放端点`,
      loops: [],
      holeCount: 0,
    };
  }
  if (!loops.length) {
    return { ready: false, kind: "open", message: "截面尚未闭合", loops: [], holeCount: 0 };
  }

  for (let index = 0; index < loops.length; index += 1) {
    if (polygonSelfIntersects(loops[index])) {
      return { ready: false, kind: "invalid", message: `第 ${index + 1} 条轮廓存在自相交`, loops: [], holeCount: 0 };
    }
    if (Math.abs(signedArea(loops[index])) <= 1e-6) {
      return { ready: false, kind: "invalid", message: `第 ${index + 1} 条轮廓面积为零`, loops: [], holeCount: 0 };
    }
  }

  loops.sort((left, right) => Math.abs(signedArea(right)) - Math.abs(signedArea(left)));
  const outer = loops[0];
  const holes = loops.slice(1);
  for (let index = 0; index < holes.length; index += 1) {
    const hole = holes[index];
    if (polygonsIntersect(outer, hole) || !pointInPolygon(outer, hole[0])) {
      return { ready: false, kind: "invalid", message: "一个管型只能有一个外轮廓，其余轮廓必须完全位于外轮廓内", loops: [], holeCount: 0 };
    }
    for (let other = 0; other < index; other += 1) {
      if (polygonsIntersect(holes[other], hole)
        || pointInPolygon(holes[other], hole[0])
        || pointInPolygon(hole, holes[other][0])) {
        return { ready: false, kind: "invalid", message: "内孔不能相交或互相嵌套", loops: [], holeCount: 0 };
      }
    }
  }

  const ordered = [orientLoop(outer, true), ...holes.map((hole) => orientLoop(hole, false))];
  return {
    ready: true,
    kind: "valid",
    message: holes.length ? `截面有效：1 个外轮廓，${holes.length} 个内孔` : "截面有效：1 个外轮廓",
    loops: ordered,
    holeCount: holes.length,
  };
}

export function validateSideSketchDraft(draft, member, reference = null) {
  if (!member) return { ready: false, kind: "empty", message: "请先在产品页生成并选择一根管件" };
  const entities = Array.isArray(draft?.entities) ? draft.entities : [];
  if (!entities.length) {
    return {
      ready: false,
      kind: draft?.persisted && draft?.dirty ? "remove" : "empty",
      message: draft?.persisted && draft?.dirty ? "草图已删空，确认后将移除已应用的侧面草图" : "还没有侧面切割图",
    };
  }
  if (entities.length > MAX_SKETCH_ENTITIES) {
    return { ready: false, kind: "invalid", message: `侧面草图不能超过 ${MAX_SKETCH_ENTITIES} 个图形` };
  }
  const length = sideUnfoldingLength(member, reference);
  const height = Math.max(
    1,
    sideUnfoldingPerimeter(reference)
      || Number(reference?.sideProjection?.height)
      || memberFaceHeight(member),
  );
  const ids = new Set();
  for (let index = 0; index < entities.length; index += 1) {
    const entity = entities[index];
    const id = String(entity?.id ?? "");
    if (!id || ids.has(id)) return { ready: false, kind: "invalid", message: `第 ${index + 1} 个图形的标识无效或重复` };
    ids.add(id);
    const issue = entityGeometryIssue(entity);
    if (issue) return { ready: false, kind: "invalid", message: `第 ${index + 1} 个图形${issue}` };
    const bounds = entityBounds(entity);
    if (![bounds.minX, bounds.minY, bounds.maxX, bounds.maxY].every(Number.isFinite)) {
      return { ready: false, kind: "invalid", message: `第 ${index + 1} 个图形包含无效坐标` };
    }
    const tolerance = Math.max(1e-6, length * 1e-9);
    if (bounds.minX < -tolerance || bounds.minY < -tolerance
      || bounds.maxX > length + tolerance || bounds.maxY > height + tolerance) {
      return { ready: false, kind: "invalid", message: `第 ${index + 1} 个图形超出当前管件的可绘制区域` };
    }
  }
  return { ready: true, kind: "valid", message: `图形有效，可应用到当前管件（${formatNumber(length)} × ${formatNumber(height)} mm）` };
}

function renderSketchValidation(validation, mode, draft) {
  const tone = validation.ready ? "valid" : (validation.kind === "empty" ? "neutral" : (validation.kind === "remove" ? "warning" : "invalid"));
  const title = validation.ready
    ? (mode === SECTION_MODE ? "截面检查通过" : "切割图检查通过")
    : (validation.kind === "empty" ? "等待绘制" : (validation.kind === "remove" && draft?.persisted ? "准备移除" : "需要处理"));
  return `<section class="tube-sketch-validation ${tone}" data-tube-sketch-validation="${tone}">
    <span aria-hidden="true">${validation.ready ? "✓" : (tone === "neutral" ? "○" : "!")}</span>
    <div><strong>${title}</strong><small>${escapeText(validation.message)}</small></div>
  </section>`;
}

function renderSectionSession(state, pending, componentProfile = false) {
  const session = normalizeSectionSession(state.sectionSession);
  const copy = componentProfile ? {
    title: "当前拉伸体截面",
    detail: "确认后回填到 CSG 节点；完成整个零件前不会写入“我的配件”",
  } : ({
    [SECTION_SESSION_CREATE]: {
      title: "新增管型",
      detail: "从空白截面开始，确认后保存到“我的管型”",
    },
    [SECTION_SESSION_UPDATE]: {
      title: "修改我的 定式管型",
      detail: `确认时可选择覆盖“${session.sourceName || state.sectionName}”或另存为新管型`,
    },
    [SECTION_SESSION_COPY]: {
      title: "基于现有管型编辑",
      detail: `原管型“${session.sourceName || ""}”保持不变，确认后新建到“我的管型”`,
    },
  }[session.kind]);
  return `<section class="tube-sketch-section-session" data-tube-sketch-section-session="${escapeAttr(session.kind)}">
    <div><strong>${escapeText(copy.title)}</strong><small>${escapeText(copy.detail)}</small></div>
    <label><span>${componentProfile ? "截面名称" : "保存名称"}</span><input type="text" maxlength="120" value="${escapeAttr(state.sectionName)}" data-cam-change-action="tube-designer-sketch-section-name" ${pending ? "disabled" : ""}/></label>
  </section>`;
}

function renderSideReferenceSummary(reference) {
  const projection = reference?.sideProjection ?? {};
  const unfolding = sideUnfoldingSurface(reference);
  const edgeCount = Array.isArray(projection.segments) ? projection.segments.length : 0;
  const holeCount = Array.isArray(reference?.holes) ? reference.holes.length : 0;
  const rectangle = sideUnfoldingRectangle(reference);
  if (reference?.unfolding?.available === true && rectangle) {
    const wireCount = Array.isArray(unfolding?.wires) ? unfolding.wires.length : 0;
    return `<section class="tube-sketch-side-reference-summary">
      <div><strong>整圈侧壁矩形展开</strong><small>不是单个面：横向 S 是管材轴向长度，纵向 U 是整圈截面弧长；U=0 与 U=周长是同一条接缝。底图只用于定位，不会作为新草图图形保存</small></div>
      <dl>
        <div><dt>展开矩形</dt><dd>${formatNumber(rectangle.width)} × ${formatNumber(rectangle.height)} mm</dd></div>
        <div><dt>端部/特征边界</dt><dd>${wireCount} 条</dd></div>
      </dl>
    </section>`;
  }
  return `<section class="tube-sketch-side-reference-summary">
    <div><strong>最终零件底图</strong><small>底图只用于定位，不会作为新草图图形保存</small></div>
    <dl>
      <div><dt>侧面区域</dt><dd>${formatNumber(projection.width)} × ${formatNumber(projection.height)} mm</dd></div>
      <div><dt>直线棱</dt><dd>${edgeCount} 段</dd></div>
      <div><dt>现有孔</dt><dd>${holeCount} 个</dd></div>
    </dl>
  </section>`;
}

function renderSectionSaveChoiceDialog(state) {
  const session = normalizeSectionSession(state.sectionSession);
  const name = String(session.sourceName || state.sectionName || "当前 定式管型");
  return `<div class="tube-sketch-save-choice-backdrop" role="presentation">
    <section class="tube-sketch-save-choice" role="dialog" aria-modal="true" aria-labelledby="tube-sketch-save-choice-title">
      <header><strong id="tube-sketch-save-choice-title">保存截面修改</strong><span>“${escapeText(name)}”是已有的 定式管型</span></header>
      <p>请选择覆盖原管型，或者保留原管型并新增一份 定式管型。</p>
      <div>
        <button type="button" class="tube-designer-primary" data-cam-action="tube-designer-sketch-save-overwrite">覆盖原管型</button>
        <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-sketch-save-copy">另存为新管型</button>
        <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-sketch-save-choice-cancel">返回草图</button>
      </div>
    </section>
  </div>`;
}

function isClosedBoundaryEntity(entity) {
  return entity.kind === "rectangle" || entity.kind === "circle" || entity.kind === "ellipse" || Boolean(entity.closed);
}

function entityGeometryIssue(entity) {
  if (!entity || !DRAW_TOOLS.has(String(entity.kind ?? ""))) return "的类型不受支持";
  const finite = (...values) => values.every((value) => Number.isFinite(Number(value)));
  if (entity.kind === "path") {
    if (!entity.segments?.length || entity.segments.length > MAX_ENTITY_POINTS) return "缺少有效曲线段";
    if (entity.segments.some(s=>!["line","bezier","circleArc","ellipseArc"].includes(s?.kind)
      || (s.kind!=="bezier" && entityGeometryIssue(s))
      || (s.kind==="bezier" && (!s.points?.length || !s.points.every(p=>finite(...p)))))) return "包含无效曲线段";
    for(let i=1;i<entity.segments.length;i++)if(pointDistance2d(curvePoint(entity.segments[i-1],1),curvePoint(entity.segments[i],0))>1e-6)return "的曲线段未连接";
    return "";
  }
  if (entity.kind === "line") {
    if (!finite(entity.x1, entity.y1, entity.x2, entity.y2)) return "包含无效坐标";
    if (Math.hypot(Number(entity.x2) - Number(entity.x1), Number(entity.y2) - Number(entity.y1)) <= 1e-6) return "是零长度直线";
    return "";
  }
  if (entity.kind === "rectangle") {
    if (!finite(entity.x, entity.y, entity.width, entity.height, entity.radius ?? 0)) return "包含无效尺寸";
    if (!(Number(entity.width) > 1e-6 && Number(entity.height) > 1e-6)) return "的宽度和高度必须大于零";
    if (Number(entity.radius ?? 0) < 0) return "的圆角半径不能小于零";
    return "";
  }
  if (entity.kind === "circle") {
    if (!finite(entity.cx, entity.cy, entity.radius)) return "包含无效尺寸";
    if (!(Number(entity.radius) > 1e-6)) return "的半径必须大于零";
    return "";
  }
  if (entity.kind === "ellipse") {
    if (!finite(entity.cx, entity.cy, entity.radiusX, entity.radiusY, entity.rotation ?? 0)) return "包含无效尺寸";
    if (!(Number(entity.radiusX) > 1e-6 && Number(entity.radiusY) > 1e-6)) return "的长短轴必须大于零";
    return "";
  }
  if (entity.kind === "circleArc") {
    if (!finite(entity.cx, entity.cy, entity.radius, entity.startAngle, entity.sweep)) return "包含无效圆弧参数";
    if (!(Number(entity.radius) > 1e-6) || Math.abs(Number(entity.sweep)) <= 1e-9) return "的半径和扫掠角必须大于零";
    return "";
  }
  if (entity.kind === "ellipseArc") {
    if (!finite(entity.cx, entity.cy, entity.radiusX, entity.radiusY, entity.rotation ?? 0, entity.startAngle, entity.sweep)) return "包含无效椭圆弧参数";
    if (!(Number(entity.radiusX) > 1e-6 && Number(entity.radiusY) > 1e-6) || Math.abs(Number(entity.sweep)) <= 1e-9) return "的长短轴和扫掠角必须大于零";
    return "";
  }
  if (entity.kind === "text") {
    if (!finite(entity.x, entity.y)) return "包含无效坐标";
    if (!String(entity.value ?? "").trim()) return "的文字内容不能为空";
    return "";
  }
  if (!Array.isArray(entity.points)) return "缺少控制点";
  const minimum = entity.kind === "arc" || entity.kind === "spline" ? 3 : 2;
  if (entity.points.length < minimum || entity.points.length > MAX_ENTITY_POINTS) return `需要 ${minimum} 至 ${MAX_ENTITY_POINTS} 个控制点`;
  if (!entity.points.every((point) => Array.isArray(point) && point.length >= 2 && finite(point[0], point[1]))) return "包含无效控制点";
  if (entityLength(entity) <= 1e-6) return "的长度必须大于零";
  return "";
}

function entityBoundaryPoints(entity) {
  if (["path","spline","arc"].includes(entity.kind)) return pathSamples(entity);
  if (entity.kind === "line") return [[Number(entity.x1), Number(entity.y1)], [Number(entity.x2), Number(entity.y2)]];
  if (["rectangle", "circle", "ellipse"].includes(entity.kind)) return entityPolygon(entity);
  if (entity.kind === "circleArc" || entity.kind === "ellipseArc") return sampleAnalyticArc(entity);
  return (entity.points ?? []).map((point) => [Number(point[0]), Number(point[1])]);
}

function normalizeLoop(points) {
  return deduplicatePoints(points);
}

function joinOpenBoundaryPaths(paths, tolerance) {
  const remaining = paths.map((points) => points.map((point) => [...point]));
  const loops = [];
  let openEndpointCount = 0;
  let branched = false;
  while (remaining.length) {
    let chain = remaining.shift();
    let changed = true;
    while (changed) {
      changed = false;
      if (chain.length >= 3 && pointsNear(chain[0], chain.at(-1), tolerance)) break;
      for (const atEnd of [true, false]) {
        const endpoint = atEnd ? chain.at(-1) : chain[0];
        const matches = [];
        for (let index = 0; index < remaining.length; index += 1) {
          if (pointsNear(endpoint, remaining[index][0], tolerance)) matches.push({ index, reverse: false });
          if (pointsNear(endpoint, remaining[index].at(-1), tolerance)) matches.push({ index, reverse: true });
        }
        const uniqueMatches = matches.filter((match, index) => matches.findIndex((candidate) => candidate.index === match.index) === index);
        if (uniqueMatches.length > 1) {
          branched = true;
          return { loops, openEndpointCount, branched };
        }
        if (!uniqueMatches.length) continue;
        const match = uniqueMatches[0];
        let candidate = remaining.splice(match.index, 1)[0];
        if (match.reverse) candidate = candidate.reverse();
        if (atEnd) {
          candidate[0] = [...endpoint];
          chain.push(...candidate.slice(1));
        } else {
          candidate = candidate.reverse();
          candidate[candidate.length - 1] = [...endpoint];
          chain.unshift(...candidate.slice(0, -1));
        }
        changed = true;
        break;
      }
    }
    if (chain.length >= 3 && pointsNear(chain[0], chain.at(-1), tolerance)) {
      chain[chain.length - 1] = [...chain[0]];
      const loop = normalizeLoop(chain);
      if (loop.length >= 3) loops.push(loop);
      else openEndpointCount += 2;
    } else {
      openEndpointCount += 2;
    }
  }
  return { loops, openEndpointCount, branched };
}

function pointsNear(left, right, tolerance = 1e-7) {
  return Math.hypot(Number(left[0]) - Number(right[0]), Number(left[1]) - Number(right[1])) <= tolerance;
}

function orientLoop(points, counterClockwise) {
  const isCounterClockwise = signedArea(points) > 0;
  return isCounterClockwise === counterClockwise ? points : [...points].reverse();
}

function polygonSelfIntersects(points) {
  for (let left = 0; left < points.length; left += 1) {
    const leftNext = (left + 1) % points.length;
    for (let right = left + 1; right < points.length; right += 1) {
      const rightNext = (right + 1) % points.length;
      if (left === right || leftNext === right || rightNext === left) continue;
      if (segmentsIntersect(points[left], points[leftNext], points[right], points[rightNext])) return true;
    }
  }
  return false;
}

function polygonsIntersect(left, right) {
  for (let leftIndex = 0; leftIndex < left.length; leftIndex += 1) {
    for (let rightIndex = 0; rightIndex < right.length; rightIndex += 1) {
      if (segmentsIntersect(
        left[leftIndex], left[(leftIndex + 1) % left.length],
        right[rightIndex], right[(rightIndex + 1) % right.length],
      )) return true;
    }
  }
  return false;
}

function segmentsIntersect(a, b, c, d) {
  const cross = (p, q, r) => (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
  const onSegment = (p, q, r) => Math.abs(cross(p, q, r)) <= 1e-8
    && r[0] >= Math.min(p[0], q[0]) - 1e-8 && r[0] <= Math.max(p[0], q[0]) + 1e-8
    && r[1] >= Math.min(p[1], q[1]) - 1e-8 && r[1] <= Math.max(p[1], q[1]) + 1e-8;
  const abC = cross(a, b, c);
  const abD = cross(a, b, d);
  const cdA = cross(c, d, a);
  const cdB = cross(c, d, b);
  if (((abC > 1e-8 && abD < -1e-8) || (abC < -1e-8 && abD > 1e-8))
    && ((cdA > 1e-8 && cdB < -1e-8) || (cdA < -1e-8 && cdB > 1e-8))) return true;
  return onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b);
}

function pointInPolygon(points, point) {
  let inside = false;
  for (let index = 0, previous = points.length - 1; index < points.length; previous = index, index += 1) {
    const left = points[index];
    const right = points[previous];
    if ((left[1] > point[1]) !== (right[1] > point[1])
      && point[0] < ((right[0] - left[0]) * (point[1] - left[1])) / (right[1] - left[1]) + left[0]) inside = !inside;
  }
  return inside;
}

function approximateWallThickness(outer, holes) {
  let distance = Number.POSITIVE_INFINITY;
  for (const hole of holes) {
    for (const point of hole) {
      for (let index = 0; index < outer.length; index += 1) {
        distance = Math.min(distance, pointSegmentDistance(point, outer[index], outer[(index + 1) % outer.length]));
      }
    }
    for (const point of outer) {
      for (let index = 0; index < hole.length; index += 1) {
        distance = Math.min(distance, pointSegmentDistance(point, hole[index], hole[(index + 1) % hole.length]));
      }
    }
  }
  return Number.isFinite(distance) ? roundCoordinate(distance) : 0;
}

function pointSegmentDistance(point, start, end) {
  const dx = end[0] - start[0];
  const dy = end[1] - start[1];
  const squared = dx * dx + dy * dy;
  if (squared <= 1e-18) return Math.hypot(point[0] - start[0], point[1] - start[1]);
  const ratio = Math.max(0, Math.min(1, ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / squared));
  return Math.hypot(point[0] - start[0] - ratio * dx, point[1] - start[1] - ratio * dy);
}

function createDraft() {
  return { entities: [], selectedId: "", selectedIds: [], selectedPoints: [], history: [], future: [], dirty: false, persisted: false };
}

function normalizeDraftSelection(draft) {
  const available = new Set((draft?.entities ?? []).map((entity) => String(entity.id ?? "")));
  const selected = Array.isArray(draft?.selectedIds)
    ? draft.selectedIds.map(String).filter((id, index, values) => available.has(id) && values.indexOf(id) === index)
    : [];
  const legacy = String(draft?.selectedId ?? "");
  if (!selected.length && available.has(legacy)) selected.push(legacy);
  draft.selectedIds = selected;
  draft.selectedId = selected[0] ?? "";
  const entitiesById = new Map((draft?.entities ?? []).map((entity) => [String(entity.id ?? ""), entity]));
  draft.selectedPoints = (Array.isArray(draft?.selectedPoints) ? draft.selectedPoints : [])
    .map((value) => ({ entityId: String(value?.entityId ?? ""), index: Number(value?.index) }))
    .filter((value, index, values) => {
      const entity = entitiesById.get(value.entityId);
      return entity && Number.isInteger(value.index) && isSelectablePointIndex(entity, value.index)
        && values.findIndex((candidate) => candidate.entityId === value.entityId && Number(candidate.index) === value.index) === index;
    });
  return selected;
}

function draftSelectionIds(draft) {
  return normalizeDraftSelection(draft);
}

function setDraftSelection(draft, ids) {
  draft.selectedIds = Array.isArray(ids) ? ids.map(String) : [];
  draft.selectedPoints = [];
  normalizeDraftSelection(draft);
}

function setDraftSelectionKeepingPoints(draft, ids) {
  draft.selectedIds = Array.isArray(ids) ? ids.map(String) : [];
  normalizeDraftSelection(draft);
}

function draftSelectedPoints(draft) {
  normalizeDraftSelection(draft);
  return draft.selectedPoints;
}

function setDraftPointSelection(draft, values) {
  draft.selectedPoints = Array.isArray(values) ? values : [];
  normalizeDraftSelection(draft);
}

function pointSelectionKey(value) {
  return `${String(value?.entityId ?? "")}:${Number(value?.index)}`;
}

function isSelectablePointIndex(entity, index) {
  if (entity?.kind === "path") return index>=0 && index<(entity.closed?entity.segments.length:entity.segments.length+1);
  if (entity?.kind === "line") return index === 0 || index === 1;
  if (entity?.kind === "rectangle") return index >= 0 && index < 4;
  if (entity?.kind === "circle") return index === 0;
  if (entity?.kind === "ellipse" || entity?.kind === "circleArc" || entity?.kind === "ellipseArc") return index === 0 || index === 1;
  return Array.isArray(entity?.points) && index >= 0 && index < entity.points.length;
}

function createSectionSession() {
  return {
    kind: SECTION_SESSION_CREATE,
    sourceProfileId: "",
    sourceProfileKey: "",
    sourceScope: "",
    sourceRevision: 0,
    sourceName: "",
  };
}

function createSectionViewport() {
  return { centerX: 0, centerY: 0, zoom: 1 };
}

function normalizeSectionViewport(viewport) {
  const source = viewport && typeof viewport === "object" ? viewport : {};
  return {
    centerX: Number.isFinite(Number(source.centerX)) ? Number(source.centerX) : 0,
    centerY: Number.isFinite(Number(source.centerY)) ? Number(source.centerY) : 0,
    zoom: Math.min(SECTION_MAX_ZOOM, Math.max(SECTION_MIN_ZOOM, Number(source.zoom) || 1)),
  };
}

function fitSectionViewport(state, entities) {
  const geometry = (Array.isArray(entities) ? entities : []).filter((entity) => entity?.kind !== "text");
  if (!geometry.length) {
    state.sectionViewport = createSectionViewport();
    return;
  }
  const bounds = geometry.map(entityBounds).reduce((combined, value) => ({
    minX: Math.min(combined.minX, value.minX),
    minY: Math.min(combined.minY, value.minY),
    maxX: Math.max(combined.maxX, value.maxX),
    maxY: Math.max(combined.maxY, value.maxY),
  }), { minX: Number.POSITIVE_INFINITY, minY: Number.POSITIVE_INFINITY, maxX: Number.NEGATIVE_INFINITY, maxY: Number.NEGATIVE_INFINITY });
  const spanX = Math.max(1, bounds.maxX - bounds.minX) * 1.25;
  const spanY = Math.max(1, bounds.maxY - bounds.minY) * 1.25;
  state.sectionViewport = {
    centerX: (bounds.minX + bounds.maxX) / 2,
    centerY: (bounds.minY + bounds.maxY) / 2,
    zoom: Math.min(SECTION_MAX_ZOOM, Math.max(SECTION_MIN_ZOOM, Math.min((SECTION_HALF_WIDTH * 2) / spanX, (SECTION_HALF_WIDTH * 2) * (state.canvasHeight??CANVAS.height)/CANVAS.width / spanY))),
  };
}

function normalizeSectionSession(session) {
  const normalized = { ...createSectionSession(), ...(session && typeof session === "object" ? session : {}) };
  if (![SECTION_SESSION_CREATE, SECTION_SESSION_UPDATE, SECTION_SESSION_COPY].includes(normalized.kind)) {
    normalized.kind = SECTION_SESSION_CREATE;
  }
  normalized.sourceProfileId = String(normalized.sourceProfileId ?? "");
  normalized.sourceProfileKey = String(normalized.sourceProfileKey ?? "");
  normalized.sourceScope = String(normalized.sourceScope ?? "");
  normalized.sourceRevision = Math.max(0, Number(normalized.sourceRevision ?? 0) || 0);
  normalized.sourceName = String(normalized.sourceName ?? "");
  if (normalized.kind === SECTION_SESSION_UPDATE && !normalized.sourceProfileId) {
    normalized.kind = SECTION_SESSION_COPY;
  }
  return normalized;
}

function currentDraft(view, state = ensureSketchState(view)) {
  if (state.mode === SECTION_MODE) return state.section;
  if (state.sideTargetKind === "part") {
    const key = state.targetPartId || "unassigned-part";
    state.sideByPart[key] ??= createDraft();
    return state.sideByPart[key];
  }
  const key = state.targetMemberId || "unassigned";
  state.sideByMember[key] ??= createDraft();
  return state.sideByMember[key];
}

function draftFromStoredSketch(sketch) {
  return {
    entities: Array.isArray(sketch?.entities) ? sketch.entities.map(normalizeEntity).filter(Boolean) : [],
    selectedId: "",
    selectedIds: [],
    selectedPoints: [],
    history: [],
    future: [],
    dirty: false,
    persisted: true,
  };
}

function sketchableMembers(designer) {
  return (Array.isArray(designer?.members) ? designer.members : [])
    .filter((member) => member?.entityId && Number(member?.length ?? 0) > 0)
    .map((member) => ({ ...member, entityId: String(member.entityId) }));
}

function targetMember(designer, memberId) {
  return sketchableMembers(designer).find((member) => member.entityId === String(memberId ?? "")) ?? null;
}

function normalizeSideProductId(value) {
  const text = String(value ?? "").trim();
  return text.startsWith("product:") ? text.slice("product:".length) : text;
}

function enrichSidePart(part, group, nestingSnapshot) {
  const linkedNesting = part?.linkedNesting === true || group?.linkedNesting === true;
  const groupProductId = String(group?.productEntityId ?? "").trim();
  const partProductId = String(part?.productEntityId ?? "").trim();
  const productEntityId = partProductId
    || (linkedNesting ? normalizeSideProductId(groupProductId) : groupProductId);
  const generationRunId = String(part?.generationRunId ?? group?.generationRunId ?? "").trim();
  const independentNesting = !linkedNesting
    && (part?.independentNesting === true || nestingSnapshot);
  return {
    ...part,
    ...(productEntityId ? { productEntityId } : {}),
    ...(generationRunId ? { generationRunId } : {}),
    ...(linkedNesting ? { linkedNesting: true } : {}),
    ...(independentNesting ? { independentNesting: true } : {}),
  };
}

function targetSideEntity(designer, state) {
  if (state?.sideTargetKind !== "part") {
    return targetMember(designer, state?.targetMemberId);
  }
  const partId = String(state?.targetPartId ?? "");
  const sources = [
    { groups: designer?.nestingGroups, nestingSnapshot: true },
    { groups: designer?.manufacturingGroups, nestingSnapshot: false },
  ];
  for (const source of sources) {
    for (const group of Array.isArray(source.groups) ? source.groups : []) {
      const part = (Array.isArray(group?.parts) ? group.parts : [])
        .find((item) => String(item?.entityId ?? "") === partId);
      if (part) return enrichSidePart(part, group, source.nestingSnapshot);
    }
  }
  return String(state?.sideTargetSnapshot?.entityId ?? "") === partId
    ? state.sideTargetSnapshot : null;
}

function memberFaceHeight(member) {
  const profile = member?.profile ?? member?.properties?.["tubeDesigner.profile"] ?? {};
  return Math.max(1, Number(profile.depth ?? profile.height ?? profile.width ?? 50));
}

function sideUnfoldingSurface(reference = null) {
  const unfolding = reference?.unfolding;
  if (unfolding?.available !== true || !Array.isArray(unfolding.surfaces)) return null;
  const surfaces = unfolding.surfaces;
  const hasRectangle = (surface) => surface?.rectangle?.available === true
    || Number(surface?.rectangle?.width) > 0;
  const hasPeriod = (surface) => Number(surface?.uPeriod) > 0;
  return surfaces.find((surface) => surface?.inner !== true && hasRectangle(surface))
    ?? surfaces.find((surface) => surface?.inner !== true && hasPeriod(surface))
    ?? surfaces.find((surface) => hasRectangle(surface))
    ?? surfaces.find((surface) => hasPeriod(surface))
    // Responses written before the rectangle contract may still be opened.
    ?? surfaces.find((surface) => Array.isArray(surface?.panels) && surface.panels.length)
    ?? null;
}

function positiveSideDimension(value) {
  const number = Number(value);
  return Number.isFinite(number) && number > 0 ? number : 0;
}

function sideUnfoldingRectangle(reference = null, member = null) {
  const unfolding = reference?.unfolding;
  if (unfolding?.available !== true) return null;
  const surface = sideUnfoldingSurface(reference);
  const candidates = [surface?.rectangle, unfolding?.lateralRectangle];
  let width = 0;
  let height = 0;
  for (const rectangle of candidates) {
    if (!rectangle || typeof rectangle !== "object") continue;
    width = width || positiveSideDimension(rectangle.width)
      || positiveSideDimension(Number(rectangle.sEnd) - Number(rectangle.sStart));
    height = height || positiveSideDimension(rectangle.height)
      || positiveSideDimension(Number(rectangle.uEnd) - Number(rectangle.uStart));
  }
  width ||= positiveSideDimension(unfolding.length) || positiveSideDimension(member?.length);
  height ||= positiveSideDimension(surface?.uPeriod) || positiveSideDimension(unfolding.perimeter);
  if (!(width > 0 && height > 0)) return null;
  return { width, height, periodic: true };
}

function sideUnfoldingPerimeter(reference = null) {
  return sideUnfoldingRectangle(reference)?.height ?? 0;
}

function sideUnfoldingLength(member, reference = null) {
  const rectangle = sideUnfoldingRectangle(reference, member);
  return rectangle?.width ?? (positiveSideDimension(member?.length) || 1);
}

function canvasMetrics(mode, member, state = null) {
  if (mode === SECTION_MODE) {
    const viewport = normalizeSectionViewport(state?.sectionViewport);
    const halfWidth = SECTION_HALF_WIDTH / viewport.zoom;
    const height=state?.canvasHeight??CANVAS.height;
    const halfHeight = halfWidth * height / CANVAS.width;
    return {
      xMin: viewport.centerX - halfWidth,
      xMax: viewport.centerX + halfWidth,
      yMin: viewport.centerY - halfHeight,
      yMax: viewport.centerY + halfHeight,
      canvasLeft: 0,
      canvasRight: CANVAS.width,
      canvasTop: 0,
      canvasBottom: height,
      sampleStep: Math.max(0.001, 0.8 / viewport.zoom),
      snapTolerance: Math.max(0.02, 2.5 / viewport.zoom),
    };
  }
  const length = Math.max(1, sideUnfoldingLength(member, state?.sideReference));
  const height = Math.max(
    1,
    sideUnfoldingPerimeter(state?.sideReference)
      || Number(state?.sideReference?.sideProjection?.height)
      || memberFaceHeight(member),
  );
  const xPerPixel = length / (CANVAS.right - CANVAS.left);
  const yPerPixel = height / (CANVAS.bottom - CANVAS.top);
  return {
    xMin: 0,
    xMax: length,
    yMin: 0,
    yMax: height,
    canvasLeft: CANVAS.left,
    canvasRight: CANVAS.right,
    canvasTop: CANVAS.top,
    canvasBottom: CANVAS.bottom,
    sampleStep: Math.max(0.5, length / 1000),
    snapTolerance: Math.max(xPerPixel, yPerPixel) * 10,
  };
}

function modelToCanvas(point, metrics) {
  const bounds = canvasDrawingBounds(metrics);
  const x = bounds.left + ((point[0] - metrics.xMin) / (metrics.xMax - metrics.xMin)) * (bounds.right - bounds.left);
  const y = bounds.bottom - ((point[1] - metrics.yMin) / (metrics.yMax - metrics.yMin)) * (bounds.bottom - bounds.top);
  return [x, y];
}

function canvasToModel(point, metrics) {
  const bounds = canvasDrawingBounds(metrics);
  const x = metrics.xMin + ((point[0] - bounds.left) / (bounds.right - bounds.left)) * (metrics.xMax - metrics.xMin);
  const y = metrics.yMin + ((bounds.bottom - point[1]) / (bounds.bottom - bounds.top)) * (metrics.yMax - metrics.yMin);
  return [x, y];
}

function canvasDrawingBounds(metrics) {
  return {
    left: Number(metrics?.canvasLeft ?? CANVAS.left),
    right: Number(metrics?.canvasRight ?? CANVAS.right),
    top: Number(metrics?.canvasTop ?? CANVAS.top),
    bottom: Number(metrics?.canvasBottom ?? CANVAS.bottom),
  };
}

function eventToModelPoint(svg, event, _mode, metrics) {
  const point = svg.createSVGPoint();
  point.x = event.clientX;
  point.y = event.clientY;
  const local = point.matrixTransform(svg.getScreenCTM().inverse());
  if(_mode===SECTION_MODE)return canvasToModel([local.x,local.y],metrics);
  const bounds = canvasDrawingBounds(metrics);
  return canvasToModel([
    Math.min(bounds.right, Math.max(bounds.left, local.x)),
    Math.min(bounds.bottom, Math.max(bounds.top, local.y)),
  ], metrics);
}

function renderSectionCanvasGuide(metrics) {
  const bounds = canvasDrawingBounds(metrics);
  const gridStep = niceGridStep((metrics.xMax - metrics.xMin) / 10);
  const verticals = [];
  const horizontals = [];
  const firstX = Math.ceil(metrics.xMin / gridStep) * gridStep;
  const firstY = Math.ceil(metrics.yMin / gridStep) * gridStep;
  for (let x = firstX, count = 0; x <= metrics.xMax + gridStep * 1e-7 && count < 40; x += gridStep, count += 1) {
    const canvasX = modelToCanvas([x, 0], metrics)[0];
    verticals.push(`<line x1="${canvasX}" y1="${bounds.top}" x2="${canvasX}" y2="${bounds.bottom}"/>`);
  }
  for (let y = firstY, count = 0; y <= metrics.yMax + gridStep * 1e-7 && count < 40; y += gridStep, count += 1) {
    const canvasY = modelToCanvas([0, y], metrics)[1];
    horizontals.push(`<line x1="${bounds.left}" y1="${canvasY}" x2="${bounds.right}" y2="${canvasY}"/>`);
  }
  const origin = modelToCanvas([0, 0], metrics);
  const horizontal = origin[1] >= bounds.top && origin[1] <= bounds.bottom
    ? `<line class="origin" x1="${bounds.left}" y1="${origin[1]}" x2="${bounds.right}" y2="${origin[1]}"/><text x="${bounds.right - 20}" y="${Math.max(bounds.top + 14, origin[1] - 8)}">X</text>` : "";
  const vertical = origin[0] >= bounds.left && origin[0] <= bounds.right
    ? `<line class="origin" x1="${origin[0]}" y1="${bounds.top}" x2="${origin[0]}" y2="${bounds.bottom}"/><text x="${Math.min(bounds.right - 18, origin[0] + 9)}" y="${bounds.top + 18}">Y</text>` : "";
  return `<g class="tube-sketch-model-grid">${verticals.join("")}${horizontals.join("")}</g><g class="tube-sketch-axis">${horizontal}${vertical}</g>`;
}

function niceGridStep(rawStep) {
  const safe = Math.max(1e-9, Number(rawStep) || 1);
  const exponent = 10 ** Math.floor(Math.log10(safe));
  const fraction = safe / exponent;
  const nice = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10;
  return nice * exponent;
}

function unfoldingPoint(value) {
  if (Array.isArray(value) && value.length >= 2) {
    const s = Number(value[0]);
    const u = Number(value[1]);
    return Number.isFinite(s) && Number.isFinite(u) ? [s, u] : null;
  }
  if (value && typeof value === "object") {
    const s = Number(value.s ?? value.S);
    const u = Number(value.u ?? value.U);
    return Number.isFinite(s) && Number.isFinite(u) ? [s, u] : null;
  }
  return null;
}

function unfoldingWirePaths(wire, metrics, period) {
  const source = Array.isArray(wire?.points) ? wire.points.map(unfoldingPoint).filter(Boolean) : [];
  if (source.length < 2) return [];
  const safePeriod = Math.max(1, Number(period) || metrics.yMax || 1);
  const normalizeU = (value) => {
    const epsilon = safePeriod * 1e-7;
    if (Math.abs(value) <= epsilon) return 0;
    if (Math.abs(value - safePeriod) <= epsilon) return safePeriod;
    return ((value % safePeriod) + safePeriod) % safePeriod;
  };
  const chunks = [[]];
  let previous = source[0];
  chunks[0].push(modelToCanvas([previous[0], normalizeU(previous[1])], metrics));
  for (let index = 1; index < source.length; index += 1) {
    const current = source[index];
    const startU = normalizeU(previous[1]);
    const endU = normalizeU(current[1]);
    const difference = endU - startU;
    if (Math.abs(difference) > safePeriod * 0.5
        && Math.abs(difference) < safePeriod * 0.999999) {
      const endThroughSeam = difference > 0 ? endU - safePeriod : endU + safePeriod;
      const seamU = difference > 0 ? 0 : safePeriod;
      const ratio = Math.max(0, Math.min(1,
        (seamU - startU) / (endThroughSeam - startU)));
      const seamS = previous[0] + (current[0] - previous[0]) * ratio;
      chunks.at(-1).push(modelToCanvas([seamS, seamU], metrics));
      chunks.push([modelToCanvas([seamS, seamU === 0 ? safePeriod : 0], metrics)]);
    }
    chunks.at(-1).push(modelToCanvas([current[0], endU], metrics));
    previous = current;
  }
  const usable = chunks.filter((points) => points.length >= 2);
  return usable.map((points) =>
    `M${points.map((point) => `${point[0]} ${point[1]}`).join(" L")}${wire?.closed && usable.length === 1 ? " Z" : ""}`);
}

function renderUnfoldingWire(wire, metrics, period, className, attribute = "") {
  return unfoldingWirePaths(wire, metrics, period).map((path) =>
    `<path class="${className}" ${attribute} data-tube-sketch-reference-unfolded-wire data-tube-sketch-wire-role="${escapeAttr(wire?.role ?? "")}" d="${path}"/>`,
  ).join("");
}

function renderSideCanvasGuide(metrics, member, reference = null) {
  const length = formatNumber(metrics.xMax);
  const height = formatNumber(metrics.yMax);
  const unfolding = sideUnfoldingSurface(reference);
  if (reference?.unfolding?.available === true) {
    const period = sideUnfoldingPerimeter(reference) || metrics.yMax;
    const wires = (Array.isArray(unfolding?.wires) ? unfolding.wires : [])
      .map((wire) => renderUnfoldingWire(
        wire, metrics, period,
        wire?.role === "end-boundary"
          ? "tube-sketch-side-unfolding-end"
          : "tube-sketch-side-unfolding-feature",
        "data-tube-sketch-reference-wire"))
      .join("");
    const featureCount = Array.isArray(unfolding?.wires) ? unfolding.wires.length : 0;
    return `<g class="tube-sketch-side-guide tube-sketch-side-unfolding" data-tube-sketch-coordinate-space="axial-arc-length">
      <rect class="tube-sketch-side-region" data-tube-sketch-reference-region data-tube-sketch-unfolding-rectangle x="${metrics.canvasLeft}" y="${metrics.canvasTop}" width="${metrics.canvasRight - metrics.canvasLeft}" height="${metrics.canvasBottom - metrics.canvasTop}" rx="2"/>
      <g class="tube-sketch-side-unfolding-wires">${wires}</g>
      <line class="tube-sketch-side-unfolding-seam" x1="${metrics.canvasLeft}" y1="${metrics.canvasTop}" x2="${metrics.canvasLeft}" y2="${metrics.canvasBottom}"/>
      <text x="88" y="116">${escapeText(member?.name ?? "管件")} · 整圈侧壁矩形展开（S / U）</text>
      <text x="70" y="535">S 0</text><text x="902" y="535">S ${length} mm</text><text x="905" y="82">U ${height} mm</text>
      <text x="88" y="132">${featureCount ? `已识别 ${featureCount} 条端部/特征边界` : "已生成整圈侧壁矩形编辑域"} · ${escapeText(reference?.unfolding?.method ?? "截面弧长")}</text>
    </g>`;
  }
  const projection = reference?.sideProjection ?? {};
  const region = `<rect class="tube-sketch-side-region" data-tube-sketch-reference-region data-tube-sketch-unfolding-rectangle x="${metrics.canvasLeft}" y="${metrics.canvasTop}" width="${metrics.canvasRight - metrics.canvasLeft}" height="${metrics.canvasBottom - metrics.canvasTop}" rx="2"/>`;
  const segments = (Array.isArray(projection.segments) ? projection.segments : [])
    .map((segment) => {
      if (!Array.isArray(segment?.start) || !Array.isArray(segment?.end)) return "";
      const start = modelToCanvas(segment.start, metrics);
      const end = modelToCanvas(segment.end, metrics);
      return `<line class="tube-sketch-side-edge" data-tube-sketch-reference-segment x1="${start[0]}" y1="${start[1]}" x2="${end[0]}" y2="${end[1]}"/>`;
    }).join("");
  const holes = (Array.isArray(reference?.holes) ? reference.holes : [])
    .map((hole) => renderSideReferenceHole(hole, metrics)).join("");
  return `<g class="tube-sketch-side-guide">
    ${region}
    <g class="tube-sketch-side-reference-edges">${segments}</g>
    <g class="tube-sketch-side-reference-holes">${holes}</g>
    <text x="88" y="116">${escapeText(member?.name ?? "管件侧面")} · 整圈侧壁矩形编辑域</text>
    <text x="70" y="535">0</text><text x="902" y="535">${length} mm</text><text x="905" y="82">${height} mm</text>
  </g>`;
}

function renderSideReferenceHole(hole, metrics) {
  const center = Array.isArray(hole?.sideCenter)
    ? hole.sideCenter
    : [
        Number(hole?.station ?? 0),
        Number(hole?.centerToFaceEdgeNegative ?? metrics.yMax / 2),
      ];
  if (center.length < 2 || !center.every((value) => Number.isFinite(Number(value)))) return "";
  const normalizedCenter = [
    Math.min(metrics.xMax, Math.max(metrics.xMin, Number(center[0]))),
    Math.min(metrics.yMax, Math.max(metrics.yMin, Number(center[1]))),
  ];
  const canvasCenter = modelToCanvas(normalizedCenter, metrics);
  const halfAlong = Math.max(0.1, Number(hole?.spanAlong ?? hole?.diameter ?? 0) / 2);
  const projectedAcross = Math.max(0, Number(hole?.sideSpanAcross ?? hole?.spanAcross ?? hole?.diameter ?? 0));
  const halfAcross = hole?.sideVisible === false
    ? Math.max(metrics.yMax / 220, projectedAcross / 2)
    : Math.max(0.1, projectedAcross / 2);
  const right = modelToCanvas([normalizedCenter[0] + halfAlong, normalizedCenter[1]], metrics);
  const top = modelToCanvas([normalizedCenter[0], normalizedCenter[1] + halfAcross], metrics);
  const radiusX = Math.abs(right[0] - canvasCenter[0]);
  const radiusY = Math.max(1.5, Math.abs(top[1] - canvasCenter[1]));
  const classes = `tube-sketch-side-hole${hole?.sideVisible === false ? " hidden" : ""}`;
  const attributes = `class="${classes}" data-tube-sketch-reference-hole data-tube-sketch-hole-index="${escapeAttr(hole?.index ?? "")}"`;
  if (String(hole?.shape ?? "") === "circle") {
    const displayRadiusY = hole?.sideVisible === false ? radiusY : radiusX;
    return `<ellipse ${attributes} cx="${canvasCenter[0]}" cy="${canvasCenter[1]}" rx="${radiusX}" ry="${displayRadiusY}"/>`;
  }
  return `<rect ${attributes} x="${canvasCenter[0] - radiusX}" y="${canvasCenter[1] - radiusY}" width="${radiusX * 2}" height="${radiusY * 2}" rx="1"/>`;
}

function hasEditableVertices(entity) {
  return entity.kind === "path" || entity.kind === "line" || isArc(entity)
    || Array.isArray(entity.points);
}

function renderUnselectedNodes(draft, state, metrics) {
  if (state.tool !== "select") return "";
  const selected = new Set(draftSelectionIds(draft));
  return `<g class="tube-sketch-inactive-nodes">${draft.entities
    .filter(entity => !selected.has(entity.id) && hasEditableVertices(entity))
    .map(entity => renderHandles(entity.id, selectableEntityPoints(entity).map(({ point, index }) => ({
      point: modelToCanvas(point, metrics), role: "vertex", index,
    })), entity.sampled ? 12 : 160)).join("")}</g>`;
}

function renderSketchEntity(entity, mode, metrics, selected, selectedPointKeys = new Set()) {
  const className = `tube-sketch-entity ${isClosedBoundaryEntity(entity) ? "closed" : "open"} ${selected ? "selected" : ""}`;
  const attributes = `class="${className}" data-tube-sketch-entity-id="${escapeAttr(entity.id)}"`;
  if (entity.kind === "path") {
    const grips=pathNodes(entity).map((p,index)=>({point:modelToCanvas(p,metrics),role:"vertex",index}));
    return `<g><path ${attributes} d="${pathSvg(entity,p=>modelToCanvas(p,metrics))}"/>${selected?renderHandles(entity.id,grips,160,selectedPointKeys):""}</g>`;
  }
  if (entity.kind === "line") {
    const start = modelToCanvas([entity.x1, entity.y1], metrics);
    const end = modelToCanvas([entity.x2, entity.y2], metrics);
    const middle = [(start[0] + end[0]) / 2, (start[1] + end[1]) / 2];
    return `<g>${selectionHalo(`<line ${attributes} x1="${start[0]}" y1="${start[1]}" x2="${end[0]}" y2="${end[1]}"/>`, selected)}${selected ? renderHandles(entity.id, [
      { point: start, role: "vertex", index: 0 },
      { point: middle, role: "move", index: -1, secondary: true },
      { point: end, role: "vertex", index: 1 },
    ], 160, selectedPointKeys) : ""}</g>`;
  }
  if (entity.kind === "ellipse") {
    const points = entityBoundaryPoints(entity).map((point) => modelToCanvas(point, metrics));
    const center = modelToCanvas([entity.cx, entity.cy], metrics);
    const radiusX = modelToCanvas(analyticCurvePoint(entity, 0), metrics);
    const radiusY = modelToCanvas(analyticCurvePoint(entity, Math.PI / 2), metrics);
    return `<g>${selectionHalo(`<path ${attributes} d="${pathSvg(entity,p=>modelToCanvas(p,metrics))}"/>`, selected)}${selected ? renderHandles(entity.id, [
      { point: center, role: "move", index: -1, secondary: true },
      { point: radiusX, role: "ellipse-radius-x", index: 0 },
      { point: radiusY, role: "ellipse-radius-y", index: 1 },
    ], 160, selectedPointKeys) : ""}</g>`;
  }
  if (entity.kind === "circleArc" || entity.kind === "ellipseArc") {
    const points = entityBoundaryPoints(entity).map((point) => modelToCanvas(point, metrics));
    const center = modelToCanvas([entity.cx, entity.cy], metrics);
    return `<g>${selectionHalo(`<path ${attributes} d="${pathSvg(entity,p=>modelToCanvas(p,metrics))}"/>`, selected)}${selected ? renderHandles(entity.id, [
      { point: points[0], role: "vertex", index: 0 },
      { point: center, role: "move", index: -1, secondary: true },
      ...(entity.kind==="circleArc"&&Math.abs(entity.sweep)<FULL_ANGLE-1e-8 ? [{point:modelToCanvas(curvePoint(entity,.5),metrics),role:"arc-middle",index:-2,secondary:true}] : []),
      { point: points.at(-1), role: "vertex", index: 1 },
    ], 160, selectedPointKeys) : ""}</g>`;
  }
  if (entity.kind === "rectangle") {
    const a = modelToCanvas([entity.x, entity.y], metrics);
    const b = modelToCanvas([entity.x + entity.width, entity.y + entity.height], metrics);
    const x = Math.min(a[0], b[0]);
    const y = Math.min(a[1], b[1]);
    const width = Math.abs(b[0] - a[0]);
    const height = Math.abs(b[1] - a[1]);
    const radiusModel = Math.max(0, Math.min(Number(entity.radius ?? 0), Number(entity.width) / 2, Number(entity.height) / 2));
    const radiusPoint = modelToCanvas([Number(entity.x) + radiusModel, Number(entity.y)], metrics);
    const radius = Math.min(width / 2, height / 2, Math.abs(radiusPoint[0] - a[0]));
    return `<g>${selectionHalo(`<rect ${attributes} x="${x}" y="${y}" width="${width}" height="${height}" rx="${radius}"/>`, selected)}${selected ? renderHandles(entity.id, [
      { point: [x, y + height], role: "corner", index: 0 },
      { point: [x + width, y + height], role: "corner", index: 1 },
      { point: [x + width, y], role: "corner", index: 2 },
      { point: [x, y], role: "corner", index: 3 },
      { point: [x + width / 2, y + height / 2], role: "move", index: -1, secondary: true },
    ], 160, selectedPointKeys) : ""}</g>`;
  }
  if (entity.kind === "circle") {
    const center = modelToCanvas([entity.cx, entity.cy], metrics);
    const edge = modelToCanvas([entity.cx + entity.radius, entity.cy], metrics);
    const radius = Math.abs(edge[0] - center[0]);
    return `<g>${selectionHalo(`<circle ${attributes} cx="${center[0]}" cy="${center[1]}" r="${radius}"/>`, selected)}${selected ? renderHandles(entity.id, [
      { point: center, role: "move", index: -1, secondary: true },
      { point: [center[0] + radius, center[1]], role: "radius", index: 0 },
    ], 160, selectedPointKeys) : ""}</g>`;
  }
  if (entity.kind === "text") {
    const point = modelToCanvas([entity.x, entity.y], metrics);
    return `<text ${attributes} x="${point[0]}" y="${point[1]}">${escapeText(entity.value)}</text>`;
  }
  const points = (entity.points ?? []).map((point) => modelToCanvas(point, metrics));
  if (!points.length) return "";
  const d = curvePath(points, entity.kind, Boolean(entity.closed));
  return `<g>${selectionHalo(`<path ${attributes} d="${d}"/>`, selected)}${selected ? renderHandles(entity.id, points.map((point, index) => ({ point, role: "vertex", index })), entity.sampled ? 12 : 160, selectedPointKeys) : ""}</g>`;
}

function selectionHalo(markup, _selected) {
  return markup;
}

function renderHandles(entityId, grips, maximum = 160, selectedPointKeys = new Set()) {
  const visible = grips.length > maximum
    ? grips.filter((grip, index) => index === 0 || index === grips.length - 1
      || selectedPointKeys.has(pointSelectionKey({ entityId, index: grip.index }))
      || index % Math.ceil(grips.length / Math.max(2, maximum - 2)) === 0)
    : grips;
  return `<g class="tube-sketch-handles">${visible.map((grip) => {
    const pointSelected = selectedPointKeys.has(pointSelectionKey({ entityId, index: grip.index }));
    return `<rect class="${[grip.secondary ? "secondary" : "", pointSelected ? "point-selected" : ""].filter(Boolean).join(" ")}" x="${grip.point[0] - 4}" y="${grip.point[1] - 4}" width="8" height="8" data-tube-sketch-grip-entity-id="${escapeAttr(entityId)}" data-tube-sketch-grip-role="${escapeAttr(grip.role)}" data-tube-sketch-grip-index="${grip.index}"><title>${grip.role==="vertex"?"拖动节点；Shift/Ctrl 多选；重合时按 Alt 点选另一端":grip.role==="arc-middle"?"拖动调整圆弧":"拖动调整图形"}</title></rect>`;
  }).join("")}</g>`;
}

function curvePath(points, kind, closed) {
  if (kind === "arc" && points.length >= 3) {
    return `M${points[0][0]} ${points[0][1]} Q${points[1][0]} ${points[1][1]} ${points[2][0]} ${points[2][1]}`;
  }
  if (kind === "spline" && points.length >= 3) {
    let d = `M${points[0][0]} ${points[0][1]}`;
    for (let index = 1; index < points.length - 1; index += 1) {
      const current = points[index];
      const next = points[index + 1];
      d += ` Q${current[0]} ${current[1]} ${(current[0] + next[0]) / 2} ${(current[1] + next[1]) / 2}`;
    }
    const last = points.at(-1);
    d += ` T${last[0]} ${last[1]}`;
    return `${d}${closed ? " Z" : ""}`;
  }
  return `M${points.map((point) => `${point[0]} ${point[1]}`).join(" L")}${closed ? " Z" : ""}`;
}

function renderEntityProperties(entity) {
  const controls = [];
  if (entity.kind === "rectangle") {
    controls.push(field("中心 X", "centerX", entity.x + entity.width / 2));
    controls.push(field("中心 Y", "centerY", entity.y + entity.height / 2));
    controls.push(field("宽度", "width", entity.width, "mm", 0.1));
    controls.push(field("高度", "height", entity.height, "mm", 0.1));
    controls.push(field("圆角半径", "radius", entity.radius ?? 0, "mm", 0.1));
  } else if (entity.kind === "circle") {
    controls.push(field("圆心 X", "centerX", entity.cx));
    controls.push(field("圆心 Y", "centerY", entity.cy));
    controls.push(field("直径", "diameter", entity.radius * 2, "mm", 0.1));
  } else if (entity.kind === "ellipse") {
    controls.push(field("中心 X", "centerX", entity.cx));
    controls.push(field("中心 Y", "centerY", entity.cy));
    controls.push(field("长轴半径", "radiusX", entity.radiusX, "mm", 0.1));
    controls.push(field("短轴半径", "radiusY", entity.radiusY, "mm", 0.1));
  } else if (entity.kind === "circleArc") {
    controls.push(field("圆心 X", "centerX", entity.cx));
    controls.push(field("圆心 Y", "centerY", entity.cy));
    controls.push(field("半径", "radius", entity.radius, "mm", 0.1));
  } else if (entity.kind === "ellipseArc") {
    controls.push(field("中心 X", "centerX", entity.cx));
    controls.push(field("中心 Y", "centerY", entity.cy));
    controls.push(field("长轴半径", "radiusX", entity.radiusX, "mm", 0.1));
    controls.push(field("短轴半径", "radiusY", entity.radiusY, "mm", 0.1));
  } else if (entity.kind === "line") {
    controls.push(field("起点 X", "x1", entity.x1));
    controls.push(field("起点 Y", "y1", entity.y1));
    controls.push(field("终点 X", "x2", entity.x2));
    controls.push(field("终点 Y", "y2", entity.y2));
  } else if (entity.kind === "text") {
    controls.push(`<label class="wide"><span>文字</span><input type="text" value="${escapeAttr(entity.value)}" data-sketch-field="value" data-sketch-entity-id="${escapeAttr(entity.id)}" data-cam-change-action="tube-designer-sketch-property"/></label>`);
    controls.push(field("位置 X", "x", entity.x));
    controls.push(field("位置 Y", "y", entity.y));
  } else if(entity.kind==="path") {
    const bounds=entityBounds(entity);
    controls.push(field("中心 X","centerX",(bounds.minX+bounds.maxX)/2));
    controls.push(field("中心 Y","centerY",(bounds.minY+bounds.maxY)/2));
  } else {
    const bounds = entityBounds(entity);
    controls.push(field("中心 X", "centerX", (bounds.minX + bounds.maxX) / 2));
    controls.push(field("中心 Y", "centerY", (bounds.minY + bounds.maxY) / 2));
    controls.push(field("宽度", "width", bounds.maxX - bounds.minX, "mm", 0.1));
    controls.push(field("高度", "height", bounds.maxY - bounds.minY, "mm", 0.1));
  }
  const isCurve = ["path", "polyline", "spline", "freehand", "arc", "circleArc", "ellipseArc"].includes(entity.kind);
  const analyticArc = entity.kind === "circleArc" || entity.kind === "ellipseArc";
  const length = entityLength(entity);
  return `<section class="tube-sketch-property-section">
      <div class="tube-sketch-property-title"><strong>所选图形</strong><span>${escapeText(entityLabel(entity))}</span></div>
      <dl><div><dt>长度</dt><dd>${formatNumber(length)} mm</dd></div>${isCurve ? `<div><dt>控制点</dt><dd>${analyticArc ? 2 : entity.kind==="path"?pathNodes(entity).length:entity.points?.length ?? 0} 个</dd></div><div><dt>状态</dt><dd>${entity.closed ? "已闭合" : "开放"}</dd></div>` : ""}</dl>
    </section>
    <section class="tube-sketch-property-section">
      <div class="tube-sketch-property-title"><strong>几何参数</strong><span>毫米</span></div>
      <div class="tube-sketch-property-grid">${controls.join("")}</div>
    </section>
    ${isCurve && !analyticArc && entity.kind!=="path" ? `<section class="tube-sketch-property-section"><div class="tube-sketch-property-actions">
      <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-sketch-reverse">反转方向</button>
      <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-sketch-toggle-closed">${entity.closed ? "打开曲线" : "闭合曲线"}</button>
    </div></section>` : ""}`;
}

function field(label, key, value, unit = "mm", step = 0.1) {
  return `<label><span>${label}</span><span class="tube-sketch-property-input"><input type="number" step="${step}" value="${escapeAttr(roundCoordinate(value))}" data-sketch-field="${key}" data-cam-change-action="tube-designer-sketch-property"/><small>${unit}</small></span></label>`;
}

function renderSectionPreview(analysis) {
  if (!analysis.ready) {
    const empty = analysis.kind === "empty";
    return `<div class="tube-sketch-preview-empty"><span class="tube-sketch-preview-empty-icon">◇</span><strong>${empty ? "还没有截面" : "截面还不能生成管型"}</strong><span>${escapeText(empty ? "完成一个有效的闭合截面后，这里将显示三维预览" : analysis.message)}</span></div>`;
  }
  const geometry = previewProfileGeometry(analysis.loops);
  const { path, bounds } = geometry;
  const offsetX = 34;
  const offsetY = -24;
  const corners = [
    [bounds.minX, bounds.minY],
    [bounds.maxX, bounds.minY],
    [bounds.maxX, bounds.maxY],
    [bounds.minX, bounds.maxY],
  ];
  const connectors = corners.map(([x, y]) => `M${x} ${y}L${x + offsetX} ${y + offsetY}`).join(" ");
  return `<svg class="tube-sketch-preview-svg" viewBox="0 0 260 300" role="img" aria-label="由当前截面生成的三维管型预览">
    <g class="tube-sketch-preview-back" transform="translate(${offsetX} ${offsetY})"><path d="${path}" fill-rule="evenodd"/></g>
    <path class="tube-sketch-preview-connector" d="${connectors}"/>
    <g class="tube-sketch-preview-front"><path d="${path}" fill-rule="evenodd"/></g>
  </svg>`;
}

function renderSidePreview(draft, member, reference = null) {
  if (!member) return `<div class="tube-sketch-preview-empty"><span class="tube-sketch-preview-empty-icon">◇</span><strong>还没有管件</strong><span>生成产品后即可绘制侧面切割图</span></div>`;
  const metrics = canvasMetrics(SIDE_MODE, member, { sideReference: reference });
  const referenceHoles = (Array.isArray(reference?.holes) ? reference.holes : [])
    .map((hole) => renderPreviewSideReferenceHole(hole, metrics)).join("");
  const paths = draft.entities.map((entity) => renderPreviewSideEntity(entity, metrics)).join("");
  return `<svg class="tube-sketch-preview-svg tube-sketch-side-preview" viewBox="0 0 300 300" role="img" aria-label="当前管件侧面切割三维预览">
    <polygon class="tube-sketch-metal-top" points="35,88 82,56 273,86 226,118"/>
    <polygon class="tube-sketch-metal-side" points="226,118 273,86 273,205 226,237"/>
    <rect class="tube-sketch-metal-front" x="35" y="88" width="191" height="149"/>
    <g class="tube-sketch-preview-reference-holes">${referenceHoles}</g>
    <g class="tube-sketch-preview-cut">${paths}</g>
  </svg>`;
}

function renderPreviewSideReferenceHole(hole, metrics) {
  const center = Array.isArray(hole?.sideCenter)
    ? hole.sideCenter
    : [Number(hole?.station ?? 0), Number(hole?.centerToFaceEdgeNegative ?? metrics.yMax / 2)];
  if (center.length < 2 || !center.every((value) => Number.isFinite(Number(value)))) return "";
  const x = 45 + ((Number(center[0]) - metrics.xMin) / (metrics.xMax - metrics.xMin)) * 170;
  const y = 224 - ((Number(center[1]) - metrics.yMin) / (metrics.yMax - metrics.yMin)) * 122;
  const radiusX = Math.max(1.5, Number(hole?.spanAlong ?? hole?.diameter ?? 0) / 2
    / (metrics.xMax - metrics.xMin) * 170);
  const radiusY = Math.max(1.5, Number(hole?.sideSpanAcross ?? hole?.spanAcross ?? hole?.diameter ?? 0) / 2
    / (metrics.yMax - metrics.yMin) * 122);
  const className = hole?.sideVisible === false ? "hidden" : "";
  return String(hole?.shape ?? "") === "circle"
    ? `<ellipse class="${className}" cx="${x}" cy="${y}" rx="${radiusX}" ry="${hole?.sideVisible === false ? radiusY : radiusX}"/>`
    : `<rect class="${className}" x="${x - radiusX}" y="${y - radiusY}" width="${radiusX * 2}" height="${radiusY * 2}"/>`;
}

function renderPreviewSideEntity(entity, metrics) {
  const map = (point) => {
    const x = 45 + ((point[0] - metrics.xMin) / (metrics.xMax - metrics.xMin)) * 170;
    const y = 224 - ((point[1] - metrics.yMin) / (metrics.yMax - metrics.yMin)) * 122;
    return [x, y];
  };
  if (entity.kind === "path") return `<path d="${pathSvg(entity,map)}"/>`;
  if (entity.kind === "line") {
    const a = map([entity.x1, entity.y1]);
    const b = map([entity.x2, entity.y2]);
    return `<line x1="${a[0]}" y1="${a[1]}" x2="${b[0]}" y2="${b[1]}"/>`;
  }
  if (entity.kind === "rectangle") {
    const a = map([entity.x, entity.y]);
    const b = map([entity.x + entity.width, entity.y + entity.height]);
    return `<rect x="${Math.min(a[0], b[0])}" y="${Math.min(a[1], b[1])}" width="${Math.abs(b[0] - a[0])}" height="${Math.abs(b[1] - a[1])}"/>`;
  }
  if (entity.kind === "circle") {
    const c = map([entity.cx, entity.cy]);
    const e = map([entity.cx + entity.radius, entity.cy]);
    return `<circle cx="${c[0]}" cy="${c[1]}" r="${Math.abs(e[0] - c[0])}"/>`;
  }
  if (["ellipse", "circleArc", "ellipseArc"].includes(entity.kind)) {
    const points = entityBoundaryPoints(entity).map(map);
    return points.length ? `<path d="${curvePath(points, "polyline", isClosedBoundaryEntity(entity))}"/>` : "";
  }
  if (entity.kind === "text") {
    const p = map([entity.x, entity.y]);
    return `<text x="${p[0]}" y="${p[1]}">${escapeText(entity.value)}</text>`;
  }
  const points = (entity.points ?? []).map(map);
  return points.length ? `<path d="${curvePath(points, entity.kind, Boolean(entity.closed))}"/>` : "";
}

function previewProfileGeometry(polygons) {
  const all = polygons.flat();
  const minX = Math.min(...all.map((point) => point[0]));
  const maxX = Math.max(...all.map((point) => point[0]));
  const minY = Math.min(...all.map((point) => point[1]));
  const maxY = Math.max(...all.map((point) => point[1]));
  const scale = Math.min(150 / Math.max(1, maxX - minX), 114 / Math.max(1, maxY - minY));
  const mappedPolygons = polygons.map((points) => points.map((point) => [
    55 + (point[0] - minX) * scale,
    207 - (point[1] - minY) * scale,
  ]));
  const mapped = mappedPolygons.flat();
  const path = mappedPolygons.map((points) => {
    return `M${points.map((point) => `${point[0]} ${point[1]}`).join(" L")} Z`;
  }).join(" ");
  return {
    path,
    bounds: {
      minX: Math.min(...mapped.map((point) => point[0])),
      maxX: Math.max(...mapped.map((point) => point[0])),
      minY: Math.min(...mapped.map((point) => point[1])),
      maxY: Math.max(...mapped.map((point) => point[1])),
    },
  };
}

function toolLabel(tool) {
  const labels = { select: "选择工具", break: "打断：在曲线上指定开口点", "insert-point": "插入点：在曲线上指定新控制点", line: "直线", polyline: "折线", rectangle: "矩形", circle: "圆", arc: "圆弧", spline: "样条", freehand: "自由曲线", text: "文字" };
  return labels[tool] ?? "选择工具";
}

function entityLabel(entity) {
  if (entity?.kind === "path") return "多段曲线";
  return ({ line: "直线", polyline: "折线", rectangle: "矩形", circle: "圆", ellipse: "椭圆", circleArc: "圆弧", ellipseArc: "椭圆弧", arc: "圆弧", spline: "样条曲线", freehand: "自由曲线", text: "文字" })[entity?.kind] ?? "图形";
}

function entityFromGesture(tool, gesture, end, metrics) {
  const start = gesture.start;
  const id = createEntityId();
  if (["line", "rectangle", "circle", "arc"].includes(tool)
    && Math.hypot(end[0] - start[0], end[1] - start[1]) < metrics.sampleStep) return null;
  if (tool === "line") return { id, kind: "line", x1: start[0], y1: start[1], x2: end[0], y2: end[1] };
  if (tool === "rectangle") return { id, kind: "rectangle", x: Math.min(start[0], end[0]), y: Math.min(start[1], end[1]), width: Math.abs(end[0] - start[0]), height: Math.abs(end[1] - start[1]), radius: 0, closed: true };
  if (tool === "circle") return { id, kind: "circle", cx: start[0], cy: start[1], radius: Math.hypot(end[0] - start[0], end[1] - start[1]), closed: true };
  if (tool === "arc") {
    const control = [(start[0] + end[0]) / 2, (start[1] + end[1]) / 2 + Math.hypot(end[0] - start[0], end[1] - start[1]) * 0.35];
    return { id, kind: "arc", points: [start, control, end], closed: false };
  }
  const sourcePoints = [...gesture.points, end];
  const closed = sourcePoints.length > 3 && pointsNear(sourcePoints[0], sourcePoints.at(-1), metrics.snapTolerance);
  if (closed) sourcePoints[sourcePoints.length - 1] = [...sourcePoints[0]];
  const points = deduplicatePoints(sourcePoints);
  if (points.length < 2) return null;
  if (!closed && entityLength({ kind: tool, points, closed: false }) < metrics.sampleStep) return null;
  return { id, kind: tool, points, closed };
}

function renderGesturePreview(svg, tool, gesture, mode, metrics) {
  const host = svg.querySelector("[data-tube-sketch-draft-preview]");
  if (!host) return;
  const entity = entityFromGesture(tool, gesture, gesture.points.at(-1), metrics);
  host.innerHTML = entity ? renderSketchEntity(entity, mode, metrics, false) : "";
}

function clearGesturePreview(svg) {
  const host = svg.querySelector("[data-tube-sketch-draft-preview]");
  if (host) host.innerHTML = "";
}

function addEntity(draft, entity, autoJoin = false) {
  pushHistory(draft);
  draft.entities.push(normalizeEntity(entity));
  if (autoJoin) joinDrawnEndpoints(draft, entity.id);
  setDraftSelection(draft, [entity.id]);
  draft.future = [];
  draft.dirty = true;
}

function joinDrawnEndpoints(draft, entityId) {
  const drawn = draft.entities.find(entity => entity.id === entityId);
  if (!drawn || isClosedBoundaryEntity(drawn)) return;
  let path = editablePath(drawn);
  if (!path.segments.length) return;
  const tolerance = 1e-7; // Only coincident/snap-resolved nodes, never a zoom-dependent gap.
  const endpoints = [curvePoint(path.segments[0], 0), curvePoint(path.segments.at(-1), 1)];
  const consumed = new Set([entityId]);
  let changed = false;
  for (const point of endpoints) {
    const atStart = pointDistance2d(curvePoint(path.segments[0], 0), point) <= tolerance;
    const atEnd = pointDistance2d(curvePoint(path.segments.at(-1), 1), point) <= tolerance;
    if (!atStart && !atEnd) continue;
    const matches = [];
    for (const entity of draft.entities) {
      if (consumed.has(entity.id) || entity.kind === "text" || isClosedBoundaryEntity(entity)) continue;
      const candidate = editablePath(entity);
      if (!candidate.segments.length) continue;
      if (pointDistance2d(curvePoint(candidate.segments[0], 0), point) <= tolerance) matches.push({entity, atStart:true});
      if (pointDistance2d(curvePoint(candidate.segments.at(-1), 1), point) <= tolerance) matches.push({entity, atStart:false});
    }
    // A branch or a coincident broken pair needs the user's explicit choice.
    if (matches.length > 1) continue;
    if (!matches.length) {
      if (changed && atStart && atEnd) {
        const closed = closeEditablePath(path, tolerance);
        if (closed) path = closed;
      }
      continue;
    }
    const match = matches[0];
    const joined = atStart
      ? joinPathsAtEndpoints(editablePath(match.entity, match.atStart), path, tolerance)
      : joinPathsAtEndpoints(path, editablePath(match.entity, !match.atStart), tolerance);
    if (joined) {
      path = joined;
      consumed.add(match.entity.id);
      changed = true;
    }
  }
  if (!changed) return;
  const index = Math.min(...draft.entities.map((entity, index) => consumed.has(entity.id) ? index : Infinity));
  draft.entities = draft.entities.filter(entity => !consumed.has(entity.id));
  draft.entities.splice(index, 0, compactEditablePath({...path, id:entityId}, false));
}

function deleteSelected(draft) {
  const selected = new Set(draftSelectionIds(draft));
  if (!selected.size) return;
  const next = draft.entities.filter((entity) => !selected.has(entity.id));
  if (next.length === draft.entities.length) return;
  pushHistory(draft);
  draft.entities = next;
  setDraftSelection(draft, []);
  draft.future = [];
  draft.dirty = true;
}

export function breakEntityAtPoint(draft, entityId, point) {
  const index = draft.entities.findIndex((e) => e.id === entityId);
  if (index < 0) return { changed: false, message: "请在要打断的曲线上单击。" };
  const original = draft.entities[index];
  let parts;
  if (original.kind === "circle" || original.kind === "ellipse") {
    const arc = editableSegments(original)[0];
    const hit = nearestSegment(arc, point);
    parts = [{ ...arc, startAngle: hit.t * FULL_ANGLE, closed: false, brokenStart: true, brokenEnd: true }];
  } else {
    const edited = editPathAtPoint(original, point, true);
    if (!edited) return { changed: false, message: "请选择曲线内部或闭合轮廓的顶点；已有开放端点不需要再次打断。" };
    parts = edited.parts.map(part=>compactEditablePath(part));
  }
  parts.forEach((e) => { e.id = createEntityId(); });
  const selectedPoints = parts.length === 1
    ? [{ entityId: parts[0].id, index: 0 }, { entityId: parts[0].id, index: endpointIndex(parts[0]) }]
    : [{ entityId: parts[0].id, index: endpointIndex(parts[0]) }, { entityId: parts[1].id, index: 0 }];
  replaceEntityWithBreak(draft, index, parts, selectedPoints);
  return { changed: true, entities: parts, message: "已在此处打断，产生两个独立端点。" };
}

export function insertPointOnEntity(draft, entityId, point) {
  const index = draft.entities.findIndex((e) => e.id === entityId);
  if (index < 0) return { changed: false, message: "请在要插入点的曲线上单击。" };
  const edited = editPathAtPoint(draft.entities[index], point, false);
  if (!edited) return { changed: false, message: "此处已有顶点，或所选图形不支持插入点。" };
  const replacement = compactEditablePath({ ...edited.parts[0], id: entityId }, false);
  pushHistory(draft);
  draft.entities[index] = replacement;
  setDraftSelection(draft, [entityId]);
  setDraftPointSelection(draft, [{ entityId, index: edited.node }]);
  draft.future = [];
  draft.dirty = true;
  return { changed: true, entity: replacement, point: edited.point, index: edited.node, message: "已插入一个连接点，曲线保持连通。" };
}

function compactEditablePath(path, simplify = true) {
  const { id, closed = false, brokenStart, brokenEnd } = path;
  let segments = editableSegments(path).map((s) => {
    const clean = { ...s };
    delete clean.id; delete clean.closed; delete clean.brokenStart; delete clean.brokenEnd;
    return clean;
  });
  if (simplify) {
    const joined = [];
    for (const s of segments) {
      const previous = joined.at(-1);
      if (previous && compatibleArcSegments(previous, s)) previous.sweep += s.sweep;
      else joined.push({ ...s });
    }
    segments = joined;
  }
  const metadata = { id, closed, brokenStart: !closed && brokenStart === true, brokenEnd: !closed && brokenEnd === true };
  if (simplify && segments.length === 1 && isArc(segments[0])) {
    const s = segments[0];
    if (closed && Math.abs(Math.abs(s.sweep) - FULL_ANGLE) < 1e-7) {
      return s.kind === "circleArc"
        ? { id, kind: "circle", cx:s.cx,cy:s.cy,radius:s.radius,closed:true }
        : { id, kind: "ellipse",cx:s.cx,cy:s.cy,radiusX:s.radiusX,radiusY:s.radiusY,rotation:s.rotation??0,closed:true };
    }
    if (!closed) return { ...s, ...metadata };
  }
  if (segments.every((s) => s.kind === "line")) {
    const points = [curvePoint(segments[0],0),...segments.map((s)=>curvePoint(s,1))];
    if (simplify && !closed && pointsAreCollinear(points, 1e-8))
      return { kind:"line",x1:points[0][0],y1:points[0][1],x2:points.at(-1)[0],y2:points.at(-1)[1],...metadata };
    return { kind:"polyline",points:closed?points.slice(0,-1):points,...metadata };
  }
  return { kind:"path",segments,...metadata };
}

function compatibleArcSegments(a,b) {
  if (!isArc(a) || a.kind !== b.kind || Math.sign(a.sweep)!==Math.sign(b.sweep)) return false;
  if (Math.abs(a.sweep+b.sweep)>FULL_ANGLE+1e-8) return false;
  const close=(a,b)=>Math.abs(Number(a)-Number(b))<1e-8;
  return close(a.cx,b.cx)&&close(a.cy,b.cy)
    && (a.kind==="circleArc" ? close(a.radius,b.radius) : close(a.radiusX,b.radiusX)&&close(a.radiusY,b.radiusY)&&close(a.rotation??0,b.rotation??0))
    && pointDistance2d(curvePoint(a,1),curvePoint(b,0))<1e-7;
}

function endpointIndex(entity) {
  if (entity.kind === "line" || isArc(entity)) return 1;
  if (entity.kind === "path") return entity.segments.length;
  return entity.points?.length - 1;
}
function isOpenEndpointSelection(entity,index) {
  return !isClosedBoundaryEntity(entity) && (index===0 || index===endpointIndex(entity));
}
function editablePath(entity,reverse=false) {
  const segments=editableSegments(entity);
  return {kind:"path",segments:reverse?segments.reverse().map(reverseSegment):segments,closed:false,
    brokenStart:reverse?entity.brokenEnd:entity.brokenStart,
    brokenEnd:reverse?entity.brokenStart:entity.brokenEnd};
}
function joinPathsAtEndpoints(a,b,tolerance) {
  const left=structuredClone(a),right=structuredClone(b);
  const p=curvePoint(left.segments.at(-1),1),q=curvePoint(right.segments[0],0);
  if (pointDistance2d(p,q)>tolerance) {
    // Point merge explicitly moves both selected endpoints; object merge passes a strict tolerance.
    return null;
  }
  const joint=midpoint2d(p,q);
  if (pointDistance2d(p,q)>1e-8) {
    left.segments[left.segments.length-1]=moveSegmentEnd(left.segments.at(-1),true,joint);
    right.segments[0]=moveSegmentEnd(right.segments[0],false,joint);
    if(pointDistance2d(curvePoint(left.segments.at(-1),1),curvePoint(right.segments[0],0))>1e-7)return null;
  }
  return {kind:"path",segments:[...left.segments,...right.segments],closed:false,
    brokenStart:left.brokenStart,brokenEnd:right.brokenEnd};
}
function closeEditablePath(path,tolerance) {
  let result=structuredClone(path);
  const p=curvePoint(result.segments[0],0),q=curvePoint(result.segments.at(-1),1);
  if (pointDistance2d(p,q)>tolerance) return null;
  if (pointDistance2d(p,q)>1e-8) {
    const joint=midpoint2d(p,q);
    result=movePathNode(result,0,joint);
    result=movePathNode(result,result.segments.length,joint);
    if(pointDistance2d(curvePoint(result.segments[0],0),curvePoint(result.segments.at(-1),1))>1e-7)return null;
  }
  result.closed=true; result.brokenStart=false; result.brokenEnd=false;
  return result;
}
function commitMergedPath(draft,entities,path) {
  const ids=new Set(entities.map(e=>e.id));
  const insertion=Math.min(...entities.map(e=>draft.entities.indexOf(e)));
  const merged=compactEditablePath({...path,id:createEntityId()});
  pushHistory(draft);
  draft.entities=draft.entities.filter(e=>!ids.has(e.id));
  draft.entities.splice(insertion,0,merged);
  setDraftSelection(draft,[merged.id]);
  draft.future=[];draft.dirty=true;
  return {changed:true,entity:merged,message:merged.closed?"端点已合并，轮廓闭合。":"端点已合并，其他断口保持开放。"};
}
export function mergeSelectedEntities(draft,tolerance=SECTION_JOIN_TOLERANCE) {
  const points=draftSelectedPoints(draft);
  if(points.length) {
    if(points.length!==2)return {changed:false,message:"请只选择两个开放端点进行合并。"};
    const refs=points.map(p=>({entity:draft.entities.find(e=>e.id===p.entityId),index:p.index}));
    if(refs.some(r=>!r.entity||!isOpenEndpointSelection(r.entity,r.index)))
      return {changed:false,message:"合并需要两个开放端点；连接点不能作为断口合并。"};
    const [a,b]=refs;
    if(a.entity.id===b.entity.id) {
      if(a.entity.kind==="line")return {changed:false,message:"直线的两端不能合并成闭合轮廓。"};
      const path=closeEditablePath(editablePath(a.entity),Infinity);
      if(!path)return {changed:false,message:"端点无法在保持曲线类型的条件下合并，请先将两端移到同一位置。"};
      return commitMergedPath(draft,[a.entity],path);
    }
    const path=joinPathsAtEndpoints(editablePath(a.entity,a.index===0),editablePath(b.entity,b.index!==0),Infinity);
    if(!path)return {changed:false,message:"端点无法在保持曲线类型的条件下合并，请先将两端移到同一位置。"};
    // Joining one selected pair must not heal another pair just because their coordinates coincide.
    return commitMergedPath(draft,[a.entity,b.entity],path);
  }
  const selected=draftSelectionIds(draft).map(id=>draft.entities.find(e=>e.id===id)).filter(Boolean);
  if(!selected.length||selected.some(e=>e.kind==="text"||isClosedBoundaryEntity(e)))
    return {changed:false,message:"请选择首尾相接的开放曲线，或框选需要合并的两个端点。"};
  let path=editablePath(selected[0]);
  const remaining=selected.slice(1);
  while(remaining.length) {
    let matched=false;
    for(let i=0;i<remaining.length&&!matched;i++) {
      for(const reverse of [false,true]) {
        const candidate=editablePath(remaining[i],reverse);
        const joined=joinPathsAtEndpoints(path,candidate,tolerance)??joinPathsAtEndpoints(candidate,path,tolerance);
        if(joined){path=joined;remaining.splice(i,1);matched=true;break;}
      }
    }
    if(!matched)return {changed:false,message:"所选曲线没有全部首尾相接。"};
  }
  const closed=closeEditablePath(path,tolerance);
  if(selected.length===1&&!closed)return {changed:false,message:"两个端点没有相接，请选择端点后合并。"};
  return commitMergedPath(draft,selected,closed??path);
}

function midpoint2d(left, right) {
  return [
    (Number(left[0]) + Number(right[0])) / 2,
    (Number(left[1]) + Number(right[1])) / 2,
  ];
}

function pointsAreCollinear(points, tolerance) {
  const start = points[0];
  const end = points.at(-1);
  if (pointDistance2d(start, end) <= 1e-9) return false;
  return points.slice(1, -1).every((point) => pointSegmentDistance(point, start, end) <= tolerance);
}

function replaceEntityWithBreak(draft, index, replacements, selectedPoints = []) {
  pushHistory(draft);
  draft.entities.splice(index, 1, ...replacements);
  setDraftSelection(draft, replacements.map((entity) => entity.id));
  setDraftPointSelection(draft, selectedPoints);
  draft.future = [];
  draft.dirty = true;
}

function nearestPolylineSegment(points, point, closed) {
  if (!Array.isArray(points) || points.length < 2) return null;
  const segmentCount = closed ? points.length : points.length - 1;
  let nearest = null;
  for (let index = 0; index < segmentCount; index += 1) {
    const projected = projectPointToSegment(point, points[index], points[(index + 1) % points.length]);
    if (!nearest || projected.distance < nearest.distance) nearest = { ...projected, index };
  }
  return nearest;
}

function projectPointToSegment(point, start, end) {
  const dx = Number(end[0]) - Number(start[0]);
  const dy = Number(end[1]) - Number(start[1]);
  const squared = dx * dx + dy * dy;
  const ratio = squared <= 1e-18 ? 0 : Math.max(0, Math.min(1,
    ((Number(point[0]) - Number(start[0])) * dx + (Number(point[1]) - Number(start[1])) * dy) / squared,
  ));
  const projected = [roundCoordinate(Number(start[0]) + dx * ratio), roundCoordinate(Number(start[1]) + dy * ratio)];
  return { point: projected, ratio, distance: Math.hypot(Number(point[0]) - projected[0], Number(point[1]) - projected[1]) };
}

function deduplicateOpenPoints(points) {
  const result = [];
  for (const value of points) {
    const point = profilePoint(value);
    if (!point) continue;
    if (!result.length || !pointsNear(result.at(-1), point, 1e-7)) result.push(point);
  }
  return result;
}

function reverseSelected(draft) {
  const entity = draft.entities.find((item) => item.id === draft.selectedId);
  if (!entity) return;
  pushHistory(draft);
  if (entity.kind === "line") {
    [entity.x1, entity.x2] = [entity.x2, entity.x1];
    [entity.y1, entity.y2] = [entity.y2, entity.y1];
  } else if (isArc(entity)) {
    Object.assign(entity, reverseSegment(entity));
    [entity.brokenStart,entity.brokenEnd]=[entity.brokenEnd,entity.brokenStart];
  } else if(entity.kind==="path") {
    entity.segments=entity.segments.reverse().map(reverseSegment);
    [entity.brokenStart,entity.brokenEnd]=[entity.brokenEnd,entity.brokenStart];
  } else if (Array.isArray(entity.points)) {
    entity.points.reverse();
    [entity.brokenStart, entity.brokenEnd] = [entity.brokenEnd === true, entity.brokenStart === true];
  }
  draft.future = [];
  draft.dirty = true;
}

function toggleSelectedClosed(draft) {
  const entity = draft.entities.find((item) => item.id === draft.selectedId);
  if (!entity || !Array.isArray(entity.points)) return;
  pushHistory(draft);
  entity.closed = !entity.closed;
  if (entity.closed) {
    delete entity.brokenStart;
    delete entity.brokenEnd;
  }
  draft.future = [];
  draft.dirty = true;
}

function updateSelectedProperty(draft, target) {
  const entity = draft.entities.find((item) => item.id === draft.selectedId);
  const key = String(target?.dataset?.sketchField ?? "");
  if (!entity || !key) return;
  if (key === "value") {
    pushHistory(draft);
    entity.value = String(target.value ?? "");
  } else {
    const value = Number(target.value);
    if (!Number.isFinite(value)) return;
    pushHistory(draft);
    updateEntityNumber(entity, key, value);
  }
  draft.future = [];
  draft.dirty = true;
}

function updateEntityNumber(entity, key, value) {
  if(entity.kind==="path") {
    const bounds=entityBounds(entity);
    if(key==="centerX")translateEntity(entity,value-(bounds.minX+bounds.maxX)/2,0);
    if(key==="centerY")translateEntity(entity,0,value-(bounds.minY+bounds.maxY)/2);
    return;
  }
  if (entity.kind === "rectangle") {
    if (key === "centerX") entity.x = value - entity.width / 2;
    else if (key === "centerY") entity.y = value - entity.height / 2;
    else if (key === "width" || key === "height") {
      entity[key] = Math.max(0.01, value);
      entity.radius = Math.min(Number(entity.radius ?? 0), entity.width / 2, entity.height / 2);
    } else if (key === "radius") entity.radius = Math.max(0, Math.min(value, entity.width / 2, entity.height / 2));
    return;
  }
  if (entity.kind === "ellipse") {
    if (key === "centerX") entity.cx = value;
    else if (key === "centerY") entity.cy = value;
    else if (key === "radiusX" || key === "radiusY") entity[key] = Math.max(0.005, value);
    return;
  }
  if (entity.kind === "circleArc" || entity.kind === "ellipseArc") {
    if (key === "centerX") entity.cx = value;
    else if (key === "centerY") entity.cy = value;
    else if (key === "radius" && entity.kind === "circleArc") entity.radius = Math.max(0.005, value);
    else if ((key === "radiusX" || key === "radiusY") && entity.kind === "ellipseArc") entity[key] = Math.max(0.005, value);
    return;
  }
  if (entity.kind === "circle") {
    if (key === "centerX") entity.cx = value;
    else if (key === "centerY") entity.cy = value;
    else if (key === "diameter") entity.radius = Math.max(0.005, value / 2);
    return;
  }
  if (entity.kind === "line" || entity.kind === "text") {
    entity[key] = value;
    return;
  }
  if (!Array.isArray(entity.points)) return;
  const bounds = entityBounds(entity);
  const centerX = (bounds.minX + bounds.maxX) / 2;
  const centerY = (bounds.minY + bounds.maxY) / 2;
  if (key === "centerX" || key === "centerY") {
    const dx = key === "centerX" ? value - centerX : 0;
    const dy = key === "centerY" ? value - centerY : 0;
    entity.points = entity.points.map((point) => [point[0] + dx, point[1] + dy]);
  } else if (key === "width" || key === "height") {
    const oldSize = key === "width" ? bounds.maxX - bounds.minX : bounds.maxY - bounds.minY;
    if (oldSize <= 1e-9) return;
    const scale = Math.max(0.001, value) / oldSize;
    entity.points = entity.points.map((point) => key === "width"
      ? [centerX + (point[0] - centerX) * scale, point[1]]
      : [point[0], centerY + (point[1] - centerY) * scale]);
  }
}

function pushHistory(draft) {
  draft.history ??= [];
  draft.history.push(JSON.stringify({ entities: draft.entities, selectedId: draft.selectedId, selectedIds: draftSelectionIds(draft), selectedPoints: draftSelectedPoints(draft) }));
  if (draft.history.length > 50) draft.history.shift();
}

function undoDraft(draft) {
  const snapshot = draft.history?.pop();
  if (!snapshot) return;
  draft.future ??= [];
  draft.future.push(JSON.stringify({ entities: draft.entities, selectedId: draft.selectedId, selectedIds: draftSelectionIds(draft), selectedPoints: draftSelectedPoints(draft) }));
  Object.assign(draft, JSON.parse(snapshot), { dirty: true });
  normalizeDraftSelection(draft);
}

function redoDraft(draft) {
  const snapshot = draft.future?.pop();
  if (!snapshot) return;
  draft.history ??= [];
  draft.history.push(JSON.stringify({ entities: draft.entities, selectedId: draft.selectedId, selectedIds: draftSelectionIds(draft), selectedPoints: draftSelectedPoints(draft) }));
  Object.assign(draft, JSON.parse(snapshot), { dirty: true });
  normalizeDraftSelection(draft);
}

async function importSketchDxf(context, view, ops) {
  const state = ensureSketchState(view);
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
  if (typeof bridge?.openFileDialog !== "function") throw new Error("当前宿主没有提供文件选择能力。");
  const sourcePath = String(await bridge.openFileDialog({ title: "选择草图 DXF", filters: [{ name: "DXF 二维图形", extensions: ["dxf"] }] }) ?? "").trim();
  if (!sourcePath) return null;
  view.pending = true;
  view.progress = { title: "正在导入草图", detail: "正在识别二维曲线", stage: "DXF 解析", mode: "Sketch" };
  ops.renderProject(context, view);
  try {
    const response = await context.productProxy.invoke("TubeDesigner.ImportProfileDxf", { sourcePath }, { timeoutMs: 120000 });
    const entities = entitiesFromProfile(response?.profile);
    const draft = currentDraft(view, state);
    pushHistory(draft);
    draft.entities = entities;
    draft.selectedId = entities[0]?.id ?? "";
    draft.selectedIds = entities[0]?.id ? [entities[0].id] : [];
    draft.future = [];
    draft.dirty = true;
    view.error = "";
    return response?.profile ?? null;
  } finally {
    view.pending = false;
    view.progress = null;
    ops.renderProject(context, view);
  }
}

async function saveSectionProfile(context, view, ops, saveMode = "") {
  const state = ensureSketchState(view);
  const proposed = String(state.sectionName ?? "").trim();
  if (!proposed) {
    view.error = "请输入管型名称。";
    ops.renderProject(context, view);
    return null;
  }
  state.sectionName = proposed;
  const profile = buildProfileFromSectionDraft(state.section, proposed);
  const componentReturn = view.tubeDesignerComponentCSGProfileReturn;
  if (componentReturn?.featureId) {
    const csgDraft = view.tubeDesignerComponentLibrary?.csgDraft;
    const feature = csgDraft?.features?.find((item) => item.id === componentReturn.featureId);
    if (!feature || feature.primitive !== "extrusion") {
      view.tubeDesignerComponentCSGProfileReturn = null;
      throw new Error("原拉伸体已经不存在，请返回 CSG 树重新选择。");
    }
    feature.profile = {
      sourceType: "fixed", key: `embedded:sketch:${feature.id}`,
      name: proposed, parameters: {}, parameterDefinitions: [], snapshot: profile,
    };
    csgDraft.selectedId = feature.id;
    csgDraft.selectedNodeKind = "primitive";
    csgDraft.dirty = true;
    csgDraft.previewStatus = "loading";
    csgDraft.previewError = "";
    state.section.dirty = false;
    state.saveChoiceDialogOpen = false;
    view.tubeDesignerSketchDialogOpen = false;
    view.tubeDesignerComponentCSGProfileReturn = null;
    view.error = "";
    await selectSketchArea(context, view, "components");
    ops.showNotice?.(context, view, `截面“${proposed}”已放入当前拉伸体；完成零件时统一保存。`);
    return profile;
  }
  const session = normalizeSectionSession(state.sectionSession);
  const editableDxf = session.kind === SECTION_SESSION_UPDATE && Boolean(session.sourceProfileId);
  if (editableDxf && !["overwrite", "copy"].includes(saveMode)) {
    state.saveChoiceDialogOpen = true;
    view.error = "";
    ops.renderProject(context, view);
    return null;
  }
  const updating = editableDxf && saveMode === "overwrite";
  state.saveChoiceDialogOpen = false;
  view.pending = true;
  view.progress = {
    title: updating ? "正在更新管型" : "正在保存管型",
    detail: updating ? "正在更新我的 定式管型" : "正在校验截面并生成新的我的管型",
    stage: "截面校验",
    mode: "Sketch",
  };
  ops.renderProject(context, view);
  let saved = null;
  try {
    const response = await context.productProxy.invoke("TubeDesigner.SaveImportedProfile", {
      ...(updating ? {
        id: session.sourceProfileId,
        revision: session.sourceRevision,
      } : {}),
      name: proposed,
      profile,
    }, { timeoutMs: 30000 });
    saved = response?.profile;
    if (!saved?.id) throw new Error("保存草图管型后没有返回记录标识。");
    view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], profileId: "" };
    const profiles = view.tubeDesignerUserData.profiles ??= [];
    const index = profiles.findIndex((item) => String(item?.id ?? "") === String(saved.id));
    if (index >= 0) profiles[index] = saved;
    else profiles.push(saved);
    state.section.dirty = false;
    state.sectionSession = normalizeSectionSession({
      kind: SECTION_SESSION_UPDATE,
      sourceProfileId: saved.id,
      sourceProfileKey: `user:${saved.id}`,
      sourceScope: "user",
      sourceRevision: saved.revision,
      sourceName: saved.name ?? proposed,
    });
    selectSavedProfileInLibrary(view, saved.id);
    view.error = "";
    return saved;
  } finally {
    view.pending = false;
    view.progress = null;
    if (saved) {
      view.tubeDesignerSketchDialogOpen = false;
      await selectSketchArea(context, view, "profiles");
      ops.showNotice(
        context,
        view,
        updating ? `管型“${proposed}”已更新。` : `新管型“${proposed}”已保存到“我的管型”。`,
      );
    } else ops.renderProject(context, view);
  }
}

async function cancelSketch(context, view, ops) {
  const state = ensureSketchState(view);
  const draft = currentDraft(view, state);
  if (draft.dirty && typeof globalThis.confirm === "function"
      && !globalThis.confirm("当前草图还有未保存的修改，确定取消吗？")) return false;
  state.command = null;
  state.saveChoiceDialogOpen = false;
  view.tubeDesignerSketchDialogOpen = false;
  const componentProfile = Boolean(view.tubeDesignerComponentCSGProfileReturn);
  view.tubeDesignerComponentCSGProfileReturn = null;
  await selectSketchArea(context, view, componentProfile ? "components" : (state.mode === SECTION_MODE
    ? "profiles" : state.sideReturnAreaId || "view"));
  ops.renderProject(context, view);
  return true;
}

async function selectSketchArea(context, view, areaId) {
  await context.actions?.selectRibbonTab?.(areaId);
  context.activeRibbonTabId = areaId;
  view.activeAreaId = areaId;
}

function selectSavedProfileInLibrary(view, profileId) {
  const key = `user:${String(profileId ?? "")}`;
  const library = view.tubeDesignerProfileLibrary ??= { scope: "user", search: "", selectedByScope: {} };
  library.scope = "user";
  library.search = "";
  library.selectedByScope ??= {};
  library.selectedByScope.user = key;
  view.tubeDesignerSelectedProfileId = key;
  view.tubeDesignerProfilePreview = null;
  view.tubeDesignerProfilePreviewRequest = null;
}

async function saveSideSketch(context, view, ops) {
  const state = ensureSketchState(view);
  const designer = view.scene?.tubeDesigner ?? {};
  const partTarget = state.sideTargetKind === "part";
  const member = targetSideEntity(designer, state);
  const productId = normalizeSideProductId(partTarget
    ? member?.productEntityId ?? state.sideTargetSnapshot?.productEntityId ?? ""
    : designer.product?.entityId ?? "");
  const draft = currentDraft(view, state);
  if (!member) throw new Error(partTarget ? "当前下料零件已不存在，请重新选择。" : "请先在产品页生成产品并选择管件。");
  if (!partTarget && !productId) throw new Error("请先在产品页生成产品并选择管件。");
  const removing = !draft.entities.length;
  if (removing && !(draft.persisted && draft.dirty)) throw new Error("请先绘制侧面切割图。");
  const validation = validateSideSketchDraft(draft, member, state.sideReference);
  if (!removing && !validation.ready) throw new Error(validation.message);
  const unfoldingPerimeter = sideUnfoldingPerimeter(state.sideReference);
  const sketch = {
    schema: SKETCH_SCHEMA,
    schemaVersion: 1,
    kind: SIDE_MODE,
    ...(partTarget ? {
      targetPartId: member.entityId,
    } : {
      targetMemberId: member.entityId,
      targetMemberKey: String(member.stableKey ?? ""),
    }),
    unit: "mm",
    faceHeight: Math.max(
      1,
      unfoldingPerimeter
        || Number(state.sideReference?.sideProjection?.height)
        || memberFaceHeight(member),
    ),
    length: sideUnfoldingLength(member, state.sideReference),
    ...(unfoldingPerimeter ? {
      coordinateSpace: "axial-arc-length",
      perimeter: unfoldingPerimeter,
      unfoldingMethod: String(state.sideReference?.unfolding?.method ?? "section-arc-length"),
    } : {}),
    entities: draft.entities.map((entity) => ({ ...entity })),
    updatedAt: new Date().toISOString(),
  };
  const wasDialogOpen = Boolean(view.tubeDesignerSketchDialogOpen);
  view.pending = true;
  view.progress = { title: "正在应用侧面草图", detail: `正在保存到${member.name || "当前管件"}`, stage: "保存二维图形", mode: "Sketch" };
  ops.renderProject(context, view);
  let saved = false;
  try {
    let method;
    let payload;
    const independentPart = partTarget
      && (member.independentNesting === true
        || state.sideTargetSnapshot?.independentNesting === true);
    if (independentPart) {
      method = "TubeDesigner.SavePartSketch";
      payload = {
        partEntityId: member.entityId,
        resourceVersion: Number(member.manufacturingGeometryResourceVersion ?? 0),
        sketch,
      };
    } else {
      const targetMemberId = String(member.sourceMemberId ?? member.entityId ?? "");
      if (!productId || !targetMemberId) throw new Error("当前下料零件没有可保存草图的产品来源。");
      method = "TubeDesigner.SaveSketch";
      payload = { productEntityId: productId, targetMemberId, sketch: {
        ...sketch,
        targetMemberId,
        targetMemberKey: String(member.stableKey ?? ""),
      } };
    }
    const response = await context.sceneProxy.invoke(method, payload, { timeoutMs: 30000 });
    if (response?.tubeDesigner) view.scene.tubeDesigner = response.tubeDesigner;
    draft.dirty = false;
    draft.persisted = !removing;
    view.error = "";
    saved = true;
    return sketch;
  } finally {
    view.pending = false;
    view.progress = null;
    if (saved) {
      if (wasDialogOpen) {
        view.tubeDesignerSketchDialogOpen = false;
        await selectSketchArea(context, view, state.sideReturnAreaId || "nesting");
      }
      ops.showNotice(context, view, removing
        ? `已从“${member.name || "当前管件"}”移除侧面草图。`
        : `侧面草图已应用到“${member.name || "当前管件"}”。`);
    }
    else ops.renderProject(context, view);
  }
}

export function entitiesFromProfile(profile) {
  const result = [];
  for (const contour of Array.isArray(profile?.contours) ? profile.contours : []) {
    const kind = String(contour?.kind ?? "");
    if (kind === "circle") {
      const center = profilePoint(contour?.center) ?? [0, 0];
      const radius = Number(contour?.radius ?? 0);
      if (radius > 0) result.push({ id: createEntityId(), kind: "circle", cx: center[0], cy: center[1], radius, closed: true });
      continue;
    }
    if (kind === "ellipse") {
      const center = profilePoint(contour?.center) ?? [0, 0];
      const radiusX = Number(contour?.radiusX ?? Number(contour?.width ?? 0) / 2);
      const radiusY = Number(contour?.radiusY ?? Number(contour?.height ?? 0) / 2);
      const rotation = Number(contour?.rotation ?? 0);
      if (radiusX > 0 && radiusY > 0) {
        if (Math.abs(radiusX - radiusY) <= 1e-7 && Math.abs(rotation) <= 1e-7) {
          result.push({ id: createEntityId(), kind: "circle", cx: center[0], cy: center[1], radius: radiusX, closed: true });
        } else {
          result.push({ id: createEntityId(), kind: "ellipse", cx: center[0], cy: center[1], radiusX, radiusY, rotation, closed: true });
        }
      }
      continue;
    }
    if (kind === "roundedRectangle" || kind === "capsule") {
      const center = profilePoint(contour?.center) ?? [0, 0];
      const width = Number(contour?.width ?? 0);
      const height = Number(contour?.height ?? 0);
      const radius = kind === "capsule"
        ? Math.min(width, height) / 2
        : Math.max(0, Math.min(Number(contour?.radius ?? 0), width / 2, height / 2));
      if (width > 0 && height > 0) {
        result.push({
          id: createEntityId(), kind: "rectangle",
          x: center[0] - width / 2, y: center[1] - height / 2,
          width, height, radius, closed: true,
        });
      }
      continue;
    }
    if (kind === "polygon") {
      const points = (Array.isArray(contour?.points) ? contour.points : []).map(profilePoint).filter(Boolean);
      if (points.length > 2) result.push({ id: createEntityId(), kind: "polyline", points: deduplicatePoints(points), closed: true });
      continue;
    }
    if (kind !== "path") continue;
    const contourStart=result.length;
    for (const segment of Array.isArray(contour?.segments) ? contour.segments : []) {
      const segmentKind = String(segment?.kind ?? "");
      if (segmentKind === "arc") {
        const arc = circularArcEntityFromProfileSegment(segment);
        if (arc) result.push(arc);
        continue;
      }
      if (segmentKind === "ellipseArc") {
        const center = profilePoint(segment?.center);
        const radiusX = Number(segment?.majorRadius ?? 0);
        const radiusY = Number(segment?.minorRadius ?? 0);
        const rotation = Number(segment?.rotation ?? 0);
        const startAngle = Number(segment?.startAngle ?? 0);
        const sweep = Number(segment?.endAngle ?? startAngle) - startAngle;
        if (center && radiusX > 0 && radiusY > 0 && Math.abs(sweep) > 1e-12) {
          result.push({
            id: createEntityId(), kind: "ellipseArc",
            cx: center[0], cy: center[1], radiusX, radiusY, rotation, startAngle, sweep,
            closed: false,
          });
        }
        continue;
      }
      if(segmentKind==="bezier"&&Array.isArray(segment.controlPoints)&&segment.controlPoints.length>=2) {
        result.push({id:createEntityId(),kind:"path",segments:[{kind:"bezier",points:structuredCloneValue(segment.controlPoints)}],closed:false});
        continue;
      }
      const points = sampleProfileSegment(segment);
      if (points.length < 2) continue;
      if (segmentKind === "line") {
        result.push({
          id: createEntityId(), kind: "line",
          x1: points[0][0], y1: points[0][1], x2: points.at(-1)[0], y2: points.at(-1)[1],
        });
      } else {
        result.push({
          id: createEntityId(),
          kind: "polyline",
          points: deduplicatePoints(points),
          closed: points.length > 2 && pointsNear(points[0], points.at(-1), 1e-6),
          sampled: true,
        });
      }
    }
    const contourEntities=result.slice(contourStart);
    if(contourEntities.length>0) {
      let path=editablePath(contourEntities[0]);
      for(const entity of contourEntities.slice(1)) {
        path=joinPathsAtEndpoints(path,editablePath(entity),1e-6)??joinPathsAtEndpoints(path,editablePath(entity,true),1e-6);
        if(!path)break;
      }
      if(path) {
        const closed=closeEditablePath(path,1e-6);
        result.splice(contourStart,contourEntities.length,compactEditablePath({...closed??path,id:createEntityId()},contourEntities.length===1));
      }
    }
  }
  return result;
}

function sampleProfileSegment(segment) {
  const kind = String(segment?.kind ?? "");
  if (kind === "line") {
    return [profilePoint(segment?.start), profilePoint(segment?.end)].filter(Boolean);
  }
  if (kind === "arc") return sampleProfileCircularArc(segment);
  if (kind === "ellipseArc") {
    const center = profilePoint(segment?.center);
    const radiusX = Number(segment?.majorRadius ?? 0);
    const radiusY = Number(segment?.minorRadius ?? 0);
    const rotation = Number(segment?.rotation ?? 0);
    const start = Number(segment?.startAngle ?? 0);
    const end = Number(segment?.endAngle ?? start);
    if (!center || !(radiusX > 0 && radiusY > 0) || !Number.isFinite(end - start)) return [];
    const steps = Math.max(8, Math.min(128, Math.ceil(Math.abs(end - start) / (Math.PI / 24))));
    return Array.from({ length: steps + 1 }, (_, index) => ellipseProfilePoint(
      center, radiusX, radiusY, rotation, start + (end - start) * index / steps,
    ));
  }
  const controls = (Array.isArray(segment?.controlPoints) ? segment.controlPoints : []).map(profilePoint).filter(Boolean);
  if (kind === "bezier") return sampleProfileBezier(controls, Math.min(128, Math.max(16, controls.length * 12)));
  if (kind === "bspline" || kind === "nurbs") {
    const sampled = sampleProfileSpline(segment, controls);
    return sampled.length >= 2 ? sampled : controls;
  }
  return [];
}

function sampleProfileCircularArc(segment) {
  const analytic = circularArcEntityFromProfileSegment(segment, false);
  if (!analytic) return [];
  return sampleAnalyticArc(analytic);
}

function circularArcEntityFromProfileSegment(segment, createId = true) {
  const exact=arcThroughPoints(segment?.start??[],segment?.middle??[],segment?.end??[]);
  if(!exact||!Number.isFinite(exact.radius))return null;
  return {...exact,...(createId?{id:createEntityId()}:{})};
}


function ellipseProfilePoint(center, radiusX, radiusY, rotation, angle) {
  const cosine = Math.cos(rotation);
  const sine = Math.sin(rotation);
  const x = radiusX * Math.cos(angle);
  const y = radiusY * Math.sin(angle);
  return [center[0] + x * cosine - y * sine, center[1] + x * sine + y * cosine];
}

function sampleRoundedRectangle(center, width, height, radius) {
  const halfWidth = width / 2;
  const halfHeight = height / 2;
  const corners = [
    [[center[0] + halfWidth - radius, center[1] - halfHeight + radius], -Math.PI / 2],
    [[center[0] + halfWidth - radius, center[1] + halfHeight - radius], 0],
    [[center[0] - halfWidth + radius, center[1] + halfHeight - radius], Math.PI / 2],
    [[center[0] - halfWidth + radius, center[1] - halfHeight + radius], Math.PI],
  ];
  return corners.flatMap(([corner, start]) => Array.from({ length: 9 }, (_, index) => {
    const angle = start + index / 8 * Math.PI / 2;
    return [roundCoordinate(corner[0] + Math.cos(angle) * radius), roundCoordinate(corner[1] + Math.sin(angle) * radius)];
  }));
}

function sampleProfileBezier(controls, count) {
  if (controls.length < 2) return [];
  return Array.from({ length: count + 1 }, (_, index) => {
    const ratio = index / count;
    const points = controls.map((point) => [...point]);
    for (let level = points.length - 1; level > 0; level -= 1) {
      for (let cursor = 0; cursor < level; cursor += 1) {
        points[cursor][0] = points[cursor][0] * (1 - ratio) + points[cursor + 1][0] * ratio;
        points[cursor][1] = points[cursor][1] * (1 - ratio) + points[cursor + 1][1] * ratio;
      }
    }
    return points[0].map(roundCoordinate);
  });
}

function sampleProfileSpline(segment, controls) {
  const degree = Math.trunc(Number(segment?.degree ?? 0));
  if (degree < 1 || controls.length < degree + 1) return [];
  const rational = String(segment?.kind ?? "") === "nurbs";
  let weights = rational ? (Array.isArray(segment?.weights) ? segment.weights : []).map(Number) : controls.map(() => 1);
  if (weights.length !== controls.length || weights.some((value) => !Number.isFinite(value) || value <= 0)) return [];
  let knots = expandProfileKnots(segment?.knots, segment?.multiplicities);
  let splineControls = controls;
  let domainStart;
  let domainEnd;
  if (segment?.periodic === true) {
    const endpointMultiplicity = knots.findIndex((value) => value !== knots[0]);
    const tailMultiplicity = knots.length - 1 - knots.findLastIndex((value) => value !== knots.at(-1));
    if (endpointMultiplicity < 1 || endpointMultiplicity !== tailMultiplicity
        || knots.length - endpointMultiplicity !== controls.length) return sampleClosedProfileControls(controls);
    const leftCount = degree + 1 - endpointMultiplicity;
    if (leftCount < 0) return sampleClosedProfileControls(controls);
    const positiveSteps = knots.slice(1).map((value, index) => value - knots[index]).filter((value) => value > 1e-12);
    const fallbackStep = positiveSteps[0] ?? 1;
    const left = [];
    let cursor = knots[0];
    for (let index = 0; index < leftCount; index += 1) {
      cursor -= positiveSteps.at(-1 - (index % positiveSteps.length)) ?? fallbackStep;
      left.unshift(cursor);
    }
    const right = [];
    cursor = knots.at(-1);
    for (let index = 0; index < degree; index += 1) {
      cursor += positiveSteps[index % positiveSteps.length] ?? fallbackStep;
      right.push(cursor);
    }
    domainStart = knots[0];
    domainEnd = knots.at(-1);
    knots = [...left, ...knots, ...right];
    splineControls = [...controls, ...controls.slice(0, degree)];
    weights = [...weights, ...weights.slice(0, degree)];
  } else {
    if (knots.length !== controls.length + degree + 1) return [];
    domainStart = knots[degree];
    domainEnd = knots[controls.length];
  }
  domainStart = Number.isFinite(Number(segment?.startParameter)) ? Number(segment.startParameter) : domainStart;
  domainEnd = Number.isFinite(Number(segment?.endParameter)) ? Number(segment.endParameter) : domainEnd;
  if (!(domainEnd > domainStart)) return [];
  const steps = Math.max(24, Math.min(256, controls.length * 16));
  const values = [];
  for (let index = 0; index <= steps; index += 1) {
    const value = segment?.periodic === true && index === steps
      ? values[0]
      : evaluateProfileSpline(splineControls, weights, knots, degree, domainStart + (domainEnd - domainStart) * index / steps);
    if (value) values.push(value.map(roundCoordinate));
  }
  return values;
}

function sampleClosedProfileControls(controls) {
  if (controls.length < 3) return controls;
  const values = [];
  for (let index = 0; index < controls.length; index += 1) {
    const center = controls[index];
    const right = controls[(index + 1) % controls.length];
    for (let step = 0; step < 16; step += 1) {
      const ratio = step / 16;
      const inverse = 1 - ratio;
      values.push([
        inverse * inverse * center[0] + 2 * inverse * ratio * ((center[0] + right[0]) / 2) + ratio * ratio * right[0],
        inverse * inverse * center[1] + 2 * inverse * ratio * ((center[1] + right[1]) / 2) + ratio * ratio * right[1],
      ].map(roundCoordinate));
    }
  }
  values.push([...values[0]]);
  return values;
}

function evaluateProfileSpline(controls, weights, knots, degree, parameter) {
  const lastControl = controls.length - 1;
  let span = degree;
  if (parameter >= knots[lastControl + 1] - 1e-12) span = lastControl;
  else {
    let low = degree;
    let high = lastControl + 1;
    while (high - low > 1) {
      const middle = Math.floor((low + high) / 2);
      if (parameter < knots[middle]) high = middle;
      else low = middle;
    }
    span = low;
  }
  const values = [];
  for (let index = 0; index <= degree; index += 1) {
    const controlIndex = span - degree + index;
    const weight = weights[controlIndex];
    values.push([controls[controlIndex][0] * weight, controls[controlIndex][1] * weight, weight]);
  }
  for (let level = 1; level <= degree; level += 1) {
    for (let index = degree; index >= level; index -= 1) {
      const controlIndex = span - degree + index;
      const denominator = knots[controlIndex + degree - level + 1] - knots[controlIndex];
      const alpha = Math.abs(denominator) <= 1e-15 ? 0 : (parameter - knots[controlIndex]) / denominator;
      values[index] = values[index - 1].map((value, component) => value * (1 - alpha) + values[index][component] * alpha);
    }
  }
  const value = values[degree];
  return Math.abs(value[2]) <= 1e-15 ? null : [value[0] / value[2], value[1] / value[2]];
}

function expandProfileKnots(rawKnots, rawMultiplicities) {
  const knots = (Array.isArray(rawKnots) ? rawKnots : []).map(Number);
  if (knots.some((value) => !Number.isFinite(value))) return [];
  if (!Array.isArray(rawMultiplicities)) return knots;
  if (rawMultiplicities.length !== knots.length) return [];
  const expanded = [];
  for (let index = 0; index < knots.length; index += 1) {
    const count = Math.trunc(Number(rawMultiplicities[index]));
    if (count < 1) return [];
    for (let repeat = 0; repeat < count; repeat += 1) expanded.push(knots[index]);
  }
  return expanded;
}

function profilePoint(value) {
  if (!Array.isArray(value) || value.length < 2) return null;
  const point = [Number(value[0]), Number(value[1])];
  return point.every(Number.isFinite) ? point : null;
}


function analyticCurvePoint(entity, angle) {
  const center = [Number(entity.cx), Number(entity.cy)];
  if (entity.kind === "circle" || entity.kind === "circleArc") {
    const radius = Number(entity.radius);
    return [
      center[0] + Math.cos(angle) * radius,
      center[1] + Math.sin(angle) * radius,
    ];
  }
  return ellipseProfilePoint(
    center,
    Number(entity.radiusX),
    Number(entity.radiusY),
    Number(entity.rotation ?? 0),
    angle,
  );
}

function ellipseLocalPoint(entity, point) {
  const dx = Number(point[0]) - Number(entity.cx);
  const dy = Number(point[1]) - Number(entity.cy);
  const rotation = Number(entity.rotation ?? 0);
  const cosine = Math.cos(rotation);
  const sine = Math.sin(rotation);
  return [dx * cosine + dy * sine, -dx * sine + dy * cosine];
}


function sampleAnalyticArc(entity) {
  const sweep = Number(entity.sweep);
  const steps = Math.max(8, Math.min(256, Math.ceil(Math.abs(sweep) / FULL_ANGLE * 96)));
  return Array.from({ length: steps + 1 }, (_, index) => (
    analyticCurvePoint(entity, Number(entity.startAngle) + sweep * index / steps)
  ));
}


function entityPolygon(entity) {
  if (entity.kind === "rectangle") {
    const radius = Math.max(0, Math.min(Number(entity.radius ?? 0), Number(entity.width) / 2, Number(entity.height) / 2));
    if (radius > 1e-7) {
      return sampleRoundedRectangle(
        [Number(entity.x) + Number(entity.width) / 2, Number(entity.y) + Number(entity.height) / 2],
        Number(entity.width), Number(entity.height), radius,
      );
    }
    return [[entity.x, entity.y], [entity.x + entity.width, entity.y], [entity.x + entity.width, entity.y + entity.height], [entity.x, entity.y + entity.height]];
  }
  if (entity.kind === "circle") {
    return Array.from({ length: 48 }, (_, index) => {
      const angle = (index / 48) * Math.PI * 2;
      return [entity.cx + Math.cos(angle) * entity.radius, entity.cy + Math.sin(angle) * entity.radius];
    });
  }
  if (entity.kind === "ellipse") {
    return Array.from({ length: 96 }, (_, index) => analyticCurvePoint(entity, index / 96 * FULL_ANGLE));
  }
  return deduplicatePoints(entity.points ?? []);
}

function entityBounds(entity) {
  let points = [];
  if (entity.kind === "line") points = [[entity.x1, entity.y1], [entity.x2, entity.y2]];
  else if (entity.kind === "rectangle") points = [[entity.x, entity.y], [entity.x + entity.width, entity.y + entity.height]];
  else if (entity.kind === "circle") points = [[entity.cx - entity.radius, entity.cy - entity.radius], [entity.cx + entity.radius, entity.cy + entity.radius]];
  else if (["path", "ellipse", "circleArc", "ellipseArc", "arc", "spline"].includes(entity.kind)) points = entityBoundaryPoints(entity);
  else if (entity.kind === "text") points = [[entity.x, entity.y]];
  else points = entity.points ?? [];
  if (!points.length) return { minX: 0, minY: 0, maxX: 0, maxY: 0 };
  return {
    minX: Math.min(...points.map((point) => Number(point[0]))),
    minY: Math.min(...points.map((point) => Number(point[1]))),
    maxX: Math.max(...points.map((point) => Number(point[0]))),
    maxY: Math.max(...points.map((point) => Number(point[1]))),
  };
}

function entityLength(entity) {
  if (entity.kind === "circle") return Math.PI * entity.radius * 2;
  if (entity.kind === "circleArc") return Math.abs(entity.radius*entity.sweep);
  if(entity.kind==="path") return entity.segments.reduce((total,s)=>total+(isArc(s)&&s.kind==="circleArc"?Math.abs(s.radius*s.sweep):pathSamples({kind:"path",segments:[s]}).reduce((sum,p,i,all)=>sum+(i?pointDistance2d(p,all[i-1]):0),0)),0);
  const points = entity.kind === "line"
    ? [[entity.x1, entity.y1], [entity.x2, entity.y2]]
    : (entity.kind === "circleArc" || entity.kind === "ellipseArc" ? entityBoundaryPoints(entity) : entityPolygon(entity));
  let length = 0;
  for (let index = 1; index < points.length; index += 1) length += Math.hypot(points[index][0] - points[index - 1][0], points[index][1] - points[index - 1][1]);
  if (entity.closed && points.length > 2) length += Math.hypot(points[0][0] - points.at(-1)[0], points[0][1] - points.at(-1)[1]);
  return length;
}

function signedArea(points) {
  return points.reduce((sum, point, index) => {
    const next = points[(index + 1) % points.length];
    return sum + point[0] * next[1] - next[0] * point[1];
  }, 0) / 2;
}

function normalizeEntity(entity) {
  if (!entity || typeof entity !== "object") return null;
  return { ...entity, id: String(entity.id || createEntityId()) };
}

function deduplicatePoints(points) {
  const result = [];
  for (const point of points) {
    if (!Array.isArray(point) || !Number.isFinite(Number(point[0])) || !Number.isFinite(Number(point[1]))) continue;
    const normalized = [Number(point[0]), Number(point[1])];
    const previous = result.at(-1);
    if (!previous || Math.hypot(previous[0] - normalized[0], previous[1] - normalized[1]) > 1e-6) result.push(normalized);
  }
  if (result.length > 2 && Math.hypot(result[0][0] - result.at(-1)[0], result[0][1] - result.at(-1)[1]) <= 1e-6) result.pop();
  return result;
}

function createEntityId() {
  return globalThis.crypto?.randomUUID?.() ?? `sketch-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function roundCoordinate(value) {
  return Math.round(Number(value) * 1000) / 1000;
}
