import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyOrdering({page,rpc,calls,output}){
  const generationCount=calls.filter(c=>c.method==='CamPlan.Generate'||c.method==='CamRapid.Generate').length;
  console.log('Order: manual clicks, prefix, history, automatic, persistence');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),geom=state=>state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const button=action=>page.locator(`[data-five-command="five.order.${action}"]`).last(),idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveGeneration?.running&&!fixture.view.fiveRapidBatch?.running);
  const prefix=()=>page.evaluate(()=>fixture.view.fiveOrder.prefix);
  const label=id=>page.locator(`[data-five-order-id="${id}"]`);
  const tree=id=>page.locator(`[data-five-intent="${id}"]`);
  // Reopened projects can already carry a manual order. Establish the automatic
  // baseline explicitly before comparing the later automatic reset against it.
  await command('five.tool.auto-order');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const before=await read(),base=geom(before),ids=base.operations.map(o=>o.intentId);assert.ok(ids.length>=7);
  await command('five.tool.manual-order');
  assert.equal(await page.getByRole('dialog',{name:'手动排序'}).isVisible(),true);
  assert.equal(await page.locator('.five-dock').isVisible(),false);
  assert.ok(await page.locator('[data-five-order-id]').evaluateAll(nodes=>nodes.every(n=>getComputedStyle(n).backgroundColor==='rgb(153, 153, 153)')));
  await label(ids.at(-1)).click();assert.deepEqual(await prefix(),[ids.at(-1)]);assert.equal(await label(ids.at(-1)).innerText(),'1');
  assert.equal(await label(ids.at(-1)).evaluate(n=>getComputedStyle(n).backgroundColor),'rgb(216, 61, 61)');
  await tree(ids[1]).click();assert.deepEqual(await prefix(),[ids.at(-1),ids[1]]);
  const orderBefore=(await read()).setups.find(s=>s.id===before.activeSetupId).definitionOrder;
  await button('undo').click();await idle();assert.deepEqual(await prefix(),[]);assert.notDeepEqual((await read()).setups.find(s=>s.id===before.activeSetupId).definitionOrder,orderBefore);
  await button('redo').click();await idle();assert.deepEqual(await prefix(),[]);assert.deepEqual((await read()).setups.find(s=>s.id===before.activeSetupId).definitionOrder,orderBefore);
  assert.deepEqual(geom(await read()).resource,base.resource);await button('close').click();assert.deepEqual(geom(await read()).resource,base.resource);
  await command('five.tool.manual-order');await button('after').click();await tree(ids[3]).click();assert.deepEqual(await prefix(),ids.slice(0,4));
  await label(ids[1]).click({button:'right'});await page.getByRole('menuitem',{name:'从此排序'}).click();assert.deepEqual(await prefix(),ids.slice(0,2));
  await tree(ids.at(-1)).click();assert.deepEqual(await prefix(),[...ids.slice(0,2),ids.at(-1)]);
  // Pick an unselected actual generated path on the canvas, away from labels/dialog.
  const point=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const v=fixture.view,viewport=v.viewport,box=viewport.renderer.domElement.getBoundingClientRect(),state=v.fiveOrder,chosen=state.prefix;
    const paths=state.base.operations.map(op=>{const p=v.scene.domain.placements.find(p=>p.id===op.placementId),matrix=new THREE.Matrix4().set(...p.transform);return {id:op.intentId,points:op.parts.flatMap(part=>part.points.map(point=>{const q=new THREE.Vector3(...point).applyMatrix4(matrix).project(viewport.camera);return [box.x+(q.x+1)*box.width/2,box.y+(1-q.y)*box.height/2,q.z];}))};});
    const distance=(p,a,b)=>{const dx=b[0]-a[0],dy=b[1]-a[1],t=Math.max(0,Math.min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/(dx*dx+dy*dy||1)));return Math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy);};
    for(const path of paths){if(chosen.includes(path.id))continue;for(let i=1;i<path.points.length;++i){const a=path.points[i-1],b=path.points[i],p=[(a[0]+b[0])/2,(a[1]+b[1])/2];if(p[0]<650||p[0]>1350||p[1]<210||p[1]>790||Math.abs(b[2])>1)continue;
      if(paths.some(other=>other.id!==path.id&&other.points.some((q,j)=>j&&distance(p,other.points[j-1],q)<12)))continue;
      if([...document.querySelectorAll('[data-five-order-id]')].some(n=>{const r=n.getBoundingClientRect();return p[0]>r.left-12&&p[0]<r.right+12&&p[1]>r.top-12&&p[1]<r.bottom+12;}))continue;
      return {id:path.id,x:p[0],y:p[1]};}}
  });
  assert.ok(point,'an exposed generated path is available');await page.mouse.click(point.x,point.y);
  assert.equal((await prefix()).at(-1),point.id);await page.screenshot({path:resolve(output,'automotive-manual-order-preview.png')});
  const selected=await prefix(),expected=[...selected,...ids.filter(id=>!selected.includes(id))];
  // Acceptance and receipt lost: same immutable request; controls and scrolling survive.
  const prior=calls.filter(c=>c.method==='CamPlan.SetOrder').length;
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.five-position-body::after{content:"";display:block;height:1000px}.five-position-body{height:130px;overflow:auto}.cam-info-pane{max-height:190px!important}'});
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.SetOrder';fixture.hold=true;fixture.loseMethod='CamPlan.SetOrder';fixture.orderCanvas=document.querySelector('canvas');fixture.orderMode=document.querySelector('[data-five-command="five.order.after"]');
    const proxy=fixture.context.sceneProxy,original=proxy.invoke.bind(proxy);proxy.invoke=async(method,payload)=>{if(method==='CamScene.QueryCommand'){proxy.invoke=original;throw Error('Injected lost receipt');}return original(method,payload);};});
  await button('apply').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')];nodes.forEach((n,i)=>n.scrollTop=20+i*6);return nodes.map(n=>n.scrollTop);});assert.ok(scroll.every(n=>n>0));
  await page.evaluate(()=>fixture.release());await idle();assert.match(await page.evaluate(()=>fixture.view.error),/尚未确认/);
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')].map(n=>n.scrollTop)),scroll);
  assert.equal(await page.evaluate(()=>fixture.orderCanvas===document.querySelector('canvas')&&fixture.orderMode===document.querySelector('[data-five-command="five.order.after"]')),true);
  await stress.evaluate(n=>n.remove());await button('apply').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const retried=calls.filter(c=>c.method==='CamPlan.SetOrder').slice(prior);assert.equal(retried.length,2);assert.deepEqual(retried[0].payload,retried[1].payload);
  const manual=geom(await read());assert.deepEqual(manual.operations.map(o=>o.intentId),expected);assert.deepEqual(manual.resource,base.resource);
  assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);
  for(let i=1;i<manual.operations.length;++i){const t=manual.transitions[i-1];assert.equal(t.from,manual.operations[i-1].intentId);assert.equal(t.to,manual.operations[i].intentId);assert.equal(t.frame,'Setup');assert.ok(t.points.length>=2);assert.equal(t.laser,false);}
  await page.screenshot({path:resolve(output,'automotive-manual-order-applied.png')});
  await rpc({action:'undo'});await command('five.refresh');assert.deepEqual(geom(await read()),base);
  await rpc({action:'redo'});await command('five.refresh');assert.deepEqual(geom(await read()),manual);
  await command('five.tool.auto-order');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const automatic=geom(await read());assert.equal(automatic.ordering.mode,'Automatic');
  assert.deepEqual([...automatic.operations.map(o=>o.intentId)].sort(),[...ids].sort());
  // A route optimizer may find a better legal route from a user's feasible
  // seed. Check machining precedence and stable repeated optimization rather
  // than requiring the former nearest-neighbour algorithm's exact sequence.
  const phase=op=>op.machiningKind==='Mark'?0:op.boundaryRole==='OuterTrim'?2:1;
  const lastPhase=new Map();for(const op of automatic.operations){assert.ok((lastPhase.get(op.placementId)??0)<=phase(op));lastPhase.set(op.placementId,phase(op));}
  const repeated=await invoke('CamPlan.SetOrder',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:(await read()).activeSetupId,mode:'Automatic'});
  assert.equal(repeated.changed,false);assert.deepEqual(geom(await read()).operations,automatic.operations);
  await page.screenshot({path:resolve(output,'automotive-auto-order.png')});
  await command('five.tool.manual-order');await tree(ids[0]).click();
  await invoke('CamCut.Reverse',{commandId:crypto.randomUUID(),expected:(await read()).expected,intentIds:[ids[0]]});await command('five.refresh');
  assert.equal(await button('apply').isDisabled(),true);assert.equal(geom(await read()).status,'Stale');
  await rpc({action:'undo'});await command('five.refresh');assert.equal(await button('apply').isDisabled(),true,'a stale draft must be reopened even after undo');
  await button('close').click();
  // A late response cannot switch pages or steal search focus/selection.
  await rpc({action:'undo'});await command('five.refresh');
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.SetOrder';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.tool.auto-order');});
  await page.waitForFunction(()=>fixture.held);await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
  const search=page.locator('[data-five-search]');await search.fill('order-focus');await search.evaluate(n=>{n.focus();n.setSelectionRange(1,4);});
  await page.evaluate(()=>fixture.release());await idle();assert.deepEqual(await search.evaluate(n=>[document.activeElement===n,n.selectionStart,n.selectionEnd]),[true,1,4]);
  await rpc({action:'undo'});assert.deepEqual(geom(await read()),manual);
  const saved=await rpc({action:'save-reopen'});assert.deepEqual(geom(saved),manual);
  await rpc({action:'export-project',path:resolve(output,'Automotive-ordered-toolpaths.i5cam')});
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.tool.manual-order');await button('close').click();
  assert.deepEqual(await page.locator('[data-five-intent]').evaluateAll(nodes=>nodes.map(n=>n.dataset.fiveIntent)),expected);
  assert.equal(await page.evaluate(()=>fixture.view.fiveGeneratedPreview.group.children.filter(n=>n.userData.generatedRapid).length),0,'ordering displays contours without manufacturing travel');
  assert.equal(calls.filter(c=>c.method==='CamPlan.Generate'||c.method==='CamRapid.Generate').length,generationCount,'ordering never triggers generation');
  await page.evaluate(() => fixture.view.viewport.fitView());await page.screenshot({path:resolve(output,'automotive-order-reopened.png')});
  writeFileSync(resolve(output,'ordering-report.json'),JSON.stringify({passed:true,cases:['actual label and canvas click order','from-start and from-here prefix','grey/red draft labels and native forward/backward tree edits','cancel leaves project unchanged','manual persistent order and updated rapid endpoints','same-command retry after lost response and receipt','canvas controls dual nested scrolling preserved','transaction undo redo','one-click automatic replaces manual order','stale drafts cannot apply','late response preserves focus/selection','embedded save reopen and displayed order'],coverage:{native:true,browser:'Edge',standaloneCef:false,machineMotion:false,collision:false},operations:ids.length},null,2));
}
