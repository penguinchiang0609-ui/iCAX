import { matchesParameterCondition, parameterVisible } from "./parameterConditions.mjs";
import { isAdvancedParameter, parameterDiagramLevelAttribute } from './parameterPresentation.mjs';
import { catalogText, getTemplateVisualAsset } from "./productCatalog.mjs";

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function definitionMap(template) {
  return new Map((template?.parameters ?? []).map((field) => [String(field?.key ?? field?.name ?? ""), field]));
}

function dimensionValue(value, field) {
  const number = Number(value);
  const text = Number.isFinite(number)
    ? new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3 }).format(number)
    : String(value ?? "—");
  const unit = String(field?.unit ?? "").trim();
  return unit ? `${text} ${unit}` : text;
}

function productDiagram(template) {
  const diagram = template?.extensions?.productDiagram;
  return diagram?.schemaVersion === 1 && typeof diagram?.kind === "string" ? diagram : null;
}

function productSceneDefinition(template) {
  const diagram = productDiagram(template);
  const bindings = template?.extensions?.sceneParameterBindings;
  if (!bindings || typeof bindings !== "object") return diagram;
  return {
    ...(diagram ?? {}),
    ...bindings,
    sceneBindings: { ...(diagram?.sceneBindings ?? {}), ...(bindings.sceneBindings ?? {}) },
    groupSceneBindings: { ...(diagram?.groupSceneBindings ?? {}), ...(bindings.groupSceneBindings ?? {}) },
    profileRoles: { ...(diagram?.profileRoles ?? {}), ...(bindings.profileRoles ?? {}) },
  };
}

function declaredDimensions(template, values) {
  const diagram = productDiagram(template);
  if (!Array.isArray(diagram?.dimensions)) return [];
  return diagram.dimensions.filter((entry) => entry?.parameter
    && matchesParameterCondition(entry.visibleWhen, values));
}

// Linked geometry remains visible; only its parameter captions/interactions
// are suppressed. Hiding a parameter must never hide the product itself.
function presentParameterArtwork(artwork, template, values) {
  const fields=definitionMap(template);
  return artwork.replace(/<([\w:-]+)([^>]*\bdata-product-diagram-parameter="([^"]*)"[^>]*)>/g, (tag,name,attributes,keys) => {
    const visible=keys.split(/\s+/).filter(key=>parameterVisible(fields.get(key),values));
    const caption=/class="[^"]*\bproduct-diagram-measure\b/.test(attributes);
    if(!visible.length){
      if(caption)return `<${name}${attributes} style="display:none" aria-hidden="true">`;
      return `<${name}${attributes.replace(/\s+(?:data-cam-action|data-tube-designer-parameter-key|data-product-diagram-parameter|tabindex|role)="[^"]*"/g,'')}>`;
    }
    return caption && visible.every(key=>isAdvancedParameter(fields.get(key)))
      ? `<${name}${attributes} data-parameter-level="advanced" style="display:none">` : tag;
  });
}

export function productPrimaryDimensions(template, values = {}) {
  const declared = declaredDimensions(template, values);
  const primary = template?.extensions?.primaryDimensions ?? {};
  const fields = definitionMap(template);
  const entries = declared.length ? declared : [
    { kind: "width", parameter: primary.widthParameter, label: "宽度" },
    { kind: "height", parameter: primary.heightParameter, label: "高度" },
    { kind: "depth", parameter: primary.depthParameter, label: "深度" },
  ];
  return entries.filter((entry) => typeof entry?.parameter === "string" && entry.parameter.trim()
    && parameterVisible(fields.get(entry.parameter), values))
    .map((entry) => {
      const field = fields.get(entry.parameter);
      return {
        kind: String(entry.kind ?? "length"),
        parameter: entry.parameter,
        label: catalogText(entry.label, catalogText(field?.displayName, field?.label ?? "尺寸")),
        value: dimensionValue(values?.[entry.parameter], field),
      };
    });
}

function sceneAnnotationDeclarations(template) {
  const display = template?.display;
  if (display?.schema !== "icax.template-display" || Number(display?.schemaVersion) !== 1) return {};
  const shared = display?.shared?.annotations ?? {};
  const scene = display?.views?.scene?.annotations ?? {};
  return { ...shared, ...scene };
}

function activeProductTemplate(designer = {}) {
  const product = designer?.product;
  if (!product) return null;
  return (designer?.templates ?? [])
    .find((item) => String(item?.id ?? "") === String(product.templateId ?? "")) ?? null;
}

function annotationGroupVisibility(view, groupKey) {
  if (view?.tubeDesignerSpecificationAnnotationsVisible === false) return false;
  const visibility = view?.tubeDesignerSpecificationAnnotationGroupVisibility;
  return visibility?.[String(groupKey ?? "")] !== false;
}

function sameParameterValue(left, right) {
  return JSON.stringify(left ?? null) === JSON.stringify(right ?? null);
}

function annotationLabel(declaration, field, value) {
  const name = catalogText(declaration?.label,
    catalogText(field?.displayName, field?.label ?? field?.key ?? "规格"));
  const formatted = dimensionValue(value, field);
  const suffix = !String(field?.unit ?? "").trim() ? String(declaration?.suffix ?? "") : "";
  return `${name} ${formatted}${suffix}`.trim();
}

function annotationEditor(field, value) {
  const type = String(field?.type ?? field?.valueType ?? "number").toLowerCase();
  if (["select", "enum"].includes(type)) {
    return {
      type: "select",
      value,
      options: (Array.isArray(field?.options) ? field.options : field?.choices ?? []).map((option) => ({
        value: typeof option === "object" ? option?.value : option,
        label: catalogText(typeof option === "object" ? option?.label ?? option?.displayName : option),
      })),
    };
  }
  const minimum = field?.min ?? field?.constraints?.minimum;
  const maximum = field?.max ?? field?.constraints?.maximum;
  const step = field?.step ?? field?.constraints?.step;
  return {
    type: ["integer", "number", "float", "double"].includes(type) ? "number" : "text",
    value,
    ...(minimum != null ? { min: minimum } : {}),
    ...(maximum != null ? { max: maximum } : {}),
    ...(step != null ? { step } : type === "integer" ? { step: 1 } : {}),
  };
}

/**
 * Merge the防盗窗's generated world-space anchors with its display rules and
 * the active instance draft. Other templates deliberately have no declaration
 * and therefore receive no scene specification annotations yet.
 */
export function resolveProductSpecificationAnnotations(designer = {}, view = {}) {
  const product = designer?.product;
  if (!product) return [];
  const template = activeProductTemplate(designer);
  if (!template) return [];
  const declarations = sceneAnnotationDeclarations(template);
  const fields = definitionMap(template);
  const committed = { ...(product.parameters ?? {}) };
  const values = { ...committed, ...(view?.tubeDesignerRightDraft ?? {}) };
  const active = String(view?.tubeDesignerLastEditedParameterKey ?? "");
  return (Array.isArray(designer?.specificationAnnotations)
    ? designer.specificationAnnotations : [])
    .flatMap((anchor, index) => {
      const parameter = String(anchor?.parameter ?? "").trim();
      // System and personal templates may declare simple envelope dimensions
      // in their own descriptor.  Their generated anchor is deliberately the
      // declaration fallback; complex products such as the security window
      // can still refine the same parameter in display.json.
      const declaration = { ...(anchor ?? {}), ...(declarations?.[parameter] ?? {}) };
      const field = fields.get(parameter);
      if (!parameter || String(anchor?.kind ?? declaration?.kind ?? "").toLowerCase() === "count"
          || !field
          || !parameterVisible(field, values)
          || !matchesParameterCondition(anchor?.visibleWhen, values)
          || !matchesParameterCondition(declaration.visibleWhen, values)) return [];
      // Product parameters are committed directly to the product EC so native
      // project undo/redo can own every edit. The annotation anchor carries the
      // value used by the last generated model; compare against that frozen
      // baseline instead of treating the current EC value as generated.
      // A fresh instance cannot have pending specification annotations.  This
      // also prevents an overlay retained by the viewport during an add/switch
      // refresh from borrowing the previous instance's generated value.
      const generatedValue = product?.modelOutdated !== true
        ? committed[parameter]
        : Object.prototype.hasOwnProperty.call(anchor ?? {}, "generatedValue")
          ? anchor.generatedValue
          : committed[parameter];
      const pending = !sameParameterValue(values[parameter], generatedValue);
      const editable = declaration.editable !== false
        && field?.readOnly !== true
        && matchesParameterCondition(field?.enabledWhen, values);
      const currentLabel = annotationLabel(declaration, field, values[parameter]);
      const changedLabel = pending
        ? `${annotationLabel(declaration, field, generatedValue)} → ${dimensionValue(values[parameter], field)}${!String(field?.unit ?? "").trim() ? String(declaration?.suffix ?? "") : ""}`
        : currentLabel;
      return [{
        ...anchor,
        id: `${String(product.entityId ?? "product")}:${String(anchor?.id ?? `security-window.${parameter}.${index}`)}`,
        parameter,
        groupKey: String(declaration?.group
          ?? field?.groupKey
          ?? field?.group
          ?? "specifications"),
        kind: String(declaration?.kind ?? anchor?.kind ?? "linear"),
        label: changedLabel,
        oldValue: generatedValue,
        newValue: values[parameter],
        cleanLabel: currentLabel,
        changedLabel,
        order: Number(declaration?.order ?? index),
        editable,
        editing: editable && String(view?.tubeDesignerSceneSpecificationEditorParameter ?? "") === parameter,
        editor: editable ? annotationEditor(field, values[parameter]) : null,
        pending,
        active: active === parameter,
        color: pending ? 0xef5b55 : 0x27c27a,
      }];
    })
    .sort((left, right) => left.order - right.order);
}

function annotationTreeState(groupKeys, view) {
  const visibleCount = groupKeys.filter((key) => annotationGroupVisibility(view, key)).length;
  return visibleCount === 0 ? "none"
    : visibleCount === groupKeys.length ? "all"
      : "mixed";
}

/**
 * Build the scene annotation switch tree from the template's existing
 * parameter groups.  Section groups come from display.json, so the scene and
 * the right parameter panel never acquire competing grouping rules.
 */
export function resolveProductSpecificationAnnotationTree(designer = {}, view = {}) {
  const template = activeProductTemplate(designer);
  if (!template) return null;
  const annotations = resolveProductSpecificationAnnotations(designer, view);
  if (!annotations.length) return null;

  const display = template?.display?.views?.right ?? {};
  const annotationGroupDefinitions = template?.display?.views?.scene?.annotationGroups ?? {};
  const displayGroups = display?.groups ?? {};
  const sectionDefinitions = display?.sectionGroups ?? {};
  const groupDefinitions = new Map((template?.groups ?? []).map((group) => [
    String(group?.key ?? ""), group,
  ]));
  const leaves = new Map();
  for (const annotation of annotations) {
    const key = String(annotation?.groupKey ?? "specifications");
    if (!leaves.has(key)) {
      const annotationDefinition = annotationGroupDefinitions?.[key] ?? {};
      const definition = groupDefinitions.get(key);
      leaves.set(key, {
        key,
        label: catalogText(annotationDefinition?.title ?? annotationDefinition?.displayName,
          catalogText(definition?.displayName ?? definition?.label,
            key === "specifications" ? "其他规格" : key)),
        order: Number(annotationDefinition?.order ?? definition?.order ?? 9999),
        sectionKey: String(annotationDefinition?.sectionGroup ?? displayGroups?.[key]?.sectionGroup ?? ""),
        count: 0,
        groupKeys: [key],
      });
    }
    leaves.get(key).count += 1;
  }

  const direct = [];
  const sections = new Map();
  for (const leaf of leaves.values()) {
    leaf.state = annotationTreeState(leaf.groupKeys, view);
    if (!leaf.sectionKey) {
      direct.push(leaf);
      continue;
    }
    if (!sections.has(leaf.sectionKey)) {
      const definition = sectionDefinitions?.[leaf.sectionKey] ?? {};
      sections.set(leaf.sectionKey, {
        key: `section:${leaf.sectionKey}`,
        label: catalogText(definition?.title, definition?.displayName ?? leaf.sectionKey),
        order: Number(definition?.order ?? 9999),
        children: [],
        groupKeys: [],
        count: 0,
      });
    }
    const section = sections.get(leaf.sectionKey);
    section.children.push(leaf);
    section.groupKeys.push(...leaf.groupKeys);
    section.count += leaf.count;
  }
  for (const section of sections.values()) {
    section.children.sort((left, right) => left.order - right.order || left.label.localeCompare(right.label, "zh-CN"));
    section.state = annotationTreeState(section.groupKeys, view);
  }
  const children = [...direct, ...sections.values()]
    .sort((left, right) => left.order - right.order || left.label.localeCompare(right.label, "zh-CN"));
  const groupKeys = [...new Set(children.flatMap((item) => item.groupKeys))];
  return {
    key: "root",
    label: "规格标注",
    count: annotations.length,
    groupKeys,
    state: annotationTreeState(groupKeys, view),
    children,
  };
}

export function bindProductSpecificationAnnotations(_mount, view) {
  const viewport = view?.viewport;
  if (!viewport?.setSpecificationAnnotations || !viewport?.clearSpecificationAnnotations) return [];
  if (view?.activeAreaId !== "view" || view?.tubeDesignerSpecificationAnnotationsVisible === false) {
    viewport.clearSpecificationAnnotations();
    return [];
  }
  const annotations = resolveProductSpecificationAnnotations(view?.scene?.tubeDesigner ?? {}, view)
    .filter((annotation) => annotationGroupVisibility(view, annotation?.groupKey));
  if (annotations.length) viewport.setSpecificationAnnotations(annotations);
  else viewport.clearSpecificationAnnotations();
  return annotations;
}

function sceneSelectorList(value) {
  if (typeof value === "string") return [value];
  return Array.isArray(value) ? value.filter((item) => typeof item === "string" && item) : [];
}

function sceneBindingSelectors(binding) {
  if (binding === "all" || binding?.all === true) return { all: true, roles: [], rolePrefixes: [], stableKeyPrefixes: [], stableKeyPatterns: [] };
  const entries = Array.isArray(binding) ? binding : [binding];
  const result = { all: false, roles: [], rolePrefixes: [], stableKeyPrefixes: [], stableKeyPatterns: [] };
  for (const entry of entries) {
    if (entry === "all" || entry?.all === true) result.all = true;
    if (!entry || typeof entry !== "object") continue;
    result.roles.push(...sceneSelectorList(entry.memberRoles ?? entry.roles));
    result.rolePrefixes.push(...sceneSelectorList(entry.memberRolePrefixes ?? entry.rolePrefixes));
    result.stableKeyPrefixes.push(...sceneSelectorList(entry.memberStableKeyPrefixes ?? entry.stableKeyPrefixes));
    result.stableKeyPatterns.push(...sceneSelectorList(entry.memberStableKeyPatterns ?? entry.stableKeyPatterns));
  }
  return result;
}

function stableKeyMatchesPattern(stableKey, pattern) {
  const source = String(pattern ?? "").split("*")
    .map((part) => part.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"))
    .join(".*");
  return source ? new RegExp(`^${source}$`).test(stableKey) : false;
}

function sceneSelectorsMatchMember(selectors, member) {
  if (selectors.all) return true;
  const role = String(member?.role ?? "");
  const stableKey = String(member?.stableKey ?? "");
  return selectors.roles.includes(role)
    || selectors.rolePrefixes.some((prefix) => role.startsWith(prefix))
    || selectors.stableKeyPrefixes.some((prefix) => stableKey.startsWith(prefix))
    || selectors.stableKeyPatterns.some((pattern) => stableKeyMatchesPattern(stableKey, pattern));
}

function parameterGroup(template, key) {
  const field = (template?.parameters ?? []).find((item) => String(item?.key ?? item?.name ?? "") === key);
  return String(field?.groupKey ?? field?.group ?? "");
}

/**
 * Resolve the actual scene members for a focused parameter.  This is deliberately
 * declaration-led: templates state their construction roles or stable-key
 * prefixes, so captions and parameter names never become an unreliable mapping
 * layer between the editor and the rendered assembly.
 */
export function productSceneMemberIds(template, members = [], parameter = "", context = {}) {
  const diagram = productSceneDefinition(template);
  const key = String(parameter ?? "");
  const profileRole = String(context?.profileRole ?? "").trim();
  if (!diagram || (!key && !profileRole) || !Array.isArray(members)) return [];
  const bindings = [];
  const direct = diagram.sceneBindings?.[key];
  if (direct) bindings.push(direct);
  const group = parameterGroup(template, key);
  const grouped = group ? diagram.groupSceneBindings?.[group] : null;
  if (grouped) bindings.push(grouped);
  const focusedProfile = profileRole ? diagram.profileRoles?.[profileRole] : null;
  if (focusedProfile) {
    bindings.push({
      memberRoles: focusedProfile.memberRoles,
      memberRolePrefixes: focusedProfile.memberRolePrefixes,
      memberStableKeyPrefixes: focusedProfile.memberStableKeyPrefixes,
      memberStableKeyPatterns: focusedProfile.memberStableKeyPatterns,
    });
  }
  for (const profile of Object.values(diagram.profileRoles ?? {})) {
    if (parameterList(profile?.parameters).includes(key)) {
      bindings.push({
        memberRoles: profile.memberRoles,
        memberRolePrefixes: profile.memberRolePrefixes,
        memberStableKeyPrefixes: profile.memberStableKeyPrefixes,
        memberStableKeyPatterns: profile.memberStableKeyPatterns,
      });
    }
  }
  if (!bindings.length) return [];
  const selectors = sceneBindingSelectors(bindings);
  return members.filter((member) => member?.entityId && sceneSelectorsMatchMember(selectors, member))
    .map((member) => String(member.entityId));
}

/** Resolve the parameter controls that own a picked scene member. */
export function productParameterTargetsForSceneMember(template, member) {
  const diagram = productSceneDefinition(template);
  if (!diagram || !member) return { parameters: [], profileRoles: [] };
  const parameters = [];
  const profileRoles = [];
  for (const [key, binding] of Object.entries(diagram.sceneBindings ?? {})) {
    if (sceneSelectorsMatchMember(sceneBindingSelectors(binding), member)) parameters.push(key);
  }
  for (const [group, binding] of Object.entries(diagram.groupSceneBindings ?? {})) {
    if (!sceneSelectorsMatchMember(sceneBindingSelectors(binding), member)) continue;
    parameters.push(...(template?.parameters ?? [])
      .filter((field) => String(field?.groupKey ?? field?.group ?? "") === group)
      .map((field) => String(field?.key ?? field?.name ?? "")));
  }
  for (const [role, binding] of Object.entries(diagram.profileRoles ?? {})) {
    if (!sceneSelectorsMatchMember(sceneBindingSelectors(binding), member)) continue;
    profileRoles.push(role);
    parameters.push(...parameterList(binding?.parameters));
  }
  return {
    parameters: [...new Set(parameters.filter(Boolean))],
    profileRoles: [...new Set(profileRoles.filter(Boolean))],
  };
}

function numberValue(values, parameter, fallback = 0) {
  const number = Number(values?.[parameter]);
  return Number.isFinite(number) ? number : fallback;
}

function centerSpacingCount(span, startOffset, endOffset, maximumSpacing) {
  const available = Number(span) - Number(startOffset) - Number(endOffset);
  if (!Number.isFinite(available) || !Number.isFinite(maximumSpacing)
      || available < 0 || maximumSpacing <= 0) return 0;
  if (available <= 1e-7) return 1;
  return Math.ceil(available / maximumSpacing - 1e-9) + 1;
}

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function point(x, y) {
  return { x, y };
}

function interpolate(a, b, ratio) {
  return point(a.x + (b.x - a.x) * ratio, a.y + (b.y - a.y) * ratio);
}

function pointsAttribute(points) {
  return points.map(({ x, y }) => `${x.toFixed(2)},${y.toFixed(2)}`).join(" ");
}

function svgLine(a, b, className = "") {
  return `<line${className ? ` class="${className}"` : ""} x1="${a.x.toFixed(2)}" y1="${a.y.toFixed(2)}" x2="${b.x.toFixed(2)}" y2="${b.y.toFixed(2)}"/>`;
}

function svgPolygon(points, className) {
  return `<polygon class="${className}" points="${pointsAttribute(points)}"/>`;
}

function binding(diagram, name, fallback = "") {
  const value = diagram?.bindings?.[name];
  return typeof value === "string" && value ? value : fallback;
}

function bindingList(diagram, name) {
  return Array.isArray(diagram?.bindings?.[name])
    ? diagram.bindings[name].filter((value) => typeof value === "string" && value)
    : [];
}

function parameterList(parameter) {
  return (Array.isArray(parameter) ? parameter.flat(Infinity) : [parameter])
    .filter((value) => typeof value === "string" && value.trim());
}

function activeClass(parameter, active) {
  return parameterList(parameter).includes(active) ? " is-active" : "";
}

function actionAttributes(parameter, mode) {
  const parameters = parameterList(parameter);
  return parameters.length
    ? ` data-cam-action="tube-designer-focus-product-parameter" data-tube-designer-editor-mode="${escapeText(mode)}" data-tube-designer-parameter-key="${escapeText(parameters[0])}" data-product-diagram-parameter="${escapeText(parameters.join(" "))}" tabindex="0" role="button"`
    : "";
}

function linkedGroup(content, parameter, active, mode, className = "", attributes = "") {
  if (!content) return "";
  return `<g class="product-diagram-linked${className ? ` ${className}` : ""}${activeClass(parameter, active)}"${actionAttributes(parameter, mode)}${attributes ? ` ${attributes}` : ""}>${content}</g>`;
}

/**
 * Product diagrams do not infer profile fields from captions.  Each template
 * declares the fields belonging to a material role (frame/horizontal/vertical)
 * in `productDiagram.profileRoles`; this converts that declaration into visual
 * metadata used by every renderer.
 */
function profileRole(diagram, name, values) {
  const declared = diagram?.profileRoles?.[name] ?? {};
  const parameters = parameterList(declared.parameters);
  const typeParameter = typeof declared.typeParameter === "string" ? declared.typeParameter : "";
  const widthParameter = typeof declared.widthParameter === "string" ? declared.widthParameter : "";
  const depthParameter = typeof declared.depthParameter === "string" ? declared.depthParameter : "";
  const dimensions = [widthParameter, depthParameter].map((key) => numberValue(values, key, 0));
  const sectionSize = Math.max(...dimensions, 0);
  const wallThickness = Math.max(0, numberValue(values, declared.wallThicknessParameter, 0));
  const cornerRadius = Math.max(0, numberValue(values, declared.cornerRadiusParameter, 0));
  const type = String(values?.[typeParameter] ?? "").trim().toLowerCase();
  const shape = type === "round" || type === "circle" ? "round" : "rect";
  const stroke = clamp(1.15 + sectionSize / 19 + wallThickness * 0.16, 1.4, 4.8);
  return {
    parameters: [...new Set(parameterList([parameters, typeParameter, widthParameter, depthParameter,
      declared.cornerRadiusParameter, declared.wallThicknessParameter]))],
    shape,
    stroke,
    attributes: `data-product-diagram-profile-role="${escapeText(name)}" data-product-diagram-profile-shape="${shape}" data-product-diagram-profile-rounded="${cornerRadius > 0 ? "true" : "false"}" style="--product-profile-stroke:${stroke.toFixed(2)}"`,
  };
}

function quadGrid(points, verticalCount, horizontalCount, options = {}) {
  const className = options.className ?? "product-diagram-grid";
  const [topLeft, topRight, bottomRight, bottomLeft] = points;
  const visibleVerticalCount = clamp(Math.round(verticalCount), 0, 16);
  const visibleHorizontalCount = clamp(Math.round(horizontalCount), 0, 12);
  const vertical = Array.from({ length: visibleVerticalCount }, (_, index) => {
    const ratio = (index + 1) / (visibleVerticalCount + 1);
    return svgLine(interpolate(topLeft, topRight, ratio), interpolate(bottomLeft, bottomRight, ratio), className);
  }).join("");
  const horizontal = Array.from({ length: visibleHorizontalCount }, (_, index) => {
    const ratio = (index + 1) / (visibleHorizontalCount + 1);
    return svgLine(interpolate(topLeft, bottomLeft, ratio), interpolate(topRight, bottomRight, ratio), className);
  }).join("");
  return linkedGroup(vertical, options.verticalParameter, options.active, options.mode,
    `product-diagram-grid-axis product-diagram-grid-vertical${options.verticalClass ? ` ${options.verticalClass}` : ""}`,
    options.verticalAttributes)
    + linkedGroup(horizontal, options.horizontalParameter, options.active, options.mode,
      `product-diagram-grid-axis product-diagram-grid-horizontal${options.horizontalClass ? ` ${options.horizontalClass}` : ""}`,
      options.horizontalAttributes);
}

function frameOutline(points, frameLayout, parameter, active, mode, attributes = "") {
  const [topLeft, topRight, bottomRight, bottomLeft] = points;
  const vertical = frameLayout !== "top_bottom"
    ? svgLine(topLeft, bottomLeft, "product-diagram-frame") + svgLine(topRight, bottomRight, "product-diagram-frame") : "";
  const horizontal = frameLayout !== "left_right"
    ? svgLine(topLeft, topRight, "product-diagram-frame") + svgLine(bottomLeft, bottomRight, "product-diagram-frame") : "";
  return linkedGroup(vertical + horizontal, parameter, active, mode, "product-diagram-frame-outline product-diagram-profile-frame", attributes);
}

function sideDimension(anchor, outer, parameter, values, fields, active, mode) {
  if (!parameter) return "";
  const middle = interpolate(anchor, outer, 0.5);
  return `<g class="product-diagram-measure product-diagram-depth-measure${activeClass(parameter, active)}"${actionAttributes(parameter, mode)}>${svgLine(anchor, outer, "product-diagram-depth-line")}<text x="${middle.x.toFixed(2)}" y="${(middle.y + 9).toFixed(2)}" text-anchor="middle">${escapeText(dimensionValue(values?.[parameter], fields.get(parameter)))}</text></g>`;
}

function securityWindowArtwork(template, diagram, values, options) {
  const mode = options.mode;
  const active = String(options.activeParameter ?? "");
  const showDimensions = options.hideDimensionCards !== true;
  const frameProfile = profileRole(diagram, "frame", values);
  const horizontalProfile = profileRole(diagram, "horizontal", values);
  const verticalProfile = profileRole(diagram, "vertical", values);
  const layoutParameter = binding(diagram, "layout");
  const widthParameter = binding(diagram, "width");
  const heightParameter = binding(diagram, "height");
  const layout = String(values?.[layoutParameter] ?? "single");
  const semanticLayout = Object.entries(diagram.layoutValues ?? {}).find(([, value]) => value === layout)?.[0] ?? layout;
  const width = Math.max(1, numberValue(values, widthParameter, 1200));
  const height = Math.max(1, numberValue(values, heightParameter, 1800));
  const scale = Math.min(132 / width, 104 / height);
  const frontWidth = clamp(width * scale, 62, 132);
  const frontHeight = clamp(height * scale, 70, 104);
  const frontLeft = 160 - frontWidth / 2;
  const frontTop = 22 + (104 - frontHeight) / 2;
  const front = [
    point(frontLeft, frontTop), point(frontLeft + frontWidth, frontTop),
    point(frontLeft + frontWidth, frontTop + frontHeight), point(frontLeft, frontTop + frontHeight),
  ];
  const lengthParameter = semanticLayout === "two" ? binding(diagram, "singleSideLength")
    : semanticLayout === "five" ? binding(diagram, "depth") : "";
  const sharedDepth = Math.max(1, numberValue(values, lengthParameter, width / 2));
  const leftParameter = semanticLayout === "three" ? binding(diagram, "leftSideLength") : lengthParameter;
  const rightParameter = semanticLayout === "three" ? binding(diagram, "rightSideLength") : lengthParameter;
  const depthPixels = (parameter) => clamp(numberValue(values, parameter, sharedDepth) / width * frontWidth * 0.72, 18, 54);
  const leftDepth = depthPixels(leftParameter);
  const rightDepth = depthPixels(rightParameter);
  const leftVector = point(-leftDepth, -leftDepth * 0.34);
  const rightVector = point(rightDepth, -rightDepth * 0.34);
  const shifted = (source, vector) => point(source.x + vector.x, source.y + vector.y);
  const leftFace = [shifted(front[0], leftVector), front[0], front[3], shifted(front[3], leftVector)];
  const rightFace = [front[1], shifted(front[1], rightVector), shifted(front[2], rightVector), front[2]];
  const sidePosition = String(values?.[binding(diagram, "sidePosition")] ?? "right");
  const hasLeft = semanticLayout === "three" || semanticLayout === "five" || (semanticLayout === "two" && sidePosition === "left");
  const hasRight = semanticLayout === "three" || semanticLayout === "five" || (semanticLayout === "two" && sidePosition !== "left");
  const topFace = [shifted(front[0], leftVector), shifted(front[1], rightVector), front[1], front[0]];
  const bottomFace = [front[3], front[2], shifted(front[2], rightVector), shifted(front[3], leftVector)];
  const pattern = String(values?.[binding(diagram, "infillPattern")] ?? "grid");
  const allowVertical = pattern !== "horizontal";
  const allowHorizontal = pattern !== "vertical";
  const spacingParameters = (maximum, start, end) => [maximum, start, end].filter(Boolean);
  const countFromSpacing = (span, parameters, defaults) => centerSpacingCount(
    span,
    numberValue(values, parameters[1], defaults[0]),
    numberValue(values, parameters[2], defaults[1]),
    numberValue(values, parameters[0], defaults[2]),
  );
  const frontVerticalParameters = spacingParameters(
    binding(diagram, "verticalMaximumCenterSpacing"),
    binding(diagram, "verticalLeftCenterOffset"),
    binding(diagram, "verticalRightCenterOffset"),
  );
  const frontHorizontalParameters = spacingParameters(
    binding(diagram, "horizontalMaximumCenterSpacing"),
    binding(diagram, "horizontalTopCenterOffset"),
    binding(diagram, "horizontalBottomCenterOffset"),
  );
  const sideVerticalParameters = spacingParameters(
    binding(diagram, "sideVerticalMaximumCenterSpacing"),
    binding(diagram, "sideVerticalStartCenterOffset"),
    binding(diagram, "sideVerticalEndCenterOffset"),
  );
  const sideHorizontalParameters = spacingParameters(
    binding(diagram, "sideHorizontalMaximumCenterSpacing"),
    binding(diagram, "horizontalTopCenterOffset"),
    binding(diagram, "horizontalBottomCenterOffset"),
  );
  const topBottomRodParameters = spacingParameters(
    binding(diagram, "topBottomRodMaximumCenterSpacing"),
    binding(diagram, "topBottomRodLeftCenterOffset"),
    binding(diagram, "topBottomRodRightCenterOffset"),
  );
  const topBottomCrossbarParameters = spacingParameters(
    binding(diagram, "topBottomCrossbarMaximumCenterSpacing"),
    binding(diagram, "topBottomCrossbarFrontCenterOffset"),
    binding(diagram, "topBottomCrossbarBackCenterOffset"),
  );
  const frontVerticalCount = allowVertical ? countFromSpacing(width, frontVerticalParameters, [110, 110, 120]) : 0;
  const frontHorizontalCount = allowHorizontal ? countFromSpacing(height, frontHorizontalParameters, [200, 200, 500]) : 0;
  const sideVerticalCount = allowVertical ? countFromSpacing(sharedDepth, sideVerticalParameters, [90, 90, 120]) : 0;
  const sideHorizontalCount = allowHorizontal ? countFromSpacing(height, sideHorizontalParameters, [200, 200, 500]) : 0;
  const topBottomRodCount = allowVertical ? countFromSpacing(width, topBottomRodParameters, [110, 110, 120]) : 0;
  const topBottomCrossbarCount = allowHorizontal
    ? countFromSpacing(sharedDepth, topBottomCrossbarParameters, [175, 175, 250]) : 0;
  const infillParameter = binding(diagram, "infillPattern");
  const grid = (points, vertical, horizontal, verticalParameter, horizontalParameter) => linkedGroup(
    quadGrid(points, vertical, horizontal, {
      verticalParameter: [verticalParameter, verticalProfile.parameters],
      horizontalParameter: [horizontalParameter, horizontalProfile.parameters],
      verticalClass: "product-diagram-profile-vertical",
      horizontalClass: "product-diagram-profile-horizontal",
      verticalAttributes: verticalProfile.attributes,
      horizontalAttributes: horizontalProfile.attributes,
      active, mode,
    }), infillParameter, active, mode, "product-diagram-infill-pattern",
  );
  const face = (points, className) => linkedGroup(svgPolygon(points, className), frameProfile.parameters,
    active, mode, `product-diagram-profile-frame product-diagram-profile-shape-${frameProfile.shape}`,
    frameProfile.attributes);
  const faces = [];
  if (semanticLayout === "five") {
    faces.push(face(topFace, "product-diagram-face product-diagram-face-cap"));
    faces.push(grid(topFace, topBottomRodCount, topBottomCrossbarCount, topBottomRodParameters, topBottomCrossbarParameters));
    faces.push(face(bottomFace, "product-diagram-face product-diagram-face-cap"));
    faces.push(grid(bottomFace, topBottomRodCount, topBottomCrossbarCount, topBottomRodParameters, topBottomCrossbarParameters));
  }
  if (hasLeft) {
    faces.push(face(leftFace, "product-diagram-face product-diagram-face-side"));
    faces.push(grid(leftFace, sideVerticalCount, sideHorizontalCount, sideVerticalParameters, sideHorizontalParameters));
  }
  if (hasRight) {
    faces.push(face(rightFace, "product-diagram-face product-diagram-face-side"));
    faces.push(grid(rightFace, sideVerticalCount, sideHorizontalCount, sideVerticalParameters, sideHorizontalParameters));
  }
  faces.push(face(front, "product-diagram-face product-diagram-face-front"));
  faces.push(grid(front, frontVerticalCount, frontHorizontalCount, frontVerticalParameters, frontHorizontalParameters));

  const frameLayoutParameter = binding(diagram, "frameLayout");
  const frameLayout = String(values?.[frameLayoutParameter] ?? "four_sides");
  if (semanticLayout === "single" && frameLayoutParameter) {
    faces.push(frameOutline(front, frameLayout, [frameLayoutParameter, frameProfile.parameters], active, mode, frameProfile.attributes));
  }

  const doorEnabledParameter = binding(diagram, "doorEnabled");
  let door = "";
  if (values?.[doorEnabledParameter] === true) {
    const doorFaceParameter = semanticLayout === "two" ? binding(diagram, "doorFaceTwo")
      : semanticLayout === "three" ? binding(diagram, "doorFaceThree")
        : semanticLayout === "five" ? binding(diagram, "doorFaceFive") : "";
    const requestedDoorFace = String(values?.[doorFaceParameter] ?? "front");
    const doorFace = semanticLayout === "two" && requestedDoorFace === "side"
      ? (sidePosition === "left" ? "left" : "right")
      : requestedDoorFace;
    const doorSurface = doorFace === "left" && hasLeft ? leftFace
      : doorFace === "right" && hasRight ? rightFace
        : doorFace === "bottom" && semanticLayout === "five" ? bottomFace : front;
    const doorSurfaceLength = doorFace === "left"
      ? numberValue(values, leftParameter, sharedDepth)
      : doorFace === "right" ? numberValue(values, rightParameter, sharedDepth) : width;
    const doorSurfaceHeight = doorFace === "bottom" && semanticLayout === "five"
      ? numberValue(values, binding(diagram, "depth"), sharedDepth) : height;
    const doorWidthRatio = clamp(
      numberValue(values, binding(diagram, "doorWidth"), doorSurfaceLength * 0.55) / Math.max(1, doorSurfaceLength),
      0.14, 0.86,
    );
    const doorHeightRatio = clamp(numberValue(values, binding(diagram, "doorHeight"), doorSurfaceHeight * 0.55) / Math.max(1, doorSurfaceHeight), 0.16, 0.86);
    const doorLeftParameter = semanticLayout === "single" ? binding(diagram, "doorLeft") : binding(diagram, "doorOffsetU");
    const doorBottomParameter = semanticLayout === "single" ? binding(diagram, "doorBottom") : binding(diagram, "doorOffsetV");
    const doorLeftRatio = clamp(
      numberValue(values, doorLeftParameter, 0) / Math.max(1, doorSurfaceLength),
      0.02, 0.96 - doorWidthRatio,
    );
    const doorBottomRatio = clamp(numberValue(values, doorBottomParameter, 0) / Math.max(1, doorSurfaceHeight), 0.02, 0.96 - doorHeightRatio);
    const quadPoint = (u, v) => interpolate(
      interpolate(doorSurface[0], doorSurface[1], u),
      interpolate(doorSurface[3], doorSurface[2], u),
      v,
    );
    const top = 1 - doorBottomRatio - doorHeightRatio;
    const bottom = 1 - doorBottomRatio;
    const doorOutline = [
      quadPoint(doorLeftRatio, top), quadPoint(doorLeftRatio + doorWidthRatio, top),
      quadPoint(doorLeftRatio + doorWidthRatio, bottom), quadPoint(doorLeftRatio, bottom),
    ];
    const doorWidthParameter = binding(diagram, "doorWidth");
    const doorHeightParameter = binding(diagram, "doorHeight");
    const doorHorizontalParameters = [
      binding(diagram, "doorHorizontalTopCenterOffset"),
      binding(diagram, "doorHorizontalBottomCenterOffset"),
      binding(diagram, "doorHorizontalMaximumCenterSpacing"),
    ].filter(Boolean);
    const doorVerticalParameters = [
      binding(diagram, "doorVerticalLeftCenterOffset"),
      binding(diagram, "doorVerticalRightCenterOffset"),
      binding(diagram, "doorVerticalMaximumCenterSpacing"),
    ].filter(Boolean);
    const doorVerticalCount = allowVertical ? centerSpacingCount(
      numberValue(values, doorWidthParameter, doorSurfaceLength * 0.55),
      numberValue(values, doorVerticalParameters[0], 89),
      numberValue(values, doorVerticalParameters[1], 89),
      numberValue(values, doorVerticalParameters[2], 120),
    ) : 0;
    const doorHorizontalCount = allowHorizontal ? centerSpacingCount(
      numberValue(values, doorHeightParameter, doorSurfaceHeight * 0.55),
      numberValue(values, doorHorizontalParameters[0], 474),
      numberValue(values, doorHorizontalParameters[1], 474),
      numberValue(values, doorHorizontalParameters[2], 400),
    ) : 0;
    const doorFrame = svgPolygon(doorOutline, "product-diagram-door");
    const doorGrid = linkedGroup(quadGrid(doorOutline, doorVerticalCount, doorHorizontalCount, {
      verticalParameter: doorVerticalParameters, horizontalParameter: doorHorizontalParameters, active, mode,
    }), infillParameter, active, mode, "product-diagram-opening-grid");
    // The door frame itself is a shared visual target for size and position.
    door = linkedGroup(doorFrame + doorGrid, [
      doorEnabledParameter, doorFaceParameter, doorWidthParameter, doorHeightParameter, doorLeftParameter, doorBottomParameter,
    ], active, mode, "product-diagram-opening-group");
  }

  const fields = showDimensions ? definitionMap(template) : null;
  const sideMeasures = showDimensions ? [
    hasLeft ? sideDimension(front[0], leftFace[0], leftParameter, values, fields, active, mode) : "",
    hasRight ? sideDimension(front[1], rightFace[1], rightParameter, values, fields, active, mode) : "",
  ].join("") : "";
  const primaryMeasures = showDimensions ? `
    <g class="product-diagram-measure${activeClass(widthParameter, active)}"${actionAttributes(widthParameter, mode)}>${svgLine(point(front[3].x, 140), point(front[2].x, 140), "product-diagram-dimension-line")}<text x="${((front[3].x + front[2].x) / 2).toFixed(2)}" y="137" text-anchor="middle">${escapeText(dimensionValue(values?.[widthParameter], fields.get(widthParameter)))}</text></g>
    <g class="product-diagram-measure${activeClass(heightParameter, active)}"${actionAttributes(heightParameter, mode)}>${svgLine(point(296, front[1].y), point(296, front[2].y), "product-diagram-dimension-line")}<text x="291" y="${((front[1].y + front[2].y) / 2).toFixed(2)}" text-anchor="end">${escapeText(dimensionValue(values?.[heightParameter], fields.get(heightParameter)))}</text></g>
  ` : "";
  const layoutLabel = ({ single: "单面", two: "双面", three: "三面", five: "五面" })[semanticLayout] ?? layout;
  return `<svg class="tube-designer-product-structure-svg" viewBox="0 0 320 150" role="img" aria-label="${escapeText(layoutLabel)}防盗窗结构与尺寸示意">
    <g class="product-diagram-structure${activeClass(layoutParameter, active)}"${actionAttributes(layoutParameter, mode)}>${faces.join("")}${door}</g>
    <text class="product-diagram-layout-label" x="10" y="18">${escapeText(layoutLabel)}结构</text>
    ${primaryMeasures}
    ${sideMeasures}
  </svg>`;
}

function guardrailPlan(layout, lengths) {
  const first = Math.max(1, lengths[0] ?? 3000);
  const second = Math.max(1, lengths[1] ?? first / 2);
  const third = Math.max(1, lengths[2] ?? second);
  if (layout === "left_l") return [point(0, 0), point(first, 0), point(first, second)];
  if (layout === "right_l") return [point(0, 0), point(first, 0), point(first, -second)];
  if (layout === "u") return [point(0, 0), point(first, 0), point(first, second), point(first - third, second)];
  return [point(0, 0), point(first, 0)];
}

function guardrailInfill(type, bottomLeft, bottomRight, topLeft, topRight, bayIndex) {
  const inset = 0.08;
  const bl = interpolate(bottomLeft, bottomRight, inset);
  const br = interpolate(bottomLeft, bottomRight, 1 - inset);
  const tl = interpolate(topLeft, topRight, inset);
  const tr = interpolate(topLeft, topRight, 1 - inset);
  if (type === "glass" || type === "plate") {
    return svgPolygon([tl, tr, br, bl], type === "glass" ? "product-diagram-glass" : "product-diagram-plate");
  }
  if (type === "cross") return svgLine(tl, br, "product-diagram-infill") + svgLine(bl, tr, "product-diagram-infill");
  if (type === "diamond") {
    const top = interpolate(tl, tr, 0.5);
    const right = interpolate(tr, br, 0.5);
    const bottom = interpolate(bl, br, 0.5);
    const left = interpolate(tl, bl, 0.5);
    return `<polygon class="product-diagram-infill" points="${pointsAttribute([top, right, bottom, left])}"/>`;
  }
  if (type === "horizontal") {
    return [0.25, 0.5, 0.75].map((ratio) => svgLine(interpolate(tl, bl, ratio), interpolate(tr, br, ratio), "product-diagram-infill")).join("");
  }
  if (type === "lower_plate") {
    const middleLeft = interpolate(tl, bl, 0.55);
    const middleRight = interpolate(tr, br, 0.55);
    return svgPolygon([middleLeft, middleRight, br, bl], "product-diagram-plate")
      + [0.25, 0.5, 0.75].map((ratio) => svgLine(interpolate(tl, tr, ratio), interpolate(middleLeft, middleRight, ratio), "product-diagram-infill")).join("");
  }
  const count = 3 + (bayIndex % 2);
  return Array.from({ length: count }, (_, index) => {
    const ratio = (index + 1) / (count + 1);
    return svgLine(interpolate(tl, tr, ratio), interpolate(bl, br, ratio), "product-diagram-infill");
  }).join("");
}

function guardrailArtwork(template, diagram, values, options) {
  const mode = options.mode;
  const active = String(options.activeParameter ?? "");
  const showDimensions = options.hideDimensionCards !== true;
  const layoutParameter = binding(diagram, "layout");
  const layoutValue = String(values?.[layoutParameter] ?? "straight");
  const semanticLayout = Object.entries(diagram.layoutValues ?? {}).find(([, value]) => value === layoutValue)?.[0] ?? layoutValue;
  const lengthParameters = bindingList(diagram, "sideLengths");
  const bayParameters = bindingList(diagram, "sideBayCounts");
  const slopeParameters = bindingList(diagram, "slopeAngles");
  const heightParameter = binding(diagram, "height");
  const pathModeParameter = binding(diagram, "pathMode");
  const pathMode = String(values?.[pathModeParameter] ?? "level");
  const segmentCount = semanticLayout === "u" ? 3 : semanticLayout === "straight" ? 1 : 2;
  const lengths = lengthParameters.slice(0, segmentCount).map((parameter, index) => Math.max(1, numberValue(values, parameter, index ? 1500 : 3000)));
  const world = guardrailPlan(semanticLayout, lengths);
  const raw = world.map(({ x, y }) => point(x + y * 0.5, y * 0.32 - x * 0.08));
  const minX = Math.min(...raw.map(({ x }) => x));
  const maxX = Math.max(...raw.map(({ x }) => x));
  const minY = Math.min(...raw.map(({ y }) => y));
  const maxY = Math.max(...raw.map(({ y }) => y));
  const scale = Math.min(238 / Math.max(1, maxX - minX), 62 / Math.max(1, maxY - minY || 1));
  const centeredX = (340 - (maxX - minX) * scale) / 2;
  const centeredY = 108 - (maxY - minY) * scale / 2;
  const plan = raw.map(({ x, y }) => point(centeredX + (x - minX) * scale, centeredY + (y - minY) * scale));
  const angles = slopeParameters.slice(0, segmentCount).map((parameter) => pathMode === "level" ? 0 : numberValue(values, parameter, 0));
  const rises = [0];
  for (let index = 0; index < segmentCount; index += 1) {
    rises.push(rises[index] + Math.tan(angles[index] * Math.PI / 180) * lengths[index]);
  }
  const riseRange = Math.max(1, Math.max(...rises) - Math.min(...rises));
  const riseScale = pathMode === "level" ? 0 : Math.min(28 / riseRange, 0.04);
  const minimumRise = Math.min(...rises);
  const bottom = plan.map((entry, index) => point(entry.x, entry.y - (rises[index] - minimumRise) * riseScale));
  const height = Math.max(1, numberValue(values, heightParameter, 1200));
  const heightPixels = clamp(46 + (height / 1200 - 1) * 16, 36, 68);
  const top = bottom.map((entry) => point(entry.x, entry.y - heightPixels));
  const infillParameter = binding(diagram, "infillType");
  const railParameter = binding(diagram, "railCount");
  const infillType = String(values?.[infillParameter] ?? "bars");
  const railCount = clamp(Math.round(numberValue(values, railParameter, 2)), 2, 4);
  const bayCounts = Array.from({ length: segmentCount }, (_, index) => clamp(
    Math.round(numberValue(values, bayParameters[index], index ? 2 : 3)), 1, 8,
  ));
  const fields = showDimensions ? definitionMap(template) : null;
  const geometry = [];
  for (let segment = 0; segment < segmentCount; segment += 1) {
    const bays = bayCounts[segment];
    const segmentBottomA = bottom[segment];
    const segmentBottomB = bottom[segment + 1];
    const segmentTopA = top[segment];
    const segmentTopB = top[segment + 1];
    const infill = [];
    const rails = [];
    const posts = [];
    for (let bay = 0; bay < bays; bay += 1) {
      const start = bay / bays;
      const finish = (bay + 1) / bays;
      const bottomA = interpolate(segmentBottomA, segmentBottomB, start);
      const bottomB = interpolate(segmentBottomA, segmentBottomB, finish);
      const topA = interpolate(segmentTopA, segmentTopB, start);
      const topB = interpolate(segmentTopA, segmentTopB, finish);
      infill.push(guardrailInfill(infillType, bottomA, bottomB, topA, topB, bay));
    }
    for (let rail = 0; rail < railCount; rail += 1) {
      const ratio = rail / (railCount - 1);
      rails.push(svgLine(interpolate(segmentTopA, segmentBottomA, ratio), interpolate(segmentTopB, segmentBottomB, ratio), rail === 0 ? "product-diagram-handrail" : "product-diagram-rail"));
    }
    for (let post = 0; post <= bays; post += 1) {
      const ratio = post / bays;
      posts.push(svgLine(interpolate(segmentTopA, segmentTopB, ratio), interpolate(segmentBottomA, segmentBottomB, ratio), "product-diagram-post"));
    }
    const lengthParameter = lengthParameters[segment];
    const bayParameter = bayParameters[segment];
    const slopeParameter = pathMode === "level" ? pathModeParameter : slopeParameters[segment];
    geometry.push(linkedGroup(infill.join(""), infillParameter, active, mode, "product-diagram-guardrail-infill"));
    geometry.push(linkedGroup(rails.join(""), railParameter, active, mode, "product-diagram-guardrail-rails"));
    geometry.push(linkedGroup(posts.join(""), bayParameter, active, mode, "product-diagram-guardrail-bays"));
    geometry.push(linkedGroup(svgLine(segmentTopA, segmentTopB, "product-diagram-slope-guide"), slopeParameter, active, mode, "product-diagram-guardrail-slope"));
    if (showDimensions) {
      const middle = interpolate(segmentTopA, segmentTopB, 0.5);
      geometry.push(`<g class="product-diagram-measure product-diagram-side-measure${activeClass(lengthParameter, active)}"${actionAttributes(lengthParameter, mode)}><text x="${middle.x.toFixed(2)}" y="${(middle.y - 7).toFixed(2)}" text-anchor="middle">第${segment + 1}边 ${escapeText(dimensionValue(values?.[lengthParameter], fields.get(lengthParameter)))}</text></g>`);
    }
  }
  const firstTop = top[0];
  const firstBottom = bottom[0];
  const layoutLabel = ({ straight: "直式", left_l: "左转 L 型", right_l: "右转 L 型", u: "U 型" })[semanticLayout] ?? layoutValue;
  return `<svg class="tube-designer-product-structure-svg" viewBox="0 0 340 160" role="img" aria-label="${escapeText(layoutLabel)}护栏结构与尺寸示意">
    <g class="product-diagram-structure${activeClass(layoutParameter, active)}"${actionAttributes(layoutParameter, mode)}>${geometry.join("")}</g>
    <text class="product-diagram-layout-label" x="10" y="18">${escapeText(layoutLabel)} · ${escapeText(bayCounts.join("/"))} 截</text>
    ${showDimensions ? `<g class="product-diagram-measure${activeClass(heightParameter, active)}"${actionAttributes(heightParameter, mode)}>${svgLine(point(firstTop.x - 14, firstTop.y), point(firstBottom.x - 14, firstBottom.y), "product-diagram-dimension-line")}<text x="${(firstTop.x - 18).toFixed(2)}" y="${((firstTop.y + firstBottom.y) / 2).toFixed(2)}" text-anchor="end">H ${escapeText(dimensionValue(values?.[heightParameter], fields.get(heightParameter)))}</text></g>` : ""}
  </svg>`;
}

function declaredArtwork(template, values, options) {
  const diagram = productDiagram(template);
  if (!diagram) return "";
  if (diagram.kind === "security-window") return securityWindowArtwork(template, diagram, values, options);
  if (diagram.kind === "guardrail") return guardrailArtwork(template, diagram, values, options);
  return "";
}

/** Render the committed instance shape itself, without editor captions or dimensions. */
export function renderProductInstanceThumbnail(template, values = {}) {
  const artwork = declaredArtwork(template, values, {
    mode: "right",
    compact: true,
    hideDimensionCards: true,
  });
  if (artwork) return artwork;
  const source = getTemplateVisualAsset(template, "schematic") || getTemplateVisualAsset(template, "icon");
  return source
    ? `<img class="tube-designer-instance-thumbnail-image" src="${escapeText(source)}" alt="${escapeText(catalogText(template?.name, "产品"))}示意图" />`
    : "";
}

export function renderProductParameterDiagram(template, values = {}, options = {}) {
  const dimensions = productPrimaryDimensions(template, values);
  const hideDimensionCards = options.hideDimensionCards === true;
  // Creation only needs a structural reference.  Templates without declared
  // dimensions still render their linked artwork or schematic in that mode.
  if (!dimensions.length && !hideDimensionCards) return "";
  const active = String(options.activeParameter ?? "");
  const mode = options.mode === "add" ? "add" : "right";
  const compact = options.compact === true;
  const dynamicArtwork = declaredArtwork(template, values, { ...options, mode });
  const source = getTemplateVisualAsset(template, "schematic") || getTemplateVisualAsset(template, "icon");
  const artwork = dynamicArtwork || (source
    ? `<img src="${escapeText(source)}" alt="${escapeText(catalogText(template?.name, "产品"))}示意图" />`
    : `<svg viewBox="0 0 160 110" role="img" aria-label="产品尺寸示意图"><rect x="26" y="18" width="108" height="74" rx="5"/><path d="M20 100h120M16 96l4 4-4 4M144 96l-4 4 4 4M148 14v82M144 10l4 4 4-4M144 100l4-4 4 4"/></svg>`);
  return `<section class="tube-designer-product-diagram ${dynamicArtwork ? "is-parameter-driven" : ""} ${compact ? "is-compact" : ""} ${hideDimensionCards ? "is-no-dimension-cards" : ""}" data-tube-designer-product-diagram>
    <header><div><strong>${compact ? "结构示意" : "成品结构与尺寸示意"}</strong><span>${compact ? "随结构选项联动" : "结构、分截及尺寸随参数联动 · 点击图中标注定位参数"}</span></div></header>
    <div class="tube-designer-product-diagram-canvas">
      <div class="tube-designer-product-diagram-art">${presentParameterArtwork(artwork,template,values)}</div>
      ${hideDimensionCards ? "" : `<div class="tube-designer-product-dimensions">
        ${dimensions.map((dimension) => `<button type="button" class="tube-designer-product-dimension is-${escapeText(dimension.kind)} ${dimension.parameter === active ? "is-active" : ""}"
          data-cam-action="tube-designer-focus-product-parameter" data-tube-designer-editor-mode="${mode}" data-tube-designer-parameter-key="${escapeText(dimension.parameter)}" data-product-diagram-parameter="${escapeText(dimension.parameter)}"${parameterDiagramLevelAttribute(definitionMap(template).get(dimension.parameter))}>
          <span>${escapeText(dimension.label)}</span><strong>${escapeText(dimension.value)}</strong>
        </button>`).join("")}
      </div>`}
    </div>
  </section>`;
}

/** Keep product diagram highlighting local to the editor that owns its fields. */
const productDiagramBindings = new WeakMap();

export function bindProductParameterDiagrams(mount) {
  for (const scope of mount?.querySelectorAll?.("[data-tube-designer-product-parameter-scope]") ?? []) {
    if (productDiagramBindings.has(scope)) { productDiagramBindings.get(scope)(); continue; }
    const mode = scope.dataset.tubeDesignerEditorMode
      ?? scope.querySelector("[data-tube-designer-editor-mode]")?.dataset?.tubeDesignerEditorMode ?? "";
    const owns = (element) => element?.closest?.("[data-tube-designer-product-parameter-scope]") === scope;
    const interactionRoots = [scope];
    const interactionNodes = () => [...new Set(interactionRoots.flatMap((root) => [
      ...root.querySelectorAll("[data-product-diagram-parameter], [data-product-parameter-key]"),
    ]))];
    const updateAdvanced = () => {
      const open=[...scope.querySelectorAll('[data-parameter-advanced]')].some(d=>d.open && !d.closest('[data-profile-parameter-scope], [data-tool-parameter-scope]'));
      for(const node of interactionNodes()) if(node.dataset.parameterLevel==='advanced')node.style.display=open?'':'none';
    };
    scope.addEventListener('toggle',updateAdvanced,true);
    const categoryFor = (node) => {
      for (let current = node; current && owns(current); current = current.parentElement) {
        const groupKey = String(current.dataset?.tubeDesignerParameterGroup ?? "");
        if (groupKey === "section:materials" || groupKey.startsWith("scene:materials:")) return "materials";
        if (groupKey === "section:process" || groupKey.startsWith("scene:process:")) return "process";
      }
      return "";
    };
    const parameterFor = (target) => {
      const node = target?.closest?.("[data-product-diagram-parameter], [data-product-parameter-key]");
      return owns(node) ? {
        key: String(node.dataset.tubeDesignerParameterKey ?? node.dataset.productParameterKey ?? node.dataset.productDiagramParameter ?? ""),
        profileRole: String(node.dataset.productProfileRole ?? ""),
        category: categoryFor(node),
      } : { key: "", profileRole: "", category: "" };
    };
    const initialKey = String(scope.dataset.tubeDesignerActiveParameter ?? "");
    let selected = {
      key: initialKey,
      profileRole: initialKey.startsWith("profile:") ? initialKey.split(":")[1] ?? "" : "",
    };
    let lastSceneSignature = null;
    const highlight = ({ key = "", profileRole = "", category = "" } = {}) => {
      for (const node of interactionNodes()) {
        if (!owns(node)) continue;
        const parameters = String(node.dataset.productDiagramParameter ?? node.dataset.productParameterKey ?? "").split(/\s+/);
        const active = !!key && parameters.includes(key);
        node.classList.toggle("is-active", active);
        if (node.hasAttribute("data-product-parameter-key")) {
          node.closest(".tube-designer-field")?.classList.toggle("is-product-parameter-active", active);
        }
      }
      const signature = `${key}\u0000${profileRole}\u0000${category}`;
      if (mode === "right" && signature !== lastSceneSignature) {
        lastSceneSignature = signature;
        scope.dispatchEvent(new CustomEvent("tube-designer-product-parameter-focus", {
          bubbles: true,
          detail: { key, mode, profileRole, category },
        }));
      }
    };
    const restoreFocus = () => {
      updateAdvanced();
      const focused = parameterFor(scope.ownerDocument.activeElement);
      if (focused.key) selected = focused;
      // Scene emphasis belongs to the current inspector interaction, not the
      // last edited field. Rebinding after an async refresh must not revive it.
      highlight(mode === "right" ? focused : focused.key ? focused : selected);
    };
    productDiagramBindings.set(scope, restoreFocus);
    for (const root of interactionRoots) {
      root.addEventListener("focusin", (event) => {
        const focused = parameterFor(event.target);
        if (!focused.key) {
          if (mode === "right") highlight();
          return;
        }
        selected = focused;
        highlight(focused);
      });
      root.addEventListener("focusout", () => queueMicrotask(restoreFocus));
      root.addEventListener("pointerover", (event) => {
        const hovered = parameterFor(event.target);
        if (hovered.key) highlight(hovered);
      });
      root.addEventListener("pointerout", (event) => {
        if (!parameterFor(event.relatedTarget).key) restoreFocus();
      });
    }
    restoreFocus();
  }
}
