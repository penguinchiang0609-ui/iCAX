import assert from "node:assert/strict";
import { migrateLegacyPunchArrays, resolvePunchArrayGroups, punchArraySkipText } from "../../apps/tube-designer/webpage/punchArrayGroups.mjs";
import { punchArrayGroupSummary, renderPunchArrayGroupsControls, renderPunchArrayGroupsSummary } from "../../apps/tube-designer/webpage/punchArrayGroupsView.mjs";
import { createPunchWizardState, installPunchCatalogue, normalizePunchFeature, renderPunchWizardDialog, openPunchParameters, closePunchParameters } from "../../apps/tube-designer/webpage/punchWizard.mjs";

const action = suffix => "fixture-punch-" + suffix;
const original = { type: "circle", station: 100, reference: "start", face: "round", rowDistributionMode: "full-circle", rowStartAngle: 30, rowCount: 4,
  distributionMode: "end-margins", headMargin: 100, tailMargin: 100, arrayCount: 5, arrayPitch: 100, skipInstancesText: "2:3", layoutDatum: "base" };
const feature = migrateLegacyPunchArrays(original, 1000), result = resolvePunchArrayGroups(feature, 1000);
assert.equal(original.arrayGroups, undefined, "Reading the grouped view never mutates a legacy recipe");
let html = renderPunchArrayGroupsControls(action, feature, "0", false, 1000, feature.arrayGroups, result, punchArraySkipText(feature));
assert.match(html, /data-array-group-id="legacy-length"/);assert.match(html, /data-array-group-id="legacy-rows"/);
assert.match(html, /data-tube-designer-punch-array-field="count"/);
assert.match(renderPunchArrayGroupsControls(action,feature,"0",false,1000,[{...feature.arrayGroups[0],distributionMode:"pitch"}],result), /data-tube-designer-punch-array-field="spacing"/);
assert.doesNotMatch(html, /data-tube-designer-punch-field="arrayCount"|skipInstancesText/);
for (const mode of ["pitch", "equal", "middle-fixed", "end-margins", "center-out", "fill", "max-spacing", "sequence", "positions"]) assert.match(html, new RegExp('value="' + mode + '"'));
for(const axis of ["X","Y","Z"]){assert.match(html,new RegExp("＋ 沿 "+axis));assert.match(html,new RegExp("＋ 绕 "+axis));}
assert.match(html, /3:2<\/textarea>/, "Legacy row:column skips display in the new ordered column-group:row-group form");
assert.match(html, /originAuto/);assert.match(html, /通过母材中心/);
assert.match(renderPunchArrayGroupsSummary(feature.arrayGroups, result), /沿主管长度.*5 个.*绕 X 圆周.*4 个/s);
assert.match(renderPunchArrayGroupsSummary(feature.arrayGroups, result), /组合后 19 个位置/);
for (const axis of ["Y", "Z"]) for (const distributionMode of ["equal", "end-margins", "fill", "sequence", "positions"]) {
  const group = { id: "changed-axis", type: "linear", axis, count: 3, spacing: 25, direction: "negative", distributionMode };
  assert.equal(punchArrayGroupSummary(group, { ...group, instanceCount: 3, layoutSummary: { centerPitch: 200 } }), "沿 " + axis + " 直线 · 3 个 · -25 mm", "Changing an advanced X group to Y/Z displays its actual straight-line rule, not retained X options");
}

const part={entityId:"test",length:1000,properties:{}},view={pending:false,tubeDesignerPunchWizard:createPunchWizardState(part)};
const s=view.tubeDesignerPunchWizard;
installPunchCatalogue(s,{tools:[{id:"branch-profile",target:"part",requiresSection:true,kind:"programmatic",version:"1",digest:"test",parameters:[
  {key:"angle",displayName:"与主管轴夹角",defaultValue:90},{key:"azimuth",displayName:"绕主管轴方位",defaultValue:0},{key:"length",displayName:"拉伸长度",defaultValue:120}]}]});
s.features=[normalizePunchFeature({...original,toolRef:{id:"branch-profile",version:"1",digest:"test"},toolTarget:"part",recordKind:"branch",section:{name:"圆管",source:"library"},toolParameters:{angle:90,azimuth:0,length:120}})];
html=renderPunchWizardDialog(part,view,{tableMode:true});
const table=html.slice(html.indexOf('<section class="tube-designer-punch-sheet">'),html.indexOf('</tbody></table></div></section>')+35);
assert.match(table,/编辑形状/);assert.match(table,/编辑位置 \/ 姿态/);assert.match(table,/编辑阵列/);
assert.doesNotMatch(table,/data-tube-designer-punch-field="station"|data-tube-designer-punch-field="arrayCount"|data-tube-designer-punch-end-row/);
assert.equal((table.match(/<th>/g)??[]).length,6);
openPunchParameters(view,"0");s.parameterEditor.mode="shape";
html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.match(html,/data-tube-designer-punch-parameter="length"/);assert.doesNotMatch(html,/data-tube-designer-punch-parameter="angle"/);
s.parameterEditor.mode="pose";
html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.match(html,/data-tube-designer-punch-parameter="angle"/);assert.match(html,/data-tube-designer-punch-field="station"/);
assert.doesNotMatch(html,/data-tube-designer-punch-parameter="length"/);
closePunchParameters(view,false,part);
console.log("Grouped array view: nine advanced length rules, independent linear/polar groups, stable keys, legacy skip display, six-column records and separate shape/pose fields passed.");
