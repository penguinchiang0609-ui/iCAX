import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {button} from './workspaceControls.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {refreshMachiningState} from './domainActions.mjs';
import {renderTransformTools} from './transformHud.mjs';

const headOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId)?.headModel;
const cmd=(action,label,extra='')=>button('five.head-collider.'+action,label,extra);
const numeric=v=>typeof v==='number'?String(Number(v.toFixed(4))):v;
export function headColliderState(view){
 const s=view.fiveHeadColliders??={show:false,active:'',partIndex:null,parts:[],mode:null};
 const head=headOf(view),id=head?.instance?.id,signature=JSON.stringify([id,head?.collisionBoxes,head?.assembly?.nodes.map(n=>n.collisionBoxes)]);
 if(id!==s.instanceId){const finish=s.boundsFinish;s.boundsFinish=null;clearTimeout(s.timer);s.instanceId=id;s.signature=null;s.parts=[];s.partIndex=null;s.active='';s.mode=null;s.request=null;s.error='';finish?.(true);}
 if(signature!==s.signature&&!s.parts.some(p=>p.dirty)&&!s.saving){
  const previous=s.parts;s.signature=signature;
  const sources=head?.assembly?head.assembly.nodes.map((n,index)=>({index,name:n.name,attachments:n.collisionBoxes||[]})):[{index:-1,name:head?.name||'刀体',attachments:head?.collisionBoxes||[]}];
  s.parts=sources.map(p=>({...p,revision:0,dirty:false,boxes:p.attachments.filter(c=>c.geometry?.type==='box').map((c,i)=>({
   id:c.id&&!String(c.id).includes(':')?c.id:previous.find(x=>x.index===p.index)?.boxes[i]?.id||crypto.randomUUID(),name:c.name||c.collisionName||'碰撞盒 '+(i+1),enabled:c.enabled!==false,
   position:c.pose.slice(0,3).map(v=>v*1000),rotation:c.pose.slice(3).map(v=>v*180/Math.PI),size:c.geometry.size.map(v=>v*1000),
  }))}));
  if(!s.parts.some(p=>p.index===s.partIndex))s.partIndex=head?.assembly?.tool??-1;
 }
 const part=s.parts.find(p=>p.index===s.partIndex);if(!part?.boxes.some(b=>b.id===s.active))s.active=part?.boxes[0]?.id||'';
 return s;
}
export const activeHeadCollider=view=>{const s=headColliderState(view),part=s.parts.find(p=>p.index===s.partIndex);return {s,part,box:part?.boxes.find(b=>b.id===s.active)};};
export function markHeadColliders(view,part){part.dirty=true;part.revision++;const s=headColliderState(view);if(!s.request)s.error='';}
const valid=part=>part.boxes.every(b=>b.name.trim()&&[...b.position,...b.rotation,...b.size].every(v=>String(v).trim()&&Number.isFinite(Number(v)))&&b.size.every(v=>Number(v)>0&&Number(v)<=1e6));
export function cancelInvalidHeadColliderEdits(view){
 const s=view.fiveHeadColliders;if(!s||s.request)return false;
 const invalid=s.parts.filter(p=>p.dirty&&!valid(p));if(!invalid.length)return false;
 clearTimeout(s.timer);s.timer=null;
 const committed=headColliderState({scene:view.scene,fiveHeadColliders:{instanceId:s.instanceId,signature:null,parts:s.parts.map(p=>({...p,dirty:false})),partIndex:s.partIndex,active:s.active}});
 for(const part of invalid){const saved=committed.parts.find(p=>p.index===part.index);part.boxes=structuredClone(saved?.boxes||[]);part.dirty=false;part.revision++;}
 s.error='';headColliderState(view);return true;
}
function payloadPart(part){return {partIndex:part.index,boxes:part.boxes.map(b=>({id:b.id,name:b.name.trim(),enabled:!!b.enabled,pose:[...b.position.map(v=>Number(v)/1000),...b.rotation.map(v=>Number(v)*Math.PI/180)],geometry:{type:'box',size:b.size.map(v=>Number(v)/1000)}}))};}
export function scheduleHeadColliderSave(context,view,ops,delay=300){
 const s=headColliderState(view);s.binding={context,view,ops};clearTimeout(s.timer);s.timer=null;
 if(s.dragging||s.request)return;
 if(s.parts.some(p=>p.dirty&&!valid(p))){s.error='请输入有效参数，尺寸必须大于零。';return;}
 s.timer=setTimeout(()=>{s.timer=null;void flushHeadColliderSaves(context,view,ops).catch(()=>{});},delay);
}
export async function flushHeadColliderSaves(context,view,ops){
 const s=headColliderState(view);clearTimeout(s.timer);s.timer=null;if(s.saving)return s.saving;if(s.dragging)throw Error('请先结束碰撞盒拖动。');
 if(!s.request&&!s.parts.some(p=>p.dirty))return;
 if(s.parts.some(p=>p.dirty&&!valid(p)))throw Error('碰撞盒参数无效，尺寸必须大于零。');
 if(view.pending)throw Error('请等待当前操作完成后保存碰撞盒。');
 const active=()=>!view.disposed&&context.isCurrentProject?.()!==false;
 s.binding={context,view,ops};s.saving=(async()=>{await Promise.resolve();
  try{
   while(s.request||s.parts.some(p=>p.dirty)){
    if(!s.request){const changed=s.parts.filter(p=>p.dirty);s.request={payload:{action:'colliders',commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:view.scene.domain.activeSetupId,instanceId:s.instanceId,collisionOverrides:changed.map(payloadPart)},revisions:changed.map(p=>[p.index,p.revision])};}
    const request=s.request;view.pending=true;s.error='';if(active())ops.renderProject(context,view);
    await context.sceneProxy.invoke('CamHead.Instance',request.payload);s.request=null;
    await saveCommittedProjectEdit(context,view,ops);
    if(active())await refreshMachiningState(context,view,ops);
    for(const [index,revision] of request.revisions){const part=s.parts.find(p=>p.index===index);if(part?.revision===revision)part.dirty=false;}
    view.pending=false;if(!active())break;
   }
  }catch(error){s.error=error.message;if(/CAM_|请输入/.test(s.error))s.request=null;throw error;}
  finally{s.saving=null;view.pending=false;if(active())ops.renderProject(context,view);}
 })();return s.saving;
}
export function renderHeadColliderTools(view){
 const {s,box}=activeHeadCollider(view);if(!s.show||!s.mode||!box)return '';
 const modes=[['translate','平移'],['rotate','旋转'],['scale','缩放'],['bounds','边界']].map(([mode,label])=>[mode,label,'five.head-collider.mode:'+mode]);modes.push(['focus','聚焦','five.head-collider.focus']);
 return renderTransformTools({owner:'head-collider',title:'碰撞盒变换',mode:s.mode,busy:view.pending||s.dragging,modes});
}
export function renderHeadColliders(view){
 const head=headOf(view);if(!head?.instance)return '';
 const s=headColliderState(view),part=s.parts.find(p=>p.index===s.partIndex),bound=!!head.instance.binding?.machineId;
 const busy=bound||view.pending&&!s.saving?'disabled':'',textBusy=busy?'readonly':'';
 const field=(b,key,label,unit)=>`<div class="five-collider-vector" data-five-key="head-box-row-${b.id}-${key}"><span>${label}<small>${unit}</small></span>${b[key].map((v,i)=>`<label><span>${'XYZ'[i]}</span><input type="text" inputmode="decimal" data-model-value-authoritative="true" data-five-key="head-box-input-${b.id}-${key}-${i}" data-five-head-collider-field="box:${b.id}:${key}:${i}" aria-label="${attr(b.name+' '+label+' '+'XYZ'[i])}" value="${attr(numeric(v))}" ${textBusy}></label>`).join('')}</div>`;
 const cards=(part?.boxes||[]).map(b=>{const editing=s.active===b.id&&!!s.mode;return `<article class="five-collider-card" data-five-key="head-box-card-${b.id}" data-editing="${editing}" data-enabled="${b.enabled}"><header><label><input type="checkbox" data-five-head-collider-field="box:${b.id}:enabled" aria-label="${attr(b.name)} 参与碰撞检测" ${b.enabled?'checked':''} ${busy}><strong>${esc(b.name)}</strong></label><span class="five-collider-header-tools">${cmd(editing?'edit-end':'edit:'+b.id,editing?'完成':'编辑',busy)}${cmd('remove:'+b.id,'×',`${busy} aria-label="删除 ${attr(b.name)}"`)}</span></header><div class="five-collider-body" data-five-key="head-box-body-${b.id}" ${editing?'':'hidden'}><input class="five-collider-name" data-model-value-authoritative="true" data-five-head-collider-field="box:${b.id}:name" aria-label="碰撞盒名称" value="${attr(b.name)}" ${textBusy}>${field(b,'position','中心偏移','mm')}${field(b,'rotation','旋转','°')}${field(b,'size','尺寸','mm')}</div></article>`;}).join('');
 return `<section class="five-head-colliders" data-five-key="head-colliders"><h3>碰撞盒</h3><div class="five-collider-toolbar"><label><input type="checkbox" data-five-head-collider-field="show" ${s.show?'checked':''}>显示线框</label>${cmd('add','＋ 添加',busy)}</div><label class="five-head-collider-part">所属刀头部件<select aria-label="碰撞盒所属刀头部件" data-five-head-collider-field="part">${s.parts.map(p=>`<option value="${p.index}" ${p.index===s.partIndex?'selected':''}>${esc(p.name)}（${p.boxes.length}）</option>`).join('')}</select></label>${s.mode?`<small>在三维预览中切换平移、旋转、缩放或边界。拖动面的方块调整边界 · Ctrl 按 1 mm 吸附 · Esc 取消拖动。</small>`:''}${cards}${!part?.boxes.length?'<p>当前部件没有碰撞盒。</p>':''}<small data-head-box-status role="status">${esc(s.error||((s.saving||s.parts.some(p=>p.dirty))?'正在保存碰撞盒…':''))}</small>${s.error?cmd('retry','重试保存'):''}</section>`;
}
export function headColliderField(context,view,key,value,ops){
 const {s,part}=activeHeadCollider(view);s.binding={context,view,ops};if(view.pending&&!s.saving)return;
 if(key==='show'){s.show=!!value;if(!s.show){s.boundsFinish?.(true);s.mode=null;}}
 else if(key==='part'){s.boundsFinish?.(true);s.partIndex=Number(value);s.active='';s.mode=null;}
 else {const [,id,group,index]=key.split(':'),box=part?.boxes.find(b=>b.id===id);if(!box)return;
  if(group==='name')box.name=value;else if(group==='enabled')box.enabled=!!value;else if(['position','rotation','size'].includes(group))box[group][Number(index)]=value;
  markHeadColliders(view,part);scheduleHeadColliderSave(context,view,ops);
 }ops.renderProject(context,view);
}
export async function headColliderCommand(context,view,action,ops){
 const {s,part}=activeHeadCollider(view);s.binding={context,view,ops};if(view.pending&&!s.saving)return;
 if(action==='retry'){await flushHeadColliderSaves(context,view,ops);return;}
 if(action==='edit-end'){s.boundsFinish?.(true);s.mode=null;}
 else if(action.startsWith('edit:')){s.boundsFinish?.(true);s.active=action.slice(5);s.mode='bounds';s.show=true;}
 else if(action.startsWith('mode:')){s.boundsFinish?.(true);s.mode=action.slice(5);s.show=true;}
 else if(action==='focus'){view.fiveHeadPreview?.focusCollider?.();return;}
 else if(action==='add'&&part){const mesh=view.fiveHeadPreview?.model,object=headOf(view)?.assembly?mesh?.children[part.index]:mesh;let size=[100,100,100],position=[0,0,0];if(object?.geometry?.boundingBox){const b=object.geometry.boundingBox;size=['x','y','z'].map(k=>Math.max(.1,b.max[k]-b.min[k]));position=['x','y','z'].map(k=>(b.max[k]+b.min[k])/2);}const box={id:crypto.randomUUID(),name:'碰撞盒 '+(part.boxes.length+1),enabled:true,position,rotation:[0,0,0],size};part.boxes.push(box);s.active=box.id;s.mode='bounds';s.show=true;markHeadColliders(view,part);scheduleHeadColliderSave(context,view,ops,0);}
 else if(action.startsWith('remove:')&&part){s.boundsFinish?.(true);part.boxes=part.boxes.filter(b=>b.id!==action.slice(7));s.active='';s.mode=null;markHeadColliders(view,part);scheduleHeadColliderSave(context,view,ops,0);}
 ops.renderProject(context,view);
}
