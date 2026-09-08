import { encodeNestingGeometry, encodePreviewMaterial } from "./nestingPreview.mjs";
import { editAtPoint, pathEditor } from "./machiningEditor.mjs";

// Selection changes material, not geometry. Immutable edited paths get a new
// entry; abandoned edits can be collected without retaining CAD or old meshes.
const pathGeometry = new WeakMap();
let geometrySequence = 0;
const materials = new Map([["normal", 0x20b4a0ff], ["selected", 0xffd400ff], ["node", 0xff7800ff], ["draft", 0xffb454ff]]
  .map(([name, color]) => [`icax-machining-material://${name}`, encodePreviewMaterial(color)]));

export function machiningPathOverlay(view, job) {
  const editor = pathEditor(view, job);
  const revision = `${job.id}:${editor.baseRevision}:${editor.version}:${editor.selectedIds.join(",")}:${editor.mode}:${editor.nodeIndex}`;
  const resources = new Map(materials), rows = [];
  const prefix = `icax-machining-path://${revision}`;
  const selected = new Set(editor.selectedIds);
  const add = (entityId, points, closed, color, selectable = true, path = null) => {
    if (points.length < 2) return;
    let geometry = path && pathGeometry.get(path);
    if (!geometry) {
      const positions = [], ranges = [];
      for (let i = 0; i < points.length - (closed ? 0 : 1); i++) {
        ranges.push(positions.length / 3, 2, 0, 0, 0);
        positions.push(...points[i], ...points[(i + 1) % points.length]);
      }
      geometry = { url: path ? `icax-machining-geometry://${++geometrySequence}/${entityId}` : `${prefix}/${entityId}`, bytes: encodeNestingGeometry({ kind: 2, positions, ranges }) };
      if (path) pathGeometry.set(path, geometry);
    }
    resources.set(geometry.url, geometry.bytes);
    rows.push({ entityId, data: { geometry: { url: geometry.url, version: 1 }, material: { url: `icax-machining-material://${color}`, version: 1 }, geometryKind: 2, renderClass: 2, visible: true, selectable } });
  };
  for (const path of editor.paths) {
    add(`machining-path:${path.id}`, path.points, path.closed, selected.has(path.id) ? "selected" : "normal", true, path);
    if (editor.mode === "node" && selected.has(path.id)) for (const [index, p] of path.points.entries()) {
      const size = 0.8;
      add(`machining-node:${path.id}:${index}`, [[p[0] - size, p[1], p[2]], [p[0] + size, p[1], p[2]], p,
        [p[0], p[1] - size, p[2]], [p[0], p[1] + size, p[2]], p, [p[0], p[1], p[2] - size], [p[0], p[1], p[2] + size]], false, "node");
    }
  }
  add("machining-draft", editor.drawing, false, "draft", false);
  return { revision, rows, resources };
}

export function handleMachiningViewportPick(context, view, _userData, _hit, event, hits, ops, job) {
  if (view.activeAreaId !== "machining") return false;
  if (view.pending || !job) return true;
  const editor = pathEditor(view, job);
  if (editor.space === "2d") return true;
  const candidates = hits ?? (_hit ? [_hit] : []);
  const nodeHit = candidates.find(h => String(h.object?.userData?.objectId).startsWith("machining-node:"));
  const pathHit = candidates.find(h => String(h.object?.userData?.objectId).startsWith("machining-path:"));
  const hit = editor.mode === "node" ? nodeHit ?? pathHit : pathHit;
  const objectId = String(hit?.object?.userData?.objectId ?? "");
  let pathId = objectId.startsWith("machining-path:") ? objectId.slice("machining-path:".length) : "", nodeIndex = null;
  if (objectId.startsWith("machining-node:")) {
    const tail = objectId.slice("machining-node:".length), at = tail.lastIndexOf(":");
    pathId = tail.slice(0, at); nodeIndex = Number(tail.slice(at + 1));
  }
  const plane = editor.fields.plane, normal = plane === "XY" ? [0, 0, 1] : plane === "YZ" ? [1, 0, 0] : [0, 1, 0];
  let point = hit?.point ? [hit.point.x, hit.point.y, hit.point.z] : null;
  try {
    if (editor.mode === "draw" || (editor.mode === "node" && editor.movingNode && nodeIndex === null)) {
      let depth = Number(editor.fields.depth);
      if (editor.mode === "node") {
        const p = editor.paths.find(p => p.id === editor.selectedIds[0])?.points[editor.nodeIndex];
        if (p) depth = p[normal.findIndex(n => n === 1)];
      }
      point = view.viewport?.pointOnWorkPlane?.(event?.clientX, event?.clientY, normal, depth);
      if (!point) throw new Error("视线与绘图平面平行，请旋转视图或更换绘图平面。");
    }
    editAtPoint(editor, point ?? [0, 0, 0], pathId, nodeIndex, event?.ctrlKey || event?.metaKey);
    view.error = "";
  } catch (error) { view.error = error?.message ?? String(error); }
  ops.renderProject(context, view);
  return true;
}
