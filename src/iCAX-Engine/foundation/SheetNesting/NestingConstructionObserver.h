#pragma once
#include "SheetNestingFeatureConstruction.h"
#include "NestingInternal.h"
#include <chrono>
#include <limits>
#include <utility>

namespace iCAX::SheetNesting::Detail
{
using ConstructionObserverClock = std::chrono::steady_clock;

class ConstructionObserver
{
  public:
    ConstructionObserver(const FeatureConstructionSettings &settings, ConstructionObserverClock::time_point started,
                         const std::size_t &attempts, const std::size_t &expanded)
        : ConstructionObserver(settings.Trace, settings.MaxTraceEvents, started, attempts, expanded)
    {
    }

    ConstructionObserver(const std::function<void(const ConstructionTraceEvent &)> &callback, std::size_t limit,
                         ConstructionObserverClock::time_point started, const std::size_t &attempts,
                         const std::size_t &expanded)
        : Callback(callback), Limit(limit), Started(started), Attempts(attempts), Expanded(expanded)
    {
        Truncated = Callback && !Limit;
    }

    bool Enabled() const
    {
        return Callback && Limit && !Failed;
    }
    bool Active()
    {
        if (!Enabled())
            return false;
        if (Count + 1 >= Limit)
        {
            Truncated = true;
            return false;
        }
        return true;
    }
    std::uint64_t NewState()
    {
        return Enabled() ? ++StateCounter : 0;
    }
    std::uint64_t NewProposal()
    {
        return Enabled() ? ++ProposalCounter : 0;
    }
    std::uint64_t NewCandidate()
    {
        return Enabled() ? ++CandidateCounter : 0;
    }

    template <class Build> void Emit(ConstructionTraceStage stage, Build &&build, bool terminal = false)
    {
        if (terminal ? (!Enabled() || Count >= Limit) : !Active())
            return;
        try
        {
            ConstructionTraceEvent event;
            event.Stage = stage;
            event.Step = ++Count;
            event.ElapsedSeconds = std::chrono::duration<double>(ConstructionObserverClock::now() - Started).count();
            event.Attempts = Attempts;
            event.ExpandedStates = Expanded;
            event.Truncated = Truncated;
            build(event);
            Callback(event);
        }
        catch (...)
        {
            // Observer allocation, output and user callback failures are
            // isolated from the independent solver lifecycle.
            Failed = true;
            Truncated = true;
        }
    }

    std::size_t Count = 0;
    bool Truncated = false, Failed = false;

  private:
    std::function<void(const ConstructionTraceEvent &)> Callback;
    std::size_t Limit = 0;
    ConstructionObserverClock::time_point Started;
    const std::size_t &Attempts;
    const std::size_t &Expanded;
    std::uint64_t StateCounter = 1, ProposalCounter = 0, CandidateCounter = 0;
};

inline void TracePattern(ConstructionTraceEvent &event, const Pattern &pattern)
{
    event.Width = pattern.Width;
    event.Height = pattern.Height;
    event.MaterialArea = pattern.MaterialArea;
    for (const auto &member : pattern.Members)
        event.Members.push_back({member.PartIndex, std::numeric_limits<std::size_t>::max(), member.Transform, 0});
}

inline ConstructionTraceGeometry TraceShape(const Shape &shape)
{
    ConstructionTraceGeometry result;
    result.Radius = shape.Radius;
    result.Center = shape.Center;
    if (!shape.Outer.empty())
        result.Rings.push_back(shape.Outer);
    result.Rings.insert(result.Rings.end(), shape.Holes.begin(), shape.Holes.end());
    return result;
}

} // namespace iCAX::SheetNesting::Detail
