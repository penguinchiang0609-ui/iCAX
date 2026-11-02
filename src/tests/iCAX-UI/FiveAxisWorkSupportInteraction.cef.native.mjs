import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
const out=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/work-support-20261101/interactions');mkdirSync(out,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9602'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));
const report={passed:false,standaloneCef:true,checks:[]};
try{
 const command=id=>page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);
 await page.evaluate(async()=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.wv=getProjectView(__icaxAppShell.getState().activeProjectId);});
 await command('five.cam-page.cutting');
 await page.locator('[data-five-intent]').first().click();await command('five.cam-inspector.axis');await page.locator('[aria-label="刀轴 欧拉角 X"]').waitFor();
 await page.evaluate(()=>{document.querySelector('#support-interaction-test')?.remove();const s=document.createElement('style');s.id='support-interaction-test';s.textContent='.cam-context-pane,.cam-info-pane{max-height:160px!important;overflow:auto!important}.cam-context-pane>*,.cam-info-pane>*{min-height:230px!important;flex-shrink:0!important}.cam-context-pane [role="tree"],.cam-info-pane section{max-height:40px!important;overflow:auto!important}';document.head.append(s);});
 for(const selector of ['[aria-label="刀轴 欧拉角 X"]','[aria-label="查找刀路"]']){
  if(selector.includes('查找'))await page.locator(selector).fill('圆');
  await page.evaluate(selector=>{
   const input=document.querySelector(selector),panels=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane')],nested=[...document.querySelectorAll('.cam-context-pane [role="tree"],.cam-info-pane section')].filter(n=>n.scrollHeight>n.clientHeight);
   window.retained={input,canvas:wv.viewport.renderer.domElement,panels,nested,events:0,event:'support-check-'+crypto.randomUUID()};const record=retained;record.listener=()=>record.events++;input.addEventListener(record.event,record.listener);input.focus({preventScroll:true});input.setSelectionRange(0,1);
   const apply=wv.viewport.applyViewSnapshot;retained.apply=apply;wv.viewport.applyViewSnapshot=async function(...args){retained.arrived=true;await new Promise(resolve=>setTimeout(resolve,180));return apply.apply(this,args);};
  },selector);
  const switching=command('five.tool.simulation');
  await page.waitForFunction(()=>retained.arrived);
  await page.evaluate(()=>{retained.panels.forEach(n=>n.scrollTop=38);retained.nested.forEach(n=>n.scrollTop=9);retained.scroll=[...retained.panels,...retained.nested].map(n=>n.scrollTop);});
  await switching;await page.waitForFunction(()=>wv.fiveViewportFrame==='cam-head');await page.waitForTimeout(250);
  const actual=await page.evaluate(selector=>{
   const r=retained;r.input.dispatchEvent(new Event(r.event));r.input.removeEventListener(r.event,r.listener);wv.viewport.applyViewSnapshot=r.apply;
   return {sameInput:r.input===document.querySelector(selector),sameCanvas:r.canvas===wv.viewport.renderer.domElement,focus:document.activeElement===r.input,selection:[r.input.selectionStart,r.input.selectionEnd],scroll:r.scroll,after:[...r.panels,...r.nested].map(n=>n.scrollTop),events:r.events,nested:r.nested.length};
  },selector);
  report.checks.push({selector,...actual});for(const key of ['sameInput','sameCanvas','focus'])assert.equal(actual[key],true,key);assert.deepEqual(actual.selection,[0,1]);assert.deepEqual(actual.after,actual.scroll);assert.ok(actual.scroll[0]>0&&actual.scroll[1]>0);assert.equal(actual.events,1);assert.ok(actual.nested>0);
  await command('five.cam-page.cutting');await page.waitForFunction(()=>wv.fiveViewportFrame==='setup');
 }
 await page.locator('[aria-label="查找刀路"]').fill('');await page.evaluate(()=>document.querySelector('#support-interaction-test').remove());
 await command('five.tool.simulation');await page.waitForFunction(()=>wv.fiveMachine?.report?.playbackAllowed);
 await command('five.simulation.play');await page.waitForFunction(()=>wv.fiveMachine.playing&&wv.fiveMachine.cursor>.1);await command('five.simulation.pause');
 assert.equal(await page.evaluate(()=>wv.fiveMachine.playing),false);assert.equal(await page.evaluate(()=>!!wv.fiveSimulation?.playing),false);report.actualPlayback=true;report.passed=true;
}catch(error){report.error=error.stack;await page.screenshot({path:resolve(out,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
