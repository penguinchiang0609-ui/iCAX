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
- 后续可放置 product module registry、ProductClient、ProductWorkspace container 和产品贡献协议。
