#include "SheetNestingPipeline.h"
#include "SheetNestingNfp.h"
#include "NestingNfpPlacement.h"
#include "NestingClearancePlacement.h"
#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    class NfpRefill final:public IPostProcessor
    {
    public:
        Result Run(const Problem& p,const Result& source,const Options& o)override
        {
            using Clock=std::chrono::steady_clock;const auto started=Clock::now();auto kept=source;Statistics work;work.VerificationSeconds=work.EvaluationSeconds=0;
            std::size_t moved=0,removed=0,trials=0;bool cancelled=false;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto stop=[&]{if(o.Cancel&&o.Cancel())cancelled=true;return cancelled||elapsed()>=o.TimeLimitSeconds||work.PlacementAttempts>=o.MaxPlacementAttempts;};
            const auto finish=[&](const std::string& reason)
            {
                kept.Stats=work;kept.Stats.CombinationGroups=source.Stats.CombinationGroups;kept.Stats.CircleLatticeGroups=source.Stats.CircleLatticeGroups;kept.Stats.GapFilledParts=source.Stats.GapFilledParts;
                kept.Stats.ElapsedSeconds=kept.Stats.PostProcessingSeconds=elapsed();
                if(cancelled)kept.State=Status::Cancelled;
                kept.Diagnostics.push_back("nfp.refill: "+reason+"; moved="+std::to_string(moved)+"; removed="+std::to_string(removed)+"; trials="+std::to_string(trials));return kept;
            };
            if(stop())return finish("cancelled-or-budget");
            if(!source.GeometryValidated||source.State!=Status::Complete||!source.Remaining.empty())return finish("requires-validated-complete-incumbent");
            if(source.Layouts.size()<2)return finish("one-used-sheet");
            if(p.Parts.size()>128||source.Layouts.size()>32)return finish("unsupported-scope");
            std::size_t count=0;for(const auto& l:source.Layouts)count+=l.Placements.size();if(count>512)return finish("placement-scope-cap");
            std::map<std::string,std::size_t> indices;std::map<std::string,std::string> materials;std::vector<double> areas;std::vector<std::vector<double>> angles;
            for(std::size_t i=0;i<p.Parts.size();++i){indices[p.Parts[i].ID]=i;areas.push_back(Detail::Area(p.Parts[i].Geometry));angles.push_back(Detail::CandidateAngles(p.Parts[i],p.Tolerance));}
            for(const auto& s:p.Stocks)materials[s.ID]=s.Material;
            const auto materialArea=[&](const Layout& l){double area=0;for(const auto& x:l.Placements)area+=areas[indices.at(x.PartID)];return area;};
            auto cache=std::make_shared<NfpCache>();std::map<std::string,std::unique_ptr<Detail::ClearanceNfpPlacement>> generators;
            const auto generatorFor=[&](const Layout& board)->Detail::ClearanceNfpPlacement&
            {
                auto found=generators.find(board.StockID);
                if(found==generators.end())
                {
                    const auto stock=std::find_if(p.Stocks.begin(),p.Stocks.end(),[&](const Stock& s){return s.ID==board.StockID;});
                    found=generators.emplace(board.StockID,std::make_unique<Detail::ClearanceNfpPlacement>(p,*stock,cache,stop)).first;
                }
                return *found->second;
            };
            if(stop())return finish("cancelled-or-budget");
            JobResult best;best.Output=kept;best.Checked=Validate(p,kept);best.Remnants=EvaluateRemnants(p,kept);if(!best.Checked.Valid)return finish("invalid-incumbent");
            bool changed=true;
            while(changed&&!stop()&&trials<96)
            {
                changed=false;
                for(std::size_t donor=kept.Layouts.size();donor-->0&&!stop()&&!changed&&trials<96;)
                {
                    const auto donorMaterial=materials.at(kept.Layouts[donor].StockID);const auto originals=kept.Layouts[donor].Placements;
                    for(int orderMode=0;orderMode<3&&!stop()&&!changed&&trials<96;++orderMode)
                    {
                        ++trials;auto proposal=kept;std::vector<std::size_t> order(originals.size());std::iota(order.begin(),order.end(),0);
                        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b)
                        {const auto aa=areas[indices.at(originals[a].PartID)],bb=areas[indices.at(originals[b].PartID)];return orderMode==1?aa<bb:orderMode==2?std::tie(originals[a].Transform.Y,originals[a].Transform.X)<std::tie(originals[b].Transform.Y,originals[b].Transform.X):aa>bb;});
                        std::size_t shifted=0;
                        for(const auto item:order)
                        {
                            if(stop())break;const auto original=originals[item];const auto pi=indices.at(original.PartID);bool placed=false;
                            std::vector<std::size_t> targets;
                            for(std::size_t i=0;i<proposal.Layouts.size();++i)if(i!=donor&&materials.at(proposal.Layouts[i].StockID)==donorMaterial)
                            {
                                // Move toward fuller compatible boards, never move a
                                // dense board's material backward just to shape a gap.
                                const auto& a=proposal.Layouts[i];const auto& b=proposal.Layouts[donor];
                                if(materialArea(a)/(a.Width*a.Height)+1e-12>=materialArea(b)/(b.Width*b.Height))targets.push_back(i);
                            }
                            std::stable_sort(targets.begin(),targets.end(),[&](auto a,auto b){const auto& x=proposal.Layouts[a];const auto& y=proposal.Layouts[b];return materialArea(x)/(x.Width*x.Height)>materialArea(y)/(y.Width*y.Height);});
                            for(const auto target:targets)
                            {
                                if(stop()||placed)break;
                                auto& generator=generatorFor(proposal.Layouts[target]);if(!generator.Ready())continue;
                                for(const auto angle:angles[pi])
                                {
                                    if(stop()||placed)break;Detail::NfpPlacementLimits limits;limits.MaxCandidates=1;limits.StopAfterFirstFit=true;limits.MaxRawCandidates=4096;limits.MaxContactSegments=512;limits.MaxAttempts=std::min<std::size_t>(4096,o.MaxPlacementAttempts-work.PlacementAttempts);
                                    const auto query=generator.Find(proposal.Layouts[target],pi,angle,limits,stop);work.PlacementAttempts+=query.Attempts;
                                    if(query.Fits.empty())continue;
                                    auto x=original;x.Transform=query.Fits.front();proposal.Layouts[target].Placements.push_back(x);
                                    auto& donorPlacements=proposal.Layouts[donor].Placements;const auto from=std::find_if(donorPlacements.begin(),donorPlacements.end(),[&](const Placement& q){return q.PartID==x.PartID&&q.Instance==x.Instance;});
                                    if(from==donorPlacements.end())throw std::logic_error("Original refill identity lost");donorPlacements.erase(from);++shifted;placed=true;
                                }
                            }
                        }
                        if(!shifted)continue;
                        if(stop())break; // Do not start another atomic full assessment after cancellation.
                        const bool empty=proposal.Layouts[donor].Placements.empty();if(empty)proposal.Layouts.erase(proposal.Layouts.begin()+donor);
                        for(auto& l:proposal.Layouts)l.MaterialArea=materialArea(l);
                        proposal.Stats={};JobResult candidate;candidate.Output=std::move(proposal);Detail::AssessCandidate(p,candidate);work.VerificationSeconds+=candidate.Output.Stats.VerificationSeconds;work.EvaluationSeconds+=candidate.Output.Stats.EvaluationSeconds;
                        if(Detail::Eligible(candidate)&&Detail::Quality(candidate,p,o.Ranking)<Detail::Quality(best,p,o.Ranking))
                        {best=std::move(candidate);kept=best.Output;moved+=shifted;removed+=empty;changed=true;}
                    }
                }
            }
            return finish(cancelled?"cancelled":elapsed()>=o.TimeLimitSeconds?"time-limit":"finite-forward-transfers-exhausted");
        }
    };
}
PostProcessStage MakeNfpRefillStage(){return {"nfp.refill",[]{return std::make_unique<NfpRefill>();}};}
}
