import { loadPreviewMeshResource } from "./previewMeshResource.mjs";

const LIVE_PREVIEW_DEBOUNCE_MS = 180;

export function scheduleCADIntentParameterLivePreview(context, view) {
  queueMicrotask(() => {
    if (view.tubeWorkspaceMode !== "editor" || !view.tubeEditorSceneProxy) return;
    const forms = context.mount?.querySelectorAll?.("[data-tube-parameter-form]") ?? [];
    for (const form of forms) {
      if (form.dataset.tubeLivePreviewReady === "1") continue;
      form.dataset.tubeLivePreviewReady = "1";
      for (const input of form.querySelectorAll("[data-tube-parameter]:not([disabled])")) {
        input.addEventListener("input", () => {
          queueCADIntentParameterLivePreview(context, view, form);
        });
      }
    }
  });
}

export function handleTubeAreaAction(context, view, action, target, ops) {
  if (action === "tube-open-part-editor") {
    void openPartEditor(context, view, ops);
    return true;
  }
  if (action === "tube-close-part-editor") {
    void closePartEditor(context, view, ops);
    return true;
  }
  if (action === "tube-add-workpiece") {
    view.workpieceImportDialogOpen = true;
    view.workpieceImportDraft = null;
    ops.renderProject(context, view);
    void chooseWorkpieceImportFile(context, view, ops);
    return true;
  }
  if (action === "tube-choose-import-file") {
    void chooseWorkpieceImportFile(context, view, ops);
    return true;
  }
  if (action === "tube-cancel-import") {
    view.workpieceImportDialogOpen = false;
    view.workpieceImportDraft = null;
    ops.renderProject(context, view);
    return true;
  }
  if (action === "tube-confirm-import") {
    void confirmWorkpieceImport(context, view, ops);
    return true;
  }
  if (action === "tube-delete-workpiece") {
    void deleteWorkpiece(context, view, target, ops);
    return true;
  }
  if (action === "tube-select-filtered-workpieces") {
    view.tubeSelectedWorkpieceIds = getVisibleWorkpieceIds(context);
    ops.renderProject(context, view);
    return true;
  }
  if (action === "tube-invert-filtered-workpieces") {
    const selected = new Set((view.tubeSelectedWorkpieceIds ?? []).map(String));
    view.tubeSelectedWorkpieceIds = getVisibleWorkpieceIds(context)
      .filter((entityId) => !selected.has(entityId));
    ops.renderProject(context, view);
    return true;
  }
  if (action === "tube-workpiece-page") {
    view.tubeWorkpiecePage = Math.max(
      1,
      Number(target?.dataset?.tubePage ?? 1),
    );
    ops.renderProject(context, view);
    return true;
  }
  if (action === "tube-bottom-tab") {
    const tab = String(target?.dataset?.tubeBottomTab ?? "nesting");
    view.tubeBottomTab = tab === "stock" ? "stock" : "nesting";
    ops.renderProject(context, view);
    return true;
  }
  if (action === "tube-select-workpiece") {
    const workpieceEntityId = String(target?.dataset?.camWorkpieceId ?? "");
    view.tubeSelectedWorkpieceIds = workpieceEntityId ? [workpieceEntityId] : [];
    clearRecognitionViewState(view);
    void ops.invokeSDOMethod(
      context,
      view,
      "Workpiece.SetActive",
      { workpieceEntityId },
    );
    return true;
  }
  if (action === "tube-select-csg") {
    view.selectedCADIntentNodeId = String(target?.dataset?.camNodeId ?? "");
    ops.renderProject(context, view);
    void loadCADIntentPreview(context, view, view.selectedCADIntentNodeId, ops);
    return true;
  }
  if (action === "tube-add-cad-tool") {
    const kind = String(target?.dataset?.tubeToolKind ?? "");
    void addCADIntentTool(context, view, kind, ops);
    return true;
  }
  if (action === "tube-recognize-intent") {
    void recognizeCADIntent(context, view, ops);
    return true;
  }
  if (action === "tube-save-parameters") {
    void saveCADIntentParameters(context, view, target, ops);
    return true;
  }
  if (action === "tube-select-candidate") {
    void selectCADIntentCandidate(context, view, target, ops);
    return true;
  }
  if (action === "import-model") {
    clearRecognitionViewState(view);
  }
  return false;
}

function getVisibleWorkpieceIds(context) {
  return [...(context.mount?.querySelectorAll?.(".tube-workpiece-card:not([hidden])") ?? [])]
    .map((element) => String(element.dataset.camWorkpieceId ?? ""))
    .filter(Boolean);
}

async function chooseWorkpieceImportFile(context, view, ops) {
  const bridge = context.appProxy?.bridge
    ?? context.productProxy?.bridge
    ?? context.sceneProxy?.bridge
    ?? null;
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力。";
    ops.renderProject(context, view);
    return;
  }
  try {
    const sourcePath = await bridge.openFileDialog({
      title: "选择要添加的管材工件",
      filters: [
        { name: "CAD Model", extensions: ["step", "stp", "igs", "iges"] },
      ],
    });
    if (!sourcePath) {
      return;
    }
    ops.appendProjectLog(context, "info", `选择工件模型：${sourcePath}`);
    const imported = await ops.invokeSDOMethodPayload(
      context,
      view,
      "WorkpieceModel.Import",
      { sourcePath },
      { timeoutMs: 120000, expectScene: false },
    );
    if (!imported.ok) {
      return;
    }
    view.workpieceImportDialogOpen = true;
    view.workpieceImportDraft = {
      ...imported.payload,
      quantity: Math.max(1, Number(imported.payload?.quantity ?? 1)),
    };
    view.error = "";
  } catch (error) {
    view.error = error?.message ?? String(error);
  }
  ops.renderProject(context, view);
}

async function confirmWorkpieceImport(context, view, ops) {
  const draft = view.workpieceImportDraft;
  if (!draft) {
    return;
  }
  const quantityInput = context.mount?.querySelector?.("[data-tube-import-quantity]");
  const quantity = Math.max(1, Math.floor(Number(quantityInput?.value ?? draft.quantity ?? 1)));
  if (!Number.isFinite(quantity)) {
    view.error = "工件数量必须是有效的正整数。";
    ops.renderProject(context, view);
    return;
  }
  const ok = await ops.invokeSDOMethod(
    context,
    view,
    "Workpiece.Instantiate",
    { ...draft, quantity },
    { timeoutMs: 60000 },
  );
  if (!ok) {
    return;
  }
  view.workpieceImportDialogOpen = false;
  view.workpieceImportDraft = null;
  view.tubeWorkspaceMode = "main";
  view.tubeWorkpiecePage = Math.max(
    1,
    Math.ceil(Number(view.scene?.workpieces?.length ?? 1) / 8),
  );
  clearRecognitionViewState(view);
  view.notice = "工件已添加到当前项目。";
  ops.renderProject(context, view);
  await ops.fitViewAfterRenderPublish(context, view);
}

async function deleteWorkpiece(context, view, target, ops) {
  const workpieceEntityId = String(
    target?.dataset?.camWorkpieceId ?? view.scene?.model?.entityId ?? "",
  );
  if (!workpieceEntityId) {
    return;
  }
  clearRecognitionViewState(view);
  const ok = await ops.invokeSDOMethod(
    context,
    view,
    "Workpiece.Delete",
    { workpieceEntityId },
  );
  if (ok) {
    view.tubeWorkspaceMode = "main";
    view.notice = "工件已从当前项目中删除。";
    ops.renderProject(context, view);
    await ops.fitViewAfterRenderPublish(context, view);
  }
}

async function openPartEditor(context, view, ops) {
  if (!view.scene?.model?.isLoaded) {
    return;
  }
  if (view.tubeEditorSceneProxy) {
    view.tubeWorkspaceMode = "editor";
    ops.renderProject(context, view);
    return;
  }
  const mainScene = view.scene;
  const workpieceEntityId = String(mainScene?.model?.entityId ?? "");
  view.pending = true;
  view.error = "";
  view.tubeEditorLogs = [{
    level: "info",
    message: "正在创建独立零件编辑 Scene……",
    time: new Date().toLocaleTimeString("zh-CN", { hour12: false }),
  }];
  ops.renderProject(context, view);
  try {
    const response = await context.sceneProxy.invoke(
      "CADIntent.OpenEditorScene",
      { workpieceEntityId },
      { timeoutMs: 120000 },
    );
    const editorSceneState = response?.editorScene;
    if (!editorSceneState?.sceneId) {
      throw new Error("后端没有返回零件编辑 Scene。");
    }
    const editorProxy = await context.projectProxy?.adoptScene?.(editorSceneState);
    if (!editorProxy) {
      throw new Error("无法连接零件编辑 Scene 的独立通道。");
    }
    // OpenEditorScene 返回的是新建 Scene 的启动快照；再从它自己的通道读取一次
    // 正式状态，确保 PDO/View/资源描述都由该 Scene 自己提供。
    await editorProxy.getState({ timeoutMs: 30000 });
    const editorScene = await editorProxy.invoke("Workpiece.List", {}, { timeoutMs: 30000 });
    view.tubeMainScene = mainScene;
    view.tubeMainSceneId = String(context.sceneProxy?.sceneId ?? "");
    view.tubeEditorSceneProxy = editorProxy;
    view.tubeEditorSceneId = String(editorSceneState.sceneId);
    view.tubeEditorSceneName = String(editorSceneState.sceneName || `${response?.workpieceName || "零件"}编辑视图`);
    view.tubeSourceWorkpieceEntityId = String(response?.sourceWorkpieceEntityId ?? workpieceEntityId);
    view.scene = editorScene ?? {};
    view.tubeWorkspaceMode = "editor";
    const tube = view.scene?.tubeGeometry ?? {};
    view.cadIntentRecognized = Boolean(tube.available && tube.resourceId);
    view.recognizedCADIntentResourceId = String(tube.resourceId ?? "");
    view.selectedCADIntentNodeId = chooseInitialNode(tube);
    view.tubeEditorLogs.push({
      level: "ok",
      message: `独立编辑 Scene 已就绪：${view.tubeEditorSceneName}`,
      time: new Date().toLocaleTimeString("zh-CN", { hour12: false }),
    });
  } catch (error) {
    view.error = error?.message ?? String(error);
    view.tubeWorkspaceMode = "main";
    view.scene = mainScene;
  } finally {
    view.pending = false;
    ops.renderProject(context, view);
  }
  if (view.tubeWorkspaceMode === "editor") {
    await loadCADIntentPreview(context, view, view.selectedCADIntentNodeId, ops);
    await ops.fitViewAfterRenderPublish(context, view);
  }
}

export async function handleTubeRibbonCommand(context, view, commandId, ops) {
  if (commandId === "tube.editor.finish") {
    await closePartEditor(context, view, ops);
    return true;
  }
  if (commandId !== "intent.recognize-cad") {
    return false;
  }
  if (view.tubeWorkspaceMode === "editor" && view.tubeEditorSceneProxy) {
    await recognizeCADIntent(context, view, ops);
  } else {
    await openPartEditor(context, view, ops);
  }
  return true;
}

async function closePartEditor(context, view, ops) {
  const editorSceneId = String(view.tubeEditorSceneId ?? "");
  const editorProxy = view.tubeEditorSceneProxy;
  const mainScene = view.tubeMainScene ?? view.scene;
  view.tubeWorkspaceMode = "main";
  view.scene = mainScene;
  view.cadIntentPreviewRequestId = Number(view.cadIntentPreviewRequestId ?? 0) + 1;
  view.cadIntentPreviewNodeId = "";
  view.viewport?.clearGhostMesh?.();
  ops.renderProject(context, view);
  if (editorSceneId) {
    try {
      await context.sceneProxy.invoke(
        "CADIntent.CloseEditorScene",
        { sceneId: editorSceneId },
        { timeoutMs: 30000 },
      );
    } catch (error) {
      view.error = error?.message ?? String(error);
    }
  }
  if (!context.projectProxy?.releaseScene?.(editorSceneId)) {
    editorProxy?.dispose?.();
  }
  view.tubeEditorSceneProxy = null;
  view.tubeEditorSceneId = "";
  view.tubeEditorSceneName = "";
  view.tubeMainScene = null;
  view.tubeMainSceneId = "";
  view.tubeEditorLogs = [];
  await ops.refreshSceneState(context, view);
}

async function recognizeCADIntent(context, view, ops) {
  const result = await ops.invokeSDOMethodPayload(
    context,
    view,
    "CADIntent.Recognize",
    {},
    { timeoutMs: 120000, expectScene: false },
  );
  if (!result.ok) {
    return;
  }
  const tube = view.scene?.tubeGeometry ?? {};
  view.tubeWorkspaceMode = "editor";
  view.cadIntentRecognized = true;
  view.recognizedCADIntentResourceId = String(tube.resourceId ?? "");
  view.selectedCADIntentNodeId = chooseInitialNode(tube);
  view.notice = "CAD 意图识别完成，可在历史树中选择特征并修改参数。";
  ops.renderProject(context, view);
  await loadCADIntentPreview(
    context,
    view,
    view.selectedCADIntentNodeId,
    ops,
  );
}

async function saveCADIntentParameters(context, view, target, ops) {
  const nodeId = String(target?.dataset?.camNodeId ?? "");
  const form = target?.closest?.("[data-tube-parameter-form]")
    ?? context.mount?.querySelector?.(`[data-tube-parameter-form][data-tube-node-id="${cssEscape(nodeId)}"]`);
  if (!nodeId || !form) {
    view.error = "没有找到要编辑的制造特征。";
    ops.renderProject(context, view);
    return;
  }

  const parameters = {};
  for (const input of form.querySelectorAll("[data-tube-parameter]:not([disabled])")) {
    const name = String(input.dataset.tubeParameter ?? "");
    const value = Number(input.value);
    const original = Number(input.dataset.tubeOriginal);
    if (!name || !Number.isFinite(value)) {
      view.error = "参数必须是有效数字。";
      ops.renderProject(context, view);
      return;
    }
    if (!Number.isFinite(original) || Math.abs(value - original) > 1e-9) {
      parameters[name] = value;
    }
  }
  if (!Object.keys(parameters).length) {
    view.notice = "参数没有变化。";
    ops.renderProject(context, view);
    return;
  }

  cancelCADIntentParameterLivePreview(view);

  const ok = await ops.invokeSDOMethod(
    context,
    view,
    "CADIntent.SetParameters",
    { nodeId, parameters },
    { timeoutMs: 60000 },
  );
  if (!ok) {
    return;
  }
  view.selectedCADIntentNodeId = nodeId;
  view.recognizedCADIntentResourceId = String(view.scene?.tubeGeometry?.resourceId ?? "");
  view.notice = "参数已应用，零件结构与构造体已重新生成。";
  ops.renderProject(context, view);
  await loadCADIntentPreview(context, view, nodeId, ops);
}

async function selectCADIntentCandidate(context, view, target, ops) {
  const nodeId = String(target?.dataset?.camNodeId ?? "");
  const candidateId = String(target?.dataset?.camCandidateId ?? "");
  if (!nodeId || !candidateId) {
    return;
  }
  const ok = await ops.invokeSDOMethod(
    context,
    view,
    "CADIntent.SelectInterpretation",
    { nodeId, candidateId },
    { timeoutMs: 60000 },
  );
  if (!ok) {
    return;
  }
  view.selectedCADIntentNodeId = nodeId;
  view.notice = "已采用人工选择的几何解释。";
  ops.renderProject(context, view);
  await loadCADIntentPreview(context, view, nodeId, ops);
}

async function addCADIntentTool(context, view, kind, ops) {
  if (!kind || view.tubeWorkspaceMode !== "editor") return;
  const result = await ops.invokeSDOMethodPayload(
    context,
    view,
    "CADIntent.AddTool",
    { kind },
    { timeoutMs: 60000, expectScene: false },
  );
  if (!result.ok) return;
  const nodeId = String(result.payload?.addedNodeId ?? "");
  if (nodeId) {
    view.selectedCADIntentNodeId = nodeId;
  }
  view.recognizedCADIntentResourceId = String(
    view.scene?.tubeGeometry?.resourceId ?? "",
  );
  view.notice = `${cadToolKindLabel(kind)}已加入当前编辑 Scene。`;
  view.tubeEditorLogs ??= [];
  view.tubeEditorLogs.push({
    level: "ok",
    message: `${cadToolKindLabel(kind)}已创建为参数化 CAD 刀具${nodeId ? `：${nodeId}` : ""}`,
    time: new Date().toLocaleTimeString("zh-CN", { hour12: false }),
  });
  ops.renderProject(context, view);
}

async function loadCADIntentPreview(context, view, nodeId, ops) {
  cancelCADIntentParameterLivePreview(view);
  const requestId = Number(view.cadIntentPreviewRequestId ?? 0) + 1;
  view.cadIntentPreviewRequestId = requestId;
  view.cadIntentPreviewNodeId = "";
  view.viewport?.clearGhostMesh?.();
  view.cadIntentLivePreviewNodeId = "";
  view.cadIntentLivePreviewResourceUrl = "";
  view.cadIntentLivePreviewResourceVersion = 0;
  if (!nodeId) {
    return;
  }
  const node = (view.scene?.tubeGeometry?.solidNodes ?? [])
    .concat(view.scene?.tubeGeometry?.sectionPrimitives ?? [])
    .find((candidate) => String(candidate?.id ?? "") === nodeId);
  const resourceUrl = String(node?.metadata?.previewResourceUrl ?? "");
  const resourceVersion = Number(node?.metadata?.previewResourceVersion ?? 0);
  if (!resourceUrl) {
    return;
  }
  try {
    const preview = await loadPreviewMeshResource(
      (view.tubeWorkspaceMode === "editor"
        ? view.tubeEditorSceneProxy
        : context.sceneProxy)?.resources,
      { url: resourceUrl, version: resourceVersion },
    );
    if (requestId !== view.cadIntentPreviewRequestId
        || nodeId !== view.selectedCADIntentNodeId) {
      return;
    }
    view.viewport?.setGhostMesh?.({
      ...preview,
      nodeId,
      color: "#e9a12f",
      opacity: 0.34,
    });
    view.cadIntentPreviewNodeId = nodeId;
    view.error = "";
  } catch (error) {
    if (requestId !== view.cadIntentPreviewRequestId) {
      return;
    }
    view.viewport?.clearGhostMesh?.();
    view.error = error instanceof Error
      ? error.message
      : "历史特征预览资源读取失败。";
  }
  ops.renderProject(context, view);
}

function queueCADIntentParameterLivePreview(context, view, form) {
  const nodeId = String(form?.dataset?.tubeNodeId ?? "");
  if (!nodeId || nodeId !== String(view.selectedCADIntentNodeId ?? "")) return;
  if (view.cadIntentLivePreviewTimer) {
    clearTimeout(view.cadIntentLivePreviewTimer);
  }
  const requestId = Number(view.cadIntentPreviewRequestId ?? 0) + 1;
  view.cadIntentPreviewRequestId = requestId;
  setLivePreviewFormState(form, "loading", "参数已改变，正在重建刀具预览……");
  view.cadIntentLivePreviewTimer = setTimeout(() => {
    view.cadIntentLivePreviewTimer = null;
    void previewCADIntentParameters(context, view, form, nodeId, requestId);
  }, LIVE_PREVIEW_DEBOUNCE_MS);
}

async function previewCADIntentParameters(context, view, form, nodeId, requestId) {
  const parameters = {};
  for (const input of form.querySelectorAll("[data-tube-parameter]:not([disabled])")) {
    const name = String(input.dataset.tubeParameter ?? "");
    const text = String(input.value ?? "").trim();
    const value = Number(text);
    if (!name || !text || !Number.isFinite(value)) {
      setLivePreviewFormState(form, "invalid", "请输入有效参数；零件尚未改变。 ");
      return;
    }
    parameters[name] = value;
  }

  try {
    const proxy = view.tubeEditorSceneProxy;
    const response = await proxy.invoke(
      "CADIntent.PreviewParameters",
      { nodeId, parameters },
      { timeoutMs: 60000 },
    );
    if (requestId !== view.cadIntentPreviewRequestId
        || nodeId !== String(view.selectedCADIntentNodeId ?? "")) {
      return;
    }
    const resourceUrl = String(response?.previewResourceUrl ?? "");
    const resourceVersion = Number(response?.previewResourceVersion ?? 0);
    if (!resourceUrl) throw new Error("后端没有返回刀具预览资源。");
    const preview = await loadPreviewMeshResource(
      proxy.resources,
      { url: resourceUrl, version: resourceVersion },
    );
    if (requestId !== view.cadIntentPreviewRequestId
        || nodeId !== String(view.selectedCADIntentNodeId ?? "")) {
      return;
    }
    view.viewport?.setGhostMesh?.({
      ...preview,
      nodeId,
      color: "#e9a12f",
      opacity: 0.34,
    });
    view.cadIntentPreviewNodeId = nodeId;
    view.cadIntentLivePreviewNodeId = nodeId;
    view.cadIntentLivePreviewResourceUrl = resourceUrl;
    view.cadIntentLivePreviewResourceVersion = resourceVersion;
    setLivePreviewFormState(
      form,
      "live",
      "刀具正在实时变化；点击“应用到零件”后才参与布尔运算。",
    );
  } catch (error) {
    if (requestId !== view.cadIntentPreviewRequestId) return;
    view.cadIntentLivePreviewNodeId = "";
    setLivePreviewFormState(
      form,
      "invalid",
      `刀具预览失败：${error?.message ?? String(error)}；零件尚未改变。`,
    );
  }
}

function setLivePreviewFormState(form, state, message) {
  const previewState = form.closest(".cam-panel")?.querySelector(".tube-preview-state");
  if (previewState) {
    previewState.classList.remove("loading", "live", "invalid");
    previewState.classList.add("active", state);
    const label = previewState.querySelector("span");
    if (label) label.textContent = message;
  }
  const applyButton = form.querySelector("[data-cam-action='tube-save-parameters']");
  applyButton?.classList.toggle("dirty", state !== "invalid");
}

function cancelCADIntentParameterLivePreview(view) {
  if (view.cadIntentLivePreviewTimer) {
    clearTimeout(view.cadIntentLivePreviewTimer);
    view.cadIntentLivePreviewTimer = null;
  }
  view.cadIntentPreviewRequestId = Number(view.cadIntentPreviewRequestId ?? 0) + 1;
}

function chooseInitialNode(tube) {
  const nodes = Array.isArray(tube.solidNodes) ? tube.solidNodes : [];
  const sectionPrimitives = Array.isArray(tube.sectionPrimitives) ? tube.sectionPrimitives : [];
  const hasEditableParameters = (node) => Array.isArray(node.parameters)
    && node.parameters.some((parameter) => parameter?.editable !== false);
  const editablePreviewRemoval = nodes.find((node) =>
    node.materialRole && node.materialRole !== "None"
      && node.previewAvailable
      && hasEditableParameters(node));
  return sectionPrimitives.find((primitive) =>
      primitive.type === "SectionOuter" && primitive.previewAvailable)?.id
    ?? sectionPrimitives.find((primitive) => primitive.previewAvailable)?.id
    ?? sectionPrimitives[0]?.id
    ?? editablePreviewRemoval?.id
    ?? nodes.find((node) => node.previewAvailable && hasEditableParameters(node))?.id
    ?? nodes.find((node) => node.materialRole && node.materialRole !== "None" && node.previewAvailable)?.id
    ?? nodes.find(hasEditableParameters)?.id
    ?? String(tube.rootNodeId ?? nodes[0]?.id ?? "");
}

function clearRecognitionViewState(view) {
  view.cadIntentPreviewRequestId = Number(view.cadIntentPreviewRequestId ?? 0) + 1;
  view.cadIntentPreviewNodeId = "";
  view.viewport?.clearGhostMesh?.();
  view.cadIntentRecognized = false;
  view.recognizedCADIntentResourceId = "";
  view.selectedCADIntentNodeId = "";
}

function cssEscape(value) {
  return globalThis.CSS?.escape ? globalThis.CSS.escape(value) : value.replace(/["\\]/g, "\\$&");
}

function cadToolKindLabel(kind) {
  return ({
    "circular-penetration": "圆形贯切",
    "rectangular-penetration": "矩形贯切",
    "tapered-penetration": "锥形贯切",
    "half-space": "平面截断",
    "wrapped-normal": "UV 法向截断",
    bevel: "坡口",
  })[kind] ?? "参数化刀具";
}
