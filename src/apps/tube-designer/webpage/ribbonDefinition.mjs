import { commandLicenseFeature, hasLicenseFeature, isLicenseWorkspaceReadOnly, licenseDenialMessage } from "./licensing.mjs";

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
            command("designer.delete-active-product", "删除", "delete", { size: "large", iconTone: "orange" }),
          ],
        },
        {
          title: "零件",
          commands: [
            command("designer.inspect-active-part", "复尺", "measure", { size: "large", iconTone: "blue" }),
            command("designer.disassemble", "拆单", "merge", { size: "large", iconTone: "green" }),
            command("designer.export-active-product-parts", "导出清单", "report", { size: "large", iconTone: "green" }),
          ],
        },
        {
          title: "Excel",
          commands: [
            command("designer.excel.export-template", "导出模板", "excel", { size: "large", iconTone: "green" }),
            command("designer.import-excel", "导入产品", "excel", { size: "large", iconTone: "blue" }),
            command("designer.excel.automation-settings", "自动处理设置", "base", { size: "large", iconTone: "green" }),
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
            command("nesting.add-from-products", "从产品添加", "add-instance", { size: "large", iconTone: "blue" }),
            command("nesting.import-part", "导入零件", "new", { size: "large", iconTone: "green" }),
            command("nesting.add-standard-part", "添加标准零件", "base", { size: "large", iconTone: "green" }),
            command("nesting.add-punch-part", "添加冲孔件", "hole", { size: "large", iconTone: "orange" }),
            command("nesting.draw-part", "三维绘制零件", "edit3d", { size: "large", iconTone: "green" }),
            command("nesting.draw-2d-part", "二维绘制零件", "edit2d", { size: "large", iconTone: "green" }),
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
            command("resources.products", "产品", "report", { size: "large", iconTone: "blue" }),
            command("resources.profiles", "管型", "profile-sketch", { size: "large", iconTone: "green" }),
            command("resources.tools", "单件工艺", "hole", { size: "large", iconTone: "orange" }),
            command("resources.assemblies", "装配", "merge", { size: "large", iconTone: "green" }),
          ],
        },
        {
          title: "产品模板",
          commands: [
            command("designer.templates.import", "导入 itpt", "new", { size: "large", iconTone: "green" }),
            command("designer.templates.delete", "删除", "delete", { size: "large", iconTone: "orange" }),
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
          title: "单件工艺操作",
          commands: [
            command("tools.new-sketch", "绘制", "profile-sketch", { size: "large", iconTone: "green" }),
            command("tools.import-package", "导入程式", "new", { size: "large", iconTone: "green" }),
            command("tools.import-dxf", "导入定式", "hole", { size: "large", iconTone: "orange" }),
            command("tools.refresh", "刷新工艺", "view-fit", { size: "large", iconTone: "green" }),
          ],
        },
        {
          title: "装配操作",
          commands: [
            command("assemblies.import-package", "导入", "new", { size: "large", iconTone: "green" }),
          ],
        },
      ],
    },
    {
      id: "sketch",
      title: "草图",
      groups: [
        {
          title: "文件",
          commands: [
            command("sketch.import", "导入 DXF", "new", { iconTone: "green" }),
            command("sketch.export", "导出 DXF", "save", { iconTone: "green" }),
          ],
        },
        {
          title: "绘制",
          commands: [
            command("sketch.select", "选择", "select"),
            command("sketch.line", "直线", "sketch-line"),
            command("sketch.polyline", "折线", "sketch-polyline"),
            command("sketch.rectangle", "矩形", "sketch-rectangle"),
            command("sketch.circle", "圆", "sketch-circle"),
            command("sketch.ellipse", "椭圆", "sketch-circle"),
            command("sketch.arc", "圆弧", "sketch-arc"),
            command("sketch.spline", "样条", "sketch-spline"),
            command("sketch.freehand", "自由曲线", "edit2d"),
            command("sketch.racetrack", "长圆孔", "sketch-rectangle"),
            command("sketch.polygon", "多边形", "sketch-polyline"),
            command("sketch.star", "星形", "sketch-polyline"),
            command("sketch.text-outline", "文字轮廓", "edit2d"),
          ],
        },
        {
          title: "变换",
          commands: [
            command("sketch.move", "移动", "select"),
            command("sketch.copy", "复制", "add-instance"),
            command("sketch.rotate", "旋转", "repair"),
            command("sketch.mirror", "镜像", "merge"),
            command("sketch.scale", "缩放", "view-fit"),
            command("sketch.align", "对齐", "measure"),
          ],
        },
        {
          title: "修图",
          commands: [
            command("sketch.trim", "修剪", "cut"),
            command("sketch.extend", "延伸", "sketch-line"),
            command("sketch.offset", "偏移", "sketch-polyline"),
            command("sketch.fillet", "圆角", "sketch-arc"),
            command("sketch.chamfer", "倒角", "cut"),
            command("sketch.measure", "测量", "measure"),
            command("sketch.diagnose", "检查", "report"),
            command("sketch.repair", "修复", "repair"),
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
          title: "阵列",
          commands: [
            command("sketch.array-rectangular", "二维阵列", "array", { iconTone: "green" }),
            command("sketch.array-polar", "圆周阵列", "repair", { iconTone: "green" }),
            command("sketch.array-circumferential", "沿管周阵列", "array", { iconTone: "green" }),
            command("sketch.array-edit", "编辑阵列", "array"),
          ],
        },
        {
          title: "零件",
          commands: [
            command("sketch.end-cuts", "编辑端部", "cut"),
            command("sketch.split-parts", "实体分件", "merge"),
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
        command("licensing.request", "导出授权申请", "save", { size: "large" }),
        command("licensing.request-trial", "导出试用申请", "save", { size: "large" }),
        command("licensing.activate", "导入激活文件", "new", { size: "large", iconTone: "green" }),
        command("licensing.export-license", "导出授权文件", "save", { size: "large" }),
      ] }],
    },
  ],
};

export const sketchRibbonGroups = allRibbonDefinition.tabs.find(tab => tab.id === "sketch").groups;
// 下料工作区公开；加工仍暂不公开，草图沿用资源编辑时的上下文入口。
export const ribbonDefinition = {
  ...allRibbonDefinition,
  tabs: allRibbonDefinition.tabs.filter(tab => ["view", "nesting", "resources", "about"].includes(tab.id)),
};

export function getRibbonDefinition(options = {}) {
  const resourceArea = ["products", "profiles", "tools", "assemblies"].includes(options?.resourceArea)
    ? options.resourceArea : "profiles";
  const resourceCommandAreas = {
    "resources.products": "products",
    "resources.profiles": "profiles",
    "resources.tools": "tools",
    "resources.assemblies": "assemblies",
  };
  const resourceGroupAreas = {
    "产品模板": "products",
    "管型操作": "profiles",
    "单件工艺操作": "tools",
    "装配操作": "assemblies",
  };
  return {
    ...ribbonDefinition,
    tabs: ribbonDefinition.tabs.map((tab) => tab.id !== "resources" ? tab : ({
      ...tab,
      groups: tab.groups
        .filter((group) => !resourceGroupAreas[group.title] || resourceGroupAreas[group.title] === resourceArea)
        .map((group) => ({
          ...group,
          commands: group.commands.map((item) => resourceCommandAreas[item.id]
            ? { ...item, active: resourceCommandAreas[item.id] === resourceArea }
            : item),
        })),
    })).map(tab => {
      const context = options.licenseContext ?? {};
      const view = options.licenseView;
      const describe = item => {
        const feature = commandLicenseFeature(item.id, tab.id);
        const blocked = (!item.id.startsWith("licensing.") && isLicenseWorkspaceReadOnly(context, view))
          || !hasLicenseFeature(context, view, feature);
        return { ...item, authorizationRequired: blocked,
          unavailableReason: blocked ? licenseDenialMessage(context, view, feature || "page.product") : "",
          menuItems: item.menuItems?.map(describe) };
      };
      return { ...tab, authorizationRequired: false,
        unavailableReason: "",
        groups: tab.groups.map(group => ({ ...group, commands: group.commands.map(describe) })) };
    }),
  };
}

function command(id, title, iconName, options = {}) {
  return { id, title, iconName, ...options };
}
