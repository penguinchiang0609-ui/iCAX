import { escapeAttr, escapeText } from "../utils/format.mjs";

export function renderToolpathOverlay(scene, item, index) {
  const color = index % 2 === 0 ? "#f8d66d" : "#7ee6a8";
  const topology = findTopologyObject(scene, item.topologyKind, item.topologyId);
  if (!topology) {
    return "";
  }
  if (item.topologyKind === "loop" && Number(topology.radius)) {
    return `<circle class="cam-toolpath" cx="${escapeAttr(topology.center?.x ?? 0)}" cy="${escapeAttr(topology.center?.y ?? 0)}" r="${escapeAttr(Number(topology.radius) + 10)}" stroke="${color}" />`;
  }
  if (item.topologyKind === "edge") {
    const points = (topology.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
    return `<polyline class="cam-toolpath" points="${escapeAttr(points)}" stroke="${color}" />`;
  }
  if (item.topologyKind === "face") {
    const points = (topology.points ?? []).map((point) => `${point.x},${point.y}`).join(" ");
    return `<polygon class="cam-toolpath-fill" points="${escapeAttr(points)}" stroke="${color}" />`;
  }
  return "";
}

export function findTopologyObject(scene, kind, id) {
  const list = kind === "face"
    ? scene.faces
    : kind === "loop"
      ? scene.loops
      : kind === "edge"
        ? scene.edges
        : [];
  return (list ?? []).find((item) => String(item.id) === String(id)) ?? null;
}

export function renderToolpathList(toolpaths) {
  if (!toolpaths.length) {
    return `<div class="cam-empty-row">暂无刀路。</div>`;
  }

  return `
    <div class="cam-list">
      ${toolpaths.map((item) => `
        <div class="cam-list-row">
          <strong>#${escapeText(item.id)} ${escapeText(item.label)}</strong>
          <small>${escapeText(item.operation)} · ${escapeText(item.source)}</small>
        </div>
      `).join("")}
    </div>
  `;
}
