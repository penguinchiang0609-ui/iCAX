import assert from 'node:assert/strict';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
const output=resolve('output/acceptance/selected-collision-20261102/full-head-cef');mkdirSync(output,{recursive:true});
const project=resolve(output,'yoke-contact.i5cam');copyFileSync(resolve('output/acceptance/selected-collision-20261102/full-head/yoke-contact.i5cam'),project);
const browser=await chromium.connectOverCDP('http://127.0.0.1:9688'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
page.setDefaultTimeout(120000);const report={passed:false,standaloneCef:true},errors=[];page.on('pageerror',e=>errors.push(e.message));
try{
 await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready');report.startupPhase='ready';
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 report.opened=await page.evaluate(()=>__icaxAppShell.getState());assert.equal(report.opened.error,'');
 await page.waitForFunction(async()=>{const a=__icaxAppShell.getState();if(a.pendingCount||!a.activeProjectId)return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.headAcceptanceView=getProjectView(a.activeProjectId);return !!headAcceptanceView.scene?.domain;});
 const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!headAcceptanceView.pending&&!headAcceptanceView.fiveToolAxisRequest);
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));await command('five.cam-page.cutting');
 const intent=await page.evaluate(()=>headAcceptanceView.scene.domain.setups[0].generatedGeometry.operations[0].intentId);await page.locator(`[data-five-intent="${intent}"]`).click();await idle();await command('five.cam-inspector.collision');
 await page.waitForFunction(()=>headAcceptanceView.fiveSimulation?.collisionReport?.segments?.length);
 const index=await page.evaluate(()=>{const r=headAcceptanceView.fiveSimulation.collisionReport;return r.segments.find(s=>s.status==='Collision'&&r.headCoverage.boxes[Number(s.witness?.headBoxIndex)]?.partIndex==='0')?.index;});assert(index!==undefined);
 await command('five.simulation.collision-result:'+index);await page.waitForFunction(()=>headAcceptanceView.fiveSimulation.headAssembly?.userData.poseSource==='NativeCollision');
 report.observed=await page.evaluate(()=>{const v=headAcceptanceView,s=v.fiveSimulation,r=s.collisionReport,d=v.scene.domain,setup=d.setups.find(x=>x.id===d.activeSetupId),hit=r.segments.find(x=>Number(x.index)===Math.floor(s.cursor));s.group.updateMatrixWorld(true);return {selected:setup.headModel.instance.id,checked:r.headCoverage.headInstanceId,parts:s.headAssembly.children.map(x=>x.name),boxes:r.headBoxCount,partErrors:s.headAssembly.children.map((m,i)=>Math.max(...m.matrixWorld.clone().transpose().toArray().map((n,k)=>Math.abs(n-hit.headPose.partTransforms[i][k])))),poseSource:s.headAssembly.userData.poseSource,error:s.headPoseError,solves:r.headCoverage.inverseSolveCount,keyposes:r.headCoverage.keyposeCount};});
 const o=report.observed;assert.equal(o.selected,o.checked);assert.equal(o.parts.length,2);assert.equal(o.boxes,'9');assert(o.partErrors.every(e=>e<1e-7));assert.equal(o.error,'');assert.deepEqual(errors,[]);
 await command('five.simulation.collision-focus');await page.screenshot({path:resolve(output,'full-head-desktop.png')});report.passed=true;
}catch(e){report.error=e.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});process.exitCode=1;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
