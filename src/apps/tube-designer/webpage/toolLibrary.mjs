import { renderParameterLevels } from './parameterPresentation.mjs';
import { parameterVisible } from './parameterConditions.mjs';
import { matchesParameterCondition, parameterEnabled } from "./parameterConditions.mjs";
import { parameterAutoFillPatch } from "./parameterAutoFill.mjs";
import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";

import { buildPunchPreviewRows } from "./punchEditor.mjs";
import { renderProfileSvg } from "./profileSvg.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import { libraryDiagramPositionStyle, renderDiagramResizeHandles } from './floatingParameterDiagram.mjs';
import { renderToolParameterDiagram as renderToolParameterDiagramPanel } from "./toolParameterDiagram.mjs";
import {
  isParametricProfile,
  libraryProfiles,
  profileName,
  profileRef,
  profileSelectionKey,
  profileSnapshot,
} from "./profileLibrary.mjs";

const SCOPES = [
  ["system", "系统内置"],
  ["template", "模板自带"],
  ["user", "我的"],
];
const LIBRARY_SCOPES = SCOPES.filter(([scope]) => scope !== "template");
const TYPES = [
  ["all", "全部"],
  ["programmatic", "程式"],
  ["fixed", "定式"],
];
const CATEGORIES = [
  ["all", "全部类别"],
  ["hole", "冲孔"],
  ["slot", "槽口"],
  ["end", "断面修整"],
];
const DEFAULT_TOOL_PREVIEW_LENGTH = 500;

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
    collapsed: [], catalogueStatus: "idle", error: "", parameterDrafts: {}, operationDrafts: {},
    profileKey: "", profileDrafts: {}, branchProfileKey: "", branchProfileDrafts: {}, profileSnapshots: {},
    showProfileDiagram: false, showToolDiagram: false, mainTubeCollapsed: true,
  };
  if (!LIBRARY_SCOPES.some(([scope]) => scope === state.scope)) state.scope = "system";
  if (!TYPES.some(([type]) => type === state.type)) state.type = "all";
  if (!CATEGORIES.some(([category]) => category === state.category)) state.category = "all";
  state.collapsed ??= [];
  state.parameterDrafts ??= {};
  state.operationDrafts ??= {};
  state.profileDrafts ??= {};
  state.branchProfileDrafts ??= {};
  state.profileSnapshots ??= {};
  state.profileKeysByTool ??= {};
  state.mainTubeCollapsed ??= true;
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
  if (raw.includes("孔") || raw.includes("hole")) return "hole";
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
  return String(tool?.displayName ?? tool?.name ?? tool?.toolLabel ?? tool?.id ?? "未命名单件工艺").trim() || "未命名单件工艺";
}

// Product templates and the standalone mould library share these canonical
// identities.  Keep the library's historical libraryKey private: it contains
// an empty template-id segment for system/user tools and is unsuitable for
// data persisted by a product instance.
export function toolScope(tool) {
  return scopeOf(tool);
}

export function toolCategory(tool) {
  return categoryOf(tool);
}

export function toolDisplayName(tool) {
  return toolName(tool);
}

export function toolSelectionKey(tool) {
  const scope = scopeOf(tool);
  const id = encodeURIComponent(String(tool?.id ?? ""));
  return scope === "template"
    ? `template:${encodeURIComponent(templateIdOf(tool))}:${id}`
    : `${scope}:${id}`;
}

export function toolReference(tool) {
  const scope = scopeOf(tool);
  return {
    scope,
    ...(scope === "template" ? { templateId: templateIdOf(tool) } : {}),
    id: String(tool?.id ?? ""),
    ...(tool?.version ? { version: String(tool.version) } : {}),
    ...(tool?.digest ? { digest: String(tool.digest) } : {}),
  };
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
  if (scope === "user") return "我的单件工艺";
  return "系统内置工艺";
}

function scopeLabel(scope) {
  return SCOPES.find(([value]) => value === scope)?.[1] ?? "";
}

function typeLabel(tool) {
  return typeOf(tool) === "fixed" ? "定式工艺" : "程式工艺";
}

function typeShortLabel(tool) {
  return typeOf(tool) === "fixed" ? "定式" : "程式";
}

function categoryLabel(tool) {
  return CATEGORIES.find(([category]) => category === categoryOf(tool))?.[1] ?? "孔型";
}

function toolParameterValues(view, tool) {
  const state = toolLibraryState(view);
  const key = String(tool?.libraryKey ?? keyOf(tool));
  const draft = state.parameterDrafts?.[key];
  return { ...(tool?.defaultParameters ?? {}), ...(draft ?? {}) };
}

function displayedToolParameterValues(view, tool) {
  const values = toolParameterValues(view, tool);
  const preview = toolLibraryState(view).preview;
  const measured = preview?.key === toolPreviewKey(view, tool)
    ? preview.response?.sectionAnalyses?.find(item => item.applicable === true)?.parameters : null;
  for (const field of tool?.parameters ?? []) {
    if (field.derived) values[field.key] = measured?.[field.key] ?? "";
  }
  return values;
}

function toolOperationParameterValues(view, tool) {
  const state = toolLibraryState(view);
  const key = String(tool?.libraryKey ?? keyOf(tool));
  const defaults = tool?.defaultOperationParameters
    ?? Object.fromEntries((tool?.operationParameters ?? []).map((definition) => [definition.key, definition.defaultValue]));
  return { ...defaults, ...(state.operationDrafts?.[key] ?? {}) };
}

function toolRequiresTargetSection(tool) {
  return (tool?.inputs ?? []).some((input) => input?.key === "targetSection" && input?.valueType === "profile");
}

const SIDE_CUT_INPUTS = [
  { key: "targetSection", displayName: "目标管型截面", valueType: "profile", required: true },
];
const SIDE_CUT_OPERATION_PARAMETERS = [
  { key: "blindHole", displayName: "盲孔", valueType: "boolean", defaultValue: false },
  { key: "cutDepth", displayName: "孔深", valueType: "number", defaultValue: 5, min: 0.1, max: 100000, step: 0.1, unit: "mm",
    visibleWhen: { op: "eq", parameter: "blindHole", value: true } },
  { key: "opposite", displayName: "对冲孔", valueType: "boolean", defaultValue: false },
];

function toolParameterVisible(definition, values) {
  return parameterVisible(definition, values);
}

const FALLBACK_TUBE_PROFILE = {
  id: "round", name: "圆管", libraryScope: "system", profileScope: "system",
  profileType: "profile-package", profileForm: "parametric", specification: "圆管 · 40 × 2",
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

function ensureToolTubeProfile(view, role = "main", tool = null) {
  const state = toolLibraryState(view);
  const profiles = toolTubeProfiles(view);
  const stateKey = role === "branch" ? "branchProfileKey" : "profileKey";
  // Do not commit the temporary round fallback as a tool's initial choice
  // before the asynchronous profile catalogue can resolve its declared default.
  if (role === "main" && libraryProfiles(view).length) {
    tool ??= visibleLibraryTools(view).find(item => item.libraryKey === state.selectedKey);
    const owner = tool ? (tool.libraryKey ?? keyOf(tool)) : "";
    if (owner && owner !== state.profileOwnerKey) {
      if (state.profileOwnerKey) state.profileKeysByTool[state.profileOwnerKey] = state.profileKey;
      const declared = tool.preview?.profileRef;
      const preferred = declared && profiles.find(item =>
        profileScopeOf(item) === declared.scope && String(item.id) === declared.id);
      state.profileKey = state.profileKeysByTool[owner]
        ?? (preferred ? profileSelectionKey(preferred) : state.profileKey);
      state.profileOwnerKey = owner;
    }
  }
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

export function toolTubeProfileEvaluationKey(view, profile, role = "main") {
  return JSON.stringify([
    role,
    profileSelectionKey(profile),
    profile?.packageDigest ?? profile?.descriptor?.version ?? profile?.version ?? "",
    toolTubeProfileValues(view, profile, role),
  ]);
}

function evaluatedToolTubeProfile(view, profile, role = "main") {
  const cached = toolLibraryState(view).profileSnapshots?.[role];
  return cached?.key === toolTubeProfileEvaluationKey(view, profile, role) ? cached.snapshot : null;
}

function profileParameterVisible(definition, values) {
  return parameterVisible(definition, values);
}

function renderToolTubeParameterInput(view, profile, definition, role = "main") {
  const values = toolTubeProfileValues(view, profile, role);
  const fieldDisabled = view?.pending || !parameterEnabled(definition, values);
  const key = String(definition?.key ?? "");
  if (!key) return "";
  const value = values[key] ?? definition?.defaultValue ?? "";
  const type = definition?.valueType ?? "number";
  const common = `data-profile-parameter-key="${escapeAttr(key)}" data-cam-change-action="tube-designer-tool-library-profile-parameter-change" data-tube-tool-library-profile-key="${escapeAttr(profileSelectionKey(profile))}" data-tube-tool-library-profile-role="${escapeAttr(role)}" data-tube-tool-library-profile-parameter="${escapeAttr(key)}" data-tube-profile-value-type="${escapeAttr(type)}"`;
  const label = escapeText(localizedText(definition?.displayName ?? definition?.name, key));
  if (type === "boolean") return `<label class="tube-designer-field tube-designer-boolean-field"><span>${label}</span><input type="checkbox" ${value ? "checked" : ""} ${common} ${fieldDisabled ? "disabled" : ""} /></label>`;
  const options = Array.isArray(definition?.options) ? definition.options : [];
  if (options.length) return `<label class="tube-designer-field is-choice"><span>${label}</span><select ${common} ${fieldDisabled ? "disabled" : ""}>${options.map((option) => { const optionValue = typeof option === "object" ? option.value : option; const optionLabel = typeof option === "object" ? (option.displayName ?? option.label ?? optionValue) : option; return `<option value="${escapeAttr(optionValue)}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(localizedText(optionLabel, optionValue))}</option>`; }).join("")}</select></label>`;
  const inputType = type === "string" ? "text" : "number";
  return `<label class="tube-designer-field ${inputType === "text" ? "is-string" : "is-number"}"><span>${label}</span><input type="${inputType}" value="${escapeAttr(value)}" ${definition.min != null ? `min="${escapeAttr(definition.min)}"` : ""} ${definition.max != null ? `max="${escapeAttr(definition.max)}"` : ""} ${definition.step != null ? `step="${escapeAttr(definition.step)}"` : ""} ${common} ${fieldDisabled ? "disabled" : ""} /></label>`;
}

function renderToolParameterInput(view, tool, definition) {
  const values = displayedToolParameterValues(view, tool);
  const fieldDisabled = view?.pending || definition.derived || !parameterEnabled(definition, values);
  const key = String(definition?.key ?? "");
  if (!key) return "";
  const value = definition.derived ? values[key] : (values[key] ?? definition?.defaultValue ?? "");
  // Suppress measurement round-off in the control only. Keep exact preview
  // measurements, conditions and editable drafts untouched.
  const displayValue = definition.derived && typeof value === "number" && Number.isFinite(value)
    ? formatNumber(value, 6) : value;
  const type = definition?.valueType ?? "number";
  const common = `data-tool-parameter-key="${escapeAttr(key)}" data-cam-change-action="tube-designer-tool-library-parameter-change" data-tube-tool-library-key="${escapeAttr(tool.libraryKey)}" data-tube-tool-library-parameter="${escapeAttr(key)}"`;
  if (type === "boolean") return `<label class="tube-designer-field tube-designer-boolean-field"><span>${escapeText(localizedText(definition.displayName, key))}</span><input type="checkbox" ${value ? "checked" : ""} ${common} ${fieldDisabled ? "disabled" : ""} /></label>`;
  if (type === "string" && Array.isArray(definition?.options)) {
    return `<label class="tube-designer-field is-choice"><span>${escapeText(localizedText(definition.displayName, key))}</span><select ${common} ${fieldDisabled ? "disabled" : ""}>${definition.options.map((option) => `<option value="${escapeAttr(option.value)}" ${String(option.value) === String(value) ? "selected" : ""}>${escapeText(localizedText(option.label ?? option.displayName, option.value))}</option>`).join("")}</select></label>`;
  }
  return `<label class="tube-designer-field ${type === "string" ? "is-string" : "is-number"}"><span>${escapeText(localizedText(definition.displayName, key))}</span><input type="${type === "string" ? "text" : "number"}" value="${escapeAttr(displayValue)}" ${definition.min !== undefined ? `min="${escapeAttr(definition.min)}"` : ""} ${definition.max !== undefined ? `max="${escapeAttr(definition.max)}"` : ""} ${definition.step !== undefined ? `step="${escapeAttr(definition.step)}"` : ""} ${common} ${fieldDisabled ? "disabled" : ""} /></label>`;
}

function renderToolOperationParameterInput(view, tool, definition) {
  const values = toolOperationParameterValues(view, tool);
  const key = String(definition?.key ?? "");
  if (!key) return "";
  const value = values[key] ?? definition?.defaultValue ?? "";
  const type = definition?.valueType ?? "number";
  const disabled = view?.pending || !parameterEnabled(definition, values);
  const common = `data-operation-parameter-key="${escapeAttr(key)}" data-cam-change-action="tube-designer-tool-library-operation-parameter-change" data-tube-tool-library-key="${escapeAttr(tool.libraryKey)}" data-tube-tool-library-operation-parameter="${escapeAttr(key)}"`;
  const label = escapeText(localizedText(definition.displayName, key));
  if (type === "boolean") return `<label class="tube-designer-field tube-designer-boolean-field"><span>${label}</span><input type="checkbox" ${value ? "checked" : ""} ${common} ${disabled ? "disabled" : ""} /></label>`;
  if (type === "string" && Array.isArray(definition?.options)) return `<label class="tube-designer-field is-choice"><span>${label}</span><select ${common} ${disabled ? "disabled" : ""}>${definition.options.map((option) => `<option value="${escapeAttr(option.value)}" ${String(option.value) === String(value) ? "selected" : ""}>${escapeText(localizedText(option.label ?? option.displayName, option.value))}</option>`).join("")}</select></label>`;
  return `<label class="tube-designer-field is-number"><span>${label}</span><input type="number" value="${escapeAttr(value)}" ${definition.min !== undefined ? `min="${escapeAttr(definition.min)}"` : ""} ${definition.max !== undefined ? `max="${escapeAttr(definition.max)}"` : ""} ${definition.step !== undefined ? `step="${escapeAttr(definition.step)}"` : ""} ${common} ${disabled ? "disabled" : ""} /></label>`;
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
  if (tool?.requiresSection && tool?.target === "part") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M24 38 V14 M24 14 C24 8 39 8 39 14 V38" /></svg>`;
  if (category === "end") return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M10 10 H38 V38 H10 Z M10 24 H38" /></svg>`;
  return `<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M10 24 H38 M24 10 V38" /></svg>`;
}

function renderToolParameterDiagram(view, tool, values, definitions) {
  const diagramValues = { ...values };
  for (const definition of definitions) {
    if (diagramValues[definition.key] === undefined && definition.defaultValue !== undefined) {
      diagramValues[definition.key] = definition.defaultValue;
    }
  }
  return renderToolParameterDiagramPanel({ tool, values: diagramValues, definitions, expanded: true, showToggle: false, fallbackSvg: renderToolIllustration(tool) });
}

function renderToolTubeSection(view, role = "main") {
  const isBranch = role === "branch";
  const profiles = toolTubeProfiles(view);
  const profile = ensureToolTubeProfile(view, role);
  const selectedKey = profileSelectionKey(profile);
  const values = toolTubeProfileValues(view, profile, role);
  const definitions = isParametricProfile(profile) && Array.isArray(profile?.descriptor?.parameters)
    ? profile.descriptor.parameters.filter((definition) => profileParameterVisible(definition, values)) : [];
  const options = profiles.map((item) => `<option value="${escapeAttr(profileSelectionKey(item))}" ${profileSelectionKey(item) === selectedKey ? "selected" : ""}>${escapeText(profileName(item))}${profileScopeOf(item) === "template" ? " · 模板" : profileScopeOf(item) === "user" ? " · 我的" : " · 系统"}</option>`).join("");
  const state = toolLibraryState(view);
  const expanded = isBranch || !state.mainTubeCollapsed;
  const title = isBranch ? "刀具截面" : "目标管型";
  const fieldLabel = isBranch ? "截面管型" : "目标管型";
  return `<section class="tube-profile-library-parameter-group tube-tool-library-tube-section${isBranch ? " tube-tool-library-branch-section" : ""}" data-profile-parameter-scope data-parameter-diagram-owner="tool-library-profile:${escapeAttr(role)}" data-tube-tool-library-tube-editor data-tube-tool-library-profile-role="${escapeAttr(role)}">
    ${isBranch ? `<div class="tube-tool-library-tube-summary"><strong>${title}</strong></div>` : `<button type="button" class="tube-tool-library-tube-summary" data-cam-action="tube-designer-tool-library-toggle-main-tube" aria-expanded="${expanded}" aria-controls="tube-tool-library-main-tube-content"><strong>${expanded ? "▾" : "▸"} ${title}</strong></button>`}
    <div class="tube-tool-library-tube-content" ${isBranch ? "" : 'id="tube-tool-library-main-tube-content"'} ${expanded ? "" : "hidden"}>
    <div class="tube-tool-library-subsection-actions"><button type="button" class="tube-profile-library-diagram-toggle" data-cam-action="tube-designer-tool-library-toggle-diagram" data-tube-tool-library-diagram="profile" aria-expanded="${!!state.showProfileDiagram}" aria-controls="tube-tool-scene-profile">显示${isBranch ? "刀具截面" : "目标管型"}示意图</button></div>
    <div class="tube-profile-library-field-grid">
      <label class="tube-designer-field is-choice is-line-full"><span>${fieldLabel}</span><select data-cam-change-action="tube-designer-tool-library-profile-change" data-tube-tool-library-profile-role="${escapeAttr(role)}" ${view?.pending ? "disabled" : ""}>${options || `<option>暂无可用管型</option>`}</select></label>
      ${definitions.length ? renderParameterLevels(definitions, definition => renderToolTubeParameterInput(view, profile, definition, role), {key:`tool-profile:${role}:${profile.id}`,gridClass:"tube-profile-library-field-grid"}) : `<p class="tube-tool-library-diagram-message is-line-full">当前管型为固定截面；工艺预览会直接使用它的实际轮廓。</p>`}
    </div>
    </div>
  </section>`;
}

function emptyText(state) {
  if (state.search) return ["没有匹配的单件工艺", "请调整搜索内容。"];
  if (state.catalogueStatus === "loading") return ["正在读取单件工艺目录", "正在同步系统内置和我的单件工艺，请稍候。"];
  return {
    system: ["没有可用的系统单件工艺", "请检查内置单件工艺资源是否完整。"],
    user: ["还没有我的单件工艺", "可从单件工艺库导入或保存自定义工艺。"],
  }[state.scope];
}

export function renderToolLibraryLeftPane(_context, view) {
  const state = toolLibraryState(view);
  const all = libraryTools(view).filter((tool) => LIBRARY_SCOPES.some(([scope]) => scope === scopeOf(tool)));
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
    <div class="tube-designer-heading tube-tool-library-heading"><div><strong>单件工艺库</strong><span>${all.length} 项工艺 · 当前显示 ${tools.length} 项</span></div></div>
    <div class="tube-component-library-filters tube-tool-library-filters">
      <input type="search" aria-label="搜索单件工艺" placeholder="搜索名称、类别" value="${escapeAttr(state.search)}" data-cam-change-action="tube-designer-tool-library-search" />
      <div class="tube-tool-library-tab-row" role="tablist" aria-label="单件工艺来源">${LIBRARY_SCOPES.map(([scope, label]) => `<button type="button" role="tab" aria-selected="${scope === state.scope}" class="${scope === state.scope ? "selected" : ""}" data-cam-action="tube-designer-tool-library-scope" data-tube-tool-library-scope="${scope}" ${view?.pending ? "disabled" : ""}>${label}<small>${all.filter((tool) => scopeOf(tool) === scope).length}</small></button>`).join("")}</div>
    </div>
    <div class="tube-tool-library-list" role="tabpanel">
      ${state.error ? `<div class="tube-tool-library-error" role="alert"><strong>单件工艺目录读取失败</strong><span>${escapeText(state.error)}</span><button type="button" class="tube-designer-secondary" data-cam-action="tools.refresh" ${view?.pending ? "disabled" : ""}>重新读取</button></div>` : ""}
      ${tools.length ? groups.map(renderGroup).join("") : `<div class="tube-tool-library-empty"><strong>${escapeText(emptyTitle)}</strong><span>${escapeText(emptyNote)}</span></div>`}
    </div>
  </div>`;
}

export function renderToolLibraryRightPane(_context, view) {
  const tools = visibleLibraryTools(view);
  const selectedKey = ensureSelection(view, tools);
  const tool = tools.find((item) => item.libraryKey === selectedKey);
  if (!tool) return `<div class="tube-designer-panel tube-tool-library-editor-empty"><div class="tube-designer-heading"><strong>单件工艺属性</strong><span>尚未选择工艺</span></div><div class="tube-designer-empty">从左侧选择单件工艺，查看其来源、类型和预览规则。</div></div>`;
  const allParameters = Array.isArray(tool.parameters) ? tool.parameters : [];
  const values = displayedToolParameterValues(view, tool);
  const operationValues = toolOperationParameterValues(view, tool);
  const operationParameters = (tool?.operationParameters ?? []).filter((parameter) => toolParameterVisible(parameter, operationValues));
  // A branch mould's angle/azimuth/roll values describe how the punch is
  // placed in the manufacturing workflow.  They are intentionally not
  // edited in the resource library.  The library owns the branch section;
  // placement remains in the punch wizard where it can be validated against
  // the selected operation.
  const branchTool = tool?.target === "part" && tool?.requiresSection;
  const parameters = branchTool ? [] : allParameters.filter((parameter) => toolParameterVisible(parameter, values));
  const programmatic = typeOf(tool) === "programmatic";
  return `<div class="tube-designer-panel tube-tool-library-editor tube-profile-library-editor">
    <div class="tube-tool-library-editor-body">
      <section class="tube-profile-library-parameter-section tube-tool-library-parameter-section" data-tool-parameter-scope data-parameter-diagram-owner="tool-library-mould">
        <header><div><strong>工艺参数</strong><span>${escapeText(toolName(tool))} · 修改后自动更新场景</span></div><button type="button" class="tube-profile-library-diagram-toggle" data-cam-action="tube-designer-tool-library-toggle-diagram" data-tube-tool-library-diagram="tool" aria-expanded="${!!toolLibraryState(view).showToolDiagram}" aria-controls="tube-tool-scene-tool">显示工艺示意图</button></header>
        <div class="tube-profile-library-parameter-list">
          ${branchTool ? renderToolTubeSection(view, "branch") : parameters.length || operationParameters.length ? `<div class="tube-profile-library-field-grid">${programmatic ? renderParameterLevels(parameters, definition => renderToolParameterInput(view, tool, definition), {key:`tool:${tool.id}`,gridClass:"tube-profile-library-field-grid"}) : ""}${renderParameterLevels(operationParameters, definition => renderToolOperationParameterInput(view, tool, definition), {key:`tool-operation:${tool.id}`,gridClass:"tube-profile-library-field-grid"})}</div>` : `<p class="tube-profile-library-frozen-note">当前为定式工艺，参数由工艺定义固定。</p>`}
        </div>
        ${tool.requiresSection && !branchTool ? renderToolTubeSection(view, "branch") : ""}
      </section>
      ${toolRequiresTargetSection(tool) || branchTool ? renderToolTubeSection(view) : ""}
    </div>
  </div>`;
}

export function renderToolLibraryViewportOverlay(_context, view) {
  const tools = visibleLibraryTools(view);
  const key = ensureSelection(view, tools);
  const tool = tools.find((item) => item.libraryKey === key);
  if (tool) ensureToolLibraryPreview(_context, view, tool);
  const state = toolLibraryState(view);
  const branchTool = !!tool?.requiresSection;
  const values = tool ? displayedToolParameterValues(view, tool) : {};
  const definitions = (tool?.parameters ?? []).filter((field) => toolParameterVisible(field, values));
  const dock = tool ? `<aside class="tube-library-diagram-dock" data-tube-tool-diagram-dock data-library-floating-diagram="tools" ${state.showToolDiagram||state.showProfileDiagram?'':'hidden'} style="${libraryDiagramPositionStyle(view,'tools')}">
    ${renderDiagramResizeHandles()}
    <header class="tube-library-diagram-drag" data-floating-diagram-drag><strong>参数示意图</strong><button type="button" data-library-diagram-close data-cam-action="tube-designer-tool-library-close-diagram" aria-label="关闭示意图">×</button></header>
    <div id="tube-tool-scene-tool" class="tube-library-diagram-content" role="tabpanel" data-tool-scene-diagram="tool" data-parameter-diagram-for="tool-library-mould" ${state.showToolDiagram ? "" : "hidden"}>${renderToolParameterDiagram(view, tool, values, definitions)}</div>
    <div id="tube-tool-scene-profile" class="tube-library-diagram-content" role="tabpanel" data-tool-scene-diagram="profile" ${state.showProfileDiagram ? "" : "hidden"}>${renderToolTubeDiagram(view, "main")}${branchTool ? renderToolTubeDiagram(view, "branch") : ""}</div>
  </aside>` : "";
  return `${dock}<div class="tube-tool-library-hud"><strong>${escapeText(tool ? toolName(tool) : "单件工艺库")}</strong><span>${escapeText(tool ? `${typeShortLabel(tool)} · ${sourceLabel(tool)}` : "选择左侧工艺查看定义")}</span><small>${tool ? (state.previewRequest ? "正在生成工艺预览…" : "主管 + 工艺作用体 · 拖动旋转 · 滚轮缩放") : "选择左侧工艺查看定义"}</small>${tool && state.previewRequest ? '<div class="tube-tool-library-preview-progress" role="progressbar" aria-label="正在生成工艺预览"><i></i></div>' : ""}${state.previewError ? `<p role="alert">${escapeText(state.previewError)}</p><button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-tool-library-retry-preview">重新预览</button>` : ""}</div>`;
}

function renderToolTubeDiagram(view, role) {
  const profile = ensureToolTubeProfile(view, role);
  const base = profileSnapshot(profile);
  const evaluated = evaluatedToolTubeProfile(view, profile, role);
  const snapshot = base || evaluated ? {
    ...(base ?? {}),
    ...(evaluated ?? {}),
    parameterDiagram: evaluated?.parameterDiagram ?? base?.parameterDiagram ?? profile?.descriptor?.parameterDiagram,
  } : null;
  const definitions = profile?.descriptor?.parameters ?? [];
  return `<div data-parameter-diagram-for="tool-library-profile:${escapeAttr(role)}">${definitions.length
    ? renderProfileParameterDiagram(snapshot, { definitions, parameters: toolTubeProfileValues(view, profile, role), compact: true, title: role === "branch" ? "刀具截面示意图" : "目标管型示意图" })
    : `<section class="td-profile-parameter-diagram"><header><strong>${role === "branch" ? "支管" : "主管"}示意图</strong></header>${snapshot ? renderProfileSvg(snapshot) : ""}</section>`}</div>`;
}

export function toolPreviewKey(view, tool) {
  // A catalogue refresh may keep the same library key while replacing the
  // executable package. Include its identity so an old extrusion can never
  // survive a version/digest change.
  const profile = ensureToolTubeProfile(view, "main", tool);
  const branchTool = !!tool?.requiresSection;
  const branchProfile = branchTool ? ensureToolTubeProfile(view, "branch") : null;
  return JSON.stringify([
    tool?.libraryKey ?? "", tool?.version ?? "", tool?.digest ?? "", toolParameterValues(view, tool), toolOperationParameterValues(view, tool),
    profileSelectionKey(profile), profile?.packageDigest ?? profile?.descriptor?.version ?? profile?.version ?? "",
    toolTubeProfileValues(view, profile), DEFAULT_TOOL_PREVIEW_LENGTH,
    branchProfile ? profileSelectionKey(branchProfile) : "",
    branchProfile?.packageDigest ?? branchProfile?.descriptor?.version ?? branchProfile?.version ?? "",
    branchProfile ? toolTubeProfileValues(view, branchProfile, "branch") : {},
  ]);
}

export function buildToolLibraryPreviewPayload(view, tool, options = {}) {
  const values = toolParameterValues(view, tool);
  const operationValues = toolOperationParameterValues(view, tool);
  const profile = ensureToolTubeProfile(view, "main", tool);
  const profileValues = toolTubeProfileValues(view, profile);
  const profileSnapshotValue = options.profileSnapshot ?? evaluatedToolTubeProfile(view, profile)
    ?? profileSnapshot(profile) ?? FALLBACK_TUBE_PROFILE.previewProfile;
  const branchTool = !!tool?.requiresSection;
  const branchProfile = branchTool ? ensureToolTubeProfile(view, "branch") : null;
  const branchProfileValues = branchProfile ? toolTubeProfileValues(view, branchProfile, "branch") : {};
  const branchProfileSnapshot = branchTool
    ? (options.branchProfileSnapshot ?? profileSnapshot(branchProfile) ?? profileSnapshotValue)
    : profileSnapshotValue;
  const toolRef = { id: String(tool.id), version: String(tool.version ?? "") };
  if (tool.digest) toolRef.digest = String(tool.digest);
  if (scopeOf(tool) !== "system") toolRef.libraryScope = scopeOf(tool);
  const base = { profileRef: profileRef(profile), parameters: profileValues, length: DEFAULT_TOOL_PREVIEW_LENGTH, features: [], ends: { start: { type: "keep" }, end: { type: "keep" } }, toolsOnly: true };
  if (tool.target === "end") {
    base.ends.start = { type: String(tool.id), toolRef, toolParameters: values, trim: 0, datum: "long", rotation: 0, ...operationValues };
    if (tool.requiresSection) {
      base.ends.start.section = { profile: branchProfileSnapshot, profileRef: profileRef(branchProfile), parameters: branchProfileValues };
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
    blindHole: !!operationValues.blindHole, cutDepth: Number(operationValues.cutDepth ?? 0), opposite: !!operationValues.opposite };
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
  const previewError = response.resultError || (response.previewToolsComplete === false ? "工艺作用体未完整生成。"
    : !rows.length || !toolRows.length ? "单件工艺预览没有返回主管和工艺作用体。" : "");
  if (previewError) {
    // Replace the previous tool with the current blank, so a failed selection
    // never leaves another tool's geometry looking like the current result.
    const resources = context.sceneProxy?.resources ?? view.sceneProxy?.resources ?? context.projectProxy?.resources;
    const blankRows = rows.filter(row => row.entityId === "punch-preview-blank");
    const receipt = await view.viewport.applyViewSnapshot({ revision: `tool-library-error:${key}`, rows: blankRows }, resources);
    if (view.activeAreaId !== "tools" || toolPreviewKey(view, tool) !== key || request !== toolLibraryState(view).previewRequest) return;
    view.viewport.setVisibleEntityIds?.(receipt?.entityIds ?? blankRows.map(row => row.entityId));
    toolLibraryState(view).preview = null;
    view.preserveCustomViewportEntities = true;
    throw new Error(previewError);
  }
  const revision = `tool-library:${key}`;
  const resources = context.sceneProxy?.resources ?? view.sceneProxy?.resources ?? context.projectProxy?.resources;
  const receipt = await view.viewport.applyViewSnapshot({ revision, rows }, resources);
  if (!receipt?.applied || receipt.missingGeometryEntityIds?.length) throw new Error("单件工艺预览几何未完整进入视口。");
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
      const profile = ensureToolTubeProfile(view, "main");
      const branchProfile = tool?.requiresSection
        ? ensureToolTubeProfile(view, "branch") : null;
      const resolveProfile = async (candidate, role) => {
        if (!candidate) return null;
        const evaluationKey = toolTubeProfileEvaluationKey(view, candidate, role);
        const cached = state.profileSnapshots?.[role];
        const snapshot = cached?.key === evaluationKey
          ? cached.snapshot : await resolveToolProfileSnapshot(context, view, candidate, role);
        if (state.previewRequest === request && toolTubeProfileEvaluationKey(view, candidate, role) === evaluationKey) {
          state.profileSnapshots[role] = { key: evaluationKey, snapshot };
        }
        return snapshot;
      };
      const [profileSnapshotValue, branchProfileSnapshot] = await Promise.all([
        resolveProfile(profile, "main"),
        resolveProfile(branchProfile, "branch"),
      ]);
      return context.sceneProxy.invoke("TubeDesigner.PreviewPunchWizard",
        buildToolLibraryPreviewPayload(view, tool, { profileSnapshot: profileSnapshotValue, branchProfileSnapshot }), { timeoutMs: 120000 });
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
      state.previewError = `单件工艺预览失败：${error?.message ?? error}`;
      if (view.activeAreaId === "tools") view.tubeDesignerToolLibraryRenderProject?.();
    });
  state.previewRequest = request;
}

export function ensureToolLibraryCatalogue(context, view, ops, options = {}) {
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
      if (!options.skipStartupGates) {
        const sceneSynchronization = view.tubeDesignerSynchronizationPromise;
        if (sceneSynchronization) {
          try { await sceneSynchronization; } catch (_) { /* catalogue can retry */ }
        }
      }
      // ListUserData is explicitly gated behind the initial scene refresh in
      // entry.mjs. Waiting for that shared promise also serializes this
      // catalogue call with the embedded Python host and prevents the
      // intermittent GetPunchTools timeout seen on startup.
      if (!options.skipStartupGates) {
        const userDataSynchronization = view.tubeDesignerUserDataSynchronizationPromise;
        if (userDataSynchronization) {
          try { await userDataSynchronization; } catch (_) { /* retry below */ }
        }
        const userDataRefresh = view.tubeDesignerUserDataRefreshPromise;
        if (userDataRefresh) {
          try { await userDataRefresh; } catch (_) { /* catalogue is independent */ }
        }
      }
      const response = await context.sceneProxy.invoke("TubeDesigner.GetPunchTools", {}, { timeoutMs: 30000 });
      const tools = Array.isArray(response?.tools) ? response.tools : [];
      if (!tools.length) {
        const diagnostics = Array.isArray(response?.errors) ? response.errors.filter(Boolean).join("；") : "";
        throw new Error(diagnostics || "单件工艺目录为空，请检查内置工艺资源是否完整。");
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
  if (action === "tube-designer-tool-library-toggle-main-tube") {
    state.mainTubeCollapsed = !state.mainTubeCollapsed;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-toggle-diagram" || action === "tube-designer-tool-library-close-diagram") {
    {
      const kind = String(target?.dataset?.tubeToolLibraryDiagram ?? "");
      if (action === "tube-designer-tool-library-close-diagram") { state.showProfileDiagram = false; state.showToolDiagram = false; }
      else if (kind === "profile") { state.showProfileDiagram = true; state.showToolDiagram = false; }
      else if (kind === "tool") { state.showToolDiagram = true; state.showProfileDiagram = false; }
      else return { handled: true };
      const dock = context.mount?.querySelector("[data-tube-tool-diagram-dock]");
      if (dock) {
        dock.hidden=!state.showToolDiagram&&!state.showProfileDiagram;
        for (const panel of dock.querySelectorAll("[data-tool-scene-diagram]")) panel.hidden = panel.dataset.toolSceneDiagram === "tool" ? !state.showToolDiagram : !state.showProfileDiagram;
        for (const button of context.mount.querySelectorAll("[data-tube-tool-library-diagram]")) {
          const open = button.dataset.tubeToolLibraryDiagram === "tool" ? state.showToolDiagram : state.showProfileDiagram;
          button.setAttribute("aria-expanded", String(!!open));
        }
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-scope") {
    if (!view.pending) {
      const scope = String(target?.dataset?.tubeToolLibraryScope ?? "");
      if (LIBRARY_SCOPES.some(([value]) => value === scope)) { const hadPreview = !!state.preview; state.scope = scope; state.selectedKey = ""; state.showToolDiagram = false; state.preview = null; state.previewError = ""; state.previewFailureKey = ""; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview; ops.renderProject(context, view); }
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
      if (tool && definition && !definition.derived && parameterEnabled(definition, toolParameterValues(view, tool))) {
        const hadPreview = !!state.preview;
        const value = definition.valueType === "boolean" ? !!target.checked
          : definition.valueType === "string" ? String(target.value ?? "") : Number(target.value);
        const displayed = displayedToolParameterValues(view, tool);
        const profile = ensureToolTubeProfile(view, "main", tool);
        const sources = { ...toolTubeProfileValues(view, profile, "main") };
        for (const candidate of tool.parameters ?? []) {
          if (candidate.derived && displayed[candidate.key] !== "" && displayed[candidate.key] != null) {
            sources[candidate.key] = displayed[candidate.key];
          }
        }
        const patch = parameterAutoFillPatch(tool.parameters, displayed, parameter, value, sources);
        state.parameterDrafts[key] = { ...(state.parameterDrafts[key] ?? {}), ...patch };
        state.previewError = ""; state.previewFailureKey = ""; state.preview = null; state.previewApplied = hadPreview; view.preserveCustomViewportEntities = hadPreview;
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-tool-library-operation-parameter-change") {
    if (!view.pending) {
      const key = String(target?.dataset?.tubeToolLibraryKey ?? "");
      const parameter = String(target?.dataset?.tubeToolLibraryOperationParameter ?? "");
      const tool = libraryTools(view).find((item) => item.libraryKey === key);
      const definition = tool?.operationParameters?.find((item) => item?.key === parameter);
      if (tool && definition) {
        const hadPreview = !!state.preview;
        const value = definition.valueType === "boolean" ? !!target.checked
          : definition.valueType === "string" ? String(target.value ?? "") : Number(target.value);
        if (typeof value === "number" && !Number.isFinite(value)) return { handled: true };
        state.operationDrafts[key] = { ...(state.operationDrafts[key] ?? {}), [parameter]: value };
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
  if (!tool?.id) throw new Error("保存单件工艺后没有返回记录标识。");
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
    title: "选择定式工艺截面 DXF",
    filters: [{ name: "DXF 二维截面", extensions: ["dxf"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  return runToolTask(context, view, ops, {
    title: "正在导入定式工艺",
    detail: "正在识别闭合截面并保存到我的单件工艺",
    stage: "DXF 截面校验",
  }, async () => {
    const imported = await context.productProxy?.invoke?.("TubeDesigner.ImportProfileDxf", { sourcePath }, { timeoutMs: 120000 });
    const profile = imported?.profile;
    if (!profile?.contours?.length) throw new Error("DXF 没有返回有效的闭合截面。");
    const saved = await context.productProxy.invoke("TubeDesigner.SavePunchTool", {
      name: profile.name ?? profile.sourceFileName ?? "定式工艺",
      kind: "fixed",
      target: "side",
      category: "孔型",
      descriptor: { inputs: SIDE_CUT_INPUTS, operationParameters: SIDE_CUT_OPERATION_PARAMETERS },
      geometry: { mode: "profile", coordinateSpace: "section", contours: profile.contours },
    }, { timeoutMs: 120000 });
    const tool = saved?.tool;
    upsertUserTool(view, tool);
    ops.showNotice(context, view, `已新增定式工艺“${tool.displayName ?? tool.name ?? tool.id}”。`);
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
    title: "选择程式工艺包",
    filters: [{ name: "程式工艺包", extensions: ["itmt"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  return runToolTask(context, view, ops, {
    title: "正在导入程式工艺",
    detail: "正在校验单个 JSON+Python 工艺包并保存到我的单件工艺",
    stage: "程式包校验",
  }, async () => {
    const response = await context.productProxy?.invoke?.("TubeDesigner.ImportPunchToolPackage", { sourcePath }, { timeoutMs: 120000 });
    const tool = response?.tool;
    upsertUserTool(view, tool);
    ops.showNotice(context, view, `已新增程式工艺“${tool.displayName ?? tool.name ?? tool.id}”。`);
    return tool;
  });
}
