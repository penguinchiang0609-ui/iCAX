import {
  connectApplication,
  escapeAttr,
  escapeText,
  loadProductModule,
  mountProductModule,
} from "../../index.mjs";

const root = document.getElementById("app");
const TOOLBAR_TOOLTIP_SELECTOR = [
  "[data-toolbar-tooltip]",
  ".ribbon-command",
  ".ribbon-command-menu-toggle",
  ".ribbon-command-menu-item",
  ".quick-button",
  ".tubest-part-toolbar button",
  ".tube-cad-tool-ribbon button",
  ".tubest-editor-header button",
].join(",");
const TOOLBAR_TOOLTIP_DELAY_MS = 260;
let toolbarTooltipTimer = 0;
let toolbarTooltipTarget = null;

const state = {
  bridgeStatus: "Disconnected",
  bridge: null,
  appState: null,
  appProxy: null,
  activeProductState: null,
  activeProductProxy: null,
  activeProjectState: null,
  activeProjectProxy: null,
  activeSceneState: null,
  activeSceneProxy: null,
  selectedProductId: "",
  productModules: new Map(),
  productSurfaceErrorKey: "",
  productSurfaceMountPromise: Promise.resolve(),
  productSurfaceMountSequence: 0,
  pendingCount: 0,
  pendingOperations: [],
  projectOperations: new Map(),
  logs: [],
  error: "",
  startCenterOpen: true,
  projectWindowStart: 0,
  activeRibbonTabId: "",
  openRibbonMenuCommandId: "",
  projectSearchText: "",
  projectSortMode: "modified-desc",
  newProjectDialogOpen: false,
  newProjectName: "",
  newProjectPath: "",
  newProjectPathTouched: false,
  newProjectError: "",
  bottomDockHeight: 64,
  bottomDockResize: null,
};

const actions = {
  async bootstrap() {
    state.bridgeStatus = "Connecting";
    render();

    state.appProxy = await connectApplication();
    state.bridge = state.appProxy.bridge;
    state.bridgeStatus = "Connected";

    await actions.refresh();
  },

  async refresh() {
    await track("App.GetState", async () => {
      state.appState = await state.appProxy.getState();
      ensureSelectedProduct();
      await mountSelectedProductModule();
      await mountActiveProductModule();
    });
  },

  async selectProduct(productId) {
    state.selectedProductId = productId ?? "";
    await mountSelectedProductModule();
    if (await render() === false) {
      throw new Error(state.error || "Product surface failed to mount");
    }
  },

  async ensureProductStarted(productId) {
    const productState = findProductState(productId);
    if (!productState?.productId) {
      throw new Error("没有可用产品。");
    }

    const existing = state.appProxy.getProduct(productState.productId);
    if (existing) {
      return existing;
    }

    const product = await state.appProxy.startProduct(productState.productId);
    state.selectedProductId = product.productId;
    state.activeProductProxy = product;
    state.activeProductState = { ...productState, ...product.state };
    await actions.refresh();
    return state.appProxy.getProduct(product.productId) ?? product;
  },

  async openProjectFromPath(projectPath, productId = state.selectedProductId) {
    const path = String(projectPath ?? "").trim();
    if (!path) {
      state.error = "项目路径不能为空。";
      render();
      return;
    }

    await track("App.OpenProjectFile", async () => {
      const response = await state.appProxy.openProjectFile(path);
      await enterProject({ ...response, projectPath: path });
      state.startCenterOpen = false;
    });
    if (await state.productSurfaceMountPromise === false) {
      throw new Error(state.error || "Opened project surface failed to synchronize");
    }
  },

  async newProjectCatalog(options = {}) {
    const productId = options.productId ?? state.selectedProductId;
    const projectName = String(options.projectName ?? "").trim();
    if (!projectName) {
      state.newProjectError = "项目名称不能为空。";
      render();
      return;
    }

    const productState = findProductState(productId);
    const { projectPath, error: pathError } = resolveNewProjectPath(options.projectPath, projectName, productState);
    if (pathError) {
      state.newProjectError = pathError;
      render();
      return;
    }
    state.newProjectPath = projectPath;

    await track("Product.OpenProjectCatalog", async () => {
      state.newProjectError = "";
      const productProxy = await actions.ensureProductStarted(productId);
      const response = await productProxy.openProjectCatalog(projectPath, {
        catalogPath: projectPath,
        catalogName: projectName,
        projectName,
      });
      await enterProject({
        productProxy,
        product: productProxy.state,
        projectProxy: response.projectProxy,
        sceneProxy: response.sceneProxy,
        catalog: response.catalog,
        projectPath,
      });
    });
    if (await state.productSurfaceMountPromise === false) {
      throw new Error(state.error || "New project surface failed to synchronize");
    }
  },

  async chooseProjectFile() {
    const bridge = state.bridge ?? state.appProxy?.bridge ?? null;
    if (typeof bridge?.openFileDialog !== "function") {
      state.error = "当前宿主没有提供文件选择能力。";
      render();
      return;
    }
    const path = await bridge.openFileDialog({
      filters: [{ name: "iCAX Project", extensions: ["icax", "i3cam"] }],
    });
    if (path) {
      await actions.openProjectFromPath(path);
    }
  },

  async undoProject() {
    if (!state.activeSceneProxy) {
      return;
    }

    await track("Project.Undo", async () => {
      const undoRedo = await state.activeSceneProxy.undo();
      const sceneState = await state.activeSceneProxy.getState();
      state.activeSceneState = { ...sceneState, undoRedo };
    });
    if (await state.productSurfaceMountPromise === false) {
      throw new Error(state.error || "Product surface failed to synchronize after undo");
    }
  },

  async redoProject() {
    if (!state.activeSceneProxy) {
      return;
    }

    await track("Project.Redo", async () => {
      const undoRedo = await state.activeSceneProxy.redo();
      const sceneState = await state.activeSceneProxy.getState();
      state.activeSceneState = { ...sceneState, undoRedo };
    });
    if (await state.productSurfaceMountPromise === false) {
      throw new Error(state.error || "Product surface failed to synchronize after redo");
    }
  },

  async refreshActiveSceneState() {
    if (!state.activeSceneProxy) {
      return null;
    }
    const sceneState = await state.activeSceneProxy.getState();
    state.activeSceneState = sceneState;
    const surfaceMount = render();
    if (await surfaceMount === false) {
      throw new Error(state.error || "Product surface failed to synchronize with scene state");
    }
    return sceneState;
  },

  async withProgress(label, work, progress = {}) {
    return track(label, work, progress);
  },

  log(level, message) {
    pushLog(level ?? "info", message ?? "");
    updateBottomDockLogs();
  },

  async withProjectProgress(projectId, progress, work) {
    const normalizedProjectId = String(projectId ?? "");
    if (!normalizedProjectId || typeof work !== "function") {
      return work?.();
    }

    const operation = createProgressOperation(progress?.title ?? "项目任务", progress ?? {});
    operation.projectId = normalizedProjectId;
    const operations = state.projectOperations.get(normalizedProjectId) ?? [];
    operations.push(operation);
    state.projectOperations.set(normalizedProjectId, operations);

    const stopProgressTimer = startProgressStageTimer(operation);
    render();
    await waitForPaint();
    try {
      return await work();
    } finally {
      await waitForMinimumProgressDuration(operation);
      stopProgressTimer();
      const currentOperations = state.projectOperations.get(normalizedProjectId) ?? [];
      const nextOperations = currentOperations.filter((item) => item.id !== operation.id);
      if (nextOperations.length) {
        state.projectOperations.set(normalizedProjectId, nextOperations);
      } else {
        state.projectOperations.delete(normalizedProjectId);
      }
      render();
    }
  },

  async selectOpenProject(projectId) {
    const entry = getOpenProjects().find((item) => item.projectId === projectId);
    if (!entry) {
      return;
    }

    state.activeProductProxy = entry.productProxy;
    state.activeProductState = entry.productProxy?.state ?? findProductState(entry.productId);
    state.activeProjectProxy = entry.projectProxy;
    state.activeProjectState = entry.projectState;
    state.activeSceneProxy = entry.projectProxy?.getMainScene?.() ?? null;
    state.activeSceneState = state.activeSceneProxy?.state ?? entry.projectState?.mainScene ?? null;
    state.startCenterOpen = false;
    await mountActiveProductModule();
    ensureActiveRibbonTab();
    if (await render() === false) {
      throw new Error(state.error || "Selected project surface failed to synchronize");
    }
  },

  openStartCenter() {
    state.startCenterOpen = true;
    ensureSelectedProduct();
    render();
  },

  closeStartCenter() {
    if (!state.activeProjectState) {
      return;
    }
    state.startCenterOpen = false;
    render();
  },

  openNewProjectDialog() {
    const product = getSelectedProductState();
    state.newProjectName = `${product?.productName ?? "iCAX"} 项目`;
    state.newProjectPath = makeDefaultProjectPath(state.newProjectName, product);
    state.newProjectPathTouched = false;
    state.newProjectError = "";
    state.newProjectDialogOpen = true;
    render();
  },

  closeNewProjectDialog() {
    state.newProjectDialogOpen = false;
    state.newProjectPathTouched = false;
    state.newProjectError = "";
    render();
  },

  async executeRibbonCommand(commandId) {
    if (commandId === "app.save") {
      pushLog("info", "保存命令已预留，等待项目文件保存链路接入。");
      render();
      return;
    }
    if (commandId === "edit.undo") {
      await actions.undoProject();
      return;
    }
    if (commandId === "edit.redo") {
      await actions.redoProject();
      return;
    }

    const module = getActiveProductModule();
    if (typeof module?.handleRibbonCommand === "function") {
      await module.handleRibbonCommand(buildProductContext(), commandId);
    }
  },

  async windowCommand(command) {
    if (command === "close") {
      const guard = getActiveWindowCloseGuard();
      if (guard?.blocked) {
        state.error = guard.message || "当前任务完成前不能退出软件。";
        pushLog("warning", state.error);
        render();
        return { closed: false, blocked: true };
      }
    }
    const bridge = state.bridge ?? state.appProxy?.bridge ?? null;
    if (typeof bridge?.windowCommand === "function") {
      await bridge.windowCommand(command);
    }
    return { closed: command === "close", blocked: false };
  },
};

exposeAppShellAutomation();

root.addEventListener("pointerover", (event) => {
  const target = resolveToolbarTooltipTarget(event.target);
  if (!target || target === toolbarTooltipTarget) return;
  scheduleToolbarTooltip(target);
});

root.addEventListener("pointerout", (event) => {
  if (!toolbarTooltipTarget) return;
  const nextTarget = event.relatedTarget instanceof Element
    ? event.relatedTarget.closest(TOOLBAR_TOOLTIP_SELECTOR)
    : null;
  if (nextTarget !== toolbarTooltipTarget) hideToolbarTooltip();
});

root.addEventListener("focusin", (event) => {
  const target = resolveToolbarTooltipTarget(event.target);
  if (target) scheduleToolbarTooltip(target, 0);
});

root.addEventListener("focusout", (event) => {
  const nextTarget = event.relatedTarget instanceof Element
    ? event.relatedTarget.closest(TOOLBAR_TOOLTIP_SELECTOR)
    : null;
  if (nextTarget !== toolbarTooltipTarget) hideToolbarTooltip();
});

window.addEventListener("resize", hideToolbarTooltip);
window.addEventListener("scroll", hideToolbarTooltip, true);
window.addEventListener("beforeunload", (event) => {
  const guard = getActiveWindowCloseGuard();
  if (!guard?.blocked) return;
  event.preventDefault();
  event.returnValue = guard.message || "当前任务完成前不能退出软件。";
});

root.addEventListener("click", (event) => {
  hideToolbarTooltip();
  const target = event.target instanceof Element ? event.target.closest("[data-action]") : null;
  if (!target) {
    closeRibbonCommandMenu();
    return;
  }
  if (target.hasAttribute("disabled")) {
    return;
  }

  const action = target.dataset.action;
  if (action === "refresh") {
    runAction(() => actions.refresh());
  } else if (action === "select-product") {
    runAction(() => actions.selectProduct(target.dataset.productId));
  } else if (action === "open-project") {
    runAction(() => actions.openProjectFromPath(target.dataset.projectPath ?? root.querySelector("[data-field='projectPath']")?.value));
  } else if (action === "choose-project") {
    runAction(() => actions.chooseProjectFile());
  } else if (action === "open-start-center") {
    actions.openStartCenter();
  } else if (action === "close-start-center") {
    actions.closeStartCenter();
  } else if (action === "open-new-project-dialog") {
    actions.openNewProjectDialog();
  } else if (action === "close-new-project-dialog") {
    actions.closeNewProjectDialog();
  } else if (action === "create-project") {
    state.newProjectName = root.querySelector("[data-field='newProjectName']")?.value ?? state.newProjectName;
    state.newProjectPath = root.querySelector("[data-field='newProjectPath']")?.value ?? state.newProjectPath;
    runAction(() => actions.newProjectCatalog({
      productId: state.selectedProductId,
      projectName: state.newProjectName,
      projectPath: state.newProjectPath,
    }));
  } else if (action === "sort-projects") {
    state.projectSortMode = nextProjectSortMode(target.dataset.sortKey);
    render();
  } else if (action === "undo-project") {
    if (isActiveProjectBusy()) {
      return;
    }
    runAction(() => actions.undoProject());
  } else if (action === "redo-project") {
    if (isActiveProjectBusy()) {
      return;
    }
    runAction(() => actions.redoProject());
  } else if (action === "project-window-prev") {
    state.projectWindowStart = Math.max(0, state.projectWindowStart - 1);
    render();
  } else if (action === "project-window-next") {
    const maxStart = Math.max(0, getOpenProjects().length - 3);
    state.projectWindowStart = Math.min(maxStart, state.projectWindowStart + 1);
    render();
  } else if (action === "select-open-project") {
    runAction(() => actions.selectOpenProject(target.dataset.projectId));
  } else if (action === "select-ribbon-tab") {
    if (isActiveProjectBusy()) {
      return;
    }
    closeRibbonCommandMenu();
    state.activeRibbonTabId = target.dataset.tabId ?? "";
    render();
  } else if (action === "ribbon-command-menu-toggle") {
    if (isActiveProjectBusy()) {
      return;
    }
    const commandId = String(target.dataset.commandId ?? "");
    state.openRibbonMenuCommandId = state.openRibbonMenuCommandId === commandId ? "" : commandId;
    render();
  } else if (action === "ribbon-command") {
    if (isActiveProjectBusy()) {
      return;
    }
    closeRibbonCommandMenu();
    runAction(() => actions.executeRibbonCommand(target.dataset.commandId));
  } else if (action === "window-minimize") {
    runAction(() => actions.windowCommand("minimize"));
  } else if (action === "window-maximize") {
    runAction(() => actions.windowCommand("maximize"));
  } else if (action === "window-close") {
    runAction(() => actions.windowCommand("close"));
  }
});

root.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && state.openRibbonMenuCommandId) {
    event.preventDefault();
    closeRibbonCommandMenu();
    return;
  }
  if (event.key !== "Enter" && event.key !== " ") {
    return;
  }

  const target = event.target instanceof Element ? event.target.closest("[data-action='open-project']") : null;
  if (!target) {
    return;
  }

  event.preventDefault();
  runAction(() => actions.openProjectFromPath(target.dataset.projectPath));
});

root.addEventListener("mousedown", (event) => {
  if (event.button !== 0) {
    return;
  }

  const resizeHandle = event.target instanceof Element ? event.target.closest("[data-bottom-dock-resize]") : null;
  if (resizeHandle) {
    beginBottomDockResize(event);
    return;
  }

  const titleBar = event.target instanceof Element ? event.target.closest(".title-bar") : null;
  if (!titleBar || event.target.closest("button,input,select,textarea,a,[data-no-window-drag]")) {
    return;
  }

  const bridge = state.bridge ?? state.appProxy?.bridge ?? null;
  if (typeof bridge?.beginWindowDrag === "function") {
    bridge.beginWindowDrag().catch(() => undefined);
  }
});

root.addEventListener("input", (event) => {
  const target = event.target;
  if (!(target instanceof HTMLInputElement)) {
    return;
  }

  if (target.dataset.field === "projectSearch") {
    state.projectSearchText = target.value;
    render();
  } else if (target.dataset.field === "newProjectName") {
    state.newProjectName = target.value;
    if (!state.newProjectPathTouched) {
      state.newProjectPath = makeDefaultProjectPath(state.newProjectName, getSelectedProductState());
      const pathInput = root.querySelector("[data-field='newProjectPath']");
      if (pathInput instanceof HTMLInputElement) {
        pathInput.value = state.newProjectPath;
      }
    }
  } else if (target.dataset.field === "newProjectPath") {
    state.newProjectPath = target.value;
    state.newProjectPathTouched = true;
  }
});

function render() {
  hideToolbarTooltip();
  if (!state.activeProjectState) {
    root.innerHTML = `
      <div class="startup-shell">
        ${renderStartupTitleBar()}
        ${renderStartCenter({ overlay: false })}
        ${renderGlobalProgress()}
      </div>
    `;
  } else {
    root.innerHTML = `
      <div class="workbench" style="--bottom-dock-height:${escapeAttr(state.bottomDockHeight)}px">
        ${renderTitleBar()}
        ${renderProductRibbon()}
        ${renderProjectWorkspace()}
        ${renderBottomDock()}
        ${state.startCenterOpen ? renderStartCenter({ overlay: true }) : ""}
        ${renderProjectProgress()}
        ${renderGlobalProgress()}
      </div>
    `;
  }

  if (state.newProjectDialogOpen) {
    root.insertAdjacentHTML("beforeend", renderNewProjectDialog());
  }

  return mountActiveProductSurface();
}

function resolveToolbarTooltipTarget(source) {
  if (!(source instanceof Element)) return null;
  const target = source.closest(TOOLBAR_TOOLTIP_SELECTOR);
  return target instanceof HTMLElement && root.contains(target) ? target : null;
}

function scheduleToolbarTooltip(target, delay = TOOLBAR_TOOLTIP_DELAY_MS) {
  hideToolbarTooltip();
  const text = getToolbarTooltipText(target);
  if (!text) return;
  toolbarTooltipTarget = target;
  toolbarTooltipTimer = window.setTimeout(() => {
    if (toolbarTooltipTarget === target && target.isConnected) {
      showToolbarTooltip(target, text);
    }
  }, delay);
}

function getToolbarTooltipText(target) {
  const nativeTitle = String(target.getAttribute("title") ?? "").trim();
  const text = String(
    target.dataset.toolbarTooltip
      || target.dataset.icaxToolbarTooltip
      || nativeTitle
      || target.getAttribute("aria-label")
      || target.querySelector(":scope > span")?.textContent
      || "",
  ).trim();
  if (nativeTitle) {
    target.dataset.icaxToolbarTooltip = nativeTitle;
    target.removeAttribute("title");
  }
  if (text && !target.hasAttribute("aria-label")) target.setAttribute("aria-label", text);
  return text;
}

function showToolbarTooltip(target, text) {
  const tooltip = ensureToolbarTooltip();
  tooltip.textContent = text;
  tooltip.hidden = false;
  tooltip.dataset.placement = "top";
  tooltip.style.left = "0px";
  tooltip.style.top = "0px";

  const targetRect = target.getBoundingClientRect();
  const tooltipRect = tooltip.getBoundingClientRect();
  const viewportWidth = document.documentElement.clientWidth;
  const viewportHeight = document.documentElement.clientHeight;
  const gap = 8;
  let top = targetRect.top - tooltipRect.height - gap;
  let placement = "top";
  if (top < 6) {
    top = targetRect.bottom + gap;
    placement = "bottom";
  }
  top = Math.min(Math.max(6, top), Math.max(6, viewportHeight - tooltipRect.height - 6));
  const left = Math.min(
    Math.max(6, targetRect.left + targetRect.width / 2 - tooltipRect.width / 2),
    Math.max(6, viewportWidth - tooltipRect.width - 6),
  );
  tooltip.dataset.placement = placement;
  tooltip.style.left = `${Math.round(left)}px`;
  tooltip.style.top = `${Math.round(top)}px`;
}

function ensureToolbarTooltip() {
  let tooltip = document.querySelector(".icax-toolbar-tooltip");
  if (tooltip instanceof HTMLElement) return tooltip;
  tooltip = document.createElement("div");
  tooltip.className = "icax-toolbar-tooltip";
  tooltip.id = "icax-toolbar-tooltip";
  tooltip.setAttribute("role", "tooltip");
  tooltip.hidden = true;
  document.body.append(tooltip);
  return tooltip;
}

function hideToolbarTooltip() {
  if (toolbarTooltipTimer) {
    window.clearTimeout(toolbarTooltipTimer);
    toolbarTooltipTimer = 0;
  }
  toolbarTooltipTarget = null;
  const tooltip = document.querySelector(".icax-toolbar-tooltip");
  if (tooltip instanceof HTMLElement) tooltip.hidden = true;
}

function exposeAppShellAutomation() {
  window.__icaxAppShell = {
    getState() {
      return {
        bridgeStatus: state.bridgeStatus,
        selectedProductId: state.selectedProductId,
        activeProductId: state.activeProductState?.productId ?? "",
        activeProjectId: state.activeProjectState?.projectId ?? "",
        activeProjectName: state.activeProjectState?.projectName ?? "",
        activeProjectPath: state.activeProjectState?.projectPath ?? "",
        activeSceneId: state.activeSceneState?.sceneId ?? "",
        startCenterOpen: state.startCenterOpen,
        pendingCount: state.pendingCount,
        error: state.error,
        products: getProducts().map((product) => ({
          productId: product.productId ?? "",
          productName: product.productName ?? product.name ?? "",
        })),
      };
    },
    async refresh() {
      await actions.refresh();
      return this.getState();
    },
    async selectProduct(productId) {
      await actions.selectProduct(productId);
      return this.getState();
    },
    async createProject(options = {}) {
      await actions.newProjectCatalog(options);
      return this.getState();
    },
    async openProject(projectPath, productId) {
      await actions.openProjectFromPath(projectPath, productId);
      return this.getState();
    },
    async executeRibbonCommand(commandId) {
      await actions.executeRibbonCommand(commandId);
      return this.getState();
    },
  };
}

function renderStartCenter({ overlay }) {
  const products = getProducts();
  const product = getSelectedProductState();
  const projects = filterRecentProjects(product?.recentProjects ?? []);
  return `
    <div class="${overlay ? "start-center-overlay" : "start-center-page"}">
      <section class="start-center">
        <header class="start-head">
          <div>
            <strong>iCAX</strong>
            <span>${escapeText(state.bridgeStatus)}</span>
          </div>
          ${overlay ? `<button class="icon-button" type="button" data-action="close-start-center" title="关闭">×</button>` : ""}
        </header>
        <div class="start-error ${state.error ? "" : "empty"}">${state.error ? escapeText(state.error) : ""}</div>
        <div class="start-body">
          <aside class="start-products">
            <div class="start-section-title">产品</div>
            <div class="start-product-list">
              ${products.length === 0 ? `<div class="empty-row">没有可用产品。</div>` : products.map((item) => `
                <button class="start-product ${product?.productId === item.productId ? "active" : ""}"
                        type="button"
                        data-action="select-product"
                        data-product-id="${escapeAttr(item.productId)}">
                  <span>${escapeText(item.productName || item.productId)}</span>
                  <small>${item.isStarted ? "运行中" : "待启动"}</small>
                </button>
              `).join("")}
            </div>
          </aside>
          <main class="start-projects">
            <div class="start-project-title">
              <div>
                <strong>最近项目</strong>
                <span>${escapeText(product?.productName ?? "选择产品")}${projects.length ? ` · ${projects.length}` : ""}</span>
              </div>
            </div>
            <div class="start-project-tools">
              <input data-field="projectSearch" type="search" value="${escapeAttr(state.projectSearchText)}" placeholder="搜索项目名称或文件路径" />
              <button class="create-project-button" type="button" data-action="open-new-project-dialog" ${product ? "" : "disabled"}>新建项目</button>
              <button class="tool-button" type="button" data-action="choose-project">浏览打开</button>
            </div>
            <div class="project-table">
              <div class="project-table-head">
                <button type="button" data-action="sort-projects" data-sort-key="name">项目名称${renderSortMark("name")}</button>
                <button type="button" data-action="sort-projects" data-sort-key="path">文件路径${renderSortMark("path")}</button>
                <button type="button" data-action="sort-projects" data-sort-key="modified">修改日期${renderSortMark("modified")}</button>
                <span>操作</span>
              </div>
              <div class="project-table-body">
                ${projects.length === 0 ? `<div class="empty-row">暂无历史项目。</div>` : projects.map((item) => `
                  <div class="project-row"
                       role="button"
                       tabindex="0"
                       data-action="open-project"
                       data-project-path="${escapeAttr(item.path)}"
                       title="打开项目">
                    <span>${escapeText(item.displayName || getFileName(item.path))}</span>
                    <span>${escapeText(item.path)}</span>
                    <span>${escapeText(formatProjectTime(item))}</span>
                    <span class="project-open-cell">打开</span>
                  </div>
                `).join("")}
              </div>
            </div>
          </main>
        </div>
      </section>
    </div>
  `;
}

function renderTitleBar() {
  const undoRedo = state.activeSceneState?.undoRedo ?? {};
  const projectBusy = isActiveProjectBusy();
  const applicationTitle = state.activeProductState?.productId === "icax.tube-one"
    ? "TubeOne"
    : "iCAX";
  return `
    <header class="title-bar">
      <div class="app-corner">
        <button class="app-button" type="button" data-action="open-start-center">${applicationTitle} ▾</button>
        <button class="quick-button" type="button" data-action="ribbon-command" data-command-id="app.save" ${projectBusy ? "disabled" : ""} title="保存">保存</button>
        <button class="quick-button" type="button" data-action="undo-project" ${undoRedo.canUndo && !projectBusy ? "" : "disabled"} title="撤销">撤销</button>
        <button class="quick-button" type="button" data-action="redo-project" ${undoRedo.canRedo && !projectBusy ? "" : "disabled"} title="重做">重做</button>
      </div>
      ${renderProjectSwitcher()}
      ${renderWindowControls()}
    </header>
  `;
}

function renderStartupTitleBar() {
  return `
    <header class="title-bar start-title-bar">
      <div class="app-corner">
        <button class="app-button" type="button" disabled>iCAX</button>
      </div>
      <div class="startup-title">选择产品与项目</div>
      ${renderWindowControls()}
    </header>
  `;
}

function renderWindowControls() {
  return `
    <div class="window-controls" data-no-window-drag>
      <button class="window-control" type="button" data-action="window-minimize" title="最小化">-</button>
      <button class="window-control" type="button" data-action="window-maximize" title="最大化">□</button>
      <button class="window-control close" type="button" data-action="window-close" title="关闭">×</button>
    </div>
  `;
}

function renderProjectSwitcher() {
  const projects = getOpenProjects();
  const maxStart = Math.max(0, projects.length - 3);
  state.projectWindowStart = Math.min(state.projectWindowStart, maxStart);
  const visibleProjects = projects.slice(state.projectWindowStart, state.projectWindowStart + 3);
  return `
    <div class="project-switcher">
      <button class="project-arrow" type="button" data-action="project-window-prev" ${state.projectWindowStart > 0 ? "" : "disabled"}>‹</button>
      <div class="project-tabs">
        ${visibleProjects.length === 0 ? `<span class="project-tab empty">未打开项目</span>` : visibleProjects.map((item) => `
          <button class="project-tab ${item.projectId === state.activeProjectState?.projectId ? "active" : ""}"
                  type="button"
                  data-action="select-open-project"
                  data-project-id="${escapeAttr(item.projectId)}">
            ${escapeText(item.projectName || item.projectPath || item.projectId)}
          </button>
        `).join("")}
      </div>
      <button class="project-arrow" type="button" data-action="project-window-next" ${state.projectWindowStart < maxStart ? "" : "disabled"}>›</button>
    </div>
  `;
}

function renderProductRibbon() {
  const ribbon = getActiveRibbonDefinition();
  ensureActiveRibbonTab(ribbon);
  const activeTab = ribbon.tabs.find((tab) => tab.id === state.activeRibbonTabId) ?? ribbon.tabs[0];
  return `
    <nav class="product-ribbon">
      <div class="ribbon-tabs">
        ${ribbon.tabs.map((tab) => `
          <button class="ribbon-tab ${activeTab?.id === tab.id ? "active" : ""}"
                  type="button"
                  data-action="select-ribbon-tab"
                  data-tab-id="${escapeAttr(tab.id)}">
            ${escapeText(tab.title)}
          </button>
        `).join("")}
      </div>
      <div class="ribbon-commands">
        ${(activeTab?.groups ?? []).map((group) => `
          <section class="ribbon-group" data-ribbon-scope="${escapeAttr(group.scope ?? "all")}">
            <div class="ribbon-command-list">
              ${group.commands.map(renderRibbonCommand).join("")}
            </div>
            <div class="ribbon-group-title">${escapeText(group.title)}</div>
          </section>
        `).join("")}
      </div>
    </nav>
  `;
}

function renderRibbonCommand(command) {
  const button = `
    <button class="ribbon-command ${command.size === "large" ? "large" : ""} ${command.menuItems.length ? "split-main" : ""}"
            type="button"
            data-action="ribbon-command"
            data-command-id="${escapeAttr(command.id)}"
            data-icon-tone="${escapeAttr(command.iconTone ?? "teal")}"
            ${command.disabled ? "disabled" : ""}>
      ${renderRibbonCommandIcon(command.iconName, command.icon)}
      <span>${escapeText(command.title)}</span>
    </button>`;
  if (!command.menuItems.length) return button;

  const open = state.openRibbonMenuCommandId === command.id;
  return `
    <div class="ribbon-split-command ${command.size === "large" ? "large" : ""}" data-ribbon-split-command="${escapeAttr(command.id)}">
      ${button}
      <button class="ribbon-command-menu-toggle"
              type="button"
              data-action="ribbon-command-menu-toggle"
              data-command-id="${escapeAttr(command.id)}"
              aria-label="展开${escapeAttr(command.title)}菜单"
              aria-haspopup="menu"
              aria-expanded="${open ? "true" : "false"}"
              ${command.disabled ? "disabled" : ""}>
        <span aria-hidden="true">⌄</span>
      </button>
      ${open ? `<div class="ribbon-command-menu" role="menu" aria-label="${escapeAttr(command.title)}菜单">
        ${command.menuItems.map((item) => `
          <button class="ribbon-command-menu-item"
                  type="button"
                  role="menuitem"
                  data-action="ribbon-command"
                  data-command-id="${escapeAttr(item.id)}"
                  ${item.disabled ? "disabled" : ""}>
            ${renderRibbonCommandIcon(item.iconName, item.icon)}
            <span>${escapeText(item.title)}</span>
          </button>
        `).join("")}
      </div>` : ""}
    </div>`;
}

function closeRibbonCommandMenu() {
  if (!state.openRibbonMenuCommandId) return;
  state.openRibbonMenuCommandId = "";
  root.querySelectorAll(".ribbon-command-menu").forEach((menu) => menu.remove());
  root.querySelectorAll(".ribbon-command-menu-toggle[aria-expanded='true']")
    .forEach((toggle) => toggle.setAttribute("aria-expanded", "false"));
}

function renderRibbonCommandIcon(iconName, fallback = "□") {
  const body = RIBBON_ICON_SHAPES[String(iconName ?? "")];
  if (!body) {
    return `<span class="command-icon">${escapeText(fallback)}</span>`;
  }
  return `<span class="command-icon vector" aria-hidden="true">
    <svg viewBox="0 0 24 24" focusable="false">${body}</svg>
  </span>`;
}

const RIBBON_ICON_SHAPES = Object.freeze({
  new: `<path d="M5 3.5h9l5 5V21H5z"/><path d="M14 3.5V9h5"/><path class="accent" d="M8 14h8M12 10v8"/>`,
  "add-instance": `<rect x="3" y="4" width="13" height="16" rx="1"/><path d="M6 8h7M6 12h5"/><circle class="accent" cx="17.5" cy="16.5" r="4.5"/><path class="accent" d="M17.5 14v5M15 16.5h5"/>`,
  excel: `<rect x="3" y="3" width="18" height="18" rx="1"/><path d="M9 3v18M9 8h12M9 13h12M15 8v13"/><path class="accent" d="m4.8 8.5 3 7m0-7-3 7"/>`,
  machine: `<path d="M4 20V5h16v15M3 20h18M7 17V8h10v9zM12 5v3"/><path class="accent" d="M12 10v3m-3 3 3-3 3 3"/>`,
  nest: `<path d="M3 5h8v6H3zM13 13h8v6h-8z"/><path class="accent" d="M13 5h8v6h-8zM3 13h8v6H3z"/>`,
  export: `<path d="M5 3.5h9l5 5V21H5z"/><path d="M14 3.5V9h5"/><path class="accent" d="M12 17V8m-3 3 3-3 3 3"/>`,
  report: `<path d="M6 4h12v17H6z"/><path class="accent" d="M9 8h6M9 12h6M9 16h4"/><path d="M9 2.5h6V6H9z"/>`,
  select: `<path d="m5 3 12 9-6 1.5L8 19z"/><path class="accent" d="m12 14 4 6"/>`,
  display: `<rect x="3" y="4" width="18" height="16" rx="1"/><path d="M3 9h18M9 9v11"/><circle class="accent fill" cx="6" cy="6.5" r="1"/>`,
  repair: `<path d="M4 7c3-3 7-3 10 0l2 2"/><path class="accent" d="m13 9 3 .2.2-3"/><path d="M20 17c-3 3-7 3-10 0l-2-2"/><path class="accent" d="m11 15-3-.2-.2 3"/>`,
  edit2d: `<path d="M3 18 8 8l5 5 4-8 4 3"/><circle class="accent fill" cx="8" cy="8" r="1.5"/><path class="accent" d="m13 19 6-6 2 2-6 6-3 .5z"/>`,
  edit3d: `<path d="m4 8 8-4 8 4-8 4zM4 8v8l8 4 8-4V8M12 12v8"/><path class="accent" d="m14 16 5-5 2 2-5 5-3 1z"/>`,
  process: `<circle cx="12" cy="12" r="9"/><path class="accent" d="m7.5 12 3 3 6-7"/>`,
  hole: `<ellipse cx="12" cy="6" rx="7" ry="3"/><path d="M5 6v10c0 2 3 3 7 3s7-1 7-3V6"/><ellipse class="accent" cx="12" cy="11" rx="3" ry="1.5"/>`,
  micro: `<path d="M3 14c4-7 7-7 10 0s5 5 8-1"/><path class="accent" d="M10 13h4M12 11v4"/>`,
  weld: `<path d="M4 5v14M20 5v14M8 5c3 4 3 10 0 14M16 5c-3 4-3 10 0 14"/><path class="accent" d="M10 12h4"/>`,
  manual: `<rect x="3" y="4" width="7" height="6"/><rect x="13" y="14" width="8" height="6"/><path class="accent" d="m8 18 4-8 2 5"/>`,
  merge: `<path d="m3 8 7-4 7 4-7 4zM3 8v7l7 4 3-2"/><path class="accent" d="m11 12 5-3 5 3-5 3zM11 12v6l5 3 5-3v-6"/>`,
  autonest: `<rect x="3" y="4" width="7" height="6"/><rect x="14" y="4" width="7" height="6"/><rect x="3" y="14" width="7" height="6"/><path class="accent" d="M14 18h7m-3-3 3 3-3 3"/>`,
  sort: `<path d="M5 5h11M5 10h8M5 15h5"/><path class="accent" d="M18 8v11m-3-3 3 3 3-3"/>`,
  simulate: `<circle cx="12" cy="12" r="9"/><path class="accent fill" d="m10 8 6 4-6 4z"/>`,
  collision: `<path d="M12 3 20 6v6c0 5-3 8-8 10-5-2-8-5-8-10V6z"/><path class="accent" d="m8 12 2.5 2.5L16 9"/>`,
  measure: `<path d="m4 17 13-13 3 3L7 20z"/><path class="accent" d="m9 15 2 2m1-5 2 2m1-5 2 2"/>`,
  optimize: `<path d="M4 19V5m0 14h15"/><path class="accent" d="m7 15 4-5 3 2 5-7"/>`,
  support: `<circle cx="12" cy="12" r="9"/><path class="accent" d="M12 11v6"/><circle class="accent fill" cx="12" cy="7.5" r="1"/>`,
  delete: `<path d="M5 7h14M9 7V4h6v3M7 7l1 14h8l1-14"/><path class="accent" d="M10 10v7m4-7v7"/>`,
  move: `<path d="M12 3v18M3 12h18"/><path class="accent" d="m9 6 3-3 3 3m-6 12 3 3 3-3M6 9l-3 3 3 3m12-6 3 3-3 3"/>`,
  boolean: `<circle cx="9" cy="12" r="6"/><circle class="accent" cx="15" cy="12" r="6"/>`,
  base: `<ellipse cx="12" cy="5.5" rx="7" ry="3"/><path d="M5 5.5v13c0 2 3 3 7 3s7-1 7-3v-13"/><path class="accent" d="M5 15.5c0 2 3 3 7 3s7-1 7-3"/>`,
  branch: `<path d="M4 5h6v14H4z"/><path class="accent" d="M10 9h10v6H10z"/><ellipse cx="20" cy="12" rx="2" ry="3"/>`,
  bevel: `<path d="M4 5h16l-6 14h-4z"/><path class="accent" d="m8 5 4 8 4-8"/>`,
  cut: `<path d="M4 5h16v14H4z"/><path class="accent" d="m5 18 14-12"/>`,
  mark: `<path d="M5 5h14M12 5v14"/><path class="accent" d="M7 19h10"/>`,
  apply: `<path d="M4 4h16v16H4z"/><path class="accent" d="m7 12 3 3 7-7"/>`,
});

function renderProjectWorkspace() {
  return `
    <main class="workspace">
      <section class="project-surface" data-product-surface="project"></section>
    </main>
  `;
}

function renderBottomDock() {
  return `
    <footer class="bottom-dock">
      <div class="bottom-resize-handle"
           data-bottom-dock-resize
           data-no-window-drag
           title="拖拽调整日志区高度"></div>
      <div class="bottom-title">日志</div>
      <div class="log-list">${renderLogRows()}</div>
    </footer>
  `;
}

function renderLogRows() {
  return `
    ${state.error ? `<div class="log-row error"><span>错误</span><span>${escapeText(state.error)}</span></div>` : ""}
    ${state.logs.length === 0 ? `<span class="muted">暂无活动。</span>` : state.logs.map((entry) => `
      <div class="log-row ${escapeAttr(entry.level)}">
        <span>${escapeText(entry.time)}</span>
        <span>${escapeText(entry.message)}</span>
      </div>
    `).join("")}
  `;
}

function updateBottomDockLogs() {
  const logList = root.querySelector(".log-list");
  if (logList) {
    logList.innerHTML = renderLogRows();
  }
}

function beginBottomDockResize(event) {
  if (!state.activeProjectState) {
    return;
  }

  event.preventDefault();
  state.bottomDockResize = {
    startY: event.clientY,
    startHeight: state.bottomDockHeight,
  };
  document.body.classList.add("resizing-bottom-dock");
  window.addEventListener("mousemove", handleBottomDockResize);
  window.addEventListener("mouseup", endBottomDockResize, { once: true });
}

function handleBottomDockResize(event) {
  const resize = state.bottomDockResize;
  if (!resize) {
    return;
  }

  const delta = resize.startY - event.clientY;
  const maxHeight = Math.max(96, Math.min(360, window.innerHeight - 220));
  state.bottomDockHeight = Math.round(Math.max(48, Math.min(maxHeight, resize.startHeight + delta)));
  root.querySelector(".workbench")?.style.setProperty("--bottom-dock-height", `${state.bottomDockHeight}px`);
}

function endBottomDockResize() {
  window.removeEventListener("mousemove", handleBottomDockResize);
  document.body.classList.remove("resizing-bottom-dock");
  state.bottomDockResize = null;
}

function renderGlobalProgress() {
  if (state.pendingCount <= 0) {
    return "";
  }

  const operation = state.pendingOperations.at(-1);
  return `
    <div class="global-progress-backdrop" aria-live="polite" aria-modal="true" role="dialog">
      <section class="global-progress-dialog">
        <div class="progress-gauge" aria-hidden="true">
          <div class="progress-gauge-face">
            ${Array.from({ length: 28 }, (_, index) => `<span style="--tick:${index}"></span>`).join("")}
            <div class="progress-gauge-sweep"></div>
            <div class="progress-gauge-needle"></div>
            <div class="progress-gauge-core">
              <strong>RUN</strong>
              <small>${escapeText(operation?.mode ?? getProgressMode(operation?.label))}</small>
            </div>
          </div>
        </div>
        <div class="global-progress-copy">
          <strong>${escapeText(operation?.title ?? getProgressTitle(operation?.label))}</strong>
          <span>${escapeText(operation?.detail ?? getProgressDetail(operation?.label))}</span>
          <div class="global-progress-stage">
            <span>${escapeText(operation?.stage ?? getProgressStage(operation?.label))}</span>
            <span>${escapeText(operation?.mode ?? getProgressMode(operation?.label))}</span>
          </div>
        </div>
      </section>
    </div>
  `;
}

function renderProjectProgress() {
  const operation = getActiveProjectOperation();
  if (!operation) {
    return "";
  }

  return `
    <div class="project-progress-backdrop" aria-live="polite" aria-modal="true" role="dialog">
      <section class="global-progress-dialog project-progress-dialog">
        <div class="progress-gauge" aria-hidden="true">
          <div class="progress-gauge-face">
            ${Array.from({ length: 28 }, (_, index) => `<span style="--tick:${index}"></span>`).join("")}
            <div class="progress-gauge-sweep"></div>
            <div class="progress-gauge-needle"></div>
            <div class="progress-gauge-core">
              <strong>RUN</strong>
              <small>${escapeText(operation.mode ?? "PROJECT")}</small>
            </div>
          </div>
        </div>
        <div class="global-progress-copy">
          <strong>${escapeText(operation.title ?? "项目任务")}</strong>
          <span>${escapeText(operation.detail ?? "正在等待后台项目任务完成")}</span>
          <div class="global-progress-stage">
            <span>${escapeText(operation.stage ?? "项目执行")}</span>
            <span>${escapeText(operation.mode ?? "PROJECT")}</span>
          </div>
        </div>
      </section>
    </div>
  `;
}

function renderNewProjectDialog() {
  const product = getSelectedProductState();
  return `
    <div class="modal-backdrop">
      <section class="new-project-dialog">
        <header>
          <strong>新建项目</strong>
          <button class="icon-button" type="button" data-action="close-new-project-dialog">×</button>
        </header>
        <div class="dialog-product">${escapeText(product?.productName ?? "")}</div>
        ${state.newProjectError ? `<div class="dialog-error">${escapeText(state.newProjectError)}</div>` : ""}
        <label>
          <span>项目名称</span>
          <input data-field="newProjectName" type="text" value="${escapeAttr(state.newProjectName)}" />
        </label>
        <label>
          <span>项目路径</span>
          <input data-field="newProjectPath" type="text" value="${escapeAttr(state.newProjectPath)}" placeholder="D:/projects/demo.i3cam" />
        </label>
        <footer>
          <button class="tool-button" type="button" data-action="close-new-project-dialog">取消</button>
          <button class="primary-button" type="button" data-action="create-project">创建</button>
        </footer>
      </section>
    </div>
  `;
}

function renderSortMark(key) {
  const [activeKey, direction] = parseProjectSortMode();
  if (activeKey !== key) {
    return "";
  }
  return direction === "asc" ? " ↑" : " ↓";
}

async function enterProject(response) {
  if (isApplicationState(response.state)) {
    state.appState = response.state;
  }

  let productProxy = response.productProxy ?? (response.product?.productId ? state.appProxy.getProduct(response.product.productId) : null);
  if (!productProxy && response.product?.productId && response.product?.isStarted === true) {
    productProxy = await state.appProxy.adoptProduct(response.product);
  }

  let catalog = response.catalog ?? findCatalogFromProductState(response.state, response.projectPath);
  if (!catalog && productProxy?.state) {
    catalog = findCatalogFromProductState(productProxy.state, response.projectPath);
  }

  let projectProxy = response.projectProxy ?? null;
  if (!projectProxy && catalog?.mainProject && productProxy) {
    projectProxy = await productProxy.adoptProject(catalog.mainProject);
  }

  if (!projectProxy && productProxy) {
    const productState = await productProxy.getState();
    catalog = catalog ?? findCatalogFromProductState(productState, response.projectPath);
    const mainProject = catalog?.mainProject ?? findCatalogFromProductState(productState, response.projectPath)?.mainProject ?? null;
    if (mainProject) {
      projectProxy = await productProxy.adoptProject(mainProject);
      catalog = catalog ?? productState.catalogs?.find((item) => item?.mainProject?.projectId === mainProject.projectId) ?? null;
    }
  }

  const projectState = projectProxy?.state ?? catalog?.mainProject ?? null;
  if (!projectState?.projectId) {
    throw new Error("项目已经返回，但没有可进入的主项目。");
  }

  state.activeProductProxy = productProxy;
  state.activeProductState = productProxy?.state ?? response.product ?? state.activeProductState;
  state.selectedProductId = state.activeProductState?.productId ?? state.selectedProductId;
  state.activeProjectProxy = projectProxy;
  state.activeProjectState = projectState;
  state.activeSceneProxy = response.sceneProxy ?? projectProxy?.getMainScene?.() ?? null;
  state.activeSceneState = state.activeSceneProxy?.state ?? state.activeProjectState?.mainScene ?? null;
  state.appState = mergeProductIntoApplicationState(state.appState, state.activeProductState);
  state.activeRibbonTabId = "";
  state.newProjectDialogOpen = false;
  state.startCenterOpen = false;
  await mountActiveProductModule();
  ensureActiveRibbonTab();
}

function findCatalogFromProductState(productState, projectPath = "") {
  const catalogs = productState?.products
    ?.flatMap((product) => product?.session?.catalogs ?? product?.catalogs ?? [])
    ?? productState?.catalogs
    ?? [];
  if (catalogs.length === 0) {
    return null;
  }

  const normalizedPath = normalizePath(projectPath);
  if (normalizedPath) {
    const matched = catalogs.find((catalog) =>
      normalizePath(catalog?.catalogPath) === normalizedPath
      || normalizePath(catalog?.mainProject?.projectPath) === normalizedPath);
    if (matched) {
      return matched;
    }
  }
  return catalogs[0] ?? null;
}

function isApplicationState(value) {
  return Array.isArray(value?.products);
}

function mergeProductIntoApplicationState(appState, productState) {
  if (!appState || !productState?.productId || !Array.isArray(appState.products)) {
    return appState;
  }

  let replaced = false;
  const products = appState.products.map((product) => {
    if (product?.productId !== productState.productId) {
      return product;
    }
    replaced = true;
    return {
      ...product,
      ...productState,
      session: productState,
    };
  });

  return {
    ...appState,
    products: replaced ? products : [...products, productState],
  };
}

async function mountSelectedProductModule() {
  const product = getSelectedProductState();
  if (!product?.frontendEntry) {
    return;
  }
  await loadProductModule(product, state.productModules, { baseUrl: import.meta.url });
}

async function mountActiveProductModule() {
  const product = state.activeProductState;
  if (!product?.frontendEntry) {
    return;
  }
  await loadProductModule(product, state.productModules, { baseUrl: import.meta.url });
  state.productSurfaceErrorKey = "";
}

function mountActiveProductSurface() {
  const product = state.activeProductState;
  const module = getActiveProductModule();
  const mount = root.querySelector("[data-product-surface='project']");
  if (!product || !module || !mount) {
    return Promise.resolve();
  }

  const mountKey = `${product.productId}:project:${state.activeRibbonTabId}`;
  if (state.productSurfaceErrorKey === mountKey) {
    return Promise.resolve();
  }

  const sequence = ++state.productSurfaceMountSequence;
  const operation = Promise.resolve(mountProductModule(module, buildProductContext(mount)))
    .catch((error) => {
      if (sequence !== state.productSurfaceMountSequence) {
        return false;
      }
      state.productSurfaceErrorKey = mountKey;
      state.error = error?.message ?? String(error);
      render();
      return false;
    });
  state.productSurfaceMountPromise = operation;
  return operation;
}

function buildProductContext(mount = root.querySelector("[data-product-surface='project']")) {
  return {
    product: state.activeProductState,
    appProxy: state.appProxy,
    productProxy: state.activeProductProxy,
    projectProxy: state.activeProjectProxy,
    sceneProxy: state.activeSceneProxy,
    project: state.activeProjectState,
    scene: state.activeSceneState,
    mount,
    mode: "project",
    activeRibbonTabId: state.activeRibbonTabId,
    actions,
  };
}

function getActiveProductModule() {
  return state.activeProductState?.productId
    ? state.productModules.get(state.activeProductState.productId)
    : null;
}

function getActiveWindowCloseGuard() {
  const module = getActiveProductModule();
  if (typeof module?.getWindowCloseGuard !== "function") return null;
  try {
    return module.getWindowCloseGuard(buildProductContext(null)) ?? null;
  } catch (error) {
    pushLog("error", `检查窗口关闭状态失败：${error?.message ?? String(error)}`);
    return null;
  }
}

function getActiveRibbonDefinition() {
  const module = getActiveProductModule();
  if (typeof module?.getRibbonDefinition === "function") {
    return normalizeRibbonDefinition(module.getRibbonDefinition(buildProductContext(null)));
  }
  return { tabs: [] };
}

function normalizeRibbonDefinition(ribbon) {
  const tabs = Array.isArray(ribbon?.tabs) ? ribbon.tabs : [];
  return {
    tabs: tabs.map((tab) => ({
      id: String(tab.id ?? ""),
      title: String(tab.title ?? tab.id ?? ""),
      groups: (Array.isArray(tab.groups) ? tab.groups : []).map((group) => ({
        title: String(group.title ?? ""),
        scope: String(group.scope ?? "all"),
        commands: (Array.isArray(group.commands) ? group.commands : []).map((command) => ({
          id: String(command.id ?? ""),
          title: String(command.title ?? command.id ?? ""),
          icon: command.icon,
          iconName: command.iconName,
          iconTone: command.iconTone,
          size: command.size,
          disabled: Boolean(command.disabled),
          menuItems: (Array.isArray(command.menuItems) ? command.menuItems : []).map((item) => ({
            id: String(item.id ?? ""),
            title: String(item.title ?? item.id ?? ""),
            icon: item.icon,
            iconName: item.iconName,
            disabled: Boolean(item.disabled),
          })).filter((item) => item.id),
        })),
      })),
    })).filter((tab) => tab.id),
  };
}

function ensureActiveRibbonTab(ribbon = getActiveRibbonDefinition()) {
  if (ribbon.tabs.some((tab) => tab.id === state.activeRibbonTabId)) {
    return;
  }
  state.activeRibbonTabId = ribbon.tabs[0]?.id ?? "";
}

function ensureSelectedProduct() {
  const products = getProducts();
  if (products.some((product) => product.productId === state.selectedProductId)) {
    return;
  }
  state.selectedProductId = products[0]?.productId ?? "";
}

function getProducts() {
  return state.appState?.products ?? [];
}

function findProductState(productId) {
  return getProducts().find((product) => product.productId === productId) ?? null;
}

function getSelectedProductState() {
  ensureSelectedProduct();
  return findProductState(state.selectedProductId);
}

function getOpenProjects() {
  const result = [];
  const seen = new Set();
  for (const productProxy of state.appProxy?.products?.values?.() ?? []) {
    for (const projectProxy of productProxy.projects?.values?.() ?? []) {
      const projectState = projectProxy.state;
      if (!projectState?.projectId || seen.has(projectState.projectId)) {
        continue;
      }
      seen.add(projectState.projectId);
      result.push({
        productId: productProxy.productId,
        productProxy,
        projectProxy,
        projectId: projectState.projectId,
        projectName: projectState.projectName,
        projectPath: projectState.projectPath,
        projectState,
      });
    }
  }
  if (state.activeProjectState?.projectId && !seen.has(state.activeProjectState.projectId)) {
    result.unshift({
      productId: state.activeProductState?.productId,
      productProxy: state.activeProductProxy,
      projectProxy: state.activeProjectProxy,
      projectId: state.activeProjectState.projectId,
      projectName: state.activeProjectState.projectName,
      projectPath: state.activeProjectState.projectPath,
      projectState: state.activeProjectState,
    });
  }
  return result;
}

function getActiveProjectOperation() {
  const projectId = state.activeProjectState?.projectId;
  if (!projectId) {
    return null;
  }
  const operations = state.projectOperations.get(String(projectId)) ?? [];
  return operations.at(-1) ?? null;
}

function isActiveProjectBusy() {
  return Boolean(getActiveProjectOperation());
}

function filterRecentProjects(projects) {
  const query = state.projectSearchText.trim().toLocaleLowerCase();
  const filtered = projects.filter((item) => {
    if (!query) {
      return true;
    }
    return `${item.displayName ?? ""} ${item.path ?? ""}`.toLocaleLowerCase().includes(query);
  });

  return filtered.sort((left, right) => {
    const [key, direction] = parseProjectSortMode();
    const sign = direction === "asc" ? 1 : -1;
    if (key === "modified") {
      return sign * (getProjectTime(left) - getProjectTime(right));
    }
    if (key === "name") {
      return sign * String(left.displayName ?? left.path ?? "").localeCompare(String(right.displayName ?? right.path ?? ""));
    }
    if (key === "path") {
      return sign * String(left.path ?? "").localeCompare(String(right.path ?? ""));
    }
    return getProjectTime(right) - getProjectTime(left);
  });
}

function resolveNewProjectPath(path, projectName, product) {
  const extensions = getProjectFileExtensions(product);
  const extension = extensions[0] ?? ".icax";
  let text = String(path ?? "").trim();
  if (!text) {
    text = makeDefaultProjectPath(projectName, product);
  }

  text = appendDefaultFileNameIfDirectory(text, projectName, extension);

  const fileName = getFileName(text);
  if (!fileName) {
    return { projectPath: "", error: "项目路径必须包含文件名。" };
  }

  if (extensions.length === 0) {
    return { projectPath: text, error: "" };
  }

  const lower = text.toLocaleLowerCase();
  if (extensions.some((item) => lower.endsWith(item))) {
    return { projectPath: text, error: "" };
  }

  if (!hasFileExtension(fileName)) {
    return { projectPath: `${text}${extension}`, error: "" };
  }

  return { projectPath: "", error: `项目文件扩展名必须是 ${extensions.join("、")}。` };
}

function makeDefaultProjectPath(projectName, product) {
  const extensions = getProjectFileExtensions(product);
  const extension = extensions[0] ?? ".icax";
  return `${makeSafeProjectFileName(projectName)}${extension}`;
}

function appendDefaultFileNameIfDirectory(path, projectName, extension) {
  const text = String(path ?? "").trim();
  if (!text) {
    return text;
  }

  const safeName = makeSafeProjectFileName(projectName);
  if (/^[A-Za-z]:$/.test(text)) {
    return `${text}\\${safeName}${extension}`;
  }
  if (/[\\/]$/.test(text)) {
    return `${text}${safeName}${extension}`;
  }
  return text;
}

function hasFileExtension(fileName) {
  const normalized = String(fileName ?? "");
  const dot = normalized.lastIndexOf(".");
  return dot > 0 && dot < normalized.length - 1;
}

function makeSafeProjectFileName(projectName) {
  const text = String(projectName ?? "").trim() || "iCAX项目";
  return text.replace(/[<>:"/\\|?*\x00-\x1F]/g, "_").replace(/\s+/g, " ").slice(0, 80) || "iCAX项目";
}

function getProjectFileExtensions(product) {
  return (Array.isArray(product?.projectFile?.fileExtensions) ? product.projectFile.fileExtensions : [])
    .map((extension) => String(extension ?? "").trim().toLocaleLowerCase())
    .filter(Boolean)
    .map((extension) => extension.startsWith(".") ? extension : `.${extension}`);
}

function nextProjectSortMode(key) {
  const currentKey = String(key ?? "");
  const [activeKey, direction] = parseProjectSortMode();
  if (activeKey === currentKey) {
    return `${currentKey}-${direction === "asc" ? "desc" : "asc"}`;
  }
  return `${currentKey}-${currentKey === "modified" ? "desc" : "asc"}`;
}

function parseProjectSortMode() {
  const [key, direction] = String(state.projectSortMode || "modified-desc").split("-");
  if (!["name", "path", "modified"].includes(key)) {
    return ["modified", "desc"];
  }
  return [key, direction === "asc" ? "asc" : "desc"];
}

function getProjectTime(item) {
  const text = item.modifiedTime ?? item.lastModifiedTime ?? item.lastOpenedTime ?? "";
  const time = Date.parse(text);
  return Number.isFinite(time) ? time : 0;
}

function formatProjectTime(item) {
  const text = item.modifiedTime ?? item.lastModifiedTime ?? item.lastOpenedTime ?? "";
  if (!text) {
    return "-";
  }
  const date = new Date(text);
  if (Number.isNaN(date.getTime())) {
    return text;
  }
  return date.toLocaleDateString();
}

function getFileName(path) {
  return String(path ?? "").split(/[\\/]/).filter(Boolean).pop() ?? "";
}

function normalizePath(path) {
  return String(path ?? "").replace(/\\/g, "/").replace(/\/+$/g, "").toLocaleLowerCase();
}

function runAction(action) {
  Promise.resolve()
    .then(action)
    .catch((error) => {
      state.error = error?.message ?? String(error);
      pushLog("error", state.error);
      render();
    });
}

async function track(label, work, progress = {}) {
  const operation = createProgressOperation(label, progress);
  const stopProgressTimer = startProgressStageTimer(operation);
  state.pendingCount += 1;
  state.pendingOperations.push(operation);
  state.error = "";
  render();
  await waitForPaint();
  try {
    const result = await work();
    pushLog("ok", label);
    return result;
  } catch (error) {
    state.error = error?.message ?? String(error);
    pushLog("error", `${label}: ${state.error}`);
    throw error;
  } finally {
    await waitForMinimumProgressDuration(operation);
    stopProgressTimer();
    state.pendingCount = Math.max(0, state.pendingCount - 1);
    state.pendingOperations = state.pendingOperations.filter((item) => item.id !== operation.id);
    render();
  }
}

function waitForPaint() {
  return new Promise((resolve) => {
    window.requestAnimationFrame(() => window.requestAnimationFrame(resolve));
  });
}

function waitForMinimumProgressDuration(operation) {
  const minimumVisibleMs = Number(operation?.minimumVisibleMs ?? 0);
  if (!Number.isFinite(minimumVisibleMs) || minimumVisibleMs <= 0) {
    return Promise.resolve();
  }

  const startedAt = Number(operation?.startedAt ?? performance.now());
  const elapsedMs = performance.now() - startedAt;
  const remainingMs = minimumVisibleMs - elapsedMs;
  if (remainingMs <= 0) {
    return Promise.resolve();
  }
  return new Promise((resolve) => window.setTimeout(resolve, remainingMs));
}

function createProgressOperation(label, progress = {}) {
  const minimumVisibleMs = Number(progress.minimumVisibleMs ?? progress.minVisibleMs ?? 0);
  return {
    id: `${Date.now()}:${Math.random()}`,
    startedAt: performance.now(),
    label,
    title: progress.title ?? getProgressTitle(label),
    detail: progress.detail ?? getProgressDetail(label),
    stage: progress.stage ?? getProgressStage(label),
    mode: progress.mode ?? getProgressMode(label),
    steps: Array.isArray(progress.steps) ? progress.steps : [],
    minimumVisibleMs: Number.isFinite(minimumVisibleMs) ? Math.max(0, minimumVisibleMs) : 0,
  };
}

function startProgressStageTimer(operation) {
  const steps = operation.steps;
  if (!steps.length) {
    return () => undefined;
  }

  let index = 0;
  applyProgressStep(operation, steps[index]);
  const timer = window.setInterval(() => {
    index = Math.min(index + 1, steps.length - 1);
    applyProgressStep(operation, steps[index]);
    render();
  }, 1400);
  return () => window.clearInterval(timer);
}

function applyProgressStep(operation, step) {
  if (Array.isArray(step)) {
    operation.stage = step[0] ?? operation.stage;
    operation.detail = step[1] ?? operation.detail;
    return;
  }
  if (step && typeof step === "object") {
    operation.stage = step.stage ?? operation.stage;
    operation.detail = step.detail ?? operation.detail;
    operation.mode = step.mode ?? operation.mode;
  }
}

function getProgressTitle(label) {
  const text = String(label ?? "");
  if (text.includes("OpenProjectCatalog")) {
    return "打开项目";
  }
  if (text.includes("OpenProjectFile")) {
    return "加载项目";
  }
  if (text.includes("GetState")) {
    return "同步状态";
  }
  if (text.includes("Undo")) {
    return "撤销";
  }
  if (text.includes("Redo")) {
    return "重做";
  }
  return text || "处理中";
}

function getProgressDetail(label) {
  const text = String(label ?? "");
  if (text.includes("OpenProjectCatalog") || text.includes("OpenProjectFile")) {
    return "正在连接后端、创建场景并同步项目状态";
  }
  if (text.includes("GetState")) {
    return "正在读取 Application/Product/Project 状态";
  }
  return "正在等待后台任务完成";
}

function getProgressStage(label) {
  const text = String(label ?? "");
  if (text.includes("OpenProjectCatalog") || text.includes("OpenProjectFile")) {
    return "项目装载";
  }
  if (text.includes("GetState")) {
    return "状态同步";
  }
  if (text.includes("Undo") || text.includes("Redo")) {
    return "历史回放";
  }
  return "后台执行";
}

function getProgressMode(label) {
  const text = String(label ?? "");
  if (text.includes("Cam.")) {
    return "CAM";
  }
  if (text.includes("Project.")) {
    return "PROJECT";
  }
  if (text.includes("Product.")) {
    return "PRODUCT";
  }
  return "ICAX";
}

function pushLog(level, message) {
  state.logs.unshift({ level, time: new Date().toLocaleTimeString(), message });
  state.logs = state.logs.slice(0, 80);
}

actions.bootstrap().catch((error) => {
  state.error = error?.message ?? String(error);
  render();
});
