import { restoreSavedNestingTask } from "./nestingWorkflow.mjs";
import { chooseNestingPartFiles } from "./nestingPartFilePicker.mjs";

function sourceFileName(path) {
  return String(path ?? "").split(/[\\/]/).at(-1) || String(path ?? "");
}

function defaultPartName(fileName) {
  const stem = String(fileName ?? "").replace(/\.[^.]+$/, "").trim();
  // The native name limit is 160 UTF-8 bytes; keep whole Unicode code points.
  const encoder = new TextEncoder();
  let name = "";
  let bytes = 0;
  for (const character of stem) {
    bytes += encoder.encode(character).length;
    if (bytes > 160) break;
    name += character;
  }
  return name.trim() || "导入零件";
}

export function buildNestingPartImportMetadata(sourcePath) {
  return { sourcePath, name: defaultPartName(sourceFileName(sourcePath)), material: "", quantity: 1 };
}

async function chooseAndImportNestingPart(context, view, ops) {
  if (view.pending || view.tubeDesignerNestingImportChoosing) return null;
  const sceneProxy = context.sceneProxy;
  view.tubeDesignerNestingImportChoosing = true;
  try {
    const selected = await chooseNestingPartFiles(context, view);
    if (!selected.length || view.pending || context.sceneProxy !== sceneProxy) return null;
    if (selected.some(path => !/\.(?:step|stp|iges|igs)$/i.test(path))) {
      throw new Error("请选择 STEP、STP、IGES 或 IGS 文件。");
    }
    return await importNestingParts(context, view, ops, selected.map(buildNestingPartImportMetadata));
  } finally {
    view.tubeDesignerNestingImportChoosing = false;
  }
}

export async function importNestingParts(context, view, ops, files) {
  if (view.pending) return null;
  if (!files.length) return null;
  const sceneProxy = context.sceneProxy;
  if (typeof sceneProxy?.invoke !== "function") throw new Error("当前项目未连接，无法导入下料零件。");
  const current = () => context.sceneProxy === sceneProxy;
  const operationId = Number(view.tubeDesignerOperationSequence ?? 0) + 1;
  view.tubeDesignerOperationSequence = operationId;
  view.tubeDesignerOperation = {
    id: operationId,
    kind: "nesting-import",
    title: "正在识别并导入零件",
    phase: "recognizing",
    phaseLabel: "读取 CAD 实体",
    message: "正在识别主方向、长度和截面规格…",
  };
  view.pending = true;
  view.error = "";
  const imported = [], failures = [];
  try {
    for (const [index, metadata] of files.entries()) {
      if (!current()) return null;
      view.tubeDesignerOperation.message = `${index + 1} / ${files.length} · ${sourceFileName(metadata.sourcePath)}`;
      ops.renderProject(context, view);
      try {
        const response = await sceneProxy.invoke("TubeDesigner.ImportNestingPart", metadata, { timeoutMs: 180000 });
        if (!current()) return null;
        if (!response?.tubeDesigner || !response.partEntityId) throw new Error("导入零件未返回有效的下料记录。");
        imported.push(response);
        view.scene ??= {};
        view.scene.tubeDesigner = response.tubeDesigner;
      } catch (error) {
        if (!current()) return null;
        const message = `${sourceFileName(metadata.sourcePath)}：${error?.message ?? String(error)}`;
        failures.push(message);
        ops.showNotice?.(context, view, `导入失败：${message}`);
      }
    }
    if (!imported.length) throw new Error(failures.join("\n"));
    await restoreSavedNestingTask(view, context);
    if (!current()) return null;
    const response = imported.at(-1);
    const partId = String(response.partEntityId);
    view.tubeDesignerNestingSelectedPartIds = imported.map(part => String(part.partEntityId));
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerNestingSelectionKind = "part";
    view.tubeDesignerActiveNestingPlacementId = "";
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerPartViewportKey = "";
    view.tubeDesignerNestingStockDraft = null;
    view.tubeDesignerNestingSettingsSourceSignature = "";
    const profile = response.profile ?? {};
    const specification = [profile.displayName, profile.specification].filter(Boolean).join(" ") || "规格已识别";
    const source = response.sourceFileName ? `（${response.sourceFileName}）` : "";
    ops.showNotice?.(context, view, files.length === 1 ? `已导入${source}，识别规格：${specification}。请在“母材设置”确认库存后开始排样。`
      : `已导入 ${imported.length} 个零件${failures.length ? `，${failures.length} 个文件失败。${failures.join("；")}` : "。"}`);
    await context.actions?.refreshActiveSceneState?.();
    return { imported, failures };
  } catch (error) {
    if (current()) view.error = error?.message ?? String(error);
    throw error;
  } finally {
    if (view.tubeDesignerOperation?.id === operationId) {
      view.tubeDesignerOperation = null;
      view.pending = false;
      if (current()) ops.renderProject(context, view);
    }
  }
}

export async function handleNestingPartImportRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "nesting.import-part") return false;
  await chooseAndImportNestingPart(context, view, ops);
  return true;
}
