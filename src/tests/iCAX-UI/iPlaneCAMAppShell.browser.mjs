// Actual AppShell, SDK proxies, shared workbench and product UI declarations.
// Only the native transport is a controlled fixture; no CAD or nesting backend
// is simulated and no production UI asset is substituted.
import assert from 'node:assert/strict';
import { ensurePlaneObjectsExpanded } from './iPlaneCAM.browserCommands.mjs';
import { createServer } from 'node:http';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve, sep, extname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const source = resolve(process.env.ICAX_PLANE_ASSET_ROOT || fileURLToPath(new URL('../../', import.meta.url)));
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(fileURLToPath(new URL('../../', import.meta.url)), '../output/tests/iplane-cam-appshell'));
mkdirSync(output, {recursive:true});
const errors=[], failedRequests=[], assets=new Map(), checks=[];
const mime={'.html':'text/html','.mjs':'text/javascript','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.ico':'image/x-icon','.png':'image/png','.json':'application/json'};
const server=createServer((request,response)=>{
  const pathname=decodeURIComponent(new URL(request.url,'http://iplane.test').pathname);
  if(pathname==='/favicon.ico'){response.writeHead(204);response.end();return;}
  const asset=resolve(source,'.'+pathname);
  if(!asset.toLowerCase().startsWith((source+sep).toLowerCase())){response.writeHead(403);response.end();return;}
  try{const bytes=readFileSync(asset);assets.set(pathname,createHash('sha256').update(bytes).digest('hex'));response.writeHead(200,{'Content-Type':mime[extname(asset)]||'application/octet-stream'});response.end(bytes);}
  catch{failedRequests.push(pathname);response.writeHead(404);response.end();}
});
await new Promise(done=>server.listen(0,'127.0.0.1',done));
const origin=`http://127.0.0.1:${server.address().port}`;
const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'chrome',args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
const report={pass:false,assetRoot:source,scope:'Actual framework UI composition only; native transport uses test data. No native CAD operations, solver integration, project persistence or desktop CEF acceptance is asserted.'};
function passed(name){checks.push(name);}
try{
  const page=await browser.newPage({viewport:{width:1600,height:1000}});
  page.on('pageerror',error=>errors.push(error.message));
  await page.addInitScript(()=>{
    const channels={app:'42db9dda-8fea-49eb-a51b-265f70ff0001',product:'42db9dda-8fea-49eb-a51b-265f70ff0002',scene:'42db9dda-8fea-49eb-a51b-265f70ff0003'};
    const projectId='42db9dda-8fea-49eb-a51b-265f70ff0004',sceneId='42db9dda-8fea-49eb-a51b-265f70ff0005';
    const model={
      drawings:[{id:'drawing-1',name:'底图 01',entityIds:[],geometryVersion:'3',description:'独立工作台',units:'mm'}],
      parts:Array.from({length:36},(_,i)=>({id:`part-${i+1}`,name:['连接板 A','框板 B','垫板 C'][i%3]+(i>2?` ${i+1}`:''),entityIds:[`${101+i}`],geometryVersion:'2',width:420,height:260,holeCount:2,quantity:4})),
      stocks:Array.from({length:14},(_,i)=>({id:`stock-${i+1}`,name:i===1?'异形板 B':`矩形板 ${i+1}`,entityIds:[`${201+i}`],width:1200,height:800,holeCount:i===1?1:0,quantity:3})),
      results:Array.from({length:10},(_,i)=>({id:`result-${i+1}`,name:`方案 ${String(i+1).padStart(2,'0')}`,entityIds:[],status:'界面测试记录',placed:12,required:12,boards:[{id:`board-${i+1}`,stockId:'stock-1',name:`B-${String(i+1).padStart(3,'0')}`,entityIds:[`${301+i}`]},...(i===0?[{id:'board-extra',stockId:'stock-2',name:'B-011',entityIds:['399']}]:[])]})),
      layers:[{id:'layer-1',name:'图形',visible:true,locked:false},{id:'layer-2',name:'辅助线',visible:true,locked:false}]
    };
    const scene={sceneId,sceneChannelId:channels.scene,pdo:{enabled:false},undoRedo:{revision:1,canUndo:false,canRedo:false},iplaneCam:model};
    const project={projectId,projectName:'iPlaneCAM 界面编排检查',projectPath:'D:/fixtures/iplane-ui.ipcam',mainSceneId:sceneId,mainScene:scene};
    const catalog={catalogId:'iplane-ui-catalog',mainProject:project};
    const secondScene={...structuredClone(scene),sceneId:'42db9dda-8fea-49eb-a51b-265f70ff0008',sceneChannelId:'42db9dda-8fea-49eb-a51b-265f70ff0009',iplaneCam:{drawings:[{id:'drawing-2',name:'第二工程底图',entityIds:[]}],parts:[],stocks:[],results:[],layers:[]}};
    const secondProject={projectId:'42db9dda-8fea-49eb-a51b-265f70ff0007',projectName:'第二个平面工程',projectPath:'',mainSceneId:secondScene.sceneId,mainScene:secondScene};
    const secondCatalog={catalogId:'iplane-ui-catalog-2',mainProject:secondProject};
    const product={productId:'icax.iplane-cam',productName:'iPlaneCAM',isStarted:true,productChannelId:channels.product,frontendEntry:'apps/iplane-cam/webpage/entry.mjs',catalogs:[catalog],projectFile:{fileExtensions:['ipcam']}};
    const appState={state:'Running',products:[product]},listeners=new Map(),requests=[],saves=[];
    globalThis.__iplaneUIFixture={scene,project,product,requests,saves,secondProject};
    let projectOpens=0;
    const methods=['App.GetState','App.StartProduct','Product.GetState','Product.OpenProjectCatalog','Project.GetState','Project.GetUndoRedoState','Project.Save','View.GetOrCreate','View.Release','PlaneScene.ReadState'];
    const correctChannel=(method,channel)=>method.startsWith('App.')?channel===channels.app:method.startsWith('Product.')?channel===channels.product:[channels.scene,secondScene.sceneChannelId].includes(channel);
    function responseFor(method,payload,channel){
      const activeProject=channel===secondScene.sceneChannelId?secondProject:project;
      const activeScene=channel===secondScene.sceneChannelId?secondScene:scene;
      if(method==='App.GetState')return appState;
      if(method==='App.StartProduct')return {state:appState,product};
      if(method==='Product.GetState')return product;
      if(method==='Product.OpenProjectCatalog'){if(++projectOpens===1)return {state:product,catalog};if(!product.catalogs.includes(secondCatalog))product.catalogs.push(secondCatalog);return {state:product,catalog:secondCatalog};}
      if(method==='Project.GetState')return {project:activeProject,activeScene};
      if(method==='Project.GetUndoRedoState')return activeScene.undoRedo;
      if(method==='PlaneScene.ReadState')return {iplaneCam:activeScene.iplaneCam,revision:'1',tasks:[]};
      if(method==='Project.Save'){saves.push(structuredClone(payload));return {saved:true,projectPath:activeProject.projectPath,project:activeProject};}
      if(method==='View.GetOrCreate')return {viewId:'iplane-ui-view',resource:{url:'icax-resource://iplane-ui/view',version:'1'}};
      if(method==='View.Release')return {};
      throw Error('Unexpected native fixture method: '+method);
    }
    globalThis.icax={
      async getApplicationChannelId(){return channels.app;},
      async registerProductChannel(){return channels.product;},
      async registerSceneChannel(requestProjectId,requestSceneId){return requestSceneId===secondScene.sceneId?secondScene.sceneChannelId:channels.scene;},
      subscribeSDOFrames(channel,handler){if(!listeners.has(channel))listeners.set(channel,new Set());listeners.get(channel).add(handler);return ()=>listeners.get(channel)?.delete(handler);},
      async postSDOFrame(frame){
        if(Number(frame.kind)!==0)return;
        const {makeSDOMethodCodeFromName}=await import('/iCAX-UI/SDK/SDO/sdoMethod.mjs');
        const {deserializeVariantText,serializeVariantText}=await import('/iCAX-UI/SDK/SDO/variantSerializer.mjs');
        const method=methods.find(name=>makeSDOMethodCodeFromName(name)===String(frame.methodCode));
        if(!method)throw Error('Unimplemented SDO must not be called by UI design: '+frame.methodCode);
        if(!correctChannel(method,frame.channelId))throw Error('Incorrect SDO scope: '+method);
        const payload=deserializeVariantText(frame.payloadText);requests.push({method,channelId:frame.channelId,payload:structuredClone(payload)});
        const data=responseFor(method,payload,frame.channelId);const response={...frame,kind:2,status:0,payloadText:serializeVariantText(data)};
        const deliver=()=>listeners.get(frame.channelId)?.forEach(handler=>handler(response));
        if(globalThis.__iplaneUIFixture.replyDelay)setTimeout(deliver,globalThis.__iplaneUIFixture.replyDelay);else queueMicrotask(deliver);
      },
      async requestResource(request){
        if(request.url!=='icax-resource://iplane-ui/view')throw Error('Unexpected UI resource '+request.url);
        const bytes=new Uint8Array(48),data=new DataView(bytes.buffer);
        data.setUint32(0,24,true);bytes.set([73,67,86,87],4);data.setUint16(8,14,true);data.setUint16(10,20,true);data.setUint16(12,4,true);data.setUint16(16,8,true);data.setInt32(24,16,true);data.setUint32(28,2,true);data.setBigUint64(32,1n,true);
        return {status:200,headers:{'ICAX-Resource-Version':'1','Content-Type':'application/vnd.icax.flatbuffer'},body:request.method==='HEAD'?new ArrayBuffer(0):bytes.buffer};
      },
      async saveFileDialog(){return project.projectPath;},async openFileDialog(){return null;}
    };
  });
  await page.goto(origin+'/iCAX-UI/SDK/AppShell/index.html');
  await page.waitForFunction(()=>globalThis.__icaxAppShell?.getState().startupPhase==='ready'&&document.querySelector('.cam-workbench'),null,{timeout:25000});
  assert.equal(await page.title(),'iPlaneCAM');
  await ensurePlaneObjectsExpanded(page);
  passed('Unmodified AppShell startup and original SDK/proxy chain loads iPlaneCAM');
  const tabs=page.locator('.product-ribbon [data-action="select-ribbon-tab"]');
  assert.deepEqual((await tabs.allTextContents()).map(text=>text.trim()),['底图','下料']);
  assert.equal(await page.locator('[data-command-id="plane.create-part"],[data-command-id="plane.create-stock"]').count(),0);
  assert.equal(await page.locator('.product-ribbon').getByRole('button',{name:'文件',exact:true}).count(),0);
  assert.equal(await page.locator('.title-bar [data-command-id="app.save"]').count(),1);
  assert.equal(await page.locator('.title-bar [data-action="open-start-center"]').count(),1);
  passed('File/project/save/history/window UI belongs to AppShell; no duplicate File ribbon');
  await page.waitForFunction(()=>document.querySelector('.icax-three-viewport canvas'));
  const drawingLayout=await page.evaluate(()=>{
    const box=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom};};
    return {workspace:box('.iplane-workspace'),viewer:box('.cam-viewer'),right:box('.cam-info-pane'),command:box('[data-iplane-command-root]')};
  });
  assert.ok(Math.abs(drawingLayout.right.right-drawingLayout.workspace.right)<1);
  assert.ok(Math.abs(drawingLayout.command.left-drawingLayout.workspace.left)<1&&Math.abs(drawingLayout.command.right-drawingLayout.workspace.right)<1);
  assert.ok(drawingLayout.command.top>=drawingLayout.viewer.bottom-1);report.drawingGeometry=drawingLayout;
  await page.evaluate(()=>{
    const fixture=__iplaneUIFixture;
    fixture.canvas=document.querySelector('.icax-three-viewport canvas');
    fixture.canvasEvents=0;fixture.canvas.addEventListener('iplane-probe',()=>fixture.canvasEvents++);
  });
  for(const id of ['drawing','nesting']){
    await page.locator(`[data-action="select-ribbon-tab"][data-tab-id="${id}"]`).click();
    await page.waitForFunction(id=>__icaxAppShell.getState().activeRibbonTabId===id,id);
    await page.locator('[data-iplane-object-id]').first().click();
    assert.equal(await page.locator('[data-command-id="plane.create-part"],[data-command-id="plane.create-stock"]').count(),0);
    assert.equal(await page.locator('.product-ribbon').getByText('从底图生成',{exact:true}).count(),0);
    await page.screenshot({path:resolve(output,`${id}-appshell.png`)});
    assert.equal(await page.evaluate(()=>__iplaneUIFixture.canvas===document.querySelector('.icax-three-viewport canvas')&&__iplaneUIFixture.canvas.isConnected),true);
  }
  passed('Drawing and unified cutting area use the same existing Three viewport canvas');
  const dock=page.locator('[data-iplane-dock-list]');
  assert.equal(await dock.count(),2);
  const nestingGeometry=await page.evaluate(()=>{
    const rect=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom,width:r.width,height:r.height};};
    return {left:rect('.cam-context-pane'),viewer:rect('.cam-viewer'),right:rect('.cam-info-pane'),stocks:rect('[data-iplane-dock-list="stocks"]'),results:rect('[data-iplane-dock-list="results"]')};
  });
  report.nestingGeometry=nestingGeometry;
  assert.ok(nestingGeometry.left.right<=nestingGeometry.viewer.left);
  assert.ok(nestingGeometry.right.left>=nestingGeometry.viewer.right);
  assert.ok(nestingGeometry.stocks.top>=nestingGeometry.viewer.bottom-1);
  assert.ok(nestingGeometry.results.top>=nestingGeometry.viewer.bottom-1);
  assert.ok(nestingGeometry.stocks.left>=nestingGeometry.viewer.left-1&&nestingGeometry.results.right<=nestingGeometry.viewer.right+1);
  passed('Unified cutting layout places parts left, stock/results below the canvas, and properties right');
  await page.locator('[data-iplane-object-id="result-1"]').click();
  await page.locator('[data-iplane-object-id="part-2"]').click();
  assert.equal(await page.locator('[data-iplane-field="name"]').inputValue(),'框板 B');
  assert.equal(await page.locator('[data-iplane-field="quantity"]').count(),1);
  const canvasSelection=()=>page.evaluate(async()=>{
    const {getProjectView}=await import('/apps/_shared/workbench/state/projectViewStore.mjs');
    const {getCanvasObject,getAreaEntityIds}=await import('/apps/iplane-cam/webpage/workspaceState.mjs');
    const view=getProjectView(__iplaneUIFixture.project.projectId),canvas=getCanvasObject({},view);
    return {kind:canvas?.kind,id:canvas?.row.id,visible:getAreaEntityIds({},view),camera:view.viewport.getCameraState()};
  });
  const resultCanvas=await canvasSelection();
  assert.equal(resultCanvas.kind,'results');assert.equal(resultCanvas.id,'board-1');assert.deepEqual(resultCanvas.visible,['301']);
  await page.locator('[data-iplane-object-id="board-extra"]').click();
  assert.deepEqual((await canvasSelection()).visible,['399']);
  await page.locator('[data-iplane-object-id="result-1"]').click();
  assert.deepEqual((await canvasSelection()).visible,['301']);
  passed('A multi-board result displays one physical board at a time and selecting a board does not overlay its siblings');
  await page.locator('[data-iplane-object-id="stock-2"]').click();
  assert.equal(await page.locator('[data-iplane-field="name"]').inputValue(),'异形板 B');
  assert.deepEqual((await canvasSelection()).visible,['202']);
  assert.equal(await page.locator('.cam-context-pane [data-iplane-object-id="part-1"]').count(),1);
  await page.locator('[data-iplane-object-id="part-1"]').click();
  assert.equal((await canvasSelection()).id,'stock-2');
  await page.locator('[data-iplane-object-id="result-1"]').click();
  assert.equal(await page.locator('[data-iplane-field="quantity"]').count(),0);
  assert.deepEqual((await canvasSelection()).visible,['301']);
  await page.screenshot({path:resolve(output,'nesting-appshell.png')});
  passed('Parts inspect without replacing the active board; stock/result selection changes canvas and right-side properties without overlaying source geometry');
  const bottomSplitter=page.locator('[data-cam-resize-pane="bottom"]');
  const splitterBox=await bottomSplitter.boundingBox();
  await page.mouse.move(splitterBox.x+splitterBox.width/2,splitterBox.y+splitterBox.height/2);
  await page.mouse.down();await page.mouse.move(splitterBox.x+splitterBox.width/2,splitterBox.y-35,{steps:4});await page.mouse.up();
  const resizedHeight=await page.evaluate(async()=>{
    const {getProjectView}=await import('/apps/_shared/workbench/state/projectViewStore.mjs');return getProjectView(__iplaneUIFixture.project.projectId).layout.bottomHeight;
  });
  assert.ok(resizedHeight>142);
  passed('Bottom dock uses the existing shared workbench splitter after switching from drawing');
  await page.locator('.icax-three-viewport canvas').click();
  assert.equal(await page.locator('.iplane-workspace').getAttribute('data-iplane-area'),'nesting');
  assert.equal(await page.evaluate(async()=>{
    const {getProjectView}=await import('/apps/_shared/workbench/state/projectViewStore.mjs');return getProjectView(__iplaneUIFixture.project.projectId).activeAreaId;
  }),'nesting');
  passed('Retained viewport picks dispatch through the current cutting context');
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();
  await page.locator('.title-bar [data-command-id="app.save"]').click();
  await page.waitForFunction(()=>__iplaneUIFixture.saves.length===1);
  assert.equal(await page.evaluate(()=>__iplaneUIFixture.requests.filter(r=>r.method==='Project.Save').length),1);
  passed('Save routes through the actual AppShell ProjectProxy and scene channel');
  await page.locator('.title-bar [data-action="open-start-center"]').click();
  await page.waitForFunction(()=>__icaxAppShell.getState().startCenterOpen);
  await page.screenshot({path:resolve(output,'framework-project-menu.png')});
  await page.locator('[data-action="close-start-center"]').click();
  await page.waitForFunction(()=>!__icaxAppShell.getState().startCenterOpen);
  assert.equal(await page.evaluate(()=>__iplaneUIFixture.canvas===document.querySelector('.icax-three-viewport canvas')),true);
  passed('Existing project/start center opens and closes without replacing the viewport');
  await page.setViewportSize({width:1280,height:720});
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
  await page.locator('[data-iplane-object-id="part-1"]').click();
  const propertyName=page.locator('[data-iplane-field="name"]');
  await propertyName.fill('界面草稿名称');
  await page.evaluate(async()=>{
    const f=__iplaneUIFixture;
    const {getProjectView}=await import('/apps/_shared/workbench/state/projectViewStore.mjs');
    f.view=getProjectView(f.project.projectId);f.viewport=f.view.viewport;
    f.camera=f.viewport.getCameraState();f.nameInput=document.querySelector('[data-iplane-field="name"]');
    f.inputEvents=0;f.nameInput.addEventListener('iplane-input-probe',()=>f.inputEvents++);
    f.nameInput.focus({preventScroll:true});f.nameInput.setSelectionRange(1,4);
    document.querySelector('[data-iplane-list]').scrollTop=130;
    document.querySelector('.cam-info-pane').scrollTop=35;
    f.replyDelay=50;f.refresh=__icaxAppShell.refresh();
    f.nameInput.setSelectionRange(2,6);
    document.querySelector('[data-iplane-list]').scrollTop=260;
    document.querySelector('.cam-info-pane').scrollTop=85;
    document.querySelector('[data-iplane-dock-list="stocks"]').scrollLeft=180;
    document.querySelector('[data-iplane-dock-list="results"]').scrollLeft=130;
    f.latestScroll=[document.querySelector('.cam-context-pane').scrollTop,document.querySelector('.cam-info-pane').scrollTop,document.querySelector('[data-iplane-list]').scrollTop,document.querySelector('[data-iplane-dock-list="stocks"]').scrollLeft,document.querySelector('[data-iplane-dock-list="results"]').scrollLeft];
  });
  await page.evaluate(()=>__iplaneUIFixture.refresh);
  const interaction=await page.evaluate(()=>{
    const f=__iplaneUIFixture;f.nameInput.dispatchEvent(new Event('iplane-input-probe'));
    return {sameInput:f.nameInput===document.querySelector('[data-iplane-field="name"]'),sameCanvas:f.canvas===document.querySelector('.icax-three-viewport canvas'),sameViewport:f.viewport===f.view.viewport,
      inputEvents:f.inputEvents,focused:document.activeElement===f.nameInput,selection:[f.nameInput.selectionStart,f.nameInput.selectionEnd],value:f.nameInput.value,
      scroll:[document.querySelector('.cam-context-pane').scrollTop,document.querySelector('.cam-info-pane').scrollTop,document.querySelector('[data-iplane-list]').scrollTop,document.querySelector('[data-iplane-dock-list="stocks"]').scrollLeft,document.querySelector('[data-iplane-dock-list="results"]').scrollLeft],latestScroll:f.latestScroll,
      camera:f.viewport.getCameraState(),originalCamera:f.camera,sourceName:f.scene.iplaneCam.parts[0].name,
      originalInputConnected:f.nameInput.isConnected,currentInput:document.querySelector('[data-iplane-field="name"]')?.outerHTML,originalInput:f.nameInput.outerHTML};
  });
  report.interaction=interaction;
  assert.equal(interaction.sameInput,true);assert.equal(interaction.sameCanvas,true);assert.equal(interaction.sameViewport,true);
  assert.equal(interaction.inputEvents,1);assert.equal(interaction.focused,true);assert.deepEqual(interaction.selection,[2,6]);
  assert.equal(interaction.value,'界面草稿名称');assert.deepEqual(interaction.scroll,interaction.latestScroll);assert.ok(interaction.scroll[2]>0);
  assert.ok(interaction.scroll[1]>0);assert.ok(interaction.scroll[3]>0);assert.ok(interaction.scroll[4]>0);
  assert.deepEqual(interaction.camera,interaction.originalCamera);assert.equal(interaction.sourceName,'连接板 A');
  report.interaction=interaction;
  passed('Delayed AppShell refresh retains actual inputs/listeners/focus/selection, latest pane and bottom scrolling, canvas and camera');
  await page.locator('[data-iplane-action="reset-draft"]').click();
  await page.locator('[data-iplane-field="quantity"]').focus();
  await page.evaluate(()=>{__iplaneUIFixture.quantityInput=document.querySelector('[data-iplane-field="quantity"]');});
  await page.locator('[data-iplane-object-id="result-1"]').click();
  assert.equal(await page.locator('[data-iplane-field="quantity"]').count(),0);
  assert.equal(await page.evaluate(()=>__iplaneUIFixture.quantityInput.isConnected),false);
  assert.equal(await page.evaluate(()=>document.activeElement===document.querySelector('[data-iplane-field="name"]')),false);
  passed('Changing from part to result removes inapplicable quantity and does not move focus to another object field');
  await page.setViewportSize({width:1280,height:800});
  await page.screenshot({path:resolve(output,'nesting-1280-appshell.png')});
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();
  assert.equal(await page.locator('.product-ribbon [data-command-id^="plane.draw-"]').count(),0);
  await page.locator('[data-command-id="plane.select"]').click();
  assert.equal(await page.locator('[data-command-id="plane.import-dxf"]').isDisabled(),true);
  await page.screenshot({path:resolve(output,'drawing-1280-appshell.png')});
  passed('Actual Ribbon icons remain loaded; unavailable draw tools are omitted and business commands stay disabled without declared native capabilities');
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();
  assert.equal(await page.locator('[data-command-id="plane.select"]').getAttribute('aria-pressed'),'true');
  await page.locator('.icax-three-viewport canvas').click();
  assert.equal(await page.locator('.iplane-workspace').getAttribute('data-iplane-area'),'nesting');
  passed('Cutting retains an explicit Select command when no drawing capability is declared');
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();
  await page.evaluate(()=>{__iplaneUIFixture.firstWorkspace=document.querySelector('main.workspace');});
  await page.evaluate(()=>__icaxAppShell.createProject({projectName:'第二个平面工程'}));
  await page.waitForFunction(()=>__icaxAppShell.getState().activeProjectId===__iplaneUIFixture.secondProject.projectId);
  assert.equal(await page.evaluate(()=>__iplaneUIFixture.firstWorkspace.isConnected),false);
  assert.equal(await page.evaluate(()=>document.querySelector('main.workspace')!==__iplaneUIFixture.firstWorkspace),true);
  assert.equal(await page.locator('[data-iplane-object-id="drawing-2"]').count(),1);
  assert.equal(await page.locator('[data-iplane-object-id="drawing-1"]').count(),0);
  await page.locator('[data-action="select-open-project"][data-project-id="42db9dda-8fea-49eb-a51b-265f70ff0004"]').click();
  await page.waitForFunction(()=>__icaxAppShell.getState().activeProjectId===__iplaneUIFixture.project.projectId);
  assert.equal(await page.locator('[data-iplane-object-id="drawing-1"]').count(),1);
  assert.equal(await page.locator('[data-iplane-object-id="drawing-2"]').count(),0);
  assert.equal(await page.evaluate(()=>__iplaneUIFixture.canvas===document.querySelector('.icax-three-viewport canvas')),true);
  passed('Different project ownership replaces the workspace; switching back restores only its own viewport and objects');
  const visibleCommands=(await page.locator('.product-ribbon').allTextContents()).join(' ');
  assert.ok(!/工艺|NC|激光|等离子/.test(visibleCommands));
  passed('No industry process or NC placeholder workflow');
  await page.evaluate(()=>__iplaneUIFixture.canvas.dispatchEvent(new Event('iplane-probe')));
  assert.equal(await page.evaluate(()=>__iplaneUIFixture.canvasEvents),1);
  passed('Viewport listener survives product navigation and framework project operations');
  assert.deepEqual(errors,[]);assert.deepEqual(failedRequests,[]);
  for(const asset of ['/iCAX-UI/SDK/AppShell/index.html','/iCAX-UI/SDK/AppShell/app/bootstrap.mjs','/iCAX-UI/SDK/runtime.mjs','/iCAX-UI/AppProxy/AppProxy.mjs','/iCAX-UI/ProductProxy/ProductProxy.mjs','/iCAX-UI/ProjectProxy/ProjectProxy.mjs','/iCAX-UI/SceneProxy/SceneProxy.mjs','/apps/_shared/workbench/createWorkbench.mjs','/iCAX-UI/SDK/Viewport/threeViewport.mjs','/apps/iplane-cam/webpage/entry.mjs'])assert.ok(assets.has(asset),asset+' was not loaded');
  passed('Source assets are actually loaded; no cloned shell or mocked UI module');
  report.pass=true;
  report.requests=await page.evaluate(()=>__iplaneUIFixture.requests);
}finally{
  Object.assign(report,{checks,errors,failedRequests,assets:Object.fromEntries(assets)});
  writeFileSync(resolve(output,'browser-report.json'),JSON.stringify(report,null,2));
  await browser.close();await new Promise(done=>server.close(done));
}
console.log(JSON.stringify({pass:report.pass,checks:checks.length,output}));
