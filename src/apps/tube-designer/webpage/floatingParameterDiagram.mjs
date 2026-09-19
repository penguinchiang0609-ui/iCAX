// Shared, non-modal diagram windows. Dragging changes only presentation state.
import { patchDomNode } from './punchDomPatch.mjs';
const bindings = new WeakMap();
const dragBindings = new WeakMap();
const PANEL_SELECTOR = '[data-floating-parameter-diagram], [data-library-floating-diagram]';
const DEFAULT_DIAGRAM_WIDTH = 360;
const DEFAULT_DIAGRAM_HEIGHT = 405;

export function floatingParameterDiagramHost(mount, create = true) {
  const workbench = mount?.querySelector?.('.cam-workbench');
  if (!workbench) return null;
  let host = workbench.querySelector(':scope > [data-floating-parameter-diagram-layer]');
  if (!host && create) {
    host = mount.ownerDocument.createElement('div');
    host.className = 'tube-floating-parameter-diagram-layer';
    host.dataset.floatingParameterDiagramLayer = '';
    workbench.append(host);
  }
  return host;
}

function diagramState(view, panel) {
  const library = panel?.dataset?.libraryFloatingDiagram;
  if (library) return ((view.tubeDesignerLibraryDiagramPositions ??= {})[library] ??= {});
  return view?.tubeDesignerFloatingToolDiagram ?? null;
}

function applyWorkspaceGeometry(panel, host, state, fallbackRect = null) {
  if (!panel || !host || !state) return;
  const parent = host.getBoundingClientRect();
  const width = Number.isFinite(state.width) ? state.width : (fallbackRect?.width ?? panel.offsetWidth);
  const height = Number.isFinite(state.height) ? state.height : (fallbackRect?.height ?? panel.offsetHeight);
  const rawX = state.coordinateSpace === 'workbench' && Number.isFinite(state.x)
    ? state.x : (fallbackRect?.left ?? parent.left) - parent.left;
  const rawY = state.coordinateSpace === 'workbench' && Number.isFinite(state.y)
    ? state.y : (fallbackRect?.top ?? parent.top) - parent.top;
  const x = Math.max(0, Math.min(Math.max(0, host.clientWidth - width), rawX));
  const y = Math.max(0, Math.min(Math.max(0, host.clientHeight - height), rawY));
  Object.assign(state, { x, y, width, height, coordinateSpace: 'workbench' });
  Object.assign(panel.style, {
    left: '0px', top: '0px', right: 'auto',
    transform: `translate3d(${x}px,${y}px,0)`,
    width: `${width}px`, height: `${height}px`,
  });
}

function initialDiagramRect(mount, panel, state, measured) {
  if (Number.isFinite(state?.x) || Number.isFinite(state?.y)) {
    if (measured?.width > 0 && measured?.height > 0) return measured;
  }
  const host = floatingParameterDiagramHost(mount,false)?.getBoundingClientRect();
  const info = mount.querySelector('.cam-info-pane')?.getBoundingClientRect();
  if (host && info?.width > 0 && info?.height > 0) {
    const width = Math.min(DEFAULT_DIAGRAM_WIDTH, Math.max(0,info.width-24), Math.max(0,host.width-16));
    const height = Math.min(DEFAULT_DIAGRAM_HEIGHT, Math.max(0,info.height-24), Math.max(0,host.height-16));
    return {
      left: info.left + Math.max(0,(info.width-width)/2),
      top: info.top + Math.max(0,(info.height-height)/2),
      width,
      height,
    };
  }
  const viewport = mount.querySelector('.cam-viewport')?.getBoundingClientRect();
  if (!viewport) return measured;
  const library = !!panel.dataset.libraryFloatingDiagram;
  const width = Number.isFinite(state?.width) ? state.width : DEFAULT_DIAGRAM_WIDTH;
  const height = Number.isFinite(state?.height) ? state.height : DEFAULT_DIAGRAM_HEIGHT;
  const legacyX = Number.isFinite(state?.x) ? state.x : (library ? Math.max(0, viewport.width - width) : 12);
  const legacyY = Number.isFinite(state?.y) ? state.y : (library ? 145 : 120);
  return {
    left: viewport.left + legacyX,
    top: viewport.top + legacyY,
    width,
    height,
  };
}

export function moveFloatingParameterDiagramsToWorkspace(mount, view) {
  const host = floatingParameterDiagramHost(mount);
  if (!host) return null;
  const panels = [...mount.querySelectorAll(PANEL_SELECTOR)];
  for (const panel of panels) {
    const state = diagramState(view, panel);
    if (!state) continue;
    if (panel.parentElement === host) {
      applyWorkspaceGeometry(panel, host, state);
      continue;
    }
    const fallbackRect = initialDiagramRect(mount,panel,state,panel.getBoundingClientRect());
    host.append(panel);
    applyWorkspaceGeometry(panel, host, state, fallbackRect);
  }
  return host;
}
export function diagramSizeStyle(state) {
  return Number.isFinite(state?.width) && Number.isFinite(state?.height)
    ? `;width:${state.width}px;height:${state.height}px` : '';
}
export function diagramPositionStyle(state, fallbackX = 0, fallbackY = 0) {
  const x = Number.isFinite(state?.x) ? Math.max(0, state.x) : Math.max(0, fallbackX);
  const y = Number.isFinite(state?.y) ? Math.max(0, state.y) : Math.max(0, fallbackY);
  return `left:0;top:0;right:auto;transform:translate3d(${x}px,${y}px,0)${diagramSizeStyle(state)}`;
}
export function renderDiagramResizeHandles() {
  return ['n','s','e','w','ne','nw','se','sw'].map(edge => `<span class="tube-diagram-resize is-${edge}" data-diagram-resize="${edge}" aria-hidden="true"></span>`).join('');
}
export function libraryDiagramPositionStyle(view,key) {
  const position=view?.tubeDesignerLibraryDiagramPositions?.[key];
  if(!Number.isFinite(position?.x)||!Number.isFinite(position?.y))return '';
  return diagramPositionStyle(position);
}
export function refreshFloatingParameterDiagram(mount,view,render) {
  const host=floatingParameterDiagramHost(mount);if(!host)return;
  const old=host.querySelector('[data-floating-parameter-diagram]');
  const template=mount.ownerDocument.createElement('template');template.innerHTML=render(view);
  const next=template.content.firstElementChild;
  if(old&&next)patchDomNode(old,next);
  else if(old)old.remove();else if(next){
    // Render once in the viewport coordinate space so legacy/default positions
    // are converted exactly, then move the panel to the workbench overlay in
    // the same task before the browser paints it.
    const viewport=mount.querySelector('.cam-viewport');
    (viewport ?? host).append(next);
  }
  moveFloatingParameterDiagramsToWorkspace(mount,view);
  mount.dispatchEvent(new CustomEvent('parameter-diagram-window-updated'));
}
export function bindFloatingParameterDiagram(mount, view, render) {
  if (!mount) return;
  moveFloatingParameterDiagramsToWorkspace(mount,view);
  bindDiagramDragging(mount,view);
  const existing=bindings.get(mount);
  if(existing){ existing.view=view; existing.render=render; return; }
  const binding={view,render};
  bindings.set(mount,binding);
  const update=()=>{
    refreshFloatingParameterDiagram(mount,binding.view,binding.render);
  };
  mount.addEventListener('click',event=>{
    const open=event.target.closest('[data-product-tool-diagram-open]');
    const close=event.target.closest('[data-floating-diagram-close]');
    if(!open&&!close)return;
    const productId=String(binding.view.scene?.tubeDesigner?.product?.entityId ?? '');
    if(open){
      const previous=binding.view.tubeDesignerFloatingToolDiagram;
      binding.view.tubeDesignerFloatingToolDiagram={
        ...(previous?.productId===productId?previous:{}),productId,
        fieldKey:open.dataset.productToolDiagramOpen,open:true};
    }else if(binding.view.tubeDesignerFloatingToolDiagram){binding.view.tubeDesignerFloatingToolDiagram.open=false;}
    update();
  });
}

export function bindDiagramDragging(mount,view) {
  if(!mount)return;
  moveFloatingParameterDiagramsToWorkspace(mount,view);
  const existing=dragBindings.get(mount);if(existing){existing.view=view;return;}
  const binding={view};dragBindings.set(mount,binding);
  mount.addEventListener('pointerdown',event=>{
    const header=event.target.closest('[data-floating-diagram-drag]');
    const resize=event.target.closest('[data-diagram-resize]');
    const handle=resize ?? header;
    if(!handle||event.button!==0||event.target.closest('button'))return;
    const panel=handle.closest(PANEL_SELECTOR), host=floatingParameterDiagramHost(mount,false);
    if(!panel||!host)return;
    if(panel.hidden)return;
    event.preventDefault(); event.stopPropagation();
    const start=panel.getBoundingClientRect(), parent=host.getBoundingClientRect();
    const pointer={id:event.pointerId,x:event.clientX,y:event.clientY};
    const library=panel.dataset.libraryFloatingDiagram;
    const state=library
      ? ((binding.view.tubeDesignerLibraryDiagramPositions ??= {})[library] ??= {})
      : binding.view.tubeDesignerFloatingToolDiagram;
    if(!state)return;
    const edge=resize?.dataset.diagramResize;
    handle.setPointerCapture(event.pointerId);
    const move=e=>{
      if(e.pointerId!==pointer.id)return;
      if(!panel.isConnected)return;
      if(edge){
        const dx=e.clientX-pointer.x,dy=e.clientY-pointer.y;
        let left=start.left-parent.left,top=start.top-parent.top,right=left+start.width,bottom=top+start.height;
        const minWidth=Math.min(240,host.clientWidth),minHeight=Math.min(160,host.clientHeight);
        if(edge.includes('w'))left=Math.max(0,Math.min(right-minWidth,left+dx));
        if(edge.includes('e'))right=Math.min(host.clientWidth,Math.max(left+minWidth,right+dx));
        if(edge.includes('n'))top=Math.max(0,Math.min(bottom-minHeight,top+dy));
        if(edge.includes('s'))bottom=Math.min(host.clientHeight,Math.max(top+minHeight,bottom+dy));
        Object.assign(state,{x:left,y:top,width:right-left,height:bottom-top});
        Object.assign(panel.style,{left:'0px',top:'0px',right:'auto',transform:`translate3d(${left}px,${top}px,0)`,width:`${state.width}px`,height:`${state.height}px`});
        return;
      }
      const x=Math.max(0,Math.min(host.clientWidth-panel.offsetWidth,start.left-parent.left+e.clientX-pointer.x));
      const y=Math.max(0,Math.min(host.clientHeight-panel.offsetHeight,start.top-parent.top+e.clientY-pointer.y));
      state.x=x; state.y=y;
      Object.assign(panel.style,{left:'0px',top:'0px',right:'auto',transform:`translate3d(${x}px,${y}px,0)`});
    };
    const end=e=>{
      if(e.pointerId!==pointer.id)return;
      handle.removeEventListener('pointermove',move);handle.removeEventListener('pointerup',end);
      handle.removeEventListener('pointercancel',end);handle.removeEventListener('lostpointercapture',end);
      if(handle.hasPointerCapture(pointer.id))handle.releasePointerCapture(pointer.id);
    };
    handle.addEventListener('pointermove',move);handle.addEventListener('pointerup',end);
    handle.addEventListener('pointercancel',end);handle.addEventListener('lostpointercapture',end);
  });
}
