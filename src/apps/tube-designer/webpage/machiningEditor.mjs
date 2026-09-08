import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { copyPaths, createPath, distance3, editPathPoint, mergePaths, nearestPathSegment, pathLength, point3, projectSidePoint, splitPath, transformPaths, unprojectSidePoint } from "./machiningPathGeometry.mjs";

const PREFIX = "tube-path-";
export function pathEditor(view, job) {
  if (!job) return null;
  view.tubeDesignerMachining ??= {};
  const edits = view.tubeDesignerMachining.edits ??= {};
  let editor = edits[job.id];
  const revision = job.analysis?.revision ?? "";
  if (!editor || (!editor.dirty && editor.baseRevision !== revision)) {
    editor = edits[job.id] = { jobId: job.id, baseRevision: revision, paths: structuredClone(job.analysis?.paths ?? []),
      selectedIds: [], history: [], future: [], version: 0, dirty: false, mode: "select", space: "3d", drawing: [], nodeIndex: 0,
      fields: { plane: "XZ", depth: "0", dx: "0", dy: "0", dz: "0", angle: "0", axis: "Y", px: "0", py: "0", pz: "0", nx: "0", ny: "0", nz: "0" } };
  }
  editor.conflict = editor.baseRevision !== revision;
  return editor;
}
export function hasUnsavedMachiningPaths(view) {
  return Object.values(view.tubeDesignerMachining?.edits ?? {}).some(e => e.dirty || e.drawing.length);
}
function remember(editor, paths, selectedIds = editor.selectedIds) {
  editor.history.push({ paths: editor.paths, selectedIds: [...editor.selectedIds] });
  // Immutable path objects are shared by history; only changed paths allocate.
  while (editor.history.length > 20) editor.history.shift();
  editor.future = [];
  editor.paths = paths;
  editor.selectedIds = selectedIds.filter(id => paths.some(p => p.id === id));
  editor.dirty = true;
  editor.version++;
}
export function selectMachiningPath(editor, id, toggle = false) {
  const ids = new Set(toggle ? editor.selectedIds : []);
  if (toggle && ids.has(id)) ids.delete(id); else if (editor.paths.some(p => p.id === id)) ids.add(id);
  editor.selectedIds = [...ids];
  editor.nodeIndex = 0;
  syncNodeFields(editor);
}
function syncNodeFields(editor) {
  const point = editor.paths.find(p => p.id === editor.selectedIds[0])?.points[editor.nodeIndex];
  if (point) [editor.fields.nx, editor.fields.ny, editor.fields.nz] = point.map(x => String(Number(x.toFixed(6))));
}
export function renderMachiningPathList(view, job) {
  const editor = pathEditor(view, job);
  if (!editor) return "";
  const diagnostics = job.analysis?.diagnostics ?? [];
  return `<section class="tube-machining-paths"><header class="tube-machining-pane-title"><strong>刀路列表</strong><span>${editor.paths.length} 条${editor.dirty ? " · 未保存" : ""}</span></header>
    <div class="tube-machining-path-list">${editor.paths.length ? editor.paths.map((p, index) => `<div class="tube-machining-path-row ${editor.selectedIds.includes(p.id) ? "active" : ""}"><input type="checkbox" aria-label="选择 ${escapeAttr(p.name)}" data-cam-change-action="${PREFIX}check" data-path-id="${escapeAttr(p.id)}" ${editor.selectedIds.includes(p.id) ? "checked" : ""}><button data-cam-action="${PREFIX}select" data-path-id="${escapeAttr(p.id)}"><span>${index + 1}</span><strong>${escapeText(p.name)}</strong><small>${p.closed ? "闭合" : "开放"} · ${formatNumber(pathLength(p))} mm</small></button></div>`).join("") : `<p class="tube-machining-note">点击“分析所选 / 分析全部”提取刀路，或直接绘制新刀路。</p>`}</div>
    ${diagnostics.length ? `<p class="tube-machining-warning" title="${escapeAttr(diagnostics.map(d => `${d.part}: ${d.message}`).join("\n"))}">${diagnostics.length} 项识别提示：部分几何可能尚未解析，请核对模型。</p>` : ""}
    ${job.analysis ? `<p class="tube-machining-note">刀路独立保存。原 CAD 变化不会覆盖编辑。</p>` : ""}</section>`;
}
const field = (editor, key, label, opts = "") => `<label><span>${label}</span><input type="number" step="any" ${opts} value="${escapeAttr(editor.fields[key])}" data-path-field="${key}" data-cam-change-action="${PREFIX}field"></label>`;
export function renderMachiningPathProperties(view, job) {
  const editor = pathEditor(view, job), path = editor?.paths.find(p => p.id === editor.selectedIds[0]);
  if (!editor) return "";
  return `<section class="tube-machining-path-properties"><header class="tube-machining-pane-title"><strong>刀路属性</strong><span>${editor.selectedIds.length ? `已选 ${editor.selectedIds.length} 条` : "未选择"}</span></header>
    ${editor.conflict ? `<p class="tube-machining-warning">已保存的刀路版本发生变化，请放弃当前草稿后重新打开。</p>` : ""}
    ${path ? `<dl class="tube-machining-facts"><dt>名称</dt><dd>${escapeText(path.name)}</dd><dt>编号</dt><dd>${escapeText(path.id)}</dd><dt>形状</dt><dd>${path.closed ? "闭合" : "开放"}三维折线</dd><dt>长度</dt><dd>${formatNumber(pathLength(path))} mm</dd><dt>节点数</dt><dd>${path.points.length}</dd><dt>来源</dt><dd>${({ analysis: "CAD 解析", copy: "刀路副本", drawn: "手工绘制", edited: "已编辑" })[path.origin] ?? "独立刀路"}</dd><dt>刀姿</dt><dd>${path.directions ? "带解析方向（待工艺校验）" : "待工艺设置"}</dd><dt>轨迹类型</dt><dd>名义刀路（离散采样，未刀补）</dd></dl>
      <h4>平移 / 旋转（所选刀路）</h4><div class="tube-machining-field-grid">${field(editor, "dx", "ΔX / mm")}${field(editor, "dy", "ΔY / mm")}${field(editor, "dz", "ΔZ / mm")}${field(editor, "angle", "旋转角 / °")}<label><span>旋转轴</span><select data-path-field="axis" data-cam-change-action="${PREFIX}field">${["X", "Y", "Z"].map(v => `<option ${editor.fields.axis === v ? "selected" : ""}>${v}</option>`).join("")}</select></label></div>
      <small>旋转中心（世界坐标）</small><div class="tube-machining-field-grid">${field(editor, "px", "X / mm")}${field(editor, "py", "Y / mm")}${field(editor, "pz", "Z / mm")}</div><div class="tube-machining-button-row"><button data-cam-action="${PREFIX}pivot">以所选中心旋转</button><button data-cam-action="${PREFIX}transform">应用变换</button></div>
      <h4>节点编辑</h4><label class="tube-machining-node-index">节点序号<input type="number" min="1" max="${path.points.length}" value="${editor.nodeIndex + 1}" data-cam-change-action="${PREFIX}node-index"></label><div class="tube-machining-field-grid">${field(editor, "nx", "X / mm")}${field(editor, "ny", "Y / mm")}${field(editor, "nz", "Z / mm")}</div><button data-cam-action="${PREFIX}node-save">更新节点</button>` : `<p class="tube-machining-note">在场景或上方列表选择刀路，查看信息并编辑。</p>`}
    <div class="tube-machining-button-row"><button class="tube-designer-primary" data-cam-action="${PREFIX}save" ${!editor.dirty || view.pending || editor.conflict ? "disabled" : ""}>保存刀路</button><button data-cam-action="${PREFIX}discard" ${!editor.dirty && !editor.drawing.length ? "disabled" : ""}>放弃编辑</button></div></section>`;
}
export function renderMachiningEditToolbar(view, job) {
  const editor = pathEditor(view, job);
  if (!editor) return "";
  return `<div class="tube-machining-edit-toolbar"><div>${[["select", "选择"], ["draw", "绘制折线"], ["node", "编辑节点"], ["break", "打断"]].map(([id, label]) => `<button data-cam-action="${PREFIX}mode" data-path-mode="${id}" class="${editor.mode === id ? "active" : ""}">${label}</button>`).join("")}
    <button data-cam-action="${PREFIX}copy">复制</button><button data-cam-action="${PREFIX}merge">合并</button><button data-cam-action="${PREFIX}delete">删除</button><button data-cam-action="${PREFIX}undo" ${editor.history.length ? "" : "disabled"}>撤销</button><button data-cam-action="${PREFIX}redo" ${editor.future.length ? "" : "disabled"}>重做</button><button data-cam-action="${PREFIX}space">${editor.space === "2d" ? "返回三维" : "二维侧面编辑"}</button></div>
    <div><label>绘图平面 <select data-path-field="plane" data-cam-change-action="${PREFIX}field">${["XZ", "XY", "YZ"].map(v => `<option ${editor.fields.plane === v ? "selected" : ""}>${v}</option>`).join("")}</select></label><label>深度 / mm <input type="number" step="any" value="${escapeAttr(editor.fields.depth)}" data-path-field="depth" data-cam-change-action="${PREFIX}field"></label>
    ${editor.mode === "draw" ? `<button data-cam-action="${PREFIX}finish">完成开放刀路</button><button data-cam-action="${PREFIX}close">闭合刀路</button><button data-cam-action="${PREFIX}cancel-draw">取消绘制</button>` : ""}
    <span>${editor.mode === "draw" ? `在工作平面点击绘制 · 已选 ${editor.drawing.length} 点` : editor.mode === "node" ? "先选节点，再点击目标位置；也可在属性栏输入坐标" : editor.mode === "break" ? "点击刀路内部打断" : "点击选择 · Ctrl 多选 · 黄色为所选刀路"}</span></div></div>`;
}
export function sideLayout(editor) {
  if (!editor.sideFrame || editor.sideFrame.plane !== editor.fields.plane) {
    const min = [Infinity, Infinity], max = [-Infinity, -Infinity];
    for (const path of editor.paths) for (const point of path.points) {
      const p = projectSidePoint(point, editor.fields.plane);
      for (let i = 0; i < 2; i++) { min[i] = Math.min(min[i], p[i]); max[i] = Math.max(max[i], p[i]); }
    }
    if (!Number.isFinite(min[0])) { min.fill(-100); max.fill(100); }
    editor.sideFrame = { plane: editor.fields.plane, scale: Math.min(900 / Math.max(max[0] - min[0], 20), 430 / Math.max(max[1] - min[1], 20)), center: min.map((v, i) => (v + max[i]) / 2) };
  }
  const { scale, center } = editor.sideFrame;
  const project = point => { const p = projectSidePoint(point, editor.fields.plane); return [500 + (p[0] - center[0]) * scale, 300 - (p[1] - center[1]) * scale]; };
  const unproject = (x, y, depth = Number(editor.fields.depth)) => unprojectSidePoint([(x - 500) / scale + center[0], -(y - 300) / scale + center[1]], editor.fields.plane, depth);
  return { project, unproject };
}
export function renderMachiningSideEditor(view, job) {
  const editor = pathEditor(view, job);
  if (editor?.space !== "2d") return "";
  const layout = sideLayout(editor);
  const coordinates = points => points.map(p => layout.project(p).join(",")).join(" ");
  return `<div class="tube-machining-side-editor"><svg viewBox="0 0 1000 600" data-machining-side-canvas aria-label="刀路二维侧面编辑" role="img"><defs><pattern id="machining-grid" width="25" height="25" patternUnits="userSpaceOnUse"><path d="M 25 0 L 0 0 0 25" fill="none" stroke="#24404b" stroke-width=".5"/></pattern></defs><rect width="1000" height="600" fill="url(#machining-grid)"/><text x="24" y="580" fill="#8eabb5" font-size="13">${escapeText(editor.fields.plane)} 侧面投影 · 原刀路深度保留 · 新刀路深度 ${escapeText(editor.fields.depth)} mm</text>
      ${editor.paths.map(path => `<polyline data-path-id="${escapeAttr(path.id)}" points="${coordinates(path.closed ? [...path.points, path.points[0]] : path.points)}" fill="none" stroke="${editor.selectedIds.includes(path.id) ? "#ffd400" : "#46d9ca"}" stroke-width="2" vector-effect="non-scaling-stroke" style="cursor:pointer;pointer-events:stroke"/>${editor.mode === "node" && editor.selectedIds.includes(path.id) ? path.points.map((p, i) => { const [x, y] = layout.project(p); return `<circle data-path-id="${escapeAttr(path.id)}" data-node-index="${i}" cx="${x}" cy="${y}" r="4" fill="${i === editor.nodeIndex ? "#ff8b42" : "#fff"}"/>`; }).join("") : ""}`).join("")}
      ${editor.drawing.length ? `<polyline points="${coordinates(editor.drawing)}" stroke="#ffae59" fill="none" stroke-width="2"/>` : ""}
    </svg></div>`;
}

export function editAtPoint(editor, point, pathId = "", nodeIndex = null, toggle = false) {
  const path = editor.paths.find(p => p.id === pathId);
  if (editor.mode === "draw") { editor.drawing.push(point3(point)); editor.version++; return; }
  if (editor.mode === "break" && path) {
    const at = nearestPathSegment(path, point), split = splitPath(path, at.index, at.fraction);
    remember(editor, editor.paths.flatMap(p => p.id === path.id ? split : [p]), split.map(p => p.id));
  } else if (editor.mode === "node") {
    if (path && nodeIndex !== null) { selectMachiningPath(editor, path.id); editor.nodeIndex = nodeIndex; editor.movingNode = true; syncNodeFields(editor); }
    else if (editor.movingNode && editor.selectedIds.length === 1) {
      remember(editor, editor.paths.map(p => p.id === editor.selectedIds[0] ? editPathPoint(p, editor.nodeIndex, point) : p));
      editor.movingNode = false; syncNodeFields(editor);
    } else if (path) { selectMachiningPath(editor, path.id); syncNodeFields(editor); }
  } else selectMachiningPath(editor, pathId, toggle);
}

export async function handleMachiningPathAction(context, view, job, action, target, ops, handlers) {
  if (!action.startsWith(PREFIX)) return false;
  const editor = pathEditor(view, job);
  if (!editor || view.pending) return true;
  try {
    const command = action.slice(PREFIX.length);
    if (command === "select" || command === "check") selectMachiningPath(editor, String(target?.dataset?.pathId ?? ""), command === "check");
    else if (command === "field") {
      const key = target?.dataset?.pathField;
      if (key in editor.fields) editor.fields[key] = String(target.value);
    } else if (command === "mode") { editor.mode = target.dataset.pathMode; editor.movingNode = false; }
    else if (command === "space") editor.space = editor.space === "2d" ? "3d" : "2d";
    else if (command === "copy") { const r = copyPaths(editor.paths, editor.selectedIds); remember(editor, r.paths, r.selectedIds); }
    else if (command === "merge") { const r = mergePaths(editor.paths, editor.selectedIds); remember(editor, r.paths, r.selectedIds); }
    else if (command === "delete") { if (editor.selectedIds.length) remember(editor, editor.paths.filter(p => !editor.selectedIds.includes(p.id)), []); }
    else if (command === "transform") {
      if (!editor.selectedIds.length) throw new Error("请先选择刀路。");
      const f = editor.fields;
      remember(editor, transformPaths(editor.paths, editor.selectedIds, { translation: [f.dx, f.dy, f.dz], pivot: [f.px, f.py, f.pz], angle: f.angle, axis: f.axis === "X" ? [1, 0, 0] : f.axis === "Y" ? [0, 1, 0] : [0, 0, 1] }));
      syncNodeFields(editor);
    } else if (command === "pivot") {
      const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
      for (const path of editor.paths) if (editor.selectedIds.includes(path.id)) for (const p of path.points)
        for (let i = 0; i < 3; i++) { min[i] = Math.min(min[i], p[i]); max[i] = Math.max(max[i], p[i]); }
      if (Number.isFinite(min[0])) [editor.fields.px, editor.fields.py, editor.fields.pz] = min.map((n, i) => String((n + max[i]) / 2));
    } else if (command === "node-index") {
      const path = editor.paths.find(p => p.id === editor.selectedIds[0]);
      editor.nodeIndex = Math.max(0, Math.min((path?.points.length ?? 1) - 1, Math.trunc(Number(target.value) || 1) - 1)); syncNodeFields(editor);
    } else if (command === "node-save") {
      const point = [editor.fields.nx, editor.fields.ny, editor.fields.nz];
      remember(editor, editor.paths.map(p => p.id === editor.selectedIds[0] ? editPathPoint(p, editor.nodeIndex, point) : p));
    } else if (command === "finish" || command === "close") {
      const path = createPath(editor.drawing, command === "close", `新刀路 ${editor.paths.length + 1}`);
      remember(editor, [...editor.paths, path], [path.id]); editor.drawing = []; editor.mode = "select";
    } else if (command === "cancel-draw") { editor.drawing = []; editor.mode = "select"; editor.version++; }
    else if (command === "undo" || command === "redo") {
      const from = command === "undo" ? editor.history : editor.future, to = command === "undo" ? editor.future : editor.history;
      const previous = from.pop();
      if (previous) { to.push({ paths: editor.paths, selectedIds: editor.selectedIds }); editor.paths = previous.paths; editor.selectedIds = previous.selectedIds; editor.version++; editor.dirty = true; }
    } else if (command === "save") {
      if (editor.drawing.length) throw new Error("请先完成或取消当前绘制。");
      await handlers.save(editor);
    } else if (command === "discard") {
      if (await handlers.confirm("放弃当前未保存的刀路编辑？", "放弃")) delete view.tubeDesignerMachining.edits[job.id];
    }
  } catch (error) { view.error = error?.message ?? String(error); }
  ops.renderProject(context, view);
  return true;
}

export function attachMachiningSideEditor(context, view, job, ops) {
  const canvas = context.mount?.querySelector?.("[data-machining-side-canvas]");
  const editor = pathEditor(view, job);
  if (!canvas || !editor || canvas.dataset.attached) return;
  canvas.dataset.attached = "true";
  canvas.addEventListener("click", event => {
    if (view.pending) return;
    const rect = canvas.getBoundingClientRect();
    const scale = Math.min(rect.width / 1000, rect.height / 600);
    if (!(scale > 0)) return;
    const x = (event.clientX - rect.left - (rect.width - 1000 * scale) / 2) / scale;
    const y = (event.clientY - rect.top - (rect.height - 600 * scale) / 2) / scale;
    const id = event.target?.dataset?.pathId ?? "", node = event.target?.dataset?.nodeIndex;
    const selected = editor.paths.find(p => p.id === (id || editor.selectedIds[0]));
    const normalIndex = editor.fields.plane === "XY" ? 2 : editor.fields.plane === "YZ" ? 0 : 1;
    const depth = editor.mode === "node" && selected ? selected.points[editor.nodeIndex]?.[normalIndex] : Number(editor.fields.depth);
    try {
      let point = sideLayout(editor).unproject(x, y, depth);
      if (editor.mode === "break" && selected) {
        // Pick in the projected plane, then interpolate the original XYZ segment.
        const projected = { ...selected, points: selected.points.map(p => unprojectSidePoint(projectSidePoint(p, editor.fields.plane), editor.fields.plane, 0)) };
        const at = nearestPathSegment(projected, sideLayout(editor).unproject(x, y, 0));
        const a = selected.points[at.index], b = selected.points[(at.index + 1) % selected.points.length];
        point = a.map((v, i) => v + (b[i] - v) * at.fraction);
      }
      editAtPoint(editor, point, id, node === undefined ? null : Number(node), event.ctrlKey || event.metaKey);
      view.error = "";
    } catch (error) { view.error = error.message; }
    ops.renderProject(context, view);
  });
}
