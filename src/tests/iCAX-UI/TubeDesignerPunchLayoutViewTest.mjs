import assert from "node:assert/strict";
import {
  punchLayoutStyles,
  renderPunchLayoutControls,
  renderPunchLayoutOverview,
  renderPunchRowLayoutControls,
} from "../../apps/tube-designer/webpage/punchLayoutView.mjs";

const action = suffix => "tube-designer-punch-" + suffix;
const base = {
  distributionMode: "pitch", reference: "start", station: 100, layoutDatum: "base",
  arrayCount: 5, arrayPitch: 100, headMargin: 100, tailMargin: 100,
  face: "top", rowCount: 1, rowPitch: 20, rowDistributionMode: "pitch",
  centerOffset: 0, centerMode: "hole", centerFirstOffset: null,
  fillAlign: "start", maxSpacing: 180,
  spacingSequence: "100, 150, 50*3", positionList: "100, 250, 750",
};
const render = (feature = {}, disabled = false, index = "draft") => renderPunchLayoutControls(action, { ...base, ...feature }, index, disabled, 1000);
const field = name => new RegExp('data-tube-designer-punch-field="' + name + '"');
const fields = html => [...html.matchAll(/data-tube-designer-punch-field="([^"]+)"/g)].map(match => match[1]);

const pitch = render();
for (const mode of ["pitch", "equal", "middle-fixed", "end-margins", "center-out", "fill", "max-spacing", "sequence", "positions"]) {
  assert.match(pitch, new RegExp('<option value="' + mode + '"'));
}
assert.match(pitch, /aria-label="沿主管长度阵列方式（X 轴）"/);
assert.match(pitch, /data-cam-change-action="tube-designer-punch-field-change"/);
assert.match(pitch, /data-tube-designer-punch-index="draft"/);
assert.match(pitch, /aria-label="孔数"/);
assert.match(pitch, /aria-label="孔间距"/);
assert.match(pitch, /沿主管拉伸轴 X/);
assert.match(pitch, /包含首孔和末孔/);
assert.match(pitch, /不是孔边净距/);
assert.match(pitch, /aria-label="布局计算结果"/);
assert.deepEqual(fields(pitch), ["distributionMode", "arrayCount", "arrayPitch", "skipInstancesText"]);

const marginHtml = render({ distributionMode: "end-margins" });
assert.match(marginHtml, /首孔中心端距/);
assert.match(marginHtml, /末孔中心端距/);
assert.deepEqual(fields(marginHtml), ["distributionMode", "arrayCount", "headMargin", "tailMargin", "skipInstancesText"]);
assert.match(marginHtml, /中心距 200 mm/);
assert.deepEqual(fields(render({ distributionMode: "equal" })), ["distributionMode", "arrayCount", "skipInstancesText"]);
assert.deepEqual(fields(render({ distributionMode: "middle-fixed" })), ["distributionMode", "arrayCount", "arrayPitch", "skipInstancesText"]);

const centerHole = render({ distributionMode: "center-out" });
assert.match(centerHole, /中心有孔（奇数）/);
assert.match(centerHole, /中心留空（偶数）/);
assert.match(centerHole, field("centerOffset"));
assert.doesNotMatch(centerHole, field("centerFirstOffset"));
const centerGap = render({ distributionMode: "center-out", centerMode: "gap", arrayCount: 6 });
assert.match(centerGap, field("centerFirstOffset"));
assert.match(centerGap, /aria-label="首对孔偏移" value="" min="0" placeholder="半步距"/);
const invalidParity = render({ distributionMode: "center-out", arrayCount: 4 });
assert.match(invalidParity, /role="alert"/);
assert.match(invalidParity, /奇数总孔数/);
assert.doesNotMatch(invalidParity, /<output/);

const filled = render({ distributionMode: "fill" });
assert.match(filled, field("fillAlign"));
assert.doesNotMatch(filled, field("arrayCount"));
assert.match(filled, /余量两端平分/);
const maximum = render({ distributionMode: "max-spacing" });
assert.match(maximum, field("maxSpacing"));
assert.doesNotMatch(maximum, field("arrayPitch"));
assert.doesNotMatch(maximum, field("arrayCount"));
assert.match(maximum, /中心距 160 mm/);

const sequence = render({ distributionMode: "sequence" });
assert.match(sequence, /<details class="tube-designer-punch-layout-details"><summary>编辑不等距序列/);
assert.match(sequence, /50\*3 表示连续 3 段 50 mm/);
assert.match(sequence, /相邻孔中心距序列/);
assert.match(sequence, /每排 6 孔/);
assert.doesNotMatch(sequence, field("arrayCount"));
assert.doesNotMatch(sequence, field("arrayPitch"));
const positions = render({ distributionMode: "positions" });
assert.match(positions, /距母材起点的孔中心位置/);
assert.match(positions, /可粘贴表格的一列/);
assert.match(render({ distributionMode: "positions", positionList: "" }), /<details class="tube-designer-punch-layout-details" open>/);
assert.match(render({ skipInstancesText: "3" }), /跳过指定孔 · 已设置/);
assert.match(render({ skipInstancesText: "3" }), /不移动其余孔/);

const disabled = render({ distributionMode: "sequence" }, true, 2);
for (const control of disabled.matchAll(/<(input|select|textarea)\b[^>]*>/g)) {
  assert.match(control[0], /aria-label="[^"]+"/);
  assert.match(control[0], / disabled/);
  assert.match(control[0], /data-tube-designer-punch-index="2"/);
}
for (const control of [...pitch.matchAll(/<(input|select|textarea)\b[^>]*>/g)]) assert.match(control[0], /aria-label="[^"]+"/);

const flatRows = renderPunchRowLayoutControls(action, { ...base, face: "top", rowCount: 3 }, 0, false, 1000);
assert.deepEqual(fields(flatRows), ["rowCount", "rowPitch"]);
assert.match(flatRows, /排间中心距/);
assert.doesNotMatch(flatRows, /整圈均分/);
const singlePartRow = renderPunchRowLayoutControls(action, { ...base, toolTarget: "part", rowCount: 1 }, 0, false, 1000);
assert.match(singlePartRow, /<summary>单排 · 横向 \/ 周向多排<\/summary>/);
assert.doesNotMatch(singlePartRow, /<details[^>]* open/);
assert.match(singlePartRow, /Y 向多排/);assert.match(singlePartRow, /Z 向多排/);assert.match(singlePartRow, /周向多排/);
assert.match(singlePartRow, /单个刀具方位在参数中设置/);
const fullCircle = renderPunchRowLayoutControls(action, { ...base, face: "round", rowCount: 4, rowDistributionMode: "full-circle", rowStartAngle: 45 }, 1, false, 1000);
assert.deepEqual(fields(fullCircle), ["rowDistributionMode", "rowCount", "rowStartAngle"]);
assert.match(fullCircle, /实际角距 90°/);
assert.match(fullCircle, /360° 位置不重复/);
assert.match(fullCircle, /相对起始角/);
assert.match(fullCircle, /角度相对刀具初始姿态/);
assert.match(fullCircle, /壁面刀具默认以 \+Y/);
assert.match(fullCircle, /不同来源的相同角值不代表相同绝对方向/);
const branchCircle = renderPunchRowLayoutControls(action, { ...base, toolTarget: "part", recordKind: "branch", face: "round", rowCount: 4, rowDistributionMode: "full-circle" }, 1, false, 1000);
assert.match(branchCircle, /支管 \/ DXF 默认从 \+Z/);
assert.match(branchCircle, /刀具周向参数/);
assert.doesNotMatch(branchCircle, /壁面刀具默认/);
const angleRange = renderPunchRowLayoutControls(action, { ...base, face: "round", rowCount: 3, rowDistributionMode: "angle-range", rowStartAngle: 30, rowEndAngle: 150 }, 1, false, 1000);
assert.deepEqual(fields(angleRange), ["rowDistributionMode", "rowCount", "rowStartAngle", "rowEndAngle"]);
assert.match(angleRange, /实际角距 60°/);
assert.match(angleRange, /相对结束角/);

const overview = renderPunchLayoutOverview({ ...base, distributionMode: "end-margins" }, 1000);
assert.match(overview, /role="img"/);
assert.match(overview, /母材中心 X=0/);
assert.match(overview, /全长 1000 mm/);
assert.match(overview, /首孔中心端距 100 mm/);
assert.match(overview, /末孔中心端距 100 mm/);
assert.match(overview, /尺寸均为孔中心距/);
assert.equal([...overview.matchAll(/data-punch-layout-point=/g)].length, 5);
assert.match(overview, /每排 5 × 1 排 · 已启用 5 个位置/);

const skipOverview = renderPunchLayoutOverview({ ...base, rowCount: 2, skipInstancesText: "3, 2:3, 2:4" }, 1000);
assert.match(skipOverview, /已启用 7 个位置/);
assert.match(skipOverview, /跳过 3 个/);
assert.match(skipOverview, /punch-layout-hole is-skipped/);
assert.match(skipOverview, /data-punch-layout-point="3" data-punch-layout-active-rows="0"/);
assert.match(skipOverview, /data-punch-layout-point="4" data-punch-layout-active-rows="1"/);
const skippedEnds = renderPunchLayoutOverview({ ...base, distributionMode: "end-margins", skipInstancesText: "1,5" }, 1000);
assert.match(skippedEnds, /候选首孔中心端距 100 mm/);
assert.match(skippedEnds, /候选末孔中心端距 100 mm/);
assert.match(skippedEnds, /启用首孔中心端距 300 mm/);
assert.match(skippedEnds, /启用末孔中心端距 300 mm/);
assert.match(skippedEnds, /至少一排启用的位置/);
assert.match(skippedEnds, /不代表实际切孔边界/);
assert.match(skippedEnds, /<title>[^<]*候选首孔距起点 100 mm[^<]*启用首孔中心端距 300 mm/);
const partialEndSkips = renderPunchLayoutOverview({ ...base, distributionMode: "end-margins", rowCount: 2, skipInstancesText: "1,5,2:5" }, 1000);
assert.match(partialEndSkips, /启用首孔中心端距 100 mm/, "A second row still has the first candidate enabled");
assert.match(partialEndSkips, /启用末孔中心端距 300 mm/, "Both rows skip the last candidate");
const reverseEndSkips = renderPunchLayoutOverview({ ...base, reference: "end", arrayPitch: 200, skipInstancesText: "1,2,5" }, 1000);
assert.match(reverseEndSkips, /候选首孔中心端距 100 mm/);
assert.match(reverseEndSkips, /候选末孔中心端距 100 mm/);
assert.match(reverseEndSkips, /启用首孔中心端距 300 mm/);
assert.match(reverseEndSkips, /启用末孔中心端距 500 mm/, "Stable skip indices are not renumbered when sorting reverse arrays");
const sampledEndSkips = renderPunchLayoutOverview({ ...base, arrayCount: 101, station: 0, arrayPitch: 10, skipInstancesText: "1,2,101" }, 1000);
assert.match(sampledEndSkips, /图示抽样 60 \/ 101/);
assert.match(sampledEndSkips, /启用首孔中心端距 20 mm/);
assert.match(sampledEndSkips, /启用末孔中心端距 10 mm/, "Distances are derived from every active candidate, not just the sampled dots");
const disabledSkippedEnds = renderPunchLayoutOverview({ ...base, enabled: false, skipInstancesText: "1,5" }, 1000);
assert.match(disabledSkippedEnds, /未跳过首孔中心端距 200 mm/);
assert.doesNotMatch(disabledSkippedEnds, /启用首孔中心端距/);
const opposite = renderPunchLayoutOverview({ ...base, depthMode: "both" }, 1000);
assert.match(opposite, /含对面 10 个刀具/);
assert.match(renderPunchLayoutOverview({ ...base, enabled: false }, 1000), /此条已停用/);

const many = renderPunchLayoutOverview({ ...base, distributionMode: "equal", arrayCount: 100 }, 1000);
assert.equal([...many.matchAll(/data-punch-layout-point=/g)].length, 60);
assert.match(many, /已启用 100 个位置/);
assert.match(many, /图示抽样 60 \/ 100 个长度位置/);
assert.match(many, /data-punch-layout-point="1"/);
assert.match(many, /data-punch-layout-point="100"/);

const reverse = renderPunchLayoutOverview({ ...base, reference: "end", arrayCount: 3, arrayPitch: 200 }, 1000);
assert.match(reverse, /首孔中心端距 500 mm/);
assert.match(reverse, /末孔中心端距 100 mm/);
const invalid = renderPunchLayoutOverview({ ...base, arrayCount: 2, arrayPitch: 0 }, 1000);
assert.match(invalid, /布局无法应用/);
assert.match(invalid, /role="alert"/);
assert.doesNotMatch(invalid, /<svg/);
assert.match(renderPunchLayoutOverview(null, 1000), /选择一条冲孔记录/);
const legacyDatum = renderPunchLayoutOverview({ ...base, layoutDatum: undefined, reference: "end", endDatum: "short", station: 80 }, 1000);
assert.match(legacyDatum, /成品端面基准 · 短点/);
assert.match(legacyDatum, /实际孔位以三维预览为准/);
assert.match(legacyDatum, /距成品尾端 80 mm/);
assert.doesNotMatch(legacyDatum, /<svg|母材中心 X=0|首孔中心端距|末孔中心端距|data-punch-layout-point/);
assert.match(render({ layoutDatum: "finished", endDatum: "center" }), /成品端面基准 · 端面中心/);
const legacyBranch = renderPunchLayoutOverview({ ...base, layoutDatum: undefined, toolTarget: "part" }, 1000);
assert.match(legacyBranch, /<svg/);
assert.doesNotMatch(legacyBranch, /成品端面基准/);
const clippedOverview = renderPunchLayoutOverview({ ...base, station: 980, arrayCount: 3, arrayPitch: 15 }, 1000);
assert.match(clippedOverview, /端外刀具按实际相交裁剪/);
assert.match(clippedOverview, /不按完整孔计数/);
assert.match(clippedOverview, /punch-layout-hole is-outside/);
assert.doesNotMatch(clippedOverview, /布局无法应用/);
for(const point of clippedOverview.matchAll(/cx="([^"]+)"/g))assert.ok(Number(point[1])>=36&&Number(point[1])<=644);
const legacyCenterDatum = renderPunchLayoutOverview({ ...base, layoutDatum: undefined, endDatum: "center" }, 1000);
assert.match(legacyCenterDatum, /成品端面基准/);
assert.doesNotMatch(legacyCenterDatum, /<svg/);

const dangerous = renderPunchLayoutControls(() => 'test" onfocus="bad', { ...base, distributionMode: "positions", positionList: '</textarea><script>alert(1)</script>' }, '"<img>', false, 1000);
assert.doesNotMatch(dangerous, /<script>|<img>| onfocus="bad/);
assert.match(dangerous, /&lt;\/textarea&gt;&lt;script&gt;/);
assert.match(dangerous, /test&quot; onfocus=&quot;bad/);
assert.match(punchLayoutStyles, /\.tube-designer-punch-layout-overview/);
assert.match(punchLayoutStyles, /\.punch-layout-hole\.is-skipped/);
assert.match(punchLayoutStyles, /:focus-visible/);

console.log("TubeDesignerPunchLayoutViewTest passed");
