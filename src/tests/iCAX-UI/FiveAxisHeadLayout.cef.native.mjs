import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/head-unified-20261101/cef');mkdirSync(output,{recursive:true});
const source=process.env.ICAX_HEAD_TEST_PROJECT||JSON.parse(readFileSync(resolve('output/acceptance/head-unified-20261101/browser/report.json'),'utf8')).project;
const project=resolve(output,`切割头同页设置-${Date.now()}.i5cam`);copyFileSync(source,project);
const report={passed:false,standaloneCef:true,project,errors:[]},browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9604');let page;
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));assert.ok(page);page.setDefaultTimeout(30000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await restoreNativeCefViewport(page);
 assert.ok(await page.evaluate(()=>innerWidth>=1000&&innerHeight>=650),'Activate the real test window before native layout acceptance');
 assert.equal(await page.evaluate(()=>!!__icaxAppShell.getState().activeProjectPath),false,'Preserve any user project');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const v=getProjectView(__icaxAppShell.getState().activeProjectId);return !!v?.scene?.domain&&!v.pending;},null,{timeout:120000});
 await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.hv=getProjectView(__icaxAppShell.getState().activeProjectId);});
 await page.locator('[data-tab-id="toolpath"]').click();await page.waitForFunction(()=>hv.fiveAxisArea==='toolpath'&&!hv.pending);await page.locator('[data-command-id="five.simulation.head-open"]').click();await page.waitForFunction(()=>hv.fiveHeadPreview?.phase==='ready');
 const read=()=>page.evaluate(()=>hv.viewportSceneProxy.invoke('CamScene.ReadState',{})),before=await read(),dialog=page.locator('[data-five-key="head-popover"]');
 assert.equal(await dialog.getAttribute('aria-label'),'切割头设置');assert.equal(await dialog.locator('[role="tablist"]').count(),0);
 const combo=dialog.locator('[data-five-simulation-field="head-choice"]');
 assert.equal(await dialog.locator('[data-five-key="head-selection"] select').count(),1);assert.ok(await combo.isVisible());
 assert.equal(await combo.locator('option').last().getAttribute('value'),'import');assert.ok(await combo.locator('optgroup[label="系统"] option').count()>0);
 const catalog=await page.evaluate(()=>hv.fiveSimulation.builtinHeads);assert.equal(catalog.length,5);
 assert.ok(catalog.every(h=>h.id.startsWith('6bfc0001-')&&!/机床|龙门|4\+1|3\+2/.test(h.name)&&!h.workpieceAxes.length));
 assert.equal(await dialog.locator('[data-five-head-instance-field="structure"],[data-five-head-instance-field$=".target"],[data-five-command="five.head-instance.machine"]').count(),0);
 assert.equal(await dialog.locator('[data-five-simulation-field="builtin-head"]').count(),0);
 assert.equal(await dialog.locator('[data-five-key="head-motion"]').evaluate(e=>e.open),false);
 assert.equal(await dialog.locator('[data-five-key="head-tcp"]').evaluate(e=>e.open),true);
 assert.equal(await dialog.locator('[data-five-key="head-configuration-library"]').count(),0);
 assert.equal(await dialog.locator('[data-five-head-joint]').count(),2);assert.equal(await dialog.evaluate(e=>e.scrollWidth<=e.clientWidth+1),true);
 await page.evaluate(()=>{window.hk={canvas:hv.fiveHeadPreview.canvas,model:hv.fiveHeadPreview.model,camera:hv.fiveHeadPreview.camera.position.toArray(),head:hv.fiveHeadPreview.model.children.map(o=>o.matrix.toArray())};});
 const slider=dialog.locator('[data-five-head-joint]').first();await slider.fill('25');await slider.dispatchEvent('input');
 assert.equal(await page.evaluate(()=>hv.fiveHeadPreview.workpiece),null);assert.notDeepEqual(await page.evaluate(()=>hv.fiveHeadPreview.model.children.map(o=>o.matrix.toArray())),await page.evaluate(()=>hk.head));
 assert.deepEqual(await page.evaluate(()=>hv.fiveHeadPreview.camera.position.toArray()),await page.evaluate(()=>hk.camera));
 const canvas=dialog.locator('canvas'),box=await canvas.boundingBox();await page.mouse.move(box.x+box.width*.5,box.y+box.height*.5);await page.mouse.down({button:'right'});await page.mouse.move(box.x+box.width*.6,box.y+box.height*.55,{steps:8});await page.mouse.up({button:'right'});
 assert.notDeepEqual(await page.evaluate(()=>hv.fiveHeadPreview.camera.position.toArray()),await page.evaluate(()=>hk.camera));
 assert.equal(await page.evaluate(()=>hk.canvas===hv.fiveHeadPreview.canvas&&hk.model===hv.fiveHeadPreview.model),true);assert.deepEqual(await read(),before);
 const model=await page.evaluate(()=>hv.fiveSimulation.builtinHeads.find(h=>h.kind==='A'));
 await combo.selectOption('builtin:'+model.id);
 await page.waitForFunction(()=>!hv.pending&&!hv.fiveSimulation.busy&&!hv.fiveSimulation.headChoiceBusy&&hv.fiveHeadPreview?.phase==='ready');
 const after=await read(),head=after.setups.find(s=>s.id===after.activeSetupId).headModel;assert.equal(head.builtinDefinitionId,model.id);
 assert.ok(head.instance.parameters.axes.every(a=>a.target==='Head'));assert.equal(await page.evaluate(()=>hv.fiveHeadPreview.workpiece),null);
 assert.equal(after.headInstances.length,before.headInstances.length+1,'One combobox change must commit once despite native input/change events');
 assert.equal(await page.evaluate(()=>hv.fiveHeadPreview.head.builtinDefinitionId),model.id);
 for(const key of ['workpieces','placements','intentToolpaths','fixtures','fixtureUnits'])assert.deepEqual(after[key],before[key]);
 await page.locator('[data-five-key="head-settings"]').evaluate(e=>e.scrollTop=0);
 await dialog.screenshot({path:resolve(output,'切割头同页设置.png')});
 // Exercise the same in-app browser the user sees, including native import
 // and the optional personal-library confirmation. No OS picker is used.
 const file=resolve(output,'桌面验收切割头.obj');writeFileSync(file,'v 0 0 0\nv 20 0 0\nv 0 30 40\nf 1 2 3\n');
 await combo.selectOption('import');const picker=page.getByRole('dialog',{name:'导入切割头',exact:true});await picker.waitFor();
 assert.equal(await picker.locator('[data-nesting-file-action="browse"]').isVisible(),false);
 await picker.locator('footer [data-nesting-file-action="cancel"]').click();await page.waitForFunction(()=>!hv.fiveSimulation.headChoiceBusy);assert.deepEqual(await read(),after);
 await combo.selectOption('import');await picker.waitFor();await picker.press('Control+l');await picker.locator('[data-nesting-file-directory]').fill(output);await picker.locator('[data-nesting-file-directory]').press('Enter');
 await picker.locator('[data-nesting-file-is-directory="false"]').filter({hasText:'桌面验收切割头.obj'}).click();
 await picker.screenshot({path:resolve(output,'程序内切割头文件选择.png')});
 await picker.locator('footer [data-nesting-file-action="open"]').click();await page.waitForFunction(()=>!hv.pending&&!hv.fiveSimulation.busy&&!hv.fiveSimulation.headChoiceBusy&&hv.fiveHeadPreview?.phase==='ready');
 const checkbox=dialog.locator('[data-five-head-instance-field="importSave"]');assert.equal(await checkbox.isChecked(),false);await checkbox.check();
 await dialog.locator('[data-five-head-instance-field="importName"]').fill('桌面验收 · 我的切割头');
 await dialog.locator('[data-five-command="five.head-instance.finish-import"]').click();await page.waitForFunction(()=>!hv.pending&&!hv.fiveHeadEditor.busy&&!hv.fiveHeadEditor.importPrompt);
 assert.equal(await combo.locator('optgroup[label="我的"] option:checked').innerText(),'桌面验收 · 我的切割头');
 const entries=await page.evaluate(()=>hv.viewportSceneProxy.invoke('CamHead.Configurations',{action:'list'}));assert.ok(entries.items.some(e=>e.name==='桌面验收 · 我的切割头'));
 await dialog.screenshot({path:resolve(output,'导入并添加到我的.png')});
 const definitions={
  '中文切割头.urdf':'<robot name="中文刀头"><link name="tool"><visual><geometry><box size=".04 .05 .08"/></geometry></visual><icax_tool name="laser" type="laser_cutting_head" tcp_mm="0 0 -40" beam="0 0 -1"/></link></robot>',
  '中文切割头.sdf':'<sdf version="1.7"><model name="中文刀头"><link name="tool"><visual name="body"><geometry><box><size>.04 .05 .08</size></box></geometry></visual></link></model></sdf>',
 };
 for(const [name,body] of Object.entries(definitions)){
  writeFileSync(resolve(output,name),body);await combo.selectOption('import');await picker.waitFor();await picker.press('Control+l');await picker.locator('[data-nesting-file-directory]').fill(output);await picker.locator('[data-nesting-file-directory]').press('Enter');
  await picker.locator('[data-nesting-file-is-directory="false"]').filter({hasText:name}).click();await picker.locator('footer [data-nesting-file-action="open"]').click();
  await dialog.locator('[data-five-head-instance-field="importSave"]').waitFor();await page.waitForFunction(()=>!hv.pending&&!hv.fiveSimulation.headChoiceBusy&&hv.fiveHeadPreview?.phase==='ready');
  const importedState=await read();assert.ok(importedState.setups.find(s=>s.id===importedState.activeSetupId)?.headModel.assembly);
  await dialog.locator('[data-five-command="five.head-instance.finish-import"]').click();
 }
 report.inAppFilePicker=true;report.actualNativeImport=true;report.personalLibrarySaved=true;
 report.standaloneHeadCatalog=catalog;report.machineAndWorkpieceControls=false;
 report.unicodeFileImports=['OBJ','URDF','SDF'];
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page){report.state=await page.evaluate(()=>({area:window.hv?.fiveAxisArea,headOpen:window.hv?.fiveSimulation?.headOpen,preview:window.hv?.fiveHeadPreview?.phase,message:window.hv?.fiveHeadPreview?.message,pending:window.hv?.pending,dialog:!!document.querySelector('[data-five-key="head-popover"]')})).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
