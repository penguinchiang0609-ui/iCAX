import { getMachineId, getMachines } from "../state/sceneSelectors.mjs";

export function handleMachiningRibbonCommand(context, view, commandId, ops) {
  if (commandId === "toolpath.recognize-holes") {
    ops.runProjectCommand(context, view, "Toolpath.RecognizeLoops", {});
    return true;
  }
  if (commandId === "toolpath.add-selection") {
    ops.runProjectCommand(context, view, "Toolpath.AddSelectionPath", {});
    return true;
  }
  if (commandId === "toolpath.clear") {
    ops.runProjectCommand(context, view, "Toolpath.ClearProgram", {});
    return true;
  }
  if (commandId === "toolpath.clear-selection") {
    ops.runProjectCommand(context, view, "Selection.PickTopology", { kind: "", id: 0, label: "" });
    return true;
  }
  if (commandId === "toolpath.pick-edge") {
    ops.showNotice(context, view, "已进入 Edge 拾取意图：请在视口中选择边。");
    return true;
  }
  if (commandId === "toolpath.pick-loop") {
    ops.showNotice(context, view, "已进入 Loop 拾取意图：请在视口中选择闭合环。");
    return true;
  }
  return false;
}

export function handleMachiningAction(context, view, action, ops) {
  if (action === "recognize-loops") {
    ops.runProjectCommand(context, view, "Toolpath.RecognizeLoops", {});
    return true;
  }
  if (action === "add-selection") {
    ops.runProjectCommand(context, view, "Toolpath.AddSelectionPath", {});
    return true;
  }
  if (action === "clear-toolpaths") {
    ops.runProjectCommand(context, view, "Toolpath.ClearProgram", {});
    return true;
  }
  return false;
}

export async function setJobMachine(context, view, machineEntityId, ops) {
  const id = String(machineEntityId ?? "").trim();
  if (!id) {
    view.error = "请选择作业使用的机床实例";
    ops.renderProject(context, view);
    return;
  }
  const machine = getMachines(view.scene).find((item) => getMachineId(item) === id);
  if (machine?.enabled === false) {
    view.error = "该机床实例已禁用，不能作为作业候选";
    ops.renderProject(context, view);
    return;
  }
  view.selectedMachineInstanceId = id;
  await ops.runProjectCommand(context, view, "Job.SetMachine", { machineEntityId: id });
}
