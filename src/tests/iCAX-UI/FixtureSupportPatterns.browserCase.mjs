import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function runSupportPatterns({page,rpc,info,output,errors}){
  const cases=[],idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,null,{timeout:600000});
  const field=key=>page.locator('[data-five-fixture-field="design:'+key+'"]');
  const fill=(key,value)=>field(key).fill(String(value));
  const select=(key,value)=>field(key).selectOption(value);
  const click=async name=>{await page.locator('[data-five-command="five.fixture.design.'+name+'"]').click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();};
  const state=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const design=()=>page.evaluate(()=>fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).fixtureDesign);
  const noError=async()=>assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await selectWorkpieceFile(page,info.fixturePath);await idle();const other=await page.evaluate(()=>fixture.view.scene.activePlacementId);
  await selectWorkpieceFile(page,info.sourcePath);await idle();const cover=await page.evaluate(()=>fixture.view.scene.activePlacementId);
  if(process.env.ICAX_FIXTURE_FLIP){const s=await state(),p=s.placements.find(p=>p.id===cover),transform=p.transform.map((v,i)=>i>=4&&i<12?-v:v);
    await rpc({action:'invoke',scope:'scene',method:'CamPlacement.SetTransform',payload:{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,placementId:cover,transform}});
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));await idle();cases.push('original cover flipped 180 degrees before face picking and generation');
  }
  assert.notEqual(other,cover);await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));await click('pick');
  assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignPlacement),cover);
  for(const direction of ['top','bottom']){
    const spots=await page.evaluate(async direction=>{
      const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');const v=fixture.view,vp=v.viewport;
      vp.setStandardView(direction);vp.fitView(1.15);await new Promise(requestAnimationFrame);
      const source=vp.sceneObjects.get(v.fixtureDesignPlacement),rect=vp.renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster(),found=new Map();source.updateMatrixWorld(true);
      for(let x=rect.width*.08;x<rect.width*.92;x+=9)for(let y=rect.height*.08;y<rect.height*.92;y+=9){
        ray.setFromCamera(new THREE.Vector2(x/rect.width*2-1,1-y/rect.height*2),vp.camera);const hit=ray.intersectObject(source,false)[0];if(!hit)continue;
        const face=v.fixtureDesignFaces.find(f=>hit.faceIndex>=Number(f.first)&&hit.faceIndex<Number(f.first)+Number(f.count));
        if(face&&!found.has(face.faceId))found.set(face.faceId,{x:rect.x+x,y:rect.y+y,id:String(face.faceId)});
      }return [...found.values()];
    },direction);
    for(const spot of spots){if(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),spot.id))continue;await page.mouse.click(spot.x,spot.y);assert.ok(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),spot.id));}
  }
  assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces.length),4);
  await select('layout','Manual');await fill('positions','0');await click('generate');await noError();
  let d=await design();assert.equal(d.candidate.supportCheck.status,'Insufficient');assert.equal(await page.locator('[data-five-command="five.fixture.design.confirm"]').isDisabled(),true);
  let s=await state();await assert.rejects(rpc({action:'invoke',scope:'scene',method:'CamFixture.Confirm',payload:{commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId,candidateId:d.candidate.id,supportReviewed:true}}),/CAM_FIXTURE_SUPPORT_INSUFFICIENT/);
  assert.equal((await state()).fixtures.length,0);cases.push('manual single plate is previewable but adoption is blocked by UI and authoritative native support assessment');
  await select('layout','Automatic');await select('pattern','Grid');await select('sizing','Manual');await fill('areaWidth',240);await fill('areaDepth',240);await fill('spacing',40);await fill('jointClearance',.3);
  await click('generate');await noError();d=await design();
  const grid=d.candidate;assert.equal(grid.supportCheck.status,'Supported');assert.ok(grid.parts.length>=7);assert.ok(grid.supportCheck.marginMm>=3);
  assert.ok(grid.parts.filter(p=>p.role==='Support').every(p=>p.clearanceMethod==='UnderlyingSurfaceEnvelope'));assert.ok((await page.locator('.cam-info-pane').innerText()).includes('跨孔洞不冒头'));
  assert.equal(grid.joint,'TabSlot');assert.ok(grid.joinery.crossLaps>0);assert.ok(grid.joinery.baseTenons>=grid.parts.length-1);
  const pairs=new Map();for(const p of grid.parts)for(const j of p.joints){const pair=pairs.get(j.id)||[];pair.push({part:p.partId,mate:j.matePartId,type:j.type});pairs.set(j.id,pair);}for(const pair of pairs.values()){assert.equal(pair.length,2);assert.equal(pair[0].part,pair[1].mate);assert.equal(pair[1].part,pair[0].mate);}
  const cap=await page.evaluate(()=>{const v=fixture.view,d=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).fixtureDesign,p=d.candidate.parts.at(-1),g=v.fixtureCandidatePreview.group.children.at(-1).geometry,positions=g.getAttribute('position');let area=0;for(let i=0;i<positions.count;i+=3){if([0,1,2].some(k=>Math.abs(positions.getZ(i+k))>1e-5))continue;const ax=positions.getX(i+1)-positions.getX(i),ay=positions.getY(i+1)-positions.getY(i),bx=positions.getX(i+2)-positions.getX(i),by=positions.getY(i+2)-positions.getY(i);area+=Math.abs(ax*by-ay*bx)/2;}const loopArea=loop=>Math.abs(loop.reduce((n,a,i)=>{const b=loop[(i+1)%loop.length];return n+a[0]*b[1]-a[1]*b[0];},0))/2;return {actual:area,expected:loopArea(p.outline)-p.holes.reduce((n,h)=>n+loopArea(h),0),holes:p.holes.length};});assert.ok(cap.holes>0);assert.ok(Math.abs(cap.actual-cap.expected)<.05,JSON.stringify(cap));
  for(const p of grid.parts.filter(p=>p.role==='Support')){assert.ok(p.contactPoints.length>0);assert.ok(p.minimumDistance<=2*p.topRetreat+.001);}
  assert.ok(grid.parts.some(p=>p.role==='Support'&&p.frame[2]>.5));assert.ok(grid.parts.some(p=>p.role==='Support'&&p.frame[6]<-.5));
  assert.equal((await state()).fixtures.length,0);await click('confirm');await click('breakdown');await click('export');await noError();
  const gridDirectory=(await design()).delivery.directory;
  await rpc({action:'export-project',path:resolve(output,'grid-support.i5cam')});
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.7]);fixture.view.viewport.fitView();});await page.screenshot({path:resolve(output,'grid.png')});
  cases.push('rectangular 240 mm base and full grid plates with paired half-laps and base tenons; correct three-dimensional preview hole area, contact and distributed support footprint');
  await fill('thickness',6);await click('generate');await noError();let thick=await design();
  assert.equal(thick.candidate.supportCheck.status,'Supported');assert.deepEqual(thick.recipe.stations.map(p=>p.id),d.recipe.stations.map(p=>p.id));await click('confirm');
  assert.equal((await state()).fixtures.length,thick.candidate.parts.length);
  await select('layout','Manual');await fill('positions','100000');await click('generate');assert.ok(await page.evaluate(()=>fixture.view.error));assert.equal((await state()).fixtures.length,thick.candidate.parts.length);
  await select('layout','Automatic'); // an inactive invalid manual field must not block generation
  assert.equal(await field('spacing').inputValue(),'40');
  await select('jointMode','Weld');assert.equal(await field('tenonWidth').count(),0);await select('jointMode','TabSlot');assert.equal(await field('tenonWidth').inputValue(),'8');assert.equal(await field('jointClearance').inputValue(),'0.3');
  await select('baseShape','Circle');assert.equal(await field('areaDepth').count(),0);
  await select('pattern','Staggered');await fill('postWidth',12);await fill('thickness',3);
  await page.setViewportSize({width:1440,height:520});await field('baseDrop').scrollIntoViewIfNeeded();
  await page.evaluate(()=>{
    const input=document.querySelector('[data-five-fixture-field="design:baseDrop"]'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
    input.focus({preventScroll:true});input.setSelectionRange(0,1);window.kept={input,left,right,canvas:document.querySelector('canvas'),calls:0};input.addEventListener('retention-probe',()=>kept.calls++);
    fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
  });await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>{kept.input.value='45';kept.input.dispatchEvent(new Event('input',{bubbles:true}));kept.input.setSelectionRange(0,1);kept.left.scrollTop+=15;kept.right.scrollTop+=30;kept.scroll=[kept.left.scrollTop,kept.right.scrollTop];fixture.release();});await idle();
  const retention=await page.evaluate(()=>{kept.input.dispatchEvent(new Event('retention-probe'));return {node:kept.input===document.querySelector('[data-five-fixture-field="design:baseDrop"]'),canvas:kept.canvas===document.querySelector('canvas'),focus:document.activeElement===kept.input,value:kept.input.value,selection:[kept.input.selectionStart,kept.input.selectionEnd],scroll:[kept.left.scrollTop,kept.right.scrollTop],expected:kept.scroll,calls:kept.calls};});
  assert.equal(retention.node,true);assert.equal(retention.canvas,true);assert.equal(retention.focus,true);assert.equal(retention.value,'45');assert.deepEqual(retention.selection,[0,1]);assert.deepEqual(retention.scroll,retention.expected);assert.equal(retention.calls,1);
  assert.equal(await page.locator('[data-five-command="five.fixture.design.export"]').isDisabled(),true);
  await page.setViewportSize({width:1440,height:1000});await click('generate');await noError();d=await design();const staggered=d.candidate;
  assert.equal(staggered.supportCheck.status,'Supported');assert.ok(staggered.parts.length>6);
  assert.equal(staggered.joint,'TabSlot');assert.equal(staggered.joinery.crossLaps,0);assert.ok(staggered.joinery.baseTenons>=staggered.parts.length-1);assert.equal(staggered.parts.at(-1).holes.length,staggered.joinery.baseTenons);
  for(const p of staggered.parts.filter(p=>p.role==='Support')){assert.ok(p.contactPoints.length>0);assert.ok(p.minimumDistance<=2*p.topRetreat+.001);for(const q of p.contactPoints)assert.ok(Math.hypot(q[0],q[1])<=105.001);}
  await click('confirm');await click('breakdown');await click('export');await noError();const exportDirectory=(await design()).delivery.directory;
  cases.push('circular 240 mm base and staggered fill with through mortises matching every support tenon; editable fit clearance, widths and density; repeated generation replaces parts and failed generation preserves prior assembly');
  cases.push('conditional rectangle/circle and manual/automatic fields; real native refresh preserves latest typing, selection, focus, input node, listener, canvas and both pane scrolls');
  s=await state();await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtures,s.fixtures);assert.deepEqual(reopened.setups,s.setups);
  await page.evaluate(async()=>{fixture.entry.mountProject(fixture.context);fixture.view=(await import('/src/apps/_shared/workbench/state/projectViewStore.mjs')).getProjectView(fixture.context.project.projectId);});await idle();
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));await idle();await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size>3);
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.7]);fixture.view.viewport.fitView();document.querySelector('.cam-info-pane').scrollTop=10000;});await page.screenshot({path:resolve(output,'staggered.png')});
  await rpc({action:'export-project',path:resolve(output,'support-patterns.i5cam')});cases.push('native CAD DXF readback, material list, assembly drawing and persisted resources/support assessment survive export and saved project reopen');assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases,source:info.sourcePath,grid,staggered,retention,gridDirectory,exportDirectory,coverage:{browser:'Edge Chromium',fileChooser:'injected paths',native:'production SDO and repository',standaloneCef:false,mechanicalLoadQualification:false}},null,2));console.log(JSON.stringify({passed:true,cases,gridParts:grid.parts.length,staggeredParts:staggered.parts.length,gridDirectory,exportDirectory}));
}
