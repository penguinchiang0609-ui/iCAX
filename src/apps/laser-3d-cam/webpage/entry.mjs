const projectViews = new Map();

export function mountProduct(context) {
  const { mount, product, actions } = context;
  if (!mount) {
    return;
  }
  ensureStyles();

  mount.innerHTML = `
    <div class="cam-product-home">
      <div class="cam-product-copy">
        <h2>${escapeText(product.productName || "三维线条切割 CAM")}</h2>
        <p>${escapeText(product.projectFile?.magic ?? "")}</p>
      </div>
      <div class="cam-product-actions">
        <button class="primary-button" type="button" data-cam-action="new-project" ${product.isStarted ? "" : "disabled"}>新建 CAM 项目</button>
      </div>
    </div>
  `;

  mount.querySelector("[data-cam-action='new-project']")?.addEventListener("click", () => {
    actions?.newProjectCatalog?.({
      catalogName: "三维线条切割",
      projectName: "未命名 CAM 项目",
    });
  });
}

export function mountProject(context) {
  const { mount, projectProxy, project } = context;
  if (!mount || !projectProxy || !project?.projectId) {
    return;
  }
  ensureStyles();

  const view = getProjectView(project.projectId);
  view.projectProxy = projectProxy;
  renderProject(context, view);

  if (!view.scene && !view.pending) {
    refreshScene(context, view);
  }
}

function getProjectView(projectId) {
  if (!projectViews.has(projectId)) {
    projectViews.set(projectId, {
      scene: null,
      pending: false,
      error: "",
      sourcePath: "",
    });
  }
  return projectViews.get(projectId);
}

function renderProject(context, view) {
  const { mount, project } = context;
  const scene = view.scene ?? {};
  const model = scene.model ?? {};
  const topology = scene.topology ?? {};
  const selection = scene.selection ?? {};
  const toolpaths = scene.toolpaths ?? [];

  mount.innerHTML = `
    <div class="cam-workspace">
      <div class="cam-toolbar">
        <input class="cam-path-input" type="text" data-cam-model-path value="${escapeAttr(view.sourcePath || model.sourcePath || "")}" placeholder="STEP 文件路径或 URI" />
        <button class="tool-button" type="button" data-cam-action="import-model">导入模型</button>
        <button class="tool-button" type="button" data-cam-action="recognize-loops" ${model.isLoaded && topology.hasTopology ? "" : "disabled"}>识别孔 loop</button>
        <button class="primary-button" type="button" data-cam-action="add-selection" ${selection.id ? "" : "disabled"}>加入刀路列表</button>
        <button class="tool-button" type="button" data-cam-action="clear-toolpaths" ${toolpaths.length ? "" : "disabled"}>清空刀路</button>
      </div>
      <div class="cam-main">
        <section class="cam-viewport">
          ${renderViewport(scene)}
        </section>
        <aside class="cam-side">
          <section class="cam-panel">
            <h3>当前选择</h3>
            <div class="cam-selection">${selection.id ? `${escapeText(selection.kind)} #${escapeText(selection.id)} · ${escapeText(selection.label)}` : "未选择"}</div>
          </section>
          <section class="cam-panel">
            <h3>刀路列表</h3>
            ${renderToolpathList(toolpaths)}
          </section>
          <section class="cam-panel">
            <h3>项目</h3>
            <dl class="cam-facts">
              <dt>名称</dt><dd>${escapeText(project.projectName)}</dd>
              <dt>模型</dt><dd>${model.isLoaded ? escapeText(model.sourcePath) : "未导入"}</dd>
              <dt>拓扑版本</dt><dd>${escapeText(model.topologyVersion ?? 0)}</dd>
              <dt>拓扑</dt><dd>${topology.hasTopology ? `${escapeText(topology.faceCount ?? 0)} 面 / ${escapeText(topology.loopCount ?? 0)} loop / ${escapeText(topology.edgeCount ?? 0)} 边` : "未生成"}</dd>
            </dl>
          </section>
        </aside>
      </div>
      ${view.pending ? `<div class="cam-status">执行中</div>` : ""}
      ${view.error ? `<div class="cam-status error">${escapeText(view.error)}</div>` : ""}
    </div>
  `;

  const pathInput = mount.querySelector("[data-cam-model-path]");
  pathInput?.addEventListener("input", () => {
    view.sourcePath = pathInput.value;
  });

  mount.onclick = (event) => {
    const actionTarget = event.target instanceof Element ? event.target.closest("[data-cam-action]") : null;
    if (actionTarget && !actionTarget.hasAttribute("disabled")) {
      runCamAction(context, view, actionTarget.dataset.camAction);
      return;
    }

    const pickTarget = event.target instanceof Element ? event.target.closest("[data-cam-pick]") : null;
    if (pickTarget) {
      const kind = pickTarget.dataset.camKind;
      const id = Number(pickTarget.dataset.camId);
      const label = pickTarget.dataset.camLabel ?? "";
      runProjectCommand(context, view, "Cam.PickTopology", { kind, id, label });
    }
  };
}

function renderViewport(scene) {
  const model = scene.model ?? {};
  if (!model.isLoaded) {
    return `
      <div class="cam-empty-model">
        <strong>CAM Project</strong>
        <span>未导入模型</span>
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

function renderFace(face, selectedKey, index) {
  const points = (face.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
  const key = `face:${face.id}`;
  const selected = key === selectedKey ? " selected" : "";
  const fill = index === 0 ? "url(#cam-face)" : "url(#cam-side)";
  return `
    <polygon class="cam-face${selected}"
             points="${escapeAttr(points)}"
             fill="${fill}"
             data-cam-pick
             data-cam-kind="face"
             data-cam-id="${escapeAttr(face.id)}"
             data-cam-label="${escapeAttr(face.label)}" />
  `;
}

function renderLoop(loop, selectedKey) {
  if (!Number(loop.radius)) {
    return "";
  }
  const key = `loop:${loop.id}`;
  const selected = key === selectedKey ? " selected" : "";
  return `
    <circle class="cam-loop${selected}"
            cx="${escapeAttr(loop.center?.x ?? 0)}"
            cy="${escapeAttr(loop.center?.y ?? 0)}"
            r="${escapeAttr(loop.radius)}"
            data-cam-pick
            data-cam-kind="loop"
            data-cam-id="${escapeAttr(loop.id)}"
            data-cam-label="${escapeAttr(loop.label)}" />
    <text class="cam-loop-label" x="${escapeAttr((loop.center?.x ?? 0) - 22)}" y="${escapeAttr((loop.center?.y ?? 0) + 4)}">${escapeText(loop.label)}</text>
  `;
}

function renderEdge(edge, selectedKey) {
  const points = (edge.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
  const key = `edge:${edge.id}`;
  const selected = key === selectedKey ? " selected" : "";
  return `
    <polyline class="cam-edge${selected}"
              points="${escapeAttr(points)}"
              data-cam-pick
              data-cam-kind="edge"
              data-cam-id="${escapeAttr(edge.id)}"
              data-cam-label="${escapeAttr(edge.label)}" />
  `;
}

function renderToolpathOverlay(scene, item, index) {
  const color = index % 2 === 0 ? "#f8d66d" : "#7ee6a8";
  const topology = findTopologyObject(scene, item.topologyKind, item.topologyId);
  if (!topology) {
    return "";
  }
  if (item.topologyKind === "loop" && Number(topology.radius)) {
    return `<circle class="cam-toolpath" cx="${escapeAttr(topology.center?.x ?? 0)}" cy="${escapeAttr(topology.center?.y ?? 0)}" r="${escapeAttr(Number(topology.radius) + 10)}" stroke="${color}" />`;
  }
  if (item.topologyKind === "edge") {
    const points = (topology.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
    return `<polyline class="cam-toolpath" points="${escapeAttr(points)}" stroke="${color}" />`;
  }
  if (item.topologyKind === "face") {
    const points = (topology.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
    return `<polygon class="cam-toolpath-fill" points="${escapeAttr(points)}" stroke="${color}" />`;
  }
  return "";
}

function findTopologyObject(scene, kind, id) {
  const list = kind === "face"
    ? scene.faces
    : kind === "loop"
      ? scene.loops
      : kind === "edge"
        ? scene.edges
        : [];
  return (list ?? []).find((item) => String(item.id) === String(id)) ?? null;
}

function renderToolpathList(toolpaths) {
  if (!toolpaths.length) {
    return `<div class="cam-empty-row">暂无刀路。</div>`;
  }

  return `
    <div class="cam-toolpath-list">
      ${toolpaths.map((item) => `
        <div class="cam-toolpath-row">
          <span>#${escapeText(item.id)}</span>
          <strong>${escapeText(item.label)}</strong>
          <small>${escapeText(item.operation)} · ${escapeText(item.source)}</small>
        </div>
      `).join("")}
    </div>
  `;
}

function runCamAction(context, view, action) {
  if (action === "import-model") {
    const input = context.mount?.querySelector?.("[data-cam-model-path]");
    const sourcePath = String(input?.value ?? view.sourcePath ?? "").trim();
    view.sourcePath = sourcePath;
    if (!sourcePath) {
      view.error = "请输入 STEP 文件路径或 URI";
      renderProject(context, view);
      return;
    }
    runProjectCommand(context, view, "Cam.ImportModel", { sourcePath });
  } else if (action === "recognize-loops") {
    runProjectCommand(context, view, "Cam.RecognizeLoops", {});
  } else if (action === "add-selection") {
    runProjectCommand(context, view, "Cam.AddSelectionToToolpathList", {});
  } else if (action === "clear-toolpaths") {
    runProjectCommand(context, view, "Cam.ClearToolpathList", {});
  }
}

function refreshScene(context, view) {
  runProjectCommand(context, view, "Cam.GetScene", {});
}

async function runProjectCommand(context, view, command, payload) {
  if (!context.projectProxy) {
    return;
  }

  view.pending = true;
  view.error = "";
  renderProject(context, view);
  try {
    view.scene = await context.projectProxy.execute(command, payload);
  } catch (error) {
    view.error = error?.message ?? String(error);
  } finally {
    view.pending = false;
    renderProject(context, view);
  }
}

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function escapeAttr(value) {
  return escapeText(value)
    .replaceAll("\"", "&quot;")
    .replaceAll("'", "&#39;");
}

function ensureStyles() {
  if (document.getElementById("laser-3d-cam-style")) {
    return;
  }

  const style = document.createElement("style");
  style.id = "laser-3d-cam-style";
  style.textContent = `
    .cam-product-home {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      min-height: 140px;
      padding: 20px 0;
      border-top: 1px solid rgba(255, 255, 255, 0.08);
      border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    }

    .cam-product-copy h2 {
      margin: 0 0 6px;
      font-size: 20px;
      font-weight: 650;
    }

    .cam-product-copy p {
      margin: 0;
      color: var(--muted, #8896a3);
      font-size: 13px;
    }

    .cam-product-actions {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }

    .cam-workspace {
      display: grid;
      grid-template-rows: auto minmax(0, 1fr);
      min-height: 560px;
      gap: 10px;
    }

    .cam-toolbar {
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
      padding: 8px 0;
    }

    .cam-path-input {
      min-width: 260px;
      flex: 1 1 320px;
      height: 34px;
      padding: 0 10px;
      border: 1px solid rgba(255, 255, 255, 0.14);
      border-radius: 5px;
      background: rgba(14, 20, 25, 0.9);
      color: #e6f0f6;
      font-size: 13px;
      outline: none;
    }

    .cam-path-input:focus {
      border-color: #7ec6d8;
    }

    .cam-main {
      display: grid;
      grid-template-columns: minmax(420px, 1fr) 300px;
      min-height: 500px;
      gap: 14px;
    }

    .cam-viewport {
      min-width: 0;
      background: #182128;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 6px;
      overflow: hidden;
    }

    .cam-svg {
      display: block;
      width: 100%;
      height: 100%;
      min-height: 500px;
    }

    .cam-empty-model {
      display: grid;
      place-items: center;
      align-content: center;
      min-height: 500px;
      color: #d9e3ea;
      gap: 8px;
    }

    .cam-empty-model strong {
      font-size: 28px;
      letter-spacing: 0;
    }

    .cam-empty-model span,
    .cam-empty-row {
      color: var(--muted, #8896a3);
      font-size: 13px;
    }

    .cam-face,
    .cam-loop,
    .cam-edge {
      cursor: pointer;
      transition: opacity 120ms ease, stroke-width 120ms ease, filter 120ms ease;
    }

    .cam-face {
      stroke: rgba(255, 255, 255, 0.42);
      stroke-width: 2;
    }

    .cam-face:hover,
    .cam-loop:hover,
    .cam-edge:hover {
      filter: brightness(1.16);
    }

    .cam-face.selected {
      stroke: #f8d66d;
      stroke-width: 4;
    }

    .cam-loop {
      fill: rgba(24, 33, 40, 0.86);
      stroke: #eaf4f7;
      stroke-width: 3;
    }

    .cam-loop.selected {
      stroke: #f8d66d;
      stroke-width: 5;
    }

    .cam-loop-label {
      fill: #dce7ed;
      font-size: 13px;
      pointer-events: none;
    }

    .cam-edge {
      fill: none;
      stroke: #e9896a;
      stroke-width: 6;
      stroke-linecap: round;
      stroke-linejoin: round;
    }

    .cam-edge.selected {
      stroke: #f8d66d;
      stroke-width: 8;
    }

    .cam-toolpath {
      fill: none;
      stroke-width: 5;
      stroke-dasharray: 10 7;
      pointer-events: none;
    }

    .cam-toolpath-fill {
      fill: rgba(248, 214, 109, 0.15);
      stroke-width: 4;
      stroke-dasharray: 10 7;
      pointer-events: none;
    }

    .cam-side {
      min-width: 0;
      display: grid;
      gap: 10px;
      align-content: start;
    }

    .cam-panel {
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 6px;
      padding: 12px;
      background: rgba(20, 28, 34, 0.72);
    }

    .cam-panel h3 {
      margin: 0 0 10px;
      font-size: 14px;
      font-weight: 650;
    }

    .cam-selection {
      color: #dce7ed;
      font-size: 13px;
      line-height: 1.5;
    }

    .cam-toolpath-list {
      display: grid;
      gap: 8px;
    }

    .cam-toolpath-row {
      display: grid;
      grid-template-columns: 36px 1fr;
      gap: 4px 8px;
      padding: 8px;
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 5px;
      background: rgba(255, 255, 255, 0.04);
    }

    .cam-toolpath-row span {
      color: #f8d66d;
      font-size: 12px;
      grid-row: span 2;
    }

    .cam-toolpath-row strong {
      font-size: 13px;
      font-weight: 650;
    }

    .cam-toolpath-row small,
    .cam-facts dt {
      color: var(--muted, #8896a3);
      font-size: 12px;
    }

    .cam-facts {
      display: grid;
      grid-template-columns: 72px 1fr;
      gap: 8px;
      margin: 0;
      font-size: 12px;
    }

    .cam-facts dd {
      margin: 0;
      min-width: 0;
      overflow-wrap: anywhere;
    }

    .cam-status {
      position: absolute;
      right: 20px;
      bottom: 20px;
      padding: 8px 10px;
      border-radius: 5px;
      background: #26343d;
      color: #e6f0f6;
      font-size: 12px;
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.28);
    }

    .cam-status.error {
      background: #5a2d32;
    }

    @media (max-width: 960px) {
      .cam-main {
        grid-template-columns: 1fr;
      }

      .cam-side {
        grid-template-columns: repeat(3, minmax(0, 1fr));
      }
    }

    @media (max-width: 720px) {
      .cam-product-home {
        align-items: stretch;
        flex-direction: column;
      }

      .cam-side {
        grid-template-columns: 1fr;
      }
    }
  `;
  document.head.appendChild(style);
}
