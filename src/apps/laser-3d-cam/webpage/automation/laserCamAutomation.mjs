import { getEnabledMachines, getJobMachineId, getMachineDefinitions, getMachineId, getMachines } from "../state/sceneSelectors.mjs";

export function exposeLaserCamAutomation(context, view, commands) {
  window.__icaxLaser3DCAM ??= {};
  window.__icaxLaser3DCAM.getState = () => ({
    projectId: context.project?.projectId ?? "",
    sceneId: context.sceneProxy?.sceneId ?? "",
    selectedMachineDefinitionId: view.selectedMachineDefinitionId ?? "",
    selectedMachineInstanceId: view.selectedMachineInstanceId ?? "",
    machineDefinitionCount: getMachineDefinitions(view.scene).length,
    machineInstanceCount: getMachines(view.scene).length,
    enabledMachineInstanceCount: getEnabledMachines(view.scene).length,
    machineInstances: getMachines(view.scene).map((machine) => ({
      id: getMachineId(machine),
      name: machine.name ?? machine.modelName ?? "",
      enabled: machine.enabled !== false,
    })),
    jobMachineId: getJobMachineId(view.scene),
    error: view.error ?? "",
  });
  window.__icaxLaser3DCAM.getViewportDebugState = (options = {}) =>
    view.viewport?.getDebugState?.({
      samplePixels: options.samplePixels ?? true,
      includeObjects: options.includeObjects ?? false,
    }) ?? null;
  window.__icaxLaser3DCAM.waitForRenderableViewport = async (options = {}) =>
    waitForRenderableViewport(view, options);
  window.__icaxLaser3DCAM.importMachineDefinition = async (sourcePath) => {
    await commands.importMachineDefinition(context, view, sourcePath);
    return {
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true }),
    };
  };
  window.__icaxLaser3DCAM.fitView = async () => {
    await commands.fitView(context, view);
    return window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true });
  };
  window.__icaxLaser3DCAM.setMachineInstanceEnabled = async (machineEntityId, enabled) => {
    await commands.setMachineInstanceEnabled(context, view, machineEntityId, enabled);
    return window.__icaxLaser3DCAM.getState();
  };
  window.__icaxLaser3DCAM.setJobMachine = async (machineEntityId) => {
    await commands.setJobMachine(context, view, machineEntityId);
    return window.__icaxLaser3DCAM.getState();
  };
  window.__icaxLaser3DCAM.pickMachineObject = async (payload) => {
    const result = await commands.pickMachineObject(context, view, payload);
    return {
      result,
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true, includeObjects: true }),
    };
  };
  window.__icaxLaser3DCAM.setMachineElementAppearance = async (payload) => {
    const result = await commands.setMachineElementAppearance(context, view, payload);
    return {
      result,
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true, includeObjects: true }),
    };
  };
  window.__icaxLaser3DCAM.setStandardView = async (viewName) => {
    const result = await commands.setStandardCameraView(context, view, viewName);
    return {
      result,
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true, includeObjects: true }),
    };
  };
  window.__icaxLaser3DCAM.getMachineDomState = () => {
    const viewCube = document.querySelector("[data-cam-viewcube-cube]");
    const axisGizmo = document.querySelector(".icax-three-axis-gizmo");
    const axisStyle = axisGizmo ? window.getComputedStyle(axisGizmo) : null;
    return {
      hasMachineImportPathInput: Boolean(document.querySelector("[data-cam-machine-path]")),
      accordionCount: document.querySelectorAll(".cam-accordion-item").length,
      viewCubeButtonCount: document.querySelectorAll(".cam-viewcube [data-cam-action='view-standard']").length,
      viewCubeExternalHotZoneCount: document.querySelectorAll(".cam-viewcube-edge, .cam-viewcube-corner").length,
      viewCubePitch: viewCube?.style.getPropertyValue("--viewcube-pitch") ?? "",
      viewCubeYaw: viewCube?.style.getPropertyValue("--viewcube-yaw") ?? "",
      hasViewCubeEmbeddedAxis: Boolean(document.querySelector(".cam-viewcube-axis")),
      hasAxisGizmo: Boolean(axisGizmo),
      axisGizmoLeft: axisStyle?.left ?? "",
      axisGizmoBottom: axisStyle?.bottom ?? "",
      hasAppearanceEditor: Boolean(document.querySelector("[data-cam-appearance-editor]")),
      hasCollisionToggle: Boolean(document.querySelector("[data-cam-machine-show-collider]")),
      selectedTreeRows: document.querySelectorAll(".cam-tree-row.selected").length,
    };
  };
  window.__icaxLaser3DCAM.currentProjectId = context.project?.projectId ?? "";
}

async function waitForRenderableViewport(view, options = {}) {
  const timeoutMs = Number(options.timeoutMs ?? 15000);
  const includeObjects = options.includeObjects ?? true;
  const deadline = performance.now() + Math.max(1000, timeoutMs);
  let lastState = null;
  while (performance.now() < deadline) {
    if (view.viewport?.refreshAll) {
      await Promise.race([view.viewport.refreshAll(), delay(500)]);
    }
    await delay(80);
    lastState = view.viewport?.getDebugState?.({ samplePixels: true, includeObjects }) ?? null;
    if (
      Number(lastState?.geometryCount ?? 0) > 0
      && Number(lastState?.objectCount ?? 0) > 0
      && Number(lastState?.visibleObjectCount ?? 0) > 0
      && Number(lastState?.cameraCount ?? 0) > 0
      && lastState?.activeCameraId
    ) {
      return lastState;
    }
  }
  return lastState;
}

function delay(durationMs) {
  return new Promise((resolve) => window.setTimeout(resolve, durationMs));
}
