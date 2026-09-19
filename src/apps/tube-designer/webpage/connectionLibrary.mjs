import { availableParameterChoices, parameterEnabled, parameterVisible } from "./parameterConditions.mjs";
import { connectionCategoryOrder, connectionParameterDefaults, connectionProcessCatalog, connectionTemplateById } from "./connectionCatalog.mjs";

export function connectionLibraryState(view) {
  const state = view.tubeDesignerConnectionLibrary ??= {
    search: "", selectedId: "tab-slot-lock", collapsed: [], parameterDrafts: {},
  };
  if (!connectionTemplateById(state.selectedId)) state.selectedId = connectionProcessCatalog[0]?.id ?? "";
  return state;
}

export function selectedConnectionTemplate(view) {
  return connectionTemplateById(connectionLibraryState(view).selectedId) ?? connectionProcessCatalog[0];
}

export function connectionParameterValues(view, template = selectedConnectionTemplate(view)) {
  return { ...connectionParameterDefaults(template), ...(connectionLibraryState(view).parameterDrafts[template?.id] ?? {}) };
}

export function renderConnectionLibraryLeftPane(_context, view) {
  const state = connectionLibraryState(view);
  const search = state.search.trim().toLocaleLowerCase("zh-CN");
  const visible = connectionProcessCatalog.filter((item) => !search || connectionSearchText(item).includes(search));
  const groups = connectionCategoryOrder.map(([id, label]) => {
    const items = visible.filter((item) => item.category === id);
    if (!items.length) return "";
    const collapsed = state.collapsed.includes(id);
    return `<section class="tube-connection-library-group"><button type="button" class="tube-connection-library-group-heading" data-action="tube-designer-connection-toggle-category" data-tube-connection-category="${attr(id)}" aria-expanded="${!collapsed}"><span>${collapsed ? "▸" : "▾"} ${text(label)}</span><small>${items.length}</small></button><div class="tube-connection-library-cards"${collapsed ? " hidden" : ""}>${items.map((item) => connectionCard(item, state.selectedId)).join("")}</div></section>`;
  }).join("");
  return `<section class="tube-profile-library-panel tube-connection-library-panel"><header class="tube-profile-library-heading"><div><strong>连接库</strong><span>${connectionProcessCatalog.length} 个系统连接 · 复用单件模具</span></div></header><label class="tube-connection-library-search"><span class="sr-only">搜索连接</span><input type="search" value="${attr(state.search)}" placeholder="搜索连接、接口、锁止方式" data-action="tube-designer-connection-search"></label><div class="tube-connection-library-list">${groups || `<div class="tube-profile-library-empty"><strong>没有匹配的连接</strong><span>换一个关键词试试</span></div>`}</div></section>`;
}

export function renderConnectionLibraryRightPane(_context, view) {
  const template = selectedConnectionTemplate(view);
  if (!template) return `<div class="tube-profile-library-editor-empty"><strong>暂无连接模板</strong></div>`;
  const values = connectionParameterValues(view, template);
  const basic = template.parameters.filter((item) => item.level !== "advanced" && parameterVisible(item, values));
  const advanced = template.parameters.filter((item) => item.level === "advanced" && parameterVisible(item, values));
  return `<section class="tube-connection-library-editor"><header class="tube-connection-library-editor-heading"><div><strong>${text(template.displayName)}</strong><span>${text(template.categoryName)} · ${text(template.topology)}</span></div><b>组合工艺</b></header><div class="tube-connection-library-editor-body"><p class="tube-connection-library-intro">${text(template.summary)}</p>${parameterSection("基本参数", basic, values, template.id, false)}${advanced.length ? parameterSection("高级设置", advanced, values, template.id, true) : ""}${renderParticipants(template)}${renderOperations(template, values)}${renderAssembly(template, values)}${renderOutputs(template)}</div></section>`;
}

export function renderConnectionLibraryViewportOverlay(_context, view) {
  const template = selectedConnectionTemplate(view);
  if (!template) return "";
  const values = connectionParameterValues(view, template);
  return `<div class="tube-connection-library-hud"><strong>${text(template.displayName)}</strong><span>${text(template.interfaceType)} · ${text(template.lockType)}</span><small>连接模板组合 ${template.operations.length} 道单件模具工艺</small></div><section class="tube-connection-library-stage" aria-label="连接关系示意图"><header><strong>连接关系示意</strong><span>${text(template.topology)}</span></header>${connectionIllustration(template, values)}<footer>${template.participants.map((item, index) => `<span><i>${index + 1}</i>${text(item.label)}</span>`).join("")}<b>→</b><span class="result">连接结果</span></footer></section>`;
}

export async function handleConnectionLibraryAction(_context, view, action, target, ops) {
  const state = connectionLibraryState(view);
  if (action === "tube-designer-connection-search") {
    state.search = String(target?.value ?? "");
    ops.renderProject(_context, view);
    return { handled: true };
  }
  if (action === "tube-designer-connection-toggle-category") {
    const id = String(target?.dataset?.tubeConnectionCategory ?? "");
    state.collapsed = state.collapsed.includes(id) ? state.collapsed.filter((item) => item !== id) : [...state.collapsed, id];
    ops.renderProject(_context, view);
    return { handled: true };
  }
  if (action === "tube-designer-connection-select") {
    const id = String(target?.dataset?.tubeConnectionId ?? "");
    if (connectionTemplateById(id)) state.selectedId = id;
    ops.renderProject(_context, view);
    return { handled: true };
  }
  if (action === "tube-designer-connection-parameter-change") {
    const template = connectionTemplateById(String(target?.dataset?.tubeConnectionId ?? ""));
    const key = String(target?.dataset?.tubeConnectionParameter ?? "");
    const definition = template?.parameters.find((item) => item.key === key);
    if (template && definition) {
      const value = definition.valueType === "boolean" ? !!target.checked : definition.valueType === "choice" ? String(target.value ?? "") : Number(target.value);
      if (definition.valueType !== "number" || Number.isFinite(value)) {
        state.parameterDrafts[template.id] = { ...(state.parameterDrafts[template.id] ?? {}), [key]: value };
        ops.renderProject(_context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-connection-reset") {
    const id = String(target?.dataset?.tubeConnectionId ?? state.selectedId);
    delete state.parameterDrafts[id];
    ops.renderProject(_context, view);
    return { handled: true };
  }
  return { handled: false };
}

function connectionCard(item, selectedId) {
  return `<button type="button" class="tube-connection-library-card${item.id === selectedId ? " selected" : ""}" data-action="tube-designer-connection-select" data-tube-connection-id="${attr(item.id)}"><span class="tube-connection-library-card-art">${miniIllustration(item.illustration)}</span><span><strong>${text(item.displayName)}</strong><small>${text(item.interfaceType)} · ${text(item.lockType)}</small></span></button>`;
}

function parameterSection(title, definitions, values, templateId, advanced) {
  if (!definitions.length) return "";
  const fields = definitions.map((item) => parameterField(item, values, templateId)).join("");
  if (advanced) return `<details class="tube-connection-library-parameter-section" open><summary><span>${title}</span><small>${definitions.length} 项</small></summary><div class="tube-connection-library-parameter-grid">${fields}</div></details>`;
  return `<section class="tube-connection-library-parameter-section basic"><header><strong>${title}</strong><small>${definitions.length} 项</small></header><div class="tube-connection-library-parameter-grid">${fields}</div></section>`;
}

function parameterField(definition, values, templateId) {
  const enabled = parameterEnabled(definition, values);
  const data = `data-action="tube-designer-connection-parameter-change" data-tube-connection-id="${attr(templateId)}" data-tube-connection-parameter="${attr(definition.key)}"`;
  if (definition.valueType === "boolean") return `<label class="tube-connection-library-check"><input type="checkbox" ${data}${values[definition.key] ? " checked" : ""}${enabled ? "" : " disabled"}><span>${text(definition.displayName)}</span></label>`;
  if (definition.valueType === "choice") return `<label><span>${text(definition.displayName)}</span><select ${data}${enabled ? "" : " disabled"}>${availableParameterChoices(definition, values).map((option) => `<option value="${attr(option.value)}"${String(option.value) === String(values[definition.key]) ? " selected" : ""}>${text(option.label ?? option.value)}</option>`).join("")}</select></label>`;
  return `<label><span>${text(definition.displayName)}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="number" value="${attr(values[definition.key])}" min="${attr(definition.min)}" max="${attr(definition.max)}" step="${attr(definition.step ?? 0.1)}" ${data}${enabled ? "" : " disabled"}></label>`;
}

function renderParticipants(template) {
  return `<section class="tube-connection-library-block"><header><strong>参与零件</strong><span>${template.participants.length} 个角色</span></header><div class="tube-connection-library-role-list">${template.participants.map((item, index) => `<article><i>${index + 1}</i><div><strong>${text(item.label)}</strong><small>${text(item.responsibility)}</small></div></article>`).join("")}</div></section>`;
}

function renderOperations(template, values) {
  return `<section class="tube-connection-library-block"><header><strong>单件工艺复用</strong><span>${template.operations.length} 道</span></header><div class="tube-connection-library-operation-list">${template.operations.map((item) => { const part = template.participants.find((role) => role.role === item.role); return `<article><span>${text(part?.label ?? item.role)}</span><div><strong>${text(item.label)}</strong><small>模具：${text(item.resource.displayName ?? item.resource.id)}</small></div><dl>${Object.entries(item.parameterBindings ?? {}).map(([key, value]) => `<div><dt>${text(bindingName(key))}</dt><dd>${text(resolveBinding(value, values))}</dd></div>`).join("")}</dl></article>`; }).join("")}</div><p class="tube-connection-library-reuse-note">这里只保存模具引用和参数映射；孔、槽、端部成形的几何仍由模具模板统一生成。</p></section>`;
}

function renderAssembly(template, values) {
  return `<section class="tube-connection-library-block"><header><strong>装配与锁止</strong><span>${template.motionType}</span></header><ol class="tube-connection-library-steps">${template.assemblyPath.map((item) => `<li><i>${text(displayBinding(item.kind, values, template))}</i><span><strong>${text(item.label)}</strong><small>${text(displayBinding(item.direction, values, template))}${item.distance ? ` · ${text(displayBinding(item.distance, values, template))}` : ""}</small></span></li>`).join("")}</ol></section>`;
}

function renderOutputs(template) {
  return `<section class="tube-connection-library-block"><header><strong>连接输出</strong><button type="button" data-action="tube-designer-connection-reset" data-tube-connection-id="${attr(template.id)}">恢复默认</button></header><div class="tube-connection-library-output-grid"><span><b>零件几何</b>${template.outputs.partGeometry.map(text).join("、")}</span><span><b>后续工艺</b>${template.outputs.secondaryProcess.map(text).join("、")}</span><span><b>BOM</b>${template.outputs.bom.map(text).join("、")}</span><span><b>装配指导</b>${template.outputs.assemblyInstruction ? "生成" : "不生成"}</span></div></section>`;
}

function connectionIllustration(template, values) {
  const label = text(template.displayName);
  if (template.illustration === "tab-slot") return `<svg viewBox="0 0 520 290" role="img" aria-label="${label}"><defs>${markers()}</defs><g class="part-a"><path d="M60 92h145v38h50v34h-50v38H60Z"/></g><g class="part-b"><path d="M465 72H292v62h55v26h-55v62h173Z"/></g><path class="motion" d="M220 147h75" marker-end="url(#connection-arrow)"/><text x="260" y="128">插入 ${text(values.insertDepth)} mm</text></svg>`;
  if (template.illustration === "bolt") return `<svg viewBox="0 0 520 290" role="img" aria-label="${label}"><defs>${markers()}</defs><g class="part-a"><rect x="70" y="64" width="310" height="58" rx="9"/><circle class="hole" cx="180" cy="93" r="13"/><circle class="hole" cx="300" cy="93" r="13"/></g><g class="part-b"><rect x="140" y="168" width="310" height="58" rx="9"/><circle class="hole" cx="180" cy="197" r="13"/><circle class="hole" cx="300" cy="197" r="13"/></g><path class="motion" d="M180 126v53M300 126v53" marker-end="url(#connection-arrow)"/><text x="365" y="149">${text(values.boltCount)} × M${text(values.boltDiameter)}</text></svg>`;
  if (template.illustration === "slot-bolt") return `<svg viewBox="0 0 520 290" role="img" aria-label="${label}"><defs>${markers()}</defs><g class="part-a"><rect x="65" y="68" width="390" height="62" rx="9"/><rect class="hole" x="194" y="87" width="132" height="24" rx="12"/></g><g class="part-b"><rect x="180" y="170" width="160" height="56" rx="9"/><circle class="hole" cx="260" cy="198" r="13"/></g><path class="motion" d="M205 151h110" marker-start="url(#connection-arrow-back)" marker-end="url(#connection-arrow)"/><text x="260" y="145" text-anchor="middle">可调 ${text(values.adjustment)} mm</text></svg>`;
  return `<svg viewBox="0 0 520 290" role="img" aria-label="${label}"><defs>${markers()}</defs><g class="part-b"><rect x="48" y="175" width="424" height="62" rx="31"/></g><g class="part-a"><path d="M210 38h100v112c-12 19-30 28-50 28s-38-9-50-28Z"/></g><path class="weld" d="M205 178q55-36 110 0"/><path class="motion" d="M260 115v43" marker-end="url(#connection-arrow)"/><text x="330" y="100">${text(values.intersectionAngle)}°</text></svg>`;
}

function miniIllustration(kind) {
  if (kind === "bolt") return `<svg viewBox="0 0 48 48"><rect x="5" y="9" width="28" height="10" rx="2"/><rect x="15" y="29" width="28" height="10" rx="2"/><path d="M20 16v17M30 16v17"/></svg>`;
  if (kind === "slot-bolt") return `<svg viewBox="0 0 48 48"><rect x="4" y="9" width="40" height="12" rx="2"/><rect x="15" y="12" width="18" height="6" rx="3"/><circle cx="24" cy="34" r="5"/></svg>`;
  if (kind === "saddle") return `<svg viewBox="0 0 48 48"><path d="M19 5h12v22q-6 8-12 0Z"/><path d="M5 34q19-13 38 0v8H5Z"/></svg>`;
  return `<svg viewBox="0 0 48 48"><path d="M4 12h16v8h8v8h-8v8H4M44 8H28v14h8v4h-8v14h16"/></svg>`;
}

function markers() { return `<marker id="connection-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 10 5 0 10Z"/></marker><marker id="connection-arrow-back" viewBox="0 0 10 10" refX="1" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M10 0 0 5 10 10Z"/></marker>`; }
function connectionSearchText(item) { return [item.displayName, item.categoryName, item.summary, item.topology, item.interfaceType, item.lockType, ...item.operations.map((op) => op.resource.id)].join(" ").toLocaleLowerCase("zh-CN"); }
function resolveBinding(value, values) {
  if (typeof value !== "string" || !value.includes("$")) return String(value ?? "");
  const expression = value.replace(/\$([A-Za-z][A-Za-z0-9_]*)/g, (_, key) => String(Number(values[key] ?? 0)));
  if (/^[\d+\-*/().\s]+$/.test(expression)) {
    try { return String(Function(`"use strict";return (${expression})`)()); } catch { /* show expression below */ }
  }
  return value.replace(/\$([A-Za-z][A-Za-z0-9_]*)/g, (_, key) => String(values[key] ?? key));
}
function displayBinding(value, values, template) {
  const resolved = resolveBinding(value, values);
  const key = typeof value === "string" && /^\$[A-Za-z]/.test(value) ? value.slice(1) : "";
  const definition = template?.parameters?.find((item) => item.key === key);
  const option = definition?.options?.find((item) => String(item.value) === String(resolved));
  if (option) return option.label;
  return ({ "joint.x": "连接坐标 X", "joint.y": "连接坐标 Y", "joint.z": "连接轴 Z", x: "连接坐标 X", y: "连接坐标 Y" })[resolved] ?? resolved;
}
function bindingName(key) { return ({ width: "宽度", depth: "深度", clearance: "间隙", diameter: "孔径", count: "数量", pitch: "间距", length: "长度", angle: "角度" })[key] ?? key; }
function text(value) { return String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;"); }
function attr(value) { return text(value).replaceAll('"', "&quot;"); }
