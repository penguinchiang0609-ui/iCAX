const DEFAULT_PARAMETERS = Object.freeze({
  productCode: "TD-001",
  height: 1800,
  width: 1200,
  horizontalCount: 4,
  middleVerticalCount: 1,
  firstHorizontalTopOffset: 200,
  lastHorizontalBottomOffset: 200,
  horizontalBranchReserve: -1,
  verticalBranchReserve: -1,
  assemblyClearance: 0.1,
  frameProfileType: "rect",
  frameWidth: 50,
  frameDepth: 50,
  frameWallThickness: 2,
  verticalProfileType: "rect",
  verticalWidth: 25,
  verticalDepth: 25,
  verticalWallThickness: 1.5,
  horizontalProfileType: "rect",
  horizontalWidth: 35,
  horizontalDepth: 35,
  horizontalWallThickness: 1.5,
});

export function getDefaultParameters() {
  return { ...DEFAULT_PARAMETERS };
}

export function renderDesignerLeftPane(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const values = view.tubeDesignerDraft ?? designer.product?.parameters ?? DEFAULT_PARAMETERS;
  const pending = Boolean(view.pending || view.tubeDesignerLoading);
  return `
    <div class="tube-designer-panel" data-tube-designer-form>
      <div class="tube-designer-heading">
        <strong>产品参数</strong>
        <span>防盗窗 1 号 · 矩形管 P0</span>
      </div>
      ${section("基本尺寸", [
        field("产品编号", "productCode", values.productCode, { wide: true, type: "text" }),
        field("产品高度 L (mm)", "height", values.height),
        field("产品宽度 D (mm)", "width", values.width),
        field("横管数量", "horizontalCount", values.horizontalCount, { step: 1, min: 0, max: 100 }),
        field("中间竖管数量", "middleVerticalCount", values.middleVerticalCount, { step: 1, min: 0, max: 10 }),
        field("首横管距顶部 (mm)", "firstHorizontalTopOffset", values.firstHorizontalTopOffset),
        field("末横管距底部 (mm)", "lastHorizontalBottomOffset", values.lastHorizontalBottomOffset),
      ])}
      ${profileSection("外框管", "frame", values)}
      ${profileSection("中间竖管", "vertical", values)}
      ${profileSection("横管", "horizontal", values)}
      ${section("装配", [
        field("横支管安装预留 (mm)", "horizontalBranchReserve", values.horizontalBranchReserve),
        field("竖支管安装预留 (mm)", "verticalBranchReserve", values.verticalBranchReserve),
        field("开孔装配间隙 (mm)", "assemblyClearance", values.assemblyClearance, { min: 0 }),
      ])}
      <span class="tube-designer-help">预留量为 -1 时按外框管宽度的一半计算。</span>
      <button class="tube-designer-primary" data-cam-action="tube-designer-generate" ${pending ? "disabled" : ""}>生成产品与拆单</button>
    </div>
  `;
}

export function renderDesignerRightPane(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  const parts = [...(designer.parts ?? [])].sort((a, b) => Number(a.index) - Number(b.index));
  const joints = designer.joints ?? [];
  const run = designer.generationRun;
  if (!product) {
    return `
      <div class="tube-designer-panel">
        <div class="tube-designer-heading"><strong>制造拆单</strong><span>尚未生成产品</span></div>
        <div class="tube-designer-empty">填写左侧参数并生成后，这里会显示制造零件、连接意图和 STEP 输出。</div>
      </div>
    `;
  }
  return `
    <div class="tube-designer-panel">
      <div class="tube-designer-heading">
        <strong>${escapeText(product.productCode)}</strong>
        <span>${escapeText(product.name)} · ${escapeText(product.templateVersion)}</span>
      </div>
      <div class="tube-designer-summary">
        ${metric(parts.length, "制造零件")}
        ${metric(joints.length, "连接意图")}
        ${metric(Number(run?.issueCount ?? 0), "生成问题")}
      </div>
      <div class="tube-designer-section">
        <strong>零件清单</strong>
        <div class="tube-designer-part-list">${parts.map(renderPart).join("")}</div>
      </div>
      <div class="tube-designer-section">
        <strong>STEP 输出</strong>
        <div class="tube-designer-export-row">
          <input data-tube-designer-export-directory value="${escapeAttribute(view.tubeDesignerExportDirectory ?? "D:\\\\TubeDesigner\\\\Output")}" aria-label="STEP 输出目录" />
          <button class="tube-designer-primary" data-cam-action="tube-designer-export-all" ${view.pending || !parts.length ? "disabled" : ""}>导出全部 ${parts.length} 个零件</button>
        </div>
      </div>
    </div>
  `;
}

export function renderDesignerViewportOverlay(_context, view) {
  const designer = view.scene?.tubeDesigner ?? {};
  const product = designer.product;
  if (!product) return "";
  const parameters = product.parameters ?? {};
  return `
    <div class="tube-designer-overlay">
      <strong>${escapeText(product.productCode)} · 装配预览</strong>
      <span>${formatNumber(parameters.width)} × ${formatNumber(parameters.height)} mm · ${designer.members?.length ?? 0} 个装配构件</span>
    </div>
  `;
}

function profileSection(title, prefix, values) {
  return section(title, [
    `<label class="tube-designer-field wide">截面类型<select data-tube-designer-parameter="${prefix}ProfileType"><option value="rect" selected>矩形管</option></select></label>`,
    field("截面宽度 (mm)", `${prefix}Width`, values[`${prefix}Width`]),
    field("截面深度 (mm)", `${prefix}Depth`, values[`${prefix}Depth`]),
    field("壁厚 (mm)", `${prefix}WallThickness`, values[`${prefix}WallThickness`], { min: 0.1 }),
  ]);
}

function section(title, fields) {
  return `<section class="tube-designer-section"><strong>${escapeText(title)}</strong><div class="tube-designer-field-grid">${fields.join("")}</div></section>`;
}

function field(label, name, value, options = {}) {
  const type = options.type ?? "number";
  const attributes = [
    `type="${type}"`,
    `data-tube-designer-parameter="${escapeAttribute(name)}"`,
    `value="${escapeAttribute(value)}"`,
  ];
  if (options.step != null) attributes.push(`step="${escapeAttribute(options.step)}"`);
  else if (type === "number") attributes.push('step="any"');
  if (options.min != null) attributes.push(`min="${escapeAttribute(options.min)}"`);
  if (options.max != null) attributes.push(`max="${escapeAttribute(options.max)}"`);
  return `<label class="tube-designer-field ${options.wide ? "wide" : ""}">${escapeText(label)}<input ${attributes.join(" ")} /></label>`;
}

function metric(value, label) {
  return `<div class="tube-designer-metric"><strong>${escapeText(value)}</strong><span>${escapeText(label)}</span></div>`;
}

function renderPart(part) {
  return `
    <div class="tube-designer-part">
      <strong>${escapeText(part.partNumber)}</strong>
      <span>${escapeText(roleLabel(part.role))} · Q${escapeText(part.quantity ?? 1)}</span>
      <span class="tube-designer-part-length">${formatNumber(part.length)} mm</span>
    </div>
  `;
}

function roleLabel(role) {
  return ({
    "left-frame": "左外框",
    "right-frame": "右外框",
    "middle-vertical": "中间竖管",
    horizontal: "横管",
  })[role] ?? role ?? "零件";
}

function formatNumber(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) return "-";
  return new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3 }).format(number);
}

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function escapeAttribute(value) {
  return escapeText(value).replaceAll("'", "&#39;");
}
