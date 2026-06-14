# build

`build` 放置本地构建辅助脚本。

## 目录结构

- `build_debug_x64.ps1`：按依赖顺序逐个编译当前 C++ 项目的 Debug|x64 配置，并在子进程中清理重复的 `Path/PATH` 环境变量。
- `run_tests_debug_x64.ps1`：编译并运行当前 Debug|x64 单元测试项目。
