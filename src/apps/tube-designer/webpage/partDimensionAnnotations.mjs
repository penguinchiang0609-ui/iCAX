import { Vector3 } from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

// The inspection window and production parts scene share the same categories
// and visibility rules. A selected/explicitly shown opening can be inspected
// without turning on every opening on a long part.
export const partDimensionCategories = Object.freeze([
  ['overall', '总尺寸'], ['opening-size', '孔口尺寸'], ['end-distance', '端距'],
  ['center-distance', '中心距'], ['clearance', '净距'], ['face-distance', '距边'],
]);

export function annotatedPartElementIds(report) {
  const ids = new Set((report?.annotations ?? []).flatMap(annotation => annotation.elementIds));
  return (report?.holes ?? []).map(hole => hole.elementId).filter(id => ids.has(id));
}

export function visiblePartDimensionAnnotations(state, { revealSelected = false } = {}) {
  if (!state.automaticDimensionsVisible) return [];
  const selected = revealSelected ? state.selectedElementId : '';
  return (state.dimensionReport?.annotations ?? []).filter(annotation =>
    (!state.hiddenCategories.has(annotation.category)
      // A pair ruler belongs to both openings. Revealing only one opening
      // must not make the neighboring opening's eye appear switched on.
      || annotation.elementIds.length > 0
        && annotation.elementIds.every(id => id === selected || state.shownElementIds?.has(id)))
    && annotation.elementIds.every(id => !state.hiddenElementIds.has(id)));
}

export function partDimensionVisibilityState(state, category = null) {
  const annotations = (state.dimensionReport?.annotations ?? []).filter(annotation => category === null || annotation.category === category);
  const visible = visiblePartDimensionAnnotations(state).filter(annotation => category === null || annotation.category === category);
  return { total: annotations.length, visible: visible.length,
    checked: annotations.length > 0 && visible.length === annotations.length,
    mixed: visible.length > 0 && visible.length < annotations.length };
}

export function partDimensionElementVisible(state, id) {
  if (!state.automaticDimensionsVisible || state.hiddenElementIds.has(id)) return false;
  // The eye is this opening's own switch. A neighboring opening can suppress
  // their shared ruler without changing this opening's visibility preference.
  return (state.dimensionReport?.annotations ?? []).some(annotation => annotation.elementIds.includes(id)
    && (!state.hiddenCategories.has(annotation.category) || state.shownElementIds?.has(id)));
}

export function setPartDimensionMasterVisibility(state, visible) {
  state.automaticDimensionsVisible = visible;
  if (visible) {
    state.hiddenCategories.clear(); state.hiddenElementIds.clear(); state.shownElementIds?.clear();
  }
}

export function setPartDimensionCategoryVisibility(state, category, visible) {
  if (visible && !state.automaticDimensionsVisible) {
    for (const [key] of partDimensionCategories) state.hiddenCategories.add(key);
  }
  state.shownElementIds?.clear();
  if (visible) {
    state.automaticDimensionsVisible = true; state.hiddenCategories.delete(category);
    for (const annotation of state.dimensionReport?.annotations ?? []) {
      if (annotation.category === category) for (const id of annotation.elementIds) state.hiddenElementIds.delete(id);
    }
  } else state.hiddenCategories.add(category);
}

export function setPartDimensionElementVisibility(state, id, visible) {
  state.shownElementIds ??= new Set();
  if (visible) {
    if (!state.automaticDimensionsVisible) {
      for (const [category] of partDimensionCategories) if (category !== 'overall') state.hiddenCategories.add(category);
    }
    state.automaticDimensionsVisible = true; state.hiddenElementIds.delete(id); state.shownElementIds.add(id);
  } else { state.hiddenElementIds.add(id); state.shownElementIds.delete(id); }
}

export function renderPartDimensionVisibilityIcon(visible, mixedState = false, mixedVisible = false) {
  return `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12Z"/><circle cx="12" cy="12" r="3"/><path data-tube-inspection-eye-slash d="M3 3 21 21" ${visible ? 'hidden' : ''}/>${mixedState ? `<path data-tube-inspection-eye-mixed d="M7 12h10" ${mixedVisible ? '' : 'hidden'}/>` : ''}</svg>`;
}

export function nearestPartDimensionElement(report, viewport, event, maxDistance = 22) {
  const canvas = viewport?.renderer?.domElement;
  if (!canvas || !viewport.content || !viewport.camera || !event) return null;
  const bounds = canvas.getBoundingClientRect();
  viewport.content.updateMatrixWorld(true);
  let nearest = null, nearestDistance = maxDistance;
  for (const hole of report?.holes ?? []) {
    const projected = new Vector3(...hole.center).applyMatrix4(viewport.content.matrixWorld).project(viewport.camera);
    if (projected.z < -1 || projected.z > 1) continue;
    const distance = Math.hypot(event.clientX - (bounds.left + (projected.x + 1) * bounds.width / 2),
      event.clientY - (bounds.top + (1 - projected.y) * bounds.height / 2));
    if (distance < nearestDistance) { nearest = hole; nearestDistance = distance; }
  }
  return nearest;
}
