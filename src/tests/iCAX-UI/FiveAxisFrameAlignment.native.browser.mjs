import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {activeHandles,identity,verifyResume,verifyFrameReset,verifyReopenedFrame} from './FiveAxisAlignmentResume.native.browser.mjs';

export async function frameAlignmentDriver(page,base='/src/'){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAlignment?.picking&&!fixture.view.fiveAlignment?.solving&&!fixture.view.sceneSnapping?.presentation?.membersPending);
  const action=async a=>{
    if(['basis-u','basis-v','plane-center'].includes(a))return pick(a==='plane-center'?'datum-point':'direction',{construction:a==='basis-u'?'PlaneU':a==='basis-v'?'PlaneV':'PlaneCenter'});
    await page.locator(`[data-five-command="five.align.${a}"]`).last().click();await idle();
  };
  const open=async()=>{
    await page.locator('.cam-info-pane [data-five-command="five.tool.align"]').click();
    await page.getByRole('region',{name:'自身坐标系重置',exact:true}).waitFor();
    assert.equal(await page.locator('.cam-info-pane .five-align-panel').count(),1);
    assert.equal(await page.locator('[data-five-overlay] .five-align-panel').count(),0);
    assert.equal(await page.locator('[data-five-command="five.align.reset-frame"]').count(),0);
    await idle();await page.waitForFunction(()=>fixture.view.fiveAlignment?.error||fixture.view.sceneSnapping?.catalogs.has(fixture.view.fiveAlignment?.placementId)&&fixture.view.sceneSnapping?.pending?.size===0);
    assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment?.error),'');
  };
  const pick=async(mode,{prefix='',exclude=[],point=null,construction=null,helperType=null,empty=false,screenshot=null,allowError=false}={})=>{
    await idle();assert.equal(await page.locator('[data-geometry-pick-mode]').count(),0);
    for(const eye of [[0,0,1],[0,0,-1],[1,-1,1],[-1,1,1],[1,0,0],[0,1,0]]){
      const found=await page.evaluate(async({base,eye,mode,prefix,exclude,point,construction,helperType,empty,allowError})=>{
        const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{inferAlignmentCandidate}=await import(base+'apps/five-axis-cam/webpage/alignmentInference.mjs'),v=fixture.view,vp=v.viewport,d=v.fiveAlignment,id=d.placementId,o=vp.sceneObjects.get(id),catalog=v.sceneSnapping.catalogs.get(id);
        vp.setViewDirection(eye);vp.fitView(1.25);vp.scene.updateMatrixWorld(true);
        const presentation=v.sceneSnapping.presentation,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
        const handles=presentation?.layer?.records.flatMap(h=>h.type==='plane'?h.corners.flatMap(c=>[.35,.7,.95].map(t=>new T.Vector3(...h.feature.point).lerp(c,t).toArray())):h.type==='direction'?[.5,.75,.95].map(t=>new T.Vector3(...h.feature.point).lerp(h.end,t).toArray()):[h.feature.point])||[];
        const points=[...handles,...catalog.features.flatMap(f=>[f.point,...f.edgeSamples.filter((_,i)=>i%8===0)])];
        for(const p of points){const projected=new T.Vector3(...p).applyMatrix4(o.matrixWorld).project(vp.camera),x=r.left+(projected.x+1)*r.width/2,y=r.top+(1-projected.y)*r.height/2;
          if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},vp.camera);
          const h=ray.intersectObjects([...vp.sceneObjects.values()].filter(o=>o.visible),false)[0];if(empty&&h)continue;
          const c=inferAlignmentCandidate(v,d,h,{clientX:x,clientY:y});
          if(!c?.result||(c.result.mode!==mode&&!(point&&['endpoint','midpoint','center','datum-point','sphere-center'].includes(c.result.mode)))||!c.result.featureId.startsWith(prefix)||exclude.includes(c.result.featureId))continue;
          if(construction&&c.result.query.construction?.kind!==construction)continue;
          if(construction==='PlaneNormal'&&Math.abs(new T.Vector3(...c.result.direction).dot(new T.Vector3(...d.plane.sourceResult.direction)))>1-1e-6)continue;
          if(mode==='line'&&!allowError&&d.plane&&Math.abs(new T.Vector3(...c.result.direction).dot(new T.Vector3(...d.plane.sourceResult.direction)))>1-1e-6)continue;
          if(point&&c.result.point.some((n,i)=>Math.abs(n-point[i])>1e-5))continue;return {x,y,result:c.result};
        }return null;
      },{base,eye,mode,prefix,exclude,point,construction,helperType,empty,allowError});
      if(found){await page.mouse.move(found.x,found.y);
        if(screenshot){await page.waitForFunction(()=>!!fixture.view.fiveAlignment.hover);await page.screenshot({path:screenshot});}
        await page.mouse.click(found.x,found.y);await idle();const error=await page.evaluate(()=>fixture.view.fiveAlignment.error);if(!allowError)assert.equal(error,'');return found.result;}
    }throw Error(`No visible ${mode}/${prefix} snap anchor`);
  };
  const complete=async({curved=false}={})=>{await open();await pick('plane',curved?{prefix:'curve-plane:'}:{});await action('next');await action('basis-u');await action('next');await action('plane-center');};
  return {idle,action,open,pick,complete};
}

export async function verifyFrameAlignment({page,read,invoke,rpc,refresh,output}){
  const cases=[],initial=await read(),real=!!process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL,curved=/\.igs$/i.test(process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||'');
  const {idle,action,open,pick,complete}=await frameAlignmentDriver(page);
  const matrix=()=>page.evaluate(()=>fixture.view.fiveAlignment.report.transform);
  const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-5,`${v} != ${b[i]}`));
  const map=(m,p,dir=false)=>[0,1,2].map(r=>(dir?0:m[r*4+3])+p.reduce((sum,v,c)=>sum+v*m[r*4+c],0));
  await open();assert.equal(await page.locator('[data-five-command="five.align.apply"]').isDisabled(),true);
  await page.screenshot({path:resolve(output,'direct-auxiliary-planes.png')});
  const plane=await pick('plane',curved?{prefix:'curve-plane:',helperType:'plane',empty:true}:{prefix:'plane:'});assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.stage),'normal');near(map(await matrix(),plane.direction,true),[0,0,1]);
  await action('flip');near(map(await matrix(),plane.direction,true),[0,0,-1]);await action('flip');
  await action('next');assert.equal(await page.locator('[data-five-command="five.align.next"]').isDisabled(),true);
  await page.screenshot({path:resolve(output,'direct-auxiliary-directions.png')});
  if(!curved){const normal=await pick('direction',{construction:'PlaneNormal',helperType:'direction'});assert.equal(normal.query.construction.kind,'PlaneNormal');await action('reselect');}
  await action('basis-u');near(map(await matrix(),plane.xDirection,true),[1,0,0]);
  await action('flip');near(map(await matrix(),plane.xDirection,true),[-1,0,0]);await action('flip');
  // Native asynchronous preview must retain both panels' current interaction state.
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1700px}'});
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.SolveRegistration';fixture.hold=true;});await page.locator('[data-five-command="five.align.flip"]').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const button=document.querySelector('.five-align-panel header button');fixture.alignNodes={canvas:document.querySelector('canvas'),button};button.focus({preventScroll:true});return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=80+i*30;return p.scrollTop;});});
  await page.evaluate(()=>fixture.release());await idle();assert.equal(await page.evaluate(()=>{const n=fixture.alignNodes;return n.canvas===document.querySelector('canvas')&&n.button===document.querySelector('.five-align-panel header button')&&document.activeElement===n.button;}),true);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),scroll);await stress.evaluate(n=>n.remove());await action('flip');
  await action('next');assert.equal(await page.locator('[data-five-command="five.align.apply"]').isDisabled(),true);await action('plane-center');near(map(await matrix(),plane.point),[0,0,0]);assert.deepEqual(await read(),initial);
  await page.screenshot({path:resolve(output,'four-step-alignment.png')});await action('close');assert.deepEqual(await read(),initial);
  cases.push('Guided plane → positive Z → positive X → origin in the right sidebar; stage gates, reversals, plane basis and exact centre preview; cancel is read-only; asynchronous preview preserves sidebar scroll, canvas and button focus');
  cases.push(curved?'Auxiliary plane picked inside its patch outside the model silhouette with no real face hit; U and centre selected directly without any pick-mode menu':'Plane normals, U arrows and centres selected by real pointer input with automatic type filtering and native exact reference resolution');

  if(!real){
    await open();await action('three-points');await pick('endpoint',{point:[0,0,20]});await pick('endpoint',{point:[40,0,20]});await pick('endpoint',{point:[0,60,20]});assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.plane.sourceResult.label),'三点基准面');
    await action('next');await action('two-points');await pick('endpoint',{point:[0,0,20]});await pick('endpoint',{point:[40,0,20]});assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.x.sourceResult.label),'两点方向');await action('next');await pick('endpoint',{point:[0,0,20]});await action('close');
    cases.push('Actual pointer picks construct a plane from three exact anchors and an X direction from two anchors through the shared snap service');
  }
  await complete({curved});await action('apply');await activeHandles(page);near((await read()).placements[0].transform,identity);
  await verifyResume({page,read,invoke,rpc,refresh,open,action});cases.push('Reset confirmation always zeros placement and immediately restores Move; actual placement drag preserves local geometry; undo, repeat alignment and native revision invalidation do not strand the editor');
  await complete({curved});const draft=await page.evaluate(()=>fixture.view.fiveAlignment),scene=await read();
  const forged={expected:scene.expected,setupId:scene.activeSetupId,objectId:scene.placements[0].id,query:{mode:'direction',construction:{kind:'TwoPointDirection',sources:[{mode:'endpoint',featureId:'vertex:invalid'},draft.origin.source]}}};
  await assert.rejects(()=>invoke('CamScene.ResolvePickReference',forged),/FEATURE_MISSING/);await action('close');
  await complete({curved});const beforeCommitCatalogs=await page.evaluate(()=>fixture.snapCalls);await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await action('apply');assert.equal(await page.getByRole('button',{name:'核对结果',exact:true}).count(),1);assert.equal(await page.getByRole('button',{name:'取消',exact:true}).isDisabled(),true);const uncertain=await read();assert.equal(await page.evaluate(()=>fixture.snapCalls),beforeCommitCatalogs,'An unconfirmed frame reset must not request features using its pre-commit revision');await action('apply');assert.deepEqual(await read(),uncertain);await activeHandles(page);
  cases.push('Constructed references are re-resolved by native geometry; forged anchors are rejected; lost confirmation is reconciled idempotently');
  await complete({curved});await verifyFrameReset({page,read,invoke,rpc,refresh,action});cases.push('Reset object frame preserves preview world vertices, normals and exact features, resets placement without an optional checkbox and is atomic under undo/redo');
  await verifyReopenedFrame({page,rpc,read});await open();await action('close');await activeHandles(page);cases.push('Save/reopen preserves the rebased model and immediately supports another alignment');
  return cases;
}
