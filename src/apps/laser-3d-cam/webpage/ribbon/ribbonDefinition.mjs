export const ribbonDefinition = {
  tabs: [
    {
      id: "machine",
      title: "机床",
      groups: [
        {
          title: "定义资源",
          commands: [
            { id: "machine.import", title: "导入定义", icon: "M", size: "large" },
            { id: "machine.design", title: "机床设计", icon: "E", disabled: true },
          ],
        },
        {
          title: "参数",
          commands: [
            { id: "machine.parameters", title: "运动参数", icon: "P" },
            { id: "machine.tcp", title: "TCP", icon: "T" },
          ],
        },
        {
          title: "实例调试",
          commands: [
            { id: "machine.jog", title: "点动", icon: "J" },
            { id: "machine.home", title: "回零", icon: "H" },
            { id: "machine.reset", title: "复位", icon: "R" },
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
            { id: "workpiece.repair", title: "自动修复", icon: "R" },
            { id: "workpiece.place", title: "摆放", icon: "P" },
          ],
        },
        {
          title: "支撑夹具",
          commands: [
            { id: "support.generate", title: "生成支架", icon: "S" },
            { id: "support.export-dxf", title: "导出 DXF", icon: "X" },
          ],
        },
      ],
    },
    {
      id: "machining",
      title: "加工",
      groups: [
        {
          title: "现场",
          commands: [
            { id: "machine.import", title: "机床定义", icon: "M", size: "large" },
            { id: "workpiece.import", title: "工件模型", icon: "W", size: "large" },
            { id: "view.fit", title: "适合窗口", icon: "F" },
          ],
        },
        {
          title: "拾取",
          commands: [
            { id: "toolpath.pick-edge", title: "拾取 Edge", icon: "E" },
            { id: "toolpath.pick-loop", title: "拾取 Loop", icon: "L" },
            { id: "toolpath.clear-selection", title: "清除选择", icon: "C" },
          ],
        },
        {
          title: "生成",
          commands: [
            { id: "toolpath.recognize-holes", title: "识别孔", icon: "H", size: "large" },
            { id: "toolpath.add-selection", title: "生成刀路", icon: "G", size: "large" },
          ],
        },
        {
          title: "编辑工艺",
          commands: [
            { id: "toolpath.layer", title: "切割图层", icon: "Y" },
            { id: "toolpath.lead", title: "引入引出", icon: "N" },
            { id: "toolpath.microjoint", title: "微联", icon: "W" },
            { id: "toolpath.clear", title: "清空刀路", icon: "D" },
          ],
        },
        {
          title: "规划",
          commands: [
            { id: "job.create", title: "新建作业", icon: "J" },
            { id: "job.block", title: "块/指令", icon: "B" },
            { id: "job.plan-motion", title: "轨迹规划", icon: "P", size: "large" },
            { id: "job.collision", title: "碰撞检测", icon: "C" },
            { id: "job.simulate", title: "运动仿真", icon: "F" },
          ],
        },
        {
          title: "输出",
          commands: [
            { id: "job.postprocessor", title: "厂家", icon: "V" },
            { id: "job.preview-nc", title: "NC 预览", icon: "N" },
            { id: "job.export-nc", title: "导出 NC", icon: "O" },
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
