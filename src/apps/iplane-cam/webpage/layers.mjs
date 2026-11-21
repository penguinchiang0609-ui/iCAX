import { renderLayerPalette, ensureLayerPaletteStyles } from "../../../iCAX-UI/SDK/Forms/layerPalette.mjs";
import { escapeAttr } from "../../_shared/workbench/utils/format.mjs";
import { getObjectRows, getSelectedObject, getSelection, getWorkspaceState, readSnapshot } from "./workspaceState.mjs";
import { invokePlane, supports } from "./nativeOperations.mjs";
import { userErrorMessage } from "./errorMessages.mjs";

const state = view => view.iplaneLayers ??= { open: false, collapsed: false, drafts: {}, newDraft: {}, error: "", deleteId: "", targetId: "", queued: 0, busy: false };
const current = (context, view) => !view.disposed && context.isCurrentProject?.() !== false && view.sceneProxy === context.sceneProxy;
const catalog = (context, view) => [...(readSnapshot(context, view).layers ?? [])].sort((a, b) => Number(a.order ?? 0) - Number(b.order ?? 0) || a.id.localeCompare(b.id));
function selection(context, view) {
  const selected = getSelectedObject(context, view);
  if (!selected) return { ids: [], rows: [], readonly: false };
  if (selected.kind === "results") return { ids: [], rows: [], readonly: true };
  const ids = new Set(getSelection(view, selected.kind));
  const rows = getObjectRows(context, view, selected.kind).filter(row => ids.has(row.id));
  return { ids: rows.map(row => row.id), rows, readonly: false };
}
function aliases({ action, id, field, value }) {
  if (action) {
    const names = { create: "layer-create", choose: "layer-assign", update: "layer-update", remove: "layer-remove" };
    return names[action] ? `data-iplane-action="${names[action]}" data-iplane-layer-id="${escapeAttr(id)}"${field ? ` data-iplane-layer-field="${escapeAttr(field)}" data-iplane-layer-value="${escapeAttr(value)}"` : ""}` : "";
  }
  if (["newName", "newColor", "name", "color"].includes(field)) return `data-iplane-form="layers" data-iplane-form-field="${escapeAttr(field === "name" ? id : field === "color" ? `color:${id}` : field)}"`;
  return "";
}
export function renderPlaneLayers(context, view) {
  if (!supports(context, view, "PlaneLayer.Update")) return "";
  const s = state(view), ui = getWorkspaceState(view), chosen = selection(context, view), snapshot = readSnapshot(context, view);
  const html = renderLayerPalette({ layers: catalog(context, view), currentId: snapshot.currentLayerId ?? "",
    selectedLayerIds: chosen.rows.map(row => row.layerId), selectionCount: chosen.rows.length, selectionReadonly: chosen.readonly,
    selectionLabel: ui.activeSketchId ? "草图曲线" : view.activeAreaId === "drawing" ? "刀路" : "源对象",
    ...s, busy: ui.busy || Boolean(ui.geometryTaskId) || s.queued > 0, attributes: aliases });
  return html.replace(/data-layer-ui-key="([^"]+)"/g, 'data-layer-ui-key="$1" data-iplane-key="layer:$1"');
}
export function ensurePlaneLayerStyles(document) { ensureLayerPaletteStyles(document); }
export function openLayerManager(view) { const s = state(view);s.open = true;s.collapsed = false;s.error = ""; }
function paint(context, view, ops) { if (current(context, view)) { ops?.renderProject(context, view);context.actions?.refreshProductRibbon?.(); } }
function enqueue(context, view, ops, action) {
  const s = state(view);s.queued++;
  context.actions?.refreshProjectHistoryControls?.();paint(context, view, ops);
  const next = (s.queue ?? Promise.resolve()).then(async () => {
    if (!current(context, view)) return false;
    s.busy = true;s.error = "";
    try { await action();return true; }
    catch (error) { s.error = userErrorMessage(error);s.open = true;return false; }
    finally { s.busy = false; }
  }).finally(() => { s.queued--;context.actions?.refreshProjectHistoryControls?.();paint(context, view, ops); });
  s.queue = next.catch(() => false);return next;
}
function update(context, view, ops, id, field, value) {
  const s = state(view), raw = value, normalized = field === "name" ? String(value).trim() : value;
  return enqueue(context, view, ops, async () => {
    await invokePlane(context, view, ops, "PlaneLayer.Update", { id, [field]: normalized }, { afterCommand() {
      if (s.drafts[id]?.[field] === raw) delete s.drafts[id][field];
      if (s.drafts[id] && !Object.keys(s.drafts[id]).length) delete s.drafts[id];
    } });
  });
}
export function inputPlaneLayer(view, target) {
  const s = state(view), field = target.dataset.layerUiField, id = target.dataset.layerUiId;
  if (field === "targetId") s.targetId = target.value;
  else if (field === "newName" || field === "newColor") s.newDraft[field === "newName" ? "name" : "color"] = target.value;
  else if (id && ["name", "color"].includes(field)) (s.drafts[id] ??= {})[field] = target.value;
}
export async function changePlaneLayer(context, view, ops, target) {
  inputPlaneLayer(view, target);
  const field = target.dataset.layerUiField, id = target.dataset.layerUiId;
  if (id && ["name", "color"].includes(field)) await update(context, view, ops, id, field, target.value);
}
export async function handlePlaneLayerAction(context, view, ops, target) {
  const s = state(view), action = target.dataset.layerUiAction, id = target.dataset.layerUiId;
  if (action === "toggle") { s.collapsed = !s.collapsed;if (s.collapsed) s.open = false;paint(context, view, ops);return; }
  if (action === "manage" || action === "close" || action === "delete-cancel") {
    s.open = action === "manage" ? !s.open : action === "close" ? false : s.open;s.deleteId = "";s.error = "";if (s.open) s.collapsed = false;paint(context, view, ops);return;
  }
  if (s.queued || getWorkspaceState(view).busy || getWorkspaceState(view).geometryTaskId) return;
  if (action === "choose") {
    const chosen = selection(context, view);if (chosen.readonly) return;
    await enqueue(context, view, ops, () => invokePlane(context, view, ops, chosen.ids.length ? "PlaneLayer.Assign" : "PlaneLayer.SetCurrent", chosen.ids.length ? { layerId: id, ids: chosen.ids } : { id }));
  } else if (action === "create" || action === "add") {
    const draft = action === "add" ? {} : { ...s.newDraft }, layers = catalog(context, view);
    let number = 1;while (layers.some(layer => layer.name === `图层 ${number}`)) number++;
    await enqueue(context, view, ops, async () => {
      await invokePlane(context, view, ops, "PlaneLayer.Create", { name: draft.name ?? `图层 ${number}`, color: draft.color ?? "#3B82F6" }, { afterCommand() {
        for (const field of ["name", "color"]) if (s.newDraft[field] === draft[field]) delete s.newDraft[field];
      } });s.open = true;s.collapsed = false;
    });
  } else if (action === "update") {
    const field = target.dataset.layerUiField;
    if (field) await update(context, view, ops, id, field, target.dataset.layerUiValue === "true");
  } else if (action === "remove") {
    const snapshot = readSnapshot(context, view);
    const used = snapshot.currentLayerId === id || ["drawing", "parts", "stocks", "sketch-curves"].some(kind => getObjectRows(context, view, kind).some(row => row.layerId === id));
    const requestReceivingLayer = () => { s.open = true;s.deleteId = id;s.targetId = catalog(context, view).find(layer => layer.id !== id && !layer.locked)?.id ?? "";s.error = "";paint(context, view, ops); };
    if (used) requestReceivingLayer();
    else await enqueue(context, view, ops, async () => {
      try { await invokePlane(context, view, ops, "PlaneLayer.Remove", { id }); }
      catch (error) {
        // Inactive sketches and edit backups are deliberately not a second
        // frontend authority. Let the native reference check request migration.
        if (/PLANE_LAYER_IN_USE|PLANE_LAYER_DELETE_TARGET_REQUIRED/.test(error?.message ?? String(error))) {
          getWorkspaceState(view).operationError = "";requestReceivingLayer();return;
        }
        throw error;
      }
    });
  } else if (action === "delete-confirm" && s.deleteId && s.targetId) {
    const source = s.deleteId, receiving = s.targetId;
    if (await enqueue(context, view, ops, () => invokePlane(context, view, ops, "PlaneLayer.Remove", { id: source, targetLayerId: receiving }))) { s.deleteId = "";s.targetId = "";paint(context, view, ops); }
  }
}
export async function flushPlaneLayerEdits(context, view, ops) {
  const s = state(view);await s.queue;
  for (const [id, draft] of Object.entries(s.drafts)) {
    const layer = catalog(context, view).find(layer => layer.id === id);
    if (!layer) { delete s.drafts[id];continue; }
    for (const field of ["name", "color"]) {
      if (draft[field] === undefined) continue;
      if (draft[field] === layer[field]) { delete draft[field];continue; }
      if (!await update(context, view, ops, id, field, draft[field])) throw new Error(s.error || "图层修改尚未保存。");
    }
  }
  await s.queue;
  if (s.error) throw new Error(s.error);
  if (Object.keys(s.newDraft).some(field => String(s.newDraft[field]).trim())) throw new Error("新图层尚未创建，请先创建图层或清空新增输入。");
}
export function planeLayerPending(view) { return (view?.iplaneLayers?.queued ?? 0) > 0 || Boolean(view?.iplaneLayers?.busy); }
