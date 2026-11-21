#include "SheetNestingPlaneCompaction.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Problem Tiles(std::size_t count=2)
{
    Problem p;p.PartGap=.25;p.EdgeMargin=.25;p.Tolerance=1e-7;
    Part tile;tile.ID="fixed-original-tile";tile.Quantity=count;tile.Material="optimization-alloy";
    tile.Geometry.Outer={{0,0},{1,0},{1,1},{0,1}};tile.FreeRotation=false;tile.AllowedAnglesDegrees={0};
    p.Parts={tile};p.Stocks={{"optimization-stock",20,20,8,1,tile.Material}};return p;
}
Options Limits()
{Options o;o.TimeLimitSeconds=10;o.MaxPlacementAttempts=100000;o.EnableBasicCompaction=false;o.Seed=173;return o;}
PlaneCompactionSettings LocalOnly()
{
    PlaneCompactionSettings s;s.Rotate=s.Reassemble=s.ShrinkRegion=false;s.StableIterations=8;
    s.PerturbEvery=1;s.MaxPerturbationAttempts=64;s.MaxRelativeEnvelopeGrowth=.25;
    s.RefillAcrossSheets=s.MixSheetRecipes=false;return s;
}
Result Packed(const Problem& p)
{
    Result r;r.State=Status::Complete;r.GeometryValidated=true;
    r.Layouts={{p.Stocks[0].ID,0,20,20,{{p.Parts[0].ID,0,{.25,.25,0},11,false},
        {p.Parts[0].ID,1,{1.5,.25,0},12,true}},2}};return r;
}
Result Spread(const Problem& p)
{
    Result r;r.State=Status::Complete;r.GeometryValidated=true;
    for(std::size_t bi=0;bi<2;++bi)
        r.Layouts.push_back({p.Stocks[0].ID,bi?6u:2u,20,20,{
            {p.Parts[0].ID,bi?1u:3u,{4,4,0},20+bi*2,false},
            {p.Parts[0].ID,bi?2u:0u,{9,8,0},21+bi*2,true}},2});
    Require(Validate(p,r).Valid&&Validate(p,r).Complete,"sparse multi-board fixture is not complete");return r;
}
std::string Poses(const Result& r)
{
    std::ostringstream text;text.precision(17);
    for(const auto& b:r.Layouts)for(const auto& q:b.Placements)
        text<<b.StockID<<':'<<b.StockInstance<<':'<<q.PartID<<':'<<q.Instance<<':'<<q.Transform.X<<':'<<q.Transform.Y<<':'<<q.Transform.AngleDegrees<<';';
    return text.str();
}
using Identity=std::pair<std::string,std::size_t>;
using Origin=std::tuple<std::string,std::size_t,std::size_t,bool>;
std::map<Identity,Origin> Provenance(const Result& r)
{
    std::map<Identity,Origin> values;
    for(const auto& b:r.Layouts)for(const auto& q:b.Placements)
        Require(values.emplace(Identity{q.PartID,q.Instance},Origin{b.StockID,b.StockInstance,q.Group,q.GapFiller}).second,
            "optimization duplicated an original identity");
    return values;
}
bool Has(const Result& r,const std::string& word)
{return std::any_of(r.Diagnostics.begin(),r.Diagnostics.end(),[&](const auto& line){return line.find(word)!=std::string::npos;});}
const Layout& Physical(const Result& r,std::size_t stockInstance)
{
    const auto board=std::find_if(r.Layouts.begin(),r.Layouts.end(),[&](const auto& value){return value.StockInstance==stockInstance;});
    if(board==r.Layouts.end())throw std::runtime_error("physical board identity missing after checkpoint");return *board;
}
std::string BoardPoses(const Layout& board)
{
    Result r;r.Layouts={board};return Poses(r);
}
double EnvelopeArea(const Problem& p,const Layout& board)
{
    double lx=0,ly=0,rx=0,ty=0;bool first=true;
    for(const auto& member:board.Placements)
    {
        const auto part=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& value){return value.ID==member.PartID;});
        const auto b=Detail::Bounds(part->Geometry,member.Transform);
        if(first){lx=b.X;ly=b.Y;rx=b.X+b.Width;ty=b.Y+b.Height;first=false;}
        else{lx=std::min(lx,b.X);ly=std::min(ly,b.Y);rx=std::max(rx,b.X+b.Width);ty=std::max(ty,b.Y+b.Height);}
    }
    return (rx-lx)*(ty-ly);
}
void Safe(const Problem& p,const Result& result)
{
    const auto checked=Validate(p,result);Require(checked.Valid&&checked.Complete,"optimization lost the complete original order");
    std::map<std::string,const Part*> parts;for(const auto& part:p.Parts)parts.emplace(part.ID,&part);
    for(const auto& board:result.Layouts)for(std::size_t i=0;i<board.Placements.size();++i)
    {
        const auto& a=board.Placements[i];const auto geometry=AxisSafety::Make(parts.at(a.PartID)->Geometry,a.Transform);
        Require(AxisSafety::Containment(geometry,board.Width,board.Height,p.EdgeMargin)==AxisSafety::Verdict::Safe,
            "optimization spent nominal board margin");
        for(std::size_t j=0;j<i;++j)
        {
            const auto& b=board.Placements[j];
            Require(AxisSafety::Pair(geometry,AxisSafety::Make(parts.at(b.PartID)->Geometry,b.Transform),p.PartGap,
                p.AllowCommonEdges,p.Tolerance)==AxisSafety::Verdict::Safe,"optimization spent nominal original-material distance");
        }
    }
}
class RowConstructor final:public IStrategy
{
    bool SpreadOut;
public:
    explicit RowConstructor(bool spaced):SpreadOut(spaced){}
    Result Run(const Problem& p,const Options& o)override
    {
        Result r;std::vector<std::size_t> used(p.Parts.size());bool cancelled=false;
        const auto stop=[&]{if(o.Cancel&&o.Cancel())cancelled=true;return cancelled||r.Stats.PlacementAttempts>=o.MaxPlacementAttempts;};
        for(const auto& stock:p.Stocks)for(std::size_t si=0;si<stock.Quantity&&!stop();++si)
        {
            Layout b{stock.ID,si,stock.Width,stock.Height,{},0};double x=p.EdgeMargin;
            for(std::size_t pi=0;pi<p.Parts.size()&&!stop();++pi)
            {
                if(p.Parts[pi].Material!=stock.Material)continue;
                const auto box=Detail::Bounds(p.Parts[pi].Geometry);
                while(used[pi]<p.Parts[pi].Quantity&&!stop())
                {
                    if(SpreadOut&&b.Placements.size()>=2)break;
                    const double px=SpreadOut?(b.Placements.empty()?4.:9.):x;
                    const double py=SpreadOut?(b.Placements.empty()?4.:8.):p.EdgeMargin;
                    if(px+box.Width>stock.Width-p.EdgeMargin||py+box.Height>stock.Height-p.EdgeMargin)break;
                    ++r.Stats.PlacementAttempts;b.Placements.push_back({p.Parts[pi].ID,used[pi]++,{px-box.X,py-box.Y,0},si+1,false});
                    b.MaterialArea+=Detail::Area(p.Parts[pi].Geometry);x+=box.Width+p.PartGap;
                }
            }
            if(b.Placements.empty())break;r.Layouts.push_back(std::move(b));
        }
        for(std::size_t pi=0;pi<p.Parts.size();++pi)if(used[pi]<p.Parts[pi].Quantity)
            r.Remaining.push_back({p.Parts[pi].ID,p.Parts[pi].Quantity-used[pi]});
        r.State=cancelled?Status::Cancelled:r.Remaining.empty()?Status::Complete:Status::Partial;
        r.GeometryValidated=Validate(p,r).Valid;return r;
    }
};
StrategyFactory Constructor(bool spaced){return [spaced]{return std::make_unique<RowConstructor>(spaced);};}
}

int main()try
{
    const auto p=Tiles();const auto initial=Packed(p);const auto original=Poses(initial);
    auto settings=LocalOnly();
    const auto perturbed=OptimizePlaneLayout(p,initial,Limits(),settings);Safe(p,perturbed.Output);
    Require(perturbed.Perturbations>0&&perturbed.AcceptedWorse>0,
        "bounded perturbation never exercised a feasible worse current state");
    Require(perturbed.BestUtilization>=perturbed.InitialUtilization&&Poses(initial)==original,
        "temporary deterioration changed the original input or degraded published best");
    Require(Provenance(perturbed.Output)==Provenance(initial),"single-board perturbation changed original provenance");
    Require(perturbed.Output.Stats.PlacementAttempts<=Limits().MaxPlacementAttempts,"single-board perturbation exceeded attempts");
    std::cout<<"bounded-perturbation: probes="<<perturbed.Perturbations<<" accepted-worse="<<perturbed.AcceptedWorse
        <<" initial-U="<<perturbed.InitialUtilization<<" best-U="<<perturbed.BestUtilization<<'\n';

    auto cancelled=Limits();cancelled.Cancel=[]{return true;};
    const auto stopped=OptimizePlaneLayout(p,initial,cancelled,settings);Safe(p,stopped.Output);
    Require(stopped.Stop==PlaneOptimizationStop::Cancelled&&Poses(stopped.Output)==original,
        "single-board cancellation discarded the original complete best");
    for(std::size_t attempts:{0u,1u,4u})
    {
        auto options=Limits();options.MaxPlacementAttempts=attempts;
        const auto bounded=OptimizePlaneLayout(p,initial,options,settings);Safe(p,bounded.Output);
        Require(bounded.Output.Stats.PlacementAttempts<=attempts&&bounded.BestUtilization>=bounded.InitialUtilization,
            "bounded single-board optimization lost its complete best or overspent attempts");
    }

    const auto multiProblem=Tiles(4);const auto spread=Spread(multiProblem);const auto inputSignature=Poses(spread);
    auto local=LocalOnly();local.EnablePerturbation=false;local.StableIterations=3;
    const auto compacted=MakePlaneOptimizationStage(local,Constructor(false)).Factory()->Run(multiProblem,spread,Limits());
    Safe(multiProblem,compacted);
    Require(compacted.Layouts.size()==2&&Provenance(compacted)==Provenance(spread),
        "isolated per-board optimization lost sparse original instances, stock instances, groups or filler tags");
    Require(Poses(compacted)!=inputSignature&&Poses(spread)==inputSignature&&Has(compacted,"plane.optimize-job:"),
        "multi-board stage did not compact physical sheets independently");
    Require(Has(compacted,"reused-refinements=1"),"equal physical recipe copies were redundantly optimized");

    // Different geometries prevent the second board from using the first
    // board's refinement cache. A short attempt budget must still retain a
    // completed first-board checkpoint while a later board stays unchanged.
    auto prefixProblem=Tiles(2);auto slower=prefixProblem.Parts[0];slower.ID="larger-originals";slower.Quantity=6;
    slower.Geometry.Outer={{0,0},{2,0},{2,1.5},{0,1.5}};prefixProblem.Parts.push_back(std::move(slower));
    Result prefix;prefix.State=Status::Complete;prefix.GeometryValidated=true;
    prefix.Layouts={{prefixProblem.Stocks[0].ID,2,20,20,{
        {prefixProblem.Parts[0].ID,0,{4,4,0},51,false},{prefixProblem.Parts[0].ID,1,{9,8,0},52,false}},2}};
    Layout later{prefixProblem.Stocks[0].ID,6,20,20,{},18};
    for(std::size_t i=0;i<6;++i)later.Placements.push_back({prefixProblem.Parts[1].ID,i,
        {3+5.*static_cast<double>(i%3),3+5.*static_cast<double>(i/3),0},60+i,false});
    prefix.Layouts.push_back(std::move(later));Safe(prefixProblem,prefix);
    const auto laterSignature=BoardPoses(Physical(prefix,6));
    const auto firstArea=EnvelopeArea(prefixProblem,Physical(prefix,2));
    const auto ample=MakePlaneOptimizationStage(local,Constructor(false)).Factory()->Run(prefixProblem,prefix,Limits());
    Safe(prefixProblem,ample);
    Require(EnvelopeArea(prefixProblem,Physical(ample,2))<firstArea&&BoardPoses(Physical(ample,6))!=laterSignature,
        "both checkpoint fixtures must have real independent geometric improvements with sufficient budget");
    bool prefixRetained=false;std::size_t prefixBudget=0;
    for(std::size_t cap=4;cap<=512&&!prefixRetained;cap+=4)
    {
        auto shortBudget=Limits();shortBudget.MaxPlacementAttempts=cap;
        const auto limited=MakePlaneOptimizationStage(local,Constructor(false)).Factory()->Run(prefixProblem,prefix,shortBudget);
        Safe(prefixProblem,limited);
        Require(limited.Stats.PlacementAttempts<=cap,"checkpoint assessment overspent its attempt budget");
        if(EnvelopeArea(prefixProblem,Physical(limited,2))<firstArea&&BoardPoses(Physical(limited,6))==laterSignature)
        {prefixRetained=true;prefixBudget=cap;}
    }
    Require(prefixRetained,"later budget exhaustion discarded an already accepted first-board improvement");
    std::cout<<"multi-board-checkpoint: preserved-first-board-attempt-budget="<<prefixBudget<<'\n';

    auto refill=local;refill.RefillAcrossSheets=true;
    const auto refilled=MakePlaneOptimizationStage(refill,Constructor(false)).Factory()->Run(multiProblem,spread,Limits());
    Safe(multiProblem,refilled);Require(refilled.Layouts.size()==1,"multi-board optimization did not execute its optional cross-board refill");
    auto mix=local;mix.MixSheetRecipes=true;
    const auto mixed=MakePlaneOptimizationStage(mix,Constructor(false)).Factory()->Run(multiProblem,spread,Limits());
    Safe(multiProblem,mixed);Require(mixed.Layouts.size()==1,"multi-board optimization did not execute its optional joint recipe selection");

    const auto workflow=MakePlaneOptimizationFactory(Constructor(true),local,.25)()->Run(multiProblem,Limits());
    Safe(multiProblem,workflow);
    Require(workflow.Layouts.size()==2&&Has(workflow,"plane.optimize-job:")&&workflow.Stats.PlacementAttempts<=Limits().MaxPlacementAttempts,
        "optimization factory rejected a complete multi-board initial solution or lost the shared budget");
    const auto multiCancelled=MakePlaneOptimizationStage(local,Constructor(false)).Factory()->Run(multiProblem,spread,cancelled);
    Safe(multiProblem,multiCancelled);
    Require(multiCancelled.State==Status::Cancelled&&Poses(multiCancelled)==inputSignature,
        "cancelled multi-board workflow did not keep its complete initial layout");
    for(std::size_t cap:{0u,1u,8u})
    {
        auto options=Limits();options.MaxPlacementAttempts=cap;
        const auto limited=MakePlaneOptimizationStage(settings,Constructor(false)).Factory()->Run(multiProblem,spread,options);
        Safe(multiProblem,limited);Require(limited.Stats.PlacementAttempts<=cap&&limited.Layouts.size()<=spread.Layouts.size(),
            "multi-board budget cutoff published a partial or overspent result");
    }
    std::cout<<"PlaneOptimizationAdvancedTests passed: real bounded perturbations, monotone complete best, sparse physical identities, recipe refinement reuse and multi-board lifecycle\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneOptimizationAdvancedTests failed: "<<error.what()<<'\n';return 1;}
