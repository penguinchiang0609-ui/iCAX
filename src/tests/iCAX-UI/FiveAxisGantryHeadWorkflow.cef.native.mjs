import assert from 'node:assert/strict';
import {chromium} from './node_modules/playwright/index.mjs';
import {copyFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';

const output=resolve('output/tests/gantry-heads-20261101/cef');mkdirSync(output,{recursive:true});
const project=resolve(output,'head-to-machine-'+Date.now()+'.i5cam');copyFileSync(resolve('output/tests/gantry-heads-20261101/AC-head.i5cam'),project);
const report={passed:false,standaloneCef:true,packagedRuntime:true,project,errors:[]},browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9637');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));page.setDefaultTimeout(45000);page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!hw.pending&&!hw.fiveMachine?.busy&&!hw.fiveSimulation?.busy&&!__icaxAppShell.getState().pendingCount,null,{timeout:60000});
try{
 const reuse=await page.evaluate(async()=>{const state=__icaxAppShell.getState(),{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.hw=getProjectView(state.activeProjectId);return hw?.scene?.machines?.length===0&&hw.scene.domain.setups.some(s=>s.headModel?.assembly?.kind==='AC')?state.activeProjectPath:null;});
 if(reuse)report.project=reuse;else {await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);await page.waitForFunction(async path=>{if(__icaxAppShell.getState().activeProjectPath!==path)return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.hw=getProjectView(__icaxAppShell.getState().activeProjectId);return hw?.scene?.domain&&!hw.pending;},project);}await idle();
 assert.equal(await page.evaluate(()=>hw.scene.machines.length),0);
 await page.locator('[data-tab-id="toolpath"]').click();await idle();if(!await page.evaluate(()=>hw.fiveSimulation?.headOpen))await page.locator('[data-command-id="five.simulation.head-open"]').click();await idle();
 const dialog=page.getByRole('dialog',{name:'切割头',exact:true});await dialog.waitFor();
 await page.waitForFunction(()=>document.querySelectorAll('optgroup[label="龙门刀头"] option').length===5);assert.equal(await dialog.locator('optgroup[label="龙门刀头"] option').count(),5);
 assert.equal(await dialog.locator('[data-five-command^="five.machine.instantiate:"]').count(),0);
 const definitionId=await page.evaluate(()=>hw.scene.domain.setups.find(s=>s.id===hw.scene.domain.activeSetupId).headModel.builtinDefinitionId);
 await dialog.getByRole('button',{name:'关闭切割头',exact:true}).click();await idle();
 await page.locator('[data-tab-id="simulation"]').click();await idle();
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.machine.definitions-open'));await idle();
 const definitions=page.getByRole('dialog',{name:'机床定义管理',exact:true});
 await definitions.locator(`[data-five-command="five.machine.definition-select:${definitionId}"]`).click();await idle();
 await definitions.getByRole('button',{name:'创建实例',exact:true}).click();await idle();
 await page.waitForFunction(()=>hw.scene.machines.length===1&&hw.fiveAxisArea==='simulation'&&hw.viewport.sceneObjects.size>=10);
 report.created=await page.evaluate(()=>({model:hw.scene.machines[0].name,area:hw.fiveAxisArea,ribbon:__icaxAppShell.getState().activeRibbonTabId,definitionId:hw.scene.machines[0].machineDefinitionId}));assert.equal(report.created.ribbon,'simulation');
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.machine-simulation'));await idle();
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.machine.mount'));await idle();
 const bound=await page.evaluate(()=>{const setup=hw.scene.domain.setups.find(s=>s.id===hw.scene.domain.activeSetupId);return {error:hw.fiveMachine.error,machineId:setup.machineSetup?.machineId,kind:setup.headModel.assembly?.kind,source:setup.headModel.assembly?.source,mountStatus:hw.fiveMachine.mounting?.collisionStatus};});
 assert.equal(bound.error,'');assert.ok(bound.machineId);assert.equal(bound.kind,'AC');assert.equal(bound.source,'MachineInstance');assert.equal(bound.mountStatus,'Clear');report.bound=bound;
 await page.evaluate(()=>{hw.viewport.setViewDirection([1,-1.5,.8]);hw.viewport.fitViewToViewport(1.05);});await page.screenshot({path:resolve(output,'head-to-machine-workflow.png')});assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;await page.screenshot({path:resolve(output,'head-to-machine-failure.png')}).catch(()=>{});throw error;}finally{writeFileSync(resolve(output,'head-workflow-report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
