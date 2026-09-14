import { matchesParameterCondition } from "./parameterConditions.mjs";
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

function declaredDimensions(template, values) {
  const diagram = productDiagram(template);
  if (!Array.isArray(diagram?.dimensions)) return [];
  return diagram.dimensions.filter((entry) => entry?.parameter
    && matchesParameterCondition(entry.visibleWhen, values));
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
  return entries.filter((entry) => typeof entry?.parameter === "string" && entry.parameter.trim())
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

function sceneSelectorList(value) {
  if (typeof value === "string") return [value];
  return Array.isArray(value) ? value.filter((item) => typeof item === "string" && item) : [];
}

function sceneBindingSelectors(binding) {
  if (binding === "all" || binding?.all === true) return { all: true, roles: [], rolePrefixes: [], stableKeyPrefixes: [] };
  const entries = Array.isArray(binding) ? binding : [binding];
  const result = { all: false, roles: [], rolePrefixes: [], stableKeyPrefixes: [] };
  for (const entry of entries) {
    if (entry === "all" || entry?.all === true) result.all = true;
    if (!entry || typeof entry !== "object") continue;
    result.roles.push(...sceneSelectorList(entry.memberRoles ?? entry.roles));
    result.rolePrefixes.push(...sceneSelectorList(entry.memberRolePrefixes ?? entry.rolePrefixes));
    result.stableKeyPrefixes.push(...sceneSelectorList(entry.memberStableKeyPrefixes ?? entry.stableKeyPrefixes));
  }
  return result;
}

/**
 * Resolve the actual scene members for a focused parameter.  This is deliberately
 * declaration-led: templates state their construction roles or stable-key
 * prefixes, so captions and parameter names never become an unreliable mapping
 * layer between the editor and the rendered assembly.
 */
export function productSceneMemberIds(template, members = [], parameter = "") {
  const diagram = productDiagram(template);
  const key = String(parameter ?? "");
  if (!diagram || !key || !Array.isArray(members)) return [];
  const bindings = [];
  const direct = diagram.sceneBindings?.[key];
  if (direct) bindings.push(direct);
  for (const profile of Object.values(diagram.profileRoles ?? {})) {
    if (parameterList(profile?.parameters).includes(key)) {
      bindings.push({
        memberRoles: profile.memberRoles,
        memberRolePrefixes: profile.memberRolePrefixes,
        memberStableKeyPrefixes: profile.memberStableKeyPrefixes,
      });
    }
  }
  if (!bindings.length) return [];
  const selectors = sceneBindingSelectors(bindings);
  return members.filter((member) => {
    if (!member?.entityId) return false;
    if (selectors.all) return true;
    const role = String(member.role ?? "");
    const stableKey = String(member.stableKey ?? "");
    return selectors.roles.includes(role)
      || selectors.rolePrefixes.some((prefix) => role.startsWith(prefix))
      || selectors.stableKeyPrefixes.some((prefix) => stableKey.startsWith(prefix));
  }).map((member) => String(member.entityId));
}

function numberValue(values, parameter, fallback = 0) {
  const number = Number(values?.[parameter]);
  return Number.isFinite(number) ? number : fallback;
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

function automaticVerticalCount(span, barWidth, maximumGap) {
  if (!(span > 0) || !(maximumGap >= 0) || !(barWidth >= 0)) return 0;
  return Math.max(0, Math.ceil(span / (maximumGap + barWidth / 2) - 1 - 1e-9));
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
  const verticalModeParameter = binding(diagram, "verticalLayoutMode");
  const maximumVerticalGapParameter = binding(diagram, "maximumVerticalClearGap");
  const sideMaximumVerticalGapParameter = binding(diagram, "sideMaximumVerticalClearGap");
  const verticalProfileWidthParameter = binding(diagram, "verticalProfileWidth");
  const verticalMode = String(values?.[verticalModeParameter] ?? "manual_count");
  const frontVerticalParameter = semanticLayout === "single"
    ? binding(diagram, "frontVerticalCount") : binding(diagram, "multiVerticalCount");
  const maximumVerticalGap = Math.max(0, numberValue(values, maximumVerticalGapParameter, 110));
  const verticalProfileWidth = Math.max(0, numberValue(values, verticalProfileWidthParameter, 19));
  const verticalCount = (span, manualParameter, fallback, maximumGap = maximumVerticalGap) => allowVertical
    ? (verticalMode === "maximum_clear_gap"
      ? automaticVerticalCount(span, verticalProfileWidth, maximumGap)
      : numberValue(values, manualParameter, fallback))
    : 0;
  const visibleVerticalParameter = verticalMode === "maximum_clear_gap"
    ? maximumVerticalGapParameter : frontVerticalParameter;
  const frontVerticalCount = verticalCount(width, frontVerticalParameter, 5);
  const frontHorizontalCount = allowHorizontal ? numberValue(values, binding(diagram, "frontHorizontalCount"), 4) : 0;
  const sideMaximumVerticalGap = Math.max(0, numberValue(values, sideMaximumVerticalGapParameter, maximumVerticalGap));
  const sideVerticalCount = verticalCount(sharedDepth, binding(diagram, "sideVerticalCount"), 4, sideMaximumVerticalGap);
  const sideHorizontalCount = allowHorizontal ? numberValue(values, binding(diagram, "sideHorizontalCount"), 4) : 0;
  const frontHorizontalParameter = binding(diagram, "frontHorizontalCount");
  const sideVerticalParameter = verticalMode === "maximum_clear_gap"
    ? sideMaximumVerticalGapParameter : binding(diagram, "sideVerticalCount");
  const sideHorizontalParameter = binding(diagram, "sideHorizontalCount");
  const topBottomRodParameter = verticalMode === "maximum_clear_gap"
    ? maximumVerticalGapParameter : binding(diagram, "topBottomRodCount");
  const topBottomRodCount = verticalCount(width, binding(diagram, "topBottomRodCount"), 4);
  const topBottomCrossbarParameter = binding(diagram, "topBottomCrossbarCount");
  const topBottomCrossbarCount = numberValue(values, topBottomCrossbarParameter, 2);
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
    faces.push(grid(topFace, topBottomRodCount, topBottomCrossbarCount, topBottomRodParameter, topBottomCrossbarParameter));
    faces.push(face(bottomFace, "product-diagram-face product-diagram-face-cap"));
    faces.push(grid(bottomFace, topBottomRodCount, topBottomCrossbarCount, topBottomRodParameter, topBottomCrossbarParameter));
  }
  if (hasLeft) {
    faces.push(face(leftFace, "product-diagram-face product-diagram-face-side"));
    faces.push(grid(leftFace, sideVerticalCount, sideHorizontalCount, sideVerticalParameter, sideHorizontalParameter));
  }
  if (hasRight) {
    faces.push(face(rightFace, "product-diagram-face product-diagram-face-side"));
    faces.push(grid(rightFace, sideVerticalCount, sideHorizontalCount, sideVerticalParameter, sideHorizontalParameter));
  }
  faces.push(face(front, "product-diagram-face product-diagram-face-front"));
  faces.push(grid(front, frontVerticalCount, frontHorizontalCount, visibleVerticalParameter, frontHorizontalParameter));

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
    const doorVerticalParameter = binding(diagram, "doorVerticalCount");
    const doorHorizontalParameter = binding(diagram, "doorHorizontalCount");
    const doorVerticalCount = allowVertical ? numberValue(values, doorVerticalParameter, 1) : 0;
    const doorHorizontalCount = allowHorizontal ? numberValue(values, doorHorizontalParameter, 1) : 0;
    const doorFrame = svgPolygon(doorOutline, "product-diagram-door");
    const doorGrid = linkedGroup(quadGrid(doorOutline, doorVerticalCount, doorHorizontalCount, {
      verticalParameter: doorVerticalParameter, horizontalParameter: doorHorizontalParameter, active, mode,
    }), infillParameter, active, mode, "product-diagram-opening-grid");
    // The door frame itself is a shared visual target for size and position.
    door = linkedGroup(doorFrame + doorGrid, [
      doorEnabledParameter, doorFaceParameter, doorWidthParameter, doorHeightParameter, doorLeftParameter, doorBottomParameter,
    ], active, mode, "product-diagram-opening-group");
  }

  const fields = definitionMap(template);
  const sideMeasures = [
    hasLeft ? sideDimension(front[0], leftFace[0], leftParameter, values, fields, active, mode) : "",
    hasRight ? sideDimension(front[1], rightFace[1], rightParameter, values, fields, active, mode) : "",
  ].join("");
  const layoutLabel = ({ single: "单面", two: "双面", three: "三面", five: "五面" })[semanticLayout] ?? layout;
  return `<svg class="tube-designer-product-structure-svg" viewBox="0 0 320 150" role="img" aria-label="${escapeText(layoutLabel)}防盗窗结构与尺寸示意">
    <g class="product-diagram-structure${activeClass(layoutParameter, active)}"${actionAttributes(layoutParameter, mode)}>${faces.join("")}${door}</g>
    <text class="product-diagram-layout-label" x="10" y="18">${escapeText(layoutLabel)}结构</text>
    <g class="product-diagram-measure${activeClass(widthParameter, active)}"${actionAttributes(widthParameter, mode)}>${svgLine(point(front[3].x, 140), point(front[2].x, 140), "product-diagram-dimension-line")}<text x="${((front[3].x + front[2].x) / 2).toFixed(2)}" y="137" text-anchor="middle">${escapeText(dimensionValue(values?.[widthParameter], fields.get(widthParameter)))}</text></g>
    <g class="product-diagram-measure${activeClass(heightParameter, active)}"${actionAttributes(heightParameter, mode)}>${svgLine(point(296, front[1].y), point(296, front[2].y), "product-diagram-dimension-line")}<text x="291" y="${((front[1].y + front[2].y) / 2).toFixed(2)}" text-anchor="end">${escapeText(dimensionValue(values?.[heightParameter], fields.get(heightParameter)))}</text></g>
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
  const fields = definitionMap(template);
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
    const middle = interpolate(segmentTopA, segmentTopB, 0.5);
    geometry.push(`<g class="product-diagram-measure product-diagram-side-measure${activeClass(lengthParameter, active)}"${actionAttributes(lengthParameter, mode)}><text x="${middle.x.toFixed(2)}" y="${(middle.y - 7).toFixed(2)}" text-anchor="middle">第${segment + 1}边 ${escapeText(dimensionValue(values?.[lengthParameter], fields.get(lengthParameter)))}</text></g>`);
  }
  const firstTop = top[0];
  const firstBottom = bottom[0];
  const layoutLabel = ({ straight: "直式", left_l: "左转 L 型", right_l: "右转 L 型", u: "U 型" })[semanticLayout] ?? layoutValue;
  return `<svg class="tube-designer-product-structure-svg" viewBox="0 0 340 160" role="img" aria-label="${escapeText(layoutLabel)}护栏结构与尺寸示意">
    <g class="product-diagram-structure${activeClass(layoutParameter, active)}"${actionAttributes(layoutParameter, mode)}>${geometry.join("")}</g>
    <text class="product-diagram-layout-label" x="10" y="18">${escapeText(layoutLabel)} · ${escapeText(bayCounts.join("/"))} 截</text>
    <g class="product-diagram-measure${activeClass(heightParameter, active)}"${actionAttributes(heightParameter, mode)}>${svgLine(point(firstTop.x - 14, firstTop.y), point(firstBottom.x - 14, firstBottom.y), "product-diagram-dimension-line")}<text x="${(firstTop.x - 18).toFixed(2)}" y="${((firstTop.y + firstBottom.y) / 2).toFixed(2)}" text-anchor="end">H ${escapeText(dimensionValue(values?.[heightParameter], fields.get(heightParameter)))}</text></g>
  </svg>`;
}

function declaredArtwork(template, values, options) {
  const diagram = productDiagram(template);
  if (!diagram) return "";
  if (diagram.kind === "security-window") return securityWindowArtwork(template, diagram, values, options);
  if (diagram.kind === "guardrail") return guardrailArtwork(template, diagram, values, options);
  return "";
}

export function renderProductParameterDiagram(template, values = {}, options = {}) {
  const dimensions = productPrimaryDimensions(template, values);
  if (!dimensions.length) return "";
  const active = String(options.activeParameter ?? "");
  const mode = options.mode === "add" ? "add" : "right";
  const dynamicArtwork = declaredArtwork(template, values, { ...options, mode });
  const source = getTemplateVisualAsset(template, "schematic") || getTemplateVisualAsset(template, "icon");
  const artwork = dynamicArtwork || (source
    ? `<img src="${escapeText(source)}" alt="${escapeText(catalogText(template?.name, "产品"))}示意图" />`
    : `<svg viewBox="0 0 160 110" role="img" aria-label="产品尺寸示意图"><rect x="26" y="18" width="108" height="74" rx="5"/><path d="M20 100h120M16 96l4 4-4 4M144 96l-4 4 4 4M148 14v82M144 10l4 4 4-4M144 100l4-4 4 4"/></svg>`);
  return `<section class="tube-designer-product-diagram ${dynamicArtwork ? "is-parameter-driven" : ""}" data-tube-designer-product-diagram>
    <header><div><strong>成品结构与尺寸示意</strong><span>结构、分截及尺寸随参数联动 · 点击图中标注定位参数</span></div></header>
    <div class="tube-designer-product-diagram-canvas">
      <div class="tube-designer-product-diagram-art">${artwork}</div>
      <div class="tube-designer-product-dimensions">
        ${dimensions.map((dimension) => `<button type="button" class="tube-designer-product-dimension is-${escapeText(dimension.kind)} ${dimension.parameter === active ? "is-active" : ""}"
          data-cam-action="tube-designer-focus-product-parameter" data-tube-designer-editor-mode="${mode}" data-tube-designer-parameter-key="${escapeText(dimension.parameter)}" data-product-diagram-parameter="${escapeText(dimension.parameter)}">
          <span>${escapeText(dimension.label)}</span><strong>${escapeText(dimension.value)}</strong>
        </button>`).join("")}
      </div>
    </div>
  </section>`;
}

/** Keep product diagram highlighting local to the editor that owns its fields. */
const productDiagramBindings = new WeakMap();

export function bindProductParameterDiagrams(mount) {
  for (const scope of mount?.querySelectorAll?.("[data-tube-designer-product-parameter-scope]") ?? []) {
    if (productDiagramBindings.has(scope)) { productDiagramBindings.get(scope)(); continue; }
    const owns = (element) => element?.closest?.("[data-tube-designer-product-parameter-scope]") === scope;
    const keyFor = (target) => {
      const node = target?.closest?.("[data-product-diagram-parameter], [data-product-parameter-key]");
      return owns(node) ? String(node.dataset.tubeDesignerParameterKey ?? node.dataset.productParameterKey ?? node.dataset.productDiagramParameter ?? "") : "";
    };
    let lastSceneKey = null;
    const highlight = (key) => {
      for (const node of scope.querySelectorAll("[data-product-diagram-parameter], [data-product-parameter-key]")) {
        if (!owns(node)) continue;
        const parameters = String(node.dataset.productDiagramParameter ?? node.dataset.productParameterKey ?? "").split(/\s+/);
        const active = !!key && parameters.includes(key);
        node.classList.toggle("is-active", active);
        if (node.hasAttribute("data-product-parameter-key")) {
          node.closest(".tube-designer-field")?.classList.toggle("is-product-parameter-active", active);
        }
      }
      const mode = scope.querySelector("[data-tube-designer-editor-mode]")?.dataset?.tubeDesignerEditorMode ?? "";
      if (mode === "right" && key !== lastSceneKey) {
        lastSceneKey = key;
        scope.dispatchEvent(new CustomEvent("tube-designer-product-parameter-focus", {
          bubbles: true,
          detail: { key, mode },
        }));
      }
    };
    const restoreFocus = () => highlight(keyFor(scope.ownerDocument.activeElement));
    productDiagramBindings.set(scope, restoreFocus);
    scope.addEventListener("focusin", (event) => highlight(keyFor(event.target)));
    scope.addEventListener("focusout", () => queueMicrotask(restoreFocus));
    scope.addEventListener("pointerover", (event) => { const key = keyFor(event.target); if (key) highlight(key); });
    scope.addEventListener("pointerout", (event) => { if (!keyFor(event.relatedTarget)) restoreFocus(); });
    restoreFocus();
  }
}
