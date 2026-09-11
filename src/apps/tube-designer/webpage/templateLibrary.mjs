import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";
import {
  buildCatalogEntries,
  getTemplateVisualAsset,
} from "./productCatalog.mjs";
import { captureScrollAnchor, restoreScrollAnchor } from "./scrollAnchor.mjs";

// Product templates are managed as portable .itpt archives.  The dialog keeps
// built-in packages read-only and exposes imported personal packages separately
// so an accidental delete can never remove an installed template.
export function renderProductTemplateManagerDialog(view) {
  const state = view?.tubeDesignerTemplateManager;
  if (!state) return "";
  const designer = view?.scene?.tubeDesigner ?? {};
  const builtins = Array.isArray(designer.templates) ? designer.templates : [];
  const custom = Array.isArray(view?.tubeDesignerUserData?.productTemplates)
    ? view.tubeDesignerUserData.productTemplates : [];
  const selectedId = String(state.selectedId ?? "");
  const create = state.mode === "create";
  const selectedCustom = custom.find((item) => String(item?.id ?? "") === selectedId);
  const selectedBuiltin = builtins.find((item) => String(item?.id ?? "") === selectedId);
  const selected = selectedCustom
    ? { ...selectedCustom, scope: "personal" }
    : selectedBuiltin ? { ...selectedBuiltin, scope: "builtin" } : null;
  if (create) {
    const baseId = String(state.baseTemplateId ?? builtins.find((item) => item?.available)?.id ?? "");
    return `<div class="tube-designer-modal-backdrop tube-designer-template-manager-backdrop" role="presentation">
      <section class="tube-designer-template-manager-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-template-create-title">
        <header class="tube-designer-dialog-header"><div><strong id="tube-template-create-title">新增产品模板</strong><span>以现有模板为基础创建个人模板包，保存后可导出为 .itpt。</span></div><button class="tube-designer-dialog-close" data-cam-action="tube-designer-template-manager-close" aria-label="关闭">×</button></header>
        <div class="tube-designer-template-manager-body">
          <label class="tube-designer-field wide"><span>基础模板</span><select data-tube-template-create-base>${builtins.filter((item) => item?.available).map((item) => `<option value="${escapeAttr(item.id)}" ${item.id === baseId ? "selected" : ""}>${escapeText(item.name ?? item.id)} · ${escapeText(item.version ?? "")}</option>`).join("")}</select><small>新模板沿用基础模板的参数和几何脚本，后续可在模板包中继续扩展。</small></label>
          <label class="tube-designer-field wide"><span>模板名称</span><input type="text" maxlength="120" data-tube-template-create-name value="${escapeAttr(state.name ?? "")}" placeholder="例如：客户 A · 直跑护栏" autofocus /></label>
          <label class="tube-designer-field wide"><span>模板说明</span><textarea rows="3" maxlength="500" data-tube-template-create-description>${escapeText(state.description ?? "")}</textarea></label>
          <p class="tube-designer-template-manager-note">创建只写入“我的模板”目录，不会修改内置模板。模板包是加密 ZIP 压缩格式，扩展名固定为 <code>.itpt</code>，密码由产品固定 magic number 自动处理。</p>
        </div>
        <footer class="tube-designer-preset-dialog-footer"><span></span><button class="tube-designer-secondary" data-cam-action="tube-designer-template-manager-close">取消</button><button class="tube-designer-primary" data-cam-action="tube-designer-template-create-confirm" ${view.pending ? "disabled" : ""}>${view.pending ? "正在保存…" : "创建模板"}</button></footer>
      </section>
    </div>`;
  }
  const rows = [
    ...builtins.filter((item) => item?.available).map((item) => ({ ...item, scope: "builtin", id: String(item.id) })),
    ...custom.map((item) => ({ ...item, scope: "personal", id: String(item.id ?? "") })),
  ];
  return `<div class="tube-designer-modal-backdrop tube-designer-template-manager-backdrop" role="presentation">
    <section class="tube-designer-template-manager-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-template-manager-title">
      <header class="tube-designer-dialog-header"><div><strong id="tube-template-manager-title">产品模板管理</strong><span>内置模板 ${builtins.filter((item) => item?.available).length} 个 · 我的模板 ${custom.length} 个</span></div><button class="tube-designer-dialog-close" data-cam-action="tube-designer-template-manager-close" aria-label="关闭">×</button></header>
      <div class="tube-designer-template-manager-toolbar"><button class="tube-designer-primary" data-cam-action="tube-designer-template-create-open">＋ 新增模板</button><button class="tube-designer-secondary" data-cam-action="tube-designer-template-import">导入 .itpt</button><button class="tube-designer-secondary" data-cam-action="tube-designer-template-export" ${selected ? "" : "disabled"}>导出 .itpt</button><button class="tube-designer-danger" data-cam-action="tube-designer-template-delete" ${selected?.scope === "personal" ? "" : "disabled"}>删除模板</button></div>
      <div class="tube-designer-template-manager-body"><div class="tube-designer-template-manager-list" role="listbox" aria-label="产品模板"><table><thead><tr><th>模板</th><th>版本</th><th>来源</th><th>状态</th></tr></thead><tbody>${rows.length ? rows.map((item) => `<tr class="${item.id === selectedId ? "selected" : ""}" data-cam-action="tube-designer-template-select" data-tube-template-id="${escapeAttr(item.id)}" role="option" aria-selected="${item.id === selectedId}"><td><strong>${escapeText(item.displayName ?? item.name ?? item.id)}</strong><small>${escapeText(item.description ?? "")}</small></td><td>${escapeText(item.version ?? item.templateVersion ?? "—")}</td><td><span class="tube-designer-template-scope ${item.scope === "builtin" ? "builtin" : "personal"}">${item.scope === "builtin" ? "内置" : "我的模板"}</span></td><td>${item.scope === "builtin" ? "只读" : "可导出 / 可删除"}</td></tr>`).join("") : `<tr><td colspan="4" class="empty">还没有可管理的模板。</td></tr>`}</tbody></table></div>${selected ? `<aside class="tube-designer-template-manager-summary"><strong>${escapeText(selected.displayName ?? selected.name ?? selected.id)}</strong><span>${selected.scope === "builtin" ? "内置模板" : "个人模板包"}</span><p>${escapeText(selected.description ?? "暂无说明")}</p><dl><dt>模板 ID</dt><dd>${escapeText(selected.id)}</dd><dt>版本</dt><dd>${escapeText(selected.version ?? selected.templateVersion ?? "—")}</dd><dt>格式</dt><dd>.itpt（ZIP）</dd></dl></aside>` : `<aside class="tube-designer-template-manager-summary empty">选择一行查看模板摘要。<br />内置模板不能删除，个人模板可导出或删除。</aside>`}</div>
      <footer class="tube-designer-preset-dialog-footer"><span>建议通过加密 .itpt 在不同项目和电脑间传递模板。</span><button class="tube-designer-secondary" data-cam-action="tube-designer-template-manager-close">关闭</button></footer>
    </section>
  </div>`;
}

export function templateManagerState(view) {
  return view.tubeDesignerTemplateManager ?? null;
}

function templateLibraryItems(view) {
  const designer = view?.scene?.tubeDesigner ?? {};
  const builtins = (Array.isArray(designer.templates) ? designer.templates : [])
    .filter((item) => item?.available !== false);
  const personal = (Array.isArray(view?.tubeDesignerUserData?.productTemplates)
    ? view.tubeDesignerUserData.productTemplates : [])
    .map((item) => {
      const descriptor = item?.descriptor && typeof item.descriptor === "object" ? item.descriptor : {};
      const templateId = String(item?.templateId ?? descriptor.id ?? item?.id ?? "");
      return {
        ...descriptor,
        ...item,
        id: templateId,
        templateId,
        version: item?.version ?? descriptor.version,
        displayName: item?.displayName ?? descriptor.displayName,
        description: item?.description ?? descriptor.description,
        extensions: item?.extensions ?? descriptor.extensions,
        groups: item?.groups ?? descriptor.groups,
        parameters: item?.parameters ?? descriptor.parameters,
        resources: item?.resources ?? descriptor.resources,
        available: item?.available ?? true,
      };
    });
  // The library and product-creation dialog consume the same one-template
  // records.  `buildCatalogEntries` only has a legacy fallback for descriptors
  // supplied by older callers; shipped resources are already one card/one ID.
  const expand = (items, fallbackScope) => buildCatalogEntries(items).map((entry) => ({
      ...entry,
      templateId: String(entry.templateId ?? entry.id ?? ""),
      id: String(entry.catalogEntryId ?? entry.id ?? ""),
      scope: String(entry.libraryScope ?? fallbackScope),
    }));
  const result = [...expand(builtins, "system"), ...expand(personal, "user")]
    .filter((item) => item.id && item.templateId);
  const seen = new Set();
  return result.filter((item) => {
    const key = `${item.scope}:${item.templateId}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

export function productTemplateLibraryState(view) {
  const state = view.tubeDesignerProductTemplateLibrary ??= {
    scope: "system", selectedId: "", search: "", parameterDrafts: {},
    preview: null, previewRequest: null, previewError: "", previewFailureKey: "", collapsed: [],
    previewApplied: false,
  };
  if (!["system", "user"].includes(state.scope)) state.scope = "system";
  state.parameterDrafts ??= {};
  state.collapsed ??= [];
  const items = templateLibraryItems(view).filter((item) => item.scope === state.scope);
  const search = String(state.search ?? "").trim().toLocaleLowerCase("zh-CN");
  const visible = items.filter((item) => !search || [productTemplateName(item), item.id, item.description]
    .some((value) => String(value ?? "").toLocaleLowerCase("zh-CN").includes(search)));
  if (!visible.some((item) => item.id === String(state.selectedId ?? ""))) {
    state.selectedId = visible[0]?.id ?? "";
  }
  return state;
}

const PRODUCT_TEMPLATE_LIBRARY_SCROLLERS = Object.freeze([
  ".tube-product-template-library-list",
  ".tube-product-template-library-editor-body",
]);

export function captureProductTemplateLibraryScrollState(context, view) {
  const mount = context?.mount;
  const target = mount?.ownerDocument?.activeElement ?? null;
  const snapshots = PRODUCT_TEMPLATE_LIBRARY_SCROLLERS.map((selector) => {
    const scroller = mount?.querySelector?.(selector);
    if (!scroller) return null;
    const anchorTarget = scroller.contains?.(target) ? target : null;
    return { selector, anchor: captureScrollAnchor(scroller, anchorTarget) };
  }).filter(Boolean);
  if (snapshots.length) view.tubeDesignerProductTemplateLibraryScrollAnchors = snapshots;
}

export function restoreProductTemplateLibraryScrollState(context, view) {
  const snapshots = view?.tubeDesignerProductTemplateLibraryScrollAnchors;
  if (!Array.isArray(snapshots) || !snapshots.length) return;
  const token = Number(view.tubeDesignerProductTemplateLibraryScrollRestorationToken ?? 0) + 1;
  view.tubeDesignerProductTemplateLibraryScrollRestorationToken = token;
  const restore = () => {
    if (view.tubeDesignerProductTemplateLibraryScrollRestorationToken !== token) return;
    const mount = context?.mount;
    for (const snapshot of snapshots) {
      const scroller = mount?.querySelector?.(snapshot.selector);
      if (!scroller || !snapshot.anchor) continue;
      restoreScrollAnchor(scroller, snapshot.anchor, {
        restoreFocus: !view.pending && snapshot.anchor.restoreFocus,
      });
    }
  };
  restore();
  queueMicrotask(() => {
    restore();
    const requestFrame = globalThis.requestAnimationFrame;
    if (typeof requestFrame !== "function") return;
    requestFrame(() => {
      restore();
      requestFrame(restore);
    });
  });
}

// The library list is made of catalog styles, while management actions still
// operate on the owning .itpt package. Expose that identity for the manager
// button instead of leaking a catalogEntryId into package-level actions.
export function productTemplateLibrarySelectedTemplateId(view) {
  const state = productTemplateLibraryState(view);
  const item = templateLibraryItems(view).find((entry) => entry.scope === state.scope && entry.id === state.selectedId);
  return String(item?.templateId ?? state.selectedId ?? "");
}

// The native scene list intentionally contains only card metadata.  Hydrate
// the selected built-in package on demand so opening the resource page does
// not parse every template's parameter schema during startup.
export async function ensureProductTemplateLibraryDescriptor(context, view) {
  if (view?.activeAreaId !== "templates" || typeof context?.sceneProxy?.invoke !== "function") return false;
  const state = productTemplateLibraryState(view);
  const item = templateLibraryItems(view).find((entry) => entry.scope === state.scope && entry.id === state.selectedId);
  if (!item || item.scope !== "system" || Array.isArray(item.parameters)) return false;
  const templateId = String(item.templateId ?? item.id ?? "").trim();
  if (!templateId) return false;
  const requests = view.tubeDesignerTemplateDescriptorRequests ??= {};
  if (!requests[templateId]) {
    requests[templateId] = context.sceneProxy.invoke(
      "TubeDesigner.GetTemplateDescriptor", { templateId }, { timeoutMs: 30000 },
    ).then((response) => response?.template ?? null);
  }
  try {
    const detail = await requests[templateId];
    if (!detail || !Array.isArray(detail.parameters)) {
      throw new Error(`模板“${item.name ?? templateId}”的参数描述未能加载。`);
    }
    const designer = view.scene?.tubeDesigner;
    if (!designer) return false;
    designer.templates = (designer.templates ?? []).map((template) =>
      String(template?.id ?? "") === templateId
        ? { ...template, ...detail, descriptorLoaded: true }
        : template);
    return true;
  } finally {
    delete requests[templateId];
  }
}

function productTemplateLibraryVisibleItems(view) {
  const state = productTemplateLibraryState(view);
  const search = String(state.search ?? "").trim().toLocaleLowerCase("zh-CN");
  return templateLibraryItems(view)
    .filter((item) => item.scope === state.scope)
    .filter((item) => !search || [productTemplateName(item), item.id, item.description]
      .some((value) => String(value ?? "").toLocaleLowerCase("zh-CN").includes(search)));
}

function productTemplateName(item) {
  const value = localizedTemplateText(item?.displayName, item?.name ?? item?.id ?? "未命名产品模板");
  return String(value).trim() || "未命名产品模板";
}

function localizedTemplateText(value, fallback = "") {
  if (typeof value === "string") return value;
  if (value && typeof value === "object") return String(value["zh-CN"] ?? value.zh ?? value["en-US"] ?? fallback);
  return fallback;
}

function templateParameterDefinitions(item) {
  return Array.isArray(item?.parameters) ? item.parameters.filter((definition) => definition?.key) : [];
}

function templateParameterGroupLabel(item, key) {
  const group = Array.isArray(item?.groups) ? item.groups.find((entry) => String(entry?.key) === String(key)) : null;
  return localizedTemplateText(group?.displayName, key || "参数");
}

function coerceTemplateParameterValue(definition, value) {
  if (!definition) return value;
  const type = String(definition.valueType ?? "string");
  if (type === "boolean") {
    if (typeof value === "boolean") return value;
    return value === true || value === "true" || value === "1" || value === 1 || value === "是";
  }
  if (type === "number" || type === "integer") {
    if (value === "" || value === null || value === undefined) return value;
    const number = Number(value);
    return Number.isFinite(number) ? number : value;
  }
  if (type === "enum" || Array.isArray(definition.choices)) {
    const choice = (definition.choices ?? []).find((entry) => String(entry?.value) === String(value));
    return choice ? choice.value : value;
  }
  return value;
}

function templateParameterRules(item) {
  return Array.isArray(item?.extensions?.parameterRules)
    ? item.extensions.parameterRules.filter((rule) => rule && typeof rule === "object")
    : [];
}

function templateParameterRuleMatches(values, when) {
  if (!when || typeof when !== "object" || Array.isArray(when)) return true;
  return Object.entries(when).every(([key, expected]) => values[key] === expected);
}

function normalizeTemplateParameterValues(item, values) {
  const result = { ...(values ?? {}) };
  for (const rule of templateParameterRules(item)) {
    if (!templateParameterRuleMatches(result, rule.when)) continue;
    const assignments = rule.set;
    if (!assignments || typeof assignments !== "object" || Array.isArray(assignments)) continue;
    for (const [key, value] of Object.entries(assignments)) {
      const definition = templateParameterDefinitions(item).find((entry) => String(entry.key) === key);
      result[key] = coerceTemplateParameterValue(definition, value);
    }
  }
  return result;
}

function templateParameterValues(view, item) {
  const state = productTemplateLibraryState(view);
  const key = String(item?.id ?? "");
  const defaults = {
    ...Object.fromEntries(templateParameterDefinitions(item).map((definition) => [
      String(definition.key), definition.defaultValue,
    ])),
    ...(item?.catalogParameters && typeof item.catalogParameters === "object" && !Array.isArray(item.catalogParameters)
      ? item.catalogParameters : {}),
  };
  const raw = { ...defaults, ...(state.parameterDrafts?.[key] ?? {}) };
  const values = Object.fromEntries(templateParameterDefinitions(item).map((definition) => [
    String(definition.key), coerceTemplateParameterValue(definition, raw[definition.key]),
  ]));
  return normalizeTemplateParameterValues(item, { ...raw, ...values });
}

function templateParameterVisible(definition, values) {
  const condition = definition?.visibleWhen;
  if (!condition) return true;
  if (condition.op === "eq") return values[condition.parameter] === condition.value;
  if (condition.op === "ne") return values[condition.parameter] !== condition.value;
  if (condition.op === "all") return (condition.conditions ?? []).every((item) => templateParameterVisible({ visibleWhen: item }, values));
  if (condition.op === "any") return (condition.conditions ?? []).some((item) => templateParameterVisible({ visibleWhen: item }, values));
  return true;
}

function templateParameterInput(view, item, definition) {
  const values = templateParameterValues(view, item);
  const key = String(definition.key);
  const value = values[key] ?? "";
  const type = String(definition.valueType ?? "string");
  const constraints = definition.constraints ?? {};
  const common = `data-cam-change-action="tube-designer-product-template-library-parameter-change" data-tube-template-library-id="${escapeAttr(item.id)}" data-tube-template-library-parameter="${escapeAttr(key)}" ${view?.pending ? "disabled" : ""}`;
  const label = localizedTemplateText(definition.displayName, key);
  if (type === "boolean") return `<label class="tube-product-template-library-check"><input type="checkbox" ${value ? "checked" : ""} ${common} /><span>${escapeText(label)}</span></label>`;
  if (type === "enum" || Array.isArray(definition.choices)) {
    const choices = Array.isArray(definition.choices) ? definition.choices : [];
    return `<label><span>${escapeText(label)}</span><select ${common}>${choices.map((choice) => `<option value="${escapeAttr(choice.value)}" ${String(choice.value) === String(value) ? "selected" : ""}>${escapeText(localizedTemplateText(choice.displayName, choice.value))}</option>`).join("")}</select></label>`;
  }
  const inputType = type === "number" || type === "integer" ? "number" : "text";
  return `<label><span>${escapeText(label)}</span><input type="${inputType}" value="${escapeAttr(value)}" ${constraints.minimum !== undefined ? `min="${escapeAttr(constraints.minimum)}"` : ""} ${constraints.maximum !== undefined ? `max="${escapeAttr(constraints.maximum)}"` : ""} ${constraints.step !== undefined ? `step="${escapeAttr(constraints.step)}"` : ""} ${common} /></label>`;
}

function templateIllustration(item) {
  const name = productTemplateName(item);
  const asset = getTemplateVisualAsset(item, "schematic") || getTemplateVisualAsset(item, "icon");
  if (asset) {
    return `<img class="tube-product-template-library-schematic" src="${escapeAttr(asset)}" alt="${escapeAttr(name)}" />`;
  }
  // A package without artwork still gets a harmless generic card. The
  // template remains fully usable and adding artwork later requires no MJS
  // change: place resource/icon.svg or resource/schematic.svg in its package.
  return `<svg class="tube-product-template-library-schematic" viewBox="0 0 56 48" aria-label="${escapeAttr(name)}"><rect x="8" y="10" width="40" height="28" rx="2" fill="none" stroke="currentColor" stroke-width="2.5"/><path d="M14 18h28M14 25h28M14 32h28" fill="none" stroke="currentColor" stroke-width="1.2"/></svg>`;
}

export function renderProductTemplateLibraryLeftPane(_context, view) {
  const state = productTemplateLibraryState(view);
  const all = templateLibraryItems(view);
  const items = productTemplateLibraryVisibleItems(view);
  const scopeCount = (scope) => all.filter((item) => item.scope === scope).length;
  const groupedItems = new Map();
  for (const item of items) {
    const path = Array.isArray(item?.catalogPath) ? item.catalogPath : [];
    const group = String(path.length > 1 ? path.at(-2) : "其他产品").trim() || "其他产品";
    if (!groupedItems.has(group)) groupedItems.set(group, []);
    groupedItems.get(group).push(item);
  }
  const renderGroup = ([group, entries]) => {
    const groupKey = `${state.scope}:${group}`;
    const expanded = !state.collapsed.includes(groupKey);
    return `<section class="tube-tool-library-group tube-product-template-library-group">
      <button type="button" class="tube-tool-library-group-heading tube-product-template-library-group-heading" data-cam-action="tube-designer-product-template-library-toggle-category" data-tube-template-library-group="${escapeAttr(groupKey)}" aria-expanded="${expanded}"><span>${expanded ? "▾" : "▸"} ${escapeText(group)}</span><small>${state.scope === "system" ? "系统内置" : "我的"} · ${entries.length}</small></button>
      <div class="tube-tool-library-group-items tube-product-template-library-group-items" ${expanded ? "" : "hidden"}>${entries.map((item) => `<button type="button" class="tube-tool-library-card tube-product-template-library-card ${item.id === state.selectedId ? "selected" : ""}" data-cam-action="tube-designer-product-template-library-select" data-tube-template-library-id="${escapeAttr(item.id)}" role="option" aria-selected="${item.id === state.selectedId}" ${view?.pending ? "disabled" : ""}>
        <span class="tube-tool-library-card-art tube-product-template-library-card-art" aria-hidden="true">${templateIllustration(item)}</span><span class="tube-tool-library-card-copy"><strong>${escapeText(productTemplateName(item))}</strong><small>${item.scope === "system" ? "系统内置" : "我的"}</small></span>
      </button>`).join("")}</div>
    </section>`;
  };
  return `<div class="tube-designer-panel tube-tool-library-panel tube-product-template-library-panel">
    <div class="tube-designer-heading tube-tool-library-heading tube-product-template-library-heading"><div><strong>产品款式</strong><span>${all.length} 款 · 当前显示 ${items.length} 款（来自产品模板）</span></div></div>
    <div class="tube-component-library-filters tube-tool-library-filters tube-product-template-library-filters">
      <input type="search" aria-label="搜索产品模板" placeholder="搜索名称、说明" value="${escapeAttr(state.search)}" data-cam-change-action="tube-designer-product-template-library-search" />
      <div class="tube-tool-library-tab-row tube-product-template-library-tab-row" role="tablist" aria-label="产品模板来源">
        <button type="button" role="tab" aria-selected="${state.scope === "system"}" class="${state.scope === "system" ? "selected" : ""}" data-cam-action="tube-designer-product-template-library-scope" data-tube-template-library-scope="system">系统内置<small>${scopeCount("system")}</small></button>
        <button type="button" role="tab" aria-selected="${state.scope === "user"}" class="${state.scope === "user" ? "selected" : ""}" data-cam-action="tube-designer-product-template-library-scope" data-tube-template-library-scope="user">我的<small>${scopeCount("user")}</small></button>
      </div>
    </div>
    <div class="tube-tool-library-list tube-product-template-library-list" role="listbox" aria-label="产品模板列表">
      ${items.length ? [...groupedItems.entries()].map(renderGroup).join("") : `<div class="tube-designer-empty-state"><strong>还没有产品模板</strong><span>${state.scope === "system" ? "请检查内置产品模板资源是否完整。" : "可通过上方“新增”或“导入 itpt”创建。"}</span></div>`}
    </div>
  </div>`;
}

export function renderProductTemplateLibraryRightPane(_context, view) {
  const state = productTemplateLibraryState(view);
  const item = templateLibraryItems(view).find((entry) => entry.scope === state.scope && entry.id === state.selectedId);
  if (!item) return `<div class="tube-designer-panel"><div class="tube-designer-heading"><strong>产品模板</strong><span>尚未选择模板</span></div><div class="tube-designer-empty">从左侧选择一个产品模板，查看摘要或使用上方管理操作。</div></div>`;
  const definitions = templateParameterDefinitions(item);
  const descriptorRequest = view?.tubeDesignerTemplateDescriptorRequests?.[String(item.templateId ?? item.id ?? "")];
  const values = templateParameterValues(view, item);
  const visible = definitions.filter((definition) => templateParameterVisible(definition, values));
  const groups = new Map();
  for (const definition of visible) {
    const group = String(definition.group ?? "参数");
    if (!groups.has(group)) groups.set(group, []);
    groups.get(group).push(definition);
  }
  const emptyContent = descriptorRequest
    ? `<div class="tube-designer-empty">正在读取模板参数…</div>`
    : `<div class="tube-designer-empty">此模板没有可编辑参数。</div>`;
  return `<div class="tube-designer-panel tube-product-template-library-editor"><div class="tube-designer-heading"><div><strong>${escapeText(productTemplateName(item))}</strong><span>${item.scope === "system" ? "系统内置模板" : "我的模板"} · 参数预览</span></div></div><div class="tube-product-template-library-editor-body"><dl class="tube-product-template-library-meta"><dt>模板 ID</dt><dd>${escapeText(item.id)}</dd><dt>版本</dt><dd>${escapeText(item.version ?? item.templateVersion ?? "—")}</dd><dt>格式</dt><dd>.itpt</dd></dl><p>${escapeText(item.description ?? "暂无模板说明")}</p>${definitions.length ? `<section class="tube-product-template-library-parameters"><header><strong>预览参数</strong><span>修改后只更新中央场景，不改模板包</span></header>${[...groups.entries()].map(([group, entries]) => `<fieldset><legend>${escapeText(templateParameterGroupLabel(item, group))}</legend><div>${entries.map((definition) => templateParameterInput(view, item, definition)).join("")}</div></fieldset>`).join("")}</section>` : emptyContent}<button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-template-manager-open">打开管理</button></div></div>`;
}

export function renderProductTemplateLibraryViewportOverlay(context, view) {
  const state = productTemplateLibraryState(view);
  const item = templateLibraryItems(view).find((entry) => entry.scope === state.scope && entry.id === state.selectedId);
  if (item) ensureProductTemplateLibraryPreview(context, view, item);
  return `<div class="tube-tool-library-hud"><strong>${escapeText(item ? productTemplateName(item) : "产品模板")}</strong><span>${item ? (item.scope === "system" ? "系统内置模板" : "我的模板") : "选择左侧模板查看摘要"}</span><small>${item ? (state.previewRequest ? "正在生成模板预览…" : "拖动旋转 · 滚轮缩放 · 支持透视 / 正交") : "选择左侧模板查看摘要"}</small>${item && state.previewRequest ? '<div class="tube-tool-library-preview-progress" role="progressbar" aria-label="正在生成产品模板预览"><i></i></div>' : ""}${state.previewError ? `<p role="alert">${escapeText(state.previewError)}</p><button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-product-template-library-retry-preview">重新预览</button>` : ""}</div>`;
}

function productTemplatePreviewKey(view, item) {
  return JSON.stringify([item?.scope ?? "", item?.templateId ?? item?.id ?? "", item?.presetId ?? "", item?.version ?? "", templateParameterValues(view, item)]);
}

function centeredTemplateMatrix(bounds) {
  const min = Array.isArray(bounds?.min) ? bounds.min.map(Number) : [0, 0, 0];
  const max = Array.isArray(bounds?.max) ? bounds.max.map(Number) : [0, 0, 0];
  return [1, 0, 0, -(min[0] + max[0]) / 2, 0, 1, 0, -(min[1] + max[1]) / 2, 0, 0, 1, -(min[2] + max[2]) / 2, 0, 0, 0, 1];
}

function productTemplatePreviewRows(response = {}) {
  const items = Array.isArray(response.items) ? response.items.filter((item) => item?.geometry?.url) : [];
  if (!items.length) return [];
  const mins = [Infinity, Infinity, Infinity];
  const maxs = [-Infinity, -Infinity, -Infinity];
  for (const item of items) {
    const bounds = item.bounds ?? {};
    const min = Array.isArray(bounds.min) ? bounds.min.map(Number) : [0, 0, 0];
    const max = Array.isArray(bounds.max) ? bounds.max.map(Number) : [0, 0, 0];
    for (let index = 0; index < 3; index++) { mins[index] = Math.min(mins[index], min[index]); maxs[index] = Math.max(maxs[index], max[index]); }
  }
  const matrix = centeredTemplateMatrix({ min: mins, max: maxs });
  return items.map((item) => ({ entityId: String(item.entityId ?? `template-preview:${item.key ?? "item"}`), data: { geometry: item.geometry, material: item.material ?? response.material, geometryKind: 1, renderClass: 1, visible: true, selectable: false, localToWorldMatrix: matrix } }));
}

async function applyProductTemplateLibraryPreview(context, view, item, response, key, request) {
  const state = productTemplateLibraryState(view);
  if (view.activeAreaId !== "templates" || productTemplatePreviewKey(view, item) !== key || state.previewRequest !== request) return;
  const rows = productTemplatePreviewRows(response);
  if (!rows.length) throw new Error("产品模板没有返回可显示的几何体。");
  if (!view.viewport?.applyViewSnapshot) throw new Error("三维视口尚未准备好。");
  const camera = state.preview?.key === key ? view.viewport.getCameraState?.() : null;
  const resources = context.sceneProxy?.resources ?? view.sceneProxy?.resources ?? context.projectProxy?.resources;
  const receipt = await view.viewport.applyViewSnapshot({ revision: `product-template:${key}`, rows }, resources);
  if (!receipt?.applied || receipt.missingGeometryEntityIds?.length) throw new Error("产品模板预览几何未完整进入视口。");
  if (view.activeAreaId !== "templates" || productTemplatePreviewKey(view, item) !== key || state.previewRequest !== request) return;
  if (camera) view.viewport.setCameraState?.(camera);
  else { view.viewport.setStandardView?.("iso"); view.viewport.fitViewToViewport?.(1.2); }
  state.preview = { key, response };
  state.previewApplied = true;
  // The resource page has no persisted View snapshot.  Keep the generated
  // product entities alive when the HUD/right pane causes a workbench repaint.
  view.preserveCustomViewportEntities = true;
}

function ensureProductTemplateLibraryPreview(context, view, item) {
  if (view.activeAreaId !== "templates" || !item || typeof context?.sceneProxy?.invoke !== "function") return;
  const state = productTemplateLibraryState(view);
  const key = productTemplatePreviewKey(view, item);
  if (state.preview?.key === key || state.previewRequest?.key === key || state.previewFailureKey === key) return;
  state.previewError = "";
  const request = { key, promise: null };
  request.promise = Promise.resolve().then(() => {
    const payload = { templateId: item.templateId ?? item.id, parameters: templateParameterValues(view, item) };
    const version = item.version ?? item.templateVersion;
    if (version) payload.templateVersion = version;
    return context.sceneProxy.invoke("TubeDesigner.GenerateProductTemplatePreview", payload, { timeoutMs: 120000 });
  }).then((response) => applyProductTemplateLibraryPreview(context, view, item, response, key, request)).then(() => {
    if (state.previewRequest === request) { state.previewRequest = null; state.previewFailureKey = ""; }
    if (view.activeAreaId === "templates") view.tubeDesignerProductTemplateLibraryRenderProject?.();
  }).catch((error) => {
    if (state.previewRequest !== request) return;
    state.previewRequest = null; state.previewFailureKey = key; state.previewError = `产品模板预览失败：${error?.message ?? error}`;
    if (view.activeAreaId === "templates") view.tubeDesignerProductTemplateLibraryRenderProject?.();
  });
  state.previewRequest = request;
}

export async function handleProductTemplateLibraryAction(context, view, action, target, ops) {
  const state = productTemplateLibraryState(view);
  if (view.pending) return { handled: true };
  if (action === "tube-designer-product-template-library-toggle-category") {
    const group = String(target?.dataset?.tubeTemplateLibraryGroup ?? "");
    if (group) {
      state.collapsed = state.collapsed.includes(group)
        ? state.collapsed.filter((value) => value !== group)
        : [...state.collapsed, group];
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-product-template-library-scope") {
    const scope = String(target?.dataset?.tubeTemplateLibraryScope ?? "");
    if (["system", "user"].includes(scope)) {
      const hadPreview = !!state.preview;
      state.scope = scope; state.selectedId = ""; state.preview = null; state.previewError = ""; state.previewFailureKey = "";
      state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-product-template-library-select") {
    const id = String(target?.dataset?.tubeTemplateLibraryId ?? "").trim();
    if (id) {
      const hadPreview = !!state.preview;
      state.selectedId = id; state.preview = null; state.previewError = ""; state.previewFailureKey = "";
      state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-product-template-library-search") {
    const hadPreview = !!state.preview;
    state.search = String(target?.value ?? ""); state.selectedId = ""; state.preview = null;
    state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-product-template-library-parameter-change") {
    const id = String(target?.dataset?.tubeTemplateLibraryId ?? "");
    const parameter = String(target?.dataset?.tubeTemplateLibraryParameter ?? "");
    const item = templateLibraryItems(view).find((entry) => entry.scope === state.scope && entry.id === id);
    const definition = templateParameterDefinitions(item).find((entry) => String(entry.key) === parameter);
    if (item && definition) {
      const value = definition.valueType === "boolean" ? !!target.checked
        : coerceTemplateParameterValue(definition, target.value ?? "");
      const nextValues = normalizeTemplateParameterValues(item, {
        ...templateParameterValues(view, item),
        [parameter]: value,
      });
      state.parameterDrafts[id] = { ...(state.parameterDrafts[id] ?? {}), ...nextValues };
      const hadPreview = !!state.preview;
      state.preview = null; state.previewError = ""; state.previewFailureKey = "";
      state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-product-template-library-retry-preview") {
    const hadPreview = !!state.preview;
    state.preview = null; state.previewError = ""; state.previewFailureKey = "";
    state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
    ops.renderProject(context, view);
    return { handled: true };
  }
  return { handled: false };
}
