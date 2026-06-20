# state

`state` 放置 H5 前端运行状态模型。

前端状态只表达 UI 所需的轻量快照，不承载 backend 业务数据。Repository、ResourcePool、Undo/Redo 日志、事务日志都属于 backend。

## 状态边界

- `application`：应用 mail id、产品清单、运行中产品。
- `productsById`：产品定义、product mail id、最近项目、ProjectCatalog 摘要。
- `projectsById`：项目名称、路径、project mail id、运行状态、错误信息。
- `ui`：当前路由、活动产品、活动项目、停靠面板、命令状态和通知。

## 目录结构

- `README.md`：本目录说明。
- `workbenchStore.mjs`：工作台内存状态、订阅、patch、命令日志，以及 application/product/project 快照归并。
