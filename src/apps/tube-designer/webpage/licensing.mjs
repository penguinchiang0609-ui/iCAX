const escape = (value) => String(value ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
export function renderLicenseStatus(view) {
  const status = view?.tubeDesignerLicense;
  return `<div class="tube-designer-panel"><div class="tube-designer-heading"><strong>软件授权</strong><span>${status?.developmentBypass ? "开发免授权" : status?.activated ? "已激活" : "尚未验证"}</span></div>
    <p>${escape(status?.activated ? `${status.kind} · ${status.licenseId}` : status?.message || "点击上方“授权状态”检查本机授权。")}</p>
    ${status?.activated ? `<p>已授权：${[[1,"设计"],[2,"拆单"],[4,"STEP 导出"],[8,"下料排样"]].filter(([bit]) => status.features & bit).map(([,name]) => name).join("、")}</p>` : ""}
    <p>导出申请交给供应商，收到激活文件后导入。本机需以管理员身份运行。</p>
    <p>${escape(view?.tubeDesignerLicenseNotice)}</p></div>`;
}
export async function handleLicenseCommand(context, view, commandId, ops) {
  if (!commandId.startsWith("licensing.")) return false;
  const proxy = context.productProxy;
  const bridge = context.appProxy?.bridge ?? proxy?.bridge ?? context.sceneProxy?.bridge;
  const invoke = (method, payload = {}) => {
    if (typeof proxy?.invoke !== "function") throw new Error("当前产品没有授权服务。");
    return proxy.invoke(`TubeDesignerLicensing.${method}`, payload, {timeoutMs: 60000});
  };
  view.pending = true; view.error = "";
  view.tubeDesignerLicenseNotice = "正在处理授权，请稍候…";
  ops.renderProject(context, view);
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
      const path = await bridge.openFileDialog({title: "选择供应商签发的激活文件", filters: [{name: "授权激活文件", extensions: ["tdact"]}]});
      if (!path) { view.tubeDesignerLicenseNotice = "已取消激活。"; return true; }
      view.tubeDesignerLicense = await invoke("Activate", {path});
      view.tubeDesignerLicenseNotice = view.tubeDesignerLicense?.activated ? "激活成功，无需重启。" : "激活未完成，请查看原因。";
    } else if (commandId === "licensing.status") {
      view.tubeDesignerLicense = await invoke("Status");
      view.tubeDesignerLicenseNotice = "授权状态已更新。";
    } else throw new Error("未知授权操作。");
  } catch (error) {
    view.error = error?.message || String(error);
    if (commandId === "licensing.status" || commandId === "licensing.activate") {
      view.tubeDesignerLicense = {activated: false, message: view.error};
    }
    view.tubeDesignerLicenseNotice = "操作未完成，未放宽授权校验。";
  } finally {
    view.pending = false; ops.renderProject(context, view);
  }
  return true;
}
