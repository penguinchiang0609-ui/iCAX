#include "SheetNesting.h"
#include "NestingAxisSafety.h"
#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
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
    void NominalWorldGeometry(const Problem& problem,const Result& result)
    {
        std::map<std::string,const Part*> parts;
        for(const auto& part:problem.Parts)parts.emplace(part.ID,&part);
        std::set<std::pair<std::string,std::size_t>> identities;
        for(const auto& layout:result.Layouts)
        {
            std::vector<AxisSafety::Geometry> geometries;
            for(const auto& placement:layout.Placements)
            {
                const auto& part=*parts.at(placement.PartID);
                Require(placement.Instance<part.Quantity&&identities.emplace(part.ID,placement.Instance).second,
                    "contact construction changed original instance inventory");
                const auto angles=part.AllowedAnglesDegrees.empty()?std::vector<double>{0}:part.AllowedAnglesDegrees;
                Require(std::any_of(angles.begin(),angles.end(),[&](double angle)
                    {return std::abs(std::remainder(placement.Transform.AngleDegrees-angle,360.))<1e-9;}),
                    "contact construction widened an original finite angle domain");
                const auto geometry=AxisSafety::Make(part.Geometry,placement.Transform);
                Require(AxisSafety::Inside(geometry,layout.Width,layout.Height,problem.EdgeMargin),
                    "actual written contact pose crossed the nominal usable sheet boundary");
                for(std::size_t j=0;j<geometries.size();++j)
                {
                    const auto verdict=AxisSafety::Pair(geometry,geometries[j],problem.PartGap,
                        problem.AllowCommonEdges,problem.Tolerance);
                    Require(verdict==AxisSafety::Verdict::Safe,
                        "actual written contact poses violate nominal original material or gap: "+
                        layout.Placements[j].PartID+" / "+placement.PartID);
                }
                geometries.push_back(geometry);
            }
        }
        std::size_t demanded=0;for(const auto& part:problem.Parts)demanded+=part.Quantity;
        Require(identities.size()==demanded,"nominal contact check did not cover every original instance");
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
        {
            Problem mirrored;mirrored.Tolerance=1e-7;mirrored.Stocks={{"sheet",4,4,1}};
            mirrored.Parts={Poly("self-triangle",{{0,0},{4,0},{0,4}},2)};
            mirrored.Parts[0].AllowedAnglesDegrees={0,270};
            mirrored.Parts[0].AllowMirror=true;
            Result witness;witness.State=Status::Complete;
            witness.Layouts={{"sheet",0,4,4,{{"self-triangle",0,{0,0,0,false},1,false},
                {"self-triangle",1,{4,4,270,true},1,false}},16}};
            Require(Validate(mirrored,witness).Valid&&Validate(mirrored,witness).Complete,
                "self mirror consumer fixture has no complete original-member witness");
            auto settings=Budget();settings.EnableCombinations=true;settings.MaxPatternMembers=2;settings.MaxPatterns=16;
            const auto check=[&](const Result& result,const char* route)
            {
                const auto valid=Validate(mirrored,result);
                if(!valid.Valid||!valid.Complete)
                {
                    std::cerr<<"mirrored consumer route="<<route<<" status="<<int(result.State)
                        <<" patterns="<<result.Stats.CandidatePatterns<<" attempts="<<result.Stats.PlacementAttempts
                        <<" placed="<<valid.PlacedCount<<'\n';
                    for(const auto& error:valid.Errors)std::cerr<<error<<'\n';
                    for(const auto& layout:result.Layouts)for(const auto& placed:layout.Placements)
                        std::cerr<<placed.PartID<<':'<<placed.Transform.X<<','<<placed.Transform.Y<<','
                            <<placed.Transform.AngleDegrees<<','<<placed.Transform.Mirrored<<'\n';
                }
                Require(valid.Valid&&valid.Complete,"shared mirrored pattern lost parity while being placed or committed");
                Require(std::any_of(result.Layouts[0].Placements.begin(),result.Layouts[0].Placements.end(),[](const auto& placed)
                    {return placed.PartID=="self-triangle"&&placed.Transform.Mirrored&&placed.Transform.AngleDegrees==270;}),
                    "mirror-only self complement was written back without its legal reflection and turn");
                NominalWorldGeometry(mirrored,result);
            };
            check(Detail::RunRules(mirrored,settings,Detail::RectScore::ShortSide,true,false),"shared-solver");
            check(Solve(mirrored,"rules.contact-large",settings).Output,"contact-large");
            mirrored.Parts[0].AllowMirror=false;
            for(const auto& result:{Detail::RunRules(mirrored,settings,Detail::RectScore::ShortSide,true,false),
                Solve(mirrored,"rules.contact-large",settings).Output})
            {
                const auto checked=Validate(mirrored,result);
                Require(checked.Valid&&!checked.Complete&&checked.PlacedCount==1,
                    "self-only contact construction widened a forbidden reflection");
            }
        }
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
        // This comparison evaluates the supplied poses themselves.
        settings.PerJob.EnableBasicCompaction=false;
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
        for(const bool combinations:{false,true})for(const double gap:{0.,.125})
        {
            Problem original;original.Tolerance=1e-6;original.PartGap=gap;
            auto anchor=Box("anchor",40.1,20.7,1);
            const double x=1000000.1,y=-999999.8;
            auto offset=Poly("offset",{{x,y},{x+20.1,y},{x+20.1,y+14.7},{x,y+14.7}},3);
            offset.AllowedAnglesDegrees={0,180};original.Parts={anchor,offset};
            original.Stocks={{"sheet",200,100,1,1,""}};
            auto options=Budget();options.EnableCombinations=combinations;
            // Inspect the producer's actual committed poses before a later
            // refinement can obscure the local-to-world conversion defect.
            options.EnableBasicCompaction=false;
            const auto constructed=Solve(original,"rules.contact-large",options);Complete(original,constructed);
            NominalWorldGeometry(original,constructed.Output);
        }
        std::cout<<"PASS actual committed contact poses preserve nominal material and gap after world registration\n";
        for(const std::string id:{"rules.contact-large","rules.contact-interleave","rules.contact-low-left",
            "rules.contact-scarce","rules.contact-remnant","rules.contact-balanced-remnant"})
        {
            Problem common;common.PartGap=.2;common.EdgeMargin=1;common.AllowCommonEdges=true;
            // The insert must enter the interior slot at (4,3). That origin
            // is neither a stock corner nor an ordinary +/-gap edge offset.
            auto channel=Poly("channel",{{0,0},{10,0},{10,4},{7,4},{7,2},{3,2},{3,4},{0,4}},1);
            channel.AllowedAnglesDegrees={0};
            auto insert=Box("insert",4,5,1);insert.AllowedAnglesDegrees={0};
            common.Parts={channel,insert};common.Stocks={{"sheet",12,9,1,1,""}};
            auto options=Budget();options.EnableBasicCompaction=false;
            const auto shared=Solve(common,id,options);
            const auto sharedCheck=Validate(common,shared.Output);
            Require(sharedCheck.Valid&&sharedCheck.Complete,
                id+" did not construct the common-edge channel fixture; placed="+std::to_string(sharedCheck.PlacedCount));
            Require(shared.Output.Layouts.size()==1,"common-edge construction opened another board");
            NominalWorldGeometry(common,shared.Output);

            common.AllowCommonEdges=false;
            const auto separated=Solve(common,id,options);
            const auto checked=Validate(common,separated.Output);
            Require(checked.Valid&&!checked.Complete&&checked.PlacedCount==1,
                "positive-gap construction filled a zero-clearance channel without common-edge permission");
            common.PartGap=0;
            const auto zeroChannel=Solve(common,id,options);
            const auto zeroChannelCheck=Validate(common,zeroChannel.Output);
            Require(zeroChannelCheck.Valid&&zeroChannelCheck.Complete,
                id+" did not retain zero-gap channel behavior");

            Problem point;point.PartGap=.2;point.EdgeMargin=1;point.AllowCommonEdges=true;
            auto triangle=Poly("triangle",{{0,0},{10,0},{0,4}},1);triangle.AllowedAnglesDegrees={0};
            auto square=Box("square",2.5,5,1);square.AllowedAnglesDegrees={0};
            point.Parts={triangle,square};point.Stocks={{"sheet",12,8,1,1,""}};
            const auto touching=Solve(point,id,options);
            const auto pointCheck=Validate(point,touching.Output);
            Require(pointCheck.Valid&&!pointCheck.Complete&&pointCheck.PlacedCount==1,
                "point contact incorrectly waived the original positive gap");
            point.PartGap=0;
            const auto zeroPoint=Solve(point,id,options);
            const auto zeroPointCheck=Validate(point,zeroPoint.Output);
            Require(zeroPointCheck.Valid&&zeroPointCheck.Complete,
                id+" did not retain zero-gap point-contact behavior");
        }
        std::cout<<"PASS positive-gap contact rules construct true seams and reject isolated point contact\n";
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
