import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";

const SECTION_MODE = "section";
const SIDE_MODE = "side";
const SKETCH_SCHEMA = "icax.tube-sketch";
const CANVAS = Object.freeze({ width: 1000, height: 600, left: 70, right: 950, top: 90, bottom: 510 });
const DRAW_TOOLS = new Set(["line", "polyline", "rectangle", "circle", "arc", "spline", "freehand", "text"]);

export function createInitialSketchState() {
  return {
    mode: SECTION_MODE,
    tool: "select",
    sectionName: "我的草图管型",
    section: createDraft(),
    sideByMember: {},
    targetMemberId: "",
    loadedProductId: "",
  };
}

export function ensureSketchState(view) {
  view.tubeDesignerSketch ??= createInitialSketchState();
  const state = view.tubeDesignerSketch;
  if (![SECTION_MODE, SIDE_MODE].includes(state.mode)) state.mode = SECTION_MODE;
  if (!state.section || !Array.isArray(state.section.entities)) state.section = createDraft();
  state.sideByMember ??= {};

  const designer = view.scene?.tubeDesigner ?? {};
  const productId = String(designer.product?.entityId ?? "");
  const members = sketchableMembers(designer);
  if (state.loadedProductId !== productId) {
    state.loadedProductId = productId;
    state.sideByMember = {};
    const stored = designer.product?.sketches?.side;
    if (stored && typeof stored === "object") {
      for (const [memberId, sketch] of Object.entries(stored)) {
        state.sideByMember[memberId] = draftFromStoredSketch(sketch);
      }
    }
    state.targetMemberId = members.some((member) => member.entityId === state.targetMemberId)
      ? state.targetMemberId : String(members[0]?.entityId ?? "");
  }
  if (!members.some((member) => member.entityId === state.targetMemberId)) {
    state.targetMemberId = String(members[0]?.entityId ?? "");
  }
  if (state.targetMemberId && !state.sideByMember[state.targetMemberId]) {
    state.sideByMember[state.targetMemberId] = createDraft();
  }
  return state;
}

export function isSectionPreviewReady(draft) {
  return closedSectionEntities(draft).length > 0;
}

export function renderSketchLeftPane(_context, view) {
  const state = ensureSketchState(view);
  const draft = currentDraft(view, state);
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetMember(designer, state.targetMemberId);
  const members = sketchableMembers(designer);
  const sectionReady = isSectionPreviewReady(state.section);
  const sectionHasGeometry = state.section.entities.length > 0;

  return `<div class="tube-sketch-preview-panel" data-tube-sketch-preview-panel>
    <header>
      <strong>${state.mode === SECTION_MODE ? "管型预览" : "三维切割预览"}</strong>
      <span>${state.mode === SECTION_MODE
        ? "截面闭合后自动生成三维管型"
        : (member ? "实时查看图形在管子侧面的效果" : "请先在产品页生成一根管件")}</span>
    </header>
    <div class="tube-sketch-preview-stage">
      ${state.mode === SECTION_MODE
        ? renderSectionPreview(state.section, sectionReady, sectionHasGeometry)
        : renderSidePreview(draft, member)}
    </div>
    <footer>
      ${state.mode === SIDE_MODE && members.length > 1 ? `<label>
        <span>当前管件</span>
        <select data-cam-change-action="tube-designer-sketch-target-member">
          ${members.map((item) => `<option value="${escapeAttr(item.entityId)}" ${item.entityId === state.targetMemberId ? "selected" : ""}>${escapeText(item.name || `管件 ${item.index ?? ""}`)}</option>`).join("")}
        </select>
      </label>` : `<span>${state.mode === SECTION_MODE
        ? (sectionReady ? "三维预览已更新" : "等待有效截面")
        : escapeText(member?.name ?? "没有可用管件")}</span>`}
    </footer>
  </div>`;
}

export function renderSketchViewportOverlay(_context, view) {
  const state = ensureSketchState(view);
  const draft = currentDraft(view, state);
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetMember(designer, state.targetMemberId);
  const metrics = canvasMetrics(state.mode, member);
  const selectedId = String(draft.selectedId ?? "");
  const modeTitle = state.mode === SECTION_MODE ? "管型截面图" : "管型侧面切割图";
  const subtitle = state.mode === SECTION_MODE
    ? "绘制一个外轮廓，并按需要增加内孔"
    : (member ? `${member.name || "当前管件"} · ${formatNumber(member.length || metrics.xMax)} mm` : "需要先在产品页生成管件");

  return `<div class="tube-sketch-canvas-shell" data-tube-sketch-canvas-shell data-tube-sketch-mode="${state.mode}">
    <header class="tube-sketch-canvas-header">
      <div><strong>${modeTitle}</strong><span>${escapeText(subtitle)}</span></div>
      <div class="tube-sketch-mode-switch" role="group" aria-label="草图类型">
        <button type="button" data-cam-action="tube-designer-sketch-switch-mode" data-tube-sketch-mode="section" aria-pressed="${state.mode === SECTION_MODE}">管型截面图</button>
        <button type="button" data-cam-action="tube-designer-sketch-switch-mode" data-tube-sketch-mode="side" aria-pressed="${state.mode === SIDE_MODE}">管型侧面切割图</button>
      </div>
    </header>
    <div class="tube-sketch-canvas-stage">
      <svg class="tube-sketch-canvas" data-tube-sketch-canvas viewBox="0 0 ${CANVAS.width} ${CANVAS.height}" role="img" aria-label="${modeTitle}绘图区">
        <defs>
          <pattern id="tube-sketch-small-grid-${state.mode}" width="20" height="20" patternUnits="userSpaceOnUse"><path d="M20 0H0V20" /></pattern>
          <pattern id="tube-sketch-grid-${state.mode}" width="100" height="100" patternUnits="userSpaceOnUse"><rect width="100" height="100" fill="url(#tube-sketch-small-grid-${state.mode})"/><path d="M100 0H0V100" /></pattern>
        </defs>
        <rect class="tube-sketch-grid" width="1000" height="600" fill="url(#tube-sketch-grid-${state.mode})" />
        ${state.mode === SECTION_MODE ? renderSectionCanvasGuide() : renderSideCanvasGuide(metrics, member)}
        <g class="tube-sketch-geometry">
          ${draft.entities.map((entity) => renderSketchEntity(entity, state.mode, metrics, entity.id === selectedId)).join("")}
        </g>
        <g class="tube-sketch-draft-preview" data-tube-sketch-draft-preview></g>
      </svg>
      ${state.mode === SIDE_MODE && !member ? `<div class="tube-sketch-canvas-empty"><strong>还没有可以绘制的管件</strong><span>先到“产品”生成产品，随后即可在管件侧面绘制切割图。</span></div>` : ""}
    </div>
    <footer class="tube-sketch-canvas-status">
      <span>${toolLabel(state.tool)}</span>
      <span>${draft.entities.length} 个图形${selectedId ? " · 已选中 1 个" : ""}</span>
      <span>${draft.dirty ? "未保存" : (draft.entities.length ? "已保存" : "空草图")}</span>
    </footer>
  </div>`;
}

export function renderSketchRightPane(_context, view) {
  const state = ensureSketchState(view);
  const draft = currentDraft(view, state);
  const selected = draft.entities.find((entity) => entity.id === draft.selectedId) ?? null;
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetMember(designer, state.targetMemberId);
  const canCommit = state.mode === SECTION_MODE
    ? isSectionPreviewReady(state.section)
    : Boolean(member && draft.entities.length);

  return `<div class="tube-sketch-property-panel">
    <header>
      <strong>${selected ? entityLabel(selected) : "图形属性"}</strong>
      <span>${selected ? "已选中 1 个图形" : "选择一个图形后可查看和修改参数"}</span>
    </header>
    <div class="tube-sketch-property-body">
      ${selected ? renderEntityProperties(selected) : `<div class="tube-sketch-property-empty">
        <span class="tube-sketch-property-empty-icon">⌁</span>
        <strong>尚未选择图形</strong>
        <span>在中间绘图区选择一条曲线、圆或矩形。</span>
      </div>`}
    </div>
    <footer>
      ${selected ? `<button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-sketch-delete-selected">删除所选图形</button>` : "<span></span>"}
      <button type="button" class="tube-designer-primary" data-cam-action="tube-designer-sketch-commit" ${canCommit && !view.pending ? "" : "disabled"}>${state.mode === SECTION_MODE ? "保存到管型" : "应用到管件"}</button>
    </footer>
  </div>`;
}

export async function handleSketchAreaAction(context, view, action, target, ops) {
  if (!String(action).startsWith("tube-designer-sketch-")) return { handled: false };
  const state = ensureSketchState(view);
  if (action === "tube-designer-sketch-switch-mode") {
    state.mode = target?.dataset?.tubeSketchMode === SIDE_MODE ? SIDE_MODE : SECTION_MODE;
    state.tool = "select";
    view.error = "";
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-sketch-target-member") {
    state.targetMemberId = String(target?.value ?? "");
    state.sideByMember[state.targetMemberId] ??= createDraft();
    state.tool = "select";
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
    state.mode = commandId.endsWith("side") ? SIDE_MODE : SECTION_MODE;
    state.tool = "select";
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.import") {
    await importSketchDxf(context, view, ops);
    return true;
  }
  if (commandId === "sketch.undo") {
    undoDraft(currentDraft(view, state));
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.redo") {
    redoDraft(currentDraft(view, state));
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.delete") {
    deleteSelected(currentDraft(view, state));
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "sketch.commit") {
    if (state.mode === SECTION_MODE) await saveSectionProfile(context, view, ops);
    else await saveSideSketch(context, view, ops);
    return true;
  }
  const tool = commandId.replace(/^sketch\./, "");
  if (tool === "select" || DRAW_TOOLS.has(tool)) {
    state.tool = tool;
    ops.renderProject(context, view);
    return true;
  }
  return true;
}

export function attachSketchAreaInteractions(context, view, mount, ops) {
  const state = ensureSketchState(view);
  if (view.activeAreaId !== "sketch" || view.pending) return;
  const svg = mount?.querySelector?.("[data-tube-sketch-canvas]");
  if (!svg || svg.dataset.tubeSketchAttached === "true") return;
  svg.dataset.tubeSketchAttached = "true";
  const designer = view.scene?.tubeDesigner ?? {};
  const member = targetMember(designer, state.targetMemberId);
  if (state.mode === SIDE_MODE && !member) return;
  const metrics = canvasMetrics(state.mode, member);
  const draft = currentDraft(view, state);
  let gesture = null;

  svg.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    const entityTarget = event.target instanceof Element ? event.target.closest("[data-tube-sketch-entity-id]") : null;
    if (state.tool === "select") {
      const selectedId = String(entityTarget?.dataset?.tubeSketchEntityId ?? "");
      if (draft.selectedId !== selectedId) {
        draft.selectedId = selectedId;
        ops.renderProject(context, view);
      }
      return;
    }
    const start = eventToModelPoint(svg, event, state.mode, metrics);
    if (state.tool === "text") {
      const value = String(globalThis.prompt?.("输入文字", "文字") ?? "").trim();
      if (value) addEntity(draft, { id: createEntityId(), kind: "text", x: start[0], y: start[1], value });
      ops.renderProject(context, view);
      return;
    }
    gesture = { start, points: [start] };
    svg.setPointerCapture?.(event.pointerId);
  });

  svg.addEventListener("pointermove", (event) => {
    if (!gesture) return;
    const point = eventToModelPoint(svg, event, state.mode, metrics);
    if (["freehand", "polyline", "spline"].includes(state.tool)) {
      const previous = gesture.points.at(-1);
      if (!previous || Math.hypot(point[0] - previous[0], point[1] - previous[1]) > metrics.sampleStep) {
        gesture.points.push(point);
      }
    } else {
      gesture.points = [gesture.start, point];
    }
    renderGesturePreview(svg, state.tool, gesture, state.mode, metrics);
  });

  const finishGesture = (event) => {
    if (!gesture) return;
    const point = eventToModelPoint(svg, event, state.mode, metrics);
    if (gesture.points.length === 1) gesture.points.push(point);
    const entity = entityFromGesture(state.tool, gesture, point, metrics);
    gesture = null;
    clearGesturePreview(svg);
    if (entity) addEntity(draft, entity);
    ops.renderProject(context, view);
  };
  svg.addEventListener("pointerup", finishGesture);
  svg.addEventListener("pointercancel", () => {
    gesture = null;
    clearGesturePreview(svg);
  });
}

export function buildProfileFromSectionDraft(draft, name = "我的草图管型") {
  const entities = closedSectionEntities(draft);
  if (!entities.length) throw new Error("请先绘制一个闭合截面。");
  const polygons = entities.map(entityPolygon).filter((points) => points.length >= 3);
  if (!polygons.length) throw new Error("当前截面没有可保存的闭合轮廓。");
  polygons.sort((left, right) => Math.abs(signedArea(right)) - Math.abs(signedArea(left)));
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
  const contours = polygons.map((points) => ({
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
    wallThickness: 0,
    cornerRadius: 0,
    wallThicknessApproximate: contours.length > 1,
    specification: `草图 ${formatNumber(width)} × ${formatNumber(depth)} mm`,
    hollow: contours.length > 1,
    contourCount: contours.length,
    contours,
  };
}

function createDraft() {
  return { entities: [], selectedId: "", history: [], future: [], dirty: false };
}

function currentDraft(view, state = ensureSketchState(view)) {
  if (state.mode === SECTION_MODE) return state.section;
  const key = state.targetMemberId || "unassigned";
  state.sideByMember[key] ??= createDraft();
  return state.sideByMember[key];
}

function draftFromStoredSketch(sketch) {
  return {
    entities: Array.isArray(sketch?.entities) ? sketch.entities.map(normalizeEntity).filter(Boolean) : [],
    selectedId: "",
    history: [],
    future: [],
    dirty: false,
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

function memberFaceHeight(member) {
  const profile = member?.profile ?? member?.properties?.["tubeDesigner.profile"] ?? {};
  return Math.max(1, Number(profile.depth ?? profile.height ?? profile.width ?? 50));
}

function canvasMetrics(mode, member) {
  if (mode === SECTION_MODE) {
    return { xMin: -110, xMax: 110, yMin: -70, yMax: 70, sampleStep: 0.8 };
  }
  const length = Math.max(1, Number(member?.length ?? 1000));
  const height = memberFaceHeight(member);
  return { xMin: 0, xMax: length, yMin: 0, yMax: height, sampleStep: Math.max(0.5, length / 1000) };
}

function modelToCanvas(point, metrics) {
  const x = CANVAS.left + ((point[0] - metrics.xMin) / (metrics.xMax - metrics.xMin)) * (CANVAS.right - CANVAS.left);
  const y = CANVAS.bottom - ((point[1] - metrics.yMin) / (metrics.yMax - metrics.yMin)) * (CANVAS.bottom - CANVAS.top);
  return [x, y];
}

function canvasToModel(point, metrics) {
  const x = metrics.xMin + ((point[0] - CANVAS.left) / (CANVAS.right - CANVAS.left)) * (metrics.xMax - metrics.xMin);
  const y = metrics.yMin + ((CANVAS.bottom - point[1]) / (CANVAS.bottom - CANVAS.top)) * (metrics.yMax - metrics.yMin);
  return [roundCoordinate(x), roundCoordinate(y)];
}

function eventToModelPoint(svg, event, _mode, metrics) {
  const point = svg.createSVGPoint();
  point.x = event.clientX;
  point.y = event.clientY;
  const local = point.matrixTransform(svg.getScreenCTM().inverse());
  return canvasToModel([
    Math.min(CANVAS.right, Math.max(CANVAS.left, local.x)),
    Math.min(CANVAS.bottom, Math.max(CANVAS.top, local.y)),
  ], metrics);
}

function renderSectionCanvasGuide() {
  return `<g class="tube-sketch-axis"><line x1="70" y1="300" x2="950" y2="300"/><line x1="500" y1="90" x2="500" y2="510"/><text x="930" y="292">X</text><text x="509" y="108">Y</text></g>`;
}

function renderSideCanvasGuide(metrics, member) {
  const length = formatNumber(metrics.xMax);
  const height = formatNumber(metrics.yMax);
  return `<g class="tube-sketch-side-guide">
    <rect x="70" y="90" width="880" height="420" rx="2"/>
    <text x="88" y="116">${escapeText(member?.name ?? "管件侧面")} · 可绘制区域</text>
    <text x="70" y="535">0</text><text x="902" y="535">${length} mm</text><text x="905" y="82">${height} mm</text>
  </g>`;
}

function renderSketchEntity(entity, mode, metrics, selected) {
  const className = `tube-sketch-entity ${selected ? "selected" : ""}`;
  const attributes = `class="${className}" data-tube-sketch-entity-id="${escapeAttr(entity.id)}"`;
  if (entity.kind === "line") {
    const start = modelToCanvas([entity.x1, entity.y1], metrics);
    const end = modelToCanvas([entity.x2, entity.y2], metrics);
    return `<g>${selectionHalo(`<line ${attributes} x1="${start[0]}" y1="${start[1]}" x2="${end[0]}" y2="${end[1]}"/>`, selected)}${selected ? renderHandles([start, end]) : ""}</g>`;
  }
  if (entity.kind === "rectangle") {
    const a = modelToCanvas([entity.x, entity.y], metrics);
    const b = modelToCanvas([entity.x + entity.width, entity.y + entity.height], metrics);
    const x = Math.min(a[0], b[0]);
    const y = Math.min(a[1], b[1]);
    const width = Math.abs(b[0] - a[0]);
    const height = Math.abs(b[1] - a[1]);
    return `<g>${selectionHalo(`<rect ${attributes} x="${x}" y="${y}" width="${width}" height="${height}" rx="${Math.max(0, Number(entity.radius ?? 0))}"/>`, selected)}${selected ? renderHandles([[x, y], [x + width, y], [x, y + height], [x + width, y + height]]) : ""}</g>`;
  }
  if (entity.kind === "circle") {
    const center = modelToCanvas([entity.cx, entity.cy], metrics);
    const edge = modelToCanvas([entity.cx + entity.radius, entity.cy], metrics);
    const radius = Math.abs(edge[0] - center[0]);
    return `<g>${selectionHalo(`<circle ${attributes} cx="${center[0]}" cy="${center[1]}" r="${radius}"/>`, selected)}${selected ? renderHandles([center, [center[0] + radius, center[1]]]) : ""}</g>`;
  }
  if (entity.kind === "text") {
    const point = modelToCanvas([entity.x, entity.y], metrics);
    return `<text ${attributes} x="${point[0]}" y="${point[1]}">${escapeText(entity.value)}</text>`;
  }
  const points = (entity.points ?? []).map((point) => modelToCanvas(point, metrics));
  if (!points.length) return "";
  const d = curvePath(points, entity.kind, Boolean(entity.closed));
  return `<g>${selectionHalo(`<path ${attributes} d="${d}"/>`, selected)}${selected ? renderHandles(points) : ""}</g>`;
}

function selectionHalo(markup, _selected) {
  return markup;
}

function renderHandles(points) {
  return `<g class="tube-sketch-handles">${points.map((point) => `<circle cx="${point[0]}" cy="${point[1]}" r="4"/>`).join("")}</g>`;
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
  } else if (entity.kind === "circle") {
    controls.push(field("圆心 X", "centerX", entity.cx));
    controls.push(field("圆心 Y", "centerY", entity.cy));
    controls.push(field("直径", "diameter", entity.radius * 2, "mm", 0.1));
  } else if (entity.kind === "line") {
    controls.push(field("起点 X", "x1", entity.x1));
    controls.push(field("起点 Y", "y1", entity.y1));
    controls.push(field("终点 X", "x2", entity.x2));
    controls.push(field("终点 Y", "y2", entity.y2));
  } else if (entity.kind === "text") {
    controls.push(`<label class="wide"><span>文字</span><input type="text" value="${escapeAttr(entity.value)}" data-sketch-field="value" data-sketch-entity-id="${escapeAttr(entity.id)}" data-cam-change-action="tube-designer-sketch-property"/></label>`);
    controls.push(field("位置 X", "x", entity.x));
    controls.push(field("位置 Y", "y", entity.y));
  } else {
    const bounds = entityBounds(entity);
    controls.push(field("中心 X", "centerX", (bounds.minX + bounds.maxX) / 2));
    controls.push(field("中心 Y", "centerY", (bounds.minY + bounds.maxY) / 2));
    controls.push(field("宽度", "width", bounds.maxX - bounds.minX, "mm", 0.1));
    controls.push(field("高度", "height", bounds.maxY - bounds.minY, "mm", 0.1));
  }
  const isCurve = ["polyline", "spline", "freehand", "arc"].includes(entity.kind);
  const length = entityLength(entity);
  return `<section class="tube-sketch-property-section">
      <div class="tube-sketch-property-title"><strong>所选图形</strong><span>${escapeText(entityLabel(entity))}</span></div>
      <dl><div><dt>长度</dt><dd>${formatNumber(length)} mm</dd></div>${isCurve ? `<div><dt>控制点</dt><dd>${entity.points?.length ?? 0} 个</dd></div><div><dt>状态</dt><dd>${entity.closed ? "已闭合" : "开放"}</dd></div>` : ""}</dl>
    </section>
    <section class="tube-sketch-property-section">
      <div class="tube-sketch-property-title"><strong>几何参数</strong><span>毫米</span></div>
      <div class="tube-sketch-property-grid">${controls.join("")}</div>
    </section>
    ${isCurve ? `<section class="tube-sketch-property-section"><div class="tube-sketch-property-actions">
      <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-sketch-reverse">反转方向</button>
      <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-sketch-toggle-closed">${entity.closed ? "打开曲线" : "闭合曲线"}</button>
    </div></section>` : ""}`;
}

function field(label, key, value, unit = "mm", step = 0.1) {
  return `<label><span>${label}</span><span class="tube-sketch-property-input"><input type="number" step="${step}" value="${escapeAttr(roundCoordinate(value))}" data-sketch-field="${key}" data-cam-change-action="tube-designer-sketch-property"/><small>${unit}</small></span></label>`;
}

function renderSectionPreview(draft, ready, hasGeometry) {
  if (!ready) {
    return `<div class="tube-sketch-preview-empty"><span class="tube-sketch-preview-empty-icon">◇</span><strong>${hasGeometry ? "截面尚未闭合" : "还没有截面"}</strong><span>${hasGeometry ? "连接开放端点后生成三维预览" : "完成一个有效的闭合截面后，这里将显示三维预览"}</span></div>`;
  }
  const geometry = previewProfileGeometry(draft);
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

function renderSidePreview(draft, member) {
  if (!member) return `<div class="tube-sketch-preview-empty"><span class="tube-sketch-preview-empty-icon">◇</span><strong>还没有管件</strong><span>生成产品后即可绘制侧面切割图</span></div>`;
  const metrics = canvasMetrics(SIDE_MODE, member);
  const paths = draft.entities.map((entity) => renderPreviewSideEntity(entity, metrics)).join("");
  return `<svg class="tube-sketch-preview-svg tube-sketch-side-preview" viewBox="0 0 300 300" role="img" aria-label="当前管件侧面切割三维预览">
    <polygon class="tube-sketch-metal-top" points="35,88 82,56 273,86 226,118"/>
    <polygon class="tube-sketch-metal-side" points="226,118 273,86 273,205 226,237"/>
    <rect class="tube-sketch-metal-front" x="35" y="88" width="191" height="149"/>
    <g class="tube-sketch-preview-cut">${paths}</g>
  </svg>`;
}

function renderPreviewSideEntity(entity, metrics) {
  const map = (point) => {
    const x = 45 + ((point[0] - metrics.xMin) / (metrics.xMax - metrics.xMin)) * 170;
    const y = 224 - ((point[1] - metrics.yMin) / (metrics.yMax - metrics.yMin)) * 122;
    return [x, y];
  };
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
  if (entity.kind === "text") {
    const p = map([entity.x, entity.y]);
    return `<text x="${p[0]}" y="${p[1]}">${escapeText(entity.value)}</text>`;
  }
  const points = (entity.points ?? []).map(map);
  return points.length ? `<path d="${curvePath(points, entity.kind, Boolean(entity.closed))}"/>` : "";
}

function previewProfileGeometry(draft) {
  const polygons = closedSectionEntities(draft).map(entityPolygon).filter((points) => points.length >= 3);
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
  const labels = { select: "选择工具", line: "直线", polyline: "折线", rectangle: "矩形", circle: "圆", arc: "圆弧", spline: "样条", freehand: "自由曲线", text: "文字" };
  return labels[tool] ?? "选择工具";
}

function entityLabel(entity) {
  return ({ line: "直线", polyline: "折线", rectangle: "矩形", circle: "圆", arc: "圆弧", spline: "样条曲线", freehand: "自由曲线", text: "文字" })[entity?.kind] ?? "图形";
}

function entityFromGesture(tool, gesture, end, metrics) {
  const start = gesture.start;
  if (Math.hypot(end[0] - start[0], end[1] - start[1]) < metrics.sampleStep) return null;
  const id = createEntityId();
  if (tool === "line") return { id, kind: "line", x1: start[0], y1: start[1], x2: end[0], y2: end[1] };
  if (tool === "rectangle") return { id, kind: "rectangle", x: Math.min(start[0], end[0]), y: Math.min(start[1], end[1]), width: Math.abs(end[0] - start[0]), height: Math.abs(end[1] - start[1]), radius: 0, closed: true };
  if (tool === "circle") return { id, kind: "circle", cx: start[0], cy: start[1], radius: Math.hypot(end[0] - start[0], end[1] - start[1]), closed: true };
  if (tool === "arc") {
    const control = [(start[0] + end[0]) / 2, (start[1] + end[1]) / 2 + Math.hypot(end[0] - start[0], end[1] - start[1]) * 0.35];
    return { id, kind: "arc", points: [start, control, end], closed: false };
  }
  const points = deduplicatePoints([...gesture.points, end]);
  if (points.length < 2) return null;
  return { id, kind: tool, points, closed: false };
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

function addEntity(draft, entity) {
  pushHistory(draft);
  draft.entities.push(normalizeEntity(entity));
  draft.selectedId = entity.id;
  draft.future = [];
  draft.dirty = true;
}

function deleteSelected(draft) {
  if (!draft.selectedId) return;
  const next = draft.entities.filter((entity) => entity.id !== draft.selectedId);
  if (next.length === draft.entities.length) return;
  pushHistory(draft);
  draft.entities = next;
  draft.selectedId = "";
  draft.future = [];
  draft.dirty = true;
}

function reverseSelected(draft) {
  const entity = draft.entities.find((item) => item.id === draft.selectedId);
  if (!entity) return;
  pushHistory(draft);
  if (entity.kind === "line") {
    [entity.x1, entity.x2] = [entity.x2, entity.x1];
    [entity.y1, entity.y2] = [entity.y2, entity.y1];
  } else if (Array.isArray(entity.points)) entity.points.reverse();
  draft.future = [];
  draft.dirty = true;
}

function toggleSelectedClosed(draft) {
  const entity = draft.entities.find((item) => item.id === draft.selectedId);
  if (!entity || !Array.isArray(entity.points)) return;
  pushHistory(draft);
  entity.closed = !entity.closed;
  draft.future = [];
  draft.dirty = true;
}

function updateSelectedProperty(draft, target) {
  const entity = draft.entities.find((item) => item.id === draft.selectedId);
  const key = String(target?.dataset?.sketchField ?? "");
  if (!entity || !key) return;
  pushHistory(draft);
  if (key === "value") {
    entity.value = String(target.value ?? "");
  } else {
    const value = Number(target.value);
    if (!Number.isFinite(value)) return;
    updateEntityNumber(entity, key, value);
  }
  draft.future = [];
  draft.dirty = true;
}

function updateEntityNumber(entity, key, value) {
  if (entity.kind === "rectangle") {
    if (key === "centerX") entity.x = value - entity.width / 2;
    else if (key === "centerY") entity.y = value - entity.height / 2;
    else if (key === "width" || key === "height") entity[key] = Math.max(0.01, value);
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
  draft.history.push(JSON.stringify({ entities: draft.entities, selectedId: draft.selectedId }));
  if (draft.history.length > 50) draft.history.shift();
}

function undoDraft(draft) {
  const snapshot = draft.history?.pop();
  if (!snapshot) return;
  draft.future ??= [];
  draft.future.push(JSON.stringify({ entities: draft.entities, selectedId: draft.selectedId }));
  Object.assign(draft, JSON.parse(snapshot), { dirty: true });
}

function redoDraft(draft) {
  const snapshot = draft.future?.pop();
  if (!snapshot) return;
  draft.history ??= [];
  draft.history.push(JSON.stringify({ entities: draft.entities, selectedId: draft.selectedId }));
  Object.assign(draft, JSON.parse(snapshot), { dirty: true });
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

async function saveSectionProfile(context, view, ops) {
  const state = ensureSketchState(view);
  const proposed = String(globalThis.prompt?.("管型名称", state.sectionName) ?? state.sectionName).trim();
  if (!proposed) return null;
  state.sectionName = proposed;
  const profile = buildProfileFromSectionDraft(state.section, proposed);
  view.pending = true;
  view.progress = { title: "正在保存管型", detail: "正在校验截面并生成可用管型", stage: "截面校验", mode: "Sketch" };
  ops.renderProject(context, view);
  let saved = null;
  try {
    const response = await context.productProxy.invoke("TubeDesigner.SaveImportedProfile", { name: proposed, profile }, { timeoutMs: 30000 });
    saved = response?.profile;
    if (!saved?.id) throw new Error("保存草图管型后没有返回记录标识。");
    view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], profileId: "" };
    const profiles = view.tubeDesignerUserData.profiles ??= [];
    const index = profiles.findIndex((item) => String(item?.id ?? "") === String(saved.id));
    if (index >= 0) profiles[index] = saved;
    else profiles.push(saved);
    state.section.dirty = false;
    view.error = "";
    return saved;
  } finally {
    view.pending = false;
    view.progress = null;
    if (saved) ops.showNotice(context, view, `草图管型“${proposed}”已保存到管型库。`);
    else ops.renderProject(context, view);
  }
}

async function saveSideSketch(context, view, ops) {
  const state = ensureSketchState(view);
  const designer = view.scene?.tubeDesigner ?? {};
  const productId = String(designer.product?.entityId ?? "");
  const member = targetMember(designer, state.targetMemberId);
  const draft = currentDraft(view, state);
  if (!productId || !member) throw new Error("请先在产品页生成产品并选择管件。");
  if (!draft.entities.length) throw new Error("请先绘制侧面切割图。");
  const sketch = {
    schema: SKETCH_SCHEMA,
    schemaVersion: 1,
    kind: SIDE_MODE,
    targetMemberId: member.entityId,
    unit: "mm",
    faceHeight: memberFaceHeight(member),
    length: Number(member.length ?? 0),
    entities: draft.entities.map((entity) => ({ ...entity })),
    updatedAt: new Date().toISOString(),
  };
  view.pending = true;
  view.progress = { title: "正在应用侧面草图", detail: `正在保存到${member.name || "当前管件"}`, stage: "保存刀路", mode: "Sketch" };
  ops.renderProject(context, view);
  let saved = false;
  try {
    const response = await context.sceneProxy.invoke("TubeDesigner.SaveSketch", { productEntityId: productId, targetMemberId: member.entityId, sketch }, { timeoutMs: 30000 });
    if (response?.tubeDesigner) view.scene.tubeDesigner = response.tubeDesigner;
    draft.dirty = false;
    view.error = "";
    saved = true;
    return sketch;
  } finally {
    view.pending = false;
    view.progress = null;
    if (saved) ops.showNotice(context, view, `侧面切割图已应用到“${member.name || "当前管件"}”。`);
    else ops.renderProject(context, view);
  }
}

function entitiesFromProfile(profile) {
  const result = [];
  for (const contour of Array.isArray(profile?.contours) ? profile.contours : []) {
    const points = [];
    for (const segment of Array.isArray(contour?.segments) ? contour.segments : []) {
      if (Array.isArray(segment?.start) && !points.length) points.push(segment.start.slice(0, 2).map(Number));
      if (segment?.kind === "arc" && Array.isArray(segment.middle)) points.push(segment.middle.slice(0, 2).map(Number));
      if (Array.isArray(segment?.end)) points.push(segment.end.slice(0, 2).map(Number));
    }
    const clean = deduplicatePoints(points);
    if (clean.length > 2) result.push({ id: createEntityId(), kind: "polyline", points: clean, closed: true });
  }
  return result;
}

function closedSectionEntities(draft) {
  return (draft?.entities ?? []).filter((entity) => entity.kind === "rectangle" || entity.kind === "circle" || (Array.isArray(entity.points) && entity.closed));
}

function entityPolygon(entity) {
  if (entity.kind === "rectangle") {
    return [[entity.x, entity.y], [entity.x + entity.width, entity.y], [entity.x + entity.width, entity.y + entity.height], [entity.x, entity.y + entity.height]];
  }
  if (entity.kind === "circle") {
    return Array.from({ length: 48 }, (_, index) => {
      const angle = (index / 48) * Math.PI * 2;
      return [entity.cx + Math.cos(angle) * entity.radius, entity.cy + Math.sin(angle) * entity.radius];
    });
  }
  return deduplicatePoints(entity.points ?? []);
}

function entityBounds(entity) {
  let points = [];
  if (entity.kind === "line") points = [[entity.x1, entity.y1], [entity.x2, entity.y2]];
  else if (entity.kind === "rectangle") points = [[entity.x, entity.y], [entity.x + entity.width, entity.y + entity.height]];
  else if (entity.kind === "circle") points = [[entity.cx - entity.radius, entity.cy - entity.radius], [entity.cx + entity.radius, entity.cy + entity.radius]];
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
  const points = entity.kind === "line" ? [[entity.x1, entity.y1], [entity.x2, entity.y2]] : entityPolygon(entity);
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
    const normalized = [roundCoordinate(Number(point[0])), roundCoordinate(Number(point[1]))];
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
