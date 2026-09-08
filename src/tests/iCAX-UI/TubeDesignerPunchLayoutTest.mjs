import assert from "node:assert/strict";
import { normalizePunchLayout, resolvePunchLayout, validatePunchLayout, punchLayoutInstanceCount } from "../../apps/tube-designer/webpage/punchLayout.mjs";

let cases = 0;
function resolve(feature, length = 1000) {
  const result = resolvePunchLayout(feature, length);
  assert.equal(result.layoutError, "", JSON.stringify(feature));
  assert.deepEqual(resolvePunchLayout(result, length), result, "Resolved layouts must be idempotent.");
  cases++;
  return result;
}
function positions(feature, expected, length = 1000) {
  const result = resolve(feature, length);
  assert.deepEqual(result.layoutSummary.positions, expected);
  return result;
}
function invalid(feature, pattern, length = 1000) {
  const result = resolvePunchLayout(feature, length);
  assert.match(result.layoutError, pattern);
  assert.deepEqual(result.arrayOffsets, []);
  assert.deepEqual(result.rowOffsets, []);
  assert.equal(punchLayoutInstanceCount(feature, length), 0);
  cases++;
}

// A01/A02: a changed length preserves both margins and the total number of holes.
const margins = { distributionMode: "end-margins", headMargin: 100, tailMargin: 150, arrayCount: 6 };
positions(margins, [100, 250, 400, 550, 700, 850]);
positions(margins, [100, 290, 480, 670, 860, 1050], 1200);
positions(resolve(margins), [100, 290, 480, 670, 860, 1050], 1200);

// A03: the transport offsets already include end-reference reversal.
const fromEnd = positions({ reference: "end", station: 80, arrayPitch: 150, arrayCount: 4 }, [920, 770, 620, 470]);
assert.deepEqual(fromEnd.arrayOffsets, [0, -150, -300, -450]);
assert.equal(fromEnd.reference, "end");
assert.equal(fromEnd.station, 80);
positions({ station: 500, arrayCount: 3, arrayPitch: -100 }, [500, 400, 300]);
positions({ reference: "end", station: 500, arrayCount: 3, arrayPitch: -100 }, [500, 600, 700]);
positions({ reference: "center", station: -100, arrayCount: 3, arrayPitch: 100 }, [400, 500, 600]);

// A04/A05: true centre-outward layouts retain an explicit centre phase.
const center = { distributionMode: "center-out", arrayPitch: 100, arrayCount: 5, centerMode: "hole" };
positions(center, [300, 400, 500, 600, 700]);
invalid({ ...center, arrayCount: 6 }, /奇数/);
positions({ ...center, centerMode: "gap", arrayCount: 6 }, [250, 350, 450, 550, 650, 750]);
invalid({ ...center, centerMode: "gap" }, /偶数/);
positions({ ...center, centerOffset: 50 }, [350, 450, 550, 650, 750]);
positions({ ...center, centerMode: "gap", arrayCount: 6, arrayPitch: 150, centerFirstOffset: 100 }, [100, 250, 400, 600, 750, 900]);
const automaticHalfStep = resolve({ ...center, centerMode: "gap", arrayCount: 4 });
assert.equal(automaticHalfStep.centerFirstOffset, null);
positions({ ...automaticHalfStep, arrayPitch: 200 }, [200, 400, 600, 800]);
positions({ ...automaticHalfStep, centerFirstOffset: "" }, [350, 450, 550, 650]);
positions({ ...automaticHalfStep, centerFirstOffset: "  " }, [350, 450, 550, 650]);
positions({ ...center, centerOffset: 600 }, [900, 1000, 1100, 1200, 1300]);
invalid({ ...center, centerMode: "gap", arrayCount: 2, centerFirstOffset: 0 }, /首对孔/);

// A06/A07: auto-count and a maximum spacing constraint are different rules.
const fill = { distributionMode: "fill", headMargin: 100, tailMargin: 100, arrayPitch: 180 };
const filled = positions(fill, [100, 280, 460, 640, 820]);
assert.equal(filled.layoutSummary.remainder, 80);
positions({ ...fill, fillAlign: "center" }, [140, 320, 500, 680, 860]);
positions({ ...fill, fillAlign: "end" }, [180, 360, 540, 720, 900]);
positions({ ...fill, arrayPitch: 900, fillAlign: "center" }, [500]);
positions({ ...fill, headMargin: 400, tailMargin: 600 }, [400]);
const maximum = positions({ ...fill, distributionMode: "max-spacing" }, [100, 260, 420, 580, 740, 900]);
assert.equal(maximum.maxSpacing, 180);
assert.equal(maximum.arrayPitch, 160);
// At a different length, the original 180 mm maximum must not become 160 mm.
const longerMaximum = resolve(maximum, 1090);
assert.equal(longerMaximum.arrayCount, 6);
assert.equal(longerMaximum.arrayPitch, 178);
positions({ ...fill, distributionMode: "max-spacing", headMargin: 400, tailMargin: 600 }, [400]);
invalid({ ...fill, arrayPitch: 0 }, /大于零/);
invalid({ ...fill, arrayPitch: -1 }, /大于零/);
invalid({ ...fill, arrayPitch: 0.01 }, /1 至 1000/);

// A08: intervals accumulate; safe repetition accepts numbers, never expressions.
const sequence = { distributionMode: "sequence", station: 100, spacingSequence: "50, 100; 50\n200" };
positions(sequence, [100, 150, 250, 300, 500]);
positions({ ...sequence, reference: "end" }, [900, 850, 750, 700, 500]);
positions({ ...sequence, spacingSequence: "50*3，100×2" }, [100, 150, 200, 250, 350, 450]);
positions({ ...sequence, spacingSequence: ".5 2e1" }, [100, 100.5, 120.5]);
invalid({ ...sequence, spacingSequence: "50, 0, 30" }, /大于零/);
invalid({ ...sequence, spacingSequence: "50, -1" }, /大于零/);
invalid({ ...sequence, spacingSequence: "50*0" }, /重复次数/);
invalid({ ...sequence, spacingSequence: "50*1000" }, /包含首孔/);
invalid({ ...sequence, spacingSequence: "50*1000000000" }, /超过/);
invalid({ ...sequence, spacingSequence: "globalThis.pwned=true" }, /不是有效数字/);
assert.equal(globalThis.pwned, undefined);
invalid({ ...sequence, spacingSequence: "Infinity" }, /不是有效数字/);

// A09: this release deliberately does not label centre spacing as edge clearance.
const equal = positions({ distributionMode: "equal", arrayCount: 4 }, [200, 400, 600, 800]);
assert.equal(equal.layoutSummary.distanceBasis, "center");
positions({ distributionMode: "middle-fixed", arrayCount: 5, arrayPitch: 100 }, [300, 400, 500, 600, 700]);
positions({ distributionMode: "middle-fixed", arrayCount: 6, arrayPitch: 100 }, [250, 350, 450, 550, 650, 750]);

// A10: N=1 cannot silently discard a second conflicting end constraint.
invalid({ ...margins, arrayCount: 1, tailMargin: 100 }, /只有一个孔/);
positions({ ...margins, arrayCount: 1, tailMargin: 900 }, [100]);
invalid({ ...margins, headMargin: 900, tailMargin: 150 }, /之和不能超过/);
invalid({ ...margins, headMargin: -1 }, /不小于零/);

// A11/A12: a full ring excludes its repeated endpoint; a partial arc includes both.
const ring = resolve({ station: 500, face: "round", rowDistributionMode: "full-circle", rowCount: 4, rowStartAngle: 30 });
assert.equal(ring.offset, 0);
assert.deepEqual(ring.rowOffsets, [30, 120, 210, 300]);
// Both source kinds consume precisely the same transform, including a nonzero phase.
const branchRing = resolve({ ...ring, toolTarget: "part", recordKind: "branch" });
assert.deepEqual(branchRing.rowOffsets, ring.rowOffsets);
assert.equal(branchRing.offset, 0);
const dxfRing = resolve({ ...ring, toolTarget: "part", recordKind: "dxf", rowStartAngle: -45 });
assert.deepEqual(dxfRing.rowOffsets, [-45, 45, 135, 225]);
assert.equal(dxfRing.offset, 0);
const arc = resolve({ station: 500, face: "round", rowDistributionMode: "angle-range", rowCount: 4, rowStartAngle: 0, rowEndAngle: 180 });
assert.deepEqual(arc.rowOffsets, [0, 60, 120, 180]);
const reverseArc = resolve({ ...arc, rowStartAngle: 180, rowEndAngle: 0 });
assert.deepEqual(reverseArc.rowOffsets, [180, 120, 60, 0]);
const shiftedArc = resolve({ ...arc, rowStartAngle: 30, rowEndAngle: 210 });
assert.deepEqual(shiftedArc.rowOffsets, [30, 90, 150, 210]);
assert.equal(shiftedArc.offset, 0);
const legacyPhase = resolve({ station: 500, face: "round", offset: 30, rowCount: 4, rowPitch: 90 });
assert.equal(legacyPhase.offset, 30);
assert.deepEqual(legacyPhase.rowOffsets, [0, 90, 180, 270]);
invalid({ ...arc, rowEndAngle: 360 }, /小于 360/);
invalid({ ...arc, rowCount: 1 }, /只有一排/);
invalid({ ...ring, face: "top" }, /周向角度加工面/);
invalid({ station: 500, face: "round", rowCount: 5, rowPitch: 90 }, /位置重复/);
invalid({ station: 500, face: "round", rowCount: 3, rowPitch: 360 }, /位置重复/);
assert.deepEqual(resolve({ station: 500, rowCount: 3, rowPitch: -20 }).rowOffsets, [0, -20, -40]);

// A13: stable candidate indices preserve lattice positions across recomputation.
const skips = { distributionMode: "end-margins", headMargin: 100, tailMargin: 100, arrayCount: 5, skipInstancesText: "3" };
const skipped = resolve(skips);
assert.deepEqual(skipped.skippedInstances, ["0:2"]);
assert.deepEqual(skipped.layoutSummary.positions, [100, 300, 500, 700, 900]);
assert.equal(skipped.layoutSummary.candidateCount, 5);
assert.equal(skipped.layoutSummary.actualCount, 4);
assert.deepEqual(resolve(skipped, 1200).skippedInstances, ["0:2"]);
const twoRows = resolve({ ...skips, rowCount: 2, skipInstancesText: "3, 2:4" });
assert.deepEqual(twoRows.skippedInstances, ["0:2", "1:3"]);
assert.equal(punchLayoutInstanceCount(twoRows, 1000), 8);
assert.equal(punchLayoutInstanceCount({ ...twoRows, opposite: true }, 1000), 16);
assert.equal(punchLayoutInstanceCount({ ...twoRows, opposite: true, through: true }, 1000), 8);
assert.equal(punchLayoutInstanceCount({ ...twoRows, enabled: false }, 1000), 0);
invalid({ ...skips, skipInstancesText: "0" }, /超出/);
invalid({ ...skips, skipInstancesText: "2:3" }, /超出/);
invalid({ ...skips, skipInstancesText: "3,1:3" }, /重复填写/);
invalid({ ...skips, skipInstancesText: "1,2,3,4,5" }, /所有候选孔位/);
invalid({ ...skips, arrayCount: 2 }, /超出/);
assert.deepEqual(resolve({ station: 100, arrayCount: 3, skippedInstances: ["0:2"] }).skippedInstances, ["0:2"]);

// A14's coordinate set can already be expressed by intervals or positions. This
// does not claim that mixed-shape repeated groups have been implemented.
positions({ ...sequence, spacingSequence: "30,170,30,170,30" }, [100, 130, 300, 330, 500, 530]);
const explicit = { distributionMode: "positions", positionList: "100\t130\n300 330;500,530" };
positions(explicit, [100, 130, 300, 330, 500, 530]);
positions({ ...explicit, positionList: "900 100 500" }, [900, 100, 500]);
invalid({ ...explicit, positionList: "100 100" }, /重复/);
positions({ ...explicit, positionList: "100 1100" }, [100, 1100]);
positions({ ...explicit, positionList: "-20 100" }, [-20, 100]);
invalid({ ...explicit, positionList: "100*2" }, /不是有效数字/);

// Every source permits end-clipped tools without a separate allow-open switch.
// The layout describes positions; native geometry determines material intersections.
positions({ station: -2, diameter: 10 }, [-2]);
const openEnd = positions({ station: -2, diameter: 10, allowOpen: true }, [-2]);
assert.equal(openEnd.layoutSummary.requiresIntersectionCheck, true);
assert.equal(openEnd.layoutSummary.outsideCenterCount, 1);
positions({ station: -2, reference: "end", diameter: 10, allowOpen: true }, [1002]);
positions({ ...explicit, positionList: "-2 500 1002", allowOpen: true }, [-2, 500, 1002]);
// Branch/DXF/V-notch sources use the same clipping policy, including new layouts.
positions({ toolTarget: "part", station: -2, distributionMode: "pitch" }, [-2]);
positions({ toolTarget: "part", station: -2, reference: "end", distributionMode: "pitch" }, [1002]);
positions({ toolTarget: "part", ...explicit, positionList: "-2 500" }, [-2, 500]);
positions({ toolTarget: "part", ...center, centerOffset: 600 }, [900, 1000, 1100, 1200, 1300]);
positions({ toolTarget: "part", ...sequence, station: -2 }, [-2, 48, 148, 198, 398]);
positions({ ...center, centerOffset: 600, allowOpen: true }, [900, 1000, 1100, 1200, 1300]);
invalid({ ...margins, headMargin: -2, allowOpen: true }, /不小于零/);
invalid({ station: NaN, allowOpen: true }, /有效数字/);
invalid({ toolTarget: "part", arrayCount: 3, arrayPitch: Number.MAX_VALUE }, /有限数字/);
positions({ station: -2, allowOpen: false }, [-2]);
const crossedEnd = positions({ station: 980, arrayCount: 3, arrayPitch: 15 }, [980, 995, 1010]);
assert.equal(crossedEnd.layoutSummary.outsideCenterCount, 1);
assert.equal(crossedEnd.layoutSummary.requiresIntersectionCheck, true);

// Invalid values cannot silently become defaults, nor allocate unbounded arrays.
invalid({ station: "" }, /有效数字/);
invalid({ arrayCount: 1.5 }, /整数/);
invalid({ arrayCount: Infinity }, /整数/);
invalid({ arrayCount: 0 }, /整数/);
invalid({ arrayPitch: NaN }, /有效数字/);
invalid({ arrayCount: 2, arrayPitch: 0 }, /不能为零/);
invalid({ rowCount: 0 }, /整数/);
invalid({ rowCount: 2, rowPitch: 0 }, /不能为零/);
invalid({ arrayCount: 1001 }, /1 至 1000/);
invalid({ arrayCount: 100, arrayPitch: 1, rowCount: 11 }, /候选位置/);
invalid({ arrayCount: 501, arrayPitch: 1, opposite: true }, /展开刀具/);
invalid({ arrayCount: 501, arrayPitch: 1, opposite: true, skipInstancesText: "1,2,3" }, /跳过孔仍计入/);
invalid({ arrayCount: 3, arrayPitch: Number.MAX_VALUE }, /有限数字/);
invalid({ distributionMode: "positions", positionList: "-1.7e308 1.7e308" }, /轴向偏移不是有限数字/);
invalid({ rowCount: 3, rowPitch: Number.MAX_VALUE }, /横向／周向偏移不是有限数字/);
invalid({}, /主管长度/, 0);
invalid({}, /主管长度/, Infinity);
invalid({ distributionMode: "unknown" }, /有效的轴向/);
assert.equal(validatePunchLayout({ enabled: false, station: NaN }, 0), "");
assert.equal(resolve({ arrayCount: 1000, arrayPitch: 1 }).arrayOffsets.length, 1000);
const opaque = { customRule: { linkedSource: "keep" }, toolRef: { id: "v-groove", version: "1" }, positionList: "12 24" };
assert.deepEqual(normalizePunchLayout(opaque).customRule, opaque.customRule);
assert.deepEqual(normalizePunchLayout(opaque).toolRef, opaque.toolRef);
assert.equal(normalizePunchLayout(opaque).positionList, "12 24");

console.log(`TubeDesignerPunchLayoutTest: ${cases} layout cases passed.`);
