// A preview session owns its native runtime resources and the renderer that
// consumes them. Closing waits for both before releasing the named scope.
const sessions = new WeakMap();

export function createDrawingPreviewKey() {
  const random = globalThis.crypto?.randomUUID?.()
    ?? `${Date.now()}-${Math.random().toString(36).slice(2)}`;
  return `drawing-${random}`.replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 96);
}

function sessionFor(state, restart = false) {
  let session = sessions.get(state);
  if (restart && session?.closed) {
    state.previewResourceKey = createDrawingPreviewKey();
    state.preview = null;
    session = null;
  }
  if (!session) {
    session = { key: state.previewResourceKey ??= createDrawingPreviewKey(), flights: new Set(), renderers: new Set(), closed: false };
    sessions.set(state, session);
  }
  return session;
}

export function retainDrawingPreviewRenderer(state) {
  const session = sessionFor(state, true);
  let detach;
  const released = new Promise(resolve => { detach = resolve; });
  session.renderers.add(released);
  return () => { session.renderers.delete(released); detach(); };
}

export function invokeDrawingPreview(sceneProxy, state, payload, options) {
  const session = sessionFor(state);
  if (session.closed) return Promise.reject(new Error("三维绘制已关闭。"));
  session.sceneProxy = sceneProxy;
  const flight = Promise.resolve().then(() => sceneProxy.invoke("TubeDesigner.PreviewPartDrawing", payload, options));
  session.flights.add(flight);
  return flight.finally(() => { session.flights.delete(flight); });
}

export function invokeTubeCadDocumentPreview(sceneProxy, state, payload, options) {
  const session = sessionFor(state);
  if (session.closed) return Promise.reject(new Error("CAD 编辑已关闭。"));
  session.sceneProxy = sceneProxy;
  session.releaseMethod = "TubeDesigner.ReleaseTubeCadDocumentPreview";
  const flight = Promise.resolve().then(() => sceneProxy.invoke("TubeDesigner.PreviewTubeCadDocument", payload, options));
  session.flights.add(flight);
  return flight.finally(() => { session.flights.delete(flight); });
}

export function releaseDrawingPreviewResources(state) {
  const session = sessions.get(state);
  if (!session) return Promise.resolve();
  session.closed = true;
  return session.release ??= Promise.allSettled([...session.flights, ...session.renderers]).then(async () => {
    if (!session.sceneProxy) return;
    try {
      await session.sceneProxy.invoke(session.releaseMethod ?? "TubeDesigner.ReleasePartDrawingPreview", { previewResourceKey: session.key });
    } catch (error) {
      state.previewReleaseError = error?.message ?? String(error);
      console.warn("三维绘制预览资源释放失败：", state.previewReleaseError);
    }
  });
}
