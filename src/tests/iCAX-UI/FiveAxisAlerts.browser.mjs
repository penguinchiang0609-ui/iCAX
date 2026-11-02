import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve,sep,extname} from 'node:path';
import {tmpdir} from 'node:os';
import {createRequire} from 'node:module';
import {fileURLToPath,pathToFileURL} from 'node:url';

const moduleName=process.env.ICAX_PLAYWRIGHT_MODULE||'playwright';
const {chromium}=moduleName.startsWith('file:')||moduleName.endsWith('.mjs')
  ?await import(/^[A-Za-z]:[\\/]/.test(moduleName)?pathToFileURL(moduleName).href:moduleName)
  :createRequire(import.meta.url)(moduleName);
const root=resolve(fileURLToPath(new URL('../../..',import.meta.url)));
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||resolve(tmpdir(),'icax-five-axis-collision-dialog-browser'));
mkdirSync(output,{recursive:true});
const browser=await chromium.launch({channel:'msedge',headless:true});
const results=[],errors=[];
let page;
try{
  page=await browser.newPage({viewport:{width:1600,height:1050}});
  page.on('pageerror',error=>errors.push(error.stack||error.message));
  await page.route('http://five-alerts.test/**',route=>{
    const url=new URL(route.request().url());
    if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><html><head><meta charset="utf-8"></head><body></body></html>'});
    const path=resolve(root,decodeURIComponent(url.pathname).slice(1));
    if(!path.startsWith(root+sep))return route.abort();
    const contentType=extname(path)==='.css'?'text/css':'text/javascript';
    return route.fulfill({contentType,body:readFileSync(path)});
  });
  await page.goto('http://five-alerts.test/');
  await page.evaluate(async()=>{
    const base='/src/apps/five-axis-cam/webpage/';
    const [{renderCollisionCheck,simulationCommand,simulationField},{patchWorkspacePanels},{syncFloatingDialogs,disposeFloatingDialogs},{installCollisionProgressStyle},{installCollisionDiagnosticsStyle},{buildPlayback},{selectTool},THREE]=await Promise.all([
      import(base+'trajectorySimulation.mjs'),import(base+'workspaceDom.mjs'),import(base+'floatingDialogs.mjs'),import(base+'collisionProgress.mjs'),
      import(base+'collisionDiagnostics.mjs'),import(base+'trajectoryPlayback.mjs'),import(base+'workspaceModel.mjs'),import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js')]);
    const stylesheet=document.createElement('link');stylesheet.rel='stylesheet';stylesheet.href=base+'workspace.css';document.head.append(stylesheet);
    const style=document.createElement('style');style.textContent=`
      *{box-sizing:border-box}body{margin:0;font:13px system-ui;background:#e8edf0}button,input{font:inherit}
      .product-ribbon{height:120px;display:flex;justify-content:flex-end;align-items:flex-start;padding:12px 22px;background:#edf2f4}
      [data-open-collision]{height:32px}.five-workspace{height:calc(100vh - 120px);display:grid;grid-template-columns:220px 1fr 250px;position:relative;overflow:hidden}
      .cam-context-pane,.cam-info-pane{height:100%;min-height:0;overflow:auto;background:#f6f8fa;padding:12px}
      .pane-spacer{height:780px;flex-shrink:0}.nested-pane{height:180px;overflow:auto;border:1px solid #aab8bf}.nested-content{height:650px}
      .cam-render-viewport{position:relative;min-width:0;min-height:0}.cam-render-viewport canvas{width:100%;height:100%;display:block;background:#cbd8df}
      [data-five-overlay]{position:absolute;inset:0;pointer-events:none}
      .five-collision-issue-list{display:flex;flex-direction:column}.five-collision-issue{text-align:left;position:relative}.five-collision-issue>span,.five-collision-issue>small{display:block}
      .five-collision-locate{position:absolute;right:7px;top:12px}.five-collision-diagnostic-actions{display:flex;gap:7px}
    `;document.head.append(style);
    document.body.innerHTML='<header class="product-ribbon"><button data-open-collision>碰撞检测</button></header><main id="mount" class="five-workspace"><aside class="cam-context-pane"></aside><section class="cam-render-viewport"><canvas tabindex="0" aria-label="三维场景"></canvas><div data-five-overlay></div></section><aside class="cam-info-pane"></aside><div data-five-dock></div></main>';
    installCollisionProgressStyle(document);installCollisionDiagnosticsStyle(document);
    await Promise.all([...document.querySelectorAll('link[rel="stylesheet"]')].map(link=>link.sheet?null:new Promise((resolve,reject)=>{link.addEventListener('load',resolve,{once:true});link.addEventListener('error',reject,{once:true});})));
    const points=Array.from({length:20},(_,i)=>[i,0,0]),directions=points.map(()=>[0,0,1]);
    const setup={id:'setup',rapidSettings:{followHeightMm:1,collisionThresholdMm:.1},headModel:{kind:'Builtin'},generatedGeometry:{status:'Current',operations:[
      {intentId:'cut-a',placementId:'placement',order:1,parts:[{role:'Cut',points,directions}]},
      {intentId:'cut-b',placementId:'placement',order:2,parts:[{role:'Cut',points:points.map(p=>[p[0]+30,0,0]),directions}]}],
      transitions:[{from:'cut-a',to:'cut-b',geometryStatus:'Generated',frame:'Setup',referencePoint:'ToolTip',points:[[19,0,1],[30,0,1]],directions:[[0,0,1],[0,0,1]],policy:{marginMm:.2}}]}};
    const placement={id:'placement',workpieceId:'workpiece',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]};
    const frames=buildPlayback(setup,[placement]).frames;
    const report={validity:'Current',scope:'BuiltinSchematicWorkpieceAndFixtures',representation:'BoxCompoundMesh',headBoxCount:'1',
      headBoxes:[{index:'0',center:[0,0,2],halfExtents:[1,1,2],axes:[[1,0,0],[0,1,0],[0,0,1]],source:'Head'}],
      status:'Collision',collisionCount:String(frames.length-1),uncheckedCount:'0',
      frames:frames.map(f=>({position:f.position.toArray(),rotation:f.rotation.toArray()})),
      segments:frames.slice(0,-1).map((f,i)=>({index:String(i),status:'Collision',reason:'',obstacleId:`fixture-${i}`,fraction:.25,
        witness:{kind:'NearestPoints',frame:'Setup',headBoxIndex:'0',headPoint:[i,0,2],obstaclePoint:[i,0,1.95],separationMm:.05,surfaceDistanceMm:.05,clearanceMm:.1,containment:false}}))};
    setup.simulationCheck=report;
    const mount=document.querySelector('#mount'),canvas=mount.querySelector('canvas');
    const view={fiveAxisArea:'toolpath',fiveCollisionOpen:true,fiveDialogPositions:{},
      scene:{intentToolpaths:[{id:'cut-a',name:'切割1'},{id:'cut-b',name:'切割2'}],domain:{expected:{epoch:'fixture',mutationSequence:'1'},activeSetupId:'setup',setups:[setup],placements:[placement],workpieces:[{id:'workpiece',name:'试验件'}],fixtureUnits:[],fixtures:report.segments.map((s,i)=>({id:s.obstacleId,name:`支撑板 ${i+1}`}))}},
      viewport:{scene:new THREE.Scene(),sceneObjects:new Map(),renderer:{domElement:canvas},perspectiveFov:45,
        getCameraState(){return this.camera??{radius:500,target:{x:0,y:0,z:0}};},setCameraState(value){this.camera=value;}}};
    selectTool(view,'collision');
    const context={mount,isCurrentProject:()=>true},binding={context,view};
    const fixture={view,context,binding,report,selection:'a',removeField:false,pending:false,canvasCalls:0,inputCalls:0,eventErrors:[]};
    const left=()=>'<h3>刀路</h3><div class="nested-pane" data-five-key="left-nested"><div class="nested-content">工序树</div></div><div class="pane-spacer"></div>';
    const right=()=>`<section data-five-key="inspector-${fixture.selection}" data-pane-interaction-scope="cut:${fixture.selection}"><label>名称<input type="text" data-five-key="inspector-name" aria-label="所选刀路名称" value="原始名称"></label>${fixture.removeField?'':'<label>备注<input type="text" data-five-key="optional-note" aria-label="条件备注" value="备注"></label>'}</section><div class="nested-pane" data-five-key="right-nested"><div class="nested-content">参数列表</div></div><div class="pane-spacer"></div>`;
    fixture.render=()=>{
      if(!patchWorkspacePanels(mount,[['.cam-context-pane',left()],['.cam-info-pane',right()],['[data-five-overlay]',renderCollisionCheck(view)]]))throw Error('Fixture patch selectors missing');
      syncFloatingDialogs(mount,binding);
    };
    const ops={renderProject:fixture.render};
    mount.addEventListener('input',event=>{const name=event.target.dataset.fiveSimulationField;if(name)simulationField(context,view,name,event.target.value);});
    mount.addEventListener('click',event=>{
      const command=event.target.closest('[data-five-command]')?.dataset.fiveCommand;if(!command)return;
      if(command.startsWith('five.simulation.'))void simulationCommand(context,view,command.slice(16),ops).catch(error=>fixture.eventErrors.push(error.message));
    });
    document.querySelector('[data-open-collision]').addEventListener('click',()=>{view.fiveCollisionOpen=true;fixture.render();});
    canvas.addEventListener('pointerdown',()=>fixture.canvasCalls++);
    fixture.hold=()=>{fixture.pending=true;fixture.awaited=new Promise(resolve=>fixture.release=resolve).then(()=>{view.error='检测报告已更新';fixture.render();fixture.pending=false;});};
    fixture.dispose=()=>disposeFloatingDialogs(binding);
    fixture.render();window.__alerts=fixture;
  });
  const panel=page.locator('[data-five-collision-dialog]'),list=page.locator('.five-collision-issue-list');
  await panel.waitFor();await page.waitForFunction(()=>getComputedStyle(document.querySelector('[data-five-collision-dialog]')).position==='fixed');
  const geometryOf=async()=>page.evaluate(()=>{
    const panel=document.querySelector('[data-five-collision-dialog]'),p=panel.getBoundingClientRect(),origin=document.querySelector('[data-five-overlay]').getBoundingClientRect(),style=getComputedStyle(panel);
    return {x:p.x,y:p.y,width:p.width,height:p.height,right:p.right,bottom:p.bottom,clientWidth:innerWidth,clientHeight:innerHeight,expectedLeft:origin.left+16,expectedTop:origin.top+64,position:style.position,background:style.backgroundColor,modal:panel.getAttribute('aria-modal'),floating:panel.dataset.fiveFloating};
  });
  let geometry=await geometryOf();assert.equal(geometry.width,900);assert.equal(geometry.position,'fixed');assert.ok(Math.abs(geometry.y-geometry.expectedTop)<1&&Math.abs(geometry.x-geometry.expectedLeft)<1,JSON.stringify(geometry));
  assert.notEqual(geometry.modal,'true');assert.equal(geometry.floating,'collision-check');assert.match(geometry.background,/rgba\([^)]*,\s*0\./);
  assert.equal(await page.locator('[data-five-alert-panel]').count(),0);assert.equal(await panel.getAttribute('aria-label'),'碰撞检测');
  results.push('collision checking is an independent translucent nonmodal floating window, separate from the generic alert summary');
  const drag=async(left,top)=>{
    const rect=await panel.boundingBox();
    await page.mouse.move(rect.x+100,rect.y+18);await page.mouse.down();await page.mouse.move(left+100,top+18,{steps:12});await page.mouse.up();
  };
  const initial={left:geometry.x,top:geometry.y};await drag(30,260);geometry=await geometryOf();
  assert.ok(Math.abs(geometry.x-30)<1&&Math.abs(geometry.y-260)<1,JSON.stringify(geometry));
  assert.ok(geometry.x<await page.locator('.cam-render-viewport').evaluate(n=>n.getBoundingClientRect().left));
  await drag(680,210);geometry=await geometryOf();assert.ok(Math.abs(geometry.x-680)<1&&Math.abs(geometry.y-210)<1,JSON.stringify(geometry));
  assert.ok(geometry.right>await page.locator('.cam-info-pane').evaluate(n=>n.getBoundingClientRect().left));
  await drag(initial.left,initial.top);
  results.push('real pointer dragging crosses both side panes in application client coordinates without a viewport clamp');
  const count=await page.evaluate(()=>__alerts.report.segments.length);assert.ok(count>16);assert.equal(await list.locator('button').count(),count);
  const bounds=await list.boundingBox();await page.mouse.move(bounds.x+bounds.width/2,bounds.y+bounds.height/2);await page.mouse.wheel(0,10000);
  await page.waitForFunction(()=>{const n=document.querySelector('.five-collision-issue-list');return n.scrollTop+n.clientHeight>=n.scrollHeight-2;});
  const last=list.locator('button').last();await last.click();await page.waitForFunction(index=>Math.floor(__alerts.view.fiveSimulation.cursor)===index,count-1);
  assert.equal(await last.getAttribute('aria-pressed'),'true');assert.equal(await page.locator(`[data-collision-segment="${count-1}"]`).count(),1);
  results.push(`all ${count} issue groups are rendered; real wheel scrolling reaches the final row and clicking it updates the actual collision selection`);
  const field=page.getByRole('textbox',{name:'所选刀路名称'});await field.fill('异步更新前继续编辑');
  await page.evaluate(()=>{
    const f=__alerts,n=document.querySelector('[aria-label="所选刀路名称"]');n.focus({preventScroll:true});n.setSelectionRange(1,4,'backward');
    f.saved={input:n,canvas:document.querySelector('canvas'),panel:document.querySelector('[data-five-collision-dialog]'),list:document.querySelector('.five-collision-issue-list')};
    n.addEventListener('input',()=>f.inputCalls++);f.hold();
  });
  await page.keyboard.type('较晚输入');
  const scrolls=await page.evaluate(()=>{
    const input=__alerts.saved.input;input.setSelectionRange(2,6,'backward');
    return ['.cam-context-pane','.cam-info-pane','[data-five-key="left-nested"]','[data-five-key="right-nested"]','.five-collision-issue-list'].map((selector,i)=>{const n=document.querySelector(selector);n.scrollTop=[95,115,140,170,380][i];return n.scrollTop;});
  });assert.ok(scrolls.every(n=>n>0));
  const retainedValue=await field.inputValue();await page.evaluate(()=>__alerts.release());await page.waitForFunction(()=>!__alerts.pending);
  const retained=await page.evaluate(()=>({
    input:__alerts.saved.input===document.querySelector('[aria-label="所选刀路名称"]'),canvas:__alerts.saved.canvas===document.querySelector('canvas'),panel:__alerts.saved.panel===document.querySelector('[data-five-collision-dialog]'),list:__alerts.saved.list===document.querySelector('.five-collision-issue-list'),
    focused:document.activeElement===__alerts.saved.input,value:__alerts.saved.input.value,selection:[__alerts.saved.input.selectionStart,__alerts.saved.input.selectionEnd,__alerts.saved.input.selectionDirection],
    scrolls:['.cam-context-pane','.cam-info-pane','[data-five-key="left-nested"]','[data-five-key="right-nested"]','.five-collision-issue-list'].map(selector=>document.querySelector(selector).scrollTop),inputCalls:__alerts.inputCalls
  }));
  assert.equal(retained.input,true);assert.equal(retained.canvas,true);assert.equal(retained.panel,true);assert.equal(retained.list,true);assert.equal(retained.focused,true);assert.equal(retained.value,retainedValue);assert.deepEqual(retained.selection,[2,6,'backward']);assert.deepEqual(retained.scrolls,scrolls);
  await page.keyboard.type('保留监听');assert.ok(await page.evaluate(previous=>__alerts.inputCalls>previous,retained.inputCalls));
  results.push('delayed real workspace patch retains latest typing/caret, input/canvas/panel/list nodes, listeners and both sidebar plus nested/list scroll positions');
  await panel.getByText('检测设置',{exact:true}).click();
  const threshold=page.getByRole('textbox',{name:'碰撞阈值',exact:true});await threshold.fill('0.25');
  await page.evaluate(()=>{
    const n=document.querySelector('[data-five-simulation-field="threshold"]'),detail=document.querySelector('.five-collision-dialog-detail');
    n.focus({preventScroll:true});n.setSelectionRange(1,3,'backward');detail.scrollTop=80;
    __alerts.dialogSaved={input:n,detail,scroll:detail.scrollTop};__alerts.hold();
  });
  await page.keyboard.type('3');await page.evaluate(()=>{const n=__alerts.dialogSaved.input;n.setSelectionRange(1,3,'backward');__alerts.release();});
  await page.waitForFunction(()=>!__alerts.pending);
  assert.deepEqual(await page.evaluate(()=>{
    const s=__alerts.dialogSaved,n=document.querySelector('[data-five-simulation-field="threshold"]');
    return{same:n===s.input,focused:document.activeElement===n,value:n.value,selection:[n.selectionStart,n.selectionEnd,n.selectionDirection],detail:s.detail===document.querySelector('.five-collision-dialog-detail'),scroll:s.detail.scrollTop===s.scroll,open:n.closest('details').open,draft:__alerts.view.fiveSimulation.thresholdDraft};
  }),{same:true,focused:true,value:'035',selection:[1,3,'backward'],detail:true,scroll:true,open:true,draft:'035'});
  results.push('asynchronous refresh preserves the collision settings input node, newest draft and selection, open settings and internal detail scroll');
  await page.evaluate(()=>{__alerts.selection='b';__alerts.render();});
  assert.equal(await page.evaluate(()=>document.activeElement===document.querySelector('[aria-label="所选刀路名称"]')),false);assert.equal(await field.inputValue(),'原始名称');
  const note=page.getByRole('textbox',{name:'条件备注'});await note.focus();await page.evaluate(()=>{__alerts.removeField=true;__alerts.render();});
  assert.equal(await note.count(),0);assert.equal(await page.evaluate(()=>document.activeElement===document.querySelector('[aria-label="所选刀路名称"]')),false);
  results.push('selected-item scope change and conditional field removal never transfer focus or draft to another field');
  await page.evaluate(()=>{const s=__alerts.view.fiveSimulation;s.cursor=0;__alerts.render();const input=document.querySelector('[data-five-simulation-field="repair-deviation"]');input.focus({preventScroll:true});s.cursor=s.frames.findIndex(f=>f.role==='Rapid');__alerts.render();});
  assert.equal(await page.locator('[data-five-simulation-field="repair-deviation"]').count(),0);assert.notEqual(await page.evaluate(()=>document.activeElement?.dataset.fiveSimulationField),'threshold');
  results.push('changing selection to a rapid removes the cut-angle field without focusing the threshold field');
  await drag(680,430);
  for(const size of [{width:1250,height:900},{width:650,height:480}]){
    await page.setViewportSize(size);await page.waitForTimeout(80);geometry=await geometryOf();
    assert.ok(geometry.x>=12&&geometry.y>=12&&geometry.right<=size.width-12+.1&&geometry.bottom<=size.height-12+.1,JSON.stringify(geometry));
    assert.ok(await list.evaluate(n=>n.clientHeight>0));assert.ok(await page.locator('.five-collision-dialog-detail').evaluate(n=>n.clientHeight>0));
  }
  await page.setViewportSize({width:1600,height:1050});await page.waitForTimeout(80);await drag(initial.left,initial.top);
  results.push('resizing the client constrains the entire dialog and keeps both issue/detail scroll regions usable, including a narrow client');
  const canvas=page.locator('canvas'),canvasRect=await canvas.boundingBox();const scenePoint={x:canvasRect.x+45,y:canvasRect.y+canvasRect.height-70};
  const before=await page.evaluate(()=>__alerts.canvasCalls);await page.mouse.click(scenePoint.x,scenePoint.y);assert.equal(await page.evaluate(()=>__alerts.canvasCalls),before+1);
  await page.screenshot({path:resolve(output,'collision-dialog-open.png')});
  results.push('real pointer input reaches the scene outside the independent window');
  await page.evaluate(()=>{__alerts.view.pending=true;__alerts.view.fiveSimulation.checking=true;});
  await page.getByRole('button',{name:'关闭碰撞检测'}).click();assert.equal(await panel.count(),0);
  assert.equal(await page.evaluate(()=>__alerts.view.fiveSimulation.checking),true);
  await page.evaluate(()=>{__alerts.view.pending=false;__alerts.view.fiveSimulation.checking=false;});
  await page.mouse.click(scenePoint.x,scenePoint.y);assert.equal(await page.evaluate(()=>__alerts.canvasCalls),before+2);
  assert.equal(await page.evaluate(()=>{const e=document.elementFromPoint(800,500);return !!e?.closest('[aria-modal="true"],.modal-backdrop,[data-five-collision-dialog]');}),false);
  await page.locator('[data-open-collision]').click();await panel.waitFor();geometry=await geometryOf();assert.ok(Math.abs(geometry.y-geometry.expectedTop)<1&&Math.abs(geometry.x-geometry.expectedLeft)<1);
  await page.screenshot({path:resolve(output,'collision-dialog-reopened.png')});
  assert.deepEqual(await page.evaluate(()=>__alerts.eventErrors),[]);assert.deepEqual(errors,[]);
  results.push('closing is available during a task without cancelling it, leaves no backdrop, and reopening restores the independent window');
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,results,issueCount:count,errors},null,2));
  console.log(JSON.stringify({passed:true,output,results},null,2));
}catch(error){
  await page?.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,results,error:error.stack||error.message,errors},null,2));
  throw error;
}finally{await page?.evaluate(()=>window.__alerts?.dispose()).catch(()=>{});await browser.close();}
