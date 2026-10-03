import { RENDER_ENTITY_VIEW_PROJECTION } from "../../_shared/workbench/projection.mjs";
import { typedVariant } from "../../../iCAX-UI/SDK/SDO/variantSerializer.mjs";

const sources = new WeakMap();
const resourceHydrations = new WeakMap();
export function assemblyProductIdentity(view) {
  const designer = view.scene?.tubeDesigner ?? {};
  return [String(designer.activeProductId || designer.product?.entityId || ""),
    String(designer.generationRun?.entityId || designer.product?.activeGenerationRunId || "")];
}

export function assemblyProductSceneState(view) {
  const identity = assemblyProductIdentity(view).join("/");
  const source = sources.get(view);
  return source?.identity === identity ? source : { status: "idle", snapshot: null, error: "" };
}

export function stopAssemblyProductScene(view) {
  const source = sources.get(view);
  sources.delete(view);
  void Promise.resolve(source?.reader?.stop?.()).catch(() => {});
}

export function assemblyProductResourceState(view) {
  const identity = assemblyProductIdentity(view).join("/");
  const state = resourceHydrations.get(view);
  return state?.identity === identity ? state : { identity, status: "idle", error: "", promise: null };
}

export function stopAssemblyProductResourceHydration(view) {
  resourceHydrations.delete(view);
}

export function ensureAssemblyProductResources(context, view) {
  const identity = assemblyProductIdentity(view).join("/");
  const existing = resourceHydrations.get(view);
  if (existing?.identity === identity && existing.sceneProxy === context.sceneProxy) return existing;
  const state = { identity, sceneProxy: context.sceneProxy, status: "loading", error: "", promise: null };
  resourceHydrations.set(view, state);
  const current = () => resourceHydrations.get(view) === state
    && assemblyProductIdentity(view).join("/") === identity;
  const repaint = () => {
    if (current() && view.activeAreaId === "assemblies"
      && view.tubeDesignerAssemblyLibrary?.workMode === "product")
      view.tubeDesignerAssemblyLibraryRenderProject?.();
  };
  if (!assemblyProductIdentity(view).every(Boolean)) { state.status = "empty"; return state; }
  if (typeof context.sceneProxy?.invoke !== "function") {
    state.status = "error";
    state.error = "当前产品资源尚未连接，请重新读取。";
    queueMicrotask(repaint);
    return state;
  }
  // The project file stores manufacturing recipes and source geometry, while
  // viewport meshes are derived.  List(true) restores those meshes in the
  // scene resource library before a viewport requests their persisted URLs.
  // Its snapshot is intentionally not assigned to view.scene: an older read
  // must never overwrite a later binding apply or an in-progress editor.
  state.promise = Promise.resolve().then(() => context.sceneProxy.invoke(
    "TubeDesigner.List", { includeManufacturingGeometry: true }, { timeoutMs: 180000 },
  )).then(() => {
    if (!current()) return state;
    state.status = "ready";
    repaint();
    return state;
  }).catch((error) => {
    if (!current()) return state;
    state.status = "error";
    state.error = `真实构件资源恢复失败：${error?.message ?? error}`;
    repaint();
    return state;
  });
  return state;
}

export function ensureAssemblyProductScene(context, view) {
  const [productId, runId] = assemblyProductIdentity(view);
  const identity = `${productId}/${runId}`;
  const existing = sources.get(view);
  if (existing?.identity === identity && existing.sceneProxy === context.sceneProxy) return existing;
  stopAssemblyProductScene(view);
  const source = { identity, sceneProxy: context.sceneProxy, status: "loading", snapshot: null, error: "", reader: null };
  sources.set(view, source);
  const current = () => sources.get(view) === source
    && assemblyProductIdentity(view).join("/") === identity
    && view.activeAreaId === "assemblies" && view.tubeDesignerAssemblyLibrary?.workMode === "product";
  const repaint = () => { if (current()) view.tubeDesignerAssemblyLibraryRenderProject?.(); };
  if (!productId || !runId) { source.status = "empty"; return source; }
  if (!context.sceneProxy?.views?.start) {
    source.status = "error";
    source.error = "当前产品视图尚未连接，请重新读取。";
    queueMicrotask(repaint);
    return source;
  }
  const receive = (snapshot) => {
    if (!current() || !snapshot || source.snapshot === snapshot) return;
    source.snapshot = snapshot;
    source.status = "ready";
    source.error = "";
    repaint();
  };
  source.promise = context.sceneProxy.views.start({ sources: [{
    sourceId: "tube-designer-assembly-product", role: "product-instance", language: "sql",
    where: "WHERE HAS CRenderInstanceComponent AND HAS CAssemblyMemberComponent AND CAssemblyMemberComponent.ProductID = :activeProductId",
    parameters: { activeProductId: typedVariant("uuid", productId) },
    projection: RENDER_ENTITY_VIEW_PROJECTION,
  }] }, { pollIntervalMs: 250, onChange: receive }).then(async (reader) => {
    if (!current()) { await reader.stop(); return; }
    source.reader = reader;
    receive(reader.snapshot ?? await reader.poll());
  }).catch((error) => {
    if (!current()) return;
    source.status = "error";
    source.error = `产品模型读取失败：${error?.message ?? error}`;
    repaint();
  });
  return source;
}

export function assemblyProductRows(view, snapshot) {
  // The View supplies the frontend mesh and its actual placement. Member
  // previewGeometryResourceId is a source BRep/mesh, not a render resource.
  const members = new Set((view.scene?.tubeDesigner?.members ?? []).map((member) => String(member.entityId)));
  return (snapshot?.rows ?? []).filter((row) => members.has(String(row.entityId))).map((row) => ({
    ...row, data: { ...row.data, visible: true, selectable: false },
  }));
}

export function assemblyProductParts(view) {
  const designer = view.scene?.tubeDesigner ?? {};
  if (!assemblyProductIdentity(view).every(Boolean) || designer.product?.modelOutdated || designer.product?.partsOutdated) return [];
  return (designer.parts ?? []).filter((part) => part.entityId && part.thumbnailGeometryResourceId);
}

export function assemblyProductPartRows(part) {
  // Final manufacturing meshes are in normalized part coordinates. Show one
  // actual part at a time; the product placement cannot safely undo this transform.
  return part ? [{ entityId: String(part.entityId), data: {
    geometry: { url: part.thumbnailGeometryResourceId, version: part.thumbnailGeometryResourceVersion },
    geometryKind: 1, renderClass: 1, visible: true, selectable: false,
  } }] : [];
}
