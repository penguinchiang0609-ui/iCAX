import { renderLicenseStatus } from "./licensing.mjs";
import { tubeDesignerSvg } from "./branding.mjs";
const ABOUT_VIEW_REVISION = "tube-designer-about:empty";
const WECHAT_CONTACT_IMAGE = new URL("./assets/wechat-contact.jpg", import.meta.url).href;

export function renderAboutLeftPane() {
  return `
    <div class="tube-designer-panel tube-designer-about-panel">
      <div class="tube-designer-about-mark">${tubeDesignerSvg()}</div>
      <div class="tube-designer-heading">
        <strong>TubeDesigner</strong>
        <span>设计与拆单</span>
      </div>
      <p>参数化设计管材产品，生成制造零件清单。</p>
    </div>`;
}

export function renderAboutRightPane(_context, view) {
  return `
    <div class="tube-designer-panel tube-designer-about-details">
      <div class="tube-designer-heading"><strong>产品信息</strong><span>当前版本</span></div>
      <dl>
        <div><dt>产品名称</dt><dd>TubeDesigner</dd></div>
        <div><dt>版本</dt><dd>0.1.0</dd></div>
      </dl>
    </div>${renderLicenseStatus(view)}`;
}

export function renderAboutViewportOverlay(_context, view) {
  scheduleAboutViewportReset(view);
  return `<section class="tube-designer-about-viewport" data-tube-designer-contact tabindex="-1" aria-labelledby="tube-designer-contact-title">
    <strong id="tube-designer-contact-title">联系我们</strong>
    <div class="tube-designer-about-contact-image">
      <img src="${WECHAT_CONTACT_IMAGE}" alt="CAXStudio1024 微信二维码，使用微信扫一扫添加好友">
    </div>
  </section>`;
}

function scheduleAboutViewportReset(view) {
  queueMicrotask(() => {
    if (view.activeAreaId !== "about" || !view.viewport?.applyViewSnapshot) return;
    if (view.viewport.getAppliedViewState?.().revision === ABOUT_VIEW_REVISION) return;
    view.viewport.setDimensionAnnotations?.([]);
    void view.viewport.applyViewSnapshot({ revision: ABOUT_VIEW_REVISION, rows: [] }).catch(() => {});
  });
}
