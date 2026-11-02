import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';

const [beforeDirectory,afterDirectory]=process.argv.slice(2);
assert.ok(beforeDirectory&&afterDirectory,'Provide baseline and optimized output directories');
const read=(directory,file)=>JSON.parse(readFileSync(resolve(directory,file),'utf8'));
// Each run deliberately creates new identities. Compare the exact authored and
// calculated values after removing only these identities and derived hashes.
// Preserve all numeric geometry, ranges, controls, policies and source roles.
const volatile=new Set(['fingerprint','definitionFingerprint','readFingerprint','algorithmReadFingerprint','contextFingerprint','observedGeometryFingerprint','revision','inputDigest','sourceKey','candidateId','userNumber','sourceGeometryRef','sourceGeometry','curveRef','featureRef','processRef','poseRef','sourceRecipeRef']);
const normalize=value=>{
 if(typeof value==='string')return value.replace(/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/gi,'UUID');
 if(Array.isArray(value))return value.map(normalize);
 if(value&&typeof value==='object')return Object.entries(value).filter(([key])=>!volatile.has(key)).map(([key,child])=>[normalize(key),normalize(child)]).sort((a,b)=>JSON.stringify(a).localeCompare(JSON.stringify(b)));
 return value;
};
const definitions=directory=>read(directory,'definitions.json').map(({id,entityId,setupId,placementId,intentPort,number,...row})=>normalize(row)).sort((a,b)=>JSON.stringify(a).localeCompare(JSON.stringify(b)));
const results=directory=>{
 const snapshot=read(directory,'operations.json'),types=new Map(snapshot.operations.map(op=>[op.id,op.definition.typeId]));
 return snapshot.derived.filter(entry=>entry.data.kind==='operationResult').map(({data})=>({
  type:types.get(data.operationId),state:data.state,evaluationRevision:data.evaluationRevision,
  outputs:data.outputs?.map(output=>normalize(output.value)),
  constraints:normalize(data.constraints),excludedRanges:normalize(data.excludedRanges),
 })).sort((a,b)=>JSON.stringify(a).localeCompare(JSON.stringify(b)));
};
const before=read(beforeDirectory,'report.json'),after=read(afterDirectory,'report.json');
assert.ok(before.passed&&after.passed);assert.equal(after.sourceHash,before.sourceHash);assert.ok(before.sourceHash);
assert.equal(after.createdCount,before.createdCount);assert.equal(after.faceCount,before.faceCount);
const compare=(name,a,b)=>{const digest=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');if(digest(a)!==digest(b)){writeFileSync(resolve(afterDirectory,name+'-before.json'),JSON.stringify(a,null,2));writeFileSync(resolve(afterDirectory,name+'-after.json'),JSON.stringify(b,null,2));}assert.equal(digest(a),digest(b),name+' changed; inspect saved normalized snapshots');};
compare('projection',definitions(beforeDirectory),definitions(afterDirectory));
compare('exact-results',results(beforeDirectory),results(afterDirectory));
const stages=['create','definitionTask','readDefinitions'],sum=report=>stages.reduce((total,key)=>total+report.stages[key],0);
const comparison={passed:true,cuts:after.createdCount,sourceHash:after.sourceHash,beforeMs:sum(before),afterMs:sum(after),improvementPercent:100*(1-sum(after)/sum(before)),stages:Object.fromEntries(stages.map(key=>[key,{beforeMs:before.stages[key],afterMs:after.stages[key]}]))};
writeFileSync(resolve(afterDirectory,'comparison.json'),JSON.stringify(comparison,null,2));console.log(JSON.stringify(comparison,null,2));
