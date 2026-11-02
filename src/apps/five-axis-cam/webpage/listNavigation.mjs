const primary='[data-five-face-focus],.five-tree-row[data-five-node],.five-tree-row[data-five-rapid-id],[data-five-fixture-part]';
const lists='.five-support-face-list,.five-cut-tree,[data-five-key="path-list-rapid"],.five-fixture-parts';

export function listKeyboardAction(event,mount){
  if(event.defaultPrevented||event.isComposing||event.ctrlKey||event.metaKey||event.altKey||event.shiftKey
    ||!['ArrowUp','ArrowDown','Enter'].includes(event.key)||!mount.contains(event.target)
    ||event.target.closest?.('input,textarea,select,[contenteditable]:not([contenteditable="false"]),[role="textbox"]'))return null;
  const list=event.target.closest?.(lists);if(!list)return null;
  const origin=event.target.closest(primary);
  if(event.key==='Enter')return origin&&!origin.disabled?{row:origin,focus:!event.repeat}:null;
  const rows=[...list.querySelectorAll(primary)].filter(row=>!row.disabled&&row.getAttribute('aria-disabled')!=='true'&&row.getClientRects().length);
  if(!rows.length)return null;
  const current=origin||event.target.closest('.five-support-face,.five-cut-tree-item,.five-cut-group-heading,.five-fixture-parts tr')?.querySelector(primary);
  const index=rows.indexOf(current),next=index<0?(event.key==='ArrowUp'?rows.length-1:0):Math.max(0,Math.min(rows.length-1,index+(event.key==='ArrowUp'?-1:1)));
  return {row:rows[next],focus:false};
}

export function revealListRow(row,mount){
  row.focus({preventScroll:true});
  scrollListRow(row,mount);
}

export function revealFaceListSelection(mount,listKey,faceId){
  if(faceId!=null)revealSceneListSelection(mount,`${listKey}-${faceId}`);
}

// Scene selection reveals a row without moving keyboard focus or another pane.
export function revealSceneListSelection(mount,key,childSelector=''){
  if(!mount||key==null)return false;
  const node=mount.querySelector(`[data-five-key="${CSS.escape(String(key))}"]`);
  const row=childSelector?node?.querySelector(childSelector):node;
  if(!row?.getClientRects().length)return false;
  scrollListRow(row,mount);return true;
}

function scrollListRow(row,mount){
  // Scroll only the nearest list container, leaving the other panes in place.
  for(let parent=row.parentElement;parent&&parent!==mount;parent=parent.parentElement){
    if(parent.scrollHeight<=parent.clientHeight||!/(auto|scroll)/.test(getComputedStyle(parent).overflowY))continue;
    scrollRowWithin(row,parent);
    break;
  }
}

function scrollRowWithin(row,parent){
  const r=row.getBoundingClientRect(),p=parent.getBoundingClientRect(),top=p.top+parent.clientTop,bottom=top+parent.clientHeight;
  if(r.top<top)parent.scrollTop+=r.top-top;else if(r.bottom>bottom)parent.scrollTop+=r.bottom-bottom;
}
