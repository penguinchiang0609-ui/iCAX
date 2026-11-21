import { clearDimensionLines as clearLines, dimensionValueText, placeDimensionLabel as placeLabel, renderDimensionLeaderLayer, showDimensionLines as showLines } from "./planarDimensionPresentation.mjs";
import { arcDimensions, arcPoint, arcSweep, dimensionInputError, dimensionNumber, normalizeArcAngle } from "./planarDimensionValues.mjs";
import { ensurePlanarEditorStyles } from "./planarEditorStyles.mjs";
import { primitiveDimensionFields, primitiveDimensionLabels, synchronizePrimitiveDimensionConstruction, recordPrimitiveDimensionPoint, constrainPrimitiveDimensionPoint, primitiveDimensionDescriptors, primitiveDimensionAction } from "./planarPrimitiveDimensions.mjs";
export function createPlanarDynamicInput({getState, getPrimitive, getRoot, getViewport = view => view.viewport, isCurrent = () => true, isEditingTarget = () => true, isMenuOpen = () => false, onPresentation = () => {}}) {
const auxiliaryLayouts = new WeakMap();
const clearDimensionLines = (view, owner) => clearLines(getViewport(view), owner);
const showDimensionLines = (view, owner, descriptors) => showLines(getViewport(view), owner, descriptors);
const placeDimensionLabel = (view, root, element, descriptor, state, editing) => placeLabel(getViewport(view),root,element,descriptor,state,editing);
const fieldsFor = primitive => primitiveDimensionFields(primitive) ?? (primitive === "circle" ? ["radius"] : primitive === "arc" ? ["radius", "startAngleDeg", "sweepAngleDeg"] : ["width", "height"]);
const numeric = (text, key, state) => state?.primitive === "ellipseArc" && key === "sweepAngleDeg"
  ? dimensionNumber(text, "startAngleDeg") != null && Number(text) !== 0 && Math.abs(Number(text)) <= 360 ? Number(text) : null : dimensionNumber(text, key);
const allFields = [["radius", "R"], ["width", "W"], ["height", "H"], ["length", "L"], ["startAngleDeg", "α"], ["sweepAngleDeg", "θ"], ["includedAngleDeg", "∠"], ...primitiveDimensionLabels];

function renderDrawingDynamicInput() {
  return `<div class="icax-planar-dimension-overlay" data-planar-dynamic-input hidden aria-label="绘图尺寸">
    ${renderDimensionLeaderLayer()}
    ${allFields.map(([key, label]) => `<label class="icax-three-specification-label icax-planar-dimension-label" data-planar-dimension-row="${key}" hidden><span class="icax-three-specification-caption" data-planar-dimension-caption hidden>${label}=</span><span data-planar-dimension-value></span><input class="icax-three-specification-input" type="text" inputmode="decimal" autocomplete="off" spellcheck="false" data-planar-dimension="${key}" aria-label="${label}（${key.endsWith("Deg") ? "°" : "mm"}）" hidden><span data-planar-dimension-unit hidden>°</span></label>`).join("")}
    <small class="icax-planar-dimension-error" data-planar-dimension-error role="status" hidden></small>
  </div>`;
}

function rootFor(view) { return isCurrent(view) ? getRoot(view) : null; }

function recordDrawingDimensionPointer(view, event) {
  const ui = getState(view);
  if (Number.isFinite(event?.clientX) && Number.isFinite(event?.clientY)) ui.dimensionPointer = { x: event.clientX, y: event.clientY };
}

function ensureDrawingDynamicInput(view, arcStart = null) {
  const ui = getState(view), primitive = getPrimitive(ui), anchor = ui.commandPoints[0];
  const special = primitiveDimensionFields(primitive);
  const allowedCounts = primitive === "ellipseArc" ? [1, 2, 3] : primitive === "ellipse" || primitive === "star" || primitive === "arc" ? [1, 2] : [1];
  if ((!special && !["circle", "rect", "arc"].includes(primitive)) || !anchor || !allowedCounts.includes(ui.commandPoints.length)) return ui.dynamicInput ?? null;
  const previous = ui.dynamicInput;
  if (previous?.epoch === ui.drawingEpoch && previous.primitive === primitive && previous.anchor.every((value, axis) => value === anchor[axis])) {
    if (special) { previous.primitiveOptions = ui.drawingShapeOptions?.[primitive] ?? {};synchronizePrimitiveDimensionConstruction(previous, ui.commandPoints);return previous; }
    if (primitive === "arc" && ui.commandPoints.length === 2) {
      const point = ui.commandPoints[1], radius = arcStart?.radius ?? Math.hypot(point[0] - anchor[0], point[1] - anchor[1]);
      const startAngleDeg = normalizeArcAngle(arcStart?.startAngleDeg ?? Math.atan2(point[1] - anchor[1], point[0] - anchor[0]) * 180 / Math.PI);
      if (previous.phase !== "sweep" || arcStart) {
        previous.phase = "sweep";previous.current.radius = radius;previous.current.startAngleDeg = startAngleDeg;
        for (const [key, value] of [["radius", radius], ["startAngleDeg", startAngleDeg]]) {
          const field = previous.drafts[key];if (numeric(field.text, key) !== value) field.text = String(value);
          field.dirty = true;field.locked = true;field.lastValid = value;
        }
        previous.activeField = "sweepAngleDeg";
      }
    }
    return previous;
  }
  const draft = () => ({ text: "", dirty: false, locked: false, lastValid: null });
  const created = ui.dynamicInput = { epoch: ui.drawingEpoch, primitive, anchor: [...anchor], drafts: Object.fromEntries(allFields.map(([key]) => [key, draft()])),
    current: { radius: 0, width: 0, height: 0, radiusX: 0, radiusY: 0, outerRadius: 0, innerRadius: 0, rotationDeg: 0, startAngleDeg: 0, sweepAngleDeg: 90 }, phase: "start", direction: [1, 1], mousePoint: null, editing: false,
    activeField: primitive === "rect" ? "width" : "radius", error: "", errorField: "", position: null, fixedPosition: null, pendingFocus: null };
  if (special) { created.primitiveOptions = ui.drawingShapeOptions?.[primitive] ?? {};synchronizePrimitiveDimensionConstruction(created, ui.commandPoints); }
  if (primitive === "arc" && ui.commandPoints.length === 2) return ensureDrawingDynamicInput(view, arcStart);
  return created;
}

function activeState(view) {
  const ui = getState(view), state = ui.dynamicInput;
  return state && state.epoch === ui.drawingEpoch && getPrimitive(ui) === state.primitive && isEditingTarget(view) ? state : null;
}

function requestFocus(state, field, selectAll = false) {
  state.editing = true;state.fixedPositions ??= {};
  if (state.positions?.[field]) state.fixedPositions[field] ??= { ...state.positions[field] };
  state.activeField = field;state.pendingFocus = { field, selectAll };
}

function invalid(view, state, field) {
  state.error = state.primitive === "ellipseArc" && field === "sweepAngleDeg" ? "扫过角须非零且绝对值不超过360°，负数为顺时针。" : dimensionInputError(field);state.errorField = field;
  requestFocus(state, field);synchronizeDrawingDynamicInput(view);return null;
}

function rejectDrawingArcSweep(view, message) {
  const state = activeState(view);if (state?.primitive !== "arc") return false;
  state.error = message;state.errorField = "sweepAngleDeg";requestFocus(state, "sweepAngleDeg");synchronizeDrawingDynamicInput(view);return false;
}

function constrainDrawingDimensionPoint(view, point, { commit = false } = {}) {
  const state = activeState(view);if (!state || !point) return point;
  const [dx, dy] = point.map((value, axis) => value - state.anchor[axis]);
  if (dx) state.direction[0] = Math.sign(dx);if (dy) state.direction[1] = Math.sign(dy);
  state.mousePoint = [...point];
  const special = primitiveDimensionFields(state.primitive);
  if (special) recordPrimitiveDimensionPoint(state, point);
  else if (state.primitive === "arc") {
    const angle = normalizeArcAngle(Math.atan2(dy, dx) * 180 / Math.PI);
    if (state.phase === "start") { state.current.radius = Math.hypot(dx, dy);state.current.startAngleDeg = angle; }
    else state.current.sweepAngleDeg = arcSweep(numeric(state.drafts.startAngleDeg.text, "startAngleDeg") ?? state.current.startAngleDeg, angle);
  } else state.current = { ...state.current, radius: Math.hypot(dx, dy), width: Math.abs(dx), height: Math.abs(dy) };
  const values = {};
  for (const field of fieldsFor(state.primitive)) {
    const draft = state.drafts[field], typed = draft.dirty ? numeric(draft.text, field, state) : null;
    if (commit && draft.dirty && typed == null) return invalid(view, state, field);
    if (typed != null) draft.lastValid = typed;
    values[field] = typed ?? (draft.dirty ? draft.lastValid : null) ?? state.current[field];
  }
  if (special) return constrainPrimitiveDimensionPoint(state, values);
  if (state.primitive === "circle") return state.drafts.radius.dirty
    ? [state.anchor[0] + values.radius, state.anchor[1]] : point;
  if (state.primitive === "arc") return arcPoint(state.anchor, values.radius, values.startAngleDeg + (state.phase === "sweep" ? values.sweepAngleDeg : 0));
  return [state.anchor[0] + state.direction[0] * values.width, state.anchor[1] + state.direction[1] * values.height];
}

function drawingArcParameters(view) {
  const state = activeState(view);if (state?.primitive !== "arc") return null;
  const value = key => numeric(state.drafts[key].text, key) ?? state.drafts[key].lastValid ?? state.current[key];
  return { center: [...state.anchor], radius: value("radius"), startAngleDeg: value("startAngleDeg"), sweepAngleDeg: value("sweepAngleDeg") };
}

function drawingDimensionMousePoint(view) {
  const state = activeState(view);return state ? state.mousePoint ?? [...state.anchor] : null;
}

function updateDrawingDimensionDraft(view, field, text) {
  const state = activeState(view);if (!state || !fieldsFor(state.primitive).includes(field)) return false;
  const draft = state.drafts[field];draft.text = text;draft.dirty = true;draft.locked = false;
  const value = numeric(text, field, state);if (value != null) draft.lastValid = value;
  state.error = "";state.errorField = "";state.activeField = field;
  state.editing = true;state.fixedPositions ??= {};
  if (state.positions?.[field]) state.fixedPositions[field] ??= { ...state.positions[field] };
  synchronizeDrawingDynamicInput(view);return true;
}

function focusDrawingDimensionField(view, field) {
  const state = activeState(view);if (!state || !fieldsFor(state.primitive).includes(field)) return;
  state.editing = true;state.fixedPositions ??= {};
  if (state.positions?.[field]) state.fixedPositions[field] ??= { ...state.positions[field] };
  state.activeField = field;synchronizeDrawingDynamicInput(view);
}

function lockField(view, state, field) {
  const draft = state.drafts[field], value = draft.dirty ? numeric(draft.text, field, state) : numeric(String(state.current[field]), field, state);
  if (value == null) return invalid(view, state, field);
  if (!draft.dirty) draft.text = String(value);
  draft.dirty = true;draft.locked = true;draft.lastValid = value;
  state.error = "";state.errorField = "";return value;
}

function handleDrawingDimensionKey(view, event) {
  const ui = getState(view), state = activeState(view);
  if (!state || ui.busy || isMenuOpen(view) || event.ctrlKey || event.metaKey || event.altKey || event.isComposing) return null;
  const field = event.target?.dataset?.planarDimension;
  if (field && fieldsFor(state.primitive).includes(field)) {
    if (!["Enter", "Tab"].includes(event.key)) return null;
    event.preventDefault();event.stopPropagation();
    if (event.key === "Tab") {
      // Tab changes fields, including empty or unfinished drafts. It only
      // locks a numerical value the user has actually entered.
      const draft = state.drafts[field];
      if (draft.dirty && numeric(draft.text, field, state) != null) lockField(view, state, field);
      const fields = fieldsFor(state.primitive), index = fields.indexOf(field);
      requestFocus(state, fields[(index + (event.shiftKey ? fields.length - 1 : 1)) % fields.length], true);
      synchronizeDrawingDynamicInput(view);return { handled: true };
    }
    const value = lockField(view, state, field);
    if (value == null) return { handled: true };
    if (primitiveDimensionFields(state.primitive)) {
      const fields = fieldsFor(state.primitive), last = fields.indexOf(field);
      for (const key of fields.slice(0, last)) if (lockField(view, state, key) == null) return { handled: true };
      const values = Object.fromEntries(fields.map(key => [key, numeric(state.drafts[key].text, key, state) ?? state.current[key]]));
      const action = primitiveDimensionAction(state, field, values);
      if (action.error) { state.error = action.error;state.errorField = action.field;requestFocus(state, action.field);synchronizeDrawingDynamicInput(view);return { handled: true }; }
      if (action.nextField) requestFocus(state, action.nextField, true);
      synchronizeDrawingDynamicInput(view);return { handled: true, ...action };
    }
    if (state.primitive === "arc") {
      if (field === "radius") { requestFocus(state, "startAngleDeg", true);synchronizeDrawingDynamicInput(view);return { handled: true }; }
      if (field === "startAngleDeg") {
        if (lockField(view, state, "radius") == null) return { handled: true };
        requestFocus(state, "sweepAngleDeg", true);synchronizeDrawingDynamicInput(view);return { handled: true, arcStart: drawingArcParameters(view) };
      }
      for (const key of ["radius", "startAngleDeg"]) if (lockField(view, state, key) == null) return { handled: true };
      return { handled: true, arc: drawingArcParameters(view) };
    }
    if (field === "width") {
      requestFocus(state, state.primitive === "circle" ? "radius" : field === "width" ? "height" : "width", true);
      synchronizeDrawingDynamicInput(view);return { handled: true };
    }
    if (state.primitive === "circle") return { handled: true, point: [state.anchor[0] + value, state.anchor[1]] };
    const width = lockField(view, state, "width");
    if (width == null) return { handled: true };
    return { handled: true, point: [state.anchor[0] + state.direction[0] * width, state.anchor[1] + state.direction[1] * value] };
  }
  if (!event.target?.matches?.(".icax-three-viewport canvas") || !(event.key === "Tab" || /^[0-9.+-]$/.test(event.key))) return null;
  event.preventDefault();event.stopPropagation();
  const targetField = state.activeField;
  requestFocus(state, targetField, event.key === "Tab");
  if (event.key !== "Tab") {
    const draft = state.drafts[targetField];draft.text = event.key;draft.dirty = true;draft.locked = false;
    draft.lastValid = numeric(draft.text, targetField, state);state.error = "";state.errorField = "";
    state.pendingFocus.caretEnd = true;
  }
  synchronizeDrawingDynamicInput(view);return { handled: true };
}

function drawingDescriptors(state) {
  const value = field => numeric(state.drafts[field].text, field, state) ?? state.drafts[field].lastValid ?? state.current[field];
  if (primitiveDimensionFields(state.primitive)) return primitiveDimensionDescriptors(state, value);
  const [x, y] = state.anchor, [sx, sy] = state.direction;
  if (state.primitive === "circle") return [{ key: "radius", label: "R", kind: "radius", value: value("radius"), start: [x, y, 0], end: [x + value("radius"), y, 0] }];
  if (state.primitive === "arc") return arcDimensions({ center: state.anchor, radius: value("radius"), startAngleDeg: value("startAngleDeg"), sweepAngleDeg: value("sweepAngleDeg") });
  return [{ key: "width", label: "W", kind: "linear", value: value("width"), start: [x, y, 0], end: [x + sx * value("width"), y, 0], offsetDirection: [0, -sy, 0] },
    { key: "height", label: "H", kind: "linear", value: value("height"), start: [x, y, 0], end: [x, y + sy * value("height"), 0], offsetDirection: [-sx, 0, 0] }];
}

function setDrawingAuxiliaryDimensions(view, descriptors) {
  getState(view).auxiliaryDimensions = descriptors;synchronizeDrawingDynamicInput(view);
}

function synchronizeDrawingDynamicInput(view) {
  const root = rootFor(view);if (!root) return;
  ensurePlanarEditorStyles(root.ownerDocument);
  const ui = getState(view), state = activeState(view);
  const auxiliary = Boolean(getPrimitive(ui)) ? ui.auxiliaryDimensions ?? [] : [];
  const descriptors = state ? drawingDescriptors(state) : auxiliary;
  root.hidden = !descriptors.length || Boolean(isMenuOpen(view));
  if (!descriptors.length) { clearDimensionLines(view, "drawing");return; }
  showDimensionLines(view, "drawing", descriptors);
  if (isMenuOpen(view)) { if (state) state.pendingFocus = null;return; }
  root.dataset.planarDimensionEditing = String(Boolean(state?.editing));
  const layout = state ?? (auxiliaryLayouts.get(view) ?? (() => {const value = {};auxiliaryLayouts.set(view,value);return value;})());
  for (const path of root.querySelectorAll("[data-planar-dimension-leader]")) path.setAttribute("d", "");
  for (const [field] of allFields) {
    const row = root.querySelector(`[data-planar-dimension-row="${field}"]`), input = row.querySelector("input");
    const descriptor = descriptors.find(item => item.key === field);row.hidden = !descriptor;
    if (!descriptor) continue;
    const draft = state?.drafts[field], editing = Boolean(state?.editing && state.activeField === field);
    row.classList.toggle("is-editing", editing);row.querySelector("[data-planar-dimension-caption]").hidden = !editing;
    const caption = row.querySelector("[data-planar-dimension-value]");caption.hidden = editing;
    const unit = descriptor.unit === "deg" ? "°" : "mm";
    const name = field === "startAngleDeg" ? "起始角 α" : field === "sweepAngleDeg" ? "扫过角 θ" : field === "includedAngleDeg" ? "与上一段前进切向的夹角" : descriptor.label;
    row.title = `${name}（${unit}）`;
    caption.textContent = `${descriptor.label} ${dimensionValueText(descriptor.value)}${unit === "°" ? unit : ""}`;input.hidden = !editing;
    row.querySelector("[data-planar-dimension-unit]").hidden = !editing || unit !== "°";
    input.setAttribute("aria-label", `${name}（${unit}）`);
    if (input.value !== (draft?.text ?? "")) input.value = draft?.text ?? "";
    input.placeholder = dimensionValueText(state?.current[field]);
    input.readOnly = Boolean(ui.busy);
    input.dataset.planarDimensionLocked = String(Boolean(draft?.locked));
    input.setAttribute("aria-invalid", String(state?.errorField === field));
    placeDimensionLabel(view, root, row, descriptor, layout, editing);
  }
  onPresentation(view, root);
  const error = root.querySelector("[data-planar-dimension-error]");error.textContent = state?.error ?? "";error.hidden = !state?.error;
  const position = state?.positions?.[state.activeField];
  if (position) { error.style.left = `${position.x}px`;error.style.top = `${position.y + 22}px`; }
  const focus = state?.pendingFocus;if (state) state.pendingFocus = null;
  if (focus) {
    const input = root.querySelector(`[data-planar-dimension="${focus.field}"]`);
    input.focus({ preventScroll: true });if (focus.selectAll) input.select();
    else if (focus.caretEnd) input.setSelectionRange(input.value.length, input.value.length);
  }
}

function clearDrawingDynamicInput(view) {
  const root = rootFor(view), wasFocused = root?.contains(root.ownerDocument.activeElement);
  const ui = getState(view);ui.dynamicInput = null;ui.auxiliaryDimensions = [];clearDimensionLines(view, "drawing");if (root) root.hidden = true;
  if (wasFocused && isCurrent(view) && isEditingTarget(view))
    getViewport(view)?.root?.querySelector("canvas")?.focus({ preventScroll: true });
}

return {renderDrawingDynamicInput,recordDrawingDimensionPointer,ensureDrawingDynamicInput,constrainDrawingDimensionPoint,drawingArcParameters,drawingDimensionMousePoint,updateDrawingDimensionDraft,focusDrawingDimensionField,handleDrawingDimensionKey,setDrawingAuxiliaryDimensions,synchronizeDrawingDynamicInput,clearDrawingDynamicInput,rejectDrawingArcSweep};
}
