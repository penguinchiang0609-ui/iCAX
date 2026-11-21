import test from 'node:test';
import assert from 'node:assert/strict';
import {switchCamPage,workspaceState} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {camPageForTool,camRibbonWorkspace,camContextSelectors,camContextCommand,renderCamContext} from '../../apps/five-axis-cam/webpage/camWorkspace.mjs';
import {renderLeft} from '../../apps/five-axis-cam/webpage/workspaceViews.mjs';
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
test('preview opens its CAM section while collision keeps the current section',()=>{
  for(const page of ['cutting','rapid','process','order']){
    assert.equal(camPageForTool({fiveCamPage:page},'simulation'),'simulation');
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
test('the current operation supplies a read-only workpiece context and every independent operation remains selectable',()=>{
  const view={scene:{activePlacementId:'p1',domain:{activeSetupId:'s1',workpieces:[{id:'w1',name:'工件甲'},{id:'w2',name:'工件乙'}],placements:[{id:'p1',workpieceId:'w1',setupId:'s1'},{id:'p2',workpieceId:'w1',setupId:'s2'},{id:'p3',workpieceId:'w2',setupId:'s3'}],setups:[{id:'s1',name:'位姿一'},{id:'s2',name:'位姿二'},{id:'s3',name:'位姿三'}]}}};
  const bar=camRibbonWorkspace(view,'toolpath');
  const selectors=camContextSelectors(view,'toolpath');assert.deepEqual(bar.selectors,[]);
  assert.deepEqual(selectors.map(s=>s.label),['工件','工序']);
  assert.equal(selectors[0].disabled,true);
  assert.deepEqual(selectors[0].options.map(o=>o.value),['w1']);
  assert.deepEqual(selectors[1].options.map(o=>o.value),['p1','p2','p3']);
  assert.ok(selectors[1].options[2].label.includes('工件乙'));
  assert.deepEqual(camRibbonWorkspace(view,'simulation').selectors.map(s=>s.label),['工件','工序']);
  assert.deepEqual(bar.pages.map(p=>p.id),['cutting','rapid','process','order','collision','simulation','head'].map(id=>'five.cam-page.'+id));
  assert.equal(bar.actions.some(a=>a.id==='five.simulation.head-open'),false);
});
test('operation management starts with an enabled workpiece chooser and has no duplicate operation selector',()=>{
 const view={scene:{activePlacementId:'source-b',domain:{workpieces:[{id:'a',name:'甲',enabled:false},{id:'b',name:'乙'}],placements:[{id:'source-a',workpieceId:'a'},{id:'source-b',workpieceId:'b'}],setups:[]}}};
 const workspace=camRibbonWorkspace(view,'operation');assert.equal(workspace.collapsibleSelectors,false);assert.deepEqual(workspace.selectors,[]);
 const selectors=camContextSelectors(view,'operation');assert.deepEqual(selectors.map(s=>s.id),['five-workpiece']);
 const chooser=selectors[0];assert.equal(chooser.disabled,false);assert.equal(chooser.value,'b');assert.deepEqual(chooser.options.map(o=>o.value),['a','b']);assert.equal(chooser.options[0].disabled,false);assert.equal(chooser.options[0].commandId,'five.cam-context.workpiece:source-a');
});
test('sidebar commands keep cross-workpiece operation selection and reject stale, busy or disabled choices',()=>{
 const view={fiveAxisArea:'toolpath',scene:{activePlacementId:'p1',domain:{activeSetupId:'s1',workpieces:[{id:'w1'},{id:'w2'}],placements:[{id:'p1',workpieceId:'w1',setupId:'s1'},{id:'p2',workpieceId:'w2',setupId:'s2'},{id:'p3',workpieceId:'w2',setupId:'s3'}],setups:[{id:'s1'},{id:'s2'},{id:'s3',enabled:false}]}}};
 assert.equal(camContextCommand(view,'toolpath','five-pose','p2'),'five.cam-context.pose:p2');assert.equal(camContextCommand(view,'toolpath','five-workpiece','w2'),'');
 assert.equal(camContextCommand(view,'toolpath','five-pose','p3'),'');assert.equal(camContextCommand(view,'toolpath','five-pose','injected'),'');assert.equal(camContextCommand(view,'operation','five-workpiece','w2'),'');
 view.fiveFixtureDesignRequest={kind:'layout'};assert.equal(camContextCommand(view,'toolpath','five-pose','p2'),'');delete view.fiveFixtureDesignRequest;
 view.scene.domain.workpieces[1].enabled=false;assert.equal(camContextCommand(view,'toolpath','five-pose','p2'),'');
});
test('sidebar context uses product events, stable keys and authoritative operation values with escaped thumbnails',()=>{
 const view={fiveAxisArea:'operation',scene:{activePlacementId:'source',domain:{workpieces:[{id:'w',name:'<工件>',geometryRef:{url:'resource://cad'},previewRef:{url:'resource://preview',version:1}}],placements:[{id:'source',workpieceId:'w'}],setups:[]}},fiveThumbnails:{cache:new Map([[JSON.stringify(['resource://preview','1']),{image:'data:image/png;base64,AAA'}]])}};
 let html=renderCamContext(view,'operation');assert.match(html,/data-five-cam-context-area="operation"/);assert.match(html,/data-pane-interaction-scope="operation:w"/);assert.match(html,/cam-context-choice-five-workpiece-w/);assert.match(html,/&lt;工件&gt;/);assert.match(html,/src="data:image\/png;base64,AAA"/);assert.doesNotMatch(html,/data-action|data-ribbon-workspace/);
 view.fiveAxisArea='toolpath';html=renderCamContext(view,'toolpath');assert.match(html,/data-five-cam-context-field="five-workpiece"[^>]*aria-disabled="true"/);assert.match(html,/data-five-cam-context-field="five-pose"[^>]*data-model-value-authoritative="true"/);assert.match(html,/<option value="" selected disabled>请选择工序<\/option>/);
 assert.equal(renderCamContext(view,'simulation'),'');
});
test('operation and toolpath empty states begin with one sidebar context and no obsolete compact choosers',()=>{
 const view={fiveAxisArea:'operation',scene:{activePlacementId:'source',model:{isLoaded:true},domain:{activeSetupId:'',workpieces:[{id:'w',name:'工件',geometryRef:{url:'resource://cad'}}],placements:[{id:'source',workpieceId:'w'}],setups:[],fixtureUnits:[]}}};
 for(const area of ['operation','toolpath']){view.fiveAxisArea=area;const html=renderLeft({},view);assert.ok(html.startsWith('<section class="five-cam-context"'));assert.equal([...html.matchAll(/data-five-key="cam-context"/g)].length,1);assert.doesNotMatch(html,/data-five-workpiece-chooser|data-five-setup-chooser/);}
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
for(const collapsibleSelectors of [true,false])test(`no-selector ribbon omits empty context and folding markup with collapsibleSelectors=${collapsibleSelectors}`,()=>{
 const workspace=normalizeRibbonWorkspace({label:'工序',selectors:[],collapsibleSelectors,actions:[{id:'five.job.open',title:'工序与输出'}]});
 const html=renderRibbonWorkspace(workspace,()=>'', '<div class="ribbon-commands">TOOLS</div>');
 assert.doesNotMatch(html,/ribbon-workspace-context|data-ribbon-workspace-context|ribbon-workspace-selector-fields|<details|<summary/);assert.match(html,/TOOLS/);assert.match(html,/工序与输出/);
});
