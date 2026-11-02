import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
import {workspaceAlertItems} from '../../apps/five-axis-cam/webpage/alerts.mjs';

const source=readFileSync(new URL('../../apps/five-axis-cam/webpage/entry.mjs',import.meta.url),'utf8');
const first=source.indexOf("  if(id.startsWith('five.alerts.open:')){"),last=source.indexOf("  if(id==='five.simulation.collision-close')",first);
assert.ok(first>=0&&last>first);
function routing(pending=false){
  const calls=[],view={pending,error:'自动保存失败',scene:{domain:{activeSetupId:'s',setups:[{id:'s',simulationCheck:{validity:'Current',collisionCount:'1'}}]}}};
  const sandbox={workspaceAlertItems,command:async(_context,current,id)=>{calls.push(id);current.error='';},ops:{renderProject(){}},context:{isCurrentProject:()=>true},view};
  runInNewContext(`globalThis.open=async id=>{const {view,context,ops}=globalThis;${source.slice(first,last)}};`,sandbox);
  return {view,calls,open:sandbox.open};
}
test('opening collision does not dismiss an unrelated error or execute business edits',async()=>{
  const f=routing();await f.open('five.alerts.open:collision');assert.deepEqual(f.calls,['five.tool.collision']);assert.equal(f.view.fiveAlertsOpen,false);assert.equal(f.view.error,'自动保存失败');
});
test('busy collision alerts do not restart detection and a resolved link has no destination',async()=>{
  const f=routing(true);await f.open('five.alerts.open:collision');assert.deepEqual(f.calls,[]);assert.equal(f.view.fiveAlertsOpen,false);
  f.view.scene.domain.setups[0].simulationCheck.collisionCount='0';await f.open('five.alerts.open:collision');assert.deepEqual(f.calls,[]);
});
test('a removed optical alert cannot navigate or change unrelated save errors',async()=>{
  const f=routing(true),domain=f.view.scene.domain;
  domain.setups[0].fixtureBeamCheck={validity:'Stale',status:'Unchecked'};
  domain.fixtureUnits=[{id:'u',setupId:'s'}];
  domain.intentToolpaths=[{id:'cut',setupId:'s'}];
  domain.setups[0].generatedGeometry={status:'Current',operations:[{intentId:'cut',parts:[{role:'Cut',points:[[0,0,0],[10,0,0]],directions:[[0,0,1],[0,0,1]]}]}]};
  await f.open('five.alerts.open:fixture-light%3Au');
  assert.deepEqual(f.calls,[]);assert.equal(f.view.error,'自动保存失败');
  domain.setups[0].fixtureBeamCheck={validity:'Current',status:'Clear'};
  await f.open('five.alerts.open:fixture-light%3Au');
  assert.deepEqual(f.calls,[],'Legacy optical results cannot restore removed routing');
});
