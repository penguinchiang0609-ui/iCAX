const ABOUT_VIEW_REVISION = "tube-designer-about:empty";

export function renderAboutLeftPane() {
  return `
    <div class="tube-designer-panel tube-designer-about-panel">
      <div class="tube-designer-about-mark" aria-hidden="true">TD</div>
      <div class="tube-designer-heading">
        <strong>TubeDesigner</strong>
        <span>管材产品设计与下料准备</span>
      </div>
      <p>从产品参数化设计、拆单到锯切排样，在同一个工程里保持产品、制造零件与下料结果的关联。</p>
      <dl>
        <div><dt>产品</dt><dd>设计与管理产品实例</dd></div>
        <div><dt>下料</dt><dd>零件清单、三维排样与结果</dd></div>
        <div><dt>管型</dt><dd>维护截面与规格</dd></div>
        <div><dt>草图</dt><dd>编辑截面和侧面切割图</dd></div>
      </dl>
    </div>`;
}

export function renderAboutRightPane() {
  return `
    <div class="tube-designer-panel tube-designer-about-details">
      <div class="tube-designer-heading"><strong>产品信息</strong><span>当前版本</span></div>
      <dl>
        <div><dt>产品名称</dt><dd>TubeDesigner</dd></div>
        <div><dt>版本</dt><dd>0.1.0</dd></div>
        <div><dt>下料策略</dt><dd>锯切优先</dd></div>
        <div><dt>复杂端面</dt><dd>安全斜截近似</dd></div>
      </dl>
    </div>`;
}

export function renderAboutViewportOverlay(_context, view) {
  scheduleAboutViewportReset(view);
  return `<div class="tube-designer-about-viewport">
    <div class="tube-designer-about-symbol" aria-hidden="true"><i></i><i></i><i></i></div>
    <strong>TubeDesigner</strong>
    <span>产品设计 · 锯切下料 · 管型与草图</span>
  </div>`;
}

function scheduleAboutViewportReset(view) {
  queueMicrotask(() => {
    if (view.activeAreaId !== "about" || !view.viewport?.applyViewSnapshot) return;
    if (view.viewport.getAppliedViewState?.().revision === ABOUT_VIEW_REVISION) return;
    view.viewport.setDimensionAnnotations?.([]);
    void view.viewport.applyViewSnapshot({ revision: ABOUT_VIEW_REVISION, rows: [] }).catch(() => {});
  });
}
