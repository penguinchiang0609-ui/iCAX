import {capturePaneInteraction} from '../../_shared/workbench/utils/paneInteractionState.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {machineTypeLabels,operationCommand,operationField} from './operationWorkspace.mjs';
import {activeWorkpiece} from './workpieceState.mjs';

const dialogs=new WeakMap();
const current=record=>!record.view.disposed&&record.context.isCurrentProject?.()!==false&&record.mount.isConnected;
export function canCreateOperation(view){
 const draft=view.fiveOperationCreateDialog,workpiece=activeWorkpiece(view);
 return !!draft&&!view.disposed&&!view.pending&&!view.fiveOperationBusy&&!view.fiveOperationRequest&&Object.hasOwn(machineTypeLabels,draft.machineType)&&workpiece?.enabled!==false&&workpiece?.id===draft.workpieceId&&draft.epoch===view.scene?.domain?.expected?.epoch;
}
function close(record,restore){
 record.dialog.close();record.dialog.remove();dialogs.delete(record.document);
 if(restore&&current(record))record.restore();
}
export function disposeOperationCreateDialog(view,document){const record=dialogs.get(document);if(record?.view===view)close(record,false);}
const options=(items,placeholder)=>`<option value="">${esc(placeholder)}</option>${items.map(([id,label])=>`<option value="${attr(id)}">${esc(label)}</option>`).join('')}`;
// Like the existing blocking dialog, this lives outside the patched workspace.
// Its fields and the original viewport survive every asynchronous scene refresh.
export function syncOperationCreateDialog(context,view,mount,ops){
 const document=mount.ownerDocument;let record=dialogs.get(document);
 if(record&&(record.view!==view||!record.mount.isConnected)){close(record,false);record=null;}
 if(!view.fiveOperationCreateDialog||view.disposed||context.isCurrentProject?.()===false){if(record)close(record,true);return;}
 if(!record){
  const dialog=document.createElement('dialog');dialog.className='five-operation-create-dialog';dialog.setAttribute('role','dialog');dialog.setAttribute('aria-labelledby','five-operation-create-title');
  dialog.innerHTML=`<form class="five-operation-create-card"><header><h2 id="five-operation-create-title">新增工序</h2></header><span>工件</span><p data-operation-create-workpiece></p><label for="five-operation-create-machine">机型</label><select id="five-operation-create-machine" name="machineType" required aria-label="机型">${options(Object.entries(machineTypeLabels),'请选择机型')}</select><p>工件几何由工件页管理；机型创建后固定。</p><p data-operation-create-error role="alert" hidden></p><footer><button type="button" data-operation-create-cancel>取消</button><button type="submit" class="five-primary" data-operation-create-apply>确定</button></footer></form>`;
  record={document,dialog,view,context,mount,ops,restore:capturePaneInteraction(mount,['.cam-context-pane','.cam-info-pane','[data-five-dock]','[data-five-overlay]'])};
  const bound=record;
  dialog.addEventListener('change',event=>{if(current(bound)&&event.target.matches('select[name]'))void operationField(bound.context,bound.view,event.target.name,event.target.value,bound.ops);});
  dialog.addEventListener('submit',event=>{event.preventDefault();if(current(bound)&&(canCreateOperation(bound.view)||bound.view.fiveOperationRequest&&!bound.view.fiveOperationBusy&&!bound.view.pending))void operationCommand(bound.context,bound.view,bound.view.fiveOperationRequest?'retry':'create-apply',bound.ops);});
  const cancel=()=>{if(current(bound))void operationCommand(bound.context,bound.view,'create-cancel',bound.ops);};
  dialog.addEventListener('cancel',event=>{event.preventDefault();cancel();});
  dialog.querySelector('[data-operation-create-cancel]').addEventListener('click',cancel);
  // Application shortcuts must not act on the scene behind a modal form.
  dialog.addEventListener('keydown',event=>event.stopPropagation());dialog.addEventListener('keyup',event=>event.stopPropagation());
  document.body.append(dialog);dialogs.set(document,record);dialog.showModal();
 }
 Object.assign(record,{context,ops});
 const draft=view.fiveOperationCreateDialog,machine=record.dialog.querySelector('[name="machineType"]');
 record.dialog.querySelector('[data-operation-create-workpiece]').textContent=draft.workpieceName;
 if(machine.value!==draft.machineType)machine.value=draft.machineType;
 machine.disabled=!!(view.fiveOperationBusy||view.fiveOperationRequest);
 const apply=record.dialog.querySelector('[data-operation-create-apply]'),retry=!!view.fiveOperationRequest;
 apply.textContent=view.fiveOperationBusy?'正在创建…':retry?'重试原操作':'确定';apply.disabled=retry?!!(view.pending||view.fiveOperationBusy):!canCreateOperation(view);
 record.dialog.querySelector('[data-operation-create-cancel]').disabled=!!(view.fiveOperationBusy||retry);
 const error=record.dialog.querySelector('[data-operation-create-error]');error.textContent=view.error||(retry?'操作结果尚未确认，请重试原操作。':'');error.hidden=!error.textContent;
 if(!record.focused){record.focused=true;machine.focus({preventScroll:true});}
}
