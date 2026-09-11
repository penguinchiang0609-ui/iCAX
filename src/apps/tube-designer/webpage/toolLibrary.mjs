import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";

import { buildPunchPreviewRows } from "./punchEditor.mjs";
import { renderProfileSvg } from "./profileSvg.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import {
  isParametricProfile,
  libraryProfiles,
  profileName,
  profileRef,
  profileSelectionKey,
  profileSnapshot,
  profileSpecification,
} from "./profileLibrary.mjs";

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

// Resource descriptors may carry localized text as an object.  Never pass
// that object directly to a DOM string helper; doing so renders
// "[object Object]" in parameter labels.
function localizedText(value, fallback = "") {
  if (value && typeof value === "object") {
    return String(value["zh-CN"] ?? value["en-US"] ?? Object.values(value)[0] ?? fallback);
  }
  return String(value ?? fallback);
}

export function toolLibraryState(view) {
  const state = view.tubeDesignerToolLibrary ??= {
    scope: "system", type: "all", category: "all", search: "", selectedKey: "",
    collapsed: [], catalogueStatus: "idle", error: "", parameterDrafts: {},
    profileKey: "", profileDrafts: {}, branchProfileKey: "", branchProfileDrafts: {}, previewLength: 500,
    showProfileDiagram: false, showBranchProfileDiagram: false, showToolDiagram: false,
  };
  if (!SCOPES.some(([scope]) => scope === state.scope)) state.scope = "system";
  if (!TYPES.some(([type]) => type === state.type)) state.type = "all";
  if (!CATEGORIES.some(([category]) => category === state.category)) state.category = "all";
  state.collapsed ??= [];
  state.parameterDrafts ??= {};
  state.profileDrafts ??= {};
  state.branchProfileDrafts ??= {};
  if (!Number.isFinite(Number(state.previewLength))) state.previewLength = 500;
  state.previewLength = Math.min(100000, Math.max(1, Number(state.previewLength)));
  if (!["idle", "loading", "ready", "error"].includes(state.catalogueStatus)) state.catalogueStatus = "idle";
  // A view can survive a cancelled navigation without its in-flight promise;
  // do not leave that view permanently stuck in loading.
  if (state.catalogueStatus === "loading" && !state.cataloguePromise) state.catalogueStatus = "idle";
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
  // 腰形孔是贯穿孔型，即使历史目录把它标成“槽口”，在资源库中也
  // 应与圆孔、方孔等孔型归在一起；真正的槽口是 V 槽、缺口类切削。
  if (tool?.id === "slot" || raw.includes("腰形孔") || raw.includes("腰圆孔")) return "hole";
  if (raw.includes("支管") || raw.includes("branch") || tool?.id === "branch-profile") return "branch";
  if (raw.includes("端") || raw.includes("end") || tool?.target === "end") return "end";
  if (raw.includes("槽") || raw.includes("slot") || raw.includes("notch")) return "slot";
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
    if (!source?.id || source.hidden === true || source.legacy === true) continue;
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

function scopeLabel(scope) {
  return SCOPES.find(([value]) => value === scope)?.[1] ?? "";
}

function typeLabel(tool) {
  return typeOf(tool) === "fixed" ? "定式模具" : "程式模具";
}

function typeShortLabel(tool) {
  return typeOf(tool) === "fixed" ? "定式" : "程式";
}

function categoryLabel(tool) {
  return CATEGORIES.find(([category]) => category === categoryOf(tool))?.[1] ?? "孔型";
}

function targetLabel(tool) {
  if (tool?.target === "part") return "支管 / 零件";
  if (tool?.target === "end") return "端面";
  return "管壁";
}

function toolParameterValues(view, tool) {
  const state = toolLibraryState(view);
  const key = String(tool?.libraryKey ?? keyOf(tool));
  const draft = state.parameterDrafts?.[key];
  return { ...(tool?.defaultParameters ?? {}), ...(draft ?? {}) };
}

function toolParameterVisible(definition, values) {
  const condition = definition?.visibleWhen;
  if (!condition) return true;
  if (condition.op === "eq") return values[condition.parameter] === condition.value;
  if (condition.op === "ne") return values[condition.parameter] !== condition.value;
  if (condition.op === "all") return (condition.conditions ?? []).every((item) => toolParameterVisible({ visibleWhen: item }, values));
  if (condition.op === "any") return (condition.conditions ?? []).some((item) => toolParameterVisible({ visibleWhen: item }, values));
  return true;
}

const FALLBACK_TUBE_PROFILE = {
  id: "round", name: "圆管", libraryScope: "system", profileScope: "system",
  profileType: "parametric-package", specification: "圆管 · 40 × 2",
  defaultParameters: { width: 40, wallThickness: 2 },
  descriptor: { parameters: [
    { key: "width", displayName: "外径", valueType: "number", defaultValue: 40, min: 1, step: 1, unit: "mm" },
    { key: "wallThickness", displayName: "壁厚", valueType: "number", defaultValue: 2, min: 0.1, step: 0.1, unit: "mm" },
  ] },
  previewProfile: { name: "圆管", kind: "round", width: 40, depth: 40, wallThickness: 2,
    contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }] },
};

function toolTubeProfiles(view) {
  const profiles = libraryProfiles(view);
  return profiles.length ? profiles : [FALLBACK_TUBE_PROFILE];
}

function ensureToolTubeProfile(view, role = "main") {
  const state = toolLibraryState(view);
  const profiles = toolTubeProfiles(view);
  const stateKey = role === "branch" ? "branchProfileKey" : "profileKey";
  if (profiles.some((profile) => profileSelectionKey(profile) === state[stateKey])) return profiles.find((profile) => profileSelectionKey(profile) === state[stateKey]);
  const preferred = profiles.find((profile) => profileScopeOf(profile) === "system" && String(profile.id) === "round") ?? profiles[0];
  state[stateKey] = profileSelectionKey(preferred);
  return preferred;
}

function profileScopeOf(profile) {
  return String(profile?.libraryScope ?? profile?.profileScope ?? profile?.scope ?? "user");
}

function toolTubeProfileValues(view, profile, role = "main") {
  const state = toolLibraryState(view);
  const key = profileSelectionKey(profile);
  const drafts = role === "branch" ? state.branchProfileDrafts : state.profileDrafts;
  return { ...(profile?.defaultParameters ?? {}), ...(drafts?.[key] ?? {}) };
}

function profileParameterVisible(definition, values) {
  const condition = definition?.visibleWhen;
  if (!condition) return true;
  if (condition.op === "eq") return values[condition.parameter ?? condition.key] === condition.value;
  if (condition.op === "ne") return values[condition.parameter ?? condition.key] !== condition.value;
  if (condition.op === "all") return (condition.conditions ?? condition.all ?? []).every((item) => profileParameterVisible({ visibleWhen: item }, values));
  if (condition.op === "any") return (condition.conditions ?? condition.any ?? []).some((item) => profileParameterVisible({ visibleWhen: item }, values));
  return true;
}

function renderToolTubeParameterInput(view, profile, definition, role = "main") {
  const values = toolTubeProfileValues(view, profile, role);
  const key = String(definition?.key ?? "");
  if (!key) return "";
  const value = values[key] ?? definition?.defaultValue ?? "";
  const type = definition?.valueType ?? "number";
  const common = `data-profile-parameter-key="${escapeAttr(key)}" data-cam-change-action="tube-designer-tool-library-profile-parameter-change" data-tube-tool-library-profile-key="${escapeAttr(profileSelectionKey(profile))}" data-tube-tool-library-profile-role="${escapeAttr(role)}" data-tube-tool-library-profile-parameter="${escapeAttr(key)}" data-tube-profile-value-type="${escapeAttr(type)}"`;
  const label = escapeText(localizedText(definition?.displayName ?? definition?.name, key));
  if (type === "boolean") return `<label class="tube-designer-field tube-designer-boolean-field"><span>${label}</span><input type="checkbox" ${value ? "checked" : ""} ${common} ${view?.pending ? "disabled" : ""} /></label>`;
  const options = Array.isArray(definition?.options) ? definition.options : [];
  if (options.length) return `<label class="tube-designer-field"><span>${label}</span><select ${common} ${view?.pending ? "disabled" : ""}>${options.map((option) => { const optionValue = typeof option === "object" ? option.value : option; const optionLabel = typeof option === "object" ? (option.displayName ?? option.label ?? optionValue) : option; return `<option value="${escapeAttr(optionValue)}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(localizedText(optionLabel, optionValue))}</option>`; }).join("")}</select></label>`;
  const inputType = type === "string" ? "text" : "number";
  return `<label class="tube-designer-field"><span>${label}</span><input type="${inputType}" value="${escapeAttr(value)}" ${definition.min != null ? `min="${escapeAttr(definition.min)}"` : ""} ${definition.max != null ? `max="${escapeAttr(definition.max)}"` : ""} ${definition.step != null ? `step="${escapeAttr(definition.step)}"` : ""} ${common} ${view?.pending ? "disabled" : ""} /></label>`;
}

function renderToolParameterInput(view, tool, definition) {
  const values = toolParameterValues(view, tool);
  const key = String(definition?.key ?? "");
  if (!key) return "";
  const value = values[key] ?? definition?.defaultValue ?? "";
  const type = definition?.valueType ?? "number";
  const common = `data-cam-change-action="tube-designer-tool-library-parameter-change" data-tube-tool-library-key="${escapeAttr(tool.libraryKey)}" data-tube-tool-library-parameter="${escapeAttr(key)}"`;
  if (type === "boolean") return `<label class="tube-tool-library-parameter-check"><input type="checkbox" ${value ? "checked" : ""} ${common} ${view?.pending ? "disabled" : ""} /><span>${escapeText(localizedText(definition.displayName, key))}</span></label>`;
  if (type === "string" && Array.isArray(definition?.options)) {
    return `<label><span>${escapeText(localizedText(definition.displayName, key))}</span><select ${common} ${view?.pending ? "disabled" : ""}>${definition.options.map((option) => `<option value="${escapeAttr(option.value)}" ${String(option.value) === String(value) ? "selected" : ""}>${escapeText(localizedText(option.label ?? option.displayName, option.value))}</option>`).join("")}</select></label>`;
  }
  return `<label><span>${escapeText(localizedText(definition.displayName, key))}</span><input type="${type === "string" ? "text" : "number"}" value="${escapeAttr(value)}" ${definition.min !== undefined ? `min="${escapeAttr(definition.min)}"` : ""} ${definition.max !== undefined ? `max="${escapeAttr(definition.max)}"` : ""} ${definition.step !== undefined ? `step="${escapeAttr(definition.step)}"` : ""} ${common} ${view?.pending ? "disabled" : ""} /></label>`;
}

function renderToolIllustration(tool) {
  const id = String(tool?.id ?? "");
  const category = categoryOf(tool);
  const illustration = tool?.illustration;
  if (illustration && typeof illustration === "object") {
    const viewBox = escapeAttr(illustration.viewBox ?? "0 0 48 48");
    const paths = (illustration.paths ?? []).map((path) => `<path d="${escapeAttr(typeof path === "string" ? path : path?.d)}" />`).join("");
    const circles = (illustration.circles ?? []).map((circle) => `<circle cx="${escapeAttr(circle?.cx)}" cy="${escapeAttr(circle?.cy)}" r="${escapeAttr(circle?.r)}" />`).join("");
    const polygons = (illustration.polygons ?? []).map((polygon) => `<polygon points="${escapeAttr(typeof polygon === "string" ? polygon : polygon?.points)}" />`).join("");
    if (paths || circles || polygons) return `<svg viewBox="${viewBox}" aria-hidden="true">${paths}${circles}${polygons}</svg>`;
  }
  const solidProfile = Array.isArray(tool?.geometry?.model?.geometry)
    ? tool.geometry.model.geometry.find((node) => node?.operator === "profile2d" && Array.isArray(node?.arguments?.contours))
    : null;
  const contours = tool?.geometry?.contours ?? tool?.geometry?.profile?.contours ?? tool?.contours
    ?? solidProfile?.arguments?.contours;
  if (Array.isArray(contours) && contours.length) {
    return renderProfileSvg({ name: toolName(tool), contours, width: tool?.geometry?.width, depth: tool?.geometry?.height });
  }
  if (id === "circle") return `<svg viewBox="0 0 48 48" aria-hidden="true"><circle cx="24" cy="24" r="13" /></svg>`;
  if (id === "ellipse") return `<svg viewBox="0 0 48 48" aria-hidden="true"><ellipse cx="24" cy="24" rx="16" ry="10" /></svg>`;
  if (id === "slot") return `<svg viewBox="0 0 48 48" aria-hidden="true"><rect x="8" y="17" width="32" height="14" rx="7" /></svg>`;
  if (id === "rectangle" || id === "square") return `<svg viewBox="0 0 48 48" aria-hidden="true"><rect x="10" y="10" width="28" height="28" rx="${id === "rectangle" ? "5" : "2"}" /></svg>`;
  if (id === "hexagon") return `<svg viewBox="0 0 48 48" aria-hidden="true"><polygon points="13,10 35,10 42,24 35,38 13,38 6,24" /></svg>`;
  if (id === "triangle") return `<svg viewBox="0 0 48 48" aria-hidden="true"><polygon points="24,7 41,38 7,38" /></svg>`;
  if (id === "single-d") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M8 34 H24 A10 10 0 0 0 24 14 H8 Z" /></svg>`;
  if (id === "double-d") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M14 14 H34 A10 10 0 0 1 34 34 H14 A10 10 0 0 1 14 14 Z" /></svg>`;
  if (id === "diamond-12") return `<svg viewBox="0 0 48 48" aria-hidden="true"><polygon points="8,24 24,12 40,24 24,36" /></svg>`;
  if (id === "end-convex") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M8 10h22a14 14 0 0 1 0 28H8Z" /></svg>`;
  if (id === "end-cope") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M8 10h32v28H8M8 24h18a8 8 0 0 0 0-16" /></svg>`;
  if (id === "end-key-joint") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M8 10h32v28H8V27h16v-6H8Z" /></svg>`;
  if (id === "end-miter") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M8 10h32v28H8Z M8 38 40 10" /></svg>`;
  if (id === "end-square") return `<svg viewBox="0 0 48 48" aria-hidden="true"><rect x="9" y="9" width="30" height="30" rx="1" /></svg>`;
  if (category === "branch") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M24 38 V14 M24 14 C24 8 39 8 39 14 V38" /></svg>`;
  if (category === "end") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M10 10 H38 V38 H10 Z M10 24 H38" /></svg>`;
  return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M10 24 H38 M24 10 V38" /></svg>`;
}

function renderToolParameterDiagram(view, tool, values, definitions) {
  const rows = definitions.map((definition, index) => {
    const value = values[definition.key] ?? definition.defaultValue ?? "—";
    const unit = definition.unit ? ` ${definition.unit}` : "";
    return `<div class="tube-tool-library-diagram-row"><b>${index + 1}</b><span><strong>${escapeText(localizedText(definition.displayName ?? definition.name, definition.key))}</strong><small>${escapeText(localizedText(definition.description, "对应当前模具截面参数"))}</small></span><em>${escapeText(localizedText(value, "—"))}${escapeText(localizedText(unit))}</em></div>`;
  }).join("");
  const state = toolLibraryState(view);
  const expanded = !!state.showToolDiagram;
  return `<section class="tube-tool-library-parameter-diagram"><header><div><strong>参数示意图</strong><span>当前模具截面 · 参数与预览同步</span></div><button type="button" class="tube-tool-library-diagram-toggle" data-cam-action="tube-designer-tool-library-toggle-diagram" data-tube-tool-library-diagram="tool" aria-expanded="${expanded}">${expanded ? "隐藏示意图" : "显示示意图"}</button></header>${expanded ? `<div class="tube-tool-library-diagram-content"><div class="tube-tool-library-diagram-art">${renderToolIllustration(tool)}</div>${rows ? `<div class="tube-tool-library-diagram-legend">${rows}</div>` : `<p class="tube-tool-library-diagram-message">定式模具使用导入的固定截面，不需要额外参数。</p>`}</div>` : ""}</section>`;
}

function renderToolTubeSection(view, role = "main") {
  const isBranch = role === "branch";
  const profiles = toolTubeProfiles(view);
  const profile = ensureToolTubeProfile(view, role);
  const selectedKey = profileSelectionKey(profile);
  const values = toolTubeProfileValues(view, profile, role);
  const definitions = isParametricProfile(profile) && Array.isArray(profile?.descriptor?.parameters)
    ? profile.descriptor.parameters.filter((definition) => profileParameterVisible(definition, values)) : [];
  const snapshotBase = profileSnapshot(profile);
  const snapshot = snapshotBase ? {
    ...snapshotBase,
    parameterDiagram: snapshotBase.parameterDiagram ?? profile?.descriptor?.parameterDiagram,
    parameterDefinitions: snapshotBase.parameterDefinitions ?? profile?.descriptor?.parameters,
  } : null;
  const options = profiles.map((item) => `<option value="${escapeAttr(profileSelectionKey(item))}" ${profileSelectionKey(item) === selectedKey ? "selected" : ""}>${escapeText(profileName(item))}${profileScopeOf(item) === "template" ? " · 模板" : profileScopeOf(item) === "user" ? " · 我的" : " · 系统"}</option>`).join("");
  const state = toolLibraryState(view);
  const showDiagram = !!(isBranch ? state.showBranchProfileDiagram : state.showProfileDiagram);
  const title = isBranch ? "支管 / 管型参数" : "主管 / 管型参数";
  const description = isBranch ? "选择支管截面，参数只影响当前支管模具预览" : "先选主管管型，再调整当前模具预览使用的尺寸";
  const fieldLabel = isBranch ? "支管管型" : "主管管型";
  const diagramTitle = isBranch ? "支管管型参数示意图" : "管型参数示意图";
  return `<section class="tube-tool-library-tube-section${isBranch ? " tube-tool-library-branch-section" : ""}" data-profile-parameter-scope data-tube-tool-library-tube-editor data-tube-tool-library-profile-role="${escapeAttr(role)}">
    <header><div><strong>${title}</strong><span>${description}</span></div><div class="tube-tool-library-tube-header-actions"><span class="tube-tool-library-section-badge">${escapeText(profileSpecification(profile) || "当前管型")}</span>${definitions.length ? `<button type="button" class="tube-tool-library-diagram-toggle" data-cam-action="tube-designer-tool-library-toggle-diagram" data-tube-tool-library-diagram="${escapeAttr(isBranch ? "branch-profile" : "profile")}" aria-expanded="${showDiagram}">${showDiagram ? "隐藏管型参数示意图" : "显示管型参数示意图"}</button>` : ""}</div></header>
    <label class="tube-designer-field wide"><span>${fieldLabel}</span><select data-cam-change-action="tube-designer-tool-library-profile-change" data-tube-tool-library-profile-role="${escapeAttr(role)}" ${view?.pending ? "disabled" : ""}>${options || `<option>暂无可用管型</option>`}</select></label>
    ${definitions.length ? `${showDiagram ? `<div data-profile-library-diagram>${renderProfileParameterDiagram(snapshot, { definitions: profile?.descriptor?.parameters ?? definitions, parameters: values, compact: true, title: diagramTitle })}</div>` : ""}<div class="tube-tool-library-tube-parameter-grid">${definitions.map((definition) => renderToolTubeParameterInput(view, profile, definition, role)).join("")}</div>` : `<p class="tube-tool-library-diagram-message">当前管型为固定截面；模具预览会直接使用它的实际轮廓。</p>`}
    ${!isBranch ? `<label class="tube-designer-field wide"><span>主管预览长度（mm）</span><input type="number" min="1" max="100000" step="1" value="${escapeAttr(state.previewLength)}" data-cam-change-action="tube-designer-tool-library-preview-length-change" ${view?.pending ? "disabled" : ""} /></label>` : ""}
  </section>`;
}

function emptyText(state) {
  if (state.search) return ["没有匹配的模具", "请调整搜索内容。"];
  if (state.catalogueStatus === "loading") return ["正在读取模具目录", "正在同步系统、模板和我的模具，请稍候。"];
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
  const [emptyTitle, emptyNote] = emptyText(state);
  const groups = CATEGORIES.slice(1).map(([category, label]) => ({
    category,
    label,
    tools: tools.filter((tool) => categoryOf(tool) === category),
  })).filter((group) => group.tools.length);
  const renderGroup = (group) => {
    const expanded = !state.collapsed.includes(group.category);
    return `<section class="tube-tool-library-group">
      <button type="button" class="tube-tool-library-group-heading" data-cam-action="tube-designer-tool-library-toggle-category" data-tube-tool-library-group="${escapeAttr(group.category)}" aria-expanded="${expanded}"><span>${expanded ? "▾" : "▸"} ${escapeText(group.label)}</span><small>${escapeText(scopeLabel(state.scope))} · ${group.tools.length}</small></button>
      <div class="tube-tool-library-group-items" ${expanded ? "" : "hidden"}>${group.tools.map((tool) => `<button type="button" class="tube-tool-library-card ${tool.libraryKey === selected ? "selected" : ""}" data-cam-action="tube-designer-tool-library-select" data-tube-tool-library-key="${escapeAttr(tool.libraryKey)}" aria-label="${escapeAttr(`${toolName(tool)}，${categoryLabel(tool)}，${typeLabel(tool)}，${sourceLabel(tool)}`)}" ${view?.pending ? "disabled" : ""}>
        <span class="tube-tool-library-card-art">${renderToolIllustration(tool)}</span><span class="tube-tool-library-card-copy"><strong>${escapeText(toolName(tool))}</strong><small>${escapeText(typeShortLabel(tool))}</small></span>
      </button>`).join("")}</div>
    </section>`;
  };
  return `<div class="tube-designer-panel tube-tool-library-panel">
    <div class="tube-designer-heading tube-tool-library-heading"><div><strong>模具库</strong><span>${all.length} 个模具 · 当前显示 ${tools.length} 个</span></div></div>
    <div class="tube-component-library-filters tube-tool-library-filters">
      <input type="search" aria-label="搜索模具" placeholder="搜索名称、类别、所属模板" value="${escapeAttr(state.search)}" data-cam-change-action="tube-designer-tool-library-search" />
      <div class="tube-tool-library-tab-row" role="tablist" aria-label="模具来源">${SCOPES.map(([scope, label]) => `<button type="button" role="tab" aria-selected="${scope === state.scope}" class="${scope === state.scope ? "selected" : ""}" data-cam-action="tube-designer-tool-library-scope" data-tube-tool-library-scope="${scope}" ${view?.pending ? "disabled" : ""}>${label}<small>${all.filter((tool) => scopeOf(tool) === scope).length}</small></button>`).join("")}</div>
    </div>
    <div class="tube-tool-library-list" role="tabpanel">
      ${state.error ? `<div class="tube-tool-library-error" role="alert"><strong>模具目录读取失败</strong><span>${escapeText(state.error)}</span><button type="button" class="tube-designer-secondary" data-cam-action="tools.refresh" ${view?.pending ? "disabled" : ""}>重新读取</button></div>` : ""}
      ${tools.length ? groups.map(renderGroup).join("") : `<div class="tube-tool-library-empty"><strong>${escapeText(emptyTitle)}</strong><span>${escapeText(emptyNote)}</span></div>`}
    </div>
  </div>`;
}

export function renderToolLibraryRightPane(_context, view) {
  const tools = visibleLibraryTools(view);
  const selectedKey = ensureSelection(view, tools);
  const tool = tools.find((item) => item.libraryKey === selectedKey);
  if (!tool) return `<div class="tube-designer-panel tube-tool-library-editor-empty"><div class="tube-designer-heading"><strong>模具属性</strong><span>尚未选择模具</span></div><div class="tube-designer-empty">从左侧选择模具，查看其来源、类型和预览规则。</div></div>`;
  const allParameters = Array.isArray(tool.parameters) ? tool.parameters : [];
  const values = toolParameterValues(view, tool);
  // A branch mould's angle/azimuth/roll values describe how the punch is
  // placed in the manufacturing workflow.  They are intentionally not
  // edited in the resource library.  The library owns the branch section;
  // placement remains in the punch wizard where it can be validated against
  // the selected operation.
  const branchTool = tool?.target === "part" && tool?.requiresSection;
  const parameters = branchTool ? [] : allParameters.filter((parameter) => toolParameterVisible(parameter, values));
  const programmatic = typeOf(tool) === "programmatic";
  const toolDiagram = branchTool ? "" : renderToolParameterDiagram(view, tool, values, parameters);
  return `<div class="tube-designer-panel tube-tool-library-editor"><div class="tube-tool-library-editor-heading"><div class="tube-tool-library-editor-title"><span class="tube-tool-library-editor-art">${renderToolIllustration(tool)}</span><div><strong>${escapeText(toolName(tool))}</strong><span>${escapeText(typeShortLabel(tool))} · ${escapeText(sourceLabel(tool))}</span></div></div><span class="tube-tool-library-editor-badge">${escapeText(categoryLabel(tool))}</span></div>
    <div class="tube-tool-library-editor-body">
      ${renderToolTubeSection(view)}
      <section class="tube-tool-library-mold-section">
        <header><div><strong>模具信息</strong><span>${escapeText(typeShortLabel(tool))} · ${escapeText(targetLabel(tool))} · 修改后自动更新场景</span></div><span class="tube-tool-library-section-badge">${escapeText(categoryLabel(tool))}</span></header>
        <dl class="tube-tool-library-meta"><dt>模具 ID</dt><dd>${escapeText(tool.id)}</dd><dt>版本</dt><dd>${escapeText(tool.version ?? "—")}</dd><dt>来源</dt><dd>${escapeText(sourceLabel(tool))}</dd></dl>
        ${branchTool ? renderToolTubeSection(view, "branch") : programmatic && parameters.length ? `<div class="tube-tool-library-mold-parameters"><header><strong>模具参数</strong><span>修改后自动更新场景</span></header><div class="tube-tool-library-parameter-grid">${parameters.map((definition) => renderToolParameterInput(view, tool, definition)).join("")}</div></div>` : `<div class="tube-tool-library-fixed-card"><strong>固定截面</strong><span>定式模具只保存一个闭合截面，可由 DXF 导入；当前参数由模具定义固定。</span></div>`}
        ${toolDiagram}
      </section>
      <section class="tube-tool-library-note"><strong>${programmatic ? "程式模具" : "定式模具"}</strong><span>${programmatic ? "由模具自带参数程式生成截面，再统一拉伸成实体。" : "由固定闭合截面统一拉伸成实体。"} 位置、姿态和阵列由使用它的业务单独设置。</span></section>
      <p class="tube-tool-library-provenance">模具库只管理模具定义；冲孔、三维绘制和产品拆单各自保存自己的模具引用、定位、姿态和阵列记录。</p>
    </div>
  </div>`;
}

export function renderToolLibraryViewportOverlay(_context, view) {
  const tools = visibleLibraryTools(view);
  const key = ensureSelection(view, tools);
  const tool = tools.find((item) => item.libraryKey === key);
  if (tool) ensureToolLibraryPreview(_context, view, tool);
  const state = toolLibraryState(view);
  return `<div class="tube-tool-library-hud"><strong>${escapeText(tool ? toolName(tool) : "模具库")}</strong><span>${escapeText(tool ? `${typeShortLabel(tool)} · ${sourceLabel(tool)}` : "选择左侧模具查看定义")}</span><small>${tool ? (state.previewRequest ? "正在生成模具预览…" : "主管 + 模具拉伸体 · 拖动旋转 · 滚轮缩放") : "选择左侧模具查看定义"}</small>${tool && state.previewRequest ? '<div class="tube-tool-library-preview-progress" role="progressbar" aria-label="正在生成模具预览"><i></i></div>' : ""}${state.previewError ? `<p role="alert">${escapeText(state.previewError)}</p><button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-tool-library-retry-preview">重新预览</button>` : ""}</div>`;
}

export function toolPreviewKey(view, tool) {
  // A catalogue refresh may keep the same library key while replacing the
  // executable package. Include its identity so an old extrusion can never
  // survive a version/digest change.
  const profile = ensureToolTubeProfile(view);
  const branchTool = tool?.target === "part" && tool?.requiresSection;
  const branchProfile = branchTool ? ensureToolTubeProfile(view, "branch") : null;
  return JSON.stringify([
    tool?.libraryKey ?? "", tool?.version ?? "", tool?.digest ?? "", toolParameterValues(view, tool),
    profileSelectionKey(profile), profile?.packageDigest ?? profile?.descriptor?.version ?? profile?.version ?? "",
    toolTubeProfileValues(view, profile), toolLibraryState(view).previewLength,
    branchProfile ? profileSelectionKey(branchProfile) : "",
    branchProfile?.packageDigest ?? branchProfile?.descriptor?.version ?? branchProfile?.version ?? "",
    branchProfile ? toolTubeProfileValues(view, branchProfile, "branch") : {},
  ]);
}

export function buildToolLibraryPreviewPayload(view, tool, options = {}) {
  const values = toolParameterValues(view, tool);
  const profile = ensureToolTubeProfile(view);
  const profileValues = toolTubeProfileValues(view, profile);
  const profileSnapshotValue = profileSnapshot(profile) ?? FALLBACK_TUBE_PROFILE.previewProfile;
  const branchTool = tool?.target === "part" && tool?.requiresSection;
  const branchProfile = branchTool ? ensureToolTubeProfile(view, "branch") : null;
  const branchProfileValues = branchProfile ? toolTubeProfileValues(view, branchProfile, "branch") : {};
  const branchProfileSnapshot = branchTool
    ? (options.branchProfileSnapshot ?? profileSnapshot(branchProfile) ?? profileSnapshotValue)
    : profileSnapshotValue;
  const toolRef = { id: String(tool.id), version: String(tool.version ?? "") };
  if (tool.digest) toolRef.digest = String(tool.digest);
  const base = { profileRef: profileRef(profile), parameters: profileValues, length: toolLibraryState(view).previewLength, features: [], ends: { start: { type: "keep" }, end: { type: "keep" } }, toolsOnly: true };
  if (tool.target === "end") {
    base.ends.start = { type: String(tool.id), toolRef, toolParameters: values, trim: 0, datum: "long", rotation: 0 };
    if (tool.requiresSection) {
      base.ends.start.section = { profile: profileSnapshotValue };
    }
    return base;
  }
  // "center" is a datum, not a second start datum.  A station of half the
  // length with reference=center would therefore land at 3/4 length.  Keep
  // the center datum explicit and use zero offset so every wall/branch/V-slot
  // preview is actually at the tube midpoint.
  const feature = { id: "library-preview", enabled: true, toolRef, toolParameters: values,
    toolTarget: tool.target === "part" ? "part" : undefined, face: "top", reference: "center",
    station: 0, layoutDatum: "base", offset: 0, rotation: 0, arrayCount: 1, arrayPitch: 50, rowCount: 1, rowPitch: 20,
    through: false, opposite: false, reverse: false, depthMode: "single" };
  if (tool.target === "part") {
    feature.section = { profile: branchProfileSnapshot };
    if (branchProfile) {
      feature.section.profileRef = profileRef(branchProfile);
      feature.section.parameters = branchProfileValues;
    }
  }
  base.features.push(feature);
  return base;
}

async function resolveToolProfileSnapshot(context, view, profile, role = "branch") {
  const fallback = profileSnapshot(profile) ?? FALLBACK_TUBE_PROFILE.previewProfile;
  if (!profile || !isParametricProfile(profile) || typeof context?.sceneProxy?.invoke !== "function") return fallback;
  const response = await context.sceneProxy.invoke("TubeDesigner.EvaluateProfilePackage", {
    profileRef: profileRef(profile),
    parameters: toolTubeProfileValues(view, profile, role),
  }, { timeoutMs: 120000 });
  return response?.profile ?? fallback;
}

function toolPreviewReferences(response = {}) {
  const references = [];
  for (const value of [response.baseGeometry, response.toolGeometry, response.baseMaterial, response.toolMaterial]) {
    if (value?.url && Number(value.version) > 0) references.push(`${value.url}@${value.version}`);
  }
  for (const item of response.toolPreviews ?? []) {
    if (item?.geometry?.url && Number(item.geometry.version) > 0) references.push(`${item.geometry.url}@${item.geometry.version}`);
  }
  return references;
}

export async function applyToolLibraryPreview(context, view, tool, response, key, request) {
  if (view.activeAreaId !== "tools" || toolPreviewKey(view, tool) !== key || request !== toolLibraryState(view).previewRequest) return;
  if (!view.viewport?.applyViewSnapshot) throw new Error("三维视口尚未准备好。");
  const rows = buildPunchPreviewRows(response, "tools");
  const toolRows = rows.filter((row) => String(row?.entityId ?? "").startsWith("punch-preview-tool:") || String(row?.entityId ?? "") === "punch-preview-tools");
  if (!rows.length || !toolRows.length) throw new Error("模具预览没有返回主管和刀具拉伸体。");
  const revision = `tool-library:${key}`;
  const resources = context.sceneProxy?.resources ?? view.sceneProxy?.resources ?? context.projectProxy?.resources;
  const receipt = await view.viewport.applyViewSnapshot({ revision, rows }, resources);
  if (!receipt?.applied || receipt.missingGeometryEntityIds?.length) throw new Error("模具预览几何未完整进入视口。");
  if (view.activeAreaId !== "tools" || toolPreviewKey(view, tool) !== key || request !== toolLibraryState(view).previewRequest) return;
  const state = toolLibraryState(view);
  if (!state.previewApplied) {
    view.viewport.setStandardView?.("iso");
    view.viewport.fitViewToViewport?.(1.2);
    state.previewApplied = true;
  } else {
    view.viewport.setVisibleEntityIds?.(receipt.entityIds ?? rows.map((row) => row.entityId));
  }
  state.preview = { key, response, references: toolPreviewReferences(response) };
  // Resource pages do not have a persisted View snapshot.  Keep the custom
  // mould/blank entities alive when the workbench repaints the right panel or
  // HUD; otherwise mountRenderViewport would clear them immediately.
  view.preserveCustomViewportEntities = true;
}

function ensureToolLibraryPreview(context, view, tool) {
  if (view.activeAreaId !== "tools" || !tool || typeof context?.sceneProxy?.invoke !== "function") return;
  const state = toolLibraryState(view);
  const key = toolPreviewKey(view, tool);
  if (state.preview?.key === key || state.previewRequest?.key === key || state.previewFailureKey === key) return;
  state.previewError = "";
  const request = { key, toolKey: tool.libraryKey, promise: null };
  request.promise = Promise.resolve().then(async () => {
      const branchProfile = tool?.target === "part" && tool?.requiresSection
        ? ensureToolTubeProfile(view, "branch") : null;
      const branchProfileSnapshot = branchProfile
        ? await resolveToolProfileSnapshot(context, view, branchProfile, "branch") : null;
      return context.sceneProxy.invoke("TubeDesigner.PreviewPunchWizard",
        buildToolLibraryPreviewPayload(view, tool, { branchProfileSnapshot }), { timeoutMs: 120000 });
    })
    .then((response) => applyToolLibraryPreview(context, view, tool, response, key, request))
    .then(() => {
      if (state.previewRequest === request) { state.previewRequest = null; state.previewFailureKey = ""; }
      if (view.activeAreaId === "tools") view.tubeDesignerToolLibraryRenderProject?.();
    })
    .catch((error) => {
      if (state.previewRequest !== request) return;
      state.previewRequest = null;
      state.previewFailureKey = key;
      state.previewError = `模具预览失败：${error?.message ?? error}`;
      if (view.activeAreaId === "tools") view.tubeDesignerToolLibraryRenderProject?.();
    });
  state.previewRequest = request;
}

export function ensureToolLibraryCatalogue(context, view, ops) {
  const state = toolLibraryState(view);
  if (state.cataloguePromise) return state.cataloguePromise;
  // A failed first request must not permanently brick the library.  Startup
  // can briefly contend with the scene/user-data SDO; opening 模具 again (or
  // pressing 重新读取) should get a fresh request instead of returning the
  // old rejected state.
  if (state.catalogueStatus === "ready"
      || (state.catalogueStatus !== "idle" && state.catalogueStatus !== "error")
      || typeof context?.sceneProxy?.invoke !== "function") return Promise.resolve();
  state.catalogueStatus = "loading";
  state.error = "";
  const requestToken = Number(state.catalogueRequestToken ?? 0) + 1;
  state.catalogueRequestToken = requestToken;
  const load = async (attempt = 0) => {
    try {
      // afterProjectRender can run during the first synchronous mount, before
      // entry.mjs has installed its shared user-data promise. Yield once so
      // that promise (and its scene-refresh gate) is visible before issuing
      // the SDO request.
      await Promise.resolve();
      // Scene restoration and ListUserData both use the embedded Python host.
      // Wait for the scene request as well when one is already in flight. The
      // initial yield above ensures afterProjectRender has had a chance to
      // install this promise before we inspect it.
      const sceneSynchronization = view.tubeDesignerSynchronizationPromise;
      if (sceneSynchronization) {
        try { await sceneSynchronization; } catch (_) { /* catalogue can retry */ }
      }
      // ListUserData is explicitly gated behind the initial scene refresh in
      // entry.mjs. Waiting for that shared promise also serializes this
      // catalogue call with the embedded Python host and prevents the
      // intermittent GetPunchTools timeout seen on startup.
      const userDataSynchronization = view.tubeDesignerUserDataSynchronizationPromise;
      if (userDataSynchronization) {
        try { await userDataSynchronization; } catch (_) { /* retry below */ }
      }
      const userDataRefresh = view.tubeDesignerUserDataRefreshPromise;
      if (userDataRefresh) {
        try { await userDataRefresh; } catch (_) { /* catalogue is independent */ }
      }
      const response = await context.sceneProxy.invoke("TubeDesigner.GetPunchTools", {}, { timeoutMs: 30000 });
      const tools = Array.isArray(response?.tools) ? response.tools : [];
      if (!tools.length) {
        const diagnostics = Array.isArray(response?.errors) ? response.errors.filter(Boolean).join("；") : "";
        throw new Error(diagnostics || "模具目录为空，请检查内置模具资源是否完整。");
      }
      return response;
    } catch (error) {
      // The embedded Python host can still be warming up immediately after a
      // project switch. Retry once before exposing a failure; never mark an
      // empty catalogue as ready, otherwise the UI gets stuck at 0 until a
      // manual refresh.
      if (attempt < 1) return load(attempt + 1);
      throw error;
    }
  };
  const request = load().then((response) => {
    const tools = Array.isArray(response?.tools) ? response.tools : [];
    if (state.catalogueRequestToken !== requestToken) return;
    view.tubeDesignerSystemPunchTools = tools
      .filter((tool) => scopeOf(tool) === "system")
      .map((tool) => ({ ...tool, libraryScope: "system" }));
    view.tubeDesignerTemplatePunchTools = tools
      .filter((tool) => scopeOf(tool) === "template")
      .map((tool) => ({ ...tool, libraryScope: "template" }));
    const userTools = tools
      .filter((tool) => scopeOf(tool) === "user")
      .map((tool) => ({ ...tool, libraryScope: "user" }));
    if (userTools.length) {
      view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], profileId: "" };
      const personal = view.tubeDesignerUserData.punchTools ??= [];
      for (const tool of userTools) {
        const index = personal.findIndex((item) => String(item?.id ?? "") === String(tool.id));
        if (index >= 0) personal[index] = tool;
        else personal.push(tool);
      }
    }
    state.catalogueStatus = "ready";
    state.error = "";
    if (view.activeAreaId === "tools") ops?.renderProject?.(context, view);
  }).catch((error) => {
    if (state.catalogueRequestToken !== requestToken) return;
    state.catalogueStatus = "error";
    state.error = error?.message ?? String(error);
    if (view.activeAreaId === "tools") ops?.renderProject?.(context, view);
  }).finally(() => {
    if (state.cataloguePromise === request) state.cataloguePromise = null;
  });
  state.cataloguePromise = request;
  return request;
}

export async function handleToolLibraryAction(context, view, action, target, ops) {
  const state = toolLibraryState(view);
  if (action === "tube-designer-tool-library-profile-change") {
    if (!view.pending) {
      const profileKey = String(target?.value ?? "");
      const role = target?.dataset?.tubeToolLibraryProfileRole === "branch" ? "branch" : "main";
      const stateKey = role === "branch" ? "branchProfileKey" : "profileKey";
      if (toolTubeProfiles(view).some((profile) => profileSelectionKey(profile) === profileKey)) {
        const hadPreview = !!state.preview;
        state[stateKey] = profileKey;
        state.previewError = ""; state.previewFailureKey = ""; state.preview = null;
        state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-profile-parameter-change") {
    if (!view.pending) {
      const profileKey = String(target?.dataset?.tubeToolLibraryProfileKey ?? "");
      const parameter = String(target?.dataset?.tubeToolLibraryProfileParameter ?? "");
      const role = target?.dataset?.tubeToolLibraryProfileRole === "branch" ? "branch" : "main";
      const profile = toolTubeProfiles(view).find((item) => profileSelectionKey(item) === profileKey);
      const definition = profile?.descriptor?.parameters?.find((item) => String(item?.key ?? "") === parameter);
      if (profile && definition) {
        const hadPreview = !!state.preview;
        let value;
        if (definition.valueType === "boolean") value = !!target.checked;
        else if (definition.valueType === "string") value = String(target.value ?? "");
        else value = Number(target.value);
        if (typeof value === "number" && !Number.isFinite(value)) return { handled: true };
        const drafts = role === "branch" ? state.branchProfileDrafts : state.profileDrafts;
        drafts[profileKey] = { ...(drafts[profileKey] ?? {}), [parameter]: value };
        state.previewError = ""; state.previewFailureKey = ""; state.preview = null;
        state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-preview-length-change") {
    if (!view.pending) {
      const value = Number(target?.value);
      if (Number.isFinite(value)) {
        const hadPreview = !!state.preview;
        state.previewLength = Math.min(100000, Math.max(1, value));
        state.previewError = ""; state.previewFailureKey = ""; state.preview = null;
        state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-toggle-diagram") {
    if (!view.pending) {
      const kind = String(target?.dataset?.tubeToolLibraryDiagram ?? "");
      if (kind === "profile") state.showProfileDiagram = !state.showProfileDiagram;
      else if (kind === "branch-profile") state.showBranchProfileDiagram = !state.showBranchProfileDiagram;
      else if (kind === "tool") state.showToolDiagram = !state.showToolDiagram;
      else return { handled: true };
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-scope") {
    if (!view.pending) {
      const scope = String(target?.dataset?.tubeToolLibraryScope ?? "");
      if (SCOPES.some(([value]) => value === scope)) { const hadPreview = !!state.preview; state.scope = scope; state.selectedKey = ""; state.showToolDiagram = false; state.preview = null; state.previewError = ""; state.previewFailureKey = ""; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview; ops.renderProject(context, view); }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-type") {
    if (!view.pending) {
      const type = String(target?.dataset?.tubeToolLibraryType ?? "");
      if (TYPES.some(([value]) => value === type)) { const hadPreview = !!state.preview; state.type = type; state.selectedKey = ""; state.preview = null; state.previewError = ""; state.previewFailureKey = ""; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview; ops.renderProject(context, view); }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-category") {
    if (!view.pending) {
      const category = String(target?.dataset?.tubeToolLibraryCategory ?? "");
      if (CATEGORIES.some(([value]) => value === category)) { const hadPreview = !!state.preview; state.category = category; state.selectedKey = ""; state.preview = null; state.previewError = ""; state.previewFailureKey = ""; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview; ops.renderProject(context, view); }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-toggle-category") {
    if (!view.pending) {
      const category = String(target?.dataset?.tubeToolLibraryGroup ?? "");
      if (CATEGORIES.some(([value]) => value === category)) {
        state.collapsed = state.collapsed.includes(category)
          ? state.collapsed.filter((value) => value !== category)
          : [...state.collapsed, category];
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-search") {
    if (!view.pending) { const hadPreview = !!state.preview; state.search = String(target?.value ?? ""); state.selectedKey = ""; state.preview = null; state.previewError = ""; state.previewFailureKey = ""; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview; ops.renderProject(context, view); }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-select") {
    // Keep the current主管 and camera in place while the next mould mesh is
    // fetched. applyToolLibraryPreview reuses the same blank/tool entity IDs,
    // so the viewport replaces only the tool object when the response lands.
    if (!view.pending) { const hadPreview = !!state.preview; state.selectedKey = String(target?.dataset?.tubeToolLibraryKey ?? ""); state.showToolDiagram = false; state.previewError = ""; state.previewFailureKey = ""; state.preview = null; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview; ops.renderProject(context, view); }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-parameter-change") {
    if (!view.pending) {
      const key = String(target?.dataset?.tubeToolLibraryKey ?? "");
      const parameter = String(target?.dataset?.tubeToolLibraryParameter ?? "");
      const tool = libraryTools(view).find((item) => item.libraryKey === key);
      const definition = tool?.parameters?.find((item) => item?.key === parameter);
      if (tool && definition) {
        const hadPreview = !!state.preview;
        const value = definition.valueType === "boolean" ? !!target.checked
          : definition.valueType === "string" ? String(target.value ?? "") : Number(target.value);
        state.parameterDrafts[key] = { ...(state.parameterDrafts[key] ?? {}), [parameter]: value };
        state.previewError = ""; state.previewFailureKey = ""; state.preview = null; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-retry-preview") {
    if (!view.pending) { const hadPreview = !!state.preview; state.previewError = ""; state.previewFailureKey = ""; state.preview = null; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview; ops.renderProject(context, view); }
    return { handled: true };
  }
  return { handled: false };
}

export async function handleToolLibraryRibbonCommand(context, view, commandId, ops) {
  if (commandId === "tools.import-dxf") {
    await importToolDxf(context, view, ops);
    return true;
  }
  if (commandId === "tools.import-package") {
    await importToolPackage(context, view, ops);
    return true;
  }
  if (commandId !== "tools.refresh") return false;
  const state = toolLibraryState(view);
  state.catalogueStatus = "idle";
  state.error = "";
  ops?.renderProject?.(context, view);
  await ensureToolLibraryCatalogue(context, view, ops);
  return true;
}

function resolveToolBridge(context) {
  return context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
}

function upsertUserTool(view, tool) {
  if (!tool?.id) throw new Error("保存模具后没有返回记录标识。");
  view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], punchTools: [], productTemplates: [], profileId: "" };
  const tools = view.tubeDesignerUserData.punchTools ??= [];
  const index = tools.findIndex((item) => String(item?.id ?? "") === String(tool.id));
  if (index >= 0) tools[index] = { ...tool, libraryScope: "user" };
  else tools.push({ ...tool, libraryScope: "user" });
  const state = toolLibraryState(view);
  state.scope = "user";
  state.type = typeOf(tool);
  state.category = "all";
  state.search = "";
  state.selectedKey = keyOf({ ...tool, libraryScope: "user" }, "user");
  return tool;
}

async function runToolTask(context, view, ops, progress, work) {
  if (view.pending) return null;
  view.pending = true;
  view.progress = { ...progress, mode: "Resources" };
  view.error = "";
  ops.renderProject(context, view);
  try {
    const result = await work();
    ops.renderProject(context, view);
    return result;
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops.renderProject(context, view);
    return null;
  } finally {
    view.pending = false;
    view.progress = null;
    ops.renderProject(context, view);
  }
}

async function importToolDxf(context, view, ops) {
  const bridge = resolveToolBridge(context);
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力。";
    ops.renderProject(context, view);
    return null;
  }
  const sourcePath = String(await bridge.openFileDialog({
    title: "选择定式模具截面 DXF",
    filters: [{ name: "DXF 二维截面", extensions: ["dxf"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  return runToolTask(context, view, ops, {
    title: "正在导入定式模具",
    detail: "正在识别闭合截面并保存到我的模具",
    stage: "DXF 截面校验",
  }, async () => {
    const imported = await context.productProxy?.invoke?.("TubeDesigner.ImportProfileDxf", { sourcePath }, { timeoutMs: 120000 });
    const profile = imported?.profile;
    if (!profile?.contours?.length) throw new Error("DXF 没有返回有效的闭合截面。");
    const saved = await context.productProxy.invoke("TubeDesigner.SavePunchTool", {
      name: profile.name ?? profile.sourceFileName ?? "定式模具",
      kind: "fixed",
      target: "side",
      category: "孔型",
      geometry: { mode: "profile", coordinateSpace: "section", contours: profile.contours },
    }, { timeoutMs: 120000 });
    const tool = saved?.tool;
    upsertUserTool(view, tool);
    ops.showNotice(context, view, `已新增定式模具“${tool.displayName ?? tool.name ?? tool.id}”。`);
    return tool;
  });
}

async function importToolPackage(context, view, ops) {
  const bridge = resolveToolBridge(context);
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力。";
    ops.renderProject(context, view);
    return null;
  }
  const sourcePath = String(await bridge.openFileDialog({
    title: "选择程式模具包",
    filters: [{ name: "程式模具包", extensions: ["itmt"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  return runToolTask(context, view, ops, {
    title: "正在导入程式模具",
    detail: "正在校验单个 JSON+Python 模具包并保存到我的模具",
    stage: "程式包校验",
  }, async () => {
    const response = await context.productProxy?.invoke?.("TubeDesigner.ImportPunchToolPackage", { sourcePath }, { timeoutMs: 120000 });
    const tool = response?.tool;
    upsertUserTool(view, tool);
    ops.showNotice(context, view, `已新增程式模具“${tool.displayName ?? tool.name ?? tool.id}”。`);
    return tool;
  });
}
