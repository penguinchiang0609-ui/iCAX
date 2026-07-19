import { findMachineDefinition, getImportedMachineDefinitionId, getMachineId, getMachines, getSelectedMachine, getSelectedMachineId } from "../state/sceneSelectors.mjs";
import { findMachineJoint, getAngularJogStepDeg, getLinearJogStepMm, isRotaryJointType } from "./machineConstants.mjs";

export async function handleMachineRibbonCommand(context, view, commandId, ops) {
  if (commandId === "machine.import") {
    await chooseMachineFile(context, view, true, ops);
    return true;
  }
  if (commandId === "machine.parameters") {
    saveMachineParameters(context, view, ops);
    return true;
  }
  if (commandId === "machine.tcp") {
    saveMachineTCP(context, view, ops);
    return true;
  }
  if (commandId === "machine.jog") {
    jogMachine(context, view, "positive", ops);
    return true;
  }
  if (commandId === "machine.home") {
    invokeMachineMethod(context, view, "Machine.Home", {}, {}, ops);
    return true;
  }
  if (commandId === "machine.reset") {
    invokeMachineMethod(context, view, "Machine.Reset", {}, {}, ops);
    return true;
  }
  if (commandId === "machine.limit-check") {
    invokeMachineMethod(context, view, "Machine.CheckLimits", {}, {}, ops);
    return true;
  }
  if (commandId === "machine.reach-check") {
    invokeMachineMethod(context, view, "Machine.CheckReach", {}, {}, ops);
    return true;
  }
  return false;
}

export function handleMachineAction(context, view, action, actionTarget, ops) {
  if (action === "choose-machine") {
    chooseMachineFile(context, view, false, ops);
    return true;
  }
  if (action === "import-machine") {
    const input = context.mount?.querySelector?.("[data-cam-machine-path]");
    const sourcePath = String(input?.value ?? view.machineSourcePath ?? "").trim();
    importMachinePath(context, view, sourcePath, ops);
    return true;
  }
  if (action === "instantiate-machine-definition") {
    const machineDefinitionId = actionTarget?.dataset?.camDefinitionId ?? view.selectedMachineDefinitionId ?? "";
    instantiateMachineDefinition(context, view, machineDefinitionId, ops);
    return true;
  }
  if (action === "toggle-machine-definition") {
    const machineDefinitionId = actionTarget?.dataset?.camDefinitionId ?? view.selectedMachineDefinitionId ?? "";
    const enabled = actionTarget?.dataset?.camNextEnabled === "true";
    setMachineDefinitionEnabled(context, view, machineDefinitionId, enabled, ops);
    return true;
  }
  if (action === "delete-machine-definition") {
    const machineDefinitionId = actionTarget?.dataset?.camDefinitionId ?? view.selectedMachineDefinitionId ?? "";
    deleteMachineDefinition(context, view, machineDefinitionId, ops);
    return true;
  }
  if (action === "toggle-machine-instance") {
    const machineEntityId = actionTarget?.dataset?.camMachineId ?? view.selectedMachineInstanceId ?? "";
    const enabled = actionTarget?.dataset?.camNextEnabled === "true";
    setMachineInstanceEnabled(context, view, machineEntityId, enabled, ops);
    return true;
  }
  if (action === "save-machine-parameters") {
    saveMachineParameters(context, view, ops);
    return true;
  }
  if (action === "save-machine-tcp") {
    saveMachineTCP(context, view, ops);
    return true;
  }
  if (action === "machine-jog-negative" || action === "machine-jog-positive") {
    jogMachine(context, view, action === "machine-jog-negative" ? "negative" : "positive", ops);
    return true;
  }
  if (action === "home-machine") {
    invokeMachineMethod(context, view, "Machine.Home", {}, {}, ops);
    return true;
  }
  if (action === "reset-machine") {
    invokeMachineMethod(context, view, "Machine.Reset", {}, {}, ops);
    return true;
  }
  if (action === "check-machine-limits") {
    invokeMachineMethod(context, view, "Machine.CheckLimits", {}, {}, ops);
    return true;
  }
  if (action === "check-machine-reach") {
    invokeMachineMethod(context, view, "Machine.CheckReach", {}, {}, ops);
    return true;
  }
  return false;
}

export function attachMachineTransformAutoApply(context, view, ops) {
  const editor = context.mount?.querySelector?.("[data-cam-transform-editor]");
  if (!editor) {
    return;
  }

  const inputs = editor.querySelectorAll("input");
  for (const input of inputs) {
    input.addEventListener("input", () => scheduleMachineElementTransformApply(context, view, editor, ops, 40));
    input.addEventListener("change", () => scheduleMachineElementTransformApply(context, view, editor, ops, 0));
    input.addEventListener("keyup", (event) => {
      if (event.key === "ArrowUp" || event.key === "ArrowDown" || event.key === "PageUp" || event.key === "PageDown") {
        scheduleMachineElementTransformApply(context, view, editor, ops, 0);
      }
    });
    input.addEventListener("pointerup", () => {
      window.setTimeout(() => scheduleMachineElementTransformApply(context, view, editor, ops, 0), 0);
    });
    input.addEventListener("wheel", () => {
      if (document.activeElement === input) {
        window.setTimeout(() => scheduleMachineElementTransformApply(context, view, editor, ops, 0), 0);
      }
    });
  }
}

export function attachMachineJointLimitAutoApply(context, view, ops) {
  const editor = context.mount?.querySelector?.("[data-cam-joint-limit-editor]");
  if (!editor) {
    return;
  }

  const inputs = editor.querySelectorAll("input");
  for (const input of inputs) {
    input.addEventListener("input", () => scheduleMachineJointLimitApply(context, view, editor, ops, 80));
    input.addEventListener("change", () => scheduleMachineJointLimitApply(context, view, editor, ops, 0));
    input.addEventListener("keyup", (event) => {
      if (event.key === "ArrowUp" || event.key === "ArrowDown" || event.key === "PageUp" || event.key === "PageDown") {
        scheduleMachineJointLimitApply(context, view, editor, ops, 0);
      }
    });
    input.addEventListener("pointerup", () => {
      window.setTimeout(() => scheduleMachineJointLimitApply(context, view, editor, ops, 0), 0);
    });
    input.addEventListener("wheel", () => {
      if (document.activeElement === input) {
        window.setTimeout(() => scheduleMachineJointLimitApply(context, view, editor, ops, 0), 0);
      }
    });
  }
}

export function attachMachineJointPositionAutoApply(context, view, ops) {
  const editor = context.mount?.querySelector?.("[data-cam-joint-position-editor]");
  const input = editor?.querySelector?.("[data-cam-joint-position]");
  if (!editor || !input) {
    return;
  }

  input.addEventListener("input", () => scheduleMachineJointPositionApply(context, view, editor, ops, 40));
  input.addEventListener("change", () => scheduleMachineJointPositionApply(context, view, editor, ops, 0));
  input.addEventListener("keyup", (event) => {
    if (event.key === "ArrowUp" || event.key === "ArrowDown" || event.key === "PageUp" || event.key === "PageDown") {
      scheduleMachineJointPositionApply(context, view, editor, ops, 0);
    }
  });
  input.addEventListener("pointerup", () => {
    window.setTimeout(() => scheduleMachineJointPositionApply(context, view, editor, ops, 0), 0);
  });
  input.addEventListener("wheel", () => {
    if (document.activeElement === input) {
      window.setTimeout(() => scheduleMachineJointPositionApply(context, view, editor, ops, 0), 0);
    }
  });
}

export function attachMachineAppearanceAutoApply(context, view, ops) {
  const editor = context.mount?.querySelector?.("[data-cam-appearance-editor]");
  if (!editor) {
    return;
  }

  const color = editor.querySelector("[data-cam-machine-element-color]");
  const collider = editor.querySelector("[data-cam-machine-show-collider]");
  color?.addEventListener("input", () => scheduleMachineAppearanceApply(context, view, editor, ops, 40));
  color?.addEventListener("change", () => scheduleMachineAppearanceApply(context, view, editor, ops, 0));
  collider?.addEventListener("change", () => scheduleMachineAppearanceApply(context, view, editor, ops, 0));
}

export function attachMachineToolTCPAutoApply(context, view, ops) {
  const editor = context.mount?.querySelector?.("[data-cam-tool-tcp-editor]");
  if (!editor) {
    return;
  }

  const inputs = editor.querySelectorAll("input");
  for (const input of inputs) {
    input.addEventListener("input", () => scheduleMachineToolTCPApply(context, view, editor, ops, 80));
    input.addEventListener("change", () => scheduleMachineToolTCPApply(context, view, editor, ops, 0));
    input.addEventListener("keyup", (event) => {
      if (event.key === "ArrowUp" || event.key === "ArrowDown" || event.key === "PageUp" || event.key === "PageDown") {
        scheduleMachineToolTCPApply(context, view, editor, ops, 0);
      }
    });
    input.addEventListener("pointerup", () => {
      window.setTimeout(() => scheduleMachineToolTCPApply(context, view, editor, ops, 0), 0);
    });
    input.addEventListener("wheel", () => {
      if (document.activeElement === input) {
        window.setTimeout(() => scheduleMachineToolTCPApply(context, view, editor, ops, 0), 0);
      }
    });
  }
}

export function attachMachineInstanceNameAutoApply(context, view, ops) {
  const editor = context.mount?.querySelector?.("[data-cam-machine-name-editor]");
  const input = editor?.querySelector?.("[data-cam-machine-instance-name]");
  if (!editor || !input) {
    return;
  }

  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      input.blur();
    }
  });
  input.addEventListener("change", () => applyMachineInstanceName(context, view, editor, input, ops));
}

async function applyMachineInstanceName(context, view, editor, input, ops) {
  if (!editor?.isConnected || !input?.isConnected || !context.sceneProxy) {
    return;
  }

  const machineEntityId = String(editor.dataset.camMachineId || getSelectedMachineId(view.scene ?? {}, view) || "").trim();
  const name = String(input.value ?? "").trim();
  const currentName = String(getSelectedMachine(view.scene ?? {}, view)?.name ?? "").trim();
  editor.classList.toggle("invalid", !machineEntityId || !name);
  if (!machineEntityId || !name || name === currentName) {
    return;
  }

  view.machineNameAutoApply ??= {};
  const state = view.machineNameAutoApply;
  state.sequence = Number(state.sequence ?? 0) + 1;
  const sequence = state.sequence;

  try {
    const responsePayload = await context.sceneProxy.invoke("Machine.SetName", { machineEntityId, name }, { timeoutMs: 10000 });
    if (sequence !== state.sequence) {
      return;
    }
    mergeMachineFacadeResult(view, responsePayload);
    ops.appendProjectLog(context, "ok", `机床实例已重命名：${name}`);
    ops.renderProject(context, view);
  } catch (error) {
    if (sequence !== state.sequence) {
      return;
    }
    view.error = error?.message ?? String(error);
    ops.appendProjectLog(context, "error", `Machine.SetName 失败：${view.error}`);
    ops.renderProject(context, view);
  }
}

function scheduleMachineAppearanceApply(context, view, editor, ops, delayMs = 120) {
  view.machineAppearanceAutoApply ??= {};
  const state = view.machineAppearanceAutoApply;
  if (state.timer) {
    window.clearTimeout(state.timer);
    state.timer = 0;
  }

  state.timer = window.setTimeout(() => {
    state.timer = 0;
    applyMachineAppearanceLive(context, view, editor, ops);
  }, delayMs);
}

async function applyMachineAppearanceLive(context, view, editor, ops) {
  if (!editor?.isConnected || !context.sceneProxy) {
    return;
  }

  const payload = readAppearanceEditorPayload(editor, view);
  editor.classList.toggle("invalid", !payload);
  if (!payload) {
    return;
  }

  view.machineAppearanceAutoApply ??= {};
  const state = view.machineAppearanceAutoApply;
  state.sequence = Number(state.sequence ?? 0) + 1;
  const sequence = state.sequence;

  try {
    const responsePayload = await context.sceneProxy.invoke("Machine.SetElementAppearance", payload, { timeoutMs: 10000 });
    if (sequence !== state.sequence) {
      return;
    }
    mergeMachineFacadeResult(view, responsePayload);
  } catch (error) {
    if (sequence !== state.sequence) {
      return;
    }
    view.error = error?.message ?? String(error);
    ops.appendProjectLog(context, "error", `Machine.SetElementAppearance 失败：${view.error}`);
    ops.renderProject(context, view);
  }
}

function readAppearanceEditorPayload(editor, view) {
  const entityId = String(editor?.dataset?.camEntityId || view.selectedSceneObjectId || view.scene?.selection?.entityId || "").trim();
  const colorHex = String(editor.querySelector("[data-cam-machine-element-color]")?.value ?? "").trim();
  const collider = editor.querySelector("[data-cam-machine-show-collider]");
  if (!entityId || !/^#[0-9a-fA-F]{6}$/.test(colorHex)) {
    return null;
  }
  return {
    entityId,
    colorHex,
    showCollision: Boolean(collider?.checked),
  };
}

export async function chooseMachineFile(context, view, importAfterChoose, ops) {
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力";
    ops.renderProject(context, view);
    return;
  }

  try {
    await ensureMachineDefinitionSupportedFormats(context, view, ops);
    const path = await bridge.openFileDialog({
      title: "选择机床定义文件",
      filters: makeMachineDefinitionFileFilters(view),
    });
    if (!path) {
      return;
    }
    ops.appendProjectLog(context, "info", `选择机床定义文件：${path}`);
    view.machineSourcePath = path;
    view.error = "";
    if (importAfterChoose) {
      importMachinePath(context, view, path, ops);
      return;
    }
  } catch (error) {
    view.error = error?.message ?? String(error);
  }
  ops.renderProject(context, view);
}

async function ensureMachineDefinitionSupportedFormats(context, view, ops) {
  if (getMachineDefinitionSupportedFormats(view).length || !context.sceneProxy) {
    return;
  }

  try {
    const payload = await context.sceneProxy.invoke("MachineDefinition.GetSupportedFormats", {}, { timeoutMs: 10000 });
    view.scene ??= {};
    if (Array.isArray(payload?.supportedFormats)) {
      view.scene.machineDefinitionSupportedFormats = payload.supportedFormats;
    }
  } catch (error) {
    ops.appendProjectLog(context, "error", `读取机床定义格式失败：${error?.message ?? String(error)}`);
  }
}

export async function importMachinePath(context, view, sourcePath, ops) {
  const path = String(sourcePath ?? "").trim();
  view.machineSourcePath = path;
  if (!path) {
    view.error = `请输入${getMachineDefinitionFormatLabel(view)}机床定义文件路径`;
    ops.renderProject(context, view);
    return;
  }
  const imported = await ops.invokeFacadeMethodPayload(
    context,
    view,
    "MachineDefinition.Import",
    { sourcePath: path },
    { timeoutMs: 60000, expectScene: false },
  );
  if (!imported.ok) {
    return;
  }

  const refreshed = await ops.refreshSceneState(context, view);
  if (!refreshed) {
    return;
  }

  const machineDefinitionId = getImportedMachineDefinitionId(imported.payload, view.scene);
  if (!machineDefinitionId) {
    view.error = "机床定义导入完成，但当前场景没有返回可用的机床定义";
    ops.appendProjectLog(context, "error", view.error);
    ops.renderProject(context, view);
    return;
  }

  const definition = findMachineDefinition(view.scene, machineDefinitionId);
  if (!hasMachineDefinitionRenderableGeometry(definition)) {
    ops.appendProjectLog(
      context,
      "info",
      "该机床定义没有 visual/collision/include 几何节点，实例化后只能看到结构树，视口不会出现模型。",
    );
  }

  selectMachineDefinition(context, view, machineDefinitionId, ops);
  await instantiateMachineDefinition(context, view, machineDefinitionId, ops);
}

function getMachineDefinitionSupportedFormats(view = {}) {
  return Array.isArray(view.scene?.machineDefinitionSupportedFormats)
    ? view.scene.machineDefinitionSupportedFormats.filter(Boolean)
    : [];
}

function getMachineDefinitionFormatLabel(view = {}) {
  const formats = getMachineDefinitionSupportedFormats(view);
  const names = formats
    .map((item) => String(item?.name || item?.formatId || "").trim())
    .filter(Boolean);
  return names.length ? names.join(" / ") : "当前产品支持的";
}

function makeMachineDefinitionFileFilters(view = {}) {
  const formats = getMachineDefinitionSupportedFormats(view);
  const filters = formats
    .map((item) => {
      const extensions = Array.isArray(item?.extensions)
        ? item.extensions
            .map((extension) => String(extension ?? "").trim().replace(/^\./, ""))
            .filter(Boolean)
        : [];
      return extensions.length
        ? { name: String(item?.name || item?.formatId || "机床定义"), extensions }
        : null;
    })
    .filter(Boolean);

  return filters;
}

export async function instantiateMachineDefinition(context, view, machineDefinitionId, ops) {
  const id = String(machineDefinitionId ?? "").trim();
  if (!id) {
    view.error = "请选择机床定义";
    ops.renderProject(context, view);
    return;
  }

  view.selectedMachineDefinitionId = id;
  const ok = await ops.invokeFacadeMethod(context, view, "Machine.Instantiate", { machineDefinitionId: id }, { timeoutMs: 60000 });
  if (ok) {
    view.selectedMachineInstanceId = findLatestMachineInstanceId(view.scene, id) || getSelectedMachineId(view.scene, view);
    await ops.fitViewAfterRenderPublish(context, view);
    await ops.refreshSceneState(context, view);
    view.selectedMachineInstanceId = findLatestMachineInstanceId(view.scene, id) || view.selectedMachineInstanceId || getSelectedMachineId(view.scene, view);
    ops.renderProject(context, view);
  }
}

export function selectMachineDefinition(context, view, machineDefinitionId, ops) {
  const id = String(machineDefinitionId ?? "").trim();
  if (!id) {
    view.error = "请选择机床定义";
    ops.renderProject(context, view);
    return;
  }

  const definition = findMachineDefinition(view.scene, id);
  if (definition?.enabled === false) {
    view.selectedMachineDefinitionId = id;
    view.error = "该机床定义已禁用，不能实例化";
    ops.renderProject(context, view);
    return;
  }

  view.selectedMachineDefinitionId = id;
  view.selectedSceneObjectId = "";
  if (view.scene) {
    view.scene.machineElement = null;
  }
  view.error = "";
  ops.renderProject(context, view);
}

export function selectMachineInstance(context, view, machineEntityId, ops) {
  const id = String(machineEntityId ?? "").trim();
  if (!id) {
    return;
  }

  view.selectedMachineInstanceId = id;
  view.selectedSceneObjectId = "";
  if (view.scene) {
    view.scene.machineElement = null;
  }
  const machine = getMachines(view.scene).find((item) => getMachineId(item) === id);
  if (machine?.machineDefinitionId) {
    view.selectedMachineDefinitionId = machine.machineDefinitionId;
  }
  view.error = "";
  ops.renderProject(context, view);
}

export async function setMachineDefinitionEnabled(context, view, machineDefinitionId, enabled, ops) {
  const id = String(machineDefinitionId ?? "").trim();
  if (!id) {
    view.error = "请选择机床定义";
    ops.renderProject(context, view);
    return;
  }

  view.selectedMachineDefinitionId = id;
  await ops.invokeFacadeMethod(context, view, "MachineDefinition.SetEnabled", { machineDefinitionId: id, enabled: Boolean(enabled) });
}

export async function deleteMachineDefinition(context, view, machineDefinitionId, ops) {
  const id = String(machineDefinitionId ?? "").trim();
  if (!id) {
    view.error = "请选择机床定义";
    ops.renderProject(context, view);
    return;
  }

  const ok = await ops.invokeFacadeMethod(context, view, "MachineDefinition.Delete", { machineDefinitionId: id });
  if (ok && view.selectedMachineDefinitionId === id) {
    view.selectedMachineDefinitionId = "";
  }
}

export async function setMachineInstanceEnabled(context, view, machineEntityId, enabled, ops) {
  const id = String(machineEntityId ?? "").trim();
  if (!id) {
    view.error = "请选择机床实例";
    ops.renderProject(context, view);
    return;
  }

  view.selectedMachineInstanceId = id;
  const result = await ops.invokeFacadeMethod(context, view, "Machine.SetEnabled", { machineEntityId: id, enabled: Boolean(enabled) });
  if (result) {
    await ops.fitViewAfterRenderPublish(context, view);
  }
}

export function saveMachineParameters(context, view, ops) {
  const machine = getSelectedMachine(view.scene ?? {}, view);
  const maxVelocity = readNumberInput(context, "[data-cam-machine-max-velocity]", Number(machine.maxVelocity ?? 1000));
  const maxAcceleration = readNumberInput(context, "[data-cam-machine-max-acceleration]", Number(machine.maxAcceleration ?? 2000));
  const linearJogStep = readNumberInput(context, "[data-cam-machine-linear-jog-step]", getLinearJogStepMm(machine));
  const angularJogStep = readNumberInput(context, "[data-cam-machine-angular-jog-step]", getAngularJogStepDeg(machine));
  invokeMachineMethod(context, view, "Machine.SetParameters", {
    maxVelocity,
    maxAcceleration,
    linearJogStep,
    angularJogStep,
  }, {}, ops);
}

export function saveMachineTCP(context, view, ops) {
  invokeMachineMethod(context, view, "Machine.SetTCP", {
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
  }, {}, ops);
}

function scheduleMachineElementTransformApply(context, view, editor, ops, delayMs = 120) {
  view.machineTransformAutoApply ??= {};
  const state = view.machineTransformAutoApply;
  if (state.timer) {
    window.clearTimeout(state.timer);
    state.timer = 0;
  }

  state.timer = window.setTimeout(() => {
    state.timer = 0;
    applyMachineElementTransformLive(context, view, editor, ops);
  }, delayMs);
}

async function applyMachineElementTransformLive(context, view, editor, ops) {
  if (!editor?.isConnected || !context.sceneProxy) {
    return;
  }

  const payload = readTransformEditorPayload(editor, view);
  editor.classList.toggle("invalid", !payload);
  if (!payload) {
    return;
  }

  view.machineTransformAutoApply ??= {};
  const state = view.machineTransformAutoApply;
  state.sequence = Number(state.sequence ?? 0) + 1;
  const sequence = state.sequence;

  try {
    const responsePayload = await context.sceneProxy.invoke("Machine.SetElementTransform", payload, { timeoutMs: 10000 });
    if (sequence !== state.sequence) {
      return;
    }
    if (responsePayload?.machineElement && view.scene) {
      view.scene.machineElement = responsePayload.machineElement;
    }
  } catch (error) {
    if (sequence !== state.sequence) {
      return;
    }
    view.error = error?.message ?? String(error);
    ops.appendProjectLog(context, "error", `Machine.SetElementTransform 失败：${view.error}`);
    ops.renderProject(context, view);
  }
}

function scheduleMachineJointLimitApply(context, view, editor, ops, delayMs = 120) {
  view.machineJointLimitAutoApply ??= {};
  const state = view.machineJointLimitAutoApply;
  if (state.timer) {
    window.clearTimeout(state.timer);
    state.timer = 0;
  }

  state.timer = window.setTimeout(() => {
    state.timer = 0;
    applyMachineJointLimitsLive(context, view, editor, ops);
  }, delayMs);
}

function scheduleMachineJointPositionApply(context, view, editor, ops, delayMs = 80) {
  view.machineJointPositionAutoApply ??= {};
  const state = view.machineJointPositionAutoApply;
  if (state.timer) {
    window.clearTimeout(state.timer);
  }
  state.timer = window.setTimeout(() => {
    state.timer = 0;
    applyMachineJointPositionLive(context, view, editor, ops);
  }, delayMs);
}

async function applyMachineJointPositionLive(context, view, editor, ops) {
  if (!editor?.isConnected || !context.sceneProxy) {
    return;
  }
  const entityId = String(editor.dataset.camEntityId || "").trim();
  const displayPosition = readFiniteEditorNumber(editor, "[data-cam-joint-position]");
  const isRotary = isRotaryJointType(String(editor.dataset.camJointType || ""));
  const payload = entityId && displayPosition !== null
    ? { entityId, position: displayPosition * (isRotary ? Math.PI / 180 : 1) }
    : null;
  editor.classList.toggle("invalid", !payload);
  if (!payload) {
    return;
  }

  view.machineJointPositionAutoApply ??= {};
  const state = view.machineJointPositionAutoApply;
  state.sequence = Number(state.sequence ?? 0) + 1;
  const sequence = state.sequence;
  try {
    const responsePayload = await context.sceneProxy.invoke("Machine.SetJointPosition", payload, { timeoutMs: 10000 });
    if (sequence !== state.sequence) {
      return;
    }
    mergeMachineFacadeResult(view, responsePayload);
  } catch (error) {
    if (sequence !== state.sequence) {
      return;
    }
    view.error = error?.message ?? String(error);
    ops.appendProjectLog(context, "error", `Machine.SetJointPosition 失败：${view.error}`);
    ops.renderProject(context, view);
  }
}

async function applyMachineJointLimitsLive(context, view, editor, ops) {
  if (!editor?.isConnected || !context.sceneProxy) {
    return;
  }

  const payload = readJointLimitEditorPayload(editor, view);
  editor.classList.toggle("invalid", !payload);
  if (!payload) {
    return;
  }

  view.machineJointLimitAutoApply ??= {};
  const state = view.machineJointLimitAutoApply;
  state.sequence = Number(state.sequence ?? 0) + 1;
  const sequence = state.sequence;

  try {
    const responsePayload = await context.sceneProxy.invoke("Machine.SetJointLimits", payload, { timeoutMs: 10000 });
    if (sequence !== state.sequence) {
      return;
    }
    mergeMachineFacadeResult(view, responsePayload);
  } catch (error) {
    if (sequence !== state.sequence) {
      return;
    }
    view.error = error?.message ?? String(error);
    ops.appendProjectLog(context, "error", `Machine.SetJointLimits 失败：${view.error}`);
    ops.renderProject(context, view);
  }
}

function readJointLimitEditorPayload(editor, view) {
  const entityId = String(editor?.dataset?.camEntityId || view.selectedSceneObjectId || view.scene?.selection?.entityId || "").trim();
  if (!entityId) {
    return null;
  }

  const lower = readFiniteEditorNumber(editor, "[data-cam-joint-lower-limit]");
  const upper = readFiniteEditorNumber(editor, "[data-cam-joint-upper-limit]");
  if (lower === null || upper === null) {
    return null;
  }

  const jointType = String(editor?.dataset?.camJointType || "").trim();
  const isRotary = isRotaryJointType(jointType);
  const scale = isRotary ? Math.PI / 180 : 1;
  return {
    entityId,
    lower: lower * scale,
    upper: upper * scale,
  };
}

function scheduleMachineToolTCPApply(context, view, editor, ops, delayMs = 120) {
  view.machineToolTCPAutoApply ??= {};
  const state = view.machineToolTCPAutoApply;
  if (state.timer) {
    window.clearTimeout(state.timer);
    state.timer = 0;
  }

  state.timer = window.setTimeout(() => {
    state.timer = 0;
    applyMachineToolTCPLive(context, view, editor, ops);
  }, delayMs);
}

async function applyMachineToolTCPLive(context, view, editor, ops) {
  if (!editor?.isConnected || !context.sceneProxy) {
    return;
  }

  const payload = readToolTCPEditorPayload(editor, view);
  editor.classList.toggle("invalid", !payload);
  if (!payload) {
    return;
  }

  view.machineToolTCPAutoApply ??= {};
  const state = view.machineToolTCPAutoApply;
  state.sequence = Number(state.sequence ?? 0) + 1;
  const sequence = state.sequence;

  try {
    const responsePayload = await context.sceneProxy.invoke("Machine.SetToolTCP", payload, { timeoutMs: 10000 });
    if (sequence !== state.sequence) {
      return;
    }
    mergeMachineFacadeResult(view, responsePayload);
  } catch (error) {
    if (sequence !== state.sequence) {
      return;
    }
    view.error = error?.message ?? String(error);
    ops.appendProjectLog(context, "error", `Machine.SetToolTCP 失败：${view.error}`);
    ops.renderProject(context, view);
  }
}

function readToolTCPEditorPayload(editor, view) {
  const entityId = String(editor?.dataset?.camEntityId || view.selectedSceneObjectId || view.scene?.selection?.entityId || "").trim();
  if (!entityId) {
    return null;
  }

  const tcpLocalPosition = readVectorFromEditor(editor, [
    "[data-cam-tool-tcp-x]",
    "[data-cam-tool-tcp-y]",
    "[data-cam-tool-tcp-z]",
  ]);
  const beamLocalDirection = readVectorFromEditor(editor, [
    "[data-cam-tool-beam-x]",
    "[data-cam-tool-beam-y]",
    "[data-cam-tool-beam-z]",
  ]);
  if (!tcpLocalPosition || !beamLocalDirection) {
    return null;
  }
  if (beamLocalDirection.every((value) => Math.abs(value) <= Number.EPSILON)) {
    return null;
  }

  return {
    entityId,
    tcpLocalPosition,
    beamLocalDirection,
  };
}

function mergeMachineFacadeResult(view, payload) {
  if (!payload || typeof payload !== "object") {
    return;
  }

  view.scene ??= {};
  if (payload.machineElement) {
    view.scene.machineElement = payload.machineElement;
  }
  if (Array.isArray(payload.machines)) {
    view.scene.machines = payload.machines;
  } else if (payload.machine) {
    view.scene.machines = upsertMachine(view.scene.machines, payload.machine);
  }
}

function upsertMachine(machines, machine) {
  const result = Array.isArray(machines) ? [...machines] : [];
  const id = String(machine?.entityId || machine?.id || "");
  const index = result.findIndex((item) => String(item?.entityId || item?.id || "") === id);
  if (index >= 0) {
    result[index] = machine;
  } else if (id) {
    result.push(machine);
  }
  return result;
}

function readTransformEditorPayload(editor, view) {
  const entityId = String(editor?.dataset?.camEntityId || view.selectedSceneObjectId || view.scene?.selection?.entityId || "").trim();
  if (!entityId) {
    return null;
  }

  const position = readVectorFromEditor(editor, [
    "[data-cam-transform-position-x]",
    "[data-cam-transform-position-y]",
    "[data-cam-transform-position-z]",
  ]);
  const rotationDegrees = readVectorFromEditor(editor, [
    "[data-cam-transform-rotation-yaw]",
    "[data-cam-transform-rotation-pitch]",
    "[data-cam-transform-rotation-roll]",
  ]);
  const scale = readVectorFromEditor(editor, [
    "[data-cam-transform-scale-x]",
    "[data-cam-transform-scale-y]",
    "[data-cam-transform-scale-z]",
  ]);
  if (!position || !rotationDegrees || !scale || scale.some((value) => value === 0)) {
    return null;
  }

  const degreesToRadians = Math.PI / 180;
  return {
    entityId,
    position,
    rotationRadians: rotationDegrees.map((value) => value * degreesToRadians),
    scale,
  };
}

function readVectorFromEditor(editor, selectors) {
  const values = [];
  for (const selector of selectors) {
    const value = readFiniteEditorNumber(editor, selector);
    if (value === null) {
      return null;
    }
    values.push(value);
  }
  return values;
}

function readFiniteEditorNumber(editor, selector) {
  const text = String(editor.querySelector(selector)?.value ?? "").trim();
  if (!text) {
    return null;
  }
  const value = Number(text);
  return Number.isFinite(value) ? value : null;
}

export function jogMachine(context, view, direction, ops) {
  const machine = getSelectedMachine(view.scene ?? {}, view);
  const axis = getSelectedJogAxis(context, machine);
  const joint = findMachineJoint(machine, axis);
  const isRotary = isRotaryJointType(joint?.type);
  const defaultDistance = isRotary ? getAngularJogStepDeg(machine) : getLinearJogStepMm(machine);
  const uiDistance = Math.abs(readNumberInput(context, "[data-cam-machine-jog-delta]", defaultDistance));
  const internalDistance = isRotary ? uiDistance * Math.PI / 180 : uiDistance;
  const delta = direction === "negative" ? -internalDistance : internalDistance;
  invokeMachineMethod(context, view, "Machine.Jog", { axis, delta }, {}, ops);
}

export function invokeMachineMethod(context, view, facadeMethod, payload = {}, options = {}, ops) {
  const machineEntityId = getSelectedMachineId(view.scene ?? {}, view);
  if (!machineEntityId) {
    view.error = "请选择机床实例";
    ops.renderProject(context, view);
    return;
  }
  ops.invokeFacadeMethod(context, view, facadeMethod, { machineEntityId, ...payload }, options);
}

function findLatestMachineInstanceId(scene = {}, machineDefinitionId = "") {
  const id = String(machineDefinitionId ?? "").trim();
  const machines = getMachines(scene);
  for (let index = machines.length - 1; index >= 0; --index) {
    const machine = machines[index];
    if (!id || String(machine.machineDefinitionId ?? "") === id) {
      return getMachineId(machine);
    }
  }
  return "";
}

function hasMachineDefinitionRenderableGeometry(definition = null) {
  if (!definition) {
    return false;
  }
  if (definition.definitionScope === "product") {
    return true;
  }
  return Number(definition.visualCount ?? 0) > 0
    || Number(definition.collisionCount ?? 0) > 0
    || Number(definition.includeCount ?? 0) > 0;
}

function readNumberInput(context, selector, fallback) {
  const value = Number(context.mount?.querySelector?.(selector)?.value);
  return Number.isFinite(value) ? value : fallback;
}

function getSelectedJogAxis(context, machine = {}) {
  const selected = String(context.mount?.querySelector?.("[data-cam-machine-jog-axis]")?.value ?? "").trim();
  if (selected) {
    return selected;
  }

  const joints = Array.isArray(machine.joints) ? machine.joints : [];
  const joint = joints.find((item) => item?.type && item.type !== "fixed") ?? joints[0];
  return String(joint?.jointName ?? joint?.name ?? "X");
}
