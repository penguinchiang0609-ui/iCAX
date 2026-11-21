import assert from 'node:assert/strict';
import {resolve} from 'node:path';

// Exercise the real product shell and viewport. Only this case's transport is
// extended: it can persist settings, but it must never generate a real path.
export async function verifyRapidGenerationParameters(page,output){
 const cases=[],originalViewport=page.viewportSize();
 await page.evaluate(()=>{
  const fixture=__fiveFixture,project=fixture.appProxy.getProduct().getProject(),proxy=project.getMainScene();
  const originalInvoke=proxy.invoke,originalHistory=proxy.getUndoRedoState;
  const probe=fixture.generationParameters={revision:1,configureCalls:[],settings:{strategy:'Quick',followHeightMm:1,liftHeightMm:10,normalRetractDistanceMm:2,sideRetractDistanceMm:2,safeZMm:50,collisionThresholdMm:.1,marginMm:.2,envelopeRadiusMm:0,envelopeLengthMm:0,linearSpeedMmS:100,linearAccelerationMmS2:1000,angularSpeedRadS:1,angularAccelerationRadS2:2}};
  proxy.invoke=async function(method,payload,options){
   if(method==='CamRapid.Configure'){
    fixture.requests.push({scope:'scene',method,payload:structuredClone(payload)});
    probe.configureCalls.push(structuredClone(payload));probe.settings=structuredClone(payload.settings);probe.revision++;
    return {};
   }
   if(/^(CamPlan\.Generate|CamRapid\.(Generate|Preview|Apply|EditPath|EditPolicy))$/.test(method))throw Error(`Generation-parameter UI must not calculate paths: ${method}`);
   const state=await originalInvoke.call(this,method,payload,options);
   if(method==='CamScene.ReadState'){
    const setup=state.setups.find(item=>item.id===state.activeSetupId);
    setup.rapidSettings=structuredClone(probe.settings);state.expected.mutationSequence=String(probe.revision);
   }
   return state;
  };
  proxy.getUndoRedoState=async()=>({revision:String(probe.revision),canUndo:probe.revision>1,canRedo:false});
  probe.restore=()=>{proxy.invoke=originalInvoke;if(originalHistory)proxy.getUndoRedoState=originalHistory;else delete proxy.getUndoRedoState;};
 });
 const settle=()=>page.waitForFunction(()=>{
  const view=__fiveFixture.view,s=view.fiveRapid;
  return !view.pending&&!s?.saveTimer&&!s?.savingPath&&!s?.settingsChanged&&!s?.fileSaving;
 });
 const dialog=page.locator('.five-overlay [data-five-floating="settings"][data-five-key="rapid-dialog"]');
 const field=name=>dialog.locator(`[data-five-rapid-field="${name}"]`);
 const open=async()=>{await page.locator('[data-command-id="five.tool.settings"]').click();await dialog.waitFor();};
 let stress;
 try{
  await page.evaluate(()=>__fiveFixture.populate(75));
  await page.locator('[data-tab-id="toolpath"]').click();
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.cam-page.rapid'));
  await page.locator('[data-five-rapid-id]').first().waitFor();
  const ribbon=await page.locator('.product-ribbon .ribbon-group').evaluateAll(groups=>groups.map(group=>({title:group.querySelector('.ribbon-group-title')?.textContent.trim(),ids:[...group.querySelectorAll('.ribbon-command-list [data-command-id]')].filter(node=>node.classList.contains('ribbon-command')).map(node=>node.dataset.commandId)})));
  const generation=ribbon.find(group=>group.title==='生成');
  assert.ok(generation,'the rapid ribbon has a generation group');
  assert.deepEqual(generation.ids,['five.tool.settings','five.tool.generate','five.rapid.safe-generate']);
  assert.ok(!ribbon.some(group=>group.title==='参数'),'the empty standalone parameter group is removed');
  assert.equal(await page.locator('.product-ribbon [data-command-id="five.tool.settings"]').count(),1);
  assert.equal(await page.locator('[data-command-id="five.tool.settings"]').innerText(),'生成参数');
  cases.push('Rapid generation group orders Generation Parameters, Generate All and Generate Single; no redundant parameter group');

  await open();
  assert.equal(await dialog.getAttribute('role'),'dialog');assert.equal(await dialog.getAttribute('aria-label'),'生成参数');
  assert.equal(await page.locator('.cam-info-pane [data-five-tool="settings"],.cam-info-pane [data-five-key="rapid-dialog"]').count(),0);
  assert.equal(await page.locator('.cam-info-pane [data-five-key="rapid-parameters"]').count(),1);
  assert.equal(await page.locator('[data-five-rapid-field="strategy"]').count(),1);
  for(const name of ['normalRetractDistanceMm','sideRetractDistanceMm','safeZMm'])assert.equal(await page.locator(`[data-five-rapid-field="${name}"]`).count(),1);
  cases.push('Generation Parameters is a unique floating dialog; the right inspector retains a separate read-only segment summary');

  const before=await dialog.boundingBox(),title=await dialog.locator('[data-five-dialog-drag]').boundingBox();
  await page.mouse.move(title.x+Math.min(80,title.width/3),title.y+title.height/2);await page.mouse.down();
  await page.mouse.move(title.x+Math.min(80,title.width/3)+120,title.y+title.height/2+65,{steps:6});await page.mouse.up();
  const after=await dialog.boundingBox();assert.ok(after.x-before.x>60&&after.y-before.y>20,JSON.stringify({before,after}));
  assert.ok(after.x>=0&&after.y>=0&&after.x+after.width<=page.viewportSize().width+1);
  cases.push('The actual shared floating-dialog drag handler moves Generation Parameters');

  await field('normalRetractDistanceMm').fill('123.456');await settle();
  assert.equal(await page.evaluate(()=>__fiveFixture.view.error||''),'');
  assert.equal(await page.evaluate(()=>__fiveFixture.generationParameters.settings.normalRetractDistanceMm),123.456);
  await field('strategy').selectOption('Frog');await settle();
  assert.equal(await field('normalRetractDistanceMm').count(),0);assert.equal(await field('followHeightMm').count(),1);assert.equal(await field('liftHeightMm').count(),1);
  await field('strategy').selectOption('Fly');await settle();
  assert.equal(await field('followHeightMm').count(),1);assert.equal(await field('liftHeightMm').count(),0);
  await field('strategy').selectOption('Quick');await settle();
  assert.equal(await field('normalRetractDistanceMm').inputValue(),'123.456');
  cases.push('Quick, Frog and Fly show only their applicable fields; controlled Configure/readback preserves drafts without generating paths');

  stress=await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:280px!important;display:block!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1000px}.five-path-list{height:130px!important;flex:none!important;overflow:auto!important}.cam-info-pane [data-five-key="rapid-parameters"]{height:110px!important;overflow:auto!important}.cam-info-pane [data-five-key="rapid-parameters"]::after{content:"";display:block;height:800px}.five-overlay [data-five-key="rapid-body"]{height:110px!important;max-height:110px!important;flex:none!important;overflow:auto!important}'});
  await field('normalRetractDistanceMm').evaluate(input=>{
   const selectors=['.cam-context-pane','.cam-info-pane','.five-path-list','.cam-info-pane [data-five-key="rapid-parameters"]','.five-overlay [data-five-key="rapid-body"]'];
   window.rapidParameterNodes={input,canvas:__fiveFixture.view.viewport.renderer.domElement,search:document.querySelector('[data-five-search]'),containers:selectors.map(selector=>document.querySelector(selector)),selectors,events:0};
   input.addEventListener('rapid-parameter-node-probe',()=>rapidParameterNodes.events++);
   __fiveFixture.hold();window.rapidParameterRefresh=__icaxAppShell.executeRibbonCommand('five.refresh');
  });
  await page.waitForFunction(()=>__fiveFixture.held);
  const scrolls=await page.evaluate(()=>{
   const nodes=rapidParameterNodes;nodes.input.focus({preventScroll:true});nodes.input.setSelectionRange(1,5);
   return nodes.containers.map((node,index)=>{node.scrollTop=65+index*19;return node.scrollTop;});
  });
  assert.ok(scrolls.every(value=>value>0),`all five real containers must scroll: ${scrolls}`);
  await page.evaluate(()=>__fiveFixture.release());await page.evaluate(()=>rapidParameterRefresh);await settle();
  const retained=await page.evaluate(()=>{
   const nodes=rapidParameterNodes,input=document.querySelector('.five-overlay [data-five-rapid-field="normalRetractDistanceMm"]');input.dispatchEvent(new Event('rapid-parameter-node-probe'));
   return {input:input===nodes.input,canvas:nodes.canvas===__fiveFixture.view.viewport.renderer.domElement,search:nodes.search===document.querySelector('[data-five-search]'),focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],value:input.value,events:nodes.events,scrolls:nodes.selectors.map(selector=>document.querySelector(selector).scrollTop)};
  });
  assert.deepEqual(retained,{input:true,canvas:true,search:true,focus:true,selection:[1,5],value:'123.456',events:1,scrolls});
  cases.push('Held real refresh preserves SDK canvas, input/search nodes and listeners, latest focus/selection, both panes and all nested scroll positions');

  await field('strategy').selectOption('Frog');await settle();
  await field('liftHeightMm').evaluate(input=>{window.removedGenerationField=input;window.retainedGenerationFollow=document.querySelector('.five-overlay [data-five-rapid-field="followHeightMm"]');input.focus({preventScroll:true});input.setSelectionRange(0,1);__fiveFixture.hold();window.conditionalParameterRefresh=__icaxAppShell.executeRibbonCommand('five.refresh');});
  await page.waitForFunction(()=>__fiveFixture.held);
  await page.evaluate(()=>{__fiveFixture.view.fiveRapid.values.strategy='Fly';__fiveFixture.generationParameters.settings.strategy='Fly';__fiveFixture.release();});
  await page.evaluate(()=>conditionalParameterRefresh);await settle();
  assert.equal(await field('liftHeightMm').count(),0);
  assert.deepEqual(await page.evaluate(()=>({removed:!removedGenerationField.isConnected,followSame:retainedGenerationFollow===document.querySelector('.five-overlay [data-five-rapid-field="followHeightMm"]'),focusNotMisassigned:document.activeElement!==retainedGenerationFollow})),{removed:true,followSame:true,focusNotMisassigned:true});
  cases.push('A conditionally removed field is not restored onto another field after an asynchronous refresh');

  await stress.evaluate(node=>node.remove());stress=null;
  await page.screenshot({path:resolve(output,'rapid-generation-parameters.png')});
  await dialog.locator('[data-five-command="five.rapid.close"]').click();assert.equal(await dialog.count(),0);
  await open();await field('strategy').press('Escape');await page.waitForFunction(()=>!document.querySelector('[data-five-floating="settings"]'));
  assert.equal(await page.locator('.five-overlay [data-five-key="rapid-dialog"]').count(),0);
  assert.equal(await page.locator('.cam-info-pane [data-five-key="rapid-dialog"]').getAttribute('aria-label'),'空移属性');
  assert.equal(await page.evaluate(()=>__fiveFixture.requests.filter(request=>/^(CamPlan\.Generate|CamRapid\.(Generate|Preview|Apply|EditPath|EditPolicy))$/.test(request.method)).length),0);
  assert.equal(await page.evaluate(()=>__fiveFixture.view.error||''),'');
  cases.push('Close and Escape return to the current segment inspector; changing generation parameters makes no path-generation calls');
  return cases;
 }finally{
  if(stress)await stress.evaluate(node=>node.remove());
  await page.evaluate(()=>{__fiveFixture.release();__fiveFixture.generationParameters?.restore();});
  if(originalViewport)await page.setViewportSize(originalViewport);
 }
}
