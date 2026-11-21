import { toolAxisErrorText } from './toolAxisErrors.mjs';
import {supportsToolAxis} from './toolAxisEligibility.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {placementWorldMatrix} from './setupWorldFrame.mjs';
import { activeTool, workspaceState } from './workspaceModel.mjs';
import { reloadReversedPointDraft } from './toolAxisPoints.mjs';
import { mergeDomainState, refreshMachiningState, readMachiningState } from './domainActions.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { sceneColors } from './scenePalette.mjs';
import { renderToolAxisVector, changeToolAxisAngle, changeToolAxisNotation, validateToolAxisAngles, directionToEuler, toolAxisNotation } from './toolAxisAngles.mjs';

const alive=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
function selected(view){const u=workspaceState(view),ids=u.selectedIntentIds||[u.selectedIntentId];return ids.length===1?view.scene?.intentToolpaths?.find(c=>c.id===ids[0]):null;}
const placement=(view,cut)=>view.scene?.domain?.placements.find(p=>p.id===cut.placementId);
const key=(view,cut)=>JSON.stringify([view.scene?.domain?.expected.epoch,cut.id,cut.poseRef,cut.featureRef,cut.curveRef,cut.sourceState,cut.start,cut.reversed,placement(view,cut)?.transform]);
const number=n=>Math.abs(n)<1e-10?'0':Number(n.toFixed(8)).toString();
const sourceValues=(d,normal=false)=>d.frame==='Setup'?d.data[normal?'setupSurfaceNormal':'setupBaseDirection']:d.data[normal?'surfaceNormal':'baseDirection'];
function reset(d){d.mode=d.data.mode;d.values=sourceValues(d).map(number);d.eulerInput=null;d.dirty=false;d.error='';d.stale=false;}
const unit=values=>{const v=values.map(s=>String(s).trim()?Number(s):NaN),n=Math.hypot(...v);if(!v.every(Number.isFinite)||!Number.isFinite(n)||n<1e-12)throw Error('请输入有效的 IJK，且不能为零向量。');return v.map(x=>x/n);};
export function renderToolAxisParameters(view,cut){
  const d=view.fiveToolAxes?.get(cut.id),busy=view.pending||!!d?.request,disabled=busy||cut.locked||!d?.data||d.stale;
  const options=(items,value)=>items.map(([id,label])=>`<option value="${id}" ${id===value?'selected':''}>${label}</option>`).join('');
  return `<section class="five-tool-axis" data-five-key="tool-axis-${attr(cut.id)}"><h4>整条刀路姿态</h4>${!d?.data?`<p>${esc(d?.error||'正在读取刀轴…')}</p>${d?.error?'<button data-five-command="five.axis.cancel">重新读取</button>':''}`:`
    <label>方向模式<select data-five-axis-field="mode" aria-label="刀轴方向模式" ${disabled?'disabled':''}>${options([['SurfaceNormal','跟随面法向'],['Fixed','固定方向']],d.mode)}</select></label>
    <label>坐标系<select data-five-axis-field="frame" aria-label="刀轴坐标系" ${busy?'disabled':''}>${options([['Setup','装夹坐标系'],['Workpiece','工件坐标系']],d.frame)}</select></label>
    ${renderToolAxisVector(view,d,{prefix:'axis',scope:'刀轴',readonly:disabled,busy})}
    <p>${d.mode==='SurfaceNormal'?'显示切割起点姿态，沿轮廓随曲面法向变化。':'固定方向作为整条刀路的基准。'}方向从加工点指向刀头。${Number(d.data.keypointCount)>0?`已有 ${d.data.keypointCount} 个局部约束，局部姿态优先。`:''}${d.data.standardHole&&d.dirty?'自定义刀轴后按轮廓加工。':''}</p><p>紫色箭头：实际刀轴；灰色短箭头：参考面法向。两者重合时只显示刀轴。</p>${Number(d.data.keypointCount)>0?`<p>起点实际${toolAxisNotation(view)==='Euler'?'姿态角（°）':' IJK'}：${(toolAxisNotation(view)==='Euler'?directionToEuler(d.frame==='Setup'?d.data.setupDirection:d.data.direction):(d.frame==='Setup'?d.data.setupDirection:d.data.direction)).map(number).join('，')}</p>`:''}
    <div class="five-button-row"><button data-five-command="five.axis.apply" ${!d.dirty&&!d.request||view.pending||cut.locked||d.stale?'disabled':''}>${d.request?'重试':'应用'}</button><button data-five-command="five.axis.reverse" ${disabled?'disabled':''}>反向</button><button data-five-command="five.axis.cancel" ${busy?'disabled':''}>取消</button></div>
    ${d.dirty?'<p class="five-axis-preview-status">姿态预览 · 尚未应用</p>':''}${d.error?`<p role="alert">${esc(d.error)}</p>`:''}`}</section>`;
}
export function toolAxisFieldChanged(view,field,value){
  const cut=selected(view),d=view.fiveToolAxes?.get(cut?.id);if(!d?.data||view.pending||d.request||d.stale)return;
  if(field==='notation'){changeToolAxisNotation(view,d,value);return;}
  if(cut.locked)return;
  if(field==='frame'){
    if(!['Setup','Workpiece'].includes(value)||value===d.frame)return;
    try{validateToolAxisAngles(d);const v=new THREE.Vector3(...unit(d.values)),m=new THREE.Matrix4().set(...placement(view,cut).transform);if(value==='Workpiece')m.invert();v.transformDirection(m);d.values=v.toArray().map(number);d.frame=value;}catch(error){d.error=toolAxisErrorText(error);}return;
  }
  if(field.startsWith('angle-')){if(changeToolAxisAngle(d,field,value)){d.mode='Fixed';d.dirty=true;}return;}
  if(field==='mode'){
    if(!['Fixed','SurfaceNormal'].includes(value))return;d.mode=value;if(value==='SurfaceNormal'){d.values=sourceValues(d,true).map(number);d.eulerInput=null;}
  }else if(['0','1','2'].includes(field)){d.values[Number(field)]=value;d.eulerInput=null;d.mode='Fixed';}else return;
  d.dirty=true;d.error='';
}
export async function toolAxisCommand(context,view,action,ops,id=selected(view)?.id){
  const cut=view.scene?.intentToolpaths?.find(c=>c.id===id),d=view.fiveToolAxes?.get(id);if(!d||!cut||!alive(context,view)||view.pending||view.fiveToolAxisRequest&&view.fiveToolAxisRequest!==d.request)return;
  if(action==='cancel'){if(d.request)return;if(d.data&&!d.stale)reset(d);else view.fiveToolAxes.delete(id);ops.renderProject(context,view);return;}
  if(cut.locked)return;
  if(action==='reverse'){await reverseToolAxes(context,view,ops,[cut.id]);return;}
  if(action!=='apply'||d.stale||!d.data||!d.dirty&&!d.request)return;
  if(!d.request){try{validateToolAxisAngles(d);d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentId:id,scope:'WholePath',frame:d.frame,mode:d.mode,...(d.mode==='Fixed'?{direction:unit(d.values)}:{})};}catch(error){d.error=toolAxisErrorText(error);ops.renderProject(context,view);return;}}
  const request=d.request;view.pending=true;view.fiveToolAxisRequest=request;d.error='';ops.renderProject(context,view);
  try{
    let result;try{result=await context.sceneProxy.invoke('CamCut.SetToolAxis',request);}catch(error){let receipt;try{receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});}catch{throw error;}
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}result=receipt.result;}
    d.request=null;view.fiveToolAxisRequest=null;await saveCommittedCutEdit(context,view,ops);
    if(alive(context,view)){
      await refreshMachiningState(context,view,ops,request,result);if(result.axis)d.data=result.axis;d.key=key(view,view.scene.intentToolpaths.find(c=>c.id===id));reset(d);
      ops.appendProjectLog(context,'ok','刀轴姿态已保存。');
    }d.request=null;view.fiveToolAxisRequest=null;
  }catch(error){if(alive(context,view))d.error=d.request?'姿态保存结果待确认，请重试。':/OPPOSED/.test(error.message)?'整条刀路方向与已有局部关键点相反，请先调整局部方向。':/LOCKED/.test(error.message)?'刀路已锁定。':/EPOCH|REVISION/.test(error.message)?'工程已变化，请取消草稿后重新编辑。':toolAxisErrorText(error);if(!d.request)view.fiveToolAxisRequest=null;}
  finally{view.pending=false;if(alive(context,view))ops.renderProject(context,view);}
}
export async function reverseToolAxes(context,view,ops,ids){
  if(view.pending||!alive(context,view))return;
  if(!view.fiveAxisReverseRequest){
    await flushToolAxisEdits(context,view,ops);
    ids??=(view.scene?.intentToolpaths||[]).filter(c=>c.enabled!==false&&supportsToolAxis(c)).map(c=>c.id);
    if(!ids.length)return;
    view.fiveAxisReverseRequest={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentIds:ids,patch:{reverseAxes:true}};
  }
  const request=view.fiveAxisReverseRequest;view.pending=true;view.error='';ops.renderProject(context,view);
  try{
    try{await context.sceneProxy.invoke('CamProcess.UpdatePose',request);}catch(error){
      const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')view.fiveAxisReverseRequest=null;throw error;}
    }
    view.fiveAxisReverseRequest=null;await saveCommittedCutEdit(context,view,ops);
    if(alive(context,view)){
      const state=await readMachiningState(context);
      const nextView={...view,scene:{...view.scene,domain:state}};
      await Promise.all([...[...(view.fiveToolAxes||[])].filter(([id,d])=>request.intentIds.includes(id)&&d.data).map(async([id,d])=>{
        const data=await context.sceneProxy.invoke('CamCut.ReadToolAxis',{expected:state.expected,intentId:id});
        const cut=state.intentToolpaths.find(c=>c.id===id);d.data=data;d.key=key(nextView,cut);reset(d);
      }),reloadReversedPointDraft(context,view,nextView,request.intentIds)]);
      if(!alive(context,view))return;
      mergeDomainState(view.scene,state);view.fiveAxisReverseRequest=null;ops.appendProjectLog(context,'ok',`${request.intentIds.length} 条刀路的刀轴姿态已反向，请重新生成刀路和空移。`);}
  }catch(error){if(alive(context,view))view.error=view.fiveAxisReverseRequest?'姿态反向结果待确认，请重试。':toolAxisErrorText(error);}
  finally{view.pending=false;if(alive(context,view))ops.renderProject(context,view);}
}
export async function flushToolAxisEdits(context,view,ops){
  for(const [id,d] of view.fiveToolAxes||[])if(d.dirty||d.request){await toolAxisCommand(context,view,'apply',ops,id);if(d.dirty||d.request)throw Error(d.error||'刀轴姿态尚未保存。');}
}
export function disposeToolAxisPreview(view){const p=view.fiveToolAxisPreview;if(p){p.group.removeFromParent();p.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});}view.fiveToolAxisPreview=null;}
export function syncToolAxisParameters(context,view,ops){
  const cut=view.fiveAxisArea==='toolpath'&&activeTool(view)!=='keypoint'?selected(view):null;
  if(!cut){disposeToolAxisPreview(view);return;}
  const drafts=view.fiveToolAxes??=new Map(),snapshot=key(view,cut);let d=drafts.get(cut.id);
  if(d?.key!==snapshot){if(d?.dirty||d?.request){d.stale=true;d.error='刀路已改变，请取消草稿后重新编辑。';}else{d=null;drafts.delete(cut.id);}}
  if(!d&&!view.pending){
    d={key:snapshot,frame:'Setup',loading:true};drafts.set(cut.id,d);const state=d;
    void context.sceneProxy.invoke('CamCut.ReadToolAxis',{expected:view.scene.domain.expected,intentId:cut.id}).then(data=>{
      if(!alive(context,view)||drafts.get(cut.id)!==state||key(view,view.scene.intentToolpaths.find(c=>c.id===cut.id)||cut)!==snapshot)return;
      state.data=data;state.loading=false;reset(state);ops.renderProject(context,view);
    }).catch(error=>{if(alive(context,view)&&drafts.get(cut.id)===state){state.loading=false;state.error=toolAxisErrorText(error);ops.renderProject(context,view);}});
  }
  const vp=view.viewport,p=placement(view,cut);if(!vp||!p||!d?.data||d.stale||!d.dirty||view.fiveToolAxisDisplay?.visible===false){disposeToolAxisPreview(view);return;}
  const signature=JSON.stringify([vp.id,cut.id,d.key,d.values,d.frame,d.mode,d.dirty,placementWorldMatrix(view,p).elements]);if(view.fiveToolAxisPreview?.key===signature)return;
  disposeToolAxisPreview(view);const group=new THREE.Group();group.name='five-axis-tool-axis';group.matrixAutoUpdate=false;group.matrix.copy(placementWorldMatrix(view,p));
  const origin=new THREE.Vector3(...d.data.point),object=vp.sceneObjects.get(p.id);object?.geometry?.computeBoundingBox();const length=Math.max(.1,(object?.geometry?.boundingBox?.getSize(new THREE.Vector3()).length()||10)*.075);
  const arrow=(values,color,size)=>{const a=new THREE.ArrowHelper(new THREE.Vector3(...values).normalize(),origin,size,color,size*.2,size*.09);a.traverse(o=>{o.raycast=()=>{};o.renderOrder=45;if(o.material)o.material.depthTest=false;});group.add(a);};
  try{let v=new THREE.Vector3(...(!d.dirty?d.data.direction:d.mode==='SurfaceNormal'?d.data.surfaceNormal:unit(d.values)));if(d.dirty&&d.mode==='Fixed'&&d.frame==='Setup')v.transformDirection(new THREE.Matrix4().set(...p.transform).invert());if(v.dot(new THREE.Vector3(...d.data.surfaceNormal))<1-1e-6)arrow(d.data.surfaceNormal,sceneColors.reference,length*.7);arrow(v.toArray(),sceneColors.toolAxis,length);}catch{}
  vp.scene.add(group);view.fiveToolAxisPreview={key:signature,group};
}
