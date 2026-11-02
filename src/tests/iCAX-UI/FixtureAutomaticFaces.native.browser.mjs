import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFixtureAutomaticFaces({page,rpc,calls,output,sourcePath,beforeRecognition}){
 const cases=[],command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,undefined,{timeout:300000});
 const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
 await selectWorkpieceFile(page,sourcePath);await idle();
 await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size===1);
 await command('five.setup.create');await idle();
 await command('five.fixture.generate');await idle();
 assert.equal(await page.locator('[data-five-command="five.fixture.design.auto"]').count(),1);
 assert.equal(await page.evaluate(()=>!!fixture.view.fixtureDesignPicking),false);
 if(beforeRecognition)await beforeRecognition();
 const before=await read(),start=calls.length,started=Date.now();
 await page.locator('[data-five-command="five.fixture.design.auto"]').click();await idle();
 assert.equal(await page.evaluate(()=>fixture.view.error),'');
 const automaticMs=Date.now()-started;
 const automatic=await page.evaluate(()=>({faces:[...fixture.view.fixtureDesignDraft.supportFaces],picking:fixture.view.fixtureDesignPicking,highlight:fixture.view.fixtureDesignOverlay?.geometry.attributes.position.count}));
 assert(automatic.faces.length>0);assert(automatic.highlight>0);assert.equal(automatic.picking,false);
 const after=await read();for(const key of ['expected','workpieces','placements','setups','fixtures','intentToolpaths'])assert.deepEqual(after[key],before[key],key);
 assert(!calls.slice(start).some(c=>/SetRecipe|Generate|Save/.test(c.method||'')));
 cases.push('Explicit automatic recognition fills and highlights an editable selection on the real STEP without changing CAD, recipes, fixtures or engineering revision');
 await page.screenshot({path:resolve(output,'automatic-selection.png')});
 await command('five.fixture.design.pick');await idle();
 // Actual viewport clicks remove and restore an automatically selected face.
 await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,-1]);fixture.view.viewport.fitView();});
 await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 const pixel=await page.evaluate(async()=>{
  const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,mesh=vp.sceneObjects.get(v.fixtureDesignPlacement),g=mesh.geometry,p=g.attributes.position,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
  mesh.updateWorldMatrix(true,true);vp.camera.updateMatrixWorld(true);
  for(const f of v.fixtureDesignFaces.filter(f=>v.fixtureDesignDraft.supportFaces.includes(String(f.faceId))).sort((a,b)=>+b.count- +a.count))for(let i=+f.first;i<+f.first+ +f.count;++i){
   const q=new T.Vector3();for(let k=0;k<3;++k)q.add(new T.Vector3().fromBufferAttribute(p,g.index?g.index.getX(i*3+k):i*3+k));q.multiplyScalar(1/3).applyMatrix4(mesh.matrixWorld).project(vp.camera);
   const x=r.x+(q.x+1)*r.width/2,y=r.y+(1-q.y)*r.height/2;
   if(Math.abs(q.x)>.9||Math.abs(q.y)>.9||Math.abs(q.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
   ray.setFromCamera({x:q.x,y:q.y},vp.camera);const hit=ray.intersectObject(mesh,false)[0];if(hit&&hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count)return {x,y,faceId:String(f.faceId)};
  }throw Error('No visible automatically selected face for the real mouse test');
 });
 const editedStart=calls.length;
 await page.mouse.click(pixel.x,pixel.y);await idle();assert.equal(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),pixel.faceId),false);
 await page.mouse.click(pixel.x,pixel.y);await idle();assert.equal(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),pixel.faceId),true);
 assert.equal(calls.slice(editedStart).filter(c=>c.method).length,0);
 cases.push('Real viewport clicks cancel and restore a suggested face entirely in the draft, with no native computation or save');
 // Hold the native face-map response while the user continues scrolling and
 // typing. The production panel patch must retain the latest interaction.
 await command('five.fixture.design.pick');await idle();
 await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{overflow:auto!important;max-height:400px!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;min-height:1400px}.five-design-body{max-height:300px!important}'});
 await page.evaluate(()=>{fixture.holdMethod='CamPlacement.ReadReferenceFaces';fixture.hold=true;fixture.keptInput=document.querySelector('[data-five-fixture-field="design:spacing"]');fixture.keptCanvas=fixture.view.viewport.renderer.domElement;fixture.probeCount=0;fixture.keptInput.addEventListener('probe',()=>fixture.probeCount++);void fixture.entry.handleRibbonCommand(fixture.context,'five.fixture.design.pick');});
 await page.waitForFunction(()=>fixture.held);
 const scroll=await page.evaluate(()=>{const n=fixture.keptInput;n.focus({preventScroll:true});n.setSelectionRange(0,1);return ['.cam-context-pane','.cam-info-pane','.five-design-body'].map((s,i)=>{const e=document.querySelector(s);e.scrollTop=45+15*i;return e.scrollTop;});});assert(scroll.every(n=>n>0));
 await page.evaluate(()=>fixture.release());await idle();
 const retained=await page.evaluate(()=>{fixture.keptInput.dispatchEvent(new Event('probe'));return {input:fixture.keptInput===document.querySelector('[data-five-fixture-field="design:spacing"]'),canvas:fixture.keptCanvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===fixture.keptInput,selection:[fixture.keptInput.selectionStart,fixture.keptInput.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-design-body'].map(s=>document.querySelector(s).scrollTop),listeners:fixture.probeCount};});
 assert.deepEqual(retained,{input:true,canvas:true,focus:true,selection:[0,1],scroll,listeners:1});
 await page.locator('[data-five-fixture-field="design:pattern"]').selectOption('Staggered');await page.locator('[data-five-fixture-field="design:postWidth"]').waitFor();
 await page.evaluate(()=>{const n=document.querySelector('[data-five-fixture-field="design:postWidth"]');n.focus({preventScroll:true});});
 await page.selectOption('[data-five-fixture-field="design:pattern"]','Grid');assert.equal(await page.locator('[data-five-fixture-field="design:postWidth"]').count(),0);
 assert.equal(await page.evaluate(()=>document.activeElement?.dataset.fiveFixtureField==='design:spacing'),false);
 cases.push('Delayed face readback preserves both sidebar scrolls, nested scroll, input identity/focus/selection/listener and canvas; conditional fields can disappear without stealing focus');
 await page.locator('[data-five-fixture-field="design:spacing"]').fill('100');
 await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('None');
 await page.locator('summary').filter({hasText:'连接与间隙'}).click();
 await page.locator('[data-five-fixture-field="design:jointMode"]').selectOption('Weld');
 const generationStarted=Date.now();await command('five.fixture.design.generate');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
 const state=await read(),candidate=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.candidate);
 assert(candidate.parts.length>1);assert.equal(candidate.supportCheck.status,'Supported');assert.equal(state.fixtures.length,0);
 for(const p of candidate.parts.filter(p=>p.role==='Support'))assert(p.contactPoints.length>0);
 assert.deepEqual(state.workpieces[0].geometryRef,before.workpieces[0].geometryRef);
 cases.push('Only Generate calculates plates; the automatic-plus-manual selection produces actual bearing contacts and a supported candidate without adopting it');
 await page.screenshot({path:resolve(output,'automatic-generated-preview.png')});
 writeFileSync(resolve(output,'fixture-automatic-faces.json'),JSON.stringify({passed:true,sourcePath,automaticMs,generationMs:Date.now()-generationStarted,automaticFaceCount:automatic.faces.length,partCount:candidate.parts.length,cases,retained,coverage:{native:true,browser:'Edge with production UI and native bridge',standaloneCef:false}},null,2));
}
