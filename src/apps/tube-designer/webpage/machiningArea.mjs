import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { listNestingParts } from "./partsArea.mjs";
import { scheduleNestingPlanHydration, cancelNestingPlanHydration } from "./nestingPreview.mjs";
import { confirmWithoutTitle } from "./confirmDialog.mjs";
import { attachMachiningSideEditor, handleMachiningPathAction, pathEditor, renderMachiningEditToolbar, renderMachiningPathList, renderMachiningPathProperties, renderMachiningSideEditor } from "./machiningEditor.mjs";
import { handleMachiningViewportPick, machiningPathOverlay } from "./machiningViewport.mjs";

const PREFIX = "tube-machining-";
const sections = {
  overview: ["加工准备", "选择加工清单中的一根排样母材或导入模型，检查三维结构与来源。"],
  lead: ["引线", "贴面引线沿工件表面布置；空间引线独立定义三维路径与进刀姿态。"],
  joint: ["微连", "在轮廓上保留未切断区间，后续支持指定位置、数量、间距及拐角避让。"],
  compensation: ["刀补", "保留设计轮廓，依据有效切割半径、切割方向和表面法向生成实际轨迹。"],
  order: ["加工排序", "母材顺序、零件顺序与轮廓顺序分别管理，并考虑切断、支撑与避碰约束。"],
  head: ["切割头模型", "后续接入切割头几何、刀尖坐标、安装方向与碰撞包络。模型尺寸不等于刀补半径。"],
};

function state(view) { return view.tubeDesignerMachining ??= { section: "overview", selectedId: "", sourceSelection: null }; }
function designer(view) { return view.scene?.tubeDesigner ?? {}; }

export function machiningSourcePlans(view) {
  const source = designer(view);
  const parts = new Map(listNestingParts(source).map(part => [String(part.entityId), part]));
  const ordinals = new Map();
  return (source.nestingTask?.result?.plans ?? []).map(plan => {
    const part = parts.get(String(plan.placements?.[0]?.partId));
    const profile = part?.profile ?? {};
    const label = [profile.displayName, profile.specification].filter(Boolean).join(" ") || plan.profileKey || "排样母材";
    const ordinal = (ordinals.get(label) ?? 0) + 1;
    ordinals.set(label, ordinal);
    return { ...plan, name: `${label} · ${ordinal}`, profile: label, profileData: profile };
  });
}

export function machiningJobs(view) {
  const source = designer(view);
  const plans = new Map(machiningSourcePlans(view).map(plan => [String(plan.id), plan]));
  return (source.machiningTask?.jobs ?? []).map(job => {
    const plan = job.sourceKind === "nesting" ? plans.get(String(job.planId)) : null;
    const stale = job.sourceKind === "nesting" && (!plan || job.sourceRevision !== source.nestingTask?.revision);
    return { ...job, plan: stale ? null : plan, stale,
      name: job.name || plan?.name || `排样 ${job.planId}`,
      status: job.analysis ? `${job.analysis.paths?.length ?? 0} 条独立刀路${stale ? " · CAD 已变化" : ""}` : stale ? "来源已变化" : "待分析刀路" };
  });
}

export function selectedMachiningJob(view) {
  const jobs = machiningJobs(view);
  const id = state(view).selectedId || designer(view).machiningTask?.activeJobId;
  return jobs.find(job => job.id === id) ?? jobs[0] ?? null;
}

export function renderTubeMachiningLeftPane(_context, view) {
  const jobs = machiningJobs(view);
  const active = selectedMachiningJob(view);
  return `<section class="tube-machining-pane"><header class="tube-machining-pane-title"><strong>加工清单</strong><span>${jobs.length} 项</span></header>
    <p class="tube-machining-note">从上方菜单接收下料排样结果，或导入已排样的三维模型。</p>
    <div class="tube-machining-job-list" aria-label="加工清单">${jobs.length ? jobs.map(job => `
      <button type="button" class="tube-machining-job ${job.id === active?.id ? "active" : ""}" data-cam-action="${PREFIX}select" data-job-id="${escapeAttr(job.id)}" aria-pressed="${job.id === active?.id}">
        <span class="tube-machining-source-tag">${job.sourceKind === "nesting" ? "下料关联" : "三维导入"}</span>
        <strong>${escapeText(job.name)}</strong><small>${job.plan ? `${job.plan.placements.length} 件 · ${formatNumber(job.plan.stockLength)} mm` : escapeText(job.sourceFileName || "原排样记录")}</small>
        <span class="tube-machining-job-status ${job.stale ? "stale" : ""}">${job.status}</span>
      </button>`).join("") : `<div class="tube-machining-empty-list">暂无加工数据<br><small>加工清单与下料零件清单分开管理</small></div>`}</div></section>`;
}

export function renderTubeMachiningRightPane(_context, view) {
  const job = selectedMachiningJob(view);
  const section = state(view).section;
  const [title, note] = sections[section] ?? sections.overview;
  return `<section class="tube-machining-pane">${renderMachiningPathList(view, job)}${renderMachiningPathProperties(view, job)}<header class="tube-machining-pane-title"><strong>${title}</strong><span>${section === "overview" ? "数据检查" : "待接入"}</span></header>
    <p class="tube-machining-note">${note}</p>
    ${section !== "overview" ? `<div class="tube-machining-pending">工艺模块准备中<br><small>当前支持独立名义刀路的解析与编辑，工艺轨迹将在下一阶段接入。</small></div>` : ""}
    ${job ? `<dl class="tube-machining-facts"><dt>名称</dt><dd>${escapeText(job.name)}</dd><dt>来源</dt><dd>${job.sourceKind === "nesting" ? "下料区排样结果（关联）" : escapeText(job.sourceFileName)}</dd><dt>状态</dt><dd>${job.status}</dd>
      ${job.plan ? `<dt>母材长度</dt><dd>${formatNumber(job.plan.stockLength)} mm</dd><dt>已排零件</dt><dd>${job.plan.placements.length} 件</dd><dt>余料长度</dt><dd>${formatNumber(job.plan.remainingLength)} mm</dd>` : ""}
      ${job.sourceKind === "cad" ? `<dt>实体数量</dt><dd>${escapeText(job.solidCount)} 个</dd><dt>坐标</dt><dd>保留原装配坐标</dd><dt>单位</dt><dd>mm</dd>` : ""}
      <dt>实际轨迹</dt><dd>尚未添加工艺</dd></dl>
      ${job.stale ? `<p class="tube-machining-warning">下料来源已更新或删除，已生成的独立刀路不受影响。需要分析新排样时，请重新接收。</p>` : ""}` : `<div class="tube-machining-empty-list">选择加工数据后显示详情</div>`}
  </section>`;
}

export function renderTubeMachiningViewportOverlay(_context, view) {
  const job = selectedMachiningJob(view);
  const editor = pathEditor(view, job);
  const toolbar = renderMachiningEditToolbar(view, job);
  if (!job || (job.stale && !editor?.paths.length && !editor?.drawing.length)) return `${toolbar}<div class="tube-machining-empty-stage"><span>加工区</span><h2>${job ? "排样来源已变化" : "准备三维加工"}</h2><p>${job ? "可直接绘制刀路，或接收有效的排样结果" : "接收下料排样结果，或导入 STEP / IGES 排样模型"}</p><small>解析刀路 · 三维编辑 · 二维侧面编辑</small></div>`;
  return `${renderMachiningSideEditor(view, job)}${toolbar}<div class="tube-designer-cutting-scene-header tube-machining-scene-header"><small>${editor.paths.length ? "独立名义刀路 · 未添加工艺" : "加工模型 · 待分析刀路"}</small><strong>${escapeText(job.name)}</strong><span data-tube-designer-nesting-preview-status>${escapeText(state(view).preview?.error || "三维模型与刀路预览")}</span></div>`;
}

export function renderTubeMachiningDialogs(view) {
  const selected = state(view).sourceSelection;
  if (!selected) return "";
  const plans = machiningSourcePlans(view);
  const disabled = view.pending ? "disabled" : "";
  return `<div class="tube-designer-modal-backdrop"><section class="tube-designer-preset-dialog tube-machining-source-dialog" role="dialog" aria-modal="true" aria-labelledby="machining-source-title">
    <header class="tube-designer-dialog-header"><div><strong id="machining-source-title">接收排样结果</strong><span>直接关联现有排样，不复制零件模型</span></div><button class="tube-designer-dialog-close" aria-label="关闭" data-cam-action="${PREFIX}cancel" ${disabled}>×</button></header>
    <div class="tube-machining-source-list">${plans.length ? plans.map(plan => `<label><input type="checkbox" data-cam-change-action="${PREFIX}source-check" data-plan-id="${escapeAttr(plan.id)}" ${selected.includes(String(plan.id)) ? "checked" : ""} ${disabled}><span><strong>${escapeText(plan.name)}</strong><small>${plan.placements.length} 件 · 母材 ${formatNumber(plan.stockLength)} mm</small></span></label>`).join("") : `<p class="tube-machining-note">下料区还没有排样结果。请先完成排样，或取消后直接导入 STEP / IGES。</p>`}</div>
    <footer class="tube-designer-preset-dialog-footer"><button class="tube-designer-secondary" data-cam-action="${PREFIX}cancel" ${disabled}>取消</button><button class="tube-designer-primary" data-cam-action="${PREFIX}receive" ${!selected.length || view.pending ? "disabled" : ""}>接收 ${selected.length} 根母材</button></footer>
  </section></div>`;
}

async function mutate(context, view, ops, payload) {
  if (view.pending) return;
  if (!context.sceneProxy?.invoke) throw new Error("当前项目未连接，无法更新加工数据。");
  view.pending = true;
  view.error = "";
  view.tubeDesignerOperation = { kind: "machining-data", title: payload.action === "analyze" ? "正在分析刀路" : payload.action === "import" ? "正在导入加工模型" : "正在更新加工清单", message: payload.action === "analyze" ? "BRep → CamPath，正在解析加工轮廓…" : "准备三维加工数据…" };
  ops.renderProject(context, view);
  try {
    const response = await context.sceneProxy.invoke("TubeDesigner.MachiningData", {
      ...payload, expectedRevision: designer(view).machiningTask?.revision ?? "",
    }, { timeoutMs: 180000 });
    if (!response?.machiningTask) throw new Error("未返回有效的加工清单。");
    view.scene.tubeDesigner = { ...designer(view), machiningTask: response.machiningTask,
      nestingTask: response.nestingTask ?? designer(view).nestingTask };
    state(view).selectedId = response.jobId;
    if (["save-paths", "analyze"].includes(payload.action)) for (const id of payload.jobIds) {
      const previous = state(view).edits?.[id];
      if (state(view).edits) delete state(view).edits[id];
      const job = machiningJobs(view).find(j => j.id === id);
      const editor = job ? pathEditor(view, job) : null;
      if (editor && payload.action === "save-paths" && previous) { editor.selectedIds = previous.selectedIds; editor.space = previous.space; editor.fields = previous.fields; }
      else if (editor) editor.selectedIds = editor.paths.length ? [editor.paths[0].id] : [];
    }
    if (payload.action === "remove" && state(view).edits) delete state(view).edits[payload.id];
    state(view).sourceSelection = null;
    state(view).section = "overview";
    view.tubeDesignerOwnMutation = true;
    view.notice = payload.action === "analyze" ? "已生成独立 CamPath。请核对识别提示；修改不会影响原 CAD。" : payload.action === "save-paths" ? "刀路编辑已保存。" : payload.action === "remove" ? "已移出加工清单，下料数据未改动。" : "加工数据已就绪，可分析刀路或直接绘制。";
    await context.actions?.refreshActiveSceneState?.();
  } finally {
    view.pending = false;
    view.tubeDesignerOperation = null;
    ops.renderProject(context, view);
  }
}

export async function analyzeMachiningJobs(context, view, jobs, ops, confirm = confirmWithoutTitle) {
  if (!jobs.length) throw new Error("请先接收或导入加工数据。");
  const existing = jobs.filter(job => job.analysis || pathEditor(view, job)?.dirty || pathEditor(view, job)?.drawing.length);
  if (existing.length && !await confirm(`所选范围内有 ${existing.length} 项已有刀路或编辑。重新分析将替换刀路，并丢弃此前的新增、复制、平移、旋转等全部刀路操作。是否重新分析？`, "重新分析")) return false;
  await mutate(context, view, ops, { action: "analyze", jobIds: jobs.map(j => j.id), confirmReplace: existing.length > 0 });
  return true;
}

export async function handleTubeMachiningRibbonCommand(context, view, commandId, ops) {
  if (!commandId.startsWith("machining.") && commandId !== "nesting.to-machining") return false;
  if (view.pending) return true;
  try {
    const command = commandId.replace("machining.", "");
    if (command === "analyze" || command === "analyze-all") {
      const jobs = command === "analyze-all" ? machiningJobs(view) : [selectedMachiningJob(view)].filter(Boolean);
      await analyzeMachiningJobs(context, view, jobs, ops);
    } else if (command === "edit") {
      const job = selectedMachiningJob(view);
      if (!job) throw new Error("请先选择加工数据。");
      pathEditor(view, job).space = "2d";
    } else if (command === "receive" || commandId === "nesting.to-machining") {
      // Refresh authoritative results/revision before selecting; Nest returns
      // its result separately from the normal scene snapshot.
      const response = await context.sceneProxy.invoke("TubeDesigner.List", { nestingOnly: true }, { timeoutMs: 180000 });
      if (!response?.tubeDesigner) throw new Error("无法读取下料排样结果。");
      view.scene.tubeDesigner = response.tubeDesigner;
      const ids = machiningSourcePlans(view).map(plan => String(plan.id));
      const preferred = view.tubeDesignerSelectedNestingPlanIds ?? [];
      const selected = ids.filter(id => preferred.includes(id));
      state(view).sourceSelection = selected.length ? selected : ids;
      await context.actions?.selectRibbonTab?.("machining");
      view.activeAreaId = "machining";
    } else if (command === "import") {
      const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
      if (!bridge?.openFileDialog) throw new Error("当前宿主没有提供文件选择能力。");
      const path = String(await bridge.openFileDialog({ title: "导入排样模型（STEP / IGES，最大 32 MiB）", filters: [{ name: "STEP / IGES", extensions: ["step", "stp", "iges", "igs"] }] }) ?? "").trim();
      if (!path) return true;
      if (!/\.(step|stp|iges|igs)$/i.test(path)) throw new Error("请选择 STEP / STP / IGES / IGS 文件。");
      await mutate(context, view, ops, { action: "import", sourcePath: path });
    } else if (command === "remove") {
      const job = selectedMachiningJob(view);
      if (job && await confirmWithoutTitle(`将“${job.name}”移出加工清单？下料数据不受影响。`, "移出"))
        await mutate(context, view, ops, { action: "remove", id: job.id });
    } else if (command === "fit") {
      const editor = pathEditor(view, selectedMachiningJob(view));
      if (editor?.space === "2d") editor.sideFrame = null;
      view.viewport?.fitViewToViewport?.(1.16);
    } else if (sections[command]) {
      state(view).section = command;
    }
  } catch (error) { view.error = error?.message ?? String(error); }
  ops.renderProject(context, view);
  return true;
}

export async function handleTubeMachiningAction(context, view, action, target, ops) {
  if (await handleMachiningPathAction(context, view, selectedMachiningJob(view), action, target, ops, {
    confirm: confirmWithoutTitle,
    save: editor => mutate(context, view, ops, { action: "save-paths", jobIds: [editor.jobId], paths: editor.paths, analysisRevision: editor.baseRevision }),
  })) return { handled: true };
  if (!action.startsWith(PREFIX)) return { handled: false };
  if (view.pending) return { handled: true };
  try {
    if (action === PREFIX + "select") state(view).selectedId = String(target?.dataset?.jobId ?? "");
    if (action === PREFIX + "cancel") state(view).sourceSelection = null;
    if (action === PREFIX + "source-check") {
      const ids = new Set(state(view).sourceSelection ?? []);
      const id = String(target?.dataset?.planId ?? "");
      if (target?.checked) ids.add(id); else ids.delete(id);
      state(view).sourceSelection = [...ids];
    }
    if (action === PREFIX + "receive") {
      const ids = state(view).sourceSelection ?? [];
      if (!ids.length) throw new Error("请选择排样结果。");
      await mutate(context, view, ops, { action: "link", planIds: ids, nestingRevision: designer(view).nestingTask?.revision });
    }
  } catch (error) { view.error = error?.message ?? String(error); }
  ops.renderProject(context, view);
  return { handled: true };
}

export function attachTubeMachining(context, view, _mount, ops) {
  const currentState = state(view);
  if (view.activeAreaId !== "machining") { currentState.preview = null; return; }
  const job = selectedMachiningJob(view);
  const editor = pathEditor(view, job);
  attachMachiningSideEditor(context, view, job, ops);
  const viewport = view.viewport;
  if (!viewport?.applyViewSnapshot) return;
  if (editor?.space === "2d") { cancelNestingPlanHydration(view); currentState.preview = null; return; }
  const overlay = job ? machiningPathOverlay(view, job) : { revision: "empty", rows: [], resources: new Map() };
  view.tubeDesignerMachiningPreviewPlanId = job?.plan?.id ?? "";
  if (job?.plan) {
    currentState.preview = null;
    scheduleNestingPlanHydration(context, view, job.plan, listNestingParts(designer(view)), { areaId: "machining", overlay });
    return;
  }
  cancelNestingPlanHydration(view);
  const showCad = job?.sourceKind === "cad" && !job.stale;
  const key = `${showCad ? `${job.id}@${job.resourceVersion}` : "empty"}:${overlay.revision}`;
  const previous = currentState.preview;
  const entityId = `machining:${job?.id}`;
  if (previous?.key === key && previous.viewport === viewport) {
    if (previous.ready) viewport.setVisibleEntityIds?.([...(showCad ? [entityId] : []), ...overlay.rows.map(row => row.entityId)]);
    return;
  }
  const request = { key, viewport, ready: false, error: "" };
  currentState.preview = request;
  viewport.setVisibleEntityIds?.([]);
  viewport.setDimensionAnnotations?.([]);
  viewport.setPresentationAxis?.([1, 0, 0], [1, 0, 0]);
  const current = () => currentState.preview === request && view.activeAreaId === "machining" && view.viewport === viewport;
  request.promise = (async () => {
    try {
      currentState.cadPreviews ??= new Map();
      const cadKey = `${job?.id}@${job?.resourceVersion}`;
      const data = !showCad ? null : currentState.cadPreviews.get(cadKey) ?? await context.sceneProxy.invoke("TubeDesigner.MachiningData", { action: "preview", id: job.id }, { timeoutMs: 180000 });
      if (data) { currentState.cadPreviews.set(cadKey, data); while (currentState.cadPreviews.size > 16) currentState.cadPreviews.delete(currentState.cadPreviews.keys().next().value); }
      if (!current()) return;
      if (data && (!data.geometryResourceId || !(Number(data.geometryResourceVersion) > 0))) throw new Error("模型三维资源不可用。");
      const receipt = await viewport.applyViewSnapshot({ revision: `machining:${key}`, rows: [...(data ? [{ entityId, data: {
        geometry: { url: data.geometryResourceId, version: data.geometryResourceVersion }, geometryKind: 1, renderClass: 1, visible: true, selectable: false,
      } }] : []), ...overlay.rows] }, { get: async (url, options) => overlay.resources.has(String(url)) ? new Response(overlay.resources.get(String(url))) : context.sceneProxy?.resources?.get(url, options) });
      if (!current()) return;
      if (data && !receipt?.applied) throw new Error("加工模型未完成显示。");
      request.ready = true;
      if ((data || overlay.rows.length) && currentState.fitJobId !== job?.id) { viewport.setStandardView?.("iso"); viewport.fitViewToViewport?.(1.16); currentState.fitJobId = job?.id; }
    } catch (error) {
      if (current()) {
        request.error = `CAD 背景不可用：${error?.message ?? error}`;
        // Independent paths stay usable even if the original CAD resource is
        // missing or its render conversion fails. Never gate editing on CAD.
        try {
          const receipt = await viewport.applyViewSnapshot({ revision: `machining-paths:${key}`, rows: overlay.rows }, {
            get: async url => new Response(overlay.resources.get(String(url))),
          });
          if (!current()) return;
          request.ready = Boolean(receipt?.applied);
          if (request.ready && overlay.rows.length && currentState.fitJobId !== job?.id) {
            viewport.setStandardView?.("iso"); viewport.fitViewToViewport?.(1.16); currentState.fitJobId = job?.id;
          }
        } catch (pathError) { if (current()) request.error += `；刀路显示失败：${pathError?.message ?? pathError}`; }
      }
    } finally { if (current()) ops.renderProject(context, view); }
  })();
}

export function handleTubeMachiningViewportPick(context, view, userData, hit, event, hits, ops) {
  return handleMachiningViewportPick(context, view, userData, hit, event, hits, ops, selectedMachiningJob(view));
}
