// A punch edit is local to its dialogs. Preserve the existing controls, scroll
// containers and renderer host; never remount the surrounding workbench.
const rendered = new WeakMap();
const roots = [".tube-designer-punch-backdrop", ".tube-designer-punch-parameter-backdrop", "[data-tube-designer-operation-wait]"];

export function rememberPunchDom(view, mount) {
  if(!mount?.querySelector)return;
  rendered.set(mount,{state:view.tubeDesignerPunchWizard,scene:view.scene?.tubeDesigner});
}
function key(node) {
  if(node.nodeType!==1)return null;
  for(const name of ["data-array-group-id","data-tube-designer-punch-row","data-tube-designer-punch-end-row","data-punch-editor-mode"])
    if(node.hasAttribute(name))return node.tagName+":"+name+":"+node.getAttribute(name);
  if(node.id)return node.tagName+"#"+node.id;
  if(node.matches("input,select,textarea,button"))return node.tagName+":"+JSON.stringify(
    [...node.attributes].filter(a=>a.name.startsWith("data-")).map(a=>[a.name,a.value]).sort());
  return null;
}
function compatible(oldNode,nextNode) {
  return oldNode.nodeType===nextNode.nodeType && (oldNode.nodeType!==1
    || oldNode.tagName===nextNode.tagName && key(oldNode)===key(nextNode));
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
    if(oldNode.value!==nextValue)oldNode.value=nextValue;
    if(oldNode.tagName==="INPUT"&&oldNode.checked!==nextChecked)oldNode.checked=nextChecked;
  }
}

export function patchPunchDom(view, mount, html) {
  const previous=rendered.get(mount);
  if(!view.tubeDesignerPunchWizard || view.tubeDesignerPartDrawing || view.activeAreaId!=="nesting"
    || previous?.state!==view.tubeDesignerPunchWizard || previous?.scene!==view.scene?.tubeDesigner
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
