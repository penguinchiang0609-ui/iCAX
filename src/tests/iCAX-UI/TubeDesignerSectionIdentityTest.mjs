import assert from "node:assert/strict";
import { buildMaterialProfileGroups, buildProfileGroups } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { nestingSectionIdentity } from "../../apps/tube-designer/webpage/profileIdentity.mjs";
import { nativeSectionIdentity } from "./fixtures/nestingSectionIdentity.mjs";

const part = (id, identity, material = "304", specification = "20 × 20 × R1.5 × 壁厚 1") => ({
  entityId: id, length: 1000, quantity: 1,
  profile: { sectionIdentity: identity, displayName: "方管", specification,
    width: 20, depth: 20, wallThickness: 1, cornerRadius: 1.5 },
  properties: { "manufacturing.partKind": "tube", "manufacturing.material": material },
});
const original = part("vertical", nativeSectionIdentity("section-20-20-r1.5-t1"));
const sameGeometry = structuredClone(original);
sameGeometry.entityId = "horizontal";
Object.assign(sameGeometry.profile, { id: "different-business-id", displayName: "名称修改",
  specification: "另一显示名称", packageVersion: "new", sectionResource: "another-resource",
  parameters: { sectionResource: "different-resource", source: "independent-copy" } });
assert.equal(buildProfileGroups([original, sameGeometry]).length, 1,
  "Every pane consumes the native physical identity, independently of names and source metadata");
assert.equal(buildMaterialProfileGroups([original, sameGeometry]).length, 1,
  "Renaming or staging a profile must not split the same material's stock list");
assert.equal(buildMaterialProfileGroups([original,
  { ...sameGeometry, properties: { ...sameGeometry.properties, "manufacturing.material": "Q235B" } }]).length, 2,
"Materials remain distinct in the material list");

const thin = part("thin-horizontal", nativeSectionIdentity("section-20-20-r1-t0.8"), "304", "20 × 20 × R1 × 壁厚 0.8");
assert.equal(buildProfileGroups([original, thin]).length, 2,
  "Equal outside dimensions retain the native distinction for their actual inner curves and radii");
assert.equal(buildMaterialProfileGroups([original, thin]).length, 2);
const sameLabelDifferentGeometry = part("different-inner-loop", nativeSectionIdentity("another-actual-inner-loop"));
assert.equal(buildProfileGroups([original, sameLabelDifferentGeometry]).length, 2,
  "Matching names and every displayed scalar cannot override different real contour identity");
assert.throws(() => nestingSectionIdentity({ width: 20, depth: 20 }), /截面身份/);
assert.throws(() => nestingSectionIdentity({ sectionIdentity: '{"schema":"wrong","contours":["x"]}' }), /截面身份/);
assert.equal(nestingSectionIdentity(original.profile), original.profile.sectionIdentity);
console.log("TubeDesigner section identity UI tests passed: native authority, consistent material/profile grouping, strict current contract.");
