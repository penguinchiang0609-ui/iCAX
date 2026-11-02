import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,sep} from 'node:path';

const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
const output=resolve(process.env.ICAX_TEST_OUTPUT||'output/tests/fixture-projection');
try{
  const page=await browser.newPage(),errors=[],cases=[];
  page.on('pageerror',error=>errors.push(error.message));
  const root=fileURLToPath(new URL('../../',import.meta.url));
  await page.route('http://fixture-projection.test/**',route=>{
    const pathname=new URL(route.request().url()).pathname;
    if(pathname==='/')return route.fulfill({contentType:'text/html',body:'<body></body>'});
    const path=resolve(root,pathname.replace(/^\/src\//,''));
    if(!pathname.startsWith('/src/')||!path.startsWith(root.replace(/[\\/]$/,'')+sep))return route.abort();
    return route.fulfill({contentType:'text/javascript',body:readFileSync(path,'utf8')});
  });
  await page.goto('http://fixture-projection.test/');
  await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{display:inline-block;width:300px;height:250px;overflow:auto;vertical-align:top}.nested{height:90px;overflow:auto}.spacer{height:1400px}'});
  await page.evaluate(async()=>{
    const {refreshMachiningState,mergeDomainState}=await import('/src/apps/five-axis-cam/webpage/domainActions.mjs');
    const {patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
    const {fixtureCutClearanceReady,renderFixtureCutClearance}=await import('/src/apps/five-axis-cam/webpage/fixtureDesign.mjs');
    let generation=0;
    const assembly=()=>({id:`generation-${++generation}`,validity:'Current',cutClearance:{status:'Clear',validity:'Current',operationCount:2,clearanceMm:2,beamRadiusMm:.5,headScope:'BuiltinFrusta'},openingPreclearance:{validity:'Current',status:'Prepared',boundaryCount:3,unclassifiedFaceCount:0,marginMm:1,toolpathRequired:false,productionQualified:false}});
    const design=()=>({recipe:{cutClearanceMode:'Toolpath'},assembly:assembly(),candidate:assembly()});
    const base=()=>({expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',jobId:'job',inputRevision:'1',placements:[],workpieces:[],fixtures:[],fixtureUnits:[],setups:[{id:'setup',generatedGeometry:{status:'Current'},fixtureDesign:{}}],intentToolpaths:[{id:'cut',setupId:'setup',poseRef:{version:'1'}}]});
    const f=window.fixture={base,design,view:{scene:{}},reads:0,renders:0,sceneRefreshes:0};
    f.context={isCurrentProject:()=>true,sceneProxy:{async invoke(method){if(method!=='CamScene.ReadState')throw Error(method);++f.reads;if(f.hold)await new Promise(resolve=>f.release=resolve);if(f.fail)throw Error('Injected state read failure');return structuredClone(f.reply);}}};
    f.reset=(withCandidate=true,unitOnly=false)=>{
      const domain=base();if(withCandidate&&!unitOnly)domain.setups[0].fixtureDesign=design();
      if(withCandidate)domain.fixtureUnits=[{id:'unit',setupId:'setup',design:design()},{id:'other',setupId:'other-setup',design:design()}];
      mergeDomainState(f.view.scene,domain);f.reads=f.renders=f.sceneRefreshes=0;f.hold=f.fail=false;f.selected=unitOnly?'unit':'';
      f.request={expected:structuredClone(domain.expected)};
      f.result={statePatch:{expected:{epoch:'epoch',mutationSequence:'2'},inputRevision:'2',setup:{id:'setup',generatedGeometry:{status:'Stale'}},intentToolpaths:[{id:'cut',setupId:'setup',poseRef:{version:'2'}}]}};
      f.reply=structuredClone(domain);f.reply.expected=f.result.statePatch.expected;f.reply.intentToolpaths=f.result.statePatch.intentToolpaths;
      for(const d of [f.reply.setups[0].fixtureDesign,...f.reply.fixtureUnits.filter(u=>u.setupId==='setup').map(u=>u.design)])for(const a of [d.candidate,d.assembly].filter(Boolean)){a.validity='Stale';Object.assign(a.cutClearance,{status:'Unchecked',validity:'Stale'});Object.assign(a.openingPreclearance,{status:'Unchecked',validity:'Stale'});}
    };
    f.currentDesign=()=>f.selected?f.view.scene.domain.fixtureUnits.find(u=>u.id===f.selected).design:f.view.scene.domain.setups[0].fixtureDesign;
    f.projection=(domain,stale=false)=>{
      const one=d=>Object.fromEntries(['candidate','assembly'].filter(k=>d?.[k]).map(k=>[k,{id:d[k].id,validity:stale?'Stale':'Current',cutClearance:{validity:stale?'Stale':'Current',status:stale?'Unchecked':'Clear'},openingPreclearance:{validity:stale?'Stale':'Current',status:stale?'Unchecked':'Prepared'}}]));
      return {setup:one(domain.setups[0].fixtureDesign),units:Object.fromEntries(domain.fixtureUnits.filter(u=>u.setupId==='setup').map(u=>[u.id,one(u.design)]))};
    };
    f.render=()=>{++f.renders;const d=f.currentDesign();patchWorkspacePanels(document.body,[['.cam-context-pane','<input data-five-key="left-input" aria-label="Left field" value="left"><div class="spacer"></div>'],['.cam-info-pane',`<input data-five-key="axis" aria-label="Axis K" value="0.8660254"><div class="nested" data-five-key="nested"><div class="spacer"></div></div><div data-five-key="result">${renderFixtureCutClearance(d.candidate||d.assembly)}</div><button data-five-key="deliver" ${fixtureCutClearanceReady(d,d.candidate||d.assembly)?'':'disabled'}>Deliver</button><div class="spacer"></div>`]]);};
    f.ops={renderProject:f.render,async refreshSceneState(){++f.sceneRefreshes;throw Error('Metadata edit must not reload scene resources');}};
    f.run=()=>refreshMachiningState(f.context,f.view,f.ops,f.request,f.result);
    document.body.innerHTML='<canvas></canvas><section class="cam-context-pane"></section><section class="cam-info-pane"></section>';
    f.reset();f.render();
  });
  // The lightweight path remains available when there is no fixture result.
  const light=await page.evaluate(async()=>{const f=fixture;f.reset(false);await f.run();return {reads:f.reads,pose:f.view.scene.domain.intentToolpaths[0].poseRef.version,expected:f.view.scene.domain.expected.mutationSequence};});
  assert.deepEqual(light,{reads:0,pose:'2',expected:'2'});cases.push('No fixture results retain the compact receipt path');

  const local=await page.evaluate(async()=>{
    const f=fixture;f.reset(false);const domain=f.view.scene.domain;
    domain.intentToolpaths=['a','b','c','d'].map(id=>({id,setupId:'setup'}));
    const operations=domain.intentToolpaths.map(c=>({intentId:c.id,status:'Current'}));
    const transitions=['a/b','b/c','c/d'].map(pair=>({from:pair[0],to:pair[2],endpointStatus:'Current',geometryStatus:'Generated',validation:'Clear',points:[[1,2,3],[4,5,6]]}));
    domain.setups[0].generatedGeometry={status:'Current',operations,transitions,rapidGeneratedCount:'3'};
    const patch=f.result.statePatch;patch.intentToolpaths=structuredClone(domain.intentToolpaths);patch.operationOrder=['a','b','c','d'];
    patch.operationStates={a:'Current',b:'Stale',c:'Current',d:'Current'};
    patch.transitionStates=Object.fromEntries(transitions.map(t=>[`${t.from}/${t.to}`,{endpointStatus:t.from==='b'||t.to==='b'?'Stale':'Current',geometryStatus:t.from==='b'||t.to==='b'?'Stale':'Generated',validation:t.from==='b'||t.to==='b'?'Unchecked':'Clear'}]));
    patch.setup.generatedGeometry={status:'Stale',rapidGeneratedCount:'1'};f.render();
    const input=document.querySelector('[aria-label="Axis K"]'),canvas=document.querySelector('canvas'),unchangedPoints=transitions[2].points;
    let events=0;input.addEventListener('input',()=>++events);input.focus({preventScroll:true});input.setSelectionRange(2,5);
    const selectors=['.cam-context-pane','.cam-info-pane','.nested'],scroll=selectors.map((s,i)=>{const n=document.querySelector(s);n.scrollTop=90+i*20;return n.scrollTop;});
    await f.run();input.dispatchEvent(new Event('input'));
    const next=domain.setups[0].generatedGeometry;
    return {reads:f.reads,states:next.transitions.map(t=>t.geometryStatus),cutStates:next.operations.map(o=>o.status),samePoints:next.transitions[2].points===unchangedPoints,sameInput:input===document.querySelector('[aria-label="Axis K"]'),sameCanvas:canvas===document.querySelector('canvas'),focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:selectors.map(s=>document.querySelector(s).scrollTop),expectedScroll:scroll,events};
  });
  assert.equal(local.reads,0);assert.deepEqual(local.states,['Stale','Stale','Generated']);assert.deepEqual(local.cutStates,['Current','Stale','Current','Current']);
  assert.ok(local.samePoints&&local.sameInput&&local.sameCanvas&&local.focus);assert.deepEqual(local.selection,[2,5]);assert.deepEqual(local.scroll,local.expectedScroll);assert.equal(local.events,1);
  cases.push('Per-connection native receipt keeps unrelated rapids, samples, nodes, listeners, focus, selection and both nested scroll positions');

  const compact=await page.evaluate(async()=>{
    const f=fixture;f.reset();const domain=f.view.scene.domain;
    f.result.statePatch.fixtureValidity=f.projection(domain);f.result.statePatch.intentToolpaths=[{id:'cut',setupId:'setup',name:'renamed'}];
    await f.run();const renamed={revision:domain.expected.mutationSequence,status:f.currentDesign().candidate.cutClearance.status};
    f.request={expected:structuredClone(domain.expected)};f.result.statePatch={...f.result.statePatch,expected:{epoch:'epoch',mutationSequence:'3'},intentToolpaths:[{id:'cut',setupId:'setup',poseRef:{version:'2'}}],fixtureValidity:f.projection(domain,true)};
    await f.run();return {renamed,reads:f.reads,revision:domain.expected.mutationSequence,status:f.currentDesign().candidate.cutClearance.status,unit:domain.fixtureUnits[0].design.assembly.cutClearance.status};
  });
  assert.deepEqual(compact,{renamed:{revision:'2',status:'Clear'},reads:0,revision:'3',status:'Unchecked',unit:'Unchecked'});
  cases.push('Atomic native receipt preserves Current on rename and marks pose edits Stale without another read, using the next valid revision');

  const openingCompact=await page.evaluate(async()=>{
    const f=fixture;f.reset();const domain=f.view.scene.domain,original=f.currentDesign().candidate.openingPreclearance;
    f.result.statePatch.fixtureValidity=f.projection(domain);
    for(const d of [f.result.statePatch.fixtureValidity.setup,...Object.values(f.result.statePatch.fixtureValidity.units)])for(const a of Object.values(d))a.openingPreclearance={validity:'Stale',status:'Unchecked'};
    await f.run();f.render();const c=f.currentDesign().candidate;
    return {reads:f.reads,assemblyValidity:c.validity,opening:c.openingPreclearance,sameOpening:original===c.openingPreclearance,unitOpening:domain.fixtureUnits[0].design.assembly.openingPreclearance,otherOpening:domain.fixtureUnits[1].design.assembly.openingPreclearance.validity,text:document.querySelector('[data-five-key="opening-preclearance-result"]').innerText};
  });
  const staleOpening={validity:'Stale',status:'Unchecked',boundaryCount:3,unclassifiedFaceCount:0,marginMm:1,toolpathRequired:false,productionQualified:false};
  assert.equal(openingCompact.reads,0);assert.equal(openingCompact.assemblyValidity,'Current');assert.equal(openingCompact.sameOpening,true);
  assert.deepEqual(openingCompact.opening,staleOpening);assert.deepEqual(openingCompact.unitOpening,staleOpening);assert.equal(openingCompact.otherOpening,'Current');
  assert.match(openingCompact.text,/孔洞预避让已过期/);assert.doesNotMatch(openingCompact.text,/孔洞预避让已生成/);
  cases.push('Compact opening validity patches retain boundary/margin facts and object identity; Current assembly with Stale preclearance renders expired without a read or changing another setup');

  const unknownOpening=await page.evaluate(async()=>{
    const f=fixture;f.reset();f.fail=true;f.result.statePatch.fixtureValidity=f.projection(f.view.scene.domain);f.result.statePatch.fixtureValidity.setup.candidate.id='unknown-candidate';
    let error;try{await f.run();}catch(e){error=e.message;}f.render();const opening=f.currentDesign().candidate.openingPreclearance;
    return {error,reads:f.reads,opening,text:document.querySelector('[data-five-key="opening-preclearance-result"]').innerText};
  });
  assert.match(unknownOpening.error,/Injected state read failure/);assert.equal(unknownOpening.reads,1);assert.deepEqual(unknownOpening.opening,staleOpening);
  assert.match(unknownOpening.text,/孔洞预避让已过期/);assert.doesNotMatch(unknownOpening.text,/孔洞预避让已生成/);
  cases.push('Unknown fixture projection falls back to an authoritative read; a failed read marks opening preclearance Stale/Unchecked while retaining its geometry facts');

  const mismatch=await page.evaluate(async()=>{
    const f=fixture;f.reset();const actual=f.currentDesign().candidate.id;f.result.statePatch.fixtureValidity=f.projection(f.view.scene.domain);
    f.result.statePatch.fixtureValidity.setup.candidate.id='previous-candidate';await f.run();return {reads:f.reads,id:f.currentDesign().candidate.id,actual,status:f.currentDesign().candidate.cutClearance.status};
  });
  assert.equal(mismatch.reads,1);assert.equal(mismatch.id,mismatch.actual);assert.equal(mismatch.status,'Unchecked');
  cases.push('Mismatched candidate certificate is rejected and re-read rather than applied to another generation');

  await page.evaluate(()=>{
    const f=fixture;f.reset();f.render();f.hold=true;
    f.nodes={canvas:document.querySelector('canvas'),input:document.querySelector('[aria-label="Axis K"]'),nested:document.querySelector('.nested')};f.events=0;f.nodes.input.addEventListener('input',()=>++f.events);
    f.pending=f.run();f.render();
  });
  await page.waitForFunction(()=>!!fixture.release);
  assert.equal(await page.getByRole('button',{name:'Deliver',exact:true}).isEnabled(),false);
  const latest=await page.evaluate(()=>{const f=fixture;f.nodes.input.focus({preventScroll:true});f.nodes.input.setSelectionRange(2,6);return ['.cam-context-pane','.cam-info-pane','.nested'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=70+i*27;return n.scrollTop;});});
  const result=await page.evaluate(async()=>{const f=fixture;f.release();await f.pending;return {reads:f.reads,sceneRefreshes:f.sceneRefreshes,candidate:f.view.scene.domain.setups[0].fixtureDesign.candidate,unit:f.view.scene.domain.fixtureUnits[0].design.assembly,other:f.view.scene.domain.fixtureUnits[1].design.assembly,same:f.nodes.canvas===document.querySelector('canvas')&&f.nodes.input===document.querySelector('[aria-label="Axis K"]')&&f.nodes.nested===document.querySelector('.nested'),focus:document.activeElement===f.nodes.input,selection:[f.nodes.input.selectionStart,f.nodes.input.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.nested'].map(s=>document.querySelector(s).scrollTop)};});
  assert.equal(result.reads,1);assert.equal(result.sceneRefreshes,0);assert.equal(result.candidate.validity,'Stale');assert.equal(result.unit.cutClearance.status,'Unchecked');assert.equal(result.other.cutClearance.status,'Clear');
  assert.equal(result.same,true);assert.equal(result.focus,true);assert.deepEqual(result.selection,[2,6]);assert.deepEqual(result.scroll,latest);
  await page.getByRole('textbox',{name:'Axis K',exact:true}).press('9');assert.equal(await page.evaluate(()=>fixture.events),1);
  cases.push('Candidate and adopted unit use authoritative validity while preserving canvas, controls, listeners, focus, caret and latest dual/nested scrolling');

  const failed=await page.evaluate(async()=>{const f=fixture;f.reset(true,true);f.render();f.fail=true;let error;try{await f.run();}catch(e){error=e.message;}f.render();return {error,reads:f.reads,unit:f.currentDesign().assembly,enabled:document.querySelector('button').disabled===false,revision:f.view.scene.domain.expected.mutationSequence,pose:f.view.scene.domain.intentToolpaths[0].poseRef.version};});
  assert.match(failed.error,/Injected state read failure/);assert.equal(failed.reads,1);assert.equal(failed.unit.validity,'Stale');assert.equal(failed.unit.cutClearance.status,'Unchecked');assert.equal(failed.enabled,false);
  assert.equal(failed.revision,'2');assert.equal(failed.pose,'2');
  cases.push('Frozen unit alone requires native state; failed read cannot retain a Clear certificate or enable delivery');

  for(const kind of ['unknown snapshot','structural cut change']){
    const guarded=await page.evaluate(async kind=>{const f=fixture;f.reset();f.fail=true;
      if(kind==='unknown snapshot')f.request.expected.mutationSequence='unknown';
      else f.result.statePatch.intentToolpaths.push({id:'new-cut',setupId:'setup'});
      try{await f.run();}catch{}f.render();return {reads:f.reads,revision:f.view.scene.domain.expected.mutationSequence,cutCount:f.view.scene.domain.intentToolpaths.length,status:f.currentDesign().candidate.cutClearance.status,disabled:document.querySelector('button').disabled};
    },kind);
    assert.deepEqual(guarded,{reads:1,revision:'1',cutCount:1,status:'Unchecked',disabled:true});
    cases.push(`${kind} rejects partial metadata and remains guarded when the authoritative read fails`);
  }

  const restored=await page.evaluate(async()=>{const f=fixture;f.fail=false;f.selected='unit';f.reply=structuredClone(f.view.scene.domain);f.reply.fixtureUnits[0].design=f.design();f.request=f.result=undefined;await f.run();return {validity:f.currentDesign().assembly.cutClearance.validity,enabled:!document.querySelector('button').disabled};});
  assert.deepEqual(restored,{validity:'Current',enabled:true});cases.push('A subsequent authoritative refresh restores current certificates without local geometry inference');

  const race=await page.evaluate(async()=>{const f=fixture;f.reset();f.render();f.hold=true;const pending=f.run();while(!f.release)await Promise.resolve();const newer=f.base();newer.expected.mutationSequence='9';f.view.scene.domain=newer;f.release();const applied=await pending;return {applied,revision:f.view.scene.domain.expected.mutationSequence};});
  assert.deepEqual(race,{applied:false,revision:'9'});assert.deepEqual(errors,[]);cases.push('Late state response cannot overwrite a newer domain snapshot');
  mkdirSync(output,{recursive:true});writeFileSync(resolve(output,'fixture-projection-report.json'),JSON.stringify({passed:true,cases,coverage:{browser:'Edge',native:false,standaloneCef:false}},null,2));
  console.log(`Fixture projection browser regression passed: ${cases.length} cases.`);
}finally{await browser.close();}
