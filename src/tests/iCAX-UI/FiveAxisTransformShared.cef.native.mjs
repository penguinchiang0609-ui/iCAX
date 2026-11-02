import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync,readFileSync} from 'node:fs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation';
try{
 const b=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=b.contexts()[0].pages()[0];await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),process.env.ICAX_TRANSFORM_PROJECT||out+'/collider-parity.i5cam');
 await page.waitForFunction(async()=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.model?.isLoaded&&!mv.pending;});
 const command=id=>page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id),idle=()=>page.waitForFunction(()=>!mv.pending&&!mv.fivePlacement?.request),driver=gizmoDriver(page);
 await page.locator('[data-tab-id="workpiece"]').click();await page.waitForFunction(()=>mv.scene?.domain?.placements?.length&&!mv.pending);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.setup.activate:'+mv.scene.domain.placements[0].id));await command('five.tool.translate');await idle();
 await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
 const value=()=>page.locator('[data-five-placement-field="value"]'),read=()=>page.evaluate(()=>({transform:[...mv.scene.domain.placements[0].transform],geometry:structuredClone(mv.scene.domain.workpieces[0].geometryRef)}));
 const original=await read();
 for(const mode of ['translate','rotate','scale']){
  await page.evaluate(mode=>__icaxAppShell.executeRibbonCommand(mode==='scale'?'five.preparation.workpiece:'+mv.scene.domain.workpieces[0].id:'five.setup.activate:'+mv.scene.domain.placements[0].id),mode);
  await command('five.tool.'+mode);await idle();await driver.pick(mode==='scale'?'XYZ':'X');
  assert.equal(await value().evaluate(e=>document.activeElement===e),true);assert.equal(await page.locator('.five-placement-hud input').count(),1);
  await value().fill(mode==='translate'?'5':mode==='rotate'?'10':'1.1');assert.deepEqual(await read(),original,'typing remains a preview');
  await value().press('Escape');assert.deepEqual(await read(),original);assert.equal(await value().count(),0);
  await driver.pick(mode==='scale'?'XYZ':'X');await value().fill(mode==='translate'?'5':mode==='rotate'?'10':'1.1');await value().press('Enter');await idle();const changed=await read();assert.notDeepEqual(changed,original);
  await value().press('Enter');await idle();assert.deepEqual(await read(),changed);
  await page.screenshot({path:out+'/workpiece-shared-'+mode+'.png'});
  await command('five.tool.select');await command('edit.undo');await idle();assert.deepEqual(await read(),original);
 }
 if(process.env.ICAX_WORKPIECE_ONLY){writeFileSync(out+'/workpiece-native-history.json',JSON.stringify({pass:true,modes:['translate','rotate','scale'],nativeUndo:true,previewCancel:true,repeatEnterNoop:true},null,2));console.log('PASS workpiece native history');process.exit(0);}
 // Collider header history uses the same top-level controls as the workpiece.
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 const saved=JSON.parse(readFileSync(out+'/collider-components-acceptance.json','utf8'));await page.locator('[data-five-machine-instance]').selectOption(saved.machineId);await page.waitForFunction(()=>!mv.fiveMachine.busy);
 await command('five.machine.part:'+saved.tip);
 const baseline=await page.evaluate(()=>structuredClone(mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes)),id=baseline[0].id;
 await command('five.machine.box-edit:'+id+':translate');await command('five.machine.box-focus');await driver.pick('X');
 const cv=page.locator('[data-five-collider-value="value"]');await cv.fill('4');await cv.press('Enter');
 const settled=()=>page.waitForFunction(()=>{const s=mv.fiveMachineColliders;return !s.saving&&![...s.drafts.values()].some(d=>d.dirty||d.timer||d.requested);},null,{timeout:60000});await settled();
 const changed=await page.evaluate(()=>structuredClone(mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes));
 await page.locator('[data-action="undo-project"]').click();await settled();assert.deepEqual(await page.evaluate(()=>mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes),baseline);
 await page.locator('[data-action="redo-project"]').click();await settled();assert.deepEqual(await page.evaluate(()=>mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes),changed);
 await page.locator('[data-action="undo-project"]').click();await settled();
 writeFileSync(out+'/transform-shared-cef.json',JSON.stringify({pass:true,workpieceModes:['translate','rotate','scale'],enterCommit:true,escapeCancel:true,repeatEnterNoop:true,colliderTopbarUndoRedo:true},null,2));console.log('PASS shared workpiece/collider transform UI in native CEF');process.exit(0);
}catch(e){console.error(e);process.exit(1);}
