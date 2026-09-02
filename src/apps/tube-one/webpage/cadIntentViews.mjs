import { renderPanel } from "../../laser-3d-cam/webpage/layout/commonViews.mjs";
import { escapeAttr, escapeText } from "../../laser-3d-cam/webpage/utils/format.mjs";
import { scheduleTubeWorkpieceThumbnailHydration } from "./workpieceThumbnail.mjs";
import { scheduleCADIntentParameterLivePreview } from "./cadIntentActions.mjs";

const NODE_COLORS = Object.freeze({
  ExtrudedRegion: "#3f8f76",
  TaperedRegion: "#7a6bb0",
  HalfSpace: "#4f83ad",
  WrappedVolume: "#d29235",
  OpeningProfileSweep: "#c26753",
  Alternative: "#9b6a9a",
  Boolean: "#6d7c83",
  Transform: "#6d7c83",
  CompositeVolume: "#9a705c",
  ResidualBRep: "#9a4f4f",
});
const WORKPIECE_PAGE_SIZE = 8;

export function renderTubeWorkpieceLeftPane(context, view) {
  scheduleTubeWorkpieceThumbnailHydration(context);
  scheduleTubeWorkpieceSearch(context, view);
  const scene = view.scene ?? {};
  const model = scene.model ?? {};
  const tube = scene.tubeGeometry ?? {};
  const nodes = Array.isArray(tube.solidNodes) ? tube.solidNodes : [];
  const sectionPrimitives = Array.isArray(tube.sectionPrimitives) ? tube.sectionPrimitives : [];
  const recognized = isCurrentIntentRecognized(view, tube);
  const workpieces = scene.workpieces ?? [];
  const editorMode = view.tubeWorkspaceMode === "editor" && model.isLoaded;
  const content = editorMode
    ? renderTubePartEditor(view, model, tube, nodes, sectionPrimitives, recognized)
    : renderTubeMainWorkpiecePane(view, model, workpieces);
  return `${content}${view.workpieceImportDialogOpen ? renderWorkpieceImportDialog(view) : ""}`;
}

export function renderTubeCADDialogPrefix(context, view, scene = {}) {
  if (view.tubeWorkspaceMode !== "editor") return "";
  const model = scene.model ?? {};
  const title = view.tubeEditorSceneName
    || `${model.name || model.sourcePath || "零件"}编辑视图`;
  const tools = [
    ["circular-penetration", "圆形贯切", "circle"],
    ["rectangular-penetration", "矩形贯切", "rect"],
    ["tapered-penetration", "锥形贯切", "taper"],
    ["half-space", "平面截断", "plane"],
    ["wrapped-normal", "UV 法向截断", "wrap"],
    ["bevel", "坡口", "bevel"],
  ];
  return `
    <header class="tube-cad-dialog-titlebar">
      <div class="tube-cad-dialog-titlecopy">
        <i aria-hidden="true">T1</i>
        <div><strong id="tube-cad-dialog-title">${escapeText(title)}</strong><small>独立 Scene · 参数化零件编辑</small></div>
      </div>
      <div class="tube-cad-dialog-window-actions">
        <span title="当前编辑现场">Scene ${escapeText(String(view.tubeEditorSceneId ?? "").slice(0, 8))}</span>
        <button type="button" data-cam-action="tube-close-part-editor" aria-label="关闭零件编辑视图" title="关闭">×</button>
      </div>
    </header>
    <nav class="tube-cad-tool-ribbon" aria-label="新增内置刀具">
      <div class="tube-cad-tool-ribbon-label"><strong>新增刀具</strong><small>构造减材体</small></div>
      ${tools.map(([kind, label, icon]) => `
        <button type="button" data-cam-action="tube-add-cad-tool" data-tube-tool-kind="${escapeAttr(kind)}"
          title="新增${escapeAttr(label)}" ${view.pending ? "disabled" : ""}>
          ${renderCADToolIcon(icon)}<span>${escapeText(label)}</span>
        </button>`).join("")}
      <span class="tube-cad-tool-ribbon-spacer"></span>
      <button type="button" data-cam-action="view-standard" data-cam-view="iso" title="等轴测视图">
        ${renderCADToolIcon("view")}<span>等轴测</span>
      </button>
      <button type="button" data-cam-action="tube-close-part-editor" title="结束编辑">
        ${renderCADToolIcon("finish")}<span>结束编辑</span>
      </button>
    </nav>`;
}

export function renderTubeCADDialogSuffix(context, view) {
  if (view.tubeWorkspaceMode !== "editor") {
    return renderTubeMainBottomDock(view.scene ?? {}, view);
  }
  const logs = Array.isArray(view.tubeEditorLogs) ? view.tubeEditorLogs : [];
  return `<section class="tube-cad-editor-log" aria-label="零件编辑日志">
    <header><strong>日志</strong><span>${escapeText(logs.length)} 条</span></header>
    <div class="tube-cad-editor-log-list">
      ${logs.length ? logs.slice(-80).map((entry) => `<div class="${escapeAttr(entry.level ?? "info")}">
        <time>${escapeText(entry.time ?? "")}</time><i></i><span>${escapeText(entry.message ?? "")}</span>
      </div>`).join("") : `<div class="empty"><span>编辑 Scene 尚无日志</span></div>`}
    </div>
  </section>`;
}

function renderCADToolIcon(kind) {
  const paths = ({
    circle: `<circle cx="10" cy="10" r="5"/><path d="M10 1v3M10 16v3M1 10h3M16 10h3"/>`,
    rect: `<rect x="4" y="5" width="12" height="10" rx="1"/><path d="M1 10h3M16 10h3"/>`,
    taper: `<path d="m5 16 3-12h4l3 12z"/><path d="M3 17h14"/>`,
    plane: `<path d="m3 14 6-9 8 2-6 9z"/><path class="accent" d="M2 10h16"/>`,
    wrap: `<path d="M4 4c3 2 9 2 12 0v12c-3-2-9-2-12 0z"/><path class="accent" d="M2 10h16"/>`,
    bevel: `<path d="M4 4v12h12"/><path class="accent" d="m4 12 8 4"/>`,
    view: `<path d="m3 7 7-4 7 4v7l-7 4-7-4zM3 7l7 4 7-4M10 11v7"/>`,
    finish: `<path d="M3 10h10M9 6l4 4-4 4"/><path d="M15 3h2v14h-2"/>`,
  })[kind] ?? "";
  return `<svg viewBox="0 0 20 20" aria-hidden="true">${paths}</svg>`;
}

function renderTubeMainWorkpiecePane(view, model, workpieces) {
  const totalQuantity = workpieces.reduce(
    (sum, item) => sum + Math.max(1, Number(item.quantity ?? 1)),
    0,
  );
  return `<div class="tubest-main-left" aria-label="零件侧边栏">
    <div class="tubest-part-toolbar">
      <div class="tubest-part-toolbar-row primary">
        ${renderWorkpieceToolbarGroup("添加", "add", [
          ["从文件添加", "import", "tube-add-workpiece", view.pending],
          ["从模板库添加", "template", "", true],
          ["从三维绘制添加", "draw-3d", "", true],
          ["从二维绘制添加", "draw-2d", "", true],
        ])}
        ${renderWorkpieceToolbarGroup("编辑", "edit", [
          ["三维 CAD 编辑", "edit-3d", "tube-open-part-editor", view.pending || !model.isLoaded],
          ["二维 CAD 编辑", "edit-2d", "", true],
        ])}
        ${renderWorkpieceToolbarGroup("删除", "delete", [
          ["删除所选", "delete-selected", "", true, "danger"],
          ["全部删除", "delete-all", "", true, "danger"],
          ["删除当前", "delete-current", "tube-delete-workpiece", view.pending || !model.entityId, "danger", model.entityId],
        ])}
      </div>
      <div class="tubest-part-toolbar-row selection">
        <section class="tubest-filter-group" aria-label="过滤">
          <label class="tubest-part-search tubest-match-condition"
            data-toolbar-tooltip="过滤语法：n:名称  t:工艺  -t:排除工艺；不符合条件的工件会被隐藏">
            <span class="tube-filter-icon" aria-hidden="true">${renderSidebarIcon("select-match")}</span>
            <input type="search" aria-label="过滤工件" placeholder="n:名称  t:工艺"
              value="${escapeAttr(view.tubeWorkpieceSearchQuery ?? "")}" data-tube-workpiece-search>
          </label>
          <strong>过滤</strong>
        </section>
        ${renderWorkpieceToolbarGroup("选择", "selection", [
          ["全选当前过滤结果", "select-all", "tube-select-filtered-workpieces", view.pending || !workpieces.length],
          ["反选当前过滤结果", "select-invert", "tube-invert-filtered-workpieces", view.pending || !workpieces.length],
        ])}
      </div>
    </div>
    ${renderTubeWorkpieceList(
      workpieces,
      model.entityId,
      view.tubeWorkpiecePage,
      view.scene?.tubeGeometry,
      view.tubeSelectedWorkpieceIds,
    )}
    <footer class="tubest-part-list-footer">
      ${renderWorkpiecePagination(workpieces.length, view.tubeWorkpiecePage)}
      <span class="summary">工件 ${escapeText(workpieces.length)} 种 · 数量 ${escapeText(totalQuantity)}</span>
    </footer>
  </div>`;
}

function renderWorkpieceToolbarGroup(label, kind, commands) {
  return `<section class="tubest-part-tool-group ${escapeAttr(kind)}" aria-label="${escapeAttr(label)}">
    <div class="tubest-part-tool-buttons">
      ${commands.map(([title, icon, action, disabled, className = "", entityId = ""]) => `
        <button type="button" class="${escapeAttr(className)}" title="${escapeAttr(title)}"
          ${action ? `data-cam-action="${escapeAttr(action)}"` : ""}
          ${entityId ? `data-cam-workpiece-id="${escapeAttr(entityId)}"` : ""}
          ${disabled ? "disabled" : ""}>${renderSidebarIcon(
            icon,
            kind === "add" ? "plus" : kind === "edit" ? "pencil" : "",
          )}<span>${escapeText(title)}</span></button>
      `).join("")}
    </div>
    <strong>${escapeText(label)}</strong>
  </section>`;
}

function renderTubePartEditor(view, model, tube, nodes, sectionPrimitives, recognized) {
  return `<div class="tubest-part-editor-left" aria-label="三维零件编辑">
    <header class="tubest-editor-header">
      <button type="button" data-cam-action="tube-close-part-editor" title="返回零件列表">‹</button>
      <div><strong>三维零件编辑</strong><small>${escapeText(model.name || model.sourcePath || "当前零件")}</small></div>
      <span class="tube-status-pill ${recognized ? "ready" : ""}">${recognized ? escapeText(recognitionText(tube.status)) : "正在识别"}</span>
    </header>
    <section class="tubest-resource-tree-section">
      <div class="tubest-section-title"><strong>CAD 原语</strong><span>主体 / 刀具</span></div>
      <div class="tubest-reference-tree">
        <span>⌖ 主视基准面</span><span>⌖ 右视基准面</span><span>⌖ 俯视基准面</span>
      </div>
      ${recognized
        ? renderHistoryTree(nodes, sectionPrimitives, tube.baseNodeId, tube.rootNodeId, view.selectedCADIntentNodeId)
        : `<div class="cam-empty-row">正在把导入 BRep 转换为可编辑中性模型……</div>`}
    </section>
    <section class="tubest-recognition-summary">
      <span>内腔 <b>${escapeText(tube.innerBoundaryCount ?? 0)}</b></span>
      <span>参数原语 <b>${escapeText([...sectionPrimitives, ...nodes].filter(isParametricNode).length)}</b></span>
      <span>候选 <b>${escapeText(tube.alternativeCount ?? 0)}</b></span>
      <span>误差 <b>${escapeText(formatPercent(tube.relativeVolumeError))}</b></span>
    </section>
  </div>`;
}

function renderWorkpieceImportDialog(view) {
  const draft = view.workpieceImportDraft;
  const quantity = Math.max(1, Number(draft?.quantity ?? 1));
  return `<div class="tube-import-dialog-backdrop">
    <section class="tube-import-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-import-dialog-title">
      <header class="tube-import-dialog-head">
        <div>
          <strong id="tube-import-dialog-title">添加管材工件</strong>
          <small>${draft ? "检查模型预览并设置数量" : "选择 STEP / IGES 管材模型"}</small>
        </div>
        <button class="tube-import-dialog-close" type="button" data-cam-action="tube-cancel-import"
          aria-label="关闭" ${view.pending ? "disabled" : ""}>×</button>
      </header>
      <div class="tube-import-preview">
        ${draft ? `<canvas width="720" height="420"
          data-tube-workpiece-thumbnail
          data-tube-preview-url="${escapeAttr(draft.previewResourceUrl ?? "")}"
          data-tube-preview-version="${escapeAttr(draft.previewResourceVersion ?? 0)}"></canvas>`
          : `<div class="tube-import-preview-empty"><i aria-hidden="true">STEP</i><span>选择模型后在这里显示三维示意图</span></div>`}
      </div>
      <div class="tube-import-dialog-info">
        <div class="tube-import-file-copy">
          <small>工件名称</small>
          <strong>${escapeText(draft?.name ?? "尚未选择文件")}</strong>
          <span>${draft ? "模型已解析，可以添加到工件列表" : "支持 .step / .stp / .igs / .iges"}</span>
        </div>
        <label class="tube-import-quantity-field">
          <span>数量</span>
          <input type="number" min="1" step="1" value="${escapeAttr(quantity)}"
            data-tube-import-quantity ${draft && !view.pending ? "" : "disabled"}>
        </label>
      </div>
      <footer class="tube-import-dialog-actions">
        <button class="tool-button" type="button" data-cam-action="tube-choose-import-file"
          ${view.pending ? "disabled" : ""}>${draft ? "重新选择" : "选择文件"}</button>
        <span></span>
        <button class="tool-button" type="button" data-cam-action="tube-cancel-import"
          ${view.pending ? "disabled" : ""}>取消</button>
        <button class="primary-button" type="button" data-cam-action="tube-confirm-import"
          ${draft && !view.pending ? "" : "disabled"}>添加工件</button>
      </footer>
    </section>
  </div>`;
}

function renderTubeWorkpieceList(
  workpieces,
  activeWorkpieceId,
  requestedPage = 1,
  tubeGeometry = null,
  selectedWorkpieceIds = [],
) {
  if (!workpieces.length) {
    return `<div class="tubest-empty-parts">
      <i aria-hidden="true">＋</i>
      <strong>零件列表为空</strong>
      <span>点击上方“添加”导入 STEP / IGES 管材零件</span>
    </div>`;
  }
  const pageCount = Math.max(1, Math.ceil(workpieces.length / WORKPIECE_PAGE_SIZE));
  const page = Math.min(pageCount, Math.max(1, Number(requestedPage) || 1));
  const firstIndex = (page - 1) * WORKPIECE_PAGE_SIZE;
  const pageItems = workpieces.slice(firstIndex, firstIndex + WORKPIECE_PAGE_SIZE);
  const groups = new Map();
  pageItems.forEach((item, pageIndex) => {
    const index = firstIndex + pageIndex;
    const cavities = getWorkpieceCavityCount(item);
    const key = String(cavities);
    const entries = groups.get(key) ?? [];
    entries.push({ item, index, cavities });
    groups.set(key, entries);
  });
  const sortedGroups = [...groups.values()].sort((left, right) => left[0].cavities - right[0].cavities);
  const selectedIds = new Set((selectedWorkpieceIds ?? []).map(String));
  return `<div class="tube-workpiece-list tubest-part-groups">
    ${sortedGroups.map((entries) => {
      const cavities = entries[0].cavities;
      const total = entries.reduce((sum, entry) => sum + Math.max(1, Number(entry.item.quantity ?? 1)), 0);
      return `<section class="tubest-section-group" data-tube-workpiece-group>
        <header>
          <div><strong>${escapeText(sectionGroupName(cavities))}</strong><small>${escapeText(sectionGroupHint(cavities))}</small></div>
          <span>零件种类: ${escapeText(entries.length)}　数量: ${escapeText(total)}</span>
        </header>
        <div class="tubest-section-cards">
          ${entries.map(({ item, index }) => {
            const active = String(item.entityId ?? "") === String(activeWorkpieceId ?? "");
            const selected = selectedIds.has(String(item.entityId ?? ""));
            const quantity = Math.max(1, Number(item.quantity ?? 1));
            const displayName = item.name || `工件 ${index + 1}`;
            const processSearchText = collectWorkpieceProcessSearchText(
              item,
              active ? tubeGeometry : null,
            );
            return `<button class="tube-workpiece-card ${active ? "active" : ""} ${selected ? "selected" : ""}" type="button"
              data-cam-action="tube-select-workpiece"
              data-cam-workpiece-id="${escapeAttr(item.entityId)}"
              data-tube-workpiece-name="${escapeAttr(displayName.toLowerCase())}"
              data-tube-workpiece-processes="${escapeAttr(processSearchText.toLowerCase())}">
              <i class="tube-workpiece-selection-check" aria-hidden="true">✓</i>
              <canvas class="tube-workpiece-thumbnail" width="180" height="112"
                data-tube-workpiece-thumbnail
                data-tube-preview-url="${escapeAttr(item.previewResourceUrl ?? "")}"
                data-tube-preview-version="${escapeAttr(item.previewResourceVersion ?? 0)}"
                data-tube-inner-cavities="${escapeAttr(cavities)}"></canvas>
              <span class="tube-workpiece-card-copy">
                <strong title="${escapeAttr(item.name || item.sourcePath || item.entityId)}">${escapeText(displayName)}</strong>
                <small>${escapeText(sectionGroupName(cavities))} · 模型 v${escapeText(item.geometryRevision ?? 0)}</small>
              </span>
              <span class="tube-workpiece-quantity"><strong>×${escapeText(quantity)}</strong><small>数量</small></span>
            </button>`;
          }).join("")}
        </div>
      </section>`;
    }).join("")}
  </div>`;
}

function renderWorkpiecePagination(itemCount, requestedPage = 1) {
  const pageCount = Math.max(1, Math.ceil(itemCount / WORKPIECE_PAGE_SIZE));
  const page = Math.min(pageCount, Math.max(1, Number(requestedPage) || 1));
  return `<nav class="tube-workpiece-pagination" aria-label="工件分页">
    <button type="button" data-cam-action="tube-workpiece-page" data-tube-page="${page - 1}"
      ${page <= 1 ? "disabled" : ""} aria-label="上一页">‹</button>
    <span><b>${escapeText(page)}</b> / ${escapeText(pageCount)}</span>
    <button type="button" data-cam-action="tube-workpiece-page" data-tube-page="${page + 1}"
      ${page >= pageCount ? "disabled" : ""} aria-label="下一页">›</button>
  </nav>`;
}

export function renderTubeWorkpieceRightPane(context, view) {
  scheduleCADIntentParameterLivePreview(context, view);
  const scene = view.scene ?? {};
  const model = scene.model ?? {};
  const tube = scene.tubeGeometry ?? {};
  const nodes = Array.isArray(tube.solidNodes) ? tube.solidNodes : [];
  const sectionPrimitives = Array.isArray(tube.sectionPrimitives) ? tube.sectionPrimitives : [];
  const recognized = isCurrentIntentRecognized(view, tube);
  const editorMode = view.tubeWorkspaceMode === "editor" && model.isLoaded;
  if (!editorMode) {
    return renderTubeMainProperties(scene, model);
  }
  const selected = recognized
    ? [...sectionPrimitives, ...nodes].find((node) => node.id === view.selectedCADIntentNodeId) ?? null
    : null;
  return `<div class="tubest-editor-properties tube-editor-right-pane" aria-label="特征属性">
    ${selected
      ? renderSelectedNode(selected, view)
      : renderPanel("参数", `<div class="cam-empty-row">在资源树中选择一个几何特征。</div>`)}
  </div>`;
}

export function renderTubeCSGViewportOverlay(context, view, scene = {}) {
  if (context.activeRibbonTabId !== "workpiece") {
    return "";
  }
  const tube = scene.tubeGeometry ?? {};
  const model = scene.model ?? {};
  const editorMode = view.tubeWorkspaceMode === "editor" && model.isLoaded;
  if (!editorMode) {
    return renderTubeMainViewportOverlay(view, scene);
  }
  if (!isCurrentIntentRecognized(view, tube)) {
    return `<div class="tubest-editor-scene-tag"><strong>三维零件编辑</strong><span>正在解析参数化特征……</span></div>`;
  }
  const nodes = Array.isArray(tube.solidNodes) ? tube.solidNodes : [];
  const sectionPrimitives = Array.isArray(tube.sectionPrimitives) ? tube.sectionPrimitives : [];
  const selected = [...sectionPrimitives, ...nodes]
    .find((node) => node.id === view.selectedCADIntentNodeId);
  if (!selected) {
    return `<div class="tubest-editor-scene-tag"><strong>三维零件编辑</strong><span>${escapeText(model.name || "当前零件")}</span></div>`;
  }
  return `<div class="tubest-editor-scene-tag"><strong>${escapeText(displayNodeName(selected))}</strong><span>${escapeText(model.name || "当前零件")}</span></div>`;
}

function renderTubeMainViewportOverlay(view, scene) {
  const model = scene.model ?? {};
  const tube = scene.tubeGeometry ?? {};
  const workpieces = Array.isArray(scene.workpieces) ? scene.workpieces : [];
  const active = workpieces.find((item) => String(item.entityId ?? "") === String(model.entityId ?? ""));
  const cavities = active
    ? getWorkpieceCavityCount(active)
    : Math.max(0, Number(tube.innerBoundaryCount ?? 0));
  const length = Number(tube.extrusionLength ?? 0);
  return `${renderTubeLayerStrip()}
    <div class="tubest-scene-caption">
      <strong>${escapeText(model.name || active?.name || "TubeOne 三维工作区")}</strong>
      <span>${model.isLoaded ? `${escapeText(sectionGroupName(cavities))}${length > 0 ? ` · L ${escapeText(formatNumber(length))} mm` : ""}` : "导入工件后开始"}</span>
    </div>`;
}

function collectTubeStockGroups(scene) {
  const workpieces = Array.isArray(scene.workpieces) ? scene.workpieces : [];
  const stockGroups = new Map();
  for (const item of workpieces) {
    const cavities = getWorkpieceCavityCount(item);
    const parameters = formatSectionParameters(item.sectionParameters);
    const key = `${item.sectionTypeId || sectionGroupName(cavities)}|${parameters}|${Number(item.length ?? 0)}`;
    const group = stockGroups.get(key) ?? { item, cavities, quantity: 0 };
    group.quantity += Math.max(1, Number(item.quantity ?? 1));
    group.workpieceCount = Number(group.workpieceCount ?? 0) + 1;
    stockGroups.set(key, group);
  }
  return [...stockGroups.values()];
}

function renderTubeMainProperties(scene, model) {
  const workpieces = Array.isArray(scene.workpieces) ? scene.workpieces : [];
  const active = workpieces.find((item) =>
    String(item.entityId ?? "") === String(model.entityId ?? "")) ?? model;
  if (!active?.entityId && !active?.isLoaded) {
    return `<aside class="tube-main-properties" aria-label="属性">
      <header><strong>属性</strong><small>选择工件后查看</small></header>
      <div class="tube-main-property-empty">当前没有可显示的工件属性。</div>
    </aside>`;
  }
  const cavities = getWorkpieceCavityCount(active);
  const sectionParameters = formatSectionParameters(active.sectionParameters);
  const length = Number(active.length ?? scene.tubeGeometry?.extrusionLength ?? 0);
  return `<aside class="tube-main-properties" aria-label="属性">
    <header><strong>属性</strong><small>当前工件</small></header>
    <section class="tube-property-identity">
      <i class="tube-stock-section" data-cavities="${escapeAttr(cavities)}"><b></b><b></b></i>
      <div><strong>${escapeText(active.name || "未命名工件")}</strong><small>${escapeText(active.sectionTypeId || sectionGroupName(cavities))}</small></div>
    </section>
    <section class="tube-property-group">
      <header>基本信息</header>
      <dl>
        <dt>管型</dt><dd>${escapeText(active.sectionTypeId || sectionGroupName(cavities))}</dd>
        <dt>截面</dt><dd>${escapeText(sectionParameters || sectionGroupHint(cavities))}</dd>
        <dt>长度</dt><dd>${length > 0 ? `${escapeText(formatNumber(length))} mm` : "-"}</dd>
        <dt>数量</dt><dd>${escapeText(Math.max(1, Number(active.quantity ?? 1)))}</dd>
      </dl>
    </section>
    <section class="tube-property-group">
      <header>模型状态</header>
      <dl>
        <dt>几何版本</dt><dd>v${escapeText(active.geometryRevision ?? 0)}</dd>
        <dt>编辑状态</dt><dd>${escapeText(active.editState || "Current")}</dd>
        <dt>内腔数量</dt><dd>${escapeText(cavities)}</dd>
        <dt>资源状态</dt><dd>${active.brepResourceId ? "已加载" : "待加载"}</dd>
      </dl>
    </section>
  </aside>`;
}

function renderTubeMainBottomDock(scene, view) {
  const model = scene.model ?? {};
  const tube = scene.tubeGeometry ?? {};
  const workpieces = Array.isArray(scene.workpieces) ? scene.workpieces : [];
  const stockGroups = collectTubeStockGroups(scene);
  const defaultLength = Number(tube.extrusionLength ?? model.length ?? 0);
  const activeTab = view.tubeBottomTab === "stock" ? "stock" : "nesting";
  return `<div class="tube-bottom-splitter" data-cam-resize-pane="bottom" data-no-window-drag
      title="拖拽调整底部区域高度" aria-label="调整底部区域高度"></div>
    <section class="tube-main-bottom-dock tubest-bottom-dock tubest-nesting-bar" aria-label="排样结果列表">
    <header>
      <nav class="tube-bottom-tabs" aria-label="排样数据">
        <button type="button" class="${activeTab === "nesting" ? "active" : ""}"
          data-cam-action="tube-bottom-tab" data-tube-bottom-tab="nesting">排样结果</button>
        <button type="button" class="${activeTab === "stock" ? "active" : ""}"
          data-cam-action="tube-bottom-tab" data-tube-bottom-tab="stock">管材列表</button>
      </nav>
      <span>${activeTab === "nesting"
        ? `${escapeText(workpieces.length)} 条结果`
        : `${escapeText(stockGroups.length)} 种管材`}</span>
    </header>
    ${activeTab === "nesting" ? `<div class="tube-nesting-table" data-tube-bottom-panel="nesting">
      <div class="head"><span>已排管材</span><span>规格</span><span>数量</span><span>总长</span><span>余料</span><span>零件数</span><span>利用率</span><span>锁定</span></div>
      ${workpieces.length ? workpieces.map((item) => {
        const quantity = Math.max(1, Number(item.quantity ?? 1));
        const active = String(item.entityId ?? "") === String(model.entityId ?? "");
        const length = Number(item.length ?? defaultLength);
        const utilization = Math.min(96, 48 + quantity * 9);
        return `<div class="row ${active ? "active" : ""}">
          <span><input type="checkbox" ${active ? "checked" : ""}> ${escapeText(item.name || "未命名工件")}</span>
          <span>${escapeText(item.sectionTypeId || sectionGroupName(getWorkpieceCavityCount(item)))}</span>
          <span>×${escapeText(quantity)}</span>
          <span>${length > 0 ? `${escapeText(formatNumber(length * quantity))} mm` : "-"}</span>
          <span>-</span><span>${escapeText(quantity)}</span>
          <span class="utilization"><i style="--progress:${utilization}%"></i><b>${utilization}%</b></span>
          <span><input type="checkbox"></span>
        </div>`;
      }).join("") : `<div class="empty">尚无排样结果</div>`}
    </div>` : renderTubeStockTable(stockGroups, model)}
  </section>`;
}

function renderTubeStockTable(groups, model) {
  return `<div class="tube-stock-table" data-tube-bottom-panel="stock">
    <div class="head"><span>管材类型</span><span>截面参数</span><span>长度</span><span>数量</span><span>关联零件</span><span>状态</span></div>
    ${groups.length ? groups.map(({ item, cavities, quantity, workpieceCount }) => {
      const active = String(item.entityId ?? "") === String(model.entityId ?? "");
      const length = Number(item.length ?? 0);
      return `<button type="button" class="row ${active ? "active" : ""}"
        data-cam-action="tube-select-workpiece" data-cam-workpiece-id="${escapeAttr(item.entityId ?? "")}">
        <span class="stock-kind"><i class="tube-stock-section" data-cavities="${escapeAttr(cavities)}"><b></b><b></b></i><strong>${escapeText(item.sectionTypeId || sectionGroupName(cavities))}</strong></span>
        <span>${escapeText(formatSectionParameters(item.sectionParameters) || sectionGroupHint(cavities))}</span>
        <span>${length > 0 ? `${escapeText(formatNumber(length))} mm` : "-"}</span>
        <span>×${escapeText(quantity)}</span><span>${escapeText(workpieceCount)} 种</span><span>可用</span>
      </button>`;
    }).join("") : `<div class="empty">尚无管材数据</div>`}
  </div>`;
}

function formatSectionParameters(parameters) {
  if (!parameters || typeof parameters !== "object") return "";
  return Object.entries(parameters)
    .filter(([, value]) => ["number", "string"].includes(typeof value))
    .slice(0, 3)
    .map(([name, value]) => `${name} ${typeof value === "number" ? formatNumber(value) : value}`)
    .join(" · ");
}

function getWorkpieceCavityCount(item) {
  const thumbnailCount = Number(item?.thumbnailInnerBoundaryCount ?? 0);
  const sectionCount = Number(
    item?.sectionParameters?.cavityCount
      ?? item?.sectionParameters?.innerBoundaryCount
      ?? 0,
  );
  return Math.max(
    0,
    Number.isFinite(thumbnailCount) ? thumbnailCount : 0,
    Number.isFinite(sectionCount) ? sectionCount : 0,
  );
}

function renderTubeLayerStrip() {
  const colors = ["#f2f2f2", "#ff3636", "#ff9e22", "#ffd93a", "#72de45", "#32d7b7", "#2baee8", "#4984f0", "#7758d8", "#ce4fdf", "#ff5aa6", "#a5afb7"];
  return `<nav class="tubest-layer-strip" aria-label="图层颜色">
    <button type="button" title="显示全部">I</button><button type="button" title="隐藏全部">×</button>
    ${colors.map((color, index) => `<button type="button" class="color ${index === 1 ? "active" : ""}" style="--layer-color:${color}" title="图层 ${index + 1}"><i></i></button>`).join("")}
  </nav>`;
}

function renderSidebarIcon(name, badge = "") {
  const body = ({
    add: `<path d="M2 9h14M9 2v14"/>`,
    remove: `<path d="M2 9h14"/>`,
    "select-list": `<path d="M2 3h3v3H2zM2 8h3v3H2zM2 13h3v3H2zM8 4.5h8M8 9.5h8M8 14.5h8"/>`,
    import: `<path d="M4 2h7l3 3v11H4zM11 2v4h4"/><path d="M6 9h5M6 12h4"/>`,
    draw: `<path d="m3 6 6-3 6 3-6 3zM3 6v7l6 3 6-3V6"/><path class="accent" d="m10 13 5-5 2 2-5 5-3 1z"/>`,
    edit: `<path d="M2 3h14v12H2zM5 6h6M5 9h4"/><path class="accent" d="m10 14 5-5 2 2-5 5-3 1z"/>`,
    template: `<path d="M2 3h6v5H2zM10 3h6v5h-6zM2 10h6v5H2zM10 10h6v5h-6z"/>`,
    "draw-3d": `<path d="m2 6 7-4 7 4-7 4zM2 6v7l7 3 7-3V6M9 10v6"/>`,
    "draw-2d": `<rect x="2" y="3" width="12" height="12"/><path d="M5 6h6M5 9h4"/>`,
    "edit-3d": `<path d="m2 6 7-4 7 4-7 4zM2 6v7l7 3 7-3V6M9 10v6"/>`,
    "edit-2d": `<rect x="2" y="3" width="12" height="12"/><path d="M5 6h6M5 9h4"/>`,
    "select-all": `<rect x="2" y="2" width="5" height="5"/><rect x="10" y="2" width="5" height="5"/><rect x="2" y="10" width="5" height="5"/><rect x="10" y="10" width="5" height="5"/><path class="accent" d="m3 4 1 1 2-2m5 1 1 1 2-2M3 12l1 1 2-2m5 1 1 1 2-2"/>`,
    "select-invert": `<rect x="2" y="2" width="5" height="5"/><rect x="10" y="2" width="5" height="5"/><rect x="2" y="10" width="5" height="5"/><rect x="10" y="10" width="5" height="5"/><path class="accent" d="M3 3h3v3H3zm8 8h3v3h-3z"/>`,
    "select-match": `<path d="M2 3h14l-5 6v5l-4 2V9z"/><path class="accent" d="m10 5 1.5 1.5L14 4"/>`,
    "delete-selected": `<path d="M4 5h10M6 5l1-2h4l1 2M6 7v8h6V7"/><path class="accent" d="m7 10 1 1 2-2"/>`,
    "delete-all": `<path d="M3 5h8M5 5l1-2h3l1 2M5 7v8h5V7M12 7h3M13 9v6h2V9"/>`,
    "delete-current": `<path d="M4 5h10M6 5l1-2h4l1 2M6 7v8h6V7"/><circle class="accent" cx="9" cy="11" r="2"/>`,
  })[name] ?? "";
  const badgeBody = badge === "plus"
    ? `<circle class="tool-badge-bg" cx="14" cy="14" r="3.25"/><path class="tool-badge-mark" d="M14 12v4M12 14h4"/>`
    : badge === "pencil"
      ? `<path class="tool-badge-bg" d="m9.8 15.8 1-3.8 3.8-3.8 2.8 2.8-3.8 3.8z"/><path class="tool-badge-mark" d="m12 10.8 2.8 2.8M10.8 12l2.8 2.8"/>`
      : "";
  return `<svg class="tubest-sidebar-icon" viewBox="0 0 18 18" aria-hidden="true">${body}${badgeBody}</svg>`;
}

function sectionGroupName(cavities) {
  if (cavities <= 0) return "实心型材";
  if (cavities === 1) return "单腔管材";
  return `${cavities} 腔管材`;
}

function sectionGroupHint(cavities) {
  if (cavities <= 0) return "截面无内孔";
  return `截面包含 ${cavities} 个内孔`;
}

function scheduleTubeWorkpieceSearch(context, view) {
  queueMicrotask(() => {
    const root = context.mount;
    const input = root?.querySelector?.("[data-tube-workpiece-search]");
    if (!input || input.dataset.tubeSearchReady === "1") return;
    input.dataset.tubeSearchReady = "1";
    const applyQuery = () => {
      const query = String(input.value ?? "").trim();
      view.tubeWorkpieceSearchQuery = query;
      const parsed = parseWorkpieceSearchQuery(query);
      input.classList.toggle("invalid", Boolean(parsed.error));
      input.setAttribute("aria-invalid", parsed.error ? "true" : "false");
      for (const card of root.querySelectorAll(".tube-workpiece-card[data-tube-workpiece-name]")) {
        card.hidden = !matchesWorkpieceSearch(card, parsed);
      }
      for (const group of root.querySelectorAll("[data-tube-workpiece-group]")) {
        group.hidden = !group.querySelector(".tube-workpiece-card:not([hidden])");
      }
    };
    input.addEventListener("input", applyQuery);
    applyQuery();
  });
}

function parseWorkpieceSearchQuery(query) {
  const tokens = tokenizeWorkpieceSearchQuery(query);
  const groups = [[]];
  let error = "";
  for (const rawToken of tokens) {
    const operator = rawToken.toLowerCase();
    if (operator === "and") continue;
    if (operator === "or") {
      if (groups.at(-1).length) groups.push([]);
      continue;
    }
    let token = rawToken;
    let excluded = false;
    if (token.startsWith("-") && token.length > 1) {
      excluded = true;
      token = token.slice(1);
    }
    const separator = token.indexOf(":");
    const field = separator > 0 ? token.slice(0, separator).toLowerCase() : "n";
    const value = (separator > 0 ? token.slice(separator + 1) : token).trim().toLowerCase();
    if (field !== "n" && field !== "t") {
      error ||= `不支持的过滤器：${field}:`;
      continue;
    }
    if (!value) {
      error ||= `${field}: 后需要输入内容`;
      continue;
    }
    groups.at(-1).push({ field, value, excluded });
  }
  return { groups: groups.filter((group) => group.length), error };
}

function tokenizeWorkpieceSearchQuery(query) {
  const tokens = [];
  let token = "";
  let quote = "";
  for (const character of String(query ?? "")) {
    if (quote) {
      if (character === quote) quote = "";
      else token += character;
      continue;
    }
    if (character === '"' || character === "'") {
      quote = character;
    } else if (/\s/.test(character)) {
      if (token) tokens.push(token);
      token = "";
    } else {
      token += character;
    }
  }
  if (token) tokens.push(token);
  return tokens;
}

function matchesWorkpieceSearch(card, parsed) {
  if (parsed.error) return false;
  if (!parsed.groups.length) return true;
  const fields = {
    n: String(card.dataset.tubeWorkpieceName ?? "").toLowerCase(),
    t: String(card.dataset.tubeWorkpieceProcesses ?? "").toLowerCase(),
  };
  return parsed.groups.some((group) => group.every((term) => {
    const matched = fields[term.field].includes(term.value);
    return term.excluded ? !matched : matched;
  }));
}

function collectWorkpieceProcessSearchText(item, tubeGeometry) {
  const values = [];
  const append = (value, depth = 0) => {
    if (value == null || depth > 2) return;
    if (["string", "number", "boolean"].includes(typeof value)) {
      values.push(String(value));
    } else if (Array.isArray(value)) {
      value.forEach((entry) => append(entry, depth + 1));
    } else if (typeof value === "object") {
      for (const key of ["name", "label", "type", "kind", "process", "technology"]) {
        if (key in value) append(value[key], depth + 1);
      }
    }
  };
  for (const key of [
    "processes", "processTags", "technologies", "technologyTags",
    "features", "operations", "appliedProcesses",
  ]) append(item?.[key]);
  for (const node of tubeGeometry?.solidNodes ?? []) {
    if (!node?.materialRole || node.materialRole === "None") continue;
    append(displayNodeName(node));
    append(typeText(node.type));
    append(roleText(node.materialRole));
    append(node.label);
  }
  return [...new Set(values.filter(Boolean))].join(" ");
}

function renderSelectedNode(node, view) {
  const parameters = Array.isArray(node.parameters) ? node.parameters : [];
  const editableParameters = parameters.filter((parameter) => parameter?.editable !== false);
  const candidates = Array.isArray(node.candidates) ? node.candidates : [];
  return `
    ${renderPanel("参数", `
      <div class="tube-node-heading">
        <div class="tube-node-copy">
          <strong>${escapeText(displayNodeName(node))}</strong>
          <small>${escapeText(node.id)}</small>
        </div>
        <span class="tube-node-role" style="--tube-node-color:${nodeColor(node.type)}">${escapeText(roleText(node.materialRole))}</span>
      </div>
      ${node.previewAvailable ? `
        <div class="tube-preview-state ${view.cadIntentPreviewNodeId === node.id ? "active" : ""} ${view.cadIntentLivePreviewNodeId === node.id ? "live" : ""}">
          <i></i>
          <span>${view.cadIntentLivePreviewNodeId === node.id
            ? "刀具正在实时变化；尚未应用到零件"
            : view.cadIntentPreviewNodeId === node.id
              ? "场景中正在半透明显示完整构造体"
            : "该特征可在场景中半透明显示"}</span>
        </div>
      ` : `<div class="tube-preview-state unavailable"><i></i><span>该节点没有独立实体，场景保留成品显示</span></div>`}
      ${parameters.length ? `
        <form class="tube-parameter-form" data-tube-parameter-form data-tube-node-id="${escapeAttr(node.id)}">
          <div class="tube-parameter-grid">
            ${parameters.map(renderParameter).join("")}
          </div>
          ${editableParameters.length ? `<div class="cam-button-row">
            <button class="primary-button" type="button" data-cam-action="tube-save-parameters"
              data-cam-node-id="${escapeAttr(node.id)}" ${view.pending ? "disabled" : ""}>应用到零件</button>
          </div>` : ""}
        </form>
      ` : `<div class="cam-hint">该节点由子节点或几何证据求值，没有直接标量参数。</div>`}
      <dl class="cam-facts">
        <dt>节点类型</dt><dd>${escapeText(typeText(node.type))}</dd>
        <dt>材料关系</dt><dd>${escapeText(roleText(node.materialRole))}</dd>
        <dt>截面边界</dt><dd>${escapeText(node.sectionBoundaryCount ?? "-")}</dd>
        <dt>UV 边界</dt><dd>${escapeText(node.uvBoundaryCount ?? "-")}</dd>
      </dl>
    `)}
    ${candidates.length ? renderPanel("等价解释", renderCandidates(node, candidates, view)) : ""}
  `;
}

function renderParameter(parameter) {
  const editable = parameter?.editable !== false;
  return `
    <label class="cam-field ${editable ? "editable" : "locked"}">
      <span>${escapeText(parameter.label ?? parameter.name)}${editable ? "" : `<em class="tube-derived-badge">派生</em>`}</span>
      <input class="cam-small-input" type="number"
        data-tube-parameter="${escapeAttr(parameter.name)}"
        data-tube-original="${escapeAttr(formatNumber(parameter.value))}"
        value="${escapeAttr(formatNumber(parameter.value))}"
        step="${escapeAttr(parameter.step ?? "any")}" ${editable ? "" : "disabled"} />
      <small class="tube-parameter-unit">${escapeText(parameter.unit ?? "")}</small>
    </label>
  `;
}

function renderCandidates(node, candidates, view) {
  return `<div class="tube-candidate-list">
    ${candidates.map((candidate) => {
      const selected = candidate.id === node.selectedCandidateId;
      return `<div class="tube-candidate ${selected ? "selected" : ""}">
        <div class="tube-candidate-copy">
          <strong>${escapeText(candidate.label || candidate.id)}</strong>
          <small>推荐 ${formatPercent(candidate.recommendationScore)} · 误差 ${formatPercent(candidate.geometryError)}</small>
        </div>
        <button class="tool-button" type="button" data-cam-action="tube-select-candidate"
          data-cam-node-id="${escapeAttr(node.id)}" data-cam-candidate-id="${escapeAttr(candidate.id)}"
          ${selected || view.pending ? "disabled" : ""}>${selected ? "已选择" : "选择"}</button>
      </div>`;
    }).join("")}
  </div>`;
}

function renderHistoryTree(nodes, sectionPrimitives, baseNodeId, rootNodeId, selectedNodeId) {
  if (!nodes.length) {
    return `<div class="cam-empty-row">没有可显示的历史特征。</div>`;
  }
  const byId = new Map(nodes.map((node) => [node.id, node]));
  const parents = new Map();
  for (const node of nodes) {
    for (const childId of Array.isArray(node.children) ? node.children : []) {
      const parentIds = parents.get(childId) ?? [];
      parentIds.push(node.id);
      parents.set(childId, parentIds);
    }
  }
  const hasMaterialAncestor = (nodeId, visited = new Set()) => {
    if (visited.has(nodeId)) return false;
    visited.add(nodeId);
    return (parents.get(nodeId) ?? []).some((parentId) => {
      const parent = byId.get(parentId);
      return Boolean(parent && parent.materialRole && parent.materialRole !== "None")
        || hasMaterialAncestor(parentId, visited);
    });
  };
  const featureRoots = nodes.filter((node) =>
    node.id !== baseNodeId
      && node.materialRole
      && node.materialRole !== "None"
      && !hasMaterialAncestor(node.id));
  const renderedNodeIds = new Set();
  const renderNode = (node, depth, stack = new Set(), stageLabel = "") => {
    if (!node || stack.has(node.id) || renderedNodeIds.has(node.id)) {
      return "";
    }
    renderedNodeIds.add(node.id);
    const nextStack = new Set(stack);
    nextStack.add(node.id);
    const children = Array.isArray(node.children) ? node.children : [];
    return `<div class="tube-csg-branch">
      ${renderNodeRow(node, depth, node.id === selectedNodeId, stageLabel)}
      ${children.map((childId) => renderNode(byId.get(childId), depth + 1, nextStack)).join("")}
    </div>`;
  };
  const base = byId.get(baseNodeId) ?? byId.get("base") ?? nodes[0];
  return `<div class="tube-history-tree">
    <div class="tube-history-stage">
      <div class="tube-history-stage-title"><span>${escapeText(sectionPrimitives.length)}</span><strong>主体</strong></div>
      ${renderNode(base, 0, new Set(), "毛坯")}
      ${sectionPrimitives.map((primitive) => renderNodeRow(
        primitive,
        1,
        primitive.id === selectedNodeId,
        primitive.type === "SectionOuter" ? "外" : "腔",
      )).join("")}
    </div>
    <div class="tube-history-stage">
      <div class="tube-history-stage-title"><span>${escapeText(featureRoots.length)}</span><strong>加工特征</strong></div>
      ${featureRoots.length
        ? featureRoots.map((node, index) => renderNode(node, 0, new Set(), `F${index + 1}`)).join("")
        : `<div class="cam-empty-row">毛坯上没有识别到减材特征。</div>`}
    </div>
    <div class="tube-history-result">
      <span>结果</span><strong>B − ${escapeText(featureRoots.length)} 个特征 = P</strong>
      <small>${escapeText(rootNodeId || "base")}</small>
    </div>
  </div>`;
}

function renderNodeRow(node, depth, selected, stageLabel = "") {
  return `
    <button class="tube-node-row ${selected ? "selected" : ""}" type="button"
      style="--tube-depth:${depth};--tube-node-color:${nodeColor(node.type)}"
      data-cam-action="tube-select-csg" data-cam-node-id="${escapeAttr(node.id)}">
      <div class="tube-node-copy">
        <strong>${stageLabel ? `<em>${escapeText(stageLabel)}</em>` : ""}${escapeText(displayNodeName(node))}</strong>
        <small>${escapeText(node.id)}</small>
      </div>
      <div class="tube-node-badges">
        <span class="tube-node-kind">${escapeText(typeText(node.type))}</span>
        ${node.materialRole && node.materialRole !== "None"
          ? `<span class="tube-node-role">${escapeText(roleText(node.materialRole))}</span>`
          : ""}
      </div>
    </button>
  `;
}

function displayNodeName(node) {
  if (node.type === "SectionOuter") return node.label || "主体外轮廓";
  if (node.type === "SectionCavity") return node.label || "截面腔体";
  if (node.id === "base") return "管材毛坯";
  if (node.id?.includes("explicit-bevel") && node.materialRole === "Penetration" && node.type === "Boolean") return "带坡口的支管贯切";
  if (node.id?.includes("bevel-extra-volume")) return "坡口附加减材体";
  if (node.type === "WrappedVolume" && node.materialRole === "Truncation") return "UV 法向截断";
  if (node.type === "ExtrudedRegion" && node.materialRole === "Penetration") return "支管贯切体";
  if (node.type === "TaperedRegion") return "锥形贯切体";
  if (node.type === "OpeningProfileSweep") return "坡口剖面";
  if (node.type === "HalfSpace") return "平面截断";
  if (node.type === "Boolean" && node.materialRole === "Penetration") return "贯切复合特征";
  if (node.type === "Boolean") return "布尔构造";
  return node.label || typeText(node.type);
}

function typeText(type) {
  return ({
    SectionOuter: "外轮廓原语",
    SectionCavity: "腔体原语",
    ExtrudedRegion: "拉伸体",
    TaperedRegion: "锥形体",
    HalfSpace: "半空间",
    WrappedVolume: "UV 法向体",
    OpeningProfileSweep: "开口剖面",
    Alternative: "候选解释",
    Boolean: "布尔",
    Transform: "变换",
    CompositeVolume: "复合体",
    ResidualBRep: "BRep 兜底",
  })[type] ?? type ?? "未知";
}

function roleText(role) {
  return ({
    Penetration: "贯切",
    Truncation: "截断",
    BoundaryProfileModifier: "边界修饰",
    UnclassifiedRemoval: "待分类减材",
    None: "构造节点",
  })[role] ?? role ?? "构造节点";
}

function recognitionText(status) {
  return ({
    Exact: "精确识别",
    EquivalentButAmbiguous: "存在等价解释",
    Partial: "部分参数化",
    Failed: "识别失败",
    Unavailable: "未识别",
  })[status] ?? "未识别";
}

function nodeColor(type) {
  if (type === "SectionOuter") return "#287b72";
  if (type === "SectionCavity") return "#b47a2c";
  return NODE_COLORS[type] ?? "#72838b";
}

function isParametricNode(node) {
  return Array.isArray(node.parameters) && node.parameters.length > 0;
}

function formatPercent(value) {
  const number = Number(value);
  return Number.isFinite(number) ? `${(number * 100).toFixed(2)}%` : "-";
}

function formatNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? String(Number(number.toFixed(6))) : "0";
}

function isCurrentIntentRecognized(view, tube) {
  const resourceId = String(tube?.resourceId ?? "");
  return Boolean(
    view.cadIntentRecognized
      && tube?.available
      && resourceId
      && resourceId === String(view.recognizedCADIntentResourceId ?? ""),
  );
}
