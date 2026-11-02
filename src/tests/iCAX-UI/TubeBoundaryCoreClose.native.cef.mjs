// Preserve and close only the boundary-core acceptance project before a DLL update.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {existsSync,mkdirSync,writeFileSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const output=resolve(process.env.ICAX_BOUNDARY_EVIDENCE||'output/tests/tube-boundary-core-20261030/runtime-acceptance');
const expected=join(output,'tube-boundary-core-runtime.ictd');
const browser=await require('playwright').chromium.connectOverCDP(process.env.ICAX_BOUNDARY_CDP||'http://127.0.0.1:9223');
const page=browser.contexts().flatMap(context=>context.pages()).find(p=>p.url().includes('/AppShell/index.html'));assert(page);
const report={passed:false,expectedProject:expected,otherProjectsUntouched:true};mkdirSync(output,{recursive:true});
try{
  report.before=await page.evaluate(()=>__icaxAppShell.getState());
  assert.equal(report.before.activeProjectPath.replaceAll('\\','/').toLowerCase(),expected.replaceAll('\\','/').toLowerCase(),
    'Refuse to save or close a project outside this acceptance run');
  const closingId=await page.evaluate(async path=>{
    const f=window.__boundaryAcceptance;
    if(!f?.project)throw new Error('This window does not own the acceptance project proxy');
    const id=__icaxAppShell.getState().activeProjectId;
    const saved=await f.project.save(path);
    if(saved?.saved!==true)throw new Error('Acceptance project was not saved');
    f.closePromise=__icaxAppShell.closeProject(id);return id;
  },expected);
  assert(existsSync(expected),'Native save created the acceptance project file');
  await page.locator(`[data-project-close-dialog="${closingId}"] [data-project-close-choice="discard"]`).click();
  await page.evaluate(()=>window.__boundaryAcceptance.closePromise);
  report.result={saved:true,closedProjectId:closingId,state:await page.evaluate(()=>__icaxAppShell.getState())};
  report.passed=true;report.readyForExactNativeWindowClose=true;
}catch(error){report.error=error.stack;throw error;}
finally{writeFileSync(join(output,'normal-project-close.json'),JSON.stringify(report,null,2));await browser.close();}
