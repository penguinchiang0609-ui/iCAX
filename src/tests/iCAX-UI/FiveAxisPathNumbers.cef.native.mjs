import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {verifyPathNumbers} from './FiveAxisPathNumbers.native.browser.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/path-numbers-20261028/cef');mkdirSync(output,{recursive:true});
const project=resolve(output,'wall-lamp-working.i5cam');copyFileSync(resolve(process.env.ICAX_PATH_NUMBER_PROJECT||'output/tests/path-numbers-20261028/native-browser/wall-lamp-numbers.i5cam'),project);
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9618');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));assert.ok(page);page.setDefaultTimeout(120000);
const errors=[];page.on('pageerror',e=>errors.push(e.message));
try{
  await page.reload();
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.evaluate(async()=>{
    const base=new URL('../../../',location.href).href,store=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
    window.fixture={entry:{handleRibbonCommand:(_context,id)=>__icaxAppShell.executeRibbonCommand(id)},context:{actions:{selectRibbonTab:id=>__icaxAppShell.selectRibbonTab(id)}}};
    Object.defineProperty(fixture,'view',{get:()=>store.getProjectView(__icaxAppShell.getState().activeProjectId)});
  });
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState().objectCount>0);
  const rpc=async request=>{
    if(request.action==='invoke')return page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload),request);
    if(request.action==='export-project'){await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));copyFileSync(project,request.path);return {};}
    throw Error('Unsupported acceptance action '+request.action);
  };
  await verifyPathNumbers({page,rpc,output,standaloneCef:true});
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'standalone-report.json'),JSON.stringify({passed:true,standaloneCef:true,assetRoot:await page.evaluate(()=>new URL('../../../',location.href).href),errors},null,2));
  console.log('PASS standalone CEF: native wall lamp paths, transform numbers, toggle, refresh and page switch');
}catch(error){await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{await browser.close();}
