# Task 方案文档

## 1. 总体方案

Task 工程位于 `src/icax/foundation/Task/`，属于 Foundation 层基础能力。

核心结构如下：

```text
Task<T> / Task<void>
    |
    v
detail::TaskState<T> / detail::TaskState<void>
    |
    +-- 状态与结果
    +-- 异常
    +-- continuation 列表
    +-- condition_variable 等待

TaskCompletionSource<T>
    |
    v
控制 TaskState 进入成功、异常或取消终态

TaskScheduler
    |
    +-- InlineScheduler
    +-- ThreadPoolScheduler
    +-- ManualTaskScheduler
```

Task 对外是轻量句柄，内部共享 `TaskState`。多个 `Task` 拷贝指向同一个状态对象。

## 2. 模块划分

### 2.1 Task.h

负责主要模板 API：

- `Task<T>`、`Task<void>`。
- `TaskCompletionSource<T>`、`TaskCompletionSource<void>`。
- `Run`、`StartNew`、`TaskFactory`。
- `ContinueWith`。
- `Delay`、`TimeoutAfter`、`WaitAsync`。
- `WhenAll`、`WhenAny`、`WaitAll`、`WaitAny`、`Unwrap`。

由于 `Task<T>` 是模板，主体实现放在头文件中。

### 2.2 CancellationToken.h

负责取消模型：

- `CancellationToken`。
- `CancellationTokenSource`。
- `CancellationTokenRegistration`。
- linked cancellation。
- `CancelAfter`。

取消状态通过共享 `CancellationState` 管理。

### 2.3 TaskScheduler.h / TaskScheduler.cpp

负责调度模型：

- 调度器接口 `ITaskScheduler`。
- 内联调度器。
- 共享线程池调度器。
- 手动调度器。
- 当前线程可见调度器。
- timer queue。

`ManualTaskScheduler` 放在头文件中，便于单元测试和单线程主循环直接使用。

### 2.4 TaskStatus.h

负责状态和异常类型：

- `TaskStatus`。
- `TaskCanceledException`。
- `TaskTimeoutException`。
- `TaskAggregateException`。

### 2.5 TaskCreationOptions.h / TaskContinuationOptions.h

负责创建选项和 continuation 选项。

## 3. 状态存储方案

`TaskState<T>` 保存：

- `m_id`：任务唯一 Id。
- `m_asyncState`：用户传入的状态对象。
- `m_creationOptions`：创建选项。
- `m_status`：任务状态。
- `m_value`：任务结果。
- `m_exception`：任务异常。
- `m_continuations`：任务完成后的 continuation。
- `m_mutex` / `m_cv`：状态同步和等待。

`TaskState<void>` 与泛型版本一致，但不保存结果值。

所有完成入口都使用 `TrySet...` 形式保证状态只能从非终态进入终态一次。

## 4. 任务创建方案

### 4.1 Run

`Run` 是常用入口：

1. 创建 `TaskCompletionSource`。
2. 如果取消令牌已取消，直接返回取消任务。
3. 将工作函数封装成调度动作。
4. 调度器执行时标记任务 `Running`。
5. 执行用户函数。
6. 根据执行结果设置成功、异常或取消。

如果用户函数可接收 `CancellationToken`，则自动传入取消令牌。

### 4.2 StartNew

`StartNew` 提供更接近 C# 的创建控制：

- `CancellationToken`。
- `TaskCreationOptions`。
- `TaskScheduler`。
- `AsyncState`。

`LongRunning` 当前通过独立 `std::thread` 执行，并 detach。普通任务进入指定调度器。

`HideScheduler` 会让任务体内看到的 `CurrentScheduler()` 变成 `DefaultScheduler()`，避免泄露外部调度器。

### 4.3 TaskFactory

`TaskFactory` 保存默认配置，减少重复传参：

- 默认取消令牌。
- 默认创建选项。
- 默认 continuation 选项。
- 默认调度器。

当前 `TaskFactory::StartNew` 使用工厂保存的取消令牌、创建选项和调度器创建任务。

## 5. Continuation 方案

`ContinueWith` 的执行流程：

1. 创建 continuation 的 `TaskCompletionSource`。
2. 检查前序任务是否有效。
3. 根据取消令牌和 `LazyCancellation` 决定是否立即取消。
4. 将 continuation 注册到前序任务。
5. 前序任务进入终态后，判断 `TaskContinuationOptions` 条件是否匹配。
6. 选择目标调度器。
7. 执行 continuation 并设置结果。

调度器选择规则：

- `ExecuteSynchronously`：使用 `InlineScheduler()`。
- `RunContinuationsAsynchronously`：使用 `DefaultScheduler()`。
- 否则使用用户传入调度器。

可见调度器选择规则：

- 设置 `HideScheduler` 时，continuation 内 `CurrentScheduler()` 为 `DefaultScheduler()`。
- 未设置时，continuation 内 `CurrentScheduler()` 为实际目标调度器。

`LazyCancellation`：

- 未设置时，取消令牌已取消会让 continuation 立即进入取消状态。
- 设置时，continuation 等前序任务完成后再进入取消状态。

## 6. 调度方案

### 6.1 InlineScheduler

直接在当前调用栈执行任务。适合：

- 已完成任务 continuation。
- 单元测试。
- 简单同步场景。

### 6.2 ThreadPoolScheduler

共享线程池调度器，内部维护：

- worker 线程列表。
- action 队列。
- condition_variable。
- stopping 标记。

`Schedule` 将任务入队，worker 唤醒后执行。任务包装层负责捕获异常，线程池只负责调度。

### 6.3 ManualTaskScheduler

手动调度器只入队，不自动执行。

适合：

- 单元测试。
- 单线程主循环。
- 后续嵌入 backend tick 或 frontend pump 的场景。

### 6.4 CurrentScheduler

当前线程通过 thread-local 保存可见调度器。

`CurrentTaskSchedulerScope` 在构造时设置当前调度器，在析构时恢复原值。

### 6.5 TimerQueue

`Delay`、`TimeoutAfter`、`WaitAsync` 和 `CancelAfter` 共用轻量 timer queue，避免每次延迟都创建独立线程。

timer queue 使用一个后台线程管理到期动作，按到期时间排序执行。

## 7. 取消方案

`CancellationTokenSource` 和 `CancellationToken` 共享同一个 `CancellationState`。

`CancellationState` 保存：

- 是否已请求取消。
- 是否已 dispose。
- cancel-after 版本号。
- 注册回调列表。

`Cancel()`：

1. 设置 requested。
2. 取出 callback 列表。
3. 在锁外执行 callback。

`CancelAfter()`：

1. 增加版本号。
2. 将延迟取消动作放入 timer queue。
3. 到期时检查版本号，只有最新版本生效。

`CreateLinkedTokenSource()`：

1. 创建新的取消源。
2. 在输入 token 上注册回调。
3. 任一输入 token 取消时，触发 linked source 取消。
4. `Dispose()` 时注销 linked registrations。

## 8. 组合方案

### 8.1 WhenAll

`WhenAll` 为每个输入任务注册 continuation。

完成时统计：

- 成功结果。
- 取消状态。
- 异常列表。

全部输入任务完成后：

- 有异常时返回 `TaskAggregateException`。
- 有取消且无异常时返回取消任务。
- 全部成功时按输入顺序返回结果。

### 8.2 WhenAny

`WhenAny` 使用 atomic 标记首个完成任务。

第一个完成的任务会成为结果，后续任务完成时不再改变输出。

### 8.3 WaitAll / WaitAny

同步等待包装：

- `WaitAll` 基于 `WhenAll(...).Result()`。
- `WaitAny` 基于 `WhenAny(...).Result()`，再通过任务 Id 查找输入索引。

### 8.4 Unwrap

`Unwrap(Task<Task<T>>)` 先等待外层任务，再等待内层任务。

外层或内层取消时，输出任务取消。外层或内层异常时，输出任务异常。

## 9. 异常与结果方案

用户工作函数中的异常由任务包装层捕获。

处理规则：

- `TaskCanceledException` 转为取消终态。
- 其它异常保存为 `exception_ptr`。
- 多任务异常通过 `TaskAggregateException` 聚合。

`Result()` 会等待任务终态：

- 成功：返回结果。
- 取消：抛出 `TaskCanceledException`。
- 异常：重新抛出保存的异常。

`TakeResult()` 用于移动结果，支持 `std::unique_ptr<T>` 这类 move-only 类型。

## 10. 当前限制

- 不支持协程。
- 不支持真实 UI 同步上下文。
- `PreferFairness` 当前保留为兼容选项，没有强公平调度保证。
- `DenyChildAttach` 当前保留为兼容选项，因为没有父子任务 attach 模型。
- `TaskFactory` 的默认 continuation options 当前只作为配置保存，未自动应用到所有 continuation。
- `WhenAll` 对 move-only 结果类型仍不作为主路径支持，move-only 建议使用单任务 `TakeResult()`。

## 11. 测试方案

单元测试使用 gtest，目录：

```text
src/tests/icax/foundation/Task/TaskTest/
```

当前测试覆盖：

- `FromResult`、`FromException`、`FromCanceled`。
- `TaskCompletionSource`。
- 手动调度器和默认线程池调度器。
- cancellation token。
- continuation 条件、取消、lazy cancellation、隐藏调度器。
- `Delay`、`TimeoutAfter`、`WaitAsync`。
- `StartNew`、`TaskFactory`、`LongRunning`。
- `AsyncState`、`CreationOptions`、`Id`、`CurrentId`。
- linked cancellation、`CancelAfter`。
- `TakeResult`。
- `WaitAll`、`WaitAny`、`Unwrap`。

验证命令：

```powershell
& 'C:\Program Files\PowerShell\7\pwsh.exe' -ExecutionPolicy Bypass -File src\tools\build\run_tests_debug_x64.ps1
& 'C:\Program Files\PowerShell\7\pwsh.exe' -ExecutionPolicy Bypass -File src\tools\build\build_debug_x64.ps1
```

当前验证结果：

- Task 单元测试 32/32 通过。
- Debug x64 全工程编译通过。

## 12. 后续演进

推荐演进顺序：

1. 为 `TaskFactory` 的 continuation options 增加更完整的使用入口。
2. 明确 `PreferFairness` 是否需要真实队列策略。
3. 如果未来需要 UI pump，再把 `FromCurrentSynchronizationContext()` 与 frontend 或 backend 主循环桥接。
4. 如果未来需要 move-only 组合结果，再重构 `WhenAll` 的结果收集容器。
5. 保持无协程版本稳定后，再单独评估是否需要 coroutine adapter。

