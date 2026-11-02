import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/ui-history-20261023';
const suppliedPath=process.argv.includes('--supplied-file-path');
mkdirSync(out+'/imports',{recursive:true});
writeFileSync(out+'/imports/undo-audit.urdf','<robot name="Undo audit box"><link name="base"><visual><geometry><box size=".2 .3 .4"/></geometry></visual><collision><geometry><box size=".2 .3 .4"/></geometry></collision></link></robot>');
try{
const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0];
await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);});
await page.locator('[data-tab-id="simulation"]').click();
const errors=[];page.on('pageerror',e=>errors.push(e.message));
const settle=async()=>{await page.waitForFunction(async()=>{const {definitionHistoryState}=await import('../../../apps/five-axis-cam/webpage/machineDefinitionHistory.mjs');return !__icaxAppShell.getState().pendingCount&&!mv.pending&&!mv.fiveDefinitionRefresh&&!mv.fiveHistoryBusy&&!mv.fiveMachine.busy&&!definitionHistoryState().blocked;});await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));};
const list=()=>page.evaluate(async()=>(await mv.viewportSceneProxy.project.product.invoke('MachineDefinition.List',{})).definitions);
const history=()=>page.evaluate(()=>mv.viewportSceneProxy.project.product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'}));
const sceneHistory=()=>page.evaluate(()=>mv.viewportSceneProxy.getUndoRedoState());
const initialScene=await sceneHistory(),records=[],states=[await list()];
const replay=async(direction,keyboard=false)=>{
 const before=await history();if(keyboard){await page.locator('[data-tab-id="simulation"]').focus();await page.keyboard.press(direction==='undo'?'Control+z':'Control+y');}
 else await page.locator(`[data-action="${direction}-project"]`).click();
 await page.waitForFunction(async b=>JSON.stringify(await mv.viewportSceneProxy.project.product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'}))!==b,JSON.stringify(before));await settle();
};
const checkUI=async(expected)=>{
 await page.waitForFunction(defs=>document.querySelectorAll('[data-five-command^="five.machine.definition-select:"]').length===defs.length&&defs.every(d=>document.querySelector(`[data-five-command="five.machine.definition-toggle:${d.id}"]`)?.getAttribute('aria-checked')===String(d.enabled)),expected,{timeout:10000});
 for(const d of expected){const row=page.locator(`[data-five-command="five.machine.definition-select:${d.id}"]`);await row.waitFor({state:'visible',timeout:10000});assert.equal(await row.count(),1);const toggle=page.locator(`[data-five-command="five.machine.definition-toggle:${d.id}"]`);assert.equal(await toggle.getAttribute('aria-checked'),String(d.enabled));}
 assert.equal(await page.locator('[data-five-command^="five.machine.definition-select:"]').count(),expected.length);
 assert.deepEqual(await sceneHistory(),initialScene,'definition operations cannot alter scene history');
};
const cycle=async(name,action)=>{
 const before=await list();await action();await settle();const after=await list();assert.notDeepEqual(after,before,name);await checkUI(after);
 await replay('undo',true);assert.deepEqual(await list(),before);await checkUI(before);
 await replay('redo');assert.deepEqual(await list(),after);await checkUI(after);
 records.push({name,before,after,undo:true,redo:true,ui:true});states.push(after);console.log('PASS',name);
 writeFileSync(out+'/definition-ui-progress.json',JSON.stringify({records},null,2));
};
const importUI=async(path)=>{
 const before=(await list()).length;
 writeFileSync(out+'/next-dialog.json',JSON.stringify({path}));console.log('NATIVE_FILE_PICKER',path);
 if(suppliedPath)await page.evaluate(path=>{const bridge=globalThis.icax??globalThis.icaxNativeBridge;window.auditChooser=bridge.openFileDialog;bridge.openFileDialog=async()=>path;},path);
 try{await page.locator('[data-command-id="five.machine-panel.import"]').click({timeout:180000});}
 finally{if(suppliedPath)await page.evaluate(()=>{(globalThis.icax??globalThis.icaxNativeBridge).openFileDialog=window.auditChooser;delete window.auditChooser;});}
 await page.waitForFunction(async n=>(await mv.viewportSceneProxy.project.product.invoke('MachineDefinition.List',{})).definitions.length===n+1,before,{timeout:180000});
};
await cycle(`导入 URDF（${suppliedPath?'脚本提供文件路径':'系统文件对话框'}）`,()=>importUI(out+'/imports/undo-audit.urdf'));
const urdf=(await list()).find(d=>!states[0].some(a=>a.id===d.id));
await cycle(`导入 SDF（${suppliedPath?'脚本提供文件路径':'系统文件对话框'}）`,()=>importUI('D:/penguinchinage0609/iCAX/src/apps/five-axis-cam/resources/machines/industrial-arm/IR-1600.sdf'));
const sdf=(await list()).find(d=>!states[1].some(a=>a.id===d.id));
const toggle=()=>page.locator(`[data-five-command="five.machine.definition-toggle:${sdf.id}"]`).click();
await cycle('禁用定义',toggle);await cycle('启用定义',toggle);
await page.locator(`[data-five-command="five.machine.definition-select:${urdf.id}"]`).click();await settle();
await cycle('删除定义',()=>page.locator('[data-command-id="five.machine-panel.delete"]').click());
await cycle('保存前禁用定义',toggle);
for(let i=records.length-1;i>=0;i--){await replay('undo');assert.deepEqual(await list(),states[i]);await checkUI(states[i]);}
for(let i=1;i<states.length;i++){await replay('redo',true);assert.deepEqual(await list(),states[i]);await checkUI(states[i]);}
await settle();await page.screenshot({path:out+'/definitions.png'});
assert.deepEqual(errors,[]);writeFileSync(out+'/definition-ui-report.json',JSON.stringify({pass:true,records,final:await list(),urdfId:urdf.id,sdfId:sdf.id,mixedUndoRedo:true,nativeFileDialogs:!suppliedPath,suppliedFilePath:suppliedPath,sceneHistoryUnchanged:true,errors},null,2));
console.log('PASS actual definition UI audit');process.exit();
}catch(e){console.error(e);process.exit(1);}
