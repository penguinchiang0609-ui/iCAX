import test from 'node:test';
import assert from 'node:assert/strict';
import {switchCamPage,workspaceState} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {camPageForTool,camRibbonWorkspace} from '../../apps/five-axis-cam/webpage/camWorkspace.mjs';
import {parameterOverrides} from '../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';
import {normalizeRibbonWorkspace,renderRibbonWorkspace,ribbonWorkspaceCommand} from '../../iCAX-UI/SDK/AppShell/app/ribbonWorkspace.mjs';

test('process and ordering retain the current cut selection; rapid restores it without replaying a previous selection',()=>{
  const view={fiveCamPage:'cutting',scene:{domain:{activeSetupId:'setup'},intentToolpaths:[{id:'a',number:'1'},{id:'b',number:'2'}]}};
  const ui=workspaceState(view);ui.selectedIntentIds=['a'];ui.selectedIntentId='a';
  switchCamPage(view,'rapid');assert.deepEqual(ui.selectedIntentIds,[]);
  switchCamPage(view,'process');assert.deepEqual(ui.selectedIntentIds,['a']);
  ui.selectedIntentIds=['b'];ui.selectedIntentId='b';
  switchCamPage(view,'order');assert.deepEqual(ui.selectedIntentIds,['b']);
  switchCamPage(view,'cutting');assert.deepEqual(ui.selectedIntentIds,['b']);
});
test('preview and collision stay in their current CAM section',()=>{
  for(const page of ['cutting','rapid','process','order']){
    assert.equal(camPageForTool({fiveCamPage:page},'simulation'),page);
    assert.equal(camPageForTool({fiveCamPage:page},'collision'),page);
  }
  assert.equal(camPageForTool({},'compensation'),'process');
  assert.equal(camPageForTool({},'manual-order'),'order');
});
test('editing speed preserves other per-path overrides and blank speed restores only its inherited value',()=>{
  const assignment={overrides:[{parameterId:'feedMmPerSecond',value:20},{parameterId:'laserPowerW',value:700}]};
  const edited=parameterOverrides({cutFeed:'35'},assignment,['cutFeed']);
  assert.deepEqual(edited,[{parameterId:'feedMmPerSecond',value:35},{parameterId:'laserPowerW',value:700}]);
  assert.deepEqual(parameterOverrides({cutFeed:''},{overrides:edited},['cutFeed']),[{parameterId:'laserPowerW',value:700}]);
  assert.throws(()=>parameterOverrides({cutFeed:'-1'},assignment,['cutFeed']));
});
test('independent workpiece and pose choices only list poses owned by the active workpiece',()=>{
  const view={scene:{activePlacementId:'p1',domain:{activeSetupId:'s1',workpieces:[{id:'w1',name:'工件甲'},{id:'w2',name:'工件乙'}],placements:[{id:'p1',workpieceId:'w1',setupId:'s1'},{id:'p2',workpieceId:'w1',setupId:'s2'},{id:'p3',workpieceId:'w2',setupId:'s3'}],setups:[{id:'s1',name:'位姿一'},{id:'s2',name:'位姿二'},{id:'s3',name:'位姿三'}]}}};
  const bar=camRibbonWorkspace(view,'toolpath');
  assert.deepEqual(bar.selectors.map(s=>s.label),['工件','位姿']);
  assert.deepEqual(bar.selectors[0].options.map(o=>o.value),['w1','w2']);
  assert.deepEqual(bar.selectors[1].options.map(o=>o.value),['p1','p2']);
  assert.equal(bar.pages.length,4);
});
test('shared ribbon selectors escape labels and refuse unknown or disabled choices',()=>{
  const workspace=normalizeRibbonWorkspace({selectors:[{id:'object',label:'工件',value:'a',options:[{value:'a',label:'<part>',commandId:'select:a'},{value:'b',label:'B',commandId:'select:b',disabled:true}]}],pages:[{id:'page.cut',title:'切割刀路',active:true}]});
  assert.match(renderRibbonWorkspace(workspace),/&lt;part&gt;/);
  assert.equal(ribbonWorkspaceCommand(workspace,'object','a'),'select:a');
  assert.equal(ribbonWorkspaceCommand(workspace,'object','b'),'');
  assert.equal(ribbonWorkspaceCommand(workspace,'object','injected'),'');
  workspace.selectors[0].disabled=true;assert.equal(ribbonWorkspaceCommand(workspace,'object','a'),'');
});
test('visual selectors render real preview images and escaped names inside the single toolbar',()=>{
  const workspace=normalizeRibbonWorkspace({selectors:[{id:'part',label:'工件',value:'a',visual:true,options:[{value:'a',label:'<工件甲>',image:'data:image/png;base64,AAA',commandId:'part:a'},{value:'b',label:'乙',imageLabel:'加载预览…',commandId:'part:b'}]}]});
  const html=renderRibbonWorkspace(workspace,()=>'', '<div class="ribbon-commands">TOOLS</div>');
  assert.match(html,/ribbon-workspace-toolbar/);assert.match(html,/ribbon-workspace-thumbnail/);assert.match(html,/src="data:image\/png;base64,AAA"/);assert.match(html,/&lt;工件甲&gt;/);assert.match(html,/TOOLS/);assert.doesNotMatch(html,/ribbon-workspace-navigation/);
});
