import { buildAutomaticDimensionReport } from "./partInspection.mjs";
import { beginPartSideSketch } from "./sketchArea.mjs";
import { scheduleDesignerPartThumbnailHydration } from "./partThumbnail.mjs";
import { isNestingResultStale, restoreSavedNestingTask } from "./nestingWorkflow.mjs";
import { groupNestingPlans } from "./nestingGroups.mjs";
import { selectedNestingPlanIds } from "./nestingExport.mjs";
import { cancelNestingPlanHydration, getNestingPlacementColor, scheduleNestingPlanHydration } from "./nestingPreview.mjs";
import { isPlatePart, isSheetPart, isComponentPart, isTubeNestingPart, plateDimensions, componentBounds,
  manufacturingPartKind, manufacturingPartKindLabel, partSourcingLabel, tubeNestingExclusionReason } from "./manufacturingParts.mjs";
import { editPunchWizardFeature, validatePunchWizard, openPunchParameters, closePunchParameters, setPunchFeatureSelected, removeSelectedPunchWizardFeatures } from "./punchWizard.mjs";
import { beginPunchOperation, finishPunchOperation, previewPunch } from "./punchEditor.mjs";
import { handlePunchArrayGroupAction } from "./punchArrayGroupActions.mjs";
import {
  addPunchWizardFeature,
  createPunchWizardState,
  getPunchWizardPayload,
  removePunchWizardFeature,
  renderPunchWizardDialog as renderPunchWizardDialogView,
  updatePunchWizardField,
} from "./punchWizard.mjs";
import {
  changePunchRecordKind,
  initializePunchRecordSource,
  punchProfileChoices,
  selectPunchProfileSource,
  updatePunchProfileParameter,
} from "./punchProfileSource.mjs";
export { isTubeNestingPart } from "./manufacturingParts.mjs";

const hydrationTokens = new WeakMap();
const partViewportOwners = new WeakMap();
const partViewportLoads = new WeakMap();
const emptyPartResources = { get: async () => { throw new Error("Empty part view has no resources"); } };
let partViewportCancellationSequence = 0;
const nestingPartScrollSnapshots = new WeakMap();
const nestingPartCenterRequests = new WeakMap();

function hasPartDrawing(part) {
  return !!(part?.properties?.["tubeDesigner.partDrawing"]?.drawing
    ?? part?.properties?.["tubeDesigner.punchWizard"]?.drawing);
}

export function listManufacturingParts(designer = {}) {
  return (designer.manufacturingGroups ?? []).flatMap((product) =>
    (product.parts ?? []).map((part) => ({
      ...part,
      productEntityId: product.productEntityId,
      generationRunId: product.generationRunId,
      productName: product.name,
      productCode: product.productCode,
    })));
}

export function listNestingParts(designer = {}) {
  // The nesting page has an explicit membership list. Entries may either own
  // imported geometry or directly reference a product disassembly.
  const hasIndependentSnapshot = Array.isArray(designer.nestingGroups);
  if (hasIndependentSnapshot && designer.nestingGroups.length === 0) return [];
  const groups = hasIndependentSnapshot ? designer.nestingGroups : (designer.manufacturingGroups ?? []);
  return listManufacturingParts({ ...designer, manufacturingGroups: groups })
    .map(part => ({
      ...part,
      nestingEntry: hasIndependentSnapshot,
      independentNesting: hasIndependentSnapshot && !part.linkedNesting,
    }));
}

export function nestingSelectionKey(view) {
  return Array.isArray(view.scene?.tubeDesigner?.nestingGroups) ? "tubeDesignerNestingSelectedPartIds" : "tubeDesignerSelectedPartIds";
}

function listAreaParts(view) {
  return view.activeAreaId === "nesting" ? listNestingParts(view.scene?.tubeDesigner ?? {})
    : listManufacturingParts(view.scene?.tubeDesigner ?? {});
}

export function buildMaterialProfileGroups(parts = []) {
  const groups = new Map();
  for (const part of parts) {
    const material = partMaterial(part);
    const profile = partProfile(part);
    const key = `${material}\u001f${profile}`;
    let group = groups.get(key);
    if (!group) {
      group = {
        id: `stock-${hashText(key)}`,
        key,
        material,
        profile,
        parts: [],
        quantity: 0,
        totalLength: 0,
      };
      groups.set(key, group);
    }
    const quantity = partQuantity(part);
    group.parts.push(part);
    group.quantity += quantity;
    if (isTubeNestingPart(part)) group.totalLength += Math.max(0, finiteNumber(part.length) ?? 0) * quantity;
  }
  return [...groups.values()].sort((left, right) =>
    left.material.localeCompare(right.material, "zh-CN")
      || left.profile.localeCompare(right.profile, "zh-CN"));
}

export function buildProfileGroups(parts = [], { includeNonTube = false } = {}) {
  const groups = new Map();
  for (const part of parts) {
    if (!includeNonTube && !isTubeNestingPart(part)) continue;
    const profile = part?.profile ?? part?.properties?.["tubeDesigner.profile"] ?? {};
    // Group by the section snapshot, never by material or a user-editable name.
    const section = isSheetPart(part) ? { kind: manufacturingPartKind(part), ...plateDimensions(part) }
      : isComponentPart(part) ? { kind: "accessory", modelReference: part.properties?.["manufacturing.modelReference"] ?? partProfile(part), ...componentBounds(part) } : {};
    for (const field of [
      "id", "kind", "packageVersion", "width", "depth", "diameter",
      "wallThickness", "cornerRadius", "hollow", "contentDigest",
      "parameters", "contours", "specification",
    ]) {
      if (profile[field] != null && profile[field] !== "") section[field] = profile[field];
    }
    if (!section.id && !section.kind) section.displayName = String(profile.displayName ?? "").trim();
    const hasSection = Object.values(section).some((value) => value !== "");
    const eligibilityKey = isTubeNestingPart(part) ? "" : `excluded:${tubeNestingExclusionReason(part)}:`;
    const key = eligibilityKey + (hasSection ? stableSectionKey(section) : `unknown:${String(part.entityId)}`);
    let group = groups.get(key);
    if (!group) {
      group = {
        id: `section-${hashText(key)}`,
        key,
        profile: partProfile(part),
        parts: [],
        quantity: 0,
        totalLength: 0,
      };
      groups.set(key, group);
    }
    const quantity = partQuantity(part);
    group.parts.push(part);
    group.quantity += quantity;
    if (isTubeNestingPart(part)) group.totalLength += Math.max(0, finiteNumber(part.length) ?? 0) * quantity;
  }
  return [...groups.values()].sort((left, right) => left.profile.localeCompare(right.profile, "zh-CN"));
}

function stableSectionKey(value) {
  if (Array.isArray(value)) return `[${value.map(stableSectionKey).join(",")}]`;
  if (value && typeof value === "object") {
    return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${stableSectionKey(value[key])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

export function filterManufacturingParts(parts = [], options = {}) {
  const query = String(options.query ?? "").trim().toLocaleLowerCase("zh-CN");
  const filter = String(options.filter ?? "all").trim() || "all";
  const selectedIds = options.selectedIds instanceof Set
    ? options.selectedIds
    : new Set(options.selectedIds ?? []);
  return parts.filter((part) => {
    const partId = String(part?.entityId ?? "");
    if (filter === "selected" && !selectedIds.has(partId)) return false;
    const processKind = partProcessKind(part);
    if (filter === "straight" && processKind !== "straight") return false;
    if (filter === "special" && processKind !== "special") return false;
    if (filter === "plate" && processKind !== "plate") return false;
    if (filter === "non-tube" && isTubeNestingPart(part)) return false;
    if (["glass", "accessory"].includes(filter) && processKind !== filter) return false;
    if (!query) return true;
    const searchable = [
      partDisplayName(part),
      part?.partNumber,
      part?.productName,
      part?.productCode,
      partMaterial(part),
      partProfile(part),
      partCategory(part),
      partProcessLabel(part),
      partSourcingLabel(part),
    ].join(" ").toLocaleLowerCase("zh-CN");
    return searchable.includes(query);
  });
}

export function renderPartsLeftPane(context, view) {
  return renderManufacturingPartsLeftPane(context, view, { legacyPartsArea: true });
}

function renderManufacturingPartsLeftPane(context, view, options = {}) {
  const legacyPartsArea = Boolean(options.legacyPartsArea);
  const designer = view.scene?.tubeDesigner ?? {};
  const parts = listManufacturingParts(designer);
  const allGroups = buildMaterialProfileGroups(parts);
  const selectedIds = selectedPartIds(view, parts);
  const query = String(view.tubeDesignerPartSearchText ?? "");
  const filter = String(view.tubeDesignerPartFilter ?? "all");
  const visibleParts = filterManufacturingParts(parts, { query, filter, selectedIds });
  const groups = buildMaterialProfileGroups(visibleParts);
  const activePart = resolveActivePart(view, visibleParts.length ? visibleParts : parts);
  const totalQuantity = parts.reduce((total, part) => total + partQuantity(part), 0);
  const selectedParts = parts.filter((part) => selectedIds.has(String(part.entityId)));
  const selectedQuantity = selectedParts.reduce((total, part) => total + partQuantity(part), 0);
  const netLength = parts.filter(isTubeNestingPart).reduce(
    (total, part) => total + Math.max(0, finiteNumber(part.length) ?? 0) * partQuantity(part),
    0,
  );
  const straightCount = parts.filter((part) => partProcessKind(part) === "straight").length;
  const plateCount = parts.filter(isPlatePart).length;
  const specialCount = parts.filter((part) => partProcessKind(part) === "special").length;
  const otherCount = parts.filter((part) => !isTubeNestingPart(part) && !isPlatePart(part)).length;
  scheduleDesignerPartThumbnailHydration(context);
  schedulePartsAreaHydration(context, view);
  return `
    <div class="tube-designer-panel tube-designer-parts-panel">
      <div class="tube-designer-heading tube-designer-parts-heading">
        <div><strong>${legacyPartsArea ? "制造零件" : "零件清单"}</strong><span>${allGroups.length} 个材料截面组 · ${parts.length} 种 / ${totalQuantity} 件</span></div>
        <div>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-open-disassemble" ${view.pending ? "disabled" : ""}>更新下料</button>
          ${legacyPartsArea ? `<button class="tube-designer-primary tube-designer-parts-nest-button" data-cam-action="tube-designer-parts-open-nesting" ${view.pending || !selectedParts.length ? "disabled" : ""}>进入下料 <b>${selectedQuantity}</b></button>` : ""}
        </div>
      </div>
      ${parts.length ? `
        <div class="tube-designer-parts-summary">
          <span><small>材料截面组</small><strong>${allGroups.length}</strong></span>
          <span><small>零件总数</small><strong>${totalQuantity} 件</strong></span>
          <span><small>管材净用料</small><strong>${formatCompactLength(netLength)}</strong></span>
        </div>
        <div class="tube-designer-parts-tools">
          <label class="tube-designer-parts-search">
            <span aria-hidden="true">⌕</span>
            <input type="search" value="${escapeAttribute(query)}" placeholder="搜索零件号、产品、材料或规格" data-cam-change-action="tube-designer-parts-search" aria-label="搜索制造零件" />
          </label>
          <div class="tube-designer-parts-filter" role="group" aria-label="零件筛选">
            ${renderFilterButton("all", "全部", parts.length, filter)}
            ${renderFilterButton("selected", "已选", selectedParts.length, filter)}
            ${renderFilterButton("straight", "直切", straightCount, filter)}
            ${renderFilterButton("special", "斜切 / 异形", specialCount, filter)}
            ${plateCount ? renderFilterButton("plate", "板件", plateCount, filter) : ""}
            ${otherCount ? renderFilterButton("non-tube", "非管排件", parts.filter((part) => !isTubeNestingPart(part)).length, filter) : ""}
          </div>
          <div class="tube-designer-parts-selection-bar">
            <span>当前显示 ${visibleParts.length} 种 · 已选 ${selectedParts.length} 种 / ${selectedQuantity} 件</span>
            <div>
              <button data-cam-action="tube-designer-parts-select-filtered" ${visibleParts.length ? "" : "disabled"}>选择当前</button>
              <button data-cam-action="tube-designer-parts-clear-filtered" ${visibleParts.some((part) => selectedIds.has(String(part.entityId))) ? "" : "disabled"}>清空当前</button>
            </div>
          </div>
        </div>
        ${groups.length
          ? `<div class="tube-designer-stock-groups">${groups.map((group) => renderStockGroup(group, activePart, selectedIds, view)).join("")}</div>`
          : `<div class="tube-designer-empty-state tube-designer-parts-empty tube-designer-parts-no-results">
              <strong>没有符合条件的零件</strong>
              <span>可以换一个关键词，或清除当前筛选条件。</span>
              <button class="tube-designer-secondary" data-cam-action="tube-designer-parts-clear-filters">清除筛选</button>
            </div>`}`
        : `<div class="tube-designer-empty-state tube-designer-parts-empty">
            <strong>还没有制造零件</strong>
            <span>先在“产品”中点击“导入下料”，结果会自动进入这里。</span>
            <button class="tube-designer-primary" data-cam-action="tube-designer-open-disassemble" ${view.pending ? "disabled" : ""}>选择产品并导入下料</button>
          </div>`}
    </div>`;
}

export function renderPartsRightPane() {
  return "";
}

export function renderPunchWizardDialog(_context, view) {
  if(view.tubeDesignerPartDrawing)return "";
  const state = view?.tubeDesignerPunchWizard;
  if (!state) return "";
  const part = listNestingParts(view.scene?.tubeDesigner ?? {})
    .find(item => String(item.entityId) === String(state.partId));
  if (!part) return "";
  const profile = partProfile(part);
  const introHtml = `<section class="tube-designer-nesting-punch-setup tube-designer-punch-existing-main">
    <div class="tube-designer-nesting-punch-profile-summary"><strong>主管 · ${escapeText(partDisplayName(part))}</strong><span>${escapeText(profile)}</span><small>成品长度 ${formatNumber(part.length)} mm</small></div>
  </section>`;
  return renderPunchWizardDialogView(part, view, {
    title: "主管冲孔向导",
    previewTitle: "主管与刀具场景",
    previewStatus: "蓝色半透明实体是主管，橙色拉伸体是当前孔刀；保存孔位后再应用到零件。",
    previewActionLabel: "更新刀具体",
    previewLegendHtml: '<div class="tube-designer-punch-preview-legend"><span><i class="is-blank"></i>主管</span><span><i class="is-tool"></i>孔刀拉伸体</span></div>',
    featureEditorTitle: state.editingId ? "编辑孔位" : "当前孔刀参数",
    featureAddLabel: state.editingId ? "保存孔位修改" : "加入孔位清单",
    featureListTitle: "冲孔记录",
    featureEmptyText: "当前主管尚未配置孔位。",
    tableMode: true,
    showEnds: true,
    editorFirst: true,
    introInEditor: true,
    bodyClass: "tube-designer-punch-body--creation",
    dialogClass: "tube-designer-nesting-punch-dialog",
    branchProfiles: punchProfileChoices(view),
    introHtml,
  });
}

function activePunchPart(view) {
  return listNestingParts(view.scene?.tubeDesigner ?? {})
    .find((part) => String(part.entityId) === String(view.tubeDesignerPunchWizard?.partId ?? ""));
}

async function previewActivePunch(context, view, ops, options = {}) {
  const part = activePunchPart(view);
  if (part && typeof context?.sceneProxy?.invoke === "function") {
    await previewPunch(context, view, part, ops, null, options);
  }
  else ops.renderProject(context, view);
}

export function renderPartsViewportOverlay(_context, view) {
  const parts = listAreaParts(view);
  const part = resolveActivePart(view, parts);
  if (!part) {
    return `<div class="tube-designer-parts-viewport-empty">
      <strong>零件视图</strong>
      <span>导入下料后，在左侧选择零件即可查看最终几何与自动尺寸。</span>
    </div>`;
  }
  const state = view.tubeDesignerPartMeasurementState;
  const key = partMeasurementKey(part);
  const matchingState = state?.key === key ? state : { status: "loading" };
  const processKind = partProcessKind(part);
  return `
    <div class="tube-designer-parts-viewport-header">
      <div class="tube-designer-part-scene-identity">
        <div class="tube-designer-part-scene-kicker">
          <span class="tube-designer-process-badge ${escapeAttribute(processKind)}">${escapeText(partProcessLabel(part))}</span>
          <span>${escapeText(partCategory(part))}</span>
        </div>
        <strong>${escapeText(partDisplayName(part))}</strong>
        <span>${escapeText(part.partNumber)} · ${escapeText(part.productName || "未命名产品")}</span>
      </div>
      <div class="tube-designer-part-scene-facts">
        <span><small>材料</small><strong>${escapeText(partMaterial(part))}</strong></span>
        <span><small>规格</small><strong>${escapeText(partProfile(part))}</strong></span>
        <span><small>${isComponentPart(part) ? "供料方式" : isSheetPart(part) ? "厚度" : "成品长度"}</small><strong>${isComponentPart(part) ? escapeText(partSourcingLabel(part)) : `${formatNumber(isSheetPart(part) ? plateDimensions(part).thickness : part.length)} mm`}</strong></span>
        <span><small>数量</small><strong>${partQuantity(part)} 件</strong></span>
      </div>
      <div class="tube-designer-parts-view-actions">
        <button data-cam-action="tube-designer-parts-toggle-dimensions" aria-pressed="${view.tubeDesignerPartDimensionsVisible === false ? "false" : "true"}">${view.tubeDesignerPartDimensionsVisible === false ? "显示尺寸" : "隐藏尺寸"}</button>
        <button data-cam-action="tube-designer-parts-default-view">默认视图</button>
        <button data-cam-action="tube-designer-parts-fit-view">适合窗口</button>
      </div>
    </div>
    <aside class="tube-designer-parts-measurement" data-tube-designer-part-measurement>
      ${renderPartMeasurement(part, matchingState, view)}
    </aside>
    ${renderPartProgress(view)}
    <div class="tube-designer-parts-scene-help">右键旋转 · 中键平移 · 滚轮缩放</div>`;
}

export function renderNestingLeftPane(context, view) {
  const parts = listNestingParts(view.scene?.tubeDesigner ?? {});
  const plans = listNestingPlans(view);
  const activePlan = resolveActiveNestingPlan(view, plans);
  const selectedIds = selectedPartIds(view, parts.filter(isTubeNestingPart));
  const query = String(view.tubeDesignerPartSearchText ?? "");
  const filter = String(view.tubeDesignerPartFilter ?? "all");
  const visibleParts = filterManufacturingParts(parts, { query, filter, selectedIds });
  const selectableVisibleParts = visibleParts.filter(isTubeNestingPart);
  const groups = buildProfileGroups(visibleParts, { includeNonTube: true });
  const selectedParts = parts.filter((part) => isTubeNestingPart(part) && selectedIds.has(String(part.entityId)));
  const selectedQuantity = selectedParts.reduce((total, part) => total + partQuantity(part), 0);
  const totalQuantity = parts.filter(isTubeNestingPart).reduce((total, part) => total + partQuantity(part), 0);
  const excludedParts = parts.filter((part) => !isTubeNestingPart(part));
  const excludedQuantity = excludedParts.reduce((total, part) => total + partQuantity(part), 0);
  const straightCount = parts.filter((part) => partProcessKind(part) === "straight").length;
  const specialCount = parts.filter((part) => isTubeNestingPart(part) && partProcessKind(part) !== "straight").length;
  captureNestingPartListScroll(context, view);
  return `
    <div class="tube-designer-cutting-parts">
      <header class="tube-designer-cutting-pane-header">
        <div><strong>零件清单</strong><span>管材已选 ${selectedQuantity} / ${totalQuantity} 件${excludedQuantity ? ` · 另行处理 ${excludedQuantity} 件` : ""}</span></div>
        <span class="tube-designer-cutting-pane-menu-hint">操作请从上方菜单选择</span>
      </header>
      ${parts.length ? `
        <div class="tube-designer-cutting-tools">
          <label class="tube-designer-cutting-search">
            <span aria-hidden="true">⌕</span>
            <input type="search" value="${escapeAttribute(query)}" placeholder="搜索零件" data-cam-change-action="tube-designer-parts-search" aria-label="搜索待排零件" />
          </label>
          <div class="tube-designer-cutting-filters" role="group" aria-label="零件筛选">
            ${renderFilterButton("all", "全部", parts.length, filter)}
            ${renderFilterButton("selected", "已选", selectedParts.length, filter)}
            ${renderFilterButton("straight", "直切", straightCount, filter)}
            ${renderFilterButton("special", "斜切", specialCount, filter)}
            ${parts.some(isPlatePart) ? renderFilterButton("plate", "板件", parts.filter(isPlatePart).length, filter) : ""}
            ${excludedParts.some((part) => !isPlatePart(part)) ? renderFilterButton("non-tube", "另行处理", excludedParts.length, filter) : ""}
          </div>
          <div class="tube-designer-cutting-select-actions">
            <span>${groups.length} 种截面 · 显示 ${visibleParts.length} 种零件</span>
            <div>
              <button data-cam-action="tube-designer-parts-select-filtered" ${selectableVisibleParts.length ? "" : "disabled"}>全选</button>
              <button data-cam-action="tube-designer-parts-clear-filtered" ${selectableVisibleParts.some((part) => selectedIds.has(String(part.entityId))) ? "" : "disabled"}>清空</button>
              <button data-cam-action="tube-designer-delete-nesting-parts" ${view.pending || !selectedParts.length ? "disabled" : ""}>删除所选</button>
            </div>
          </div>
        </div>
        <div class="tube-designer-cutting-group-list">
          ${groups.length
            ? groups.map((group) => renderNestingStockGroup(group, activePlan, selectedIds, view)).join("")
            : `<div class="tube-designer-cutting-empty"><strong>没有符合条件的零件</strong><span>更换关键词或筛选条件。</span></div>`}
        </div>`
        : `<div class="tube-designer-cutting-empty"><strong>还没有零件</strong><span>可以回到产品页完成拆单，也可以从上方“零件”菜单添加标准零件、导入 STEP / IGES 或添加冲孔件。</span></div>`}
      ${renderNestingPartContextMenu(view, plans)}
    </div>`;
}

function renderNestingPartContextMenu(view, plans) {
  const state = view?.tubeDesignerNestingContextMenu;
  if (!state?.partId) return "";
  const partId = String(state.partId);
  const part = listNestingParts(view.scene?.tubeDesigner ?? {})
    .find(item => String(item.entityId) === partId);
  if (!part) return "";
  const locations = nestingPartLocationsAcrossPlans(plans, partId);
  const location = locations[0];
  const planIndex = location ? plans.indexOf(location.plan) : -1;
  const locationText = location
    ? `${nestingPlanName(location.plan, planIndex, plans)} · 第 ${location.index + 1} 件`
    : "当前零件尚未排入母材";
  return `<div class="tube-designer-nesting-context-menu" data-tube-designer-nesting-context-menu style="left:${Math.max(8, Number(state.x) || 8)}px;top:${Math.max(8, Number(state.y) || 8)}px" role="menu">
    <strong>${escapeText(partDisplayName(part))}</strong>
    <span>${escapeText(locationText)}</span>
    <button type="button" data-cam-action="tube-designer-nesting-view-part-locations" data-tube-designer-part-id="${escapeAttribute(partId)}" ${location ? "" : "disabled"} role="menuitem">查看所在排样</button>
  </div>`;
}

export function renderNestingRightPane(context, view) {
  const plans = listNestingPlans(view);
  const activePlan = resolveActiveNestingPlan(view, plans);
  const parts = listNestingParts(view.scene?.tubeDesigner ?? {});
  const part = resolveActivePart(view, parts);
  const showPlan = view.tubeDesignerNestingSelectionKind === "plan" && activePlan;
  if (showPlan) return renderNestingPlanInspector(activePlan, plans.indexOf(activePlan), isNestingResultStale(view, context), view);
  if (!part) {
    return `<div class="tube-designer-cutting-inspector">
      <header class="tube-designer-cutting-pane-header"><div><strong>当前选择</strong><span>零件或排样原材</span></div></header>
      <div class="tube-designer-cutting-empty"><strong>未选择内容</strong><span>从左侧选择零件，或从下方选择排样结果。</span></div>
    </div>`;
  }
  return renderNestingPartInspector(part, view);
}

export function renderNestingViewportOverlay(context, view) {
  const plans = listNestingPlans(view);
  const activePlan = resolveActiveNestingPlan(view, plans);
  const parts = listNestingParts(view.scene?.tubeDesigner ?? {});
  const part = resolveActivePart(view, parts);
  const showPlan = view.tubeDesignerNestingSelectionKind === "plan" && activePlan;
  const punchPart = showPlan
    ? parts.find(item => String(item.entityId) === String(view.tubeDesignerActiveNestingPartId ?? ""))
    : part;
  if (showPlan) {
    hydrationTokens.set(view, {});
    cancelPartViewportLoad(view);
    setPartProgress(context, view, null);
    view.tubeDesignerPartViewportKey = "";
    scheduleNestingPlanHydration(context, view, activePlan, parts);
  } else {
    cancelNestingPlanHydration(view);
  }
  if (!part && !showPlan) {
    hydrationTokens.set(view, {});
    cancelPartViewportLoad(view);
    setPartProgress(context, view, null);
    partViewportOwners.delete(view);
    view.tubeDesignerPartViewportKey = "";
    view.viewport?.setDimensionAnnotations?.([]);
    view.viewport?.setVisibleEntityIds?.([]);
    return `<div class="tube-designer-nesting-empty">
      <strong>下料三维场景</strong>
      <span>拆单后的零件会出现在左侧；选择排样结果后，这里显示对应原材。</span>
    </div>`;
  }
  if (!showPlan) schedulePartsAreaHydration(context, view);
  return `
    <div class="tube-designer-cutting-scene-header">
      <div class="tube-designer-cutting-scene-title">
        <span>${showPlan ? (isNestingResultStale(view, context) ? "旧排样结果 · 需要重新排样" : "排样母材") : "待排零件"}</span>
        <strong>${escapeText(showPlan ? nestingPlanName(activePlan, plans.indexOf(activePlan), plans) : partDisplayName(part))}</strong>
        <small>${escapeText(showPlan
          ? nestingPlanProfile(activePlan)
          : `${partMaterial(part)} · ${partProfile(part)}`)}</small>
      </div>
      <div class="tube-designer-cutting-scene-actions">
        ${!showPlan ? `<button data-cam-action="tube-designer-parts-toggle-dimensions" aria-pressed="${view.tubeDesignerPartDimensionsVisible === false ? "false" : "true"}">${view.tubeDesignerPartDimensionsVisible === false ? "显示标注" : "隐藏标注"}</button>` : ""}
        <button data-cam-action="tube-designer-parts-default-view">等轴测</button>
        <button data-cam-action="tube-designer-parts-fit-view">适合窗口</button>
        ${punchPart && isTubeNestingPart(punchPart) && !showPlan ? `<button class="tube-designer-secondary" data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${escapeAttribute(punchPart.entityId)}" ${view.pending ? "disabled" : ""}>二维绘制</button>` : ""}
        ${punchPart && isTubeNestingPart(punchPart) && punchPart.independentNesting ? `<button class="tube-designer-primary" data-cam-action="${hasPartDrawing(punchPart)?"tube-designer-drawing-open":"tube-designer-punch-open"}" data-tube-designer-part-id="${escapeAttribute(punchPart.entityId)}">${hasPartDrawing(punchPart)?"三维编辑":"冲孔向导"}</button>` : ""}
      </div>
    </div>
    ${showPlan ? '<span class="tube-designer-nesting-preview-status" data-tube-designer-nesting-preview-status role="status"></span>' : ""}
    ${showPlan ? renderNestingSceneSummary(activePlan) : ""}
    ${showPlan ? "" : renderPartProgress(view)}
    <div class="tube-designer-cutting-scene-help">右键旋转 · 中键平移 · 滚轮缩放</div>`;
}

export function listNestingPlans(view) {
  const designer = view?.scene?.tubeDesigner ?? {};
  const result = view?.tubeDesignerNestingResult ?? designer.nestingResult ?? designer.nesting ?? {};
  const plans = Array.isArray(result) ? result : (result.plans ?? result.stockPlans ?? []);
  return Array.isArray(plans) ? plans : [];
}

export function renderNestingResultDock(context, view) {
  const plans = listNestingPlans(view);
  const groups = groupNestingPlans(plans);
  const result = view.tubeDesignerNestingResult;
  const stale = isNestingResultStale(view, context);
  const unplaced = result?.unplaced ?? [];
  const missing = unplaced.reduce((sum, row) => sum + row.quantity, 0);
  const placed = plans.reduce((sum, plan) => sum + (plan.placements?.length ?? 0), 0);
  const selected = selectedNestingPlanIds(view, plans);
  const locked = new Set((view.tubeDesignerLockedNestingPlanIds ?? []).map(String));
  const allSelected = plans.length > 0 && selected.size === plans.length;
  const indeterminate = selected.size > 0 && !allSelected;
  queueMicrotask(() => {
    const checkbox = context?.mount?.querySelector?.('[data-cam-action="tube-designer-nesting-toggle-all-plans"]');
    if (checkbox) checkbox.indeterminate = indeterminate;
    for (const details of context?.mount?.querySelectorAll?.('[data-tube-designer-nesting-result-group]') ?? []) {
      details.addEventListener("toggle", () => {
        if (details.isConnected === false) return;
        const closed = new Set(view.tubeDesignerClosedNestingResultGroups ?? []);
        const key = details.dataset.tubeDesignerNestingGroupKey;
        if (details.open) closed.delete(key); else closed.add(key);
        view.tubeDesignerClosedNestingResultGroups = [...closed];
      });
      const input = details.querySelector('[data-cam-action="tube-designer-nesting-toggle-group"]');
      if (input) input.indeterminate = input.getAttribute("aria-checked") === "mixed";
    }
  });
  const activePlanId = String(view.tubeDesignerActiveNestingPlanId ?? plans[0]?.id ?? plans[0]?.stockId ?? "");
  scheduleActiveNestingPlanCentering(context, activePlanId);
  return `<div class="tube-designer-bottom-splitter" data-cam-resize-pane="bottom" data-no-window-drag
      title="拖拽调整排样结果区域高度" aria-label="调整排样结果区域高度"></div>
    <section class="tube-designer-nesting-bottom" aria-label="排样结果列表">
      <header>
        <div><strong>排样结果</strong><span>${stale ? "输入已变化或上次排样失败，以下是旧结果" : "选中母材，查看三维排布与切割顺序"}</span></div>
        <div class="tube-designer-nesting-result-actions">
          <span>${plans.length} 根母材 · 已排 ${placed} 件${missing ? ` · 未排 ${missing} 件` : ""} · 已选 ${selected.size} 根${locked.size ? ` · 已锁 ${locked.size} 根` : ""}</span>
          <button type="button" data-cam-action="tube-designer-nesting-export-selected" ${!selected.size || stale || view.pending ? "disabled" : ""}>导出排样结果</button>
        </div>
      </header>
      <div class="tube-designer-nesting-result-table">
        ${stale ? '<div class="tube-designer-nesting-warning">这些结果不再对应当前输入，请点击“开始排样”重新计算。</div>' : ""}
        ${unplaced.length ? `<details class="tube-designer-nesting-unplaced" open><summary>未排入 ${missing} 件</summary>${unplaced.map((row) => `<div><strong>${escapeText(row.partName ?? row.partId)}</strong><span>${row.quantity} 件</span><span>${escapeText(row.reason ?? "母材不足")}</span></div>`).join("")}</details>` : ""}
        <div class="head"><span><input type="checkbox" data-cam-action="tube-designer-nesting-toggle-all-plans" aria-label="选择全部排样结果" aria-checked="${indeterminate ? "mixed" : allSelected}" ${allSelected ? "checked" : ""} ${!plans.length || view.pending ? "disabled" : ""} /></span><span class="tube-designer-nesting-lock-heading" aria-label="锁定状态" title="锁定状态">${renderNestingLockIcon(true)}</span><span>排样结果</span><span>截面规格</span><span>母材长</span><span>已用</span><span>余料</span><span>零件数</span><span>利用率</span></div>
        ${plans.length ? groups.map((group) => {
          const count = group.entries.filter(({ plan }) => selected.has(String(plan.id))).length;
          const checked = count === group.entries.length;
          const closed = (view.tubeDesignerClosedNestingResultGroups ?? []).includes(group.key);
          return `<details class="tube-designer-nesting-result-group" data-tube-designer-nesting-result-group data-tube-designer-nesting-group-key="${escapeAttribute(group.key)}" ${closed ? "" : "open"}>
            <summary><input type="checkbox" data-cam-action="tube-designer-nesting-toggle-group" data-tube-designer-nesting-group-key="${escapeAttribute(group.key)}" aria-label="选择该规格全部排样结果" aria-checked="${count && !checked ? "mixed" : checked}" ${checked ? "checked" : ""} ${view.pending ? "disabled" : ""} />
              <strong>${escapeText(group.label)}</strong><span>${group.entries.length} 根母材 · 已选 ${count} 根</span>
              <button type="button" data-cam-action="tube-designer-nesting-export-group" data-tube-designer-nesting-group-key="${escapeAttribute(group.key)}" ${stale || view.pending ? "disabled" : ""}>导出本组</button></summary>
            ${group.entries.map(({ plan, index }) => renderNestingPlanRow(plan, index, plans, activePlanId, selected.has(String(plan.id)), locked.has(String(plan.id)), view.pending)).join("")}
          </details>`;
        }).join("")
          : `<div class="empty"><strong>${result ? "本次没有可用排样方案" : "尚未生成排样结果"}</strong><span>${result ? "请根据未排原因调整母材，再点击“开始排样”。" : "勾选零件，设置母材和间距，再点击菜单中的“开始排样”。"}</span></div>`}
      </div>
    </section>`;
}

function renderNestingPlanRow(plan, index, plans, activePlanId, selected = true, locked = false, pending = false) {
  const planId = String(plan?.id ?? plan?.stockId ?? plan?.stockID ?? `stock-${index + 1}`);
  const stockLength = finiteNumber(plan?.stockLength ?? plan?.length) ?? 0;
  const usedLength = finiteNumber(plan?.usedLength ?? plan?.consumedLength) ?? 0;
  const remainingLength = finiteNumber(plan?.remainingLength ?? plan?.remnantLength)
    ?? Math.max(0, stockLength - usedLength);
  const utilization = finiteNumber(plan?.utilization)
    ?? (stockLength > 0 ? Math.max(0, Math.min(1, usedLength / stockLength)) : 0);
  const utilizationPercent = utilization <= 1 ? utilization * 100 : utilization;
  const placements = Array.isArray(plan?.placements) ? plan.placements : [];
  const partCount = finiteNumber(plan?.partCount) ?? placements.length;
  const profile = String(plan?.profile ?? plan?.profileName ?? plan?.stockTypeId ?? "截面未指定");
  const name = nestingPlanName(plan, index, plans);
  return `<div class="row ${planId === activePlanId ? "active" : ""} ${locked ? "locked" : ""}"
    data-tube-designer-nesting-plan-row data-tube-designer-nesting-plan-id="${escapeAttribute(planId)}">
    <span><input type="checkbox" data-cam-action="tube-designer-nesting-toggle-plan" data-tube-designer-nesting-plan-id="${escapeAttribute(planId)}" aria-label="选择${escapeAttribute(plan?.name ?? `母材 ${index + 1}`)}" ${selected ? "checked" : ""} ${pending ? "disabled" : ""} /></span>
    <span><button type="button" class="tube-designer-nesting-lock-button" data-cam-action="tube-designer-toggle-nesting-plan-lock"
      data-tube-designer-nesting-plan-id="${escapeAttribute(planId)}" aria-pressed="${locked}" aria-label="${locked ? "解除锁定" : "锁定此排样结果"}"
      title="${locked ? "解除锁定，允许重新排样" : "锁定此结果，重新排样时保持不变"}" ${pending ? "disabled" : ""}>${renderNestingLockIcon(locked)}</button></span>
    <button type="button" class="tube-designer-nesting-plan-select" ${pending ? "disabled" : ""}
      data-cam-action="tube-designer-select-nesting-plan" data-tube-designer-nesting-plan-id="${escapeAttribute(planId)}">
    <span>${escapeText(name)}</span>
    <span>${escapeText(profile)}</span>
    <span>${formatNumber(stockLength)} mm</span>
    <span>${formatNumber(usedLength)} mm</span>
    <span>${formatNumber(remainingLength)} mm</span>
    <span>${formatNumber(partCount)} 件</span>
    <span class="utilization"><i style="--progress:${Math.max(0, Math.min(100, utilizationPercent))}%"></i><b>${formatNumber(utilizationPercent)}%</b></span>
    </button>
  </div>`;
}

function renderNestingStockGroup(group, activePlan, selectedIds, view) {
  const partIds = group.parts.filter(isTubeNestingPart).map((part) => String(part.entityId));
  const selectedCount = partIds.filter((id) => selectedIds.has(id)).length;
  const checked = partIds.length > 0 && selectedCount === partIds.length;
  return `<details class="tube-designer-cutting-group" data-tube-designer-nesting-profile-id="${escapeAttribute(group.id)}" open>
    <summary>
      <input type="checkbox" data-cam-action="tube-designer-parts-toggle-group"
        data-tube-designer-part-ids="${escapeAttribute(partIds.join(" "))}" ${checked ? "checked" : ""}
        ${selectedCount > 0 && !checked ? "data-tube-designer-indeterminate=\"true\"" : ""}
        ${partIds.length ? "" : "disabled"} aria-label="选择该截面的全部管材零件" />
      <span><strong>${escapeText(group.profile)}</strong></span>
      <b>${partIds.length ? `${selectedCount}/${group.parts.length}` : escapeText(manufacturingPartKindLabel(group.parts[0]))}</b>
    </summary>
    <div>${group.parts.map((part) => {
      const locations = nestingPartLocations(activePlan, part.entityId);
      const active = String(view.tubeDesignerActiveNestingPartId ?? "") === String(part.entityId);
      return `
      <div class="tube-designer-cutting-part ${active ? "active" : ""} ${locations.length ? "in-active-plan" : ""}"
        data-tube-designer-nesting-part-row data-tube-designer-part-id="${escapeAttribute(part.entityId)}">
        <input type="checkbox" data-cam-action="tube-designer-parts-toggle-part"
          data-tube-designer-part-id="${escapeAttribute(part.entityId)}" ${isTubeNestingPart(part) && selectedIds.has(String(part.entityId)) ? "checked" : ""}
          ${isTubeNestingPart(part) ? "" : `disabled title="${escapeAttribute(tubeNestingExclusionReason(part))}"`}
          aria-label="选择 ${escapeAttribute(partDisplayName(part))}" />
        <button data-cam-action="tube-designer-parts-select-part" data-tube-designer-part-id="${escapeAttribute(part.entityId)}">
          <span><strong>${escapeText(partDisplayName(part))}</strong><small>${escapeText(part.partNumber || part.productName || "未编号")}</small></span>
          <span class="tube-designer-nesting-part-locations" title="${locations.length ? "在当前排样结果中的切割序号" : "未排入当前结果"}">${locations.length
            ? locations.slice(0, 3).map(({ placement, index }) => `<i style="--location-color:${getNestingPlacementColor(index, isActiveNestingPlacement(view, placement, index))}">${index + 1}</i>`).join("") + (locations.length > 3 ? `<small>+${locations.length - 3}</small>` : "")
            : ""}</span>
          <span><strong>${isSheetPart(part) || isComponentPart(part) ? escapeText(manufacturingPartKindLabel(part)) : `${formatNumber(part.length)} mm`}</strong><small>${!isTubeNestingPart(part) ? "不参与管材排样 · " : ""}× ${partQuantity(part)} 件</small></span>
        </button>
      </div>`;
    }).join("")}</div>
  </details>`;
}

function renderNestingPartInspector(part, view = {}) {
  const endProcess = partEndProcess(part);
  const imported = Boolean(part?.properties?.["manufacturing.imported"]);
  const importedMeasurement = part?.properties?.["manufacturing.geometryMeasurement"];
  const importedFeatureCount = Number.isFinite(Number(importedMeasurement?.openingCount))
    ? Number(importedMeasurement.openingCount) : 0;
  const importedSource = String(part?.properties?.["manufacturing.import.sourceFileName"] ?? "").trim();
  const linked = Boolean(part?.linkedNesting);
  return `<div class="tube-designer-cutting-inspector">
    <header class="tube-designer-cutting-pane-header">
      <div><strong>当前零件</strong><span>${escapeText(partDisplayName(part))}</span></div>
      ${isTubeNestingPart(part) ? `<div class="tube-designer-cutting-pane-header-actions">
        <button type="button" data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" ${view.pending ? "disabled" : ""}>二维绘制</button>
      </div>` : ""}
      <em class="tube-designer-process-badge ${escapeAttribute(partProcessKind(part))}">${escapeText(partProcessLabel(part))}</em>
    </header>
    <div class="tube-designer-cutting-inspector-scroll">
      <section class="tube-designer-part-editor" data-tube-designer-part-editor data-tube-designer-part-id="${escapeAttribute(part.entityId)}">
        <h3>零件参数</h3>
        <label><span>名称</span><input type="text" maxlength="160" value="${escapeAttribute(part.name || part.partNumber || "")}" data-tube-designer-part-field="name" ${view.pending || linked ? "disabled" : ""} /></label>
        ${imported ? `<label><span>材料</span><input type="text" maxlength="240" value="${escapeAttribute(partMaterial(part) === "材料未指定" ? "" : partMaterial(part))}" placeholder="例如：Q235B、不锈钢 304" data-tube-designer-part-field="material" ${view.pending || linked ? "disabled" : ""} /></label>` : ""}
        <label><span>数量</span><input type="number" min="1" max="1000000" step="1" value="${escapeAttribute(partQuantity(part))}" data-tube-designer-part-field="quantity" ${view.pending || linked ? "disabled" : ""} /></label>
        <div class="tube-designer-part-editor-actions">
          ${linked ? "" : `<button class="tube-designer-primary" data-cam-action="tube-designer-part-edit-save" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" ${view.pending ? "disabled" : ""}>保存修改</button>`}
          <button class="tube-designer-danger" data-cam-action="tube-designer-part-edit-delete" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" ${view.pending ? "disabled" : ""}>${linked ? "移出下料" : "删除此零件"}</button>
        </div>
        <small>${linked ? "该零件直接关联产品拆单结果；名称和数量随产品更新，删除仅取消下料关联。" : "修改后的数量会用于下料和导出；删除只移除当前零件记录。"}</small>
      </section>
      <section>
        <h3>基本信息</h3>
        <dl>
          ${renderNestingProperty("零件编号", part.partNumber || "未编号")}
          ${renderNestingProperty("数量", `${partQuantity(part)} 件`)}
          ${renderNestingProperty(isSheetPart(part) || isComponentPart(part) ? `${manufacturingPartKindLabel(part)}尺寸` : "成品长度", isSheetPart(part) || isComponentPart(part) ? partProfile(part) : `${formatNumber(part.length)} mm`, true)}
          ${isComponentPart(part) || partSourcingLabel(part) !== "供料方式未指定" ? renderNestingProperty("供料方式", partSourcingLabel(part)) : ""}
          ${renderNestingProperty("来源产品", part.productName || part.productCode || "未命名产品", true)}
        </dl>
      </section>
      <section>
        <h3>下料信息</h3>
        <dl>
          ${renderNestingProperty("材料", partMaterial(part))}
          ${renderNestingProperty("截面", partProfile(part))}
          ${imported ? renderNestingProperty("几何识别", `主方向 +X · ${importedFeatureCount} 个孔 / 开口`, true) : ""}
          ${importedSource ? renderNestingProperty("来源文件", importedSource, true) : ""}
          ${!isTubeNestingPart(part) ? renderNestingProperty("下料方式", tubeNestingExclusionReason(part), true)
            : renderNestingProperty("起始端", cutName(endProcess.startCut)) + renderNestingProperty("结束端", cutName(endProcess.endCut))}
        </dl>
      </section>
      <div data-tube-designer-part-measurement data-tube-designer-nesting-measurement>
        ${renderNestingPartMeasurement(part, view)}
      </div>
    </div>
  </div>`;
}

function renderNestingPartMeasurement(part, view = {}) {
  const state = view.tubeDesignerPartMeasurementState;
  const key = partMeasurementKey(part);
  const matchingState = state?.key === key ? state : { status: "loading" };
  const report = matchingState?.report;
  const holes = Array.isArray(report?.holes) ? report.holes : [];
  const pitches = Array.isArray(report?.pitches) ? report.pitches : [];
  const measurement = part?.properties?.["manufacturing.geometryMeasurement"] ?? {};
  const section = measurement?.section ?? {};
  const wallThickness = finiteNumber(section.wallThickness ?? measurement.wallThickness);
  const normalizedAxis = String(measurement.normalizedAxis ?? "+X");
  let body;
  if (matchingState.status === "error") {
    body = `<div class="tube-designer-dimension-empty">${escapeText(matchingState.message ?? "无法读取零件复尺信息")}</div>`;
  } else if (!report) {
    body = `<div class="tube-designer-part-measurement-loading"><i></i><span>正在分析最终零件几何…</span></div>`;
  } else if (report.partKind === "accessory") {
    body = `<div class="tube-designer-dimension-overview">${[["width", "实体宽 X"], ["depth", "实体深 Y"], ["height", "实体高 Z"]
      ].map(([keyName, label]) => `<span><small>${label}</small><strong>${formatNumber(report.bounds?.[keyName])} mm</strong></span>`).join("")}</div>
      <div class="tube-designer-part-no-holes">配件按完整三维模型管理，不生成管材长度或孔距标尺。</div>`;
  } else if (report.plate) {
    body = `<div class="tube-designer-dimension-overview">
        <span><small>长边</small><strong>${formatNumber(report.plate.longSide)} mm</strong></span>
        <span><small>短边</small><strong>${formatNumber(report.plate.shortSide)} mm</strong></span>
        <span><small>厚度</small><strong>${formatNumber(report.plate.thickness)} mm</strong></span>
        <span><small>孔 / 开口</small><strong>${holes.length} 个</strong></span>
      </div>
      ${holes.length ? renderNestingHoleDetails(report, holes, pitches) : `<div class="tube-designer-dimension-empty">当前实体未识别到孔或开口。</div>`}`;
  } else {
    body = `<div class="tube-designer-dimension-overview">
        <span><small>成品总长</small><strong>${formatNumber(report.length)} mm</strong></span>
        <span><small>截面规格</small><strong>${escapeText(report.profile || partProfile(part))}</strong></span>
        <span><small>孔 / 开口</small><strong>${holes.length} 个</strong></span>
        <span><small>主方向</small><strong>${escapeText(normalizedAxis)}</strong></span>
        <span><small>壁厚</small><strong>${wallThickness != null ? `${formatNumber(wallThickness)} mm` : "未识别"}</strong></span>
        <span><small>数据来源</small><strong>最终几何</strong></span>
      </div>
      ${holes.length ? renderNestingHoleDetails(report, holes, pitches) : `<div class="tube-designer-dimension-empty">当前实体未识别到孔或开口；总长与截面取自最终实体。</div>`}`;
  }
  return `<section class="tube-designer-nesting-measurement-card">
    <h3><span>自动复尺</span><span>${report ? "取自最终三维实体 · 单位 mm" : "正在读取最终三维实体"}</span></h3>
    ${body}
  </section>`;
}

function renderNestingHoleDetails(report, holes, pitches) {
  return `<div class="tube-designer-dimension-list">
    ${holes.map((hole) => `
      <article>
        <strong>${hole.kind === "side-opening" ? "单侧开口" : "孔"} ${hole.index} · ${escapeText(hole.sizeLabel)}</strong>
        <span>中心距首端 ${formatNumber(hole.station)} mm · 距末端 ${formatNumber(Math.max(0, report.length - hole.station))} mm</span>
        <span>开口边距首端 ${formatNumber(hole.startEdgeDistance)} mm · 距末端 ${formatNumber(hole.endEdgeDistance)} mm</span>
        <span>面宽方向中心距边 ${formatNumber(hole.centerToFaceEdgeNegative)} / ${formatNumber(hole.centerToFaceEdgePositive)} mm</span>
        <span>面宽方向开口边净距 ${formatNumber(hole.faceEdgeClearanceNegative)} / ${formatNumber(hole.faceEdgeClearancePositive)} mm</span>
      </article>`).join("")}
    ${pitches.map((pitch) => `
      <article class="tube-designer-dimension-pitch">
        <strong>开口 ${pitch.from} — 开口 ${pitch.to}</strong>
        <span>中心距 ${formatNumber(pitch.centerDistance)} mm · 最近开口边净距 ${formatNumber(pitch.edgeClearance)} mm</span>
      </article>`).join("")}
  </div>`;
}

function renderNestingPlanInspector(plan, index, stale = false, view = {}) {
  const stockLength = finiteNumber(plan?.stockLength ?? plan?.length) ?? 0;
  const usedLength = finiteNumber(plan?.usedLength ?? plan?.consumedLength) ?? 0;
  const remainingLength = finiteNumber(plan?.remainingLength ?? plan?.remnantLength)
    ?? Math.max(0, stockLength - usedLength);
  const utilization = nestingPlanUtilization(plan, stockLength, usedLength);
  const placements = Array.isArray(plan?.placements) ? plan.placements : [];
  return `<div class="tube-designer-cutting-inspector">
    <header class="tube-designer-cutting-pane-header">
      <div><strong>当前母材</strong><span>${escapeText(nestingPlanName(plan, index))}</span></div>
      <em class="tube-designer-cutting-status">${stale ? "需重新排样" : escapeText(plan?.status ?? "已排样")}</em>
    </header>
    <div class="tube-designer-cutting-inspector-scroll">
      <section>
        <h3>排样摘要</h3>
        <dl>
          ${renderNestingProperty("截面", nestingPlanProfile(plan), true)}
          ${renderNestingProperty("原材长度", `${formatNumber(stockLength)} mm`)}
          ${renderNestingProperty("利用率", `${formatNumber(utilization)}%`)}
          ${renderNestingProperty("已用", `${formatNumber(usedLength)} mm`)}
          ${renderNestingProperty("余料", `${formatNumber(remainingLength)} mm`)}
        </dl>
      </section>
      <section>
        <h3>切割顺序 <span>${placements.length} 件</span></h3>
        <div class="tube-designer-cutting-order">
          ${placements.length ? placements.map((placement, placementIndex) => {
            const active = isActiveNestingPlacement(view, placement, placementIndex);
            return `<button type="button" class="${active ? "active" : ""}" data-cam-action="tube-designer-select-nesting-placement"
              data-tube-designer-nesting-plan-id="${escapeAttribute(plan?.id ?? plan?.stockId ?? "")}" data-tube-designer-part-id="${escapeAttribute(placement?.partId ?? "")}" data-tube-designer-placement-id="${escapeAttribute(nestingPlacementId(placement, placementIndex))}">
            <b style="border-color: ${getNestingPlacementColor(placementIndex, active)}" title="与三维零件颜色对应">${placementIndex + 1}</b>
            <span><strong>${escapeText(placement?.partName ?? placement?.name ?? placement?.partNumber ?? `零件 ${placementIndex + 1}`)}</strong><small>${formatNumber(placement?.start ?? placement?.position ?? 0)} — ${formatNumber(placement?.end ?? ((finiteNumber(placement?.start ?? placement?.position) ?? 0) + (finiteNumber(placement?.length) ?? 0)))} mm</small></span>
          </button>`;
          }).join("") : `<div class="tube-designer-cutting-empty"><span>暂无切割顺序数据</span></div>`}
        </div>
      </section>
    </div>
  </div>`;
}

function renderNestingProperty(label, value, wide = false) {
  return `<div class="${wide ? "wide" : ""}"><dt>${escapeText(label)}</dt><dd>${escapeText(value)}</dd></div>`;
}

function resolveActiveNestingPlan(view, plans) {
  const requested = String(view.tubeDesignerActiveNestingPlanId ?? "");
  const plan = plans.find((item, index) => String(item?.id ?? item?.stockId ?? `stock-${index + 1}`) === requested)
    ?? plans[0]
    ?? null;
  if (plan && !requested) view.tubeDesignerActiveNestingPlanId = String(plan?.id ?? plan?.stockId ?? "stock-1");
  return plan;
}

function nestingPlanName(plan, index = 0, plans = null) {
  if (plan?.name) return String(plan.name);
  const profile = nestingPlanProfile(plan);
  const ordinal = Array.isArray(plans)
    ? plans.slice(0, index + 1).filter((item) => nestingPlanProfile(item) === profile).length
    : index + 1;
  return `${profile}-${Math.max(1, ordinal)}`;
}

function renderNestingLockIcon(locked) {
  return locked
    ? `<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><rect x="5" y="10" width="14" height="10" rx="2"></rect><path d="M8 10V7a4 4 0 0 1 8 0v3"></path><circle cx="12" cy="15" r="1"></circle><path d="M12 16v2"></path></svg>`
    : `<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><rect x="5" y="10" width="14" height="10" rx="2"></rect><path d="M8 10V7a4 4 0 0 1 7.7-1.5"></path><circle cx="12" cy="15" r="1"></circle><path d="M12 16v2"></path></svg>`;
}

function nestingPlacementId(placement, index = 0) {
  return String(placement?.instanceId ?? `${placement?.partId ?? "part"}#${index + 1}`);
}

function isActiveNestingPlacement(view, placement, index = 0) {
  const placementId = String(view?.tubeDesignerActiveNestingPlacementId ?? "");
  if (placementId) return placementId === nestingPlacementId(placement, index);
  const partId = String(view?.tubeDesignerActiveNestingPartId ?? "");
  return Boolean(partId && partId === String(placement?.partId ?? ""));
}

function nestingPartLocations(plan, partId) {
  return (plan?.placements ?? []).map((placement, index) => ({ placement, index }))
    .filter(({ placement }) => String(placement?.partId ?? "") === String(partId ?? ""));
}

function nestingPartLocationsAcrossPlans(plans, partId) {
  return (Array.isArray(plans) ? plans : []).flatMap((plan) =>
    nestingPartLocations(plan, partId).map((location) => ({ ...location, plan })),
  );
}

export function attachNestingPartContextMenu(context, view, mount, ops) {
  if (!mount) return;
  if (view?.activeAreaId !== "nesting") {
    mount.oncontextmenu = null;
    mount.onmousedown = null;
    return;
  }
  mount.oncontextmenu = (event) => {
    const target = typeof Element !== "undefined" && event.target instanceof Element
      ? event.target : null;
    const row = target?.closest?.("[data-tube-designer-nesting-part-row]");
    const actionTarget = target?.closest?.("[data-cam-action]");
    const viewport = target?.closest?.("[data-cam-render-viewport]");
    const partId = String(row?.dataset?.tubeDesignerPartId
      ?? (viewport && !actionTarget ? view.tubeDesignerActiveNestingPartId : "")).trim();
    if (!partId) return;
    const plans = listNestingPlans(view);
    if (!nestingPartLocationsAcrossPlans(plans, partId).length) return;
    event.preventDefault();
    event.stopPropagation?.();
    view.tubeDesignerNestingContextMenu = {
      partId,
      x: Number(event.clientX ?? event.pageX ?? 8),
      y: Number(event.clientY ?? event.pageY ?? 8),
    };
    ops?.renderProject?.(context, view);
  };
  mount.onmousedown = (event) => {
    if (!view.tubeDesignerNestingContextMenu || event.button !== 0) return;
    const target = typeof Element !== "undefined" && event.target instanceof Element
      ? event.target : null;
    if (target?.closest?.("[data-tube-designer-nesting-context-menu]")) return;
    view.tubeDesignerNestingContextMenu = null;
    if (!target?.closest?.("[data-cam-action]")) ops?.renderProject?.(context, view);
  };
}

function captureNestingPartListScroll(context, view) {
  const requestedPartId = nestingPartCenterRequests.get(view) ?? "";
  nestingPartCenterRequests.delete(view);
  const partId = requestedPartId || String(view?.tubeDesignerActiveNestingPartId ?? "");
  const scroller = context?.mount?.querySelector?.(".tube-designer-cutting-group-list");
  const activeRow = scroller?.querySelector?.("[data-tube-designer-nesting-part-row].active");
  const previousPartId = String(activeRow?.dataset?.tubeDesignerPartId ?? "");
  const target = findNestingPartRow(scroller, partId);
  const scrollerRect = scroller?.getBoundingClientRect?.();
  const targetRect = target?.getBoundingClientRect?.();
  const height = finiteNumber(scroller?.clientHeight) ?? 0;
  const top = finiteNumber(scrollerRect?.top);
  const targetTop = finiteNumber(targetRect?.top);
  const targetHeight = finiteNumber(targetRect?.height);
  const targetWasVisible = height > 0 && top != null && targetTop != null && targetHeight != null
    && targetTop >= top && targetTop + targetHeight <= top + height;
  nestingPartScrollSnapshots.set(view, {
    scrollTop: Math.max(0, finiteNumber(scroller?.scrollTop) ?? 0),
    partId,
    reveal: Boolean(partId && (requestedPartId
      ? !isInVerticalComfortZone(scroller, target)
      : previousPartId !== partId && !targetWasVisible)),
    groups: new Map(Array.from(scroller?.querySelectorAll?.("[data-tube-designer-nesting-profile-id]") ?? [])
      .map((group) => [String(group.dataset?.tubeDesignerNestingProfileId ?? ""), group.open])),
  });
}

// Called synchronously after the replacement DOM is mounted, on every render.
// Never animate from the replacement list's initial scrollTop=0 or wait a frame.
export function restoreNestingPartListScroll(context, view, mount = context?.mount) {
  const snapshot = nestingPartScrollSnapshots.get(view);
  nestingPartScrollSnapshots.delete(view);
  if (!snapshot || view?.activeAreaId !== "nesting") return;
  const scroller = mount?.querySelector?.(".tube-designer-cutting-group-list");
  if (!scroller) return;
  for (const group of scroller.querySelectorAll?.("[data-tube-designer-nesting-profile-id]") ?? []) {
    const id = String(group.dataset?.tubeDesignerNestingProfileId ?? "");
    if (snapshot.groups.has(id)) group.open = snapshot.groups.get(id);
  }
  const target = findNestingPartRow(scroller, snapshot.partId);
  if (snapshot.reveal && target) {
    const group = target.closest?.("details.tube-designer-cutting-group");
    if (group) group.open = true;
  }
  const height = Math.max(0, finiteNumber(scroller.clientHeight) ?? 0);
  const maximum = Math.max(0, (finiteNumber(scroller.scrollHeight) ?? 0) - height);
  let scrollTop = snapshot.scrollTop;
  if (snapshot.reveal && target && height > 0) {
    const scrollerRect = scroller.getBoundingClientRect?.();
    const targetRect = target.getBoundingClientRect?.();
    const top = finiteNumber(scrollerRect?.top);
    const targetTop = finiteNumber(targetRect?.top);
    const targetHeight = finiteNumber(targetRect?.height);
    if (top != null && targetTop != null && targetHeight != null) {
      scrollTop = targetTop - top + (finiteNumber(scroller.scrollTop) ?? 0)
        + targetHeight / 2 - height / 2;
    }
  }
  scroller.scrollTop = Math.max(0, Math.min(maximum, scrollTop));
}

function findNestingPartRow(scroller, partId) {
  if (!partId) return null;
  return Array.from(scroller?.querySelectorAll?.("[data-tube-designer-nesting-part-row]") ?? [])
    .find((row) => String(row?.dataset?.tubeDesignerPartId ?? "") === partId) ?? null;
}

function scheduleActiveNestingPlanCentering(context, activePlanId) {
  if (!activePlanId) return;
  const currentScroller = context?.mount?.querySelector?.(".tube-designer-nesting-result-table") ?? null;
  const previousScrollTop = Math.max(0, finiteNumber(currentScroller?.scrollTop) ?? 0);
  const previousActiveRow = currentScroller?.querySelector?.("[data-tube-designer-nesting-plan-row].active") ?? null;
  const previousActivePlanId = String(previousActiveRow?.dataset?.tubeDesignerNestingPlanId ?? "");
  const targetRow = findNestingPlanRow(currentScroller, activePlanId);
  const targetWasComfortablyVisible = isInVerticalComfortZone(currentScroller, targetRow);
  const shouldCenter = !currentScroller
    || previousActivePlanId !== activePlanId && !targetWasComfortablyVisible;

  queueMicrotask(() => {
    const scroller = context?.mount?.querySelector?.(".tube-designer-nesting-result-table") ?? null;
    const target = findNestingPlanRow(scroller, activePlanId);
    if (!scroller || !target) return;
    if (previousActivePlanId !== activePlanId) {
      const group = target.closest?.("details.tube-designer-nesting-result-group");
      if (group) group.open = true;
    }
    const viewportHeight = Math.max(0, finiteNumber(scroller.clientHeight) ?? 0);
    const maximumScrollTop = Math.max(0, (finiteNumber(scroller.scrollHeight) ?? 0) - viewportHeight);
    let nextScrollTop = previousScrollTop;
    if (shouldCenter && viewportHeight > 0) {
      const scrollerRect = scroller.getBoundingClientRect?.();
      const targetRect = target.getBoundingClientRect?.();
      if (Number.isFinite(scrollerRect?.top) && Number.isFinite(targetRect?.top)
        && Number.isFinite(targetRect?.height)) {
        const targetCenterInContent = Number(targetRect.top) - Number(scrollerRect.top)
          + (finiteNumber(scroller.scrollTop) ?? 0) + Number(targetRect.height) / 2;
        nextScrollTop = targetCenterInContent - viewportHeight / 2;
      }
    }
    // Apply the preserved or centered position exactly once before the browser
    // paints the replacement DOM. Smooth scrolling here creates a visible
    // top-then-search jump after every project render.
    scroller.scrollTop = Math.max(0, Math.min(maximumScrollTop, nextScrollTop));
  });
}

function findNestingPlanRow(scroller, planId) {
  return Array.from(scroller?.querySelectorAll?.("[data-tube-designer-nesting-plan-row]") ?? [])
    .find((row) => String(row?.dataset?.tubeDesignerNestingPlanId ?? "") === String(planId)) ?? null;
}

function isInVerticalComfortZone(scroller, row) {
  const scrollerRect = scroller?.getBoundingClientRect?.();
  const rowRect = row?.getBoundingClientRect?.();
  const height = finiteNumber(scrollerRect?.height) ?? finiteNumber(scroller?.clientHeight) ?? 0;
  if (!(height > 0) || !Number.isFinite(scrollerRect?.top)
    || !Number.isFinite(rowRect?.top) || !Number.isFinite(rowRect?.height)) return false;
  const rowCenter = Number(rowRect.top) + Number(rowRect.height) / 2;
  const comfortableTop = Number(scrollerRect.top) + height * 0.3;
  const comfortableBottom = Number(scrollerRect.top) + height * 0.7;
  return rowCenter >= comfortableTop && rowCenter <= comfortableBottom;
}

function nestingPlanMaterial(plan) {
  return String(plan?.material ?? plan?.materialGrade ?? "材料未指定");
}

function nestingPlanProfile(plan) {
  return String(plan?.profile ?? plan?.profileName ?? plan?.stockTypeId ?? "截面未指定");
}

function nestingPlanUtilization(plan, stockLength = 0, usedLength = 0) {
  const utilization = finiteNumber(plan?.utilization)
    ?? (stockLength > 0 ? Math.max(0, Math.min(1, usedLength / stockLength)) : 0);
  return utilization <= 1 ? utilization * 100 : utilization;
}

function renderNestingSceneSummary(plan) {
  const stockLength = finiteNumber(plan?.stockLength ?? plan?.length) ?? 0;
  const usedLength = finiteNumber(plan?.usedLength ?? plan?.consumedLength) ?? 0;
  const remainingLength = finiteNumber(plan?.remainingLength ?? plan?.remnantLength)
    ?? Math.max(0, stockLength - usedLength);
  return `<div class="tube-designer-cutting-scene-summary">
    <span><small>原材</small><strong>${formatNumber(stockLength)} mm</strong></span>
    <span><small>已用</small><strong>${formatNumber(usedLength)} mm</strong></span>
    <span><small>余料</small><strong>${formatNumber(remainingLength)} mm</strong></span>
    <span><small>利用率</small><strong>${formatNumber(nestingPlanUtilization(plan, stockLength, usedLength))}%</strong></span>
  </div>`;
}

export async function handlePartsAreaAction(context, view, action, target, ops) {
  if (action.startsWith("tube-designer-punch-") && view.pending) return { handled: true };
  if(action==="tube-designer-punch-parameters-open") {
    const revision=view.tubeDesignerPunchWizard?.revision;
    openPunchParameters(view,target?.dataset?.tubeDesignerPunchIndex,target?.dataset?.tubeDesignerPunchEnd,target?.dataset?.tubeDesignerPunchEditorMode);
    if(revision!==view.tubeDesignerPunchWizard?.revision)await previewActivePunch(context,view,ops,{includeDraft:view.tubeDesignerPunchWizard?.parameterEditor?.index==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  const arrayAction=handlePunchArrayGroupAction(view,action.startsWith("tube-designer-punch-")?action.slice("tube-designer-punch-".length):"",target);
  if(arrayAction.handled) {
    if(arrayAction.changed)await previewActivePunch(context,view,ops,{includeDraft:arrayAction.includeDraft,quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(action==="tube-designer-punch-preview-mode") {
    if(view.tubeDesignerPunchWizard)view.tubeDesignerPunchWizard.previewMode="tools";
    ops.renderProject(context,view);return {handled:true};
  }
  if(action==="tube-designer-punch-summary-select") {
    const s=view.tubeDesignerPunchWizard,index=Number(target?.dataset?.tubeDesignerPunchIndex);
    if(s?.features?.[index]){s.selectedFeatureId=s.features[index].id;s.scrollToFeatureIndex=index;}
    ops.renderProject(context,view);return {handled:true};
  }
  if(action==="tube-designer-punch-selection-change") {
    setPunchFeatureSelected(view,target?.dataset?.tubeDesignerPunchIndex,target?.checked===true);
    ops.renderProject(context,view);return {handled:true};
  }
  if(action==="tube-designer-punch-remove-selected") {
    if(removeSelectedPunchWizardFeatures(view))await previewActivePunch(context,view,ops,{quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(action==="tube-designer-punch-copy-selected") {
    const s=view.tubeDesignerPunchWizard;
    const index=s?.features?.findIndex(item=>item.id===s.selectedFeatureId)??-1;
    const changed=Number.isInteger(index)&&index>=0&&editPunchWizardFeature(view,"copy",index);
    if(changed)await previewActivePunch(context,view,ops,{includeDraft:true,quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(["tube-designer-punch-parameters-apply","tube-designer-punch-parameters-cancel","tube-designer-punch-parameters-close","tube-designer-punch-parameters-preview"].includes(action)) {
    const s=view.tubeDesignerPunchWizard,part=listNestingParts(view.scene?.tubeDesigner??{}).find(item=>String(item.entityId)===String(s?.partId));
    const includeDraft=s?.parameterEditor?.index==="draft";
    // Parameter windows preview continuously; their close button commits the
    // live values. Keep the legacy preview/apply actions for old integrations,
    // but do not roll back a visible close.
    const commit=!action.endsWith("-preview"), closeWithoutValidation=action.endsWith("-parameters-cancel")||action.endsWith("-parameters-close");
    if(action.endsWith("-preview")||closePunchParameters(view,commit,part,closeWithoutValidation?false:true))await previewActivePunch(context,view,ops,{includeDraft,quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if (["edit","copy","toggle","undo","redo","remove-end"].some(name => action === "tube-designer-punch-"+name)) {
    const suffix = action.slice("tube-designer-punch-".length);
    const changed = editPunchWizardFeature(view,suffix,target?.dataset?.tubeDesignerPunchIndex);
    if (changed) await previewActivePunch(context, view, ops, {
      includeDraft: suffix === "edit" || suffix === "copy", quiet: true,
    });
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(action === "tube-designer-punch-preview") {
    await previewActivePunch(context, view, ops, { includeDraft: true });
    return {handled:true};
  }
  if(action==="tube-designer-punch-record-kind-change") {
    const selected=String(target?.value??"");
    const changed=selected==="dxf"
      ? await selectPunchProfileSource(context,view,{dataset:target?.dataset??{},value:"__dxf__"},ops)
      : changePunchRecordKind(view,target);
    if(changed)await previewActivePunch(context,view,ops,{includeDraft:String(target?.dataset?.tubeDesignerPunchIndex??"draft")==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(action==="tube-designer-punch-profile-select") {
    if(await selectPunchProfileSource(context,view,target,ops))await previewActivePunch(context,view,ops,{includeDraft:String(target?.dataset?.tubeDesignerPunchIndex??"draft")==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(action==="tube-designer-punch-profile-parameter") {
    if(await updatePunchProfileParameter(context,view,target,ops))await previewActivePunch(context,view,ops,{includeDraft:String(target?.dataset?.tubeDesignerPunchIndex??"draft")==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if (view.tubeDesignerNestingContextMenu) view.tubeDesignerNestingContextMenu = null;
  if (action === "tube-designer-part-open-sketch") {
    if (view.pending) return { handled: true };
    const partId = String(target?.dataset?.tubeDesignerPartId
      ?? view.tubeDesignerActiveNestingPartId
      ?? view.tubeDesignerActivePartId ?? "").trim();
    const part = listNestingParts(view.scene?.tubeDesigner ?? {})
      .find((item) => String(item.entityId) === partId);
    if (!part || !isTubeNestingPart(part)) {
      throw new Error("请选择一个管材下料零件后再进入二维编辑。");
    }
    const measurementKey = partMeasurementKey(part);
    let report = view.tubeDesignerPartMeasurementState?.key === measurementKey
      ? view.tubeDesignerPartMeasurementState?.report : null;
    report ??= view.tubeDesignerPartMeasurementCache?.get?.(measurementKey) ?? null;
    let unfolding = view.tubeDesignerPartUnfoldingCache?.get?.(measurementKey) ?? null;
    if (!unfolding && report?.unfolding?.available === true) unfolding = report.unfolding;
    view.pending = true;
    view.error = "";
    if (!report || !unfolding) {
      view.progress = {
        title: "正在打开零件二维草图",
        detail: !report
          ? "正在从最终零件几何提取侧面区域、棱线和孔位"
          : "正在从最终 BRep 生成管材侧面展开",
        stage: !report ? "生成侧视底图" : "生成展开底图",
        mode: "Sketch",
      };
    }
    ops.renderProject(context, view);
    try {
      if (!report) {
        await waitForPartsAreaPaint();
        const response = await context.sceneProxy?.invoke("TubeDesigner.MeasurePartGeometry", {
          partEntityId: partId,
          resourceVersion: Number(part.manufacturingGeometryResourceVersion ?? 0),
        }, { timeoutMs: 120000 });
        if (!response?.available) throw new Error(response?.message ?? "无法生成零件侧视底图。");
        report = buildAutomaticDimensionReport({ geometryMeasurement: response });
        view.tubeDesignerPartMeasurementCache ??= new Map();
        view.tubeDesignerPartMeasurementCache.set(measurementKey, report);
        view.tubeDesignerPartMeasurementState = { key: measurementKey, status: "ready", report };
      }
      if (!unfolding && typeof context.sceneProxy?.invoke === "function") {
        await waitForPartsAreaPaint();
        const unfoldingResponse = await context.sceneProxy.invoke("TubeDesigner.UnfoldPartBRep", {
          partEntityId: partId,
          resourceVersion: Number(part.manufacturingGeometryResourceVersion ?? 0),
        }, { timeoutMs: 120000 });
        if (!unfoldingResponse?.available) {
          throw new Error(unfoldingResponse?.diagnostic
            ?? unfoldingResponse?.message
            ?? "无法从最终 BRep 生成管材侧面展开。");
        }
        if (String(unfoldingResponse.resourceId ?? "")
            !== String(part.manufacturingGeometryResourceId ?? "")
          || Number(unfoldingResponse.resourceVersion ?? 0)
            !== Number(part.manufacturingGeometryResourceVersion ?? 0)) {
          throw new Error("展开结果与当前最终几何版本不一致，请重新打开零件。");
        }
        unfolding = unfoldingResponse;
        view.tubeDesignerPartUnfoldingCache ??= new Map();
        view.tubeDesignerPartUnfoldingCache.set(measurementKey, unfolding);
      }
      if (unfolding && report && report.unfolding !== unfolding) {
        report = Object.freeze({ ...report, unfolding });
        view.tubeDesignerPartMeasurementCache ??= new Map();
        view.tubeDesignerPartMeasurementCache.set(measurementKey, report);
        view.tubeDesignerPartMeasurementState = { key: measurementKey, status: "ready", report };
      }
      beginPartSideSketch(view, part, report);
      view.tubeDesignerSketchDialogOpen = true;
    } catch (error) {
      view.error = error?.message ?? String(error);
      throw error;
    } finally {
      view.pending = false;
      view.progress = null;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-punch-open") {
    if (view.pending) return { handled: true };
    const partId = String(target?.dataset?.tubeDesignerPartId ?? view.tubeDesignerActivePartId ?? "").trim();
    const part = listNestingParts(view.scene?.tubeDesigner ?? {})
      .find(item => String(item.entityId) === partId);
    if (!part || !isTubeNestingPart(part) || !part.independentNesting) {
      throw new Error("请先将管材零件加入下料区，再打开冲孔向导。");
    }
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerNestingSelectionKind = "part";
    view.tubeDesignerPunchWizard = createPunchWizardState(part);
    view.tubeDesignerPunchWizard.initialToolsPreviewPart=part;
    initializePunchRecordSource(view);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-punch-cancel") {
    if (!view.pending) {
      view.tubeDesignerPunchWizard = null;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-punch-field-change") {
    if (updatePunchWizardField(view, target)) {
      const row = target?.dataset?.tubeDesignerPunchIndex;
      await previewActivePunch(context, view, ops, {
        includeDraft: row === undefined || row === "" || row === "draft", quiet: true,
      });
    } else ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-punch-add") {
    const state = view.tubeDesignerPunchWizard;
    const part = listNestingParts(view.scene?.tubeDesigner ?? {})
      .find(item => String(item.entityId) === String(state?.partId ?? ""));
    if (part && addPunchWizardFeature(view, part)) {
      await previewActivePunch(context, view, ops, { quiet: true });
    } else ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-punch-remove") {
    if (removePunchWizardFeature(view, target?.dataset?.tubeDesignerPunchIndex)) {
      await previewActivePunch(context, view, ops, { quiet: true });
    } else ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-punch-apply") {
    if (view.pending) return { handled: true };
    const state = view.tubeDesignerPunchWizard;
    const partId = String(target?.dataset?.tubeDesignerPartId ?? state?.partId ?? "").trim();
    const part = listNestingParts(view.scene?.tubeDesigner ?? {})
      .find(item => String(item.entityId) === partId);
    if (!part || !state || String(state.partId) !== partId) throw new Error("当前冲孔零件已不存在，请重新选择。");
    const validationError = validatePunchWizard(view, part);
    if (validationError) {
      state.error = validationError;
      ops.renderProject(context, view);
      return { handled: true };
    }
    if (!context.sceneProxy?.invoke) throw new Error("当前项目未连接，无法保存冲孔结果。");
    const invocationOptions = beginPunchOperation(context,view,"正在应用冲孔与端部切形");
    view.pending = true;
    state.error = "";
    ops.renderProject(context, view);
    await waitForPartsAreaPaint();
    try {
      const response = await context.sceneProxy.invoke("TubeDesigner.ApplyPunchWizard", {
        ...getPunchWizardPayload(view),
        partEntityId: partId,
      }, invocationOptions);
      if (!response?.tubeDesigner) throw new Error("冲孔结果未能保存。");
      view.scene.tubeDesigner = response.tubeDesigner;
      restoreSavedNestingTask(view, context);
      view.tubeDesignerPunchWizard = null;
      view.tubeDesignerActivePartId = partId;
      view.tubeDesignerActiveNestingPartId = partId;
      view.tubeDesignerNestingSelectionKind = "part";
      view.tubeDesignerActiveNestingPlacementId = "";
      view.tubeDesignerPartViewportKey = "";
      view.tubeDesignerPartMeasurementState = null;
      view.viewport?.setCustomVisibleEntityIds?.([]);
      await context.actions?.refreshActiveSceneState?.();
    } catch (error) {
      state.error = error?.message ?? String(error);
      view.error = state.error;
      throw error;
    } finally {
      finishPunchOperation(view);
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-parts-select-part") {
    const partId = String(target?.dataset?.tubeDesignerPartId ?? "").trim();
    if (!partId || view.pending) return { handled: true };
    view.tubeDesignerActivePartId = partId;
    if (view.activeAreaId === "nesting") {
      const plans = listNestingPlans(view);
      const preferred = resolveActiveNestingPlan(view, plans);
      const plan = [preferred, ...plans].filter(Boolean).find((candidate) => nestingPartLocations(candidate, partId).length);
      const location = nestingPartLocations(plan, partId)[0];
      if (plan && location) {
        view.tubeDesignerActiveNestingPlanId = String(plan.id ?? plan.stockId ?? "");
      }
      view.tubeDesignerNestingSelectionKind = "part";
      view.tubeDesignerActiveNestingPartId = partId;
      view.tubeDesignerActiveNestingPlacementId = "";
    }
    view.tubeDesignerPartMeasurementState = null;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-nesting-view-part-locations") {
    const partId = String(target?.dataset?.tubeDesignerPartId ?? "").trim();
    if (!partId || view.pending) return { handled: true };
    const plans = listNestingPlans(view);
    const locations = nestingPartLocationsAcrossPlans(plans, partId);
    if (!locations.length) return { handled: true };
    const activePlanId = String(view.tubeDesignerActiveNestingPlanId ?? "");
    const location = locations.find(({ plan }) => String(plan?.id ?? plan?.stockId ?? "") === activePlanId)
      ?? locations[0];
    const planId = String(location.plan?.id ?? location.plan?.stockId ?? "");
    if (!planId) return { handled: true };
    nestingPartCenterRequests.set(view, partId);
    view.tubeDesignerActiveNestingPlanId = planId;
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerActiveNestingPlacementId = nestingPlacementId(location.placement, location.index);
    view.tubeDesignerNestingSelectionKind = "plan";
    view.viewport?.setDimensionAnnotations?.([]);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-parts-toggle-part") {
    const parts = listAreaParts(view);
    toggleSelectedParts(view, [target?.dataset?.tubeDesignerPartId], Boolean(target?.checked), parts);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-parts-toggle-group") {
    const parts = listAreaParts(view);
    const ids = String(target?.dataset?.tubeDesignerPartIds ?? "").split(/\s+/).filter(Boolean);
    toggleSelectedParts(view, ids, Boolean(target?.checked), parts);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-parts-search") {
    view.tubeDesignerPartSearchText = String(target?.value ?? "");
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-parts-filter") {
    view.tubeDesignerPartFilter = String(target?.dataset?.tubeDesignerPartFilter ?? "all");
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-parts-clear-filters") {
    view.tubeDesignerPartSearchText = "";
    view.tubeDesignerPartFilter = "all";
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-part-edit-save") {
    if (view.pending) return { handled: true };
    const partId = String(target?.dataset?.tubeDesignerPartId ?? "").trim();
    const editor = target?.closest?.("[data-tube-designer-part-editor]");
    const name = editor?.querySelector?.('[data-tube-designer-part-field="name"]')?.value ?? "";
    const material = editor?.querySelector?.('[data-tube-designer-part-field="material"]')?.value;
    const quantity = editor?.querySelector?.('[data-tube-designer-part-field="quantity"]')?.value ?? "";
    if (!partId || !editor) return { handled: true };
    if (!context.sceneProxy?.invoke) throw new Error("当前项目未连接，无法修改零件。");
    view.pending = true;
    view.error = "";
    ops.renderProject(context, view);
    try {
      const response = await context.sceneProxy.invoke("TubeDesigner.UpdateManufacturingPart", {
        partEntityId: partId,
        name: String(name),
        ...(material !== undefined ? { material: String(material) } : {}),
        quantity: Number(quantity),
      }, { timeoutMs: 120000 });
      if (!response?.tubeDesigner) throw new Error("零件修改未能保存。");
      view.scene.tubeDesigner = response.tubeDesigner;
      restoreSavedNestingTask(view, context);
      view.tubeDesignerPartMeasurementState = null;
      await context.actions?.refreshActiveSceneState?.();
    } catch (error) {
      view.error = error?.message ?? String(error);
      throw error;
    } finally {
      view.pending = false;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-part-edit-delete") {
    if (view.pending) return { handled: true };
    const partId = String(target?.dataset?.tubeDesignerPartId ?? "").trim();
    if (!partId || !context.sceneProxy?.invoke) return { handled: true };
    const parts = listAreaParts(view);
    const part = parts.find((item) => String(item.entityId) === partId);
    if (!part) return { handled: true };
    view.pending = true;
    view.error = "";
    ops.renderProject(context, view);
    try {
      const independent = Boolean(part.nestingEntry ?? part.independentNesting);
      const response = await context.sceneProxy.invoke(
        independent ? "TubeDesigner.DeleteNestingParts" : "TubeDesigner.DeleteManufacturingPart",
        independent ? { partEntityIds: [partId] } : { partEntityId: partId },
        { timeoutMs: 120000 },
      );
      if (!response?.tubeDesigner) throw new Error("零件删除未能完成。");
      view.scene.tubeDesigner = response.tubeDesigner;
      restoreSavedNestingTask(view, context);
      view.tubeDesignerActivePartId = "";
      view.tubeDesignerActiveNestingPartId = "";
      view.tubeDesignerActiveNestingPlacementId = "";
      view.tubeDesignerPartMeasurementState = null;
      view.viewport?.setCustomVisibleEntityIds?.([]);
      await context.actions?.refreshActiveSceneState?.();
    } catch (error) {
      view.error = error?.message ?? String(error);
      throw error;
    } finally {
      view.pending = false;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-parts-select-filtered" || action === "tube-designer-parts-clear-filtered") {
    const parts = listAreaParts(view);
    const selectedIds = selectedPartIds(view, parts);
    const visibleParts = filterManufacturingParts(parts, {
      query: view.tubeDesignerPartSearchText,
      filter: view.tubeDesignerPartFilter,
      selectedIds,
    });
    toggleSelectedParts(
      view,
      visibleParts.map((part) => part.entityId),
      action === "tube-designer-parts-select-filtered",
      parts,
    );
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-parts-fit-view") {
    view.viewport?.fitViewToViewport?.(1.16);
    return { handled: true };
  }
  if (action === "tube-designer-parts-default-view") {
    applyPartPresentation(view);
    view.viewport?.fitViewToViewport?.(1.16);
    return { handled: true };
  }
  if (action === "tube-designer-parts-toggle-dimensions") {
    view.tubeDesignerPartDimensionsVisible = view.tubeDesignerPartDimensionsVisible === false;
    const report = view.tubeDesignerPartMeasurementState?.report;
    view.viewport?.setDimensionAnnotations?.(
      view.tubeDesignerPartDimensionsVisible === false ? [] : report?.annotations ?? [],
    );
    synchronizeMeasurementDom(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-parts-open-nesting") {
    if (view.pending) return { handled: true };
    const selected = Array.isArray(view.tubeDesignerSelectedPartIds) ? new Set(view.tubeDesignerSelectedPartIds.map(String)) : null;
    const ids = listManufacturingParts(view.scene?.tubeDesigner ?? {})
      .filter(part => isTubeNestingPart(part) && (!selected || selected.has(String(part.entityId))))
      .map(part => String(part.entityId));
    if (!ids.length) throw new Error("请先选择要加入下料的管材零件。");
    if (!context.sceneProxy?.invoke) throw new Error("当前项目未连接，无法加入下料任务。");
    const operationId = Number(view.tubeDesignerOperationSequence ?? 0) + 1;
    view.tubeDesignerOperationSequence = operationId;
    view.tubeDesignerOperation = {
      id: operationId,
      kind: "stage-nesting",
      title: "正在准备下料",
      phase: "staging",
      phaseLabel: "保存下料零件",
      message: `正在关联 ${ids.length} 种零件到下料清单…`,
    };
    view.pending = true;
    view.error = "";
    ops.renderProject(context, view);
    await waitForPartsAreaPaint();
    try {
      const response = await context.sceneProxy.invoke("TubeDesigner.StageNestingParts", {partEntityIds: ids}, {timeoutMs: 180000});
      if (!response?.tubeDesigner) throw new Error("下料零件未能关联。");
      if (view.tubeDesignerOperation?.id === operationId) {
        Object.assign(view.tubeDesignerOperation, {
          phase: "refreshing",
          phaseLabel: "刷新下料清单",
          message: "零件已关联，正在刷新下料页面…",
        });
      }
      view.scene.tubeDesigner = response.tubeDesigner;
      restoreSavedNestingTask(view, context);
      view.tubeDesignerPartDimensionsVisible = false;
      view.tubeDesignerNestingSelectionKind = "part";
      await context.actions?.refreshActiveSceneState?.();
      await context.actions?.selectRibbonTab?.("nesting");
      // selectRibbonTab() updates the app-shell state, but the action context
      // is an earlier snapshot. Keep the final render from switching the body
      // back to the product area after the ribbon has moved to 下料.
      context.activeRibbonTabId = "nesting";
      view.activeAreaId = "nesting";
    } catch (error) {
      view.error = error?.message ?? String(error);
      throw error;
    } finally {
      if (view.tubeDesignerOperation?.id === operationId) view.tubeDesignerOperation = null;
      view.pending = false;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-delete-nesting-parts") {
    if (view.pending) return { handled: true };
    const parts = listNestingParts(view.scene?.tubeDesigner ?? {});
    const ids = [...selectedPartIds(view, parts)];
    if (!ids.length) return { handled: true };
    view.pending = true;
    ops.renderProject(context, view);
    try {
      const response = await context.sceneProxy.invoke("TubeDesigner.DeleteNestingParts", { partEntityIds: ids });
      if (!response?.tubeDesigner) throw new Error("下料零件未能删除。");
      view.scene.tubeDesigner = response.tubeDesigner;
      restoreSavedNestingTask(view, context);
      view.tubeDesignerActiveNestingPartId = "";
      view.tubeDesignerActivePartId = "";
      view.tubeDesignerPartViewportKey = "";
      cancelNestingPlanHydration(view);
      view.viewport?.setCustomVisibleEntityIds?.([]);
      await context.actions?.refreshActiveSceneState?.();
    } finally {
      view.pending = false;
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-select-nesting-plan") {
    const planId = String(target?.dataset?.tubeDesignerNestingPlanId ?? "");
    const plan = listNestingPlans(view).find((item, index) => String(item?.id ?? item?.stockId ?? `stock-${index + 1}`) === planId);
    if (!plan || view.pending) return { handled: true };
    // Badge 1 is placements[0]. This only moves the list; selection stays on
    // the complete stock, not on the first placement or a particular part.
    nestingPartCenterRequests.set(view, String(plan.placements?.[0]?.partId ?? ""));
    view.tubeDesignerActiveNestingPlanId = planId;
    view.tubeDesignerNestingSelectionKind = "plan";
    view.tubeDesignerActiveNestingPartId = "";
    view.tubeDesignerActiveNestingPlacementId = "";
    view.viewport?.setDimensionAnnotations?.([]);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-select-nesting-placement") {
    const planId = String(target?.dataset?.tubeDesignerNestingPlanId ?? "");
    const partId = String(target?.dataset?.tubeDesignerPartId ?? "");
    const placementId = String(target?.dataset?.tubeDesignerPlacementId ?? "");
    if (!planId || !partId || !placementId || view.pending) return { handled: true };
    view.tubeDesignerActiveNestingPlanId = planId;
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerActiveNestingPlacementId = placementId;
    view.tubeDesignerNestingSelectionKind = "plan";
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (action === "tube-designer-toggle-nesting-plan-lock") {
    const planId = String(target?.dataset?.tubeDesignerNestingPlanId ?? "");
    const plans = listNestingPlans(view);
    if (!planId || !plans.some((plan) => String(plan.id) === planId) || view.pending) return { handled: true };
    const locked = new Set((view.tubeDesignerLockedNestingPlanIds ?? []).map(String));
    if (locked.has(planId)) locked.delete(planId);
    else locked.add(planId);
    view.tubeDesignerLockedNestingPlanIds = [...locked];
    ops.renderProject(context, view);
    return { handled: true };
  }
  return { handled: false };
}

export function clearPartsViewportAnnotations(view) {
  if (view.activeAreaId === "parts" || view.activeAreaId === "nesting") return;
  cancelNestingPlanHydration(view);
  hydrationTokens.set(view, {});
  cancelPartViewportLoad(view);
  setPartProgress(null, view, null);
  view.viewport?.setDimensionAnnotations?.([]);
  view.tubeDesignerPartViewportKey = "";
}

function renderStockGroup(group, activePart, selectedIds, view) {
  const partIds = group.parts.map((part) => String(part.entityId));
  const selectedCount = partIds.filter((id) => selectedIds.has(id)).length;
  const checked = partIds.length > 0 && selectedCount === partIds.length;
  const openGroups = new Set(view.tubeDesignerOpenStockGroupIds ?? []);
  const explicitlyManaged = Array.isArray(view.tubeDesignerOpenStockGroupIds);
  const open = explicitlyManaged ? openGroups.has(group.id) : true;
  return `<details class="tube-designer-stock-group" ${open ? "open" : ""}>
    <summary>
      <input type="checkbox" data-cam-action="tube-designer-parts-toggle-group" data-tube-designer-part-ids="${escapeAttribute(partIds.join(" "))}" ${checked ? "checked" : ""} ${selectedCount > 0 && !checked ? "data-tube-designer-indeterminate=\"true\"" : ""} aria-label="选择该材料截面组的全部零件" />
      <span><strong>${escapeText(group.material)}</strong><small>${escapeText(group.profile)}</small></span>
      <span class="tube-designer-stock-group-totals"><strong>${selectedCount} / ${group.parts.length} 种</strong><small>${group.quantity} 件${group.parts.some(isTubeNestingPart) ? ` · 净长 ${formatCompactLength(group.totalLength)}` : " · 另行处理"}</small></span>
    </summary>
    <div class="tube-designer-stock-part-list">${group.parts.map((part) => `
      <div class="tube-designer-stock-part ${String(activePart?.entityId) === String(part.entityId) ? "selected" : ""}">
        <input type="checkbox" data-cam-action="tube-designer-parts-toggle-part" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" ${selectedIds.has(String(part.entityId)) ? "checked" : ""} aria-label="选择 ${escapeAttribute(partDisplayName(part))}" />
        <button data-cam-action="tube-designer-parts-select-part" data-tube-designer-part-id="${escapeAttribute(part.entityId)}">
          <canvas width="84" height="50" data-tube-designer-part-thumbnail data-tube-preview-url="${escapeAttribute(part.thumbnailGeometryResourceId)}" data-tube-preview-version="${escapeAttribute(part.thumbnailGeometryResourceVersion)}" aria-label="${escapeAttribute(partDisplayName(part))} 三维示意图"></canvas>
          <span class="tube-designer-stock-part-identity">
            <span><strong>${escapeText(partDisplayName(part))}</strong><mark class="tube-designer-process-badge ${escapeAttribute(partProcessKind(part))}">${escapeText(partProcessLabel(part))}</mark></span>
            <small>${escapeText(part.partNumber || "未编号")} · ${escapeText(part.productName || part.productCode || "未命名产品")}</small>
          </span>
          <span class="tube-designer-stock-part-measure"><strong>${isSheetPart(part) || isComponentPart(part) ? escapeText(manufacturingPartKindLabel(part)) : `${formatNumber(part.length)} mm`}</strong><small>× ${partQuantity(part)} 件</small></span>
        </button>
      </div>`).join("")}</div>
  </details>`;
}

function renderFilterButton(id, label, count, activeFilter) {
  const selected = id === activeFilter;
  return `<button data-cam-action="tube-designer-parts-filter" data-tube-designer-part-filter="${escapeAttribute(id)}" aria-pressed="${selected ? "true" : "false"}">${escapeText(label)} <b>${count}</b></button>`;
}

function renderProductionUpgrade(moduleName) {
  return `<div class="tube-designer-panel tube-designer-production-upgrade">
    <span class="tube-designer-production-lock" aria-hidden="true">◇</span>
    <strong>${escapeText(moduleName)}是高阶生产功能</strong>
    <span>额外授权后可跨产品归集制造零件，按材料与截面分组，并衔接内部排样与装配体 STEP。</span>
    <small>基础版仍可在“产品”栏逐个实例打开零件清单、复尺并导出给第三方 CAM。</small>
  </div>`;
}

function renderLockedViewport(moduleName) {
  return `<div class="tube-designer-nesting-empty tube-designer-production-locked-viewport">
    <strong>${escapeText(moduleName)}模块未授权</strong>
    <span>获得高阶生产授权后即可使用。</span>
  </div>`;
}

function toggleSelectedParts(view, ids, checked, parts = []) {
  const selectable = view.activeAreaId === "nesting" ? parts.filter(isTubeNestingPart) : parts;
  const validIds = new Set(selectable.map((part) => String(part.entityId)));
  const selected = selectedPartIds(view, selectable);
  for (const id of ids.map((value) => String(value ?? "").trim()).filter(Boolean)) {
    if (!validIds.has(id)) continue;
    if (checked) selected.add(id);
    else selected.delete(id);
  }
  const key = view.activeAreaId === "nesting" || parts.some(part => part.independentNesting)
    ? nestingSelectionKey(view) : "tubeDesignerSelectedPartIds";
  view[key] = [...selected];
}

function selectedPartIds(view, parts = []) {
  const key = view.activeAreaId === "nesting" || parts.some(part => part.independentNesting)
    ? nestingSelectionKey(view) : "tubeDesignerSelectedPartIds";
  const current = Array.isArray(view[key])
    ? view[key]
    : parts.map((part) => part.entityId);
  const validIds = new Set(parts.map((part) => String(part.entityId)));
  return new Set(current.map((id) => String(id)).filter((id) => validIds.has(id)));
}

function cancelPartViewportLoad(view) {
  const pending = partViewportLoads.get(view);
  partViewportLoads.delete(view);
  if (!pending) return;
  view.tubeDesignerPartViewportKey = "";
  partViewportOwners.delete(view);
  // Cancel within the viewport as well: a stale apply can otherwise install its
  // mesh before the caller gets a chance to reject the asynchronous receipt.
  try {
    void Promise.resolve(pending.viewport.applyViewSnapshot({
      revision: `tube-designer-part:cancel:${++partViewportCancellationSequence}`, rows: [],
    }, emptyPartResources)).catch(() => {});
  } catch { /* A disposed viewport no longer owns visible content. */ }
}

function partViewportKey(part) {
  return `${part.entityId}@${String(part.thumbnailGeometryResourceId ?? "").trim()}@${Number(part.thumbnailGeometryResourceVersion ?? 0)}`;
}

function setPartProgress(context, view, part, phase) {
  const progress = part && phase ? {
    key: partViewportKey(part), phase, partName: partDisplayName(part),
    label: phase === "geometry" ? "正在加载三维模型…" : "正在复尺并生成标注…",
  } : null;
  view.tubeDesignerPartProgress = progress;
  for (const host of context?.mount?.querySelectorAll?.("[data-tube-designer-part-progress]") ?? []) {
    host.hidden = !progress;
    host.setAttribute("aria-busy", progress ? "true" : "false");
    const label = host.querySelector?.("[data-tube-designer-part-progress-label]");
    if (label) label.textContent = progress?.label ?? "";
    const name = host.querySelector?.("[data-tube-designer-part-progress-name]");
    if (name) name.textContent = progress?.partName ?? "";
    host.querySelector?.('[role="progressbar"]')?.setAttribute("aria-valuetext", progress?.label ?? "已完成");
  }
}

function renderPartProgress(view) {
  const progress = view.tubeDesignerPartProgress;
  return `<div class="tube-designer-part-load-progress" data-tube-designer-part-progress role="status" aria-live="polite" aria-busy="${!!progress}" ${progress ? "" : "hidden"}>
    <strong data-tube-designer-part-progress-label>${escapeText(progress?.label ?? "")}</strong>
    <span data-tube-designer-part-progress-name>${escapeText(progress?.partName ?? "")}</span>
    <div class="tube-designer-export-progress-track is-indeterminate" role="progressbar" aria-label="零件显示进度" aria-valuetext="${escapeAttribute(progress?.label ?? "已完成")}"><i style="width:36%"></i></div>
  </div>`;
}

function schedulePartsAreaHydration(context, view) {
  const token = {};
  hydrationTokens.set(view, token);
  const part = resolveActivePart(view, listAreaParts(view));
  const geometryReady = part && view.tubeDesignerPartViewportKey === partViewportKey(part)
    && partViewportOwners.get(view) === view.viewport;
  const phase = !part ? null : !geometryReady ? "geometry"
    : view.tubeDesignerPartMeasurementCache?.has(partMeasurementKey(part)) ? null : "measurement";
  setPartProgress(context, view, part, phase);
  queueMicrotask(() => {
    if (hydrationTokens.get(view) !== token || !isPartInspectionArea(view)) return;
    void hydratePartsArea(context, view, token);
  });
}

async function hydratePartsArea(context, view, token) {
  const parts = listAreaParts(view);
  const part = resolveActivePart(view, parts);
  if (!part || !view.viewport || hydrationTokens.get(view) !== token) return;
  const viewport = view.viewport;
  const areaId = view.activeAreaId;
  const selectionKind = view.tubeDesignerNestingSelectionKind;
  const isCurrent = () => hydrationTokens.get(view) === token && view.viewport === viewport
    && view.activeAreaId === areaId && isPartInspectionArea(view)
    && (areaId !== "nesting" || view.tubeDesignerNestingSelectionKind === selectionKind);
  const resourceId = String(part.thumbnailGeometryResourceId ?? "").trim();
  const resourceVersion = Number(part.thumbnailGeometryResourceVersion ?? 0);
  const viewportKey = partViewportKey(part);
  try {
    if (!resourceId) {
      cancelPartViewportLoad(view);
      view.tubeDesignerPartViewportKey = "";
      partViewportOwners.delete(view);
      viewport.setDimensionAnnotations?.([]);
      viewport.setVisibleEntityIds?.([]);
      throw new Error("当前零件缺少可显示的三维资源。");
    }
    if (view.tubeDesignerPartViewportKey !== viewportKey || partViewportOwners.get(view) !== viewport) {
      cancelPartViewportLoad(view);
      // Once replacement starts, the previous cached part no longer owns the
      // viewport. Selecting it again must supersede the in-flight replacement.
      view.tubeDesignerPartViewportKey = "";
      partViewportOwners.delete(view);
      viewport.setDimensionAnnotations?.([]);
      setPartProgress(context, view, part, "geometry");
      await waitForPartsAreaPaint();
      if (!isCurrent()) return;
      const revision = `tube-designer-part:${viewportKey}`;
      const pending = { viewport };
      partViewportLoads.set(view, pending);
      let receipt;
      try {
        receipt = await viewport.applyViewSnapshot({
          revision,
          rows: [{
            entityId: String(part.entityId),
            data: {
              geometry: { url: resourceId, version: resourceVersion },
              geometryKind: 1,
              renderClass: 1,
              visible: true,
              selectable: true,
            },
          }],
        }, context.sceneProxy?.resources);
      } finally {
        if (partViewportLoads.get(view) === pending) partViewportLoads.delete(view);
      }
      if (!isCurrent()) return;
      if (!receipt?.applied || !receipt.entityIds?.includes(String(part.entityId))) {
        throw new Error("零件三维资源没有进入视图。");
      }
      view.tubeDesignerPartViewportKey = viewportKey;
      partViewportOwners.set(view, viewport);
      viewport.setStandardView?.("top-front");
      viewport.fitViewToViewport?.(1.16);
    }

    // The shared workbench hides custom-view entities on every mount. Restoring
    // a cached part must restore visibility too, without reloading its geometry.
    if (!isCurrent()) return;
    viewport.setVisibleEntityIds?.([String(part.entityId)]);

    const measurementKey = partMeasurementKey(part);
    view.tubeDesignerPartMeasurementCache ??= new Map();
    const cached = view.tubeDesignerPartMeasurementCache.get(measurementKey);
    if (cached) {
      setPartProgress(context, view, null);
      view.tubeDesignerPartMeasurementState = { key: measurementKey, status: "ready", report: cached };
      applyPartPresentation(view);
      synchronizeMeasurementDom(context, view, part);
      return;
    }
    view.tubeDesignerPartMeasurementState = { key: measurementKey, status: "loading" };
    setPartProgress(context, view, part, "measurement");
    synchronizeMeasurementDom(context, view, part);
    await waitForPartsAreaPaint();
    if (!isCurrent()) return;
    const response = await context.sceneProxy?.invoke("TubeDesigner.MeasurePartGeometry", {
      partEntityId: String(part.entityId),
      resourceVersion: Number(part.manufacturingGeometryResourceVersion ?? 0),
    }, { timeoutMs: 120000 });
    if (!isCurrent()) return;
    if (!response?.available) throw new Error(response?.message ?? "无法从最终零件几何提取自动尺寸。");
    const report = buildAutomaticDimensionReport({ geometryMeasurement: response });
    view.tubeDesignerPartMeasurementCache.set(measurementKey, report);
    view.tubeDesignerPartMeasurementState = { key: measurementKey, status: "ready", report };
    setPartProgress(context, view, null);
    applyPartPresentation(view);
    view.viewport.fitViewToViewport?.(1.16);
    synchronizeMeasurementDom(context, view, part);
  } catch (error) {
    if (!isCurrent()) return;
    setPartProgress(context, view, null);
    view.tubeDesignerPartMeasurementState = {
      key: partMeasurementKey(part),
      status: "error",
      message: error?.message ?? String(error),
    };
    synchronizeMeasurementDom(context, view, part);
  }
}

function isPartInspectionArea(view) {
  return view.activeAreaId === "parts" || view.activeAreaId === "nesting";
}

function applyPartPresentation(view) {
  const report = view.tubeDesignerPartMeasurementState?.report;
  if (Array.isArray(report?.axis)) {
    view.viewport?.setPresentationAxis?.(report.axis, [1, 0, 0]);
  }
  view.viewport?.setStandardView?.("top-front");
  view.viewport?.setDimensionAnnotations?.(
    view.tubeDesignerPartDimensionsVisible === false
      ? []
      : report?.annotations ?? [],
  );
}

function synchronizeMeasurementDom(context, view, suppliedPart = null) {
  const hosts = context.mount?.querySelectorAll
    ? [...context.mount.querySelectorAll("[data-tube-designer-part-measurement]")]
    : [context.mount?.querySelector?.("[data-tube-designer-part-measurement]")].filter(Boolean);
  if (!hosts.length) return;
  const parts = listAreaParts(view);
  const part = suppliedPart ?? resolveActivePart(view, parts);
  if (!part) return;
  for (const host of hosts) {
    host.innerHTML = host.hasAttribute?.("data-tube-designer-nesting-measurement")
      ? renderNestingPartMeasurement(part, view)
      : renderPartMeasurement(part, view.tubeDesignerPartMeasurementState, view);
  }
  const dimensionsVisible = view.tubeDesignerPartDimensionsVisible !== false;
  const buttons = context.mount?.querySelectorAll?.('[data-cam-action="tube-designer-parts-toggle-dimensions"]') ?? [];
  for (const button of buttons) {
    button.setAttribute("aria-pressed", dimensionsVisible ? "true" : "false");
    button.textContent = button.closest?.(".tube-designer-cutting-scene-actions")
      ? (dimensionsVisible ? "隐藏标注" : "显示标注")
      : button.closest?.(".tube-designer-parts-viewport-header")
        ? (dimensionsVisible ? "隐藏尺寸" : "显示尺寸")
        : (dimensionsVisible ? "隐藏" : "显示");
  }
}

function renderPartMeasurement(part, state, view) {
  const report = state?.report;
  const holes = report?.holes ?? [];
  const endProcess = partEndProcess(part);
  const measurementBody = state?.status === "error"
    ? `<div class="tube-designer-dimension-empty">${escapeText(state.message)}</div>`
    : report
      ? isComponentPart(part)
        ? `<div class="tube-designer-dimension-overview tube-designer-part-dimension-overview">${[ ["width", "实体宽 X"], ["depth", "实体深 Y"], ["height", "实体高 Z"] ].map(([key, label]) => `<span><small>${label}</small><strong>${formatNumber(report.bounds?.[key])} mm</strong></span>`).join("")}</div><div class="tube-designer-part-no-holes">配件按完整三维模型管理，不生成管材长度或孔距标尺。</div>`
        : `<div class="tube-designer-dimension-overview tube-designer-part-dimension-overview">
          <span><small>${isSheetPart(part) ? "实体长边" : "实体总长"}</small><strong>${formatNumber(report.length)} mm</strong></span>
          <span><small>${isSheetPart(part) ? "实体厚度" : "孔 / 开口"}</small><strong>${isSheetPart(part) ? `${formatNumber(report.plate?.thickness)} mm` : `${holes.length} 个`}</strong></span>
        </div>
        ${holes.length ? `<div class="tube-designer-dimension-list">${holes.slice(0, 8).map((hole) => `
          <article><strong>孔 / 开口 ${hole.index}</strong><span>距起点 ${formatNumber(hole.station)} · ${escapeText(hole.shapeLabel ?? hole.kind ?? "")}</span></article>`).join("")}</div>`
          : `<div class="tube-designer-part-no-holes">${isSheetPart(part) ? `实体短边 ${formatNumber(report.plate?.shortSide)} mm · ${manufacturingPartKindLabel(part)}另行处理` : "当前实体未识别到孔或开口"}</div>`}`
      : `<div class="tube-designer-part-measurement-loading"><i></i><span>正在分析最终零件几何…</span></div>`;
  return `
    <section class="tube-designer-part-manufacturing-card">
      <div class="tube-designer-parts-measurement-heading">
        <div><strong>制造信息</strong><span>${isTubeNestingPart(part) ? "下料零件 · 可作为排样输入" : escapeText(tubeNestingExclusionReason(part))}</span></div>
        <em class="tube-designer-part-ready-state">${escapeText(partReadyLabel(part))}</em>
      </div>
      <div class="tube-designer-part-property-grid">
        <span><small>零件编号</small><strong>${escapeText(part.partNumber || "未编号")}</strong></span>
        <span><small>数量</small><strong>${partQuantity(part)} 件</strong></span>
        ${isComponentPart(part) ? `<span class="wide"><small>模型宽 × 深 × 高</small><strong>${escapeText(componentDimensionsText(componentBounds(part)))}</strong></span><span><small>供料方式</small><strong>${escapeText(partSourcingLabel(part))}</strong></span>`
          : isSheetPart(part) ? `<span><small>宽 × 高</small><strong>${formatNumber(plateDimensions(part).width)} × ${formatNumber(plateDimensions(part).height)} mm</strong></span>
          <span><small>厚度</small><strong>${formatNumber(plateDimensions(part).thickness)} mm</strong></span>`
          : `<span><small>起始端</small><strong>${escapeText(cutName(endProcess.startCut))}</strong></span>
          <span><small>结束端</small><strong>${escapeText(cutName(endProcess.endCut))}</strong></span>`}
        <span class="wide"><small>材料 / 截面</small><strong>${escapeText(partMaterial(part))} · ${escapeText(report?.profile || partProfile(part))}</strong></span>
        <span class="wide"><small>来源产品</small><strong>${escapeText(part.productName || part.productCode || "未命名产品")}</strong></span>
      </div>
    </section>
    <section class="tube-designer-part-dimension-card">
      <div class="tube-designer-parts-measurement-heading">
        <div><strong>自动尺寸</strong><span>取自最终三维实体 · 单位 mm</span></div>
        <button data-cam-action="tube-designer-parts-toggle-dimensions" aria-pressed="${view.tubeDesignerPartDimensionsVisible === false ? "false" : "true"}">${view.tubeDesignerPartDimensionsVisible === false ? "显示" : "隐藏"}</button>
      </div>
      ${measurementBody}
    </section>`;
}

function resolveActivePart(view, parts) {
  const requested = String(view.tubeDesignerActivePartId ?? "");
  const part = parts.find((item) => String(item.entityId) === requested) ?? parts[0] ?? null;
  if (part && requested !== String(part.entityId)) view.tubeDesignerActivePartId = String(part.entityId);
  return part;
}

function partMeasurementKey(part) {
  // A resource version is scoped to its resource identity. Normal edits keep
  // that identity and advance the version; include both here as a defensive
  // guard for legacy/imported parts whose resource identity may be replaced.
  const entityId = String(part?.entityId ?? "");
  const resourceId = String(part?.manufacturingGeometryResourceId ?? "").trim();
  const version = Number(part?.manufacturingGeometryResourceVersion ?? 0);
  // Keep the compact legacy form for transient test/legacy records that do
  // not expose a resource ID at all; real persisted parts always include it.
  return resourceId ? `${entityId}@${resourceId}@${version}` : `${entityId}@${version}`;
}

function partMaterial(part) {
  const properties = part?.properties ?? {};
  return String(
    properties["manufacturing.material"]
      ?? properties["material.grade"]
      ?? properties.material
      ?? properties["tubeDesigner.material"]
      ?? "材料未指定",
  ).trim() || "材料未指定";
}

function partProfile(part) {
  if (isSheetPart(part)) {
    const plate = plateDimensions(part);
    return `${manufacturingPartKindLabel(part)} ${formatNumber(plate.width)} × ${formatNumber(plate.height)} × ${formatNumber(plate.thickness)} mm`;
  }
  if (isComponentPart(part)) {
    return `${part.properties?.["manufacturing.modelName"] ?? "三维配件"} ${componentDimensionsText(componentBounds(part))} · ${partSourcingLabel(part)}`;
  }
  const profile = part?.profile ?? {};
  const displayName = String(profile.displayName ?? "").trim();
  const specification = String(profile.specification ?? "").trim();
  return [displayName, specification].filter(Boolean).join(" ") || "截面未指定";
}

function componentDimensionsText(bounds) {
  return ["width", "depth", "height"].map((key) => Number(bounds?.[key]) > 0 ? formatNumber(bounds[key]) : "—").join(" × ") + " mm";
}

function partDisplayName(part) {
  return String(part?.name ?? part?.partNumber ?? `零件 ${part?.index ?? ""}`).trim();
}

function partCategory(part) {
  const properties = part?.properties ?? {};
  return String(
    properties["manufacturing.categoryName"]
      ?? part?.role
      ?? "普通零件",
  ).trim() || "普通零件";
}

function partEndProcess(part) {
  const properties = part?.properties ?? {};
  const process = properties["tubeDesigner.endProcess"];
  return process && typeof process === "object" ? process : {};
}

function partProcessKind(part) {
  if (isSheetPart(part) || isComponentPart(part)) return manufacturingPartKind(part);
  if (!isTubeNestingPart(part)) return "other";
  const properties = part?.properties ?? {};
  if (properties["tubeDesigner.cornerProcess"]) return "special";
  const process = partEndProcess(part);
  const cuts = [process.startCut, process.endCut]
    .map((cut) => String(cut ?? "").trim().toLowerCase())
    .filter(Boolean);
  if (!cuts.length) return "special";
  return cuts.every((cut) => ["square", "straight", "90", "90-degree"].includes(cut))
    ? "straight"
    : "special";
}

function partProcessLabel(part) {
  if (isSheetPart(part) || isComponentPart(part)) return manufacturingPartKindLabel(part);
  if (partSourcingLabel(part) === "外购") return "外购";
  if (!isTubeNestingPart(part)) return "另行处理";
  const properties = part?.properties ?? {};
  if (hasPartDrawing(part)) return "三维绘制";
  if (properties["manufacturing.punchPart"]) return "冲孔件";
  if (properties["tubeDesigner.cornerProcess"]) return "折弯 / 异形";
  const process = partEndProcess(part);
  const start = String(process.startCut ?? "").trim();
  const end = String(process.endCut ?? "").trim();
  if (!start && !end) return "待识别";
  if (partProcessKind(part) === "straight") return "直切";
  if (start && end && start === end) return `${cutName(start)} · 双端`;
  return "斜切 / 异形";
}

function cutName(value) {
  const cut = String(value ?? "").trim().toLowerCase();
  if (!cut) return "未标注";
  const names = {
    square: "直切 90°",
    straight: "直切 90°",
    "90": "直切 90°",
    "90-degree": "直切 90°",
    "miter-45": "斜切 45°",
    miter: "斜切",
    cope: "相贯 / 弧口",
    convex: "圆柱凸头",
  };
  return names[cut] ?? String(value);
}

function partReadyLabel(part) {
  const status = String(part?.status ?? "").trim().toLowerCase();
  if (["failed", "error", "invalid"].includes(status)) return "需要检查";
  if (part?.manufacturingGeometryResourceId || ["ready", "succeeded", "generated"].includes(status)) {
    return isTubeNestingPart(part) ? "可排样" : "可导出";
  }
  return "待确认";
}

function partQuantity(part) {
  const value = finiteNumber(part?.quantity);
  return value && value > 0 ? value : 1;
}

function finiteNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function waitForPartsAreaPaint() {
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
    const fallback = globalThis.setTimeout(finish, 100);
    requestFrame(() => requestFrame(finish));
  });
}

function formatNumber(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) return "—";
  return new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3 }).format(number);
}

function formatCompactLength(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) return "—";
  if (number >= 1000) return `${formatNumber(number / 1000)} m`;
  return `${formatNumber(number)} mm`;
}

function hashText(value) {
  let hash = 2166136261;
  for (const character of String(value)) {
    hash ^= character.codePointAt(0);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(36);
}

function escapeText(value) {
  return String(value ?? "").replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll('"', "&quot;");
}

function escapeAttribute(value) {
  return escapeText(value).replaceAll("'", "&#39;");
}
