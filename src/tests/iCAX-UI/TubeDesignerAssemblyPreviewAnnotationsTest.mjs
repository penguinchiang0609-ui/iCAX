import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { buildAssemblyPreviewDimensionAnnotations, buildAssemblyPreviewValueNotes } from "../../apps/tube-designer/webpage/assemblyPreviewAnnotations.mjs";

const alongZ = [0, 0, -1, 10, 0, 1, 0, 2, 1, 0, 0, 3, 0, 0, 0, 1];
const translated = (x, y, z) => [1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1];

const plan = {
  templateId: "unseen-template-from-catalogue",
  resolvedWorkflow: { realization: "separate" },
  designParts: [
    { id: "design-a", role: "memberA", label: "构件 A", request: {
      length: 180, parameters: { width: 40, depth: 40 },
    }, matrix: alongZ },
    { id: "design-b", role: "memberB", label: "构件 B", request: {
      length: 260, parameters: { width: 50, depth: 30 },
    }, matrix: translated(40, 0, 0) },
  ],
  manufacturingParts: [
    { id: "manufacturing-a", blankId: "blankA", label: "A 端部下料", request: {
      length: 180, parameters: { width: 40, depth: 40 },
      ends: { end: { type: "end-miter", trim: 25 } },
    }, explodedMatrix: translated(-200, 0, 0) },
    { id: "manufacturing-b", blankId: "blankB", label: "B 侧槽下料", request: {
      length: 260, parameters: { width: 50, depth: 30 },
      features: [{ toolRef: { id: "paired-side-slots" }, station: 30 }],
    }, explodedMatrix: translated(100, 0, 0) },
  ],
};
const preview = { plan };
const finishedBounds = { min: [-280, -30, -20], max: [250, 30, 20] };
const assertFinishedLabels = (dimensions) => {
  assert.ok(dimensions.every((item) => item.placement === "outside"));
  assert.ok(dimensions.every((item) => !/逻辑段设定长|下料基准长|设定长|下料|展开|计算半径|K=/.test(item.label)),
    "成品视图只显示成品尺寸，不显示逻辑段或加工尺寸");
};

const finished = buildAssemblyPreviewDimensionAnnotations(preview, "finished", finishedBounds);
assert.equal(finished.length, 1, "直线成品只标实际外廓总长，忽略两段设计长度");
assert.equal(finished[0].label, "成品总长 530 mm");
assert.deepEqual([finished[0].start[0], finished[0].end[0]], [-280, 250]);
assertFinishedLabels(finished);

const blanks = buildAssemblyPreviewDimensionAnnotations(preview, "exploded");
assert.equal(blanks.length, 2);
assert.deepEqual(blanks[0].start, [-200, 0, 0]);
assert.deepEqual(blanks[0].end, [-20, 0, 0]);
assert.match(blanks[0].label, /下料基准长.*180 mm/);
assert.doesNotMatch(blanks[0].label, /切后|成品|实测/,
  "the request's stock length must not masquerade as a cut-part measurement");
assert.ok(blanks.every((item) => item.label.includes("下料基准长")));

const tabSlot = { plan: { ...plan, templateId: "tab-slot-lock", designParts: [
  { ...plan.designParts[0], role: "tab", label: "插舌件",
    request: { ...plan.designParts[0].request, length: 280 }, matrix: translated(-280, 0, 0) },
  { ...plan.designParts[1], role: "slot", label: "插槽件",
    request: { ...plan.designParts[1].request, length: 280 }, matrix: translated(-30, 0, 0) },
] } };
const tabSlotFinished = buildAssemblyPreviewDimensionAnnotations(tabSlot, "finished", finishedBounds);
assert.equal(tabSlotFinished.length, 1, "插舌 / 插槽装配成品只标一个实际总长");
assert.equal(tabSlotFinished[0].label, "成品总长 530 mm", "两根 280 mm 管重叠 30 mm 后成品总长为 530 mm");
assert.deepEqual([tabSlotFinished[0].start[0], tabSlotFinished[0].end[0]], [-280, 250]);
assertFinishedLabels(tabSlotFinished);

const declared = { ...plan, previewAnnotations: [
  { id: "role-a", kind: "length-role", view: "finished", label: "构件 A 轴长", role: "memberA" },
  { id: "blank-b", kind: "length-blank", view: "blank", label: "B 下料长度", blankId: "blankB" },
  { id: "gap", kind: "value-note", view: "finished", label: "装配间隙", value: 0.5, unit: "mm" },
  { id: "hole", kind: "value-note", view: "blank", label: "孔径", value: 12, unit: "mm" },
  { id: "unresolved", kind: "length-blank", view: "blank", label: "不存在的管", blankId: "missing" },
] };
const annotatedFinished = buildAssemblyPreviewDimensionAnnotations({ plan: declared }, "finished", finishedBounds);
assert.deepEqual(annotatedFinished, finished,
  "成品外廓不能被模板声明的逻辑角色长度改写");
assert.equal(buildAssemblyPreviewValueNotes({ plan: declared }, "finished")[0].value, 0.5);
const annotatedBlanks = buildAssemblyPreviewDimensionAnnotations({ plan: declared }, "exploded");
assert.equal(annotatedBlanks.length, 2, "declared blank length replaces automatic length");
assert.deepEqual(annotatedBlanks[1].start, [100, 0, 0]);
assert.deepEqual(annotatedBlanks[1].end, [360, 0, 0]);
assert.match(annotatedBlanks[1].label, /B 下料长度.*下料基准长.*260 mm/);
assert.deepEqual(buildAssemblyPreviewValueNotes({ plan: declared }, "blank"), [
  { id: "hole", label: "孔径", value: 12, unit: "mm" },
]);
assert.ok(!annotatedBlanks.some((item) => item.label.includes("孔径")),
  "a value-note without feature anchors must not become a measurement line");

const formed = {
  plan: { ...plan, resolvedWorkflow: { realization: "integrated" } },
  formedPreview: { rows: [{ entityId: "formed" }], mesh: { metadata: {
    angle: 90, centerlineCorner: [4, 0, 0], section: { width: 40, lengthA: 180, lengthB: 260 },
  } } },
};
const formedBounds = { min: [-176, -20, -20], max: [4, 20, 260] };
const formedDimensions = buildAssemblyPreviewDimensionAnnotations(formed, "finished", formedBounds);
assert.equal(formedDimensions.length, 2);
assert.ok(formedDimensions.some((item) => item.label.includes("180 mm")));
assert.ok(formedDimensions.some((item) => item.label.includes("280 mm")));
assertFinishedLabels(formedDimensions);
const declaredFormed = buildAssemblyPreviewDimensionAnnotations({ ...formed,
  plan: { ...formed.plan, previewAnnotations: [{ id: "formed-a", kind: "length-role",
    view: "finished", role: "memberA", label: "首段" }] },
}, "finished", formedBounds);
assert.deepEqual(declaredFormed, formedDimensions,
  "成品标注不能被声明的逻辑段长度替换");
assert.deepEqual(buildAssemblyPreviewDimensionAnnotations({ ...formed,
  formedPreview: { rows: [{ entityId: "formed" }], mesh: { metadata: {} } },
}, "finished", formedBounds), formedDimensions,
"成品实物边界已知时不依赖工艺成形锚点");

const segmented = { plan: { ...plan, resolvedWorkflow: { realization: "integrated" },
  previewAnnotations: [{ id: "segment-a", kind: "length-role", view: "finished",
    role: "memberA", label: "首段设定长" }] },
formedPreview: { rows: [{ entityId: "formed" }], annotations: {
  kind: "segmented-bend-target", values: { pitch: 20, angle: 90, segmentCount: 5 },
  anchors: { straightStart: [-185, 0, 0], firstUncutEdge: [-5, 0, 0],
    firstHinge: [0, 0, 0], secondHinge: [20, 0, 0],
    lastHinge: [60, 0, 60], lastUncutEdge: [60, 0, 65],
    straightEnd: [60, 0, 325], planeNormal: [0, 1, 0] },
} } };
const segmentedBounds = { min: [-185, -20, 0], max: [60, 20, 325] };
const segmentedDimensions = buildAssemblyPreviewDimensionAnnotations(segmented, "finished", segmentedBounds);
assert.equal(segmentedDimensions.length, 2, "成品图标两条实际外廓大跨距尺寸");
assert.ok(segmentedDimensions.some((item) => item.label.includes("245 mm")));
assert.ok(segmentedDimensions.some((item) => item.label.includes("325 mm")));
assertFinishedLabels(segmentedDimensions);
assert.deepEqual(buildAssemblyPreviewDimensionAnnotations({ ...segmented,
  formedPreview: { rows: [{ entityId: "formed" }], annotations: {
    ...segmented.formedPreview.annotations, anchors: { ...segmented.formedPreview.annotations.anchors,
      secondHinge: undefined },
  } },
}, "finished", segmentedBounds), segmentedDimensions,
"成品标注不应依赖工艺槽距锚点");

const flexible = { plan: { ...plan, resolvedWorkflow: { realization: "integrated" },
  previewAnnotations: [{ id: "segment-a", kind: "length-role", view: "finished",
    role: "memberA", label: "首段设定长" }] },
formedPreview: { rows: [{ entityId: "formed" }], annotations: {
  kind: "flexible-slit-target", values: { angle: 90, bendRadius: 40,
    slitCount: 5, slitPitch: 9, flexibleLength: 40 * Math.PI / 2 },
  anchors: { straightStart: [-180, 0, 0], curveStart: [0, 0, 0],
    slitCenters: [[2, 0, 0], [10, 0, 4], [17, 0, 9], [22, 0, 15], [26, 0, 22]],
    curveEnd: [40, 0, 40], straightEnd: [40, 0, 300], planeNormal: [0, 1, 0] },
} } };
const flexibleBounds = { min: [-180, -20, 0], max: [40, 20, 300] };
const flexibleDimensions = buildAssemblyPreviewDimensionAnnotations(flexible, "finished", flexibleBounds);
assert.equal(flexibleDimensions.length, 2);
assert.ok(flexibleDimensions.some((item) => item.label.includes("220 mm")));
assert.ok(flexibleDimensions.some((item) => item.label.includes("300 mm")));
assertFinishedLabels(flexibleDimensions);
assert.deepEqual(buildAssemblyPreviewDimensionAnnotations({ ...flexible,
  formedPreview: { rows: [{ entityId: "formed" }], annotations: {
    ...flexible.formedPreview.annotations, anchors: { ...flexible.formedPreview.annotations.anchors,
      curveStart: undefined },
  } },
}, "finished", flexibleBounds), flexibleDimensions,
"成品实物边界已知时不依赖柔性槽工艺锚点");

const cold = {
  plan: {
    ...plan, templateId: "bend", parameters: { bendMethod: "cold", angle: 90,
      bendRadius: 40, bendFactor: 0.5 },
    previewAnnotations: [
      { id: "blank-length", kind: "length-blank", view: "blank",
        blankId: "blankA", label: "总下料长度" },
      { id: "factor", kind: "value-note", view: "blank", label: "折弯因子 K", value: 0.5 },
    ],
    resolvedWorkflow: { realization: "integrated" },
    manufacturingParts: [{ ...plan.manufacturingParts[0], request: {
      ...plan.manufacturingParts[0].request, length: 584.4,
    } }],
  },
  formedPreview: { rows: [{ entityId: "formed" }], annotations: {
    kind: "cold-bend-development",
    values: { angle: 90, R: 40, t: 2, K: 0.5, calculationRadius: 41,
      arcLength: 41 * Math.PI / 2, straightA: 180, straightB: 260, blankLength: 584.4 },
    anchors: {
      bendCenter: [0, 0, 41], arcStart: [0, 0, 0], arcMid: [29, 0, 12],
      arcEnd: [41, 0, 41], straightStart: [-180, 0, 0], straightEnd: [41, 0, 301],
      planeNormal: [0, 1, 0], radialUp: [0, 0, 1],
      wallOuter: [-180, 0, 20], wallInner: [-180, 0, 18],
    },
  } },
};
const coldBounds = { min: [-180, -20, 0], max: [41, 20, 301] };
const coldFormed = buildAssemblyPreviewDimensionAnnotations(cold, "finished", coldBounds);
assert.equal(coldFormed.length, 3, "冷折成品保留转角及两条实际外廓尺寸");
assert.ok(coldFormed.some((item) => /转角.*90°/.test(item.label)));
assert.ok(coldFormed.some((item) => item.label.includes("221 mm")));
assert.ok(coldFormed.some((item) => item.label.includes("301 mm")));
assertFinishedLabels(coldFormed);
const coldBlank = buildAssemblyPreviewDimensionAnnotations(cold, "exploded");
assert.equal(coldBlank.length, 5, "declared total blank replaces the specialized total-blank line");
assert.ok(coldBlank.some((item) => item.label.includes("K=0.5")));
assert.equal(coldBlank.filter((item) => item.label.includes("总下料长度")).length, 1);
assert.ok(coldBlank.some((item) => item.label.includes("总下料长度（下料基准长） 584.4 mm")));
assert.deepEqual(buildAssemblyPreviewValueNotes(cold, "exploded"), [
  { id: "factor", label: "折弯因子 K", value: 0.5, unit: "" },
]);

console.log(`Assembly preview annotations: ${fileURLToPath(import.meta.url)} passed`);
