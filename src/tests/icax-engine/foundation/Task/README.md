# Task Tests

`Task` 测试目录与 `src/icax-engine/foundation/Task` 一一对应，放置 foundation Task 项目的单元测试。

## 目录结构

- `TaskTest/`：基于 GoogleTest 的 Task 单元测试工程。
- `TaskTest/TaskSanitizerProbe.cpp`：独立的 AddressSanitizer 压力检查，不链接预编译 GoogleTest。

## 可靠性回归

运行 `TaskTest.exe` 执行全部 Task、Coroutine 和压力回归；运行
`TaskTest.exe --gtest_filter=TaskStressTest.* --gtest_repeat=100` 重复测试并发提交、
关闭排空、worker 内销毁、竞争完成、异常聚合和取消传播。
每轮包含 4 个提交线程、4000 次任务、200 次结果/失败/取消竞争以及 100 次 worker 内销毁。

修复前已复现：第二个 Shutdown 提前返回、worker 关闭时等待依赖自己的同行、
取消回调异常阻断后续等待者通知。worker 捕获队列共享状态，也消除了原先销毁后继续使用 Impl 的悬空访问路径。

内存检查需将 `Task.dll` 和 probe 一起使用同一 MSVC 工具链的 `/fsanitize=address /Zi /MDd /D_DEBUG /std:c++20 /EHsc /utf-8` 编译，
probe 链接对应 `Task.lib`，加入 foundation 头文件目录，并将该工具链的 ASan runtime DLL 目录加入 PATH。
probe 执行 1000 次 worker 内释放线程池、40000 次并发任务和取消异常传播检查。
不要把这一检查与仓库预编译的旧 GoogleTest 二进制混用：其旧 STL 与新编译器的 ASan STL 混合实例化会在测试注册阶段报错。

压力测试与 ASan 检查不是形式化证明，也不涵盖所有 API 组合。有限线程池内的同步嵌套等待仍需调用方避免，详见 Task README。
