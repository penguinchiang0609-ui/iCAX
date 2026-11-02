import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';

const out=resolve('output/tests/ab-head-axis-20261101/cef');mkdirSync(out,{recursive:true});
const project=resolve(out,`AB-axes-preview-${Date.now()}.i5cam`);copyFileSync(resolve('output/projects/CAM-工作副本-20261101.i5cam'),project);
const report={passed:false,standaloneCef:true,packagedRuntime:true,project,errors:[]};
let browser,page;
for(let attempt=0;attempt<30;attempt++){
 try{browser=await chromium.connectOverCDP('http://127.0.0.1:9641');break;}
 catch(error){if(attempt===29)throw error;await new Promise(resolve=>setTimeout(resolve,500));}
}
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));page.on('pageerror',e=>report.errors.push(e.message));page.setDefaultTimeout(45000);
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async path=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.abv=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&abv?.scene?.domain&&!abv.pending;},project);
 await page.locator('[data-tab-id="toolpath"]').click();await page.locator('[data-command-id="five.simulation.head-open"]').click();
 await page.waitForFunction(()=>abv.fiveSimulation?.builtinHeads?.length===9);
 const heads=await page.evaluate(()=>abv.fiveSimulation.builtinHeads),select=page.locator('[data-five-simulation-field="builtin-head"]');
 for(const kind of ['AC','AB']){
  const id=heads.find(h=>h.kind===kind).id;await select.selectOption(id);
  await page.waitForFunction(id=>!abv.pending&&!abv.fiveSimulation?.busy&&abv.fiveHeadPreview?.phase==='ready'&&abv.scene.domain.setups.find(s=>s.id===abv.scene.domain.activeSetupId).headModel.builtinDefinitionId===id,id);
 }
 const snapshot=()=>page.evaluate(()=>{const v=abv,s=v.fiveHeadPreview,h=v.scene.domain.setups.find(x=>x.id===v.scene.domain.activeSetupId).headModel;return {names:s.model.children.map(m=>m.name),matrices:s.model.children.map(m=>m.matrix.toArray()),positions:s.previewPositions,axes:h.assembly.nodes.map(n=>({name:n.joint,type:n.type,axis:n.axis})),saved:h.assembly.nodes.map(n=>n.position),markers:s.markers.children.filter(o=>o.userData.previewJointAxis).map(o=>o.userData.previewJointAxis)};});
 const read=()=>page.evaluate(()=>abv.viewportSceneProxy.invoke('CamScene.ReadState',{})),native=await read(),before=await snapshot();
 assert.equal(before.names.length,3);assert.equal(before.axes[0].type,'fixed');assert.deepEqual(before.axes[1].axis,[0,1,0]);assert.deepEqual(before.axes[2].axis,[1,0,0]);assert.deepEqual(before.markers,['B','A']);
 const set=async(index,angle)=>page.locator(`[data-five-head-joint="${index}"]`).evaluate((input,angle)=>{input.value=String(angle);input.dispatchEvent(new Event('input',{bubbles:true}));},angle);
 await set(2,30);const a=await snapshot();assert.deepEqual(a.matrices.slice(0,2),before.matrices.slice(0,2));assert.notDeepEqual(a.matrices[2],before.matrices[2]);assert.deepEqual(a.saved,before.saved);
 await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(out,'AB-A-about-X.png')});
 await page.locator('[data-five-head-joints-reset]').click();await set(1,30);const b=await snapshot();assert.deepEqual(b.matrices[0],before.matrices[0]);assert.notDeepEqual(b.matrices[1],before.matrices[1]);assert.notDeepEqual(b.matrices[2],before.matrices[2]);assert.deepEqual(b.saved,before.saved);
 await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(out,'AB-B-about-Y.png')});
 await page.locator('[data-five-head-joints-reset]').click();assert.deepEqual((await snapshot()).matrices,before.matrices);assert.deepEqual(await read(),native);
 await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(out,'AB-fixed-stator-two-axes.png')});
 report.current={parts:before.names,axes:before.axes,A:'X; only tool moves',B:'Y; carrier and tool move',fixedMountUnchanged:true,savedSceneUnchanged:true};
 if(process.env.ICAX_TEST_LEGACY_AB==='1'){
 const old=JSON.parse(readFileSync(resolve('output/tests/gantry-heads-20261101/cef/report.json'),'utf8')).cases.find(c=>c.kind==='AB').project;
 const legacy=resolve(out,'legacy-AB-instance.i5cam');copyFileSync(old,legacy);await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),legacy);
 await page.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.legacyAB=getProjectView(__icaxAppShell.getState().activeProjectId);const d=legacyAB?.scene?.domain,h=d?.setups.find(s=>s.id===d.activeSetupId)?.headModel;if(__icaxAppShell.getState().activeProjectPath!==path||__icaxAppShell.getState().pendingCount||legacyAB?.pending||h?.collisionSource!=='MachineInstance')return false;window.legacyRead=structuredClone(h);return true;},legacy);
 const legacyHead=await page.evaluate(()=>legacyRead);
 assert.equal(legacyHead.collisionSource,'MachineInstance');assert.equal(legacyHead.assembly.nodes.length,2);report.legacy={retained:true,parts:legacyHead.assembly.nodes.length,boundary:legacyHead.assembly.nodes[0].name};
 }
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.failure=error.stack;await page?.screenshot({path:resolve(out,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
