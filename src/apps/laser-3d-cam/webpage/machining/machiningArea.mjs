import { renderPanel, renderSelection } from "../layout/commonViews.mjs";
import { getJobMachine } from "../state/sceneSelectors.mjs";
import { renderToolpathList } from "../toolpath/toolpathViews.mjs";
import { escapeText } from "../utils/format.mjs";
import { renderJobMachineSelector, renderMachiningReadiness } from "./machiningViews.mjs";

export function renderMachiningLeftPane(context, view) {
  const scene = view.scene ?? {};
  const toolpaths = scene.toolpaths ?? [];

  return `
    ${renderPanel("现场状态", renderMachiningReadiness(scene))}
    ${renderPanel("作业机床", renderJobMachineSelector(scene, view.pending))}
    ${renderPanel("刀路列表", renderToolpathList(toolpaths))}
    ${renderPanel("当前选择", renderSelection(scene.selection))}
    ${renderPanel("块与指令", `
      <div class="cam-empty-row">可在这里组织刀路块、前指令和后指令。</div>
    `)}
  `;
}

export function renderMachiningRightPane(context, view) {
  const scene = view.scene ?? {};
  const jobMachine = getJobMachine(scene);
  const model = scene.model ?? {};
  const toolpaths = scene.toolpaths ?? [];

  return `
    ${renderPanel("加工现场", `
      ${renderMachiningReadiness(scene)}
      <div class="cam-hint">这里操作主 Scene：机床实例、工件实例、刀路、作业规划和仿真都属于正式项目数据。</div>
    `)}
    ${renderPanel("刀路拾取", `
      <dl class="cam-facts">
        <dt>刀路</dt><dd>${escapeText(toolpaths.length)}</dd>
        <dt>图层</dt><dd>切割图层</dd>
        <dt>工艺</dt><dd>微联 / 引入引出</dd>
      </dl>
      ${renderSelection(scene.selection)}
      <div class="cam-button-row">
        <button class="tool-button" type="button" data-cam-action="recognize-loops" ${model.isLoaded && scene.topology?.hasTopology ? "" : "disabled"}>识别孔</button>
        <button class="primary-button" type="button" data-cam-action="add-selection" ${scene.selection?.id ? "" : "disabled"}>生成刀路</button>
        <button class="tool-button" type="button" data-cam-action="clear-toolpaths" ${toolpaths.length ? "" : "disabled"}>清空</button>
      </div>
    `)}
    ${renderPanel("排序规划", `
      <dl class="cam-facts">
        <dt>机床</dt><dd>${jobMachine.isLoaded ? escapeText(jobMachine.name || jobMachine.modelName || "作业机床") : "未配置"}</dd>
        <dt>刀路</dt><dd>${escapeText(toolpaths.length)}</dd>
        <dt>状态</dt><dd>未规划</dd>
      </dl>
      <div class="cam-list-row"><strong>轨迹规划</strong><small>避奇异点 / 减少下刀 / 蛙跳</small></div>
      <div class="cam-list-row"><strong>碰撞检测</strong><small>等待规划结果</small></div>
      <div class="cam-list-row"><strong>运动仿真</strong><small>等待规划结果</small></div>
    `)}
    ${renderPanel("输出", `
      <div class="cam-list-row"><strong>厂家</strong><small>待选择</small></div>
      <button class="primary-button" type="button" data-cam-action="job-placeholder">生成 NC</button>
    `)}
  `;
}
