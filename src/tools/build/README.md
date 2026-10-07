# build

`build` 放置本地构建辅助脚本。

## Visual Studio 完整解决方案

完整应用使用 `src/iCAX.slnx`，配置选择 `Debug | x64` 或 `Release | x64`，可在 VS 中执行“生成解决方案”或“重新生成解决方案”。`src/iCAX-Engine/iCAX.sln` 仅包含部分引擎与测试项目，不能代替主解决方案验证完整应用。

## Release x64

安装 Visual Studio 2022 17.14 或更新版本的 C++ 桌面开发工具，以及 CMake、PowerShell 7。在仓库根目录运行：

```powershell
pwsh -NoProfile -File src/tools/build/build_solution_x64.ps1 -Configuration Release
```

脚本编译完整 `iCAX.slnx`（含已启用的测试项目），自动准备缺失的对应配置 OpenCascade 库，并处理宿主环境中重复的 `Path/PATH`。默认同时编译 4 个项目，可用 `-Parallel` 调整。首次构建 OpenCascade 较久，后续增量编译复用已生成的依赖。

输出为 `src/x64/Release/TubeDesigner.exe` 及同目录 DLL；日志为 `src/.codex_tmp/build/Release-x64.log`。Debug 使用 `-Configuration Debug`，输出到独立的 `src/x64/Debug`。构建前会检查目标目录是否被正在运行的程序占用，不会结束这些进程。

若只使用 Visual Studio，需要先准备几何内核的 Release 库，再打开 `src/iCAX.slnx`，选择 `Release | x64` 并生成解决方案：

```powershell
pwsh -NoProfile -File src/tools/build/build-opencascade.ps1 -Configuration Release -Parallel 4
```

只有 `libd/bind` 表示仅安装了 Debug 版 OpenCascade；Release 需要 `lib/bin`，否则链接报 `LNK1181: TKernel.lib`。不得通过改用 Debug 库来绕过 Release 依赖。Application 或 TubeDesigner 生成后会同步模板、网页和公共 SDK，再通过部署脚本更新产品运行清单，避免新版界面加载旧模板或旧接口。手工部署清单使用 `deploy_tube_designer_manifest.ps1 -OutputDirectory src/x64/Release`；该脚本先校验全部模块路径，不能直接复制源码清单。

生成前先保存并退出正在运行的 iCAX 和测试程序。`src/Directory.Build.targets` 会在清理和链接前检查输出文件是否被占用；若出现 `ICAXBUILD001`，解除占用后重试。DLL 正在运行时无法覆盖，仅调整编译顺序不能解决该问题。

项目顺序由 `.vcxproj` 的 `ProjectReference` 决定。新增引用须填写与被引用项目一致的 `Project` GUID，并把被引用项目加入主解决方案；动态加载的运行时插件也应使用项目引用，设置 `LinkLibraryDependencies=false`。测试继承 `src/Directory.Build.props`，与应用共用输出目录，每个项目保留独立中间目录。

TubeDesigner 插件生成后会完整同步模板运行文件，单独生成插件也包含各模板的 `applicability.py`、`example.py` 及共用脚本。手工更新模板时使用 `sync_tube_designer_templates.ps1 -OutputDirectory src/x64/Debug`（Release 同理），不要只复制单个入口脚本。添加 `-ValidateOnly` 可只读检查全部部署文件的内容与源码一致；缺失或错版会失败。原生接口回归应在对应运行目录启动，避免从仓库根目录运行而只验证源码模板。

主解决方案中的 JoltPhysics、JoltColliderService、JoltPhysicsTest、ColliderServiceTest、WpfUIContainer 保持原有未启用配置；其“跳过”消息不是编译失败。

## 目录结构

- `build_solution_x64.ps1`：完整应用解决方案的 x64 构建入口，默认 Release，支持 Debug。
- `run_tests_debug_x64.ps1`：编译并运行当前 Debug|x64 单元测试项目。

Application 或 TubeDesigner 生成后还会镜像产品网页和公共工作台资产，删除源码已撤除的运行文件；单独生成产品插件也会更新界面。手工同步使用 `sync_tube_designer_web_assets.ps1 -OutputDirectory src/x64/Debug`，`-ValidateOnly` 校验全部内容及多余文件。

发布前可将 Release 构建目录部署为独立运行包：

当前 Release 强制执行授权。正式打包前须由现有离线签发库导出 `IssuerTrust.generated.h`，放入 `src/licensing/include` 后重建；客户端只包含公钥。运行包脚本读取实际 `TubeDesigner.dll` 的构建信息，拒绝开发免授权、未配置公钥或功能目录不一致的版本。正式私钥、签发库、客户授权文件不进入安装包。

```powershell
pwsh -NoProfile -File src/tools/build/package_tube_designer_release.ps1 -BuildDirectory src/x64/Release -OutputDirectory output/TubeDesigner
```

该脚本仅复制正式 EXE、DLL 和 CEF 数据，使用部署脚本生成产品清单、同步完整模板和网页并准备内嵌 Python，同时从 Visual Studio 的 x64 可再发行目录补齐 VC 运行库。测试桥、符号、中间产物、日志和用户数据不会被带入。添加 `-ValidateOnly` 可检查运行包与当前构建及源码内容一致，包含 Python 运行文件；可用 `-VisualCppRuntimeDirectory` 指定 x64 `Microsoft.VC143.CRT` 目录。默认 EXE 保留项目既有的管理员权限要求，实际启动验收需完成系统提权。

## Windows 安装包

安装器使用 NSIS 3.13（Unicode），仅支持 x64 Windows 10 / 11。先完成上面的 Release 构建和独立运行包部署，再运行：

```powershell
pwsh -NoProfile -File src/tools/build/build_tube_designer_installer.ps1 `
    -PackageDirectory output/releases/TubeDesigner-20261007 `
    -OutputDirectory output/releases/installers `
    -NSISCompilerPath .deps/nsis-3.13/makensis.exe `
    -ReleaseDate 20261007
```

`BuildDirectory` 默认为 `src/x64/Release`；`ReleaseDate` 默认使用制作当天的 `yyyyMMdd` 日期，产品版本保持 `0.1.0`。需要其他构建目录或 VC 运行库位置时可传入 `-BuildDirectory`、`-VisualCppRuntimeDirectory`。脚本先调用运行包脚本的 `-ValidateOnly`，再按已验证的正式文件清单生成安装器；不会打包任意构建目录文件或 Python 缓存。文件路径含 NSIS 特殊字符（引号、美元符号或换行）或经过链接时会拒绝。

输出为 `TubeDesigner-0.1.0-20261007-x64-Setup.exe` 和对应 `.sha256` 校验文件。文件清单、安装/卸载指令和编译日志保存在输出目录的 `.installer-metadata/TubeDesigner-0.1.0-20261007-x64`，供验收使用，不进入运行包。输出目录必须与运行包分开。

正式安装需要管理员权限，默认路径为 `Program Files/TubeDesigner`，建立桌面和开始菜单快捷方式，以及 Windows 应用卸载记录。安装程序不结束正在运行的程序、不添加文件关联、不自动启动 TubeDesigner；覆盖安装前先保存工作并退出程序。卸载仅删除本次明确声明的程序文件和空目录，不递归清空安装目录，也不删除用户项目或 LocalAppData 中的数据。

验收使用同一个 `TubeDesigner.nsi`，可额外定义 `PACKAGE_TEST_ONLY=1` 和独立的 `INSTALLER_OUTPUT`，配合生成的 `CONFIG_INCLUDE` 编译隔离测试安装器。例如：

```powershell
& .deps/nsis-3.13/makensis.exe /INPUTCHARSET UTF8 `
    /DPACKAGE_TEST_ONLY=1 `
    "/DINSTALLER_OUTPUT=$((Join-Path (Get-Location) 'output/tests/installer-20261007/test-setup.exe'))" `
    "/DCONFIG_INCLUDE=$((Join-Path (Get-Location) 'output/releases/installers/.installer-metadata/TubeDesigner-0.1.0-20261007-x64/installer-config.nsh'))" `
    src/tools/build/TubeDesigner.nsi
```

测试模式仅去掉压缩、提权、系统卸载记录和快捷方式操作，安装与卸载仍使用同一份精确 payload 指令。只能将测试版静默安装到隔离验收目录，比对全部文件哈希，再加入模拟用户文件验证卸载保留；测试版不得作为正式安装包发布。正式构建脚本不会启用该定义。
