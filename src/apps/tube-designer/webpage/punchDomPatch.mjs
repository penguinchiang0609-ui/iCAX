// A punch edit is local to its dialogs. Preserve the existing controls, scroll
// containers and renderer host; never remount the surrounding workbench.
const rendered = new WeakMap();
const roots = [".tube-designer-punch-backdrop", ".tube-designer-punch-parameter-backdrop", "[data-tube-designer-operation-wait]"];

export function rememberPunchDom(view, mount) {
  if(!mount?.querySelector)return;
  const previous=rendered.get(mount),batch=view.tubeDesignerPunchBatch;
  const scene=batch&&previous?.batch===batch?previous.scene:view.scene?.tubeDesigner;
  rendered.set(mount,{state:batch??view.tubeDesignerPunchWizard,batch,scene});
}
export function closePunchDom(view,mount) {
  const previous=rendered.get(mount);
  if(view.tubeDesignerPunchWizard||view.activeAreaId!=="nesting"||!previous?.batch
    ||previous.scene!==view.scene?.tubeDesigner||!mount?.querySelector?.('.tube-designer-punch-backdrop'))return false;
  for(const selector of roots)mount.querySelector(selector)?.remove();
  return true;
}
function key(node) {
  if(node.nodeType!==1)return null;
  if(node.hasAttribute("data-tube-designer-library-profile-row"))return node.tagName+":profile:"+node.dataset.tubeDesignerProfileId;
  if(!node.matches("input,select,textarea,button") && (node.hasAttribute("data-row-id")||node.hasAttribute("data-profile-key")))
    return node.tagName+":stock:"+(node.dataset.profileKey??"")+":"+(node.dataset.rowId??"");
  for(const name of ["data-floating-editor-layer","data-floating-editor-window","data-tube-designer-parameter-group","data-tube-designer-product-tool-editor","data-tube-designer-product-part-row","data-parameter-advanced-key","data-parameter-advanced-item","data-array-group-id","data-punch-batch-part-row","data-punch-batch-definition-row","data-punch-batch-definition-editor","data-punch-batch-current-part","data-tube-designer-punch-row","data-tube-designer-punch-end-row","data-punch-inline-row","data-punch-record-column","data-punch-column-key","data-punch-region","data-punch-region-splitter","data-punch-editor-mode","data-floating-parameter-diagram-layer","data-library-floating-diagram","data-parameter-diagram-for"])
    if(node.hasAttribute(name))return node.tagName+":"+name+":"+node.getAttribute(name);
  if(node.id)return node.tagName+"#"+node.id;
  // Conditional fields insert/remove entire labels. Match their containers by
  // the control's stable identity, not by position, so following inputs and
  // listeners survive when a preceding field appears or disappears.
  if(node.matches("label.tube-designer-field,label.punch-pose-field,label.punch-array-field,label.tube-designer-punch-layout-field,label.tube-designer-punch-layout-text")) {
    const control=node.querySelector("input,select,textarea");
    if(control)return node.tagName+":"+key(control);
  }
  if(node.matches("input,select,textarea,button"))return node.tagName+":"+JSON.stringify(
    [...node.attributes].filter(a=>a.name.startsWith("data-")).map(a=>[a.name,a.value]).sort());
  return null;
}
function compatible(oldNode,nextNode) {
  return oldNode.nodeType===nextNode.nodeType && (oldNode.nodeType!==1
    || oldNode.tagName===nextNode.tagName && key(oldNode)===key(nextNode));
}
function activeControlDraft(oldNode,nextNode) {
  if(oldNode!==oldNode.ownerDocument.activeElement || oldNode.disabled || oldNode.readOnly
    || nextNode.disabled || nextNode.readOnly || key(oldNode)!==key(nextNode)
    || !oldNode.matches("textarea,input") || oldNode.type!==nextNode.type
    || ["checkbox","radio","button","submit","reset","range","file","color","hidden"].includes(oldNode.type)
    || oldNode.value===oldNode.defaultValue || nextNode.value!==oldNode.defaultValue)return null;
  // The response still contains the last rendered value. Preserve the current
  // unsubmitted text, captured now rather than when an async request began.
  // A changed model value or resource/control identity must update normally.
  return {value:oldNode.value,start:oldNode.selectionStart,end:oldNode.selectionEnd,
    direction:oldNode.selectionDirection,top:oldNode.scrollTop,left:oldNode.scrollLeft};
}
export function patchDomNode(oldNode,nextNode) {
  patch(oldNode,nextNode);
}
function patch(oldNode,nextNode) {
  if(oldNode.nodeType!==1) {
    if(oldNode.nodeValue!==nextNode.nodeValue)oldNode.nodeValue=nextNode.nodeValue;
    return;
  }
  // This subtree belongs to the viewport, not the HTML renderer. In particular
  // retain its exact canvas, cube and event bindings across parameter edits.
  if(oldNode.hasAttribute("data-tube-designer-punch-viewport"))return;
  const control=oldNode.matches("input,select,textarea");
  const nextValue=control?nextNode.value:null, nextChecked=nextNode.checked;
  const draft=control?activeControlDraft(oldNode,nextNode):null;
  for(const attribute of [...oldNode.attributes]) {
    if(oldNode.tagName==="DETAILS"&&attribute.name==="open")continue;
    if(!nextNode.hasAttribute(attribute.name))oldNode.removeAttribute(attribute.name);
  }
  for(const attribute of nextNode.attributes) {
    if(oldNode.tagName==="DETAILS"&&attribute.name==="open")continue;
    if(oldNode.getAttribute(attribute.name)!==attribute.value)oldNode.setAttribute(attribute.name,attribute.value);
  }
  const available=[...oldNode.childNodes];
  let cursor=oldNode.firstChild;
  for(const nextChild of [...nextNode.childNodes]) {
    const childKey=key(nextChild);
    const match=childKey!==null?available.find(node=>key(node)===childKey&&compatible(node,nextChild))
      :available.find(node=>node===cursor&&compatible(node,nextChild));
    if(match) {
      available.splice(available.indexOf(match),1);
      if(match!==cursor)oldNode.insertBefore(match,cursor);
      patch(match,nextChild);cursor=match.nextSibling;
    } else {
      oldNode.insertBefore(nextChild,cursor);
    }
  }
  for(const child of available)child.remove();
  if(control) {
    const value=draft?draft.value:nextValue;
    if(oldNode.value!==value)oldNode.value=value;
    if(oldNode.tagName==="INPUT"&&oldNode.checked!==nextChecked)oldNode.checked=nextChecked;
    if(draft) {
      if(draft.start!=null && typeof oldNode.setSelectionRange==="function") {
        try {oldNode.setSelectionRange(draft.start,draft.end,draft.direction);} catch {}
      }
      oldNode.scrollTop=draft.top;oldNode.scrollLeft=draft.left;
    }
  }
}

export function patchPunchDom(view, mount, html) {
  const previous=rendered.get(mount);
  if(!view.tubeDesignerPunchWizard || view.tubeDesignerPartDrawing || view.activeAreaId!=="nesting"
    || previous?.state!==(view.tubeDesignerPunchBatch??view.tubeDesignerPunchWizard)
    || !view.tubeDesignerPunchBatch&&previous?.scene!==view.scene?.tubeDesigner
    || !mount.querySelector(".tube-designer-punch-backdrop"))return false;
  const template=mount.ownerDocument.createElement("template");template.innerHTML=html;
  const parent=mount.querySelector(".tube-designer-punch-backdrop").parentNode;
  for(const selector of roots) {
    const oldNode=mount.querySelector(selector),nextNode=template.content.querySelector(selector);
    if(oldNode&&nextNode)patch(oldNode,nextNode);
    else if(oldNode)oldNode.remove();
    else if(nextNode)parent.append(nextNode);
  }
  return true;
}
