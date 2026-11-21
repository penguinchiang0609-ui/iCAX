import {camPathKind} from './workspaceModel.mjs';
import {supportsToolAxis} from './toolAxisEligibility.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { sceneColors } from './scenePalette.mjs';
import { sameToolAxisAnchor } from './toolAxisAnchors.mjs';
import {camMachinePlayback} from './machinePlaybackContext.mjs';
import {placementWorldMatrix} from './setupWorldFrame.mjs';

const alive=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const state=view=>view.fiveToolAxisDisplay??={visible:true,key:'',loading:false,data:null,error:'',cache:new Map(),failed:new Map(),request:null};
const cuts=view=>(view.scene?.intentToolpaths||[]).filter(c=>c.enabled!==false&&supportsToolAxis(c));
const sampleKey=(view,c)=>JSON.stringify([view.scene?.domain?.expected?.epoch,c.id,c.poseRef,c.processRef,c.featureRef,c.curveRef,c.sourceState,c.start,c.reversed]);
const geometryKey=(view,rows)=>JSON.stringify([view.viewport?.id,rows.map(c=>[c.id,sampleKey(view,c)]),(view.scene?.domain?.placements||[]).map(p=>[p.id,placementWorldMatrix(view,p).elements])]);
const errorText=error=>/SOURCE_NOT_CURRENT|SOURCE_CHANGED/.test(error)?'工件已改变，请重新提取刀路。':error;

export function renderToolAxisDisplayControl(view){
  const d=state(view),label=d.visible?'隐藏全部刀路关键点姿态':'显示全部刀路关键点姿态';d.error=cuts(view).map(c=>d.failed.get(sampleKey(view,c))).find(Boolean)||'';
  return `<button type="button" class="five-normal-display" data-five-key="all-path-normals-toggle" data-five-command="five.normals.toggle" aria-pressed="${d.visible}" aria-label="${label}" title="${attr(d.error?errorText(d.error):'只显示起点、终点和手动关键点；紫色为刀轴，灰色为参考面法向')}">姿态显示：${d.visible?'显示':'隐藏'}${d.loading?'…':d.error?' !':''}</button>`;
}
export function toolAxisDisplayCommand(context,view,action,ops){
  if(!alive(context,view))return;
  const d=state(view);if(action==='retry'){d.failed.clear();d.error='';}else if(action==='toggle'){d.visible=!d.visible;if(d.visible){d.failed.clear();d.error='';}}else return;
  if(!d.visible)removeOverlay(d);ops.renderProject(context,view);
}
export function setToolAxisDisplayVisible(view,visible=true){const d=state(view);d.visible=!!visible;if(!d.visible)removeOverlay(d);}

const sameAnchor=(a,b)=>sameToolAxisAnchor(a,b,1e-12);
function highlightedIndex(mesh,hit){
  if(!hit||hit.intentId!==mesh.userData.toolAxisSamples?.[0]?.intentId)return -1;
  const target=hit.sample?.anchor||hit.anchor;if(!target)return -1;
  return mesh.userData.toolAxisSamples.findIndex(s=>sameAnchor(s.anchor,target));
}
function updatePickHighlight(view,mesh,hover){
  const mode=view.fivePoseSelection,active=mode?.active&&mesh.userData.kind==='axis';
  mesh.material.uniforms.hoverIndex.value=active?highlightedIndex(mesh,hover):-1;
  mesh.material.uniforms.selectedIndex.value=active?highlightedIndex(mesh,mode.selected):-1;
}
export function syncToolAxisPickHighlight(view,hover=view.fivePoseSelection?.hover){
  for(const cut of view.fiveToolAxisDisplay?.group?.children||[])for(const mesh of cut.children)updatePickHighlight(view,mesh,hover);
}

// One draw per cut/kind. Positions and directions are instance attributes;
// pixel offsets are applied after projection, so camera navigation never reads
// CAD again and never changes glyph size. End-on directions use a dot/cross.
function glyphGeometry(){
  const vertices=[],types=[];
  const stroke=(a,b,width,type)=>{const l=Math.hypot(b[0]-a[0],b[1]-a[1]),x=-(b[1]-a[1])/l*width/2,y=(b[0]-a[0])/l*width/2;
    const p=[[a[0]+x,a[1]+y],[a[0]-x,a[1]-y],[b[0]+x,b[1]+y],[b[0]-x,b[1]-y]];
    for(const i of [0,1,2,2,1,3]){vertices.push(...p[i],0);types.push(type);}
  };
  stroke([0,0],[22,0],1.15,0);stroke([16,3.5],[22,0],1.15,0);stroke([16,-3.5],[22,0],1.15,0);
  for(let i=0;i<20;++i){const a=i*Math.PI/10,b=(i+1)*Math.PI/10;stroke([Math.cos(a)*4.7,Math.sin(a)*4.7],[Math.cos(b)*4.7,Math.sin(b)*4.7],1,1);}
  stroke([-1,0],[1,0],2,2);stroke([-2,-2],[2,2],1.1,3);stroke([-2,2],[2,-2],1.1,3);
  for(let i=0;i<24;++i){const a=i*Math.PI/12,b=(i+1)*Math.PI/12;stroke([Math.cos(a)*6.4,Math.sin(a)*6.4],[Math.cos(b)*6.4,Math.sin(b)*6.4],1.35,4);}
  const geometry=new THREE.InstancedBufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));geometry.setAttribute('glyphType',new THREE.Float32BufferAttribute(types,1));return geometry;
}
// The display mask depends only on projection, CSS size and placement. Compare
// all displayed anchors within each cut, including opposite sides of closed
// contours, rather than thinning only consecutive curve samples.
function sampleVisibility(view,row,cut){
  const attribute=new THREE.InstancedBufferAttribute(new Float32Array(row.samples.length),1),referenceAttribute=new THREE.InstancedBufferAttribute(new Float32Array(row.samples.length),1);
  attribute.setUsage(THREE.DynamicDrawUsage);referenceAttribute.setUsage(THREE.DynamicDrawUsage);
  const distinct=row.samples.map(s=>s.direction.reduce((n,x,i)=>n+x*s.surfaceNormal[i],0)<=1-1e-6);
  const size=new THREE.Vector2(),projection=new Float64Array(16),cameraView=new Float64Array(16),world=new Float64Array(16),mvp=new THREE.Matrix4();let width=-1,height=-1,previousSelected=-2;
  const update=(renderer,camera,mesh)=>{
    const selected=view.fivePoseSelection?.active?view.fivePoseSelection.selected:null,target=selected?.sample?.anchor||selected?.anchor;
    const selectedIndex=selected?.intentId===row.intentId&&target?row.samples.findIndex(s=>sameAnchor(s.anchor,target)):-1;
    renderer.getSize(size);const p=camera.projectionMatrix.elements,v=camera.matrixWorldInverse.elements,w=mesh.matrixWorld.elements;
    if(width===size.x&&height===size.y&&previousSelected===selectedIndex&&p.every((x,i)=>x===projection[i])&&v.every((x,i)=>x===cameraView[i])&&w.every((x,i)=>x===world[i]))return;
    width=Math.max(1,size.x);height=Math.max(1,size.y);previousSelected=selectedIndex;projection.set(p);cameraView.set(v);world.set(w);mvp.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse).multiply(mesh.matrixWorld);
    const m=mvp.elements,screen=[],spacing=34,cells=new Map(),mask=attribute.array;mask.fill(0);
    for(let i=0;i<row.samples.length;++i){const [x,y,z]=row.samples[i].point,den=m[3]*x+m[7]*y+m[11]*z+m[15];if(Math.abs(den)<1e-12)continue;
      const sx=((m[0]*x+m[4]*y+m[8]*z+m[12])/den+1)*width/2,sy=(1-(m[1]*x+m[5]*y+m[9]*z+m[13])/den)*height/2,nz=(m[2]*x+m[6]*y+m[10]*z+m[14])/den;
      if(Number.isFinite(sx)&&Number.isFinite(sy))screen.push({i,x:sx,y:sy,on:den>0&&nz>=-1&&nz<=1&&sx>=0&&sx<=width&&sy>=0&&sy<=height});
    }
    // Keep the selected station visible while navigating; its neighbours are
    // thinned around it. Hover does not change density or move selectable glyphs.
    const pinned=screen.find(point=>point.i===selectedIndex&&point.on),ordered=pinned?[pinned,...screen.filter(point=>point!==pinned)]:screen;
    let shown=0;for(const point of ordered){if(!point.on)continue;const cx=Math.floor(point.x/spacing),cy=Math.floor(point.y/spacing);let close=false;
      for(let dx=-1;dx<=1&&!close;++dx)for(let dy=-1;dy<=1&&!close;++dy)for(const other of cells.get(`${cx+dx},${cy+dy}`)||[])if((point.x-other.x)**2+(point.y-other.y)**2<spacing**2){close=true;break;}
      if(close)continue;const key=`${cx},${cy}`;if(!cells.has(key))cells.set(key,[]);cells.get(key).push(point);mask[point.i]=1;++shown;
    }
    if(!shown&&row.samples.length){const nearest=screen.reduce((a,b)=>!a||(b.x-width/2)**2+(b.y-height/2)**2<(a.x-width/2)**2+(a.y-height/2)**2?b:a,null);mask[nearest?.i??0]=1;shown=1;}
    let references=0;for(let i=0;i<mask.length;++i){referenceAttribute.array[i]=mask[i]&&distinct[i]?1:0;references+=referenceAttribute.array[i];}
    attribute.needsUpdate=true;referenceAttribute.needsUpdate=true;cut.userData.renderedCount=shown;cut.userData.referenceRenderedCount=references;cut.userData.shownMask=mask;cut.userData.sampleCount=row.samples.length;cut.userData.spacingCssPx=spacing;
    for(const child of cut.children){child.userData.renderedCount=child.userData.kind==='normal'?references:shown;child.userData.shownMask=child.userData.kind==='normal'?referenceAttribute.array:mask;child.userData.densityMask=mask;child.userData.sampleCount=row.samples.length;}
  };
  return {attribute,referenceAttribute,update};
}
function glyphMesh(view,row,kind,visibility){
  const geometry=glyphGeometry(),points=[],directions=[];for(const s of row.samples){points.push(...s.point);directions.push(...(kind==='normal'?s.surfaceNormal:s.direction));}
  geometry.setAttribute('instancePoint',new THREE.InstancedBufferAttribute(new Float32Array(points),3));
  geometry.setAttribute('instanceDirection',new THREE.InstancedBufferAttribute(new Float32Array(directions),3));geometry.instanceCount=row.samples.length;
  geometry.setAttribute('instanceShown',kind==='normal'?visibility.referenceAttribute:visibility.attribute);geometry.setAttribute('instanceDensity',visibility.attribute);
  geometry.setAttribute('instanceIndex',new THREE.InstancedBufferAttribute(Float32Array.from(row.samples,(_,i)=>i),1));
  const material=new THREE.ShaderMaterial({uniforms:{resolution:{value:new THREE.Vector2(1,1)},color:{value:new THREE.Color(kind==='normal'?sceneColors.reference:sceneColors.toolAxis)},accent:{value:new THREE.Color(sceneColors.selected)},hoverColor:{value:new THREE.Color(0x68e6f3)},size:{value:kind==='normal'?.7:1},hoverIndex:{value:-1},selectedIndex:{value:-1},previewIndex:{value:-1}},
    vertexShader:`attribute vec3 instancePoint; attribute vec3 instanceDirection; attribute float glyphType; attribute float instanceShown; attribute float instanceIndex;
      uniform vec2 resolution; uniform float size; uniform float hoverIndex; uniform float selectedIndex; uniform float previewIndex; uniform float hideAll; varying float shown; varying float baseRing; varying float pickState;
      void main(){
        vec4 anchor=projectionMatrix*modelViewMatrix*vec4(instancePoint,1.0);
        vec4 next=projectionMatrix*modelViewMatrix*vec4(instancePoint+instanceDirection,1.0);
        vec2 delta=(next.xy/next.w-anchor.xy/anchor.w)*resolution;
        float length2d=length(delta); bool endOn=length2d<0.002;
        vec3 eyeDirection=mat3(modelViewMatrix)*instanceDirection;
        bool hovered=abs(instanceIndex-hoverIndex)<0.1;bool selected=abs(instanceIndex-selectedIndex)<0.1;bool highlighted=hovered||selected;
        pickState=hovered?1.0:selected?2.0:0.0;
        baseRing=glyphType>3.5?1.0:0.0;
        shown=instanceShown>0.5&&abs(instanceIndex-previewIndex)>0.1&&anchor.w>0.0&&(glyphType>3.5&&highlighted||!endOn&&glyphType<0.5||endOn&&glyphType>0.5&&glyphType<1.5||endOn&&glyphType>1.5&&glyphType<2.5&&eyeDirection.z>0.0||endOn&&glyphType>2.5&&glyphType<3.5&&eyeDirection.z<=0.0)?1.0:0.0;
        if(hideAll>0.5)shown=0.0;
        vec2 along=endOn?vec2(1.0,0.0):delta/max(length2d,0.000001);
        float thicker=highlighted&&glyphType<0.5?1.5:1.0;
        vec2 offset=(along*position.x+vec2(-along.y,along.x)*position.y*thicker)*size;
        gl_Position=anchor;gl_Position.xy+=offset*2.0/resolution*anchor.w;
      }`,
    fragmentShader:`uniform vec3 color;uniform vec3 accent;uniform vec3 hoverColor;varying float shown;varying float baseRing;varying float pickState;void main(){if(shown<0.5)discard;vec3 tint=pickState>1.5?accent:pickState>0.5?hoverColor:mix(color,accent,baseRing);gl_FragColor=vec4(tint,0.94);#include <colorspace_fragment>\n}`.replace(';#include',';\n#include'),
    transparent:true,depthTest:false,depthWrite:false,side:THREE.DoubleSide});
  material.uniforms.hideAll={value:0};
  const mesh=new THREE.Mesh(geometry,material);mesh.frustumCulled=false;mesh.raycast=()=>{};mesh.renderOrder=kind==='axis'?46:45;
  mesh.userData.kind=kind;mesh.userData.toolAxisSamples=row.samples.map(s=>({...s,intentId:row.intentId,kind}));
  mesh.onBeforeRender=(renderer,scene,camera)=>{
    renderer.getSize(material.uniforms.resolution.value);visibility.update(renderer,camera,mesh);updatePickHighlight(view,mesh,view.fivePoseSelection?.hover);
    // The draft replaces this station's saved axis, so rotation shows one
    // direction at the same screen size instead of two overlapping arrows.
    material.uniforms.previewIndex.value=kind==='axis'&&!mesh.userData.pointPreview?highlightedIndex(mesh,view.fiveAxisPointPreview):-1;
    material.uniforms.hideAll.value=view.fiveSimulation?.checking||!mesh.userData.pointPreview&&view.fiveAxisPointPreview?.influence&&view.fiveAxisPointPreview.intentId===row.intentId?1:0;
  };return mesh;
}
export function createToolAxisPointGlyph(view,intentId,point,direction){
  const attribute=new THREE.InstancedBufferAttribute(new Float32Array([1]),1);
  const mesh=glyphMesh(view,{intentId,samples:[{point:point.point,anchor:point.anchor,direction}]},'axis',{attribute,update(){}});
  mesh.material.uniforms.size.value=1.6;
  mesh.material.uniforms.color.value.setHex(sceneColors.selected);
  mesh.name='tool-axis-point-preview';mesh.userData.pointPreview=true;return mesh;
}
function removeOverlay(d){
  if(d.group){d.group.removeFromParent();d.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});}d.group=null;d.overlayKey='';
}
export function disposeToolAxisDisplay(view){
  const d=view.fiveToolAxisDisplay;if(!d)return;removeOverlay(d);d.request=null;d.loading=false;d.key='';d.data=null;d.cache.clear();d.failed.clear();view.fiveToolAxisDisplay=null;
}
function rebuild(view,d,rows,key){
  if(d.overlayKey===key&&d.group)return;removeOverlay(d);
  const group=new THREE.Group();group.name='five-axis-all-tool-axes';
  const placements=new Map((view.scene?.domain?.placements||[]).map(p=>[p.id,p]));
  for(const row of rows){const data=d.cache.get(sampleKey(view,row)),placement=placements.get(row.placementId);if(!data||!placement)continue;
    const cut=new THREE.Group();cut.name=`tool-axes-${row.id}`;cut.userData.intentId=row.id;cut.userData.placementId=row.placementId;cut.matrixAutoUpdate=false;cut.matrix.copy(placementWorldMatrix(view,placement));
    const visible={...data,samples:data.samples.filter(s=>s.keypointIds.length)};
    const visibility=sampleVisibility(view,visible,cut);cut.add(glyphMesh(view,visible,'normal',visibility),glyphMesh(view,visible,'axis',visibility));group.add(cut);
  }
  view.viewport.scene.add(group);d.group=group;d.overlayKey=key;
}
export function syncToolAxisDisplay(context,view,ops){
  const d=state(view);if(!alive(context,view)||view.fiveAxisArea!=='toolpath'||camMachinePlayback(view)||camPathKind(view)==='rapid'||!d.visible||!view.viewport){removeOverlay(d);return;}
  // An epoch change cannot reuse old pose data or complete an old async read.
  const epoch=view.scene?.domain?.expected?.epoch;if(d.epoch!==epoch){d.epoch=epoch;d.cache.clear();d.failed.clear();d.request=null;d.loading=false;d.error='';removeOverlay(d);}
  const rows=cuts(view),snapshot=geometryKey(view,rows);d.key=snapshot;
  const needed=rows.filter(c=>!c.poseDeferred&&c.sourceState!=='Pending'&&!d.cache.has(sampleKey(view,c))&&!d.failed.has(sampleKey(view,c)));
  const responseRows=rows.map(c=>d.cache.get(sampleKey(view,c))).filter(Boolean),errors=rows.filter(c=>d.failed.has(sampleKey(view,c))).map(c=>({intentId:c.id,error:d.failed.get(sampleKey(view,c))}));
  d.error=errors[0]?.error||'';d.data={frame:'Workpiece',geometry:'NominalCutDefinition',cuts:responseRows,errors};
  const displayKey=JSON.stringify([snapshot,responseRows.map(r=>r.intentId)]);rebuild(view,d,rows,displayKey);
  // Keep a bounded project-local cache so toggling enable and selecting paths
  // does not restart CAD work.
  if(d.request||!needed.length||view.pending)return;
  // The native read budget is per request, not a project/path limit. Drain
  // missing rows in bounded batches; the next render requests the remainder.
  const batch=needed.slice(0,256);
  const request={expected:structuredClone(view.scene.domain.expected),intentIds:batch.map(c=>c.id)},token={request,keys:new Map(batch.map(c=>[c.id,sampleKey(view,c)]))};d.request=token;d.loading=true;
  void context.sceneProxy.invoke('CamCut.ReadToolAxisSamples',request).then(data=>{
    if(!alive(context,view)||view.fiveToolAxisDisplay!==d||d.request!==token||view.scene?.domain?.expected?.epoch!==request.expected.epoch)return;
    for(const row of data.cuts||[]){const current=cuts(view).find(c=>c.id===row.intentId);if(current&&sampleKey(view,current)===token.keys.get(row.intentId))d.cache.set(token.keys.get(row.intentId),row);}
    for(const error of data.errors||[]){const current=cuts(view).find(c=>c.id===error.intentId);if(current&&sampleKey(view,current)===token.keys.get(error.intentId))d.failed.set(token.keys.get(error.intentId),errorText(error.error));}
  }).catch(error=>{
    if(!alive(context,view)||view.fiveToolAxisDisplay!==d||d.request!==token)return;
    // An unrelated scene mutation may supersede this read; retry against the
    // fresh expected value. Other failures stay explicit until toggle/retry.
    if(JSON.stringify(view.scene?.domain?.expected)!==JSON.stringify(request.expected))return;
    for(const [id,k] of token.keys){const current=cuts(view).find(c=>c.id===id);if(current&&sampleKey(view,current)===k)d.failed.set(k,errorText(error.message));}
  }).finally(()=>{
    if(view.fiveToolAxisDisplay!==d||d.request!==token)return;
    d.request=null;d.loading=false;
    // Project navigation may temporarily deactivate this same view. Its read
    // result is discarded above, but its own token must still settle so the
    // next activation can request fresh samples. Never render an inactive view.
    if(!alive(context,view))return;
    if(d.cache.size>512){const retained=new Set(cuts(view).map(c=>sampleKey(view,c)));for(const k of d.cache.keys())if(!retained.has(k))d.cache.delete(k);}
    ops.renderProject(context,view);
  });
}
