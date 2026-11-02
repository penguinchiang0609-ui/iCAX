import { toolAxisErrorText } from './toolAxisErrors.mjs';
import {revealSceneCut} from './sceneListReveal.mjs';
import {supportsToolAxis} from './toolAxisEligibility.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { activeTool,selectTool,workspaceState } from './workspaceModel.mjs';
import { nearestCutLine, selectCut } from './cutSelection.mjs';
import { refreshMachiningState } from './domainActions.mjs';
import { escapeText as esc,escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { sceneColors } from './scenePalette.mjs';
import { createToolAxisPointGlyph } from './toolAxisDisplay.mjs';
import { renderToolAxisVector, changeToolAxisAngle, changeToolAxisNotation, validateToolAxisAngles } from './toolAxisAngles.mjs';
import { renderAxisInfluence, loadAxisInfluence, resetAxisInfluence, axisInfluenceRequest, axisInfluenceCommand, axisInfluenceField, addAxisInfluenceGlyphs, pickAxisInfluence, syncAxisInfluenceGraph } from './toolAxisInfluence.mjs';

const alive=(c,v)=>!v.disposed&&c.isCurrentProject?.()!==false;
const selected=v=>{const poseId=v.fivePoseSelection?.active&&v.fivePoseSelection.selected?.intentId;if(poseId)return v.scene?.intentToolpaths?.find(c=>c.id===poseId);const u=workspaceState(v),ids=u.selectedIntentIds||[u.selectedIntentId];return ids.length===1?v.scene?.intentToolpaths?.find(c=>c.id===ids[0]):null;};
const placement=(v,cut)=>v.scene.domain.placements.find(p=>p.id===cut.placementId);
const key=(v,cut)=>JSON.stringify([v.scene.domain.expected.epoch,cut.id,cut.poseRef,cut.processRef,cut.featureRef,cut.curveRef,cut.sourceState,cut.start,cut.reversed,placement(v,cut)?.transform]);
const format=n=>Math.abs(n)<1e-10?'0':Number(n.toFixed(8)).toString();
const unit=values=>{const v=values.map(x=>String(x).trim()?Number(x):NaN),n=Math.hypot(...v);if(!v.every(Number.isFinite)||!Number.isFinite(n)||n<1e-12)throw Error('请输入有效的 IJK，且不能为零向量。');return v.map(x=>x/n);};
const points=d=>[...(d.points||[]),...(d.newPoint?[d.newPoint]:[])];
const row=d=>points(d).find(p=>p.id===d.pointId);
function renderPointPosition(d,readonly){
  return `<fieldset class="five-point-position" data-five-key="point-position"><legend>位置 XYZ（mm）</legend><div class="five-vector-fields">${['X','Y','Z'].map((label,i)=>`<label>${label}<input type="text" inputmode="decimal" data-five-key="point-position-${i}" data-five-point-field="position-${i}" aria-label="关键点位置 ${label}" value="${attr(d.positionValues?.[i]??'')}" ${readonly?'readonly':''}></label>`).join('')}</div></fieldset>`;
}
export const canDeleteToolAxisPoint=p=>!!p&&!p.system&&!p.sampled&&!['start','end'].includes(p.id);
function populate(d,axis){d.axis=axis;d.positionDirty=false;d.positionValues=(d.frame==='Setup'?axis.setupPoint:axis.point)?.map(format);d.values=(d.frame==='Setup'?axis.setupDirection:axis.direction).map(format);d.eulerInput=null;d.dirty=false;d.error='';resetAxisInfluence(d);}
function recoverPointReceipt(context,view,d,ops){
  if(d.recoveryTimer||d.recovering||!d.request)return;
  const request=d.request;
  d.recoveryTimer=setTimeout(async()=>{
    d.recoveryTimer=null;
    if(!alive(context,view)||view.fiveAxisPointEdit!==d||selected(view)?.id!==d.id||d.request!==request)return;
    if(view.pending||view.fiveAxisPointSaving){recoverPointReceipt(context,view,d,ops);return;}
    d.recovering=true;
    try{
      const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId},{timeoutMs:5000});
      if(!alive(context,view)||view.fiveAxisPointEdit!==d||d.request!==request)return;
      if(receipt.status==='Completed'){
        d.recoveredReceipt={commandId:request.commandId,result:receipt.result};
        await toolAxisPointCommand(context,view,'apply',ops);
      }else if(receipt.status==='NotFound'){
        // Never resubmit an uncertain edit or ask the user to confirm it.
        // A negative receipt retains the local angle as an unsaved draft.
        d.request=null;if(view.fiveToolAxisRequest===request)view.fiveToolAxisRequest=null;
        d.error='关键点未保存，当前角度已保留在编辑预览中。';
      }
    }catch(error){
      if(alive(context,view)&&view.fiveAxisPointEdit===d&&d.request===request){
        if(/CAM_EPOCH/.test(error.message)){d.request=null;d.stale=true;if(view.fiveToolAxisRequest===request)view.fiveToolAxisRequest=null;d.error='工程已变化，当前角度预览已保留。';}
        else d.error='正在恢复关键点保存结果…';
      }
    }finally{
      d.recovering=false;d.recoveryDelay=Math.min(30000,(d.recoveryDelay||500)*2);
      if(alive(context,view)&&view.fiveAxisPointEdit===d){ops.renderProject(context,view);if(d.request)recoverPointReceipt(context,view,d,ops);}
    }
  },d.recoveryDelay||500);
}
export function recoverToolAxisPointReceipt(context,view,ops){
  const d=view.fiveAxisPointEdit;if(d?.request)recoverPointReceipt(context,view,d,ops);
}
export function exitToolAxisPointModes(view){
  view.fiveAxisPointInsert=false;view.fiveAxisPointInsertOwner=null;view.fiveAxisPointDelete=false;view.fiveAxisPointDeleteOwner=null;
  if(view.fiveAxisPointEdit)view.fiveAxisPointEdit.picking=false;
  const lookup=view.fiveAxisPointLookup;
  // A submitted edit still owns its receipt and automatic save. Only a read
  // which has not reached submission can be abandoned by leaving add mode.
  if(lookup&&lookup.phase!=='committing'){
    lookup.cancelled=true;view.fiveAxisPointLookup=null;
    if(lookup.pending&&!view.fiveToolAxisRequest)view.pending=false;
    if(lookup.draft&&view.fiveAxisPointEdit===lookup.draft){lookup.draft.loading=false;lookup.draft.readToken=(lookup.draft.readToken||0)+1;}
  }
}
export function prepareToolAxisPointModes(view){
  const owner=view.fiveAxisPointInsert?view.fiveAxisPointInsertOwner:view.fiveAxisPointDeleteOwner;
  if((view.fiveAxisPointInsert||view.fiveAxisPointDelete)&&(!owner||view.disposed||view.fiveAxisArea!=='toolpath'||view.fiveCamPage==='rapid'||activeTool(view)!=='keypoint'
    ||owner.epoch!==view.scene?.domain?.expected?.epoch||owner.setupId!==view.scene?.domain?.activeSetupId))exitToolAxisPointModes(view);
}
export function renderToolAxisPoints(view,cut){
  const d=view.fiveAxisPointEdit,selecting=!!view.fivePoseSelection?.active,busy=view.pending||d?.loading||d?.influenceLoading||d?.request||d?.rotation?.gesture,readonly=selecting||busy||cut.locked||!d?.axis||d?.stale,p=d&&row(d);
  const options=(items,value)=>items.map(([id,label])=>`<option value="${attr(id)}" ${id===value?'selected':''}>${esc(label)}</option>`).join('');
  return `<section class="five-tool-axis five-axis-keypoints" data-five-key="axis-keypoints-${attr(cut.id)}"><h4>局部关键点姿态</h4>${!d||d.id!==cut.id?'<p>正在读取关键点…</p>':`
    <label>当前关键点<select data-five-point-field="point" aria-label="当前刀轴关键点" ${selecting||busy?'disabled':''}>${options(points(d).map(p=>[p.id,p.label+(p.overridden?' · 已修改':'')]),d.pointId)}</select></label>
    ${view.fiveAxisPointInsert?'<p class="five-axis-preview-status">连续新增已开启：点击刀路依次添加并自动保存；再次点击“新增关键点”或按 Esc 退出。</p>':''}${p?`<p>位置：${(d.frame==='Setup'?p.setupPoint:p.point).map(n=>format(n)).join('，')} mm</p>`:''}
    ${view.fiveAxisPointDelete?'<p class="five-axis-preview-status">连续删除已开启：依次点击手动关键点删除并自动保存；系统关键点不能删除。再次点击“删除关键点”或按 Esc 退出。</p>':''}
    <label>坐标系<select data-five-point-field="frame" aria-label="关键点刀轴坐标系" ${busy?'disabled':''}>${options([['Setup','装夹坐标系'],['Workpiece','工件坐标系']],d.frame)}</select></label>
    ${renderPointPosition(d,readonly)}
    ${renderToolAxisVector(view,d,{prefix:'point',scope:'关键点',readonly,busy})}
    <p class="five-point-edit-scope">修改范围：当前姿态${d.influence?.stations.some(s=>s.weight!==null)?'及选中的跟随姿态':' · 保留其他姿态'}</p>
    ${renderAxisInfluence(view,d,readonly)}
    <label>局部过渡<select data-five-point-field="transition" aria-label="关键点姿态过渡" ${readonly?'disabled':''}>${options([['ArcLength','按弧长'],['DirectionChange','按导矢变化']],d.transition||'ArcLength')}</select></label>
    <p>${selecting?'姿态选择模式：点击紫色关键点箭头查看姿态，再点击“编辑关键点”调姿。':'左键拖动旋转环调姿，松开保存；点击环可输入角度。右键拖动旋转视角、中键拖动平移、滚轮缩放。'}方向从加工点指向刀头，选定姿态之间连续过渡。整条修改请使用“整条刀路”。</p>
    <div class="five-button-row"><button data-five-command="five.points.apply" ${selecting||!d.dirty||d.request||view.pending||cut.locked||d.stale?'disabled':''}>${d.request?'正在保存':'应用'}</button><button data-five-command="five.points.reset" ${readonly||!p?.overridden?'disabled':''}>恢复默认</button><button data-five-command="five.points.cancel" ${selecting||busy?'disabled':''}>取消修改</button></div>
    ${d.dirty?'<p class="five-axis-preview-status">当前点姿态预览 · 尚未应用</p>':cut.poseDeferred||cut.sourceState==='Pending'?'<p class="five-axis-preview-status">姿态已保存；生成、仿真或碰撞检测时更新过渡。</p>':''}${d.error?`<p role="alert">${esc(d.error)}</p>`:''}`}</section>`;
}
// Selecting a displayed axis is a read-only inspection. Its exact source anchor
// becomes a manual keypoint only after the user changes and applies its pose.
export async function inspectToolAxisSample(context,view,hit,ops){
  if(!alive(context,view)||!view.fivePoseSelection?.active||view.pending||view.fiveToolAxisRequest)return;
  const cut=view.scene?.intentToolpaths?.find(c=>c.id===hit.intentId&&c.enabled!==false&&supportsToolAxis(c));if(!cut)return;
  const snapshot=key(view,cut),previous=view.fiveAxisPointEdit,ui=workspaceState(view);
  ui.selectedIntentId=cut.id;ui.selectedIntentIds=[cut.id];ui.cutSelectionAnchor=cut.id;ui.selectedGroupId='';ui.selectedGroupIds=[];
  const d=view.fiveAxisPointEdit={id:cut.id,key:snapshot,frame:previous?.frame||'Setup',loading:true,dirty:false,pointId:'',picking:false,readToken:1},readToken=d.readToken;
  const owns=()=>{const current=view.scene?.intentToolpaths?.find(c=>c.id===cut.id);return alive(context,view)&&view.fiveAxisPointEdit===d&&d.readToken===readToken&&current&&key(view,current)===snapshot;};
  ops.renderProject(context,view);
  revealSceneCut(context,view,ops);
  try{
    const expected=structuredClone(view.scene.domain.expected),[list,axis]=await Promise.all([
      previous?.id===cut.id&&previous.key===snapshot&&previous.points?Promise.resolve({points:previous.points}):context.sceneProxy.invoke('CamCut.ListToolAxisPoints',{expected,intentId:cut.id}),
      context.sceneProxy.invoke('CamCut.ReadToolAxis',{expected,intentId:cut.id,anchor:hit.sample.anchor})]);
    if(!owns())return;d.points=list.points;
    const point=d.points.find(p=>hit.sample.keypointIds.includes(p.id));
    if(point){d.pointId=point.id;d.transition=point.transition;}
    else throw Error('关键点已改变，请重新选择。');
    populate(d,axis);
  }catch(error){if(owns())d.error=toolAxisErrorText(error);}
  finally{if(owns()){d.loading=false;ops.renderProject(context,view);}}
}
// Diagnostic navigation resolves the exact source location without inserting a
// saved keypoint. An interpolated location becomes an edit only on user apply.
export async function inspectToolAxisAtPoint(context,view,chosen,ops){
  if(!alive(context,view)||view.pending||view.fiveToolAxisRequest||view.fiveAxisArea!=='toolpath'||activeTool(view)!=='keypoint')return false;
  const cut=view.scene?.intentToolpaths?.find(c=>c.id===chosen?.intentId&&c.enabled!==false&&!c.locked&&supportsToolAxis(c));
  if(!cut||selected(view)?.id!==cut.id)return false;
  const endpoint=!!chosen.pointId,anchored=!!chosen.anchor;
  if(!endpoint&&!anchored&&(!Array.isArray(chosen.nearPoint)||chosen.nearPoint.length!==3||!chosen.nearPoint.every(n=>typeof n==='number'&&Number.isFinite(n))||typeof chosen.maximumDistanceMm!=='number'||!Number.isFinite(chosen.maximumDistanceMm)||chosen.maximumDistanceMm<0))return false;
  const previous=view.fiveAxisPointEdit;
  if(previous?.dirty||previous?.request||previous?.rotation?.gesture||previous?.influenceLoading)return false;
  exitToolAxisPointModes(view);
  const snapshot=key(view,cut),expected=structuredClone(view.scene.domain.expected),expectedKey=JSON.stringify(expected);
  const d=view.fiveAxisPointEdit={id:cut.id,key:snapshot,frame:previous?.frame==='Workpiece'?'Workpiece':'Setup',loading:true,dirty:false,pointId:'',picking:false,readToken:1},token=d.readToken;
  const owns=()=>view.fiveAxisPointEdit===d&&d.readToken===token;
  const current=()=>{const live=selected(view);return owns()&&alive(context,view)&&view.fiveAxisArea==='toolpath'&&activeTool(view)==='keypoint'&&live?.id===cut.id&&key(view,live)===snapshot&&JSON.stringify(view.scene?.domain?.expected)===expectedKey&&!d.dirty&&!d.request&&!d.rotation?.gesture;};
  ops.renderProject(context,view);
  try{
    let list,resolved;
    if(endpoint){
      list=await context.sceneProxy.invoke('CamCut.ListToolAxisPoints',{expected,intentId:cut.id});if(!current())return false;
      const point=list.points.find(p=>p.id===chosen.pointId);if(!point)throw Error('系统关键点已改变，请重新定位。');
      const axis=await context.sceneProxy.invoke('CamCut.ReadToolAxis',{expected,intentId:cut.id,anchor:point.anchor});resolved={pointId:point.id,anchor:point.anchor,axis};
    }else if(anchored){
      const [rows,axis]=await Promise.all([context.sceneProxy.invoke('CamCut.ListToolAxisPoints',{expected,intentId:cut.id}),context.sceneProxy.invoke('CamCut.ReadToolAxis',{expected,intentId:cut.id,anchor:chosen.anchor})]);
      list=rows;resolved={anchor:chosen.anchor,axis};
    }else [list,resolved]=await Promise.all([
      context.sceneProxy.invoke('CamCut.ListToolAxisPoints',{expected,intentId:cut.id}),
      context.sceneProxy.invoke('CamCut.ResolveToolAxisPoint',{expected,intentId:cut.id,nearPoint:[...chosen.nearPoint],maximumDistanceMm:chosen.maximumDistanceMm,frame:'Workpiece'})]);
    if(!current())return false;
    d.points=list.points;
    if(resolved.pointId){
      const point=d.points.find(p=>p.id===resolved.pointId);if(!point)throw Error('关键点已改变，请重新定位。');
      d.pointId=point.id;d.transition=point.transition;d.newPoint=null;
    }else{
      d.pointId=crypto.randomUUID();d.transition='ArcLength';
      d.newPoint={id:d.pointId,label:'局部位置（未保存）',anchor:resolved.anchor,point:resolved.axis.point,setupPoint:resolved.axis.setupPoint,system:false,overridden:false,transition:d.transition};
    }
    populate(d,resolved.axis);
    if(row(d)?.influence)await loadAxisInfluence(context,view,d,ops);
    return current();
  }catch(error){if(current())d.error=toolAxisErrorText(error);return false;}
  finally{if(owns()){d.loading=false;if(alive(context,view)&&selected(view)?.id===d.id){if(!d.dirty&&!d.request&&JSON.stringify(view.scene?.domain?.expected)!==expectedKey)d.error='工程已变化，请重新定位当前刀路位置。';ops.renderProject(context,view);}}}
}
async function selectPoint(context,view,d,id,ops){
  const p=points(d).find(p=>p.id===id);if(!p)return;d.loading=true;d.pointId=id;d.transition=p.transition;d.error='';d.rotation=null;d.influence=null;d.influenceLoading=null;
  const token=(d.readToken||0)+1;d.readToken=token;const snapshot=d.key;ops.renderProject(context,view);
  const owns=()=>alive(context,view)&&view.fiveAxisPointEdit===d&&selected(view)?.id===d.id&&d.readToken===token&&d.key===snapshot&&d.pointId===id;
  try{const data=await context.sceneProxy.invoke('CamCut.ReadToolAxis',{expected:view.scene.domain.expected,intentId:d.id,anchor:p.anchor});if(owns()){populate(d,data);if(p.influence)await loadAxisInfluence(context,view,d,ops);}}
  catch(error){if(owns())d.error=toolAxisErrorText(error);}
  finally{if(owns()){d.loading=false;if(alive(context,view))ops.renderProject(context,view);}}
}
export function toolAxisPointField(view,field,value){
  const d=view.fiveAxisPointEdit,cut=selected(view);if(!d?.axis||!cut||view.pending||d.loading||d.request||d.stale||d.rotation?.gesture||d.influenceLoading)return;
  if(field.startsWith('influence-')){if(!cut.locked)axisInfluenceField(view,d,field,value);return;}
  if(field==='notation'){changeToolAxisNotation(view,d,value);return;}
  if(field==='frame'){
    if(!['Setup','Workpiece'].includes(value)||value===d.frame)return;
    d.rotation=null;
    try{validateToolAxisAngles(d);const m=new THREE.Matrix4().set(...placement(view,cut).transform);if(value==='Workpiece')m.invert();const v=new THREE.Vector3(...unit(d.values)).transformDirection(m);d.values=v.toArray().map(format);if(d.positionValues?.every(x=>String(x).trim()&&Number.isFinite(Number(x))))d.positionValues=new THREE.Vector3(...d.positionValues.map(Number)).applyMatrix4(m).toArray().map(format);d.frame=value;}catch(error){d.error=toolAxisErrorText(error);}return;
  }
  if(view.fivePoseSelection?.active||cut.locked)return;
  d.rotation=null;
  if(field.startsWith('angle-')){if(changeToolAxisAngle(d,field,value))d.dirty=true;return;}
  if(/^position-[012]$/.test(field)){d.positionValues[Number(field.at(-1))]=value;d.positionDirty=true;d.dirty=true;d.error='';return;}
  if(field==='transition'){d.transition=value;d.dirty=true;return;}else if(['0','1','2'].includes(field)){d.values[Number(field)]=value;d.eulerInput=null;}else return;d.dirty=true;d.error='';
}
export async function toolAxisPointCommand(context,view,action,ops,value){
  let d=view.fiveAxisPointEdit,cut=selected(view);if(!alive(context,view))return;
  if(d?.request&&(d.recoveryTimer||d.recovering)&&!d.recoveredReceipt){recoverPointReceipt(context,view,d,ops);return;}
  if(['exit-add','exit-delete'].includes(action)||action==='add'&&view.fiveAxisPointInsert||action==='delete'&&view.fiveAxisPointDelete){exitToolAxisPointModes(view);ops.renderProject(context,view);return;}
  if(action==='cancel'&&view.fiveAxisPointLookup){
    exitToolAxisPointModes(view);if(!view.fiveToolAxisRequest)view.fiveAxisPointEdit=null;ops.renderProject(context,view);return;
  }
  if(view.pending||d?.rotation?.gesture||view.fivePoseSelection?.active&&!['edit','add','delete','open','pick','whole','select','start','end'].includes(action))return;
  if(action.startsWith('influence-')){if(d?.axis&&!d.loading&&!d.influenceLoading&&!d.request&&!d.stale&&!cut?.locked)await axisInfluenceCommand(context,view,d,action.slice(10),ops);return;}
  if(['add','delete','edit','open','pick'].includes(action)){
    if(d?.dirty||d?.request){await toolAxisPointCommand(context,view,'apply',ops);if(d.dirty||d.request)return;}
    const whole=view.fiveToolAxes?.get(cut?.id);
    if(whole?.dirty||whole?.request){const {toolAxisCommand}=await import('./toolAxisParameters.mjs');await toolAxisCommand(context,view,'apply',ops);if(whole.dirty||whole.request)return;}
    selectTool(view,'keypoint');view.fiveCamPage='cutting';view.fiveAxisExtractionOpen=false;view.fiveAxisSplitOpen=false;view.fiveAxisLeadsOpen=false;view.fiveAxisHoleOpen=false;
    exitToolAxisPointModes(view);
    if(['add','pick','delete'].includes(action)){
      const owner={epoch:view.scene?.domain?.expected?.epoch,setupId:view.scene?.domain?.activeSetupId};
      if(action==='delete'){view.fiveAxisPointDelete=true;view.fiveAxisPointDeleteOwner=owner;}
      else{view.fiveAxisPointInsert=true;view.fiveAxisPointInsertOwner=owner;}
      if(d?.influence){d.influence.picking=false;d.influence.rangeMode=false;d.influence.rangeStart=null;}
    }
    if(d){d.picking=!!view.fiveAxisPointInsert;d.rotation=null;}
    if(d?.axis&&!d.influence&&row(d)?.influence)await loadAxisInfluence(context,view,d,ops);
    ops.renderProject(context,view);return;
  }
  if(action==='whole'){
    if(d?.dirty||d?.request){await toolAxisPointCommand(context,view,'apply',ops);if(d.dirty||d.request)return;}
    exitToolAxisPointModes(view);selectTool(view,'extract');view.fiveCamPage='cutting';view.fiveAxisExtractionOpen=false;ops.renderProject(context,view);return;
  }
  if(!cut)return;
  if(!d||d.id!==cut.id||d.loading||view.fiveToolAxisRequest&&view.fiveToolAxisRequest!==d.request)return;
  if(action==='cancel'){
    if(d.request)return;exitToolAxisPointModes(view);d.rotation=null;if(d.stale){view.fiveAxisPointEdit=null;ops.renderProject(context,view);return;}
    d.newPoint=null;d.dirty=false;await selectPoint(context,view,d,points(d).some(p=>p.id===d.pointId)?d.pointId:'start',ops);return;
  }
  if(d.stale)return;
  if(['start','end','select'].includes(action)){
    if(d.dirty||d.request){await toolAxisPointCommand(context,view,'apply',ops);if(d.dirty||d.request)return;}
    d.picking=!!view.fiveAxisPointInsert;await selectPoint(context,view,d,action==='select'?value:action,ops);return;
  }
  if(cut.locked||!d.axis)return;
  if(action==='reverse'){
    if(d.request)return;
    try{validateToolAxisAngles(d);d.values=unit(d.values).map(n=>format(-n));d.eulerInput=null;d.rotation=null;d.dirty=true;d.error='';}catch(error){d.error=toolAxisErrorText(error);}
    ops.renderProject(context,view);return;
  }
  const deleteTarget=action==='delete-point'&&value?points(d).find(p=>p.id===value):row(d);
  if(action==='delete-point'&&!canDeleteToolAxisPoint(deleteTarget))return;
  if(action==='delete-point'&&deleteTarget===d.newPoint){d.newPoint=null;d.dirty=false;await selectPoint(context,view,d,'start',ops);return;}
  if(!['apply','delete-point','reset'].includes(action)||action==='apply'&&!d.dirty&&!d.request)return;
  if(!d.request){try{if(action==='apply')validateToolAxisAngles(d);const influence=action==='apply'?axisInfluenceRequest(d):null;if(action==='apply'&&(!d.positionValues?.every(x=>String(x).trim()&&Number.isFinite(Number(x)))))throw Error('请输入有效的 XYZ 位置。');d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentId:cut.id,pointId:action==='delete-point'?deleteTarget.id:d.pointId,action:action==='apply'?'Upsert':action==='delete-point'?'Delete':'Reset',...(action==='apply'?{anchor:row(d).anchor,direction:unit(d.values),...(d.positionDirty?{position:d.positionValues.map(Number)}:{}),frame:d.frame,transition:d.transition,...(influence?{influence}:{})}:{})};}catch(error){d.error=toolAxisErrorText(error);ops.renderProject(context,view);return;}}
  const request=d.request;view.fiveToolAxisRequest=request;view.fiveAxisPointSaving=request;d.error='';ops.renderProject(context,view);
  try{let result;try{result=d.recoveredReceipt?.commandId===request.commandId?d.recoveredReceipt.result:await context.sceneProxy.invoke('CamCut.EditToolAxisPoint',request);}catch(error){let receipt;try{receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});}catch{throw error;}
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}result=receipt.result;}
    d.request=null;view.fiveToolAxisRequest=null;d.recoveredReceipt=null;d.recoveryDelay=0;
    // An acknowledged transaction keeps its file-save obligation even if the
    // user has selected another point while its result was arriving.
    const previousSaveError=view.fiveCutSaveError;
    void saveCommittedCutEdit(context,view,ops).then(()=>{if(alive(context,view)&&(view.fiveCutSaveError||previousSaveError))ops.renderProject(context,view);});
    if(alive(context,view)){
      const applied=await refreshMachiningState(context,view,ops,request,result);
      if(result.statePatch?.deferredPose&&!applied){d.stale=true;d.error='工程已变化，已保存的姿态和当前角度预览均已保留。';return;}
      if(view.fiveAxisPointEdit!==d){d.request=null;if(view.fiveToolAxisRequest===request)view.fiveToolAxisRequest=null;return;}
      d.key=key(view,view.scene.intentToolpaths.find(c=>c.id===d.id));if(d.rotation)d.rotation.key=d.key;d.points=result.points;d.newPoint=null;d.request=null;d.stale=false;d.picking=!!view.fiveAxisPointInsert;view.fiveToolAxisRequest=null;
      if(request.action==='Delete')d.pointId=request.pointId;
      if(!points(d).some(p=>p.id===d.pointId))d.pointId='start';d.transition=row(d)?.transition;populate(d,result.selectedAxis);
      if(d.rotation&&request.action==='Upsert')Object.assign(d.rotation,{key:d.key,savedValues:[...d.values],savedFrame:d.frame,savedDirty:false,savedValue:d.rotation.value});
      if(request.action!=='Upsert')d.loading=false;
      ops.appendProjectLog(context,'ok','局部刀轴关键点已保存。');
    }else{d.request=null;d.stale=true;if(view.fiveToolAxisRequest===request)view.fiveToolAxisRequest=null;}
  }catch(error){if(alive(context,view))d.error=d.request?'正在恢复关键点保存结果…':/INFLUENCED_POINT/.test(error.message)?'该姿态正在跟随另一主姿态，请先在原主姿态的影响范围中取消它。':/INFLUENCE_OWNER_SELECTED|INFLUENCE_OVERLAP/.test(error.message)?'选择范围与另一主姿态重叠，请取消重复的姿态。':/ENDPOINT_NOT/.test(error.message)?'起点、终点不能删除。':toolAxisErrorText(error);if(!d.request)view.fiveToolAxisRequest=null;}
  finally{if(view.fiveToolAxisRequest===request&&!d.request)view.fiveToolAxisRequest=null;if(view.fiveAxisPointSaving===request)view.fiveAxisPointSaving=null;if(alive(context,view)){ops.renderProject(context,view);if(d.request)recoverPointReceipt(context,view,d,ops);}}
}
export function toolAxisPointAtPointer(view,event){
  const d=view.fiveAxisPointEdit;if(!d?.axis||d.loading||d.stale)return null;
  const box=view.viewport.renderer.domElement.getBoundingClientRect();let best=11,chosen=null;
  for(const p of points(d)){const q=new THREE.Vector3(...p.setupPoint).project(view.viewport.camera);if(q.z<-1||q.z>1)continue;
    const distance=Math.hypot(event.clientX-box.left-(q.x+1)*box.width/2,event.clientY-box.top-(1-q.y)*box.height/2);
    if(distance<best){best=distance;chosen=p;}}
  return chosen;
}
export function handleToolAxisPointPick(context,view,_data,_hit,event,_hits,ops){
  if(view.fiveAxisArea!=='toolpath'||activeTool(view)!=='keypoint'||!event)return false;
  if(event.button!=null&&event.button!==0)return false;
  let d=view.fiveAxisPointEdit;
  if(view.pending||view.fiveAxisPointLookup||view.fiveToolAxisRequest||d?.loading||d?.influenceLoading)return true;
  if(d?.rotation?.gesture)return true;
  if(view.fiveAxisPointDelete){
    const chosen=toolAxisPointAtPointer(view,event);
    if(canDeleteToolAxisPoint(chosen))void deleteToolAxisPoint(context,view,chosen.id,ops);
    return true;
  }
  if(pickAxisInfluence(context,view,d,event,ops))return true;
  // Marker hit testing is in screen pixels, independent of model scale/camera.
  if(d?.axis&&!d.loading){
    const chosen=toolAxisPointAtPointer(view,event);
    if(chosen){
      if(view.fiveAxisPointInsert)void insertToolAxisPoint(context,view,{intentId:d.id,pointId:chosen.id},ops);
      else{if(selected(view)?.id!==d.id)selectCut(view,d.id);if(chosen.id!==d.pointId)void toolAxisPointCommand(context,view,'select',ops,chosen.id);else ops.renderProject(context,view);revealSceneCut(context,view,ops);}
      return true;
    }
  }
  if(!view.fiveAxisPointInsert&&!d?.picking)return false;
  const chosen=nearestCutLine(view,event);if(!chosen)return true;
  void insertToolAxisPoint(context,view,chosen,ops);return true;
}
async function insertToolAxisPoint(context,view,chosen,ops){
  if(view.fiveAxisPointLookup||!view.fiveAxisPointInsert)return;
  let d=view.fiveAxisPointEdit;
  const owner=view.fiveAxisPointInsertOwner,selection=selected(view)?.id,token={draft:d,phase:'prepare',pending:false};view.fiveAxisPointLookup=token;
  const current=()=>alive(context,view)&&!token.cancelled&&view.fiveAxisPointLookup===token&&view.fiveAxisPointInsert&&view.fiveAxisPointInsertOwner===owner
    &&view.fiveAxisArea==='toolpath'&&view.fiveCamPage!=='rapid'&&activeTool(view)==='keypoint';
  try{
    // Finish the current point before changing its owning path. Invalid text
    // remains in that editor and never becomes a point on the next path.
    if(d?.dirty||d?.request){token.phase='committing';await flushToolAxisPointEdits(context,view,ops);token.phase='prepare';}
    if(!current()||selected(view)?.id!==selection)return;
    const cut=view.scene?.intentToolpaths?.find(c=>c.id===chosen.intentId&&c.enabled!==false&&!c.locked&&supportsToolAxis(c));if(!cut)return;
    if(selected(view)?.id!==cut.id)selectCut(view,cut.id);
    if(selected(view)?.id!==cut.id)return;
    if(!d||d.id!==cut.id||d.key!==key(view,cut))d=view.fiveAxisPointEdit={id:cut.id,key:key(view,cut),frame:'Setup',pointId:'start'};
    token.draft=d;
    if(chosen.pointId){await selectPoint(context,view,d,chosen.pointId,ops);d.picking=!!view.fiveAxisPointInsert;return;}
    const snapshot=d.key,request={expected:structuredClone(view.scene.domain.expected),intentId:d.id,nearPoint:chosen.nearPoint,maximumDistanceMm:chosen.maximumDistanceMm,frame:'Workpiece'};
    const expected=JSON.stringify(request.expected),owns=()=>current()&&view.fiveAxisPointEdit===d&&selected(view)?.id===d.id&&d.key===snapshot
      &&JSON.stringify(view.scene?.domain?.expected)===expected&&key(view,selected(view))===snapshot;
    token.phase='resolving';token.pending=true;d.readToken=(d.readToken||0)+1;d.loading=true;d.picking=true;d.rotation=null;view.pending=true;ops.renderProject(context,view);
    const [result,list]=await Promise.all([context.sceneProxy.invoke('CamCut.ResolveToolAxisPoint',request),d.points?Promise.resolve({points:d.points}):context.sceneProxy.invoke('CamCut.ListToolAxisPoints',{expected:request.expected,intentId:d.id})]);
    if(!owns())return;
    d.points=list.points;
    if(result.pointId){d.pointId=result.pointId;d.transition=row(d)?.transition;populate(d,result.axis);}
    else{d.pointId=crypto.randomUUID();d.newPoint={id:d.pointId,label:'新增关键点',anchor:result.anchor,point:result.axis.point,setupPoint:result.axis.setupPoint,system:false,overridden:false,transition:'ArcLength'};d.transition='ArcLength';populate(d,result.axis);d.dirty=true;}
    token.pending=false;view.pending=false;d.loading=false;d.picking=!!view.fiveAxisPointInsert;
    if(!result.pointId&&owns()){token.phase='committing';await toolAxisPointCommand(context,view,'apply',ops);}
  }catch(error){if(alive(context,view)&&view.fiveAxisPointEdit===d&&!token.cancelled)d.error=toolAxisErrorText(error);}
  finally{
    if(view.fiveAxisPointLookup===token){view.fiveAxisPointLookup=null;if(token.pending&&!view.fiveToolAxisRequest)view.pending=false;if(view.fiveAxisPointEdit===d)d.loading=false;}
    if(alive(context,view))ops.renderProject(context,view);
  }
}
async function deleteToolAxisPoint(context,view,pointId,ops){
  if(view.fiveAxisPointLookup||!view.fiveAxisPointDelete)return;
  const d=view.fiveAxisPointEdit,owner=view.fiveAxisPointDeleteOwner,selection=selected(view)?.id;
  if(!d||!canDeleteToolAxisPoint(points(d).find(p=>p.id===pointId)))return;
  const token={draft:d,phase:'prepare',pending:false};view.fiveAxisPointLookup=token;
  const current=()=>alive(context,view)&&!token.cancelled&&view.fiveAxisPointLookup===token&&view.fiveAxisPointDelete&&view.fiveAxisPointDeleteOwner===owner
    &&view.fiveAxisPointEdit===d&&selected(view)?.id===selection&&selection===d.id&&view.fiveAxisArea==='toolpath'&&view.fiveCamPage!=='rapid'&&activeTool(view)==='keypoint';
  try{
    if(d.dirty||d.request){token.phase='committing';await flushToolAxisPointEdits(context,view,ops);token.phase='prepare';}
    const cut=selected(view);
    if(!current()||!cut||cut.locked||cut.enabled===false||d.key!==key(view,cut)||!canDeleteToolAxisPoint(points(d).find(p=>p.id===pointId)))return;
    d.rotation=null;token.phase='committing';
    // Keep the displayed point and its matching axis together if the native
    // command rejects the clicked target. The receipt refreshes survivors.
    await toolAxisPointCommand(context,view,'delete-point',ops,pointId);
  }catch(error){if(alive(context,view)&&view.fiveAxisPointEdit===d&&!token.cancelled)d.error=toolAxisErrorText(error);}
  finally{if(view.fiveAxisPointLookup===token)view.fiveAxisPointLookup=null;if(alive(context,view))ops.renderProject(context,view);}
}
export function disposeToolAxisPointPreview(view){const p=view.fiveAxisPointPreview;if(p){p.group.removeFromParent();p.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});}view.fiveAxisPointPreview=null;}
export function syncToolAxisPoints(context,view,ops){
  prepareToolAxisPointModes(view);
  const cut=view.fiveAxisArea==='toolpath'&&activeTool(view)==='keypoint'?selected(view):null;if(!cut){disposeToolAxisPointPreview(view);return;}
  const snapshot=key(view,cut);let d=view.fiveAxisPointEdit;
  if(d?.request&&!view.pending&&!view.fiveAxisPointSaving)recoverPointReceipt(context,view,d,ops);
  if(d?.id!==cut.id||d.key!==snapshot&&!d.dirty&&!d.request){d=null;view.fiveAxisPointEdit=null;}
  if(d&&d.key!==snapshot&&(d.dirty||d.request))d.stale=true;
  if(!d&&!view.pending){d=view.fiveAxisPointEdit={id:cut.id,key:snapshot,frame:'Setup',pointId:'start',loading:true,picking:!!view.fiveAxisPointInsert};const state=d;
    void context.sceneProxy.invoke('CamCut.ListToolAxisPoints',{expected:view.scene.domain.expected,intentId:cut.id}).then(async result=>{
      if(!alive(context,view)||view.fiveAxisPointEdit!==state||key(view,selected(view)||cut)!==snapshot)return;
      state.points=result.points;await selectPoint(context,view,state,'start',ops);
    }).catch(error=>{if(alive(context,view)&&view.fiveAxisPointEdit===state){state.error=toolAxisErrorText(error);state.loading=false;ops.renderProject(context,view);}});
  }
  const vp=view.viewport,p=placement(view,cut);if(view.fivePoseSelection?.active||!vp||!p||!d?.axis||d.stale){disposeToolAxisPointPreview(view);return;}
  syncAxisInfluenceGraph(context,view,d,ops);
  const signature=JSON.stringify([vp.id,d.key,d.pointId,d.values,d.positionValues,d.frame,points(d),d.influence?.stations,d.influence?.interpolation]);if(view.fiveAxisPointPreview?.key===signature)return;
  disposeToolAxisPointPreview(view);const root=new THREE.Group();root.name='five-axis-pose-keypoints';root.matrixAutoUpdate=false;root.matrix.set(...p.transform);
  const marker=(items,shape,color,size)=>{
    if(!items.length)return;const geometry=new THREE.BufferGeometry().setFromPoints(items.map(point=>new THREE.Vector3(...point.point)));
    const material=new THREE.PointsMaterial({color,size,sizeAttenuation:false,depthTest:false});
    material.onBeforeCompile=shader=>{shader.fragmentShader=shader.fragmentShader.replace('#include <clipping_planes_fragment>',`#include <clipping_planes_fragment>\n${shape==='ring'?'float r=length(gl_PointCoord-vec2(.5));if(r>.5||r<.31)discard;':shape==='circle'?'if(length(gl_PointCoord-vec2(.5))>.5)discard;':''}`);};
    material.customProgramCacheKey=()=>`five-pose-marker-${shape}`;
    const dots=new THREE.Points(geometry,material);dots.renderOrder=shape==='ring'?48:44;dots.raycast=()=>{};dots.userData.poseMarkerShape=shape;dots.userData.pointIds=items.map(point=>point.id);root.add(dots);
  };
  marker(points(d).filter(point=>point.system),'circle',sceneColors.reference,8);
  marker(points(d).filter(point=>!point.system),'square',sceneColors.reference,8);
  const savedPoint=row(d),previewPosition=d.positionValues?.every(x=>String(x).trim()&&Number.isFinite(Number(x)))?new THREE.Vector3(...d.positionValues.map(Number)):null;if(previewPosition&&d.frame==='Setup')previewPosition.applyMatrix4(new THREE.Matrix4().set(...p.transform).invert());
  const at=savedPoint&&previewPosition?{...savedPoint,point:previewPosition.toArray()}:savedPoint;if(at){marker([at],'ring',sceneColors.selected,16);try{const v=new THREE.Vector3(...unit(d.values));if(d.frame==='Setup')v.transformDirection(new THREE.Matrix4().set(...p.transform).invert());root.add(createToolAxisPointGlyph(view,cut.id,at,v.toArray()));addAxisInfluenceGlyphs(view,d,root,v.toArray());}catch{}}
  vp.scene.add(root);view.fiveAxisPointPreview={key:signature,group:root,intentId:cut.id,influence:!!d.influence,anchor:root.children.some(o=>o.userData.pointPreview)?at?.anchor:null};
}

export async function flushToolAxisPointEdits(context,view,ops){
  const d=view.fiveAxisPointEdit;if(!d||!d.dirty&&!d.request)return;
  if(selected(view)?.id!==d.id)throw Error('请选回正在编辑的刀路并应用局部刀轴。');
  await toolAxisPointCommand(context,view,'apply',ops);if(d.dirty||d.request)throw Error(d.error||'局部刀轴尚未保存。');
}

export async function reloadReversedPointDraft(context,view,nextView,ids){
  const d=view.fiveAxisPointEdit;if(!d||!ids.includes(d.id))return;
  const cut=nextView.scene.domain.intentToolpaths.find(c=>c.id===d.id),point=row(d);if(!cut||!point)return;
  const expected=nextView.scene.domain.expected;
  const [list,axis]=await Promise.all([context.sceneProxy.invoke('CamCut.ListToolAxisPoints',{expected,intentId:d.id}),context.sceneProxy.invoke('CamCut.ReadToolAxis',{expected,intentId:d.id,anchor:point.anchor})]);
  if(view.fiveAxisPointEdit!==d)return;
  d.key=key(nextView,cut);d.points=list.points;d.rotation=null;d.stale=false;populate(d,axis);
}
