import { renderParameterLevels } from './parameterPresentation.mjs';
import { renderFloatingEditorResizeHandles } from './floatingEditorDom.mjs';
import { parameterVisible } from './parameterConditions.mjs';
import { matchesParameterCondition, parameterEnabled, availableParameterChoices, effectiveParameterChoice } from "./parameterConditions.mjs";
import { productControlChoices, productControlEditableAfterCreation, productControlEffectiveValues, productControlFields, productControlValue, productStructureEditor, productStructureEditors } from "./productControls.mjs";
import {
  cancelDesignerPartThumbnailHydration,
  scheduleDesignerPartThumbnailHydration,
} from "./partThumbnail.mjs";
import { scheduleDesignerPartInspectionHydration } from "./partInspection.mjs";
import { importableProductManufacturingGroups, manufacturingPartKind, plateDimensions } from "./manufacturingParts.mjs";
import { restoreScrollAnchor } from "./scrollAnchor.mjs";
import { renderSecurityWindowReview, securityWindowOpeningDimensions } from "./securityWindowReview.mjs";
import { renderComponentModelField } from "./componentLibrary.mjs";
import {
  libraryProfiles,
  profileName,
  profileScope,
  profileSelectionKey,
} from "./profileLibrary.mjs";
import {
  isProductProfileField,
  isProductToolField,
  profileAllowedForProductField,
  productProfileConstraintError,
  productProfileRole,
  productProfileParameterBindings,
  productProfileDefaultSelection,
  productProfileRoleDeclaration,
  productToolBinding,
  productToolRole,
} from "./productResourceBindings.mjs";
import {
  catalogText,
  buildCatalogEntries,
  buildTemplateGroupTree,
  getCatalogGroupKeys,
  getCatalogEntry,
  getCatalogTemplatePath,
  getTemplateVisualAsset,
  sortTemplatesByCatalog,
} from "./productCatalog.mjs";
import { renderProductTemplateManagerDialog } from "./templateLibrary.mjs";
import {
  renderProductInstanceThumbnail,
  renderProductParameterDiagram,
  productPrimaryDimensions,
  resolveProductSpecificationAnnotations,
  resolveProductSpecificationAnnotationTree,
} from "./productParameterDiagram.mjs";
import { creationOnlyParameterKeys } from "./productParameterDependencies.mjs";
import { productParameterKind } from "./productParameterClassification.mjs";
export { sortTemplatesByCatalog } from "./productCatalog.mjs";

export function getDefaultParameters(templates = [], templateId = "") {
  const template = templateId ? getTemplateById(templates, templateId) : getDefaultTemplate(templates);
  const fields = Array.isArray(template?.parameters) ? template.parameters : [];
  return Object.fromEntries(fields.map((field) => [field.key ?? field.name, field.defaultValue]));
}

function parameterPresetScopeParameterKeys(template, scopeKey) {
  const key = String(scopeKey ?? "").trim();
  if (!key) return null;
  const section = parameterLayoutSections(template).find((entry) => entry.key === key);
  if (!section) return new Set();
  const groups = new Set(section.groups);
  return new Set((template?.parameters ?? [])
    .filter((field) => groups.has(String(field?.groupKey ?? field?.group ?? "").trim()))
    .map((field) => String(field?.key ?? field?.name ?? "").trim())
    .filter(Boolean));
}

export function getReusablePresetValues(template, values = {}, scopeKey = "") {
  const parameterKeys = new Set((template?.parameters ?? [])
    .map((field) => String(field?.key ?? field?.name ?? "").trim())
    .filter(Boolean));
  const scopedKeys = parameterPresetScopeParameterKeys(template, scopeKey);
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
  const result = Object.fromEntries(Object.entries(values ?? {}).filter(([key]) =>
    parameterKeys.has(key) && !excludedKeys.has(key) && (!scopedKeys || scopedKeys.has(key))));
  const profileRoles = (template?.parameters ?? [])
    .filter((field) => isProductProfileField(field)
      && (!scopedKeys || scopedKeys.has(String(field?.key ?? field?.name ?? ""))))
    .map(productProfileRole);
  const toolRoles = (template?.parameters ?? [])
    .filter((field) => isProductToolField(field)
      && (!scopedKeys || scopedKeys.has(String(field?.key ?? field?.name ?? ""))))
    .map(productToolRole);
  const profileOverrides = Object.fromEntries(profileRoles
    .filter((role) => values?.tubeDesignerProfileOverrides?.[role])
    .map((role) => [role, values.tubeDesignerProfileOverrides[role]]));
  const toolBindings = Object.fromEntries(toolRoles
    .filter((role) => values?.tubeDesignerToolBindings?.[role])
    .map((role) => [role, values.tubeDesignerToolBindings[role]]));
  if (Object.keys(profileOverrides).length) result.tubeDesignerProfileOverrides = profileOverrides;
  if (Object.keys(toolBindings).length) result.tubeDesignerToolBindings = toolBindings;
  return result;
}

export function applyReusablePresetValues(template, currentValues = {}, presetValues = {}, scopeKey = "") {
  const parameterKeys = new Set((template?.parameters ?? [])
    .map((field) => String(field?.key ?? field?.name ?? "").trim())
    .filter(Boolean));
  const scopedKeys = parameterPresetScopeParameterKeys(template, scopeKey);
  const result = { ...currentValues };
  for (const [key, value] of Object.entries(presetValues ?? {})) {
    if (parameterKeys.has(key) && (!scopedKeys || scopedKeys.has(key))) result[key] = value;
  }
  const mergeBindings = (storageKey, fields, roleOf) => {
    const source = presetValues?.[storageKey];
    if (!source || typeof source !== "object" || Array.isArray(source)) return;
    const allowedRoles = new Set((template?.parameters ?? [])
      .filter((field) => fields(field)
        && (!scopedKeys || scopedKeys.has(String(field?.key ?? field?.name ?? ""))))
      .map(roleOf));
    const accepted = Object.fromEntries(Object.entries(source)
      .filter(([role]) => allowedRoles.has(role)));
    if (Object.keys(accepted).length) result[storageKey] = {
      ...(result[storageKey] ?? {}), ...accepted,
    };
  };
  mergeBindings("tubeDesignerProfileOverrides", isProductProfileField, productProfileRole);
  mergeBindings("tubeDesignerToolBindings", isProductToolField, productToolRole);
  return result;
}

export function getTemplateById(templates = [], templateId = "") {
  return templates.find((item) => item?.id === templateId) ?? null;
}

export function templateDisplayView(template, mode) {
  const display = template?.display;
  if (display?.schema !== "icax.template-display" || Number(display?.schemaVersion) !== 1) {
    return { fields: {}, groups: {}, sectionGroups: {} };
  }
  const shared = display.shared ?? {};
  const view = display.views?.[mode] ?? {};
  return {
    fields: { ...(shared.fields ?? {}), ...(view.fields ?? {}) },
    groups: { ...(shared.groups ?? {}), ...(view.groups ?? {}) },
    sectionGroups: { ...(shared.sectionGroups ?? {}), ...(view.sectionGroups ?? {}) },
  };
}

export function applyTemplateFieldDisplay(template, fields, mode) {
  const rules = templateDisplayView(template, mode).fields;
  return (fields ?? []).map((field) => {
    const key = String(field?.key ?? field?.name ?? "");
    const rule = rules?.[key];
    if (!rule || typeof rule !== "object") return field;
    const order = Number(rule.order);
    const width = Object.fromEntries(["min", "preferred", "max"].flatMap((name) => {
      const value = Number(rule?.width?.[name]);
      return Number.isFinite(value) && value > 0 ? [[name, Math.min(800, Math.max(40, value))]] : [];
    }));
    return {
      ...field,
      ...(Number.isFinite(order) ? { order } : {}),
      uiDisplay: {
        line: rule.line === "full" ? "full" : "flow",
        width,
        ...(typeof rule.category === "string" && rule.category.trim()
          ? { category: rule.category.trim() } : {}),
        ...(Object.prototype.hasOwnProperty.call(rule, "sectionGroup")
          ? { sectionGroup: String(rule.sectionGroup ?? "").trim() } : {}),
      },
    };
  });
}

export function fieldDisplayClass(field) {
  const classes = [];
  if (field?.uiDisplay?.line === "full" || field?.presentation?.line === "full") classes.push("is-line-full");
  if (Object.keys(field?.uiDisplay?.width ?? {}).length) classes.push("has-control-size");
  return classes.length ? ` ${classes.join(" ")}` : "";
}

export function fieldDisplayStyle(field) {
  const width = field?.uiDisplay?.width ?? {};
  const properties = [
    ["min", "--tube-designer-control-min-width"],
    ["preferred", "--tube-designer-control-preferred-width"],
    ["max", "--tube-designer-control-max-width"],
  ].flatMap(([name, property]) => Number.isFinite(width[name])
    ? [`${property}:${width[name]}px`] : []);
  return properties.length ? ` style="${properties.join(";")}"` : "";
}

export function getTemplateDisplayName(template) {
  return getCatalogTemplatePath(template).at(-1);
}

export function getDefaultTemplate(templates = []) {
  return sortTemplatesByCatalog(templates).find((item) => item?.available) ?? null;
}

export function renderDesignerLeftPane(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const instances = Array.isArray(designer.instances) ? designer.instances.slice().sort((left, right) => {
    const leftTime = Date.parse(left?.createdAt ?? "");
    const rightTime = Date.parse(right?.createdAt ?? "");
    if (Number.isFinite(leftTime) && Number.isFinite(rightTime) && leftTime !== rightTime) return leftTime - rightTime;
    return 0;
  }) : [];
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

export function renderDesignerRightPane(context, view, { includePartInspection = true } = {}) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  const templates = Array.isArray(designer.templates) ? designer.templates : [];
  const dialogs = view.tubeDesignerPartInspectionOpen && !includePartInspection
    ? "" : renderDesignerDialogs(designer, view);
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
  if (!Array.isArray(template?.parameters)) {
    const detailError = String(view.tubeDesignerTemplateLoadError ?? "").trim();
    return `
      <div class="tube-designer-panel tube-designer-parameter-panel" data-tube-designer-parameter-form>
        <header class="tube-designer-parameter-header">
          <div class="tube-designer-parameter-toolbar">
            <div class="tube-designer-heading">
              <strong>${escapeText(product.name)}</strong>
              <span>${escapeText(formatProductDimensions(template, product.parameters ?? {}))} · ${designer.members?.length ?? 0} 个构件</span>
            </div>
          </div>
          <div class="tube-designer-summary">
            ${metric(designer.members?.length ?? 0, "预览构件")}
            ${metric(designer.joints?.length ?? 0, "连接关系")}
            ${metric(designer.parts?.length ?? 0, "已生成零件")}
          </div>
        </header>
        <div class="tube-designer-parameter-scroll" data-tube-designer-parameter-scroll>
          <div class="tube-designer-parameter-load-state" role="status">
            <strong>${detailError ? "产品参数未能载入" : "正在载入产品参数"}</strong>
            <span>${escapeText(detailError || "参数定义将在载入后显示；当前三维场景和已生成零件不受影响。")}</span>
            <button type="button" data-cam-action="tube-designer-reload-product-parameters" ${view.pending ? "disabled" : ""}>重新载入参数</button>
          </div>
        </div>
      </div>
      ${dialogs}
    `;
  }
  const parameterContent = renderDesignerRightParameterContent(designer, view);
  const { hasSavedPanelState, parts, scrollContent } = parameterContent;
  scheduleDesignerParameterPanelRestoration(context, view, product.entityId, hasSavedPanelState);
  return `
    <div class="tube-designer-panel tube-designer-parameter-panel"
      data-tube-designer-parameter-form data-tube-designer-product-parameter-scope
      data-window-state-controls="[data-product-parameter-key]"
      data-tube-designer-editor-mode="right" data-tube-designer-active-parameter="${escapeAttribute(view.tubeDesignerLastEditedParameterKey ?? "")}">
      <header class="tube-designer-parameter-header">
        <div class="tube-designer-parameter-toolbar">
          <div class="tube-designer-heading">
            <strong>${escapeText(product.name)}</strong>
            <span>${escapeText(formatProductDimensions(template, product.parameters ?? {}))} · ${designer.members?.length ?? 0} 个构件</span>
          </div>
        </div>
        <div class="tube-designer-summary">
          ${metric(designer.members?.length ?? 0, "预览构件")}
          ${metric(designer.joints?.length ?? 0, "连接关系")}
          ${metric(parts.length, "已生成零件")}
        </div>
      </header>
      <div class="tube-designer-parameter-scroll" data-tube-designer-parameter-scroll>${scrollContent}</div>
    </div>
    ${dialogs}
  `;
}

export function renderDesignerRightParameterContent(designer, view) {
  const product = designer?.product;
  const templates = Array.isArray(designer?.templates) ? designer.templates : [];
  const template = getTemplateById(templates, product?.templateId);
  if (!product || !Array.isArray(template?.parameters)) {
    return {
      hasSavedPanelState: false,
      parts: [],
      scrollContent: "",
    };
  }
  const values = { ...getDefaultParameters(templates, product.templateId), ...(product.parameters ?? {}), ...(view.tubeDesignerRightDraft ?? {}) };
  const locked = creationOnlyParameterKeys(template);
  const visibleFields = visibleParameterFields(template, values).filter(field =>
    field.presentation?.editor === "product-control"
      ? productControlEditableAfterCreation(template, field)
      : !locked.has(String(field.key ?? field.name)));
  const groupTree = buildParameterGroupTree(template, visibleFields, "right");
  const identityFields = collectSceneIdentityFields(groupTree, template);
  const parameterGroupTree = removeSceneIdentityFields(groupTree, template);
  // The scene editor always uses the same four business sections.  The
  // template's own groups remain underneath them, but category presentation
  // must not depend on which template happened to declare a layout extension.
  const allSceneGroupTree = buildSceneParameterSections(template, parameterGroupTree);
  const sceneGroupTree = allSceneGroupTree.filter((group) =>
    ["section:materials", "section:process"].includes(group.key) && countParameterGroupFields(group));
  const presetHostGroups = resolveParameterPresetHostGroups(template, sceneGroupTree);
  const defaultExpandedGroups = defaultExpandedParameterGroups(sceneGroupTree);
  const hasSavedPanelState = String(view.tubeDesignerParameterPanelProductId ?? "") === String(product.entityId ?? "")
    && Object.keys(view.tubeDesignerParameterDisclosureState ?? {}).length > 0;
  const expandedGroups = new Set(hasSavedPanelState && Array.isArray(view.tubeDesignerExpandedParameterGroups)
    ? view.tubeDesignerExpandedParameterGroups
    : defaultExpandedGroups);
  if (hasSavedPanelState) {
    // A newly applicable part group starts expanded. Explicitly collapsed
    // groups keep their current state across local and asynchronous patches.
    for (const key of defaultExpandedGroups) {
      if (!Object.hasOwn(view.tubeDesignerParameterDisclosureState, key)) expandedGroups.add(key);
    }
  }
  const parts = designer.parts ?? [];
  const scrollContent = `
    <section class="tube-designer-product-editor-stage" data-tube-designer-editor-stage="parameters">
          ${renderInstanceQuantityField(product.quantity ?? 1, "right", view.pending)}
          ${identityFields.length ? `<section class="tube-designer-scene-product-identity">
            <header><strong>产品信息</strong><small>${identityFields.length} 项</small></header>
            <div class="tube-designer-field-grid">${renderEditorParameterLevels(identityFields, (field) => renderField(
              field,
              values[field.key ?? field.name],
              view.pending,
              { values, view, mode: "right", template },
            ), { view, mode:'right', template }, 'identity')}</div>
          </section>` : ""}
          <div class="tube-designer-parameter-sections">
            ${sceneGroupTree.map((group) => compactParameterGroup(
              group,
              values,
              view.pending,
              expandedGroups,
              view,
              "right",
              template,
              presetHostGroups,
            )).join("")}
          </div>
    </section>`;
  return { hasSavedPanelState, parts, scrollContent };
}

export function renderDesignerViewportOverlay(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  if (!product) return "";
  return `
    ${renderDesignerRuntimeStatus(view)}
    ${renderDesignerSpecificationAnnotationTree(view)}
  `;
}

function renderSpecificationTreeSwitch(node, depth, action) {
  const state = String(node?.state ?? "none");
  const groupKeys = (node?.groupKeys ?? []).join(" ");
  return `<button type="button"
    class="tube-designer-scene-specification-switch is-${state}${depth === 0 ? " tube-designer-scene-specification-toggle" : ""}"
    style="--tube-designer-annotation-tree-depth:${depth}"
    data-cam-action="${escapeAttribute(action)}"
    data-tube-designer-annotation-groups="${escapeAttribute(groupKeys)}"
    role="treeitem"
    aria-checked="${state === "mixed" ? "mixed" : String(state === "all")}"
    aria-pressed="${state === "all"}"
    title="${escapeAttribute(state === "all" ? `隐藏${node.label}` : `显示${node.label}`)}">
      <span class="tube-designer-scene-specification-check" aria-hidden="true"></span>
      <span class="tube-designer-scene-specification-label">${escapeText(node?.label ?? "规格")}</span>
      <small>${Number(node?.count ?? 0)}</small>
  </button>`;
}

export function renderDesignerSpecificationAnnotationTree(view, designer = view?.scene?.tubeDesigner ?? {}) {
  const tree = resolveProductSpecificationAnnotationTree(designer, view);
  if (!tree) return "";
  const collapsed = view?.tubeDesignerSpecificationTreeCollapsed === true;
  const collapseButton = `<button type="button"
    class="tube-designer-scene-specification-collapse${collapsed ? " is-collapsed" : ""}"
    data-cam-action="tube-designer-toggle-specification-tree-collapse"
    aria-label="${collapsed ? "展开尺寸规格分组" : "收起尺寸规格分组"}"
    aria-expanded="${String(!collapsed)}"
    title="${collapsed ? "展开尺寸规格" : "收起到左侧"}">
      <span class="tube-designer-scene-specification-collapse-icon" aria-hidden="true">${collapsed ? "›" : "‹"}</span>
      ${collapsed ? '<span class="tube-designer-scene-specification-collapse-label">规格</span>' : ""}
  </button>`;
  if (collapsed) {
    return `<section class="tube-designer-scene-specification-tree is-collapsed"
      data-tube-designer-specification-tree aria-label="尺寸规格显示设置">
        ${collapseButton}
    </section>`;
  }
  const children = tree.children.map((node) => {
    if (!Array.isArray(node?.children) || !node.children.length) {
      return renderSpecificationTreeSwitch(node, 1, "tube-designer-toggle-specification-annotation-group");
    }
    return `<div class="tube-designer-scene-specification-branch" role="group">
      ${renderSpecificationTreeSwitch(node, 1, "tube-designer-toggle-specification-annotation-group")}
      <div class="tube-designer-scene-specification-children" role="group">
        ${node.children.map((child) => renderSpecificationTreeSwitch(
          child, 2, "tube-designer-toggle-specification-annotation-group",
        )).join("")}
      </div>
    </div>`;
  }).join("");
  return `<section class="tube-designer-scene-specification-tree"
    data-tube-designer-specification-tree role="tree" aria-label="尺寸规格显示设置">
      <header>${renderSpecificationTreeSwitch(
        tree, 0, "tube-designer-toggle-specification-annotations",
      )}${collapseButton}</header>
      <div class="tube-designer-scene-specification-tree-body" role="group">${children}</div>
  </section>`;
}

export function renderDesignerRuntimeStatus(view) {
  const outdated = view.tubeDesignerRightDraftDirty === true;
  if (outdated) {
    return `<div class="tube-designer-scene-runtime-status is-outdated" data-tube-designer-runtime-status role="status">
      <span>${view.pending ? "正在重新生成产品实例…" : "产品实例已过期，是否重新生成"}</span>
      <button type="button" class="tube-designer-primary" data-cam-action="tube-designer-confirm-update"
        title="按当前参数重新生成三维模型；原零件清单需要重新拆单" ${view.pending ? "disabled" : ""}>确定</button>
    </div>`;
  }
  return `<div class="tube-designer-scene-runtime-status" data-tube-designer-runtime-status hidden></div>`;
}

/**
 * The product scene owns its realised parts.  This intentionally reads the
 * disassembly result, not a template's provisional `tables`, so a measurement
 * always opens the final three-dimensional part that will be exported.
 */
export function renderDesignerProductPartsDock(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  const productId = String(product?.entityId ?? designer.activeProductId ?? "");
  const group = (designer.manufacturingGroups ?? [])
    .find((item) => String(item?.productEntityId ?? "") === productId);
  const parts = [...(group?.parts ?? [])].sort((left, right) => Number(left?.index ?? 0) - Number(right?.index ?? 0));
  const totalQuantity = parts.reduce((total, part) => total + partQuantity(part), 0);
  const partsOutdated = Boolean(productId) && (view.tubeDesignerPartsDraftDirty === true
    || view.tubeDesignerRightDraftDirty === true || product?.modelOutdated === true || product?.partsOutdated === true);
  const requiresRedisassembly = Boolean(productId)
    && (view.tubeDesignerProductsRequiringDisassembly ?? []).map(String).includes(productId)
    && !parts.length;
  const disabled = view.pending || view.tubeDesignerExportOperation;
  const heading = product
    ? partsOutdated && parts.length
      ? "零件清单已过期，是否重新生成"
      : `${product.name} · ${parts.length ? `${parts.length} 种零件 · 共 ${formatNumber(totalQuantity)} 件` : requiresRedisassembly ? "需要重新拆单" : "未拆单"}`
    : "尚未选择产品实例";
  return `<div class="tube-designer-bottom-splitter tube-designer-product-bottom-splitter" data-cam-resize-pane="bottom" data-no-window-drag
      title="拖拽调整实例零件清单高度" aria-label="调整实例零件清单高度"></div>
    <section class="tube-designer-product-parts-dock ${partsOutdated && parts.length ? "is-outdated" : ""}" aria-label="当前产品实例零件清单">
      <header>
        <div><strong>实例零件清单</strong><span>${escapeText(heading)}</span></div>
        <div class="tube-designer-product-parts-dock-actions">
          ${product && (!parts.length || partsOutdated) ? `<button type="button" class="tube-designer-primary" data-cam-action="tube-designer-disassemble-active-product" ${disabled ? "disabled" : ""}>${partsOutdated && parts.length ? "确定" : "生成零件清单"}</button>` : ""}
        </div>
      </header>
      <div class="tube-designer-product-parts-table">
        ${product ? (parts.length ? `<table><thead><tr><th>零件编号</th><th>名称</th><th>规格</th><th>数量</th><th>长度</th></tr></thead>
          <tbody>${parts.map((part) => {
            const active = String(view.tubeDesignerActivePartId ?? "") === String(part.entityId)
              || (view.tubeDesignerActiveProductPartIds ?? []).map(String).includes(String(part.entityId));
            const preview = (view.tubeDesignerHoveredProductPartIds ?? []).map(String).includes(String(part.entityId));
            return `<tr class="${active ? "is-active" : ""}${preview ? " is-preview" : ""}" data-cam-action="tube-designer-select-product-part" data-tube-designer-part-id="${escapeAttribute(part.entityId)}" data-tube-designer-product-part-row="${escapeAttribute(part.entityId)}" aria-selected="${active}" tabindex="0"><td>${escapeText(part.partNumber ?? "—")}</td><td>${escapeText(partDisplayName(part))}</td><td>${escapeText(formatPartSpecificationWithLength(part))}</td><td>${escapeText(part.quantity ?? 1)}</td><td>${escapeText(formatNumber(part.length))} mm</td></tr>`;
          }).join("")}</tbody></table>`
          : `<div class="tube-designer-product-parts-empty ${requiresRedisassembly ? "is-awaiting-disassembly" : ""}"><strong>${requiresRedisassembly ? "待拆单生成零件信息" : "尚未生成零件清单"}</strong>${requiresRedisassembly ? "" : "<span>生成后将显示此实例的最终零件；可逐件进入复尺。</span>"}</div>`)
          : `<div class="tube-designer-product-parts-empty"><strong>请选择产品实例</strong><span>左侧选择实例后，在这里查看它自己的零件清单。</span></div>`}
      </div>
    </section>`;
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
        <div class="tube-designer-export-progress-track is-indeterminate" role="progressbar" aria-label="${escapeAttribute(operation.title ?? "任务进度")}" aria-valuetext="${escapeAttribute(operation.phaseLabel ?? "处理中")}">
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
    view.tubeDesignerTemplateManager ? renderProductTemplateManagerDialog(view) : "",
    view.tubeDesignerExcelTemplateDialog ? renderBatchExcelTemplateDialog(designer, view) : "",
    view.tubeDesignerAddDialogOpen ? renderDesignerAddDialog(designer, view) : "",
    view.activeAreaId === "view" && view.tubeDesignerBatchDisassemblyDialog ? renderBatchDisassemblyDialog(designer, view) : "",
    view.tubeDesignerDisassemblySelectorOpen ? renderProductNestingImportSelector(designer, view) : "",
    view.tubeDesignerBreakdownOpen ? renderBreakdownDialog(designer, view) : "",
    view.tubeDesignerPresetDialog ? renderParameterPresetDialog(designer, view) : "",
    view.tubeDesignerProfileDialog ? renderImportedProfileDialog(designer, view) : "",
    view.tubeDesignerProfileLibraryDialog ? renderImportedProfileLibraryDialog(view) : "",
  ].join("");
}


export function renderBatchExcelTemplateDialog(designer, view) {
  const state = view?.tubeDesignerExcelTemplateDialog ?? {};
  const templates = Array.isArray(designer?.templates) ? designer.templates : [];
  const selectedId = String(state.templateId ?? "");
  const selected = getTemplateById(templates, selectedId);
  const pending = Boolean(view?.pending || state.loading);
  // The product catalogue can carry a lightweight record and a hydrated
  // descriptor for the same template during one session.  Excel export is
  // template-oriented, not catalogue-card-oriented: one stable template ID
  // must always produce exactly one option here.
  const availableTemplates = [...new Map(sortTemplatesByCatalog(templates)
    .filter((template) => template?.available && template?.extensions?.catalog?.listed !== false)
    .map((template) => [String(template.id), template])).values()];
  const columns = Array.isArray(state.columns) ? state.columns : [];
  const allIncluded = columns.length > 0 && columns.every((column) => column.included !== false);
  const allRequired = columns.length > 0 && columns.every((column) => column.required === true);
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-excel-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-excel-template-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-excel-template-title">导出 Excel 批量导入工作簿</strong><span>导出后直接填写并导入。每一行会创建一个产品实例。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-excel-template-close" aria-label="关闭" ${pending ? "disabled" : ""}>×</button>
        </header>
        <form class="tube-designer-excel-template-form" data-tube-designer-excel-template-form>
          <label class="tube-designer-field"><span>产品模板</span>
            <select data-cam-change-action="tube-designer-excel-template-select" aria-label="选择产品模板" ${pending ? "disabled" : ""}>
              ${availableTemplates.map((template) => `<option value="${escapeAttribute(template.id)}" ${template.id === selectedId ? "selected" : ""}>${escapeText(getTemplateDisplayName(template) || template.id)}</option>`).join("")}
            </select>
          </label>
          <div class="tube-designer-excel-template-help">勾选需要填写的字段，未勾选的产品参数使用模板默认值。导出的工作表只显示中文列名和中文选项；内部字段及选项映射保存在隐藏页，导入时自动识别。</div>
          <div class="tube-designer-excel-column-table" role="table" aria-label="Excel 导入列配置">
            <div class="tube-designer-excel-column-head" role="row">
              <label class="tube-designer-excel-head-check"><input type="checkbox"
                data-cam-change-action="tube-designer-excel-template-toggle-all"
                data-tube-designer-excel-select-all="include"
                aria-label="全选或取消全部携带字段" ${allIncluded ? "checked" : ""} ${pending ? "disabled" : ""}/><span>携带</span></label>
              <span>导出列名</span><span>自定义列名（可选）</span><span>产品参数说明</span>
              <label class="tube-designer-excel-head-check"><input type="checkbox"
                data-cam-change-action="tube-designer-excel-template-toggle-all"
                data-tube-designer-excel-select-all="required"
                aria-label="全选或取消全部必填字段" ${allRequired ? "checked" : ""} ${pending ? "disabled" : ""}/><span>必填</span></label>
              <span>为空时默认值</span>
            </div>
            ${columns.map((column) => `<div class="tube-designer-excel-column-row" role="row">
              <label class="tube-designer-excel-include"><input type="checkbox"
                data-cam-change-action="tube-designer-excel-template-selection-change"
                data-tube-designer-excel-selection-kind="include"
                data-tube-designer-excel-include="${escapeAttribute(column.key)}"
                aria-label="携带${escapeAttribute(column.displayName ?? column.key)}" ${column.included !== false ? "checked" : ""} ${pending ? "disabled" : ""}/></label>
              <code class="tube-designer-excel-column-name">${escapeText(column.title ?? column.key)}</code>
              <input class="tube-designer-excel-column-alias" type="text"
                data-tube-designer-excel-alias="${escapeAttribute(column.key)}"
                value="${escapeAttribute(column.alias ?? "")}"
                placeholder="${escapeAttribute(column.title ?? column.key)}"
                aria-label="${escapeAttribute(column.displayName ?? column.key)}的 Excel 列别名" ${pending ? "disabled" : ""}/>
              <span class="tube-designer-excel-column-context"><b>${escapeText(column.displayName ?? column.key)}</b><em>${escapeText(column.groupTitle ?? "产品参数")}</em>${column.description ? `<small>${escapeText(column.description)}</small>` : ""}</span>
              <label class="tube-designer-excel-required"><input type="checkbox"
                data-cam-change-action="tube-designer-excel-template-selection-change"
                data-tube-designer-excel-selection-kind="required"
                data-tube-designer-excel-required="${escapeAttribute(column.key)}"
                aria-label="${escapeAttribute(column.displayName ?? column.key)}设为必填" ${column.required ? "checked" : ""} ${pending ? "disabled" : ""}/></label>
              ${renderBatchExcelDefaultControl(column, pending)}
            </div>`).join("")}
          </div>
        </form>
        <footer class="tube-designer-dialog-footer">
          <span>导出的 .xlsx 可直接填写并导入。</span>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-excel-template-close" ${pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-excel-template-export" ${pending || !selected ? "disabled" : ""}>导出模板</button>
        </footer>
      </section>
    </div>`;
}

function renderBatchExcelDefaultControl(column, pending) {
  const key = escapeAttribute(column?.key ?? "");
  const value = String(column?.defaultValue ?? "");
  const disabled = pending ? "disabled" : "";
  if (column?.inputKind === "select") {
    const options = Array.isArray(column?.options) ? column.options : [];
    const hasValue = options.some((option) => String(option?.value ?? "") === value);
    return `<select data-tube-designer-excel-default="${key}" aria-label="${escapeAttribute(column?.title ?? column?.key ?? "默认值")}" ${disabled}>
      <option value="" ${value === "" ? "selected" : ""}>留空则使用模板默认值</option>
      ${!hasValue && value !== "" ? `<option value="${escapeAttribute(value)}" selected>${escapeText(value)}（当前值）</option>` : ""}
      ${options.map((option) => `<option value="${escapeAttribute(option?.value ?? "")}" ${String(option?.value ?? "") === value ? "selected" : ""}>${escapeText(option?.label ?? option?.value ?? "")}</option>`).join("")}
    </select>`;
  }
  if (column?.inputKind === "boolean") {
    const checked = value === true || value === "true" || value === "1" || value === "是";
    return `<select data-tube-designer-excel-default="${key}" aria-label="${escapeAttribute(column?.title ?? column?.key ?? "默认值")}" ${disabled}>
      <option value="" ${value === "" ? "selected" : ""}>留空则使用模板默认值</option>
      <option value="true" ${checked ? "selected" : ""}>是</option>
      <option value="false" ${!checked && value !== "" ? "selected" : ""}>否</option>
    </select>`;
  }
  const attributes = [
    `data-tube-designer-excel-default="${key}"`,
    `value="${escapeAttribute(value)}"`,
    'placeholder="留空则使用模板默认值"',
    disabled,
  ];
  if (column?.inputKind === "number") {
    attributes.unshift('type="number"');
    if (column.minimum != null) attributes.push(`min="${escapeAttribute(column.minimum)}"`);
    if (column.maximum != null) attributes.push(`max="${escapeAttribute(column.maximum)}"`);
    attributes.push(`step="${escapeAttribute(column.step ?? "any")}"`);
  }
  return `<input ${attributes.filter(Boolean).join(" ")} />`;
}

function renderInstanceCard(instance, templates, selected, pending) {
  const template = getTemplateById(templates, instance.templateId);
  const parameters = instance.parameters ?? {};
  return `
    <article class="tube-designer-instance-item ${selected ? "selected" : ""}">
      <button class="tube-designer-instance-card ${selected ? "selected" : ""}" data-cam-action="tube-designer-select-instance" data-tube-designer-instance-id="${escapeAttribute(instance.entityId)}" ${pending || selected ? "disabled" : ""}>
        <span class="tube-designer-instance-thumbnail">${renderProductInstanceThumbnail(template, parameters) || renderSchematic(template, parameters)}</span>
        <span class="tube-designer-instance-copy">
          <strong>${escapeText(instance.name)}</strong>
          <span>${escapeText(getTemplateDisplayName(template) || instance.templateId)}</span>
          <small>${escapeText(formatProductDimensions(template, parameters))} · 数量 ${escapeText(instance.quantity ?? 1)} · ${instance.hasDisassembly ? `${instance.partCount} 种零件` : "未拆单"}</small>
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
  const selectedEntry = getCatalogEntry(templates, template?.id);
  const expandedTemplateGroupIds = new Set(
    view.tubeDesignerExpandedTemplateGroupIds ?? getCatalogGroupKeys(templates),
  );
  const pending = Boolean(view.pending || view.tubeDesignerTemplateSwitchPending);
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-config-dialog tube-designer-add-dialog" data-tube-designer-add-dialog role="dialog" aria-modal="true" aria-labelledby="tube-designer-add-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-add-title">添加产品实例</strong><span>先选择款式并确定结构；创建后可通过标注修改尺寸与布局，并调整用料和装配。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-cancel-add" aria-label="取消添加" ${pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-config-body">
          <aside class="tube-designer-template-pane">
            <div class="tube-designer-pane-title"><strong>产品款式</strong><span>${buildCatalogEntries(templates).length} 款可用</span></div>
            <div class="tube-designer-template-list" aria-label="产品款式分组">
              ${templateGroups.map((group) => renderTemplateGroup(
                group,
                selectedEntry?.catalogEntryId,
                pending,
                expandedTemplateGroupIds,
              )).join("")}
            </div>
          </aside>
          <main class="tube-designer-config-parameters" data-tube-designer-add-form data-tube-designer-rendered-template-id="${escapeAttribute(template?.id)}">
            ${renderDesignerAddParameterContent(designer, view)}
          </main>
        </div>
        <footer class="tube-designer-dialog-footer">
          <span>确定后创建实例并生成产品预览</span>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-cancel-add" ${pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-confirm-add" ${pending || !template?.available ? "disabled" : ""}>${pending ? "正在生成…" : "确定"}</button>
        </footer>
      </section>
    </div>
  `;
}

function renderInstanceQuantityField(quantity, mode, pending) {
  return `<label class="tube-designer-field tube-designer-instance-quantity"><span>生产数量</span>
    <input type="number" min="1" max="1000000" step="1" value="${escapeAttribute(quantity)}"
      data-tube-designer-instance-quantity="${mode}" data-cam-change-action="tube-designer-instance-quantity-change" ${pending ? "disabled" : ""} />
    ${mode === "add" ? "<small>按此数量计算下料与导出清单。</small>" : ""}</label>`;
}

export function renderDesignerAddParameterContent(designer, view) {
  const templates = Array.isArray(designer?.templates) ? designer.templates : [];
  const template = getTemplateById(templates, view?.tubeDesignerAddTemplateId) ?? getDefaultTemplate(templates);
  const values = { ...getDefaultParameters(templates, template?.id), ...(view?.tubeDesignerAddDraft ?? {}) };
  const structureFields = addDialogStructureFields(template, values);
  const groupTree = buildParameterGroupTree(template, structureFields, "add");
  const declaredGroups = template?.extensions?.addDialog?.parameterGroups;
  const displayFields = applyTemplateFieldDisplay(template, structureFields, "add");
  const addDialogGroups = Array.isArray(declaredGroups) && declaredGroups.length
    ? declaredGroups.flatMap(group => {
      const fields = (group.parameters ?? []).flatMap(key => displayFields.filter(field => field.key === key));
      return fields.length ? [{ key: `add:${group.key}`, title: catalogText(group.displayName, "产品选项"),
        fields, children: [], defaultOpen: true }] : [];
    }) : groupTree.flatMap((group) => group.children?.length ? group.children : [group]);
  const pending = Boolean(view?.pending);
  return `
    <section class="tube-designer-product-editor-stage tube-designer-add-editor-stage" data-tube-designer-editor-stage="parameters" data-tube-designer-product-parameter-scope data-tube-designer-editor-mode="add" data-tube-designer-active-parameter="${escapeAttribute(view?.tubeDesignerLastEditedParameterKey ?? "")}">
      <div class="tube-designer-add-quick-layout">
        <div class="tube-designer-add-quick-visual">${renderProductParameterDiagram(template, values, {
          mode: "add",
          compact: true,
          hideDimensionCards: true,
          activeParameter: view?.tubeDesignerLastEditedParameterKey,
        })}</div>
        <div class="tube-designer-add-quick-options">
          <div class="tube-designer-add-structure-intro"><strong>确定结构</strong><span>结构创建后固定；尺寸与布局数值仍可通过标注修改。</span></div>
          ${template?.available && addDialogGroups.length
            ? `<div class="tube-designer-add-structure-fields">${addDialogGroups.map((group) => renderAddParameterGroup(
              group, values, pending, view, "add", template,
            )).join("")}</div>`
            : template?.available
              ? `<div class="tube-designer-empty">此款式无需预先选择结构；创建后可通过标注修改尺寸与布局，并调整用料和装配。</div>`
              : `<div class="tube-designer-empty">${escapeText(template?.status ?? "该模板尚不可用。")}</div>`}
        </div>
      </div>
    </section>
  `;
}

function addDialogStructureFields(template, values) {
  const declared = template?.extensions?.addDialog?.structureParameters;
  const visible = visibleParameterFields(template, values);
  if (Array.isArray(declared) && declared.length) {
    const wanted = new Set(declared.map(String));
    return visible.filter((field) => wanted.has(String(field?.key ?? field?.name ?? "")));
  }
  return [];
}

function productEditorStage(view, mode) {
  const stage = mode === "add" ? view?.tubeDesignerAddEditorStage : view?.tubeDesignerRightEditorStage;
  return stage === "manufacturing" ? "manufacturing" : "parameters";
}

function renderProductEditorTabs(mode, activeStage, planState, fingerprint) {
  const planCurrent = planState?.fingerprint === fingerprint;
  const planStatus = planCurrent ? String(planState?.status ?? "") : "stale";
  const planLabel = planStatus === "loading" ? "计算中"
    : planStatus === "ready" ? "已计算"
      : planStatus === "error" ? "需处理" : "待计算";
  return `<nav class="tube-designer-product-editor-tabs" aria-label="产品模板编辑阶段">
    <button type="button" class="${activeStage === "parameters" ? "selected" : ""}" data-cam-action="tube-designer-select-product-editor-stage" data-tube-designer-editor-mode="${mode}" data-tube-designer-editor-stage="parameters" aria-selected="${activeStage === "parameters"}"><span>参数配置</span><small>规格、材料与工艺</small></button>
    <button type="button" class="${activeStage === "manufacturing" ? "selected" : ""}" data-cam-action="tube-designer-select-product-editor-stage" data-tube-designer-editor-mode="${mode}" data-tube-designer-editor-stage="manufacturing" aria-selected="${activeStage === "manufacturing"}"><span>加工清单</span><small>${planLabel}</small></button>
  </nav>`;
}

function visibleParameterFields(template, values) {
  const selector = String(template?.extensions?.parameterPresets?.selectorParameter ?? "").trim();
  const hiddenGroups = new Set((template?.groups ?? [])
    .filter((group) => group?.editorHidden === true)
    .map((group) => String(group?.key ?? "").trim()).filter(Boolean));
  return [...visibleFields(template?.parameters ?? [], values),
    ...visibleFields(productControlFields(template), productControlEffectiveValues(template, values))]
    .filter((field) => {
      const key = String(field?.key ?? field?.name ?? "");
      if (key === selector) return false;
      if (hiddenGroups.has(String(field?.groupKey ?? field?.group ?? "").trim())) return false;
      if (fieldReplacedBySelectedProfile(field, { template, values })) return false;
      return true;
    });
}

function presetSelectionForScope(view, mode, scopeKey) {
  const selections = mode === "add"
    ? view?.tubeDesignerAddPresetSelections : view?.tubeDesignerRightPresetSelections;
  const key = String(scopeKey ?? "").trim() || "default";
  if (selections && typeof selections === "object" && !Array.isArray(selections)) {
    return String(selections[key] ?? "");
  }
  return "";
}

function renderParameterPresetBar(template, values, view, mode, scopeKey = "") {
  if (!template) return "";
  const definition = template?.extensions?.parameterPresets ?? {};
  const selector = String(definition?.selectorParameter ?? "").trim();
  const builtInScopeKey = getDefaultParameterPresetScopeKey(template);
  const selectorField = (template?.parameters ?? [])
    .find((field) => String(field?.key ?? field?.name ?? "") === selector);
  const choiceLabels = new Map((selectorField?.options ?? selectorField?.choices ?? [])
    .map((choice) => [String(choice?.value ?? choice), String(choice?.label ?? choice?.displayName ?? choice)]));
  const builtIns = (scopeKey === builtInScopeKey || (!scopeKey && !builtInScopeKey)
    ? (Array.isArray(definition?.presets) ? definition.presets : []) : [])
    .filter((preset) => preset?.value && preset?.values && typeof preset.values === "object");
  const userPresets = getUserParameterPresets(view, template, scopeKey);
  const storedSelection = presetSelectionForScope(view, mode, scopeKey);
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
    <section class="tube-designer-user-preset-bar" data-tube-designer-preset-mode="${escapeAttribute(mode)}" data-tube-designer-preset-scope="${escapeAttribute(scopeKey)}">
      <label>
        <span>${scopeKey === "process" ? "常用连接方案" : scopeKey === "materials" ? "常用材料方案" : "常用参数"}</span>
        <select data-cam-change-action="tube-designer-apply-parameter-preset" data-tube-designer-preset-selection="${escapeAttribute(mode)}" data-tube-designer-preset-mode="${escapeAttribute(mode)}" data-tube-designer-preset-scope="${escapeAttribute(scopeKey)}" ${view?.pending ? "disabled" : ""}>
          <option value="custom" ${selected === "custom" ? "selected" : ""}>当前自定义参数</option>
          ${builtIns.length ? `<optgroup label="模板内置">${builtIns.map((preset) => {
            const value = `builtin:${preset.value}`;
            return `<option value="${escapeAttribute(value)}" ${selected === value ? "selected" : ""}>${escapeText(choiceLabels.get(String(preset.value)) ?? preset.value)}</option>`;
          }).join("")}</optgroup>` : ""}
          ${userPresets.length ? `<optgroup label="我的常用方案">${userPresets.map((preset) => {
            const value = `user:${preset.id}`;
            const customerName = customersById.get(String(preset.customerId ?? ""))?.name;
            const label = customerName ? `${customerName} / ${preset.name}` : `${preset.name}`;
            return `<option value="${escapeAttribute(value)}" ${selected === value ? "selected" : ""}>${escapeText(label)}</option>`;
          }).join("")}</optgroup>` : ""}
        </select>
      </label>
      <div>
        <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-open-save-preset" data-tube-designer-preset-mode="${escapeAttribute(mode)}" data-tube-designer-preset-scope="${escapeAttribute(scopeKey)}" ${view?.pending ? "disabled" : ""}>另存为常用</button>
        ${selectedUserPreset ? `<button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-open-manage-preset" data-tube-designer-preset-mode="${escapeAttribute(mode)}" data-tube-designer-preset-scope="${escapeAttribute(scopeKey)}" data-tube-designer-preset-id="${escapeAttribute(selectedUserPreset.id)}" ${view?.pending ? "disabled" : ""}>管理</button>` : ""}
      </div>
    </section>`;
}

function parameterLayoutSections(template) {
  const sections = template?.extensions?.parameterLayout?.sections;
  if (!Array.isArray(sections)) return [];
  return sections
    .map((section, index) => ({
      key: String(section?.key ?? "").trim(),
      title: localizedProfileText(section?.displayName, String(section?.key ?? "参数")),
      order: Number.isFinite(Number(section?.order)) ? Number(section.order) : index,
      defaultOpen: section?.defaultOpen === true,
      allowPresets: section?.allowPresets === true,
      groups: (Array.isArray(section?.groups) ? section.groups : [])
        .map((key) => String(key ?? "").trim()).filter(Boolean),
    }))
    .filter((section) => section.key && section.groups.length)
    .sort((left, right) => left.order - right.order);
}

export function getParameterPresetScopeKey(template, parameterKey) {
  const key = String(parameterKey ?? "").trim();
  const field = (template?.parameters ?? [])
    .find((candidate) => String(candidate?.key ?? candidate?.name ?? "").trim() === key);
  const groupKey = String(field?.groupKey ?? field?.group ?? "").trim();
  return parameterLayoutSections(template)
    .find((section) => section.groups.includes(groupKey))?.key ?? "";
}

export function getDefaultParameterPresetScopeKey(template) {
  const selector = String(template?.extensions?.parameterPresets?.selectorParameter ?? "").trim();
  const selectorScope = getParameterPresetScopeKey(template, selector);
  if (selectorScope) return selectorScope;
  return parameterLayoutSections(template).find((section) => section.allowPresets)?.key ?? "";
}

function resolveParameterPresetHostGroups(template, groupTree) {
  const result = new Map();
  for (const section of parameterLayoutSections(template).filter((entry) => entry.allowPresets)) {
    const rootKey = `section:${section.key}`;
    if (groupTree.some((group) => group.key === rootKey)) result.set(rootKey, section.key);
  }
  return result;
}

export function getUserParameterPresets(view, template, scopeKey = "") {
  const templateId = String(template?.id ?? "");
  return (Array.isArray(view?.tubeDesignerUserData?.parameterPresets)
    ? view.tubeDesignerUserData.parameterPresets : [])
    .filter((preset) => String(preset?.templateId ?? "") === String(templateId ?? ""))
    .filter((preset) => String(preset?.templateVersion ?? "") === String(template?.version ?? ""))
    .filter((preset) => !scopeKey || String(preset?.scopeKey ?? "") === scopeKey)
    .sort((left, right) => String(left?.name ?? "").localeCompare(String(right?.name ?? ""), "zh-CN"));
}

function renderParameterPresetDialog(designer, view) {
  const state = view?.tubeDesignerPresetDialog ?? {};
  const mode = state.mode === "add" ? "add" : "right";
  const templateId = mode === "add"
    ? view?.tubeDesignerAddTemplateId
    : designer?.product?.templateId;
  const template = getTemplateById(designer?.templates ?? [], templateId);
  const scopeKey = String(state.scopeKey ?? "").trim();
  const declaredTitle = parameterLayoutSections(template).find((section) => section.key === scopeKey)?.title ?? "参数";
  const scopeTitle = scopeKey === "process" && declaredTitle === "连接做法" ? "连接" : declaredTitle;
  const preset = state.presetId
    ? getUserParameterPresets(view, template, scopeKey).find((item) => String(item.id) === String(state.presetId))
    : null;
  const customers = Array.isArray(view?.tubeDesignerUserData?.customers)
    ? view.tubeDesignerUserData.customers : [];
  return `
    <div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop tube-floating-editor-layer" data-floating-editor-layer="product-preset" role="presentation">
      <section class="tube-designer-preset-dialog" data-floating-editor-window="product-preset" role="dialog" aria-modal="false" aria-labelledby="tube-designer-preset-title"
        data-window-state-controls="${preset ? '' : '[data-tube-designer-preset-name],[data-tube-designer-preset-customer-id]'}">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-preset-title">${preset ? `管理${escapeText(scopeTitle)}方案` : `保存${escapeText(scopeTitle)}方案`}</strong><span>${escapeText(getTemplateDisplayName(template))} · 只保存“${escapeText(scopeTitle)}”中的参数，不覆盖其他分区。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-preset-dialog" aria-label="关闭" ${view?.pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-preset-dialog-body">
          <label class="tube-designer-field wide">方案名称<input type="text" data-tube-designer-preset-name value="${escapeAttribute(state.name ?? preset?.name ?? "")}" maxlength="120" placeholder="${scopeKey === "process" ? "例如：标准转角与框体连接方案" : "例如：常用不锈钢管材配置"}" /></label>
          <label class="tube-designer-field wide">已有客户<select data-tube-designer-preset-customer-id>
            <option value="">不关联客户</option>
            ${customers.map((customer) => `<option value="${escapeAttribute(customer?.id)}" ${String(customer?.id) === String(state.customerId ?? preset?.customerId ?? "") ? "selected" : ""}>${escapeText(customer?.name)}</option>`).join("")}
          </select></label>
          <label class="tube-designer-field wide">新客户<input type="text" data-tube-designer-preset-customer-name value="${escapeAttribute(state.customerName ?? "")}" maxlength="120" placeholder="可选；填写后自动建立客户档案" /></label>
          <p>以后选择这个方案时，只覆盖当前分区中仍然存在的参数；模板升级后也不会写入未知字段。</p>
        </div>
        <footer class="tube-designer-preset-dialog-footer">
          ${preset ? `<button class="tube-designer-danger" data-cam-action="tube-designer-delete-parameter-preset" data-tube-designer-preset-id="${escapeAttribute(preset.id)}" ${view?.pending ? "disabled" : ""}>删除方案</button>` : "<span></span>"}
          <button class="tube-designer-secondary" data-cam-action="tube-designer-close-preset-dialog" ${view?.pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-save-parameter-preset" data-tube-designer-preset-mode="${escapeAttribute(mode)}" data-tube-designer-preset-scope="${escapeAttribute(scopeKey)}" data-tube-designer-preset-id="${escapeAttribute(preset?.id ?? "")}" ${view?.pending ? "disabled" : ""}>${preset ? "保存修改" : "保存方案"}</button>
        </footer>
        ${renderFloatingEditorResizeHandles()}
      </section>
    </div>`;
}

function renderImportedProfileDialog(_designer, view) {
  const state = view?.tubeDesignerProfileDialog ?? {};
  const profile = state.profile ?? {};
  const savedId = String(state.savedProfileId ?? "");
  return `
    <div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop tube-floating-editor-layer" data-floating-editor-layer="imported-profile" role="presentation">
      <section class="tube-designer-preset-dialog" data-floating-editor-window="imported-profile" role="dialog" aria-modal="false" aria-labelledby="tube-designer-profile-title"
        data-window-state-controls="${savedId ? '' : '[data-tube-designer-profile-name]'}">
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
        ${renderFloatingEditorResizeHandles()}
      </section>
    </div>`;
}

function renderImportedProfileLibraryDialog(view) {
  const profiles = (Array.isArray(view?.tubeDesignerUserData?.profiles)
    ? view.tubeDesignerUserData.profiles : [])
    .slice().sort((left, right) => String(left?.name ?? "").localeCompare(String(right?.name ?? ""), "zh-CN"));
  return `
    <div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop tube-floating-editor-layer" data-floating-editor-layer="imported-profile-library" role="presentation">
      <section class="tube-designer-preset-dialog tube-designer-profile-library-dialog" data-floating-editor-window="imported-profile-library" role="dialog" aria-modal="false" aria-labelledby="tube-designer-profile-library-title">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-profile-library-title">我的管型管理</strong><span>${profiles.length} 个已保存管型 · 可重命名或删除</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-profile-library" aria-label="关闭" ${view?.pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-profile-library-list">
          ${profiles.length ? profiles.map((profile) => `
            <article class="tube-designer-profile-library-row" data-tube-designer-library-profile-row data-tube-designer-profile-id="${escapeAttribute(profile.id)}">
              <div class="tube-designer-profile-library-copy">
                <input type="text" data-tube-designer-library-profile-name data-tube-designer-profile-id="${escapeAttribute(profile.id)}" value="${escapeAttribute(profile.name ?? "")}" maxlength="120" aria-label="管型名称" ${view?.pending ? "disabled" : ""} />
                <span>${escapeText(profile.previewProfile?.specification ?? profile.specification ?? "管型截面")}</span>
                <small>${escapeText(profile.sourceFileName ?? "管型资源")} · ${profile.descriptor?.profileForm === "parametric" ? "参数可编辑" : `${Number(profile.contourCount ?? profile.contours?.length ?? 0)} 条轮廓`}</small>
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
        ${renderFloatingEditorResizeHandles()}
      </section>
    </div>`;
}

function renderTemplateGroup(group, selectedEntryId, pending, expandedGroupIds, depth = 0) {
  const availableCount = countAvailableTemplates(group);
  const expanded = expandedGroupIds.has(group.key);
  return `
    <section class="tube-designer-template-group ${expanded ? "expanded" : ""}" data-tube-designer-template-group-id="${escapeAttribute(group.key)}" data-tube-designer-template-group-depth="${depth}">
      <button type="button" class="tube-designer-template-group-toggle" data-cam-action="tube-designer-toggle-template-group" data-tube-designer-template-group-id="${escapeAttribute(group.key)}" aria-expanded="${expanded}" ${pending ? "disabled" : ""}>
        <span><i aria-hidden="true"></i><strong>${escapeText(group.title)}</strong></span><em>${availableCount} 种</em>
      </button>
      <div class="tube-designer-template-group-items" ${expanded ? "" : "hidden"}>
        ${group.children.map((child) => renderTemplateGroup(child, selectedEntryId, pending, expandedGroupIds, depth + 1)).join("")}
        ${group.templates.length ? `<div class="tube-designer-template-card-grid">${group.templates.map((item) => renderTemplateCard(
          item,
          item.catalogEntryId === selectedEntryId,
          pending,
        )).join("")}</div>` : ""}
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
    <button class="tube-designer-template-card ${selected ? "selected" : ""} ${disabled ? "disabled" : ""}" data-cam-action="tube-designer-select-template" data-tube-designer-template-id="${escapeAttribute(template?.id)}" data-tube-designer-catalog-entry-id="${escapeAttribute(template?.catalogEntryId)}" aria-pressed="${selected ? "true" : "false"}" ${pending || disabled ? "disabled" : ""}>
      <span class="tube-designer-template-schematic">${renderSchematic(template, template?.catalogParameters ?? {})}</span>
      <span><strong>${escapeText(template?.displayName ?? getTemplateDisplayName(template))}</strong><small>版本 ${escapeText(template?.version)}</small></span>
      <i aria-hidden="true"></i>
    </button>
  `;
}

export function renderBatchDisassemblyDialog(designer, view) {
  const state = view?.tubeDesignerBatchDisassemblyDialog ?? {};
  const instances = Array.isArray(designer?.instances) ? designer.instances : [];
  const templates = Array.isArray(designer?.templates) ? designer.templates : [];
  const pending = Boolean(view?.pending || state.pending);
  const unavailableIds = new Set((state.unavailableProductIds ?? []).map(String));
  const rows = instances.map((instance) => {
    const productId = String(instance.entityId ?? "");
    const unavailable = !productId || instance.modelOutdated === true || unavailableIds.has(productId);
    const unavailableReason = instance.modelOutdated === true ? "先更新模型"
      : String(state.unavailableReasons?.[productId] ?? "先更新模型");
    const status = unavailable ? { title: "暂不可拆单", detail: unavailableReason }
      : instance.partsOutdated === true ? { title: "需重新拆单", detail: "装配已修改，重新生成零件清单" }
        : instance.hasDisassembly === true ? { title: "已拆单", detail: "可重新生成零件清单" }
          : { title: "未拆单", detail: "拆单后可复尺和导出" };
    return { instance, productId, unavailable, status };
  });
  const eligibleIds = new Set(rows.filter((row) => !row.unavailable).map((row) => row.productId));
  const selected = new Set((state.selectedProductIds ?? []).map(String)
    .filter((productId) => eligibleIds.has(productId)));
  const allSelected = eligibleIds.size > 0 && selected.size === eligibleIds.size;
  const unavailableCount = rows.filter((row) => row.unavailable).length;
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-selection-dialog tube-designer-batch-disassembly-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-batch-disassembly-title" aria-busy="${pending}"
        data-window-state-controls="input[data-tube-designer-instance-id]">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-batch-disassembly-title">批量拆单</strong><span>选择产品实例，生成各实例的零件清单。</span></div>
          <button type="button" class="tube-designer-dialog-close" data-cam-action="tube-designer-batch-disassembly-close" aria-label="关闭批量拆单" ${pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-batch-disassembly-body">
          <div class="tube-designer-selection-table-wrap">
            <table class="tube-designer-selection-table" aria-label="可拆单产品实例">
              <thead><tr><th><input type="checkbox" data-cam-action="tube-designer-batch-disassembly-toggle-all" aria-label="全选可拆单实例" ${allSelected ? "checked" : ""} ${pending || !eligibleIds.size ? "disabled" : ""} /></th><th>缩略图</th><th>实例名称</th><th>生产数量</th><th>产品模板</th><th>尺寸规格</th><th>零件状态</th></tr></thead>
              <tbody>${rows.length ? rows.map(({ instance, productId, unavailable, status }) => {
                const template = getTemplateById(templates, instance.templateId);
                const parameters = instance.parameters ?? {};
                return `<tr class="${unavailable ? "is-disabled" : ""}" data-tube-designer-batch-disassembly-instance-row="${escapeAttribute(productId)}" aria-disabled="${unavailable}">
                  <td><input type="checkbox" data-cam-action="tube-designer-batch-disassembly-toggle" data-tube-designer-instance-id="${escapeAttribute(productId)}" aria-label="选择${escapeAttribute(instance.name ?? "产品实例")}" ${selected.has(productId) ? "checked" : ""} ${pending || unavailable ? "disabled" : ""} /></td>
                  <td><span class="tube-designer-table-thumbnail">${renderProductInstanceThumbnail(template, parameters) || renderSchematic(template, parameters)}</span></td>
                  <td><strong>${escapeText(instance.name)}</strong><small>${escapeText(instance.productCode)}</small></td>
                  <td>${escapeText(instance.quantity ?? 1)}</td>
                  <td>${escapeText(getTemplateDisplayName(template) || instance.templateId)}</td>
                  <td>${escapeText(formatProductDimensions(template, parameters)) || "—"}</td>
                  <td><strong>${escapeText(status.title)}</strong><small>${escapeText(status.detail)}</small></td>
                </tr>`;
              }).join("") : `<tr><td colspan="7" class="tube-designer-batch-disassembly-empty"><strong>还没有产品实例</strong><span>先添加产品，或从 Excel 导入产品。</span></td></tr>`}</tbody>
            </table>
          </div>
          ${state.error ? `<p class="tube-designer-batch-disassembly-error" role="alert">${escapeText(state.error)}</p>` : ""}
        </div>
        <footer class="tube-designer-dialog-footer">
          <span data-tube-designer-batch-disassembly-summary>已选择 ${selected.size} / ${eligibleIds.size} 个可拆单实例${unavailableCount ? ` · ${unavailableCount} 个需先更新模型` : ""}</span>
          <button type="button" class="tube-designer-secondary" data-cam-action="tube-designer-batch-disassembly-close" ${pending ? "disabled" : ""}>取消</button>
          <button type="button" class="tube-designer-primary" data-cam-action="tube-designer-batch-disassembly-confirm" ${pending || !selected.size ? "disabled" : ""}>${pending ? "正在拆单…" : "拆单"}</button>
        </footer>
      </section>
    </div>`;
}

function renderProductNestingImportSelector(designer, view) {
  const instances = designer.instances ?? [];
  const templates = designer.templates ?? [];
  const requiresDisassembly = new Set(
    (view.tubeDesignerProductsRequiringDisassembly ?? []).map(String),
  );
  const groupsByProductId = new Map(importableProductManufacturingGroups(
    designer,
    view.tubeDesignerProductsRequiringDisassembly,
  )
    .map((group) => [String(group.productEntityId), group]));
  const importableIds = new Set(groupsByProductId.keys());
  const selected = new Set((view.tubeDesignerSelectedInstanceIds ?? [])
    .map(String).filter((productId) => importableIds.has(productId)));
  const allSelected = importableIds.size > 0
    && [...importableIds].every((productId) => selected.has(productId));
  const unavailableCount = Math.max(0, instances.length - importableIds.size);
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-selection-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-disassemble-title"
        data-window-state-controls="input[data-tube-designer-instance-id]">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-disassemble-title">选择已拆单产品</strong><span>只导入产品页已经生成的制造零件；这里不会重新拆单或重新生成零件。</span></div>
          <button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-disassemble" aria-label="取消导入下料" ${view.pending ? "disabled" : ""}>×</button>
        </header>
        <div class="tube-designer-selection-table-wrap">
          <table class="tube-designer-selection-table">
            <thead><tr><th><input type="checkbox" data-cam-action="tube-designer-toggle-all-instances" ${allSelected ? "checked" : ""} ${view.pending || !importableIds.size ? "disabled" : ""} /></th><th>缩略图</th><th>实例名称</th><th>数量</th><th>模板</th><th>外尺寸</th><th>零件状态</th></tr></thead>
            <tbody>${instances.map((instance) => {
              const template = getTemplateById(templates, instance.templateId);
              const parameters = instance.parameters ?? {};
              const productId = String(instance.entityId ?? "");
              const group = groupsByProductId.get(productId);
              const importable = importableIds.has(productId);
              return `<tr class="${importable ? "" : "is-disabled"}" data-tube-designer-disassembly-instance-row="${escapeAttribute(productId)}" aria-disabled="${!importable}">
                <td><input type="checkbox" data-cam-action="tube-designer-toggle-instance" data-tube-designer-instance-id="${escapeAttribute(productId)}" ${selected.has(productId) ? "checked" : ""} ${view.pending || !importable ? "disabled" : ""} /></td>
                <td><span class="tube-designer-table-thumbnail">${renderProductInstanceThumbnail(template, parameters) || renderSchematic(template, parameters)}</span></td>
                <td><strong>${escapeText(instance.name)}</strong><small>${escapeText(instance.productCode)}</small></td>
                <td>${escapeText(instance.quantity ?? 1)}</td>
                <td>${escapeText(getTemplateDisplayName(template) || instance.templateId)}</td>
                <td>${formatNumber(parameters.width)} × ${formatNumber(parameters.height)} mm</td>
                <td>${importable
                  ? `<strong>${escapeText(group.parts.length)} 种零件</strong><small>已拆单，可直接导入</small>`
                  : `<strong>不可导入</strong><small>${requiresDisassembly.has(productId) ? "零件清单已失效，需重新拆单" : "未拆单，请先到产品页生成零件清单"}</small>`}</td>
              </tr>`;
            }).join("")}</tbody>
          </table>
        </div>
        <footer class="tube-designer-dialog-footer">
          <span>已选择 ${selected.size} / ${importableIds.size} 个已拆单实例${unavailableCount ? ` · ${unavailableCount} 个未拆单不可选` : ""}</span>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-close-disassemble" ${view.pending ? "disabled" : ""}>取消</button>
          <button class="tube-designer-primary" data-cam-action="tube-designer-confirm-disassemble" ${view.pending || !selected.size ? "disabled" : ""}>${view.pending ? "正在导入…" : "导入下料"}</button>
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
  const productExport = view.tubeDesignerBreakdownMode === "export";
  const nestingExport = view.tubeDesignerBreakdownMode === "nesting-export";
  const destinationTitle = productExport
    ? "产品零件清单 · STEP + Excel"
    : nestingExport
      ? "零件清单 · STEP + Excel"
      : "STEP + Excel 分组导出";
  return `
    <div class="tube-designer-modal-backdrop" role="presentation">
      <section class="tube-designer-breakdown-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-breakdown-title" aria-busy="${exportBusy ? "true" : "false"}"
        data-window-state-controls="input[data-tube-designer-part-id]">
        <header class="tube-designer-dialog-header">
          <div><strong id="tube-designer-breakdown-title">零件清单</strong><span data-tube-designer-breakdown-summary>${groups.length} 个产品 · ${allParts.length} 个零件 · 已选择 ${selectedCount} 个</span></div>
          ${nestingExport ? "" : `<button class="tube-designer-dialog-close" data-cam-action="tube-designer-close-breakdown" aria-label="关闭零件清单" ${exportBusy ? "disabled" : ""}>×</button>`}
        </header>
        ${renderDesignerBreakdownBody(designer, view)}
        <footer class="tube-designer-breakdown-footer${nestingExport ? " tube-designer-breakdown-footer--simple" : ""}">
          <div class="tube-designer-export-destination"><strong>${destinationTitle}</strong><span>${view.tubeDesignerExportDirectory ? `上次总目录：${escapeText(view.tubeDesignerExportDirectory)}` : "总目录生成零件清单.xlsx，每个产品创建自己的 STEP 子目录"}</span></div>
          <span data-tube-designer-export-selection-summary>将导出 ${selectedCount} 个零件</span>
          ${nestingExport
            ? `<button class="tube-designer-secondary" data-cam-action="tube-designer-close-breakdown" ${view.pending || exportBusy ? "disabled" : ""}>取消</button>`
            : productExport
              ? ""
              : ""}
          <button class="tube-designer-primary" data-cam-action="tube-designer-export-selected" data-tube-designer-export-selected ${view.pending || exportBusy || !selectedCount ? "disabled" : ""}>${exportBusy ? "正在导出…" : nestingExport ? "导出" : "选择目录并导出"}</button>
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
  return currentGroup ? renderProductPartGroup(currentGroup, selected, view, designer?.templates ?? []) : "";
}

function getBreakdownGroups(designer, view) {
  const visibleIds = new Set(view?.tubeDesignerBreakdownProductIds ?? []);
  const sourceGroups = view?.tubeDesignerBreakdownMode === "nesting-export"
    && Array.isArray(designer?.nestingGroups)
    ? designer.nestingGroups
    : (designer?.manufacturingGroups ?? []);
  return sourceGroups
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
  const sourceGroups = view?.activeAreaId === "nesting"
    && view?.tubeDesignerBreakdownMode === "nesting-export"
    && Array.isArray(designer?.nestingGroups)
    ? designer.nestingGroups
    : (designer.manufacturingGroups ?? []);
  const part = sourceGroups
    .flatMap((group) => group.parts ?? [])
    .find((item) => String(item.entityId) === partId);
  if (!part) return "";
  return `
    <div class="tube-designer-modal-backdrop tube-designer-part-inspection-backdrop" role="presentation">
      <section class="tube-designer-part-inspection-dialog" role="dialog" aria-labelledby="tube-designer-part-inspection-title">
        <header class="tube-designer-dialog-header" data-tube-inspection-window-drag data-no-window-drag>
          <div>
            <strong id="tube-designer-part-inspection-title">零件复尺 · ${escapeText(partDisplayName(part))}</strong>
          </div>
          <button type="button" class="tube-designer-dialog-close" data-cam-action="tube-designer-close-part-inspection" aria-label="关闭零件复尺" title="关闭"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 4L16 16M16 4L4 16" /></svg></button>
        </header>
        <div class="tube-designer-part-inspection-body">
          <div class="tube-designer-part-inspection-stage">
            <div class="tube-designer-part-inspection-progress" data-tube-designer-inspection-progress role="progressbar" aria-label="零件复尺处理进度" aria-valuetext="正在载入最终零件三维资源" aria-hidden="false">
              <i aria-hidden="true"></i>
            </div>
            <div class="tube-designer-part-inspection-canvas">
              <div class="tube-designer-part-inspection-viewport" data-tube-designer-part-inspection-viewport></div>
              <section class="tube-designer-inspection-dimension-tree" data-tube-inspection-dimension-tree aria-label="标尺显示设置">
                <header><div><label><input type="checkbox" data-tube-inspection-all-dimensions checked />标尺</label><span data-tube-inspection-visible-count>正在载入</span></div><button type="button" class="tube-designer-scene-specification-collapse" data-tube-inspection-tree-collapse aria-expanded="true" aria-label="收起标尺到左侧" title="收起到左侧"><span class="tube-designer-scene-specification-collapse-icon" data-tube-inspection-tree-collapse-icon aria-hidden="true">‹</span><span class="tube-designer-scene-specification-collapse-label" data-tube-inspection-tree-collapse-label hidden>标尺</span></button></header>
                <div data-tube-inspection-dimension-categories></div>
              </section>
              <div class="tube-designer-inspection-state" data-tube-designer-inspection-status role="status">正在载入零件三维资源…</div>
            </div>
          </div>
          <aside class="tube-designer-measurement-panel">
            <section class="tube-designer-automatic-dimension-section">
              <div class="tube-designer-measurement-heading">
                <div><strong>自动尺寸</strong><span>单位：毫米</span></div>
              </div>
              <div data-tube-designer-automatic-dimensions>
                <div class="tube-designer-dimension-empty">正在读取零件尺寸…</div>
              </div>
            </section>
          </aside>
        </div>
        ${["n", "ne", "e", "se", "s", "sw", "w", "nw"].map((direction) => `<span class="tube-designer-inspection-resize is-${direction}" data-tube-inspection-resize="${direction}" data-no-window-drag aria-hidden="true"></span>`).join("")}
      </section>
    </div>`;
}

function renderProductPartGroup(group, selected, view, templates = []) {
  const parameters = group.parameters ?? {};
  const template = getTemplateById(templates, group.templateId);
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
      <td>${escapeText(formatProductDimensions(template, parameters))}</td>
      <td><span class="tube-designer-tree-product-thumbnail">${renderProductInstanceThumbnail(template, parameters) || renderSchematic(template, parameters)}</span></td>
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

function renderSchematic(template, parameters = {}, className = "") {
  const asset = getTemplateVisualAsset(template, "schematic")
    || getTemplateVisualAsset(template, "icon");
  if (asset) {
    return `<img class="tube-designer-schematic ${escapeAttribute(className)}" src="${escapeAttribute(asset)}" alt="产品示意图" />`;
  }
  // The fallback is intentionally template-agnostic. A template owns its
  // real artwork in resource/schematic.svg; missing artwork must not make the
  // product editor depend on a central ID/name switch.
  return `<svg class="tube-designer-schematic ${escapeAttribute(className)}" viewBox="0 0 100 100" role="img" aria-label="产品示意图"><rect class="placeholder" x="24" y="14" width="52" height="72" rx="4" /><path class="plus" d="M50 38v24M38 50h24" /></svg>`;
}

function renderMultiFaceSchematic(faceType, parameters = {}) {
  if (faceType === "five") {
    return renderFiveFaceSchematic(parameters);
  }
  let points;
  if (faceType === "two") {
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
    if (faceType === "two") {
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
    `;
}

function formatProductDimensions(template, parameters = {}) {
  const summary = template?.display?.shared?.instanceSummary;
  const selected = (Array.isArray(summary) ? summary : []).find(entry =>
    matchesParameterCondition(entry.visibleWhen, parameters));
  if (selected) return catalogText(selected.text).replace(/\{(\w+)(?::(choice))?\}/g,
    (_token, key, format) => {
      if (format === "choice") {
        const field = (template.parameters ?? []).find(field => String(field.key ?? field.name) === key);
        const choice = (field?.choices ?? field?.options ?? []).find(choice => Object.is(choice.value, parameters[key]));
        return catalogText(choice?.displayName ?? choice?.label, String(parameters[key] ?? ""));
      }
      return formatNumber(parameters[key]);
    });
  return productPrimaryDimensions(template, parameters).map(entry => `${entry.label} ${entry.value}`).join(" · ");
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

function renderParametricProfileParameters(profile, prefix, disabled, context) {
  if (profile?.profileForm !== "parametric") return "";
  const selector = context.template.parameters.find(field => isProductProfileField(field) && productProfileRole(field) === prefix);
  const bindings = productProfileParameterBindings(context.template, selector, profile)
    .filter(binding => parameterVisible(binding.field, context.values));
  if (!bindings.length) return "";
  const unit = bindings[0].field.unit;
  return `<div class="tube-designer-parametric-profile-parameters" data-profile-parameter-scope>
    <strong>截面尺寸${unit ? `（${escapeText(unit)}）` : ""}</strong>
    <div class="tube-designer-field-grid">${bindings.map(({ field, label, resourceParameter, productParameter }) => {
      const displayField = applyTemplateFieldDisplay(context.template, [field], "right")[0];
      const value = context.values[productParameter] ?? field.defaultValue;
      const name = escapeAttribute(productParameter);
      return `<label class="tube-designer-field is-number${fieldDisplayClass(displayField)}"${fieldDisplayStyle(displayField)}><span>${escapeText(label)}</span><input type="number" value="${escapeAttribute(value)}" step="${escapeAttribute(field.step ?? "any")}" ${field.min != null ? `min="${field.min}"` : ""} ${field.max != null ? `max="${field.max}"` : ""} data-cam-change-action="tube-designer-parameter-change" data-tube-designer-parameter="${name}" data-product-parameter-key="${name}" data-profile-parameter-key="${escapeAttribute(resourceParameter)}" ${disabled || !parameterEnabled(field, context.values) ? "disabled" : ""} /></label>`;
    }).join("")}</div>
  </div>`;
}

function renderProfileField(field, value, disabled, context) {
  const key = String(field.key ?? field.name ?? ""), prefix = productProfileRole(field);
  const override = getProfileOverrides(context?.values)[prefix];
  const error = override ? productProfileConstraintError(override, field) : "";
  const available = libraryProfiles(context?.view ?? {}).filter(profile => profile.available !== false && profileAllowedForProductField(profile, field));
  const selected = override ? (override.profileScope === "system" ? `system:${override.profileDefinitionId}`
    : `user:${override.savedProfileId ?? override.profileDefinitionId}`) : productProfileDefaultSelection(context.template, field, context.values);
  const known = available.some(profile => profileSelectionKey(profile) === selected);
  const mode = context?.mode === "add" ? "add" : "right";
  return `<div class="tube-designer-field tube-designer-profile-field wide${fieldDisplayClass(field)}"${fieldDisplayStyle(field)}>
    <span>选用管型</span>
    <select data-cam-change-action="tube-designer-profile-selection-change" data-tube-designer-profile-prefix="${escapeAttribute(prefix)}" data-tube-designer-profile-parameter="${escapeAttribute(key)}" data-tube-designer-profile-mode="${mode}" data-tube-designer-profile-current-selection="${escapeAttribute(selected)}" data-product-parameter-key="${escapeAttribute(key)}" ${disabled ? "disabled" : ""}>
      ${!known ? '<option value="" selected disabled>请选择可用管型</option>' : ""}
      ${[ ["system", "系统内置"], ["user", "我的"] ].map(([scope, title]) => {
        const profiles = available.filter(profile => profileScope(profile) === scope);
        return profiles.length ? `<optgroup label="${title}">${profiles.map(profile => {
          const source = profileSelectionKey(profile);
          return `<option value="${escapeAttribute(source)}" ${source === selected ? "selected" : ""}>${escapeText(profileName(profile))}</option>`;
        }).join("")}</optgroup>` : "";
      }).join("")}
    </select>
    ${error ? `<small role="alert">${escapeText(error)}</small>` : ""}
    ${renderParametricProfileParameters(override, prefix, disabled, context)}
  </div>`;
}

function renderProductOptionField(field, disabled, context) {
  const mode = context?.mode === "add" ? "add" : "right";
  const binding = productToolBinding(context?.values, field);
  const selectedKey = String(binding?.selectionKey ?? context?.values?.[field?.key ?? field?.name] ?? "");
  const options = Array.isArray(field?.presentation?.productOptions)
    ? field.presentation.productOptions : [];
  const known = options.some((option) => String(option?.value ?? "") === selectedKey);
  const label = field.presentation?.editor === "product-option" ? field.displayName ?? field.label : "转角样式";
  return `<label class="tube-designer-field wide is-choice${fieldDisplayClass(field)}"${fieldDisplayStyle(field)} data-product-option-editor="${escapeAttribute(field.key ?? field.name)}"><span>${escapeText(label)}</span><select data-cam-change-action="tube-designer-product-tool-selection-change" data-tube-designer-tool-mode="${mode}" data-tube-designer-tool-field="${escapeAttribute(field.key ?? field.name)}" data-product-parameter-key="${escapeAttribute(field.key ?? field.name)}" ${disabled ? "disabled" : ""}>${!known ? '<option value="" selected>沿用当前转角</option>' : ""}${options.map((option) => `<option value="${escapeAttribute(option.value)}" ${String(option.value) === selectedKey ? "selected" : ""}>${escapeText(catalogText(option.displayName, option.label ?? "转角"))}</option>`).join("")}</select></label>`;
}

function renderProductControlField(field, disabled, context) {
  const mode = context?.mode === "add" ? "add" : "right";
  const value = productControlValue(context?.template, field, context?.values ?? {});
  const options = productControlChoices(context?.template, field, context?.values ?? {});
  return `<label class="tube-designer-field wide is-line-full is-choice${fieldDisplayClass(field)}"${fieldDisplayStyle(field)} data-product-control-editor="${escapeAttribute(field.key)}"><span>${escapeText(field.displayName)}</span><select data-cam-change-action="tube-designer-product-control-change" data-product-control-key="${escapeAttribute(field.key)}" data-product-control-mode="${mode}" data-product-parameter-key="${escapeAttribute(field.key)}" ${disabled ? "disabled" : ""}>${value === "" ? '<option value="" selected>沿用当前做法</option>' : ""}${options.map((choice) => `<option value="${escapeAttribute(choice.value)}" ${choice.value === value ? "selected" : ""} ${choice.disabled ? "disabled" : ""}>${escapeText(choice.label)}</option>`).join("")}</select></label>`;
}

function fieldReplacedBySelectedProfile(field, context) {
  const key = String(field?.key ?? field?.name ?? "");
  if (!key) return false;
  const overrides = getProfileOverrides(context?.values);
  for (const selector of context?.template?.parameters ?? []) {
    const selectorKey = String(selector?.key ?? selector?.name ?? "");
    if (!isProductProfileField(selector)) continue;
    const role = productProfileRole(selector);
    const override = overrides[role];
    if (selectorKey === key) continue;
    const declaration = productProfileRoleDeclaration(context.template, selector);
    if (!(declaration.managedParameters ?? []).includes(key)) continue;
    if (override) return true;
    const kind = context.values?.[selectorKey];
    if (!Object.values(declaration.parameterBindingsBySectionKind?.[kind] ?? {}).includes(key)) return true;
  }
  return false;
}

function renderField(field, value, disabled = false, context = null) {
  if (fieldReplacedBySelectedProfile(field, context)) return "";
  if (field.presentation?.editor === "product-control") {
    return renderProductControlField(field, disabled || !parameterEnabled(field,
      productControlEffectiveValues(context?.template, context?.values ?? {})), context);
  }
  // A fixed physical section kind still permits choosing a library resource
  // of that supported kind and editing the product's declared dimensions.
  if (isProductProfileField(field)) return renderProfileField(field, value,
    disabled || !matchesParameterCondition(field.enabledWhen, context?.values ?? {}), context);
  disabled = disabled || !parameterEnabled(field, context?.values ?? {});
  if (field.presentation?.editor === "component-model") return renderComponentModelField(field, value, disabled, context);
  if (isProductToolField(field)) return renderProductOptionField(field, disabled, context);
  const name = escapeAttribute(field.key ?? field.name);
  const label = escapeText(field.displayName ?? field.label);
  const displayClass = fieldDisplayClass(field);
  const displayStyle = fieldDisplayStyle(field);
  if (context?.summaryOnly) {
    const selected = (Array.isArray(field.options) ? field.options : []).find((option) => {
      const optionValue = typeof option === "object" ? option?.value : option;
      return String(optionValue) === String(value);
    });
    const selectedLabel = selected == null ? null
      : typeof selected === "object" ? selected?.label : selected;
    const shownValue = field.type === "boolean" || field.valueType === "boolean"
      ? (value === true || value === "true" || value === "是" ? "是" : "否")
      : selectedLabel ?? value ?? "—";
    const unit = field.unit && shownValue !== "—" ? ` ${field.unit}` : "";
    return `<div class="tube-designer-field tube-designer-parameter-summary${displayClass}"${displayStyle} data-tube-designer-parameter-summary="${name}"><span>${label}</span><strong>${escapeText(`${shownValue}${unit}`)}</strong></div>`;
  }
  if (field.presentation?.editor === "profile-library"
      || (field.type === "select" && String(field.key ?? field.name ?? "").endsWith("ProfileType"))) {
    return renderProfileField(field, value, disabled, context);
  }
  if (field.type === "readonly") {
    return `<label class="tube-designer-field is-string${displayClass}"${displayStyle}>${label}<input type="text" data-tube-designer-parameter="${name}" data-product-parameter-key="${name}" value="${escapeAttribute(value)}" readonly /></label>`;
  }
  if (field.type === "boolean" || field.valueType === "boolean") {
    return `<label class="tube-designer-field tube-designer-boolean-field${displayClass}"${displayStyle}><span>${label}</span><input type="checkbox" data-tube-designer-parameter="${name}" data-product-parameter-key="${name}" data-cam-change-action="tube-designer-parameter-change" ${value === true || value === "true" || value === "是" ? "checked" : ""} ${disabled ? "disabled" : ""} /></label>`;
  }
  if (field.type === "select") {
    const options = availableParameterChoices(field, context?.values ?? {});
    const selectedValue = effectiveParameterChoice(field, value, context?.values ?? {});
    const visibleOptions = options;
    return `<label class="tube-designer-field wide is-choice${displayClass}"${displayStyle}>${label}<select data-tube-designer-parameter="${name}" data-product-parameter-key="${name}" data-cam-change-action="tube-designer-parameter-change" ${disabled || field.readOnly ? "disabled" : ""}>${visibleOptions.map((option) => {
      const optionValue = typeof option === "object" ? option?.value : option;
      const optionLabel = typeof option === "object" ? option?.label : option;
      return `<option value="${escapeAttribute(optionValue)}" data-tube-designer-value-type="${typeof optionValue}" ${String(optionValue) === String(selectedValue) ? "selected" : ""}>${escapeText(optionLabel)}</option>`;
    }).join("")}</select></label>`;
  }
  const type = field.type === "text" ? "text" : "number";
  const attributes = [
    `type="${type}"`,
    `data-tube-designer-parameter="${name}"`,
    `data-product-parameter-key="${name}"`,
    `data-cam-change-action="tube-designer-parameter-change"`,
    `value="${escapeAttribute(value)}"`,
  ];
  if (type === "number") attributes.push(`step="${escapeAttribute(field.step ?? (field.type === "integer" ? 1 : "any"))}"`);
  if (field.min != null) attributes.push(`min="${escapeAttribute(field.min)}"`);
  if (field.max != null) attributes.push(`max="${escapeAttribute(field.max)}"`);
  if (disabled) attributes.push("disabled");
  return `<label class="tube-designer-field ${field.type === "text" ? "wide is-string" : "is-number"}${displayClass}"${displayStyle}>${label}<input ${attributes.join(" ")} /></label>`;
}

function visibleFields(fields, values) {
  return (Array.isArray(fields) ? fields : []).filter((field) => parameterVisible(field, values));
}

function matchesVisibility(condition, values) {
  return matchesParameterCondition(condition, values);
}

function buildParameterGroupTree(template, fields, mode = "right") {
  fields = applyTemplateFieldDisplay(template, fields, mode);
  const toolGroups = new Set(fields.filter(isProductToolField)
    .map((field) => String(field.groupKey ?? field.group ?? "").trim()));
  const displayGroups = templateDisplayView(template, mode).groups;
  const nodes = new Map();
  const descriptors = Array.isArray(template?.groups) ? template.groups : [];
  const descriptorsByKey = new Map(descriptors.map((descriptor) => [
    String(descriptor?.key ?? ""), descriptor,
  ]));
  const groupOrders = new Map(descriptors.map((descriptor, index) => {
    const key = String(descriptor?.key ?? "");
    const displayOrder = Number(displayGroups?.[key]?.order);
    return [key, Number.isFinite(displayOrder) ? displayOrder : Number(descriptor?.order ?? index)];
  }));
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
      const node = nodes.get(key);
      node.order = Math.min(node.order, order);
      if (title) node.title = title;
      if (parentKey) node.parentKey = parentKey;
    }
    return nodes.get(key);
  };

  // Group descriptors may form a hierarchy.  This keeps broad product
  // semantics (structure / dimensions) separate while retaining useful
  // business subgroups such as grids, posts and infill.
  descriptors.forEach((descriptor, index) => {
    const groupKey = String(descriptor?.key ?? "").trim();
    if (!groupKey) return;
    const parentGroupKey = String(descriptor?.parentKey ?? "").trim();
    const node = ensureNode(
      `group:${groupKey}`,
      localizedProfileText(descriptor?.displayName, groupKey),
      parentGroupKey ? `group:${parentGroupKey}` : "",
      groupOrders.get(groupKey) ?? index,
    );
    if (typeof displayGroups?.[groupKey]?.defaultOpen === "boolean") {
      node.defaultOpen = displayGroups[groupKey].defaultOpen;
    }
    const sectionGroup = String(displayGroups?.[groupKey]?.sectionGroup ?? "").trim();
    if (sectionGroup) node.uiDisplay = { ...(node.uiDisplay ?? {}), sectionGroup };
  });

  (Array.isArray(fields) ? fields : []).forEach((field, fieldIndex) => {
    let displayPath = String(field?.displayName ?? field?.label ?? field?.key ?? "参数")
      .split("/").map((part) => part.trim()).filter(Boolean);
    // Group hierarchy is declared by the template, labels do not add a second hierarchy.
    displayPath = [displayPath.at(-1)];
    const groupKey = String(field?.groupKey ?? "").trim();
    const groupOrder = groupOrders.get(groupKey) ?? fieldIndex;
    const descriptor = descriptorsByKey.get(groupKey);
    const declaredTitle = localizedProfileText(descriptor?.displayName, String(field?.group ?? "参数"));
    const title = toolGroups.has(groupKey) && /模具|槽口|单件工艺/.test(declaredTitle) ? "连接做法" : declaredTitle;
    const baseKey = `group:${groupKey || title}`;
    const parentGroupKey = String(descriptor?.parentKey ?? "").trim();
    const baseNode = ensureNode(baseKey, title, parentGroupKey ? `group:${parentGroupKey}` : "", groupOrder);
    if (displayPath.length > 1) {
      let parentKey = baseKey;
      displayPath.slice(0, -1).forEach((title, depth, groupPath) => {
        const key = `path:${baseKey}:${groupPath.slice(0, depth + 1).join("/")}`;
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
    baseNode.fields.push({ ...field, displayName: displayPath.at(-1), label: displayPath.at(-1) });
  });

  for (const node of nodes.values()) {
    // Sort only within a group. Stable sorting preserves descriptor order for ties.
    const fieldOrder = (field) => typeof field?.order === "number" && Number.isFinite(field.order)
      ? field.order : Infinity;
    node.fields.sort((left, right) => {
      const a = fieldOrder(left), b = fieldOrder(right);
      return a === b ? 0 : a < b ? -1 : 1;
    });
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

  const sorted = sortNodes(roots).map(pruneNode).filter(Boolean);
  const layoutSections = parameterLayoutSections(template);
  if (layoutSections.length) {
    const descendantGroupKeys = (group) => [
      group.key.replace(/^group:/, ""),
      ...group.children.flatMap(descendantGroupKeys),
    ];
    const selectedRoots = (section) => sorted.filter((group) =>
      section.groups.includes(group.key.replace(/^group:/, "")));
    const assignedGroups = new Set(layoutSections.flatMap((section) =>
      selectedRoots(section).flatMap(descendantGroupKeys)));
    const productKind = (field) => {
      if (mode !== "right") return productParameterKind(field, template);
      const fieldGroup = String(field?.groupKey ?? field?.group ?? "").trim();
      const declared = String(field?.uiDisplay?.category ?? displayGroups?.[fieldGroup]?.category ?? "").trim();
      if (["structure", "specifications"].includes(declared)) return declared;
      return productParameterKind(field, template) === "dimension" ? "specifications" : "structure";
    };
    const filterByProductKind = (node, kind) => {
      const children = node.children.map((child) => filterByProductKind(child, kind)).filter(Boolean);
      const matchingFields = node.fields.filter((field) => productKind(field) === kind);
      if (!matchingFields.length && !children.length) return null;
      return {
        ...node,
        key: `product-kind:${kind}:${node.key}`,
        fields: matchingFields,
        children,
      };
    };
    const result = layoutSections.flatMap((section) => {
      const sectionRoots = selectedRoots(section);
      if (section.key !== "product") {
        const collectFields = (group) => [...group.fields, ...group.children.flatMap(collectFields)];
        const sectionFields = sectionRoots.flatMap(collectFields);
        const flatControls = section.key === "process"
          && sectionFields.some((field) => field.presentation?.editor === "product-control");
        return [{
          key: `section:${section.key}`,
          title: section.title,
          fields: flatControls ? sectionFields.map((field) => ({
            ...field, uiDisplay: { ...field.uiDisplay, line: "full" },
          })) : [],
          children: flatControls ? [] : sectionRoots,
          defaultOpen: section.defaultOpen,
        }];
      }
      if (mode === "right") return [
        {
          key: "section:structure",
          title: "结构",
          fields: [],
          children: sectionRoots.map((group) => filterByProductKind(group, "structure")).filter(Boolean),
          defaultOpen: section.defaultOpen,
        },
        {
          key: "section:specifications",
          title: "规格",
          fields: [],
          children: sectionRoots.map((group) => filterByProductKind(group, "specifications")).filter(Boolean),
          defaultOpen: false,
        },
      ];
      return [
        {
          key: "section:structure",
          title: "结构参数",
          fields: [],
          children: sectionRoots.map((group) => filterByProductKind(group, "structure")).filter(Boolean),
          defaultOpen: section.defaultOpen,
        },
        {
          key: "section:dimensions",
          title: "尺寸参数",
          fields: [],
          children: sectionRoots.map((group) => filterByProductKind(group, "dimension")).filter(Boolean),
          defaultOpen: false,
        },
      ];
    }).filter((section) => section.fields.length || section.children.length);
    const unassigned = sorted.filter((group) => !assignedGroups.has(group.key.replace(/^group:/, "")));
    if (unassigned.length) {
      if (result.length) result[0].children.push(...unassigned);
      else return unassigned;
    }
    return result;
  }
  return sorted;
}

function renderEditorParameterLevels(definitions, render, context, key) {
  const saved = context?.mode === 'add'
    ? context?.view?.tubeDesignerAddDisclosureStates?.[context?.template?.id]
    : context?.view?.tubeDesignerParameterDisclosureState;
  return renderParameterLevels(definitions, render, {key,gridClass:'tube-designer-field-grid',trackDisclosure:true,open:saved?.[`advanced:${key}`] === true});
}

function renderAddParameterGroup(group, values, disabled, view, mode, template, presetHostGroups = new Map(), depth = 0, siblingIndex = 0) {
  const fields = renderEditorParameterLevels(group.fields, (field) => renderField(
    field,
    values[field.key ?? field.name],
    disabled,
    { values, view, mode, template },
  ), { view, mode, template }, group.key);
  const children = group.children.map((child, index) => renderAddParameterGroup(
    child,
    values,
    disabled,
    view,
    mode,
    template,
    presetHostGroups,
    depth + 1,
    index,
  )).join("");
  const presetScopeKey = presetHostGroups.get(group.key);
  const presetBar = presetScopeKey !== undefined
    ? renderParameterPresetBar(template, values, view, mode, presetScopeKey) : "";
  if (depth || parameterLayoutSections(template).length) {
    const savedOpen = view.tubeDesignerAddDisclosureStates?.[template?.id]?.[group.key];
    return `<details class="tube-designer-config-subsection" data-tube-designer-parameter-group="${escapeAttribute(group.key)}" data-tube-designer-group-depth="${depth}" ${(savedOpen ?? group.defaultOpen ?? siblingIndex === 0) ? "open" : ""}>
      <summary><span>${escapeText(group.title)}</span><small>${countParameterGroupFields(group)} 项</small></summary>
      <div class="tube-designer-config-subsection-content">
        ${presetBar}
        ${fields ? `<div class="tube-designer-field-grid">${fields}</div>` : ""}
        ${children ? `<div class="tube-designer-subsection-list">${children}</div>` : ""}
      </div>
    </details>`;
  }
  return `<section class="tube-designer-section" data-tube-designer-group-depth="${depth}">
    <strong>${escapeText(group.title)}</strong>
    ${presetBar}
    ${fields ? `<div class="tube-designer-field-grid">${fields}</div>` : ""}
    ${children ? `<div class="tube-designer-subsection-list">${children}</div>` : ""}
  </section>`;
}

function compactParameterGroup(group, values, disabled, expandedGroups, view, mode, template, presetHostGroups = new Map(), depth = 0, summaryOnly = false) {
  const groupSummaryOnly = summaryOnly || (mode === "right" && depth === 0 && group.key === "section:structure");
  const fields = renderEditorParameterLevels(group.fields, (field) => renderField(
    field,
    values[field.key ?? field.name],
    disabled,
    { values, view, mode, template, summaryOnly: groupSummaryOnly },
  ), { view, mode, template }, group.key);
  const children = group.children.map((child) => compactParameterGroup(
    child,
    values,
    disabled,
    expandedGroups,
    view,
    mode,
    template,
    presetHostGroups,
    depth + 1,
    groupSummaryOnly,
  )).join("");
  const presetScopeKey = groupSummaryOnly ? undefined : presetHostGroups.get(group.key);
  const presetBar = presetScopeKey !== undefined
    ? renderParameterPresetBar(template, values, view, mode, presetScopeKey) : "";
  const itemCount = countParameterGroupFields(group);
  const className = depth ? "tube-designer-parameter-subsection" : "tube-designer-parameter-section";
  return `<details class="${className}" data-tube-designer-parameter-group="${escapeAttribute(group.key)}" data-tube-designer-group-depth="${depth}" ${expandedGroups.has(group.key) ? "open" : ""}>
    <summary><span>${escapeText(group.title)}</span><small>${itemCount} 项</small></summary>
    <div class="tube-designer-parameter-group-content">
      ${presetBar}
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
  const declared = new Set();
  let hasDeclaredState = false;
  const collectDeclared = (group, ancestors = []) => {
    if (typeof group.defaultOpen === "boolean") {
      hasDeclaredState = true;
      if (group.defaultOpen) [...ancestors, group.key].forEach((key) => declared.add(key));
    }
    group.children.forEach((child) => collectDeclared(child, [...ancestors, group.key]));
  };
  groupTree.forEach((group) => collectDeclared(group));
  if (hasDeclaredState) return [...declared];
  const firstRoot = groupTree[0];
  if (!firstRoot) return [];
  const expanded = [firstRoot.key];
  const addFirstChild = (group) => {
    const firstChild = group.children[0];
    if (!firstChild) return;
    expanded.push(firstChild.key);
    addFirstChild(firstChild);
  };
  addFirstChild(firstRoot);
  return expanded;
}

function sceneLogicalGroupKey(group) {
  const key = String(group?.key ?? "");
  const marker = key.lastIndexOf("group:");
  return (marker >= 0 ? key.slice(marker + 6) : key).split(":")[0].trim().toLowerCase();
}

function isSceneIdentityGroup(group) {
  return ["identity", "product_identity", "identification"].includes(sceneLogicalGroupKey(group));
}

function isSceneIdentityField(field, group, template) {
  const fieldGroup = String(field?.groupKey ?? field?.group ?? "").trim().toLowerCase();
  const declaredCode = String(template?.extensions?.productIdentity?.codeParameter ?? "");
  return declaredCode && String(field?.key ?? field?.name ?? "") === declaredCode
    || isSceneIdentityGroup(group)
    || ["identity", "product_identity", "identification"].includes(fieldGroup);
}

function collectSceneIdentityFields(groupTree, template) {
  const fields = [];
  const visit = (group) => {
    for (const field of group?.fields ?? []) {
      if (isSceneIdentityField(field, group, template)) fields.push(field);
    }
    for (const child of group?.children ?? []) visit(child);
  };
  for (const group of groupTree ?? []) visit(group);
  return fields;
}

function removeSceneIdentityFields(groupTree, template) {
  const prune = (group) => {
    if (isSceneIdentityGroup(group)) return null;
    const fields = (group?.fields ?? []).filter((field) => !isSceneIdentityField(field, group, template));
    const children = (group?.children ?? []).map(prune).filter(Boolean);
    return fields.length || children.length ? { ...group, fields, children } : null;
  };
  return (groupTree ?? []).map(prune).filter(Boolean);
}

function buildSceneParameterSections(template, groupTree) {
  const materialsTitle = parameterLayoutSections(template).find((section) => section.key === "materials")?.title ?? "材料";
  const processTitle = parameterLayoutSections(template).find((section) => section.key === "process")?.title ?? "连接做法";
  const sections = [
    { key: "structure", title: "结构", defaultOpen: true },
    { key: "specifications", title: "规格", defaultOpen: false },
    { key: "materials", title: materialsTitle, defaultOpen: true },
    { key: "process", title: processTitle, defaultOpen: true },
  ];
  const sourceGroups = Array.isArray(groupTree) ? groupTree : [];
  const sectionGroupDescriptors = templateDisplayView(template, "right").sectionGroups;
  const declaredSectionGroups = Object.entries(sectionGroupDescriptors ?? {}).flatMap(([key, value]) => {
    if (!key || !value || typeof value !== "object") return [];
    const order = Number(value.order);
    return [{
      key,
      title: localizedProfileText(value.title ?? value.displayName, key),
      order: Number.isFinite(order) ? order : 1000,
      defaultOpen: value.defaultOpen === true,
    }];
  }).sort((left, right) => left.order - right.order);
  const copyForSectionGroup = (group, requestedSectionGroup = "", inheritedSectionGroup = "") => {
    const groupSectionGroup = String(group?.uiDisplay?.sectionGroup ?? inheritedSectionGroup).trim();
    const fields = (group.fields ?? []).filter((field) => {
      const fieldSectionGroup = String(field?.uiDisplay?.sectionGroup ?? groupSectionGroup).trim();
      return fieldSectionGroup === requestedSectionGroup;
    });
    const children = (group.children ?? []).map((child) => copyForSectionGroup(
      child, requestedSectionGroup, groupSectionGroup,
    )).filter(Boolean);
    if (!fields.length && !children.length) return null;
    return {
      ...group,
      key: `section-group:${requestedSectionGroup || "main"}:${group.key}`,
      fields,
      children,
    };
  };
  const arrangeSectionGroups = (children, sectionKey) => {
    const direct = children.map((group) => copyForSectionGroup(group)).filter(Boolean);
    const nested = declaredSectionGroups.flatMap((sectionGroup) => {
      const grouped = children.map((group) => copyForSectionGroup(group, sectionGroup.key)).filter(Boolean);
      return grouped.length ? [{
        key: `scene:${sectionKey}:section-group:${sectionGroup.key}`,
        title: sectionGroup.title,
        order: sectionGroup.order,
        fields: [],
        children: grouped,
        defaultOpen: sectionGroup.defaultOpen,
      }] : [];
    });
    return [...direct, ...nested];
  };
  const moved = [];
  const keepCategory = (group, sectionKey) => {
    const fields = (group.fields ?? []).filter(field => {
      const category = field.uiDisplay?.category;
      if (category && category !== sectionKey) { moved.push({ field, category, group }); return false; }
      return true;
    });
    return { ...group, fields, children: (group.children ?? []).map(child => keepCategory(child, sectionKey)) };
  };
  const arranged = sourceGroups.map(group => keepCategory(group, group.key.replace(/^section:/, "")));
  for (const { field, category, group } of moved) {
    let section = arranged.find(item => item.key === `section:${category}`);
    if (!section) { section = { key: `section:${category}`, fields: [], children: [] }; arranged.push(section); }
    let target = section.children.find(item => item.key === `moved:${group.key}`);
    if (!target) { target = { ...group, key: `moved:${group.key}`, fields: [], children: [] }; section.children.push(target); }
    target.fields.push(field);
  }
  const existingSections = new Map(arranged.map((group) => [String(group?.key ?? ""), group]));
  if (sections.some((section) => existingSections.has(`section:${section.key}`))) {
    return sections.map((section) => {
      const group = existingSections.get(`section:${section.key}`);
      // Product connection controls are flat fields in the assembly section.
      // The display declaration can arrange them by the part being assembled,
      // while retaining one line per field and their original DOM identities.
      const processFields = section.key === "process" ? group?.fields ?? [] : [];
      const displayGroups = templateDisplayView(template, "right").groups;
      const partKey = (field) => String(field.uiDisplay?.sectionGroup
        ?? displayGroups?.[field.groupKey ?? field.group]?.sectionGroup ?? "").trim();
      const partGroups = declaredSectionGroups.flatMap((part) => {
        const fields = processFields.filter((field) => partKey(field) === part.key)
          .sort((left, right) => (Number.isFinite(left.order) ? left.order : Infinity)
            - (Number.isFinite(right.order) ? right.order : Infinity));
        return fields.length ? [{
          key: `scene:process:section-group:${part.key}`,
          title: part.title,
          fields,
          children: [],
          defaultOpen: part.defaultOpen,
        }] : [];
      });
      const assignedParts = new Set(declaredSectionGroups.map((part) => part.key));
      return group
        ? {
          ...group,
          title: section.title,
          fields: partGroups.length ? processFields.filter((field) => !assignedParts.has(partKey(field))) : group.fields,
          children: partGroups.length ? partGroups : arrangeSectionGroups(group.children ?? [], section.key),
          defaultOpen: group.defaultOpen ?? section.defaultOpen,
        }
        : { key: `section:${section.key}`, title: section.title, fields: [], children: [], defaultOpen: section.defaultOpen };
    });
  }

  const configuredCategories = new Map();
  for (const section of parameterLayoutSections(template)) {
    const category = section.key === "materials" ? "materials"
      : section.key === "process" ? "process"
        : section.key === "specifications" || section.key === "dimensions" ? "specifications"
          : section.key === "structure" ? "structure" : "";
    if (!category) continue;
    for (const groupKey of section.groups) configuredCategories.set(groupKey, category);
  }
  const groupKeyOf = (group) => String(group?.key ?? "").split("group:").at(-1).split(":")[0];
  const categoryFor = (field, group) => {
    const fieldGroup = String(field?.groupKey ?? field?.group ?? groupKeyOf(group)).trim();
    if (configuredCategories.has(fieldGroup)) return configuredCategories.get(fieldGroup);
    if (/(profile|material|surface|finish|coating)/i.test(fieldGroup)) return "materials";
    if (/(process|assembly|install|groove|weld|bend|punch|mold|tool)/i.test(fieldGroup)) return "process";
    const declared = String(field?.uiDisplay?.category ?? "").trim();
    if (["structure", "specifications", "materials", "process"].includes(declared)) return declared;
    return productParameterKind(field, template) === "dimension" ? "specifications" : "structure";
  };
  const copyForCategory = (group, category, requestedSectionGroup = "", inheritedSectionGroup = "") => {
    const groupSectionGroup = String(group?.uiDisplay?.sectionGroup ?? inheritedSectionGroup).trim();
    const fields = (group.fields ?? []).filter((field) => {
      const fieldSectionGroup = String(field?.uiDisplay?.sectionGroup ?? groupSectionGroup).trim();
      return categoryFor(field, group) === category && fieldSectionGroup === requestedSectionGroup;
    });
    const children = (group.children ?? []).map((child) => copyForCategory(
      child, category, requestedSectionGroup, groupSectionGroup,
    )).filter(Boolean);
    if (!fields.length && !children.length) return null;
    return { ...group, key: `scene:${category}:${group.key}`, fields, children };
  };
  return sections.map((section) => {
    const directChildren = sourceGroups.map((group) => copyForCategory(group, section.key)).filter(Boolean);
    const groupedChildren = declaredSectionGroups.flatMap((sectionGroup) => {
      const children = sourceGroups.map((group) => copyForCategory(
        group, section.key, sectionGroup.key,
      )).filter(Boolean);
      return children.length ? [{
        key: `scene:${section.key}:section-group:${sectionGroup.key}`,
        title: sectionGroup.title,
        order: sectionGroup.order,
        fields: [],
        children,
        defaultOpen: sectionGroup.defaultOpen,
      }] : [];
    });
    return {
      key: `section:${section.key}`,
      title: section.title,
      fields: [],
      children: [...directChildren, ...groupedChildren],
      defaultOpen: section.defaultOpen,
    };
  });
}

function scheduleDesignerParameterPanelRestoration(context, view, productId, hasSavedPanelState) {
  if (!hasSavedPanelState) return;
  const restorationToken = Number(view.tubeDesignerParameterPanelRestorationToken ?? 0) + 1;
  view.tubeDesignerParameterPanelRestorationToken = restorationToken;
  queueMicrotask(() => {
    if (view.tubeDesignerParameterPanelRestorationToken !== restorationToken) return;
    if (String(view.scene?.tubeDesigner?.product?.entityId ?? "") !== String(productId ?? "")) return;
    const panel = context.mount?.querySelector?.("[data-tube-designer-parameter-form]");
    const scroller = panel?.querySelector?.("[data-tube-designer-parameter-scroll]")
      ?? panel?.querySelector?.(".tube-designer-parameter-sections");
    if (!scroller) return;
    const detailScroller = panel?.querySelector?.("[data-tube-designer-product-detail-scroll]");
    if (detailScroller) {
      detailScroller.scrollTop = Number(view.tubeDesignerProductDetailScrollTop ?? 0);
    }
    const shouldRestoreFocus = !view.pending && Boolean(view.tubeDesignerRestoreParameterFocus);
    const restoredAnchor = restoreScrollAnchor(
      scroller,
      view.tubeDesignerParameterPanelScrollAnchor,
      { restoreFocus: shouldRestoreFocus },
    );
    if (!view.tubeDesignerParameterPanelScrollAnchor) {
      scroller.scrollTop = Number(view.tubeDesignerParameterPanelScrollTop ?? 0);
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
