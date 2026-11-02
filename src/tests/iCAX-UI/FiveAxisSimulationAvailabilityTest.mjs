import assert from 'node:assert/strict';
import test from 'node:test';
import {commandAvailability} from '../../apps/five-axis-cam/webpage/commandAvailability.mjs';
import {blockingOperation} from '../../apps/five-axis-cam/webpage/operationFeedback.mjs';
const fixture=()=>{
 const cut={id:'cut',setupId:'setup',machiningKind:'Cut',enabled:true};
 const setup={id:'setup',generatedGeometry:{status:'Current',operations:[{intentId:'cut'}]}};
 const view={scene:{activePlacementId:'placement',domain:{activeSetupId:'setup',workpieces:[{id:'workpiece',geometryRef:{url:'resource://workpiece'}}],placements:[{id:'placement',setupId:'setup',workpieceId:'workpiece'}],setups:[setup]},intentToolpaths:[cut]}};
 return {view,setup,cut};
};
test('simulation and collision remain available for enabled authored paths with stale or missing calculations',()=>{
 const {view,setup}=fixture();for(const tool of ['simulation','collision'])for(const status of ['Current','Stale','Missing']){setup.generatedGeometry={status,operations:[]};assert.equal(commandAvailability(view,'five.tool.'+tool).disabled,false,status+' '+tool);}
});
test('disabled or absent definitions and an active save still prevent launching simulation or collision',()=>{
 for(const tool of ['simulation','collision']){const {view,cut}=fixture();cut.enabled=false;assert.equal(commandAvailability(view,'five.tool.'+tool).disabled,true);cut.enabled=true;view.fiveToolAxisRequest={};assert.equal(commandAvailability(view,'five.tool.'+tool).disabled,true);view.fiveToolAxisRequest=null;view.scene.intentToolpaths=[];assert.equal(commandAvailability(view,'five.tool.'+tool).disabled,true);}
});
test('keypoint save, lookup and receipt recovery retain local feedback without a modal or a duplicate retry',()=>{
 const request={commandId:'pose'},view={fiveToolAxisRequest:request,fiveAxisPointEdit:{request},fiveAxisPointSaving:request};assert.equal(blockingOperation(view).inline,true);assert.equal(blockingOperation(view).retry,undefined);view.fiveAxisPointSaving=null;assert.equal(blockingOperation(view).inline,true);view.fiveToolAxisRequest=null;view.fiveAxisPointEdit.request=null;view.fiveAxisPointLookup={phase:'committing'};assert.equal(blockingOperation(view).inline,true);view.fixtureRequest={};assert.notEqual(blockingOperation(view).inline,true);view.fixtureRequest=null;view.sceneRefreshPending=true;assert.notEqual(blockingOperation(view).inline,true);
});
