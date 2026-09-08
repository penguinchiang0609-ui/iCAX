import * as THREE from "../ThirdParty/three/three.module.js";
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
    this.continuousRendering = options.continuousRender !== false;
    this.pickingEnabled = options.pickingEnabled !== false;
    this.host = null;
    this.sceneProxy = null;
    this.subscriptions = [];
    this.slotDescriptors = new Map();
    this.colliderSlotDescriptors = new Map();
    this.geometryPayloads = new Map();
    this.geometryObjects = new Map();
    this.materialPayloads = new Map();
    this.resourcePromises = new Map();
    this.viewApplyGeneration = 0;
    this.appliedViewRevision = "0";
    this.renderSequence = 0;
    this.instancePayloads = new Map();
    this.transformPayloads = new Map();
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
    this.ghostPreview = null;
    this.diagnosticKeys = new Set();
    this.domListeners = [];
    this.isDefragging = false;
    this.isDisposed = false;
    this.dynamicReadPending = false;
    this.navigation = {
      pointerId: null,
      mode: null,
      lastX: 0,
      lastY: 0,
      moved: false,
    };

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
    });
    const pixelRatioCap = Math.max(0.5, Number(options.pixelRatioCap ?? 2));
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, pixelRatioCap));
    this.renderer.domElement.className = "icax-three-viewport-canvas";
    this.renderer.domElement.tabIndex = 0;
    this.root.appendChild(this.renderer.domElement);

    this.#installProjectionToggle();
    this.#installOrientationGizmo();

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
    this.root.classList.toggle("picking-enabled", this.pickingEnabled);
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
    const references = new Map();
    for (const { data } of rows) {
      for (const reference of [data?.geometry, data?.material]) {
        const url = String(reference?.url ?? "").trim();
        if (url) {
          references.set(`${url}@${String(reference?.version ?? 0)}`, reference);
        }
      }
    }

    const resources = await Promise.all(
      [...references.values()].map((reference) =>
        this.#loadViewResource(resourceClient, reference)),
    );
    if (generation !== this.viewApplyGeneration || this.isDisposed) {
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
    this.#updateSelectionVisuals();
    this.#setStatus(rows.length && !desiredIds.size
      ? "View 中暂时没有可读取的几何资源"
      : "");
    this.appliedViewRevision = String(snapshot?.revision ?? "0");
    this.#renderOnce();
    return {
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
  }

  fitViewForRevision(revision, padding = 1.25) {
    const expectedRevision = String(revision ?? "0");
    if (expectedRevision === "0" || this.appliedViewRevision !== expectedRevision) {
      throw new Error(
        `Cannot fit View revision ${expectedRevision}; applied revision is ${this.appliedViewRevision}`,
      );
    }
    if (!this.fitView(padding)) {
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

  fitViewToViewport(padding = 1.18) {
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
      const start = toFiniteVector3(annotation?.start);
      const end = toFiniteVector3(annotation?.end);
      if (!start || !end || start.distanceToSquared(end) <= Number.EPSILON) continue;
      const offset = toFiniteVector3(annotation?.offset) ?? new THREE.Vector3();
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

      const label = String(annotation?.label ?? "").trim();
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

  setSelectedObjectId(objectId) {
    return this.setSelectedObjectIds(objectId ? [objectId] : [], objectId);
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

  setSelectedObjectIds(objectIds = [], primaryObjectId = "") {
    const nextIds = new Set((Array.isArray(objectIds) ? objectIds : [objectIds])
      .map((id) => String(id ?? "").trim())
      .filter(Boolean));
    const nextPrimaryId = String(primaryObjectId ?? "").trim() || [...nextIds][0] || "";
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
      ghostPreviewVisible: Boolean(this.ghostPreview?.visible),
      ghostPreviewNodeId: String(this.ghostPreview?.userData?.nodeId ?? ""),
      ghostPreviewVertexCount: Number(
        this.ghostPreview?.children?.[0]?.geometry?.getAttribute?.("position")?.count ?? 0,
      ),
      continuousRendering: this.continuousRendering,
      pickingEnabled: this.pickingEnabled,
      projectionMode: this.projectionMode,
      projectionToggleVisible: Boolean(this.projectionToggle && !this.projectionToggle.hidden),
      orbitConstrained: this.options.constrainOrbit !== false,
      animationFrameActive: Boolean(this.animationFrame),
      dimensionAnnotationCount: this.dimensionContent.children.filter(
        (object) => object.userData?.dimensionLabel,
      ).length,
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
    object.matrixAutoUpdate = false;
    this.#refreshObjectMaterial(object, instance);
    const transform = this.transformPayloads.get(instance.transformId);
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
    object.visible = Boolean(payload.flags & ColliderFlags.visible)
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
      geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array(payload.positions), 3));
      if (payload.normals?.length) {
        geometry.setAttribute("normal", new THREE.BufferAttribute(new Float32Array(payload.normals), 3));
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

  #makeMaterial(instance, hasVertexColors = false) {
    const materialResource = this.materialPayloads.get(instance.materialId);
    const color = rgbaToCssColor(
      materialResource?.colorRGBA
        ?? this.#defaultColorForClass(instance.renderClass),
    );
    if (instance.geometryKind === RenderGeometryKind.mesh) {
      const materialOptions = {
        color: new THREE.Color(color.r / 255, color.g / 255, color.b / 255),
        transparent: color.a < 1,
        opacity: Math.max(0.08, color.a),
        side: THREE.DoubleSide,
        vertexColors: Boolean(hasVertexColors),
      };
      return new THREE.MeshPhongMaterial(materialOptions);
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
      this.materialPayloads.get(instance.materialId)?.dataVersion ?? "",
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
    const wasSelected = this.#isObjectSelected(object.userData?.objectId);
    this.#setObjectSelectedVisual(object, false);
    const previousMaterial = object.material;
    object.material = this.#makeMaterial(instance, Boolean(object.userData?.hasVertexColors));
    previousMaterial?.dispose?.();
    object.userData.materialSignature = signature;
    if (wasSelected) {
      this.#setObjectSelectedVisual(object, true);
    }
  }

  #refreshSceneObjectMaterials() {
    for (const object of this.sceneObjects.values()) {
      const instance = object.userData?.instance;
      if (instance) {
        this.#refreshObjectMaterial(object, instance);
      }
    }
  }

  #defaultColorForClass(renderClass) {
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
      const transform = this.transformPayloads.get(transformId);
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
    const transform = this.transformPayloads.get(String(transformId));
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
    this.#setObjectSelectedVisual(object, false);
    object.parent?.remove(object);
    object.material?.dispose?.();
    this.sceneObjects.delete(normalizedId);
    this.selectedObjectIds.delete(normalizedId);
    if (this.selectedObjectId === normalizedId) {
      this.selectedObjectId = [...this.selectedObjectIds][0] || "";
    }
    this.#updateSelectionHelper();
  }

  #clearViewContent() {
    for (const object of this.sceneObjects.values()) {
      this.#setObjectSelectedVisual(object, false);
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
    this.cameras.clear();
    this.activeCameraId = null;
    this.appliedViewRevision = "0";
    this.selectionAxisHelper.visible = false;
  }

  #clearRenderContent() {
    this.clearGhostMesh();
    this.#clearViewContent();
    for (const object of this.colliderObjects.values()) {
      object.parent?.remove(object);
      disposeObject3D(object);
    }
    this.colliderObjects.clear();
    this.colliderPayloads.clear();
    this.colliderSlotDescriptors.clear();
    this.selectedObjectId = "";
    this.selectedObjectIds.clear();
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
    if (!objectId) {
      this.selectionAxisHelper.visible = false;
      return;
    }

    const transform = this.transformPayloads.get(objectId);
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
    this.selectionAxisHelper.scale.setScalar(this.#selectionAxisWorldScale(position));
    this.selectionAxisHelper.visible = true;
    this.selectionAxisHelper.updateMatrixWorld(true);
  }

  #isObjectSelected(objectId) {
    return this.selectedObjectIds.has(String(objectId ?? "").trim());
  }

  #setObjectSelectedVisual(object, isSelected) {
    if (!object?.material) {
      return;
    }

    const material = object.material;
    if (isSelected) {
      if (!object.userData.selectionMaterialState) {
        object.userData.selectionMaterialState = this.#captureSelectionMaterialState(material);
      }
      if (material.emissive) {
        material.emissive.setHex(0x5f4100);
        material.emissiveIntensity = 0.18;
      }
      if ("linewidth" in material) {
        material.linewidth = Math.max(2, Number(material.linewidth ?? 1));
      }
      material.needsUpdate = true;
      return;
    }

    this.#restoreSelectionMaterialState(material, object.userData.selectionMaterialState);
    object.userData.selectionMaterialState = null;
  }

  #captureSelectionMaterialState(material) {
    return {
      color: material.color?.clone?.() ?? null,
      emissive: material.emissive?.clone?.() ?? null,
      emissiveIntensity: material.emissiveIntensity,
      linewidth: material.linewidth,
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
    const grid = new THREE.GridHelper(800, 20, 0x38505d, 0x2a3a43);
    grid.rotation.x = Math.PI / 2;
    this.scene.add(grid);

    const axis = new THREE.AxesHelper(120);
    this.scene.add(axis);

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

  #installPointerControls() {
    const canvas = this.renderer.domElement;
    this.#listen(canvas, "pointerdown", (event) => {
      canvas.focus?.();
      const mode = event.button === 2
        ? "orbit"
        : (event.button === 1 ? "pan" : null);
      this.navigation.pointerId = event.pointerId;
      this.navigation.mode = mode;
      this.navigation.lastX = event.clientX;
      this.navigation.lastY = event.clientY;
      this.navigation.moved = false;
      if (mode) {
        event.preventDefault();
        canvas.setPointerCapture?.(event.pointerId);
      }
    });
    this.#listen(canvas, "pointerup", (event) => {
      const wasNavigation = this.navigation.pointerId === event.pointerId
        && Boolean(this.navigation.mode);
      const moved = this.navigation.moved;
      if (this.navigation.pointerId === event.pointerId) {
        this.#resetNavigation();
      }
      if (canvas.hasPointerCapture?.(event.pointerId)) {
        canvas.releasePointerCapture(event.pointerId);
      }
      if (!wasNavigation && !moved && event.button === 0) {
        this.#emitPick(event);
      }
    });
    this.#listen(canvas, "pointermove", (event) => {
      if (this.navigation.pointerId !== event.pointerId || !this.navigation.mode) {
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
      event.preventDefault();
      const speed = Number(this.options.zoomSpeed ?? 0.0015);
      this.cameraState.radius = this.#clampCameraRadius(
        this.cameraState.radius * Math.exp((event.deltaY || 0) * speed),
      );
      this.#updateCamera();
      this.#renderOnce();
      this.#emitCameraChange("zoom");
    }, { passive: false });
    this.#listen(canvas, "contextmenu", (event) => event.preventDefault());
    this.#listen(window, "blur", () => {
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

  #emitPick(event) {
    if (!this.pickingEnabled || typeof this.options.onPick !== "function") {
      return;
    }
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    this.pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const hits = this.raycaster.intersectObjects(
      [...this.sceneObjects.values()].filter((object) => object.visible),
      false,
    );
    this.options.onPick(hits[0]?.object?.userData ?? null, hits[0] ?? null, event, hits);
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
    const fast = event.shiftKey ? 2.5 : 1;
    if (mode === "orbit") {
      const speed = Number(this.options.orbitSpeed ?? 0.005) * fast;
      this.cameraState.theta -= dx * speed;
      this.cameraState.phi = this.#resolveOrbitPhi(this.cameraState.phi - dy * speed);
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
    if (typeof this.options.onCameraChange === "function") {
      this.options.onCameraChange(this.getCameraState(), { reason });
    }
  }

  #resetNavigation() {
    this.navigation.pointerId = null;
    this.navigation.mode = null;
    this.navigation.lastX = 0;
    this.navigation.lastY = 0;
    this.navigation.moved = false;
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
    this.#updateSelectionHelper();
    this.#updateDimensionLabelScales();
    this.renderer.render(this.scene, this.camera);
    this.#renderOrientationGizmo();
    this.renderSequence += 1;
    return this.renderSequence;
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
      min-height: 280px;
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
