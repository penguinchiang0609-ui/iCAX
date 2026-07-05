# Resources

`Resources` 是 framework 层的资源系统项目。上层用资源路径或来源字符串定位资源，路径本身就是资源键，适合承载第三方模型、图片、文本、材质、渲染缓存等对象。

它负责资源条目、资源对象池、轻量元信息、资源内容版本、持久化策略、manifest 查询、ResourceLoader 抽象、ResourceImporter/ResourceExporter 抽象和注册表。具体文件格式解析、GPU 上传或业务生命周期调度由插件或业务模块实现。

## 目录结构

- `ResourceKey.*`：Resources 内部资源索引，常规上层代码不需要手动构造。
- `ResourceInfo.h`：资源条目元信息、资源内容版本和持久化策略。
- `ResourceLibrary.*`：Scene 级资源库入口，提供 `resources.Load<T>(source)`、`resources.Get<T>(source)`、`resources.Unload(source)` 等 API，常规由 `scene.Resources()` 暴露。
- `ResourcePool.*`：线程安全资源池，作为 `CResourceLibrary` 的内部存储对象。
- `ResourcePoolAccess.h`：显式 `CResourcePool` 的高级访问重载，供测试、导入预览、临时工程或多工程并行处理使用。
- `IResourceLoader.h` / `ResourceLoadContext.h` / `ResourceLoadResult.h`：资源加载器抽象、加载上下文和加载结果。
- `ResourceImportExport.*`：通用资源导入/导出契约。导入器把外部文件或 URI 转成一个或多个 `Scene.Resources` 资源，并用 `Role` 标记 `source`、`geometry.brep`、`preview.mesh` 等用途；导出器执行相反方向。
- `BinaryResource.h`：通用二进制资源，用于把第三方文件原文嵌入项目。
- `ResourceLoaderRegistry.*`：资源加载器、导入器、导出器注册表，正式运行路径由 ProductRuntime 和 Scene 分别持有实例。默认 `CResourceLibrary` 只创建空的私有 registry，不回退静态全局实例。提供 `ICAX_REGISTER_RESOURCE_LOADER(ResourceClass, LoaderType)`、`ICAX_REGISTER_RESOURCE_IMPORTER(ImporterType)`、`ICAX_REGISTER_RESOURCE_EXPORTER(ExporterType)` 宏。
- `Resources.h`：资源系统普通入口头，不包含 `ResourcePool.h`。
- `ResourcesExport.h`：DLL 导出宏。
- `framework.h` / `pch.*` / `dllmain.cpp`：Visual Studio 动态库工程基础文件。

## 导入导出扩展

资源导入导出遵守开闭原则：framework 不认识 STEP、IGES、PNG、SDF 等具体格式，只定义 `IResourceImporter` / `IResourceExporter`。插件实现接口并通过宏注册，ProductRuntime 加载产品 manifest 中的 DLL 后，会按模块路径把注册回放到当前产品自己的资源注册表。

上层典型调用：

```cpp
iCAX::Resource::CResourceImportRequest request;
request.SourcePath = "D:/part.step";
request.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
request.Options["tolerance"] = "0.001";

auto result = scene.Resources().Import(request);
if (!result.IsOK())
{
    throw std::runtime_error(result.Error);
}
```

导入结果的 `PrimaryResourceID` 表示主资源，`Items` 表示本次导入产生的所有资源。产品代码应按 `Role` 获取自己需要的资源，例如 CAD 导入器通常返回 `source` 和 `geometry.brep`，CAM 产品再基于 `geometry.brep` 生成自己的拓扑索引或刀路数据。
