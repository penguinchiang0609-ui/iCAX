import {chromium} from 'playwright';
for(const port of [9688,9689]){
 let browser;
 try{browser=await chromium.connectOverCDP('http://127.0.0.1:'+port);const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
  console.log(JSON.stringify({port,data:await page.evaluate(async()=>{const a=__icaxAppShell.getState(),{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs'),v=getProjectView(a.activeProjectId),d=v?.scene?.domain,s=d?.setups?.find(s=>s.id===d.activeSetupId);return {app:a.activeProjectId,project:a.openProjects,appKeys:Object.keys(a),fixtureBounds:d?.fixtures?.map(f=>{const o=v.viewport.sceneObjects.get(f.id),b=o?.geometry?.boundingBox;return {id:f.id,min:b?.min.toArray(),max:b?.max.toArray()};}),pending:v?.pending,checking:v?.fiveSimulation?.checking,pointDirty:v?.fiveAxisPointEdit?.dirty,head:s?.headModel?.instance,fixtures:d?.fixtures,placements:d?.placements,workpieces:d?.workpieces,cutNames:v?.scene?.intentToolpaths?.map(c=>({id:c.id,number:c.number,name:c.name})),report:s?.simulationCheck?{scope:s.simulationCheck.scope,status:s.simulationCheck.status,frames:s.simulationCheck.frames?.length}:null};})}));
 }catch(e){console.log(JSON.stringify({port,error:e.message}));}finally{await browser?.close();}
}
