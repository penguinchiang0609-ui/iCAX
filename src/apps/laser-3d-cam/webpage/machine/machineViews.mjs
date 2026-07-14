import { getMachineId } from "../state/sceneSelectors.mjs";
import { escapeAttr, escapeText, formatNumber, getArrayValue } from "../utils/format.mjs";
export function renderMachineDefinitionList(definitions = [], activeDefinitionId = "", selectedDefinitionId = "", pending = false, usedDefinitionIds = new Set()) {
  if (!definitions.length) {
    return `<div class="cam-empty-row">未导入机床定义。请导入 SDF/XML 机床定义文件。</div>`;
  }

  return `
    <div class="cam-list">
      ${definitions.map((definition) => {
        const id = String(definition.id ?? definition.entityId ?? "");
        const enabled = definition.enabled !== false;
        const isActive = id && id === String(activeDefinitionId ?? "");
        const isSelected = id && id === String(selectedDefinitionId || activeDefinitionId || "");
        const isUsed = usedDefinitionIds?.has?.(id) === true;
        return `
          <div class="cam-list-row ${isActive ? "active" : ""} ${isSelected ? "selected" : ""} ${enabled ? "" : "disabled"}"
               data-cam-machine-definition-id="${escapeAttr(id)}"
               data-cam-definition-enabled="${enabled ? "true" : "false"}"
               role="button"
               tabindex="0">
            <strong>${escapeText(definition.name || definition.modelName || id)}</strong>
            <small>${escapeText(definition.sourcePath || "SDF 机床定义")}</small>
            <div class="cam-list-actions">
              <span>${enabled ? "已启用" : "已禁用"}${isActive ? " · 当前实例" : isSelected ? " · 已选中" : ""}</span>
              <button class="tool-button" type="button"
                      data-cam-action="instantiate-machine-definition"
                      data-cam-definition-id="${escapeAttr(id)}"
                      ${enabled && !pending ? "" : "disabled"}>新增实例</button>
              <button class="tool-button" type="button"
                      data-cam-action="toggle-machine-definition"
                      data-cam-definition-id="${escapeAttr(id)}"
                      data-cam-next-enabled="${enabled ? "false" : "true"}"
                      ${pending ? "disabled" : ""}>${enabled ? "禁用" : "启用"}</button>
              <button class="tool-button danger" type="button"
                      data-cam-action="delete-machine-definition"
                      data-cam-definition-id="${escapeAttr(id)}"
                      title="${escapeAttr(isUsed ? "该定义已有实例，不能删除" : "删除未使用的机床定义")}"
                      ${pending || isUsed ? "disabled" : ""}>删除</button>
            </div>
          </div>
        `;
      }).join("")}
    </div>
  `;
}

export function renderMachineList(machines = [], selectedMachineId = "") {
  if (!machines.length) {
    return `<div class="cam-empty-row">还没有机床实例。请选择机床定义后新增实例。</div>`;
  }

  return `
    <div class="cam-list">
      ${machines.map((item, index) => {
        const id = getMachineId(item);
        const enabled = item?.enabled !== false;
        const isSelected = id && id === String(selectedMachineId ?? "");
        return `
          <div class="cam-list-row ${isSelected ? "selected" : ""} ${enabled ? "" : "disabled"}"
               data-cam-machine-instance-id="${escapeAttr(id)}"
               role="button"
               tabindex="0">
            <strong>${escapeText(index + 1)}. ${escapeText(item.name || item.modelName || id)}</strong>
            <small>${escapeText(item.machineDefinitionId || item.sourcePath || "机床实例")}</small>
            <div class="cam-list-actions">
              <span>${enabled ? "已启用" : "已禁用"}${isSelected ? " · 正在编辑" : ""}</span>
              <button class="tool-button" type="button"
                      data-cam-action="toggle-machine-instance"
                      data-cam-machine-id="${escapeAttr(id)}"
                      data-cam-next-enabled="${enabled ? "false" : "true"}">${enabled ? "禁用" : "启用"}</button>
            </div>
          </div>
        `;
      }).join("")}
    </div>
  `;
}

export function renderMachineDefinitionSummary(machine = {}) {
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">场景中还没有机床实例。可以从左侧定义列表选择并实例化。</div>`;
  }
  return `
    <dl class="cam-facts">
      <dt>实例</dt><dd>${escapeText(machine.entityId || "-")}</dd>
      <dt>启用</dt><dd>${machine.enabled === false ? "否" : "是"}</dd>
      <dt>定义资源</dt><dd>${escapeText(machine.machineResourceId || machine.resourceId || "-")}</dd>
      <dt>模型</dt><dd>${escapeText(machine.modelName || machine.name || "-")}</dd>
      <dt>部件</dt><dd>${escapeText(machine.links?.length ?? 0)}</dd>
      <dt>运动轴</dt><dd>${escapeText(machine.joints?.length ?? 0)}</dd>
      <dt>显示几何</dt><dd>${escapeText(machine.visuals?.length ?? 0)}</dd>
      <dt>碰撞包络</dt><dd>${escapeText(machine.collisions?.length ?? 0)}</dd>
      <dt>状态</dt><dd>${escapeText(machine.status || "NotLoaded")}</dd>
      <dt>检查</dt><dd>${escapeText(machine.lastCheckResult || "-")}</dd>
      <dt>工位</dt><dd>${escapeText(machine.workstationName || "默认工位")}</dd>
    </dl>
  `;
}

export function renderMachineSceneTree(machine = {}, selectedEntityId = "", selectedEntityIds = []) {
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">实例化机床后显示机床结构树。</div>`;
  }

  const root = buildMachineStructureTree(machine);
  const selectedIds = new Set((selectedEntityIds.length ? selectedEntityIds : [selectedEntityId])
    .map((id) => String(id ?? "").trim())
    .filter(Boolean));

  return `<div class="cam-tree">${renderMachineTreeNode(root, 0, String(selectedEntityId ?? "").trim(), selectedIds)}</div>`;
}

function buildMachineStructureTree(machine = {}) {
  const machineId = String(machine.entityId || machine.id || "machine");
  const elements = Array.isArray(machine.elements) && machine.elements.length
    ? machine.elements
    : buildFallbackMachineElements(machine);
  const nodesById = new Map();

  for (const item of elements) {
    const entityId = String(item?.entityId || "").trim();
    if (!entityId) {
      continue;
    }
    nodesById.set(entityId, {
      ...item,
      entityId,
      kind: item.kind || "element",
      name: item.name || item.linkName || item.visualName || item.collisionName || entityId,
      childEntityIds: Array.isArray(item.childEntityIds) ? item.childEntityIds.map((id) => String(id)) : [],
      children: [],
    });
  }

  if (!nodesById.has(machineId)) {
    nodesById.set(machineId, {
      entityId: machineId,
      machineId,
      parentEntityId: "",
      childEntityIds: [],
      name: machine.name || machine.modelName || "Machine",
      kind: "machine",
      hasTransform: true,
      children: [],
    });
  }

  const root = nodesById.get(machineId);
  const attached = new Set([root.entityId]);
  for (const node of nodesById.values()) {
    for (const childId of node.childEntityIds ?? []) {
      const child = nodesById.get(String(childId));
      if (child && child !== node && !node.children.includes(child)) {
        node.children.push(child);
        attached.add(child.entityId);
      }
    }
  }

  for (const node of nodesById.values()) {
    if (node === root || attached.has(node.entityId)) {
      continue;
    }
    const parent = nodesById.get(String(node.parentEntityId || "")) ?? root;
    if (!parent.children.includes(node)) {
      parent.children.push(node);
    }
  }
  return root;
}

function buildFallbackMachineElements(machine = {}) {
  const machineId = String(machine.entityId || machine.id || "machine");
  const elements = [{
    entityId: machineId,
    machineId,
    parentEntityId: "",
    childEntityIds: [],
    name: machine.name || machine.modelName || "Machine",
    kind: "machine",
    hasTransform: true,
  }];
  for (const [items, kind] of [
    [machine.links, "link"],
    [machine.visuals, "visual"],
    [machine.collisions, "collision"],
  ]) {
    for (const item of items ?? []) {
      elements.push({
        ...item,
        kind: item.kind || kind,
        name: item.name || item.linkName || item.visualName || item.collisionName,
      });
    }
  }
  return elements;
}

export function renderMachineTreeNode(node, depth, selectedEntityId = "", selectedEntityIds = new Set()) {
  const children = node.children ?? [];
  const kind = node.kind || "node";
  const entityId = String(node.entityId || "").trim();
  const isPrimarySelected = entityId === String(selectedEntityId || "");
  const isSelected = selectedEntityIds.has(entityId);
  const badges = [
    node.hasJoint ? "轴" : "",
    node.hasRender ? "显" : "",
    node.hasCollision ? "碰" : "",
  ].filter(Boolean).join(" ");
  return `
    <div class="cam-tree-node" style="--depth:${depth}">
      <button class="cam-tree-row ${isSelected ? "selected" : ""}" type="button"
              data-cam-machine-pick
              data-cam-machine-kind="machine.${escapeAttr(kind)}"
              data-cam-machine-entity-id="${escapeAttr(entityId)}"
              data-cam-machine-id="${escapeAttr(node.sourceIndex ?? 0)}"
              data-cam-machine-root-id="${escapeAttr(node.machineId || "")}"
              data-cam-selected-machine-node="${isPrimarySelected ? "true" : "false"}"
              data-cam-subtree-selected="${isSelected ? "true" : "false"}"
              data-cam-machine-label="${escapeAttr(node.name || kind)}">
        <span>${escapeText(formatMachineElementKind(kind))}</span>
        <strong>${escapeText(node.name || kind)}</strong>
        <small>${escapeText(badges || children.length)}</small>
      </button>
      ${children.map((child) => renderMachineTreeNode(child, depth + 1, selectedEntityId, selectedEntityIds)).join("")}
    </div>
  `;
}

function formatMachineElementKind(kind) {
  if (kind === "machine") {
    return "机床";
  }
  if (kind === "link") {
    return "Link";
  }
  if (kind === "part") {
    return "部件";
  }
  if (kind === "visual") {
    return "Visual";
  }
  if (kind === "collision") {
    return "Collision";
  }
  return kind || "Element";
}

export function renderMachineLinkList(machine = {}) {
  const links = machine.links ?? [];
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机床定义导入后显示 Link。</div>`;
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
                  data-cam-machine-entity-id="${escapeAttr(link?.entityId || "")}"
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

export function renderMachineJointStructureList(machine = {}) {
  const joints = machine.joints ?? [];
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机床定义导入后显示 Joint。</div>`;
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
                  data-cam-machine-entity-id="${escapeAttr(joint?.entityId || "")}"
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

export function renderMachineVisualList(machine = {}) {
  const visuals = machine.visuals ?? [];
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机床定义导入后显示 Visual。</div>`;
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
                  data-cam-machine-kind="machine.part"
                  data-cam-machine-entity-id="${escapeAttr(visual?.entityId || "")}"
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

export function renderMachineMaterialSummary(machine = {}) {
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机床定义导入后显示材质摘要。</div>`;
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

export function describeMaterial(material) {
  if (!material) {
    return "默认材质";
  }
  if (material.materialResourceId) {
    return material.colorRgba !== undefined
      ? `${formatRgba(material.colorRgba)} · ${material.lighting === false ? "无光照" : "光照"}`
      : material.materialResourceId;
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

function formatRgba(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) {
    return "-";
  }
  return `#${(number >>> 0).toString(16).padStart(8, "0").toUpperCase()}`;
}

export function renderMachineJointList(machine = {}) {
  if (!machine.isLoaded) {
    return `<div class="cam-empty-row">机床定义导入后显示轴位置。</div>`;
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

export function renderMachineAxisOptions(machine = {}) {
  const names = machine.jointNames?.length ? machine.jointNames : ["X", "Y", "Z", "A", "C"];
  return names.map((name) => `<option value="${escapeAttr(name)}">${escapeText(name)}</option>`).join("");
}

export function renderNumberField(label, attrName, value) {
  return `
    <label class="cam-field">
      <span>${escapeText(label)}</span>
      <input class="cam-small-input" type="number" step="0.001" ${attrName} value="${escapeAttr(formatNumber(value, 3))}" />
    </label>
  `;
}
