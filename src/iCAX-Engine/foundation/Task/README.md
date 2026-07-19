# Task

`Task` 是 foundation 中的异步任务基础工程，目标是为 C++ 上层代码提供接近 C# `Task` 的调用体验。

本工程只依赖 C++ 标准库，不依赖 `services`、Facade 调用、`pdo` 或插件框架。

## 目录结构

- `Task.h`：`Task<T>`、`Task<void>`、`TaskCompletionSource<T>`、`TaskFactory`、`Run`、`StartNew`、`Delay`、`TimeoutAfter`、`WaitAsync`、`WhenAll`、`WhenAny`、`WaitAll`、`WaitAny`、`Unwrap`。
- `CancellationToken.h`：取消令牌、取消源、取消回调注册、`CancelAfter`、linked cancellation。
- `TaskContinuationOptions.h`：条件 continuation 选项。
- `TaskCreationOptions.h`：任务创建选项，包括 `LongRunning`、`HideScheduler`、`RunContinuationsAsynchronously` 等。
- `TaskScheduler.h`：任务调度接口、默认调度器、当前线程调度器、内联调度器、线程池调度器、事件循环调度器、轻量计时器。
- `TaskStatus.h`：任务状态、取消异常、超时异常、聚合异常。
- `TaskDefines.h`：DLL 导入导出宏。
- `TaskScheduler.cpp`：内联调度器和线程池调度器的默认实例。
- `Coroutine.h/.cpp`：C++20 Coroutine frame、`CCoroutineRuntime` 逐帧运行器以及 Task await 适配。

Coroutine 是 Task 之上的调度适配，不改变 Task 的一次性异步结果语义。Task 可以在线程池真正并行执行；Coroutine 由其 owner 的 event loop scheduler 驱动，适合固定线程内跨多帧执行。

每个 Task 在创建时绑定一个 scheduler。未显式指定时使用 `DefaultScheduler()`，当前实现为共享线程池。无 scheduler 参数的 `ContinueWith()` 自动继承前序 Task 的 scheduler；显式传入 scheduler 的重载会覆盖该 continuation 的调度位置，并让返回的 Task 继续继承这个新 scheduler。

## 调用示例

```cpp
#include <Task/Task.h>

using namespace iCAX::Tasks;

auto task = Run([] {
    return 21;
});

auto next = task.ContinueWith([](Task<int> completed) {
    return completed.Result() * 2;
});

int value = next.Result();
```

## Coroutine

`CCoroutineRuntime` 必须绑定 owner 的 `EventLoopTaskScheduler`。Runtime 只负责启动、逐帧推进、暂停、恢复和取消协程，不理解 Component、页面等业务 owner。外层保存返回的 handle，并在自己的禁用、启用、销毁事件中显式调用 `Pause()`、`Resume()`、`Cancel()`；整体关闭时可调用 `CancelAll()`。Task 完成回调只向 runtime scheduler 投递唤醒，不会在完成 Task 的线程直接恢复 coroutine。

```cpp
using namespace iCAX::Coroutines;

CCoroutine<int> Load(Task<int> valueTask)
{
    co_await NextFrame();
    co_await WaitForSeconds(0.25);
    co_await WaitUntil([] { return IsReady(); });
    const int value = co_await Await(std::move(valueTask));
    Apply(value);
    co_return value;
}

CCoroutineRuntime runtime(scheduler);
auto handle = runtime.Start(Load(task));

// owner event loop，每帧一次
scheduler->RunAll();
runtime.Tick(deltaTime, totalTime);

runtime.Cancel(handle);
```

`CCoroutine<TResult>` 与 `CCoroutineHandle<TResult>` 保持强类型。`co_return value` 会完成 `handle.Completion()` 返回的 `Task<TResult>`；无返回值流程使用 `CCoroutine<>`。异常映射为 faulted Task，handle 取消映射为 canceled Task。move-only 返回值通过 completion Task 的 `TakeResult()` 取得。

`NextFrame()` 最多在下一次 runtime tick 恢复；`WaitForSeconds()` 使用 runtime 接收的累计时间；`WaitUntil()` 每帧检查；`Await(Task<T>)` 将恢复重新排入 runtime scheduler。`Pause(handle)` 会冻结该协程，`Resume(handle)` 后才继续推进。

## 创建任务

`Run` 用于最常见的后台任务；`StartNew` 和 `TaskFactory` 提供更接近 C# 的创建选项、状态对象和调度器控制。

```cpp
auto scheduler = std::make_shared<EventLoopTaskScheduler>();
auto state = std::make_shared<int>(7);

TaskFactory factory(
    CancellationToken::None(),
    TaskCreationOptions::HideScheduler,
    TaskContinuationOptions::None,
    scheduler);

auto task = factory.StartNew([] {
    return 1;
}, state);

scheduler->RunAll();
auto sameState = std::static_pointer_cast<int>(task.AsyncState());
```

## 取消

任务体可以选择接收 `CancellationToken`：

```cpp
CancellationTokenSource source;

auto task = Run([](CancellationToken token) {
    token.ThrowIfCancellationRequested();
    return 1;
}, source.Token());

source.Cancel();
```

也可以组合多个取消源，或使用 `CancelAfter`：

```cpp
CancellationTokenSource owner;
CancellationTokenSource timeout;
auto linked = CancellationTokenSource::CreateLinkedTokenSource(owner.Token(), timeout.Token());

timeout.CancelAfter(std::chrono::milliseconds(100));
linked.Token().ThrowIfCancellationRequested();
```

## 条件 continuation

```cpp
auto handled = task.ContinueWith(
    [](Task<int> completed) {
        return completed.Exception() != nullptr;
    },
    TaskContinuationOptions::OnlyOnFaulted);
```

需要显式取消 continuation 时，可以传入 `CancellationToken`：

```cpp
CancellationTokenSource source;
source.Cancel();

auto skipped = task.ContinueWith(
    [](Task<int>) { return 1; },
    TaskContinuationOptions::None,
    DefaultScheduler(),
    source.Token());
```

`TaskContinuationOptions::LazyCancellation` 会让 continuation 等前序任务完成后再进入取消状态。

## 超时和延迟

```cpp
auto delayed = Delay(std::chrono::milliseconds(100));
delayed.Wait();

auto guarded = TimeoutAfter(task, std::chrono::seconds(1));
auto value = guarded.Result();
```

## 组合和等待

```cpp
auto all = WhenAll<int>({
    Task<int>::FromResult(1),
    Task<int>::FromResult(2)
});

WaitAll<int>({
    Task<int>::FromResult(1),
    Task<int>::FromResult(2)
});

auto index = WaitAny<int>({
    Task<int>::FromResult(1),
    Task<int>::FromResult(2)
});

auto nested = Task<Task<int>>::FromResult(Task<int>::FromResult(42));
int value = Unwrap(nested).Result();
```

对于 Engine/UI 这类单线程 event loop，可以使用 `EventLoopTaskScheduler`。构造初始 TaskSource 时绑定一次即可，后续 continuation 自动继承：

```cpp
auto scheduler = std::make_shared<EventLoopTaskScheduler>();

TaskCompletionSource<int> source(scheduler);

auto task = source.GetTask();
auto next = task.ContinueWith([](Task<int> completed) {
    return completed.Result() + 1;
});

source.SetResult(41);
scheduler->RunAll();
int value = next.Result();
```
