// Semantic cases supplement the runner's descriptor type/range checks.
const excludedGroups = new Set(["profile_preset", "outer_profile", "horizontal_profile", "vertical_profile", "door_frame_profile", "door_leaf_profile", "door_horizontal_profile", "door_vertical_profile", "outer_process", "door_process", "infill_process", "outerFrame_fold_process", "doorFrame_fold_process", "doorLeafFrame_fold_process", "folded_post_process", "groove_library", "assembly"]);
const ignoredKeys = new Set(["assemblyPlanningMode", "productCode"]);
const noDoor = { accessDoorEnabled: false };
const side = { ...noDoor, faceType: "two" };
const five = { ...noDoor, faceType: "five" };
const leafRows = { doorHorizontalTopCenterOffset: 100, doorHorizontalBottomCenterOffset: 100 };
const smallOpening = { doorClearWidth: 300, doorClearHeight: 500, doorUOffset: 110, doorVOffset: 350, doorHorizontalTopCenterOffset: 40, doorHorizontalBottomCenterOffset: 40, doorVerticalLeftCenterOffset: 50, doorVerticalRightCenterOffset: 50 };
const collision = "范围|留距|相碰|重叠|根数|两倍|尺寸不足|不足以|越界|超出|无法容纳|有效|正数|太小|空间";
const frameCounts = { single: 4, two: 7, three: 10, five: 12 };

export function buildGeometryCases(descriptor) {
  const fields = descriptor.parameters.filter((p) => !excludedGroups.has(p.group) && !ignoredKeys.has(p.key));
  const cases = [];
  const add = (id, keys, parameters, extra = {}) => cases.push({ id: `geometry:${id}`, keys: Array.isArray(keys) ? keys : [keys], kind: "valid", parameters, notes: "适用条件下的结构及排杆测试", ...extra });
  const changes = {
    width: [noDoor, 1400], height: [noDoor, 2000], horizontalMaximumCenterSpacing: [noDoor, 350],
    firstHorizontalTopOffset: [noDoor, 250], lastHorizontalBottomOffset: [noDoor, 250],
    verticalLeftCenterOffset: [noDoor, 140], verticalRightCenterOffset: [noDoor, 140], verticalMaximumCenterSpacing: [noDoor, 90],
    sideHorizontalMaximumCenterSpacing: [side, 350], sideVerticalStartCenterOffset: [side, 120], sideVerticalEndCenterOffset: [side, 120], sideVerticalMaximumCenterSpacing: [side, 90],
    topBottomCrossbarFrontCenterOffset: [five, 150], topBottomCrossbarBackCenterOffset: [five, 150], topBottomCrossbarMaximumCenterSpacing: [five, 100],
    topBottomRodLeftCenterOffset: [five, 140], topBottomRodRightCenterOffset: [five, 140], topBottomRodMaximumCenterSpacing: [five, 90],
    doorClearWidth: [{}, 700], doorClearHeight: [{}, 1100], doorLeft: [{}, 180], doorBottom: [{}, 400], doorGap: [leafRows, 8],
    doorHorizontalTopCenterOffset: [{}, 430], doorHorizontalBottomCenterOffset: [{}, 430], doorHorizontalMaximumCenterSpacing: [leafRows, 250],
    doorVerticalLeftCenterOffset: [{}, 100], doorVerticalRightCenterOffset: [{}, 100], doorVerticalMaximumCenterSpacing: [{}, 90],
    sideWidth: [side, 700], leftWidth: [{ ...noDoor, faceType: "three" }, 700], rightWidth: [{ ...noDoor, faceType: "three" }, 800], depth: [five, 800],
    doorUOffset: [{ faceType: "three" }, 180], doorVOffset: [{ faceType: "three" }, 400],
  };
  const enumContext = (key, value) => {
    if (key === "faceType") return { ...noDoor, faceType: value };
    if (key === "infillPattern") return { ...noDoor, infillPattern: value };
    if (key === "frameLayout") return { ...noDoor, frameLayout: value };
    if (key === "sidePosition") return { ...side, sidePosition: value };
    if (/^accessDoorFace[235]$/.test(key)) {
      const faceType = { 2: "two", 3: "three", 5: "five" }[key.slice(-1)];
      const context = value === "front" ? {} : { ...smallOpening };
      if (value === "bottom") Object.assign(context, { doorClearHeight: 200, doorVOffset: 120 });
      return { ...context, faceType, [key]: value };
    }
    throw new Error(`No semantic enum context for ${key}`);
  };
  for (const field of fields) {
    const key = field.key;
    if (field.valueType === "enum") for (const choice of field.choices) {
      const parameters = enumContext(key, choice.value);
      // Retain the exact same opening size, location and leaf-grid settings in
      // both runs; only the selected face changes. This keeps side/bottom inputs
      // and the front baseline legal without attributing a size change to a switch.
      const compareTo = { ...parameters, [key]: field.defaultValue };
      const extra = { compareTo, effect: choice.value === field.defaultValue ? "none" : "geometry" };
      if (key === "faceType") extra.expectedFrames = frameCounts[choice.value];
      if (key === "frameLayout") extra.expectedFrames = choice.value === "four_sides" ? 4 : 2;
      add(`${key}:${choice.value}`, key, parameters, extra);
    }
    else if (field.valueType === "boolean") for (const value of [false, true])
      add(`${key}:${value}`, key, { [key]: value }, { compareTo: { [key]: !value }, effect: "geometry", expectedFrames: 4 });
    else {
      if (!changes[key]) throw new Error(`No semantic numeric variation for ${key}`);
      const [context, value] = changes[key];
      add(`${key}:changed`, key, { ...context, [key]: value }, { compareTo: context, effect: "geometry" });
    }
  }

  // A scalar bound can be valid while its surrounding geometry does not fit.
  // Enlarge the other dimensions where possible; otherwise require a clear rejection.
  for (const field of fields.filter((p) => p.valueType === "number")) {
    const key = field.key; const [context] = changes[key];
    for (const endpoint of ["minimum", "maximum"]) {
      const value = field.constraints?.[endpoint]; if (value == null) continue;
      let parameters = { ...context, [key]: value }, kind = "rejection";
      if (endpoint === "maximum") {
        if (key === "width") { parameters = { ...noDoor, width: value, verticalMaximumCenterSpacing: 1000 }; kind = "valid"; }
        else if (key === "height") { parameters = { ...noDoor, height: value, horizontalMaximumCenterSpacing: 2000 }; kind = "valid"; }
        else if (["sideWidth", "leftWidth", "rightWidth"].includes(key)) { parameters = { ...context, [key]: value, sideVerticalMaximumCenterSpacing: 2000 }; kind = "valid"; }
        else if (key === "depth") { parameters = { ...five, depth: value, sideVerticalMaximumCenterSpacing: 2000, topBottomCrossbarMaximumCenterSpacing: 2000 }; kind = "valid"; }
        else if (key.includes("MaximumCenterSpacing")) kind = "valid";
        else if (["verticalLeftCenterOffset", "verticalRightCenterOffset"].includes(key)) { parameters = { ...context, width: 5000, [key]: value }; kind = "valid"; }
        else if (["sideVerticalStartCenterOffset", "sideVerticalEndCenterOffset"].includes(key)) { parameters = { ...side, sideWidth: 5000, [key]: value }; kind = "valid"; }
        else if (["topBottomCrossbarFrontCenterOffset", "topBottomCrossbarBackCenterOffset"].includes(key)) { parameters = { ...five, depth: 5000, [key]: value }; kind = "valid"; }
        else if (["topBottomRodLeftCenterOffset", "topBottomRodRightCenterOffset"].includes(key)) { parameters = { ...five, width: 5000, [key]: value }; kind = "valid"; }
        else if (["doorHorizontalTopCenterOffset", "doorHorizontalBottomCenterOffset"].includes(key)) { parameters = { height: 12000, doorClearHeight: 11000, [key]: value }; kind = "valid"; }
        else if (["doorVerticalLeftCenterOffset", "doorVerticalRightCenterOffset"].includes(key)) { parameters = { width: 14000, doorClearWidth: 11000, verticalMaximumCenterSpacing: 500, [key]: value }; kind = "valid"; }
      } else {
        if (key === "doorGap") kind = "valid";
        else if (["horizontalMaximumCenterSpacing", "sideHorizontalMaximumCenterSpacing"].includes(key)) { parameters = { ...context, height: 600, infillPattern: "horizontal", horizontalWidth: 16, horizontalDepth: 16, verticalWidth: 12, verticalDepth: 12, [key]: value }; kind = "valid"; }
        else if (key === "topBottomCrossbarMaximumCenterSpacing") { parameters = { ...five, horizontalWidth: 16, horizontalDepth: 16, verticalWidth: 12, verticalDepth: 12, [key]: value }; kind = "valid"; }
        else if (["verticalMaximumCenterSpacing", "sideVerticalMaximumCenterSpacing", "topBottomRodMaximumCenterSpacing"].includes(key)) kind = "valid";
        else if (key === "doorHorizontalMaximumCenterSpacing") { parameters = { [key]: value }; kind = "valid"; }
        else if (["topBottomCrossbarFrontCenterOffset", "topBottomCrossbarBackCenterOffset"].includes(key)) kind = "valid";
      }
      add(`${key}:${endpoint}`, key, parameters, { kind, notes: kind === "valid" ? "调整相关尺寸/间距，使标量边界在实体上可用" : "标量边界合法，但默认组合无法容纳实体，应给出几何拒绝", ...(kind === "rejection" ? { reasonPattern: collision } : {}) });
    }
  }

  for (const faceType of ["single", "two", "three", "five"]) for (const infillPattern of ["vertical", "horizontal", "grid"]) for (const accessDoorEnabled of [false, true])
    add(`matrix:${faceType}:${infillPattern}:${accessDoorEnabled}`, ["faceType", "infillPattern", "accessDoorEnabled"], { faceType, infillPattern, accessDoorEnabled }, {
      compareTo: { ...noDoor, faceType: "single", infillPattern: "grid" },
      effect: faceType === "single" && infillPattern === "grid" && !accessDoorEnabled ? "none" : "geometry",
      expectedFrames: frameCounts[faceType],
    });
  add("opening:shrink-with-stale-offsets", "doorClearHeight", { doorClearHeight: 900 }, { kind: "rejection", reasonPattern: "窗内横杆.*留距|留距.*可用范围|两端.*范围", notes: "开启口缩小但仍保留旧窗内留距，应拒绝" });
  add("opening:shrink-recomputed", ["doorClearWidth", "doorClearHeight"], { doorClearWidth: 700, doorClearHeight: 900, doorHorizontalTopCenterOffset: 400, doorHorizontalBottomCenterOffset: 400 });
  for (const [key, faceType, value] of [["accessDoorFace2", "two", "side"], ["accessDoorFace3", "three", "left"], ["accessDoorFace5", "five", "bottom"]])
    add(`opening:${key}:default-outside`, key, { faceType, [key]: value }, { kind: "rejection", reasonPattern: "逃生窗.*超出|逃生窗.*范围|开启.*范围", notes: "默认 800×1000 开启口不能直接放入 600 深度侧面/底面" });
  add("grid:zero-span-one-column", "verticalLeftCenterOffset", { ...noDoor, verticalLeftCenterOffset: 562, verticalRightCenterOffset: 562 });
  add("grid:zero-span-one-row", "firstHorizontalTopOffset", { ...noDoor, firstHorizontalTopOffset: 900, lastHorizontalBottomOffset: 900 });
  add("grid:overlap", "horizontalMaximumCenterSpacing", { ...noDoor, horizontalMaximumCenterSpacing: 20 }, { kind: "rejection", reasonPattern: "相碰|重叠", notes: "默认 22 mm 横管不可能保持 20 mm 中心距" });
  add("grid:over-100", "verticalMaximumCenterSpacing", { ...noDoor, width: 5000, verticalMaximumCenterSpacing: 20 }, { kind: "rejection", reasonPattern: "100|根数|数量", notes: "最多 100 根的实体规模限制" });
  for (const [key, faceType] of [["sideWidth", "two"], ["leftWidth", "three"], ["rightWidth", "three"]])
    add(`${key}:minimum-fitted`, key, { ...noDoor, faceType, [key]: 150, sideVerticalStartCenterOffset: 37, sideVerticalEndCenterOffset: 37 });
  add("depth:minimum-fitted", "depth", { ...five, depth: 150, sideVerticalStartCenterOffset: 37, sideVerticalEndCenterOffset: 37, topBottomCrossbarFrontCenterOffset: 18, topBottomCrossbarBackCenterOffset: 18 });

  const inactive = (key, context, value) => add(`${key}:inactive`, key, { ...context, [key]: value }, { kind: "inactive", compareTo: context, effect: "none", notes: "条件隐藏的合法草稿不应改变几何或阻断生成" });
  for (const key of ["horizontalMaximumCenterSpacing", "firstHorizontalTopOffset", "lastHorizontalBottomOffset"]) inactive(key, { ...noDoor, infillPattern: "vertical" }, key.includes("Spacing") ? 20 : 0);
  for (const key of ["verticalLeftCenterOffset", "verticalRightCenterOffset", "verticalMaximumCenterSpacing"]) inactive(key, { ...noDoor, infillPattern: "horizontal" }, key.includes("Spacing") ? 20 : 0);
  inactive("sideHorizontalMaximumCenterSpacing", { ...side, infillPattern: "vertical" }, 20);
  for (const key of ["sideVerticalStartCenterOffset", "sideVerticalEndCenterOffset", "sideVerticalMaximumCenterSpacing"]) inactive(key, { ...side, infillPattern: "horizontal" }, key.includes("Spacing") ? 20 : 0);
  for (const key of ["sideHorizontalMaximumCenterSpacing", "sideVerticalStartCenterOffset", "sideVerticalEndCenterOffset", "sideVerticalMaximumCenterSpacing", "topBottomCrossbarFrontCenterOffset", "topBottomCrossbarBackCenterOffset", "topBottomCrossbarMaximumCenterSpacing", "topBottomRodLeftCenterOffset", "topBottomRodRightCenterOffset", "topBottomRodMaximumCenterSpacing", "sideWidth", "leftWidth", "rightWidth", "depth", "doorUOffset", "doorVOffset"])
    inactive(key, noDoor, key.includes("Spacing") ? 20 : key.endsWith("Width") || key === "depth" ? 20000 : 0);
  for (const key of ["doorClearWidth", "doorClearHeight", "doorLeft", "doorBottom", "doorGap", "doorHorizontalTopCenterOffset", "doorHorizontalBottomCenterOffset", "doorHorizontalMaximumCenterSpacing", "doorVerticalLeftCenterOffset", "doorVerticalRightCenterOffset", "doorVerticalMaximumCenterSpacing"])
    inactive(key, noDoor, key.includes("Spacing") || key.includes("Clear") ? 1 : 0);
  inactive("frameLayout", { ...noDoor, faceType: "three" }, "left_right"); inactive("sidePosition", noDoor, "left");
  for (const [key, value] of [["accessDoorFace2", "side"], ["accessDoorFace3", "left"], ["accessDoorFace5", "bottom"]]) inactive(key, noDoor, value);
  inactive("doorLeft", { faceType: "three" }, 0); inactive("doorBottom", { faceType: "three" }, 0);
  for (const faceType of ["single", "two", "three", "five"]) {
    for (const key of ["doorHorizontalTopCenterOffset", "doorHorizontalBottomCenterOffset", "doorHorizontalMaximumCenterSpacing"]) inactive(key, { faceType, infillPattern: "vertical" }, key.includes("Spacing") ? 1 : 0);
    for (const key of ["doorVerticalLeftCenterOffset", "doorVerticalRightCenterOffset", "doorVerticalMaximumCenterSpacing"]) inactive(key, { faceType, infillPattern: "horizontal" }, key.includes("Spacing") ? 1 : 0);
  }
  const seen = new Map();
  for (const c of cases) { const count = (seen.get(c.id) ?? 0) + 1; seen.set(c.id, count); if (count > 1) c.id += `:${count}`; }
  return cases;
}
