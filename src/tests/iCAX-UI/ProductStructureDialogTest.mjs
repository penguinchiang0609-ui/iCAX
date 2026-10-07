import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { parameterVisible } from "../../apps/tube-designer/webpage/parameterConditions.mjs";
import { applyProductControlChoice, productControlChoices, productControlEffectiveValues, productControlFields, productControlValue, productStructureEditors } from "../../apps/tube-designer/webpage/productControls.mjs";
import { renderDesignerAddParameterContent, renderDesignerRightParameterContent, renderProductStructureDialog } from "../../apps/tube-designer/webpage/designerViews.mjs";

const directory = new URL("../../apps/tube-designer/templates/product/single_face_security_window/", import.meta.url);
const descriptor = JSON.parse(readFileSync(new URL("template.json", directory)));
const display = JSON.parse(readFileSync(new URL("display.json", directory)));
const template = { ...descriptor, display, available: true,
  groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
  parameters: descriptor.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
    displayName: catalogText(field.displayName), groupKey: field.group,
    options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
const defaults = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
const controls = productControlFields(template);
const editors = productStructureEditors(template);
const groupKeys = ["product-shape", "outer-frame-structure", "fixed-frame-structure", "leaf-frame-structure", "product-options"];
assert.equal(descriptor.version, "3.9.9");
assert.deepEqual(descriptor.extensions.addDialog.parameterGroups.map(group => group.key), groupKeys);
assert.deepEqual(editors.map(editor => editor.controlKey), ["outerFrameStructure", "doorFrameStructure", "doorLeafFrameStructure"]);
for (const editor of editors) {
  assert.deepEqual(editor.controls,[editor.controlKey],"Structure dialogs contain no detailed manufacturing choices");
  for (const key of editor.controls) {
    assert.ok(descriptor.extensions.addDialog.structureParameters.includes(key));
    assert.equal(template.parameters.some(field => field.key === key), false, "Product choices remain virtual");
  }
}

function fixture(parameters) {
  const product = { entityId: "product-structure-test", templateId: template.id, parameters, quantity: 1 };
  return { product, templates: [template], parts: [] };
}
function viewFor(designer, key, parameters = designer.product.parameters) {
  return { activeAreaId: "view", tubeDesignerParameterPanelProductId: designer.product.entityId,
    tubeDesignerParameterDisclosureState: { initialized: true }, tubeDesignerExpandedParameterGroups: ["section:process"],
    tubeDesignerStructureDialog: { productEntityId: designer.product.entityId, templateId: template.id, controlKey: key,
      parameters: structuredClone(parameters) } };
}
function selectKeys(html) {
  return [...html.matchAll(/<select\b[^>]*data-product-control-key="([^"]+)"/g)].map(match => match[1]);
}
function editorFields(editor, values) {
  return editor.controls.map(key => controls.find(control => control.key === key))
    .filter(control => parameterVisible(control, productControlEffectiveValues(template, values)));
}
function assertSummary(html, editor, parameters) {
  const expected = editorFields(editor, parameters).map(control => {
    const value = productControlValue(template, control, parameters);
    return productControlChoices(template, control, parameters).find(choice => choice.value === value)?.label ?? "尚未选择";
  }).join(" · ");
  assert.ok(html.includes(`data-tube-designer-current-structure>${expected}</strong>`), `${editor.key} summary includes the active product choices`);
  assert.ok(html.includes(`data-cam-action="tube-designer-open-product-structure" data-product-control-key="${editor.controlKey}"`));
}

let routes = 0;
let popups = 0;
for (const faceType of ["single", "two", "three", "five"]) {
  for (const mode of ["segment_weld", "plane_v_notch", ...(["two", "three"].includes(faceType) ? ["spatial_v_notch"] : [])]) {
    for (const groove of ["sharp", "rounded", "edge"]) {
      const parameters = { ...defaults, faceType, frameLayout: "four_sides", frameManufacturingMode: mode,
        outerFrameGrooveTool: groove === "edge" ? "system:edge-arc-groove" : "system:v-notch-sharp",
        outerFrameVGrooveBottomStrategy: groove === "rounded" ? "rounded" : "sharp",
        doorFrameJoinType: "v_groove_90:tool_library", doorFrameGrooveTool: "system:v-notch-sharp", doorFrameVGrooveBottomStrategy: "rounded",
        doorLeafFrameJoinType: "butt_90", accessDoorEnabled: true };
      const before = structuredClone(parameters);
      const designer = fixture(parameters);
      const add = renderDesignerAddParameterContent(designer, { tubeDesignerAddTemplateId: template.id, tubeDesignerAddDraft: parameters });
      const order = groupKeys.map(key => add.indexOf(`data-tube-designer-parameter-group="add:${key}"`));
      assert.ok(order.every((position, index) => position >= 0 && (index === 0 || order[index - 1] < position)), "Add groups follow the requested product-part order");
      assert.deepEqual(selectKeys(add), ["outerFrameStructure", "doorFrameStructure", "doorLeafFrameStructure"]);
      assert.equal(add.includes('data-tube-designer-parameter="frameManufacturingMode"'), false);
      for (const control of controls) assert.equal(add.includes(`data-tube-designer-parameter="${control.key}"`), false);

      const rightView = viewFor(designer, editors[0].controlKey);
      const right = renderDesignerRightParameterContent(designer, rightView).scrollContent;
      for (const editor of editors) assertSummary(right, editor, parameters);
      assert.equal((right.match(/data-tube-designer-current-structure>/g) ?? []).length, 3);
      assert.ok(selectKeys(right).every(key => !editors.some(editor => editor.controls.includes(key))), "Right panel has summaries rather than inline structure choices");
      assert.ok(right.includes(`data-product-control-editor="${mode === "segment_weld" ? "outerFrameConnection" : "outerFrameBendType"}"`));
      assert.ok(right.includes('data-product-control-editor="doorFrameConnection"'));
      assert.ok(right.includes('data-product-control-editor="doorLeafFrameConnection"'));
      assert.deepEqual(productControlChoices(template,"doorFrameConnection",parameters).map(choice=>choice.value),["v-groove","rounded-v-groove","edge-arc"]);
      assert.deepEqual(productControlChoices(template,"doorLeafFrameConnection",parameters).map(choice=>choice.value),["miter","butt"]);
      if (mode !== "segment_weld") {
        assert.ok(right.includes('data-tube-designer-parameter="outerFrameFoldBridge"'), "Applicable numeric fold settings remain in the right panel");
      }
      for (const editor of editors) {
        const view = viewFor(designer, editor.controlKey);
        const popup = renderProductStructureDialog(designer, view);
        assert.deepEqual(selectKeys(popup), editorFields(editor, parameters).map(control => control.key));
        assert.equal((popup.match(/<select\b/g)??[]).length,1,"Each popup is strictly a structure selector");
        assert.equal(popup.includes('value="rounded-v-groove"'),false);
        assert.equal(popup.includes('value="miter"'),false);
        assert.equal(popup.includes('value="butt"'),false);
        assert.ok(popup.includes(`id="tube-designer-structure-dialog-title">${catalogText(editor.dialogTitle)}</strong>`));
        assert.ok(popup.includes('data-tube-designer-structure-backdrop'));
        assert.ok(popup.includes('data-cam-change-action="tube-designer-structure-dialog-change"'));
        assert.ok(popup.includes('data-cam-action="tube-designer-confirm-product-structure"'));
        assert.ok(popup.includes('data-cam-action="tube-designer-cancel-product-structure"'));
        assert.equal(popup.includes('data-tube-designer-parameter='), false, "Virtual selections cannot become native host fields");
        assert.equal(renderProductStructureDialog(designer, { ...view, activeAreaId: "nesting" }), "");
        for (const [key, value] of [["productEntityId", "another"], ["templateId", "another"], ["controlKey", "unknown"]]) {
          assert.equal(renderProductStructureDialog(designer, { ...view, tubeDesignerStructureDialog: { ...view.tubeDesignerStructureDialog, [key]: value } }), "");
        }
        assert.deepEqual(view.tubeDesignerStructureDialog.parameters, before);
        popups++;
      }
      assert.deepEqual(parameters, before, "Rendering preserves every active and inactive physical draft");
      routes++;
    }
  }
}

// A spatial draft retained while the face is temporarily single projects a
// valid plane route without changing the physical draft.
const temporary = { ...defaults, faceType: "single", frameManufacturingMode: "spatial_v_notch", frameLayout: "four_sides",
  outerFrameGrooveTool: "system:v-notch-sharp", outerFrameVGrooveBottomStrategy: "rounded" };
const temporaryBefore = structuredClone(temporary);
const temporaryDesigner = fixture(temporary);
const temporaryAdd = renderDesignerAddParameterContent(temporaryDesigner, { tubeDesignerAddTemplateId: template.id, tubeDesignerAddDraft: temporary });
assert.deepEqual(selectKeys(temporaryAdd), ["outerFrameStructure", "doorFrameStructure", "doorLeafFrameStructure"]);
const temporaryPopup = renderProductStructureDialog(temporaryDesigner, viewFor(temporaryDesigner, "outerFrameStructure"));
assert.ok(temporaryPopup.includes('value="plane-single" selected'));
assert.equal(temporaryPopup.includes('value="rounded-v-groove"'),false);
assert.equal(temporaryPopup.includes('value="spatial"'), false);
assert.deepEqual(temporary, temporaryBefore);

// Latest external visibility wins over a stale popup draft.
const noDoor = { ...defaults, accessDoorEnabled: false };
const noDoorDesigner = fixture(noDoor);
const noDoorAdd = renderDesignerAddParameterContent(noDoorDesigner, { tubeDesignerAddTemplateId: template.id, tubeDesignerAddDraft: noDoor });
const noDoorRight = renderDesignerRightParameterContent(noDoorDesigner, viewFor(noDoorDesigner, "outerFrameStructure")).scrollContent;
for (const editor of editors.slice(1)) {
  assert.equal(noDoorAdd.includes(`data-product-control-editor="${editor.controlKey}"`), false);
  assert.equal(noDoorRight.includes(`data-product-control-editor="${editor.controlKey}"`), false);
  assert.equal(renderProductStructureDialog(noDoorDesigner, viewFor(noDoorDesigner, editor.controlKey, { ...noDoor, accessDoorEnabled: true })), "");
}

// Popup contents reflect its local physical draft; summaries reflect only the
// latest right-hand product values until the caller confirms the draft.
const localDesigner = fixture({ ...defaults, faceType: "three", frameManufacturingMode: "spatial_v_notch", frameLayout: "four_sides" });
const localParameters = { ...localDesigner.product.parameters, frameManufacturingMode: "segment_weld" };
const localView = viewFor(localDesigner, "outerFrameStructure", localParameters);
assert.deepEqual(selectKeys(renderProductStructureDialog(localDesigner, localView)), ["outerFrameStructure"]);
assertSummary(renderDesignerRightParameterContent(localDesigner, localView).scrollContent, editors[0], localDesigner.product.parameters);
assert.deepEqual(localView.tubeDesignerStructureDialog.parameters, localParameters);

for(const prefix of ["doorFrame","doorLeafFrame"]) {
  const structure=prefix+"Structure",join=prefix+"JoinType";
  for(const joined of ["miter_45","butt_90"]) {
    const values={...defaults,[join]:joined,[prefix+"VGrooveRoundRadius"]:3.7,[prefix+"BendKFactor"]:.62};
    const before=structuredClone(values);
    assert.deepEqual(applyProductControlChoice({},template,structure,values,"joined",{reapply:true}),before,
      "Confirming an existing joined structure preserves the chosen butt/miter and all inactive drafts");
    const folded=applyProductControlChoice({},template,structure,values,"folded",{reapply:true});
    assert.deepEqual(folded,{...before,[join]:"v_groove_90:tool_library"});
    assert.deepEqual(productControlChoices(template,prefix+"Connection",folded).map(choice=>choice.value),["v-groove","rounded-v-groove","edge-arc"]);
    assert.deepEqual(applyProductControlChoice({},template,structure,folded,"folded",{reapply:true}),folded,
      "A folded confirmation retains its tool and numeric drafts");
    assert.deepEqual(applyProductControlChoice({},template,structure,folded,"joined",{reapply:true}),{...before,[join]:"miter_45"});
    assert.deepEqual(values,before,"Applying a structure never mutates its source input");
  }
}
console.log(`ProductStructureDialogTest passed: ${routes} shape/structure/groove routes, ${popups} scoped role popups, five creation groups, effective drafts and three right summaries.`);
