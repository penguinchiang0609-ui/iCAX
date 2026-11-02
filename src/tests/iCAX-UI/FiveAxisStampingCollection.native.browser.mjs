import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {readFileSync,writeFileSync} from 'node:fs';
import {selectModelFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyStampingCollection({page,rpc,output}){
 const root=resolve(process.env.ICAX_STAMPING_COLLECTION_DIRECTORY||'output/models/tebis-stamping-20261031'),models=JSON.parse(readFileSync(resolve(root,'models.json'),'utf8')).models;
 assert.equal(models.length,Number(process.env.ICAX_STAMPING_EXPECTED_COUNT||4));page.setDefaultTimeout(120000);
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&!fixture.view.fiveAxisImportChoosing);
 const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
 const cases=[];
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));await idle();
 for(const [index,model] of models.entries()){
  console.log('Import stamping model',model.id);
  // The production ribbon is hosted by the desktop shell; this fixture mounts
  // the project body and invokes that ribbon's actual command handler.
  await page.evaluate(()=>{void fixture.entry.handleRibbonCommand(fixture.context,'workpiece.import');});
  await selectModelFile(page,resolve(root,model.file+'.step'));await idle();
  const state=await read();assert.equal(state.workpieces.length,index+1);
  const workpiece=state.workpieces.find(w=>w.name.includes(model.file));assert.ok(workpiece,model.file);
  assert.equal(workpiece.importMetadata.validBRep,true);
  await page.waitForFunction(()=>{const v=fixture.view,p=v.scene.domain.placements.find(p=>p.id===v.scene.activePlacementId);return !!p&&!!v.viewport?.sceneObjects.get(p.id)?.geometry;});
  const rendered=await page.evaluate(()=>{const v=fixture.view,vp=v.viewport;vp.setViewDirection([1,-1,.9]);vp.fitView();const object=vp.sceneObjects.get(v.scene.activePlacementId);return {vertices:object.geometry.attributes.position.count,triangles:object.geometry.index?object.geometry.index.count/3:object.geometry.attributes.position.count/3,error:v.error};});
  assert.equal(rendered.error,'');assert.ok(rendered.triangles>500);
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  await page.screenshot({path:resolve(output,model.id+'-native-model.png')});
  cases.push({id:model.id,name:model.name,file:model.file+'.step',validNativeBRep:true,rendered});
  console.log('Imported and rendered stamping model',model.id);
 }
 const before=await read();await rpc({action:'export-project',path:resolve(root,process.env.ICAX_STAMPING_PROJECT_NAME||'四款冲压件模型浏览.i5cam')});await rpc({action:'save-reopen'});
 const after=await read();assert.deepEqual(after.workpieces,before.workpieces);assert.deepEqual(after.placements,before.placements);
 assert.equal(after.intentToolpaths.length,0);
 writeFileSync(resolve(root,'native-import-audit.json'),JSON.stringify({passed:true,nativeGeometryImport:true,browserEngine:'Edge',standaloneCef:false,osFilePicker:false,independentWorkpieces:cases.length,saveReopen:true,toolpathsCreated:false,cases},null,2));
}
