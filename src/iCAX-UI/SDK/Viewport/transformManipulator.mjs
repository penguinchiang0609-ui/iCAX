import * as THREE from '../ThirdParty/three/three.module.js';
import { TransformControls } from '../ThirdParty/three/TransformControls.js';
import { styleTransformGizmo, updateRotationGizmoAppearance } from './transformGizmoAppearance.mjs';
import {snapTranslation,snapRotation} from './transformSnapping.mjs';

// Neutral display/gesture adapter. The product owns the draft and commit.
// Pointer routing belongs to the viewport so handles never also select/orbit.
export class TransformManipulator {
  constructor(viewport, render) {
    this.viewport = viewport; this.render = render;
    this.proxy = new THREE.Object3D();
    this.proxy.name = 'Transform gesture reference';
    this.control = new TransformControls(viewport.camera);
    // No addon DOM listeners: the viewport forwards each owned event once.
    this.control.domElement = viewport.renderer.domElement;
    this.control.showXY = this.control.showXZ = this.control.showYZ = false;
    this.control.showE = this.control.showXYZE = false;
    this.control.showXYZ = false;
    this.control.setColors(0xe65b59, 0x48ad68, 0x5389ee, 0xf4bf46);
    this.helper = this.control.getHelper();
    styleTransformGizmo(this.control);
    viewport.scene.add(this.proxy, this.helper);
    const segments=[[-1,0,0],[1,0,0],[0,-1,0],[0,1,0],[-.7,-.7,0],[.7,-.7,0],[.7,-.7,0],[.7,.7,0],[.7,.7,0],[-.7,.7,0],[-.7,.7,0],[-.7,-.7,0]];
    this.snapMarker=new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(segments.map(p=>new THREE.Vector3(...p))),new THREE.LineBasicMaterial({color:0xf4bf46,depthTest:false,depthWrite:false}));
    this.snapMarker.visible=false;this.snapMarker.renderOrder=130;this.snapMarker.raycast=()=>{};
    viewport.scene.add(this.snapMarker);
    this.control.attach(this.proxy);
    this.control.addEventListener('change', () => { if (!this.syncing) this.render(); });
    this.control.addEventListener('objectChange', () => {
      if (this.syncing || !this.start) return;
      this.applySnap();
      if (this.options.uniformScale && this.control.mode === 'scale') {
        const axis = this.control.axis?.[0]?.toLowerCase();
        const factor = Math.max(1e-6, this.proxy.scale[axis] / this.start.scale[axis]);
        this.proxy.scale.copy(this.start.scale).multiplyScalar(factor);
        this.proxy.updateMatrixWorld(true);
      }
      this.options.onChange?.({ axis: this.control.axis,
        translation: this.proxy.position.clone().sub(this.start.position).toArray(),
        scale: this.proxy.scale.clone().divide(this.start.scale).toArray(),
        angle: this.control.rotationAngle,snap:this.snap });
    });
  }

  worldUnitsPerPixel(point) {
    const camera=this.viewport.camera,height=Math.max(1,this.viewport.renderer.domElement.clientHeight);
    return camera.isOrthographicCamera?(camera.top-camera.bottom)/camera.zoom/height:
      2*camera.position.distanceTo(point)*Math.tan(camera.fov*Math.PI/360)/camera.zoom/height;
  }

  applySnap() {
    this.snap=null;this.snapMarker.visible=false;
    if(this.dragEvent?.altKey||!this.snapOptions)return;
    const c=this.control,options=this.snapOptions;
    if(c.mode==='translate'&&options.translation){
      const reference=new THREE.Vector3(...options.translation.reference).add(this.proxy.position.clone().sub(this.start.position));
      const basis=[...c.axis].filter(axis=>'XYZ'.includes(axis)).map(axis=>{
        const v=new THREE.Vector3(...({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]})[axis]);
        if(c.space==='local')v.applyQuaternion(this.start.quaternion);return v.toArray();
      });
      const camera=this.viewport.camera,canvas=this.viewport.renderer.domElement;
      const result=snapTranslation({...options.translation,point:reference.toArray(),basis,
        maxDistance:this.worldUnitsPerPixel(reference)*(options.translation.tolerancePx??8)*2,
        project:point=>{const p=new THREE.Vector3(...point).project(camera);return p.z>=-1&&p.z<=1?[(p.x+1)*canvas.clientWidth/2,(1-p.y)*canvas.clientHeight/2]:null;}});
      if(result){this.proxy.position.add(new THREE.Vector3(...result.correction));this.snap=result;}
    }else if(c.mode==='rotate'&&options.rotation){
      const angle=snapRotation(c.rotationAngle,options.rotation.step,options.rotation.tolerance);
      if(angle!==null){
        c.rotationAngle=angle;
        const q=new THREE.Quaternion().setFromAxisAngle(c.rotationAxis,angle);
        this.proxy.quaternion.copy(this.start.quaternion);
        if(c.space==='local'&&c.axis!=='E'&&c.axis!=='XYZE')this.proxy.quaternion.multiply(q);else this.proxy.quaternion.premultiply(q);
        this.proxy.quaternion.normalize();this.snap={kind:'angle',angle,point:this.proxy.position.toArray()};
      }
    }
    this.proxy.updateMatrixWorld(true);
    if(this.snap){this.snapMarker.position.fromArray(this.snap.point);this.snapMarker.visible=true;}
  }

  moveControl(event){
    const c=this.control;
    if(c.mode!=='rotate'){c.pointerMove(this.pointer(event,-1));return;}
    // Right/up increase, left/down decrease. Use the signed displacement
    // with the greater magnitude, consistently in every view and frame.
    const dx=event.clientX-this.startPointer.x,up=this.startPointer.y-event.clientY;
    c.rotationAngle=(Math.abs(dx)>=Math.abs(up)?dx:up)*Math.PI/360;
    const q=new THREE.Quaternion().setFromAxisAngle(c.rotationAxis,c.rotationAngle);
    this.proxy.quaternion.copy(this.start.quaternion);
    if(c.space==='local'&&c.axis!=='E')this.proxy.quaternion.multiply(q);else this.proxy.quaternion.premultiply(q);
    this.proxy.quaternion.normalize();this.proxy.updateMatrixWorld(true);
    c.dispatchEvent({type:'objectChange'});c.dispatchEvent({type:'change'});
  }

  configure(options) {
    this.options = options;
    this.syncing = true;
    this.control.enabled = options.enabled !== false;
    this.control.setMode(options.mode);
    for (const axis of ['X','Y','Z']) this.control['show'+axis] = !options.axes || options.axes.includes(axis);
    this.control.showE = options.mode === 'rotate' && options.screenRotate === true;
    this.control.showXY = this.control.showXZ = this.control.showYZ = options.mode === 'scale' && !options.uniformScale
      || options.mode === 'translate' && options.planeTranslate === true;
    this.control.showXYZ = options.mode === 'scale' || options.mode === 'translate' && options.freeTranslate === true;
    // position is an editing pivot, not the world origin. TransformControls keeps
    // world handle directions fixed and applies proxy orientation only in local mode.
    this.control.setSpace(options.mode === 'scale' ? 'local' : options.space ?? 'world');
    if (!this.control.dragging) {
      this.proxy.position.fromArray(options.position);
      this.proxy.quaternion.fromArray(options.quaternion ?? [0,0,0,1]);
      this.proxy.scale.set(1,1,1);
      this.proxy.updateMatrixWorld(true);
    }
    this.syncing = false;
  }

  update() {
    const vp = this.viewport, c = this.control;
    this.syncing = true;
    c.camera = vp.camera;
    // One geometry unit has the same CSS-pixel size in both projections:
    // default arrows are 96 px long and rotation rings are 160 px across.
    const height = Math.max(1,vp.renderer.domElement.clientHeight);
    const sizePixels = Number.isFinite(this.options?.sizePixels) ? this.options.sizePixels : 160;
    c.size = (vp.camera.isOrthographicCamera ? 4 : 8/1.9) * sizePixels / height;
    this.helper.updateMatrixWorld(true);
    if(this.snapMarker.visible){this.snapMarker.quaternion.copy(vp.camera.quaternion);this.snapMarker.scale.setScalar(this.worldUnitsPerPixel(this.snapMarker.position)*7);}
    if (c.mode === 'rotate') updateRotationGizmoAppearance(c, this.options.activeAxis?.toUpperCase());
    this.syncing = false;
    const p = this.proxy.position.clone().project(vp.camera), r = vp.renderer.domElement.getBoundingClientRect();
    this.options?.onPosition?.({ x:(p.x+1)*r.width/2, y:(1-p.y)*r.height/2,
      visible:p.z>=-1&&p.z<=1, width:r.width, height:r.height });
  }

  pointer(event, button = event.button) {
    const rect = this.viewport.renderer.domElement.getBoundingClientRect();
    return { x:(event.clientX-rect.left)/rect.width*2-1, y:1-(event.clientY-rect.top)/rect.height*2, button };
  }

  hover(event) {
    if (!this.control.enabled || event.buttons || this.control.dragging) return false;
    this.update();
    if (this.control.mode === 'rotate') {
      const ray = this.control.getRaycaster(); ray.setFromCamera(this.pointer(event), this.viewport.camera);
      const hit = ray.intersectObjects(this.control._gizmo.picker.rotate.children, false).find(hit => hit.object.visible
        && (hit.object.name === 'E' || hit.point.clone().sub(this.proxy.position).dot(this.control.eye) >= -1e-7));
      this.control.axis = hit?.object.name ?? null;
    } else this.control.pointerHover(this.pointer(event));
    const axes = ['X','Y','Z'].filter(axis => this.control['show'+axis]);
    if (this.control.showXY) axes.push('XY','XZ','YZ');
    if (this.control.showXYZ) axes.push('XYZ');
    if (this.control.showE) axes.push('E');
    if (!axes.includes(this.control.axis)) this.control.axis = null;
    this.viewport.renderer.domElement.style.cursor = this.control.axis ? 'grab' : '';
    return !!this.control.axis;
  }

  down(event) {
    if (this.pointerId != null) return true;
    if (event.button !== 0 || !this.control.enabled) return false;
    // pointerdown already carries buttons=1; hover must run with buttons=0.
    this.hover({clientX:event.clientX,clientY:event.clientY,button:0,buttons:0});
    const axis = this.control.axis;
    if (!axis) return false;
    // A product may still be committing the previous gesture. Consume the
    // handle press without capturing a second pointer or moving its proxy.
    if (this.options.onStart?.({axis, direction:this.control.eye.toArray()}) === false) {
      event.preventDefault(); return true;
    }
    this.update(); this.control.axis = axis;
    this.start = {position:this.proxy.position.clone(),quaternion:this.proxy.quaternion.clone(),scale:this.proxy.scale.clone()};
    this.snapOptions=this.options.snap?structuredClone(this.options.snap):null;
    this.startPointer = {x:event.clientX,y:event.clientY}; this.moved = false;
    if(this.control.mode==='rotate'){
      this.control.rotationAngle=0;
      this.control.rotationAxis.fromArray(axis==='E'?this.control.eye.toArray():({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]})[axis]);
      this.control.dragging=true;
    }else this.control.pointerDown(this.pointer(event,0));
    this.pointerId = event.pointerId;
    const canvas = this.viewport.renderer.domElement;
    canvas.focus({preventScroll:true}); canvas.setPointerCapture(event.pointerId); canvas.style.cursor='grabbing';
    event.preventDefault();
    return true;
  }

  move(event) {
    if (this.pointerId == null) return this.hover(event);
    if (event.pointerId === this.pointerId) {
      const distance=this.control.mode==='rotate'?Math.max(Math.abs(event.clientX-this.startPointer.x),Math.abs(event.clientY-this.startPointer.y)):Math.hypot(event.clientX-this.startPointer.x,event.clientY-this.startPointer.y);
      if (!this.moved && distance<3) return true;
      this.moved = true;
      this.dragEvent=event;
      this.moveControl(event); event.preventDefault();
    }
    return true;
  }

  up(event) {
    if (this.pointerId == null || event.pointerId !== this.pointerId) return false;
    this.finish(false); event.preventDefault(); return true;
  }

  modifier(event) {
    if(event.key!=='Alt'||this.pointerId==null||!this.dragEvent||!this.snapOptions)return false;
    this.dragEvent={clientX:this.dragEvent.clientX,clientY:this.dragEvent.clientY,altKey:event.type==='keydown'};
    this.moveControl(this.dragEvent);
    event.preventDefault();return true;
  }

  finish(cancel) {
    if (this.pointerId == null) return false;
    const id = this.pointerId, axis = this.control.axis;
    this.pointerId = null;
    this.control.dragging = false; this.control.axis = null;
    if (cancel && this.start) {
      this.proxy.position.copy(this.start.position); this.proxy.quaternion.copy(this.start.quaternion);
      this.proxy.scale.copy(this.start.scale);
      this.proxy.updateMatrixWorld(true);
    }
    const canvas = this.viewport.renderer.domElement;
    if (canvas.hasPointerCapture(id)) canvas.releasePointerCapture(id);
    canvas.style.cursor = '';
    this.start = null;
    this.snap=null;this.snapOptions=null;this.dragEvent=null;this.snapMarker.visible=false;
    if (cancel) this.options.onCancel?.(); else this.options.onEnd?.({axis,moved:this.moved});
    this.render(); return true;
  }

  dispose() {
    // Disposal is driven by the product's cancellation/lifecycle; no late callback.
    this.options = {}; this.finish(true);
    this.helper.removeFromParent(); this.proxy.removeFromParent();this.snapMarker.removeFromParent();
    this.snapMarker.geometry.dispose();this.snapMarker.material.dispose();
    this.control.dispose();
    this.viewport.renderer.domElement.style.cursor = '';
  }
}
