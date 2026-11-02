import assert from 'node:assert/strict';
import { chromium } from 'playwright';
import { serveBrowserAsset } from './browserPackageRuntime.mjs';

const browser=await chromium.launch({channel:'msedge',headless:true});
try{
  const page=await browser.newPage();
  await page.route('**/*',serveBrowserAsset);await page.goto('http://localhost/');
  const result=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const {syncLeads,currentLeadPreview,leadCommand}=await import('/src/apps/five-axis-cam/webpage/cutLeads.mjs');
    const transform=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
    const domain={expected:{sequence:1},placements:[{id:'p',transform}]};
    const item={intentId:'a',placementId:'p',leads:[{role:'LeadIn',points:[[0,0,0],[1,0,0]]},{role:'LeadOut',points:[[1,1,0],[2,1,0]]}]};
    const view={scene:{domain,intentToolpaths:[{id:'a'}]},viewport:{scene:new THREE.Scene()},fiveAxisArea:'toolpath',fiveAxisUI:{selectionSnapshot:domain,tools:{toolpath:'reverse'},selectedIntentIds:['a']}};
    let resolveRequest;
    const context={isCurrentProject:()=>true,sceneProxy:{invoke:()=>new Promise(r=>resolveRequest=r)}};
    const ops={renderProject:()=>syncLeads(context,view,ops)};
    const lines=()=>{const out=[];view.fiveLeadOverlay?.group.traverse(n=>{if(n.isLine)out.push([n.userData.leadRole,n.material.color.getHex(),n.geometry.attributes.position.count]);});return out;};
    syncLeads(context,view,ops);resolveRequest({items:[item]});await new Promise(r=>setTimeout(r,0));
    const saved=lines();
    view.fiveAxisUI.tools.toolpath='path-start';ops.renderProject();const switched=lines();
    view.fiveAxisUI.tools.toolpath='leads';view.fiveAxisLeadsOpen=true;
    view.fiveLeads={scope:JSON.stringify([domain.expected,['a']]),ids:['a'],preview:{items:[{...item,leads:[]}]}};
    ops.renderProject();const draft=lines();
    await leadCommand(context,view,'close',ops);const cancelled=lines();
    view.scene.domain.expected={sequence:2};syncLeads(context,view,ops);
    const stale=currentLeadPreview(view);
    resolveRequest({items:[{...item,leads:[]}]});await new Promise(r=>setTimeout(r,0));const cleared=lines();
    view.scene.domain.expected={sequence:3};syncLeads(context,view,ops);const old=resolveRequest;
    view.scene.domain.expected={sequence:4};syncLeads(context,view,ops);resolveRequest({items:[item]});await new Promise(r=>setTimeout(r,0));
    old({items:[]});await new Promise(r=>setTimeout(r,0));
    return {saved,switched,draft,cancelled,stale,cleared,late:lines()};
  });
  const colors=[['LeadIn',0x55b9e7,2],['LeadOut',0x55b9e7,2]];
  assert.deepEqual(result.saved,colors);assert.deepEqual(result.switched,colors);
  assert.deepEqual(result.draft,[]);assert.deepEqual(result.cancelled,colors);
  assert.equal(result.stale,null);assert.deepEqual(result.cleared,[]);assert.deepEqual(result.late,colors);
  const batches=await page.evaluate(async()=>{
    const {syncLeads}=await import('/src/apps/five-axis-cam/webpage/cutLeads.mjs');
    const run=async(message)=>{
      const calls=[],view={scene:{domain:{expected:{sequence:1}},intentToolpaths:Array.from({length:480},(_,i)=>({id:String(i)}))},fiveAxisArea:'toolpath',fiveAxisUI:{tools:{toolpath:'reverse'}}};
      const context={isCurrentProject:()=>true,sceneProxy:{invoke:async(method,payload,options)=>{
        calls.push({count:payload.intentIds.length,timeoutMs:options?.timeoutMs});
        if(calls.length===1)throw Error(message);
        return {items:payload.intentIds.map(intentId=>({intentId,leads:[]}))};
      }}};
      syncLeads(context,view,{renderProject:()=>{}});await new Promise(r=>setTimeout(r,0));
      return {calls,items:view.fiveSavedLeads.preview?.items.length};
    };
    return {timeout:await run('SDO invocation timed out: CamProcess.PreviewLeads'),stale:await run('CAM_EXPECTED_SEQUENCE_MISMATCH'),geometry:await run('LEAD_FRAME_DEGENERATE')};
  });
  assert.equal(batches.timeout.calls.length,1,'a slow batch must not enqueue 480 duplicate reads');
  assert.equal(batches.stale.calls.length,1,'stale scene scope must not retry individual cuts');
  assert.equal(batches.timeout.calls[0].timeoutMs,120000);
  assert.equal(batches.geometry.items,480,'a contour geometry failure still permits the other previews');
  assert.deepEqual(batches.geometry.calls.filter(c=>c.count>1).map(c=>c.count),[256,224]);
  const restricted=await page.evaluate(async()=>{
    const {applyLeadToCut}=await import('/src/apps/five-axis-cam/webpage/cutLeads.mjs');let calls=0;
    const view={fiveAxisArea:'toolpath',fiveAxisUI:{tools:{toolpath:'leads'}},scene:{intentToolpaths:[{id:'mark',machiningKind:'Mark'},{id:'locked',machiningKind:'Cut',locked:true},{id:'off',machiningKind:'Cut',enabled:false}]}};
    const context={sceneProxy:{invoke:async()=>{calls++;throw Error('Unexpected edit');}}};
    for(const id of ['mark','locked','off','rapid'])await applyLeadToCut(context,view,id,{});
    return calls;
  });
  assert.equal(restricted,0,'marking, rapid, disabled and locked paths cannot receive leads');
  console.log('Saved lead visibility, colors, draft cancellation, clear, stale responses and cutting-only application browser checks passed');
}finally{await browser.close();}
