import test from 'node:test';
import assert from 'node:assert/strict';
import {workspaceAlerts,workspaceAlertTarget,renderWorkspaceAlerts,registerWorkspaceAlertProvider,syncAlertPanel,disposeAlertPanel} from '../../apps/five-axis-cam/webpage/alerts.mjs';

function generatedCut(view,setupId){
  view.scene.domain.intentToolpaths=[{id:'cut',setupId}];
  view.scene.domain.setups.find(s=>s.id===setupId).generatedGeometry={status:'Current',operations:[{intentId:'cut',parts:[{role:'Cut',points:[[0,0,0],[10,0,0]],directions:[[0,0,1],[0,0,1]]}]}]};
}

test('global alerts use the active setup report and never display a previous setup message',()=>{
  const view={fiveSimulation:{message:'旧装夹碰撞',sourceKey:'old'},scene:{domain:{activeSetupId:'new',setups:[
    {id:'old',simulationCheck:{validity:'Current',collisionCount:'7'}},{id:'new',simulationCheck:{validity:'Current',collisionCount:'0',uncheckedCount:'0'}},
  ]}}};
  assert.deepEqual(workspaceAlerts(view).items,[]);
  view.scene.domain.setups[1].simulationCheck={validity:'Current',collisionCount:'2',uncheckedCount:'3'};
  view.error='保存失败';
  assert.deepEqual(workspaceAlerts(view).items.map(i=>i.id),['operation','collision','unchecked']);
  view.scene.domain.setups[1].simulationCheck.validity='Stale';
  assert.deepEqual(workspaceAlerts(view).items.map(i=>i.id),['operation','collision-stale']);
});

test('repeated synchronous panel updates retain one animation callback and release every listener',()=>{
  let next=0,observed=0,disconnected=0;
  const callbacks=new Map(),listeners=new Map(),properties=new Map();
  const window={innerWidth:1600,innerHeight:1000,requestAnimationFrame(fn){callbacks.set(++next,fn);return next;},
    cancelAnimationFrame(id){callbacks.delete(id);},addEventListener(name,fn){listeners.set(name,fn);},removeEventListener(name,fn){if(listeners.get(name)===fn)listeners.delete(name);},
    ResizeObserver:class{observe(){observed++;}disconnect(){disconnected++;}}};
  const anchor={getBoundingClientRect:()=>({right:1580}),closest:()=>({getBoundingClientRect:()=>({bottom:148})})};
  const document={defaultView:window,querySelector:()=>anchor},panel={style:{setProperty:(name,value)=>properties.set(name,value)}};
  const context={mount:{ownerDocument:document,querySelector:()=>panel},isCurrentProject:()=>true},view={activeAreaId:'cam'},binding={context,view,area:'cam'};
  syncAlertPanel(context,view,binding);syncAlertPanel(context,view,binding);
  assert.equal(callbacks.size,1,'updates must not lose the pending callback ID');
  assert.equal(observed,1);assert.equal(properties.get('--five-alert-top'),'154px');
  disposeAlertPanel(binding);
  assert.equal(callbacks.size,0);assert.equal(listeners.size,0);assert.ok(disconnected>=1);assert.equal(binding.alertPanel,null);
});

test('independent alert facts open their own handlers and resolved facts disappear',()=>{
  const assembly={id:'assembly',cutClearance:{validity:'Stale',status:'Unchecked'}};
  const view={fiveAlertsOpen:true,scene:{domain:{activeSetupId:'setup',setups:[{id:'setup',simulationCheck:{validity:'Current',collisionCount:'3',uncheckedCount:'0'},fixtureDesign:{assembly}}],fixtureUnits:[{id:'fixture',name:'支撑架',setupId:'setup',design:{assembly}},{id:'other',setupId:'other',design:{assembly:{id:'other',cutClearance:{validity:'Stale'}}}}]}}};
  generatedCut(view,'setup');let items=workspaceAlerts(view).items;assert.equal(items.length,1);
  assert.equal(workspaceAlertTarget(view,'collision').commandId,'five.tool.collision');
  assert.equal(workspaceAlertTarget(view,'fixture-light:fixture'),undefined);
  const html=renderWorkspaceAlerts(view);assert.match(html,/告警概要/);assert.match(html,/切割头检测：3 段间隙告警/);assert.doesNotMatch(html,/光路.*验证/);
  assert.doesNotMatch(html,/碰撞阈值|开始检测|data-five-collision-dialog|repair-deviation/);
  view.scene.domain.setups[0].simulationCheck.scope='BuiltinSchematicWorkpieceAndFixtures';
  assert.match(renderWorkspaceAlerts(view),/示意切割头检测：3 段间隙告警/);
  view.scene.domain.setups[0].simulationCheck.collisionCount='0';
  assert.equal(workspaceAlertTarget(view,'collision'),undefined,'stale links must not open a different feature');
  assembly.cutClearance={validity:'Current',status:'Clear'};
  assert.deepEqual(workspaceAlerts(view).items,[]);assert.match(renderWorkspaceAlerts(view),/暂无告警/);
});

test('new alert providers extend overview and routing without becoming collision UI',()=>{
  const dispose=registerWorkspaceAlertProvider('motion',(_view,setup)=>setup?.id==='active'?[{id:'limit',title:'行程越界',detail:'X 轴超出行程',severity:'error',target:{commandId:'five.tool.machine-simulation'}}]:[]);
  try{
    const view={fiveAlertsOpen:true,scene:{domain:{activeSetupId:'active',setups:[{id:'active'}]}}};
    const item=workspaceAlerts(view).items[0];assert.equal(item.id,'motion:limit');assert.equal(item.commandId,'five.alerts.open:motion%3Alimit');
    assert.equal(workspaceAlertTarget(view,item.id).commandId,'five.tool.machine-simulation');assert.match(renderWorkspaceAlerts(view),/行程越界/);
    view.scene.domain.activeSetupId='other';assert.deepEqual(workspaceAlerts(view).items,[]);
  }finally{dispose();}
});

test('legacy optical certificates do not create alerts or change head collision facts',()=>{
  const setup={id:'s',simulationCheck:{validity:'Current',collisionCount:'2'},fixtureBeamCheck:{validity:'Current',status:'Clear'}};
  const view={pending:true,scene:{domain:{activeSetupId:'s',setups:[setup],fixtureUnits:[{id:'u',setupId:'s',design:{assembly:{id:'a',cutClearance:{validity:'Stale',status:'Unchecked'}}}}]}}};
  assert.deepEqual(workspaceAlerts(view).items.map(item=>item.id),['collision']);
  generatedCut(view,'s');
  setup.fixtureBeamCheck.validity='Stale';
  assert.equal(workspaceAlerts(view).items.find(item=>item.id==='fixture-light:u'),undefined);
  setup.fixtureBeamCheck={validity:'Current',status:'Blocked'};
  assert.equal(workspaceAlerts(view).items.find(item=>item.id==='fixture-light:u'),undefined);
  setup.fixtureBeamCheck={validity:'Current',status:'Unchecked'};
  assert.equal(workspaceAlerts(view).items.find(item=>item.id==='fixture-light:u'),undefined);
});

test('clear optical results retain independent stale fixture geometry warnings and disabled fixtures need no optical check',()=>{
  const assembly={id:'a',geometryValidity:'Stale',parts:[{partId:'p'}]},setup={id:'s',fixtureBeamCheck:{validity:'Current',status:'Clear'}};
  const view={scene:{domain:{activeSetupId:'s',setups:[setup],fixtureUnits:[{id:'u',setupId:'s',name:'支撑架',design:{assembly}}],fixtures:[{id:'p',setupId:'s',unitId:'u'}]}}};
  let items=workspaceAlerts(view).items;
  assert.deepEqual(items.map(item=>item.id),['fixture-geometry:u']);assert.equal(items[0].target.commandId,'five.fixture.select:u');
  setup.fixtureBeamCheck.validity='Stale';view.scene.domain.fixtures[0].enabled=false;
  assert.deepEqual(workspaceAlerts(view).items,[],'Disabled plate meshes have no outstanding optical scope');
});

test('busy overview permits opening collision results without navigating away from another task',()=>{
  const view={pending:true,error:'保存失败',scene:{domain:{activeSetupId:'s',setups:[{id:'s',simulationCheck:{validity:'Current',collisionCount:'1'}}]}}};
  const items=workspaceAlerts(view).items;assert.equal(items.find(i=>i.id==='collision').disabled,false);assert.equal(items.find(i=>i.id==='operation').disabled,true);
});
