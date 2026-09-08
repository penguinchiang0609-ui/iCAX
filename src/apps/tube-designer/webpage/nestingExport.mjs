import { buildNestingRequest, isNestingResultStale } from "./nestingWorkflow.mjs";
import { groupNestingPlans } from "./nestingGroups.mjs";

export function selectedNestingPlanIds(view, plans = view.tubeDesignerNestingResult?.plans ?? []) {
  const requested = Array.isArray(view.tubeDesignerSelectedNestingPlanIds)
    ? new Set(view.tubeDesignerSelectedNestingPlanIds.map(String)) : null;
  return new Set(plans.map((plan) => String(plan.id)).filter((id) => !requested || requested.has(id)));
}

export async function handleNestingExportAction(context, view, action, target, ops) {
  const actions = ["tube-designer-nesting-toggle-plan", "tube-designer-nesting-toggle-all-plans",
    "tube-designer-nesting-export-selected", "tube-designer-nesting-toggle-group", "tube-designer-nesting-export-group"];
  if (!actions.includes(action)) return { handled: false };
  if (view.pending || view.tubeDesignerExportOperation || view.tubeDesignerNestingSettingsSaving) return { handled: true };
  const result = view.tubeDesignerNestingResult;
  const plans = result?.plans ?? [];
  const selected = selectedNestingPlanIds(view, plans);
  const group = groupNestingPlans(plans).find((item) => item.key === String(target?.dataset?.tubeDesignerNestingGroupKey ?? ""));
  if (action === "tube-designer-nesting-toggle-group") {
    if (group) {
      for (const { plan } of group.entries) {
        if (target?.checked) selected.add(String(plan.id));
        else selected.delete(String(plan.id));
      }
      view.tubeDesignerSelectedNestingPlanIds = [...selected];
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-nesting-toggle-plan") {
    const id = String(target?.dataset?.tubeDesignerNestingPlanId ?? "");
    if (plans.some((plan) => String(plan.id) === id)) {
      if (target?.checked) selected.add(id);
      else selected.delete(id);
      view.tubeDesignerSelectedNestingPlanIds = [...selected];
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-nesting-toggle-all-plans") {
    view.tubeDesignerSelectedNestingPlanIds = target?.checked ? plans.map((plan) => String(plan.id)) : [];
    ops.renderProject(context, view);
    return { handled: true };
  }
  let operation = null;
  try {
    if (!plans.length) throw new Error("请先生成排样结果。");
    if (isNestingResultStale(view, context)) throw new Error("排样输入已变化，请重新排样后再导出。");
    if (action === "tube-designer-nesting-export-group" && !group) throw new Error("该规格组已不存在，请重新选择。");
    const exporting = action === "tube-designer-nesting-export-group"
      ? group.entries.map(({ plan }) => plan)
      : plans.filter((plan) => selected.has(String(plan.id)));
    if (!exporting.length) throw new Error("请勾选需要导出的母材。");
    if (!context.sceneProxy?.invoke) throw new Error("当前项目没有连接导出服务。");
    // Export must replay the exact request that produced this result. Building a
    // fresh request here would accidentally include locks toggled after solving.
    const request = result.solveRequest ?? buildNestingRequest(view, context);
    operation = { kind: "nesting-export", title: "导出排样结果", stage: "选择导出目录" };
    view.tubeDesignerOperation = operation;
    view.pending = true;
    view.error = "";
    view.notice = "";
    ops.renderProject(context, view);
    let targetDirectory = String(target?.dataset?.tubeDesignerNestingExportDirectory ?? "").trim();
    if (!targetDirectory) {
      const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
      if (!bridge?.openDirectoryDialog) throw new Error("当前宿主没有提供目录选择能力。");
      targetDirectory = String(await bridge.openDirectoryDialog({
        title: `导出 ${exporting.length} 根母材的排样结果`,
        initialDirectory: view.tubeDesignerNestingExportDirectory ?? "",
      }) ?? "").trim();
    }
    if (!targetDirectory) return { handled: true, result: { cancelled: true } };
    if (result !== view.tubeDesignerNestingResult || isNestingResultStale(view, context)) {
      throw new Error("排样结果已变化，请重新选择后导出。");
    }
    operation.stage = "正在生成排样文件";
    ops.renderProject(context, view);
    const response = await context.sceneProxy.invoke("TubeDesigner.ExportNesting", {
      targetDirectory, plans: exporting, parameters: request.parameters, request,
    }, { timeoutMs: 300000 });
    if (Number(response?.exportedCount) !== exporting.length || !Array.isArray(response?.exportedFiles)
      || response.exportedFiles.length !== exporting.length || !String(response?.partListFile ?? "").trim()) {
      throw new Error("导出服务未返回完整的文件清单。");
    }
    view.tubeDesignerNestingExportDirectory = targetDirectory;
    view.tubeDesignerLastNestingExport = response;
    view.notice = `已导出 ${exporting.length} 根母材的排样结果至 ${response.outputDirectory || targetDirectory}`;
    ops.appendProjectLog?.(context, "info", view.notice);
    return { handled: true, result: response };
  } catch (error) {
    view.error = `导出失败：${error?.message ?? String(error)}`;
    ops.appendProjectLog?.(context, "error", view.error);
    return { handled: true, result: false };
  } finally {
    if (operation && view.tubeDesignerOperation === operation) {
      view.tubeDesignerOperation = null;
      view.pending = false;
    }
    ops.renderProject(context, view);
  }
}
