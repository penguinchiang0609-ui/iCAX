import {workpieceSelectionBusy} from './workpieceSelection.mjs';
import {setSetupOrder} from './jobExecution.mjs';
export function attachPreparationDrag(mount,binding){
 let drag=null,cue=null;
 const clear=()=>{cue?.removeAttribute('data-five-setup-drop');cue=null;};
 mount.addEventListener('dragstart',event=>{
  const row=event.target.closest?.('[data-five-setup-drag]');if(!row)return;
  const {view,context}=binding;if(workpieceSelectionBusy(view)||context.isCurrentProject?.()===false){event.preventDefault();return;}
  drag={view,id:row.dataset.fiveSetupDrag,workpieceId:row.dataset.fiveSetupWorkpiece,expected:structuredClone(view.scene.domain.expected)};
  event.dataTransfer.effectAllowed='move';event.dataTransfer.setData('text/plain',drag.id);
 });
 const target=event=>{
  const {view,context}=binding,row=event.target.closest?.('[data-five-setup-drag]');
  if(!drag||drag.view!==view||context.isCurrentProject?.()===false||workpieceSelectionBusy(view)||JSON.stringify(drag.expected)!==JSON.stringify(view.scene.domain.expected)||!row||view.fiveAxisArea!=='operation'&&row.dataset.fiveSetupWorkpiece!==drag.workpieceId||row.dataset.fiveSetupDrag===drag.id)return null;
  return {row,id:row.dataset.fiveSetupDrag,after:event.clientY>row.getBoundingClientRect().top+row.getBoundingClientRect().height/2};
 };
 mount.addEventListener('dragover',event=>{clear();const hit=target(event);if(!hit)return;event.preventDefault();event.dataTransfer.dropEffect='move';cue=hit.row;cue.dataset.fiveSetupDrop=hit.after?'after':'before';const pane=cue.closest('.cam-context-pane'),r=pane?.getBoundingClientRect();if(r){if(event.clientY<r.top+40)pane.scrollTop-=12;else if(event.clientY>r.bottom-40)pane.scrollTop+=12;}});
 mount.addEventListener('drop',event=>{
  const hit=target(event),source=drag;clear();drag=null;if(!hit)return;event.preventDefault();
  const {view,context,ops}=binding,ids=view.scene.domain.setups.map(s=>s.id),from=ids.indexOf(source.id);ids.splice(from,1);ids.splice(ids.indexOf(hit.id)+(hit.after?1:0),0,source.id);
  void setSetupOrder(context,view,ids,ops,source.expected);
 });
 mount.addEventListener('dragend',()=>{clear();drag=null;});
}
