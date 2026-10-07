import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { buildMaterialProfileGroups, buildProfileGroups } from "../../apps/tube-designer/webpage/partsArea.mjs";

const path = process.argv[2] || fileURLToPath(new URL("../../../output/tests/identical-manufacturing-parts/section-identities.json", import.meta.url));
const snapshot = JSON.parse(readFileSync(path, "utf8"));
const parts = snapshot.parts;
assert.equal(parts.length, 20, "The native fixture contains the twenty actual manufacturing part types");
const sections = buildProfileGroups(parts);
assert.equal(sections.length, 7, "The original 41 physical stocks use seven real sections, without traversal/source ID duplicates");
assert.equal(sections.reduce((count, group) => count + group.quantity, 0), 41);
assert.equal(buildMaterialProfileGroups(parts).length, 7);
const twenties = sections.filter(group => group.parts[0].profile.width === 20);
assert.equal(twenties.length, 2, "20×20 frame and rail have genuinely different inner wall/radius geometry");
assert.deepEqual(new Set(twenties.map(group => group.parts[0].profile.wallThickness)), new Set([1, 0.8]));
assert.deepEqual(new Set(twenties.map(group => group.parts[0].profile.cornerRadius)), new Set([1.5, 1]));
assert.equal(new Set(twenties.map(group => group.profile)).size, 2, "The shared native specification exposes their actual difference");
assert.ok(twenties.every(group => group.profile.includes("壁厚") && group.profile.includes("R")));
assert.equal(sections.filter(group => group.parts[0].profile.width === 38).length, 1);
assert.equal(sections.filter(group => group.parts[0].profile.width === 25).length, 1);
for (const group of sections) {
  assert.ok(group.parts.every(part => part.profile.sectionIdentity === group.key));
  assert.ok(group.parts.every(part => !Object.hasOwn(part.profile.parameters, "sectionResource")));
}
console.log("TubeDesigner real native section fixture passed: 41 stocks / 20 part types / 7 physical sections and shared complete labels.");
