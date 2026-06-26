let styleInstalled = false;

export function mountProduct(context) {
  installStyle();
  const mount = context.mount;
  const product = context.product;
  if (!mount) {
    return;
  }

  mount.innerHTML = `
    <div class="flc-product">
      <div class="flc-summary">
        <div>
          <h2>${escapeText(product.productName)}</h2>
          <p>${escapeText(product.projectFile?.magic)} / ${escapeText(product.projectFile?.formatVersion)}</p>
        </div>
        <div class="flc-metrics">
          <span><strong>${product.isStarted ? "运行中" : "未启动"}</strong><small>产品状态</small></span>
          <span><strong>${escapeText(product.projectFile?.fileExtensions?.[0] ?? "-")}</strong><small>工程格式</small></span>
          <span><strong>${(product.recentProjects ?? []).length}</strong><small>最近项目</small></span>
        </div>
      </div>
    </div>
  `;
}

export function mountProject(context) {
  installStyle();
  const mount = context.mount;
  const project = context.project;
  if (!mount) {
    return;
  }

  mount.innerHTML = `
    <div class="flc-project">
      <aside class="flc-tree">
        <div class="flc-caption">工程</div>
        <button class="flc-tree-row active" type="button">图纸</button>
        <button class="flc-tree-row" type="button">板材</button>
        <button class="flc-tree-row" type="button">排样</button>
        <button class="flc-tree-row" type="button">刀路</button>
      </aside>
      <main class="flc-view">
        <div class="flc-view-toolbar">
          <button type="button">导入图纸</button>
          <button type="button">创建板材</button>
          <button type="button">自动排样</button>
          <button type="button">生成刀路</button>
          <button type="button">仿真</button>
        </div>
        <div class="flc-canvas">
          <svg viewBox="0 0 900 420" role="img" aria-label="Flat laser sheet">
            <rect x="30" y="28" width="840" height="360" fill="#f8fbfd" stroke="#7f96a8" stroke-width="2"/>
            <path d="M72 74 h132 v70 h-132 z M96 96 h24 v24 h-24 z M156 96 h24 v24 h-24 z" fill="#dfe9ee" stroke="#547082" stroke-width="2"/>
            <path d="M240 74 h132 v70 h-132 z M264 96 h24 v24 h-24 z M324 96 h24 v24 h-24 z" fill="#dfe9ee" stroke="#547082" stroke-width="2"/>
            <path d="M408 74 h132 v70 h-132 z M432 96 h24 v24 h-24 z M492 96 h24 v24 h-24 z" fill="#dfe9ee" stroke="#547082" stroke-width="2"/>
            <path d="M82 210 h128 v48 h-42 v58 h-86 z" fill="#dfe9ee" stroke="#547082" stroke-width="2"/>
            <path d="M252 210 h128 v48 h-42 v58 h-86 z" fill="#dfe9ee" stroke="#547082" stroke-width="2"/>
            <path d="M422 210 h128 v48 h-42 v58 h-86 z" fill="#dfe9ee" stroke="#547082" stroke-width="2"/>
            <path d="M70 62 H742 V322 H96" fill="none" stroke="#d1495b" stroke-width="3" stroke-dasharray="8 7"/>
          </svg>
        </div>
      </main>
      <aside class="flc-props">
        <div class="flc-caption">项目</div>
        <dl>
          <dt>名称</dt><dd>${escapeText(project?.projectName ?? "-")}</dd>
          <dt>状态</dt><dd>${escapeText(project?.state ?? "-")}</dd>
          <dt>邮箱</dt><dd>${escapeText(project?.projectMailId ?? "-")}</dd>
        </dl>
      </aside>
    </div>
  `;
}

function installStyle() {
  if (styleInstalled) {
    return;
  }

  const style = document.createElement("style");
  style.textContent = `
    .flc-product,.flc-project{color:#1f2a33}
    .flc-summary{display:flex;align-items:center;justify-content:space-between;gap:16px}
    .flc-summary h2{margin:0;font-size:18px}.flc-summary p{margin:4px 0 0;color:#617384}
    .flc-metrics{display:grid;grid-template-columns:repeat(3,minmax(90px,1fr));gap:8px}
    .flc-metrics span{border-left:3px solid #2f6f7e;padding-left:10px}.flc-metrics strong{display:block}.flc-metrics small{color:#617384}
    .flc-project{height:100%;min-height:500px;display:grid;grid-template-columns:180px minmax(0,1fr)220px;gap:10px}
    .flc-tree,.flc-props,.flc-view{min-width:0;border:1px solid #cdd7df;background:#fff}
    .flc-tree,.flc-props{padding:10px}.flc-caption{font-size:12px;font-weight:800;color:#617384;text-transform:uppercase}
    .flc-tree-row{width:100%;min-height:32px;margin-top:7px;border:1px solid #cdd7df;background:#fff;text-align:left;padding:0 9px}
    .flc-tree-row.active{border-color:#2f6f7e;background:#eaf6f7}
    .flc-view{display:grid;grid-template-rows:42px minmax(0,1fr)}
    .flc-view-toolbar{display:flex;align-items:center;gap:7px;padding:0 10px;border-bottom:1px solid #cdd7df}
    .flc-view-toolbar button{min-height:28px;border:1px solid #9caebe;background:#fff}
    .flc-canvas{min-height:0;padding:10px;background:#f7f9fb}.flc-canvas svg{width:100%;height:100%;min-height:430px;background:#eef2f5;border:1px solid #cdd7df}
    .flc-props dl{display:grid;grid-template-columns:48px minmax(0,1fr);gap:8px;margin:12px 0 0}.flc-props dt{color:#617384}.flc-props dd{margin:0;overflow-wrap:anywhere}
  `;
  document.head.appendChild(style);
  styleInstalled = true;
}

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}
