#include "pch.h"


#include <Task/Task.h>
#include <Task/Coroutine.h>
#include <future>
#include <array>


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

TEST(TaskStressTest, EveryShutdownCallerWaitsForAcceptedWork)
{
    auto pool = std::make_shared<ThreadPoolTaskScheduler>(1);
    std::promise<void> release, entered;
    auto gate = release.get_future().share();
    pool->Schedule([&entered, gate] { entered.set_value(); gate.wait(); });
    entered.get_future().wait();
    auto first = std::async(std::launch::async, [pool] { pool->Shutdown(); });
    bool stopped = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!stopped && std::chrono::steady_clock::now() < deadline)
    {
        try { pool->Schedule([] {}); }
        catch (const std::runtime_error&) { stopped = true; }
        std::this_thread::yield();
    }
    auto second = std::async(std::launch::async, [pool] { pool->Shutdown(); });
    const auto early = second.wait_for(50ms);
    release.set_value(); // Release before assertions, including on a failed test.
    first.get();
    second.get();
    EXPECT_TRUE(stopped);
    EXPECT_EQ(std::future_status::timeout, early);
    EXPECT_EQ(0u, pool->PendingCount());
    EXPECT_THROW(pool->Schedule([] {}), std::runtime_error);
}

TEST(TaskStressTest, WorkerShutdownDoesNotJoinDependentPeers)
{
    auto pool = std::make_shared<ThreadPoolTaskScheduler>(2);
    std::promise<void> stopped, peerEntered;
    auto stopSignal = stopped.get_future().share();
    auto peerSignal = peerEntered.get_future().share();
    pool->Schedule([&peerEntered, stopSignal] {
        peerEntered.set_value();
        stopSignal.wait_for(2s); // Bounded even in the old, deadlocking implementation.
    });
    pool->Schedule([pool, &stopped, peerSignal] {
        peerSignal.wait();
        pool->Shutdown();
        stopped.set_value();
    });
    EXPECT_EQ(std::future_status::ready, stopSignal.wait_for(500ms));
    stopSignal.wait();
    pool->Shutdown();
}

TEST(TaskStressTest, LastPoolOwnerCanBeReleasedOnWorkerWhileQueueDrains)
{
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        auto pool = std::make_shared<ThreadPoolTaskScheduler>(1);
        std::promise<void> release;
        auto drained = std::make_shared<std::promise<void>>();
        auto gate = release.get_future().share();
        auto done = drained->get_future();
        pool->Schedule([owner = pool, gate]() mutable { gate.wait(); owner.reset(); });
        pool->Schedule([drained] { drained->set_value(); });
        pool.reset();
        release.set_value();
        ASSERT_EQ(std::future_status::ready, done.wait_for(2s));
    }
}

TEST(TaskStressTest, ConcurrentProducersExecuteEveryTaskExactlyOnce)
{
    auto pool = std::make_shared<ThreadPoolTaskScheduler>(4);
    std::array<std::atomic_int, 4000> visits{};
    std::array<std::vector<Task<int>>, 4> submitted;
    std::vector<std::jthread> producers;
    for (int producer = 0; producer < 4; ++producer)
        producers.emplace_back([&, producer] {
            auto& tasks = submitted[producer];
            for (int index = producer * 1000; index < (producer + 1) * 1000; ++index)
                tasks.push_back(iCAX::Tasks::Run([&, index] { ++visits[index]; return index; }, pool));
        });
    producers.clear();
    for (const auto& tasks : submitted)
    {
        auto all = WhenAll(tasks);
        ASSERT_TRUE(all.WaitFor(5s));
        const auto values = all.Result();
        for (std::size_t index = 0; index < tasks.size(); ++index)
            EXPECT_EQ(tasks[index].Result(), values[index]);
    }
    pool->Shutdown();
    for (const auto& count : visits) EXPECT_EQ(1, count.load());
}

TEST(TaskStressTest, CompletionRaceHasOneWinnerAndOneContinuation)
{
    auto pool = std::make_shared<ThreadPoolTaskScheduler>(4);
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        TaskCompletionSource<int> source;
        std::atomic_int winners = 0, callbacks = 0;
        auto continuation = source.GetTask().ContinueWith(
            [&](Task<int>) { ++callbacks; }, InlineScheduler());
        auto a = iCAX::Tasks::Run([&] { if (source.TrySetResult(7)) ++winners; }, pool);
        auto b = iCAX::Tasks::Run([&] { if (source.TrySetCanceled()) ++winners; }, pool);
        auto c = iCAX::Tasks::Run([&] { if (source.TrySetException(std::make_exception_ptr(std::runtime_error("test")))) ++winners; }, pool);
        WhenAll(std::vector<Task<void>>{ a, b, c }).Result();
        continuation.Result();
        EXPECT_EQ(1, winners.load());
        EXPECT_EQ(1, callbacks.load());
    }
}

TEST(TaskStressTest, WhenAllWaitsForOutstandingWorkBeforeReportingFaults)
{
    TaskCompletionSource<void> pending;
    auto all = WhenAll(std::vector<Task<void>>{
        Task<void>::FromException(std::make_exception_ptr(std::runtime_error("first"))),
        Task<void>::FromCanceled(), pending.GetTask() });
    EXPECT_FALSE(all.WaitFor(10ms));
    pending.SetException(std::make_exception_ptr(std::logic_error("second")));
    ASSERT_TRUE(all.WaitFor(2s));
    try { all.Result(); FAIL(); }
    catch (const TaskAggregateException& error) { EXPECT_EQ(2u, error.Exceptions().size()); }
}

TEST(TaskStressTest, ThrowingCancellationCallbackDoesNotStrandOtherWaiters)
{
    CancellationTokenSource source;
    auto registration = source.Token().Register([] { throw std::runtime_error("callback"); });
    TaskCompletionSource<int> pending;
    auto waiting = WaitAsync(pending.GetTask(), source.Token());
    EXPECT_THROW(source.Cancel(), TaskAggregateException);
    EXPECT_TRUE(waiting.WaitFor(100ms));
    EXPECT_TRUE(waiting.IsCanceled());
    pending.TrySetResult(1);
}

TEST(TaskStressTest, QueuedCancellationAndRejectedSubmissionReachTerminalState)
{
    auto pool = std::make_shared<ThreadPoolTaskScheduler>(1);
    std::promise<void> release, entered;
    auto gate = release.get_future().share();
    pool->Schedule([gate, &entered] { entered.set_value(); gate.wait(); });
    entered.get_future().wait();
    CancellationTokenSource cancellation;
    std::atomic_bool ran = false;
    auto queued = iCAX::Tasks::Run([&] { ran = true; }, pool, cancellation.Token());
    cancellation.Cancel();
    release.set_value();
    EXPECT_TRUE(queued.WaitFor(2s));
    EXPECT_TRUE(queued.IsCanceled());
    EXPECT_FALSE(ran.load());
    pool->Shutdown();
    auto rejected = iCAX::Tasks::Run([] {}, pool);
    ASSERT_TRUE(rejected.WaitFor(2s));
    EXPECT_TRUE(rejected.IsFaulted());
    EXPECT_THROW(rejected.Result(), std::runtime_error);
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
