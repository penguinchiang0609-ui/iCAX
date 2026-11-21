#include "SheetNestingPlaneSheetMix.h"
#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
using Clock=std::chrono::steady_clock;
bool SettingsValid(const PlaneSheetMixSettings& settings)
{
    return settings.MaxRecipes&&settings.MaxRecipes<=128&&settings.MaxConstructionProbes<=512&&
        settings.MaxSearchNodes&&settings.MaxSearchNodes<=1000000&&settings.MaxTailTrials<=128&&
        std::isfinite(settings.SecondsPerProbe)&&settings.SecondsPerProbe>0&&
        std::isfinite(settings.SecondsPerTail)&&settings.SecondsPerTail>0;
}
struct Recipe
{
    Layout Board;
    std::vector<std::size_t> Need;
    std::size_t Stock=0;
    double Material=0;
};
struct Selection
{
    std::vector<std::size_t> Remaining,StockUsed,Copies;
    std::size_t Boards=0;
};
struct TailSeed {Selection Selected;std::size_t Bound=0,Unplaced=0;};
class Mixer
{
    const Problem& P;
    const Result& Source;
    const Options& O;
    StrategyFactory Construct;
    PlaneSheetMixSettings S;
    Clock::time_point Started=Clock::now();
    Statistics Work;
    Result Kept;
    JobResult Best;
    std::map<std::string,std::size_t> Parts,Stocks;
    std::vector<double> Areas;
    std::vector<Recipe> Recipes;
    std::set<std::string> RecipeKeys,Visited;
    std::vector<TailSeed> Tails;
    std::size_t Nodes=0,Probes=0,TailTrials=0,Improvements=0,ExactSelections=0;
    bool Cancelled=false,CallbackFailed=false,SearchLimited=false;

    double Seconds()const{return std::chrono::duration<double>(Clock::now()-Started).count();}
    bool Poll(bool chargeLimit=true)
    {
        if(!Cancelled&&!CallbackFailed&&O.Cancel)
        {
            try{if(O.Cancel())Cancelled=true;}
            catch(...){CallbackFailed=true;}
        }
        return Cancelled||CallbackFailed||Seconds()>=O.TimeLimitSeconds||(chargeLimit&&Work.PlacementAttempts>=O.MaxPlacementAttempts);
    }
    bool Charge()
    {if(Poll())return false;++Work.PlacementAttempts;return true;}
    Options Budget(double seconds,std::size_t attempts)
    {
        auto result=O;result.EnableBasicCompaction=false;
        result.TimeLimitSeconds=std::max(0.,std::min(seconds,O.TimeLimitSeconds-Seconds()));
        result.MaxPlacementAttempts=std::min(attempts,O.MaxPlacementAttempts-Work.PlacementAttempts);
        result.Cancel=[this]{return Poll();};return result;
    }
    void Measure(const JobResult& result)
    {
        Work.VerificationSeconds+=std::max(0.,result.Output.Stats.VerificationSeconds);
        Work.EvaluationSeconds+=std::max(0.,result.Output.Stats.EvaluationSeconds);
    }
    std::string RecipeKey(const Layout& board)const
    {
        std::vector<Placement> values=board.Placements;
        std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b)
        {return std::tie(a.PartID,a.Transform.AngleDegrees,a.Transform.Mirrored,a.Transform.Y,a.Transform.X,a.GapFiller)<
                std::tie(b.PartID,b.Transform.AngleDegrees,b.Transform.Mirrored,b.Transform.Y,b.Transform.X,b.GapFiller);});
        std::ostringstream key;key.precision(17);key<<board.StockID<<'\n';
        std::map<std::size_t,std::size_t> groups;std::size_t nextGroup=1;
        for(const auto& member:values)
        {
            std::size_t group=0;
            if(member.Group)
            {
                const auto [found,inserted]=groups.emplace(member.Group,nextGroup);
                if(inserted)++nextGroup;group=found->second;
            }
            key<<member.PartID.size()<<':'<<member.PartID<<':'<<member.Transform.X<<':'<<member.Transform.Y<<':'
                <<member.Transform.AngleDegrees<<':'<<member.Transform.Mirrored<<':'<<group<<':'<<member.GapFiller<<';';
        }
        return key.str();
    }
    bool AddRecipe(const Layout& board)
    {
        if(Poll()||Recipes.size()>=S.MaxRecipes||board.Placements.empty())return false;
        const auto stock=Stocks.find(board.StockID);
        if(stock==Stocks.end()||board.Width!=P.Stocks[stock->second].Width||board.Height!=P.Stocks[stock->second].Height)return false;
        const auto key=RecipeKey(board);if(RecipeKeys.contains(key))return false;
        Recipe recipe;recipe.Board=board;recipe.Board.StockInstance=0;recipe.Stock=stock->second;recipe.Need.resize(P.Parts.size());
        for(auto& member:recipe.Board.Placements)
        {
            if(!Charge())return false;
            const auto part=Parts.find(member.PartID);if(part==Parts.end())return false;
            if(P.Parts[part->second].Material!=P.Stocks[recipe.Stock].Material)return false;
            member.Instance=recipe.Need[part->second]++;
            if(recipe.Need[part->second]>P.Parts[part->second].Quantity)return false;
            recipe.Material+=Areas[part->second];
        }
        Problem local=P;local.Stocks={P.Stocks[recipe.Stock]};local.Stocks[0].Quantity=1;
        for(std::size_t i=0;i<local.Parts.size();++i)local.Parts[i].Quantity=recipe.Need[i];
        Result check;check.State=Status::Complete;check.Layouts={recipe.Board};
        const auto started=Clock::now();const auto valid=Validate(local,check);
        Work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-started).count();
        if(!valid.Valid||!valid.Complete||Poll())return false;
        recipe.Board.MaterialArea=recipe.Material;Recipes.push_back(std::move(recipe));RecipeKeys.insert(key);return true;
    }
    void ProbeRecipes()
    {
        if(!S.MaxConstructionProbes||Poll())return;
        struct Probe {std::size_t Stock;std::size_t Focus;bool Reverse;};
        constexpr auto all=std::numeric_limits<std::size_t>::max();
        std::vector<Probe> probes;
        for(std::size_t si=0;si<P.Stocks.size();++si)if(P.Stocks[si].Quantity)probes.push_back({si,all,false});
        for(std::size_t pi=0;pi<P.Parts.size();++pi)if(P.Parts[pi].Quantity)
            for(std::size_t si=0;si<P.Stocks.size();++si)
                if(P.Stocks[si].Quantity&&P.Parts[pi].Material==P.Stocks[si].Material)probes.push_back({si,pi,false});
        for(std::size_t si=0;si<P.Stocks.size();++si)if(P.Stocks[si].Quantity)probes.push_back({si,all,true});
        if(probes.size()>S.MaxConstructionProbes)probes.resize(S.MaxConstructionProbes);
        const double phaseEnd=Seconds()+std::max(0.,O.TimeLimitSeconds-Seconds())*.35;
        for(const auto& probe:probes)
        {
            if(Poll()||Recipes.size()>=S.MaxRecipes||Seconds()>=phaseEnd)break;
            Problem local=P;local.Stocks={P.Stocks[probe.Stock]};local.Stocks[0].Quantity=1;
            if(probe.Focus!=all)for(std::size_t i=0;i<local.Parts.size();++i)if(i!=probe.Focus)local.Parts[i].Quantity=0;
            if(probe.Reverse)std::reverse(local.Parts.begin(),local.Parts.end());
            const auto remaining=std::max<std::size_t>(1,probes.size()-Probes);
            auto q=Budget(std::min(S.SecondsPerProbe,std::max(0.,phaseEnd-Seconds())/static_cast<double>(remaining)),
                std::max<std::size_t>(1,(O.MaxPlacementAttempts-Work.PlacementAttempts)/(2*remaining)));
            if(q.TimeLimitSeconds<=0||!q.MaxPlacementAttempts)break;
            q.Seed+=Probes;
            Result trial;
            try
            {
                auto solver=Construct();
                if(!solver){++Probes;continue;}
                trial=solver->Run(local,q);
            }
            catch(...){++Probes;continue;}
            ++Probes;Work.PlacementAttempts+=std::min(trial.Stats.PlacementAttempts,q.MaxPlacementAttempts);
            const auto started=Clock::now();const auto valid=Validate(local,trial);
            Work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-started).count();
            if(!valid.Valid||Poll())continue;
            for(const auto& board:trial.Layouts)if(!Poll())AddRecipe(board);
        }
    }
    std::size_t LowerBound(const Selection& state)const
    {
        std::map<std::string,long double> required;
        for(std::size_t i=0;i<P.Parts.size();++i)if(state.Remaining[i])
            required[P.Parts[i].Material]+=static_cast<long double>(Areas[i])*state.Remaining[i];
        std::size_t bound=0;
        for(const auto& [material,area]:required)
        {
            long double capacity=0;
            for(std::size_t i=0;i<P.Stocks.size();++i)if(P.Stocks[i].Material==material&&state.StockUsed[i]<P.Stocks[i].Quantity)
                capacity=std::max(capacity,static_cast<long double>(P.Stocks[i].Width)*P.Stocks[i].Height);
            if(capacity<=0)return std::numeric_limits<std::size_t>::max();
            const auto sheets=std::ceil(area/capacity-1e-12L);
            if(sheets>static_cast<long double>(Kept.Layouts.size()))return std::numeric_limits<std::size_t>::max();
            bound+=static_cast<std::size_t>(std::max(0.L,sheets));
        }
        return bound;
    }
    std::size_t Fits(const Recipe& recipe,const Selection& state)const
    {
        if(state.Copies[&recipe-Recipes.data()])return 0;
        std::size_t count=P.Stocks[recipe.Stock].Quantity-state.StockUsed[recipe.Stock];
        for(std::size_t i=0;i<recipe.Need.size();++i)if(recipe.Need[i])count=std::min(count,state.Remaining[i]/recipe.Need[i]);
        return std::min(count,Kept.Layouts.size()-state.Boards);
    }
    std::string StateKey(const Selection& state)const
    {
        std::ostringstream key;
        for(auto n:state.Remaining)key<<n<<',';key<<'|';
        for(auto n:state.StockUsed)key<<n<<',';key<<'|';
        for(auto n:state.Copies)key<<(n!=0);return key.str();
    }
    void SaveTail(const Selection& state,std::size_t bound)
    {
        if(!S.MaxTailTrials||!state.Boards||state.Boards+bound>Kept.Layouts.size())return;
        const auto unplaced=std::accumulate(state.Remaining.begin(),state.Remaining.end(),std::size_t{});
        if(!unplaced)return;
        const auto duplicate=std::any_of(Tails.begin(),Tails.end(),[&](const TailSeed& old)
        {return old.Selected.Remaining==state.Remaining&&old.Selected.StockUsed==state.StockUsed;});
        if(duplicate)return;
        Tails.push_back({state,state.Boards+bound,unplaced});
        std::stable_sort(Tails.begin(),Tails.end(),[](const TailSeed& a,const TailSeed& b)
        {return std::tie(a.Bound,a.Unplaced,a.Selected.Boards)<std::tie(b.Bound,b.Unplaced,b.Selected.Boards);});
        if(Tails.size()>4*S.MaxTailTrials)Tails.resize(4*S.MaxTailTrials);
    }
    bool Build(const Selection& selected,const std::vector<Layout>& tail,Result& output)
    {
        output={};output.State=Status::Complete;
        std::vector<std::size_t> used(P.Parts.size()),stockUsed(P.Stocks.size());std::size_t nextGroup=1;
        const auto append=[&](const Layout& original)
        {
            if(Poll())return false;
            const auto stock=Stocks.find(original.StockID);
            if(stock==Stocks.end()||stockUsed[stock->second]>=P.Stocks[stock->second].Quantity)return false;
            Layout board=original;board.StockInstance=stockUsed[stock->second]++;board.MaterialArea=0;
            std::map<std::size_t,std::size_t> groups,counts;
            for(auto& member:board.Placements)
            {
                if(!Charge())return false;
                const auto found=Parts.find(member.PartID);
                if(found==Parts.end()||used[found->second]>=P.Parts[found->second].Quantity)return false;
                member.Instance=used[found->second]++;board.MaterialArea+=Areas[found->second];
                if(member.Group)
                {
                    const auto [where,inserted]=groups.emplace(member.Group,nextGroup);
                    if(inserted)++nextGroup;member.Group=where->second;++counts[member.Group];
                }
                output.Stats.GapFilledParts+=member.GapFiller;
            }
            for(const auto& [group,count]:counts){(void)group;output.Stats.CombinationGroups+=count>1;}
            if(Poll(false))return false;
            output.Layouts.push_back(std::move(board));return true;
        };
        for(std::size_t i=0;i<Recipes.size();++i)for(std::size_t n=0;n<selected.Copies[i];++n)
            if(!append(Recipes[i].Board))return false;
        for(const auto& board:tail)if(!append(board))return false;
        for(std::size_t i=0;i<P.Parts.size();++i)if(used[i]!=P.Parts[i].Quantity)return false;
        return !Poll(false);
    }
    void Consider(const Selection& selected,const std::vector<Layout>& tail={})
    {
        if(Poll()||selected.Boards+tail.size()>Kept.Layouts.size())return;
        Result output;if(!Build(selected,tail,output)||Poll(false))return;
        JobResult candidate;candidate.Output=std::move(output);Detail::AssessCandidate(P,candidate);Measure(candidate);
        if(!candidate.Checked.Valid||!candidate.Checked.Complete||Poll(false))return;
        if(Detail::Eligible(candidate)&&Detail::Quality(candidate,P,O.Ranking)<Detail::Quality(Best,P,O.Ranking))
        {Best=std::move(candidate);Kept=Best.Output;++Improvements;}
    }
    void Search(const Selection& state)
    {
        if(Poll())return;
        if(Nodes>=S.MaxSearchNodes){SearchLimited=true;return;}
        ++Nodes;if(!Charge())return;
        const auto lower=LowerBound(state);
        if(lower==std::numeric_limits<std::size_t>::max()||state.Boards+lower>Kept.Layouts.size())return;
        if(!Visited.insert(StateKey(state)).second)return;
        const bool complete=std::all_of(state.Remaining.begin(),state.Remaining.end(),[](auto n){return n==0;});
        if(complete){++ExactSelections;Consider(state);return;}
        SaveTail(state,lower);
        if(state.Boards>=Kept.Layouts.size())return;
        std::size_t pivot=P.Parts.size(),fewest=std::numeric_limits<std::size_t>::max();
        for(std::size_t pi=0;pi<state.Remaining.size();++pi)if(state.Remaining[pi])
        {
            std::size_t choices=0;for(const auto& recipe:Recipes)choices+=recipe.Need[pi]&&Fits(recipe,state)>0;
            // An uncovered rare specification belongs to residual rebuilding;
            // it must not prevent selecting useful recipes for other originals.
            if(choices&&choices<fewest){pivot=pi;fewest=choices;}
        }
        if(pivot==P.Parts.size()||!fewest)return;
        for(std::size_t ri=0;ri<Recipes.size()&&!Poll();++ri)
        {
            const auto& recipe=Recipes[ri];if(!recipe.Need[pivot])continue;
            const auto maximum=Fits(recipe,state);
            for(std::size_t copies=maximum;copies&&!Poll();--copies)
            {
                if(Nodes>=S.MaxSearchNodes){SearchLimited=true;return;}
                Selection next=state;next.Copies[ri]=copies;next.StockUsed[recipe.Stock]+=copies;next.Boards+=copies;
                for(std::size_t pi=0;pi<recipe.Need.size();++pi)next.Remaining[pi]-=recipe.Need[pi]*copies;
                Search(next);
            }
        }
    }
    void RebuildTails()
    {
        for(const auto& seed:Tails)
        {
            if(Poll()||TailTrials>=S.MaxTailTrials)break;
            if(seed.Bound>Kept.Layouts.size())continue;
            Problem residual=P;
            for(std::size_t i=0;i<residual.Parts.size();++i)residual.Parts[i].Quantity=seed.Selected.Remaining[i];
            for(std::size_t i=0;i<residual.Stocks.size();++i)residual.Stocks[i].Quantity-=seed.Selected.StockUsed[i];
            auto q=Budget(S.SecondsPerTail,std::max<std::size_t>(1,(O.MaxPlacementAttempts-Work.PlacementAttempts)/
                std::max<std::size_t>(2,S.MaxTailTrials-TailTrials+1)));
            if(q.TimeLimitSeconds<=0||!q.MaxPlacementAttempts)break;
            Result trial;
            try
            {
                auto solver=Construct();
                if(!solver){++TailTrials;continue;}
                trial=solver->Run(residual,q);
            }
            catch(...){++TailTrials;continue;}
            ++TailTrials;Work.PlacementAttempts+=std::min(trial.Stats.PlacementAttempts,q.MaxPlacementAttempts);
            if(Poll())break;
            const auto started=Clock::now();const auto valid=Validate(residual,trial);
            Work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-started).count();
            if(valid.Valid&&valid.Complete&&!Poll())Consider(seed.Selected,trial.Layouts);
        }
    }
    Result Finish(const std::string& reason)
    {
        const auto combinations=Kept.Stats.CombinationGroups,circles=Kept.Stats.CircleLatticeGroups,fillers=Kept.Stats.GapFilledParts;
        Kept.Stats=Work;Kept.Stats.CombinationGroups=combinations;Kept.Stats.CircleLatticeGroups=circles;Kept.Stats.GapFilledParts=fillers;
        Kept.Stats.ElapsedSeconds=Kept.Stats.PostProcessingSeconds=Seconds();
        if(Cancelled)Kept.State=Status::Cancelled;
        Kept.Diagnostics.push_back("plane.sheet-mix: "+reason+" recipes="+std::to_string(Recipes.size())+
            " probes="+std::to_string(Probes)+" nodes="+std::to_string(Nodes)+" exact-selections="+std::to_string(ExactSelections)+
            " tail-trials="+std::to_string(TailTrials)+" improvements="+std::to_string(Improvements)+
            "; current-task recipes, joint integer copies, original inventory, shared quality and atomic complete acceptance");
        return Kept;
    }
public:
    Mixer(const Problem& p,const Result& source,const Options& options,StrategyFactory construct,PlaneSheetMixSettings settings)
        :P(p),Source(source),O(options),Construct(std::move(construct)),S(settings),Kept(source)
    {Work.VerificationSeconds=Work.EvaluationSeconds=0;}
    Result Run()
    {
        if(!SettingsValid(S)||!Construct||!std::isfinite(O.TimeLimitSeconds)||O.TimeLimitSeconds<=0||!O.MaxPlacementAttempts)
            return Finish("invalid-settings-or-budget; incumbent retained");
        if(Poll())return Finish("cancelled-or-budget; incumbent retained");
        const auto started=Clock::now();const auto checked=Validate(P,Source);
        Work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-started).count();
        if(!checked.Valid||!checked.Complete||!Source.Remaining.empty()||Source.Layouts.empty())
            return Finish("requires complete original-valid incumbent");
        Kept.GeometryValidated=true;
        for(std::size_t i=0;i<P.Parts.size();++i){Parts.emplace(P.Parts[i].ID,i);Areas.push_back(Detail::Area(P.Parts[i].Geometry));}
        for(std::size_t i=0;i<P.Stocks.size();++i)Stocks.emplace(P.Stocks[i].ID,i);
        Best.Output=Source;Best.Output.Stats.VerificationSeconds=Best.Output.Stats.EvaluationSeconds=0;
        Detail::AssessCandidate(P,Best);Measure(Best);
        if(Poll())return Finish("cancelled-or-budget; incumbent retained");
        const auto sourceLimit=S.MaxConstructionProbes?std::max<std::size_t>(1,S.MaxRecipes/2):S.MaxRecipes;
        for(const auto& board:Source.Layouts)
        {if(Poll()||Recipes.size()>=sourceLimit)break;AddRecipe(board);}
        ProbeRecipes();
        if(Poll())return Finish("cancelled-or-budget; best complete incumbent retained");
        std::stable_sort(Recipes.begin(),Recipes.end(),[&](const Recipe& a,const Recipe& b)
        {
            const auto ad=a.Material/(a.Board.Width*a.Board.Height),bd=b.Material/(b.Board.Width*b.Board.Height);
            if(ad!=bd)return ad>bd;
            if(a.Board.Placements.size()!=b.Board.Placements.size())return a.Board.Placements.size()>b.Board.Placements.size();
            return std::tie(a.Stock,a.Need)<std::tie(b.Stock,b.Need);
        });
        Selection initial;initial.StockUsed.resize(P.Stocks.size());initial.Copies.resize(Recipes.size());
        for(const auto& part:P.Parts)initial.Remaining.push_back(part.Quantity);
        if(!Recipes.empty())Search(initial);
        if(!Poll())RebuildTails();
        return Finish(Cancelled?"cancelled":CallbackFailed?"callback failed safely":Seconds()>=O.TimeLimitSeconds?"time limit":
            Work.PlacementAttempts>=O.MaxPlacementAttempts?"attempt limit":SearchLimited?"bounded integer search limit":"bounded recipe and residual search completed");
    }
};
class SheetMix final:public IPostProcessor
{
    StrategyFactory Construct;PlaneSheetMixSettings Settings;
public:
    SheetMix(StrategyFactory construct,PlaneSheetMixSettings settings):Construct(std::move(construct)),Settings(settings){}
    Result Run(const Problem& p,const Result& incumbent,const Options& options)override
    {return Mixer(p,incumbent,options,Construct,Settings).Run();}
};
}
Result OptimizePlaneSheetMix(const Problem& problem,const Result& incumbent,const Options& options,
    StrategyFactory construct,PlaneSheetMixSettings settings)
{return Mixer(problem,incumbent,options,std::move(construct),settings).Run();}
PostProcessStage MakePlaneSheetMixStage(StrategyFactory construct,PlaneSheetMixSettings settings)
{
    if(!construct||!SettingsValid(settings))throw std::invalid_argument("Invalid plane sheet-mix factory or settings.");
    return {"plane.sheet-mix",[construct=std::move(construct),settings]{return std::make_unique<SheetMix>(construct,settings);}};
}
}
