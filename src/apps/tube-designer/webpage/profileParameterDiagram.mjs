import { parameterVisible } from './parameterConditions.mjs';
import { parameterDiagramLevelAttribute } from './parameterPresentation.mjs';
import { matchesParameterCondition, parameterEnabled } from "./parameterConditions.mjs";
import { escapeAttr as attr, escapeText as text } from "../../_shared/workbench/utils/format.mjs";
import { profileSvgGeometry } from "./profileSvg.mjs";
import { bindParameterDiagramScopes } from "./parameterDiagramBinding.mjs";

const localized = (value, fallback = "") => value && typeof value === "object"
  ? String(value["zh-CN"] ?? value["en-US"] ?? Object.values(value)[0] ?? fallback) : String(value ?? fallback);
const number = (value) => Number(value.toFixed(5));
const coordinate = (value) => Array.isArray(value) && value.length === 2
  && value.every((item) => typeof item === "number" && Number.isFinite(item) && Math.abs(item) <= 1e9);
const displayNumber = (value) => new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3, useGrouping: false }).format(value);

function visible(condition, values) {
  return matchesParameterCondition(condition, values);
}

function valueText(definition, values) {
  const value = values[definition.key] ?? definition.defaultValue;
  const option = (Array.isArray(definition.options) ? definition.options : []).find((item) => (item && typeof item === "object" ? item.value : item) === value);
  if (option != null) return localized(option?.displayName ?? option?.label, typeof option === "object" ? option.value : option);
  if (typeof value === "boolean") return value ? "开启" : "关闭";
  if (typeof value === "number" && Number.isFinite(value)) return displayNumber(value);
  return localized(value, "—");
}

function validAnnotations(snapshot, definitions, values) {
  const diagram = snapshot?.parameterDiagram;
  if (diagram?.schemaVersion !== 1 || !Array.isArray(diagram.annotations)) return [];
  const byKey = new Map(definitions.map((definition) => [definition.key, definition]));
  return diagram.annotations.slice(0, 128).filter((item) => {
    if (!item || !byKey.has(item.parameter) || !visible(item.visibleWhen, values)
        || !visible(byKey.get(item.parameter).visibleWhen, values)) return false;
    if (!["top", "bottom", "left", "right"].includes(item.side)) return false;
    if (item.kind === "leader") return coordinate(item.point);
    return item.kind === "linear" && coordinate(item.from) && coordinate(item.to)
      && ((item.axis === "x" && ["top", "bottom"].includes(item.side))
        || (item.axis === "y" && ["left", "right"].includes(item.side)));
  }).map((item) => ({ ...item, definition: byKey.get(item.parameter) }));
}

function arrow(point, direction) {
  const dx = Math.cos(direction), dy = Math.sin(direction);
  return `<path class="td-profile-dimension-arrow" d="M ${number(point[0])} ${number(point[1])} l ${number(-dx * 6 + dy * 2.5)} ${number(-dy * 6 - dx * 2.5)} l ${number(-dy * 5)} ${number(dx * 5)} Z"/>`;
}

function line(from, to, className = "") {
  return `<line class="${className}" x1="${number(from[0])}" y1="${number(from[1])}" x2="${number(to[0])}" y2="${number(to[1])}"/>`;
}

function diagramSvg(snapshot, annotations, definitions, values) {
  const geometry = profileSvgGeometry(snapshot);
  if (!geometry.bounds || !geometry.markup) return "";
  // Labels use separate outside lanes; geometry and annotation anchors share one transform.
  const sides = Object.fromEntries(["left", "right", "top", "bottom"].map((side) => [side, annotations.filter((item) => item.side === side)]));
  const linearCount = (side) => sides[side].filter((item) => item.kind === "linear").length;
  const left = 78 + 24 * linearCount("left"), right = 78 + 24 * linearCount("right");
  const top = 32 + 25 * linearCount("top"), bottom = 32 + 25 * linearCount("bottom");
  const width = left + 228 + right, height = top + 180 + bottom;
  const bounds = { ...geometry.bounds };
  for (const item of annotations) for (const point of item.kind === "linear" ? [item.from, item.to] : [item.point]) {
    bounds.minX = Math.min(bounds.minX, point[0]); bounds.maxX = Math.max(bounds.maxX, point[0]);
    bounds.minY = Math.min(bounds.minY, point[1]); bounds.maxY = Math.max(bounds.maxY, point[1]);
  }
  const scale = Math.min(228 / Math.max(bounds.maxX - bounds.minX, 1e-6), 180 / Math.max(bounds.maxY - bounds.minY, 1e-6));
  const cx = left + 114, cy = top + 90;
  const tx = cx - (bounds.minX + bounds.maxX) * scale / 2, ty = cy + (bounds.minY + bounds.maxY) * scale / 2;
  const map = (point) => [tx + point[0] * scale, ty - point[1] * scale];
  const geometryTop = map([0, geometry.bounds.maxY])[1], geometryBottom = map([0, geometry.bounds.minY])[1];
  const geometryLeft = map([geometry.bounds.minX, 0])[0], geometryRight = map([geometry.bounds.maxX, 0])[0];
  const lanes = { top: 0, bottom: 0, left: 0, right: 0 };
  const order = new Map(definitions.map((definition, index) => [definition.key, index + 1]));
  const rendered = annotations.map((item) => {
    const index = order.get(item.parameter);
    const label = localized(item.definition.displayName ?? item.definition.name, item.parameter);
    const value = valueText(item.definition, values);
    const title = `${label}：${value}${item.description ? `。${localized(item.description)}` : ""}`;
    let body;
    if (item.kind === "linear") {
      const from = map(item.from), to = map(item.to), lane = lanes[item.side]++;
      const horizontal = item.axis === "x";
      const position = item.side === "top" ? top - 16 - lane * 25 : item.side === "bottom" ? top + 180 + 16 + lane * 25
        : item.side === "left" ? left - 16 - lane * 24 : left + 228 + 16 + lane * 24;
      const start = horizontal ? [from[0], position] : [position, from[1]];
      const end = horizontal ? [to[0], position] : [position, to[1]];
      const angle = Math.atan2(end[1] - start[1], end[0] - start[0]);
      const middle = [(start[0] + end[0]) / 2, (start[1] + end[1]) / 2];
      const shortValue = typeof values[item.parameter] === "number" ? displayNumber(values[item.parameter]) : value;
      const shortName = label.replace(/\s*\/\s*mm.*$/, "").replace(/（.*$/, "");
      const labelText = `${shortName} ${shortValue}`;
      const labelWidth = Math.max(42, [...labelText].reduce((n,c) => n + (c.charCodeAt(0)>255 ? 11 : 6.7), 10));
      body = line(from, start, "td-profile-extension") + line(to, end, "td-profile-extension") + line(start, end)
        + arrow(start, angle + Math.PI) + arrow(end, angle)
        + `<g transform="translate(${number(middle[0])} ${number(middle[1])})${horizontal ? "" : " rotate(-90)"}"><rect class="td-profile-dimension-label" x="${-labelWidth / 2}" y="-9" width="${labelWidth}" height="18" rx="4"/><text text-anchor="middle" dominant-baseline="central">${text(labelText)}</text></g>`;
    } else {
      const point = map(item.point);
      const siblings = sides[item.side].filter((other) => other.kind === "leader");
      const fraction = (siblings.indexOf(item) + 1) / (siblings.length + 1);
      const anchor = item.side === "left" ? [16, top + 180 * fraction] : item.side === "right" ? [width - 16, top + 180 * fraction]
        : item.side === "top" ? [left + 228 * fraction, 14] : [left + 228 * fraction, height - 14];
      const elbow = item.side === "left" ? [Math.min(geometryLeft - 10, anchor[0] + 22), anchor[1]]
        : item.side === "right" ? [Math.max(geometryRight + 10, anchor[0] - 22), anchor[1]]
          : item.side === "top" ? [anchor[0], geometryTop - 10] : [anchor[0], geometryBottom + 10];
      body = `<polyline points="${[point, elbow, anchor].map((p) => p.map(number).join(",")).join(" ")}"/>`
        + `<circle class="td-profile-anchor" cx="${number(point[0])}" cy="${number(point[1])}" r="3"/>`
        + `<circle class="td-profile-parameter-badge" cx="${number(anchor[0])}" cy="${number(anchor[1])}" r="11"/><text x="${number(anchor[0])}" y="${number(anchor[1])}" text-anchor="middle" dominant-baseline="central">${index}</text>`
        + `<text x="${number(anchor[0])}" y="${number(anchor[1]+24)}" text-anchor="${item.side === 'left' ? 'start' : item.side === 'right' ? 'end' : 'middle'}">${text(label.replace(/\s*\/\s*mm.*$/, ""))} ${text(value)}</text>`;
    }
    return `<g class="td-profile-annotation" data-profile-annotation-key="${attr(item.parameter)}"${parameterDiagramLevelAttribute(item.definition)} role="button" tabindex="0" aria-label="${attr(title)}"><title>${text(title)}</title>${body}</g>`;
  }).join("");
  return `<svg class="td-profile-parameter-svg" viewBox="0 0 ${width} ${height}" role="group" aria-label="${attr(localized(snapshot.name, "程式管型"))}参数尺寸示意图">
    <g class="td-profile-diagram-shape" transform="translate(${number(tx)} ${number(ty)}) scale(${scale} ${-scale})">${geometry.markup}</g>${rendered}</svg>`;
}

/** Render only explicit metadata. Parameter names never imply an anchor on arbitrary geometry. */
export function renderProfileParameterDiagram(snapshot, options = {}) {
  // The editor draft is newer than the last asynchronous geometry snapshot.
  // Prefer it for applicability so a newly enabled advanced mode does not keep
  // showing the previous mode's annotations while regeneration is in flight.
  const values = options.parameters ?? snapshot?.parameters ?? {};
  const declared = options.definitions ?? snapshot?.parameterDefinitions ?? [];
  const definitions = (Array.isArray(declared) ? declared : []).filter((definition) => definition?.key
    && parameterVisible(definition, values)
    && definition?.presentation?.diagramVisible !== false);
  if (!definitions.length) return "";
  const annotations = validAnnotations(snapshot, definitions, values);
  const svg = diagramSvg(snapshot, annotations, definitions, values);
  return `<section class="td-profile-parameter-diagram${options.compact ? " is-compact" : ""}" data-profile-parameter-diagram>
    <header><strong>${text(options.title ?? "参数示意图")}</strong></header>
    ${svg ? `<div class="td-profile-parameter-canvas">${svg}</div>` : ""}
  </section>`;
}

/** Bind locally: multiple main/branch/component sections must never highlight each other. */
export function bindProfileParameterDiagrams(mount) {
  bindParameterDiagramScopes(mount, "profile");
}
