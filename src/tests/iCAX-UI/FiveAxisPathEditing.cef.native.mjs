import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {verifyPathEditing} from './FiveAxisPathEditing.native.browser.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/path-edit-cef');mkdirSync(output,{recursive:true});
const workingFile=resolve(output,'刀路编辑验收.i5cam');copyFileSync(process.env.ICAX_PATH_EDIT_PROJECT||'output/tests/fixtures/path-edit-fixture.i5cam',workingFile);
const assetBase=pathToFileURL(resolve('src')+'/').href;
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9483');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/iCAX-UI/SDK/AppShell/index.html'));assert.ok(page,'Standalone CAM AppShell');page.setDefaultTimeout(180000);
const errors=[];page.on('pageerror',e=>errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view?.pending&&!!fixture.view?.scene?.domain);
try{
 await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),workingFile);
 await page.evaluate(async base=>{const store=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={entry:{handleRibbonCommand:(_context,id)=>__icaxAppShell.executeRibbonCommand(id),handleRibbonInput:async(_context,id,value)=>{const entry=await import(base+'apps/five-axis-cam/webpage/entry.mjs');return entry.handleRibbonInput({project:{projectId:__icaxAppShell.getState().activeProjectId}},id,value);}},context:{actions:{selectRibbonTab:id=>__icaxAppShell.selectRibbonTab(id)}}};Object.defineProperty(fixture,'view',{get:()=>store.getProjectView(__icaxAppShell.getState().activeProjectId)});},assetBase);
 await idle();
 const rpc=async request=>{
  if(request.action==='read-state')return page.evaluate(async base=>{const {readMachiningState}=await import(base+'apps/five-axis-cam/webpage/domainActions.mjs');return readMachiningState({sceneProxy:fixture.view.viewportSceneProxy});},assetBase);
  if(request.action==='invoke')return page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:180000}),request);
  if(request.action==='export-project'){await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));copyFileSync(workingFile,request.path);return {};}
  if(request.action==='save-reopen'){await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));const closing=page.evaluate(()=>__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId));await page.getByRole('button',{name:'保存并关闭',exact:true}).click();await closing;await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),workingFile);await idle();return {};}
  throw Error('Unsupported acceptance action '+request.action);
 };
 await verifyPathEditing({page,rpc,output,assetBase,standaloneCef:true});
 assert.deepEqual(errors,[]);await page.screenshot({path:resolve(output,'standalone-cam-accepted.png')});
 console.log('PASS standalone CEF, native CAM parameter editing, mouse split/merge, transforms, CAD isolation and save/reopen');
}catch(error){await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});writeFileSync(resolve(output,'failure.json'),JSON.stringify({error:error.stack,errors,state:await page.evaluate(()=>({shell:__icaxAppShell.getState(),text:document.body.innerText,error:fixture.view?.error})).catch(()=>null)},null,2));throw error;}
finally{await browser.close();}
