import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

export async function recheckSupportPreview({page,rpc,output,errors}){
  const cases=[],idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,null,{timeout:600000});
  const click=async name=>{await page.locator('[data-five-command="five.fixture.design.'+name+'"]').click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const field=key=>page.locator('[data-five-fixture-field="design:'+key+'"]');
  const visibility=()=>page.evaluate(()=>{const v=fixture.view,d=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).fixtureDesign;return {preview:v.fixtureCandidatePreview?.group.children.length||0,prior:d.assembly.parts.map(p=>v.viewport.sceneObjects.get(p.partId)?.visible)};});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));await idle();
  let shown=await visibility();assert.ok(shown.prior.length>=7);assert.ok(shown.prior.every(Boolean));
  await field('layout').selectOption('Manual');await field('positions').fill('0');await click('generate');
  shown=await visibility();assert.equal(shown.preview,2);assert.ok(shown.prior.every(v=>v===false));
  const capCoverage=await page.evaluate(()=>{
    const v=fixture.view,parts=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).fixtureDesign.candidate.parts;
    const area=loop=>Math.abs(loop.reduce((n,p,i)=>{const q=loop[(i+1)%loop.length];return n+p[0]*q[1]-p[1]*q[0];},0))/2;
    return parts.map((p,index)=>{const positions=v.fixtureCandidatePreview.group.children[index].geometry.getAttribute('position');let actual=0;
      for(let i=0;i<positions.count;i+=3){if([0,1,2].some(k=>Math.abs(positions.getZ(i+k))>1e-5))continue;actual+=Math.abs((positions.getX(i+1)-positions.getX(i))*(positions.getY(i+2)-positions.getY(i))-(positions.getY(i+1)-positions.getY(i))*(positions.getX(i+2)-positions.getX(i)))/2;}
      return {part:p.number,actual,expected:area(p.outline)-p.holes.reduce((n,h)=>n+area(h),0)};});
  });for(const p of capCoverage)assert.ok(Math.abs(p.actual-p.expected)<.05,JSON.stringify(p));
  const assessment=await page.evaluate(()=>fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).fixtureDesign.candidate.supportCheck.status);assert.equal(assessment,'Insufficient');
  cases.push('manual candidate is shown alone while the previous adopted grid assembly is temporarily hidden; native fixtures remain intact');
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));await idle();shown=await visibility();assert.equal(shown.preview,2);assert.ok(shown.prior.every(v=>v===false));
  await field('positions').fill('100000');shown=await visibility();assert.equal(shown.preview,0);assert.ok(shown.prior.every(Boolean));
  await click('reset');shown=await visibility();assert.equal(shown.preview,2);assert.ok(shown.prior.every(v=>v===false));
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.select'));await idle();shown=await visibility();assert.equal(shown.preview,0);assert.ok(shown.prior.every(Boolean));
  cases.push('scene refresh reapplies preview isolation; editing or leaving the tool restores the adopted assembly without changing stored visibility');
  const saved=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});assert.equal(saved.fixtures.filter(f=>f.setupId===saved.activeSetupId).length,shown.prior.length);assert.ok(saved.fixtures.every(f=>f.visible));assert.deepEqual(errors,[]);
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.7]);fixture.view.viewport.fitView();});await page.screenshot({path:resolve(output,'restored-grid.png')});
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,capCoverage,parts:shown.prior.length,coverage:{browser:'Edge Chromium',native:'production SDO and repository',standaloneCef:false}},null,2));console.log(JSON.stringify({passed:true,cases}));
}

export async function recheckSupportInputs({page,rpc,output,errors}){
  const cases=[],idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,null,{timeout:600000});
  const field=key=>page.locator('[data-five-fixture-field="design:'+key+'"]');
  const fill=(key,value)=>field(key).fill(String(value)),select=(key,value)=>field(key).selectOption(value);
  const click=async name=>{await page.locator('[data-five-command="five.fixture.design.'+name+'"]').click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const design=()=>page.evaluate(()=>fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).fixtureDesign);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces.length),4);
  await select('sizing','Manual');await select('baseShape','Rectangle');await fill('areaDepth',260);await fill('spacing',40);
  await select('pattern','Staggered');await fill('postWidth',14);await select('layout','Manual');await fill('positions','0');
  await click('generate');assert.equal((await design()).candidate.supportCheck.status,'Insufficient');
  await select('layout','Automatic');assert.equal(await field('spacing').inputValue(),'40');assert.equal(await field('postWidth').inputValue(),'14');assert.equal(await field('areaDepth').inputValue(),'260');
  cases.push('successful manual generation preserves hidden automatic spacing and post width; saved-recipe refresh does not replace the draft');
  await fill('areaDepth','');await select('baseShape','Circle');assert.equal(await field('areaDepth').count(),0);
  await page.setViewportSize({width:1440,height:520});await field('baseDrop').scrollIntoViewIfNeeded();
  await page.evaluate(()=>{
    const input=document.querySelector('[data-five-fixture-field="design:baseDrop"]'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
    input.focus({preventScroll:true});input.setSelectionRange(0,1);window.kept={input,left,right,canvas:document.querySelector('canvas'),calls:0};input.addEventListener('retention-probe',()=>kept.calls++);
    fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
  });await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>{kept.input.value='42';kept.input.dispatchEvent(new Event('input',{bubbles:true}));kept.input.setSelectionRange(0,1);kept.left.scrollTop+=15;kept.right.scrollTop+=30;kept.scroll=[kept.left.scrollTop,kept.right.scrollTop];fixture.release();});await idle();
  const retention=await page.evaluate(()=>{kept.input.dispatchEvent(new Event('retention-probe'));return {node:kept.input===document.querySelector('[data-five-fixture-field="design:baseDrop"]'),canvas:kept.canvas===document.querySelector('canvas'),focus:document.activeElement===kept.input,value:kept.input.value,selection:[kept.input.selectionStart,kept.input.selectionEnd],scroll:[kept.left.scrollTop,kept.right.scrollTop],expected:kept.scroll,calls:kept.calls};});
  assert.equal(retention.node,true);assert.equal(retention.canvas,true);assert.equal(retention.focus,true);assert.equal(retention.value,'42');assert.deepEqual(retention.selection,[0,1]);assert.deepEqual(retention.scroll,retention.expected);assert.equal(retention.calls,1);
  cases.push('asynchronous native refresh preserves latest input, focus, selection, both scroll positions, canvas, input node and listener');
  await page.setViewportSize({width:1440,height:1000});await click('generate');let d=await design();
  assert.equal(d.recipe.spacing,40);assert.equal(d.recipe.postWidth,14);assert.equal(d.recipe.areaDepth,0);assert.equal(d.candidate.supportCheck.status,'Supported');
  await select('baseShape','Rectangle');assert.equal(await field('areaDepth').inputValue(),'');await fill('areaDepth',260);await select('baseShape','Circle');await click('generate');d=await design();assert.equal(d.recipe.areaDepth,260);
  await select('baseShape','Rectangle');assert.equal(await field('areaDepth').inputValue(),'260');await select('baseShape','Circle');
  assert.equal(await page.locator('[data-five-command="five.fixture.design.confirm"]').isDisabled(),true);await click('reset');
  cases.push('inactive blank rectangle depth does not block circular generation and its draft is restored when visible; valid hidden depth persists through generation');
  await click('confirm');await click('breakdown');await click('export');d=await design();assert.ok(d.delivery.directory);
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.7]);fixture.view.viewport.fitView();document.querySelector('.cam-info-pane').scrollTop=0;});await page.screenshot({path:resolve(output,'support-inputs.png')});
  const before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtures,before.fixtures);assert.deepEqual(reopened.setups,before.setups);assert.deepEqual(errors,[]);
  await rpc({action:'export-project',path:resolve(output,'staggered-support.i5cam')});
  cases.push('updated packaged UI supports adoption, breakdown, export and exact native save/reopen');
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,retention,parts:d.assembly.parts.length,exportDirectory:d.delivery.directory,coverage:{browser:'Edge Chromium',native:'production SDO and repository',standaloneCef:false}},null,2));console.log(JSON.stringify({passed:true,cases,parts:d.assembly.parts.length}));
}
