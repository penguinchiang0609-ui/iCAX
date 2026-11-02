export function attachViewCubeNavigation(host, viewport) {
  let drag = null, suppressClick = false;
  const listeners = [];
  const on = (target,type,handler,options) => {
    target.addEventListener(type,handler,options);
    listeners.push(()=>target.removeEventListener(type,handler,options));
  };
  const stop = event => {event.preventDefault();event.stopPropagation();};
  const angleAt = event => {
    const p = new DOMPoint(event.clientX,event.clientY).matrixTransform(drag.inverse);
    const x = (p.x-drag.cx)/drag.rx, y = (p.y-drag.cy)/drag.ry;
    // Near the centre, angular motion is undefined. Re-anchor on exit instead
    // of producing a sudden half-turn when crossing the middle of the widget.
    return Math.hypot(x,y) < .25 ? null : Math.atan2(y,x);
  };
  const finish = (cancel = false, activate = false) => {
    if (!drag) return;
    const previous = drag; drag = null;
    delete host.dataset.compassDragging;
    delete host.dataset.navigationDragging;
    if (host.hasPointerCapture(previous.pointerId)) host.releasePointerCapture(previous.pointerId);
    if (cancel) viewport.setCameraState(previous.camera);
    else if (activate && previous.mode === 'orbit' && !previous.moved && previous.view)
      viewport.setStandardView(previous.view);
  };
  on(host,'pointerdown',event => {
    if (drag) {stop(event);return;}
    suppressClick = false;
    const ring = event.target.closest?.('[data-cam-compass]');
    if (event.button !== 0) return;
    const matrix = ring?.ownerSVGElement.getScreenCTM();
    if (ring && !matrix) return;
    stop(event);
    drag = {pointerId:event.pointerId, camera:viewport.getCameraState(),pivot:viewport.captureOrbitPivot(), mode:ring?'compass':'orbit',
      view:event.target.closest?.('[data-cam-view]')?.dataset.camView,
      x:event.clientX,y:event.clientY,moved:false};
    if(ring){
      Object.assign(drag,{inverse:matrix.inverse(),cx:Number(ring.dataset.cx),cy:Number(ring.dataset.cy),
        rx:Number(ring.dataset.rx),ry:Number(ring.dataset.ry),sign:Number(ring.dataset.sign),last:null,delta:0});
      drag.last=angleAt(event);host.dataset.compassDragging='true';
    }
    suppressClick = true;
    host.dataset.navigationDragging=drag.mode;
    // The SVG is replaced as the camera turns; capture on its persistent host.
    host.setPointerCapture(event.pointerId);
  });
  on(host,'pointermove',event => {
    if (!drag || drag.pointerId !== event.pointerId) return;
    stop(event);
    if(drag.mode==='orbit'){
      const dx=event.clientX-drag.x,dy=event.clientY-drag.y;
      if(!drag.moved&&Math.hypot(dx,dy)<3)return;
      drag.moved=true;
      const speed=Number(viewport.options.orbitSpeed??.005);
      viewport.setOrbitCameraState(drag.camera,{theta:drag.camera.theta-dx*speed,phi:drag.camera.phi-dy*speed},drag.pivot);
      return;
    }
    const angle = angleAt(event);
    if (angle != null && drag.last != null) {
      drag.delta += Math.atan2(Math.sin(angle-drag.last),Math.cos(angle-drag.last));
      viewport.setOrbitCameraState(drag.camera,{theta:drag.camera.theta+drag.delta*drag.sign},drag.pivot);
    }
    drag.last = angle;
  });
  on(host,'pointerup',event => {if(drag?.pointerId===event.pointerId){stop(event);finish(false,true);}});
  on(host,'pointercancel',event => {if(drag?.pointerId===event.pointerId){stop(event);finish(true);}});
  on(host,'lostpointercapture',()=>finish());
  on(host.ownerDocument.defaultView,'blur',()=>finish());
  on(host.ownerDocument.defaultView,'keydown',event => {
    if(drag && event.key==='Escape'){stop(event);event.stopImmediatePropagation();finish(true);}
  },true);
  on(host,'keydown',event => {
    if (drag || !event.target.closest?.('[data-cam-compass]') || !['ArrowLeft','ArrowRight'].includes(event.key)) return;
    stop(event);suppressClick = false;
    const camera = viewport.getCameraState();
    viewport.setCameraState({...camera,theta:camera.theta+(event.key==='ArrowLeft'?-1:1)*(event.shiftKey?15:5)*Math.PI/180});
  });
  on(host,'click',event => {
    if(suppressClick || event.target.closest?.('[data-cam-compass]')){stop(event);suppressClick=false;}
  },true);
  on(host,'dblclick',event => {if(event.target.closest?.('[data-cam-compass]'))stop(event);});
  on(host,'contextmenu',event => {if(event.target.closest?.('[data-cam-compass]'))stop(event);});
  return () => {finish();listeners.forEach(remove=>remove());};
}
