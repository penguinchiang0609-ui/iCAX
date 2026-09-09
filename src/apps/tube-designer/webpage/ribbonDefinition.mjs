const allRibbonDefinition = {
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
            command("designer.export-parts", "导出零件", "report", { size: "large", iconTone: "green" }),
            command("designer.disassemble", "导入下料", "machine", { size: "large", iconTone: "orange" }),
          ],
        },
        {
          title: "产品模板",
          commands: [
            command("designer.templates.manage", "模板管理", "report", { size: "large", iconTone: "blue" }),
            command("designer.templates.new", "新增模板", "add-instance", { size: "large", iconTone: "green" }),
            command("designer.templates.import", "导入 iPT", "new", { size: "large", iconTone: "green" }),
            command("designer.templates.export", "导出 iPT", "save", { size: "large", iconTone: "green" }),
            command("designer.templates.delete", "删除模板", "delete", { size: "large", iconTone: "orange" }),
          ],
        },
      ],
    },
    {
      id: "nesting",
      title: "下料",
      groups: [
        {
          title: "零件",
          commands: [
            command("nesting.import-part", "导入零件", "new", { size: "large", iconTone: "green" }),
            command("nesting.add-standard-part", "添加标准零件", "base", { size: "large", iconTone: "green" }),
            command("nesting.add-punch-part", "添加冲孔件", "hole", { size: "large", iconTone: "orange" }),
            command("nesting.draw-part", "三维绘制零件", "edit3d", { size: "large", iconTone: "green" }),
            command("nesting.export-parts", "零件清单", "report", { size: "large", iconTone: "green" }),
          ],
        },
        {
          title: "母材",
          commands: [
            command("nesting.stock-settings", "母材设置", "base", { size: "large", iconTone: "green" }),
          ],
        },
        {
          title: "排样",
          commands: [
            command("nesting.parameters", "排样参数", "measure", { size: "large", iconTone: "orange" }),
            command("nesting.start", "开始排样", "autonest", { size: "large", iconTone: "green" }),
            command("nesting.results", "查看排样结果", "report", { size: "large", iconTone: "blue" }),
            command("nesting.export-result", "导出排样结果", "export", { size: "large", iconTone: "green" }),
            command("nesting.to-machining", "送入加工", "machine", { size: "large", iconTone: "orange" }),
          ],
        },
      ],
    },
    {
      id: "machining",
      title: "加工",
      groups: [
        { title: "加工数据", commands: [
          command("machining.receive", "接收排样结果", "autonest", { size: "large", iconTone: "green" }),
          command("machining.import", "导入排样模型", "new", { size: "large", iconTone: "green" }),
          command("machining.remove", "移出加工", "delete"),
          command("machining.fit", "适合视图", "view-fit"),
        ] },
        { title: "刀路", commands: [
          command("machining.analyze", "分析所选", "measure", { size: "large", iconTone: "blue" }),
          command("machining.analyze-all", "分析全部", "measure"),
          command("machining.edit", "二维编辑", "edit2d", { size: "large", iconTone: "green" }),
        ] },
        { title: "加工工艺", commands: [
          command("machining.lead", "引线", "sketch-line", { size: "large" }),
          command("machining.joint", "微连", "cut", { size: "large" }),
          command("machining.compensation", "刀补", "measure", { size: "large" }),
        ] },
        { title: "加工排序", commands: [command("machining.order", "加工顺序", "report", { size: "large" })] },
        { title: "切割头", commands: [command("machining.head", "切割头模型", "machine", { size: "large", iconTone: "orange" })] },
      ],
    },
    {
      id: "resources",
      title: "资源库",
      groups: [
        {
          title: "资源类型",
          commands: [
            command("resources.profiles", "管型", "profile-sketch", { size: "large", iconTone: "green" }),
            command("resources.tools", "模具", "hole", { size: "large", iconTone: "orange" }),
            command("resources.components", "配件", "machine", { size: "large", iconTone: "blue" }),
          ],
        },
        {
          title: "管型操作",
          commands: [
            command("profiles.new-sketch", "绘制", "profile-sketch", { size: "large", iconTone: "green" }),
            command("profiles.import-package", "导入程式", "new", { size: "large", iconTone: "green" }),
            command("profiles.import-dxf", "导入定式", "hole", { size: "large", iconTone: "orange" }),
            command("profiles.export-dxf", "导出截面 DXF", "save", { size: "large", iconTone: "green" }),
            command("profiles.export-step", "导出管子 STEP", "machine", { size: "large", iconTone: "orange" }),
          ],
        },
        {
          title: "模具操作",
          commands: [command("tools.refresh", "刷新模具", "view-fit", { size: "large", iconTone: "green" })],
        },
        {
          title: "配件操作",
          commands: [
            command("components.draw", "绘制", "profile-sketch", { size: "large", iconTone: "green" }),
            command("components.import", "导入三维模型", "new", { size: "large", iconTone: "green" }),
            command("components.export-step", "导出配件 STEP", "save", { size: "large", iconTone: "green" }),
          ],
        },
      ],
    },
    {
      id: "sketch",
      title: "草图",
      groups: [
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
          ],
        },
        {
          title: "编辑",
          commands: [
            command("sketch.undo", "撤销", "undo"),
            command("sketch.redo", "重做", "redo"),
            command("sketch.break", "打断", "cut", { iconTone: "orange" }),
            command("sketch.insert-point", "插入点", "add-instance"),
            command("sketch.join", "合并", "merge", { iconTone: "green" }),
            command("sketch.delete", "删除", "delete"),
          ],
        },
        {
          title: "完成",
          commands: [
            command("sketch.commit", "确认", "apply", { size: "large", iconTone: "green" }),
            command("sketch.cancel", "取消", "undo", { size: "large", iconTone: "orange" }),
          ],
        },
      ],
    },
    {
      id: "about",
      title: "关于",
      groups: [{ title: "授权", commands: [
        command("licensing.status", "授权状态", "base", { size: "large" }),
        command("licensing.request", "导出授权申请", "save", { size: "large" }),
        command("licensing.request-trial", "导出试用申请", "save", { size: "large" }),
        command("licensing.activate", "导入激活文件", "new", { size: "large", iconTone: "green" }),
      ] }],
    },
  ],
};

export const sketchRibbonGroups = allRibbonDefinition.tabs.find(tab => tab.id === "sketch").groups;
export const ribbonDefinition = { ...allRibbonDefinition, tabs: allRibbonDefinition.tabs.filter(tab => tab.id !== "sketch") };

export function getRibbonDefinition() {
  return ribbonDefinition;
}

function command(id, title, iconName, options = {}) {
  return { id, title, iconName, ...options };
}
