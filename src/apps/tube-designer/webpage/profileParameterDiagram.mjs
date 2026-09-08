import { escapeAttr as attr, escapeText as text } from "../../_shared/workbench/utils/format.mjs";
import { profileSvgGeometry } from "./profileSvg.mjs";

const localized = (value, fallback = "") => value && typeof value === "object"
  ? String(value["zh-CN"] ?? value["en-US"] ?? Object.values(value)[0] ?? fallback) : String(value ?? fallback);
const number = (value) => Number(value.toFixed(5));
const coordinate = (value) => Array.isArray(value) && value.length === 2
  && value.every((item) => typeof item === "number" && Number.isFinite(item) && Math.abs(item) <= 1e9);
const displayNumber = (value) => new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3, useGrouping: false }).format(value);
const diagramBindings = new WeakMap();

function visible(condition, values) {
  if (!condition) return true;
  const all = condition.op === "all" ? condition.conditions : condition.all;
  const any = condition.op === "any" ? condition.conditions : condition.any;
  if (Array.isArray(all)) return all.every((item) => visible(item, values));
  if (Array.isArray(any)) return any.some((item) => visible(item, values));
  const value = values[condition.parameter ?? condition.key];
  return condition.op === "eq" ? value === condition.value : condition.op === "ne" ? value !== condition.value : true;
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
    if (!item || !byKey.has(item.parameter) || !visible(item.visibleWhen, values)) return false;
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
  const left = 42 + 24 * linearCount("left"), right = 42 + 24 * linearCount("right");
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
      const labelText = `${index} · ${shortValue}`;
      const labelWidth = Math.max(42, labelText.length * 6.7 + 10);
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
        + `<circle class="td-profile-parameter-badge" cx="${number(anchor[0])}" cy="${number(anchor[1])}" r="11"/><text x="${number(anchor[0])}" y="${number(anchor[1])}" text-anchor="middle" dominant-baseline="central">${index}</text>`;
    }
    return `<g class="td-profile-annotation" data-profile-annotation-key="${attr(item.parameter)}" role="button" tabindex="0" aria-label="${attr(title)}"><title>${text(title)}</title>${body}</g>`;
  }).join("");
  return `<svg class="td-profile-parameter-svg" viewBox="0 0 ${width} ${height}" role="group" aria-label="${attr(localized(snapshot.name, "程式管型"))}参数尺寸示意图">
    <g class="td-profile-diagram-shape" transform="translate(${number(tx)} ${number(ty)}) scale(${scale} ${-scale})">${geometry.markup}</g>${rendered}</svg>`;
}

/** Render only explicit metadata. Parameter names never imply an anchor on arbitrary geometry. */
export function renderProfileParameterDiagram(snapshot, options = {}) {
  const values = snapshot?.parameters ?? options.parameters ?? {};
  const declared = options.definitions ?? snapshot?.parameterDefinitions ?? [];
  const definitions = (Array.isArray(declared) ? declared : []).filter((definition) => definition?.key && visible(definition.visibleWhen, values));
  if (!definitions.length) return "";
  const annotations = validAnnotations(snapshot, definitions, values);
  const svg = diagramSvg(snapshot, annotations, definitions, values);
  const hasAnchors = !!svg && annotations.length > 0;
  const stale = !!snapshot?.parameters && !!options.parameters && definitions.some(({ key }) => options.parameters[key] != null && String(options.parameters[key]) !== String(snapshot.parameters[key]));
  const rows = definitions.map((definition, index) => {
    const annotation = annotations.find((item) => item.parameter === definition.key);
    const name = localized(definition.displayName ?? definition.name, definition.key);
    const description = localized(annotation?.description ?? definition.description ?? definition.help);
    const unit = localized(definition.unit);
    return `<button type="button" class="td-profile-parameter-legend-row" data-profile-annotation-key="${attr(definition.key)}" aria-label="定位参数：${attr(name)}"><b>${index + 1}</b><span><strong>${text(name)}</strong><small>${text(description || (annotation ? "点击查看对应位置并编辑参数" : "此参数尚未提供位置标注"))}</small></span><em>${text(valueText(definition, values))}${unit ? ` ${text(unit)}` : ""}</em></button>`;
  }).join("");
  return `<section class="td-profile-parameter-diagram${options.compact ? " is-compact" : ""}" data-profile-parameter-diagram>
    <header><strong>${text(options.title ?? "参数示意图")}</strong><small>截面尺寸：mm · 点击编号可定位参数</small></header>
    ${svg ? `<div class="td-profile-parameter-canvas">${svg}</div>` : ""}
    ${!hasAnchors ? `<p class="td-profile-diagram-message">此程式管型尚未提供参数位置标注。请参考下方参数说明；管型包可声明专属示意图。</p>` : ""}
    ${stale ? `<p class="td-profile-diagram-message" role="status">参数已更改，此图仍为上次成功生成的截面，请以更新后的结果为准。</p>` : ""}
    <div class="td-profile-parameter-legend">${rows}</div>
  </section>`;
}

/** Bind locally: multiple main/branch/component sections must never highlight each other. */
export function bindProfileParameterDiagrams(mount) {
  for (const scope of mount?.querySelectorAll?.("[data-profile-parameter-scope]") ?? []) {
    if (diagramBindings.has(scope)) { diagramBindings.get(scope)(); continue; }
    scope.dataset.profileDiagramBound = "true";
    const owningScope = (element) => element?.closest?.("[data-profile-parameter-scope]") === scope;
    const controls = () => [...scope.querySelectorAll("[data-profile-parameter-key]")].filter(owningScope);
    const highlight = (key) => {
      for (const node of scope.querySelectorAll("[data-profile-annotation-key], [data-profile-parameter-key]")) {
        if (!owningScope(node)) continue;
        const active = !!key && (node.dataset.profileAnnotationKey ?? node.dataset.profileParameterKey) === key;
        node.classList.toggle("is-active", active);
        if (node.hasAttribute("data-profile-annotation-key")) node.setAttribute("aria-pressed", String(active));
        if (node.hasAttribute("data-profile-parameter-key")) node.closest("label")?.classList.toggle("is-profile-parameter-active", active);
      }
    };
    const keyFor = (target) => {
      const node = target?.closest?.("[data-profile-annotation-key], [data-profile-parameter-key]");
      return owningScope(node) ? node.dataset.profileAnnotationKey ?? node.dataset.profileParameterKey : "";
    };
    const restoreFocus = () => highlight(keyFor(scope.ownerDocument.activeElement));
    diagramBindings.set(scope, restoreFocus);
    scope.addEventListener("focusin", (event) => highlight(keyFor(event.target)));
    scope.addEventListener("focusout", () => queueMicrotask(restoreFocus));
    scope.addEventListener("pointerover", (event) => { const key = keyFor(event.target); if (key) highlight(key); });
    scope.addEventListener("pointerout", (event) => { if (!keyFor(event.relatedTarget)) restoreFocus(); });
    const activate = (event) => {
      const annotation = event.target?.closest?.("[data-profile-annotation-key]");
      if (!annotation || !owningScope(annotation)) return;
      const key = annotation.dataset.profileAnnotationKey;
      const input = controls().find((control) => control.dataset.profileParameterKey === key && !control.disabled);
      if (input) { input.focus({ preventScroll: true }); input.scrollIntoView?.({ block: "nearest", behavior: "smooth" }); }
      highlight(key);
    };
    scope.addEventListener("click", activate);
    scope.addEventListener("keydown", (event) => {
      if (!["Enter", " "].includes(event.key) || event.target?.tagName?.toLowerCase() !== "g") return;
      event.preventDefault(); activate(event);
    });
    restoreFocus();
  }
}
