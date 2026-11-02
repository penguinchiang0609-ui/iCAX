import { saveCommittedProjectEdit,clearCommittedSaveError } from './projectEditPersistence.mjs';

export function clearCutSaveError(view) {
  clearCommittedSaveError(view,'fiveCutSaveError');
}

// A file-write failure must not turn an acknowledged database transaction
// into an uncertain command or prevent reading its authoritative scene state.
export async function saveCommittedCutEdit(context, view, ops) {
  return saveCommittedProjectEdit(context,view,ops,'fiveCutSaveError');
}
