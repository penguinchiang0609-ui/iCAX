# product

`product` 放置产品级 H5 工作区能力。

ProductWorkspace 对应 backend 的 `ProductRuntime`。它负责产品首页、最近项目、打开项目、产品设置和产品级命令入口。

## 职责

- 根据 `frontendEntry` 加载产品前端模块。
- 维护 product mail id 到 ProductClient 的映射。
- 展示最近项目和 ProjectCatalog 列表。
- 调用 `Product.OpenProjectCatalog` 和 `Product.CloseProjectCatalog`。
- 接收产品模块贡献的页面、面板和命令。

## 目录结构

- `README.md`：本目录说明。
- `productModuleHost.mjs`：按 `frontendEntry` 动态加载产品前端模块，并挂载到当前产品或项目工作区。

## 产品入口协议

产品前端入口可导出：

- `createProductWorkspace(context)`：产品首页工作区。
- `createProjectWorkspace(context)`：项目编辑工作区。
