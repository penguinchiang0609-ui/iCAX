import { createPlanarDynamicInput } from "../../../iCAX-UI/SDK/Cad/planarDynamicInput.mjs";
import { getWorkspaceState, isGeometryEditing } from "./workspaceState.mjs";
const input = createPlanarDynamicInput({
  getState: getWorkspaceState,
  getPrimitive: ui => ui.activeTool.startsWith("plane.draw-") ? ui.activeTool.slice(11) : "",
  getRoot: view => view.iplaneLive?.context?.mount?.querySelector("[data-planar-dynamic-input]"),
  isCurrent: view => !view.disposed && view.iplaneLive?.context?.isCurrentProject?.() !== false,
  isEditingTarget: isGeometryEditing,
  isMenuOpen: view => Boolean(view.iplaneContextMenu),
  onPresentation(view, root) {
    root.dataset.iplaneDimensionEditing = root.dataset.planarDimensionEditing;view.iplaneDimensionOwner = "drawing";
    for (const path of root.querySelectorAll("[data-planar-dimension-leader]")) path.dataset.iplaneDimensionLeader = path.dataset.planarDimensionLeader;
    for (const element of root.querySelectorAll("[data-planar-dimension-row]")) {
      element.dataset.iplaneDimensionAnchorX = element.dataset.planarDimensionAnchorX;element.dataset.iplaneDimensionAnchorY = element.dataset.planarDimensionAnchorY;
      const input = element.querySelector("input");input.dataset.iplaneDimensionLocked = input.dataset.planarDimensionLocked;
    }
  },
});
const aliases = html => html.replace(/(data-planar-([a-z-]+))(="[^"]*")?/g, '$1$3 data-iplane-$2$3').replace(/icax-planar-([a-z-]+)/g, '$& iplane-$1');
export const renderDrawingDynamicInput = () => aliases(input.renderDrawingDynamicInput());
export const recordDrawingDimensionPointer = input.recordDrawingDimensionPointer;
export const ensureDrawingDynamicInput = input.ensureDrawingDynamicInput;
export const constrainDrawingDimensionPoint = input.constrainDrawingDimensionPoint;
export const drawingArcParameters = input.drawingArcParameters;
export const drawingDimensionMousePoint = input.drawingDimensionMousePoint;
export const updateDrawingDimensionDraft = input.updateDrawingDimensionDraft;
export const focusDrawingDimensionField = input.focusDrawingDimensionField;
export const handleDrawingDimensionKey = input.handleDrawingDimensionKey;
export const setDrawingAuxiliaryDimensions = input.setDrawingAuxiliaryDimensions;
export const synchronizeDrawingDynamicInput = input.synchronizeDrawingDynamicInput;
export function clearDrawingDynamicInput(view) { input.clearDrawingDynamicInput(view);if (view.iplaneDimensionOwner === "drawing") delete view.iplaneDimensionOwner; }
export const rejectDrawingArcSweep = input.rejectDrawingArcSweep;
