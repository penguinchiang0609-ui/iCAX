export const AREA_IDS = Object.freeze(["drawing", "nesting"]);
export const AREA_TITLES = Object.freeze({ drawing: "底图", nesting: "下料" });
export const OBJECT_KINDS = Object.freeze(["drawing", "parts", "stocks", "results", "sketch-curves"]);
export const OBJECT_TITLES = Object.freeze({ drawing: "刀路", parts: "零件", stocks: "板材", results: "排样结果", "sketch-curves": "草图曲线" });
const collections = Object.freeze({ drawing: "drawings", parts: "parts", stocks: "stocks", results: "results", "sketch-curves": "sketchCurves" });

export function normalizeAreaId(value) { return AREA_IDS.includes(value) ? value : "drawing"; }

export function getWorkspaceState(view) {
  view.iplaneCamUI ??= {
    activeTool: "plane.select", snapEnabled: true, gridVisible: true, orthoEnabled: false,
    selectedIdsByKind: { drawing: [], parts: [], stocks: [], results: [] },
    selectionKind: "parts", canvasObject: null,
    drafts: {}, cameraByArea: {}, commandInput: "", commandStatus: "选择图形或对象。",
  };
  const ui = view.iplaneCamUI;
  ui.selectedIdsByKind ??= {
    drawing: ui.selectedIdsByArea?.drawing ?? [], parts: ui.selectedIdsByArea?.parts ?? [],
    stocks: ui.selectedIdsByArea?.stocks ?? [], results: ui.selectedIdsByArea?.nesting ?? [],
  };
  ui.selectedIdsByKind["sketch-curves"] ??= [];
  ui.selectionKind = ["parts", "stocks", "results", "sketch-curves"].includes(ui.selectionKind) ? ui.selectionKind : "parts";
  ui.panelMode ??= "object";ui.formDrafts ??= {};ui.commandPoints ??= [];
  return ui;
}

export function readSnapshot(context, view) {
  return view.scene?.iplaneCam ?? context.scene?.iplaneCam ?? context.sceneProxy?.state?.iplaneCam ?? {};
}

export function isGeometryEditing(view) { return Boolean(getWorkspaceState(view).activeSketchId) || normalizeAreaId(view.activeAreaId) === "drawing"; }
export function geometryObjectKind(view) { return getWorkspaceState(view).activeSketchId ? "sketch-curves" : "drawing"; }
export function geometryMethod(view, method) { return `${getWorkspaceState(view).activeSketchId ? "PlaneSketch" : "PlaneDrawing"}.${method}`; }
export function activeSketch(context, view) { return (readSnapshot(context, view).sketches ?? []).find(row => row.id === getWorkspaceState(view).activeSketchId) ?? null; }

export function leaveSketchContext(view) {
  const ui = getWorkspaceState(view), previous = ui.sketchReturn;
  ui.activeSketchId = "";ui.nodeEditor = null;delete ui.sketchReturn;
  if (previous) {
    ui.selectionKind = previous.selectionKind;ui.selectedIdsByKind = previous.selectedIdsByKind;ui.canvasObject = previous.canvasObject;
    ui.formDrafts = previous.formDrafts ?? {};
  }
  ui.panelMode = "object";
}

export function restoreSketchContext(context, view) {
  const ui = getWorkspaceState(view);
  if (ui.busy || ui.geometryTaskId) return;
  const sketches = readSnapshot(context, view).sketches ?? [];
  if (ui.activeSketchId) {
    if (sketches.some(row => row.id === ui.activeSketchId && row.status === "Editing")) return;
    leaveSketchContext(view);return "left";
  }
  const sketch = sketches.find(row => row.status === "Editing");
  if (!sketch) return;
  ui.sketchReturn = { selectionKind: sketch.ownerKind, selectedIdsByKind: { ...structuredClone(ui.selectedIdsByKind), [sketch.ownerKind]: [sketch.ownerId] }, canvasObject: ui.canvasObject ? { ...ui.canvasObject } : null, formDrafts: ui.formDrafts };
  ui.formDrafts = {};ui.activeSketchId = sketch.id;ui.selectionKind = "sketch-curves";ui.panelMode = "object";
  return "entered";
}

export function getObjectRows(context, view, kind) {
  if (!OBJECT_KINDS.includes(kind)) return [];
  const snapshot = readSnapshot(context, view);
  let source = Array.isArray(snapshot)
    ? snapshot.filter(item => item?.area === kind || item?.kind === collections[kind] || item?.kind === kind
      || kind === "results" && item?.area === "nesting")
    : snapshot?.[collections[kind]];
  if (kind === "sketch-curves" && getWorkspaceState(view).activeSketchId) source = (source ?? []).filter(row => row.sketchId === getWorkspaceState(view).activeSketchId);
  const rows = [], seen = new Set();
  const visit = (item, depth = 0, ancestorIds = []) => {
    if (!item || typeof item !== "object") return;
    const id = String(item.id ?? item.entityId ?? "").trim();
    if (!id || seen.has(id)) return;
    seen.add(id);
    const entityIds = Array.isArray(item.entityIds) ? item.entityIds : item.entityId != null ? [item.entityId] : [];
    rows.push({ ...item, id, depth, ancestorIds, objectKind: kind, name: String(item.name ?? item.label ?? id),
      entityIds: entityIds.filter(value => value != null).map(value => String(value).trim()).filter(Boolean) });
    const children = item.children ?? item.entities ?? item.boards ?? item.placements;
    if (Array.isArray(children)) children.forEach(child => visit(child, depth + 1, [...ancestorIds, id]));
  };
  if (Array.isArray(source)) source.forEach(item => visit(item));
  return rows;
}

export function getAreaRows(context, view, area = view.activeAreaId) {
  if (getWorkspaceState(view).activeSketchId) return getObjectRows(context, view, "sketch-curves");
  return normalizeAreaId(area) === "drawing" ? getObjectRows(context, view, "drawing")
    : ["parts", "stocks", "results"].flatMap(kind => getObjectRows(context, view, kind));
}

export function getSelection(view, kind) {
  const ui = getWorkspaceState(view);
  kind ??= ui.activeSketchId ? "sketch-curves" : normalizeAreaId(view.activeAreaId) === "drawing" ? "drawing" : ui.selectionKind;
  return ui.selectedIdsByKind[kind] ?? [];
}

export function selectObject(view, kind, objectId, additive = false) {
  if (!OBJECT_KINDS.includes(kind)) return;
  const ui = getWorkspaceState(view), previous = getSelection(view, kind);
  ui.panelMode = "object";
  ui.panelEpoch = (ui.panelEpoch ?? 0) + 1;
  objectId = String(objectId ?? "");
  if (kind !== "drawing") ui.selectionKind = kind;
  ui.selectedIdsByKind[kind] = !objectId ? [] : additive
    ? previous.includes(objectId) ? previous.filter(id => id !== objectId) : [...previous, objectId]
    : [objectId];
  if (objectId && kind === "stocks") ui.canvasObject = { kind, id: objectId };
  if (objectId && kind === "results") {
    const row = getObjectRows({}, view, "results").find(item => item.id === objectId);
    if (row && row.depth <= 1) ui.canvasObject = { kind, id: objectId };
  }
}

export function getSelectedObject(context, view) {
  const ui = getWorkspaceState(view), area = normalizeAreaId(view.activeAreaId || context.activeRibbonTabId);
  const kind = ui.activeSketchId ? "sketch-curves" : area === "drawing" ? "drawing" : ui.selectionKind;
  const row = getObjectRows(context, view, kind).find(item => item.id === getSelection(view, kind).at(-1));
  return row ? { kind, row } : null;
}

export function getSelectedRow(context, view, kind) {
  return kind ? getObjectRows(context, view, kind).find(row => row.id === getSelection(view, kind).at(-1)) ?? null
    : getSelectedObject(context, view)?.row ?? null;
}

function resolveCanvasObject(kind, rows, row) {
  if (kind !== "results" || !Array.isArray(row.boards) || !row.boards.length) return { kind, row };
  // Each physical board has its own local origin. A plan opens its first board
  // rather than drawing every board on top of the others.
  for (const board of row.boards) {
    const id = String(board?.id ?? board?.entityId ?? "").trim();
    const firstBoard = rows.find(item => item.id === id && item.ancestorIds.at(-1) === row.id);
    if (firstBoard) return { kind, row: firstBoard };
  }
  return null;
}

export function getCanvasObject(context, view, area = view.activeAreaId) {
  const sketch = activeSketch(context, view);if (sketch) return { kind: "sketch", row: sketch };
  if (normalizeAreaId(area) === "drawing") {
    const rows = getObjectRows(context, view, "drawing"), selected = getSelection(view, "drawing").at(-1);
    const row = rows.find(item => item.id === selected) ?? rows[0];
    return row ? { kind: "drawing", row } : null;
  }
  const ui = getWorkspaceState(view), saved = ui.canvasObject;
  if (saved && ["stocks", "results"].includes(saved.kind)) {
    const rows = getObjectRows(context, view, saved.kind), row = rows.find(item => item.id === saved.id);
    if (row) return resolveCanvasObject(saved.kind, rows, row);
  }
  for (const kind of ["results", "stocks"]) {
    const rows = getObjectRows(context, view, kind), row = rows[0];
    if (row) { ui.canvasObject = { kind, id: row.id };return resolveCanvasObject(kind, rows, row); }
  }
  ui.canvasObject = null;
  return null;
}

function collectEntityIds(rows, ids) {
  return rows.filter(row => ids.has(row.id) || row.ancestorIds.some(id => ids.has(id))).flatMap(row => row.entityIds);
}

export function getAreaEntityIds(context, view, area = view.activeAreaId, selectedOnly = false) {
  if (getWorkspaceState(view).activeSketchId) {
    const selected = new Set(getSelection(view, "sketch-curves"));
    return getObjectRows(context, view, "sketch-curves").filter(row => row.visible !== false && (!selectedOnly || selected.has(row.id))).flatMap(row => row.entityIds);
  }
  area = normalizeAreaId(area);
  if (area === "drawing") {
    const rows = getObjectRows(context, view, "drawing").filter(row => row.visible !== false);
    const selected = new Set(getSelection(view, "drawing"));
    return [...new Set((selectedOnly ? rows.filter(row => selected.has(row.id)) : rows).flatMap(row => row.entityIds))];
  }
  const canvas = getCanvasObject(context, view, area);
  if (!canvas) return [];
  const visible = [...new Set(collectEntityIds(getObjectRows(context, view, canvas.kind).filter(row => row.visible !== false), new Set([canvas.row.id])))];
  if (!selectedOnly) return visible;
  const selectedKind = area === "drawing" ? "drawing" : getWorkspaceState(view).selectionKind;
  if (selectedKind === "parts") {
    const selectedParts = new Set(getSelection(view, "parts"));
    const placementEntities = new Set(getObjectRows(context, view, "results").filter(row => selectedParts.has(String(row.partId ?? ""))).flatMap(row => row.entityIds));
    return visible.filter(id => placementEntities.has(id));
  }
  const selectedEntities = new Set(collectEntityIds(getObjectRows(context, view, selectedKind), new Set(getSelection(view, selectedKind))));
  return visible.filter(id => selectedEntities.has(id));
}

export function draftKey(kind, row) { return `${kind}:${typeof row === "object" ? row.id : row}`; }

export function fieldValue(view, kind, row, field) {
  const draft = getWorkspaceState(view).drafts[draftKey(kind, row)];
  return draft && Object.hasOwn(draft, field) ? draft[field] : String(row[field] ?? "");
}

export function updateDraft(view, kind, row, field, value) {
  if (!["name", "note", "quantity"].includes(field)) return;
  const ui = getWorkspaceState(view), key = draftKey(kind, row);
  ui.drafts[key] ??= {};
  if (String(value) === String(row[field] ?? "")) delete ui.drafts[key][field];
  else ui.drafts[key][field] = String(value);
  if (!Object.keys(ui.drafts[key]).length) delete ui.drafts[key];
}

export function hasPropertyDrafts(view) {
  return Object.values(getWorkspaceState(view).drafts).some(draft => Object.keys(draft).length > 0);
}

export function clearSelectedDraft(context, view) {
  const selected = getSelectedObject(context, view);
  if (selected) delete getWorkspaceState(view).drafts[draftKey(selected.kind, selected.row)];
}
