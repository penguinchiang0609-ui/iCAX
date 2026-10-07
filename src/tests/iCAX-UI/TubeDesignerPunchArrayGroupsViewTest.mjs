import assert from "node:assert/strict";
import { createPunchArrayGroupsFromLayout, resolvePunchArrayGroups, punchArraySkipText } from "../../apps/tube-designer/webpage/punchArrayGroups.mjs";
import { punchArrayGroupSummary, renderPunchArrayGroupsControls, renderPunchArrayGroupsSummary } from "../../apps/tube-designer/webpage/punchArrayGroupsView.mjs";
import { createPunchWizardState, installPunchCatalogue, normalizePunchFeature, renderPunchWizardDialog, togglePunchRowParameters, closePunchParameters } from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { normalizePunchArrayGroup } from '../../apps/tube-designer/webpage/punchArrayGroups.mjs';
import { punchArrayColumns } from '../../apps/tube-designer/webpage/punchSheetColumns.mjs';

const action = suffix => "fixture-punch-" + suffix;
const original = { type: "circle", station: 100, reference: "start", face: "round", rowDistributionMode: "full-circle", rowStartAngle: 30, rowCount: 4,
  distributionMode: "end-margins", headMargin: 100, tailMargin: 100, arrayCount: 5, arrayPitch: 100, skipInstancesText: "2:3", layoutDatum: "base" };
const feature = createPunchArrayGroupsFromLayout(original, 1000), result = resolvePunchArrayGroups(feature, 1000);
assert.equal(original.arrayGroups, undefined, "Reading the grouped view never mutates a legacy recipe");
let html = renderPunchArrayGroupsControls(action, feature, "0", false, 1000, feature.arrayGroups, result, punchArraySkipText(feature));
assert.match(html, /data-array-group-id="layout-length"/);assert.match(html, /data-array-group-id="layout-rows"/);
assert.match(html, /data-tube-designer-punch-array-field="count"/);
assert.match(renderPunchArrayGroupsControls(action,feature,"0",false,1000,[{...feature.arrayGroups[0],distributionMode:"pitch"}],result), /data-tube-designer-punch-array-field="spacing"/);
assert.doesNotMatch(html, /data-tube-designer-punch-field="arrayCount"|skipInstancesText/);
for (const mode of ["pitch", "equal", "middle-fixed", "end-margins", "center-out", "fill", "max-spacing", "sequence", "positions"]) assert.match(html, new RegExp('value="' + mode + '"'));
assert.match(html,/array-mode-change/);
assert.match(html,/<option value="two" selected>二维阵列<\/option>/);
assert.doesNotMatch(html,/array-group-add|array-group-create|array-group-remove/);
assert.match(html, /3:2<\/textarea>/, "Layout row:column skips display in the ordered column-group:row-group form");
assert.match(html, /originAuto/);assert.match(html, /通过母材中心/);
assert.match(renderPunchArrayGroupsSummary(feature.arrayGroups, result), /沿主管长度.*5 个.*绕 X 圆周.*4 个/s);
assert.match(renderPunchArrayGroupsSummary(feature.arrayGroups, result), /组合后 19 个位置/);
for (const axis of ["Y", "Z"]) for (const distributionMode of ["equal", "end-margins", "fill", "sequence", "positions"]) {
  const group = { id: "changed-axis", type: "linear", axis, count: 3, spacing: 25, direction: "negative", distributionMode };
  assert.equal(punchArrayGroupSummary(group, { ...group, instanceCount: 3, layoutSummary: { centerPitch: 200 } }), "沿 " + axis + " 直线 · 3 个 · -25 mm", "Changing an advanced X group to Y/Z displays its actual straight-line rule, not retained X options");
}
// Column expansion must expose exactly the fields of the existing editable
// recipe, including advanced length rules and custom polar axes.
for(const candidate of [
  ...['pitch','equal','middle-fixed','end-margins','center-out','fill','max-spacing','sequence','positions'].map(distributionMode=>({type:'linear',axis:'X',distributionMode,centerMode:'gap'})),
  ...['Y','Z'].map(axis=>({type:'linear',axis})),
  ...['pitch','full-circle','angle-range'].map(angleMode=>({type:'polar',axis:'Z',angleMode}))
]) {
  const group=normalizePunchArrayGroup({...candidate,id:'parity'});
  const existing=renderPunchArrayGroupsControls(action,feature,'0',false,1000,[group]);
  const keys=[...existing.matchAll(/data-tube-designer-punch-array-group="parity" data-tube-designer-punch-array-field="([^"]+)"/g)].map(match=>match[1]);
  assert.deepEqual(punchArrayColumns(group).map(field=>field.key).sort(),keys.sort(),'Column editor retains every existing applicable recipe field: '+JSON.stringify(candidate));
}

const part={entityId:"test",length:1000,properties:{}},view={pending:false,tubeDesignerPunchWizard:createPunchWizardState(part)};
const s=view.tubeDesignerPunchWizard;
installPunchCatalogue(s,{tools:[{id:"branch-profile",target:"part",requiresSection:true,kind:"programmatic",version:"1",digest:"test",parameters:[
  {key:"angle",displayName:"与主管轴夹角",defaultValue:90},{key:"azimuth",displayName:"绕主管轴方位",defaultValue:0},{key:"length",displayName:"拉伸长度",defaultValue:120}]}]});
s.features=[normalizePunchFeature({...original,toolRef:{id:"branch-profile",version:"1",digest:"test"},toolTarget:"part",recordKind:"branch",section:{name:"圆管",source:"library"},toolParameters:{angle:90,azimuth:0,length:120}})];
html=renderPunchWizardDialog(part,view,{tableMode:true});
const tableStart=html.indexOf('<section class="tube-designer-punch-sheet">');
const table=html.slice(tableStart,html.indexOf('</tbody></table></div></section>',tableStart)+35);
assert.match(table,/data-punch-record-column="shape"[^>]*data-punch-cell-activate/);
for(const mode of ['pose','arrays'])assert.match(table,new RegExp('data-punch-record-column="'+mode+':'));
assert.doesNotMatch(table,/<button[^>]*parameters-open/);
assert.match(table,/data-tube-designer-punch-field="station"/);
assert.match(table,/data-punch-column-key="count"/);
assert.doesNotMatch(table,/data-tube-designer-punch-array-field="enabled"/);
assert.match(table,/<option value="none">无<\/option>/);
assert.match(table,/<option value="two" selected>二维阵列<\/option>/);
assert.doesNotMatch(table,/arrays:groups|array-group-add|array-group-create|array-group-remove/,"Array dimensions directly choose one or two parameter lines without group management buttons");
assert.doesNotMatch(table,/data-tube-designer-punch-field="arrayCount"|data-tube-designer-punch-end-row/);
assert.match(table,/colspan="\d+"[^>]*><button[^>]*columns-toggle/);
togglePunchRowParameters(view,"0");s.parameterEditor.mode="shape";
html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.match(html,/data-tube-designer-punch-parameter="length"/);assert.match(html,/data-tube-designer-punch-parameter="angle"/);
assert.match(html,/data-punch-inline-editor/);assert.doesNotMatch(html,/data-punch-parameter-dialog/);
assert.match(html,/data-punch-inline-row="[^"]+"><td colspan="\d+"><section/);
s.parameterEditor.mode="pose";
html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.match(html,/data-tube-designer-punch-field="station"/);assert.match(html,/data-tube-designer-punch-field="roll"/);
assert.doesNotMatch(html,/data-tube-designer-punch-parameter="length"/);
assert.match(html,/data-punch-record-column="pose:station"/);
assert.doesNotMatch(html,/data-punch-cell-editor=/);
assert.doesNotMatch(html,/data-punch-inline-row=/);
s.parameterEditor.mode="arrays";
html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.match(html,/data-punch-record-column="arrays:count"/);
assert.doesNotMatch(html,/data-punch-cell-editor=/);
assert.doesNotMatch(html,/data-punch-inline-row=/);
closePunchParameters(view,false,part);
const disabled=normalizePunchArrayGroup({id:'disabled',type:'polar',axis:'X',count:12,enabled:false});
assert.deepEqual(punchArrayColumns(disabled).map(field=>field.key),['type']);
const disabledControls=renderPunchArrayGroupsControls(action,feature,'0',false,1000,[disabled]);
assert.match(disabledControls,/<option value="none" selected>无<\/option>/);
assert.doesNotMatch(disabledControls,/data-tube-designer-punch-array-field="enabled"|data-tube-designer-punch-array-field="count"/);
s.features[0].arrayGroups=[disabled];
html=renderPunchWizardDialog(part,view,{tableMode:true});
assert.match(html,/<option value="none" selected>无<\/option>/);
console.log("Grouped array view: nine length rules, independent linear/polar groups, full-row shape editing and direct pose/array columns passed.");
