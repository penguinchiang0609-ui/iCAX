import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { verifyCutStarting } from './FiveAxisCutStarting.native.browser.mjs';

// Invoked after recognition creates a real persisted definition in the native bridge.
export async function verifyCutEditing({page,rpc,calls,output,intentId}) {
  const invoke=(method,payload)=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=async()=>{
    const state=await invoke('CamScene.ReadState',{});
    return (await invoke('CamCut.Read',{expected:state.expected,intentIds:[intentId]})).items[0];
  };
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await page.waitForFunction(()=>!fixture.view.pending);};
  const input=name=>page.locator(`[data-five-field="${name}"]`);
  const button=name=>page.locator(`[data-five-command="five.cut.${name}"]`);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const original=await read();
  const starting=await verifyCutStarting({page,rpc,calls,output,intentId});
  const started=await read();
  await page.evaluate(()=>{fixture.loseMethod='CamCut.Reverse';});
  const reversals=calls.filter(c=>c.method==='CamCut.Reverse').length;
  await command('five.tool.reverse');await idle();
  const reversed=await read();assert.equal(reversed.reversed,!started.reversed);
  assert.deepEqual(reversed.start,started.start);assert.equal(reversed.retainedSide,started.retainedSide);
  assert.equal(calls.filter(c=>c.method==='CamCut.Reverse').length,reversals+1,'lost response must not reverse twice');
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await rpc({action:'undo'});await command('five.refresh');
  assert.equal((await read()).reversed,started.reversed);
  await rpc({action:'redo'});await command('five.refresh');
  assert.equal((await read()).reversed,reversed.reversed);
  await page.screenshot({path:resolve(output,'cut-reverse.png')});

  await command('five.tool.side');await input('retainedSide').selectOption('Interior');
  const style=await page.addStyleTag({content:'.five-tree-footer{padding-bottom:600px!important}[data-five-key="draft-note-editor"]{height:100px;overflow:auto}[data-five-key="row-note"]{min-height:240px}.five-pane-heading{padding-bottom:200px!important}'});
  await page.evaluate(()=>{
    window.cutPreserved={canvas:document.querySelector('canvas'),input:document.querySelector('[data-five-field="note"]'),nested:document.querySelector('[data-five-key="draft-note-editor"]'),events:0};
    cutPreserved.input.addEventListener('input',()=>++cutPreserved.events);
  });
  await page.evaluate(()=>{fixture.holdMethod='CamCut.SetRetainedSide';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.cut.apply');});
  await page.waitForFunction(()=>fixture.held);await input('retainedSide').selectOption('Exterior');
  await input('note').fill('保留正在输入的备注');
  const scroll=await page.evaluate(()=>{
    cutPreserved.input.focus({preventScroll:true});cutPreserved.input.setSelectionRange(1,4);
    const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),cutPreserved.nested];
    nodes.forEach((node,i)=>{node.scrollTop=60+i*10;});return nodes.map(node=>node.scrollTop);
  });
  assert.ok(scroll.every(n=>n>0));
  await page.evaluate(()=>fixture.release());await idle();
  const interaction=await page.evaluate(()=>({canvas:cutPreserved.canvas===document.querySelector('canvas'),input:cutPreserved.input===document.querySelector('[data-five-field="note"]'),nested:cutPreserved.nested===document.querySelector('[data-five-key="draft-note-editor"]'),focused:document.activeElement===cutPreserved.input,selection:[cutPreserved.input.selectionStart,cutPreserved.input.selectionEnd],value:cutPreserved.input.value,events:cutPreserved.events,scroll:[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),cutPreserved.nested].map(node=>node.scrollTop)}));
  for(const k of ['canvas','input','nested','focused'])assert.equal(interaction[k],true,k);
  assert.deepEqual(interaction.selection,[1,4]);assert.deepEqual(interaction.scroll,scroll);assert.equal(interaction.value,'保留正在输入的备注');assert.ok(interaction.events>0);
  await style.evaluate(node=>node.remove());
  assert.equal((await read()).retainedSide,'Interior');assert.equal(await input('retainedSide').inputValue(),'Exterior','later side selection remains a draft');
  // Read refresh keeps an unsaved side decision; the source geometry remains untouched.
  await button('load').click();await idle();assert.equal(await input('retainedSide').inputValue(),'Exterior');
  await input('retainedSide').selectOption('Interior');await button('apply').click();await idle();
  const savedDefinition=await read();assert.deepEqual(savedDefinition.start,started.start);
  assert.deepEqual(savedDefinition.sourceSpans,original.sourceSpans);assert.equal(savedDefinition.retainedSide,'Interior');
  await page.screenshot({path:resolve(output,'cut-retained-side.png')});

  await page.evaluate(()=>{fixture.holdMethod='CamCut.Read';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.cut.load');});
  await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
  await page.locator('[data-five-search]').fill('当前页面');
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.activeAreaId),'help');
  assert.equal(await page.locator('[data-five-search]').inputValue(),'当前页面');
  assert.equal(await page.evaluate(()=>document.activeElement?.hasAttribute('data-five-search')),true);
  await page.locator('[data-five-search]').fill('');
  await command('five.tool.side');assert.equal(await page.evaluate(()=>fixture.view.error),'');
  return {passed:true,savedDefinition,starting,interaction,cases:[
    'save preserves canvas/input/listeners/focus/selection/latest dual and nested scrolling',
    'reverse keeps physical start and retained side', 'lost reverse receipt does not toggle twice', 'undo/redo refreshes current direction',
    'retained side saves independently and preserves newer selection', 'cross-page read response preserves current focus']};
}
