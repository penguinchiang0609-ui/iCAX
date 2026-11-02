import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {inspectStraightBoundary} from './stage1_edge_evidence.mjs';

const data=JSON.parse(readFileSync(new URL('../../../../output/tests/cut-unit-stage1-20261025/cases/003/view.json',import.meta.url),'utf8'));
const f=data.faces.find(f=>f.faceId===3),g=f.groups.find(g=>g.key==='W1:G1');
const evidence=inspectStraightBoundary(f,g,data.faces);
assert.equal(evidence.matches.length,1);
assert.equal(evidence.matches[0].faceId,1);
assert.deepEqual(evidence.matches[0].sourceEdges,[1]);
assert.deepEqual(g.sourceEdges,[179]); // Detached topology, matching geometry.
assert.ok(evidence.matches[0].mismatchMm<1e-8);
assert.ok(evidence.matches[0].areaMm2>.03&&evidence.matches[0].areaMm2<.031); // Source face, not assembled unit area.
assert.ok(Math.abs(evidence.length-2.40273260318315)<1e-10);
assert.ok(Math.abs(evidence.matches[0].profile.height-.0189952869478569)<1e-6);

const moved=structuredClone(data),transform=([x,y,z])=>[-z+50,x-100,y+17];
for(const face of moved.faces)for(const group of face.groups){group.start=transform(group.start);group.end=transform(group.end);group.points=group.points.map(transform);}
const mf=moved.faces.find(f=>f.faceId===3),mg=mf.groups.find(g=>g.key==='W1:G1');
const rotated=inspectStraightBoundary(mf,mg,moved.faces);
assert.equal(rotated.matches[0].faceId,1);
assert.ok(Math.abs(rotated.length-evidence.length)<1e-10);
assert.ok(Math.abs(rotated.matches[0].profile.height-evidence.matches[0].profile.height)<1e-10);

const different=structuredClone(data);
different.faces[0].groups.find(g=>g.key==='W1:G2').end[1]+=.01;
assert.equal(inspectStraightBoundary(f,g,different.faces).matches.length,0);
assert.equal(inspectStraightBoundary(f,{...g,geometry:'Curve'},data.faces),null);
console.log('PASS: actual E179/E1 correspondence, cap dimensions, rigid transform, nonmatching endpoints, non-line exclusion');
