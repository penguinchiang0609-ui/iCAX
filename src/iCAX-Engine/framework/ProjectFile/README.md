# ProjectFile

`ProjectFile` 是 Database 与 ResourceLibrary 的项目持久化入口。

普通上层只使用 `CProjectFile::Save` 和 `CProjectFile::Open`：保存时直接交入 Database 与 ResourceLibrary；打开时传入两个新建的空容器，模块完成校验、升级和填充。

内部仍保留三层能力：

- `CProjectDocument`：类似 IFC/STEP 的中立平铺文档；
- `CProjectFileCodec`：等价的 ASCII/Binary 编码与原子落盘；
- `CProjectMigrationRegistry`：文档和资源 Schema 的单向升级链。

这些内部能力用于迁移器、诊断工具和测试，不是日常业务调用路径。

版本边界保持不变：只支持低版本向当前版本升级，不支持降级，也不支持低版本保存高版本项目。Database、ResourceLibrary、PDO 和前端 View 本身不理解项目文件版本。

完整调用见 [USAGE.md](USAGE.md)。
