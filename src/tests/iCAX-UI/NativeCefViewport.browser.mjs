import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';
import {browserReportDirectory} from './browserPackageRuntime.mjs';

const output=browserReportDirectory('native-viewport-restore-20261101'),browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
const report={passed:false,cases:[],coverage:'real Chromium device-metrics override; separate from the user CEF window'};
try{
 const page=await browser.newPage({viewport:{width:1280,height:800}});
 await page.setContent('<!doctype html><input id="draft" value="keep input"><canvas id="canvas"></canvas>');
 const original=await restoreNativeCefViewport(page),frameInsets=original.outer.map((n,i)=>n-original.inner[i]);
 await page.evaluate(()=>{window.keptInput=document.querySelector('#draft');window.keptCanvas=document.querySelector('#canvas');keptInput.focus();keptInput.setSelectionRange(1,4);});
 for(const size of [{width:2200,height:1250},{width:1024,height:680},{width:3200,height:1800}]){
  await page.setViewportSize(size);
  assert.deepEqual(await page.evaluate(()=>[innerWidth,innerHeight]),[size.width,size.height]);
  await restoreNativeCefViewport(page);
  // Ordinary Chromium has a window frame; CEF is borderless. Compare with the
  // original native content area rather than the decorated outer dimensions.
  await page.waitForFunction(insets=>innerWidth===outerWidth-insets[0]&&innerHeight===outerHeight-insets[1],frameInsets,{timeout:3000});
  const restored=await page.evaluate(()=>({inner:[innerWidth,innerHeight],outer:[outerWidth,outerHeight],devicePixelRatio}));
  assert.deepEqual(restored.inner,restored.outer.map((n,i)=>n-frameInsets[i]),'Web contents must match the actual host content area');
  assert.deepEqual(await page.evaluate(()=>({input:keptInput===document.querySelector('#draft'),canvas:keptCanvas===document.querySelector('#canvas'),focus:document.activeElement===keptInput,selection:[keptInput.selectionStart,keptInput.selectionEnd]})),{input:true,canvas:true,focus:true,selection:[1,4]});
  report.cases.push({override:size,restored});
 }
 report.passed=true;
}finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
