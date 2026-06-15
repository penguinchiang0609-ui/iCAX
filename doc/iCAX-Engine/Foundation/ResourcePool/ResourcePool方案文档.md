# ResourcePool 方案文档

## 1. 放置位置

`ResourcePool` 放在：

```text
src/icax/foundation/ResourcePool/
```

原因：

- 它是底层资源引用和缓存能力，不依赖 `framework`、`services` 或产品扩展。
- `Database`、渲染、导入导出、前后端交互都可能只保存资源 key，而不应该直接依赖某个具体资源服务。
- 它抽象的是进程内对象池，不是业务服务。

## 2. 和旧 AssetService 思路的关系

旧代码里资源服务的典型使用方式是：

```cpp
auto mesh = resource->Get<rMesh>(meshId);
auto glMesh = resource->Get<glMesh>(meshId);
resource->Set<glMesh>(meshId, glMesh);
```

这个模式保留，但资源池不再绑定服务系统。新的完整 key 是：

```text
ResourceType + ResourceID
```

这样同一个资源 ID 可以对应不同资源形态：

```text
render.mesh   + meshId -> source mesh
render.glmesh + meshId -> GPU mesh cache
```

旧 `Document::ResourceBlock` 提供了资源入口表的思想：资源 ID、类型、标志、偏移和大小。当前 `ResourcePool` 先只做内存池；未来持久化资源包可以复用 `CResourceKey` 和 `CResourceInfo`。

## 3. 内部结构

核心数据结构：

```cpp
std::map<CResourceKey, CResourceRecord> m_mapResources;
```

其中：

```cpp
struct CResourceRecord
{
    CResourceInfo Info;
    std::type_index RuntimeType;
    std::shared_ptr<void> pResource;
};
```

`CResourceKey` 使用 `Type` 和 `ID` 排序，因此天然支持同 ID 不同类型并存。

## 4. 类型安全

资源对象以 `shared_ptr<void>` 存储，同时记录 `std::type_index`。

模板接口：

```cpp
pool.Set<T>(id, ptr, type);
auto ptr = pool.Get<T>(id, type);
```

`Get<T>` 先查 `ResourceKey`，再校验运行时类型。类型不匹配返回 `nullptr`，避免把错误资源强转成目标类型。

## 5. 并发策略

资源池使用 `std::shared_mutex`：

- `Get`、`Contains`、`Count`、`GetInfo`、`GetKeys`、`GetInfos` 使用共享锁。
- `Set`、`TryAdd`、`Remove`、`Clear`、`UpdateInfo` 使用独占锁。

资源池只保证映射表线程安全。资源对象自身是否可变、是否线程安全，由资源类型自己决定。

## 6. 生命周期

资源池持有 `shared_ptr`。当资源被 `Remove`、`Clear` 或替换时，资源池释放自己的引用；如果外部仍持有 `shared_ptr`，对象继续存活。

这使资源池适合做缓存和共享入口，不强制控制所有资源生命周期。

## 7. 不纳入当前项目的能力

当前不做：

- 文件加载器。
- 图片、模型、字体等格式解析。
- 资源包格式。
- 异步加载。
- LRU 或内存预算淘汰。
- GPU 或前端资源释放协议。

这些能力可以后续建立在 `ResourcePool` 之上，但不应该塞进当前基础项目。
