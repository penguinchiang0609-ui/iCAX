import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";

export const machiningStages = Object.freeze([
  { id: "program", title: "刀路编程", description: "分析 · 三维 / 二维编辑" },
  { id: "process", title: "加工工艺", description: "引线 · 微连 · 刀补 · 顺序" },
  { id: "machine", title: "机床与夹持", description: "机型 · 卡盘 · 行程" },
  { id: "simulation", title: "加工仿真", description: "姿态 · 碰撞 · 切断" },
  { id: "output", title: "程序输出", description: "后处理 · 回验 · 发布" },
]);

export function machiningStage(view) {
  return view.tubeDesignerMachining?.stage ?? "program";
}

export function renderMachiningWorkflow(view, jobs) {
  const stage = machiningStage(view);
  return `<nav class="tube-cam-workflow" data-tube-machining-workflow aria-label="管材加工流程">
    <div class="tube-cam-workflow-brand"><strong>管材加工</strong><span>激光切管</span></div>
    <div class="tube-cam-workflow-steps">${machiningStages.map((item, index) => `<button type="button" class="${stage === item.id ? "active" : ""}" data-cam-action="tube-machining-stage" data-machining-stage="${item.id}" aria-pressed="${stage === item.id}"><span class="tube-cam-step-number">${index + 1}</span><span><strong>${item.title}</strong><small>${item.description}</small></span></button>`).join("")}</div>
    <div class="tube-cam-workflow-count"><strong>${jobs.length}</strong><span>根母材</span></div>
  </nav>`;
}

export function renderMachiningSourceActions(view) {
  return `<div class="tube-cam-source-actions"><button type="button" class="tube-designer-primary" data-cam-action="tube-machining-command" data-machining-command="receive" ${view.pending ? "disabled" : ""}>接收排样</button><button type="button" class="tube-designer-secondary" data-cam-action="tube-machining-command" data-machining-command="import" ${view.pending ? "disabled" : ""}>导入模型</button></div>`;
}

export function machiningReadiness(job, editor) {
  return [
    { id: "source", title: "加工来源", value: !job ? "待接收" : job.stale ? "来源已变化" : "已接收", state: !job ? "waiting" : job.stale ? "warning" : "ready" },
    { id: "paths", title: "名义刀路", value: `${editor?.paths.length ?? 0} 条${editor?.dirty ? " · 未保存" : ""}`, state: editor?.dirty || editor?.conflict ? "warning" : editor?.paths.length ? "ready" : "waiting" },
    { id: "process", title: "工艺轨迹", value: "待接入", state: "waiting" },
    { id: "machine", title: "机床与夹持", value: "待接入", state: "waiting" },
    { id: "verification", title: "仿真与程序回验", value: "未验证", state: "waiting" },
  ];
}

export function renderMachiningReadiness(job, editor) {
  return `<section class="tube-cam-readiness" aria-label="加工准备检查"><header><strong>加工准备</strong><span>当前母材</span></header><ul>${machiningReadiness(job, editor).map(item => `<li data-machining-check="${item.id}" class="${item.state}"><span class="tube-cam-check-dot" aria-hidden="true"></span><span>${item.title}</span><strong>${escapeText(item.value)}</strong></li>`).join("")}</ul></section>`;
}

const unavailable = (label, reason) => `<button type="button" class="tube-designer-secondary" disabled title="${escapeAttr(reason)}">${label}</button>`;
const card = (title, body) => `<section class="tube-cam-setting-card"><header><strong>${title}</strong><span class="tube-cam-capability-tag">待接入</span></header>${body}</section>`;
const specification = items => `<dl class="tube-cam-specification">${items.map(([label, value]) => `<dt>${label}</dt><dd>${value}</dd>`).join("")}</dl>`;

export function renderMachiningStagePanel(view, job, editor) {
  const stage = machiningStage(view);
  const item = machiningStages.find(item => item.id === stage) ?? machiningStages[0];
  if (stage === "program") return "";
  let content = "";
  if (stage === "process") {
    const choices = [["lead", "引线"], ["joint", "微连"], ["compensation", "刀补"], ["order", "顺序"]];
    const selected = view.tubeDesignerMachining?.section;
    const section = choices.some(([id]) => id === selected) ? selected : "lead";
    const definitions = {
      lead: ["引入 / 引出", [["方式", "贴面引线 / 空间引线"], ["编辑内容", "长度、方向、进刀姿态"], ["当前轨迹", "名义刀路"]]],
      joint: ["微连", [["布置", "指定位置 / 数量 / 间距"], ["约束", "拐角与切断避让"], ["当前轨迹", "未设置微连"]]],
      compensation: ["割缝补偿", [["输入", "有效割缝、切割方向、表面法向"], ["结果", "由设计轮廓生成实际切割轨迹"], ["当前轨迹", "未做刀补"]]],
      order: ["加工顺序", [["层级", "母材 → 零件 → 轮廓"], ["约束", "切断、支撑与避碰"], ["当前顺序", "名义刀路列表顺序"]]],
      head: ["切割头", [["定义", "头部几何与碰撞包络"], ["坐标", "刀尖位置、安装方向"], ["当前状态", "未配置"]]],
    };
    const definition = definitions[section] ?? definitions.lead;
    content = `<div class="tube-cam-process-tabs" aria-label="工艺类型">${choices.map(([id, title]) => `<button type="button" data-cam-action="tube-machining-command" data-machining-command="${id}" class="${section === id ? "active" : ""}" aria-pressed="${section === id}">${title}</button>`).join("")}</div>${card(definition[0], specification(definition[1]) + unavailable("应用工艺", "工艺轨迹生成尚未接入"))}<p class="tube-machining-note">工艺参数将作用于所选刀路；当前可编辑和保存名义刀路。</p>`;
  } else if (stage === "machine") {
    content = card("机床配置", specification([["目标机床", "未配置"], ["机床版本", "—"], ["切割头 / TCP", "未配置"]]) + unavailable("选择机床", "机床资源与加工任务的绑定尚未接入"))
      + card("夹持与送料", specification([["卡盘与支撑", "未配置"], ["夹持区 / 行程", "待机床定义"], ["尾料与换夹", "待规划"]]) + unavailable("规划夹持", "夹持规划尚未接入"));
  } else if (stage === "simulation") {
    content = card("仿真检查", `<ul class="tube-cam-review-list"><li>切割头姿态与轴行程</li><li>卡盘、支撑与零件碰撞</li><li>切断后的夹持与余料</li></ul>${unavailable("开始仿真", "需要工艺轨迹、机床及夹持计划；仿真尚未接入")}`)
      + `<p class="tube-machining-note">视口当前显示模型与名义刀路，尚无机床仿真结果。</p>`;
  } else if (stage === "output") {
    content = card("加工程序", specification([["后处理器", "未配置"], ["目标程序", "—"], ["程序回验", "未执行"], ["生产放行", "未放行"]]) + unavailable("生成 NC 程序", "后处理与程序回验尚未接入"))
      + `<p class="tube-machining-note">完成工艺、夹持和仿真后，按指定机床输出程序并回验。</p>`;
  }
  return `<section class="tube-cam-stage-panel" data-machining-stage-panel="${stage}"><header class="tube-machining-pane-title"><strong>${item.title}</strong><span>激光切管</span></header>${renderMachiningReadiness(job, editor)}${content}</section>`;
}
