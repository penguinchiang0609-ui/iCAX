import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync,existsSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/head-colliders-20261102/cef');mkdirSync(output,{recursive:true});
// Each run owns a fresh archive path, including its autosave journal sidecar.
const project=resolve(output,'切割头碰撞盒桌面验证-'+Date.now()+'.i5cam');copyFileSync(resolve(process.env.ICAX_HEAD_TEST_PROJECT||'output/acceptance/head-joint-rows-20261102/browser/刀头实例验证.i5cam'),project);
const report={passed:false,standaloneCef:true,project,errors:[]},browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9662');let page;
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));assert.ok(page);page.setDefaultTimeout(45000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');assert.equal(await page.evaluate(()=>!!__icaxAppShell.getState().activeProjectPath),false,'Use an isolated clean runtime');
 const open=async()=>{
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.hc=getProjectView(__icaxAppShell.getState().activeProjectId);return !!hc?.scene?.domain&&!hc.pending;},null,{timeout:120000});
  await page.locator('[data-tab-id="toolpath"]').click();await page.waitForFunction(()=>hc.fiveAxisArea==='toolpath'&&!hc.pending);if(!await page.locator('[data-five-key="head-popover"]').count())await page.locator('[data-command-id="five.simulation.head-open"]').click();
  await page.waitForFunction(()=>hc.fiveHeadPreview?.phase==='ready');
 };
 const settle=()=>page.waitForFunction(()=>{const s=hc.fiveHeadColliders;return !hc.pending&&!hc.fiveSimulation?.headChoiceBusy&&!s?.saving&&!s?.timer&&!s?.request&&!s?.parts.some(p=>p.dirty);},null,{timeout:90000});
 const read=()=>page.evaluate(()=>hc.viewportSceneProxy.invoke('CamScene.ReadState',{}));
 const cmd=action=>page.locator(`[data-five-command="five.head-collider.${action}"]`);
 const editable=state=>{const h=state.setups.find(s=>s.id===state.activeSetupId).headModel;return {parameters:h.instance.parameters,boxes:h.collisionBoxes,parts:h.assembly?.nodes.map(n=>n.collisionBoxes),count:h.collisionModel.boxCount};};
 const history=async direction=>{await page.evaluate(direction=>__icaxAppShell.executeRibbonCommand('edit.'+direction),direction);await settle();await page.waitForFunction(()=>hc.fiveHeadPreview?.phase==='ready');assert.equal(await page.locator('[data-five-key="head-popover"]').isVisible(),true);};
 const cases=[];
 const roundTrip=async(name,change)=>{const before=editable(await read());await change();await settle();const after=editable(await read());assert.notDeepEqual(after,before,name+' changes the saved head');await history('undo');assert.deepEqual(editable(await read()),before,name+' one-step undo');await history('redo');assert.deepEqual(editable(await read()),after,name+' redo');cases.push(name);};
 await open();const ac=await page.evaluate(()=>hc.fiveSimulation.builtinHeads.find(h=>h.kind==='AC'));await page.locator('[data-five-simulation-field="head-choice"]').selectOption('builtin:'+ac.id);await settle();await page.waitForFunction(()=>hc.fiveHeadPreview?.phase==='ready');
 const before=await read(),oldHead=before.setups.find(s=>s.id===before.activeSetupId).headModel;
 await page.locator('[data-five-head-collider-field="show"]').check();assert.equal(await page.evaluate(()=>hc.fiveHeadPreview.colliderObjects.size),9);
 assert.deepEqual(await page.locator('.five-head-preview-joint-row button').allTextContents(),['C 试转','A 试转']);
 const box=await page.evaluate(()=>structuredClone(hc.fiveHeadColliders.parts.find(p=>p.index===hc.fiveHeadColliders.partIndex).boxes[0]));await cmd('edit:'+box.id).click();
 const toolbar=page.locator('.five-head-preview-stage [data-transform-owner="head-collider"]');assert.equal(await toolbar.isVisible(),true);assert.equal(await page.locator('.five-head-settings .five-placement-tools,.five-head-settings .five-collider-tools').count(),0);
 assert.deepEqual(await toolbar.locator('button').allTextContents(),['平移W','旋转E','缩放R','边界M','聚焦F']);
 const modesBefore=editable(await read());
 for(const [key,mode] of [['w','translate'],['e','rotate'],['r','scale'],['m','bounds']]){await page.locator('[data-five-head-preview]').focus();await page.keyboard.press(key);await page.waitForFunction(mode=>hc.fiveHeadColliders.mode===mode,mode);assert.equal(await toolbar.locator(`[data-transform-mode="${mode}"]`).getAttribute('aria-pressed'),'true');}
 assert.deepEqual(editable(await read()),modesBefore,'Switching scene tools creates no edit');
 assert.equal(await page.evaluate(()=>{const r=document.querySelector('.five-head-preview-stage').getBoundingClientRect(),t=document.querySelector('[data-transform-owner="head-collider"]').getBoundingClientRect();return t.left>=r.left&&t.right<=r.right&&t.top>=r.top&&t.bottom<=r.bottom;}),true);
 report.sceneToolbar=true;report.sceneShortcuts=['W','E','R','M','F'];
 const sizeX=page.locator(`[data-five-head-collider-field="box:${box.id}:size:0"]`);
 await page.evaluate(()=>{window.hcCanvas=hc.fiveHeadPreview.canvas;window.hcMainCanvas=hc.viewport.renderer.domElement;window.hcModel=hc.fiveHeadPreview.model;});
 await sizeX.fill(String(box.size[0]+12));await sizeX.press('Tab');await settle();
 let state=await read(),head=state.setups.find(s=>s.id===state.activeSetupId).headModel;assert.ok(Math.abs(head.collisionBoxes[0].geometry.size[0]*1000-box.size[0]-12)<1e-7);
 await history('undo');head=(await read()).setups.find(s=>s.id===state.activeSetupId).headModel;assert.ok(Math.abs(head.collisionBoxes[0].geometry.size[0]*1000-box.size[0])<1e-7);
 await history('redo');head=(await read()).setups.find(s=>s.id===state.activeSetupId).headModel;assert.ok(Math.abs(head.collisionBoxes[0].geometry.size[0]*1000-box.size[0]-12)<1e-7);cases.push('尺寸数值');
 assert.equal(await page.evaluate(()=>hcCanvas===hc.fiveHeadPreview.canvas&&hcMainCanvas===hc.viewport.renderer.domElement&&hcModel===hc.fiveHeadPreview.model),true);
 const field=key=>page.locator(`[data-five-head-collider-field="box:${box.id}:${key}"]`);
 // A native history refresh updates focused values without replacing the input.
 await field('size:0').focus();await field('size:0').evaluate(input=>{input.setSelectionRange(0,2);window.hcFocused=input;});await history('undo');assert.equal(await field('size:0').inputValue(),String(Number(box.size[0].toFixed(4))));assert.equal(await page.evaluate(()=>document.activeElement===hcFocused&&hcFocused.selectionStart===0&&hcFocused.selectionEnd===2),true);await history('redo');assert.equal(await page.evaluate(()=>document.activeElement===hcFocused&&hcFocused.selectionStart===0&&hcFocused.selectionEnd===2),true);report.historyFocusAndSelectionRetained=true;
 for(const [name,key,value] of [['名称','name','桌面可撤销碰撞盒'],['中心数值','position:0','8'],['旋转数值','rotation:2','17']])await roundTrip(name,async()=>{await field(key).fill(value);await field(key).press('Tab');});
 await roundTrip('禁用',()=>field('enabled').uncheck());await roundTrip('启用',()=>field('enabled').check());
 let added;await roundTrip('添加',async()=>{await cmd('add').click();await settle();added=await page.evaluate(()=>hc.fiveHeadColliders.active);});
 await roundTrip('删除',()=>cmd('remove:'+added).click());await cmd('edit:'+box.id).click();
 // Invalid text never entered native history: undo only discards that draft.
 const valid=editable(await read());await field('size:0').fill('0');await page.waitForFunction(()=>hc.fiveHeadColliders.error.includes('尺寸'));await history('undo');assert.deepEqual(editable(await read()),valid);assert.ok(Number(await field('size:0').inputValue())>0);cases.push('无效输入撤回不消耗工程历史');
 await page.evaluate(()=>{window.fixture={view:{get viewport(){const s=hc.fiveHeadPreview;return {camera:s.camera,renderer:s.renderer,get transformManipulator(){return s.colliderManipulator;}};}}};});
 const driver=gizmoDriver(page);
 for(const [mode,axis] of [['translate','X'],['rotate','Z'],['scale','X']]){
  await cmd('mode:'+mode).click();await cmd('focus').click();await roundTrip(mode,()=>driver.drag(axis,22));
 }
 // A held drag is preview-only and blocks history; Escape leaves no entry.
 const cancelled=editable(await read());await cmd('mode:translate').click();await driver.drag('X',18,false);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));assert.deepEqual(editable(await read()),cancelled);await page.keyboard.press('Escape');await page.mouse.up();await settle();assert.deepEqual(editable(await read()),cancelled);cases.push('拖动中阻止历史切换，Esc 取消');
 await cmd('mode:bounds').click();await cmd('focus').click();
 const handle=await page.evaluate(async()=>{const T=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),s=hc.fiveHeadColliders,p=s.parts.find(p=>p.index===s.partIndex),e=hc.fiveHeadPreview.colliderObjects.get(p.index+':'+s.active),v=hc.fiveHeadPreview,r=v.canvas.getBoundingClientRect();e.wire.updateWorldMatrix(true,false);const points=[];for(let i=0;i<6;i++){const a=Math.floor(i/2),sign=i%2?1:-1,pos=new T.Vector3().setComponent(a,sign*.5).applyMatrix4(e.wire.matrixWorld),q=pos.clone().project(v.camera),step=new T.Vector3().setComponent(a,sign).transformDirection(e.wire.matrixWorld),next=pos.clone().addScaledVector(step,10).project(v.camera),x=r.left+(q.x+1)*r.width/2,y=r.top+(1-q.y)*r.height/2,dx=(next.x-q.x)*r.width/2,dy=-(next.y-q.y)*r.height/2,n=Math.hypot(dx,dy);if(n>.01&&Math.abs(q.z)<1&&x>r.left+15&&x<r.right-15&&y>r.top+15&&y<r.bottom-15)points.push({x,y,dx:dx/n,dy:dy/n,n});}points.sort((a,b)=>b.n-a.n);if(!points.length)throw Error('No visible face handle');return points[0];});
 await roundTrip('盒面边界拖拽',async()=>{await page.mouse.move(handle.x,handle.y);await page.mouse.down();await page.mouse.move(handle.x+handle.dx*25,handle.y+handle.dy*25,{steps:8});await page.mouse.up();});
 // Consecutive undo/redo crosses different edit kinds in their original order.
 const sequence=[editable(await read())];await field('name').fill('连续历史 A');await field('name').press('Tab');await settle();sequence.push(editable(await read()));await field('position:1').fill('9');await field('position:1').press('Tab');await settle();sequence.push(editable(await read()));await field('enabled').uncheck();await settle();sequence.push(editable(await read()));
 for(let i=2;i>=0;i--){await history('undo');assert.deepEqual(editable(await read()),sequence[i]);}for(let i=1;i<=3;i++){await history('redo');assert.deepEqual(editable(await read()),sequence[i]);}await roundTrip('恢复启用',()=>field('enabled').check());
 const shortcutBefore=editable(await read());await field('name').fill('快捷键历史验证');await field('name').press('Tab');await settle();const shortcutAfter=editable(await read());
 await page.locator('[data-five-head-preview]').focus();await page.keyboard.press('Control+z');await page.waitForFunction(()=>hc.scene.domain.setups.find(s=>s.id===hc.scene.domain.activeSetupId).headModel.collisionBoxes[0].name==='连续历史 A'&&!hc.fiveHistoryBusy&&!hc.pending);assert.deepEqual(editable(await read()),shortcutBefore);
 await page.keyboard.press('Control+y');await page.waitForFunction(()=>hc.scene.domain.setups.find(s=>s.id===hc.scene.domain.activeSetupId).headModel.collisionBoxes[0].name==='快捷键历史验证'&&!hc.fiveHistoryBusy&&!hc.pending);assert.deepEqual(editable(await read()),shortcutAfter);report.historyKeyboardShortcuts=true;
 report.undoRedoCases=cases;report.consecutiveUndoRedo=3;report.editorStayedOpen=true;
 const joints=page.locator('[data-five-head-joint]');await joints.nth(0).fill('30');await joints.nth(0).dispatchEvent('input');await joints.nth(1).fill('20');await joints.nth(1).dispatchEvent('input');
 await page.locator('[data-five-head-preview-reset]').click();await cmd('mode:bounds').click();
 await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,'切割头碰撞盒查看与编辑.png')});
 for(const key of ['workpieces','placements','intentToolpaths','fixtures','fixtureUnits'])assert.deepEqual(state[key],before[key]);assert.deepEqual(head.previewRef,oldHead.previewRef);
 const menu=page.locator('[data-five-key="head-instance-actions"]');await menu.locator('summary').click();await page.locator('[data-five-command="five.head-instance.library-open"]').click();
 const libraryName='桌面碰撞盒编辑验收-'+Date.now();await page.locator('[data-five-key="head-save-configuration"] > summary').click();await page.locator('[data-five-head-instance-field="profileName"]').fill(libraryName);await page.locator('[data-five-command="five.head-instance.save-profile"]').click();await settle();
 const entry=(await page.evaluate(()=>hc.viewportSceneProxy.invoke('CamHead.Configurations',{action:'list'}))).items.find(e=>e.name===libraryName);assert.ok(entry);
 await page.locator('[data-five-head-instance-field="entryId"]').selectOption(entry.id);await page.locator('[data-five-command="five.head-instance.use-profile"]').click();await settle();
 state=await read();const expected=state.setups.find(s=>s.id===state.activeSetupId).headModel.instance.parameters.collisionOverrides;assert.ok(expected?.length);await page.locator('[data-five-command="five.head-instance.library-close"]').click();
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));await settle();assert.ok(existsSync(project));
 const closing=page.evaluate(()=>__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId));await page.locator('[data-project-close-choice="save"]').click();await closing;await open();await settle();state=await read();head=state.setups.find(s=>s.id===state.activeSetupId).headModel;
 assert.deepEqual(head.instance.parameters.collisionOverrides,expected);assert.equal(head.instance.profile.id,entry.id);await page.locator('[data-five-head-collider-field="show"]').check();assert.equal(await page.evaluate(()=>hc.fiveHeadPreview.colliderObjects.size),9);
 report.nativeUndoRedo=true;report.personalLibrary=true;report.actualProjectCloseReopen=true;report.previewAndMainCanvasRetained=true;report.flatJointRows=['C','A'];assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page){report.state=await page.evaluate(()=>({pending:hc?.pending,error:hc?.fiveHeadColliders?.error,saving:!!hc?.fiveHeadColliders?.saving,request:hc?.fiveHeadColliders?.request})).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
