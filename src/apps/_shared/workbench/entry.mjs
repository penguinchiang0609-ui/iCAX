import { createWorkbench } from "./createWorkbench.mjs";
export { RENDER_ENTITY_VIEW_PROJECTION } from "./projection.mjs";
const workbench = createWorkbench();
export const { mountProduct, mountProject, synchronizeActiveAreaView, waitForActiveAreaAction, handleRibbonCommand } = workbench;
