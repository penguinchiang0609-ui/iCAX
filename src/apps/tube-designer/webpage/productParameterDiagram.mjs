import { catalogText, getTemplateVisualAsset } from "./productCatalog.mjs";

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function definitionMap(template) {
  return new Map((template?.parameters ?? []).map((field) => [String(field?.key ?? field?.name ?? ""), field]));
}

function dimensionValue(value, field) {
  const number = Number(value);
  const text = Number.isFinite(number)
    ? new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3 }).format(number)
    : String(value ?? "—");
  const unit = String(field?.unit ?? "").trim();
  return unit ? `${text} ${unit}` : text;
}

export function productPrimaryDimensions(template, values = {}) {
  const primary = template?.extensions?.primaryDimensions ?? {};
  const fields = definitionMap(template);
  return [
    ["width", primary.widthParameter, "宽度"],
    ["height", primary.heightParameter, "高度"],
    ["depth", primary.depthParameter, "深度"],
  ].filter(([, parameter]) => typeof parameter === "string" && parameter.trim()).map(([kind, parameter, fallback]) => {
    const field = fields.get(parameter);
    return {
      kind,
      parameter,
      label: catalogText(field?.displayName, field?.label ?? fallback),
      value: dimensionValue(values?.[parameter], field),
    };
  });
}

export function renderProductParameterDiagram(template, values = {}, options = {}) {
  const dimensions = productPrimaryDimensions(template, values);
  if (!dimensions.length) return "";
  const source = getTemplateVisualAsset(template, "schematic") || getTemplateVisualAsset(template, "icon");
  const active = String(options.activeParameter ?? "");
  const mode = options.mode === "add" ? "add" : "right";
  const artwork = source
    ? `<img src="${escapeText(source)}" alt="${escapeText(catalogText(template?.name, "产品"))}示意图" />`
    : `<svg viewBox="0 0 160 110" role="img" aria-label="产品尺寸示意图"><rect x="26" y="18" width="108" height="74" rx="5"/><path d="M20 100h120M16 96l4 4-4 4M144 96l-4 4 4 4M148 14v82M144 10l4 4 4-4M144 100l4-4 4 4"/></svg>`;
  return `<section class="tube-designer-product-diagram" data-tube-designer-product-diagram>
    <header><div><strong>成品尺寸示意</strong><span>尺寸随参数联动 · 点击标注定位参数</span></div></header>
    <div class="tube-designer-product-diagram-canvas">
      <div class="tube-designer-product-diagram-art">${artwork}</div>
      ${dimensions.map((dimension) => `<button type="button" class="tube-designer-product-dimension is-${dimension.kind} ${dimension.parameter === active ? "is-active" : ""}"
        data-cam-action="tube-designer-focus-product-parameter" data-tube-designer-editor-mode="${mode}" data-tube-designer-parameter-key="${escapeText(dimension.parameter)}">
        <span>${escapeText(dimension.label)}</span><strong>${escapeText(dimension.value)}</strong>
      </button>`).join("")}
    </div>
  </section>`;
}
