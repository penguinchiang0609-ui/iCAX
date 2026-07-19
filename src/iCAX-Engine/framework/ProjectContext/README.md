# ProjectContext

`ProjectContext` 是 framework 层的项目/场景公共契约项目。

它把项目级管理容器和场景级运行现场拆成两个接口。`CProject` 实现 `IProjectContext`，只表达项目身份、项目路径和跟项目文件走的 `Settings`；`CProjectScene` 实现 `ISceneContext`，表达一个具体运行现场。

项目级设置是跟随项目文件保存和打开的项目级参数。ProjectContext 提供唯一 settings 访问入口。这里不能混入产品级设置或应用级设置。会被用户作为对象选择、增删、排序、撤销的数据仍应进入 Repository，而不是放到 settings 中。

framework 不定义任何项目业务字段。它只提供通用键值/路径数据表达；具体图纸加载器、文件模块或产品插件负责写入自己的项目级参数。

目标接口形态：

```cpp
class IProjectContext
{
public:
    virtual iCAX::Data::PropertyBag& Settings() = 0;
    virtual const iCAX::Data::PropertyBag& Settings() const = 0;
};
```

注意：产品级应用参数属于 ProductContext / ProductData，不属于 ProjectContext。

基本分界线：

```text
ApplicationContext / ApplicationSettings -> iCAX 应用程序参数，和任何产品无关
ProductContext / ProductData             -> 产品级应用参数，跟产品和用户环境走
ProjectContext / Settings                -> 项目级参数，跟项目文件走
Repository / Entity                      -> 场景业务对象
Resources / PDOHub / FacadeChannel         -> 场景运行资源
SceneObjectRegistry                      -> 场景运行期对象、alias、几何、Transform、Collider 的统一身份映射
```

## 目录结构

- `IProjectContext.h` / `IProjectContext.cpp`：项目级上下文接口，只包含项目身份、项目路径和 Settings。
- `ISceneContext.h` / `ISceneContext.cpp`：场景级上下文接口，包含 Repository、ResourceLibrary、ServiceProvider、FacadeChannel、PDOHub 和 SceneObjectRegistry 访问入口。
- `SceneObjectRegistry.h/.cpp`：Scene 内运行期身份注册表。Repository Entity 可以映射为 `SceneObjectID`；非 Entity 对象通过 framework 不解释的 `alias namespace + key` 映射为 `SceneObjectID`。渲染 instance、camera、transform、collider 都使用这个身份体系，前端或物理服务返回 ID 后，后端可以通过 Registry 反查到业务 Entity 或运行期 alias。

`SceneObjectRegistry` 不定义任何产品对象类型。相机、碰撞体、资源预览、切割头、刀路等都不是 framework 的固定枚举；它们如果需要运行期身份，只能由对应产品或插件用自己的 alias 命名空间注册，例如 `render.camera/default` 或 `physics.body/42`。
- `ProjectContextExport.h`：DLL 导出宏。
