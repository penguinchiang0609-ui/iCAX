export function getMachineDefinitionId(definition) {
  if (!definition || typeof definition !== "object") {
    return "";
  }
  return String(definition.id ?? definition.entityId ?? "").trim();
}

export function getMachineDefinitions(scene = {}) {
  scene ??= {};
  return Array.isArray(scene.machineDefinitions) ? scene.machineDefinitions.filter(Boolean) : [];
}

export function getSelectedMachineDefinition(scene = {}, view = {}) {
  scene ??= {};
  view ??= {};
  const definitions = getMachineDefinitions(scene);
  const selectedId = view.selectedMachineDefinitionId || getSelectedMachine(scene, view).machineDefinitionId || "";
  return definitions.find((definition) => getMachineDefinitionId(definition) === String(selectedId)) ?? definitions[0] ?? null;
}

export function getMachineId(machine = {}) {
  return String(machine?.entityId ?? machine?.id ?? "").trim();
}

export function getMachines(scene = {}) {
  scene ??= {};
  return Array.isArray(scene.machines) ? scene.machines : [];
}

export function getEnabledMachines(scene = {}) {
  return getMachines(scene).filter((machine) => machine?.enabled !== false);
}

export function getJobMachineId(scene = {}) {
  scene ??= {};
  return String(scene?.job?.machineEntityId ?? "").trim();
}

export function getJobMachine(scene = {}) {
  scene ??= {};
  const jobMachineId = getJobMachineId(scene);
  if (!jobMachineId) {
    return {};
  }
  return getMachines(scene).find((item) => getMachineId(item) === jobMachineId) ?? {};
}

export function getSelectedMachine(scene = {}, view = {}) {
  scene ??= {};
  view ??= {};
  const machines = getMachines(scene);
  const selectedId = String(view.selectedMachineInstanceId ?? "").trim();
  const jobMachineId = getJobMachineId(scene);
  const preferredId = selectedId || jobMachineId;
  if (preferredId) {
    const machine = machines.find((item) => getMachineId(item) === preferredId);
    if (machine) {
      return machine;
    }
  }
  return machines[0] ?? {};
}

export function reconcileSelectedMachine(view, scene = {}) {
  scene ??= {};
  view ??= {};
  const machines = getMachines(scene);
  const selectedId = String(view.selectedMachineInstanceId ?? "").trim();
  if (selectedId && machines.some((item) => getMachineId(item) === selectedId)) {
    return;
  }

  const fallbackId = getJobMachineId(scene) || getMachineId(machines[0]);
  view.selectedMachineInstanceId = fallbackId;
}

export function getSelectedMachineId(scene = {}, view = {}) {
  return getMachineId(getSelectedMachine(scene, view));
}

export function getMachineSubtreeEntityIds(machine = {}, rootEntityId = "") {
  machine ??= {};
  const rootId = String(rootEntityId ?? "").trim();
  if (!rootId) {
    return [];
  }

  const machineId = getMachineId(machine);
  const childIdsById = new Map();
  const ensureNode = (id) => {
    const key = String(id ?? "").trim();
    if (!key) {
      return "";
    }
    if (!childIdsById.has(key)) {
      childIdsById.set(key, new Set());
    }
    return key;
  };

  ensureNode(machineId);
  const elements = Array.isArray(machine.elements) ? machine.elements : [];
  for (const item of elements) {
    const entityId = ensureNode(item?.entityId);
    if (!entityId) {
      continue;
    }
    for (const childId of item?.childEntityIds ?? []) {
      const child = ensureNode(childId);
      if (child) {
        childIdsById.get(entityId).add(child);
      }
    }
    const parentId = ensureNode(item?.parentEntityId);
    if (parentId && parentId !== entityId) {
      childIdsById.get(parentId).add(entityId);
    }
  }

  if (rootId === machineId && !childIdsById.get(machineId)?.size) {
    for (const item of elements) {
      const entityId = ensureNode(item?.entityId);
      if (entityId && entityId !== machineId) {
        childIdsById.get(machineId).add(entityId);
      }
    }
  }

  if (!childIdsById.has(rootId)) {
    return [rootId];
  }

  const result = [];
  const visited = new Set();
  const visit = (id) => {
    const key = String(id ?? "").trim();
    if (!key || visited.has(key)) {
      return;
    }
    visited.add(key);
    result.push(key);
    for (const childId of childIdsById.get(key) ?? []) {
      visit(childId);
    }
  };
  visit(rootId);
  return result;
}

export function getFirstEnabledMachineDefinitionId(scene = {}) {
  scene ??= {};
  const definitions = getMachineDefinitions(scene);
  const definition = definitions.find((item) => item.enabled !== false) ?? definitions[0] ?? null;
  return definition ? getMachineDefinitionId(definition) : "";
}

export function findMachineDefinition(scene = {}, machineDefinitionId = "") {
  scene ??= {};
  const id = String(machineDefinitionId ?? "").trim();
  if (!id) {
    return null;
  }
  return getMachineDefinitions(scene).find((definition) => getMachineDefinitionId(definition) === id) ?? null;
}

export function normalizeFilePath(value) {
  return String(value ?? "").replaceAll("\\", "/").toLowerCase();
}

export function getImportedMachineDefinitionId(payload = {}, scene = {}) {
  const importedId = String(payload?.machineDefinitionId ?? payload?.definition?.id ?? payload?.definition?.entityId ?? "").trim();
  if (importedId && findMachineDefinition(scene, importedId)) {
    return importedId;
  }

  const sourcePath = normalizeFilePath(payload?.sourcePath ?? payload?.definition?.sourcePath ?? "");
  if (sourcePath) {
    const bySource = getMachineDefinitions(scene).find((definition) => {
      return normalizeFilePath(definition.sourcePath) === sourcePath;
    });
    const bySourceId = getMachineDefinitionId(bySource);
    if (bySourceId) {
      return bySourceId;
    }
  }

  return getFirstEnabledMachineDefinitionId(scene);
}
