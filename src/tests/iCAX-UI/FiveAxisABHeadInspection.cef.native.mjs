import {chromium} from 'playwright';
const browser=await chromium.connectOverCDP('http://127.0.0.1:9595');
try{
 const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));
 const facts=await page.evaluate(async()=>{
  const {getProjectView}=await import(new URL('../../../',location.href).href+'apps/_shared/workbench/state/projectViewStore.mjs');
  const v=getProjectView(__icaxAppShell.getState().activeProjectId),head=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId)?.headModel;
  const bounds=o=>{o.geometry.computeBoundingBox();return {min:o.geometry.boundingBox.min.toArray(),max:o.geometry.boundingBox.max.toArray(),vertices:o.geometry.attributes.position.count};};
  return {project:__icaxAppShell.getState().activeProjectPath,head:{name:head?.name,source:head?.collisionSource,id:head?.builtinDefinitionId},
   assembly:head?.assembly&&{kind:head.assembly.kind,tool:head.assembly.tool,tcp:head.assembly.tcp,nodes:head.assembly.nodes.map(n=>({name:n.name,joint:n.joint,type:n.type,axis:n.axis,origin:n.origin,parent:n.parent,previewRef:n.previewRef}))},
   preview:v.fiveHeadPreview&&{phase:v.fiveHeadPreview.phase,model:v.fiveHeadPreview.model?.children.map(o=>({name:o.name,...bounds(o),matrix:o.matrix.toArray()}))}};
 });console.log(JSON.stringify(facts,null,2));
}finally{await browser.close();}
