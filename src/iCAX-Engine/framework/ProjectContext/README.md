# ProjectContext

`ProjectContext` 是 framework 层的项目公共契约项目。

它只表达项目现场的访问接口，不负责项目线程、邮箱、快速保存日志、PDOHub 或 Universe 生命周期。`Project` 项目中的 `CProject` 实现 `IProjectContext`。

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
Repository / Entity                      -> 项目业务对象
Resources                                -> 项目内嵌资源
```

## 目录结构

- `IProjectContext.h` / `IProjectContext.cpp`：项目运行上下文接口，包含 Settings、Repository、ResourceLibrary、ServiceProvider 和可选 PDOHub 访问入口。
- `ProjectContextExport.h`：DLL 导出宏。
