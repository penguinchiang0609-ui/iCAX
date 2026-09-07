import { escapeText } from "../../../iCAX-UI/UI/html.mjs";

const WINDOW_LAYOUTS = new Map([
  ["single-face-security-window", "single-face"],
  ["two-face-security-window", "two-face"],
  ["three-face-security-window", "three-face"],
  ["five-face-security-window", "five-face"],
]);

function windowLayout(template) {
  const definition = template?.extensions?.securityWindow;
  if (definition && typeof definition === "object") {
    return definition.layout || WINDOW_LAYOUTS.get(template?.id) || "unspecified";
  }
  return definition === true ? WINDOW_LAYOUTS.get(template?.id) || "unspecified"
    : WINDOW_LAYOUTS.get(template?.id) || "";
}

function dimension(value, fallback) {
  const candidate = value ?? fallback;
  if (candidate === "" || typeof candidate === "boolean" || candidate == null) return null;
  const number = Number(candidate);
  return Number.isFinite(number) && number >= 0 ? number : null;
}

function profileDimension(values, prefix, axis, fallback) {
  const override = values.tubeDesignerProfileOverrides?.[prefix];
  if (override != null) {
    if (override.schema !== "icax.imported-tube-profile" || override.schemaVersion !== 1
      || !["imported-dxf", "parametric-package"].includes(override.kind)) return null;
    const value = dimension(override[axis]);
    return value != null && value > 0 ? value : null;
  }
  const value = dimension(values[`${prefix}${axis[0].toUpperCase()}${axis.slice(1)}`], fallback);
  return value != null && value > 0 ? value : null;
}

// The frozen/evaluated imported profile has precedence over the ordinary inputs,
// just as in the geometry kernel. Share this calculation with the schematic.
export function securityWindowOpeningDimensions(values = {}) {
  const width = dimension(values.doorClearWidth, 800);
  const height = dimension(values.doorClearHeight, 1000);
  const leafDepth = profileDimension(values, "doorLeafFrame", "depth", 20);
  const fixedWidth = profileDimension(values, "doorFrame", "width", 25);
  const hardware = dimension(values.doorHardwareClearance, 10);
  const fixedClearWidth = width == null || width <= 0 || leafDepth == null || hardware == null
    ? null : width + leafDepth + hardware;
  return {
    width, height, leafDepth, fixedWidth, hardware, fixedClearWidth,
    outsideWidth: fixedClearWidth == null || fixedWidth == null ? null : fixedClearWidth + 2 * fixedWidth,
    outsideHeight: height == null || height <= 0 || fixedWidth == null ? null : height + 2 * fixedWidth,
  };
}

function isVGroove(value) {
  return typeof value === "string" && value.startsWith("v_groove_90:");
}

function frameJoinName(value) {
  if (isVGroove(value)) return "V 槽折弯";
  return { miter_45: "45°拼焊", butt_90: "90°直拼" }[value] || "待确认";
}

function connectionReview(layout, values, enabled, rows, warnings) {
  const horizontal = { insert: "横杆插入边框", weld: "横杆平口焊接" }[values.mainHorizontalConnection ?? "insert"] || "待确认";
  const miterPreview = "预览保留未切角管材，转角可能重叠；生产零件按45°切角。";
  rows.push(row("横杆连接", horizontal));
  if (layout === "single-face") {
    const closed = (values.frameLayout ?? "four_sides") === "four_sides";
    const outerJoin = values.frameJoinType ?? "miter_45";
    const fixedJoin = values.doorFrameJoinType ?? "miter_45";
    const leafJoin = values.doorLeafFrameJoinType ?? "miter_45";
    rows.push(row("大框连接", closed ? frameJoinName(outerJoin) : "开边框"));
    if (enabled) rows.push(row("开启框连接", `固定框 ${frameJoinName(fixedJoin)}；窗扇 ${frameJoinName(leafJoin)}`));
    if (!closed) warnings.push("大框未四边闭合，开边需由现场围护补齐并确认固定方式。");
    if ((closed && isVGroove(outerJoin)) || (enabled && (isVGroove(fixedJoin) || isVGroove(leafJoin)))) {
      warnings.push("已选 V 槽折弯：须先打样确认管材、设备及折弯补偿，不能直接按示意图投产。");
    }
    if ((closed && outerJoin === "miter_45") || (enabled && (fixedJoin === "miter_45" || leafJoin === "miter_45"))) {
      warnings.push(miterPreview);
    }
  } else if (["two-face", "three-face", "five-face"].includes(layout)) {
    if (enabled) {
      const fixedJoin = values.doorFrameJoinType ?? "butt_90";
      const leafJoin = values.doorLeafFrameJoinType ?? "butt_90";
      rows.push(row("开启框连接", `固定框 ${frameJoinName(fixedJoin)}；窗扇 ${frameJoinName(leafJoin)}`));
      if (isVGroove(fixedJoin) || isVGroove(leafJoin)) warnings.push("已选 V 槽折弯：须先打样确认管材、设备及折弯补偿，不能直接按示意图投产。");
      if (fixedJoin === "miter_45" || leafJoin === "miter_45") warnings.push(miterPreview);
    }
    rows.push(row("外框转角", { post_butt: "立柱贯通、横梁直拼", rail_miter: "横梁45°拼角" }[values.frameCornerJoin ?? "post_butt"] || "待确认"));
    if (values.frameCornerJoin === "rail_miter") warnings.push(miterPreview);
    if (layout === "five-face") {
      warnings.push("背面朝墙，需确认外凸安装条件、原窗开启和清洁检修空间。");
    } else {
      warnings.push("顶底未独立封闭，开边需由现场墙体、窗台等围护补齐。");
    }
  }
}

function size(width, height) {
  if (width == null || height == null || width <= 0 || height <= 0) return "待填写有效尺寸";
  const format = (value) => Number(value.toFixed(2)).toString();
  return `${format(width)} × ${format(height)} mm（宽 × 高）`;
}

function row(label, value) {
  return `<div><span>${escapeText(label)}：</span><strong style="font-weight:500">${escapeText(value)}</strong></div>`;
}

export function renderSecurityWindowReview(template, values = {}) {
  const layout = windowLayout(template);
  if (!layout) return "";
  values = { ...Object.fromEntries((template?.parameters ?? []).map((field) => [field.key ?? field.name, field.defaultValue])), ...values };
  const enabledValue = values.accessDoorEnabled ?? true;
  const enabled = enabledValue === true || enabledValue === "true" || enabledValue === "是";
  const maintenance = values.doorUse === "maintenance";
  const rows = [row("整窗尺寸口径", "成品外包尺寸")];
  const warnings = [];

  connectionReview(layout, values, enabled, rows, warnings);

  if (!enabled) {
    warnings.push("未设置逃生窗：请确认室内可开启的逃生与救援通道，不能仅凭此模板判断满足现场要求。");
  } else {
    const { width, height, leafDepth, hardware, fixedClearWidth } = securityWindowOpeningDimensions(values);
    rows.push(row("开启口用途", maintenance ? "检修口（非逃生窗）" : "逃生窗"));
    rows.push(row(maintenance ? "目标开启净尺寸" : "目标通行净尺寸", size(width, height)));
    rows.push(row("固定框内净尺寸", size(fixedClearWidth, height)));
    rows.push(row("开启宽度预留", leafDepth == null || hardware == null
      ? "待填写有效尺寸" : `窗扇厚度 ${leafDepth} mm + 五金侵入 ${hardware} mm`));
    warnings.push("目标尺寸为设计值，不代表现场可通行或合规结论。");
    if (maintenance) warnings.push("当前为检修口，不作为逃生窗；应另行确认逃生与救援通道。");
  }

  if (!values.materialGrade || values.materialGrade === "unspecified") {
    warnings.push("材质未指定：管材配套只代表截面规格，请补充材质与表面处理。");
  } else {
    rows.push(row("材质", values.materialGrade));
    if (!values.surfaceTreatment || values.surfaceTreatment === "unspecified") {
      warnings.push("表面处理未指定，请结合安装环境确认防腐做法。");
    }
  }

  return `<aside data-security-window-review aria-label="防盗窗设计核对" style="margin:8px 0;padding:10px 12px;border:1px solid rgba(127,127,127,.25);border-radius:8px;font-size:12px;line-height:1.6;overflow-wrap:anywhere">
    <strong>设计核对</strong>
    <div style="margin-top:4px">${rows.join("")}</div>
    ${warnings.map((message) => `<p style="margin:6px 0 0">${escapeText(message)}</p>`).join("")}
    <p style="margin:6px 0 0">仅完成几何与装配校验，五金开启、墙体锚固、承载和当地要求待现场核验。</p>
  </aside>`;
}
