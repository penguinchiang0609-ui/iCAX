import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {worldGeometry} from './FiveAxisAlignmentResume.native.browser.mjs';
import {startQuietOperationProbe,stopQuietOperationProbe} from './fixtures/quietOperationProbe.mjs';

export async function pickFrameProperty(page,row,{mode=null,point=null,normal=false,screenshot=null}={}){
  await page.locator(`[data-five-command="five.align.pick.${row}"]`).click();
  await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.fiveAlignment?.placementId)&&!fixture.view.sceneSnapping.pending.size);
  for(const eye of [[0,0,1],[1,-1,1],[-1,1,1],[0,0,-1],[1,0,0],[0,1,0]]){
    const found=await page.evaluate(async({eye,mode,point,normal})=>{
      const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),{inferAlignmentCandidate}=await import('/src/apps/five-axis-cam/webpage/alignmentInference.mjs');
      const v=fixture.view,vp=v.viewport,d=v.fiveAlignment,object=vp.sceneObjects.get(d.placementId),catalog=v.sceneSnapping.catalogs.get(d.placementId);
      vp.setViewDirection(eye);vp.fitView(1.25);vp.scene.updateMatrixWorld(true);
      const r=vp.renderer.domElement.getBoundingClientRect(),points=catalog.features.flatMap(f=>[f.point,...(f.edgeSamples||[]).filter((_,i)=>i%5===0)]);
      for(const p of points){const q=new T.Vector3(...p).applyMatrix4(object.matrixWorld).project(vp.camera),x=r.x+(q.x+1)*r.width/2,y=r.y+(1-q.y)*r.height/2;
        if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
        const c=inferAlignmentCandidate(v,d,null,{clientX:x,clientY:y});
        if(c?.result&&d.selecting!=='origin'&&d.firstAxis&&d.firstAxis!==d.selecting){const {frameReferenceVector}=await import('/src/apps/five-axis-cam/webpage/workpieceFrame.mjs');const direction=frameReferenceVector(c.result.direction,d.savedFrame);if(Math.abs(direction.reduce((sum,n,i)=>sum+n*d.frame[d.firstAxis][i],0))>.999999)continue;}
        if(!c?.result||mode&&c.result.mode!==mode||normal&&c.result.query.construction?.kind!=='PlaneNormal'||point&&c.result.point.some((n,i)=>Math.abs(n-point[i])>1e-5))continue;
        return {x,y,result:c.result};
      }return null;
    },{eye,mode,point,normal});
    if(!found)continue;
    await page.mouse.move(found.x,found.y);await page.waitForFunction(()=>!!fixture.view.fiveAlignment.hover);
    if(screenshot)await page.screenshot({path:screenshot});
    await page.mouse.click(found.x,found.y);await page.waitForFunction(()=>!fixture.view.fiveAlignment?.picking&&!fixture.view.fiveAlignment?.selecting);
    assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.error),'');return found.result;
  }
  throw Error(`No visible frame property target: ${row}/${mode}/${normal}`);
}

export async function verifyObjectFrameProperties({page,rpc,output}){
  const cases=[],read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),refresh=()=>page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  const initial=await read(),panel=page.locator('.cam-info-pane .five-frame-properties');
  const field=(row,axis)=>panel.locator(`[data-five-frame-field="${row}-${axis}"]`),command=action=>panel.locator(`[data-five-command="five.align.${action}"]`);
  const frame=()=>page.evaluate(()=>fixture.view.fiveAlignment.frame),saved=()=>page.waitForFunction(()=>!!fixture.view.fiveAlignment&&!fixture.view.fiveAlignment.saving&&!fixture.view.fiveAlignment.saveTimer&&!fixture.view.fiveAlignment.dirty&&!fixture.view.fiveAlignment.request);
  const near=(a,b)=>a.forEach((n,i)=>assert.ok(Math.abs(n-b[i])<1e-8,`${a} != ${b}`));
  const rightHanded=f=>{for(const a of ['x','y','z'])assert.ok(Math.abs(Math.hypot(...f[a])-1)<1e-8);const [x,y,z]=[f.x,f.y,f.z];assert.ok(Math.abs(x.reduce((s,n,i)=>s+n*z[i],0))<1e-8);near([z[1]*x[2]-z[2]*x[1],z[2]*x[0]-z[0]*x[2],z[0]*x[1]-z[1]*x[0]],y);};
  assert.equal(await panel.locator('input').count(),12);assert.equal(await panel.locator('input[readonly]').count(),3);
  assert.equal(await command('pick.y').count()+await command('flip.y').count(),0);assert.equal(await command('apply').count()+await command('close').count(),0);
  for(const width of [320,260]){
    await page.evaluate(width=>document.querySelector('.five-workspace').style.setProperty('--cam-right-width',width+'px'),width);
    const sizes=await panel.locator('[data-five-key="frame-row-y"] input').evaluateAll(nodes=>nodes.map(n=>({width:n.getBoundingClientRect().width,y:n.getBoundingClientRect().y})));
    assert.ok(sizes.every(s=>s.width>20&&s.width<110&&s.y===sizes[0].y));
  }
  await page.evaluate(()=>document.querySelector('.five-workspace').style.removeProperty('--cam-right-width'));await page.screenshot({path:resolve(output,'readonly-y-no-confirm.png')});
  cases.push('X and Z remain editable and pickable; Y is visibly read-only and automatic; no confirm/cancel controls appear, including in a 260px sidebar');

  await startQuietOperationProbe(page);await field('x','Y').fill('1');await field('x','X').fill('0');await field('x','X').press('Enter');await saved();
  near((await frame()).x,[0,1,0]);near((await frame()).origin,[0,0,0]);rightHanded(await frame());assert.notDeepEqual((await read()).workpieces,initial.workpieces);
  assert.equal(await field('x','Y').inputValue(),'1');assert.equal(await field('x','X').inputValue(),'0');assert.equal(await panel.locator('footer').count(),0);
  assert.equal(await page.locator('.cam-info-pane [data-five-transform-field^="scale-"]').count(),0);
  const primary=[...(await frame()).x];await field('z','X').fill('1');await field('z','Y').fill('1');await field('z','Z').fill('1');
  const preview=await worldGeometry(page);await field('z','Z').press('Enter');await saved();near((await frame()).x,primary);near((await frame()).z,[Math.SQRT1_2,0,Math.SQRT1_2]);rightHanded(await frame());
  const after=await worldGeometry(page);after.points.forEach((n,i)=>assert.ok(Math.abs(n-preview.points[i])<5e-5));
  const beforeFlip=await frame();await command('flip.z').click();await saved();near((await frame()).x,primary);near((await frame()).z,beforeFlip.z.map(n=>-n));rightHanded(await frame());
  cases.push('Axis-only input and reversal automatically commit natively; saving retains numeric values, the first chosen axis and exact preview geometry');

  const valid=await frame(),nativeValid=await read();await field('z','X').fill('0');await field('z','Y').fill('1');await field('z','Z').fill('0');
  assert.equal(await field('z','Z').getAttribute('aria-invalid'),'true');assert.match(await panel.innerText(),/不能平行/);await page.waitForTimeout(550);assert.deepEqual(await read(),nativeValid);
  for(const [i,a] of ['X','Y','Z'].entries())await field('z',a).fill(String(valid.z[i]));await field('z','Z').press('Enter');await saved();
  await field('z','X').fill('1e309');assert.equal(await field('z','X').getAttribute('aria-invalid'),'true');await field('z','X').fill(String(valid.z[0]));await field('z','X').press('Enter');await saved();
  cases.push('Parallel and non-finite directions show field errors and never enter the native repository');

  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1300px}.five-frame-properties{height:170px;overflow:auto}.five-frame-properties::after{content:"";display:block;height:600px}'});
  await page.evaluate(()=>{fixture.holdAfterApply=true;fixture.keptFrameInput=document.querySelector('[data-five-frame-field="origin-Y"]');fixture.keptCanvas=fixture.view.viewport.renderer.domElement;fixture.inputEvents=0;fixture.keptFrameInput.addEventListener('acceptance-kept',()=>fixture.inputEvents++);});
  await field('origin','X').fill('10');await field('origin','X').press('Enter');await page.waitForFunction(()=>fixture.held);
  assert.equal(await field('origin','Y').getAttribute('readonly'),null);assert.equal(await page.locator('.five-blocking-progress').count(),0);await field('origin','Y').fill('20.000');
  const latest=await page.evaluate(()=>{const input=fixture.keptFrameInput,panel=input.closest('.five-frame-properties'),panes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')];input.focus({preventScroll:true});input.setSelectionRange(1,4);panes.forEach(p=>p.scrollTop=25);panel.scrollTop=15;panes.forEach((p,i)=>p.scrollTop=80+i*30);panel.scrollTop=60;return {scroll:panes.map(p=>p.scrollTop),nested:panel.scrollTop};});
  await page.evaluate(()=>fixture.release());await saved();
  const retained=await page.evaluate(()=>{const input=document.querySelector('[data-five-frame-field="origin-Y"]');input.dispatchEvent(new Event('acceptance-kept'));return {input:fixture.keptFrameInput===input,canvas:fixture.keptCanvas===fixture.view.viewport.renderer.domElement,focused:document.activeElement===input,value:input.value,selection:[input.selectionStart,input.selectionEnd],events:fixture.inputEvents,scroll:[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')].map(p=>p.scrollTop),nested:input.closest('.five-frame-properties').scrollTop};});
  assert.deepEqual(retained,{input:true,canvas:true,focused:true,value:'20.000',selection:[1,4],events:1,...latest});near((await frame()).origin,[10,20,0]);await stress.evaluate(n=>n.remove());
  cases.push('Delayed saves preserve newer input, selection, focus, listeners, canvas and both latest sidebar/nested scroll positions; later edits save in sequence with no blocking progress panel');

  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await command('flip.z').click();await page.waitForFunction(()=>fixture.view.fiveAlignment.saveError&&!fixture.view.fiveAlignment.saving);
  const once=await read();assert.equal(await command('retry-save').count(),1);assert.equal(await command('apply').count(),0);await command('retry-save').click();await saved();assert.deepEqual(await read(),once);
  cases.push('Lost save receipts expose only retry; reconciliation is idempotent and never applies the coordinate change twice');

  await pickFrameProperty(page,'origin',{mode:'endpoint'});await saved();await pickFrameProperty(page,'x',{mode:'line'});await saved();const firstPicked=[...(await frame()).x];
  await pickFrameProperty(page,'z',{mode:'direction',normal:true,screenshot:resolve(output,'two-axis-pick.png')});await saved();near((await frame()).x,firstPicked);rightHanded(await frame());
  assert.equal(await panel.locator('footer').count(),0);await page.screenshot({path:resolve(output,'realtime-frame-saved.png')});
  cases.push('Actual pointer selection of an endpoint, straight edge and face normal saves automatically; the second picked axis preserves the first and derives right-handed Y');

  const committed=await read();await assert.rejects(()=>rpc({action:'invoke',scope:'scene',method:'CamPlacement.ApplyRegistration',payload:{expected:committed.expected,commandId:crypto.randomUUID(),setupId:committed.placements[0].setupId,placementId:committed.placements[0].id,resetWorkpieceFrame:true,registration:{method:'Alignment',modelFrame:'Workpiece',targetFrame:'Setup',unit:'mm',frame:{origin:[0,0,0],x:[1,0,0],y:[0,-1,0],z:[0,0,1]}}}}),/CAM_ALIGNMENT_FRAME_INVALID/);assert.deepEqual(await read(),committed);
  assert.deepEqual(await stopQuietOperationProbe(page),[]);cases.push('The entire live-edit lifecycle, including native geometry publication and resource readback, never sets global pending or adds a blocking progress overlay');
  await rpc({action:'undo'});await refresh();assert.notDeepEqual((await read()).workpieces,committed.workpieces);await rpc({action:'redo'});await refresh();assert.deepEqual((await read()).workpieces,committed.workpieces);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.workpieces,committed.workpieces);assert.deepEqual(reopened.placements,committed.placements);
  cases.push('Native commit independently rejects a forged left-handed frame; repository undo/redo and archive save/reopen retain automatic edits');return {cases,retained};
}

