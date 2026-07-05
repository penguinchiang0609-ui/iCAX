# Resources

`Resources` 是 framework 层的资源系统项目。上层用资源路径或来源字符串定位资源，路径本身就是资源键，适合承载第三方模型、图片、文本、材质、渲染缓存等对象。

它负责资源条目、资源对象池、轻量元信息、资源内容版本、持久化策略、manifest 查询、ResourceLoader 抽象和 ResourceLoader 注册表。具体文件格式解析、GPU 上传或业务生命周期调度由插件或业务模块实现。

## 目录结构

- `ResourceKey.*`：Resources 内部资源索引，常规上层代码不需要手动构造。
- `ResourceInfo.h`：资源条目元信息、资源内容版本和持久化策略。
- `ResourceLibrary.*`：Scene 级资源库入口，提供 `resources.Load<T>(source)`、`resources.Get<T>(source)`、`resources.Unload(source)` 等 API，常规由 `scene.Resources()` 暴露。
- `ResourcePool.*`：线程安全资源池，作为 `CResourceLibrary` 的内部存储对象。
- `ResourcePoolAccess.h`：显式 `CResourcePool` 的高级访问重载，供测试、导入预览、临时工程或多工程并行处理使用。
- `IResourceLoader.h` / `ResourceLoadContext.h` / `ResourceLoadResult.h`：资源加载器抽象、加载上下文和加载结果。
- `ResourceLoaderRegistry.*`：资源加载器注册表，正式运行路径由 ProductRuntime 和 Scene 分别持有实例。默认 `CResourceLibrary` 只创建空的私有 registry，不回退静态全局实例。提供 `ICAX_REGISTER_RESOURCE_LOADER(ResourceClass, LoaderType)` 宏，按 C++ 资源类型查找 loader 并返回加载结果。
- `Resources.h`：资源系统普通入口头，不包含 `ResourcePool.h`。
- `ResourcesExport.h`：DLL 导出宏。
- `framework.h` / `pch.*` / `dllmain.cpp`：Visual Studio 动态库工程基础文件。
