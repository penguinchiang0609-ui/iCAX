import { hasLicenseFeature } from "./licensing.mjs";

const PREFIX = "tube-designer-excel-automation-";
const POLL_MS = 2500;
const service = { target: null, timer: null, running: false, stopped: false, lastError: "", scanErrors: new Set() };

function escape(value) {
  return String(value ?? "").replace(/[&<>"']/g, character => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  })[character]);
}

function product(context, method, payload = {}) {
  if (!context.productProxy?.invoke) throw new Error("当前产品未连接。");
  return context.productProxy.invoke(`TubeDesigner.${method}`, payload, { timeoutMs: 120000 });
}

function scene(context, method, payload = {}) {
  if (!context.sceneProxy?.invoke) throw new Error("当前项目未连接。");
  return context.sceneProxy.invoke(`TubeDesigner.${method}`, payload, { timeoutMs: 180000 });
}

function current(context, view) {
  return (!service.target || (service.target.view === view
      && service.target.context.sceneProxy === context.sceneProxy
      && service.target.context.project?.projectId === context.project?.projectId))
    && context.mount?.isConnected !== false;
}

function render(context, view, ops) {
  if (current(context, view)) ops.renderProject?.(context, view);
}

function available(context, view) {
  const mount = context.mount;
  const focused = mount?.ownerDocument?.activeElement;
  return hasAutomaticExcelLicense(context, view)
    && context.productProxy?.invoke && context.sceneProxy?.invoke && mount?.isConnected !== false
    && view.tubeDesignerLoaded !== false && !view.pending && !view.tubeDesignerLoading
    && !view.tubeDesignerSynchronizationPromise && !view.tubeDesignerUserDataLoading
    && !view.tubeDesignerUserDataSynchronizationPromise && !view.tubeDesignerExportOperation
    && !view.tubeDesignerRightDraftDirty && !view.tubeDesignerExcelImport
    && !view.tubeDesignerExcelAutomationDialog
    && !mount?.querySelector?.('[aria-modal="true"]')
    && !(mount?.contains?.(focused) && focused?.matches?.('input,textarea,select,[contenteditable="true"]'));
}

function log(context, ops, level, message) {
  ops.appendProjectLog?.(context, level, message);
}

export async function runBatchExcelAutomationOnce(context, view, ops) {
  if (service.running || !available(context, view)) return false;
  service.running = true;
  const sceneProxy = context.sceneProxy;
  const projectId = context.project?.projectId;
  const originalScene = view.scene;
  const operationContext = { ...context, sceneProxy, productProxy: context.productProxy,
    project: context.project ? { ...context.project } : context.project };
  const isCurrent = () => current(context, view) && context.sceneProxy === sceneProxy
    && context.project?.projectId === projectId && view.scene === originalScene;
  const renderCurrent = () => { if (isCurrent()) render(context, view, ops); };
  let operation;
  let job;
  let previousProductId = "";
  let ownsPending = false;
  let sceneChanged = false;
  let outputs = [];
  try {
    const scan = await product(operationContext, "ScanBatchExcelAutomation");
    if (!isCurrent()) return false;
    service.lastError = "";
    view.tubeDesignerExcelAutomationSettings = scan.settings;
    const scanErrors = new Set();
    for (const error of scan.errors ?? []) {
      const message = `${error.sourcePath ? `${error.sourcePath}：` : ""}${error.error ?? error.message ?? error}`;
      scanErrors.add(message);
      if (!service.scanErrors.has(message)) log(operationContext, ops, "error", `Excel 自动处理：${message}`);
    }
    service.scanErrors = scanErrors;
    const candidate = scan.settings?.enabled && scan.files?.[0];
    // The user can start editing while directory inspection is in flight.
    if (!candidate || !available(context, view) || !isCurrent()) return false;
    previousProductId = String(view.scene?.tubeDesigner?.product?.entityId
      ?? view.scene?.tubeDesigner?.activeProductId ?? "");
    view.pending = true;
    ownsPending = true;
    operation = view.tubeDesignerOperation = { kind: "automatic-excel", title: "正在自动处理 Excel",
      phaseLabel: "读取产品数据", message: candidate.sourceName, completed: 0, total: 0 };
    renderCurrent();
    job = await product(operationContext, "ClaimBatchExcelAutomation", {
      sourcePath: candidate.sourcePath, fileToken: candidate.fileToken,
    });
    if (job.claimed === false) {
      if (job.error) log(operationContext, ops, "error", `Excel 自动处理失败（${candidate.sourceName}）：${job.error}`);
      job = null;
      return false;
    }
    if (!job?.sourcePath || !job.targetDirectory || !job.originalSourcePath || !job.fileToken)
      throw new Error("自动处理任务缺少源文件或输出目录。");
    const workbook = await product(operationContext, "ReadBatchExcelImport", { sourcePath: job.sourcePath });
    if (!workbook.templateId || !workbook.rows?.length) throw new Error("Excel 中没有可导入的产品。");
    const productIds = [];
    for (const [index, row] of workbook.rows.entries()) {
      operation.phaseLabel = `创建产品 ${index + 1} / ${workbook.rows.length}`;
      operation.completed = index;
      operation.total = workbook.rows.length;
      renderCurrent();
      try {
        sceneChanged = true;
        const created = await scene(operationContext, "GeneratePreview", {
          templateId: workbook.templateId, ...row.parameters,
          instanceName: row.instanceName, instanceQuantity: row.instanceQuantity,
        });
        const id = created.tubeDesigner?.product?.entityId ?? created.productEntityId;
        if (!id) throw new Error("创建结果没有返回产品实例。");
        productIds.push(String(id));
      } catch (error) {
        throw new Error(`Excel 第 ${row.sourceRow} 行：${error.message}`, { cause: error });
      }
    }
    operation.phaseLabel = "生成零件清单";
    renderCurrent();
    const disassembled = await scene(operationContext, "DisassembleSelected", { productEntityIds: productIds });
    const groups = disassembled.tubeDesigner?.manufacturingGroups ?? disassembled.manufacturingGroups ?? [];
    const expectedIds = new Set(productIds);
    const selectedGroups = groups.filter(group => expectedIds.has(String(group.productEntityId)));
    if (selectedGroups.length !== productIds.length || selectedGroups.some(group => !group.parts?.length))
      throw new Error("部分产品尚未生成有效零件清单。");
    const partEntityIds = selectedGroups.flatMap(group => group.parts.map(part => String(part.entityId)));
    operation.phaseLabel = "导出零件与 Excel 清单";
    renderCurrent();
    const exported = await scene(operationContext, "ExportSelected", { targetDirectory: job.targetDirectory, partEntityIds });
    outputs = [...new Set([...(exported.exportedFiles ?? []), exported.partListFile]
      .filter(path => typeof path === "string" && path))];
    if (!(exported.exportedCount > 0) || !outputs.length) throw new Error("没有返回已导出的零件文件。");
    await product(operationContext, "CompleteBatchExcelAutomation", {
      sourcePath: job.originalSourcePath, fileToken: job.fileToken, status: "succeeded", outputs, error: "",
    });
    log(operationContext, ops, "info", `Excel 自动处理完成：${job.sourceName}，${productIds.length} 个产品、${exported.exportedCount} 个零件；输出：${job.targetDirectory}`);
    service.lastError = "";
    return true;
  } catch (error) {
    if (job?.originalSourcePath && job?.fileToken) {
      try {
        await product(operationContext, "CompleteBatchExcelAutomation", {
          sourcePath: job.originalSourcePath, fileToken: job.fileToken, status: "failed", outputs,
          error: error.message ?? String(error),
        });
      } catch (completionError) {
        log(operationContext, ops, "error", `记录 Excel 自动处理结果失败：${completionError.message}`);
      }
    }
    const message = `Excel 自动处理失败${job?.sourceName ? `（${job.sourceName}）` : ""}：${error.message ?? error}`;
    if (message !== service.lastError) log(operationContext, ops, "error", message);
    service.lastError = message;
    return false;
  } finally {
    if (ownsPending) {
      try {
        if (sceneChanged) {
          if (previousProductId) await scene(operationContext, "ActivateProduct", { productEntityId: previousProductId });
          if (isCurrent()) await ops.refreshDesignerState?.(operationContext, view);
          // Refreshing the designer snapshot hides the old/new signature change
          // from mountProject. Publish the actual active product View explicitly.
          if (isCurrent() && view.activeAreaId === "view" && ops.refreshActiveAreaView) {
            const designer = view.scene?.tubeDesigner;
            const activeProduct = designer?.product;
            const generationRunId = String(designer?.generationRun?.entityId
              ?? activeProduct?.activeGenerationRunId ?? "");
            const memberIds = (designer?.members ?? []).map(member => String(member.entityId ?? "")).filter(Boolean);
            if (activeProduct?.entityId && generationRunId && memberIds.length) {
              const content = await ops.refreshActiveAreaView(operationContext, view, {
                correlationId: generationRunId, expectedEntityIds: memberIds,
              });
              if (isCurrent() && view.activeAreaId === "view" && !previousProductId) {
                ops.fitDesignerDefaultView?.(view, content.revision, activeProduct);
              }
            }
          }
          if (isCurrent()) {
            view.tubeDesignerOwnMutation = true;
            try {
              await context.actions?.refreshActiveSceneState?.({ sceneProxy, projectId });
            } finally { view.tubeDesignerOwnMutation = false; }
          }
        }
      } catch (error) { log(operationContext, ops, "error", `自动处理后刷新场景失败：${error.message}`); }
      if (view.tubeDesignerOperation === operation) {
        view.pending = false;
        view.tubeDesignerOperation = null;
      }
      renderCurrent();
    }
    service.running = false;
  }
}

function schedule(delay = POLL_MS) {
  if (service.stopped || service.timer !== null) return;
  service.timer = setTimeout(async () => {
    service.timer = null;
    const target = service.target;
    if (!target || target.context.mount?.isConnected === false) return;
    await runBatchExcelAutomationOnce(target.context, target.view, target.ops);
    schedule();
  }, delay);
}

export function connectBatchExcelAutomation(context, view, ops) {
  if (!hasAutomaticExcelLicense(context, view)) { stopBatchExcelAutomation(); return; }
  service.target = { context, view, ops };
  service.stopped = false;
  schedule(0);
}

export function hasAutomaticExcelLicense(context, view) {
  return ["product.design", "product.breakdown", "product.export"].every(id => hasLicenseFeature(context, view, id));
}

export function stopBatchExcelAutomation() {
  service.stopped = true;
  if (service.timer !== null) clearTimeout(service.timer);
  service.timer = null;
}

function collect(context, view, target) {
  const form = target?.closest?.("[data-batch-excel-automation-dialog]")
    ?? context.mount?.querySelector?.("[data-batch-excel-automation-dialog]");
  const draft = view.tubeDesignerExcelAutomationDialog?.draft;
  if (!draft) return;
  for (const input of form?.querySelectorAll?.("[data-excel-automation-field]") ?? []) {
    const field = input.dataset.excelAutomationField;
    draft[field] = field === "enabled" ? input.checked : String(input.value ?? "");
  }
}

export async function handleBatchExcelAutomationRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "designer.excel.automation-settings") return false;
  if (view.pending) return true;
  const dialog = { loading: true, saving: false, error: "", draft: {
    enabled: false, inputDirectory: "", tempDirectory: "", outputDirectory: "", ...view.tubeDesignerExcelAutomationSettings,
  } };
  view.tubeDesignerExcelAutomationDialog = dialog;
  render(context, view, ops);
  try {
    const response = await product(context, "GetBatchExcelAutomationSettings");
    if (view.tubeDesignerExcelAutomationDialog !== dialog) return true;
    view.tubeDesignerExcelAutomationSettings = response.settings;
    dialog.draft = { ...response.settings };
  } catch (error) { dialog.error = error.message; }
  finally {
    dialog.loading = false;
    if (view.tubeDesignerExcelAutomationDialog === dialog) render(context, view, ops);
  }
  return true;
}

export async function handleBatchExcelAutomationAction(context, view, action, target, ops) {
  if (!action.startsWith(PREFIX)) return { handled: false };
  const dialog = view.tubeDesignerExcelAutomationDialog;
  if (!dialog || dialog.saving) return { handled: true };
  if (action === PREFIX + "cancel") {
    view.tubeDesignerExcelAutomationDialog = null;
    render(context, view, ops);
    return { handled: true };
  }
  if (dialog.loading) return { handled: true };
  collect(context, view, target);
  if (action === PREFIX + "change") return { handled: true };
  if (action === PREFIX + "browse") {
    const field = target?.dataset?.directoryField;
    const titles = {
      inputDirectory: "选择 Excel 侦听目录",
      tempDirectory: "选择自动处理临时目录（temp）",
      outputDirectory: "选择自动处理输出目录",
    };
    if (!Object.hasOwn(titles, field)) return { handled: true };
    try {
      const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge;
      if (!bridge?.openDirectoryDialog) throw new Error("当前宿主没有提供目录选择能力。");
      const directory = await bridge.openDirectoryDialog({
        title: titles[field],
        initialDirectory: dialog.draft[field],
      });
      if (directory && view.tubeDesignerExcelAutomationDialog === dialog) {
        dialog.draft[field] = String(directory);
        const input = context.mount.querySelector(`[data-excel-automation-field="${field}"]`);
        if (input) input.value = dialog.draft[field];
      }
    } catch (error) { dialog.error = error.message; render(context, view, ops); }
    return { handled: true };
  }
  if (action !== PREFIX + "save") return { handled: true };
  dialog.saving = true;
  dialog.error = "";
  render(context, view, ops);
  try {
    const response = await product(context, "SaveBatchExcelAutomationSettings", { settings: {
      enabled: Boolean(dialog.draft.enabled), inputDirectory: String(dialog.draft.inputDirectory).trim(),
      tempDirectory: String(dialog.draft.tempDirectory).trim(),
      outputDirectory: String(dialog.draft.outputDirectory).trim(),
    } });
    view.tubeDesignerExcelAutomationSettings = response.settings;
    view.tubeDesignerExcelAutomationDialog = null;
    log(context, ops, "info", response.settings.enabled ? "Excel 自动处理已启用。" : "Excel 自动处理已关闭。");
  } catch (error) { dialog.error = error.message; }
  finally { dialog.saving = false; render(context, view, ops); }
  return { handled: true };
}

export function renderBatchExcelAutomationDialog(view) {
  const dialog = view.tubeDesignerExcelAutomationDialog;
  if (!dialog) return "";
  const disabled = dialog.loading || dialog.saving ? "disabled" : "";
  return `<div class="tube-nesting-settings-backdrop" role="presentation">
    <section class="tube-nesting-settings-dialog tube-excel-automation-dialog" role="dialog" aria-modal="true"
      aria-labelledby="tube-excel-automation-title" data-batch-excel-automation-dialog
      data-window-state-controls="input[data-excel-automation-field]">
      <header class="tube-nesting-settings-header"><div><strong id="tube-excel-automation-title">Excel 自动处理设置</strong>
        <span>启用后，启动时检查并持续侦听新文件，自动导入产品、生成零件清单并导出。</span></div>
        <button type="button" class="tube-nesting-settings-close" aria-label="关闭自动处理设置"
          data-cam-action="${PREFIX}cancel" ${dialog.saving ? "disabled" : ""}>×</button></header>
      <div class="tube-nesting-settings-body">
        <label class="tube-excel-automation-enabled"><input type="checkbox" data-excel-automation-field="enabled"
          data-cam-action="${PREFIX}change" ${dialog.draft.enabled ? "checked" : ""} ${disabled}>启用自动处理</label>
        ${[["inputDirectory", "侦听目录"], ["tempDirectory", "临时目录"], ["outputDirectory", "输出目录"]].map(([field, label]) =>
          `<label class="tube-excel-automation-directory"><span>${label}</span><div><input type="text"
            aria-label="${label}" data-excel-automation-field="${field}" data-cam-action="${PREFIX}change"
            value="${escape(dialog.draft[field])}" ${disabled}><button type="button" data-cam-action="${PREFIX}browse"
            data-directory-field="${field}" ${disabled}>选择目录</button></div></label>`).join("")}
        <p class="tube-excel-automation-hint">每个产品模板使用各自的导入工作簿。新文件先移入临时目录再处理，使侦听目录保持清洁。原始工作簿无论成功或失败均保留在临时目录；处理结果写入输出目录中的独立文件夹。失败详情可在日志中查看。</p>
        ${dialog.loading ? '<p role="status">正在读取设置…</p>' : ""}
        ${dialog.error ? `<p class="tube-excel-automation-error" role="alert">${escape(dialog.error)}</p>` : ""}
      </div><footer class="tube-nesting-settings-footer"><button type="button" data-cam-action="${PREFIX}cancel"
        ${dialog.saving ? "disabled" : ""}>取消</button><button type="button" class="is-primary"
        data-cam-action="${PREFIX}save" ${disabled}>${dialog.saving ? "正在保存…" : "保存"}</button></footer>
    </section></div>`;
}
