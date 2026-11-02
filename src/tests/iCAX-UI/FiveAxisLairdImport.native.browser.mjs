import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyLairdImport({page,rpc,root,output,errors,scaleModels=false}) {
  page.setDefaultTimeout(120000);
  const results=[];
  for (const [index,name] of ['BMI-S-211-C.step','BMI-S-230-C.step'].entries()) {
    await selectWorkpieceFile(page,resolve(root,'TestData/real-sheet-metal/laird-stamped-covers',name));
    await page.waitForFunction(count=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&fixture.view.scene?.domain?.workpieces?.length===count,index+1,{timeout:120000});
    await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount>0);
    assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const state=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
    assert.equal(state.workpieces.length,index+1);
    const workpiece=state.workpieces.find(w=>w.name===name);
    assert.ok(workpiece,`Persisted ${name}`);
    assert.equal(workpiece.importMetadata.validBRep,true);
    const display=await page.evaluate(()=>fixture.view.viewport.getDebugState());
    assert.ok(display.contentBounds,`Visible bounds for ${name}`);
    await page.screenshot({path:resolve(output,`${name}.png`)});
    results.push({name,workpiece,display});
    if(scaleModels){
      const bounds=()=>page.evaluate(()=>{
        const object=fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId);
        object.geometry.computeBoundingBox();return object.geometry.boundingBox.getSize(object.position.clone()).toArray();
      });
      const meshCounts=()=>page.evaluate(()=>{
        const g=fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId).geometry;
        return {vertices:g.getAttribute('position').count,indices:g.index?.count??0};
      });
      const meshBefore=await meshCounts();
      const before=await bounds(),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
      const {pick,numeric}=gizmoDriver(page);await command('five.tool.scale');await pick('X');
      await numeric().fill('1.25');const scaleStarted=Date.now();await numeric().press('Enter');
      await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fivePlacement?.request);
      assert.equal(await page.evaluate(()=>fixture.view.error),'');
      const after=await bounds();
      const meshAfter=await meshCounts();assert.deepEqual(meshAfter,meshBefore,'uniform scale must preserve the source display triangulation');
      for(let i=0;i<3;i++)assert.ok(Math.abs(after[i]-before[i]*1.25)<.01,`${name}: uniformly scaled dimension ${i}`);
      const scaled=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
      assert.notDeepEqual(scaled.workpieces[index].geometryRef,workpiece.geometryRef);
      const inspection=await rpc({action:'invoke',scope:'scene',method:'CamWorkpiece.Inspect',payload:{workpieceId:workpiece.id}});
      assert.equal(Number(inspection.cadInspection.solidCount),1);
      assert.equal(Number(inspection.cadInspection.faceCount),index===0?654:409);
      await page.screenshot({path:resolve(output,`${name}.scaled.png`)});
      results.at(-1).scale={before,after,meshBefore,meshAfter,elapsedMs:Date.now()-scaleStarted,geometryRef:scaled.workpieces[index].geometryRef,inspection};
      await command('five.tool.select');
    }
  }
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'laird-report.json'),JSON.stringify({passed:true,results,
    coverage:'Production chooser, native Scene import and persisted workpieces, production View resources and Edge viewport; standalone CEF not covered'},null,2));
  console.log('Both Laird STEP models imported and displayed; two persisted workpieces verified');
}
