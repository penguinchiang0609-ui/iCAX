import { createPunchWizardState, normalizePunchFeature, selectPunchTool, getPunchWizardPayload } from "./punchWizard.mjs";

const clone = value => structuredClone(value);
const uid = () => globalThis.crypto?.randomUUID?.() ?? "batch-" + Date.now() + "-" + Math.random().toString(36).slice(2);
const shapeKeys = Object.freeze(["type", "recordKind", "toolRef", "toolLabel", "toolKind", "toolTarget", "toolParameters", "section",
  "diameter", "spanAlong", "spanAcross", "cornerRadius", "blindHole", "cutDepth", "opposite", "allowOpen", "toolSnapshot", "frozenTool", "frozenCut"]);
const columns = Object.freeze(["name", "length", "quantity", "lengthDatum", "material"]);
const datums = new Set(["long", "center", "short"]);
const placementOperationKeys = new Set(["angle", "azimuth", "roll", "direction", "length", "offsetY", "offsetZ", "rotation", "trim", "datum", "axialOffset", "offset", "face"]);

function sharedOperationKeys(record, wizard) {
  return (wizard?.tools?.find(tool => tool.id === record.toolRef?.id)?.operationParameters ?? [])
    .map(definition => definition.key).filter(key => !placementOperationKeys.has(key));
}

export function punchBatchDefinitionRecipe(record = {}, wizard = null) {
  const keys = [...new Set([...shapeKeys, ...sharedOperationKeys(record, wizard)])];
  return Object.fromEntries(keys.filter(key => record[key] !== undefined).map(key => [key, clone(record[key])]));
}

function stable(value) {
  if (Array.isArray(value)) return value.map(stable);
  if (!value || typeof value !== "object") return value;
  return Object.fromEntries(Object.keys(value).sort().map(key => [key, stable(value[key])]));
}
const signature = recipe => JSON.stringify(stable(recipe));

export function punchBatchSectionInputKey(section) {
  return signature([section?.source, section?.key, section?.ref, section?.parameters]);
}

// A shared definition can finish generating while another part/definition is
// selected. Publish only its still-current input, preserving placement and
// existing section objects used by in-flight request guards.
export function installPunchBatchSectionPreview(view, batch, definitionId, section, inputKey) {
  if (view.tubeDesignerPunchBatch !== batch) return false;
  const definition = batch.definitions.find(item => item.id === definitionId);
  if (!definition || punchBatchSectionInputKey(definition.recipe.section) !== inputKey) return false;
  const install = target => {
    if (punchBatchSectionInputKey(target) !== inputKey) return false;
    target.profile = clone(section.profile);
    target.name = section.name;
    return true;
  };
  install(definition.recipe.section);
  for (const part of batch.parts) {
    if (part.completedPartId) continue;
    let changed = false;
    for (const record of [...(part.wizard.features ?? []), part.wizard.draft].filter(Boolean)) {
      if (record.batchDefinitionId === definitionId && install(record.section)) changed = true;
    }
    if (changed) {
      part.wizard.revision = (part.wizard.revision ?? 0) + 1;
      part.wizard.error = "";
    }
  }
  return true;
}

function applyDefinition(record, definition, wizard = null) {
  const previousOperations = sharedOperationKeys(record, wizard);
  if (wizard && definition.recipe.toolRef?.id !== record.toolRef?.id
    && wizard.tools?.some(tool => tool.id === definition.recipe.toolRef?.id)) {
    // Changing a circle to a section cutter must initialise the latter's own
    // placement defaults. Its independent placement is retained when present.
    const placement = Object.fromEntries([...placementOperationKeys].filter(key => record[key] !== undefined).map(key => [key, clone(record[key])]));
    selectPunchTool(wizard, record, definition.recipe.toolRef.id);
    Object.assign(record, placement);
  }
  for (const key of new Set([...shapeKeys, ...previousOperations, ...sharedOperationKeys(record, wizard), ...Object.keys(definition.recipe)])) delete record[key];
  Object.assign(record, clone(definition.recipe));
  record.batchDefinitionId = definition.id;
  return record;
}

export function currentPunchBatchPart(view) {
  const batch = view?.tubeDesignerPunchBatch;
  return batch?.parts.find(part => part.id === batch.selectedPartId) ?? null;
}

export function createPunchBatch(view) {
  const draft = view?.tubeDesignerNestingPunchPartDraft;
  const wizard = view?.tubeDesignerPunchWizard;
  if (!draft || !wizard) return null;
  const part = { id: uid(), draft, wizard, completedPartId: "" };
  draft.lengthDatum ??= "long";
  wizard.batchPartId = part.id;
  const batch = { parts: [part], selectedPartId: part.id, selectedPartIds: [], definitions: [], selectedDefinitionId: "",
    profileKey: draft.profileKey, profileParameters: wizard.creationInput?.profileParameters ?? {}, error: "", revision: 0 };
  batch.profileSignature = signature([batch.profileKey, batch.profileParameters]);
  view.tubeDesignerPunchBatch = batch;
  syncPunchBatch(view);
  return batch;
}

/** Stock advanced fields use a separate draft until the modal is confirmed. */
export function openPunchBatchProfileAdvanced(view, profileKey, parameters, opener = null) {
  const batch = view?.tubeDesignerPunchBatch;
  if (!batch || batch.profileAdvancedEditor) return batch?.profileAdvancedEditor ?? null;
  const editor = {
    profileKey: String(profileKey), parameters: clone(parameters),
    originalParameters: clone(parameters), error: "",
    openerElement: opener?.nodeType === 1 ? opener : null,
    openerData: opener?.dataset ? { ...opener.dataset } : null,
  };
  batch.profileAdvancedEditor = editor;
  return editor;
}

export function updatePunchBatchProfileAdvanced(view, key, value) {
  const editor = view?.tubeDesignerPunchBatch?.profileAdvancedEditor;
  if (!editor || Object.is(editor.parameters[key], value)) return false;
  // Keep the transaction identity stable while conditional fields are patched.
  editor.parameters[key] = value;
  editor.error = "";
  return true;
}

export function closePunchBatchProfileAdvanced(view) {
  const batch = view?.tubeDesignerPunchBatch, editor = batch?.profileAdvancedEditor;
  if (!editor) return null;
  batch.profileAdvancedEditor = null;
  return editor;
}

export function punchBatchProfileAdvancedChanged(view) {
  const editor = view?.tubeDesignerPunchBatch?.profileAdvancedEditor;
  return !!editor && signature(editor.parameters) !== signature(editor.originalParameters);
}

function bindPart(view, part) {
  const batch = view.tubeDesignerPunchBatch;
  part.draft.profileKey = batch.profileKey;
  part.wizard.batchPartId = part.id;
  part.wizard.baseLength = Number(part.draft.length);
  part.wizard.creationInput = { draft: part.draft, profileParameters: batch.profileParameters };
  view.tubeDesignerNestingPunchPartDraft = part.draft;
  view.tubeDesignerPunchWizard = part.wizard;
}

function addDefinition(batch, record, wizard = null) {
  const definition = { id: uid(), label: "H" + (batch.definitionSequence = (batch.definitionSequence ?? 0) + 1), recipe: punchBatchDefinitionRecipe(record, wizard) };
  batch.definitions.push(definition);
  record.batchDefinitionId = definition.id;
  batch.selectedDefinitionId ||= definition.id;
  return definition;
}

/** Bind any replaced undo input back to its row and propagate only shared shape fields. */
export function syncPunchBatch(view) {
  const batch = view?.tubeDesignerPunchBatch, current = currentPunchBatchPart(view);
  if (!batch || !current) return false;
  current.draft = view.tubeDesignerNestingPunchPartDraft;
  current.wizard = view.tubeDesignerPunchWizard;
  batch.profileKey = current.draft.profileKey;
  batch.profileParameters = current.wizard.creationInput?.profileParameters ?? batch.profileParameters;
  const profileSignature = signature([batch.profileKey, batch.profileParameters]);
  const stockChanged = batch.profileSignature !== profileSignature;
  batch.profileSignature = profileSignature;
  const edited = new Set();
  const originals = new Map(batch.definitions.map(definition => [definition.id, signature(definition.recipe)]));
  for (const record of [...(current.wizard.features ?? []), current.wizard.draft].filter(Boolean)) {
    if (!record.toolRef?.id) continue;
    const recipe = punchBatchDefinitionRecipe(record, current.wizard);
    let definition = batch.definitions.find(item => item.id === record.batchDefinitionId);
    if (!definition) {
      definition = batch.definitions.find(item => signature(item.recipe) === signature(recipe)) ?? addDefinition(batch, record, current.wizard);
      record.batchDefinitionId = definition.id;
    } else if (originals.get(definition.id) !== signature(recipe)) {
      definition.recipe = recipe;
      edited.add(definition.id);
    }
  }
  for (const part of batch.parts) {
    if (part.completedPartId) continue;
    part.draft.profileKey = batch.profileKey;
    part.wizard.creationInput = { draft: part.draft, profileParameters: batch.profileParameters };
    part.wizard.batchPartId = part.id;
    part.wizard.baseLength = Number(part.draft.length);
    if (stockChanged) {
      part.wizard.revision = (part.wizard.revision ?? 0) + 1;
      part.wizard.previewSourceRecipe = null;
      part.wizard.error = "";
    }
    // A successful row is a receipt, not an editable pending request.
    let changed = false;
    for (const record of [...(part.wizard.features ?? []), part.wizard.draft].filter(Boolean)) {
      if (!edited.has(record.batchDefinitionId)) continue;
      const definition = batch.definitions.find(item => item.id === record.batchDefinitionId);
      if (definition && signature(punchBatchDefinitionRecipe(record, part.wizard)) !== signature(definition.recipe)) {
        applyDefinition(record, definition, part.wizard); changed = true;
      }
    }
    if (changed) { part.wizard.revision = (part.wizard.revision ?? 0) + 1; part.wizard.error = ""; }
  }
  return true;
}

export function selectPunchBatchPart(view, id) {
  const batch = view?.tubeDesignerPunchBatch;
  const next = batch?.parts.find(part => part.id === String(id));
  if (!next || next.completedPartId) return false;
  syncPunchBatch(view);
  if (batch.selectedPartId !== next.id) {
    delete next.wizard.uiFocus; delete next.wizard.restoreParameterFocus; delete next.wizard.uiPendingClick;
    if (next.wizard.parameterEditor) next.wizard.parameterEditor.focused = true;
  }
  batch.selectedPartId = next.id;
  bindPart(view, next);
  return true;
}

function emptyPart(batch, source, draft) {
  const wizard = createPunchWizardState({ entityId: source.wizard.partId, length: Number(draft.length) });
  Object.assign(wizard, { creationMode: "main-tube-punch", tools: clone(source.wizard.tools ?? []), catalogueStatus: source.wizard.catalogueStatus });
  if (batch.selectedDefinitionId) {
    const definition = batch.definitions.find(item => item.id === batch.selectedDefinitionId);
    if (definition) applyDefinition(wizard.draft, definition, wizard);
  }
  return { id: uid(), draft, wizard, completedPartId: "" };
}

export function addPunchBatchPart(view) {
  const batch = view?.tubeDesignerPunchBatch, source = currentPunchBatchPart(view);
  if (!batch || !source) return null;
  syncPunchBatch(view);
  const part = emptyPart(batch, source, { ...clone(source.draft), name: "冲孔件 " + (batch.parts.length + 1), quantity: "1", error: "" });
  batch.parts.push(part); batch.selectedPartIds = [part.id]; batch.endEditorPartId = "";
  selectPunchBatchPart(view, part.id);
  return part;
}

export function copyPunchBatchParts(view) {
  const batch = view?.tubeDesignerPunchBatch;
  if (!batch) return [];
  syncPunchBatch(view);
  const ids = batch.selectedPartIds.length ? batch.selectedPartIds : [batch.selectedPartId];
  const copies = [];
  for (const source of batch.parts.filter(part => ids.includes(part.id))) {
    const part = clone(source);
    part.id = uid(); part.completedPartId = ""; part.draft.error = "";
    part.wizard.history = []; part.wizard.future = []; part.wizard.parameterEditor = null;
    part.wizard.preview = null; part.wizard.previewSourceRecipe = null; part.wizard.previewRecipe = null;
    part.wizard.previewPending = false; part.wizard.previewRenderError = ""; part.wizard.editingId = "";
    part.wizard.features = part.wizard.features.map(feature => ({ ...feature, id: uid() }));
    part.wizard.draft.id = uid(); part.wizard.selectedFeatureIds = []; part.wizard.selectedFeatureId = "";
    batch.parts.splice(batch.parts.indexOf(source) + 1, 0, part); copies.push(part);
  }
  batch.selectedPartIds = copies.map(part => part.id); batch.endEditorPartId = "";
  if (copies.length) selectPunchBatchPart(view, copies[0].id);
  return copies;
}

export function removePunchBatchParts(view) {
  const batch = view?.tubeDesignerPunchBatch;
  if (!batch) return false;
  syncPunchBatch(view);
  const ids = batch.selectedPartIds.length ? batch.selectedPartIds : [batch.selectedPartId];
  const remaining = batch.parts.filter(part => !ids.includes(part.id));
  if (!remaining.length) { batch.error = "请至少保留一行零件。"; return false; }
  batch.parts = remaining; batch.selectedPartIds = []; batch.endEditorPartId = "";
  if (!remaining.some(part => part.id === batch.selectedPartId)) {
    const next = remaining.find(part => !part.completedPartId);
    if (next) selectPunchBatchPart(view, next.id);
  }
  batch.error = "";
  return true;
}

export function setPunchBatchSelection(view, id, selected) {
  const batch = view?.tubeDesignerPunchBatch;
  if (!batch?.parts.some(part => part.id === id)) return false;
  const ids = new Set(batch.selectedPartIds);
  if (selected) ids.add(id); else ids.delete(id);
  batch.selectedPartIds = [...ids];
  return true;
}

export function updatePunchBatchPart(view, id, field, value) {
  const batch = view?.tubeDesignerPunchBatch, part = batch?.parts.find(item => item.id === id);
  if (!part || part.completedPartId || !columns.includes(field)) return false;
  value = String(value ?? "");
  if (part.draft[field] === value) return false;
  part.draft[field] = value; part.draft.error = ""; batch.error = "";
  if (field === "length") { part.wizard.baseLength = Number(value); part.wizard.revision = (part.wizard.revision ?? 0) + 1; }
  if (field === "lengthDatum" && datums.has(value)) {
    for (const end of Object.values(part.wizard.ends ?? {})) end.datum = value;
    part.wizard.revision = (part.wizard.revision ?? 0) + 1;
  }
  batch.revision++;
  return true;
}

export function pastePunchBatchParts(view, id, field, text) {
  const batch = view?.tubeDesignerPunchBatch, source = currentPunchBatchPart(view);
  if (!batch || !source || !columns.includes(field)) return false;
  const cells = String(text ?? "").replace(/\r\n?/g, "\n").split("\n");
  while (cells.at(-1) === "") cells.pop();
  if (!cells.length) return false;
  syncPunchBatch(view);
  const start = batch.parts.findIndex(part => part.id === id), column = columns.indexOf(field);
  if (start < 0) return false;
  for (let offset = 0; offset < cells.length; offset++) {
    let part = batch.parts[start + offset];
    if (!part) {
      part = emptyPart(batch, source, { ...clone(source.draft), name: "冲孔件 " + (batch.parts.length + 1), quantity: "1", error: "" });
      batch.parts.push(part);
    }
    cells[offset].split("\t").slice(0, columns.length - column).forEach((value, index) => updatePunchBatchPart(view, part.id, columns[column + index], value));
  }
  batch.focusCell = { partId: id, field };
  return true;
}

export function advancePunchBatchPart(view, id, field) {
  const batch = view?.tubeDesignerPunchBatch;
  const index = batch?.parts.findIndex(part => part.id === id) ?? -1;
  if (index < 0) return false;
  const part = batch.parts[index + 1] ?? addPunchBatchPart(view);
  if (!part || !selectPunchBatchPart(view, part.id)) return false;
  batch.focusCell = { partId: part.id, field: columns.includes(field) ? field : "name" };
  return true;
}

export function selectPunchBatchDefinition(view, id) {
  const batch = view?.tubeDesignerPunchBatch, definition = batch?.definitions.find(item => item.id === String(id));
  if (!definition || !view.tubeDesignerPunchWizard) return false;
  batch.selectedDefinitionId = definition.id;
  batch.definitionEditorCollapsed = false;
  if (signature(punchBatchDefinitionRecipe(view.tubeDesignerPunchWizard.draft, view.tubeDesignerPunchWizard)) !== signature(definition.recipe)) {
    view.tubeDesignerPunchWizard.revision = (view.tubeDesignerPunchWizard.revision ?? 0) + 1;
  }
  applyDefinition(view.tubeDesignerPunchWizard.draft, definition, view.tubeDesignerPunchWizard);
  return true;
}

export function addPunchBatchDefinition(view) {
  const batch = view?.tubeDesignerPunchBatch, wizard = view?.tubeDesignerPunchWizard;
  if (!batch || !wizard) return null;
  const record = normalizePunchFeature({ recordKind: "tool", station: wizard.baseLength / 2 });
  const tool = wizard.tools?.find(item => item.target === "side" && !item.requiresSection);
  if (tool) selectPunchTool(wizard, record, tool.id);
  else { batch.error = "请等待刀具模板加载完成。"; return null; }
  const definition = addDefinition(batch, record, wizard);
  selectPunchBatchDefinition(view, definition.id);
  return definition;
}

export function removePunchBatchDefinition(view, id) {
  const batch = view?.tubeDesignerPunchBatch;
  if (!batch) return false;
  id ||= batch.selectedDefinitionId;
  if (batch.parts.some(part => part.wizard.features?.some(feature => feature.batchDefinitionId === id))) {
    batch.error = "该孔型仍被零件使用，请先更换或删除对应孔位。"; return false;
  }
  if (batch.definitions.length < 2) { batch.error = "请至少保留一个孔型。"; return false; }
  batch.definitions = batch.definitions.filter(item => item.id !== id);
  for (const part of batch.parts) if (part.wizard.draft.batchDefinitionId === id) applyDefinition(part.wizard.draft, batch.definitions[0], part.wizard);
  selectPunchBatchDefinition(view, batch.definitions[0].id); batch.error = "";
  return true;
}

export function setPunchBatchHoleDefinition(view, featureId, definitionId) {
  const wizard = view?.tubeDesignerPunchWizard, batch = view?.tubeDesignerPunchBatch;
  const definition = batch?.definitions.find(item => item.id === definitionId);
  const feature = featureId === "draft" ? wizard?.draft : wizard?.features?.find(item => item.id === featureId);
  if (!definition || !feature) return false;
  applyDefinition(feature, definition, wizard);
  wizard.revision = (wizard.revision ?? 0) + 1; wizard.error = "";
  batch.selectedDefinitionId = definitionId;
  return true;
}

export function punchBatchPartPayload(part) {
  const payload = getPunchWizardPayload({ tubeDesignerPunchWizard: part.wizard });
  for (const feature of payload.features) delete feature.batchDefinitionId;
  for (const end of Object.values(payload.ends)) delete end.batchDefinitionId;
  return payload;
}

export function punchBatchTotals(batch) {
  const parts = batch?.parts ?? [];
  return { kinds: parts.length, quantity: parts.reduce((sum, part) => sum + (Number.isSafeInteger(Number(part.draft.quantity)) ? Number(part.draft.quantity) : 0), 0),
    completed: parts.filter(part => part.completedPartId).length, pending: parts.filter(part => !part.completedPartId).length };
}

export const punchBatchColumns = columns;
