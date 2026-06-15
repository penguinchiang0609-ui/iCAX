# ResourcePool 规格文档

## 1. 定位

`ResourcePool` 是 foundation 层的进程内资源池。

它解决的问题是：模型、图片、文字、材质、渲染缓存等资源可以统一放入资源池，其他模块只需要保存资源类型和资源 ID，就能在需要时取回资源对象。

核心模型：

```text
ResourcePool
  ResourceKey(Type, ID)
    ResourceObject
    ResourceInfo
```

`ResourcePool` 不负责文件加载、格式解析、GPU 上传、远程下载或资源持久化。它只负责进程内对象索引和缓存。

## 2. 核心概念

### 2.1 ResourceKey

`CResourceKey` 是资源的稳定引用：

```cpp
struct CResourceKey
{
    std::string Type;
    iCAX::Data::uuid ID;
};
```

`Type` 是稳定资源类型，例如：

```text
render.mesh
render.material
render.texture2d
text.plain
image.rgba8
```

`ID` 是资源 UUID。同一个 `ID` 可以同时用于不同 `Type`，例如源网格 `render.mesh` 和 GPU 缓存 `render.glmesh` 可以共用同一个 ID，但它们的完整 key 不同。

### 2.2 ResourceInfo

`CResourceInfo` 是轻量描述信息：

```cpp
struct CResourceInfo
{
    CResourceKey Key;
    std::string Name;
    std::string Source;
    uint64_t nSize;
    uint32_t nFlags;
    std::map<std::string, std::string> Metadata;
};
```

它可以记录显示名、来源、字节大小、标志位和扩展元数据。`ResourceInfo` 不拥有资源对象。

### 2.3 ResourcePool

`CResourcePool` 是线程安全资源池，提供：

```cpp
void Set<T>(uuid id, std::shared_ptr<T> resource, std::string type, CResourceInfo info = {});
bool TryAdd<T>(uuid id, std::shared_ptr<T> resource, std::string type, CResourceInfo info = {});
std::shared_ptr<T> Get<T>(uuid id, std::string type) const;
bool Contains(CResourceKey key) const;
bool Remove(CResourceKey key);
void Clear();
size_t Count() const;
std::optional<CResourceInfo> GetInfo(CResourceKey key) const;
bool UpdateInfo(CResourceKey key, CResourceInfo info);
std::vector<CResourceKey> GetKeys(std::string type = {}) const;
std::vector<CResourceInfo> GetInfos(std::string type = {}) const;
```

`Set` 会替换同 key 的旧资源；`TryAdd` 在 key 已存在时返回 `false`。

`Get<T>` 会校验运行时 C++ 类型。如果 key 存在但资源对象不是 `T`，返回 `nullptr`。

## 3. 使用样例

### 3.1 注册和获取模型资源

```cpp
using namespace iCAX::Resource;

CResourcePool pool;
auto meshId = iCAX::Data::GenerateNewUUID();

auto mesh = std::make_shared<MeshResource>();
pool.Set<MeshResource>(meshId, mesh, "render.mesh");

CResourceKey meshRef{ "render.mesh", meshId };
auto loadedMesh = pool.Get<MeshResource>(meshRef.ID, meshRef.Type);
```

上层组件或数据库字段只需要保存：

```cpp
CResourceKey meshRef{ "render.mesh", meshId };
```

### 3.2 同 ID 下保存源资源和派生缓存

```cpp
auto meshId = iCAX::Data::GenerateNewUUID();

pool.Set<MeshResource>(meshId, mesh, "render.mesh");
pool.Set<GpuMeshResource>(meshId, gpuMesh, "render.glmesh");

auto sourceMesh = pool.Get<MeshResource>(meshId, "render.mesh");
auto cachedMesh = pool.Get<GpuMeshResource>(meshId, "render.glmesh");
```

这对应旧渲染代码中的场景：同一个 mesh ID 可以找到源 mesh，也可以找到已经上传到渲染后端的 gl mesh。

### 3.3 资源元信息

```cpp
CResourceInfo info;
info.Name = "PartPreview";
info.Source = "project://assets/preview.png";
info.nSize = 1024;
info.Metadata["format"] = "rgba8";

pool.Set<ImageResource>(imageId, image, "image.rgba8", info);

auto storedInfo = pool.GetInfo(CResourceKey{ "image.rgba8", imageId });
```

## 4. 类型约定

长期保存或跨模块传递的引用必须使用显式稳定类型字符串，例如 `render.mesh`。

模板默认类型名来自 C++ `typeid(T).name()`，只适合进程内临时缓存，不应写入项目文件或长期协议。

## 5. 线程安全

`CResourcePool` 的公开接口是线程安全的。

资源池保护的是内部映射表，不保护资源对象自身。多个模块取到同一个 `shared_ptr<T>` 后，如果要修改资源对象，仍需要资源对象自己提供同步策略。
