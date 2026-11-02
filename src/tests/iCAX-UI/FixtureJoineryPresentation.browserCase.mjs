import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

export async function recheckJoineryPresentation({page,output,errors}){
  const cases=[],idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,null,{timeout:600000});
  const field=key=>page.locator('[data-five-fixture-field="design:'+key+'"]');
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));await idle();
  const initial=await page.evaluate(()=>{const v=fixture.view,d=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).fixtureDesign;return {mode:d.recipe.jointMode,pattern:d.recipe.pattern,joinery:d.candidate.joinery,body:document.body.innerText};});
  assert.equal(initial.mode,'TabSlot');
  if(initial.pattern==='Staggered'){assert.equal(initial.joinery.crossLaps,0);assert.ok(initial.body.includes('支撑板向下插入对应底座榫孔'));assert.ok(!initial.body.includes('先装上开槽 Y 板'));}
  else {assert.equal(initial.pattern,'Grid');assert.ok(initial.joinery.crossLaps>0);assert.ok(initial.body.includes('先装上开槽 Y 板'));}
  await page.setViewportSize({width:1440,height:520});await field('tenonWidth').scrollIntoViewIfNeeded();
  await page.evaluate(()=>{const input=document.querySelector('[data-five-fixture-field="design:tenonWidth"]'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),nested=[...left.querySelectorAll('*'),...right.querySelectorAll('*')].filter(e=>e.scrollHeight>e.clientHeight+5&&/auto|scroll/.test(getComputedStyle(e).overflowY));window.keptJoint={input,left,right,nested,canvas:document.querySelector('canvas'),calls:0};input.focus({preventScroll:true});input.setSelectionRange(0,1);input.addEventListener('retention-probe',()=>keptJoint.calls++);fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');});
  await page.waitForFunction(()=>fixture.held);await page.evaluate(()=>{const k=keptJoint;k.input.value='8.1';k.input.dispatchEvent(new Event('input',{bubbles:true}));k.input.setSelectionRange(0,1);k.left.scrollTop+=12;k.right.scrollTop+=27;k.nested.forEach(e=>e.scrollTop+=31);k.scroll=[k.left.scrollTop,k.right.scrollTop,...k.nested.map(e=>e.scrollTop)];fixture.release();});await idle();
  const retained=await page.evaluate(()=>{const k=keptJoint;k.input.dispatchEvent(new Event('retention-probe'));return {node:k.input===document.querySelector('[data-five-fixture-field="design:tenonWidth"]'),canvas:k.canvas===document.querySelector('canvas'),focus:document.activeElement===k.input,selection:[k.input.selectionStart,k.input.selectionEnd],value:k.input.value,calls:k.calls,nestedCount:k.nested.length,nestedAttached:k.nested.every(e=>e.isConnected),scroll:[k.left.scrollTop,k.right.scrollTop,...k.nested.map(e=>e.scrollTop)],expected:k.scroll};});
  assert.equal(retained.node,true);assert.equal(retained.canvas,true);assert.equal(retained.focus,true);assert.equal(retained.value,'8.1');assert.deepEqual(retained.selection,[0,1]);assert.deepEqual(retained.scroll,retained.expected);assert.equal(retained.calls,1);assert.ok(retained.nestedCount>0);assert.equal(retained.nestedAttached,true);
  cases.push('saved joinery UI refresh preserves newest tenon width, focus, selection, input node, listener, canvas, both panes and real nested fixture-list scrolling');
  await field('jointMode').selectOption('Weld');assert.equal(await field('tenonWidth').count(),0);await field('jointMode').selectOption('TabSlot');assert.equal(await field('tenonWidth').inputValue(),'8.1');
  await page.locator('[data-five-command="five.fixture.design.reset"]').click();await idle();assert.equal(await field('tenonWidth').inputValue(),'8');
  await page.setViewportSize({width:1440,height:1000});await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.7]);fixture.view.viewport.fitView();});await field('jointMode').scrollIntoViewIfNeeded();await page.screenshot({path:resolve(output,'joinery-controls.png')});assert.deepEqual(errors,[]);
  cases.push('connection-mode conditional fields preserve drafts, reset restores saved fit values, and saved supports show their applicable insertion instructions');
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,retained,joinery:initial.joinery,coverage:{browser:'Edge Chromium',native:'production SDO and repository',standaloneCef:false}},null,2));console.log(JSON.stringify({passed:true,cases}));
}
