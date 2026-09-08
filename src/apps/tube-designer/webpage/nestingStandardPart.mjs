import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";
import { libraryProfiles, profileRef, profileScope, profileSelectionKey, renderProfileSvg } from "./profileLibrary.mjs";
import { restoreSavedNestingTask } from "./nestingWorkflow.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";

const PREFIX = "tube-designer-nesting-standard-";
const DXF_KEY = "__dxf__";
const action = (name) => PREFIX + name;
const clone = (value) => JSON.parse(JSON.stringify(value));
const localized = (value, fallback = "") => typeof value === "object" && value
  ? String(value["zh-CN"] ?? value["en-US"] ?? Object.values(value)[0] ?? fallback) : String(value ?? fallback);
const snapshot = (profile) => profile?.previewProfile ?? profile?.profile ?? (profile?.contours ? profile : null);
const nameOf = (profile) => localized(profile?.name ?? profile?.descriptor?.displayName ?? snapshot(profile)?.name, "未命名管型");
const parametric = (profile) => !!profile && (profileScope(profile) === "system"
  || (profile.profileType ?? profile.kind) === "parametric-package");
const definitionsOf = (profile) => parametric(profile)
  ? profile?.descriptor?.parameters ?? snapshot(profile)?.parameterDefinitions ?? [] : [];

export function standardPartProfiles(view) {
  return libraryProfiles(view).filter((profile) => ["system", "user"].includes(profileScope(profile)) && profile.id);
}

function selectedProfile(view, draft) {
  return standardPartProfiles(view).find((profile) => profileSelectionKey(profile) === draft?.profileKey) ?? null;
}

function selectProfile(draft, profile) {
  draft.profileKey = profileSelectionKey(profile);
  draft.parameters = parametric(profile) ? clone({
    ...Object.fromEntries(definitionsOf(profile).filter((item) => item.key && item.defaultValue != null).map((item) => [item.key, item.defaultValue])),
    ...(snapshot(profile)?.parameters ?? {}),
    ...(profile.defaultParameters ?? {}),
  }) : {};
  draft.profile = snapshot(profile);
  draft.importedProfile = null;
  draft.previewInvalid = false;
  draft.error = "";
}

function openDialog(view) {
  const profiles = standardPartProfiles(view);
  const current = String(view.tubeDesignerSelectedProfileId ?? "");
  const profile = profiles.find((item) => profileSelectionKey(item) === current)
    ?? profiles.find((item) => String(item.id) === current) ?? profiles[0];
  const draft = { profileKey: "", length: "1000", parameters: {}, error: "" };
  if (profile) selectProfile(draft, profile);
  view.tubeDesignerNestingStandardPartDraft = draft;
  view.error = "";
}

function visible(condition, values) {
  if (!condition) return true;
  const all = condition.op === "all" ? condition.conditions : condition.all;
  const any = condition.op === "any" ? condition.conditions : condition.any;
  if (Array.isArray(all)) return all.every((item) => visible(item, values));
  if (Array.isArray(any)) return any.some((item) => visible(item, values));
  const value = values[condition.parameter ?? condition.key];
  return condition.op === "eq" ? value === condition.value : condition.op === "ne" ? value !== condition.value : true;
}

function renderParameter(definition, values, disabled) {
  const key = definition.key;
  const value = values[key] ?? definition.defaultValue ?? "";
  const label = localized(definition.displayName ?? definition.name, key);
  const common = `data-profile-parameter-key="${escapeAttr(key)}" data-cam-change-action="${action("parameter")}" data-standard-part-parameter="${escapeAttr(key)}" ${disabled}`;
  let control;
  if (definition.valueType === "boolean") {
    control = `<input type="checkbox" ${common} ${value === true ? "checked" : ""} />`;
  } else if (Array.isArray(definition.options) && definition.options.length) {
    control = `<select ${common}>${definition.options.map((option) => {
      const optionValue = typeof option === "object" ? option.value : option;
      return `<option value="${escapeAttr(optionValue)}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(localized(option?.displayName ?? option?.label, optionValue))}</option>`;
    }).join("")}</select>`;
  } else {
    const minimum = definition.min ?? definition.minimum;
    const maximum = definition.max ?? definition.maximum;
    control = `<input type="${definition.valueType === "string" ? "text" : "number"}" value="${escapeAttr(value)}" ${common} step="${escapeAttr(definition.step ?? (definition.valueType === "integer" ? 1 : "any"))}" ${minimum != null ? `min="${escapeAttr(minimum)}"` : ""} ${maximum != null ? `max="${escapeAttr(maximum)}"` : ""} />`;
  }
  return `<label class="tube-designer-field"><span>${escapeText(label)}${definition.unit ? `（${escapeText(localized(definition.unit))}）` : ""}</span>${control}</label>`;
}

export function renderNestingStandardPartDialog(view) {
  const draft = view.tubeDesignerNestingStandardPartDraft;
  if (!draft) return "";
  const profiles = standardPartProfiles(view);
  const profile = selectedProfile(view, draft);
  const source = draft.profileKey === DXF_KEY ? draft.importedProfile : profile;
  const preview = draft.previewInvalid ? null : draft.profile ?? snapshot(source);
  const disabled = view.pending ? "disabled" : "";
  const definitions = definitionsOf(profile).filter((item) => visible(item.visibleWhen, draft.parameters));
  return `<div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop" role="presentation">
    <section class="tube-designer-preset-dialog tube-nesting-standard-part-dialog" data-profile-parameter-scope role="dialog" aria-modal="true" aria-labelledby="nesting-standard-part-title">
      <header class="tube-designer-dialog-header">
        <div><strong id="nesting-standard-part-title">添加标准零件</strong><span>选择截面并输入长度，生成直管下料零件</span></div>
        <button class="tube-designer-dialog-close" data-cam-action="${action("cancel")}" aria-label="取消添加" ${disabled}>×</button>
      </header>
      <div class="tube-nesting-standard-part-body">
        <figure class="tube-nesting-standard-part-preview ${parametric(profile) && !draft.previewInvalid ? "has-parameter-diagram" : ""}">
          ${parametric(profile) && !draft.previewInvalid ? renderProfileParameterDiagram(preview, { definitions: definitionsOf(profile), parameters: draft.parameters, compact: true }) : `
          <div class="tube-nesting-standard-part-section">${preview?.contours?.length ? renderProfileSvg(preview) : `<span>${draft.previewInvalid ? "请检查截面参数，示意图将在成功生成后更新" : "请选择管型或导入 DXF"}</span>`}</div>
          <figcaption><strong>${source ? escapeText(nameOf(source)) : "截面预览"}</strong><span>${escapeText(preview?.specification ?? "")}</span><small>按此截面沿长度方向直线拉伸</small></figcaption>`}
        </figure>
        <div class="tube-nesting-standard-part-fields">
          <label class="tube-designer-field"><span>管型</span><select aria-label="管型" data-cam-change-action="${action("profile-select")}" ${disabled}>
            <option value="" ${!draft.profileKey ? "selected" : ""} disabled>请选择管型</option>
            ${[["system", "系统内置"], ["user", "我的"]].map(([scope, label]) => {
              const items = profiles.filter((item) => profileScope(item) === scope);
              return items.length ? `<optgroup label="${label}">${items.map((item) => `<option value="${escapeAttr(profileSelectionKey(item))}" ${draft.profileKey === profileSelectionKey(item) ? "selected" : ""}>${escapeText(nameOf(item))} · ${parametric(item) ? "程式" : "定式"}</option>`).join("")}</optgroup>` : "";
            }).join("")}
            <option value="${DXF_KEY}" ${draft.profileKey === DXF_KEY ? "selected" : ""}>${draft.importedProfile ? `本地 DXF · ${escapeText(nameOf(draft.importedProfile))}` : "从本地 DXF 导入…"}</option>
          </select></label>
          <div class="tube-nesting-standard-part-source"><span>仅显示系统和我的管型</span><button class="tube-designer-secondary" data-cam-action="${action("import-dxf")}" ${disabled}>导入 DXF</button></div>
          ${definitions.length ? `<section class="tube-nesting-standard-part-parameters"><strong>截面参数</strong><div>${definitions.map((item) => renderParameter(item, draft.parameters, disabled)).join("")}</div><small>仅用于本次零件，不修改管型库</small></section>` : source ? `<p class="tube-nesting-standard-part-note">${parametric(profile) ? "此程式管型未提供可编辑参数，使用默认截面。" : "定式截面保持原始轮廓，直接设置长度即可。"}</p>` : ""}
          <label class="tube-designer-field"><span>长度（mm）</span><input aria-label="长度（mm）" type="number" min="1" max="100000" step="any" value="${escapeAttr(draft.length)}" data-cam-change-action="${action("length")}" ${disabled} /></label>
          ${draft.error ? `<div class="tube-designer-punch-error" role="alert">${escapeText(draft.error)}</div>` : ""}
        </div>
      </div>
      <footer class="tube-designer-preset-dialog-footer"><small>添加后可在零件列表调整数量</small><button class="tube-designer-secondary" data-cam-action="${action("cancel")}" ${disabled}>取消</button><button class="tube-designer-primary" data-cam-action="${action("confirm")}" ${disabled || (!source ? "disabled" : "")}>${view.pending ? "处理中…" : "添加零件"}</button></footer>
    </section>
  </div>`;
}

function parametersFor(profile, draft) {
  const parameters = { ...draft.parameters };
  for (const definition of definitionsOf(profile)) {
    if (!visible(definition.visibleWhen, parameters)) continue;
    const key = definition.key;
    const label = localized(definition.displayName ?? definition.name, key);
    const raw = parameters[key] ?? definition.defaultValue;
    const type = definition.valueType ?? "number";
    let value = raw;
    if (type === "number" || type === "integer") {
      value = Number(raw);
      if (raw == null || String(raw).trim() === "" || !Number.isFinite(value) || (type === "integer" && !Number.isSafeInteger(value))) {
        throw new Error(`${label}须为有效${type === "integer" ? "整数" : "数值"}。`);
      }
      const min = definition.min ?? definition.minimum;
      const max = definition.max ?? definition.maximum;
      if ((min != null && value < min) || (max != null && value > max)) throw new Error(`${label}超出允许范围${min != null ? `，最小 ${min}` : ""}${max != null ? `，最大 ${max}` : ""}。`);
    } else if (type === "boolean") {
      if (typeof value !== "boolean") throw new Error(`${label}须为开关值。`);
    } else if (type === "string") value = String(raw ?? "");
    if (definition.options?.length && !definition.options.some((option) => (typeof option === "object" ? option.value : option) === value)) throw new Error(`请选择有效的${label}。`);
    parameters[key] = value;
  }
  return parameters;
}

function beginOperation(context, view, ops, operation) {
  const id = Number(view.tubeDesignerOperationSequence ?? 0) + 1;
  view.tubeDesignerOperationSequence = id;
  view.tubeDesignerOperation = { id, ...operation };
  view.pending = true;
  ops.renderProject(context, view);
  return id;
}

function finishOperation(context, view, ops, id) {
  if (view.tubeDesignerOperation?.id !== id) return;
  view.tubeDesignerOperation = null;
  view.pending = false;
  ops.renderProject(context, view);
}

function updateOperation(view, id, update) {
  if (view.tubeDesignerOperation?.id === id) Object.assign(view.tubeDesignerOperation, update);
}

function waitForProgressPaint() {
  const requestFrame = globalThis.requestAnimationFrame;
  if (typeof requestFrame !== "function") return Promise.resolve();
  return new Promise((resolve) => {
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      globalThis.clearTimeout(fallback);
      resolve();
    };
    // A hidden WebView can suspend animation frames. Do not strand the task;
    // in a visible view, two frames allow the progress overlay to paint first.
    const fallback = globalThis.setTimeout(finish, 100);
    requestFrame(() => requestFrame(finish));
  });
}

async function importDxf(context, view, draft, ops) {
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
  if (typeof bridge?.openFileDialog !== "function") throw new Error("当前宿主没有提供文件选择能力。");
  const operationId = beginOperation(context, view, ops, {
    kind: "nesting-standard-dxf", title: "正在导入 DXF 截面", phase: "selecting-file",
    phaseLabel: "等待选择文件", message: "请在文件窗口中选择 DXF 截面，或取消返回。",
  });
  try {
    await waitForProgressPaint();
    const sourcePath = String(await bridge.openFileDialog({ title: "选择标准零件的 DXF 截面", filters: [{ name: "DXF 截面", extensions: ["dxf"] }] }) ?? "").trim();
    if (!sourcePath) return null;
    if (!/\.dxf$/i.test(sourcePath)) throw new Error("请选择 DXF 截面文件。");
    updateOperation(view, operationId, {
      phase: "importing-dxf", phaseLabel: "读取并检查 DXF 轮廓",
      message: "正在读取截面曲线并检查封闭轮廓，完成后更新示意图。",
    });
    ops.renderProject(context, view);
    await waitForProgressPaint();
    const response = await context.productProxy?.invoke("TubeDesigner.ImportProfileDxf", { sourcePath }, { timeoutMs: 60000 });
    if (!response?.profile?.contours?.length) throw new Error("DXF 未返回有效的封闭截面。");
    if (view.tubeDesignerNestingStandardPartDraft !== draft) return null;
    draft.importedProfile = clone(response.profile);
    draft.profile = draft.importedProfile;
    draft.profileKey = DXF_KEY;
    draft.parameters = {};
    draft.previewInvalid = false;
    draft.error = "";
    return response;
  } finally {
    finishOperation(context, view, ops, operationId);
  }
}

async function updateParameter(context, view, draft, target, ops) {
  const profile = selectedProfile(view, draft);
  const definition = definitionsOf(profile).find((item) => item.key === target?.dataset?.standardPartParameter);
  if (!definition) return null;
  draft.parameters[definition.key] = definition.valueType === "boolean" ? !!target.checked : String(target.value ?? "");
  draft.previewInvalid = true;
  const parameters = parametersFor(profile, draft);
  draft.parameters = parameters;
  const operationId = beginOperation(context, view, ops, {
    kind: "nesting-standard-parameters", title: "正在更新截面", phase: "evaluating-profile",
    phaseLabel: "按参数计算截面", message: "正在计算程式管型，成功后更新截面及参数示意图。",
  });
  try {
    await waitForProgressPaint();
    const response = await context.productProxy?.invoke("TubeDesigner.EvaluateProfilePackage", { profileRef: profileRef(profile), parameters }, { timeoutMs: 30000 });
    if (!response?.profile?.contours?.length) throw new Error("未能生成有效截面，请检查管型参数。");
    if (view.tubeDesignerNestingStandardPartDraft !== draft) return null;
    draft.profile = response.profile;
    draft.previewInvalid = false;
    draft.error = "";
    return response;
  } finally {
    finishOperation(context, view, ops, operationId);
  }
}

async function confirmPart(context, view, draft, ops) {
  const length = Number(draft.length);
  if (!Number.isFinite(length) || length < 1 || length > 100000) throw new Error("长度须为 1 至 100000 mm 之间的有效数值。");
  if (draft.previewInvalid) throw new Error("请先修正截面参数，待截面预览成功后再添加零件。");
  const profile = selectedProfile(view, draft);
  let payload;
  if (draft.profileKey === DXF_KEY && draft.importedProfile) payload = { profile: clone(draft.importedProfile), length };
  else {
    if (!profile) throw new Error("请选择系统或我的管型，模板自带管型不可用于添加标准零件。");
    payload = { profileRef: profileRef(profile), parameters: parametersFor(profile, draft), length };
  }
  if (typeof context.sceneProxy?.invoke !== "function") throw new Error("当前项目未连接，无法添加标准零件。");
  const operationId = beginOperation(context, view, ops, {
    kind: "nesting-standard-part", title: "正在添加标准零件", phase: "generating",
    phaseLabel: "生成直管实体", message: "正在按截面和长度创建下料零件…",
  });
  try {
    await waitForProgressPaint();
    const response = await context.sceneProxy.invoke("TubeDesigner.AddNestingStandardPart", payload, { timeoutMs: 180000 });
    if (!response?.tubeDesigner || !response.partEntityId) throw new Error("添加标准零件未返回有效的下料记录。");
    updateOperation(view, operationId, {
      phase: "refreshing-scene", phaseLabel: "刷新场景和零件列表",
      message: "零件已生成，正在更新三维场景和下料列表，请勿重复添加。",
    });
    view.scene ??= {};
    view.scene.tubeDesigner = response.tubeDesigner;
    restoreSavedNestingTask(view, context);
    const partId = String(response.partEntityId);
    view.tubeDesignerNestingSelectedPartIds = [partId];
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerNestingSelectionKind = "part";
    view.tubeDesignerActiveNestingPlacementId = "";
    view.tubeDesignerPartSearchText = "";
    view.tubeDesignerPartFilter = "all";
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerPartViewportKey = "";
    view.tubeDesignerNestingStandardPartDraft = null;
    view.tubeDesignerNestingStockDraft = null;
    view.tubeDesignerNestingSettingsSourceSignature = "";
    ops.renderProject(context, view);
    await waitForProgressPaint();
    try {
      await context.actions?.refreshActiveSceneState?.();
    } catch (error) {
      throw new Error(`标准零件已添加，但场景刷新失败，请刷新场景，不要重复添加。${error?.message ?? String(error)}`);
    }
    ops.showNotice?.(context, view, `已添加标准零件，长度 ${length} mm。可在零件列表调整数量。`);
    return response;
  } finally {
    finishOperation(context, view, ops, operationId);
  }
}

export async function handleNestingStandardPartAction(context, view, command, target, ops) {
  const suffix = command.startsWith(PREFIX) ? command.slice(PREFIX.length) : "";
  if (!["open", "cancel", "profile-select", "import-dxf", "parameter", "length", "confirm"].includes(suffix)) return { handled: false };
  if (view.pending) return { handled: true };
  let result = null;
  try {
    if (suffix === "open") openDialog(view);
    else if (suffix === "cancel") view.tubeDesignerNestingStandardPartDraft = null;
    else {
      const draft = view.tubeDesignerNestingStandardPartDraft;
      if (!draft) return { handled: true };
      draft.error = "";
      view.error = "";
      if (suffix === "length") {
        draft.length = String(target?.value ?? "");
        // Keep the active input/button in place when a blur commits the length.
        return { handled: true };
      }
      else if (suffix === "import-dxf" || (suffix === "profile-select" && target?.value === DXF_KEY)) result = await importDxf(context, view, draft, ops);
      else if (suffix === "profile-select") {
        const profile = standardPartProfiles(view).find((item) => profileSelectionKey(item) === target?.value);
        if (!profile) throw new Error("请选择系统或我的管型，模板自带管型不可用于添加标准零件。");
        selectProfile(draft, profile);
      } else if (suffix === "parameter") result = await updateParameter(context, view, draft, target, ops);
      else if (suffix === "confirm") result = await confirmPart(context, view, draft, ops);
    }
  } catch (error) {
    const draft = view.tubeDesignerNestingStandardPartDraft;
    if (draft) draft.error = error?.message ?? String(error);
    else view.error = error?.message ?? String(error);
  }
  ops.renderProject(context, view);
  return { handled: true, result };
}

export async function handleNestingStandardPartRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "nesting.add-standard-part") return false;
  await handleNestingStandardPartAction(context, view, action("open"), null, ops);
  return true;
}
