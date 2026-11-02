import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,unlinkSync,rmdirSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

export async function verifyHeadImport({page,invoke,rpc,report}){
 const root=mkdtempSync(join(tmpdir(),'icax-head-ui-'));
 const sources={
  'head.urdf':`<robot name="Imported AB head"><link name="mount"/><link name="fork"><visual><geometry><box size=".06 .08 .1"/></geometry></visual></link><link name="tool"><visual><geometry><cylinder radius=".02" length=".1"/></geometry></visual><icax_tool name="laser" type="laser_cutting_head" tcp_mm="0 0 -50" beam="0 0 -1"/></link><joint name="A" type="revolute"><parent link="mount"/><child link="fork"/><axis xyz="1 0 0"/><limit lower="-1.5" upper="1.5" effort="1" velocity="1"/></joint><joint name="B" type="revolute"><parent link="fork"/><child link="tool"/><origin xyz="0 0 -.04"/><axis xyz="0 1 0"/><limit lower="-1.5" upper="1.5" effort="1" velocity="1"/></joint></robot>`,
  'head.sdf':`<sdf version="1.7"><model name="Imported AC head"><link name="mount"/><link name="fork"><visual name="body"><geometry><box><size>.08 .08 .1</size></box></geometry></visual></link><link name="tool"><visual name="tool"><geometry><cylinder><radius>.02</radius><length>.1</length></cylinder></geometry></visual></link><joint name="C" type="revolute"><parent>mount</parent><child>fork</child><axis><xyz>0 0 1</xyz><limit><lower>-3.14</lower><upper>3.14</upper></limit></axis></joint><joint name="A" type="revolute"><parent>fork</parent><child>tool</child><axis><xyz>1 0 0</xyz><limit><lower>-1.5</lower><upper>1.5</upper></limit></axis></joint></model></sdf>`,
  'head.stl':'solid head\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 20 0 0\nvertex 0 30 40\nendloop\nendfacet\nendsolid head\n',
  'head.obj':'v 0 0 0\nv 20 0 0\nv 0 30 40\nf 1 2 3\n',
 };
 for(const [name,source] of Object.entries(sources))writeFileSync(join(root,name),source);
 const before=await invoke('CamScene.ReadState');
 try{
  await page.evaluate(()=>{f.context.appProxy={bridge:{async openFileDialog(options){f.pickerOptions=options;return f.nextHeadPath;}}};});
  const cases=[];
  for(const [index,name] of Object.keys(sources).entries()){
   await page.evaluate(({path,hold})=>{f.nextHeadPath=path;if(hold){f.hold=true;f.importDone=false;void f.S.simulationCommand(f.context,f.view,'head-import',f.ops).finally(()=>f.importDone=true);}}, {path:join(root,name),hold:index===0});
   if(index===0){
    await page.waitForFunction(()=>f.held||f.importDone);
    assert.equal(await page.evaluate(()=>f.held),true,await page.evaluate(()=>f.view.fiveSimulation.headError));
    await page.evaluate(()=>{const input=document.querySelector('#right-draft'),left=document.querySelector('.left'),right=document.querySelector('.right');window.headKept={input,left,right,canvas:f.view.viewport.renderer.domElement,events:0};input.addEventListener('head-probe',()=>headKept.events++);input.focus({preventScroll:true});input.setSelectionRange(1,5);left.scrollTop=140;right.scrollTop=200;for(const n of document.querySelectorAll('.nested'))n.scrollTop=45;headKept.scroll=[left.scrollTop,right.scrollTop];f.release();});
   }else await page.evaluate(()=>f.S.simulationCommand(f.context,f.view,'head-import',f.ops));
   await page.waitForFunction(()=>!f.view.fiveSimulation.busy&&!f.view.fiveSimulation.request);
   if(index===0){
    const retained=await page.evaluate(()=>{headKept.input.dispatchEvent(new Event('head-probe'));return {input:headKept.input===document.querySelector('#right-draft'),focus:document.activeElement===headKept.input,selection:[headKept.input.selectionStart,headKept.input.selectionEnd],canvas:headKept.canvas===f.view.viewport.renderer.domElement,scroll:[headKept.left.scrollTop,headKept.right.scrollTop],expected:headKept.scroll,nested:[...document.querySelectorAll('.nested')].map(n=>n.scrollTop),events:headKept.events};});
    assert.equal(retained.input,true);assert.equal(retained.focus,true);assert.equal(retained.canvas,true);assert.deepEqual(retained.selection,[1,5]);assert.deepEqual(retained.scroll,retained.expected);assert.deepEqual(retained.nested,[45,45]);assert.equal(retained.events,1);report.importInteraction=retained;
   }
   let state=await invoke('CamScene.ReadState'),head=state.setups.find(s=>s.id===state.activeSetupId).headModel;
   const importError=await page.evaluate(()=>f.view.fiveSimulation.headError);
   if(importError){assert.equal(head.calibrated,false,name);assert.match(importError,/^请先标定工具端的喷嘴端点和光束方向。$/,name);}
   const articulated=index<2;assert.equal(head.motionType,articulated?'Articulated':'Rigid');assert.equal(Number(head.rotaryAxisCount),articulated?2:0);
   assert.equal(!!head.assembly,articulated);
   if(!head.calibrated){
    await page.evaluate(()=>f.I.headInstanceCommand(f.context,f.view,'tab.tool',f.ops));
    const tcpZ=page.locator('[data-five-head-instance-field="tcp.2"]');await tcpZ.fill('-40');await tcpZ.press('Tab');
    await page.evaluate(()=>f.I.headInstanceCommand(f.context,f.view,'apply',f.ops));
    assert.equal(await page.evaluate(()=>f.view.fiveHeadEditor.error),'',name+' calibration');
   }
   await page.waitForFunction(articulated=>articulated?!!f.view.fiveSimulation.headAssembly:!!f.view.fiveSimulation.group?.getObjectByName('imported-cutting-head'),articulated);
   state=await invoke('CamScene.ReadState');head=state.setups.find(s=>s.id===state.activeSetupId).headModel;
   assert.ok(head.instance.id);assert.equal(head.instance.id,state.setups.find(s=>s.id===state.activeSetupId).headInstanceId);
   assert.deepEqual(head.instance.parameters.tcp,head.tcpLocalPosition);
   if(articulated){assert.ok(head.assembly.nodes.some(n=>!n.previewRef?.url));assert.deepEqual(head.assembly.tcp,head.tcpLocalPosition);}
   assert.deepEqual(state.workpieces,before.workpieces);assert.deepEqual(state.placements,before.placements);
   const saved=await rpc({action:'save-reopen'});assert.deepEqual(saved.setups.find(s=>s.id===saved.activeSetupId).headModel,head);
   cases.push({format:name.split('.').at(-1),motionType:head.motionType,rotaryAxes:Number(head.rotaryAxisCount),saveReopen:true});
   await page.evaluate(async()=>{f.view.scene.domain=await f.context.sceneProxy.invoke('CamScene.ReadState');f.ops.renderProject();});
  }
  const options=await page.evaluate(()=>f.pickerOptions.filters);assert.deepEqual(options[1].extensions,['urdf','sdf']);assert.ok(options[2].extensions.includes('obj'));
  report.imports=cases;report.headImportPicker={nativeFilePathInjected:true,osDialog:false};
 }finally{for(const name of Object.keys(sources))unlinkSync(join(root,name));rmdirSync(root);}
}
