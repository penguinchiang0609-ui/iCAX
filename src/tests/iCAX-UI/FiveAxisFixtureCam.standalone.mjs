import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {verifyFixtureCam} from './FiveAxisFixtureCam.browserCase.mjs';

const {chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/fixture-cam-presentation-20261026/standalone');
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9546');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
page.setDefaultTimeout(45000);
const errors=[],report={passed:false,cases:[],coverage:{native:true,standaloneCef:true}};
page.on('pageerror',e=>errors.push(e.message));
try{
  await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready'&&!__icaxAppShell.getState().pendingCount);
  await page.evaluate(async()=>{
    const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
    window.fixture={moduleBase:new URL('../../../',location.href).href,view:getProjectView(__icaxAppShell.getState().activeProjectId),context:{actions:{selectRibbonTab:async id=>document.querySelector(`[data-tab-id="${id}"]`).click()}}};
  });
  const readState=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
  const before=await readState();
  await verifyFixtureCam({page,before,output,report,errors,readState});
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{report.errors=errors;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,cases:report.cases,error:report.error,errors}));await browser.close();}
