import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {verifyResume,verifyFrameReset,verifyReopenedFrame} from './FiveAxisAlignmentResume.native.browser.mjs';

export async function verifyAlignmentReal({page,command,read,rpc,invoke,refresh,output}){
  const cases=[],initial=await read(),id=initial.placements[0].id,curved=/\.igs$/i.test(process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||'');
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAlignment?.picking&&!fixture.view.fiveAlignment?.solving);
  const action=async a=>{await page.locator(`[data-five-command="five.align.${a}"]`).last().click();await idle();};
  const matrix=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId).matrix.clone().transpose().toArray());
  const map=(m,p,d=false)=>[0,1,2].map(i=>(d?0:m[i*4+3])+p.reduce((s,v,j)=>s+v*m[i*4+j],0));
  const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-5,`${a} != ${b}`));
  const open=async()=>{await page.locator(`[data-five-workpiece="${id}"]`).click();await page.getByRole('button',{name:'对齐',exact:true}).click();await idle();await page.waitForFunction(()=>fixture.view.sceneSnapping.pending?.size===0);};
  const find=async(mode,objectId=id)=>{
    await page.locator('[data-geometry-pick-mode]').selectOption(mode);
    for(const eye of [[0,0,1],[0,0,-1],[1,-1,1],[-1,1,1]]){
      await page.evaluate(eye=>{fixture.view.viewport.setViewDirection(eye);fixture.view.viewport.fitView(1.25);},eye);
      const samples=await page.evaluate(async({objectId,mode})=>{
        const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view.viewport,o=v.sceneObjects.get(objectId),r=v.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster(),result=[];
        o.updateMatrixWorld(true);
        for(let y=r.y+170;y<r.bottom-50;y+=12)for(let x=r.x+300;x<r.right-30;x+=12){
          if(document.elementFromPoint(x,y)!==v.renderer.domElement)continue;
          ray.setFromCamera({x:(x-r.x)/r.width*2-1,y:1-(y-r.y)/r.height*2},v.camera);
          const h=ray.intersectObjects([...v.sceneObjects.values()].filter(o=>o.visible),false)[0];if(h?.object!==o)continue;
          const p=o.worldToLocal(h.point.clone()),n=h.point.clone().project(v.camera),dx=new T.Vector3(n.x+24/r.width,n.y,n.z).unproject(v.camera);const distance=Math.min(100,o.worldToLocal(dx).distanceTo(p)*1.5);
          result.push({x,y,query:{mode,frame:'Workpiece',triangleIndex:String(h.faceIndex),nearPoint:p.toArray(),maximumDistanceMm:distance}});
        }return result;
      },{objectId,mode});
      for(const p of samples){try{
        const scene=await read();const r=await invoke('CamScene.ResolvePickReference',{expected:scene.expected,setupId:scene.activeSetupId,objectId,query:p.query});
        await page.mouse.move(p.x,p.y);await page.mouse.click(p.x,p.y);await idle();
        const d=await page.evaluate(()=>({current:fixture.view.fiveAlignment.current,error:fixture.view.fiveAlignment.error,valid:fixture.view.fiveAlignment.valid}));
        assert.equal(d.error,'');assert.equal(d.valid,true);return r;
      }catch(error){if(!/CAD_REFERENCE_(NOT_FOUND|TOO_FAR|REQUIRES_|AMBIGUOUS)/.test(error.message))throw error;}}
    }throw Error(`No visible ${mode} on real model`);
  };
  await open();const plane=await find(curved?'center':'plane');if(curved)await action('circle-axis');near(map(await matrix(),plane.direction,true),[0,0,1]);assert.deepEqual(await read(),initial);
  await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.fitView();});await page.screenshot({path:resolve(output,'real-plane-align.png')});
  await action('next');const circle=await find('center');near(map(await matrix(),circle.point),[0,0,0]);near(map(await matrix(),plane.direction,true),[0,0,1]);
  await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.fitView();});await page.screenshot({path:resolve(output,'real-circle-origin.png')});
  const result=await matrix();await action('apply');near((await read()).placements[0].transform,result);
  assert.deepEqual(await page.evaluate(()=>({tool:fixture.view.fiveAxisUI.tools.workpiece,placementOpen:fixture.view.fivePlacement?.open,placementMode:fixture.view.fivePlacement?.mode,manipulator:fixture.view.viewport.transformManipulator?.control.mode,toolbar:!!document.querySelector('.five-placement-tools'),error:fixture.view.error})),{tool:'translate',placementOpen:true,placementMode:'translate',manipulator:'translate',toolbar:true,error:''});
  await rpc({action:'undo'});await refresh();near((await read()).placements[0].transform,initial.placements[0].transform);
  await rpc({action:'redo'});await refresh();near((await read()).placements[0].transform,result);
  cases.push(`Real CAD model: ${curved?'circle axis':'plane'} selection immediately levels the part; a circular edge locates its exact centre at the origin while retaining the direction; preview, confirm and undo/redo agree`);
  await verifyResume({page,read,invoke,rpc,refresh,open,action});
  cases.push('Immediately after confirm: real gizmo drag, undo, repeat alignment, and native revision invalidation restore usable Move handles');
  await open();const axisCircle=await find('center');await action('circle-axis');near(map(await matrix(),axisCircle.direction,true),[0,0,1]);
  await action('flip');near(map(await matrix(),axisCircle.direction,true),[0,0,-1]);await action('close');near(await matrix(),result);
  cases.push('Circle centre and hole-axis actions share the same exact circle pick; axis alignment and reversal preview correctly and cancellation restores the saved placement');


  // Reuse the real imported part as a fixture target, held at another placement.
  let scene=await read();let task=await invoke('CamFixture.Import',{commandId:crypto.randomUUID(),expected:scene.expected,setupId:scene.activeSetupId,sourcePath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL});
  while(task.status==='Running'){task=await invoke('CamFixture.QueryTask',{epoch:task.epoch,taskId:task.taskId});}
  assert.equal(task.status,'Completed',task.error);const fixtureId=task.result.fixtureId;await refresh();
  await open();await find(curved?'center':'plane');await action('target-pick');const targetPlane=await find(curved?'center':'plane',fixtureId);
  const fixtureTransform=(await read()).fixtures.find(f=>f.id===fixtureId).transform;
  const current=await page.evaluate(()=>fixture.view.fiveAlignment.current);
  if(curved)near(map(await matrix(),current.sourceResult.point),map(fixtureTransform,targetPlane.point));
  else {near(map(await matrix(),current.sourceResult.direction,true),map(fixtureTransform,targetPlane.direction,true));await action('contact');const targetNormal=map(fixtureTransform,targetPlane.direction,true),targetPoint=map(fixtureTransform,targetPlane.point),sourcePoint=map(await matrix(),current.sourceResult.point);assert.ok(Math.abs(targetNormal.reduce((sum,n,i)=>sum+n*(sourcePoint[i]-targetPoint[i]),0))<1e-5);}
  await page.screenshot({path:resolve(output,'real-fixture-align.png')});await action('apply');scene=await read();assert.deepEqual(scene.fixtures.find(f=>f.id===fixtureId).transform,fixtureTransform);
  cases.push(`A visible fixture is a real selectable target; ${curved?'circle centre alignment':'plane alignment and optional contact'} uses its placement and exact geometry while leaving the fixture unchanged`);
  await open();await find('center');await action('circle-axis');
  await verifyFrameReset({page,read,invoke,rpc,refresh,action});
  cases.push('Real-model frame reset preserves all world vertices/normals and exact snap features; identity, geometry, undo/redo and fixture placement agree');
  assert.deepEqual((await read()).fixtures.find(f=>f.id===fixtureId).transform,fixtureTransform);
  await verifyReopenedFrame({page,rpc,read});
  cases.push('Real-model rebased geometry and fixture targets survive save/reopen and Move handles are immediately active');return cases;
}
