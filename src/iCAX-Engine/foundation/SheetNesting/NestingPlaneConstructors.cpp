#include "NestingPlaneConstruction.h"
#include <algorithm>
#include <map>
#include <random>

namespace iCAX::SheetNesting::Detail::PlaneConstruction
{
namespace
{
using Kind=PlaneStrategyKind;
bool ContourArray(Kind k){return k==Kind::NfpArray||k==Kind::NfpMultiArray;}

// Controllers own construction order. Every candidate and commit below goes
// through the same contour services; none owns a copied geometry oracle.
void LayeredArray(Kernel& k,State& state,const std::string& material)
{
    if(!k.Work().O.EnableCombinations||!k.Work().O.EnableGapFill)
    {
        // A caller disabling either stage requests ordinary forward arrays.
        // Keep their certified unit-growth path instead of the combination-
        // only proposal phases used by the complete layered construction.
        std::optional<Candidate> continuation;
        while(!k.Work().Stop())
        {
            auto main=k.ForwardMain(state,material,&continuation);if(!main)break;
            continuation.reset();
            k.Choice(state,*main,"forward-array-with-disabled-layer-stage");k.Commit(state,*main);
            if(k.Work().O.EnableGapFill)continuation=k.ForwardBandFill(state,material);
            else k.FillHoles(state,material);
        }
        return;
    }
    while(!k.Work().Stop())
    {
        k.Phase(state,ConstructionTraceStage::Begin,"main-combination-array",1);
        int phase=1;auto main=k.LayeredMain(state,material,phase);
        if(main)
        {
            k.Choice(state,*main,phase==1?"large-combination-array":"large-original-array-fallback");
            k.Commit(state,*main,false,{},phase);
        }
        k.Phase(state,ConstructionTraceStage::Complete,"main-combination-array",phase);
        if(!main)break;
        if(k.Work().O.EnableGapFill)
            if(const auto band=k.OccupiedBand(state);band&&band->Width>0&&band->Height>0)
            {
                k.GapFill(state,material,*band,true);
                k.GapFill(state,material,*band,false);
            }
        k.Phase(state,ConstructionTraceStage::Begin,"next-blank-area",4);
        k.Phase(state,ConstructionTraceStage::Complete,"next-blank-area",4);
    }
}
void DeferredArray(Kernel& k,State& state,const std::string& material)
{
    k.Phase(state,ConstructionTraceStage::Begin,"all-main-arrays-before-gap-fill",10);
    while(!k.Work().Stop())
    {
        auto main=k.MainArray(state,material);if(!main)break;
        k.Choice(state,*main,"deferred-main-array");k.Commit(state,*main,false,{},10);
    }
    k.Phase(state,ConstructionTraceStage::Complete,"all-main-arrays-before-gap-fill",10);
    if(k.Work().O.EnableGapFill)
        if(const auto band=k.OccupiedBand(state))
        {k.GapFill(state,material,*band,true);k.GapFill(state,material,*band,false);}
}
void ImmediateArray(Kernel& k,State& state,const std::string& material)
{
    while(!k.Work().Stop())
    {
        auto main=k.MainArray(state,material);if(!main)break;
        k.Choice(state,*main,"immediate-main-array");k.Commit(state,*main,false,{},11);
        // Fill only this batch's real domain before growing another batch.
        if(k.Work().O.EnableGapFill)
        {k.GapFill(state,material,main->Box,true);k.GapFill(state,material,main->Box,false);}
        k.FillHoles(state,material);
    }
}
void RandomArray(Kernel& k,State& state,const std::string& material)
{
    std::mt19937_64 random(k.Work().O.Seed+state.Board.StockInstance);
    while(!k.Work().Stop())
    {
        std::vector<Candidate> choices;
        for(const auto part:k.LargeFirst())
        {choices=k.Arrays(state,material,part);if(!choices.empty()||k.Work().Stop())break;}
        if(choices.empty())break;
        const auto index=random()%std::min<std::size_t>(9,choices.size());
        k.Emit(ConstructionTraceStage::Selected,[&](ConstructionTraceEvent& event)
        {event.Source="random-array.draw";event.StateID=state.TraceID;event.CandidateID=choices[index].TraceID;
            event.Measures={{"pool",double(choices.size())},{"index",double(index)},{"seed",double(k.Work().O.Seed)}};});
        auto choice=std::move(choices[index]);k.Choice(state,choice,"random-main-array");k.Commit(state,choice,false,{},12);
        if(k.Work().O.EnableGapFill)
            if(const auto band=k.OccupiedBand(state))
            {k.GapFill(state,material,*band,true);k.GapFill(state,material,*band,false);}
    }
}
void NfpSingles(Kernel& k,State& state,const std::string& material)
{
    while(!k.Work().Stop())
    {
        auto choice=k.Single(state,material);if(!choice)break;
        k.Choice(state,*choice,"single-original-nfp");k.Commit(state,*choice,false,{},20);
        k.FillHoles(state,material);
    }
}
void Scanline(Kernel& k,State& state,const std::string& material)
{
    while(!k.Work().Stop())
    {
        auto choice=k.Single(state,material,false);if(!choice)break;
        k.Choice(state,*choice,"scanline-grid-original");k.Commit(state,*choice,false,{},21);
        k.FillHoles(state,material);
    }
}
void Strip(Kernel& k,State& state,const std::string& material)
{
    const auto physicalWidth=state.Board.Width,physicalHeight=state.Board.Height;
    std::optional<Rect> domain;
    while(!k.Work().Stop())
    {
        auto choice=k.Single(state,material,true,domain);if(!choice)break;
        const bool first=state.Board.Placements.empty();
        k.Choice(state,*choice,"strip-single-original");k.Commit(state,*choice,false,{},22);
        if(first)
        {
            const auto margin=k.Input().EdgeMargin;
            domain=Rect{margin,margin,physicalWidth-2*margin,physicalHeight-2*margin};
            if(k.Settings().XFirst)domain->Height=choice->Box.Y+choice->Box.Height-margin;
            else domain->Width=choice->Box.X+choice->Box.Width-margin;
            k.Emit(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
            {event.Source="strip.locked-domain";event.StateID=state.TraceID;
                event.Width=domain->Width;event.Height=domain->Height;
                event.Measures={{"domainX",domain->X},{"domainY",domain->Y},
                    {"physicalWidth",physicalWidth},{"physicalHeight",physicalHeight}};});
        }
        k.FillHoles(state,material);
    }
}
void ContourArrays(Kernel& k,State& state,const std::string& material)
{
    while(!k.Work().Stop())
    {
        auto choices=k.ContourArrays(state,material);if(choices.empty())break;
        k.RankCandidates(state,choices);
        k.Choice(state,choices.front(),"contour-unit-or-array");k.Commit(state,choices.front(),false,{},23);
        k.FillHoles(state,material);
    }
    if(k.Work().O.EnableGapFill&&!k.Work().Stop())k.Fill(state,material);
}
void RectangularArrays(Kernel& k,State& state,const std::string& material)
{
    while(!k.Work().Stop())
    {
        auto choices=k.Arrays(state,material);if(choices.empty())break;
        k.Choice(state,choices.front(),"rectangle-array");k.Commit(state,choices.front(),false,{},24);
    }
}
State SearchBoard(Kernel& K,State initial,const std::string& material)
    {
        const auto& P=K.Input();const auto& S=K.Settings();auto& B=K.Work();
        struct SearchTimer{Kernel& K;double Started;~SearchTimer(){K.AddSearchSeconds(K.Work().Seconds()-Started);}} timer{K,B.Seconds()};
        std::vector<double> Areas;std::map<std::string,std::size_t> Index;
        for(std::size_t i=0;i<P.Parts.size();++i){Areas.push_back(Area(P.Parts[i].Geometry));Index.emplace(P.Parts[i].ID,i);}
        if(B.Stop())return initial;
        const auto previousTime=B.LocalTimeLimit;
        const auto previousAttempts=B.LocalAttemptLimit;
        struct RestoreLimits
        {
            Budget& Work;double Time;std::size_t Attempts;
            ~RestoreLimits(){Work.LocalTimeLimit=Time;Work.LocalAttemptLimit=Attempts;}
        } restore{B,previousTime,previousAttempts};
        double remainingArea=0;
        for(std::size_t i=0;i<P.Parts.size();++i)if(P.Parts[i].Material==material)
            remainingArea+=(P.Parts[i].Quantity-initial.Used[i])*Areas[i];
        const auto width=std::max(0.,initial.Board.Width-2*P.EdgeMargin);
        const auto height=std::max(0.,initial.Board.Height-2*P.EdgeMargin);
        const auto boards=static_cast<std::size_t>(std::clamp(
            std::ceil(remainingArea/std::max(1e-30,width*height)),1.,100000.));
        const auto started=B.Seconds();
        const auto globalEnd=std::min(previousTime,B.O.TimeLimitSeconds);
        const auto remainingTime=std::max(0.,globalEnd-started);
        const auto originalTimeShare=remainingTime/boards;
        const auto attemptCap=std::min(previousAttempts,B.O.MaxPlacementAttempts);
        const auto availableAttempts=attemptCap-std::min(attemptCap,B.Attempts);
        const auto originalAttemptShare=std::min(availableAttempts,std::max<std::size_t>(1,availableAttempts/boards));
        const auto beginAttempts=B.Attempts;
        const auto stock=std::find_if(P.Stocks.begin(),P.Stocks.end(),[&](const Stock& value){return value.ID==initial.Board.StockID;});
        const bool repeatPlanning=S.EnableSheetTemplateReuse&&stock!=P.Stocks.end()&&
            initial.Board.StockInstance<stock->Quantity&&stock->Quantity-initial.Board.StockInstance>1&&
            std::any_of(P.Parts.begin(),P.Parts.end(),[&](const Part& part)
                {return part.Material==material&&part.Quantity-initial.Used[Index.at(part.ID)]>=2;});
        const auto first=std::find_if(P.Parts.begin(),P.Parts.end(),[&](const Part& p){return initial.Used[Index.at(p.ID)]<p.Quantity&&p.Material==material;});
        const auto allDemandPlaced=[&](const State& state)
        {for(std::size_t i=0;i<P.Parts.size();++i)if(P.Parts[i].Material==material&&state.Used[i]<P.Parts[i].Quantity)return false;return true;};
        const auto candidatesFor=[&](const State& state)
        {
            auto candidates=ContourArray(S.Kind)?K.ContourArrays(state,material):K.Arrays(state,material);
            if(S.EnableHoleNesting&&!K.RectanglesOnly())
            {
                auto holes=K.Holes(state,material);
                if(!holes.empty())
                {
                    candidates.insert(candidates.end(),std::make_move_iterator(holes.begin()),std::make_move_iterator(holes.end()));
                    K.LimitCandidates(candidates,S.MaxCandidates);
                }
            }
            if(S.Kind==Kind::ArrayReduceSearch&&!candidates.empty())
            {
                const auto base=candidates.front();
                candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const Candidate& c)
                {return c.Part!=base.Part||c.Template!=base.Template||c.Box.X!=base.Box.X||c.Box.Y!=base.Box.Y||
                    !((c.Columns==base.Columns&&c.Rows==base.Rows)||(c.Columns==base.Columns&&c.Rows+1==base.Rows)||(c.Columns+1==base.Columns&&c.Rows==base.Rows));}),candidates.end());
            }
            if(S.Kind==Kind::ArrayFirstSpecificationSearch&&first!=P.Parts.end())
                candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const Candidate& c){return c.Part!=Index.at(first->ID);}),candidates.end());
            return candidates;
        };
        // Build a feasible greedy incumbent before spending effort on branches.
        // Reaching a local search cap must never discard this board or prevent
        // later physical boards from using the same array construction.
        const double bootstrapSeconds=repeatPlanning?std::min(remainingTime/16,.1):0.;
        const auto candidateBootstrapCap=S.MaxCandidates>std::numeric_limits<std::size_t>::max()/16?
            std::numeric_limits<std::size_t>::max():S.MaxCandidates*16;
        const auto bootstrapAttempts=repeatPlanning?
            std::min(availableAttempts/16,candidateBootstrapCap):std::size_t{};
        B.LocalTimeLimit=started+std::max(originalTimeShare/3,bootstrapSeconds);
        const auto greedyAttempts=std::min(availableAttempts,std::max(
            std::min(originalAttemptShare,std::max<std::size_t>(1,originalAttemptShare/3)),bootstrapAttempts));
        B.LocalAttemptLimit=beginAttempts+greedyAttempts;
        State best=initial;
        while(!B.Stop())
        {
            auto candidates=candidatesFor(best);if(candidates.empty())break;
            K.Commit(best,candidates.front());
            if(S.Kind==Kind::FourBlockArray&&B.O.EnableGapFill)K.Fill(best,material,candidates.front().Box);
            if(allDemandPlaced(best))break;
        }
        // Local bootstrap limits are not global exhaustion. Restore the parent
        // before checking cancellation/time and before valuing this actual
        // board recipe; quantities alone cannot predict its repeatability.
        B.LocalTimeLimit=previousTime;B.LocalAttemptLimit=previousAttempts;
        if(B.Stop()||allDemandPlaced(best))return best;
        std::size_t copies=0;
        if(repeatPlanning&&!best.Board.Placements.empty())
        {
            copies=stock->Quantity-initial.Board.StockInstance-1;
            std::vector<std::size_t> needs(P.Parts.size());
            for(const auto& item:best.Board.Placements)++needs[Index.at(item.PartID)];
            bool any=false;
            for(std::size_t i=0;i<P.Parts.size();++i)
            {
                const auto need=needs[i];if(!need)continue;any=true;
                copies=std::min(copies,(P.Parts[i].Quantity-best.Used[i])/need);
            }
            if(!any)copies=0;
        }
        const double timeShare=std::max(originalTimeShare,std::min(remainingTime*.25,
            originalTimeShare*(1.+static_cast<double>(copies))));
        auto attemptShare=originalAttemptShare;
        const auto boostCap=availableAttempts/4;
        if(copies&&originalAttemptShare<boostCap)
        {
            // Saturate before multiplication, including SIZE_MAX quantities.
            const auto weighted=copies>=boostCap/originalAttemptShare?boostCap:
                std::min(boostCap,originalAttemptShare*(copies+1));
            attemptShare=std::max(originalAttemptShare,weighted);
        }
        double boardEnd=started+timeShare;
        auto boardAttemptEnd=beginAttempts+attemptShare;
        auto tailSeconds=B.O.EnableGapFill?timeShare*.2:0.;
        auto tailAttempts=B.O.EnableGapFill?attemptShare/5:std::size_t{};
        if(repeatPlanning)
        {
            // A bounded startup can exceed the tiny original physical-board
            // share. Charge it honestly and retain only the old tail allowance
            // when the recipe cannot repeat, rather than granting more DFS.
            const auto now=B.Seconds();
            tailSeconds=std::min(tailSeconds,std::max(0.,globalEnd-now));
            tailAttempts=std::min(attemptCap-B.Attempts,tailAttempts);
            boardEnd=std::min(globalEnd,std::max(boardEnd,now+tailSeconds));
            boardAttemptEnd=std::min(attemptCap,std::max(boardAttemptEnd,B.Attempts+tailAttempts));
        }
        // Keep a final share for the selected leaf's optional gap fill. The
        // global attempt counter remains cumulative; the node cap is per board.
        B.LocalTimeLimit=repeatPlanning?boardEnd-tailSeconds:
            (B.O.EnableGapFill?started+timeShare*.8:boardEnd);
        B.LocalAttemptLimit=boardAttemptEnd-tailAttempts;
        struct Node{State Value;std::size_t Depth;};
        std::vector<Node> stack;stack.push_back({initial,0});std::size_t nodes=0;
        while(!stack.empty()&&!B.Stop()&&nodes<S.MaxNodes)
        {
            auto node=std::move(stack.back());stack.pop_back();++nodes;++B.Nodes;
            K.Emit(ConstructionTraceStage::Expand,[&](ConstructionTraceEvent& event){event.Source="array.search";event.StateID=node.Value.TraceID;
                event.Layouts={node.Value.Board};event.Measures.emplace_back("depth",static_cast<double>(node.Depth));});
            if(K.Better(node.Value,best))best=node.Value;
            auto candidates=candidatesFor(node.Value);
            const auto count=std::min(candidates.size(),node.Depth<S.BranchDecisions?S.BranchWidth:std::size_t{1});
            K.Emit(ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event){event.Source="array.search.branch-limit";event.StateID=node.Value.TraceID;
                event.SearchTruncated=count<candidates.size();event.Measures.insert(event.Measures.end(),{{"before",static_cast<double>(candidates.size())},
                {"after",static_cast<double>(count)},{"depth",static_cast<double>(node.Depth)},{"branchDecisions",static_cast<double>(S.BranchDecisions)}});});
            // Depth/branch limits retain a feasible incumbent, never a feasibility certificate.
            for(std::size_t i=count;i>0;--i)
            {
                State child=node.Value;K.Commit(child,candidates[i-1]);
                if(S.Kind==Kind::FourBlockArray&&B.O.EnableGapFill)K.Fill(child,material,candidates[i-1].Box);
                if(K.Better(child,best))best=child;
                if(stack.size()<std::min<std::size_t>(256,S.MaxNodes))stack.push_back({std::move(child),node.Depth+1});
            }
            if(allDemandPlaced(best))break;
            K.Emit(ConstructionTraceStage::Backtrack,[&](ConstructionTraceEvent& event){event.Source="array.search";event.StateID=node.Value.TraceID;
                event.Reason="expanded-node-finished";event.Measures.emplace_back("pendingStates",static_cast<double>(stack.size()));});
        }
        B.LocalTimeLimit=boardEnd;B.LocalAttemptLimit=boardAttemptEnd;
        if(B.O.EnableGapFill&&!B.Stop()&&S.Kind!=Kind::FourBlockArray)K.Fill(best,material);
        K.Emit(ConstructionTraceStage::Selected,[&](ConstructionTraceEvent& event){event.Source="array.search.winner";event.StateID=best.TraceID;
            event.Layouts={best.Board};event.Reason=nodes>=S.MaxNodes?"node-cap":!stack.empty()?"budget-or-complete-demand":"frontier-exhausted";
            event.Measures.insert(event.Measures.end(),{{"nodes",static_cast<double>(nodes)},{"maxNodes",static_cast<double>(S.MaxNodes)}});});
        return best;
    }

void BoundedArraySearch(Kernel& k,State& state,const std::string& material)
{state=SearchBoard(k,std::move(state),material);}
}
BoardConstructor MakeBoardConstructor(Kind kind)
{
    BoardConstructor construct;const char* name=nullptr;
    switch(kind)
    {
    case Kind::ArrayQuantityDeferred:construct=LayeredArray;name="layered-array";break;
    case Kind::ArrayGeometryDeferred:case Kind::HistoricalFirstArray:construct=DeferredArray;name="deferred-array";break;
    case Kind::ArrayQuantityImmediate:case Kind::ArrayGeometryImmediate:construct=ImmediateArray;name="immediate-array";break;
    case Kind::RandomArrays:construct=RandomArray;name="random-array-restart";break;
    case Kind::NfpAllSpecifications:construct=NfpSingles;name="nfp-all-specifications";break;
    case Kind::NfpClosest:construct=NfpSingles;name="nfp-contact";break;
    case Kind::NfpDirectional:construct=NfpSingles;name="nfp-directional-contact";break;
    case Kind::NfpStrip:construct=Strip;name="nfp-strip";break;
    case Kind::Scanline:construct=Scanline;name="scanline";break;
    case Kind::NfpArray:case Kind::NfpMultiArray:construct=ContourArrays;name="contour-array";break;
    case Kind::ArrayFirstSpecificationSearch:case Kind::ArrayMultiSpecificationSearch:case Kind::ArrayReduceSearch:
    case Kind::RectangleBlockSearch:case Kind::FourBlockArray:construct=BoundedArraySearch;name="bounded-array-search";break;
    case Kind::RectangleContact:case Kind::SingleProductArray:case Kind::SingleColumn:
        construct=RectangularArrays;name="rectangular-array";break;
    default:throw std::invalid_argument("Board competition requires its own controller.");
    }
    return [construct=std::move(construct),name](Kernel& k,State& state,const std::string& material)
    {
        k.Emit(ConstructionTraceStage::Begin,[&](ConstructionTraceEvent& event)
        {event.Source="construction.controller";event.Reason=name;event.StateID=state.TraceID;});
        construct(k,state,material);
        k.SelectedPath(state);
        k.Emit(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {event.Source="board.raw-construction";event.StateID=state.TraceID;event.Layouts={state.Board};});
        if(k.Settings().EnableLayoutRefinement)
        {
            k.RefineTail(state,material);
            k.Emit(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
            {event.Source="board.after-tail-repack";event.StateID=state.TraceID;event.Layouts={state.Board};});
        }
        if(k.Settings().EnableContourSettling)
        {
            k.Settle(state);
            k.Emit(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
            {event.Source="board.after-allowed-angle-settling";event.StateID=state.TraceID;event.Layouts={state.Board};});
        }
        k.Emit(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {event.Source="construction.controller";event.Reason=name;event.StateID=state.TraceID;event.Layouts={state.Board};});
    };
}
}
