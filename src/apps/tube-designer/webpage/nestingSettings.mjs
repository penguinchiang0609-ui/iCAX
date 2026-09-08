import { buildProfileGroups, listNestingParts } from "./partsArea.mjs";

const VERSION = 2;
const ACTION_PREFIX = "tube-designer-nesting-";
let rowSequence = 0;

function sectionGroups(view) {
  return buildProfileGroups(listNestingParts(view.scene?.tubeDesigner ?? {}));
}

function newRow() {
  return { id: `stock-row-${Date.now().toString(36)}-${++rowSequence}`, length: 6000, quantity: -1 };
}

function copy(value) {
  return JSON.parse(JSON.stringify(value));
}

function numeric(value) {
  if ((typeof value !== "number" && typeof value !== "string") || String(value).trim() === "") return null;
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function validRow(row) {
  const quantity = numeric(row?.quantity);
  const length = numeric(row?.length);
  if (!Number.isSafeInteger(quantity) || quantity < -1) return null;
  if (length === null || length <= 0) return null;
  return { id: String(row?.id || newRow().id), length, quantity };
}

function ensureSettings(view, _context = null) {
  // The scene is authoritative: loading a project or undoing a save replaces this value.
  const saved = view.scene?.tubeDesigner?.nestingSettings ?? {};
  const sourceSignature = JSON.stringify(saved);
  if (!view.tubeDesignerNestingSettings || view.tubeDesignerNestingSettingsSourceSignature !== sourceSignature) {
    const partGap = numeric(saved?.parameters?.partGap);
    view.tubeDesignerNestingSettings = {
      version: VERSION,
      stocks: saved?.version === VERSION && Array.isArray(saved.stocks)
        ? saved.stocks.filter((group) => typeof group?.profileKey === "string").map((group) => ({
          profileKey: group.profileKey,
          rows: (Array.isArray(group.rows) ? group.rows : []).map(validRow).filter(Boolean),
        })) : [],
      parameters: { partGap: partGap !== null && partGap >= 0 ? partGap : 0 },
    };
    view.tubeDesignerNestingSettingsSourceSignature = sourceSignature;
  }
  const settings = view.tubeDesignerNestingSettings;
  const existing = new Set(settings.stocks.map((group) => group.profileKey));
  for (const group of sectionGroups(view)) {
    if (!existing.has(group.key)) {
      settings.stocks.push({ profileKey: group.key, rows: [newRow()] });
      existing.add(group.key);
    }
  }
  return settings;
}

function syncStockDraft(view) {
  const draft = view.tubeDesignerNestingStockDraft ??= [];
  for (const group of sectionGroups(view)) {
    if (!draft.some((item) => item.profileKey === group.key)) {
      const saved = ensureSettings(view).stocks.find((item) => item.profileKey === group.key);
      draft.push(copy(saved ?? { profileKey: group.key, rows: [newRow()] }));
    }
  }
  return draft;
}

export function getNestingStockInputs(view, context = null) {
  const settings = ensureSettings(view, context);
  return sectionGroups(view).flatMap((group) => {
    const saved = settings.stocks.find((item) => item.profileKey === group.key);
    return (saved?.rows ?? []).flatMap((row) => {
      const valid = validRow(row);
      // -1 is unlimited; a disabled row (0) must never reach the nesting request.
      if (!valid || valid.quantity === 0) return [];
      return [{
        id: valid.id,
        profileKey: group.key,
        profile: copy(group.parts[0]?.profile ?? group.parts[0]?.properties?.["tubeDesigner.profile"] ?? {}),
        length: valid.length,
        quantity: valid.quantity,
      }];
    });
  });
}

export function getNestingParameters(view, context = null) {
  const value = numeric(ensureSettings(view, context).parameters.partGap);
  return { partGap: value !== null && value >= 0 ? value : 0 };
}

export function handleNestingSettingsRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "nesting.stock-settings" && commandId !== "nesting.parameters") return false;
  if (view.pending || view.tubeDesignerNestingSettingsSaving) return true;
  const settings = ensureSettings(view, context);
  view.tubeDesignerNestingSettingsDialog = commandId === "nesting.stock-settings" ? "stock" : "parameters";
  const currentKeys = new Set(sectionGroups(view).map((group) => group.key));
  view.tubeDesignerNestingStockDraft = copy(settings.stocks.filter((group) => currentKeys.has(group.profileKey)));
  view.tubeDesignerNestingParameterDraft = { partGap: String(settings.parameters.partGap) };
  view.tubeDesignerNestingSettingsError = "";
  ops.renderProject(context, view);
  return true;
}

function updateStockField(view, target) {
  const { profileKey, rowId, field } = target?.dataset ?? {};
  if (field !== "length" && field !== "quantity") return;
  const row = view.tubeDesignerNestingStockDraft?.find((group) => group.profileKey === profileKey)
    ?.rows.find((item) => item.id === rowId);
  if (row) row[field] = String(target?.value ?? "");
}

function collectFields(context, view, target) {
  const dialog = target?.closest?.("[data-tube-nesting-dialog]")
    ?? context?.mount?.querySelector?.("[data-tube-nesting-dialog]");
  for (const input of dialog?.querySelectorAll?.("[data-tube-nesting-stock-field]") ?? []) {
    updateStockField(view, input);
  }
  const gap = dialog?.querySelector?.("[data-tube-nesting-parameter='partGap']");
  if (gap && view.tubeDesignerNestingParameterDraft) {
    view.tubeDesignerNestingParameterDraft.partGap = String(gap.value ?? "");
  }
}

function closeDialog(context, view, ops) {
  view.tubeDesignerNestingSettingsDialog = "";
  view.tubeDesignerNestingStockDraft = null;
  view.tubeDesignerNestingParameterDraft = null;
  view.tubeDesignerNestingSettingsError = "";
  ops.renderProject(context, view);
}

export async function handleNestingSettingsAction(context, view, action, target, ops) {
  const known = ["stock-change", "stock-add", "stock-remove", "stock-save", "parameters-change", "parameters-save", "settings-cancel"];
  if (!known.some((name) => action === ACTION_PREFIX + name)) return { handled: false };
  if (view.pending || view.tubeDesignerNestingSettingsSaving || !view.tubeDesignerNestingSettingsDialog) return { handled: true };
  if (action === ACTION_PREFIX + "settings-cancel") {
    closeDialog(context, view, ops);
    return { handled: true };
  }
  if (action === ACTION_PREFIX + "stock-change") {
    updateStockField(view, target);
    return { handled: true };
  }
  if (action === ACTION_PREFIX + "parameters-change") {
    if (target?.dataset?.field === "partGap" && view.tubeDesignerNestingParameterDraft) {
      view.tubeDesignerNestingParameterDraft.partGap = String(target?.value ?? "");
    }
    return { handled: true };
  }
  collectFields(context, view, target);
  if (action === ACTION_PREFIX + "stock-add" || action === ACTION_PREFIX + "stock-remove") {
    const group = syncStockDraft(view).find((item) => item.profileKey === target?.dataset?.profileKey);
    if (!group) return { handled: true };
    if (action === ACTION_PREFIX + "stock-add") group.rows.push(newRow());
    else group.rows = group.rows.filter((row) => row.id !== target?.dataset?.rowId);
    view.tubeDesignerNestingSettingsError = "";
    const scrollTop = context?.mount?.querySelector?.(".tube-nesting-settings-body")?.scrollTop ?? 0;
    ops.renderProject(context, view);
    const body = context?.mount?.querySelector?.(".tube-nesting-settings-body");
    if (body) body.scrollTop = scrollTop;
    return { handled: true };
  }
  const settings = copy(ensureSettings(view, context));
  if (action === ACTION_PREFIX + "stock-save") {
    const groups = sectionGroups(view);
    const draft = syncStockDraft(view);
    const normalized = [];
    for (const group of groups) {
      const rows = draft.find((item) => item.profileKey === group.key)?.rows ?? [];
      const valid = rows.map(validRow);
      const invalidIndex = valid.findIndex((row) => !row);
      if (invalidIndex !== -1) {
        view.tubeDesignerNestingSettingsError = `${group.profile}，第 ${invalidIndex + 1} 行：长度须大于 0；数量填 -1（不限）、0（不用）或正整数。`;
        ops.renderProject(context, view);
        return { handled: true, result: false };
      }
      normalized.push({ profileKey: group.key, rows: valid });
    }
    const currentKeys = new Set(groups.map((group) => group.key));
    settings.stocks = [...settings.stocks.filter((group) => !currentKeys.has(group.profileKey)), ...normalized];
  } else if (action === ACTION_PREFIX + "parameters-save") {
    const gap = numeric(view.tubeDesignerNestingParameterDraft?.partGap);
    if (gap === null || gap < 0) {
      view.tubeDesignerNestingSettingsError = "零件间距须为大于或等于 0 的数值。";
      ops.renderProject(context, view);
      return { handled: true, result: false };
    }
    settings.parameters = { ...settings.parameters, partGap: gap };
  }
  view.tubeDesignerNestingSettingsSaving = true;
  view.tubeDesignerNestingSettingsError = "";
  ops.renderProject(context, view);
  try {
    if (!context.sceneProxy?.invoke) throw new Error("当前项目未连接，无法保存排样设置。");
    const response = await context.sceneProxy.invoke("TubeDesigner.SaveNestingSettings", { settings }, { timeoutMs: 30000 });
    if (response?.settings?.version !== VERSION || !Array.isArray(response.settings.stocks)) {
      throw new Error(response?.message || "项目没有返回已保存的排样设置，请重试。");
    }
    view.scene ??= {};
    view.scene.tubeDesigner ??= {};
    view.scene.tubeDesigner.nestingSettings = copy(response.settings);
    ensureSettings(view, context);
    closeDialog(context, view, ops);
    try {
      view.tubeDesignerOwnMutation = true;
      await context.actions?.refreshActiveSceneState?.();
    } catch (error) {
      view.notice = `设置已保存，界面刷新失败：${error?.message ?? error}`;
    } finally {
      view.tubeDesignerOwnMutation = false;
    }
    return { handled: true, result: true };
  } catch (error) {
    view.tubeDesignerNestingSettingsError = error?.message ?? String(error);
    return { handled: true, result: false };
  } finally {
    view.tubeDesignerNestingSettingsSaving = false;
    ops.renderProject(context, view);
  }
}

function escape(value) {
  return String(value ?? "").replace(/[&<>"']/g, (character) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  })[character]);
}

function renderStockGroup(group, draft, saving) {
  const rows = draft.find((item) => item.profileKey === group.key)?.rows ?? [];
  return `<section class="tube-nesting-stock-group">
    <header><div><strong>${escape(group.profile)}</strong><span>${group.quantity} 件零件</span></div>
      <button type="button" data-cam-action="${ACTION_PREFIX}stock-add" data-profile-key="${escape(group.key)}" ${saving ? "disabled" : ""}>＋ 添加长度</button>
    </header>
    <div class="tube-nesting-stock-columns" aria-hidden="true"><span>母材长度（mm）</span><span>数量（根）</span><span></span></div>
    ${rows.map((row, index) => `<div class="tube-nesting-stock-row">
      <input type="number" min="0" step="any" value="${escape(row.length)}" placeholder="填写母材长度" aria-label="${escape(group.profile)} 第${index + 1}行 母材长度（mm）" data-tube-nesting-stock-field data-cam-change-action="${ACTION_PREFIX}stock-change" data-profile-key="${escape(group.key)}" data-row-id="${escape(row.id)}" data-field="length" ${saving ? "disabled" : ""} />
      <input type="number" min="-1" step="1" value="${escape(row.quantity)}" title="-1 不限，0 不用，正整数为有限库存" aria-label="${escape(group.profile)} 第${index + 1}行 数量（根）" data-tube-nesting-stock-field data-cam-change-action="${ACTION_PREFIX}stock-change" data-profile-key="${escape(group.key)}" data-row-id="${escape(row.id)}" data-field="quantity" ${saving ? "disabled" : ""} />
      <button type="button" class="tube-nesting-remove-row" data-cam-action="${ACTION_PREFIX}stock-remove" data-profile-key="${escape(group.key)}" data-row-id="${escape(row.id)}" aria-label="删除${escape(group.profile)}第${index + 1}行" ${saving ? "disabled" : ""}>删除</button>
    </div>`).join("")}
    ${rows.length ? "" : '<p class="tube-nesting-settings-hint">尚未配置母材，点击“添加长度”填写。</p>'}
  </section>`;
}

export function renderNestingSettingsDialogs(context, view) {
  const settings = ensureSettings(view, context);
  const kind = view.tubeDesignerNestingSettingsDialog;
  if (kind !== "stock" && kind !== "parameters") return "";
  const stock = kind === "stock";
  const groups = stock ? sectionGroups(view) : [];
  const draft = stock ? syncStockDraft(view) : null;
  const title = stock ? "母材设置" : "排样参数";
  const error = view.tubeDesignerNestingSettingsError;
  const saving = Boolean(view.tubeDesignerNestingSettingsSaving);
  return `<div class="tube-nesting-settings-backdrop">
    <section class="tube-nesting-settings-dialog ${stock ? "is-stock" : "is-parameters"}" data-tube-nesting-dialog="${kind}" role="dialog" aria-modal="true" aria-labelledby="tube-nesting-settings-title">
      <header class="tube-nesting-settings-header"><div><strong id="tube-nesting-settings-title">${title}</strong><span>${stock ? "按零件截面生成，默认 6000 mm；可添加不同长度。设置保存在当前项目中。" : "设置排样时相邻零件之间预留的距离，保存在当前项目中。"}</span></div>
        <button type="button" data-cam-action="${ACTION_PREFIX}settings-cancel" class="tube-nesting-settings-close" aria-label="关闭${title}" ${saving ? "disabled" : ""}>×</button></header>
      <div class="tube-nesting-settings-body">
        ${stock ? (groups.length ? groups.map((group) => renderStockGroup(group, draft, saving)).join("") : '<div class="tube-nesting-settings-empty"><strong>还没有零件</strong><span>请先在产品页点击“导入下料”。</span></div>') : `<label class="tube-nesting-parameter-field"><span>零件间距（mm）</span><input type="number" min="0" step="any" value="${escape(view.tubeDesignerNestingParameterDraft?.partGap ?? settings.parameters.partGap)}" data-tube-nesting-parameter="partGap" data-cam-change-action="${ACTION_PREFIX}parameters-change" data-field="partGap" ${saving ? "disabled" : ""} /><small>设为 0 表示不额外预留零件间距。</small></label>`}
      </div>
      <footer class="tube-nesting-settings-footer">
        <div>${error ? `<p class="tube-nesting-settings-error" role="alert">${escape(error)}</p>` : `<span class="tube-nesting-settings-hint">${stock ? "数量 -1：不限；0：不用；正整数：有限库存，排样时优先使用。" : "单位：毫米（mm）"}</span>`}</div>
        <button type="button" data-cam-action="${ACTION_PREFIX}settings-cancel" ${saving ? "disabled" : ""}>取消</button>
        <button type="button" class="tube-nesting-settings-save" data-cam-action="${ACTION_PREFIX}${stock ? "stock-save" : "parameters-save"}" ${saving || stock && !groups.length ? "disabled" : ""}>${saving ? "保存中…" : "确定"}</button>
      </footer>
    </section>
  </div>`;
}
