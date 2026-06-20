function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function productStatus(product) {
  return product.isStarted ? "Running" : "Stopped";
}

export function mountWorkbench(root, actions) {
  if (!root) {
    throw new TypeError("root is required");
  }

  let latestState = null;

  root.addEventListener("click", (event) => {
    if (!(event.target instanceof Element)) {
      return;
    }

    const target = event.target.closest("[data-action]");
    if (!target) {
      return;
    }

    const action = target.dataset.action;
    const productId = target.dataset.productId ?? "";
    const projectId = target.dataset.projectId ?? "";

    if (action === "refresh") {
      actions.refresh();
    } else if (action === "start-product") {
      actions.startProduct(productId);
    } else if (action === "activate-product") {
      actions.activateProduct(productId);
    } else if (action === "activate-project") {
      actions.activateProject(projectId);
    } else if (action === "open-file") {
      const input = root.querySelector("[data-field='projectPath']");
      actions.openProjectFile(input?.value ?? "");
    } else if (action === "choose-file") {
      actions.chooseAndOpenProjectFile();
    }
  });

  return {
    render(state) {
      latestState = state;
      root.innerHTML = renderWorkbench(state);
    },
    getState() {
      return latestState;
    },
  };
}

function renderWorkbench(state) {
  return `
    <div class="workbench">
      ${renderAppBar(state)}
      <div class="workbench-main">
        ${renderProductRail(state)}
        ${renderWorkspace(state)}
        ${renderInspector(state)}
      </div>
      ${renderBottomDock(state)}
    </div>
  `;
}

function renderAppBar(state) {
  const activeProduct = state.productsById.get(state.activeProductId);
  const activeProject = state.projectsById.get(state.activeProjectId);
  return `
    <header class="app-bar">
      <div class="brand">
        <div class="brand-mark">iCAX</div>
        <div>
          <div class="app-title">Workbench</div>
          <div class="app-subtitle">${escapeText(activeProduct?.productName ?? "Application")}</div>
        </div>
      </div>
      <div class="context-line">
        <span>${escapeText(activeProject?.projectName ?? "No project")}</span>
        <span class="muted">${escapeText(state.bridgeStatus)}</span>
      </div>
      <div class="app-actions">
        <button class="tool-button" data-action="refresh" type="button">Refresh</button>
        <span class="status-pill">${state.pendingCommandCount > 0 ? "Waiting" : "Idle"}</span>
      </div>
    </header>
  `;
}

function renderProductRail(state) {
  const products = state.application.products;
  return `
    <aside class="product-rail">
      <div class="rail-title">Products</div>
      <div class="rail-list">
        ${products.map((product) => `
          <button class="rail-item ${product.productId === state.activeProductId ? "active" : ""}"
                  data-action="activate-product"
                  data-product-id="${escapeText(product.productId)}"
                  type="button">
            <span class="rail-name">${escapeText(product.productName || product.productId)}</span>
            <span class="rail-state">${productStatus(product)}</span>
          </button>
        `).join("")}
      </div>
    </aside>
  `;
}

function renderWorkspace(state) {
  const activeProduct = state.productsById.get(state.activeProductId);
  if (!activeProduct) {
    return `
      <main class="workspace">
        <section class="workspace-header">
          <h1>Products</h1>
          <p>Select a product to start its workspace.</p>
        </section>
        <div class="product-grid">
          ${state.application.products.map((product) => renderProductCard(product)).join("")}
        </div>
      </main>
    `;
  }

  const activeProject = state.projectsById.get(state.activeProjectId);
  if (activeProject) {
    return renderProjectWorkspace(activeProduct, activeProject);
  }

  return renderProductWorkspace(activeProduct);
}

function renderProductCard(product) {
  return `
    <article class="product-card">
      <div class="card-kicker">${escapeText(product.productId)}</div>
      <h2>${escapeText(product.productName || product.productId)}</h2>
      <p>${escapeText(product.frontendEntry || "No frontend entry")}</p>
      <button class="primary-button"
              data-action="start-product"
              data-product-id="${escapeText(product.productId)}"
              type="button">
        Start
      </button>
    </article>
  `;
}

function renderProductWorkspace(product) {
  const recentProjects = product.recentProjects ?? [];
  const catalogs = product.runtime?.catalogs ?? product.catalogs ?? [];
  return `
    <main class="workspace">
      <section class="workspace-header">
        <div>
          <h1>${escapeText(product.productName || product.productId)}</h1>
          <p>${escapeText(product.frontendEntry || "Product workspace")}</p>
        </div>
        ${product.isStarted ? "" : `
          <button class="primary-button"
                  data-action="start-product"
                  data-product-id="${escapeText(product.productId)}"
                  type="button">
            Start
          </button>
        `}
      </section>

      <section class="open-project-strip">
        <input data-field="projectPath" type="text" value="D:/projects/sample.icax" aria-label="Project path" />
        <button class="primary-button" data-action="open-file" type="button">Open File</button>
        <button class="tool-button" data-action="choose-file" type="button">Browse</button>
      </section>

      <section class="workspace-section">
        <h2>Open Projects</h2>
        ${catalogs.length === 0 ? `<div class="empty-block">No project is open.</div>` : `
          <div class="project-list">
            ${catalogs.map((catalog) => renderProjectRow(catalog)).join("")}
          </div>
        `}
      </section>

      <section class="workspace-section">
        <h2>Recent Projects</h2>
        ${recentProjects.length === 0 ? `<div class="empty-block">No recent project.</div>` : `
          <div class="recent-list">
            ${recentProjects.map((item) => `
              <div class="recent-row">
                <div>
                  <div class="recent-title">${escapeText(item.displayName || item.path)}</div>
                  <div class="recent-path">${escapeText(item.path)}</div>
                </div>
                <div class="recent-time">${escapeText(item.lastOpenedTime ?? "")}</div>
              </div>
            `).join("")}
          </div>
        `}
      </section>
    </main>
  `;
}

function renderProjectRow(catalog) {
  const project = catalog.mainProject;
  return `
    <button class="project-row"
            data-action="activate-project"
            data-project-id="${escapeText(project?.projectId ?? "")}"
            type="button">
      <span>${escapeText(project?.projectName ?? catalog.catalogName)}</span>
      <span>${escapeText(project?.state ?? "Unknown")}</span>
    </button>
  `;
}

function renderProjectWorkspace(product, project) {
  return `
    <main class="workspace project-workspace">
      <section class="workspace-header">
        <div>
          <h1>${escapeText(project.projectName)}</h1>
          <p>${escapeText(project.projectPath)}</p>
        </div>
        <div class="project-command-bar">
          <button class="tool-button" type="button">Save</button>
          <button class="tool-button" type="button">Undo</button>
          <button class="tool-button" type="button">Redo</button>
        </div>
      </section>
      <div class="editor-surface">
        <div class="editor-title">${escapeText(product.productName)} Project Workspace</div>
        <div class="editor-grid">
          <div>Scene</div>
          <div>Process</div>
          <div>Resources</div>
          <div>Diagnostics</div>
        </div>
      </div>
    </main>
  `;
}

function renderInspector(state) {
  const activeProduct = state.productsById.get(state.activeProductId);
  const activeProject = state.projectsById.get(state.activeProjectId);
  return `
    <aside class="inspector">
      <div class="panel-title">Inspector</div>
      ${activeProject ? `
        <dl class="property-list">
          <dt>Project</dt><dd>${escapeText(activeProject.projectName)}</dd>
          <dt>State</dt><dd>${escapeText(activeProject.state)}</dd>
          <dt>Mail</dt><dd>${escapeText(activeProject.projectMailId)}</dd>
        </dl>
      ` : activeProduct ? `
        <dl class="property-list">
          <dt>Product</dt><dd>${escapeText(activeProduct.productName)}</dd>
          <dt>Version</dt><dd>${escapeText(activeProduct.productVersion)}</dd>
          <dt>Magic</dt><dd>${escapeText(activeProduct.projectFile?.magic)}</dd>
          <dt>Mail</dt><dd>${escapeText(activeProduct.productMailId || "Not started")}</dd>
        </dl>
      ` : `
        <div class="empty-block">No active context.</div>
      `}
    </aside>
  `;
}

function renderBottomDock(state) {
  return `
    <footer class="bottom-dock">
      <div class="panel-title">Log</div>
      <div class="log-list">
        ${state.log.length === 0 ? `<span class="muted">No activity.</span>` : state.log.map((entry) => `
          <div class="log-row ${escapeText(entry.level)}">
            <span>${escapeText(entry.time)}</span>
            <span>${escapeText(entry.message)}</span>
          </div>
        `).join("")}
      </div>
    </footer>
  `;
}
