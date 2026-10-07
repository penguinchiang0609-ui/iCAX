// Current product-owned spatial folding controls, using the actual resource descriptors.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  applyProductControlChoice, productControlChoices, productControlFields, productControlValue,
} from "../../apps/tube-designer/webpage/productControls.mjs";
import { parameterVisible } from "../../apps/tube-designer/webpage/parameterConditions.mjs";
import {
  makeProductToolBinding, productToolRole,
} from "../../apps/tube-designer/webpage/productResourceBindings.mjs";
import {
  manufacturingOnlyParameterKeys, productDisplayParameters,
} from "../../apps/tube-designer/webpage/productParameterDependencies.mjs";

const templates = fileURLToPath(new URL("../../apps/tube-designer/templates/", import.meta.url));
const template = JSON.parse(readFileSync(resolve(templates,
  "product/single_face_security_window/template.json"), "utf8"));
const fields = productControlFields(template);
const structure = fields.find(field => field.key === "outerFrameStructure");
const bend = fields.find(field => field.key === "outerFrameBendType");
const join = fields.find(field => field.key === "outerFrameConnection");
assert.ok(structure && bend && join, "The current product must declare independent structure, bend and splice controls");
assert.equal(fields.some(field => field.key === "outerFrameFoldLayout"), false, "No duplicate split selector");

function loadTool(id) {
  const packageRoot = resolve(templates, "mold", id), manifest = resolve(packageRoot, "tool.json");
  const descriptor = JSON.parse(readFileSync(manifest, "utf8"));
  const member = resolve(packageRoot, descriptor.kind === "programmatic" ? "tool.py" : "geometry.json");
  const digest = createHash("sha256").update(readFileSync(manifest)).update(Buffer.from([0]))
    .update(readFileSync(member)).update(readFileSync(resolve(templates, "_shared/section_geometry.py"))).digest("hex");
  return { ...descriptor, libraryScope: "system", digest,
    defaultParameters: Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue])) };
}

const tools = [loadTool("v-notch-sharp"), loadTool("edge-arc-groove")];
const view = { tubeDesignerSystemPunchTools: tools };
const publicKeys = new Set(template.parameters.map(field => field.key));
const allowedKeys = new Set([...publicKeys, "tubeDesignerToolBindings", "tubeDesignerProfileOverrides"]);
const manufacturingKeys = manufacturingOnlyParameterKeys(template);
assert.ok(manufacturingKeys.has("frameManufacturingMode"));
assert.ok(manufacturingKeys.has("tubeDesignerToolBindings"));
assert.deepEqual(bend.choices.map(choice => choice.value), ["v-groove", "rounded-v-groove", "edge-arc"]);
for (const field of fields) assert.equal(publicKeys.has(field.key), false,
  `Synthesized UI control ${field.key} must not be a persisted parameter`);

function assertPublic(values) {
  for (const key of Object.keys(values)) assert.ok(allowedKeys.has(key), `Undeclared public key ${key}`);
  for (const field of fields) assert.equal(Object.hasOwn(values, field.key), false,
    `Synthesized UI control ${field.key} leaked into saved parameters`);
}

function choose(values, choice, control = structure) {
  const before = structuredClone(values);
  const next = applyProductControlChoice(view, template, control, values, choice);
  assert.deepEqual(values, before, `${choice} must preserve the previous parameter draft`);
  assertPublic(next);
  assert.equal(productControlValue(template, control, next), choice, `${choice} must round-trip`);
  assert.deepEqual(productDisplayParameters(template, next), productDisplayParameters(template, before),
    `${choice} must change manufacturing inputs without expiring the display model`);
  return next;
}

function initialValues(faceType, sidePosition, tool) {
  const values = { ...Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue])),
    faceType, sidePosition, assemblyPlanningMode: "builtin_rules", frameLayout: "four_sides",
    frameManufacturingMode: "plane_v_notch", frameCornerJoin: "rail_miter",
    frameJoinType: "butt_90", frameButtWrapMode: "horizontal_wraps_side",
    foldedPostJoint: "tabs", foldedPostTabWidth: 12.5, foldedPostTabLength: 8.5,
    tubeDesignerToolBindings: {} };
  for (const key of ["outerFrameGrooveTool", "doorFrameGrooveTool", "doorLeafFrameGrooveTool"]) {
    const field = template.parameters.find(item => item.key === key);
    const binding = makeProductToolBinding(field, tool, null, template);
    // Real, currently inactive resource fields remain a complete saved draft.
    Object.assign(binding.parameters, { bendCompensation: false, kFactor: 0.57,
      maleFemale: false, maleFemaleSize: 1.75 });
    if (tool.id === "v-notch-sharp") Object.assign(binding.parameters,
      { rootSlotPattern: false, centerSlotLength: 9.5, reliefShape: "roundedRectangle" });
    else Object.assign(binding.parameters, { leftArc: true, bottomCut: false, bottomCutWidth: 3.5 });
    assert.equal(Object.keys(binding.parameters).length, tool.parameters.length);
    assert.deepEqual(binding.snapshot.parameterDefinitions, tool.parameters);
    values[key] = binding.selectionKey;
    values.tubeDesignerToolBindings[productToolRole(field)] = binding;
  }
  return values;
}


let spatialCases = 0, rejectedCases = 0, grooveTransitions = 0;
const planarChoices = {single:"plane-single",two:"plane-l",three:"plane-u",five:"plane-frames"};
for (const faceType of ["two","three"]) for (const sidePosition of ["left","right"]) for (const tool of tools) {
  const values = initialValues(faceType,sidePosition,tool);
  const grooveChoice = tool.id === "v-notch-sharp" ? "v-groove" : "edge-arc";
  assert.equal(productControlValue(template,structure,values),planarChoices[faceType]);
  assert.equal(productControlValue(template,bend,values),grooveChoice);
  assert.equal(parameterVisible(join,values),false);
  assert.deepEqual(productControlChoices(template,structure,values).map(choice=>choice.value),
    ["spatial",planarChoices[faceType],"joined"]);
  const spatial = choose(values,"spatial");
  assert.deepEqual(spatial,{...values,frameManufacturingMode:"spatial_v_notch"});
  assert.equal(productControlValue(template,bend,spatial),grooveChoice);
  assert.equal(applyProductControlChoice(view,template,structure,spatial,"spatial"),spatial);
  spatialCases++;
  for(const selected of ["edge-arc","v-groove","rounded-v-groove"]) {
    const changed = choose(spatial,selected,bend);
    assert.equal(changed.frameManufacturingMode,"spatial_v_notch","Changing a bend never changes structure");
    assert.deepEqual(changed.tubeDesignerToolBindings.doorFrameGroove,values.tubeDesignerToolBindings.doorFrameGroove);
    assert.deepEqual(changed.tubeDesignerToolBindings.doorLeafFrameGroove,values.tubeDesignerToolBindings.doorLeafFrameGroove);
    const planar = choose(changed,planarChoices[faceType]);
    assert.deepEqual(planar,{...changed,frameManufacturingMode:"plane_v_notch"});
    assert.equal(productControlValue(template,bend,planar),selected);
    const reSpatial = choose(planar,"spatial");
    assert.deepEqual(reSpatial,{...planar,frameManufacturingMode:"spatial_v_notch"});
    const joined = choose(reSpatial,"joined");
    assert.deepEqual(joined,{...reSpatial,frameManufacturingMode:"segment_weld"});
    assert.equal(parameterVisible(bend,joined),false);
    assert.equal(parameterVisible(join,joined),true);
    assert.deepEqual(join.choices.map(choice=>choice.value),["miter","butt","tabs"]);
    assert.equal(productControlValue(template,bend,joined),"");
    assert.deepEqual(choose(joined,"spatial"),{...joined,frameManufacturingMode:"spatial_v_notch"});
    grooveTransitions++;
  }
  const staleSelector={...values,outerFrameGrooveTool:grooveChoice==="v-groove"?"system:edge-arc-groove":"system:v-notch-sharp"};
  assert.equal(productControlValue(template,bend,staleSelector),grooveChoice);
  assert.deepEqual(choose(staleSelector,"spatial"),{...staleSelector,frameManufacturingMode:"spatial_v_notch"});
}
for(const faceType of ["single","five"])for(const sidePosition of ["left","right"])for(const tool of tools){
  const values=initialValues(faceType,sidePosition,tool),before=structuredClone(values);
  assert.deepEqual(productControlChoices(template,structure,values).map(choice=>choice.value),[planarChoices[faceType],"joined"]);
  assert.throws(()=>applyProductControlChoice(view,template,structure,values,"spatial"),/不适用/);
  assert.deepEqual(values,before);
  assert.notEqual(productControlValue(template,structure,{...values,frameManufacturingMode:"spatial_v_notch"}),"spatial");
  rejectedCases++;
}
for(const faceType of ["single","two","three","five"]){
 const values=initialValues(faceType,"right",tools[0]);
 const labels=productControlChoices(template,structure,values).map(choice=>choice.label);
 assert.ok(labels.includes({single:"折弯",two:"上下 L 型折弯＋拼接",three:"上下 U 型折弯＋拼接",five:"上下框折弯＋拼接"}[faceType]));
}
console.log(`SpatialFoldProductControlTest passed: ${spatialCases} spatial variants, ${grooveTransitions} structure-preserving groove transitions, ${rejectedCases} inapplicable variants.`);
