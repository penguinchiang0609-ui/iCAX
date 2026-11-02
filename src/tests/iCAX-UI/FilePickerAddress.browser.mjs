import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {browserAssetPath,browserReportDirectory} from './browserPackageRuntime.mjs';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('file-picker-address');
const browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage({viewport:{width:1440,height:900}}),errors=[],cases=[];
page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(15000);
try{
 await page.route('**/*',route=>{const path=decodeURIComponent(new URL(route.request().url()).pathname);if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><style>body{margin:0;font:14px Segoe UI}.pane{height:120px;overflow:auto}.pane div{height:1000px}</style><body><div id="app"><aside class="pane" id="left"><div></div></aside><canvas></canvas><aside class="pane" id="right"><div></div></aside></div>'});try{return route.fulfill({contentType:path.endsWith('.css')?'text/css':'text/javascript',body:readFileSync(browserAssetPath(path.slice(5)))});}catch{return route.abort();}});
 await page.goto('http://localhost/');await page.evaluate(async()=>{
  const {chooseModelFile}=await import('/src/apps/five-axis-cam/webpage/workpieceFilePicker.mjs');localStorage.clear();
  const f=window.f={view:{scene:{marker:'unchanged'}},calls:[],root:'D:/CAD/工 件',hold:'',modified:'1000'};
  const folder=path=>({directory:true,path,name:path.split('/').at(-1)}),file=path=>({directory:false,path,name:path.split('/').at(-1),sizeBytes:100,lastModifiedNs:f.modified});
  f.result=directory=>({directory,parentDirectory:directory==='D:/'?'':directory.lastIndexOf('/')===2?directory.slice(0,3):directory.slice(0,directory.lastIndexOf('/')),entries:directory===f.root?[folder(directory+'/子目录'),...Array.from({length:28},(_,i)=>file(directory+`/part-${i}.${['step','stp','iges','igs'][i%4]}`))]:directory==='D:/CAD'?[folder(f.root)]:directory==='D:/'?[folder('D:/CAD')]:directory.startsWith('//')?[folder(directory+'/子目录'),file(directory+'/网络件.STEP')]:[file(directory+'/inside.step')]});
  f.context={mount:document.querySelector('#app'),isCurrentProject:()=>true,appProxy:{bridge:{async openDirectoryDialog(){return f.root;}}},sceneProxy:{async invoke(method,payload={}){
   f.calls.push({method,payload});if(method==='CamWorkpiece.ListFiles'){const directory=payload.directory||f.root;if(directory===f.hold)return new Promise(resolve=>f.finish=()=>{f.hold='';resolve(f.result(directory));});if(directory.includes('missing'))throw Error('目录不存在');return f.result(directory);}
   if(method==='CamWorkpiece.PreviewFile')return {epoch:'e',taskId:String(f.calls.length),status:'Completed',result:{sourcePath:payload.sourcePath,sizeBytes:100,lastModifiedNs:f.modified,wireframe:{polylines:[[0,0,0,10,0,0,10,10,0]],edgeCount:1,pointCount:3}}};
   if(method==='CamWorkpiece.ReleaseFilePreview')return {released:true};throw Error(method);
  }}};f.open=()=>{f.choice=chooseModelFile(f.context,f.view);};f.open();
 });
 const input=page.locator('[data-nesting-file-directory]'),bar=page.locator('.tube-directory-address'),format=page.getByRole('combobox',{name:'文件格式'}),list=page.locator('.tube-nesting-file-list');
 const at=path=>page.waitForFunction(path=>f.view.fiveAxisFilePicker?.directory===path&&!document.querySelector('.tube-nesting-file-list').hasAttribute('aria-busy'),path);
 const ready=()=>page.waitForFunction(()=>f.view.fiveAxisFilePicker?.previewStatus==='ready');const go=async path=>{await page.keyboard.press('Control+l');await input.fill(path);await input.press('Enter');await at(path);};
 await at('D:/CAD/工 件');assert.equal(await page.locator('[data-nesting-file-action="up"]').count(),0);assert.deepEqual(await page.locator('[data-nesting-file-directory-target]').allTextContents(),['D:','CAD','工 件']);assert.equal(await bar.evaluate(n=>n.classList.contains('is-editing')),false);
 await page.locator('[data-nesting-file-directory-target="D:/CAD"]').click();await at('D:/CAD');await page.keyboard.press('Alt+ArrowUp');await at('D:/');await go('D:/CAD/工 件');
 await bar.click({position:{x:700,y:18}});assert.equal(await input.evaluate(n=>document.activeElement===n),true);await input.fill('D:/draft');await input.press('Escape');assert.equal(await input.inputValue(),'D:/CAD/工 件');assert.equal(await page.locator('.tube-nesting-file-picker').count(),1);
 await page.keyboard.press('Alt+d');assert.deepEqual(await input.evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,'D:/CAD/工 件'.length]);await input.press('Escape');
 cases.push('Drive and Chinese/space breadcrumbs navigate; Up is removed; Ctrl+L/Alt+D, blank-area editing, Enter and Escape behave as an address bar');
 await go('//server/share/产品');assert.deepEqual(await page.locator('[data-nesting-file-directory-target]').allTextContents(),['\\\\server\\share','产品']);await page.locator('[data-nesting-file-directory-target="//server/share/"]').click();await at('//server/share/');await go('D:/CAD/工 件');
 cases.push('UNC share roots remain valid clickable paths without truncating the server or share');
 await page.locator('[data-nesting-file-path="D:/CAD/工 件/part-2.iges"]').click();await ready();const before=await page.evaluate(()=>{f.input=document.querySelector('[data-nesting-file-directory]');f.svg=document.querySelector('[data-nesting-wireframe]');f.canvas=document.querySelector('canvas');return f.calls.length;});
 await format.selectOption('step');assert.equal(await page.locator('[data-nesting-file-is-directory="false"]:visible').count(),14);assert.equal(await page.locator('[data-nesting-file-action="open"]').isDisabled(),true);assert.equal(await page.locator('[data-nesting-file-is-directory="true"]:visible').count(),1);assert.equal(await page.evaluate(()=>f.calls.length),before);
 await format.selectOption('iges');assert.equal(await page.locator('[data-nesting-file-is-directory="false"]:visible').count(),14);await format.selectOption('all');assert.equal(await page.locator('[data-nesting-file-is-directory="false"]:visible').count(),28);
 assert.equal(await page.evaluate(()=>f.input===document.querySelector('[data-nesting-file-directory]')&&f.canvas===document.querySelector('canvas')),true);
 cases.push('Extension filters keep folders, clear hidden selections and disable Open; changing filters performs no directory or model read and preserves existing controls');
 await page.locator('[data-nesting-file-path="D:/CAD/工 件/part-0.step"]').click();await ready();const previewCalls=await page.evaluate(()=>f.calls.filter(c=>c.method==='CamWorkpiece.PreviewFile').length);await page.locator('footer [data-nesting-file-action="cancel"]').click();await page.evaluate(()=>f.open());await ready();assert.equal(await page.evaluate(()=>f.calls.filter(c=>c.method==='CamWorkpiece.PreviewFile').length),previewCalls+1);assert.equal(await page.evaluate(()=>Object.hasOwn(f.view.fiveAxisFilePicker,'previewCache')),false);
 cases.push('Reopening asks the native disk-cache path for preview again; no historical in-memory preview cache exists');
 await page.keyboard.press('Control+l');await input.fill('D:/CAD');await page.evaluate(()=>f.hold='D:/CAD');await input.press('Enter');await page.waitForFunction(()=>!!f.finish);
 await input.fill('D:/继续输入');const positions=await page.evaluate(()=>{const input=document.querySelector('[data-nesting-file-directory]');input.focus({preventScroll:true});input.setSelectionRange(3,6,'backward');document.querySelector('#left').scrollTop=80;document.querySelector('#right').scrollTop=90;return [80,90];});await page.evaluate(()=>f.finish());await at('D:/CAD');
 assert.deepEqual(await input.evaluate(n=>[n.value,document.activeElement===n,n.selectionStart,n.selectionEnd,n.selectionDirection]),['D:/继续输入',true,3,6,'backward']);assert.deepEqual(await page.evaluate(()=>[document.querySelector('#left').scrollTop,document.querySelector('#right').scrollTop]),positions);assert.equal(await bar.evaluate(n=>n.classList.contains('is-editing')),true);
 cases.push('A delayed directory response retains newer text, focus, selection and both background pane scroll positions');
 await input.press('Escape');await go('D:/CAD/工 件');await page.locator('[data-nesting-file-path="D:/CAD/工 件/part-0.step"]').click();await ready();await page.screenshot({path:resolve(output,'address-and-filter.png')});
 assert.deepEqual(errors,[]);assert.equal(await page.evaluate(()=>f.view.scene.marker),'unchanged');writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,errors},null,2));console.log(`Address/filter browser passed (${cases.length} cases)`);
}catch(error){writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,cases,error:error.stack,errors},null,2));await page.screenshot({path:resolve(output,'failure.png')});throw error;}finally{await browser.close();}
