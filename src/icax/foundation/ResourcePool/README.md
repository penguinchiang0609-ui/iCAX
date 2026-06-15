# ResourcePool

`ResourcePool` 是 foundation 层的进程内资源池。它用 `ResourceType + ResourceID` 定位资源，适合承载第三方模型、图片、文本、材质、渲染缓存等对象。

它只负责资源对象的注册、查找、替换、删除、枚举和轻量元信息维护，不负责文件加载、格式解析、GPU 上传或业务生命周期调度。

## 目录结构

- `ResourceKey.*`：资源稳定引用，包含资源类型和资源 ID。
- `ResourcePool.*`：线程安全资源池和类型安全模板访问接口。
- `ResourcePoolExport.h`：DLL 导出宏。
- `framework.h` / `pch.*` / `dllmain.cpp`：Visual Studio 动态库工程基础文件。
