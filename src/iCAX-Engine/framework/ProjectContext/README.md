# ProjectContext

`ProjectContext` 是 framework 层的项目公共契约项目。

它只表达项目现场的访问接口，不负责项目线程、邮箱、快速保存日志、PDOHub 或 Universe 生命周期。`Project` 项目中的 `CProject` 实现 `IProjectContext`。

## 目录结构

- `IProjectContext.h` / `IProjectContext.cpp`：项目运行上下文接口，包含 Repository、ResourceLibrary、ServiceProvider 和可选 PDOHub 访问入口。
- `ProjectContextExport.h`：DLL 导出宏。
