import {syncIndependentHead,paintIndependentHead,clearIndependentHead} from './independentHeadPlayback.mjs';
import {floatingDialogStyle} from './floatingDialogs.mjs';
import {machineDefinitionThumbnail} from './machineDefinitionThumbnails.mjs';
import {renderMachineIK,machineIKField,flushMachinePoseEdits} from './machineIK.mjs';
import {renderMachineAssemblyPlacement,machineAssemblyCommand} from './machineAssemblyPlacement.mjs';
import {planMachineMotion,cancelMachinePlan,loadSavedMachinePlan,machinePlanningActivity} from './machineMotionPlan.mjs';
import {renderMachinePlanRecovery,machinePlanRecoveryCommand,disposeMachinePlanRecovery} from './machinePlanRecovery.mjs';
import {saveEditedProject} from './projectEditPersistence.mjs';
import {queueMachineParameterEdit,flushMachineParameterEdits} from './machineParameterEdits.mjs';
import {activeTool,selectTool} from './workspaceModel.mjs';
import {camMachinePlayback,machinePlaybackActive,machineViewportActive,machineViewportFrame,camHeadElements,machineMounts} from './machinePlaybackContext.mjs';
import {refreshDefinitionHistory,definitionHistoryState} from './machineDefinitionHistory.mjs';
import {renderMachineColliders,syncMachineColliders,machineColliderField,machineColliderCommand,scheduleMachineColliderSave,flushMachineColliderSaves} from './machineColliders.mjs';
import {machineForward as forward} from './machineKinematics.mjs';
import {renderMachineTree,toggleMachineTree} from './machineTree.mjs';
import {selectMachinePart,syncMachinePartSelection} from './machineSelection.mjs';
import {renderMachineObservation,machineObservationField,syncMachineObservation,paintMachineTrail} from './machineObservation.mjs';
import {renderMachineProgram,paintMachineProgram,machineProgramPage,machineProgramCurrent} from './machineProgramView.mjs';
import {advanceMachinePlayback} from './machinePlaybackTiming.mjs';
import {machinePlaybackJoints} from './machineTrajectory.mjs';
import {hydrateMachineProgram} from './machinePlanResource.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {button,section,hint,facts} from './workspaceControls.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {renderMachineCollisionFeedback,setMachineCollisionFeedback,machineCollisionFeedbackGeometrySignature} from './machineCollisionFeedback.mjs';

const setupOf=view=>view.scene?.domain?.setups?.find(x=>x.id===view.scene.domain.activeSetupId);
const safeReport=s=>s.report?.playbackAllowed===true&&s.report?.placementPreview!==true&&s.report?.validity==='Current'&&!s.dirty&&!s.postDirty;
const exportReady=s=>safeReport(s)&&s.report?.status==='Ready'&&s.report?.collisionStatus==='Clear';
const endFrame=s=>Number(s.report?.alarmFrame??Math.max(0,(s.report?.frames?.length||1)-1));
const machineId=m=>m?.entityId||m?.id||'';
const collisionWitness=report=>report?.collisionJoints?report:report?.rejectedMotion?.collisionJoints?report.rejectedMotion:null;
const collisionRepresentationLabel=value=>({'MachineBoxes; WorkpieceFixtureOwnMesh':'机床碰撞盒 · 工件夹具网格'})[value]||'未报告检测模型';
export const machineState=view=>view.fiveMachine??={cursor:0,speed:1,playing:false,dirty:false,originals:new Map()};
export const machinePlaybackReady=view=>safeReport(machineState(view));
const machineOf=view=>view.scene?.machines?.find(m=>machineId(m)===(machineState(view).machineId||setupOf(view)?.machineSetup?.machineId));
export const selectedMachineDefinition=view=>view.scene?.machineDefinitions?.find(d=>d.id===view.fiveMachine?.definitionId)||view.scene?.machineDefinitions?.[0];
const matrix=values=>new THREE.Matrix4().set(...values);
const row=m=>[0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15].map(i=>m.elements[i]);
const identity=()=>row(new THREE.Matrix4());
const postValueKey=value=>JSON.stringify(value,(_,entry)=>entry&&typeof entry==='object'&&!Array.isArray(entry)?Object.fromEntries(Object.keys(entry).sort().map(key=>[key,entry[key]])):entry);
const samePostValue=(a,b)=>postValueKey(a)===postValueKey(b);
function displayedJoint(s,joint){
  const index=s.report?.nodes?.findIndex(n=>n.id===joint.entityId)??-1;
  if(index<0||!safeReport(s))return Number(joint.position||0);
  const frame=Math.floor(s.cursor),a=s.report.frames[frame],b=s.report.frames[Math.min(frame+1,s.report.frames.length-1)];
  return a?machinePlaybackJoints(s.report.frames,s.cursor)[index]:Number(joint.position||0);
}
function configurationDraft(s,c){
  s.toolId=c.toolId||'';s.supportId=c.supportId||'';s.toolCollisionRepresentation='Boxes';
  const m=matrix(c.placement||identity()),position=new THREE.Vector3(),rotation=new THREE.Quaternion(),scale=new THREE.Vector3();m.decompose(position,rotation,scale);
  const e=new THREE.Euler().setFromQuaternion(rotation,'ZYX');s.position=position.toArray().map(String);s.rotation=[e.x,e.y,e.z].map(v=>String(v*180/Math.PI));s.tcp=(c.tcp||[0,0,0]).map(String);s.beam=(c.beam||[0,0,-1]).map(String);
}
export function acceptMachineAssemblyPlacement(view,configuration,expected){
  const s=machineState(view),setup=setupOf(view),snapshot=s.snapshot;
  stopMachine(view,true);
  setup.machineSetup=configuration;view.scene.domain.expected=expected;
  configurationDraft(s,configuration);
  s.setupKey=JSON.stringify([setup.id,configuration]);s.revision=JSON.stringify(expected);
  s.report=null;s.program=null;s.programReport=null;s.dirty=false;s.error='';s.alarm=false;
  // Reuse the unchanged machine hierarchy and meshes. This snapshot can draw
  // the new placement, but its former motion/collision identity is retired.
  s.snapshot=snapshot?{...snapshot,configuration,modelKey:''}:null;
  s.snapshotKey=s.snapshot?JSON.stringify([s.setupKey,view.scene?.machines]):null;
}
function draft(view){
  const s=machineState(view),setup=setupOf(view),key=JSON.stringify([setup?.id,setup?.machineSetup,...(setup?.headModel?.motion?[setup.headModel.motion]:[])]);
  if(s.setupKey!==key||camMachinePlayback(view)&&setup?.machineSetup?.machineId&&s.machineId!==setup.machineSetup.machineId){stopMachine(view,true);s.setupKey=key;s.snapshotKey=null;s.report=null;s.snapshot=null;s.error='';s.alarm=false;s.dirty=false;
    const c=setup?.machineSetup||{};s.machineId=c.machineId||(camMachinePlayback(view)&&setup?.headModel?.motion?'':machineId(view.scene?.machines?.[0]));configurationDraft(s,c);
  }
  return s;
}
function options(items,value,placeholder){return `<option value="">${esc(placeholder)}</option>`+items.map(([id,label])=>`<option value="${attr(id)}" ${value===id?'selected':''}>${esc(label)}</option>`).join('');}
const field=(id,label,value,disabled='',step=null)=>`<label class="five-machine-field" data-five-key="machine-${attr(id)}"><span>${esc(label)}</span><input type="${step===null?'text':'number'}" ${step===null?'':`step="${attr(step)}"`} inputmode="decimal" aria-label="${attr(label)}" data-five-machine-field="${attr(id)}" data-five-key="machine-input-${attr(id)}" value="${attr(value)}" ${disabled}></label>`;
function jogField(id,label,value,busy,step){
  const arrow=(direction,symbol,title)=>button(`five.machine.jog-${direction}:${id}`,symbol,`${busy} class="five-machine-jog-arrow" data-five-key="jog-${direction}-${attr(id)}" aria-label="${attr(label+' '+title)}" title="${attr(title+'；短按一步，长按连续')}"`);
  return `<div class="five-machine-field" data-five-key="machine-joint:${attr(id)}"><span>${esc(label)}</span><span class="five-machine-jog-input" data-five-key="jog-input-${attr(id)}"><input type="number" step="${attr(step)}" inputmode="decimal" aria-label="${attr(label)}" data-five-machine-field="joint:${attr(id)}" data-five-key="machine-input-joint:${attr(id)}" value="${attr(value)}" ${busy}><span class="five-machine-jog-arrows" data-five-key="jog-arrows-${attr(id)}">${arrow('plus','▴','增加')}${arrow('minus','▾','减少')}</span></span></div>`;
}
function renderElementPosition(view,m,busy){
  const id=view.fiveMachineSelectedPartId,element=m?.elements?.find(e=>e.entityId===id&&e.kind!=='machine');
  if(!element||!Array.isArray(element.restPosition)||element.restPosition.length!==3)return '';
  const joint=m.joints?.find(j=>j.entityId===id),title=['revolute','continuous'].includes(joint?.type)?'轴心位置':'安装位置';
  const values=machineState(view).originDraft?.[id]??element.restPosition;
  return section('machine-origin',title+' · mm',hint('相对父部件的零位位置。')+`<div class="five-machine-vector" data-five-key="machine-origin-fields-${attr(id)}">${values.map((value,index)=>{const axis='XYZ'[index];return `<label class="five-machine-field" data-five-key="machine-origin-${attr(id)}-${axis}"><span>${axis}</span><input type="text" inputmode="decimal" aria-label="${title} ${axis}" data-five-machine-field="origin:${axis.toLowerCase()}" data-five-key="machine-origin-input-${attr(id)}-${axis}" value="${attr(value)}" ${busy}></label>`;}).join('')}</div>`);
}
function invalidateMachineMotion(view){
  const s=machineState(view);stopMachine(view,true);s.report=null;s.snapshot=null;s.snapshotKey=null;s.program=null;s.programReport=null;s.cursor=0;s.alarm=false;
}
export function renderMachineLeft(view){
  const s=draft(view),busy=view.pending||s.busy&&!s.poseMoving?'disabled':'',machines=view.scene?.machines||[],mode=activeTool(view);
  const selected=machineOf(view),enabled=selected?.enabled!==false,toggleBusy=view.pending||s.busy||view.fiveMachineJog?.held||view.fiveMachineJog?.running||view.fiveMachineIK?.gesture||view.fiveMachineIK?.inflight;
  const list=section('machine-instances','机床实例',`<div class="five-machine-instance-picker" data-five-key="machine-instance-picker"><select class="five-machine-instance-select" aria-label="机床实例" data-five-machine-instance data-five-key="machine-instance-select" data-model-value-authoritative="true" ${view.pending||s.busy&&!s.selecting||!machines.length?'disabled':''}>${!machines.some(m=>machineId(m)===s.machineId)?'<option value="">请选择机床实例</option>':''}${machines.map(m=>`<option value="${attr(machineId(m))}" ${s.machineId===machineId(m)?'selected':''}>${esc(m.name||m.modelName||machineId(m))}${m.enabled===false?'（已禁用）':''}</option>`).join('')}</select>${selected?button('five.machine.instance-toggle',enabled?'已启用':'已禁用',`class="five-machine-instance-status" data-five-key="machine-instance-status" role="switch" aria-label="机床实例启用状态" aria-checked="${enabled}" title="点击${enabled?'禁用':'启用'}实例" ${toggleBusy?'disabled':''}`):''}</div>`+(!machines.length?hint('点击功能区“创建实例”，选择已启用的机床定义。'):''));
  if(mode==='machine-instances')return list+section('machine-tree','机械结构',renderMachineTree(view,machineOf(view),busy));
  return section('machine-jobs','作业 · 工件与装夹',(view.scene?.domain?.placements||[]).map(p=>{const d=view.scene.domain,w=d.workpieces.find(w=>w.id===p.workpieceId),setup=d.setups.find(x=>x.id===p.setupId);return button('five.machine.job:'+p.id,`${w?.name||'工件'} · ${setup?.name||'装夹'}`,`${busy} aria-pressed="${p.setupId===d.activeSetupId}"`);}).join('')||hint('请先在工件页导入工件并生成刀路。'))+list+section('machine-workpiece','当前作业',facts([['装夹',setupOf(view)?.name||'未选择'],['后置',machineOf(view)?.postprocessor?.name||'请在机床实例中配置']])+button('five.machine.fit','整机视角',busy)+button('five.area.workpiece','工件与装夹'));
}
export function renderMachineDefinitionManager(view){
  if(!view.fiveMachineDefinitionsOpen||view.fiveAxisArea!=='simulation')return '';
  const s=machineState(view),definitions=view.scene?.machineDefinitions||[],selected=selectedMachineDefinition(view);
  const waiting=view.pending||s.busy||definitionHistoryState().blocked,busy=waiting?'disabled':'';
  const selectedBusy=waiting||!selected?'disabled':'',canCreate=!waiting&&selected&&selected.enabled!==false;
  const rows=definitions.map(d=>{
    const enabled=s.definitionEnabledPending?.id===d.id?s.definitionEnabledPending.enabled:d.enabled!==false;
    return `<div role="listitem" class="five-machine-definition-row" data-five-key="template-${attr(d.id)}" data-selected="${selected?.id===d.id}"><button type="button" class="five-machine-definition-select" data-five-key="definition-select-${attr(d.id)}" data-five-command="five.machine.definition-select:${attr(d.id)}" aria-pressed="${selected?.id===d.id}" aria-disabled="${!!waiting}">${machineDefinitionThumbnail(d)}<span class="five-machine-definition-name"><strong>${esc(d.name||d.modelName||'未命名机床')}</strong><small>${esc(d.format||d.descriptionFormat||'')}</small></span></button><button type="button" role="switch" class="five-machine-definition-status" data-five-key="definition-status-${attr(d.id)}" data-five-command="five.machine.definition-toggle:${attr(d.id)}" aria-label="${attr(d.name||d.modelName)}启用状态" aria-checked="${enabled}" title="点击${enabled?'禁用':'启用'}" aria-disabled="${!!waiting}">${enabled?'已启用':'已禁用'}</button></div>`;
  }).join('');
  return `<div class="five-position-dialog five-floating-dialog five-machine-library" data-five-key="machine-definition-manager" data-five-floating="machine-definition-manager" ${floatingDialogStyle(view,'machine-definition-manager')} role="dialog" aria-label="机床定义管理"><header class="five-position-title" data-five-dialog-drag><strong>机床定义管理</strong>${button('five.machine.definitions-close','×',`aria-label="关闭机床定义管理" ${busy}`)}</header><div class="five-machine-library-toolbar">${button('five.machine.import','新增 / 导入',busy)}${button('five.machine.definition-toggle:'+(selected?.id||''),selected?.enabled===false?'启用':'禁用',selectedBusy)}${button('five.machine.definition-delete:'+(selected?.id||''),'删除',selectedBusy)}<span>${definitions.length} 个定义</span></div><div class="five-position-body five-machine-library-body" data-five-key="machine-definition-list"><div class="five-machine-definitions" role="list" aria-label="机床定义">${rows||hint('暂无机床定义，点击“新增 / 导入”添加 URDF 或 SDF 文件。')}</div></div><div class="five-machine-library-details" data-five-key="definition-properties">${selected?facts([['名称',selected.name||selected.modelName||'未命名机床'],['格式',selected.format||selected.descriptionFormat||'—']]):hint('请选择机床定义。')}${s.error?`<p role="alert" data-five-key="machine-definition-error">${esc(s.error)}</p>`:''}</div><footer class="five-position-actions"><small>定义用于创建实例；管理定义不修改已有项目实例。</small>${button('five.machine.instantiate:'+(selected?.id||''),'创建实例',canCreate?'class="five-primary"':'disabled')}${button('five.machine.definitions-close','关闭',busy)}</footer></div>`;
}
function postDraft(view){const s=draft(view),m=machineOf(view);if(!m)return null;const key=machineId(m),stored=JSON.stringify(m.postprocessor);if(s.postMachine!==key||!s.postDirty&&s.postStored!==stored){s.postMachine=key;s.postStored=stored;s.postDirty=false;s.post=structuredClone(m.postprocessor?.name?m.postprocessor:{name:'轴运动数据',format:'JointJSON',extension:'json',axes:(m.joints||[]).filter(j=>j.type!=='fixed').map((j,i)=>({jointId:j.entityId,word:/^[XYZABCUVW]$/.test(j.jointName)?j.jointName:'XYZABCUVW'[i],sign:1,offset:0})),header:'G21 G90 G93 G61',footer:'M30',processOn:'',processOff:'',motion:'N{line} G1 {axes} F{feed}',cutFeedMmMin:6000,travelFeedMmMin:6000});s.postBase=structuredClone(s.post);}return s.post;}
function renderPost(view,busy){const s=draft(view),p=postDraft(view);if(!p)return '';const edit=(key,label)=>field('post:'+key,label,p[key]??'',busy),text=(key,label)=>`<label class="five-machine-field" data-five-key="post-${key}"><span>${label}</span><textarea aria-label="${label}" data-five-machine-field="post:${key}" ${busy}>${esc(p[key])}</textarea></label>`;
  return section('machine-post','后置配置',edit('name','后置名称')+`<label class="five-machine-field"><span>输出格式</span><select aria-label="输出格式" data-five-machine-field="post:format"><option value="JointJSON" ${p.format==='JointJSON'?'selected':''}>轴运动数据 JSON</option><option value="InverseTimeNC" ${p.format==='InverseTimeNC'?'selected':''}>可配置 NC · 逆时间进给</option></select></label>`+edit('extension','文件扩展名')+edit('cutFeedMmMin','加工速度 · mm/min')+edit('travelFeedMmMin','接近与空移速度 · mm/min')+hint('加工与空移速度留空表示不限，机床轴的速度上限仍生效。')+(p.format==='InverseTimeNC'?text('header','程序头')+text('motion','运动行')+text('processOn','加工开启指令')+text('processOff','加工关闭指令')+text('footer','程序尾')+hint('通用 G1 后置无法保持连续五次轨迹，当前不能导出。请选择连续轴运动 JSON，或接入经过验证的控制器适配器。'):'')+(!machineOf(view)?.postprocessor?.name?hint('首次使用先采用当前配置，再规划机床运动。')+button('five.machine.post-save','采用当前后置配置',busy):'')+(!samePostValue({...p,axes:[]},{...s.postBase,axes:[]})?hint('正在保存后置…'):''));
}
function renderAxisMapping(view,busy){
  const m=machineOf(view),p=postDraft(view),id=view.fiveMachineSelectedPartId,j=m?.joints?.find(j=>j.entityId===id&&j.type!=='fixed'),a=p?.axes.find(a=>a.jointId===id);
  if(!j||!a)return '';
  const changed=!samePostValue(a,machineState(view).postBase.axes.find(a=>a.jointId===id));
  return section('machine-axis-mapping','逻辑轴映射',facts([['物理轴',j.jointName||j.name],['类型',j.type==='prismatic'?'直线轴':'旋转轴']])+field('axis:'+id+':word','逻辑轴字',a.word,busy)+field('axis:'+id+':sign','映射方向（1 或 -1）',a.sign,busy)+field('axis:'+id+':offset','输出零位偏置 · '+(j.type==='prismatic'?'mm':'°'),a.offset,busy)+field('axis:'+id+':maxAcceleration','加速度上限 · '+(j.type==='prismatic'?'mm/s²':'°/s²'),a.maxAcceleration??'',busy)+field('axis:'+id+':maxJerk','加加速度上限 · '+(j.type==='prismatic'?'mm/s³':'°/s³'),a.maxJerk??'',busy)+hint('加速度与加加速度上限留空表示不限；已填写的上限继续生效。')+(changed?hint('正在保存轴参数…'):''));
}
export function renderMachineRight(view){
  // A read-only scene refresh must not blur an in-progress placement edit.
  const s=draft(view),m=machineOf(view),busy=s.busy&&!s.poseMoving&&!s.savingPost?'disabled':'',elements=m?.elements||[];
  const links=elements.filter(e=>e.kind!=='machine').map(e=>[e.entityId,e.name||e.label||e.entityId]);
  const vector=(key,label)=>`<div class="five-machine-vector" data-five-key="machine-vector-${key}"><strong>${label}</strong>${s[key].map((v,i)=>field(key+i,'XYZ'[i],v,busy)).join('')}</div>`;
  const select=(key,label)=>`<label class="five-machine-field" data-five-key="machine-${key}"><span>${label}</span><select aria-label="${label}" data-five-machine-field="${key}" ${busy}>${options(links,s[key],'请选择部件')}</select></label>`;
  const collisionChoice=`<div class="five-machine-field" data-five-key="machine-tool-collision-representation"><span>刀头碰撞模型</span><span>机床碰撞盒</span></div>${hint('刀头直接采用机床实例中已启用的碰撞盒；编辑盒体的位置、姿态与尺寸会改变检测范围。工件与夹具按自身网格检测。')}`;
  const selectedJoints=(m?.joints||[]).filter(j=>j.type!=='fixed'&&(activeTool(view)!=='machine-instances'||j.entityId===view.fiveMachineSelectedPartId));
  const joints=section('machine-joints',activeTool(view)==='machine-instances'?'物理轴':'运动轴',selectedJoints.length?`<label class="five-machine-field"><span>点动步长 · mm / °</span><select aria-label="点动步长" data-five-machine-field="jogStep">${[.1,1,10].map(v=>`<option ${Number(s.jogStep||1)===v?'selected':''}>${v}</option>`).join('')}</select></label>`+hint(activeTool(view)==='machine-instances'?'输入关节值，或用输入框上下箭头按步长调整。':'输入框上下箭头：短按一步，长按连续；松开或移出停止。')+selectedJoints.map(j=>{
    const factor=j.type==='prismatic'?1:180/Math.PI,id=j.entityId,instance=activeTool(view)==='machine-instances',label=`${j.jointName||j.name} · ${j.type==='prismatic'?'mm':'°'}`,value=s.jointDraft?.[id]??Number(((instance?j.position:displayedJoint(s,j))*factor).toFixed(3)),step=Number(s.jogStep||1);
    return `<div class="five-machine-joint" data-five-key="joint-${attr(id)}">${instance?field('joint:'+id,label,value,busy,step):jogField(id,label,value,busy,step)}${instance?field('lower:'+id,'下限',s.limitDraft?.['lower:'+id]??Number((j.lower*factor).toFixed(3)),busy)+field('upper:'+id,'上限',s.limitDraft?.['upper:'+id]??Number((j.upper*factor).toFixed(3)),busy):`<small>行程 ${Number(j.lower*factor).toFixed(2)} ～ ${Number(j.upper*factor).toFixed(2)}</small>`}</div>`;
  }).join('')+(activeTool(view)==='machine-instances'?'':button('five.machine.home','回到初始位',busy)):hint(view.fiveMachineSelectedPartId?'此部件没有独立运动关节。':'选择部件后显示对应关节。'));
  if(activeTool(view)==='machine-instances'){
    const selected=elements.some(e=>e.entityId===view.fiveMachineSelectedPartId&&e.kind!=='machine');
    return renderMachineCollisionFeedback(view)+(selected?renderElementPosition(view,m,busy)+renderMachineIK(view)+joints+renderAxisMapping(view,busy)+renderMachineColliders(view,m,busy):section('instance-state','实例参数',m?facts([['名称',m.name],['状态',m.enabled===false?'禁用':'启用']]):'')+renderMachineObservation(view)+renderPost(view,busy))+(s.error?`<div role="alert">${esc(s.error)}</div>`:'');
  }
  return renderMachineCollisionFeedback(view)+section('machine-binding','机床与工件摆放',m?`${button('five.machine.mount','放到工作台',`${busy||m.enabled===false?'disabled':'class="five-primary"'}`)}${hint('工件与启用的夹具作为整体放上工作台，采用机床模板声明的刀具与装夹端点。')}${s.mounting&&s.mountingSetupId===setupOf(view)?.id?facts([['装夹组合',`${Number(s.mounting.setupBounds.max[0]-s.mounting.setupBounds.min[0]).toFixed(1)} × ${Number(s.mounting.setupBounds.max[1]-s.mounting.setupBounds.min[1]).toFixed(1)} × ${Number(s.mounting.setupBounds.max[2]-s.mounting.setupBounds.min[2]).toFixed(1)} mm`],['台面高度',`${Number(s.mounting.surfaceZMm).toFixed(1)} mm`]]):''}${select('supportId','工作台／装夹部件')}${select('toolId','刀具部件')}${collisionChoice}${renderMachineAssemblyPlacement(view,busy)}${vector('position','组合坐标原点 · mm')}${vector('rotation','组合姿态 · °')}${vector('tcp','刀具 TCP · mm')}${vector('beam','光束方向')}${facts([['喷嘴离板',`${Number(setupOf(view)?.rapidSettings?.followHeightMm??0).toFixed(2)} mm（空移设置）`]])}${hint('组合位置相对工作台；TCP 与光束方向相对刀具部件。模板物理 TCP 尚未实机标定。')}${s.dirty?hint('正在保存摆放与标定…'):''}`:hint('请先创建或选择机床实例。'))
    +renderMachineObservation(view)+joints
    +section('machine-report','仿真结果',s.report?facts([['运动轨迹',s.report.kinematicsStatus==='Ready'?'已规划':'尚未找到可用运动'],['轨迹点',`${s.report.frames.length} / ${s.report.sampleCount}`],['最大位置残差',`${Number(s.report.positionErrorMm).toFixed(4)} mm`],['检测模型',collisionRepresentationLabel(s.report.collisionRepresentation)],['碰撞检查',({Clear:'连续间隙检查通过',Blocked:s.alarm?'碰撞报警，已停止':'候选运动发生干涉，禁止播放',Unchecked:'未完成，禁止播放'})[s.report.collisionStatus]||'未检查']])+hint(s.report.reason||'轴运动已通过连续碰撞检查。轨迹精度采用自适应采样核对，时间按轴速度上限计算。'):hint('摆放工件并完成标定后，规划机床轴运动。'))
    +(s.error?`<div class="five-position-error" role="alert" data-five-key="machine-error">${esc(s.error)}</div>`:'');
}
export function renderMachineDock(view){
  const s=draft(view),ready=safeReport(s),disabled=view.pending||s.busy?'disabled':'',blocked=disabled||!ready||view.fiveJobPlayback?.boundary?'disabled':'';
  if(!machinePlaybackActive(view))return `<div class="five-dock-head"><strong>机床实例</strong><span>行程与点动 · 后置配置</span></div>`;
  const setup=setupOf(view),machine=machineOf(view),support=machine?.elements?.find(e=>e.entityId===setup?.machineSetup?.supportId);
  const mounts=machineMounts(view,setup?.machineSetup?.machineId).filter(p=>p.setupId===setup?.id);
  const binding=mounts.length?`${mounts.map(p=>p.name).join('、')} → ${support?.name||'装夹部件缺失'}`:'请先绑定工件与装夹部件';
  return `<div class="five-dock-head" data-five-key="machine-dock-head"><strong>${camMachinePlayback(view)?'CAM 加工仿真':'机床加工仿真'}</strong><span data-five-machine-binding title="${attr(binding)}">${esc(binding)} · 随装夹部件运动</span></div>
    ${!s.planning?(renderMachinePlanRecovery(view)||(s.error||s.report?.playbackAllowed===false?`<div class="five-position-error" role="alert" data-five-key="machine-plan-failure">${esc(s.error||s.report.reason||'运动规划未完成，没有可播放的轨迹。')}</div>`:!ready?hint('请先规划机床运动，完成轴行程与整机连续碰撞检查。'):'')):''}
    <div class="five-machine-playback" data-five-key="machine-playback">
    ${button('five.machine.build',s.planning?'正在规划…':'规划机床运动',disabled||!setup?.machineSetup?.machineId?'disabled':'')}${s.planning?`<span data-machine-plan-stage role="status">${esc(machinePlanningActivity(s))}</span>${button('five.machine.cancel-plan','取消规划')}`:''}
    ${button('five.machine.previous','|◀',`aria-label="上一步" ${blocked||s.cursor<=0?'disabled':''}`)}${button('five.machine.play','开始加工仿真',blocked||s.playing?'disabled':'')}${button('five.machine.pause','暂停',!s.playing?'disabled':'')}${button('five.machine.next','▶|',`aria-label="下一步" ${blocked||s.cursor>=endFrame(s)?'disabled':''}`)}${button('five.machine.reset','复位',disabled)}
    ${button('five.machine.program','查看运动指令',disabled||!exportReady(s)||!machine?.postprocessor?.name?'disabled':'')}${button('five.machine.export','导出运动指令',disabled||!exportReady(s)||!machine?.postprocessor?.name?'disabled':'')}${collisionWitness(s.report)?button('five.machine.inspect',s.report.collisionJoints?'查看干涉位置':'查看被拒绝候选干涉位置',disabled):''}
    <input type="range" aria-label="机床仿真进度" data-five-machine-field="seek" min="0" max="${endFrame(s)}" step="0.001" value="${s.cursor}" ${blocked}><span data-five-machine-progress>${s.report?.frames?.length?Math.floor(s.cursor)+1:0} / ${s.report?.frames?.length||0}</span><select aria-label="机床仿真速度" data-five-machine-field="speed">${[.25,.5,1,2,5,10].map(v=>`<option value="${v}" ${s.speed===v?'selected':''}>${v}×</option>`).join('')}</select></div>
    ${camMachinePlayback(view)?renderMachineObservation(view):''}${renderMachineProgram(s)}`;
}
export function renderCamMachinePlayback(view){
  const s=draft(view),ready=safeReport(s),busy=!!(view.pending||s.busy),boundary=!!view.fiveJobPlayback?.boundary,disabled=busy||!ready||boundary?'disabled':'';
  const step=(action,label,symbol,end)=>button('five.simulation.'+action,symbol,`aria-label="${label}" ${disabled||end?'disabled':''}`);
  return `<div class="five-dock-head" data-five-key="cam-head-simulation-title"><strong>切割头仿真</strong><span>工件 · 夹具 · 切割头机构</span></div>
    <div class="five-simulation-bar" data-five-key="simulation-bar" data-five-tool="simulation" aria-label="轨迹仿真控制">
    ${step('previous','上一步','|◀',s.cursor<=0)}${button('five.simulation.play','▶',`aria-label="播放" title="${ready?'播放切割头与工件的机构运动':'准备机构运动后播放'}" ${busy||s.playing||boundary?'disabled':''}`)}${button('five.simulation.pause','Ⅱ',`aria-label="暂停" ${!s.playing?'disabled':''}`)}${step('next','下一步','▶|',s.cursor>=endFrame(s))}${button('five.simulation.reset','复位',busy?'disabled':'')}
    <input type="range" min="0" max="${endFrame(s)}" step="any" value="${s.cursor}" data-five-machine-field="seek" aria-label="仿真进度" ${disabled}><span data-five-machine-progress>${s.report?.frames?.length?Math.floor(s.cursor)+1:0} / ${s.report?.frames?.length||0}</span>
    <select data-five-machine-field="speed" aria-label="预览速度">${[.25,.5,1,2,5,10].map(v=>`<option value="${v}" ${s.speed===v?'selected':''}>${v}×</option>`).join('')}</select>
    ${button('five.simulation.head-toggle',s.camShowHead===false?'显示切割头':'隐藏切割头',`aria-pressed="${s.camShowHead!==false}"`)}
    </div>${renderMachineObservation(view)}${s.planning?`<div class="five-simulation-status" data-five-key="cam-head-preparing"><span data-machine-plan-stage role="status">${esc(machinePlanningActivity(s))}</span>${button('five.machine.cancel-plan','取消')}</div>`:s.error?`<div class="five-position-error" role="alert" data-five-key="cam-head-playback-error">${esc(s.error)}</div>`:''}`;
}
export function stopMachine(view,restore=false){
  const s=machineState(view);s.playing=false;cancelAnimationFrame(s.animation);s.animation=0;s.last=0;
  if(restore){if(s.headGroup)s.headGroup.visible=false;s.inspect=false;for(const [object,original] of s.highlighted||[]){const tinted=Array.isArray(object.material)?object.material:[object.material];object.material=original;tinted.forEach(m=>m.dispose());}s.highlighted?.clear();for(const [object,original]of s.originals){object.matrix.copy(original.matrix);object.matrixAutoUpdate=original.auto;if(original.visible!==undefined)object.visible=original.visible;object.matrix.decompose(object.position,object.quaternion,object.scale);object.updateMatrixWorld(true);}s.originals.clear();}
}
function paint(view,snapshot,positions){
  const s=machineState(view),world=forward(snapshot,positions),objects=view.viewport?.sceneObjects;if(!objects)return;
  const head=camMachinePlayback(view)?new Set(camHeadElements(view).map(e=>e.entityId)):null;
  const apply=(id,transform)=>{const object=objects.get(id);if(!object)return;if(!s.originals.has(object))s.originals.set(object,{matrix:object.matrix.clone(),auto:object.matrixAutoUpdate,visible:object.visible});
    if(head?.has(id))object.visible=s.camShowHead!==false;
    object.matrixAutoUpdate=false;object.matrix.copy(transform);object.matrix.decompose(object.position,object.quaternion,object.scale);object.updateMatrixWorld(true);};
  snapshot.nodes.forEach((n,i)=>{if(!head||head.has(n.id))apply(n.id,world[i]);});
  if(snapshot.independentHead)paintIndependentHead(s,snapshot,world);
  const placement=world[snapshot.support].clone().multiply(matrix(snapshot.configuration.placement));
  for(const p of [...(view.scene?.domain?.placements||[]),...(view.scene?.domain?.fixtures||[])])if(p.setupId===setupOf(view)?.id)apply(p.id,placement.clone().multiply(matrix(p.transform)));
  return world;
}
function paintFrame(context,view){const s=machineState(view),r=s.report,f=r?.frames?.[Math.floor(s.cursor)];if(!f)return;
  const next=r.frames[Math.min(r.frames.length-1,Math.floor(s.cursor)+1)],fraction=s.cursor-Math.floor(s.cursor);
  const world=paint(view,r,machinePlaybackJoints(r.frames,s.cursor));
  if(world)paintMachineTrail(view,r,s.cursor,world);
  paintMachineProgram(context,s);
  const progress=context.mount.querySelector('[data-five-machine-progress]');if(progress)progress.textContent=`${Math.floor(s.cursor)+1} / ${r.frames.length}`;
  const seek=context.mount.querySelector('[data-five-machine-field="seek"]');if(seek)seek.value=s.cursor;
  for(const [action,disabled] of [['previous',s.cursor<=0],['next',s.cursor>=endFrame(s)]]){
    const button=context.mount.querySelector(`[data-five-command="five.${camMachinePlayback(view)?'simulation':'machine'}.${action}"]`);if(button)button.disabled=!!(disabled||view.pending||s.busy||view.fiveJobPlayback?.boundary||!safeReport(s));
  }
  for(const joint of machineOf(view)?.joints||[]){const input=context.mount.querySelector(`[data-five-machine-field="joint:${joint.entityId}"]`);
    if(input&&document.activeElement!==input)input.value=Number(displayedJoint(s,joint)*(joint.type==='prismatic'?1:180/Math.PI)).toFixed(3);}
}
export function machineField(context,view,key,value,ops){
  if(view.fiveMachineAssembly?.gesture||view.fiveMachineAssembly?.saving)return;
  if(key.startsWith('origin:')){
    if(view.fiveMachineIK)view.fiveMachineIK.hud=null;
    const s=draft(view),m=machineOf(view),id=view.fiveMachineSelectedPartId,element=m?.elements?.find(e=>e.entityId===id&&e.kind!=='machine'),index='xyz'.indexOf(key.slice(7));
    if(!element||!Array.isArray(element.restPosition)||element.restPosition.length!==3||index<0||key.length!==8)return;
    s.originDraft??={};const values=s.originDraft[id]??=[...element.restPosition].map(String);values[index]=value;invalidateMachineMotion(view);
    queueMachineParameterEdit(context,view,ops,'origin:'+id,()=>{
      const position=values.map(v=>{if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('请输入有效的部件位置。');return Number(v);});
      return {method:'Machine.SetElementPosition',request:{entityId:id,position}};
    },()=>{delete s.originDraft[id];invalidateMachineMotion(view);});return;
  }
  if(machineIKField(context,view,key,value,ops))return;
  if(machineObservationField(view,key,value)){syncMachineObservation(context,view);if(machineState(view).report)paintFrame(context,view);return;}
  if(machineColliderField(view,machineOf(view),key,value)){scheduleMachineColliderSave(context,view,ops,key.endsWith(':enabled')?0:300);ops.renderProject(context,view);return;}
  const s=draft(view);if(key==='seek'){if(!safeReport(s)||view.fiveJobPlayback?.boundary||!Number.isFinite(Number(value)))return;stopMachine(view,true);s.alarm=false;s.cursor=Math.max(0,Math.min(endFrame(s),Number(value)));paintFrame(context,view);if(s.cursor>=endFrame(s)&&s.report.alarmKind)raiseAlarm(context,view,ops);return;}
  if(key==='jogStep'){s.jogStep=Number(value);return;}
  if(key.startsWith('lower:')||key.startsWith('upper:')){
    s.limitDraft??={};s.limitDraft[key]=value;const id=key.slice(key.indexOf(':')+1),j=machineOf(view)?.joints.find(j=>j.entityId===id),factor=j?.type==='prismatic'?1:Math.PI/180;
    queueMachineParameterEdit(context,view,ops,'limits:'+id,()=>{const n=k=>{const v=s.limitDraft[k+':'+id]??j[k]/factor;if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('请输入有效的行程上下限。');return Number(v)*factor;};return {method:'Machine.SetJointLimits',request:{entityId:id,lower:n('lower'),upper:n('upper')}};},()=>{delete s.limitDraft['lower:'+id];delete s.limitDraft['upper:'+id];s.report=null;});return;
  }
  if(key.startsWith('post:')||key.startsWith('axis:')){
    const p=postDraft(view);if(!p)return;stopMachine(view,true);s.report=null;s.postDirty=true;const parts=key.split(':');
    if(parts[0]==='axis'){const a=p.axes.find(a=>a.jointId===parts[1]);if(a&&a.jointId===view.fiveMachineSelectedPartId)a[parts[2]]=value;}
    else{p[parts[1]]=value;if(parts[1]==='format')p.extension=value==='JointJSON'?'json':'nc';}
    const id=s.machineId;
    queueMachineParameterEdit(context,view,ops,'post:'+id,()=>{
      const post=structuredClone(p),number=v=>{if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('请输入有效的后置参数。');return Number(v);};
      for(const k of ['cutFeedMmMin','travelFeedMmMin']){if(post[k]!==undefined&&String(post[k]).trim()!=='')post[k]=number(post[k]);else delete post[k];}for(const a of post.axes){a.sign=number(a.sign);a.offset=number(a.offset);for(const key of ['maxAcceleration','maxJerk']){if(a[key]!==undefined&&String(a[key]).trim()!=='')a[key]=number(a[key]);else delete a[key];}}
      return {method:'CamSimulation.ConfigureMachinePost',expected:true,request:{machineId:id,postprocessor:post}};
    },()=>{s.postDirty=false;s.postMachine=null;});return;
  }
  if(key==='speed'){s.speed=Number(value);return;}
  if(key.startsWith('joint:')){
    const id=key.slice(6),m=machineOf(view),j=m?.joints.find(j=>j.entityId===id),setup=setupOf(view),bound=setup?.machineSetup?.machineId===m?.entityId;
    // Native number arrows use the value attribute as their step base. Keep it
    // at the latest typed value so 85.21 +/- 1 remains 86.21/84.21, including
    // before the asynchronous native acknowledgement refreshes the panel.
    const input=context.mount.querySelector(`[data-five-machine-field="${key}"]`);
    if(input?.type==='number'&&String(value).trim()&&Number.isFinite(Number(value)))input.defaultValue=String(value);
    stopMachine(view,true);s.report=null;s.snapshot=null;s.snapshotKey=null;s.jointDraft??={};s.jointDraft[id]=value;
    queueMachineParameterEdit(context,view,ops,key,()=>{
      if(!j||!String(value).trim()||!Number.isFinite(Number(value)))throw Error('请输入有效的轴位置。');
      if(bound&&s.dirty)throw Error('请先完成工件摆放。');
      return {method:'CamSimulation.MoveMachine',expected:true,request:{...(bound?{setupId:setup.id}:{machineId:m.entityId}),positions:{[id]:Number(value)*(j.type==='prismatic'?1:Math.PI/180)}}};
    },()=>{delete s.jointDraft[id];});return;
  }
  stopMachine(view,true);s.dirty=true;s.report=null;s.snapshot=null;s.error='';
  if(key==='toolCollisionRepresentation')s.toolCollisionRepresentation='Boxes';else if(['toolId','supportId'].includes(key))s[key]=value;else {const group=key.slice(0,-1),index=Number(key.at(-1));if(s[group])s[group][index]=value;}
  const setupId=setupOf(view)?.id;
  queueMachineParameterEdit(context,view,ops,'setup:'+setupId,()=>{
    if(!setupId||!s.machineId||!s.toolId||!s.supportId)throw Error('请选择机床、工作台和刀具部件。');
    const numbers=k=>s[k].map(v=>{if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('位置、姿态与标定必须为有效数值。');return Number(v);});
    const placement=new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(...numbers('rotation').map(v=>v*Math.PI/180),'ZYX'));placement.setPosition(...numbers('position'));
    return {method:'CamSimulation.ConfigureMachine',expected:true,request:{commandId:crypto.randomUUID(),setupId,configuration:{machineId:s.machineId,supportId:s.supportId,toolId:s.toolId,toolCollisionRepresentation:'Boxes',placement:row(placement),tcp:numbers('tcp'),beam:numbers('beam')}}};
  },()=>{if(s.mountingDraft)return;s.dirty=false;s.setupKey=null;s.report=null;s.snapshotKey=null;});
}
async function loadSnapshot(context,view,ops){
  const s=draft(view);
  if(s.loading){await s.snapshotRead;if(!view.disposed&&context.isCurrentProject?.()!==false)return loadSnapshot(context,view,ops);return;}
  const setup=setupOf(view),independent=!!setup?.headModel?.motion&&!setup?.machineSetup?.machineId;
  if(s.dirty||!independent&&(!setup?.machineSetup?.machineId||s.machineId!==setup.machineSetup.machineId))return;
  const setupKey=s.setupKey,key=JSON.stringify([setupKey,view.scene?.machines]);if(s.snapshotKey===key){if(s.snapshot)await loadSavedMachinePlan(context,view,s,ops);return;}s.snapshotKey=key;s.loading=true;const proxy=context.sceneProxy;
  s.snapshotRead=(async()=>{
  try{const snapshot=await proxy.invoke('CamSimulation.ReadMachine',{setupId:setupOf(view).id});
    if(s.snapshotKey!==key||s.setupKey!==setupKey||view.disposed||context.isCurrentProject?.()===false)return;
    if(s.report&&s.report.modelKey!==snapshot.modelKey){stopMachine(view,true);s.report=null;}
    s.snapshot=snapshot;if(snapshot.independentHead)syncIndependentHead(context,view,s,setup.headModel,()=>{if(!machinePlaybackActive(view)||view.fiveViewportFrame!==machineViewportFrame(view)||!s.snapshot?.independentHead)return;if(s.inspect&&collisionWitness(s.report))paint(view,s.report,collisionWitness(s.report).collisionJoints);else if(safeReport(s))paintFrame(context,view);else paint(view,s.snapshot,s.snapshot.positions);});if(machinePlaybackActive(view)&&view.fiveViewportFrame===machineViewportFrame(view)&&!s.report)paint(view,snapshot,snapshot.positions);
    await loadSavedMachinePlan(context,view,s,ops);
  }catch(error){if(s.snapshotKey===key&&s.setupKey===setupKey&&!view.disposed&&context.isCurrentProject?.()!==false){s.error=error.message;s.snapshot=null;ops.renderProject(context,view);}}finally{s.loading=false;}
  })();
  return s.snapshotRead;
}
export async function prepareMachinePlayback(context,view,ops){
  await loadSnapshot(context,view,ops);
  if(!view.disposed&&context.isCurrentProject?.()!==false&&machinePlaybackActive(view))await ops.refreshActiveAreaView?.(context,view,{});
  return !view.disposed&&context.isCurrentProject?.()!==false&&machinePlaybackActive(view)&&safeReport(machineState(view));
}
export function syncMachine(context,view,ops){
  syncMachineObservation(context,view);
  syncMachineColliders(view,machineOf(view),view.fiveAxisArea==='simulation'&&activeTool(view)==='machine-instances');
  syncMachinePartSelection(view);
  const s=draft(view);if(!machineViewportActive(view)){stopMachine(view,true);clearIndependentHead(view);if(s.visible){view.viewport?.setDisplayMode?.(s.previousDisplayMode||'shadedEdges');view.viewport?.setGridVisible?.(s.previousGridVisible);s.visible=false;}return;}
  if(!s.visible){s.visible=true;s.previousDisplayMode=view.viewport?.displayMode;s.previousGridVisible=view.viewport?.getGridState?.().visible!==false;view.viewport?.setGridVisible?.(false);view.viewport?.setDisplayMode?.('shaded');
    if(view.scene?.machines?.length||view.scene?.domain?.placements?.length)void ops.fitViewAfterRenderPublish(context,view).catch(error=>{s.error=error.message;});}
  const revision=JSON.stringify(view.scene?.domain?.expected);if(s.revision!==revision){stopMachine(view,true);s.report=null;s.snapshotKey=null;s.revision=revision;}
  // Instance motion is authoritative native Transform motion. Its parent chain
  // already propagates incremental world matrices to the viewport.
  if(!machinePlaybackActive(view)){stopMachine(view,true);s.cursor=0;return;}
  if(view.fiveMachineAssembly?.gesture||view.fiveMachineAssembly?.saving)return;
  void loadSnapshot(context,view,ops);
  if(view.fiveViewportFrame!==machineViewportFrame(view))return;
  if(s.alarm&&s.report?.alarmJoints)paint(view,s.report,s.report.alarmJoints);else if(s.inspect&&collisionWitness(s.report))paint(view,s.report,collisionWitness(s.report).collisionJoints);else if(safeReport(s))paintFrame(context,view);else if(s.snapshot&&!s.dirty)paint(view,s.snapshot,s.snapshot.positions);
}
function raiseAlarm(context,view,ops){const s=machineState(view);stopMachine(view,true);s.alarm=true;s.error=s.report.reason;s.cursor=endFrame(s);paint(view,s.report,s.report.alarmJoints);s.highlighted??=new Map();for(const id of s.report.collisionBodies||[]){view.viewport?.sceneObjects?.get(id)?.traverse(part=>{if(!part.material)return;s.highlighted.set(part,part.material);const tint=m=>{const c=m.clone();c.color?.set('#ef4747');return c;};part.material=Array.isArray(part.material)?part.material.map(tint):tint(part.material);});}ops.renderProject(context,view);}
export async function machineCommand(context,view,action,ops){
  if(action.startsWith('recovery:')){await machinePlanRecoveryCommand(context,view,action.slice(9),ops,{replan:()=>machineCommand(context,view,'build',ops)});return;}
  if(action.startsWith('assembly:')||['build','play','mount','apply','reset','home'].includes(action)||/^(select:|job:)/.test(action))disposeMachinePlanRecovery(view);
  if(machineAssemblyCommand(context,view,action,ops))return;
  if(view.fiveMachineAssembly?.gesture||view.fiveMachineAssembly?.saving)return;
  if(action==='definitions-open'){view.fiveMachineDefinitionsOpen=true;ops.renderProject(context,view);return;}
  if(action==='definitions-close'){if(view.pending||machineState(view).busy)return;view.fiveMachineDefinitionsOpen=false;ops.renderProject(context,view);return;}
  if(action==='cancel-plan'){await cancelMachinePlan(context,machineState(view));return;}
  if(/^(select:|job:|part:|jog-minus:|jog-plus:)|^(build|play|export|program|instance-toggle)$/.test(action))await flushMachineParameterEdits(context,view,ops);
  if(action.startsWith('tree-toggle:')){toggleMachineTree(context,view,machineOf(view),action.slice(12));return;}
  const s=draft(view),selecting=action.startsWith('select:'),instantiateArea=action.startsWith('instantiate:')?(view.activeAreaId||view.fiveAxisArea):null;
  if(selecting&&s.selecting){s.nextMachineId=action.slice(7);return;}
  if(s.busy||view.pending)return;
  if(action==='program-close'){s.programOpen=false;ops.renderProject(context,view);return;}
  if(action.startsWith('program-')){
    if(!safeReport(s)||!machineProgramCurrent(s))return;
    if(action.startsWith('program-seek:')){stopMachine(view,true);s.alarm=false;s.cursor=Math.max(0,Math.min(endFrame(s),Number(action.slice(13))));paintFrame(context,view);}
    else {machineProgramPage(s,action.slice(8));ops.renderProject(context,view);}
    return;
  }
  if(action.startsWith('definition-select:')){s.definitionId=action.slice(18);ops.renderProject(context,view);return;}
  if((action==='import'||action.startsWith('definition-'))&&definitionHistoryState().mutationPending)return;
  if(selecting&&(!view.scene?.machines?.some(m=>machineId(m)===action.slice(7))||s.machineId===action.slice(7)))return;
  if(action.startsWith('part:')){selectMachinePart(context,view,action.slice(5),ops);return;}
  if(action.startsWith('box-')){s.error='';try{await machineColliderCommand(context,view,machineOf(view),action,ops);}catch(e){s.error=e.message;}finally{ops.renderProject(context,view);}return;}
  const jointText=action.startsWith('joint:')?context.mount.querySelector(`[data-five-machine-field="${action}"]`)?.value?.trim():null;
  const limitText=action.startsWith('limits:')?['lower','upper'].map(k=>context.mount.querySelector(`[data-five-machine-field="${k}:${action.slice(7)}"]`)?.value?.trim()):null;
  if(action==='fit'){view.viewport?.setViewDirection?.([1,-1.5,.8]);await ops.fitViewAfterRenderPublish(context,view);view.viewport?.fitViewToViewport?.(1);return;}
  if(action==='inspect'){const witness=collisionWitness(s.report);if(!witness)return;stopMachine(view,true);s.inspect=true;paint(view,s.report,witness.collisionJoints);s.highlighted??=new Map();for(const id of witness.collisionBodies||[]){const object=view.viewport?.sceneObjects?.get(id);object?.traverse(part=>{if(!part.material)return;s.highlighted.set(part,part.material);const tint=m=>{const c=m.clone();c.color?.set('#ef4747');c.emissive?.set('#440000');return c;};part.material=Array.isArray(part.material)?part.material.map(tint):tint(part.material);});}ops.renderProject(context,view);return;}
  if(action==='pause'){stopMachine(view);ops.renderProject(context,view);return;}
  if(action==='head-toggle'&&camMachinePlayback(view)){s.camShowHead=s.camShowHead===false;ops.renderProject(context,view);return;}
  if(action==='reset'){stopMachine(view,true);s.alarm=false;s.error='';s.cursor=0;ops.renderProject(context,view);return;}
  if(['previous','next'].includes(action)){
    if(view.fiveJobPlayback?.boundary||!machinePlaybackActive(view)||!safeReport(s))return;
    stopMachine(view,true);s.alarm=false;s.cursor=Math.max(0,Math.min(endFrame(s),action==='next'?Math.floor(s.cursor)+1:Math.ceil(s.cursor)-1));paintFrame(context,view);
    if(s.cursor>=endFrame(s)&&s.report.alarmKind)raiseAlarm(context,view,ops);
    else if(action==='next'&&s.cursor>=s.report.frames.length-1)view.fiveJobPlayback?.onEnd?.();
    ops.renderProject(context,view);return;
  }
  if(action==='play'){
    if(view.fiveJobPlayback?.boundary||!machinePlaybackActive(view)||!await prepareMachinePlayback(context,view,ops)){s.error='当前装夹没有可播放的有效机床运动计划，请先规划机床运动。';ops.renderProject(context,view);return false;}
    stopMachine(view,true);if(s.cursor>=endFrame(s))s.cursor=0;s.alarm=false;s.error='';s.playing=true;s.last=0;ops.renderProject(context,view);
    const tick=time=>{if(!safeReport(s)||!s.playing||!machinePlaybackActive(view)||view.disposed||view.pending||context.isCurrentProject?.()===false||document.hidden){stopMachine(view);return;}
      const dt=s.last?Math.min(.1,(time-s.last)/1000)*s.speed:0;s.last=time;
      // Consume the certified native segment times, including adaptive sub-ms
      // segments. Controller acceleration remains outside this preview.
      try{advanceMachinePlayback(s,dt,endFrame(s));}
      catch(error){stopMachine(view);s.error=error.message;paintFrame(context,view);ops.renderProject(context,view);return;}
      s.cursor=Math.min(s.cursor,endFrame(s));paintFrame(context,view);if(s.cursor>=endFrame(s)&&s.report.alarmKind){raiseAlarm(context,view,ops);return;}if(s.cursor>=s.report.frames.length-1){stopMachine(view);view.fiveJobPlayback?.onEnd?.();ops.renderProject(context,view);}else s.animation=requestAnimationFrame(tick);};s.animation=requestAnimationFrame(tick);return true;
  }
  const poseOnly=action==='home'||/^(joint:|jog-minus:|jog-plus:)/.test(action);
  let mountConfiguration=null,mountSetupId=null,suspendedSetupEntry=null,mountSucceeded=false;
  if(action==='mount'){
    mountSetupId=setupOf(view)?.id;
    if(s.dirty&&(s.toolId||s.supportId)){
      const numbers=key=>s[key].map(v=>{if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('位置、姿态与标定必须为有效数字。');return Number(v);});
      try{
        if(!s.toolId||!s.supportId)throw Error('请选择机床、工作台和刀具部件。');
        mountConfiguration={machineId:s.machineId,supportId:s.supportId,toolId:s.toolId,toolCollisionRepresentation:'Boxes',placement:row(new THREE.Matrix4()),tcp:numbers('tcp'),beam:numbers('beam')};
      }catch(error){s.error=error.message;ops.appendProjectLog(context,'error',s.error);ops.renderProject(context,view);return;}
      s.mountingDraft=true;
    }
  }
  if(action.startsWith('definition-toggle:')){const id=action.slice(18),d=view.scene.machineDefinitions.find(d=>d.id===id);if(!d)return;s.definitionEnabledPending={id,enabled:d.enabled===false};}
  stopMachine(view,true);s.busy=true;s.selecting=selecting;s.poseMoving=poseOnly;s.savingPost=action==='post-save'||action.startsWith('axis-save:');s.error='';if(!poseOnly)ops.renderProject(context,view);
  try{
    if(poseOnly||['build','export','program','mount'].includes(action)){await flushMachinePoseEdits(context,view,ops);await flushMachineColliderSaves(context,view);}
    if(action==='mount'){
      const edits=view.fiveMachineParameterEdits;
      if(mountConfiguration){
        await edits?.request;
        const key='setup:'+mountSetupId,entry=edits?.entries.get(key);
        if(entry){clearTimeout(entry.timer);entry.timer=null;entry.ready=false;edits.entries.delete(key);suspendedSetupEntry={key,entry};}
      }
      await flushMachineParameterEdits(context,view,ops);
      if(view.disposed||context.isCurrentProject?.()===false||setupOf(view)?.id!==mountSetupId)throw Error('工程或装夹已切换，请重新放到工作台。');
    }
    const move=async positions=>{const setup=setupOf(view),bound=setup?.machineSetup?.machineId===s.machineId;if(s.dirty&&bound)throw Error('请先应用已修改的工件摆放。');const geometrySignature=machineCollisionFeedbackGeometrySignature(view,s.machineId),result=await context.sceneProxy.invoke('CamSimulation.MoveMachine',{expected:view.scene.domain.expected,...(bound?{setupId:setup.id}:{machineId:s.machineId}),positions},{timeoutMs:60000});setMachineCollisionFeedback(view,s.machineId,result,{geometrySignature});if(result.status!=='Ready'||result.collisionStatus!=='Clear')throw Error(result.reason||'尚未确认该移动安全。');acceptPositions(result.positions);};
    const acceptPositions=positions=>{
      for(const j of machineOf(view)?.joints||[])if(Object.hasOwn(positions,j.entityId))j.position=positions[j.entityId];
      s.report=null;s.snapshot=null;s.snapshotKey=null;
      for(const j of machineOf(view)?.joints||[]){const input=context.mount.querySelector(`[data-five-machine-field="joint:${j.entityId}"]`);if(input&&document.activeElement!==input)input.value=String(j.position*(j.type==='prismatic'?1:180/Math.PI));}
    };
    if(action.startsWith('definition-toggle:')){const id=action.slice(18),d=view.scene.machineDefinitions.find(d=>d.id===id);await context.productProxy.invoke('MachineDefinition.SetEnabled',{machineDefinitionId:id,enabled:d.enabled===false});}
    else if(action.startsWith('definition-delete:')){await context.productProxy.invoke('MachineDefinition.Delete',{machineDefinitionId:action.slice(18)});}
    else if(action==='instance-toggle'){await context.sceneProxy.invoke('Machine.SetEnabled',{machineEntityId:s.machineId,enabled:machineOf(view).enabled===false});s.report=null;}
    else if(action.startsWith('job:')){await context.sceneProxy.invoke('CamWorkpiece.Activate',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,placementId:action.slice(4)});s.report=null;}
    else if(action==='post-save'||action.startsWith('axis-save:')){
      const p=postDraft(view),axisOnly=action.startsWith('axis-save:'),id=action.slice(10);
      const post=structuredClone(axisOnly?s.postBase:{...p,axes:s.postBase.axes});
      if(axisOnly){const a=p.axes.find(a=>a.jointId===id),index=post.axes.findIndex(a=>a.jointId===id);if(!a||index<0||id!==view.fiveMachineSelectedPartId)throw Error('请先选择要保存的轴。');post.axes[index]=structuredClone(a);}
      const result=await context.sceneProxy.invoke('CamSimulation.ConfigureMachinePost',{expected:view.scene.domain.expected,machineId:s.machineId,postprocessor:post});
      s.postBase=structuredClone(result.postprocessor);s.postStored=JSON.stringify(result.postprocessor);machineOf(view).postprocessor=structuredClone(result.postprocessor);
      if(axisOnly){const index=p.axes.findIndex(a=>a.jointId===id);if(samePostValue(p.axes[index],post.axes.find(a=>a.jointId===id)))p.axes[index]=structuredClone(result.postprocessor.axes.find(a=>a.jointId===id));}else for(const key of Object.keys(result.postprocessor))if(key!=='axes'&&samePostValue(p[key],post[key]))p[key]=structuredClone(result.postprocessor[key]);
      s.postDirty=!samePostValue(s.post,s.postBase);s.report=null;s.snapshotKey=null;
    }
    else if(action==='program'){
      if(!exportReady(s))throw Error('请先完成当前机床与作业的仿真检查。');
      const report=s.report,setupId=setupOf(view).id,expected=structuredClone(view.scene.domain.expected);
      const metadata=await context.sceneProxy.invoke('CamSimulation.ExportMachineProgram',{expected,setupId,modelKey:report.modelKey},{timeoutMs:120000});
      if(view.disposed||context.isCurrentProject?.()===false)return;
      if(s.report!==report||setupOf(view)?.id!==setupId||JSON.stringify(view.scene.domain.expected)!==JSON.stringify(expected))throw Error('作业或运动计划已变化，请重新查看指令。');
      const program=await hydrateMachineProgram(metadata,context.sceneProxy,{active:()=>!view.disposed&&context.isCurrentProject?.()!==false&&s.report===report&&setupOf(view)?.id===setupId&&JSON.stringify(view.scene.domain.expected)===JSON.stringify(expected)});
      if(!program)return;
      s.program=program;s.programReport=report;s.programOpen=true;machineProgramPage(s,'current');
    }
    else if(action==='export'){if(!exportReady(s))throw Error('请先完成当前机床与作业的仿真检查。');const post=machineOf(view)?.postprocessor;if(!post?.name)throw Error('请在机床实例中保存后置。');const bridge=context.appProxy?.bridge??context.sceneProxy?.bridge;if(!bridge?.saveFileDialog)throw Error('当前宿主不支持导出文件。');const destination=await bridge.saveFileDialog({title:'导出机床运动指令',defaultPath:'加工程序.'+post.extension,defaultExtension:post.extension,filters:[{name:post.name,extensions:[post.extension]}]});if(!destination)return;const result=await context.sceneProxy.invoke('CamSimulation.ExportMachineProgram',{expected:view.scene.domain.expected,setupId:setupOf(view).id,modelKey:s.report.modelKey,destination},{timeoutMs:120000});ops.appendProjectLog(context,'info',`已导出 ${result.instructionCount} 条轴运动指令：${destination}`);}
    else if(action.startsWith('limits:')){const id=action.slice(7),j=machineOf(view)?.joints.find(j=>j.entityId===id),factor=j?.type==='prismatic'?1:Math.PI/180;if(!j||limitText.some(t=>!t||!Number.isFinite(Number(t))))throw Error('请输入有效的行程上下限。');await context.sceneProxy.invoke('Machine.SetJointLimits',{entityId:id,lower:Number(limitText[0])*factor,upper:Number(limitText[1])*factor});if(s.limitDraft){delete s.limitDraft['lower:'+id];delete s.limitDraft['upper:'+id];}s.report=null;s.snapshotKey=null;}
    else if(action.startsWith('jog-minus:')||action.startsWith('jog-plus:')){const id=action.slice(action.indexOf(':')+1),j=machineOf(view)?.joints.find(j=>j.entityId===id);const position=j.position+(action.startsWith('jog-minus:')?-1:1)*Number(s.jogStep||1)*(j.type==='prismatic'?1:Math.PI/180);await move({[id]:position});}
    else if(action==='import'){
      const bridge=context.appProxy?.bridge??context.productProxy?.bridge??context.sceneProxy?.bridge;
      const path=await bridge?.openFileDialog({title:'导入机床定义',filters:[{name:'URDF / SDF 机床',extensions:['urdf','sdf']},{name:'URDF 机床',extensions:['urdf']},{name:'SDF 机床',extensions:['sdf']}]});if(!path)return;
      const imported=await context.productProxy.invoke('MachineDefinition.Import',{sourcePath:path},{timeoutMs:60000});s.definitionId=imported.machineDefinitionId||imported.definition?.id||s.definitionId;
    }else if(action.startsWith('instantiate:')){
      const definition=view.scene?.machineDefinitions?.find(d=>d.id===action.slice(12));
      if(!definition||definition.enabled===false)throw Error('请选择已启用的机床定义。');
      const response=await context.sceneProxy.invoke('Machine.Instantiate',{machineDefinitionId:action.slice(12)},{timeoutMs:60000});s.machineId=machineId(response.machine);s.dirty=false;s.report=null;s.snapshot=null;view.fiveMachineDefinitionsOpen=false;selectTool(view,'machine-instances');
    }else if(action.startsWith('select:')){
      s.machineId=action.slice(7);const stored=setupOf(view)?.machineSetup,bound=stored?.machineId===s.machineId;
      configurationDraft(s,bound?stored:{});s.dirty=activeTool(view)==='machine-simulation'&&!bound;s.report=null;s.snapshot=null;s.mounting=null;s.snapshotKey=null;
    }
    else if(action.startsWith('joint:')){
      const id=action.slice(6),j=machineOf(view)?.joints?.find(j=>j.entityId===id),text=jointText,number=Number(text);
      if(!j||!text||!Number.isFinite(number))throw Error('请输入有效的轴位置。');
      await move({[id]:number*(j.type==='prismatic'?1:Math.PI/180)});
    }else if(action==='home'){await move(Object.fromEntries((machineOf(view)?.joints||[]).filter(j=>j.type!=='fixed').map(j=>[j.entityId,j.homePosition])));}
    else if(action==='mount'){
      if(!machineOf(view)||machineOf(view).enabled===false)throw Error('请选择已启用的机床实例。');
      const result=await context.sceneProxy.invoke('CamSimulation.MountMachine',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:setupOf(view).id,machineId:s.machineId,...(mountConfiguration?{configuration:mountConfiguration}:{})},{timeoutMs:120000});
      mountSucceeded=true;s.mounting=result.mounting;s.mountingSetupId=setupOf(view).id;s.dirty=false;s.setupKey=null;s.report=null;s.snapshot=null;s.snapshotKey=null;
    }else if(action==='apply'){
      const numbers=key=>s[key].map(v=>{if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('位置、姿态与标定必须为有效数字。');return Number(v);});
      const rotation=numbers('rotation').map(v=>v*Math.PI/180),placement=new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(...rotation,'ZYX'));placement.setPosition(...numbers('position'));
      await context.sceneProxy.invoke('CamSimulation.ConfigureMachine',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:setupOf(view).id,configuration:{machineId:s.machineId,supportId:s.supportId,toolId:s.toolId,toolCollisionRepresentation:'Boxes',placement:row(placement),tcp:numbers('tcp'),beam:numbers('beam')}});s.dirty=false;s.report=null;s.snapshotKey=null;
    }else if(action==='build'){
      if(s.dirty)throw Error('请先应用机床配置。');
      await planMachineMotion(context,view,s,ops);
    }
    if(action.startsWith('definition-')||action==='import'){
      const result=await context.productProxy.invoke('MachineDefinition.List',{});
      if(!view.disposed)view.scene.machineDefinitions=result.definitions;
    }else if(!poseOnly&&!['build','export','program'].includes(action)&&!action.startsWith('select:'))await ops.refreshSceneState(context,view);
    await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
    const changedProject=poseOnly||['instance-toggle','apply','mount','post-save'].includes(action)||/^(instantiate:|job:|axis-save:|limits:)/.test(action);
    if(changedProject){await saveEditedProject(context);if(poseOnly)await view.areas?.[view.activeAreaId]?.viewReader?.poll?.();}
    if(action.startsWith('definition-')||action==='import')await refreshDefinitionHistory(context);
    if(action.startsWith('instantiate:')||action.startsWith('select:')||action==='mount')await ops.fitViewAfterRenderPublish(context,view);
  }catch(error){s.error=error.message;ops.appendProjectLog(context,'error',s.error);}
  finally{
    if(suspendedSetupEntry&&!mountSucceeded){const edits=view.fiveMachineParameterEdits;if(edits&&!edits.entries.has(suspendedSetupEntry.key))edits.entries.set(suspendedSetupEntry.key,suspendedSetupEntry.entry);s.dirty=true;}
    s.mountingDraft=false;s.definitionEnabledPending=null;s.busy=false;s.selecting=false;s.poseMoving=false;s.savingPost=false;if(!view.disposed&&context.isCurrentProject?.()!==false){if(!poseOnly||s.error||activeTool(view)==='machine-simulation')ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();}}
  if(selecting&&s.nextMachineId){const next=s.nextMachineId;s.nextMachineId='';if(!view.disposed&&context.isCurrentProject?.()!==false)await machineCommand(context,view,'select:'+next,ops);}
  // Creation can originate in CAM head settings. Navigate after the previous
  // area's refresh/save has finished so its captured context cannot undo it.
  if(instantiateArea&&instantiateArea!=='simulation'&&!s.error&&!view.disposed&&context.isCurrentProject?.()!==false&&(view.activeAreaId||view.fiveAxisArea)===instantiateArea){
    if(view.fiveSimulation)view.fiveSimulation.headOpen=false;
    await context.actions?.selectRibbonTab?.('simulation');
  }
}
