# Frontend

本目录记录 iCAX H5 前端工作台设计。

H5 前端不直接拥有 backend 数据，也不直接访问 C++ 对象。前端通过宿主 bridge 获取应用级 mailbox 入口，再按 `ApplicationHost -> ProductRuntime -> Project` 的层级逐步获得产品和项目入口。

## 目录结构

- `Frontend规格文档.md`：H5 前端对外能力、页面结构、通信入口和启动流程。
- `Frontend方案文档.md`：内部模块划分、状态模型、路由和产品 UI 扩展方案。
