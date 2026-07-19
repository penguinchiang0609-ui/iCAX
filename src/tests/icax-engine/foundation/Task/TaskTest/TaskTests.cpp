#include "pch.h"


#include <Task/Task.h>
#include <Task/Coroutine.h>


using namespace std::chrono_literals;
using namespace iCAX::Coroutines;
using namespace iCAX::Tasks;

namespace
{
    CCoroutine<> RunAcrossTwoFrames(int& count_)
    {
        ++count_;
        co_await NextFrame();
        ++count_;
    }

    CCoroutine<> RunAfterDelay(int& count_)
    {
        ++count_;
        co_await WaitForSeconds(0.5);
        ++count_;
    }

    CCoroutine<> RunWhenReady(bool& ready_, int& count_)
    {
        ++count_;
        co_await WaitUntil([&ready_] { return ready_; });
        ++count_;
    }

    CCoroutine<> RunAfterTask(Task<int> task_, int& result_)
    {
        result_ = co_await Await(std::move(task_));
    }

    struct CCoroutineLifetimeProbe final
    {
        bool* pDestroyed = nullptr;

        ~CCoroutineLifetimeProbe()
        {
            if (pDestroyed)
            {
                *pDestroyed = true;
            }
        }
    };

    CCoroutine<> HoldUntilTask(Task<void> task_, bool& frameDestroyed_)
    {
        CCoroutineLifetimeProbe probe{ &frameDestroyed_ };
        co_await Await(std::move(task_));
    }

    CCoroutine<> ThrowFromCoroutine()
    {
        throw std::runtime_error("coroutine failure");
        co_return;
    }

    CCoroutine<int> ReturnValueAfterFrame()
    {
        co_await NextFrame();
        co_return 42;
    }

    CCoroutine<std::unique_ptr<int>> ReturnMoveOnlyValue()
    {
        co_return std::make_unique<int>(17);
    }

    CCoroutine<int> ThrowFromResultCoroutine()
    {
        throw std::runtime_error("typed coroutine failure");
        co_return 0;
    }

    CCoroutine<int> AwaitMoveOnlyTask(Task<std::unique_ptr<int>> task_)
    {
        auto value = co_await Await(std::move(task_));
        co_return value ? *value : 0;
    }
}

TEST(TaskTest, FromResultReturnsValue)
{
    auto task = Task<int>::FromResult(42);

    EXPECT_TRUE(task.IsCompleted());
    EXPECT_TRUE(task.IsCompletedSuccessfully());
    EXPECT_EQ(TaskStatus::RanToCompletion, task.Status());
    EXPECT_EQ(42, task.Result());
}

TEST(TaskTest, TaskCompletionSourceControlsTaskCompletion)
{
    TaskCompletionSource<int> source;
    auto task = source.GetTask();

    EXPECT_FALSE(task.IsCompleted());
    source.SetResult(7);

    EXPECT_TRUE(task.IsCompleted());
    EXPECT_EQ(7, task.Result());
}

TEST(TaskTest, TaskCompletionSourceUsesDefaultScheduler)
{
    TaskCompletionSource<int> source;
    auto task = source.GetTask();

    EXPECT_EQ(DefaultScheduler(), source.Scheduler());
    EXPECT_EQ(DefaultScheduler(), task.Scheduler());
}

TEST(TaskTest, TaskCompletionSourceSchedulerIsInheritedByContinueWith)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    TaskCompletionSource<int> source(scheduler);
    auto task = source.GetTask();
    auto continuation = task.ContinueWith(
        [](Task<int> completed) {
            return completed.Result() + 1;
        });

    source.SetResult(41);

    EXPECT_EQ(scheduler, task.Scheduler());
    EXPECT_EQ(scheduler, continuation.Scheduler());
    EXPECT_FALSE(continuation.IsCompleted());
    EXPECT_EQ(1u, scheduler->RunAll());
    EXPECT_EQ(42, continuation.Result());
}

TEST(TaskTest, ContinueWithSchedulerOverridesTaskScheduler)
{
    auto taskScheduler = std::make_shared<EventLoopTaskScheduler>();
    auto continuationScheduler = std::make_shared<EventLoopTaskScheduler>();
    TaskCompletionSource<int> source(taskScheduler);
    auto continuation = source.GetTask().ContinueWith(
        [](Task<int> completed) {
            return completed.Result() * 2;
        },
        continuationScheduler);

    source.SetResult(3);

    EXPECT_EQ(0u, taskScheduler->PendingCount());
    EXPECT_EQ(1u, continuationScheduler->PendingCount());
    EXPECT_EQ(continuationScheduler, continuation.Scheduler());
    EXPECT_EQ(1u, continuationScheduler->RunAll());
    EXPECT_EQ(6, continuation.Result());
}

TEST(TaskTest, EventLoopSchedulerRunsQueuedWork)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();

    auto task = iCAX::Tasks::Run([] { return 21; }, scheduler);
    auto next = task.ContinueWith(
        [](Task<int> completed) {
            return completed.Result() * 2;
        },
        scheduler);

    EXPECT_FALSE(next.IsCompleted());
    EXPECT_EQ(2u, scheduler->RunAll());
    EXPECT_EQ(42, next.Result());
}

TEST(TaskTest, ThreadPoolSchedulerRunsWorkByDefault)
{
    auto task = iCAX::Tasks::Run([] { return 5; });

    ASSERT_TRUE(task.WaitFor(2s));
    EXPECT_EQ(5, task.Result());
}

TEST(TaskTest, StartNewUsesDefaultSchedulerInsideCurrentSchedulerScope)
{
    auto eventLoopScheduler = std::make_shared<EventLoopTaskScheduler>();
    Task<int> task;

    {
        CurrentTaskSchedulerScope scope(eventLoopScheduler);
        task = StartNew([] {
            return CurrentScheduler() == DefaultScheduler() ? 1 : 0;
        });
    }

    EXPECT_EQ(DefaultScheduler(), task.Scheduler());
    EXPECT_TRUE(task.WaitFor(2s));
    EXPECT_EQ(1, task.Result());
    EXPECT_EQ(0u, eventLoopScheduler->PendingCount());
}

TEST(TaskTest, CancellationBeforeRunProducesCanceledTask)
{
    CancellationTokenSource source;
    source.Cancel();

    auto task = iCAX::Tasks::Run([] { return 1; }, source.Token());

    EXPECT_TRUE(task.IsCompleted());
    EXPECT_TRUE(task.IsCanceled());
    EXPECT_THROW(task.Result(), TaskCanceledException);
}

TEST(TaskTest, WorkCanObserveCancellationToken)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CancellationTokenSource source;

    auto task = iCAX::Tasks::Run(
        [](CancellationToken token) {
            token.ThrowIfCancellationRequested();
            return 1;
        },
        scheduler,
        source.Token());

    source.Cancel();
    scheduler->RunAll();

    EXPECT_TRUE(task.IsCanceled());
    EXPECT_THROW(task.Result(), TaskCanceledException);
}

TEST(TaskTest, ContinueWithCanRunOnlyOnFaulted)
{
    auto faulted = Task<int>::FromException(std::make_exception_ptr(std::runtime_error("boom")));

    auto handled = faulted.ContinueWith(
        [](Task<int> completed) {
            return completed.IsFaulted();
        },
        TaskContinuationOptions::OnlyOnFaulted);

    EXPECT_TRUE(handled.Result());
}

TEST(TaskTest, ContinueWithCancelsWhenConditionDoesNotMatch)
{
    auto faulted = Task<int>::FromException(std::make_exception_ptr(std::runtime_error("boom")));

    auto skipped = faulted.ContinueWith(
        [](Task<int>) {
            return 1;
        },
        TaskContinuationOptions::NotOnFaulted);

    ASSERT_TRUE(skipped.WaitFor(2s));
    EXPECT_TRUE(skipped.IsCanceled());
    EXPECT_THROW(skipped.Result(), TaskCanceledException);
}

TEST(TaskTest, WhenAllReturnsValuesInInputOrder)
{
    auto all = WhenAll<int>({
        Task<int>::FromResult(1),
        Task<int>::FromResult(2),
        Task<int>::FromResult(3)
    });

    const auto values = all.Result();

    ASSERT_EQ(3u, values.size());
    EXPECT_EQ(1, values[0]);
    EXPECT_EQ(2, values[1]);
    EXPECT_EQ(3, values[2]);
}

TEST(TaskTest, WhenAllAggregatesExceptions)
{
    auto all = WhenAll<int>({
        Task<int>::FromException(std::make_exception_ptr(std::runtime_error("a"))),
        Task<int>::FromException(std::make_exception_ptr(std::runtime_error("b")))
    });

    try
    {
        all.Result();
        FAIL() << "Expected TaskAggregateException.";
    }
    catch (const TaskAggregateException& ex)
    {
        EXPECT_EQ(2u, ex.Exceptions().size());
    }
}

TEST(TaskTest, WhenAnyReturnsFirstCompletedTask)
{
    auto first = Task<int>::FromResult(10);
    auto second = Task<int>::FromResult(20);

    auto any = WhenAny<int>({ first, second });

    EXPECT_EQ(10, any.Result().Result());
}

TEST(TaskTest, DelayCompletes)
{
    auto task = Delay(1ms);

    ASSERT_TRUE(task.WaitFor(1s));
    EXPECT_NO_THROW(task.Result());
}

TEST(TaskTest, DelayCanBeCanceled)
{
    CancellationTokenSource source;
    auto task = Delay(1s, source.Token());

    source.Cancel();

    ASSERT_TRUE(task.WaitFor(1s));
    EXPECT_TRUE(task.IsCanceled());
    EXPECT_THROW(task.Result(), TaskCanceledException);
}

TEST(TaskTest, TimeoutAfterFailsWhenTaskDoesNotCompleteInTime)
{
    auto task = TimeoutAfter(Delay(200ms), 1ms);

    ASSERT_TRUE(task.WaitFor(1s));
    EXPECT_THROW(task.Result(), TaskTimeoutException);
}

TEST(TaskTest, WaitAsyncCanBeCanceled)
{
    CancellationTokenSource source;
    auto task = WaitAsync(Delay(1s), source.Token());

    source.Cancel();

    ASSERT_TRUE(task.WaitFor(1s));
    EXPECT_TRUE(task.IsCanceled());
    EXPECT_THROW(task.Result(), TaskCanceledException);
}

TEST(TaskTest, TaskHasStableIdAndCurrentIdDuringExecution)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();

    auto task = StartNew(
        [] {
            return Task<int>::CurrentId();
        },
        CancellationToken::None(),
        TaskCreationOptions::None,
        scheduler);

    const auto id = task.Id();
    ASSERT_NE(0u, id);

    ASSERT_EQ(1u, scheduler->RunAll());
    EXPECT_EQ(id, task.Result());
}

TEST(TaskTest, TaskCompletionSourceStoresAsyncStateAndCreationOptions)
{
    auto state = std::make_shared<int>(9);
    TaskCompletionSource<int> source(
        state,
        TaskCreationOptions::RunContinuationsAsynchronously | TaskCreationOptions::DenyChildAttach);

    auto task = source.GetTask();
    source.SetResult(3);

    EXPECT_EQ(state, std::static_pointer_cast<int>(task.AsyncState()));
    EXPECT_TRUE(HasFlag(task.CreationOptions(), TaskCreationOptions::RunContinuationsAsynchronously));
    EXPECT_TRUE(HasFlag(task.CreationOptions(), TaskCreationOptions::DenyChildAttach));
    EXPECT_EQ(3, task.Result());
}

TEST(TaskTest, RunContinuationsAsynchronouslySchedulesStoredContinuations)
{
    const auto callerThread = std::this_thread::get_id();
    TaskCompletionSource<int> source(
        nullptr,
        TaskCreationOptions::RunContinuationsAsynchronously);

    auto continuation = source.GetTask().ContinueWith(
        [callerThread](Task<int> completed) {
            return completed.Result() == 1 && std::this_thread::get_id() != callerThread;
        },
        InlineScheduler());

    source.SetResult(1);

    ASSERT_TRUE(continuation.WaitFor(2s));
    EXPECT_TRUE(continuation.Result());
}

TEST(TaskTest, TaskFactoryUsesSchedulerAsyncStateAndHideScheduler)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    auto state = std::make_shared<int>(11);
    TaskFactory factory(
        CancellationToken::None(),
        TaskCreationOptions::HideScheduler,
        TaskContinuationOptions::None,
        scheduler);

    auto task = factory.StartNew(
        [] {
            return CurrentScheduler() == DefaultScheduler();
        },
        state);

    EXPECT_FALSE(task.IsCompleted());
    EXPECT_EQ(state, std::static_pointer_cast<int>(task.AsyncState()));
    EXPECT_TRUE(HasFlag(task.CreationOptions(), TaskCreationOptions::HideScheduler));

    ASSERT_EQ(1u, scheduler->RunAll());
    EXPECT_TRUE(task.Result());
}

TEST(TaskTest, StartNewLongRunningRunsWithoutEventLoopPump)
{
    auto task = StartNew(
        [] {
            return 17;
        },
        CancellationToken::None(),
        TaskCreationOptions::LongRunning);

    ASSERT_TRUE(task.WaitFor(2s));
    EXPECT_TRUE(HasFlag(task.CreationOptions(), TaskCreationOptions::LongRunning));
    EXPECT_EQ(17, task.Result());
}

TEST(TaskTest, TaskFactoryHonorsDefaultCancellationToken)
{
    CancellationTokenSource source;
    source.Cancel();

    TaskFactory factory(
        source.Token(),
        TaskCreationOptions::None,
        TaskContinuationOptions::None,
        InlineScheduler());

    auto task = factory.StartNew([] { return 1; });

    EXPECT_TRUE(task.IsCanceled());
    EXPECT_THROW(task.Result(), TaskCanceledException);
}

TEST(TaskTest, ContinueWithCanBeCanceledByToken)
{
    CancellationTokenSource source;
    source.Cancel();

    auto task = Task<int>::FromResult(1);
    auto continuation = task.ContinueWith(
        [](Task<int>) {
            return 2;
        },
        TaskContinuationOptions::None,
        InlineScheduler(),
        source.Token());

    EXPECT_TRUE(continuation.IsCanceled());
    EXPECT_THROW(continuation.Result(), TaskCanceledException);
}

TEST(TaskTest, ContinueWithLazyCancellationWaitsForAntecedentCompletion)
{
    TaskCompletionSource<int> source;
    CancellationTokenSource cancellation;
    cancellation.Cancel();

    auto continuation = source.GetTask().ContinueWith(
        [](Task<int>) {
            return 2;
        },
        TaskContinuationOptions::LazyCancellation,
        InlineScheduler(),
        cancellation.Token());

    EXPECT_FALSE(continuation.IsCompleted());

    source.SetResult(1);

    EXPECT_TRUE(continuation.IsCanceled());
    EXPECT_THROW(continuation.Result(), TaskCanceledException);
}

TEST(TaskTest, ContinueWithHideSchedulerExposesDefaultScheduler)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    auto task = Task<int>::FromResult(1);

    auto continuation = task.ContinueWith(
        [](Task<int>) {
            return CurrentScheduler() == DefaultScheduler();
        },
        TaskContinuationOptions::HideScheduler,
        scheduler);

    ASSERT_EQ(1u, scheduler->RunAll());
    EXPECT_TRUE(continuation.Result());
}

TEST(TaskTest, LinkedCancellationSourceCancelsWhenAnyInputCancels)
{
    CancellationTokenSource owner;
    CancellationTokenSource timeout;
    auto linked = CancellationTokenSource::CreateLinkedTokenSource(owner.Token(), timeout.Token());

    EXPECT_FALSE(linked.IsCancellationRequested());

    timeout.Cancel();

    EXPECT_TRUE(linked.IsCancellationRequested());
}

TEST(TaskTest, CancelAfterRequestsCancellationThroughTimer)
{
    CancellationTokenSource source;
    source.CancelAfter(1ms);

    auto task = WaitAsync(Delay(1s), source.Token());

    ASSERT_TRUE(task.WaitFor(1s));
    EXPECT_TRUE(source.IsCancellationRequested());
    EXPECT_TRUE(task.IsCanceled());
}

TEST(TaskTest, FromExceptionCanStoreAggregateException)
{
    std::vector<std::exception_ptr> exceptions;
    exceptions.push_back(std::make_exception_ptr(std::runtime_error("one")));
    exceptions.push_back(std::make_exception_ptr(std::runtime_error("two")));

    auto task = Task<int>::FromException(std::move(exceptions));

    try
    {
        task.Result();
        FAIL() << "Expected TaskAggregateException.";
    }
    catch (const TaskAggregateException& ex)
    {
        EXPECT_EQ(2u, ex.Exceptions().size());
    }
}

TEST(TaskTest, FromCanceledCreatesCanceledTask)
{
    CancellationTokenSource source;
    source.Cancel();

    auto task = Task<int>::FromCanceled(source.Token());

    EXPECT_TRUE(task.IsCanceled());
    EXPECT_THROW(task.Result(), TaskCanceledException);
}

TEST(TaskTest, TakeResultSupportsMoveOnlyValues)
{
    auto task = Task<std::unique_ptr<int>>::FromResult(std::make_unique<int>(29));

    auto value = task.TakeResult();

    ASSERT_TRUE(value);
    EXPECT_EQ(29, *value);
}

TEST(TaskTest, WaitAllAndWaitAnySupportInitializerLists)
{
    EXPECT_NO_THROW(WaitAll<int>({
        Task<int>::FromResult(1),
        Task<int>::FromResult(2)
    }));

    const auto completedIndex = WaitAny<int>({
        Task<int>::FromResult(3),
        Task<int>::FromResult(4)
    });

    EXPECT_EQ(0u, completedIndex);
}

TEST(TaskTest, UnwrapReturnsInnerTaskResult)
{
    auto nested = Task<Task<int>>::FromResult(Task<int>::FromResult(42));
    auto unwrapped = Unwrap(nested);

    EXPECT_EQ(42, unwrapped.Result());
}

TEST(TaskTest, CurrentTaskSchedulerScopeRestoresPreviousScheduler)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    const auto original = CurrentScheduler();

    {
        CurrentTaskSchedulerScope scope(scheduler);
        EXPECT_EQ(scheduler, CurrentScheduler());
    }

    EXPECT_EQ(original, CurrentScheduler());
}

TEST(CoroutineTest, NextFrameResumesAtMostOncePerTick)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    int count = 0;

    const auto handle = manager.Start(RunAcrossTwoFrames(count));

    EXPECT_EQ(1u, manager.Tick(0.016, 0.016));
    EXPECT_EQ(1, count);
    EXPECT_TRUE(manager.IsRunning(handle));

    EXPECT_EQ(1u, manager.Tick(0.016, 0.032));
    EXPECT_EQ(2, count);
    EXPECT_FALSE(manager.IsRunning(handle));
    EXPECT_TRUE(handle.Completion().IsCompletedSuccessfully());
}

TEST(CoroutineTest, WaitForSecondsUsesRuntimeFrameTime)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    int count = 0;

    manager.Start(RunAfterDelay(count));
    manager.Tick(0.1, 1.0);
    EXPECT_EQ(1, count);

    manager.Tick(0.2, 1.2);
    EXPECT_EQ(1, count);

    manager.Tick(0.3, 1.5);
    EXPECT_EQ(2, count);
}

TEST(CoroutineTest, WaitUntilRunsPredicateOnRuntimeTick)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    bool ready = false;
    int count = 0;

    manager.Start(RunWhenReady(ready, count));
    manager.Tick(0.016, 0.016);
    manager.Tick(0.016, 0.032);
    EXPECT_EQ(1, count);

    ready = true;
    manager.Tick(0.016, 0.048);
    EXPECT_EQ(2, count);
}

TEST(CoroutineTest, AwaitTaskWakesThroughRuntimeScheduler)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    TaskCompletionSource<int> source;
    int result = 0;

    manager.Start(RunAfterTask(source.GetTask(), result));
    manager.Tick(0.016, 0.016);
    source.SetResult(42);

    manager.Tick(0.016, 0.032);
    EXPECT_EQ(0, result);
    EXPECT_EQ(1u, scheduler->RunAll());

    manager.Tick(0.016, 0.048);
    EXPECT_EQ(42, result);
}

TEST(CoroutineTest, CancelDestroysFrameAndIgnoresLateTaskWake)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    TaskCompletionSource<void> source;
    bool frameDestroyed = false;

    const auto handle = manager.Start(HoldUntilTask(source.GetTask(), frameDestroyed));
    manager.Tick(0.016, 0.016);
    manager.Cancel(handle);

    EXPECT_TRUE(frameDestroyed);
    EXPECT_TRUE(handle.Completion().IsCanceled());
    EXPECT_EQ(0u, manager.RunningCount());

    source.SetResult();
    EXPECT_EQ(1u, scheduler->RunAll());
    EXPECT_EQ(0u, manager.Tick(0.016, 0.032));
}

TEST(CoroutineTest, PauseAndResumeControlCoroutine)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    int count = 0;

    const auto handle = manager.Start(RunAcrossTwoFrames(count));
    manager.Pause(handle);
    EXPECT_EQ(0u, manager.Tick(0.016, 0.016));
    EXPECT_EQ(ECoroutineStatus::Paused, manager.Status(handle));

    manager.Resume(handle);
    EXPECT_EQ(1u, manager.Tick(0.016, 0.032));
    EXPECT_EQ(1, count);
}

TEST(CoroutineTest, PauseFreezesWaitForSeconds)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime runtime(scheduler);
    int count = 0;

    const auto handle = runtime.Start(RunAfterDelay(count));
    runtime.Tick(0.1, 1.0);
    ASSERT_EQ(1, count);

    runtime.Pause(handle);
    runtime.Tick(0.8, 1.8);
    EXPECT_EQ(1, count);

    runtime.Resume(handle);
    runtime.Tick(0.1, 1.9);
    EXPECT_EQ(1, count);

    runtime.Tick(0.41, 2.31);
    EXPECT_EQ(2, count);
}

TEST(CoroutineTest, ExceptionFaultsOnlyTheCoroutine)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    std::exception_ptr reported;
    manager.SetExceptionHandler(
        [&](const CCoroutineHandleBase&, std::exception_ptr exception_) {
            reported = exception_;
        });

    const auto handle = manager.Start(ThrowFromCoroutine());
    EXPECT_EQ(1u, manager.Tick(0.016, 0.016));

    EXPECT_TRUE(handle.Completion().IsFaulted());
    EXPECT_NE(nullptr, reported);
    EXPECT_EQ(0u, manager.RunningCount());
}

TEST(CoroutineTest, ResultCoroutineCompletesWithTypedValue)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);

    const auto handle = manager.Start(ReturnValueAfterFrame());
    manager.Tick(0.016, 0.016);
    EXPECT_FALSE(handle.Completion().IsCompleted());

    manager.Tick(0.016, 0.032);
    ASSERT_TRUE(handle.Completion().IsCompletedSuccessfully());
    EXPECT_EQ(42, handle.Completion().Result());
    EXPECT_EQ(ECoroutineStatus::Completed, manager.Status(handle));
}

TEST(CoroutineTest, ResultCoroutineSupportsMoveOnlyValue)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);

    const auto handle = manager.Start(ReturnMoveOnlyValue());
    manager.Tick(0.016, 0.016);

    ASSERT_TRUE(handle.Completion().IsCompletedSuccessfully());
    auto value = handle.Completion().TakeResult();
    ASSERT_NE(nullptr, value);
    EXPECT_EQ(17, *value);
}

TEST(CoroutineTest, ResultCoroutineCancellationCancelsTypedCompletion)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);

    const auto handle = manager.Start(ReturnValueAfterFrame());
    manager.Tick(0.016, 0.016);
    manager.Cancel(handle);

    EXPECT_TRUE(handle.Completion().IsCanceled());
    EXPECT_EQ(ECoroutineStatus::Canceled, manager.Status(handle));
}

TEST(CoroutineTest, ResultCoroutineExceptionFaultsTypedCompletion)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);

    const auto handle = manager.Start(ThrowFromResultCoroutine());
    manager.Tick(0.016, 0.016);

    EXPECT_TRUE(handle.Completion().IsFaulted());
    EXPECT_EQ(ECoroutineStatus::Faulted, manager.Status(handle));
    EXPECT_THROW(handle.Completion().Result(), std::runtime_error);
}

TEST(CoroutineTest, ResultCoroutineCanAwaitMoveOnlyTaskValue)
{
    auto scheduler = std::make_shared<EventLoopTaskScheduler>();
    CCoroutineRuntime manager(scheduler);
    TaskCompletionSource<std::unique_ptr<int>> source;

    const auto handle = manager.Start(AwaitMoveOnlyTask(source.GetTask()));
    manager.Tick(0.016, 0.016);
    source.SetResult(std::make_unique<int>(23));
    scheduler->RunAll();
    manager.Tick(0.016, 0.032);

    ASSERT_TRUE(handle.Completion().IsCompletedSuccessfully());
    EXPECT_EQ(23, handle.Completion().Result());
}
