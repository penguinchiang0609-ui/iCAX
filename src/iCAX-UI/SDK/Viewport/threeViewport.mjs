import * as THREE from "../ThirdParty/three/three.module.js";
import { TransformManipulator } from './transformManipulator.mjs';
import { OrbitAxisSnap } from './orbitAxisSnap.mjs';
import {sampleOrbitPivot,orbitCameraState} from './orbitPivot.mjs';
import {prepareMeshPicking} from './meshPicking.mjs';
import { placeSpecificationCallout, layoutSpecificationCallouts } from "./specificationCalloutLayout.mjs";
import { createMeshFeatureEdgesGeometry } from "./meshFeatureEdges.mjs";
import { loadRenderTexture } from "./renderTexture.mjs";
import {
  ColliderFlags,
  ColliderPDOEvents,
  ColliderPDOLayout,
  ColliderShapeKind,
  parseColliderPDOEvent,
  parseColliderPDOPayload,
} from "./colliderPDO.mjs";
import {
  loadRenderResource,
  loadRenderResources,
  RenderFlags,
  RenderGeometryKind,
  RenderLayers,
  rgbaToCssColor,
} from "./renderResource.mjs";

const DEFAULT_CAMERA = Object.freeze({
  eye: { x: 260, y: -320, z: 220 },
  target: { x: 0, y: 0, z: 0 },
  up: { x: 0, y: 0, z: 1 },
});

const ENGINE_CAMERA_TO_THREE_CAMERA_MATRIX = new THREE.Matrix4().fromArray([
  0, -1, 0, 0,
  0, 0, 1, 0,
  -1, 0, 0, 0,
  0, 0, 0, 1,
]);
const SELECTION_AXIS_PIXEL_SIZE = 94;
const SELECTION_AXIS_MIN_SCALE = 0.001;
const DEFAULT_SELECTION_VISUAL = Object.freeze({
  color: 0xffad1f,
  emissive: 0xd47700,
  emissiveIntensity: 0.68,
  linewidth: 2,
});
const DEFAULT_EMPHASIS_VISUAL = Object.freeze({
  color: 0xa855f7,
  emissive: 0x6b21a8,
  emissiveIntensity: 0.78,
  linewidth: 2,
});

function normalizeSelectionVisual(value = null) {
  const source = value && typeof value === "object" ? value : {};
  const finite = (candidate, fallback) => Number.isFinite(Number(candidate))
    ? Number(candidate) : fallback;
  return {
    color: finite(source.color, DEFAULT_SELECTION_VISUAL.color),
    emissive: finite(source.emissive, DEFAULT_SELECTION_VISUAL.emissive),
    emissiveIntensity: finite(source.emissiveIntensity, DEFAULT_SELECTION_VISUAL.emissiveIntensity),
    linewidth: finite(source.linewidth, DEFAULT_SELECTION_VISUAL.linewidth),
  };
}

function normalizeEmphasisVisual(value = null) {
  const source = value && typeof value === "object" ? value : {};
  const finite = (candidate, fallback) => Number.isFinite(Number(candidate))
    ? Number(candidate) : fallback;
  return {
    color: finite(source.color, DEFAULT_EMPHASIS_VISUAL.color),
    emissive: finite(source.emissive, DEFAULT_EMPHASIS_VISUAL.emissive),
    emissiveIntensity: finite(source.emissiveIntensity, DEFAULT_EMPHASIS_VISUAL.emissiveIntensity),
    linewidth: finite(source.linewidth, DEFAULT_EMPHASIS_VISUAL.linewidth),
  };
}

function specificationScreenOffset(annotation) {
  const offset=annotation?.screenOffset;
  return Array.isArray(offset)&&offset.length===2&&offset.every(Number.isFinite)?offset:[0,0];
}
function specificationAnnotationSignature(annotations) {
  return JSON.stringify((Array.isArray(annotations) ? annotations : []).map((annotation) => {
    const vector = (value) => {
      const point = toFiniteVector3(value);
      return point ? [point.x, point.y, point.z] : null;
    };
    const editor = annotation?.editor;
    return {
      id: String(annotation?.id ?? ""),
      parameter: String(annotation?.parameter ?? ""),
      kind: String(annotation?.kind ?? ""),
      label: String(annotation?.label ?? ""),
      oldValue: annotation?.oldValue,
      newValue: annotation?.newValue,
      cleanLabel: String(annotation?.cleanLabel ?? ""),
      changedLabel: String(annotation?.changedLabel ?? ""),
      start: vector(annotation?.start),
      end: vector(annotation?.end),
      offset: vector(annotation?.offset),
      screenOffset: specificationScreenOffset(annotation),
      color: Number(annotation?.color ?? 0x27c27a),
      pending: Boolean(annotation?.pending),
      active: Boolean(annotation?.active),
      editing: Boolean(annotation?.editing),
      editable: annotation?.editable !== false,
      pickThrough: Boolean(annotation?.pickThrough),
      editor: editor ? {
        type: String(editor.type ?? "text"),
        value: editor.value,
        min: editor.min,
        max: editor.max,
        step: editor.step,
        options: (Array.isArray(editor.options) ? editor.options : []).map((option) => ({
          value: option?.value,
          label: String(option?.label ?? option?.value ?? ""),
        })),
      } : null,
    };
  }));
}

function sameSpecificationValue(left, right) {
  if (left == null || right == null) return left == null && right == null;
  const leftNumber = Number(left);
  const rightNumber = Number(right);
  if (String(left).trim() !== "" && String(right).trim() !== ""
      && Number.isFinite(leftNumber) && Number.isFinite(rightNumber)) {
    return Math.abs(leftNumber - rightNumber) <= 1.0e-9;
  }
  return String(left) === String(right);
}
const STANDARD_VIEW_DIRECTIONS = Object.freeze({
  front: Object.freeze([0, -1, 0]),
  back: Object.freeze([0, 1, 0]),
  left: Object.freeze([-1, 0, 0]),
  right: Object.freeze([1, 0, 0]),
  top: Object.freeze([0, 0, 1]),
  bottom: Object.freeze([0, 0, -1]),
});

export function resolveStandardViewDirection(viewName) {
  const name = String(viewName ?? "").trim().toLowerCase();
  if (name === "iso" || name === "isometric") {
    return [1, -1, 0.78];
  }

  const tokens = name.split("-");
  if (!tokens.length || tokens.some((token) => !token)) {
    return null;
  }

  const direction = [0, 0, 0];
  const occupiedAxes = new Set();
  for (const token of tokens) {
    const component = STANDARD_VIEW_DIRECTIONS[token];
    if (!component) {
      return null;
    }
    const axis = component.findIndex((value) => value !== 0);
    if (occupiedAxes.has(axis)) {
      return null;
    }
    occupiedAxes.add(axis);
    direction[axis] = component[axis];
  }
  return direction;
}

export function createThreeViewport(options = {}) {
  return new ThreeRenderViewport(options);
}

export class ThreeRenderViewport {
  constructor(options = {}) {
    ensureThreeViewportStyles();
    this.options = options;
    this.displayMode = options.displayMode ?? null;
    this.showSurfaceEdges = options.showSurfaceEdges === true;
    this.continuousRendering = options.continuousRender !== false;
    this.pickingEnabled = options.pickingEnabled !== false;
    this.host = null;
    this.sceneProxy = null;
    this.subscriptions = [];
    this.slotDescriptors = new Map();
    this.colliderSlotDescriptors = new Map();
    this.geometryPayloads = new Map();
    this.geometryObjects = new Map();
    this.meshEdgeGeometries = new WeakMap();
    this.renderedDepthBounds = new WeakMap();
    this.materialPayloads = new Map();
    this.texturePayloads = new Map();
    this.texturePromises = new Map();
    this.desiredTextureUrls = new Set();
    this.textureGeneration = 0;
    this.resourcePromises = new Map();
    this.viewApplyGeneration = 0;
    this.appliedViewRevision = "0";
    this.renderSequence = 0;
    this.instancePayloads = new Map();
    this.transformPayloads = new Map();
    this.transformPreviews = new Map();
    this.colliderPayloads = new Map();
    this.colliderObjects = new Map();
    this.objectSlots = new Map();
    this.sceneObjects = new Map();
    this.cameras = new Map();
    this.activeCameraId = null;
    this.renderSceneId = options.renderSceneId == null
      ? null
      : String(options.renderSceneId);
    this.visibleLayerMask = Number(options.visibleLayerMask ?? RenderLayers.all) >>> 0;
    this.visibleEntityIds = options.visibleEntityIds == null
      ? null
      : new Set(normalizeEntityIds(options.visibleEntityIds));
    this.selectedObjectId = "";
    this.selectedObjectIds = new Set();
    this.defaultSelectionVisual = normalizeSelectionVisual(options.selectionVisual);
    this.selectionVisual = { ...this.defaultSelectionVisual };
    this.emphasizedObjectIds = new Set();
    this.emphasisDimOpacity = null;
    this.defaultEmphasisVisual = normalizeEmphasisVisual(options.emphasisVisual);
    this.emphasisVisual = { ...this.defaultEmphasisVisual };
    this.ghostPreview = null;
    this.diagnosticKeys = new Set();
    this.domListeners = [];
    this.isDefragging = false;
    this.isDisposed = false;
    this.dynamicReadPending = false;
    this.blankDoubleClickFitEnabled = options.blankDoubleClickFitEnabled === true;
    this.cameraNavigationEnabled = options.cameraNavigationEnabled !== false;
    this.pendingBlankPick = null;
    this.hoverFrame = null;
    this.pendingHoverEvent = null;
    this.hoveredObjectId = "";
    this.hoverPointerActive = false;
    this.navigation = {
      pointerId: null,
      mode: null,
      lastX: 0,
      lastY: 0,
      moved: false,
      pickAllowed: false,
    };
    this.rectangleSelectionKey = null;
    this.selectionRectangle = null;

    this.root = document.createElement("div");
    this.root.className = "icax-three-viewport";
    this.status = document.createElement("div");
    this.status.className = "icax-three-viewport-status";
    this.status.textContent = "等待渲染数据";
    this.root.appendChild(this.status);

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(options.backgroundColor ?? 0x182128);
    this.content = new THREE.Group();
    this.content.name = "iCAX View Resource Content";
    this.scene.add(this.content);
    this.measurementContent = new THREE.Group();
    this.measurementContent.name = "iCAX Measurement Overlay";
    this.scene.add(this.measurementContent);
    this.dimensionContent = new THREE.Group();
    this.dimensionContent.name = "iCAX Dimension Overlay";
    this.scene.add(this.dimensionContent);
    this.outsideDimensionLabels = [];
    this.specificationContent = new THREE.Group();
    this.specificationContent.name = "iCAX Interactive Specification Overlay";
    this.scene.add(this.specificationContent);
    this.specificationLabels = [];
    this.specificationAnnotationsSignature = "";
    // Editor state belongs to the viewport component.  The generated value is
    // the old value; newValue is what the user is currently typing.  Scene
    // refreshes may advance the old value after a commit, but must never
    // overwrite a dirty draft.
    this.specificationEditorValues = new Map();
    this.activeSpecificationEditor = "";
    this.pendingSpecificationAnnotations = null;
    this.colliderContent = new THREE.Group();
    this.colliderContent.name = "iCAX ColliderPDO Content";
    this.scene.add(this.colliderContent);
    this.selectionAxisHelper = this.#createSelectionAxisHelper();
    this.scene.add(this.selectionAxisHelper);

    this.projectionMode = normalizeProjectionMode(options.projectionMode);
    this.perspectiveFov = THREE.MathUtils.clamp(
      Number(options.perspectiveFov ?? 45), 1, 179,
    );
    this.viewportAspect = 1;
    this.camera = this.projectionMode === "orthographic"
      ? new THREE.OrthographicCamera(-1, 1, 1, -1, 0.1, 1000000)
      : new THREE.PerspectiveCamera(this.perspectiveFov, 1, 0.1, 1000000);
    this.camera.up.set(0, 0, 1);
    this.cameraState = {
      target: new THREE.Vector3(DEFAULT_CAMERA.target.x, DEFAULT_CAMERA.target.y, DEFAULT_CAMERA.target.z),
      radius: 470,
      theta: -0.88,
      phi: 1.08,
    };

    this.renderer = new THREE.WebGLRenderer({
      antialias: options.antialias !== false,
      alpha: false,
      // Close inspection can put the eye inside a hollow assembly's bounds.
      // Its near plane must then stay close to the eye; a conventional depth
      // buffer cannot resolve millimetre-separated surfaces farther inside it.
      logarithmicDepthBuffer: true,
    });
    const pixelRatioCap = Math.max(0.5, Number(options.pixelRatioCap ?? 2));
    const minimumPixelRatio = Math.max(.5, Number(options.minimumPixelRatio ?? .5));
    this.renderer.setPixelRatio(Math.min(Math.max(window.devicePixelRatio || 1, minimumPixelRatio), pixelRatioCap));
    this.renderer.domElement.className = "icax-three-viewport-canvas";
    this.renderer.domElement.tabIndex = 0;
    this.root.appendChild(this.renderer.domElement);
    this.dimensionLabelLayer = document.createElement("div");
    this.dimensionLabelLayer.className = "icax-three-dimension-label-layer";
    this.dimensionLeaderSvg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    this.dimensionLeaderSvg.classList.add("icax-three-dimension-leaders");
    this.dimensionLabelLayer.appendChild(this.dimensionLeaderSvg);
    this.root.appendChild(this.dimensionLabelLayer);
    this.specificationLabelLayer = document.createElement("div");
    this.specificationLabelLayer.className = "icax-three-specification-label-layer";
    this.specificationLabelLayer.setAttribute("aria-label", "产品尺寸规格");
    this.specificationLeaderSvg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    this.specificationLeaderSvg.classList.add("icax-three-dimension-leaders");
    this.specificationLabelLayer.appendChild(this.specificationLeaderSvg);
    this.root.appendChild(this.specificationLabelLayer);

    this.#installDisplayModeControl();
    this.#installProjectionToggle();
    this.#installTransformSpaceToggle();
    if(options.showOrientationGizmo!==false)this.#installOrientationGizmo();

    this.raycaster = new THREE.Raycaster();
    this.pointer = new THREE.Vector2();

    this.#installSceneBasics();
    this.#installPointerControls();
    this.#updateProjectionMatrix();
    this.#updateCamera();
    this.root.classList.toggle("picking-enabled", this.pickingEnabled);
    this.#setStatus("等待渲染数据");
  }

  mount(host) {
    if (!host) {
      throw new TypeError("Three viewport host is required");
    }
    if (this.host === host && this.root.parentElement === host) {
      this.resize();
      return this;
    }

    this.host = host;
    host.replaceChildren(this.root);
    this.resize();
    this.resizeObserver?.disconnect?.();
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(host);
    if (this.continuousRendering) {
      this.#startRenderLoop();
    } else {
      this.#renderOnce();
    }
    return this;
  }

  setContinuousRendering(enabled) {
    const next = Boolean(enabled);
    if (this.continuousRendering === next) return this;
    this.continuousRendering = next;
    if (next) {
      this.#startRenderLoop();
    } else if (this.animationFrame) {
      cancelAnimationFrame(this.animationFrame);
      this.animationFrame = 0;
    }
    if (!this.isDisposed) this.#renderOnce();
    return this;
  }

  setPickingEnabled(enabled) {
    this.pickingEnabled = Boolean(enabled);
    if (!this.pickingEnabled && this.rectangleSelectionKey != null) this.#resetNavigation();
    if (!this.pickingEnabled) this.#clearHover();
    this.root.classList.toggle("picking-enabled", this.pickingEnabled);
    return this;
  }

  setBlankDoubleClickFitEnabled(enabled) {
    this.blankDoubleClickFitEnabled = Boolean(enabled);
    this.#cancelBlankPick();
    return this;
  }

  setHoverEnabled(enabled) {
    this.options.hoverEnabled = Boolean(enabled);
    if (!enabled) this.#clearHover();
    return this;
  }

  setBackgroundColor(color) {
    const next = new THREE.Color(color ?? 0x182128);
    if (this.scene.background?.equals?.(next)) {
      return;
    }
    this.scene.background = next;
    this.#renderOnce();
  }

  getProjectionMode() {
    return this.projectionMode;
  }

  pointOnWorkPlane(clientX, clientY, normal = [0, 1, 0], offset = 0) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const n = toFiniteVector3(normal);
    if (!n || n.lengthSq() < 1e-12 || !Number.isFinite(Number(offset)) || !rect.width || !rect.height) return null;
    n.normalize();
    const localPoint = n.clone().multiplyScalar(Number(offset));
    this.content.updateMatrixWorld(true);
    const worldPoint = localPoint.applyMatrix4(this.content.matrixWorld);
    const worldNormal = n.transformDirection(this.content.matrixWorld);
    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(new THREE.Vector2((clientX - rect.left) / rect.width * 2 - 1, -((clientY - rect.top) / rect.height) * 2 + 1), this.camera);
    const hit = raycaster.ray.intersectPlane(new THREE.Plane().setFromNormalAndCoplanarPoint(worldNormal, worldPoint), new THREE.Vector3());
    if (!hit) return null;
    return hit.applyMatrix4(new THREE.Matrix4().copy(this.content.matrixWorld).invert()).toArray();
  }

  setProjectionMode(value, controlOptions = {}) {
    const mode = normalizeProjectionMode(value);
    if (mode === this.projectionMode) {
      this.#syncProjectionToggle();
      return this;
    }
    const previousCamera = this.camera;
    this.transformManipulator?.finish(true);
    const nextCamera = mode === "orthographic"
      ? new THREE.OrthographicCamera(-1, 1, 1, -1, previousCamera.near, previousCamera.far)
      : new THREE.PerspectiveCamera(
        this.perspectiveFov,
        this.viewportAspect,
        previousCamera.near,
        previousCamera.far,
      );
    nextCamera.up.copy(previousCamera.up);
    this.camera = nextCamera;
    this.projectionMode = mode;
    this.#updateProjectionMatrix();
    this.#updateCamera();
    this.#syncProjectionToggle();
    this.#renderOnce();
    this.#emitCameraChange("projection");
    if (controlOptions?.notify === true
        && typeof this.options.onProjectionChange === "function") {
      this.options.onProjectionChange(mode);
    }
    return this;
  }

  setDisplayMode(mode) {
    if (!["shaded", "shadedEdges", "wireframe"].includes(mode)) {
      throw new TypeError("Invalid viewport display mode");
    }
    this.displayMode = mode;
    for (const object of this.sceneObjects.values()) this.#refreshMeshEdges(object, object.userData.instance);
    this.#syncDisplayModeControl();
    this.#renderOnce();
    return this;
  }

  refreshObjectAppearance() {
    this.#refreshSceneObjectMaterials();
    this.#renderOnce();
    return this;
  }

  setGridVisible(visible) {
    this.options.showGrid = Boolean(visible);
    const grid = this.scene.getObjectByName('iCAX XY Workbench Grid');
    if (grid) grid.visible = Boolean(visible);
    this.#renderOnce();
    return this;
  }

  setShowSurfaceEdges(visible) {
    this.showSurfaceEdges = Boolean(visible);
    for (const object of this.sceneObjects.values()) this.#refreshMeshEdges(object, object.userData.instance);
    this.#syncDisplayModeControl();
    this.#renderOnce();
    return this;
  }

  setProjectionToggleVisible(visible) {
    const next = Boolean(visible);
    if (this.projectionToggle) this.projectionToggle.hidden = !next;
    this.root.classList.toggle("projection-toggle-visible", next);
    return this;
  }

  setProjectionChangeHandler(handler) {
    this.options.onProjectionChange = typeof handler === "function" ? handler : null;
    return this;
  }

  setOrbitConstrained(constrained) {
    const next = Boolean(constrained);
    const previous = this.options.constrainOrbit !== false;
    this.options.constrainOrbit = next;
    this.root.dataset.orbitConstrained = String(next);
    if (next && !previous) {
      const offset = this.camera.position.clone().sub(this.cameraState.target);
      if (offset.lengthSq() > Number.EPSILON) {
        this.cameraState.radius = this.#clampCameraRadius(offset.length());
        offset.normalize();
        this.cameraState.theta = Math.atan2(offset.y, offset.x);
        this.cameraState.phi = Math.acos(THREE.MathUtils.clamp(offset.z, -1, 1));
      }
      this.#updateCamera();
      this.#renderOnce();
    }
    return this;
  }

  // Gate user navigation gestures; explicit view/projection APIs remain usable.
  // Switching tools also ends an already captured camera gesture immediately.
  setCameraNavigationEnabled(enabled) {
    const next=Boolean(enabled);
    if(this.cameraNavigationEnabled!==next){
      this.cameraNavigationEnabled=next;
      if(!next){this.#cancelBlankPick();this.#resetNavigation();}
    }
    this.root.dataset.cameraNavigationEnabled=String(next);
    return this;
  }

  setTransformSpace(space, notify = false) {
    if (!['world','local'].includes(space)) throw new TypeError('Unknown transform space');
    this.transformManipulator?.finish(true);
    this.transformSpace = space;
    for (const button of this.transformSpaceToggle?.querySelectorAll('button') ?? []) {
      const active = button.dataset.transformSpace === space;
      button.classList.toggle('active',active); button.setAttribute('aria-pressed',String(active));
    }
    if (notify) this.options.onTransformSpaceChange?.(space);
    this.#renderOnce(); return this;
  }

  getGridState() {
    const size=this.options.gridSize??800,divisions=this.options.gridDivisions??20;
    return {visible:this.options.showGrid!==false,size,divisions,spacing:size/divisions,origin:[-size/2,-size/2,0]};
  }

  setTransformManipulator(options) {
    this.transformManipulator ??= new TransformManipulator(this, () => { if (!this.isDisposed) this.#renderOnce(); });
    this.transformManipulator.configure(options);
    this.#renderOnce(); return this;
  }

  clearTransformManipulator() {
    const manipulator = this.transformManipulator;
    this.transformManipulator = null;
    manipulator?.dispose();
    if (!this.isDisposed) this.#renderOnce();
    return this;
  }

  connectScene(sceneProxy) {
    if (this.sceneProxy === sceneProxy) {
      return this;
    }
    this.#unsubscribe();
    this.#resetNavigation();
    this.sceneProxy = sceneProxy ?? null;
    this.slotDescriptors.clear();
    this.objectSlots.clear();
    this.dynamicReadPending = false;
    this.#clearRenderContent();

    if (!this.sceneProxy) {
      this.#setStatus("未绑定场景");
      return this;
    }
    if (this.sceneProxy.pdo?.enabled) {
      this.subscriptions.push(this.sceneProxy.pdoStore.subscribe((change) => {
        const type = String(change.descriptor?.type ?? "");
        if (type.startsWith("collider.")) {
          void this.#handleColliderSlotEvent({ payload: change.descriptor });
        }
      }, { emitCurrent: true }));
      this.subscriptions.push(this.sceneProxy.subscribe(
        ColliderPDOEvents.defragBegin,
        (event) => this.#handleDefragEvent(event, true),
      ));
      this.subscriptions.push(this.sceneProxy.subscribe(
        ColliderPDOEvents.defragEnd,
        (event) => this.#handleDefragEvent(event, false),
      ));
    }
    this.#setStatus("等待当前 View 的资源快照");
    return this;
  }

  resize() {
    const rect = this.host?.getBoundingClientRect?.() ?? this.root.getBoundingClientRect();
    const width = Math.max(1, Math.floor(rect.width || this.root.clientWidth || 1));
    const height = Math.max(1, Math.floor(rect.height || this.root.clientHeight || 1));
    this.viewportAspect = width / height;
    this.#updateProjectionMatrix();
    this.renderer.setSize(width, height, false);
    this.axisRenderer?.setSize(86, 86, false);
    this.#renderOnce();
  }

  dispose() {
    this.isDisposed = true;
    this.clearTransformManipulator();
    this.#resetNavigation();
    this.#cancelBlankPick();
    this.#clearHover();
    this.#unsubscribe();
    this.#removeDomListeners();
    this.resizeObserver?.disconnect?.();
    this.resizeObserver = null;
    if (this.animationFrame) {
      cancelAnimationFrame(this.animationFrame);
      this.animationFrame = 0;
    }
    this.#clearRenderContent();
    this.clearMeasurementPoints(false);
    this.clearDimensionAnnotations(false);
    this.clearSpecificationAnnotations(false);
    this.axisRenderer?.dispose?.();
    this.axisRenderer = null;
    this.renderer.dispose();
    this.root.remove();
  }

  async refreshAll() {
    for (const descriptor of this.colliderSlotDescriptors.values()) {
      await Promise.race([this.#readColliderSlot(descriptor), delay(300)]);
    }
  }

  async applyViewSnapshot(snapshot, resourceClient = this.sceneProxy?.resources) {
    if (!resourceClient || typeof resourceClient.get !== "function") {
      throw new TypeError("View rendering requires a ResourceClient");
    }
    const generation = ++this.viewApplyGeneration;
    const rows = Array.isArray(snapshot?.rows) ? snapshot.rows : [];
    const staticKey=JSON.stringify([snapshot?.viewId,rows.map(row=>{
      const {localToWorldMatrix,...data}=row.data??{};return [row.entityId,data];
    })]);
    if(this.transformOnlyViewKey===staticKey&&rows.length===this.sceneObjects.size
      &&rows.every(row=>this.sceneObjects.has(String(row.entityId)))
      &&(typeof snapshot?.isCurrent!=="function"||snapshot.isCurrent())){
      // Undo can return to the last applied view while a newer image is still
      // decoding. Retire that pending image even on this transform-only path.
      this.desiredTextureUrls = new Set(rows.map(row => this.materialPayloads.get(String(row.data?.material?.url ?? ""))?.baseColorTextureUrl).filter(Boolean));
      this.#pruneViewTextures();
      let changed=0;
      for(const row of rows){
        const id=String(row.entityId),matrix=toThreeMatrixArray(row.data.localToWorldMatrix),previous=this.transformPayloads.get(id);
        if(previous?.localToWorld?.every((v,i)=>v===matrix[i]))continue;
        this.transformPayloads.set(id,{kind:"transform",transformId:id,entityId:id,localToWorld:matrix});
        const object=this.sceneObjects.get(id),preview=this.transformPreviews.get(id);
        object.matrix.fromArray(preview?.localToWorld??matrix);object.matrixWorldNeedsUpdate=true;
        this.#updateColliderUsingTransform(id);changed++;
      }
      this.appliedViewRevision=String(snapshot?.revision??"0");
      if(changed)this.#renderOnce();
      return {...this.transformOnlyReceipt,applied:true,superseded:false,revision:this.appliedViewRevision,renderSequence:this.renderSequence,transformOnly:true,changedTransforms:changed};
    }
    const references = new Map();
    for (const { data } of rows) {
      for (const reference of [data?.geometry, data?.material]) {
        const url = String(reference?.url ?? "").trim();
        if (url) {
          references.set(`${url}@${String(reference?.version ?? 0)}`, reference);
        }
      }
    }

    const pending = [...references.entries()]
      .filter(([key]) => !this.resourcePromises.has(key));
    if (typeof resourceClient.getMany === "function") {
      for (let offset = 0; offset < pending.length; offset += 4) {
        const batch = pending.slice(offset, offset + 4);
        if (batch.length < 2) continue;
        const loaded = loadRenderResources(resourceClient, batch.map(([, reference]) => reference));
        batch.forEach(([key], index) => {
          const request = loaded.then((resources) => resources[index]).catch((error) => {
            this.resourcePromises.delete(key);
            throw error;
          });
          this.resourcePromises.set(key, request);
        });
      }
    }
    const resources = await Promise.all([...references.values()].map((reference) =>
      this.#loadViewResource(resourceClient, reference)));
    if (generation === this.viewApplyGeneration && !this.isDisposed
        && (typeof snapshot?.isCurrent !== "function" || snapshot.isCurrent())) {
      this.desiredTextureUrls = new Set(resources.filter(resource => resource?.type === "material")
        .map(resource => resource.data.baseColorTextureUrl).filter(Boolean));
      await Promise.all([...this.desiredTextureUrls].map(url => this.#loadViewTexture(resourceClient, url)));
    }
    // An editor can change while the resources for this snapshot are loading,
    // before it has a replacement snapshot to submit. Do not display stale
    // geometry even for a frame in that interval.
    if (generation !== this.viewApplyGeneration || this.isDisposed
      || typeof snapshot?.isCurrent === "function" && !snapshot.isCurrent()) {
      return {
        applied: false,
        superseded: true,
        revision: String(snapshot?.revision ?? "0"),
      };
    }
    for (const resource of resources) {
      if (!resource) {
        continue;
      }
      if (resource.type === "geometry") {
        const existingPayload=this.geometryPayloads.get(resource.url);
        if(existingPayload?.viewResourceVersion===resource.version&&this.geometryObjects.has(resource.url))continue;
        const payload = { ...resource.data, geometryId: resource.url, viewResourceVersion: resource.version };
        const previous = this.geometryObjects.get(resource.url);
        previous?.dispose?.();
        this.geometryPayloads.set(resource.url, payload);
        this.geometryObjects.set(resource.url, this.#makeGeometryObject(payload));
        this.#updateObjectsUsingGeometry(resource.url);
      } else if (resource.type === "material") {
        this.materialPayloads.set(resource.url, resource.data);
      }
    }

    const desiredIds = new Set();
    for (const row of rows) {
      const objectId = String(row?.entityId ?? "").trim();
      const data = row?.data ?? {};
      const geometryId = String(data.geometry?.url ?? "").trim();
      if (!objectId || !geometryId || !this.geometryObjects.has(geometryId)) {
        continue;
      }
      desiredIds.add(objectId);
      const flags = (data.visible === false ? 0 : RenderFlags.visible)
        | (data.selectable === false ? 0 : RenderFlags.selectable)
        | (data.highlighted ? RenderFlags.highlighted : 0)
        | (data.selected ? RenderFlags.selected : 0);
      const instance = {
        kind: "object",
        objectId,
        entityId: objectId,
        transformId: objectId,
        geometryId,
        materialId: String(data.material?.url ?? "").trim(),
        geometryKind: Number(data.geometryKind ?? 1),
        renderClass: Number(data.renderClass ?? 1),
        meshEdges: data.meshEdges === true || this.options.meshEdges === true,
        renderOrder: Number.isFinite(Number(data.renderOrder)) ? Number(data.renderOrder) : 0,
        flags,
        layerMask: Number(data.layerMask ?? RenderLayers.default) >>> 0,
      };
      this.instancePayloads.set(objectId, instance);
      this.transformPayloads.set(objectId, {
        kind: "transform",
        transformId: objectId,
        entityId: objectId,
        localToWorld: toThreeMatrixArray(data.localToWorldMatrix),
      });
      this.#upsertSceneObject(instance);
      this.#updateColliderUsingTransform(objectId);
    }
    for (const objectId of [...this.sceneObjects.keys()]) {
      if (!desiredIds.has(objectId)) {
        this.#removeSceneObject(objectId);
        this.instancePayloads.delete(objectId);
        this.transformPayloads.delete(objectId);
      }
    }
    this.visibleEntityIds = desiredIds;
    for (const object of this.sceneObjects.values()) {
      this.#applyObjectVisibility(object);
    }
    for (const [objectId, object] of this.colliderObjects) {
      this.#applyColliderVisibility(objectId, object);
    }
    this.#refreshSceneObjectMaterials();
    this.#pruneViewTextures();
    this.#updateSelectionVisuals();
    this.#setStatus(rows.length && !desiredIds.size
      ? "View 中暂时没有可读取的几何资源"
      : "");
    this.appliedViewRevision = String(snapshot?.revision ?? "0");
    this.#renderOnce();
    const receipt = {
      applied: true,
      superseded: false,
      revision: this.appliedViewRevision,
      renderSequence: this.renderSequence,
      entityIds: [...desiredIds],
      rowCount: rows.length,
      geometryReferenceCount: rows.filter((row) =>
        String(row?.data?.geometry?.url ?? "").trim()).length,
      missingGeometryEntityIds: rows
        .filter((row) => {
          const geometryId = String(row?.data?.geometry?.url ?? "").trim();
          return !geometryId || !this.geometryObjects.has(geometryId);
        })
        .map((row) => String(row?.entityId ?? "")),
    };
    this.transformOnlyViewKey=staticKey;this.transformOnlyReceipt=receipt;return receipt;
  }

  fitViewForRevision(revision, padding = 1.25) {
    const expectedRevision = String(revision ?? "0");
    if (expectedRevision === "0" || this.appliedViewRevision !== expectedRevision) {
      throw new Error(
        `Cannot fit View revision ${expectedRevision}; applied revision is ${this.appliedViewRevision}`,
      );
    }
    if (!this.fitViewToViewport(padding)) {
      throw new Error(`View revision ${expectedRevision} has no visible geometry to fit`);
    }
    return {
      fitted: true,
      revision: expectedRevision,
      renderSequence: this.renderSequence,
    };
  }

  getAppliedViewState() {
    return {
      revision: this.appliedViewRevision,
      renderSequence: this.renderSequence,
      entityIds: [...this.sceneObjects.keys()],
    };
  }

  // A transient editor may replace resource versions many times without closing.
  // Retain its current recipe (including alternate display modes), not every
  // historical mesh. Active instances stay protected even if omitted by a caller.
  retainViewResources(references = []) {
    const keys = new Set(references.filter(Boolean).map(reference =>
      `${String(reference.url ?? "")}@${String(reference.version ?? 0)}`));
    const urls = new Set(references.filter(Boolean).map(reference => String(reference.url ?? "")));
    for (const instance of this.instancePayloads.values()) {
      urls.add(instance.geometryId);
      urls.add(instance.materialId);
    }
    for (const key of this.resourcePromises.keys()) {
      if (!keys.has(key)) this.resourcePromises.delete(key);
    }
    for (const [url, geometry] of this.geometryObjects) {
      if (!urls.has(url)) {
        geometry.dispose?.();
        this.geometryObjects.delete(url);
        this.geometryPayloads.delete(url);
      }
    }
    for (const url of this.materialPayloads.keys()) {
      if (!urls.has(url)) this.materialPayloads.delete(url);
    }
  }

  fitView(padding = 1.25) {
    const bounds = new THREE.Box3();
    for (const object of this.sceneObjects.values()) {
      if (object.visible) {
        bounds.expandByObject(object);
      }
    }
    if (bounds.isEmpty()) {
      return false;
    }
    const sphere = new THREE.Sphere();
    bounds.getBoundingSphere(sphere);
    this.cameraState.target.copy(sphere.center);
    const halfFov = THREE.MathUtils.degToRad(this.perspectiveFov * 0.5);
    this.cameraState.radius = this.#clampCameraRadius(Math.max(
      1,
      (sphere.radius * Math.max(1, Number(padding))) / Math.sin(halfFov),
    ));
    this.camera.near = Math.max(0.001, this.cameraState.radius / 10000);
    this.camera.far = Math.max(1000, this.cameraState.radius * 20);
    this.#updateProjectionMatrix();
    this.#updateCamera();
    this.#renderOnce();
    this.#emitCameraChange("fit");
    return true;
  }

  getVisibleBounds() {
    const bounds = new THREE.Box3();
    for (const object of this.sceneObjects.values()) {
      if (object.visible) bounds.expandByObject(object, true);
    }
    if (bounds.isEmpty()) return null;
    return {
      min: [bounds.min.x, bounds.min.y, bounds.min.z],
      max: [bounds.max.x, bounds.max.y, bounds.max.z],
    };
  }

  fitViewToViewport(padding = 1.18) {
    // A new product can change the surrounding pane layout before the
    // ResizeObserver runs. Fit against the current container, not its old size.
    if (this.host) this.resize();
    const bounds = new THREE.Box3();
    for (const object of this.sceneObjects.values()) {
      if (object.visible) bounds.expandByObject(object);
    }
    if (bounds.isEmpty()) return false;

    const center = bounds.getCenter(new THREE.Vector3());
    this.cameraState.target.copy(center);
    this.#updateCamera();
    const right = new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion).normalize();
    const up = new THREE.Vector3(0, 1, 0).applyQuaternion(this.camera.quaternion).normalize();
    const towardCamera = this.camera.position.clone().sub(center).normalize();
    let halfWidth = 0;
    let halfHeight = 0;
    let halfDepth = 0;
    for (const x of [bounds.min.x, bounds.max.x]) {
      for (const y of [bounds.min.y, bounds.max.y]) {
        for (const z of [bounds.min.z, bounds.max.z]) {
          const offset = new THREE.Vector3(x, y, z).sub(center);
          halfWidth = Math.max(halfWidth, Math.abs(offset.dot(right)));
          halfHeight = Math.max(halfHeight, Math.abs(offset.dot(up)));
          halfDepth = Math.max(halfDepth, Math.abs(offset.dot(towardCamera)));
        }
      }
    }
    const tanHalfFov = Math.tan(THREE.MathUtils.degToRad(this.perspectiveFov * 0.5));
    const safePadding = Math.max(1, Number(padding));
    this.cameraState.radius = this.#clampCameraRadius(
      Math.max(
        halfHeight / tanHalfFov,
        halfWidth / (tanHalfFov * Math.max(0.1, this.viewportAspect)),
      ) * safePadding + halfDepth,
    );
    this.camera.near = Math.max(0.001, this.cameraState.radius / 10000);
    this.camera.far = Math.max(1000, this.cameraState.radius * 20);
    this.#updateProjectionMatrix();
    this.#updateCamera();
    this.#renderOnce();
    this.#emitCameraChange("fit-viewport");
    return true;
  }

  setStandardView(viewName) {
    const direction = resolveStandardViewDirection(viewName);
    if (!direction) {
      return false;
    }
    return this.#setViewDirection(direction, "standard-view");
  }

  setViewDirection(direction) {
    return this.#setViewDirection(direction, "direction");
  }

  setPresentationAxis(sourceAxis, targetAxis = [1, 0, 0]) {
    const source = toFiniteVector3(sourceAxis);
    const target = toFiniteVector3(targetAxis);
    if (!source || !target
      || source.lengthSq() <= Number.EPSILON
      || target.lengthSq() <= Number.EPSILON) {
      return false;
    }
    const rotation = new THREE.Quaternion().setFromUnitVectors(
      source.normalize(),
      target.normalize(),
    );
    for (const group of [
      this.content,
      this.measurementContent,
      this.dimensionContent,
      this.colliderContent,
    ]) {
      group.quaternion.copy(rotation);
      group.updateMatrixWorld(true);
    }
    this.#renderOnce();
    return true;
  }

  #setViewDirection(direction, reason) {
    if (!Array.isArray(direction)
      || direction.length !== 3
      || !direction.every((value) => Number.isFinite(Number(value)))) {
      return false;
    }
    const vector = new THREE.Vector3(...direction).normalize();
    if (vector.lengthSq() <= Number.EPSILON) return false;
    this.cameraState.theta = Math.atan2(vector.y, vector.x);
    this.cameraState.phi = Math.acos(THREE.MathUtils.clamp(vector.z, -1, 1));
    this.#updateCamera();
    this.#renderOnce();
    this.#emitCameraChange(reason);
    return true;
  }

  getCameraState() {
    return {
      projectionMode: this.projectionMode,
      orbitConstrained: this.options.constrainOrbit !== false,
      target: {
        x: this.cameraState.target.x,
        y: this.cameraState.target.y,
        z: this.cameraState.target.z,
      },
      radius: this.cameraState.radius,
      theta: this.cameraState.theta,
      phi: this.cameraState.phi,
    };
  }

  captureOrbitPivot(event=null) {
    if(!this.options.orbitPivotPicking)return null;
    this.scene.updateMatrixWorld(true);this.camera.updateMatrixWorld(true);
    const rect=this.renderer.domElement.getBoundingClientRect();
    const hit=sampleOrbitPivot({camera:this.camera,objects:[...this.sceneObjects.values()].filter(object=>object.visible&&object.isMesh),rect,
      clientX:event?.clientX??rect.left+rect.width/2,clientY:event?.clientY??rect.top+rect.height/2,
      radiusPx:this.options.orbitPivotRadiusPx??6});
    if(hit)return hit;
    const fallback=this.options.getOrbitFallbackCenter?.();
    if(Array.isArray(fallback)&&fallback.length===3&&fallback.every(Number.isFinite))return {point:[...fallback],source:'workpiece',hitCount:0,samples:[]};
    const bounds=this.options.getOrbitFallbackCenter?null:this.getVisibleBounds();
    if(bounds)return {point:bounds.min.map((v,i)=>(v+bounds.max[i])/2),source:'scene',hitCount:0,samples:[]};
    return {point:this.cameraState.target.toArray(),source:'view',hitCount:0,samples:[]};
  }

  setOrbitCameraState(start,angles,pivot=null) {
    const next=orbitCameraState(start,{theta:Number(angles.theta??start.theta),phi:this.#resolveOrbitPhi(angles.phi??start.phi)},pivot?.point??pivot,this.options.constrainOrbit!==false);
    return this.setCameraState(next);
  }

  setCameraState(value = {}) {
    const target = value.target ?? {};
    const radius = Number(value.radius);
    const theta = Number(value.theta);
    const phi = Number(value.phi);
    if (![target.x, target.y, target.z, radius, theta, phi]
      .every((entry) => Number.isFinite(Number(entry)))) {
      throw new TypeError("Camera state requires finite target, radius, theta, and phi values");
    }
    if (value.projectionMode != null) {
      this.setProjectionMode(value.projectionMode);
    }
    this.cameraState.target.set(Number(target.x), Number(target.y), Number(target.z));
    this.cameraState.radius = this.#clampCameraRadius(radius);
    this.cameraState.theta = theta;
    this.cameraState.phi = this.#resolveOrbitPhi(phi);
    this.#updateCamera();
    this.#renderOnce();
    this.#emitCameraChange("restore");
    return this;
  }

  setMeasurementPoints(points = []) {
    this.clearMeasurementPoints(false);
    const values = (Array.isArray(points) ? points : [])
      .slice(0, 2)
      .map((point) => new THREE.Vector3(Number(point?.x), Number(point?.y), Number(point?.z)))
      .filter((point) => [point.x, point.y, point.z].every(Number.isFinite));
    if (!values.length) {
      this.#renderOnce();
      return this;
    }

    const pointGeometry = new THREE.BufferGeometry().setFromPoints(values);
    const pointMaterial = new THREE.PointsMaterial({
      color: 0xffbd45,
      size: 10,
      sizeAttenuation: false,
      depthTest: false,
    });
    const pointObject = new THREE.Points(pointGeometry, pointMaterial);
    pointObject.renderOrder = 100;
    this.measurementContent.add(pointObject);

    if (values.length === 2) {
      const lineGeometry = new THREE.BufferGeometry().setFromPoints(values);
      const lineMaterial = new THREE.LineBasicMaterial({
        color: 0xffd36f,
        depthTest: false,
        transparent: true,
        opacity: 0.95,
      });
      const lineObject = new THREE.Line(lineGeometry, lineMaterial);
      lineObject.renderOrder = 99;
      this.measurementContent.add(lineObject);
    }
    this.#renderOnce();
    return this;
  }

  clearMeasurementPoints(render = true) {
    for (const object of [...this.measurementContent.children]) {
      this.measurementContent.remove(object);
      object.geometry?.dispose?.();
      if (Array.isArray(object.material)) {
        for (const material of object.material) material?.dispose?.();
      } else {
        object.material?.dispose?.();
      }
    }
    if (render) this.#renderOnce();
    return this;
  }

  setDimensionAnnotations(annotations = []) {
    this.clearDimensionAnnotations(false);
    for (const annotation of Array.isArray(annotations) ? annotations : []) {
      if (annotation?.kind === "angle") {
        const center = toFiniteVector3(annotation.center);
        const radius = Number(annotation.radius);
        const startDegrees = Number(annotation.startAngleDeg);
        const sweepDegrees = Number(annotation.sweepAngleDeg);
        if (!center || !Number.isFinite(radius) || radius <= 0
            || !Number.isFinite(startDegrees) || !Number.isFinite(sweepDegrees)
            || Math.abs(sweepDegrees) > 360) continue;
        // Angular annotations use the document XY plane. Retain an explicit
        // sweep so reflex angles and full circles do not become short chords.
        const start = (startDegrees % 360) * Math.PI / 180;
        const sweep = sweepDegrees * Math.PI / 180;
        const at = angle => center.clone().add(new THREE.Vector3(
          radius * Math.cos(angle), radius * Math.sin(angle), 0));
        const vertices = [center.clone(), at(start), center.clone(), at(start + sweep)];
        const segments = Math.max(1, Math.ceil(Math.abs(sweepDegrees) / 4));
        for (let index = 0; index < segments; index++)
          vertices.push(at(start + sweep * index / segments), at(start + sweep * (index + 1) / segments));
        if (Math.abs(sweepDegrees) > Number.EPSILON) {
          const size = radius * 0.045;
          const direction = Math.sign(sweep);
          for (const [angle, side] of [[start, 1], [start + sweep, -1]]) {
            const tip = at(angle);
            const radial = new THREE.Vector3(Math.cos(angle), Math.sin(angle), 0).multiplyScalar(size * 0.4);
            const tangent = new THREE.Vector3(-Math.sin(angle), Math.cos(angle), 0).multiplyScalar(size * side * direction);
            const tail = tip.clone().add(tangent);
            vertices.push(tip, tail.clone().add(radial), tip.clone(), tail.clone().sub(radial));
          }
        }
        const color = new THREE.Color(annotation.color ?? 0xffc857);
        const geometry = new THREE.BufferGeometry().setFromPoints(vertices);
        const material = new THREE.LineBasicMaterial({ color, depthTest: false, depthWrite: false, transparent: true, opacity: 0.96 });
        const lines = new THREE.LineSegments(geometry, material);
        lines.renderOrder = 120;
        this.dimensionContent.add(lines);
        const label = String(annotation.label ?? "").trim();
        if (label) {
          const sprite = this.#createDimensionLabel(label, color);
          sprite.position.copy(at(start + sweep / 2));
          sprite.renderOrder = 121;
          this.dimensionContent.add(sprite);
        }
        continue;
      }
      const start = toFiniteVector3(annotation?.start);
      const end = toFiniteVector3(annotation?.end);
      if (!start || !end || start.distanceToSquared(end) <= Number.EPSILON) continue;
      const offset = toFiniteVector3(annotation?.offset) ?? new THREE.Vector3();
      const label = String(annotation?.label ?? "").trim();
      if (annotation?.placement === "outside" && label) {
        const color = new THREE.Color(annotation?.color ?? 0xffc857);
        const element = document.createElement("div");
        element.className = "icax-three-dimension-label";
        element.dataset.icaxDimensionAnnotation = String(annotation?.id ?? "");
        element.style.setProperty("--icax-dimension-color", `#${color.getHexString()}`);
        element.textContent = label;
        const leader = document.createElementNS("http://www.w3.org/2000/svg", "path");
        leader.setAttribute("stroke", `#${color.getHexString()}`);
        leader.setAttribute("fill", "none");
        leader.setAttribute("stroke-width", "1.5");
        leader.setAttribute("stroke-linecap", "round");
        leader.dataset.icaxDimensionLeader = String(annotation?.id ?? "");
        this.dimensionLeaderSvg.appendChild(leader);
        this.dimensionLabelLayer.appendChild(element);
        this.outsideDimensionLabels.push({ element, leader, start, end, offset });
        continue;
      }
      const displayStart = start.clone().add(offset);
      const displayEnd = end.clone().add(offset);
      const color = new THREE.Color(annotation?.color ?? 0xffc857);
      const vertices = [];
      if (offset.lengthSq() > Number.EPSILON) {
        vertices.push(start, displayStart, end, displayEnd);
      }
      vertices.push(displayStart, displayEnd);

      const direction = displayEnd.clone().sub(displayStart).normalize();
      let tickDirection = offset.clone();
      if (tickDirection.lengthSq() <= Number.EPSILON) {
        tickDirection = new THREE.Vector3(0, 0, 1);
        if (Math.abs(direction.dot(tickDirection)) > 0.92) tickDirection.set(1, 0, 0);
      }
      tickDirection.normalize();
      const tickSize = Math.max(0.8, Math.min(8, start.distanceTo(end) * 0.035));
      const tick = tickDirection.multiplyScalar(tickSize * 0.5);
      vertices.push(
        displayStart.clone().sub(tick), displayStart.clone().add(tick),
        displayEnd.clone().sub(tick), displayEnd.clone().add(tick),
      );

      const geometry = new THREE.BufferGeometry().setFromPoints(vertices);
      const material = new THREE.LineBasicMaterial({
        color,
        depthTest: false,
        depthWrite: false,
        transparent: true,
        opacity: 0.96,
      });
      const lines = new THREE.LineSegments(geometry, material);
      lines.renderOrder = 120;
      this.dimensionContent.add(lines);

      if (label) {
        const sprite = this.#createDimensionLabel(label, color);
        sprite.position.copy(displayStart).lerp(displayEnd, 0.5);
        sprite.renderOrder = 121;
        this.dimensionContent.add(sprite);
      }
    }
    this.#renderOnce();
    return this;
  }

  clearDimensionAnnotations(render = true) {
    for (const object of [...this.dimensionContent.children]) {
      this.dimensionContent.remove(object);
      object.geometry?.dispose?.();
      const materials = Array.isArray(object.material) ? object.material : [object.material];
      for (const material of materials) {
        material?.map?.dispose?.();
        material?.dispose?.();
      }
    }
    this.outsideDimensionLabels = [];
    this.dimensionLeaderSvg?.replaceChildren?.();
    this.dimensionLabelLayer?.replaceChildren?.(this.dimensionLeaderSvg);
    if (render) this.#renderOnce();
    return this;
  }

  setSpecificationAnnotations(annotations = []) {
    const annotationList = Array.isArray(annotations) ? annotations : [];
    const signature = specificationAnnotationSignature(annotationList);
    if (signature === this.specificationAnnotationsSignature) return this;
    const focusedEditor = this.specificationLabelLayer?.contains?.(document.activeElement)
      && document.activeElement?.matches?.("[data-tube-designer-scene-specification-input]")
      ? document.activeElement : null;
    if (focusedEditor && !focusedEditor.__icaxSpecificationCommitStarted) {
      const parameter = String(focusedEditor.dataset?.tubeDesignerSceneSpecificationInput ?? "");
      const componentKey = String(focusedEditor.dataset?.tubeDesignerSceneSpecificationComponent ?? parameter);
      const state = this.specificationEditorValues.get(componentKey);
      if (state) {
        state.newValue = focusedEditor.value;
        state.editing = true;
      }
      this.activeSpecificationEditor = parameter;
      this.pendingSpecificationAnnotations = annotationList;
      return this;
    }
    this.pendingSpecificationAnnotations = null;
    const activeEditor = this.captureSpecificationAnnotationEditorState();
    this.clearSpecificationAnnotations(false);
    for (const annotation of annotationList) {
      const start = toFiniteVector3(annotation?.start);
      const end = toFiniteVector3(annotation?.end);
      const parameterCallout = ["parameter", "count"].includes(String(annotation?.kind ?? ""));
      // Point labels stay at their anchor without dimension geometry or callout layout.
      const pointLabel = annotation?.kind === "label";
      if (!start || !end || (!parameterCallout && !pointLabel && start.distanceToSquared(end) <= Number.EPSILON)) continue;
      const offset = toFiniteVector3(annotation?.offset) ?? new THREE.Vector3();
      const displayStart = start.clone().add(offset);
      const displayEnd = end.clone().add(offset);
      const hasDualValues = Object.prototype.hasOwnProperty.call(annotation ?? {}, "oldValue")
        && Object.prototype.hasOwnProperty.call(annotation ?? {}, "newValue");
      const parameter = String(annotation?.parameter ?? "").trim();
      const componentKey = String(annotation?.id ?? parameter).trim() || parameter;
      const incomingOldValue = String(hasDualValues ? annotation.oldValue : annotation?.editor?.value ?? "");
      const incomingNewValue = String(hasDualValues ? annotation.newValue : annotation?.editor?.value ?? "");
      let valueState = this.specificationEditorValues.get(componentKey);
      if (!valueState) {
        valueState = {
          oldValue: incomingOldValue,
          newValue: incomingNewValue,
          editing: false,
          awaitingCommit: false,
        };
        this.specificationEditorValues.set(componentKey, valueState);
      } else if (valueState.editing) {
        // Generated/model refreshes may advance the baseline, but an active
        // editor owns its new value until Enter, blur, or Escape commits it.
        valueState.oldValue = incomingOldValue;
      } else if (valueState.awaitingCommit && incomingNewValue !== valueState.newValue) {
        // A render queued before the change event finished still carries the
        // previous value.  Keep the component's submitted new value until the
        // product EC echoes it back.
        valueState.oldValue = incomingOldValue;
      } else {
        valueState.oldValue = incomingOldValue;
        valueState.newValue = incomingNewValue;
        valueState.awaitingCommit = false;
      }
      const componentPending = hasDualValues
        ? !sameSpecificationValue(valueState.oldValue, valueState.newValue)
        : Boolean(annotation?.pending);
      const componentEditing = Boolean(annotation?.editing && annotation?.editor);
      const componentChangedOrEditing = componentPending || componentEditing;
      const color = new THREE.Color(hasDualValues
        ? (componentChangedOrEditing ? 0xef5b55 : 0x27c27a)
        : (annotation?.color ?? 0x27c27a));
      const vertices = [];
      if (!parameterCallout && !pointLabel) {
      if (offset.lengthSq() > Number.EPSILON) vertices.push(start, displayStart, end, displayEnd);
      vertices.push(displayStart, displayEnd);

      const direction = displayEnd.clone().sub(displayStart).normalize();
      let tickDirection = offset.clone();
      if (tickDirection.lengthSq() <= Number.EPSILON) {
        tickDirection = new THREE.Vector3(0, 0, 1);
        if (Math.abs(direction.dot(tickDirection)) > 0.92) tickDirection.set(1, 0, 0);
      }
      tickDirection.normalize();
      const tickSize = Math.max(0.8, Math.min(9, start.distanceTo(end) * 0.035));
      const tick = tickDirection.multiplyScalar(tickSize * 0.5);
      vertices.push(
        displayStart.clone().sub(tick), displayStart.clone().add(tick),
        displayEnd.clone().sub(tick), displayEnd.clone().add(tick),
      );
      }

      const geometry = new THREE.BufferGeometry().setFromPoints(vertices);
      const material = new THREE.LineBasicMaterial({
        color,
        depthTest: false,
        depthWrite: false,
        transparent: true,
        opacity: componentChangedOrEditing ? 1 : 0.92,
      });
      const lines = new THREE.LineSegments(geometry, material);
      lines.renderOrder = 124;
      lines.userData.specificationAnnotation = true;
      lines.userData.parameter = String(annotation?.parameter ?? "");
      this.specificationContent.add(lines);

      const label = String(componentPending
        ? (annotation?.changedLabel ?? annotation?.label ?? "")
        : (annotation?.cleanLabel ?? annotation?.label ?? "")).trim();
      if (!label) continue;
      const labelRoot = document.createElement("div");
      labelRoot.className = "icax-three-specification-label";
      if (componentPending) labelRoot.classList.add("is-pending");
      if (annotation?.active) labelRoot.classList.add("is-active");
      if (annotation?.editing && annotation?.editor) {
        labelRoot.classList.add("is-editing");
        const caption = document.createElement("span");
        caption.className = "icax-three-specification-caption";
        caption.textContent = label.split(" ")[0] || label;
        labelRoot.appendChild(caption);
        const editor = annotation.editor;
        let input;
        if (editor.type === "select") {
          input = document.createElement("select");
          for (const option of Array.isArray(editor.options) ? editor.options : []) {
            const element = document.createElement("option");
            element.value = String(option?.value ?? "");
            element.textContent = String(option?.label ?? option?.value ?? "");
            element.dataset.tubeDesignerValueType = typeof option?.value;
            element.selected = String(option?.value) === String(editor.value);
            input.appendChild(element);
          }
        } else {
          input = document.createElement("input");
          input.type = editor.type === "number" ? "number" : "text";
          if (editor.min != null) input.min = String(editor.min);
          if (editor.max != null) input.max = String(editor.max);
          if (editor.step != null) input.step = String(editor.step);
        }
        input.value = valueState.newValue;
        input.className = "icax-three-specification-input";
        input.dataset.camChangeAction = "tube-designer-scene-specification-change";
        input.dataset.tubeDesignerParameter = parameter;
        input.dataset.tubeDesignerSceneSpecificationInput = parameter;
        input.dataset.tubeDesignerSceneSpecificationComponent = componentKey;
        input.setAttribute("aria-label", label);
        let commitStarted = false;
        const rememberDraft = () => {
          valueState.newValue = input.value;
        };
        const flushPendingLayout = () => {
          const pending = this.pendingSpecificationAnnotations;
          this.pendingSpecificationAnnotations = null;
          if (pending) this.setSpecificationAnnotations(pending);
        };
        const commit = () => {
          if (commitStarted) return;
          commitStarted = true;
          rememberDraft();
          valueState.editing = false;
          valueState.awaitingCommit = true;
          this.activeSpecificationEditor = "";
          input.__icaxSpecificationCommitStarted = true;
          input.dispatchEvent(new Event("change", { bubbles: true }));
        };
        input.addEventListener("input", rememberDraft);
        input.addEventListener("focus", () => {
          valueState.editing = true;
          this.activeSpecificationEditor = parameter;
        });
        input.addEventListener("change", (event) => {
          if (input.__icaxSpecificationReparenting) {
            event.stopImmediatePropagation();
            return;
          }
          rememberDraft();
          if (commitStarted && event.isTrusted) {
            event.stopImmediatePropagation();
            return;
          }
          commitStarted = true;
          valueState.editing = false;
          valueState.awaitingCommit = true;
          this.activeSpecificationEditor = "";
          input.__icaxSpecificationCommitStarted = true;
        });
        input.addEventListener("keydown", (event) => {
          event.stopImmediatePropagation();
          if (event.key === "Enter") {
            event.preventDefault();
            commit();
            input.blur();
          } else if (event.key === "Escape") {
            event.preventDefault();
            input.value = valueState.oldValue;
            rememberDraft();
            commit();
            input.blur();
          }
        }, { capture: true });
        input.addEventListener("blur", () => {
          if (input.__icaxSpecificationReparenting) return;
          this.activeSpecificationEditor = "";
          // Browsers only emit change on blur when the value differs. Commit an
          // unchanged editor as well so clicking away always closes the popup.
          queueMicrotask(() => {
            if (input.isConnected && document.activeElement !== input) commit();
            flushPendingLayout();
          });
        });
        labelRoot.appendChild(input);
      } else if (parameter && annotation?.editable !== false) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "icax-three-specification-trigger";
        button.textContent = label;
        button.title = `${label}；双击直接修改`;
        button.dataset.camAction = "tube-designer-edit-scene-specification";
        button.dataset.tubeDesignerParameterKey = parameter;
        labelRoot.appendChild(button);
      } else {
        const text = document.createElement("span");
        text.className = "icax-three-specification-caption";
        text.textContent = label;
        labelRoot.appendChild(text);
      }
      this.specificationLabelLayer.appendChild(labelRoot);
      this.specificationLabels.push({
        element: labelRoot,
        pickThrough: Boolean(annotation?.pickThrough),
        screenOffset: specificationScreenOffset(annotation),
        worldPoint: parameterCallout || pointLabel ? displayStart.clone() : displayStart.clone().lerp(displayEnd, 0.5),
        parameterCallout,
        anchorPoint: start.clone(),
        color: `#${color.getHexString()}`,
      });
    }
    this.specificationAnnotationsSignature = signature;
    this.restoreSpecificationAnnotationEditorState(activeEditor);
    this.#renderOnce();
    return this;
  }

  focusSpecificationAnnotationEditor(parameter) {
    const escaped = globalThis.CSS?.escape
      ? globalThis.CSS.escape(String(parameter ?? ""))
      : String(parameter ?? "").replace(/["\\]/g, "\\$&");
    const input = this.specificationLabelLayer?.querySelector?.(
      `[data-tube-designer-scene-specification-input="${escaped}"]`,
    );
    input?.focus?.({ preventScroll: true });
    input?.select?.();
    return Boolean(input);
  }

  captureSpecificationAnnotationEditorState({ suspendCommit = false } = {}) {
    const input = this.specificationLabelLayer?.contains?.(document.activeElement)
      && document.activeElement?.matches?.("[data-tube-designer-scene-specification-input]")
      ? document.activeElement : null;
    if (!input) return null;
    const parameter = String(input.dataset?.tubeDesignerSceneSpecificationInput ?? "");
    const componentKey = String(input.dataset?.tubeDesignerSceneSpecificationComponent ?? parameter);
    const valueState = this.specificationEditorValues.get(componentKey);
    if (valueState) {
      valueState.newValue = input.value;
      valueState.editing = true;
    }
    if (suspendCommit) input.__icaxSpecificationReparenting = true;
    return {
      parameter,
      componentKey,
      value: input.value,
      selectionStart: input.selectionStart,
      selectionEnd: input.selectionEnd,
      selectionDirection: input.selectionDirection,
    };
  }

  restoreSpecificationAnnotationEditorState(state) {
    const parameter = String(state?.parameter ?? "").trim();
    if (!parameter) return false;
    const componentKey = String(state?.componentKey ?? parameter).trim();
    const escaped = globalThis.CSS?.escape
      ? globalThis.CSS.escape(componentKey)
      : componentKey.replace(/["\\]/g, "\\$&");
    const input = this.specificationLabelLayer?.querySelector?.(
      `[data-tube-designer-scene-specification-component="${escaped}"]`,
    );
    if (!input?.isConnected) return false;
    const valueState = this.specificationEditorValues.get(componentKey);
    input.value = String(valueState?.newValue ?? state.value ?? "");
    if (valueState) valueState.editing = true;
    delete input.__icaxSpecificationReparenting;
    input.focus({ preventScroll: true });
    if (input.type !== "number" && state.selectionStart != null) {
      input.setSelectionRange(
        state.selectionStart,
        state.selectionEnd,
        state.selectionDirection ?? "none",
      );
    }
    return document.activeElement === input;
  }

  clearSpecificationAnnotations(render = true) {
    for (const object of [...this.specificationContent.children]) {
      this.specificationContent.remove(object);
      object.geometry?.dispose?.();
      const materials = Array.isArray(object.material) ? object.material : [object.material];
      for (const material of materials) material?.dispose?.();
    }
    this.specificationLabels = [];
    this.specificationAnnotationsSignature = "";
    this.specificationLeaderSvg?.replaceChildren?.();
    this.specificationLabelLayer?.replaceChildren?.();
    this.specificationLabelLayer?.appendChild?.(this.specificationLeaderSvg);
    if (render) this.#renderOnce();
    return this;
  }

  getViewCubeState() {
    const direction = new THREE.Vector3();
    this.camera.getWorldDirection(direction);
    if (direction.lengthSq() <= Number.EPSILON) {
      direction.set(-0.55, 0.68, -0.46);
    }
    direction.normalize();

    const up = new THREE.Vector3(0, 1, 0).applyQuaternion(this.camera.quaternion);
    if (up.lengthSq() <= Number.EPSILON) {
      up.set(0, 0, 1);
    }
    up.normalize();

    return {
      activeCameraId: this.activeCameraId,
      direction: { x: direction.x, y: direction.y, z: direction.z },
      up: { x: up.x, y: up.y, z: up.z },
    };
  }

  setSelectedObjectId(objectId, options = {}) {
    return this.setSelectedObjectIds(objectId ? [objectId] : [], objectId, options);
  }

  // Display-only transforms, in the same column-major convention as View data.
  // Never modify the authoritative render payload or the underlying geometry.
  setTransformPreview(transformId, localToWorld) {
    return this.setTransformPreviews([[transformId,localToWorld]]);
  }

  // One accepted machine result can move many nodes. Publish the whole batch
  // before rendering, so meshes and their collider overlays share one pose.
  setTransformPreviews(entries) {
    const transforms=Array.from(entries,([id,matrix])=>[String(id),Array.from(matrix??[])]);
    if(transforms.some(([,matrix])=>matrix.length!==16||!matrix.every(Number.isFinite)))throw new TypeError('Transform preview requires a finite 4x4 matrix');
    for(const [id,matrix] of transforms)this.transformPreviews.set(id,{localToWorld:matrix});
    for(const [id] of transforms)this.#updateObjectsUsingTransform(id);
    this.#renderOnce();return this;
  }

  clearTransformPreviews(transformIds) {
    for(const transformId of transformIds){const id=String(transformId);if(this.transformPreviews.delete(id))this.#updateObjectsUsingTransform(id);}
    this.#renderOnce();return this;
  }

  clearTransformPreview(transformId) {
    const id = String(transformId);
    if (this.transformPreviews.delete(id)) {
      this.#updateObjectsUsingTransform(id);
      this.#renderOnce();
    }
    return this;
  }

  setGhostMesh(payload = {}) {
    this.clearGhostMesh();
    const positions = Array.isArray(payload.positions)
      || ArrayBuffer.isView(payload.positions)
      ? payload.positions
      : [];
    const indices = Array.isArray(payload.indices)
      || ArrayBuffer.isView(payload.indices)
      ? payload.indices
      : [];
    if (positions.length < 9 || positions.length % 3 !== 0 || indices.length < 3) {
      return this;
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute(
      "position",
      new THREE.BufferAttribute(Float32Array.from(positions, Number), 3),
    );
    geometry.setIndex(new THREE.BufferAttribute(Uint32Array.from(indices, Number), 1));
    geometry.computeVertexNormals();
    geometry.computeBoundingSphere();

    const opacity = Math.max(0.08, Math.min(0.82, Number(payload.opacity ?? 0.34)));
    const group = new THREE.Group();
    group.name = `CAD intent ghost: ${String(payload.nodeId ?? "feature")}`;
    group.userData.isGhostPreview = true;
    group.userData.nodeId = String(payload.nodeId ?? "");

    const material = new THREE.MeshPhongMaterial({
      color: new THREE.Color(payload.color ?? "#e9a12f"),
      emissive: new THREE.Color("#5c3100"),
      emissiveIntensity: 0.16,
      transparent: true,
      opacity,
      depthTest: false,
      depthWrite: false,
      side: THREE.DoubleSide,
      shininess: 38,
    });
    const mesh = new THREE.Mesh(geometry, material);
    mesh.name = "CAD intent ghost body";
    mesh.renderOrder = 20;
    group.add(mesh);

    const edges = new THREE.LineSegments(
      new THREE.EdgesGeometry(geometry, 22),
      new THREE.LineBasicMaterial({
        color: new THREE.Color("#ffd27b"),
        transparent: true,
        opacity: Math.min(0.9, opacity + 0.28),
        depthTest: false,
        depthWrite: false,
      }),
    );
    edges.name = "CAD intent ghost outline";
    edges.renderOrder = 21;
    group.add(edges);

    this.ghostPreview = group;
    this.content.add(group);
    this.#renderOnce();
    return this;
  }

  clearGhostMesh() {
    if (!this.ghostPreview) {
      return this;
    }
    this.ghostPreview.parent?.remove(this.ghostPreview);
    disposeObject3D(this.ghostPreview);
    this.ghostPreview = null;
    this.#renderOnce();
    return this;
  }

  setVisibleLayerMask(layerMask) {
    const nextMask = Number(layerMask) >>> 0;
    if (nextMask === this.visibleLayerMask) {
      return this;
    }
    this.visibleLayerMask = nextMask;
    for (const object of this.sceneObjects.values()) {
      this.#applyObjectVisibility(object);
    }
    for (const [objectId, object] of this.colliderObjects) {
      this.#applyColliderVisibility(objectId, object);
    }
    this.#updateSelectionHelper();
    this.#renderOnce();
    return this;
  }

  setRenderSceneId(renderSceneId = null) {
    const nextSceneId = renderSceneId == null ? null : String(renderSceneId);
    if (nextSceneId === this.renderSceneId) {
      return this;
    }
    this.renderSceneId = nextSceneId;
    return this;
  }

  setVisibleEntityIds(entityIds = null) {
    const nextIds = entityIds == null ? null : new Set(normalizeEntityIds(entityIds));
    if ((nextIds === null && this.visibleEntityIds === null)
      || (nextIds !== null && this.visibleEntityIds !== null && setsEqual(nextIds, this.visibleEntityIds))) {
      return this;
    }
    this.visibleEntityIds = nextIds;
    for (const object of this.sceneObjects.values()) {
      this.#applyObjectVisibility(object);
    }
    for (const [objectId, object] of this.colliderObjects) {
      this.#applyColliderVisibility(objectId, object);
    }
    this.#updateSelectionHelper();
    this.#renderOnce();
    return this;
  }

  setSelectedObjectIds(objectIds = [], primaryObjectId = "", options = {}) {
    const nextIds = new Set((Array.isArray(objectIds) ? objectIds : [objectIds])
      .map((id) => String(id ?? "").trim())
      .filter(Boolean));
    const nextPrimaryId = String(primaryObjectId ?? "").trim() || [...nextIds][0] || "";
    this.selectionVisual = normalizeSelectionVisual(options?.selectionVisual ?? this.defaultSelectionVisual);
    if (nextPrimaryId === this.selectedObjectId && setsEqual(nextIds, this.selectedObjectIds)) {
      this.#updateSelectionVisuals();
      this.#renderOnce();
      return this;
    }

    this.selectedObjectId = nextPrimaryId;
    this.selectedObjectIds = nextIds;
    this.#updateSelectionVisuals();
    this.#renderOnce();
    return this;
  }

  /**
   * Apply a transient, non-selecting tint to scene objects. This deliberately
   * stays separate from selection so feature guidance can use its own colour
   * without moving the selection axis or replacing parts-list selection.
   */
  setEmphasizedObjectIds(objectIds = [], options = {}) {
    this.emphasisVisual = normalizeEmphasisVisual(options?.visual ?? this.defaultEmphasisVisual);
    this.emphasizedObjectIds = new Set((Array.isArray(objectIds) ? objectIds : [objectIds])
      .map((id) => String(id ?? "").trim())
      .filter(Boolean));
    this.emphasisDimOpacity = this.emphasizedObjectIds.size && Number.isFinite(options.dimOthers)
      ? Math.max(0.02, Math.min(1, options.dimOthers)) : null;
    this.#updateSelectionVisuals();
    this.#renderOnce();
    return this;
  }

  getDebugState(options = {}) {
    this.#renderOnce();
    const canvas = this.renderer.domElement;
    const bounds = new THREE.Box3().setFromObject(this.content);
    const sphere = new THREE.Sphere();
    if (!bounds.isEmpty()) {
      bounds.getBoundingSphere(sphere);
    }
    const projectedCenter = bounds.isEmpty()
      ? null
      : sphere.center.clone().project(this.camera);
    const cameraDirection = new THREE.Vector3();
    this.camera.getWorldDirection(cameraDirection);
    const colliderObjects = [...this.colliderObjects.values()];
    const visibleColliderObjects = colliderObjects.filter((object) => object.visible);
    const colliderShapeCount = [...this.colliderPayloads.values()].reduce(
      (sum, payload) => sum + Number(payload?.shapeCount ?? payload?.shapes?.length ?? 0),
      0,
    );
    const state = {
      mounted: Boolean(this.host && this.root.parentElement),
      canvasClientWidth: canvas.clientWidth,
      canvasClientHeight: canvas.clientHeight,
      canvasWidth: canvas.width,
      canvasHeight: canvas.height,
      slotCount: this.slotDescriptors.size,
      renderSceneId: this.renderSceneId,
      colliderSlotCount: this.colliderSlotDescriptors.size,
      slots: [...this.slotDescriptors.values()].map((descriptor) => ({
        pdoId: String(descriptor.pdoId ?? ""),
        payloadKind: String(descriptor.payloadKind ?? ""),
        slotRole: String(descriptor.slotRole ?? ""),
        geometryId: String(descriptor.geometryId ?? ""),
        objectId: String(descriptor.objectId ?? ""),
        transformId: String(descriptor.transformId ?? ""),
        sceneId: String(descriptor.sceneId ?? ""),
        version: Number(descriptor.version ?? 0),
        slotVersion: Number(descriptor.version ?? 0),
        payloadCapacity: Number(descriptor.payloadCapacity ?? 0),
        lastReadDataVersion: String(descriptor.lastReadDataVersion ?? ""),
      })),
      geometryCount: this.geometryObjects.size,
      objectCount: this.sceneObjects.size,
      transformCount: this.transformPayloads.size,
      cameraCount: this.cameras.size,
      colliderObjectCount: this.colliderObjects.size,
      visibleColliderObjectCount: visibleColliderObjects.length,
      colliderShapeCount,
      activeCameraId: this.activeCameraId,
      visibleEntityFilterCount: this.visibleEntityIds?.size ?? null,
      selectedObjectId: this.selectedObjectId,
      selectedObjectIds: [...this.selectedObjectIds],
      emphasizedObjectIds: [...this.emphasizedObjectIds],
      emphasisVisual: { ...this.emphasisVisual },
      ghostPreviewVisible: Boolean(this.ghostPreview?.visible),
      ghostPreviewNodeId: String(this.ghostPreview?.userData?.nodeId ?? ""),
      ghostPreviewVertexCount: Number(
        this.ghostPreview?.children?.[0]?.geometry?.getAttribute?.("position")?.count ?? 0,
      ),
      continuousRendering: this.continuousRendering,
      pickingEnabled: this.pickingEnabled,
      projectionMode: this.projectionMode,
      displayMode: this.displayMode,
      showSurfaceEdges: this.showSurfaceEdges,
      topologyEdgeCount: [...this.geometryObjects.values()].reduce((count, geometry) => count + (geometry.userData?.topologyEdgeCount ?? 0), 0),
      projectionToggleVisible: Boolean(this.projectionToggle && !this.projectionToggle.hidden),
      orbitConstrained: this.options.constrainOrbit !== false,
      animationFrameActive: Boolean(this.animationFrame),
      dimensionAnnotationCount: this.outsideDimensionLabels.length + this.dimensionContent.children.filter(
        (object) => object.userData?.dimensionLabel,
      ).length,
      specificationAnnotationCount: this.specificationLabels.length,
      cameraPosition: {
        x: this.camera.position.x,
        y: this.camera.position.y,
        z: this.camera.position.z,
      },
      cameraDirection: {
        x: cameraDirection.x,
        y: cameraDirection.y,
        z: cameraDirection.z,
      },
      visibleObjectCount: [...this.sceneObjects.values()].filter((object) => object.visible).length,
      contentBounds: bounds.isEmpty() ? null : {
        min: { x: bounds.min.x, y: bounds.min.y, z: bounds.min.z },
        max: { x: bounds.max.x, y: bounds.max.y, z: bounds.max.z },
        center: { x: sphere.center.x, y: sphere.center.y, z: sphere.center.z },
        radius: sphere.radius,
        projectedCenter: projectedCenter ? { x: projectedCenter.x, y: projectedCenter.y, z: projectedCenter.z } : null,
      },
      renderInfo: {
        calls: this.renderer.info.render.calls,
        triangles: this.renderer.info.render.triangles,
        lines: this.renderer.info.render.lines,
        points: this.renderer.info.render.points,
      },
    };
    if (options.samplePixels) {
      state.pixelSample = this.#sampleCanvasPixels();
    }
    if (options.includeObjects) {
      state.objects = [...this.sceneObjects.values()].map((object) => ({
        objectId: String(object.userData?.objectId ?? ""),
        geometryId: String(object.userData?.geometryId ?? ""),
        renderClass: Number(object.userData?.renderClass ?? 0),
        layerMask: Number(object.userData?.layerMask ?? RenderLayers.default),
        geometryKind: Number(object.userData?.geometryKind ?? 0),
        materialId: String(object.userData?.materialId ?? ""),
        visible: Boolean(object.visible),
      }));
      state.colliders = colliderObjects.map((object) => ({
        objectId: String(object.userData?.objectId ?? ""),
        dataVersion: String(object.userData?.dataVersion ?? ""),
        shapeCount: Number(this.colliderPayloads.get(String(object.userData?.objectId ?? ""))?.shapeCount ?? object.children.length),
        visible: Boolean(object.visible),
      }));
    }
    return state;
  }

  async #handleColliderSlotEvent(event) {
    let payload = null;
    try {
      payload = parseColliderPDOEvent(event);
    } catch (error) {
      this.#setStatus(`ColliderPDO 事件解析失败: ${error.message}`);
      return;
    }
    if (!payload?.pdoId) {
      return;
    }

    if (payload.event === "SlotFreed") {
      this.#removeColliderSlot(payload);
      return;
    }

    const descriptor = {
      pdoId: payload.pdoId,
      payloadKind: payload.payloadKind,
      slotRole: payload.slotRole,
      objectId: payload.objectId,
      payloadCapacity: Number(payload.payloadCapacity ?? 0),
      version: Number(payload.slotVersion ?? ColliderPDOLayout.version),
      lastReadDataVersion: null,
    };
    this.colliderSlotDescriptors.set(descriptor.pdoId, descriptor);
    this.#emitDiagnosticOnce(
      `collider-slot:${payload.event}:${descriptor.pdoId}`,
      `ColliderPDO slot ${payload.event}: object=${descriptor.objectId}, pdo=${descriptor.pdoId}`,
      "info",
    );
    await this.#readColliderSlot(descriptor, { force: true });
  }

  #handleDefragEvent(event, isBegin) {
    this.isDefragging = isBegin;
    if (isBegin) {
      this.#setStatus("PDO 正在整理内存");
      return;
    }
    this.#setStatus("");
    this.refreshAll();
  }

  async #readColliderSlot(descriptor, options = {}) {
    if (!this.sceneProxy?.pdo?.enabled || this.isDefragging || !descriptor?.payloadCapacity) {
      return;
    }

    const force = Boolean(options.force);
    const silentTransient = options.silentTransient ?? !force;
    if (!force) {
      const meta = await this.#getSlotMeta(descriptor, { silentTransient });
      const dataVersion = String(meta?.publishedDataVersion ?? meta?.dataVersion ?? "0");
      if (!dataVersion || dataVersion === "0" || dataVersion === descriptor.lastReadDataVersion) {
        return;
      }
    }

    let lastError = null;
    const maxAttempts = force ? 3 : 1;
    for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
      try {
        await this.sceneProxy.pdo.withReadDescriptor({
          id: descriptor.pdoId,
          version: descriptor.version,
          payloadSize: descriptor.payloadCapacity,
        }, (buffer) => {
          const payload = parseColliderPDOPayload(buffer);
          const dataVersion = String(payload.header?.dataVersion ?? "");
          if (!force && dataVersion && dataVersion === descriptor.lastReadDataVersion) {
            return;
          }
          this.#applyColliderPayload(payload);
          descriptor.lastReadDataVersion = dataVersion || descriptor.lastReadDataVersion;
        });
        return;
      } catch (error) {
        lastError = error;
        if (this.#isTransientPDOReadError(error) && attempt < maxAttempts - 1) {
          await delay(24 * (attempt + 1));
          continue;
        }
        break;
      }
    }

    if (lastError) {
      const message = String(lastError.message ?? lastError);
      if (this.#isTransientPDOReadError(lastError)) {
        if (!silentTransient) {
          this.#emitDiagnosticOnce(`collider-transient-read:${descriptor.pdoId}:${message}`, `ColliderPDO 等待可读数据：${message}`, "info");
        }
        return;
      }
      this.#emitDiagnostic(`ColliderPDO 读取失败：${message}`, "error");
    }
  }

  async #getSlotMeta(descriptor, options = {}) {
    try {
      return await this.sceneProxy.pdo.getMetaDescriptor({
        id: descriptor.pdoId,
        version: descriptor.version,
        payloadSize: descriptor.payloadCapacity,
      });
    } catch (error) {
      if (this.#isTransientPDOReadError(error)) {
        if (!options.silentTransient) {
          const message = String(error.message ?? error);
          this.#emitDiagnosticOnce(`transient-meta:${descriptor.pdoId}:${message}`, `PDO 等待版本信息：${message}`, "info");
        }
        return null;
      }
      throw error;
    }
  }

  #isTransientPDOReadError(error) {
    const message = String(error?.message ?? error ?? "");
    return message.includes("slot buffer index is invalid")
      || message.includes("reading buffer has no reader")
      || message.includes("published buffer state is invalid")
      || message.includes("ready buffer state is invalid")
      || message.includes("PDO 正在整理")
      || message.includes("defrag");
  }

  #loadViewResource(resourceClient, reference) {
    const key = `${String(reference?.url ?? "")}@${String(reference?.version ?? 0)}`;
    let request = this.resourcePromises.get(key);
    if (!request) {
      request = loadRenderResource(resourceClient, reference)
        .catch((error) => {
          this.resourcePromises.delete(key);
          throw error;
        });
      this.resourcePromises.set(key, request);
    }
    return request;
  }

  #loadViewTexture(resourceClient, url) {
    let request = this.texturePromises.get(url);
    if (!request) {
      const generation = this.textureGeneration;
      request = loadRenderTexture(resourceClient, url).then(texture => {
        if (this.isDisposed || generation !== this.textureGeneration || !this.desiredTextureUrls.has(url)
            || this.texturePromises.get(url) !== request) {
          texture.dispose();if (this.texturePromises.get(url) === request) this.texturePromises.delete(url);return null;
        }
        this.texturePayloads.set(url, texture);return texture;
      }).catch(error => { if (this.texturePromises.get(url) === request) this.texturePromises.delete(url);throw error; });
      this.texturePromises.set(url, request);
    }
    return request;
  }

  #pruneViewTextures() {
    for (const [url, texture] of this.texturePayloads) if (!this.desiredTextureUrls.has(url)) {
      texture.dispose();this.texturePayloads.delete(url);this.texturePromises.delete(url);
    }
  }

  #applyColliderPayload(payload) {
    if (!payload?.objectId) {
      return;
    }
    this.colliderPayloads.set(payload.objectId, payload);
    this.#upsertColliderObject(payload);
    this.#emitDiagnosticOnce(
      `collider:${payload.objectId}:${payload.header?.dataVersion ?? ""}`,
      `ColliderPDO 对象已读取：object=${payload.objectId}, shapes=${payload.shapeCount}`,
      "ok",
    );
    this.#renderOnce();
  }

  #upsertColliderObject(payload) {
    let object = this.colliderObjects.get(payload.objectId);
    if (object) {
      object.parent?.remove(object);
      disposeObject3D(object);
    }

    object = this.#makeColliderObject(payload);
    this.#applyColliderVisibility(payload.objectId, object, payload);
    object.matrixAutoUpdate = false;
    object.userData.objectId = payload.objectId;
    object.userData.dataVersion = payload.header?.dataVersion ?? "";
    this.colliderObjects.set(payload.objectId, object);
    this.colliderContent.add(object);
    this.#updateColliderUsingTransform(payload.objectId);
  }

  #makeColliderObject(payload) {
    const group = new THREE.Group();
    group.name = `ColliderObject:${payload.objectId}`;
    const material = new THREE.LineBasicMaterial({
      color: 0xffe24d,
      linewidth: 1,
      transparent: true,
      opacity: 0.96,
      depthTest: true,
      depthWrite: false,
    });
    for (const shape of payload.shapes ?? []) {
      const geometry = this.#makeColliderShapeGeometry(shape);
      if (!geometry) {
        continue;
      }
      const line = new THREE.LineSegments(geometry, material.clone());
      line.name = "ColliderShape";
      line.renderOrder = 6000;
      group.add(line);
    }
    return group;
  }

  #makeColliderShapeGeometry(shape) {
    const positions = [];
    const matrix = new THREE.Matrix4().fromArray(shape.localMatrix ?? identityMatrixArray());
    const addSegment = (a, b) => {
      const pa = a.clone().applyMatrix4(matrix);
      const pb = b.clone().applyMatrix4(matrix);
      positions.push(pa.x, pa.y, pa.z, pb.x, pb.y, pb.z);
    };

    if (shape.shapeKind === ColliderShapeKind.box) {
      this.#appendBoxColliderSegments(shape.parameters, addSegment);
    } else if (shape.shapeKind === ColliderShapeKind.cylinder) {
      this.#appendCylinderColliderSegments(shape.parameters, addSegment);
    } else if (shape.shapeKind === ColliderShapeKind.sphere) {
      this.#appendSphereColliderSegments(shape.parameters, addSegment);
    } else if (shape.shapeKind === ColliderShapeKind.capsule) {
      this.#appendCapsuleColliderSegments(shape.parameters, addSegment);
    } else {
      return null;
    }

    if (!positions.length) {
      return null;
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array(positions), 3));
    geometry.computeBoundingSphere();
    return geometry;
  }

  #appendBoxColliderSegments(parameters, addSegment) {
    const hx = Math.max(0.0001, Math.abs(Number(parameters?.[0] ?? 0.5)));
    const hy = Math.max(0.0001, Math.abs(Number(parameters?.[1] ?? 0.5)));
    const hz = Math.max(0.0001, Math.abs(Number(parameters?.[2] ?? 0.5)));
    const corners = [
      new THREE.Vector3(-hx, -hy, -hz), new THREE.Vector3(hx, -hy, -hz),
      new THREE.Vector3(hx, hy, -hz), new THREE.Vector3(-hx, hy, -hz),
      new THREE.Vector3(-hx, -hy, hz), new THREE.Vector3(hx, -hy, hz),
      new THREE.Vector3(hx, hy, hz), new THREE.Vector3(-hx, hy, hz),
    ];
    for (const [a, b] of [
      [0, 1], [1, 2], [2, 3], [3, 0],
      [4, 5], [5, 6], [6, 7], [7, 4],
      [0, 4], [1, 5], [2, 6], [3, 7],
    ]) {
      addSegment(corners[a], corners[b]);
    }
  }

  #appendCylinderColliderSegments(parameters, addSegment) {
    const radius = Math.max(0.0001, Math.abs(Number(parameters?.[0] ?? 0.5)));
    const halfLength = Math.max(0.0001, Math.abs(Number(parameters?.[1] ?? 1.0)) * 0.5);
    this.#appendCircleSegments(radius, -halfLength, "z", addSegment);
    this.#appendCircleSegments(radius, halfLength, "z", addSegment);
    for (const angle of [0, Math.PI * 0.5, Math.PI, Math.PI * 1.5]) {
      const x = Math.cos(angle) * radius;
      const y = Math.sin(angle) * radius;
      addSegment(new THREE.Vector3(x, y, -halfLength), new THREE.Vector3(x, y, halfLength));
    }
  }

  #appendSphereColliderSegments(parameters, addSegment) {
    const radius = Math.max(0.0001, Math.abs(Number(parameters?.[0] ?? 0.5)));
    this.#appendCircleSegments(radius, 0, "z", addSegment);
    this.#appendCircleSegments(radius, 0, "x", addSegment);
    this.#appendCircleSegments(radius, 0, "y", addSegment);
  }

  #appendCapsuleColliderSegments(parameters, addSegment) {
    const radius = Math.max(0.0001, Math.abs(Number(parameters?.[0] ?? 0.1)));
    const halfHeight = Math.max(0.0001, Math.abs(Number(parameters?.[1] ?? 0.5)));
    this.#appendCircleSegments(radius, -halfHeight, "z", addSegment);
    this.#appendCircleSegments(radius, halfHeight, "z", addSegment);
    for (const angle of [0, Math.PI * 0.5, Math.PI, Math.PI * 1.5]) {
      const x = Math.cos(angle) * radius;
      const y = Math.sin(angle) * radius;
      addSegment(new THREE.Vector3(x, y, -halfHeight), new THREE.Vector3(x, y, halfHeight));
    }
  }

  #appendCircleSegments(radius, offset, axis, addSegment) {
    const segmentCount = 40;
    for (let index = 0; index < segmentCount; index += 1) {
      const a0 = (index / segmentCount) * Math.PI * 2;
      const a1 = ((index + 1) / segmentCount) * Math.PI * 2;
      addSegment(makeCirclePoint(radius, offset, axis, a0), makeCirclePoint(radius, offset, axis, a1));
    }
  }

  #applyObject(descriptor, payload) {
    this.objectSlots.set(payload.objectId, descriptor.pdoId);
    this.instancePayloads.set(String(payload.objectId), payload);
    this.#upsertSceneObject(payload);
    const colliderObject = this.colliderObjects.get(payload.objectId);
    if (colliderObject) {
      this.#applyColliderVisibility(payload.objectId, colliderObject);
    }
  }

  #upsertSceneObject(instance) {
    const geometryObject = this.geometryObjects.get(instance.geometryId);
    if (!geometryObject) {
      this.#emitDiagnosticOnce(
        `pending-instance:${instance.objectId}:${instance.geometryId}`,
        `View 实例等待几何资源：object=${instance.objectId}, geometry=${instance.geometryId}`,
        "info",
      );
      return;
    }

    let object = this.sceneObjects.get(instance.objectId);
    if (!object || object.userData.geometryId !== instance.geometryId || object.userData.geometryKind !== instance.geometryKind) {
      if (object) {
        this.#removeSceneObject(instance.objectId);
      }
      object = this.#instantiateObject(instance, geometryObject);
      this.sceneObjects.set(instance.objectId, object);
      this.content.add(object);
      this.#emitDiagnosticOnce(
        `scene-object:${instance.objectId}:${instance.geometryId}`,
        `视口对象已创建：object=${instance.objectId}, geometry=${instance.geometryId}`,
        "ok",
      );
    }

    this.#applyObjectVisibility(object, instance);
    object.userData.instance = instance;
    // Explicit ordering is useful for overlapping transparent preview layers.
    // Reset on every upsert as an entity can return to ordinary scene rendering.
    object.renderOrder = Number.isFinite(instance.renderOrder) ? instance.renderOrder : 0;
    object.matrixAutoUpdate = false;
    this.#refreshObjectMaterial(object, instance);
    this.#refreshMeshEdges(object, instance);
    const transform = this.transformPreviews.get(instance.transformId) ?? this.transformPayloads.get(instance.transformId);
    object.matrix.fromArray(transform?.localToWorld ?? identityMatrixArray());
    object.matrixWorldNeedsUpdate = true;
    this.#setObjectSelectedVisual(object, this.#isObjectSelected(instance.objectId));
    this.#updateSelectionHelper();
  }

  #upsertObjectsUsingGeometry(geometryId) {
    for (const instance of this.instancePayloads.values()) {
      if (instance.geometryId === geometryId) {
        this.#upsertSceneObject(instance);
      }
    }
  }

  #instantiateObject(instance, geometryObject) {
    const hasVertexColors = Boolean(geometryObject?.getAttribute?.("color"));
    const material = this.#makeMaterial(instance, hasVertexColors);
    let object = null;
    if (instance.geometryKind === RenderGeometryKind.mesh) {
      object = new THREE.Mesh(geometryObject, material);
      try { prepareMeshPicking(object); }
      catch(error) { this.#emitDiagnosticOnce(`mesh-picking:${instance.geometryId}`,`网格拾取索引未建立：${error.message}`,'warning'); }
    } else if (instance.geometryKind === RenderGeometryKind.polyline || instance.geometryKind === RenderGeometryKind.toolpath) {
      object = new THREE.LineSegments(geometryObject, material);
    } else {
      object = new THREE.Object3D();
    }
    object.name = `SceneObject:${instance.objectId}`;
    object.userData = {
      objectId: instance.objectId,
      geometryId: instance.geometryId,
      geometryKind: instance.geometryKind,
      renderClass: instance.renderClass,
      layerMask: instance.layerMask,
      materialId: instance.materialId,
      hasVertexColors,
    };
    return object;
  }

  #removeMeshEdges(object) {
    const edges = object?.userData?.meshEdges;
    if (!edges) return;
    edges.parent?.remove(edges);
    edges.material.dispose();
    delete object.userData.meshEdges;
    // Geometry is shared by instances and released with its source geometry.
  }

  #refreshMeshEdges(object, instance) {
    if (!object?.isMesh) return;
    const mode = this.displayMode ?? (instance?.meshEdges === true ? "shadedEdges" : "shaded");
    // Keep the mesh and its original geometry available to surface picking in wireframe mode.
    object.material.visible = mode !== "wireframe";
    object.material.polygonOffset = mode === "shadedEdges";
    // A slope-scaled offset can exceed a close-fitting assembly's actual gap
    // and reveal hidden support edges through the workpiece by a screen pixel.
    object.material.polygonOffsetFactor = 0;
    object.material.polygonOffsetUnits = 1;
    if (mode === "shaded") { this.#removeMeshEdges(object); return; }
    const cad = object.geometry.userData.topologyEdgeCount > 0;
    const all = mode === "wireframe" || this.showSurfaceEdges;
    const key = cad ? (all ? "cad-all" : "cad-feature") : (mode === "wireframe" ? "mesh-wire" : "mesh-feature");
    const edgeColor = mode === "wireframe" ? (this.options.wireframeColor ?? 0x46505c) : 0x46505c;
    const edgeOpacity = rgbaToCssColor(this.#meshColorRGBA(instance) ?? 0xFFFFFFFF).a;
    const previous = object.userData.meshEdges;
    if (previous?.userData.sourceGeometry === object.geometry && previous.userData.edgeMode === key) {
      previous.material.color.setHex(edgeColor);
      previous.material.opacity = edgeOpacity;
      previous.material.transparent = edgeOpacity < 1;
      previous.renderOrder = object.renderOrder + 1;
      return;
    }
    this.#removeMeshEdges(object);
    let cache = this.meshEdgeGeometries.get(object.geometry);
    if (!cache) {
      cache = new Map();
      this.meshEdgeGeometries.set(object.geometry, cache);
      object.geometry.addEventListener("dispose", () => { for (const value of cache.values()) value.dispose(); });
    }
    let geometry = cache.get(key);
    if (!geometry) {
      if (cad) {
        geometry = new THREE.BufferGeometry();
        geometry.setAttribute("position", new THREE.BufferAttribute(
          all ? object.geometry.userData.topologyEdges : object.geometry.userData.topologyFeatureEdges, 3));
      } else {
        geometry = mode === "wireframe" ? new THREE.WireframeGeometry(object.geometry)
          : createMeshFeatureEdgesGeometry(object.geometry, 22);
      }
      cache.set(key, geometry);
    }
    const edges = new THREE.LineSegments(geometry, new THREE.LineBasicMaterial({
      color: edgeColor,
      opacity: edgeOpacity,
      transparent: edgeOpacity < 1,
      depthTest: true,
      depthWrite: false,
      toneMapped: false,
    }));
    if (this.options.meshEdgeDepthBiasPixels != null) {
      // Line rasterization can evaluate depth outside the geometric segment.
      // A screen-sized depth bias keeps a hidden CAD edge behind opaque faces.
      const depthBias = { value: 0 };
      edges.material.onBeforeCompile = shader => {
        shader.uniforms.icaxEdgeDepthBias = depthBias;
        shader.vertexShader = 'uniform float icaxEdgeDepthBias;\n' + shader.vertexShader.replace(
          '#include <project_vertex>', `#include <project_vertex>
          vec4 edgeDepthPosition = projectionMatrix * (mvPosition - vec4(0.0, 0.0, icaxEdgeDepthBias, 0.0));
          gl_Position.z = edgeDepthPosition.z / edgeDepthPosition.w * gl_Position.w;`);
      };
      edges.onBeforeRender = () => {
        const configuredBias = this.options.meshEdgeDepthBiasPixels;
        const pixels = typeof configuredBias === 'function' ? configuredBias(object.userData.instance) : configuredBias;
        depthBias.value = this.displayMode === 'wireframe' ? 0
          : this.#visibleHeightAtTarget() / Math.max(1, this.renderer.domElement.clientHeight)
            * Number(pixels ?? 0);
      };
    }
    edges.name = cad ? "CAD topology edges" : "Mesh edges";
    edges.userData.edgeMode = key;
    edges.userData.meshFeatureEdges = true;
    edges.userData.sourceGeometry = object.geometry;
    edges.renderOrder = object.renderOrder + 1;
    edges.raycast = () => {};
    object.add(edges);
    object.userData.meshEdges = edges;
  }

  #applyObjectVisibility(object, instance = object?.userData?.instance) {
    if (!object || !instance) {
      return;
    }
    const layerMask = Number(instance.layerMask ?? RenderLayers.default) >>> 0;
    object.visible = Boolean(instance.flags & RenderFlags.visible)
      && this.#isEntityVisible(instance.objectId)
      && Boolean(layerMask & this.visibleLayerMask);
  }

  #applyColliderVisibility(objectId, object, payload = this.colliderPayloads.get(objectId)) {
    if (!object || !payload) {
      return;
    }
    const instance = this.instancePayloads.get(String(objectId));
    const layerMask = Number(instance?.layerMask ?? RenderLayers.default) >>> 0;
    object.visible = this.options.showColliders!==false && Boolean(payload.flags & ColliderFlags.visible)
      && Boolean(payload.flags & ColliderFlags.enabled)
      && this.#isEntityVisible(objectId)
      && Boolean(layerMask & this.visibleLayerMask);
  }

  #isEntityVisible(objectId) {
    return this.visibleEntityIds === null
      || this.visibleEntityIds.has(String(objectId ?? "").trim());
  }

  #makeGeometryObject(payload) {
    if (payload.kind === "mesh") {
      const geometry = new THREE.BufferGeometry();
      geometry.userData.topologyEdgeCount = payload.topologyEdgeCount ?? 0;
      geometry.userData.topologyEdges = payload.topologyEdges;
      geometry.userData.topologyFeatureEdges = payload.topologyFeatureEdges;
      geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array(payload.positions), 3));
      if (payload.normals?.length) {
        geometry.setAttribute("normal", new THREE.BufferAttribute(new Float32Array(payload.normals), 3));
        geometry.userData.surfaceNormals = true;
      }
      if (payload.textureCoordinates?.length) {
        geometry.setAttribute("uv", new THREE.BufferAttribute(new Float32Array(payload.textureCoordinates), 2));
      }
      if (payload.vertexColors?.length) {
        geometry.setAttribute("color", new THREE.BufferAttribute(makeVertexColorArray(payload.vertexColors), 3));
      }
      if (payload.indices?.length) {
        geometry.setIndex(new THREE.BufferAttribute(new Uint32Array(payload.indices), 1));
      }
      if (!payload.normals?.length) {
        geometry.computeVertexNormals();
      }
      geometry.computeBoundingSphere();
      this.#emitDiagnosticOnce(
        `mesh-size:${payload.geometryId}:${payload.positions?.length ?? 0}:${payload.indices?.length ?? 0}`,
        `View Mesh 资源：geometry=${payload.geometryId}, vertices=${Math.floor((payload.positions?.length ?? 0) / 3)}, indices=${payload.indices?.length ?? 0}`,
        "info",
      );
      return geometry;
    }

    if (payload.kind === "polyline") {
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array(payload.points), 3));
      geometry.computeBoundingSphere();
      return geometry;
    }

    if (payload.kind === "toolpath") {
      const positions = new Float32Array(Math.max(0, payload.points.length - 1) * 6);
      let cursor = 0;
      for (const span of payload.spans) {
        const end = span.firstPoint + span.pointCount - 1;
        for (let index = span.firstPoint; index < end; index += 1) {
          const a = payload.points[index]?.position;
          const b = payload.points[index + 1]?.position;
          if (!a || !b) {
            continue;
          }
          positions[cursor++] = a.x;
          positions[cursor++] = a.y;
          positions[cursor++] = a.z;
          positions[cursor++] = b.x;
          positions[cursor++] = b.y;
          positions[cursor++] = b.z;
        }
      }
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute("position", new THREE.BufferAttribute(positions.slice(0, cursor), 3));
      geometry.computeBoundingSphere();
      return geometry;
    }

    return new THREE.BufferGeometry();
  }

  #meshColorRGBA(instance) {
    if (instance.geometryKind !== RenderGeometryKind.mesh) return undefined;
    const configured = this.options.meshColorRGBA;
    const color = typeof configured === "function" ? configured(instance) : configured;
    return Number.isFinite(color) ? color : undefined;
  }

  #makeMaterial(instance, hasVertexColors = false) {
    const materialResource = this.materialPayloads.get(instance.materialId);
    const meshColor = this.#meshColorRGBA(instance);
    const color = rgbaToCssColor(
      meshColor ?? materialResource?.colorRGBA
        ?? this.#defaultColorForClass(instance.renderClass),
    );
    if (instance.geometryKind === RenderGeometryKind.mesh) {
      const materialOptions = {
        color: new THREE.Color(color.r / 255, color.g / 255, color.b / 255),
        map: this.texturePayloads.get(materialResource?.baseColorTextureUrl) ?? null,
        transparent: color.a < 1 || Boolean(materialResource?.baseColorTextureUrl),
        opacity: Math.max(materialResource?.baseColorTextureUrl ? 0 : 0.08, color.a),
        depthWrite: color.a >= 1 && !materialResource?.baseColorTextureUrl,
        side: THREE.DoubleSide,
        vertexColors: meshColor == null && Boolean(hasVertexColors),
        polygonOffset: instance.meshEdges === true,
        polygonOffsetFactor: 1,
        polygonOffsetUnits: 1,
      };
      return materialResource?.materialFlags & 1 ? new THREE.MeshBasicMaterial(materialOptions) : new THREE.MeshPhongMaterial(materialOptions);
    }
    return new THREE.LineBasicMaterial({
      color: new THREE.Color(color.r / 255, color.g / 255, color.b / 255),
      linewidth: Number(materialResource?.lineWidth ?? 1),
      transparent: color.a < 1,
      opacity: Math.max(0.08, color.a),
    });
  }

  #makeMaterialSignature(instance) {
    return [
      instance.materialId ?? "",
      instance.renderClass ?? "",
      instance.geometryKind ?? "",
      instance.flags ?? "",
      instance.meshEdges === true,
      this.#meshColorRGBA(instance) ?? "",
      this.materialPayloads.get(instance.materialId)?.dataVersion ?? "",
      this.materialPayloads.get(instance.materialId)?.baseColorTextureUrl ?? "",
      this.materialPayloads.get(instance.materialId)?.materialFlags ?? "",
    ].join("|");
  }

  #refreshObjectMaterial(object, instance) {
    if (!object?.material) {
      return;
    }
    const signature = this.#makeMaterialSignature(instance);
    if (object.userData.materialSignature === signature) {
      return;
    }
    const objectId = object.userData?.objectId;
    const wasSelected = this.#isObjectSelected(objectId);
    const wasEmphasized = this.#isObjectEmphasized(objectId);
    this.#setObjectInteractionVisual(object, false, false);
    const previousMaterial = object.material;
    object.material = this.#makeMaterial(instance, Boolean(object.userData?.hasVertexColors));
    previousMaterial?.dispose?.();
    object.userData.materialSignature = signature;
    this.#setObjectInteractionVisual(object, wasSelected, wasEmphasized);
  }

  #refreshSceneObjectMaterials() {
    for (const object of this.sceneObjects.values()) {
      const instance = object.userData?.instance;
      if (instance) {
        this.#refreshObjectMaterial(object, instance);
        this.#refreshMeshEdges(object, instance);
      }
    }
  }

  #defaultColorForClass(renderClass) {
    const override = this.options.renderClassColors?.[Number(renderClass)] ?? this.options.defaultGeometryColor;
    if (Number.isFinite(override)) return override;
    switch (Number(renderClass)) {
      case 5:
        return 0xFFB84DFF;
      case 6:
        return 0x55D88CFF;
      case 7:
      case 8:
        return 0xF8D66DFF;
      default:
        return 0x8FB8C9FF;
    }
  }

  #updateObjectsUsingGeometry(geometryId) {
    for (const object of this.sceneObjects.values()) {
      if (object.userData.geometryId !== geometryId) {
        continue;
      }
      const geometry = this.geometryObjects.get(geometryId);
      if (geometry && "geometry" in object) {
        const hadVertexColors = Boolean(object.userData?.hasVertexColors);
        const hasVertexColors = Boolean(geometry.getAttribute?.("color"));
        object.geometry = geometry;
        object.userData.hasVertexColors = hasVertexColors;
        if (hadVertexColors !== hasVertexColors) {
          object.userData.materialSignature = null;
        }
        if (object.userData.instance) {
          this.#refreshObjectMaterial(object, object.userData.instance);
          this.#refreshMeshEdges(object, object.userData.instance);
        }
      }
    }
  }

  #updateObjectsUsingTransform(transformId) {
    for (const object of this.sceneObjects.values()) {
      const instance = object.userData.instance;
      if (instance?.transformId !== transformId) {
        continue;
      }
      const transform = this.transformPreviews.get(transformId) ?? this.transformPayloads.get(transformId);
      object.matrixAutoUpdate = false;
      object.matrix.fromArray(transform?.localToWorld ?? identityMatrixArray());
      object.matrixWorldNeedsUpdate = true;
    }
    this.#updateColliderUsingTransform(transformId);
    this.#updateSelectionHelper();
  }

  #updateColliderUsingTransform(transformId) {
    const object = this.colliderObjects.get(String(transformId));
    if (!object) {
      return;
    }
    const transform = this.transformPreviews.get(String(transformId)) ?? this.transformPayloads.get(String(transformId));
    object.matrixAutoUpdate = false;
    object.matrix.fromArray(transform?.localToWorld ?? identityMatrixArray());
    object.matrixWorldNeedsUpdate = true;
  }

  #removeSlot(payload, removeContent = true) {
    this.slotDescriptors.delete(payload.pdoId);
    if (!removeContent) {
      return;
    }
    if (payload.slotRole === "Transform" && payload.transformId) {
      this.transformPreviews.delete(payload.transformId);
      this.transformPayloads.delete(payload.transformId);
      this.#updateObjectsUsingTransform(payload.transformId);
      this.#applyActiveCamera();
      return;
    }
    if (payload.slotRole === "Object" && payload.objectId) {
      this.#removeSceneObject(payload.objectId);
      this.objectSlots.delete(payload.objectId);
      this.instancePayloads.delete(String(payload.objectId));
      return;
    }
    if (payload.geometryId) {
      const geometry = this.geometryObjects.get(payload.geometryId);
      geometry?.dispose?.();
      this.geometryObjects.delete(payload.geometryId);
      this.geometryPayloads.delete(payload.geometryId);
    }
  }

  #removeColliderSlot(payload) {
    this.colliderSlotDescriptors.delete(payload.pdoId);
    const objectId = String(payload.objectId ?? "").trim();
    if (!objectId) {
      return;
    }
    this.colliderPayloads.delete(objectId);
    const object = this.colliderObjects.get(objectId);
    if (!object) {
      return;
    }
    object.parent?.remove(object);
    disposeObject3D(object);
    this.colliderObjects.delete(objectId);
  }

  #removeSceneObject(objectId) {
    const normalizedId = String(objectId);
    const object = this.sceneObjects.get(normalizedId);
    if (!object) {
      return;
    }
    this.#setObjectInteractionVisual(object, false, false);
    this.#removeMeshEdges(object);
    object.parent?.remove(object);
    object.material?.dispose?.();
    this.sceneObjects.delete(normalizedId);
    if (this.hoveredObjectId === normalizedId) this.#clearHover();
    this.selectedObjectIds.delete(normalizedId);
    this.emphasizedObjectIds.delete(normalizedId);
    if (this.selectedObjectId === normalizedId) {
      this.selectedObjectId = [...this.selectedObjectIds][0] || "";
    }
    this.#updateSelectionHelper();
  }

  #clearViewContent() {
    for (const object of this.sceneObjects.values()) {
      this.#setObjectInteractionVisual(object, false, false);
      this.#removeMeshEdges(object);
      object.parent?.remove(object);
      object.material?.dispose?.();
    }
    for (const geometry of this.geometryObjects.values()) {
      geometry.dispose?.();
    }
    this.sceneObjects.clear();
    this.geometryObjects.clear();
    this.geometryPayloads.clear();
    this.materialPayloads.clear();
    this.instancePayloads.clear();
    this.objectSlots.clear();
    this.transformPayloads.clear();
    this.transformPreviews.clear();
    this.cameras.clear();
    this.activeCameraId = null;
    this.appliedViewRevision = "0";
    this.selectionAxisHelper.visible = false;
  }

  #clearRenderContent() {
    this.clearTransformManipulator();
    this.#clearHover();
    this.clearGhostMesh();
    this.#clearViewContent();
    this.textureGeneration++;this.desiredTextureUrls.clear();
    for (const texture of this.texturePayloads.values()) texture.dispose();
    this.texturePayloads.clear();this.texturePromises.clear();
    for (const object of this.colliderObjects.values()) {
      object.parent?.remove(object);
      disposeObject3D(object);
    }
    this.colliderObjects.clear();
    this.colliderPayloads.clear();
    this.colliderSlotDescriptors.clear();
    this.selectedObjectId = "";
    this.selectedObjectIds.clear();
    this.emphasizedObjectIds.clear();
    this.selectionAxisHelper.visible = false;
    this.diagnosticKeys.clear();
  }

  #updateSelectionVisuals() {
    for (const object of this.sceneObjects.values()) {
      this.#setObjectSelectedVisual(object, this.#isObjectSelected(object.userData?.objectId));
    }
    this.#updateSelectionHelper();
  }

  #updateSelectionHelper() {
    const objectId = this.selectedObjectId || [...this.selectedObjectIds][0] || "";
    if (!objectId || this.transformManipulator) {
      this.selectionAxisHelper.visible = false;
      return;
    }

    const transform = this.transformPreviews.get(objectId) ?? this.transformPayloads.get(objectId);
    const object = this.sceneObjects.get(objectId);
    const matrix = new THREE.Matrix4();
    if (transform?.localToWorld) {
      matrix.fromArray(transform.localToWorld);
    } else if (object) {
      object.updateWorldMatrix(true, false);
      matrix.copy(object.matrixWorld);
    } else {
      this.selectionAxisHelper.visible = false;
      return;
    }

    const position = new THREE.Vector3();
    const rotation = new THREE.Quaternion();
    const sourceScale = new THREE.Vector3();
    matrix.decompose(position, rotation, sourceScale);
    this.selectionAxisHelper.position.copy(position);
    this.selectionAxisHelper.quaternion.copy(rotation);
    if (this.transformSpace === 'world') this.selectionAxisHelper.quaternion.identity();
    this.selectionAxisHelper.scale.setScalar(this.#selectionAxisWorldScale(position));
    this.selectionAxisHelper.visible = true;
    this.selectionAxisHelper.updateMatrixWorld(true);
  }

  #isObjectSelected(objectId) {
    return this.selectedObjectIds.has(String(objectId ?? "").trim());
  }

  #isObjectEmphasized(objectId) {
    return this.emphasizedObjectIds.has(String(objectId ?? "").trim());
  }

  #setObjectSelectedVisual(object, isSelected) {
    this.#setObjectInteractionVisual(
      object,
      isSelected,
      this.#isObjectEmphasized(object?.userData?.objectId),
    );
  }

  #setObjectInteractionVisual(object, isSelected, isEmphasized) {
    if (!object) return;
    const isDimmed = this.emphasisDimOpacity !== null && this.emphasizedObjectIds.size > 0 && !isEmphasized;
    const apply = (node) => {
      // Keep a contrasting rim when the surface is selected or emphasized.
      if (node?.userData?.independentInteractionVisual || node?.userData?.meshFeatureEdges && !isDimmed && !node.userData.selectionMaterialState) return;
      const materials = Array.isArray(node?.material)
        ? node.material.filter(Boolean)
        : (node?.material ? [node.material] : []);
      if (!materials.length) return;
      if (!node.userData.selectionMaterialState && (isSelected || isEmphasized || isDimmed)) {
        node.userData.selectionMaterialState = materials.map((material) => this.#captureSelectionMaterialState(material));
      }
      const states = Array.isArray(node.userData.selectionMaterialState)
        ? node.userData.selectionMaterialState : [];
      materials.forEach((material, index) => this.#restoreSelectionMaterialState(material, states[index]));
      if (!isSelected && !isEmphasized && !isDimmed) {
        node.userData.selectionMaterialState = null;
        return;
      }
      // Purple panel emphasis intentionally takes visual priority while active.
      // Clearing it restores an existing orange parts-list selection exactly.
      const visual = node.userData.meshFeatureEdges ? null : isEmphasized ? this.emphasisVisual : isSelected ? this.selectionVisual : null;
      for (const material of materials) {
        if (visual && material.color) material.color.setHex(visual.color);
        if (visual && material.emissive) {
          material.emissive.setHex(visual.emissive);
          material.emissiveIntensity = visual.emissiveIntensity;
        }
        if (visual && "linewidth" in material) {
          material.linewidth = Math.max(visual.linewidth, Number(material.linewidth ?? 1));
        }
        if (isDimmed) { material.opacity *= this.emphasisDimOpacity; material.transparent = true; material.depthWrite = false; }
        material.needsUpdate = true;
      }
    };
    if (typeof object.traverse === "function") object.traverse(apply);
    else apply(object);
  }

  #captureSelectionMaterialState(material) {
    return {
      color: material.color?.clone?.() ?? null,
      emissive: material.emissive?.clone?.() ?? null,
      emissiveIntensity: material.emissiveIntensity,
      linewidth: material.linewidth,
      opacity: material.opacity,
      transparent: material.transparent,
      depthWrite: material.depthWrite,
    };
  }

  #restoreSelectionMaterialState(material, state) {
    if (!state) {
      return;
    }
    if (state.color && material.color) {
      material.color.copy(state.color);
    }
    if (state.emissive && material.emissive) {
      material.emissive.copy(state.emissive);
    }
    if (state.emissiveIntensity !== undefined && "emissiveIntensity" in material) {
      material.emissiveIntensity = state.emissiveIntensity;
    }
    if (state.linewidth !== undefined && "linewidth" in material) {
      material.linewidth = state.linewidth;
    }
    for (const key of ["opacity", "transparent", "depthWrite"]) if (state[key] !== undefined) material[key] = state[key];
    material.needsUpdate = true;
  }

  #applyTransform(payload) {
    this.transformPayloads.set(payload.transformId, payload);
    this.#updateObjectsUsingTransform(payload.transformId);
    if (this.activeCameraId && this.cameras.get(this.activeCameraId)?.transformId === payload.transformId) {
      this.#applyActiveCamera();
    }
  }

  #applyCamera(payload) {
    if (!payload?.cameraId) {
      return;
    }
    this.cameras.set(payload.cameraId, payload);
    if (payload.active) {
      this.activeCameraId = payload.cameraId;
    } else if (this.activeCameraId === payload.cameraId) {
      this.activeCameraId = null;
    }
    this.#applyActiveCamera();
  }

  #applyActiveCamera() {
    const camera = this.activeCameraId ? this.cameras.get(this.activeCameraId) : null;
    if (!camera) {
      return;
    }
    this.camera.near = camera.nearPlane || 0.1;
    this.camera.far = camera.farPlane || 1000000;
    this.perspectiveFov = THREE.MathUtils.radToDeg(
      camera.verticalFovRadians || 0.785398185,
    );
    this.camera.matrixAutoUpdate = false;
    const transform = this.transformPayloads.get(camera.transformId);
    if (transform?.localToWorld) {
      this.camera.matrix.fromArray(transform.localToWorld);
      this.camera.matrix.multiply(ENGINE_CAMERA_TO_THREE_CAMERA_MATRIX);
      this.camera.matrix.decompose(this.camera.position, this.camera.quaternion, this.camera.scale);
      this.camera.updateMatrixWorld(true);
    }
    this.#updateProjectionMatrix();
  }

  #installSceneBasics() {
    if (this.options.showGrid !== false) {
      const {size,divisions}=this.getGridState();
      const grid = new THREE.GridHelper(size, divisions, 0x38505d, 0x2a3a43);
      grid.name = "iCAX XY Workbench Grid";
      grid.rotation.x = Math.PI / 2;
      this.scene.add(grid);
    }

    if (this.options.showWorldAxes !== false) {
      const length = this.options.worldAxisLength ?? 120;
      const axis = new THREE.AxesHelper(length);
      const group = new THREE.Group();
      group.name = "iCAX World XYZ";
      group.add(axis);
      this.scene.add(group);
      if (this.options.worldAxisLabels === true) {
        this.worldAxes = group;
        for (const [label, color, direction] of [["X", 0xff4f4f, [1,0,0]], ["Y", 0x4fd66f, [0,1,0]], ["Z", 0x5aa7ff, [0,0,1]]]) {
          const arrow = new THREE.ArrowHelper(new THREE.Vector3(...direction), new THREE.Vector3(), length, color, length * .08, length * .035);
          arrow.name = `iCAX World ${label}`;
          group.add(arrow);
          const sprite = this.#createAxisLabel(label, color, new THREE.Vector3(...direction).multiplyScalar(length * 1.1));
          sprite.scale.setScalar(length * .18);
          sprite.name = `iCAX World ${label} Label`;
          group.add(sprite);
        }
        group.traverse(object => {
          object.renderOrder = 9999;
          for (const material of [object.material].flat().filter(Boolean)) {
            material.depthTest = false;
            material.depthWrite = false;
            material.transparent = true;
            material.opacity = this.options.worldAxisOpacity ?? 1;
          }
        });
      }
    }

    const ambient = new THREE.AmbientLight(0xffffff, 0.58);
    this.scene.add(ambient);

    const key = new THREE.DirectionalLight(0xffffff, 0.75);
    key.position.set(0.4, -0.6, 0.8);
    this.scene.add(key);
  }

  #createSelectionAxisHelper() {
    const group = new THREE.Group();
    group.name = "iCAX Selected Object Local Axes";
    group.visible = false;
    group.renderOrder = 10000;

    const origin = new THREE.Mesh(
      new THREE.SphereGeometry(0.07, 18, 12),
      new THREE.MeshBasicMaterial({
        color: 0xf4f7f8,
        transparent: true,
        opacity: 0.88,
        depthTest: false,
        depthWrite: false,
      }),
    );
    origin.renderOrder = 10000;
    group.add(origin);

    group.add(this.#createSelectionAxisArrow(new THREE.Vector3(1, 0, 0), 0xff4a35));
    group.add(this.#createSelectionAxisArrow(new THREE.Vector3(0, 1, 0), 0x79ff45));
    group.add(this.#createSelectionAxisArrow(new THREE.Vector3(0, 0, 1), 0x4c8dff));
    return group;
  }

  #createSelectionAxisArrow(direction, color) {
    const arrow = new THREE.ArrowHelper(
      direction,
      new THREE.Vector3(0, 0, 0),
      1.0,
      color,
      0.24,
      0.1,
    );
    arrow.renderOrder = 10000;
    arrow.traverse((object) => {
      object.renderOrder = 10000;
      const material = object.material;
      if (!material) {
        return;
      }
      material.depthTest = false;
      material.depthWrite = false;
      material.transparent = true;
      material.opacity = 0.98;
      material.needsUpdate = true;
    });
    return arrow;
  }

  #selectionAxisWorldScale(position) {
    const canvas = this.renderer.domElement;
    const pixelRatio = this.renderer.getPixelRatio?.() || 1;
    const canvasHeight = Math.max(1, canvas.clientHeight || Math.floor(canvas.height / pixelRatio) || 1);
    if (this.camera.isOrthographicCamera) {
      const visibleHeight = Math.abs((this.camera.top ?? 1) - (this.camera.bottom ?? -1)) / Math.max(0.0001, this.camera.zoom || 1);
      return Math.max(SELECTION_AXIS_MIN_SCALE, (visibleHeight / canvasHeight) * SELECTION_AXIS_PIXEL_SIZE);
    }

    const distance = Math.max(0.0001, this.camera.position.distanceTo(position));
    const fovRadians = THREE.MathUtils.degToRad(this.perspectiveFov);
    const visibleHeight = 2 * Math.tan(fovRadians * 0.5) * distance;
    return Math.max(SELECTION_AXIS_MIN_SCALE, (visibleHeight / canvasHeight) * SELECTION_AXIS_PIXEL_SIZE);
  }

  #installDisplayModeControl() {
    const control = document.createElement("div");
    control.className = "icax-three-display-mode";
    control.hidden = this.options.showDisplayModeControl !== true;
    const select = document.createElement("select");
    select.setAttribute("aria-label", "显示模式");
    for (const [value, label] of [["shaded", "着色"], ["shadedEdges", "着色+线框"], ["wireframe", "线框"]]) {
      const option = document.createElement("option");
      option.value = value; option.textContent = label; select.appendChild(option);
    }
    control.append(select);
    this.#listen(select, "change", () => this.setDisplayMode(select.value));
    for (const event of ["pointerdown", "pointerup", "click", "dblclick", "wheel", "keydown"]) {
      this.#listen(control, event, e => e.stopPropagation());
    }
    this.displayModeControl = control; this.displayModeSelect = select;
    this.root.appendChild(control);
    this.#syncDisplayModeControl();
  }

  #syncDisplayModeControl() {
    if (!this.displayModeControl) return;
    this.displayModeControl.hidden = this.options.showDisplayModeControl !== true;
    this.displayModeSelect.value = this.displayMode ?? (this.options.meshEdges ? "shadedEdges" : "shaded");
    this.root.dataset.displayMode = this.displayModeSelect.value;
  }

  #installProjectionToggle() {
    this.projectionToggle = document.createElement("div");
    this.projectionToggle.className = "icax-three-projection-toggle";
    this.projectionToggle.setAttribute("role", "group");
    this.projectionToggle.setAttribute("aria-label", "投影方式");
    this.projectionButtons = new Map();
    for (const [mode, label] of [["perspective", "透视"], ["orthographic", "正交"]]) {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = label;
      button.dataset.projectionMode = mode;
      button.title = mode === "perspective" ? "切换为透视投影" : "切换为正交投影";
      this.#listen(button, "click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        this.setProjectionMode(mode, { notify: true });
      });
      this.projectionButtons.set(mode, button);
      this.projectionToggle.appendChild(button);
    }
    this.root.appendChild(this.projectionToggle);
    this.setProjectionToggleVisible(this.options.showProjectionToggle === true);
    this.#syncProjectionToggle();
    this.root.dataset.orbitConstrained = String(this.options.constrainOrbit !== false);
  }

  #installTransformSpaceToggle() {
    this.transformSpace = this.options.transformSpace ?? 'local';
    if (!this.options.showTransformSpaceToggle) return;
    const group = document.createElement('div');
    group.className = 'icax-three-transform-space';
    group.setAttribute('role','group'); group.setAttribute('aria-label','操作坐标系');
    for (const [space,label] of [['world','世界'],['local','局部']]) {
      const button = document.createElement('button');
      button.type='button'; button.textContent=label;button.dataset.transformSpace=space;
      button.classList.toggle('active',space===this.transformSpace);
      button.setAttribute('aria-pressed',String(space===this.transformSpace));
      this.#listen(button,'click',event=>{event.stopPropagation();this.setTransformSpace(space,true);});
      group.append(button);
    }
    this.transformSpaceToggle=group;this.root.append(group);
  }

  #syncProjectionToggle() {
    for (const [mode, button] of this.projectionButtons ?? []) {
      const active = mode === this.projectionMode;
      button.classList.toggle("active", active);
      button.setAttribute("aria-pressed", String(active));
    }
    if (this.projectionToggle) {
      this.projectionToggle.dataset.projectionMode = this.projectionMode;
    }
  }

  #installOrientationGizmo() {
    this.axisHost = document.createElement("div");
    this.axisHost.className = "icax-three-axis-gizmo";
    this.root.appendChild(this.axisHost);

    this.axisScene = new THREE.Scene();
    this.axisCamera = new THREE.PerspectiveCamera(35, 1, 0.1, 100);
    this.axisCamera.up.set(0, 0, 1);
    this.axisRenderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.axisRenderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.axisRenderer.setSize(86, 86, false);
    this.axisRenderer.domElement.className = "icax-three-axis-canvas";
    this.axisHost.appendChild(this.axisRenderer.domElement);

    const axis = new THREE.AxesHelper(1.35);
    this.axisScene.add(axis);
    const origin = new THREE.Mesh(
      new THREE.SphereGeometry(0.045, 12, 8),
      new THREE.MeshBasicMaterial({ color: 0xffffff }),
    );
    this.axisScene.add(origin);
    this.axisScene.add(this.#createAxisLabel("X", 0xff4f4f, new THREE.Vector3(1.58, 0, 0)));
    this.axisScene.add(this.#createAxisLabel("Y", 0x4fd66f, new THREE.Vector3(0, 1.58, 0)));
    this.axisScene.add(this.#createAxisLabel("Z", 0x5aa7ff, new THREE.Vector3(0, 0, 1.58)));
  }

  #createAxisLabel(text, color, position) {
    const canvas = document.createElement("canvas");
    canvas.width = 64;
    canvas.height = 64;
    const context = canvas.getContext("2d");
    context.clearRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = `#${color.toString(16).padStart(6, "0")}`;
    context.font = "bold 34px sans-serif";
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(text, 32, 32);
    const texture = new THREE.CanvasTexture(canvas);
    const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, transparent: true, depthTest: false }));
    sprite.position.copy(position);
    sprite.scale.set(0.38, 0.38, 0.38);
    return sprite;
  }

  #createDimensionLabel(text, color) {
    const canvas = document.createElement("canvas");
    const context = canvas.getContext("2d");
    context.font = "600 28px sans-serif";
    const textWidth = Math.ceil(context.measureText(text).width);
    canvas.width = Math.max(96, Math.min(1024, textWidth + 34));
    canvas.height = 54;
    context.fillStyle = "rgba(14, 31, 38, 0.92)";
    context.strokeStyle = `#${color.getHexString()}`;
    context.lineWidth = 2;
    context.beginPath();
    context.roundRect(1, 1, canvas.width - 2, canvas.height - 2, 10);
    context.fill();
    context.stroke();
    context.fillStyle = "#f7fbfc";
    context.font = "600 28px sans-serif";
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(text, canvas.width / 2, canvas.height / 2 + 1);
    const texture = new THREE.CanvasTexture(canvas);
    texture.needsUpdate = true;
    const material = new THREE.SpriteMaterial({
      map: texture,
      transparent: true,
      depthTest: false,
      depthWrite: false,
    });
    const sprite = new THREE.Sprite(material);
    sprite.userData.dimensionLabel = true;
    sprite.userData.dimensionLabelAspect = canvas.width / canvas.height;
    return sprite;
  }

  #specificationLabelAtPointer(event) {
    return [...this.specificationLabels].reverse().find(({ element, pickThrough }) => {
      if (element.hidden || pickThrough) return false;
      const rect = element.getBoundingClientRect();
      return event.clientX >= rect.left && event.clientX <= rect.right
        && event.clientY >= rect.top && event.clientY <= rect.bottom;
    });
  }

  #viewportOverlayAtPointer(event) {
    const host = this.root.closest(".cam-viewport") ?? this.root;
    const elements = [
      ...this.outsideDimensionLabels.map(({ element }) => element),
      ...host.querySelectorAll(".cam-viewcube, .icax-three-axis-gizmo, .icax-three-projection-toggle, .icax-three-transform-space, .icax-three-display-mode"),
    ];
    return elements.some(element => {
      if (element.hidden) return false;
      const rect = element.getBoundingClientRect();
      return rect.width > 0 && rect.height > 0
        && event.clientX >= rect.left && event.clientX <= rect.right
        && event.clientY >= rect.top && event.clientY <= rect.bottom;
    });
  }

  #annotationGraphicAtPointer(event) {
    // Dimension captions can be WebGL sprites or DOM labels. Their rulers and
    // leaders still occupy the canvas even though they never select a model.
    const point = new DOMPoint(event.clientX, event.clientY);
    for (const svg of [this.dimensionLeaderSvg, this.specificationLeaderSvg]) {
      for (const shape of svg.querySelectorAll("path, line")) {
        if (!shape.getClientRects().length) continue;
        const matrix = shape.getScreenCTM();
        if (matrix && shape.isPointInStroke(point.matrixTransform(matrix.inverse()))) return true;
      }
    }
    const canvas = this.renderer.domElement, bounds = canvas.getBoundingClientRect();
    if (!bounds.width || !bounds.height) return false;
    this.pointer.set((event.clientX - bounds.left) / bounds.width * 2 - 1,
      -(event.clientY - bounds.top) / bounds.height * 2 + 1);
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const objects = [this.dimensionContent, this.specificationContent, this.measurementContent]
      .filter(group => group.visible).flatMap(group => group.children.filter(object => object.visible));
    const lineThreshold = this.raycaster.params.Line.threshold;
    const pointThreshold = this.raycaster.params.Points.threshold;
    const worldPerPixel = this.#visibleHeightAtTarget() / bounds.height;
    this.raycaster.params.Line.threshold = worldPerPixel * 3;
    this.raycaster.params.Points.threshold = worldPerPixel * 5;
    try {
      return this.raycaster.intersectObjects(objects, false).length > 0;
    } finally {
      this.raycaster.params.Line.threshold = lineThreshold;
      this.raycaster.params.Points.threshold = pointThreshold;
    }
  }

  #cancelBlankPick() {
    if (this.pendingBlankPick != null) clearTimeout(this.pendingBlankPick);
    this.pendingBlankPick = null;
  }

  #notifyHover(hit, event, hits, pointerActive) {
    const objectId = hit ? String(hit.object?.userData?.objectId ?? hit.object?.uuid ?? "") : "";
    // Feature tools need movement within a single mesh (face, edge or snap point).
    // Object-level consumers retain the inexpensive enter/leave notification.
    if (objectId === this.hoveredObjectId && pointerActive === this.hoverPointerActive
        && !(this.options.hoverDetail === "surface" && event?.type === "pointermove")) return;
    this.hoveredObjectId = objectId;
    this.hoverPointerActive = pointerActive;
    this.options.onHover?.(hit?.object?.userData ?? null, hit ?? null, event ?? null, hits, { pointerActive });
  }

  #clearHover(event = null, pointerActive = false) {
    if (this.hoverFrame != null) cancelAnimationFrame(this.hoverFrame);
    this.hoverFrame = null;
    this.pendingHoverEvent = null;
    this.#notifyHover(null, event, [], pointerActive);
  }

  #queueHover(event) {
    if (this.options.hoverEnabled === false || typeof this.options.onHover !== "function") return;
    this.pendingHoverEvent = event;
    if (this.hoverFrame != null) return;
    this.hoverFrame = requestAnimationFrame(() => {
      this.hoverFrame = null;
      const current = this.pendingHoverEvent;
      this.pendingHoverEvent = null;
      if (!current || this.isDisposed) return;
      if (!this.pickingEnabled || this.navigation.pointerId != null || current.buttons
          || this.#specificationLabelAtPointer(current) || this.#viewportOverlayAtPointer(current)) {
        this.#clearHover(current, true);
        return;
      }
      const hits = this.#hitsAtPointer(current);
      this.#notifyHover(hits[0] ?? null, current, hits, true);
    });
  }

  #installPointerControls() {
    const canvas = this.renderer.domElement;
    let blankContextClick = null;
    const blankContextPoint = event => event.target === canvas
      && canvas.ownerDocument.elementFromPoint(event.clientX, event.clientY) === canvas
      && !this.#viewportOverlayAtPointer(event) && !this.#specificationLabelAtPointer(event)
      && !this.#hitsAtPointer(event).length && !this.#annotationGraphicAtPointer(event)
      && !this.transformManipulator?.hover({ clientX:event.clientX, clientY:event.clientY, button:2, buttons:0 });
    // Resting specification labels deliberately do not participate in pointer
    // hit-testing, so orbit/pan/zoom continue to use the canvas even when the
    // cursor is visually over a label. Their screen rectangles still own left
    // clicks, including both clicks before double-click editing starts.
    this.#listen(canvas, "dblclick", (event) => {
      if (event.button !== 0 || event.target !== canvas) return;
      if (this.transformManipulator?.hover(event)) { event.preventDefault(); return; }
      this.#cancelBlankPick();
      const entry = this.#specificationLabelAtPointer(event);
      if (entry) {
        if (entry.element.classList.contains("is-editing")) return;
        const trigger = entry.element.querySelector(".icax-three-specification-trigger");
        if (!trigger) return;
        event.preventDefault();
        event.stopPropagation();
        trigger.click();
        return;
      }
      if (!this.cameraNavigationEnabled || !this.blankDoubleClickFitEnabled
        || this.#viewportOverlayAtPointer(event) || this.#hitsAtPointer(event).length
        || this.#annotationGraphicAtPointer(event)) return;
      event.preventDefault();
      event.stopPropagation();
      this.fitViewToViewport(Number(this.options.fitViewPadding ?? 1.35));
    });
    this.#listen(canvas, "pointerdown", (event) => {
      blankContextClick = null;
      if (!this.#viewportOverlayAtPointer(event) && this.transformManipulator?.down(event)) {
        this.#cancelBlankPick();this.#clearHover(event,true);this.#resetNavigation();return;
      }
      if (this.navigation.pointerId != null) this.#resetNavigation();
      this.#cancelBlankPick();
      this.#clearHover(event, true);
      // A blank context click is independent of camera-navigation permission.
      // Products decide its meaning; the viewport only distinguishes clicks,
      // drags, model/handle hits and gestures that began on another surface.
      if (event.button === 2 && event.buttons === 2 && typeof this.options.onBlankContextClick === 'function'
          && blankContextPoint(event)) blankContextClick = { pointerId:event.pointerId, x:event.clientX, y:event.clientY, moved:false };
      const pickAllowed = event.button === 0 && !this.#specificationLabelAtPointer(event)
        && !this.#viewportOverlayAtPointer(event);
      const mode = typeof this.options.getCameraNavigationMode === 'function'
        ? this.options.getCameraNavigationMode(event)
        : (event.button === 2 ? 'orbit' : (event.button === 1 ? 'pan' : null));
      if(mode&&!this.cameraNavigationEnabled){event.preventDefault();return;}
      canvas.focus?.({ preventScroll: true });
      this.navigation.pointerId = event.pointerId;
      this.navigation.mode = mode;
      this.navigation.orbitPivot=mode==='orbit'?this.captureOrbitPivot(event):null;
      this.navigation.axisSnap=mode==='orbit'&&this.options.orbitAxisSnap===true?new OrbitAxisSnap():null;
      this.navigation.lastX = event.clientX;
      this.navigation.lastY = event.clientY;
      this.navigation.moved = false;
      this.navigation.pickAllowed = pickAllowed;
      this.rectangleSelectionKey = pickAllowed && this.pickingEnabled
        ? this.options.getRectangleSelectionKey?.() ?? null : null;
      if (mode || this.rectangleSelectionKey != null) {
        event.preventDefault();
        canvas.setPointerCapture?.(event.pointerId);
      }
    });
    this.#listen(canvas, "pointerup", (event) => {
      const contextClick = blankContextClick; blankContextClick = null;
      if (this.transformManipulator?.up(event)) return;
      const startedOnCanvas = this.navigation.pointerId === event.pointerId;
      const wasNavigation = startedOnCanvas && Boolean(this.navigation.mode);
      const pickAllowed = startedOnCanvas && this.navigation.pickAllowed;
      const moved = this.navigation.moved;
      const rectangle = startedOnCanvas && this.selectionRectangle && event.button === 0
        && this.rectangleSelectionKey === this.options.getRectangleSelectionKey?.()
        ? this.#updateSelectionRectangle(event) : null;
      if (startedOnCanvas) {
        this.#resetNavigation();
      }
      if (canvas.hasPointerCapture?.(event.pointerId)) {
        canvas.releasePointerCapture(event.pointerId);
      }
      if (rectangle) {
        event.preventDefault();
        this.options.onRectangleSelect?.(rectangle, event);
        return;
      }
      if (contextClick?.pointerId === event.pointerId && event.button === 2 && !contextClick.moved
          && Math.hypot(event.clientX-contextClick.x,event.clientY-contextClick.y) <= 3 && blankContextPoint(event)) {
        event.preventDefault(); this.options.onBlankContextClick?.(event); return;
      }
      if (pickAllowed && !wasNavigation && !moved && event.button === 0
          && !this.#specificationLabelAtPointer(event) && !this.#viewportOverlayAtPointer(event)) {
        this.#emitPick(event);
      }
    });
    this.#listen(canvas, "pointermove", (event) => {
      if (this.transformManipulator?.move(event)) { this.#clearHover(event,true);return; }
      if (this.navigation.pointerId !== event.pointerId) {
        if (this.navigation.pointerId == null && !event.buttons) this.#queueHover(event);
        else this.#clearHover(event, true);
        return;
      }
      this.#clearHover(event, true);
      if (!this.navigation.mode) {
        if (this.rectangleSelectionKey != null
          && this.rectangleSelectionKey !== this.options.getRectangleSelectionKey?.()) {
          this.#resetNavigation();
          return;
        }
        if (Math.hypot(event.clientX - this.navigation.lastX,
          event.clientY - this.navigation.lastY) > 3) {
          this.navigation.moved = true;
          this.#cancelBlankPick();
        }
        if (this.navigation.moved && this.rectangleSelectionKey != null) {
          event.preventDefault();
          this.#updateSelectionRectangle(event);
        }
        return;
      }
      const dx = event.clientX - this.navigation.lastX;
      const dy = event.clientY - this.navigation.lastY;
      this.navigation.lastX = event.clientX;
      this.navigation.lastY = event.clientY;
      if (Math.abs(dx) + Math.abs(dy) > 0) {
        this.navigation.moved = true;
        this.#applyLocalNavigation(this.navigation.mode, dx, dy, event);
      }
    });
    this.#listen(canvas, "wheel", (event) => {
      if (!this.cameraNavigationEnabled || this.transformManipulator?.pointerId != null) {event.preventDefault();return;}
      if (this.rectangleSelectionKey != null) this.#resetNavigation();
      this.#cancelBlankPick();
      this.#clearHover(event, true);
      event.preventDefault();
      const speed = Number(this.options.zoomSpeed ?? 0.0015);
      let zoomAnchor = null, zoomPlane = null;
      if (this.options.zoomToPointer === true) {
        const hits = this.#hitsAtPointer(event);
        const target = this.cameraState.target;
        zoomAnchor = hits[0]?.point?.clone() ?? new THREE.Vector3(target.x, target.y, target.z);
        zoomPlane = new THREE.Plane().setFromNormalAndCoplanarPoint(
          this.camera.getWorldDirection(new THREE.Vector3()), zoomAnchor);
        zoomAnchor = this.raycaster.ray.intersectPlane(zoomPlane, new THREE.Vector3());
      }
      this.cameraState.radius = this.#clampCameraRadius(
        this.cameraState.radius * Math.exp((event.deltaY || 0) * speed),
      );
      this.#updateCamera();
      if (zoomAnchor) {
        this.raycaster.setFromCamera(this.pointer, this.camera);
        const after = this.raycaster.ray.intersectPlane(zoomPlane, new THREE.Vector3());
        if (after) {
          const delta = zoomAnchor.sub(after);
          this.cameraState.target.x += delta.x;
          this.cameraState.target.y += delta.y;
          this.cameraState.target.z += delta.z;
          this.#updateCamera();
        }
      }
      this.#renderOnce();
      this.#emitCameraChange("zoom");
    }, { passive: false });
    this.#listen(canvas, "contextmenu", (event) => event.preventDefault());
    this.#listen(window, "pointermove", event => {
      if (blankContextClick?.pointerId === event.pointerId
          && Math.hypot(event.clientX-blankContextClick.x,event.clientY-blankContextClick.y)>3) blankContextClick.moved=true;
    });
    this.#listen(canvas, "pointerleave", event => this.#clearHover(event));
    this.#listen(canvas, "pointercancel", event => {this.transformManipulator?.finish(true);this.#clearHover(event);});
    this.#listen(canvas, "lostpointercapture", event => {
      if (this.transformManipulator?.pointerId === event.pointerId) this.transformManipulator.finish(true);
      if (this.navigation.pointerId === event.pointerId) this.#resetNavigation();
    });
    this.#listen(window, "keydown", event => {
      if(this.transformManipulator?.modifier(event)){event.stopImmediatePropagation();return;}
      if (event.key==='Escape' && this.transformManipulator?.finish(true)) {
        event.preventDefault();event.stopImmediatePropagation();return;
      }
      if (event.key !== "Escape" || this.rectangleSelectionKey == null) return;
      event.preventDefault();
      event.stopPropagation();
      this.#resetNavigation();
    }, { capture: true });
    this.#listen(window,"keyup",event=>{if(this.transformManipulator?.modifier(event))event.stopImmediatePropagation();},{capture:true});
    const finishPointer = (event) => {
      if (blankContextClick?.pointerId === event.pointerId) blankContextClick = null;
      if (this.navigation.pointerId === event.pointerId) this.#resetNavigation();
    };
    // A gesture may end on an editor or another overlay instead of the canvas.
    // Clear its origin so a later release cannot inherit permission to pick.
    this.#listen(window, "pointerup", finishPointer);
    this.#listen(window, "pointercancel", finishPointer);
    this.#listen(window, "pointerdown", () => this.#cancelBlankPick(), { capture: true });
    this.#listen(window, "blur", () => {
      blankContextClick = null;
      this.transformManipulator?.finish(true);
      this.#cancelBlankPick();
      this.#clearHover();
      this.#resetNavigation();
    });
  }

  #listen(target, type, handler, options) {
    target.addEventListener(type, handler, options);
    this.domListeners.push(() => target.removeEventListener(type, handler, options));
  }

  #removeDomListeners() {
    for (const remove of this.domListeners.splice(0)) {
      remove();
    }
  }

  #hitsAtPointer(event) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    if (!rect.width || !rect.height) return [];
    this.pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    this.pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(this.pointer, this.camera);
    return this.raycaster.intersectObjects(
      [...this.sceneObjects.values()].filter((object) => object.visible
        && this.options.isObjectPickable?.(object.userData.instance) !== false),
      false,
    );
  }

  #emitPick(event) {
    if (!this.pickingEnabled || typeof this.options.onPick !== "function") return;
    const hits = this.#hitsAtPointer(event);
    this.#cancelBlankPick();
    if (hits.length) {
      this.options.onPick(hits[0].object.userData ?? null, hits[0], event, hits);
      return;
    }
    if (!this.blankDoubleClickFitEnabled || this.options.immediateBlankPick === true) {
      this.options.onPick(null, null, event, []);
      return;
    }
    // Products using immediateBlankPick process the first click immediately,
    // even when a second click subsequently fits the view.
    // Otherwise a blank double click fits without changing the selection.
    // Defer only blank single clicks long enough for the browser to distinguish
    // the gesture; component selection remains immediate.
    const revision = this.appliedViewRevision;
    this.pendingBlankPick = setTimeout(() => {
      this.pendingBlankPick = null;
      if (!this.isDisposed && this.pickingEnabled && this.appliedViewRevision === revision) {
        this.options.onPick?.(null, null, event, []);
      }
    }, 500);
  }

  #updateCamera() {
    const state = this.cameraState;
    const sinPhi = Math.sin(state.phi);
    this.camera.matrixAutoUpdate = true;
    this.camera.position.set(
      state.target.x + state.radius * sinPhi * Math.cos(state.theta),
      state.target.y + state.radius * sinPhi * Math.sin(state.theta),
      state.target.z + state.radius * Math.cos(state.phi),
    );
    // Free orbit follows the camera's meridian through the poles. A fixed Z-up
    // vector becomes parallel to the viewing direction at top/bottom and flips.
    if (this.options.constrainOrbit === false) {
      this.camera.up.set(
        -Math.cos(state.phi) * Math.cos(state.theta),
        -Math.cos(state.phi) * Math.sin(state.theta),
        sinPhi,
      );
    } else {
      this.camera.up.set(0, 0, 1);
    }
    this.camera.lookAt(state.target);
    this.camera.updateMatrixWorld(true);
    this.#updateProjectionMatrix();
  }

  #visibleHeightAtTarget() {
    return 2 * this.cameraState.radius
      * Math.tan(THREE.MathUtils.degToRad(this.perspectiveFov * 0.5));
  }

  #updateProjectionMatrix() {
    if (this.camera.isPerspectiveCamera) {
      this.camera.fov = this.perspectiveFov;
      this.camera.aspect = this.viewportAspect;
    } else if (this.camera.isOrthographicCamera) {
      const halfHeight = Math.max(0.000001, this.#visibleHeightAtTarget() * 0.5);
      const halfWidth = halfHeight * Math.max(0.000001, this.viewportAspect);
      this.camera.left = -halfWidth;
      this.camera.right = halfWidth;
      this.camera.top = halfHeight;
      this.camera.bottom = -halfHeight;
      this.camera.zoom = 1;
    }
    this.camera.updateProjectionMatrix();
  }

  #applyLocalNavigation(mode, dx, dy, event) {
    if(!this.cameraNavigationEnabled)return;
    const fast = event.shiftKey ? 2.5 : 1;
    if (mode === "orbit") {
      if(this.navigation.axisSnap){
        const filtered=this.navigation.axisSnap.move(dx,dy);
        dx=filtered.dx;dy=filtered.dy;this.root.dataset.orbitAxis=filtered.axis??'free';
        if(!dx&&!dy)return;
      }
      const speed = Number(this.options.orbitSpeed ?? 0.005) * fast;
      const next=orbitCameraState(this.getCameraState(),{theta:this.cameraState.theta-dx*speed,phi:this.#resolveOrbitPhi(this.cameraState.phi-dy*speed)},this.navigation.orbitPivot?.point,this.options.constrainOrbit!==false);
      this.cameraState.theta=next.theta;this.cameraState.phi=next.phi;
      this.cameraState.target.set(next.target.x,next.target.y,next.target.z);
    } else if (mode === "pan") {
      const canvasHeight = Math.max(1, this.renderer.domElement.clientHeight);
      const worldPerPixel = this.#visibleHeightAtTarget() / canvasHeight;
      const speed = Number(this.options.panSpeed ?? 1) * fast * worldPerPixel;
      const right = new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion);
      const up = new THREE.Vector3(0, 1, 0).applyQuaternion(this.camera.quaternion);
      this.cameraState.target
        .addScaledVector(right, -dx * speed)
        .addScaledVector(up, dy * speed);
    }
    this.#updateCamera();
    this.#renderOnce();
    this.#emitCameraChange(mode);
  }

  #clampCameraRadius(radius) {
    const minimum = Math.max(0.000001, Number(this.options.minCameraDistance ?? 0.01));
    const maximum = Math.max(minimum, Number(this.options.maxCameraDistance ?? 1000000000));
    return THREE.MathUtils.clamp(Number(radius), minimum, maximum);
  }

  #resolveOrbitPhi(phi) {
    const angle = Number(phi);
    if (this.options.constrainOrbit === false) {
      return angle;
    }
    return THREE.MathUtils.clamp(angle, 0.001, Math.PI - 0.001);
  }

  #emitCameraChange(reason) {
    this.#cancelBlankPick();
    if (typeof this.options.onCameraChange === "function") {
      this.options.onCameraChange(this.getCameraState(), { reason });
    }
  }

  #resetNavigation() {
    const pointerId = this.navigation.pointerId;
    this.navigation.pointerId = null;
    this.navigation.mode = null;
    this.navigation.axisSnap = null;
    this.navigation.orbitPivot=null;
    delete this.root.dataset.orbitAxis;
    this.navigation.lastX = 0;
    this.navigation.lastY = 0;
    this.navigation.moved = false;
    this.navigation.pickAllowed = false;
    this.rectangleSelectionKey = null;
    this.selectionRectangle?.remove();
    this.selectionRectangle = null;
    if (pointerId != null && this.renderer.domElement.hasPointerCapture?.(pointerId)) {
      this.renderer.domElement.releasePointerCapture(pointerId);
    }
  }

  #updateSelectionRectangle(event) {
    const bounds = this.renderer.domElement.getBoundingClientRect(), root = this.root.getBoundingClientRect();
    const x = Math.max(bounds.left, Math.min(bounds.right, event.clientX));
    const y = Math.max(bounds.top, Math.min(bounds.bottom, event.clientY));
    const startX = this.navigation.lastX, startY = this.navigation.lastY;
    const rectangle = { left: Math.min(startX,x), right: Math.max(startX,x),
      top: Math.min(startY,y), bottom: Math.max(startY,y), mode: x >= startX ? 'contain' : 'crossing' };
    if (!this.selectionRectangle) {
      this.selectionRectangle = document.createElement('div');
      this.selectionRectangle.className = 'icax-three-selection-rectangle';
      this.selectionRectangle.setAttribute('aria-hidden','true');
      Object.assign(this.selectionRectangle.style, { position:'absolute', pointerEvents:'none', zIndex:'20', boxSizing:'border-box' });
      this.root.appendChild(this.selectionRectangle);
    }
    this.selectionRectangle.dataset.mode = rectangle.mode;
    Object.assign(this.selectionRectangle.style, {
      left:`${rectangle.left-root.left}px`, top:`${rectangle.top-root.top}px`,
      width:`${rectangle.right-rectangle.left}px`, height:`${rectangle.bottom-rectangle.top}px`,
      border:rectangle.mode==='contain'?'1px solid #3484dc':'1px dashed #15947f',
      background:rectangle.mode==='contain'?'rgba(52,132,220,.12)':'rgba(21,148,127,.12)',
    });
    return rectangle;
  }

  #startRenderLoop() {
    if (this.animationFrame || this.isDisposed || !this.continuousRendering) {
      return;
    }
    const tick = () => {
      this.animationFrame = 0;
      if (!this.isDisposed && this.continuousRendering) {
        this.#renderOnce();
        this.animationFrame = requestAnimationFrame(tick);
      }
    };
    this.animationFrame = requestAnimationFrame(tick);
  }

  #renderOnce() {
    this.#updateDepthRange();
    if (this.worldAxes) {
      const length = this.options.worldAxisLength ?? 120;
      const pixels = this.options.worldAxisPixelSize ?? SELECTION_AXIS_PIXEL_SIZE * 1.6;
      this.worldAxes.scale.setScalar(this.#selectionAxisWorldScale(new THREE.Vector3()) * pixels / SELECTION_AXIS_PIXEL_SIZE / length);
    }
    this.transformManipulator?.update();
    this.#updateSelectionHelper();
    this.#updateDimensionLabelScales();
    this.#updateOutsideDimensionPositions();
    this.#updateSpecificationLabelPositions();
    this.renderer.render(this.scene, this.camera);
    this.#renderOrientationGizmo();
    this.renderSequence += 1;
    return this.renderSequence;
  }

  #updateDepthRange() {
    // Use visible geometry in camera space. A fixed 0.1 .. 1e6 frustum loses
    // millimetres of depth precision on CAD assemblies even at normal zoom.
    this.scene.updateMatrixWorld(true);
    this.camera.updateMatrixWorld(true);
    const box = new THREE.Box3(), transform = new THREE.Matrix4(), point = new THREE.Vector3();
    let nearest = Infinity, farthest = -Infinity;
    this.scene.traverseVisible(object => {
      if (!object.isMesh || !object.geometry?.attributes?.position || !object.layers.test(this.camera.layers)) return;
      const geometry = object.geometry;
      const position = geometry.attributes.position, count = geometry.index?.count ?? position.count;
      const first = Math.max(0, geometry.drawRange.start), last = Math.min(count, geometry.drawRange.start + geometry.drawRange.count);
      // Match the renderer's material and draw-group visibility. Invisible
      // picking helpers still have geometry, sometimes much larger than the
      // model; including them would discard thin-sheet depth precision.
      const ranges = Array.isArray(object.material)
        ? geometry.groups.filter(group => object.material[group.materialIndex]?.visible === true)
          .map(group => [Math.max(first, group.start), Math.min(last, group.start + group.count)])
        : object.material?.visible === true ? [[first, last]] : [];
      const drawn = ranges.filter(([start, end]) => end > start).sort((a, b) => a[0] - b[0]);
      if (!drawn.length) return;
      let covered = 0;
      for (const [start, end] of drawn) { if (start > covered) break; covered = Math.max(covered, end); }
      if (covered >= count) {
        if (!geometry.boundingBox) geometry.computeBoundingBox();
        if (!geometry.boundingBox || geometry.boundingBox.isEmpty()) return;
        box.copy(geometry.boundingBox);
      } else {
        let cached = this.renderedDepthBounds.get(geometry);
        if (!cached || cached.position !== position || cached.index !== geometry.index
          || cached.positionVersion !== position.version || cached.indexVersion !== geometry.index?.version) {
          cached = {position, index: geometry.index, positionVersion: position.version,
            indexVersion: geometry.index?.version, ranges: new Map()};
          this.renderedDepthBounds.set(geometry, cached);
        }
        const key = JSON.stringify(drawn);
        if (!cached.ranges.has(key)) {
          box.makeEmpty();
          for (const [start, end] of drawn) for (let i = start; i < end; ++i) {
            point.fromBufferAttribute(position, geometry.index ? geometry.index.getX(i) : i);
            box.expandByPoint(point);
          }
          if (cached.ranges.size >= 8) cached.ranges.clear();
          cached.ranges.set(key, box.clone());
        } else box.copy(cached.ranges.get(key));
        if (box.isEmpty()) return;
      }
      transform.multiplyMatrices(this.camera.matrixWorldInverse, object.matrixWorld);
      box.applyMatrix4(transform);
      nearest = Math.min(nearest, -box.max.z);
      farthest = Math.max(farthest, -box.min.z);
    });
    if (!Number.isFinite(nearest) || farthest <= 0) return;
    const near = Math.max(0.001, nearest > 0 ? nearest * .5 : 0.001);
    const far = Math.max(near * 2, farthest + Math.max(1, (farthest - nearest) * .1));
    if (this.camera.near !== near || this.camera.far !== far) {
      this.camera.near = near;
      this.camera.far = far;
      this.camera.updateProjectionMatrix();
    }
  }

  #updateDimensionLabelScales() {
    const canvasHeight = Math.max(1, this.renderer.domElement.clientHeight);
    const worldPerPixel = this.#visibleHeightAtTarget() / canvasHeight;
    const labelHeight = Math.max(0.001, worldPerPixel * 26);
    for (const object of this.dimensionContent.children) {
      if (!object.userData?.dimensionLabel) continue;
      const aspect = Math.max(1, Number(object.userData.dimensionLabelAspect ?? 1));
      object.scale.set(labelHeight * aspect, labelHeight, 1);
    }
  }

  #updateOutsideDimensionPositions() {
    if (!this.outsideDimensionLabels.length) return;
    const canvas = this.renderer.domElement;
    const width = Math.max(1, canvas.clientWidth);
    const height = Math.max(1, canvas.clientHeight);
    const worldPerPixel = Math.max(0.001, this.#visibleHeightAtTarget() / height);
    this.camera.updateMatrixWorld(true);
    const project = (point) => {
      const projected = point.clone().project(this.camera);
      return { x: (projected.x * 0.5 + 0.5) * width,
        y: (-projected.y * 0.5 + 0.5) * height, z: projected.z };
    };
    const projected = this.outsideDimensionLabels.map((entry) => ({
      ...entry, startScreen: project(entry.start), endScreen: project(entry.end),
    }));
    const visible = projected.filter(({ startScreen, endScreen }) =>
      [startScreen, endScreen].every((point) => Number.isFinite(point.x)
        && Number.isFinite(point.y) && Number.isFinite(point.z)
        && point.z >= -1 && point.z <= 1)
      && [startScreen, endScreen].some((point) => point.x >= -width * 0.12
        && point.x <= width * 1.12 && point.y >= -height * 0.12
        && point.y <= height * 1.12));
    // Screen-space geometry bounds keep captions clear of the tubes even when
    // their world-space offset projects straight toward the camera.
    const geometryRects = this.content.children.filter((object) => object.visible)
      .flatMap((object) => {
        const bounds = new THREE.Box3().setFromObject(object);
        if (bounds.isEmpty()) return [];
        const corners = [];
        for (const x of [bounds.min.x, bounds.max.x]) {
          for (const y of [bounds.min.y, bounds.max.y]) {
            for (const z of [bounds.min.z, bounds.max.z]) {
              corners.push(project(new THREE.Vector3(x, y, z)));
            }
          }
        }
        if (!corners.every((point) => Number.isFinite(point.x) && Number.isFinite(point.y))) return [];
        return [{ left: Math.min(...corners.map((point) => point.x)),
          right: Math.max(...corners.map((point) => point.x)),
          top: Math.min(...corners.map((point) => point.y)),
          bottom: Math.max(...corners.map((point) => point.y)) }];
      });
    const center = visible.length ? visible.reduce((sum, entry) => ({
      x: sum.x + (entry.startScreen.x + entry.endScreen.x) / (2 * visible.length),
      y: sum.y + (entry.startScreen.y + entry.endScreen.y) / (2 * visible.length),
    }), { x: 0, y: 0 }) : { x: width / 2, y: height / 2 };
    const occupied = [];
    const overlaps = (a, b) => a.left < b.right + 8 && a.right + 8 > b.left
      && a.top < b.bottom + 8 && a.bottom + 8 > b.top;
    const inside = (rect) => rect.left >= 8 && rect.top >= 8
      && rect.right <= width - 8 && rect.bottom <= height - 8;
    for (const [index, entry] of projected.entries()) {
      const { element, leader, startScreen: a, endScreen: b } = entry;
      if (!visible.includes(entry)) {
        element.hidden = true;
        leader.style.display = "none";
        continue;
      }
      element.hidden = false;
      leader.style.display = "";
      const labelWidth = element.offsetWidth;
      const labelHeight = element.offsetHeight;
      const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
      const length = Math.hypot(b.x - a.x, b.y - a.y);
      const radial = { x: mid.x - center.x, y: mid.y - center.y };
      const tangent = length > 5
        ? { x: (b.x - a.x) / length, y: (b.y - a.y) / length }
        : Math.hypot(radial.x, radial.y) > 5
          ? { x: -radial.y / Math.hypot(radial.x, radial.y),
            y: radial.x / Math.hypot(radial.x, radial.y) }
          : { x: 1, y: 0 };
      const normal = { x: -tangent.y, y: tangent.x };
      // Prefer the side facing away from the combined assembly. Use the
      // template's offset only when the part lies near the assembly center.
      let outward = radial.x * normal.x + radial.y * normal.y;
      if (Math.abs(outward) < 8) {
        const hint = project(entry.start.clone().lerp(entry.end, 0.5).add(entry.offset));
        outward = (hint.x - mid.x) * normal.x + (hint.y - mid.y) * normal.y;
      }
      const preferredSign = Math.abs(outward) >= 3 ? Math.sign(outward) : index % 2 ? 1 : -1;
      const railGap = Math.max(40, Math.min(125, entry.offset.length() / worldPerPixel));
      const extent = Math.abs(normal.x) * labelWidth / 2 + Math.abs(normal.y) * labelHeight / 2;
      let chosen = null;
      for (const sign of [preferredSign, -preferredSign]) {
        if (chosen) break;
        for (const extra of [0, 42, 84, 126]) {
          if (chosen) break;
          for (const shift of [0, -46, 46, -92, 92, -138, 138]) {
            const x = mid.x + normal.x * sign * (railGap + 14 + extent + extra)
              + tangent.x * shift;
            const y = mid.y + normal.y * sign * (railGap + 14 + extent + extra)
              + tangent.y * shift;
            const rect = { left: x - labelWidth / 2, right: x + labelWidth / 2,
              top: y - labelHeight / 2, bottom: y + labelHeight / 2 };
            if (inside(rect) && occupied.every((other) => !overlaps(rect, other))
                && geometryRects.every((other) => !overlaps(rect, other))) {
              chosen = { x, y, rect, sign, railGap: railGap + extra };
              break;
            }
          }
        }
      }
      if (!chosen) {
        const sign = preferredSign;
        const x = Math.max(labelWidth / 2 + 8, Math.min(width - labelWidth / 2 - 8,
          mid.x + normal.x * sign * (railGap + 14 + extent)));
        const y = Math.max(labelHeight / 2 + 8, Math.min(height - labelHeight / 2 - 8,
          mid.y + normal.y * sign * (railGap + 14 + extent)));
        chosen = { x, y, sign, railGap,
          rect: { left: x - labelWidth / 2, right: x + labelWidth / 2,
            top: y - labelHeight / 2, bottom: y + labelHeight / 2 } };
      }
      occupied.push(chosen.rect);
      element.style.left = `${chosen.x}px`;
      element.style.top = `${chosen.y}px`;
      const nx = normal.x * chosen.sign, ny = normal.y * chosen.sign;
      const railA = { x: a.x + nx * chosen.railGap, y: a.y + ny * chosen.railGap };
      const railB = { x: b.x + nx * chosen.railGap, y: b.y + ny * chosen.railGap };
      const railMid = { x: (railA.x + railB.x) / 2, y: (railA.y + railB.y) / 2 };
      leader.setAttribute("d", `M ${a.x} ${a.y} L ${railA.x} ${railA.y}`
        + ` M ${b.x} ${b.y} L ${railB.x} ${railB.y}`
        + ` M ${railA.x} ${railA.y} L ${railB.x} ${railB.y}`
        + ` M ${railA.x - nx * 4} ${railA.y - ny * 4} L ${railA.x + nx * 4} ${railA.y + ny * 4}`
        + ` M ${railB.x - nx * 4} ${railB.y - ny * 4} L ${railB.x + nx * 4} ${railB.y + ny * 4}`
        + ` M ${railMid.x} ${railMid.y} L ${chosen.x} ${chosen.y}`);
    }
  }

  #updateSpecificationLabelPositions() {
    if (!this.specificationLabels.length) return;
    const canvas = this.renderer.domElement;
    const width = Math.max(1, canvas.clientWidth);
    const height = Math.max(1, canvas.clientHeight);
    this.camera.updateMatrixWorld(true);
    this.specificationLeaderSvg.replaceChildren();
    this.specificationLeaderSvg.setAttribute("viewBox", `0 0 ${width} ${height}`);
    const occupied = [];
    const callouts = [];
    const visibleEntries = [];
    const canvasBounds = canvas.getBoundingClientRect();
    // A canvas may itself fit while a scrolling/clipping ancestor hides part
    // of it. Keep editable captions inside the actual visible intersection.
    let visibleLeft = Math.max(0, canvasBounds.left), visibleTop = Math.max(0, canvasBounds.top);
    let visibleRight = Math.min(innerWidth, canvasBounds.right), visibleBottom = Math.min(innerHeight, canvasBounds.bottom);
    for (let ancestor = canvas.parentElement; ancestor; ancestor = ancestor.parentElement) {
      const style = getComputedStyle(ancestor), box = ancestor.getBoundingClientRect();
      if (/hidden|clip|scroll|auto/.test(style.overflowX)) {
        visibleLeft = Math.max(visibleLeft, box.left + ancestor.clientLeft);
        visibleRight = Math.min(visibleRight, box.left + ancestor.clientLeft + ancestor.clientWidth);
      }
      if (/hidden|clip|scroll|auto/.test(style.overflowY)) {
        visibleTop = Math.max(visibleTop, box.top + ancestor.clientTop);
        visibleBottom = Math.min(visibleBottom, box.top + ancestor.clientTop + ancestor.clientHeight);
      }
    }
    const visibleBounds = {x:visibleLeft-canvasBounds.left,y:visibleTop-canvasBounds.top,
      w:Math.max(0,visibleRight-visibleLeft),h:Math.max(0,visibleBottom-visibleTop)};
    const overlayHost = this.root.closest(".cam-viewport") ?? this.root;
    for (const control of overlayHost.querySelectorAll(".cam-viewcube, .icax-three-axis-gizmo, .icax-three-projection-toggle, .icax-three-transform-space, .icax-three-display-mode, .tube-designer-scene-specification-tree, [data-tube-designer-runtime-status]:not([hidden])")) {
      const box = control.getBoundingClientRect();
      if (box.width && box.height) occupied.push({ x: box.left - canvasBounds.left,
        y: box.top - canvasBounds.top, w: box.width, h: box.height });
    }
    const reserved = [...occupied];
    for (const label of this.specificationLabels) {
      const projected = label.worldPoint.clone().project(this.camera);
      const visible = Number.isFinite(projected.x) && Number.isFinite(projected.y)
        && Number.isFinite(projected.z) && projected.z >= -1 && projected.z <= 1
        && projected.x >= -1.12 && projected.x <= 1.12
        && projected.y >= -1.12 && projected.y <= 1.12;
      label.element.hidden = !visible;
      if (!visible) continue;
      // Start at the template's world-space anchor. Crowded or clipped captions
      // move below with a leader while the actual dimension line stays exact.
      label.element.style.left = `${(projected.x * 0.5 + 0.5) * width}px`;
      label.element.style.top = `${(-projected.y * 0.5 + 0.5) * height}px`;
      const box = label.element.getBoundingClientRect();
      const x = (projected.x * 0.5 + 0.5) * width, y = (-projected.y * 0.5 + 0.5) * height;
      const caption = {x:x-box.width/2,y:y-box.height/2,w:box.width,h:box.height};
      visibleEntries.push({label,x:x+(label.screenOffset?.[0]??0),y:y+(label.screenOffset?.[1]??0),w:box.width,h:box.height});
      const clipped = caption.x < visibleBounds.x + 8 || caption.y < visibleBounds.y + 8
        || caption.x + caption.w > visibleBounds.x + visibleBounds.w - 8
        || caption.y + caption.h > visibleBounds.y + visibleBounds.h - 8;
      const crowded = this.options.avoidSpecificationOverlap === true && (clipped || occupied.some(r =>
        caption.x < r.x+r.w+6 && caption.x+caption.w+6 > r.x
        && caption.y < r.y+r.h+6 && caption.y+caption.h+6 > r.y));
      if (label.parameterCallout || crowded) callouts.push({label,x:x+(label.screenOffset?.[0]??0),y:y+(label.screenOffset?.[1]??0),w:box.width,h:box.height});
      else occupied.push(caption);
    }
    if (this.options.avoidSpecificationOverlap === true) {
      const positions = layoutSpecificationCallouts(visibleEntries, reserved, width, height, visibleBounds);
      callouts.length=0;
      visibleEntries.forEach((entry,i)=>callouts.push({...entry,placed:positions[i]}));
    }
    // Retain world anchors and exact dimension lines; relocate only captions.
    for (const entry of callouts) {
      const {label,w,h}=entry;
      const position = entry.placed ?? placeSpecificationCallout(entry, occupied, width, height, visibleBounds);
      occupied.push({...position,w,h});
      label.element.style.left=`${position.x+w/2}px`;
      label.element.style.top=`${position.y+h/2}px`;
      const anchor=(label.parameterCallout ? label.anchorPoint : label.worldPoint).clone().project(this.camera);
      const line=document.createElementNS("http://www.w3.org/2000/svg", "line");
      line.setAttribute("x1",String((anchor.x*0.5+0.5)*width));line.setAttribute("y1",String((-anchor.y*0.5+0.5)*height));
      line.setAttribute("x2",String(position.x+w/2));line.setAttribute("y2",String(position.y+h/2));
      line.setAttribute("stroke",label.color);line.setAttribute("stroke-width","1");
      this.specificationLeaderSvg.appendChild(line);
    }
  }

  #renderOrientationGizmo() {
    if (!this.axisRenderer || !this.axisScene || !this.axisCamera) {
      return;
    }
    const direction = new THREE.Vector3();
    this.camera.getWorldDirection(direction);
    if (direction.lengthSq() <= Number.EPSILON) {
      direction.set(-1, 1, -0.65);
    }
    direction.normalize();
    const up = new THREE.Vector3(0, 1, 0).applyQuaternion(this.camera.quaternion).normalize();
    this.axisCamera.position.copy(direction).multiplyScalar(-4.2);
    this.axisCamera.up.copy(up.lengthSq() > Number.EPSILON ? up : new THREE.Vector3(0, 0, 1));
    this.axisCamera.lookAt(0, 0, 0);
    this.axisCamera.updateMatrixWorld(true);
    this.axisRenderer.render(this.axisScene, this.axisCamera);
  }

  #sampleCanvasPixels() {
    const canvas = this.renderer.domElement;
    const width = canvas.width;
    const height = canvas.height;
    if (!width || !height) {
      return { sampled: 0, nonBackground: 0 };
    }

    const context = this.renderer.getContext();
    const pixels = new Uint8Array(4);
    const samplePoints = [];
    const grid = 17;
    for (let yIndex = 1; yIndex < grid; yIndex += 1) {
      for (let xIndex = 1; xIndex < grid; xIndex += 1) {
        samplePoints.push([xIndex / grid, yIndex / grid]);
      }
    }
    let nonBackground = 0;
    let firstNonBackground = null;
    for (const [xFactor, yFactor] of samplePoints) {
      const x = Math.min(width - 1, Math.max(0, Math.floor(width * xFactor)));
      const y = Math.min(height - 1, Math.max(0, Math.floor(height * yFactor)));
      context.readPixels(x, y, 1, 1, context.RGBA, context.UNSIGNED_BYTE, pixels);
      if (pixels[0] !== 24 || pixels[1] !== 33 || pixels[2] !== 40) {
        nonBackground += 1;
        firstNonBackground ??= { x, y, rgba: [pixels[0], pixels[1], pixels[2], pixels[3]] };
      }
    }
    return { sampled: samplePoints.length, nonBackground, firstNonBackground };
  }

  #setStatus(message) {
    this.status.textContent = message ?? "";
    this.status.hidden = !message;
  }

  #emitDiagnosticOnce(key, message, level = "info") {
    if (this.diagnosticKeys.has(key)) {
      return;
    }
    this.diagnosticKeys.add(key);
    this.#emitDiagnostic(message, level);
  }

  #emitDiagnostic(message, level = "info") {
    if (typeof this.options.onDiagnostic === "function") {
      this.options.onDiagnostic({ level, message });
    }
  }

  #unsubscribe() {
    for (const unsubscribe of this.subscriptions.splice(0)) {
      unsubscribe();
    }
  }
}

const IDENTITY_MATRIX = Object.freeze([
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1,
]);

function identityMatrixArray() {
  return IDENTITY_MATRIX;
}

function toThreeMatrixArray(value) {
  if (!Array.isArray(value) && !ArrayBuffer.isView(value)) {
    return IDENTITY_MATRIX;
  }
  if (value.length !== 16) {
    return IDENTITY_MATRIX;
  }
  const result = new Array(16);
  for (let row = 0; row < 4; row += 1) {
    for (let column = 0; column < 4; column += 1) {
      result[column * 4 + row] = Number(value[row * 4 + column]);
    }
  }
  return result;
}

function makeVertexColorArray(values) {
  const colors = new Float32Array(values.length * 3);
  for (let index = 0; index < values.length; index += 1) {
    const rgba = Number(values[index]) >>> 0;
    colors[index * 3] = ((rgba >>> 24) & 0xff) / 255;
    colors[index * 3 + 1] = ((rgba >>> 16) & 0xff) / 255;
    colors[index * 3 + 2] = ((rgba >>> 8) & 0xff) / 255;
  }
  return colors;
}

function makeCirclePoint(radius, offset, axis, angle) {
  const u = Math.cos(angle) * radius;
  const v = Math.sin(angle) * radius;
  if (axis === "x") {
    return new THREE.Vector3(offset, u, v);
  }
  if (axis === "y") {
    return new THREE.Vector3(u, offset, v);
  }
  return new THREE.Vector3(u, v, offset);
}

function disposeObject3D(object) {
  object?.traverse?.((child) => {
    child.geometry?.dispose?.();
    if (Array.isArray(child.material)) {
      for (const material of child.material) {
        material?.dispose?.();
      }
    } else {
      child.material?.dispose?.();
    }
  });
}

function normalizeEntityIds(entityIds) {
  const values = entityIds instanceof Set
    ? [...entityIds]
    : (Array.isArray(entityIds) ? entityIds : [entityIds]);
  return values
    .map((id) => String(id ?? "").trim())
    .filter(Boolean);
}

function toFiniteVector3(value) {
  const source = Array.isArray(value)
    ? value
    : [value?.x, value?.y, value?.z];
  if (source.length !== 3 || !source.every((entry) => Number.isFinite(Number(entry)))) {
    return null;
  }
  return new THREE.Vector3(Number(source[0]), Number(source[1]), Number(source[2]));
}

function setsEqual(left, right) {
  if (left === right) {
    return true;
  }
  if (!left || !right || left.size !== right.size) {
    return false;
  }
  for (const value of left) {
    if (!right.has(value)) {
      return false;
    }
  }
  return true;
}

function formatNumber(value, digits = 2) {
  const number = Number(value);
  return Number.isFinite(number) ? number.toFixed(digits) : "0.00";
}

function delay(durationMs) {
  return new Promise((resolve) => window.setTimeout(resolve, durationMs));
}

function normalizeProjectionMode(value) {
  return String(value ?? "perspective").trim().toLowerCase() === "orthographic"
    ? "orthographic"
    : "perspective";
}

function ensureThreeViewportStyles() {
  if (document.getElementById("icax-three-viewport-style")) {
    return;
  }
  const style = document.createElement("style");
  style.id = "icax-three-viewport-style";
  style.textContent = `
    .icax-three-viewport {
      position: relative;
      width: 100%;
      height: 100%;
      min-height: 0;
      overflow: hidden;
      background: #182128;
    }

    .icax-three-viewport-canvas {
      display: block;
      width: 100%;
      height: 100%;
      outline: none;
      touch-action: none;
    }

    .icax-three-viewport-status {
      position: absolute;
      left: 12px;
      top: 12px;
      z-index: 1;
      padding: 6px 8px;
      border: 1px solid rgba(255, 255, 255, 0.14);
      border-radius: 5px;
      background: rgba(12, 18, 23, 0.72);
      color: #dce7ed;
      font-size: 12px;
      pointer-events: none;
    }

    .icax-three-dimension-label-layer {
      position: absolute;
      inset: 0;
      z-index: 5;
      overflow: hidden;
      pointer-events: none;
    }

    .icax-three-dimension-leaders {
      position: absolute;
      inset: 0;
      width: 100%;
      height: 100%;
      overflow: hidden;
    }

    .icax-three-dimension-label {
      position: absolute;
      max-width: min(340px, calc(100% - 24px));
      padding: 4px 9px;
      border: 1px solid var(--icax-dimension-color);
      border-radius: 5px;
      background: rgba(14, 31, 38, 0.94);
      box-shadow: 0 3px 12px rgba(0, 0, 0, 0.28);
      color: #f7fbfc;
      font: 600 12px/17px system-ui, sans-serif;
      text-align: center;
      overflow-wrap: anywhere;
      transform: translate(-50%, -50%);
    }

    .icax-three-specification-label-layer {
      position: absolute;
      inset: 0;
      z-index: 6;
      overflow: hidden;
      pointer-events: none;
    }

    .icax-three-specification-label {
      position: absolute;
      min-width: 0;
      width: max-content;
      max-width: 210px;
      min-height: 25px;
      padding: 3px 8px;
      border: 1px solid #35d88d;
      border-radius: 5px;
      background: rgba(9, 55, 39, 0.92);
      box-shadow: 0 3px 12px rgba(0, 0, 0, 0.28);
      color: #d9ffed;
      font: 600 12px/17px system-ui, sans-serif;
      white-space: normal;
      transform: translate(-50%, -50%);
      pointer-events: none;
    }

    .icax-three-specification-trigger {
      display: block;
      margin: -3px -8px;
      padding: 3px 8px;
      border: 0;
      background: transparent;
      color: inherit;
      cursor: pointer;
      font: inherit;
      white-space: inherit;
    }

    .icax-three-specification-label.is-editing {
      display: flex;
      align-items: center;
      gap: 6px;
      max-width: none;
      padding: 4px 5px 4px 8px;
      border-style: solid;
      border-color: #ff7770;
      background: rgba(142, 39, 36, 0.96);
      color: #ffffff;
      pointer-events: auto;
    }

    .icax-three-specification-caption {
      white-space: nowrap;
    }

    .icax-three-specification-input {
      width: 92px;
      height: 25px;
      padding: 2px 6px;
      border: 1px solid rgba(255, 255, 255, 0.65);
      border-radius: 3px;
      background: #ffffff;
      color: #1f343d;
      font: 600 12px/19px system-ui, sans-serif;
      outline: none;
    }

    .icax-three-specification-input:focus {
      border-color: #ffffff;
      box-shadow: 0 0 0 2px rgba(255, 119, 112, 0.45);
    }

    .icax-three-specification-label:hover,
    .icax-three-specification-label:focus-visible,
    .icax-three-specification-label.is-active {
      border-color: #8bffc2;
      background: rgba(10, 91, 58, 0.97);
      outline: none;
    }

    .icax-three-specification-label.is-pending {
      border-style: dashed;
      color: #ffffff;
      border-color: #ff7770;
      background: rgba(142, 39, 36, 0.96);
    }

    .icax-three-display-mode {
      position: absolute; right: 14px; bottom: 14px; z-index: 7;
      display: flex; align-items: center; gap: 10px; padding: 5px 8px;
      border: 1px solid #c8cdd3; border-radius: 4px;
      background: rgba(255,255,255,.94); color: #404954; font: 12px/1.4 sans-serif;
      pointer-events: auto;
    }
    .icax-three-display-mode[hidden] { display: none; }
    .icax-three-display-mode select {
      height: 27px; padding: 0 22px 0 6px; border: 0; background: transparent;
      color: inherit; font: inherit; cursor: pointer;
    }
    .icax-three-display-mode :focus-visible { outline: 2px solid #315ded; outline-offset: 2px; }

    .icax-three-projection-toggle {
      position: absolute;
      left: 50%;
      bottom: 14px;
      z-index: 7;
      display: flex;
      padding: 3px;
      border: 1px solid rgba(151, 183, 194, 0.38);
      border-radius: 7px;
      background: rgba(13, 29, 36, 0.88);
      box-shadow: 0 6px 18px rgba(0, 0, 0, 0.2);
      transform: translateX(-50%);
      backdrop-filter: blur(8px);
      pointer-events: auto;
      user-select: none;
    }

    .icax-three-projection-toggle[hidden] {
      display: none;
    }

    .icax-three-projection-toggle button {
      min-width: 46px;
      height: 27px;
      padding: 0 10px;
      border: 0;
      border-radius: 4px;
      background: transparent;
      color: #a9c0c8;
      cursor: pointer;
      font-size: 11px;
      line-height: 27px;
    }

    .icax-three-projection-toggle button:hover {
      color: #ffffff;
      background: rgba(255, 255, 255, 0.08);
    }

    .icax-three-projection-toggle button.active {
      background: #eef5f6;
      color: #1f4852;
      box-shadow: 0 1px 4px rgba(0, 0, 0, 0.2);
      font-weight: 600;
    }

    .icax-three-axis-gizmo {
      position: absolute;
      left: 14px;
      bottom: 14px;
      z-index: 4;
      width: 86px;
      height: 86px;
      pointer-events: none;
      background: transparent;
    }

    .icax-three-axis-canvas {
      display: block;
      width: 86px;
      height: 86px;
    }
  `;
  document.head.appendChild(style);
}
