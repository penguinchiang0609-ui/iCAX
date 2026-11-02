import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyHeadPreviewLimits({page,invoke,output}){
 const ready=()=>page.waitForFunction(()=>!f.view.pending&&f.view.fiveHeadPreview?.phase==='ready');
 const read=()=>invoke('CamScene.ReadState');
 const original=await read(),setup=original.setups.find(s=>s.id===original.activeSetupId),parameters=structuredClone(setup.headModel.instance.parameters);
 const indices=await page.evaluate(()=>Object.fromEntries(f.head().assembly.nodes.map((n,index)=>[n.joint,index]).filter(([joint])=>['C','A'].includes(joint))));
 const range=joint=>page.locator(`[data-five-head-joint="${indices[joint]}"]`);
 const label=joint=>page.locator(`[data-five-head-range="${indices[joint]}"]`);
 assert.equal(await label('C').isVisible(),true);assert.equal(await label('C').innerText(),'范围：-180° ～ 180°');
 assert.equal(await label('A').innerText(),'范围：-120° ～ 120°');
 const bounds=async(joint,lower,upper)=>{
  const input=range(joint);assert.equal(Number(await input.getAttribute('min')),lower);assert.equal(Number(await input.getAttribute('max')),upper);
  await input.focus();await input.press('End');assert.equal(Number(await input.inputValue()),upper,joint+' must reach the exact upper bound');
  await input.press('Home');assert.equal(Number(await input.inputValue()),lower,joint+' must reach the exact lower bound');
 };
 await bounds('C',-180,180);await bounds('A',-120,120);
 assert.deepEqual(await read(),original,'Preview endpoints must not change saved head positions or engineering state');
 const rejectOutOfRange=()=>page.evaluate(index=>{
  const s=f.view.fiveHeadPreview,values=[...s.previewPositions];values[index]=f.head().assembly.nodes[index].upper+.001;
  try{f.P.poseCuttingHeadPreview(s.model,values);return false;}catch(error){return error.message.includes('超出范围');}
 },indices.C);
 assert.equal(await rejectOutOfRange(),true);

 const changed=structuredClone(parameters);
 Object.assign(changed.axes.find(a=>a.index===indices.C),{lowerDeg:-30.25,upperDeg:45.75});
 Object.assign(changed.axes.find(a=>a.index===indices.A),{lowerDeg:-40.5,upperDeg:40.5});
 const apply=async parameters=>{
  const state=await read();await invoke('CamHead.Instance',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,action:'configure',parameters});
  const domain=await read();await page.evaluate(domain=>{f.view.scene.domain=domain;f.ops.renderProject();},domain);await ready();return domain;
 };
 let restored=false;
 try{
  const domain=await apply(changed),effective=domain.setups.find(s=>s.id===domain.activeSetupId).headModel;
  const c=effective.assembly.nodes[indices.C];assert.ok(Math.abs(c.lower*180/Math.PI+30.25)<1e-8);assert.ok(Math.abs(c.upper*180/Math.PI-45.75)<1e-8);
  assert.equal(await label('C').innerText(),'范围：-30.25° ～ 45.75°');assert.equal(await label('A').innerText(),'范围：-40.5° ～ 40.5°');
  await bounds('C',-30.25,45.75);await bounds('A',-40.5,40.5);assert.equal(await rejectOutOfRange(),true);
  const solved=await page.evaluate(async indices=>{
   const {machineForward}=await import('/src/apps/five-axis-cam/webpage/machineKinematics.mjs'),{headAssemblyPose}=await import('/src/apps/five-axis-cam/webpage/cuttingHeadAssembly.mjs'),a=f.head().assembly,T=f.THREE;
   const target=c=>{const values=a.nodes.map(n=>Number(n.position||0));values[indices.C]=T.MathUtils.degToRad(c);values[indices.A]=T.MathUtils.degToRad(25);const world=machineForward(a,values);return new T.Vector3(...a.beam).transformDirection(world[a.tool]).negate().toArray();};
   const inside=headAssemblyPose(a,target(35)),boundary=headAssemblyPose(a,target(45.75)),outside=headAssemblyPose(a,target(46));
   return {inside:inside.valid,boundary:boundary.valid,outside:outside.valid,within:inside.valid&&inside.values.every((v,i)=>a.nodes[i].type==='fixed'||v>=a.nodes[i].lower-1e-8&&v<=a.nodes[i].upper+1e-8)};
  },indices);
  assert.deepEqual(solved,{inside:true,boundary:true,outside:false,within:true});
  const after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],original[key],key+' changed while editing only axis limits');
  await page.locator('[data-five-head-joints-reset]').click();await page.locator('[data-five-key="head-preview"]').screenshot({path:resolve(output,'AC-custom-angle-limits.png')});
  const final=await apply(parameters);restored=true;
  // Parameters pass through JSON; signed zero has no geometric meaning.
  assert.deepEqual(JSON.parse(JSON.stringify(final.setups.find(s=>s.id===final.activeSetupId).headModel.instance.parameters)),JSON.parse(JSON.stringify(parameters)));
  assert.equal(await label('C').innerText(),'范围：-180° ～ 180°');assert.equal(await label('A').innerText(),'范围：-120° ～ 120°');
  return {passed:true,default:{C:[-180,180],A:[-120,120]},custom:{C:[-30.25,45.75],A:[-40.5,40.5]},exactPreviewEndpoints:true,previewBeyondLimitRejected:true,solver:solved,savedPositionsUnchangedByPreview:true,cadAndPathsUnchanged:true,originalParametersRestored:true,continuousAxesCovered:false};
 }finally{if(!restored)await apply(parameters).catch(()=>{});}
}
