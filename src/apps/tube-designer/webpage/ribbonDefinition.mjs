export const ribbonDefinition = {
  tabs: [
    {
      id: "view",
      title: "TubeDesigner",
      groups: [
        {
          title: "产品",
          commands: [
            command("designer.add", "添加", "add-instance", { size: "large", iconTone: "green" }),
            command("designer.batch-add", "批量添加", "excel", { size: "large", iconTone: "green" }),
            command("designer.export-machining", "导出加工", "machine", { size: "large", iconTone: "orange" }),
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
