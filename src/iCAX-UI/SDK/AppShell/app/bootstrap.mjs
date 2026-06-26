import {
  connectApplication,
  escapeAttr,
  escapeText,
  loadProductModule,
  mountProductModule,
} from "../../index.mjs";

const root = document.getElementById("app");

const state = {
  bridgeStatus: "Disconnected",
  bridge: null,
  appState: null,
  appProxy: null,
  activeProductState: null,
  activeProductProxy: null,
  activeProjectState: null,
  activeProjectProxy: null,
  productModules: new Map(),
  productSurfaceErrorKey: "",
  pendingCount: 0,
  logs: [],
  error: "",
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
      const activeProductId = state.activeProductState?.productId;
      state.activeProductState = activeProductId
        ? state.appState.products?.find((product) => product.productId === activeProductId) ?? null
        : state.activeProductState;
      if (!state.activeProductState) {
        state.activeProductState = state.appState.products?.find((product) => product.isStarted)
          ?? state.appState.products?.[0]
          ?? null;
      }
      state.activeProductProxy = state.activeProductState?.productId
        ? state.appProxy.getProduct(state.activeProductState.productId)
        : null;
      await mountActiveProductModule();
    });
  },

  async selectProduct(productId) {
    state.activeProductState = state.appState?.products?.find((product) => product.productId === productId) ?? null;
    state.activeProductProxy = state.activeProductState?.productId
      ? state.appProxy.getProduct(state.activeProductState.productId)
      : null;
    state.activeProjectState = null;
    state.activeProjectProxy = null;
    await mountActiveProductModule();
    render();
  },

  async startProduct(productId) {
    await track("App.StartProduct", async () => {
      const product = await state.appProxy.startProduct(productId);
      state.activeProductProxy = product;
      state.activeProductState = product.state;
      await actions.refresh();
      await mountActiveProductModule();
    });
  },

  async openProjectFromPath(projectPath) {
    const path = String(projectPath ?? "").trim();
    if (!path) {
      state.error = "项目路径不能为空。";
      render();
      return;
    }

    await track("App.OpenProjectFile", async () => {
      const response = await state.appProxy.openProjectFile(path);
      state.appState = response.state;
      state.activeProductProxy = response.productProxy;
      state.activeProductState = response.productProxy?.state ?? response.product;
      state.activeProjectProxy = response.projectProxy;
      state.activeProjectState = response.projectProxy?.state ?? response.catalog?.mainProject ?? null;
      await mountActiveProductModule();
    });
  },

  async chooseProjectFile() {
    const bridge = state.bridge ?? state.appProxy?.bridge ?? null;
    if (typeof bridge?.openFileDialog !== "function") {
      state.error = "当前宿主没有提供文件选择能力。";
      render();
      return;
    }
    const path = await bridge?.openFileDialog?.({
      filters: [{ name: "iCAX Project", extensions: ["icax", "ilcam"] }],
    });
    if (path) {
      await actions.openProjectFromPath(path);
    }
  },

  async undoProject() {
    if (!state.activeProjectProxy) {
      return;
    }

    await track("Project.Undo", async () => {
      const undoRedo = await state.activeProjectProxy.undo();
      const projectState = await state.activeProjectProxy.getState();
      state.activeProjectState = { ...projectState, undoRedo };
    });
  },

  async redoProject() {
    if (!state.activeProjectProxy) {
      return;
    }

    await track("Project.Redo", async () => {
      const undoRedo = await state.activeProjectProxy.redo();
      const projectState = await state.activeProjectProxy.getState();
      state.activeProjectState = { ...projectState, undoRedo };
    });
  },
};

root.addEventListener("click", (event) => {
  const target = event.target instanceof Element ? event.target.closest("[data-action]") : null;
  if (!target) {
    return;
  }

  const action = target.dataset.action;
  if (target.hasAttribute("disabled")) {
    return;
  }

  if (action === "refresh") {
    runAction(() => actions.refresh());
  } else if (action === "select-product") {
    runAction(() => actions.selectProduct(target.dataset.productId));
  } else if (action === "start-product") {
    runAction(() => actions.startProduct(target.dataset.productId));
  } else if (action === "open-project") {
    runAction(() => actions.openProjectFromPath(target.dataset.projectPath ?? root.querySelector("[data-field='projectPath']")?.value));
  } else if (action === "choose-project") {
    runAction(() => actions.chooseProjectFile());
  } else if (action === "undo-project") {
    runAction(() => actions.undoProject());
  } else if (action === "redo-project") {
    runAction(() => actions.redoProject());
  }
});

function render() {
  root.innerHTML = `
    <div class="workbench">
      ${renderTopBar()}
      <div class="workbench-body">
        ${renderProductRail()}
        ${renderWorkspace()}
        ${renderInspector()}
      </div>
      ${renderBottomDock()}
    </div>
  `;

  mountActiveProductSurface();
}

function renderTopBar() {
  const productName = state.activeProductState?.productName ?? "Application";
  const projectName = state.activeProjectState?.projectName ?? "No project";
  return `
    <header class="top-bar">
      <div class="brand">
        <span class="brand-mark">iCAX</span>
        <span class="brand-context">${escapeText(productName)}</span>
      </div>
      <div class="context-path">
        <span>${escapeText(projectName)}</span>
        <span class="muted">${escapeText(state.bridgeStatus)}</span>
      </div>
      <div class="top-actions">
        <button class="tool-button" type="button" data-action="refresh">刷新</button>
        <span class="status-badge">${state.pendingCount > 0 ? "执行中" : "就绪"}</span>
      </div>
    </header>
  `;
}

function renderProductRail() {
  const products = state.appState?.products ?? [];
  return `
    <aside class="product-rail">
      <div class="panel-caption">产品</div>
      <div class="rail-list">
        ${products.map((product) => `
          <button class="rail-item ${state.activeProductState?.productId === product.productId ? "active" : ""}"
                  type="button"
                  data-action="select-product"
                  data-product-id="${escapeAttr(product.productId)}">
            <span>${escapeText(product.productName || product.productId)}</span>
            <small>${product.isStarted ? "运行中" : "未启动"}</small>
          </button>
        `).join("")}
      </div>
    </aside>
  `;
}

function renderWorkspace() {
  if (!state.appState) {
    return `<main class="workspace"><div class="empty-state">正在连接后台。</div></main>`;
  }
  if (!state.activeProductState) {
    return `<main class="workspace"><div class="empty-state">没有可用产品。</div></main>`;
  }
  if (state.activeProjectState) {
    return renderProjectWorkspace();
  }
  return renderProductWorkspace();
}

function renderProductWorkspace() {
  const product = state.activeProductState;
  const recentProjects = product.recentProjects ?? [];
  return `
    <main class="workspace">
      <section class="workspace-head">
        <div>
          <h1>${escapeText(product.productName || product.productId)}</h1>
          <p>${escapeText(product.productVersion || product.productId)}</p>
        </div>
        ${product.isStarted ? "" : `
          <button class="primary-button" type="button" data-action="start-product" data-product-id="${escapeAttr(product.productId)}">启动产品</button>
        `}
      </section>

      <section class="product-surface" data-product-surface="product"></section>

      <section class="open-strip">
        <input data-field="projectPath" type="text" value="${escapeAttr(recentProjects[0]?.path ?? "")}" placeholder="D:/projects/project.icax" />
        <button class="primary-button" type="button" data-action="open-project">打开</button>
        <button class="tool-button" type="button" data-action="choose-project">浏览</button>
      </section>

      <section class="list-section">
        <h2>最近项目</h2>
        ${recentProjects.length === 0 ? `<div class="empty-row">暂无最近项目。</div>` : recentProjects.map((item) => `
          <button class="recent-row" type="button" data-action="open-project" data-project-path="${escapeAttr(item.path)}">
            <span>${escapeText(item.displayName || item.path)}</span>
            <small>${escapeText(item.lastOpenedTime ?? "")}</small>
          </button>
        `).join("")}
      </section>
    </main>
  `;
}

function renderProjectWorkspace() {
  const undoRedo = state.activeProjectState?.undoRedo ?? {};
  return `
    <main class="workspace">
      <section class="workspace-head">
        <div>
          <h1>${escapeText(state.activeProjectState.projectName)}</h1>
          <p>${escapeText(state.activeProjectState.projectPath)}</p>
        </div>
        <div class="command-strip">
          <button class="tool-button" type="button" data-action="undo-project" ${undoRedo.canUndo ? "" : "disabled"}>撤销</button>
          <button class="tool-button" type="button" data-action="redo-project" ${undoRedo.canRedo ? "" : "disabled"}>重做</button>
        </div>
      </section>
      <section class="project-surface" data-product-surface="project"></section>
    </main>
  `;
}

function renderInspector() {
  const product = state.activeProductState;
  const project = state.activeProjectState;
  return `
    <aside class="inspector">
      <div class="panel-caption">属性</div>
      ${project ? `
        <dl class="property-grid">
          <dt>项目</dt><dd>${escapeText(project.projectName)}</dd>
          <dt>状态</dt><dd>${escapeText(project.state)}</dd>
          <dt>通道</dt><dd>${escapeText(project.projectChannelId)}</dd>
          <dt>PDO</dt><dd>${project.pdo?.enabled ? "启用" : "未启用"}</dd>
        </dl>
      ` : product ? `
        <dl class="property-grid">
          <dt>产品</dt><dd>${escapeText(product.productName)}</dd>
          <dt>版本</dt><dd>${escapeText(product.productVersion)}</dd>
          <dt>Magic</dt><dd>${escapeText(product.projectFile?.magic)}</dd>
          <dt>通道</dt><dd>${escapeText(product.productChannelId || "-")}</dd>
        </dl>
      ` : `<div class="empty-row">无活动上下文。</div>`}
    </aside>
  `;
}

function renderBottomDock() {
  return `
    <footer class="bottom-dock">
      <div class="panel-caption">日志</div>
      <div class="log-list">
        ${state.error ? `<div class="log-row error"><span>错误</span><span>${escapeText(state.error)}</span></div>` : ""}
        ${state.logs.length === 0 ? `<span class="muted">暂无活动。</span>` : state.logs.map((entry) => `
          <div class="log-row ${escapeAttr(entry.level)}">
            <span>${escapeText(entry.time)}</span>
            <span>${escapeText(entry.message)}</span>
          </div>
        `).join("")}
      </div>
    </footer>
  `;
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
  if (!product) {
    return;
  }

  const module = state.productModules.get(product.productId);
  const productMount = root.querySelector("[data-product-surface='product']");
  const projectMount = root.querySelector("[data-product-surface='project']");
  if (!module) {
    return;
  }

  const mode = projectMount ? "project" : "product";
  const mountKey = `${product.productId}:${mode}`;
  if (state.productSurfaceErrorKey === mountKey) {
    return;
  }

  const context = {
    product,
    appProxy: state.appProxy,
    productProxy: state.activeProductProxy,
    projectProxy: state.activeProjectProxy,
    project: state.activeProjectState,
    mount: productMount ?? projectMount,
    mode,
    actions,
  };

  Promise.resolve(mountProductModule(module, context)).catch((error) => {
    state.productSurfaceErrorKey = mountKey;
    state.error = error?.message ?? String(error);
    render();
  });
}

function runAction(action) {
  Promise.resolve()
    .then(action)
    .catch((error) => {
      state.error = error?.message ?? String(error);
      state.logs.unshift({ level: "error", time: new Date().toLocaleTimeString(), message: state.error });
      state.logs = state.logs.slice(0, 80);
      render();
    });
}

async function track(label, work) {
  state.pendingCount += 1;
  state.error = "";
  render();
  try {
    const result = await work();
    state.logs.unshift({ level: "ok", time: new Date().toLocaleTimeString(), message: label });
    state.logs = state.logs.slice(0, 80);
    return result;
  } catch (error) {
    state.error = error?.message ?? String(error);
    state.logs.unshift({ level: "error", time: new Date().toLocaleTimeString(), message: `${label}: ${state.error}` });
    state.logs = state.logs.slice(0, 80);
    throw error;
  } finally {
    state.pendingCount = Math.max(0, state.pendingCount - 1);
    render();
  }
}

actions.bootstrap().catch((error) => {
  state.error = error?.message ?? String(error);
  render();
});
