import { patchDomNode } from "./punchDomPatch.mjs";
import { floatingParameterDiagramHost, moveFloatingParameterDiagramsToWorkspace } from "./floatingParameterDiagram.mjs";
import { capturePaneInteraction } from "../../_shared/workbench/utils/paneInteractionState.mjs";
import { moveFloatingEditorWindowsToWorkspace, patchFloatingEditorWindows, stripFloatingEditorWindows } from "./floatingEditorDom.mjs";

// Product parameter content uses the same keyed patch as the resource library.
// Capture the current interaction immediately before this synchronous patch.
export function patchParameterContent(mount, container, html) {
  if (!mount || !container?.isConnected) return false;
  const restore = capturePaneInteraction(mount);
  const template = container.ownerDocument.createElement("template");
  template.innerHTML = html;
  const next = container.cloneNode(false);
  next.append(template.content);
  patchDomNode(container, next);
  restore();
  return true;
}
const rendered=new WeakMap();
function productEditorIdentity(view) {
  const designer=view.scene?.tubeDesigner ?? {}, product=designer.product;
  return product ? JSON.stringify([product.entityId,product.templateId,view.scene?.selection ?? null,
    (designer.members ?? []).map(member=>[member.entityId,member.previewGeometryResourceId,
      member.previewGeometryResourceVersion,member.transform])]) : "";
}
function productDialogSuffix(mount,suffix) {
  const template=mount.ownerDocument.createElement("template");template.innerHTML=stripFloatingEditorWindows(mount,suffix);
  template.content.querySelector(".tube-designer-product-parts-dock")?.remove();
  template.content.querySelector("[data-tube-designer-operation-wait]")?.remove();
  return template.innerHTML.trim();
}
export function rememberLibraryDom(view,mount,suffix,sceneProxy=null) {
  rendered.set(mount,{view,sceneProxy,area:view.activeAreaId,suffix:stripFloatingEditorWindows(mount,suffix),product:productEditorIdentity(view),
    dialogs:view.activeAreaId==="view"?productDialogSuffix(mount,suffix):null});
}
export function patchLibraryDom(view,mount,{left,right,overlay,suffix,sceneProxy=null}) {
  const previous=rendered.get(mount);
  // First mount, navigation and full workflow dialogs use the viewport
  // lifecycle. Floating parameter editors and ordinary preview responses stay local.
  const product=view.activeAreaId==="view";
  if(!["tools","profiles","assemblies","view"].includes(view.activeAreaId)
    || previous?.view!==view || previous.sceneProxy!==sceneProxy || previous.area!==view.activeAreaId)return false;
  if(product) {
    // A parameter response for the same generated model can patch its editor.
    // Product navigation and a new model still use the viewport lifecycle.
    if(!previous.product || previous.product!==productEditorIdentity(view)
      || previous.dialogs!==productDialogSuffix(mount,suffix))return false;
  } else if(previous.suffix!==stripFloatingEditorWindows(mount,suffix))return false;
  const leftPane=mount.querySelector(".cam-context-pane");
  const rightPane=mount.querySelector(".cam-info-pane");
  const viewport=mount.querySelector(".cam-viewport");
  if(!leftPane||!rightPane||!viewport)return false;
  moveFloatingEditorWindowsToWorkspace(mount,view);
  const floatingHost=moveFloatingParameterDiagramsToWorkspace(mount,view) ?? floatingParameterDiagramHost(mount);
  const parse=html=>{
    const template=mount.ownerDocument.createElement("template");
    template.innerHTML=html;return template.content;
  };
  for(const [pane,html] of [[leftPane,left],[rightPane,stripFloatingEditorWindows(mount,right)]]) {
    const next=pane.cloneNode(false);
    next.append(parse(html));
    patchDomNode(pane,next);
  }
  // Patch only HTML-owned HUD nodes. Never touch renderer/canvas/view cube.
  const selectors=product?["[data-tube-designer-runtime-status]","[data-tube-designer-specification-tree]","[data-tube-designer-scene-settings]"]:view.activeAreaId==="tools"?[".tube-tool-library-hud"]:
    view.activeAreaId==="assemblies"?[".tube-assembly-preview-pane > header",".tube-connection-library-hud","[data-tube-assembly-preview-progress]"]:
    [".tube-profile-library-preview-hud",".tube-profile-library-preview-wait","[data-tube-designer-specification-tree]"];
  const fragment=parse(overlay);
  if(view.activeAreaId==="assemblies") {
    const currentPane=viewport.querySelector(".tube-assembly-preview-pane");
    const nextPane=fragment.querySelector(".tube-assembly-preview-pane");
    if(currentPane&&nextPane)currentPane.setAttribute("aria-label",nextPane.getAttribute("aria-label")??"");
  }
  for(const selector of selectors) {
    const old=viewport.querySelector(selector),next=fragment.querySelector(selector);
    if(old&&next)patchDomNode(old,next);
    else if(old)old.remove();
    else if(next)viewport.append(next);
  }
  // Diagram windows live above the entire workbench, not inside the WebGL
  // viewport. Patch them in place so focus, selection and nested scroll survive.
  const floatingSelector=product?"":view.activeAreaId==="tools"?"[data-tube-tool-diagram-dock]":view.activeAreaId==="profiles"?"[data-tube-profile-diagram-dock]":view.activeAreaId==="assemblies"?"[data-tube-assembly-diagram-dock]":"";
  const oldFloating=floatingSelector?floatingHost?.querySelector(floatingSelector):null;
  const nextFloating=floatingSelector?fragment.querySelector(floatingSelector):null;
  if(oldFloating&&nextFloating)patchDomNode(oldFloating,nextFloating);
  else if(oldFloating)oldFloating.remove();
  else if(nextFloating)floatingHost?.append(nextFloating);
  moveFloatingParameterDiagramsToWorkspace(mount,view);
  const workbench=mount.querySelector(".cam-workbench");
  if(product) {
    const oldDock=workbench?.querySelector(":scope > .tube-designer-product-parts-dock");
    const nextDock=parse(suffix).querySelector(".tube-designer-product-parts-dock");
    if(oldDock&&nextDock)patchDomNode(oldDock,nextDock);
    else if(oldDock)oldDock.remove();
    else if(nextDock)workbench?.append(nextDock);
    const oldProgress=workbench?.querySelector(":scope > [data-tube-designer-operation-wait]");
    const nextProgress=parse(suffix).querySelector("[data-tube-designer-operation-wait]");
    if(oldProgress&&nextProgress)patchDomNode(oldProgress,nextProgress);
    else if(oldProgress)oldProgress.remove();
    else if(nextProgress)workbench?.append(nextProgress);
  }
  for(const kind of ["notice","error"]) {
    let node=workbench?.querySelector(":scope > .cam-status."+kind);
    const value=view[kind];
    if(!value){node?.remove();continue;}
    if(!node){node=mount.ownerDocument.createElement("div");node.className="cam-status "+kind;workbench?.append(node);}
    node.textContent=String(value);
  }
  patchFloatingEditorWindows(mount,right+suffix,view);
  rememberLibraryDom(view,mount,suffix,sceneProxy);
  return true;
}
