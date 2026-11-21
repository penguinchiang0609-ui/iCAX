#pragma once
#include "NativeConstructionTraceJson.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <utility>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif

namespace iCAX::SheetNesting::Tools
{
struct NativeConstructionTraceLimits
{
    std::size_t Routes = 32;
    std::size_t DetailEventsPerRoute = 1024, DetailBytesPerRoute = 256 * 1024;
    std::size_t ProgressEventsPerRoute = 1024, ProgressBytesPerRoute = 512 * 1024;
    std::size_t StateEventsPerRoute = 4096, StateBytesPerRoute = 32 * 1024 * 1024;
    std::size_t PathEventsPerRoute = 4096, PathBytesPerRoute = 32 * 1024 * 1024;
    std::size_t SummaryEventsPerRoute = 8192, SummaryBytesPerRoute = 8 * 1024 * 1024;
    std::size_t PerformanceEventsPerRoute = 4096, PerformanceBytesPerRoute = 8 * 1024 * 1024;
    std::size_t TerminalBytesPerRoute = 512 * 1024, FinalSelectionBytes = 512 * 1024;
};

// An observer only: all quotas apply to records, never to solver work. Each
// route owns separate proposal, summary, performance, progress, state and terminal reservations. A busy
// early route cannot consume the later routes' final snapshots or ranks.
class NativeConstructionTraceWriter
{
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t Stages = 16;
    struct Route
    {
        std::size_t Seen = 0, Written = 0, Omitted = 0;
        std::size_t DetailWritten = 0, DetailBytes = 0, DetailOmitted = 0;
        std::size_t ProgressWritten = 0, ProgressBytes = 0, ProgressOmitted = 0;
        std::size_t StateSeen = 0, StateWritten = 0, StateBytes = 0, StateOmitted = 0, StateSnapshotsTruncated = 0;
        std::size_t PathSeen = 0, PathWritten = 0, PathBytes = 0, PathOmitted = 0, PathSnapshotsTruncated = 0;
        std::size_t SummaryWritten = 0, SummaryBytes = 0, SummaryOmitted = 0;
        std::size_t PerformanceSeen = 0, PerformanceWritten = 0, PerformanceBytes = 0, PerformanceOmitted = 0;
        std::size_t TerminalBytes = 0, TerminalSnapshotsTruncated = 0;
        bool TerminalObserved = false, RankObserved = false;
        std::array<std::size_t, Stages> StageSeen{}, StageWritten{}, StageBytes{};
    };
    class LimitedBuffer final : public std::streambuf
    {
        std::string Data;
        std::size_t Limit;
    protected:
        std::streamsize xsputn(const char* data, std::streamsize count) override
        {
            const auto size = static_cast<std::size_t>(count);
            const auto kept = std::min(size, Limit - Data.size());
            Data.append(data, kept);
            return static_cast<std::streamsize>(kept);
        }
        int_type overflow(int_type value) override
        {
            if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
            if (Data.size() == Limit) return traits_type::eof();
            Data.push_back(traits_type::to_char_type(value));
            return value;
        }
    public:
        explicit LimitedBuffer(std::size_t limit) : Limit(limit) { Data.reserve(std::min(limit, std::size_t{4096})); }
        std::string Take() { return std::move(Data); }
    };

    std::filesystem::path Index, Events, ResultFile;
    std::string OriginalProblem;
    std::shared_ptr<ConstructionTraceHealth> Health;
    NativeConstructionTraceLimits Limits;
    std::FILE* Stream = nullptr;
    mutable std::mutex Mutex;
    std::map<std::string, Route> Routes;
    Route GlobalRoute;
    std::size_t Seen = 0, Written = 0, Omitted = 0, Bytes = 0;
    std::size_t DetailOmitted = 0, ProgressOmitted = 0, PayloadsTruncated = 0, RoutesOmitted = 0;
    std::size_t SummaryOmitted = 0;
    std::size_t PerformanceSeen = 0, PerformanceWritten = 0, PerformanceBytes = 0, PerformanceOmitted = 0;
    std::size_t StateSeen = 0, StateWritten = 0, StateBytes = 0, StateOmitted = 0, StateSnapshotsTruncated = 0;
    std::size_t PathSeen = 0, PathWritten = 0, PathBytes = 0, PathOmitted = 0, PathSnapshotsTruncated = 0;
    std::size_t FinalBytes = 0;
    double TimeLimit;
    double PartGap, EdgeMargin;
    std::size_t AttemptCap;
    RankingPolicy Ranking;
    bool AllowCommonEdges, FreeRotation;
    bool Failed = false, Finished = false, CoreTruncated = false, RunCompleted = false;
    bool FinalSelectionObserved = false, FinalSnapshotTruncated = false;
    Clock::time_point LastIndex = Clock::now(), LastFlush = Clock::now();

    static bool Starts(const std::string& text, const char* prefix) { return text.rfind(prefix, 0) == 0; }
    static bool FinalSelection(const ConstructionTraceEvent& event)
    {
        return event.Source == "scheduler.final-selection" || event.Source == "scheduler.serial-final-selection" ||
            event.Source == "scheduler.portfolio-final-selection";
    }
    static bool Terminal(const ConstructionTraceEvent& event)
    {
        return event.Source == "scheduler.route-result" || event.Source == "scheduler.serial-route-result" ||
            event.Source == "scheduler.portfolio-route-result" || event.Source == "scheduler.not-started" ||
            event.Source == "scheduler.serial-not-started";
    }
    static bool Rank(const ConstructionTraceEvent& event)
    {
        return event.Source == "scheduler.quality" || event.Source == "scheduler.serial-quality" ||
            event.Source == "scheduler.portfolio-quality";
    }
    static bool CoreState(const ConstructionTraceEvent& event)
    {
        return event.Stage == ConstructionTraceStage::Commit || event.Source == "board.raw-construction";
    }
    static bool SelectedPath(const ConstructionTraceEvent& event)
    {
        return Starts(event.Source,"construction.path.") || event.Source=="strategy.construction-selection" ||
            event.Source=="competition.board-selection" || event.Source=="sheet-template.copy";
    }
    static bool Performance(const ConstructionTraceEvent& event)
    {
        return (event.Source == "array.query-profile" && event.Stage == ConstructionTraceStage::Complete) ||
            (event.Source == "placement.space-rank" && event.Stage == ConstructionTraceStage::Score) ||
            (event.Source == "construction.phase" &&
                (event.Stage == ConstructionTraceStage::Begin || event.Stage == ConstructionTraceStage::Complete));
    }
    static bool Progress(const ConstructionTraceEvent& event)
    {
        // Query completion is not a committed construction state. A busy
        // placement service must not consume the snapshots needed to replay
        // the later original commits; its compact costs use the summary lane.
        if(Performance(event)||((event.Source=="nfp.query"||event.Source=="nfp.rigid-placement")&&event.Stage==ConstructionTraceStage::Complete))return false;
        return event.Stage == ConstructionTraceStage::Commit || event.Stage == ConstructionTraceStage::Selected ||
            event.Stage == ConstructionTraceStage::Complete;
    }
    static bool PreparationSummary(const ConstructionTraceEvent& event)
    {
        return event.Source == "preparation.pair-summary" ||
            event.Source == "placement.local-pair-result" || event.Source == "placement.local-group-result" ||
            event.Source == "array.seed-origin" || event.Source == "placement.choice" ||
            event.Source == "array.unit-original-proof-summary" ||
            event.Source == "nfp.query-result" || event.Source == "array.original-query-result" ||
            event.Source == "array.rigid-query-result" ||
            (event.Source == "preparation.combination-summary" && event.Stage == ConstructionTraceStage::Complete) ||
            (event.Source == "nfp.original-clearance" && event.Stage == ConstructionTraceStage::Complete) ||
            (event.Source == "nfp.rigid-placement" && event.Stage == ConstructionTraceStage::Complete);
    }
    void Fail() noexcept
    {
        Failed = true;
        if (Health) Health->Failed.store(true);
    }
    bool ObserverFailed() const { return Failed || (Health && Health->Failed.load()); }
    std::size_t TotalOmitted() const { return Omitted + (Health ? Health->Omitted.load() : 0); }
    bool Truncated() const { return TotalOmitted() || PayloadsTruncated || CoreTruncated || ObserverFailed() || !RunCompleted; }
    void Omit(Route* route, bool progress, bool summary = false, bool state = false, bool performance = false, bool path = false)
    {
        ++Omitted;
        if (route) ++route->Omitted;
        if (path) { ++PathOmitted; CoreTruncated = true; if (route) ++route->PathOmitted; }
        else if (performance) { ++PerformanceOmitted; if (route) ++route->PerformanceOmitted; }
        else if (state)
        {
            ++StateOmitted; CoreTruncated = true;
            if (route) ++route->StateOmitted;
        }
        else if (summary) { ++SummaryOmitted; if (route) ++route->SummaryOmitted; }
        else if (progress) { ++ProgressOmitted; if (route) ++route->ProgressOmitted; }
        else { ++DetailOmitted; if (route) ++route->DetailOmitted; }
        // Sampling must not leave the live omission counters frozen for the
        // rest of a long route. Publication remains occasional and atomic.
        if (!Finished && Seen % 256 == 0 && Clock::now() - LastIndex >= std::chrono::seconds(2))
        { try { SaveIndex(); } catch (...) { Fail(); } }
    }
    bool Serialize(const ConstructionTraceEvent& event, std::size_t cap, std::string& output)
    {
        LimitedBuffer buffer(cap);
        std::ostream out(&buffer); out.imbue(std::locale::classic());
        out.exceptions(std::ios::badbit | std::ios::failbit);
        try
        {
            out << std::setprecision(std::numeric_limits<double>::max_digits10);
            out << "{\"ordinal\":" << Written + 1 << ",\"event\":";
            TraceJson(out, event); out << "}\n";
        }
        catch (const std::ios_base::failure&) { return false; }
        output = buffer.Take(); return true;
    }
    ConstructionTraceEvent Summary(const ConstructionTraceEvent& event) const
    {
        ConstructionTraceEvent summary;
        summary.Route = event.Route; summary.Scope = event.Scope; summary.Source = event.Source;
        summary.Measures = event.Measures; summary.Rank = event.Rank;
        summary.Stage = event.Stage; summary.Step = event.Step; summary.StateID = event.StateID;
        summary.ParentStateID = event.ParentStateID; summary.ProposalID = event.ProposalID; summary.CandidateID = event.CandidateID;
        summary.PartIndex = event.PartIndex; summary.StockIndex = event.StockIndex;
        summary.StockInstance = event.StockInstance; summary.BoardIndex = event.BoardIndex;
        summary.BoardWidth = event.BoardWidth; summary.BoardHeight = event.BoardHeight;
        summary.Width = event.Width; summary.Height = event.Height; summary.MaterialArea = event.MaterialArea;
        summary.Saving = event.Saving; summary.Fill = event.Fill; summary.FootprintArea = event.FootprintArea;
        summary.Increment = event.Increment; summary.EnvelopeArea = event.EnvelopeArea;
        summary.RegularRemnantArea = event.RegularRemnantArea; summary.ElapsedSeconds = event.ElapsedSeconds;
        summary.Attempts = event.Attempts; summary.ExpandedStates = event.ExpandedStates;
        summary.FeatureDerived = event.FeatureDerived; summary.ArrayDerived = event.ArrayDerived;
        summary.Accepted = event.Accepted; summary.Truncated = true;
        summary.AnalysisTruncated = event.AnalysisTruncated; summary.SearchTruncated = event.SearchTruncated;
        summary.ResultStatus = event.ResultStatus;
        summary.Reason = event.Reason.substr(0, 1024) + "; observation-payload-truncated; original result remains authoritative";
        std::size_t placements = 0;
        for (const auto& layout : event.Layouts) placements += layout.Placements.size();
        summary.Measures.emplace_back("observationOriginalLayouts", static_cast<double>(event.Layouts.size()));
        summary.Measures.emplace_back("observationOriginalPlacements", static_cast<double>(placements));
        summary.Measures.emplace_back("observationOriginalMembers", static_cast<double>(event.Members.size()));
        summary.Measures.emplace_back("observationOriginalGeometries", static_cast<double>(event.Geometries.size()));
        return summary;
    }
    void SaveIndex()
    {
        auto staging = Index; staging += ".tmp";
        std::ofstream out(staging, std::ios::binary | std::ios::trunc);
        out.imbue(std::locale::classic()); out << std::setprecision(17) << std::boolalpha;
        out << "{\"schemaVersion\":2,\"format\":\"native-construction-jsonl\",\"resultFile\":";
        TraceText(out, ResultFile.filename().string()); out << ",\"eventsFile\":"; TraceText(out, Events.filename().string());
        out << ",\"rotatedFrame\":false,\"instrumentedTiming\":true,\"eventCap\":"
            << Limits.Routes * (Limits.DetailEventsPerRoute + Limits.ProgressEventsPerRoute + Limits.StateEventsPerRoute + Limits.PathEventsPerRoute + Limits.SummaryEventsPerRoute + Limits.PerformanceEventsPerRoute + 16) + 8
            << ",\"byteCap\":" << Limits.Routes * (Limits.DetailBytesPerRoute + Limits.ProgressBytesPerRoute + Limits.StateBytesPerRoute + Limits.PathBytesPerRoute + Limits.SummaryBytesPerRoute + Limits.PerformanceBytesPerRoute + Limits.TerminalBytesPerRoute) + Limits.FinalSelectionBytes;
        StatusJsonUnlocked(out);
        out << ",\"caps\":{\"routes\":" << Limits.Routes << ",\"detailEventsPerRoute\":" << Limits.DetailEventsPerRoute
            << ",\"detailBytesPerRoute\":" << Limits.DetailBytesPerRoute << ",\"progressEventsPerRoute\":" << Limits.ProgressEventsPerRoute
            << ",\"progressBytesPerRoute\":" << Limits.ProgressBytesPerRoute << ",\"terminalBytesPerRoute\":" << Limits.TerminalBytesPerRoute
            << ",\"stateEventsPerRoute\":" << Limits.StateEventsPerRoute << ",\"stateBytesPerRoute\":" << Limits.StateBytesPerRoute
            << ",\"pathEventsPerRoute\":" << Limits.PathEventsPerRoute << ",\"pathBytesPerRoute\":" << Limits.PathBytesPerRoute
            << ",\"summaryEventsPerRoute\":" << Limits.SummaryEventsPerRoute << ",\"summaryBytesPerRoute\":" << Limits.SummaryBytesPerRoute
            << ",\"performanceEventsPerRoute\":" << Limits.PerformanceEventsPerRoute << ",\"performanceBytesPerRoute\":" << Limits.PerformanceBytesPerRoute
            << ",\"finalSelectionBytes\":" << Limits.FinalSelectionBytes << "},\"samplingPolicy\":\"per-route-and-stage; first events and logarithmic progress; independent unsampled bounded commit/raw-state, pair/query summary, array performance/construction phase/current-space score and terminal/rank reservations; core state and performance loss explicitly counted\",\"routes\":[";
        bool first = true;
        for (const auto& [id, route] : Routes)
        {
            if (!first) out << ','; first = false;
            out << "{\"id\":"; TraceText(out, id);
            out << ",\"events\":" << route.Written << ",\"eventsSeen\":" << route.Seen << ",\"eventsOmitted\":" << route.Omitted
                << ",\"detailEventsOmitted\":" << route.DetailOmitted << ",\"progressEventsOmitted\":" << route.ProgressOmitted
                << ",\"summaryEventsWritten\":" << route.SummaryWritten << ",\"summaryEventsOmitted\":" << route.SummaryOmitted
                << ",\"performanceEventsSeen\":" << route.PerformanceSeen << ",\"performanceEventsWritten\":" << route.PerformanceWritten
                << ",\"performanceEventsOmitted\":" << route.PerformanceOmitted << ",\"performanceBytesWritten\":" << route.PerformanceBytes
                << ",\"performanceObservationTruncated\":" << (route.PerformanceOmitted ? "true" : "false")
                << ",\"stateEventsSeen\":" << route.StateSeen << ",\"stateEventsWritten\":" << route.StateWritten
                << ",\"stateEventsOmitted\":" << route.StateOmitted << ",\"stateBytesWritten\":" << route.StateBytes
                << ",\"stateSnapshotsTruncated\":" << route.StateSnapshotsTruncated
                << ",\"pathEventsSeen\":" << route.PathSeen << ",\"pathEventsWritten\":" << route.PathWritten
                << ",\"pathEventsOmitted\":" << route.PathOmitted << ",\"pathBytesWritten\":" << route.PathBytes
                << ",\"pathSnapshotsTruncated\":" << route.PathSnapshotsTruncated
                << ",\"terminalObserved\":" << route.TerminalObserved << ",\"rankObserved\":" << route.RankObserved
                << ",\"terminalSnapshotsTruncated\":" << route.TerminalSnapshotsTruncated << '}';
        }
        out << "],\"conditions\":{\"timeLimitSeconds\":" << TimeLimit << ",\"attemptCap\":" << AttemptCap
            << ",\"rankingPolicy\":" << static_cast<int>(Ranking) << ",\"diagnosticRun\":true,\"externalLayoutSeed\":false,\"taskProfile\":\"original\""
            << ",\"partGap\":" << PartGap << ",\"edgeMargin\":" << EdgeMargin << ",\"allowCommonEdges\":" << AllowCommonEdges
            << ",\"freeRotation\":" << FreeRotation << "},\"problem\":"
            << OriginalProblem << '}';
        out.flush(); if (!out) throw std::runtime_error("Cannot write construction trace index");
        out.close(); if (!out) throw std::runtime_error("Cannot close construction trace index");
        std::filesystem::rename(staging, Index); LastIndex = Clock::now();
    }
    void StatusJsonUnlocked(std::ostream& out) const
    {
        out << ",\"eventsSeen\":" << Seen << ",\"eventsWritten\":" << Written << ",\"eventsOmitted\":" << TotalOmitted()
            << ",\"bytesWritten\":" << Bytes << ",\"runCompleted\":" << (RunCompleted ? "true" : "false")
            << ",\"observerFailed\":" << (ObserverFailed() ? "true" : "false") << ",\"truncated\":" << (Truncated() ? "true" : "false")
            << ",\"detailEventsOmitted\":" << DetailOmitted << ",\"progressEventsOmitted\":" << ProgressOmitted
            << ",\"summaryEventsOmitted\":" << SummaryOmitted
            << ",\"performanceEventsSeen\":" << PerformanceSeen << ",\"performanceEventsWritten\":" << PerformanceWritten
            << ",\"performanceEventsOmitted\":" << PerformanceOmitted << ",\"performanceBytesWritten\":" << PerformanceBytes
            << ",\"performanceObservationTruncated\":" << (PerformanceOmitted ? "true" : "false")
            << ",\"stateEventsSeen\":" << StateSeen << ",\"stateEventsWritten\":" << StateWritten
            << ",\"stateEventsOmitted\":" << StateOmitted << ",\"stateBytesWritten\":" << StateBytes
            << ",\"pathEventsSeen\":" << PathSeen << ",\"pathEventsWritten\":" << PathWritten
            << ",\"pathEventsOmitted\":" << PathOmitted << ",\"pathBytesWritten\":" << PathBytes
            << ",\"pathSnapshotsTruncated\":" << PathSnapshotsTruncated
            << ",\"stateSnapshotsTruncated\":" << StateSnapshotsTruncated
            << ",\"payloadsTruncated\":" << PayloadsTruncated << ",\"routesOmitted\":" << RoutesOmitted
            << ",\"coreObservationTruncated\":" << (CoreTruncated ? "true" : "false")
            << ",\"finalSelectionObserved\":" << (FinalSelectionObserved ? "true" : "false")
            << ",\"finalSnapshotTruncated\":" << (FinalSnapshotTruncated ? "true" : "false");
    }
public:
    NativeConstructionTraceWriter(const std::filesystem::path& index, const std::filesystem::path& result,
        const Problem& input, const Options& options, const std::function<void(std::ostream&, const Problem&)>& problemJson,
        NativeConstructionTraceLimits limits = {})
        : Index(index), ResultFile(result), Health(options.ConstructionObservation), Limits(limits),
          TimeLimit(options.TimeLimitSeconds), PartGap(input.PartGap), EdgeMargin(input.EdgeMargin),
          AttemptCap(options.MaxPlacementAttempts), Ranking(options.Ranking), AllowCommonEdges(input.AllowCommonEdges),
          FreeRotation(std::all_of(input.Parts.begin(),input.Parts.end(),[](const Part& part){return part.FreeRotation;}))
    {
        const auto name = Index.filename().string();
        constexpr const char* suffix = ".trace-index.json";
        if (!Index.is_absolute() || !name.ends_with(suffix)) throw std::runtime_error("--trace needs an absolute .trace-index.json path");
        Events = Index.parent_path() / (name.substr(0, name.size() - std::char_traits<char>::length(suffix)) + ".construction.jsonl");
        std::error_code indexError, eventsError;
        const bool indexExists = std::filesystem::exists(Index, indexError), eventsExist = std::filesystem::exists(Events, eventsError);
        if (indexExists || eventsExist) throw std::runtime_error("Refusing to overwrite native construction evidence");
        std::ostringstream original; original.imbue(std::locale::classic()); original << std::setprecision(17) << std::boolalpha;
        problemJson(original, input); OriginalProblem = original.str();
#ifdef _WIN32
        // Keep exclusive creation/write ownership, but let the live report
        // read flushed events while the constructor is still running.
        int descriptor=-1;
        if(!_wsopen_s(&descriptor,Events.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,
            _SH_DENYWR,_S_IREAD|_S_IWRITE))
        {Stream=_wfdopen(descriptor,L"wb");if(!Stream)_close(descriptor);}
#else
        Stream = std::fopen(Events.c_str(), "wbx");
#endif
        if (!Stream || indexError || eventsError) Fail();
        try { SaveIndex(); } catch (...) { Fail(); }
    }
    ~NativeConstructionTraceWriter() { Finish(false); if (Stream) std::fclose(Stream); }
    const std::filesystem::path& IndexPath() const { return Index; }
    void Record(const ConstructionTraceEvent& event) noexcept
    {
        std::lock_guard guard(Mutex);
        const bool path = SelectedPath(event);
        const bool state = CoreState(event) && !path;
        const bool performance = Performance(event);
        if (state) ++StateSeen;
        if (path) ++PathSeen;
        if (performance) ++PerformanceSeen;
        ++Seen; CoreTruncated = CoreTruncated || event.Truncated;
        if (Finished || ObserverFailed() || !Stream) { Omit(nullptr, Progress(event), PreparationSummary(event), state, performance,path); return; }
        bool written = false;
        Route* observedRoute = nullptr;
        try
        {
            const bool terminal = Terminal(event), rank = Rank(event), final = FinalSelection(event), progress = Progress(event), summary = PreparationSummary(event);
            auto found = Routes.find(event.Route);
            Route* routePointer = nullptr;
            if (found == Routes.end())
            {
                if (Routes.size() >= Limits.Routes)
                {
                    if (!final) { ++RoutesOmitted; Omit(nullptr, progress, summary, state, performance,path); return; }
                    routePointer = &GlobalRoute;
                }
                else found = Routes.try_emplace(event.Route).first;
            }
            if (!routePointer) routePointer = &found->second;
            auto& route = *routePointer; ++route.Seen;
            observedRoute = &route;
            if (state) ++route.StateSeen;
            if (path) ++route.PathSeen;
            if (performance) ++route.PerformanceSeen;
            const auto stage = std::min(static_cast<std::size_t>(event.Stage), Stages - 1);
            const auto stageSeen = state || performance || path ? std::size_t{} : ++route.StageSeen[stage];
            const bool mandatory = terminal || rank || final || state || path;
            if ((terminal && route.TerminalObserved) || (rank && route.RankObserved) || (final && FinalSelectionObserved))
            { Omit(&route, progress, summary, state, performance,path); return; }
            std::size_t remaining = 0;
            if (final) remaining = Limits.FinalSelectionBytes - std::min(FinalBytes, Limits.FinalSelectionBytes);
            else if (path)
            {
                if(route.PathWritten>=Limits.PathEventsPerRoute)
                {Omit(&route,false,false,false,false,true);return;}
                remaining=Limits.PathBytesPerRoute-std::min(route.PathBytes,Limits.PathBytesPerRoute);
            }
            else if (performance)
            {
                // Array profiles, current-space scores and phase boundaries must
                // survive any detail, progress or query-summary flood. This independent
                // bounded lane writes the whole cumulative record or counts
                // its omission; it never substitutes a partial metric payload.
                if (route.PerformanceWritten >= Limits.PerformanceEventsPerRoute)
                { Omit(&route, false, false, false, true); return; }
                remaining = Limits.PerformanceBytesPerRoute - std::min(route.PerformanceBytes, Limits.PerformanceBytesPerRoute);
            }
            else if (state)
            {
                if (route.StateWritten >= Limits.StateEventsPerRoute)
                { Omit(&route, progress, summary, true); return; }
                remaining = Limits.StateBytesPerRoute - std::min(route.StateBytes, Limits.StateBytesPerRoute);
            }
            else if (summary)
            {
                // Aggregate construction evidence has its own unsampled lane.
                // Thousands of earlier singleton/detail events cannot hide the
                // later specifications' pair generation and rejection causes.
                // Actual alternatives and aggregate original-proof costs must
                // remain inspectable after thousands of query/seed summaries.
                // Reserve the last quarter for these two compact record types.
                const bool protectedSummary=event.Source=="placement.choice"||event.Source=="array.unit-original-proof-summary";
                const auto eventCap=Limits.SummaryEventsPerRoute-(protectedSummary?0:Limits.SummaryEventsPerRoute/4);
                const auto byteCap=Limits.SummaryBytesPerRoute-(protectedSummary?0:Limits.SummaryBytesPerRoute/4);
                if (route.SummaryWritten >= eventCap)
                { Omit(&route, false, true); return; }
                remaining = byteCap - std::min(route.SummaryBytes, byteCap);
            }
            else if (mandatory)
            {
                // Reserve 1/16 of every terminal lane for the unified quality
                // tuple, which is observed immediately after the route result.
                const auto cap = rank ? Limits.TerminalBytesPerRoute : Limits.TerminalBytesPerRoute * 15 / 16;
                remaining = cap - std::min(route.TerminalBytes, cap);
            }
            else
            {
                const auto capEvents = progress ? Limits.ProgressEventsPerRoute : Limits.DetailEventsPerRoute;
                const auto capBytes = progress ? Limits.ProgressBytesPerRoute : Limits.DetailBytesPerRoute;
                const auto count = progress ? route.ProgressWritten : route.DetailWritten;
                const auto bytes = progress ? route.ProgressBytes : route.DetailBytes;
                const auto firstEvents = progress ? 32u : 8u;
                const bool sample = stageSeen <= firstEvents || (stageSeen & (stageSeen - 1)) == 0;
                const auto stageByteCap = std::max<std::size_t>(1, capBytes / (progress ? 3 : Stages));
                if (!sample || count >= capEvents || route.StageWritten[stage] >= std::max<std::size_t>(1, capEvents / (progress ? 3 : Stages)))
                { Omit(&route, progress); return; }
                remaining = std::min(capBytes - std::min(bytes, capBytes), stageByteCap - std::min(route.StageBytes[stage], stageByteCap));
            }
            std::string data; bool summarized = false;
            if (!Serialize(event, remaining, data))
            {
                if ((!mandatory && !summary) || !Serialize(Summary(event), remaining, data)) { Omit(&route, progress, summary, state, performance,path); return; }
                summarized = true;
            }
            if (std::fwrite(data.data(), 1, data.size(), Stream) != data.size()) { Fail(); Omit(&route, progress, summary, state, performance,path); return; }
            ++Written; ++route.Written; Bytes += data.size();
            written = true;
            if (summarized)
            {
                ++PayloadsTruncated;
                if (state) { ++StateSnapshotsTruncated; ++route.StateSnapshotsTruncated; CoreTruncated = true; }
                if (path) { ++PathSnapshotsTruncated; ++route.PathSnapshotsTruncated; CoreTruncated = true; }
                if (terminal) ++route.TerminalSnapshotsTruncated;
                if (final) FinalSnapshotTruncated = true;
            }
            if (final) { FinalBytes += data.size(); FinalSelectionObserved = true; }
            else if (path) { ++PathWritten;++route.PathWritten;PathBytes+=data.size();route.PathBytes+=data.size(); }
            else if (performance)
            { ++PerformanceWritten; ++route.PerformanceWritten; PerformanceBytes += data.size(); route.PerformanceBytes += data.size(); }
            else if (state) { ++StateWritten; ++route.StateWritten; StateBytes += data.size(); route.StateBytes += data.size(); }
            else if (summary) { ++route.SummaryWritten; route.SummaryBytes += data.size(); }
            else if (mandatory) { route.TerminalBytes += data.size(); route.TerminalObserved = route.TerminalObserved || terminal; route.RankObserved = route.RankObserved || rank; }
            else
            {
                ++route.StageWritten[stage]; route.StageBytes[stage] += data.size();
                if (progress) { ++route.ProgressWritten; route.ProgressBytes += data.size(); }
                else { ++route.DetailWritten; route.DetailBytes += data.size(); }
            }
            const auto now = Clock::now();
            if (mandatory || performance || Written % 64 == 0 || now - LastFlush >= std::chrono::milliseconds(250))
            { if (std::fflush(Stream)) Fail(); LastFlush = now; }
            if (terminal || final || state || path || performance || now - LastIndex >= std::chrono::seconds(2)) SaveIndex();
        }
        catch (...) { Fail(); if (!written) Omit(observedRoute, Progress(event), PreparationSummary(event), state, performance,path); }
    }
    bool Finish(bool completed) noexcept
    {
        std::lock_guard guard(Mutex);
        if (Finished) return !ObserverFailed();
        Finished = true; RunCompleted = completed;
        if (Stream && std::fflush(Stream)) Fail();
        try { SaveIndex(); } catch (...) { Fail(); }
        return !ObserverFailed();
    }
    void StatusJson(std::ostream& out) const
    {
        std::lock_guard guard(Mutex);
        out << "{\"instrumentedTiming\":true"; StatusJsonUnlocked(out); out << '}';
    }
};
}
