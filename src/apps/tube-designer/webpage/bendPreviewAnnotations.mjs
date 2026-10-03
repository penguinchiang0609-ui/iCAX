// Dimension descriptions for the cold-bend example. The formed shape is an
// illustration; its R + Kt curve is the line used for the blank calculation.

const FORMED_COLOR = 0x66d7ca;
const LENGTH_COLOR = 0xffc857;
const FACTOR_COLOR = 0xffa96b;

const number = (value) => Number(value);
const validPoint = (point) => Array.isArray(point) && point.length === 3
  && point.every((coordinate) => Number.isFinite(number(coordinate)));
const point = (value) => value.map(number);
const scale = (value, factor) => value.map((coordinate) => coordinate * factor);
const format = (value) => Number(number(value).toFixed(2)).toString();

function dimension(id, start, end, offset, label, color) {
  if (![start, end, offset].every(validPoint)) return null;
  return { id, start: point(start), end: point(end), offset: point(offset), label, color };
}

// Assembly matrices are row-major, just like localToWorldMatrix on scene rows.
function transformPoint(matrix, position) {
  if (!Array.isArray(matrix) || matrix.length !== 16 || !matrix.every((value) => Number.isFinite(number(value))))
    return point(position);
  const [x, y, z] = position;
  const w = matrix[12] * x + matrix[13] * y + matrix[14] * z + matrix[15];
  if (!Number.isFinite(w) || Math.abs(w) < 1e-12) return point(position);
  return [
    (matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3]) / w,
    (matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7]) / w,
    (matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11]) / w,
  ];
}

function transformVector(matrix, vector) {
  if (!Array.isArray(matrix) || matrix.length !== 16 || !matrix.every((value) => Number.isFinite(number(value))))
    return point(vector);
  const [x, y, z] = vector;
  return [
    matrix[0] * x + matrix[1] * y + matrix[2] * z,
    matrix[4] * x + matrix[5] * y + matrix[6] * z,
    matrix[8] * x + matrix[9] * y + matrix[10] * z,
  ];
}

function bendData(preview) {
  const supplied = preview?.formedPreview?.annotations;
  if (supplied?.kind === "cold-bend-development") return supplied;
  const recipe = preview?.plan?.formedPreviewRecipe;
  if (recipe?.kind !== "continuous-cold-bend") return null;

  // The blank can still be annotated when a host only has the native plan.
  const plan = preview.plan;
  const [first, second] = plan.designParts ?? [];
  const manufacturing = plan.manufacturingParts?.[0];
  const angle = number(recipe.angle), R = number(recipe.radius);
  const K = number(recipe.factor);
  const t = number(first?.request?.parameters?.wallThickness);
  const straightA = number(first?.request?.length), straightB = number(second?.request?.length);
  const blankLength = number(manufacturing?.request?.length);
  if (![angle, R, K, t, straightA, straightB, blankLength].every(Number.isFinite)
      || angle <= 0 || angle >= 180 || R <= 0 || K < 0 || K > 1 || t <= 0
      || straightA <= 0 || straightB <= 0 || blankLength <= 0) return null;
  const calculationRadius = R + K * t;
  return { kind: "cold-bend-development", values: {
    angle, R, t, K, calculationRadius,
    arcLength: calculationRadius * angle * Math.PI / 180,
    straightA, straightB, blankLength,
  }, anchors: null };
}

function formedDimensions(preview, data) {
  const angle = number(data.values.angle);
  const anchors = data.anchors;
  if (!Number.isFinite(angle) || !anchors
      || ![anchors.arcStart, anchors.arcEnd, anchors.radialUp].every(validPoint)) return [];
  const depth = number(preview.plan.designParts?.[0]?.request?.parameters?.depth) || 40;
  const up = point(anchors.radialUp);
  return [
    dimension("bend-angle", point(anchors.arcStart), point(anchors.arcEnd),
      scale(up, depth / 2 + 52), `轴线转角 θ=${format(angle)}°`, FORMED_COLOR),
  ].filter(Boolean);
}

function blankDimensions(preview, data) {
  const { angle, R, t, K, arcLength, straightA, straightB, blankLength } = data.values;
  const manufacturing = preview.plan.manufacturingParts?.[0];
  const matrix = manufacturing?.explodedMatrix ?? manufacturing?.matrix;
  const width = number(preview.plan.designParts?.[0]?.request?.parameters?.width) || 40;
  const firstBreak = straightA, secondBreak = straightA + arcLength;
  const KContribution = K * t * angle * Math.PI / 180;
  const baseArc = R * angle * Math.PI / 180;
  const sideGap = width / 2 + 24;
  const make = (id, from, to, sideOffset, label, color) => dimension(id,
    transformPoint(matrix, [from, 0, 0]), transformPoint(matrix, [to, 0, 0]),
    transformVector(matrix, [0, sideOffset, 0]), label, color);
  return [
    make("straight-a", 0, firstBreak, -sideGap,
      `直段 A ${format(straightA)} mm`, FORMED_COLOR),
    make("developed-arc", firstBreak, secondBreak, sideGap,
      `弯区 ${format(baseArc)}+${format(KContribution)}=${format(arcLength)} mm`, LENGTH_COLOR),
    make("straight-b", secondBreak, blankLength, -sideGap,
      `直段 B ${format(straightB)} mm`, FORMED_COLOR),
    make("k-contribution", firstBreak, secondBreak, sideGap + 63,
      `K=${format(K)}，补偿 Ktθ(弧度)=+${format(KContribution)} mm`, FACTOR_COLOR),
    make("total-blank", 0, blankLength, -sideGap - 66,
      `总下料 L=${format(blankLength)} mm`, LENGTH_COLOR),
  ].filter(Boolean);
}

/**
 * Build 3D labels for threeViewport.setDimensionAnnotations(). `mode` is
 * "finished"/"formed" or "exploded"/"blank". No shared viewport state changes.
 */
export function buildBendPreviewDimensionAnnotations(preview, mode = "finished") {
  const data = bendData(preview);
  if (!data) return [];
  const blank = mode === "exploded" || mode === "blank" || mode === true;
  return blank ? blankDimensions(preview, data) : formedDimensions(preview, data);
}
