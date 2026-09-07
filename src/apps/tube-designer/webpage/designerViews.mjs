import {
  cancelDesignerPartThumbnailHydration,
  scheduleDesignerPartThumbnailHydration,
} from "./partThumbnail.mjs";
import { scheduleDesignerPartInspectionHydration } from "./partInspection.mjs";
import { manufacturingPartKind, plateDimensions } from "./manufacturingParts.mjs";
import { restoreScrollAnchor } from "./scrollAnchor.mjs";
import { renderSecurityWindowReview, securityWindowOpeningDimensions } from "./securityWindowReview.mjs";
import { renderComponentModelField } from "./componentLibrary.mjs";
import {
  buildCatalogEntries,
  buildTemplateGroupTree,
  getCatalogEntry,
  getCatalogTemplatePath,
  renderGuardrailSchematic,
  sortTemplatesByCatalog,
} from "./productCatalog.mjs";
export { sortTemplatesByCatalog } from "./productCatalog.mjs";

const FALLBACK_PARAMETERS = Object.freeze({
  productCode: "TD-001", height: 1800, width: 1200,
  horizontalCount: 4, verticalLayoutMode: "maximum_clear_gap", maximumVerticalClearGap: 110,
  middleVerticalCount: 9,
  firstHorizontalTopOffset: 200, lastHorizontalBottomOffset: 200,
  horizontalBranchReserve: 10, verticalBranchReserve: 10, assemblyClearance: 0.1,
  frameProfileType: "rect", frameWidth: 38, frameDepth: 25, frameWallThickness: 1.2,
  verticalProfileType: "round", verticalWidth: 19, verticalDepth: 19, verticalWallThickness: 1,
  horizontalProfileType: "rect", horizontalWidth: 22, horizontalDepth: 22, horizontalWallThickness: 1,
});

export function getDefaultParameters(templates = [], templateId = "") {
  const template = getTemplateById(templates, templateId) ?? getDefaultTemplate(templates);
  const fields = Array.isArray(template?.parameters) ? template.parameters : [];
  if (!fields.length) return { ...FALLBACK_PARAMETERS };
  return Object.fromEntries(fields.map((field) => [field.key ?? field.name, field.defaultValue]));
}

export function getReusablePresetValues(template, values = {}) {
  const parameterKeys = new Set((template?.parameters ?? [])
    .map((field) => String(field?.key ?? field?.name ?? "").trim())
    .filter(Boolean));
  const excludedKeys = new Set();
  const identityKey = String(template?.extensions?.productIdentity?.codeParameter ?? "").trim();
  const selectorKey = String(template?.extensions?.parameterPresets?.selectorParameter ?? "").trim();
  if (identityKey) excludedKeys.add(identityKey);
  if (selectorKey) excludedKeys.add(selectorKey);
  for (const [name, value] of Object.entries(template?.extensions?.primaryDimensions ?? {})) {
    if (name.endsWith("Parameter") && typeof value === "string" && value.trim()) {
      excludedKeys.add(value.trim());
    }
  }
  return Object.fromEntries(Object.entries(values ?? {}).filter(([key]) =>
    parameterKeys.has(key) && !excludedKeys.has(key)));
}

export function applyReusablePresetValues(template, currentValues = {}, presetValues = {}) {
  const parameterKeys = new Set((template?.parameters ?? [])
    .map((field) => String(field?.key ?? field?.name ?? "").trim())
    .filter(Boolean));
  const result = { ...currentValues };
  for (const [key, value] of Object.entries(presetValues ?? {})) {
    if (parameterKeys.has(key)) result[key] = value;
  }
  return result;
}

export function getTemplateById(templates = [], templateId = "") {
  return templates.find((item) => item?.id === templateId) ?? null;
}

export function getTemplateDisplayName(template) {
  return getCatalogTemplatePath(template).at(-1);
}

export function getDefaultTemplate(templates = []) {
  return sortTemplatesByCatalog(templates).find((item) => item?.available) ?? null;
}

export function renderDesignerLeftPane(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const instances = Array.isArray(designer.instances) ? designer.instances : [];
  const templates = Array.isArray(designer.templates) ? designer.templates : [];
  const activeId = designer.product?.entityId ?? designer.activeProductId;
  return `
    <div class="tube-designer-panel tube-designer-instance-panel">
      <div class="tube-designer-heading tube-designer-instance-heading">
        <div><strong>产品实例</strong><span>${instances.length} 个实例</span></div>
      </div>
      <div class="tube-designer-instance-list">
        ${instances.length
          ? instances.map((instance) => renderInstanceCard(instance, templates, instance.entityId === activeId, view.pending)).join("")
          : `<div class="tube-designer-empty-state">
              ${renderSchematic("empty", {}, "tube-designer-empty-schematic")}
              <strong>还没有产品实例</strong>
              <span>点击上方“添加”，选择模板并设置初始参数。</span>
            </div>`}
      </div>
    </div>
  `;
}

export function renderDesignerRightPane(context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  const templates = Array.isArray(designer.templates) ? designer.templates : [];
  const dialogs = renderDesignerDialogs(designer, view);
  if (view.tubeDesignerBreakdownOpen && !view.tubeDesignerPartInspectionOpen) {
    scheduleDesignerPartThumbnailHydration(context);
  } else {
    cancelDesignerPartThumbnailHydration(context);
  }
  scheduleDesignerPartInspectionHydration(context, designer, view);
  if (!product) {
    return `
      <div class="tube-designer-panel">
        <div class="tube-designer-heading"><strong>实例参数</strong><span>尚未选择实例</span></div>
        <div class="tube-designer-empty">从上方添加产品，或在左侧选择一个产品实例。场景只显示当前选中的实例。</div>
      </div>
      ${dialogs}
    `;
  }

  const template = getTemplateById(templates, product.templateId);
  const values = { ...getDefaultParameters(templates, product.templateId), ...(product.parameters ?? {}), ...(view.tubeDesignerRightDraft ?? {}) };
  const groupTree = buildParameterGroupTree(template, visibleParameterFields(template, values));
  const defaultExpandedGroups = defaultExpandedParameterGroups(groupTree);
  const hasSavedPanelState = String(view.tubeDesignerParameterPanelProductId ?? "") === String(product.entityId ?? "");
  const expandedGroups = new Set(hasSavedPanelState && Array.isArray(view.tubeDesignerExpandedParameterGroups)
    ? view.tubeDesignerExpandedParameterGroups
    : defaultExpandedGroups);
  const parts = designer.parts ?? [];
  scheduleDesignerParameterPanelRestoration(context, view, product.entityId, hasSavedPanelState);
  return `
    <div class="tube-designer-panel tube-designer-parameter-panel" data-tube-designer-parameter-form>
      <header class="tube-designer-parameter-header">
        <div class="tube-designer-parameter-toolbar">
          <div class="tube-designer-heading">
            <strong>${escapeText(product.name)}</strong>
            <span>${escapeText(getTemplateDisplayName(template) || product.templateId)} · 当前场景实例</span>
          </div>
          <button class="tube-designer-primary tube-designer-regenerate-button" data-cam-action="tube-designer-confirm-update" title="按当前参数重新生成；原拆单结果将失效" ${view.pending ? "disabled" : ""}>${view.pending ? "生成中…" : "重新生成"}</button>
        </div>
        <div class="tube-designer-summary">
          ${metric(designer.members?.length ?? 0, "预览装配单元")}
          ${metric(designer.joints?.length ?? 0, "连接关系")}
          ${metric(parts.length, "已拆零件")}
        </div>
      </header>
      ${renderSecurityWindowReview(template, values)}
      ${renderParameterPresetBar(template, values, view, "right")}
      <div class="tube-designer-parameter-sections">
        ${groupTree.map((group) => compactParameterGroup(
          group,
          values,
          view.pending,
          expandedGroups,
          view,
          "right",
          template,
        )).join("")}
      </div>
    </div>
    ${dialogs}
  `;
}

export function renderDesignerViewportOverlay(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  if (!product) return "";
  const parameters = product.parameters ?? {};
  return `
    <div class="tube-designer-overlay">
      <strong>${escapeText(product.name)}</strong>
      <span>${escapeText(formatProductDimensions(product.templateId, parameters))} · ${designer.members?.length ?? 0} 个装配构件</span>
    </div>
  `;
}

export function renderDesignerOperationOverlay(_context, view) {
  const exportOperation = view.tubeDesignerExportOperation;
  if (exportOperation) {
    return `<div class="tube-designer-operation-wait" data-tube-designer-operation-wait>${renderExportWait(exportOperation)}</div>`;
  }
  const operation = view.tubeDesignerOperation;
  if (!operation) return "";
  return `
    <div class="tube-designer-operation-wait" data-tube-designer-operation-wait role="status" aria-live="polite">
      <div class="tube-designer-export-progress-card">
        <span class="tube-designer-export-spinner" aria-hidden="true"></span>
        <strong data-tube-designer-operation-title>${escapeText(operation.title ?? "正在处理")}</strong>
        <span data-tube-designer-operation-message>${escapeText(operation.message ?? "正在等待后台任务完成")}</span>
        <div class="tube-designer-export-progress-track is-indeterminate">
          <i style="width:36%"></i>
        </div>
        <small data-tube-designer-operation-phase>${escapeText(operation.phaseLabel ?? "处理中")}</small>
        <em>任务完成或失败后，界面会自动恢复。</em>
      </div>
    </div>`;
}

export function renderDesignerDialogs(designer, view) {
  if (view.tubeDesignerPartInspectionOpen) {
    return renderPartInspectionDialog(designer, view);
  }
  return [
    view.tubeDesignerPostDisassemblyChoice ? renderPostDisassemblyChoice(view) : "",
    view.tubeDesignerAddDialogOpen ? renderDesignerAddDialog(designer, view) : "",
    view.tubeDesignerDisassemblySelectorOpen ? renderDisassemblySelector(designer, view) : "",
    view.tubeDesignerBreakdownOpen ? renderBreakdownDialog(designer, view) : "",
    view.tubeDesignerPresetDialog ? renderParameterPresetDialog(designer, view) : "",
    view.tubeDesignerProfileDialog ? renderImportedProfileDialog(designer, view) : "",
    view.tubeDesignerProfileLibraryDialog ? renderImportedProfileLibraryDialog(view) : "",
  ].join("");
}

function renderPostDisassemblyChoice(view) {
  const result = view.tubeDesignerPostDisassemblyChoice ?? {};
  const groupCount = Math.max(0, Number(result.groupCount ?? 0));
  const partCount = Math.max(0, Number(result.partCount ?? 0));
  return `
    <div class="tube-designer-modal-backdrop tube-designer-post-disassembly-backdrop" role="presentation">
      <section class="tube-designer-post-disassembly-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-post-disassembly-title">
        <button class="tube-designer-dialog-close" data-cam-action="tube-designer-dismiss-disassembly-choice" aria-label="稍后处理">×</button>
        <div class="tube-designer-post-disassembly-icon" aria-hidden="true">✓</div>
        <div class="tube-designer-post-disassembly-copy">
          <strong id="tube-designer-post-disassembly-title">拆单完成</strong>
          <span>${groupCount} 个产品，共生成 ${partCount} 种制造零件。下一步可以直接导出，或进入下料工作区继续排样。</span>
        </div>
        <div class="tube-designer-post-disassembly-summary">
          <span><small>产品</small><strong>${groupCount}</strong></span>
          <span><small>零件种类</small><strong>${partCount}</strong></span>
          <span><small>当前选择</small><strong>${partCount}</strong></span>
        </div>
        <footer>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-dismiss-disassembly-choice">稍后处理</button>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-export-after-disassembly">导出零件</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-enter-cutting">进入下料</button>
        </footer>
      </section>
    </div>`;
}

function renderInstanceCard(instance, templates, selected, pending) {
  const template = getTemplateById(templates, instance.templateId);
  const parameters = instance.parameters ?? {};
  return `
    <article class="tube-designer-instance-item ${selected ? "selected" : ""}">
      <button class="tube-designer-instance-card ${selected ? "selected" : ""}" data-cam-action="tube-designer-select-instance" data-tube-designer-instance-id="${escapeAttribute(instance.entityId)}" ${pending || selected ? "disabled" : ""}>
        <span class="tube-designer-instance-thumbnail">${renderSchematic(instance.templateId, parameters)}</span>
        <span class="tube-designer-instance-copy">
          <strong>${escapeText(instance.name)}</strong>
          <span>${escapeText(getTemplateDisplayName(template) || instance.templateId)}</span>
          <small>${escapeText(formatProductDimensions(instance.templateId, parameters))} · ${instance.hasDisassembly ? `${instance.partCount} 个零件` : "未拆单"}</small>
        </span>
        <i aria-hidden="true"></i>
      </button>
      <button class="tube-designer-instance-part-list" data-cam-action="tube-designer-open-instance-breakdown" data-tube-designer-instance-id="${escapeAttribute(instance.entityId)}" ${pending || !instance.hasDisassembly ? "disabled" : ""} title="打开该产品实例的零件清单，可复尺并导出给第三方 CAM">${instance.hasDisassembly ? "零件清单" : "拆单后可导出"}</button>
    </article>
  `;
}

export function renderDesignerAddDialog(designer, view) {
  const templates = Array.isArray(designer.templates) ? designer.templates : [];
  const template = getTemplateById(templates, view.tubeDesignerAddTemplateId) ?? getDefaultTemplate(templates);
  const templateGroups = buildTemplateGroupTree(templates);
  const selectedEntry = getCatalogEntry(templates, template?.id, view.tubeDesignerAddCatalogPresetId);
  const expandedTemplateGroups = new Set(view.tubeDesignerExpandedTemplateGroupIds ?? []);
  const pending = Boolean(view.pending);
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-config-dialog" data-tube-designer-add-dialog role="dialog" aria-modal="true" aria-labelledby="tube-designer-add-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-add-title">添加产品实例</strong><span>选择模板并一次设置好初始参数；确定前不会修改项目数据。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-cancel-add" aria-label="取消添加" ${pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-config-body">
          <aside class="tube-designer-template-pane">
            <div class="tube-designer-pane-title"><strong>产品款式</strong><span>${buildCatalogEntries(templates).length} 款可用</span></div>
            <div class="tube-designer-template-list">
              ${templateGroups.map((group) => renderTemplateGroup(
                group,
                selectedEntry?.catalogEntryId,
                expandedTemplateGroups,
                pending,
              )).join("")}
            </div>
          </aside>
          <main class="tube-designer-config-parameters" data-tube-designer-add-form>
            ${renderDesignerAddParameterContent(designer, view)}
          </main>
        </div>
        <footer class="tube-designer-dialog-footer">
          <span>确定后创建实例并生成装配预览</span>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-cancel-add" ${pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-confirm-add" ${pending || !template?.available ? "disabled" : ""}>${pending ? "正在生成…" : "确定"}</button>
        </footer>
      </section>
    </div>
  `;
}

export function renderDesignerAddParameterContent(designer, view) {
  const templates = Array.isArray(designer?.templates) ? designer.templates : [];
  const template = getTemplateById(templates, view?.tubeDesignerAddTemplateId) ?? getDefaultTemplate(templates);
  const entry = getCatalogEntry(templates, template?.id, view?.tubeDesignerAddCatalogPresetId);
  const values = { ...getDefaultParameters(templates, template?.id), ...(view?.tubeDesignerAddDraft ?? {}) };
  const groupTree = buildParameterGroupTree(template, visibleParameterFields(template, values));
  const pending = Boolean(view?.pending);
  return `
    <div class="tube-designer-config-summary">
      <span class="tube-designer-config-preview">${renderSchematic(template?.id, values)}</span>
      <div><strong>${escapeText(view?.tubeDesignerAddInstanceName)}</strong><span>${escapeText(entry?.displayName ?? getTemplateDisplayName(template))} · ${escapeText(formatProductDimensions(template?.id, values))}</span></div>
    </div>
    ${renderSecurityWindowReview(template, values)}
    ${renderParameterPresetBar(template, values, view, "add")}
    ${template?.available
      ? groupTree.map((group) => renderAddParameterGroup(group, values, pending, view, "add", template)).join("")
      : `<div class="tube-designer-empty">${escapeText(template?.status ?? "该模板尚不可用。")}</div>`}
  `;
}

function visibleParameterFields(template, values) {
  const selector = String(template?.extensions?.parameterPresets?.selectorParameter ?? "").trim();
  const importedPrefixes = new Set(Object.keys(getProfileOverrides(values)));
  return visibleFields(template?.parameters ?? [], values)
    .filter((field) => {
      const key = String(field?.key ?? field?.name ?? "");
      if (key === selector) return false;
      for (const prefix of importedPrefixes) {
        const profileKeys = new Set([
          `${prefix}Width`, `${prefix}WallThickness`, `${prefix}CornerRadius`,
          prefix === "tread" ? `${prefix}DepthProfile` : `${prefix}Depth`,
        ]);
        if (profileKeys.has(key)) return false;
      }
      return true;
    });
}

function renderParameterPresetBar(template, values, view, mode) {
  if (!template) return "";
  const definition = template?.extensions?.parameterPresets ?? {};
  const selector = String(definition?.selectorParameter ?? "").trim();
  const selectorField = (template?.parameters ?? [])
    .find((field) => String(field?.key ?? field?.name ?? "") === selector);
  const choiceLabels = new Map((selectorField?.options ?? selectorField?.choices ?? [])
    .map((choice) => [String(choice?.value ?? choice), String(choice?.label ?? choice?.displayName ?? choice)]));
  const builtIns = (Array.isArray(definition?.presets) ? definition.presets : [])
    .filter((preset) => preset?.value && preset?.values && typeof preset.values === "object");
  const userPresets = getUserParameterPresets(view, template.id);
  const storedSelection = String(mode === "add"
    ? view?.tubeDesignerAddPresetSelection
    : view?.tubeDesignerRightPresetSelection);
  const validSelections = new Set([
    "custom",
    ...builtIns.map((preset) => `builtin:${preset.value}`),
    ...userPresets.map((preset) => `user:${preset.id}`),
  ]);
  const inferredSelection = selector && builtIns.some((preset) => String(preset.value) === String(values?.[selector]))
    ? `builtin:${values[selector]}`
    : "custom";
  const selected = validSelections.has(storedSelection) ? storedSelection : inferredSelection;
  const selectedUserPreset = selected.startsWith("user:")
    ? userPresets.find((preset) => `user:${preset.id}` === selected)
    : null;
  const customersById = new Map((view?.tubeDesignerUserData?.customers ?? [])
    .map((customer) => [String(customer?.id ?? ""), customer]));
  return `
    <section class="tube-designer-user-preset-bar" data-tube-designer-preset-mode="${escapeAttribute(mode)}">
      <label>
        <span>常用参数</span>
        <select data-cam-change-action="tube-designer-apply-parameter-preset" data-tube-designer-preset-selection="${escapeAttribute(mode)}" data-tube-designer-preset-mode="${escapeAttribute(mode)}" ${view?.pending ? "disabled" : ""}>
          <option value="custom" ${selected === "custom" ? "selected" : ""}>当前自定义参数</option>
          ${builtIns.length ? `<optgroup label="模板内置">${builtIns.map((preset) => {
            const value = `builtin:${preset.value}`;
            return `<option value="${escapeAttribute(value)}" ${selected === value ? "selected" : ""}>${escapeText(choiceLabels.get(String(preset.value)) ?? preset.value)}</option>`;
          }).join("")}</optgroup>` : ""}
          ${userPresets.length ? `<optgroup label="我的常用方案">${userPresets.map((preset) => {
            const value = `user:${preset.id}`;
            const customerName = customersById.get(String(preset.customerId ?? ""))?.name;
            const versionNote = preset.templateVersion && preset.templateVersion !== template.version ? " · 旧版" : "";
            const label = customerName ? `${customerName} / ${preset.name}${versionNote}` : `${preset.name}${versionNote}`;
            return `<option value="${escapeAttribute(value)}" ${selected === value ? "selected" : ""}>${escapeText(label)}</option>`;
          }).join("")}</optgroup>` : ""}
        </select>
      </label>
      <div>
        <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-open-save-preset" data-tube-designer-preset-mode="${escapeAttribute(mode)}" ${view?.pending ? "disabled" : ""}>另存为常用</button>
        ${selectedUserPreset ? `<button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-open-manage-preset" data-tube-designer-preset-mode="${escapeAttribute(mode)}" data-tube-designer-preset-id="${escapeAttribute(selectedUserPreset.id)}" ${view?.pending ? "disabled" : ""}>管理</button>` : ""}
      </div>
    </section>`;
}

function getUserParameterPresets(view, templateId) {
  return (Array.isArray(view?.tubeDesignerUserData?.parameterPresets)
    ? view.tubeDesignerUserData.parameterPresets : [])
    .filter((preset) => String(preset?.templateId ?? "") === String(templateId ?? ""))
    .sort((left, right) => String(left?.name ?? "").localeCompare(String(right?.name ?? ""), "zh-CN"));
}

function renderParameterPresetDialog(designer, view) {
  const state = view?.tubeDesignerPresetDialog ?? {};
  const mode = state.mode === "add" ? "add" : "right";
  const templateId = mode === "add"
    ? view?.tubeDesignerAddTemplateId
    : designer?.product?.templateId;
  const template = getTemplateById(designer?.templates ?? [], templateId);
  const preset = state.presetId
    ? getUserParameterPresets(view, templateId).find((item) => String(item.id) === String(state.presetId))
    : null;
  const customers = Array.isArray(view?.tubeDesignerUserData?.customers)
    ? view.tubeDesignerUserData.customers : [];
  return `
    <div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop" role="presentation">
      <section class="tube-designer-preset-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-preset-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-preset-title">${preset ? "管理常用参数" : "保存为常用参数"}</strong><span>${escapeText(getTemplateDisplayName(template))} · 只保存可复用参数，不保存编号和主尺寸。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-preset-dialog" aria-label="关闭" ${view?.pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-preset-dialog-body">
          <label class="tube-designer-field wide">方案名称<input type="text" data-tube-designer-preset-name value="${escapeAttribute(state.name ?? preset?.name ?? "")}" maxlength="120" placeholder="例如：张经理家常用不锈钢配置" /></label>
          <label class="tube-designer-field wide">已有客户<select data-tube-designer-preset-customer-id>
            <option value="">不关联客户</option>
            ${customers.map((customer) => `<option value="${escapeAttribute(customer?.id)}" ${String(customer?.id) === String(state.customerId ?? preset?.customerId ?? "") ? "selected" : ""}>${escapeText(customer?.name)}</option>`).join("")}
          </select></label>
          <label class="tube-designer-field wide">新客户<input type="text" data-tube-designer-preset-customer-name value="${escapeAttribute(state.customerName ?? "")}" maxlength="120" placeholder="可选；填写后自动建立客户档案" /></label>
          <p>以后选择这个方案时，只覆盖当前模板中仍然存在的参数；模板升级后也不会写入未知字段。</p>
        </div>
        <footer class="tube-designer-preset-dialog-footer">
          ${preset ? `<button class="tube-designer-danger" data-cam-action="tube-designer-delete-parameter-preset" data-tube-designer-preset-id="${escapeAttribute(preset.id)}" ${view?.pending ? "disabled" : ""}>删除方案</button>` : "<span></span>"}
          <button class="tube-designer-secondary" data-cam-action="tube-designer-close-preset-dialog" ${view?.pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-save-parameter-preset" data-tube-designer-preset-mode="${escapeAttribute(mode)}" data-tube-designer-preset-id="${escapeAttribute(preset?.id ?? "")}" ${view?.pending ? "disabled" : ""}>${preset ? "保存修改" : "保存方案"}</button>
        </footer>
      </section>
    </div>`;
}

function renderImportedProfileDialog(_designer, view) {
  const state = view?.tubeDesignerProfileDialog ?? {};
  const profile = state.profile ?? {};
  const savedId = String(state.savedProfileId ?? "");
  return `
    <div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop" role="presentation">
      <section class="tube-designer-preset-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-profile-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-profile-title">${savedId ? "管理我的管型" : "保存为我的管型"}</strong><span>保存的是 DXF 原始截面的冻结副本，下次可直接选用。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-profile-dialog" aria-label="关闭" ${view?.pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-preset-dialog-body">
          <label class="tube-designer-field wide">管型名称<input type="text" data-tube-designer-profile-name value="${escapeAttribute(state.name ?? profile.name ?? "")}" maxlength="120" placeholder="例如：供应商 A / 50 系列梅花管" /></label>
          <div class="tube-designer-imported-profile-summary">
            <strong>${escapeText(profile.sourceFileName ?? "DXF 截面")}</strong>
            <span>${escapeText(profile.specification ?? "")}</span>
            <small>${escapeText(profile.sourceUnit ?? "毫米")} · ${Number(profile.contourCount ?? profile.contours?.length ?? 0)} 条轮廓 · 不支持参数改形</small>
          </div>
        </div>
        <footer class="tube-designer-preset-dialog-footer">
          ${savedId ? `<button class="tube-designer-danger" data-cam-action="tube-designer-delete-imported-profile" data-tube-designer-profile-id="${escapeAttribute(savedId)}" ${view?.pending ? "disabled" : ""}>删除管型</button>` : "<span></span>"}
          <button class="tube-designer-secondary" data-cam-action="tube-designer-close-profile-dialog" ${view?.pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-save-imported-profile" data-tube-designer-profile-id="${escapeAttribute(savedId)}" ${view?.pending ? "disabled" : ""}>${savedId ? "保存名称" : "保存管型"}</button>
        </footer>
      </section>
    </div>`;
}

function renderImportedProfileLibraryDialog(view) {
  const profiles = (Array.isArray(view?.tubeDesignerUserData?.profiles)
    ? view.tubeDesignerUserData.profiles : [])
    .slice().sort((left, right) => String(left?.name ?? "").localeCompare(String(right?.name ?? ""), "zh-CN"));
  return `
    <div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop" role="presentation">
      <section class="tube-designer-preset-dialog tube-designer-profile-library-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-profile-library-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-profile-library-title">我的管型管理</strong><span>${profiles.length} 个已保存管型 · 可重命名或删除</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-profile-library" aria-label="关闭" ${view?.pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-profile-library-list">
          ${profiles.length ? profiles.map((profile) => `
            <article class="tube-designer-profile-library-row" data-tube-designer-library-profile-row data-tube-designer-profile-id="${escapeAttribute(profile.id)}">
              <div class="tube-designer-profile-library-copy">
                <input type="text" data-tube-designer-library-profile-name value="${escapeAttribute(profile.name ?? "")}" maxlength="120" aria-label="管型名称" ${view?.pending ? "disabled" : ""} />
                <span>${escapeText(profile.previewProfile?.specification ?? profile.specification ?? "管型截面")}</span>
                <small>${escapeText(profile.sourceFileName ?? "管型资源")} · ${profile.profileType === "parametric-package" ? "参数可编辑" : `${Number(profile.contourCount ?? profile.contours?.length ?? 0)} 条轮廓`}</small>
              </div>
              <div class="tube-designer-profile-library-actions">
                <button class="tube-designer-secondary" data-cam-action="tube-designer-rename-library-profile" data-tube-designer-profile-id="${escapeAttribute(profile.id)}" ${view?.pending ? "disabled" : ""}>保存名称</button>
                <button class="tube-designer-danger" data-cam-action="tube-designer-delete-library-profile" data-tube-designer-profile-id="${escapeAttribute(profile.id)}" ${view?.pending ? "disabled" : ""}>删除</button>
              </div>
            </article>`).join("") : `<div class="tube-designer-profile-library-empty"><strong>还没有保存的管型</strong><span>先从管型参数区域导入 DXF，再保存到“我的管型”。</span></div>`}
        </div>
        <footer class="tube-designer-preset-dialog-footer">
          <span>删除只影响“我的管型”列表，不会破坏已经生成的产品。</span>
          <button class="tube-designer-primary" data-cam-action="tube-designer-close-profile-library" ${view?.pending ? "disabled" : ""}>完成</button>
        </footer>
      </section>
    </div>`;
}

function renderTemplateGroup(group, selectedEntryId, expandedGroups, pending, depth = 0) {
  const expanded = expandedGroups.has(group.key);
  const availableCount = countAvailableTemplates(group);
  return `
    <section class="tube-designer-template-group ${expanded ? "expanded" : ""}" data-tube-designer-template-group-depth="${depth}">
      <button type="button" class="tube-designer-template-group-toggle" data-cam-action="tube-designer-toggle-template-group" data-tube-designer-template-group-id="${escapeAttribute(group.key)}" aria-expanded="${expanded ? "true" : "false"}" ${pending ? "disabled" : ""}>
        <span><i aria-hidden="true"></i><strong>${escapeText(group.title)}</strong></span>
        <em>${availableCount} 种</em>
      </button>
      <div class="tube-designer-template-group-items" ${expanded ? "" : "hidden"}>
        ${group.children.map((child) => renderTemplateGroup(
            child,
            selectedEntryId,
            expandedGroups,
            pending,
            depth + 1,
          )).join("") + group.templates.map((item) => renderTemplateCard(
            item,
            item.catalogEntryId === selectedEntryId,
            pending,
          )).join("")}
      </div>
    </section>`;
}

function countAvailableTemplates(group) {
  return group.templates.filter((item) => item.available).length
    + group.children.reduce((total, child) => total + countAvailableTemplates(child), 0);
}

function renderTemplateCard(template, selected, pending) {
  const disabled = !template?.available;
  return `
    <button class="tube-designer-template-card ${selected ? "selected" : ""} ${disabled ? "disabled" : ""}" data-cam-action="tube-designer-select-template" data-tube-designer-template-id="${escapeAttribute(template?.id)}" data-tube-designer-catalog-preset-id="${escapeAttribute(template?.presetId ?? "")}" data-tube-designer-catalog-entry-id="${escapeAttribute(template?.catalogEntryId)}" aria-pressed="${selected ? "true" : "false"}" ${pending || disabled ? "disabled" : ""}>
      <span class="tube-designer-template-schematic">${renderSchematic(template?.id, template?.catalogParameters ?? {})}</span>
      <span><strong>${escapeText(template?.displayName ?? getTemplateDisplayName(template))}</strong><small>${template?.presetId ? "参数化款式 · 可调整尺寸" : `版本 ${escapeText(template?.version)}`}</small></span>
      <i aria-hidden="true"></i>
    </button>
  `;
}

function renderDisassemblySelector(designer, view) {
  const instances = designer.instances ?? [];
  const templates = designer.templates ?? [];
  const selected = new Set(view.tubeDesignerSelectedInstanceIds ?? []);
  const allSelected = instances.length > 0 && instances.every((item) => selected.has(item.entityId));
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-selection-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-disassemble-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-disassemble-title">选择要拆单的产品实例</strong><span>可选择一个或多个产品实例批量拆单。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-disassemble" aria-label="取消拆单" ${view.pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-selection-table-wrap">
          <table class="tube-designer-selection-table">
            <thead><tr><th><input type="checkbox" data-cam-action="tube-designer-toggle-all-instances" ${allSelected ? "checked" : ""} /></th><th>缩略图</th><th>实例名称</th><th>模板</th><th>外尺寸</th></tr></thead>
            <tbody>${instances.map((instance) => {
              const template = getTemplateById(templates, instance.templateId);
              const parameters = instance.parameters ?? {};
              return `<tr data-tube-designer-disassembly-instance-row="${escapeAttribute(instance.entityId)}">
                <td><input type="checkbox" data-cam-action="tube-designer-toggle-instance" data-tube-designer-instance-id="${escapeAttribute(instance.entityId)}" ${selected.has(instance.entityId) ? "checked" : ""} /></td>
                <td><span class="tube-designer-table-thumbnail">${renderSchematic(instance.templateId, parameters)}</span></td>
                <td><strong>${escapeText(instance.name)}</strong><small>${escapeText(instance.productCode)}</small></td>
                <td>${escapeText(getTemplateDisplayName(template) || instance.templateId)}</td>
                <td>${formatNumber(parameters.width)} × ${formatNumber(parameters.height)} mm</td>
              </tr>`;
            }).join("")}</tbody>
          </table>
        </div>
        <footer class="tube-designer-dialog-footer">
          <span>已选择 ${selected.size} / ${instances.length} 个实例</span>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-close-disassemble" ${view.pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-confirm-disassemble" ${view.pending || !selected.size ? "disabled" : ""}>${view.pending ? "正在拆单…" : "拆单"}</button>
        </footer>
      </section>
    </div>
  `;
}

function renderBreakdownDialog(designer, view) {
  const groups = getBreakdownGroups(designer, view);
  const allParts = groups.flatMap((group) => group.parts ?? []);
  const selected = new Set(view.tubeDesignerSelectedPartIds ?? allParts.map((part) => part.entityId));
  const selectedCount = allParts.filter((part) => selected.has(part.entityId)).length;
  const exportOperation = view.tubeDesignerExportOperation ?? null;
  const exportBusy = Boolean(exportOperation);
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-breakdown-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-breakdown-title" aria-busy="${exportBusy ? "true" : "false"}">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-breakdown-title">零件清单</strong><span data-tube-designer-breakdown-summary>${groups.length} 个产品 · ${allParts.length} 个零件 · 已选择 ${selectedCount} 个</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-breakdown" aria-label="关闭零件清单" ${exportBusy ? "disabled" : ""}>×</button>
        </header>
        ${renderDesignerBreakdownBody(designer, view)}
        <footer class="tube-designer-breakdown-footer">
          <div class="tube-designer-export-destination"><strong>STEP + Excel 分组导出</strong><span>${view.tubeDesignerExportDirectory ? `上次总目录：${escapeText(view.tubeDesignerExportDirectory)}` : "总目录生成零件清单.xlsx，每个产品创建自己的 STEP 子目录"}</span></div>
          <span data-tube-designer-export-selection-summary>将导出 ${selectedCount} 个零件</span>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-enter-cutting" ${view.pending || exportBusy || !selectedCount ? "disabled" : ""}>进入下料</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-export-selected" data-tube-designer-export-selected ${view.pending || exportBusy || !selectedCount ? "disabled" : ""}>${exportBusy ? "正在导出…" : "选择目录并导出"}</button>
        </footer>
        ${exportBusy ? renderExportWait(exportOperation) : ""}
      </section>
    </div>
  `;
}

export function renderDesignerBreakdownBody(designer, view) {
  const groups = getBreakdownGroups(designer, view);
  const { group: currentGroup, index: pageIndex } = resolveBreakdownPage(groups, view);
  const pageParts = currentGroup?.parts ?? [];
  const selected = new Set(view?.tubeDesignerSelectedPartIds
    ?? groups.flatMap((group) => group.parts ?? []).map((part) => part.entityId));
  const allPagePartsSelected = pageParts.length > 0
    && pageParts.every((part) => selected.has(part.entityId));
  const exportBusy = Boolean(view?.tubeDesignerExportOperation);
  const currentName = currentGroup?.name ?? "暂无产品";
  const currentProductId = String(currentGroup?.productEntityId ?? "");
  return `
    <div class="tube-designer-breakdown-body" data-tube-designer-breakdown-page="${escapeAttribute(currentProductId)}">
      <div class="tube-designer-breakdown-tools">
        <span class="tube-designer-breakdown-current-product">
          <strong>${escapeText(currentName)}</strong>
          <small>${groups.length ? `第 ${pageIndex + 1} / ${groups.length} 页 · ${pageParts.length} 个零件` : "没有可显示的零件"}</small>
        </span>
        <div>
          <button type="button" data-cam-action="tube-designer-expand-breakdown-all" ${exportBusy || !currentGroup ? "disabled" : ""}>展开本页</button>
          <button type="button" data-cam-action="tube-designer-collapse-breakdown-all" ${exportBusy || !currentGroup ? "disabled" : ""}>折叠本页</button>
        </div>
      </div>
      ${renderBreakdownPagination(groups, pageIndex, exportBusy)}
      <div class="tube-designer-sheet-wrap">
        <table class="tube-designer-sheet">
          <thead><tr><th class="tube-designer-check-cell"><input type="checkbox" data-cam-action="tube-designer-toggle-all-parts" data-tube-designer-all-parts ${allPagePartsSelected ? "checked" : ""} aria-label="选择当前产品的全部零件" ${!currentGroup ? "disabled" : ""} /></th><th class="tube-designer-tree-column">名称</th><th>规格</th><th>示意图</th><th class="tube-designer-action-column">复尺</th></tr></thead>
          <tbody data-tube-designer-breakdown-rows>${renderDesignerBreakdownRows(designer, view)}</tbody>
        </table>
      </div>
    </div>`;
}

export function renderDesignerBreakdownRows(designer, view) {
  const groups = getBreakdownGroups(designer, view);
  const currentGroup = resolveBreakdownPage(groups, view).group;
  const allParts = currentGroup?.parts ?? [];
  const selected = new Set(view?.tubeDesignerSelectedPartIds ?? allParts.map((part) => part.entityId));
  return currentGroup ? renderProductPartGroup(currentGroup, selected, view) : "";
}

function getBreakdownGroups(designer, view) {
  const visibleIds = new Set(view?.tubeDesignerBreakdownProductIds ?? []);
  return (designer?.manufacturingGroups ?? [])
    .filter((group) => !visibleIds.size || visibleIds.has(group.productEntityId));
}

function resolveBreakdownPage(groups, view) {
  const requestedId = String(view?.tubeDesignerBreakdownPageProductId ?? "");
  const requestedIndex = groups.findIndex((group) => String(group.productEntityId ?? "") === requestedId);
  const index = requestedIndex >= 0 ? requestedIndex : 0;
  return { group: groups[index] ?? null, index };
}

function renderBreakdownPagination(groups, pageIndex, disabled) {
  if (!groups.length) return `<nav class="tube-designer-breakdown-pagination" aria-label="产品实例分页"><span>暂无产品实例</span></nav>`;
  const previous = groups[Math.max(0, pageIndex - 1)];
  const next = groups[Math.min(groups.length - 1, pageIndex + 1)];
  return `
    <nav class="tube-designer-breakdown-pagination" aria-label="产品实例分页">
      <button type="button" data-cam-action="tube-designer-set-breakdown-page" data-tube-designer-breakdown-page-product-id="${escapeAttribute(previous.productEntityId)}" ${disabled || pageIndex === 0 ? "disabled" : ""}>上一页</button>
      <div class="tube-designer-breakdown-page-numbers">${renderBreakdownPageNumbers(groups, pageIndex, disabled)}</div>
      <span>第 <strong>${pageIndex + 1}</strong> / ${groups.length} 页</span>
      <button type="button" data-cam-action="tube-designer-set-breakdown-page" data-tube-designer-breakdown-page-product-id="${escapeAttribute(next.productEntityId)}" ${disabled || pageIndex === groups.length - 1 ? "disabled" : ""}>下一页</button>
    </nav>`;
}

function renderBreakdownPageNumbers(groups, pageIndex, disabled) {
  const indexes = [...new Set([0, groups.length - 1, pageIndex - 2, pageIndex - 1, pageIndex, pageIndex + 1, pageIndex + 2])]
    .filter((index) => index >= 0 && index < groups.length)
    .sort((left, right) => left - right);
  const items = [];
  for (let position = 0; position < indexes.length; position += 1) {
    const index = indexes[position];
    if (position > 0 && index - indexes[position - 1] > 1) {
      items.push(`<span class="tube-designer-breakdown-page-gap" aria-hidden="true">…</span>`);
    }
    const group = groups[index];
    const current = index === pageIndex;
    items.push(`<button type="button" data-cam-action="tube-designer-set-breakdown-page" data-tube-designer-breakdown-page-product-id="${escapeAttribute(group.productEntityId)}" title="${escapeAttribute(group.name)}" ${current ? `class="is-current" aria-current="page"` : ""} ${disabled ? "disabled" : ""}>${index + 1}</button>`);
  }
  return items.join("");
}

function renderExportWait(operation) {
  const phase = String(operation?.phase ?? "preparing");
  const completed = Math.max(0, Number(operation?.completed ?? 0));
  const total = Math.max(0, Number(operation?.total ?? 0));
  const determinate = total > 0 && phase !== "selecting-directory" && phase !== "preparing";
  const percent = determinate ? Math.min(100, (completed / total) * 100) : 0;
  const title = phase === "selecting-directory"
    ? "等待选择导出目录"
    : phase === "exporting"
      ? `正在导出 STEP（${Math.min(completed, total)} / ${total}）`
      : phase === "workbook"
        ? "正在生成 Excel 零件清单"
        : phase === "completed" ? "正在确认导出结果" : "正在准备导出";
  return `
    <div class="tube-designer-export-wait" role="status" aria-live="polite">
      <div class="tube-designer-export-progress-card">
        <span class="tube-designer-export-spinner" aria-hidden="true"></span>
        <strong data-tube-designer-export-progress-title>${escapeText(title)}</strong>
        <span data-tube-designer-export-progress-message>${escapeText(operation?.message ?? "请保持当前窗口开启")}</span>
        <div class="tube-designer-export-progress-track ${determinate ? "" : "is-indeterminate"}" data-tube-designer-export-progress-track>
          <i data-tube-designer-export-progress-bar style="width:${determinate ? percent : 36}%"></i>
        </div>
        <small data-tube-designer-export-progress-count>${total > 0 && phase !== "selecting-directory" ? `${Math.min(completed, total)} / ${total}` : "准备中"}</small>
        <em>完成或失败前，本窗口会保持锁定，请勿退出软件。</em>
      </div>
    </div>`;
}

function renderPartInspectionDialog(designer, view) {
  const partId = String(view.tubeDesignerInspectedPartId ?? "");
  const part = (designer.manufacturingGroups ?? [])
    .flatMap((group) => group.parts ?? [])
    .find((item) => String(item.entityId) === partId);
  if (!part) return "";
  return `
    <div class="tube-designer-modal-backdrop tube-designer-part-inspection-backdrop" role="presentation">
      <section class="tube-designer-part-inspection-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-part-inspection-title">
        <header class="tube-designer-dialog-header">
          <div>
            <strong id="tube-designer-part-inspection-title">零件复尺 · ${escapeText(partDisplayName(part))}</strong>
            <span>${escapeText(part.partNumber)} · 尺寸以当前版本的最终三维实体为准</span>
          </div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-part-inspection" aria-label="关闭零件复尺">×</button>
        </header>
        <div class="tube-designer-part-inspection-body">
          <div class="tube-designer-part-inspection-stage">
            <div class="tube-designer-part-inspection-toolbar">
              <span data-tube-designer-inspection-status>正在载入零件三维资源…</span>
              <div>
                <button data-cam-action="tube-designer-inspection-iso-view">默认视图</button>
                <button data-cam-action="tube-designer-inspection-fit-view">适合窗口</button>
              </div>
            </div>
            <div class="tube-designer-part-inspection-progress" data-tube-designer-inspection-progress role="progressbar" aria-label="零件复尺处理进度" aria-valuetext="正在载入最终零件三维资源" aria-hidden="false">
              <i aria-hidden="true"></i>
            </div>
            <div class="tube-designer-part-inspection-viewport" data-tube-designer-part-inspection-viewport></div>
          </div>
          <aside class="tube-designer-measurement-panel">
            <section class="tube-designer-automatic-dimension-section">
              <div class="tube-designer-measurement-heading">
                <div><strong>自动尺寸</strong><span>单位：毫米</span></div>
                <button data-cam-action="tube-designer-toggle-automatic-dimensions" aria-pressed="true">隐藏标尺</button>
              </div>
              <div data-tube-designer-automatic-dimensions>
                <div class="tube-designer-dimension-empty">正在读取零件尺寸…</div>
              </div>
            </section>
            <div class="tube-designer-measurement-help">
              <strong>视图操作</strong>
              <span>右键拖动：旋转</span>
              <span>中键拖动：平移</span>
              <span>滚轮：缩放</span>
              <small>自动尺寸直接分析当前版本的最终三维实体。</small>
            </div>
          </aside>
        </div>
      </section>
    </div>`;
}

function renderProductPartGroup(group, selected, view) {
  const parameters = group.parameters ?? {};
  const parts = [...(group.parts ?? [])].sort((left, right) => Number(left.index) - Number(right.index));
  if (!parts.length) return "";
  const productId = String(group.productEntityId ?? "");
  const categories = buildPartCategories(parts, productId);
  const collapsedProducts = new Set(view.tubeDesignerCollapsedBreakdownProductIds ?? []);
  const expandedCategories = new Set(view.tubeDesignerExpandedBreakdownCategoryIds ?? []);
  const productCollapsed = collapsedProducts.has(productId);
  const productPartIds = parts.map((part) => String(part.entityId));
  const productSelection = selectionState(productPartIds, selected);
  const productRow = `
    <tr class="tube-designer-sheet-row tube-designer-product-row" data-tube-designer-product-row="${escapeAttribute(productId)}">
      <td class="tube-designer-check-cell">${selectionCheckbox("tube-designer-toggle-part-group", productPartIds, productSelection, `选择产品 ${group.name} 的全部零件`)}</td>
      <td class="tube-designer-tree-cell tube-designer-tree-level-0">
        <span class="tube-designer-tree-node">
          ${treeToggle("tube-designer-toggle-product-tree", productCollapsed, { tubeDesignerProductId: productId }, group.name)}
          <span class="tube-designer-tree-node-copy"><strong>${escapeText(group.name)}</strong><small>${categories.length} 种 · ${parts.length} 个零件</small></span>
        </span>
      </td>
      <td>${escapeText(formatProductDimensions(group.templateId, parameters))}</td>
      <td><span class="tube-designer-tree-product-thumbnail">${renderSchematic(group.templateId, parameters)}</span></td>
      <td>—</td>
    </tr>`;
  if (productCollapsed) return productRow;
  return productRow + categories.map((category) => renderPartCategory(
    category,
    selected,
    expandedCategories.has(category.id),
  )).join("");
}

function renderPartCategory(category, selected, expanded) {
  const partIds = category.parts.map((part) => String(part.entityId));
  const state = selectionState(partIds, selected);
  const representative = category.parts[0];
  const categoryRow = `
    <tr class="tube-designer-sheet-row tube-designer-category-row" data-tube-designer-category-row="${escapeAttribute(category.id)}">
      <td class="tube-designer-check-cell">${selectionCheckbox("tube-designer-toggle-part-group", partIds, state, `选择种类 ${category.name} 的全部零件`)}</td>
      <td class="tube-designer-tree-cell tube-designer-tree-level-1">
        <span class="tube-designer-tree-node">
          ${treeToggle("tube-designer-toggle-category-tree", !expanded, { tubeDesignerCategoryId: category.id }, category.name)}
          <span class="tube-designer-tree-node-copy"><strong>${escapeText(category.name)}</strong><small>${category.parts.length} 个零件</small></span>
        </span>
      </td>
      <td class="tube-designer-part-specification">${escapeText(formatCategorySpecification(category))}</td>
      <td>${renderPartThumbnail(representative, `${category.name} 种类示意图`)}</td>
      <td>—</td>
    </tr>`;
  if (!expanded) return categoryRow;
  return categoryRow + category.parts.map((part, index) => renderPartRow(
    part,
    selected.has(part.entityId),
    index === category.parts.length - 1,
  )).join("");
}

function renderPartRow(part, checked, isLastInCategory = false) {
  const spec = formatPartSpecificationWithLength(part);
  return `
    <tr class="tube-designer-sheet-row tube-designer-part-row${isLastInCategory ? " tube-designer-category-last-row" : ""}" data-tube-designer-part-row="${escapeAttribute(part.entityId)}">
      <td class="tube-designer-check-cell"><input type="checkbox" data-cam-action="tube-designer-toggle-part" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" data-tube-designer-selection-ids="${escapeAttribute(part.entityId)}" ${checked ? "checked" : ""} aria-label="选择零件 ${escapeAttribute(part.partNumber)}" /></td>
      <td class="tube-designer-tree-cell tube-designer-tree-level-2"><span class="tube-designer-part-name">${escapeText(partDisplayName(part))}</span></td>
      <td class="tube-designer-part-specification">${escapeText(spec)}</td>
      <td>${renderPartThumbnail(part, `${part.partNumber} 三维示意图`)}</td>
      <td><button class="tube-designer-part-inspection-button" data-cam-action="tube-designer-open-part-inspection" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" data-tube-designer-inspect-part-id="${escapeAttribute(part.entityId)}" title="进入零件三维复尺">复尺</button></td>
    </tr>
  `;
}

export function buildPartCategories(parts = [], productId = "") {
  const categories = new Map();
  for (const part of parts) {
    const properties = part?.properties ?? {};
    const explicitKey = String(properties["manufacturing.categoryKey"] ?? "").trim();
    const key = explicitKey || `unique:${String(part?.entityId ?? part?.stableKey ?? part?.partNumber ?? "")}`;
    let category = categories.get(key);
    if (!category) {
      category = {
        id: `${productId}:${hashText(key)}`,
        key,
        name: String(properties["manufacturing.categoryName"] ?? partDisplayName(part)).trim() || "未命名种类",
        parts: [],
        quantity: 0,
        specifications: [],
        lengths: [],
      };
      categories.set(key, category);
    }
    category.parts.push(part);
    category.quantity += partQuantity(part);
  }
  for (const category of categories.values()) {
    category.specifications = [...new Set(category.parts.map((part) => formatPartSpecification(part)))];
    category.lengths = [...new Set(category.parts.filter((part) => !["plate", "glass", "accessory"].includes(manufacturingPartKind(part)))
      .map((part) => Number(part?.length ?? 0)))];
  }
  return [...categories.values()];
}

function selectionState(partIds, selected) {
  const selectedCount = partIds.reduce((count, id) => count + (selected.has(id) ? 1 : 0), 0);
  return {
    checked: partIds.length > 0 && selectedCount === partIds.length,
    indeterminate: selectedCount > 0 && selectedCount < partIds.length,
  };
}

function selectionCheckbox(action, partIds, state, label) {
  return `<input type="checkbox" data-cam-action="${action}" data-tube-designer-selection-ids="${escapeAttribute(partIds.join(" "))}" ${state.checked ? "checked" : ""} ${state.indeterminate ? "data-tube-designer-indeterminate=\"true\"" : ""} aria-label="${escapeAttribute(label)}" />`;
}

function treeToggle(action, collapsed, data, label) {
  const attributes = Object.entries(data).map(([key, value]) => `data-${key.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`)}="${escapeAttribute(value)}"`).join(" ");
  return `<button class="tube-designer-tree-toggle" data-cam-action="${action}" ${attributes} aria-expanded="${collapsed ? "false" : "true"}" aria-label="${collapsed ? "展开" : "折叠"}${escapeAttribute(label)}" title="${collapsed ? "展开" : "折叠"}"><i aria-hidden="true"></i></button>`;
}

function renderPartThumbnail(part, label) {
  return `<canvas width="116" height="68" data-tube-designer-part-thumbnail data-tube-preview-url="${escapeAttribute(part.thumbnailGeometryResourceId)}" data-tube-preview-version="${escapeAttribute(part.thumbnailGeometryResourceVersion)}" aria-label="${escapeAttribute(label)}"></canvas>`;
}

function partQuantity(part) {
  const quantity = Number(part?.quantity ?? 1);
  return Number.isFinite(quantity) && quantity > 0 ? quantity : 1;
}

function hashText(value) {
  let hash = 2166136261;
  for (const character of String(value)) {
    hash ^= character.codePointAt(0);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(36);
}

function renderSchematic(templateId, parameters = {}, className = "") {
  const width = Math.max(1, Number(parameters.width ?? 1200));
  const height = Math.max(1, Number(parameters.height ?? 1800));
  const aspect = Math.max(.45, Math.min(1.5, width / height));
  const x = 50 - 31 * aspect;
  const w = 62 * aspect;
  const rail = (y) => `<line x1="${x + 3}" y1="${y}" x2="${x + w - 3}" y2="${y}" />`;
  let content = "";
  if (["straight-steel-staircase", "l-turn-steel-staircase", "u-turn-steel-staircase"].includes(templateId)) {
    content = renderSteelStaircaseSchematic(templateId, parameters);
  } else if (templateId === "straight-stair-railing") {
    content = renderStraightStairRailingSchematic(parameters);
  } else if (templateId === "modular-guardrail") {
    content = renderGuardrailSchematic(parameters);
  } else if (["two-face-security-window", "three-face-security-window", "five-face-security-window"].includes(templateId)) {
    content = renderMultiFaceSchematic(templateId, parameters);
  } else if (templateId === "single-face-security-window" && schematicDoorEnabled(parameters)) {
    const outer = parameters.frameLayout === "top_bottom"
      ? `<line x1="${x}" y1="8" x2="${x + w}" y2="8" /><line x1="${x}" y1="92" x2="${x + w}" y2="92" />`
      : parameters.frameLayout === "four_sides"
        ? `<rect x="${x}" y="8" width="${w}" height="84" rx="2" />`
        : `<line x1="${x}" y1="8" x2="${x}" y2="92" /><line x1="${x + w}" y1="8" x2="${x + w}" y2="92" />`;
    const door = renderProjectedDoor({
      ...parameters,
      doorUOffset: parameters.doorLeft ?? 160,
      doorVOffset: parameters.doorBottom ?? 350,
    }, [x, 92], [x + w, 92], [x, 8], width, height);
    content = `${outer}${rail(27)}${rail(47)}${rail(68)}${rail(84)}<line x1="50" y1="10" x2="50" y2="90" />${door}`;
  } else if (templateId === "single-face-security-window" && parameters.frameLayout === "four_sides") {
    const groove = String(parameters.frameJoinType ?? "").startsWith("v_groove_90:")
      ? `<path class="notch" d="M ${x + w - 8} 8 l 4 5 l 4 -5" />` : "";
    content = `<rect x="${x}" y="8" width="${w}" height="84" rx="2" />${rail(28)}${rail(48)}${rail(68)}<line x1="50" y1="8" x2="50" y2="92" />${groove}`;
  } else if (templateId === "single-face-security-window" && parameters.frameLayout === "top_bottom") {
    content = `<line x1="${x}" y1="8" x2="${x + w}" y2="8" /><line x1="${x}" y1="92" x2="${x + w}" y2="92" /><line x1="50" y1="10" x2="50" y2="90" />${rail(24)}${rail(41)}${rail(59)}${rail(76)}`;
  } else if (templateId === "empty") {
    content = `<rect class="placeholder" x="24" y="14" width="52" height="72" rx="4" /><path class="plus" d="M50 38v24M38 50h24" />`;
  } else {
    content = `<line x1="${x}" y1="8" x2="${x}" y2="92" /><line x1="${x + w}" y1="8" x2="${x + w}" y2="92" /><line x1="50" y1="10" x2="50" y2="90" />${rail(24)}${rail(41)}${rail(59)}${rail(76)}`;
  }
  if (templateId === "single-face-security-window" && !schematicDoorEnabled(parameters)) {
    content += renderProjectedPlate(parameters, (u, v) => [x + w * u, 92 - 84 * v], .5, .5, width, height);
  }
  return `<svg class="tube-designer-schematic ${escapeAttribute(className)}" viewBox="0 0 100 100" role="img" aria-label="产品示意图">${content}</svg>`;
}

function renderSteelStaircaseSchematic(templateId, parameters = {}) {
  const railingVisible = String(parameters.railingSide ?? "both") !== "none";
  if (templateId === "l-turn-steel-staircase") {
    const right = String(parameters.turnDirection ?? "left") === "right";
    const transform = right ? ' transform="translate(100 0) scale(-1 1)"' : "";
    return `<g${transform}>
      <path class="stair-reference" d="M8 82 h34 v-10 h10 V37 h10 V27 h29" />
      <path class="stair-infill" d="M8 78 h8 v-6 h8 v-6 h8 v-6 h10 M52 68 v-8 h8 v-8 h8 v-8 h8 v-8 h15" />
      <path class="stair-post" d="M42 82 V52 M52 72 V42 M91 27 V7" />
      ${railingVisible ? '<path class="stair-handrail" d="M8 58 L42 42 L52 32 L91 7" />' : ""}
      <rect class="stair-reference" x="42" y="68" width="10" height="14" rx="1" />
    </g>`;
  }
  if (templateId === "u-turn-steel-staircase") {
    return `<path class="stair-reference" d="M10 84 h34 V26 h46 M10 70 h27 V33 h53" />
      <path class="stair-infill" d="M14 78 h7 v-7 h7 v-7 h7 v-7 h9 M90 39 h-8 v7 h-8 v7 h-8 v7 h-8 v7 h-8" />
      <rect class="stair-reference" x="37" y="21" width="20" height="18" rx="1" />
      <path class="stair-post" d="M10 84 V59 M44 57 V32 M50 67 V42 M90 39 V14" />
      ${railingVisible ? '<path class="stair-handrail" d="M10 59 L44 32 M50 42 L90 14" />' : ""}`;
  }
  let content = '<path class="stair-reference" d="M7 84 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h10 v-7 h16" />';
  content += '<line class="stair-infill" x1="8" y1="79" x2="92" y2="36" />';
  if (railingVisible) {
    content += '<line class="stair-handrail" x1="8" y1="55" x2="92" y2="12" />';
    for (const [x, lower, upper] of [[8,79,55],[36,65,41],[64,51,27],[92,36,12]]) {
      content += `<line class="stair-post" x1="${x}" y1="${lower}" x2="${x}" y2="${upper}" />`;
    }
  }
  return content;
}

function renderStraightStairRailingSchematic(parameters = {}) {
  const start = [9, 80];
  const end = [91, 34];
  const railingHeight = 30;
  const point = (ratio, offset = 0) => [
    start[0] + (end[0] - start[0]) * ratio,
    start[1] + (end[1] - start[1]) * ratio - offset,
  ];
  const line = (from, to, className) => `<line class="${className}" x1="${from[0]}" y1="${from[1]}" x2="${to[0]}" y2="${to[1]}" />`;
  let content = `<path class="stair-reference" d="M7 84 h12 v-7 h12 v-7 h12 v-7 h12 v-7 h12 v-7 h12 v-7 h14" />`;
  content += line(point(0, railingHeight), point(1, railingHeight), "stair-handrail");
  for (const ratio of [0, 0.34, 0.67, 1]) {
    content += line(point(ratio, 0), point(ratio, railingHeight), "stair-post");
  }
  const infillType = String(parameters.infillType ?? "vertical");
  if (infillType === "horizontal") {
    for (const offset of [9, 16, 23]) {
      content += line(point(0, offset), point(1, offset), "stair-infill");
    }
  } else if (infillType === "vertical") {
    content += line(point(0, 7), point(1, 7), "stair-infill");
    for (const ratio of [0.1, 0.2, 0.3, 0.43, 0.54, 0.64, 0.77, 0.88]) {
      content += line(point(ratio, 7), point(ratio, railingHeight), "stair-infill");
    }
  }
  return content;
}

function renderMultiFaceSchematic(templateId, parameters = {}) {
  if (templateId === "five-face-security-window") {
    return renderFiveFaceSchematic(parameters);
  }
  let points;
  if (templateId === "two-face-security-window") {
    points = String(parameters.sidePosition ?? "right") === "left"
      ? [[9, 14], [31, 25], [89, 25]]
      : [[11, 25], [69, 25], [91, 14]];
  } else {
    points = [[7, 13], [28, 25], [72, 25], [93, 13]];
  }

  const verticalSpan = 64;
  const bottom = points.map(([x, y]) => [x, y + verticalSpan]);
  const line = (start, end, className) => `<line class="${className}" x1="${start[0]}" y1="${start[1]}" x2="${end[0]}" y2="${end[1]}" />`;
  const pointBetween = (start, end, ratio) => [
    start[0] + (end[0] - start[0]) * ratio,
    start[1] + (end[1] - start[1]) * ratio,
  ];
  const pointText = (values) => values.map((point) => point.join(",")).join(" ");

  let faces = "";
  let grids = "";
  let frames = "";
  for (let index = 0; index < points.length - 1; index += 1) {
    const topStart = points[index];
    const topEnd = points[index + 1];
    const bottomStart = bottom[index];
    const bottomEnd = bottom[index + 1];
    faces += `<polygon class="multi-face-fill face-${index + 1}" points="${pointText([topStart, topEnd, bottomEnd, bottomStart])}" />`;
    frames += line(topStart, topEnd, "multi-face-frame");
    frames += line(bottomStart, bottomEnd, "multi-face-frame");
    for (const ratio of [0.3, 0.55, 0.8]) {
      grids += line(
        pointBetween(topStart, bottomStart, ratio),
        pointBetween(topEnd, bottomEnd, ratio),
        "multi-face-rail",
      );
    }
    const interiorPostCount = Math.abs(topEnd[0] - topStart[0]) >= 34 ? 2 : 1;
    for (let post = 1; post <= interiorPostCount; post += 1) {
      const ratio = post / (interiorPostCount + 1);
      grids += line(
        pointBetween(topStart, topEnd, ratio),
        pointBetween(bottomStart, bottomEnd, ratio),
        "multi-face-rod",
      );
    }
  }
  const posts = points.map((point, index) => line(
    point,
    bottom[index],
    index > 0 && index < points.length - 1 ? "multi-face-frame multi-face-corner" : "multi-face-frame",
  )).join("");
  let door = "";
  if (schematicDoorEnabled(parameters)) {
    let faceIndex = 0;
    let widths = [];
    if (templateId === "two-face-security-window") {
      const sideIsLeft = String(parameters.sidePosition ?? "right") === "left";
      widths = sideIsLeft
        ? [Number(parameters.sideWidth ?? 600), Number(parameters.frontWidth ?? 1200)]
        : [Number(parameters.frontWidth ?? 1200), Number(parameters.sideWidth ?? 600)];
      faceIndex = String(parameters.accessDoorFace ?? "front") === "side"
        ? (sideIsLeft ? 0 : 1)
        : (sideIsLeft ? 1 : 0);
    } else {
      widths = [
        Number(parameters.leftWidth ?? 600),
        Number(parameters.frontWidth ?? 1200),
        Number(parameters.rightWidth ?? 600),
      ];
      faceIndex = { left: 0, front: 1, right: 2 }[String(parameters.accessDoorFace ?? "front")] ?? 1;
    }
    door = renderProjectedDoor(
      parameters,
      bottom[faceIndex], bottom[faceIndex + 1], points[faceIndex],
      widths[faceIndex], Number(parameters.height ?? 1800),
    );
  }
  return `${faces}${grids}${frames}${posts}${door}`;
}

function renderFiveFaceSchematic(parameters = {}) {
  const back = { topLeft: [7, 7], topRight: [64, 7], bottomLeft: [7, 69], bottomRight: [64, 69] };
  const front = { topLeft: [25, 23], topRight: [92, 23], bottomLeft: [25, 92], bottomRight: [92, 92] };
  const pointBetween = (start, end, ratio) => [
    start[0] + (end[0] - start[0]) * ratio,
    start[1] + (end[1] - start[1]) * ratio,
  ];
  const pointText = (values) => values.map((point) => point.join(",")).join(" ");
  const line = (start, end, className) => `<line class="${className}" x1="${start[0]}" y1="${start[1]}" x2="${end[0]}" y2="${end[1]}" />`;
  const polygon = (values, className) => `<polygon class="${className}" points="${pointText(values)}" />`;

  const faces = [
    polygon([back.topLeft, back.topRight, front.topRight, front.topLeft], "multi-face-fill face-top"),
    polygon([back.bottomLeft, front.bottomLeft, front.bottomRight, back.bottomRight], "multi-face-fill face-bottom"),
    polygon([back.topLeft, front.topLeft, front.bottomLeft, back.bottomLeft], "multi-face-fill face-left"),
    polygon([back.topRight, back.bottomRight, front.bottomRight, front.topRight], "multi-face-fill face-right"),
    polygon([front.topLeft, front.topRight, front.bottomRight, front.bottomLeft], "multi-face-fill face-front"),
  ].join("");

  let grids = "";
  for (const ratio of [0.3, 0.55, 0.8]) {
    grids += line(
      pointBetween(front.topLeft, front.bottomLeft, ratio),
      pointBetween(front.topRight, front.bottomRight, ratio),
      "multi-face-rail",
    );
    grids += line(
      pointBetween(back.topLeft, back.bottomLeft, ratio),
      pointBetween(front.topLeft, front.bottomLeft, ratio),
      "multi-face-rail",
    );
    grids += line(
      pointBetween(back.topRight, back.bottomRight, ratio),
      pointBetween(front.topRight, front.bottomRight, ratio),
      "multi-face-rail",
    );
  }
  for (const ratio of [1 / 3, 2 / 3]) {
    grids += line(
      pointBetween(front.topLeft, front.topRight, ratio),
      pointBetween(front.bottomLeft, front.bottomRight, ratio),
      "multi-face-rod",
    );
  }
  for (const ratio of [0.38, 0.7]) {
    grids += line(
      pointBetween(back.topLeft, front.topLeft, ratio),
      pointBetween(back.topRight, front.topRight, ratio),
      "multi-face-rail",
    );
    grids += line(
      pointBetween(back.bottomLeft, front.bottomLeft, ratio),
      pointBetween(back.bottomRight, front.bottomRight, ratio),
      "multi-face-rail",
    );
  }
  for (const ratio of [1 / 3, 2 / 3]) {
    grids += line(
      pointBetween(back.topLeft, back.topRight, ratio),
      pointBetween(front.topLeft, front.topRight, ratio),
      "multi-face-rod",
    );
    grids += line(
      pointBetween(back.bottomLeft, back.bottomRight, ratio),
      pointBetween(front.bottomLeft, front.bottomRight, ratio),
      "multi-face-rod",
    );
  }
  grids += line(pointBetween(back.topLeft, front.topLeft, 0.5), pointBetween(back.bottomLeft, front.bottomLeft, 0.5), "multi-face-rod");
  grids += line(pointBetween(back.topRight, front.topRight, 0.5), pointBetween(back.bottomRight, front.bottomRight, 0.5), "multi-face-rod");

  const frameEdges = [
    [back.topLeft, back.topRight], [back.topRight, back.bottomRight],
    [back.bottomRight, back.bottomLeft], [back.bottomLeft, back.topLeft],
    [front.topLeft, front.topRight], [front.topRight, front.bottomRight],
    [front.bottomRight, front.bottomLeft], [front.bottomLeft, front.topLeft],
    [back.topLeft, front.topLeft], [back.topRight, front.topRight],
    [back.bottomLeft, front.bottomLeft], [back.bottomRight, front.bottomRight],
  ].map(([start, end]) => line(start, end, "multi-face-frame")).join("");
  let door = "";
  if (schematicDoorEnabled(parameters)) {
    const width = Number(parameters.frontWidth ?? 1200);
    const depth = Number(parameters.depth ?? 600);
    const height = Number(parameters.height ?? 1800);
    const surfaces = {
      left: [back.bottomLeft, front.bottomLeft, back.topLeft, depth, height],
      front: [front.bottomLeft, front.bottomRight, front.topLeft, width, height],
      right: [front.bottomRight, back.bottomRight, front.topRight, depth, height],
      top: [front.topLeft, front.topRight, back.topLeft, width, depth],
      bottom: [front.bottomLeft, front.bottomRight, back.bottomLeft, width, depth],
    };
    const surface = surfaces[String(parameters.accessDoorFace ?? "front")] ?? surfaces.front;
    door = renderProjectedDoor(parameters, ...surface);
  }
  return `${faces}${grids}${frameEdges}${door}`;
}

function schematicDoorEnabled(parameters) {
  const value = parameters.accessDoorEnabled ?? true;
  return value === true || value === "true" || value === "是";
}

function renderProjectedDoor(parameters, origin, uEnd, vEnd, uLength, vLength) {
  const safeU = Math.max(1, Number(uLength));
  const safeV = Math.max(1, Number(vLength));
  const { outsideWidth, outsideHeight } = securityWindowOpeningDimensions(parameters);
  const u0 = Number(parameters.doorUOffset ?? 80) / safeU;
  const v0 = Number(parameters.doorVOffset ?? 80) / safeV;
  const u1 = u0 + outsideWidth / safeU;
  const v1 = v0 + outsideHeight / safeV;
  if (![u0, v0, u1, v1].every(Number.isFinite) || outsideWidth <= 0 || outsideHeight <= 0) return "";
  const point = (u, v) => [
    origin[0] + (uEnd[0] - origin[0]) * u + (vEnd[0] - origin[0]) * v,
    origin[1] + (uEnd[1] - origin[1]) * u + (vEnd[1] - origin[1]) * v,
  ];
  const polygonPoints = (minimumU, minimumV, maximumU, maximumV) => [
    point(minimumU, minimumV), point(maximumU, minimumV),
    point(maximumU, maximumV), point(minimumU, maximumV),
  ].map((value) => value.join(",")).join(" ");
  const insetU = Math.min(0.035, (u1 - u0) * 0.18);
  const insetV = Math.min(0.035, (v1 - v0) * 0.18);
  return `<polygon class="multi-face-door-frame" points="${polygonPoints(u0, v0, u1, v1)}" />
    <polygon class="multi-face-door-leaf" points="${polygonPoints(u0 + insetU, v0 + insetV, u1 - insetU, v1 - insetV)}" />
    ${renderProjectedPlate(parameters, point, (u0 + u1) / 2, (v0 + v1) / 2, safeU, safeV)}`;
}

function renderProjectedPlate(parameters, point, centerU, centerV, uLength, vLength) {
  if (parameters.mainInfillMode !== "center_plate") return "";
  const width = Number(parameters.centerPlateWidth ?? 300) / uLength;
  const height = Number(parameters.centerPlateHeight ?? 600) / vLength;
  const u = centerU + Number(parameters.centerPlateHorizontalOffset ?? 0) / uLength;
  const v = centerV + Number(parameters.centerPlateVerticalOffset ?? 0) / vLength;
  if (![width, height, u, v].every(Number.isFinite) || width <= 0 || height <= 0) return "";
  const points = [[u - width / 2, v - height / 2], [u + width / 2, v - height / 2],
    [u + width / 2, v + height / 2], [u - width / 2, v + height / 2]]
    .map(([a, b]) => point(a, b).map((value) => value.toFixed(2)).join(",")).join(" ");
  return `<polygon class="security-window-plate" points="${points}" />`;
}

function formatProductDimensions(templateId, parameters = {}) {
  const height = formatNumber(parameters.height);
  if (["straight-steel-staircase", "l-turn-steel-staircase", "u-turn-steel-staircase"].includes(templateId)) {
    const base = `层高 ${formatNumber(parameters.floorHeight)} × 梯宽 ${formatNumber(parameters.stairWidth)} · ${formatNumber(parameters.totalRiserCount)}级`;
    if (templateId === "l-turn-steel-staircase") {
      const turn = String(parameters.turnDirection ?? "left") === "right" ? "右转" : "左转";
      return `${base} · ${turn} · 平台 ${formatNumber(parameters.landingLength)} mm`;
    }
    if (templateId === "u-turn-steel-staircase") {
      return `${base} · 梯井 ${formatNumber(parameters.wellGap)} · 平台 ${formatNumber(parameters.landingLength)} mm`;
    }
    return `${base} · 踏步 ${formatNumber(parameters.treadDepth)} mm`;
  }
  if (templateId === "straight-stair-railing") {
    return `水平 ${formatNumber(parameters.flightRun)} × 提升 ${formatNumber(parameters.flightRise)} × 护栏高 ${formatNumber(parameters.railingHeight)} mm`;
  }
  if (templateId === "modular-guardrail") {
    const layout = String(parameters.layout ?? "straight");
    const lengths = [parameters.sideLength1];
    if (layout !== "straight") lengths.push(parameters.sideLength2);
    if (layout === "u") lengths.push(parameters.sideLength3);
    const layoutName = { straight: "直式", left_l: "左转 L 型", right_l: "右转 L 型", u: "U 型" }[layout] ?? "组合式";
    return `${layoutName} · 各段 ${lengths.map(formatNumber).join(" / ")} · 高 ${formatNumber(parameters.guardHeight)} mm`;
  }
  if (templateId === "two-face-security-window") {
    const direction = String(parameters.sidePosition ?? "right") === "left" ? "左前" : "右前";
    return `${direction} · 正面 ${formatNumber(parameters.frontWidth)} × 侧面 ${formatNumber(parameters.sideWidth)} × 高 ${height} mm`;
  }
  if (templateId === "three-face-security-window") {
    return `左 ${formatNumber(parameters.leftWidth)} × 正 ${formatNumber(parameters.frontWidth)} × 右 ${formatNumber(parameters.rightWidth)} × 高 ${height} mm`;
  }
  if (templateId === "five-face-security-window") {
    return `正面 ${formatNumber(parameters.frontWidth)} × 出墙 ${formatNumber(parameters.depth)} × 高 ${height} mm`;
  }
  return `${formatNumber(parameters.width)} × ${height} mm`;
}

function getProfileOverrides(values) {
  const overrides = values?.tubeDesignerProfileOverrides;
  return overrides && typeof overrides === "object" && !Array.isArray(overrides)
    ? overrides : {};
}

function localizedProfileText(value, fallback = "参数") {
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

function renderParametricProfileParameters(profile, prefix, mode, disabled) {
  if (profile?.kind !== "parametric-package") return "";
  const values = profile.parameters ?? {};
  const definitions = Array.isArray(profile.parameterDefinitions) ? profile.parameterDefinitions : [];
  return `<div class="tube-designer-parametric-profile-parameters">
    <strong>管型参数</strong>
    <div class="tube-designer-field-grid">${definitions.map((definition) => {
      const key = String(definition?.key ?? "");
      const label = localizedProfileText(definition?.displayName, key);
      const value = values[key] ?? definition?.defaultValue ?? "";
      const common = `data-cam-change-action="tube-designer-profile-parameter-change" data-tube-designer-profile-prefix="${escapeAttribute(prefix)}" data-tube-designer-profile-mode="${mode}" data-tube-designer-profile-parameter="${escapeAttribute(key)}"`;
      if (definition?.valueType === "boolean") {
        return `<label class="tube-designer-field tube-designer-boolean-field"><span>${escapeText(label)}</span><input type="checkbox" ${common} ${value ? "checked" : ""} ${disabled ? "disabled" : ""} /></label>`;
      }
      const options = Array.isArray(definition?.options) ? definition.options : [];
      if (options.length) {
        return `<label class="tube-designer-field"><span>${escapeText(label)}</span><select ${common} ${disabled ? "disabled" : ""}>${options.map((option) => {
          const optionValue = typeof option === "object" ? option?.value : option;
          const optionLabel = typeof option === "object" ? localizedProfileText(option?.displayName ?? option?.label, optionValue) : option;
          return `<option value="${escapeAttribute(optionValue)}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(optionLabel)}</option>`;
        }).join("")}</select></label>`;
      }
      const type = definition?.valueType === "string" ? "text" : "number";
      const attributes = [`type="${type}"`, `value="${escapeAttribute(value)}"`, common];
      if (definition?.min != null || definition?.minimum != null) attributes.push(`min="${escapeAttribute(definition.min ?? definition.minimum)}"`);
      if (definition?.max != null || definition?.maximum != null) attributes.push(`max="${escapeAttribute(definition.max ?? definition.maximum)}"`);
      if (type === "number") attributes.push(`step="${escapeAttribute(definition?.step ?? (definition?.valueType === "integer" ? 1 : "any"))}"`);
      if (disabled) attributes.push("disabled");
      return `<label class="tube-designer-field"><span>${escapeText(label)}</span><input ${attributes.join(" ")} /></label>`;
    }).join("")}</div>
  </div>`;
}

function renderProfileField(field, value, disabled, context) {
  const key = String(field.key ?? field.name ?? "");
  const prefix = key.slice(0, -"ProfileType".length);
  const label = escapeText(field.displayName ?? field.label);
  const override = getProfileOverrides(context?.values)[prefix];
  const profiles = (Array.isArray(context?.view?.tubeDesignerUserData?.profiles)
    ? context.view.tubeDesignerUserData.profiles : [])
    .slice().sort((left, right) => String(left?.name ?? "").localeCompare(String(right?.name ?? ""), "zh-CN"));
  const saved = profiles.find((item) => String(item?.id ?? "") === String(override?.savedProfileId ?? ""));
  const selected = override ? (saved ? `saved:${saved.id}` : "current") : `builtin:${value}`;
  const options = Array.isArray(field.options) ? field.options : [];
  const mode = context?.mode === "add" ? "add" : "right";
  return `<div class="tube-designer-field tube-designer-profile-field wide">
    <span>${label}</span>
    <select data-cam-change-action="tube-designer-profile-selection-change" data-tube-designer-profile-prefix="${escapeAttribute(prefix)}" data-tube-designer-profile-mode="${mode}" ${disabled ? "disabled" : ""}>
      <optgroup label="模板管型">${options.map((option) => {
        const optionValue = typeof option === "object" ? option?.value : option;
        const optionLabel = typeof option === "object" ? option?.label : option;
        const sourceValue = `builtin:${optionValue}`;
        return `<option value="${escapeAttribute(sourceValue)}" ${sourceValue === selected ? "selected" : ""}>${escapeText(optionLabel)}</option>`;
      }).join("")}</optgroup>
      ${profiles.length ? `<optgroup label="我的管型">${profiles.map((profile) => {
        const sourceValue = `saved:${profile.id}`;
        return `<option value="${escapeAttribute(sourceValue)}" ${sourceValue === selected ? "selected" : ""}>${escapeText(profile.name ?? profile.sourceFileName ?? "DXF 管型")}</option>`;
      }).join("")}</optgroup>` : ""}
      ${override && !saved ? `<option value="current" selected>${escapeText(override.name ?? override.sourceFileName ?? "当前导入 DXF")}</option>` : ""}
    </select>
    <div class="tube-designer-profile-actions">
      <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-import-profile-dxf" data-tube-designer-profile-prefix="${escapeAttribute(prefix)}" data-tube-designer-profile-mode="${mode}" ${disabled ? "disabled" : ""}>导入 DXF</button>
      ${override && !saved ? `<button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-open-profile-dialog" data-tube-designer-profile-prefix="${escapeAttribute(prefix)}" data-tube-designer-profile-mode="${mode}" data-tube-designer-profile-id="" ${disabled ? "disabled" : ""}>保存为我的管型</button>` : ""}
      <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-open-profile-library" data-tube-designer-profile-mode="${mode}" ${disabled ? "disabled" : ""}>我的管型管理${profiles.length ? ` (${profiles.length})` : ""}</button>
      ${override ? `
      <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-clear-imported-profile" data-tube-designer-profile-prefix="${escapeAttribute(prefix)}" data-tube-designer-profile-mode="${mode}" ${disabled ? "disabled" : ""}>恢复模板管型</button>` : ""}
    </div>
    ${renderParametricProfileParameters(override, prefix, mode, disabled)}
    ${override ? `<small class="tube-designer-profile-readonly-note"><strong>${escapeText(override.name ?? "导入管型")}</strong> · ${escapeText(override.specification ?? "")} · ${override.kind === "parametric-package" ? "参数可编辑，产品保存当前截面快照" : "冻结截面，不支持尺寸参数修改"}</small>` : ""}
  </div>`;
}

function renderField(field, value, disabled = false, context = null) {
  if (field.presentation?.editor === "component-model") return renderComponentModelField(field, value, disabled, context);
  const name = escapeAttribute(field.key ?? field.name);
  const label = escapeText(field.displayName ?? field.label);
  if (field.type === "select" && String(field.key ?? field.name ?? "").endsWith("ProfileType")) {
    return renderProfileField(field, value, disabled, context);
  }
  if (field.type === "readonly") {
    return `<label class="tube-designer-field">${label}<input type="text" data-tube-designer-parameter="${name}" value="${escapeAttribute(value)}" readonly /></label>`;
  }
  if (field.type === "boolean" || field.valueType === "boolean") {
    return `<label class="tube-designer-field tube-designer-boolean-field"><span>${label}</span><input type="checkbox" data-tube-designer-parameter="${name}" data-cam-change-action="tube-designer-parameter-change" ${value === true || value === "true" || value === "是" ? "checked" : ""} ${disabled ? "disabled" : ""} /></label>`;
  }
  if (field.type === "select") {
    const options = Array.isArray(field.options) ? field.options : [];
    return `<label class="tube-designer-field">${label}<select data-tube-designer-parameter="${name}" data-cam-change-action="tube-designer-parameter-change" ${disabled || field.readOnly ? "disabled" : ""}>${options.map((option) => {
      const optionValue = typeof option === "object" ? option?.value : option;
      const optionLabel = typeof option === "object" ? option?.label : option;
      return `<option value="${escapeAttribute(optionValue)}" data-tube-designer-value-type="${typeof optionValue}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(optionLabel)}</option>`;
    }).join("")}</select></label>`;
  }
  const type = field.type === "text" ? "text" : "number";
  const attributes = [`type="${type}"`, `data-tube-designer-parameter="${name}"`, `value="${escapeAttribute(value)}"`];
  if (type === "number") attributes.push(`step="${escapeAttribute(field.step ?? (field.type === "integer" ? 1 : "any"))}"`);
  if (field.min != null) attributes.push(`min="${escapeAttribute(field.min)}"`);
  if (field.max != null) attributes.push(`max="${escapeAttribute(field.max)}"`);
  if (disabled) attributes.push("disabled");
  return `<label class="tube-designer-field ${field.type === "text" ? "wide" : ""}">${label}<input ${attributes.join(" ")} /></label>`;
}

function visibleFields(fields, values) {
  return (Array.isArray(fields) ? fields : []).filter((field) => matchesVisibility(field?.visibleWhen, values));
}

function matchesVisibility(condition, values) {
  if (!condition) return true;
  const all = condition.conditions && condition.op === "all" ? condition.conditions : condition.all;
  const any = condition.conditions && condition.op === "any" ? condition.conditions : condition.any;
  if (Array.isArray(all)) {
    return all.every((item) => matchesVisibility(item, values));
  }
  if (Array.isArray(any)) {
    return any.some((item) => matchesVisibility(item, values));
  }
  const negated = condition.condition && condition.op === "not" ? condition.condition : condition.not;
  if (negated) return !matchesVisibility(negated, values);
  const name = condition.parameter ?? condition.name;
  if (!name) return true;
  const equal = Object.is(values?.[name], condition.value)
    || String(values?.[name] ?? "") === String(condition.value ?? "");
  return condition.op === "ne" ? !equal : equal;
}

function buildParameterGroupTree(template, fields) {
  const nodes = new Map();
  const descriptors = Array.isArray(template?.groups) ? template.groups : [];
  const groupOrders = new Map(descriptors.map((descriptor, index) => [
    String(descriptor?.key ?? ""),
    Number(descriptor?.order ?? index),
  ]));
  let encounterIndex = 0;
  const ensureNode = (key, title, parentKey, order) => {
    if (!nodes.has(key)) {
      nodes.set(key, {
        key,
        title,
        parentKey,
        order,
        index: encounterIndex++,
        fields: [],
        children: [],
      });
    } else {
      nodes.get(key).order = Math.min(nodes.get(key).order, order);
    }
    return nodes.get(key);
  };

  (Array.isArray(fields) ? fields : []).forEach((field, fieldIndex) => {
    const displayPath = String(field?.displayName ?? field?.label ?? field?.key ?? "参数")
      .split("/").map((part) => part.trim()).filter(Boolean);
    const groupKey = String(field?.groupKey ?? "").trim();
    const groupOrder = groupOrders.get(groupKey) ?? fieldIndex;
    if (displayPath.length > 1) {
      let parentKey = "";
      displayPath.slice(0, -1).forEach((title, depth, groupPath) => {
        const key = `path:${groupPath.slice(0, depth + 1).join("/")}`;
        ensureNode(key, title, parentKey, groupOrder);
        parentKey = key;
      });
      nodes.get(parentKey).fields.push({
        ...field,
        displayName: displayPath.at(-1),
        label: displayPath.at(-1),
      });
      return;
    }
    const title = String(field?.group ?? "参数");
    const key = `group:${groupKey || title}`;
    ensureNode(key, title, "", groupOrder).fields.push(field);
  });

  for (const node of nodes.values()) {
    node.fields.sort((left, right) => Number(left?.order ?? 0) - Number(right?.order ?? 0));
  }

  const roots = [];
  for (const node of nodes.values()) {
    const parent = node.parentKey ? nodes.get(node.parentKey) : null;
    if (parent && parent !== node) parent.children.push(node);
    else roots.push(node);
  }
  const sortNodes = (items) => items.sort((left, right) => left.order - right.order || left.index - right.index)
    .map((node) => ({ ...node, children: sortNodes(node.children) }));
  const pruneNode = (node) => {
    const children = node.children.map(pruneNode).filter(Boolean);
    return node.fields.length || children.length ? { ...node, children } : null;
  };
  return sortNodes(roots).map(pruneNode).filter(Boolean);
}

function renderAddParameterGroup(group, values, disabled, view, mode, template, depth = 0, siblingIndex = 0) {
  const fields = group.fields.map((field) => renderField(
    field,
    values[field.key ?? field.name],
    disabled,
    { values, view, mode, template },
  )).join("");
  const children = group.children.map((child, index) => renderAddParameterGroup(
    child,
    values,
    disabled,
    view,
    mode,
    template,
    depth + 1,
    index,
  )).join("");
  if (depth) {
    return `<details class="tube-designer-config-subsection" data-tube-designer-group-depth="${depth}" ${siblingIndex === 0 ? "open" : ""}>
      <summary><span>${escapeText(group.title)}</span><small>${countParameterGroupFields(group)} 项</small></summary>
      <div class="tube-designer-config-subsection-content">
        ${fields ? `<div class="tube-designer-field-grid">${fields}</div>` : ""}
        ${children ? `<div class="tube-designer-subsection-list">${children}</div>` : ""}
      </div>
    </details>`;
  }
  return `<section class="tube-designer-section" data-tube-designer-group-depth="${depth}">
    <strong>${escapeText(group.title)}</strong>
    ${fields ? `<div class="tube-designer-field-grid">${fields}</div>` : ""}
    ${children ? `<div class="tube-designer-subsection-list">${children}</div>` : ""}
  </section>`;
}

function compactParameterGroup(group, values, disabled, expandedGroups, view, mode, template, depth = 0) {
  const fields = group.fields.map((field) => renderField(
    field,
    values[field.key ?? field.name],
    disabled,
    { values, view, mode, template },
  )).join("");
  const children = group.children.map((child) => compactParameterGroup(
    child,
    values,
    disabled,
    expandedGroups,
    view,
    mode,
    template,
    depth + 1,
  )).join("");
  const itemCount = countParameterGroupFields(group);
  const className = depth ? "tube-designer-parameter-subsection" : "tube-designer-parameter-section";
  return `<details class="${className}" data-tube-designer-parameter-group="${escapeAttribute(group.key)}" data-tube-designer-group-depth="${depth}" ${expandedGroups.has(group.key) ? "open" : ""}>
    <summary><span>${escapeText(group.title)}</span><small>${itemCount} 项</small></summary>
    <div class="tube-designer-parameter-group-content">
      ${fields ? `<div class="tube-designer-field-grid">${fields}</div>` : ""}
      ${children ? `<div class="tube-designer-parameter-subsection-list">${children}</div>` : ""}
    </div>
  </details>`;
}

function countParameterGroupFields(group) {
  return group.fields.length + group.children.reduce(
    (total, child) => total + countParameterGroupFields(child),
    0,
  );
}

function defaultExpandedParameterGroups(groupTree) {
  const expanded = groupTree.slice(0, 2).map((group) => group.key);
  const addFirstChild = (group) => {
    const firstChild = group.children[0];
    if (!firstChild) return;
    expanded.push(firstChild.key);
    addFirstChild(firstChild);
  };
  groupTree.forEach(addFirstChild);
  return expanded;
}

function scheduleDesignerParameterPanelRestoration(context, view, productId, hasSavedPanelState) {
  if (!hasSavedPanelState) return;
  const restorationToken = Number(view.tubeDesignerParameterPanelRestorationToken ?? 0) + 1;
  view.tubeDesignerParameterPanelRestorationToken = restorationToken;
  queueMicrotask(() => {
    if (view.tubeDesignerParameterPanelRestorationToken !== restorationToken) return;
    if (String(view.scene?.tubeDesigner?.product?.entityId ?? "") !== String(productId ?? "")) return;
    const panel = context.mount?.querySelector?.("[data-tube-designer-parameter-form]");
    const sections = panel?.querySelector?.(".tube-designer-parameter-sections");
    if (!sections) return;
    const shouldRestoreFocus = !view.pending && Boolean(view.tubeDesignerRestoreParameterFocus);
    const restoredAnchor = restoreScrollAnchor(
      sections,
      view.tubeDesignerParameterPanelScrollAnchor,
      { restoreFocus: shouldRestoreFocus },
    );
    if (!view.tubeDesignerParameterPanelScrollAnchor) {
      sections.scrollTop = Number(view.tubeDesignerParameterPanelScrollTop ?? 0);
    }
    if (!shouldRestoreFocus) return;
    const parameterKey = String(view.tubeDesignerLastEditedParameterKey ?? "");
    const field = Array.from(panel.querySelectorAll("[data-tube-designer-parameter]"))
      .find((item) => String(item.dataset.tubeDesignerParameter ?? "") === parameterKey);
    if (!restoredAnchor) field?.focus?.({ preventScroll: true });
    view.tubeDesignerRestoreParameterFocus = false;
  });
}

function metric(value, label) {
  return `<div class="tube-designer-metric"><strong>${escapeText(value)}</strong><span>${escapeText(label)}</span></div>`;
}

function partDisplayName(part) {
  const name = String(part?.name ?? "").trim();
  if (name) return name;
  const role = String(part?.role ?? "").trim();
  if (role && role !== "main") return roleLabel(role);
  return String(part?.partNumber ?? `零件 ${part?.index ?? ""}`).trim();
}

function formatPartSpecification(part) {
  const kind = manufacturingPartKind(part);
  if (kind === "plate" || kind === "glass") {
    const { width, height, thickness } = plateDimensions(part);
    return `${kind === "glass" ? "玻璃" : "板件"} ${formatNumber(width)} × ${formatNumber(height)} × ${formatNumber(thickness)} mm`;
  }
  if (kind === "accessory") {
    const properties = part?.properties ?? {};
    const name = String(properties["manufacturing.modelName"] ?? part.modelName ?? "三维配件");
    const bounds = properties["manufacturing.modelBounds"] ?? part.modelBounds ?? {};
    const dimensions = [bounds.width, bounds.depth, bounds.height].map((value) => Number(value));
    const size = dimensions.every((value) => Number.isFinite(value) && value > 0)
      ? ` ${dimensions.map(formatNumber).join(" × ")} mm` : "";
    const sourcing = properties["manufacturing.sourcing"] ?? part.sourcing;
    const sourceLabel = sourcing === "made" ? "自制" : sourcing === "purchased" ? "外购" : "供料方式未指定";
    return `${name}${size} · ${sourceLabel}`;
  }
  const profile = part?.profile;
  if (!profile || typeof profile !== "object") return "—";
  const displayName = String(profile.displayName ?? "").trim();
  const specification = String(profile.specification ?? "").trim();
  return [displayName, specification].filter(Boolean).join(" ") || "—";
}

function formatPartSpecificationWithLength(part) {
  const specification = formatPartSpecification(part);
  if (["plate", "glass", "accessory"].includes(manufacturingPartKind(part))) return specification;
  const length = Number(part?.length);
  return Number.isFinite(length) && length > 0
    ? `${specification}*len*${formatNumber(length)}`
    : specification;
}

function formatCategorySpecification(category) {
  const specification = category.specifications.length === 1
    ? category.specifications[0]
    : `${category.specifications.length} 种规格`;
  const lengths = category.lengths
    .filter((length) => Number.isFinite(length) && length > 0)
    .map((length) => formatNumber(length));
  return lengths.length
    ? `${specification}*len*${lengths.join("/")}`
    : specification;
}

function roleLabel(role) {
  return ({
    "outer-frame": "连续折弯外框", "left-frame": "左外框", "right-frame": "右外框",
    "top-frame": "上外框", "bottom-frame": "下外框", "middle-vertical": "中间竖管",
    "middle-horizontal": "中间横管", horizontal: "横管",
    "handle-left-vertical": "把手左竖管", "handle-right-vertical": "把手右竖管",
    "handle-top-horizontal": "把手上横管", "handle-bottom-horizontal": "把手下横管",
    "handle-area-horizontal": "把手区域横管", "top-area-horizontal": "上部横管", "bottom-area-horizontal": "下部横管",
    "main-horizontal": "主横杆", "main-horizontal-left": "窗框左侧横杆", "main-horizontal-right": "窗框右侧横杆",
    "main-vertical": "主竖杆", "main-vertical-bottom": "窗框下方竖杆", "main-vertical-top": "窗框上方竖杆",
    "access-door-fixed-left-frame": "固定窗框左杆", "access-door-fixed-right-frame": "固定窗框右杆",
    "access-door-fixed-top-frame": "固定窗框上杆", "access-door-fixed-bottom-frame": "固定窗框下杆",
    "access-door-fixed-frame": "固定窗框组件",
    "access-door-leaf-left-frame": "活动门左框", "access-door-leaf-right-frame": "活动门右框",
    "access-door-leaf-top-frame": "活动门上框", "access-door-leaf-bottom-frame": "活动门下框",
    "access-door-leaf": "活动窗扇组件",
    "access-door-leaf-horizontal": "窗内横杆", "access-door-leaf-vertical": "窗内竖杆",
  })[role] ?? role ?? "零件";
}

function formatNumber(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) return "-";
  return new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3 }).format(number);
}

function escapeText(value) {
  return String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll('"', "&quot;");
}

function escapeAttribute(value) {
  return escapeText(value).replaceAll("'", "&#39;");
}
