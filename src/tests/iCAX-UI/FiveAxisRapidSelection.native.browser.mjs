import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyRapidSelection({page,output,ids}){
 const frame=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
 const tab=async pageId=>{await page.locator(`[data-five-command="five.cam-page.${pageId}"]`).click();await idle();await frame();};
 const row=id=>page.locator(`[data-five-intent="${id}"]`);
 const state=()=>page.evaluate(()=>{const v=fixture.view,strokes=[];v.viewport.scene.traverse(o=>{if(o.userData.pathSelectionStroke)strokes.push({cut:o.parent.userData.cuttingLineId||o.parent.userData.generatedIntentId||o.parent.userData.compensatedIntentId,rapid:o.parent.userData.rapidTransitionId,color:o.material.uniforms.color.value.getHex(),dashed:o.material.uniforms.dashSize.value>0});});return {cuts:v.fiveAxisUI.selectedIntentIds,primary:v.fiveAxisUI.selectedIntentId,rapid:v.fiveRapid?.id||'',strokes};});
 await row(ids[0]).click();await row(ids[1]).click({modifiers:['Control']});
 const cutting=await state();assert.deepEqual(cutting.cuts.sort(),ids.slice(0,2).sort());
 await tab('rapid');let current=await state();assert.deepEqual(current.cuts,[]);assert.equal(current.primary,'');assert.ok(current.strokes.every(s=>s.rapid&&!s.cut&&s.color===0xe83eff&&s.dashed));
 const rapidIds=await page.locator('[data-five-rapid-id]').evaluateAll(es=>es.map(e=>e.dataset.fiveRapidId));assert.ok(rapidIds.length>=2);
 await page.locator(`[data-five-rapid-id="${rapidIds.at(-1)}"]`).click();await frame();
 await page.mouse.move(50,200);await frame();current=await state();assert.equal(current.rapid,rapidIds.at(-1));assert.equal(current.strokes.length,1);assert.equal(current.strokes[0].rapid,current.rapid);assert.equal(current.strokes[0].color,0xe83eff);assert.equal(current.strokes[0].dashed,true);
 await page.screenshot({path:resolve(output,'rapid-selected-purple.png')});
 await tab('cutting');current=await state();assert.deepEqual(current.cuts.sort(),cutting.cuts);assert.equal(current.primary,cutting.primary);assert.equal(current.rapid,'');assert.ok(current.strokes.length&&current.strokes.every(s=>s.cut&&!s.rapid));
 await page.screenshot({path:resolve(output,'cut-selection-restored.png')});
 await tab('rapid');current=await state();assert.equal(current.rapid,rapidIds.at(-1));assert.deepEqual(current.cuts,[]);assert.ok(current.strokes.every(s=>s.rapid===current.rapid));
 // Pick a different actual native travel segment in the viewport.
 const pixel=await page.evaluate(async previous=>{
  const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),{rapidTransitionAtPointer}=await import('/src/apps/five-axis-cam/webpage/rapidPlanning.mjs');
  const v=fixture.view,vp=v.viewport,box=vp.renderer.domElement.getBoundingClientRect(),geometry=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).generatedGeometry;vp.camera.updateMatrixWorld(true);
  for(const t of geometry.transitions){const id=`${t.from}/${t.to}`;if(id===previous||t.geometryStatus!=='Generated')continue;
   for(let i=1;i<t.points.length;i++){const q=new T.Vector3(...t.points[i-1]).lerp(new T.Vector3(...t.points[i]),.5).project(vp.camera),x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
    if(Math.abs(q.z)<=1&&document.elementFromPoint(x,y)===vp.renderer.domElement&&rapidTransitionAtPointer(v,{clientX:x,clientY:y})?.id===id)return {x,y,id};
   }
  }throw Error('No visible alternate native rapid segment');
 },current.rapid);
 await page.mouse.click(pixel.x,pixel.y);await page.mouse.move(50,200);await frame();current=await state();assert.equal(current.rapid,pixel.id);assert.deepEqual(current.cuts,[]);assert.equal(current.strokes[0].rapid,pixel.id);
 await tab('cutting');await tab('rapid');assert.equal((await state()).rapid,pixel.id);
 // An asynchronous state response must retain the latest editing/scroll state.
 await page.addStyleTag({content:'.selection-scroll-test .cam-context-pane::after,.selection-scroll-test .cam-info-pane::after{content:"";display:block;height:1500px}.selection-scroll-test .five-rapid-body{max-height:100px;overflow:auto}'});
 await page.evaluate(()=>{document.body.classList.add('selection-scroll-test');const e=document.querySelector('input[data-five-rapid-field]');if(!e)throw Error('Native rapid policy input missing');fixture.selectionInput=e;fixture.selectionCanvas=fixture.view.viewport.renderer.domElement;e.focus({preventScroll:true});e.setSelectionRange(0,1);fixture.holdMethod='CamScene.ReadState';fixture.hold=true;});
 const refreshing=page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
 await page.waitForFunction(()=>fixture.held);
 const positions=await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((s,i)=>{const e=document.querySelector(s);e.scrollTop=40+i*20;return e.scrollTop;}));
 await page.evaluate(()=>fixture.release());await refreshing;await idle();
 assert.equal(await page.evaluate(()=>fixture.selectionInput===document.activeElement&&fixture.selectionInput.isConnected&&fixture.selectionInput.selectionStart===0&&fixture.selectionInput.selectionEnd===1&&fixture.selectionCanvas===fixture.view.viewport.renderer.domElement),true);
 assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop)),positions);
 assert.deepEqual((await state()).cuts,[]);assert.equal((await state()).rapid,pixel.id);
 await page.evaluate(()=>document.body.classList.remove('selection-scroll-test'));
 await page.screenshot({path:resolve(output,'rapid-selection-restored.png')});
 return ['cutting and rapid tabs save and restore independent selections','inactive path type has no active selection or purple stroke','native rapid selection stays purple and four pixels wide after mouse leave','mouse clicks select the exact native travel segment','rapid policy focus, caret, canvas and both sidebar/nested scroll positions survive delayed refresh'];
}
