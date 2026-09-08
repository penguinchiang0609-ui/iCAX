import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { catalogText } from "./productCatalog.mjs";
import { libraryProfiles, profileRef, profileScope, profileSelectionKey } from "./profileLibrary.mjs";
import { beginNewSectionSketch, beginProfileSectionSketch } from "./sketchArea.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";

const PREFIX = "tube-designer-component-";
const SCOPES = ["system", "template", "user"];
const EDITABLE_FIELDS = ["name", "category", "sourcing", "material", "description"];
const MODEL_ICON = '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M16 3 28 10v13L16 30 4 23V10Z M4 10l12 7 12-7 M16 17v13" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>';
const CSG_PRIMITIVES = {
  extrusion: { label: "拉伸体", glyph: "▱", parameters: { height: 30 } },
  box: { label: "长方体", glyph: "▰", parameters: { width: 40, depth: 40, height: 20 } },
  cylinder: { label: "圆柱体", glyph: "◉", parameters: { radius: 15, height: 30 } },
  sphere: { label: "球体", glyph: "●", parameters: { radius: 18 } },
  cone: { label: "圆锥体", glyph: "▲", parameters: { bottomRadius: 18, topRadius: 6, height: 30 } },
};
const CSG_OPERATIONS = { base: "基础体", union: "并集", difference: "差集", intersection: "交集" };
const CSG_OPERATION_SYMBOLS = { union: "∪", difference: "−", intersection: "∩" };
const CSG_TOOLS = {
  view: { label: "查看", hint: "拖动旋转视角，滚轮缩放" },
  move: { label: "移动", hint: "拖动：左右改 X、上下改 Z；按住 Shift 左右改 Y" },
  rotate: { label: "旋转", hint: "拖动：左右绕 Z、上下绕 X；按住 Shift 左右绕 Y" },
  scale: { label: "缩放", hint: "向右或向上拖动放大，反向拖动缩小" },
};
const csgViewportControllers = new WeakMap();

const CSG_BUILTIN_PROFILES = {
  "builtin:solid-rectangle": {
    name: "实心矩形", parameters: [
      { key: "width", displayName: "宽度", valueType: "number", defaultValue: 40, min: .01, step: 1 },
      { key: "depth", displayName: "高度", valueType: "number", defaultValue: 30, min: .01, step: 1 },
    ],
    build(values) {
      const width = positiveCSGProfileNumber(values.width, "宽度");
      const depth = positiveCSGProfileNumber(values.depth, "高度");
      return csgProfileSnapshot("实心矩形", width, depth, [{ kind: "polygon", points: [
        [-width / 2, -depth / 2], [width / 2, -depth / 2],
        [width / 2, depth / 2], [-width / 2, depth / 2],
      ] }], values, this.parameters, [
        { parameter: "width", kind: "linear", axis: "x", side: "bottom", from: [-width / 2, -depth / 2], to: [width / 2, -depth / 2], description: "矩形截面左右外边之间的宽度" },
        { parameter: "depth", kind: "linear", axis: "y", side: "left", from: [-width / 2, -depth / 2], to: [-width / 2, depth / 2], description: "矩形截面上下外边之间的高度" },
      ]);
    },
  },
  "builtin:solid-circle": {
    name: "实心圆", parameters: [
      { key: "diameter", displayName: "直径", valueType: "number", defaultValue: 40, min: .01, step: 1 },
    ],
    build(values) {
      const diameter = positiveCSGProfileNumber(values.diameter, "直径");
      return csgProfileSnapshot("实心圆", diameter, diameter,
        [{ kind: "circle", radius: diameter / 2 }], values, this.parameters, [
          { parameter: "diameter", kind: "linear", axis: "x", side: "bottom", from: [-diameter / 2, 0], to: [diameter / 2, 0], description: "穿过圆心的截面直径" },
        ]);
    },
  },
  "builtin:ring": {
    name: "圆环", parameters: [
      { key: "outerDiameter", displayName: "外径", valueType: "number", defaultValue: 40, min: .01, step: 1 },
      { key: "innerDiameter", displayName: "内径", valueType: "number", defaultValue: 24, min: .01, step: 1 },
    ],
    build(values) {
      const outer = positiveCSGProfileNumber(values.outerDiameter, "外径");
      const inner = positiveCSGProfileNumber(values.innerDiameter, "内径");
      if (inner >= outer) throw new Error("圆环内径必须小于外径。");
      return csgProfileSnapshot("圆环", outer, outer,
        [{ kind: "circle", radius: outer / 2 }, { kind: "circle", radius: inner / 2 }],
        values, this.parameters, [
          { parameter: "outerDiameter", kind: "linear", axis: "x", side: "bottom", from: [-outer / 2, 0], to: [outer / 2, 0], description: "圆环外圆的直径" },
          { parameter: "innerDiameter", kind: "linear", axis: "x", side: "top", from: [-inner / 2, 0], to: [inner / 2, 0], description: "圆环内部通孔的直径" },
        ]);
    },
  },
  "builtin:l-section": {
    name: "L 形", parameters: [
      { key: "width", displayName: "宽度", valueType: "number", defaultValue: 40, min: .01, step: 1 },
      { key: "depth", displayName: "高度", valueType: "number", defaultValue: 40, min: .01, step: 1 },
      { key: "thickness", displayName: "厚度", valueType: "number", defaultValue: 6, min: .01, step: .5 },
    ],
    build(values) {
      const width = positiveCSGProfileNumber(values.width, "宽度");
      const depth = positiveCSGProfileNumber(values.depth, "高度");
      const thickness = positiveCSGProfileNumber(values.thickness, "厚度");
      if (thickness >= Math.min(width, depth)) throw new Error("L 形厚度必须小于宽度和高度。");
      return csgProfileSnapshot("L 形", width, depth, [{ kind: "polygon", points: [
        [-width / 2, -depth / 2], [width / 2, -depth / 2],
        [width / 2, -depth / 2 + thickness], [-width / 2 + thickness, -depth / 2 + thickness],
        [-width / 2 + thickness, depth / 2], [-width / 2, depth / 2],
      ] }], values, this.parameters, [
        { parameter: "width", kind: "linear", axis: "x", side: "bottom", from: [-width / 2, -depth / 2], to: [width / 2, -depth / 2], description: "L 形截面横肢的外宽" },
        { parameter: "depth", kind: "linear", axis: "y", side: "left", from: [-width / 2, -depth / 2], to: [-width / 2, depth / 2], description: "L 形截面竖肢的外高" },
        { parameter: "thickness", kind: "linear", axis: "y", side: "right", from: [width / 2, -depth / 2], to: [width / 2, -depth / 2 + thickness], description: "横肢和竖肢共用的壁厚" },
      ]);
    },
  },
};

function cloneValue(value) { return value == null ? value : JSON.parse(JSON.stringify(value)); }
function csgNumber(value, fallback = 0) { const number = Number(value); return Number.isFinite(number) ? number : fallback; }
function positiveCSGProfileNumber(value, label) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0 || number > 100000) throw new Error(`${label}必须在 0 到 100000 mm 之间。`);
  return number;
}
function csgProfileSnapshot(name, width, depth, contours, parameters, parameterDefinitions, annotations) {
  return {
    schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "parametric-package",
    name, sourceFileName: "内置程式截面", sourceFormat: "icax.component-csg-profile",
    width, depth, wallThickness: 0, cornerRadius: 0,
    specification: `${formatNumber(width)} × ${formatNumber(depth)} mm`,
    hollow: contours.length > 1, contourCount: contours.length, contours,
    parameters: cloneValue(parameters), parameterDefinitions: cloneValue(parameterDefinitions),
    parameterDiagram: { schemaVersion: 1, annotations },
    editableParameters: true, frozenGeometry: false, contentDigest: "icax.component-csg-profile:1",
  };
}
function builtinCSGProfile(key, values = null) {
  const definition = CSG_BUILTIN_PROFILES[key] ?? CSG_BUILTIN_PROFILES["builtin:solid-rectangle"];
  const parameters = Object.fromEntries(definition.parameters.map((item) => [item.key,
    values?.[item.key] ?? item.defaultValue]));
  return {
    sourceType: "parametric", key: Object.entries(CSG_BUILTIN_PROFILES).find(([, item]) => item === definition)?.[0]
      ?? "builtin:solid-rectangle",
    name: definition.name, parameters, parameterDefinitions: cloneValue(definition.parameters),
    snapshot: definition.build(parameters),
  };
}
function normalizeCSGProfile(profile) {
  if (!profile?.snapshot?.contours?.length) return builtinCSGProfile("builtin:solid-rectangle");
  const result = cloneValue(profile);
  result.sourceType = result.sourceType === "fixed" ? "fixed" : "parametric";
  result.key = String(result.key || "embedded:profile");
  result.name = String(result.name || result.snapshot.name || "截面");
  result.parameters = result.parameters && typeof result.parameters === "object" ? result.parameters : {};
  result.parameterDefinitions = Array.isArray(result.parameterDefinitions)
    ? result.parameterDefinitions : (Array.isArray(result.snapshot.parameterDefinitions) ? result.snapshot.parameterDefinitions : []);
  if (result.sourceType === "parametric" && !result.snapshot.parameterDiagram && CSG_BUILTIN_PROFILES[result.key]) {
    try {
      result.snapshot.parameterDiagram = CSG_BUILTIN_PROFILES[result.key].build(result.parameters).parameterDiagram;
    } catch { /* Keep older or incomplete snapshots readable without guessed dimensions. */ }
  }
  return result;
}
function createCSGFeature(primitive, index = 0) {
  const definition = CSG_PRIMITIVES[primitive] ?? CSG_PRIMITIVES.box;
  return {
    id: `feature-${Date.now().toString(36)}-${index}-${Math.random().toString(36).slice(2, 7)}`,
    name: definition.label,
    primitive: CSG_PRIMITIVES[primitive] ? primitive : "box",
    operation: index ? "union" : "base",
    parameters: { ...definition.parameters },
    ...(primitive === "extrusion" ? { profile: builtinCSGProfile("builtin:solid-rectangle") } : {}),
    transform: { position: { x: index * 12, y: index * 8, z: 0 }, rotation: { x: 0, y: 0, z: 0 } },
  };
}

function normalizeCSGDraft(model = null) {
  const source = model?.csgDefinition;
  const features = Array.isArray(source?.features) && source.features.length
    ? cloneValue(source.features) : [createCSGFeature("extrusion")];
  for (let index = 0; index < features.length; index++) {
    const feature = features[index];
    feature.id = String(feature.id || `feature-${index + 1}`);
    feature.primitive = CSG_PRIMITIVES[feature.primitive] ? feature.primitive : "box";
    feature.name = String(feature.name || CSG_PRIMITIVES[feature.primitive].label);
    feature.operation = index === 0 ? "base" : (CSG_OPERATIONS[feature.operation] ? feature.operation : "union");
    feature.parameters = { ...CSG_PRIMITIVES[feature.primitive].parameters, ...(feature.parameters ?? {}) };
    if (feature.primitive === "extrusion") feature.profile = normalizeCSGProfile(feature.profile);
    feature.transform = {
      position: { x: 0, y: 0, z: 0, ...(feature.transform?.position ?? {}) },
      rotation: { x: 0, y: 0, z: 0, ...(feature.transform?.rotation ?? {}) },
    };
  }
  return {
    mode: model ? "edit" : "create", id: model?.id ?? "", expectedRevision: model?.revision ?? 0,
    name: String(model?.name ?? "未命名配件"), category: String(model?.category ?? "自制配件"),
    sourcing: "made", material: String(model?.material ?? ""), description: String(model?.description ?? ""),
    features, selectedId: features[0].id, selectedNodeKind: "primitive", tool: "view",
    previewStatus: "idle", previewError: "", dirty: !model,
  };
}

export function openComponentCSGEditor(view, model = null) {
  const state = componentLibraryState(view);
  if (model && (model.scope !== "user" || model.modelType !== "csg" || !model.csgDefinition)) return false;
  state.error = "";
  state.csgDraft = normalizeCSGDraft(model);
  return true;
}

export function componentLibraryState(view) {
  const state = view.tubeDesignerComponentLibrary ??= {
    models: [], selectedKey: "", scope: "system", search: "", collapsed: [], drafts: {},
    loadState: "idle", error: "", previewCache: new Map(), previewFailureKey: "",
  };
  if (!SCOPES.includes(state.scope)) state.scope = "system";
  state.selectedByScope ??= {};
  return state;
}

export function componentModelKey(model) {
  if (model?.scope === "template")
    return `template:${encodeURIComponent(String(model.templateId ?? ""))}:${encodeURIComponent(String(model.id ?? ""))}`;
  return `${model?.scope === "system" ? "system" : "user"}:${String(model?.id ?? "")}`;
}

export function getComponentModels(view) {
  return [...(componentLibraryState(view).models ?? [])]
    .filter((model) => model?.id && SCOPES.includes(model.scope) && (model.scope !== "template" || model.templateId))
    .sort((a, b) => SCOPES.indexOf(a.scope) - SCOPES.indexOf(b.scope)
      || (a.scope === "template" ? templateName(a).localeCompare(templateName(b), "zh-CN")
        || String(a.templateId).localeCompare(String(b.templateId)) : 0)
      || String(a.category ?? "").localeCompare(String(b.category ?? ""), "zh-CN")
      || String(a.name ?? "").localeCompare(String(b.name ?? ""), "zh-CN"));
}

export function getVisibleComponentModels(view) {
  const state = componentLibraryState(view);
  const query = String(state.search ?? "").trim().toLocaleLowerCase();
  return getComponentModels(view).filter((model) => model.scope === state.scope
    && [model.name, model.category, model.material, model.sourceFileName, model.templateId, templateName(model)]
      .some((value) => String(value ?? "").toLocaleLowerCase().includes(query)));
}

function modelByKey(view, key) {
  return getComponentModels(view).find((model) => componentModelKey(model) === key) ?? null;
}

function selectedModel(view) {
  const state = componentLibraryState(view);
  const models = getVisibleComponentModels(view);
  const model = models.find((item) => componentModelKey(item) === state.selectedKey)
    ?? models.find((item) => componentModelKey(item) === state.selectedByScope[state.scope])
    ?? models[0] ?? null;
  state.selectedKey = model ? componentModelKey(model) : "";
  if (model) state.selectedByScope[state.scope] = state.selectedKey;
  return model;
}

function sourcingLabel(value) { return value === "made" ? "自制" : "外购"; }
function categoryName(model) { return String(model?.category ?? "").trim() || "未分类"; }
function modelName(model) { return String(model?.name ?? model?.sourceFileName ?? model?.id ?? "三维配件"); }
function templateName(model) { return catalogText(model?.templateName, String(model?.templateId ?? "")); }
function scopeLabel(scope) { return { system: "系统内置", template: "模板自带", user: "我的" }[scope] ?? ""; }
function dimensionText(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) && numeric > 0 ? formatNumber(numeric) : "—";
}
function boundsText(model) {
  return ["width", "depth", "height"].map((key) => dimensionText(model?.bounds?.[key])).join(" × ") + " mm";
}

export function renderComponentLibraryLeftPane(_context, view) {
  const state = componentLibraryState(view);
  if (state.csgDraft) return renderComponentCSGTreePane(view, state.csgDraft);
  const all = getComponentModels(view);
  selectedModel(view);
  const visible = getVisibleComponentModels(view);
  const groups = new Map();
  for (const model of visible) {
    const key = JSON.stringify([model.scope, model.templateId ?? "", categoryName(model)]);
    if (!groups.has(key)) groups.set(key, { key, scope: model.scope, templateId: model.templateId, templateName: templateName(model), category: categoryName(model), models: [] });
    groups.get(key).models.push(model);
  }
  const renderGroup = (group) => {
    const expanded = !state.collapsed.includes(group.key);
    return `<section class="tube-component-library-group">
      <button type="button" class="tube-component-library-group-heading" data-cam-action="${PREFIX}toggle-category" data-component-category="${escapeAttr(group.key)}" aria-expanded="${expanded}"><span>${expanded ? "▾" : "▸"} ${escapeText(group.category)}</span><small>${scopeLabel(group.scope)} · ${group.models.length}</small></button>
      <div ${expanded ? "" : "hidden"}>${group.models.map((model) => {
        const key = componentModelKey(model);
        return `<button type="button" class="tube-component-library-card ${key === state.selectedKey ? "selected" : ""}" data-cam-action="${PREFIX}select" data-component-key="${escapeAttr(key)}" aria-pressed="${key === state.selectedKey}">
          <span class="tube-component-library-icon">${MODEL_ICON}</span><span><strong>${escapeText(modelName(model))}</strong><small>${sourcingLabel(model.sourcing)}${model.material ? ` · ${escapeText(model.material)}` : ""}</small><em>${escapeText(boundsText(model))}</em></span></button>`;
      }).join("")}</div></section>`;
  };
  const templates = new Map();
  for (const group of groups.values()) {
    if (!templates.has(group.templateId)) templates.set(group.templateId, []);
    templates.get(group.templateId).push(group);
  }
  const list = state.scope === "template" ? [...templates.entries()].map(([templateId, entries]) => {
    const key = JSON.stringify(["template-owner", templateId]);
    const expanded = !state.collapsed.includes(key);
    return `<section class="tube-component-library-group tube-component-library-template" data-component-template-id="${escapeAttr(templateId)}">
      <button type="button" class="tube-component-library-group-heading" data-cam-action="${PREFIX}toggle-category" data-component-category="${escapeAttr(key)}" aria-expanded="${expanded}"><span>${expanded ? "▾" : "▸"} ${escapeText(entries[0].templateName)}</span><small>仅所属模板可用</small></button>
      <div ${expanded ? "" : "hidden"}><div class="tube-component-library-source"><small>${escapeText(templateId)}</small></div>${entries.map(renderGroup).join("")}</div></section>`;
  }).join("") : [...groups.values()].map(renderGroup).join("");
  return `<div class="tube-designer-panel tube-component-library-panel">
    <div class="tube-designer-heading"><div><strong>配件库</strong><span>${SCOPES.map((scope) => `${scopeLabel(scope)} ${all.filter((m) => m.scope === scope).length}`).join(" · ")}</span></div></div>
    <div class="tube-component-library-filters">
      <input type="search" aria-label="搜索配件" placeholder="搜索名称、分类、材料或所属模板" value="${escapeAttr(state.search)}" data-cam-change-action="${PREFIX}search" />
      <div>${SCOPES.map((scope) => [scope, scopeLabel(scope)]).map(([scope, label]) =>
        `<button type="button" class="${scope === state.scope ? "selected" : ""}" data-cam-action="${PREFIX}scope" data-component-scope="${scope}" aria-pressed="${scope === state.scope}">${label}</button>`).join("")}</div>
    </div>
    <div class="tube-component-library-list">
      ${state.loadState === "loading" ? '<div class="tube-designer-empty">正在读取配件库…</div>' : ""}
      ${state.loadState === "error" ? `<div class="tube-component-library-error" role="alert">${escapeText(state.error)}<button class="tube-designer-secondary" data-cam-action="${PREFIX}refresh">重新读取</button></div>` : ""}
      ${list}
      ${!visible.length && state.loadState !== "loading" ? `<div class="tube-designer-empty">${String(state.search ?? "").trim() ? "当前来源没有匹配的配件，请调整搜索。" : `“${scopeLabel(state.scope)}”暂无配件。${state.scope === "user" ? "可以绘制可编辑模型，或导入 STEP / STP / BREP。" : ""}`}</div>` : ""}
    </div>
    ${state.scope === "user" ? `<footer><button class="tube-designer-secondary" data-cam-action="${PREFIX}import" ${view.pending ? "disabled" : ""}>导入三维模型</button><button class="tube-designer-primary" data-cam-action="${PREFIX}draw" ${view.pending ? "disabled" : ""}>绘制新模型</button></footer>` : ""}
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
  if (state.csgDraft) return renderComponentCSGInspectorPane(view, state.csgDraft);
  const model = selectedModel(view);
  if (!model) return '<div class="tube-designer-panel"><div class="tube-designer-heading"><strong>配件属性</strong></div><div class="tube-designer-empty">从左侧选择配件，中央显示真实三维模型。</div></div>';
  const key = componentModelKey(model);
  const readOnly = model.scope !== "user";
  const draft = state.drafts[key];
  const values = draft ?? model;
  return `<div class="tube-designer-panel tube-component-library-editor" data-component-editor data-component-key="${escapeAttr(key)}">
    <div class="tube-designer-heading"><div><strong>${scopeLabel(model.scope)}配件</strong><span>${readOnly ? "只读资源，不可修改或删除" : "编辑名称、分类与制造属性"}</span></div></div>
    <div class="tube-component-library-editor-body">
      ${model.scope === "template" ? `<div class="tube-component-library-source"><span>所属模板 · 仅此模板可使用</span><strong>${escapeText(templateName(model))}</strong><small>${escapeText(model.templateId)}</small></div>` : ""}
      ${metadataFields(values, readOnly || view.pending, "draft")}
      <div class="tube-component-library-bounds"><strong>模型外包尺寸（mm）</strong><dl>${[["width", "宽 X"], ["depth", "深 Y"], ["height", "高 Z"]].map(([field, label]) => `<div><dt>${label}</dt><dd>${escapeText(dimensionText(model.bounds?.[field]))}</dd></div>`).join("")}</dl><small>尺寸来自真实模型，不能在此修改或缩放。</small></div>
      <div class="tube-component-library-source"><span>${model.modelType === "csg" ? "模型表达" : "来源文件"}</span><strong>${escapeText(model.modelType === "csg" ? `可编辑 CSG · ${model.csgDefinition?.features?.length ?? 0} 个基础体` : (model.sourceFileName || "系统内置模型"))}</strong></div>
      <p class="tube-component-library-note">配件是非管材三维模型。“自制”仅记录供料方式，具体制造工艺仍需确认，不表示可直接使用管材加工。</p>
      ${state.error && state.loadState !== "error" ? `<div class="tube-component-library-error" role="alert">${escapeText(state.error)}</div>` : ""}
    </div>
    ${!readOnly ? `<footer><button class="tube-designer-danger" data-cam-action="${PREFIX}delete" data-component-key="${escapeAttr(key)}" ${view.pending ? "disabled" : ""}>删除</button>${model.modelType === "csg" ? `<button class="tube-designer-secondary" data-cam-action="${PREFIX}edit-csg" data-component-key="${escapeAttr(key)}" ${view.pending ? "disabled" : ""}>继续绘制</button>` : ""}<button class="tube-designer-primary" data-cam-action="${PREFIX}save" data-component-key="${escapeAttr(key)}" ${view.pending ? "disabled" : ""}>保存属性</button></footer>` : ""}
  </div>`;
}

export function renderComponentLibraryViewportOverlay(_context, view) {
  const state = componentLibraryState(view);
  if (state.csgDraft) return renderComponentCSGViewportOverlay(view, state.csgDraft);
  const model = selectedModel(view);
  const pending = model && state.previewRequest?.key === previewKey(model);
  const progress = pending && state.previewRequest.progressVisible ? `<div class="tube-profile-library-preview-wait" data-component-preview-progress aria-live="polite">
    <div class="tube-designer-export-progress-card">
      <span class="tube-designer-export-spinner" aria-hidden="true"></span>
      <strong>正在加载三维配件</strong><span>${escapeText(modelName(model))}</span>
      <div class="tube-designer-export-progress-track is-indeterminate" role="progressbar" aria-label="三维配件加载进度" aria-valuetext="${escapeAttr(state.previewRequest.phase)}"><i style="width:36%"></i></div>
      <small>${escapeText(state.previewRequest.phase)}</small><em>完成或失败后，界面会自动恢复。</em>
    </div></div>` : "";
  return progress + `<div class="tube-component-library-hud"><strong>${escapeText(model ? modelName(model) : "当前来源没有可预览的配件")}</strong><span>${model ? escapeText(boundsText(model)) : "调整搜索，或切换来源"}</span>${model?.scope === "template" ? `<span>所属模板：${escapeText(templateName(model))}</span>` : ""}<small>${pending ? "正在装载真实三维几何…" : "拖动旋转 · 滚轮缩放 · 支持透视 / 正交"}</small>${model && state.previewError ? `<p role="alert">${escapeText(state.previewError)}</p><button class="tube-designer-secondary" data-cam-action="${PREFIX}retry-preview">重新预览</button>` : ""}</div>`;
}

function csgFeatureBounds(feature) {
  const p = feature.parameters ?? {};
  if (feature.primitive === "extrusion") return {
    width: csgNumber(feature.profile?.snapshot?.width, 1),
    depth: csgNumber(feature.profile?.snapshot?.depth, 1),
    height: csgNumber(p.height, 1),
  };
  if (feature.primitive === "sphere") { const r = csgNumber(p.radius, 1); return { width: r * 2, depth: r * 2, height: r * 2 }; }
  if (feature.primitive === "cylinder") { const r = csgNumber(p.radius, 1); return { width: r * 2, depth: r * 2, height: csgNumber(p.height, 1) }; }
  if (feature.primitive === "cone") { const r = Math.max(csgNumber(p.bottomRadius, 1), csgNumber(p.topRadius, 0)); return { width: r * 2, depth: r * 2, height: csgNumber(p.height, 1) }; }
  return { width: csgNumber(p.width, 1), depth: csgNumber(p.depth, 1), height: csgNumber(p.height, 1) };
}

function csgDefinition(draft) {
  return { schema: "icax.component-csg", schemaVersion: 1, evaluation: "left-fold", features: cloneValue(draft.features) };
}

function csgBoundsText(bounds) {
  if (!bounds) return "";
  return `${dimensionText(bounds.width)} × ${dimensionText(bounds.depth)} × ${dimensionText(bounds.height)} mm`;
}

function csgPreviewLabel(draft) {
  if (draft.previewStatus === "error") return draft.previewError || "当前布尔组合无法生成实体";
  if (draft.previewStatus === "loading") return "正在重新计算真实 CSG 实体…";
  if (draft.previewStatus === "ready") return `实体已更新${draft.previewBounds ? ` · ${csgBoundsText(draft.previewBounds)}` : ""}`;
  return "正在启动三维绘制器…";
}

function csgNumberField(label, field, value, disabled) {
  return `<label><span>${label}</span><input type="number" step="0.1" value="${escapeAttr(csgNumber(value))}" data-cam-change-action="${PREFIX}csg-feature-change" data-csg-feature-field="${field}" ${disabled ? "disabled" : ""}/></label>`;
}

function csgLocalizedText(value, fallback = "参数") {
  if (typeof value === "string" && value.trim()) return value.trim();
  if (value && typeof value === "object") {
    for (const locale of ["zh-CN", "zh", "en-US", "en"]) {
      if (typeof value[locale] === "string" && value[locale].trim()) return value[locale].trim();
    }
    const text = Object.values(value).find((item) => typeof item === "string" && item.trim());
    if (text) return text.trim();
  }
  return fallback;
}

function isParametricCSGLibraryProfile(profile) {
  return profileScope(profile) !== "user"
    || String(profile?.profileType ?? profile?.kind ?? "imported-dxf") === "parametric-package";
}

function csgLibraryProfileName(profile) {
  return String(profile?.name ?? profile?.previewProfile?.name
    ?? csgLocalizedText(profile?.descriptor?.displayName, "未命名截面"));
}

function csgProfileChoice(profile) {
  const parametric = isParametricCSGLibraryProfile(profile);
  const snapshot = parametric
    ? profile?.previewProfile ?? profile?.profile ?? (profile?.contours ? profile : null)
    : profile;
  if (!snapshot?.contours?.length) return null;
  const definitions = Array.isArray(profile?.descriptor?.parameters) ? profile.descriptor.parameters
    : (Array.isArray(snapshot.parameterDefinitions) ? snapshot.parameterDefinitions : []);
  const defaults = profile?.defaultParameters ?? snapshot.parameters ?? {};
  return {
    sourceType: parametric ? "parametric" : "fixed",
    key: profileSelectionKey(profile), name: csgLibraryProfileName(profile),
    ...(parametric ? { profileRef: profileRef(profile) } : {}),
    parameters: cloneValue(defaults), parameterDefinitions: cloneValue(definitions), snapshot: cloneValue(snapshot),
  };
}

function csgProfileChoices(view) {
  return libraryProfiles(view).map((profile) => ({ profile, value: csgProfileChoice(profile) }))
    .filter((item) => item.value);
}

function matchesCSGParameterVisibility(condition, values) {
  if (!condition) return true;
  const all = condition.conditions && condition.op === "all" ? condition.conditions : condition.all;
  const any = condition.conditions && condition.op === "any" ? condition.conditions : condition.any;
  if (Array.isArray(all)) return all.every((item) => matchesCSGParameterVisibility(item, values));
  if (Array.isArray(any)) return any.some((item) => matchesCSGParameterVisibility(item, values));
  const actual = values?.[condition.parameter ?? condition.key];
  if (condition.op === "eq") return actual === condition.value;
  if (condition.op === "ne") return actual !== condition.value;
  return true;
}

function renderCSGProfileParameter(definition, values, disabled) {
  const key = String(definition?.key ?? "");
  const label = csgLocalizedText(definition?.displayName, key);
  const value = values?.[key] ?? definition?.defaultValue ?? "";
  const common = `data-cam-change-action="${PREFIX}csg-profile-parameter-change" data-csg-profile-parameter="${escapeAttr(key)}" data-profile-parameter-key="${escapeAttr(key)}" data-csg-profile-value-type="${escapeAttr(definition?.valueType ?? "number")}"`;
  if (definition?.valueType === "boolean") return `<label><span>${escapeText(label)}</span><input type="checkbox" ${common} ${value ? "checked" : ""} ${disabled ? "disabled" : ""}/></label>`;
  const options = Array.isArray(definition?.options) ? definition.options : [];
  if (options.length) return `<label><span>${escapeText(label)}</span><select ${common} ${disabled ? "disabled" : ""}>${options.map((option) => {
    const optionValue = typeof option === "object" ? option?.value : option;
    const optionLabel = typeof option === "object" ? csgLocalizedText(option?.displayName ?? option?.label, optionValue) : option;
    return `<option value="${escapeAttr(optionValue)}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(optionLabel)}</option>`;
  }).join("")}</select></label>`;
  const type = definition?.valueType === "string" ? "text" : "number";
  return `<label><span>${escapeText(label)}</span><input type="${type}" value="${escapeAttr(value)}" ${common}
    ${definition?.min != null || definition?.minimum != null ? `min="${escapeAttr(definition.min ?? definition.minimum)}"` : ""}
    ${definition?.max != null || definition?.maximum != null ? `max="${escapeAttr(definition.max ?? definition.maximum)}"` : ""}
    ${type === "number" ? `step="${escapeAttr(definition?.step ?? (definition?.valueType === "integer" ? 1 : "any"))}"` : ""} ${disabled ? "disabled" : ""}/></label>`;
}

function renderCSGExtrusionProfileEditor(view, feature) {
  const choices = csgProfileChoices(view);
  const currentKey = String(feature.profile?.key ?? "");
  const builtinOptions = Object.entries(CSG_BUILTIN_PROFILES).map(([key, profile]) =>
    `<option value="${escapeAttr(key)}" ${key === currentKey ? "selected" : ""}>${escapeText(profile.name)}</option>`).join("");
  const renderLibraryOptions = (parametric) => choices.filter((item) => (item.value.sourceType === "parametric") === parametric)
    .map(({ profile, value }) => `<option value="${escapeAttr(value.key)}" ${value.key === currentKey ? "selected" : ""}>${escapeText(scopeLabel(profileScope(profile)))} · ${escapeText(value.name)}</option>`).join("");
  const currentFound = currentKey.startsWith("builtin:") || choices.some((item) => item.value.key === currentKey);
  const definitions = Array.isArray(feature.profile?.parameterDefinitions) ? feature.profile.parameterDefinitions : [];
  const parameterFields = feature.profile?.sourceType === "parametric"
    ? definitions.filter((item) => matchesCSGParameterVisibility(item?.visibleWhen, feature.profile.parameters))
      .map((item) => renderCSGProfileParameter(item, feature.profile.parameters, view.pending)).join("")
    : "";
  return `<div class="tube-component-csg-profile-editor" data-csg-profile-editor data-profile-parameter-scope>
    <header><strong>拉伸截面</strong><small>${feature.profile?.sourceType === "fixed" ? "定式截面" : "程式截面 · 参数可修改"}</small></header>
    <label><span>截面来源</span><select data-cam-change-action="${PREFIX}csg-profile-select" ${view.pending ? "disabled" : ""}>
      ${!currentFound ? `<option value="${escapeAttr(currentKey)}" selected>当前嵌入截面 · ${escapeText(feature.profile?.name ?? "截面")}</option>` : ""}
      <optgroup label="程式截面 · 基础实体">${builtinOptions}</optgroup>
      <optgroup label="程式截面 · 管型库">${renderLibraryOptions(true)}</optgroup>
      <optgroup label="定式截面 · DXF / 二维草图">${renderLibraryOptions(false)}</optgroup>
    </select></label>
    <div class="tube-component-csg-profile-summary"><strong>${escapeText(feature.profile?.name ?? "截面")}</strong><span>${dimensionText(feature.profile?.snapshot?.width)} × ${dimensionText(feature.profile?.snapshot?.depth)} mm · ${feature.profile?.snapshot?.contourCount ?? feature.profile?.snapshot?.contours?.length ?? 0} 个闭合轮廓</span></div>
    ${feature.profile?.sourceType === "parametric" ? renderProfileParameterDiagram(feature.profile.snapshot, { definitions, compact: true }) : ""}
    ${parameterFields ? `<div class="tube-component-csg-field-grid tube-component-csg-profile-parameters">${parameterFields}</div>` : ""}
    ${feature.profile?.sourceType === "fixed" ? "<p>定式截面保留原始曲线，可再次用二维草图修改，也可换成 DXF 或程式截面。</p>" : "<p>程式参数改变后会重新生成截面并立即参与 CSG 计算；转到二维草图后会成为可自由修改的定式截面。</p>"}
    <div class="tube-component-csg-profile-actions"><button type="button" data-cam-action="${PREFIX}csg-import-profile-dxf" ${view.pending ? "disabled" : ""}>导入 DXF</button><button type="button" data-cam-action="${PREFIX}csg-new-profile-sketch" ${view.pending ? "disabled" : ""}>${feature.profile?.sourceType === "fixed" ? "编辑二维轮廓" : "转到二维草图"}</button></div>
  </div>`;
}

function buildCSGExpressionTree(features) {
  if (!features.length) return null;
  let root = { kind: "primitive", feature: features[0] };
  for (let index = 1; index < features.length; index++) {
    const feature = features[index];
    root = { kind: "operation", feature, operation: feature.operation, left: root,
      right: { kind: "primitive", feature } };
  }
  return root;
}

function renderCSGExpressionNode(node, draft, depth = 0) {
  if (!node) return "";
  const feature = node.feature;
  const selected = draft.selectedId === feature.id && draft.selectedNodeKind === node.kind;
  if (node.kind === "primitive") {
    const primitive = CSG_PRIMITIVES[feature.primitive] ?? CSG_PRIMITIVES.box;
    const detail = feature.primitive === "extrusion"
      ? `${feature.profile?.sourceType === "fixed" ? "定式截面" : "程式截面"} · ${feature.profile?.name ?? "截面"} · 拉伸 ${formatNumber(feature.parameters?.height)} mm`
      : `${primitive.label} · 基础实体`;
    return `<li class="primitive-node" role="treeitem" aria-level="${depth + 1}"><button type="button" class="${selected ? "selected" : ""}" data-cam-action="${PREFIX}csg-select" data-csg-node-kind="primitive" data-csg-feature-id="${escapeAttr(feature.id)}"><em>${primitive.glyph}</em><span><strong>${escapeText(feature.name || primitive.label)}</strong><small>${escapeText(detail)}</small></span></button></li>`;
  }
  const operation = CSG_OPERATIONS[node.operation] ?? CSG_OPERATIONS.union;
  return `<li class="operation-node operation-${escapeAttr(node.operation)}" role="treeitem" aria-expanded="true" aria-level="${depth + 1}"><button type="button" class="${selected ? "selected" : ""}" data-cam-action="${PREFIX}csg-select" data-csg-node-kind="operation" data-csg-feature-id="${escapeAttr(feature.id)}"><em>${CSG_OPERATION_SYMBOLS[node.operation] ?? "∪"}</em><span><strong>${operation}</strong><small>布尔运算 · 生成实体</small></span></button><ul role="group">${renderCSGExpressionNode(node.left, draft, depth + 1)}${renderCSGExpressionNode(node.right, draft, depth + 1)}</ul></li>`;
}

function renderComponentCSGTreePane(view, draft) {
  const selected = selectedCSGFeature(draft);
  const selectedIndex = draft.features.indexOf(selected);
  const primitiveSelected = draft.selectedNodeKind !== "operation";
  const tree = renderCSGExpressionNode(buildCSGExpressionTree(draft.features), draft);
  return `<div class="tube-designer-panel tube-component-csg-page-tree" data-component-csg-editor>
    <div class="tube-designer-heading"><div><strong>CSG 结构树</strong><span>${draft.mode === "edit" ? "编辑已有零件" : "新建零件"} · 当前会话${draft.dirty ? "有未保存修改" : "尚未修改"}</span></div><button type="button" class="tube-designer-secondary" data-cam-action="${PREFIX}cancel-csg" ${view.pending ? "disabled" : ""}>放弃并退出</button></div>
    <div class="tube-component-csg-session-note"><strong>当前零件 · ${escapeText(draft.name)}</strong><span>下面所有节点只属于这个零件；完成前不会写入“我的配件”。</span></div>
    <div class="tube-component-csg-add-grid"><span>添加基础体</span>${Object.entries(CSG_PRIMITIVES).map(([key, item]) => `<button type="button" data-cam-action="${PREFIX}csg-add" data-csg-primitive="${key}" ${view.pending ? "disabled" : ""}><b>${item.glyph}</b>${item.label}</button>`).join("")}</div>
    <div class="tube-component-csg-expression-wrap"><ul class="tube-component-csg-expression" role="tree" aria-label="当前零件 CSG 结构树">${tree}</ul></div>
    <div class="tube-component-csg-tree-actions"><button data-cam-action="${PREFIX}csg-move" data-csg-direction="up" ${!primitiveSelected || selectedIndex <= 0 || view.pending ? "disabled" : ""}>上移节点</button><button data-cam-action="${PREFIX}csg-move" data-csg-direction="down" ${!primitiveSelected || selectedIndex < 0 || selectedIndex >= draft.features.length - 1 || view.pending ? "disabled" : ""}>下移节点</button><button class="danger" data-cam-action="${PREFIX}csg-delete" ${!primitiveSelected || draft.features.length <= 1 || view.pending ? "disabled" : ""}>删除基础体</button></div>
  </div>`;
}

function renderComponentCSGInspectorPane(view, draft) {
  const state = componentLibraryState(view);
  const selected = selectedCSGFeature(draft);
  const selectedIndex = draft.features.indexOf(selected);
  const primitive = CSG_PRIMITIVES[selected?.primitive] ?? CSG_PRIMITIVES.box;
  const operationSelected = draft.selectedNodeKind === "operation" && selectedIndex > 0;
  const parameterFields = selected ? Object.entries(primitive.parameters).map(([field]) => csgNumberField({ width: "宽 X", depth: "深 Y", height: selected.primitive === "extrusion" ? "拉伸高度" : "高 Z", radius: "半径", bottomRadius: "底部半径", topRadius: "顶部半径" }[field], `parameters.${field}`, selected.parameters?.[field], view.pending)).join("") : "";
  const selectionEditor = operationSelected
    ? `<section data-csg-selected="${escapeAttr(selected.id)}"><header><strong>${escapeText(CSG_OPERATIONS[selected.operation])}</strong><small>CSG 运算节点</small></header><p class="tube-component-csg-base-note">左输入是此前的计算结果，右输入是“${escapeText(selected.name)}”。选择运算后立即重新计算，但不会保存到配件库。</p><div class="tube-component-csg-operation"><span>布尔类型</span><div>${["union", "difference", "intersection"].map((op) => `<button type="button" class="${selected.operation === op ? "selected" : ""}" data-cam-action="${PREFIX}csg-operation" data-csg-operation="${op}" ${view.pending ? "disabled" : ""}>${CSG_OPERATION_SYMBOLS[op]} ${CSG_OPERATIONS[op]}</button>`).join("")}</div></div><dl class="tube-component-csg-operands"><div><dt>左输入</dt><dd>上一级结果</dd></div><div><dt>右输入</dt><dd>${escapeText(selected.name)}</dd></div></dl></section>`
    : (selected ? `<section data-csg-selected="${escapeAttr(selected.id)}"><header><strong>${escapeText(primitive.label)}</strong><small>基础体节点</small></header><label><span>节点名称</span><input value="${escapeAttr(selected.name)}" maxlength="80" data-cam-change-action="${PREFIX}csg-feature-change" data-csg-feature-field="name" ${view.pending ? "disabled" : ""}/></label>${selectedIndex === 0 ? `<p class="tube-component-csg-base-note">这是构造树的第一个实体。添加下一个基础体后，树中会生成独立的布尔运算节点。</p>` : ""}${selected.primitive === "extrusion" ? renderCSGExtrusionProfileEditor(view, selected) : ""}<header><strong>${selected.primitive === "extrusion" ? "拉伸" : "尺寸"}</strong><small>mm</small></header><div class="tube-component-csg-field-grid">${parameterFields}</div><header><strong>位置</strong><small>X / Y / Z · mm</small></header><div class="tube-component-csg-field-grid">${["x", "y", "z"].map((axis) => csgNumberField(axis.toUpperCase(), `position.${axis}`, selected.transform?.position?.[axis], view.pending)).join("")}</div><header><strong>旋转</strong><small>X / Y / Z · °</small></header><div class="tube-component-csg-field-grid">${["x", "y", "z"].map((axis) => csgNumberField(axis.toUpperCase(), `rotation.${axis}`, selected.transform?.rotation?.[axis], view.pending)).join("")}</div></section>` : "");
  return `<div class="tube-designer-panel tube-component-csg-page-inspector">
    <div class="tube-designer-heading"><div><strong>节点属性</strong><span>选择左侧基础体或布尔节点进行修改</span></div></div>
    <div class="tube-component-csg-inspector">${selectionEditor}<section><header><strong>零件信息</strong><small>仅完成时保存一次</small></header><label><span>名称</span><input value="${escapeAttr(draft.name)}" maxlength="120" data-cam-change-action="${PREFIX}csg-model-change" data-csg-model-field="name" ${view.pending ? "disabled" : ""}/></label><label><span>分类</span><input value="${escapeAttr(draft.category)}" maxlength="160" data-cam-change-action="${PREFIX}csg-model-change" data-csg-model-field="category" ${view.pending ? "disabled" : ""}/></label><label><span>材料</span><input value="${escapeAttr(draft.material)}" maxlength="160" placeholder="可选" data-cam-change-action="${PREFIX}csg-model-change" data-csg-model-field="material" ${view.pending ? "disabled" : ""}/></label><label><span>说明</span><textarea rows="3" maxlength="2000" data-cam-change-action="${PREFIX}csg-model-change" data-csg-model-field="description" ${view.pending ? "disabled" : ""}>${escapeText(draft.description)}</textarea></label></section>${state.error ? `<div class="tube-component-library-error" role="alert">${escapeText(state.error)}</div>` : ""}</div>
    <footer class="tube-component-csg-page-save"><small>不会为树节点分别创建“我的配件”记录。</small><button class="tube-designer-primary" data-component-csg-save data-cam-action="${PREFIX}save-csg" ${view.pending || draft.previewStatus === "loading" || draft.previewStatus === "error" ? "disabled" : ""}>${view.pending ? "正在生成零件…" : (draft.mode === "edit" ? "完成修改" : "完成并保存零件")}</button></footer>
  </div>`;
}

function renderComponentCSGViewportOverlay(view, draft) {
  const tool = CSG_TOOLS[draft.tool] ? draft.tool : "view";
  return `<div class="tube-component-csg-page-overlay tool-${tool}" data-component-csg-workspace>
    <div class="tube-component-csg-page-toolbar"><strong>零件编辑</strong><span>操作</span><div class="tube-component-csg-toolset">${Object.entries(CSG_TOOLS).map(([key, item]) => `<button type="button" class="${tool === key ? "selected" : ""}" data-cam-action="${PREFIX}csg-tool" data-csg-tool="${key}" ${view.pending ? "disabled" : ""}>${item.label}</button>`).join("")}</div><i></i><button type="button" data-cam-action="${PREFIX}csg-camera" data-csg-camera="iso">等轴测</button><button type="button" data-cam-action="${PREFIX}csg-camera" data-csg-camera="top">顶视</button><button type="button" data-cam-action="${PREFIX}csg-camera" data-csg-camera="front">前视</button><button type="button" data-cam-action="${PREFIX}csg-fit">适合窗口</button></div>
    <div class="tube-component-csg-manipulation ${tool === "view" ? "" : "active"}" data-component-csg-manipulation tabindex="0" aria-label="${escapeAttr(CSG_TOOLS[tool].hint)}"></div>
    <div class="tube-component-csg-page-caption"><strong>当前零件实时结果</strong><span>${escapeText(CSG_TOOLS[tool].hint)}</span></div>
    <div class="tube-component-csg-preview-status ${escapeAttr(draft.previewStatus)}" data-component-csg-preview-status ${draft.previewStatus === "error" ? 'role="alert"' : 'aria-live="polite"'}><i></i><span>${escapeText(csgPreviewLabel(draft))}</span></div>
    <div class="tube-component-csg-legend"><span><i class="result"></i>CSG 结果</span><span><i class="operand"></i>当前基础体</span></div>
  </div>`;
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
  for (const model of getComponentModels(view)) {
    if (model.scope === "template") {
      if (template?.id && model.templateId === template.id && !result.some((item) => item.value === `template:${model.id}`))
        result.push({ value: `template:${model.id}`, group: "模板自带", label: modelName(model) });
      continue;
    }
    result.push({
    value: `${model.scope === "system" ? "system" : "library"}:${model.id}`,
    group: model.scope === "system" ? "系统内置" : "我的",
    label: `${modelName(model)} · ${categoryName(model)} · ${sourcingLabel(model.sourcing)}`,
    });
  }
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
  const groups = SCOPES.map(scopeLabel);
  const missing = current && !options.some((option) => option.value === current);
  return `<div class="tube-designer-field wide tube-component-model-field"><label><span>${escapeText(catalogText(field.displayName, field.label ?? name))}</span><select data-tube-designer-parameter="${escapeAttr(name)}" data-cam-change-action="tube-designer-parameter-change" ${disabled || field.readOnly ? "disabled" : ""}>
    <option value="" ${current ? "" : "selected"}>未选择配件</option>
    ${missing ? `<option value="${escapeAttr(current)}" selected>当前引用：${escapeText(current)}（${state.loadState === "loaded" ? "库中未找到" : "尚未读取"}）</option>` : ""}
    ${groups.map((group) => {
      const items = options.filter((option) => option.group === group);
      return items.length ? `<optgroup label="${group}">${items.map((option) => `<option value="${escapeAttr(option.value)}" ${option.value === current ? "selected" : ""}>${escapeText(option.label)}</option>`).join("")}</optgroup>` : "";
    }).join("")}</select></label><small>模板自带配件仅限所属模板。完整模型不作为管材截面，不自动缩放，请核对用途、安装面与尺寸。</small><button type="button" class="tube-designer-secondary" data-cam-action="${PREFIX}refresh" ${view.pending || state.loadState === "loading" ? "disabled" : ""}>${state.loadState === "loading" ? "正在读取配件…" : "刷新可选配件"}</button>${state.loadState === "error" ? `<small role="alert">${escapeText(state.error)}</small>` : ""}</div>`;
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
      state.previewFailureKey = "";
      state.previewError = "";
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
  state.selectedByScope[model.scope] = key;
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

async function exportComponentModel(context, view, ops) {
  if (view.pending) return null;
  const state = componentLibraryState(view);
  // Do not auto-select a different model for an export command. The selected
  // record must still be visible under the current source and search filter.
  const model = getVisibleComponentModels(view).find((item) => componentModelKey(item) === state.selectedKey);
  if (!model) {
    state.error = view.error = "请先选择当前列表中的一个配件。";
    ops.renderProject(context, view);
    return null;
  }
  const reference = {
    scope: model.scope,
    id: String(model.id),
    ...(model.scope === "template" ? { templateId: String(model.templateId) } : {}),
  };
  return componentTask(context, view, ops, "正在导出配件 STEP", async () => {
    const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
    if (typeof bridge?.openDirectoryDialog !== "function") throw new Error("当前宿主没有提供目录选择能力。");
    if (typeof context.sceneProxy?.invoke !== "function") throw new Error("当前场景没有提供配件导出能力。");
    const targetDirectory = String(await bridge.openDirectoryDialog({
      title: "选择配件 STEP 导出目录",
      initialDirectory: view.tubeDesignerComponentExportDirectory ?? "",
    }) ?? "").trim();
    if (!targetDirectory) return null;
    view.tubeDesignerComponentExportDirectory = targetDirectory;
    const response = await context.sceneProxy.invoke("TubeDesigner.ExportComponentModel", {
      ...reference, targetDirectory,
    }, { timeoutMs: 120000 });
    if (typeof response?.path !== "string" || !response.path.trim()) throw new Error("配件导出没有返回有效的文件路径。");
    if (response.format !== "step") throw new Error("配件导出没有返回 STEP 格式文件。");
    ops.showNotice?.(context, view, `配件 STEP 已导出：${response.path}`);
    return response;
  });
}

function selectedCSGExtrusion(draft) {
  const feature = draft?.features?.find((item) => item.id === draft.selectedId);
  return feature?.primitive === "extrusion" && draft.selectedNodeKind !== "operation" ? feature : null;
}

function setCSGExtrusionProfile(draft, feature, profile) {
  if (!draft || !feature || feature.primitive !== "extrusion") return false;
  feature.profile = normalizeCSGProfile(profile);
  draft.selectedId = feature.id;
  draft.selectedNodeKind = "primitive";
  draft.dirty = true;
  draft.previewStatus = "loading";
  draft.previewError = "";
  return true;
}

function csgProfileParameterValue(target, definition) {
  if (definition?.valueType === "boolean") return Boolean(target?.checked);
  if (definition?.valueType === "string") return String(target?.value ?? "");
  const numeric = Number(target?.value);
  if (!Number.isFinite(numeric)) throw new Error(`${csgLocalizedText(definition?.displayName, definition?.key)}必须是有效数值。`);
  return definition?.valueType === "integer" ? Math.round(numeric) : numeric;
}

async function chooseCSGProfileDxf(context, view, ops, draft, feature) {
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
  if (typeof bridge?.openFileDialog !== "function") throw new Error("当前宿主没有提供文件选择能力。");
  const sourcePath = String(await bridge.openFileDialog({
    title: "选择拉伸体的 DXF 截面",
    filters: [{ name: "DXF 二维截面", extensions: ["dxf"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  return componentTask(context, view, ops, "正在读取定式截面", async () => {
    const response = await productInvoke(context, "TubeDesigner.ImportProfileDxf", { sourcePath });
    const snapshot = response?.profile;
    if (!snapshot?.contours?.length) throw new Error("DXF 没有返回可用的闭合截面。");
    if (!draft.features.includes(feature)) return null;
    setCSGExtrusionProfile(draft, feature, {
      sourceType: "fixed", key: `embedded:dxf:${Date.now()}`,
      name: String(snapshot.name ?? snapshot.sourceFileName ?? "DXF 截面"),
      parameters: {}, parameterDefinitions: [], snapshot,
    });
    ops.showNotice?.(context, view, "DXF 截面已放入当前拉伸体；完成零件时才会写入“我的配件”。");
    return snapshot;
  });
}

export async function handleComponentLibraryAction(context, view, action, target, ops) {
  if (!String(action).startsWith(PREFIX)) return { handled: false };
  const state = componentLibraryState(view);
  const suffix = action.slice(PREFIX.length);
  if (view.pending) return { handled: true };
  if (suffix === "refresh") return { handled: true, result: await refreshComponentModels(context, view, ops) };
  if (suffix === "import") return { handled: true, result: await chooseComponentModel(context, view, ops) };
  if (suffix === "draw") {
    if (!state.csgDraft) openComponentCSGEditor(view);
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "edit-csg") {
    const model = modelByKey(view, String(target?.dataset?.componentKey ?? state.selectedKey));
    if (openComponentCSGEditor(view, model)) ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "cancel-csg") {
    state.csgDraft = null; state.error = "";
    view.tubeDesignerComponentCSGProfileReturn = null;
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-add" && state.csgDraft) {
    const primitive = String(target?.dataset?.csgPrimitive ?? "");
    if (CSG_PRIMITIVES[primitive] && state.csgDraft.features.length < 64) {
      const feature = createCSGFeature(primitive, state.csgDraft.features.length);
      state.csgDraft.features.push(feature); state.csgDraft.selectedId = feature.id;
      state.csgDraft.selectedNodeKind = "primitive"; state.csgDraft.dirty = true;
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-profile-select" && state.csgDraft) {
    const feature = selectedCSGExtrusion(state.csgDraft);
    const key = String(target?.value ?? target?.dataset?.csgProfileKey ?? "");
    if (feature) {
      const next = key.startsWith("builtin:") ? builtinCSGProfile(key)
        : csgProfileChoices(view).find((item) => item.value.key === key)?.value;
      if (next) setCSGExtrusionProfile(state.csgDraft, feature, cloneValue(next));
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-profile-parameter-change" && state.csgDraft) {
    const draft = state.csgDraft;
    const feature = selectedCSGExtrusion(draft);
    const key = String(target?.dataset?.csgProfileParameter ?? "");
    const definition = feature?.profile?.parameterDefinitions?.find((item) => String(item?.key ?? "") === key);
    if (!feature || !definition) return { handled: true };
    const parameters = { ...(feature.profile.parameters ?? {}), [key]: csgProfileParameterValue(target, definition) };
    if (feature.profile.key.startsWith("builtin:")) {
      setCSGExtrusionProfile(draft, feature, builtinCSGProfile(feature.profile.key, parameters));
      ops.renderProject(context, view); return { handled: true };
    }
    const reference = feature.profile.profileRef;
    if (!reference?.id) throw new Error("当前程式截面缺少可求值的来源。");
    return { handled: true, result: await componentTask(context, view, ops, "正在更新程式截面", async () => {
      const response = await productInvoke(context, "TubeDesigner.EvaluateProfilePackage", {
        profileRef: cloneValue(reference), parameters,
      });
      if (!response?.profile?.contours?.length) throw new Error("程式截面没有生成有效的闭合轮廓。");
      if (!draft.features.includes(feature)) return null;
      setCSGExtrusionProfile(draft, feature, { ...feature.profile, parameters, snapshot: response.profile });
      return response.profile;
    }) };
  }
  if (suffix === "csg-import-profile-dxf" && state.csgDraft) {
    const feature = selectedCSGExtrusion(state.csgDraft);
    return { handled: true, result: feature
      ? await chooseCSGProfileDxf(context, view, ops, state.csgDraft, feature) : null };
  }
  if (suffix === "csg-new-profile-sketch" && state.csgDraft) {
    const feature = selectedCSGExtrusion(state.csgDraft);
    if (feature) {
      let sketch;
      try {
        sketch = feature.profile?.snapshot?.contours?.length
          ? beginProfileSectionSketch(view, { snapshot: feature.profile.snapshot, name: feature.profile.name })
          : beginNewSectionSketch(view);
      } catch {
        sketch = beginNewSectionSketch(view);
      }
      sketch.sectionName = `${feature.name || "拉伸体"}截面`;
      view.tubeDesignerComponentCSGProfileReturn = { featureId: feature.id };
      view.tubeDesignerSketchDialogOpen = true;
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-select" && state.csgDraft) {
    const id = String(target?.dataset?.csgFeatureId ?? "");
    const kind = target?.dataset?.csgNodeKind === "operation" ? "operation" : "primitive";
    const index = state.csgDraft.features.findIndex((feature) => feature.id === id);
    if (index >= 0) {
      state.csgDraft.selectedId = id;
      state.csgDraft.selectedNodeKind = kind === "operation" && index > 0 ? "operation" : "primitive";
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-tool" && state.csgDraft) {
    const tool = String(target?.dataset?.csgTool ?? "");
    if (CSG_TOOLS[tool]) state.csgDraft.tool = tool;
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-camera" && state.csgDraft) {
    const camera = String(target?.dataset?.csgCamera ?? "");
    const controller = csgViewportControllers.get(context.mount);
    if (["iso", "top", "front", "right"].includes(camera)) {
      controller?.viewport.setStandardView?.(camera);
      controller?.viewport.fitViewToViewport?.(1.24);
    }
    return { handled: true };
  }
  if (suffix === "csg-fit" && state.csgDraft) {
    csgViewportControllers.get(context.mount)?.viewport.fitViewToViewport?.(1.24);
    return { handled: true };
  }
  if (suffix === "csg-delete" && state.csgDraft) {
    if (state.csgDraft.selectedNodeKind === "operation") return { handled: true };
    const index = state.csgDraft.features.findIndex((feature) => feature.id === state.csgDraft.selectedId);
    if (index >= 0 && state.csgDraft.features.length > 1) {
      state.csgDraft.features.splice(index, 1); state.csgDraft.features[0].operation = "base";
      state.csgDraft.selectedId = state.csgDraft.features[Math.min(index, state.csgDraft.features.length - 1)].id;
      state.csgDraft.selectedNodeKind = "primitive"; state.csgDraft.dirty = true;
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-operation" && state.csgDraft) {
    const feature = state.csgDraft.features.find((item) => item.id === state.csgDraft.selectedId);
    const operation = String(target?.dataset?.csgOperation ?? "");
    const index = state.csgDraft.features.indexOf(feature);
    if (feature && index > 0 && ["union", "difference", "intersection"].includes(operation)) {
      feature.operation = operation;
      state.csgDraft.selectedNodeKind = "operation"; state.csgDraft.dirty = true;
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-move" && state.csgDraft) {
    if (state.csgDraft.selectedNodeKind === "operation") return { handled: true };
    const index = state.csgDraft.features.findIndex((feature) => feature.id === state.csgDraft.selectedId);
    const next = index + (target?.dataset?.csgDirection === "up" ? -1 : 1);
    if (index >= 0 && next >= 0 && next < state.csgDraft.features.length) {
      [state.csgDraft.features[index], state.csgDraft.features[next]] = [state.csgDraft.features[next], state.csgDraft.features[index]];
      state.csgDraft.features[0].operation = "base";
      for (let featureIndex = 1; featureIndex < state.csgDraft.features.length; featureIndex++)
        if (state.csgDraft.features[featureIndex].operation === "base") state.csgDraft.features[featureIndex].operation = "union";
      state.csgDraft.dirty = true;
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "csg-model-change" && state.csgDraft) {
    const field = String(target?.dataset?.csgModelField ?? "");
    if (["name", "category", "material", "description"].includes(field)) {
      state.csgDraft[field] = String(target?.value ?? ""); state.csgDraft.dirty = true;
    }
    return { handled: true };
  }
  if (suffix === "csg-feature-change" && state.csgDraft) {
    const feature = state.csgDraft.features.find((item) => item.id === state.csgDraft.selectedId);
    const field = String(target?.dataset?.csgFeatureField ?? "");
    if (feature && field === "name") { feature.name = String(target?.value ?? ""); state.csgDraft.dirty = true; }
    else if (feature && field === "operation" && CSG_OPERATIONS[target?.value] && target.value !== "base") {
      feature.operation = target.value; state.csgDraft.dirty = true;
    }
    else if (feature && /^(?:parameters\.[A-Za-z]+|position\.[xyz]|rotation\.[xyz])$/.test(field)) {
      const [group, key] = field.split(".");
      const owner = group === "parameters" ? feature.parameters : feature.transform[group];
      owner[key] = csgNumber(target?.value, owner[key]); state.csgDraft.dirty = true;
    }
    ops.renderProject(context, view); return { handled: true };
  }
  if (suffix === "save-csg" && state.csgDraft) {
    const draft = state.csgDraft;
    if (draft.previewStatus === "error") throw new Error(draft.previewError || "当前 CSG 组合无法生成有效实体。");
    if (!String(draft.name).trim()) throw new Error("请填写模型名称。");
    if (!String(draft.category).trim()) throw new Error("请填写模型分类。");
    if (!draft.features.length) throw new Error("请至少添加一个基础体。");
    const payload = { name: draft.name.trim(), category: draft.category.trim(), sourcing: "made", material: draft.material.trim(), description: draft.description.trim(), csgDefinition: csgDefinition(draft) };
    if (draft.mode === "edit") { payload.id = draft.id; payload.expectedRevision = draft.expectedRevision; }
    return { handled: true, result: await componentTask(context, view, ops, draft.mode === "edit" ? "正在更新 CSG 配件" : "正在构建 CSG 配件", async () => {
      const method = draft.mode === "edit" ? "TubeDesigner.UpdateComponentCSGModel" : "TubeDesigner.CreateComponentCSGModel";
      const response = await productInvoke(context, method, payload);
      updateModel(view, response?.model); state.csgDraft = null; state.scope = "user"; state.search = "";
      ops.showNotice?.(context, view, `${draft.mode === "edit" ? "已更新" : "已保存"}“${modelName(response.model)}”，CSG 构造树可继续编辑。`);
      return response.model;
    }) };
  }
  if (suffix === "select") {
    const key = String(target?.dataset?.componentKey ?? "");
    if (getVisibleComponentModels(view).some((model) => componentModelKey(model) === key)) {
      state.selectedKey = key; state.selectedByScope[state.scope] = key; state.previewError = ""; state.previewFailureKey = "";
    }
  } else if (suffix === "scope") {
    const scope = target?.dataset?.componentScope;
    if (SCOPES.includes(scope) && scope !== state.scope) {
      selectedModel(view);
      state.scope = scope; state.selectedKey = ""; state.previewError = ""; state.previewFailureKey = "";
    }
  } else if (suffix === "search") { state.search = String(target?.value ?? ""); state.previewError = ""; state.previewFailureKey = ""; }
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
      state.scope = "user";
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
  selectedModel(view);
  ops.renderProject(context, view);
  return { handled: true };
}

export async function handleComponentLibraryRibbonCommand(context, view, commandId, ops) {
  const state = componentLibraryState(view);
  if (state.csgDraft && (commandId === "components.import" || commandId === "components.export-step")) {
    ops.showNotice?.(context, view, "请先完成当前零件，或退出绘制。");
    return true;
  }
  if (commandId === "components.import") { await chooseComponentModel(context, view, ops); return true; }
  if (commandId === "components.draw") {
    if (!state.csgDraft) openComponentCSGEditor(view);
    ops.renderProject(context, view); return true;
  }
  if (commandId === "components.export-step") { await exportComponentModel(context, view, ops); return true; }
  return false;
}

function previewKey(model) { return model ? JSON.stringify([model.scope, model.scope === "template" ? model.templateId : "", model.id, model.revision]) : ""; }

export async function ensureComponentModelPreview(context, view, ops = null) {
  const state = componentLibraryState(view);
  if (state.csgDraft) return;
  if (view.activeAreaId !== "components" || !view.viewport?.applyViewSnapshot) return;
  const model = selectedModel(view);
  const key = previewKey(model);
  if (!model) {
    state.previewRequest = null;
    state.previewError = "";
    state.previewFailureKey = "";
    view.viewport.setVisibleEntityIds?.([]);
    view.viewport.setSelectedObjectIds?.([], "");
    view.viewport.setDimensionAnnotations?.([]);
    if (state.emptyPreviewRequest) return state.emptyPreviewRequest.promise;
    if (view.viewport.getAppliedViewState?.().revision === "components-empty") return;
    const empty = { promise: null };
    state.emptyPreviewRequest = empty;
    state.applyTail = Promise.resolve(state.applyTail).catch(() => {}).then(async () => {
      if (state.emptyPreviewRequest !== empty || view.activeAreaId !== "components" || selectedModel(view)) return;
      await view.viewport.applyViewSnapshot({ revision: "components-empty", rows: [] }, context.sceneProxy?.resources);
    });
    empty.promise = state.applyTail.finally(() => { if (state.emptyPreviewRequest === empty) state.emptyPreviewRequest = null; });
    await empty.promise;
    return;
  }
  state.emptyPreviewRequest = null;
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
  const request = { key, promise: null, progressVisible: false, shownAt: 0, phase: "生成三维资源" };
  state.previewRequest = request;
  state.previewError = "";
  view.viewport.setVisibleEntityIds?.([]);
  const current = () => state.previewRequest === request && !state.csgDraft
    && view.activeAreaId === "components" && previewKey(selectedModel(view)) === key;
  // Match the profile library: avoid flashing on fast/cache hits, and keep a
  // shown indicator visible long enough to be readable.
  const progressTimer = setTimeout(() => {
    if (!current()) return;
    request.progressVisible = true;
    request.shownAt = Date.now();
    ops?.renderProject?.(context, view);
  }, 200);
  request.promise = (async () => {
    try {
      if (typeof context.sceneProxy?.invoke !== "function") throw new Error("当前场景未提供配件预览接口。");
      const payload = { scope: model.scope, id: model.id, ...(model.scope === "template" ? { templateId: model.templateId } : {}) };
      const response = cached ?? await context.sceneProxy.invoke("TubeDesigner.GenerateComponentModelPreview", payload, { timeoutMs: 120000 });
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
        request.phase = "装载并显示三维模型";
        if (request.progressVisible) ops?.renderProject?.(context, view);
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
      clearTimeout(progressTimer);
      if (request.progressVisible && current()) {
        const remaining = 500 - (Date.now() - request.shownAt);
        if (remaining > 0) await new Promise(resolve => setTimeout(resolve, remaining));
      }
      if (state.previewRequest === request) state.previewRequest = null;
      if (view.activeAreaId === "components") ops?.renderProject?.(context, view);
    }
  })();
  ops?.renderProject?.(context, view);
  return request.promise;
}

function csgPreviewSignature(draft) {
  return JSON.stringify([draft.selectedId, draft.features]);
}

function updateCSGPreviewStatus(controller, draft) {
  const status = controller.pane?.querySelector?.("[data-component-csg-preview-status]");
  if (!status) return;
  status.className = `tube-component-csg-preview-status ${draft.previewStatus ?? "idle"}`;
  if (draft.previewStatus === "error") {
    status.setAttribute("role", "alert");
    status.removeAttribute("aria-live");
  } else {
    status.removeAttribute("role");
    status.setAttribute("aria-live", "polite");
  }
  const label = status.querySelector("span");
  if (label) label.textContent = csgPreviewLabel(draft);
  const save = controller.dialog?.querySelector?.("[data-component-csg-save]");
  if (save) save.disabled = Boolean(controller.view?.pending
    || draft.previewStatus === "loading" || draft.previewStatus === "error");
}

function disposeComponentCSGViewport(mount) {
  if (!mount || (typeof mount !== "object" && typeof mount !== "function")) return;
  const controller = csgViewportControllers.get(mount);
  if (!controller) return;
  controller.disposed = true;
  if (controller.previewTimer) clearTimeout(controller.previewTimer);
  if (controller.ownsViewport) controller.viewport.dispose?.();
  csgViewportControllers.delete(mount);
}

function selectedCSGFeature(draft) {
  return draft.features.find((feature) => feature.id === draft.selectedId) ?? draft.features[0] ?? null;
}

function roundCSGDrag(value) {
  return Math.round(csgNumber(value) * 100) / 100;
}

function updateCSGFeatureInputs(controller, feature) {
  for (const group of ["position", "rotation"]) {
    for (const axis of ["x", "y", "z"]) {
      const input = controller.pane?.querySelector?.(`[data-csg-feature-field="${group}.${axis}"]`);
      if (input) input.value = String(roundCSGDrag(feature.transform?.[group]?.[axis]));
    }
  }
  for (const [key, value] of Object.entries(feature.parameters ?? {})) {
    const input = controller.pane?.querySelector?.(`[data-csg-feature-field="parameters.${key}"]`);
    if (input) input.value = String(roundCSGDrag(value));
  }
}

function applyCSGDrag(controller, event) {
  const drag = controller.drag;
  const draft = controller.view?.tubeDesignerComponentLibrary?.csgDraft;
  const feature = draft && selectedCSGFeature(draft);
  if (!drag || !feature || feature.id !== drag.featureId) return;
  const dx = Number(event.clientX) - drag.startX;
  const dy = Number(event.clientY) - drag.startY;
  if (draft.tool === "move") {
    const scale = drag.unitsPerPixel;
    feature.transform.position.x = roundCSGDrag(drag.position.x + (event.shiftKey ? 0 : dx * scale));
    feature.transform.position.y = roundCSGDrag(drag.position.y + (event.shiftKey ? dx * scale : 0));
    feature.transform.position.z = roundCSGDrag(drag.position.z - dy * scale);
  } else if (draft.tool === "rotate") {
    feature.transform.rotation.x = roundCSGDrag(drag.rotation.x - dy * .5);
    feature.transform.rotation.y = roundCSGDrag(drag.rotation.y + (event.shiftKey ? dx * .5 : 0));
    feature.transform.rotation.z = roundCSGDrag(drag.rotation.z + (event.shiftKey ? 0 : dx * .5));
  } else if (draft.tool === "scale") {
    const factor = Math.max(.01, Math.min(100, Math.exp((dx - dy) * .006)));
    for (const [key, start] of Object.entries(drag.parameters)) {
      const minimum = feature.primitive === "cone" && (key === "bottomRadius" || key === "topRadius") ? 0 : .01;
      feature.parameters[key] = roundCSGDrag(Math.max(minimum, Math.min(100000, start * factor)));
    }
  }
  draft.dirty = true;
  updateCSGFeatureInputs(controller, feature);
  const now = performance.now();
  if (now - controller.lastDragPreviewAt > 120) {
    controller.lastDragPreviewAt = now;
    scheduleComponentCSGPreview(controller, 0);
  }
}

function bindCSGManipulation(controller, surface) {
  if (controller.surface === surface) return;
  controller.surface = surface;
  surface.addEventListener("pointerdown", (event) => {
    const draft = controller.view?.tubeDesignerComponentLibrary?.csgDraft;
    const feature = draft && selectedCSGFeature(draft);
    if (!feature || draft.tool === "view" || controller.view.pending) return;
    const bounds = csgFeatureBounds(feature);
    controller.drag = {
      pointerId: event.pointerId, featureId: feature.id,
      startX: Number(event.clientX), startY: Number(event.clientY),
      unitsPerPixel: Math.max(.05, Math.max(bounds.width, bounds.depth, bounds.height) / 250),
      position: cloneValue(feature.transform.position), rotation: cloneValue(feature.transform.rotation),
      parameters: cloneValue(feature.parameters),
    };
    controller.lastDragPreviewAt = 0;
    surface.classList.add("dragging");
    surface.setPointerCapture?.(event.pointerId);
    event.preventDefault();
  });
  surface.addEventListener("pointermove", (event) => {
    if (controller.drag?.pointerId !== event.pointerId) return;
    applyCSGDrag(controller, event);
    event.preventDefault();
  });
  const finish = (event) => {
    if (controller.drag?.pointerId !== event.pointerId) return;
    applyCSGDrag(controller, event);
    controller.drag = null;
    surface.classList.remove("dragging");
    surface.releasePointerCapture?.(event.pointerId);
    scheduleComponentCSGPreview(controller, 0);
  };
  surface.addEventListener("pointerup", finish);
  surface.addEventListener("pointercancel", finish);
}

function scheduleComponentCSGPreview(controller, delay = 90) {
  const draft = controller.view?.tubeDesignerComponentLibrary?.csgDraft;
  if (!draft || controller.disposed) return;
  const signature = csgPreviewSignature(draft);
  controller.latestSignature = signature;
  if (signature === controller.appliedSignature) {
    draft.previewStatus = "ready";
    draft.previewError = "";
    updateCSGPreviewStatus(controller, draft);
    return;
  }
  if (signature === controller.failedSignature && !controller.previewRunning) {
    updateCSGPreviewStatus(controller, draft);
    return;
  }
  draft.previewStatus = "loading";
  draft.previewError = "";
  updateCSGPreviewStatus(controller, draft);
  if (controller.previewRunning) return;
  if (controller.previewTimer) clearTimeout(controller.previewTimer);
  controller.previewTimer = setTimeout(() => {
    controller.previewTimer = null;
    void runComponentCSGPreview(controller);
  }, Math.max(0, Number(delay) || 0));
}

async function runComponentCSGPreview(controller) {
  const draft = controller.view?.tubeDesignerComponentLibrary?.csgDraft;
  if (!draft || controller.disposed || controller.previewRunning) return;
  const signature = controller.latestSignature ?? csgPreviewSignature(draft);
  controller.previewRunning = true;
  try {
    if (typeof controller.context.sceneProxy?.invoke !== "function")
      throw new Error("当前场景没有提供 CSG 实时计算能力。");
    const response = await controller.context.sceneProxy.invoke("TubeDesigner.GenerateComponentCSGPreview", {
      csgDefinition: csgDefinition(draft), selectedFeatureId: draft.selectedId,
    }, { timeoutMs: 120000 });
    if (controller.disposed || controller.view?.tubeDesignerComponentLibrary?.csgDraft !== draft
      || controller.latestSignature !== signature) return;
    const geometryId = String(response?.geometryResourceId ?? "");
    const geometryVersion = Number(response?.geometryResourceVersion);
    const resultIsValid = response?.valid !== false && geometryId
      && Number.isFinite(geometryVersion) && geometryVersion > 0;
    let resultData = null;
    if (resultIsValid) {
      resultData = { geometry: { url: geometryId, version: geometryVersion }, geometryKind: 1,
        renderClass: 1, visible: true, selectable: false };
      if (response.materialResourceId && Number(response.materialResourceVersion) > 0)
        resultData.material = { url: String(response.materialResourceId), version: Number(response.materialResourceVersion) };
      controller.lastResultData = cloneValue(resultData);
    } else if (response?.resultError && controller.lastResultData) {
      resultData = cloneValue(controller.lastResultData);
    } else if (!response?.resultError) {
      throw new Error("几何内核没有返回有效的 CSG 实体。");
    }
    const rows = resultData ? [{ entityId: "component-csg-live-result", data: resultData }] : [];
    const operandId = String(response?.operandGeometryResourceId ?? "");
    const operandVersion = Number(response?.operandGeometryResourceVersion);
    if (operandId && Number.isFinite(operandVersion) && operandVersion > 0) {
      const operandData = { geometry: { url: operandId, version: operandVersion }, geometryKind: 1,
        renderClass: 1, visible: true, selectable: false };
      if (response.operandMaterialResourceId && Number(response.operandMaterialResourceVersion) > 0)
        operandData.material = { url: String(response.operandMaterialResourceId), version: Number(response.operandMaterialResourceVersion) };
      rows.push({ entityId: "component-csg-live-operand", data: operandData });
    }
    if (!rows.length) throw new Error(response?.resultError || "没有可显示的 CSG 几何。");
    const shownResult = resultData?.geometry ?? {};
    const revision = `component-csg-live:${shownResult.url ?? "invalid"}:${shownResult.version ?? 0}:${operandVersion || 0}`;
    const receipt = await controller.viewport.applyViewSnapshot({ revision, rows }, controller.context.sceneProxy.resources);
    if (controller.disposed || controller.latestSignature !== signature) return;
    if (!receipt?.applied || (resultData && receipt.entityIds
      && !receipt.entityIds.includes("component-csg-live-result")))
      throw new Error("CSG 几何没有进入三维视口。");
    controller.viewport.setVisibleEntityIds?.(rows.map((row) => row.entityId));
    controller.viewport.setSelectedObjectIds?.([], "");
    controller.viewport.setDimensionAnnotations?.([]);
    if (!controller.hasFitted) {
      controller.viewport.setStandardView?.("iso");
      controller.viewport.fitViewToViewport?.(1.3);
      controller.hasFitted = true;
    }
    if (!resultIsValid) {
      controller.failedSignature = signature;
      draft.previewStatus = "error";
      draft.previewError = `当前组合无有效实体：${response.resultError}`;
      updateCSGPreviewStatus(controller, draft);
      return;
    }
    controller.appliedSignature = signature;
    controller.failedSignature = "";
    draft.previewBounds = response.bounds ?? null;
    draft.previewStatus = "ready";
    draft.previewError = "";
    updateCSGPreviewStatus(controller, draft);
  } catch (error) {
    if (!controller.disposed && controller.latestSignature === signature
      && controller.view?.tubeDesignerComponentLibrary?.csgDraft === draft) {
      controller.failedSignature = signature;
      draft.previewStatus = "error";
      draft.previewError = `当前组合无有效实体：${error?.message ?? error}`;
      updateCSGPreviewStatus(controller, draft);
    }
  } finally {
    controller.previewRunning = false;
    if (!controller.disposed && controller.view?.tubeDesignerComponentLibrary?.csgDraft
      && controller.latestSignature !== signature) scheduleComponentCSGPreview(controller, 0);
  }
}

export function ensureComponentCSGPreview(context, view, mount) {
  if (!mount || (typeof mount !== "object" && typeof mount !== "function")) return null;
  const draft = componentLibraryState(view).csgDraft;
  const surface = mount.querySelector?.("[data-component-csg-manipulation]") ?? null;
  const viewport = view.viewport;
  let controller = csgViewportControllers.get(mount);
  if (!draft || !surface || !viewport?.applyViewSnapshot) {
    disposeComponentCSGViewport(mount);
    return null;
  }
  if (controller && controller.viewport !== viewport) {
    disposeComponentCSGViewport(mount);
    controller = null;
  }
  if (!controller) {
    controller = {
      context, view, pane: null, surface: null, previewTimer: null,
      previewRunning: false, latestSignature: "", appliedSignature: "", failedSignature: "",
      hasFitted: false, disposed: false, drag: null, lastDragPreviewAt: 0, ownsViewport: false,
      viewport,
    };
    csgViewportControllers.set(mount, controller);
  }
  controller.context = context;
  controller.view = view;
  controller.pane = mount;
  controller.dialog = mount;
  bindCSGManipulation(controller, surface);
  scheduleComponentCSGPreview(controller);
  return controller;
}

export function attachComponentLibrary(context, view, mount, ops) {
  const state = componentLibraryState(view);
  const designer = view.scene?.tubeDesigner ?? {};
  const templateId = view.tubeDesignerAddDialogOpen ? view.tubeDesignerAddTemplateId : designer.product?.templateId;
  const template = (designer.templates ?? []).find((item) => item.id === templateId);
  const needsModels = view.activeAreaId === "components" || (template?.parameters ?? []).some((field) => field.presentation?.editor === "component-model");
  if (needsModels && state.loadState === "idle") queueMicrotask(() => {
    if (state.loadState === "idle") void refreshComponentModels(context, view, ops);
  });
  if (view.activeAreaId === "components" && !state.csgDraft)
    queueMicrotask(() => { void ensureComponentModelPreview(context, view, ops).catch(() => {}); });
  queueMicrotask(() => { ensureComponentCSGPreview(context, view, mount); });
}
