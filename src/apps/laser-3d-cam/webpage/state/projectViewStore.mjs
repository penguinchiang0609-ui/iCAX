const projectViews = new Map();

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
      layout: {
        leftWidth: 320,
        rightWidth: 340,
      },
    });
  }
  return projectViews.get(projectId);
}
