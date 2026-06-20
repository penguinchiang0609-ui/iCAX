# project

`project` 放置项目级 H5 工作区能力。

ProjectWorkspace 对应 backend 的 `Project`。它负责项目编辑视图、属性面板、项目命令、资源视图和项目级 PDO 绑定。

## 职责

- 维护 project mail id 到 ProjectClient 的映射。
- 承载产品模块贡献的主编辑视图。
- 提供项目导航、属性检查器、日志和任务区域。
- 转发保存、撤销、重做、导出等项目命令。
- 订阅项目级 PDO 快照和过程状态。

## 目录结构

- `README.md`：本目录说明。
- 后续可放置 ProjectClient、ProjectWorkspace container、panel host、command bar 和 project view registry。
