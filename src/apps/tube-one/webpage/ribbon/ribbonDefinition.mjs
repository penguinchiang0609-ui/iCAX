export const ribbonDefinition = {
  tabs: [
    {
      id: "workpiece",
      title: "TubeOne",
      groups: [
        {
          title: "文件",
          scope: "main",
          commands: [
            command("project.new", "新建", "new", { disabled: true }),
            command("nest.create", "套料", "nest", { disabled: true }),
            command("job.export-nc", "导出", "export", { disabled: true }),
            command("job.preview-nc", "报告", "report", { disabled: true }),
          ],
        },
        {
          title: "视图",
          scope: "main",
          commands: [
            command("view.select", "选择", "select", { disabled: true }),
            command("view.display", "显示", "display", { disabled: true }),
          ],
        },
        {
          title: "零件修正",
          scope: "main",
          commands: [
            command("workpiece.check", "截面修正", "repair", { size: "large", iconTone: "magenta" }),
            command("workpiece.repair", "二维编辑", "edit2d", { iconTone: "blue" }),
            command("intent.recognize-cad", "三维零件编辑", "edit3d", { size: "large", iconTone: "green" }),
          ],
        },
        {
          title: "图形工艺",
          scope: "main",
          commands: [
            command("process.auto", "自动工艺", "process", { disabled: true, iconTone: "green" }),
            command("process.clear", "清除", "delete", { disabled: true }),
            command("process.sample", "一键打样", "autonest", { disabled: true }),
            command("process.weld", "焊缝补偿", "weld", { disabled: true, iconTone: "magenta" }),
            command("process.hole", "添加坡口", "hole", { disabled: true, iconTone: "green" }),
            command("process.optimize", "刀路优化", "optimize", { disabled: true }),
            command("process.compensation", "补偿", "repair", { disabled: true, iconTone: "green" }),
            command("process.lead", "引线", "edit2d", { disabled: true, iconTone: "green" }),
            command("process.micro-joint", "微连", "micro", { disabled: true, iconTone: "green" }),
          ],
        },
        {
          title: "排样",
          scope: "main",
          commands: [
            command("nest.manual", "手动套料", "manual", { disabled: true }),
            command("nest.merge", "合并零件", "merge", { disabled: true, iconTone: "orange" }),
            command("nest.auto", "自动排样", "autonest", { disabled: true, iconTone: "green" }),
          ],
        },
        {
          title: "排序",
          scope: "main",
          commands: [
            command("nest.sort", "排序", "sort", { disabled: true, size: "large" }),
            command("job.simulate", "模拟", "simulate", { disabled: true, iconTone: "green" }),
          ],
        },
        {
          title: "工具",
          scope: "main",
          commands: [
            command("view.measure", "测量", "measure", { disabled: true }),
            command("job.collision", "路径检查", "collision", { disabled: true }),
            command("app.support", "支持", "support", { disabled: true, iconTone: "blue" }),
          ],
        },
        {
          title: "操作",
          scope: "editor",
          commands: [
            command("tube.editor.select", "选择", "select", { disabled: true }),
            command("tube.editor.delete", "删除", "delete", { disabled: true }),
            command("tube.editor.merge", "合并", "merge", { disabled: true }),
            command("tube.editor.move", "移动", "move", { disabled: true, iconTone: "blue" }),
            command("tube.editor.move-face", "移动面", "move", { disabled: true, iconTone: "orange" }),
            command("tube.editor.boolean", "布尔", "boolean", { disabled: true, iconTone: "magenta" }),
          ],
        },
        {
          title: "设计",
          scope: "editor",
          commands: [
            command("tube.editor.base", "主管", "base", { disabled: true, size: "large", iconTone: "orange" }),
            command("tube.editor.branch", "支管", "branch", { disabled: true, iconTone: "green" }),
            command("tube.editor.bevel-sharp", "尖角 V 槽", "bevel", { disabled: true, iconTone: "magenta" }),
            command("tube.editor.bevel-round", "圆角 V 槽", "bevel", { disabled: true, iconTone: "blue" }),
            command("tube.editor.cut", "切断", "cut", { disabled: true, iconTone: "orange" }),
            command("tube.editor.mark", "标记", "mark", { disabled: true, iconTone: "blue" }),
          ],
        },
        {
          title: "完成",
          scope: "editor",
          commands: [
            command("intent.recognize-cad", "重新识别", "process", { iconTone: "green" }),
            command("tube.editor.finish", "完成", "apply", { size: "large", iconTone: "green" }),
          ],
        },
      ],
    },
  ],
};

export function getRibbonDefinition() {
  return ribbonDefinition;
}

function command(id, title, iconName, options = {}) {
  return { id, title, iconName, ...options };
}
