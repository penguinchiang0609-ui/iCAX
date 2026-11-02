import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

export async function verifyHeadChoiceImport({page,invoke,rpc,report,output}){
 const combo=page.locator('[data-five-simulation-field="head-choice"]');
 const finish=page.locator('[data-five-command="five.head-instance.finish-import"]');
 const idle=()=>page.waitForFunction(()=>!f.view.pending&&!f.view.fiveHeadEditor.busy&&!f.view.fiveSimulation.busy&&!f.view.fiveSimulation.headChoiceBusy);
 const current=()=>page.evaluate(()=>f.head().instance.id);
 await page.evaluate(()=>{f.pickerCalls=0;f.context.appProxy={bridge:{async openFileDialog(){f.pickerCalls++;throw Error('System file dialog must not be used');},async openDirectoryDialog(){f.pickerCalls++;throw Error('System directory dialog must not be used');}}};});
 const before=await invoke('CamScene.ReadState'),previous=await current(),previousChoice=await combo.inputValue();
 const picker=page.getByRole('dialog',{name:'导入切割头',exact:true});
 await combo.selectOption('import');await picker.waitFor();assert.equal(await picker.locator('[data-nesting-file-action="browse"]').isVisible(),false);
 await picker.locator('footer [data-nesting-file-action="cancel"]').click();await idle();assert.deepEqual(await invoke('CamScene.ReadState'),before);assert.equal(await combo.inputValue(),previousChoice);
 assert.equal(await page.locator('[data-five-key="head-import-prompt"]').count(),0);assert.equal(await page.evaluate(()=>f.pickerCalls),0);
 const file=resolve(output,'自定义切割头.obj');writeFileSync(file,'v 0 0 0\nv 20 0 0\nv 0 30 40\nf 1 2 3\n');
 for(const extension of ['urdf','sdf','STEP','stp','iges','igs','stl'])writeFileSync(resolve(output,'格式检查.'+extension),'file format listing test');
 const listed=(await invoke('CamWorkpiece.ListFiles',{directory:output,fileType:'cutting-head'})).entries.filter(e=>!e.directory);
 for(const extension of ['urdf','sdf','STEP','stp','iges','igs','stl','obj'])assert.ok(listed.some(e=>e.name.endsWith('.'+extension)),extension);
 const cad=(await invoke('CamWorkpiece.ListFiles',{directory:output})).entries.filter(e=>!e.directory);assert.ok(cad.every(e=>/\.(?:step|stp|iges|igs)$/i.test(e.name)));
 const importFile=async()=>{
  const beforeBrowse=await invoke('CamScene.ReadState');
  await combo.selectOption('import');await picker.waitFor();await picker.press('Control+l');
  await picker.locator('[data-nesting-file-directory]').fill(output);await picker.locator('[data-nesting-file-directory]').press('Enter');
  const row=picker.locator('[data-nesting-file-is-directory="false"]').filter({hasText:'自定义切割头.obj'});await row.click();
  await picker.locator('[aria-label="文件格式"]').selectOption('assembly');assert.equal(await row.isVisible(),false);
  await picker.locator('[aria-label="文件格式"]').selectOption('all');await row.click();
  await picker.screenshot({path:resolve(output,'程序内切割头文件选择.png')});
  assert.deepEqual(await invoke('CamScene.ReadState'),beforeBrowse);
  await picker.locator('footer [data-nesting-file-action="open"]').click();await idle();
  await page.waitForFunction(()=>f.view.fiveHeadPreview?.phase==='ready');
 };
 await importFile();
 assert.notEqual(await current(),previous,JSON.stringify(await page.evaluate(()=>({native:f.lastNativeError,error:f.view.fiveSimulation.headError,request:f.view.fiveSimulation.request}))));const imported=await current();
 const check=page.locator('[data-five-head-instance-field="importSave"]');assert.equal(await check.isChecked(),false);
 await finish.click();await idle();assert.equal((await invoke('CamHead.Configurations',{action:'list'})).items.length,0);
 assert.equal(await combo.locator('optgroup[label="我的"] option:checked').getAttribute('value'),imported);
 assert.deepEqual(await combo.locator('optgroup').evaluateAll(groups=>groups.map(g=>g.label)),['系统','我的']);
 // A second imported head is explicitly saved to the personal library.
 await importFile();await check.check();
 await page.locator('[data-five-head-instance-field="importName"]').fill('我的自定义切割头');await finish.click();await idle();
 const entries=(await invoke('CamHead.Configurations',{action:'list'})).items,entry=entries.find(e=>e.name==='我的自定义切割头');assert.ok(entry);
 assert.equal(await combo.locator('optgroup[label="我的"] option:checked').innerText(),'我的自定义切割头');
 assert.deepEqual(await combo.locator('optgroup').evaluateAll(groups=>groups.map(g=>g.label)),['系统','我的']);
 await combo.selectOption(previousChoice);await idle();assert.equal(await current(),previous);
 await rpc({action:'save-reopen'});
 await page.evaluate(()=>{f.view.fiveHeadEditor.sources.clear();f.view.fiveHeadEditor.profileLinks={};return f.refresh();});
 // The saved personal entry must work without an in-memory project link.
 await combo.selectOption('profile:'+entry.id);await idle();await page.waitForFunction(()=>f.view.fiveHeadPreview?.phase==='ready');
 assert.equal(await page.evaluate(()=>f.head().instance.profile.id),entry.id);assert.equal(await combo.locator('optgroup[label="我的"] option:checked').innerText(),'我的自定义切割头');
 assert.equal(await combo.locator('option').last().getAttribute('value'),'import');
 await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,'系统与我的切割头.png')});
 // Library deletion leaves the embedded head usable in the current project.
 await page.evaluate(async()=>{f.view.fiveHeadEditor.libraryOpen=true;await f.I.headInstanceCommand(f.context,f.view,'refresh-profiles',f.ops);});
 await page.locator('[data-five-head-instance-field="entryId"]').selectOption(entry.id);
 await page.locator('[data-five-command="five.head-instance.delete-profile"]').click();await idle();
 assert.equal((await invoke('CamHead.Configurations',{action:'list'})).items.length,0);assert.ok(await current());
 await page.locator('[data-five-command="five.head-instance.library-close"]').click();
 report.cases.push('Combobox import opens the picker once; cancel preserves the head; unchecked import stays in the project; checked import persists under My and loads after project reopen; System remains intact');
 assert.equal(await page.evaluate(()=>f.pickerCalls),0);
 report.filePicker='Production in-app file picker with native directory browsing, all eight extensions, filters, cancel and actual import; zero OS dialog calls';
}
