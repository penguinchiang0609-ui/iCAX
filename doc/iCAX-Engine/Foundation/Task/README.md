# Task

`Task` 是 foundation 中面向 C++ 上层代码的异步任务基础能力，提供接近 C# `Task` 的一次性异步结果模型，并提供由 owner event loop 驱动的 C++20 Coroutine 适配。

## 目录结构

- `Task规格文档.md`：描述 Task 的目标、范围、公开 API、行为语义和测试要求。
- `Task方案文档.md`：描述 Task 的内部结构、调度方案、取消方案、组合方案和后续演进约束。
