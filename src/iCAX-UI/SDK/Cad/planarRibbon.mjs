import { polylineSegmentKinds } from "./planarCurveSegments.mjs";

const primitive = (key, label, icon, alternatives = []) => Object.freeze({ key, label, icon, alternatives: Object.freeze(alternatives) });
export const planarDrawingToolGroups = Object.freeze([
  Object.freeze({ id: "lines", label: "线条", tools: Object.freeze([
    primitive("line", "直线", "sketch-line"), primitive("polyline", "多段线", "sketch-polyline"),
  ]) }),
  Object.freeze({ id: "curves", label: "曲线", tools: Object.freeze([
    primitive("arc", "圆弧", "sketch-arc", [primitive("ellipseArc", "椭圆弧", "sketch-ellipse-arc"),
      primitive("bezier", "贝塞尔", "sketch-bezier"), primitive("bspline", "B样条", "sketch-bspline")]),
  ]) }),
  Object.freeze({ id: "shapes", label: "形状", tools: Object.freeze([
    primitive("rect", "矩形", "sketch-rectangle", [primitive("triangle", "三角形", "sketch-triangle"),
      primitive("polygon", "正多边形", "sketch-polygon"), primitive("star", "星形", "sketch-star")]),
    primitive("circle", "圆", "sketch-circle", [primitive("ellipse", "椭圆", "sketch-ellipse")]),
  ]) }),
]);
const transforms = Object.freeze([["move", "移动", "move"], ["copy", "复制", "copy"], ["rotate", "旋转", "rotate"], ["mirror", "镜像", "edit2d"], ["scale", "缩放", "scale"]]);
const modifications = Object.freeze([["nodes", "节点编辑", "edit2d"], ["close", "闭合轮廓", "sketch-polyline"], ["delete", "删除", "delete"], ["trim", "修剪", "cut"], ["extend", "延伸", "sketch-line"], ["join", "连接", "group"], ["break", "打断", "ungroup"], ["offset", "偏移", "array"], ["fillet", "圆角", "sketch-arc"], ["chamfer", "倒角", "sketch-polyline"]]);

export function createPlanarRibbonGroups({ command, tool, ids, availablePrimitives, availableEdits, snapEnabled, orthoEnabled, gridVisible, includeLayers = true, polylineActive = false, availableSegmentTypes = [], activeSegmentType = "line" }) {
  const group = (title, commands) => ({ title, commands });
  const primitiveAllowed = key => !availablePrimitives || availablePrimitives.includes(key);
  const editAllowed = key => availableEdits?.includes(key);
  const drawGroups = planarDrawingToolGroups.map(definition => group(definition.label, definition.tools.flatMap(primary => {
    const choices = [primary, ...primary.alternatives].filter(item => primitiveAllowed(item.key));
    if (!choices.length) return [];
    const [current, ...alternatives] = choices;
    return [tool(ids.draw(current.key), current.label, current.icon, { menuItems: alternatives.map(item => tool(ids.draw(item.key), item.label, item.icon)) })];
  }))).filter(definition => definition.commands.length);
  const transformRows = transforms.filter(([key]) => editAllowed(key)), primaryTransform = transformRows[0];
  const transformButton = primaryTransform && tool(ids.edit(primaryTransform[0]), primaryTransform[1], primaryTransform[2], { menuItems: transformRows.slice(1).map(([key, label, icon]) => tool(ids.edit(key), label, icon)) });
  const arrays = [["array-rectangular", "矩形阵列", "array"], ["array-polar", "圆周阵列", "array"]].filter(([key]) => editAllowed(key));
  const arrayButton = arrays[0] && tool(ids.edit(arrays[0][0]), "阵列", "array", { menuItems: arrays.slice(1).map(([key, label, icon]) => tool(ids.edit(key), label, icon)) });
  const segmentTypes = polylineActive && ids.segment ? polylineSegmentKinds.filter(([key]) => availableSegmentTypes.includes(key)) : [];
  const currentSegment = segmentTypes.find(([key]) => key === activeSegmentType) ?? segmentTypes[0];
  const segmentCommand = ([key, label, shortcut]) => command(ids.segment(key), `${label}（${shortcut}）`, ({ line: "sketch-line", arc: "sketch-arc", ellipseArc: "sketch-ellipse-arc", bezier: "sketch-bezier", bspline: "sketch-bspline" })[key] ?? "sketch-arc", { active: activeSegmentType === key });
  return [
    ...drawGroups,
    ...(segmentTypes.length > 1 ? [group("当前曲段", [{ ...segmentCommand(currentSegment), menuItems: segmentTypes.filter(row => row !== currentSegment).map(segmentCommand) }])] : []),
    group("修改", [tool(ids.select, "选择", "select", { menuItems: modifications.filter(([key]) => editAllowed(key)).map(([key, label, icon]) => command(ids.edit(key), label, icon)) })]),
    ...([transformButton, arrayButton].some(Boolean) ? [group("变换与阵列", [transformButton, arrayButton].filter(Boolean))] : []),
    ...(includeLayers ? [group("图层", [command(ids.layers, "图层", "group")])] : []),
    group("辅助与视图", [command(ids.snap, "对象吸附", "locate-point", { active: snapEnabled !== false, menuItems: [command(ids.ortho, "正交", "locate-line", { active: orthoEnabled === true }), command(ids.grid, "网格", "array", { active: gridVisible !== false })] }), command(ids.fit, "适合窗口", "view-fit", { menuItems: [command(ids.resetLayout, "重置布局", "display")] })]),
  ];
}
