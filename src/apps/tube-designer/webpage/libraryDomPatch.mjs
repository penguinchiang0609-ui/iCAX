import { patchDomNode } from "./punchDomPatch.mjs";
import { floatingParameterDiagramHost, moveFloatingParameterDiagramsToWorkspace } from "./floatingParameterDiagram.mjs";
const rendered=new WeakMap();
export function rememberLibraryDom(view,mount,suffix) {
  rendered.set(mount,{area:view.activeAreaId,suffix});
}
export function patchLibraryDom(view,mount,{left,right,overlay,suffix}) {
  const previous=rendered.get(mount);
  // First mount, navigation and opening/closing dialogs still use the full
  // lifecycle. Ordinary field edits and preview responses never remount it.
  if(!["tools","profiles","connections"].includes(view.activeAreaId) ||
    previous?.area!==view.activeAreaId || previous.suffix!==suffix)return false;
  const leftPane=mount.querySelector(".cam-context-pane");
  const rightPane=mount.querySelector(".cam-info-pane");
  const viewport=mount.querySelector(".cam-viewport");
  if(!leftPane||!rightPane||!viewport)return false;
  const floatingHost=moveFloatingParameterDiagramsToWorkspace(mount,view) ?? floatingParameterDiagramHost(mount);
  const parse=html=>{
    const template=mount.ownerDocument.createElement("template");
    template.innerHTML=html;return template.content;
  };
  for(const [pane,html] of [[leftPane,left],[rightPane,right]]) {
    const next=pane.cloneNode(false);
    next.append(parse(html));
    patchDomNode(pane,next);
  }
  // Patch only HTML-owned HUD nodes. Never touch renderer/canvas/view cube.
  const selectors=view.activeAreaId==="tools"?[".tube-tool-library-hud"]:
    view.activeAreaId==="connections"?[".tube-connection-library-hud",".tube-connection-library-stage"]:
    [".tube-profile-library-preview-hud",".tube-profile-library-preview-wait","[data-tube-designer-specification-tree]"];
  const fragment=parse(overlay);
  for(const selector of selectors) {
    const old=viewport.querySelector(selector),next=fragment.querySelector(selector);
    if(old&&next)patchDomNode(old,next);
    else if(old)old.remove();
    else if(next)viewport.append(next);
  }
  // Diagram windows live above the entire workbench, not inside the WebGL
  // viewport. Patch them in place so focus, selection and nested scroll survive.
  const floatingSelector=view.activeAreaId==="tools"?"[data-tube-tool-diagram-dock]":view.activeAreaId==="profiles"?"[data-tube-profile-diagram-dock]":"";
  const oldFloating=floatingSelector?floatingHost?.querySelector(floatingSelector):null;
  const nextFloating=floatingSelector?fragment.querySelector(floatingSelector):null;
  if(oldFloating&&nextFloating)patchDomNode(oldFloating,nextFloating);
  else if(oldFloating)oldFloating.remove();
  else if(nextFloating)floatingHost?.append(nextFloating);
  moveFloatingParameterDiagramsToWorkspace(mount,view);
  const workbench=mount.querySelector(".cam-workbench");
  for(const kind of ["notice","error"]) {
    let node=workbench?.querySelector(":scope > .cam-status."+kind);
    const value=view[kind];
    if(!value){node?.remove();continue;}
    if(!node){node=mount.ownerDocument.createElement("div");node.className="cam-status "+kind;workbench?.append(node);}
    node.textContent=String(value);
  }
  return true;
}
