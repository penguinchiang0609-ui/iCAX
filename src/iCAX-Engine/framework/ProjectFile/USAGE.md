# ProjectFile 外部使用

普通 target 不需要构造 `CProjectDocument`，也不需要依次调用 Decode、Migration 和运行时物化。一个产品只创建一个 `CProjectFile` 入口，保存和打开时直接传入 Database 与 ResourceLibrary。

## 1. 产品级初始化

```cpp
#include <ProjectFile/ProjectFile.h>

using namespace iCAX::ProjectFile;

CProjectFile g_ProjectFile({
    .Magic = "ICAX-CAM-PROJECT",
    .ProductID = "icax.cam",
    .CurrentFormatVersion = "2.0",
    .nCurrentFormatRevision = 2,
    .DefaultEncoding = EProjectFileEncoding::Binary
});
```

构造时会自动回放当前进程已经注册的项目升级器。多产品宿主可通过 `MigrationModulePaths` 只回放当前产品 DLL 提供的升级器。

## 2. 保存

```cpp
CProjectDocumentInfo _Info;
_Info.ProjectID = Project.ID();
_Info.MainSceneID = Scene.ID();
_Info.ProjectName = Project.Name();
_Info.ProjectSettings = Project.Settings();
_Info.MainSceneSettings = Scene.Settings();

g_ProjectFile.Save(
    "part.icax",
    std::move(_Info),
    Scene.Database(),
    Scene.Resources());
```

`Save` 自动完成：

- 遍历 Database 的 Entity 和 Component，只采集声明为 `Persistent` 的字段；
- 从 ResourceLibrary 采集全部持久化资源及其精确版本依赖闭包；
- 校验引用、依赖和资源正文；
- 使用临时文件校验后原子替换目标文件。

业务层不再手工平铺组件或资源。

## 3. 打开

```cpp
// Database 与 ResourceLibrary 必须是为该 Scene 新建的空容器；
// Database 的 ID 应等于文件中的 MainSceneID。
auto _Result = g_ProjectFile.Open(
    "part.icax",
    Scene.Database(),
    Scene.Resources());

Project.SetName(_Result.Info.ProjectName);
Project.SetSettings(_Result.Info.ProjectSettings);
Scene.SetSettings(_Result.Info.MainSceneSettings);
```

`Open` 自动完成：读取、完整性校验、旧版本向当前版本升级、依赖优先恢复 Resource、基线填充 Database。任何一步失败都会回滚 Database 并清空本次恢复的 Resource，不会留下半加载状态。

## 4. 业务资源只注册一次 codec

`resource.binary` 和 `resource.flatbuffer` 已有内置 codec。BREP、刀路等业务 C++ 对象只需在创建 ResourceLibrary 时注册一次稳定类型与编解码规则：

```cpp
iCAX::Resource::CResourceVersionCodec _Codec;
_Codec.Serialize = SerializeBrep;
_Codec.Deserialize = DeserializeBrep;

Scene.Resources().RegisterPersistenceCodec<CBrepResource>(
    "geometry.brep",
    std::move(_Codec));
```

注册之后，所有项目的 `Save/Open` 都由文件模块自动处理该资源。项目文件只保存稳定的 `ResourceTypeID`，不保存编译器相关的 C++ 类型名。

## 5. 版本升级

产品 DLL 继续通过静态宏贡献单向升级器：

```cpp
ICAX_REGISTER_PROJECT_DOCUMENT_MIGRATION(CDocumentMigration1To2)
ICAX_REGISTER_PROJECT_RESOURCE_MIGRATION(CBrepMigration1To2)
ICAX_REGISTER_CURRENT_PROJECT_RESOURCE_SCHEMA("geometry.brep", 2)
```

正常调用 `Open` 时会自动执行这些升级器。只有迁移工具、格式诊断器和单元测试才需要直接使用 `CProjectDocument`、`CProjectFileCodec` 或 `CProjectMigrationRegistry`。
