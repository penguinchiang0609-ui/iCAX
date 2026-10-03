import { availableParameterChoices, parameterEnabled, parameterVisible } from "./parameterConditions.mjs";
import { finishedProductInput } from "./finishedProductModel.mjs";
import { libraryProfiles, profileName, profileRef, profileScope, profileSelectionKey } from "./profileLibrary.mjs";

const clone = (value) => structuredClone(value);
const text = (value) => String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
const attr = (value) => text(value).replaceAll('"', "&quot;");
const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const defaults = (template) => Object.fromEntries((template.parameters ?? []).filter((p) => p.scope !== "scene")
  .map((p) => [p.key, clone(p.defaultValue)]));
const profileDefaults = (profile) => ({ ...Object.fromEntries((profile?.descriptor?.parameters ?? [])
  .filter((p) => p.defaultValue !== undefined).map((p) => [p.key, clone(p.defaultValue)])), ...clone(profile?.defaultParameters ?? {}) });
export const supportsStockOperations = (template) => template?.inputContract?.supportedInputModes?.includes("stock-operation");

export function stockProcessState(view, template) {
  if (!view.tubeDesignerStockProcess) {
    const product = finishedProductInput(view), spans = Object.values(product.spans);
    const source = spans[0];
    const profile = libraryProfiles(view).find((item) => profileScope(item) === source.profileRef.scope
      && String(item.id) === String(source.profileRef.id));
    const length = spans.reduce((total, part) => total + part.length, 0);
    const instanceId = "stock-operation-1";
    view.tubeDesignerStockProcess = { stock: { id: "continuous-stock", label: "母材 1", ...clone(source),
      parameters: { ...profileDefaults(profile), ...clone(source.parameters) }, length },
      nextId: 2, revision: 0, planId: "", storedPlanDigest: "", selectedInstanceId: instanceId, instances: [{ instanceId, templateId: template.id,
        station: length / 2, angle: 90, rotation: 0,
        settings: { [template.id]: { parameters: defaults(template), processDrafts: {} } } }] };
  }
  return view.tubeDesignerStockProcess;
}

export function selectedStockOperation(view, template) {
  const state = stockProcessState(view, template);
  return state.instances.find((row) => row.instanceId === state.selectedInstanceId) ?? state.instances[0];
}

export function stockOperationSettings(view, template) {
  const row = selectedStockOperation(view, template);
  if (!row) return { parameters: defaults(template), processDrafts: {} };
  return row.settings[template.id] ??= { parameters: defaults(template), processDrafts: {} };
}

function field(definition, values) {
  if (!parameterVisible(definition, values)) return "";
  const key = definition.key, value = values[key] ?? definition.defaultValue ?? "";
  const enabled = parameterEnabled(definition, values);
  const data = `data-cam-change-action="tube-designer-stock-profile-parameter" data-stock-profile-parameter="${attr(key)}" data-profile-parameter-key="${attr(key)}"${enabled ? "" : " disabled"}`;
  const display = definition.displayName;
  const name = text(typeof display === "object" ? display["zh-CN"] ?? display.zh ?? display.en ?? key : display ?? key);
  if (definition.valueType === "boolean") return `<label class="tube-designer-field"><span>${name}</span><input type="checkbox" ${data}${value ? " checked" : ""}></label>`;
  if (definition.valueType === "choice" || definition.options || definition.choices)
    return `<label class="tube-designer-field"><span>${name}</span><select ${data}>${availableParameterChoices(definition, values).map((option) => {
      const v = typeof option === "object" ? option.value : option;
      return `<option value="${attr(v)}"${String(value) === String(v) ? " selected" : ""}>${text(option.label ?? option.displayName ?? v)}</option>`;
    }).join("")}</select></label>`;
  return `<label class="tube-designer-field"><span>${name}${definition.unit ? `（${text(definition.unit)}）` : ""}</span><input type="${definition.valueType === "string" ? "text" : "number"}" value="${attr(value)}" ${data}${definition.min == null ? "" : ` min="${attr(definition.min)}"`}${definition.max == null ? "" : ` max="${attr(definition.max)}"`}${definition.step == null ? "" : ` step="${attr(definition.step)}"`}></label>`;
}

export function renderStockProcessEditor(view, template, templates) {
  const state = stockProcessState(view, template), stock = state.stock;
  const profiles = libraryProfiles(view), profile = profiles.find((item) => profileScope(item) === stock.profileRef.scope
    && String(item.id) === String(stock.profileRef.id));
  const selectedKey = profile ? profileSelectionKey(profile) : `${stock.profileRef.scope}:${stock.profileRef.id}`;
  const fields = (profile?.descriptor?.parameters ?? []).map((definition) => field(definition, stock.parameters)).join("");
  const choices = templates.filter(supportsStockOperations);
  const rows = state.instances.map((row, index) => `<article class="tube-stock-operation" id="${attr(row.instanceId)}" data-stock-operation-id="${attr(row.instanceId)}"><header><strong>折弯 ${index + 1}</strong><button type="button" data-cam-action="tube-designer-stock-operation-select" data-stock-instance="${attr(row.instanceId)}" aria-pressed="${state.selectedInstanceId === row.instanceId}">加工设置</button><button type="button" data-cam-action="tube-designer-stock-operation-remove" data-stock-instance="${attr(row.instanceId)}">删除</button></header><div class="tube-connection-library-parameter-grid"><label class="tube-designer-field"><span>工艺</span><select data-cam-change-action="tube-designer-stock-operation-template" data-stock-instance="${attr(row.instanceId)}">${choices.map((item) => `<option value="${attr(item.id)}"${item.id === row.templateId ? " selected" : ""}>${text(item.displayName)}</option>`).join("")}</select></label>${[["station", "距母材起端（mm）", 0, stock.length], ["angle", "折弯角度（°，正负表示方向）", -170, 170], ["rotation", "折弯平面转角（°）", -180, 180]].map(([key, label, min, max]) => `<label class="tube-designer-field"><span>${label}</span><input type="number" value="${attr(row[key])}" min="${min}" max="${max}" step="0.1" data-cam-change-action="tube-designer-stock-operation-change" data-stock-instance="${attr(row.instanceId)}" data-stock-operation-parameter="${key}"></label>`).join("")}</div></article>`).join("");
  return `<section class="tube-connection-library-parameter-section basic" data-stock-process-editor><header><strong>连续母材</strong><small>1 根 · ${state.instances.length} 处折弯</small></header><details class="tube-assembly-scene-member" id="stock-profile" data-profile-parameter-scope open><summary><strong>母材截面</strong><small>${text(profile ? profileName(profile) : stock.profileRef.id)}</small></summary><div class="tube-connection-library-parameter-grid"><label class="tube-designer-field"><span>母材名称</span><input type="text" value="${attr(stock.label)}" maxlength="64" data-cam-change-action="tube-designer-stock-label"></label><label class="tube-designer-field"><span>管型</span><select data-cam-change-action="tube-designer-stock-profile">${profile ? "" : `<option value="${attr(selectedKey)}" selected>当前管型</option>`}${profiles.map((item) => `<option value="${attr(profileSelectionKey(item))}"${profileSelectionKey(item) === selectedKey ? " selected" : ""}>${text(profileName(item))}</option>`).join("")}</select></label><label class="tube-designer-field"><span>母材长度（mm）</span><input type="number" value="${attr(stock.length)}" min="1" max="100000" step="1" data-cam-change-action="tube-designer-stock-length"></label>${fields}</div></details><div class="tube-stock-operation-list" id="stock-operation-list">${rows}</div><button type="button" data-cam-action="tube-designer-stock-operation-add">添加折弯位置</button><p class="tube-assembly-product-angle">下面的加工参数属于当前选中的折弯位置。</p></section>`;
}

export function stockProcessPayload(view, template, templates) {
  const state = stockProcessState(view, template), stock = clone(state.stock);
  const instances = state.instances.map((row) => {
    const chosen = templates.find((item) => item.id === row.templateId);
    if (!supportsStockOperations(chosen)) throw new Error("折弯位置选择了不支持连续母材的工艺");
    const settings = row.settings[chosen.id] ??= { parameters: defaults(chosen), processDrafts: {} };
    return { instanceId: row.instanceId, templateId: chosen.id,
      processInput: { schema: "icax.assembly-process-input", schemaVersion: 1,
        parts: { stock: { ...clone(stock), matrix: clone(stock.matrix ?? identity) } },
        geometry: { angle: row.angle, station: row.station, rotation: row.rotation } },
      parameters: clone(settings.parameters), processDrafts: clone(settings.processDrafts),
      targets: { stock: stock.id } };
  });
  return { stocks: [stock], instances };
}

export function markStockProcessEdited(view, template) {
  const state = stockProcessState(view, template);
  state.revision = (state.revision ?? 0) + 1;
}

export function restoreStockProcessPlan(view, record, templates) {
  const source = record?.sourceInput;
  if (source?.stocks?.length !== 1 || !Array.isArray(source.instances))
    throw new Error("此编辑区支持一根连续母材；该保存方案的母材数量或加工类型不同。");
  const stock = clone(source.stocks[0]);
  const instances = source.instances.map((item) => {
    const template = templates.find((candidate) => candidate.id === item.templateId);
    const input = item.processInput, geometry = input?.geometry;
    if (!supportsStockOperations(template) || Object.keys(input?.parts ?? {}).join() !== "stock"
        || item.targets?.stock !== stock.id || input.parts.stock.id !== stock.id)
      throw new Error("此方案包含其他母材或连接加工，请在对应的加工编辑区打开。");
    if (![geometry?.station, geometry?.angle, geometry?.rotation].every(Number.isFinite))
      throw new Error("保存方案缺少折弯位置、角度或平面转角。");
    if (!stock.matrix && input.parts.stock.matrix) stock.matrix = clone(input.parts.stock.matrix);
    if (stock.matrix && JSON.stringify(input.parts.stock.matrix) !== JSON.stringify(stock.matrix))
      throw new Error("该方案为同一母材保存了不同姿态，请在对应加工编辑区打开。");
    return { instanceId: item.instanceId, templateId: item.templateId,
      station: geometry.station, angle: geometry.angle, rotation: geometry.rotation,
      settings: { [item.templateId]: { parameters: clone(item.parameters ?? {}),
        processDrafts: clone(item.processDrafts ?? {}) } } };
  });
  view.tubeDesignerStockProcess = { stock, instances, revision: 0,
    nextId: Math.max(0, ...instances.map((item) => Number(item.instanceId.match(/^stock-operation-(\d+)$/)?.[1]) || 0)) + 1,
    selectedInstanceId: instances[0]?.instanceId ?? "", planId: record.planId, storedPlanDigest: record.planDigest };
  return instances[0]?.templateId ?? view.tubeDesignerAssemblyLibrary.selectedId;
}

export function handleStockProcessAction(view, template, templates, action, target) {
  if (!action.startsWith("tube-designer-stock-")) return { handled: false };
  const state = stockProcessState(view, template);
  const row = state.instances.find((item) => item.instanceId === target?.dataset?.stockInstance);
  let selectedTemplateId = "", changed = false;
  if (action === "tube-designer-stock-operation-select" && row) {
    state.selectedInstanceId = row.instanceId; selectedTemplateId = row.templateId; changed = true;
  } else if (action === "tube-designer-stock-operation-add") {
    const instanceId = `stock-operation-${state.nextId++}`;
    const station = Math.min(state.stock.length - 1, (state.instances.at(-1)?.station ?? 0) + state.stock.length / 4);
    state.instances.push({ instanceId, templateId: template.id, station,
      angle: -(state.instances.at(-1)?.angle || 90), rotation: 0,
      settings: { [template.id]: { parameters: defaults(template), processDrafts: {} } } });
    state.selectedInstanceId = instanceId; changed = true;
  } else if (action === "tube-designer-stock-operation-remove" && row) {
    state.instances = state.instances.filter((item) => item !== row);
    if (state.selectedInstanceId === row.instanceId) state.selectedInstanceId = state.instances[0]?.instanceId ?? "";
    selectedTemplateId = state.instances.find((item) => item.instanceId === state.selectedInstanceId)?.templateId ?? ""; changed = true;
  } else if (action === "tube-designer-stock-operation-template" && row) {
    const chosen = templates.find((item) => item.id === String(target.value));
    if (supportsStockOperations(chosen)) {
      row.templateId = chosen.id; state.selectedInstanceId = row.instanceId; selectedTemplateId = chosen.id;
      row.settings[chosen.id] ??= { parameters: defaults(chosen), processDrafts: {} }; changed = true;
    }
  } else if (action === "tube-designer-stock-operation-change" && row) {
    const key = target.dataset.stockOperationParameter, value = Number(target.value);
    if (["station", "angle", "rotation"].includes(key) && Number.isFinite(value)) { row[key] = value; changed = true; }
  } else if (action === "tube-designer-stock-label") {
    state.stock.label = String(target.value).slice(0, 64); changed = true;
  } else if (action === "tube-designer-stock-length") {
    const value = Number(target.value);
    if (Number.isFinite(value) && value > 0 && value <= 100000) { state.stock.length = value; changed = true; }
  } else if (action === "tube-designer-stock-profile") {
    const chosen = libraryProfiles(view).find((item) => profileSelectionKey(item) === String(target.value));
    if (chosen) {
      state.stock.profileRef = profileRef(chosen);
      state.stock.parameters = profileDefaults(chosen); changed = true;
    }
  } else if (action === "tube-designer-stock-profile-parameter") {
    const profile = libraryProfiles(view).find((item) => profileScope(item) === state.stock.profileRef.scope
      && String(item.id) === String(state.stock.profileRef.id));
    const definition = profile?.descriptor?.parameters?.find((item) => item.key === target.dataset.stockProfileParameter);
    if (definition && parameterVisible(definition, state.stock.parameters) && parameterEnabled(definition, state.stock.parameters)) {
      const choices = definition.valueType === "choice" || definition.options || definition.choices;
      const option = choices && availableParameterChoices(definition, state.stock.parameters).find((item) =>
        String(typeof item === "object" ? item.value : item) === String(target.value));
      const value = definition.valueType === "boolean" ? !!target.checked
        : choices ? typeof option === "object" ? option?.value : option
        : definition.valueType === "string" ? String(target.value) : Number(target.value);
      if (value !== undefined && (!(definition.valueType === "number" || definition.valueType === "integer") || Number.isFinite(value))) {
        state.stock.parameters[definition.key] = value; changed = true;
      }
    }
  }
  if (changed && action !== "tube-designer-stock-operation-select") markStockProcessEdited(view, template);
  return { handled: true, changed, selectedTemplateId };
}
