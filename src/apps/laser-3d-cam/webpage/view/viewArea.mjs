import { renderPanel } from "../layout/commonViews.mjs";

export function renderViewLeftPane() {
  return `
    ${renderPanel("显示对象", `
      <div class="cam-list-row active"><strong>工件</strong><small>显示</small></div>
      <div class="cam-list-row active"><strong>刀路</strong><small>显示</small></div>
      <div class="cam-list-row"><strong>机床</strong><small>未导入</small></div>
    `)}
    ${renderPanel("面板", `
      <div class="cam-list-row active"><strong>左侧上下文</strong><small>显示</small></div>
      <div class="cam-list-row active"><strong>右侧参数</strong><small>显示</small></div>
    `)}
  `;
}

export function renderViewRightPane() {
  return `
    ${renderPanel("视图参数", `
      <dl class="cam-facts">
        <dt>相机</dt><dd>后端控制</dd>
        <dt>拾取</dt><dd>视口对象</dd>
        <dt>显示</dt><dd>实体 / 刀路 / 机床</dd>
      </dl>
    `)}
  `;
}
