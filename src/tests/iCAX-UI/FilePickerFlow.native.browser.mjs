import assert from 'node:assert/strict';
import {resolve,dirname} from 'node:path';
import {writeFileSync} from 'node:fs';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';
export async function verifyFilePickerFlow({page,rpc,root,output,calls,errors}){
 const source=resolve(root,'TestData/FA/Video_HatRail_1200x220x70_t1p2.step'),directory=dirname(source).replaceAll('\\','/'),parent=dirname(directory).replaceAll('\\','/'),cases=[];
 const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),initial=await read();
 await page.evaluate(()=>{fixture.previewCacheReceipts=[];const proxy=fixture.context.sceneProxy,invoke=proxy.invoke;proxy.invoke=async function(method,payload,options){const result=await invoke.call(this,method,payload,options);if(method==='CamWorkpiece.QueryFilePreview'&&result.status==='Completed')fixture.previewCacheReceipts.push(result.result.previewCache);return result;};fixture.pickerCanvas=fixture.view.viewport.renderer.domElement;});
 const ready=()=>page.waitForFunction(()=>fixture.view.fiveAxisFilePicker?.previewStatus==='ready',null,{timeout:120000});
 await selectWorkpieceFile(page,source,{preview:true,open:false});assert((await page.evaluate(()=>fixture.view.fiveAxisFilePicker.viewport.getDebugState().edgeCount))>0);assert.equal(await page.locator('[data-nesting-file-action="open"]').isEnabled(),true);
 assert.equal(await page.locator('[data-nesting-file-action="up"]').count(),0);
 const start=calls.length;await page.getByRole('combobox',{name:'文件格式'}).selectOption('step');assert.equal(await page.locator('[data-nesting-file-action="open"]').isEnabled(),true);assert(calls.slice(start).every(c=>c.action!=='invoke'||!/^CamWorkpiece\.(ListFiles|PreviewFile)$/.test(c.method||'')));
 await page.getByRole('combobox',{name:'文件格式'}).selectOption('iges');assert.equal(await page.locator('[data-nesting-file-action="open"]').isDisabled(),true);assert((await page.locator('[data-nesting-file-is-directory="false"]:visible').evaluateAll(rows=>rows.map(r=>r.dataset.nestingFilePath))).every(path=>/\.(igs|iges)$/i.test(path)));
 await page.getByRole('combobox',{name:'文件格式'}).selectOption('all');
 await page.locator(`[data-nesting-file-directory-target="${parent}"]`).click();await page.waitForFunction(path=>fixture.view.fiveAxisFilePicker.directory===path&&!document.querySelector('.tube-nesting-file-list').hasAttribute('aria-busy'),parent);
 await page.keyboard.press('Control+l');await page.locator('[data-nesting-file-directory]').fill(directory);await page.locator('[data-nesting-file-directory]').press('Enter');await page.locator(`[data-nesting-file-path="${source.replaceAll('\\','/')}"]`).click();await ready();
 assert.deepEqual(await read(),initial);cases.push('Actual native directory breadcrumbs and STEP/IGES filtering retain a valid real STEP preview and Open action without modifying the scene');
 await page.screenshot({path:resolve(output,'native-address-filter-preview.png')});
 await page.locator('footer [data-nesting-file-action="cancel"]').click();await page.locator('[data-five-command="workpiece.import"]').first().click();await ready();
 const receipts=await page.evaluate(()=>fixture.previewCacheReceipts);assert(receipts.at(-1).hit);assert.equal(await page.evaluate(()=>Object.hasOwn(fixture.view.fiveAxisFilePicker,'previewCache')),false);assert.deepEqual(await read(),initial);
 cases.push('Reopening obtains the real wireframe from the native disk cache; no historical preview Map exists in the picker');
 await page.locator('footer [data-nesting-file-action="open"]').click();await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.scene.domain.workpieces.length===1,null,{timeout:120000});await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1,null,{timeout:120000});
 const imported=await read();assert.equal(imported.workpieces.length,1);assert.equal(imported.placements.length,1);assert.equal(await page.evaluate(()=>fixture.view.viewport.renderer.domElement===fixture.pickerCanvas),true);assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.deepEqual(errors,[]);
 cases.push('Open follows the existing production import, creates the actual workpiece and placement, and retains the original canvas');
 writeFileSync(resolve(output,'file-picker-flow.json'),JSON.stringify({passed:true,source,cases,previewCacheReceipts:receipts,coverage:{native:true,productionUi:true,standaloneCef:false}},null,2));console.log('Native address/filter/disk-cache/import flow passed');
}
