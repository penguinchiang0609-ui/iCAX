# Resources

`Resources` 是 framework 层的资源系统项目。资源 URL 本身就是资源键，适合承载模型、图片、材质、几何、刀路和渲染缓存等内容。

它负责资源条目、资源对象池、轻量元信息、资源内容版本、REST 风格直接访问、持久化策略、manifest 查询、ResourceLoader 抽象、ResourceImporter/ResourceExporter 抽象和注册表。具体业务 Schema 解释、GPU 上传或业务生命周期调度由插件或业务模块实现。

结构化 Resource 可以用 Google FlatBuffer 保存不可变内容，资源副本共享同一份 byte owner；业务资源类型仍负责自己的 schema、file identifier 和生成代码，并在读取前执行 `Verifier`。STEP、PNG、压缩包等外部原始格式继续使用 `CBinaryResource` 保存原文，不强制转换成 FlatBuffer。

## 目录结构

- `ResourceKey.*`：Resources 内部资源索引，常规上层代码不需要手动构造。
- `ResourceInfo.h`：资源条目元信息、资源内容版本和持久化策略。
- `ResourceURL.*`：资源 GUID、Application/Product/Project/Scene 四级作用域，以及规范 URL 的构造、分配、解析和校验。
- `ResourceAccess.*`：不经过 SDO 邮件的直接资源 API，定义 `HEAD/GET/POST/PUT/DELETE/OPTIONS`、Headers、状态码、ETag 和条件请求。
- `ResourceLibrary.*`：各级资源库入口，既提供 `Load<T>/Get<T>` 等 C++ 对象访问，也提供按 URL 的 `Head/Get/Post/Put/Delete/Options/Request`。
- `ResourcePool.*`：线程安全资源池，作为 `CResourceLibrary` 的内部存储对象。
- `ResourcePoolAccess.h`：显式 `CResourcePool` 的高级访问重载，供测试、导入预览、临时工程或多工程并行处理使用。
- `IResourceLoader.h` / `ResourceLoadContext.h` / `ResourceLoadResult.h`：资源加载器抽象、加载上下文和加载结果。
- `ResourceImportExport.*`：通用资源导入/导出契约。导入器把外部文件或 URI 转成一个或多个 `Scene.Resources` 资源，并用 `Role` 标记 `source`、`geometry.brep`、`preview.mesh` 等用途；导出器执行相反方向。
- `BinaryResource.h`：通用二进制资源，用于把第三方文件原文嵌入项目。
- `FlatBufferResource.*` / `ResourceFlatBuffer.h`：不可变 FlatBuffer byte owner，以及标准 Google FlatBuffer 构造、校验和零拷贝 root 读取辅助。
- `ResourceLoaderRegistry.*`：资源加载器、导入器、导出器注册表，正式运行路径由 ProductRuntime 和 Scene 分别持有实例。默认 `CResourceLibrary` 只创建空的私有 registry，不回退静态全局实例。提供 `ICAX_REGISTER_RESOURCE_LOADER(ResourceClass, LoaderType)`、`ICAX_REGISTER_RESOURCE_IMPORTER(ImporterType)`、`ICAX_REGISTER_RESOURCE_EXPORTER(ExporterType)` 宏，也提供带稳定 provider ID 的 `*_PROVIDER(...)` 宏。
- `ResourceTypeName.h`：资源类型稳定名辅助。资源数据结构可声明 `inline static constexpr const char* kResourceTypeName`，manifest 通过该名称匹配资源类型。
- `Resources.h`：资源系统普通入口头，不包含 `ResourcePool.h`。
- `ResourcesExport.h`：DLL 导出宏。
- `framework.h` / `pch.*` / `dllmain.cpp`：Visual Studio 动态库工程基础文件。

## 直接资源 API

资源内容属于数据面，不通过 SDO 邮件传输。后端可以直接调用：

```cpp
const auto resourceID = scene.Resources().AllocateResourceID();
const auto resourceURL = scene.Resources().MakeResourceURL(resourceID);
auto response = scene.Resources().Get(
    resourceURL,
    {
        { "Accept", "application/vnd.icax.flatbuffer" },
        { "If-None-Match", "\"icax-v42\"" }
    });
```

前端通过绑定 Scene 的 `ResourceClient` 使用同样语义：

```js
const response = await sceneProxy.resources.get(resourceUrl, {
  headers: {
    Accept: "application/vnd.icax.flatbuffer",
  },
});

const schemaVersion = response.headers.get("ICAX-Schema-Version");
const bytes = await response.arrayBuffer();
```

### URL 与 GUID

`ResourceID` 是非空 GUID。URL 同时编码资源的所有权作用域，采用以下规范形式：

```text
resource://{applicationID}/resources/{resourceID}
resource://{applicationID}/products/{productID}/resources/{resourceID}
resource://{applicationID}/products/{productID}/projects/{projectID}/resources/{resourceID}
resource://{applicationID}/products/{productID}/projects/{projectID}/scenes/{sceneID}/resources/{resourceID}
```

去掉最后的 `{resourceID}` 就得到对应作用域的 collection URL。`projectID`、`sceneID` 和 `resourceID` 都是规范小写 GUID；ApplicationID/ProductID 是 URL 安全的稳定 ID。URL 不允许查询串、fragment、空路径段、`..` 或非规范 GUID，避免同一资源出现多个身份。

调用方可以在写入前自行分配 GUID，随后用已知 URL 幂等创建：

```cpp
const auto resourceURL = scene.Resources().AllocateResourceURL();
const CFlatBufferResource body(flatBufferBytes);
const auto response = scene.Resources().Put(
    resourceURL,
    body,
    {
        { "Content-Type", "application/vnd.icax.flatbuffer" },
        { "If-None-Match", "*" }
    });
```

也可以对 collection `POST`，让资源库分配 GUID；创建成功返回 `201`，新 URL 位于 `Location`。这遵循标准 HTTP 语义：`POST collection` 是服务端分配身份的创建，`PUT resource URL` 是对调用方已知身份的创建或完整替换。

公开方法语义：

- `HEAD`：返回资源类型、Schema 版本、FlatBuffer identifier、长度、资源版本和 ETag，不返回 Body；即使正文尚未加载，也可以直接从 manifest 回答。
- `GET`：返回同样的 Headers 和 FlatBuffer Body，支持 `If-None-Match`。
- `POST`：只作用于 collection URL，由资源库分配新 GUID 并创建资源，返回 `201` 和 `Location`。
- `PUT`：创建或完整替换资源，原子递增资源版本，支持 `If-Match` 和 `If-None-Match: *`。
- `DELETE`：移除 URL 的当前入口并归档当前版本，支持 `If-Match`；已锁定版本的引用仍可读取。
- `OPTIONS`：返回 `Allow` 和可写入媒体类型。

结构化写入应使用 `application/vnd.icax.flatbuffer` 或明确的
`application/*+flatbuffer(s)` 媒体类型；API 不会把模糊的
`application/octet-stream` 猜成 FlatBuffer。调用方可先用 `HEAD`
检查 `Content-Type`，再根据 `ICAX-Schema-Version` 和
`ICAX-FlatBuffer-Identifier` 选择对应的生成代码与翻译器。

资源更新成功后由 ResourcePool 生成严格递增的 `ICAX-Resource-Version`。`ETag` 是当前 URL 下的版本验证器；`ICAX-Schema-Version` 是业务 Layout 版本，两者不能混用。

`CResourceInfo::ResourceID` 保存 URL 末段的 GUID；`CResourceInfo::Source` 只保存原始文件或外部 URI。资源 URL 是身份，Source 是来源，两者不互相替代。

## 历史版本与撤销

通过版本化 `PUT` 替换资源时，ResourcePool 不会丢弃旧版本；版本化 `DELETE` 也只删除当前入口，删除前的内容仍可按 URL + version 读取。即使资源删除后重新创建，版本号也会从该 URL 的历史最高版本继续递增，不会复用旧版本号。

组件和文档撤销栈只需要保存资源 URL 与版本号。读取历史版本时在 `GET` 或 `HEAD` 请求中携带：

```text
ICAX-Resource-Version: 7
```

C++ 也可以直接调用：

```cpp
auto response = scene.Resources().Get(resourceUrl, 7);
auto brep = scene.Resources().Get<BRepModel>(resourceUrl, 7);

CResourceInfo stored;
auto result = scene.Resources().PutVersioned<BRepModel>(
    resourceUrl,
    editedBRep,
    {},
    EResourceVersionCondition::VersionMatches,
    7,
    &stored);
```

历史版本采用以下存储策略：

1. `CFlatBufferResource` 和 `CBinaryResource` 内置冷存储编解码器。版本过期后立即原子写入本 ResourcePool 独占的 temp 子目录，并释放池对旧对象的强引用。
2. 历史版本读取时从磁盘反序列化；ResourcePool 只保存弱缓存。调用方不再使用该对象后，内存可以自然释放。
3. BREP 等业务对象由插件通过 `ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC("geometry.brep", BRepModel, MakeBRepCodec)` 自动登记稳定 `ResourceTypeID` 与编解码规则；ProductRuntime 只向实际加载该插件的产品 ResourceLibrary 回放。该规则同时用于项目文件和历史版本冷存储。动态场景仍可直接调用 `RegisterPersistenceCodec`；只需要冷存储、不进入项目文件的运行时类型仍可使用 `RegisterVersionCodec`。
4. 如果类型没有编解码器、序列化失败或磁盘不可写，ResourcePool 会回退到内存强持有。可靠性优先于缓存预算，绝不会静默丢弃仍可能用于撤销的版本。
5. ResourcePool 不提供单版本 `Discard` 或硬 `Remove`。历史版本在项目会话期间完整保留，避免撤销栈或资源依赖被物理清理破坏。

独立使用 `CResourceLibrary` 时，默认冷存储目录位于系统 temp 下的 `iCAX/ResourceVersions/pool-*`。正式 Application 中，每个 Scene 从 `ApplicationRuntimeConfig.Paths.ResourceVersionDirectory` 取得应用级根目录；该参数为空时使用 `Paths.TempDirectory/ResourceVersions`，相对路径以 `Paths.InstallDirectory` 为基准。每个 ResourcePool 再在根目录下创建独占的 `pool-*` 会话子目录，正常析构时只清理自己的子目录。

该目录用于当前 Scene/项目会话的撤销，不是跨进程持久化。项目关闭时，Scene 清空资源库，ResourcePool 会删除自己的整个 `pool-*` 目录；析构时还会再次兜底清理。若要求关闭并重新打开项目后仍能撤销，相关版本必须进入项目包、autosave 或其他持久化版本库。

## 版本锁定的资源依赖

一个资源版本可以通过 `CResourceInfo::Dependencies` 引用其他不可变资源版本。依赖始终保存完整的 `{URL, Version}`，不支持隐式跟随某个 URL 的最新版本：

```cpp
CResourceInfo materialInfo;
materialInfo.Dependencies = {
    { textureUrl, textureVersion }
};

CResourceInfo storedMaterial;
resources.PutVersioned<MaterialResource>(
    materialUrl,
    material,
    materialInfo,
    EResourceVersionCondition::VersionMatches,
    oldMaterialVersion,
    &storedMaterial);
```

更新纹理只创建新的纹理版本，不会修改既有材质版本。材质明确采用新纹理后再创建新的材质版本；未变化的依赖继续引用原来的 `{URL, Version}`，因此新旧资源树可以共享同一个不可变节点和正文。

如果父资源正文没有变化，只是采用了新的子资源版本，可以直接重绑定依赖。该操作复用父资源的不可变对象，只生成新的父资源版本和依赖元数据：

```cpp
CResourceInfo materialV6;
resources.RebindDependencyVersioned(
    { materialUrl, 5 },
    { textureUrl, 7 },
    { textureUrl, 8 },
    &materialV6);
```

资源池在提交版本时校验全部依赖已经存在，拒绝版本内修改依赖、缺失依赖、环依赖，以及持久化资源对 RuntimeOnly 资源的强依赖。资源池不暴露单版本物理删除接口，普通 `DELETE` 只归档当前版本。

保存项目时，由组件和文档对象提供根引用，再收集依赖闭包：

```cpp
const auto closure = resources.CollectReachable(componentResourceRoots);
if (!closure.IsComplete())
{
    // Missing 中包含损坏的根引用或依赖，不能提交不完整项目文件。
}
// closure.Resources 已按依赖优先顺序去重，可用于生成项目资源清单。
```

依赖元数据和历史版本的 `CResourceInfo` 常驻内存；大尺寸资源正文仍按前述策略进入磁盘冷存储。资源依赖图只允许不可变的版本锁定边，不执行自动级联更新。

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
