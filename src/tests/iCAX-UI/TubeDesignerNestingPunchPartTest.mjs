import assert from "node:assert/strict";
import {
  handleNestingPunchPartAction,
  renderNestingPunchPartDialog,
  NEW_PART_ID,
} from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";
import { buildPunchPreviewRows } from "../../apps/tube-designer/webpage/punchEditor.mjs";

const profile = {
  id: "round",
  name: "系统圆管",
  profileType: "parametric-package",
  descriptor: { parameters: [
    { key: "width", displayName: "外径", valueType: "number", defaultValue: 40, unit: "mm" },
    { key: "wallThickness", displayName: "壁厚", valueType: "number", defaultValue: 2, unit: "mm" },
  ] },
  defaultParameters: { width: 40, wallThickness: 2 },
  previewProfile: {
    kind: "round", name: "系统圆管", width: 40, depth: 40, diameter: 40,
    specification: "Φ40 × 2", contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }],
  },
};
const view = {
  activeAreaId: "nesting",
  pending: false,
  tubeDesignerSystemProfiles: [profile],
  tubeDesignerNestingSelectedPartIds: [],
};
const operations = { renderProject() {}, showNotice() {} };
const previewCalls = [];
const previewContext = {
  sceneProxy: {
    async invoke(method, request) {
      previewCalls.push({ method, request });
      return {
        toolsOnly:true,
        baseGeometry: { url: "memory://main-tube", version: previewCalls.length },
        baseMaterial: { url: "memory://main-tube-material", version: 1 },
        ...(request.features?.length ? {
          toolGeometry: { url: "memory://punch-tools", version: previewCalls.length },
          toolMaterial: { url: "memory://punch-tool-material", version: 1 },
          toolCount: request.features.length,
        } : { toolCount: 0 }),
        baseBounds: { width: 900 },
      };
    },
  },
};

await handleNestingPunchPartAction(previewContext, view, "tube-designer-nesting-punch-create-open", {}, operations);
assert.equal(view.tubeDesignerPunchWizard.partId, NEW_PART_ID);
assert.equal(previewCalls[0].method, "TubeDesigner.PreviewPunchWizard");
assert.equal(previewCalls[0].request.profileRef.id, "round");
assert.equal(previewCalls[0].request.length, 1000);
assert.deepEqual(previewCalls[0].request.features, []);
assert.equal(view.tubeDesignerPunchWizard.preview.baseGeometry.url, "memory://main-tube");
const batch=view.tubeDesignerPunchBatch;
assert.equal(batch.parts.length,1);
const previewRows=buildPunchPreviewRows(view.tubeDesignerPunchWizard.preview);
assert.equal(previewRows[0].entityId,'punch-preview-blank');
assert.equal(previewRows[0].data.material.url,'memory://main-tube-material');
const firstId=batch.selectedPartId;
let dialog=renderNestingPunchPartDialog(view);
for(const text of ['批量冲孔向导','整批共用孔型','批量零件直接录入表','外径','壁厚','左端面','右端面','孔位'])assert.ok(dialog.includes(text),text);
assert.match(dialog,/data-cam-action="tube-designer-nesting-punch-create-batch-add"/);
assert.equal(view.tubeDesignerPunchWizard.draft.recordKind,'branch');
assert.equal(view.tubeDesignerPunchWizard.draft.section.key,'system:round');
await handleNestingPunchPartAction(previewContext,view,'tube-designer-nesting-punch-create-main-profile-parameter',{
  value:'50',dataset:{tubeDesignerMainProfileParameter:'width',tubeDesignerMainProfileValueType:'number'},
},operations);
assert.equal(previewCalls.at(-1).request.parameters.width,50);
const changePart=(id,key,value)=>handleNestingPunchPartAction(previewContext,view,'tube-designer-nesting-punch-create-batch-change',{
  value:String(value),dataset:{punchBatchPartId:id,punchBatchField:key},
},operations);
await changePart(firstId,'length',900);await changePart(firstId,'quantity',2);
await changePart(firstId,'name','批量圆管一');
await handleNestingPunchPartAction(previewContext,view,'tube-designer-nesting-punch-create-batch-copy',{},operations);
assert.equal(batch.parts.length,2);
const secondId=batch.selectedPartId;assert.notEqual(secondId,firstId);
await changePart(secondId,'length',600);await changePart(secondId,'quantity',3);
await changePart(secondId,'name','批量圆管二');
dialog=renderNestingPunchPartDialog(view);
assert.match(dialog,/2 种零件 · 5 件/);
assert.match(dialog,/value="批量圆管一"/);assert.match(dialog,/value="批量圆管二"/);
const calls=[],parts=[];
const context={sceneProxy:{async invoke(method,request){
  calls.push({method,request:structuredClone(request)});
  assert.equal(method,'TubeDesigner.AddNestingPunchPart');
  const entityId='new-part-'+calls.length;
  parts.push({entityId,independentNesting:true,length:request.length,quantity:request.quantity,
    properties:{'manufacturing.partKind':'tube','tubeDesigner.cadDocument':{documentResourceId:'document-'+entityId,documentResourceVersion:1}}});
  return {tubeDesigner:{nestingGroups:[{parts:structuredClone(parts)}]},partEntityId:entityId};
}},actions:{async refreshActiveSceneState(){}}};
await handleNestingPunchPartAction(context,view,'tube-designer-nesting-punch-create-apply',{},operations);
assert.equal(calls.length,2);
assert.deepEqual(calls.map(call=>call.request.length),[900,600]);
assert.deepEqual(calls.map(call=>call.request.quantity),[2,3]);
assert.deepEqual(calls.map(call=>call.request.name),['批量圆管一','批量圆管二']);
for(const call of calls) {
  assert.equal(call.request.profileRef.id,'round');assert.equal(call.request.parameters.width,50);
  assert.deepEqual(call.request.features,[]);
}
assert.equal(view.scene.tubeDesigner.nestingGroups[0].parts.length,2);
assert.ok(view.scene.tubeDesigner.nestingGroups[0].parts.every(part=>part.properties['tubeDesigner.cadDocument']));
assert.equal(view.tubeDesignerNestingPunchPartDraft,null);
assert.equal(view.tubeDesignerPunchWizard,null);
assert.equal(view.tubeDesignerPunchBatch,null);
assert.equal(view.pending,false);
console.log('TubeDesigner batch punch creation: current batch UI, common profile parameters, copy, lengths/quantities and direct scene creation passed.');
