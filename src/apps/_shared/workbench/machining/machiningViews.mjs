import { getEnabledMachines, getJobMachine, getJobMachineId, getMachineId } from "../state/sceneSelectors.mjs";
import { escapeAttr, escapeText } from "../utils/format.mjs";

export function renderJobMachineSelector(scene = {}, pending = false) {
  const machines = getEnabledMachines(scene);
  const jobMachineId = getJobMachineId(scene);
  if (!machines.length) {
    return `<div class="cam-empty-row">暂无启用的机床实例。请先在“机床定义”大区新增或启用机床实例。</div>`;
  }

  return `
    <label class="cam-field">
      <span>作业使用机床</span>
      <select class="cam-select" data-cam-job-machine-select ${pending ? "disabled" : ""}>
        ${machines.map((machine) => {
          const id = getMachineId(machine);
          return `<option value="${escapeAttr(id)}" ${id === jobMachineId ? "selected" : ""}>${escapeText(machine.name || machine.modelName || id)}</option>`;
        }).join("")}
      </select>
    </label>
    <div class="cam-hint">这里修改的是作业组件中的机床引用，不是全局 active 状态。</div>
  `;
}

export function renderMachiningReadiness(scene = {}) {
  const machine = getJobMachine(scene);
  const model = scene.model ?? {};
  const toolpaths = scene.toolpaths ?? [];
  const rows = [
    {
      title: "机床实例",
      ready: Boolean(machine.isLoaded),
      detail: machine.isLoaded
        ? (machine.name || machine.modelName || "已实例化")
        : "请先在“机床定义”中导入定义并实例化",
    },
    {
      title: "工件实例",
      ready: Boolean(model.isLoaded),
      detail: model.isLoaded
        ? (model.sourcePath || model.entityId || "已导入")
        : "请先在“工件编辑”中导入或修复工件",
    },
    {
      title: "拓扑拾取",
      ready: Boolean(scene.topology?.hasTopology),
      detail: scene.topology?.hasTopology
        ? `faces=${scene.topology.faceCount ?? 0}, loops=${scene.topology.loopCount ?? 0}, edges=${scene.topology.edgeCount ?? 0}`
        : "等待工件拓扑资源",
    },
    {
      title: "刀路集合",
      ready: toolpaths.length > 0,
      detail: toolpaths.length ? `${toolpaths.length} 条理想刀路` : "等待拾取或特征识别生成刀路",
    },
  ];

  return `
    <div class="cam-readiness">
      ${rows.map((row) => `
        <div class="cam-list-row ${row.ready ? "active" : ""}">
          <strong>${escapeText(row.title)}</strong>
          <small>${escapeText(row.detail)}</small>
        </div>
      `).join("")}
    </div>
  `;
}
