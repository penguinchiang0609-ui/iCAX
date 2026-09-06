import { createThreeViewport } from "../../../iCAX-UI/SDK/index.mjs";
import { renderProgress } from "./layout/commonViews.mjs";
import { activateProjectArea, getProjectArea, getProjectView, setProjectAreaViewContent } from "./state/projectViewStore.mjs";
import { getMachineId, getMachineSubtreeEntityIds, getMachines, getSelectedMachine, reconcileSelectedMachine } from "./state/sceneSelectors.mjs";
import { escapeAttr, escapeText, formatNumber } from "./utils/format.mjs";
import { attachViewCube, renderViewCube, stopViewCubeAnimation } from "./viewport/viewCube.mjs";
import { ensureStyles } from "./styles/ensureStyles.mjs";
import { exposeLaserCamAutomation } from "./automation/laserCamAutomation.mjs";
import { RENDER_ENTITY_VIEW_PROJECTION } from "./projection.mjs";

// Product-specific features are opt-in; the default workbench never queries CAM services.
export function createWorkbench(features = {}) {
  const {
  attachMachineAppearanceAutoApply = () => false,
  attachMachineInstanceNameAutoApply = () => false,
  attachMachineJointLimitAutoApply = () => false,
  attachMachineToolTCPAutoApply = () => false,
  attachMachineTransformAutoApply = () => false,
  handleMachineAction = () => false,
  handleMachineRibbonCommand = () => false,
  importMachinePathAction = () => false,
  selectMachineDefinitionAction = () => false,
  selectMachineInstanceAction = () => false,
  setMachineInstanceEnabledAction = () => false,
  renderMachineLeftPane = () => "",
  renderMachineRightPane = () => "",
  handleMachiningAction = () => false,
  handleMachiningRibbonCommand = () => false,
  setJobMachineAction = () => false,
  renderMachiningLeftPane = () => "",
  renderMachiningRightPane = () => "",
  findCommandTitle = (id) => id,
  getTabTitle = (tabId) => tabId,
  normalizeCamTab = (tabId) => tabId || "view",
  renderToolpathOverlay = () => "",
  renderViewLeftPane = () => "",
  renderViewRightPane = () => "",
  handleWorkpieceAction = () => false,
  handleWorkpieceRibbonCommand = () => false,
  importModelPathAction = () => false,
  renderWorkpieceLeftPane = () => "",
  renderWorkpieceRightPane = () => "",
  renderEdge = () => "",
  renderFace = () => "",
  renderLoop = () => ""
  } = features;

const DEFAULT_WORKBENCH_LAYOUT = Object.freeze({
  leftWidth: 320,
  rightWidth: 340,
  bottomHeight: 142,
});
const WORKBENCH_LAYOUT_LIMITS = Object.freeze({
  minLeftWidth: 190,
  maxLeftWidth: 560,
  minRightWidth: 220,
  maxRightWidth: 620,
  minViewerWidth: 420,
  splitterTotalWidth: 12,
  minBottomHeight: 96,
  maxBottomHeight: 420,
  minViewerHeight: 260,
  bottomSplitterHeight: 5,
});
const NOTICE_VISIBLE_DURATION_MS = 2400;
const AREA_VIEW_DEFINITIONS = features.areaViewDefinitions ?? {
  view: { sources: [{ sourceId: "scene-renderables", role: "scene", language: "sql",
    where: "WHERE HAS CRenderInstanceComponent", projection: RENDER_ENTITY_VIEW_PROJECTION }] },
};

function getProjectOps() {
  return {
    renderProject,
    invokeSDOMethod,
    invokeSDOMethodPayload,
    refreshSceneState,
    refreshActiveAreaView,
    getActiveAreaViewRevision,
    appendProjectLog,
    fitViewAfterRenderPublish,
    showNotice,
  };
}

function mountProduct(context) {
  const { mount, product } = context;
  if (!mount) {
    return;
  }
  ensureStyles();
  mount.innerHTML = `
    <div class="cam-product-home">
      <strong>${escapeText(product.productName || "工作台")}</strong>
      <span>新建或打开项目，开始工作</span>
    </div>
  `;
}

function mountProject(context) {
  const { mount, sceneProxy, project } = context;
  if (!mount || !sceneProxy || !project?.projectId) {
    return;
  }
  ensureStyles();

  const view = getProjectView(project.projectId);
  view.sceneProxy = sceneProxy;
  renderProject(context, view);

  if (!view.scene && !view.pending) {
    refreshScene(context, view);
  }
}

async function synchronizeActiveAreaView(context, expectation = {}) {
  const projectId = String(context?.project?.projectId ?? "").trim();
  if (!projectId) {
    throw new Error("Active-area synchronization requires a project");
  }
  return refreshActiveAreaView(context, getProjectView(projectId), expectation);
}

async function waitForActiveAreaAction(context) {
  const projectId = String(context?.project?.projectId ?? "").trim();
  if (!projectId) {
    throw new Error("Area-action synchronization requires a project");
  }
  return getProjectView(projectId).activeAreaAction?.promise ?? null;
}

async function handleRibbonCommand(context, commandId) {
  const view = context?.project?.projectId ? getProjectView(context.project.projectId) : null;
  if (!view) {
    return;
  }
  const ops = getProjectOps();

  if (typeof context.handleAreaRibbonCommand === "function"
      && await context.handleAreaRibbonCommand(context, view, commandId, ops)) {
    return;
  }
  if (await handleMachineRibbonCommand(context, view, commandId, ops)) {
    return;
  }
  if (await handleWorkpieceRibbonCommand(context, view, commandId, ops)) {
    return;
  }
  if (handleMachiningRibbonCommand(context, view, commandId, ops)) {
    return;
  }

  if (commandId === "view.fit") {
    fitViewport(context, view);
  } else if (commandId === "view.reset-layout") {
    resetWorkbenchLayout(view);
    showNotice(context, view, "布局已恢复默认尺寸。");
  } else {
    showNotice(context, view, `${findCommandTitle(commandId)} 功能入口已就位，等待后端能力接入。`);
  }
}

function normalizeAreaId(context, tabId) {
  if (typeof context?.normalizeAreaId === "function") {
    const value = String(context.normalizeAreaId(tabId) ?? "").trim();
    if (value) return value;
  }
  return normalizeCamTab(tabId);
}

function renderProject(context, view) {
  const mount = resolveProjectMount(context);
  const { project } = context;
  if (!mount) {
    return;
  }
  const tab = normalizeAreaId(context, context.activeRibbonTabId);
  activateProjectArea(view, tab);
  const scene = view.scene ?? {};
  reconcileSelectedMachine(view, scene);
  const topology = scene.topology ?? {};
  const layout = getWorkbenchLayout(view);
  const workbenchPresentation = typeof context.resolveWorkbenchPresentation === "function"
    ? (context.resolveWorkbenchPresentation(context, view, scene, tab) ?? {})
    : {};
  const workbenchClass = String(workbenchPresentation.className ?? "").trim();
  const workbenchAttributes = String(workbenchPresentation.attributes ?? "").trim();
  const workbenchStyle = [
    renderWorkbenchLayoutStyle(layout),
    String(workbenchPresentation.style ?? "").trim(),
  ].filter(Boolean).join(";");

  mount.innerHTML = `
    <div class="cam-workbench ${escapeText(workbenchClass)}" ${workbenchAttributes} style="${escapeAttr(workbenchStyle)}">
      ${typeof context.renderWorkbenchPrefix === "function"
        ? context.renderWorkbenchPrefix(context, view, scene)
        : ""}
      ${renderImportNotice(topology)}
      <aside class="cam-context-pane">
        ${renderLeftPane(tab, context, view)}
      </aside>
      <div class="cam-splitter cam-splitter-left"
           data-cam-resize-pane="left"
           data-no-window-drag
           title="拖拽调整左侧区域宽度"></div>
      <section class="cam-viewer">
        <div class="cam-viewer-head">
          <strong>${escapeText(project.projectName)}</strong>
          <div>
            <span>${escapeText(context.areaTitleOverrides?.[tab] ?? getTabTitle(tab))}</span>
            <span>View / Resources</span>
          </div>
        </div>
        <div class="cam-viewport">
          ${renderViewport(context, view, scene)}
          ${typeof context.renderViewportOverlay === "function"
            ? context.renderViewportOverlay(context, view, scene)
            : ""}
        </div>
      </section>
      <div class="cam-splitter cam-splitter-right"
           data-cam-resize-pane="right"
           data-no-window-drag
           title="拖拽调整右侧区域宽度"></div>
      <aside class="cam-info-pane">
        ${renderRightPane(tab, context, view)}
      </aside>
      ${typeof context.renderWorkbenchSuffix === "function"
        ? context.renderWorkbenchSuffix(context, view, scene)
        : ""}
      ${view.pending && view.progress ? renderProgress(view.progress) : ""}
      ${view.notice ? `<div class="cam-status notice">${escapeText(view.notice)}</div>` : ""}
      ${view.error ? `<div class="cam-status error">${escapeText(view.error)}</div>` : ""}
    </div>
  `;

  synchronizeNoticeDismiss(mount, view);

  attachWorkbenchResizeHandlers(mount, view);

  const pathInput = mount.querySelector("[data-cam-model-path]");
  pathInput?.addEventListener("input", () => {
    view.sourcePath = pathInput.value;
  });

  const machinePathInput = mount.querySelector("[data-cam-machine-path]");
  machinePathInput?.addEventListener("input", () => {
    view.machineSourcePath = machinePathInput.value;
  });
  if (tab === "machine") {
    attachMachineTransformAutoApply(context, view, getProjectOps());
    attachMachineJointLimitAutoApply(context, view, getProjectOps());
    attachMachineAppearanceAutoApply(context, view, getProjectOps());
    attachMachineToolTCPAutoApply(context, view, getProjectOps());
    attachMachineInstanceNameAutoApply(context, view, getProjectOps());
  }

  mount.onclick = (event) => {
    const actionTarget = event.target instanceof Element ? event.target.closest("[data-cam-action]") : null;
    if (actionTarget && !actionTarget.hasAttribute("disabled")) {
      const action = String(actionTarget.dataset.camAction ?? "");
      const operation = runAction(context, view, action, actionTarget);
      view.activeAreaAction = { action, promise: operation };
      void operation
        .catch((error) => {
          view.error = error?.message ?? String(error);
          appendProjectLog(context, "error", `${action} 失败：${view.error}`);
          renderProject(context, view);
        })
        .finally(() => {
          if (view.activeAreaAction?.promise === operation) {
            view.activeAreaAction = null;
          }
        });
      return;
    }

    const machineDefinitionTarget = event.target instanceof Element ? event.target.closest("[data-cam-machine-definition-id]") : null;
    if (machineDefinitionTarget) {
      if (view.pending) {
        return;
      }
      const machineDefinitionId = machineDefinitionTarget.dataset.camMachineDefinitionId ?? "";
      selectMachineDefinitionAction(context, view, machineDefinitionId, getProjectOps());
      return;
    }

    const machineInstanceTarget = event.target instanceof Element ? event.target.closest("[data-cam-machine-instance-id]") : null;
    if (machineInstanceTarget) {
      if (view.pending) {
        return;
      }
      selectMachineInstanceAction(context, view, machineInstanceTarget.dataset.camMachineInstanceId ?? "", getProjectOps());
      return;
    }

    const workpieceTarget = event.target instanceof Element ? event.target.closest("[data-cam-workpiece-id]") : null;
    if (workpieceTarget) {
      invokeSDOMethod(context, view, "Workpiece.SetActive", { workpieceEntityId: workpieceTarget.dataset.camWorkpieceId });
      return;
    }

    const machinePickTarget = event.target instanceof Element ? event.target.closest("[data-cam-machine-pick]") : null;
    if (machinePickTarget) {
      const id = Number(machinePickTarget.dataset.camMachineId ?? 0);
      const entityId = String(machinePickTarget.dataset.camMachineEntityId ?? "").trim();
      const payload = {
        kind: machinePickTarget.dataset.camMachineKind ?? "machine",
        entityId,
        objectId: entityId,
        machineId: machinePickTarget.dataset.camMachineRootId ?? "",
        label: machinePickTarget.dataset.camMachineLabel ?? "",
      };
      if (Number.isFinite(id) && id > 0) {
        payload.id = id;
      }
      pickMachineObject(context, view, payload);
      return;
    }

    const pickTarget = event.target instanceof Element ? event.target.closest("[data-cam-pick]") : null;
    if (pickTarget) {
      const kind = pickTarget.dataset.camKind;
      const id = Number(pickTarget.dataset.camId);
      const label = pickTarget.dataset.camLabel ?? "";
      invokeSDOMethod(context, view, "Selection.PickTopology", { kind, id, label });
    }
  };

  mount.onchange = (event) => {
    const actionTarget = event.target instanceof Element
      ? event.target.closest("[data-cam-change-action]")
      : null;
    if (!actionTarget || actionTarget.hasAttribute("disabled")) return;
    const action = String(actionTarget.dataset.camChangeAction ?? "");
    const operation = runAction(context, view, action, actionTarget);
    view.activeAreaAction = { action, promise: operation };
    void operation
      .catch((error) => {
        view.error = error?.message ?? String(error);
        appendProjectLog(context, "error", `${action} 失败：${view.error}`);
        renderProject(context, view);
      })
      .finally(() => {
        if (view.activeAreaAction?.promise === operation) view.activeAreaAction = null;
      });
  };

  const jobMachineSelect = mount.querySelector("[data-cam-job-machine-select]");
  jobMachineSelect?.addEventListener("change", () => {
    if (view.pending) {
      return;
    }
    setJobMachineAction(context, view, jobMachineSelect.value, getProjectOps());
  });

  mountRenderViewport(context, view);
  if (typeof context.afterProjectRender === "function") {
    context.afterProjectRender(context, view, mount, getProjectOps());
  }
  void ensureAreaViewContent(context, view, tab);
  scrollSelectedMachineTreeNodeIntoView(mount, view);
}

function renderLeftPane(tab, context, view) {
  const override = context.areaRenderers?.[tab]?.left;
  if (typeof override === "function") {
    return override(context, view, getProjectOps());
  }
  if (tab === "machine") {
    return renderMachineLeftPane(context, view);
  }

  if (tab === "workpiece") {
    return renderWorkpieceLeftPane(context, view);
  }

  if (tab === "machining") {
    return renderMachiningLeftPane(context, view);
  }

  return renderViewLeftPane(context, view);
}

function renderRightPane(tab, context, view) {
  const override = context.areaRenderers?.[tab]?.right;
  if (typeof override === "function") {
    return override(context, view, getProjectOps());
  }
  if (tab === "machine") {
    return renderMachineRightPane(context, view);
  }

  if (tab === "workpiece") {
    return renderWorkpieceRightPane(context, view);
  }

  if (tab === "machining") {
    return renderMachiningRightPane(context, view);
  }

  return renderViewRightPane(context, view);
}

function getWorkbenchLayout(view) {
  view.layout ??= {};
  view.layout.leftWidth = Number.isFinite(Number(view.layout.leftWidth))
    ? Number(view.layout.leftWidth)
    : DEFAULT_WORKBENCH_LAYOUT.leftWidth;
  view.layout.rightWidth = Number.isFinite(Number(view.layout.rightWidth))
    ? Number(view.layout.rightWidth)
    : DEFAULT_WORKBENCH_LAYOUT.rightWidth;
  view.layout.bottomHeight = Number.isFinite(Number(view.layout.bottomHeight))
    ? Number(view.layout.bottomHeight)
    : DEFAULT_WORKBENCH_LAYOUT.bottomHeight;
  return view.layout;
}

function resetWorkbenchLayout(view) {
  view.layout = { ...DEFAULT_WORKBENCH_LAYOUT };
}

function renderWorkbenchLayoutStyle(layout) {
  return [
    `--cam-left-width:${escapeText(Math.round(layout.leftWidth))}px`,
    `--cam-right-width:${escapeText(Math.round(layout.rightWidth))}px`,
    `--cam-bottom-height:${escapeText(Math.round(layout.bottomHeight))}px`,
  ].join(";");
}

function attachWorkbenchResizeHandlers(mount, view) {
  const handles = mount.querySelectorAll("[data-cam-resize-pane]");
  for (const handle of handles) {
    handle.addEventListener("pointerdown", (event) => beginWorkbenchPaneResize(event, mount, view));
  }
}

function beginWorkbenchPaneResize(event, mount, view) {
  if (event.button !== 0) {
    return;
  }

  const handle = event.currentTarget;
  if (!(handle instanceof Element)) {
    return;
  }

  const side = handle.dataset.camResizePane;
  if (side !== "left" && side !== "right" && side !== "bottom") {
    return;
  }

  const workbench = mount.querySelector(".cam-workbench");
  if (!(workbench instanceof HTMLElement)) {
    return;
  }

  event.preventDefault();
  try {
    handle.setPointerCapture?.(event.pointerId);
  } catch {
    // Synthetic pointer events used by smoke tests may not create a capturable pointer.
  }
  const startLayout = { ...getWorkbenchLayout(view) };
  const resizeState = {
    side,
    startX: event.clientX,
    startY: event.clientY,
    startLayout,
    workbench,
  };

  document.body.classList.add(
    resizeState.side === "bottom" ? "cam-pane-resizing-vertical" : "cam-pane-resizing",
  );

  const onPointerMove = (moveEvent) => {
    const deltaX = moveEvent.clientX - resizeState.startX;
    const nextLayout = { ...resizeState.startLayout };
    if (resizeState.side === "left") {
      nextLayout.leftWidth = resizeState.startLayout.leftWidth + deltaX;
    } else if (resizeState.side === "right") {
      nextLayout.rightWidth = resizeState.startLayout.rightWidth - deltaX;
    } else {
      const deltaY = moveEvent.clientY - resizeState.startY;
      nextLayout.bottomHeight = resizeState.startLayout.bottomHeight - deltaY;
    }
    applyWorkbenchLayout(view, resizeState.workbench, nextLayout, resizeState.side);
  };

  const endResize = () => {
    window.removeEventListener("pointermove", onPointerMove);
    window.removeEventListener("pointerup", endResize);
    window.removeEventListener("pointercancel", endResize);
    document.body.classList.remove("cam-pane-resizing");
    document.body.classList.remove("cam-pane-resizing-vertical");
    try {
      handle.releasePointerCapture?.(event.pointerId);
    } catch {
      // Pointer capture may be absent when the resize was driven by automation.
    }
    view.viewport?.resize?.();
  };

  window.addEventListener("pointermove", onPointerMove);
  window.addEventListener("pointerup", endResize, { once: true });
  window.addEventListener("pointercancel", endResize, { once: true });
}

function applyWorkbenchLayout(view, workbench, nextLayout, activeSide) {
  const bounds = workbench.getBoundingClientRect();
  const normalized = normalizeWorkbenchLayout(nextLayout, bounds.width, activeSide, bounds.height);
  view.layout = normalized;
  workbench.style.setProperty("--cam-left-width", `${Math.round(normalized.leftWidth)}px`);
  workbench.style.setProperty("--cam-right-width", `${Math.round(normalized.rightWidth)}px`);
  workbench.style.setProperty("--cam-bottom-height", `${Math.round(normalized.bottomHeight)}px`);
}

function normalizeWorkbenchLayout(layout, width, activeSide = "", height = 0) {
  const limits = WORKBENCH_LAYOUT_LIMITS;
  const availableWidth = Number.isFinite(width) && width > 0 ? width : 1200;
  const maxSidebarTotal = Math.max(
    limits.minLeftWidth + limits.minRightWidth,
    availableWidth - limits.minViewerWidth - limits.splitterTotalWidth,
  );

  let leftWidth = clampNumber(
    Number(layout.leftWidth),
    limits.minLeftWidth,
    Math.min(limits.maxLeftWidth, maxSidebarTotal - limits.minRightWidth),
  );
  let rightWidth = clampNumber(
    Number(layout.rightWidth),
    limits.minRightWidth,
    Math.min(limits.maxRightWidth, maxSidebarTotal - limits.minLeftWidth),
  );

  const overflow = leftWidth + rightWidth - maxSidebarTotal;
  if (overflow > 0) {
    if (activeSide === "left") {
      leftWidth = Math.max(limits.minLeftWidth, leftWidth - overflow);
    } else if (activeSide === "right") {
      rightWidth = Math.max(limits.minRightWidth, rightWidth - overflow);
    } else {
      const half = overflow / 2;
      leftWidth = Math.max(limits.minLeftWidth, leftWidth - half);
      rightWidth = Math.max(limits.minRightWidth, rightWidth - (overflow - half));
    }
  }

  const availableHeight = Number.isFinite(height) && height > 0 ? height : 760;
  const maxBottomHeight = Math.max(
    limits.minBottomHeight,
    Math.min(
      limits.maxBottomHeight,
      availableHeight - limits.minViewerHeight - limits.bottomSplitterHeight,
    ),
  );
  const bottomHeight = clampNumber(
    Number(layout.bottomHeight),
    limits.minBottomHeight,
    maxBottomHeight,
  );

  return {
    leftWidth: Math.round(leftWidth),
    rightWidth: Math.round(rightWidth),
    bottomHeight: Math.round(bottomHeight),
  };
}

function clampNumber(value, min, max) {
  const finite = Number.isFinite(value) ? value : min;
  return Math.max(min, Math.min(max, finite));
}

function renderImportNotice(topology) {
  if (topology?.importMode !== "fallback-preview") {
    return "";
  }

  return `
    <div class="cam-import-notice">
      ${escapeText(topology.diagnostic || "当前显示的是 fallback 预览拓扑，不是真实 CAD 内核解析结果。")}
    </div>
  `;
}

function renderViewport(context, view, scene) {
  const model = scene.model ?? {};
  const sceneProxy = resolveSceneProxy(context, view);
  if (sceneProxy?.pdo?.enabled) {
    return `
      <div class="cam-render-viewport-shell">
        <div class="cam-render-viewport" data-cam-render-viewport></div>
        ${renderViewCube()}
      </div>
    `;
  }

  if (!model.isLoaded) {
    return `
      <div class="cam-empty-model">
        <strong>${escapeText(context.product?.productName || "三维线条切割 CAM")}</strong>
        <span>请先准备机床定义和工件资源，然后进入编程大区</span>
      </div>
    `;
  }

  const topology = scene.topology ?? {};
  if (!topology.hasTopology) {
    return `
      <div class="cam-empty-model">
        <strong>${escapeText(model.sourcePath)}</strong>
        <span>模型已登记，等待模型导入插件生成拓扑和显示数据</span>
      </div>
    `;
  }

  const selection = scene.selection ?? {};
  const selectedKey = `${selection.kind}:${selection.id}`;

  return `
    <svg class="cam-svg" viewBox="0 0 820 450" role="img" aria-label="CAM topology viewport">
      <defs>
        <linearGradient id="cam-face" x1="0" x2="1" y1="0" y2="1">
          <stop offset="0" stop-color="#8fb8c9" />
          <stop offset="1" stop-color="#5f8398" />
        </linearGradient>
        <linearGradient id="cam-side" x1="0" x2="1" y1="0" y2="1">
          <stop offset="0" stop-color="#507084" />
          <stop offset="1" stop-color="#395364" />
        </linearGradient>
      </defs>
      <rect x="0" y="0" width="820" height="450" fill="#182128" />
      <g transform="translate(28 28)">
        ${(scene.faces ?? []).map((face, index) => renderFace(face, selectedKey, index)).join("")}
        ${(scene.loops ?? []).map((loop) => renderLoop(loop, selectedKey)).join("")}
        ${(scene.edges ?? []).map((edge) => renderEdge(edge, selectedKey)).join("")}
        ${(scene.toolpaths ?? []).map((item, index) => renderToolpathOverlay(scene, item, index)).join("")}
      </g>
    </svg>
  `;
}

function mountRenderViewport(context, view) {
  const mount = resolveProjectMount(context);
  const host = mount?.querySelector?.("[data-cam-render-viewport]");
  if (!host) {
    stopViewCubeAnimation(view);
    return;
  }

  if (!view.viewport) {
    view.viewport = createThreeViewport({
      backgroundColor: 0x182128,
      onPick: (userData, hit) => handleViewportPick(context, view, userData, hit),
      onDiagnostic: (entry) => appendProjectLog(context, entry.level ?? "info", entry.message ?? ""),
    });
  }
  const sceneProxy = resolveSceneProxy(context, view);
  if (view.viewportSceneProxy !== sceneProxy) {
    for (const areaState of Object.values(view.areas ?? {})) {
      void areaState.viewReader?.stop?.();
      areaState.viewReader = null;
      areaState.viewContent = null;
      areaState.viewContentRequest = null;
    }
    view.viewport.connectScene(sceneProxy);
    view.viewportSceneProxy = sceneProxy;
  }
  const activeAreaId = normalizeAreaId(context, context.activeRibbonTabId);
  const area = getProjectArea(view, activeAreaId);
  if (typeof context.configureViewport === "function") {
    context.configureViewport(context, view, activeAreaId);
  }
  const backgroundColor = typeof context.resolveViewportBackgroundColor === "function"
    ? context.resolveViewportBackgroundColor(context, view, activeAreaId)
    : 0x182128;
  view.viewport.setBackgroundColor?.(backgroundColor);
  view.viewport.setRenderSceneId(null);
  if (!area.viewContent?.snapshot) {
    view.viewport.setVisibleEntityIds([]);
  }
  view.viewport.mount(host);
  attachViewCube(view, mount);
  syncViewportSelection(view, view.scene);
  exposeLaserCamAutomation(context, view, {
    importMachineDefinition: (commandContext, commandView, sourcePath) =>
      importMachinePathAction(commandContext, commandView, sourcePath, getProjectOps()),
    importWorkpiece: (commandContext, commandView, sourcePath) =>
      importModelPathAction(commandContext, commandView, sourcePath, getProjectOps()),
    recognizeCADIntent: (commandContext, commandView) =>
      typeof commandContext.handleAreaRibbonCommand === "function"
        ? commandContext.handleAreaRibbonCommand(
            commandContext,
            commandView,
            "intent.recognize-cad",
            getProjectOps(),
          )
        : false,
    setMachineInstanceEnabled: (commandContext, commandView, machineEntityId, enabled) =>
      setMachineInstanceEnabledAction(commandContext, commandView, machineEntityId, enabled, getProjectOps()),
    setMachineInstanceName: (commandContext, commandView, machineEntityId, name) =>
      invokeSDOMethod(commandContext, commandView, "Machine.SetName", { machineEntityId, name }, { timeoutMs: 10000 }),
    setJobMachine: (commandContext, commandView, machineEntityId) =>
      setJobMachineAction(commandContext, commandView, machineEntityId, getProjectOps()),
    pickMachineObject: (commandContext, commandView, payload) =>
      pickMachineObject(commandContext, commandView, payload),
    setMachineElementAppearance: (commandContext, commandView, payload) =>
      invokeSDOMethodPayload(commandContext, commandView, "Machine.SetElementAppearance", payload, { timeoutMs: 10000 }),
    setStandardCameraView: (commandContext, commandView, viewName) =>
      setViewportStandardView(commandContext, commandView, viewName),
    fitView: (commandContext, commandView) => fitViewport(commandContext, commandView),
    executeAreaAction: (commandContext, commandView, action, actionTarget = null) =>
      runAction(commandContext, commandView, action, actionTarget),
  });
  view.viewport.refreshAll();
}

function resolveProjectMount(context) {
  const liveMount = document.querySelector("[data-product-surface='project']");
  if (liveMount) {
    context.mount = liveMount;
    return liveMount;
  }
  return context.mount?.isConnected ? context.mount : null;
}

function resolveSceneProxy(context, view) {
  const resolved = typeof context.resolveSceneProxy === "function"
    ? context.resolveSceneProxy(context, view)
    : null;
  return resolved ?? context.sceneProxy ?? null;
}

async function runAction(context, view, action, actionTarget = null) {
  const ops = getProjectOps();
  if (action === "view-standard") {
    const viewName = String(actionTarget?.dataset?.camView ?? "iso").trim() || "iso";
    setViewportStandardView(context, view, viewName);
    return true;
  }
  if (typeof context.handleAreaAction === "function") {
    const handled = await context.handleAreaAction(context, view, action, actionTarget, ops);
    if (handled) {
      return handled;
    }
  }
  if (handleMachineAction(context, view, action, actionTarget, ops)) {
    return true;
  }
  if (handleWorkpieceAction(context, view, action, ops)) {
    return true;
  }
  if (handleMachiningAction(context, view, action, ops)) {
    return true;
  }
  showNotice(context, view, "该功能入口已就位，等待后端能力接入。");
  return false;
}

function handleViewportPick(context, view, userData, hit) {
  if (!userData || !hit) {
    selectSceneObjectLocally(view, "");
    return;
  }

  const objectId = String(userData.objectId ?? "").trim();
  if (features.camSelection && Number(userData.renderClass) === 4 && objectId && objectId !== "0") {
    pickMachineObject(context, view, {
      objectId,
      entityId: objectId,
      kind: "machine.part",
      label: `machine object ${objectId}`,
    });
    return;
  }

  const faceIndex = Number(hit.faceIndex);
  if (features.camSelection && Number.isInteger(faceIndex) && faceIndex >= 0) {
    const face = findFaceByTriangleIndex(view.scene, faceIndex);
    if (face) {
      selectSceneObjectLocally(view, "");
      invokeSDOMethod(context, view, "Selection.PickTopology", {
        kind: "face",
        id: Number(face.id),
        label: face.label ?? `face ${face.id}`,
      });
      return;
    }
  }

  selectSceneObjectLocally(view, objectId);
}

function findFaceByTriangleIndex(scene, faceIndex) {
  for (const face of scene?.faces ?? []) {
    const start = Number(face.triangleStart);
    const count = Number(face.triangleCount);
    if (!Number.isInteger(start) || !Number.isInteger(count) || count <= 0) {
      continue;
    }
    if (faceIndex >= start && faceIndex < start + count) {
      return face;
    }
  }
  return null;
}

function refreshScene(context, view) {
  refreshSceneState(context, view);
}

async function refreshSceneState(context, view) {
  const sceneProxy = resolveSceneProxy(context, view);
  if (!sceneProxy) {
    return false;
  }

  view.pending = true;
  view.error = "";
  renderProject(context, view);
  try {
    const scene = {};
    for (const sdoMethod of features.sceneMethods ?? []) {
      mergeScenePayload(scene, await sceneProxy.invoke(sdoMethod, {}, { timeoutMs: 30000 }));
    }
    scene.readiness = buildReadiness(scene);
    view.scene = scene;
    syncViewportSelection(view, scene);
    await refreshSelectedMachineElement(context, view, scene);
    await ensureAreaViewContent(context, view, view.activeAreaId || normalizeAreaId(context, context.activeRibbonTabId), {
      force: true,
      render: false,
    });
    syncViewportSelection(view, scene);
    return true;
  } catch (error) {
    view.error = error?.message ?? String(error);
    appendProjectLog(context, "error", `刷新项目状态失败：${view.error}`);
    return false;
  } finally {
    view.pending = false;
    renderProject(context, view);
  }
}

async function ensureAreaViewContent(context, view, areaId, options = {}) {
  const { force = false, render = true } = options;
  const defaultDefinition = AREA_VIEW_DEFINITIONS[areaId];
  const definition = typeof context.resolveAreaViewDefinition === "function"
    ? (context.resolveAreaViewDefinition(context, view, areaId, defaultDefinition) ?? defaultDefinition)
    : defaultDefinition;
  const sceneProxy = resolveSceneProxy(context, view);
  if (!sceneProxy || !definition) {
    return null;
  }

  const area = getProjectArea(view, areaId);
  const definitionKey = JSON.stringify(definition);
  if (area.viewContentRequest) {
    if (!force && area.viewDefinitionKey === definitionKey) {
      return area.viewContentRequest;
    }
    await area.viewContentRequest;
    return ensureAreaViewContent(context, view, areaId, options);
  }
  if (area.viewReader && area.viewDefinitionKey !== definitionKey) {
    await area.viewReader.stop();
    area.viewReader = null;
    area.viewContent = null;
    area.appliedViewRevision = "0";
    area.viewApplyByRevision?.clear?.();
  }
  if (area.viewReader) {
    const snapshot = force
      ? await area.viewReader.poll()
      : (area.viewReader.snapshot ?? await area.viewReader.poll());
    if (snapshot) {
      return enqueueAreaViewSnapshot(context, view, areaId, snapshot, render);
    }
    return area.viewContent;
  }

  area.viewDefinitionKey = definitionKey;
  const request = sceneProxy.views
    .start(definition, {
      pollIntervalMs: 100,
      onChange: (snapshot) => {
        void enqueueAreaViewSnapshot(
          context,
          view,
          areaId,
          snapshot,
          view.activeAreaId === areaId,
        ).catch((error) => {
          appendProjectLog(context, "error", `应用 View revision 失败：${error?.message ?? error}`);
        });
      },
    })
    .then(async (reader) => {
      area.viewReader = reader;
      const snapshot = await reader.poll().catch(() => null);
      if (snapshot) {
        return enqueueAreaViewSnapshot(context, view, areaId, snapshot, render);
      }
      return area.viewContent;
    })
    .catch((error) => {
      const message = error?.message ?? String(error);
      view.error = `读取 View 资源失败：${message}`;
      appendProjectLog(context, "error", view.error);
      return null;
    })
    .finally(() => {
      if (area.viewContentRequest === request) {
        area.viewContentRequest = null;
      }
    });
  area.viewContentRequest = request;
  return request;
}

function enqueueAreaViewSnapshot(context, view, areaId, snapshot, render) {
  const area = getProjectArea(view, areaId);
  const revision = String(snapshot?.revision ?? "0");
  const snapshotKey = `${String(snapshot?.viewId ?? "")}:${revision}`;
  if (revision === "0") {
    return Promise.reject(new Error("View snapshot has no repository revision"));
  }
  const existing = area.viewApplyByRevision?.get(snapshotKey);
  if (existing) {
    return existing;
  }
  const viewportRevision = String(view.viewport?.getAppliedViewState?.().revision ?? "0");
  if (area.appliedViewRevision === revision
      && area.viewContent?.snapshot?.viewId === snapshot?.viewId
      && (view.activeAreaId !== areaId || viewportRevision === revision)) {
    return Promise.resolve(area.viewContent);
  }

  area.viewApplyByRevision ??= new Map();
  const previous = Promise.resolve(area.viewApplyQueue).catch(() => null);
  const operation = previous.then(() =>
    applyAreaViewSnapshot(context, view, areaId, snapshot, render));
  area.viewApplyQueue = operation;
  area.viewApplyByRevision.set(snapshotKey, operation);
  operation.then(
    () => area.viewApplyByRevision.delete(snapshotKey),
    () => area.viewApplyByRevision.delete(snapshotKey),
  );
  return operation;
}

async function applyAreaViewSnapshot(context, view, areaId, snapshot, render) {
  const area = getProjectArea(view, areaId);
  const revision = String(snapshot?.revision ?? "0");
  if (view.activeAreaId !== areaId) {
    const content = setProjectAreaViewContent(view, areaId, snapshot);
    return content;
  }
  view.viewport?.setRenderSceneId(null);
  let viewportReceipt = null;
  try {
    viewportReceipt = await view.viewport?.applyViewSnapshot(
      snapshot,
      resolveSceneProxy(context, view)?.resources,
    );
  } catch (error) {
    appendProjectLog(context, "error", `渲染 View 资源失败：${error?.message ?? error}`);
    throw error;
  }
  if (!viewportReceipt?.applied || viewportReceipt.revision !== revision) {
    throw new Error(
      `View revision ${revision} was superseded before its geometry was applied`,
    );
  }
  const content = setProjectAreaViewContent(view, areaId, snapshot);
  content.viewportReceipt = viewportReceipt;
  area.appliedViewRevision = revision;
  syncViewportSelection(view, view.scene);
  if (render) {
    renderProject(context, view);
  }
  return content;
}

async function refreshActiveAreaView(context, view, expectation = {}) {
  const areaId = view.activeAreaId || normalizeAreaId(context, context.activeRibbonTabId);
  await ensureAreaViewContent(context, view, areaId, { render: false });
  const area = getProjectArea(view, areaId);
  if (!area.viewReader?.waitForSnapshot) {
    throw new Error(`View reader for area ${areaId} does not support revision events`);
  }
  const hasExpectation = (expectation.expectedEntityIds?.length ?? 0) > 0
    || (expectation.excludedEntityIds?.length ?? 0) > 0
    || Object.prototype.hasOwnProperty.call(expectation, "afterRevision")
    || String(expectation.expectedRevision ?? "0") !== "0";
  const snapshot = hasExpectation
    ? await area.viewReader.waitForSnapshot((candidate) =>
        snapshotMatchesExpectation(candidate, expectation))
    : await area.viewReader.poll();
  if (!snapshot) {
    throw new Error(`View reader for area ${areaId} returned no snapshot`);
  }
  const content = await enqueueAreaViewSnapshot(context, view, areaId, snapshot, false);
  if (hasExpectation && !snapshotMatchesExpectation(content?.snapshot, expectation)) {
    throw new Error(
      `Applied View revision ${content?.revision ?? "0"} does not match operation ${expectation.correlationId ?? ""}`,
    );
  }
  return content;
}

function snapshotMatchesExpectation(snapshot, expectation = {}) {
  if (!snapshot) {
    return false;
  }
  const revision = String(snapshot.revision ?? "0");
  const expectedRevision = String(expectation.expectedRevision ?? "0");
  if (expectedRevision !== "0" && revision !== expectedRevision) {
    return false;
  }
  const hasAfterRevision = Object.prototype.hasOwnProperty.call(expectation, "afterRevision");
  const afterRevision = String(expectation.afterRevision ?? "0");
  if (hasAfterRevision && compareViewRevisions(revision, afterRevision) <= 0) {
    return false;
  }
  const entityIds = new Set((Array.isArray(snapshot.entityIds)
    ? snapshot.entityIds
    : (snapshot.rows ?? []).map((row) => row?.entityId))
    .map((entityId) => String(entityId ?? "").trim())
    .filter(Boolean));
  for (const entityId of expectation.expectedEntityIds ?? []) {
    if (!entityIds.has(String(entityId))) {
      return false;
    }
  }
  for (const entityId of expectation.excludedEntityIds ?? []) {
    if (entityIds.has(String(entityId))) {
      return false;
    }
  }
  return true;
}

function compareViewRevisions(left, right) {
  try {
    const leftValue = BigInt(String(left ?? "0"));
    const rightValue = BigInt(String(right ?? "0"));
    return leftValue < rightValue ? -1 : leftValue > rightValue ? 1 : 0;
  } catch {
    return String(left ?? "0").localeCompare(String(right ?? "0"), undefined, { numeric: true });
  }
}

function getActiveAreaViewRevision(view) {
  const areaId = view.activeAreaId || "view";
  return String(getProjectArea(view, areaId).viewContent?.revision ?? "0");
}

async function refreshSelectedMachineElement(context, view, scene = {}) {
  const selection = scene.selection ?? {};
  const kind = String(selection.kind ?? "");
  const entityId = String(selection.entityId ?? "").trim();
  if (!entityId || !kind.startsWith("machine")) {
    scene.machineElement = null;
    return;
  }

  try {
    const sceneProxy = resolveSceneProxy(context, view);
    mergeScenePayload(scene, await sceneProxy.invoke("Machine.GetElement", { entityId }, { timeoutMs: 10000 }));
    syncViewportSelection(view, scene);
  } catch (error) {
    scene.machineElement = null;
    appendProjectLog(context, "error", `读取机床元素属性失败：${error?.message ?? String(error)}`);
  }
}

async function invokeSDOMethod(context, view, sdoMethod, payload, options = {}) {
  const { refreshScene: shouldRefreshScene = shouldRefreshSceneAfterSDOCall(sdoMethod), ...invokeOptions } = options;
  const result = await invokeSDOMethodPayload(context, view, sdoMethod, payload, { ...invokeOptions, expectScene: false });
  if (result.ok) {
    mergeScenePayload(view.scene ??= {}, result.payload);
    syncViewportSelection(view, view.scene);
    if (shouldRefreshScene) {
      await refreshSceneState(context, view);
    }
  }
  return result.ok;
}

async function invokeSDOMethodPayload(context, view, sdoMethod, payload, options = {}) {
  const sceneProxy = resolveSceneProxy(context, view);
  if (!sceneProxy) {
    return { ok: false, payload: null };
  }

  const { expectScene = false, ...invokeOptions } = options;
  appendProjectLog(context, "info", makeSDOCallStartLog(sdoMethod, payload));
  const showProgress = isLongSDOCall(sdoMethod);
  const progress = showProgress ? createSDOCallProgress(sdoMethod) : null;
  const useShellProjectProgress = showProgress
    && typeof context.actions?.withProjectProgress === "function"
    && context.project?.projectId;
  view.pending = true;
  view.progress = useShellProjectProgress ? null : progress;
  view.error = "";
  view.notice = "";
  renderProject(context, view);
  const progressStartedAt = performance.now();
  try {
    const invoke = () => sceneProxy.invoke(sdoMethod, payload, invokeOptions);
    await waitForPaint();
    let responsePayload = null;
    if (useShellProjectProgress) {
      responsePayload = await context.actions.withProjectProgress(context.project.projectId, progress, invoke);
    } else {
      responsePayload = await invoke();
    }
    if (expectScene) {
      view.scene = responsePayload;
      syncViewportSelection(view, responsePayload);
    } else {
      mergeScenePayload(view.scene ??= {}, responsePayload);
      syncViewportSelection(view, view.scene);
    }
    appendProjectLog(
      context,
      "ok",
      makeSDOCallSuccessLog(sdoMethod, expectScene ? view.scene : responsePayload),
    );
    return { ok: true, payload: responsePayload };
  } catch (error) {
    view.error = error?.message ?? String(error);
    appendProjectLog(context, "error", `${sdoMethod} 失败：${view.error}`);
    return { ok: false, payload: null };
  } finally {
    if (!useShellProjectProgress) {
      await waitForMinimumDuration(progressStartedAt, progress?.minimumVisibleMs ?? 0);
    }
    view.pending = false;
    view.progress = null;
    renderProject(context, view);
  }
}

function shouldRefreshSceneAfterSDOCall(sdoMethod) {
  return ![
    "Machine.GetElement",
    "Selection.Get",
    "Selection.PickMachineObject",
    "Selection.PickTopology",
  ].includes(sdoMethod);
}

function mergeScenePayload(scene, payload) {
  if (!scene || !payload || typeof payload !== "object") {
    return scene;
  }
  for (const key of [
    "job",
    "jobs",
    "machineDefinitions",
    "definitions",
    "supportedFormats",
    "machines",
    "machine",
    "machineElement",
    "workpiece",
    "workpieces",
    "model",
    "topology",
    "faces",
    "loops",
    "edges",
    "cadInspection",
    "tubeGeometry",
    "selection",
    "cuttingLayers",
    "visibleLayers",
    "program",
    "toolpaths",
    "intentDocumentId",
    "intentToolpaths",
    "fitView",
  ]) {
    if (Object.prototype.hasOwnProperty.call(payload, key)) {
      if (key === "definitions") {
        scene.machineDefinitions = payload.definitions;
      } else if (key === "supportedFormats") {
        scene.machineDefinitionSupportedFormats = payload.supportedFormats;
      } else if (key !== "machine") {
        scene[key] = payload[key];
      }
    }
  }
  if (payload.machine && Array.isArray(scene.machines)) {
    const machineId = String(payload.machine.entityId || payload.machine.id || "");
    const index = scene.machines.findIndex((item) => String(item?.entityId || item?.id || "") === machineId);
    if (index >= 0) {
      scene.machines[index] = payload.machine;
    } else if (machineId) {
      scene.machines.push(payload.machine);
    }
  }
  scene.readiness = buildReadiness(scene);
  return scene;
}

function buildReadiness(scene = {}) {
  const machine = getSelectedMachine(scene, {});
  const model = scene.model ?? scene.workpiece ?? {};
  const topology = scene.topology ?? {};
  const toolpaths = Array.isArray(scene.toolpaths) ? scene.toolpaths : [];
  const machineReady = Boolean(machine?.enabled !== false && (machine?.entityId || machine?.id));
  const workpieceReady = Boolean(model?.brepResourceId || model?.modelResourceId);
  const topologyReady = Boolean(topology?.hasTopology);
  const toolpathReady = toolpaths.length > 0;
  return {
    machineReady,
    workpieceReady,
    topologyReady,
    toolpathReady,
    jobReady: machineReady && workpieceReady && toolpathReady,
    nextStep: !machineReady
      ? "machine-definition"
      : !workpieceReady
        ? "workpiece-model"
        : !topologyReady
          ? "topology"
          : !toolpathReady
            ? "toolpath"
            : "job",
  };
}

function selectSceneObjectLocally(view, objectId) {
  const selectedId = String(objectId ?? "").trim();
  const previousId = String(view.selectedSceneObjectId ?? "").trim();
  view.selectedSceneObjectId = selectedId;
  if (previousId !== selectedId && view.scene?.machineElement) {
    view.scene.machineElement = null;
  }
  syncViewportHighlightedObjects(view, view.scene);
}

function syncViewportSelection(view, scene = {}) {
  if (scene?.selection && isSelectionVisibleInArea(view, scene.selection)) {
    view.selectedSceneObjectId = String(scene.selection.entityId || scene.selection.objectId || "").trim();
    syncSelectedMachineInstanceFromSelection(view, scene, scene.selection);
  }
  if (scene?.machineElement && String(scene.machineElement.entityId || "") !== String(view.selectedSceneObjectId || "")) {
    scene.machineElement = null;
  }
  if (scene?.machineElement) {
    syncSelectedMachineInstanceFromSelection(view, scene, scene.machineElement);
  }
  syncViewportHighlightedObjects(view, scene);
}

function isSelectionVisibleInArea(view, selection = {}) {
  const entityId = String(selection.entityId || selection.objectId || "").trim();
  if (!entityId) {
    return true;
  }
  const content = getProjectArea(view, view.activeAreaId).viewContent;
  return Boolean(content?.entityIds.has(entityId));
}

async function pickMachineObject(context, view, payload = {}) {
  const entityId = String(payload.entityId || payload.objectId || "").trim();
  const machineId = String(payload.machineId || "").trim();
  if (machineId) {
    view.selectedMachineInstanceId = machineId;
  }
  selectSceneObjectLocally(view, entityId);
  if (!entityId) {
    renderProject(context, view);
    return { ok: false, payload: null };
  }

  const pickResult = await invokeSDOMethodPayload(
    context,
    view,
    "Selection.PickMachineObject",
    { ...payload, entityId, objectId: entityId },
    { expectScene: false },
  );
  if (!pickResult.ok) {
    return pickResult;
  }

  const elementResult = await invokeSDOMethodPayload(
    context,
    view,
    "Machine.GetElement",
    { entityId },
    { expectScene: false, timeoutMs: 10000 },
  );
  if (!elementResult.ok) {
    return elementResult;
  }

  return {
    ok: true,
    payload: {
      ...(pickResult.payload ?? {}),
      ...(elementResult.payload ?? {}),
    },
  };
}

function syncSelectedMachineInstanceFromSelection(view, scene = {}, selection = {}) {
  const machineId = String(selection.machineId || "").trim();
  if (!machineId) {
    return;
  }
  const machines = getMachines(scene);
  if (machines.some((item) => getMachineId(item) === machineId)) {
    view.selectedMachineInstanceId = machineId;
  }
}

function getSelectedSceneObjectIds(view, scene = {}) {
  const selectedId = String(view.selectedSceneObjectId ?? "").trim();
  if (!selectedId) {
    return [];
  }
  const machine = getSelectedMachine(scene, view);
  const subtreeIds = getMachineSubtreeEntityIds(machine, selectedId);
  return subtreeIds.length ? subtreeIds : [selectedId];
}

function syncViewportHighlightedObjects(view, scene = {}) {
  const selectedIds = getSelectedSceneObjectIds(view, scene);
  if (typeof view.viewport?.setSelectedObjectIds === "function") {
    view.viewport.setSelectedObjectIds(selectedIds, view.selectedSceneObjectId || "");
    return;
  }
  view.viewport?.setSelectedObjectId?.(view.selectedSceneObjectId || "");
}

function scrollSelectedMachineTreeNodeIntoView(mount, view) {
  if (!view.selectedSceneObjectId) {
    return;
  }
  const selectedRow = mount.querySelector("[data-cam-selected-machine-node='true']");
  if (!selectedRow) {
    return;
  }
  requestAnimationFrame(() => {
    selectedRow.scrollIntoView({ block: "nearest", inline: "nearest" });
  });
}

function appendProjectLog(context, level, message) {
  const text = String(message ?? "").trim();
  if (!text) {
    return;
  }
  if (typeof context.onProjectLog === "function") {
    context.onProjectLog(context, level, text);
  }
  if (typeof context.actions?.log === "function") {
    context.actions.log(level, text);
    return;
  }
  console[level === "error" ? "error" : "log"](text);
}

function makeSDOCallStartLog(sdoMethod, payload) {
  const sourcePath = payload?.sourcePath ? `：${payload.sourcePath}` : "";
  if (sdoMethod === "MachineDefinition.Import") {
    return `开始导入机床定义${sourcePath}`;
  }
  if (sdoMethod === "Machine.Instantiate") {
    return `开始实例化机床：${payload?.machineDefinitionId ?? "-"}`;
  }
  if (sdoMethod === "Machine.SetEnabled") {
    return `${payload?.enabled === false ? "禁用" : "启用"}机床实例：${payload?.machineEntityId ?? "-"}`;
  }
  if (sdoMethod === "WorkpieceModel.Import") {
    return `开始导入工件模型资源${sourcePath}`;
  }
  if (sdoMethod === "Workpiece.Instantiate") {
    return `开始实例化工件：${payload?.modelResourceId ?? "-"}`;
  }
  return `调用 ${sdoMethod}`;
}

function makeSDOCallSuccessLog(sdoMethod, scene) {
  if (sdoMethod === "MachineDefinition.Import") {
    return `机床定义导入完成：definition=${scene?.machineDefinitionId ?? "-"}, path=${scene?.managedPath ?? scene?.sourcePath ?? "-"}`;
  }
  if (sdoMethod === "Machine.Instantiate") {
    const machines = getMachines(scene);
    const machine = machines[machines.length - 1] ?? {};
    const visualCount = machine.visuals?.length ?? 0;
    const collisionCount = machine.collisions?.length ?? 0;
    const includeCount = machine.includes?.length ?? 0;
    const suffix = visualCount || collisionCount || includeCount
      ? ""
      : "。该定义没有可显示几何，视口保持为空是正常结果";
    return `机床实例化完成：links=${machine.links?.length ?? 0}, joints=${machine.joints?.length ?? 0}, visuals=${visualCount}, collisions=${collisionCount}, includes=${includeCount}${suffix}`;
  }
  if (sdoMethod === "Machine.SetEnabled") {
    const machine = scene?.machine ?? getSelectedMachine(scene, {});
    return `机床实例状态已更新：${machine.enabled === false ? "已禁用" : "已启用"}`;
  }
  if (sdoMethod === "WorkpieceModel.Import") {
    return `工件模型资源导入完成：model=${scene?.modelResourceId ?? "-"}, brep=${scene?.brepResourceId ?? "-"}, topology=${scene?.topologyResourceId ?? "-"}`;
  }
  if (sdoMethod === "Workpiece.Instantiate") {
    const topology = scene?.topology ?? {};
    return `工件实例化完成：faces=${topology.faceCount ?? 0}, loops=${topology.loopCount ?? 0}, edges=${topology.edgeCount ?? 0}`;
  }
  return `${sdoMethod} 完成`;
}

async function fitViewAfterRenderPublish(context, view, expectation = {}) {
  const content = await refreshActiveAreaView(context, view, expectation);
  const receipt = view.viewport?.fitViewForRevision?.(content?.revision);
  const fitted = Boolean(receipt?.fitted);
  appendProjectLog(
    context,
    fitted ? "ok" : "error",
    fitted
      ? `View revision ${content.revision} 已适配到最佳视角`
      : `View revision ${content?.revision ?? "0"} 未能完成视角适配`,
  );
  return receipt;
}

function fitViewport(context, view, options = {}) {
  const fitted = Boolean(view.viewport?.fitView?.());
  if (!options.quiet) {
    appendProjectLog(
      context,
      fitted ? "ok" : "info",
      fitted ? "当前 View 已适配到最佳视角" : "当前 View 暂无可适配的几何",
    );
  }
  return fitted;
}

function setViewportStandardView(context, view, viewName) {
  const applied = Boolean(view.viewport?.setStandardView?.(viewName));
  appendProjectLog(
    context,
    applied ? "ok" : "error",
    applied ? `已切换到 ${viewName} 视图` : `不支持的标准视图：${viewName}`,
  );
  return Promise.resolve(applied);
}

function waitForPaint() {
  return new Promise((resolve) => {
    window.requestAnimationFrame(() => window.requestAnimationFrame(resolve));
  });
}

function waitForMinimumDuration(startedAt, minimumVisibleMs) {
  const minimum = Number(minimumVisibleMs);
  if (!Number.isFinite(minimum) || minimum <= 0) {
    return Promise.resolve();
  }
  const remaining = minimum - (performance.now() - startedAt);
  if (remaining <= 0) {
    return Promise.resolve();
  }
  return new Promise((resolve) => window.setTimeout(resolve, remaining));
}

function isLongSDOCall(sdoMethod) {
  return sdoMethod === "MachineDefinition.Import"
    || sdoMethod === "Machine.Instantiate"
    || sdoMethod === "WorkpieceModel.Import"
    || sdoMethod === "Workpiece.Instantiate"
    || sdoMethod === "Toolpath.RecognizeLoops"
    || sdoMethod === "Toolpath.AddSelectionPath";
}

function createSDOCallProgress(sdoMethod) {
  if (sdoMethod === "MachineDefinition.Import") {
    return {
      title: "导入机床定义",
      detail: "正在托管机床定义源文件",
      stage: "机床定义导入",
      mode: "Machine",
      minimumVisibleMs: 1600,
      steps: [
        ["格式检查", "正在校验当前产品支持的机床定义格式"],
        ["文件托管", "正在把源文件目录托管到产品数据区"],
        ["定义登记", "正在写入产品级机床定义目录"],
        ["结果返回", "正在返回机床定义 ID"],
      ],
    };
  }
  if (sdoMethod === "Machine.Instantiate") {
    return {
      title: "实例化机床",
      detail: "正在把机床定义展开到当前加工场景",
      stage: "机床实例化",
      mode: "Machine",
      minimumVisibleMs: 1200,
      steps: [
        ["源文件读取", "正在读取机床定义源文件"],
        ["结构展开", "正在创建 link、joint、visual 和 collision Entity"],
        ["参数初始化", "正在初始化 TCP、轴限位和机床状态"],
        ["显示同步", "正在生成机床 visual 资源并更新 View"],
        ["面板刷新", "正在刷新机床结构面板"],
      ],
    };
  }
  if (sdoMethod === "WorkpieceModel.Import") {
    return {
      title: "导入工件模型",
      detail: "正在读取模型文件",
      stage: "文件读取",
      mode: "CAD Import",
      minimumVisibleMs: 1600,
      steps: [
        ["文件读取", "正在读取 STEP/IGS 文件"],
        ["CAD 内核解析", "正在解析 B-Rep 拓扑"],
        ["拓扑归档", "正在写入 BRep 与拓扑资源"],
        ["显示数据", "正在生成轻量显示网格"],
        ["结果返回", "正在返回资源句柄"],
      ],
    };
  }
  if (sdoMethod === "Workpiece.Instantiate") {
    return {
      title: "实例化工件",
      detail: "正在把工件资源加入当前加工场景",
      stage: "工件实例化",
      mode: "Workpiece",
      minimumVisibleMs: 1000,
      steps: [
        ["资源检查", "正在校验 BRep 与拓扑资源"],
        ["场景写入", "正在创建工件 Entity"],
        ["显示实例", "正在绑定 RenderInstance 与 Transform"],
        ["激活工件", "正在更新当前激活工件"],
        ["View 更新", "正在发布显示资源与 View 新版本"],
      ],
    };
  }
  if (sdoMethod === "Toolpath.RecognizeLoops") {
    return {
      title: "识别孔特征",
      detail: "正在扫描模型拓扑",
      stage: "特征识别",
      mode: "Feature",
      minimumVisibleMs: 1200,
      steps: [
        ["拓扑扫描", "正在扫描边和 Loop"],
        ["孔特征匹配", "正在匹配孔、V 坡口和 X 坡口候选"],
        ["结果写入", "正在更新选择与候选刀路"],
      ],
    };
  }
  if (sdoMethod === "Toolpath.AddSelectionPath") {
    return {
      title: "生成刀路",
      detail: "正在从当前选择生成空间曲线",
      stage: "刀路生成",
      mode: "Toolpath",
      minimumVisibleMs: 1200,
      steps: [
        ["曲线构建", "正在生成空间曲线"],
        ["姿态场", "正在初始化五轴姿态场"],
        ["写入项目", "正在写入刀路列表"],
      ],
    };
  }
  return {
    title: "后台执行",
    detail: "正在等待后台调用完成",
    stage: sdoMethod,
    mode: "SDO",
    minimumVisibleMs: 800,
    steps: [[sdoMethod, "正在等待后台调用完成"]],
  };
}

function showNotice(context, view, message) {
  view.notice = message;
  view.noticeRevision = Number(view.noticeRevision ?? 0) + 1;
  view.error = "";
  renderProject(context, view);
}

function synchronizeNoticeDismiss(mount, view) {
  const notice = String(view.notice ?? "");
  if (!notice) {
    if (view.noticeDismissTimer) {
      window.clearTimeout(view.noticeDismissTimer);
    }
    view.noticeDismissTimer = null;
    view.noticeDismissValue = "";
    view.noticeDismissRevision = Number(view.noticeRevision ?? 0);
    return;
  }

  const revision = Number(view.noticeRevision ?? 0);
  if (view.noticeDismissTimer
      && view.noticeDismissValue === notice
      && view.noticeDismissRevision === revision) {
    return;
  }
  if (view.noticeDismissTimer) {
    window.clearTimeout(view.noticeDismissTimer);
  }

  view.noticeDismissValue = notice;
  view.noticeDismissRevision = revision;
  view.noticeDismissTimer = window.setTimeout(() => {
    view.noticeDismissTimer = null;
    if (String(view.notice ?? "") !== notice
        || Number(view.noticeRevision ?? 0) !== revision) {
      return;
    }
    view.notice = "";
    view.noticeDismissValue = "";
    const renderedNotice = mount.querySelector(".cam-status.notice");
    if (renderedNotice?.textContent?.trim() === notice.trim()) {
      renderedNotice.remove();
    }
  }, NOTICE_VISIBLE_DURATION_MS);
}

return { mountProduct, mountProject, synchronizeActiveAreaView, waitForActiveAreaAction, handleRibbonCommand };
}
