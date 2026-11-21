import { invokeGeometry } from "./editingTarget.mjs";
import { getObjectRows, getSelection, getWorkspaceState, isGeometryEditing, geometryMethod, geometryObjectKind } from "./workspaceState.mjs";
import { invokePlane, supports } from "./nativeOperations.mjs";
import { userErrorMessage } from "./errorMessages.mjs";
import { clearDrawingPreview, updateDrawingPreview } from "./drawingPreview.mjs";
import { clearDimensionLines, dimensionValueText, placeDimensionLabel, renderDimensionLeaderLayer, showDimensionLines } from "./dimensionPresentation.mjs";
import { arcDimensions, arcPoint, arcSweep, dimensionInputError, dimensionNumber } from "./arcDimensionValues.mjs";

const keys = ["radius", "width", "height", "length", "startAngleDeg", "sweepAngleDeg"];
const isCurrent = view => !view.disposed && view.iplaneLive?.context?.isCurrentProject?.() !== false;
const isOwned = (context, view) => isCurrent(view) && context.isCurrentProject?.() !== false
  && view.sceneProxy === context.sceneProxy && view.iplaneLive?.context?.sceneProxy === context.sceneProxy;
const rootFor = view => isCurrent(view) ? view.iplaneLive?.context?.mount?.querySelector("[data-iplane-selected-dimensions]") : null;

export function renderSelectedDimensionAnnotations() {
  return `<div class="iplane-dimension-overlay" data-iplane-selected-dimensions hidden aria-label="所选图形尺寸">
    ${renderDimensionLeaderLayer()}
    ${keys.map(key => `<label class="icax-three-specification-label iplane-dimension-label" data-iplane-selected-dimension-label="${key}" hidden><button type="button" class="icax-three-specification-trigger iplane-dimension-trigger" data-iplane-dimension-edit="${key}" title="双击修改尺寸"></button><span data-iplane-selected-dimension-value></span><span class="icax-three-specification-caption" data-iplane-selected-dimension-caption hidden></span><input class="icax-three-specification-input" type="text" inputmode="decimal" autocomplete="off" spellcheck="false" data-iplane-selected-dimension="${key}" hidden><span data-iplane-selected-dimension-unit hidden>°</span></label>`).join("")}
    <small class="iplane-dimension-error" data-iplane-selected-dimension-error role="status" hidden></small>
  </div>`;
}

function selectedRow(context, view) {
  if (!isGeometryEditing(view) || getWorkspaceState(view).activeTool !== "plane.select") return null;
  const ids = getSelection(view, geometryObjectKind(view));
  return ids.length === 1 ? getObjectRows(context, view, geometryObjectKind(view)).find(row => row.id === ids[0] && row.visible !== false) : null;
}

function clearSelectedPreview(view) {
  if (view.iplaneSelectedPreviewActive) {
    clearDrawingPreview(view);view.iplaneSelectedPreviewActive = false;
    if (view.iplaneDrawingPreview) delete view.iplaneDrawingPreview.group.userData.iplaneSelectedDimensionPreview;
  }
}

export function clearSelectedDimensionAnnotations(view) {
  view.iplaneDimensionQuery = (view.iplaneDimensionQuery ?? 0) + 1;
  delete view.iplaneSelectedDimensions;const root = rootFor(view);if (root) root.hidden = true;
  clearDimensionLines(view, "selected");clearSelectedPreview(view);
}

export function synchronizeSelectedDimensionAnnotations(context, view, ops) {
  const row = selectedRow(context, view);
  if (!row || !supports(context, view, geometryMethod(view, "ReadGeometry"))) { if (view.iplaneSelectedDimensions) clearSelectedDimensionAnnotations(view);return; }
  let state = view.iplaneSelectedDimensions;
  if (state?.id !== row.id) {
    clearSelectedDimensionAnnotations(view);state = view.iplaneSelectedDimensions = { id: row.id, geometry: null, draft: null, layout: {} };
  }
  const requestKey = JSON.stringify([row.id, row.geometryRef?.url, row.geometryRef?.version, row.geometryVersion]);
  if (state.requestKey !== requestKey) {
    state.requestKey = requestKey;
    const generation = view.iplaneDimensionQuery = (view.iplaneDimensionQuery ?? 0) + 1;
    void context.sceneProxy.invoke(geometryMethod(view, "ReadGeometry"), { id: row.id }, { timeoutMs: 30000 }).then(geometry => {
      if (!isOwned(context, view) || generation !== view.iplaneDimensionQuery || view.iplaneSelectedDimensions !== state || selectedRow(context, view)?.id !== row.id) return;
      state.geometry = geometry;state.error = "";refreshSelectedDimensionPresentation(view);
    }).catch(error => {
      if (!isOwned(context, view) || generation !== view.iplaneDimensionQuery || view.iplaneSelectedDimensions !== state) return;
      state.error = userErrorMessage(error);ops?.appendProjectLog?.(context, "error", error?.message ?? String(error));refreshSelectedDimensionPresentation(view);
    });
  }
  refreshSelectedDimensionPresentation(view);
}

function draftDescriptors(state) {
  const source = state.draft?.geometry ?? state.geometry;
  const curve = source?.curves?.length === 1 ? source.curves[0] : null;
  if (curve?.type === "arc") {
    const value = (key, fallback) => state.draft?.dirty.has(key) ? dimensionNumber(state.draft.values[key], key) ?? fallback : fallback;
    const descriptors = arcDimensions({ center: curve.center, radius: value("radius", curve.radius), startAngleDeg: value("startAngleDeg", curve.startAngleDeg),
      sweepAngleDeg: value("sweepAngleDeg", source.dimensions?.find(item => item.key === "sweepAngleDeg")?.value ?? arcSweep(curve.startAngleDeg, curve.endAngleDeg)) }, source.dimensions);
    return (source.dimensions ?? []).map(descriptor => descriptors.find(item => item.key === (descriptor.key ?? descriptor.parameter)) ?? descriptor);
  }
  return (source?.dimensions ?? []).map(descriptor => {
    const key = descriptor.key ?? descriptor.parameter, typed = state.draft?.dirty.has(key) ? dimensionNumber(state.draft.values[key], key) : null;
    if (typed == null || descriptor.value <= 0) return { ...descriptor, key };
    return { ...descriptor, key, value: typed, end: descriptor.end.map((value, axis) => descriptor.start[axis] + (value - descriptor.start[axis]) * typed / descriptor.value) };
  });
}

function previewSelectedDimensions(view, state, descriptors) {
  const draft = state.draft;
  if (!draft || !draft.dirty.size || [...draft.dirty].some(key => dimensionNumber(draft.values[key], key) == null)) { clearSelectedPreview(view);return; }
  const curve = draft.geometry.curves?.length === 1 ? draft.geometry.curves[0] : null;
  const dimension = key => descriptors.find(item => item.key === key);
  const radius = dimension("radius"), length = dimension("length"), width = dimension("width"), height = dimension("height");
  if (curve && radius && ["circle", "arc"].includes(curve.type)) {
    const center = curve.center;
    const startAngleDeg = draft.dirty.has("startAngleDeg") ? dimension("startAngleDeg").value : curve.startAngleDeg;
    const sweepAngleDeg = dimension("sweepAngleDeg")?.value ?? arcSweep(curve.startAngleDeg, curve.endAngleDeg);
    const at = angle => arcPoint(center, radius.value, angle);
    updateDrawingPreview(view, curve.type, curve.type === "circle" ? [center, at(0)] : [center, at(startAngleDeg), at(startAngleDeg + sweepAngleDeg)], null,
      curve.type === "arc" ? { arc: { radius: radius.value, startAngleDeg, sweepAngleDeg } } : {});
  } else if (length && (curve?.type === "line" || curve?.type === "polyline" && !curve.closed && curve.points?.length === 2)) updateDrawingPreview(view, "line", [length.start, length.end], null);
  else if (curve?.type === "polyline" && width?.editable && height?.editable) {
    const a = width.start.slice(0, 2), b = width.end.slice(0, 2), d = height.end.slice(0, 2), c = b.map((value, axis) => value + d[axis] - a[axis]);
    updateDrawingPreview(view, "polyline", [a, b, c, d], null, { closed: true });
  } else { clearSelectedPreview(view);return; }
  view.iplaneSelectedPreviewActive = true;
  if (view.iplaneDrawingPreview) view.iplaneDrawingPreview.group.userData.iplaneSelectedDimensionPreview = true;
}

export function refreshSelectedDimensionPresentation(view) {
  const root = rootFor(view), state = view.iplaneSelectedDimensions;
  if (!root) return;
  if (!state || getWorkspaceState(view).activeTool !== "plane.select" || !isGeometryEditing(view)) { root.hidden = true;return; }
  const descriptors = draftDescriptors(state);root.hidden = !descriptors.length || Boolean(view.iplaneContextMenu);
  showDimensionLines(view, "selected", descriptors);previewSelectedDimensions(view, state, descriptors);
  const draft = state.draft, layout = draft ?? state.layout;
  for (const path of root.querySelectorAll("[data-iplane-dimension-leader]")) path.setAttribute("d", "");
  for (const key of keys) {
    const row = root.querySelector(`[data-iplane-selected-dimension-label="${key}"]`), descriptor = descriptors.find(item => item.key === key);
    row.hidden = !descriptor;if (!descriptor) continue;
    const editing = draft?.activeField === key;
    row.dataset.iplaneDimensionEditable = String(descriptor.editable === true);
    row.classList.toggle("is-editing", editing);row.classList.toggle("is-pending", Boolean(draft?.dirty.has(key)));
    const name = descriptor.kind === "bounds" ? key === "width" ? "外形宽 W" : "外形高 H" : descriptor.label;
    const unit = descriptor.unit === "deg" || descriptor.kind === "angle" ? "°" : "mm";
    const accessibleName = key === "startAngleDeg" ? "起始角 α" : key === "sweepAngleDeg" ? "扫过角 θ" : descriptor.label;
    row.title = `${accessibleName}（${unit}）`;
    const text = `${name} ${dimensionValueText(descriptor.value)}${unit === "°" ? unit : ""}`;
    const button = row.querySelector("button"), value = row.querySelector("[data-iplane-selected-dimension-value]"), caption = row.querySelector("[data-iplane-selected-dimension-caption]"), input = row.querySelector("input");
    button.textContent = text;button.title = `双击修改${accessibleName}`;button.hidden = editing || !descriptor.editable;value.textContent = text;value.hidden = editing || descriptor.editable;
    caption.hidden = !editing;caption.textContent = `${descriptor.label}=`;input.hidden = !editing;
    const nextValue = draft?.values[key] ?? String(descriptor.value);if (input.value !== nextValue) input.value = nextValue;
    input.readOnly = Boolean(getWorkspaceState(view).busy || getWorkspaceState(view).geometryTaskId) || !descriptor.editable;input.setAttribute("aria-label", `${accessibleName}（${unit}）`);
    row.querySelector("[data-iplane-selected-dimension-unit]").hidden = !editing || unit !== "°";
    input.setAttribute("aria-invalid", String(draft?.errorField === key));
    placeDimensionLabel(view, root, row, descriptor, layout, editing);
  }
  const error = root.querySelector("[data-iplane-selected-dimension-error]");error.textContent = draft?.error ?? state.error ?? "";error.hidden = !error.textContent;
  const position = layout.positions?.[draft?.activeField];if (position) { error.style.left = `${position.x}px`;error.style.top = `${position.y + 22}px`; }
  const focus = draft?.pendingFocus;if (draft) draft.pendingFocus = null;
  if (focus && !view.iplaneContextMenu) { const input = root.querySelector(`[data-iplane-selected-dimension="${focus}"]`);input.focus({ preventScroll: true });input.select(); }
}

export function beginSelectedDimensionEdit(view, key) {
  const state = view.iplaneSelectedDimensions;
  if (!state?.geometry || getWorkspaceState(view).busy || getWorkspaceState(view).geometryTaskId || !state.geometry.dimensions?.some(item => (item.key ?? item.parameter) === key && item.editable)) return false;
  if (!state.draft) state.draft = { geometry: structuredClone(state.geometry), baseRef: { ...state.geometry.geometryRef }, values: Object.fromEntries(state.geometry.dimensions.filter(item => item.editable).map(item => [item.key ?? item.parameter, String(item.value)])), dirty: new Set(), activeField: key, positions: { ...state.layout.positions }, fixedPositions: {}, error: "" };
  const draft = state.draft;draft.activeField = key;draft.pendingFocus = key;
  if (draft.positions?.[key]) draft.fixedPositions[key] ??= { ...draft.positions[key] };
  refreshSelectedDimensionPresentation(view);return true;
}

export function updateSelectedDimensionDraft(view, key, text) {
  const state = view.iplaneSelectedDimensions, draft = state?.draft;
  if (!draft || !Object.hasOwn(draft.values, key)) return false;
  draft.values[key] = text;draft.dirty.add(key);draft.error = "";draft.errorField = "";refreshSelectedDimensionPresentation(view);return true;
}

export function cancelSelectedDimensionEdit(view, restoreFocus = true) {
  const state = view.iplaneSelectedDimensions;if (!state?.draft) return false;
  const root = rootFor(view), ownedFocus = root?.contains(root.ownerDocument.activeElement);
  state.draft = null;clearSelectedPreview(view);refreshSelectedDimensionPresentation(view);
  if (ownedFocus && restoreFocus) view.viewport?.root?.querySelector("canvas")?.focus({ preventScroll: true });
  return true;
}

export function handleSelectedDimensionKey(view, event) {
  const state = view.iplaneSelectedDimensions, draft = state?.draft, key = event.target?.dataset?.iplaneSelectedDimension;
  if (!draft || !key || getWorkspaceState(view).busy || event.isComposing || event.ctrlKey || event.metaKey || event.altKey) return null;
  if (!["Enter", "Tab"].includes(event.key)) return null;
  event.preventDefault();event.stopPropagation();
  if (event.key === "Enter") return { commit: true };
  const editable = draft.geometry.dimensions.filter(item => item.editable).map(item => item.key ?? item.parameter);
  const next = editable[(editable.indexOf(key) + (event.shiftKey ? editable.length - 1 : 1)) % editable.length];
  draft.activeField = next;draft.pendingFocus = next;if (draft.positions?.[next]) draft.fixedPositions[next] ??= { ...draft.positions[next] };
  refreshSelectedDimensionPresentation(view);return { commit: false };
}

export async function commitSelectedDimensions(context, view, ops) {
  const state = view.iplaneSelectedDimensions, draft = state?.draft;if (!draft || !isOwned(context, view)) return false;
  const values = {};
  for (const key of draft.dirty) {
    const value = dimensionNumber(draft.values[key], key);
    if (value == null) { draft.error = dimensionInputError(key);draft.errorField = key;draft.activeField = key;draft.pendingFocus = key;refreshSelectedDimensionPresentation(view);return false; }
    values[key] = value;
  }
  if (!Object.keys(values).length) return cancelSelectedDimensionEdit(view);
  try {
    const response = await invokeGeometry(context, view, ops, "Edit", { id: state.id, operation: "dimensions", ...values, expectedGeometryRef: draft.baseRef }, { isErrorCurrent: () => isOwned(context, view) && view.iplaneSelectedDimensions === state && state.draft === draft });
    if (!isOwned(context, view) || response == null) return false;
    if (view.iplaneSelectedDimensions === state && state.draft === draft) { cancelSelectedDimensionEdit(view);state.requestKey = "";synchronizeSelectedDimensionAnnotations(context, view, ops); }
    return true;
  } catch (error) {
    if (isOwned(context, view) && view.iplaneSelectedDimensions === state && state.draft === draft) { draft.error = userErrorMessage(error);refreshSelectedDimensionPresentation(view); }
    return false;
  }
}
