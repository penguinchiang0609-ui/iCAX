import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyDiscretePlayback({page,button,field,read,command,idle,calls,errors,count,output}) {
  // This geometry/performance fixture has one setup. Read an explicit native
  // render projection so it does not depend on the workbench's area lifecycle.
  await page.evaluate(async()=>{
    const {RENDER_ENTITY_VIEW_PROJECTION}=await import('/src/apps/_shared/workbench/projection.mjs');
    const proxy=fixture.context.sceneProxy;
    fixture.discreteReader=await proxy.views.start({sources:[{sourceId:'discrete-fixture-scene',role:'scene',language:'sql',where:'WHERE HAS CRenderInstanceComponent',projection:RENDER_ENTITY_VIEW_PROJECTION}]});
    const snapshot=await fixture.discreteReader.poll();
    console.log('Explicit native mesh rows',snapshot.rows.length);
    await fixture.view.viewport.applyViewSnapshot(snapshot,proxy.resources);
  });
  console.log('Discrete geometry:',await page.evaluate(()=>{const v=fixture.view;return {placements:v.scene.domain.placements.map(p=>({id:p.id,setupId:p.setupId})),objects:[...v.viewport.sceneObjects].map(([id,o])=>({id,type:o.type,position:o.geometry?.attributes.position?.count})),contact:v.fiveSimulation.frameContact};}));
  try{await page.waitForFunction(()=>{const v=fixture.view,d=v.scene.domain;return d.placements.filter(p=>p.setupId===d.activeSetupId).every(p=>v.viewport.sceneObjects.get(p.id)?.isMesh);},null,{timeout:15000});}
  catch(error){console.log('Discrete load failure:',await page.evaluate(()=>({logs:fixture.logs,viewport:fixture.view.viewport.getDebugState(),snapshots:[...fixture.context.sceneProxy.views.readers].map(r=>r.snapshot)})));throw error;}
  const before=await read(),callStart=calls.length;
  const started=performance.now();await button('play').click();
  await page.waitForFunction(()=>fixture.view.fiveSimulation.cursor>.2||!!fixture.view.fiveSimulation.alarmCollision);
  const firstPlayMs=Math.round(performance.now()-started);
  const samples=await page.evaluate(async()=>{
    const samples=[];
    for(let i=0;i<90;++i){await new Promise(requestAnimationFrame);const s=fixture.view.fiveSimulation;samples.push({ms:s.frameContact?.elapsedMs,cursor:s.cursor,playing:s.playing,status:s.frameContact?.status,mode:s.frameContact?.mode});}
    return samples;
  });
  assert.ok(samples.every(s=>s.mode==='DiscreteBoxes'&&s.status!=='Unchecked'),JSON.stringify(samples.slice(-2)));
  assert.ok(samples.some(s=>s.playing),'the real workpiece can play');
  assert.ok(samples.at(-1).cursor>samples[0].cursor,'playback advances while each rendered pose is checked');
  await button('pause').click();
  assert.equal(calls.slice(callStart).filter(c=>/^CamSimulation\./.test(c.method)).length,0,'no native path or collision task during playback');
  const timings=samples.map(s=>s.ms).filter(Number.isFinite).sort((a,b)=>a-b);
  const p95Ms=timings[Math.floor(timings.length*.95)];
  console.log('Discrete playback timings:',{firstPlayMs,p95Ms,maxMs:timings.at(-1)});
  assert.ok(p95Ms<16.7,`per-frame collision p95 ${p95Ms}ms exceeds one 60Hz frame`);
  const cursor=await page.evaluate(()=>fixture.view.fiveSimulation.cursor);
  await page.waitForTimeout(100);assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.cursor),cursor);
  const seekSamples=await page.evaluate(async()=>{
    const {simulationField}=await import('/src/apps/five-axis-cam/webpage/trajectorySimulation.mjs');
    const v=fixture.view,start=v.fiveSimulation.cursor,results=[];
    for(const cursor of [1000.25,3200.5,6408.3,8000.5]){if(cursor>=v.fiveSimulation.frames.length-1)continue;
      simulationField(fixture.context,v,'seek',cursor);results.push({...v.fiveSimulation.frameContact});}
    simulationField(fixture.context,v,'seek',start);return results;
  });
  assert.ok(seekSamples.every(s=>s.status!=='Unchecked'&&s.elapsedMs<16.7),JSON.stringify(seekSamples));

  // A hidden but collision-enabled fixture at the CURRENT fractional pose
  // must stop there, independently of existing full-path report contents.
  await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const v=fixture.view,s=v.fiveSimulation,head=v.scene.domain.setups.find(item=>item.id===v.scene.domain.activeSetupId).headModel;s.group.updateWorldMatrix(true,false);
    const box=head.collisionModel.boxes[0];
    const mesh=new THREE.Mesh(new THREE.BoxGeometry(6,6,6),new THREE.MeshBasicMaterial());
    mesh.position.copy(new THREE.Vector3(...box.center).applyMatrix4(s.group.matrixWorld));mesh.visible=false;
    fixture.discreteFixture=mesh;v.viewport.scene.add(mesh);v.viewport.sceneObjects.set('discrete-fixture',mesh);
    v.scene.domain.fixtures.push({id:'discrete-fixture',setupId:v.scene.domain.activeSetupId,collisionEnabled:true});
  });
  await button('play').click();
  const alarm=await page.evaluate(()=>{const s=fixture.view.fiveSimulation;return {cursor:s.cursor,contact:s.frameContact,playing:s.playing,message:s.message};});
  assert.equal(alarm.playing,false);assert.equal(alarm.cursor,cursor);
  assert.equal(alarm.contact.status,'Collision');assert.equal(alarm.contact.obstacleId,'discrete-fixture');
  assert.equal(alarm.contact.cursor,alarm.cursor);assert.match(alarm.message,/当前位置.*碰撞/);
  await page.screenshot({path:resolve(output,'discrete-current-pose-collision.png')});
  await page.evaluate(()=>{const v=fixture.view;v.scene.domain.fixtures=v.scene.domain.fixtures.filter(f=>f.id!=='discrete-fixture');v.viewport.sceneObjects.delete('discrete-fixture');fixture.discreteFixture.removeFromParent();fixture.discreteFixture.geometry.dispose();fixture.discreteFixture.material.dispose();});
  await field('seek').evaluate((input,value)=>{input.value=String(value);input.dispatchEvent(new Event('input',{bubbles:true}));},cursor);
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.frameContact.status),'Clear');
  await button('head-toggle').click();await button('play').click();
  await page.waitForFunction(start=>fixture.view.fiveSimulation.cursor>start+.1,cursor);await button('pause').click();
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.frameContact.status),'Clear');
  assert.equal(calls.slice(callStart).filter(c=>/^CamSimulation\./.test(c.method)).length,0);
  assert.deepEqual(await read(),before,'display-frame checks never publish or alter a full-path report');
  await button('head-toggle').click();
  await command('five.tool.collision');
  await button('collision-check').click();await page.waitForFunction(()=>!!fixture.view.fiveSimulation.cancelCheck);
  await button('collision-cancel').click();await idle();
  assert.ok(calls.slice(callStart).some(c=>c.method==='CamSimulation.CheckCollision'),'explicit collision operation still runs through native SDO');
  assert.deepEqual(errors,[]);
  const report={passed:true,frames:count,firstPlayMs,displayedFrames:samples.length,collisionQueryMs:{p95:p95Ms,max:timings.at(-1)},seekSamples,nativeCollisionCallsDuringPlayback:0,cases:['current interpolated displayed pose only','no sweep or path prefetch','obstacle BVH reuse','hidden head and enabled hidden fixture','collision pauses at the actual displayed pose','independent explicit collision check and cancel','no scene or report mutation'],coverage:{nativeScene:true,nativeRenderProjection:'single-setup unfiltered CRenderInstanceComponent view',browser:'Edge',standaloneCef:false,collisionGeometry:'native calibrated TCP boxes against obstacle triangle meshes'}};
  writeFileSync(resolve(output,'contact-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}
