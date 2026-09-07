export const ribbonDefinition = {
  tabs: [
    {
      id: "view",
      title: "产品",
      groups: [
        {
          title: "产品",
          commands: [
            command("designer.add", "添加", "add-instance", { size: "large", iconTone: "green" }),
            command("designer.batch-add", "批量添加", "excel", { size: "large", iconTone: "green" }),
            command("designer.disassemble", "拆单", "machine", { size: "large", iconTone: "orange" }),
          ],
        },
      ],
    },
    {
      id: "nesting",
      title: "下料",
      groups: [
        {
          title: "下料准备",
          commands: [
            command("nesting.stock-settings", "母材设置", "base", { size: "large", iconTone: "green" }),
            command("nesting.parameters", "排样参数", "measure", { size: "large", iconTone: "orange" }),
            command("nesting.start", "开始排样", "autonest", { size: "large", iconTone: "green" }),
          ],
        },
      ],
    },
    {
      id: "profiles",
      title: "管型",
      groups: [
        {
          title: "新增管型",
          commands: [
            command("profiles.import-package", "导入可编辑管型包", "new", { size: "large", iconTone: "green" }),
            command("profiles.import-dxf", "导入 DXF 管型", "hole", { size: "large", iconTone: "orange" }),
          ],
        },
        {
          title: "导出管型",
          commands: [
            command("profiles.export-dxf", "导出截面 DXF", "save", { size: "large", iconTone: "green" }),
            command("profiles.export-step", "导出管子 STEP", "machine", { size: "large", iconTone: "orange" }),
          ],
        },
      ],
    },
    {
      id: "components",
      title: "配件库",
      groups: [{
        title: "三维配件",
        commands: [
          command("components.import", "导入三维模型", "new", { size: "large", iconTone: "green" }),
          command("components.refresh", "刷新配件库", "apply", { size: "large", iconTone: "orange" }),
        ],
      }],
    },
    {
      id: "sketch",
      title: "草图",
      groups: [
        {
          title: "草图类型",
          commands: [
            command("sketch.mode-section", "管型截面图", "profile-sketch", { size: "large", iconTone: "green" }),
            command("sketch.mode-side", "管型侧面切割图", "side-sketch", { size: "large", iconTone: "orange" }),
          ],
        },
        {
          title: "导入",
          commands: [command("sketch.import", "导入 DXF", "new", { size: "large", iconTone: "green" })],
        },
        {
          title: "绘制",
          commands: [
            command("sketch.select", "选择", "select"),
            command("sketch.line", "直线", "sketch-line"),
            command("sketch.polyline", "折线", "sketch-polyline"),
            command("sketch.rectangle", "矩形", "sketch-rectangle"),
            command("sketch.circle", "圆", "sketch-circle"),
            command("sketch.arc", "圆弧", "sketch-arc"),
            command("sketch.spline", "样条", "sketch-spline"),
            command("sketch.freehand", "自由曲线", "edit2d"),
            command("sketch.text", "文字", "mark"),
          ],
        },
        {
          title: "编辑",
          commands: [
            command("sketch.undo", "撤销", "undo"),
            command("sketch.redo", "重做", "redo"),
            command("sketch.delete", "删除", "delete"),
          ],
        },
        {
          title: "完成",
          commands: [command("sketch.commit", "保存 / 应用", "apply", { size: "large", iconTone: "green" })],
        },
      ],
    },
    {
      id: "about",
      title: "关于",
      groups: [],
    },
  ],
};

export function getRibbonDefinition() {
  return ribbonDefinition;
}

function command(id, title, iconName, options = {}) {
  return { id, title, iconName, ...options };
}
