import {prepareOperationArea} from './operationWorkspace.mjs';
import {workpieceSelectionBusy} from './workpieceSelection.mjs';

const navigationQueues=new WeakMap();
const live=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const preparationPending=view=>!!(view.fiveOperationAreaSync||view.fiveWorkpieceSwitching||view.sceneRefreshPending);
const nextPreparationTurn=()=>new Promise(resolve=>setTimeout(resolve,16));

// A running Activate/save/readback owns the scene until it drains. Keep only
// the latest navigation intent, rather than issuing overlapping mutations or
// silently dropping a click while that preparation is in progress.
export function prepareOperationNavigation(context,view,ops,area){
  const sequence=view.fiveOperationNavigationSequence=(view.fiveOperationNavigationSequence||0)+1;
  let queue=navigationQueues.get(view);
  if(!queue){queue={tail:Promise.resolve(),latest:sequence};navigationQueues.set(view,queue);}
  queue.latest=sequence;view.fiveOperationNavigationPending=true;
  const current=()=>live(context,view)&&view.fiveOperationNavigationSequence===sequence;
  const request=queue.tail.catch(()=>{}).then(async()=>{
    if(!current())return false;
    // Initial mounting has no product binding/domain to prepare yet.
    if(!ops||!view.scene?.domain)return true;
    while(preparationPending(view)){
      if(!current())return false;
      await nextPreparationTurn();
    }
    if(!current()||workpieceSelectionBusy(view))return false;
    view.fiveOperationNavigationPreparing=true;
    try{
      const prepared=await prepareOperationArea(context,view,ops,area);
      if(!prepared||!current())return false;
      if(area!=='operation')view.fiveOperationCreateDialog=null;
      return true;
    }finally{view.fiveOperationNavigationPreparing=false;}
  }).catch(error=>{
    if(live(context,view)){view.error=error.message;ops?.renderProject?.(context,view);}
    return false;
  });
  const finished=request.finally(()=>{
    if(queue.latest!==sequence)return;
    view.fiveOperationNavigationPending=false;
    if(navigationQueues.get(view)===queue)navigationQueues.delete(view);
  });
  queue.tail=finished;
  return finished;
}
