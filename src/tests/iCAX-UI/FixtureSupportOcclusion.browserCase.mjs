import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

export async function recheckSupportOcclusion({page,rpc,output,errors}) {
  await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size>=12);
  const initial=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  if(process.env.ICAX_FIXTURE_OCCLUSION_REGENERATE){
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.fixture'));
    for(const command of ['generate','confirm']) {
      await page.locator('[data-five-command="five.fixture.design.'+command+'"]').click();
      await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));
      await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,null,{timeout:600000});
      assert.equal(await page.evaluate(()=>fixture.view.error),'');
    }
    await page.waitForFunction(()=>{const v=fixture.view,d=v.scene.domain,p=d.placements.find(p=>p.setupId===d.activeSetupId),m=v.viewport.sceneObjects.get(p.id);return (m?.geometry.index?.count||m?.geometry.attributes.position.count)>20000;});
  }
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.select'));
  await page.evaluate(()=>new Promise(requestAnimationFrame));
  const measure=()=>page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const v=fixture.view,d=v.scene.domain,s=d.setups.find(s=>s.id===d.activeSetupId),parts=s.fixtureDesign.assembly.parts;
    const placement=d.placements.find(p=>p.setupId===s.id),mesh=v.viewport.sceneObjects.get(placement.id);
    mesh.updateMatrixWorld(true);
    const pos=mesh.geometry.attributes.position,indices=mesh.geometry.index,bins=new Map(),cell=4,vertices=[];
    for(let i=0;i<pos.count;++i) vertices.push(new THREE.Vector3().fromBufferAttribute(pos,i).applyMatrix4(mesh.matrixWorld));
    for(let i=0;i<(indices?.count||pos.count);i+=3){
      const t=[0,1,2].map(k=>vertices[indices?indices.getX(i+k):i+k]);
      const [a,b,c]=t,det=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(Math.abs(det)<1e-10)continue;
      for(let x=Math.floor(Math.min(...t.map(p=>p.x))/cell);x<=Math.floor(Math.max(...t.map(p=>p.x))/cell);++x)
      for(let y=Math.floor(Math.min(...t.map(p=>p.y))/cell);y<=Math.floor(Math.max(...t.map(p=>p.y))/cell);++y){const key=x+','+y;if(!bins.has(key))bins.set(key,[]);bins.get(key).push({a,b,c,det});}
    }
    fixture.workMask=(x,y)=>{
      let height=-Infinity;
      for(const {a,b,c,det} of bins.get(Math.floor(x/cell)+','+Math.floor(y/cell))||[]){
        const u=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/det,w=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/det;
        if(u>=0&&w>=0&&u+w<=1)height=Math.max(height,u*a.z+w*b.z+(1-u-w)*c.z);
      }return height;
    };
    fixture.workProbePoints=[];
    let samples=0,covered=0,above=0,maxAbove=-1e100,worst=null;
    for(const p of parts.filter(p=>p.role==='Support')) {
      const matrix=new THREE.Matrix4().set(...p.frame);
      for(let i=0;i<p.outline.length;++i){const a=p.outline[i],b=p.outline[(i+1)%p.outline.length],n=Math.max(1,Math.ceil(Math.hypot(b[0]-a[0],b[1]-a[1])/.5));
        for(let j=0;j<=n;++j)for(const depth of [0,.25,.5,.75,1]){
          const q=new THREE.Vector3(a[0]+(b[0]-a[0])*j/n,a[1]+(b[1]-a[1])*j/n,p.thickness*depth).applyMatrix4(matrix);++samples;
          let ceiling=Infinity;
          for(const {a,b,c,det} of bins.get(Math.floor(q.x/cell)+','+Math.floor(q.y/cell))||[]){
            const u=((b.y-c.y)*(q.x-c.x)+(c.x-b.x)*(q.y-c.y))/det,w=((c.y-a.y)*(q.x-c.x)+(a.x-c.x)*(q.y-c.y))/det;
            if(u< -1e-7||w< -1e-7||u+w>1+1e-7)continue;
            ceiling=Math.min(ceiling,u*a.z+w*b.z+(1-u-w)*c.z);
          }
          if(!Number.isFinite(ceiling))continue;++covered;const delta=q.z-ceiling;if(delta>1e-4)++above;
          if(delta<-.005&&delta>-.05&&Math.hypot(q.x,q.y)>15)fixture.workProbePoints.push(q.clone());
          if(delta>maxAbove){maxAbove=delta;worst={part:p.number,point:q.toArray(),ceiling};}
        }
      }
    }
    const actual={samples:0,covered:0,above:0,maxAbove:-1e100,worst:null};
    for(const p of parts.filter(p=>p.role==='Support')){const rendered=v.viewport.sceneObjects.get(p.partId);rendered.updateMatrixWorld(true);const position=rendered.geometry.attributes.position;
      for(let i=0;i<position.count;++i){const q=new THREE.Vector3().fromBufferAttribute(position,i).applyMatrix4(rendered.matrixWorld);++actual.samples;const ceiling=fixture.workMask(q.x,q.y);if(!Number.isFinite(ceiling))continue;++actual.covered;const delta=q.z-ceiling;if(delta>1e-4)++actual.above;
        if(delta>actual.maxAbove){actual.maxAbove=delta;actual.worst={part:p.number,point:q.toArray(),ceiling};}}
    }
    return {samples,covered,above,maxAbove,worst,actual,workpieceTriangles:(indices?.count||pos.count)/3,near:v.viewport.camera.near,far:v.viewport.camera.far,position:v.viewport.camera.position.toArray(),radius:v.viewport.cameraState.radius};
  });
  const before=await measure();writeFileSync(resolve(output,'measure.json'),JSON.stringify(before,null,2));console.log(JSON.stringify(before));
  const pixels=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,d=v.scene.domain,p=d.placements.find(p=>p.setupId===d.activeSetupId),work=vp.sceneObjects.get(p.id);
    const saved=[];
    for(const [id,m] of vp.sceneObjects){if(!m.isMesh)continue;const material=m.material,edge=m.userData.meshEdges;
      saved.push({m,visible:m.visible,color:material.color.clone(),emissive:material.emissive.clone(),before:m.onBeforeRender,edgeBefore:edge?.onBeforeRender,edgeColor:edge?.material.color.clone(),edgeVisible:edge?.visible});
      const isWork=id===p.id;material.color.setHex(isWork?0x0000ff:0x00ff00);material.emissive.setHex(isWork?0x0000ff:0x00ff00);
      m.onBeforeRender=()=>{m.material.color.setHex(isWork?0x0000ff:0x00ff00);m.material.emissive.setHex(isWork?0x0000ff:0x00ff00);};
      if(edge){edge.material.color.setHex(0xff0000);if(isWork)edge.visible=false;}
    }
    const originalBias=vp.options.meshEdgeDepthBiasPixels;
    const axes=vp.worldAxes?.visible;if(vp.worldAxes)vp.worldAxes.visible=false;
    const projection=vp.projectionMode;vp.cameraState.target.set(0,0,-5);vp.cameraState.radius=300;vp.setProjectionMode('orthographic');vp.setStandardView('top');
    const canvas=vp.renderer.domElement,w=canvas.width,h=canvas.height,gl=vp.renderer.getContext(),depthBits=gl.getParameter(gl.DEPTH_BITS),targets=new Map();
    const step=Math.max(1,Math.floor(fixture.workProbePoints.length/1500)),pixel=vp.camera.top/h*2;
    const right=new THREE.Vector3(1,0,0).applyQuaternion(vp.camera.quaternion),up=new THREE.Vector3(0,1,0).applyQuaternion(vp.camera.quaternion);
    const coverage=[-.55,-.375,-.125,0,.125,.375,.55].flatMap(x=>[-.55,-.375,-.125,0,.125,.375,.55].map(y=>right.clone().multiplyScalar(x*pixel).addScaledVector(up,y*pixel)));
    for(let i=0;i<fixture.workProbePoints.length;i+=step){const q=fixture.workProbePoints[i].clone().project(vp.camera),x=Math.floor((q.x+1)*w/2),y=Math.floor((q.y+1)*h/2);
      if(x<1||y<1||x>=w-1||y>=h-1)continue;
      const center=new THREE.Vector3((x+.5)/w*2-1,(y+.5)/h*2-1,0).unproject(vp.camera);
      // Test pixel interiors as well as their corners: a narrow slot can pass
      // through a pixel whose four corners all lie on opaque material.
      if(coverage.some(offset=>!Number.isFinite(fixture.workMask(center.x+offset.x,center.y+offset.y))))continue;
      targets.set(x+','+y,{x,y});
    }
    let lastPixels;
    const scan=(factor,bias)=>{vp.options.meshEdgeDepthBiasPixels=bias;for(const {m} of saved)m.material.polygonOffsetFactor=factor;vp.setStandardView('top');
      const data=lastPixels=new Uint8Array(w*h*4);gl.readPixels(0,0,w,h,gl.RGBA,gl.UNSIGNED_BYTE,data);let visible=0;const examples=[];
      for(const {x,y} of targets.values()){const offset=(y*w+x)*4;if(data[offset]>40||data[offset+1]>40||data[offset+2]<80){++visible;if(examples.length<8)examples.push({x,y,rgba:[...data.slice(offset,offset+4)]});}}return {visible,examples};};
    for(const s of saved)if(s.m!==work)s.m.visible=false;
    scan(0,0);const baseline=lastPixels;
    for(const [key,{x,y}] of targets){const offset=(y*w+x)*4;if(baseline[offset]>25||baseline[offset+1]>25||baseline[offset+2]<250)targets.delete(key);}
    for(const s of saved)s.m.visible=s.visible;
    const slopeOffset=scan(1,0),fixed=scan(0,1),calibration={noBias:scan(0,0)};
    if(fixed.visible)for(const bias of [2,4,8])calibration[bias]=scan(0,bias);
    if(originalBias==null)delete vp.options.meshEdgeDepthBiasPixels;else vp.options.meshEdgeDepthBiasPixels=originalBias;
    for(const s of saved){s.m.onBeforeRender=s.before;s.m.material.color.copy(s.color);s.m.material.emissive.copy(s.emissive);const edge=s.m.userData.meshEdges;if(edge){edge.onBeforeRender=s.edgeBefore;edge.material.color.copy(s.edgeColor);edge.visible=s.edgeVisible;}}
    if(vp.worldAxes)vp.worldAxes.visible=axes;vp.setProjectionMode(projection);vp.setViewDirection([1,-1,.6]);
    return {samples:targets.size,slopeOffset,fixed,calibration,depthBits,antialias:gl.getContextAttributes().antialias,pixelRatio:vp.renderer.getPixelRatio(),material:{opacity:work.material.opacity,depthTest:work.material.depthTest,depthWrite:work.material.depthWrite,transparent:work.material.transparent}};
  });writeFileSync(resolve(output,'pixel-occlusion.json'),JSON.stringify(pixels,null,2));console.log(JSON.stringify(pixels));
  assert.ok(pixels.samples>100);assert.ok(pixels.slopeOffset.visible>0);assert.equal(pixels.fixed.visible,0);
  const exact=await page.evaluate(async worst=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),d=fixture.view.scene.domain,p=d.placements.find(p=>p.setupId===d.activeSetupId),matrix=new THREE.Matrix4().set(...p.transform);
    const near=new THREE.Vector3(...worst.point).applyMatrix4(matrix.clone().invert()).toArray();
    const common={expected:d.expected,placementId:p.id,setupId:p.setupId};
    const faces=await fixture.context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',common),hits=[];
    for(const f of faces.faces)try{
      const r=await fixture.context.sceneProxy.invoke('CamPlacement.ResolveReference',{...common,query:{mode:'point',frame:'Workpiece',triangleIndex:f.first,nearPoint:near,maximumDistanceMm:1}});
      hits.push({faceId:r.faceId,point:new THREE.Vector3(...r.point).applyMatrix4(matrix).toArray(),direction:new THREE.Vector3(...r.direction).transformDirection(matrix).toArray(),distance:r.distanceMm});
    }catch(e){hits.push({error:e.message});}
    return hits;
  },before.worst);writeFileSync(resolve(output,'exact-worst.json'),JSON.stringify(exact,null,2));console.log(JSON.stringify(exact));
  await page.setViewportSize({width:1900,height:1100});
  const clipping=[];
  for(const [name,direction] of [['oblique',[1,-1,.6]],['left',[-1,0,.35]],['top',[0,0,1]]]){
    clipping.push(await page.evaluate(({direction})=>{const vp=fixture.view.viewport;vp.cameraState.target.set(0,0,-5);vp.cameraState.radius=300;vp.setViewDirection(direction);return {near:vp.camera.near,far:vp.camera.far};}, {direction}));
    await page.screenshot({path:resolve(output,name+'.png')});
  }
  const picking=await page.evaluate(async()=>{
    const v=fixture.view,d=v.scene.domain,p=d.placements.find(p=>p.setupId===d.activeSetupId),work=d.workpieces.find(w=>w.id===p.workpieceId),mesh=v.viewport.sceneObjects.get(p.id),g=mesh.geometry,pos=g.attributes.position;
    const common={expected:d.expected,setupId:p.setupId,placementId:p.id},faces=(await fixture.context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',common)).faces,results=[];
    for(const face of faces){const t=Number(face.first)+Math.floor(Number(face.count)/2),near=[0,0,0];for(let k=0;k<3;++k){const index=g.index?g.index.getX(t*3+k):t*3+k;near[0]+=pos.getX(index)/3;near[1]+=pos.getY(index)/3;near[2]+=pos.getZ(index)/3;}
      const query={mode:'point',frame:'Workpiece',triangleIndex:String(t),nearPoint:near,maximumDistanceMm:.05};
      const r=await fixture.context.sceneProxy.invoke('CamPlacement.ResolveReference',{...common,query});
      const cut=await fixture.context.sceneProxy.invoke('CamCut.ResolveFace',{...common,sourceGeometry:work.geometryRef,query});
      results.push({expected:face.faceId,actual:r.faceId,stable:r.query.faceId,distance:r.distanceMm,range:r.triangleRanges,cutRange:cut.triangleRanges,face});
    }
    return {triangles:(g.index?.count||pos.count)/3,faces,results};
  });
  assert.equal(picking.faces.reduce((n,f)=>n+Number(f.count),0),picking.triangles);
  for(const r of picking.results){assert.equal(r.actual,r.expected);assert.equal(r.stable,r.expected);assert.deepEqual(r.range,[{first:r.face.first,count:r.face.count}]);assert.deepEqual(r.cutRange,r.range);}
  assert.ok(clipping.every(c=>c.near>1&&c.far>c.near&&c.far/c.near<100));
  assert.deepEqual(errors,[]);
  await rpc({action:'export-project',path:resolve(output,'grid-support.i5cam')});
  if(!process.env.ICAX_FIXTURE_OCCLUSION_DIAGNOSTIC){assert.ok(before.maxAbove<=1e-4,JSON.stringify(before));assert.ok(before.actual.maxAbove<=1e-4,JSON.stringify(before.actual));}
  const after=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.deepEqual(after.workpieces.map(w=>w.geometryRef),initial.workpieces.map(w=>w.geometryRef));
  assert.deepEqual(after.placements,initial.placements);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.workpieces,after.workpieces);assert.deepEqual(reopened.fixtures,after.fixtures);
  await page.evaluate(async()=>{fixture.entry.mountProject(fixture.context);fixture.view=(await import('/src/apps/_shared/workbench/state/projectViewStore.mjs')).getProjectView(fixture.context.project.projectId);});
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.sceneObjects.size>=12);
  const restored=await measure();assert.equal(restored.workpieceTriangles,before.workpieceTriangles);assert.ok(restored.maxAbove<=1e-4,JSON.stringify(restored));assert.ok(restored.actual.maxAbove<=1e-4,JSON.stringify(restored.actual));
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,measurement:before,restored,exact,picking,clipping,pixels,cases:['full-thickness final plate outlines below the actual rendered workpiece mesh','GPU pixels hide support edges behind solid material; slope-offset control reproduces the defect','close oblique, left and top views with adequate depth precision','all refined face ranges resolve to the original CAD faces for registration and cut extraction','original machining geometry and placements unchanged; refined display and assembly survive save/reopen']},null,2));
}
