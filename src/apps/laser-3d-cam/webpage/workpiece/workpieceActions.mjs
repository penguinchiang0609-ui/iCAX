export async function handleWorkpieceRibbonCommand(context, view, commandId, ops) {
  if (commandId === "workpiece.import") {
    await chooseModelFile(context, view, true, ops);
    return true;
  }
  if (commandId === "workpiece.check") {
    await invokeCadMethod(context, view, "WorkpieceEdit.Inspect", ops);
    return true;
  }
  if (commandId === "workpiece.repair") {
    if (!view.scene?.model?.hasDraft) {
      await invokeCadMethod(context, view, "WorkpieceEdit.Begin", ops);
    }
    return true;
  }
  if (commandId === "intent.from-selection") {
    await invokeCadMethod(context, view, "IntentToolpath.CreateFromSelection", ops);
    return true;
  }
  return false;
}

export function handleWorkpieceAction(context, view, action, ops) {
  if (action === "choose-model") {
    chooseModelFile(context, view, false, ops);
    return true;
  }
  if (action === "import-model") {
    const input = context.mount?.querySelector?.("[data-cam-model-path]");
    const sourcePath = String(input?.value ?? view.sourcePath ?? "").trim();
    importModelPath(context, view, sourcePath, ops);
    return true;
  }
  const cadMethods = {
    "cad-begin": "WorkpieceEdit.Begin",
    "cad-inspect": "WorkpieceEdit.Inspect",
    "cad-commit": "WorkpieceEdit.Commit",
    "cad-discard": "WorkpieceEdit.Discard",
    "intent-from-selection": "IntentToolpath.CreateFromSelection",
  };
  if (cadMethods[action]) {
    invokeCadMethod(context, view, cadMethods[action], ops);
    return true;
  }
  return false;
}

async function invokeCadMethod(context, view, facadeMethod, ops) {
  const ok = await ops.invokeFacadeMethod(context, view, facadeMethod, {});
  if (ok && facadeMethod === "WorkpieceEdit.Inspect") {
    view.notice = "模型检查完成";
  }
}

export async function chooseModelFile(context, view, importAfterChoose, ops) {
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力";
    ops.renderProject(context, view);
    return;
  }

  try {
    const path = await bridge.openFileDialog({
      title: "选择工件",
      filters: [
        { name: "CAD Model", extensions: ["step", "stp", "igs", "iges"] },
      ],
    });
    if (!path) {
      return;
    }
    ops.appendProjectLog(context, "info", `选择工件模型：${path}`);
    view.sourcePath = path;
    view.error = "";
    if (importAfterChoose) {
      importModelPath(context, view, path, ops);
      return;
    }
  } catch (error) {
    view.error = error?.message ?? String(error);
  }
  ops.renderProject(context, view);
}

export async function importModelPath(context, view, sourcePath, ops) {
  const path = String(sourcePath ?? "").trim();
  view.sourcePath = path;
  if (!path) {
    view.error = "请输入 STEP/STP/IGS/IGES 文件路径";
    ops.renderProject(context, view);
    return;
  }
  const imported = await ops.invokeFacadeMethodPayload(
    context,
    view,
    "WorkpieceModel.Import",
    { sourcePath: path },
    { timeoutMs: 120000, expectScene: false },
  );
  if (!imported.ok) {
    return;
  }
  const ok = await ops.invokeFacadeMethod(context, view, "Workpiece.Instantiate", imported.payload, { timeoutMs: 60000 });
  if (ok) {
    await ops.fitViewAfterRenderPublish(context, view);
  }
}
