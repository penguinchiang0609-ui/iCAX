import { workspaceState } from './workspaceModel.mjs';

export function hasUnappliedCutDrafts(view) {
  return !!(view.fiveNameDrafts?.size
    || [...(view.fiveCommandDrafts?.values() ?? [])].some(d => d.dirty)
    || [...(view.fiveToolAxes?.values() ?? [])].some(d => d.dirty)
    || view.fiveAxisPointEdit?.dirty
    || (view.fiveAxisLeadsOpen && view.fiveLeads?.edited)
    || (view.fiveOrder?.open && view.fiveOrder.prefix?.length)
    || Object.entries(workspaceState(view).drafts).some(([key, d]) =>
      key.startsWith('definition:') && (d.processDirty || d.cutSideDirty)));
}

// Only discard unapplied previews. The framework Database restores all
// committed engineering facts; this module never records an undo stack.
export function clearCutEditorDrafts(view) {
  view.fiveNameDrafts?.clear();
  view.fiveCommandDrafts?.clear();
  view.fiveToolAxes?.clear();
  view.fiveAxisPointEdit = null;
  if (view.fiveOrder) view.fiveOrder.prefix = [];
  const drafts = workspaceState(view).drafts;
  for (const key of Object.keys(drafts)) if (key.startsWith('definition:')) delete drafts[key];
  clearTimeout(view.fiveLeads?.timer);
  view.fiveLeads = null;
}
