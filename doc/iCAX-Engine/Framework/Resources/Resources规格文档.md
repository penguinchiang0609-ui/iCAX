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

### 2.3 ResourceLibrary

`CResourceLibrary` 是 Project 级资源入口。每个 Project 持有自己的 `CResourceLibrary`，内部独占一个资源池，因此多个 Project 同时打开时资源天然隔离。

上层业务常规使用方式是：

```cpp
auto model = project.Resources().Load<ModelResource>("D:/assets/robot.fbx");
auto cached = project.Resources().Get<ModelResource>("D:/assets/robot.fbx");

project.Resources().Unload("D:/assets/robot.fbx");
project.Resources().Remove("D:/assets/robot.fbx");

auto manifest = project.Resources().GetManifest();
```

在没有 Project 封装的测试或局部代码中，也可以直接创建资源库：

```cpp
CResourceLibrary resources;
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

`CResourcePool` 是线程安全资源池。它是 `CResourceLibrary` 背后的存储对象。普通业务代码不需要直接操作它，也不应该把它作为 Project API 暴露出去。

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

`CResourceLoaderRegistry` 按资源类型注册 loader。它既支持实例注册表，也保留静态全局注册表作为裸用 `CResourceLibrary` 或底层测试的回退入口。正式运行路径中，`ApplicationHost` 持有应用级 registry，`ProductRuntime` 把产品模块中的 loader 注册回放进去，Project 的 `CResourceLibrary` 使用该应用级 registry 加载资源。

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

宏不会直接写入某个全局资源池，而是把注册动作记录到 `CResourceLoaderRegistrationCatalog`。ApplicationHost 创建应用级 registry 后，ProductRuntime 按模块路径把对应注册动作回放到该 registry。

注册表按 C++ 资源类型匹配 loader，也就是 `typeid(ModelResource)`。文件后缀或来源格式不是资源类型，应该由 loader 的 `CanLoad` 判断：

```cpp
bool FbxResourceLoader::CanLoad(const CResourceLoadContext& context) const
{
    return EndsWith(context.Source, ".fbx");
}
```

上层业务代码通常不直接操作注册表，只调用 `project.Resources().Load<T>(source)`。

`CResourceLoaderRegistry::LoadResource` 只负责调用 loader 并返回 `CResourceLoadResult`，不直接写入资源池。写入 `CResourceLibrary` 内部资源池或显式底层池由访问层完成：

- loader 返回资源对象时，访问层写入资源对象和元信息。
- loader 只返回元信息时，访问层只登记资源条目。

### 2.7 CResourceLibrary::Load

`CResourceLibrary::Load<T>` 是上层最常用的资源访问入口：

```cpp
auto resource = project.Resources().Load<T>(source);
```

它的语义是：

```text
基于 source 生成内部 ResourceKey，同一个 source 只对应一个资源身份
先从当前 Project 的 CResourceLibrary 获取 T
  获取成功 -> 直接返回
  获取失败且内部 key 下已有其他运行时对象 -> 返回 nullptr
  未加载 -> 通过当前 ResourceLibrary 绑定的 ResourceLoaderRegistry 按 typeid(T) 查找 loader
    加载成功 -> 写入当前 Project 的资源库并返回 T
    加载失败 -> 返回 nullptr
```

如果 `CResourceLibrary` 是裸创建且没有注入 registry，则会回退到静态全局 registry。该能力主要服务测试和局部工具代码，正式 Project 路径应使用 ApplicationHost 注入的应用级 registry。

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

auto model = project.Resources().Load<ModelResource>("D:/assets/robot.fbx");
auto sameModel = project.Resources().Load<ModelResource>("D:/assets/robot.fbx");
```

第二次调用会直接从该 Project 的资源库返回已有对象，不会重复调用 loader。

### 3.2 加载外部资源并标记持久化策略

```cpp
CResourceInfo info;
info.Name = "RobotArm";
info.Persistence = EResourcePersistenceMode::External;

auto model = project.Resources().Load<ModelResource>(
    "D:/assets/robot.fbx",
    info);
```

### 3.3 工程恢复后按需加载资源

```cpp
auto model = project.Resources().Load<ModelResource>("D:/assets/robot.fbx");
```

工程打开时只要恢复了资源来源，后续访问仍然使用同一个路径字符串。

### 3.4 获取工程资源清单

```cpp
auto manifest = project.Resources().GetManifest();
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

## 4. 类型约定

长期保存或跨模块传递的资源引用保存 `Source` 和 `CResourceInfo` 中的持久化信息。路径加载入口不会要求调用方额外保存 `ResourceKey`，也不要求调用方持有 `CResourcePool`。

C++ 资源类型不参与资源身份。`typeid(T)` 只用于 `ResourceLoaderRegistry` 查找 loader，以及 `CResourceLibrary::Get<T>` / `CResourcePool::Get<T>` 取对象时做运行时类型校验。

## 5. 线程安全

`CResourceLibrary`、`CResourcePool` 和 `CResourceLoaderRegistry` 的公开接口是线程安全的。

资源池保护的是内部映射表，不保护资源对象自身。多个模块取到同一个 `shared_ptr<T>` 后，如果要修改资源对象，仍需要资源对象自己提供同步策略。
