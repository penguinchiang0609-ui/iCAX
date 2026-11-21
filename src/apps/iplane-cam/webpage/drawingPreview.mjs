import { createPlanarDrawingPreview } from "../../../iCAX-UI/SDK/Cad/planarDrawingPreview.mjs";
import { primitiveInputOptions } from "../../../iCAX-UI/SDK/Cad/planarDrawingPrimitives.mjs";
import { getWorkspaceState } from "./workspaceState.mjs";
function ensure(view) {
  if (view.iplaneDrawingPreview?.viewport === view.viewport) return view.iplaneDrawingPreview;
  disposeDrawingPreview(view);const preview = createPlanarDrawingPreview(view.viewport);
  if (preview) { preview.group.userData.iplaneDrawingPreview = true;view.iplaneDrawingPreview = preview; }
  return preview;
}
export function updateDrawingPreview(view, primitive, accepted, pointer, options) {
  const preview = ensure(view);if (!preview) return;
  const ui = getWorkspaceState(view);
  preview.update(primitive, accepted, pointer, { ...options, primitiveOptions: primitiveInputOptions(primitive, ui.drawingShapeOptions?.[primitive], ui.dynamicInput) });
  const root = view.viewport?.root;if (root) {root.dataset.iplaneDrawingPreview = primitive;root.dataset.iplanePreviewPoints = String(preview.group.userData.outlinePointCount);root.dataset.iplaneAcceptedPoints = String(accepted.length);}
}
export function clearDrawingPreview(view) {
  view.iplaneDrawingPreview?.clear();const root = view.viewport?.root;
  if (root) for (const key of ["iplaneDrawingPreview","iplanePreviewPoints","iplaneAcceptedPoints"]) delete root.dataset[key];
}
export function disposeDrawingPreview(view) { clearDrawingPreview(view);view.iplaneDrawingPreview?.dispose();delete view.iplaneDrawingPreview; }
