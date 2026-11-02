import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import {readFileSync} from 'node:fs';
import {createRequire} from 'node:module';

test('generic floating editor crosses both panes and retains project interaction through refresh, resize and release',async()=>{
  const require=createRequire(import.meta.url),{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
  const server=http.createServer((request,response)=>{
    const path=new URL(request.url,'http://localhost').pathname;
    try{
      if(!path.startsWith('/src/'))throw Error('Unsupported test resource');
      const resource=new URL('../../'+path.slice(5),import.meta.url);
      response.writeHead(200,{'Content-Type':path.endsWith('.css')?'text/css':'text/javascript'});response.end(readFileSync(resource));
    }catch{response.writeHead(404);response.end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
  try{
    const page=await browser.newPage({viewport:{width:1440,height:900}}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    const origin=`http://127.0.0.1:${server.address().port}`;
    await page.route(origin+'/',route=>route.fulfill({contentType:'text/html',body:`<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/src/apps/_shared/workbench/styles/workbench.css"><link rel="stylesheet" href="/src/apps/five-axis-cam/webpage/workspace.css"><style>
      html,body{margin:0;width:100%;height:100%;overflow:hidden}#mount{height:100%}.cam-workbench.five-workspace{display:block;position:relative;height:100%}.cam-context-pane,.cam-info-pane{position:absolute;top:120px;bottom:12px;overflow:auto!important;width:240px!important;height:auto!important;max-height:none!important}.cam-context-pane{left:0}.cam-info-pane{right:0;width:300px!important}.cam-viewport{position:absolute!important;left:240px;right:300px;top:190px;bottom:90px;overflow:hidden}.cam-viewport>canvas{width:100%;height:100%;display:block}.scroll-content{height:2000px}.nested{height:120px;overflow:auto}
      </style><div id="mount"><div class="cam-workbench five-workspace"><aside class="cam-context-pane"><div class="nested"><div class="scroll-content"></div></div><div class="scroll-content"></div></aside><div class="cam-viewport"><canvas></canvas><div class="five-overlay" data-five-overlay></div></div><aside class="cam-info-pane"><textarea data-five-key="right-input">retained command text</textarea><div class="scroll-content"></div></aside></div></div><button id="outside" style="position:fixed;left:16px;top:16px">Other page</button><script type="module">
      import {attachFloatingDialogs,syncFloatingDialogs,disposeFloatingDialogs,floatingDialogStyle} from '/src/apps/five-axis-cam/webpage/floatingDialogs.mjs';
      import {patchWorkspacePanels} from '/src/apps/five-axis-cam/webpage/workspaceDom.mjs';
      const mount=document.querySelector('#mount'),overlay=mount.querySelector('[data-five-overlay]');
      const view={fiveAxisArea:'toolpath',fiveDialogPositions:{}};
      const renderDialog=()=>'<section class="five-position-dialog five-floating-dialog" data-five-floating="generic-editor" data-five-key="generic-editor" role="dialog" aria-label="通用编辑器" aria-modal="false" '+floatingDialogStyle(view,'generic-editor')+'><header class="five-position-title" data-five-dialog-drag>通用编辑器</header><div class="five-position-body"><label>编辑值<input type="text" aria-label="编辑值" data-five-key="editor-value" value="0.1"></label><p>独立验证浮窗组件的拖动、交互保护和生命周期。</p></div></section>';
      let current=true;
      const context={activeRibbonTabId:'toolpath',isCurrentProject:()=>current},binding={view,context};
      overlay.innerHTML=renderDialog();attachFloatingDialogs(mount,binding);syncFloatingDialogs(mount,binding);
      window.fixture={view,binding,mount,refresh(){patchWorkspacePanels(mount,[['.cam-context-pane',mount.querySelector('.cam-context-pane').innerHTML],['.cam-info-pane',mount.querySelector('.cam-info-pane').innerHTML],['[data-five-overlay]',renderDialog()]]);syncFloatingDialogs(mount,binding);},setCurrent(value){current=value;syncFloatingDialogs(mount,binding);},cycleArea(){
        binding.area='toolpath';view.activeAreaId='other';syncFloatingDialogs(mount,binding);
        const other=document.createElement('section');document.body.append(other);
        const next={view:{activeAreaId:'other'},area:'other',context:{isCurrentProject:()=>true}};
        attachFloatingDialogs(other,next);syncFloatingDialogs(other,next);const previousReleased=binding.floatingDialogs===null;
        view.activeAreaId='toolpath';syncFloatingDialogs(mount,binding);const nextReleased=next.floatingDialogs===null;other.remove();
        return {previousReleased,nextReleased,reattached:!!binding.floatingDialogs};
      },release(){disposeFloatingDialogs(binding);mount.replaceChildren();},rebind(){
        const next={fiveDialogPositions:{}},nextBinding={view:next,context:{isCurrentProject:()=>true}};
        mount.innerHTML='<div class="five-workspace"><div class="five-position-dialog five-floating-dialog" data-five-floating="other" style="left:100px;top:100px;width:200px;height:150px"><header data-five-dialog-drag>Next project</header></div></div>';
        attachFloatingDialogs(mount,nextBinding);syncFloatingDialogs(mount,nextBinding);return nextBinding;
      }};
      document.querySelector('#outside').onclick=()=>window.outsideClicks=(window.outsideClicks||0)+1;
      document.querySelector('canvas').addEventListener('pointerdown',()=>window.canvasClicks=(window.canvasClicks||0)+1);
      </script>`}));
    await page.goto(origin+'/');await page.waitForFunction(()=>!!window.fixture);
    const dialog=page.getByRole('dialog',{name:'通用编辑器',exact:true}),title=dialog.locator('[data-five-dialog-drag]');
    assert.equal(await dialog.count(),1,await page.locator('[data-five-overlay]').innerHTML());
    const box=()=>dialog.boundingBox(),drag=async(dx,dy)=>{const r=await title.boundingBox();await page.mouse.move(r.x+45,r.y+16);await page.mouse.down();await page.mouse.move(r.x+45+dx,r.y+16+dy,{steps:8});await page.mouse.up();};
    assert.equal(await dialog.evaluate(n=>getComputedStyle(n).position),'fixed');
    const initial=await box(),scene=await page.locator('.cam-viewport').boundingBox();assert.ok(Math.abs(initial.x-scene.x-16)<1&&Math.abs(initial.y-scene.y-64)<1);
    await drag(-3000,0);assert.equal(Math.round((await box()).x),12);
    assert.equal(await title.evaluate(n=>{const r=n.getBoundingClientRect();return n.contains(document.elementFromPoint(r.x+45,r.y+16));}),true);
    await drag(3000,0);let right=await box();assert.equal(Math.round(right.x+right.width),1428);
    assert.equal(await title.evaluate(n=>{const r=n.getBoundingClientRect();return n.contains(document.elementFromPoint(r.x+45,r.y+16));}),true);
    await page.locator('#outside').click();assert.equal(await page.evaluate(()=>outsideClicks),1,'no backdrop intercepts normal controls');
    await page.mouse.click(scene.x+40,scene.y+scene.height-40);assert.equal(await page.evaluate(()=>canvasClicks),1,'the model canvas receives real pointer input outside the floating editor');
    // A field remains focused while a pending refresh settles after later
    // scrolling and selection changes, including the dialog's own input.
    const original=await page.evaluate(()=>{
      const input=document.querySelector('.cam-info-pane textarea'),dialog=document.querySelector('[data-five-floating]'),canvas=document.querySelector('canvas');
      window.retained={input,dialog,canvas,calls:0};input.addEventListener('retained-test',()=>retained.calls++);input.focus({preventScroll:true});input.setSelectionRange(1,4);
      window.settleRefresh=()=>fixture.refresh();
      return ['.cam-context-pane','.cam-info-pane','.nested'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=120+i*20;return n.scrollTop;});
    });
    await page.evaluate(()=>{retained.input.setSelectionRange(2,7);settleRefresh();});
    const preserved=await page.evaluate(()=>{const r=retained;r.input.dispatchEvent(new Event('retained-test'));return{input:document.querySelector('.cam-info-pane textarea')===r.input,dialog:document.querySelector('[data-five-floating]')===r.dialog,canvas:document.querySelector('canvas')===r.canvas,focus:document.activeElement===r.input,selection:[r.input.selectionStart,r.input.selectionEnd],calls:r.calls,scroll:['.cam-context-pane','.cam-info-pane','.nested'].map(s=>document.querySelector(s).scrollTop)};});
    assert.deepEqual(preserved,{input:true,dialog:true,canvas:true,focus:true,selection:[2,7],calls:1,scroll:original});assert.deepEqual(await box(),right);
    await page.getByRole('textbox',{name:'编辑值',exact:true}).fill('0.25');await page.evaluate(()=>{const n=document.querySelector('[data-five-key="editor-value"]');n.focus({preventScroll:true});n.setSelectionRange(1,3);fixture.refresh();});
    assert.equal(await page.getByRole('textbox',{name:'编辑值',exact:true}).inputValue(),'0.25');assert.deepEqual(await page.evaluate(()=>{const n=document.activeElement;return[n.dataset.fiveKey,n.selectionStart,n.selectionEnd];}),['editor-value',1,3]);
    for(const size of [{width:700,height:410},{width:520,height:320},{width:1280,height:800}]){
      await page.setViewportSize(size);await page.waitForFunction(()=>{const n=document.querySelector('[data-five-floating]'),r=n.getBoundingClientRect();return r.left>=11&&r.top>=11&&r.right<=innerWidth-11&&r.bottom<=innerHeight-11;});
      const r=await box();assert.ok(r.width<=size.width-24&&r.height<=size.height-24);
    }
    const beforeCycle=await box();assert.deepEqual(await page.evaluate(()=>fixture.cycleArea()),{previousReleased:true,nextReleased:true,reattached:true});assert.deepEqual(await box(),beforeCycle);
    const inactive=await page.evaluate(()=>{fixture.setCurrent(false);return JSON.stringify(fixture.view.fiveDialogPositions);});await page.setViewportSize({width:1000,height:700});await page.waitForTimeout(50);assert.equal(await page.evaluate(()=>JSON.stringify(fixture.view.fiveDialogPositions)),inactive);
    await page.evaluate(()=>{fixture.setCurrent(true);fixture.release();window.nextBinding=fixture.rebind();});
    assert.equal(await page.locator('[data-five-floating="generic-editor"]').count(),0);
    const oldPositions=await page.evaluate(()=>JSON.stringify(fixture.view.fiveDialogPositions)),next=page.locator('[data-five-floating="other"]'),nextTitle=next.locator('header'),r=await nextTitle.boundingBox();
    await page.mouse.move(r.x+40,r.y+8);await page.mouse.down();await page.mouse.move(r.x+150,r.y+38,{steps:5});await page.mouse.up();
    assert.equal(await page.evaluate(()=>JSON.stringify(fixture.view.fiveDialogPositions)),oldPositions,'released project listeners never handle the next project');
    assert.ok(Number(await next.evaluate(n=>n.style.left.replace('px','')))>100);
    assert.deepEqual(errors,[]);
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
});
