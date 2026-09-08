import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";
import { restoreSavedNestingTask } from "./nestingWorkflow.mjs";

const ACTION_PREFIX = "tube-designer-nesting-import-";
const CAD_EXTENSIONS = ["step", "stp", "iges", "igs"];

function sourceBridge(context) {
  return context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
}

function sourceFileName(path) {
  return String(path ?? "").split(/[\\/]/).at(-1) || String(path ?? "");
}

function sourceStem(name) {
  return String(name ?? "").replace(/\.[^.]+$/, "");
}

function nestingPartImportDraft(view) {
  return view.tubeDesignerNestingImportDraft ?? null;
}

export function renderNestingPartImportDialog(view) {
  const draft = nestingPartImportDraft(view);
  if (!draft) return "";
  const disabled = view.pending ? "disabled" : "";
  return `<div class="tube-designer-modal-backdrop" role="presentation">
    <section class="tube-designer-preset-dialog tube-nesting-part-import-dialog" role="dialog" aria-modal="true" aria-labelledby="nesting-part-import-title">
      <header class="tube-designer-dialog-header">
        <div><strong id="nesting-part-import-title">导入下料零件</strong><span>${escapeText(draft.sourceFileName)}</span></div>
        <button class="tube-designer-dialog-close" data-cam-action="${ACTION_PREFIX}cancel" aria-label="取消导入" ${disabled}>×</button>
      </header>
      <div class="tube-designer-preset-dialog-body" data-tube-designer-nesting-import-form>
        <p class="tube-nesting-part-import-note">导入后系统会从真实实体识别主方向、成品长度、截面形状与尺寸、可识别的壁厚，以及端部和孔槽特征。只有能确认是线性管材的实体才会进入排样清单。</p>
        <label class="tube-designer-field wide"><span>零件名称</span><input type="text" maxlength="160" value="${escapeAttr(draft.name)}" data-tube-designer-nesting-import-field="name" data-cam-change-action="${ACTION_PREFIX}draft" ${disabled} /></label>
        <label class="tube-designer-field wide"><span>材料（可选）</span><input type="text" maxlength="240" placeholder="例如：Q235B、不锈钢 304" value="${escapeAttr(draft.material)}" data-tube-designer-nesting-import-field="material" data-cam-change-action="${ACTION_PREFIX}draft" ${disabled} /></label>
        <label class="tube-designer-field"><span>数量</span><input type="number" min="1" max="1000000" step="1" value="${escapeAttr(draft.quantity)}" data-tube-designer-nesting-import-field="quantity" data-cam-change-action="${ACTION_PREFIX}draft" ${disabled} /></label>
        <div class="tube-nesting-part-import-file"><span>来源文件</span><strong>${escapeText(draft.sourceFileName)}</strong><small>支持 STEP / STP / IGES / IGS，单位按毫米读取。</small></div>
      </div>
      <footer class="tube-designer-preset-dialog-footer">
        <button class="tube-designer-secondary" data-cam-action="${ACTION_PREFIX}cancel" ${disabled}>取消</button>
        <button class="tube-designer-primary" data-cam-action="${ACTION_PREFIX}confirm" ${disabled}>${view.pending ? "识别并导入中…" : "识别并导入"}</button>
      </footer>
    </section>
  </div>`;
}

async function chooseNestingPart(context, view, ops) {
  if (view.pending) return null;
  const bridge = sourceBridge(context);
  if (typeof bridge?.openFileDialog !== "function") throw new Error("当前宿主没有提供文件选择能力。");
  const selected = String(await bridge.openFileDialog({
    title: "导入下料零件",
    filters: [{ name: "STEP / IGES 三维零件", extensions: CAD_EXTENSIONS }],
  }) ?? "").trim();
  if (!selected) return null;
  if (!/\.(?:step|stp|iges|igs)$/i.test(selected)) {
    throw new Error("请选择 STEP、STP、IGES 或 IGS 文件。");
  }
  const fileName = sourceFileName(selected);
  view.tubeDesignerNestingImportDraft = {
    sourcePath: selected,
    sourceFileName: fileName,
    name: sourceStem(fileName),
    material: "",
    quantity: "1",
  };
  ops.renderProject(context, view);
  return selected;
}

function readImportMetadata(view) {
  const draft = nestingPartImportDraft(view);
  if (!draft?.sourcePath) throw new Error("请先选择要导入的三维零件文件。");
  const name = String(draft.name ?? "").trim();
  if (!name) throw new Error("请填写零件名称。");
  if (name.length > 160) throw new Error("零件名称不能超过 160 个字符。");
  const material = String(draft.material ?? "").trim();
  if (material.length > 240) throw new Error("材料名称不能超过 240 个字符。");
  const quantity = Number(draft.quantity);
  if (!Number.isSafeInteger(quantity) || quantity < 1 || quantity > 1000000) {
    throw new Error("数量须为 1 至 1000000 之间的整数。");
  }
  return { sourcePath: draft.sourcePath, name, material, quantity };
}

async function importNestingPart(context, view, ops) {
  if (view.pending) return null;
  const metadata = readImportMetadata(view);
  if (typeof context.sceneProxy?.invoke !== "function") throw new Error("当前项目未连接，无法导入下料零件。");
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
  ops.renderProject(context, view);
  try {
    const response = await context.sceneProxy.invoke("TubeDesigner.ImportNestingPart", metadata, { timeoutMs: 180000 });
    if (!response?.tubeDesigner || !response.partEntityId) throw new Error("导入零件未返回有效的下料记录。");
    view.scene ??= {};
    view.scene.tubeDesigner = response.tubeDesigner;
    restoreSavedNestingTask(view, context);
    const partId = String(response.partEntityId);
    view.tubeDesignerNestingSelectedPartIds = [partId];
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerNestingSelectionKind = "part";
    view.tubeDesignerActiveNestingPlacementId = "";
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerPartViewportKey = "";
    view.tubeDesignerNestingImportDraft = null;
    view.tubeDesignerNestingStockDraft = null;
    view.tubeDesignerNestingSettingsSourceSignature = "";
    const profile = response.profile ?? {};
    const specification = [profile.displayName, profile.specification].filter(Boolean).join(" ") || "规格已识别";
    const source = response.sourceFileName ? `（${response.sourceFileName}）` : "";
    ops.showNotice?.(context, view, `已导入${source}，识别规格：${specification}。请在“母材设置”确认库存后开始排样。`);
    await context.actions?.refreshActiveSceneState?.();
    return response;
  } catch (error) {
    view.error = error?.message ?? String(error);
    throw error;
  } finally {
    if (view.tubeDesignerOperation?.id === operationId) view.tubeDesignerOperation = null;
    view.pending = false;
    ops.renderProject(context, view);
  }
}

export async function handleNestingPartImportAction(context, view, action, target, ops) {
  if (!["choose", "draft", "cancel", "confirm"].some((name) => action === ACTION_PREFIX + name)) {
    return { handled: false };
  }
  if (view.pending) return { handled: true };
  if (action === ACTION_PREFIX + "choose") {
    return { handled: true, result: await chooseNestingPart(context, view, ops) };
  }
  if (action === ACTION_PREFIX + "draft") {
    const field = target?.dataset?.tubeDesignerNestingImportField;
    if (view.tubeDesignerNestingImportDraft && ["name", "material", "quantity"].includes(field)) {
      view.tubeDesignerNestingImportDraft[field] = String(target?.value ?? "");
    }
    return { handled: true };
  }
  if (action === ACTION_PREFIX + "cancel") {
    view.tubeDesignerNestingImportDraft = null;
    ops.renderProject(context, view);
    return { handled: true };
  }
  return { handled: true, result: await importNestingPart(context, view, ops) };
}

export async function handleNestingPartImportRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "nesting.import-part") return false;
  await chooseNestingPart(context, view, ops);
  return true;
}
