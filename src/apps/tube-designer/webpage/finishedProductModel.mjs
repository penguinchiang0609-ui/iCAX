import bundled from "./finishedProductShapes.generated.mjs";

const copy = (value) => structuredClone(value);

export function finishedProductKey(value) {
  const ordered = (item) => Array.isArray(item) ? item.map(ordered)
    : item && typeof item === "object" ? Object.fromEntries(Object.keys(item).sort()
      .map((key) => [key, ordered(item[key])])) : item;
  return JSON.stringify(ordered(value));
}

export function finishedProductShapes(view = null) {
  return view?.tubeDesignerFinishedShapeCatalogue ?? bundled.shapes;
}

export function finishedProductShape(shapeId, view = null) {
  return finishedProductShapes(view).find((shape) => shape.id === shapeId);
}

export function createFinishedProduct(shapeId, view = null) {
  const shape = finishedProductShape(shapeId, view);
  if (!shape) throw new Error("成品造型不存在");
  return { schema: "icax.finished-product", schemaVersion: 1, shapeId,
    parameters: Object.fromEntries(shape.parameters.map((p) => [p.key, copy(p.defaultValue)])),
    spans: Object.fromEntries(shape.spans.map((span) => [span.id, {
      profileRef: copy(span.profileRef), parameters: copy(span.parameters), length: span.length,
    }])) };
}

// This state belongs to the finished product, never to a process selection.
// Spans describe portions of the overall shape; they do not fix blank count.
export function finishedProductState(view, initialShapeId = "l") {
  const state = view.tubeDesignerFinishedProduct ??= { selectedShapeId: initialShapeId, drafts: {} };
  state.drafts ??= {};
  state.selectedShapeId ??= initialShapeId;
  return state;
}

export function finishedProductInput(view, shapeId = null) {
  const state = finishedProductState(view, shapeId || "l");
  const id = shapeId || state.selectedShapeId;
  return state.drafts[id] ??= createFinishedProduct(id, view);
}

export function processInputProblem(template, input) {
  const contract = template?.inputContract;
  if (!contract || input?.schema !== "icax.assembly-process-input") return "工艺缺少明确的局部几何输入";
  const roles = Object.keys(input.parts ?? {});
  const stockMode = contract.supportedInputModes?.includes("stock-operation") && roles.length === 1 && roles[0] === "stock";
  if (!stockMode && (roles.length !== contract.roles.length || contract.roles.some((role) => !input.parts[role])))
    return "请选择此工艺所需的局部管段";
  for (const definition of contract.geometryParameters ?? []) {
    if (!Object.hasOwn(input.geometry ?? {}, definition.key)) return `工艺缺少${definition.displayName || definition.key}`;
  }
  for (const [key, requirement] of Object.entries(contract.requirements ?? {})) {
    const value = input.geometry[key];
    if (requirement.min != null && value < requirement.min
        || requirement.max != null && value > requirement.max
        || requirement.values && !requirement.values.includes(value))
      return `当前工艺不支持成品尺寸 ${value}，请选择其他工艺`;
  }
  return "";
}

const dot = (first, second) => first.reduce((value, entry, index) => value + entry * second[index], 0);
const axis = (part, column = 0) => [0, 1, 2].map((row) => part.matrix[row * 4 + column]);
const origin = (part) => [3, 7, 11].map((index) => part.matrix[index]);

/** Extract local geometry from the independent product's resolved poses.
 * The example only suggests role names. Neither its shape nor its dimensions
 * are a condition for invoking an assembly function.
 */
export function assemblyProcessInput(template, product, finishedPlan, roleMapping = {}) {
  const contract = template?.inputContract;
  if (!contract) throw new Error("工艺未声明局部输入契约");
  const designs = finishedPlan?.designParts ?? [];
  const available = new Map(designs.map((part) => [part.role, part]));
  const parts = {};
  for (const [index, role] of contract.roles.entries()) {
    const explicit = roleMapping[role];
    const suggested = template.exampleInput?.roles?.[role];
    const source = available.get(explicit || role) ?? available.get(suggested)
      ?? (designs.length === contract.roles.length ? designs[index] : null);
    if (!source || !Array.isArray(source.matrix) || source.matrix.length !== 16)
      throw new Error(`请选择${role}对应的实际管段`);
    const request = source.request;
    const placement = template.partProcesses?.find((process) => process.participants?.includes(role))?.previewPlacement;
    const anchorKind = template.productBinding?.anchors?.[role] || (placement?.target === "side" ? "side" : "end");
    const exampleAnchor = anchorKind === "side" ? { kind: "side", reference: "start",
      station: request.length / 2, offset: 0, rotation: 0, face: placement?.face || "top" }
      : { kind: "end", end: placement?.end || (index === 0 ? "end" : "start"), trim: 0, rotation: 0 };
    parts[role] = { profileRef: copy(request.profileRef), parameters: copy(request.parameters),
      length: request.length, matrix: copy(source.matrix), anchor: copy(source.anchor || exampleAnchor) };
  }
  const entries = Object.values(parts);
  const geometry = {};
  for (const definition of contract.geometryParameters ?? []) {
    const key = definition.key;
    if (["angle", "jointAngle", "intersectionAngle"].includes(key) && entries.length >= 2) {
      geometry[key] = Math.acos(Math.min(1, Math.max(-1, dot(axis(entries[0]), axis(entries[1]))))) * 180 / Math.PI;
    } else if (key === "planeRotation" && entries.length >= 2) {
      const toward = axis(entries[1]);
      const y = dot(toward, axis(entries[0], 1)), z = dot(toward, axis(entries[0], 2));
      geometry[key] = Math.hypot(y, z) < 1e-9 ? 0 : Math.atan2(-y, z) * 180 / Math.PI;
    } else if (key === "overlapLength" && entries.length >= 2) {
      const first = entries[0], second = entries[1], along = axis(first);
      const start = dot(origin(second).map((value, index) => value - origin(first)[index]), along);
      const end = start + dot(along, axis(second)) * second.length;
      geometry[key] = Math.max(0, Math.min(first.length, Math.max(start, end)) - Math.max(0, Math.min(start, end)));
    } else if (key === "nodeLayout" && entries.length === 4) {
      const axes = entries.map((part) => axis(part));
      const spatial = axes.some((a, i) => axes.some((b, j) => j > i && axes.some((c, k) => k > j
        && Math.abs(dot([a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]], c)) > 1e-6)));
      geometry[key] = spatial ? "spatialFour" : "planarCross";
    } else {
      const source = template.exampleInput?.parameterBindings?.[key] || key;
      if (!Object.hasOwn(product.parameters ?? {}, source)) throw new Error(`局部输入缺少${definition.displayName || key}`);
      geometry[key] = copy(product.parameters[source]);
    }
    if (typeof geometry[key] === "number" && Math.abs(geometry[key]) < 1e-9) geometry[key] = 0;
  }
  return { schema: "icax.assembly-process-input", schemaVersion: 1, parts, geometry };
}

// The editor exposes the whole independent product. A bound process receives
// only its own parameters and the product aliases it explicitly declares.
export function assemblyBindingParameterValues(template, values = {}) {
  if (!template?.exampleInput) return { ...values };
  const keys = new Set((template.parameters ?? []).filter((p) => p.scope !== "scene")
    .map((p) => p.key));
  for (const definition of template.inputContract?.geometryParameters ?? []) keys.add(definition.key);
  return Object.fromEntries(Object.entries(values).filter(([key]) => keys.has(key)));
}

export function assemblyBindingAngleDefinition(template) {
  const key = template?.productBinding?.axisAngle?.expected?.parameter;
  return typeof key === "string" ? template.inputContract?.geometryParameters?.find((p) => p.key === key)
    ?? template.parameters?.find((p) => p.key === key) : null;
}

// Native node geometry reports the angle between the committed anchor axes.
// The process declares whether its input uses that angle or its supplement.
export function assemblyBindingAxisAngle(template, connection) {
  const rule = template?.productBinding?.axisAngle;
  const geometry = connection?.nodeGeometry;
  const raw = geometry?.nodeGeometryStatus === "verified" ? geometry.axisAngleDeg : null;
  if (!Number.isFinite(raw) || raw < 0 || raw > 180) return null;
  const value = rule?.measure === "supplement" ? 180 - raw
    : rule?.measure === "direct" ? raw : null;
  if (value == null) return null;
  const definition = assemblyBindingAngleDefinition(template);
  if (definition && (definition.min != null && value < definition.min
      || definition.max != null && value > definition.max)) return null;
  const requirement = template?.inputContract?.requirements?.[definition?.key];
  if (requirement && (requirement.min != null && value < requirement.min
      || requirement.max != null && value > requirement.max
      || requirement.values && !requirement.values.includes(value))) return null;
  return value;
}

/** Internal input adapter for the existing process editor and expression UI.
 * Definitions come exclusively from the independent product catalogue.
 * Nothing materialized here is saved back into an assembly descriptor.
 */
export function assemblyProcessWithInput(template, view = null) {
  if (!template?.exampleInput || template.finishedProductDefinition) return template;
  const input = template.exampleInput;
  const shape = finishedProductShape(input.shapeId, view);
  if (!shape) return template;
  const aliases = Object.fromEntries(Object.entries(input.parameterBindings ?? {}).map(([key, source]) => [source, key]));
  for (const p of shape.parameters) aliases[p.key] ??= p.key;
  const roles = Object.fromEntries(Object.entries(input.roles).map(([role, id]) => [id, role]));
  const mapping = { ...aliases };
  for (const span of shape.spans) for (const key of ["length", ...Object.keys(span.parameters)])
    mapping[`${span.id}_${key}`] = `${roles[span.id]}_${key}`;
  const rewrite = (value) => {
    if (typeof value === "string") return value.replace(/\$([A-Za-z][A-Za-z0-9_]*)/g, (_, key) => `$${mapping[key] ?? key}`);
    if (Array.isArray(value)) return value.map(rewrite);
    if (value && typeof value === "object") return Object.fromEntries(Object.entries(value)
      .map(([key, entry]) => [key, key === "parameter" ? mapping[entry] ?? entry : rewrite(entry)]));
    return value;
  };
  return { ...template, finishedProductDefinition: shape,
    parameters: [...template.parameters.filter((p) => p.scope !== "scene"),
      ...(template.inputContract?.geometryParameters ?? []).map((p) => ({ ...p,
        sceneKey: input.parameterBindings?.[p.key] || p.key, scope: "scene" }))],
    previewScene: { ...template.previewScene,
      designParts: Object.entries(input.roles).map(([role, id]) => ({
        ...rewrite(copy(shape.spans.find((span) => span.id === id))), role, sceneSlot: id,
      })),
    },
  };
}
