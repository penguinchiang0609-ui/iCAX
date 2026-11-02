import assert from "node:assert/strict";
import { parseSketchDxf } from "../../apps/tube-designer/webpage/sketchCadDxfImport.mjs";
import { curvePoint } from "../../apps/_shared/workbench/sketch/sketchGeometry.mjs";

const near = (a, b, eps = 1e-8) => a.forEach((v, i) => assert.ok(Math.abs(v - b[i]) < eps, `${a} differs from ${b}`));
const cadDxf = ({ unit = 4, tables = [], blocks = [], entities = [] }) => {
  const section = (name, pairs) => [[0, "SECTION"], [2, name], ...pairs, [0, "ENDSEC"]];
  return [...section("HEADER", [[9, "$INSUNITS"], [70, unit]]), ...section("TABLES", tables), ...section("BLOCKS", blocks), ...section("ENTITIES", entities), [0, "EOF"]].map(([code, value]) => `${code}\n${value}\n`).join("");
};
const lineTags = (layer, a, b, extra = []) => [[0, "LINE"], [8, layer], ...extra, [10, a[0]], [20, a[1]], [11, b[0]], [21, b[1]]];

// Hidden compound records consume their child sequence. Layer names are case
// insensitive, and arbitrary paper tab names cannot contribute cutting paths.
const visibility = parseSketchDxf(cadDxf({
  tables: [[0, "LAYER"], [2, "Hidden"], [62, -7], [0, "LAYER"], [2, "Frozen"], [70, 1]],
  entities: [
    [999, "standard entity-section comment"],
    ...lineTags("Cut", [0, 0], [10, 0], [[410, "Model"]]),
    ...lineTags("cut", [10, 0], [10, 5]),
    [0, "POLYLINE"], [8, "Hidden"], [0, "VERTEX"], [0, "VERTEX"], [0, "SEQEND"],
    [0, "POLYLINE"], [67, 1], [0, "VERTEX"], [0, "SEQEND"],
    [0, "INSERT"], [60, 1], [0, "ATTRIB"], [0, "SEQEND"],
    [0, "ELLIPSE"], [8, "Frozen"],
    ...lineTags("Cut", [10, 5], [0, 0], [[410, "制造图"]]),
    ...lineTags("Cut", [10, 5], [0, 0], [[62, -3]]),
  ],
}));
assert.equal(visibility.entities.length, 1);
assert.equal(visibility.entities[0].kind, "path");
assert.equal(visibility.entities[0].closed, false);
assert.equal(visibility.entities[0].segments.length, 2);
assert.deepEqual(visibility.ignoredEntityCounts, { HIDDEN: 4, PAPERSPACE: 2 });
const junction = parseSketchDxf(cadDxf({ entities: [
  ...lineTags("A", [0, 0], [1, 0]), ...lineTags("A", [1, 0], [2, 0]), ...lineTags("A", [1, 0], [1, 1]),
  ...lineTags("B", [2, 0], [0, 0]),
] }));
assert.equal(junction.entities.length, 4);
assert.ok(junction.entities.every(entity => !entity.closed));

// Independent OCS/block evaluation verifies directed curves through nested,
// nonuniform and reflected transforms. Array spacing is rotated, not scaled.
const blockOrigin = [1, -2], outerOrigin = [4, 5];
const blockShapes = [
  { layer: "Arc", tags: [[0, "ARC"], [8, "Arc"], [10, 0], [20, 4], [40, 1], [50, 20], [51, 300], [230, -1]], sample: t => [-Math.cos((20 + 280 * t) * Math.PI / 180), 4 + Math.sin((20 + 280 * t) * Math.PI / 180)] },
  { layer: "Ellipse", tags: [[0, "ELLIPSE"], [8, "Ellipse"], [10, -2], [20, -3], [11, 3], [21, 4], [40, 0.4], [41, 0.3], [42, 4.7], [230, -1]], sample: t => { const a = 0.3 + 4.4 * t; return [-2 + 3 * Math.cos(a) + 1.6 * Math.sin(a), -3 + 4 * Math.cos(a) - 1.2 * Math.sin(a)]; } },
  { layer: "Circle", tags: [[0, "CIRCLE"], [8, "Circle"], [10, 3], [20, 1], [40, 2]], sample: t => [3 + 2 * Math.cos(t * Math.PI * 2), 1 + 2 * Math.sin(t * Math.PI * 2)] },
  { layer: "Spline", tags: [[0, "SPLINE"], [8, "Spline"], [70, 8], [71, 2], [72, 6], [73, 3], [40, 0], [40, 0], [40, 0], [40, 1], [40, 1], [40, 1], [10, 10], [20, 0], [10, 12], [20, 3], [10, 15], [20, -2]], sample: t => [10 * (1 - t) ** 2 + 24 * (1 - t) * t + 15 * t ** 2, 6 * (1 - t) * t - 2 * t ** 2] },
];
const insertedPoint = (p, origin, sx, sy, angle, normal, at, offset = [0, 0]) => {
  const x = sx * (p[0] - origin[0]), y = sy * (p[1] - origin[1]), c = Math.cos(angle), s = Math.sin(angle);
  return [normal * (at[0] + c * (x + offset[0]) - s * (y + offset[1])), at[1] + s * (x + offset[0]) + c * (y + offset[1])];
};
for (const [sx, sy, normal] of [[2, 0.7, 1], [-2, 0.7, 1], [2, -0.7, -1], [-2, -0.7, -1]]) {
  const innerAngle = 0.43, outerAngle = -0.71, outerScale = [0.8, -1.6], at = [20, -7];
  const mapped = parseSketchDxf(cadDxf({ blocks: [
    [0, "BLOCK"], [2, "Shape"], [10, blockOrigin[0]], [20, blockOrigin[1]], ...blockShapes.flatMap(shape => shape.tags), [0, "ENDBLK"],
    [0, "BLOCK"], [2, "Nested"], [10, outerOrigin[0]], [20, outerOrigin[1]],
    [0, "INSERT"], [2, "Shape"], [10, 5], [20, 9], [41, sx], [42, sy], [50, innerAngle * 180 / Math.PI], [230, normal], [0, "ENDBLK"],
  ], entities: [[0, "INSERT"], [2, "Nested"], [10, at[0]], [20, at[1]], [41, outerScale[0]], [42, outerScale[1]], [50, outerAngle * 180 / Math.PI], [230, -1], [70, 2], [71, 2], [44, 80], [45, 60]] }));
  assert.equal(mapped.entities.length, 16);
  const physical = (p, col, row) => insertedPoint(insertedPoint(p, blockOrigin, sx, sy, innerAngle, normal, [5, 9]), outerOrigin, ...outerScale, outerAngle, -1, at, [80 * col, 60 * row]);
  for (const shape of blockShapes) {
    const imported = mapped.entities.filter(entity => entity.dxfLayer === shape.layer);
    assert.equal(imported.length, 4);
    for (let row = 0; row < 2; row++) for (let col = 0; col < 2; col++) {
      const entity = imported[row * 2 + col];
      if (shape.layer === "Circle") {
        assert.equal(entity.kind, "ellipse");
        for (const t of [0, 0.13, 0.5, 0.86, 1]) {
          const p = physical(shape.sample(t), col, row), dx = p[0] - entity.cx, dy = p[1] - entity.cy, c = Math.cos(entity.rotation), s = Math.sin(entity.rotation);
          assert.ok(Math.abs(((c * dx + s * dy) / entity.radiusX) ** 2 + ((-s * dx + c * dy) / entity.radiusY) ** 2 - 1) < 1e-9);
        }
      } else {
        const segment = shape.layer === "Spline" ? entity.segments[0] : entity;
        for (const t of [0, 0.13, 0.5, 0.86, 1]) near(curvePoint(segment, t), physical(shape.sample(t), col, row));
      }
    }
  }
}

// Survey units have a distinct exact scale; unknown values fail atomically.
for (const [unit, scale] of [[21, 1200000 / 3937], [22, 100000 / 3937], [23, 3600000 / 3937], [24, 6336000000 / 3937]]) {
  const result = parseSketchDxf(cadDxf({ unit, entities: lineTags("0", [1, 2], [3, 4]) }));
  assert.equal(result.unitScaleToMillimeter, scale);
  near([result.entities[0].x1, result.entities[0].y1, result.entities[0].x2, result.entities[0].y2], [scale, 2 * scale, 3 * scale, 4 * scale]);
}
assert.throws(() => parseSketchDxf(cadDxf({ unit: 25, entities: lineTags("0", [1, 2], [3, 4]) })), /单位/);
assert.throws(() => parseSketchDxf(cadDxf({ entities: [[0, "ELLIPSE"], [10, 0], [20, 0], [11, 3], [21, 0], [40, 0.5], [41, 1], [42, 1]] })), /角度/);
assert.throws(() => parseSketchDxf(cadDxf({ unit: 20, entities: lineTags("0", [1e300, 0], [2e300, 1]) })), /有效范围/);
const slightlyOpenSpline = [[0, "SPLINE"], [70, 9], [71, 2], [72, 6], [73, 3], [40, 0], [40, 0], [40, 0], [40, 1], [40, 1], [40, 1], [10, 0], [20, 0], [10, 1], [20, 1], [10, 5e-9], [20, 0]];
assert.throws(() => parseSketchDxf(cadDxf({ unit: 6, entities: slightlyOpenSpline })), /端点/);
console.log("Sketch DXF mapper: nested/reflected/nonuniform blocks, OCS, array spacing, visibility, layers, units and invalid-geometry checks passed.");
