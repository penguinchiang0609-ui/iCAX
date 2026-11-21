import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {loadWorkData,renderWorkDetails} from '../workInspectionData.mjs';

const args=process.argv.slice(2),files=[];
let outputFile='output/tests/skin-path-evidence-20261109/viewer/saved-output-validation.json';
for(let i=0;i<args.length;++i){if(args[i]==='--output'){assert.ok(args[i+1],'--output requires a report path');outputFile=args[++i];}else files.push(args[i]);}
assert.ok(files.length,'Pass saved FaceCutCoverage JSON paths; this validates presentation of real native output.');
const results=[];
for(const file of files){
 const bytes=fs.readFileSync(file),saved=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));let reads=0;
 assert.equal(saved.stage,'faceCutCoverage',file);assert.equal(saved.schema,3);assert.equal(saved.coverageDomain,'source-skin-native-intervals');
 const entry={status:saved.success?(saved.complete?'passed':'partial'):'failed',dataUrl:file,geometryUrl:file,identityGroup:'classified-id',artifacts:[]};
 const data=await loadWorkData('faceCutCoverage',entry,async()=>{reads++;return saved;});
 assert.equal(reads,1);assert.equal(data.faces.length,saved.faces.length);assert.equal(data.geometry.faces.length,saved.geometry.faces.length);
 assert.equal(data.edges.filter(edge=>edge.workReference).length,saved.referenceEdges.length);assert.equal(data.edges.filter(edge=>edge.workCoverage).length,saved.coverage.length);
 const keys=new Set();
 for(const edge of data.edges){assert.ok(!keys.has(edge.pathKey),`Unique record ${edge.pathKey}`);keys.add(edge.pathKey);assert.ok((edge.faceIds??[]).every(id=>data.faces.some(face=>String(face.faceId)===String(id))),`Reference owner exists ${edge.pathKey}`);}
 for(const span of saved.coverage){
  const edge=data.edges.find(edge=>edge.pathKey===span.key);assert.ok(edge);assert.equal(edge.faceId,String(span.faceId));assert.equal(edge.analysisRecord.firstParameter,span.firstParameter);assert.equal(edge.analysisRecord.lastParameter,span.lastParameter);assert.deepEqual(edge.analysisRecord.sourcePathKeys,span.sourcePathKeys);
  const detail=renderWorkDetails(data,{selectedEdge:span.key});assert.ok(detail.includes('原生参数区间'));assert.ok(detail.includes('区间判定证据'));assert.ok(detail.includes('到达证据'));assert.ok(!detail.includes('arrival[')||!detail.includes('缺失；保留原始索引'));
  assert.equal(edge.sourceEdgeIds[0],String(span.sourceEdgeId));
  for(const index of span.evidenceIndices){const evidence=saved.evidence[index];assert.ok(evidence);assert.equal(evidence.sourcePathKey,span.sourcePathKey);assert.equal(String(evidence.faceId),String(span.faceId));}
  for(const sourceKey of span.baselineSourcePathKeys){assert.ok(saved.sources.some(source=>source.sourcePathKey===sourceKey&&String(source.faceId)===String(span.faceId)));}
  for(const index of span.arrivalIndices??[]){assert.ok(saved.arrivals[index]);assert.equal(String(saved.arrivals[index].faceId),String(span.faceId));}
 }
 for(const face of data.faces){const detail=renderWorkDetails(data,{selectedFace:face.faceId});assert.ok(detail.includes('原面编号'));}
 assert.deepEqual(fs.readFileSync(file),bytes,'Read-only validation did not change native JSON');
 results.push({file:path.resolve(file),faces:data.faces.length,meshFaces:data.geometry.faces.length,machiningFaces:data.faces.filter(face=>face.machining).length,referenceEdges:saved.referenceEdges.length,coverageSpans:saved.coverage.length,through:saved.counts.throughSpans,partial:saved.counts.partialSpans,pending:saved.counts.pendingSpans,status:'passed'});
}
const output=path.resolve(outputFile);fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify({status:'passed',models:results.length,browserInteractionTested:false,results},null,2));console.log(JSON.stringify({status:'passed',models:results.length,report:output,results},null,2));
