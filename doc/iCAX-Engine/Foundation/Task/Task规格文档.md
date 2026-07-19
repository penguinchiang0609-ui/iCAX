# Task 规格文档

## 1. 定位

`Task` 是 iCAX Engine Foundation 层的 C++ 异步任务基础库，用于表达一次可能尚未完成的计算、延迟、等待、取消或异常结果。

它面向 C++ 后台代码，提供尽量接近 C# `Task` 的调用体验，并提供显式 `Await(Task<T>)` 的 C++20 Coroutine 适配；当前不提供 Task 自身的隐式 `co_await`、任务层级 attach、同步上下文捕获或 UI 线程消息泵。

## 2. 设计目标

- 为上层 C++ 代码提供统一的异步结果模型。
- 支持 `Task<T>` 与 `Task<void>`。
- 支持手动完成、后台执行、延迟、超时、取消、组合等待和 continuation。
- API 命名和使用习惯尽量贴近 C# `Task` / `TaskCompletionSource`。
- 只依赖 C++ 标准库，保持 Foundation 层独立。
- Coroutine runtime 只依赖抽象 scheduler 与显式 owner scope，不依赖 backend/frontend 线程模型，也不直接依赖 Facade 调用或 PDO。

## 3. 非目标

- 不让 Task 隐式捕获线程或同步上下文；Coroutine 必须由显式 manager/scope 驱动。
- 不提供 C# `async/await` 编译器级状态机。
- 不提供 `SynchronizationContext` 的真实 UI 消息循环语义。
- 不提供父子任务 attach 语义。
- 不提供 `ValueTask`、`IAsyncEnumerable` 或 async stream。
- 不负责 backend 与 frontend bridge 的 Facade 调用/PDO 通信。

## 4. 公开能力

### 4.1 Task 类型

- `Task<T>`：表示一个返回 `T` 的任务。
- `Task<void>`：表示一个无返回值任务。

任务支持：

- `Id()`：返回任务唯一标识。
- `Status()`：返回当前状态。
- `IsCompleted()`：是否进入终态。
- `IsCompletedSuccessfully()`：是否成功完成。
- `IsFaulted()`：是否异常完成。
- `IsCanceled()`：是否取消完成。
- `Wait()` / `WaitFor()` / `WaitUntil()`：阻塞等待。
- `Result()`：等待并返回结果，异常或取消时抛出。
- `ResultRef()`：返回结果引用，仅 `Task<T>` 支持。
- `TakeResult()`：移动取出结果，仅 `Task<T>` 支持。
- `Exception()`：返回任务保存的异常。
- `CurrentId()`：返回当前执行中的任务 Id，无任务上下文时返回 `0`。

### 4.2 TaskCompletionSource

`TaskCompletionSource<T>` 与 `TaskCompletionSource<void>` 用于外部手动控制任务完成。

支持：

- `GetTask()`：获取受控任务。
- `SetResult()` / `TrySetResult()`：设置成功结果。
- `SetException()` / `TrySetException()`：设置异常。
- `SetCanceled()` / `TrySetCanceled()`：设置取消。
- `MarkRunning()`：标记任务进入 running 状态。
- 构造时可只传入 `TaskScheduler`，也可同时传入 `AsyncState`、`TaskCreationOptions` 与 `TaskScheduler`。
- 未指定 scheduler 时绑定 `DefaultScheduler()`；指定 scheduler 后，返回的 Task 默认使用该 scheduler 调度 continuation。

### 4.3 任务创建

- `Run(work)`：在默认调度器上执行工作。
- `Run(work, scheduler)`：在指定调度器上执行工作。
- `Run(work, cancellationToken)`：使用默认调度器并支持取消。
- `Run(work, scheduler, cancellationToken)`：指定调度器并支持取消。
- `StartNew(work, token, creationOptions, scheduler, asyncState)`：支持更完整的创建选项；省略 scheduler 时使用 `DefaultScheduler()`，不会隐式继承当前线程的 scheduler。
- `TaskFactory`：封装默认取消令牌、创建选项和调度器；`continuationOptions` 当前保存并暴露为配置，不会自动套用到普通 `ContinueWith`。

工作函数可选择接收 `CancellationToken`。如果可调用对象签名支持 `CancellationToken`，Task 会把令牌传入；否则按无参函数调用。

`TaskCreationOptions` 当前已生效：

- `LongRunning`：使用独立线程执行。
- `HideScheduler`：任务体中隐藏外部调度器。
- `RunContinuationsAsynchronously`：任务完成后异步调度 continuation。

`TaskCreationOptions` 当前保留：

- `PreferFairness`：当前不提供强公平调度保证。
- `DenyChildAttach`：当前没有父子任务 attach 模型，因此仅作为兼容标志保留。

### 4.4 静态完成任务

- `Task<T>::FromResult(value)`：创建成功完成任务。
- `Task<T>::FromException(exception)`：创建异常完成任务。
- `Task<T>::FromException(vector<exception_ptr>)`：创建聚合异常任务。
- `Task<T>::FromCanceled()`：创建取消任务。
- `Task<T>::FromCanceled(token)`：创建取消任务。
- `Task<void>::CompletedTask()`：创建成功完成的 void 任务。

### 4.5 Continuation

`ContinueWith` 支持：

- 指定 continuation 函数。
- 指定 `TaskContinuationOptions`。
- 指定 `TaskScheduler`。
- 指定 `CancellationToken`。

每个 Task 都保存创建时绑定的 scheduler。`ContinueWith(continuation)` 和 `ContinueWith(continuation, options)` 默认继承前序 Task 的 scheduler；带 `scheduler` 参数的重载覆盖本次 continuation 的执行位置，且返回的 Task 继承被覆盖后的 scheduler，便于后续链式调用保持同一线程语义。

`TaskContinuationOptions` 包括：

- 条件执行：`OnlyOnRanToCompletion`、`OnlyOnFaulted`、`OnlyOnCanceled`。
- 排除执行：`NotOnRanToCompletion`、`NotOnFaulted`、`NotOnCanceled`。
- 执行方式：`ExecuteSynchronously`、`RunContinuationsAsynchronously`。
- 取消方式：`LazyCancellation`。
- 调度隐藏：`HideScheduler`。
- 兼容保留：`DenyChildAttach`。

`LazyCancellation` 的语义是：如果 continuation 的取消令牌已经取消，continuation 不会提前进入终态，而是在前序任务完成后再进入取消状态。

### 4.6 调度器

支持的调度器：

- `InlineScheduler()`：同步内联执行。
- `ThreadPoolScheduler()`：共享线程池执行。
- `BackgroundScheduler()`：当前等同于共享线程池。
- `DefaultScheduler()`：默认调度器，当前等同于共享线程池。
- `CurrentScheduler()`：返回当前线程可见调度器，无显式设置时返回默认调度器。
- `FromCurrentSynchronizationContext()`：当前返回 `CurrentScheduler()`，为后续同步上下文扩展保留。
- `EventLoopTaskScheduler`：只做线程安全入队、不创建线程，由 Engine/UI 等单线程 event loop 调用 `RunOne()` / `RunAll()` 消费。
- `CurrentTaskSchedulerScope`：在当前作用域内设置可见调度器。

### 4.7 取消

`CancellationTokenSource` 支持：

- `Token()`：获取取消令牌。
- `Cancel()`：请求取消。
- `CancelAfter(duration)`：延迟请求取消。
- `Dispose()`：释放注册回调和 linked registration。
- `CreateLinkedTokenSource(tokens)`：组合多个取消令牌。

`CancellationToken` 支持：

- `CanBeCanceled()`。
- `IsCancellationRequested()`。
- `ThrowIfCancellationRequested()`。
- `Register(callback)`。
- `None()`。

### 4.8 时间与组合

- `Delay(duration, token)`：延迟完成，可取消。
- `TimeoutAfter(task, timeout)`：任务超时包装。
- `WaitAsync(task, token)`：等待任务并支持外部取消。
- `WhenAll(tasks)`：等待全部任务。
- `WhenAny(tasks)`：等待任意任务。
- `WaitAll(tasks)`：同步等待全部任务并传播异常。
- `WaitAny(tasks)`：同步等待任意任务并返回完成任务索引。
- `Unwrap(taskOfTask)`：展开嵌套任务。

### 4.9 Coroutine runtime

- `CCoroutine<TResult>`：冷启动、move-only 的 C++20 coroutine frame；无返回值时使用 `CCoroutine<>`。
- `CCoroutineRuntime`：由 owner event loop 每帧调用 `Tick(deltaTime, totalTime)` 推进，并按 handle 暂停、恢复或取消协程。
- `CCoroutineHandle<TResult>`：控制单个协程，并通过 `Completion()` 暴露 `Task<TResult>`。
- `NextFrame()`、`WaitForSeconds()`、`WaitUntil()`：逐帧等待器。
- `Await(Task<T>)`：显式等待 Task。Task 完成后只向 runtime scheduler 投递唤醒，不能在 Task 完成线程直接恢复 coroutine。

runtime 使用的 scheduler 必须与其 owner event loop 串行消费。Foundation 不识别 Universe、Component、WPF 或 CEF；上层保存 handle，并把自身生命周期事件翻译为 `Pause`、`Resume` 和 `Cancel`。

`co_return value` 完成 typed completion Task；未处理异常使其 faulted；handle 取消使其 canceled。move-only 结果受支持，调用方通过 completion Task 的 `TakeResult()` 移出。

## 5. 状态模型

任务状态包括：

- `Created`：已创建，尚未运行或完成。
- `Running`：正在运行。
- `RanToCompletion`：成功完成。
- `Faulted`：异常完成。
- `Canceled`：取消完成。

`RanToCompletion`、`Faulted`、`Canceled` 是终态。任务一旦进入终态，不允许再次变更状态。

## 6. 异常模型

- 任务体抛出的普通异常被捕获并保存到任务中。
- `Result()` 在任务异常完成时重新抛出保存的异常。
- `TaskCanceledException` 表示取消。
- `TaskTimeoutException` 表示超时。
- 多个任务同时异常时使用 `TaskAggregateException` 聚合。

## 7. 线程模型

`Task` 内部使用 mutex、condition_variable 和 atomic 保护状态。

本库允许任务在多个 C++ 线程之间使用，但它不改变 iCAX 的主体执行模型。当前 backend 由 ApplicationRuntime、ProductRuntime 和 Project 组织；ApplicationRuntime 拥有应用级工作线程，每个 Project 拥有自己的项目线程，前端通过 bridge 使用 Facades、PDO 和 Resource 与 backend 交互。

Task 只作为 Foundation 能力提供，不作为 backend/frontend 通信协议。

## 8. 上层使用样例

以下样例面向上层 C++ 业务代码，默认包含：

```cpp
#include <Task/Task.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace iCAX::Tasks;
```

### 8.1 后台执行并取得结果

适用于把一个普通 C++ 函数提交到默认后台调度器，并在需要结果的位置等待。

```cpp
Task<double> task = Run([] {
    return 12.0 * 3.5;
});

double value = task.Result();
```

如果任务体抛出异常，异常会被保存到任务中，并在 `Result()` 时重新抛出：

```cpp
Task<int> task = Run([]() -> int {
    throw std::runtime_error("load failed");
});

try
{
    int value = task.Result();
}
catch (const std::runtime_error&)
{
    // 在这里处理业务异常。
}
```

### 8.2 后台执行后追加处理

适用于把一个计算结果继续传递给后续处理逻辑。

```cpp
auto loadTask = Run([] {
    return 21;
});

auto convertTask = loadTask.ContinueWith([](Task<int> completed) {
    return completed.Result() * 2;
});

int value = convertTask.Result(); // 42
```

只在前序任务异常时处理：

```cpp
auto task = Task<int>::FromException(
    std::make_exception_ptr(std::runtime_error("bad data")));

auto handled = task.ContinueWith(
    [](Task<int> completed) {
        return completed.Exception() != nullptr;
    },
    TaskContinuationOptions::OnlyOnFaulted);

bool hasError = handled.Result();
```

在 continuation 中也可以判断前序任务是成功、取消还是失败，并在失败时读取异常：

```cpp
auto task = Run([]() -> int {
    throw std::runtime_error("load failed");
});

auto handled = task.ContinueWith([](Task<int> completed) {
    if (completed.IsCompletedSuccessfully())
    {
        int value = completed.Result();
        return value;
    }

    if (completed.IsCanceled())
    {
        return -1;
    }

    if (completed.IsFaulted())
    {
        try
        {
            std::rethrow_exception(completed.Exception());
        }
        catch (const std::runtime_error& ex)
        {
            // 这里可以记录 ex.what()，或者转换成业务错误码。
            return -2;
        }
    }

    return 0;
});

int result = handled.Result();
```

这段代码对应 C# 中常见的写法：

```csharp
Task<int> handled = task.ContinueWith(completed =>
{
    if (completed.IsCompletedSuccessfully)
    {
        return completed.Result;
    }

    if (completed.IsCanceled)
    {
        return -1;
    }

    if (completed.IsFaulted)
    {
        Exception error = completed.Exception;
        return -2;
    }

    return 0;
});
```

### 8.3 手动完成一个异步操作

适用于上层把“外部事件完成”包装成 Task，例如等待某个消息返回、等待某个回调触发。

```cpp
class SaveOperation final
{
public:
    Task<bool> Start()
    {
        return m_source.GetTask();
    }

    void OnSaveFinished(bool ok)
    {
        m_source.TrySetResult(ok);
    }

    void OnSaveFailed(std::exception_ptr error)
    {
        m_source.TrySetException(error);
    }

private:
    TaskCompletionSource<bool> m_source;
};

SaveOperation operation;
Task<bool> task = operation.Start();

operation.OnSaveFinished(true);
bool ok = task.Result();
```

### 8.4 支持取消的任务

适用于耗时任务需要响应用户取消、命令取消或关闭流程。

```cpp
CancellationTokenSource source;

auto task = Run(
    [](CancellationToken token) {
        for (int index = 0; index < 100; ++index)
        {
            token.ThrowIfCancellationRequested();
            // 执行业务步骤。
        }

        return true;
    },
    source.Token());

source.Cancel();

try
{
    bool ok = task.Result();
}
catch (const TaskCanceledException&)
{
    // 任务已取消。
}
```

### 8.5 超时控制

适用于上层等待一个任务，但不希望无限期阻塞。

```cpp
auto slowTask = Run([] {
    std::this_thread::sleep_for(5s);
    return 1;
});

auto guarded = TimeoutAfter(slowTask, 100ms);

try
{
    int value = guarded.Result();
}
catch (const TaskTimeoutException&)
{
    // 超时处理。
}
```

也可以只等待取消令牌，不改变原任务本身：

```cpp
CancellationTokenSource source;
auto task = Run([] {
    std::this_thread::sleep_for(5s);
    return 1;
});

auto waitTask = WaitAsync(task, source.Token());
source.CancelAfter(100ms);

try
{
    int value = waitTask.Result();
}
catch (const TaskCanceledException&)
{
    // 等待过程被取消，原 task 是否继续执行由其自身逻辑决定。
}
```

### 8.6 多个任务全部完成

适用于批量计算、批量加载或多个独立步骤全部完成后继续。

```cpp
auto all = WhenAll<int>({
    Run([] { return 1; }),
    Run([] { return 2; }),
    Run([] { return 3; })
});

std::vector<int> values = all.Result();
```

如果多个任务异常，结果任务会抛出 `TaskAggregateException`：

```cpp
auto all = WhenAll<int>({
    Task<int>::FromException(std::make_exception_ptr(std::runtime_error("a"))),
    Task<int>::FromException(std::make_exception_ptr(std::runtime_error("b")))
});

try
{
    auto values = all.Result();
}
catch (const TaskAggregateException& ex)
{
    auto count = ex.Exceptions().size();
}
```

### 8.7 多个任务任意一个完成

适用于抢占式等待，例如多个数据源谁先返回就先使用谁。

```cpp
auto first = WhenAny<int>({
    Run([] { return 10; }),
    Run([] { return 20; })
});

Task<int> completed = first.Result();
int value = completed.Result();
```

需要同步等待索引时：

```cpp
std::size_t index = WaitAny<int>({
    Task<int>::FromResult(10),
    Task<int>::FromResult(20)
});
```

### 8.8 单线程主循环中手动调度

适用于需要显式手动泵的单线程主循环或单元测试中，不希望任务自动在线程池运行。

```cpp
auto scheduler = std::make_shared<EventLoopTaskScheduler>();

auto task = Run([] {
    return 42;
}, scheduler);

// 主循环中某个安全位置执行。
scheduler->RunAll();

int value = task.Result();
```

初始 Task 绑定 event-loop scheduler 后，continuation 会自动继承，不需要逐次传参：

```cpp
auto scheduler = std::make_shared<EventLoopTaskScheduler>();

TaskCompletionSource<int> source(scheduler);
auto task = source.GetTask();

auto next = task.ContinueWith(
    [](Task<int> completed) {
        return completed.Result() * 2;
    });

source.SetResult(21);
scheduler->RunAll();
int value = next.Result();
```

### 8.9 使用 TaskFactory 固化默认策略

适用于某个模块内希望统一调度器、取消令牌和创建选项。

```cpp
auto scheduler = std::make_shared<EventLoopTaskScheduler>();
auto state = std::make_shared<int>(1001);

TaskFactory factory(
    CancellationToken::None(),
    TaskCreationOptions::HideScheduler,
    TaskContinuationOptions::None,
    scheduler);

auto task = factory.StartNew([] {
    return CurrentScheduler() == DefaultScheduler();
}, state);

scheduler->RunAll();

bool hidden = task.Result();
auto taskState = std::static_pointer_cast<int>(task.AsyncState());
```

### 8.10 组合取消源

适用于一个任务同时受用户取消、超时取消和宿主关闭取消控制。

```cpp
CancellationTokenSource userCancel;
CancellationTokenSource timeoutCancel;
CancellationTokenSource shutdownCancel;

auto linked = CancellationTokenSource::CreateLinkedTokenSource(
    userCancel.Token(),
    timeoutCancel.Token(),
    shutdownCancel.Token());

timeoutCancel.CancelAfter(500ms);

auto task = Run(
    [](CancellationToken token) {
        token.ThrowIfCancellationRequested();
        return true;
    },
    linked.Token());
```

### 8.11 展开嵌套任务

适用于某个异步步骤返回另一个异步步骤。

C# 中这类场景通常写成：

```csharp
Task<Task<int>> nested = Task.Run(() =>
{
    return Task.Run(() => 42);
});

Task<int> task = nested.Unwrap();
int value = task.Result;
```

iCAX C++ Task 对应使用全局 `Unwrap` 函数：

```cpp
Task<Task<int>> nested = Run([] {
    return Run([] {
        return 42;
    });
});

Task<int> task = Unwrap(nested);
int value = task.Result();
```

### 8.12 移动结果

适用于返回不可拷贝对象，例如 `std::unique_ptr<T>`。

```cpp
auto task = Task<std::unique_ptr<int>>::FromResult(
    std::make_unique<int>(42));

std::unique_ptr<int> value = task.TakeResult();
```

## 9. 与 C# Task 的差异

当前已对齐的能力：

- `Task<T>` / `Task<void>`。
- `TaskCompletionSource`。
- `Run` / `StartNew` / `TaskFactory`。
- continuation 与 continuation options。
- `Delay`、`WhenAll`、`WhenAny`、`WaitAll`、`WaitAny`、`Unwrap`。
- cancellation token、linked token、`CancelAfter`。
- `AsyncState`、`Id`、`CurrentId`、创建选项。

当前保留差异：

- 不支持 `async/await` 和 coroutine。
- 不支持真实 `SynchronizationContext` 捕获。
- 不支持父子任务 attach。
- 不支持 `ConfigureAwait`。
- 不支持 async stream。
- 不提供 .NET Runtime 级别的任务调度公平性保证。
- `TaskFactory` 暂不提供 C# `TaskFactory` 中的 `ContinueWhenAll` / `ContinueWhenAny` 等工厂 continuation API。

## 10. 测试要求

Task 的单元测试位于 `src/tests/icax-engine/foundation/Task/TaskTest/`。

测试应覆盖：

- 成功、异常、取消三种终态。
- `TaskCompletionSource` 手动完成。
- 默认调度器、手动调度器、当前调度器作用域。
- `Run`、`StartNew`、`TaskFactory`。
- `ContinueWith` 条件、取消、lazy cancellation、隐藏调度器。
- `Delay`、`TimeoutAfter`、`WaitAsync`。
- `WhenAll`、`WhenAny`、`WaitAll`、`WaitAny`、`Unwrap`。
- linked cancellation 与 `CancelAfter`。
- 移动结果 `TakeResult`。
