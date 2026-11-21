import { PlanarNodeEditor } from "../../../iCAX-UI/SDK/Cad/planarNodeEditor.mjs";
import { geometryMethod, geometryObjectKind, getSelectedObject, getWorkspaceState } from "./workspaceState.mjs";
import { invokeGeometry } from "./editingTarget.mjs";
import { userErrorMessage } from "./errorMessages.mjs";

export function synchronizeNodeEditor(context, view, ops) {
  const ui = getWorkspaceState(view), selected = getSelectedObject(context, view);
  if (ui.activeTool !== "plane.edit-nodes" || selected?.kind !== geometryObjectKind(view)) { ui.nodeEditor = null;return; }
  const key = JSON.stringify([selected.row.id, selected.row.geometryRef, selected.row.geometryVersion]);
  if (ui.nodeEditor?.key === key) return;
  const state = ui.nodeEditor = { key, id: selected.row.id, form: `nodes:${key}`, editor: null, error: "" };
  void context.sceneProxy.invoke(geometryMethod(view,"ReadGeometry"),{id: state.id},{timeoutMs:30000}).then(geometry => {
    if (view.disposed || context.isCurrentProject?.() === false || view.sceneProxy !== context.sceneProxy || ui.nodeEditor !== state) return;
    state.editor = new PlanarNodeEditor(geometry);ops?.renderProject(context,view);
  }).catch(error => { if (ui.nodeEditor !== state || context.isCurrentProject?.() === false) return;state.error = userErrorMessage(error);ops?.renderProject(context,view); });
}

export async function applyNodeEdit(context, view, ops) {
  const ui = getWorkspaceState(view), state = ui.nodeEditor;
  if (!state?.editor) throw new Error("请先选择可编辑节点的曲线。");
  const draft = ui.formDrafts[state.form] ?? {}, submitted = JSON.stringify(draft);
  await invokeGeometry(context,view,ops,"Edit",{id:state.id,...state.editor.request(draft)},{afterCommand() {
    if (JSON.stringify(ui.formDrafts[state.form] ?? {}) === submitted) delete ui.formDrafts[state.form];
  }});
}
