import { getDefaultParameters } from "./designerViews.mjs";

export async function handleDesignerAreaAction(context, view, action, _target, ops) {
  if (action === "tube-designer-generate") {
    return { handled: true, result: await generateProduct(context, view, ops) };
  }
  if (action === "tube-designer-export-all") {
    return { handled: true, result: await exportAll(context, view, ops) };
  }
  return false;
}

export async function handleDesignerRibbonCommand(context, view, commandId, ops) {
  if (commandId === "designer.generate") {
    await generateProduct(context, view, ops);
    return true;
  }
  if (commandId === "designer.reset") {
    view.tubeDesignerDraft = getDefaultParameters();
    ops.renderProject(context, view);
    return true;
  }
  if (commandId === "designer.export-step") {
    await exportAll(context, view, ops);
    return true;
  }
  return false;
}

export async function refreshDesignerState(context, view, ops = null) {
  const sceneProxy = context.sceneProxy;
  if (!sceneProxy) return false;
  try {
    const response = await sceneProxy.invoke("TubeDesigner.List", {}, { timeoutMs: 30000 });
    view.scene ??= {};
    view.scene.tubeDesigner = response?.tubeDesigner ?? {};
    view.tubeDesignerDraft = {
      ...getDefaultParameters(),
      ...(response?.tubeDesigner?.product?.parameters ?? {}),
    };
    ops?.renderProject?.(context, view);
    return true;
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops?.renderProject?.(context, view);
    throw error;
  }
}

async function generateProduct(context, view, ops) {
  const payload = collectParameters(context.mount, view);
  view.tubeDesignerDraft = payload;
  const previousViewRevision = ops.getActiveAreaViewRevision?.(view) ?? "0";
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(context, "TubeDesigner.Generate", payload);
    const designer = response?.tubeDesigner;
    const generationRunId = String(designer?.generationRun?.entityId ?? "").trim();
    const memberIds = (designer?.members ?? [])
      .map((member) => String(member?.entityId ?? "").trim())
      .filter(Boolean);
    if (!generationRunId || memberIds.length === 0) {
      throw new Error("TubeDesigner.Generate did not return a committed generation identity");
    }
    view.scene ??= {};
    view.scene.tubeDesigner = designer;

    const viewContent = await ops.refreshActiveAreaView(context, view, {
      correlationId: generationRunId,
      expectedEntityIds: memberIds,
      afterRevision: previousViewRevision,
    });
    const viewportReceipt = viewContent?.viewportReceipt;
    if (!viewportReceipt?.applied || viewportReceipt.revision !== viewContent.revision) {
      throw new Error(
        `Generation ${generationRunId} did not reach the viewport at View revision ${viewContent?.revision ?? "0"}`,
      );
    }
    const renderedEntityIds = new Set(viewportReceipt.entityIds ?? []);
    const missingRenderedEntityIds = memberIds.filter((entityId) =>
      !renderedEntityIds.has(entityId));
    if (missingRenderedEntityIds.length > 0) {
      throw new Error(
        `Generation ${generationRunId} reached View revision ${viewContent.revision}, `
        + `but ${missingRenderedEntityIds.length} target geometries were not renderable `
        + `(rows=${viewportReceipt.rowCount ?? 0}, geometryRefs=${viewportReceipt.geometryReferenceCount ?? 0})`,
      );
    }
    const fitReceipt = view.viewport?.fitViewForRevision?.(viewContent.revision);
    if (!fitReceipt?.fitted || fitReceipt.revision !== viewContent.revision) {
      throw new Error(
        `Generation ${generationRunId} was rendered but not fitted at the same View revision`,
      );
    }

    view.tubeDesignerLastOperation = {
      kind: "generate",
      generationRunId,
      viewRevision: viewContent.revision,
      memberIds,
      renderSequence: viewportReceipt.renderSequence,
      fitRenderSequence: fitReceipt.renderSequence,
    };
    view.tubeDesignerOwnMutation = true;
    try {
      await context.actions?.refreshActiveSceneState?.();
    } finally {
      view.tubeDesignerOwnMutation = false;
    }
    view.notice = "产品与制造拆单已生成。";
    return view.tubeDesignerLastOperation;
  });
}

async function exportAll(context, view, ops) {
  const input = context.mount?.querySelector?.("[data-tube-designer-export-directory]");
  const targetDirectory = String(input?.value ?? view.tubeDesignerExportDirectory ?? "").trim();
  if (!targetDirectory) {
    view.error = "请先填写 STEP 输出目录。";
    ops.renderProject(context, view);
    return;
  }
  view.tubeDesignerExportDirectory = targetDirectory;
  const generationRunId = String(
    view.scene?.tubeDesigner?.generationRun?.entityId ?? "",
  ).trim();
  if (!generationRunId) {
    view.error = "当前没有已提交的生成批次可供导出。";
    ops.renderProject(context, view);
    return null;
  }
  return runDesignerOperation(context, view, ops, async () => {
    const response = await invokeDesignerRequest(
      context,
      "TubeDesigner.ExportAll",
      { targetDirectory, generationRunId },
    );
    view.tubeDesignerLastOperation = {
      kind: "export",
      generationRunId,
      exportedCount: Number(response?.exportedCount ?? 0),
      exportedFiles: response?.exportedFiles ?? [],
    };
    view.notice = "制造零件 STEP 已全部导出。";
    return view.tubeDesignerLastOperation;
  });
}

async function runDesignerOperation(context, view, ops, work) {
  if (!context.sceneProxy) return null;
  view.pending = true;
  view.error = "";
  view.notice = "";
  ops.renderProject(context, view);
  try {
    return await work();
  } catch (error) {
    view.error = error?.message ?? String(error);
    throw error;
  } finally {
    view.pending = false;
    ops.renderProject(context, view);
  }
}

async function invokeDesignerRequest(context, method, payload) {
  if (!context.sceneProxy) {
    throw new Error(`${method} requires an active scene`);
  }
  return context.sceneProxy.invoke(method, payload, { timeoutMs: 120000 });
}

function collectParameters(mount, view) {
  const result = { ...getDefaultParameters(), ...(view.tubeDesignerDraft ?? {}) };
  for (const input of mount?.querySelectorAll?.("[data-tube-designer-parameter]") ?? []) {
    const name = input.dataset.tubeDesignerParameter;
    if (!name) continue;
    result[name] = input.type === "number" ? Number(input.value) : input.value;
  }
  return result;
}
