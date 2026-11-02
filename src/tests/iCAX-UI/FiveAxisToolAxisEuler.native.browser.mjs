import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

const near=(a,b,tolerance=3e-7)=>{assert.equal(a.length,b.length);a.forEach((x,i)=>assert.ok(Math.abs(x-b[i])<tolerance,`${a} != ${b}`));};
const frame=page=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));

export async function verifyToolAxisEuler({page,rpc,calls,output}){
  page.setDefaultTimeout(60000);console.log('Euler pose: actual-model test started');
  const cases=[],invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload}),read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisPointEdit?.loading&&!fixture.view.fiveToolAxisDisplay?.loading);
  const field=(axis,scope='关键点')=>page.getByRole('textbox',{name:`${scope} 欧拉角 ${axis}`,exact:true});
  const values=async(scope='关键点')=>Promise.all(['X','Y','Z'].map(async a=>Number(await field(a,scope).inputValue())));
  // Independent oracle: the renderer's full Euler rotation matrix acts on +Z.
  const direction=angles=>page.evaluate(async angles=>{const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');return new T.Vector3(0,0,1).applyEuler(new T.Euler(...angles.map(n=>n*Math.PI/180),'XYZ')).toArray();},angles);
  const roundTrips=await page.evaluate(async()=>{
    const A=await import('/src/apps/five-axis-cam/webpage/toolAxisAngles.mjs'),T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const vectors=[[0,0,1],[0,0,-1],[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[3,4,-5],[-4,-3,-2],[1,1e-13,-1e-13]];
    return vectors.map(v=>{const euler=A.directionToEuler(v),actual=new T.Vector3(0,0,1).applyEuler(new T.Euler(...euler.map(n=>n*Math.PI/180),'XYZ'));return {v,euler,expected:new T.Vector3(...v).normalize().toArray(),actual:actual.toArray(),back:A.eulerToDirection(euler)};});
  });
  for(const item of roundTrips){near(item.actual,item.expected,1e-10);near(item.back,item.expected,1e-10);assert.equal(item.euler[2],0);}
  cases.push('XYZ Euler conversion matches independent rotation matrices for opposite axes, negative octants, non-unit vectors and singular directions; no spindle roll is invented');

  const initial=await read(),setup=s=>s.setups.find(t=>t.id===s.activeSetupId),id=setup(initial).definitionOrder[2],wholeId=setup(initial).definitionOrder[3];
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await page.getByRole('tab',{name:'切割',exact:true}).click();
  await page.locator(`[data-five-intent="${id}"]`).click();await command('five.points.edit');await idle();await field('X').waitFor();
  assert.equal(await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).inputValue(),'Euler');assert.notEqual(await field('Z').getAttribute('readonly'),null);
  const anchor=await page.evaluate(()=>fixture.view.fiveAxisPointEdit.points.find(p=>p.id===fixture.view.fiveAxisPointEdit.pointId).anchor);
  let state=await read(),axis=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(await direction(await values()),axis.setupDirection);
  for(const [value,property] of [['Workpiece','direction'],['Setup','setupDirection']]){await page.getByRole('combobox',{name:'关键点刀轴坐标系',exact:true}).selectOption(value);near(await direction(await values()),axis[property]);}
  assert.deepEqual(await read(),initial,'changing only representation/frame must not edit the native scene');
  cases.push('local pose defaults to degree-valued XYZ angles, with read-only zero roll; both coordinate frames reconstruct the exact native axis');

  const {locate,drag,pick}=gizmoDriver(page);
  const navigation=async(label)=>{
    const before=await read(),camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
    const draft=await page.evaluate(()=>({pointId:fixture.view.fiveAxisPointEdit.pointId,values:fixture.view.fiveAxisPointEdit.values,dirty:fixture.view.fiveAxisPointEdit.dirty,rotation:fixture.view.fiveAxisPointEdit.rotation?.value,selected:fixture.view.fiveAxisUI.selectedIntentIds}));
    for(const onRing of [false,true])for(const button of ['right','middle']){
      const at=onRing?await locate('X'):await page.evaluate(()=>{const b=fixture.view.viewport.renderer.domElement.getBoundingClientRect();return{x:b.left+b.width*.7,y:b.top+140};});
      const old=await page.evaluate(()=>fixture.view.viewport.getCameraState());
      await page.mouse.move(at.x,at.y);await page.mouse.down({button});
      const pivot=button==='right'?await page.evaluate(()=>fixture.view.viewport.navigation.orbitPivot):null;
      await page.mouse.move(at.x+24,at.y+16,{steps:6});
      if(button==='right')assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.navigation.orbitPivot),pivot,`${label}: orbit retains its captured pivot throughout the drag`);
      await page.mouse.up({button});await frame(page);
      const next=await page.evaluate(()=>fixture.view.viewport.getCameraState());
      if(button==='right'){
        assert.ok(next.theta!==old.theta||next.phi!==old.phi,`${label}: right drag orbits ${onRing?'over a ring':'in the scene'}`);
      }else{
        assert.notDeepEqual(next.target,old.target,`${label}: middle drag pans ${onRing?'over a ring':'in the scene'}`);
        assert.equal(next.theta,old.theta,`${label}: pan preserves camera azimuth`);assert.equal(next.phi,old.phi,`${label}: pan preserves camera elevation`);
      }
      assert.equal(next.radius,old.radius,`${label}: dragging does not zoom`);
    }
    const old=await page.evaluate(()=>fixture.view.viewport.getCameraState());await page.mouse.wheel(0,-120);await frame(page);assert.notEqual((await page.evaluate(()=>fixture.view.viewport.getCameraState())).radius,old.radius);
    assert.deepEqual(await page.evaluate(()=>({pointId:fixture.view.fiveAxisPointEdit.pointId,values:fixture.view.fiveAxisPointEdit.values,dirty:fixture.view.fiveAxisPointEdit.dirty,rotation:fixture.view.fiveAxisPointEdit.rotation?.value,selected:fixture.view.fiveAxisUI.selectedIntentIds})),draft);
    assert.deepEqual(await read(),before,`${label}: navigation preserves the pose and creates no history`);
    await page.evaluate(camera=>fixture.view.viewport.setCameraState(camera),camera);await frame(page);
  };
  await navigation('editing');
  cases.push('right drag orbits, middle drag pans without modifiers, and wheel zoom works during pose editing, including starting over a rotation ring; native pose, selected point and draft remain unchanged');
  if(process.env.ICAX_CAMERA_NAVIGATION_ONLY){
    await page.screenshot({path:resolve(output,'pose-euler-navigation.png')});
    writeFileSync(resolve(output,'pose-euler-navigation-report.json'),JSON.stringify({passed:true,operations:initial.intentToolpaths.length,cases,coverage:{native:true,realMouse:true,browser:'Edge',standaloneCef:false}},null,2));
    return;
  }

  const oldAngles=await values();await field('X').fill(String(oldAngles[0]+6));const expected=await direction(await values());
  await navigation('unsaved Euler draft');near(await direction(await values()),expected);
  await field('Y').fill('');await field('Y').press('Enter');await idle();assert.match(await page.locator('.five-axis-keypoints').innerText(),/有效的姿态角/);assert.deepEqual(await read(),state);
  await field('Y').fill(String(oldAngles[1]));
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-axis-keypoints::after{content:"";display:block;height:1600px}.five-axis-keypoints{max-height:360px;overflow:auto}'});
  await page.evaluate(()=>{fixture.eulerNodes={input:document.querySelector('[aria-label="关键点 欧拉角 X"]'),canvas:fixture.view.viewport.renderer.domElement};fixture.holdMethod='CamCut.EditToolAxisPoint';fixture.hold=true;});
  await field('X').press('Enter');await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const n=fixture.eulerNodes.input;n.focus({preventScroll:true});n.setSelectionRange(0,2);fixture.eulerSelection=[n.selectionStart,n.selectionEnd];return ['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=85+i*20;return p.scrollTop;});});assert.ok(scroll.every(n=>n>0));
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.eulerNodes.input===document.querySelector('[aria-label="关键点 欧拉角 X"]')&&document.activeElement===fixture.eulerNodes.input&&fixture.eulerNodes.canvas===fixture.view.viewport.renderer.domElement),true);
  assert.deepEqual(await field('X').evaluate(n=>[n.selectionStart,n.selectionEnd]),await page.evaluate(()=>fixture.eulerSelection));assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map(s=>document.querySelector(s).scrollTop)),scroll);
  await stress.evaluate(n=>n.remove());state=await read();axis=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(axis.setupDirection,expected);near(await direction(await values()),expected);
  cases.push('Euler input previews without saving, rejects incomplete angles, preserves an unsaved draft during navigation, and saves normalized IJK without replacing input/canvas or losing focus, selection and dual/nested scroll');

  await page.evaluate(()=>{document.querySelector('.cam-info-pane').scrollTop=0;});await frame(page);
  const beforeGestureCamera=await page.evaluate(()=>fixture.view.viewport.getCameraState()),beforeGesture=await read();
  await drag('X',20,false);await page.mouse.wheel(0,-120);await frame(page);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),beforeGestureCamera,'a held left-button rotation owns the gesture and prevents wheel drift');
  near(await direction(await values()),await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)));
  await page.keyboard.press('Escape');await page.mouse.up();await idle();assert.deepEqual(await read(),beforeGesture);await navigation('after cancelling ring drag');
  const edits=calls.filter(c=>c.method==='CamCut.EditToolAxisPoint').length;
  await drag('Y',22,false);const preview=await direction(await values());near(preview,await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)));
  await page.mouse.up();await idle();state=await read();axis=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(axis.setupDirection,preview);assert.equal(calls.filter(c=>c.method==='CamCut.EditToolAxisPoint').length,edits+1);
  await navigation('after saving ring drag');
  cases.push('rotation-ring dragging updates Euler fields live; a held left-button gesture does not move the camera; cancel restores pose and navigation, release saves once and navigation remains available');
  const beforeScreenAxis=[...axis.setupDirection];await pick('E');
  const screenAxis=await page.evaluate(()=>fixture.view.fiveAxisPointEdit.rotation.rotationAxis);
  const screenExpected=await page.evaluate(async({v,u})=>{const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');return new T.Vector3(...v).applyAxisAngle(new T.Vector3(...u),17*Math.PI/180).toArray();},{v:beforeScreenAxis,u:screenAxis});
  await page.getByRole('textbox',{name:'刀轴旋转角度',exact:true}).fill('17');await page.getByRole('textbox',{name:'刀轴旋转角度',exact:true}).press('Enter');await idle();
  state=await read();near((await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor})).setupDirection,screenExpected);
  await rpc({action:'undo'});await command('five.refresh');await idle();state=await read();axis=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(axis.setupDirection,beforeScreenAxis);
  cases.push('the outer screen ring also edits keypoint tool direction around the view axis and undo restores the exact original direction');
  await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('IJK');
  near(await Promise.all(['I','J','K'].map(async a=>Number(await page.getByRole('textbox',{name:`关键点 ${a}`,exact:true}).inputValue()))),axis.setupDirection);
  await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('Euler');near(await direction(await values()),axis.setupDirection);
  await page.screenshot({path:resolve(output,'pose-euler-navigation.png')});
  cases.push('Euler and IJK presentations are interchangeable without altering the saved pose');

  await command('five.points.whole');await idle();await page.locator(`[data-five-intent="${wholeId}"]`).click();await field('X','刀轴').waitFor();
  await page.getByRole('combobox',{name:'刀轴坐标系',exact:true}).selectOption('Workpiece');
  const wholeAngles=await values('刀轴');await field('X','刀轴').fill(String(wholeAngles[0]+5));const wholeDirection=await direction(await values('刀轴'));await field('X','刀轴').press('Enter');await idle();
  state=await read();const whole=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:wholeId});near(whole.direction,wholeDirection);assert.equal(whole.mode,'Fixed');
  let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);
  await command('five.refresh');await idle();
  const operation=await page.evaluate(intentId=>{const domain=fixture.view.scene.domain;return domain.setups.find(s=>s.id===domain.activeSetupId).generatedGeometry.operations.find(o=>o.intentId===intentId);},wholeId);
  assert.ok(operation.parts.length);for(const part of operation.parts)for(const v of part.directions)near(v,wholeDirection);
  cases.push('whole-path Euler editing saves a Fixed tool axis and native regenerated cutting data uses the corresponding IJK at every point');
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});
  near((await invoke('CamCut.ReadToolAxis',{expected:reopened.expected,intentId:wholeId})).direction,wholeDirection);near((await invoke('CamCut.ReadToolAxis',{expected:reopened.expected,intentId:id,anchor})).setupDirection,axis.setupDirection);
  cases.push('save/reopen preserves local and whole-path direction edits made through Euler angles');
  writeFileSync(resolve(output,'pose-euler-navigation-report.json'),JSON.stringify({passed:true,operations:initial.intentToolpaths.length,roundTrips,cases,coverage:{native:true,realMouse:true,browser:'Edge',standaloneCef:false}},null,2));
}
