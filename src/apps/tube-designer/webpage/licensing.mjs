const escape = (value) => String(value ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
// Native capabilities already include the parent page grant. Never decode
// certificate bits or infer an entitlement from a development/release flag.
const sessions = new WeakMap();
const bindings = new WeakMap();
const pageFeatures = { view: "page.product", nesting: "page.nesting", parts: "page.nesting", machining: "page.machining", simulation: "page.simulation" };
const pageLabels = { "page.product": "产品页", "page.nesting": "下料页", "page.machining": "加工页", "page.simulation": "仿真页" };

function sessionFor(context, view) {
  const proxy = context?.productProxy;
  if (!proxy || typeof proxy.invoke !== "function") return null;
  const channel = String(proxy.productChannelId ?? "");
  let session = sessions.get(proxy);
  if (!session || session.channel !== channel) {
    session = { channel, status: null, promise: null, request: 0, views: new Set() };
    sessions.set(proxy, session);
  }
  if (view) {
    if (bindings.get(view) !== session) {
      bindings.get(view)?.views.delete(view);
      bindings.set(view, session);
      view.tubeDesignerLicense = session.status;
    }
    session.views.add(view);
    if (session.status) view.tubeDesignerLicense = session.status;
  }
  return session;
}

export function licenseStatus(context, view) {
  return sessionFor(context, view)?.status ?? view?.tubeDesignerLicense ?? null;
}

function publishStatus(session, status) {
  session.status = status;
  for (const view of session.views) {
    if (view.disposed) { session.views.delete(view); continue; }
    view.tubeDesignerLicense = status;
  }
}

export async function ensureLicenseStatus(context, view, { refresh = false } = {}) {
  const session = sessionFor(context, view);
  if (!session) {
    const status = { activated: false, capabilities: {}, message: "当前产品没有连接授权服务。" };
    if (view) view.tubeDesignerLicense = status;
    return status;
  }
  if (!refresh && session.status) return session.status;
  if (session.promise) return session.promise;
  const request = ++session.request;
  const promise = (async () => {
    let status;
    try {
      status = await context.productProxy.invoke("TubeDesignerLicensing.Status", {}, { timeoutMs: 60000 });
      if (status?.featureSchemaVersion !== 1 || !status.capabilities || typeof status.capabilities !== "object") throw new Error("授权服务未返回有效的功能权限，请检查当前安装。");
    } catch (error) {
      status = { activated: false, capabilities: {}, message: error?.message || String(error), errorMessage: error?.message || String(error) };
    }
    if (sessions.get(context.productProxy) === session && session.request === request) {
      publishStatus(session, status);
      if (isLicenseWorkspaceReadOnly(context, view) && !session.unlicensedLogged) {
        context.actions?.log?.("error", "软件未授权。可切换产品、下料、资源库和关于页面；请在关于页申请授权或导入激活文件。");
        session.unlicensedLogged = true;
      }
    }
    return status;
  })();
  session.promise = promise;
  try { return await promise; }
  finally { if (session.promise === promise) session.promise = null; }
}

export const pageLicenseFeature = (areaId) => pageFeatures[areaId] ?? "";
export function isLicenseWorkspaceReadOnly(context, view) {
  return !Object.keys(pageLabels).some(id => hasLicenseFeature(context, view, id));
}
export function hasLicenseFeature(context, view, featureId) {
  if (Array.isArray(featureId)) return featureId.some(id => hasLicenseFeature(context, view, id));
  return !featureId || licenseStatus(context, view)?.capabilities?.[featureId] === true;
}
export function licenseFeatureLabel(context, view, featureId) {
  if (Array.isArray(featureId)) return featureId.map(id => licenseFeatureLabel(context, view, id)).join(" / ");
  return licenseStatus(context, view)?.featureCatalog?.find(item => item.id === featureId)?.label ?? pageLabels[featureId] ?? featureId;
}
export function licenseDenialMessage(context, view, featureId) {
  const status = licenseStatus(context, view);
  return `${licenseFeatureLabel(context, view, featureId)}未授权。${status?.message ? ` ${status.message}` : "请到“关于”查看授权状态，导出申请或导入激活文件。"}`;
}

// An unavailable operation reports only to the log, leaving the editor intact.
export function showLicenseDenial(context, view, featureId, message = "") {
  const text = message || licenseDenialMessage(context, view, featureId);
  context.actions?.log?.("error", text);
  return false;
}

export function checkPageLicense() {
  // A page grant contributes to its function grants, never to navigation.
  return true;
}

export function commandLicenseFeature(commandId, areaId = "view") {
  if (/\.(cancel|close)$/.test(commandId)) return "";
  if (commandId.startsWith("licensing.") || commandId.startsWith("app.")) return "";
  const resourceFeature = resourceCommandLicenseFeature(commandId);
  if (resourceFeature !== undefined) return resourceFeature;
  if (commandId.startsWith("simulation.")) return commandId === "simulation.run" ? "simulation.run" : "page.simulation";
  if (commandId.startsWith("machining.")) return /export/.test(commandId) ? "machining.export" : "machining.toolpath";
  if (commandId.startsWith("nesting.")) {
    if (["nesting.export-parts", "nesting.export-result"].includes(commandId)) return "nesting.export";
    if (commandId === "nesting.start") return "nesting.calculate";
    if (commandId === "nesting.results") return "page.nesting";
    return "nesting.edit";
  }
  if (commandId === "designer.disassemble") return "product.breakdown";
  if (commandId === "designer.export-active-product-parts" || commandId === "designer.excel.export-template") return "product.export";
  if (commandId === "designer.inspect-active-part") return "page.product";
  if (commandId.startsWith("designer.")) return "product.design";
  if (commandId.startsWith("sketch.") && areaId === "nesting") return "nesting.edit";
  return pageLicenseFeature(areaId);
}

export function actionLicenseFeature(action, areaId = "view") {
  if (action.startsWith("licensing.")) return "";
  if (/-(close|cancel)$/.test(action)) return "";
  const resourceFeature = resourceActionLicenseFeature(action, areaId);
  if (resourceFeature !== undefined) return resourceFeature;
  if (/^(tube-designer-(toggle|select|focus)-|tube-designer-parts-(search|filter|clear-filters|select|toggle|fit|default|dimension|element))/.test(action)) return pageLicenseFeature(areaId);
  if (/export/.test(action)) return areaId === "nesting" ? "nesting.export" : areaId === "machining" ? "machining.export" : areaId === "view" ? "product.export" : "";
  if (/disassembl|refresh-manufacturing-plan/.test(action)) return "product.breakdown";
  if (/^tube-designer-(nesting|punch|part-drawing|part-edit|delete-nesting|standard-part|part-import)/.test(action)) return /start|calculate/.test(action) ? "nesting.calculate" : "nesting.edit";
  if (/^tube-designer-machining-/.test(action)) return "machining.toolpath";
  if (/^tube-designer-simulation-/.test(action)) return "simulation.run";
  if (areaId === "view" && action.startsWith("tube-designer-")) return "product.design";
  if (areaId === "nesting" && action.startsWith("tube-designer-")) return "nesting.edit";
  return pageLicenseFeature(areaId);
}

export function allowLicenseFeature(context, view, featureId) {
  return hasLicenseFeature(context, view, featureId) || showLicenseDenial(context, view, featureId);
}

export function renderLicenseStatus(view) {
  const status = view?.tubeDesignerLicense;
  const title = status?.developmentBypass ? "开发免授权" : status?.activated ? "已激活" : "未激活";
  return `<div class="tube-designer-panel" data-tube-designer-license-status><div class="tube-designer-heading"><strong>软件授权</strong><span>${title}</span></div>
    <p>${escape(status?.activated ? `${status.kind} · ${status.licenseId}` : status?.message || "正在检查本机授权…")}</p>
    ${status?.developmentBypass ? "" : "<p>页面与页面内功能分别授权。导出申请交给供应商，收到激活文件后导入。</p>"}
    <p>${escape(view?.tubeDesignerLicenseNotice)}</p></div>`;
}

export async function handleLicenseCommand(context, view, commandId, ops) {
  if (!commandId.startsWith("licensing.")) return false;
  if (view.tubeDesignerLicenseBusy) return true;
  const proxy = context.productProxy;
  const bridge = context.appProxy?.bridge ?? proxy?.bridge ?? context.sceneProxy?.bridge;
  const invoke = (method, payload = {}) => {
    if (typeof proxy?.invoke !== "function") throw new Error("当前产品没有授权服务。");
    return proxy.invoke(`TubeDesignerLicensing.${method}`, payload, {timeoutMs: 60000});
  };
  const session = sessionFor(context, view);
  view.tubeDesignerLicenseBusy = true;
  view.error = "";
  view.tubeDesignerLicenseNotice = "正在处理授权，请稍候…";
  try {
    if (commandId === "licensing.request" || commandId === "licensing.request-trial") {
      if (!bridge?.openDirectoryDialog) throw new Error("当前宿主不支持选择保存目录。");
      const directory = await bridge.openDirectoryDialog({title: "选择授权申请保存目录"});
      if (!directory) { view.tubeDesignerLicenseNotice = "已取消导出申请。"; return true; }
      const filename = `TubeDesigner-${new Date().toISOString().replace(/[:.]/g, "-")}.tdreq`;
      const path = `${String(directory).replace(/[\\/]$/, "")}\\${filename}`;
      await invoke(commandId === "licensing.request-trial" ? "RequestTrial" : "Request", {path});
      view.tubeDesignerLicenseNotice = `申请已保存：${path}。请将此文件交给供应商。`;
    } else if (commandId === "licensing.activate") {
      if (!bridge?.openFileDialog) throw new Error("当前宿主不支持选择激活文件。");
      const path = await bridge.openFileDialog({title: "选择供应商签发的授权文件", filters: [{name: "授权文件", extensions: ["tdact"]}]});
      if (!path) { view.tubeDesignerLicenseNotice = "已取消激活。"; return true; }
      const status = await invoke("Activate", {path});
      if (status?.featureSchemaVersion !== 1 || !status.capabilities) throw new Error("激活结果缺少功能权限，请检查当前安装。");
      if (session) { session.request += 1; publishStatus(session, status); } else view.tubeDesignerLicense = status;
      view.tubeDesignerLicenseNotice = status?.activated ? "授权已更新，无需重启。" : "授权未更新，请查看原因。";
    } else if (commandId === "licensing.export-license") {
      if (!bridge?.openDirectoryDialog) throw new Error("当前宿主不支持选择保存目录。");
      const directory = await bridge.openDirectoryDialog({title: "选择授权文件保存目录"});
      if (!directory) { view.tubeDesignerLicenseNotice = "已取消导出授权文件。"; return true; }
      const filename = `iCAX-Authorization-${new Date().toISOString().replace(/[:.]/g, "-")}.tdact`;
      const path = `${String(directory).replace(/[\\/]$/, "")}\\${filename}`;
      await invoke("ExportLicenseFile", {path});
      view.tubeDesignerLicenseNotice = `完整授权文件已保存：${path}。升级授权时请将此文件交给供应商。`;
    } else if (commandId === "licensing.status") {
      const status = await ensureLicenseStatus(context, view, { refresh: true });
      view.error = status?.errorMessage ?? "";
      view.tubeDesignerLicenseNotice = view.error ? "操作未完成，未放宽授权校验。" : "授权状态已更新。";
    } else throw new Error("未知授权操作。");
  } catch (error) {
    view.error = error?.message || String(error);
    if (commandId === "licensing.status" || commandId === "licensing.activate") {
      const status = { activated: false, capabilities: {}, message: view.error };
      if (session) publishStatus(session, status); else view.tubeDesignerLicense = status;
    }
    view.tubeDesignerLicenseNotice = "操作未完成，未放宽授权校验。";
  } finally {
    view.tubeDesignerLicenseBusy = false;
    if (!view.disposed && context.isCurrentProject?.() !== false) {
      context.actions?.refreshProductRibbon?.();
      context.refreshLicenseControls?.(view);
      // Update only this panel; allowed inputs, canvas and scroll containers
      // retain their actual nodes and the user's latest interaction state.
      const panel = context.mount?.querySelector?.("[data-tube-designer-license-status]");
      if (panel) panel.outerHTML = renderLicenseStatus(view);
      ops?.appendProjectLog?.(context, view.error ? "error" : "info", view.error || view.tubeDesignerLicenseNotice);
    }
  }
  return true;
}
import { resourceActionLicenseFeature, resourceCommandLicenseFeature } from "./resourceLicensing.mjs";
