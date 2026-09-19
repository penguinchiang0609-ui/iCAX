// A connection template describes how several parts are prepared and assembled.
// It references single-part moulds; it never copies their geometry definition.
export const connectionProcessCatalog = Object.freeze([
  connection({
    id: "tab-slot-lock",
    displayName: "插舌 / 插槽连接",
    category: "insert",
    categoryName: "插接与卡接",
    summary: "两件分别加工插舌与插槽，沿连接轴插入后锁止。",
    topology: "端部—端部",
    interfaceType: "插舌 / 插槽",
    lockType: "结构锁止",
    motionType: "直线插入",
    participants: [participant("tab", "插舌件", "主动件"), participant("slot", "插槽件", "承接件")],
    parameters: [
      numberParameter("nominalWidth", "连接名义宽度", 20, 1, 200, "mm"),
      numberParameter("insertDepth", "插入深度", 25, 1, 300, "mm"),
      numberParameter("fitClearance", "配合间隙", 0.2, 0, 5, "mm", "advanced"),
      choiceParameter("lockMethod", "锁止方式", "self", [
        ["self", "结构自锁"], ["weld", "点焊锁止"], ["bolt", "螺栓锁止"],
      ], "advanced"),
    ],
    operations: [
      operation("tab-end", "tab", "插舌端部成形", "end-key-joint", { width: "$nominalWidth", depth: "$insertDepth" }),
      operation("slot-end", "slot", "插槽端部成形", "end-key-joint", { width: "$nominalWidth", depth: "$insertDepth", clearance: "$fitClearance" }),
    ],
    assemblyPath: [assemblyStep("align", "对正插舌与插槽", "对正", "joint.z", 0), assemblyStep("insert", "沿连接轴插入", "直线", "joint.z", "$insertDepth"), assemblyStep("lock", "执行锁止", "$lockMethod", "joint.z", 0)],
    outputs: output(["插舌件端部", "插槽件端部"], ["装配定位", "锁止"], ["锁止件（按锁止方式生成）"]),
    illustration: "tab-slot",
  }),
  connection({
    id: "through-bolt",
    displayName: "对穿螺栓连接",
    category: "fastener",
    categoryName: "紧固件连接",
    summary: "两件同轴冲孔，以螺栓、螺母和垫片完成可拆连接。",
    topology: "面—面",
    interfaceType: "同轴孔",
    lockType: "螺栓副",
    motionType: "法向贴合",
    participants: [participant("partA", "零件 A", "基准件"), participant("partB", "零件 B", "连接件")],
    parameters: [
      numberParameter("boltDiameter", "螺栓公称直径", 10, 2, 48, "mm"),
      numberParameter("holeClearance", "孔径间隙", 1, 0, 6, "mm"),
      numberParameter("boltCount", "螺栓数量", 2, 1, 20, "个"),
      numberParameter("pitch", "孔距", 50, 5, 1000, "mm"),
      choiceParameter("washer", "垫片配置", "both", [["none", "无"], ["nut", "螺母侧"], ["both", "两侧"]], "advanced"),
    ],
    operations: [
      operation("hole-a", "partA", "A 件连接孔", "circle", { diameter: "$boltDiameter + $holeClearance", count: "$boltCount", pitch: "$pitch" }),
      operation("hole-b", "partB", "B 件连接孔", "circle", { diameter: "$boltDiameter + $holeClearance", count: "$boltCount", pitch: "$pitch" }),
    ],
    assemblyPath: [assemblyStep("align", "对正两组孔", "对正", "joint.z", 0), assemblyStep("bolt", "穿入螺栓并紧固", "紧固", "joint.z", 0)],
    outputs: output(["A 件孔组", "B 件孔组"], ["螺栓紧固"], ["螺栓", "螺母", "垫片"]),
    illustration: "bolt",
  }),
  connection({
    id: "slot-bolt-adjustable",
    displayName: "长孔调节连接",
    category: "fastener",
    categoryName: "紧固件连接",
    summary: "一件加工长孔、一件加工圆孔，保留装配调节量后再锁紧。",
    topology: "面—面",
    interfaceType: "长孔 / 圆孔",
    lockType: "螺栓副",
    motionType: "单轴可调",
    participants: [participant("adjuster", "调节件", "长孔件"), participant("datum", "基准件", "圆孔件")],
    parameters: [
      numberParameter("boltDiameter", "螺栓公称直径", 10, 2, 48, "mm"),
      numberParameter("adjustment", "单向调节量", 12, 1, 200, "mm"),
      numberParameter("holeClearance", "孔径间隙", 1, 0, 6, "mm", "advanced"),
      choiceParameter("adjustAxis", "调节方向", "x", [["x", "连接坐标 X"], ["y", "连接坐标 Y"]], "advanced"),
      booleanParameter("retainPosition", "锁紧后保留装配位置", true, "advanced"),
    ],
    operations: [
      operation("slot-a", "adjuster", "调节件长孔", "slot", { width: "$boltDiameter + $holeClearance", length: "$boltDiameter + $holeClearance + $adjustment" }),
      operation("hole-b", "datum", "基准件圆孔", "circle", { diameter: "$boltDiameter + $holeClearance" }),
    ],
    assemblyPath: [assemblyStep("align", "圆孔进入长孔调节范围", "对正", "joint.z", 0), assemblyStep("adjust", "沿调节轴定位", "滑动", "$adjustAxis", "$adjustment"), assemblyStep("lock", "穿入螺栓并锁紧", "紧固", "joint.z", 0)],
    outputs: output(["调节件长孔", "基准件圆孔"], ["调节定位", "螺栓紧固"], ["螺栓", "螺母", "垫片"]),
    illustration: "slot-bolt",
  }),
  connection({
    id: "saddle-weld",
    displayName: "鞍口相贯焊接",
    category: "weld",
    categoryName: "焊接连接",
    summary: "支管端部加工鞍口，与主管贴合定位后施焊。",
    topology: "端部—曲面",
    interfaceType: "相贯贴合",
    lockType: "焊缝",
    motionType: "法向贴合",
    participants: [participant("branch", "支管", "鞍口件"), participant("main", "主管", "承接件")],
    parameters: [
      numberParameter("intersectionAngle", "相交角度", 90, 10, 170, "°"),
      numberParameter("fitGap", "装配间隙", 0.5, 0, 5, "mm"),
      numberParameter("weldLeg", "焊脚尺寸", 4, 1, 30, "mm", "advanced"),
      choiceParameter("weldScope", "焊接范围", "full", [["full", "连续满焊"], ["stitch", "间断焊"], ["tack", "定位点焊"]], "advanced"),
    ],
    operations: [
      operation("cope-branch", "branch", "支管端部鞍口", "end-cope", { angle: "$intersectionAngle", clearance: "$fitGap" }),
    ],
    assemblyPath: [assemblyStep("orient", "按相交角度建立支管姿态", "转动", "joint.y", "$intersectionAngle"), assemblyStep("fit", "支管鞍口贴合主管", "直线", "joint.z", "$fitGap"), assemblyStep("weld", "按焊接范围施焊", "$weldScope", "joint.z", 0)],
    outputs: output(["支管鞍口"], ["相贯定位", "焊接"], ["焊材（按工艺定额）"]),
    illustration: "saddle",
  }),
]);

export const connectionCategoryOrder = Object.freeze([
  ["insert", "插接与卡接"], ["fastener", "紧固件连接"], ["weld", "焊接连接"],
]);

export function connectionTemplateById(id) {
  return connectionProcessCatalog.find((item) => item.id === String(id ?? ""));
}

export function connectionParameterDefaults(template) {
  return Object.fromEntries((template?.parameters ?? []).map((item) => [item.key, item.defaultValue]));
}

function connection(value) {
  return Object.freeze({
    schema: "icax.connection-process",
    schemaVersion: 1,
    version: "1.0.0",
    jointFrame: { origin: "interface-center", x: "interface-horizontal", y: "interface-vertical", z: "assembly-forward" },
    geometryPolicy: { keepSkeleton: true, keepOuterEnvelope: false, keepPartLength: false },
    manufacturingStrategy: "prepare-parts-then-assemble",
    ...value,
  });
}
function participant(role, label, responsibility) { return { role, label, responsibility, count: 1 }; }
function operation(id, role, label, toolId, parameterBindings) { return { id, role, label, resource: { kind: "punch-tool", id: toolId, displayName: toolName(toolId) }, parameterBindings }; }
function assemblyStep(id, label, kind, direction, distance) { return { id, label, kind, direction, distance }; }
function output(partGeometry, secondaryProcess, bom) { return { partGeometry, secondaryProcess, bom, assemblyInstruction: true }; }
function numberParameter(key, displayName, defaultValue, min, max, unit, level = "basic") { return { key, displayName, valueType: "number", defaultValue, min, max, step: unit === "个" ? 1 : 0.1, unit, level }; }
function booleanParameter(key, displayName, defaultValue, level = "basic") { return { key, displayName, valueType: "boolean", defaultValue, level }; }
function choiceParameter(key, displayName, defaultValue, options, level = "basic") { return { key, displayName, valueType: "choice", defaultValue, options: options.map(([value, label]) => ({ value, label })), level }; }
function toolName(id) { return ({ "end-key-joint": "端部插舌 / 插槽", circle: "圆孔", slot: "长孔", "end-cope": "端部鞍口" })[id] ?? id; }
