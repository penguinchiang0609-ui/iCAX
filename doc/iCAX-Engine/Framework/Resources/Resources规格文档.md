# Resources 规格文档

## 1. 定位

`Resources` 是 framework 层的工程资源系统。

它解决的问题是：模型、图片、文字、材质、渲染缓存等资源可以统一通过资源路径或来源字符串访问。Database、CommandHandler、Behaviour 或业务代码不需要直接持有资源对象，也不需要手动构造资源 key。

核心模型：

```text
Resources
  Source(path or uri)
    ResourceKey(internal)
      ResourceInfo
      ResourceObject(optional)
  ResourceLoaderRegistry
    IResourceLoader
```

`Resources` 负责资源条目、资源对象池、资源元信息、持久化策略、manifest 查询、ResourceLoader 抽象和 loader 注册表。它不负责具体文件格式解析、GPU 上传、远程下载或工程文件写入，这些应由后续 ProjectStorage、ResourceLoader 实现或业务插件完成。

## 2. 核心概念

### 2.1 ResourceKey

`CResourceKey` 是资源系统内部索引。上层业务不需要手动构造它，常规访问只传资源路径或来源字符串：

```cpp
struct CResourceKey
{
    std::string Source;
};
```

`Source` 原样就是资源身份。资源格式、语义类型、持久化方式等信息放在 `CResourceInfo` 或 loader 逻辑中，不参与资源身份。

需要注意的是，`Source` 是精确字符串键；路径大小写、分隔符、相对路径规范化等属于调用方或后续路径策略需要统一的规则。

### 2.2 ResourceInfo

`CResourceInfo` 是资源条目的轻量描述信息：

```cpp
enum class EResourcePersistenceMode : uint8_t
{
    RuntimeOnly,
    Embedded,
    External
};

struct CResourceInfo
{
    CResourceKey Key;
    std::string Name;
    std::string Source;
    std::string ContentHash;
    uint64_t nVersion;
    uint64_t nSize;
    uint32_t nFlags;
    EResourcePersistenceMode Persistence;
    std::map<std::string, std::string> Metadata;
};
```

持久化策略：

- `RuntimeOnly`：运行时资源，不进入工程文件 manifest。
- `Embedded`：资源需要内嵌保存到工程文件或工程包。
- `External`：资源以外部引用形式保存，工程文件记录引用信息。

`nVersion` 是资源内容版本，类型为 `uint64_t`。它不是文件格式版本，也不是 loader 版本，而是当前资源对象内容的运行期版本。运行期生成的 mesh、渲染缓存、识别结果等资源发生内容变化时，应调用 `Touch(source)` 递增版本；这个版本可以直接作为 PDO 的 `dataVersion` 使用，避免未变化资源每帧重复序列化。

### 2.3 ResourceLibrary

`CResourceLibrary` 是 Scene 级资源入口。每个 Scene 持有自己的 `CResourceLibrary`，内部独占一个资源池，因此多个 Scene 同时存在时资源天然隔离。

上层业务常规使用方式是：

```cpp
auto model = scene.Resources().Load<ModelResource>("D:/assets/robot.fbx");
auto cached = scene.Resources().Get<ModelResource>("D:/assets/robot.fbx");

scene.Resources().Unload("D:/assets/robot.fbx");
scene.Resources().Remove("D:/assets/robot.fbx");

auto manifest = scene.Resources().GetManifest();
```

在没有 Project 封装的测试或局部代码中，也可以直接创建资源库。默认构造的 `CResourceLibrary` 会创建一个空的私有 loader registry，不会使用全局 loader：

```cpp
CResourceLibrary resources;
auto missing = resources.Load<ModelResource>("D:/assets/robot.fbx"); // 没有注入 loader，返回 nullptr
```

需要加载资源时应显式注入 registry：

```cpp
auto loaders = std::make_shared<CResourceLoaderRegistry>();
loaders->RegisterLoader(typeid(ModelResource), std::make_shared<FbxResourceLoader>());

CResourceLibrary resources(loaders);
auto model = resources.Load<ModelResource>("D:/assets/robot.fbx");
```

`CResourceLibrary` 提供：

```cpp
template <typename T>
std::shared_ptr<T> Load(std::string source, CResourceInfo info = {}, std::map<std::string, std::string> options = {});

template <typename T>
void Set(std::string source, std::shared_ptr<T> resource, CResourceInfo info = {});

template <typename T>
bool TryAdd(std::string source, std::shared_ptr<T> resource, CResourceInfo info = {});

template <typename T>
std::shared_ptr<T> Get(std::string source);

void Register(std::string source, CResourceInfo info = {});
bool TryRegister(std::string source, CResourceInfo info = {});
bool Contains(std::string source);
bool HasObject(std::string source);
uint64_t GetVersion(std::string source);
uint64_t Touch(std::string source);
bool Unload(std::string source);
bool Remove(std::string source);
void Clear();
size_t Count();
std::optional<CResourceInfo> GetInfo(std::string source);
bool UpdateInfo(std::string source, CResourceInfo info);
std::vector<CResourceKey> GetKeys() const;
std::vector<CResourceInfo> GetInfos() const;
std::vector<CResourceInfo> GetManifest(bool includeRuntimeOnly = false);
```

### 2.4 ResourcePool

`CResourcePool` 是线程安全资源池。它是 `CResourceLibrary` 背后的存储对象。普通业务代码不需要直接操作它，也不应该把它作为 Project API 暴露出去；运行期应通过 Scene 的 `CResourceLibrary` 访问资源。

普通业务代码包含：

```cpp
#include <Resources/Resources.h>
```

需要直接验证资源池行为或构造底层测试时，再包含：

```cpp
#include <Resources/ResourcePoolAccess.h>
```

`CResourcePool` 的底层能力包括：

```cpp
void Register(CResourceInfo info);
bool TryRegister(CResourceInfo info);

void Set<T>(std::string source, std::shared_ptr<T> resource, CResourceInfo info = {});
bool TryAdd<T>(std::string source, std::shared_ptr<T> resource, CResourceInfo info = {});
std::shared_ptr<T> Get<T>(std::string source) const;

bool Contains(CResourceKey key) const;
bool HasObject(CResourceKey key) const;
uint64_t GetVersion(CResourceKey key) const;
uint64_t Touch(CResourceKey key);
bool Unload(CResourceKey key);
bool Remove(CResourceKey key);
void Clear();
size_t Count() const;

std::optional<CResourceInfo> GetInfo(CResourceKey key) const;
bool UpdateInfo(CResourceKey key, CResourceInfo info);
std::vector<CResourceKey> GetKeys() const;
std::vector<CResourceInfo> GetInfos() const;
std::vector<CResourceInfo> GetManifest(bool includeRuntimeOnly = false) const;
```

`Register` 只登记资源条目，不要求资源对象已经加载。  
`Set` 会替换同 key 的旧资源对象和元信息。  
`Touch` 只在资源内容变化时调用，递增并返回资源内容版本；单纯修改 `Name`、`Persistence`、`Metadata` 不应调用。
`Unload` 释放资源池持有的对象引用，但保留资源条目和元信息。  
`GetManifest` 默认只返回 `Embedded` 和 `External` 资源条目。

### 2.5 ResourceLoader

`IResourceLoader` 是资源加载器抽象：

```cpp
class IResourceLoader
{
public:
    virtual bool CanLoad(const CResourceLoadContext& context) const = 0;
    virtual CResourceLoadResult Load(const CResourceLoadContext& context) = 0;
};
```

framework 只提供抽象和注册表。具体 `FbxResourceLoader`、`PngResourceLoader`、`StepResourceLoader` 等实现应放在插件或业务模块里。

### 2.6 ResourceLoaderRegistry

`CResourceLoaderRegistry` 按资源类型注册 loader。正式运行路径中，ProductRuntime 持有产品级 registry；创建 Scene 时会为该 Scene 创建独立 registry，并按产品模块路径回放 ResourceLoader 注册动作。Scene 的 `CResourceLibrary` 只使用自己的 Scene 级 registry 加载资源。

```cpp
bool RegisterLoader(const std::type_info& resourceType, std::shared_ptr<IResourceLoader> loader);
template <typename TResource>
bool RegisterLoader(std::shared_ptr<IResourceLoader> loader);
std::vector<std::shared_ptr<IResourceLoader>> GetLoadersFor(const std::type_info& resourceType) const;
std::shared_ptr<IResourceLoader> FindLoaderFor(CResourceLoadContext context) const;
CResourceLoadResult LoadResource(CResourceLoadContext context);
```

业务模块和插件通常在 loader 的 `.cpp` 中通过宏完成注册：

```cpp
ICAX_REGISTER_RESOURCE_LOADER(ModelResource, FbxResourceLoader)
```

宏不会直接写入某个全局资源池，而是把注册动作记录到 `CResourceLoaderRegistrationCatalog`。ProductRuntime 加载产品模块后，按模块路径把对应注册动作回放到产品级 registry；创建 Scene 时再回放到 Scene 级 registry。

注册表按 C++ 资源类型匹配 loader，也就是 `typeid(ModelResource)`。文件后缀或来源格式不是资源类型，应该由 loader 的 `CanLoad` 判断：

```cpp
bool FbxResourceLoader::CanLoad(const CResourceLoadContext& context) const
{
    return EndsWith(context.Source, ".fbx");
}
```

上层业务代码通常不直接操作注册表，只调用 `scene.Resources().Load<T>(source)`。

`CResourceLoaderRegistry::LoadResource` 只负责调用 loader 并返回 `CResourceLoadResult`，不直接写入资源池。写入 `CResourceLibrary` 内部资源池或显式底层池由访问层完成：

- loader 返回资源对象时，访问层写入资源对象和元信息。
- loader 只返回元信息时，访问层只登记资源条目。

### 2.7 CResourceLibrary::Load

`CResourceLibrary::Load<T>` 是上层最常用的资源访问入口：

```cpp
auto resource = scene.Resources().Load<T>(source);
```

它的语义是：

```text
基于 source 生成内部 ResourceKey，同一个 source 只对应一个资源身份
先从当前 Scene 的 CResourceLibrary 获取 T
  获取成功 -> 直接返回
  获取失败且内部 key 下已有其他运行时对象 -> 返回 nullptr
  未加载 -> 通过当前 ResourceLibrary 绑定的 Scene 级 ResourceLoaderRegistry 按 typeid(T) 查找 loader
    加载成功 -> 写入当前 Scene 的资源库并返回 T
    加载失败 -> 返回 nullptr
```

如果 `CResourceLibrary` 是裸创建且没有注入 registry，则只拥有一个空的私有 registry，不会回退到静态全局 registry。

底层资源池仍然使用 `CResourceKey` 做索引，但这是 Resources 内部机制，不作为上层加载资源时必须提供的额外 key。

## 3. 使用样例

### 3.1 通过路径加载模型资源

在 `FbxResourceLoader.cpp` 中注册 loader：

```cpp
using namespace iCAX::Resource;

ICAX_REGISTER_RESOURCE_LOADER(ModelResource, FbxResourceLoader)
```

业务代码按路径加载：

```cpp
using namespace iCAX::Resource;

auto model = scene.Resources().Load<ModelResource>("D:/assets/robot.fbx");
auto sameModel = scene.Resources().Load<ModelResource>("D:/assets/robot.fbx");
```

第二次调用会直接从该 Scene 的资源库返回已有对象，不会重复调用 loader。

### 3.2 加载外部资源并标记持久化策略

```cpp
CResourceInfo info;
info.Name = "RobotArm";
info.Persistence = EResourcePersistenceMode::External;

auto model = scene.Resources().Load<ModelResource>(
    "D:/assets/robot.fbx",
    info);
```

### 3.3 工程恢复后按需加载资源

```cpp
auto model = scene.Resources().Load<ModelResource>("D:/assets/robot.fbx");
```

工程打开时只要恢复了资源来源，后续访问仍然使用同一个路径字符串。

### 3.4 获取工程资源清单

```cpp
auto manifest = scene.Resources().GetManifest();
for (const auto& item : manifest)
{
    if (item.IsEmbedded())
    {
        // ProjectStorage 后续负责把资源内容写入工程包。
    }
    else if (item.IsExternal())
    {
        // ProjectStorage 后续负责记录外部引用。
    }
}
```

### 3.5 运行期生成资源与 PDO 版本

```cpp
auto mesh = std::make_shared<MeshResource>();
scene.Resources().Set<MeshResource>("runtime://preview/mesh", mesh);

// 识别或仿真更新了 mesh 内容。
mesh->UpdateFromSimulation(result);
auto version = scene.Resources().Touch("runtime://preview/mesh");

void* writeData = nullptr;
auto& slot = scene.PDOHub().GetSlot(previewMeshPDO);
if (slot.TryGetWriteDataIfNewer(version, writeData))
{
    SerializeMeshToPDO(*mesh, writeData);
    slot.MarkWriteReady(version);
}
```

如果只修改资源的显示名称、持久化策略或扩展 metadata，不调用 `Touch`。

## 4. 类型约定

长期保存或跨模块传递的资源引用保存 `Source` 和 `CResourceInfo` 中的持久化信息。路径加载入口不会要求调用方额外保存 `ResourceKey`，也不要求调用方持有 `CResourcePool`。

C++ 资源类型不参与资源身份。`typeid(T)` 只用于 `ResourceLoaderRegistry` 查找 loader，以及 `CResourceLibrary::Get<T>` / `CResourcePool::Get<T>` 取对象时做运行时类型校验。

## 5. 线程安全

`CResourceLibrary`、`CResourcePool` 和 `CResourceLoaderRegistry` 的公开接口是线程安全的。

资源池保护的是内部映射表，不保护资源对象自身。多个模块取到同一个 `shared_ptr<T>` 后，如果要修改资源对象，仍需要资源对象自己提供同步策略。
