import test from 'node:test';
import assert from 'node:assert/strict';
import {scopedExtractionCandidates,validExtractionScope} from '../../apps/five-axis-cam/webpage/extractionScope.mjs';
const candidates=[{candidateId:'hole',boundaryRole:'Opening',geometryKind:'GeneralContour'},{candidateId:'outside',boundaryRole:'OuterTrim',geometryKind:'Circle'},{candidateId:'mark',boundaryRole:'Marking'},{candidateId:'unknown',boundaryRole:'Unknown'}];
test('recognition scope uses native boundary roles and includes non-circular holes',()=>{
 assert.deepEqual(scopedExtractionCandidates(candidates,'inner').map(c=>c.candidateId),['hole']);
 assert.deepEqual(scopedExtractionCandidates(candidates,'outer').map(c=>c.candidateId),['outside']);
 assert.deepEqual(scopedExtractionCandidates(candidates,'all').map(c=>c.candidateId),['hole','outside']);
 assert.equal(validExtractionScope('all'),true);assert.equal(validExtractionScope('bogus'),false);
 assert.equal(validExtractionScope('none'),true);assert.deepEqual(scopedExtractionCandidates(candidates,'none'),[]);
});
test('native loop nesting covers sheet boundaries without turning markings into holes',()=>{
 const sheet=[{boundaryRole:'PatchBoundary',boundary:{regionRelation:'InnerLoop'}},{boundaryRole:'PatchBoundary',boundary:{regionRelation:'OuterLoop'}},{boundaryRole:'Marking',boundary:{regionRelation:'InnerLoop'}}];
 assert.deepEqual(scopedExtractionCandidates(sheet,'inner'),[sheet[0]]);assert.deepEqual(scopedExtractionCandidates(sheet,'outer'),[sheet[1]]);assert.deepEqual(scopedExtractionCandidates(sheet,'all'),sheet.slice(0,2));
});
