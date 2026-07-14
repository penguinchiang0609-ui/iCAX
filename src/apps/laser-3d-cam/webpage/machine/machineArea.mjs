import { renderPanel } from "../layout/commonViews.mjs";
import { getMachineId, getMachineSubtreeEntityIds, getSelectedMachine, getSelectedMachineDefinition } from "../state/sceneSelectors.mjs";
import { escapeAttr, escapeText, formatNumber } from "../utils/format.mjs";
import { getAngularJogStepDeg, getLinearJogStepMm } from "./machineConstants.mjs";
import {
  renderMachineDefinitionList,
  renderMachineList,
  renderMachineSceneTree,
} from "./machineViews.mjs";

export function renderMachineLeftPane(context, view) {
  const scene = view.scene ?? {};
  const machine = getSelectedMachine(scene, view);
  const machines = scene.machines ?? [];
  const machineDefinitions = scene.machineDefinitions ?? [];
  const selectedSceneObjectId = view.selectedSceneObjectId || scene.selection?.entityId || "";
  const selectedSceneObjectIds = getMachineSubtreeEntityIds(machine, selectedSceneObjectId);

  return `
    ${renderPanel("机床资源", renderMachineResourceAccordion(machineDefinitions, machines, machine, view))}
    ${renderPanel("当前实例场景树", renderMachineSceneTree(machine, selectedSceneObjectId, selectedSceneObjectIds))}
  `;
}

export function renderMachineRightPane(context, view) {
  const scene = view.scene ?? {};
  const machine = getSelectedMachine(scene, view);
  const selectedMachineDefinition = getSelectedMachineDefinition(scene, view);
  const selectedEntityId = view.selectedSceneObjectId || scene.selection?.entityId || "";
  const selectedElement = scene.machineElement && String(scene.machineElement.entityId || "") === String(selectedEntityId || "")
    ? scene.machineElement
    : null;

  if (selectedEntityId) {
    return selectedElement
      ? renderMachineElementPropertyPanels(selectedElement, machine, view.pending)
      : renderPanel("属性", `<div class="cam-empty-row">正在读取所选元素属性...</div>`);
  }

  if (machine.isLoaded) {
    return renderPanel("机床实例属性", renderMachineInstanceInspector(machine));
  }

  if (selectedMachineDefinition) {
    return renderPanel("机床定义属性", renderMachineDefinitionInspector(selectedMachineDefinition));
  }

  return renderPanel("属性", `<div class="cam-empty-row">选择一个机床定义、机床实例或场景树节点后显示属性。</div>`);
}

function renderMachineDefinitionInspector(definition = {}) {
  return renderFacts([
    ["定义", definition.name || definition.modelName || "未命名定义"],
    ["启用", definition.enabled === false ? "否" : "是"],
    ["格式", definition.format || "SDF"],
    ["版本", definition.formatVersion || "-"],
    ["模型", definition.modelName || "-"],
    ["部件", definition.linkCount ?? 0],
    ["运动轴", definition.jointCount ?? 0],
    ["显示几何", definition.visualCount ?? 0],
    ["碰撞包络", definition.collisionCount ?? 0],
    ["来源文件", definition.sourcePath || "-"],
  ]);
}

function renderMachineInstanceInspector(machine = {}) {
  return renderFacts([
    ["实例", machine.name || machine.modelName || "机床实例"],
    ["启用", machine.enabled === false ? "否" : "是"],
    ["机床定义", machine.modelName || machine.name || "-"],
    ["线性点动", `${formatNumber(getLinearJogStepMm(machine), 3)} mm`],
    ["旋转点动", `${formatNumber(getAngularJogStepDeg(machine), 3)}°`],
    ["部件", machine.links?.length ?? 0],
    ["运动轴", machine.joints?.length ?? 0],
    ["显示几何", machine.visuals?.length ?? 0],
    ["碰撞包络", machine.collisions?.length ?? 0],
    ["状态", machine.status || "Loaded"],
  ]);
}

function renderMachineElementPropertyPanels(item = {}, machine = {}, pending = false) {
  return `
    ${renderPanel("结构", renderMachineElementSourceInspector(item, machine))}
    ${renderPanel("位置 / 姿态（相对上级）", renderMachineElementTransformEditor(item, machine, pending))}
    ${renderPanel("外观", renderMachineElementAppearanceEditor(item, pending))}
    ${renderPanel("运动学 / 约束", renderMachineElementKinematicsInspector(item, machine, pending))}
    ${item.tool ? renderPanel("工具 / TCP", renderMachineToolTCPEditor(item, pending)) : ""}
  `;
}

function renderMachineResourceAccordion(machineDefinitions = [], machines = [], machine = {}, view = {}) {
  const usedDefinitionIds = new Set(
    machines
      .map((item) => String(item?.machineDefinitionId ?? "").trim())
      .filter(Boolean),
  );
  return `
    <div class="cam-accordion">
      <details class="cam-accordion-item" open>
        <summary>
          <strong>机床定义</strong>
          <span>${machineDefinitions.length} 项</span>
        </summary>
        <div class="cam-accordion-body">
          ${renderMachineDefinitionList(machineDefinitions, machine.machineDefinitionId, view.selectedMachineDefinitionId, view.pending, usedDefinitionIds)}
        </div>
      </details>
      <details class="cam-accordion-item" open>
        <summary>
          <strong>机床实例</strong>
          <span>${machines.length} 项</span>
        </summary>
        <div class="cam-accordion-body">
          ${renderMachineList(machines, getMachineId(machine))}
        </div>
      </details>
    </div>
  `;
}

function renderMachineElementSourceInspector(item = {}, machine = {}) {
  const transform = item.transform ?? {};
  const parentId = item.parentEntityId || transform.parentEntityId || "";
  const parent = findMachineElementById(machine, parentId);
  const parentName = getMachineElementDisplayName(parent);
  const childCount = Array.isArray(item.childEntityIds) ? item.childEntityIds.length : transform.childEntityIds?.length ?? 0;
  const rows = [
    ["类型", formatElementKind(item.kind)],
    ["名称", getMachineElementDisplayName(item)],
    ["下级数量", childCount],
  ];
  if (parentName) {
    rows.splice(2, 0, ["上级", parentName]);
  }
  const role = formatMachineElementRole(item);
  if (role) {
    rows.push(["结构角色", role]);
  }
  return renderFacts(rows);
}

function renderMachineElementTransformEditor(item = {}, machine = {}, pending = false) {
  const transform = item.transform ?? {};
  if (transform.hasTransform === false) {
    return `<div class="cam-empty-row">该元素没有 Transform 组件。</div>`;
  }

  const position = normalizeVector(transform.position, [0, 0, 0]);
  const rotation = normalizeVector(transform.rotationRadians, [0, 0, 0]).map((value) => value * 180 / Math.PI);
  const scale = normalizeVector(transform.scale, [1, 1, 1]);
  const policy = item.transformEditPolicy ?? {};
  const linearStep = getLinearJogStepMm(machine);
  const angularStep = getAngularJogStepDeg(machine);
  return `
    <div class="cam-transform-editor"
         data-cam-transform-editor
         data-cam-entity-id="${escapeAttr(item.entityId || "")}">
      <div class="cam-hint">${escapeText(policy.reason || "此处编辑本地位姿，即相对父对象的 Transform；输入合法后自动同步到后端并由 PDO 刷新场景。")}</div>
      <div class="cam-form-grid three">
        ${renderEditableNumberField("X mm", "data-cam-transform-position-x", position[0], resolveFieldPolicy(policy, "position", 0, pending, linearStep))}
        ${renderEditableNumberField("Y mm", "data-cam-transform-position-y", position[1], resolveFieldPolicy(policy, "position", 1, pending, linearStep))}
        ${renderEditableNumberField("Z mm", "data-cam-transform-position-z", position[2], resolveFieldPolicy(policy, "position", 2, pending, linearStep))}
      </div>
      <div class="cam-form-grid three">
        ${renderEditableNumberField("Yaw °", "data-cam-transform-rotation-yaw", rotation[0], resolveRotationDegreePolicy(policy, 0, pending, angularStep))}
        ${renderEditableNumberField("Pitch °", "data-cam-transform-rotation-pitch", rotation[1], resolveRotationDegreePolicy(policy, 1, pending, angularStep))}
        ${renderEditableNumberField("Roll °", "data-cam-transform-rotation-roll", rotation[2], resolveRotationDegreePolicy(policy, 2, pending, angularStep))}
      </div>
      <div class="cam-form-grid three">
        ${renderEditableNumberField("Scale X", "data-cam-transform-scale-x", scale[0], resolveFieldPolicy(policy, "scale", 0, true, 0.001))}
        ${renderEditableNumberField("Scale Y", "data-cam-transform-scale-y", scale[1], resolveFieldPolicy(policy, "scale", 1, true, 0.001))}
        ${renderEditableNumberField("Scale Z", "data-cam-transform-scale-z", scale[2], resolveFieldPolicy(policy, "scale", 2, true, 0.001))}
      </div>
    </div>
  `;
}

function renderMachineElementAppearanceEditor(item = {}, pending = false) {
  const appearance = item.appearance ?? {};
  const color = normalizeColorInput(appearance.colorHex || appearance.color || "#8fb8c9");
  const showCollision = appearance.showCollision !== false;
  const disabled = pending ? "disabled" : "";
  return `
    <div class="cam-appearance-editor"
         data-cam-appearance-editor
         data-cam-entity-id="${escapeAttr(item.entityId || "")}">
      <div class="cam-hint">颜色只覆盖当前选中部件，不影响子部件，也不回写机床定义资源。</div>
      <div class="cam-appearance-row">
        <label class="cam-field editable">
          <span>部件颜色</span>
          <input class="cam-color-input" type="color"
                 data-cam-machine-element-color
                 value="${escapeAttr(color)}"
                 ${disabled} />
        </label>
        <label class="cam-check-row">
          <input type="checkbox"
                 data-cam-machine-show-collider
                 ${showCollision ? "checked" : ""}
                 ${disabled} />
          <span>显示碰撞器</span>
        </label>
      </div>
    </div>
  `;
}

function renderMachineElementKinematicsInspector(item = {}, machine = {}, pending = false) {
  const rows = [];
  if (item.machine) {
    rows.push(
      ["状态", item.machine.status || "-"],
      ["工位", item.machine.workstationName || "-"],
      ["最大速度", `${formatNumber(item.machine.maxVelocity ?? 0, 2)} mm/s`],
      ["最大加速度", `${formatNumber(item.machine.maxAcceleration ?? 0, 2)} mm/s²`],
      ["线性点动", `${formatNumber(item.machine.linearJogStep ?? getLinearJogStepMm(), 3)} mm`],
      ["旋转点动", `${formatNumber(item.machine.angularJogStep ?? getAngularJogStepDeg(), 3)}°`],
    );
  }

  if (item.link) {
    rows.push(
      ["部件名称", item.link.name || "-"],
      ["自碰撞", formatBoolean(item.link.selfCollide)],
      ["重力", formatBoolean(item.link.gravity)],
      ["运动学", formatBoolean(item.link.kinematic)],
    );
  }

  if (item.joint) {
    rows.push(
      ["轴名称", item.joint.name || "-"],
      ["轴类型", formatJointType(item.joint.type)],
      ["上级部件", item.joint.parentLink || "-"],
      ["下级部件", item.joint.childLink || "-"],
      ["轴向", formatVector(item.joint.axis)],
      ["当前位置", formatJointPosition(item.joint)],
    );
  }

  const facts = rows.length ? renderFacts(rows) : `<div class="cam-empty-row">该元素没有运动学参数。</div>`;
  return item.joint ? `${facts}${renderMachineJointLimitEditor(item, machine, pending)}` : facts;
}

function renderMachineToolTCPEditor(item = {}, pending = false) {
  const tool = item.tool ?? {};
  const tcp = normalizeVector(tool.tcpLocalPosition, [0, 0, 0]);
  const beam = normalizeVector(tool.beamLocalDirection, [0, 0, -1]);
  const rows = [
    ["工具", tool.name || getMachineElementDisplayName(item)],
    ["类型", formatToolType(tool.type)],
    ["世界 TCP", formatVector(tool.tcpWorldPosition, "mm")],
    ["世界光束方向", formatVector(tool.beamWorldDirection)],
  ];
  const editPolicy = {
    editable: !pending,
    step: 1,
    precision: 3,
    reason: "TCP 是相对当前工具端元素的局部坐标，单位 mm。",
  };
  const directionPolicy = {
    editable: !pending,
    step: 0.01,
    precision: 4,
    reason: "光束方向是当前工具端元素局部坐标系下的方向向量，后端会归一化。",
  };

  return `
    ${renderFacts(rows)}
    <div class="cam-transform-editor"
         data-cam-tool-tcp-editor
         data-cam-entity-id="${escapeAttr(item.entityId || "")}">
      <div class="cam-hint">修改 TCP 后只影响当前机床实例，不回写原始 SDF；世界 TCP 由后端 Transform 派生。</div>
      <div class="cam-form-grid three">
        ${renderEditableNumberField("TCP X mm", "data-cam-tool-tcp-x", tcp[0], editPolicy)}
        ${renderEditableNumberField("TCP Y mm", "data-cam-tool-tcp-y", tcp[1], editPolicy)}
        ${renderEditableNumberField("TCP Z mm", "data-cam-tool-tcp-z", tcp[2], editPolicy)}
      </div>
      <div class="cam-form-grid three">
        ${renderEditableNumberField("Beam X", "data-cam-tool-beam-x", beam[0], directionPolicy)}
        ${renderEditableNumberField("Beam Y", "data-cam-tool-beam-y", beam[1], directionPolicy)}
        ${renderEditableNumberField("Beam Z", "data-cam-tool-beam-z", beam[2], directionPolicy)}
      </div>
    </div>
  `;
}

function renderMachineJointLimitEditor(item = {}, machine = {}, pending = false) {
  const joint = item.joint ?? {};
  if (joint.type === "fixed") {
    return `<div class="cam-empty-row">固定轴没有可编辑行程范围。</div>`;
  }

  const isRotary = isRotaryJoint(joint.type);
  const lower = Number(joint.lower ?? 0);
  const upper = Number(joint.upper ?? 0);
  const displayLower = isRotary ? lower * 180 / Math.PI : lower;
  const displayUpper = isRotary ? upper * 180 / Math.PI : upper;
  const unit = isRotary ? "°" : "mm";
  const step = isRotary ? getAngularJogStepDeg(machine) : getLinearJogStepMm(machine);
  const precision = isRotary ? 2 : 3;
  const disabled = pending ? "disabled" : "";
  const title = isRotary
    ? "编辑当前机床实例的旋转轴软限位，内部保存为弧度。"
    : "编辑当前机床实例的平移轴软限位，内部单位为 mm。";

  return `
    <div class="cam-transform-editor"
         data-cam-joint-limit-editor
         data-cam-entity-id="${escapeAttr(item.entityId || "")}"
         data-cam-joint-type="${escapeAttr(joint.type || "")}">
      <div class="cam-hint">${escapeText(title)} 修改后会保存到当前项目中的机床实例，不回写原始 SDF。</div>
      <div class="cam-form-grid two">
        ${renderEditableNumberField(`下限 ${unit}`, "data-cam-joint-lower-limit", displayLower, {
          editable: !pending,
          step,
          precision,
          reason: title,
        })}
        ${renderEditableNumberField(`上限 ${unit}`, "data-cam-joint-upper-limit", displayUpper, {
          editable: !pending,
          step,
          precision,
          reason: title,
        })}
      </div>
    </div>
  `;
}

function renderEditableNumberField(label, attrName, value, policy = {}) {
  const attributes = renderInputPolicyAttributes(policy);
  const precision = Number.isInteger(policy.precision) && policy.precision >= 0 ? policy.precision : 4;
  const stateClass = policy.editable ? "editable" : "locked";
  return `
    <label class="cam-field ${stateClass}">
      <span>${escapeText(label)}</span>
      <input class="cam-small-input" type="number" ${attrName} value="${escapeAttr(formatNumber(value, precision))}" ${attributes} />
    </label>
  `;
}

function resolveFieldPolicy(policy, groupName, index, pending, fallbackStep) {
  const group = Array.isArray(policy?.[groupName]) ? policy[groupName] : [];
  const field = group[index] ?? {};
  const editable = !pending && field.editable === true;
  return {
    editable,
    hasRange: field.hasRange === true,
    min: Number(field.min),
    max: Number(field.max),
    step: Number.isFinite(Number(field.step)) ? Number(field.step) : fallbackStep,
    precision: Number.isInteger(Number(field.precision)) && Number(field.precision) >= 0 ? Number(field.precision) : 4,
    reason: field.reason || "",
  };
}

function resolveRotationDegreePolicy(policy, index, pending, fallbackStep) {
  const field = resolveFieldPolicy(policy, "rotationRadians", index, pending, fallbackStep * Math.PI / 180);
  return {
    ...field,
    min: Number.isFinite(field.min) ? field.min * 180 / Math.PI : field.min,
    max: Number.isFinite(field.max) ? field.max * 180 / Math.PI : field.max,
    step: Number.isFinite(field.step) ? field.step * 180 / Math.PI : fallbackStep,
    precision: Math.max(0, Math.min(4, field.precision ?? 2)),
  };
}

function renderInputPolicyAttributes(policy = {}) {
  const precision = Number.isInteger(policy.precision) && policy.precision >= 0 ? policy.precision : 4;
  const attrs = [`step="${escapeAttr(formatNumber(policy.step ?? 1, precision))}"`];
  if (!policy.editable) {
    attrs.push("disabled");
  }
  if (policy.hasRange && Number.isFinite(policy.min)) {
    attrs.push(`min="${escapeAttr(formatNumber(policy.min, precision))}"`);
  }
  if (policy.hasRange && Number.isFinite(policy.max)) {
    attrs.push(`max="${escapeAttr(formatNumber(policy.max, precision))}"`);
  }
  if (policy.reason) {
    attrs.push(`title="${escapeAttr(policy.reason)}"`);
  }
  return attrs.join(" ");
}

function renderFacts(rows) {
  return `
    <dl class="cam-facts">
      ${rows.map(([label, value]) => `<dt>${escapeText(label)}</dt><dd>${escapeText(formatFactValue(value))}</dd>`).join("")}
    </dl>
  `;
}

function formatFactValue(value) {
  if (Array.isArray(value)) {
    return value.map((item) => formatFactValue(item)).join(", ");
  }
  return value ?? "-";
}

function normalizeColorInput(value) {
  const text = String(value ?? "").trim();
  const match = /^#?([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/.exec(text);
  return match ? `#${match[1].toLowerCase()}` : "#8fb8c9";
}

function findMachineElementById(machine = {}, entityId = "") {
  const id = String(entityId || "").trim();
  if (!id) {
    return null;
  }

  if (String(machine.entityId || "") === id) {
    return {
      kind: "machine",
      name: machine.name || machine.modelName || "机床实例",
    };
  }

  const elements = Array.isArray(machine.elements) ? machine.elements : [];
  return elements.find((element) => String(element?.entityId || "") === id) || null;
}

function getMachineElementDisplayName(item = {}) {
  if (!item) {
    return "";
  }
  return item.name
    || item.link?.name
    || item.joint?.name
    || item.visual?.name
    || item.collision?.name
    || "-";
}

function formatMachineElementRole(item = {}) {
  if (item.kind === "machine") {
    return "机床实例根";
  }
  if (item.joint?.type) {
    return formatJointType(item.joint.type);
  }
  if (item.hasTool || item.tool) {
    return "工具端";
  }
  if (item.hasJoint) {
    return "运动轴";
  }
  if (item.hasVisual && item.hasCollision) {
    return "显示与碰撞部件";
  }
  if (item.hasVisual || item.hasRender) {
    return "显示部件";
  }
  if (item.hasCollision) {
    return "碰撞部件";
  }
  return "结构部件";
}

function formatElementKind(kind) {
  if (kind === "machine") {
    return "机床实例";
  }
  if (kind === "link") {
    return "机床部件";
  }
  if (kind === "part") {
    return "机床部件";
  }
  if (kind === "visual") {
    return "显示元素";
  }
  if (kind === "collision") {
    return "碰撞元素";
  }
  return kind || "-";
}

function formatJointType(type) {
  if (type === "fixed") {
    return "固定轴";
  }
  if (type === "revolute") {
    return "旋转轴";
  }
  if (type === "continuous") {
    return "连续旋转轴";
  }
  if (type === "prismatic") {
    return "平移轴";
  }
  return type || "-";
}

function formatToolType(type) {
  if (type === "laser_cutting_head") {
    return "激光切割头";
  }
  return type || "工具";
}

function formatJointPosition(joint = {}) {
  const value = Number(joint.position ?? 0);
  if (!Number.isFinite(value)) {
    return "-";
  }

  if (isRotaryJoint(joint.type)) {
    return `${formatNumber(value * 180 / Math.PI, 2)}°`;
  }
  if (joint.type === "prismatic") {
    return `${formatNumber(value, 3)} mm`;
  }
  return formatNumber(value, 4);
}

function formatJointRange(joint = {}) {
  if (joint.type === "fixed") {
    return "-";
  }

  const lower = Number(joint.lower ?? 0);
  const upper = Number(joint.upper ?? 0);
  if (!Number.isFinite(lower) || !Number.isFinite(upper)) {
    return "-";
  }

  if (isRotaryJoint(joint.type)) {
    return `${formatNumber(lower * 180 / Math.PI, 2)}° ~ ${formatNumber(upper * 180 / Math.PI, 2)}°`;
  }
  if (joint.type === "prismatic") {
    return `${formatNumber(lower, 3)} ~ ${formatNumber(upper, 3)} mm`;
  }
  return `${formatNumber(lower, 4)} ~ ${formatNumber(upper, 4)}`;
}

function isRotaryJoint(type) {
  return type === "revolute" || type === "continuous";
}

function formatBoolean(value) {
  if (value === true) {
    return "是";
  }
  if (value === false) {
    return "否";
  }
  return "-";
}

function formatVector(values, unit = "") {
  if (!Array.isArray(values) || !values.length) {
    return "-";
  }
  const suffix = unit ? ` ${unit}` : "";
  return values.map((value) => formatNumber(value, 3)).join(", ") + suffix;
}

function formatRotation(values) {
  if (!Array.isArray(values) || values.length < 3) {
    return "-";
  }
  const labels = ["Yaw", "Pitch", "Roll"];
  return values.slice(0, 3)
    .map((value, index) => `${labels[index]} ${formatNumber(Number(value) * 180 / Math.PI, 2)}°`)
    .join(", ");
}

function normalizeVector(values, fallback) {
  return fallback.map((defaultValue, index) => {
    const value = Number(values?.[index]);
    return Number.isFinite(value) ? value : defaultValue;
  });
}
