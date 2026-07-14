import * as THREE from "../ThirdParty/three/three.module.js";
import {
  InputModifierFlags,
  InputStateFlags,
  mapKeyboardEventCode,
  writeInputStatePDO,
} from "../Input/inputPDO.mjs";
import {
  RenderFlags,
  RenderGeometryKind,
  RenderPDOCommands,
  RenderPDOLayout,
  parseRenderPDOEvent,
  parseRenderPDOPayload,
  rgbaToCssColor,
} from "./renderPDO.mjs";
import {
  ColliderFlags,
  ColliderPDOCommands,
  ColliderPDOLayout,
  ColliderShapeKind,
  parseColliderPDOEvent,
  parseColliderPDOPayload,
} from "./colliderPDO.mjs";

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

export function createThreeViewport(options = {}) {
  return new ThreeRenderViewport(options);
}

export class ThreeRenderViewport {
  constructor(options = {}) {
    ensureThreeViewportStyles();
    this.options = options;
    this.host = null;
    this.sceneProxy = null;
    this.subscriptions = [];
    this.slotDescriptors = new Map();
    this.colliderSlotDescriptors = new Map();
    this.geometryPayloads = new Map();
    this.geometryObjects = new Map();
    this.instancePayloads = new Map();
    this.transformPayloads = new Map();
    this.colliderPayloads = new Map();
    this.colliderObjects = new Map();
    this.objectSlots = new Map();
    this.sceneObjects = new Map();
    this.cameras = new Map();
    this.activeCameraId = null;
    this.selectedObjectId = "";
    this.selectedObjectIds = new Set();
    this.diagnosticKeys = new Set();
    this.domListeners = [];
    this.isDefragging = false;
    this.isDisposed = false;
    this.dynamicReadPending = false;
    this.input = {
      keys: new Set(),
      dataVersion: 1n,
      sequence: 1n,
      viewportId: BigInt(options.viewportId ?? 1),
      pointerX: 0,
      pointerY: 0,
      lastPointerX: 0,
      lastPointerY: 0,
      pointerDeltaX: 0,
      pointerDeltaY: 0,
      wheelX: 0,
      wheelY: 0,
      buttonMask: 0,
      modifierMask: 0,
      hasFocus: false,
      pointerInside: false,
      pointerCaptured: false,
      writePending: false,
      lastSentSignature: null,
      transientResetPending: false,
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
    this.content.name = "iCAX RenderPDO Content";
    this.scene.add(this.content);
    this.colliderContent = new THREE.Group();
    this.colliderContent.name = "iCAX ColliderPDO Content";
    this.scene.add(this.colliderContent);
    this.selectionAxisHelper = this.#createSelectionAxisHelper();
    this.scene.add(this.selectionAxisHelper);

    this.camera = new THREE.PerspectiveCamera(45, 1, 0.1, 1000000);
    this.camera.up.set(0, 0, 1);
    this.cameraState = {
      target: new THREE.Vector3(DEFAULT_CAMERA.target.x, DEFAULT_CAMERA.target.y, DEFAULT_CAMERA.target.z),
      radius: 470,
      theta: -0.88,
      phi: 1.08,
    };

    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.domElement.className = "icax-three-viewport-canvas";
    this.renderer.domElement.tabIndex = 0;
    this.root.appendChild(this.renderer.domElement);

    this.#installOrientationGizmo();

    this.raycaster = new THREE.Raycaster();
    this.pointer = new THREE.Vector2();

    this.#installSceneBasics();
    this.#installPointerControls();
    this.#updateCamera();
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
    this.#startRenderLoop();
    return this;
  }

  connectScene(sceneProxy) {
    if (this.sceneProxy === sceneProxy) {
      return this;
    }
    this.#unsubscribe();
    this.#resetInputState();
    this.sceneProxy = sceneProxy ?? null;
    this.slotDescriptors.clear();
    this.objectSlots.clear();
    this.dynamicReadPending = false;
    this.#clearRenderContent();

    if (!this.sceneProxy) {
      this.#setStatus("未绑定场景");
      return this;
    }
    if (!this.sceneProxy.pdo?.enabled) {
      this.#setStatus("当前 Scene 未启用 PDO");
      return this;
    }

    this.subscriptions.push(this.sceneProxy.subscribe(RenderPDOCommands.slotAllocated, (event) => this.#handleSlotEvent(event)));
    this.subscriptions.push(this.sceneProxy.subscribe(RenderPDOCommands.slotMoved, (event) => this.#handleSlotEvent(event)));
    this.subscriptions.push(this.sceneProxy.subscribe(RenderPDOCommands.slotFreed, (event) => this.#handleSlotEvent(event)));
    this.subscriptions.push(this.sceneProxy.subscribe(RenderPDOCommands.defragBegin, (event) => this.#handleDefragEvent(event, true)));
    this.subscriptions.push(this.sceneProxy.subscribe(RenderPDOCommands.defragEnd, (event) => this.#handleDefragEvent(event, false)));
    this.subscriptions.push(this.sceneProxy.subscribe(ColliderPDOCommands.slotAllocated, (event) => this.#handleColliderSlotEvent(event)));
    this.subscriptions.push(this.sceneProxy.subscribe(ColliderPDOCommands.slotMoved, (event) => this.#handleColliderSlotEvent(event)));
    this.subscriptions.push(this.sceneProxy.subscribe(ColliderPDOCommands.slotFreed, (event) => this.#handleColliderSlotEvent(event)));
    this.subscriptions.push(this.sceneProxy.subscribe(ColliderPDOCommands.defragBegin, (event) => this.#handleDefragEvent(event, true)));
    this.subscriptions.push(this.sceneProxy.subscribe(ColliderPDOCommands.defragEnd, (event) => this.#handleDefragEvent(event, false)));
    this.#setStatus("等待 RenderPDO slot");
    return this;
  }

  resize() {
    const rect = this.host?.getBoundingClientRect?.() ?? this.root.getBoundingClientRect();
    const width = Math.max(1, Math.floor(rect.width || this.root.clientWidth || 1));
    const height = Math.max(1, Math.floor(rect.height || this.root.clientHeight || 1));
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
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
    this.axisRenderer?.dispose?.();
    this.axisRenderer = null;
    this.renderer.dispose();
    this.root.remove();
  }

  async refreshAll() {
    for (const descriptor of this.slotDescriptors.values()) {
      await Promise.race([this.#readSlot(descriptor), delay(300)]);
    }
    for (const descriptor of this.colliderSlotDescriptors.values()) {
      await Promise.race([this.#readColliderSlot(descriptor), delay(300)]);
    }
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
      colliderSlotCount: this.colliderSlotDescriptors.size,
      slots: [...this.slotDescriptors.values()].map((descriptor) => ({
        pdoId: String(descriptor.pdoId ?? ""),
        payloadKind: String(descriptor.payloadKind ?? ""),
        slotRole: String(descriptor.slotRole ?? ""),
        geometryId: String(descriptor.geometryId ?? ""),
        objectId: String(descriptor.objectId ?? ""),
        transformId: String(descriptor.transformId ?? ""),
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
      selectedObjectId: this.selectedObjectId,
      selectedObjectIds: [...this.selectedObjectIds],
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

  async #handleSlotEvent(event) {
    let payload = null;
    try {
      payload = parseRenderPDOEvent(event);
    } catch (error) {
      this.#setStatus(`RenderPDO 事件解析失败: ${error.message}`);
      return;
    }
    if (!payload?.pdoId) {
      return;
    }

    if (payload.event === "SlotFreed") {
      this.#removeSlot(payload);
      return;
    }

    const descriptor = {
      pdoId: payload.pdoId,
      payloadKind: payload.payloadKind,
      slotRole: payload.slotRole,
      geometryId: payload.geometryId,
      objectId: payload.objectId,
      transformId: payload.transformId,
      payloadCapacity: Number(payload.payloadCapacity ?? 0),
      version: Number(payload.slotVersion ?? RenderPDOLayout.version),
      lastReadDataVersion: null,
    };
    this.slotDescriptors.set(descriptor.pdoId, descriptor);
    this.#emitDiagnosticOnce(
      `slot:${payload.event}:${descriptor.pdoId}`,
      `RenderPDO slot ${payload.event}: role=${descriptor.slotRole}, pdo=${descriptor.pdoId}`,
      "info",
    );
    await this.#readSlot(descriptor, { force: true });
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

  async #readSlot(descriptor, options = {}) {
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
          const payload = parseRenderPDOPayload(buffer);
          const dataVersion = String(payload.header?.dataVersion ?? "");
          if (!force && dataVersion && dataVersion === descriptor.lastReadDataVersion) {
            return;
          }
          this.#applyPayload(descriptor, payload);
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
        this.#setStatus("等待 RenderPDO 可读数据");
        if (!silentTransient) {
          this.#emitDiagnosticOnce(`transient-read:${descriptor.pdoId}:${message}`, `RenderPDO 等待可读数据：${message}`, "info");
        }
        return;
      }
      this.#setStatus(`RenderPDO 读取失败: ${message}`);
      this.#emitDiagnostic(`RenderPDO 读取失败：${message}`, "error");
    }
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
          this.#emitDiagnosticOnce(`transient-meta:${descriptor.pdoId}:${message}`, `RenderPDO 等待版本信息：${message}`, "info");
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

  #applyPayload(descriptor, payload) {
    if (payload.kind === "mesh" || payload.kind === "polyline" || payload.kind === "toolpath") {
      this.geometryPayloads.set(payload.geometryId, payload);
      this.geometryObjects.set(payload.geometryId, this.#makeGeometryObject(payload));
      this.#updateObjectsUsingGeometry(payload.geometryId);
      this.#upsertObjectsUsingGeometry(payload.geometryId);
      this.#emitDiagnosticOnce(
        `geometry:${payload.geometryId}:${payload.header?.dataVersion ?? ""}`,
        `RenderPDO ${payload.kind} 已读取：geometry=${payload.geometryId}`,
        "ok",
      );
      this.#setStatus("");
      this.#renderOnce();
      return;
    }
    if (payload.kind === "object") {
      this.#applyObject(descriptor, payload);
      this.#emitDiagnosticOnce(
        `instance:${descriptor.pdoId}:${payload.header?.dataVersion ?? ""}`,
        `RenderPDO 对象已读取：object=${payload.objectId}`,
        "ok",
      );
      this.#setStatus("");
      this.#renderOnce();
      return;
    }
    if (payload.kind === "transform") {
      this.#applyTransform(payload);
      this.#setStatus("");
      this.#renderOnce();
      return;
    }
    if (payload.kind === "camera") {
      this.#applyCamera(payload);
      this.#renderOnce();
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
    object.visible = Boolean(payload.flags & ColliderFlags.visible) && Boolean(payload.flags & ColliderFlags.enabled);
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
  }

  #upsertSceneObject(instance) {
    const geometryObject = this.geometryObjects.get(instance.geometryId);
    if (!geometryObject) {
      this.#emitDiagnosticOnce(
        `pending-instance:${instance.objectId}:${instance.geometryId}`,
        `RenderPDO 实例等待几何：object=${instance.objectId}, geometry=${instance.geometryId}`,
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

    object.visible = Boolean(instance.flags & RenderFlags.visible);
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
      materialId: instance.materialId,
      hasVertexColors,
    };
    return object;
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
        `RenderPDO Mesh 数据：geometry=${payload.geometryId}, vertices=${Math.floor((payload.positions?.length ?? 0) / 3)}, indices=${payload.indices?.length ?? 0}`,
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
    const color = rgbaToCssColor(this.#defaultColorForClass(instance.renderClass));
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
      linewidth: 1,
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

  #removeSlot(payload) {
    this.slotDescriptors.delete(payload.pdoId);
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

  #clearRenderContent() {
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
    this.instancePayloads.clear();
    for (const object of this.colliderObjects.values()) {
      object.parent?.remove(object);
      disposeObject3D(object);
    }
    this.colliderObjects.clear();
    this.colliderPayloads.clear();
    this.colliderSlotDescriptors.clear();
    this.transformPayloads.clear();
    this.cameras.clear();
    this.activeCameraId = null;
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
    this.camera.fov = THREE.MathUtils.radToDeg(camera.verticalFovRadians || 0.785398185);
    this.camera.matrixAutoUpdate = false;
    const transform = this.transformPayloads.get(camera.transformId);
    if (transform?.localToWorld) {
      this.camera.matrix.fromArray(transform.localToWorld);
      this.camera.matrix.multiply(ENGINE_CAMERA_TO_THREE_CAMERA_MATRIX);
      this.camera.matrix.decompose(this.camera.position, this.camera.quaternion, this.camera.scale);
      this.camera.updateMatrixWorld(true);
    }
    this.camera.updateProjectionMatrix();
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
    const fovRadians = THREE.MathUtils.degToRad(this.camera.fov || 45);
    const visibleHeight = 2 * Math.tan(fovRadians * 0.5) * distance;
    return Math.max(SELECTION_AXIS_MIN_SCALE, (visibleHeight / canvasHeight) * SELECTION_AXIS_PIXEL_SIZE);
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

  #installPointerControls() {
    const canvas = this.renderer.domElement;
    this.#listen(canvas, "pointerdown", (event) => {
      canvas.focus?.();
      this.input.hasFocus = true;
      this.input.pointerInside = true;
      this.input.pointerCaptured = true;
      this.#updatePointerFromEvent(event);
      this.input.buttonMask = event.buttons || buttonToMask(event.button);
      this.renderer.domElement.setPointerCapture?.(event.pointerId);
    });
    this.#listen(canvas, "pointerup", (event) => {
      this.#updatePointerFromEvent(event);
      this.input.buttonMask = event.buttons || 0;
      if (!this.input.buttonMask) {
        this.input.pointerCaptured = false;
      }
      this.renderer.domElement.releasePointerCapture?.(event.pointerId);
      this.#emitPick(event);
    });
    this.#listen(canvas, "pointermove", (event) => {
      this.#updatePointerFromEvent(event);
      this.input.buttonMask = event.buttons || this.input.buttonMask;
    });
    this.#listen(canvas, "pointerenter", (event) => {
      this.input.pointerInside = true;
      this.#updatePointerFromEvent(event, false);
    });
    this.#listen(canvas, "pointerleave", () => {
      this.input.pointerInside = false;
    });
    this.#listen(canvas, "focus", () => {
      this.input.hasFocus = true;
    });
    this.#listen(canvas, "blur", () => {
      this.input.hasFocus = false;
      this.input.keys.clear();
      this.input.buttonMask = 0;
      this.input.pointerCaptured = false;
    });
    this.#listen(canvas, "wheel", (event) => {
      event.preventDefault();
      this.#updatePointerFromEvent(event, false);
      this.input.wheelX += event.deltaX || 0;
      this.input.wheelY += event.deltaY || 0;
    }, { passive: false });
    this.#listen(canvas, "contextmenu", (event) => event.preventDefault());
    this.#listen(window, "keydown", (event) => this.#handleKeyboard(event, true));
    this.#listen(window, "keyup", (event) => this.#handleKeyboard(event, false));
    this.#listen(window, "blur", () => {
      this.input.keys.clear();
      this.input.buttonMask = 0;
      this.input.pointerCaptured = false;
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

  #handleKeyboard(event, isDown) {
    if (!this.#shouldCaptureKeyboard(event)) {
      return;
    }

    const keyCode = mapKeyboardEventCode(event);
    if (!keyCode) {
      return;
    }

    this.input.modifierMask = modifierMaskFromEvent(event);
    if (isDown) {
      this.input.keys.add(keyCode);
    } else {
      this.input.keys.delete(keyCode);
    }
    event.preventDefault();
  }

  #shouldCaptureKeyboard(event) {
    if (isEditableTarget(event.target)) {
      return false;
    }
    return this.input.hasFocus || this.input.pointerInside || this.input.pointerCaptured;
  }

  #updatePointerFromEvent(event, accumulateDelta = true) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    this.input.pointerX = Number.isFinite(x) ? x : this.input.pointerX;
    this.input.pointerY = Number.isFinite(y) ? y : this.input.pointerY;
    this.input.modifierMask = modifierMaskFromEvent(event);

    if (!accumulateDelta) {
      this.input.lastPointerX = this.input.pointerX;
      this.input.lastPointerY = this.input.pointerY;
      return;
    }

    const dx = Number.isFinite(event.movementX) ? event.movementX : this.input.pointerX - this.input.lastPointerX;
    const dy = Number.isFinite(event.movementY) ? event.movementY : this.input.pointerY - this.input.lastPointerY;
    this.input.pointerDeltaX += dx || 0;
    this.input.pointerDeltaY += dy || 0;
    this.input.lastPointerX = this.input.pointerX;
    this.input.lastPointerY = this.input.pointerY;
  }

  #emitPick(event) {
    if (typeof this.options.onPick !== "function") {
      return;
    }
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    this.pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const hits = this.raycaster.intersectObjects([...this.sceneObjects.values()], false);
    this.options.onPick(hits[0]?.object?.userData ?? null, hits[0] ?? null);
  }

  #updateCamera() {
    const state = this.cameraState;
    const sinPhi = Math.sin(state.phi);
    this.camera.position.set(
      state.target.x + state.radius * sinPhi * Math.cos(state.theta),
      state.target.y + state.radius * sinPhi * Math.sin(state.theta),
      state.target.z + state.radius * Math.cos(state.phi),
    );
    this.camera.lookAt(state.target);
    this.camera.updateMatrixWorld(true);
  }

  #startRenderLoop() {
    if (this.animationFrame || this.isDisposed) {
      return;
    }
    const tick = () => {
      this.animationFrame = 0;
      if (!this.isDisposed) {
        void this.#flushInputPDO();
        void this.#refreshDynamicRenderPDO();
        this.#renderOnce();
        this.animationFrame = requestAnimationFrame(tick);
      }
    };
    this.animationFrame = requestAnimationFrame(tick);
  }

  #renderOnce() {
    this.#updateSelectionHelper();
    this.renderer.render(this.scene, this.camera);
    this.#renderOrientationGizmo();
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

  async #flushInputPDO() {
    if (!this.sceneProxy?.pdo?.enabled) {
      this.#clearTransientInput();
      return;
    }
    if (this.input.writePending) {
      return;
    }

    const sentTransient = {
      pointerDeltaX: this.input.pointerDeltaX,
      pointerDeltaY: this.input.pointerDeltaY,
      wheelX: this.input.wheelX,
      wheelY: this.input.wheelY,
    };
    const hasTransient = hasInputTransient(sentTransient);
    const signature = makeInputSignature(this.input);
    const hasState = hasInputState(this.input);
    const shouldSendTransientReset = !hasTransient && this.input.transientResetPending;
    if (!hasTransient && !shouldSendTransientReset && signature === this.input.lastSentSignature) {
      return;
    }
    if (!hasTransient && !shouldSendTransientReset && !hasState && this.input.lastSentSignature === null) {
      this.input.lastSentSignature = signature;
      return;
    }

    this.input.writePending = true;
    const dataVersion = this.input.dataVersion++;
    const sequence = this.input.sequence++;
    const flags = (this.input.hasFocus ? InputStateFlags.focused : 0)
      | (this.input.pointerInside ? InputStateFlags.pointerInside : 0)
      | (this.input.pointerCaptured ? InputStateFlags.pointerCaptured : 0);
    try {
      const wrote = await writeInputStatePDO(this.sceneProxy, {
        dataVersion,
        sequence,
        sceneId: this.sceneProxy.pdo.makeId("input.scene", this.sceneProxy.sceneId ?? "scene"),
        viewportId: this.input.viewportId,
        flags,
        modifierMask: this.input.modifierMask,
        lockMask: 0,
        keys: this.input.keys,
        pointer: {
          pointerKind: 1,
          buttonMask: this.input.buttonMask,
          flags: 0,
          deviceId: 1,
          x: this.input.pointerX,
          y: this.input.pointerY,
          deltaX: sentTransient.pointerDeltaX,
          deltaY: sentTransient.pointerDeltaY,
          wheelX: sentTransient.wheelX,
          wheelY: sentTransient.wheelY,
        },
      });
      if (wrote) {
        this.input.lastSentSignature = signature;
        this.input.transientResetPending = hasTransient;
      }
    } catch {
      // 输入 PDO 是可丢帧通道：slot 尚未由后端创建或当前帧写缓冲不可用时，直接丢弃本帧输入。
    } finally {
      this.input.writePending = false;
      this.#subtractTransientInput(sentTransient);
    }
  }

  async #refreshDynamicRenderPDO() {
    if (!this.sceneProxy?.pdo?.enabled || this.isDefragging || this.dynamicReadPending) {
      return;
    }

    const renderDescriptors = [...this.slotDescriptors.values()].filter((descriptor) =>
      descriptor.slotRole === "Transform" || descriptor.slotRole === "Camera");
    const colliderDescriptors = [...this.colliderSlotDescriptors.values()];
    if (!renderDescriptors.length && !colliderDescriptors.length) {
      return;
    }

    this.dynamicReadPending = true;
    try {
      for (const descriptor of renderDescriptors) {
        await this.#readSlot(descriptor);
      }
      for (const descriptor of colliderDescriptors) {
        await this.#readColliderSlot(descriptor);
      }
    } finally {
      this.dynamicReadPending = false;
    }
  }

  #clearTransientInput() {
    this.input.pointerDeltaX = 0;
    this.input.pointerDeltaY = 0;
    this.input.wheelX = 0;
    this.input.wheelY = 0;
  }

  #subtractTransientInput(transient) {
    this.input.pointerDeltaX -= transient.pointerDeltaX;
    this.input.pointerDeltaY -= transient.pointerDeltaY;
    this.input.wheelX -= transient.wheelX;
    this.input.wheelY -= transient.wheelY;
    if (Math.abs(this.input.pointerDeltaX) < 0.000001) {
      this.input.pointerDeltaX = 0;
    }
    if (Math.abs(this.input.pointerDeltaY) < 0.000001) {
      this.input.pointerDeltaY = 0;
    }
    if (Math.abs(this.input.wheelX) < 0.000001) {
      this.input.wheelX = 0;
    }
    if (Math.abs(this.input.wheelY) < 0.000001) {
      this.input.wheelY = 0;
    }
  }

  #resetInputState() {
    this.input.keys.clear();
    this.input.buttonMask = 0;
    this.input.modifierMask = 0;
    this.input.pointerCaptured = false;
    this.input.lastSentSignature = null;
    this.input.transientResetPending = false;
    this.#clearTransientInput();
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

function modifierMaskFromEvent(event) {
  return (event.shiftKey ? InputModifierFlags.shift : 0)
    | (event.ctrlKey ? InputModifierFlags.ctrl : 0)
    | (event.altKey ? InputModifierFlags.alt : 0)
    | (event.metaKey ? InputModifierFlags.super : 0);
}

function buttonToMask(button) {
  switch (button) {
    case 0:
      return 1;
    case 1:
      return 4;
    case 2:
      return 2;
    case 3:
      return 8;
    case 4:
      return 16;
    default:
      return 0;
  }
}

function hasInputTransient(transient) {
  return Math.abs(Number(transient.pointerDeltaX ?? 0)) > 0.000001
    || Math.abs(Number(transient.pointerDeltaY ?? 0)) > 0.000001
    || Math.abs(Number(transient.wheelX ?? 0)) > 0.000001
    || Math.abs(Number(transient.wheelY ?? 0)) > 0.000001;
}

function hasInputState(input) {
  return Boolean(input.hasFocus)
    || Boolean(input.pointerInside)
    || Boolean(input.pointerCaptured)
    || Number(input.buttonMask ?? 0) !== 0
    || Number(input.modifierMask ?? 0) !== 0
    || (input.keys?.size ?? 0) > 0;
}

function makeInputSignature(input) {
  const keys = [...(input.keys ?? [])].map((key) => Number(key)).sort((left, right) => left - right).join(",");
  return [
    input.hasFocus ? 1 : 0,
    input.pointerInside ? 1 : 0,
    input.pointerCaptured ? 1 : 0,
    Number(input.buttonMask ?? 0),
    Number(input.modifierMask ?? 0),
    keys,
  ].join("|");
}

function isEditableTarget(target) {
  const element = target instanceof Element ? target : null;
  if (!element) {
    return false;
  }
  const tagName = element.tagName?.toLowerCase?.() ?? "";
  return element.isContentEditable || tagName === "input" || tagName === "textarea" || tagName === "select";
}

function delay(durationMs) {
  return new Promise((resolve) => window.setTimeout(resolve, durationMs));
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
