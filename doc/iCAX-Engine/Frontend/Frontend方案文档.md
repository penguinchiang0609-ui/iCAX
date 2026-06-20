# Frontend 方案文档

## 1. 工程位置

```text
src/icax-workbench/
```

`icax-workbench` 是通用 H5 工作台壳。具体产品 UI 放在 `src/apps/{app}/frontend/`，通过产品定义中的 `frontendEntry` 动态加载。

## 2. 模块划分

```text
icax-workbench
  shell       H5 启动、生命周期、全局错误边界
  bridge      宿主 bridge 抽象
  mailbox     Mailbox request/response 客户端
  pdo         PDO 订阅、缓存和 UI 绑定
  router      应用、产品、项目路由
  state       前端运行状态
  layout      AppBar、ProductRail、Workspace、Inspector、BottomDock
  product     产品前端模块加载和 ProductWorkspace 协议
  project     ProjectWorkspace 协议和项目视图容器
  common-ui   基础控件
  theme       主题变量、密度、颜色和图标规范
```

## 3. 状态模型

```text
WorkbenchState
  bridgeStatus
  application
    mailId
    products[]
    runningProducts[]
  activeProductId?
  productsById
    productId
      productMailId
      definition
      recentProjects[]
      catalogs[]
      activeProjectId?
  projectsById
    projectId
      productId
      catalogId
      projectMailId
      name
      path
      state
      dirty
      faultMessage
  ui
    layout
    activePanel
    pendingCommands
    notifications
```

前端状态只保存 UI 需要的快照。Repository、ResourcePool、Undo/Redo 历史、事务日志等都属于 backend。

## 4. 通信封装

Mailbox 客户端统一负责：

- request id 分配。
- payload 编解码。
- 超时、取消和错误归一化。
- 响应写回前端状态。

建议 H5 使用语义 API，而不是页面直接拼 payload：

```ts
appClient.getState();
appClient.startProduct(productId);
appClient.openProjectFile(projectPath);
productClient.openProjectCatalog(projectPath);
projectClient.execute(command, payload);
```

`MailboxClient.send()` 的返回值永远是 `Promise`。内部流程是：

```text
send(command)
  -> allocate request id
  -> pending[request id] = { resolve, reject, timeout }
  -> bridge.postMail(mailId, requestMail)
  -> return Promise

bridge.subscribeMail(mailId)
  -> receive response mail
  -> find pending by response.originId
  -> resolve/reject Promise
```

这样前端线程不会假设后端线程立即执行完成，也不会把 bridge 误设计成同步 RPC。

## 5. 路由

```text
/
  ApplicationWorkspace

/products/:productId
  ProductWorkspace

/products/:productId/projects/:projectId
  ProjectWorkspace
```

路由只是 UI 定位，不是 backend 生命周期的唯一来源。刷新或恢复时，应先从 `App.GetState` 与 `Product.GetState` 重建运行态，再映射到路由。

## 6. 产品 UI 扩展

产品前端入口由 backend 的 `ProductDefinition.FrontendEntry` 提供。H5 工作台加载入口模块后，期望模块导出：

```ts
interface ProductFrontendModule {
  productId: string;
  displayName: string;
  createProductWorkspace(context: ProductFrontendContext): ProductWorkspaceContribution;
  createProjectWorkspace(context: ProjectFrontendContext): ProjectWorkspaceContribution;
}
```

`ProductWorkspaceContribution` 可注册：

- 产品首页。
- 最近项目视图。
- 产品设置面板。
- 产品级命令。
- 产品级工具栏。

`ProjectWorkspaceContribution` 可注册：

- 主编辑视图。
- 左侧导航树。
- 右侧属性面板。
- 底部日志或任务页。
- 项目级命令。

## 7. UI 布局原则

- 第一屏就是工作台，不做营销式首页。
- 产品未启动时展示产品清单和最近打开入口。
- 产品启动后侧边栏展示当前产品和项目列表。
- 项目打开后中央区域优先给主编辑视图，属性、日志、任务放在可停靠区域。
- AppBar 保持稳定，只放应用级状态、产品切换、命令入口和设置。
- ProjectWorkspace 内的工具栏由产品模块贡献，通用壳不写业务按钮。

## 8. 双击文件

双击文件不经过产品选择：

```text
bridge.onProjectFileOpen(path)
  -> appClient.openProjectFile(path)
  -> backend resolves product by magic
  -> frontend loads product module by frontendEntry
  -> activate product and project route
```

如果 backend 返回 `NotFound`，前端显示无法识别项目文件，并提供“打开为外部资源/取消”等非项目入口。不能让用户强选产品打开项目文件。

## 9. 演进顺序

1. 建立 bridge、mailbox client 和 WorkbenchState。
2. 实现 ApplicationWorkspace：产品列表、启动产品、双击文件打开。
3. 实现 ProductWorkspace：最近项目、打开项目、ProjectCatalog 列表。
4. 实现 ProjectWorkspace 容器：主视图、属性、日志、命令结果。
5. 接入 PDO：项目状态、视图状态、高频进度。
6. 接入产品前端模块动态加载。
