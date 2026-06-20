# router

`router` 放置 H5 工作台的路由定义和应用页面挂载逻辑。

路由只表达 UI 定位，不直接决定 backend 生命周期。刷新或恢复时应先从 backend 查询运行态，再把状态映射到路由。

## 目录结构

- `README.md`：本目录说明。
- 后续可放置 route table、navigation guard、product route registry 和 project route registry。

## 路由约定

```text
/
/products/:productId
/products/:productId/projects/:projectId
```
