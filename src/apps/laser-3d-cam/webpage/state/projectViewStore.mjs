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
  area.viewContent = {
    viewDefinitionId: String(payload.viewDefinitionId ?? ""),
    revision: Number(payload.revision ?? 0),
    entityIds: new Set(objects
      .map((object) => String(object?.entityId ?? "").trim())
      .filter(Boolean)),
    renderOverrides: new Map(objects
      .filter((object) => object?.presentation && typeof object.presentation === "object")
      .map((object) => [String(object.entityId ?? "").trim(), object.presentation])),
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
  };
  return view.areas[areaId];
}
