import { buildCutTree } from './cutTreeModel.mjs';
import {revealSceneCut} from './sceneListReveal.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { activeTool, workspaceState } from './workspaceModel.mjs';
import { escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { button } from './workspaceControls.mjs';
import { constrainedCutIds, cutTreeCommand } from './cutTree.mjs';
import {toolAxisErrorText} from './toolAxisErrors.mjs';
import {waitForTaskResult} from './taskProgressStream.mjs';
import {machiningTaskTimeoutMs} from './operationTiming.mjs';
import {applyOrderReceipt} from './orderState.mjs';
import {orderInput} from './orderParameters.mjs';

const geometry=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId)?.generatedGeometry;
// An editable definition preview does not imply generated machining or travel.
const definitions=view=>({status:'Definition',revision:key(view),transitions:[],operations:buildCutTree(view).cuts
  .filter(c=>c.enabled!==false&&c.setupId===view.scene.domain.activeSetupId)
  .map((c,i)=>({intentId:c.id,placementId:c.placementId,order:String(i+1),machiningKind:c.machiningKind,boundaryRole:c.boundaryRole,
    start:c.startPoint,parts:[{role:'Cut',points:c.previewPoints}]}))});
const key=view=>JSON.stringify(view.scene?.domain?.expected);
const isManual=view=>activeTool(view)==='manual-order'&&view.fiveOrder?.open;
const prefix=state=>state.prefix;
const valid=view=>!!view.fiveOrder?.base?.operations?.length&&view.fiveOrder?.key===key(view);
const busy=view=>view.pending||view.fiveRapidBatch?.running||!!view.fiveOrder?.request;
export function manualOrderAnchor(view){
  const ui=workspaceState(view),ids=ui.selectedIntentIds||[ui.selectedIntentId].filter(Boolean);
  if(ids.length!==1)return null;
  return view.scene?.intentToolpaths?.find(c=>c.id===ids[0]&&c.setupId===view.scene.domain.activeSetupId&&c.enabled!==false)?.id??null;
}
export function openOrdering(view,mode='start'){
  if(view.fiveOrder?.request){view.fiveOrder.open=true;return;}
  const result=definitions(view);
  let retained=[];
  if(mode==='after'){
    const id=manualOrderAnchor(view),ids=result.operations.map(o=>o.intentId),tree=buildCutTree(view);let node=tree.nodes.get(`c:${id}`);
    if(!node||!ids.includes(id)){view.error='请先选择当前装夹中的一条已启用刀路。';return false;}
    while(node.parent)node=tree.nodes.get(node.parent);
    // A grouped path anchors at the end of its whole ordering unit.
    retained=ids.slice(0,Math.max(...node.leafIds.map(value=>ids.indexOf(value)))+1);
  }
  view.fiveOrder={open:true,mode,key:key(view),expected:structuredClone(view.scene.domain.expected),
    base:structuredClone(result),prefix:retained,menu:null};
  view.error=result.operations.length?'':'请先添加并启用刀路。';
  return true;
}
export function orderingPreview(view,result){
  if(['manual-order','auto-order'].includes(activeTool(view)))result=definitions(view);
  if(!isManual(view)||!valid(view))return result;
  const state=view.fiveOrder,chosen=prefix(state),ids=[...chosen,...state.base.operations.map(o=>o.intentId).filter(id=>!chosen.includes(id))];
  const byId=new Map(state.base.operations.map(o=>[o.intentId,o]));
  const operations=constrainedCutIds(view,ids).filter(id=>byId.has(id)).map((id,i)=>({...byId.get(id),order:String(i+1)}));
  return {...result,operations,transitions:[],orderDraft:chosen};
}
export function orderingPick(view,id){
  if(!isManual(view))return false;
  if(view.scene.intentToolpaths?.find(c=>c.id===id)?.locked)return true;
  if(busy(view))return true;
  if(!valid(view)){view.error='刀路已变化，请重新打开手动排序。';return true;}
  const state=view.fiveOrder,ids=state.base.operations.map(o=>o.intentId),chosen=prefix(state);
  if(!ids.includes(id)||chosen.includes(id))return true;
  const tree=buildCutTree(view);let node=tree.nodes.get(`c:${id}`);while(node?.parent)node=tree.nodes.get(node.parent);const members=ids.filter(value=>node.leafIds.includes(value));
  if(members.some(value=>view.scene.intentToolpaths.find(c=>c.id===value)?.locked))return true;
  const next=state.mode==='after'&&!chosen.length?ids.slice(0,Math.max(...members.map(value=>ids.indexOf(value)))+1):[...chosen,id];
  state.prefix=next;
  state.menu=null;view.error='';const ui=workspaceState(view);ui.selectedIntentId=id;ui.selectedIntentIds=members;view.fivePreferredCutSelection=[...members];ui.selectedGroupIds=node.kind==='Group'?[node.id]:[];ui.selectedGroupId=ui.selectedGroupIds[0]||'';return true;
}
export function renderOrdering(view){
  const state=view.fiveOrder;if(!state?.open)return '';
  if(activeTool(view)!=='manual-order')return '';
  return `<div class="five-order-status" data-five-key="order-status" role="status">手动排序 · ${state.mode==='after'?'从当前开始':'从头开始'} · 已排 ${prefix(state).length} / ${state.base?.operations?.length||0} · 依次点选刀路，Enter 保存，Esc 退出${view.error?`<span role="alert">${esc(view.error)}</span>`:''}</div>`;
}
export function renderOrderMenu(view){
  const menu=view.fiveOrder?.menu;if(!isManual(view)||!menu)return '';
  return `<div class="five-order-menu" data-five-key="order-menu" role="menu" style="left:${menu.x}px;top:${menu.y}px">${button('five.order.menu-start','从头开始','role="menuitem"')}${button('five.order.menu-after','从此排序','role="menuitem"')}${button('five.order.redo','前移','role="menuitem"')}${button('five.order.undo','后移','role="menuitem"')}</div>`;
}
export function orderContextMenu(view,id,event){
  if(!isManual(view)||busy(view))return false;
  event.preventDefault();view.fiveOrder.menu={id,x:Math.max(0,Math.min(event.clientX,innerWidth-145)),y:Math.max(0,Math.min(event.clientY,innerHeight-155))};return true;
}
export async function orderCommand(context,view,action,ops){
  const state=view.fiveOrder;if(!state||action!=='reconcile'&&(!state.open||!['manual-order','auto-order'].includes(activeTool(view))))return;
  if(action==='close'){if(state.running||state.request)return;state.open=false;state.menu=null;view.error='';ops.renderProject(context,view);return;}
  if(!['apply','reconcile'].includes(action)){
    if(busy(view))return;
    const id=state.menu?.id;state.menu=null;
    if(['start','after','menu-start','menu-after'].includes(action)){
      state.mode=action.endsWith('after')?'after':'start';state.prefix=[];
      if(action.startsWith('menu-'))orderingPick(view,id);
    }else if(['undo','redo'].includes(action)){
      // Move through the native tree transaction and the shared file writer.
      state.open=false;await cutTreeCommand(context,view,action==='redo'?'Up':'Down',ops);
      openOrdering(view);
    }
    ops.renderProject(context,view);return;
  }
  if(view.pending||state.running)return;
  if(!state.request&&!valid(view)){view.error=state.base?.operations?.length?'刀路已变化，请重新打开排序。':'请先添加并启用刀路。';ops.renderProject(context,view);return;}
  const manual=state.request?state.request.mode==='Manual':activeTool(view)==='manual-order';
  if(!state.request&&manual&&!prefix(state).length)return;
  try{
    state.request??={commandId:crypto.randomUUID(),expected:state.expected,setupId:view.scene.domain.activeSetupId,
      mode:manual?'Manual':'Automatic',...(manual?{intentIds:orderingPreview(view,geometry(view)).operations.map(o=>o.intentId)}:orderInput(view))};
  }catch(error){view.error=error.message;ops.renderProject(context,view);return;}
  const request=state.request;state.running=true;state.done=false;view.pending=true;view.error='';ops.renderProject(context,view);
  const current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  try{
    let receipt,result;
    try{receipt=await context.sceneProxy.invoke(manual?'CamPlan.SetOrder':'CamPlan.BeginOrder',request);}
    catch(error){
      if(error.frame?.kind===2&&Number(error.frame.status)!==0||/CAM_|INVALID/.test(error.message)){state.request=null;throw error;}
      receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});
      if(receipt.status==='NotFound'){
        // Reconcile the identical idempotent command automatically. A delayed
        // reply must never turn the Apply button into a duplicate mutation.
        receipt=await context.sceneProxy.invoke(manual?'CamPlan.SetOrder':'CamPlan.BeginOrder',request);
      }
    }
    if(manual)result=receipt.result??receipt;
    else{
      if(current())ops.renderProject(context,view);
      if(receipt.status!=='Running'&&receipt.taskId)receipt=await context.sceneProxy.invoke('CamPlan.QueryStatus',{epoch:receipt.epoch,taskId:receipt.taskId});
      const task=await waitForTaskResult(context,receipt,{active:current,queryMethod:'CamPlan.QueryStatus',cancelMethod:'CamPlan.CancelTask',timeoutMs:machiningTaskTimeoutMs});
      if(task?.status!=='Completed'){state.request=null;throw Error(task?.error||(/Superseded/.test(task?.status)?'CAM_REVISION_CONFLICT':'排序已取消。'));}
      result=task.result;
    }
    state.request=null;state.done=true;state.open=false;state.menu=null;
    await saveCommittedCutEdit(context,view,ops);
    ops.appendProjectLog(context,'ok',manual?'手动排序已保存':'自动排序已完成');
    if(current()&&JSON.stringify(view.scene.domain.expected)===JSON.stringify(request.expected)){
      applyOrderReceipt(view,request,result);
      state.expected=structuredClone(view.scene.domain.expected);state.key=key(view);state.base=definitions(view);
    }
  }catch(error){
    if(/CAM_EPOCH_CHANGED/.test(error.message))state.request=null;
    if(current())view.error=state.request?'排序通信中断，正在自动核对结果。':/CAM_ORDER_DEPENDENCY_CONFLICT/.test(error.message)?'分组或锁定顺序与工艺先后冲突。请调整分组或解除相关锁定后重新排序。':/STALE|REVISION|EPOCH/.test(error.message)?'工程已变化，请重新执行排序。':toolAxisErrorText(error);
    if(state.request&&current()){
      const retry=async()=>{
        if(!current())return;
        if(view.pending||state.running){setTimeout(retry,1000);return;}
        await orderCommand(context,view,'reconcile',ops);
      };
      setTimeout(retry,1000);
    }
  }finally{state.running=false;view.pending=false;if(current())ops.renderProject(context,view);}
}
export function handleOrderPick(context,view,_data,_hit,event,_hits,ops){
  if(!isManual(view)||view.fiveAxisArea!=='toolpath'||!event)return false;
  if(busy(view))return true;
  const hit=orderingAtPointer(view,event);if(hit){orderingPick(view,hit.id);ops.renderProject(context,view);revealSceneCut(context,view,ops);}return true;
}
export function orderingAtPointer(view,event){
  const result=orderingPreview(view,geometry(view)),viewport=view.viewport,box=viewport.renderer.domElement.getBoundingClientRect();let best=9,hit=null;
  for(const op of result?.operations||[]){
    if(view.scene.intentToolpaths?.find(c=>c.id===op.intentId)?.locked)continue;
    const placement=view.scene.domain.placements.find(p=>p.id===op.placementId);if(!placement)continue;
    const matrix=new THREE.Matrix4().set(...placement.transform);
    for(const part of op.parts){let last;
      for(const p of part.points){const q=new THREE.Vector3(...p).applyMatrix4(matrix).project(viewport.camera),b=[box.x+(q.x+1)*box.width/2,box.y+(1-q.y)*box.height/2,q.z];
        if(last&&last[2]>=-1&&last[2]<=1&&b[2]>=-1&&b[2]<=1){const dx=b[0]-last[0],dy=b[1]-last[1],t=Math.max(0,Math.min(1,((event.clientX-last[0])*dx+(event.clientY-last[1])*dy)/(dx*dx+dy*dy||1)));
          const d=Math.hypot(event.clientX-last[0]-t*dx,event.clientY-last[1]-t*dy);if(d<best){best=d;hit={id:op.intentId,parts:op.parts,placementId:op.placementId};}}
        last=b;
      }
    }
  }
  return hit;
}
