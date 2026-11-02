import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

export async function recheckFixturePartMode({page,rpc,output,errors}){
  const cases=[],read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveFixturePlate?.loading);
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();};
  const original=await read(),unit=original.fixtureUnits.find(u=>u.kind==='Generated');assert.ok(unit);
  await command('five.fixture.select:'+unit.id);
  const mode=page.locator('[data-five-key="fixture-parts-mode"]');
  assert.equal(await mode.innerText(),'编辑');
  const material=()=>page.evaluate(()=>Object.fromEntries([...fixture.view.viewport.sceneObjects].map(([id,obj])=>{const values=[];obj.traverse(o=>{if(o.userData?.meshFeatureEdges||o.userData?.independentInteractionVisual)return;for(const m of Array.isArray(o.material)?o.material:o.material?[o.material]:[])values.push(m.opacity);});return [id,values];})));
  const normal=await material();
  await mode.click();await idle();assert.equal(await mode.innerText(),'完成');
  const part=unit.design.assembly.parts.at(-1);
  await page.locator(`[data-five-command="five.fixture.part-select:${part.partId}"]`).click();await idle();
  assert.deepEqual(await page.evaluate(()=>[...fixture.view.viewport.emphasizedObjectIds]),[part.partId]);
  assert.notDeepEqual(await material(),normal);
  await mode.click();await idle();assert.equal(await mode.innerText(),'编辑');
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixtureSelectedPart),'');
  assert.equal(await page.locator('.five-fixture-parts tr[aria-selected="true"]').count(),0);
  assert.deepEqual(await material(),normal);
  await mode.click();await idle();await page.keyboard.press('Escape');await idle();
  assert.equal(await mode.innerText(),'编辑');assert.deepEqual(await material(),normal);
  cases.push('Header Edit/Done and Escape leave single-plate emphasis, clear its row and restore every original mesh opacity');

  // A real scene pointer outside edit mode must select the unit as a whole.
  const spot=await page.evaluate(()=>{
    const v=fixture.view.viewport;v.setViewDirection([1,-1,.8]);v.fitView();
    const rect=v.renderer.domElement.getBoundingClientRect(),ids=new Set(fixture.view.scene.domain.fixtures.map(p=>p.id));
    for(let y=.25;y<.9;y+=.015)for(let x=.15;x<.9;x+=.015){v.raycaster.setFromCamera({x:x*2-1,y:1-y*2},v.camera);const h=v.raycaster.intersectObjects([...v.sceneObjects.values()],true).find(h=>h.object.visible&&!h.object.userData?.meshFeatureEdges);if(ids.has(h?.object.userData?.objectId))return {id:h.object.userData.objectId,x:rect.left+x*rect.width,y:rect.top+y*rect.height};}return null;
  });assert.ok(spot);await page.mouse.click(spot.x,spot.y);await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixtureSelectedPart),'');
  await mode.click();await idle();await page.mouse.click(spot.x,spot.y);await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixtureSelectedPart),spot.id);
  cases.push('Actual raycast and pointer picking stays at unit scope after Done; explicit Edit restores list/scene plate synchronization');

  // Test the actual asynchronous native PartProfile callback against the same
  // panel patcher, including independently scrolled nested containers.
  await page.addStyleTag({content:'.cam-context-pane{height:180px!important;flex:none!important;overflow:auto!important}.cam-info-pane{height:260px!important;flex:none!important;overflow:auto!important}.five-preparation-tree{height:200px;overflow:auto}.five-preparation-workpiece{min-height:500px}[data-five-key="unit-generation-info"] .five-facts{max-height:100px;overflow:auto}'});
  await page.evaluate(()=>{fixture.holdMethod='CamFixture.PartProfile';fixture.hold=true;fixture.opening=fixture.entry.handleRibbonCommand(fixture.context,'five.fixture.part-edit:'+fixture.view.fiveFixtureSelectedPart);});
  await page.waitForFunction(()=>fixture.held);
  const expected=await page.evaluate(()=>{
    const q=s=>document.querySelector(s),input=q('[data-five-fixture-field="name"]');
    fixture.kept={input,canvas:fixture.view.viewport.renderer.domElement,left:q('.cam-context-pane'),right:q('.cam-info-pane'),leftNested:q('.five-preparation-tree'),rightNested:q('[data-five-key="unit-generation-info"] .five-facts')};
    fixture.probe=0;input.addEventListener('fixture-mode-probe',()=>++fixture.probe);input.focus({preventScroll:true});input.setSelectionRange(1,3,'backward');
    return ['left','right','leftNested','rightNested'].map((k,i)=>{fixture.kept[k].scrollTop=15+i*12;return fixture.kept[k].scrollTop;});
  });assert.ok(expected.every(x=>x>0));
  await page.evaluate(async()=>{fixture.release();await fixture.opening;});await idle();
  const retained=await page.evaluate(()=>{const k=fixture.kept;k.input.dispatchEvent(new Event('fixture-mode-probe'));return {input:k.input===document.querySelector('[data-five-fixture-field="name"]'),canvas:k.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===k.input,selection:[k.input.selectionStart,k.input.selectionEnd,k.input.selectionDirection],scroll:['left','right','leftNested','rightNested'].map(n=>k[n].scrollTop),listeners:fixture.probe};});
  assert.deepEqual(retained,{input:true,canvas:true,focus:true,selection:[1,3,'backward'],scroll:expected,listeners:1});
  await page.locator('[data-plate-canvas]').focus();await page.keyboard.press('Escape');
  assert.equal(await page.locator('[data-fixture-plate-editor]').count(),1);
  await page.locator('[data-plate-action="close"]').last().click();await idle();
  await command('five.fixture.parts-done');
  assert.equal(await page.locator('[data-fixture-plate-editor]').count(),0);assert.deepEqual(await material(),normal);
  cases.push('Native asynchronous profile read retains both panes, nested scrolling, focused input/caret, canvas and listeners; editor Escape retains its existing local meaning');
  const after=await read();assert.deepEqual(after.fixtures,original.fixtures);assert.deepEqual(after.fixtureUnits,original.fixtureUnits);
  assert.deepEqual(errors,[]);await page.screenshot({path:resolve(output,'part-edit-finished.png')});
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,retained,plateCount:unit.partIds.length,geometryUnchanged:true,coverage:{browser:'Edge',native:true,standaloneCef:false}},null,2));
}
