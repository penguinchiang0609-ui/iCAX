# Resources

`Resources` 是 framework 层的资源系统项目。上层用资源路径或来源字符串定位资源，路径本身就是资源键，适合承载第三方模型、图片、文本、材质、渲染缓存等对象。

它负责资源条目、资源对象池、轻量元信息、资源内容版本、持久化策略、manifest 查询、ResourceLoader 抽象、ResourceImporter/ResourceExporter 抽象和注册表。具体文件格式解析、GPU 上传或业务生命周期调度由插件或业务模块实现。

## 目录结构

- `ResourceKey.*`：Resources 内部资源索引，常规上层代码不需要手动构造。
- `ResourceInfo.h`：资源条目元信息、资源内容版本和持久化策略。
- `ResourceLibrary.*`：Scene 级资源库入口，提供 `resources.Load<T>(source)`、`resources.Import<T>(path)`、`resources.Export<T>(id, path)`、`resources.Get<T>(source)`、`resources.Unload(source)` 等 API，常规由 `scene.Resources()` 暴露。
- `ResourcePool.*`：线程安全资源池，作为 `CResourceLibrary` 的内部存储对象。
- `ResourcePoolAccess.h`：显式 `CResourcePool` 的高级访问重载，供测试、导入预览、临时工程或多工程并行处理使用。
- `IResourceLoader.h` / `ResourceLoadContext.h` / `ResourceLoadResult.h`：资源加载器抽象、加载上下文和加载结果。
- `ResourceImportExport.*`：通用资源导入/导出契约。导入器把外部文件或 URI 转成一个或多个 `Scene.Resources` 资源，并用 `Role` 标记 `source`、`geometry.brep`、`preview.mesh` 等用途；导出器执行相反方向。
- `BinaryResource.h`：通用二进制资源，用于把第三方文件原文嵌入项目。
- `ResourceLoaderRegistry.*`：资源加载器、导入器、导出器注册表，正式运行路径由 ProductRuntime 和 Scene 分别持有实例。默认 `CResourceLibrary` 只创建空的私有 registry，不回退静态全局实例。提供 `ICAX_REGISTER_RESOURCE_LOADER(ResourceClass, LoaderType)`、`ICAX_REGISTER_RESOURCE_IMPORTER(ImporterType)`、`ICAX_REGISTER_RESOURCE_EXPORTER(ExporterType)` 宏，也提供带稳定 provider ID 的 `*_PROVIDER(...)` 宏。
- `ResourceTypeName.h`：资源类型稳定名辅助。资源数据结构可声明 `inline static constexpr const char* kResourceTypeName`，manifest 通过该名称匹配资源类型。
- `Resources.h`：资源系统普通入口头，不包含 `ResourcePool.h`。
- `ResourcesExport.h`：DLL 导出宏。
- `framework.h` / `pch.*` / `dllmain.cpp`：Visual Studio 动态库工程基础文件。

## 导入导出扩展

资源导入导出遵守开闭原则：framework 不认识 STEP、IGES、PNG、SDF 等具体格式，只定义 `IResourceImporter` / `IResourceExporter`。插件实现接口并通过宏注册，ProductRuntime 加载产品 manifest 中的 DLL 后，会按模块路径把注册回放到当前产品自己的资源注册表。

推荐上层调用只表达目标类型和外部路径：

```cpp
iCAX::Resource::CResourceImportRequest request;
request.SourcePath = "D:/part.step";
request.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
request.Options["tolerance"] = "0.001";

auto brep = scene.Resources().Import<iCAX::GeometryData::BRepModel>(request);
if (!brep)
{
    throw std::runtime_error("CAD import failed");
}
```

需要读取导入器返回的附带资源时，传出完整结果：

```cpp
iCAX::Resource::CResourceImportResult result;
auto brep = scene.Resources().Import<iCAX::GeometryData::BRepModel>(request, &result);
if (!result.IsOK())
{
    throw std::runtime_error(result.Error);
}
```

导入结果的 `PrimaryResourceID` 表示主资源，`Items` 表示本次导入产生的所有资源。产品代码应按 `Role` 获取自己需要的资源，例如 CAD 导入器通常返回 `source` 和 `geometry.brep`，CAM 产品再基于 `geometry.brep` 生成自己的拓扑索引或刀路数据。

产品 manifest 可以按资源类型、格式、扩展名、provider 和 DLL 路径选择具体实现。比如同样是 STEP，可以让当前产品走 OCC，也可以换成另一个 DLL：

```json
{
  "backend": {
    "resources": {
      "handlers": [
        {
          "kind": "importer",
          "resourceType": "geometry.brep",
          "formatId": "cad.step",
          "extensions": [".step", ".stp"],
          "module": "../../iCAX-Plugins/cad/OpenCascadeResourceImport/${Platform}/${Configuration}/OpenCascadeResourceImport.dll",
          "provider": "occ.opencascade",
          "priority": 100
        }
      ]
    }
  }
}
```

运行期选择流程：

1. `ProductRuntime` 按 manifest 加载 `backend.resources.handlers[].module` 中声明的 DLL。
2. DLL 静态初始化时通过注册宏把 importer/exporter/loader 注册动作写入 `CResourceLoaderRegistrationCatalog`。
3. `ProductRuntime` 只把已加载模块路径对应的注册动作回放到当前产品和当前 Scene 的 `CResourceLoaderRegistry`。
4. `ResourceLibrary::Import<T>` / `Export<T>` 把 `T::kResourceTypeName` 写入请求。
5. `CResourceLoaderRegistry` 优先按 manifest 规则匹配 `kind + resourceType + formatId + extension + provider/module + priority`，再调用处理器的 `CanImport/CanExport/CanLoad` 做最终确认。

插件注册建议使用稳定 provider ID：

```cpp
class COpenCascadeResourceImporter final : public iCAX::Resource::IResourceImporter
{
    // ...
};

ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER("occ.opencascade", COpenCascadeResourceImporter)
```

如果临时不指定 provider，也可以使用 `ICAX_REGISTER_RESOURCE_IMPORTER(CMyImporter)`，此时 provider ID 默认是类名字符串，不建议写入正式产品 manifest。
