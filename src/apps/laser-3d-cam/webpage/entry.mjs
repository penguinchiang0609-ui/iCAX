import { createThreeViewport } from "../../../iCAX-UI/SDK/index.mjs";

const projectViews = new Map();

const ribbonDefinition = {
  tabs: [
    {
      id: "machine",
      title: "机床",
      groups: [
        {
          title: "机器",
          commands: [
            { id: "machine.import", title: "导入机器", icon: "M", size: "large" },
            { id: "machine.parameters", title: "机器参数", icon: "P" },
            { id: "machine.tcp", title: "TCP", icon: "T" },
          ],
        },
        {
          title: "驱动",
          commands: [
            { id: "machine.jog", title: "点动", icon: "J" },
            { id: "machine.home", title: "回零", icon: "H" },
            { id: "machine.reset", title: "复位", icon: "R" },
          ],
        },
        {
          title: "检查",
          commands: [
            { id: "machine.limit-check", title: "轴限位", icon: "L" },
            { id: "machine.reach-check", title: "行程检查", icon: "C" },
          ],
        },
      ],
    },
    {
      id: "workpiece",
      title: "工件",
      groups: [
        {
          title: "工件",
          commands: [
            { id: "workpiece.import", title: "导入工件", icon: "I", size: "large" },
            { id: "workpiece.update", title: "更新", icon: "U" },
            { id: "workpiece.delete", title: "删除", icon: "D" },
          ],
        },
        {
          title: "检查修复",
          commands: [
            { id: "workpiece.check", title: "模型检查", icon: "K" },
            { id: "workpiece.repair", title: "自动修复", icon: "R" },
            { id: "workpiece.place", title: "摆放", icon: "P" },
          ],
        },
        {
          title: "支撑夹具",
          commands: [
            { id: "support.generate", title: "生成支架", icon: "S" },
            { id: "support.export-dxf", title: "导出 DXF", icon: "X" },
          ],
        },
      ],
    },
    {
      id: "toolpath",
      title: "刀路",
      groups: [
        {
          title: "选择",
          commands: [
            { id: "toolpath.pick-edge", title: "拾取 Edge", icon: "E" },
            { id: "toolpath.pick-loop", title: "拾取 Loop", icon: "L" },
            { id: "toolpath.clear-selection", title: "清除选择", icon: "C" },
          ],
        },
        {
          title: "生成",
          commands: [
            { id: "toolpath.recognize-holes", title: "识别孔", icon: "H", size: "large" },
            { id: "toolpath.add-selection", title: "生成刀路", icon: "G", size: "large" },
          ],
        },
        {
          title: "编辑工艺",
          commands: [
            { id: "toolpath.layer", title: "切割图层", icon: "Y" },
            { id: "toolpath.lead", title: "引入引出", icon: "N" },
            { id: "toolpath.microjoint", title: "微联", icon: "W" },
            { id: "toolpath.clear", title: "清空刀路", icon: "D" },
          ],
        },
      ],
    },
    {
      id: "job",
      title: "作业",
      groups: [
        {
          title: "作业",
          commands: [
            { id: "job.create", title: "新建作业", icon: "J", size: "large" },
            { id: "job.select-paths", title: "选择刀路", icon: "S" },
            { id: "job.block", title: "块/指令", icon: "B" },
          ],
        },
        {
          title: "规划检查",
          commands: [
            { id: "job.plan-motion", title: "轨迹规划", icon: "P", size: "large" },
            { id: "job.collision", title: "碰撞检测", icon: "C" },
            { id: "job.simulate", title: "运动仿真", icon: "F" },
          ],
        },
        {
          title: "输出",
          commands: [
            { id: "job.postprocessor", title: "厂家", icon: "V" },
            { id: "job.preview-nc", title: "NC 预览", icon: "N" },
            { id: "job.export-nc", title: "导出 NC", icon: "O" },
          ],
        },
      ],
    },
    {
      id: "view",
      title: "视图",
      groups: [
        {
          title: "视口",
          commands: [
            { id: "view.fit", title: "适合窗口", icon: "F", size: "large" },
            { id: "view.standard", title: "标准视图", icon: "V" },
            { id: "view.display", title: "显示模式", icon: "D" },
          ],
        },
        {
          title: "面板",
          commands: [
            { id: "view.left-panel", title: "左侧面板", icon: "L" },
            { id: "view.right-panel", title: "右侧面板", icon: "R" },
            { id: "view.reset-layout", title: "重置布局", icon: "Q" },
          ],
        },
      ],
    },
  ],
};

export function getRibbonDefinition() {
  return ribbonDefinition;
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

  if (commandId === "machine.import") {
    await chooseMachineFile(context, view, true);
  } else if (commandId === "machine.parameters") {
    saveMachineParameters(context, view);
  } else if (commandId === "machine.tcp") {
    saveMachineTCP(context, view);
  } else if (commandId === "machine.jog") {
    jogMachine(context, view, "positive");
  } else if (commandId === "machine.home") {
    runProjectCommand(context, view, "Machine.Home", {});
  } else if (commandId === "machine.reset") {
    runProjectCommand(context, view, "Machine.Reset", {});
  } else if (commandId === "machine.limit-check") {
    runProjectCommand(context, view, "Machine.CheckLimits", {});
  } else if (commandId === "machine.reach-check") {
    runProjectCommand(context, view, "Machine.CheckReach", {});
  } else if (commandId === "workpiece.import") {
    await chooseModelFile(context, view, true);
  } else if (commandId === "toolpath.recognize-holes") {
    runProjectCommand(context, view, "Toolpath.RecognizeLoops", {});
  } else if (commandId === "toolpath.add-selection") {
    runProjectCommand(context, view, "Toolpath.AddSelectionPath", {});
  } else if (commandId === "toolpath.clear") {
    runProjectCommand(context, view, "Toolpath.ClearProgram", {});
  } else if (commandId === "toolpath.clear-selection") {
    runProjectCommand(context, view, "Selection.PickTopology", { kind: "", id: 0, label: "" });
  } else if (commandId === "toolpath.pick-edge") {
    showNotice(context, view, "已进入 Edge 拾取意图：请在视口中选择边。");
  } else if (commandId === "toolpath.pick-loop") {
    showNotice(context, view, "已进入 Loop 拾取意图：请在视口中选择闭合环。");
  } else if (commandId === "view.fit") {
    runProjectCommand(context, view, "CameraView.Fit", {});
  } else {
    showNotice(context, view, `${findCommandTitle(commandId)} 功能入口已就位，等待后端能力接入。`);
  }
}

function getProjectView(projectId) {
  if (!projectViews.has(projectId)) {
    projectViews.set(projectId, {
      scene: null,
      pending: false,
      error: "",
      notice: "",
      machineSourcePath: "",
      sourcePath: "",
      viewport: null,
      viewportSceneProxy: null,
      progress: null,
    });
  }
  return projectViews.get(projectId);
}

function renderProject(context, view) {
  const { mount, project } = context;
  const tab = context.activeRibbonTabId || "machine";
  const scene = view.scene ?? {};
  const topology = scene.topology ?? {};

  mount.innerHTML = `
    <div class="cam-workbench">
      ${renderImportNotice(topology)}
      <aside class="cam-context-pane">
        ${renderLeftPane(tab, context, view)}
      </aside>
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
      <aside class="cam-info-pane">
        ${renderRightPane(tab, context, view)}
      </aside>
      ${view.pending && view.progress ? renderProgress(view.progress) : ""}
      ${view.notice ? `<div class="cam-status notice">${escapeText(view.notice)}</div>` : ""}
      ${view.error ? `<div class="cam-status error">${escapeText(view.error)}</div>` : ""}
    </div>
  `;

  const pathInput = mount.querySelector("[data-cam-model-path]");
  pathInput?.addEventListener("input", () => {
    view.sourcePath = pathInput.value;
  });

  const machinePathInput = mount.querySelector("[data-cam-machine-path]");
  machinePathInput?.addEventListener("input", () => {
    view.machineSourcePath = machinePathInput.value;
  });

  mount.onclick = (event) => {
    const actionTarget = event.target instanceof Element ? event.target.closest("[data-cam-action]") : null;
    if (actionTarget && !actionTarget.hasAttribute("disabled")) {
      runAction(context, view, actionTarget.dataset.camAction);
      return;
    }

    const workpieceTarget = event.target instanceof Element ? event.target.closest("[data-cam-workpiece-id]") : null;
    if (workpieceTarget) {
      runProjectCommand(context, view, "Workpiece.SetActive", { workpieceEntityId: workpieceTarget.dataset.camWorkpieceId });
      return;
    }

    const machinePickTarget = event.target instanceof Element ? event.target.closest("[data-cam-machine-pick]") : null;
    if (machinePickTarget) {
      runProjectCommand(context, view, "Selection.PickMachineObject", {
        kind: machinePickTarget.dataset.camMachineKind ?? "machine",
        id: Number(machinePickTarget.dataset.camMachineId ?? 0),
        label: machinePickTarget.dataset.camMachineLabel ?? "",
      });
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

  mountRenderViewport(context, view);
}

function renderLeftPane(tab, context, view) {
  const scene = view.scene ?? {};
  const machine = scene.machine ?? {};
  const machines = scene.machines ?? [];
  const model = scene.model ?? {};
  const workpieces = scene.workpieces ?? [];
  const toolpaths = scene.toolpaths ?? [];

  if (tab === "machine") {
    return `
      ${renderPanel("机器", renderMachineList(machines, machine.entityId))}
      ${renderPanel("工位", `<div class="cam-list-row active"><strong>${escapeText(machine.workstationName || "默认工位")}</strong><small>第一版采用单机器单工位</small></div>`)}
      ${renderPanel("Link", renderMachineLinkList(machine))}
      ${renderPanel("Joint", renderMachineJointStructureList(machine))}
      ${renderPanel("Visual", renderMachineVisualList(machine))}
      ${renderPanel("轴位置", renderMachineJointList(machine))}
    `;
  }

  if (tab === "workpiece") {
    return `
      ${renderPanel("工件列表", renderWorkpieceList(workpieces, model.entityId))}
      ${renderPanel("模型检查", `
        <dl class="cam-facts">
          <dt>拓扑</dt><dd>${scene.topology?.hasTopology ? "可用" : "未生成"}</dd>
          <dt>面</dt><dd>${escapeText(scene.topology?.faceCount ?? 0)}</dd>
          <dt>Loop</dt><dd>${escapeText(scene.topology?.loopCount ?? 0)}</dd>
          <dt>边</dt><dd>${escapeText(scene.topology?.edgeCount ?? 0)}</dd>
        </dl>
      `)}
    `;
  }

  if (tab === "toolpath") {
    return `
      ${renderPanel("刀路列表", renderToolpathList(toolpaths))}
      ${renderPanel("当前选择", renderSelection(scene.selection))}
    `;
  }

  if (tab === "job") {
    return `
      ${renderPanel("作业列表", `
        <div class="cam-list-row active"><strong>默认作业</strong><small>${escapeText(toolpaths.length)} 条理想刀路</small></div>
      `)}
      ${renderPanel("块与指令", `
        <div class="cam-empty-row">可在这里组织刀路块、前指令和后指令。</div>
      `)}
    `;
  }

  return `
    ${renderPanel("显示对象", `
      <div class="cam-list-row active"><strong>工件</strong><small>显示</small></div>
      <div class="cam-list-row active"><strong>刀路</strong><small>显示</small></div>
      <div class="cam-list-row"><strong>机床</strong><small>未导入</small></div>
    `)}
    ${renderPanel("面板", `
      <div class="cam-list-row active"><strong>左侧上下文</strong><small>显示</small></div>
      <div class="cam-list-row active"><strong>右侧参数</strong><small>显示</small></div>
    `)}
  `;
}

function renderRightPane(tab, context, view) {
  const scene = view.scene ?? {};
  const machine = scene.machine ?? {};
  const model = scene.model ?? {};
  const workpieces = scene.workpieces ?? [];
  const toolpaths = scene.toolpaths ?? [];

  if (tab === "machine") {
    const disabled = view.pending ? "disabled" : "";
    const sourcePath = view.machineSourcePath || machine.sourcePath || "";
    return `
      ${renderPanel("机器参数", `
        <dl class="cam-facts">
          <dt>结构</dt><dd>${escapeText(machine.descriptionFormat || "SDF")}</dd>
          <dt>SDF</dt><dd>${escapeText(machine.sdfVersion || "-")}</dd>
          <dt>模型</dt><dd>${machine.isLoaded ? escapeText(machine.modelName || machine.name || machine.entityId) : "未导入"}</dd>
          <dt>Link</dt><dd>${escapeText(machine.links?.length ?? 0)}</dd>
          <dt>Joint</dt><dd>${escapeText(machine.joints?.length ?? 0)}</dd>
          <dt>Visual</dt><dd>${escapeText(machine.visuals?.length ?? 0)}</dd>
          <dt>Collision</dt><dd>${escapeText(machine.collisions?.length ?? 0)}</dd>
          <dt>状态</dt><dd>${escapeText(machine.status || "NotLoaded")}</dd>
          <dt>检查</dt><dd>${escapeText(machine.lastCheckResult || "-")}</dd>
        </dl>
      `)}
      ${renderPanel("导入", `
        <div class="cam-file-strip">
          <input class="cam-path-input" type="text" data-cam-machine-path value="${escapeAttr(sourcePath)}" placeholder="SDF/XML 机器描述文件路径" />
          <button class="tool-button" type="button" data-cam-action="choose-machine" ${disabled}>浏览</button>
          <button class="primary-button" type="button" data-cam-action="import-machine" ${disabled}>导入</button>
        </div>
        <div class="cam-hint">导入后解析 link、joint、visual、collision 和 material，并发布到三维视口。</div>
      `)}
      ${renderPanel("材质 / 插件", renderMachineMaterialSummary(machine))}
      ${renderPanel("运动参数", `
        <div class="cam-form-grid">
          <label class="cam-field">
            <span>最大速度</span>
            <input class="cam-small-input" type="number" step="1" min="1" data-cam-machine-max-velocity value="${escapeAttr(formatNumber(machine.maxVelocity || 1000, 3))}" />
          </label>
          <label class="cam-field">
            <span>最大加速度</span>
            <input class="cam-small-input" type="number" step="1" min="1" data-cam-machine-max-acceleration value="${escapeAttr(formatNumber(machine.maxAcceleration || 2000, 3))}" />
          </label>
        </div>
        <button class="tool-button" type="button" data-cam-action="save-machine-parameters" ${machine.isLoaded ? "" : "disabled"}>保存参数</button>
      `)}
      ${renderPanel("TCP", `
        <div class="cam-form-grid three">
          ${renderNumberField("X", "data-cam-machine-tcp-x", getArrayValue(machine.tcp, 0, 0))}
          ${renderNumberField("Y", "data-cam-machine-tcp-y", getArrayValue(machine.tcp, 1, 0))}
          ${renderNumberField("Z", "data-cam-machine-tcp-z", getArrayValue(machine.tcp, 2, 0))}
          ${renderNumberField("光束 X", "data-cam-machine-beam-x", getArrayValue(machine.beamDirection, 0, 0))}
          ${renderNumberField("光束 Y", "data-cam-machine-beam-y", getArrayValue(machine.beamDirection, 1, 0))}
          ${renderNumberField("光束 Z", "data-cam-machine-beam-z", getArrayValue(machine.beamDirection, 2, 1))}
        </div>
        <button class="tool-button" type="button" data-cam-action="save-machine-tcp" ${machine.isLoaded ? "" : "disabled"}>保存 TCP</button>
      `)}
      ${renderPanel("点动", `
        <div class="cam-jog-row">
          <select class="cam-select" data-cam-machine-jog-axis>${renderMachineAxisOptions(machine)}</select>
          <input class="cam-small-input" type="number" step="1" min="0.001" data-cam-machine-jog-delta value="10" />
          <button class="tool-button" type="button" data-cam-action="machine-jog-negative" ${machine.isLoaded ? "" : "disabled"}>-</button>
          <button class="tool-button" type="button" data-cam-action="machine-jog-positive" ${machine.isLoaded ? "" : "disabled"}>+</button>
        </div>
        <div class="cam-button-row">
          <button class="tool-button" type="button" data-cam-action="home-machine" ${machine.isLoaded ? "" : "disabled"}>回零</button>
          <button class="tool-button" type="button" data-cam-action="reset-machine" ${machine.isLoaded ? "" : "disabled"}>复位</button>
          <button class="tool-button" type="button" data-cam-action="check-machine-limits" ${machine.isLoaded ? "" : "disabled"}>轴限位</button>
          <button class="tool-button" type="button" data-cam-action="check-machine-reach" ${machine.isLoaded ? "" : "disabled"}>行程检查</button>
        </div>
      `)}
    `;
  }

  if (tab === "workpiece") {
    return `
      ${renderPanel("导入工件", `
        <div class="cam-file-strip">
          <input class="cam-path-input" type="text" data-cam-model-path value="${escapeAttr(view.sourcePath || model.sourcePath || "")}" placeholder="STEP/STP/IGS/IGES 文件路径" />
          <button class="tool-button" type="button" data-cam-action="choose-model" ${view.pending ? "disabled" : ""}>浏览</button>
          <button class="primary-button" type="button" data-cam-action="import-model" ${view.pending ? "disabled" : ""}>导入</button>
        </div>
      `)}
      ${renderPanel("工件属性", `
        <dl class="cam-facts">
          <dt>数量</dt><dd>${escapeText(workpieces.length)}</dd>
          <dt>当前</dt><dd>${escapeText(model.entityId || "-")}</dd>
          <dt>文件</dt><dd>${model.isLoaded ? escapeText(model.sourcePath) : "未导入"}</dd>
        </dl>
      `)}
      ${renderPanel("支撑/夹具", `
        <button class="tool-button" type="button" data-cam-action="support-placeholder">生成支架</button>
        <button class="tool-button" type="button" data-cam-action="support-placeholder">导出支架 DXF</button>
        <div class="cam-hint">支架拆单与排样交给平面激光 CAM 处理。</div>
      `)}
    `;
  }

  if (tab === "toolpath") {
    return `
      ${renderPanel("理想刀路", `
        <dl class="cam-facts">
          <dt>刀路</dt><dd>${escapeText(toolpaths.length)}</dd>
          <dt>图层</dt><dd>切割图层</dd>
          <dt>工艺</dt><dd>微联 / 引入引出</dd>
        </dl>
      `)}
      ${renderPanel("选择与生成", `
        ${renderSelection(scene.selection)}
        <div class="cam-button-row">
          <button class="tool-button" type="button" data-cam-action="recognize-loops" ${model.isLoaded && scene.topology?.hasTopology ? "" : "disabled"}>识别孔</button>
          <button class="primary-button" type="button" data-cam-action="add-selection" ${scene.selection?.id ? "" : "disabled"}>生成刀路</button>
          <button class="tool-button" type="button" data-cam-action="clear-toolpaths" ${toolpaths.length ? "" : "disabled"}>清空</button>
        </div>
      `)}
    `;
  }

  if (tab === "job") {
    return `
      ${renderPanel("当前作业", `
        <dl class="cam-facts">
          <dt>机床</dt><dd>当前单机器</dd>
          <dt>刀路</dt><dd>${escapeText(toolpaths.length)}</dd>
          <dt>状态</dt><dd>未规划</dd>
        </dl>
      `)}
      ${renderPanel("运动与检查", `
        <div class="cam-list-row"><strong>轨迹规划</strong><small>避奇异点 / 减少下刀 / 蛙跳</small></div>
        <div class="cam-list-row"><strong>碰撞检测</strong><small>等待规划结果</small></div>
        <div class="cam-list-row"><strong>运动仿真</strong><small>等待规划结果</small></div>
      `)}
      ${renderPanel("输出", `
        <div class="cam-list-row"><strong>厂家</strong><small>待选择</small></div>
        <button class="primary-button" type="button" data-cam-action="job-placeholder">生成 NC</button>
      `)}
    `;
  }

  return `
    ${renderPanel("视图参数", `
      <dl class="cam-facts">
        <dt>相机</dt><dd>后端控制</dd>
        <dt>拾取</dt><dd>视口对象</dd>
        <dt>显示</dt><dd>实体 / 刀路 / 机床</dd>
      </dl>
    `)}
  `;
}

function renderPanel(title, content) {
  return `
    <section class="cam-panel">
      <div class="cam-panel-title">${escapeText(title)}</div>
      <div class="cam-panel-body">${content}</div>
    </section>
  `;
}

function renderProgress(progress = {}) {
  return `
    <div class="cam-progress-backdrop" aria-live="polite" aria-modal="true" role="dialog">
      <section class="cam-progress-dialog">
        <div class="cam-progress-gauge" aria-hidden="true">
          <div class="cam-progress-face">
            ${Array.from({ length: 32 }, (_, index) => `<span style="--tick:${index}"></span>`).join("")}
            <div class="cam-progress-sweep"></div>
            <div class="cam-progress-needle"></div>
            <div class="cam-progress-core">
              <strong>RUN</strong>
              <small>${escapeText(progress.mode ?? "CAM")}</small>
            </div>
          </div>
        </div>
        <div class="cam-progress-copy">
          <strong>${escapeText(progress.title ?? "项目任务")}</strong>
          <span>${escapeText(progress.detail ?? "正在等待后台任务完成")}</span>
          <div class="cam-progress-stage">
            <span>${escapeText(progress.stage ?? "后台执行")}</span>
            <span>${escapeText(progress.mode ?? "CAM")}</span>
          </div>
        </div>
      </section>
    </div>
  `;
}

function renderSelection(selection = {}) {
  if (!selection.id) {
    return `<div class="cam-empty-row">未选择对象。</div>`;
  }
  return `
    <dl class="cam-facts">
      <dt>类型</dt><dd>${escapeText(selection.kind)}</dd>
      <dt>ID</dt><dd>${escapeText(selection.id)}</dd>
      <dt>名称</dt><dd>${escapeText(selection.label)}</dd>
    </dl>
  `;
}

function renderMachineList(machines, activeMachineId) {
  if (!machines.length) {
    return `<div class="cam-empty-row">未导入机器。请导入 SDF 机器描述文件。</div>`;
  }

  return `
    <div class="cam-list">
      ${machines.map((item) => {
        const isActive = String(item.entityId ?? "") === String(activeMachineId ?? "");
        return `
          <div class="cam-list-row ${isActive ? "active" : ""}">
            <strong>${escapeText(item.name || item.sourcePath || item.entityId)}</strong>
            <small>${escapeText(item.sourcePath || "SDF 机器描述")}</small>
          </div>
        `;
      }).join("")}
    </div>
  `;
}

function renderMachineLinkList(machine = {}) {
  const links = machine.links ?? [];
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机器导入后显示 Link。</div>`;
  }
  if (!links.length) {
    return `<div class="cam-empty-row">SDF 中没有 link。</div>`;
  }
  return `
    <div class="cam-list compact">
      ${links.slice(0, 24).map((link, index) => {
        const name = link?.name || `link ${index + 1}`;
        const visualCount = link?.visuals?.length ?? 0;
        const collisionCount = link?.collisions?.length ?? 0;
        return `
          <button class="cam-list-row" type="button"
                  data-cam-machine-pick
                  data-cam-machine-kind="machine.link"
                  data-cam-machine-id="${escapeAttr(index + 1)}"
                  data-cam-machine-label="${escapeAttr(name)}">
            <strong>${escapeText(name)}</strong>
            <small>${escapeText(visualCount)} visual / ${escapeText(collisionCount)} collision</small>
          </button>
        `;
      }).join("")}
      ${links.length > 24 ? `<div class="cam-empty-row">还有 ${escapeText(links.length - 24)} 个 link 未展开。</div>` : ""}
    </div>
  `;
}

function renderMachineJointStructureList(machine = {}) {
  const joints = machine.joints ?? [];
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机器导入后显示 Joint。</div>`;
  }
  if (!joints.length) {
    return `<div class="cam-empty-row">SDF 中没有 joint。</div>`;
  }
  return `
    <div class="cam-list compact">
      ${joints.slice(0, 24).map((joint, index) => {
        const name = joint?.name || `joint ${index + 1}`;
        const type = joint?.type || "unknown";
        const parent = joint?.parent || "-";
        const child = joint?.child || "-";
        return `
          <button class="cam-list-row" type="button"
                  data-cam-machine-pick
                  data-cam-machine-kind="machine.joint"
                  data-cam-machine-id="${escapeAttr(index + 1)}"
                  data-cam-machine-label="${escapeAttr(name)}">
            <strong>${escapeText(name)} · ${escapeText(type)}</strong>
            <small>${escapeText(parent)} → ${escapeText(child)}</small>
          </button>
        `;
      }).join("")}
      ${joints.length > 24 ? `<div class="cam-empty-row">还有 ${escapeText(joints.length - 24)} 个 joint 未展开。</div>` : ""}
    </div>
  `;
}

function renderMachineVisualList(machine = {}) {
  const visuals = machine.visuals ?? [];
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机器导入后显示 Visual。</div>`;
  }
  if (!visuals.length) {
    return `<div class="cam-empty-row">SDF 中没有 visual。</div>`;
  }
  return `
    <div class="cam-list compact">
      ${visuals.slice(0, 24).map((visual, index) => {
        const name = visual?.name || `visual ${index + 1}`;
        const linkName = visual?.linkName || "-";
        const geometryType = visual?.geometry?.type || "unknown";
        const material = describeMaterial(visual?.material);
        return `
          <button class="cam-list-row" type="button"
                  data-cam-machine-pick
                  data-cam-machine-kind="machine.visual"
                  data-cam-machine-id="${escapeAttr(index + 1)}"
                  data-cam-machine-label="${escapeAttr(`${linkName}/${name}`)}">
            <strong>${escapeText(name)} · ${escapeText(geometryType)}</strong>
            <small>${escapeText(linkName)} · ${escapeText(material)}</small>
          </button>
        `;
      }).join("")}
      ${visuals.length > 24 ? `<div class="cam-empty-row">还有 ${escapeText(visuals.length - 24)} 个 visual 未展开。</div>` : ""}
    </div>
  `;
}

function renderMachineMaterialSummary(machine = {}) {
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机器导入后显示材质摘要。</div>`;
  }
  const materials = machine.materials ?? [];
  const includes = machine.includes ?? [];
  const frames = machine.frames ?? [];
  const plugins = machine.plugins ?? [];
  return `
    <dl class="cam-facts">
      <dt>Material</dt><dd>${escapeText(materials.length)}</dd>
      <dt>Include</dt><dd>${escapeText(includes.length)}</dd>
      <dt>Frame</dt><dd>${escapeText(frames.length)}</dd>
      <dt>Plugin</dt><dd>${escapeText(plugins.length)}</dd>
    </dl>
    ${materials.length ? `
      <div class="cam-list compact">
        ${materials.slice(0, 8).map((material, index) => `
          <div class="cam-list-row">
            <strong>${escapeText(material?.linkName || "-")} / ${escapeText(material?.visualName || `material ${index + 1}`)}</strong>
            <small>${escapeText(describeMaterial(material))}</small>
          </div>
        `).join("")}
      </div>
    ` : ""}
  `;
}

function describeMaterial(material) {
  if (!material) {
    return "默认材质";
  }
  if (material.script?.name) {
    return `script: ${material.script.name}`;
  }
  const diffuse = material.diffuse ?? [];
  if (diffuse.length >= 3) {
    return `rgba(${formatNumber(diffuse[0], 2)}, ${formatNumber(diffuse[1], 2)}, ${formatNumber(diffuse[2], 2)}, ${formatNumber(diffuse[3] ?? 1, 2)})`;
  }
  return "material";
}

function renderMachineJointList(machine = {}) {
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机器导入后显示轴位置。</div>`;
  }

  const names = machine.jointNames ?? [];
  if (!names.length) {
    return `<div class="cam-empty-row">SDF 中没有可点动的运动关节。</div>`;
  }
  const positions = machine.jointPositions ?? [];
  const lowerLimits = machine.jointLowerLimits ?? [];
  const upperLimits = machine.jointUpperLimits ?? [];
  return `
    <div class="cam-joint-grid">
      ${names.map((name, index) => `
        <div class="cam-joint-row">
          <strong>${escapeText(name)}</strong>
          <span>${escapeText(formatNumber(getArrayValue(positions, index, 0), 3))}</span>
          <small>${escapeText(formatNumber(getArrayValue(lowerLimits, index, 0), 0))} ~ ${escapeText(formatNumber(getArrayValue(upperLimits, index, 0), 0))}</small>
        </div>
      `).join("")}
    </div>
  `;
}

function renderMachineAxisOptions(machine = {}) {
  const names = machine.jointNames?.length ? machine.jointNames : ["X", "Y", "Z", "A", "C"];
  return names.map((name) => `<option value="${escapeAttr(name)}">${escapeText(name)}</option>`).join("");
}

function renderNumberField(label, attrName, value) {
  return `
    <label class="cam-field">
      <span>${escapeText(label)}</span>
      <input class="cam-small-input" type="number" step="0.001" ${attrName} value="${escapeAttr(formatNumber(value, 3))}" />
    </label>
  `;
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
      </div>
    `;
  }

  if (!model.isLoaded) {
    return `
      <div class="cam-empty-model">
        <strong>三维线条切割 CAM</strong>
        <span>请在“工件”大区导入 STEP / IGS 工件文件</span>
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
  const host = context.mount?.querySelector?.("[data-cam-render-viewport]");
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
  view.viewport.refreshAll();
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

function renderWorkpieceList(workpieces, activeWorkpieceId) {
  if (!workpieces.length) {
    return `<div class="cam-empty-row">暂无工件。</div>`;
  }

  return `
    <div class="cam-list">
      ${workpieces.map((item, index) => {
        const isActive = String(item.entityId ?? "") === String(activeWorkpieceId ?? "");
        return `
          <button class="cam-list-row ${isActive ? "active" : ""}"
                  type="button"
                  data-cam-workpiece-id="${escapeAttr(item.entityId)}">
            <strong>${escapeText(index + 1)}. ${escapeText(item.name || item.sourcePath || item.entityId)}</strong>
            <small>${escapeText(item.sourcePath || item.modelResourceId || "")}</small>
          </button>
        `;
      }).join("")}
    </div>
  `;
}

function renderToolpathList(toolpaths) {
  if (!toolpaths.length) {
    return `<div class="cam-empty-row">暂无刀路。</div>`;
  }

  return `
    <div class="cam-list">
      ${toolpaths.map((item) => `
        <div class="cam-list-row">
          <strong>#${escapeText(item.id)} ${escapeText(item.label)}</strong>
          <small>${escapeText(item.operation)} · ${escapeText(item.source)}</small>
        </div>
      `).join("")}
    </div>
  `;
}

function runAction(context, view, action) {
  if (action === "choose-machine") {
    chooseMachineFile(context, view, false);
    return;
  }

  if (action === "import-machine") {
    const input = context.mount?.querySelector?.("[data-cam-machine-path]");
    const sourcePath = String(input?.value ?? view.machineSourcePath ?? "").trim();
    importMachinePath(context, view, sourcePath);
    return;
  }

  if (action === "save-machine-parameters") {
    saveMachineParameters(context, view);
    return;
  }

  if (action === "save-machine-tcp") {
    saveMachineTCP(context, view);
    return;
  }

  if (action === "machine-jog-negative" || action === "machine-jog-positive") {
    jogMachine(context, view, action === "machine-jog-negative" ? "negative" : "positive");
    return;
  }

  if (action === "home-machine") {
    runProjectCommand(context, view, "Machine.Home", {});
    return;
  }

  if (action === "reset-machine") {
    runProjectCommand(context, view, "Machine.Reset", {});
    return;
  }

  if (action === "check-machine-limits") {
    runProjectCommand(context, view, "Machine.CheckLimits", {});
    return;
  }

  if (action === "check-machine-reach") {
    runProjectCommand(context, view, "Machine.CheckReach", {});
    return;
  }

  if (action === "choose-model") {
    chooseModelFile(context, view, false);
    return;
  }

  if (action === "import-model") {
    const input = context.mount?.querySelector?.("[data-cam-model-path]");
    const sourcePath = String(input?.value ?? view.sourcePath ?? "").trim();
    importModelPath(context, view, sourcePath);
  } else if (action === "recognize-loops") {
    runProjectCommand(context, view, "Toolpath.RecognizeLoops", {});
  } else if (action === "add-selection") {
    runProjectCommand(context, view, "Toolpath.AddSelectionPath", {});
  } else if (action === "clear-toolpaths") {
    runProjectCommand(context, view, "Toolpath.ClearProgram", {});
  } else {
    showNotice(context, view, "该功能入口已就位，等待后端能力接入。");
  }
}

async function chooseMachineFile(context, view, importAfterChoose) {
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力";
    renderProject(context, view);
    return;
  }

  try {
    const path = await bridge.openFileDialog({
      title: "选择机器描述文件",
      filters: [
        { name: "SDF Machine", extensions: ["sdf", "xml"] },
      ],
    });
    if (!path) {
      return;
    }
    appendProjectLog(context, "info", `选择 SDF 机器文件：${path}`);
    view.machineSourcePath = path;
    view.error = "";
    if (importAfterChoose) {
      importMachinePath(context, view, path);
      return;
    }
  } catch (error) {
    view.error = error?.message ?? String(error);
  }
  renderProject(context, view);
}

async function chooseModelFile(context, view, importAfterChoose) {
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力";
    renderProject(context, view);
    return;
  }

  try {
    const path = await bridge.openFileDialog({
      title: "选择工件",
      filters: [
        { name: "CAD Model", extensions: ["step", "stp", "igs", "iges"] },
      ],
    });
    if (!path) {
      return;
    }
    appendProjectLog(context, "info", `选择工件模型：${path}`);
    view.sourcePath = path;
    view.error = "";
    if (importAfterChoose) {
      importModelPath(context, view, path);
      return;
    }
  } catch (error) {
    view.error = error?.message ?? String(error);
  }
  renderProject(context, view);
}

function importModelPath(context, view, sourcePath) {
  const path = String(sourcePath ?? "").trim();
  view.sourcePath = path;
  if (!path) {
    view.error = "请输入 STEP/STP/IGS/IGES 文件路径";
    renderProject(context, view);
    return;
  }
  runProjectCommand(context, view, "Workpiece.Import", { sourcePath: path }, { timeoutMs: 120000 })
    .then((ok) => ok ? fitViewAfterRenderPublish(context, view) : null);
}

function importMachinePath(context, view, sourcePath) {
  const path = String(sourcePath ?? "").trim();
  view.machineSourcePath = path;
  if (!path) {
    view.error = "请输入 SDF/XML 机器描述文件路径";
    renderProject(context, view);
    return;
  }
  runProjectCommand(context, view, "Machine.Import", { sourcePath: path }, { timeoutMs: 60000 })
    .then((ok) => ok ? fitViewAfterRenderPublish(context, view) : null);
}

function saveMachineParameters(context, view) {
  const maxVelocity = readNumberInput(context, "[data-cam-machine-max-velocity]", 1000);
  const maxAcceleration = readNumberInput(context, "[data-cam-machine-max-acceleration]", 2000);
  runProjectCommand(context, view, "Machine.SetParameters", {
    maxVelocity,
    maxAcceleration,
  });
}

function saveMachineTCP(context, view) {
  runProjectCommand(context, view, "Machine.SetTCP", {
    tcp: [
      readNumberInput(context, "[data-cam-machine-tcp-x]", 0),
      readNumberInput(context, "[data-cam-machine-tcp-y]", 0),
      readNumberInput(context, "[data-cam-machine-tcp-z]", 0),
    ],
    beamDirection: [
      readNumberInput(context, "[data-cam-machine-beam-x]", 0),
      readNumberInput(context, "[data-cam-machine-beam-y]", 0),
      readNumberInput(context, "[data-cam-machine-beam-z]", 1),
    ],
  });
}

function jogMachine(context, view, direction) {
  const axis = context.mount?.querySelector?.("[data-cam-machine-jog-axis]")?.value || "X";
  const distance = Math.abs(readNumberInput(context, "[data-cam-machine-jog-delta]", 10));
  const delta = direction === "negative" ? -distance : distance;
  runProjectCommand(context, view, "Machine.Jog", { axis, delta });
}

function readNumberInput(context, selector, fallback) {
  const value = Number(context.mount?.querySelector?.(selector)?.value);
  return Number.isFinite(value) ? value : fallback;
}

function handleViewportPick(context, view, userData, hit) {
  if (!userData || !hit) {
    view.error = "未命中可选择对象";
    renderProject(context, view);
    return;
  }

  if (Number(userData.renderClass) === 4 && userData.objectId) {
    runProjectCommand(context, view, "Selection.PickMachineObject", {
      objectId: Number(userData.objectId),
      kind: "machine.visual",
      label: `machine object ${userData.objectId}`,
    });
    return;
  }

  const faceIndex = Number(hit.faceIndex);
  if (Number.isInteger(faceIndex) && faceIndex >= 0) {
    const face = findFaceByTriangleIndex(view.scene, faceIndex);
    if (face) {
      runProjectCommand(context, view, "Selection.PickTopology", {
        kind: "face",
        id: Number(face.id),
        label: face.label ?? `face ${face.id}`,
      });
      return;
    }
  }

  view.error = "当前渲染数据缺少 edge/loop 拾取映射，已命中工件但无法定位拓扑对象";
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
  runProjectCommand(context, view, "Scene.Get", {});
}

async function runProjectCommand(context, view, command, payload, options = {}) {
  if (!context.sceneProxy) {
    return false;
  }

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
    const execute = () => context.sceneProxy.execute(command, payload, options);
    await waitForPaint();
    if (useShellProjectProgress) {
      view.scene = await context.actions.withProjectProgress(context.project.projectId, progress, execute);
    } else {
      view.scene = await execute();
    }
    appendProjectLog(context, command === "CameraView.Fit" && !view.scene?.fitView?.fitted ? "info" : "ok", makeCommandSuccessLog(command, view.scene));
    return true;
  } catch (error) {
    view.error = error?.message ?? String(error);
    appendProjectLog(context, "error", `${command} 失败：${view.error}`);
    return false;
  } finally {
    if (!useShellProjectProgress) {
      await waitForMinimumDuration(progressStartedAt, progress?.minimumVisibleMs ?? 0);
    }
    view.pending = false;
    view.progress = null;
    renderProject(context, view);
  }
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
  if (command === "Machine.Import") {
    return `开始导入 SDF 机器${sourcePath}`;
  }
  if (command === "Workpiece.Import") {
    return `开始导入工件模型${sourcePath}`;
  }
  if (command === "CameraView.Fit") {
    return "开始自动适配最佳视角";
  }
  return `执行 ${command}`;
}

function makeCommandSuccessLog(command, scene) {
  if (command === "Machine.Import") {
    const machine = scene?.machine ?? {};
    return `SDF 机器导入完成：links=${machine.links?.length ?? 0}, joints=${machine.joints?.length ?? 0}, visuals=${machine.visuals?.length ?? 0}, collisions=${machine.collisions?.length ?? 0}, includes=${machine.includes?.length ?? 0}`;
  }
  if (command === "Workpiece.Import") {
    const topology = scene?.topology ?? {};
    return `工件导入完成：faces=${topology.faceCount ?? 0}, loops=${topology.loopCount ?? 0}, edges=${topology.edgeCount ?? 0}`;
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
        `mesh=${fit.renderMeshCount ?? 0}`,
        `instance=${fit.renderInstanceCount ?? 0}`,
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
  }
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
  return command === "Machine.Import"
    || command === "Workpiece.Import"
    || command === "Toolpath.RecognizeLoops"
    || command === "Toolpath.AddSelectionPath";
}

function createCommandProgress(command) {
  if (command === "Machine.Import") {
    return {
      title: "导入机床",
      detail: "正在解析 SDF 机器描述文件",
      stage: "机器导入",
      mode: "Machine",
      minimumVisibleMs: 1600,
      steps: [
        ["文件检查", "正在校验 SDF/XML 路径"],
        ["SDF 解析", "正在解析 link、joint、visual、collision 和 material"],
        ["项目写入", "正在写入机床 Entity 与结构数据"],
        ["显示同步", "正在发布机器 visual 到 RenderPDO"],
        ["交互刷新", "正在刷新机床结构面板"],
      ],
    };
  }
  if (command === "Workpiece.Import") {
    return {
      title: "导入工件",
      detail: "正在读取模型文件",
      stage: "文件读取",
      mode: "CAD Import",
      minimumVisibleMs: 1600,
      steps: [
        ["文件读取", "正在读取 STEP/IGS 文件"],
        ["CAD 内核解析", "正在解析 B-Rep 拓扑"],
        ["拓扑归档", "正在写入工件与拓扑资源"],
        ["显示数据", "正在生成轻量显示网格"],
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

function getTabTitle(tabId) {
  return ribbonDefinition.tabs.find((tab) => tab.id === tabId)?.title ?? "机床";
}

function findCommandTitle(commandId) {
  for (const tab of ribbonDefinition.tabs) {
    for (const group of tab.groups) {
      const command = group.commands.find((item) => item.id === commandId);
      if (command) {
        return command.title;
      }
    }
  }
  return "该";
}

function getArrayValue(values, index, fallback = 0) {
  const value = Number(values?.[index]);
  return Number.isFinite(value) ? value : fallback;
}

function formatNumber(value, digits = 3) {
  const number = Number(value);
  if (!Number.isFinite(number)) {
    return "0";
  }
  return String(Number(number.toFixed(digits)));
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
      display: grid;
      gap: 4px;
      padding: 8px 0;
    }

    .cam-product-home strong {
      font-size: 14px;
    }

    .cam-product-home span {
      color: var(--muted);
      font-size: 12px;
    }

    .cam-workbench {
      position: relative;
      display: grid;
      grid-template-columns: 258px minmax(420px, 1fr) 318px;
      grid-template-rows: auto minmax(0, 1fr);
      grid-template-areas:
        "notice notice notice"
        "left viewer right";
      width: 100%;
      height: 100%;
      min-height: 0;
      background: #dfe4e7;
    }

    .cam-import-notice {
      grid-area: notice;
      padding: 7px 10px;
      border-bottom: 1px solid #d7c088;
      background: #fff7df;
      color: #785d16;
      font-size: 12px;
      line-height: 1.45;
    }

    .cam-context-pane,
    .cam-info-pane {
      min-width: 0;
      min-height: 0;
      display: grid;
      align-content: start;
      gap: 8px;
      padding: 8px;
      overflow: auto;
      background: #eef2f4;
    }

    .cam-context-pane {
      grid-area: left;
      border-right: 1px solid #c4ccd1;
    }

    .cam-info-pane {
      grid-area: right;
      border-left: 1px solid #c4ccd1;
    }

    .cam-viewer {
      grid-area: viewer;
      min-width: 0;
      min-height: 0;
      display: grid;
      grid-template-rows: 32px minmax(0, 1fr);
      background: #d7dde1;
    }

    .cam-viewer-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      min-width: 0;
      padding: 0 10px;
      border-bottom: 1px solid #bdc7cd;
      background: #f2f4f5;
      color: #252d32;
      font-size: 12px;
    }

    .cam-viewer-head strong {
      min-width: 0;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      font-weight: 650;
    }

    .cam-viewer-head div {
      display: flex;
      gap: 6px;
      color: #61707a;
    }

    .cam-viewer-head span {
      border: 1px solid #cbd3d8;
      background: #ffffff;
      padding: 2px 7px;
    }

    .cam-viewport {
      min-width: 0;
      min-height: 0;
      margin: 8px;
      border: 1px solid #293236;
      background: #1d2020;
      overflow: hidden;
    }

    .cam-svg,
    .cam-render-viewport-shell,
    .cam-render-viewport {
      display: block;
      width: 100%;
      height: 100%;
      min-height: 420px;
    }

    .cam-empty-model {
      min-height: 420px;
      display: grid;
      place-items: center;
      align-content: center;
      gap: 8px;
      color: #e9ece8;
    }

    .cam-empty-model strong {
      font-size: 22px;
      font-weight: 650;
    }

    .cam-empty-model span {
      color: #aeb9bd;
      font-size: 12px;
    }

    .cam-panel {
      min-width: 0;
      border: 1px solid #c8d0d5;
      background: #ffffff;
    }

    .cam-panel-title {
      min-height: 30px;
      display: flex;
      align-items: center;
      padding: 0 9px;
      border-bottom: 1px solid #d9e0e4;
      background: #f8f9fa;
      font-size: 12px;
      font-weight: 700;
    }

    .cam-panel-body {
      display: grid;
      gap: 7px;
      padding: 7px;
      font-size: 12px;
    }

    .cam-list {
      display: grid;
      gap: 1px;
    }

    .cam-list-row {
      display: grid;
      gap: 2px;
      width: 100%;
      min-height: 42px;
      padding: 6px 7px;
      border: 1px solid transparent;
      background: #ffffff;
      color: #20272c;
      text-align: left;
    }

    button.cam-list-row {
      font: inherit;
      cursor: pointer;
    }

    .cam-list.compact .cam-list-row {
      min-height: 34px;
      padding: 5px 6px;
    }

    .cam-list-row:hover,
    .cam-list-row.active {
      border-color: #2f735f;
      background: #e7f1ed;
    }

    .cam-list-row strong {
      min-width: 0;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      font-size: 12px;
      font-weight: 650;
    }

    .cam-list-row small,
    .cam-empty-row,
    .cam-hint {
      color: #65727b;
      font-size: 11px;
      line-height: 1.45;
    }

    .cam-empty-row {
      min-height: 28px;
      display: flex;
      align-items: center;
    }

    .cam-facts {
      display: grid;
      grid-template-columns: 70px minmax(0, 1fr);
      gap: 7px;
      margin: 0;
      font-size: 12px;
    }

    .cam-facts dt {
      color: #65727b;
    }

    .cam-facts dd {
      min-width: 0;
      margin: 0;
      overflow-wrap: anywhere;
    }

    .cam-file-strip {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 58px 58px;
      gap: 6px;
    }

    .cam-path-input {
      min-width: 0;
      height: 28px;
      padding: 0 8px;
      border: 1px solid #9aa8b0;
      background: #ffffff;
      color: #20272c;
      font-size: 12px;
      outline: none;
    }

    .cam-path-input:focus {
      border-color: #2f735f;
      box-shadow: 0 0 0 2px rgba(47, 115, 95, 0.16);
    }

    .cam-form-grid {
      display: grid;
      grid-template-columns: minmax(0, 1fr);
      gap: 6px;
    }

    .cam-form-grid.three {
      grid-template-columns: repeat(3, minmax(0, 1fr));
    }

    .cam-field {
      min-width: 0;
      display: grid;
      gap: 3px;
      color: #65727b;
      font-size: 11px;
    }

    .cam-small-input,
    .cam-select {
      width: 100%;
      min-width: 0;
      height: 28px;
      padding: 0 7px;
      border: 1px solid #9aa8b0;
      background: #ffffff;
      color: #20272c;
      font-size: 12px;
      outline: none;
    }

    .cam-small-input:focus,
    .cam-select:focus {
      border-color: #2f735f;
      box-shadow: 0 0 0 2px rgba(47, 115, 95, 0.16);
    }

    .cam-jog-row {
      display: grid;
      grid-template-columns: 58px minmax(0, 1fr) 38px 38px;
      gap: 6px;
      align-items: end;
    }

    .cam-joint-grid {
      display: grid;
      gap: 4px;
    }

    .cam-joint-row {
      display: grid;
      grid-template-columns: 38px minmax(0, 1fr) auto;
      align-items: center;
      gap: 7px;
      min-height: 28px;
      padding: 4px 6px;
      border: 1px solid #d4dce0;
      background: #ffffff;
      color: #20272c;
      font-size: 12px;
    }

    .cam-joint-row strong {
      font-weight: 700;
    }

    .cam-joint-row span {
      font-variant-numeric: tabular-nums;
    }

    .cam-joint-row small {
      color: #65727b;
      font-size: 11px;
    }

    .cam-button-row {
      display: flex;
      gap: 6px;
      flex-wrap: wrap;
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
      stroke: #d8a43a;
      stroke-width: 4;
    }

    .cam-loop {
      fill: rgba(29, 32, 32, 0.86);
      stroke: #e6e2d6;
      stroke-width: 3;
    }

    .cam-loop.selected {
      stroke: #d8a43a;
      stroke-width: 5;
    }

    .cam-loop-label {
      fill: #e6e2d6;
      font-size: 13px;
      pointer-events: none;
    }

    .cam-edge {
      fill: none;
      stroke: #c86548;
      stroke-width: 6;
      stroke-linecap: round;
      stroke-linejoin: round;
    }

    .cam-edge.selected {
      stroke: #d8a43a;
      stroke-width: 8;
    }

    .cam-toolpath {
      fill: none;
      stroke-width: 5;
      stroke-dasharray: 10 7;
      pointer-events: none;
    }

    .cam-toolpath-fill {
      fill: rgba(216, 164, 58, 0.16);
      stroke-width: 4;
      stroke-dasharray: 10 7;
      pointer-events: none;
    }

    .cam-status {
      position: absolute;
      right: 16px;
      bottom: 16px;
      max-width: 420px;
      padding: 7px 10px;
      border: 1px solid #2f735f;
      background: #eaf3ef;
      color: #1f5a49;
      font-size: 12px;
      box-shadow: 0 8px 22px rgba(0, 0, 0, 0.18);
    }

    .cam-status.notice {
      border-color: #d8a43a;
      background: #fff8e8;
      color: #785d16;
    }

    .cam-status.error {
      border-color: #a84646;
      background: #f8e7e7;
      color: #8a2d2d;
    }

    .cam-progress-backdrop {
      position: absolute;
      inset: 0;
      z-index: 20;
      display: grid;
      place-items: center;
      padding: 24px;
      background: rgba(21, 27, 30, 0.46);
      backdrop-filter: blur(2px);
      pointer-events: auto;
    }

    .cam-progress-dialog {
      width: min(520px, 92%);
      min-height: 190px;
      display: grid;
      grid-template-columns: 168px minmax(0, 1fr);
      align-items: center;
      gap: 24px;
      padding: 22px 24px;
      border: 1px solid rgba(136, 165, 173, 0.82);
      background: linear-gradient(135deg, rgba(27, 34, 37, 0.96), rgba(41, 49, 51, 0.96));
      box-shadow: 0 24px 70px rgba(0, 0, 0, 0.38), inset 0 1px 0 rgba(255, 255, 255, 0.08);
      color: #f4f7f7;
    }

    .cam-progress-gauge {
      width: 148px;
      aspect-ratio: 1;
      display: grid;
      place-items: center;
    }

    .cam-progress-face {
      position: relative;
      width: 100%;
      height: 100%;
      border-radius: 50%;
      border: 1px solid rgba(169, 190, 195, 0.72);
      background:
        radial-gradient(circle at 50% 52%, rgba(38, 48, 51, 1) 0 39%, transparent 40%),
        conic-gradient(from -120deg, rgba(61, 186, 146, 0.2), rgba(216, 164, 58, 0.78), rgba(61, 186, 146, 0.2));
      box-shadow: inset 0 0 24px rgba(0, 0, 0, 0.45), 0 0 34px rgba(61, 186, 146, 0.16);
      overflow: hidden;
    }

    .cam-progress-face span {
      position: absolute;
      left: 50%;
      top: 7px;
      width: 1px;
      height: 11px;
      transform-origin: 0 67px;
      transform: rotate(calc(var(--tick) * 11.25deg));
      background: rgba(232, 238, 238, 0.72);
    }

    .cam-progress-sweep {
      position: absolute;
      inset: 13px;
      border-radius: 50%;
      border: 3px solid transparent;
      border-top-color: #7ee6a8;
      border-right-color: rgba(126, 230, 168, 0.35);
      animation: cam-progress-sweep 1.55s linear infinite;
    }

    .cam-progress-needle {
      position: absolute;
      left: 50%;
      bottom: 50%;
      width: 3px;
      height: 52px;
      transform-origin: 50% 100%;
      background: #d8a43a;
      box-shadow: 0 0 14px rgba(216, 164, 58, 0.75);
      animation: cam-progress-needle 1.35s ease-in-out infinite;
    }

    .cam-progress-core {
      position: absolute;
      inset: 47px;
      border-radius: 50%;
      display: grid;
      place-items: center;
      align-content: center;
      gap: 2px;
      background: #1e2729;
      border: 1px solid rgba(232, 238, 238, 0.34);
    }

    .cam-progress-core strong,
    .cam-progress-core small {
      letter-spacing: 0;
      line-height: 1;
    }

    .cam-progress-core strong {
      font-size: 16px;
    }

    .cam-progress-core small {
      color: #9fb1b6;
      font-size: 10px;
    }

    .cam-progress-copy {
      min-width: 0;
      display: grid;
      gap: 10px;
    }

    .cam-progress-copy strong {
      font-size: 20px;
      font-weight: 700;
    }

    .cam-progress-copy > span {
      color: #c8d4d7;
      font-size: 13px;
      line-height: 1.55;
    }

    .cam-progress-stage {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }

    .cam-progress-stage span {
      border: 1px solid rgba(126, 230, 168, 0.28);
      background: rgba(126, 230, 168, 0.1);
      color: #d7eee5;
      padding: 4px 8px;
      font-size: 12px;
    }

    @keyframes cam-progress-sweep {
      to { transform: rotate(360deg); }
    }

    @keyframes cam-progress-needle {
      0%, 100% { transform: rotate(-42deg); }
      50% { transform: rotate(58deg); }
    }

    @media (max-width: 1240px) {
      .cam-workbench {
        grid-template-columns: 220px minmax(360px, 1fr) 280px;
      }
    }
  `;
  document.head.appendChild(style);
}
