import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0],out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',path=out+'/工业机床碰撞验证.i5cam';
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await page.mouse.up();
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machines?.length&&!mv.pending;},path);
 await page.locator('[data-tab-id="simulation"]').click();
 const groups=await page.locator('.ribbon-group').evaluateAll(groups=>groups.map(g=>({title:g.querySelector('.ribbon-group-title').textContent,commands:[...g.querySelectorAll('[data-command-id]')].map(b=>b.dataset.commandId)})));
 assert.deepEqual(groups.map(g=>g.title),['机床定义','机床实例','作业']);assert.deepEqual(groups[0].commands,['five.tool.machine-definitions']);assert.ok(groups.slice(1).every(g=>g.commands.length>=2));
 const ribbon=id=>page.locator(`[data-command-id="five.machine-panel.${id}"]`);
 assert.equal(await ribbon('export').isDisabled(),true);assert.equal(await ribbon('play').isDisabled(),true);
 const panels=[];
 for(const action of ['post','joints','colliders']){
  const toggle=page.locator('.ribbon-command-menu-toggle[data-command-id="five.tool.machine-instances"]');
  if(await toggle.getAttribute('aria-expanded')!=='true')await toggle.click();
  await page.locator(`.ribbon-command-menu [data-command-id="five.machine-panel.${action}"]`).click();const r=await page.locator(`[data-five-key="machine-${action}"]`).evaluate(e=>({offset:e.getBoundingClientRect().top-e.closest('.cam-info-pane').getBoundingClientRect().top}));
  assert.ok(Math.abs(r.offset-12)<3,action+' opens the requested editor');panels.push({action,...r});
  assert.equal(await page.locator('[data-five-machine-instance]').count(),1);
 }
 await page.locator('[data-command-id="five.tool.machine-definitions"]').click();await page.locator('[data-five-key="machine-definition-manager"]').waitFor();await page.locator('[data-five-key="machine-definition-manager"] [data-five-command="five.machine.definitions-close"]').last().click();
 await ribbon('build').click();await page.waitForFunction(()=>!mv.fiveMachine.busy&&mv.fiveMachine.report,{},{timeout:120000});
 const report=await page.evaluate(()=>({status:mv.fiveMachine.report.status,collision:mv.fiveMachine.report.collisionStatus,frames:mv.fiveMachine.report.frames.length}));assert.equal(report.status,'Ready');assert.equal(report.collision,'Clear');
 assert.equal(await ribbon('export').isEnabled(),true);assert.equal(await ribbon('play').isEnabled(),true);
 await ribbon('play').click();await page.waitForFunction(()=>mv.fiveMachine.cursor>0);await ribbon('pause').click();assert.equal(await page.evaluate(()=>mv.fiveMachine.playing),false);
 await ribbon('reset').click();assert.equal(await page.evaluate(()=>mv.fiveMachine.cursor),0);
 await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.machine.fit'));await page.waitForTimeout(150);
 await page.screenshot({path:out+'/three-ribbon-groups.png'});
 writeFileSync(out+'/ribbon-acceptance.json',JSON.stringify({pass:true,groups,panels,report,ribbonPlayPauseReset:true,exportRequiresPassingReport:true},null,2));console.log(JSON.stringify({pass:true,groups:groups.map(g=>({title:g.title,commands:g.commands.length})),report,panels:true,playPauseReset:true,exportGate:true}));process.exit(0);
}catch(e){console.error(e);process.exit(1);}
