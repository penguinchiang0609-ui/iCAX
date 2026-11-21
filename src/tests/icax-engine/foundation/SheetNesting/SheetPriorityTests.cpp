#include "SheetNesting.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
    class Supplied final:public IStrategy
    {Result Value;public:explicit Supplied(Result r):Value(std::move(r)){}Result Run(const Problem&,const Options&)override{return Value;}};
    Problem Input(std::size_t quantity=12)
    {
        Problem p;Part tile;tile.ID="tile";tile.Quantity=quantity;tile.FreeRotation=false;tile.AllowedAnglesDegrees={0};tile.Geometry.Outer={{0,0},{20,0},{20,20},{0,20}};p.Parts={tile};p.Stocks={{"stock",100,100,3,1,""}};return p;
    }
    Placement At(std::size_t i,double x,double y){return {"tile",i,{x,y,0},0,false};}
    Layout Board(std::size_t i){return {"stock",i,100,100,{},0};}
    Result DenseFront()
    {
        Result r;r.State=Status::Complete;r.Layouts={Board(0),Board(1)};
        for(std::size_t i=0;i<11;++i)r.Layouts[0].Placements.push_back(At(i,20.*(i%5),20.*(i/5)));
        r.Layouts[1].Placements={At(11,40,40)};return r;
    }
    Result RegularButSpread()
    {
        Result r;r.State=Status::Complete;r.Layouts={Board(0),Board(1)};
        for(std::size_t i=0;i<8;++i)r.Layouts[0].Placements.push_back(At(i,20.*(i%4),20.*(i/4)));
        for(std::size_t i=8;i<12;++i)r.Layouts[1].Placements.push_back(At(i,20.*((i-8)%2),20.*((i-8)/2)));return r;
    }
    void Register(StrategyRegistry& r,const std::string& id,const Result& value)
    {r.Register(id,[value]{return std::make_unique<Supplied>(value);});}
}
int main()try
{
    const auto p=Input();const auto dense=DenseFront(),spread=RegularButSpread();
    Require(Validate(p,dense).Valid&&Validate(p,spread).Valid,"original comparison geometry invalid");
    const auto a=EvaluateRemnants(p,dense),b=EvaluateRemnants(p,spread);
    Require(a.LastSheetFreeArea>b.LastSheetFreeArea&&a.LastSheetRegularArea<b.LastSheetRegularArea,"counterexample must trade total last-sheet free area against regularity");
    for(auto ranking:{RankingPolicy::RegularRemnant,RankingPolicy::LowThenLeft,RankingPolicy::CompactArea})
    {
        StrategyRegistry registry;Register(registry,"dense-front",dense);Register(registry,"regular-but-spread",spread);PortfolioOptions o;o.Ranking=ranking;
        // Compare the supplied poses themselves. Automatic refinement has its
        // own lifecycle tests and can legitimately change these tail areas.
        o.PerJob.EnableBasicCompaction=false;
        const auto result=RunPortfolio(p,o,registry);const auto expected=ranking==RankingPolicy::RegularRemnant?"regular-but-spread":"dense-front";
        Require(result.BestJob<result.Jobs.size()&&result.Jobs[result.BestJob].StrategyID==expected,
            "default must prioritize certified tail remnant while explicit alternatives retain consolidation priority");
    }
    std::cout<<"PASS default tail regularity first; explicit low/compact consolidation semantics preserved\n";
    Result one;one.State=Status::Complete;one.Layouts={Board(0)};for(std::size_t i=0;i<12;++i)one.Layouts[0].Placements.push_back(At(i,20.*(i%5),20.*(i/5)));
    StrategyRegistry registry;Register(registry,"two-big-tail",dense);Register(registry,"one-board",one);PortfolioOptions countOptions;countOptions.PerJob.EnableBasicCompaction=false;auto selected=RunPortfolio(p,countOptions,registry);
    Require(selected.Jobs[selected.BestJob].StrategyID=="one-board","one fewer board must beat a much larger tail remnant");
    std::cout<<"PASS used-sheet count precedes every remnant and consolidation score\n";
    auto reversed=dense;std::reverse(reversed.Layouts.begin(),reversed.Layouts.end());StrategyRegistry ordering;Register(ordering,"reverse",reversed);Options orderSettings;orderSettings.EnableBasicCompaction=false;const auto ordered=RunJob({"order","reverse",p,orderSettings},ordering);
    Require(ordered.Checked.Valid&&ordered.Checked.Complete&&ordered.Output.Layouts[0].StockInstance==0&&ordered.Output.Layouts[0].Placements.size()==11&&ordered.Output.Layouts[1].StockInstance==1&&ordered.Output.Layouts[1].Placements.size()==1,"actual output must place denser compatible boards before the tail, preserving stock identities");
    Require(ordered.Remnants.FinalLayoutIndices==std::vector<std::size_t>{1},"tail certificate must follow the corresponding reordered board");
    for(const auto& l:ordered.Output.Layouts)for(const auto& x:l.Placements)
    {const auto& expected=x.Instance==11?dense.Layouts[1].Placements[0]:dense.Layouts[0].Placements[x.Instance];Require(x.Transform.X==expected.Transform.X&&x.Transform.Y==expected.Transform.Y,"presentation order must not move original geometry");}
    std::cout<<"PASS board order and tail certificates preserve every original placement and physical stock\n";
    auto fragmented=spread;fragmented.Layouts[1].Placements.clear();for(std::size_t i=8;i<12;++i)fragmented.Layouts[1].Placements.push_back(At(i,20+40.*((i-8)%2),20+40.*((i-8)/2)));
    Require(Validate(p,fragmented).Valid,"fragmented-tail control invalid");const auto measured=EvaluateRemnants(p,fragmented);
    Require(measured.FinalLayoutIndices==std::vector<std::size_t>{1}&&measured.PerLayout[0].Area>measured.PerLayout[1].Area,"a preceding board with a larger regular rectangle must not be chosen as the logical last sheet");
    std::cout<<"PASS logical tail is selected by unused material area before rectangle shape\n";
    std::string selectedBeforeRename;
    for(bool rename:{false,true})
    {
        auto mixed=Input();mixed.Parts[0].ID="A";mixed.Parts[0].Material=rename?"zeta":"alpha";
        auto second=mixed.Parts[0];second.ID="B";second.Material=rename?"alpha":"zeta";mixed.Parts.push_back(second);
        mixed.Stocks={{"SA",100,100,2,1,mixed.Parts[0].Material},{"SB",100,100,2,1,mixed.Parts[1].Material}};
        const auto distribution=[&](std::size_t na,std::size_t nb)
        {
            Result r;r.State=Status::Complete;
            for(std::size_t material=0;material<2;++material)
            {
                const auto n=material?nb:na;const auto id=material?"B":"A";const auto stock=material?"SB":"SA";
                Layout front{stock,0,100,100,{},0},tail{stock,1,100,100,{},0};
                for(std::size_t i=0;i<n;++i){auto x=At(i,20.*(i%5),20.*(i/5));x.PartID=id;front.Placements.push_back(x);}
                for(std::size_t i=n;i<12;++i){auto x=At(i,20.*((i-n)%2),20.*((i-n)/2));x.PartID=id;tail.Placements.push_back(x);}
                r.Layouts.push_back(front);r.Layouts.push_back(tail);
            }
            return r;
        };
        StrategyRegistry materialNames;Register(materialNames,"front-11-8",distribution(11,8));Register(materialNames,"front-10-9",distribution(10,9));PortfolioOptions materialOptions;materialOptions.PerJob.EnableBasicCompaction=false;auto chosen=RunPortfolio(mixed,materialOptions,materialNames);
        const auto selected=chosen.Jobs[chosen.BestJob].StrategyID;
        if(!rename)selectedBeforeRename=selected;
        Require(selected==selectedBeforeRename,"renaming compatible material identities reversed the selected-tail remnant comparison");
    }
    std::cout<<"PASS material identity names cannot change the selected-tail remnant decision\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
