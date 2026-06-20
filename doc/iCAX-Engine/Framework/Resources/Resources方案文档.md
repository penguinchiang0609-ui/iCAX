# Resources 方案文档

## 1. 放置位置

`Resources` 放在：

```text
src/icax/framework/Resources/
```

原因：

- 它不只是底层对象池，还表达工程资源条目、资源 manifest、持久化策略和加载器抽象。
- Database、ProjectStorage、CommandHandler、Behaviour 和业务插件都通过资源路径或来源字符串引用资源，`ResourceKey` 只作为 Resources 内部索引。
- 资源是否运行时临时、内嵌保存或外部引用，属于工程框架语义，不适合放在 foundation。

`Resources` 仍然不依赖 `Database`、`Services`、`ApplicationHost` 或业务扩展，避免形成框架内部循环依赖。

## 2. 项目组成

```text
Resources/
  ResourceKey.*
  ResourceInfo.h
  ResourceLibrary.*
  ResourcePool.*
  ResourcePoolAccess.h
  ResourceLoadContext.h
  ResourceLoadResult.h
  IResourceLoader.h
  ResourceLoaderRegistry.*
  Resources.h
```

`CResourceLibrary` 负责 Project 级资源访问入口，普通上层代码通过 `project.Resources()` 访问 Resources，不需要看到 `CResourcePool`。  
`CResourcePool` 负责资源条目和对象池。  
`ResourcePoolAccess.h` 负责显式 `CResourcePool` 的高级访问重载。  
`IResourceLoader` 负责定义加载器协议。  
`CResourceLoaderRegistry` 负责按 C++ 资源类型注册、查找和调用 loader。
`CResourceLibrary::Load<T>` 负责给上层提供“路径即 key，有则取、无则加载”的简化入口。常规入口使用 Project 持有的资源库，不要求调用方传入 `CResourcePool`。

`Resources.h` 不包含 `ResourcePool.h`。普通业务代码 include `Resources.h` 时只看到 `CResourceLibrary`、资源信息、loader 协议和注册表；需要底层资源池能力时显式 include `ResourcePoolAccess.h`。

## 3. 条目和对象分离

资源池把“资源条目”和“资源对象”分开：

- `Register` / `TryRegister`：只创建或更新资源条目。
- `Set` / `TryAdd`：创建资源条目并绑定对象。
- `Unload`：释放对象但保留条目。
- `Remove`：删除整个条目。

这样工程打开时可以先恢复资源目录；真实资源对象可以在后续命令、行为或渲染路径中按需加载。

## 4. Loader 设计

loader 抽象不绑定具体文件格式：

```cpp
class IResourceLoader
{
public:
    virtual bool CanLoad(const CResourceLoadContext& context) const = 0;
    virtual CResourceLoadResult Load(const CResourceLoadContext& context) = 0;
};
```

`CanLoad` 允许 loader 基于来源、扩展名、选项或上下文进一步判断。  
`Load` 返回资源对象或只返回资源元信息。

具体格式 loader 放在插件或业务模块里。例如：

- `FbxResourceLoader`
- `PngResourceLoader`
- `TextResourceLoader`
- `StepResourceLoader`

## 5. Registry 设计

`CResourceLoaderRegistry` 是按资源类型组织的 loader 注册表。正式运行路径使用 ApplicationHost 持有的应用级 registry；裸用 `CResourceLibrary` 或底层测试可以回退到静态全局 registry。内部结构：

```cpp
std::map<std::type_index, std::vector<std::shared_ptr<IResourceLoader>>> loaders;
```

同一个 C++ 资源类型可以注册多个不同 loader。为了适配宏注册和静态初始化，同一资源类型下重复注册同一个 loader 类会被忽略。`FindLoader` 会先按 `std::type_index(typeid(TResource))` 查候选 loader，再调用 `CanLoad` 选择第一个可处理的 loader。

业务模块和插件通常在具体 loader 的 `.cpp` 中使用宏注册：

```cpp
ICAX_REGISTER_RESOURCE_LOADER(ModelResource, FbxResourceLoader)
```

也可以直接调用实例注册表：

```cpp
CResourceLoaderRegistry registry;
registry.RegisterLoader(typeid(ModelResource), std::make_shared<FbxResourceLoader>());
```

宏在静态初始化阶段把注册动作记录到 `CResourceLoaderRegistrationCatalog`。ApplicationHost 创建应用级 registry 后，ProductRuntime 按产品模块路径把对应注册动作回放进去。业务代码不需要在每次加载资源时传 registry。`.fbx`、`.png` 这类后缀只是来源格式，应由 loader 的 `CanLoad` 判断，不作为注册表的 key。

`Load` 的流程：

```text
FindLoader(context)
  no loader -> NoLoader
  loader.Load(context)
    failed -> return failed result
    ok + object -> return object result
    ok + metadata only -> return metadata result
```

`CResourceLoaderRegistry` 不写资源池，避免 loader 上下文泄漏 pool 概念。资源池写入由 `CResourceLibrary` 或 `ResourcePoolAccess` 在拿到成功结果后完成。

## 6. 简化访问入口

上层业务代码不应该直接拼装 `CResourceLoadContext`。常用路径使用：

```cpp
auto value = project.Resources().Load<T>(source);
```

内部流程：

```text
source -> MakeResourceKeyFromSource(source)
project resource library.Get<T>(internalKey)
  success -> return shared_ptr<T>
project resource library.HasObject(internalKey)
  true -> return nullptr
build load context from key/source/stored ResourceInfo
当前 ResourceLoaderRegistry.LoadResource(context with typeid(T))
  failed -> return nullptr
store successful result into project resource library
project resource library.Get<T>(internalKey)
  return loaded object
```

如果 `CResourceLibrary` 没有绑定 registry，会回退到静态全局 registry。正式 Project 路径由 `Project` 构造时注入 ApplicationHost 的应用级 registry，不依赖全局静态注册表。

路径入口生成的内部 key 直接保存 `source` 字符串，不进行二次映射。对上层而言，路径就是资源身份，同一个路径不会因为 `T` 不同而形成两个 key。

显式 `CResourcePool` 重载仍然保留给隔离场景：

```cpp
CResourcePool previewPool;
auto value = Load<T>(previewPool, source);
```

这类用法适合单元测试、导入预览、临时工程或多工程并行处理，不是业务代码的默认写法。

## 7. 持久化策略

`CResourceInfo::Persistence` 表达资源条目的保存策略：

```cpp
enum class EResourcePersistenceMode : uint8_t
{
    RuntimeOnly,
    Embedded,
    External
};
```

`GetManifest(false)` 默认只返回 `Embedded` 和 `External` 条目，供后续 ProjectStorage 保存工程时使用。

## 8. 类型安全

资源对象以 `shared_ptr<void>` 存储，同时记录 `std::type_index`。

底层模板接口：

```cpp
pool.Set<T>(source, ptr);
auto ptr = pool.Get<T>(source);
```

这也是 `Source` 身份模型。上层加载资源时使用 `project.Resources().Load<T>(source)`，loader 返回对象时必须携带运行时类型，访问层写入资源池前会校验对象和类型信息是否完整。运行时类型只用于校验，不参与 key。

## 9. 并发策略

资源池和 loader 注册表都使用 `std::shared_mutex`：

- 读取、查找、枚举使用共享锁。
- 注册、删除、更新使用独占锁。

资源池只保证映射表线程安全。资源对象自身是否可变、是否线程安全，由资源类型自己决定。

## 10. 不纳入当前项目的能力

当前不做：

- 图片、模型、字体等具体格式解析。
- 工程文件或资源包格式。
- 异步加载。
- LRU 或内存预算淘汰。
- GPU 或前端资源释放协议。

这些能力可以后续建立在 `Resources`、`ProjectStorage` 或业务插件之上。
