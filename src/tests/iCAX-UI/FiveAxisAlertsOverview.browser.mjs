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
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||resolve(tmpdir(),'icax-five-axis-alerts-overview-browser'));
mkdirSync(output,{recursive:true});
const browser=await chromium.launch({channel:'msedge',headless:true});
const results=[],errors=[];let page;
try{
  page=await browser.newPage({viewport:{width:1600,height:1050}});
  page.on('pageerror',error=>errors.push(error.stack||error.message));
  await page.route('http://five-overview.test/**',route=>{
    const url=new URL(route.request().url());
    if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><html><head><meta charset="utf-8"></head><body></body></html>'});
    const path=resolve(root,decodeURIComponent(url.pathname).slice(1));
    if(!path.startsWith(root+sep))return route.abort();
    return route.fulfill({contentType:extname(path)==='.css'?'text/css':'text/javascript',body:readFileSync(path)});
  });
  await page.goto('http://five-overview.test/');
  await page.evaluate(async()=>{
    const base='/src/apps/five-axis-cam/webpage/';
    const [alerts,{patchWorkspacePanels},{syncFloatingDialogs,disposeFloatingDialogs},{renderCollisionCheck,simulationCommand},{installCollisionProgressStyle},{installCollisionDiagnosticsStyle},{selectTool},{buildPlayback},THREE]=await Promise.all([
      import(base+'alerts.mjs'),import(base+'workspaceDom.mjs'),import(base+'floatingDialogs.mjs'),import(base+'trajectorySimulation.mjs'),import(base+'collisionProgress.mjs'),import(base+'collisionDiagnostics.mjs'),import(base+'workspaceModel.mjs'),import(base+'trajectoryPlayback.mjs'),import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js')]);
    const workspaceStyle=document.createElement('link');workspaceStyle.rel='stylesheet';workspaceStyle.href=base+'workspace.css';document.head.append(workspaceStyle);
    alerts.installAlertStyle(document);installCollisionProgressStyle(document);installCollisionDiagnosticsStyle(document);
    await Promise.all([...document.querySelectorAll('link[rel="stylesheet"]')].map(link=>link.sheet?null:new Promise((resolve,reject)=>{link.addEventListener('load',resolve,{once:true});link.addEventListener('error',reject,{once:true});})));
    const style=document.createElement('style');style.textContent=`
      *{box-sizing:border-box}body{margin:0;font:13px system-ui;background:#e8edf0}button,input{font:inherit}
      .product-ribbon{height:120px;display:flex;justify-content:flex-end;align-items:flex-start;padding:12px 22px;background:#edf2f4}
      .five-workspace{height:calc(100vh - 120px);display:grid;grid-template-columns:220px 1fr 250px;position:relative;overflow:hidden}
      .cam-context-pane,.cam-info-pane{height:100%;min-height:0;overflow:auto;background:#f6f8fa;padding:12px}
      .pane-spacer{height:800px}.nested-pane{height:180px;overflow:auto;border:1px solid #aab8bf}.nested-content{height:650px}
      .cam-render-viewport{position:relative;min-width:0;min-height:0}.cam-render-viewport canvas{width:100%;height:100%;display:block;background:#cbd8df}
      [data-five-overlay]{position:absolute;inset:0;pointer-events:none}
      [data-fixture-window],[data-machine-window]{position:fixed;left:300px;top:270px;width:350px;padding:15px;background:white;border:1px solid #b5cbd3;pointer-events:auto}
    `;document.head.append(style);
    document.body.innerHTML='<header class="product-ribbon"><button data-ribbon-alerts>告警</button></header><main id="mount" class="five-workspace"><aside class="cam-context-pane"></aside><section class="cam-render-viewport"><canvas tabindex="0" aria-label="三维场景"></canvas><div data-five-overlay></div></section><aside class="cam-info-pane"></aside><div data-five-dock></div><div data-five-alert-host></div></main>';
    const setup={id:'setup',rapidSettings:{followHeightMm:1,collisionThresholdMm:.1},headModel:{kind:'Builtin'},generatedGeometry:{status:'Current',operations:[{intentId:'cut',placementId:'placement',order:1,parts:[{role:'Cut',points:[[0,0,0],[10,0,0],[20,0,0]],directions:[[0,0,1],[0,0,1],[0,0,1]]}]}],transitions:[]}};
    const placement={id:'placement',workpieceId:'workpiece',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]};
    const frames=buildPlayback(setup,[placement]).frames;
    setup.simulationCheck={validity:'Current',status:'Collision',collisionCount:String(frames.length-1),uncheckedCount:'0',scope:'BuiltinSchematicWorkpieceAndFixtures',frames:frames.map(f=>({position:f.position.toArray(),rotation:f.rotation.toArray()})),segments:frames.slice(0,-1).map((_,i)=>({index:String(i),status:'Collision',obstacleId:'workpiece',fraction:.5}))};
    const fixtureUnit={id:'fixture-unit',name:'端部支撑',setupId:'setup',enabled:true,design:{assembly:{id:'assembly',parts:[{partId:'fixture-part'}],cutClearance:{validity:'Current',status:'Blocked'}}}};
    const mount=document.querySelector('#mount'),canvas=mount.querySelector('canvas');
    const view={fiveAxisArea:'toolpath',fiveAlertsOpen:true,fiveCollisionOpen:false,fiveDialogPositions:{},scene:{intentToolpaths:[{id:'cut',name:'外轮廓'}],domain:{expected:{epoch:'fixture',mutationSequence:'1'},activeSetupId:'setup',setups:[setup,{id:'other'}],placements:[placement],workpieces:[{id:'workpiece',name:'试验件'}],fixtureUnits:[fixtureUnit],fixtures:[{id:'fixture-part',setupId:'setup',unitId:'fixture-unit',enabled:true,collisionEnabled:true}]}},viewport:{scene:new THREE.Scene(),sceneObjects:new Map(),renderer:{domElement:canvas}}};
    selectTool(view,'collision');
    const context={mount,isCurrentProject:()=>true},binding={context,view};
    const fixture={view,context,binding,setup,fixtureUnit,commands:[],canvasCalls:0,inputCalls:0,providerEnabled:false,pending:false};
    const left=()=>'<h3>刀路</h3><div class="nested-pane" data-five-key="left-nested"><div class="nested-content">工序树</div></div><div class="pane-spacer"></div>';
    const right=()=>'<section data-five-key="inspector" data-pane-interaction-scope="cut:cut"><label>名称<input type="text" data-five-key="name" aria-label="所选刀路名称" value="外轮廓"></label></section><div class="nested-pane" data-five-key="right-nested"><div class="nested-content">参数列表</div></div><div class="pane-spacer"></div>';
    fixture.render=()=>{
      const editors=(fixture.fixtureOpen?'<section data-fixture-window role="dialog" aria-label="夹具处理">端部支撑 · 切割避让设置</section>':'')+(fixture.machineOpen?'<section data-machine-window role="dialog" aria-label="设备处理">行程越界设置</section>':'');
      if(!patchWorkspacePanels(mount,[['.cam-context-pane',left()],['.cam-info-pane',right()],['[data-five-alert-host]',alerts.renderWorkspaceAlerts(view)],['[data-five-overlay]',renderCollisionCheck(view)+editors]]))throw Error('Missing fixture patch region');
      alerts.syncAlertPanel(context,view,binding);syncFloatingDialogs(mount,binding);
    };
    // Only command routing is adapted here. No SDO, native result or save is simulated.
    mount.addEventListener('click',event=>{
      const command=event.target.closest('[data-five-command]')?.dataset.fiveCommand;if(!command)return;
      if(command==='five.alerts.close'){view.fiveAlertsOpen=false;fixture.render();return;}
      if(command.startsWith('five.alerts.open:')){
        const target=alerts.workspaceAlertTarget(view,decodeURIComponent(command.slice('five.alerts.open:'.length)));if(!target)return;
        view.fiveAlertsOpen=false;
        fixture.commands.push(target.commandId);
        if(target.commandId==='five.tool.collision')view.fiveCollisionOpen=true;
        else if(['five.fixture.light-open:fixture-unit','five.fixture.select:fixture-unit'].includes(target.commandId))fixture.fixtureOpen=true;
        else if(target.commandId==='five.tool.machine-simulation')fixture.machineOpen=true;
        fixture.render();return;
      }
      if(command==='five.simulation.collision-close')void simulationCommand(context,view,'collision-close',{renderProject:fixture.render});
    });
    document.querySelector('[data-ribbon-alerts]').addEventListener('click',()=>{view.fiveAlertsOpen=!view.fiveAlertsOpen;fixture.render();});
    canvas.addEventListener('pointerdown',()=>fixture.canvasCalls++);
    fixture.registerProvider=()=>fixture.unregister=alerts.registerWorkspaceAlertProvider('travel',(_view,active)=>fixture.providerEnabled&&active?.id==='setup'?[{id:'limit',title:'设备行程越界',detail:'打开设备处理窗体',severity:'error',target:{commandId:'five.tool.machine-simulation'}}]:[]);
    fixture.hold=()=>{fixture.pending=true;fixture.awaited=new Promise(resolve=>fixture.release=resolve).then(()=>{fixture.setup.simulationCheck.collisionCount='3';fixture.providerEnabled=true;fixture.render();fixture.pending=false;});};
    fixture.dispose=()=>{fixture.unregister?.();alerts.disposeAlertPanel(binding);disposeFloatingDialogs(binding);};
    fixture.render();window.__overview=fixture;
  });
  const summary=page.locator('[data-five-alert-panel]'),collision=page.locator('[data-five-collision-dialog]');
  await summary.waitFor();assert.equal(await collision.count(),0);
  assert.equal(await summary.locator('[data-alert-id]').count(),1);
  assert.equal(await summary.locator('input,select,details,[data-five-simulation-field],.five-collision-issue').count(),0);
  const links=await summary.locator('[data-alert-id]').evaluateAll(nodes=>nodes.map(n=>({id:n.dataset.alertId,command:n.dataset.fiveCommand})));
  assert.deepEqual(links,[{id:'collision',command:'five.alerts.open:collision'}]);
  results.push('overview keeps head collision alerts and omits removed optical verification');
  await summary.locator('[data-alert-id="collision"]').click();await collision.waitFor();
  assert.deepEqual(await page.evaluate(()=>__overview.commands),['five.tool.collision']);assert.equal(await page.locator('[data-fixture-window]').count(),0);
  assert.equal(await summary.count(),0);await page.locator('[data-ribbon-alerts]').click();await summary.waitFor();
  const summaryNode=await page.evaluate(()=>{__overview.summaryNode=document.querySelector('[data-five-alert-panel]');return true;});assert.equal(summaryNode,true);
  await page.getByRole('button',{name:'关闭碰撞检测'}).click();assert.equal(await collision.count(),0);
  assert.equal(await page.evaluate(()=>document.querySelector('[data-five-alert-panel]')===__overview.summaryNode&&__overview.view.fiveAlertsOpen),true);
  assert.equal(await summary.locator('[data-alert-id^="fixture-light:"]').count(),0);
  results.push('closing collision retains the overview without removed fixture validation links');
  await page.evaluate(()=>{__overview.fixtureOpen=false;__overview.registerProvider();__overview.providerEnabled=true;__overview.render();});
  const third=summary.locator('[data-alert-id="travel:limit"]');await third.waitFor();
  assert.equal(await third.getAttribute('data-five-command'),'five.alerts.open:travel%3Alimit');
  await third.click();await page.locator('[data-machine-window]').waitFor();assert.equal(await page.evaluate(()=>__overview.commands.at(-1)),'five.tool.machine-simulation');
  await page.locator('[data-ribbon-alerts]').click();await summary.waitFor();
  results.push('a registered third alert provider appears and routes to its own window through the unchanged shared summary renderer');
  await page.evaluate(()=>{__overview.machineOpen=false;__overview.providerEnabled=false;__overview.render();});
  const field=page.getByRole('textbox',{name:'所选刀路名称'});await field.fill('异步前继续编辑');
  await page.evaluate(()=>{
    const f=__overview,n=document.querySelector('[aria-label="所选刀路名称"]');n.focus({preventScroll:true});n.setSelectionRange(1,4,'backward');
    f.saved={input:n,canvas:document.querySelector('canvas'),summary:document.querySelector('[data-five-alert-panel]'),row:document.querySelector('[data-alert-id="collision"]')};
    n.addEventListener('input',()=>f.inputCalls++);f.hold();
  });
  await page.keyboard.type('更晚输入');
  const scrolls=await page.evaluate(()=>{
    __overview.saved.input.setSelectionRange(2,6,'backward');
    return ['.cam-context-pane','.cam-info-pane','[data-five-key="left-nested"]','[data-five-key="right-nested"]'].map((selector,i)=>{const n=document.querySelector(selector);n.scrollTop=[95,115,140,170][i];return n.scrollTop;});
  });assert.ok(scrolls.every(n=>n>0));const value=await field.inputValue();
  await page.evaluate(()=>__overview.release());await page.waitForFunction(()=>!__overview.pending);
  const kept=await page.evaluate(()=>{const s=__overview.saved,n=document.querySelector('[aria-label="所选刀路名称"]');return{input:n===s.input,canvas:document.querySelector('canvas')===s.canvas,summary:document.querySelector('[data-five-alert-panel]')===s.summary,row:document.querySelector('[data-alert-id="collision"]')===s.row,focused:document.activeElement===n,value:n.value,selection:[n.selectionStart,n.selectionEnd,n.selectionDirection],scrolls:['.cam-context-pane','.cam-info-pane','[data-five-key="left-nested"]','[data-five-key="right-nested"]'].map(selector=>document.querySelector(selector).scrollTop),inputCalls:__overview.inputCalls};});
  assert.deepEqual({...kept,inputCalls:undefined},{input:true,canvas:true,summary:true,row:true,focused:true,value,selection:[2,6,'backward'],scrolls,inputCalls:undefined});
  await page.keyboard.type('保留监听');assert.ok(await page.evaluate(previous=>__overview.inputCalls>previous,kept.inputCalls));
  assert.equal(await summary.locator('[data-alert-id]').count(),2);await page.getByText('示意切割头检测：3 段间隙告警',{exact:true}).waitFor();
  results.push('late generic-list updates retain newest sidebar text, caret/selection, input/listener/canvas/summary/row nodes and both sidebar/nested scroll positions');
  await page.evaluate(()=>{__overview.setup.simulationCheck.collisionCount='0';__overview.fixtureUnit.design.assembly.cutClearance={validity:'Current',status:'Clear'};__overview.render();});
  assert.equal(await summary.locator('[data-alert-id="collision"],[data-alert-id="fixture-light:fixture-unit"]').count(),0);assert.equal(await third.count(),1);
  await page.evaluate(()=>{__overview.view.scene.domain.activeSetupId='other';__overview.render();});
  assert.equal(await summary.locator('[data-alert-id]').count(),0);await summary.getByText('暂无告警',{exact:true}).waitFor();
  assert.equal(await collision.count(),0);assert.equal(await page.locator('[data-fixture-window],[data-machine-window]').count(),0);
  results.push('resolved collision and fixture warnings disappear; switching setup removes the third provider warning without stale links');
  await page.evaluate(()=>{const f=__overview;f.providerEnabled=false;f.view.scene.domain.activeSetupId='setup';f.setup.fixtureBeamCheck={validity:'Current',status:'Clear'};f.fixtureUnit.design.assembly.geometryValidity='Stale';f.render();});
  const geometryWarning=summary.locator('[data-alert-id="fixture-geometry:fixture-unit"]');await geometryWarning.waitFor();assert.equal(await summary.locator('[data-alert-id^="fixture-light:"]').count(),0);assert.equal(await summary.locator('[data-alert-id]').count(),1);
  await geometryWarning.click();await page.locator('[data-fixture-window]').waitFor();assert.equal(await page.evaluate(()=>__overview.commands.at(-1)),'five.fixture.select:fixture-unit');
  await page.locator('[data-ribbon-alerts]').click();
  await page.evaluate(()=>{const f=__overview;f.fixtureOpen=false;f.setup.fixtureBeamCheck.validity='Stale';f.view.scene.domain.fixtures[0].enabled=false;f.render();});
  assert.equal(await summary.locator('[data-alert-id^="fixture-light:"],[data-alert-id^="fixture-geometry:"]').count(),0);await summary.getByText('暂无告警',{exact:true}).waitFor();
  results.push('a clear optical result preserves the separate stale-geometry warning and its existing fixture editor route; disabling all participating plates removes optical warnings');
  const canvas=await page.locator('canvas').boundingBox();await page.mouse.click(canvas.x+50,canvas.y+canvas.height-70);assert.equal(await page.evaluate(()=>__overview.canvasCalls),1);
  await page.screenshot({path:resolve(output,'overview-resolved.png')});assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,results,errors},null,2));
  console.log(JSON.stringify({passed:true,output,results},null,2));
}catch(error){
  await page?.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,results,error:error.stack||error.message,errors},null,2));throw error;
}finally{await page?.evaluate(()=>window.__overview?.dispose()).catch(()=>{});await browser.close();}
