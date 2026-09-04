import { scheduleDesignerPartThumbnailHydration } from "./partThumbnail.mjs";
import { scheduleDesignerPartInspectionHydration } from "./partInspection.mjs";

const FALLBACK_PARAMETERS = Object.freeze({
  productCode: "TD-001", height: 1800, width: 1200,
  horizontalCount: 4, verticalLayoutMode: "maximum_clear_gap", maximumVerticalClearGap: 110,
  middleVerticalCount: 9,
  firstHorizontalTopOffset: 200, lastHorizontalBottomOffset: 200,
  horizontalBranchReserve: 10, verticalBranchReserve: 10, assemblyClearance: 0.1,
  frameProfileType: "rect", frameWidth: 38, frameDepth: 25, frameWallThickness: 1.2,
  verticalProfileType: "round", verticalWidth: 19, verticalDepth: 19, verticalWallThickness: 1,
  horizontalProfileType: "rect", horizontalWidth: 25, horizontalDepth: 25, horizontalWallThickness: 1,
});

export function getDefaultParameters(templates = [], templateId = "") {
  const template = getTemplateById(templates, templateId) ?? templates.find((item) => item?.available);
  const fields = Array.isArray(template?.parameters) ? template.parameters : [];
  if (!fields.length) return { ...FALLBACK_PARAMETERS };
  return Object.fromEntries(fields.map((field) => [field.key ?? field.name, field.defaultValue]));
}

export function getTemplateById(templates = [], templateId = "") {
  return templates.find((item) => item?.id === templateId) ?? null;
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
  const dialogs = renderDialogs(designer, view);
  if (view.tubeDesignerBreakdownOpen) scheduleDesignerPartThumbnailHydration(context);
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
  const groups = groupFields(visibleFields(template?.parameters ?? [], values));
  const groupEntries = [...groups.entries()];
  const defaultExpandedGroups = groupEntries.slice(0, 2).map(([title, fields]) => parameterGroupKey(title, fields));
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
            <span>${escapeText(template?.name ?? product.templateId)} · 当前场景实例</span>
          </div>
          <button class="tube-designer-primary tube-designer-regenerate-button" data-cam-action="tube-designer-confirm-update" title="按当前参数重新生成；原拆单结果将失效" ${view.pending ? "disabled" : ""}>${view.pending ? "生成中…" : "重新生成"}</button>
        </div>
        <div class="tube-designer-summary">
          ${metric(designer.members?.length ?? 0, "预览装配单元")}
          ${metric(designer.joints?.length ?? 0, "连接关系")}
          ${metric(parts.length, "已拆零件")}
        </div>
      </header>
      <div class="tube-designer-parameter-sections">
        ${groupEntries.map(([title, fields]) => compactParameterSection(
          title,
          fields,
          values,
          view.pending,
          expandedGroups.has(parameterGroupKey(title, fields)),
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
  const operation = view.tubeDesignerOperation;
  if (!operation || operation.kind === "export") return "";
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

function renderDialogs(designer, view) {
  return [
    view.tubeDesignerAddDialogOpen ? renderAddDialog(designer, view) : "",
    view.tubeDesignerDisassemblySelectorOpen ? renderDisassemblySelector(designer, view) : "",
    view.tubeDesignerBreakdownOpen ? renderBreakdownDialog(designer, view) : "",
    view.tubeDesignerPartInspectionOpen ? renderPartInspectionDialog(designer, view) : "",
  ].join("");
}

function renderInstanceCard(instance, templates, selected, pending) {
  const template = getTemplateById(templates, instance.templateId);
  const parameters = instance.parameters ?? {};
  return `
    <button class="tube-designer-instance-card ${selected ? "selected" : ""}" data-cam-action="tube-designer-select-instance" data-tube-designer-instance-id="${escapeAttribute(instance.entityId)}" ${pending || selected ? "disabled" : ""}>
      <span class="tube-designer-instance-thumbnail">${renderSchematic(instance.templateId, parameters)}</span>
      <span class="tube-designer-instance-copy">
        <strong>${escapeText(instance.name)}</strong>
        <span>${escapeText(template?.name ?? instance.templateId)}</span>
        <small>${escapeText(formatProductDimensions(instance.templateId, parameters))} · ${instance.hasDisassembly ? `${instance.partCount} 个零件` : "未拆单"}</small>
      </span>
      <i aria-hidden="true"></i>
    </button>
  `;
}

function renderAddDialog(designer, view) {
  const templates = Array.isArray(designer.templates) ? designer.templates : [];
  const template = getTemplateById(templates, view.tubeDesignerAddTemplateId) ?? templates.find((item) => item.available);
  const values = { ...getDefaultParameters(templates, template?.id), ...(view.tubeDesignerAddDraft ?? {}) };
  const groups = groupFields(visibleFields(template?.parameters ?? [], values));
  const pending = Boolean(view.pending);
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-config-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-add-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-add-title">添加产品实例</strong><span>选择模板并一次设置好初始参数；确定前不会修改项目数据。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-cancel-add" aria-label="取消添加" ${pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-config-body">
          <aside class="tube-designer-template-pane">
            <div class="tube-designer-pane-title"><strong>产品模板</strong><span>${templates.filter((item) => item.available).length} 个可用</span></div>
            <div class="tube-designer-template-list">
              ${templates.map((item) => renderTemplateCard(item, item.id === template?.id, pending)).join("")}
            </div>
          </aside>
          <main class="tube-designer-config-parameters" data-tube-designer-add-form>
            <div class="tube-designer-config-summary">
              <span class="tube-designer-config-preview">${renderSchematic(template?.id, values)}</span>
              <div><strong>${escapeText(view.tubeDesignerAddInstanceName)}</strong><span>${escapeText(template?.name)} · ${formatNumber(values.width)} × ${formatNumber(values.height)} mm</span></div>
            </div>
            ${template?.available
              ? [...groups.entries()].map(([title, fields]) => section(
                title,
                fields.map((field) => renderField(field, values[field.key ?? field.name], pending)),
              )).join("")
              : `<div class="tube-designer-empty">${escapeText(template?.status ?? "该模板尚不可用。")}</div>`}
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

function renderTemplateCard(template, selected, pending) {
  const disabled = !template?.available;
  return `
    <button class="tube-designer-template-card ${selected ? "selected" : ""} ${disabled ? "disabled" : ""}" data-cam-action="tube-designer-select-template" data-tube-designer-template-id="${escapeAttribute(template?.id)}" ${pending || disabled ? "disabled" : ""}>
      <span class="tube-designer-template-schematic">${renderSchematic(template?.id, {})}</span>
      <span><strong>${escapeText(template?.name)}</strong><small>${disabled ? escapeText(template?.status ?? "尚未迁移") : `版本 ${escapeText(template?.version)}`}</small></span>
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
          <div><strong id="tube-designer-disassemble-title">选择要拆单的产品实例</strong><span>拆单不会改变当前场景中显示的实例。</span></div>
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
                <td>${escapeText(template?.name ?? instance.templateId)}</td>
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
  const visibleIds = new Set(view.tubeDesignerBreakdownProductIds ?? []);
  const groups = (designer.manufacturingGroups ?? []).filter((group) => !visibleIds.size || visibleIds.has(group.productEntityId));
  const allParts = groups.flatMap((group) => group.parts ?? []);
  const selected = new Set(view.tubeDesignerSelectedPartIds ?? allParts.map((part) => part.entityId));
  const allSelected = allParts.length > 0 && allParts.every((part) => selected.has(part.entityId));
  const selectedCount = allParts.filter((part) => selected.has(part.entityId)).length;
  const exportOperation = view.tubeDesignerExportOperation ?? null;
  const exportBusy = Boolean(exportOperation);
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-breakdown-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-breakdown-title" aria-busy="${exportBusy ? "true" : "false"}">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-breakdown-title">产品拆单结果</strong><span data-tube-designer-breakdown-summary>${groups.length} 个产品 · ${allParts.length} 个零件 · 已选择 ${selectedCount} 个</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-breakdown" aria-label="关闭拆单结果" ${exportBusy ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-sheet-wrap">
          <table class="tube-designer-sheet">
            <thead><tr><th class="tube-designer-tree-column">名称</th><th class="tube-designer-check-cell"><input type="checkbox" data-cam-action="tube-designer-toggle-all-parts" data-tube-designer-all-parts ${allSelected ? "checked" : ""} aria-label="选择全部零件" /></th><th>序号</th><th>示意图</th><th>规格 / 外尺寸</th><th>长度</th><th>数量</th><th>文件名</th></tr></thead>
            <tbody>${groups.map((group) => renderProductPartGroup(group, selected, view)).join("")}</tbody>
          </table>
        </div>
        <footer class="tube-designer-breakdown-footer">
          <div class="tube-designer-export-destination"><strong>STEP + Excel 分组导出</strong><span>${view.tubeDesignerExportDirectory ? `上次总目录：${escapeText(view.tubeDesignerExportDirectory)}` : "总目录生成零件清单.xlsx，每个产品创建自己的 STEP 子目录"}</span></div>
          <span data-tube-designer-export-selection-summary>将导出 ${selectedCount} 个零件</span>
          <button class="tube-designer-primary" data-cam-action="tube-designer-export-selected" data-tube-designer-export-selected ${view.pending || exportBusy || !selectedCount ? "disabled" : ""}>${exportBusy ? "正在导出…" : "选择目录并导出"}</button>
        </footer>
        ${exportBusy ? renderExportWait(exportOperation) : ""}
      </section>
    </div>
  `;
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
            <span>${escapeText(part.fileName)} · 尺寸以当前版本的最终三维实体为准</span>
          </div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-part-inspection" aria-label="关闭零件复尺">×</button>
        </header>
        <div class="tube-designer-part-inspection-body">
          <div class="tube-designer-part-inspection-stage">
            <div class="tube-designer-part-inspection-toolbar">
              <span data-tube-designer-inspection-status>正在载入零件三维资源…</span>
              <div>
                <button data-cam-action="tube-designer-inspection-iso-view">等轴测</button>
                <button data-cam-action="tube-designer-inspection-fit-view">适合窗口</button>
              </div>
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
            <section class="tube-designer-manual-measurement-section">
              <div class="tube-designer-measurement-heading">
                <div><strong>手工两点测量</strong><span>默认关闭</span></div>
                <button data-cam-action="tube-designer-toggle-part-measurement" aria-pressed="false">开始测量</button>
              </div>
              <div class="tube-designer-measurement-result" data-tube-designer-measurement-result data-measurement-point-count="0">
                <strong>测量已关闭</strong><span>查看模型不会触发拾取检测</span>
              </div>
              <button class="tube-designer-measurement-clear" data-cam-action="tube-designer-clear-part-measurement" disabled>清除测量</button>
            </section>
            <div class="tube-designer-measurement-help">
              <strong>视图操作</strong>
              <span data-tube-designer-measurement-pick-help>左键：测量关闭，不执行拾取</span>
              <span>右键拖动：旋转</span>
              <span>中键拖动：平移</span>
              <span>滚轮：缩放</span>
              <small>自动尺寸直接分析当前版本的最终三维实体；需要补测任意位置时再打开手工测量。</small>
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
      <td class="tube-designer-tree-cell tube-designer-tree-level-0">
        ${treeToggle("tube-designer-toggle-product-tree", productCollapsed, { tubeDesignerProductId: productId }, group.name)}
      </td>
      <td class="tube-designer-check-cell">${selectionCheckbox("tube-designer-toggle-part-group", productPartIds, productSelection, `选择产品 ${group.name} 的全部零件`)}</td>
      <td>—</td>
      <td><span class="tube-designer-tree-product-thumbnail">${renderSchematic(group.templateId, parameters)}</span></td>
      <td>${escapeText(formatProductDimensions(group.templateId, parameters))}</td>
      <td class="tube-designer-number">—</td>
      <td class="tube-designer-number">${parts.reduce((sum, part) => sum + partQuantity(part), 0)}</td>
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
      <td class="tube-designer-tree-cell tube-designer-tree-level-1">
        ${treeToggle("tube-designer-toggle-category-tree", !expanded, { tubeDesignerCategoryId: category.id }, category.name)}
      </td>
      <td class="tube-designer-check-cell">${selectionCheckbox("tube-designer-toggle-part-group", partIds, state, `选择种类 ${category.name} 的全部零件`)}</td>
      <td>${category.parts.length === 1 ? escapeText(representative.index) : "—"}</td>
      <td>${renderPartThumbnail(representative, `${category.name} 种类示意图`)}</td>
      <td class="tube-designer-part-specification">${escapeText(category.specifications.length === 1 ? category.specifications[0] : `${category.specifications.length} 种规格`)}</td>
      <td class="tube-designer-number">${category.lengths.length === 1 ? `${formatNumber(category.lengths[0])} mm` : "多种"}</td>
      <td class="tube-designer-number"><strong>${category.quantity}</strong></td>
      <td>${category.parts.length === 1 ? escapeText(representative.fileName) : `<span class="tube-designer-merged-value">${category.parts.length} 个文件</span>`}</td>
    </tr>`;
  if (!expanded) return categoryRow;
  return categoryRow + category.parts.map((part, index) => renderPartRow(
    part,
    selected.has(part.entityId),
    index === category.parts.length - 1,
  )).join("");
}

function renderPartRow(part, checked, isLastInCategory = false) {
  const spec = formatPartSpecification(part);
  return `
    <tr class="tube-designer-sheet-row tube-designer-part-row${isLastInCategory ? " tube-designer-category-last-row" : ""}" data-tube-designer-part-row="${escapeAttribute(part.entityId)}">
      <td class="tube-designer-tree-cell tube-designer-tree-level-2"><button class="tube-designer-part-inspection-link" data-cam-action="tube-designer-open-part-inspection" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" title="打开零件三维复尺">${escapeText(partDisplayName(part))}</button></td>
      <td class="tube-designer-check-cell"><input type="checkbox" data-cam-action="tube-designer-toggle-part" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" data-tube-designer-selection-ids="${escapeAttribute(part.entityId)}" ${checked ? "checked" : ""} aria-label="选择零件 ${escapeAttribute(part.partNumber)}" /></td>
      <td>${escapeText(part.index)}</td>
      <td>${renderPartThumbnail(part, `${part.partNumber} 三维示意图`)}</td>
      <td class="tube-designer-part-specification">${escapeText(spec)}</td>
      <td class="tube-designer-number">${formatNumber(part.length)} mm</td>
      <td class="tube-designer-number">${escapeText(part.quantity ?? 1)}</td>
      <td>${escapeText(part.fileName)}</td>
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
    category.lengths = [...new Set(category.parts.map((part) => Number(part?.length ?? 0)))];
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
  return `<button class="tube-designer-tree-toggle" data-cam-action="${action}" ${attributes} aria-expanded="${collapsed ? "false" : "true"}" aria-label="${collapsed ? "展开" : "折叠"}${escapeAttribute(label)}" title="点击${collapsed ? "展开" : "折叠"}">${escapeText(label)}</button>`;
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
  const effectiveTemplateId = templateId === "single-face-security-window"
    ? (parameters.frameLayout === "four_sides" ? "security-window-3" : "security-window-1")
    : templateId;
  if (["two-face-security-window", "three-face-security-window", "five-face-security-window"].includes(templateId)) {
    content = renderMultiFaceSchematic(templateId, parameters);
  } else if (templateId === "single-face-security-window" && (parameters.accessDoorEnabled === true || parameters.accessDoorEnabled === "是")) {
    const outer = parameters.frameLayout === "top_bottom"
      ? `<line x1="${x}" y1="8" x2="${x + w}" y2="8" /><line x1="${x}" y1="92" x2="${x + w}" y2="92" />`
      : parameters.frameLayout === "four_sides"
        ? `<rect x="${x}" y="8" width="${w}" height="84" rx="2" />`
        : `<line x1="${x}" y1="8" x2="${x}" y2="92" /><line x1="${x + w}" y1="8" x2="${x + w}" y2="92" />`;
    content = `${outer}${rail(27)}${rail(47)}${rail(68)}${rail(84)}<line x1="50" y1="10" x2="50" y2="90" /><rect x="39" y="44" width="22" height="28" rx="1" /><rect x="42" y="47" width="16" height="22" rx="1" /><circle class="notch" cx="40" cy="53" r="1.4" /><circle class="notch" cx="40" cy="64" r="1.4" />`;
  } else if (effectiveTemplateId === "security-window-2") {
    content = `<rect x="${x}" y="8" width="${w}" height="84" rx="2" />${rail(27)}${rail(43)}${rail(73)}${rail(84)}<rect x="${42}" y="45" width="16" height="25" rx="1" /><line x1="50" y1="45" x2="50" y2="70" />`;
  } else if (effectiveTemplateId === "security-window-3") {
    content = `<rect x="${x}" y="8" width="${w}" height="84" rx="2" />${rail(28)}${rail(48)}${rail(68)}<line x1="50" y1="8" x2="50" y2="92" /><path class="notch" d="M ${x + w - 8} 8 l 4 5 l 4 -5" />`;
  } else if (templateId === "empty") {
    content = `<rect class="placeholder" x="24" y="14" width="52" height="72" rx="4" /><path class="plus" d="M50 38v24M38 50h24" />`;
  } else {
    content = `<line x1="${x}" y1="8" x2="${x}" y2="92" /><line x1="${x + w}" y1="8" x2="${x + w}" y2="92" /><line x1="50" y1="10" x2="50" y2="90" />${rail(24)}${rail(41)}${rail(59)}${rail(76)}`;
  }
  return `<svg class="tube-designer-schematic ${escapeAttribute(className)}" viewBox="0 0 100 100" role="img" aria-label="产品示意图">${content}</svg>`;
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
  if (parameters.accessDoorEnabled === true || parameters.accessDoorEnabled === "是") {
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
  if (parameters.accessDoorEnabled === true || parameters.accessDoorEnabled === "是") {
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

function renderProjectedDoor(parameters, origin, uEnd, vEnd, uLength, vLength) {
  const clamp = (value, minimum, maximum) => Math.max(minimum, Math.min(maximum, value));
  const safeU = Math.max(1, Number(uLength));
  const safeV = Math.max(1, Number(vLength));
  const u0 = clamp(Number(parameters.doorUOffset ?? 80) / safeU, 0.03, 0.88);
  const v0 = clamp(Number(parameters.doorVOffset ?? 80) / safeV, 0.03, 0.88);
  const u1 = clamp((Number(parameters.doorUOffset ?? 80) + Number(parameters.doorWidth ?? 400)) / safeU, u0 + 0.06, 0.97);
  const v1 = clamp((Number(parameters.doorVOffset ?? 80) + Number(parameters.doorHeight ?? 400)) / safeV, v0 + 0.06, 0.97);
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
    <polygon class="multi-face-door-leaf" points="${polygonPoints(u0 + insetU, v0 + insetV, u1 - insetU, v1 - insetV)}" />`;
}

function formatProductDimensions(templateId, parameters = {}) {
  const height = formatNumber(parameters.height);
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

function renderField(field, value, disabled = false) {
  const name = escapeAttribute(field.key ?? field.name);
  const label = escapeText(field.displayName ?? field.label);
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
      return `<option value="${escapeAttribute(optionValue)}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(optionLabel)}</option>`;
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

function groupFields(fields) {
  const groups = new Map();
  for (const field of Array.isArray(fields) ? fields : []) {
    const group = String(field?.group ?? "参数");
    if (!groups.has(group)) groups.set(group, []);
    groups.get(group).push(field);
  }
  return groups;
}

function section(title, fields) {
  return `<section class="tube-designer-section"><strong>${escapeText(title)}</strong><div class="tube-designer-field-grid">${fields.join("")}</div></section>`;
}

function compactParameterSection(title, fields, values, disabled, expanded) {
  const groupKey = parameterGroupKey(title, fields);
  return `<details class="tube-designer-parameter-section" data-tube-designer-parameter-group="${escapeAttribute(groupKey)}" ${expanded ? "open" : ""}>
    <summary><span>${escapeText(title)}</span><small>${fields.length} 项</small></summary>
    <div class="tube-designer-field-grid">${fields.map((field) => renderField(
      field,
      values[field.key ?? field.name],
      disabled,
    )).join("")}</div>
  </details>`;
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
    sections.scrollTop = Number(view.tubeDesignerParameterPanelScrollTop ?? 0);
    if (view.pending || !view.tubeDesignerRestoreParameterFocus) return;
    const parameterKey = String(view.tubeDesignerLastEditedParameterKey ?? "");
    const field = Array.from(panel.querySelectorAll("[data-tube-designer-parameter]"))
      .find((item) => String(item.dataset.tubeDesignerParameter ?? "") === parameterKey);
    field?.focus?.({ preventScroll: true });
    view.tubeDesignerRestoreParameterFocus = false;
  });
}

function parameterGroupKey(title, fields) {
  return String(fields?.[0]?.groupKey ?? title ?? "parameters");
}

function metric(value, label) {
  return `<div class="tube-designer-metric"><strong>${escapeText(value)}</strong><span>${escapeText(label)}</span></div>`;
}

function profileLabel(type) {
  return ({ rect: "矩形管", round: "圆管" })[type] ?? type ?? "管材";
}

function partDisplayName(part) {
  const name = String(part?.name ?? "").trim();
  if (name) return name;
  const role = String(part?.role ?? "").trim();
  if (role && role !== "main") return roleLabel(role);
  return String(part?.partNumber ?? `零件 ${part?.index ?? ""}`).trim();
}

function formatPartSpecification(part) {
  const profileType = String(part?.profileType ?? "");
  const width = Number(part?.sectionWidth);
  const depth = Number(part?.sectionDepth);
  const wallThickness = Number(part?.wallThickness);
  const cornerRadius = Number(part?.cornerRadius ?? 0);
  if (!(width > 0) || !(wallThickness > 0)) return "—";
  if (profileType === "round") {
    return `${profileLabel(profileType)} Φ${formatNumber(width)} × ${formatNumber(wallThickness)}`;
  }
  if (!(depth > 0)) return "—";
  return `${profileLabel(profileType)} ${formatNumber(width)} × ${formatNumber(depth)} × R${formatNumber(cornerRadius)} × ${formatNumber(wallThickness)}`;
}

function roleLabel(role) {
  return ({
    "outer-frame": "连续折弯外框", "left-frame": "左外框", "right-frame": "右外框",
    "top-frame": "上外框", "bottom-frame": "下外框", "middle-vertical": "中间竖管",
    "middle-horizontal": "中间横管", horizontal: "横管",
    "handle-left-vertical": "把手左竖管", "handle-right-vertical": "把手右竖管",
    "handle-top-horizontal": "把手上横管", "handle-bottom-horizontal": "把手下横管",
    "handle-area-horizontal": "把手区域横管", "top-area-horizontal": "上部横管", "bottom-area-horizontal": "下部横管",
    "main-horizontal": "主横杆", "main-horizontal-left": "门框左侧横杆", "main-horizontal-right": "门框右侧横杆",
    "main-vertical": "主竖杆", "main-vertical-bottom": "门框下方竖杆", "main-vertical-top": "门框上方竖杆",
    "access-door-fixed-left-frame": "固定门框左杆", "access-door-fixed-right-frame": "固定门框右杆",
    "access-door-fixed-top-frame": "固定门框上杆", "access-door-fixed-bottom-frame": "固定门框下杆",
    "access-door-fixed-frame": "固定门框组件",
    "access-door-leaf-left-frame": "活动门左框", "access-door-leaf-right-frame": "活动门右框",
    "access-door-leaf-top-frame": "活动门上框", "access-door-leaf-bottom-frame": "活动门下框",
    "access-door-leaf": "活动门扇组件",
    "access-door-leaf-horizontal": "门内横杆", "access-door-leaf-vertical": "门内竖杆",
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
