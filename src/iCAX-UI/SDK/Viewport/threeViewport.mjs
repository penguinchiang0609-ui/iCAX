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

const DEFAULT_CAMERA = Object.freeze({
  eye: { x: 260, y: -320, z: 220 },
  target: { x: 0, y: 0, z: 0 },
  up: { x: 0, y: 0, z: 1 },
});

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
    this.geometryPayloads = new Map();
    this.geometryObjects = new Map();
    this.instancePayloads = new Map();
    this.transformPayloads = new Map();
    this.objectSlots = new Map();
    this.sceneObjects = new Map();
    this.cameras = new Map();
    this.activeCameraId = null;
    this.styles = new Map();
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
    this.renderer.dispose();
    this.root.remove();
  }

  async refreshAll() {
    const reads = [];
    for (const descriptor of this.slotDescriptors.values()) {
      reads.push(this.#readSlot(descriptor));
    }
    await Promise.allSettled(reads);
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
      version: RenderPDOLayout.version,
    };
    this.slotDescriptors.set(descriptor.pdoId, descriptor);
    this.#emitDiagnosticOnce(
      `slot:${payload.event}:${descriptor.pdoId}`,
      `RenderPDO slot ${payload.event}: role=${descriptor.slotRole}, pdo=${descriptor.pdoId}`,
      "info",
    );
    await this.#readSlot(descriptor);
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

  async #readSlot(descriptor) {
    if (!this.sceneProxy?.pdo?.enabled || this.isDefragging || !descriptor?.payloadCapacity) {
      return;
    }

    let lastError = null;
    for (let attempt = 0; attempt < 3; attempt += 1) {
      try {
        await this.sceneProxy.pdo.withReadDescriptor({
          id: descriptor.pdoId,
          version: descriptor.version,
          payloadSize: descriptor.payloadCapacity,
        }, (buffer) => {
          const payload = parseRenderPDOPayload(buffer);
          this.#applyPayload(descriptor, payload);
        });
        return;
      } catch (error) {
        lastError = error;
        if (this.#isTransientPDOReadError(error) && attempt < 2) {
          await delay(24 * (attempt + 1));
          continue;
        }
        break;
      }
    }

    if (lastError) {
      this.#setStatus(`RenderPDO 读取失败: ${lastError.message}`);
      this.#emitDiagnostic(`RenderPDO 读取失败：${lastError.message}`, "error");
    }
  }

  #isTransientPDOReadError(error) {
    const message = String(error?.message ?? error ?? "");
    return message.includes("slot buffer index is invalid")
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
      return;
    }
    if (payload.kind === "instance_list") {
      this.#applyInstanceList(descriptor, payload);
      this.#emitDiagnosticOnce(
        `instance:${descriptor.pdoId}:${payload.header?.dataVersion ?? ""}`,
        `RenderPDO 实例已读取：${payload.instances?.length ?? 0} 个对象`,
        "ok",
      );
      this.#setStatus("");
      return;
    }
    if (payload.kind === "transform") {
      this.#applyTransform(payload);
      this.#emitDiagnosticOnce(
        `transform:${payload.transformId}:${payload.header?.dataVersion ?? ""}`,
        `RenderPDO Transform 已读取：transform=${payload.transformId}`,
        "info",
      );
      this.#setStatus("");
      return;
    }
    if (payload.kind === "camera") {
      this.#applyCamera(payload);
      this.#emitDiagnosticOnce(
        `camera:${payload.activeCameraId}:${payload.header?.dataVersion ?? ""}`,
        `RenderPDO 相机已读取：active=${payload.activeCameraId ?? "-"}`,
        "ok",
      );
    }
  }

  #applyInstanceList(descriptor, payload) {
    for (const style of payload.styles ?? []) {
      this.styles.set(style.styleId, style);
    }

    const liveObjectIds = new Set();
    for (const instance of payload.instances ?? []) {
      liveObjectIds.add(instance.objectId);
      this.objectSlots.set(instance.objectId, descriptor.pdoId);
      this.instancePayloads.set(String(instance.objectId), instance);
      this.#upsertSceneObject(instance);
    }

    if (descriptor.objectId && payload.instances?.length === 0) {
      this.#removeSceneObject(descriptor.objectId);
      this.instancePayloads.delete(String(descriptor.objectId));
    }
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
    const transform = this.transformPayloads.get(instance.transformId);
    object.matrix.fromArray(transform?.localToWorld ?? identityMatrixArray());
    object.matrixWorldNeedsUpdate = true;
  }

  #upsertObjectsUsingGeometry(geometryId) {
    for (const instance of this.instancePayloads.values()) {
      if (instance.geometryId === geometryId) {
        this.#upsertSceneObject(instance);
      }
    }
  }

  #instantiateObject(instance, geometryObject) {
    const material = this.#makeMaterial(instance);
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
      styleId: instance.styleId,
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

  #makeMaterial(instance) {
    const style = this.styles.get(instance.styleId);
    const color = rgbaToCssColor(style?.colorRGBA ?? this.#defaultColorForClass(instance.renderClass));
    if (instance.geometryKind === RenderGeometryKind.mesh) {
      return new THREE.MeshLambertMaterial({
        color: new THREE.Color(color.r / 255, color.g / 255, color.b / 255),
        transparent: color.a < 1,
        opacity: Math.max(0.08, color.a),
        side: THREE.DoubleSide,
      });
    }
    return new THREE.LineBasicMaterial({
      color: new THREE.Color(color.r / 255, color.g / 255, color.b / 255),
      linewidth: style?.lineWidth ?? 1,
      transparent: color.a < 1,
      opacity: Math.max(0.08, color.a),
    });
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
        object.geometry = geometry;
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

  #removeSceneObject(objectId) {
    const object = this.sceneObjects.get(String(objectId));
    if (!object) {
      return;
    }
    object.parent?.remove(object);
    object.material?.dispose?.();
    this.sceneObjects.delete(String(objectId));
  }

  #clearRenderContent() {
    for (const object of this.sceneObjects.values()) {
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
    this.transformPayloads.clear();
    this.cameras.clear();
    this.activeCameraId = null;
    this.diagnosticKeys.clear();
  }

  #applyTransform(payload) {
    this.transformPayloads.set(payload.transformId, payload);
    this.#updateObjectsUsingTransform(payload.transformId);
    if (this.activeCameraId && this.cameras.get(this.activeCameraId)?.transformId === payload.transformId) {
      this.#applyActiveCamera();
    }
  }

  #applyCamera(payload) {
    this.cameras.clear();
    for (const camera of payload.cameras ?? []) {
      this.cameras.set(camera.cameraId, camera);
    }
    this.activeCameraId = payload.activeCameraId ?? null;
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
    this.renderer.render(this.scene, this.camera);
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

    this.input.writePending = true;
    const dataVersion = this.input.dataVersion++;
    const sequence = this.input.sequence++;
    const sentTransient = {
      pointerDeltaX: this.input.pointerDeltaX,
      pointerDeltaY: this.input.pointerDeltaY,
      wheelX: this.input.wheelX,
      wheelY: this.input.wheelY,
    };
    const flags = (this.input.hasFocus ? InputStateFlags.focused : 0)
      | (this.input.pointerInside ? InputStateFlags.pointerInside : 0)
      | (this.input.pointerCaptured ? InputStateFlags.pointerCaptured : 0);
    try {
      await writeInputStatePDO(this.sceneProxy, {
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

    const descriptors = [...this.slotDescriptors.values()].filter((descriptor) =>
      descriptor.slotRole === "Transform" || descriptor.slotRole === "Camera");
    if (!descriptors.length) {
      return;
    }

    this.dynamicReadPending = true;
    try {
      await Promise.allSettled(descriptors.map((descriptor) => this.#readSlot(descriptor)));
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
  `;
  document.head.appendChild(style);
}
