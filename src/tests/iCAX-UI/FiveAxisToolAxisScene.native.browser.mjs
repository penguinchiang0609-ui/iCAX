import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

const near=(a,b,tolerance=1e-7)=>{assert.equal(a.length,b.length);a.forEach((n,i)=>assert.ok(Math.abs(n-b[i])<tolerance,`${a} != ${b}`));};
const rotate=(v,axis,degrees)=>{
  const a=degrees*Math.PI/180,c=Math.cos(a),s=Math.sin(a),dot=v.reduce((n,x,i)=>n+x*axis[i],0);
  const cross=[axis[1]*v[2]-axis[2]*v[1],axis[2]*v[0]-axis[0]*v[2],axis[0]*v[1]-axis[1]*v[0]];
  return v.map((x,i)=>x*c+cross[i]*s+axis[i]*dot*(1-c));
};

// Pick the actual rendered source curve with a real mouse click. Camera setup is
// test preparation only; production hit testing and native anchor resolution run.
async function focusCut(page,id){
  await page.evaluate(id=>{
    const c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport,o=v.sceneObjects.get(c.placementId);
    const lo=[0,1,2].map(i=>Math.min(...c.previewPoints.map(p=>p[i]))),hi=[0,1,2].map(i=>Math.max(...c.previewPoints.map(p=>p[i])));
    const target=o.localToWorld(v.camera.position.clone().set(...lo.map((x,i)=>(x+hi[i])/2)));
    v.setCameraState({...v.getCameraState(),target:{x:target.x,y:target.y,z:target.z},radius:Math.max(10,Math.hypot(...lo.map((x,i)=>hi[i]-x))*1.7)});
  },id);
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
}
async function cutPoint(page,id){
  return page.evaluate(id=>{
    const c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport,o=v.sceneObjects.get(c.placementId),b=v.renderer.domElement.getBoundingClientRect();
    for(let i=Math.floor(c.previewPoints.length*.3);i<c.previewPoints.length-2;i++){
      const p=c.previewPoints[i].map((n,j)=>(n+c.previewPoints[i+1][j])/2),q=o.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);
      const x=b.x+(q.x+1)*b.width/2,y=b.y+(1-q.y)*b.height/2;
      if(x>b.x+45&&x<b.right-45&&y>b.y+130&&y<b.bottom-100&&document.elementFromPoint(x,y)===v.renderer.domElement)return{x,y};
    }
    throw Error('No visible source curve point for real pointer insertion');
  },id);
}

async function verifySceneLayout({page,command,idle,read,id,output}){
  const before=await read(),layouts=[],overlaps=(a,b)=>a.left<b.right-.5&&a.right>b.left+.5&&a.top<b.bottom-.5&&a.bottom>b.top+.5;
  await page.locator(`[data-five-intent="${id}"]`).click();await command('five.points.edit');await idle();await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('IJK');await page.getByRole('textbox',{name:'关键点 I',exact:true}).waitFor();
  if(await page.locator('[data-five-command="five.display.numbers"]').getAttribute('aria-pressed')==='true')await command('five.display.numbers');
  for(const [width,height] of [[1024,700],[1440,900],[1920,1080]]){
    await page.setViewportSize({width,height});await page.evaluate(()=>fixture.view.viewport.fitView());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
    const layout=await page.evaluate(()=>{const rect=n=>{const r=n.getBoundingClientRect();return{left:r.left,right:r.right,top:r.top,bottom:r.bottom,width:r.width,height:r.height};};return{canvas:rect(fixture.view.viewport.renderer.domElement),legend:rect(document.querySelector('.five-legend')),items:[...document.querySelectorAll('.five-legend span')].map(n=>({text:n.textContent,rect:rect(n)})),projection:rect(document.querySelector('.icax-three-projection-toggle')),hint:rect(document.querySelector('.five-point-scene-hint')),cornerAxis:rect(document.querySelector('.icax-three-axis-gizmo')),controls:rect(document.querySelector('.five-pose-controls')),buttons:[...document.querySelectorAll('.five-pose-controls button')].map(rect)};});
    assert.ok(layout.items.length>=6);assert.ok(layout.legend.width>0&&layout.legend.height>0);
    for(const item of layout.items){const b=item.rect,c=layout.canvas;assert.ok(b.left>=c.left-.5&&b.right<=c.right+.5&&b.top>=c.top-.5&&b.bottom<=c.bottom+.5,`${width}x${height}: legend ${item.text} stays inside the canvas`);assert.equal(overlaps(b,layout.projection),false,`${width}x${height}: legend ${item.text} overlaps projection controls`);assert.equal(overlaps(b,layout.hint),false,`${width}x${height}: legend ${item.text} overlaps pose hint`);}
    assert.equal(overlaps(layout.legend,layout.projection),false,`${width}x${height}: legend block overlaps projection controls`);assert.equal(overlaps(layout.legend,layout.hint),false,`${width}x${height}: legend block overlaps pose hint`);
    assert.ok(layout.legend.left>=layout.projection.right+7,`${width}x${height}: legend reserves the right side beyond the centered projection control`);assert.equal(overlaps(layout.legend,layout.cornerAxis),false,`${width}x${height}: legend block overlaps corner XYZ axes`);
    const {controls,buttons,canvas,cornerAxis}=layout;assert.equal(buttons.length,2);near([buttons[0].left,buttons[0].width,buttons[0].height],[buttons[1].left,buttons[1].width,buttons[1].height]);
    assert.ok(Math.abs(controls.left-canvas.left-16)<1&&controls.top>canvas.top+canvas.height/2,'pose switches align at the bottom left');
    assert.ok(controls.bottom<=cornerAxis.top-7&&buttons[1].top>=buttons[0].bottom+5,'stacked switches have space above the corner axis');
    for(const other of [layout.legend,layout.projection,layout.hint,cornerAxis])assert.equal(overlaps(controls,other),false,'pose switches never overlap another scene control');
    assert.ok(layout.hint.bottom<=controls.top-7&&layout.hint.left===controls.left,'pose instructions align above the switches');
    layouts.push({width,height,...layout});await page.screenshot({path:resolve(output,`scene-layout-${width}x${height}.png`)});
  }
  await page.setViewportSize({width:1440,height:900});await focusCut(page,id);await gizmoDriver(page).pick('X');await page.screenshot({path:resolve(output,'tool-axis-scene-keypoint-gizmo.png')});
  await command('five.points.whole');await idle();await page.evaluate(()=>fixture.view.viewport.fitView());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await page.screenshot({path:resolve(output,'tool-axis-scene-overview.png')});
  assert.deepEqual(await read(),before,'layout inspection and ring selection never mutate native machining state');
  writeFileSync(resolve(output,'scene-layout-report.json'),JSON.stringify({passed:true,layouts,coverage:{native:true,browser:'Edge',standaloneCef:false,scope:'bottom-left pose switches, legend and projection/hint spacing at three viewport sizes'}},null,2));
}

// Measure the pixels actually drawn by the live production glyph geometry and
// shader, isolated from the model and gizmo so their pixels cannot inflate it.
async function measurePoseGlyphs(page){
  return page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),view=fixture.view,v=view.viewport,r=v.renderer;
    v.scene.updateMatrixWorld(true);const preview=view.fiveAxisPointPreview.group.children.find(m=>m.userData.pointPreview);
    const owner=view.fiveToolAxisDisplay.group.children.find(c=>c.userData.intentId===view.fiveAxisPointEdit.id),saved=owner.children.find(m=>m.userData.kind==='axis');
    const size=r.getSize(new THREE.Vector2()),target=new THREE.WebGLRenderTarget(size.x,size.y),pixels=new Uint8Array(size.x*size.y*4),result=[];
    const oldTarget=r.getRenderTarget(),oldColor=r.getClearColor(new THREE.Color()),oldAlpha=r.getClearAlpha(),autoClear=r.autoClear;
    try{
      r.setRenderTarget(target);r.setClearColor(0,0);r.autoClear=true;
      for(const [name,source,index] of [['preview',preview,0],['existing',saved,0]]){
        const geometry=source.geometry.clone(),mask=new Float32Array(geometry.instanceCount);mask[index]=1;geometry.setAttribute('instanceShown',new THREE.InstancedBufferAttribute(mask,1));
        const material=source.material.clone();material.uniforms.previewIndex.value=-1;material.uniforms.hoverIndex.value=-1;material.uniforms.selectedIndex.value=-1;material.uniforms.resolution.value.copy(size);
        const mesh=new THREE.Mesh(geometry,material);mesh.frustumCulled=false;mesh.matrixAutoUpdate=false;mesh.matrix.copy(source.matrixWorld);
        const scene=new THREE.Scene();scene.add(mesh);
        const point=new THREE.Vector3().fromBufferAttribute(geometry.attributes.instancePoint,index).applyMatrix4(source.matrixWorld),state=v.getCameraState();
        const away=v.camera.position.clone().sub(new THREE.Vector3(state.target.x,state.target.y,state.target.z));
        for(const factor of [.5,1,2]){
          const camera=v.camera.clone();camera.position.copy(point).addScaledVector(away,factor);camera.lookAt(point);camera.updateMatrixWorld(true);
          r.render(scene,camera);r.readRenderTargetPixels(target,0,0,size.x,size.y,pixels);
          const drawn=[];for(let i=0;i<pixels.length;i+=4)if(pixels[i+3]>40)drawn.push([i/4%size.x,Math.floor(i/4/size.x)]);
          let diameter=0;for(const a of drawn)for(const b of drawn)diameter=Math.max(diameter,Math.hypot(a[0]-b[0],a[1]-b[1]));
          result.push({name,factor,pixels:drawn.length,diameter});
        }
        geometry.dispose();material.dispose();
      }
    }finally{r.setRenderTarget(oldTarget);r.setClearColor(oldColor,oldAlpha);r.autoClear=autoClear;target.dispose();}
    return result;
  });
}

export async function verifyToolAxisScene({page,rpc,calls,output}){
  page.setDefaultTimeout(60000);console.log('Scene pose: entered actual-model test');
  const shaderErrors=[];page.on('console',m=>{if(m.type()==='error'&&/WebGLProgram|Shader Error|VALIDATE_STATUS|GL_INVALID_OPERATION/i.test(m.text()))shaderErrors.push(m.text());});
  const cases=[],invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload}),read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisPointLookup&&!fixture.view.fiveAxisPointEdit?.loading&&!fixture.view.fiveToolAxisDisplay?.loading);
  const setup=s=>s.setups.find(p=>p.id===s.activeSetupId),field=a=>page.getByRole('textbox',{name:`关键点 ${a}`,exact:true});
  const first=await read(),id=setup(first).definitionOrder[2],original=first.intentToolpaths.find(c=>c.id===id);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await page.getByRole('tab',{name:'切割',exact:true}).click();await idle();
  assert.equal(await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='toolpath').groups.some(g=>g.title==='姿态调整')),false);
  assert.deepEqual(await page.locator('.cam-info-pane .five-pose-editor-actions button').evaluateAll(nodes=>nodes.map(n=>[n.dataset.fiveCommand,n.getAttribute('aria-label')])),[['five.axes.reverse-all','全部姿态反向'],['five.pose-select.toggle','姿态选择'],['five.points.whole','整条刀路'],['five.points.add','新增关键点'],['five.points.delete','删除关键点'],['five.points.edit','编辑关键点'],['five.points.reverse','当前姿态反向']]);
  await page.waitForFunction(()=>fixture.view.fiveToolAxisDisplay?.data?.cuts?.length>0);
  await page.evaluate(()=>fixture.view.viewport.fitView());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const display=()=>page.evaluate(()=>{
    const d=fixture.view.fiveToolAxisDisplay,v=fixture.view.viewport,box=v.renderer.domElement.getBoundingClientRect();
    return{visible:d.visible,data:d.data,error:d.error,groups:d.group?.children.map(g=>({id:g.userData.intentId,renderedCount:g.userData.renderedCount,density:Array.from(g.userData.shownMask),sharedDensity:g.children[0].geometry.attributes.instanceDensity===g.children[1].geometry.attributes.instanceDensity,projected:g.children[0].userData.toolAxisSamples.map(s=>{const p=v.camera.position.clone().set(...s.point).applyMatrix4(g.matrixWorld).project(v.camera);return[(p.x+1)*box.width/2,(1-p.y)*box.height/2];}),meshes:g.children.map(m=>({kind:m.userData.kind,color:m.material.uniforms.color.value.getHex(),count:m.geometry.instanceCount,renderedCount:m.userData.renderedCount,samples:m.userData.toolAxisSamples,points:Array.from(m.geometry.attributes.instancePoint.array),directions:Array.from(m.geometry.attributes.instanceDirection.array),shown:Array.from(m.geometry.attributes.instanceShown.array)}))}))};
  });
  const batch=await display(),ids=first.intentToolpaths.filter(c=>c.enabled!==false&&c.machiningKind==='Cut').map(c=>c.id).sort();
  const palette=await page.evaluate(async()=>(await import('/src/apps/five-axis-cam/webpage/scenePalette.mjs')).sceneColors);assert.equal(new Set(['cutting','toolAxis','selected','reference'].map(k=>palette[k])).size,4,'cutting, actual axis, selection and reference have distinct roles');
  if(process.env.ICAX_TOOL_AXIS_SCENE_LAYOUT_ONLY){await verifySceneLayout({page,command,idle,read,id,output});return;}
  const scenePaletteState=()=>page.evaluate(()=>{
    const color=n=>{const [r,g,b]=getComputedStyle(n,n.classList.contains('axis')?'::before':null).borderTopColor.match(/[\d.]+/g).map(Number);return(r<<16)|(g<<8)|b;},view=fixture.view;
    return{legend:Object.fromEntries([...document.querySelectorAll('.five-legend i')].map(n=>[n.className,color(n)])),labels:[...document.querySelectorAll('.icax-three-specification-label')].map(n=>getComputedStyle(n).backgroundColor),lines:[...(view.fiveCutPreview?.group.children||[]),...(view.fiveGeneratedPreview?.group.children||[])].filter(o=>o.userData.cuttingLineId||o.userData.generatedIntentId).map(o=>({id:o.userData.cuttingLineId||o.userData.generatedIntentId,color:o.material.color.getHex()}))};
  });
  const initialPalette=await scenePaletteState();for(const [kind,role] of Object.entries({cut:'cutting',axis:'toolAxis',selected:'selected',reference:'reference',rapid:'reference',stale:'stale'}))assert.equal(initialPalette.legend[kind],palette[role],`legend ${kind} uses the scene ${role} color`);
  assert.ok(initialPalette.lines.length>=ids.length);assert.ok(initialPalette.lines.every(line=>[palette.cutting,palette.stale].includes(line.color)),'unselected cuts use cutting/stale colors');
  for(const color of initialPalette.labels){const [r,g,b]=color.match(/[\d.]+/g).map(Number);assert.ok(!(r>150&&r>g*1.3&&r>b*1.3),'ordinary machining sequence labels must not use error red');}
  assert.equal(batch.visible,true);assert.equal(batch.error,'');assert.ok(!batch.data.errors?.length);assert.deepEqual(batch.data.cuts.map(c=>c.intentId).sort(),ids);
  assert.deepEqual(batch.groups.map(g=>g.id).sort(),ids);
  console.log('Scene pose: all-path instances loaded',ids.length);
  for(const cut of batch.data.cuts){
    assert.ok(cut.samples.length>=3,`multiple direction stations required for ${cut.intentId}`);
    const group=batch.groups.find(g=>g.id===cut.intentId);assert.deepEqual(group.meshes.map(m=>m.kind).sort(),['axis','normal']);
    const visible=cut.samples.filter(s=>s.keypointIds.length);assert.ok(visible.length>0&&visible.length<cut.samples.length,'only system/manual keypoints are eligible for UI glyphs');
    for(const mesh of group.meshes){assert.equal(mesh.color,palette[mesh.kind==='normal'?'reference':'toolAxis']);assert.equal(mesh.count,visible.length);assert.equal(mesh.samples.length,visible.length);assert.equal(mesh.points.length,visible.length*3);assert.equal(mesh.directions.length,visible.length*3);for(let i=0;i<visible.length;i++){assert.ok(mesh.samples[i].keypointIds.length);near(mesh.points.slice(i*3,i*3+3),visible[i].point,3e-4);near(mesh.directions.slice(i*3,i*3+3),visible[i][mesh.kind==='normal'?'surfaceNormal':'direction'],1e-6);}}
    for(const sample of cut.samples){near([Math.hypot(...sample.direction)],[1]);near([Math.hypot(...sample.surfaceNormal)],[1]);}
    const sample=cut.samples[Math.floor(cut.samples.length/2)],truth=await invoke('CamCut.ReadToolAxis',{expected:first.expected,intentId:cut.intentId,anchor:sample.anchor});
    near(sample.point,truth.point);near(sample.direction,truth.direction);near(sample.surfaceNormal,truth.surfaceNormal);
  }
  cases.push('all 19 cutting paths render only system/manual keypoint normals and axes; every ordinary reference sample is excluded from GPU glyphs and picking; native IJK is cross-checked');
  const batchCalls=calls.filter(c=>c.method==='CamCut.ReadToolAxisSamples').length;
  assert.equal(batchCalls,1,'all initial cutting paths are read in one native batch');
  const densityCount=data=>data.groups.reduce((total,g)=>{
    const normal=g.meshes.find(m=>m.kind==='normal'),axis=g.meshes.find(m=>m.kind==='axis');assert.equal(g.sharedDensity,true);assert.deepEqual(axis.shown,g.density);
    assert.equal(normal.shown.length,normal.count);assert.ok(axis.shown.every(n=>n===0||n===1));
    for(let i=0;i<axis.count;i++){const sample=axis.samples[i],dot=sample.direction.reduce((sum,n,j)=>sum+n*sample.surfaceNormal[j],0);assert.equal(normal.shown[i],axis.shown[i]*(dot<=1-1e-6?1:0),'coincident reference normals are suppressed; other references share the projected density stations');}
    const visible=g.projected.filter((_,i)=>axis.shown[i]);for(let i=0;i<visible.length;i++)for(let j=i+1;j<visible.length;j++)assert.ok(Math.hypot(visible[i][0]-visible[j][0],visible[i][1]-visible[j][1])>=33.5,'shown stations stay at least 34 CSS pixels apart within each path');
    const shown=axis.shown.reduce((sum,n)=>sum+n,0);assert.ok(shown>=1,`density mask must retain path ${g.id}`);assert.equal(g.renderedCount,shown);assert.equal(axis.renderedCount,shown);assert.equal(normal.renderedCount,normal.shown.reduce((sum,n)=>sum+n,0));return total+shown;
  },0);
  const density={full:batch.data.cuts.reduce((sum,c)=>sum+c.samples.length,0),fit:densityCount(batch),zoom:[]},fitCamera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  assert.ok(density.fit<density.full,'fit view reduces overlapping direction glyphs while retaining every cutting path');
  for(const factor of [.55,.32,1.4]){
    await page.evaluate(({camera,factor})=>fixture.view.viewport.setCameraState({...camera,radius:camera.radius*factor}),{camera:fitCamera,factor});
    await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));density.zoom.push(densityCount(await display()));
  }
  assert.ok(density.zoom.every(count=>count<=batch.data.cuts.reduce((sum,c)=>sum+c.samples.filter(s=>s.keypointIds.length).length,0)),'navigation never reveals ordinary reference samples');assert.equal(calls.filter(c=>c.method==='CamCut.ReadToolAxisSamples').length,batchCalls,'density changes never reread native CAD');
  await page.evaluate(camera=>fixture.view.viewport.setCameraState(camera),fitCamera);await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  cases.push('fit/zoom/orbit show only keypoint axes with 34px spacing and suppress coincident references; camera navigation never reveals intermediate samples or rereads CAD');
  await page.locator(`[data-five-intent="${id}"]`).click();await idle();
  await page.evaluate(()=>{const v=fixture.view.viewport,c=v.getCameraState();v.setCameraState({...c,radius:c.radius*.8,theta:c.theta+.2});});
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  assert.equal(calls.filter(c=>c.method==='CamCut.ReadToolAxisSamples').length,batchCalls,'selection and camera changes reuse the native sample batch');
  await command('five.normals.toggle');await idle();assert.equal(await page.evaluate(()=>!!fixture.view.fiveToolAxisDisplay.group?.parent),false);
  await command('five.normals.toggle');await idle();assert.equal((await display()).groups.length,ids.length);
  cases.push('all-path overlay toggles, keeps one instanced mesh per kind/path, and caches native samples across selection, zoom and orbit');
  console.log('Scene pose: native batch and display cache verified');

  await command('five.points.edit');await idle();await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('IJK');await field('I').waitFor();
  assert.equal(await page.locator('.five-axis-keypoints [data-five-command="five.points.start"],.five-axis-keypoints [data-five-command="five.points.end"],.five-axis-keypoints [data-five-command="five.points.pick"],.five-axis-keypoints [data-five-command="five.points.delete"]').count(),0,'point management belongs to the right pose controls');
  await page.getByRole('combobox',{name:'关键点刀轴坐标系',exact:true}).selectOption('Setup');
  await focusCut(page,id);await page.locator(`[data-five-intent="${id}"]`).click();await idle();assert.deepEqual(await page.evaluate(()=>fixture.view.fiveAxisUI.selectedIntentIds),[id]);
  await command('five.points.add');const at=await cutPoint(page,id);
  const beforeInsert=await read();await page.mouse.click(at.x,at.y);await idle();assert.equal(await page.evaluate(()=>!!fixture.view.fiveAxisPointInsert&&!fixture.view.fiveAxisPointEdit.newPoint&&!fixture.view.fiveAxisPointEdit.dirty),true);
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveAxisUI.selectedIntentIds),[id],'continuous insertion keeps its owning path selected');
  const anchor=await page.evaluate(()=>{const d=fixture.view.fiveAxisPointEdit;return d.points.find(p=>p.id===d.pointId).anchor;});assert.notDeepEqual((await read()).expected,beforeInsert.expected,'inserting a point commits through native history immediately');
  await command('five.points.add');await idle();assert.equal(await page.evaluate(()=>!!fixture.view.fiveAxisPointInsert),false);
  assert.equal((await display()).groups.length,ids.length,'all paths remain visible while editing a local point');
  const selectedPalette=await scenePaletteState();assert.ok(selectedPalette.lines.some(line=>line.id===id&&line.color===palette.selected),'the selected cutting path uses the selection color');
  const pointPalette=await page.evaluate(()=>{const group=fixture.view.fiveAxisPointPreview.group,arrows=group.children.filter(o=>o.userData.pointPreview).map(o=>o.material.uniforms.color.value.getHex());return{markers:group.children.filter(o=>o.isPoints).map(o=>({shape:o.userData.poseMarkerShape,color:o.material.color.getHex(),pointIds:o.userData.pointIds})),arrows};});
  assert.deepEqual(pointPalette.markers.map(m=>m.shape).sort(),['circle','ring','square']);for(const marker of pointPalette.markers)assert.equal(marker.color,palette[marker.shape==='ring'?'selected':'reference']);assert.ok(pointPalette.arrows.length>0&&pointPalette.arrows.every(color=>color===palette.selected));
  cases.push('cutting, actual axes, selection and references use distinct shared colors; legend matches materials; system/manual/active point shapes are distinct; ordinary sequence labels do not use error red');
  cases.push('right pose controls own whole/add/edit/delete actions; a real scene click commits the exact anchor and keeps continuous Add armed until the button is pressed again');
  const glyphPixels=await measurePoseGlyphs(page);
  for(const name of ['preview','existing']){
    const glyphs=glyphPixels.filter(g=>g.name===name),lo=name==='preview'?32:20,hi=name==='preview'?38:24;
    for(const glyph of glyphs)assert.ok(glyph.pixels>10&&glyph.diameter>=lo&&glyph.diameter<=hi,`${name} at zoom ${glyph.factor}: ${glyph.diameter}px`);
    assert.ok(Math.max(...glyphs.map(g=>g.diameter))-Math.min(...glyphs.map(g=>g.diameter))<=2);
  }
  cases.push('selected pose is yellow and about 35px; other poses remain about 22px across a 4x zoom range');
  console.log('Scene pose: exact keypoint inserted with real mouse');

  // The production pose editor reuses the same viewport transform manipulator
  // as workpiece rotation. Raycasting only locates handles; mouse events drive it.
  const {drag,pick}=gizmoDriver(page),angle=()=>page.locator('[data-five-point-angle]');
  await page.locator('[data-transform-space="world"]').click();
  await page.waitForFunction(()=>fixture.view.viewport.transformManipulator?.control.mode==='rotate');
  const base=await invoke('CamCut.ReadToolAxis',{expected:first.expected,intentId:id,anchor});
  const beforeDrag=await read(),beforeEdits=calls.filter(c=>c.method==='CamCut.EditToolAxisPoint').length;
  await drag('X',32,false);assert.deepEqual(await read(),beforeDrag,'a held rotation is preview only');
  const preview=await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number));
  assert.ok(Math.hypot(...preview.map((n,i)=>n-base.setupDirection[i]))>.001,'real ring drag changes IJK before release');
  await page.mouse.up();await idle();let state=await read();
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  assert.ok(await page.evaluate(()=>fixture.view.fiveToolAxisDisplay.group.children.find(c=>c.userData.intentId===fixture.view.fiveAxisPointEdit.id).children.find(m=>m.userData.kind==='axis').material.uniforms.previewIndex.value>=0),'the active preview replaces the saved arrow at its source anchor');
  const dragged=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(dragged.setupDirection,preview,2e-6);
  assert.equal(calls.filter(c=>c.method==='CamCut.EditToolAxisPoint').length,beforeEdits+1,'one native pose edit per mouse gesture');
  await angle().waitFor();await angle().fill('7');near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),rotate(base.setupDirection,[1,0,0],7),2e-6);
  await angle().press('Escape');await idle();near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),dragged.setupDirection,2e-6);assert.deepEqual(await read(),state);
  cases.push('real rotation-ring drag previews IJK continuously, keeps native state unchanged until release, and commits one normalized local pose');
  console.log('Scene pose: ring drag and replacement angle verified');

  // Start a fresh X gesture and independently check its numeric rotation in the
  // fixed Scene frame. No product quaternion/pose solver is used for the oracle.
  await pick('X');await angle().fill('25');const expected=rotate(dragged.setupDirection,[1,0,0],25);
  near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),expected,2e-6);assert.deepEqual(await read(),state);
  await angle().press('Escape');await idle();near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),dragged.setupDirection,2e-6);assert.deepEqual(await read(),state);
  await drag('Y',25,false);await page.keyboard.press('Escape');await page.mouse.up();await idle();assert.deepEqual(await read(),state);
  await drag('X',25,false);await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.mouse.up();await idle();assert.deepEqual(await read(),state);
  await page.evaluate(()=>fixture.view.viewport.renderer.domElement.addEventListener('pointerdown',e=>fixture.posePointer=e.pointerId,{once:true}));
  await drag('Y',25,false);await page.evaluate(()=>fixture.view.viewport.renderer.domElement.dispatchEvent(new PointerEvent('pointercancel',{pointerId:fixture.posePointer,pointerType:'mouse',bubbles:true})));await page.mouse.up();await idle();assert.deepEqual(await read(),state);
  await page.locator('[data-transform-space="local"]').click();await pick('X');await angle().fill('13');
  const matrix=first.placements.find(p=>p.id===original.placementId).transform,localX=[matrix[0],matrix[4],matrix[8]];
  near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),rotate(dragged.setupDirection,localX,13),2e-6);
  await angle().press('Escape');await idle();assert.deepEqual(await read(),state);await page.locator('[data-transform-space="world"]').click();
  cases.push('numeric scene rotation uses fixed Scene axes or the placement axes; Escape, pointer cancellation and lost window focus cancel drafts and held gestures without writing the native scene');

  await pick('X');await angle().fill('25');
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-axis-keypoints::after{content:"";display:block;height:1600px}.five-axis-keypoints{max-height:300px;overflow:auto}'});
  await page.evaluate(()=>{fixture.scenePoseNodes={angle:document.querySelector('[data-five-point-angle]'),input:document.querySelector('[aria-label="关键点 K"]'),canvas:document.querySelector('canvas'),mesh:[...fixture.view.viewport.sceneObjects.values()][0],events:0};fixture.scenePoseNodes.input.addEventListener('input',()=>fixture.scenePoseNodes.events++);fixture.holdMethod='CamCut.EditToolAxisPoint';fixture.hold=true;});
  await angle().press('Enter');await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const n=fixture.scenePoseNodes.input;n.focus({preventScroll:true});n.setSelectionRange(0,1);return ['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=90+20*i;return p.scrollTop;});});assert.ok(scroll.every(x=>x>0));
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>{const n=fixture.scenePoseNodes;return n.input===document.querySelector('[aria-label="关键点 K"]')&&n.canvas===document.querySelector('canvas')&&n.mesh===[...fixture.view.viewport.sceneObjects.values()][0]&&document.activeElement===n.input;}),true);
  assert.deepEqual(await field('K').evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,1]);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map(s=>document.querySelector(s).scrollTop)),scroll);
  await stress.evaluate(n=>n.remove());await field('K').fill(await field('K').inputValue());assert.ok(await page.evaluate(()=>fixture.scenePoseNodes.events)>0,'original input listener remains active');await command('five.points.cancel');await idle();
  state=await read();let numeric=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(numeric.setupDirection,expected,2e-6);
  const rightBase=[...numeric.setupDirection];await pick('X');await angle().fill('9');await page.locator('.five-axis-keypoints [data-five-command="five.points.apply"]').click();await idle();
  state=await read();numeric=await invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id,anchor});near(numeric.setupDirection,rotate(rightBase,[1,0,0],9),2e-6);
  await angle().fill('12');await angle().press('Escape');await idle();near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),numeric.setupDirection,2e-6);assert.deepEqual(await read(),state,'right-panel Apply establishes the new Escape restoration value');
  const changed=state.intentToolpaths.find(c=>c.id===id);for(const k of ['curveRef','featureRef','previewPoints','start','reversed','number'])assert.deepEqual(changed[k],original[k],k);
  const updated=(await display()).data.cuts.find(c=>c.intentId===id).samples.find(s=>s.anchor.sourceSpanId===anchor.sourceSpanId&&Math.abs(s.anchor.nativeParameter-anchor.nativeParameter)<1e-10);
  assert.ok(updated,'all-path overlay must include every saved hard point');near(updated.direction,numeric.direction,2e-6);
  const originalControls=batch.data.cuts.find(c=>c.intentId===id).samples;
  const unaffectedControls=originalControls.filter(s=>s.anchor.sourceSpanId!==anchor.sourceSpanId||Math.abs(s.anchor.nativeParameter-anchor.nativeParameter)>1e-9);
  const afterLocalEdit=(await display()).data;
  let preserved=0;for(const before of batch.data.cuts){const after=afterLocalEdit.cuts.find(c=>c.intentId===before.intentId);for(const sample of before.samples){if(before.intentId===id&&!unaffectedControls.includes(sample))continue;const same=after.samples.find(s=>s.anchor.sourceSpanId===sample.anchor.sourceSpanId&&Math.abs(s.anchor.nativeParameter-sample.anchor.nativeParameter)<1e-9);assert.ok(same);near(same.direction,sample.direction,1e-9);preserved++;}}
  assert.ok(preserved>100);assert.match(await page.locator('.five-point-edit-scope').innerText(),/当前姿态.*保留相邻/);
  cases.push(`real scene rotation and numeric edits preserve all ${preserved} other native reference directions across all 19 paths, including both neighboring controls on the edited path`);
  cases.push('delayed scene-rotation save preserves input/canvas/mesh nodes, listeners, focus, selection and the latest scroll in both panes and a nested editor; source cutting geometry remains unchanged');
  cases.push('right-panel Apply saves an angle preview and establishes the correct restoration value for later numeric Escape');

  const manual=await page.evaluate(()=>{const d=fixture.view.fiveAxisPointEdit;return d.points.find(p=>p.id===d.pointId);});assert.ok(manual&&!manual.system);
  await page.getByRole('combobox',{name:'当前刀轴关键点',exact:true}).selectOption('end');await idle();const beforeMarker=await read();
  const marker=await page.evaluate(({id,point})=>{const v=fixture.view.viewport,c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),o=v.sceneObjects.get(c.placementId),b=v.renderer.domElement.getBoundingClientRect(),p=o.localToWorld(v.camera.position.clone().set(...point)).project(v.camera);return{x:b.x+(p.x+1)*b.width/2,y:b.y+(1-p.y)*b.height/2};},{id,point:manual.point});
  await page.mouse.click(marker.x,marker.y);await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.pointId),manual.id);near(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.values.map(Number)),numeric.setupDirection,2e-6);assert.deepEqual(await read(),beforeMarker);
  cases.push('real scene marker click selects its saved keypoint and effective IJK without a native edit or stale numeric-angle draft');
  await page.locator(`[data-five-intent="${id}"]`).click();await idle();
  await command('five.points.add');await page.mouse.click(marker.x,marker.y);await idle();assert.deepEqual(await page.evaluate(()=>fixture.view.fiveAxisUI.selectedIntentIds),[id]);assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.pointId),manual.id);assert.equal(await page.evaluate(()=>!!fixture.view.fiveAxisPointInsert&&!fixture.view.fiveAxisPointEdit.newPoint),true);assert.deepEqual(await read(),beforeMarker);
  await command('five.points.add');await idle();
  cases.push('continuous Add on an existing marker selects the saved point without creating history or a duplicate and stays armed until toggled off');

  let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
  assert.equal(task.status,'Completed',task.error);await command('five.refresh');await idle();state=await read();
  const operation=setup(state).generatedGeometry.operations.find(o=>o.intentId===id);let hits=0;
  for(const control of unaffectedControls){let count=0;for(const part of operation.parts)for(let i=0;i<(part.sourceSamples?.length||0);i++){const source=part.sourceSamples[i];if(source.sourceSpanId===control.anchor.sourceSpanId&&Math.abs(source.nativeParameter-control.anchor.nativeParameter)<1e-9){near(part.directions[i],control.direction,1e-9);count++;}}assert.ok(count>0,'generated cutting data must contain the exact preserved neighboring reference, not just the edited point');}
  cases.push(`regenerated native cutting data contains and preserves every ${unaffectedControls.length} neighboring reference direction of the edited real-model contour`);
  for(const part of operation.parts)for(let i=0;i<(part.sourceSamples?.length||0);i++){
    const sample=part.sourceSamples[i];if(sample.sourceSpanId===anchor.sourceSpanId&&Math.abs(sample.nativeParameter-anchor.nativeParameter)<1e-10){near(part.directions[i],numeric.direction,2e-6);hits++;}
  }
  assert.ok(hits>0,'regenerated machining path must contain the exact inserted anchor with its scene-edited axis');
  cases.push('real native regeneration contains the inserted hard point with the scene-edited unit IJK');
  console.log('Scene pose: actual regenerated hard point verified');

  await command(`five.tree.ToggleLocked:${id}`);await idle();state=await read();assert.equal(state.intentToolpaths.find(c=>c.id===id).locked,true);
  assert.equal(await page.locator(`[data-five-intent="${id}"]`).isDisabled(),true);assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator?.control.enabled),false);
  assert.ok((await display()).groups.some(g=>g.id===id),'locking preserves the tool-axis display');
  await command(`five.tree.ToggleLocked:${id}`);await idle();await page.locator(`[data-five-intent="${id}"]`).click();await idle();
  cases.push('locked paths remain visible but cannot expose an enabled scene rotation manipulator');
  const beforePlacement=await read();await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));await page.evaluate(()=>fixture.view.viewport.fitView());await command('five.tool.rotate');await idle();
  assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator?.control.mode),'rotate');await pick('X');const placementAngle=page.locator('[data-five-placement-field="value"]');await placementAngle.fill('11');await placementAngle.press('Escape');await idle();assert.deepEqual(await read(),beforePlacement,'switching from pose editing to workpiece rotation retains an independent cancellable gizmo');
  await command('five.tool.select');await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();
  cases.push('leaving point editing and entering workpiece rotation preserves the shared manipulator with independent draft ownership');
  if(await page.locator('[data-five-command="five.display.numbers"]').getAttribute('aria-pressed')==='true')await command('five.display.numbers');
  await focusCut(page,id);await pick('X');await page.screenshot({path:resolve(output,'tool-axis-scene-keypoint-gizmo.png')});
  await command('five.points.whole');await idle();await page.evaluate(()=>fixture.view.viewport.fitView());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await page.screenshot({path:resolve(output,'tool-axis-scene-overview.png')});
  assert.deepEqual(shaderErrors,[],'native normal/axis instance data must render without WebGL shader failures');
  await verifySceneLayout({page,command,idle,read,id,output});cases.push('pose display and selection switches stay aligned at bottom left without overlapping hints, axes, projection buttons or legend at three viewport sizes');
  assert.ok(await page.evaluate(()=>fixture.view.fiveToolAxisDisplay.group.children.every(c=>c.children.every(m=>m.material.uniforms.previewIndex.value===-1))),'leaving point editing restores every saved arrow');
  const persisted=await read();await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});
  near((await invoke('CamCut.ReadToolAxis',{expected:reopened.expected,intentId:id,anchor})).direction,numeric.direction,2e-6);
  assert.deepEqual(reopened.intentToolpaths.map(c=>[c.id,c.poseRef]),persisted.intentToolpaths.map(c=>[c.id,c.poseRef]));
  cases.push('embedded save/reopen preserves the exact source anchor and scene-edited tool-axis policy');
  writeFileSync(resolve(output,'tool-axis-scene-report.json'),JSON.stringify({passed:true,operations:first.intentToolpaths.length,density,glyphPixels,displaySamples:batch.data.cuts.map(c=>({intentId:c.intentId,samples:c.samples.length})),batchReadTimesMs:calls.filter(c=>c.method==='CamCut.ReadToolAxisSamples').map(c=>c.elapsedMs),cases,coverage:{native:true,realMouse:true,browser:'Edge',standaloneCef:false}},null,2));
}
