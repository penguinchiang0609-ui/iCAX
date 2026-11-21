import * as presentation from "../../../iCAX-UI/SDK/Cad/planarDimensionPresentation.mjs";
export const dimensionValueText = presentation.dimensionValueText;
export const dimensionScale = view => presentation.dimensionScale(view.viewport);
export const dimensionAnnotation = (view, descriptor) => presentation.dimensionAnnotation(view.viewport, descriptor);
export const projectDimensionAnchor = (view, descriptor) => presentation.projectDimensionAnchor(view.viewport, descriptor);
export const renderDimensionLeaderLayer = () => presentation.renderDimensionLeaderLayer().replace(/(data-planar-([a-z-]+))(="[^"]*")?/g, '$1$3 data-iplane-$2$3').replace('icax-planar-dimension-leaders', 'icax-planar-dimension-leaders iplane-dimension-leaders');
export function showDimensionLines(view, owner, descriptors) { presentation.showDimensionLines(view.viewport, owner, descriptors);view.iplaneDimensionOwner = owner; }
export function clearDimensionLines(view, owner) { presentation.clearDimensionLines(view.viewport, owner);if (view.iplaneDimensionOwner === owner) delete view.iplaneDimensionOwner; }
export function placeDimensionLabel(view, root, element, descriptor, state, editing = false) {
  presentation.placeDimensionLabel(view.viewport, root, element, descriptor, state, editing);
  element.dataset.iplaneDimensionAnchorX = element.dataset.planarDimensionAnchorX;element.dataset.iplaneDimensionAnchorY = element.dataset.planarDimensionAnchorY;
  for (const path of root.querySelectorAll("[data-planar-dimension-leader]")) path.dataset.iplaneDimensionLeader = path.dataset.planarDimensionLeader;
}
