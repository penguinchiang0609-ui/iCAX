import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {serveBrowserAsset} from './browserPackageRuntime.mjs';
import {rapidListModel} from '../../apps/five-axis-cam/webpage/rapidListModel.mjs';
import {openRapid,rapidPick,renderRapidTree,renderRapid} from '../../apps/five-axis-cam/webpage/rapidPlanning.mjs';
import {commandAvailability} from '../../apps/five-axis-cam/webpage/commandAvailability.mjs';

function fixture(){
 const cuts=['a','b','c'].map((id,i)=>({id,setupId:'setup',placementId:'placement',number:String(20-i),enabled:true}));
 const setup={id:'setup',enabled:true,definitionOrder:['b','c','a'],cutGroups:[],rapidSettings:{strategy:'Quick',marginMm:.2,liftHeightMm:10,normalRetractDistanceMm:2,sideRetractDistanceMm:10,safeZMm:80},generatedGeometry:{status:'Missing'}};
 return {fiveAxisArea:'toolpath',fiveCamPage:'rapid',fiveAxisUI:{tools:{toolpath:'rapid'}},scene:{intentToolpaths:cuts,activePlacementId:'placement',domain:{expected:{epoch:'pending',mutationSequence:'1'},activeSetupId:'setup',setups:[setup],placements:[{id:'placement',setupId:'setup',workpieceId:'workpiece'}],workpieces:[{id:'workpiece',enabled:true,geometryRef:{url:'resource://workpiece'}}],fixtures:[]}}};
}
const setup=v=>v.scene.domain.setups[0],pairs=v=>rapidListModel(v).transitions.map(t=>`${t.from}/${t.to}`);
test('enabled definitions expose adjacent connections before machining or rapid generation without fabricating coordinates',()=>{
 const view=fixture(),original=structuredClone(view.scene),model=rapidListModel(view);
 assert.deepEqual(pairs(view),['b/c','c/a']);assert.deepEqual(model.operations.map(o=>o.order),['1','2','3']);
 for(const t of model.transitions){assert.equal(t.geometryStatus,'Missing');assert.equal(t.validation,'Unchecked');assert.equal(t.definitionOnly,true);assert.equal(Object.hasOwn(t,'points'),false);assert.equal(Object.hasOwn(t,'directions'),false);}
 assert.deepEqual(view.scene,original);assert.equal(commandAvailability(view,'five.tool.rapid').disabled,false);
});
test('connection identities follow sorting and exclude disabled cuts, groups and other setups',()=>{
 const view=fixture();setup(view).definitionOrder=['a','b','c'];setup(view).cutGroups=[{id:'disabled-group',enabled:false}];view.scene.intentToolpaths[1].groupId='disabled-group';
 view.scene.intentToolpaths.push({id:'elsewhere',setupId:'other',number:'1',enabled:true});assert.deepEqual(pairs(view),['a/c']);
 setup(view).cutGroups[0].enabled=true;assert.deepEqual(pairs(view),['a/b','b/c']);setup(view).definitionOrder=['c','b','a'];assert.deepEqual(pairs(view),['c/b','b/a']);
 view.scene.intentToolpaths[1].enabled=false;assert.deepEqual(pairs(view),['c/a']);view.scene.intentToolpaths[0].enabled=false;assert.deepEqual(pairs(view),[]);assert.match(renderRapidTree(view),/只有一条/);
});
test('only current adjacent pairs acquire generated data; newly adjacent pairs stay pending',()=>{
 const view=fixture(),saved={from:'c',to:'a',geometryStatus:'Generated',validation:'Unchecked',frame:'Setup',referencePoint:'ToolTip',points:[[0,0,1],[5,0,1]],directions:[[0,0,1],[0,0,1]],policy:{strategy:'Quick',marginMm:.2,liftHeightMm:10}};
 setup(view).generatedGeometry={status:'Current',operations:[],transitions:[saved]};const before=structuredClone(saved),model=rapidListModel(view);
 assert.equal(model.transitions[0].definitionOnly,true);assert.equal(model.transitions[1].geometryStatus,'Generated');assert.equal(model.transitions[1].fromOrder,'2');assert.deepEqual(saved,before);
});
test('pending rows support selection and status while withholding geometry-dependent controls',()=>{
 const view=fixture();openRapid(view);assert.equal(view.fiveRapid.id,'b/c');rapidPick(view,'c/a');assert.equal(view.fiveRapid.id,'c/a');
 const tree=renderRapidTree(view),panel=renderRapid(view,true);assert.match(tree,/2 → 3/);assert.match(panel,/role="status">未生成/);assert.match(panel,/value="c\/a" selected/);
 assert.doesNotMatch(panel,/five\.rapid\.(check|path-edit|preview|apply)|data-five-rapid-field="marginMm"/);assert.equal(setup(view).generatedGeometry.status,'Missing');
});
test('the selected connection remains the same identity when generated geometry arrives',()=>{
 const view=fixture();openRapid(view);rapidPick(view,'c/a');setup(view).generatedGeometry={status:'Current',revision:'geometry',operations:[{intentId:'b',order:'1'},{intentId:'c',order:'2'},{intentId:'a',order:'3'}],transitions:[{from:'c',to:'a',inputKey:'path-key',endpointStatus:'Current',geometryStatus:'Generated',validation:'Unchecked',policy:{strategy:'Quick',marginMm:.2,liftHeightMm:10},frame:'Setup',referencePoint:'ToolTip',points:[[0,0,1],[5,0,1]],directions:[[0,0,1],[0,0,1]]}]};
 openRapid(view);assert.equal(view.fiveRapid.id,'c/a');assert.match(renderRapid(view,true),/已生成/);assert.match(renderRapidTree(view),/2 → 3/);
});

test('browser pending connections select and become generated without replacing panels, input, canvas, listeners or latest scrolling',async()=>{
 const require=createRequire(import.meta.url),{chromium}=require('playwright');
 const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:1350,height:900}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://127.0.0.1:19372/**',route=>new URL(route.request().url()).pathname==='/'?route.fulfill({contentType:'text/html',body:'<style>main{display:grid;grid-template-columns:300px 500px 500px}.left,.right{height:420px;overflow:auto}.nested{height:60px;overflow:auto}.nested div{height:600px}.spacer{height:1400px}.five-tree-row{display:block;height:48px}input{width:120px}</style><main><aside class="left"></aside><canvas></canvas><aside class="right"></aside></main>'}):serveBrowserAsset(route));
  await page.goto('http://127.0.0.1:19372/');
  for(const focus of ['left','right']){
   await page.evaluate(async({data,focus})=>{
    const p=await import('/src/apps/five-axis-cam/webpage/rapidPlanning.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
    const view=data,s=view.scene.domain.setups[0];view.scene.intentToolpaths=Array.from({length:50},(_,i)=>({id:'cut'+i,setupId:s.id,placementId:'placement',number:String(i+1),enabled:true}));s.definitionOrder=view.scene.intentToolpaths.map(c=>c.id);
    const f=window.pendingFixture={view,p,events:0};p.openRapid(view);
    const extra=side=>`<input data-five-key="${side}-input" id="${side}-input" value="123456"><div class="nested" data-five-key="${side}-nested"><div></div></div>`;
    f.render=()=>patchWorkspacePanels(document.querySelector('main'),[['.left',extra('left')+p.renderRapidTree(view)+'<div class="spacer" data-five-key="left-spacer"></div>'],['.right',extra('right')+p.renderRapid(view,true)+'<div class="spacer" data-five-key="right-spacer"></div>']]);f.render();
    document.querySelector('main').onclick=e=>{const row=e.target.closest('[data-five-rapid-id]');if(row){p.rapidPick(view,row.dataset.fiveRapidId);f.render();}};
    f.focus=focus;
   },{data:fixture(),focus});
   assert.equal(await page.locator('[data-five-rapid-id]').count(),49);assert.match(await page.locator('.right').innerText(),/未生成/);
   assert.equal(await page.locator('[data-five-command="five.rapid.path-edit"]').count(),0);
   await page.locator('[data-five-rapid-id="cut29/cut30"]').click();assert.equal(await page.locator('[data-five-rapid-field="connection"]').inputValue(),'cut29/cut30');
   const before=await page.evaluate(()=>{
    const f=pendingFixture;f.input=document.querySelector('#'+f.focus+'-input');f.canvas=document.querySelector('canvas');f.nodes=['.left','.right','.left .nested','.right .nested'].map(x=>document.querySelector(x));
    f.input.addEventListener('input',()=>f.events++);f.input.focus({preventScroll:true});f.input.setSelectionRange(1,4);
    f.refresh=new Promise(r=>f.release=r).then(()=>{const s=f.view.scene.domain.setups[0];s.generatedGeometry={status:'Current',revision:'generated',operations:[],transitions:[{from:'cut29',to:'cut30',geometryStatus:'Generated',endpointStatus:'Current',inputKey:'generated-route',validation:'Unchecked',frame:'Setup',referencePoint:'ToolTip',policy:{strategy:'Quick',marginMm:.2,liftHeightMm:10},points:[[0,0,1],[5,0,1]],directions:[[0,0,1],[0,0,1]]}]};f.p.openRapid(f.view);f.render();});
    f.nodes.forEach((n,i)=>n.scrollTop=30+i*5);f.nodes.forEach((n,i)=>n.scrollTop=100+i*10);
    return {scroll:f.nodes.map(n=>n.scrollTop),selection:[f.input.selectionStart,f.input.selectionEnd]};
   });
   assert.ok(before.scroll.every(x=>x>0));await page.evaluate(async()=>{pendingFixture.release();await pendingFixture.refresh;});
   const after=await page.evaluate(()=>{const f=pendingFixture;f.input.dispatchEvent(new Event('input'));return {input:f.input===document.querySelector('#'+f.focus+'-input'),canvas:f.canvas===document.querySelector('canvas'),nodes:f.nodes.every(n=>n.isConnected),focus:document.activeElement===f.input,selection:[f.input.selectionStart,f.input.selectionEnd],scroll:f.nodes.map(n=>n.scrollTop),events:f.events,id:f.view.fiveRapid.id};});
   assert.deepEqual(after,{input:true,canvas:true,nodes:true,focus:true,selection:before.selection,scroll:before.scroll,events:1,id:'cut29/cut30'});
   assert.match(await page.locator('[data-five-rapid-id="cut29/cut30"]').innerText(),/已生成/);assert.equal(await page.locator('[data-five-rapid-id*="cut"]').count(),49);assert.equal(await page.locator('[data-five-command="five.rapid.path-edit"]').count(),1);
   // When geometry disappears, its fields must disappear without assigning the
   // removed field's focus to a different input at the same DOM position.
   await page.locator('[data-five-rapid-field="marginMm"]').focus();
   await page.evaluate(()=>{const f=pendingFixture;f.view.scene.domain.setups[0].generatedGeometry={status:'Missing'};f.p.openRapid(f.view);f.render();});
   assert.equal(await page.locator('[data-five-rapid-field="marginMm"]').count(),0);assert.equal(await page.evaluate(()=>['left-input','right-input'].includes(document.activeElement.id)),false);
  }
  assert.deepEqual(errors,[]);
 }finally{await browser.close();}
});
