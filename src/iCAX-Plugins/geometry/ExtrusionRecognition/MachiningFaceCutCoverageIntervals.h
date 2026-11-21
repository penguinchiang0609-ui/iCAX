#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iCAX::ExtrusionRecognition::machining_face_cut_coverage
{
enum class Kind : std::uint8_t
{
    Baseline,
    Arrival
};

// First/Last are already in TargetEdgeId's finite native parameter domain.
// Mapping direction is evidence owned by the caller; it does not change
// coverage. In particular, this helper never wraps periodic parameters.
struct Input final
{
    std::uint64_t FaceId = 0;
    std::uint64_t TargetEdgeId = 0;
    double First = 0.0;
    double Last = 0.0;
    std::string SourcePathKey;
    Kind Type = Kind::Baseline;
    std::size_t EvidenceIndex = 0;
};

struct Atom final
{
    std::uint64_t FaceId = 0;
    std::uint64_t TargetEdgeId = 0;
    double First = 0.0;
    double Last = 0.0;
    // A path contributes at most once on an atom, even when its fragments
    // or several projections overlap there. Different faces never mix.
    std::vector<std::string> SourcePathKeys;
    // Retain each active input's caller-owned evidence reference. These may
    // contain repeated indices; evidence is not removed by source de-duplication.
    std::vector<std::size_t> BaselineEvidenceIndices;
    std::vector<std::size_t> ArrivalEvidenceIndices;
};

// Returns positive-length atoms in (FaceId, TargetEdgeId, First) order.
// Only the measure of open parameter intervals is counted: a contact at a
// shared endpoint cannot introduce another coverage count. A zero-length
// input is ignored. Invalid non-finite/reversed ranges and empty sources fail.
// The default preserves every representable positive interval, however narrow.
// A nonzero caller-selected parameterTolerance only suppresses output atoms
// no longer than that value; it never moves endpoints or bridges a gap.
inline std::vector<Atom> Build(const std::vector<Input>& inputs,
    const double parameterTolerance = 0.0)
{
    if (!std::isfinite(parameterTolerance) || parameterTolerance < 0.0)
        throw std::invalid_argument("Coverage parameter tolerance must be finite and nonnegative");

    struct Event final
    {
        double Parameter = 0.0;
        std::size_t InputIndex = 0;
        bool Starting = false;
    };
    using Target = std::pair<std::uint64_t, std::uint64_t>;
    std::map<Target, std::vector<Event>> groups;
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        const auto& input = inputs[index];
        if (!std::isfinite(input.First) || !std::isfinite(input.Last)
            || input.Last < input.First)
            throw std::invalid_argument("Coverage input must have finite ordered native parameters");
        if (input.SourcePathKey.empty())
            throw std::invalid_argument("Coverage input must retain its source path key");
        if (input.Type != Kind::Baseline && input.Type != Kind::Arrival)
            throw std::invalid_argument("Coverage input has an invalid evidence kind");
        if (input.First == input.Last) continue;
        auto& events = groups[{ input.FaceId, input.TargetEdgeId }];
        events.push_back({ input.First, index, true });
        events.push_back({ input.Last, index, false });
    }

    std::vector<Atom> result;
    for (auto& [target, events] : groups)
    {
        std::sort(events.begin(), events.end(), [](const Event& a, const Event& b)
        {
            if (a.Parameter != b.Parameter) return a.Parameter < b.Parameter;
            if (a.Starting != b.Starting) return !a.Starting;
            return a.InputIndex < b.InputIndex;
        });
        std::set<std::size_t> active;
        double previous = events.front().Parameter;
        std::size_t eventIndex = 0;
        while (eventIndex < events.size())
        {
            const double parameter = events[eventIndex].Parameter;
            if (!active.empty() && parameter > previous
                && parameter - previous > parameterTolerance)
            {
                Atom atom;
                atom.FaceId = target.first;
                atom.TargetEdgeId = target.second;
                atom.First = previous;
                atom.Last = parameter;
                std::set<std::string> sources;
                for (const auto inputIndex : active)
                {
                    const auto& input = inputs[inputIndex];
                    sources.insert(input.SourcePathKey);
                    auto& evidence = input.Type == Kind::Baseline
                        ? atom.BaselineEvidenceIndices : atom.ArrivalEvidenceIndices;
                    evidence.push_back(input.EvidenceIndex);
                }
                atom.SourcePathKeys.assign(sources.begin(), sources.end());
                std::sort(atom.BaselineEvidenceIndices.begin(), atom.BaselineEvidenceIndices.end());
                std::sort(atom.ArrivalEvidenceIndices.begin(), atom.ArrivalEvidenceIndices.end());
                result.push_back(std::move(atom));
            }

            // Treat every event at this exact parameter as one boundary. The
            // resulting active set applies only to the following open span.
            while (eventIndex < events.size() && events[eventIndex].Parameter == parameter)
            {
                const auto& event = events[eventIndex++];
                if (event.Starting) active.insert(event.InputIndex);
                else active.erase(event.InputIndex);
            }
            previous = parameter;
        }
    }
    return result;
}
}
