import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";

const SCOPES = [
  ["system", "系统内置"],
  ["template", "模板自带"],
  ["user", "我的"],
];
const TYPES = [
  ["all", "全部"],
  ["programmatic", "程式"],
  ["fixed", "定式"],
];
const CATEGORIES = [
  ["all", "全部类别"],
  ["hole", "孔型"],
  ["slot", "槽口"],
  ["branch", "支管"],
  ["end", "端面"],
];

export function toolLibraryState(view) {
  const state = view.tubeDesignerToolLibrary ??= {
    scope: "system", type: "all", category: "all", search: "", selectedKey: "",
    catalogueStatus: "idle", error: "",
  };
  if (!SCOPES.some(([scope]) => scope === state.scope)) state.scope = "system";
  if (!TYPES.some(([type]) => type === state.type)) state.type = "all";
  if (!CATEGORIES.some(([category]) => category === state.category)) state.category = "all";
  return state;
}

function scopeOf(tool, fallback = "system") {
  const value = String(tool?.libraryScope ?? tool?.toolScope ?? tool?.scope ?? fallback);
  return SCOPES.some(([scope]) => scope === value) ? value : fallback;
}

function typeOf(tool) {
  return tool?.kind === "fixed" ? "fixed" : "programmatic";
}

function categoryOf(tool) {
  const raw = String(tool?.category ?? "").toLocaleLowerCase("zh-CN");
  if (raw.includes("支管") || raw.includes("branch") || tool?.id === "branch-profile") return "branch";
  if (raw.includes("端") || raw.includes("end") || tool?.target === "end") return "end";
  if (raw.includes("槽") || raw.includes("slot") || raw.includes("notch") || tool?.id === "v-notch") return "slot";
  return "hole";
}

function templateIdOf(tool) {
  return String(tool?.templateId ?? tool?.ownerTemplateId ?? "");
}

function keyOf(tool, scope = scopeOf(tool)) {
  return `${scope}:${templateIdOf(tool)}:${String(tool?.id ?? "")}`;
}

function toolName(tool) {
  return String(tool?.displayName ?? tool?.name ?? tool?.toolLabel ?? tool?.id ?? "未命名模具").trim() || "未命名模具";
}

function sources(view) {
  const wizardTools = view?.tubeDesignerPunchWizard?.tools;
  return [
    ...(Array.isArray(view?.tubeDesignerSystemPunchTools) ? view.tubeDesignerSystemPunchTools : []),
    ...(Array.isArray(view?.tubeDesignerTemplatePunchTools) ? view.tubeDesignerTemplatePunchTools : []),
    ...(Array.isArray(view?.tubeDesignerUserData?.punchTools) ? view.tubeDesignerUserData.punchTools : []),
    ...(Array.isArray(wizardTools) ? wizardTools.map((tool) => ({ ...tool, libraryScope: tool.libraryScope ?? "system" })) : []),
  ];
}

export function libraryTools(view) {
  const result = new Map();
  for (const source of sources(view)) {
    if (!source?.id) continue;
    const scope = scopeOf(source);
    const tool = { ...source, libraryScope: scope, kind: typeOf(source) };
    const key = keyOf(tool, scope);
    if (!result.has(key)) result.set(key, { ...tool, libraryKey: key });
  }
  return [...result.values()];
}

export function visibleLibraryTools(view) {
  const state = toolLibraryState(view);
  const source = libraryTools(view).filter((tool) => scopeOf(tool) === state.scope);
  const search = String(state.search ?? "").trim().toLocaleLowerCase("zh-CN");
  return source.filter((tool) => {
    if (state.type !== "all" && typeOf(tool) !== state.type) return false;
    if (state.category !== "all" && categoryOf(tool) !== state.category) return false;
    if (!search) return true;
    return [toolName(tool), tool.id, tool.category, tool.templateName, tool.description]
      .some((value) => String(value ?? "").toLocaleLowerCase("zh-CN").includes(search));
  });
}

function ensureSelection(view, tools) {
  const state = toolLibraryState(view);
  if (tools.some((tool) => tool.libraryKey === state.selectedKey)) return state.selectedKey;
  state.selectedKey = tools[0]?.libraryKey ?? "";
  return state.selectedKey;
}

function sourceLabel(tool) {
  const scope = scopeOf(tool);
  if (scope === "template") return `模板自带 · ${tool.templateName ?? tool.templateId ?? "当前模板"}`;
  if (scope === "user") return "我的模具";
  return "系统内置模具";
}

function typeLabel(tool) {
  return typeOf(tool) === "fixed" ? "定式模具" : "程式模具";
}

function categoryLabel(tool) {
  return CATEGORIES.find(([category]) => category === categoryOf(tool))?.[1] ?? "孔型";
}

function renderToolIllustration(tool) {
  const id = String(tool?.id ?? "");
  const category = categoryOf(tool);
  if (id === "circle") return `<svg viewBox="0 0 48 48" aria-hidden="true"><circle cx="24" cy="24" r="13" /></svg>`;
  if (id === "ellipse") return `<svg viewBox="0 0 48 48" aria-hidden="true"><ellipse cx="24" cy="24" rx="16" ry="10" /></svg>`;
  if (id === "slot") return `<svg viewBox="0 0 48 48" aria-hidden="true"><rect x="8" y="17" width="32" height="14" rx="7" /></svg>`;
  if (id === "rectangle" || id === "square") return `<svg viewBox="0 0 48 48" aria-hidden="true"><rect x="10" y="10" width="28" height="28" rx="${id === "rectangle" ? "5" : "2"}" /></svg>`;
  if (id === "hexagon") return `<svg viewBox="0 0 48 48" aria-hidden="true"><polygon points="13,10 35,10 42,24 35,38 13,38 6,24" /></svg>`;
  if (id === "triangle") return `<svg viewBox="0 0 48 48" aria-hidden="true"><polygon points="24,7 41,38 7,38" /></svg>`;
  if (id === "single-d") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M8 34 H24 A10 10 0 0 0 24 14 H8 Z" /></svg>`;
  if (id === "double-d") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M14 14 H34 A10 10 0 0 1 34 34 H14 A10 10 0 0 1 14 14 Z" /></svg>`;
  if (id === "diamond-12") return `<svg viewBox="0 0 48 48" aria-hidden="true"><polygon points="8,24 24,12 40,24 24,36" /></svg>`;
  if (id === "v-notch") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M8 12 L24 36 L40 12" /></svg>`;
  if (category === "branch") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M24 38 V14 M24 14 C24 8 39 8 39 14 V38" /></svg>`;
  if (category === "end") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M10 10 H38 V38 H10 Z M10 24 H38" /></svg>`;
  return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M10 24 H38 M24 10 V38" /></svg>`;
}

function emptyText(state) {
  if (state.search) return ["没有匹配的模具", "请调整搜索内容。"];
  if (state.type === "fixed") return ["还没有定式模具", "定式模具由固定截面（可来自 DXF）生成标准拉伸体。"];
  if (state.type === "programmatic") return ["还没有程式模具", "程式模具由参数程式生成标准拉伸体。"];
  if (state.category !== "all") {
    const label = CATEGORIES.find(([category]) => category === state.category)?.[1] ?? "该类别";
    return [`还没有${label}模具`, `系统、模板或我的模具中尚未提供${label}定义。`];
  }
  return {
    system: ["没有可用的系统模具", "请检查内置模具资源是否完整。"],
    template: ["还没有模板自带模具", "模板可在自己的资源声明中提供专用模具。"],
    user: ["还没有我的模具", "可从模具库导入或保存自定义模具。"],
  }[state.scope];
}

export function renderToolLibraryLeftPane(_context, view) {
  const state = toolLibraryState(view);
  const all = libraryTools(view);
  const tools = visibleLibraryTools(view);
  const selected = ensureSelection(view, tools);
  const sourceTools = all.filter((tool) => scopeOf(tool) === state.scope);
  const [emptyTitle, emptyNote] = emptyText(state);
  return `<div class="tube-designer-panel tube-tool-library-panel">
    <div class="tube-designer-heading tube-tool-library-heading"><div><strong>模具库</strong><span>${all.length} 个模具 · 当前显示 ${tools.length} 个</span></div></div>
    <div class="tube-component-library-filters tube-tool-library-filters">
      <input type="search" aria-label="搜索模具" placeholder="搜索名称、类别、所属模板" value="${escapeAttr(state.search)}" data-cam-change-action="tube-designer-tool-library-search" />
      <div class="tube-tool-library-tab-row" role="tablist" aria-label="模具类别">${CATEGORIES.map(([category, label]) => `<button type="button" role="tab" aria-selected="${category === state.category}" class="${category === state.category ? "selected" : ""}" data-cam-action="tube-designer-tool-library-category" data-tube-tool-library-category="${category}" ${view?.pending ? "disabled" : ""}>${label}<small>${category === "all" ? sourceTools.length : sourceTools.filter((tool) => categoryOf(tool) === category).length}</small></button>`).join("")}</div>
      <div class="tube-tool-library-tab-row" role="tablist" aria-label="模具来源">${SCOPES.map(([scope, label]) => `<button type="button" role="tab" aria-selected="${scope === state.scope}" class="${scope === state.scope ? "selected" : ""}" data-cam-action="tube-designer-tool-library-scope" data-tube-tool-library-scope="${scope}" ${view?.pending ? "disabled" : ""}>${label}<small>${all.filter((tool) => scopeOf(tool) === scope).length}</small></button>`).join("")}</div>
      <div class="tube-tool-library-tab-row" role="tablist" aria-label="模具类型">${TYPES.map(([type, label]) => `<button type="button" role="tab" aria-selected="${type === state.type}" class="${type === state.type ? "selected" : ""}" data-cam-action="tube-designer-tool-library-type" data-tube-tool-library-type="${type}" ${view?.pending ? "disabled" : ""}>${label}<small>${type === "all" ? sourceTools.length : sourceTools.filter((tool) => typeOf(tool) === type).length}</small></button>`).join("")}</div>
    </div>
    <div class="tube-tool-library-list" role="tabpanel">
      ${tools.length ? tools.map((tool) => `<button type="button" class="tube-tool-library-card ${tool.libraryKey === selected ? "selected" : ""}" data-cam-action="tube-designer-tool-library-select" data-tube-tool-library-key="${escapeAttr(tool.libraryKey)}" aria-label="${escapeAttr(`${toolName(tool)}，${categoryLabel(tool)}，${typeLabel(tool)}，${sourceLabel(tool)}`)}" ${view?.pending ? "disabled" : ""}>
        <span class="tube-tool-library-card-icon ${scopeOf(tool) === "system" ? "is-system" : ""}" aria-hidden="true">${scopeOf(tool) === "template" ? "T" : scopeOf(tool) === "system" ? "S" : "P"}</span><span class="tube-tool-library-card-art">${renderToolIllustration(tool)}</span><span class="tube-tool-library-card-copy"><strong>${escapeText(toolName(tool))}</strong><small>${escapeText(categoryLabel(tool))}</small></span>
      </button>`).join("") : `<div class="tube-tool-library-empty"><strong>${escapeText(emptyTitle)}</strong><span>${escapeText(emptyNote)}</span></div>`}
    </div>
  </div>`;
}

export function renderToolLibraryRightPane(_context, view) {
  const tools = visibleLibraryTools(view);
  const selectedKey = ensureSelection(view, tools);
  const tool = tools.find((item) => item.libraryKey === selectedKey);
  if (!tool) return `<div class="tube-designer-panel tube-tool-library-editor-empty"><div class="tube-designer-heading"><strong>模具属性</strong><span>尚未选择模具</span></div><div class="tube-designer-empty">从左侧选择模具，查看其来源、类型和标准拉伸体规则。</div></div>`;
  const parameters = Array.isArray(tool.parameters) ? tool.parameters : [];
  const details = parameters.map((parameter) => String(parameter?.displayName ?? parameter?.key ?? "参数")).filter(Boolean);
  return `<div class="tube-designer-panel tube-tool-library-editor"><div class="tube-designer-heading"><div><strong>${escapeText(toolName(tool))}</strong><span>${escapeText(typeLabel(tool))} · ${escapeText(sourceLabel(tool))}</span></div></div>
    <div class="tube-tool-library-editor-body">
      <div class="tube-tool-library-summary"><strong>${escapeText(typeLabel(tool))}</strong><span>${escapeText(categoryLabel(tool))} · ${escapeText(sourceLabel(tool))}</span><small>所有模具统一输出标准拉伸体，参与冲孔、三维绘制和产品拆单。</small></div>
      <dl class="tube-tool-library-meta"><dt>模具 ID</dt><dd>${escapeText(tool.id)}</dd><dt>版本</dt><dd>${escapeText(tool.version ?? "—")}</dd><dt>适用目标</dt><dd>${escapeText(tool.target ?? "零件")}</dd><dt>类别</dt><dd>${escapeText(tool.category ?? categoryLabel(tool))}</dd></dl>
      ${typeOf(tool) === "fixed" ? `<p class="tube-tool-library-note">定式模具不声明可变形状参数。它只保存固定闭合截面（可由 DXF 导入），使用统一标准拉伸规则生成模具体；位置、姿态、阵列仍由业务场景设置。</p>` : `<p class="tube-tool-library-note">程式模具由参数程式生成闭合截面和标准拉伸体。参数定义来自模具本身，三个业务入口不再各自复制一套字段。</p>`}
      ${details.length ? `<section class="tube-tool-library-parameters"><header><strong>形状参数</strong><span>${details.length} 项</span></header><div>${details.map((item) => `<span>${escapeText(item)}</span>`).join("")}</div></section>` : ""}
      <p class="tube-tool-library-provenance">模具库只管理模具定义；冲孔、三维绘制和产品拆单各自保存自己的模具引用、定位、姿态和阵列记录。</p>
    </div>
  </div>`;
}

export function renderToolLibraryViewportOverlay(_context, view) {
  const tools = visibleLibraryTools(view);
  const key = ensureSelection(view, tools);
  const tool = tools.find((item) => item.libraryKey === key);
  return `<div class="tube-tool-library-hud"><strong>${escapeText(tool ? toolName(tool) : "模具库")}</strong><span>${escapeText(tool ? `${typeLabel(tool)} · ${sourceLabel(tool)}` : "选择左侧模具查看定义")}</span><small>模具定义只输出标准拉伸体；三处业务分别保存使用记录。</small></div>`;
}

export function ensureToolLibraryCatalogue(context, view, ops) {
  const state = toolLibraryState(view);
  if (state.catalogueStatus !== "idle" || typeof context?.sceneProxy?.invoke !== "function") return;
  state.catalogueStatus = "loading";
  void context.sceneProxy.invoke("TubeDesigner.GetPunchTools", {}, { timeoutMs: 30000 }).then((response) => {
    const tools = Array.isArray(response?.tools) ? response.tools : [];
    view.tubeDesignerSystemPunchTools = tools
      .filter((tool) => scopeOf(tool) === "system")
      .map((tool) => ({ ...tool, libraryScope: "system" }));
    view.tubeDesignerTemplatePunchTools = tools
      .filter((tool) => scopeOf(tool) === "template")
      .map((tool) => ({ ...tool, libraryScope: "template" }));
    state.catalogueStatus = "ready";
    state.error = "";
    if (view.activeAreaId === "tools") ops?.renderProject?.(context, view);
  }).catch((error) => {
    state.catalogueStatus = "error";
    state.error = error?.message ?? String(error);
    if (view.activeAreaId === "tools") ops?.renderProject?.(context, view);
  });
}

export async function handleToolLibraryAction(context, view, action, target, ops) {
  const state = toolLibraryState(view);
  if (action === "tube-designer-tool-library-scope") {
    if (!view.pending) {
      const scope = String(target?.dataset?.tubeToolLibraryScope ?? "");
      if (SCOPES.some(([value]) => value === scope)) { state.scope = scope; state.selectedKey = ""; ops.renderProject(context, view); }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-type") {
    if (!view.pending) {
      const type = String(target?.dataset?.tubeToolLibraryType ?? "");
      if (TYPES.some(([value]) => value === type)) { state.type = type; state.selectedKey = ""; ops.renderProject(context, view); }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-category") {
    if (!view.pending) {
      const category = String(target?.dataset?.tubeToolLibraryCategory ?? "");
      if (CATEGORIES.some(([value]) => value === category)) { state.category = category; state.selectedKey = ""; ops.renderProject(context, view); }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-search") {
    if (!view.pending) { state.search = String(target?.value ?? ""); state.selectedKey = ""; ops.renderProject(context, view); }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-select") {
    if (!view.pending) { state.selectedKey = String(target?.dataset?.tubeToolLibraryKey ?? ""); ops.renderProject(context, view); }
    return { handled: true };
  }
  return { handled: false };
}

export async function handleToolLibraryRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "tools.refresh") return false;
  const state = toolLibraryState(view);
  state.catalogueStatus = "idle";
  ensureToolLibraryCatalogue(context, view, ops);
  return true;
}
