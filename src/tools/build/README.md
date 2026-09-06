# build

`build` 放置本地构建辅助脚本。

## Visual Studio 完整解决方案

完整应用使用 `src/iCAX.slnx`，配置选择 `Debug | x64`，可在 VS 中执行“生成解决方案”或“重新生成解决方案”。`src/iCAX-Engine/iCAX.sln` 仅包含部分引擎与测试项目，不能代替主解决方案验证完整应用。

生成前先保存并退出正在运行的 iCAX 和测试程序。`src/Directory.Build.targets` 会在清理和链接前检查输出文件是否被占用；若出现 `ICAXBUILD001`，解除占用后重试。DLL 正在运行时无法覆盖，仅调整编译顺序不能解决该问题。

项目顺序由 `.vcxproj` 的 `ProjectReference` 决定。新增引用须填写与被引用项目一致的 `Project` GUID，并把被引用项目加入主解决方案；动态加载的运行时插件也应使用项目引用，设置 `LinkLibraryDependencies=false`。测试继承 `src/Directory.Build.props`，与应用共用输出目录，每个项目保留独立中间目录。

主解决方案中的 JoltPhysics、JoltColliderService、JoltPhysicsTest、ColliderServiceTest、WpfUIContainer 保持原有未启用配置；其“跳过”消息不是编译失败。

## 目录结构

- `build_debug_x64.ps1`：按依赖顺序逐个编译当前 C++ 项目的 Debug|x64 配置，并在子进程中清理重复的 `Path/PATH` 环境变量。
- `run_tests_debug_x64.ps1`：编译并运行当前 Debug|x64 单元测试项目。
