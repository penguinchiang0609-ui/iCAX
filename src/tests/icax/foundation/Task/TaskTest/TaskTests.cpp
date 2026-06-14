#include <gtest/gtest.h>

#include <Task/Task.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace iCAX::Tasks;

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

TEST(TaskTest, ManualSchedulerRunsQueuedWork)
{
    auto scheduler = std::make_shared<ManualTaskScheduler>();

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
    auto scheduler = std::make_shared<ManualTaskScheduler>();
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
    auto scheduler = std::make_shared<ManualTaskScheduler>();

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
    auto scheduler = std::make_shared<ManualTaskScheduler>();
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

TEST(TaskTest, StartNewLongRunningRunsWithoutManualScheduler)
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
    auto scheduler = std::make_shared<ManualTaskScheduler>();
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
    auto scheduler = std::make_shared<ManualTaskScheduler>();
    const auto original = CurrentScheduler();

    {
        CurrentTaskSchedulerScope scope(scheduler);
        EXPECT_EQ(scheduler, CurrentScheduler());
    }

    EXPECT_EQ(original, CurrentScheduler());
}
