import { escapeAttr, escapeText } from "../utils/format.mjs";
export function renderFace(face, selectedKey, index) {
  const points = (face.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
  const key = `face:${face.id}`;
  const selected = key === selectedKey ? " selected" : "";
  const fill = index === 0 ? "url(#cam-face)" : "url(#cam-side)";
  return `
    <polygon class="cam-face${selected}"
             points="${escapeAttr(points)}"
             fill="${fill}"
             data-cam-pick
             data-cam-kind="face"
             data-cam-id="${escapeAttr(face.id)}"
             data-cam-label="${escapeAttr(face.label)}" />
  `;
}

export function renderLoop(loop, selectedKey) {
  if (!Number(loop.radius)) {
    return "";
  }
  const key = `loop:${loop.id}`;
  const selected = key === selectedKey ? " selected" : "";
  return `
    <circle class="cam-loop${selected}"
            cx="${escapeAttr(loop.center?.x ?? 0)}"
            cy="${escapeAttr(loop.center?.y ?? 0)}"
            r="${escapeAttr(loop.radius)}"
            data-cam-pick
            data-cam-kind="loop"
            data-cam-id="${escapeAttr(loop.id)}"
            data-cam-label="${escapeAttr(loop.label)}" />
    <text class="cam-loop-label" x="${escapeAttr((loop.center?.x ?? 0) - 22)}" y="${escapeAttr((loop.center?.y ?? 0) + 4)}">${escapeText(loop.label)}</text>
  `;
}

export function renderEdge(edge, selectedKey) {
  const points = (edge.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
  const key = `edge:${edge.id}`;
  const selected = key === selectedKey ? " selected" : "";
  return `
    <polyline class="cam-edge${selected}"
              points="${escapeAttr(points)}"
              data-cam-pick
              data-cam-kind="edge"
              data-cam-id="${escapeAttr(edge.id)}"
              data-cam-label="${escapeAttr(edge.label)}" />
  `;
}

export function renderWorkpieceList(workpieces, activeWorkpieceId) {
  if (!workpieces.length) {
    return `<div class="cam-empty-row">暂无工件。</div>`;
  }

  return `
    <div class="cam-list">
      ${workpieces.map((item, index) => {
        const isActive = String(item.entityId ?? "") === String(activeWorkpieceId ?? "");
        return `
          <button class="cam-list-row ${isActive ? "active" : ""}"
                  type="button"
                  data-cam-workpiece-id="${escapeAttr(item.entityId)}">
            <strong>${escapeText(index + 1)}. ${escapeText(item.name || item.sourcePath || item.entityId)}</strong>
            <small>v${escapeText(item.geometryRevision ?? 0)} · ${item.hasDraft ? "CAD 草稿" : "当前版本"} · ${escapeText(item.sourcePath || item.modelResourceId || "")}</small>
          </button>
        `;
      }).join("")}
    </div>
  `;
}

export function renderIntentToolpathTree(nodes = [], depth = 0) {
  if (!nodes.length && depth === 0) {
    return `<div class="cam-empty-row">暂无意图刀路。请在场景中选择CAD轮廓后创建。</div>`;
  }
  return nodes.map((node) => {
    const children = Array.isArray(node.children) ? node.children : [];
    const state = node.superseded ? "已替代" : node.sourceState === "Current" ? "有效" : node.sourceState;
    return `<div class="cam-intent-node" style="--cam-tree-depth:${depth}">
      <button class="cam-list-row ${node.superseded ? "superseded" : ""}" type="button" data-cam-intent-id="${escapeAttr(node.entityId)}">
        <strong>${escapeText(node.name || "未命名意图")}</strong>
        <small>${escapeText(node.kind || "Cut")} · ${escapeText(state || "-")}</small>
      </button>
      ${children.length ? `<div class="cam-intent-children">${renderIntentToolpathTree(children, depth + 1)}</div>` : ""}
    </div>`;
  }).join("");
}
