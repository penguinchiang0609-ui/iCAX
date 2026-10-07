const projectViews = new Map();

const DEFAULT_AREA_LAYOUT = Object.freeze({
  leftWidth: 320,
  rightWidth: 340,
});

export function getProjectView(projectId) {
  if (!projectViews.has(projectId)) {
    projectViews.set(projectId, {
      disposed: false,
      scene: null,
      pending: false,
      error: "",
      notice: "",
      machineSourcePath: "",
      selectedMachineDefinitionId: "",
      selectedMachineInstanceId: "",
      selectedSceneObjectId: "",
      sourcePath: "",
      cadIntentRecognized: false,
      recognizedCADIntentResourceId: "",
      selectedCADIntentNodeId: "",
      cadIntentPreviewNodeId: "",
      cadIntentPreviewRequestId: 0,
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

export function findProjectView(projectId) {
  return projectViews.get(projectId) ?? null;
}

// Remove the session immediately. A reopened file keeps its project ID but
// must receive fresh scene state, drafts and viewport readers.
export async function releaseProjectView(projectId) {
  const view = findProjectView(projectId);
  if (!view) return false;
  view.disposed = true;
  projectViews.delete(projectId);
  if (view.noticeDismissTimer != null) globalThis.clearTimeout(view.noticeDismissTimer);
  view.noticeDismissTimer = null;
  view.notice = "";
  const cleanups = [];
  const release = (resource, method) => {
    if (typeof resource?.[method] !== "function") return;
    try { cleanups.push(Promise.resolve(resource[method]())); } catch {}
  };
  for (const area of Object.values(view.areas ?? {})) {
    release(area.viewReader, "stop");
    area.viewReader = null;
    area.viewContent = null;
    area.viewContentRequest = null;
    area.viewApplyByRevision?.clear?.();
  }
  release(view.viewport, "dispose");
  view.viewport = null;
  view.viewportSceneProxy = null;
  view.sceneProxy = null;
  view.scene = null;
  await Promise.allSettled(cleanups);
  return true;
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
    snapshot: payload,
    rows: Array.isArray(payload.rows) ? payload.rows : [],
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
    viewReader: null,
    viewApplyQueue: Promise.resolve(),
    viewApplyByRevision: new Map(),
    appliedViewRevision: "0",
  };
  return view.areas[areaId];
}
