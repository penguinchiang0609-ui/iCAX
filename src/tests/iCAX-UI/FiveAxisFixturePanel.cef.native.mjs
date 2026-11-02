import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
const output=resolve('output/tests/cam-sdo-errors-20261026');mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:9586');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
const report={passed:false,standaloneCef:true,realNativeRefresh:true};
try{
 assert.equal(await page.locator('[data-five-key="design-group-cut-clearance"]').count(),0);
 assert.equal(await page.locator('[data-five-fixture-field="design:cutClearanceMode"]').count(),0);
 await page.evaluate(()=>{
  const input=document.querySelector('[data-five-fixture-field="design:spacing"]');if(!input)throw Error('Fixture spacing editor required');
  const canvas=document.querySelector('.cam-render-viewport canvas');
  const panes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane,.five-design-body')];
  const old=document.activeElement;
  window.fixturePanelCheck={input,canvas,panes,old,selection:[old?.selectionStart,old?.selectionEnd,old?.selectionDirection],scroll:panes.map(n=>[n.scrollTop,n.scrollLeft]),userActivity:false};
  fixturePanelCheck.activity=e=>{if(e.isTrusted)fixturePanelCheck.userActivity=true;};
  for(const type of ['wheel','keydown','pointerdown'])window.addEventListener(type,fixturePanelCheck.activity,true);
  input.focus({preventScroll:true});input.setSelectionRange(0,Math.min(2,input.value.length));
 });
 const refreshing=page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));
 await page.evaluate(()=>{
  const state=fixturePanelCheck;
  for(const n of state.panes)n.scrollTop=Math.max(0,Math.floor((n.scrollHeight-n.clientHeight)*.7));
  state.latestScroll=state.panes.map(n=>[n.scrollTop,n.scrollLeft]);
 });
 await refreshing;
 report.refreshed=await page.evaluate(()=>{
  const s=fixturePanelCheck;return {sameCanvas:document.querySelector('.cam-render-viewport canvas')===s.canvas,
   sameInput:document.querySelector('[data-five-fixture-field="design:spacing"]')===s.input,
   focused:document.activeElement===s.input,selection:[s.input.selectionStart,s.input.selectionEnd],
   scroll:s.panes.map(n=>[n.scrollTop,n.scrollLeft]),expectedScroll:s.latestScroll,
   removedGroup:!document.querySelector('[data-five-key="design-group-cut-clearance"]')};
 });
 for(const key of ['sameCanvas','sameInput','focused','removedGroup'])assert.equal(report.refreshed[key],true,key);
 assert.deepEqual(report.refreshed.selection,[0,2]);assert.deepEqual(report.refreshed.scroll,report.refreshed.expectedScroll);
 await page.screenshot({path:resolve(output,'fixture-panel-removed.png')});report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;}
finally{
 await page.evaluate(()=>{
  const s=window.fixturePanelCheck;if(!s)return;
  if(!s.userActivity){if(s.old?.isConnected){s.old.focus({preventScroll:true});if(Number.isInteger(s.selection[0]))s.old.setSelectionRange?.(...s.selection);}s.panes.forEach((n,i)=>{n.scrollTop=s.scroll[i][0];n.scrollLeft=s.scroll[i][1];});}
  for(const type of ['wheel','keydown','pointerdown'])window.removeEventListener(type,s.activity,true);
 }).catch(()=>{});
 writeFileSync(resolve(output,'fixture-panel-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();
}
