import {createRequire} from 'node:module';
import {copyFileSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {verifyExportInteraction} from './FiveAxisCutPointExport.interactions.mjs';

const {chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/cutpoint-export-20261026/standalone');
const project=resolve(output,'focus-baseline-'+Date.now()+'.i5cam');
copyFileSync(resolve(output,'two-setup-baseline.i5cam'),project);
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9548');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
page.setDefaultTimeout(120000);
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest&&!!fixture.view.scene?.domain,null,{timeout:180000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
try{
  await page.evaluate(()=>{window.focusProjectClose=__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId);});
  await page.locator('[data-project-close-choice="discard"]').click();
  await page.evaluate(()=>focusProjectClose);
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(path=>__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/').toLowerCase()===path.replaceAll('\\','/').toLowerCase()&&__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId,project);
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};});await idle();
  await command('five.area.toolpath');
  const destination=resolve(output,'异步焦点回归.cl');
  await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},destination);
  const interaction=await verifyExportInteraction({page,command,idle});
  const reportPath=resolve(output,'report.json'),report=JSON.parse(readFileSync(reportPath,'utf8'));
  report.interaction=interaction;
  report.cases=report.cases.map(c=>c.startsWith('Real asynchronous export receipt')?'Real asynchronous export receipt retains the canvas, setup-name input/listener/focus/selection and latest left/right/nested scroll positions':c);
  writeFileSync(reportPath,JSON.stringify(report,null,2));
  writeFileSync(resolve(output,'focus-report.json'),JSON.stringify({passed:true,standaloneCef:true,interaction},null,2));
  console.log(JSON.stringify({passed:true,interaction}));
}finally{await browser.close();}
