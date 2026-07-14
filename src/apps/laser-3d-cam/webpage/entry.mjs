import { createThreeViewport } from "../../../iCAX-UI/SDK/index.mjs";
import { renderProgress } from "./layout/commonViews.mjs";
import { attachMachineAppearanceAutoApply, attachMachineJointLimitAutoApply, attachMachineToolTCPAutoApply, attachMachineTransformAutoApply, handleMachineAction, handleMachineRibbonCommand, importMachinePath as importMachinePathAction, selectMachineDefinition as selectMachineDefinitionAction, selectMachineInstance as selectMachineInstanceAction, setMachineInstanceEnabled as setMachineInstanceEnabledAction } from "./machine/machineActions.mjs";
import { renderMachineLeftPane, renderMachineRightPane } from "./machine/machineArea.mjs";
import { handleMachiningAction, handleMachiningRibbonCommand, setJobMachine as setJobMachineAction } from "./machining/machiningActions.mjs";
import { renderMachiningLeftPane, renderMachiningRightPane } from "./machining/machiningArea.mjs";
import { findCommandTitle, getTabTitle, normalizeCamTab } from "./ribbon/ribbonDefinition.mjs";
import { getProjectView } from "./state/projectViewStore.mjs";
import { getMachineId, getMachineSubtreeEntityIds, getMachines, getSelectedMachine, reconcileSelectedMachine } from "./state/sceneSelectors.mjs";
import { renderToolpathOverlay } from "./toolpath/toolpathViews.mjs";
import { escapeText, formatNumber } from "./utils/format.mjs";
import { renderViewLeftPane, renderViewRightPane } from "./view/viewArea.mjs";
import { ensureStyles } from "./styles/ensureStyles.mjs";
import { handleWorkpieceAction, handleWorkpieceRibbonCommand } from "./workpiece/workpieceActions.mjs";
import { renderWorkpieceLeftPane, renderWorkpieceRightPane } from "./workpiece/workpieceArea.mjs";
import { renderEdge, renderFace, renderLoop } from "./workpiece/workpieceViews.mjs";
import { exposeLaserCamAutomation } from "./automation/laserCamAutomation.mjs";

export { getRibbonDefinition } from "./ribbon/ribbonDefinition.mjs";

const DEFAULT_WORKBENCH_LAYOUT = Object.freeze({
  leftWidth: 320,
  rightWidth: 340,
});
const WORKBENCH_LAYOUT_LIMITS = Object.freeze({
  minLeftWidth: 190,
  maxLeftWidth: 560,
  minRightWidth: 220,
  maxRightWidth: 620,
  minViewerWidth: 420,
  splitterTotalWidth: 12,
});

function getProjectOps() {
  return {
    renderProject,
    runProjectCommand,
    runProjectCommandPayload,
    refreshSceneState,
    appendProjectLog,
    fitViewAfterRenderPublish,
    showNotice,
  };
}

export function mountProduct(context) {
  const { mount, product } = context;
  if (!mount) {
    return;
  }
  ensureStyles();
  mount.innerHTML = `
    <div class="cam-product-home">
      <strong>${escapeText(product.productName || "三维线条切割 CAM")}</strong>
      <span>${escapeText(product.projectFile?.magic ?? "")}</span>
    </div>
  `;
}

export function mountProject(context) {
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

export async function handleRibbonCommand(context, commandId) {
  const view = context?.project?.projectId ? getProjectView(context.project.projectId) : null;
  if (!view) {
    return;
  }
  const ops = getProjectOps();

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
    runProjectCommand(context, view, "CameraView.Fit", {});
  } else if (commandId === "view.reset-layout") {
    resetWorkbenchLayout(view);
    showNotice(context, view, "布局已恢复默认尺寸。");
  } else {
    showNotice(context, view, `${findCommandTitle(commandId)} 功能入口已就位，等待后端能力接入。`);
  }
}

function renderProject(context, view) {
  const mount = resolveProjectMount(context);
  const { project } = context;
  if (!mount) {
    return;
  }
  const tab = normalizeCamTab(context.activeRibbonTabId);
  const scene = view.scene ?? {};
  reconcileSelectedMachine(view, scene);
  const topology = scene.topology ?? {};
  const layout = getWorkbenchLayout(view);

  mount.innerHTML = `
    <div class="cam-workbench" style="${renderWorkbenchLayoutStyle(layout)}">
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
            <span>${escapeText(getTabTitle(tab))}</span>
            <span>${context.sceneProxy?.pdo?.enabled ? "PDO" : "SVG"}</span>
          </div>
        </div>
        <div class="cam-viewport">
          ${renderViewport(context, scene)}
        </div>
      </section>
      <div class="cam-splitter cam-splitter-right"
           data-cam-resize-pane="right"
           data-no-window-drag
           title="拖拽调整右侧区域宽度"></div>
      <aside class="cam-info-pane">
        ${renderRightPane(tab, context, view)}
      </aside>
      ${view.pending && view.progress ? renderProgress(view.progress) : ""}
      ${view.notice ? `<div class="cam-status notice">${escapeText(view.notice)}</div>` : ""}
      ${view.error ? `<div class="cam-status error">${escapeText(view.error)}</div>` : ""}
    </div>
  `;

  attachWorkbenchResizeHandlers(mount, view);

  const pathInput = mount.querySelector("[data-cam-model-path]");
  pathInput?.addEventListener("input", () => {
    view.sourcePath = pathInput.value;
  });

  const machinePathInput = mount.querySelector("[data-cam-machine-path]");
  machinePathInput?.addEventListener("input", () => {
    view.machineSourcePath = machinePathInput.value;
  });
  attachMachineTransformAutoApply(context, view, getProjectOps());
  attachMachineJointLimitAutoApply(context, view, getProjectOps());
  attachMachineAppearanceAutoApply(context, view, getProjectOps());
  attachMachineToolTCPAutoApply(context, view, getProjectOps());

  mount.onclick = (event) => {
    const actionTarget = event.target instanceof Element ? event.target.closest("[data-cam-action]") : null;
    if (actionTarget && !actionTarget.hasAttribute("disabled")) {
      runAction(context, view, actionTarget.dataset.camAction, actionTarget);
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
      runProjectCommand(context, view, "Workpiece.SetActive", { workpieceEntityId: workpieceTarget.dataset.camWorkpieceId });
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
      runProjectCommand(context, view, "Selection.PickTopology", { kind, id, label });
    }
  };

  const jobMachineSelect = mount.querySelector("[data-cam-job-machine-select]");
  jobMachineSelect?.addEventListener("change", () => {
    if (view.pending) {
      return;
    }
    setJobMachineAction(context, view, jobMachineSelect.value, getProjectOps());
  });

  mountRenderViewport(context, view);
  scrollSelectedMachineTreeNodeIntoView(mount, view);
}

function renderLeftPane(tab, context, view) {
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
  return view.layout;
}

function resetWorkbenchLayout(view) {
  view.layout = { ...DEFAULT_WORKBENCH_LAYOUT };
}

function renderWorkbenchLayoutStyle(layout) {
  return [
    `--cam-left-width:${escapeText(Math.round(layout.leftWidth))}px`,
    `--cam-right-width:${escapeText(Math.round(layout.rightWidth))}px`,
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
  if (side !== "left" && side !== "right") {
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
    startLayout,
    workbench,
  };

  document.body.classList.add("cam-pane-resizing");

  const onPointerMove = (moveEvent) => {
    const deltaX = moveEvent.clientX - resizeState.startX;
    const nextLayout = { ...resizeState.startLayout };
    if (resizeState.side === "left") {
      nextLayout.leftWidth = resizeState.startLayout.leftWidth + deltaX;
    } else {
      nextLayout.rightWidth = resizeState.startLayout.rightWidth - deltaX;
    }
    applyWorkbenchLayout(view, resizeState.workbench, nextLayout, resizeState.side);
  };

  const endResize = () => {
    window.removeEventListener("pointermove", onPointerMove);
    window.removeEventListener("pointerup", endResize);
    window.removeEventListener("pointercancel", endResize);
    document.body.classList.remove("cam-pane-resizing");
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
  const normalized = normalizeWorkbenchLayout(nextLayout, workbench.getBoundingClientRect().width, activeSide);
  view.layout = normalized;
  workbench.style.setProperty("--cam-left-width", `${Math.round(normalized.leftWidth)}px`);
  workbench.style.setProperty("--cam-right-width", `${Math.round(normalized.rightWidth)}px`);
}

function normalizeWorkbenchLayout(layout, width, activeSide = "") {
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

  return {
    leftWidth: Math.round(leftWidth),
    rightWidth: Math.round(rightWidth),
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

function renderViewport(context, scene) {
  const model = scene.model ?? {};
  if (context.sceneProxy?.pdo?.enabled) {
    return `
      <div class="cam-render-viewport-shell">
        <div class="cam-render-viewport" data-cam-render-viewport></div>
        <div class="cam-viewcube" data-no-window-drag aria-label="视角切换">
          <div class="cam-viewcube-cube" aria-hidden="false">
            <button class="cam-viewcube-face cam-viewcube-face-top" type="button" data-cam-action="view-standard" data-cam-view="top" title="顶视图">TOP</button>
            <button class="cam-viewcube-face cam-viewcube-face-front" type="button" data-cam-action="view-standard" data-cam-view="front" title="前视图">FRONT</button>
            <button class="cam-viewcube-face cam-viewcube-face-right" type="button" data-cam-action="view-standard" data-cam-view="right" title="右视图">RIGHT</button>
            <button class="cam-viewcube-face cam-viewcube-face-left" type="button" data-cam-action="view-standard" data-cam-view="left" title="左视图">LEFT</button>
          </div>
          <button class="cam-viewcube-iso" type="button" data-cam-action="view-standard" data-cam-view="iso" title="等轴测">ISO</button>
        </div>
      </div>
    `;
  }

  if (!model.isLoaded) {
    return `
      <div class="cam-empty-model">
        <strong>三维线条切割 CAM</strong>
        <span>请先准备机床定义和工件资源，然后进入“加工”大区编程</span>
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
    return;
  }

  if (!view.viewport) {
    view.viewport = createThreeViewport({
      backgroundColor: 0x182128,
      onPick: (userData, hit) => handleViewportPick(context, view, userData, hit),
      onDiagnostic: (entry) => appendProjectLog(context, entry.level ?? "info", entry.message ?? ""),
    });
  }
  if (view.viewportSceneProxy !== context.sceneProxy) {
    view.viewport.connectScene(context.sceneProxy);
    view.viewportSceneProxy = context.sceneProxy;
  }
  view.viewport.mount(host);
  syncViewportSelection(view, view.scene);
  exposeLaserCamAutomation(context, view, {
    importMachineDefinition: (commandContext, commandView, sourcePath) =>
      importMachinePathAction(commandContext, commandView, sourcePath, getProjectOps()),
    setMachineInstanceEnabled: (commandContext, commandView, machineEntityId, enabled) =>
      setMachineInstanceEnabledAction(commandContext, commandView, machineEntityId, enabled, getProjectOps()),
    setJobMachine: (commandContext, commandView, machineEntityId) =>
      setJobMachineAction(commandContext, commandView, machineEntityId, getProjectOps()),
    pickMachineObject: (commandContext, commandView, payload) =>
      pickMachineObject(commandContext, commandView, payload),
    setMachineElementAppearance: (commandContext, commandView, payload) =>
      runProjectCommandPayload(commandContext, commandView, "Machine.SetElementAppearance", payload, { timeoutMs: 10000 }),
    setStandardCameraView: (commandContext, commandView, viewName) =>
      runProjectCommand(commandContext, commandView, "CameraView.SetStandard", { view: viewName }, { refreshScene: false, timeoutMs: 10000 }),
    fitView: (commandContext, commandView) => runProjectCommand(commandContext, commandView, "CameraView.Fit", {}),
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

function runAction(context, view, action, actionTarget = null) {
  const ops = getProjectOps();
  if (action === "view-standard") {
    const viewName = String(actionTarget?.dataset?.camView ?? "iso").trim() || "iso";
    runProjectCommand(context, view, "CameraView.SetStandard", { view: viewName });
    return;
  }
  if (handleMachineAction(context, view, action, actionTarget, ops)) {
    return;
  }
  if (handleWorkpieceAction(context, view, action, ops)) {
    return;
  }
  if (handleMachiningAction(context, view, action, ops)) {
    return;
  }
  showNotice(context, view, "该功能入口已就位，等待后端能力接入。");
}

function handleViewportPick(context, view, userData, hit) {
  if (!userData || !hit) {
    view.error = "未命中可选择对象";
    selectSceneObjectLocally(view, "");
    renderProject(context, view);
    return;
  }

  const objectId = String(userData.objectId ?? "").trim();
  if (Number(userData.renderClass) === 4 && objectId && objectId !== "0") {
    pickMachineObject(context, view, {
      objectId,
      entityId: objectId,
      kind: "machine.part",
      label: `machine object ${objectId}`,
    });
    return;
  }

  const faceIndex = Number(hit.faceIndex);
  if (Number.isInteger(faceIndex) && faceIndex >= 0) {
    const face = findFaceByTriangleIndex(view.scene, faceIndex);
    if (face) {
      selectSceneObjectLocally(view, "");
      runProjectCommand(context, view, "Selection.PickTopology", {
        kind: "face",
        id: Number(face.id),
        label: face.label ?? `face ${face.id}`,
      });
      return;
    }
  }

  view.error = "当前渲染数据缺少 edge/loop 拾取映射，已命中工件但无法定位拓扑对象";
  selectSceneObjectLocally(view, "");
  renderProject(context, view);
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
  if (!context.sceneProxy) {
    return false;
  }

  view.pending = true;
  view.error = "";
  renderProject(context, view);
  try {
    const scene = {};
    for (const command of [
      "Job.Get",
      "MachineDefinition.List",
      "Machine.List",
      "Workpiece.List",
      "Toolpath.List",
      "Selection.Get",
    ]) {
      mergeScenePayload(scene, await context.sceneProxy.execute(command, {}, { timeoutMs: 30000 }));
    }
    scene.readiness = buildReadiness(scene);
    view.scene = scene;
    syncViewportSelection(view, scene);
    await refreshSelectedMachineElement(context, view, scene);
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

async function refreshSelectedMachineElement(context, view, scene = {}) {
  const selection = scene.selection ?? {};
  const kind = String(selection.kind ?? "");
  const entityId = String(selection.entityId ?? "").trim();
  if (!entityId || !kind.startsWith("machine")) {
    scene.machineElement = null;
    return;
  }

  try {
    mergeScenePayload(scene, await context.sceneProxy.execute("Machine.GetElement", { entityId }, { timeoutMs: 10000 }));
    syncViewportSelection(view, scene);
  } catch (error) {
    scene.machineElement = null;
    appendProjectLog(context, "error", `读取机床元素属性失败：${error?.message ?? String(error)}`);
  }
}

async function runProjectCommand(context, view, command, payload, options = {}) {
  const { refreshScene: shouldRefreshScene = shouldRefreshSceneAfterCommand(command), ...executeOptions } = options;
  const result = await runProjectCommandPayload(context, view, command, payload, { ...executeOptions, expectScene: false });
  if (result.ok) {
    mergeScenePayload(view.scene ??= {}, result.payload);
    syncViewportSelection(view, view.scene);
    if (shouldRefreshScene) {
      await refreshSceneState(context, view);
    }
  }
  return result.ok;
}

async function runProjectCommandPayload(context, view, command, payload, options = {}) {
  if (!context.sceneProxy) {
    return { ok: false, payload: null };
  }

  const { expectScene = false, ...executeOptions } = options;
  appendProjectLog(context, "info", makeCommandStartLog(command, payload));
  const showProgress = isLongProjectCommand(command);
  const progress = showProgress ? createCommandProgress(command) : null;
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
    const execute = () => context.sceneProxy.execute(command, payload, executeOptions);
    await waitForPaint();
    let responsePayload = null;
    if (useShellProjectProgress) {
      responsePayload = await context.actions.withProjectProgress(context.project.projectId, progress, execute);
    } else {
      responsePayload = await execute();
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
      command === "CameraView.Fit" && !view.scene?.fitView?.fitted ? "info" : "ok",
      makeCommandSuccessLog(command, expectScene ? view.scene : responsePayload),
    );
    return { ok: true, payload: responsePayload };
  } catch (error) {
    view.error = error?.message ?? String(error);
    appendProjectLog(context, "error", `${command} 失败：${view.error}`);
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

function shouldRefreshSceneAfterCommand(command) {
  return ![
    "CameraView.Fit",
    "Machine.GetElement",
    "Selection.Get",
    "Selection.PickMachineObject",
    "Selection.PickTopology",
  ].includes(command);
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
    "selection",
    "cuttingLayers",
    "visibleLayers",
    "program",
    "toolpaths",
    "fitView",
  ]) {
    if (Object.prototype.hasOwnProperty.call(payload, key)) {
      if (key === "definitions") {
        scene.machineDefinitions = payload.definitions;
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
  const machineReady = Boolean(machine?.enabled !== false && (machine?.machineResourceId || machine?.resourceId));
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
  if (scene?.selection) {
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

  const pickResult = await runProjectCommandPayload(
    context,
    view,
    "Selection.PickMachineObject",
    { ...payload, entityId, objectId: entityId },
    { expectScene: false },
  );
  if (!pickResult.ok) {
    return pickResult;
  }

  const elementResult = await runProjectCommandPayload(
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
  if (typeof context.actions?.log === "function") {
    context.actions.log(level, text);
    return;
  }
  console[level === "error" ? "error" : "log"](text);
}

function makeCommandStartLog(command, payload) {
  const sourcePath = payload?.sourcePath ? `：${payload.sourcePath}` : "";
  if (command === "MachineDefinition.Import") {
    return `开始导入机床定义${sourcePath}`;
  }
  if (command === "Machine.Instantiate") {
    return `开始实例化机床：${payload?.machineDefinitionId ?? payload?.machineResourceId ?? payload?.resourceId ?? "-"}`;
  }
  if (command === "Machine.SetEnabled") {
    return `${payload?.enabled === false ? "禁用" : "启用"}机床实例：${payload?.machineEntityId ?? "-"}`;
  }
  if (command === "WorkpieceModel.Import") {
    return `开始导入工件模型资源${sourcePath}`;
  }
  if (command === "Workpiece.Instantiate") {
    return `开始实例化工件：${payload?.modelResourceId ?? "-"}`;
  }
  if (command === "CameraView.Fit") {
    return "开始自动适配最佳视角";
  }
  return `执行 ${command}`;
}

function makeCommandSuccessLog(command, scene) {
  if (command === "MachineDefinition.Import") {
    return `机床定义导入完成：definition=${scene?.machineDefinitionId ?? "-"}, resource=${scene?.machineResourceId ?? scene?.resourceId ?? "-"}, links=${scene?.linkCount ?? 0}, joints=${scene?.jointCount ?? 0}, visuals=${scene?.visualCount ?? 0}, collisions=${scene?.collisionCount ?? 0}`;
  }
  if (command === "Machine.Instantiate") {
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
  if (command === "Machine.SetEnabled") {
    const machine = scene?.machine ?? getSelectedMachine(scene, {});
    return `机床实例状态已更新：${machine.enabled === false ? "已禁用" : "已启用"}`;
  }
  if (command === "WorkpieceModel.Import") {
    return `工件模型资源导入完成：model=${scene?.modelResourceId ?? "-"}, brep=${scene?.brepResourceId ?? "-"}, topology=${scene?.topologyResourceId ?? "-"}`;
  }
  if (command === "Workpiece.Instantiate") {
    const topology = scene?.topology ?? {};
    return `工件实例化完成：faces=${topology.faceCount ?? 0}, loops=${topology.loopCount ?? 0}, edges=${topology.edgeCount ?? 0}`;
  }
  if (command === "CameraView.Fit") {
    const fit = scene?.fitView ?? {};
    if (!fit.fitted) {
      return [
        `最佳视角暂未生效：${fit.reason ?? "等待 RenderPDO 发布"}`,
        `machine=${fit.machineCount ?? 0}`,
        `visual=${fit.machineVisualCount ?? 0}`,
        `collision=${fit.machineCollisionCount ?? 0}`,
        `include=${fit.machineIncludeCount ?? 0}`,
        `workpiece=${fit.workpieceCount ?? 0}`,
        `renderable=${fit.renderInstanceComponentCount ?? 0}`,
        `mesh=${fit.renderMeshCount ?? 0}`,
        `object=${fit.renderObjectCount ?? 0}`,
        `transform=${fit.renderTransformCount ?? 0}`,
        `camera=${fit.renderCameraCount ?? 0}`,
        `marker=${fit.hasRenderMarker ? "yes" : "no"}`
      ].join(" / ");
    }
    return `最佳视角已适配：radius=${formatNumber(fit.radius ?? 0, 2)}, distance=${formatNumber(fit.distance ?? 0, 2)}, speed=${formatNumber(fit.moveSpeed ?? 0, 2)}`;
  }
  return `${command} 完成`;
}

async function fitViewAfterRenderPublish(context, view) {
  for (let attempt = 0; attempt < 5; attempt += 1) {
    await sleep(attempt === 0 ? 120 : 180);
    const ok = await runProjectCommand(context, view, "CameraView.Fit", {});
    if (ok && view.scene?.fitView?.fitted) {
      return;
    }
    if (ok && isFitBlockedByNoRenderableSource(view.scene?.fitView)) {
      return;
    }
  }
}

function isFitBlockedByNoRenderableSource(fit = null) {
  if (!fit || fit.fitted || fit.reason !== "render scene is empty") {
    return false;
  }
  const sourceCount =
    Number(fit.machineVisualCount ?? 0)
    + Number(fit.machineCollisionCount ?? 0)
    + Number(fit.machineIncludeCount ?? 0)
    + Number(fit.workpieceCount ?? 0)
    + Number(fit.renderInstanceComponentCount ?? 0)
    + Number(fit.renderObjectCount ?? 0)
    + Number(fit.renderMeshCount ?? 0)
    + Number(fit.renderPolylineCount ?? 0)
    + Number(fit.renderToolpathCount ?? 0);
  return sourceCount === 0;
}

function sleep(durationMs) {
  return new Promise((resolve) => window.setTimeout(resolve, durationMs));
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

function isLongProjectCommand(command) {
  return command === "MachineDefinition.Import"
    || command === "Machine.Instantiate"
    || command === "WorkpieceModel.Import"
    || command === "Workpiece.Instantiate"
    || command === "Toolpath.RecognizeLoops"
    || command === "Toolpath.AddSelectionPath";
}

function createCommandProgress(command) {
  if (command === "MachineDefinition.Import") {
    return {
      title: "导入机床定义",
      detail: "正在解析 SDF 机床定义文件",
      stage: "机床定义导入",
      mode: "Machine",
      minimumVisibleMs: 1600,
      steps: [
        ["文件检查", "正在校验 SDF/XML 路径"],
        ["SDF 解析", "正在解析 link、joint、visual、collision 和 material"],
        ["资源入库", "正在保存机床定义资源"],
        ["定义登记", "正在写入机床定义组件"],
        ["结果返回", "正在返回机床定义 ID"],
      ],
    };
  }
  if (command === "Machine.Instantiate") {
    return {
      title: "实例化机床",
      detail: "正在把机床定义展开到当前加工场景",
      stage: "机床实例化",
      mode: "Machine",
      minimumVisibleMs: 1200,
      steps: [
        ["资源读取", "正在读取机床定义资源"],
        ["结构展开", "正在创建 link、joint、visual 和 collision Entity"],
        ["参数初始化", "正在初始化 TCP、轴限位和机床状态"],
        ["显示同步", "正在发布机床 visual 到 RenderPDO"],
        ["面板刷新", "正在刷新机床结构面板"],
      ],
    };
  }
  if (command === "WorkpieceModel.Import") {
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
  if (command === "Workpiece.Instantiate") {
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
        ["PDO 同步", "正在同步显示数据到前端"],
      ],
    };
  }
  if (command === "Toolpath.RecognizeLoops") {
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
  if (command === "Toolpath.AddSelectionPath") {
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
    detail: "正在等待后台命令完成",
    stage: command,
    mode: "Command",
    minimumVisibleMs: 800,
    steps: [[command, "正在等待后台命令完成"]],
  };
}

function showNotice(context, view, message) {
  view.notice = message;
  view.error = "";
  renderProject(context, view);
}
