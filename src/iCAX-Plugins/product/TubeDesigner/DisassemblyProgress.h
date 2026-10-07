#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace iCAX::TubeDesigner
{
    struct SDisassemblyProgress final
    {
        std::string Phase;
        std::uint64_t Completed = 0;
        std::uint64_t Total = 0;
        std::string Message;
        std::uint64_t ElapsedMs = 0;
    };

    // Reports only the stage and counts supplied by the scene-owning thread.
    // Long CAD builders and database commits can block that thread; the reporter
    // keeps their current activity visible without touching scene/database data.
    class CDisassemblyProgress final
    {
    public:
        using Reporter = std::function<void(const SDisassemblyProgress&)>;

        explicit CDisassemblyProgress(Reporter Reporter_,
            std::chrono::milliseconds Interval_ = std::chrono::seconds(5))
            : Report(std::move(Reporter_)), Interval(Interval_), Started(std::chrono::steady_clock::now())
        {
            if (Report) Worker = std::thread([this] { Run(); });
        }

        ~CDisassemblyProgress()
        {
            {
                const std::lock_guard _Lock(StateMutex);
                Stopping = true;
            }
            Wake.notify_all();
            if (Worker.joinable()) Worker.join();
        }

        CDisassemblyProgress(const CDisassemblyProgress&) = delete;
        CDisassemblyProgress& operator=(const CDisassemblyProgress&) = delete;

        void Set(std::string Phase_, std::uint64_t Completed_, std::uint64_t Total_, std::string Message_)
        {
            {
                const std::lock_guard _Lock(StateMutex);
                Current = {std::move(Phase_), Completed_, Total_, std::move(Message_), 0};
            }
            Publish();
        }

    private:
        void Publish() noexcept
        {
            try
            {
                // Serialise stage and activity reports. The immutable invocation
                // handler outlives this joined scope; no callback touches the scene.
                const std::lock_guard _ReportLock(ReportMutex);
                SDisassemblyProgress _Snapshot;
                {
                    const std::lock_guard _Lock(StateMutex);
                    if (Stopping || Current.Phase.empty()) return;
                    _Snapshot = Current;
                }
                _Snapshot.ElapsedMs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - Started).count());
                if (Report) Report(_Snapshot);
            }
            catch (...) {} // A closed report channel must not terminate a CAD job.
        }

        void Run() noexcept
        {
            std::unique_lock _Lock(StateMutex);
            while (!Wake.wait_for(_Lock, Interval, [this] { return Stopping; }))
            {
                _Lock.unlock();
                Publish();
                _Lock.lock();
            }
        }

        Reporter Report;
        std::chrono::milliseconds Interval;
        std::chrono::steady_clock::time_point Started;
        std::mutex StateMutex;
        std::mutex ReportMutex;
        std::condition_variable Wake;
        SDisassemblyProgress Current;
        bool Stopping = false;
        std::thread Worker;
    };
}
