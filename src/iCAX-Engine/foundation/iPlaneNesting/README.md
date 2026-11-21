# iPlaneNesting

iCAX 源码内的独立二维排样算法项目，C++20 实现，提供 C ABI。它包含此前 v0.4.0 交付版的完整计算内核及公开 PN2/PN2S 接口，生成 `iPlaneNesting.dll` 和 `iPlaneNesting.lib`。主头文件为 `include/iPlaneNesting.h`；原 `alma_study.h` 作为转发头保留，已有调用端重新链接新导入库即可。

项目在 `src/iCAX.slnx` 的 `iCAX-Engine/foundation/iPlaneNesting` 下独立存在。它不依赖 iCAX 场景、服务、界面或研究目录；本次整理不切换现有产品的排样入口。

## 目录

- `include/`：公开 C 接口及兼容转发头。
- `src/`：排样策略、几何、约束、LP、会话与结果管理。
- `src/material/`：真实板材/内孔、连续角度与混合库存跨板优化。
- `cmake/`：内核、示例、测试以及供调用方使用的 CMake 配置。
- `examples/`、`tests/`：公开调用示例和独立原生回归。
- `third_party/`：固定版本的 Clipper2、HiGHS 1.7.2、Boost 1.87.0 源码、来源及许可证。

Demo 的 HTTP 服务、网页、DXF 导入界面和历史逆向工具不属于算法项目。运行和构建均不需要供应商 DLL、原版机器指令夹具、授权组件、Node 或 Python。

## 构建与测试

Windows x64，需要 Visual Studio 2022 C/C++ 工具链和 CMake 3.24 以上。

```powershell
# 生成算法 DLL、导入库、公头及 CMake 调用配置
.\build.ps1 -Configuration Release

# 编译并运行全部独立原生测试
.\build.ps1 -Configuration Release -Action Test

# 可分别使用 Debug、Rebuild 和 Clean
.\build.ps1 -Configuration Debug
```

Visual Studio 中直接生成 `iPlaneNesting` 项目与上述 Build 等价；测试单独执行 `-Action Test`。Win32 解决方案配置不会自动生成此 x64 项目。编译文件位于模块 `build/<配置>`，SDK 位于 `dist/<配置>`，不会部署到正在使用的应用目录。

也可直接使用 CMake：

```powershell
cmake -S . -B build/Release -G "Visual Studio 17 2022" -A x64
cmake --build build/Release --config Release --target iPlaneNesting
```

## 调用

C/C++ 调用方包含 `iPlaneNesting.h`，链接 `iPlaneNesting.lib`，运行时保留配套 `iPlaneNesting.dll`。所有对象均由会话管理；通过公开查询读取结果，按接口销毁对象，不跨 DLL 释放其内部内存。实际调用见 `examples/pn2_c_example.c` 和 `tests/material_api_client.cpp`。

其他 CMake 项目可以 `add_subdirectory` 后链接 `iCAX::iPlaneNesting`；也可以使用已安装的 SDK：

```cmake
find_package(iPlaneNesting CONFIG REQUIRED)
target_link_libraries(my_program PRIVATE iCAX::iPlaneNesting)
```

用 `-DiPlaneNesting_DIR=<SDK目录>/lib/cmake/iPlaneNesting` 指定安装位置。作为子项目默认不构建测试和示例，可用 `IPLANE_NESTING_BUILD_TESTS`、`IPLANE_NESTING_BUILD_EXAMPLES` 显式设置。

## 能力与边界

保留矩形和异形板材、板内缺陷、零件内孔、有限及连续角度、镜像、间距/边距、混合物理库存与跨板重排能力。板材净面积利用率和包络填充率分别计算。整批搜索和结果检查共用时间预算，结果保留实际求得的角度及具体零件身份。

PN2 风格接口和限时工程策略不代表与商业原版逐项等价；特殊工艺等未支持的组合仍按接口明确返回。授权、激活及加密狗不在本项目范围内。源版本和机械迁移记录见 `provenance.json`；这里的独立构建与测试结果以本模块当前生成的报告为准，不沿用研究目录旧 DLL 的验收身份。

## 迁入验收

2026-11-15，正式源码位置的 Release 构建与 69 项独立 CTest 全部通过；Visual Studio 包装项目实际生成成功。安装后的 SDK 另外通过 C、C++ 和新主头文件调用这 3 项外部消费者检查，74 个公开导出符号与源版本一致。作为 CMake 子项目的配置也通过父选项隔离检查，未声称已验证 Debug 或 iPlaneCAM 产品运行。

实际 DLL、源码、测试报告及版本身份记录在本模块生成的 `validation/migration-audit.json`。构建日志与测试报告位于 `validation/`、`build/Release/native-tests.xml`。这些生成文件不进入源码提交。
