export const ribbonDefinition = {
  tabs: [
    {
      id: "view",
      title: "TubeDesigner",
      groups: [
        {
          title: "产品",
          commands: [
            command("designer.generate", "生成产品", "apply", { size: "large", iconTone: "green" }),
            command("designer.reset", "恢复默认", "repair", { iconTone: "blue" }),
          ],
        },
        {
          title: "数据",
          commands: [
            command("designer.import-excel", "导入 Excel", "open", { disabled: true }),
            command("designer.export-step", "导出 STEP", "export", { size: "large", iconTone: "orange" }),
          ],
        },
        {
          title: "视图",
          commands: [
            command("view.fit", "适合窗口", "fit", { iconTone: "blue" }),
            command("view.reset-layout", "重置布局", "display"),
          ],
        },
        {
          title: "下游",
          commands: [
            command("designer.send-to-tube-one", "进入 TubeOne", "process", { disabled: true, iconTone: "magenta" }),
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
