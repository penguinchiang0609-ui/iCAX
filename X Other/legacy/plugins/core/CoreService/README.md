# Legacy CoreService Files

本目录保留从 `src/icax/plugins/core/CoreService` 移出的旧实现。

- `TaskService.*`、`Task.h`、`BS_thread_pool.h`：旧线程池 Task 服务。当前运行框架不通过 CoreService 提供后台 Task/WorkShop/Coroutine 能力。
- `IResourceService.h`、`ResourceService.*`：旧应用级资源服务。当前资源由 Framework/Resources 按 Project 隔离维护。

这些文件仅作为历史参考，不参与当前解决方案构建。
