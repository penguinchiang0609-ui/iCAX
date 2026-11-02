import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifySimulationTrace({page,command,field,button,idle,read,output}) {
  const before=await read(),seek=async n=>{await field('seek').fill(String(n));await field('seek').dispatchEvent('input');};
  const trace=()=>page.evaluate(()=>{const s=fixture.view.fiveSimulation,t=s.trace,r=t.line.geometry.drawRange,rapid=t.rapidLine.geometry.drawRange;return {cursor:s.cursor,start:s.traceStart,count:r.count,offset:r.start,color:t.line.material.color.getHex(),playing:s.playing,
    rapidCount:rapid.count,rapidOffset:rapid.start,rapidColor:t.rapidLine.material.color.getHex(),rapidDashed:t.rapidLine.material.isLineDashedMaterial===true,rapidLineDistances:!!t.rapidLine.geometry.getAttribute('lineDistance')};});
  const endpoints=rapid=>page.evaluate(rapid=>{const s=fixture.view.fiveSimulation,t=s.trace,line=rapid?t.rapidLine:t.line,p=line.geometry.getAttribute('position'),range=line.geometry.drawRange,
    segments=rapid?t.rapidSegments:t.segments,first=segments[range.start/2],last=segments[(range.start+range.count)/2-1],source=frame=>rapid?frame.position:frame.target;
    return {start:[p.getX(range.start),p.getY(range.start),p.getZ(range.start)],end:[p.getX(range.start+range.count-1),p.getY(range.start+range.count-1),p.getZ(range.start+range.count-1)],a:source(s.frames[first]).toArray(),b:source(s.frames[last+1]).toArray()};},rapid);
  const interpolated=(actual,a,b,fraction)=>actual.forEach((value,index)=>assert.ok(Math.abs(value-(a[index]*(1-fraction)+b[index]*fraction))<.001,`partial trace endpoint ${index} does not match the actual motion`));
  await page.evaluate(()=>{const v=fixture.view;fixture.traceNodes={canvas:v.viewport.renderer.domElement,model:[...v.viewport.sceneObjects.values()][0],paths:v.fiveGeneratedPreview.group,line:v.fiveSimulation.trace.line,rapidLine:v.fiveSimulation.trace.rapidLine,slider:document.querySelector('[data-five-simulation-field="seek"]')};});
  assert.equal((await trace()).count,0);
  await button('play').click();await page.waitForFunction(()=>fixture.view.fiveSimulation.cursor>.2||!fixture.view.fiveSimulation.playing&&fixture.view.fiveSimulation.frameContact?.status==='Collision');
  if((await trace()).playing)await button('pause').click();
  if((await trace()).cursor<=.2)await seek(.6); // A real initial collision correctly stops playback before a trail exists.
  assert.ok((await trace()).count>0);assert.equal((await trace()).color,0xff3030);
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.frameContact?.mode),'DiscreteBoxes');
  const indices=await page.evaluate(()=>{const f=fixture.view.fiveSimulation.frames;return {rapid:f.findIndex((a,i)=>a.role==='Rapid'&&f[i+1]?.role==='Rapid'&&!f[i+1].breakBefore&&a.position.distanceToSquared(f[i+1].position)>1e-20),cut:f.findIndex((a,i)=>a.laser&&a.target&&f[i+1]?.target&&!f[i+1].breakBefore&&a.intentId===f[i+1].intentId&&a.target.distanceToSquared(f[i+1].target)>1e-20)};});
  assert.ok(indices.cut>=0&&indices.rapid>0);
  await seek(indices.rapid+.1);const rapidStart=await trace();assert.ok(rapidStart.rapidCount>0);assert.equal(rapidStart.rapidColor,0xff3030);assert.equal(rapidStart.rapidDashed,true);assert.equal(rapidStart.rapidLineDistances,true);
  const rapidPartial=await page.evaluate(()=>{const s=fixture.view.fiveSimulation,t=s.trace,r=t.rapidLine.geometry.drawRange,p=t.rapidLine.geometry.getAttribute('position'),last=r.start+r.count-1,i=t.rapidSegments[last/2|0];return {actual:[p.getX(last),p.getY(last),p.getZ(last)],a:s.frames[i].position.toArray(),b:s.frames[i+1].position.toArray()};});
  interpolated(rapidPartial.actual,rapidPartial.a,rapidPartial.b,.1);
  await seek(indices.rapid+.9);const rapidEnd=await trace();assert.equal(rapidEnd.count,rapidStart.count,'rapid progress cannot add cutting contact segments');assert.equal(rapidEnd.rapidCount,rapidStart.rapidCount);
  const rapidPartialEnd=await endpoints(true);interpolated(rapidPartialEnd.end,rapidPartial.a,rapidPartial.b,.9);
  await page.screenshot({path:resolve(output,'simulation-red-cut-and-dashed-rapid.png')});
  await seek(indices.rapid+.25);await page.getByLabel('加工轨迹',{exact:true}).click();await page.getByRole('menuitem',{name:'清除加工轨迹'}).click();
  assert.equal((await trace()).count,0);assert.equal((await trace()).rapidCount,0);await seek(indices.rapid+.75);assert.equal((await trace()).count,0);assert.equal((await trace()).rapidCount,2);
  const clearedRapid=await endpoints(true);interpolated(clearedRapid.start,clearedRapid.a,clearedRapid.b,.25);interpolated(clearedRapid.end,clearedRapid.a,clearedRapid.b,.75);
  await seek(indices.cut+.4);
  await page.getByLabel('加工轨迹',{exact:true}).click();
  assert.equal(await page.getByRole('menuitem',{name:'清除加工轨迹'}).isVisible(),true);
  await page.getByRole('menuitem',{name:'清除加工轨迹'}).click();
  let actual=await trace();assert.equal(actual.count,0);assert.equal(actual.rapidCount,0);assert.equal(actual.cursor,indices.cut+.4);assert.equal(actual.start,actual.cursor);
  await seek(indices.cut+.8);actual=await trace();assert.equal(actual.count,2);
  const ends=await page.evaluate(()=>{const s=fixture.view.fiveSimulation,t=s.trace,p=t.line.geometry.getAttribute('position'),offset=t.line.geometry.drawRange.start,i=t.segments[offset/2];
    return {start:[p.getX(offset),p.getY(offset),p.getZ(offset)],end:[p.getX(offset+1),p.getY(offset+1),p.getZ(offset+1)],a:s.frames[i].target.toArray(),b:s.frames[i+1].target.toArray()};});
  for(let i=0;i<3;++i){assert.ok(Math.abs(ends.start[i]-(ends.a[i]*.6+ends.b[i]*.4))<.001);assert.ok(Math.abs(ends.end[i]-(ends.a[i]*.2+ends.b[i]*.8))<.001);}
  await seek(indices.cut+.3);actual=await trace();assert.equal(actual.count,0);assert.equal(actual.rapidCount,0);assert.equal(actual.start,indices.cut+.3,'rewinding before the cleared point resets the trace origin');
  await seek(indices.cut+.6);actual=await trace();assert.equal(actual.count,2);assert.equal(actual.rapidCount,0);const rewound=await endpoints(false);interpolated(rewound.start,rewound.a,rewound.b,.3);interpolated(rewound.end,rewound.a,rewound.b,.6);
  await page.getByLabel('加工轨迹',{exact:true}).click();
  await page.evaluate(()=>{fixture.holdMethod='CamScene.ReadState';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');});
  await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const menu=document.querySelector('.five-trace-menu');fixture.traceMenu=menu;menu.querySelector('summary').focus({preventScroll:true});const pane=document.querySelector('.cam-context-pane');pane.scrollTop=130;return pane.scrollTop;});
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.traceMenu===document.querySelector('.five-trace-menu')&&fixture.traceMenu.open&&document.activeElement===fixture.traceMenu.querySelector('summary')),true);
  assert.equal(await page.locator('.cam-context-pane').evaluate(n=>n.scrollTop),scroll);
  await page.getByRole('menuitem',{name:'清除加工轨迹'}).click();
  await button('play').click();await page.waitForFunction(()=>fixture.view.fiveSimulation.cursor>1.1);
  await page.getByLabel('加工轨迹',{exact:true}).click();await page.getByRole('menuitem',{name:'清除加工轨迹'}).click();
  assert.equal((await trace()).playing,true,'clearing the trace does not interrupt playback');
  await page.waitForFunction(()=>fixture.view.fiveSimulation.trace.line.geometry.drawRange.count>0);await button('pause').click();assert.ok((await trace()).count>0);
  assert.equal(await page.evaluate(()=>{const v=fixture.view,n=fixture.traceNodes;return n.canvas===v.viewport.renderer.domElement&&n.model===[...v.viewport.sceneObjects.values()][0]&&n.paths===v.fiveGeneratedPreview.group&&n.line===v.fiveSimulation.trace.line&&n.rapidLine===v.fiveSimulation.trace.rapidLine&&n.slider===document.querySelector('[data-five-simulation-field="seek"]');}),true);
  const mid=await page.evaluate(()=>Math.floor(fixture.view.fiveSimulation.frames.length*.45));await seek(mid);
  const collisionPriority=await page.evaluate(()=>{const v=fixture.view,s=v.fiveSimulation,lines=[];s.collisionOverlay?.traverse(line=>{if(line.isLineSegments&&/^collision-Collision(?:-rapid)?$/.test(line.name))lines.push({name:line.name,visible:line.visible,color:line.material.color.getHex(),order:line.renderOrder,dashed:line.material.isLineDashedMaterial===true});});return {reported:Number(s.collisionReport?.collisionCount)||0,lines,cutOrder:s.trace.line.renderOrder,rapidOrder:s.trace.rapidLine.renderOrder};});
  const nativeCollision=before.setups.find(setup=>setup.id===before.activeSetupId)?.simulationCheck;
  if(nativeCollision?.validity==='Current'&&Number(nativeCollision.collisionCount)>0)assert.equal(collisionPriority.reported,Number(nativeCollision.collisionCount),'trace colors must retain the authoritative native collision report');
  if(collisionPriority.reported){assert.ok(collisionPriority.lines.length,'native collision geometry must remain visible');for(const line of collisionPriority.lines){assert.equal(line.visible,true);assert.equal(line.color,0xff3030);assert.equal(line.dashed,line.name.endsWith('-rapid'));assert.ok(line.order>collisionPriority.cutOrder&&line.order>collisionPriority.rapidOrder,'native collision red must draw above both traversed motion trails');}}
  if(!collisionPriority.reported)collisionPriority.discreteSearch=await page.evaluate(()=>{
    const s=fixture.view.fiveSimulation,input=document.querySelector('[data-five-simulation-field="seek"]'),index=s.trace.segments[0];
    if(index===undefined)return {checked:0,status:'NoCutSegment'};
    input.value=String(index+.6);input.dispatchEvent(new Event('input',{bubbles:true}));
    return {checked:1,index,status:s.frameContact?.mode==='DiscreteBoxes'?s.frameContact.status:'NoDiscreteContactResult'};
  });
  const traversedHit=await page.evaluate(()=>{const s=fixture.view.fiveSimulation,t=s.trace,reported=new Set([...(s.collisionReport?.segments||[]).filter(segment=>segment.status==='Collision').map(segment=>Number(segment.index)),...t.contacts]);for(const key of ['collisionCut','collisionRapid']){const index=t[key].segments.find(index=>reported.has(index)&&index>=s.traceStart);if(index!==undefined)return {key,index};}return null;});
  if(traversedHit){
    await seek(traversedHit.index+.6);
    const red=await page.evaluate(({key,index})=>{const s=fixture.view.fiveSimulation,t=s.trace,track=t[key],line=track.line,r=line.geometry.drawRange,p=line.geometry.getAttribute('position'),last=r.start+r.count-1,source=track.isRapid?'position':'target';return {count:r.count,color:line.material.color.getHex(),dashed:line.material.isLineDashedMaterial===true,rapid:track.isRapid,order:line.renderOrder,baseOrder:(track.isRapid?t.rapidLine:t.line).renderOrder,drawn:track.segments.slice(r.start/2,(r.start+r.count)/2),cursor:s.cursor,end:[p.getX(last),p.getY(last),p.getZ(last)],a:s.frames[index][source].toArray(),b:s.frames[index+1][source].toArray()};},traversedHit);
    assert.ok(red.count>0);assert.equal(red.color,0xff3030);assert.equal(red.dashed,red.rapid);assert.ok(red.order>red.baseOrder);assert.ok(red.drawn.every(index=>index<red.cursor),'red traversed trace cannot reveal future segments');interpolated(red.end,red.a,red.b,.6);collisionPriority.traversedHit={...traversedHit,...red};
  }
  await page.screenshot({path:resolve(output,'simulation-red-cut-trace.png')});
  await page.getByLabel('加工轨迹',{exact:true}).click();await page.screenshot({path:resolve(output,'simulation-clear-trace-menu.png')});
  await page.getByRole('menuitem',{name:'清除加工轨迹'}).click();assert.equal((await trace()).count,0);assert.equal((await trace()).rapidCount,0);
  await page.screenshot({path:resolve(output,'simulation-trace-cleared.png')});
  assert.deepEqual(await read(),before,'trail display and clearing never change cutting definitions, generated paths or the collision report');
  await command('five.tool.select');assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.trace),null);assert.equal(await page.evaluate(()=>!fixture.traceNodes.line.parent&&!fixture.traceNodes.rapidLine.parent),true,'both cutting and rapid trace graphics detach on exit');
  await command('five.tool.simulation');assert.equal((await trace()).count,0,'cleared trail stays cleared across tool changes');assert.equal((await trace()).rapidCount,0);
  writeFileSync(resolve(output,'trace-report.json'),JSON.stringify({passed:true,cases:['red workpiece contact trail during playback','red dashed rapid TCP trail with fractional endpoints','clear fractional cut and rapid then resume','seek and rewind','native collision red renders above both motion trails when a collision report is present','menu focus, scroll and DOM retained across delayed refresh','clearing preserves machining data','both graphics released on exit'],collisionPriority,frames:await page.evaluate(()=>fixture.view.fiveSimulation.frames.length)},null,2));
}
