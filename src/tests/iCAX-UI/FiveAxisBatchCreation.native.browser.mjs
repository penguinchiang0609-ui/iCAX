import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';
import {deserializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

export async function verifyBatchCreation({page,rpc,calls,output,sourcePath}){
 page.setDefaultTimeout(60000);
 const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload}),read=()=>invoke('CamScene.ReadState');
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
 const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await idle();};
 await selectWorkpieceFile(page,sourcePath);await idle();
 let state=await read();if(!state.setups.length)await command('five.setup.create');
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();
 await command('five.extraction.begin.Faces');
 const before=await read(),inventory=await rpc({action:'face-inventory'});
 const targetIds=inventory.faces.filter(f=>+f.wireCount>1).sort((a,b)=>+b.wireCount- +a.wireCount).map(f=>f.id);
 const picked=await page.evaluate(async targetIds=>{
  const view=fixture.view,vp=view.viewport,placement=view.scene.domain.placements.find(p=>p.id===view.scene.activePlacementId);
  const mapped=await fixture.context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:view.scene.domain.expected,setupId:placement.setupId,placementId:placement.id});
  const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
  vp.setViewDirection([1,-1,1]);vp.fitView();await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
  vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);const mesh=vp.sceneObjects.get(placement.id),g=mesh.geometry,p=g.attributes.position,r=vp.renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster();
  for(const id of targetIds)for(const f of mapped.faces.filter(f=>String(f.faceId)===id))for(let i=+f.first;i<+f.first+ +f.count;i++){
   const q=new THREE.Vector3();for(let k=0;k<3;k++)q.add(new THREE.Vector3().fromBufferAttribute(p,g.index?g.index.getX(i*3+k):i*3+k));q.multiplyScalar(1/3).applyMatrix4(mesh.matrixWorld).project(vp.camera);
   const x=r.x+(q.x+1)*r.width/2,y=r.y+(1-q.y)*r.height/2;
   if(Math.abs(q.x)>.9||Math.abs(q.y)>.9||Math.abs(q.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
   ray.setFromCamera({x:q.x,y:q.y},vp.camera);const hit=ray.intersectObject(mesh,false)[0];
   if(hit&&hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count)return {x,y,faceId:id};
  }
  throw Error('No visible non-machining face for the real mouse test');
 },targetIds);
 await page.mouse.click(picked.x,picked.y);await idle();
 const selected=await page.evaluate(async()=>{const m=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');return m.extractionDraft(fixture.view).extraction.faces.map(f=>f.localId);});
 assert.deepEqual(selected,[picked.faceId]);
 await page.getByLabel('加工观察方向',{exact:true}).click();
 await page.getByRole('textbox',{name:'加工观察方向 Z',exact:true}).fill('-2');
 await page.getByRole('textbox',{name:'加工观察方向 Z',exact:true}).press('Escape');
 await page.getByRole('checkbox',{name:'内孔',exact:true}).check();await page.getByRole('checkbox',{name:'外轮廓',exact:true}).check();
 assert.equal(await page.locator('[data-five-command="five.extraction.run"]').count(),0);
 await page.getByRole('button',{name:'确定',exact:true}).click();await idle();
 assert.equal(await page.evaluate(()=>fixture.view.error),'');
 const recorded=calls.filter(c=>c.method==='CamCut.ExtractCandidates').at(-1),request=recorded.payload??deserializeVariantText(recorded.payloadText);
 assert.equal(request.selection.mode,'Faces');assert.deepEqual(request.selection.faceUses.map(f=>f.localId),selected);assert.deepEqual(request.side.observationDirection,[0,0,-1]);
 state=await read();assert.ok(state.intentToolpaths.length>0);assert.deepEqual(state.workpieces,before.workpieces);
 const count=state.intentToolpaths.length,first=state.intentToolpaths[0];
 await page.locator(`[data-five-intent="${first.id}"]`).click();await command('five.tree.Delete');
 assert.equal((await read()).intentToolpaths.length,count-1);
 await rpc({action:'undo'});await command('five.refresh');assert.equal((await read()).intentToolpaths.length,count);
 const saved=await read();await rpc({action:'export-project',path:resolve(output,'batch-created.i5cam')});await rpc({action:'save-reopen'});await command('five.refresh');const reopened=await read();
 assert.deepEqual(reopened.intentToolpaths,saved.intentToolpaths);assert.deepEqual(reopened.workpieces,before.workpieces);
 await page.screenshot({path:resolve(output,'native-batch-created.png')});
 writeFileSync(resolve(output,'batch-creation-report.json'),JSON.stringify({passed:true,sourcePath,pickedFace:picked.faceId,realMouseFaceSelection:true,realNativeRecognition:true,request,createdCount:count,deleteUndo:true,saveReopen:true,cadUnchanged:true,standaloneCef:false},null,2));
}
