import { encodeNestingGeometry } from "./nestingPreview.mjs";

let nextFinishedShapeId = 0;

const finitePositive = (value) => typeof value === "number" && Number.isFinite(value) && value > 0;
const finite = (value) => typeof value === "number" && Number.isFinite(value);
const same = (a, b) => Math.abs(a - b) <= 1e-6;

function sectionFromDesignParts(parts) {
  if (!Array.isArray(parts) || parts.length !== 2) return null;
  const [first, second] = parts.map((part) => part?.request);
  if (first?.profileRef?.scope !== "system" || first.profileRef.id !== "rect"
      || second?.profileRef?.scope !== "system" || second.profileRef.id !== "rect") return null;
  const a = first.parameters, b = second.parameters;
  if (!a || !b) return null;
  const width = a.width, depth = a.depth, wall = a.wallThickness;
  const lengthA = first.length, lengthB = second.length;
  if (![width, depth, wall, lengthA, lengthB].every(finitePositive)
      || wall * 2 >= Math.min(width, depth)) return null;
  const outerRadius = a.cornerRadius ?? 0, innerRadius = a.innerRadius ?? 0;
  const otherOuterRadius = b.cornerRadius ?? 0, otherInnerRadius = b.innerRadius ?? 0;
  if (![outerRadius, innerRadius, b.width, b.depth, b.wallThickness,
    otherOuterRadius, otherInnerRadius].every(finite)
      || outerRadius < 0 || innerRadius < 0 || otherOuterRadius < 0 || otherInnerRadius < 0
      || !same(width, b.width) || !same(depth, b.depth) || !same(wall, b.wallThickness)
      || !same(outerRadius, otherOuterRadius) || !same(innerRadius, otherInnerRadius)) return null;
  if (outerRadius > Math.min(width, depth) / 2
      || innerRadius > Math.min(width, depth) / 2 - wall) return null;
  return { width, depth, wall, outerRadius, innerRadius, lengthA, lengthB };
}

function roundedRectangle(width, depth, radius, segments = 5) {
  const halfWidth = width / 2, halfDepth = depth / 2;
  // Keep the loops equally sampled even when a profile has square corners.
  const safeRadius = Math.min(Math.max(0.0001, radius), halfWidth, halfDepth);
  const corners = [
    [halfWidth - safeRadius, halfDepth - safeRadius, 0],
    [-halfWidth + safeRadius, halfDepth - safeRadius, 90],
    [-halfWidth + safeRadius, -halfDepth + safeRadius, 180],
    [halfWidth - safeRadius, -halfDepth + safeRadius, 270],
  ];
  return corners.flatMap(([centerY, centerZ, begin]) =>
    Array.from({ length: segments }, (_, index) => {
      const radians = (begin + 90 * index / segments) * Math.PI / 180;
      return [centerY + safeRadius * Math.cos(radians),
        centerZ + safeRadius * Math.sin(radians)];
    }));
}

function makeFinishedMesh(section, angle, planeRotation) {
  const radians = angle * Math.PI / 180;
  const halfTangent = Math.tan(radians / 2);
  const largestMiter = section.depth / 2 * halfTangent;
  if (!Number.isFinite(halfTangent)
      || largestMiter >= Math.min(section.lengthA, section.lengthB)) return null;

  const contours = [
    roundedRectangle(section.width, section.depth, section.outerRadius),
    roundedRectangle(section.width - 2 * section.wall, section.depth - 2 * section.wall,
      section.innerRadius),
  ];
  const count = contours[0].length;
  const sine = Math.sin(radians), cosine = Math.cos(radians);
  const spin = planeRotation * Math.PI / 180;
  const spinSine = Math.sin(spin), spinCosine = Math.cos(spin);
  const positions = [], indices = [];
  const ring = (contour, station) => {
    const first = positions.length / 3;
    for (const [y, z] of contour) {
      const x = station === 0 ? -section.lengthA
        : station === 1 ? -z * halfTangent
          : section.lengthB * cosine - z * sine;
      const radial = station === 2 ? section.lengthB * sine + z * cosine : z;
      positions.push(x, y * spinCosine - radial * spinSine,
        y * spinSine + radial * spinCosine);
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
      previewKind: "finished-shape", approximate: true,
      angle, planeRotation, section, centerlineCorner: [0, 0, 0],
      endDirection: [cosine, -sine * spinSine, sine * spinCosine],
      planeNormal: [0, spinCosine, spinSine],
      miterPlane: [1, -halfTangent * spinSine, halfTangent * spinCosine],
      contourVertexCount: count,
    },
  };
}

/** A product-shape illustration derived only from its two design parts and scene angles. */
export function buildAssemblyFinishedShapePreview(plan) {
  const section = sectionFromDesignParts(plan?.designParts);
  if (!section) return null;
  const scene = plan?.sceneParameters ?? {};
  const angle = scene.angle ?? scene.jointAngle ?? 90;
  const planeRotation = scene.planeRotation ?? 0;
  if (!finite(angle) || angle <= 0 || angle >= 180
      || !finite(planeRotation)) return null;
  const mesh = makeFinishedMesh(section, angle, planeRotation);
  if (!mesh) return null;
  const url = `icax-assembly-finished://preview/${++nextFinishedShapeId}`;
  return {
    rows: [{
      entityId: "assembly-finished-shape",
      roles: plan.designParts.map((part) => part.role).filter(Boolean),
      data: {
        geometry: { url, version: 1 }, geometryKind: 1, renderClass: 1,
        visible: true, selectable: false,
      },
    }],
    resources: new Map([[url, encodeNestingGeometry(mesh)]]),
    mesh,
  };
}
