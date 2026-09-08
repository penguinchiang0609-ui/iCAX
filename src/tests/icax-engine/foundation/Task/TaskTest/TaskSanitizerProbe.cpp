// Standalone sanitizer probe: intentionally avoids the prebuilt legacy GoogleTest
// binary so every STL instantiation uses the same compiler/sanitizer runtime.
#include <Task/Task.h>
#include <array>
#include <future>
#include <iostream>

using namespace iCAX::Tasks;
using namespace std::chrono_literals;

int main()
{
    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        auto pool = std::make_shared<ThreadPoolTaskScheduler>(2);
        std::promise<void> release;
        auto gate = release.get_future().share();
        auto finished = std::make_shared<std::promise<void>>();
        auto done = finished->get_future();
        pool->Schedule([owner = pool, gate]() mutable { gate.wait(); owner.reset(); });
        pool->Schedule([finished, gate] { gate.wait(); finished->set_value(); });
        pool.reset();
        release.set_value();
        if (done.wait_for(5s) != std::future_status::ready)
            throw std::runtime_error("self-destruction did not drain accepted work");
    }

    auto pool = std::make_shared<ThreadPoolTaskScheduler>(4);
    std::array<std::vector<Task<int>>, 4> submitted;
    std::atomic_int executed = 0;
    {
        std::vector<std::jthread> producers;
        for (int index = 0; index < 4; ++index)
            producers.emplace_back([&, index] {
                for (int item = 0; item < 10000; ++item)
                    submitted[index].push_back(Run([&] { return ++executed; }, pool));
            });
    }
    for (const auto& tasks : submitted) WhenAll(tasks).Result();
    if (executed != 40000) throw std::runtime_error("lost or duplicated work");

    CancellationTokenSource cancellation;
    auto registration = cancellation.Token().Register([] { throw std::runtime_error("observer"); });
    TaskCompletionSource<int> pending;
    auto waiting = WaitAsync(pending.GetTask(), cancellation.Token());
    try { cancellation.Cancel(); }
    catch (const TaskAggregateException&) {}
    if (!waiting.WaitFor(1s) || !waiting.IsCanceled())
        throw std::runtime_error("stranded cancellation waiter");
    pending.SetResult(0);
    pool->Shutdown();
    std::cout << "PASS: 1000 worker-owned shutdowns, 40000 concurrent tasks, cancellation propagation\n";
}
