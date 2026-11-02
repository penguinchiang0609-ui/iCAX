import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve,sep} from 'node:path';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const root=resolve('src'),output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/customer-feedback-closure-20261023/focus-model');mkdirSync(output,{recursive:true});
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage({viewport:{width:1100,height:750}});
 await page.route('http://pane-model.test/**',route=>{const url=new URL(route.request().url());if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><html><body></body></html>'});const path=resolve(root,decodeURIComponent(url.pathname).slice(1));if(!path.startsWith(root+sep))return route.abort();const selected=url.pathname.endsWith('/paneInteractionState.mjs')&&process.env.ICAX_PANE_INTERACTION_SOURCE?process.env.ICAX_PANE_INTERACTION_SOURCE:path;return route.fulfill({contentType:'text/javascript',body:readFileSync(selected,'utf8')});});
 await page.goto('http://pane-model.test/');
 const results=await page.evaluate(async()=>{
  const {capturePaneInteraction}=await import('/apps/_shared/workbench/utils/paneInteractionState.mjs');
  const {patchDomNode}=await import('/apps/tube-designer/webpage/punchDomPatch.mjs');
  document.body.innerHTML='<main id="mount"><aside class="cam-context-pane" style="height:150px;overflow:auto"><div style="height:900px"></div></aside><aside class="cam-info-pane" style="height:250px;overflow:auto"><section data-pane-interaction-scope="product:a"><div id="scroll" style="height:180px;overflow:auto"><label><input type="text" data-product-parameter-key="code" value="原值"></label><div style="height:1200px"></div></div></section><div style="height:900px"></div></aside></main>';
  const mount=document.querySelector('#mount'),input=()=>mount.querySelector('input'),nodes=[mount.querySelector('.cam-context-pane'),mount.querySelector('.cam-info-pane'),mount.querySelector('#scroll')];
  const setScroll=()=>nodes.forEach((n,i)=>n.scrollTop=[120,90,140][i]);
  const update=(value,scope='product:a',replace=false)=>{const old=mount.querySelector('[data-pane-interaction-scope]');const next=old.cloneNode(true);next.dataset.paneInteractionScope=scope;next.querySelector('input').setAttribute('value',value);next.querySelector('input').value=value;if(replace)old.replaceWith(next);else patchDomNode(old,next);};
  const check=(condition,message)=>{if(!condition)throw new Error(message);};
  let calls=0;input().addEventListener('probe',()=>calls++);
  input().focus();input().value='异步响应前继续输入';input().setSelectionRange(2,6,'backward');setScroll();
  const restoreDraft=capturePaneInteraction(mount);update('原值');restoreDraft();
  check(input().value==='异步响应前继续输入','Latest unsubmitted input must survive unchanged model');check(input().selectionStart===2&&input().selectionEnd===6,'Selection lost');check(document.activeElement===input(),'Focus lost');check(nodes.every((n,i)=>n.scrollTop===[120,90,140][i]),'Scroll lost');input().dispatchEvent(new Event('probe'));check(calls===1,'Listener lost');
  const results=[{name:'latest draft selection scroll and listener survive unchanged response',passed:true}];
  input().value=input().defaultValue;
  const original=input();const restorePreset=capturePaneInteraction(mount);update('方案新值');restorePreset();
  check(input()===original,'Keyed patch replaced input');check(input().value==='方案新值','Focused stale text overwrote authoritative preset value');check(document.activeElement===input(),'Authoritative update lost focus');results.push({name:'authoritative model change updates retained focused input',passed:true});
  input().value='更晚输入';input().setSelectionRange(1,3);const restoreAcknowledgement=capturePaneInteraction(mount);update('较早提交确认');restoreAcknowledgement();check(input().value==='更晚输入'&&input().selectionStart===1&&input().selectionEnd===3,'Earlier acknowledgement overwrote later typing');results.push({name:'earlier edit acknowledgement keeps later unsubmitted typing',passed:true});
  input().value='切换前草稿';input().setSelectionRange(1,4);const restoreOther=capturePaneInteraction(mount);update('新实例值','product:b',true);restoreOther();
  check(input().value==='新实例值','Other product received previous draft');check(document.activeElement!==input(),'Other product stole previous focus');results.push({name:'product navigation cannot transfer draft or focus',passed:true});
  input().focus();input().value='待替换草稿';input().setSelectionRange(1,3,'forward');const restoreReplacement=capturePaneInteraction(mount);update('新实例值','product:b',true);restoreReplacement();check(input().value==='待替换草稿','Necessary same-scope replacement lost latest text');check(document.activeElement===input()&&input().selectionStart===1&&input().selectionEnd===3,'Replacement lost focus or selection');results.push({name:'necessary same-scope replacement keeps unsubmitted text and selection',passed:true});return results;
 });
 writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,results},null,2));console.log(JSON.stringify(results));
}finally{await browser.close();}
