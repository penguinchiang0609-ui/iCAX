# CoreService

`CoreService` 是平台级基础服务工程，提供设置、日志等通用服务实现。

资源加载与缓存由 Framework/Resources 按 Project 隔离维护；Task/线程池能力不作为 CoreService 自动注册服务提供。

## 目录结构

当前目录直接放置 `CoreService.vcxproj` 及其源码文件。
