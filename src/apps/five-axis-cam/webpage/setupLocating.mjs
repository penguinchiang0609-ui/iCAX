import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {activeTool} from './workspaceModel.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {currentSetup} from './setupManagement.mjs';
import {syncSceneSnapping,sceneSnapCandidate,presentSceneSnaps,clearSceneSnapPresentation} from './sceneSnapping.mjs';
import {button,section,hint,facts} from './workspaceControls.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {registrationError} from './registrationDraft.mjs';
import {placementWorldMatrix} from './setupWorldFrame.mjs';

const modes=['endpoint','midpoint','center','sphere-center','datum-point','point'];
const key=view=>JSON.stringify([view.scene.domain.expected.epoch,selectedPlacement(view)?.id,view.scene.model?.geometryRef]);
function draft(view){
  if(view.fiveLocating?.key===key(view))return view.fiveLocating;
  disposeLocating(view);
  const plan=currentSetup(view)?.registrationPlan;
  return view.fiveLocating={key:key(view),points:(plan?.points||[]).map(p=>({point:p.modelPoint,query:p.query})),
    measured:Array.from({length:3},(_,i)=>Array.from({length:3},(_,j)=>String(plan?.machineRegistration?.measuredPoints?.[i]?.[j]??''))),
    tolerance:String(plan?.machineRegistration?.residualLimitMm??0.1),picking:null,hover:null,dirty:false};
}
export function locatingField(view,name,value){
  const d=draft(view);if(view.pending||d.request)return;
  if(name==='tolerance')d.tolerance=value;
  else {const [i,j]=name.split(':').map(Number);if(!Number.isInteger(i)||i<0||i>2||!Number.isInteger(j)||j<0||j>2)return;d.measured[i][j]=value;}
  d.dirty=true;
}
const current=(context,view,d)=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveLocating===d&&view.fiveLocatingOpen!==false&&activeTool(view)==='registration'&&d.key===key(view);
export function renderLocating(view){
  if(view.fiveLocatingOpen===false)return '';
  const d=draft(view),plan=currentSetup(view)?.registrationPlan,status=plan?.status;
  return `<div class="five-position-dialog" data-five-key="locating-dialog" data-five-tool="registration" role="dialog" aria-label="三点定位" aria-modal="false" tabindex="-1">
    <header class="five-position-title"><strong>三点定位</strong>${button('five.locating.close','×',`aria-label="关闭三点定位" ${view.pending||d.request?'disabled':''}`)}</header><div class="five-position-body five-locating-body">`
    +section('locating-state','当前装夹的定位资料',facts([['所属装夹',currentSetup(view)?.name||''],['状态',d.dirty?'有未保存的取点':status==='Prepared'?'定位资料已准备':status==='Stale'?'资料已过期，请重新保存三点':'待选三个特征点'],['坐标系','当前装夹坐标系 · mm']])
    +hint(d.picking===null?'选择三个分散、不共线的特征点。':`正在选择 P${d.picking+1}：点击模型上的中心标记或特征；Esc 暂停选点。`))
    +[0,1,2].map(i=>{const point=d.points[i]?.point,m=selectedPlacement(view)?.transform,p=point&&m?new THREE.Vector3(...point).applyMatrix4(new THREE.Matrix4().set(...m)):null;
      return `<div class="five-locating-point" data-five-key="locating-${i}"><strong>P${i+1}</strong><div><span>${p?p.toArray().map(v=>Number(v.toFixed(4))).join('，'):'尚未选择'}</span>${point?`<small>${esc(d.points[i].label||(d.points[i].query?.featureId?.startsWith('cylinder-center:')?'圆柱面轴心':'模型特征点'))}</small>`:''}</div>${button(`five.locating.pick:${i}`,d.picking===i?'拾取中':point?'重选':'选点',view.pending||d.request?'disabled':'')}
        <div class="five-locating-measured"><small>对应机床坐标（mm）</small>${'XYZ'.split('').map((axis,j)=>`<label>${axis}<input type="text" inputmode="decimal" data-five-key="locating-machine-${i}-${j}" data-five-locating-field="${i}:${j}" aria-label="P${i+1} 机床 ${axis}" value="${attr(d.measured[i][j])}" ${view.pending||d.request?'readonly':''}></label>`).join('')}</div></div>`;}).join('')
    +section('locating-machine','机床定位',`<div class="five-field"><label for="locating-tolerance">允许最大配对误差（mm）</label><input id="locating-tolerance" data-five-key="locating-tolerance" data-five-locating-field="tolerance" type="text" inputmode="decimal" value="${attr(d.tolerance)}" ${view.pending||d.request?'readonly':''}></div>`
      +hint(d.dirty?'输入有修改，保存后才用于输出。':status==='Stale'?'定位资料已过期，请重新核对两组三点并保存。':plan?.machineStatus==='Qualified'?`已建立机床定位 · 最大误差 ${Number(plan.machineRegistration.maximumResidualMm.toFixed(5))} mm`:'未填写机床坐标时可先保存模型三点；统一输出前须完成机床定位。'))
    +hint('圆柱面轴心：孔壁面积中心在孔轴上的投影。现场须采用相同基准。')
    +hint('定位点仅属于当前装夹；其他装夹独立选点。按 P1、P2、P3 顺序交付，摆放或几何变化后需重新保存。')
    +`</div><footer class="five-position-actions">${button('five.locating.save',d.measured.flat().some(v=>v.trim())?'保存并计算定位':'保存三点',view.pending||d.points.filter(Boolean).length!==3?'disabled':'class="five-primary"')}${button('five.locating.export','导出定位资料',view.pending||d.dirty||status!=='Prepared'?'disabled':'')}</footer></div>`;
}
function feedback(view,d){
  const object=view.viewport?.sceneObjects.get(selectedPlacement(view)?.id);if(!object)return;
  d.layer?.removeFromParent();d.layer?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});
  const group=new THREE.Group();group.name='locating-point-feedback';
  for(const [i,p] of [d.points[0],d.points[1],d.points[2],d.hover].entries())if(p?.point){
    // Screen-sized hollow rings leave the actual CAD point and nearby edges
    // visible even at close zoom. Feedback never participates in picking.
    const geometry=new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...p.point)]),material=new THREE.PointsMaterial({color:i===3?0x60dfcc:0xffce64,size:i===3?12:9,sizeAttenuation:false,depthTest:false,depthWrite:false});
    material.onBeforeCompile=shader=>{shader.fragmentShader=shader.fragmentShader.replace('#include <clipping_planes_fragment>','#include <clipping_planes_fragment>\nfloat radius=length(gl_PointCoord-vec2(.5));if(radius>.5||radius<.31)discard;');};
    const marker=new THREE.Points(geometry,material);marker.userData.locatingPoint=i===3?'hover':`P${i+1}`;marker.raycast=()=>{};marker.renderOrder=100;group.add(marker);
  }
  object.add(group);d.layer=group;
  const matrix=placementWorldMatrix(view,selectedPlacement(view));
  view.viewport.setSpecificationAnnotations(d.points.flatMap((p,i)=>{if(!p)return [];const point=new THREE.Vector3(...p.point).applyMatrix4(matrix).toArray();return [{id:`locating-P${i+1}`,kind:'label',start:point,end:point,label:`P${i+1}`,color:0xffce64,pickThrough:true}];}));
  view.sceneSnapping?.presentation?.layer?.highlight(d.hover);
}
export function disposeLocating(view){const d=view.fiveLocating;if(d){d.hover=null;d.hoverToken=(d.hoverToken||0)+1;}if(d?.layer){d.layer.removeFromParent();d.layer.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});d.layer=null;}if(d?.displayed){view.viewport?.setSpecificationAnnotations([]);clearSceneSnapPresentation(view);d.displayed=false;}}
export function syncLocating(context,view){
  if(activeTool(view)!=='registration'||view.fiveLocatingOpen===false){disposeLocating(view);return;}
  const d=draft(view);d.displayed=true;
  if(view.pending||d.request){d.hover=null;d.hoverToken=(d.hoverToken||0)+1;feedback(view,d);view.viewport?.setHoverEnabled(false);return;}
  syncSceneSnapping(context,view);view.viewport?.setHoverEnabled(d.picking!==null);
  if(d.picking!==null)presentSceneSnaps(context,view,{objectId:selectedPlacement(view)?.id,allowedModes:modes});else clearSceneSnapPresentation(view);
  feedback(view,d);
}
export function handleLocatingHover(context,view,data,hit,event,_hits,pointer){
  if(activeTool(view)!=='registration'||view.fiveLocatingOpen===false)return false;const d=draft(view);if(d.picking===null||view.pending)return true;
  const c=pointer?.pointerActive!==false&&event?.type==='pointermove'&&!event.buttons?sceneSnapCandidate(context,view,{objectId:selectedPlacement(view)?.id,allowedModes:modes},hit,event):null;
  const token=d.hoverToken=(d.hoverToken||0)+1;d.hover=null;
  if(c){feedback(view,d);void c.resolve().then(r=>{if(current(context,view,d)&&d.picking!==null&&!view.pending&&d.hoverToken===token){d.hover=r;feedback(view,d);}}).catch(()=>{});}
  else feedback(view,d);return true;
}
export function handleLocatingPick(context,view,data,hit,event,_hits,ops){
  if(activeTool(view)!=='registration'||view.fiveLocatingOpen===false)return false;const d=draft(view);if(d.picking===null||view.pending||d.request)return true;
  const c=sceneSnapCandidate(context,view,{objectId:selectedPlacement(view)?.id,allowedModes:modes},hit,event);if(!c)return true;
  const index=d.picking;view.pending=true;ops.renderProject(context,view);
  void c.resolve().then(r=>{if(current(context,view,d)){d.points[index]=r;d.dirty=true;d.hover=null;d.picking=[0,1,2].find(i=>!d.points[i])??null;view.error='';}})
    .catch(error=>{if(current(context,view,d))view.error=error.message;}).finally(()=>{view.pending=false;if(!view.disposed)ops.renderProject(context,view);});return true;
}
export async function locatingCommand(context,view,action,ops){
  if(view.pending)return;const d=draft(view);
  if(action==='close'||action==='cancel-pick'){
    if(d.request)return;
    d.picking=null;d.hover=null;d.hoverToken=(d.hoverToken||0)+1;
    if(action==='close'){view.fiveLocatingOpen=false;disposeLocating(view);}
    view.error='';ops.renderProject(context,view);return;
  }
  if(d.request&&action!=='save')return;
  if(action.startsWith('pick:')){d.picking=Number(action.slice(5));view.error='';ops.renderProject(context,view);return;}
  let destination;
  if(action==='export'){
    const bridge=context.appProxy?.bridge??context.sceneProxy?.bridge;
    if(!bridge?.saveFileDialog){view.error='当前宿主不支持选择导出文件。';ops.renderProject(context,view);return;}
    destination=await bridge.saveFileDialog({title:'导出三点定位资料',defaultPath:'三点定位.json',defaultExtension:'json',filters:[{name:'定位资料',extensions:['json']}]});
    if(!destination||!current(context,view,d))return;
  }
  view.pending=true;view.error='';ops.renderProject(context,view);
  try{
    if(action==='save'){
      if(!d.request){
        const measurement={};
        if(d.measured.flat().some(v=>v.trim())){
          if(d.measured.flat().some(v=>!v.trim()||!Number.isFinite(Number(v))))throw Error('请完整填写 P1、P2、P3 对应的机床 X、Y、Z 坐标。');
          if(!d.tolerance.trim()||!Number.isFinite(Number(d.tolerance))||Number(d.tolerance)<=0)throw Error('允许最大配对误差须为大于 0 的数。');
          measurement.measuredPoints=d.measured.map(p=>p.map(Number));measurement.maximumResidualMm=Number(d.tolerance);
        }
        d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),placementId:selectedPlacement(view).id,queries:d.points.map(p=>p.query),...measurement};
      }
      let plan;try{plan=await context.sceneProxy.invoke('CamSetup.SetRegistrationPlan',d.request);}catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:d.request.expected.epoch,commandId:d.request.commandId});if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}plan=receipt.result;}
      d.points=plan.points.map(p=>({point:p.modelPoint,query:p.query}));d.request=null;d.dirty=false;await saveCommittedProjectEdit(context,view,ops);await ops.refreshSceneState(context,view);ops.appendProjectLog(context,'ok',plan.machineStatus==='Qualified'?'机床定位已保存，当前装夹输出将转换到机床坐标系。':'模型三点已保存；填写对应机床坐标后可计算定位。');
    }else if(action==='export'){
      const result=await context.sceneProxy.invoke('CamSetup.ExportRegistrationPlan',{expected:view.scene.domain.expected,setupId:currentSetup(view).id,destination});
      ops.appendProjectLog(context,'ok',`已导出三点定位资料：${result.path}（装夹坐标系，mm）。`);
    }
  }catch(error){view.error=registrationError(error);}
  finally{view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
