// Keep the cloned cut/rapid projects (same native ID) out of the shell together.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{execFileSync}=require('node:child_process');
const{chromium}=require('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
(async()=>{
 const output=path.resolve('output/tests/cut-edit-audit-20261023'),cutFile=path.join(output,'cut-audit.i5cam').replaceAll('\\','/');
 const b=await chromium.connectOverCDP('http://127.0.0.1:9483'),p=b.contexts()[0].pages()[0];p.setDefaultTimeout(30000);
 // A fixed CEF viewport lets the user minimize the native audit window while
 // these supplementary page controls run, without shrinking it to 12x12.
 await p.setViewportSize({width:1600,height:950});
 const close=async id=>{const tab=p.locator(`[data-action="close-open-project"][data-project-id="${id}"]`);if(!await tab.count())return;await tab.click();await p.locator('[data-project-close-choice="save"]').click();await tab.waitFor({state:'detached'});};
 const cutId=JSON.parse(fs.readFileSync(path.join(output,'saved-expected.json'),'utf8')).business.cuts[0].curveRef.url.split('/projects/')[1].split('/')[0];
 await close(cutId);
 execFileSync(process.execPath,['src/tests/iCAX-UI/FiveAxisSharedClose.standalone.cjs'],{stdio:'inherit'});
 await close(cutId);
 const old=path.resolve('output/tests/full-edit-audit-20261023');
 const machine=JSON.parse(fs.readFileSync(path.join(old,'lifecycle-report.json'),'utf8')),rapid=JSON.parse(fs.readFileSync(path.join(old,'shared-close-rapid-report.json'),'utf8'));
 assert.equal(machine.passed,true);assert.equal(rapid.passed,true);
 await p.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),cutFile);
 await p.waitForFunction(async file=>{if(__icaxAppShell.getState().activeProjectPath!==file)return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');return getProjectView(__icaxAppShell.getState().activeProjectId)?.scene?.intentToolpaths?.length===4;},cutFile);
 fs.writeFileSync(path.join(output,'related-page-close-report.json'),JSON.stringify({passed:true,standaloneCef:true,viewport:{width:1600,height:950},machine,rapid,returnedToSavedCutProject:true},null,2));
 console.log('PASS machine and rapid page save/close/reopen after cutting changes');process.exit(0);
})().catch(e=>{console.error(e);process.exit(1)});
