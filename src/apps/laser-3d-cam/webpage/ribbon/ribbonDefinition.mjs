export const ribbonDefinition = {
  tabs: [
    {
      id: "machine",
      title: "机床",
      groups: [
        {
          title: "机床定义",
          commands: [
            { id: "machine.import", title: "导入定义", icon: "M", size: "large" },
            { id: "machine.design", title: "机床设计", icon: "E", disabled: true },
          ],
        },
      ],
    },
    {
      id: "workpiece",
      title: "工件编辑",
      groups: [
        {
          title: "模型资源",
          commands: [
            { id: "workpiece.import", title: "导入工件", icon: "I", size: "large" },
            { id: "workpiece.update", title: "更新", icon: "U" },
            { id: "workpiece.delete", title: "删除", icon: "D" },
          ],
        },
        {
          title: "检查修复",
          commands: [
            { id: "workpiece.check", title: "模型检查", icon: "K" },
            { id: "workpiece.repair", title: "CAD 编辑", icon: "R" },
            { id: "workpiece.place", title: "摆放", icon: "P" },
          ],
        },
        {
          title: "意图刀路",
          commands: [
            { id: "intent.from-selection", title: "从选择创建", icon: "C", size: "large" },
            { id: "intent.draw", title: "绘制", icon: "D", disabled: true },
            { id: "intent.micro-joint", title: "添加微联", icon: "M", disabled: true },
            { id: "intent.lead", title: "引入引出", icon: "L", disabled: true },
            { id: "intent.break", title: "打断", icon: "B", disabled: true },
            { id: "intent.merge", title: "合并", icon: "G", disabled: true },
          ],
        },
      ],
    },
    {
      id: "machining",
      title: "作业",
      groups: [
        {
          title: "作业设置",
          commands: [
            { id: "job.create", title: "新建作业", icon: "J", size: "large" },
            { id: "job.machine", title: "机床/控制器", icon: "M" },
            { id: "job.setup", title: "装夹/工件", icon: "S", disabled: true },
            { id: "job.operations", title: "加工操作", icon: "O", disabled: true },
          ],
        },
        {
          title: "轨迹生成",
          commands: [
            { id: "job.generate-toolpath", title: "生成 Toolpath", icon: "T", size: "large", disabled: true },
            { id: "job.link-paths", title: "连接路径", icon: "L", disabled: true },
            { id: "job.orientation", title: "姿态场", icon: "A", disabled: true },
            { id: "job.process-events", title: "工艺事件", icon: "E", disabled: true },
          ],
        },
        {
          title: "运动规划",
          commands: [
            { id: "job.block", title: "块/指令", icon: "B" },
            { id: "job.plan-motion", title: "轨迹规划", icon: "P", size: "large" },
            { id: "job.inverse-kinematics", title: "逆解", icon: "I", disabled: true },
            { id: "job.joint-limits", title: "轴限位", icon: "L", disabled: true },
            { id: "job.collision", title: "碰撞检测", icon: "C" },
          ],
        },
        {
          title: "验证",
          commands: [
            { id: "job.simulate", title: "运动仿真", icon: "F", size: "large" },
            { id: "job.cycle-time", title: "节拍估算", icon: "T", disabled: true },
            { id: "job.safety-report", title: "安全报告", icon: "R", disabled: true },
          ],
        },
        {
          title: "输出",
          commands: [
            { id: "job.postprocessor", title: "后处理器", icon: "P" },
            { id: "job.preview-nc", title: "NC 预览", icon: "N" },
            { id: "job.export-nc", title: "导出 NC", icon: "O", size: "large" },
          ],
        },
      ],
    },
    {
      id: "view",
      title: "视图",
      groups: [
        {
          title: "视口",
          commands: [
            { id: "view.fit", title: "适合窗口", icon: "F", size: "large" },
            { id: "view.standard", title: "标准视图", icon: "V" },
            { id: "view.display", title: "显示模式", icon: "D" },
          ],
        },
        {
          title: "面板",
          commands: [
            { id: "view.left-panel", title: "左侧面板", icon: "L" },
            { id: "view.right-panel", title: "右侧面板", icon: "R" },
            { id: "view.reset-layout", title: "重置布局", icon: "Q" },
          ],
        },
      ],
    },
  ],
};

export function getRibbonDefinition() {
  return ribbonDefinition;
}

export function normalizeCamTab(tabId) {
  if (tabId === "toolpath" || tabId === "job") {
    return "machining";
  }
  return ribbonDefinition.tabs.some((tab) => tab.id === tabId) ? tabId : "machine";
}

export function getTabTitle(tabId) {
  return ribbonDefinition.tabs.find((tab) => tab.id === normalizeCamTab(tabId))?.title ?? "机床";
}

export function findCommandTitle(commandId) {
  for (const tab of ribbonDefinition.tabs) {
    for (const group of tab.groups) {
      const command = group.commands.find((item) => item.id === commandId);
      if (command) {
        return command.title;
      }
    }
  }
  return "该";
}
