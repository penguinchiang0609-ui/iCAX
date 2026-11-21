#pragma once
// Internal reusable construction services. Controllers own the order in which
// these operations are called; geometry and certification have one owner.
#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingPlaneEvaluation.h"
#include "SheetNestingNfp.h"
#include "SheetNestingConstructionTrace.h"
#include "NestingInternal.h"
#include "NestingSheetTemplateReuse.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace iCAX::SheetNesting::Detail::PlaneConstruction
{
struct Budget
{
    const Options& O;
    std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
    std::size_t Attempts = 0, Nodes = 0;
    std::size_t LocalAttemptLimit = std::numeric_limits<std::size_t>::max();
    double LocalTimeLimit = std::numeric_limits<double>::infinity();
    bool Cancelled = false;
    double Seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now()-Start).count(); }
    bool Interrupted()
    {
        if(!Cancelled&&O.Cancel&&O.Cancel())Cancelled=true;
        return Cancelled||Seconds()>=std::min(O.TimeLimitSeconds,LocalTimeLimit);
    }
    bool Stop(){return Interrupted()||Attempts>=std::min(O.MaxPlacementAttempts,LocalAttemptLimit);}
    Options Remaining() const
    {
        auto q=O;q.TimeLimitSeconds=std::max(0.,O.TimeLimitSeconds-Seconds());
        q.MaxPlacementAttempts=O.MaxPlacementAttempts-std::min(Attempts,O.MaxPlacementAttempts);
        q.EnableBasicCompaction=false;return q;
    }
};
struct StageTime
{
    const Budget& Work;
    double& Total;
    double Started;
    StageTime(const Budget& work,double& total):Work(work),Total(total),Started(work.Seconds()){}
    ~StageTime(){Total+=Work.Seconds()-Started;}
};
struct CommitHistory;
struct State
{
    std::uint64_t TraceID=0;
    Layout Board;
    MaxRects Free;
    std::vector<std::size_t> Used;
    std::size_t Groups=0, Fillers=0, ReservedOriginals=0;
    std::optional<std::size_t> Product;
    double ColumnWidth=0, ColumnTop=0;
    // Observation only; search copies share immutable actual commit prefixes.
    std::shared_ptr<const CommitHistory> History;
    State(const Stock& stock,std::size_t instance,const Problem& p,const std::vector<std::size_t>& used)
        :Board{stock.ID,instance,stock.Width,stock.Height,{},0},
         Free(std::max(0.,stock.Width-2*p.EdgeMargin+p.PartGap),std::max(0.,stock.Height-2*p.EdgeMargin+p.PartGap),p.Tolerance),Used(used){}
};
struct Candidate
{
    std::uint64_t TraceID=0,TraceProposal=0;
    std::vector<Member> Members;
    std::vector<Point> RepeatPitches;
    Rect Box;
    std::size_t Part=0, Columns=1, Rows=1, UnitMembers=0;
    double Score=0, Material=0,ContactRank=0;
    double OccupiedMaterial=0,OccupiedPlanningArea=0,UsedSectionArea=0,Utilization=-std::numeric_limits<double>::infinity();
    double TieArea=0,TieCorner=0,PrimaryAxis=0,SecondaryAxis=0;
    std::size_t Template=0;
    double ConstructionSeconds=0,PlanningInset=0;
    std::size_t PlacementQueries=0,GeneratedPositions=0,VerifiedPositions=0;
    PlanePlacementSpaceEvaluation Space;
    double SpaceEvaluationSeconds=0;
    std::size_t SpaceCandidatesEvaluated=0;
    std::optional<std::size_t> ContinuationStateSize;
};

using BoardConstructor=std::function<void(class Kernel&,State&,const std::string&)>;
class Kernel
{
    class Impl;
    std::unique_ptr<Impl> Data;
public:
    Kernel(const Problem&,PlaneStrategySettings,Budget&,std::size_t trial=0,std::shared_ptr<NfpCache> cache={});
    ~Kernel();
    Kernel(const Kernel&)=delete;
    Kernel& operator=(const Kernel&)=delete;
    const Problem& Input()const;
    const PlaneStrategySettings& Settings()const;
    Budget& Work();
    Result Run(const Result* fixed=nullptr);
    std::optional<Candidate> Single(const State&,const std::string&,bool nfp=true,const std::optional<Rect>& domain={});
    std::vector<Candidate> Arrays(const State&,const std::string&,const std::optional<std::size_t>& specification={});
    std::vector<Candidate> ContourArrays(const State&,const std::string&);
    std::vector<Candidate> Holes(const State&,const std::string&);
    std::vector<std::size_t> LargeFirst()const;
    std::optional<Candidate> MainArray(const State&,const std::string&);
    std::optional<Candidate> LayeredMain(const State&,const std::string&,int& phase);
    void GapFill(State&,const std::string&,Rect band,bool combinations);
    std::optional<Candidate> ForwardMain(const State&,const std::string&,const std::optional<Candidate>* continuation);
    std::optional<Candidate> ForwardBandFill(State&,const std::string&);
    void Fill(State&,const std::string&,const std::optional<Rect>& domain={});
    void FillHoles(State&,const std::string&);
    void RefineTail(State&,const std::string&);
    void Settle(State&);
    std::optional<Rect> OccupiedBand(const State&)const;
    void Commit(State&,const Candidate&,bool filler=false,const std::optional<Rect>& band={},int phase=0);
    void SelectedPath(const State&)const;
    void Choice(const State&,const Candidate&,const char* family);
    void Phase(const State&,ConstructionTraceStage,const char*,int,const std::optional<Rect>& band={});
    void Emit(ConstructionTraceStage,const std::function<void(ConstructionTraceEvent&)>&)const;
    void RankCandidates(const State&,std::vector<Candidate>&);
    void LimitCandidates(std::vector<Candidate>&,std::size_t)const;
    bool Better(const State&,const State&)const;
    bool RectanglesOnly()const;
    void AddSearchSeconds(double);
};
BoardConstructor MakeBoardConstructor(PlaneStrategyKind);
SheetTemplateReuseReport ReuseLastBoard(const Problem&,const PlaneStrategySettings&,Budget&,const Stock&,
    std::size_t&,std::vector<std::size_t>&,std::size_t&,Result&);
}
