const projectViews = new Map();

const DEFAULT_AREA_LAYOUT = Object.freeze({
  leftWidth: 320,
  rightWidth: 340,
});

export function getProjectView(projectId) {
  if (!projectViews.has(projectId)) {
    projectViews.set(projectId, {
      scene: null,
      pending: false,
      error: "",
      notice: "",
      machineSourcePath: "",
      selectedMachineDefinitionId: "",
      selectedMachineInstanceId: "",
      selectedSceneObjectId: "",
      sourcePath: "",
      viewport: null,
      viewportSceneProxy: null,
      progress: null,
      activeAreaId: "",
      areas: {},
      layout: { ...DEFAULT_AREA_LAYOUT },
    });
  }
  return projectViews.get(projectId);
}

export function activateProjectArea(view, areaId) {
  const nextAreaId = String(areaId || "machine");
  view.areas ??= {};
  if (view.activeAreaId === nextAreaId) {
    return getOrCreateArea(view, nextAreaId);
  }
  if (view.activeAreaId && view.activeAreaId !== nextAreaId) {
    const previous = getOrCreateArea(view, view.activeAreaId);
    previous.layout = { ...(view.layout ?? DEFAULT_AREA_LAYOUT) };
    previous.selectedSceneObjectId = String(view.selectedSceneObjectId ?? "");
    previous.selectedMachineInstanceId = String(view.selectedMachineInstanceId ?? "");
  }
  const next = getOrCreateArea(view, nextAreaId);
  view.activeAreaId = nextAreaId;
  view.layout = { ...next.layout };
  view.selectedSceneObjectId = next.selectedSceneObjectId;
  view.selectedMachineInstanceId = next.selectedMachineInstanceId;
  return next;
}

export function getProjectArea(view, areaId) {
  view.areas ??= {};
  return getOrCreateArea(view, String(areaId || "machine"));
}

export function setProjectAreaViewContent(view, areaId, payload = {}) {
  const area = getProjectArea(view, areaId);
  const objects = Array.isArray(payload.objects) ? payload.objects : [];
  const entityIds = Array.isArray(payload.entityIds)
    ? payload.entityIds
    : objects.map((object) => object?.entityId);
  area.viewContent = {
    revision: String(payload.revision ?? "0"),
    entityIds: new Set(entityIds
      .map((entityId) => String(entityId ?? "").trim())
      .filter(Boolean)),
  };
  return area.viewContent;
}

function getOrCreateArea(view, areaId) {
  view.areas[areaId] ??= {
    layout: { ...DEFAULT_AREA_LAYOUT },
    selectedSceneObjectId: "",
    selectedMachineInstanceId: "",
    viewContent: null,
    viewContentRequest: null,
    entityViewReader: null,
  };
  return view.areas[areaId];
}
