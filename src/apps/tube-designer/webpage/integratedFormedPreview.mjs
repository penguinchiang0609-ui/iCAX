import { encodeNestingGeometry } from "./nestingPreview.mjs";

let nextFormedPreviewId = 0;

function finitePositive(value, label) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0) throw new Error(`${label}无效。`);
  return number;
}

function roundedRectangle(width, depth, radius, segments = 5) {
  const halfWidth = width / 2, halfDepth = depth / 2;
  // A zero-radius corner still needs a tiny visual rounding so the inner and
  // outer loops have matching vertex counts and no collapsed triangles.
  const safeRadius = Math.max(0.0001, Math.min(Number(radius) || 0, halfWidth, halfDepth));
  const corners = [
    [halfWidth - safeRadius, halfDepth - safeRadius, 0],
    [-halfWidth + safeRadius, halfDepth - safeRadius, 90],
    [-halfWidth + safeRadius, -halfDepth + safeRadius, 180],
    [halfWidth - safeRadius, -halfDepth + safeRadius, 270],
  ];
  return corners.flatMap(([centerY, centerZ, begin]) =>
    Array.from({ length: segments + 1 }, (_, index) => {
      const radians = (begin + 90 * index / segments) * Math.PI / 180;
      return [centerY + safeRadius * Math.cos(radians), centerZ + safeRadius * Math.sin(radians)];
    }));
}

function sectionFromPlan(plan) {
  const [first, second] = plan.designParts ?? [];
  if (!first || !second || plan.designParts.length !== 2
      || first.request?.profileRef?.id !== "rect" || second.request?.profileRef?.id !== "rect") {
    throw new Error("当前折弯外形示意仅支持两段相同方矩管。");
  }
  const a = first.request.parameters ?? {}, b = second.request.parameters ?? {};
  const width = finitePositive(a.width, "截面宽度");
  const depth = finitePositive(a.depth, "截面深度");
  const wall = finitePositive(a.wallThickness, "壁厚");
  if (width <= wall * 2 || depth <= wall * 2
      || Math.abs(width - Number(b.width)) > 1e-6
      || Math.abs(depth - Number(b.depth)) > 1e-6
      || Math.abs(wall - Number(b.wallThickness)) > 1e-6) {
    throw new Error("两段截面不一致或壁厚过大，无法绘制连续折弯示意。");
  }
  const outerRadius = Math.max(0, Math.min(Number(a.cornerRadius) || 0, width / 2, depth / 2));
  const innerRadius = Math.max(0, Math.min(Number(a.innerRadius) || 0, width / 2 - wall, depth / 2 - wall));
  const lengthA = finitePositive(first.request.length, "前段长度");
  const lengthB = finitePositive(second.request.length, "后段长度");
  return { width, depth, wall, outerRadius, innerRadius, lengthA, lengthB };
}

function makeColdBendMesh(plan, recipe) {
  const section = sectionFromPlan(plan);
  const angle = Number(recipe.angle);
  if (!Number.isFinite(angle) || angle <= 0 || angle >= 180) throw new Error("折弯角度无效。");
  const bendRadius = finitePositive(recipe.radius, "折弯半径");
  const bendFactor = Number(recipe.factor);
  if (!Number.isFinite(bendFactor) || bendFactor < 0 || bendFactor > 1) {
    throw new Error("折弯因子无效。");
  }

  // The template develops the blank along R + Kt. Use that same radius for
  // this illustrative centerline so the straight legs and arc correspond to
  // the actual blank length. The section itself is not plastically deformed.
  const centerlineRadius = bendRadius + bendFactor * section.wall;
  if (centerlineRadius <= section.depth / 2 + 1e-6) {
    throw new Error("折弯半径小于管截面内侧，无法绘制不自交的折弯外形示意。");
  }
  const blankLength = finitePositive(plan.manufacturingParts?.[0]?.request?.length, "下料长度");
  const radians = angle * Math.PI / 180;
  const point = (x, radial = 0, lateral = 0) => [x, lateral, radial];
  const arcPoint = (theta) => point(centerlineRadius * Math.sin(theta),
    centerlineRadius * (1 - Math.cos(theta)));
  // These are anchors for an explanatory development overlay. The path uses
  // R + Kt to match the template's blank length, but it is not a measured
  // neutral layer on the visible tube wall.
  const annotations = {
    kind: "cold-bend-development",
    values: {
      angle, R: bendRadius, t: section.wall, K: bendFactor,
      calculationRadius: centerlineRadius,
      arcLength: centerlineRadius * radians,
      straightA: section.lengthA,
      straightB: section.lengthB,
      blankLength,
    },
    anchors: {
      bendCenter: point(0, centerlineRadius),
      arcStart: arcPoint(0),
      arcMid: arcPoint(radians / 2),
      arcEnd: arcPoint(radians),
      straightStart: point(-section.lengthA),
      straightEnd: point(centerlineRadius * Math.sin(radians) + section.lengthB * Math.cos(radians),
        centerlineRadius * (1 - Math.cos(radians)) + section.lengthB * Math.sin(radians)),
      planeNormal: [0, 1, 0],
      radialUp: [0, 0, 1],
      wallOuter: point(-section.lengthA, section.depth / 2),
      wallInner: point(-section.lengthA, section.depth / 2 - section.wall),
    },
  };
  const contours = [
    roundedRectangle(section.width, section.depth, section.outerRadius),
    roundedRectangle(section.width - 2 * section.wall, section.depth - 2 * section.wall,
      section.innerRadius),
  ];
  const count = contours[0].length;
  const arcSegments = Math.max(12, Math.ceil(angle / 5));
  const stations = [{ x: -section.lengthA, z: 0, theta: 0 }];
  for (let index = 0; index <= arcSegments; index++) {
    const theta = radians * index / arcSegments;
    stations.push({ x: centerlineRadius * Math.sin(theta),
      z: centerlineRadius * (1 - Math.cos(theta)), theta });
  }
  stations.push({ x: centerlineRadius * Math.sin(radians) + section.lengthB * Math.cos(radians),
    z: centerlineRadius * (1 - Math.cos(radians)) + section.lengthB * Math.sin(radians), theta: radians });

  const positions = [], indices = [];
  const ring = (contour, station) => {
    const first = positions.length / 3;
    const sine = Math.sin(station.theta), cosine = Math.cos(station.theta);
    for (const [y, z] of contour) {
      const axial = station.x - z * sine;
      const radial = station.z + z * cosine;
      positions.push(axial, y, radial);
    }
    return first;
  };
  const outer = stations.map((station) => ring(contours[0], station));
  const inner = stations.map((station) => ring(contours[1], station));
  const wallStrip = (near, far, inward = false) => {
    for (let index = 0; index < count; index++) {
      const next = (index + 1) % count;
      if (inward) indices.push(near + index, far + next, near + next,
        near + index, far + index, far + next);
      else indices.push(near + index, near + next, far + next,
        near + index, far + next, far + index);
    }
  };
  for (let station = 0; station < stations.length - 1; station++) {
    wallStrip(outer[station], outer[station + 1]);
    wallStrip(inner[station], inner[station + 1], true);
  }
  const last = stations.length - 1;
  for (let index = 0; index < count; index++) {
    const next = (index + 1) % count;
    indices.push(outer[0] + index, inner[0] + index, inner[0] + next,
      outer[0] + index, inner[0] + next, outer[0] + next);
    indices.push(outer[last] + index, outer[last] + next, inner[last] + next,
      outer[last] + index, inner[last] + next, inner[last] + index);
  }
  return {
    kind: 1, positions, indices,
    metadata: { angle, section, bendMethod: "cold",
      bendRadius, bendFactor, centerlineRadius, contourVertexCount: count,
      stationCount: stations.length, arcSegments, approximate: true, annotations },
  };
}

/**
 * A single hollow appearance mesh. Cold bending follows the developed arc;
 * supported groove templates retain their mitered hinge. Neither is a
 * manufactured BRep; the actual blank stays in the separate 下料件 view.
 */
export function makeIntegratedFormedMesh(plan) {
  const recipe = plan?.formedPreviewRecipe;
  if (!recipe || plan?.resolvedWorkflow?.realization !== "integrated"
      || plan?.manufacturingParts?.length !== 1) {
    throw new Error("当前方案没有可绘制的一体折弯外形。");
  }
  if (recipe.kind === "continuous-cold-bend") return makeColdBendMesh(plan, recipe);
  if (recipe.kind !== "node-groove-fold") throw new Error("装配模板声明了不支持的成形预览策略。");
  const section = sectionFromPlan(plan);
  const angle = Number(recipe.angle);
  if (!Number.isFinite(angle) || angle <= 0 || angle >= 180) throw new Error("折弯角度无效。");
  const radians = angle * Math.PI / 180;
  const sine = Math.sin(radians), cosine = Math.cos(radians);
  const halfTangent = Math.tan(radians / 2);
  const largestMiter = section.depth / 2 * halfTangent;
  if (largestMiter >= Math.min(section.lengthA, section.lengthB)) {
    throw new Error("当前角度与管段长度无法形成完整的折弯外形示意。");
  }

  const groove = plan.resolvedWorkflow.partOperations?.find((item) => item.processId === recipe.processId)?.toolParameters ?? {};
  if (groove.bottomCut === true) throw new Error("槽底已切断，无法绘制单根连续管的折弯外形。");
  const declaredBridge = Number(groove.leaveBottom ?? groove.bridge ?? section.wall / 2);
  if (!Number.isFinite(declaredBridge) || declaredBridge < 0 || declaredBridge >= section.depth) {
    throw new Error("槽根留底高度无效。");
  }
  const rootHeight = declaredBridge + (groove.bottomReference === "inner" ? section.wall : 0);
  if (rootHeight >= section.depth) throw new Error("槽根位置超出管截面。");
  // The groove's retained bottom wall is the fold hinge. At 90 degrees the
  // default 40 mm section with a 1 mm bridge has hinge z=-19 mm, x=0. The
  // centerline corner moves left; it is never used as a pivot for two full
  // intersecting tubes.
  const hingeZ = -section.depth / 2 + rootHeight;
  const centerX = hingeZ * halfTangent;
  const contours = [
    roundedRectangle(section.width, section.depth, section.outerRadius),
    roundedRectangle(section.width - 2 * section.wall, section.depth - 2 * section.wall,
      section.innerRadius),
  ];
  const count = contours[0].length;
  const positions = [], indices = [];
  const ring = (contour, station) => {
    const first = positions.length / 3;
    for (const [y, z] of contour) {
      if (station === 0) positions.push(centerX - section.lengthA, y, z);
      else if (station === 1) positions.push(centerX - z * halfTangent, y, z);
      else positions.push(centerX + section.lengthB * cosine - z * sine,
        y, section.lengthB * sine + z * cosine);
    }
    return first;
  };
  const outer = [0, 1, 2].map((station) => ring(contours[0], station));
  const inner = [0, 1, 2].map((station) => ring(contours[1], station));
  const wallStrip = (near, far, inward = false) => {
    for (let index = 0; index < count; index++) {
      const next = (index + 1) % count;
      if (inward) indices.push(near + index, far + next, near + next,
        near + index, far + index, far + next);
      else indices.push(near + index, near + next, far + next,
        near + index, far + next, far + index);
    }
  };
  for (let station = 0; station < 2; station++) {
    wallStrip(outer[station], outer[station + 1]);
    wallStrip(inner[station], inner[station + 1], true);
  }
  for (let index = 0; index < count; index++) {
    const next = (index + 1) % count;
    indices.push(outer[0] + index, inner[0] + index, inner[0] + next,
      outer[0] + index, inner[0] + next, outer[0] + next);
    indices.push(outer[2] + index, outer[2] + next, inner[2] + next,
      outer[2] + index, inner[2] + next, inner[2] + index);
  }
  return {
    kind: 1, positions, indices,
    metadata: {
      angle, section, miterPlane: [1, 0, halfTangent],
      centerlineCorner: [centerX, 0, 0],
      hinge: [0, 0, hingeZ],
      outerCorner: [centerX + largestMiter, 0, -section.depth / 2],
      contourVertexCount: count,
      approximate: true,
    },
  };
}

export function buildIntegratedFormedPreview(preview) {
  const mesh = preview?.plan?.formedPreviewMesh ?? makeIntegratedFormedMesh(preview?.plan);
  const blank = preview.plan.manufacturingParts[0];
  const roles = [...new Set((preview.plan.designParts ?? []).map((part) => part.role).filter(Boolean))];
  const url = `icax-assembly-formed://preview/${++nextFormedPreviewId}`;
  return {
    rows: [{
      entityId: `assembly-formed:${blank?.blankId ?? preview.plan.templateId}`,
      roles: roles.length ? roles : blank?.participantRoles ?? [],
      data: {
        geometry: { url, version: 1 },
        geometryKind: 1,
        renderClass: 1,
        visible: true,
        selectable: false,
      },
    }],
    resources: new Map([[url, encodeNestingGeometry(mesh)]]),
    approximate: true,
    mesh,
    annotations: mesh.metadata.annotations ?? null,
  };
}
