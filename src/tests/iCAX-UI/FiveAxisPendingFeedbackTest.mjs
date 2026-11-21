import test from 'node:test';
import assert from 'node:assert/strict';
import {pendingFeedbackTitle} from '../../apps/_shared/workbench/layout/commonViews.mjs';
import {renderOverlay} from '../../apps/five-axis-cam/webpage/workspaceViews.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {workspaceAlerts,renderWorkspaceAlerts} from '../../apps/five-axis-cam/webpage/alerts.mjs';
import {operationUnavailableReason,renderBlockingOperation} from '../../apps/five-axis-cam/webpage/operationFeedback.mjs';
import {workpieceSelectionBusy} from '../../apps/five-axis-cam/webpage/workpieceSelection.mjs';
import {blockingProgress} from '../../apps/five-axis-cam/webpage/blockingProgress.mjs';

const context={activeRibbonTabId:'simulation',pendingPresentation:'explicit'};
test('navigation handoffs keep menu clicks available while manual switches and geometry work remain protected',()=>{
 const view={pending:true,fiveWorkpieceSwitching:true};
 assert.ok(blockingProgress(view),'A manual workpiece change retains its modal protection');
 for(const flag of ['fiveOperationNavigationPreparing','fiveOperationAreaSync']){
  view[flag]=true;
  assert.equal(blockingProgress(view),null,'Only the navigation handoff stays inline');
  view.fixtureRequest={};assert.ok(blockingProgress(view),'Fixture work must still block conflicting actions');delete view.fixtureRequest;
  view.fiveAxisImportActive=true;assert.match(blockingProgress(view).title,/导入/);delete view.fiveAxisImportActive;
  delete view[flag];
 }
});
function fixture(){
 const view={fiveAxisArea:'simulation',pending:true,scene:{model:{isLoaded:true},machines:[],intentToolpaths:[],domain:{placements:[],workpieces:[],setups:[]}}};
 selectTool(view,'machine-instances');return view;
}

test('ordinary edit/history readbacks remain busy without a misleading state-read notification',()=>{
 const view=fixture();assert.equal(pendingFeedbackTitle(context,view),'');
 assert.doesNotMatch(renderOverlay(context,view),/正在读取工程状态|data-five-key="feedback"/);
 assert.equal(view.pending,true,'the native-operation gate is retained');
});

test('the shared policy retains first-load feedback and the default presentation for other products',()=>{
 assert.equal(pendingFeedbackTitle(context,{pending:true,scene:null}),'正在读取工程状态…');
 assert.equal(pendingFeedbackTitle({},fixture()),'正在读取工程状态…');
 assert.equal(pendingFeedbackTitle(context,{pending:false,progress:{title:'Named task'}}),'');
});

test('named long-task feedback remains visible and readback errors stay in the generic alert overview',()=>{
 const view=fixture();view.progress={title:'正在导入机床'};
 assert.match(renderOverlay(context,view),/role="status">正在导入机床/);
 view.progress=null;view.error='刷新项目状态失败';
 assert.equal(workspaceAlerts(view).items.find(item=>item.id==='operation').detail,'刷新项目状态失败');
 view.fiveAlertsOpen=true;assert.match(renderWorkspaceAlerts(view),/刷新项目状态失败/);
 assert.equal(view.error,'刷新项目状态失败');
});

test('support parameter calculation gates conflicting actions without creating a global pending overlay',()=>{
 const view=fixture();view.pending=false;view.fiveFixtureDesignRequest={kind:'layout',title:'正在计算支撑板数量…'};
 assert.equal(workpieceSelectionBusy(view),true);
 assert.match(operationUnavailableReason(view),/正在计算支撑板数量/);
 assert.equal(pendingFeedbackTitle(context,view),'');
 assert.equal(renderBlockingOperation(view),'');
 assert.equal(view.pending,false);
 view.fiveFixtureDesignRequest=null;
 assert.equal(workpieceSelectionBusy(view),false);
 assert.equal(operationUnavailableReason(view),'');
});
