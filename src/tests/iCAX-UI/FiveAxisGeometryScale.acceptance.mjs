import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';
import {startQuietOperationProbe,stopQuietOperationProbe} from './fixtures/quietOperationProbe.mjs';

export async function verifyGeometryScale({page,rpc,info,output,modelPath}){
  const cases=[],model=modelPath||info.sourcePath,hash=()=>createHash('sha256').update(readFileSync(model)).digest('hex'),sourceHash=hash();
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),refresh=()=>page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  await selectWorkpieceFile(page,model);await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  const frameField=(row,axis)=>page.locator(`[data-five-frame-field="${row}-${axis}"]`),frameSaved=()=>page.waitForFunction(()=>!!fixture.view.fiveAlignment&&!fixture.view.fiveAlignment.saving&&!fixture.view.fiveAlignment.saveTimer&&!fixture.view.fiveAlignment.dirty);
  await frameField('origin','X').fill('12.5');await frameField('origin','Y').fill('-4.25');await frameField('origin','Z').fill('7.3');await frameField('origin','Z').press('Enter');await frameSaved();
  await page.locator('[data-five-command="five.align.flip.x"]').click();await frameSaved();
  const parameterValues=()=>page.locator('[data-five-frame-field]').evaluateAll(nodes=>nodes.map(n=>n.value)),authoredValues=await parameterValues(),authoredFrame=(await read()).workpieces[0].coordinateFrame;
  assert.deepEqual(authoredFrame.origin,[12.5,-4.25,7.3]);assert.deepEqual(authoredFrame.x,[-1,0,0]);
  await page.evaluate(async()=>{
    const {renderRibbonCommandIcon}=await import('/src/iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs');
    const group=fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='workpiece').groups.find(g=>g.title==='几何');
    fixture.geometryCommands=group.commands.map(c=>({id:c.id,title:c.title}));
    const host=document.createElement('nav');host.id='test-geometry-ribbon';host.className='ribbon';host.style.cssText='height:84px;background:white;display:flex;gap:12px;padding:10px;box-sizing:border-box';document.body.prepend(host);fixture.context.mount.style.height='calc(100vh - 84px)';
    const button=document.createElement('button'),c=group.commands[0];button.type='button';button.dataset.commandId=c.id;button.innerHTML=renderRibbonCommandIcon(c.iconName)+`<span>${c.title}</span>`;button.style.cssText='display:grid;justify-items:center;width:68px;background:white;border:0';host.append(button);
    button.addEventListener('click',()=>void fixture.entry.handleRibbonCommand(fixture.context,c.id));
    fixture.scaleCanvas=fixture.view.viewport.renderer.domElement;
  });
  assert.deepEqual(await page.evaluate(()=>fixture.geometryCommands),[{id:'five.tool.scale',title:'缩放'}]);
  assert.equal(await page.locator('.cam-info-pane [data-five-key="transform-parameters"],.cam-info-pane [data-five-transform-field^="scale-"]').count(),0);
  assert.equal(await page.locator('.cam-info-pane .five-frame-properties').count(),1);
  const open=()=>page.locator('#test-geometry-ribbon button').click(),dialog=page.getByRole('dialog',{name:'缩放工件',exact:true}),field=dialog.getByRole('textbox',{name:'缩放比例',exact:true});
  const size=()=>page.evaluate(()=>{const o=[...fixture.view.viewport.sceneObjects.values()][0];o.geometry.computeBoundingBox();return o.geometry.boundingBox.getSize(o.position.clone()).toArray();});
  const initial=await read(),initialSize=await size();await open();await dialog.waitFor();assert.equal(await field.inputValue(),'1');assert.equal(await field.evaluate(n=>document.activeElement===n),true);
  assert.deepEqual(await parameterValues(),authoredValues);
  await field.fill('1.1');assert.deepEqual(await read(),initial);await page.evaluate(()=>fixture.entry.prepareProjectSave(fixture.context));assert.deepEqual(await read(),initial);await page.screenshot({path:resolve(output,'scale-menu-input.png')});await dialog.getByRole('button',{name:'取消缩放',exact:true}).click();assert.deepEqual(await read(),initial);assert.deepEqual(await size(),initialSize);
  cases.push('The Geometry ribbon group contains only Scale; the sidebar has no scale properties; the compact operation opens focused, previews locally and Cancel leaves native geometry unchanged');

  await open();for(const value of ['0','-1','abc']){await field.fill(value);await dialog.getByRole('button',{name:'执行缩放',exact:true}).click();assert.equal(await dialog.count(),1);assert.deepEqual(await read(),initial);}
  await field.fill('1.1');await page.evaluate(()=>{fixture.holdAfterApply=true;fixture.scaleMethodStart=fixture.methods.length;});await startQuietOperationProbe(page);
  await dialog.getByRole('button',{name:'执行缩放',exact:true}).click();await page.waitForFunction(()=>fixture.held);
  assert.equal(await page.evaluate(()=>fixture.view.pending),false);assert.equal(await dialog.locator('[role="status"]').innerText(),'正在缩放…');
  await page.screenshot({path:resolve(output,'scale-local-saving.png')});
  const camera=await page.evaluate(()=>fixture.view.viewport.getCameraState()),canvas=page.locator('.cam-render-viewport canvas').first(),box=await canvas.boundingBox();await page.mouse.move(box.x+box.width*.7,box.y+box.height*.55);await page.mouse.wheel(0,-100);await page.waitForTimeout(100);
  assert.notDeepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);
  await page.evaluate(()=>fixture.release());await page.waitForFunction(()=>!fixture.view.fivePlacement?.saving&&!fixture.view.fivePlacement?.request);await dialog.waitFor({state:'detached'});
  const samples=await stopQuietOperationProbe(page);assert.deepEqual(samples,[]);
  const committed=await read(),scaled=await size();assert.notDeepEqual(committed.workpieces[0].geometryRef,initial.workpieces[0].geometryRef);assert.deepEqual(committed.placements.map(p=>[p.id,p.workpieceId,p.setupId,p.transform]),initial.placements.map(p=>[p.id,p.workpieceId,p.setupId,p.transform]));
  scaled.forEach((n,i)=>assert.ok(Math.abs(n-initialSize[i]*1.1)<.001));assert.equal(hash(),sourceHash);
  assert.deepEqual(committed.workpieces[0].coordinateFrame,authoredFrame);assert.deepEqual(await parameterValues(),authoredValues);
  const methods=await page.evaluate(()=>fixture.methods.slice(fixture.scaleMethodStart).map(m=>m.method));assert.equal(methods.filter(m=>m==='CamWorkpiece.Scale').length,1);assert.equal(methods.includes('Machine.List')||methods.includes('MachineDefinition.List'),false);
  assert.equal(await page.evaluate(()=>fixture.scaleCanvas===fixture.view.viewport.renderer.domElement),true);await page.screenshot({path:resolve(output,'scaled-once.png')});
  cases.push('Executing once changes the native BRep reference and exact-size mesh by 1.1, preserves placement and the imported file, closes the operation, keeps camera navigation responsive and never displays global progress throughout commit/resource refresh');

  await open();assert.equal(await field.inputValue(),'1');await page.keyboard.press('Escape');assert.deepEqual((await read()).workpieces,committed.workpieces);
  await rpc({action:'undo'});await refresh();assert.deepEqual((await read()).workpieces,initial.workpieces);await rpc({action:'redo'});await refresh();assert.deepEqual((await read()).workpieces,committed.workpieces);
  assert.deepEqual(await parameterValues(),authoredValues);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.workpieces,committed.workpieces);
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs'),{ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.sceneObjects.size===1);assert.deepEqual(await parameterValues(),authoredValues);
  cases.push('A nonzero authored origin and reversed axes retain all 12 sidebar values through opening/canceling Scale, native scale, undo/redo, and a fresh project remount after archive reopen');
  cases.push('Each new Scale operation starts at 1; Escape makes no new mutation; native undo/redo and archive save/reopen retain the single BRep scale');
  return {cases,model,sourceHash,initialSize,scaledSize:scaled,blockingSamples:samples,ribbonHarness:'production definition/icons and production entry dispatch in an isolated browser shell'};
}
