import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import {runInNewContext} from 'node:vm';
import {escapeAttr,escapeText} from '../../iCAX-UI/UI/html.mjs';

const source=readFileSync(new URL('../../iCAX-UI/SDK/AppShell/app/bootstrap.mjs',import.meta.url),'utf8');
function between(start,end){
  const first=source.indexOf(start),last=source.indexOf(end,first+start.length);
  assert.ok(first>=0&&last>first);
  return source.slice(first,last);
}

test('busy ribbon only permits its declared alert command from the alert area',()=>{
  const calls=[];
  const sandbox={busy:true,alerts:{commandId:'five.alerts.toggle'},
    isActiveProjectBusy:()=>sandbox.busy,getActiveRibbonDefinition:()=>({alerts:sandbox.alerts}),
    closeRibbonCommandMenu(){},getActiveProductModule:()=>({shouldPreserveRibbonCommandSurface:()=>true}),
    buildProductContext:()=>({}),runAction:work=>work(),actions:{executeRibbonCommand:id=>calls.push(id)}};
  const branch=between('  } else if (action === "ribbon-command") {','  } else if (action === "window-minimize") {').replace(/^  } else /,'');
  runInNewContext(`globalThis.click=target=>{const action='ribbon-command';${branch}}};`,sandbox);
  const target=(commandId,inside)=>({dataset:{commandId},closest:()=>inside?{}:null});
  sandbox.click(target('five.alerts.toggle',true));
  sandbox.click(target('five.alerts.toggle',false));
  sandbox.click(target('job.export',true));
  sandbox.alerts={commandId:''};sandbox.click(target('',true));
  sandbox.alerts=undefined;sandbox.click(target('five.alerts.toggle',true));
  assert.deepEqual(calls,['five.alerts.toggle']);
  sandbox.busy=false;sandbox.click(target('job.export',false));
  assert.deepEqual(calls,['five.alerts.toggle','job.export']);
});

function ribbonFixture(){
  const document={body:{},documentElement:{},activeElement:null};
  const focusCalls=[];
  const makeRibbon=(ids=['first','second'])=>{
    const row={scrollLeft:0};
    const button=(kind,id)=>({kind,dataset:{ribbonAlertId:id},disabled:false,matches:selector=>selector==='.ribbon-alerts-heading'&&kind==='heading',
      focus(options){focusCalls.push({button:this,preventScroll:options.preventScroll});document.activeElement=this;}});
    const heading=button('heading'),items=ids.slice(0,2).map((id,i)=>button(i,id));
    const alerts={contains:node=>node===heading||items.includes(node),querySelectorAll:()=>items};
    return {row,heading,items,alerts,contains:node=>alerts.contains(node),
      querySelector:selector=>selector==='.ribbon-commands'?row:selector==='[data-ribbon-alerts]'?alerts:selector==='.ribbon-alerts-heading'?heading:null,
      querySelectorAll:()=>items,
      set outerHTML(_html){if(alerts.contains(document.activeElement))document.activeElement=document.body;current=makeRibbon(nextIds);}};
  };
  let current=makeRibbon(),nextIds=['first','second'];
  const state={activeProjectProxy:{projectId:'one'},activeProjectState:{projectId:'one'}};
  const sandbox={state,document,root:{querySelector:selector=>selector==='.product-ribbon'?current:null},captureRibbonWorkspace:()=>()=>{},
    renderProductRibbon(){sandbox.onRender?.();return '<nav></nav>';},positionScrollableRibbonMenu(){},
    refreshProjectSaveControl(){},refreshProjectHistoryControls(){sandbox.onControls?.();},refreshProjectNavigationControls(){}};
  const method=between('  refreshProductRibbon() {','\n  async windowCommand(');
  runInNewContext(`globalThis.actions={${method}};`,sandbox);
  return {sandbox,document,state,focusCalls,get current(){return current;},set nextIds(value){nextIds=value;}};
}

test('ribbon refresh preserves horizontal scroll and focused alert heading or row',()=>{
  for(const kind of ['heading',1]){
    const f=ribbonFixture();f.current.row.scrollLeft=184;
    f.document.activeElement=kind==='heading'?f.current.heading:f.current.items[kind];
    f.sandbox.actions.refreshProductRibbon();
    assert.equal(f.current.row.scrollLeft,184);
    assert.equal(f.document.activeElement,kind==='heading'?f.current.heading:f.current.items[kind]);
    assert.equal(f.focusCalls.length,1);assert.equal(f.focusCalls[0].preventScroll,true);
  }
});

test('ribbon refresh leaves live editor focus and newer focus alone',()=>{
  const f=ribbonFixture(),input={kind:'editor'};f.document.activeElement=input;
  f.sandbox.actions.refreshProductRibbon();assert.equal(f.document.activeElement,input);assert.equal(f.focusCalls.length,0);
  f.document.activeElement=f.current.heading;
  f.sandbox.onControls=()=>{f.document.activeElement=input;};
  f.sandbox.actions.refreshProductRibbon();assert.equal(f.document.activeElement,input);assert.equal(f.focusCalls.length,0);
});

test('ribbon refresh does not restore another project or redirect a removed alert row',()=>{
  const switched=ribbonFixture();switched.current.row.scrollLeft=200;switched.document.activeElement=switched.current.heading;
  switched.sandbox.onRender=()=>{switched.state.activeProjectProxy={projectId:'two'};switched.state.activeProjectState={projectId:'two'};};
  switched.sandbox.actions.refreshProductRibbon();assert.equal(switched.current.row.scrollLeft,0);assert.equal(switched.focusCalls.length,0);
  const removed=ribbonFixture();removed.document.activeElement=removed.current.items[1];removed.nextIds=['first'];
  removed.sandbox.actions.refreshProductRibbon();assert.equal(removed.document.activeElement,removed.document.body);assert.equal(removed.focusCalls.length,0);
});

test('alert focus follows its id when a preceding row disappears or is inserted',()=>{
  const removed=ribbonFixture();removed.document.activeElement=removed.current.items[1];removed.nextIds=['second'];
  removed.sandbox.actions.refreshProductRibbon();assert.equal(removed.document.activeElement,removed.current.items[0]);
  const inserted=ribbonFixture();inserted.document.activeElement=inserted.current.items[0];inserted.nextIds=['operation','first','second'];
  inserted.sandbox.actions.refreshProductRibbon();assert.equal(inserted.document.activeElement,inserted.current.items[1]);
});

test('alert focus never transfers to a different, hidden, missing-id or ambiguous row',()=>{
  for(const nextIds of [['replacement','second'],['operation','replacement','first'],['first','first']]){
    const f=ribbonFixture();f.document.activeElement=f.current.items[0];f.nextIds=nextIds;
    f.sandbox.actions.refreshProductRibbon();assert.equal(f.document.activeElement,f.document.body);assert.equal(f.focusCalls.length,0);
  }
  const noId=ribbonFixture();noId.document.activeElement=noId.current.items[0];noId.document.activeElement.dataset.ribbonAlertId='';noId.nextIds=['','second'];
  noId.sandbox.actions.refreshProductRibbon();assert.equal(noId.document.activeElement,noId.document.body);assert.equal(noId.focusCalls.length,0);
});

test('alert renderer emits an escaped stable id for each overview row',()=>{
  const sandbox={escapeAttr,escapeText};
  runInNewContext(between('function renderRibbonAlerts(','\nfunction renderRibbonCommand(')+'\nglobalThis.render=renderRibbonAlerts;',sandbox);
  const html=sandbox.render({label:'Alerts',commandId:'five.alerts.toggle',expanded:false,items:[{id:'alert" <unsafe>',title:'Collision',detail:'Details',severity:'error'}]});
  assert.match(html,/data-ribbon-alert-id="alert&quot; &lt;unsafe&gt;"/);
  assert.doesNotMatch(html,/<unsafe>/);
});

test('each overview row uses its declared destination instead of the overview heading command',()=>{
  const sandbox={escapeAttr,escapeText};runInNewContext(between('function renderRibbonAlerts(','\nfunction renderRibbonCommand(')+'\nglobalThis.render=renderRibbonAlerts;',sandbox);
  const html=sandbox.render({label:'告警',commandId:'overview',expanded:false,items:[{id:'collision',title:'碰撞',severity:'error',commandId:'open.collision'},{id:'light',title:'光路',severity:'warning',commandId:'open.fixture'}]});
  assert.match(html,/data-ribbon-alert-id="collision"[^>]*data-command-id="open.collision"/);assert.match(html,/data-ribbon-alert-id="light"[^>]*data-command-id="open.fixture"/);
});

test('busy ribbon permits only a currently declared enabled alert row and matching id',()=>{
  const calls=[],alerts={commandId:'overview',items:[{id:'collision',commandId:'open.collision',disabled:false},{id:'light',commandId:'open.fixture',disabled:true}]};
  const sandbox={isActiveProjectBusy:()=>true,getActiveRibbonDefinition:()=>({alerts}),closeRibbonCommandMenu(){},getActiveProductModule:()=>({shouldPreserveRibbonCommandSurface:()=>true}),buildProductContext:()=>({}),runAction:fn=>fn(),actions:{executeRibbonCommand:id=>calls.push(id)}};
  const branch=between('  } else if (action === "ribbon-command") {','  } else if (action === "window-minimize") {').replace(/^  } else /,'');runInNewContext(`globalThis.click=target=>{const action='ribbon-command';${branch}}};`,sandbox);
  const target=(commandId,id,inside=true)=>({dataset:{commandId,ribbonAlertId:id},closest:()=>inside?{}:null});
  sandbox.click(target('open.collision','collision'));sandbox.click(target('open.fixture','light'));sandbox.click(target('open.collision','light'));sandbox.click(target('open.collision','collision',false));
  assert.deepEqual(calls,['open.collision']);
});
