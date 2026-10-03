// Read-only bridge between the assembly catalogue and the active product's
// committed connection declarations. A catalogue preview is never a product
// manufacturing result.

function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (character) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  })[character]);
}

const connectionKindNames = new Map([
  ["assembly", "插接装配"], ["weld", "焊接"], ["insert", "插入"],
  ["corner", "转角连接"], ["hinge", "铰接"],
]);

function manufacturingStatus(parts) {
  const references = parts.flatMap((part) => Array.isArray(part.stockRefs) ? part.stockRefs : []);
  const states = parts.flatMap((part) => part.stockRefs?.length
    ? part.stockRefs.map((stock) => stock.partMappingStatus) : [part.manufacturingMappingStatus]);
  const stockIds = references.map((stock) => stock.stockEntityId).filter(Boolean);
  const shared = parts.some((part) => part.manufacturingMappingStatus === "shared-stock")
    || new Set(stockIds).size < stockIds.length;
  if (states.some((value) => value === "not-output")) return "含非独立下料构件";
  if (states.some((value) => value === "not-present")) return "下料件未就绪";
  if (states.length && states.every((value) => value === "persisted")) return shared ? "共用母材已保存" : "下料件已保存";
  if (states.some((value) => value === "transient")) return shared ? "共用母材待保存" : "下料件待保存";
  if (states.some((value) => value === "planned")) return shared ? "共用母材待生成" : "下料件待生成";
  if (shared) return "共用母材，保存状态未提供";
  return "下料状态未提供";
}

export function productAssemblyManufacturingRevision(view) {
  const designer = view?.scene?.tubeDesigner ?? {};
  const productId = String(designer.activeProductId || designer.product?.entityId || "");
  const runId = String(designer.generationRun?.entityId || designer.product?.activeGenerationRunId || "");
  const group = (designer.manufacturingGroups ?? []).find((item) =>
    String(item.productEntityId ?? "") === productId && String(item.generationRunId ?? "") === runId);
  return JSON.stringify((group?.parts ?? designer.parts ?? []).map((part) => [
    part.entityId || part.stableKey || "", part.length ?? null, part.status ?? "",
    part.manufacturingGeometryResourceId ?? "", part.manufacturingGeometryResourceVersion ?? null,
  ]).sort((a, b) => String(a[0]).localeCompare(String(b[0]))));
}

export function productAssemblyConnectionIdentity(view) {
  const designer = view?.scene?.tubeDesigner ?? {};
  const productEntityId = String(designer.activeProductId || designer.product?.entityId || "").trim();
  const generationRunId = String(designer.generationRun?.entityId
    || designer.product?.activeGenerationRunId || "").trim();
  const modelOutdated = !!designer.product?.modelOutdated;
  const partsOutdated = !!designer.product?.partsOutdated;
  const key = productEntityId && generationRunId
    ? `${productEntityId}/${generationRunId}/${modelOutdated}/${partsOutdated}/${productAssemblyManufacturingRevision(view)}` : "";
  return { productEntityId, generationRunId, key };
}

export function productAssemblyConnectionsState(view) {
  return view.tubeDesignerAssemblyProductConnections ??= {
    key: "", status: "idle", result: null, error: "", promise: null,
  };
}

export function ensureProductAssemblyConnections(context, view, ops) {
  const identity = productAssemblyConnectionIdentity(view);
  const state = productAssemblyConnectionsState(view);
  if (!identity.key) {
    state.key = "";
    state.status = "idle";
    state.result = null;
    state.error = "";
    state.promise = null;
    return Promise.resolve();
  }
  if (state.key === identity.key && state.status !== "idle") return state.promise ?? Promise.resolve();
  if (typeof context?.sceneProxy?.invoke !== "function") return Promise.resolve();

  state.key = identity.key;
  state.status = "loading";
  state.result = null;
  state.error = "";
  const request = context.sceneProxy.invoke("TubeDesigner.GetProductAssemblyConnections", {
    productEntityId: identity.productEntityId,
    generationRunId: identity.generationRunId,
  }, { timeoutMs: 30000 }).then((result) => {
    if (state.key !== identity.key || productAssemblyConnectionIdentity(view).key !== identity.key) return;
    if (result?.productEntityId !== identity.productEntityId
        || result?.generationRunId !== identity.generationRunId
        || result?.source !== "committed-product-model"
        || !Array.isArray(result?.connections)) {
      throw new Error("当前产品连接数据与已提交模型不一致");
    }
    state.result = result;
    state.status = "ready";
    if (view.activeAreaId === "assemblies") ops?.renderProject?.(context, view);
  }).catch((error) => {
    if (state.key !== identity.key || productAssemblyConnectionIdentity(view).key !== identity.key) return;
    state.result = null;
    state.status = "error";
    state.error = error?.message ?? String(error);
    if (view.activeAreaId === "assemblies") ops?.renderProject?.(context, view);
  }).finally(() => {
    if (state.key === identity.key && state.promise === request) state.promise = null;
  });
  state.promise = request;
  return request;
}

export function renderProductAssemblyConnections(view, selectedKey = "") {
  const identity = productAssemblyConnectionIdentity(view);
  const state = productAssemblyConnectionsState(view);
  if (!identity.productEntityId) return `<section class="tube-assembly-product-connections"><strong>当前产品连接</strong><span>选中产品后可查看其真实连接关系。</span></section>`;
  if (!identity.generationRunId) return `<section class="tube-assembly-product-connections"><strong>当前产品连接</strong><span>该产品尚无已提交模型。</span></section>`;
  if (state.key !== identity.key || state.status === "idle" || state.status === "loading")
    return `<section class="tube-assembly-product-connections"><strong>当前产品连接</strong><span>正在读取已提交模型…</span></section>`;
  if (state.status === "error")
    return `<section class="tube-assembly-product-connections"><strong>当前产品连接</strong><span class="error">${escapeHtml(state.error || "读取失败")}</span></section>`;

  const result = state.result;
  const connections = result?.connections ?? [];
  const outdated = result?.modelOutdated || result?.partsOutdated;
  const rows = connections.map((connection, index) => {
    const parts = Array.isArray(connection.participants) ? connection.participants : [];
    const names = parts.map((part, partIndex) => `${String.fromCharCode(65 + partIndex)}：${part.name || part.itemKey || "未命名构件"}`);
    const kind = connectionKindNames.get(connection.kind) ?? connection.kind ?? "连接";
    return `<li><button type="button" data-cam-action="tube-designer-assembly-select-connection" data-tube-connection-key="${escapeHtml(connection.key)}" aria-pressed="${connection.key === selectedKey}"${result?.modelOutdated ? " disabled" : ""} title="${escapeHtml(connection.label || connection.key)}"><strong>${escapeHtml(`连接 ${index + 1} · ${kind}`)}</strong><span>${escapeHtml([connection.label, names.join(" + ")].filter(Boolean).join(" · "))}</span><small>${manufacturingStatus(parts)}</small></button></li>`;
  }).join("");
  return `<section class="tube-assembly-product-connections" data-tube-product-connections><header><strong>1 · 选择连接节点</strong><span>${connections.length} 处 · 已提交模型</span></header>${outdated ? `<p class="warning">产品或拆单已变化，请重新核对。</p>` : ""}${rows ? `<ol>${rows}</ol>` : `<p>该产品模板没有声明连接位置。</p>`}<p>${rows ? "先选构件的连接节点，再选装配工艺。" : "请先生成有实际连接关系的产品。"}</p></section>`;
}
