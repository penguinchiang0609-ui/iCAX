import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {prepareModelStage,publishModelStage,validatePublishDirectory} from '../../../../../tools/testing/run_machining_edge_analysis.mjs';

const root=fs.mkdtempSync(path.join(os.tmpdir(),'icax-edge-runner-'));
const staging=path.join(root,'staging'),publish=path.join(root,'published','EdgeAnalysis');
const part={group:'fixture.STEP',id:'part-1'},groups=[part.group];
const base=path.join(staging,'EdgeAnalysis',part.group,part.id);
const put=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,value);};
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const core=()=>({schema:1,stage:'edgeAnalysis',name:part.group,success:true,complete:true,
  faces:[{faceId:'17'}],edges:[{edgeId:'42',skinRelation:'OnSkin',convexity:'Free'}],diagnostics:[],
  counts:{faces:1,edges:1,unresolvedSkin:0,unresolvedConvexity:0,skin:{OnSkin:1},convexity:{Free:1}}});
try{
  const invalidPublish=path.join(root,'wrong');
  assert.throws(()=>publishModelStage({publish:invalidPublish,staging,part,groups,row:{status:'failed'}}),/--publish must name/);
  assert.ok(!fs.existsSync(invalidPublish),'Invalid publish path is rejected before any writes.');
  validatePublishDirectory(publish);

  for(const suffix of ['.step','.json','.brep','.html'])put(base+suffix,'stale stage');
  const priorReport=path.join(staging,'Reports',part.group,part.id+'.json');put(priorReport,'stale report');
  const unrelated=path.join(staging,'Classify',part.group,part.id+'-machining.step');put(unrelated,'original Classify');
  const otherModel=path.join(staging,'EdgeAnalysis','other.STEP',part.id+'.json');put(otherModel,'other model');
  assert.equal(prepareModelStage(staging,part),priorReport);
  for(const suffix of ['.step','.json','.brep','.html'])assert.ok(!fs.existsSync(base+suffix));
  assert.ok(!fs.existsSync(priorReport));assert.equal(fs.readFileSync(unrelated,'utf8'),'original Classify');
  assert.equal(fs.readFileSync(otherModel,'utf8'),'other model');

  for(const suffix of ['step','json','brep','html'])put(path.join(publish,'fixture.'+suffix),'previous success '+suffix);
  put(base+'.brep','incomplete native BREP');
  const failedRow={id:1,status:'failed',stage:'classify',error:'Classify rejected the model',sourceUnchanged:true};
  const failedModel=publishModelStage({publish,staging,part,groups,row:failedRow});
  assert.equal(failedModel.status,'failed');assert.equal(failedModel.faceCount,0);
  const failure=read(failedRow.artifacts.edgeAnalysisData);
  assert.equal(failure.success,false);assert.equal(failure.complete,false);assert.equal(failure.status,'failed');
  assert.deepEqual(failure.faces,[]);assert.deepEqual(failure.edges,[]);assert.ok(failure.diagnostics.some(message=>message.includes('Classify rejected')));
  for(const suffix of ['step','brep','html'])assert.ok(!fs.existsSync(path.join(publish,'fixture.'+suffix)));
  for(const suffix of ['step','json','brep','html'])assert.equal(fs.readFileSync(path.join(root,'published','Reports/previous-artifacts/EdgeAnalysis','fixture.'+suffix),'utf8'),'previous success '+suffix);
  assert.equal(fs.readFileSync(base+'.brep','utf8'),'incomplete native BREP','Untrusted partial geometry remains diagnostic only.');

  prepareModelStage(staging,part);
  put(base+'.json',JSON.stringify(core()));put(base+'.brep','current complete BREP');
  const exportFailedRow={id:1,status:'failed',stage:'edgeAnalysis',error:'STEP style coverage failed',sourceUnchanged:true};
  const exportFailedModel=publishModelStage({publish,staging,part,groups,row:exportFailedRow});
  assert.equal(exportFailedModel.status,'failed');assert.equal(exportFailedModel.faceCount,1);
  const currentFailure=read(exportFailedRow.artifacts.edgeAnalysisData);
  assert.equal(currentFailure.success,true,'Core success stays independent of the export result.');
  assert.equal(currentFailure.status,'failed');assert.equal(currentFailure.execution.error,'STEP style coverage failed');
  assert.equal(fs.readFileSync(exportFailedRow.artifacts.edgeAnalysisBrep,'utf8'),'current complete BREP');
  assert.ok(!Object.hasOwn(exportFailedRow.artifacts,'edgeAnalysis'),'No old STEP is republished.');

  prepareModelStage(staging,part);put(base+'.json',JSON.stringify(core()));put(base+'.brep','next BREP');put(base+'.step','next STEP');
  const passedRow={id:1,status:'passed',stage:'edgeAnalysis',sourceUnchanged:true,code:0};
  const passedModel=publishModelStage({publish,staging,part,groups,row:passedRow});
  assert.equal(passedModel.status,'passed');assert.equal(passedModel.edgeCount,1);assert.equal(passedModel.dataUrl,'/data/fixture.json');
  assert.equal(read(passedRow.artifacts.edgeAnalysisData).execution.status,'passed');
  assert.equal(fs.readFileSync(passedRow.artifacts.edgeAnalysis,'utf8'),'next STEP');

  prepareModelStage(staging,part);
  const missingRow={id:1,status:'passed',stage:'edgeAnalysis',sourceUnchanged:true,code:0};
  const missingModel=publishModelStage({publish,staging,part,groups,row:missingRow});
  assert.equal(missingModel.status,'failed');assert.equal(read(missingRow.artifacts.edgeAnalysisData).success,false);
  assert.ok(!fs.existsSync(path.join(publish,'fixture.step')));assert.ok(!fs.existsSync(path.join(publish,'fixture.brep')));
  console.log('RunMachiningEdgeAnalysisTest: passed (isolated cleanup, early validation, failed rerun archive, visible failures, current core preservation, missing JSON rejection)');
}finally{
  const target=fs.realpathSync(root),parent=fs.realpathSync(os.tmpdir());
  if(path.dirname(target)!==parent||!path.basename(target).startsWith('icax-edge-runner-'))throw Error('Unexpected cleanup target');
  fs.rmSync(target,{recursive:true});
}
