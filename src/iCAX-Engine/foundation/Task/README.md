# Task

`Task` 是 foundation 中的异步任务基础工程，目标是为 C++ 上层代码提供接近 C# `Task` 的调用体验。

本工程只依赖 C++ 标准库，不依赖 `services`、Mail 通信、`pdo` 或插件框架。

## 目录结构

- `Task.h`：`Task<T>`、`Task<void>`、`TaskCompletionSource<T>`、`TaskFactory`、`Run`、`StartNew`、`Delay`、`TimeoutAfter`、`WaitAsync`、`WhenAll`、`WhenAny`、`WaitAll`、`WaitAny`、`Unwrap`。
- `CancellationToken.h`：取消令牌、取消源、取消回调注册、`CancelAfter`、linked cancellation。
- `TaskContinuationOptions.h`：条件 continuation 选项。
- `TaskCreationOptions.h`：任务创建选项，包括 `LongRunning`、`HideScheduler`、`RunContinuationsAsynchronously` 等。
- `TaskScheduler.h`：任务调度接口、默认调度器、当前线程调度器、内联调度器、线程池调度器、手动调度器、轻量计时器。
- `TaskStatus.h`：任务状态、取消异常、超时异常、聚合异常。
- `TaskDefines.h`：DLL 导入导出宏。
- `TaskScheduler.cpp`：内联调度器和线程池调度器的默认实例。

当前只提供普通 `Task` API，不做语言级异步语法扩展。

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

## 创建任务

`Run` 用于最常见的后台任务；`StartNew` 和 `TaskFactory` 提供更接近 C# 的创建选项、状态对象和调度器控制。

```cpp
auto scheduler = std::make_shared<ManualTaskScheduler>();
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

对于单线程主循环，可以使用 `ManualTaskScheduler`：

```cpp
auto scheduler = std::make_shared<ManualTaskScheduler>();

auto task = Run([] {
    return 1;
}, scheduler);

scheduler->RunAll();
int value = task.Result();
```
