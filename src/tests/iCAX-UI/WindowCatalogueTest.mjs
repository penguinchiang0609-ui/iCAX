import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {buildCatalogEntries} from "../../apps/tube-designer/webpage/productCatalog.mjs";
import {matchesParameterCondition as matches} from "../../apps/tube-designer/webpage/parameterConditions.mjs";
const read = n => ({...JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/product/" + n + "/template.json", import.meta.url), "utf8")), available: true});
const security = read("single_face_security_window");
const all = [security, read("louver_window")];
assert.deepEqual(buildCatalogEntries(all).map(t => t.catalogPath), [["窗", "防盗窗"], ["窗", "百叶窗"]]);
const defaults = Object.fromEntries(security.parameters.map(p => [p.key, p.defaultValue]));
function shown(key, faceType) {return matches(security.parameters.find(p => p.key === key).visibleWhen, {...defaults, faceType, accessDoorEnabled: true});}
for (const face of ["single", "two", "three", "five"]) {
  assert.equal(shown("sideWidth", face), face === "two");
  assert.equal(shown("leftWidth", face), face === "three");
  assert.equal(shown("depth", face), face === "five");
  assert.equal(shown("topBottomRodMaximumCenterSpacing", face), face === "five");
  for (const [type, key] of [["two", "accessDoorFace2"], ["three", "accessDoorFace3"], ["five", "accessDoorFace5"]]) assert.equal(shown(key, face), face === type);
}
console.log("Window catalogue and face-dependent fields passed.");
const louver=read("louver_window");
const lv=Object.fromEntries(louver.parameters.map(p=>[p.key,p.defaultValue]));
const visible=(key,changes)=>matches(louver.parameters.find(p=>p.key===key).visibleWhen,{...lv,...changes});
for(const mode of ["pitch","gap","overlap","count"])
  for(const [key,active]of [["bladePitch","pitch"],["bladeGap","gap"],["bladeOverlap","overlap"],["bladeCount","count"]])
    assert.equal(visible(key,{arrayMode:mode}),mode===active);
for(const mode of ["face_weld","slot_insert","through_insert"]){
  assert.equal(visible("insertDepth",{bladeConnection:mode}),mode==="slot_insert");
  assert.equal(visible("endClearance",{bladeConnection:mode}),mode==="face_weld");
  assert.equal(visible("throughExtension",{bladeConnection:mode}),mode==="through_insert");
  assert.equal(visible("slotClearance",{bladeConnection:mode}),mode!=="face_weld");
}
assert.equal(visible("slotClearance",{middlePostCount:1,supportMode:"through"}),true);
assert.equal(visible("supportMode",{middlePostCount:1,bladeConnection:"through_insert"}),false);
console.log("Louver array/connection/support conditions passed.");
