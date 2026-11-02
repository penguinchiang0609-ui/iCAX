import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=process.env.ICAX_CONTINUOUS_EVIDENCE;
const fixture=JSON.parse(readFileSync(join(root,'default-continuous-row-report.json'),'utf8'));
const member=fixture.model.members.find(m=>m.stableKey==='access_door.fixed_frame.top.0001');assert(member);
const browser=await chromium.connectOverCDP('http://127.0.0.1:19459');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
const report={nativeBusinessMocked:false,stableKey:member.stableKey,entityId:member.entityId,clicks:[]};
try{
 await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();});
 const toggle=page.locator('.tube-designer-scene-specification-tree input[type="checkbox"]').first();if(await toggle.count())await toggle.uncheck();
 const points=await page.evaluate(async id=>{
  const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
  const viewport=getProjectView(window.__icaxWorkbench.getState().projectId).viewport;
  const object=viewport.sceneObjects.get(id);if(!object)throw Error('Member has no own display object: '+id);
  object.geometry.computeBoundingBox();const bounds=object.geometry.boundingBox;
  const size=bounds.getSize(viewport.camera.position.clone()),axis=['x','y','z'].sort((a,b)=>size[b]-size[a])[0];
  const rect=viewport.renderer.domElement.getBoundingClientRect();
  return [.08,.5,.92].map(t=>{
   const local=bounds.getCenter(viewport.camera.position.clone());local[axis]=bounds.min[axis]+t*size[axis];
   const q=object.localToWorld(local).project(viewport.camera);
   return {t,x:rect.x+(q.x+1)*rect.width/2,y:rect.y+(1-q.y)*rect.height/2};
  });
 },member.entityId);
 for(const point of points){
  await page.mouse.click(point.x,point.y);
  await page.waitForFunction(async id=>{
   const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
   return getProjectView(window.__icaxWorkbench.getState().projectId).viewport.selectedObjectId===id;
  },member.entityId,{timeout:10000});
  report.clicks.push({...point,selectedEntityId:member.entityId});
 }
 await page.locator('.cam-viewport').screenshot({path:join(root,'one-continuous-row-selected.png')});
 report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;}
finally{writeFileSync(join(root,'continuous-selection-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
