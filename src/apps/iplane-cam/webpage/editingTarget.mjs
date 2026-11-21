import { geometryMethod, getWorkspaceState } from "./workspaceState.mjs";
import { invokePlane } from "./nativeOperations.mjs";

export function invokeGeometry(context, view, ops, operation, payload = {}, options = {}) {
  const sketchId = getWorkspaceState(view).activeSketchId;
  return invokePlane(context, view, ops, geometryMethod(view, operation), {
    ...payload, ...(sketchId && operation === "Create" ? { sketchId } : {}),
  }, options);
}
