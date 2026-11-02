#include "SheetNesting.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
    Part Poly(std::string id, std::vector<Point> points, std::size_t quantity)
    { Part p; p.ID=std::move(id); p.Geometry.Outer=std::move(points); p.Quantity=quantity; p.FreeRotation=false; return p; }
    Part Box(std::string id, double w, double h, std::size_t quantity)
    { return Poly(std::move(id),{{0,0},{w,0},{w,h},{0,h}},quantity); }
    Part Circle(std::string id,double r,std::size_t quantity)
    { Part p; p.ID=std::move(id); p.Geometry.Radius=r; p.Quantity=quantity; return p; }
    Options Budget()
    { Options o; o.TimeLimitSeconds=10; o.MaxPlacementAttempts=1000000; o.EnableCombinations=false; o.EnableGapFill=false; return o; }
    JobResult Solve(const Problem& p,const std::string& id,Options o=Budget())
    { return RunJob({"contact-test",id,p,std::move(o)},DefaultStrategies()); }
    void Complete(const Problem& p,const JobResult& r)
    {
        auto checked=Validate(p,r.Output);
        for(const auto& e:checked.Errors)std::cerr<<e<<'\n';
        Require(checked.Valid&&checked.Complete,"expected complete, valid contact layout");
    }
    class SuppliedLayout final : public IStrategy
    {
    public:
        explicit SuppliedLayout(Result result) : Value(std::move(result)) {}
        Result Run(const Problem&,const Options&) override { return Value; }
    private:
        Result Value;
    };
}
int main()
{
    try
    {
        Problem p;
        p.Parts={Poly("P80",{{0,0},{80,0},{100,40},{20,40}},2),Poly("P60",{{0,0},{60,0},{80,40},{20,40}},2)};
        p.Stocks={{"sheet",180,80,1,1,""}};
        const auto compactContact=Solve(p,"rules.contact-interleave"); Complete(p,compactContact);
        Require(compactContact.Checked.UsedEnvelopeArea<=12800+1e-4,"interleaving should reproduce the human 180-to-160 width improvement");
        std::cout<<"PASS interleaved specifications improve the verified human counterexample\n";
        p.Parts={Box("rectangle",60,40,2),Circle("circle",10,4)};p.Stocks={{"sheet",100,80,1,1,""}};
        Complete(p,Solve(p,"rules.contact-large"));
        std::cout<<"PASS different basic shape families share real contact space\n";
        p.Parts={Poly("notch",{{0,0},{100,0},{100,20},{40,20},{40,80},{0,80}},1),Box("complement",60,60,1),Circle("circle",10,3)};
        p.Stocks={{"sheet",120,80,1,1,""}};
        Complete(p,Solve(p,"rules.contact-large"));
        auto motifs=Budget();motifs.EnableCombinations=true;
        Complete(p,Solve(p,"rules.contact-large",motifs));
        std::cout<<"PASS complex-to-basic complement and circular neighbours work with and without rigid motifs\n";
        for(const std::string id:{"rules.contact-large","rules.contact-interleave","rules.contact-low-left","rules.contact-scarce"})
        {
            auto options=Budget();options.MaxPlacementAttempts=20;
            auto r=Solve(p,id,options);Require(Validate(p,r.Output).Valid,"attempt-cut result must conserve geometry and inventory");
            options=Budget();options.Cancel=[] {return true;};r=Solve(p,id,options);
            Require(r.Output.State==Status::Cancelled&&Validate(p,r.Output).Valid,"cancelled contact job accounting");
        }
        std::cout<<"PASS every independent contact policy preserves partial and cancelled state\n";
        p.Parts={Box("rotatable",80,20,1)};p.Parts[0].AllowedAnglesDegrees={0,90};p.Stocks={{"sheet",200,100,1,1,""}};
        for(const std::string id:{"rules.contact-remnant","rules.contact-balanced-remnant"})
        {
            auto oriented=Solve(p,id);Complete(p,oriented);
            Require(std::abs(oriented.Remnants.LastSheetRegularArea-18000)<.2,"remnant policies must compare allowed orientations of the selected demand");
        }
        std::cout<<"PASS remnant preservation compares legal orientations instead of committing the first angle\n";
        p.Parts={Box("rectangle",80,20,2)};p.Stocks={{"sheet",200,100,2,1,""}};
        Result compact,low;compact.State=low.State=Status::Complete;
        Layout a{"sheet",0,200,100,{{"rectangle",0,{0,0,0},1,false},{"rectangle",1,{0,20,0},2,false}},3200};
        Layout b{"sheet",0,200,100,{{"rectangle",0,{0,0,0},1,false},{"rectangle",1,{90,0,0},2,false}},3200};
        compact.Layouts={a};low.Layouts={b};
        StrategyRegistry registry;
        registry.Register("compact",[compact] {return std::make_unique<SuppliedLayout>(compact);});
        registry.Register("low",[low] {return std::make_unique<SuppliedLayout>(low);});
        PortfolioOptions settings;settings.PerJob=Budget();settings.Ranking=RankingPolicy::LowThenLeft;
        auto ranked=RunPortfolio(p,settings,registry);
        Require(ranked.Jobs[ranked.BestJob].StrategyID=="low","height-first must not be silently replaced by area scoring");
        settings.Ranking=RankingPolicy::CompactArea;ranked=RunPortfolio(p,settings,registry);
        Require(ranked.Jobs[ranked.BestJob].StrategyID=="compact","area mode should select the smaller envelope despite greater height");
        Result split=low;split.Layouts[0].Placements.resize(1);
        auto second=split.Layouts[0];second.StockInstance=1;second.Placements[0].Instance=1;split.Layouts.push_back(second);
        StrategyRegistry boards;
        boards.Register("two-boards",[split] {return std::make_unique<SuppliedLayout>(split);});
        boards.Register("one-board",[low] {return std::make_unique<SuppliedLayout>(low);});
        ranked=RunPortfolio(p,settings,boards);
        Require(ranked.Jobs[ranked.BestJob].StrategyID=="one-board","board count must precede compactness");
        std::cout<<"PASS explicit ranking policies preserve the board-first requirement and geometric tradeoffs\n";
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
