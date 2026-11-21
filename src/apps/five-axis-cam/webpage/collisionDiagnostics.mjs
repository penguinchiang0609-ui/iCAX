import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeSetupWorldFrame,applySetupDisplayFrame} from './setupWorldFrame.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {button} from './workspaceControls.mjs';
import {playbackPose} from './trajectoryPlayback.mjs';
import {selectedCollisionCheck,collisionLocationLabel,collisionPathLabel} from './collisionLocations.mjs';

const vector=value=>Array.isArray(value)&&value.length===3&&value.every(n=>typeof n==='number'&&Number.isFinite(n)&&Math.abs(n)<=1e9);
const integer=value=>String(value??'').trim()!==''&&Number.isSafeInteger(Number(value))&&Number(value)>=0?Number(value):null;
const number=value=>typeof value==='number'&&Number.isFinite(value)&&value>=0?value:null;
const mm=value=>Number(value.toFixed(3)).toString();
const role=frame=>frame?.role==='Rapid'?'空移':frame?.role==='LeadIn'?'引入':frame?.role==='LeadOut'?'引出':'切割';
const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);

export function installCollisionDiagnosticsStyle(document){
 if(!document?.head||document.getElementById('five-collision-diagnostics-style'))return;
 const link=document.createElement('link');link.id='five-collision-diagnostics-style';link.rel='stylesheet';link.href=new URL('./collisionDiagnostics.css',import.meta.url).href;document.head.append(link);
}
export function collisionObstacle(view,id){
 const domain=view.scene?.domain,fixture=domain?.fixtures?.find(f=>f.id===id);
 if(fixture){const unit=domain.fixtureUnits?.find(u=>u.partIds?.includes(id));return {id,kind:'夹具',name:unit&&unit.name!==fixture.name?`${unit.name} · ${fixture.name}`:fixture.name||unit?.name||'夹具'};}
 const placement=domain?.placements?.find(p=>p.id===id),workpiece=domain?.workpieces?.find(w=>w.id===placement?.workpieceId);
 return placement?{id,kind:'工件',name:workpiece?.name||view.scene?.model?.name||'工件'}:{id,kind:'障碍物',name:'未命名检测对象'};
}
export function collisionHeadBoxes(report,headPose=null){
 if(report?.representation!=='BoxCompoundMesh'||!Array.isArray(report.headBoxes)||report.headBoxes.length!==integer(report.headBoxCount))return [];
 const boxes=report.headCoverage?.scope==='CompleteSelectedHeadAssembly'?headPose?.boxes:report.headBoxes;
 if(!Array.isArray(boxes)||boxes.length!==integer(report.headBoxCount))return [];
 if(boxes.some((box,i)=>integer(box.index)!==i||!vector(box.center)||!vector(box.halfExtents)||box.halfExtents.some(n=>n<=0)||!Array.isArray(box.axes)||box.axes.length!==3||box.axes.some(a=>!vector(a))))return [];
 for(const box of boxes){const axes=box.axes.map(a=>new THREE.Vector3(...a));if(axes.some(a=>Math.abs(a.length()-1)>1e-5)||Math.abs(axes[0].dot(axes[1]))>1e-5||Math.abs(axes[0].dot(axes[2]))>1e-5||Math.abs(axes[1].dot(axes[2]))>1e-5||new THREE.Vector3().crossVectors(axes[0],axes[1]).dot(axes[2])<.99999)return [];}
 return boxes;
}
export function collisionDiagnosis(view,s){
 const report=s.collisionReport;if(s.checking||!report||report.validity!=='Current'||report.representation!=='BoxCompoundMesh')return null;
 const chosen=selectedCollisionCheck(s),index=chosen?Number(chosen.index):Math.floor(s.cursor),segment=chosen&&['Collision','Unchecked'].includes(chosen.status)?chosen:report.segments?.find(x=>Number(x.index)===index&&x.status==='Collision');if(!segment)return null;
 const fraction=Number(segment.fraction);if(!Number.isFinite(fraction)||fraction<0||fraction>1||!s.frames[index])return null;
 const boxes=collisionHeadBoxes(report,segment.headPose),raw=segment.witness,frame=s.frames[index],obstacle=collisionObstacle(view,segment.obstacleId),boxIndex=raw?.frame==='Setup'?integer(raw.headBoxIndex):null,box=boxIndex!==null?boxes[boxIndex]:null;
 const hasPoints=raw?.frame==='Setup'&&vector(raw.headPoint)&&vector(raw.obstaclePoint);
 const witness=hasPoints?{...raw,headPoint:[...raw.headPoint],obstaclePoint:[...raw.obstaclePoint],contained:raw.kind==='Contained'||raw.containment===true}:null;
 return {index,segment,fraction,pose:playbackPose(s.frames,index+fraction),headPose:segment.headPose,frame,role:role(frame),obstacle,boxes,boxIndex:box?boxIndex:null,witness};
}
export function renderCollisionDiagnosis(view,s){
 const d=collisionDiagnosis(view,s);if(!d)return '';
 const location=d.segment.location||(d.segment.first?{first:d.segment.first,last:d.segment.last}:null),points=location?.first?[location.first,location.last]:location?[location]:[];
 const w=d.witness,separation=number(w?.separationMm),clearance=number(w?.clearanceMm),definition=s.collisionReport.headCoverage?.boxes?.[d.boxIndex],box=definition?.partName?`${definition.partName} · ${definition.boxName||`检测盒 ${d.boxIndex+1}`}`:d.boxIndex!==null?`检测盒 ${d.boxIndex+1}`:'切割头碰撞盒';
 const limit=d.segment.reason==='CAM_HEAD_LIMIT_NO_SOLUTION';
 const detail=limit?'当前关键点未找到切割头关节限位内的可用姿态。可调整该关键点后复检。':d.segment.status==='Unchecked'?'此处尚未完成验证，不能判定为可加工。':!w?'当前报告未提供几何见证点，只定位已检测到问题的运动段。':w.contained?'检测盒进入障碍物内部。见证连线表示两侧几何位置，不代表穿透深度。':w.kind==='Overlap'||separation===0?'检测盒与障碍物网格相交。红点在检测盒上，黄点在工件或夹具上。':separation!==null?`两侧几何间隙 ${mm(separation)} mm${clearance!==null?`，要求至少 ${mm(clearance)} mm`:''}。`:'红点与黄点标出此次检测的两侧几何见证位置。';
 const contact=d.segment.status==='Collision';
 return `<section class="five-collision-diagnosis" data-five-key="collision-diagnosis" data-collision-segment="${d.index}"><header><strong>${limit?'关键点需调整 · 限位内无解':d.segment.status==='Unchecked'?'尚未完成验证':'关键点需调整 · 碰撞盒间隙不足'}</strong><span>${esc(collisionPathLabel(view,location||d.frame))} · ${esc(collisionLocationLabel(location)||d.role)}</span></header>${contact?`<p class="five-collision-pair">${esc(box)} ↔ ${esc(d.obstacle.kind)}「${esc(d.obstacle.name)}」</p>`:''}<p>${esc(detail)}</p>${contact?`<div class="five-collision-legend"><span><i class="hit-box"></i>发生干涉的检测盒</span><span><i class="other-box"></i>其余检测盒</span>${w?'<span><i class="witness"></i>网格见证点</span>':''}</div><small>半透明切割头外形仅供辨认；红色盒与工件、夹具网格参与本次检测。</small>`:''}<p class="five-collision-adjust-hint">调整相关关键点后，点击“重新检测本条”复检。</p><div class="five-collision-diagnostic-actions">${points.filter(p=>p.pointId||p.anchor||p.rapidPointIndex!==undefined).map(p=>button(p.rapidPointIndex!==undefined?'five.simulation.collision-edit-rapid:'+p.frameIndex:p.pointId?'five.simulation.collision-edit-point:'+encodeURIComponent(p.pointId):'five.simulation.collision-edit-anchor:'+p.frameIndex,'调整'+p.label)).join('')}${button('five.simulation.collision-focus','定位问题')}${contact?button('five.simulation.collision-detail-toggle',s.collisionDetailXray?'恢复实体显示':'看清内部位置',`aria-pressed="${!!s.collisionDetailXray}"`):''}</div></section>`;
}

function disposeTree(group){group?.removeFromParent();group?.traverse(o=>{if(!o.userData.sharedGeometry)o.geometry?.dispose();for(const material of Array.isArray(o.material)?o.material:[o.material])material?.dispose();});}
function restoreMaterials(s){for(const saved of s.collisionDiagnosticMaterials||[]){if(saved.object.material===saved.temporary)saved.object.material=saved.original;for(const m of Array.isArray(saved.temporary)?saved.temporary:[saved.temporary])m.dispose();}s.collisionDiagnosticMaterials=[];}
function fadeObject(s,object,opacity){
 object?.traverse(mesh=>{if(!mesh.isMesh||!mesh.material)return;const original=mesh.material,copy=material=>{const m=material.clone();m.transparent=true;m.opacity=Math.min(opacity,material.opacity);m.depthWrite=false;return m;},temporary=Array.isArray(original)?original.map(copy):copy(original);mesh.material=temporary;(s.collisionDiagnosticMaterials??=[]).push({object:mesh,original,temporary});});
}
export function disposeCollisionDiagnostics(s){disposeTree(s.collisionDiagnosticOverlay);s.collisionDiagnosticOverlay=null;s.collisionDiagnosticKey=null;restoreMaterials(s);}
function paintRepairedHead(view,s){
 const boxes=collisionHeadBoxes(s.collisionReport),pose=playbackPose(s.frames,s.cursor);if(!pose||!boxes.length)return false;
 const key=JSON.stringify(['AppliedLocalRepair',s.collisionKey,s.cursor,boxes]);if(s.collisionDiagnosticKey===key){applySetupDisplayFrame(view,s.collisionDiagnosticOverlay);return true;}disposeCollisionDiagnostics(s);s.collisionDiagnosticKey=key;
 const group=new THREE.Group();group.name='five-collision-diagnostics';group.userData={kind:'AppliedLocalRepair',poseSource:'GeneratedPathPose',cursor:s.cursor};group.position.copy(pose.position);group.quaternion.copy(pose.rotation);
 for(const box of boxes){const geometry=new THREE.BoxGeometry(...box.halfExtents.map(n=>n*2)),wire=new THREE.LineSegments(new THREE.EdgesGeometry(geometry),new THREE.LineBasicMaterial({color:0x71b9ae,transparent:true,opacity:.4,depthTest:false,depthWrite:false}));geometry.dispose();wire.name=`collision-head-box-${box.index}`;wire.userData.headBoxIndex=Number(box.index);wire.position.fromArray(box.center);wire.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(...box.axes.map(a=>new THREE.Vector3(...a))));wire.renderOrder=150;wire.raycast=()=>{};group.add(wire);}
 applySetupDisplayFrame(view,group);view.viewport.scene.add(group);s.collisionDiagnosticOverlay=group;return true;
}
function marker(view,position,color,name){
 const mesh=new THREE.Mesh(new THREE.SphereGeometry(1,14,10),new THREE.MeshBasicMaterial({color,depthTest:false,depthWrite:false}));mesh.name=name;mesh.position.fromArray(position);mesh.renderOrder=154;mesh.raycast=()=>{};
 mesh.onBeforeRender=(_renderer,_scene,camera)=>{const height=Math.max(1,view.viewport.renderer.domElement.clientHeight),world=camera.isOrthographicCamera?(camera.top-camera.bottom)/camera.zoom:2*mesh.getWorldPosition(new THREE.Vector3()).distanceTo(camera.position)*Math.tan(THREE.MathUtils.degToRad(camera.fov/2));mesh.scale.setScalar(world/height*5);mesh.updateMatrixWorld();};return mesh;
}
export function paintCollisionDiagnostics(view,s){
 const d=collisionDiagnosis(view,s);if(!view.viewport||!d){if(view.viewport&&!s.checking&&!s.repairing&&s.repairResult?.changed&&s.collisionReport?.validity==='Current'&&paintRepairedHead(view,s))return;if(s.collisionDiagnosticOverlay)disposeCollisionDiagnostics(s);return;}
 const key=JSON.stringify([s.collisionKey,d.index,d.fraction,d.boxes,d.witness,s.collisionDetailXray,s.group?.uuid,view.viewport.sceneObjects?.get(d.obstacle.id)?.uuid]);
 if(s.collisionDiagnosticKey===key){applySetupDisplayFrame(view,s.collisionDiagnosticOverlay);return;}disposeCollisionDiagnostics(s);s.collisionDiagnosticKey=key;
 const group=new THREE.Group();group.name='five-collision-diagnostics';group.userData={segmentIndex:d.index,representation:'BoxCompoundMesh',poseSource:'NativeCollisionWitness',headBoxIndex:d.boxIndex};
 const head=new THREE.Group();head.name='collision-detection-boxes';head.position.copy(d.pose.position);head.quaternion.copy(d.pose.rotation);group.add(head);
 for(const box of d.boxes){const selected=Number(box.index)===d.boxIndex,geometry=new THREE.BoxGeometry(...box.halfExtents.map(n=>n*2)),orientation=new THREE.Matrix4().makeBasis(...box.axes.map(a=>new THREE.Vector3(...a))),container=new THREE.Group();container.position.fromArray(box.center);container.quaternion.setFromRotationMatrix(orientation);container.userData={headBoxIndex:Number(box.index),source:box.source};container.name=`collision-head-box-${box.index}`;head.add(container);
  const line=new THREE.LineSegments(new THREE.EdgesGeometry(geometry),new THREE.LineBasicMaterial({color:selected?0xff4f4f:0x9db7c0,depthTest:false,transparent:true,opacity:selected?1:.23,depthWrite:false}));line.renderOrder=150;container.add(line);
  if(selected){const mesh=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({color:0xff4545,depthTest:false,transparent:true,opacity:.22,depthWrite:false,side:THREE.DoubleSide}));mesh.renderOrder=149;container.add(mesh);}else geometry.dispose();
 }
 if(d.witness){const w=d.witness;group.add(marker(view,w.headPoint,0xff4f4f,'collision-head-witness'),marker(view,w.obstaclePoint,0xffd05a,'collision-obstacle-witness'));const line=new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...w.headPoint),new THREE.Vector3(...w.obstaclePoint)]),new THREE.LineDashedMaterial({color:0xffd05a,depthTest:false,depthWrite:false,dashSize:1,gapSize:.5}));line.name='collision-witness-link';line.renderOrder=153;line.computeLineDistances();group.add(line);}
 group.traverse(o=>{o.raycast=()=>{};});applySetupDisplayFrame(view,group);view.viewport.scene.add(group);s.collisionDiagnosticOverlay=group;
 // The visible head is a reference shape. Its material is restored on leaving
 // this diagnosis, and source geometry/materials are never disposed here.
 fadeObject(s,s.group,.45);if(s.collisionDetailXray)fadeObject(s,view.viewport.sceneObjects?.get(d.obstacle.id),.22);
}
export function focusCollisionDiagnosis(view,s){
 const d=collisionDiagnosis(view,s),vp=view.viewport;if(!vp?.setCameraState||!d)return false;
 paintCollisionDiagnostics(view,s);const bounds=new THREE.Box3().setFromObject(s.collisionDiagnosticOverlay);if(bounds.isEmpty())bounds.expandByPoint(d.pose.position);
 const sphere=bounds.getBoundingSphere(new THREE.Sphere()),camera=vp.getCameraState(),rect=vp.renderer.domElement.getBoundingClientRect(),aspect=Math.max(.2,rect.width/Math.max(1,rect.height)),fov=Number(vp.perspectiveFov||vp.camera?.fov||45)*Math.PI/180;
 const radius=Math.max(35,Math.max(6,sphere.radius)/Math.sin(fov/2)/Math.min(1,aspect)*1.18),center=d?.witness?new THREE.Vector3(...d.witness.headPoint).lerp(new THREE.Vector3(...d.witness.obstaclePoint),.5).applyMatrix4(activeSetupWorldFrame(view)).lerp(sphere.center,.45):sphere.center;
 vp.setCameraState({...camera,target:{x:center.x,y:center.y,z:center.z},radius});return true;
}

const stages={CheckingLimits:'先验证切割头限位',CheckingCollision:'在限位通过后验证碰撞',Preparing:'读取当前碰撞及衔接',CheckingKeypoints:'先检查关键点姿态',Searching:'搜索局部修正',SearchingAngles:'优先在角度容差内调整 IJK',SearchingPosition:'沿原 IJK 方向尝试位置与角度组合',RebuildingRapids:'重新规划必要的相邻空移',Verifying:'验证本处修正及连接边界',Applying:'保存通过局部验证的修复'};
export function collisionRepairActivity(s){
 const p=s.repairTask?.repairProgress||{},stage=s.repairCancelRequested?'正在停止自动修复':stages[p.stage]||'正在准备自动修复',completed=integer(p.completedIssueCount),total=integer(p.totalIssueCount),candidates=integer(p.candidateCount);
 const checked=integer(p.completedSegmentCount),segments=integer(p.totalSegmentCount);
 return {stage,detail:s.repairCancelRequested?'正在结束当前尝试；未通过局部检测的候选不会写入工程。':p.stage==='CheckingKeypoints'?'先区分关键点本身碰撞与点间插值碰撞，再固定相邻关键点求解。':p.stage==='Verifying'?'验证相关关键点和相邻区间的碰撞与限位；其余刀路保持不变。':p.stage==='Applying'?'本处及衔接验证已通过，正在应用并保存修复。':'当前三维保留已检测的碰撞位置；搜索中的候选尚未应用。',count:checked!==null&&segments!==null&&segments>0&&checked<=segments?(p.stage==='CheckingKeypoints'?`已检查 ${checked} / ${segments} 个关键点`:`本处已验证 ${checked} / ${segments} 段`):total!==null&&total>0&&completed!==null&&completed<=total?`本次已处理 ${completed} / ${total} 处局部问题`:'正在读取当前碰撞范围',candidates:candidates!==null?`实际已尝试 ${candidates} 个候选姿态`:''};
}
export function collisionRepairFailure(result){
 const reason=result?.unresolved?.[0]?.reason;
 return ({CAM_REPAIR_NEIGHBOR_KEYPOINT_NOT_CLEAR:'相邻关键点也有碰撞或尚未通过检测，请先调整相邻问题后再复检。',CAM_REPAIR_LINKED_KEYPOINT_CONSTRAINT:'此关键点带有关联姿态，修改会影响当前问题范围之外；本次未修改。',CAM_REPAIR_SEARCH_UNVERIFIED:'本次搜索或验证未完成，原关键点保持不变。',CAM_REPAIR_NO_SOLUTION_IN_TOLERANCE:'不可达：本次在给定容差内未找到可加工方案。'})[reason]||'修正未完成，原关键点保持不变。';
}
export function updateCollisionRepair(context,s){const a=collisionRepairActivity(s);for(const [selector,text]of[['[data-five-repair-stage]',a.stage],['[data-five-repair-detail]',a.detail],['[data-five-repair-count]',a.count],['[data-five-repair-candidates]',a.candidates]]){const node=context.mount?.querySelector(selector);if(node&&node.textContent!==text)node.textContent=text;}}
export function renderCollisionRepair(view,s){
 if(s.repairing){const a=collisionRepairActivity(s);return `<section class="five-collision-repair" data-five-key="collision-repair" aria-live="polite"><strong data-five-repair-stage>${esc(a.stage)}</strong><p data-five-repair-detail>${esc(a.detail)}</p><div class="five-repair-progress"><span data-five-repair-count>${esc(a.count)}</span><span data-five-repair-candidates>${esc(a.candidates)}</span></div></section>`;}
 const selected=collisionDiagnosis(view,s),result=s.repairResult,canRepair=selected&&(selected.segment.status==='Collision'||selected.segment.reason==='CAM_HEAD_LIMIT_NO_SOLUTION');
 const angle=s.repairDeviationDraft??String(s.repairDeviationDeg??15),distance=s.repairTranslationDraft??String(s.repairTranslationMm??5);
 const scope=s.repairScope?.pathKind,title=scope?`自动修复全部${scope==='Rapid'?'空移':'切割'}刀路`:'自动修复本条刀路';
 const outcome=result?.changed?'关键点修正已保存，请复检本条':result?collisionRepairFailure(result):'';
 return `<section class="five-collision-repair" data-five-key="collision-repair"><h4>${title}</h4><p>先修复超限，再修复碰撞。优先调整 IJK；必要时沿原 IJK 方向移动 XYZ，始终保持在本次输入的容差内。</p><label>角度容差（°）<input type="text" inputmode="decimal" value="${attr(angle)}" data-five-simulation-field="repair-deviation" aria-label="自动修复最大调姿角" ${view.pending?'readonly':''}></label><label>位置容差（mm）<input type="text" inputmode="decimal" value="${attr(distance)}" data-five-simulation-field="repair-position" aria-label="自动修正位置容差" ${view.pending?'readonly':''}></label>${button(scope?'five.simulation.collision-repair-scope':'five.simulation.collision-repair-path',scope?'开始自动修复':'自动修复本条',view.pending||s.checking||(!scope&&!s.collisionPathHasFrame)?'disabled':'class="five-primary"')}${outcome?`<p role="status">${esc(outcome)}</p>`:''}</section>`;
}
