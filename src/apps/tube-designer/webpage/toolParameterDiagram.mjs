import { matchesParameterCondition, parameterVisible } from "./parameterConditions.mjs";
import { renderParameterLevels, parameterDiagramLevelAttribute } from './parameterPresentation.mjs';
import { escapeAttr as attr, escapeText as text } from "../../_shared/workbench/utils/format.mjs";
import { bindParameterDiagramScopes } from "./parameterDiagramBinding.mjs";

const localized = (value, fallback = "") => value && typeof value === "object"
  ? String(value["zh-CN"] ?? value["en-US"] ?? Object.values(value)[0] ?? fallback) : String(value ?? fallback);
const finite = (value) => typeof value === "number" && Number.isFinite(value);
const point = (value) => Array.isArray(value) && value.length === 2 && value.every(finite);
const rounded = (value) => Number(Number(value).toFixed(5));
const displayNumber = (value) => new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3, useGrouping: false }).format(value);

function visible(condition, values) {
  return matchesParameterCondition(condition, values);
}

function definitionName(definition) {
  return localized(definition?.displayName ?? definition?.name, definition?.key ?? "参数");
}

function shortName(definition) {
  return definitionName(definition)
    .replace(/[（(][^）)]*(?:mm|°)[^）)]*[）)]/gi, "")
    .replace(/（.*$/, "")
    .trim();
}

function definitionUnit(definition) {
  const explicit = localized(definition?.unit);
  if (explicit) return explicit;
  const name = definitionName(definition);
  if (/°|角度|开口角/.test(name)) return "°";
  if (/\bmm\b/i.test(name) || /尺寸|直径|半径|宽度|高度|长度|间距|留量|阶差|偏移|壁厚|切深|上移|间隙/.test(name)) return "mm";
  return "";
}

export function toolParameterValueText(definition, values) {
  const value = values?.[definition?.key] ?? definition?.defaultValue;
  const option = (Array.isArray(definition?.options) ? definition.options : []).find((item) => String(typeof item === "object" ? item.value : item) === String(value));
  if (option != null) return localized(option?.displayName ?? option?.label, typeof option === "object" ? option.value : option);
  if (typeof value === "boolean") return value ? "开启" : "关闭";
  if (finite(value)) return displayNumber(value);
  return localized(value, "—");
}

function resolvedDiagram(tool, values) {
  const base = tool?.parameterDiagram;
  if (!base || typeof base !== "object") return null;
  const variant = (Array.isArray(base.variants) ? base.variants : []).find((item) => visible(item?.visibleWhen, values));
  return variant ? { ...base, ...variant, variants: base.variants } : base;
}

function viewBoxOf(diagram) {
  const parts = String(diagram?.viewBox ?? "0 0 240 160").trim().split(/\s+/).map(Number);
  if (parts.length === 4 && parts.every(finite) && parts[2] > 0 && parts[3] > 0) return parts;
  return [0, 0, 240, 160];
}

function shapeMarkup(diagram) {
  const pathMarkup = (diagram?.paths ?? []).map((entry) => {
    const pathData = typeof entry === "string" ? entry : entry?.d;
    if (!pathData) return "";
    const className = typeof entry === "object" && entry?.className ? entry.className : "tool-diagram-profile";
    return `<path class="${attr(className)}" d="${attr(pathData)}"/>`;
  }).join("");
  const lineMarkup = (diagram?.lines ?? []).map((entry) => `<line class="${attr(entry?.className ?? "tool-diagram-detail")}" x1="${attr(entry?.x1)}" y1="${attr(entry?.y1)}" x2="${attr(entry?.x2)}" y2="${attr(entry?.y2)}"/>`).join("");
  const circleMarkup = (diagram?.circles ?? []).map((entry) => `<circle class="${attr(entry?.className ?? "tool-diagram-detail")}" cx="${attr(entry?.cx)}" cy="${attr(entry?.cy)}" r="${attr(entry?.r)}"/>`).join("");
  const polygonMarkup = (diagram?.polygons ?? []).map((entry) => `<polygon class="${attr(entry?.className ?? "tool-diagram-profile")}" points="${attr(typeof entry === "string" ? entry : entry?.points)}"/>`).join("");
  return pathMarkup + lineMarkup + circleMarkup + polygonMarkup;
}

function arrow(at, direction) {
  const dx = Math.cos(direction), dy = Math.sin(direction);
  return `<path class="tool-diagram-arrow" d="M ${rounded(at[0])} ${rounded(at[1])} l ${rounded(-dx * 6 + dy * 2.5)} ${rounded(-dy * 6 - dx * 2.5)} l ${rounded(-dy * 5)} ${rounded(dx * 5)} Z"/>`;
}

function svgLine(from, to, className = "") {
  return `<line class="${className}" x1="${rounded(from[0])}" y1="${rounded(from[1])}" x2="${rounded(to[0])}" y2="${rounded(to[1])}"/>`;
}

function validAnnotations(diagram, definitions, values) {
  if (diagram?.schemaVersion !== 2 || !Array.isArray(diagram.annotations)) return [];
  const byKey = new Map(definitions.map((definition) => [definition.key, definition]));
  return diagram.annotations.slice(0, 128).filter((annotation) => {
    const definition = byKey.get(annotation?.parameter);
    if (!definition || !visible(annotation?.visibleWhen, values) || !parameterVisible(definition, values)) return false;
    if (!["top", "bottom", "left", "right"].includes(annotation.side)) return false;
    if (annotation.kind === "leader") return point(annotation.point);
    return annotation.kind === "linear" && point(annotation.from) && point(annotation.to)
      && ((annotation.axis === "x" && ["top", "bottom"].includes(annotation.side))
        || (annotation.axis === "y" && ["left", "right"].includes(annotation.side)));
  }).map((annotation) => ({ ...annotation, definition: byKey.get(annotation.parameter) }));
}

function richDiagramSvg(tool, diagram, values, definitions) {
  const markup = shapeMarkup(diagram);
  if (!markup) return "";
  const annotations = validAnnotations(diagram, definitions, values);
  const [sourceX, sourceY, sourceWidth, sourceHeight] = viewBoxOf(diagram);
  const sideItems = Object.fromEntries(["left", "right", "top", "bottom"].map((side) => [side, annotations.filter((item) => item.side === side)]));
  const linearCount = (side) => sideItems[side].filter((item) => item.kind === "linear").length;
  const left = 42 + linearCount("left") * 25, right = 42 + linearCount("right") * 25;
  const top = 30 + linearCount("top") * 25, bottom = 30 + linearCount("bottom") * 25;
  const drawingWidth = 272, drawingHeight = 176;
  const width = left + drawingWidth + right, height = top + drawingHeight + bottom;
  const scale = Math.min(drawingWidth / sourceWidth, drawingHeight / sourceHeight);
  const shapeWidth = sourceWidth * scale, shapeHeight = sourceHeight * scale;
  const tx = left + (drawingWidth - shapeWidth) / 2 - sourceX * scale;
  const ty = top + (drawingHeight - shapeHeight) / 2 - sourceY * scale;
  const mirrored = !!diagram.mirrorParameter && values?.[diagram.mirrorParameter] === false;
  const map = ([x, y]) => [tx + (mirrored ? sourceX * 2 + sourceWidth - x : x) * scale, ty + y * scale];
  const drawingLeft = left, drawingRight = left + drawingWidth, drawingTop = top, drawingBottom = top + drawingHeight;
  const lanes = { top: 0, bottom: 0, left: 0, right: 0 };
  const order = new Map(definitions.map((definition, index) => [definition.key, index + 1]));
  const rendered = annotations.map((item) => {
    const index = order.get(item.parameter);
    const label = shortName(item.definition);
    const value = toolParameterValueText(item.definition, values);
    const unit = definitionUnit(item.definition);
    const title = `${definitionName(item.definition)}：${value}${unit ? ` ${unit}` : ""}${item.description ? `。${localized(item.description)}` : ""}`;
    let body = "";
    if (item.kind === "linear") {
      const from = map(item.from), to = map(item.to), lane = lanes[item.side]++;
      const horizontal = item.axis === "x";
      const position = item.side === "top" ? drawingTop - 14 - lane * 25 : item.side === "bottom" ? drawingBottom + 14 + lane * 25
        : item.side === "left" ? drawingLeft - 14 - lane * 25 : drawingRight + 14 + lane * 25;
      const start = horizontal ? [from[0], position] : [position, from[1]];
      const end = horizontal ? [to[0], position] : [position, to[1]];
      const angle = Math.atan2(end[1] - start[1], end[0] - start[0]);
      const middle = [(start[0] + end[0]) / 2, (start[1] + end[1]) / 2];
      const labelText = `${label} ${value}${unit ? ` ${unit}` : ""}`;
      const labelWidth = Math.max(48, [...labelText].reduce((sum, character) => sum + (character.charCodeAt(0) > 255 ? 11 : 6.7), 12));
      body = svgLine(from, start, "tool-diagram-extension") + svgLine(to, end, "tool-diagram-extension") + svgLine(start, end)
        + arrow(start, angle + Math.PI) + arrow(end, angle)
        + `<g transform="translate(${rounded(middle[0])} ${rounded(middle[1])})${horizontal ? "" : " rotate(-90)"}"><rect class="tool-diagram-dimension-label" x="${rounded(-labelWidth / 2)}" y="-9" width="${rounded(labelWidth)}" height="18" rx="4"/><text text-anchor="middle" dominant-baseline="central">${text(labelText)}</text></g>`;
    } else {
      const location = map(item.point);
      const peers = sideItems[item.side].filter((other) => other.kind === "leader");
      const fraction = (peers.indexOf(item) + 1) / (peers.length + 1);
      const badge = item.side === "left" ? [18, drawingTop + drawingHeight * fraction]
        : item.side === "right" ? [width - 18, drawingTop + drawingHeight * fraction]
          : item.side === "top" ? [drawingLeft + drawingWidth * fraction, 15]
            : [drawingLeft + drawingWidth * fraction, height - 15];
      const elbow = item.side === "left" ? [drawingLeft - 10, badge[1]] : item.side === "right" ? [drawingRight + 10, badge[1]]
        : item.side === "top" ? [badge[0], drawingTop - 9] : [badge[0], drawingBottom + 9];
      body = `<polyline points="${[location, elbow, badge].map((valuePoint) => valuePoint.map(rounded).join(",")).join(" ")}"/>`
        + `<circle class="tool-diagram-anchor" cx="${rounded(location[0])}" cy="${rounded(location[1])}" r="3"/>`
        + `<circle class="tool-diagram-parameter-badge" cx="${rounded(badge[0])}" cy="${rounded(badge[1])}" r="11"/><text x="${rounded(badge[0])}" y="${rounded(badge[1])}" text-anchor="middle" dominant-baseline="central">${index}</text>`;
    }
    return `<g class="tool-diagram-annotation" data-tool-annotation-key="${attr(item.parameter)}"${parameterDiagramLevelAttribute(item.definition)} role="button" tabindex="0" aria-label="${attr(title)}"><title>${text(title)}</title>${body}</g>`;
  }).join("");
  const mirrorTransform = mirrored ? ` translate(${rounded(sourceX * 2 + sourceWidth)} 0) scale(-1 1)` : "";
  return `<svg class="tool-parameter-svg is-rich" viewBox="0 0 ${width} ${height}" role="group" aria-label="${attr(`${localized(tool?.displayName ?? tool?.name, "模具")}参数示意图`)}"><g class="tool-diagram-shape" transform="translate(${rounded(tx)} ${rounded(ty)}) scale(${rounded(scale)})${mirrorTransform}">${markup}</g>${rendered}</svg>`;
}

function legacyDiagramSvg(tool, diagram, values, definitions) {
  const [sourceX, , sourceWidth] = viewBoxOf(diagram);
  const mirrored = !!diagram.mirrorParameter && values?.[diagram.mirrorParameter] === false;
  const transform = mirrored ? ` transform="translate(${attr(sourceX * 2 + sourceWidth)} 0) scale(-1 1)"` : "";
  const definitionMap = new Map(definitions.map((definition, index) => [definition.key, { definition, index: index + 1 }]));
  const labels = (diagram.labels ?? []).map((label) => {
    const found = label?.parameter ? definitionMap.get(label.parameter) : null;
    if (label?.parameter && !found) return "";
    const value = found ? toolParameterValueText(found.definition, values) : "";
    const unit = found ? definitionUnit(found.definition) : "";
    const content = found ? `${label.text || shortName(found.definition)} ${value}${label.unit ?? (unit ? ` ${unit}` : "")}` : String(label?.text ?? "");
    if (!content) return "";
    const body = `<text class="tool-diagram-label" x="${attr(label.x)}" y="${attr(label.y)}" text-anchor="${attr(label.anchor ?? "middle")}">${text(content)}</text>`;
    return found ? `<g class="tool-diagram-annotation" data-tool-annotation-key="${attr(label.parameter)}"${parameterDiagramLevelAttribute(found.definition)} role="button" tabindex="0" aria-label="${attr(content)}"><circle class="tool-diagram-parameter-badge" cx="${attr(Number(label.x) - 12)}" cy="${attr(Number(label.y) - 3)}" r="8"/><text class="tool-diagram-badge-text" x="${attr(Number(label.x) - 12)}" y="${attr(Number(label.y) - 3)}" text-anchor="middle" dominant-baseline="central">${found.index}</text>${body}</g>` : body;
  }).join("");
  return `<svg class="tool-parameter-svg" viewBox="${attr(diagram.viewBox ?? "0 0 240 160")}" role="group" aria-label="${attr(`${localized(tool?.displayName ?? tool?.name, "模具")}参数示意图`)}"><g${transform}>${shapeMarkup(diagram)}</g>${labels}</svg>`;
}

export function renderToolParameterDiagramSvg(tool, values, definitions, fallbackSvg = "") {
  definitions = definitions.filter(d => parameterVisible(d, values));
  const diagram = resolvedDiagram(tool, values);
  if (!diagram) return fallbackSvg;
  return diagram.schemaVersion === 2
    ? richDiagramSvg(tool, diagram, values, definitions)
    : legacyDiagramSvg(tool, diagram, values, definitions);
}

export function renderToolParameterDiagram({ tool, values, definitions, expanded, fallbackSvg = "", toggleAction = "tube-designer-tool-library-toggle-diagram", showToggle = true }) {
  definitions = definitions.filter(d => parameterVisible(d, values));
  const rows = renderParameterLevels(definitions, (definition) => {
    const index = definitions.indexOf(definition);
    const name = definitionName(definition);
    const description = localized(definition?.description ?? definition?.help, "点击定位并编辑此参数");
    const unit = definitionUnit(definition);
    return `<button type="button" class="tube-tool-library-diagram-row" data-tool-annotation-key="${attr(definition.key)}" aria-label="定位参数：${attr(name)}"><b>${index + 1}</b><span><strong>${text(name)}</strong><small>${text(description)}</small></span><em>${text(toolParameterValueText(definition, values))}${unit ? ` ${text(unit)}` : ""}</em></button>`;
  }, {key:'tool-legend',gridClass:'tube-tool-library-diagram-legend'});
  const diagram = resolvedDiagram(tool, values);
  const annotations = validAnnotations(diagram, definitions, values);
  const hasDiagram = !!diagram || !!fallbackSvg;
  return `<section class="tube-tool-library-parameter-diagram" data-tool-parameter-diagram><header><div><strong>参数示意图</strong><span>尺寸随参数实时更新 · 点击编号可定位参数</span></div>${showToggle ? `<button type="button" class="tube-tool-library-diagram-toggle" data-cam-action="${attr(toggleAction)}" data-tube-tool-library-diagram="tool" aria-expanded="${expanded}">${expanded ? "隐藏示意图" : "显示示意图"}</button>` : ""}</header>${expanded ? `<div class="tube-tool-library-diagram-content">${hasDiagram ? `<div class="tube-tool-library-diagram-art">${renderToolParameterDiagramSvg(tool, values, definitions, fallbackSvg)}</div>` : ""}${diagram?.schemaVersion === 2 && definitions.length && !annotations.length ? `<p class="tube-tool-library-diagram-message">此模具尚未声明参数位置标注。</p>` : ""}${rows ? `<div class="tube-tool-library-diagram-legend">${rows}</div>` : `<p class="tube-tool-library-diagram-message">此模具没有可编辑的截面参数。</p>`}</div>` : ""}</section>`;
}

/** Keep each mould independent from the main/branch profile diagrams around it. */
export function bindToolParameterDiagrams(mount) {
  bindParameterDiagramScopes(mount, "tool");
}
