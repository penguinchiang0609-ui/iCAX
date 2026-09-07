import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { catalogText } from "./productCatalog.mjs";

const PREFIX = "tube-designer-component-";
const EDITABLE_FIELDS = ["name", "category", "sourcing", "material", "description"];
const MODEL_ICON = '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M16 3 28 10v13L16 30 4 23V10Z M4 10l12 7 12-7 M16 17v13" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>';

export function componentLibraryState(view) {
  return view.tubeDesignerComponentLibrary ??= {
    models: [], selectedKey: "", scope: "all", search: "", collapsed: [], drafts: {},
    loadState: "idle", error: "", previewCache: new Map(), previewFailureKey: "",
  };
}

export function componentModelKey(model) {
  return `${model?.scope === "system" ? "system" : "user"}:${String(model?.id ?? "")}`;
}

export function getComponentModels(view) {
  return [...(componentLibraryState(view).models ?? [])]
    .filter((model) => model?.id && ["system", "user"].includes(model.scope))
    .sort((a, b) => (a.scope === b.scope ? 0 : a.scope === "system" ? -1 : 1)
      || String(a.category ?? "").localeCompare(String(b.category ?? ""), "zh-CN")
      || String(a.name ?? "").localeCompare(String(b.name ?? ""), "zh-CN"));
}

function modelByKey(view, key) {
  return getComponentModels(view).find((model) => componentModelKey(model) === key) ?? null;
}

function selectedModel(view) {
  const state = componentLibraryState(view);
  const models = getComponentModels(view);
  if (!models.some((model) => componentModelKey(model) === state.selectedKey))
    state.selectedKey = models[0] ? componentModelKey(models[0]) : "";
  return models.find((model) => componentModelKey(model) === state.selectedKey) ?? null;
}

function sourcingLabel(value) { return value === "made" ? "自制" : "外购"; }
function categoryName(model) { return String(model?.category ?? "").trim() || "未分类"; }
function modelName(model) { return String(model?.name ?? model?.sourceFileName ?? model?.id ?? "三维配件"); }
function dimensionText(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) && numeric > 0 ? formatNumber(numeric) : "—";
}
function boundsText(model) {
  return ["width", "depth", "height"].map((key) => dimensionText(model?.bounds?.[key])).join(" × ") + " mm";
}

export function renderComponentLibraryLeftPane(_context, view) {
  const state = componentLibraryState(view);
  const all = getComponentModels(view);
  selectedModel(view);
  const query = String(state.search ?? "").toLocaleLowerCase();
  const visible = all.filter((model) => (state.scope === "all" || model.scope === state.scope)
    && [model.name, model.category, model.material, model.sourceFileName]
      .some((value) => String(value ?? "").toLocaleLowerCase().includes(query)));
  const groups = new Map();
  for (const model of visible) {
    const key = JSON.stringify([model.scope, categoryName(model)]);
    if (!groups.has(key)) groups.set(key, { key, scope: model.scope, category: categoryName(model), models: [] });
    groups.get(key).models.push(model);
  }
  return `<div class="tube-designer-panel tube-component-library-panel">
    <div class="tube-designer-heading"><div><strong>配件库</strong><span>${all.filter((m) => m.scope === "system").length} 个系统内置 · ${all.filter((m) => m.scope === "user").length} 个我的模型</span></div></div>
    <div class="tube-component-library-filters">
      <input type="search" aria-label="搜索配件" placeholder="搜索名称、分类、材料" value="${escapeAttr(state.search)}" data-cam-change-action="${PREFIX}search" />
      <div>${[["all", "全部"], ["system", "系统内置"], ["user", "我的模型"]].map(([scope, label]) =>
        `<button type="button" class="${scope === state.scope ? "selected" : ""}" data-cam-action="${PREFIX}scope" data-component-scope="${scope}" aria-pressed="${scope === state.scope}">${label}</button>`).join("")}</div>
    </div>
    <div class="tube-component-library-list">
      ${state.loadState === "loading" ? '<div class="tube-designer-empty">正在读取配件库…</div>' : ""}
      ${state.loadState === "error" ? `<div class="tube-component-library-error" role="alert">${escapeText(state.error)}<button class="tube-designer-secondary" data-cam-action="${PREFIX}refresh">重新读取</button></div>` : ""}
      ${[...groups.values()].map((group) => {
        const expanded = !state.collapsed.includes(group.key);
        return `<section class="tube-component-library-group">
          <button type="button" class="tube-component-library-group-heading" data-cam-action="${PREFIX}toggle-category" data-component-category="${escapeAttr(group.key)}" aria-expanded="${expanded}"><span>${expanded ? "▾" : "▸"} ${escapeText(group.category)}</span><small>${group.scope === "system" ? "系统" : "我的"} · ${group.models.length}</small></button>
          <div ${expanded ? "" : "hidden"}>${group.models.map((model) => {
            const key = componentModelKey(model);
            return `<button type="button" class="tube-component-library-card ${key === state.selectedKey ? "selected" : ""}" data-cam-action="${PREFIX}select" data-component-key="${escapeAttr(key)}" aria-pressed="${key === state.selectedKey}">
              <span class="tube-component-library-icon">${MODEL_ICON}</span><span><strong>${escapeText(modelName(model))}</strong><small>${sourcingLabel(model.sourcing)}${model.material ? ` · ${escapeText(model.material)}` : ""}</small><em>${escapeText(boundsText(model))}</em></span></button>`;
          }).join("")}</div></section>`;
      }).join("")}
      ${!visible.length && state.loadState !== "loading" ? `<div class="tube-designer-empty">${all.length ? "没有匹配的配件，请调整搜索或来源。" : "还没有可用配件。导入 STEP / STP / BREP 建立我的模型。"}</div>` : ""}
    </div>
    <footer><button class="tube-designer-primary" data-cam-action="${PREFIX}import" ${view.pending ? "disabled" : ""}>导入三维模型</button><button class="tube-designer-secondary" data-cam-action="${PREFIX}refresh" ${view.pending || state.loadState === "loading" ? "disabled" : ""}>刷新</button></footer>
  </div>`;
}

function metadataFields(values, disabled, action) {
  return EDITABLE_FIELDS.map((key) => {
    const common = `data-component-field="${key}" data-cam-change-action="${PREFIX}${action}" ${disabled ? "disabled" : ""}`;
    const value = String(values[key] ?? "");
    if (key === "sourcing") return `<label class="tube-designer-field wide"><span>供料方式</span><select ${common}><option value="purchased" ${value !== "made" ? "selected" : ""}>外购</option><option value="made" ${value === "made" ? "selected" : ""}>自制</option></select></label>`;
    const label = { name: "模型名称", category: "分类", material: "材料", description: "说明" }[key];
    return `<label class="tube-designer-field wide"><span>${label}</span>${key === "description"
      ? `<textarea rows="4" maxlength="2000" ${common}>${escapeText(value)}</textarea>`
      : `<input type="text" maxlength="${key === "name" ? 120 : 160}" value="${escapeAttr(value)}" ${common} />`}</label>`;
  }).join("");
}

export function renderComponentLibraryRightPane(_context, view) {
  const state = componentLibraryState(view);
  const model = selectedModel(view);
  if (!model) return '<div class="tube-designer-panel"><div class="tube-designer-heading"><strong>配件属性</strong></div><div class="tube-designer-empty">从左侧选择配件，中央显示真实三维模型。</div></div>';
  const key = componentModelKey(model);
  const system = model.scope === "system";
  const draft = state.drafts[key];
  const values = draft ?? model;
  return `<div class="tube-designer-panel tube-component-library-editor" data-component-editor data-component-key="${escapeAttr(key)}">
    <div class="tube-designer-heading"><div><strong>${system ? "系统内置配件" : "我的模型"}</strong><span>${system ? "只读资源，不可修改或删除" : "编辑名称、分类与制造属性"}</span></div></div>
    <div class="tube-component-library-editor-body">
      ${metadataFields(values, system || view.pending, "draft")}
      <div class="tube-component-library-bounds"><strong>模型外包尺寸（mm）</strong><dl>${[["width", "宽 X"], ["depth", "深 Y"], ["height", "高 Z"]].map(([field, label]) => `<div><dt>${label}</dt><dd>${escapeText(dimensionText(model.bounds?.[field]))}</dd></div>`).join("")}</dl><small>尺寸来自真实模型，不能在此修改或缩放。</small></div>
      <div class="tube-component-library-source"><span>来源文件</span><strong>${escapeText(model.sourceFileName || "系统内置模型")}</strong></div>
      <p class="tube-component-library-note">配件是非管材三维模型。“自制”仅记录供料方式，具体制造工艺仍需确认，不表示可直接使用管材加工。</p>
      ${state.error && state.loadState !== "error" ? `<div class="tube-component-library-error" role="alert">${escapeText(state.error)}</div>` : ""}
    </div>
    ${!system ? `<footer><button class="tube-designer-danger" data-cam-action="${PREFIX}delete" data-component-key="${escapeAttr(key)}" ${view.pending ? "disabled" : ""}>删除</button><button class="tube-designer-primary" data-cam-action="${PREFIX}save" data-component-key="${escapeAttr(key)}" ${view.pending ? "disabled" : ""}>保存属性</button></footer>` : ""}
  </div>`;
}

export function renderComponentLibraryViewportOverlay(_context, view) {
  const state = componentLibraryState(view);
  const model = selectedModel(view);
  const pending = Boolean(state.previewRequest);
  return `<div class="tube-component-library-hud"><strong>${escapeText(model ? modelName(model) : "三维配件预览")}</strong><span>${model ? escapeText(boundsText(model)) : "选择已有配件或导入三维模型"}</span><small>${pending ? "正在装载真实三维几何…" : "拖动旋转 · 滚轮缩放 · 支持透视 / 正交"}</small>${state.previewError ? `<p role="alert">${escapeText(state.previewError)}</p><button class="tube-designer-secondary" data-cam-action="${PREFIX}retry-preview">重新预览</button>` : ""}</div>`;
}

export function renderComponentLibraryDialogs(view) {
  const state = componentLibraryState(view);
  if (state.importDraft) return `<div class="tube-designer-modal-backdrop" role="presentation"><section class="tube-designer-preset-dialog tube-component-library-dialog" role="dialog" aria-modal="true" aria-labelledby="component-import-title">
    <header class="tube-designer-dialog-header"><div><strong id="component-import-title">导入三维配件</strong><span>${escapeText(state.importDraft.sourceFileName)}</span></div><button class="tube-designer-dialog-close" data-cam-action="${PREFIX}cancel-import" aria-label="取消导入" ${view.pending ? "disabled" : ""}>×</button></header>
    <div class="tube-designer-preset-dialog-body" data-component-import-form>${metadataFields(state.importDraft, view.pending, "import-draft")}<p class="tube-component-library-note">导入后保存为“我的模型”。保留原始三维形状，默认按外购配件管理。</p></div>
    <footer class="tube-designer-preset-dialog-footer"><button class="tube-designer-secondary" data-cam-action="${PREFIX}cancel-import" ${view.pending ? "disabled" : ""}>取消</button><button class="tube-designer-primary" data-cam-action="${PREFIX}confirm-import" ${view.pending ? "disabled" : ""}>${view.pending ? "正在导入…" : "导入"}</button></footer>
  </section></div>`;
  if (state.deleteDialog) return `<div class="tube-designer-modal-backdrop" role="presentation"><section class="tube-designer-preset-dialog" role="dialog" aria-modal="true" aria-labelledby="component-delete-title"><header class="tube-designer-dialog-header"><strong id="component-delete-title">删除我的模型</strong></header><div class="tube-designer-preset-dialog-body"><p>确定从配件库删除“${escapeText(state.deleteDialog.name)}”吗？</p><p>此操作删除库记录，不会删除导入时选择的原始文件。</p></div><footer class="tube-designer-preset-dialog-footer"><button class="tube-designer-secondary" data-cam-action="${PREFIX}cancel-delete" ${view.pending ? "disabled" : ""}>取消</button><button class="tube-designer-danger" data-cam-action="${PREFIX}confirm-delete" ${view.pending ? "disabled" : ""}>确认删除</button></footer></section></div>`;
  return "";
}

export function getComponentModelOptions(template, view) {
  const templateModels = template?.extensions?.modelResources;
  const result = templateModels && typeof templateModels === "object" && !Array.isArray(templateModels)
    ? Object.entries(templateModels).map(([key, model]) => ({
      value: `template:${key}`, group: "模板自带", label: catalogText(model?.displayName, catalogText(model?.name, key)),
    })) : [];
  for (const model of getComponentModels(view)) result.push({
    value: `${model.scope === "system" ? "system" : "library"}:${model.id}`,
    group: model.scope === "system" ? "系统内置" : "我的模型",
    label: `${modelName(model)} · ${categoryName(model)} · ${sourcingLabel(model.sourcing)}`,
  });
  return result;
}

export function renderComponentModelField(field, value, disabled, context = {}) {
  const view = context?.view ?? {};
  const designer = view.scene?.tubeDesigner ?? {};
  const templateId = context?.mode === "add" ? view.tubeDesignerAddTemplateId : designer.product?.templateId;
  const template = context?.template ?? (designer.templates ?? []).find((item) => item.id === templateId);
  const state = componentLibraryState(view);
  const options = getComponentModelOptions(template, view);
  const current = String(value ?? "");
  const name = String(field.key ?? field.name ?? "");
  const groups = ["模板自带", "系统内置", "我的模型"];
  const missing = current && !options.some((option) => option.value === current);
  return `<div class="tube-designer-field wide tube-component-model-field"><label><span>${escapeText(catalogText(field.displayName, field.label ?? name))}</span><select data-tube-designer-parameter="${escapeAttr(name)}" data-cam-change-action="tube-designer-parameter-change" ${disabled || field.readOnly ? "disabled" : ""}>
    <option value="" ${current ? "" : "selected"}>未选择配件</option>
    ${missing ? `<option value="${escapeAttr(current)}" selected>当前引用：${escapeText(current)}（${state.loadState === "loaded" ? "库中未找到" : "尚未读取"}）</option>` : ""}
    ${groups.map((group) => {
      const items = options.filter((option) => option.group === group);
      return items.length ? `<optgroup label="${group}">${items.map((option) => `<option value="${escapeAttr(option.value)}" ${option.value === current ? "selected" : ""}>${escapeText(option.label)}</option>`).join("")}</optgroup>` : "";
    }).join("")}</select></label><small>选择完整三维配件，不作为管材截面。模型不自动缩放，请核对用途、安装面与尺寸。</small><button type="button" class="tube-designer-secondary" data-cam-action="${PREFIX}refresh" ${view.pending || state.loadState === "loading" ? "disabled" : ""}>${state.loadState === "loading" ? "正在读取配件…" : "刷新可选配件"}</button>${state.loadState === "error" ? `<small role="alert">${escapeText(state.error)}</small>` : ""}</div>`;
}

function productInvoke(context, method, payload = {}) {
  if (typeof context.productProxy?.invoke !== "function") throw new Error("当前宿主未提供配件库接口。");
  return context.productProxy.invoke(method, payload, { timeoutMs: 120000 });
}

export async function refreshComponentModels(context, view, ops = null) {
  const state = componentLibraryState(view);
  if (state.loadPromise) return state.loadPromise;
  state.loadState = "loading";
  state.error = "";
  const mutationVersion = state.mutationVersion ?? 0;
  const request = Promise.resolve().then(async () => {
    try {
      const response = await productInvoke(context, "TubeDesigner.ListComponentModels");
      if (!Array.isArray(response?.models)) throw new Error("配件库没有返回有效的模型列表。");
      if ((state.mutationVersion ?? 0) === mutationVersion) state.models = response.models;
      state.loadState = "loaded";
      selectedModel(view);
      return state.models;
    } catch (error) {
      state.loadState = "error";
      state.error = `配件库读取失败：${error?.message ?? error}`;
      return null;
    } finally {
      state.loadPromise = null;
      ops?.renderProject?.(context, view);
    }
  });
  state.loadPromise = request;
  ops?.renderProject?.(context, view);
  return request;
}

function updateModel(view, model) {
  if (!model?.id || !["system", "user"].includes(model.scope)) throw new Error("配件库未返回有效的模型记录。");
  const state = componentLibraryState(view);
  const key = componentModelKey(model);
  state.models = [...state.models.filter((item) => componentModelKey(item) !== key), model];
  state.mutationVersion = (state.mutationVersion ?? 0) + 1;
  state.selectedKey = key;
  delete state.drafts[key];
  state.previewFailureKey = "";
}

function readMetadata(context, selector, seed) {
  const values = { ...seed };
  const form = context.mount?.querySelector?.(selector);
  for (const input of form?.querySelectorAll?.("[data-component-field]") ?? []) {
    const key = input.dataset?.componentField;
    if (EDITABLE_FIELDS.includes(key)) values[key] = String(input.value ?? "");
  }
  const result = Object.fromEntries(EDITABLE_FIELDS.map((key) => [key, String(values[key] ?? "").trim()]));
  if (!result.name) throw new Error("请填写模型名称。");
  if (!result.category) result.category = "未分类";
  if (!["made", "purchased"].includes(result.sourcing)) throw new Error("请选择自制或外购。");
  return result;
}

async function componentTask(context, view, ops, title, work) {
  if (view.pending) return null;
  const state = componentLibraryState(view);
  const operation = { kind: "components", title, message: "正在处理配件库数据" };
  view.pending = true;
  view.tubeDesignerOperation = operation;
  state.error = "";
  view.error = "";
  ops.renderProject(context, view);
  try { return await work(); }
  catch (error) { state.error = error?.message ?? String(error); view.error = state.error; throw error; }
  finally {
    view.pending = false;
    if (view.tubeDesignerOperation === operation) view.tubeDesignerOperation = null;
    ops.renderProject(context, view);
  }
}

async function chooseComponentModel(context, view, ops) {
  if (view.pending) return null;
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
  if (typeof bridge?.openFileDialog !== "function") throw new Error("当前宿主没有提供文件选择能力。");
  const sourcePath = String(await bridge.openFileDialog({ title: "导入三维配件", filters: [{ name: "三维模型", extensions: ["step", "stp", "brep"] }] }) ?? "").trim();
  if (!sourcePath) return null;
  if (!/\.(?:step|stp|brep)$/i.test(sourcePath)) throw new Error("请选择 STEP、STP 或 BREP 文件。");
  const sourceFileName = sourcePath.split(/[\\/]/).at(-1);
  componentLibraryState(view).importDraft = {
    sourcePath, sourceFileName, name: sourceFileName.replace(/\.[^.]+$/, ""),
    category: "常用配件", sourcing: "purchased", material: "", description: "",
  };
  ops.renderProject(context, view);
  return sourcePath;
}

export async function handleComponentLibraryAction(context, view, action, target, ops) {
  if (!String(action).startsWith(PREFIX)) return { handled: false };
  const state = componentLibraryState(view);
  const suffix = action.slice(PREFIX.length);
  if (view.pending) return { handled: true };
  if (suffix === "refresh") return { handled: true, result: await refreshComponentModels(context, view, ops) };
  if (suffix === "import") return { handled: true, result: await chooseComponentModel(context, view, ops) };
  if (suffix === "select") {
    const key = String(target?.dataset?.componentKey ?? "");
    if (modelByKey(view, key)) { state.selectedKey = key; state.previewError = ""; }
  } else if (suffix === "scope") {
    const scope = target?.dataset?.componentScope;
    if (["all", "system", "user"].includes(scope)) state.scope = scope;
  } else if (suffix === "search") state.search = String(target?.value ?? "");
  else if (suffix === "toggle-category") {
    const key = String(target?.dataset?.componentCategory ?? "");
    state.collapsed = state.collapsed.includes(key) ? state.collapsed.filter((item) => item !== key) : [...state.collapsed, key];
  } else if (suffix === "draft") {
    const key = String(target?.closest?.("[data-component-editor]")?.dataset?.componentKey ?? state.selectedKey);
    const model = modelByKey(view, key);
    const field = target?.dataset?.componentField;
    if (model?.scope === "user" && EDITABLE_FIELDS.includes(field)) {
      state.drafts[key] ??= { ...model, expectedRevision: model.revision };
      state.drafts[key][field] = String(target.value ?? "");
    }
    return { handled: true }; // Keep focus and text selection while editing.
  } else if (suffix === "import-draft") {
    const field = target?.dataset?.componentField;
    if (state.importDraft && EDITABLE_FIELDS.includes(field)) state.importDraft[field] = String(target.value ?? "");
    return { handled: true };
  } else if (suffix === "cancel-import") state.importDraft = null;
  else if (suffix === "confirm-import") {
    if (!state.importDraft?.sourcePath) return { handled: true };
    const metadata = readMetadata(context, "[data-component-import-form]", state.importDraft);
    const sourcePath = state.importDraft.sourcePath;
    return { handled: true, result: await componentTask(context, view, ops, "正在导入三维配件", async () => {
      const response = await productInvoke(context, "TubeDesigner.ImportComponentModel", { sourcePath, ...metadata });
      updateModel(view, response?.model);
      state.importDraft = null;
      state.scope = "all";
      state.search = "";
      ops.showNotice?.(context, view, `已导入“${modelName(response.model)}”。`);
      return response.model;
    }) };
  } else if (suffix === "save") {
    const model = modelByKey(view, String(target?.dataset?.componentKey ?? state.selectedKey));
    if (model?.scope !== "user") return { handled: true };
    const draft = state.drafts[componentModelKey(model)];
    const metadata = readMetadata(context, "[data-component-editor]", draft ?? model);
    return { handled: true, result: await componentTask(context, view, ops, "正在保存配件属性", async () => {
      const response = await productInvoke(context, "TubeDesigner.UpdateComponentModel", { id: model.id, expectedRevision: draft?.expectedRevision ?? model.revision, ...metadata });
      updateModel(view, response?.model);
      ops.showNotice?.(context, view, `已保存“${modelName(response.model)}”。`);
      return response.model;
    }) };
  } else if (suffix === "delete") {
    const model = modelByKey(view, String(target?.dataset?.componentKey ?? state.selectedKey));
    if (model?.scope === "user") state.deleteDialog = { key: componentModelKey(model), name: modelName(model), expectedRevision: model.revision };
  } else if (suffix === "cancel-delete") state.deleteDialog = null;
  else if (suffix === "confirm-delete") {
    const confirmation = state.deleteDialog;
    const model = modelByKey(view, confirmation?.key);
    if (!confirmation || model?.scope !== "user") return { handled: true };
    return { handled: true, result: await componentTask(context, view, ops, "正在删除配件模型", async () => {
      const response = await productInvoke(context, "TubeDesigner.DeleteComponentModel", { id: model.id, expectedRevision: confirmation.expectedRevision });
      if (response?.deleted !== true) throw new Error("配件库没有确认删除成功。");
      state.models = state.models.filter((item) => componentModelKey(item) !== confirmation.key);
      state.mutationVersion = (state.mutationVersion ?? 0) + 1;
      delete state.drafts[confirmation.key];
      state.deleteDialog = null;
      selectedModel(view);
      ops.showNotice?.(context, view, `已从配件库删除“${confirmation.name}”。`);
      return response;
    }) };
  } else if (suffix === "retry-preview") { state.previewFailureKey = ""; state.previewError = ""; }
  else return { handled: false };
  ops.renderProject(context, view);
  return { handled: true };
}

export async function handleComponentLibraryRibbonCommand(context, view, commandId, ops) {
  if (commandId === "components.import") { await chooseComponentModel(context, view, ops); return true; }
  if (commandId === "components.refresh") { await refreshComponentModels(context, view, ops); return true; }
  return false;
}

function previewKey(model) { return model ? JSON.stringify([model.scope, model.id, model.revision]) : ""; }

export async function ensureComponentModelPreview(context, view, ops = null) {
  const state = componentLibraryState(view);
  if (view.activeAreaId !== "components" || !view.viewport?.applyViewSnapshot) return;
  const model = selectedModel(view);
  const key = previewKey(model);
  if (!model) {
    if (view.viewport.getAppliedViewState?.().revision !== "components-empty")
      await view.viewport.applyViewSnapshot({ revision: "components-empty", rows: [] }, context.sceneProxy?.resources);
    return;
  }
  if (state.previewRequest?.key === key) return state.previewRequest.promise;
  if (state.previewFailureKey === key) return;
  const entityId = `component-model:${componentModelKey(model)}`;
  const cached = state.previewCache.get(key);
  const revisionFor = (response) => `component-preview:${key}:${response.geometryResourceId}:${response.geometryResourceVersion}`;
  if (cached && view.viewport.getAppliedViewState?.().revision === revisionFor(cached)) {
    // The shared workbench hides entities when remounting self-managed areas.
    // Restore visibility even when geometry is already applied, without refitting.
    view.viewport.setVisibleEntityIds?.([entityId]);
    view.viewport.setSelectedObjectIds?.([], "");
    return;
  }
  const request = { key, promise: null };
  state.previewRequest = request;
  state.previewError = "";
  view.viewport.setVisibleEntityIds?.([]);
  const current = () => state.previewRequest === request && view.activeAreaId === "components" && previewKey(selectedModel(view)) === key;
  request.promise = (async () => {
    try {
      if (typeof context.sceneProxy?.invoke !== "function") throw new Error("当前场景未提供配件预览接口。");
      const response = cached ?? await context.sceneProxy.invoke("TubeDesigner.GenerateComponentModelPreview", { scope: model.scope, id: model.id }, { timeoutMs: 120000 });
      if (!current()) return;
      const geometryId = String(response?.geometryResourceId ?? "");
      const geometryVersion = Number(response?.geometryResourceVersion);
      if (!geometryId || !Number.isFinite(geometryVersion) || geometryVersion <= 0) throw new Error("没有返回有效的三维几何资源。");
      state.previewCache.set(key, response);
      while (state.previewCache.size > 16) state.previewCache.delete(state.previewCache.keys().next().value);
      const data = { geometry: { url: geometryId, version: geometryVersion }, geometryKind: 1, renderClass: 1, visible: true, selectable: true };
      if (response.materialResourceId && Number(response.materialResourceVersion) > 0)
        data.material = { url: String(response.materialResourceId), version: Number(response.materialResourceVersion) };
      const revision = revisionFor(response);
      const apply = async () => {
        if (!current()) return;
        view.viewport.setDimensionAnnotations?.([]);
        const receipt = await view.viewport.applyViewSnapshot({ revision, rows: [{ entityId, data }] }, context.sceneProxy.resources);
        if (!current()) return;
        if (!receipt?.applied) throw new Error("三维资源尚未进入配件预览视图。");
        view.viewport.setVisibleEntityIds?.([entityId]);
        view.viewport.setSelectedObjectIds?.([], "");
        view.viewport.fitViewForRevision?.(revision, 1.25);
        view.viewport.setStandardView?.("iso");
      };
      state.applyTail = Promise.resolve(state.applyTail).catch(() => {}).then(apply);
      await state.applyTail;
    } catch (error) {
      if (current()) { state.previewFailureKey = key; state.previewError = `配件预览失败：${error?.message ?? error}`; }
    } finally {
      if (state.previewRequest === request) state.previewRequest = null;
      if (view.activeAreaId === "components") ops?.renderProject?.(context, view);
    }
  })();
  ops?.renderProject?.(context, view);
  return request.promise;
}

export function attachComponentLibrary(context, view, _mount, ops) {
  const state = componentLibraryState(view);
  const designer = view.scene?.tubeDesigner ?? {};
  const templateId = view.tubeDesignerAddDialogOpen ? view.tubeDesignerAddTemplateId : designer.product?.templateId;
  const template = (designer.templates ?? []).find((item) => item.id === templateId);
  const needsModels = view.activeAreaId === "components" || (template?.parameters ?? []).some((field) => field.presentation?.editor === "component-model");
  if (needsModels && state.loadState === "idle") queueMicrotask(() => {
    if (state.loadState === "idle") void refreshComponentModels(context, view, ops);
  });
  if (view.activeAreaId === "components") queueMicrotask(() => { void ensureComponentModelPreview(context, view, ops).catch(() => {}); });
}
