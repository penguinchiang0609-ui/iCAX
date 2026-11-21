import * as THREE from './three/three.module.js';
import {LineSegments2} from './three-lines/LineSegments2.js';
import {LineSegmentsGeometry} from './three-lines/LineSegmentsGeometry.js';
import {LineMaterial} from './three-lines/LineMaterial.js';
import {loadWorkData,renderWorkDetails,workLegend} from './workInspectionData.mjs?v=penetration';

const $=id=>document.getElementById(id), esc=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const number=value=>Number.isFinite(value)?Number(value.toPrecision(8)).toLocaleString('zh-CN',{maximumFractionDigits:8}):'未取得';
const STATUS={NotSelected:'不作为走刀',Ready:'刀轴已生成',ParallelToGenerator:'走向与母线平行 · 待处理',PartiallyParallelToGenerator:'部分走向与母线平行 · 待处理',UnsupportedSurface:'曲面母线尚未支持',InvalidGeometry:'几何分析失败'};
const MODE={None:'无刀轴',PlaneTangentCrossNormal:'平面内叉积方向',StraightGenerator:'曲面直线母线'};
const AXIS_COLOR='#f4df91';
const SKIN={OnSkin:'在皮上',OffSkin:'不在皮上',PartialOnSkin:'部分在皮上'};
const CONVEXITY={Convex:'凸',Concave:'凹',Coplanar:'局部共面／光顺',Free:'自由边',Mixed:'来源凹凸多值（见原边链）'};
const canvas=$('scene'), renderer=new THREE.WebGLRenderer({canvas,antialias:true});
renderer.setPixelRatio(Math.min(devicePixelRatio||1,2));renderer.outputColorSpace=THREE.SRGBColorSpace;
const scene=new THREE.Scene();scene.background=new THREE.Color('#111d2b');scene.add(new THREE.HemisphereLight(0xf1f6ff,0x647990,2.4));
const light=new THREE.DirectionalLight(0xffffff,2);light.position.set(2,-3,5);scene.add(light);
const camera=new THREE.PerspectiveCamera(37,1,.001,1e7);camera.up.set(0,0,1);
const facesGroup=new THREE.Group(),linesGroup=new THREE.Group(),axesGroup=new THREE.Group();scene.add(facesGroup,linesGroup,axesGroup);
const target=new THREE.Vector3(),raycaster=new THREE.Raycaster(),mouse=new THREE.Vector2();raycaster.params.Line2={threshold:6};
let models=[],model=null,data=null,faceMap=new Map(),edgeMap=new Map(),faceMeshes=[],edgeLines=[],axisLines=null;
let perFaceEdges=new Map(),displayMembers=new Map(),boundaryMap=new Map();
let selectedFace='',selectedEdge='',radius=1,orbit=5,theta=.8,phi=1.08,width=1,height=1,pointer=null,revision=0,drawPending=false;
let axisShown=0,axisComputed=0,materials=[],viewFocus=false,viewRadius=1,shownAxisLength=0,detailsKey='',faceListKey='';
let shownAxisMinLength=0,shownAxisMaxPixels=0,axisPixelLimit=12;
let workModels=new Map(),currentWork='pathAnalysis',currentView='',pendingWork='',pendingView='',fittedModelId='',loadedModelId='';
const WORK_LABEL={import:'导入',heal:'Heal 修复',align:'Align 对齐',contour:'Contour 轮廓',regionPartition:'区域划分',classify:'Classify 分面',edgeAnalysis:'EdgeAnalysis 边分析',pathAnalysis:'PathAnalysis 刀轴',faceCutCoverage:'FaceCutCoverage 覆盖',penetration:'切透不切透的判定'};
const SECTION_WORKS = {
  sectionContour: '原始截面',
  sectionSimplify: '轮廓简化',
  sectionRegions: '截面分区',
  sectionOverlap: '重合区拆分',
  sectionWallTransitions: '直弯过渡分区',
  sectionSharedCoverage: '共享壁区',
};
Object.assign(WORK_LABEL, SECTION_WORKS);
const LEGACY_VIEWS = {
  sectionContour: ['contour', 'original'],
  sectionSimplify: ['contour', 'simplified'],
  sectionRegions: ['regionPartition', 'partition'],
  sectionOverlap: ['regionPartition', 'overlap'],
  sectionWallTransitions: ['regionPartition', 'wallTransitions'],
  sectionSharedCoverage: ['regionPartition', 'sharedCoverage'],
};
const workTabs = $('workTabs'), workView = $('workView');
let availableWorks = new Set([...workTabs.children].map(button => button.dataset.work));
const WORK_STATE={passed:'已完成',complete:'已完成',partial:'部分完成',failed:'失败',missing:'产物缺失',
  'not-run':'未执行',running:'运行中',unsupported:'当前不支持',unresolved:'待解',timeout:'超时'};
const displayWorkStatus = entry => entry?.status === 'failed' ? 'failed' : entry?.resultStatus ?? entry?.status ?? 'missing';
function configureWorkTabs(works) {
  if (!Array.isArray(works)) return;
  const existing = new Map([...workTabs.children].map(button => [button.dataset.work, button]));
  availableWorks = new Set(works.map(work => work.id));
  for (const button of existing.values()) if (!availableWorks.has(button.dataset.work)) button.remove();
  for (const work of works) {
    const button = existing.get(work.id) ?? document.createElement('button');
    button.type = 'button';
    button.setAttribute('role', 'tab');
    button.dataset.work = work.id;
    button.textContent = work.label ?? WORK_LABEL[work.id] ?? work.id;
    WORK_LABEL[work.id] = button.textContent;
    workTabs.append(button);
  }
}
function defaultWork() {
  return ['pathAnalysis', 'classify'].find(work => availableWorks.has(work)) ?? availableWorks.values().next().value;
}
function resolveWork(next, requestedWork, requestedView) {
  let work = requestedWork, view = requestedView;
  if (!availableWorks.has(work)) {
    const legacy = LEGACY_VIEWS[work];
    [work, view] = legacy && availableWorks.has(legacy[0]) ? legacy : [defaultWork(), ''];
  }
  const parent = workEntry(next, work);
  const views = parent.views ?? {};
  if (!Object.hasOwn(views, view)) {
    view = Object.hasOwn(views, parent.defaultView) ? parent.defaultView : Object.keys(views)[0] ?? '';
  }
  return { work, view, views, entry: views[view] ?? parent };
}
function updateWorkView({ view, views }) {
  const options = Object.entries(views);
  $('workViewControl').hidden = !options.length;
  const existing = [...workView.options];
  if (options.length !== existing.length || options.some(([id], index) => existing[index].value !== id)) {
    workView.replaceChildren(...options.map(([id]) => new Option('', id)));
  }
  for (const option of workView.options) {
    const entry = views[option.value], state = displayWorkStatus(entry);
    option.textContent = entry.label ?? option.value;
    option.title = [WORK_STATE[state] ?? state, entry.reason].filter(Boolean).join(' · ');
  }
  workView.value = view;
}
function writeWorkHash(next, face = '', edge = '') {
  const hash = new URLSearchParams({ case: next.id, work: currentWork });
  if (currentView) hash.set('view', currentView);
  if (face) hash.set('face', face);
  if (edge) hash.set('edge', edge);
  history.replaceState(null, '', '#' + hash);
}
const pathStage=()=>currentWork==='pathAnalysis';
const classifiedStage=()=>['classify','edgeAnalysis','pathAnalysis','faceCutCoverage'].includes(currentWork)
  ||currentWork==='penetration'&&data?.counts?.machiningFaces>0;
function captureInteraction(){const active=document.activeElement;return {active,start:active?.selectionStart,end:active?.selectionEnd,direction:active?.selectionDirection,open:[...document.querySelectorAll('#details details[open]')].map(el=>el.querySelector('summary')?.textContent),scroll:[...document.querySelectorAll('.models,.scroll,.details')].map(el=>({el,top:el.scrollTop,left:el.scrollLeft}))};}
function restoreInteraction(saved){for(const el of document.querySelectorAll('#details details'))if(saved.open?.includes(el.querySelector('summary')?.textContent))el.open=true;if(saved.active?.isConnected&&document.activeElement!==saved.active){saved.active.focus({preventScroll:true});if(typeof saved.start==='number')saved.active.setSelectionRange(saved.start,saved.end,saved.direction);}for(const {el,top,left}of saved.scroll)if(el.isConnected){el.scrollTop=top;el.scrollLeft=left;}}
function updateWorkTabs(activeWork=currentWork){const entries=workModels.get(String(model?.id))?.works??{};for(const button of $('workTabs').querySelectorAll('[data-work]')){button.setAttribute('aria-selected',String(button.dataset.work===activeWork));button.dataset.status=displayWorkStatus(entries[button.dataset.work]);button.title=`${WORK_LABEL[button.dataset.work]} · ${WORK_STATE[button.dataset.status]??button.dataset.status}${entries[button.dataset.work]?.reason?' · '+entries[button.dataset.work].reason:''}`;}}
const point=p=>Array.isArray(p)&&p.length===3&&p.every(Number.isFinite);
const faceKind=f=>classifiedStage()?(f?.machining?'加工面':'非加工面'):'原面';
const faceName=id=>{const f=faceMap.get(String(id));return f?.machining?`F${f.machiningIndex} · ID ${id}`:`${faceKind(f)} ID ${id}`;};
const role=edge=>[edge.path?'<span class="role-path">走刀路径</span>':'',edge.side?'<span class="role-side">侧边</span>':''].filter(Boolean).join(' ＋ ')||'非路径／非侧边';
const edgeName=edge=>pathStage()?`G${edge.groupId} · P${edge.partIndex} · ${edge.curveKind}`:(edge.label??edge.pathKey);
const rows=(pairs)=>`<dl>${pairs.map(([k,v])=>`<dt>${esc(k)}</dt><dd>${v}</dd>`).join('')}</dl>`;
const raw=value=>`<pre class="evidence">${esc(JSON.stringify(value,null,2))}</pre>`;
function status(message,error=false){$('status').hidden=!message;$('status').textContent=message;$('status').classList.toggle('error',error);}
function draw(){if(drawPending)return;drawPending=true;requestAnimationFrame(()=>{drawPending=false;updateWorkLineDistances();renderer.render(scene,camera);});}
function updateWorkLineDistances(){camera.updateMatrixWorld(true);const p=new THREE.Vector3(),q=new THREE.Vector3();for(const object of edgeLines){if(!object.userData.workPattern||!object.material.dashed)continue;const a=object.geometry.attributes.instanceStart,b=object.geometry.attributes.instanceEnd,da=object.geometry.attributes.instanceDistanceStart,db=object.geometry.attributes.instanceDistanceEnd;let total=0;for(let i=0;i<a.count;i++){p.fromBufferAttribute(a,i).project(camera);q.fromBufferAttribute(b,i).project(camera);da.setX(i,total);const length=Math.hypot((p.x-q.x)*width*.5,(p.y-q.y)*height*.5);total+=Number.isFinite(length)?length:0;db.setX(i,total);}da.needsUpdate=db.needsUpdate=true;}}
function updateCamera(){phi=Math.max(.008,Math.min(Math.PI-.008,phi));camera.position.set(target.x+orbit*Math.sin(phi)*Math.cos(theta),target.y+orbit*Math.sin(phi)*Math.sin(theta),target.z+orbit*Math.cos(phi));camera.near=Math.max(orbit*1e-4,viewRadius*1e-4,1e-12);camera.far=Math.max(orbit+radius*4,viewRadius*8,1e-8);camera.updateProjectionMatrix();camera.lookAt(target);if(data)rebuildAxes();if(window.__pathAnalysisViewer?.loaded)Object.assign(window.__pathAnalysisViewer,{camera:{target:target.toArray(),orbit,theta,phi},orbit,viewFocus,viewRadius});draw();}
function resize(){const rect=$('viewport').getBoundingClientRect();width=Math.max(1,rect.width);height=Math.max(1,rect.height);renderer.setSize(width,height,false);camera.aspect=width/height;for(const m of materials)m.resolution.set(width,height);updateCamera();}
function selectedBounds(){const box=new THREE.Box3();for(const mesh of faceMeshes)if(mesh.userData.faceId===selectedFace)box.expandByObject(mesh);return box;}
function axisWorldLength(){let length=(data?.display?.axisLengthMm??radius*.02)*.25;if(selectedFace&&$('axisScope').value==='selection'){const box=selectedBounds();if(!box.isEmpty()){const size=box.getSize(new THREE.Vector3()),positive=[size.x,size.y,size.z].filter(v=>v>size.length()*1e-10);length=Math.min(length,size.length()*.10,...positive.map(v=>v*.20));}}return length*Number($('axisScale').value);}
function axisDisplayLength(p,a,worldLimit,pixelLimit){
  const root=new THREE.Vector3(...p).applyMatrix4(camera.matrixWorldInverse),depth=-root.z;
  if(depth<=camera.near*2)return {length:0,pixels:0};
  const axis=new THREE.Vector3(...a).normalize(),helper=Math.abs(axis.z)<.9?new THREE.Vector3(0,0,1):new THREE.Vector3(0,1,0),side=new THREE.Vector3().crossVectors(axis,helper).normalize();
  const rotation=new THREE.Matrix3().setFromMatrix4(camera.matrixWorldInverse),focal=height/(2*Math.tan(camera.fov*Math.PI/360));
  // These are the shaft tip and two arrowhead vertices, not toolpath samples.
  const offsets=[axis,axis.clone().multiplyScalar(.8).addScaledVector(side,.075),axis.clone().multiplyScalar(.8).addScaledVector(side,-.075)].map(v=>v.clone().applyMatrix3(rotation));
  let length=worldLimit;
  for(const v of offsets){
    const rate=focal*Math.hypot(v.x+root.x*v.z/depth,v.y+root.y*v.z/depth),denominator=rate+pixelLimit*v.z;
    if(denominator>0)length=Math.min(length,pixelLimit*depth/denominator);
    if(v.z>0)length=Math.min(length,(depth-camera.near*2)/v.z);
  }
  length=Math.max(0,length);
  const pixels=Math.max(...offsets.map(v=>focal*length*Math.hypot(v.x+root.x*v.z/depth,v.y+root.y*v.z/depth)/(depth-length*v.z)));
  return {length,pixels};
}
function fit(selection=false){
  if(selection&&!selectedFace)return;viewFocus=selection;
  const box=new THREE.Box3();for(const mesh of faceMeshes)if(!selection||mesh.userData.faceId===selectedFace)box.expandByObject(mesh);
  if(box.isEmpty())return;
  const original=box.getSize(new THREE.Vector3()).length();
  if(selection){const margin=Math.min(data.display?.axisLengthMm??original*.1,original*.10)*Number($('axisScale').value)*1.2;box.expandByScalar(margin);}
  box.getCenter(target);const extent=box.getSize(new THREE.Vector3()).length();viewRadius=Math.max(extent/2,1e-12);if(!selection)radius=viewRadius;
  // Fit the box in the current view, including each corner's perspective
  // depth. A world-space diagonal leaves long side views unnecessarily small.
  const direction=new THREE.Vector3(Math.sin(phi)*Math.cos(theta),Math.sin(phi)*Math.sin(theta),Math.cos(phi));
  const right=new THREE.Vector3(-Math.sin(theta),Math.cos(theta),0),up=new THREE.Vector3().crossVectors(direction,right);
  const vertical=Math.tan(camera.fov*Math.PI/360)*.86,horizontal=vertical*camera.aspect;
  let distance=0;
  for(const x of [box.min.x,box.max.x])for(const y of [box.min.y,box.max.y])for(const z of [box.min.z,box.max.z]){
    const offset=new THREE.Vector3(x,y,z).sub(target),depth=offset.dot(direction);
    distance=Math.max(distance,depth+Math.abs(offset.dot(right))/horizontal,depth+Math.abs(offset.dot(up))/vertical);
  }
  orbit=Math.max(distance+viewRadius*.02,1e-12);updateCamera();applySelection();
}
function clear(){for(const group of [facesGroup,linesGroup,axesGroup])while(group.children.length){const object=group.children[0];group.remove(object);object.geometry?.dispose();if(object.material&&!materials.includes(object.material))object.material.dispose();}for(const m of materials)m.dispose();materials=[];faceMeshes=[];edgeLines=[];axisLines=null;}
function line(segments,color,dashed,key,unresolved=false,reference=false,workPattern=''){const positions=[];for(const segment of segments){const points=segment.points??[];for(let i=1;i<points.length;i++)if(point(points[i-1])&&point(points[i]))positions.push(...points[i-1],...points[i]);}if(!positions.length)return;
  const geometry=new LineSegmentsGeometry();geometry.setPositions(positions);
  const dash=Math.max(data.display.axisLengthMm*.33,radius*1e-6);
  const material=new LineMaterial({color,linewidth:dashed?1.8:2.8,worldUnits:false,dashed,dashSize:dash,gapSize:dash*.6,depthTest:!$('xray').checked,depthWrite:false,transparent:true,opacity:1});material.resolution.set(width,height);materials.push(material);
  const pattern={solid:[],dashed:[9,5],'dash-dot':[11,4,2,4],dotted:[2,4]}[workPattern];if(pattern?.length){material.dashSize=9;material.gapSize=5;const period=pattern.reduce((a,b)=>a+b,0),intervals=[];let start=0;pattern.forEach((length,i)=>{if(i%2===0)intervals.push(`(phase >= ${start.toFixed(1)} && phase <= ${(start+length).toFixed(1)})`);start+=length;});material.onBeforeCompile=shader=>{shader.fragmentShader=shader.fragmentShader.replace(/if\s*\(\s*mod\(\s*vLineDistance\s*\+\s*dashOffset,\s*dashSize\s*\+\s*gapSize\s*\)\s*>\s*dashSize\s*\)\s*discard;/,`float phase = mod(vLineDistance + dashOffset, ${period.toFixed(1)}); if (!(${intervals.join(' || ')})) discard;`);};material.customProgramCacheKey=()=>`work-pattern-${workPattern}`;}
  const object=new LineSegments2(geometry,material);object.computeLineDistances();object.userData={pathKey:key,side:!reference&&dashed&&!unresolved,unresolved,reference,workPattern,faceId:unresolved?String(boundaryMap.get(key).faceId):''};object.renderOrder=unresolved?4:dashed?6:5;edgeLines.push(object);linesGroup.add(object);
}
function build({preserveView=false}={}){clear();boundaryMap=new Map((data.unresolvedBoundaries??[]).map(b=>[b.boundaryKey,b]));faceMap=new Map(data.faces.map(f=>[String(f.faceId),f]));edgeMap=new Map(data.edges.map(e=>[e.pathKey,e]));perFaceEdges=new Map();displayMembers=new Map();for(const e of data.edges){const owners=e.faceIds?.length?e.faceIds:[e.faceId],display=e.displaySpanKey;for(const id of owners){const face=String(id);if(!perFaceEdges.has(face))perFaceEdges.set(face,[]);perFaceEdges.get(face).push(e);}if(!displayMembers.has(display))displayMembers.set(display,[]);displayMembers.get(display).push(e);}
  for(const f of data.geometry?.faces??[]){if(!f.positions?.length||f.positions.length%3)continue;const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(f.positions,3));if(f.indices?.length)geometry.setIndex(f.indices);geometry.computeVertexNormals();const baseColor=faceMap.get(String(f.faceId))?.workColor??'#72869c';const material=new THREE.MeshStandardMaterial({color:baseColor,side:THREE.DoubleSide,transparent:true,opacity:Number($('opacity').value),roughness:.86,metalness:0,polygonOffset:true,polygonOffsetFactor:1,polygonOffsetUnits:1});const mesh=new THREE.Mesh(geometry,material);mesh.userData.faceId=String(f.faceId);mesh.userData.baseColor=baseColor;faceMeshes.push(mesh);facesGroup.add(mesh);}
  if(pathStage()){const renderedPaths=new Set(),renderedSides=new Set();for(const e of data.edges){const k=e.displaySpanKey;if(e.path&&!renderedPaths.has(k)){renderedPaths.add(k);line(e.previewSegments??[],'#1fe05c',false,e.pathKey);}if(e.side&&!renderedSides.has(k)){renderedSides.add(k);line(e.previewSegments??[],'#ff303b',true,e.pathKey);}}
  // Path eligibility never removes a machining boundary from inspection.
  const renderedReferences=new Set();for(const e of data.edges){const k=e.displaySpanKey;if(!e.path&&!e.side&&!renderedPaths.has(k)&&!renderedSides.has(k)&&!renderedReferences.has(k)){renderedReferences.add(k);line(e.previewSegments??[],'#849ab2',false,e.pathKey,false,true);}}
  }else{const rendered=new Set();for(const e of data.edges){if(rendered.has(e.displaySpanKey))continue;rendered.add(e.displaySpanKey);line(e.previewSegments??[],e.workColor??'#849ab2',!!e.workPattern&&e.workPattern!=='solid',e.pathKey,false,true,e.workPattern??'');const object=edgeLines.at(-1);if(object?.userData.pathKey===e.pathKey&&e.workCoverage){object.userData.workCoverage=true;object.renderOrder=7;}}}
  for(const b of boundaryMap.values())line(b.previewSegments??[],'#a1aab8',false,b.boundaryKey,true);
  if(!preserveView)fit();else{const box=new THREE.Box3();for(const mesh of faceMeshes)box.expandByObject(mesh);if(!box.isEmpty())radius=Math.max(box.getSize(new THREE.Vector3()).length()/2,1e-12);updateCamera();}applySelection();
}
function edgeRows(edges){return `<div class="edge-table">${edges.map(e=>`<button class="edge-row ${e.pathKey===selectedEdge?'active':''}" data-edge="${esc(e.pathKey)}"><b>${esc(edgeName(e))}</b> · ${role(e)}<small>${esc(faceName(e.faceId))} · ${esc(STATUS[e.status]??e.status)}</small></button>`).join('')}</div>`;}
function unresolvedHtml(boundaries){if(!boundaries.length)return '';return `<h3>边界待建立逻辑关系（${boundaries.length}）</h3><div class="notice warning">这些边界尚未可靠排序。灰线仅保留原边界，没有走刀路径或刀轴。</div>${boundaries.map(b=>`<details><summary>${esc(faceName(b.faceId))} · W${esc(b.wireId)}</summary><p class="muted">原Topo边：${(b.sourceEdgeIds??[]).map(x=>'E'+esc(x)).join('、')}</p><div class="evidence">${esc(b.reason)}</div></details>`).join('')}`;}
function faceDetails(face){const edges=perFaceEdges.get(String(face.faceId))??[];return `<h2>${esc(faceName(face.faceId))}</h2>${rows([['原稳定面 ID',esc(face.faceId)],['分类',esc(faceKind(face))],['来源标识',esc(face.sourceId||'未记录')],...(face.displaySkippedZeroArea?[['显示说明','已证明无面积，完整 BREP / STEP 仍保留此面']]:[]),['面内逻辑边',String(edges.length)],['走刀路径',String(edges.filter(e=>e.path).length)],['侧边',String(edges.filter(e=>e.side).length)],['其他加工边',String(edges.filter(e=>!e.path&&!e.side).length)],['平行母线待处理',String(edges.filter(e=>e.status.includes('Parallel')).length)]])}${face.sourceTags?.length?`<details><summary>来源与分类记录</summary>${raw(face.sourceTags)}</details>`:''}${unresolvedHtml([...boundaryMap.values()].filter(b=>String(b.faceId)===String(face.faceId)))}<h3>此面的逻辑边</h3>${edgeRows(edges)}`;}
function edgeDetails(e){
  const endpoints=e.endpoints??[],valid=endpoints.filter(s=>s.axisValid&&s.positionValid&&s.tangentValid);
  const sourceRows=(e.sourceUses??[]).map(u=>`<div class="evidence">E${esc(u.edgeId)} · ${esc(u.spanKey)}<br>参数 ${number(u.firstParameter)} ～ ${number(u.lastParameter)} · ${u.reversed?'反向':'正向'}<br>${esc(SKIN[u.skinRelation]??u.skinRelation)} · ${esc(CONVEXITY[u.convexity]??u.convexity)}<br>来源皮面：${u.sourceSkinMembership===true?(u.sourceSkinFaceIds??[]).map(id=>esc(faceName(id))).join('、')||'已归属（面编号未记录）':u.sourceSkinMembership===false?'无来源皮面归属':'未记录'}<br>相对理想皮最大偏差：${Number.isFinite(u.maxSkinDistanceMm)?number(u.maxSkinDistanceMm)+' mm':'未记录'}${u.skinReason?'<br>皮关系依据：'+esc(u.skinReason):''}</div>`).join('');
  return `<h2>逻辑边 ${esc(edgeName(e))}</h2><p>${role(e)}</p>${rows([
    ['所属原面',`<button data-face="${esc(e.faceId)}">${esc(faceName(e.faceId))}</button>`],['逻辑边标识',esc(e.pathKey)],
    ['线环 / 连续组',`W${esc(e.wireId)} / G${esc(e.groupId)} / P${esc(e.partIndex)}`],['闭合逻辑边',e.closed?'是':'否'],
    ['原Topo边 ID',(e.sourceEdgeIds??[]).map(x=>'E'+esc(x)).join('、')],['来源边段数',String(e.sourceUses?.length??0)],
    ['逻辑皮关系',esc(SKIN[e.skinRelation]??e.skinRelation)],['逻辑凹凸关系',esc(CONVEXITY[e.convexity]??e.convexity??'未取得')],
    ['路径依据',e.path&&e.onSkinPath?'皮上的逻辑边':'未选为实际刀路'],
    ['非皮凸区间证据',e.convexEvidence?'有，供后续切透分析使用':'无'],
    ['曲面类型',esc(e.surfaceKind)],['刀轴来源',esc(MODE[e.axisMode]??e.axisMode)],['方向选取',valid.length?(valid.every(s=>s.reason?.includes('axis-sense-into-trimmed-machining-face'))?'朝当前加工面内侧':'旧结果：朝向尚未修正'):'待处理'],['处理状态',esc(STATUS[e.status]??e.status)],['有效首尾刀轴',`${valid.length} / ${endpoints.length}`]
  ])}<div class="notice ${e.status.includes('Parallel')||e.status==='UnsupportedSurface'||e.status==='InvalidGeometry'?'warning':''}">${esc(e.reason||'无附加说明')}</div>
  <p class="notice">整条逻辑边只处理行走首尾两点。绿色／红色曲线沿完整原边链绘制，不把首尾连成直线。</p>
  ${e.path&&e.side?'<p class="notice">此逻辑边同时满足路径与侧边规则：绿色实线与红色虚线分别绘制，可用上方开关单独查看。</p>':''}
  ${(displayMembers.get(e.displaySpanKey)?.length??0)>1?'<h3>同一几何边的逐面记录</h3>'+edgeRows(displayMembers.get(e.displaySpanKey)):''}
  <details open><summary>逻辑边首尾刀轴</summary>${endpoints.map(s=>`<div class="evidence"><b>${s.endpoint==='start'?'起点':'终点'} · 原边 E${esc(s.sourceEdgeId)} · 参数 ${number(s.parameter)}</b><br>位置：${s.positionValid&&point(s.position)?s.position.map(number).join(', '):'未得到'}<br>边界行走切向：${s.tangentValid&&point(s.tangent)?s.tangent.map(number).join(', '):'未得到'}<br>所属加工面法向：${point(s.normal)?s.normal.map(number).join(', '):'未得到'}${e.axisMode==='StraightGenerator'?'<br>原母线：'+(point(s.generator)?s.generator.map(number).join(', '):'未得到'):''}<br>刀轴：${s.axisValid?s.axis.map(number).join(', '):'待处理'}<br>状态：${esc(STATUS[s.status]??s.status)}${s.reason?'<br>'+esc(s.reason):''}</div>`).join('')}</details>
  <details><summary>组成逻辑边的完整原边链（${e.sourceUses?.length??0} 段）</summary>${sourceRows}</details><details><summary>完整逻辑边记录</summary>${raw(e)}</details>`;
}
function details(){if(!pathStage()){$('details').innerHTML=renderWorkDetails(data,{selectedFace,selectedEdge});$('selectionCaption').textContent=selectedEdge?edgeName(edgeMap.get(selectedEdge)):selectedFace?faceName(selectedFace):`${WORK_LABEL[currentWork]} · 所有本阶段面与边均保留`;return;}if(selectedEdge&&edgeMap.has(selectedEdge))$('details').innerHTML=edgeDetails(edgeMap.get(selectedEdge));else if(selectedFace&&faceMap.has(selectedFace))$('details').innerHTML=faceDetails(faceMap.get(selectedFace));else $('details').innerHTML=`<h2>完整模型</h2>${rows([['完整模型面',String(data.counts.wholeFaces)],['加工面',String(data.counts.machiningFaces)],['逐面逻辑边',String(data.edges.length)],['走刀路径',String(data.counts.pathOccurrences)],['侧边',String(data.counts.sideOccurrences)],['其他加工边',String(data.edges.filter(e=>!e.path&&!e.side).length)],['刀轴已生成',String(data.counts.ready)],['平行母线待处理',String(data.counts.pendingParallel)],['暂不支持曲面',String(data.counts.unsupported)],['几何分析失败',String(data.counts.invalid)],['边界待建立逻辑关系',String(boundaryMap.size)]])}<div class="notice">同一共享边在两个面上分别记录刀轴。点击原面或边查看，不合并不同面的姿态。</div>${data.error?`<p class="bad">${esc(data.error)}</p>`:''}${unresolvedHtml([...boundaryMap.values()])}<h3>分析信息</h3><ul class="diagnostics">${(data.diagnostics??[]).map(x=>`<li>${esc(x)}</li>`).join('')}</ul><details><summary>输出文件检查</summary>${raw(data.artifacts)}</details><h3>全部加工边</h3>${edgeRows(data.edges)}`;
  $('selectionCaption').textContent=selectedEdge?`${faceName(edgeMap.get(selectedEdge).faceId)} · ${edgeName(edgeMap.get(selectedEdge))}`:selectedFace?faceName(selectedFace):'完整模型 · 路径绿色，刀轴淡黄色，侧边红色';
  const entry=data.inspection?.entry;if(entry)$('details').insertAdjacentHTML('beforeend',`<details><summary>本 Work 输入与产出来源</summary>${rows([['运行批次',esc(entry.provenance?.run??'未记录')],['来源 CAD',esc(entry.provenance?.source??'未记录')],['CAD SHA-256',esc(entry.provenance?.sourceSha256??'未记录')]])}${(entry.artifacts??[]).map(a=>`<div class="evidence"><a href="${esc(a.url)}" download>${esc(a.label)}</a><br>${esc(a.sourcePath??'')}<br>${esc(a.sha256??'')}</div>`).join('')}</details>`);
}
function addArrow(positions,p,a,length){const start=new THREE.Vector3(...p),axis=new THREE.Vector3(...a).normalize(),end=start.clone().addScaledVector(axis,length);positions.push(...start.toArray(),...end.toArray());const helper=Math.abs(axis.z)<.9?new THREE.Vector3(0,0,1):new THREE.Vector3(0,1,0);const side=new THREE.Vector3().crossVectors(axis,helper).normalize();for(const sign of [-1,1]){const wing=end.clone().addScaledVector(axis,-length*.20).addScaledVector(side,length*.075*sign);positions.push(...end.toArray(),...wing.toArray());}}
function rebuildAxes(){if(axisLines){axesGroup.remove(axisLines);axisLines.geometry.dispose();axisLines.material.dispose();axisLines=null;}
  const scope=$('axisScope').value,candidates=[];axisComputed=0;
  if(pathStage()&&$('showAxes').checked&&scope!=='none')for(const e of data.edges){if(!e.path)continue;if(scope==='selection'&&selectedFace&&String(e.faceId)!==selectedFace)continue;if(scope==='selection'&&selectedEdge&&e.pathKey!==selectedEdge)continue;
    const valid=(e.endpoints??[]).filter(s=>s.axisValid&&s.positionValid&&s.tangentValid&&point(s.position)&&point(s.axis));axisComputed+=valid.length;candidates.push(...valid);}
  camera.updateMatrixWorld(true);
  const positions=[],worldLimit=axisWorldLength();axisPixelLimit=Math.min(24,12*Number($('axisScale').value));shownAxisLength=0;shownAxisMinLength=Infinity;shownAxisMaxPixels=0;axisShown=0;
  for(const endpoint of candidates){const display=axisDisplayLength(endpoint.position,endpoint.axis,worldLimit,axisPixelLimit);if(display.length<=0)continue;addArrow(positions,endpoint.position,endpoint.axis,display.length);shownAxisLength=Math.max(shownAxisLength,display.length);shownAxisMinLength=Math.min(shownAxisMinLength,display.length);shownAxisMaxPixels=Math.max(shownAxisMaxPixels,display.pixels);axisShown++;}
  if(!axisShown)shownAxisMinLength=0;
  if(positions.length){const geometry=new LineSegmentsGeometry();geometry.setPositions(positions);const material=new LineMaterial({color:AXIS_COLOR,linewidth:2,worldUnits:false,depthTest:!$('xray').checked,depthWrite:false,transparent:true,opacity:1});material.resolution.set(width,height);axisLines=new LineSegments2(geometry,material);axisLines.renderOrder=7;axesGroup.add(axisLines);}
  $('axisInfo').textContent=`显示 ${axisShown} 根 · 短箭头 · 每条逻辑边仅处理首尾`;if(window.__pathAnalysisViewer?.loaded)Object.assign(window.__pathAnalysisViewer,{shownAxisLengthMm:shownAxisLength,shownAxisMinLengthMm:shownAxisMinLength,shownAxisMaxPixels,axisPixelLimit,axisShown,axisComputed,viewFocus,viewRadius,orbit});
}
function applySelection(){if(!data)return;const opacity=Number($('opacity').value),focus=selectedFace||edgeMap.get(selectedEdge)?.faceId;
  for(const mesh of faceMeshes){const selected=String(mesh.userData.faceId)===String(focus);mesh.material.color.set(selected?(currentWork==='classify'?(faceMap.get(mesh.userData.faceId)?.machining?'#ffd09b':'#bac9d9'):'#a4b8cc'):(mesh.userData.baseColor??'#72869c'));mesh.material.opacity=focus&&!selected?Math.max(.08,opacity*.28):opacity;}
  for(const object of edgeLines){if(object.userData.unresolved){const belongs=!focus||object.userData.faceId===String(focus);object.visible=!viewFocus||belongs;object.material.depthTest=!$('xray').checked;object.material.opacity=belongs?.9:.25;object.material.linewidth=1.4;continue;}const e=edgeMap.get(object.userData.pathKey),belongs=!focus||(displayMembers.get(e.displaySpanKey)??[e]).some(item=>(item.faceIds?.length?item.faceIds:[item.faceId]).some(id=>String(id)===String(focus))),side=object.userData.side;object.visible=(object.userData.reference?true:side?$('showSides').checked:$('showPaths').checked)&&(!viewFocus||belongs);object.material.depthTest=!$('xray').checked;object.material.opacity=belongs?1:.3;object.material.linewidth=selectedEdge===e.pathKey?(side?3:4.6):(object.userData.workCoverage?3.2:object.userData.reference?1.5:side?1.8:2.8);if(object.userData.workCoverage)object.renderOrder=selectedEdge===e.pathKey?9:belongs?7:5;}
  rebuildAxes();const nextDetailsKey=`${revision}:${currentWork}:${selectedFace}:${selectedEdge}`;if(nextDetailsKey!==detailsKey){const saved=captureInteraction();details();detailsKey=nextDetailsKey;restoreInteraction(saved);}updateFaceList();draw();window.__pathAnalysisViewer={loaded:true,modelId:String(model?.id),work:currentWork,view:currentView,workStatus:data.inspection?.entry?.status,identityGroup:data.inspection?.identityGroup,camera:{target:target.toArray(),orbit,theta,phi},renderedBoundaryCount:edgeLines.length,wholeFaceCount:faceMap.size,machiningFaceCount:data.counts?.machiningFaces,edgeOccurrenceCount:edgeMap.size,unresolvedBoundaryCount:boundaryMap.size,pathCount:data.counts.pathOccurrences,sideCount:data.counts.sideOccurrences,selectedFace,selectedEdge,axisShown,axisComputed,axisLengthMm:data.display?.axisLengthMm,shownAxisLengthMm:shownAxisLength,shownAxisMinLengthMm:shownAxisMinLength,shownAxisMaxPixels,axisPixelLimit,viewFocus,viewRadius,orbit,complete:data.complete,success:data.success,coverageCounts:currentWork==='faceCutCoverage'?{through:data.counts.throughSpans??0,partial:data.counts.partialSpans??0,pending:data.counts.pendingSpans??0}:null,renderedCoverageCount:edgeLines.filter(object=>object.userData.workCoverage).length,getEdgeStyles:()=>edgeLines.map(object=>({pathKey:object.userData.pathKey,coverage:object.userData.workCoverage===true,color:'#'+object.material.color.getHexString(),width:object.material.linewidth,visible:object.visible})),colors:{path:'#1fe05c',axis:AXIS_COLOR,side:'#ff303b',through:'#1fe05c',partial:'#f59e0b',pending:'#94a3b8'}};
}
function select(face='',edge='',hash=true,preserveView=false){selectedEdge=edge&&edgeMap.has(edge)?edge:'';const owners=selectedEdge?(edgeMap.get(selectedEdge).faceIds??[edgeMap.get(selectedEdge).faceId]).map(String):[];selectedFace=selectedEdge?(owners.includes(String(face||selectedFace))?String(face||selectedFace):owners[0]??''):(face&&faceMap.has(String(face))?String(face):'');applySelection();if(viewFocus&&selectedFace&&!preserveView)fit(true);if(hash)writeWorkHash(model,selectedFace,selectedEdge);}
function updateModelList(){const q=$('modelSearch').value.toLowerCase(),active=pendingWork||currentWork,matching=models.filter(m=>`${m.id} ${m.name}`.toLowerCase().includes(q)),wanted=new Set(matching.map(m=>String(m.id))),existing=new Map([...$('modelList').children].map(el=>[el.dataset.model,el]));for(const [id,el]of existing)if(!wanted.has(id))el.remove();let cursor=$('modelList').firstElementChild;for(const m of matching){let button=existing.get(String(m.id));if(!button){button=document.createElement('button');button.dataset.model=String(m.id);button.append(document.createElement('b'),document.createElement('small'));}const record=workModels.get(String(m.id))?.works?.[active],state=displayWorkStatus(record);button.firstElementChild.textContent=`${m.id} · ${m.name}`;button.lastElementChild.textContent=record?`${WORK_LABEL[active]} · ${WORK_STATE[state]??state}`:active==='pathAnalysis'?`${m.counts?.pathOccurrences??'?'} 条路径`:'尚无本阶段记录';button.classList.toggle('active',String(model?.id)===String(m.id));if(button!==cursor)$('modelList').insertBefore(button,cursor);cursor=button.nextElementSibling;}}
function updateFaceList(){if(!data)return;const q=$('faceSearch').value.toLowerCase(),matching=[...faceMap.values()].filter(f=>`${faceName(f.faceId)} ${f.faceId}`.toLowerCase().includes(q)),wanted=new Set(matching.map(f=>String(f.faceId))),existing=new Map([...$('faceList').children].map(el=>[el.dataset.face,el]));for(const [id,el]of existing)if(!wanted.has(id))el.remove();let cursor=$('faceList').firstElementChild;for(const f of matching){let button=existing.get(String(f.faceId));if(!button){button=document.createElement('button');button.dataset.face=String(f.faceId);button.append(document.createElement('b'),document.createElement('small'));}const edges=perFaceEdges.get(String(f.faceId))??[];button.firstElementChild.textContent=faceName(f.faceId);button.lastElementChild.textContent=pathStage()?(f.machining?`${edges.filter(e=>e.path).length} 路径 · ${edges.filter(e=>e.side).length} 侧边${f.unresolvedBoundaryKeys?.length?' · '+f.unresolvedBoundaryKeys.length+' 边界待处理':''}`:faceKind(f)+' · 无加工边分析'):currentWork==='faceCutCoverage'?`${edges.filter(e=>e.workCoverage).length} 覆盖区间 · ${edges.filter(e=>!e.workCoverage).length} 原边参考 · ${faceKind(f)}`:`${edges.length} 条本阶段边 · ${faceKind(f)}`;button.classList.toggle('active',String(f.faceId)===selectedFace);if(button!==cursor)$('faceList').insertBefore(button,cursor);cursor=button.nextElementSibling;}}
async function json(url){const response=await fetch(url,{cache:'no-store'});if(!response.ok)throw Error(`${response.status}: ${url}`);return response.json();}
function workEntry(next,work){return workModels.get(String(next.id))?.works?.[work]??(work==='pathAnalysis'?{status:next.status??'missing',dataUrl:next.dataUrl,geometryUrl:next.dataUrl,identityGroup:'classified-id',coordinateNote:'本批 Classify 坐标：+X 对齐',artifacts:[],provenance:{}}:{status:'missing',reason:'本模型尚无该 Work 的产物索引',identityGroup:['classify','edgeAnalysis','faceCutCoverage'].includes(work)?'classified-id':work+'-id',artifacts:[],provenance:{}});}
function workChrome(entry){updateWorkTabs();canvas.setAttribute('aria-label',WORK_LABEL[currentWork]+' · 完整模型与全部本阶段边');const path=pathStage();for(const id of ['pathLegend','axisLegend','pathLegendNote'])$(id).hidden=!path;$('stageLegend').hidden=path;if(!path)$('stageLegend').innerHTML=workLegend(currentWork);$('workInfo').textContent=`${WORK_LABEL[currentWork]} · ${WORK_STATE[entry.status]??entry.status??'未记录'} · ${entry.coordinateNote??''}${entry.reason?' · '+entry.reason:''}`;if(entry.previewStatus==='partial'||entry.previewStatus==='failed')$('workInfo').textContent+=' · 三维预览'+(entry.previewStatus==='partial'?'部分完成':'失败');$('workInfo').title=`来源：${entry.provenance?.run??entry.provenance?.source??'未记录'}`;}
function showSectionWork(next, resolved, options) {
  revision++;
  const saved = captureInteraction();
  const { work, view, entry } = resolved;
  const resultStatus = displayWorkStatus(entry);
  const state = WORK_STATE[resultStatus] ?? resultStatus;
  model = next;
  pendingWork = '';
  pendingView = '';
  currentWork = work;
  currentView = view;
  updateWorkView(resolved);
  const pane = $('sectionPane'), frame = $('sectionFrame'), message = $('sectionMessage');
  pane.hidden = false;
  pane.closest('.workspace').classList.add('section-active');
  const viewerUrl = typeof entry.viewerUrl === 'string' && entry.viewerUrl.startsWith('/section/')
    ? entry.viewerUrl : null;
  frame.hidden = !viewerUrl;
  message.hidden = !!viewerUrl;
  message.textContent = entry.reason || '本阶段没有可预览的截面产物';
  if (!viewerUrl) {
    frame.removeAttribute('src');
    frame.classList.remove('section-loading');
  } else if (frame.getAttribute('src') !== viewerUrl) {
    const documentUrl = new URL(viewerUrl, location.href).href.split('#')[0];
    const sameDocument = frame.contentDocument?.URL.split('#')[0] === documentUrl;
    frame.classList.toggle('section-loading', !sameDocument);
    frame.onload = () => {
      if (frame.getAttribute('src') === viewerUrl &&
          frame.contentDocument?.URL.split('#')[0] === documentUrl) {
        frame.classList.remove('section-loading');
      }
    };
    frame.src = viewerUrl;
  }
  $('summary').textContent = `${models.length} 个模型 · ${WORK_LABEL[work]} · ${state}`;
  $('workInfo').textContent = [next.name, WORK_LABEL[work], view && entry.label, state, entry.reason].filter(Boolean).join(' · ');
  $('workInfo').title = [entry.provenance?.run ?? entry.provenance?.source, entry.reason].filter(Boolean).join(' · ');
  const artifacts = (entry.artifacts || []).map(item =>
    `<a class="work-artifact" href="${esc(item.url)}" download>${esc(item.label)}</a>`).join('');
  $('modelInfo').textContent = `${WORK_LABEL[work]} · ${state}`;
  $('details').innerHTML = `<h2>${esc(WORK_LABEL[work])}</h2><p>${esc([view && entry.label, next.name, state].filter(Boolean).join(' · '))}</p>` +
    (entry.reason ? `<p class="notice">${esc(entry.reason)}</p>` : '') +
    (entry.coordinateNote ? `<p class="work-detail-note">${esc(entry.coordinateNote)}</p>` : '') +
    (viewerUrl ? `<a class="work-artifact" href="${esc(viewerUrl)}" target="_blank" rel="noopener">打开完整截面页面</a>` : '') + artifacts;
  detailsKey = '';
  updateModelList();
  updateWorkTabs();
  restoreInteraction(saved);
  window.__pathAnalysisViewer = { loaded: true, modelId: String(next.id), work, view, workStatus: resultStatus };
  if (options.hash !== false) writeWorkHash(next);
}
async function load(next, selection = {}, options = {}) {
  const resolved = resolveWork(next, options.work ?? (pendingWork || currentWork), options.view);
  if (Object.hasOwn(SECTION_WORKS, resolved.work) || resolved.work === 'regionPartition' ||
      (resolved.work === 'contour' && resolved.view && resolved.view !== 'native')) {
    showSectionWork(next, resolved, options);
    return;
  }
  if (!$('sectionPane').hidden) {
    $('sectionPane').hidden = true;
    $('sectionPane').closest('.workspace').classList.remove('section-active');
    $('sectionFrame').removeAttribute('src');
  }
  await loadModelWork(next, selection, options, resolved);
}
async function loadModelWork(next, selection, options, resolved) {
 const { work: chosenWork, view, entry } = resolved;
 const request=++revision,sameModel=loadedModelId===String(next.id);
 model=next;pendingWork=chosenWork;pendingView=view;updateModelList();updateWorkTabs(chosenWork);
 updateWorkView(resolved);
 status(`正在读取 ${WORK_LABEL[chosenWork]} 的已保存产出…`);
 window.__pathAnalysisViewer={loaded:false,modelId:String(next.id),work:chosenWork,view};
 try{const result=await loadWorkData(chosenWork,entry,json);if(request!==revision)return;if(chosenWork==='pathAnalysis'&&result.stage==='pathAnalysis'&&(result.analysisGranularity!=='logical-edge-parts'||!Array.isArray(result.edges)||!Array.isArray(result.faces)))throw Error('该路径结果的逻辑边格式不完整');const saved=captureInteraction(),keepView=sameModel&&fittedModelId===String(next.id),oldIdentity=data?.inspection?.identityGroup,oldFace=selectedFace,oldEdge=edgeMap.get(selectedEdge),sameIds=sameModel&&oldIdentity===result.inspection?.identityGroup;let face=selection.face??(sameIds?oldFace:''),edge=selection.edge??'';if(!edge&&sameIds&&oldEdge){const source=new Set([...(oldEdge.sourceEdgeIds??[]),...(oldEdge.analysisRecord?.matchedEdgeIds??[])].map(String)),candidate=result.edges.find(e=>e.pathKey===selectedEdge)||result.edges.find(e=>(e.faceIds??[e.faceId]).map(String).includes(String(face))&&[...(e.sourceEdgeIds??[]),...(e.analysisRecord?.matchedEdgeIds??[])].some(id=>source.has(String(id))));edge=candidate?.pathKey??'';}data=result;loadedModelId=String(next.id);currentWork=chosenWork;currentView=view;pendingWork='';pendingView='';selectedFace=face&&data.faces.some(f=>String(f.faceId)===String(face))?String(face):'';selectedEdge=edge&&data.edges.some(e=>e.pathKey===edge)?edge:'';if(!selectedFace)viewFocus=false;workChrome(entry);build({preserveView:keepView});if(data.geometry?.faces?.length)fittedModelId=String(next.id);select(selectedFace,selectedEdge,false,true);$('modelName').textContent=next.name;const state=WORK_STATE[entry.status]??entry.status??'未记录';$('summary').textContent=pathStage()?`${models.length} 个模型 · ${data.counts.wholeFaces} 面 · ${data.counts.pathOccurrences} 路径 · ${data.counts.sideOccurrences} 侧边`:currentWork==='faceCutCoverage'?`${models.length} 个模型 · ${data.counts.wholeFaces} 面 · 切透 ${data.counts.throughSpans??0} / 半切透 ${data.counts.partialSpans??0} / 待定 ${data.counts.pendingSpans??0}`:`${models.length} 个模型 · ${WORK_LABEL[currentWork]} · ${data.counts.wholeFaces} 面 · ${data.edges.length} 边记录`;$('viewSubtitle').textContent=`${WORK_LABEL[currentWork]} · ${state} · ${data.counts.wholeFaces} 个原面`;const artifacts=entry.artifacts??[];$('modelInfo').innerHTML=artifacts.map(a=>`<a href="${esc(a.url)}" download>${esc(a.label)}</a>`).join('')+`<div>${WORK_LABEL[currentWork]} · ${state}<br>${esc(entry.coordinateNote??'')}</div>`;status(!data.geometry?.faces?.length?(entry.reason||'该阶段没有可显示的几何产物'):entry.previewStatus==='failed'?'几何预览失败；原始产物与阶段报告仍可下载。':'',entry.status==='failed'||entry.previewStatus==='failed');updateModelList();restoreInteraction(saved);if(options.hash!==false)writeWorkHash(next,selectedFace,selectedEdge);}
 catch(error){if(request!==revision)return;pendingWork='';pendingView='';currentWork=chosenWork;currentView=view;clear();data=null;faceMap=new Map();edgeMap=new Map();$('faceList').replaceChildren();$('details').textContent=error.message;$('modelInfo').textContent='本阶段读取失败；没有用前序结果替代。';$('viewSubtitle').textContent=WORK_LABEL[currentWork]+' · 读取失败';status(error.message,true);updateWorkTabs();window.__pathAnalysisViewer={loaded:false,modelId:String(next.id),work:currentWork,view:currentView,error:error.message};console.error(error);}}
function hit(event){const rect=canvas.getBoundingClientRect();mouse.set((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1);raycaster.setFromCamera(mouse,camera);const face=raycaster.intersectObjects(faceMeshes,false)[0];const hits=raycaster.intersectObjects(edgeLines.filter(o=>o.visible),false).filter(h=>$('xray').checked||!face||h.distance<=face.distance+radius*.002);const edge=['faceCutCoverage','penetration'].includes(currentWork)?(hits.find(h=>h.object.userData.workCoverage&&(edgeMap.get(h.object.userData.pathKey)?.faceIds??[]).includes(selectedFace))??hits.find(h=>h.object.userData.workCoverage)??hits[0]):hits[0];if(edge)return edge.object.userData.unresolved?{face:edge.object.userData.faceId}:{edge:edge.object.userData.pathKey};return face?{face:face.object.userData.faceId}:null;}
canvas.addEventListener('pointerdown',e=>{if(e.button!==0&&e.button!==1&&e.button!==2)return;e.preventDefault();pointer={id:e.pointerId,button:e.button,x:e.clientX,y:e.clientY,sx:e.clientX,sy:e.clientY,drag:false};canvas.setPointerCapture(e.pointerId);});
canvas.addEventListener('pointermove',e=>{if(!pointer)return;const dx=e.clientX-pointer.x,dy=e.clientY-pointer.y;pointer.x=e.clientX;pointer.y=e.clientY;if(Math.hypot(e.clientX-pointer.sx,e.clientY-pointer.sy)>4)pointer.drag=true;if(!pointer.drag)return;if(pointer.button===0){theta-=dx*.006;phi-=dy*.006;}else{const scale=2*orbit*Math.tan(camera.fov*Math.PI/360)/height;target.addScaledVector(new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld,0),-dx*scale).addScaledVector(new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld,1),dy*scale);}updateCamera();});
canvas.addEventListener('pointerup',e=>{if(!pointer||pointer.id!==e.pointerId)return;const click=!pointer.drag&&pointer.button===0;pointer=null;if(canvas.hasPointerCapture(e.pointerId))canvas.releasePointerCapture(e.pointerId);if(click){const found=hit(e);if(found?.edge)select('',found.edge);else if(found?.face)select(found.face);}});
canvas.addEventListener('pointercancel',()=>{pointer=null;});canvas.addEventListener('contextmenu',e=>e.preventDefault());canvas.addEventListener('auxclick',e=>{if(e.button===1)e.preventDefault();});canvas.addEventListener('wheel',e=>{e.preventDefault();orbit=Math.max(radius*1e-4,Math.min(radius*1e5,orbit*Math.exp(e.deltaY*.001)));updateCamera();},{passive:false});
workTabs.addEventListener('click',e=>{const button=e.target.closest('[data-work]');if(button&&model&&button.dataset.work!==(pendingWork||currentWork))load(model,{}, {work:button.dataset.work});});
workView.addEventListener('change',()=>{if(model)load(model,{}, {work:pendingWork||currentWork,view:workView.value});});
$('modelSearch').addEventListener('input',updateModelList);$('faceSearch').addEventListener('input',updateFaceList);
$('modelList').addEventListener('click',e=>{const row=e.target.closest('[data-model]');if(row){const next=models.find(m=>String(m.id)===row.dataset.model);if(next)load(next,{}, {work:pendingWork||currentWork,view:pendingWork?pendingView:currentView});}});
$('faceList').addEventListener('click',e=>{const row=e.target.closest('[data-face]');if(row)select(row.dataset.face);});$('details').addEventListener('click',e=>{const edge=e.target.closest('[data-edge]'),face=e.target.closest('[data-face]');if(edge)select('',edge.dataset.edge);else if(face)select(face.dataset.face);});
$('clearSelection').addEventListener('click',()=>select());$('fit').addEventListener('click',()=>fit());$('fitSelection').addEventListener('click',()=>fit(true));$('iso').addEventListener('click',()=>{theta=.8;phi=1.08;fit(viewFocus);});for(const b of document.querySelectorAll('[data-axis]'))b.addEventListener('click',()=>{theta=b.dataset.axis==='y'?Math.PI/2:0;phi=b.dataset.axis==='z'?.015:Math.PI/2;fit(viewFocus);});
for(const id of ['showPaths','showSides','showAxes','axisScope','xray'])$(id).addEventListener('change',applySelection);for(const id of ['axisScale','opacity'])$(id).addEventListener('input',applySelection);new ResizeObserver(resize).observe($('viewport'));
async function loadFromHash() {
  const hash = new URLSearchParams(location.hash.slice(1));
  const next = models.find(item => String(item.id) === hash.get('case')) ?? model ?? models[0];
  if (!next || !availableWorks.size) return;
  const requestedWork = hash.get('work') ?? defaultWork();
  const requestedView = hash.get('view') ?? '';
  const {work, view} = resolveWork(next, requestedWork, requestedView);
  const rewrite = requestedWork !== work || requestedView !== view;
  const face = hash.get('face'), edge = hash.get('edge');
  if (pendingWork || String(next.id) !== String(model?.id) || work !== currentWork || view !== currentView) {
    await load(next, {face, edge}, {work, view, hash: rewrite});
  } else if ($('sectionPane').hidden) {
    select(face ?? '', edge ?? '', rewrite);
  } else if (rewrite) {
    writeWorkHash(next);
  }
}
window.addEventListener('hashchange',loadFromHash);
try {
  const reviewPartition=new URLSearchParams(location.search).get('review')==='section-partition';
  const partitionRoot='/section/SectionPartition/';
  const [manifest,catalog,penetration,partition]=await Promise.allSettled([
    json('manifest.json'),json('/data/work-manifest.json'),json('/data/Penetration/manifest.json'),
    reviewPartition ? json(partitionRoot+'manifest.json') : Promise.resolve(null),
  ]);
  if (manifest.status !== 'fulfilled') throw manifest.reason;
  models = manifest.value.models ?? [];
  let workDefinitions=catalog.status==='fulfilled'?[...catalog.value.works]:[...availableWorks].map(id=>({id}));
  if(catalog.status==='fulfilled')
    workModels=new Map((catalog.value.models??[]).map(entry=>[String(entry.id),entry]));
  const hasPenetration=penetration.status==='fulfilled'&&penetration.value.stage==='penetration';
  if(hasPenetration){
    const known=new Set(models.map(model=>String(model.id)));
    for(const row of penetration.value.models??[]){
      const key=String(row.id);
      if(!known.has(key))models.push({id:row.id,name:row.name});
      const previous=workModels.get(key)??{id:row.id,name:row.name,works:{}};
      const entry={...row,identityGroup:'classified-id',coordinateNote:'沿用 Classify 原面编号与坐标'};
      workModels.set(key,{...previous,works:{...previous.works,penetration:entry}});
    }
    if(!workDefinitions.some(work=>work.id==='penetration'))
      workDefinitions.push({id:'penetration',label:WORK_LABEL.penetration});
  }
  if(reviewPartition){
    if(partition.status!=='fulfilled') throw partition.reason;
    const {stage,records,runId,startedAt}=partition.value;
    if(stage!=='regionPartition'||!Array.isArray(records)) throw Error('区域划分清单格式错误');
    // Older manifests store the UTC start time only in the run ID.
    const legacyStart=runId?.match(/^(\d{4}-\d{2}-\d{2}T\d{2})-(\d{2})-(\d{2}\.\d{3}Z)-[0-9a-f]{8}$/i);
    const runDate=new Date(startedAt??(legacyStart ? `${legacyStart[1]}:${legacyStart[2]}:${legacyStart[3]}` : undefined));
    const runTime=Number.isFinite(runDate.getTime())
      ? new Intl.DateTimeFormat('zh-CN',{dateStyle:'short',timeStyle:'medium'}).format(runDate) : '';
    const byId=new Map(models.map(model=>[String(model.id),model]));
    for(const row of records){
      const key=String(Number(row.id)), model=byId.get(key);
      if(!model||model.name!==row.name||(row.sourceSha256&&model.sourceSha256&&row.sourceSha256!==model.sourceSha256))
        throw Error('区域划分样件与当前检查目录不一致');
      const previous=workModels.get(key)??{id:model.id,name:model.name,works:{}};
      const artifacts=[
        ['完整材料 BREP',row.brepUrl],['分区原生 JSON',row.dataUrl],['原生运行报告',row.evidenceUrl],
      ].filter(([,url])=>url).map(([label,url])=>({label,url:partitionRoot+url}));
      const reason=row.status==='complete'&&row.boundaryProvenanceComplete===false
        ? '材料分区已完成，部分边界归属仍缺少几何证据' : row.reason;
      const coordinateNote='Classify 之后的对齐 YZ 截面'+(runTime?' · 本轮运行 '+runTime:'');
      const regionPartition={status:row.status,reason,artifacts,coordinateNote,
        provenance:{run:runId,source:row.name},viewerUrl:row.dataUrl||row.contourUrl
          ? partitionRoot+'index.html?embedded#'+encodeURIComponent(row.id) : undefined};
      workModels.set(key,{...previous,works:{...previous.works,regionPartition}});
    }
    workDefinitions=workDefinitions.filter(work=>work.id!=='regionPartition');
    const classify=workDefinitions.findIndex(work=>work.id==='classify');
    workDefinitions.splice(classify===-1?workDefinitions.length:classify+1,0,{id:'regionPartition',label:'区域划分'});
  }
  if(catalog.status==='fulfilled'||hasPenetration||reviewPartition)
    configureWorkTabs(workDefinitions);
  workTabs.hidden = !availableWorks.size;
  updateModelList();
  if (models.length && availableWorks.size) await loadFromHash();
  else status('尚无 Work 产出。');
} catch (error) {
  status(error.message,true);
}
resize();
