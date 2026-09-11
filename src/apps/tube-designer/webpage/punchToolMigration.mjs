// Recipe migration and shared placement definitions.
// Tool packages describe geometry only. Placement belongs to the feature/end
// record that uses a package, so all callers use this module for old records.

const clone = value => structuredClone(value);

export const V_NOTCH_STYLE_TO_TOOL = Object.freeze({
  sharp_v: "v-notch-sharp",
  asymmetric_v: "v-notch-sharp",
  left_arc: "edge-arc-groove",
  right_arc: "edge-arc-groove",
  relief_v: "v-notch-sharp",
  // The former flat-v variant had no verified product definition. Preserve
  // its old root width as the optional flat root of a normal sharp V.
  flat_v: "v-notch-sharp",
});

export const V_NOTCH_TOOL_IDS = new Set(Object.values(V_NOTCH_STYLE_TO_TOOL));

export const BRANCH_PLACEMENT_KEYS = Object.freeze([
  "angle", "azimuth", "roll", "offsetY", "offsetZ", "direction", "length",
]);
export const BRANCH_PLACEMENT_DEFAULTS = Object.freeze({
  angle: 90, azimuth: 0, roll: 0, offsetY: 0, offsetZ: 0, direction: "through", length: 120,
});
export const END_PROFILE_PLACEMENT_KEYS = Object.freeze([
  "angle", "azimuth", "roll", "axialOffset", "offsetY", "offsetZ",
]);

const V_SHAPE_KEYS = Object.freeze({
  "v-notch-sharp": ["angle", "asymmetric", "leftAngle", "rightAngle", "leaveBottom", "bottomStrategy", "flatWidth", "roundRadius", "reliefLength", "reliefHeight", "reliefRadius", "maleFemale", "maleFemaleSize"],
  "edge-arc-groove": ["angle", "leftArc", "bridge", "reliefDiameter", "reliefLift", "bottomCut", "bottomCutWidth"],
});

function migrateSharpParameters(params, oldStyle = "") {
  if (oldStyle === "asymmetric_v") {
    params.asymmetric ??= true;
    if (params.angle === undefined && Number.isFinite(Number(params.leftAngle)) && Number.isFinite(Number(params.rightAngle))) {
      params.angle = Number(params.leftAngle) + Number(params.rightAngle);
    }
  }
  if (params.bridge !== undefined && params.leaveBottom === undefined) params.leaveBottom = params.bridge;
  if (params.rootWidth !== undefined && params.flatWidth === undefined) params.flatWidth = params.rootWidth;
  if (params.rootRadius !== undefined && params.roundRadius === undefined) params.roundRadius = params.rootRadius;
  if (params.reliefWidth !== undefined && params.reliefHeight === undefined) params.reliefHeight = params.reliefWidth;
  if (params.flatWidth > 0 && params.bottomStrategy === undefined) params.bottomStrategy = "flat";
  if (params.reliefDiameter > 0 && params.reliefWidth === undefined) {
    params.reliefLength = params.reliefDiameter;
    params.reliefHeight = params.reliefDiameter;
    params.reliefRadius = 0;
    params.bottomStrategy ??= "relief";
  }
  if (oldStyle === "relief_v") {
    const diameter = params.holeDiameter ?? params.reliefDiameter;
    if (diameter !== undefined) {
      params.reliefLength ??= diameter;
      params.reliefHeight ??= diameter;
      params.reliefRadius ??= 0;
    }
    params.bottomStrategy ??= "relief";
  }
  if (oldStyle === "flat_v" && params.flatWidth > 0) params.bottomStrategy = "flat";
  for (const key of ["bridge", "rootWidth", "reliefWidth", "reliefDiameter", "reliefLift", "bottomCut", "bottomCutWidth", "rootRadius", "holeDiameter", "holeLift"]) delete params[key];
}

const END_PLACEMENT_BY_TOOL = Object.freeze({
  "end-convex": ["angle", "offset"],
  "end-cope": ["angle", "offset"],
  "end-key-joint": ["offset"],
  "end-profile": END_PROFILE_PLACEMENT_KEYS,
});

function setIfMissing(item, key, value) {
  if (item[key] === undefined && value !== undefined) item[key] = value;
}

function migrateVNotch(item, params) {
  const oldStyle = String(params.style ?? item.style ?? "sharp_v");
  const migratedId = V_NOTCH_STYLE_TO_TOOL[oldStyle] ?? "v-notch-sharp";
  item.type = migratedId;
  // A new independent package must be resolved and re-pinned; an old digest
  // identifies the removed combined package and cannot be reused.
  item.toolRef = { id: migratedId };
  delete item.toolLabel;
  delete item.toolKind;
  setIfMissing(item, "rotation", params.rotation);
  delete params.style;
  delete params.rotation;
  delete item.style;
  delete item.flatWidth;
  if (oldStyle === "flat_v" && params.rootWidth === undefined && params.flatWidth !== undefined) {
    params.rootWidth = params.flatWidth;
  }
  if (migratedId === "edge-arc-groove") params.leftArc = oldStyle === "left_arc";
  if (migratedId === "v-notch-sharp") migrateSharpParameters(params, oldStyle);
  const allowed = new Set(V_SHAPE_KEYS[migratedId] ?? []);
  for (const key of Object.keys(params)) if (!allowed.has(key)) delete params[key];
  return true;
}

export function migratePunchRecord(value = {}) {
  const item = clone(value ?? {});
  const params = item.toolParameters && typeof item.toolParameters === "object" && !Array.isArray(item.toolParameters)
    ? { ...item.toolParameters } : {};
  const id = String(item.toolRef?.id ?? item.type ?? "");
  let changed = false;
  if (id === "v-notch") changed = migrateVNotch(item, params) || changed;
  else if (V_NOTCH_TOOL_IDS.has(id)) {
    // Early split builds placed rotation in toolParameters. Move it to the
    // feature record and drop the stale package pin so the edited descriptor
    // is installed again.
    const before = JSON.stringify(params);
    if (id === "v-notch-sharp") migrateSharpParameters(params);
    const allowed = new Set(V_SHAPE_KEYS[id] ?? []);
    for (const key of Object.keys(params)) if (!allowed.has(key)) delete params[key];
    changed = changed || before !== JSON.stringify(params);
    if (params.rotation !== undefined) {
      setIfMissing(item, "rotation", params.rotation);
      delete params.rotation;
      item.toolRef = { id };
      changed = true;
    }
  }
  if (id === "branch-profile") {
    for (const key of BRANCH_PLACEMENT_KEYS) {
      if (params[key] !== undefined) { setIfMissing(item, key, params[key]); delete params[key]; changed = true; }
    }
  }
  const endPlacement = END_PLACEMENT_BY_TOOL[id];
  if (endPlacement) {
    for (const key of endPlacement) {
      if (params[key] !== undefined) { setIfMissing(item, key, params[key]); delete params[key]; changed = true; }
    }
  }
  if (Object.keys(params).length || item.toolParameters !== undefined) item.toolParameters = params;
  if (changed) {
    // Regenerate from the independent package. A frozen snapshot tied to the
    // removed package is not a valid migration target.
    delete item.frozenTool;
    delete item.frozenCut;
    const migratedToolId = String(item.toolRef?.id ?? item.type ?? "");
    if (migratedToolId) item.toolRef = { id: migratedToolId };
  }
  return item;
}

export function migratePunchRecipe(value = {}) {
  const recipe = clone(value ?? {});
  if (Array.isArray(recipe.features)) recipe.features = recipe.features.map(migratePunchRecord);
  if (recipe.ends && typeof recipe.ends === "object") {
    recipe.ends = Object.fromEntries(Object.entries(recipe.ends).map(([key, item]) => [key, migratePunchRecord(item)]));
  }
  return recipe;
}
