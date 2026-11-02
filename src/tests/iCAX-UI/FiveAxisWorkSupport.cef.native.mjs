import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,existsSync,mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';

const out=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/work-support-20261101/cef');mkdirSync(out,{recursive:true});
const project=resolve(out,`工件随动验收-${Date.now()}.i5cam`),source=process.env.ICAX_WORK_SUPPORT_SOURCE||'C:/icax-copp-motion-20261026/product-fixtures/GA-3015-gantry-head-plan-current.i5cam';
copyFileSync(source,project);if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');
const report={passed:false,project,realNativeSDO:true,standaloneCef:true,cases:[],errors:[]},write=()=>writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));write();
const browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9601');let page;
const check=name=>{report.cases.push(name);write();console.log('PASS '+name);};
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));assert.ok(page);page.setDefaultTimeout(120000);page.on('pageerror',e=>report.errors.push(e.message));await page.bringToFront();console.log('CONNECTED');await restoreNativeCefViewport(page);console.log('VIEWPORT');
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 // This is an isolated acceptance process. Close its former test before opening
 // another copy of the same persistent project UUID.
 const close=async choice=>{const id=await page.evaluate(()=>__icaxAppShell.getState().activeProjectId);if(!id)return;await page.evaluate(id=>{window.supportClose=__icaxAppShell.closeProject(id);},id);await page.locator(`[data-project-close-choice="${choice}"]`).click({force:true});await page.evaluate(()=>window.supportClose);};
 await close('discard');
 await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);
 await page.waitForTimeout(750);
 const bind=async()=>{await page.waitForFunction(async expected=>{const a=__icaxAppShell.getState();if(a.pendingCount||a.activeProjectPath?.replaceAll('\\','/')!==expected.replaceAll('\\','/'))return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const current=getProjectView(a.activeProjectId);return !!current?.scene?.domain&&!current.pending&&!current.disposed&&!!current.viewportSceneProxy&&!!current.viewport?.sceneObjects?.size;},project);await page.evaluate(async()=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.wv=getProjectView(__icaxAppShell.getState().activeProjectId);});};
 await bind();
 console.log('READY '+project);
 const idle=()=>page.waitForFunction(()=>!!wv.scene?.domain&&!wv.pending&&!wv.fiveMachine?.busy&&!wv.fiveMachine?.loading&&!wv.fiveMachine?.planLoading&&!wv.fiveMachineParameterEdits?.saving&&!__icaxAppShell.getState().pendingCount);
 const command=async id=>{await page.locator(`[data-five-command="${id}"],[data-command-id="${id}"]`).filter({visible:true}).first().click();await idle();};
 const read=()=>page.evaluate(()=>wv.viewportSceneProxy.invoke('CamScene.ReadState',{}));
 const before=await read();
 if(process.env.ICAX_WORK_SUPPORT_FORCE_REPLAN){
  await page.locator('[data-tab-id="simulation"]').click();await command('five.tool.machine-instances');
  await command('five.machine.part:');const feed=page.locator('[data-five-machine-field="post:cutFeedMmMin"]');await feed.fill(String(Number(await feed.inputValue())+1));await feed.press('Tab');await idle();
  report.autoPrepareRequested=true;
 }
 // Give a closed contour one locally tilted tool-axis station. Native IK must
 // solve a changing support rotation, not just replay an initially tilted table.
 if(!process.env.ICAX_WORK_SUPPORT_REUSE_PLAN)report.testPose=await page.evaluate(async()=>{
  const proxy=wv.viewportSceneProxy,d=await proxy.invoke('CamScene.ReadState',{}),cut=wv.scene.intentToolpaths.find(c=>c.enabled!==false),at=cut.previewPoints[Math.floor(cut.previewPoints.length/2)];
  const resolved=await proxy.invoke('CamCut.ResolveToolAxisPoint',{expected:d.expected,intentId:cut.id,nearPoint:at,maximumDistanceMm:.2,frame:'Workpiece'});
  const direction=[...resolved.axis.direction];direction[0]+=.025;
  await proxy.invoke('CamCut.EditToolAxisPoint',{commandId:crypto.randomUUID(),expected:d.expected,intentId:cut.id,pointId:crypto.randomUUID(),action:'Upsert',anchor:resolved.anchor,direction,frame:'Workpiece',transition:'ArcLength'});
  return {point:at,direction};
 });
 await page.locator('[data-tab-id="toolpath"]').click();await command('five.tool.simulation');
 if(process.env.ICAX_WORK_SUPPORT_REUSE_PLAN)await page.waitForFunction(()=>!!wv.fiveMachine?.report||['Stale','Missing'].includes(wv.fiveMachine?.planValidity));
 if(!process.env.ICAX_WORK_SUPPORT_REUSE_PLAN||!await page.evaluate(()=>!!wv.fiveMachine?.report?.playbackAllowed)){await command('five.simulation.play');await command('five.simulation.pause');}
 await page.waitForFunction(()=>wv.fiveMachine?.report?.validity==='Current'&&wv.fiveMachine.report.playbackAllowed&&!wv.fiveMachine.busy,null,{timeout:240000});
 const plan=await page.evaluate(()=>{const r=wv.fiveMachine.report,indices=[];for(let i=r.support;i>=0;i=r.nodes[i].parent)indices.push(i);const chain=indices.map(i=>({i,id:r.nodes[i].id,type:r.nodes[i].type,min:Math.min(...r.frames.map(f=>f.joints[i])),max:Math.max(...r.frames.map(f=>f.joints[i]))})),moving=chain.find(n=>['revolute','continuous'].includes(n.type)&&n.max-n.min>.001),motionIndex=moving?r.frames.findIndex(f=>f.joints[moving.i]===moving.min):0;return {frames:r.frames.length,collisionStatus:r.collisionStatus,scope:r.collisionScope,support:r.nodes[r.support].id,chain,motionIndex};});
 assert.equal(plan.collisionStatus,'Clear');assert.ok(plan.chain.some(n=>['revolute','continuous'].includes(n.type)&&n.max-n.min>.001),JSON.stringify(plan));report.nativePlan=plan;check('Native IK plans changing work-support rotation with full-machine continuous collision checks');
 await page.waitForFunction(()=>wv.fiveViewportFrame==='cam-head'&&wv.viewport.sceneObjects.size>2);
 const display=await page.evaluate(async()=>{const{camHeadElements}=await import('../../../apps/five-axis-cam/webpage/machinePlaybackContext.mjs'),head=camHeadElements(wv),d=wv.scene.domain,expected=[...head.map(e=>e.entityId),...d.placements.filter(p=>p.setupId===d.activeSetupId).map(p=>p.id),...d.fixtures.filter(p=>p.setupId===d.activeSetupId&&p.enabled!==false).map(p=>p.id)];return {head:head.map(e=>({id:e.entityId,name:e.name})),expected:expected.sort(),visible:[...wv.viewport.sceneObjects].filter(([id,o])=>o.visible).map(([id])=>id).sort()};});
 assert.deepEqual(display.visible,display.expected);assert.ok(display.head.length>=2,'The fork and rotary head must both render');report.camDisplay=display;
 assert.equal(await page.locator('[data-five-command="five.machine.build"],[data-five-command="five.machine.program"],[data-five-command="five.machine.export"],[data-five-machine-field="observeAutomatic"]').count(),0);check('CAM renders only mounted workpiece, fixtures and the complete terminal cutting-head mechanism');
 assert.equal(await page.evaluate(()=>!!wv.fiveSimulation?.group),false);
 const seek=async value=>{await page.locator('[data-five-machine-field="seek"]').fill(String(value));};
 const capture=()=>page.evaluate(()=>{wv.viewport.scene.updateMatrixWorld(true);return Object.fromEntries([...wv.viewport.sceneObjects].map(([id,o])=>[id,o.matrixWorld.toArray()]));});
 await command('five.simulation.head-toggle');assert.equal(await page.evaluate(ids=>ids.every(id=>!wv.viewport.sceneObjects.get(id).visible),display.head.map(e=>e.id)),true);await command('five.simulation.head-toggle');
 await command('five.simulation.play');await page.waitForFunction(()=>wv.fiveMachine.playing&&wv.fiveMachine.cursor>.1);await command('five.simulation.pause');
 await seek(0);const zero=await capture();const cursor=Math.min(plan.frames-1,plan.motionIndex+.37);await seek(cursor);
 const cam=await capture();
 const oracle=await page.evaluate(async()=>{
  const v=wv,r=v.fiveMachine.report,c=v.fiveMachine.cursor,{machineForward}=await import('../../../apps/five-axis-cam/webpage/machineKinematics.mjs'),{machinePlaybackJoints}=await import('../../../apps/five-axis-cam/webpage/machineTrajectory.mjs'),THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js');
  const world=machineForward(r,machinePlaybackJoints(r.frames,c)),errors=[],setup=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId),base=world[r.support].clone().multiply(new THREE.Matrix4().set(...r.configuration.placement));
  v.viewport.scene.updateMatrixWorld(true);
  const compare=(id,matrix)=>{const o=v.viewport.sceneObjects.get(id);if(o&&o.matrixWorld.elements.some((x,i)=>Math.abs(x-matrix.elements[i])>1e-6))errors.push(id);};
  r.nodes.forEach((n,i)=>compare(n.id,world[i]));for(const p of [...v.scene.domain.placements,...v.scene.domain.fixtures])if(p.setupId===setup.id)compare(p.id,base.clone().multiply(new THREE.Matrix4().set(...p.transform)));
  return {errors,cursor:c,placements:v.scene.domain.placements.filter(p=>p.setupId===setup.id).map(p=>p.id),trail:v.fiveMachineObservation?.trail?.root.matrix.toArray(),base:base.toArray(),canvas:v.viewport.renderer.domElement===window.savedCanvas};
 });
 assert.deepEqual(oracle.errors,[]);assert.ok(oracle.placements.some(id=>cam[id]?.some((x,i)=>Math.abs(x-zero[id][i])>1e-5)));assert.deepEqual(oracle.trail,oracle.base);report.camOracle=oracle;check('CAM moves visible workpiece and executed trail with its support; no separate head player');
 await page.screenshot({path:resolve(out,'CAM随装夹运动.png')});
 await page.evaluate(()=>{window.savedCanvas=wv.viewport.renderer.domElement;window.savedCamera=wv.viewport.getCameraState();});
 await page.locator('[data-tab-id="simulation"]').click();await command('five.tool.machine-simulation');await idle();await seek(cursor);const machine=await capture();
 for(const [id,m] of Object.entries(cam))if(machine[id])m.forEach((x,i)=>assert.ok(Math.abs(x-machine[id][i])<1e-6,`${id} matrix ${i}`));check('CAM and whole-machine simulation produce identical visible world transforms at the same progress');
 await command('five.tool.machine-instances');await page.locator('[data-machine-mount]').first().waitFor();
 const relation=await page.locator('[data-machine-mount]').first().evaluate(n=>({id:n.dataset.machineMount,parent:n.parentElement.parentElement.dataset.machineNode,text:n.innerText}));assert.equal(relation.parent,plan.support);report.relation=relation;await page.screenshot({path:resolve(out,'装夹随动关系.png')});check('Mechanical tree nests the document workpiece beneath its persisted work-support endpoint');
 await page.locator('[data-tab-id="toolpath"]').click();await command('five.cam-page.rapid');await page.waitForFunction(()=>wv.fiveViewportFrame==='setup'&&!wv.fiveMachine.playing);
 assert.equal(await page.evaluate(()=>!!wv.fiveMachineObservation?.trail),false);
 const after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits'])assert.deepEqual(after[key],before[key]);check('Leaving simulation restores editing coordinates without modifying source CAD or local mounting transforms');
 // Reopen the exact saved document and its journal, then load the certified plan.
 await close('save');
 await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);await page.waitForTimeout(750);await bind();await page.locator('[data-tab-id="toolpath"]').click();await command('five.tool.simulation');await page.waitForFunction(()=>wv.fiveMachine?.report?.validity==='Current'&&wv.fiveViewportFrame==='cam-head');
 const reopened=await read();assert.deepEqual(reopened.setups[0].machineSetup,after.setups[0].machineSetup);assert.equal(await page.evaluate(()=>wv.fiveMachine.report.frames.length),plan.frames);check('Save and reopen retain the support binding and complete accepted axis-motion plan');
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(out,'failure.png')}).catch(()=>{});throw error;}
finally{write();await browser.close();}
