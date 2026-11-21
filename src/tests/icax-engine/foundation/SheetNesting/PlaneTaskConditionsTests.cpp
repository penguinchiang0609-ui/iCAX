#include "SheetNestingPlaneTaskConditions.h"
#include "SheetNestingPlaneStrategies.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool condition,const char* message)
{if(!condition)throw std::runtime_error(message);}
bool SameRing(const std::vector<Point>& a,const std::vector<Point>& b)
{
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(a[i].X!=b[i].X||a[i].Y!=b[i].Y)return false;
    return true;
}
bool SameShape(const Shape& a,const Shape& b)
{
    if(a.Radius!=b.Radius||a.Center.X!=b.Center.X||a.Center.Y!=b.Center.Y||
        !SameRing(a.Outer,b.Outer)||a.Holes.size()!=b.Holes.size())return false;
    for(std::size_t i=0;i<a.Holes.size();++i)if(!SameRing(a.Holes[i],b.Holes[i]))return false;
    return true;
}
bool SameProblem(const Problem& a,const Problem& b)
{
    if(a.PartGap!=b.PartGap||a.EdgeMargin!=b.EdgeMargin||a.AllowCommonEdges!=b.AllowCommonEdges||
        a.Tolerance!=b.Tolerance||a.Parts.size()!=b.Parts.size()||a.Stocks.size()!=b.Stocks.size())return false;
    for(std::size_t i=0;i<a.Parts.size();++i)
    {
        const auto& x=a.Parts[i];const auto& y=b.Parts[i];
        if(x.ID!=y.ID||x.Material!=y.Material||x.Quantity!=y.Quantity||x.FreeRotation!=y.FreeRotation||
            x.AllowedAnglesDegrees!=y.AllowedAnglesDegrees||!SameShape(x.Geometry,y.Geometry))return false;
    }
    for(std::size_t i=0;i<a.Stocks.size();++i)
    {
        const auto& x=a.Stocks[i];const auto& y=b.Stocks[i];
        if(x.ID!=y.ID||x.Material!=y.Material||x.Quantity!=y.Quantity||x.Cost!=y.Cost||x.Width!=y.Width||x.Height!=y.Height)return false;
    }
    return true;
}
Part Rectangle()
{
    Part part;part.ID="original";part.Material="steel";part.Quantity=1;part.FreeRotation=false;
    part.AllowedAnglesDegrees={17,197};part.Geometry.Outer={{-3,-1},{15,-1},{15,11},{-3,11}};return part;
}
Problem Original()
{
    Problem p;p.PartGap=8;p.EdgeMargin=3;p.Tolerance=1e-7;
    auto frame=Rectangle();frame.Quantity=3;
    frame.Geometry.Holes={{{1,1},{1,3},{4,3},{4,1}}};
    Part circle;circle.ID="analytic-circle";circle.Material="aluminium";circle.Quantity=5;
    circle.FreeRotation=false;circle.AllowedAnglesDegrees={90};circle.Geometry.Radius=3;
    circle.Geometry.Center={-4.25,10.5};
    p.Parts={frame,circle};p.Stocks={{"steel-stock",8000,4000,3,7.5,"steel"},{"aluminium-stock",4000,2000,0,2.5,"aluminium"}};
    return p;
}
void PreservesOriginalMaterialAndInventory()
{
    const auto original=Original();auto applied=original;ApplyPlaneTaskConditions(applied);
    Require(applied.PartGap==.2&&applied.EdgeMargin==.5&&applied.AllowCommonEdges,"shared task process conditions were not applied");
    Require(applied.Tolerance==original.Tolerance&&applied.Parts.size()==original.Parts.size(),"task conditions changed original tolerance or demand rows");
    for(std::size_t i=0;i<applied.Parts.size();++i)
    {
        const auto& part=applied.Parts[i];const auto& source=original.Parts[i];
        Require(part.ID==source.ID&&part.Quantity==source.Quantity&&part.Material==source.Material,
            "task conditions changed original identity, demand or material");
        Require(SameShape(part.Geometry,source.Geometry),"task conditions scaled, mirrored or replaced original polygon/hole/circle geometry");
        Require(part.FreeRotation&&part.AllowedAnglesDegrees.empty(),"source finite angle domain still restricts unrestricted task rotation");
    }
    for(std::size_t i=0;i<applied.Stocks.size();++i)
    {
        const auto& stock=applied.Stocks[i];const auto& source=original.Stocks[i];
        Require(stock.Width==2440&&stock.Height==1220,"standard sheet dimensions were not applied to every material stock");
        Require(stock.ID==source.ID&&stock.Material==source.Material&&stock.Cost==source.Cost&&stock.Quantity==source.Quantity,
            "default task conditions invented stock, cost or material");
    }
    auto again=applied;ApplyPlaneTaskConditions(again);
    Require(SameProblem(applied,again),"shared conditions are not idempotent");
    Require(!original.Parts[0].FreeRotation&&original.Stocks[0].Width==8000,"conditions changed a separate source copy");
    auto oversized=original;oversized.Parts[0].Geometry.Outer={{0,0},{5000,0},{5000,3000},{0,3000}};
    const auto sourceShape=oversized.Parts[0].Geometry;ApplyPlaneTaskConditions(oversized);
    Require(SameShape(sourceShape,oversized.Parts[0].Geometry),"oversized originals were silently fitted to the new stock");
}
void ExplicitInventoryAndInvalidConditions()
{
    auto input=Original();PlaneTaskConditions conditions;conditions.StockQuantity=7;
    ApplyPlaneTaskConditions(input,conditions);
    Require(input.Stocks[0].Quantity==7&&input.Stocks[1].Quantity==7,"explicit caller inventory was ignored");
    conditions.StockQuantity=0;ApplyPlaneTaskConditions(input,conditions);
    Require(input.Stocks[0].Quantity==0&&input.Stocks[1].Quantity==0,"explicit zero inventory was replaced");
    for(int invalid=0;invalid<4;++invalid)
    {
        auto source=Original();const auto before=source;PlaneTaskConditions bad;
        if(invalid==0)bad.StockWidth=std::numeric_limits<double>::infinity();
        if(invalid==1)bad.PartGap=-.2;
        if(invalid==2)bad.EdgeMargin=610;
        if(invalid==3)bad.StockQuantity=100001;
        bool rejected=false;try{ApplyPlaneTaskConditions(source,bad);}catch(const std::invalid_argument&){rejected=true;}
        Require(rejected&&SameProblem(source,before),"invalid conditions changed the original input before rejection");
    }
}
struct Inspections
{std::mutex Mutex;std::set<std::string> Routes;};
class InspectedConstructor final:public IStrategy
{
    std::unique_ptr<IStrategy> Inner;
    const Problem& Expected;
    std::shared_ptr<Inspections> Inspected;
    std::string Route;
public:
    InspectedConstructor(std::unique_ptr<IStrategy> inner,const Problem& expected,
        std::shared_ptr<Inspections> inspected,std::string route)
        :Inner(std::move(inner)),Expected(expected),Inspected(std::move(inspected)),Route(std::move(route)){}
    bool OwnsRefinementLifecycle()const noexcept override{return Inner->OwnsRefinementLifecycle();}
    bool IsPureConstruction()const noexcept override{return Inner->IsPureConstruction();}
    Result Run(const Problem& problem,const Options& options)override
    {
        Require(SameProblem(problem,Expected),"constructor received different task conditions or originals");
        auto result=Inner->Run(problem,options);
        Require(SameProblem(problem,Expected),"constructor mutated the shared original task");
        Require(Validate(problem,result).Valid,"constructor produced an invalid result under the shared process conditions");
        std::lock_guard guard(Inspected->Mutex);Inspected->Routes.insert(Route);return result;
    }
};
void EveryRegisteredConstructorReceivesTheSameTask()
{
    Problem task;task.Parts={Rectangle()};task.Stocks={{"finite-steel-stock",100,100,2,3,"steel"}};
    ApplyPlaneTaskConditions(task);
    StrategyRegistry originals;RegisterPlaneStrategies(originals);
    StrategyRegistry inspected;std::vector<std::string> ids;
    const auto inspection=std::make_shared<Inspections>();
    for(const auto& id:originals.IDs())
    {
        if(id=="plane.initial"||id=="plane.nfp-all")continue;
        ids.push_back(id);
        inspected.Register(id,[&,id]{return std::make_unique<InspectedConstructor>(originals.Create(id),task,inspection,id);});
    }
    Require(ids.size()==27,"registered initial-constructor coverage changed; update the task propagation contract");
    ParallelConstructionSettings scheduling;scheduling.Workers=4;scheduling.FairScheduling=true;
    Options options;options.TimeLimitSeconds=20;options.MaxPlacementAttempts=1000000;
    options.EnableBasicCompaction=false;options.EnableCombinations=false;
    const auto output=MakeParallelConstructionPortfolioFactory(inspected,ids,scheduling)()->Run(task,options);
    Require(inspection->Routes.size()==ids.size(),"not every registered initial constructor received and retained the same task conditions");
    Require(Validate(task,output).Complete,"common process conditions lost an elementary feasible original task");
}
}
int main()try
{
    PreservesOriginalMaterialAndInventory();ExplicitInventoryAndInvalidConditions();EveryRegisteredConstructorReceivesTheSameTask();
    std::cout<<"PlaneTaskConditionsTests: original geometry/demand/inventory and all 27 constructors passed\n";return 0;
}
catch(const std::exception& error)
{std::cerr<<"PlaneTaskConditionsTests: "<<error.what()<<'\n';return 1;}
