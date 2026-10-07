import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { parameterVisible, parameterEnabled } from '../../apps/tube-designer/webpage/parameterConditions.mjs';

const root=fileURLToPath(new URL('../../../',import.meta.url));
const relative='src/apps/tube-designer/templates/product/single_face_security_window/template.json';
const path=root+relative;
const digest=()=>createHash('sha256').update(readFileSync(path)).digest('hex');
const start=digest();
const descriptor=JSON.parse(readFileSync(path,'utf8'));
const byKey=new Map(descriptor.parameters.map(parameter=>[parameter.key,parameter]));
const defaults=Object.fromEntries(descriptor.parameters.map(parameter=>[parameter.key,parameter.defaultValue]));
function validComparisons(condition){
  if(condition.conditions){condition.conditions.forEach(validComparisons);return;}
  const source=byKey.get(condition.parameter);
  assert.ok(source,`Missing condition parameter ${condition.parameter}`);
  const choices=(source.choices||[]).map(choice=>choice.value);
  if(choices.length)assert.ok(choices.includes(condition.value),`Invalid enum condition ${condition.parameter}=${condition.value}`);
}
for(const key of ['horizontalBranchReserve','verticalBranchReserve'])validComparisons(byKey.get(key).visibleWhen);
const rows=[];
for(const faceType of ['single','two','three','five'])for(const assemblyPlanningMode of ['legacy_processed','external_templates'])
for(const infillPattern of ['horizontal','vertical','grid'])for(const key of ['horizontalBranchReserve','verticalBranchReserve']){
  const values={...defaults,faceType,assemblyPlanningMode,infillPattern},before=structuredClone(values);
  const activeMode=assemblyPlanningMode==='legacy_processed'||faceType!=='single';
  const expectedVisible=activeMode&&(faceType==='five'||infillPattern===(key==='horizontalBranchReserve'?'horizontal':'vertical')||infillPattern==='grid');
  const visible=parameterVisible(byKey.get(key),values),enabled=parameterEnabled(byKey.get(key),values);
  assert.equal(visible,expectedVisible,`${faceType}/${assemblyPlanningMode}/${infillPattern}/${key}`);
  assert.deepEqual(values,before);
  rows.push({faceType,assemblyPlanningMode,infillPattern,key,visible,enabled,expectedVisible,hostInputsUnchanged:true});
}
assert.equal(digest(),start);
const report={schema:'icax.all-products-recheck-window-orientation-conditions',readOnly:true,
  sharedEvaluator:'src/apps/tube-designer/webpage/parameterConditions.mjs',sourcePath:relative,
  sourceDigest:start,sourceUnchanged:true,allConditionEnumValuesDeclared:true,rows,passed:rows.length,failed:0,
  nativeVerified:false};
writeFileSync(root+'output/tests/assembly-process/all-products-recheck-window-orientation-conditions-after.json',JSON.stringify(report,null,2));
console.log(`PASS ${rows.length} actual shared condition evaluations; native verification pending`);
