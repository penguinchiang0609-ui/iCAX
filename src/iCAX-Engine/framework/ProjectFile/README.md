# ProjectFile

`ProjectFile` 是 Database 与 ResourceLibrary 的项目持久化入口。

普通上层只使用 `CProjectFile::Save` 和 `CProjectFile::Open`：保存时直接交入 Database 与 ResourceLibrary；打开时传入两个新建的空容器，模块完成当前格式校验和填充。

内部保留两层能力：

- `CProjectDocument`：类似 IFC/STEP 的中立平铺文档；
- `CProjectFileCodec`：等价的 ASCII/Binary 编码与原子落盘；

这些内部能力用于诊断工具和测试，不是日常业务调用路径。

项目只读取当前容器和当前产品格式。其他版本明确拒绝；没有升级器、迁移模块或旧资源布局。Database、ResourceLibrary、PDO 和前端 View 本身不解释项目文件版本。

完整调用见 [USAGE.md](USAGE.md)。
