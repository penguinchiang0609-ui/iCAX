import * as THREE from '../../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

const points=new Set(['endpoint','midpoint','center','sphere-center','datum-point']);
const v=p=>new THREE.Vector3(...p);
const query=f=>f.query||{mode:f.mode,frame:'Workpiece',featureId:f.featureId};
export const planeNormal=f=>({...f,mode:'direction',featureId:`normal:${f.featureId}`,
  label:`${f.label||'平面'}法向`,derived:true,
  query:{mode:'direction',frame:'Workpiece',construction:{kind:'PlaneNormal',sources:[query(f)]}}});

// A viewport layer of the shared snap system, independent of the consuming tool.
// Features and exact frames come from the service. Meshes below are only visual
// handles; a pick retains its source reference for native re-resolution.
export class SnapFeatureScene {
  constructor(object,catalog,{allowedModes=null,extras=[],inference=false,candidateDisplay=null}={}){
    this.object=object;this.catalog=catalog;this.records=[];this.pointBatches=new Map();
    this.group=new THREE.Group();this.group.name='snap-features';object.add(this.group);
    if(!object.geometry.boundingBox)object.geometry.computeBoundingBox();this.size=Math.max(1,object.geometry.boundingBox.getSize(new THREE.Vector3()).length());
    this.candidateDisplay=candidateDisplay;this.highlightColor=candidateDisplay?0x61decc:0xffcc62;
    // Keep the box outline visible in every picking stage. Selectability still
    // follows allowedModes; these context strokes never capture scene picks.
    for(const kind of inference?[]:['OBB']){
      const lines=catalog.features.filter(f=>f.boxKind===kind&&f.mode==='line').flatMap(f=>f.edgeSamples.map(v));
      if(lines.length){const outline=new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(lines),new THREE.LineBasicMaterial({color:0xd7b777,transparent:true,opacity:.45,depthTest:false,depthWrite:false}));outline.renderOrder=54;outline.raycast=()=>{};this.group.add(outline);}
    }
    const permits=mode=>!allowedModes||allowedModes.includes(mode),seen=new Set();
    const add=(f,type,source=f)=>{
      // Coincident handles share one screen target; their supporting topology
      // remains in the catalog and is still available through edge/face picks.
      const round=n=>Math.round(n*1e6);
      let signature=JSON.stringify([type,...f.point.map(round),...(type==='point'?[]:f.direction.map(round))]);
      if(type==='line'){
        const ends=[f.edgeSamples[0],f.edgeSamples.at(-1)].map(p=>p.map(round));
        ends.sort((a,b)=>a[0]-b[0]||a[1]-b[1]||a[2]-b[2]);signature=JSON.stringify([type,...ends]);
      }
      if(type==='plane'||f.query?.construction?.kind==='PlaneNormal'){
        const normal=v(source.direction).normalize();
        if(type==='plane'&&[normal.x,normal.y,normal.z].find(n=>Math.abs(n)>1e-8)<0)normal.negate();
        signature=JSON.stringify([type,...normal.toArray().map(round),round(normal.dot(v(source.point))),...(source.planeCorners?.length?[source.planeCorners.map(p=>p.map(round))]:[])]);
      }
      if(seen.has(signature))return;seen.add(signature);
      const record={feature:{...f,query:query(f),geometryRef:catalog.geometryRef,
        triangleRanges:catalog.faces.filter(r=>f.faceIds?.includes(r.faceId))},type,source,objects:[]};
      this.records.push(record);this.draw(record);
    };
    // Prefer explicit context anchors and larger, legible reference surfaces.
    const displayed=candidateDisplay==='points'?catalog.features.filter(f=>points.has(f.mode)&&!f.boxKind)
      :candidateDisplay==='directions'?catalog.features.filter(f=>f.mode==='line'&&!f.boxKind)
      :inference?[]:catalog.features.slice().sort((a,b)=>(b.radius||0)-(a.radius||0));
    const features=[...extras,...displayed];
    for(const f of features){
      if(points.has(f.mode)&&permits(f.mode))add(f,'point');
      if((f.boxKind||candidateDisplay==='directions')&&f.mode==='line'&&permits('line'))add(f,'line');
      if(['axis','direction'].includes(f.mode)&&permits(f.mode))add(f,'direction');
      if(f.mode==='plane'){
        if(f.derived&&permits('plane'))add(f,'plane');
        if(permits('direction')){
          const normal=planeNormal(f);add(normal,'direction',f);
          if(f.derived)add(normal,'plane',f);
        }
      }
    }
    for(const [size,batch] of this.pointBatches){
      const geometry=new THREE.BufferGeometry().setFromPoints(batch.records.map(r=>v(r.feature.point)));
      batch.colors=new THREE.Float32BufferAttribute(batch.records.flatMap(r=>new THREE.Color(r.color).toArray()),3);geometry.setAttribute('color',batch.colors);
      const markers=new THREE.Points(geometry,new THREE.PointsMaterial({size,sizeAttenuation:false,vertexColors:true,depthTest:!!candidateDisplay,depthWrite:false}));markers.renderOrder=55;this.group.add(markers);
    }
    this.group.updateWorldMatrix(true,true);
  }
  draw(record){
    const f=record.source,center=v(f.point),color=f.boxKind==='OBB'?0xd7b777:record.type==='plane'?0x74a6bd:record.feature.query.construction?.kind==='PlaneU'?0xe87a80:record.feature.query.construction?.kind==='PlaneV'?0x6bc19a:this.candidateDisplay?0x36b9d7:0x89cbd9;record.color=color;
    const own=o=>{o.userData.snapFeature=record.feature;o.renderOrder=55;record.objects.push(o);this.group.add(o);return o;};
    if(record.type==='plane'){
      const size=Math.max(f.radius||this.size*.06,this.size*.008),u=v(f.xDirection).normalize(),normal=v(f.direction).normalize(),w=new THREE.Vector3().crossVectors(normal,u).normalize();
      const corners=f.planeCorners?.length===4?f.planeCorners.map(v):[[-1,-1],[1,-1],[1,1],[-1,1]].map(([x,y])=>center.clone().addScaledVector(u,x*size).addScaledVector(w,y*size));
      record.corners=corners;
      record.surface=own(new THREE.Mesh(new THREE.BufferGeometry().setFromPoints([corners[0],corners[1],corners[2],corners[0],corners[2],corners[3]]),
        new THREE.MeshBasicMaterial({color,transparent:true,opacity:.035,side:THREE.DoubleSide,depthWrite:false,depthTest:false})));
      own(new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(corners),new THREE.LineBasicMaterial({color,transparent:true,opacity:.5,depthTest:false,depthWrite:false})));
    }else if(record.type==='line'){
      record.ends=f.edgeSamples.map(v);
      const material=new THREE.LineBasicMaterial({color,transparent:true,opacity:.8,depthTest:!!this.candidateDisplay,depthWrite:false});
      if(this.candidateDisplay){
        const bias=Math.min(.05,this.size*1e-4);
        // Lift the overlay slightly in view depth so exact BRep edges remain
        // visible against the tessellation, retaining ordinary depth occlusion.
        material.onBeforeCompile=shader=>{shader.uniforms.icaxSnapDepthBias={value:bias};shader.vertexShader='uniform float icaxSnapDepthBias;\n'+shader.vertexShader.replace('#include <project_vertex>','#include <project_vertex>\nmvPosition.z += icaxSnapDepthBias;\ngl_Position = projectionMatrix * mvPosition;');};
      }
      own(new THREE.Line(new THREE.BufferGeometry().setFromPoints(record.ends),material));
      record.feature.lineEnds=f.edgeSamples;
    }else if(record.type==='point'){
      const size=['endpoint','midpoint'].includes(f.mode)?4:8;
      if(!this.pointBatches.has(size))this.pointBatches.set(size,{records:[]});
      const batch=this.pointBatches.get(size);record.pointBatch=batch;record.pointIndex=batch.records.length;batch.records.push(record);
    }else{
      const length=Math.max(this.size*.10,Math.min(f.radius||0,this.size*.25));
      record.end=center.clone().addScaledVector(v(record.feature.direction).normalize(),length);
      const arrow=new THREE.ArrowHelper(v(record.feature.direction).normalize(),center,length,color,length*.13,length*.055);
      arrow.traverse(o=>{if(o.geometry)o.geometry=o.geometry.clone();if(o.material){o.material.depthTest=false;o.material.depthWrite=false;}});own(arrow);
    }
    for(const object of record.objects)object.traverse(o=>{if(o.material)o.userData.snapColor=color;});
  }
  highlight(feature){
    const id=feature&&JSON.stringify(feature.query);
    if(id===this.highlighted)return;this.highlighted=id;
    for(const r of this.records){const active=id===JSON.stringify(r.feature.query)||r.type==='point'&&points.has(feature?.mode)&&r.feature.point.every((n,i)=>Math.abs(n-feature.point[i])<1e-8);
      if(r.pointBatch){const c=new THREE.Color(active?this.highlightColor:r.color);r.pointBatch.colors.setXYZ(r.pointIndex,c.r,c.g,c.b);r.pointBatch.colors.needsUpdate=true;}
      for(const object of r.objects)object.traverse(o=>{
      if(o.material?.color)o.material.color.setHex(active?this.highlightColor:o.userData.snapColor);
      if(o===r.surface)o.material.opacity=active?.14:.035;
    });}
  }
  candidate(viewport,event,radius=12){
    if(!this.object.visible||!this.group.parent||!Number.isFinite(event?.clientX))return null;
    this.group.updateWorldMatrix(true,true);
    const rect=viewport.renderer.domElement.getBoundingClientRect(),camera=viewport.camera,mouse={x:event.clientX,y:event.clientY};
    const project=p=>{const q=p.clone().applyMatrix4(this.object.matrixWorld).project(camera);return {x:rect.left+(q.x+1)*rect.width/2,y:rect.top+(1-q.y)*rect.height/2,z:q.z};};
    const ray=new THREE.Raycaster();ray.setFromCamera({x:(mouse.x-rect.left)/rect.width*2-1,y:1-(mouse.y-rect.top)/rect.height*2},camera);
    const candidates=[];
    for(const r of this.records){
      const a=project(v(r.feature.point));if(a.z< -1||a.z>1)continue;
      let gap=Math.hypot(a.x-mouse.x,a.y-mouse.y),rank=0,depth=a.z;
      if(r.type==='line'){
        const p=project(r.ends[0]),q=project(r.ends.at(-1)),dx=q.x-p.x,dy=q.y-p.y,n=dx*dx+dy*dy;if(n<1)continue;
        const t=Math.max(0,Math.min(1,((mouse.x-p.x)*dx+(mouse.y-p.y)*dy)/n));gap=Math.hypot(p.x+t*dx-mouse.x,p.y+t*dy-mouse.y);rank=1;depth=p.z+(q.z-p.z)*t;
      }else if(r.type==='direction'){
        const b=project(r.end),dx=b.x-a.x,dy=b.y-a.y;
        // Do not turn an end-on normal into a large invisible pick target.
        if(Math.hypot(dx,dy)<8)continue;
        const t=Math.max(.18,Math.min(1,((mouse.x-a.x)*dx+(mouse.y-a.y)*dy)/(dx*dx+dy*dy)));
        gap=Math.hypot(a.x+t*dx-mouse.x,a.y+t*dy-mouse.y);rank=1;
      }else if(r.type==='plane'){
        const hit=ray.intersectObject(r.surface,false)[0];if(!hit)continue;
        const ps=r.corners.map(project);if(Math.max(...ps.map(p=>Math.hypot(p.x-a.x,p.y-a.y)))<10)continue;
        // A front box must not swallow a nested OBB's explicit face centre.
        // The visible centre is a precise plane target; elsewhere use depth.
        rank=r.source.boxKind&&gap<=radius?1:2;gap=0;depth=hit.point.project(camera).z;
      }
      if(gap<=radius)candidates.push({r,gap,rank,depth});
    }
    // Collinear box centres can project onto the same pixel. Subpixel roundoff
    // must not change the chosen anchor between hover and pointer down.
    candidates.sort((a,b)=>a.rank-b.rank||(Math.abs(a.gap-b.gap)>.25?a.gap-b.gap:0)||a.depth-b.depth||a.r.feature.featureId.localeCompare(b.r.feature.featureId));
    const best=candidates[0];if(!best)return null;
    const result=best.r.feature;
    return {objectId:this.object.userData.objectId,result,request:{query:result.query},resolve:()=>Promise.resolve(result),helper:true,helperType:best.r.type};
  }
  dispose(){this.group.removeFromParent();this.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});this.records=[];}
}
